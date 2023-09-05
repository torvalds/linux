// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2011 matt mooney <mfm@muteddisk.com>
 *               2005-2007 Takahiro Hirofuchi
 */

#include "vhci_driver.h"
#include "usbip_common.h"

static int list_imported_devices(void)
{
	int i;
	struct usbip_imported_device *idev;
	int ret;

	if (usbip_names_init(USBIDS_FILE))
		err("failed to open %s", USBIDS_FILE);

	ret = usbip_vhci_driver_open();
	if (ret < 0) {
		err("open vhci_driver (is vhci_hcd loaded?)");
		goto err_names_free;
	}

	printf("Imported USB devices\n");
	printf("====================\n");

	for (i = 0; i < vhci_driver->nports; i++) {
		idev = &vhci_driver->idev[i];

		if (usbip_vhci_imported_device_dump(idev) < 0)
			goto err_driver_close;
	}

	usbip_vhci_driver_close();
	usbip_names_free();

	return ret;

err_driver_close:
	usbip_vhci_driver_close();
err_names_free:
	usbip_names_free();
	return -1;
}

int usbip_port_show(__attribute__((unused)) int argc,
		    __attribute__((unused)) char *argv[])
{
	int ret;

	ret = list_imported_devices();
	if (ret < 0)
		err("list imported devices");

	return ret;
}
