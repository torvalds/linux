/*
 * Copyright (C) 2017, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __RK3368_MAILBOX_H__
#define __RK3368_MAILBOX_H__

struct rk3368_mbox_msg {
	u32 cmd;
	int tx_size;
	void *tx_buf;
	int rx_size;
	void *rx_buf;
	void *cl_data;
};

#endif /* __RK3368_MAILBOX_H__ */
