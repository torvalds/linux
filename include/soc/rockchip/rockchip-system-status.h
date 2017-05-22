/*
 * Copyright (c) 2016, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */
#ifndef __SOC_ROCKCHIP_SYSTEM_STATUS_H
#define __SOC_ROCKCHIP_SYSTEM_STATUS_H

int rockchip_register_system_status_notifier(struct notifier_block *nb);
int rockchip_unregister_system_status_notifier(struct notifier_block *nb);
void rockchip_set_system_status(unsigned long status);
void rockchip_clear_system_status(unsigned long status);
unsigned long rockchip_get_system_status(void);

#endif
