/*
 * Copyright 2019 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef __SMU_INTERNAL_H__
#define __SMU_INTERNAL_H__

#include "amdgpu_smu.h"

#if defined(SWSMU_CODE_LAYER_L1)

#define smu_ppt_funcs(intf, ret, smu, args...) \
	((smu)->ppt_funcs ? ((smu)->ppt_funcs->intf ? (smu)->ppt_funcs->intf(smu, ##args) : ret) : -EINVAL)

#define smu_init_microcode(smu)						smu_ppt_funcs(init_microcode, 0, smu)
#define smu_fini_microcode(smu)						smu_ppt_funcs(fini_microcode, 0, smu)
#define smu_init_smc_tables(smu)					smu_ppt_funcs(init_smc_tables, 0, smu)
#define smu_fini_smc_tables(smu)					smu_ppt_funcs(fini_smc_tables, 0, smu)
#define smu_init_power(smu)						smu_ppt_funcs(init_power,	0, smu)
#define smu_fini_power(smu)						smu_ppt_funcs(fini_power, 0, smu)
#define smu_setup_pptable(smu)						smu_ppt_funcs(setup_pptable, 0, smu)
#define smu_powergate_sdma(smu, gate)					smu_ppt_funcs(powergate_sdma, 0, smu, gate)
#define smu_get_vbios_bootup_values(smu)				smu_ppt_funcs(get_vbios_bootup_values, 0, smu)
#define smu_check_fw_version(smu)					smu_ppt_funcs(check_fw_version, 0, smu)
#define smu_write_pptable(smu)						smu_ppt_funcs(write_pptable, 0, smu)
#define smu_set_min_dcef_deep_sleep(smu, clk)				smu_ppt_funcs(set_min_dcef_deep_sleep, 0, smu, clk)
#define smu_set_driver_table_location(smu)				smu_ppt_funcs(set_driver_table_location, 0, smu)
#define smu_set_tool_table_location(smu)				smu_ppt_funcs(set_tool_table_location, 0, smu)
#define smu_notify_memory_pool_location(smu)				smu_ppt_funcs(notify_memory_pool_location, 0, smu)
#define smu_gfx_off_control(smu, enable)				smu_ppt_funcs(gfx_off_control, 0, smu, enable)
#define smu_get_gfx_off_status(smu)						smu_ppt_funcs(get_gfx_off_status, 0, smu)
#define smu_get_gfx_off_entrycount(smu, value)						smu_ppt_funcs(get_gfx_off_entrycount, 0, smu, value)
#define smu_get_gfx_off_residency(smu, value)						smu_ppt_funcs(get_gfx_off_residency, 0, smu, value)
#define smu_set_gfx_off_residency(smu, value)						smu_ppt_funcs(set_gfx_off_residency, 0, smu, value)
#define smu_set_last_dcef_min_deep_sleep_clk(smu)			smu_ppt_funcs(set_last_dcef_min_deep_sleep_clk, 0, smu)
#define smu_system_features_control(smu, en)				smu_ppt_funcs(system_features_control, 0, smu, en)
#define smu_init_max_sustainable_clocks(smu)				smu_ppt_funcs(init_max_sustainable_clocks, 0, smu)
#define smu_set_default_od_settings(smu)				smu_ppt_funcs(set_default_od_settings, 0, smu)
#define smu_send_smc_msg_with_param(smu, msg, param, read_arg)		smu_ppt_funcs(send_smc_msg_with_param, 0, smu, msg, param, read_arg)
#define smu_send_smc_msg(smu, msg, read_arg)				smu_ppt_funcs(send_smc_msg, 0, smu, msg, read_arg)
#define smu_init_display_count(smu, count)				smu_ppt_funcs(init_display_count, 0, smu, count)
#define smu_feature_set_allowed_mask(smu)				smu_ppt_funcs(set_allowed_mask, 0, smu)
#define smu_feature_get_enabled_mask(smu, mask)				smu_ppt_funcs(get_enabled_mask, -EOPNOTSUPP, smu, mask)
#define smu_feature_is_enabled(smu, mask)				smu_ppt_funcs(feature_is_enabled, 0, smu, mask)
#define smu_disable_all_features_with_exception(smu, mask)		smu_ppt_funcs(disable_all_features_with_exception, 0, smu, mask)
#define smu_is_dpm_running(smu)						smu_ppt_funcs(is_dpm_running, 0, smu)
#define smu_notify_display_change(smu)					smu_ppt_funcs(notify_display_change, 0, smu)
#define smu_populate_umd_state_clk(smu)					smu_ppt_funcs(populate_umd_state_clk, 0, smu)
#define smu_enable_thermal_alert(smu)					smu_ppt_funcs(enable_thermal_alert, 0, smu)
#define smu_disable_thermal_alert(smu)					smu_ppt_funcs(disable_thermal_alert, 0, smu)
#define smu_smc_read_sensor(smu, sensor, data, size)			smu_ppt_funcs(read_sensor, -EINVAL, smu, sensor, data, size)
#define smu_pre_display_config_changed(smu)				smu_ppt_funcs(pre_display_config_changed, 0, smu)
#define smu_display_config_changed(smu)					smu_ppt_funcs(display_config_changed, 0, smu)
#define smu_apply_clocks_adjust_rules(smu)				smu_ppt_funcs(apply_clocks_adjust_rules, 0, smu)
#define smu_notify_smc_display_config(smu)				smu_ppt_funcs(notify_smc_display_config, 0, smu)
#define smu_run_btc(smu)						smu_ppt_funcs(run_btc, 0, smu)
#define smu_get_allowed_feature_mask(smu, feature_mask, num)		smu_ppt_funcs(get_allowed_feature_mask, 0, smu, feature_mask, num)
#define smu_set_watermarks_table(smu, clock_ranges)			smu_ppt_funcs(set_watermarks_table, 0, smu, clock_ranges)
#define smu_thermal_temperature_range_update(smu, range, rw)		smu_ppt_funcs(thermal_temperature_range_update, 0, smu, range, rw)
#define smu_register_irq_handler(smu)					smu_ppt_funcs(register_irq_handler, 0, smu)
#define smu_get_dpm_ultimate_freq(smu, param, min, max)			smu_ppt_funcs(get_dpm_ultimate_freq, 0, smu, param, min, max)
#define smu_asic_set_performance_level(smu, level)			smu_ppt_funcs(set_performance_level, -EINVAL, smu, level)
#define smu_dump_pptable(smu)						smu_ppt_funcs(dump_pptable, 0, smu)
#define smu_update_pcie_parameters(smu, pcie_gen_cap, pcie_width_cap)	smu_ppt_funcs(update_pcie_parameters, 0, smu, pcie_gen_cap, pcie_width_cap)
#define smu_set_power_source(smu, power_src)				smu_ppt_funcs(set_power_source, 0, smu, power_src)
#define smu_i2c_init(smu)                                               smu_ppt_funcs(i2c_init, 0, smu)
#define smu_i2c_fini(smu)                                               smu_ppt_funcs(i2c_fini, 0, smu)
#define smu_get_unique_id(smu)						smu_ppt_funcs(get_unique_id, 0, smu)
#define smu_log_thermal_throttling(smu)					smu_ppt_funcs(log_thermal_throttling_event, 0, smu)
#define smu_get_asic_power_limits(smu, current, default, max, min)		smu_ppt_funcs(get_power_limit, 0, smu, current, default, max, min)
#define smu_get_pp_feature_mask(smu, buf)				smu_ppt_funcs(get_pp_feature_mask, 0, smu, buf)
#define smu_set_pp_feature_mask(smu, new_mask)				smu_ppt_funcs(set_pp_feature_mask, 0, smu, new_mask)
#define smu_gfx_ulv_control(smu, enablement)				smu_ppt_funcs(gfx_ulv_control, 0, smu, enablement)
#define smu_deep_sleep_control(smu, enablement)				smu_ppt_funcs(deep_sleep_control, 0, smu, enablement)
#define smu_get_fan_parameters(smu)					smu_ppt_funcs(get_fan_parameters, 0, smu)
#define smu_post_init(smu)						smu_ppt_funcs(post_init, 0, smu)
#define smu_gpo_control(smu, enablement)				smu_ppt_funcs(gpo_control, 0, smu, enablement)
#define smu_set_fine_grain_gfx_freq_parameters(smu)					smu_ppt_funcs(set_fine_grain_gfx_freq_parameters, 0, smu)
#define smu_get_default_config_table_settings(smu, config_table)	smu_ppt_funcs(get_default_config_table_settings, -EOPNOTSUPP, smu, config_table)
#define smu_set_config_table(smu, config_table)				smu_ppt_funcs(set_config_table, -EOPNOTSUPP, smu, config_table)
#define smu_init_pptable_microcode(smu)					smu_ppt_funcs(init_pptable_microcode, 0, smu)
#define smu_notify_rlc_state(smu, en)					smu_ppt_funcs(notify_rlc_state, 0, smu, en)
#define smu_is_asic_wbrf_supported(smu)			smu_ppt_funcs(is_asic_wbrf_supported, false, smu)
#define smu_enable_uclk_shadow(smu, enable)		smu_ppt_funcs(enable_uclk_shadow, 0, smu, enable)
#define smu_set_wbrf_exclusion_ranges(smu, freq_band_range)		smu_ppt_funcs(set_wbrf_exclusion_ranges, -EOPNOTSUPP, smu, freq_band_range)

#endif
#endif
