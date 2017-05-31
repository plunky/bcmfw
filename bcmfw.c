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

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/syslimits.h>
#include <dev/usb/usb.h>
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define USB_VENDOR_BROADCOM		0x0a5c
#define USB_PRODUCT_BROADCOM_BCM2033NF	0x2033

int main(int, char *[]);
void usage(void);
int open_ep(int, int);
void write_file(const char *);
int query_dev(void);

/* Default filenames */
const char *dv = NULL;
const char *md = "BCM2033-MD.hex";
const char *fw = "BCM2033-FW.bin";

/* Endpoint descriptor */
int intr = -1;
int bulk = -1;

/* options */
int quiet = 0;

int
main(int argc, char *argv[])
{
	char buf[10];
	int ch;

	while ((ch = getopt(argc, argv, "f:hm:")) != -1) {
		switch (ch) {
		case 'f': /* firmware file */
			fw = optarg;
			break;

		case 'm': /* Mini-driver */
			md = optarg;
			break;

		case 'q':
			quiet = 1;
			break;

		case 'h':
		default:
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	while (argc-- > 0) {
		dv = *argv++;

		if (!query_dev())
			continue;

		write_file(md);

		usleep(10);

		if (write(bulk, "#", 1) < 1)
			errx(EXIT_FAILURE, "%s: write # failed", dv);

		if (read(intr, buf, sizeof(buf)) < 1)
			errx(EXIT_FAILURE, "%s: read # failed", dv);

		if (buf[0] != '#')
			errx(EXIT_FAILURE, "%s: memory select failed", dv);

		write_file(fw);

		if (read(intr, buf, sizeof(buf)) < 1)
			err(EXIT_FAILURE, "%s: read . failed", dv);

		if (buf[0] != '.')
			errx(EXIT_FAILURE, "%s: firmware load failed", dv);

		close(intr);
		close(bulk);

		printf("%s: loaded\n", dv);
	}

	exit(EXIT_SUCCESS);
}

void 
usage(void)
{
	fprintf(stderr,
		"Usage: %s [-q] [-f firmware] [-m mini-driver] [ugenN ...]\n",
		getprogname());

	exit(EXIT_FAILURE);
}

/*
 * Open ugen endpoint
 */
int
open_ep(int ee, int flags)
{
	char path[PATH_MAX];
	int fd;

	snprintf(path, sizeof(path), "/dev/%s.%02d", dv, ee);
	fd = open(path, flags);
	if (fd < 0)
		err(EXIT_FAILURE, "%s", path);

	return fd;
}

/*
 * Copy file to bulk endpoint
 */
void
write_file(const char *name)
{
	char buf[1024];
	size_t len;
	int fd;

	if ((fd = open(name, O_RDONLY)) < 0)
		err(EXIT_FAILURE, "%s", name);

	while ((len = read(fd, buf, sizeof(buf))) > 0) {
		if ((len = write(bulk, buf, len)) < 0)
			err(EXIT_FAILURE, "write");
	}

	close(fd);
}

/*
 * Open Control endpoint and verify VendorID & ProductID, then
 * find the interrupt and bulk-in device numbers for Interface 0,
 * Configuration 1 and open them.
 */
int
query_dev(void)
{
	usb_device_descriptor_t	dev;
	struct usb_interface_desc iface;
	int ctl, n, intr_ep, bulk_ep;

	ctl = open_ep(0, O_RDWR);

	/*
	 * Check we have the right device
	 */
	if (ioctl(ctl, USB_GET_DEVICE_DESC, &dev) < 0)
		err(EXIT_FAILURE, "USB_GET_DEVICE_DESC");

	if (UGETW(dev.idVendor) != USB_VENDOR_BROADCOM
	    || UGETW(dev.idProduct) != USB_PRODUCT_BROADCOM_BCM2033NF) {
		if (!quiet)
			fprintf(stderr, "%s: not Broadcom 2033NF", dv);

		return 0;
	}

	/*
	 * Set Configuration # 1
	 */
	n = 1;
	if (ioctl(ctl, USB_SET_CONFIG, &n) < 0)
		err(EXIT_FAILURE, "%s: USB_SET_CONFIG", dv);

	/*
	 * Interface 0
	 */
	iface.uid_config_index = USB_CURRENT_CONFIG_INDEX;
	iface.uid_interface_index = 0;
	iface.uid_alt_index = USB_CURRENT_ALT_INDEX;
	if (ioctl(ctl, USB_GET_INTERFACE_DESC, &iface) < 0)
		err(EXIT_FAILURE, "%s: USB_GET_INTERFACE_DESC", dv);

	intr_ep = bulk_ep = -1;

	for (n = 0 ; n < iface.uid_desc.bNumEndpoints ; n++) {
		struct usb_endpoint_desc ep;
		int dir, type;

		ep.ued_config_index = USB_CURRENT_CONFIG_INDEX;
		ep.ued_interface_index = iface.uid_interface_index;
		ep.ued_alt_index = USB_CURRENT_ALT_INDEX;
		ep.ued_endpoint_index = n;
		if (ioctl(ctl, USB_GET_ENDPOINT_DESC, &ep) < 0)
			err(EXIT_FAILURE, "%s: USB_GET_ENDPOINT_DESC", dv);

		dir = UE_GET_DIR(ep.ued_desc.bEndpointAddress);
		type = UE_GET_XFERTYPE(ep.ued_desc.bmAttributes);

		if (dir == UE_DIR_IN && type == UE_INTERRUPT)
			intr_ep = UE_GET_ADDR(ep.ued_desc.bEndpointAddress);

		if (dir == UE_DIR_OUT && type == UE_BULK)
			bulk_ep = UE_GET_ADDR(ep.ued_desc.bEndpointAddress);
	}

	if (intr_ep == -1)
		errx(EXIT_FAILURE, "%s: Interrupt Endpoint not found", dv);

	intr = open_ep(intr_ep, O_RDONLY);

	if (bulk_ep == -1)
		errx(EXIT_FAILURE, "%s: Bulk Out Endpoint not found", dv);

	bulk = open_ep(bulk_ep, O_WRONLY);

	close(ctl);
	return 1;
}
