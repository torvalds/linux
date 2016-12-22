/*
 * Copyright (C) 2016, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef _RK_KEYS_H
#define _RK_KEYS_H

#ifdef CONFIG_KEYBOARD_ROCKCHIP
void rk_send_power_key(int state);
void rk_send_wakeup_key(void);
#else
static inline void rk_send_power_key(int state) { }
static inline void rk_send_wakeup_key(void) { }
#endif

#endif
