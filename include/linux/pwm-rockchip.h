/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Copyright (c) 2023 Rockchip Electronics Co., Ltd.
 */

#ifndef _PWM_ROCKCHIP_H_
#define _PWM_ROCKCHIP_H_

#include <linux/pwm.h>

/**
 * enum rockchip_pwm_global_ctrl_cmd - commands for pwm global ctrl
 * @PWM_GLOBAL_CTRL_JOIN: join the global control group
 * @PWM_GLOBAL_CTRL_EXIT: exit the global control group
 * @PWM_GLOBAL_CTRL_GRANT: obtian the permission of global control
 * @PWM_GLOBAL_CTRL_RECLAIM: reclaim the permission of global control
 * @PWM_GLOBAL_CTRL_UPDATE: update the configs for all channels in group
 * @PWM_GLOBAL_CTRL_ENABLE: enable all channels in group
 * @PWM_GLOBAL_CTRL_DISABLE: disable all channels in group
 */
enum rockchip_pwm_global_ctrl_cmd {
	PWM_GLOBAL_CTRL_JOIN,
	PWM_GLOBAL_CTRL_EXIT,
	PWM_GLOBAL_CTRL_GRANT,
	PWM_GLOBAL_CTRL_RECLAIM,
	PWM_GLOBAL_CTRL_UPDATE,
	PWM_GLOBAL_CTRL_ENABLE,
	PWM_GLOBAL_CTRL_DISABLE,
};

/**
 * struct rockchip_pwm_wave_table - wave table config object
 * @offset: the offset of wave table to set
 * @len: the length of wave table to set
 * @table: the values of wave table to set
 * @
 */
struct rockchip_pwm_wave_table {
	u16 offset;
	u16 len;
	u64 *table;
};

/**
 * enum rockchip_pwm_wave_table_width_mode - element width of pwm wave table
 * @PWM_WAVE_TABLE_8BITS_WIDTH: each element in table is 8bits
 * @PWM_WAVE_TABLE_16BITS_WIDTH: each element in table is 16bits
 */
enum rockchip_pwm_wave_table_width_mode {
	PWM_WAVE_TABLE_8BITS_WIDTH,
	PWM_WAVE_TABLE_16BITS_WIDTH,
};

/**
 * enum rockchip_pwm_wave_update_mode - update mode of wave generator
 * @PWM_WAVE_INCREASING:
 *     The wave table address will wrap back to minimum address when increase to
 *     maximum and then increase again.
 * @PWM_WAVE_INCREASING_THEN_DECREASING:
 *     The wave table address will change to decreasing when increasing to the maximum
 *     address. it will return to increasing when decrease to the minimum value.
 */
enum rockchip_pwm_wave_update_mode {
	PWM_WAVE_INCREASING,
	PWM_WAVE_INCREASING_THEN_DECREASING,
};

/**
 * struct rockchip_pwm_wave_config - wave generator config object
 * @duty_table: the wave table config of duty
 * @period_table: the wave table config of period
 * @enable: enable or disable wave generator
 * @duty_en: to update duty by duty table or not
 * @period_en: to update period by period table or not
 * @width_mode: the width mode of wave table
 * @update_mode: the update mode of wave generator
 * @duty_max: the maximum address of duty table
 * @duty_min: the minimum address of duty table
 * @period_max: the maximum address of period table
 * @period_min: the minimum address of period table
 * @offset: the initial offset address of duty and period
 * @middle: the middle address of duty and period
 * @max_hold: the time to stop at maximum address
 * @min_hold: the time to stop at minimum address
 * @middle_hold: the time to stop at middle address
 */
struct rockchip_pwm_wave_config {
	struct rockchip_pwm_wave_table *duty_table;
	struct rockchip_pwm_wave_table *period_table;
	bool enable;
	bool duty_en;
	bool period_en;
	u16 rpt;
	u32 width_mode;
	u32 update_mode;
	u32 duty_max;
	u32 duty_min;
	u32 period_max;
	u32 period_min;
	u32 offset;
	u32 middle;
	u32 max_hold;
	u32 min_hold;
	u32 middle_hold;
};

#if IS_REACHABLE(CONFIG_PWM_ROCKCHIP)
/**
 * rockchip_pwm_set_counter() - setup pwm counter mode
 * @pwm: PWM device
 * @enable: enable/disable counter mode
 *
 * Returns: 0 on success or a negative error code on failure.
 */
int rockchip_pwm_set_counter(struct pwm_device *pwm, bool enable);

/**
 * rockchip_pwm_get_counter_result() - get counter result
 * @pwm: PWM device
 * @counter_res: number of input waveforms
 * @is_clear: clear counter result or not
 *
 * Returns: 0 on success or a negative error code on failure.
 */
int rockchip_pwm_get_counter_result(struct pwm_device *pwm,
				    unsigned long *counter_res, bool is_clear);

/**
 * rockchip_pwm_set_freq_meter() - setup pwm frequency meter mode
 * @pwm: PWM device
 * @delay_ms: time to wait, in milliseconds, before getting frequency meter result
 * @freq_hz: parameter in Hz to fill with frequency meter result
 *
 * Returns: 0 on success or a negative error code on failure.
 */
int rockchip_pwm_set_freq_meter(struct pwm_device *pwm, unsigned long delay_ms,
				unsigned long *freq_hz);

/**
 * rockchip_pwm_global_ctrl() - execute global control commands
 * @pwm: PWM device
 * @cmd: command type to execute
 *
 * Returns: 0 on success or a negative error code on failure.
 */
int rockchip_pwm_global_ctrl(struct pwm_device *pwm, enum rockchip_pwm_global_ctrl_cmd cmd);

/**
 * rockchip_pwm_set_wave() - setup pwm wave generator mode
 * @pwm: PWM device
 * @config: configs of wave generator mode
 *
 * Returns: 0 on success or a negative error code on failure.
 */
int rockchip_pwm_set_wave(struct pwm_device *pwm, struct rockchip_pwm_wave_config *config);
#else
static inline int rockchip_pwm_set_counter(struct pwm_device *pwm, bool enable)
{
	return 0;
}

static inline int rockchip_pwm_get_counter_result(struct pwm_device *pwm,
						  unsigned long *counter_res, bool is_clear)
{
	return 0;
}

static inline int rockchip_pwm_set_freq_meter(struct pwm_device *pwm, unsigned long delay_ms,
					      unsigned long *freq_hz)
{
	return 0;
}

static inline  int rockchip_pwm_global_ctrl(struct pwm_device *pwm,
					    enum rockchip_pwm_global_ctrl_cmd cmd)
{
	return 0;
}

static inline int rockchip_pwm_set_wave(struct pwm_device *pwm,
					struct rockchip_pwm_wave_config *config)
{
	return 0;
}
#endif

#endif /* _PWM_ROCKCHIP_H_ */
