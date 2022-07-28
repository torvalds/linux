/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022 Rockchip Electronics Co., Ltd
 */

#ifndef __SOC_ROCKCHIP_MAILBOX_H__
#define __SOC_ROCKCHIP_MAILBOX_H__

#include <linux/errno.h>
#include <linux/types.h>

struct mbox_chan;

struct rockchip_mbox_msg {
	u32 cmd;
	u32 data;
};

#if IS_REACHABLE(CONFIG_ROCKCHIP_MBOX)
int rockchip_mbox_read_msg(struct mbox_chan *chan,
			   struct rockchip_mbox_msg *msg);
#else
static inline int rockchip_mbox_read_msg(struct mbox_chan *chan,
					 struct rockchip_mbox_msg *msg)
{
	return -EOPNOTSUPP;
}
#endif

#endif /* __SOC_ROCKCHIP_MAILBOX_H__ */
