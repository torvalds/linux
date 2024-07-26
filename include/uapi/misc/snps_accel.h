/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * Copyright (C) 2023 Synopsys, Inc. (www.synopsys.com)
 */

#ifndef __SNPS_ACCEL_H__
#define __SNPS_ACCEL_H__

#include "linux/types.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define SNPS_ACCEL_MAGIC		'N'

#define SNPS_ACCEL_INFO_SHMEM		0x01
#define SNPS_ACCEL_INFO_NOTIFY		0x02
#define SNPS_ACCEL_WAIT_IRQ		0x03
#define SNPS_ACCEL_DMABUF_ALLOC		0x04
#define SNPS_ACCEL_DMABUF_INFO		0x05
#define SNPS_ACCEL_DMABUF_IMPORT	0x06
#define SNPS_ACCEL_DMABUF_DETACH	0x07

#define SNPS_ACCEL_IOCTL_INFO_SHMEM	\
	_IOR(SNPS_ACCEL_MAGIC, SNPS_ACCEL_INFO_SHMEM, struct snps_accel_shmem)
#define SNPS_ACCEL_IOCTL_INFO_NOTIFY	\
	_IOR(SNPS_ACCEL_MAGIC, SNPS_ACCEL_INFO_NOTIFY, struct snps_accel_notify)
#define SNPS_ACCEL_IOCTL_WAIT_IRQ	\
	_IOWR(SNPS_ACCEL_MAGIC, SNPS_ACCEL_WAIT_IRQ, struct snps_accel_wait_irq)
#define SNPS_ACCEL_IOCTL_DMABUF_ALLOC	\
	_IOWR(SNPS_ACCEL_MAGIC, SNPS_ACCEL_DMABUF_ALLOC, struct snps_accel_dmabuf_alloc)
#define SNPS_ACCEL_IOCTL_DMABUF_INFO	\
	_IOWR(SNPS_ACCEL_MAGIC, SNPS_ACCEL_DMABUF_INFO, struct snps_accel_dmabuf_info)
#define SNPS_ACCEL_IOCTL_DMABUF_IMPORT	\
	_IOW(SNPS_ACCEL_MAGIC, SNPS_ACCEL_DMABUF_IMPORT, struct snps_accel_dmabuf_import)
#define SNPS_ACCEL_IOCTL_DMABUF_DETACH	\
	_IOW(SNPS_ACCEL_MAGIC, SNPS_ACCEL_DMABUF_DETACH, struct snps_accel_dmabuf_detach)

struct snps_accel_shmem {
	/* Shared memory intermediate offset for use in mmap */
	__u64 offset;

	/* Size of mapped region */
	__u64 size;
};

struct snps_accel_notify {
	/* Shared memory intermediate offset for use in mmap */
	__u64 offset;

	/* Size of mapped region */
	__u64 size;
};

struct snps_accel_wait_irq {
	/* Timeout in milliseconds for blocking wait operation */
	__u32 timeout;
	/* Total interrupt count returned by the driver */
	__u32 count;
};

enum {
	SNPS_ACCEL_IO_R = 0x1,
	SNPS_ACCEL_IO_W = 0x2
};

struct snps_accel_dmabuf_alloc {
	/* dma-buf file descriptor */
	__s32 fd;

	/* Flags to allpy for dma buffer device mappings */
	__u32 flags;

	/* Size of dma buffer to allocate */
	__u64 size;
};

struct snps_accel_dmabuf_info {
	/* dma-buf file descriptor */
	__s32 fd;

	/* Address as it seen by DMA of a device */
	__u64 addr;

	/* Size of dma buffer to allocate */
	__u64 size;
};

struct snps_accel_dmabuf_import {
	/* dma-buf file descriptor of extermal buffer */
	__s32 fd;
};

struct snps_accel_dmabuf_detach {
	/* dma-buf file descriptor */
	__s32 fd;
};

#if defined(__cplusplus)
}
#endif

#endif  /* __SNPS_ACCEL_H__ */
