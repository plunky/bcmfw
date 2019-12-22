/*-
 * Copyright (c) 2016 Iain Hibbert
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Modern Broadcom Bluetooth chips find their initial firmware instruction
 * from an internal PROM after power-up. Once this initial firmware is
 * running, it can be upgraded over the Bluetooth HCI interface with
 * "Patch RAM" data files provided by Broadcom.
 *
 * These chips can appear under various USB vendor/product ID combinations,
 * or perhaps in other devices. 
 *
 * The Patch RAM data files can only be found in the Broadcom drivers for
 * Windows and redistribution is not permitted by the licence supplied.
 *
 * The drivers are often provided as a self extracting archive so access
 * to a Windows machine may be required to install the driver before the
 * firmware files can be accessed.
 */

#include <sys/types.h>
#include <sys/ioctl.h>

#include <bluetooth.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>
#include <unistd.h>
#include <util.h>

#include "bcmfw.h"

static int		hci;	/* HCI socket */
static struct bt_devreq	req;	/* HCI requests */
static struct btreq	btr;	/* HCI ioctl request */

#define	REQ_TIMEOUT	2

static bool		Enabled;	/* if the device was enabled */
static uint16_t		Manufacturer;	/* device Manufacturer */
static uint16_t		Revision;	/* HCI revision */
static uint16_t		VendorID;	/* USB VendorID */
static uint16_t		ProductID;	/* USB ProductID */
static uint16_t		BuildNum;	/* Broadcom Firmware version */
static bdaddr_t		bdaddr;		/* Bluetooth Device Address */
static struct ihex *	Firmware;	/* loaded firmware */

#define BLUETOOTH_MANUFACTURER_BROADCOM		15

#define	USB_VENDOR_BROADCOM			0x0a5c

/*
 * Known Broadcom vendor commands
 */
#define	BCM_CMD_WRITE_BDADDR			0xfc01
#define	BCM_CMD_UPDATE_UART_BAUD_RATE		0xfc18
#define	BCM_CMD_SET_SLEEPMODE_PARAM		0xfc27
#define BCM_CMD_DOWNLOAD_MINIDRIVER		0xfc2e
#define BCM_CMD_ENABLE_USBHID_EMULATION		0xfc3b
#define	BCM_CMD_WRITE_UART_CLOCK_SETTING	0xfc45
#define BCM_CMD_WRITE_RAM			0xfc4c
#define BCM_CMD_LAUNCH_RAM			0xfc4e
#define BCM_CMD_WAKEUP				0xfc53
#define BCM_CMD_READ_USB_PRODUCT		0xfc5a
#define BCM_CMD_READ_VERBOSE_CONFIG		0xfc79

static void __unused
hci_read_bdaddr(void)
{
	uint8_t rp[7];	/* [0]	u8	status
			   [1]	bdaddr	addr		*/

	req = (struct bt_devreq) {
		.opcode = HCI_CMD_READ_BDADDR,
		.rparam = &rp,
		.rlen = sizeof(rp)
	};

	if (bt_devreq(hci, &req, REQ_TIMEOUT) == -1)
		err(EXIT_FAILURE, "HCI Read BDADDR");

	if (req.rlen != sizeof(rp) || rp[0] > 0)
		errx(EXIT_FAILURE, "HCI Read BDADDR: failed");

	bdaddr_copy(&bdaddr, (bdaddr_t *)&rp[1]);

	if (verbose > 0) {
		printf("Read BDADDR:\n");
		printf("  Address %s\n", bt_ntoa(&bdaddr, NULL));
		printf("\n");
	}
}

static void
hci_read_local_version(void)
{
	uint8_t rp[9];	/* [0]	u8	status
			   [1]	u8	HCIVersion
			   [2]	u16	HCIRevision
			   [4]	u8	LMPVersion
			   [5]	u16	Manufacturer
			   [7]	u16	LMPSubversion	*/

	req = (struct bt_devreq) {
		.opcode = HCI_CMD_READ_LOCAL_VER,
		.rparam = &rp,
		.rlen = sizeof(rp)
	};

	if (bt_devreq(hci, &req, REQ_TIMEOUT) == -1)
		err(EXIT_FAILURE, "HCI Read Local Version");

	if (req.rlen != sizeof(rp) || rp[0] > 0)
		errx(EXIT_FAILURE, "HCI Read Local Version: failed");

	Manufacturer = le16dec(&rp[5]);
	Revision = le16dec(&rp[2]);

	if (verbose > 0) {
		printf("Read Local Version:\n");
		printf("  Manufacturer %u\n", Manufacturer);
		printf("  HCI version 0x%02x rev 0x%04x\n", rp[1], Revision);
		printf("  LMP version 0x%02x sub 0x%04x\n", rp[4], le16dec(&rp[7]));
		printf("\n");
	}
}

static void __unused
hci_reset(void)
{
	uint8_t rp[1];	/* [0] u8	status	*/

	req = (struct bt_devreq) {
		.opcode = HCI_CMD_RESET,
		.rparam = &rp,
		.rlen = sizeof(rp)
	};

	if (bt_devreq(hci, &req, REQ_TIMEOUT) == -1)
		err(EXIT_FAILURE, "HCI Reset");

	if (req.rlen != sizeof(rp) || rp[0] > 0)
		errx(EXIT_FAILURE, "HCI Reset: failed");
}

static void __unused
bcm_write_bdaddr(void)
{
	uint8_t rp[1];	/* [0] u8	status	*/

	req = (struct bt_devreq) {
		.opcode = BCM_CMD_WRITE_BDADDR,
		.cparam = &bdaddr,
		.clen = sizeof(bdaddr),
		.rparam = &rp,
		.rlen = sizeof(rp)
	};

	if (bt_devreq(hci, &req, REQ_TIMEOUT) == -1)
		err(EXIT_FAILURE, "Write BDADDR");

	if (req.rlen != sizeof(rp) || rp[0] > 0)
		errx(EXIT_FAILURE, "Write BDADDR: failed");

	if (verbose > 0) {
		printf("Write BDADDR:\n");
		printf("  Address %s\n", bt_ntoa(&bdaddr, NULL));
		printf("\n");
	}
}

static void
bcm_read_usb_product(void)
{
	uint8_t	rp[5];	/* [0]	u8	status
			   [1]	u16	VendorID
			   [3]	u16	ProductID	*/

	req = (struct bt_devreq) {
		.opcode = BCM_CMD_READ_USB_PRODUCT,
		.rparam = &rp,
		.rlen = sizeof(rp)
	};

	if (bt_devreq(hci, &req, REQ_TIMEOUT) == -1)
		err(EXIT_FAILURE, "Read USB Product");

	if (req.rlen != sizeof(rp) || rp[0] > 0)
		errx(EXIT_FAILURE, "Read USB Product: failed");

	VendorID = le16dec(&rp[1]);
	ProductID = le16dec(&rp[3]);

	if (verbose > 0) {
		printf("Read USB Product:\n");
		printf("  VendorID 0x%04x\n", VendorID);
		printf("  ProductID 0x%04x\n", ProductID);
		printf("\n");
	}
}

static void
bcm_read_verbose_config(void)
{
	uint8_t rp[7];	/* [0]	u8	status
			   [1]	u8	ChipID
			   [2]	u8	TargetID
			   [3]	u16	BuildBase
			   [5]	u16	BuildNum	*/

	req = (struct bt_devreq) {
		.opcode = BCM_CMD_READ_VERBOSE_CONFIG,
		.rparam = &rp,
		.rlen = sizeof(rp)
	};

	if (bt_devreq(hci, &req, REQ_TIMEOUT) == -1)
		err(EXIT_FAILURE, "Read Verbose Config");

	if (req.rlen != sizeof(rp) || rp[0] > 0)
		errx(EXIT_FAILURE, "Read Verbose Config: failed");

	BuildNum = le16dec(&rp[5]);

	if (verbose > 0) {
		printf("Read Verbose Config:\n");
		printf("  ChipID 0x%02x\n", rp[1]);
		printf("  TargetID 0x%02x\n", rp[2]);
		printf("  BuildBase 0x%04x\n", le16dec(&rp[3]));
		printf("  BuildNum 0x%04x\n", BuildNum);
		printf("\n");
	}
}

static void
bcm_load_firmware(void)
{
	char *line;
	size_t size;
	ssize_t len;
	FILE *i;
	unsigned int vid, pid;
	int n;

	i = fopen("index.txt", "r");
	if (i == NULL)
		return;

	line = NULL;
	size = 0;

	while ((len = getline(&line, &size, i)) != EOF) {
		if (sscanf(line, "%x:%x\t%n%*s\n", &vid, &pid, &n) != 2
		    || vid != VendorID || pid != ProductID)
			continue;

		line[len - 1] = '\0';
		Firmware = read_ihex(&line[n]);
		break;
	}

	free(line);
	fclose(i);
}

static void
bcm_update_device(void)
{
	struct ihex *ihex;
	uint8_t cp[4];	/* [0]	u32	addr		*/
	uint8_t rp[1];	/* [0]	u8	status		*/

	req = (struct bt_devreq) {
		.opcode = BCM_CMD_DOWNLOAD_MINIDRIVER,
		.rparam = &rp,
		.rlen = sizeof(rp)
	};

	if (bt_devreq(hci, &req, REQ_TIMEOUT) == -1)
		err(EXIT_FAILURE, "Download Minidriver");

	if (req.rlen != sizeof(rp) || rp[0] > 0)
		errx(EXIT_FAILURE, "Download Minidriver: failed");

	usleep(100);

	for (ihex = Firmware; ihex != NULL; ihex = ihex->next) {
		req = (struct bt_devreq) {
			.opcode = BCM_CMD_WRITE_RAM,
			.cparam = ihex->data,
			.clen = ihex->count,
			.rparam = &rp,
			.rlen = sizeof(rp)
		};

		if (bt_devreq(hci, &req, REQ_TIMEOUT) == -1)
			err(EXIT_FAILURE, "Write RAM");

		if (req.rlen != sizeof(rp) || rp[0] > 0)
			errx(EXIT_FAILURE, "Write RAM: failed");
	}

	le32enc(&cp, 0xffffffff);
	req = (struct bt_devreq) {
		.opcode = BCM_CMD_LAUNCH_RAM,
		.cparam = &cp,
		.clen = sizeof(cp),
		.rparam = &rp,
		.rlen = sizeof(rp)
	};

	if (bt_devreq(hci, &req, REQ_TIMEOUT) == -1)
		err(EXIT_FAILURE, "Launch RAM");

	if (req.rlen != sizeof(rp) || rp[0] > 0)
		errx(EXIT_FAILURE, "Launch RAM: failed");

	usleep(250);
}

static bool
get_btdev(unsigned long cmd)
{
	struct sockaddr_bt sa;

	if (ioctl(hci, cmd, &btr) == -1)
		return false;

	Enabled = (btr.btr_flags & BTF_UP) ? true : false;
	if (!Enabled) {
		btr.btr_flags |= BTF_UP;
		if (ioctl(hci, SIOCSBTFLAGS, &btr) == -1)
			err(EXIT_FAILURE, "cannot enable device");

		if (ioctl(hci, SIOCGBTINFO, &btr) == -1)
			err(EXIT_FAILURE, "cannot read device info");
	}

	sa.bt_len = sizeof(sa);
	sa.bt_family = AF_BLUETOOTH;
	bdaddr_copy(&sa.bt_bdaddr, &btr.btr_bdaddr);

	if (bind(hci, (struct sockaddr *)&sa, sizeof(sa)) == -1)
		err(EXIT_FAILURE, "bind");

	if (connect(hci, (struct sockaddr *)&sa, sizeof(sa)) == -1)
		err(EXIT_FAILURE, "connect");

	return true;
}

static void
put_btdev(void)
{

	if (!Enabled) {
		btr.btr_flags &= ~BTF_UP;

		if (ioctl(hci, SIOCSBTFLAGS, &btr) == -1)
			warn("failed to disable device");
	}
}

static void
update_btdev(void)
{

	hci_read_local_version();
	if (Manufacturer != BLUETOOTH_MANUFACTURER_BROADCOM) {
		if (verbose > 0)
			printf("%s: Manufacturer is not Broadcom\n", btr.btr_name);

		return;
	}

	switch(Revision & 0xf000) {
	case 0x1000:
	case 0x2000:
		bcm_read_usb_product();
		if (VendorID != USB_VENDOR_BROADCOM) {
			if (verbose > 0)
				printf("%s: VendorID is not Broadcom\n", btr.btr_name);

			return;
		}
		break;

	default:
	case 0x0000:
	case 0x3000:
		/*
		 * According to the Linux driver, the Broadcom Vendor commands
		 * may not work for these devices.
		 *
		 * (I have a BCM2035 which shows
		 *	HCI version 0x02 rev 0x0000
		 * and it returns a command complete with single data byte 0x11)
		 */
		if (verbose > 0)
			printf("%s: Firmware updating not available\n", btr.btr_name);

		return;
	}

	bcm_read_verbose_config();
	if (BuildNum > 0) {
		if (verbose > 0)
			printf("%s: Firmware update is not required\n", btr.btr_name);

		return;
	}

	bcm_load_firmware();
	if (Firmware == NULL) {
		if (verbose > 0)
			printf("%s: Firmware not found\n", btr.btr_name);

		return;
	}

	if (Enabled) {
		if (verbose > 0)
			printf("%s: Not updating (previously enabled)\n", btr.btr_name);

		return;
	}

	if (verbose > 0) {
		printf("Updating ...");
		fflush(stdout);
	}

	bcm_update_device();

	if (verbose > 0) {
		printf(" done\n");
		printf("\n");
	}
}

void
check_btdev(const char *dev)
{

	hci = socket(PF_BLUETOOTH, SOCK_RAW, BTPROTO_HCI);
	if (hci == -1)
		err(EXIT_FAILURE, "socket");

	memset(btr.btr_name, 0, HCI_DEVNAME_SIZE);
	if (dev) {
		snprintf(btr.btr_name, HCI_DEVNAME_SIZE, "%s", dev);
		if (!get_btdev(SIOCGBTINFO))
			err(EXIT_FAILURE, "%s get info failed", dev);

		update_btdev();
		put_btdev();
	} else {
		while (get_btdev(SIOCNBTINFO)) {
			update_btdev();
			put_btdev();
		}
	}

	close(hci);
}
