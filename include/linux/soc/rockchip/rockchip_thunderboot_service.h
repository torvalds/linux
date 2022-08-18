/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2022 Rockchip Electronics Co., Ltd */

#ifndef _ROCKCHIP_THUNDERBOOT_SERVICE_H
#define _ROCKCHIP_THUNDERBOOT_SERVICE_H

struct rk_tb_client {
	struct list_head node;
	void *data;
	void (*cb)(void *data);
};

#ifdef CONFIG_ROCKCHIP_THUNDER_BOOT_SERVICE
bool rk_tb_mcu_is_done(void);
int rk_tb_client_register_cb(struct rk_tb_client *client);
#else
static inline bool rk_tb_mcu_is_done(void)
{
	return true;
}
static inline int rk_tb_client_register_cb(struct rk_tb_client *client)
{
	if (client && client->cb)
		client->cb(client->data);

	return 0;
}
#endif
#endif
