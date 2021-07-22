/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/*
 * Copyright (C) 2021 Intel Corporation
 * Author: Johannes Berg <johannes@sipsolutions.net>
 */
#ifndef _UAPI_LINUX_VIRTIO_PCIDEV_H
#define _UAPI_LINUX_VIRTIO_PCIDEV_H
#include <linux/types.h>

/**
 * enum virtio_pcidev_ops - virtual PCI device operations
 * @VIRTIO_PCIDEV_OP_CFG_READ: read config space, size is 1, 2, 4 or 8;
 *	the @data field should be filled in by the device (in little endian).
 * @VIRTIO_PCIDEV_OP_CFG_WRITE: write config space, size is 1, 2, 4 or 8;
 *	the @data field contains the data to write (in little endian).
 * @VIRTIO_PCIDEV_OP_BAR_READ: read BAR mem/pio, size can be variable;
 *	the @data field should be filled in by the device (in little endian).
 * @VIRTIO_PCIDEV_OP_BAR_WRITE: write BAR mem/pio, size can be variable;
 *	the @data field contains the data to write (in little endian).
 * @VIRTIO_PCIDEV_OP_MMIO_MEMSET: memset MMIO, size is variable but
 *	the @data field only has one byte (unlike @VIRTIO_PCIDEV_OP_MMIO_WRITE)
 * @VIRTIO_PCIDEV_OP_INT: legacy INTx# pin interrupt, the addr field is 1-4 for
 *	the number
 * @VIRTIO_PCIDEV_OP_MSI: MSI(-X) interrupt, this message basically transports
 *	the 16- or 32-bit write that would otherwise be done into memory,
 *	analogous to the write messages (@VIRTIO_PCIDEV_OP_MMIO_WRITE) above
 * @VIRTIO_PCIDEV_OP_PME: Dummy message whose content is ignored (and should be
 *	all zeroes) to signal the PME# pin.
 */
enum virtio_pcidev_ops {
	VIRTIO_PCIDEV_OP_RESERVED = 0,
	VIRTIO_PCIDEV_OP_CFG_READ,
	VIRTIO_PCIDEV_OP_CFG_WRITE,
	VIRTIO_PCIDEV_OP_MMIO_READ,
	VIRTIO_PCIDEV_OP_MMIO_WRITE,
	VIRTIO_PCIDEV_OP_MMIO_MEMSET,
	VIRTIO_PCIDEV_OP_INT,
	VIRTIO_PCIDEV_OP_MSI,
	VIRTIO_PCIDEV_OP_PME,
};

/**
 * struct virtio_pcidev_msg - virtio PCI device operation
 * @op: the operation to do
 * @bar: the bar (only with BAR read/write messages)
 * @reserved: reserved
 * @size: the size of the read/write (in bytes)
 * @addr: the address to read/write
 * @data: the data, normally @size long, but just one byte for
 *	%VIRTIO_PCIDEV_OP_MMIO_MEMSET
 *
 * Note: the fields are all in native (CPU) endian, however, the
 * @data values will often be in little endian (see the ops above.)
 */
struct virtio_pcidev_msg {
	__u8 op;
	__u8 bar;
	__u16 reserved;
	__u32 size;
	__u64 addr;
	__u8 data[];
};

#endif /* _UAPI_LINUX_VIRTIO_PCIDEV_H */
