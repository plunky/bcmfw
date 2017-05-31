BCMFW
=====

NetBSD firmware loader for Broadcom chip based Bluetooth adaptors

Modern Broadcom chips find their initial firmware instructions from an
internal PROM after power up.  This initial firmware is usually enough
for proper functionality but, once running, **bcmfw** can upgrade it over the
HCI interface with "Patch RAM" data files provided by Broadcom.  **bcmfw**
probes all adaptors connected to the Bluetooth protocol stack unless any
are specified on the commandline, in which case only those are probed.
The probe first checks the Manufacturer code with the standard "Read
Local Version" command, and only when it is 15 (Broadcom) does it attempt
to check the device itself, with vendor-specific commands.

Because the device will need to be reset afterwards, and to prevent
errors in a running system, no update will be attempted unless **bcmfw**
found the device in a non-enabled state.

Older BCM2033 based devices do not have firmware and will not initially
be configured as Bluetooth adaptors, so will attach as ugen(4).  If a
ugen device name is passed on the commandline, **bcmfw** checks the USB
Vendor & Product ID's directly before proceeding, which should be 0x0a5c
and 0x2033 respectively.

The Patch RAM files are not available directly from Broadcom but are
provided via Windows Bluetooth drivers, often for specific devices, and
the **bcmfw-install** program can be used to install them to the NetBSD
filesystem.  For example, the support page for the ASUS X99 laptop at

   https://www.asus.com/

currently lists the

   Bluetooth_V1201650_WHQL_Win10.zip

archive which corresponds to the Broadcom Bluetooth driver version
12.0.1.650.  Download and extract this archive, then find a directory
with the ".hex" files and an ".inf" file (Bluetooth/BCM_DriverOnly/32),
and execute **bcmfw-install** from there to install firmware files and an
index to /usr/pkg/libdata/bcmfw/libdata/bcmfw.

After a successful update, the HCI revision of the device will change.
