/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2018 Rockchip Electronics Co., Ltd.
 */

enum transfer_type {
	PCIE_DMA_DATA_SND,
	PCIE_DMA_DATA_RCV_ACK,
	PCIE_DMA_DATA_FREE_ACK,
};

union pcie_dma_ioctl_param {
	struct {
		u32	idx;
		u32	l_widx;
		u32	r_widx;
		u32	size;
		u32	type;
	} in;
	struct {
		u32	lwa;
		u32	rwa;
	} out;
	u32		lra;
	u32		count;
};

#define PCIE_BASE	'P'
#define PCIE_DMA_START					\
	_IOW(PCIE_BASE, 0, union pcie_dma_ioctl_param)
#define PCIE_DMA_GET_LOCAL_READ_BUFFER_INDEX		\
	_IOR(PCIE_BASE, 1, union pcie_dma_ioctl_param)
#define PCIE_DMA_GET_LOCAL_REMOTE_WRITE_BUFFER_INDEX	\
	_IOR(PCIE_BASE, 2, union pcie_dma_ioctl_param)
#define PCIE_DMA_SET_LOCAL_READ_BUFFER_INDEX		\
	_IOW(PCIE_BASE, 3, union pcie_dma_ioctl_param)
#define PCIE_DMA_SYNC_BUFFER_FOR_CPU			\
	_IOW(PCIE_BASE, 4, union pcie_dma_ioctl_param)
#define PCIE_DMA_SYNC_BUFFER_TO_DEVICE			\
	_IOW(PCIE_BASE, 5, union pcie_dma_ioctl_param)
#define PCIE_DMA_WAIT_TRANSFER_COMPLETE			\
	_IO(PCIE_BASE, 6)
#define PCIE_DMA_SET_LOOP_COUNT				\
	_IOW(PCIE_BASE, 7, union pcie_dma_ioctl_param)

