/*
 * Copyright (c) 2014, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

#ifndef __MAILBOX_ROCKCHIP_H__
#define __MAILBOX_ROCKCHIP_H__

struct rockchip_mbox_msg {
	u32 cmd;
	int tx_size;
	void *tx_buf;
	int rx_size;
	void *rx_buf;
	void *cl_data;
};

#endif /* __MAILBOX_ROCKCHIP_H__ */
