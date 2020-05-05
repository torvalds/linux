/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/* Copyright IBM Corp 2019 */

#ifndef _UAPI_LINUX_ASPEED_XDMA_H_
#define _UAPI_LINUX_ASPEED_XDMA_H_

#include <linux/ioctl.h>
#include <linux/types.h>

#define __ASPEED_XDMA_IOCTL_MAGIC	0xb7
#define ASPEED_XDMA_IOCTL_RESET		_IO(__ASPEED_XDMA_IOCTL_MAGIC, 0)

/*
 * aspeed_xdma_direction
 *
 * ASPEED_XDMA_DIRECTION_DOWNSTREAM: transfers data from the host to the BMC
 *
 * ASPEED_XDMA_DIRECTION_UPSTREAM: transfers data from the BMC to the host
 */
enum aspeed_xdma_direction {
	ASPEED_XDMA_DIRECTION_DOWNSTREAM = 0,
	ASPEED_XDMA_DIRECTION_UPSTREAM,
};

/*
 * aspeed_xdma_op
 *
 * host_addr: the DMA address on the host side, typically configured by PCI
 *            subsystem
 *
 * len: the size of the transfer in bytes
 *
 * direction: an enumerator indicating the direction of the DMA operation; see
 *            enum aspeed_xdma_direction
 */
struct aspeed_xdma_op {
	__u64 host_addr;
	__u32 len;
	__u32 direction;
};

#endif /* _UAPI_LINUX_ASPEED_XDMA_H_ */
