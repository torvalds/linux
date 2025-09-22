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
 *
 */
#ifndef __SMU_V11_0_H__
#define __SMU_V11_0_H__

#include "amdgpu_smu.h"

#define SMU11_DRIVER_IF_VERSION_INV 0xFFFFFFFF
#define SMU11_DRIVER_IF_VERSION_ARCT 0x17
#define SMU11_DRIVER_IF_VERSION_NV10 0x37
#define SMU11_DRIVER_IF_VERSION_NV12 0x38
#define SMU11_DRIVER_IF_VERSION_NV14 0x38
#define SMU11_DRIVER_IF_VERSION_Sienna_Cichlid 0x40
#define SMU11_DRIVER_IF_VERSION_Navy_Flounder 0xE
#define SMU11_DRIVER_IF_VERSION_VANGOGH 0x03
#define SMU11_DRIVER_IF_VERSION_Dimgrey_Cavefish 0xF
#define SMU11_DRIVER_IF_VERSION_Beige_Goby 0xD
#define SMU11_DRIVER_IF_VERSION_Cyan_Skillfish 0x8

/* MP Apertures */
#define MP0_Public			0x03800000
#define MP0_SRAM			0x03900000
#define MP1_Public			0x03b00000
#define MP1_SRAM			0x03c00004

/* address block */
#define smnMP1_FIRMWARE_FLAGS		0x3010024
#define smnMP0_FW_INTF			0x30101c0
#define smnMP1_PUB_CTRL			0x3010b14

#define TEMP_RANGE_MIN			(0)
#define TEMP_RANGE_MAX			(80 * 1000)

#define SMU11_TOOL_SIZE			0x19000

#define MAX_DPM_LEVELS 16
#define MAX_PCIE_CONF 2

#define CTF_OFFSET_EDGE			5
#define CTF_OFFSET_HOTSPOT		5
#define CTF_OFFSET_MEM			5

#define LINK_WIDTH_MAX			6
#define LINK_SPEED_MAX			3

static const __maybe_unused uint16_t link_width[] = {0, 1, 2, 4, 8, 12, 16};

static const
struct smu_temperature_range __maybe_unused smu11_thermal_policy[] = {
	{-273150,  99000, 99000, -273150, 99000, 99000, -273150, 99000, 99000},
	{ 120000, 120000, 120000, 120000, 120000, 120000, 120000, 120000, 120000},
};

struct smu_11_0_max_sustainable_clocks {
	uint32_t display_clock;
	uint32_t phy_clock;
	uint32_t pixel_clock;
	uint32_t uclock;
	uint32_t dcef_clock;
	uint32_t soc_clock;
};

struct smu_11_0_dpm_clk_level {
	bool				enabled;
	uint32_t			value;
};

struct smu_11_0_dpm_table {
	uint32_t			min;        /* MHz */
	uint32_t			max;        /* MHz */
	uint32_t			count;
	bool				is_fine_grained;
	struct smu_11_0_dpm_clk_level	dpm_levels[MAX_DPM_LEVELS];
};

struct smu_11_0_pcie_table {
	uint8_t  pcie_gen[MAX_PCIE_CONF];
	uint8_t  pcie_lane[MAX_PCIE_CONF];
};

struct smu_11_0_dpm_tables {
	struct smu_11_0_dpm_table        soc_table;
	struct smu_11_0_dpm_table        gfx_table;
	struct smu_11_0_dpm_table        uclk_table;
	struct smu_11_0_dpm_table        eclk_table;
	struct smu_11_0_dpm_table        vclk_table;
	struct smu_11_0_dpm_table        vclk1_table;
	struct smu_11_0_dpm_table        dclk_table;
	struct smu_11_0_dpm_table        dclk1_table;
	struct smu_11_0_dpm_table        dcef_table;
	struct smu_11_0_dpm_table        pixel_table;
	struct smu_11_0_dpm_table        display_table;
	struct smu_11_0_dpm_table        phy_table;
	struct smu_11_0_dpm_table        fclk_table;
	struct smu_11_0_pcie_table       pcie_table;
};

struct smu_11_0_dpm_context {
	struct smu_11_0_dpm_tables  dpm_tables;
	uint32_t                    workload_policy_mask;
	uint32_t                    dcef_min_ds_clk;
};

enum smu_11_0_power_state {
	SMU_11_0_POWER_STATE__D0 = 0,
	SMU_11_0_POWER_STATE__D1,
	SMU_11_0_POWER_STATE__D3, /* Sleep*/
	SMU_11_0_POWER_STATE__D4, /* Hibernate*/
	SMU_11_0_POWER_STATE__D5, /* Power off*/
};

struct smu_11_0_power_context {
	uint32_t	power_source;
	uint8_t		in_power_limit_boost_mode;
	enum smu_11_0_power_state power_state;
};

struct smu_11_5_power_context {
	uint32_t	power_source;
	uint8_t		in_power_limit_boost_mode;
	enum smu_11_0_power_state power_state;

	uint32_t	current_fast_ppt_limit;
	uint32_t	default_fast_ppt_limit;
	uint32_t	max_fast_ppt_limit;
};

#if defined(SWSMU_CODE_LAYER_L2) || defined(SWSMU_CODE_LAYER_L3)

int smu_v11_0_init_microcode(struct smu_context *smu);

void smu_v11_0_fini_microcode(struct smu_context *smu);

int smu_v11_0_load_microcode(struct smu_context *smu);

int smu_v11_0_init_smc_tables(struct smu_context *smu);

int smu_v11_0_fini_smc_tables(struct smu_context *smu);

int smu_v11_0_init_power(struct smu_context *smu);

int smu_v11_0_fini_power(struct smu_context *smu);

int smu_v11_0_check_fw_status(struct smu_context *smu);

int smu_v11_0_setup_pptable(struct smu_context *smu);

int smu_v11_0_get_vbios_bootup_values(struct smu_context *smu);

int smu_v11_0_check_fw_version(struct smu_context *smu);

int smu_v11_0_set_driver_table_location(struct smu_context *smu);

int smu_v11_0_set_tool_table_location(struct smu_context *smu);

int smu_v11_0_notify_memory_pool_location(struct smu_context *smu);

int smu_v11_0_system_features_control(struct smu_context *smu,
					     bool en);

int smu_v11_0_init_display_count(struct smu_context *smu, uint32_t count);

int smu_v11_0_set_allowed_mask(struct smu_context *smu);

int smu_v11_0_notify_display_change(struct smu_context *smu);

int smu_v11_0_get_current_power_limit(struct smu_context *smu,
				      uint32_t *power_limit);

int smu_v11_0_set_power_limit(struct smu_context *smu,
			      enum smu_ppt_limit_type limit_type,
			      uint32_t limit);

int smu_v11_0_init_max_sustainable_clocks(struct smu_context *smu);

int smu_v11_0_enable_thermal_alert(struct smu_context *smu);

int smu_v11_0_disable_thermal_alert(struct smu_context *smu);

int smu_v11_0_get_gfx_vdd(struct smu_context *smu, uint32_t *value);

int smu_v11_0_set_min_deep_sleep_dcefclk(struct smu_context *smu, uint32_t clk);

int
smu_v11_0_display_clock_voltage_request(struct smu_context *smu,
					struct pp_display_clock_request
					*clock_req);

uint32_t
smu_v11_0_get_fan_control_mode(struct smu_context *smu);

int
smu_v11_0_set_fan_control_mode(struct smu_context *smu,
			       uint32_t mode);

int smu_v11_0_set_fan_speed_pwm(struct smu_context *smu,
				    uint32_t speed);

int smu_v11_0_set_fan_speed_rpm(struct smu_context *smu,
				uint32_t speed);

int smu_v11_0_get_fan_speed_pwm(struct smu_context *smu,
				    uint32_t *speed);

int smu_v11_0_get_fan_speed_rpm(struct smu_context *smu,
				uint32_t *speed);

int smu_v11_0_set_xgmi_pstate(struct smu_context *smu,
				     uint32_t pstate);

int smu_v11_0_gfx_off_control(struct smu_context *smu, bool enable);

int smu_v11_0_register_irq_handler(struct smu_context *smu);

int smu_v11_0_set_azalia_d3_pme(struct smu_context *smu);

int smu_v11_0_get_max_sustainable_clocks_by_dc(struct smu_context *smu,
		struct pp_smu_nv_clock_table *max_clocks);

int smu_v11_0_get_bamaco_support(struct smu_context *smu);

enum smu_baco_state smu_v11_0_baco_get_state(struct smu_context *smu);

int smu_v11_0_baco_set_state(struct smu_context *smu, enum smu_baco_state state);

int smu_v11_0_baco_enter(struct smu_context *smu);
int smu_v11_0_baco_exit(struct smu_context *smu);

int smu_v11_0_baco_set_armd3_sequence(struct smu_context *smu,
				      enum smu_baco_seq baco_seq);

int smu_v11_0_mode1_reset(struct smu_context *smu);

int smu_v11_0_get_dpm_ultimate_freq(struct smu_context *smu, enum smu_clk_type clk_type,
						 uint32_t *min, uint32_t *max);

int smu_v11_0_set_soft_freq_limited_range(struct smu_context *smu, enum smu_clk_type clk_type,
			    uint32_t min, uint32_t max);

int smu_v11_0_set_hard_freq_limited_range(struct smu_context *smu,
					  enum smu_clk_type clk_type,
					  uint32_t min,
					  uint32_t max);

int smu_v11_0_set_performance_level(struct smu_context *smu,
				    enum amd_dpm_forced_level level);

int smu_v11_0_set_power_source(struct smu_context *smu,
			       enum smu_power_src_type power_src);

int smu_v11_0_get_dpm_freq_by_index(struct smu_context *smu,
				    enum smu_clk_type clk_type,
				    uint16_t level,
				    uint32_t *value);

int smu_v11_0_get_dpm_level_count(struct smu_context *smu,
				  enum smu_clk_type clk_type,
				  uint32_t *value);

int smu_v11_0_set_single_dpm_table(struct smu_context *smu,
				   enum smu_clk_type clk_type,
				   struct smu_11_0_dpm_table *single_dpm_table);

int smu_v11_0_get_dpm_level_range(struct smu_context *smu,
				  enum smu_clk_type clk_type,
				  uint32_t *min_value,
				  uint32_t *max_value);

int smu_v11_0_get_current_pcie_link_width_level(struct smu_context *smu);

uint16_t smu_v11_0_get_current_pcie_link_width(struct smu_context *smu);

int smu_v11_0_get_current_pcie_link_speed_level(struct smu_context *smu);

uint16_t smu_v11_0_get_current_pcie_link_speed(struct smu_context *smu);

int smu_v11_0_gfx_ulv_control(struct smu_context *smu,
			      bool enablement);

int smu_v11_0_deep_sleep_control(struct smu_context *smu,
				 bool enablement);

void smu_v11_0_interrupt_work(struct smu_context *smu);

int smu_v11_0_handle_passthrough_sbr(struct smu_context *smu, bool enable);

int smu_v11_0_restore_user_od_settings(struct smu_context *smu);

void smu_v11_0_set_smu_mailbox_registers(struct smu_context *smu);

#endif
#endif
