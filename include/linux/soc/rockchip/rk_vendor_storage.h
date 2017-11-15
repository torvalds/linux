/*
 * Copyright (c) 2016, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#ifndef __PLAT_RK_VENDOR_STORAGE_H
#define __PLAT_RK_VENDOR_STORAGE_H

#define RSV_ID		0
#define SN_ID		1
#define WIFI_MAC_ID	2
#define LAN_MAC_ID	3
#define BT_MAC_ID	4
#define SENSOR_CALIBRATION_ID 7

int rk_vendor_read(u32 id, void *pbuf, u32 size);
int rk_vendor_write(u32 id, void *pbuf, u32 size);
int rk_vendor_register(void *read, void *write);
bool is_rk_vendor_ready(void);

#endif
