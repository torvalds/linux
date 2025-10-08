/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * pcitest.h - PCI test uapi defines
 *
 * Copyright (C) 2017 Texas Instruments
 * Author: Kishon Vijay Abraham I <kishon@ti.com>
 *
 */

#ifndef __UAPI_LINUX_PCITEST_H
#define __UAPI_LINUX_PCITEST_H

#define PCITEST_BAR		_IO('P', 0x1)
#define PCITEST_INTX_IRQ	_IO('P', 0x2)
#define PCITEST_LEGACY_IRQ	PCITEST_INTX_IRQ
#define PCITEST_MSI		_IOW('P', 0x3, int)
#define PCITEST_WRITE		_IOW('P', 0x4, unsigned long)
#define PCITEST_READ		_IOW('P', 0x5, unsigned long)
#define PCITEST_COPY		_IOW('P', 0x6, unsigned long)
#define PCITEST_MSIX		_IOW('P', 0x7, int)
#define PCITEST_SET_IRQTYPE	_IOW('P', 0x8, int)
#define PCITEST_GET_IRQTYPE	_IO('P', 0x9)
#define PCITEST_BARS		_IO('P', 0xa)
#define PCITEST_DOORBELL	_IO('P', 0xb)
#define PCITEST_CLEAR_IRQ	_IO('P', 0x10)

#define PCITEST_IRQ_TYPE_UNDEFINED	-1
#define PCITEST_IRQ_TYPE_INTX		0
#define PCITEST_IRQ_TYPE_MSI		1
#define PCITEST_IRQ_TYPE_MSIX		2
#define PCITEST_IRQ_TYPE_AUTO		3

#define PCITEST_FLAGS_USE_DMA	0x00000001

struct pci_endpoint_test_xfer_param {
	unsigned long size;
	unsigned char flags;
};

#endif /* __UAPI_LINUX_PCITEST_H */
