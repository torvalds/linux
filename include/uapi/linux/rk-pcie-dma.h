/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (C) 2018 Rockchip Electronics Co., Ltd.
 */
#ifndef _UAPI__PCIE_DMA_TRX_H__
#define _UAPI__PCIE_DMA_TRX_H__

#include <linux/types.h>

union pcie_dma_ioctl_param {
	struct {
		__u32	idx;
		__u32	l_widx;
		__u32	r_widx;
		__u32	size;
		__u32	chn;
	} in;
	struct {
		__u32	lwa;
		__u32	rwa;
	} out;
	__u32		lra;
	__u32		count;
	__u32		total_buffer_size;
	__u64		local_addr;
	__u32		buffer_size;
};

#define PCIE_BASE	'P'
#define PCIE_DMA_START					\
	_IOW(PCIE_BASE, 0, union pcie_dma_ioctl_param)
#define PCIE_DMA_GET_LOCAL_READ_BUFFER_INDEX		\
	_IOR(PCIE_BASE, 1, union pcie_dma_ioctl_param)
#define PCIE_DMA_GET_LOCAL_REMOTE_WRITE_BUFFER_INDEX	\
	_IOR(PCIE_BASE, 2, union pcie_dma_ioctl_param)
#define PCIE_DMA_FREE_LOCAL_READ_BUFFER_INDEX		\
	_IOW(PCIE_BASE, 3, union pcie_dma_ioctl_param)
#define PCIE_DMA_SYNC_BUFFER_FOR_CPU			\
	_IOW(PCIE_BASE, 4, union pcie_dma_ioctl_param)
#define PCIE_DMA_SYNC_BUFFER_TO_DEVICE			\
	_IOW(PCIE_BASE, 5, union pcie_dma_ioctl_param)
#define PCIE_DMA_WAIT_TRANSFER_COMPLETE			\
	_IO(PCIE_BASE, 6)
#define PCIE_DMA_SET_LOOP_COUNT				\
	_IOW(PCIE_BASE, 7, union pcie_dma_ioctl_param)
#define PCIE_DMA_GET_TOTAL_BUFFER_SIZE			\
	_IOR(PCIE_BASE, 8, union pcie_dma_ioctl_param)
#define PCIE_DMA_SET_BUFFER_SIZE			\
	_IOW(PCIE_BASE, 9, union pcie_dma_ioctl_param)
#define PCIE_DMA_READ_FROM_REMOTE			\
	_IOW(PCIE_BASE, 0xa, union pcie_dma_ioctl_param)
#define PCIE_DMA_USER_SET_BUF_ADDR			\
	_IOW(PCIE_BASE, 0xb, union pcie_dma_ioctl_param)
#define PCIE_DMA_GET_BUFFER_SIZE			\
	_IOR(PCIE_BASE, 0xc, union pcie_dma_ioctl_param)

#endif
