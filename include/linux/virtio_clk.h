/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _LINUX_VIRTIO_CLK_H
#define _LINUX_VIRTIO_CLK_H

#include <linux/types.h>
#include <linux/virtio_ids.h>
#include <linux/virtio_config.h>
#include <linux/virtio_types.h>

/* Virtio ID of clock */
#ifdef VIRTIO_ID_CLOCK
#undef VIRTIO_ID_CLOCK
#endif

/* Virtio ID of clock : 0xC000 */
#define VIRTIO_ID_CLOCK		49152

/* Feature bits */
#define VIRTIO_CLK_F_RESET	1	/* Support reset */
#define VIRTIO_CLK_F_NAME	2	/* Support clock name */

/* Configuration layout */
struct virtio_clk_config {
	__u32 num_clks;
	__u32 num_resets;
	__u8 name[20];
} __packed;

/* Request/response message format */
struct virtio_clk_msg {
	u8 name[40];
	__virtio32 id;
	__virtio32 type;
	__virtio32 result;
	__virtio32 data[4];
};

/* Request type */
#define VIRTIO_CLK_T_ENABLE	0
#define VIRTIO_CLK_T_DISABLE	1
#define VIRTIO_CLK_T_SET_RATE	2
#define VIRTIO_CLK_T_GET_RATE	3
#define VIRTIO_CLK_T_ROUND_RATE	4
#define VIRTIO_CLK_T_RESET	5
#define VIRTIO_CLK_T_SET_FLAGS	6
#define VIRTIO_CLK_T_SET_PARENT	8

#endif /* _LINUX_VIRTIO_CLK_H */
