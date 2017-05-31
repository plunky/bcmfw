/*-
 * Copyright (c) 2006 Itronix Inc.
 * All rights reserved.
 *
 * Written by Iain Hibbert for Itronix Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of Itronix Inc. may not be used to endorse
 *    or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY ITRONIX INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL ITRONIX INC. BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * If a ugen device name was given, thats an indication that this is one
 * of the older BCM2033 devices, which require firmware loading over the
 * USB endpoint directly. Probe the device and update firmware if the
 * Vendor/Prodct IDs match.
 */

#include <sys/types.h>
#include <sys/ioctl.h>

#include <dev/usb/usb.h>

#include <err.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "bcmfw.h"

#define USB_VENDOR_BROADCOM		0x0a5c
#define USB_PRODUCT_BROADCOM_BCM2033NF	0x2033

/* Default filenames */
const char *bcm2033_fw = "BCM2033-FW.bin";
const char *bcm2033_md = "BCM2033-MD.hex";

/* Endpoint descriptor */
static int intr = -1;
static int bulk = -1;

/*
 * Open ugen endpoint
 */
static int
ugen_open_ep(const char *dv, int ee, int flags)
{
	char path[PATH_MAX];
	int fd;

	snprintf(path, sizeof(path), "/dev/%s.%02d", dv, ee);
	fd = open(path, flags);
	if (fd == -1)
		warn("%s", path);

	return fd;
}

/*
 * Copy file to bulk endpoint
 */
static void
ugen_write_file(const char *name)
{
	char buf[1024];
	ssize_t len;
	int fd;

	if ((fd = open(name, O_RDONLY)) == -1)
		err(EXIT_FAILURE, "%s", name);

	for (;;) {
		len = read(fd, buf, sizeof(buf));
		if (len == 0)
			break;
		if (len == -1)
			err(EXIT_FAILURE, "read");

		len = write(bulk, buf, len);
		if (len == -1)
			err(EXIT_FAILURE, "write");
	}

	close(fd);
}

/*
 * Open Control endpoint and verify VendorID & ProductID, then
 * find the interrupt and bulk-in device numbers for Interface 0,
 * Configuration 1 and open them.
 */
static bool
ugen_query_dev(const char *dv)
{
	usb_device_descriptor_t	dev;
	struct usb_interface_desc iface;
	int ctrl, n, intr_ep, bulk_ep;

	ctrl = ugen_open_ep(dv, 0, O_RDWR);
	if (ctrl == -1)
		return false;

	/*
	 * Check we have the right device
	 */
	if (ioctl(ctrl, USB_GET_DEVICE_DESC, &dev) == -1)
		err(EXIT_FAILURE, "%s: USB_GET_DEVICE_DESC", dv);

	if (UGETW(dev.idVendor) != USB_VENDOR_BROADCOM
	    || UGETW(dev.idProduct) != USB_PRODUCT_BROADCOM_BCM2033NF) {
		if (verbose > 0)
			warnx("%s: not Broadcom 2033NF", dv);

		close(ctrl);
		return false;
	}

	/*
	 * Set Configuration # 1
	 */
	n = 1;
	if (ioctl(ctrl, USB_SET_CONFIG, &n) == -1)
		err(EXIT_FAILURE, "%s: USB_SET_CONFIG", dv);

	/*
	 * Interface 0
	 */
	iface.uid_config_index = USB_CURRENT_CONFIG_INDEX;
	iface.uid_interface_index = 0;
	iface.uid_alt_index = USB_CURRENT_ALT_INDEX;
	if (ioctl(ctrl, USB_GET_INTERFACE_DESC, &iface) == -1)
		err(EXIT_FAILURE, "%s: USB_GET_INTERFACE_DESC", dv);

	intr_ep = bulk_ep = -1;

	for (n = 0 ; n < iface.uid_desc.bNumEndpoints ; n++) {
		struct usb_endpoint_desc ep;
		int dir, type;

		ep.ued_config_index = USB_CURRENT_CONFIG_INDEX;
		ep.ued_interface_index = iface.uid_interface_index;
		ep.ued_alt_index = USB_CURRENT_ALT_INDEX;
		ep.ued_endpoint_index = n;
		if (ioctl(ctrl, USB_GET_ENDPOINT_DESC, &ep) == -1)
			err(EXIT_FAILURE, "%s: USB_GET_ENDPOINT_DESC", dv);

		dir = UE_GET_DIR(ep.ued_desc.bEndpointAddress);
		type = UE_GET_XFERTYPE(ep.ued_desc.bmAttributes);

		if (dir == UE_DIR_IN && type == UE_INTERRUPT)
			intr_ep = UE_GET_ADDR(ep.ued_desc.bEndpointAddress);

		if (dir == UE_DIR_OUT && type == UE_BULK)
			bulk_ep = UE_GET_ADDR(ep.ued_desc.bEndpointAddress);
	}

	close(ctrl);

	if (intr_ep == -1)
		errx(EXIT_FAILURE, "%s: Interrupt Endpoint not found", dv);

	if (bulk_ep == -1)
		errx(EXIT_FAILURE, "%s: Bulk Out Endpoint not found", dv);

	intr = ugen_open_ep(dv, intr_ep, O_RDONLY);
	if (intr == -1)
		return false;

	bulk = ugen_open_ep(dv, bulk_ep, O_WRONLY);
	if (bulk == -1)
		return false;

	return true;
}

void
check_ugen(const char *dv)
{
	char buf[10];

	if (ugen_query_dev(dv)) {
		ugen_write_file(bcm2033_md);

		usleep(100);

		if (write(bulk, "#", 1) < 1)
			errx(EXIT_FAILURE, "%s: write `#' failed", dv);

		if (read(intr, buf, sizeof(buf)) < 1)
			errx(EXIT_FAILURE, "%s: read `#' failed", dv);

		if (buf[0] != '#')
			errx(EXIT_FAILURE, "%s: memory select failed", dv);

		ugen_write_file(bcm2033_fw);
		usleep(250);

		if (read(intr, buf, sizeof(buf)) < 1)
			err(EXIT_FAILURE, "%s: read `.' failed", dv);

		if (buf[0] != '.')
			errx(EXIT_FAILURE, "%s: firmware load failed", dv);

		printf("%s: loaded\n", dv);
	}

	if (intr != -1) {
		close(intr);
		intr = -1;
	}

	if (bulk != -1) {
		close(bulk);
		bulk = -1;
	}
}
