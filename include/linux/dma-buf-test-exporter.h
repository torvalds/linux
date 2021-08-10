/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2012-2013, 2017, 2020-2021 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

#ifndef _LINUX_DMA_BUF_TEST_EXPORTER_H_
#define _LINUX_DMA_BUF_TEST_EXPORTER_H_

#include <linux/types.h>
#include <linux/ioctl.h>

#define DMA_BUF_TE_VER_MAJOR 1
#define DMA_BUF_TE_VER_MINOR 0
#define DMA_BUF_TE_ENQ 0x642d7465
#define DMA_BUF_TE_ACK 0x68692100

struct dma_buf_te_ioctl_version
{
	int op;    /**< Must be set to DMA_BUF_TE_ENQ by client, driver will set it to DMA_BUF_TE_ACK */
	int major; /**< Major version */
	int minor; /**< Minor version */
};

struct dma_buf_te_ioctl_alloc
{
	__u64 size; /* size of buffer to allocate, in pages */
};

struct dma_buf_te_ioctl_status
{
	/* in */
	int fd; /* the dma_buf to query, only dma_buf objects exported by this driver is supported */
	/* out */
	int attached_devices; /* number of devices attached (active 'dma_buf_attach's) */
	int device_mappings; /* number of device mappings (active 'dma_buf_map_attachment's) */
	int cpu_mappings;    /* number of cpu mappings (active 'mmap's) */
};

struct dma_buf_te_ioctl_set_failing
{
	/* in */
	int fd; /* the dma_buf to set failure mode for, only dma_buf objects exported by this driver is supported */

	/* zero = no fail injection, non-zero = inject failure */
	int fail_attach;
	int fail_map;
	int fail_mmap;
};

struct dma_buf_te_ioctl_fill
{
	int fd;
	unsigned int value;
};

#define DMA_BUF_TE_IOCTL_BASE 'E'
/* Below all returning 0 if successful or -errcode except DMA_BUF_TE_ALLOC which will return fd or -errcode */
#define DMA_BUF_TE_VERSION         _IOR(DMA_BUF_TE_IOCTL_BASE, 0x00, struct dma_buf_te_ioctl_version)
#define DMA_BUF_TE_ALLOC           _IOR(DMA_BUF_TE_IOCTL_BASE, 0x01, struct dma_buf_te_ioctl_alloc)
#define DMA_BUF_TE_QUERY           _IOR(DMA_BUF_TE_IOCTL_BASE, 0x02, struct dma_buf_te_ioctl_status)
#define DMA_BUF_TE_SET_FAILING     _IOW(DMA_BUF_TE_IOCTL_BASE, 0x03, struct dma_buf_te_ioctl_set_failing)
#define DMA_BUF_TE_ALLOC_CONT      _IOR(DMA_BUF_TE_IOCTL_BASE, 0x04, struct dma_buf_te_ioctl_alloc)
#define DMA_BUF_TE_FILL            _IOR(DMA_BUF_TE_IOCTL_BASE, 0x05, struct dma_buf_te_ioctl_fill)

#endif /* _LINUX_DMA_BUF_TEST_EXPORTER_H_ */
