/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2005 Silicon Graphics, Inc.  All Rights Reserved.
 */

#ifndef _LINUX_IOC4_COMMON_H
#define _LINUX_IOC4_COMMON_H

/* prototypes */

int ioc4_serial_init(void);

int ioc4_serial_attach_one(struct pci_dev *pdev, const struct
				pci_device_id *pci_id);
int ioc4_ide_attach_one(struct pci_dev *pdev, const struct
				pci_device_id *pci_id);

#endif	/* _LINUX_IOC4_COMMON_H */
