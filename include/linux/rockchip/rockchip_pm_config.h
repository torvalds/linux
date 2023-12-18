/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Copyright (c) 2023 Rockchip Electronics Co., Ltd.
 */

#ifndef __ROCKCHIP_PM_CONFIG_H
#define __ROCKCHIP_PM_CONFIG_H

struct rk_sleep_config {
	u32 mode_config;
	u32 wakeup_config;
	u32 sleep_debug_en;
	u32 pwm_regulator_config;
	u32 *power_ctrl_config;
	u32 power_ctrl_config_cnt;
	u32 *sleep_io_config;
	u32 sleep_io_config_cnt;
	u32 apios_suspend;
	u32 io_ret_config;
	u32 sleep_pin_config[2];
};

#if IS_REACHABLE(CONFIG_ROCKCHIP_SUSPEND_MODE)
const struct rk_sleep_config *rockchip_get_cur_sleep_config(void);
#else
static inline const struct rk_sleep_config *rockchip_get_cur_sleep_config(void)
{
	return NULL;
}
#endif

#endif /* __ROCKCHIP_PM_CONFIG_H */
