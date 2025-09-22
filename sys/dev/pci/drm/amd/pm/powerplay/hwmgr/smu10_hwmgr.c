/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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
#include "pp_debug.h"
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include "atom-types.h"
#include "atombios.h"
#include "processpptables.h"
#include "cgs_common.h"
#include "smumgr.h"
#include "hwmgr.h"
#include "hardwaremanager.h"
#include "rv_ppsmc.h"
#include "smu10_hwmgr.h"
#include "power_state.h"
#include "soc15_common.h"
#include "smu10.h"
#include "asic_reg/pwr/pwr_10_0_offset.h"
#include "asic_reg/pwr/pwr_10_0_sh_mask.h"

#define SMU10_MAX_DEEPSLEEP_DIVIDER_ID     5
#define SMU10_MINIMUM_ENGINE_CLOCK         800   /* 8Mhz, the low boundary of engine clock allowed on this chip */
#define SCLK_MIN_DIV_INTV_SHIFT         12
#define SMU10_DISPCLK_BYPASS_THRESHOLD     10000 /* 100Mhz */
#define SMC_RAM_END                     0x40000

static const unsigned long SMU10_Magic = (unsigned long) PHM_Rv_Magic;


static int smu10_display_clock_voltage_request(struct pp_hwmgr *hwmgr,
		struct pp_display_clock_request *clock_req)
{
	struct smu10_hwmgr *smu10_data = (struct smu10_hwmgr *)(hwmgr->backend);
	enum amd_pp_clock_type clk_type = clock_req->clock_type;
	uint32_t clk_freq = clock_req->clock_freq_in_khz / 1000;
	PPSMC_Msg        msg;

	switch (clk_type) {
	case amd_pp_dcf_clock:
		if (clk_freq == smu10_data->dcf_actual_hard_min_freq)
			return 0;
		msg =  PPSMC_MSG_SetHardMinDcefclkByFreq;
		smu10_data->dcf_actual_hard_min_freq = clk_freq;
		break;
	case amd_pp_soc_clock:
		 msg = PPSMC_MSG_SetHardMinSocclkByFreq;
		break;
	case amd_pp_f_clock:
		if (clk_freq == smu10_data->f_actual_hard_min_freq)
			return 0;
		smu10_data->f_actual_hard_min_freq = clk_freq;
		msg = PPSMC_MSG_SetHardMinFclkByFreq;
		break;
	default:
		pr_info("[DisplayClockVoltageRequest]Invalid Clock Type!");
		return -EINVAL;
	}
	smum_send_msg_to_smc_with_parameter(hwmgr, msg, clk_freq, NULL);

	return 0;
}

static struct smu10_power_state *cast_smu10_ps(struct pp_hw_power_state *hw_ps)
{
	if (SMU10_Magic != hw_ps->magic)
		return NULL;

	return (struct smu10_power_state *)hw_ps;
}

static const struct smu10_power_state *cast_const_smu10_ps(
				const struct pp_hw_power_state *hw_ps)
{
	if (SMU10_Magic != hw_ps->magic)
		return NULL;

	return (struct smu10_power_state *)hw_ps;
}

static int smu10_initialize_dpm_defaults(struct pp_hwmgr *hwmgr)
{
	struct smu10_hwmgr *smu10_data = (struct smu10_hwmgr *)(hwmgr->backend);

	smu10_data->dce_slow_sclk_threshold = 30000;
	smu10_data->thermal_auto_throttling_treshold = 0;
	smu10_data->is_nb_dpm_enabled = 1;
	smu10_data->dpm_flags = 1;
	smu10_data->need_min_deep_sleep_dcefclk = true;
	smu10_data->num_active_display = 0;
	smu10_data->deep_sleep_dcefclk = 0;

	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
					PHM_PlatformCaps_SclkDeepSleep);

	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_SclkThrottleLowNotification);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_PowerPlaySupport);
	return 0;
}

static int smu10_construct_max_power_limits_table(struct pp_hwmgr *hwmgr,
			struct phm_clock_and_voltage_limits *table)
{
	return 0;
}

static int smu10_init_dynamic_state_adjustment_rule_settings(
							struct pp_hwmgr *hwmgr)
{
	int count = 8;
	struct phm_clock_voltage_dependency_table *table_clk_vlt;

	table_clk_vlt = kzalloc(struct_size(table_clk_vlt, entries, count),
				GFP_KERNEL);

	if (NULL == table_clk_vlt) {
		pr_err("Can not allocate memory!\n");
		return -ENOMEM;
	}

	table_clk_vlt->count = count;
	table_clk_vlt->entries[0].clk = PP_DAL_POWERLEVEL_0;
	table_clk_vlt->entries[0].v = 0;
	table_clk_vlt->entries[1].clk = PP_DAL_POWERLEVEL_1;
	table_clk_vlt->entries[1].v = 1;
	table_clk_vlt->entries[2].clk = PP_DAL_POWERLEVEL_2;
	table_clk_vlt->entries[2].v = 2;
	table_clk_vlt->entries[3].clk = PP_DAL_POWERLEVEL_3;
	table_clk_vlt->entries[3].v = 3;
	table_clk_vlt->entries[4].clk = PP_DAL_POWERLEVEL_4;
	table_clk_vlt->entries[4].v = 4;
	table_clk_vlt->entries[5].clk = PP_DAL_POWERLEVEL_5;
	table_clk_vlt->entries[5].v = 5;
	table_clk_vlt->entries[6].clk = PP_DAL_POWERLEVEL_6;
	table_clk_vlt->entries[6].v = 6;
	table_clk_vlt->entries[7].clk = PP_DAL_POWERLEVEL_7;
	table_clk_vlt->entries[7].v = 7;
	hwmgr->dyn_state.vddc_dep_on_dal_pwrl = table_clk_vlt;

	return 0;
}

static int smu10_get_system_info_data(struct pp_hwmgr *hwmgr)
{
	struct smu10_hwmgr *smu10_data = (struct smu10_hwmgr *)hwmgr->backend;

	smu10_data->sys_info.htc_hyst_lmt = 5;
	smu10_data->sys_info.htc_tmp_lmt = 203;

	if (smu10_data->thermal_auto_throttling_treshold == 0)
		 smu10_data->thermal_auto_throttling_treshold = 203;

	smu10_construct_max_power_limits_table (hwmgr,
				    &hwmgr->dyn_state.max_clock_voltage_on_ac);

	smu10_init_dynamic_state_adjustment_rule_settings(hwmgr);

	return 0;
}

static int smu10_construct_boot_state(struct pp_hwmgr *hwmgr)
{
	return 0;
}

static int smu10_set_clock_limit(struct pp_hwmgr *hwmgr, const void *input)
{
	struct PP_Clocks clocks = {0};
	struct pp_display_clock_request clock_req;

	clocks.dcefClock = hwmgr->display_config->min_dcef_set_clk;
	clock_req.clock_type = amd_pp_dcf_clock;
	clock_req.clock_freq_in_khz = clocks.dcefClock * 10;

	PP_ASSERT_WITH_CODE(!smu10_display_clock_voltage_request(hwmgr, &clock_req),
				"Attempt to set DCF Clock Failed!", return -EINVAL);

	return 0;
}

static int smu10_set_min_deep_sleep_dcefclk(struct pp_hwmgr *hwmgr, uint32_t clock)
{
	struct smu10_hwmgr *smu10_data = (struct smu10_hwmgr *)(hwmgr->backend);

	if (clock && smu10_data->deep_sleep_dcefclk != clock) {
		smu10_data->deep_sleep_dcefclk = clock;
		smum_send_msg_to_smc_with_parameter(hwmgr,
					PPSMC_MSG_SetMinDeepSleepDcefclk,
					smu10_data->deep_sleep_dcefclk,
					NULL);
	}
	return 0;
}

static int smu10_set_hard_min_dcefclk_by_freq(struct pp_hwmgr *hwmgr, uint32_t clock)
{
	struct smu10_hwmgr *smu10_data = (struct smu10_hwmgr *)(hwmgr->backend);

	if (clock && smu10_data->dcf_actual_hard_min_freq != clock) {
		smu10_data->dcf_actual_hard_min_freq = clock;
		smum_send_msg_to_smc_with_parameter(hwmgr,
					PPSMC_MSG_SetHardMinDcefclkByFreq,
					smu10_data->dcf_actual_hard_min_freq,
					NULL);
	}
	return 0;
}

static int smu10_set_hard_min_fclk_by_freq(struct pp_hwmgr *hwmgr, uint32_t clock)
{
	struct smu10_hwmgr *smu10_data = (struct smu10_hwmgr *)(hwmgr->backend);

	if (clock && smu10_data->f_actual_hard_min_freq != clock) {
		smu10_data->f_actual_hard_min_freq = clock;
		smum_send_msg_to_smc_with_parameter(hwmgr,
					PPSMC_MSG_SetHardMinFclkByFreq,
					smu10_data->f_actual_hard_min_freq,
					NULL);
	}
	return 0;
}

static int smu10_set_hard_min_gfxclk_by_freq(struct pp_hwmgr *hwmgr, uint32_t clock)
{
	struct smu10_hwmgr *smu10_data = (struct smu10_hwmgr *)(hwmgr->backend);

	if (clock && smu10_data->gfx_actual_soft_min_freq != clock) {
		smu10_data->gfx_actual_soft_min_freq = clock;
		smum_send_msg_to_smc_with_parameter(hwmgr,
					PPSMC_MSG_SetHardMinGfxClk,
					clock,
					NULL);
	}
	return 0;
}

static int smu10_set_soft_max_gfxclk_by_freq(struct pp_hwmgr *hwmgr, uint32_t clock)
{
	struct smu10_hwmgr *smu10_data = (struct smu10_hwmgr *)(hwmgr->backend);

	if (clock && smu10_data->gfx_max_freq_limit != (clock * 100))  {
		smu10_data->gfx_max_freq_limit = clock * 100;
		smum_send_msg_to_smc_with_parameter(hwmgr,
					PPSMC_MSG_SetSoftMaxGfxClk,
					clock,
					NULL);
	}
	return 0;
}

static int smu10_set_active_display_count(struct pp_hwmgr *hwmgr, uint32_t count)
{
	struct smu10_hwmgr *smu10_data = (struct smu10_hwmgr *)(hwmgr->backend);

	if (smu10_data->num_active_display != count) {
		smu10_data->num_active_display = count;
		smum_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_SetDisplayCount,
				smu10_data->num_active_display,
				NULL);
	}

	return 0;
}

static int smu10_set_power_state_tasks(struct pp_hwmgr *hwmgr, const void *input)
{
	return smu10_set_clock_limit(hwmgr, input);
}

static int smu10_init_power_gate_state(struct pp_hwmgr *hwmgr)
{
	struct smu10_hwmgr *smu10_data = (struct smu10_hwmgr *)(hwmgr->backend);
	struct amdgpu_device *adev = hwmgr->adev;

	smu10_data->vcn_power_gated = true;
	smu10_data->isp_tileA_power_gated = true;
	smu10_data->isp_tileB_power_gated = true;

	if (adev->pg_flags & AMD_PG_SUPPORT_GFX_PG)
		return smum_send_msg_to_smc_with_parameter(hwmgr,
							   PPSMC_MSG_SetGfxCGPG,
							   true,
							   NULL);
	else
		return 0;
}


static int smu10_setup_asic_task(struct pp_hwmgr *hwmgr)
{
	return smu10_init_power_gate_state(hwmgr);
}

static int smu10_reset_cc6_data(struct pp_hwmgr *hwmgr)
{
	struct smu10_hwmgr *smu10_data = (struct smu10_hwmgr *)(hwmgr->backend);

	smu10_data->separation_time = 0;
	smu10_data->cc6_disable = false;
	smu10_data->pstate_disable = false;
	smu10_data->cc6_setting_changed = false;

	return 0;
}

static int smu10_power_off_asic(struct pp_hwmgr *hwmgr)
{
	return smu10_reset_cc6_data(hwmgr);
}

static bool smu10_is_gfx_on(struct pp_hwmgr *hwmgr)
{
	uint32_t reg;
	struct amdgpu_device *adev = hwmgr->adev;

	reg = RREG32_SOC15(PWR, 0, mmPWR_MISC_CNTL_STATUS);
	if ((reg & PWR_MISC_CNTL_STATUS__PWR_GFXOFF_STATUS_MASK) ==
	    (0x2 << PWR_MISC_CNTL_STATUS__PWR_GFXOFF_STATUS__SHIFT))
		return true;

	return false;
}

static int smu10_disable_gfx_off(struct pp_hwmgr *hwmgr)
{
	struct amdgpu_device *adev = hwmgr->adev;

	if (adev->pm.pp_feature & PP_GFXOFF_MASK) {
		smum_send_msg_to_smc(hwmgr, PPSMC_MSG_DisableGfxOff, NULL);

		/* confirm gfx is back to "on" state */
		while (!smu10_is_gfx_on(hwmgr))
			drm_msleep(1);
	}

	return 0;
}

static int smu10_disable_dpm_tasks(struct pp_hwmgr *hwmgr)
{
	return 0;
}

static int smu10_enable_gfx_off(struct pp_hwmgr *hwmgr)
{
	struct amdgpu_device *adev = hwmgr->adev;

	if (adev->pm.pp_feature & PP_GFXOFF_MASK)
		smum_send_msg_to_smc(hwmgr, PPSMC_MSG_EnableGfxOff, NULL);

	return 0;
}

static void smu10_populate_umdpstate_clocks(struct pp_hwmgr *hwmgr)
{
	hwmgr->pstate_sclk = SMU10_UMD_PSTATE_GFXCLK;
	hwmgr->pstate_mclk = SMU10_UMD_PSTATE_FCLK;

	smum_send_msg_to_smc(hwmgr,
			     PPSMC_MSG_GetMaxGfxclkFrequency,
			     &hwmgr->pstate_sclk_peak);
	hwmgr->pstate_mclk_peak = SMU10_UMD_PSTATE_PEAK_FCLK;
}

static int smu10_enable_dpm_tasks(struct pp_hwmgr *hwmgr)
{
	struct amdgpu_device *adev = hwmgr->adev;
	struct smu10_hwmgr *smu10_data = (struct smu10_hwmgr *)(hwmgr->backend);
	int ret = -EINVAL;

	if (adev->in_suspend) {
		pr_info("restore the fine grain parameters\n");

		ret = smum_send_msg_to_smc_with_parameter(hwmgr,
					PPSMC_MSG_SetHardMinGfxClk,
					smu10_data->gfx_actual_soft_min_freq,
					NULL);
		if (ret)
			return ret;
		ret = smum_send_msg_to_smc_with_parameter(hwmgr,
					PPSMC_MSG_SetSoftMaxGfxClk,
					smu10_data->gfx_actual_soft_max_freq,
					NULL);
		if (ret)
			return ret;
	}

	smu10_populate_umdpstate_clocks(hwmgr);

	return 0;
}

static int smu10_gfx_off_control(struct pp_hwmgr *hwmgr, bool enable)
{
	if (enable)
		return smu10_enable_gfx_off(hwmgr);
	else
		return smu10_disable_gfx_off(hwmgr);
}

static int smu10_apply_state_adjust_rules(struct pp_hwmgr *hwmgr,
				struct pp_power_state  *prequest_ps,
			const struct pp_power_state *pcurrent_ps)
{
	return 0;
}

/* temporary hardcoded clock voltage breakdown tables */
static const DpmClock_t VddDcfClk[] = {
	{ 300, 2600},
	{ 600, 3200},
	{ 600, 3600},
};

static const DpmClock_t VddSocClk[] = {
	{ 478, 2600},
	{ 722, 3200},
	{ 722, 3600},
};

static const DpmClock_t VddFClk[] = {
	{ 400, 2600},
	{1200, 3200},
	{1200, 3600},
};

static const DpmClock_t VddDispClk[] = {
	{ 435, 2600},
	{ 661, 3200},
	{1086, 3600},
};

static const DpmClock_t VddDppClk[] = {
	{ 435, 2600},
	{ 661, 3200},
	{ 661, 3600},
};

static const DpmClock_t VddPhyClk[] = {
	{ 540, 2600},
	{ 810, 3200},
	{ 810, 3600},
};

static int smu10_get_clock_voltage_dependency_table(struct pp_hwmgr *hwmgr,
			struct smu10_voltage_dependency_table **pptable,
			uint32_t num_entry, const DpmClock_t *pclk_dependency_table)
{
	uint32_t i;
	struct smu10_voltage_dependency_table *ptable;

	ptable = kzalloc(struct_size(ptable, entries, num_entry), GFP_KERNEL);
	if (NULL == ptable)
		return -ENOMEM;

	ptable->count = num_entry;

	for (i = 0; i < ptable->count; i++) {
		ptable->entries[i].clk         = pclk_dependency_table->Freq * 100;
		ptable->entries[i].vol         = pclk_dependency_table->Vol;
		pclk_dependency_table++;
	}

	*pptable = ptable;

	return 0;
}


static int smu10_populate_clock_table(struct pp_hwmgr *hwmgr)
{
	uint32_t result;

	struct smu10_hwmgr *smu10_data = (struct smu10_hwmgr *)(hwmgr->backend);
	DpmClocks_t  *table = &(smu10_data->clock_table);
	struct smu10_clock_voltage_information *pinfo = &(smu10_data->clock_vol_info);

	result = smum_smc_table_manager(hwmgr, (uint8_t *)table, SMU10_CLOCKTABLE, true);

	PP_ASSERT_WITH_CODE((0 == result),
			"Attempt to copy clock table from smc failed",
			return result);

	if (0 == result && table->DcefClocks[0].Freq != 0) {
		smu10_get_clock_voltage_dependency_table(hwmgr, &pinfo->vdd_dep_on_dcefclk,
						NUM_DCEFCLK_DPM_LEVELS,
						&smu10_data->clock_table.DcefClocks[0]);
		smu10_get_clock_voltage_dependency_table(hwmgr, &pinfo->vdd_dep_on_socclk,
						NUM_SOCCLK_DPM_LEVELS,
						&smu10_data->clock_table.SocClocks[0]);
		smu10_get_clock_voltage_dependency_table(hwmgr, &pinfo->vdd_dep_on_fclk,
						NUM_FCLK_DPM_LEVELS,
						&smu10_data->clock_table.FClocks[0]);
		smu10_get_clock_voltage_dependency_table(hwmgr, &pinfo->vdd_dep_on_mclk,
						NUM_MEMCLK_DPM_LEVELS,
						&smu10_data->clock_table.MemClocks[0]);
	} else {
		smu10_get_clock_voltage_dependency_table(hwmgr, &pinfo->vdd_dep_on_dcefclk,
						ARRAY_SIZE(VddDcfClk),
						&VddDcfClk[0]);
		smu10_get_clock_voltage_dependency_table(hwmgr, &pinfo->vdd_dep_on_socclk,
						ARRAY_SIZE(VddSocClk),
						&VddSocClk[0]);
		smu10_get_clock_voltage_dependency_table(hwmgr, &pinfo->vdd_dep_on_fclk,
						ARRAY_SIZE(VddFClk),
						&VddFClk[0]);
	}
	smu10_get_clock_voltage_dependency_table(hwmgr, &pinfo->vdd_dep_on_dispclk,
					ARRAY_SIZE(VddDispClk),
					&VddDispClk[0]);
	smu10_get_clock_voltage_dependency_table(hwmgr, &pinfo->vdd_dep_on_dppclk,
					ARRAY_SIZE(VddDppClk), &VddDppClk[0]);
	smu10_get_clock_voltage_dependency_table(hwmgr, &pinfo->vdd_dep_on_phyclk,
					ARRAY_SIZE(VddPhyClk), &VddPhyClk[0]);

	smum_send_msg_to_smc(hwmgr, PPSMC_MSG_GetMinGfxclkFrequency, &result);
	smu10_data->gfx_min_freq_limit = result / 10 * 1000;

	smum_send_msg_to_smc(hwmgr, PPSMC_MSG_GetMaxGfxclkFrequency, &result);
	smu10_data->gfx_max_freq_limit = result / 10 * 1000;

	return 0;
}

static int smu10_hwmgr_backend_init(struct pp_hwmgr *hwmgr)
{
	int result = 0;
	struct smu10_hwmgr *data;

	data = kzalloc(sizeof(struct smu10_hwmgr), GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;

	hwmgr->backend = data;

	result = smu10_initialize_dpm_defaults(hwmgr);
	if (result != 0) {
		pr_err("smu10_initialize_dpm_defaults failed\n");
		return result;
	}

	smu10_populate_clock_table(hwmgr);

	result = smu10_get_system_info_data(hwmgr);
	if (result != 0) {
		pr_err("smu10_get_system_info_data failed\n");
		return result;
	}

	smu10_construct_boot_state(hwmgr);

	hwmgr->platform_descriptor.hardwareActivityPerformanceLevels =
						SMU10_MAX_HARDWARE_POWERLEVELS;

	hwmgr->platform_descriptor.hardwarePerformanceLevels =
						SMU10_MAX_HARDWARE_POWERLEVELS;

	hwmgr->platform_descriptor.vbiosInterruptId = 0;

	hwmgr->platform_descriptor.clockStep.engineClock = 500;

	hwmgr->platform_descriptor.clockStep.memoryClock = 500;

	hwmgr->platform_descriptor.minimumClocksReductionPercentage = 50;

	/* enable the pp_od_clk_voltage sysfs file */
	hwmgr->od_enabled = 1;
	/* disabled fine grain tuning function by default */
	data->fine_grain_enabled = 0;
	return result;
}

static int smu10_hwmgr_backend_fini(struct pp_hwmgr *hwmgr)
{
	struct smu10_hwmgr *smu10_data = (struct smu10_hwmgr *)(hwmgr->backend);
	struct smu10_clock_voltage_information *pinfo = &(smu10_data->clock_vol_info);

	kfree(pinfo->vdd_dep_on_dcefclk);
	pinfo->vdd_dep_on_dcefclk = NULL;
	kfree(pinfo->vdd_dep_on_socclk);
	pinfo->vdd_dep_on_socclk = NULL;
	kfree(pinfo->vdd_dep_on_fclk);
	pinfo->vdd_dep_on_fclk = NULL;
	kfree(pinfo->vdd_dep_on_dispclk);
	pinfo->vdd_dep_on_dispclk = NULL;
	kfree(pinfo->vdd_dep_on_dppclk);
	pinfo->vdd_dep_on_dppclk = NULL;
	kfree(pinfo->vdd_dep_on_phyclk);
	pinfo->vdd_dep_on_phyclk = NULL;

	kfree(hwmgr->dyn_state.vddc_dep_on_dal_pwrl);
	hwmgr->dyn_state.vddc_dep_on_dal_pwrl = NULL;

	kfree(hwmgr->backend);
	hwmgr->backend = NULL;

	return 0;
}

static int smu10_dpm_force_dpm_level(struct pp_hwmgr *hwmgr,
				enum amd_dpm_forced_level level)
{
	struct smu10_hwmgr *data = hwmgr->backend;
	uint32_t min_sclk = hwmgr->display_config->min_core_set_clock;
	uint32_t min_mclk = hwmgr->display_config->min_mem_set_clock/100;
	uint32_t index_fclk = data->clock_vol_info.vdd_dep_on_fclk->count - 1;
	uint32_t index_socclk = data->clock_vol_info.vdd_dep_on_socclk->count - 1;
	uint32_t fine_grain_min_freq = 0, fine_grain_max_freq = 0;

	if (hwmgr->smu_version < 0x1E3700) {
		pr_info("smu firmware version too old, can not set dpm level\n");
		return 0;
	}

	if (min_sclk < data->gfx_min_freq_limit)
		min_sclk = data->gfx_min_freq_limit;

	min_sclk /= 100; /* transfer 10KHz to MHz */
	if (min_mclk < data->clock_table.FClocks[0].Freq)
		min_mclk = data->clock_table.FClocks[0].Freq;

	switch (level) {
	case AMD_DPM_FORCED_LEVEL_HIGH:
	case AMD_DPM_FORCED_LEVEL_PROFILE_PEAK:
		data->fine_grain_enabled = 0;

		smum_send_msg_to_smc(hwmgr, PPSMC_MSG_GetMinGfxclkFrequency, &fine_grain_min_freq);
		smum_send_msg_to_smc(hwmgr, PPSMC_MSG_GetMaxGfxclkFrequency, &fine_grain_max_freq);

		data->gfx_actual_soft_min_freq = fine_grain_min_freq;
		data->gfx_actual_soft_max_freq = fine_grain_max_freq;

		smum_send_msg_to_smc_with_parameter(hwmgr,
						PPSMC_MSG_SetHardMinGfxClk,
						data->gfx_max_freq_limit/100,
						NULL);
		smum_send_msg_to_smc_with_parameter(hwmgr,
						PPSMC_MSG_SetHardMinFclkByFreq,
						SMU10_UMD_PSTATE_PEAK_FCLK,
						NULL);
		smum_send_msg_to_smc_with_parameter(hwmgr,
						PPSMC_MSG_SetHardMinSocclkByFreq,
						SMU10_UMD_PSTATE_PEAK_SOCCLK,
						NULL);
		smum_send_msg_to_smc_with_parameter(hwmgr,
						PPSMC_MSG_SetHardMinVcn,
						SMU10_UMD_PSTATE_VCE,
						NULL);

		smum_send_msg_to_smc_with_parameter(hwmgr,
						PPSMC_MSG_SetSoftMaxGfxClk,
						data->gfx_max_freq_limit/100,
						NULL);
		smum_send_msg_to_smc_with_parameter(hwmgr,
						PPSMC_MSG_SetSoftMaxFclkByFreq,
						SMU10_UMD_PSTATE_PEAK_FCLK,
						NULL);
		smum_send_msg_to_smc_with_parameter(hwmgr,
						PPSMC_MSG_SetSoftMaxSocclkByFreq,
						SMU10_UMD_PSTATE_PEAK_SOCCLK,
						NULL);
		smum_send_msg_to_smc_with_parameter(hwmgr,
						PPSMC_MSG_SetSoftMaxVcn,
						SMU10_UMD_PSTATE_VCE,
						NULL);
		break;
	case AMD_DPM_FORCED_LEVEL_PROFILE_MIN_SCLK:
		data->fine_grain_enabled = 0;

		smum_send_msg_to_smc(hwmgr, PPSMC_MSG_GetMinGfxclkFrequency, &fine_grain_min_freq);
		smum_send_msg_to_smc(hwmgr, PPSMC_MSG_GetMaxGfxclkFrequency, &fine_grain_max_freq);

		data->gfx_actual_soft_min_freq = fine_grain_min_freq;
		data->gfx_actual_soft_max_freq = fine_grain_max_freq;

		smum_send_msg_to_smc_with_parameter(hwmgr,
						PPSMC_MSG_SetHardMinGfxClk,
						min_sclk,
						NULL);
		smum_send_msg_to_smc_with_parameter(hwmgr,
						PPSMC_MSG_SetSoftMaxGfxClk,
						min_sclk,
						NULL);
		break;
	case AMD_DPM_FORCED_LEVEL_PROFILE_MIN_MCLK:
		data->fine_grain_enabled = 0;

		smum_send_msg_to_smc(hwmgr, PPSMC_MSG_GetMinGfxclkFrequency, &fine_grain_min_freq);
		smum_send_msg_to_smc(hwmgr, PPSMC_MSG_GetMaxGfxclkFrequency, &fine_grain_max_freq);

		data->gfx_actual_soft_min_freq = fine_grain_min_freq;
		data->gfx_actual_soft_max_freq = fine_grain_max_freq;

		smum_send_msg_to_smc_with_parameter(hwmgr,
						PPSMC_MSG_SetHardMinFclkByFreq,
						min_mclk,
						NULL);
		smum_send_msg_to_smc_with_parameter(hwmgr,
						PPSMC_MSG_SetSoftMaxFclkByFreq,
						min_mclk,
						NULL);
		break;
	case AMD_DPM_FORCED_LEVEL_PROFILE_STANDARD:
		data->fine_grain_enabled = 0;

		smum_send_msg_to_smc(hwmgr, PPSMC_MSG_GetMinGfxclkFrequency, &fine_grain_min_freq);
		smum_send_msg_to_smc(hwmgr, PPSMC_MSG_GetMaxGfxclkFrequency, &fine_grain_max_freq);

		data->gfx_actual_soft_min_freq = fine_grain_min_freq;
		data->gfx_actual_soft_max_freq = fine_grain_max_freq;

		smum_send_msg_to_smc_with_parameter(hwmgr,
						PPSMC_MSG_SetHardMinGfxClk,
						SMU10_UMD_PSTATE_GFXCLK,
						NULL);
		smum_send_msg_to_smc_with_parameter(hwmgr,
						PPSMC_MSG_SetHardMinFclkByFreq,
						SMU10_UMD_PSTATE_FCLK,
						NULL);
		smum_send_msg_to_smc_with_parameter(hwmgr,
						PPSMC_MSG_SetHardMinSocclkByFreq,
						SMU10_UMD_PSTATE_SOCCLK,
						NULL);
		smum_send_msg_to_smc_with_parameter(hwmgr,
						PPSMC_MSG_SetHardMinVcn,
						SMU10_UMD_PSTATE_PROFILE_VCE,
						NULL);

		smum_send_msg_to_smc_with_parameter(hwmgr,
						PPSMC_MSG_SetSoftMaxGfxClk,
						SMU10_UMD_PSTATE_GFXCLK,
						NULL);
		smum_send_msg_to_smc_with_parameter(hwmgr,
						PPSMC_MSG_SetSoftMaxFclkByFreq,
						SMU10_UMD_PSTATE_FCLK,
						NULL);
		smum_send_msg_to_smc_with_parameter(hwmgr,
						PPSMC_MSG_SetSoftMaxSocclkByFreq,
						SMU10_UMD_PSTATE_SOCCLK,
						NULL);
		smum_send_msg_to_smc_with_parameter(hwmgr,
						PPSMC_MSG_SetSoftMaxVcn,
						SMU10_UMD_PSTATE_PROFILE_VCE,
						NULL);
		break;
	case AMD_DPM_FORCED_LEVEL_AUTO:
		data->fine_grain_enabled = 0;

		smum_send_msg_to_smc(hwmgr, PPSMC_MSG_GetMinGfxclkFrequency, &fine_grain_min_freq);
		smum_send_msg_to_smc(hwmgr, PPSMC_MSG_GetMaxGfxclkFrequency, &fine_grain_max_freq);

		data->gfx_actual_soft_min_freq = fine_grain_min_freq;
		data->gfx_actual_soft_max_freq = fine_grain_max_freq;

		smum_send_msg_to_smc_with_parameter(hwmgr,
						PPSMC_MSG_SetHardMinGfxClk,
						min_sclk,
						NULL);
		smum_send_msg_to_smc_with_parameter(hwmgr,
						PPSMC_MSG_SetHardMinFclkByFreq,
						hwmgr->display_config->num_display > 3 ?
						(data->clock_vol_info.vdd_dep_on_fclk->entries[0].clk / 100) :
						min_mclk,
						NULL);

		smum_send_msg_to_smc_with_parameter(hwmgr,
						PPSMC_MSG_SetHardMinSocclkByFreq,
						data->clock_vol_info.vdd_dep_on_socclk->entries[0].clk / 100,
						NULL);
		smum_send_msg_to_smc_with_parameter(hwmgr,
						PPSMC_MSG_SetHardMinVcn,
						SMU10_UMD_PSTATE_MIN_VCE,
						NULL);

		smum_send_msg_to_smc_with_parameter(hwmgr,
						PPSMC_MSG_SetSoftMaxGfxClk,
						data->gfx_max_freq_limit/100,
						NULL);
		smum_send_msg_to_smc_with_parameter(hwmgr,
						PPSMC_MSG_SetSoftMaxFclkByFreq,
						data->clock_vol_info.vdd_dep_on_fclk->entries[index_fclk].clk / 100,
						NULL);
		smum_send_msg_to_smc_with_parameter(hwmgr,
						PPSMC_MSG_SetSoftMaxSocclkByFreq,
						data->clock_vol_info.vdd_dep_on_socclk->entries[index_socclk].clk / 100,
						NULL);
		smum_send_msg_to_smc_with_parameter(hwmgr,
						PPSMC_MSG_SetSoftMaxVcn,
						SMU10_UMD_PSTATE_VCE,
						NULL);
		break;
	case AMD_DPM_FORCED_LEVEL_LOW:
		data->fine_grain_enabled = 0;

		smum_send_msg_to_smc(hwmgr, PPSMC_MSG_GetMinGfxclkFrequency, &fine_grain_min_freq);
		smum_send_msg_to_smc(hwmgr, PPSMC_MSG_GetMaxGfxclkFrequency, &fine_grain_max_freq);

		data->gfx_actual_soft_min_freq = fine_grain_min_freq;
		data->gfx_actual_soft_max_freq = fine_grain_max_freq;

		smum_send_msg_to_smc_with_parameter(hwmgr,
						PPSMC_MSG_SetHardMinGfxClk,
						data->gfx_min_freq_limit/100,
						NULL);
		smum_send_msg_to_smc_with_parameter(hwmgr,
						PPSMC_MSG_SetSoftMaxGfxClk,
						data->gfx_min_freq_limit/100,
						NULL);
		smum_send_msg_to_smc_with_parameter(hwmgr,
						PPSMC_MSG_SetHardMinFclkByFreq,
						min_mclk,
						NULL);
		smum_send_msg_to_smc_with_parameter(hwmgr,
						PPSMC_MSG_SetSoftMaxFclkByFreq,
						min_mclk,
						NULL);
		break;
	case AMD_DPM_FORCED_LEVEL_MANUAL:
		data->fine_grain_enabled = 1;
		break;
	case AMD_DPM_FORCED_LEVEL_PROFILE_EXIT:
	default:
		break;
	}
	return 0;
}

static uint32_t smu10_dpm_get_mclk(struct pp_hwmgr *hwmgr, bool low)
{
	struct smu10_hwmgr *data;

	if (hwmgr == NULL)
		return -EINVAL;

	data = (struct smu10_hwmgr *)(hwmgr->backend);

	if (low)
		return data->clock_vol_info.vdd_dep_on_fclk->entries[0].clk;
	else
		return data->clock_vol_info.vdd_dep_on_fclk->entries[
			data->clock_vol_info.vdd_dep_on_fclk->count - 1].clk;
}

static uint32_t smu10_dpm_get_sclk(struct pp_hwmgr *hwmgr, bool low)
{
	struct smu10_hwmgr *data;

	if (hwmgr == NULL)
		return -EINVAL;

	data = (struct smu10_hwmgr *)(hwmgr->backend);

	if (low)
		return data->gfx_min_freq_limit;
	else
		return data->gfx_max_freq_limit;
}

static int smu10_dpm_patch_boot_state(struct pp_hwmgr *hwmgr,
					struct pp_hw_power_state *hw_ps)
{
	return 0;
}

static int smu10_dpm_get_pp_table_entry_callback(
						     struct pp_hwmgr *hwmgr,
					   struct pp_hw_power_state *hw_ps,
							  unsigned int index,
						     const void *clock_info)
{
	struct smu10_power_state *smu10_ps = cast_smu10_ps(hw_ps);

	smu10_ps->levels[index].engine_clock = 0;

	smu10_ps->levels[index].vddc_index = 0;
	smu10_ps->level = index + 1;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_SclkDeepSleep)) {
		smu10_ps->levels[index].ds_divider_index = 5;
		smu10_ps->levels[index].ss_divider_index = 5;
	}

	return 0;
}

static int smu10_dpm_get_num_of_pp_table_entries(struct pp_hwmgr *hwmgr)
{
	int result;
	unsigned long ret = 0;

	result = pp_tables_get_num_of_entries(hwmgr, &ret);

	return result ? 0 : ret;
}

static int smu10_dpm_get_pp_table_entry(struct pp_hwmgr *hwmgr,
		    unsigned long entry, struct pp_power_state *ps)
{
	int result;
	struct smu10_power_state *smu10_ps;

	ps->hardware.magic = SMU10_Magic;

	smu10_ps = cast_smu10_ps(&(ps->hardware));

	result = pp_tables_get_entry(hwmgr, entry, ps,
			smu10_dpm_get_pp_table_entry_callback);

	smu10_ps->uvd_clocks.vclk = ps->uvd_clocks.VCLK;
	smu10_ps->uvd_clocks.dclk = ps->uvd_clocks.DCLK;

	return result;
}

static int smu10_get_power_state_size(struct pp_hwmgr *hwmgr)
{
	return sizeof(struct smu10_power_state);
}

static int smu10_set_cpu_power_state(struct pp_hwmgr *hwmgr)
{
	return 0;
}


static int smu10_store_cc6_data(struct pp_hwmgr *hwmgr, uint32_t separation_time,
			bool cc6_disable, bool pstate_disable, bool pstate_switch_disable)
{
	struct smu10_hwmgr *data = (struct smu10_hwmgr *)(hwmgr->backend);

	if (separation_time != data->separation_time ||
			cc6_disable != data->cc6_disable ||
			pstate_disable != data->pstate_disable) {
		data->separation_time = separation_time;
		data->cc6_disable = cc6_disable;
		data->pstate_disable = pstate_disable;
		data->cc6_setting_changed = true;
	}
	return 0;
}

static int smu10_get_dal_power_level(struct pp_hwmgr *hwmgr,
		struct amd_pp_simple_clock_info *info)
{
	return -EINVAL;
}

static int smu10_force_clock_level(struct pp_hwmgr *hwmgr,
		enum pp_clock_type type, uint32_t mask)
{
	struct smu10_hwmgr *data = hwmgr->backend;
	struct smu10_voltage_dependency_table *mclk_table =
					data->clock_vol_info.vdd_dep_on_fclk;
	uint32_t low, high;

	low = mask ? (ffs(mask) - 1) : 0;
	high = mask ? (fls(mask) - 1) : 0;

	switch (type) {
	case PP_SCLK:
		if (low > 2 || high > 2) {
			pr_info("Currently sclk only support 3 levels on RV\n");
			return -EINVAL;
		}

		smum_send_msg_to_smc_with_parameter(hwmgr,
						PPSMC_MSG_SetHardMinGfxClk,
						low == 2 ? data->gfx_max_freq_limit/100 :
						low == 1 ? SMU10_UMD_PSTATE_GFXCLK :
						data->gfx_min_freq_limit/100,
						NULL);

		smum_send_msg_to_smc_with_parameter(hwmgr,
						PPSMC_MSG_SetSoftMaxGfxClk,
						high == 0 ? data->gfx_min_freq_limit/100 :
						high == 1 ? SMU10_UMD_PSTATE_GFXCLK :
						data->gfx_max_freq_limit/100,
						NULL);
		break;

	case PP_MCLK:
		if (low > mclk_table->count - 1 || high > mclk_table->count - 1)
			return -EINVAL;

		smum_send_msg_to_smc_with_parameter(hwmgr,
						PPSMC_MSG_SetHardMinFclkByFreq,
						mclk_table->entries[low].clk/100,
						NULL);

		smum_send_msg_to_smc_with_parameter(hwmgr,
						PPSMC_MSG_SetSoftMaxFclkByFreq,
						mclk_table->entries[high].clk/100,
						NULL);
		break;

	case PP_PCIE:
	default:
		break;
	}
	return 0;
}

static int smu10_print_clock_levels(struct pp_hwmgr *hwmgr,
		enum pp_clock_type type, char *buf)
{
	struct smu10_hwmgr *data = (struct smu10_hwmgr *)(hwmgr->backend);
	struct smu10_voltage_dependency_table *mclk_table =
			data->clock_vol_info.vdd_dep_on_fclk;
	uint32_t i, now, size = 0;
	uint32_t min_freq, max_freq = 0;
	uint32_t ret = 0;

	switch (type) {
	case PP_SCLK:
		ret = smum_send_msg_to_smc(hwmgr, PPSMC_MSG_GetGfxclkFrequency, &now);
		if (ret)
			return ret;

	/* driver only know min/max gfx_clk, Add level 1 for all other gfx clks */
		if (now == data->gfx_max_freq_limit/100)
			i = 2;
		else if (now == data->gfx_min_freq_limit/100)
			i = 0;
		else
			i = 1;

		size += snprintf(buf + size, PAGE_SIZE - size, "0: %uMhz %s\n",
					data->gfx_min_freq_limit/100,
					i == 0 ? "*" : "");
		size += snprintf(buf + size, PAGE_SIZE - size, "1: %uMhz %s\n",
					i == 1 ? now : SMU10_UMD_PSTATE_GFXCLK,
					i == 1 ? "*" : "");
		size += snprintf(buf + size, PAGE_SIZE - size, "2: %uMhz %s\n",
					data->gfx_max_freq_limit/100,
					i == 2 ? "*" : "");
		break;
	case PP_MCLK:
		ret = smum_send_msg_to_smc(hwmgr, PPSMC_MSG_GetFclkFrequency, &now);
		if (ret)
			return ret;

		for (i = 0; i < mclk_table->count; i++)
			size += snprintf(buf + size, PAGE_SIZE - size, "%d: %uMhz %s\n",
					i,
					mclk_table->entries[i].clk / 100,
					((mclk_table->entries[i].clk / 100)
					 == now) ? "*" : "");
		break;
	case OD_SCLK:
		if (hwmgr->od_enabled) {
			ret = smum_send_msg_to_smc(hwmgr, PPSMC_MSG_GetMinGfxclkFrequency, &min_freq);
			if (ret)
				return ret;
			ret = smum_send_msg_to_smc(hwmgr, PPSMC_MSG_GetMaxGfxclkFrequency, &max_freq);
			if (ret)
				return ret;

			size += snprintf(buf + size, PAGE_SIZE - size, "%s:\n", "OD_SCLK");
			size += snprintf(buf + size, PAGE_SIZE - size, "0: %10uMhz\n",
			(data->gfx_actual_soft_min_freq > 0) ? data->gfx_actual_soft_min_freq : min_freq);
			size += snprintf(buf + size, PAGE_SIZE - size, "1: %10uMhz\n",
			(data->gfx_actual_soft_max_freq > 0) ? data->gfx_actual_soft_max_freq : max_freq);
		}
		break;
	case OD_RANGE:
		if (hwmgr->od_enabled) {
			ret = smum_send_msg_to_smc(hwmgr, PPSMC_MSG_GetMinGfxclkFrequency, &min_freq);
			if (ret)
				return ret;
			ret = smum_send_msg_to_smc(hwmgr, PPSMC_MSG_GetMaxGfxclkFrequency, &max_freq);
			if (ret)
				return ret;

			size += snprintf(buf + size, PAGE_SIZE - size, "%s:\n", "OD_RANGE");
			size += snprintf(buf + size, PAGE_SIZE - size, "SCLK: %7uMHz %10uMHz\n",
				min_freq, max_freq);
		}
		break;
	default:
		break;
	}

	return size;
}

static int smu10_get_performance_level(struct pp_hwmgr *hwmgr, const struct pp_hw_power_state *state,
				PHM_PerformanceLevelDesignation designation, uint32_t index,
				PHM_PerformanceLevel *level)
{
	struct smu10_hwmgr *data;

	if (level == NULL || hwmgr == NULL || state == NULL)
		return -EINVAL;

	data = (struct smu10_hwmgr *)(hwmgr->backend);

	if (index == 0) {
		level->memory_clock = data->clock_vol_info.vdd_dep_on_fclk->entries[0].clk;
		level->coreClock = data->gfx_min_freq_limit;
	} else {
		level->memory_clock = data->clock_vol_info.vdd_dep_on_fclk->entries[
			data->clock_vol_info.vdd_dep_on_fclk->count - 1].clk;
		level->coreClock = data->gfx_max_freq_limit;
	}

	level->nonLocalMemoryFreq = 0;
	level->nonLocalMemoryWidth = 0;

	return 0;
}

static int smu10_get_current_shallow_sleep_clocks(struct pp_hwmgr *hwmgr,
	const struct pp_hw_power_state *state, struct pp_clock_info *clock_info)
{
	const struct smu10_power_state *ps = cast_const_smu10_ps(state);

	clock_info->min_eng_clk = ps->levels[0].engine_clock / (1 << (ps->levels[0].ss_divider_index));
	clock_info->max_eng_clk = ps->levels[ps->level - 1].engine_clock / (1 << (ps->levels[ps->level - 1].ss_divider_index));

	return 0;
}

#define MEM_FREQ_LOW_LATENCY        25000
#define MEM_FREQ_HIGH_LATENCY       80000
#define MEM_LATENCY_HIGH            245
#define MEM_LATENCY_LOW             35
#define MEM_LATENCY_ERR             0xFFFF


static uint32_t smu10_get_mem_latency(struct pp_hwmgr *hwmgr,
		uint32_t clock)
{
	if (clock >= MEM_FREQ_LOW_LATENCY &&
			clock < MEM_FREQ_HIGH_LATENCY)
		return MEM_LATENCY_HIGH;
	else if (clock >= MEM_FREQ_HIGH_LATENCY)
		return MEM_LATENCY_LOW;
	else
		return MEM_LATENCY_ERR;
}

static int smu10_get_clock_by_type_with_latency(struct pp_hwmgr *hwmgr,
		enum amd_pp_clock_type type,
		struct pp_clock_levels_with_latency *clocks)
{
	uint32_t i;
	struct smu10_hwmgr *smu10_data = (struct smu10_hwmgr *)(hwmgr->backend);
	struct smu10_clock_voltage_information *pinfo = &(smu10_data->clock_vol_info);
	struct smu10_voltage_dependency_table *pclk_vol_table;
	bool latency_required = false;

	if (pinfo == NULL)
		return -EINVAL;

	switch (type) {
	case amd_pp_mem_clock:
		pclk_vol_table = pinfo->vdd_dep_on_mclk;
		latency_required = true;
		break;
	case amd_pp_f_clock:
		pclk_vol_table = pinfo->vdd_dep_on_fclk;
		latency_required = true;
		break;
	case amd_pp_dcf_clock:
		pclk_vol_table = pinfo->vdd_dep_on_dcefclk;
		break;
	case amd_pp_disp_clock:
		pclk_vol_table = pinfo->vdd_dep_on_dispclk;
		break;
	case amd_pp_phy_clock:
		pclk_vol_table = pinfo->vdd_dep_on_phyclk;
		break;
	case amd_pp_dpp_clock:
		pclk_vol_table = pinfo->vdd_dep_on_dppclk;
		break;
	default:
		return -EINVAL;
	}

	if (pclk_vol_table == NULL || pclk_vol_table->count == 0)
		return -EINVAL;

	clocks->num_levels = 0;
	for (i = 0; i < pclk_vol_table->count; i++) {
		if (pclk_vol_table->entries[i].clk) {
			clocks->data[clocks->num_levels].clocks_in_khz =
				pclk_vol_table->entries[i].clk * 10;
			clocks->data[clocks->num_levels].latency_in_us = latency_required ?
				smu10_get_mem_latency(hwmgr,
						      pclk_vol_table->entries[i].clk) :
				0;
			clocks->num_levels++;
		}
	}

	return 0;
}

static int smu10_get_clock_by_type_with_voltage(struct pp_hwmgr *hwmgr,
		enum amd_pp_clock_type type,
		struct pp_clock_levels_with_voltage *clocks)
{
	uint32_t i;
	struct smu10_hwmgr *smu10_data = (struct smu10_hwmgr *)(hwmgr->backend);
	struct smu10_clock_voltage_information *pinfo = &(smu10_data->clock_vol_info);
	struct smu10_voltage_dependency_table *pclk_vol_table = NULL;

	if (pinfo == NULL)
		return -EINVAL;

	switch (type) {
	case amd_pp_mem_clock:
		pclk_vol_table = pinfo->vdd_dep_on_mclk;
		break;
	case amd_pp_f_clock:
		pclk_vol_table = pinfo->vdd_dep_on_fclk;
		break;
	case amd_pp_dcf_clock:
		pclk_vol_table = pinfo->vdd_dep_on_dcefclk;
		break;
	case amd_pp_soc_clock:
		pclk_vol_table = pinfo->vdd_dep_on_socclk;
		break;
	case amd_pp_disp_clock:
		pclk_vol_table = pinfo->vdd_dep_on_dispclk;
		break;
	case amd_pp_phy_clock:
		pclk_vol_table = pinfo->vdd_dep_on_phyclk;
		break;
	default:
		return -EINVAL;
	}

	if (pclk_vol_table == NULL || pclk_vol_table->count == 0)
		return -EINVAL;

	clocks->num_levels = 0;
	for (i = 0; i < pclk_vol_table->count; i++) {
		if (pclk_vol_table->entries[i].clk) {
			clocks->data[clocks->num_levels].clocks_in_khz = pclk_vol_table->entries[i].clk  * 10;
			clocks->data[clocks->num_levels].voltage_in_mv = pclk_vol_table->entries[i].vol;
			clocks->num_levels++;
		}
	}

	return 0;
}



static int smu10_get_max_high_clocks(struct pp_hwmgr *hwmgr, struct amd_pp_simple_clock_info *clocks)
{
	clocks->engine_max_clock = 80000; /* driver can't get engine clock, temp hard code to 800MHz */
	return 0;
}

static int smu10_thermal_get_temperature(struct pp_hwmgr *hwmgr)
{
	struct amdgpu_device *adev = hwmgr->adev;
	uint32_t reg_value = RREG32_SOC15(THM, 0, mmTHM_TCON_CUR_TMP);
	int cur_temp =
		(reg_value & THM_TCON_CUR_TMP__CUR_TEMP_MASK) >> THM_TCON_CUR_TMP__CUR_TEMP__SHIFT;

	if (cur_temp & THM_TCON_CUR_TMP__CUR_TEMP_RANGE_SEL_MASK)
		cur_temp = ((cur_temp / 8) - 49) * PP_TEMPERATURE_UNITS_PER_CENTIGRADES;
	else
		cur_temp = (cur_temp / 8) * PP_TEMPERATURE_UNITS_PER_CENTIGRADES;

	return cur_temp;
}

static int smu10_read_sensor(struct pp_hwmgr *hwmgr, int idx,
			  void *value, int *size)
{
	struct smu10_hwmgr *smu10_data = (struct smu10_hwmgr *)(hwmgr->backend);
	struct amdgpu_device *adev = hwmgr->adev;
	uint32_t sclk, mclk, activity_percent;
	bool has_gfx_busy;
	int ret = 0;

	/* GetGfxBusy support was added on RV SMU FW 30.85.00 and PCO 4.30.59 */
	if ((adev->apu_flags & AMD_APU_IS_PICASSO) &&
	    (hwmgr->smu_version >= 0x41e3b))
		has_gfx_busy = true;
	else if ((adev->apu_flags & AMD_APU_IS_RAVEN) &&
		 (hwmgr->smu_version >= 0x1e5500))
		has_gfx_busy = true;
	else
		has_gfx_busy = false;

	switch (idx) {
	case AMDGPU_PP_SENSOR_GFX_SCLK:
		ret = smum_send_msg_to_smc(hwmgr, PPSMC_MSG_GetGfxclkFrequency, &sclk);
		if (ret)
			break;
			/* in units of 10KHZ */
		*((uint32_t *)value) = sclk * 100;
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_GFX_MCLK:
		ret = smum_send_msg_to_smc(hwmgr, PPSMC_MSG_GetFclkFrequency, &mclk);
		if (ret)
			break;
			/* in units of 10KHZ */
		*((uint32_t *)value) = mclk * 100;
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_GPU_TEMP:
		*((uint32_t *)value) = smu10_thermal_get_temperature(hwmgr);
		break;
	case AMDGPU_PP_SENSOR_VCN_POWER_STATE:
		*(uint32_t *)value =  smu10_data->vcn_power_gated ? 0 : 1;
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_GPU_LOAD:
		if (!has_gfx_busy)
			ret = -EOPNOTSUPP;
		else {
			ret = smum_send_msg_to_smc(hwmgr,
						   PPSMC_MSG_GetGfxBusy,
						   &activity_percent);
			if (!ret)
				*((uint32_t *)value) = min(activity_percent, (u32)100);
			else
				ret = -EIO;
		}
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	return ret;
}

static int smu10_set_watermarks_for_clocks_ranges(struct pp_hwmgr *hwmgr,
		void *clock_ranges)
{
	struct smu10_hwmgr *data = hwmgr->backend;
	struct dm_pp_wm_sets_with_clock_ranges_soc15 *wm_with_clock_ranges = clock_ranges;
	Watermarks_t *table = &(data->water_marks_table);
	struct amdgpu_device *adev = hwmgr->adev;
	int i;

	smu_set_watermarks_for_clocks_ranges(table, wm_with_clock_ranges);

	if (adev->apu_flags & AMD_APU_IS_RAVEN2) {
		for (i = 0; i < NUM_WM_RANGES; i++)
			table->WatermarkRow[WM_DCFCLK][i].WmType = (uint8_t)0;

		for (i = 0; i < NUM_WM_RANGES; i++)
			table->WatermarkRow[WM_SOCCLK][i].WmType = (uint8_t)0;
	}

	smum_smc_table_manager(hwmgr, (uint8_t *)table, (uint16_t)SMU10_WMTABLE, false);
	data->water_marks_exist = true;
	return 0;
}

static int smu10_smus_notify_pwe(struct pp_hwmgr *hwmgr)
{

	return smum_send_msg_to_smc(hwmgr, PPSMC_MSG_SetRccPfcPmeRestoreRegister, NULL);
}

static int smu10_powergate_mmhub(struct pp_hwmgr *hwmgr)
{
	return smum_send_msg_to_smc(hwmgr, PPSMC_MSG_PowerGateMmHub, NULL);
}

static int smu10_powergate_sdma(struct pp_hwmgr *hwmgr, bool gate)
{
	if (gate)
		return smum_send_msg_to_smc(hwmgr, PPSMC_MSG_PowerDownSdma, NULL);
	else
		return smum_send_msg_to_smc(hwmgr, PPSMC_MSG_PowerUpSdma, NULL);
}

static void smu10_powergate_vcn(struct pp_hwmgr *hwmgr, bool bgate)
{
	struct smu10_hwmgr *smu10_data = (struct smu10_hwmgr *)(hwmgr->backend);

	if (bgate) {
		amdgpu_device_ip_set_powergating_state(hwmgr->adev,
						AMD_IP_BLOCK_TYPE_VCN,
						AMD_PG_STATE_GATE);
		smum_send_msg_to_smc_with_parameter(hwmgr,
					PPSMC_MSG_PowerDownVcn, 0, NULL);
		smu10_data->vcn_power_gated = true;
	} else {
		smum_send_msg_to_smc_with_parameter(hwmgr,
						PPSMC_MSG_PowerUpVcn, 0, NULL);
		amdgpu_device_ip_set_powergating_state(hwmgr->adev,
						AMD_IP_BLOCK_TYPE_VCN,
						AMD_PG_STATE_UNGATE);
		smu10_data->vcn_power_gated = false;
	}
}

static int conv_power_profile_to_pplib_workload(int power_profile)
{
	int pplib_workload = 0;

	switch (power_profile) {
	case PP_SMC_POWER_PROFILE_FULLSCREEN3D:
		pplib_workload = WORKLOAD_PPLIB_FULL_SCREEN_3D_BIT;
		break;
	case PP_SMC_POWER_PROFILE_VIDEO:
		pplib_workload = WORKLOAD_PPLIB_VIDEO_BIT;
		break;
	case PP_SMC_POWER_PROFILE_VR:
		pplib_workload = WORKLOAD_PPLIB_VR_BIT;
		break;
	case PP_SMC_POWER_PROFILE_COMPUTE:
		pplib_workload = WORKLOAD_PPLIB_COMPUTE_BIT;
		break;
	case PP_SMC_POWER_PROFILE_CUSTOM:
		pplib_workload = WORKLOAD_PPLIB_CUSTOM_BIT;
		break;
	}

	return pplib_workload;
}

static int smu10_get_power_profile_mode(struct pp_hwmgr *hwmgr, char *buf)
{
	uint32_t i, size = 0;
	static const uint8_t
		profile_mode_setting[6][4] = {{70, 60, 0, 0,},
						{70, 60, 1, 3,},
						{90, 60, 0, 0,},
						{70, 60, 0, 0,},
						{70, 90, 0, 0,},
						{30, 60, 0, 6,},
						};
	static const char *title[6] = {"NUM",
			"MODE_NAME",
			"BUSY_SET_POINT",
			"FPS",
			"USE_RLC_BUSY",
			"MIN_ACTIVE_LEVEL"};

	if (!buf)
		return -EINVAL;

	phm_get_sysfs_buf(&buf, &size);

	size += sysfs_emit_at(buf, size, "%s %16s %s %s %s %s\n", title[0],
			title[1], title[2], title[3], title[4], title[5]);

	for (i = 0; i <= PP_SMC_POWER_PROFILE_COMPUTE; i++)
		size += sysfs_emit_at(buf, size, "%3d %14s%s: %14d %3d %10d %14d\n",
			i, amdgpu_pp_profile_name[i], (i == hwmgr->power_profile_mode) ? "*" : " ",
			profile_mode_setting[i][0], profile_mode_setting[i][1],
			profile_mode_setting[i][2], profile_mode_setting[i][3]);

	return size;
}

static bool smu10_is_raven1_refresh(struct pp_hwmgr *hwmgr)
{
	struct amdgpu_device *adev = hwmgr->adev;
	if ((adev->apu_flags & AMD_APU_IS_RAVEN) &&
	    (hwmgr->smu_version >= 0x41e2b))
		return true;
	else
		return false;
}

static int smu10_set_power_profile_mode(struct pp_hwmgr *hwmgr, long *input, uint32_t size)
{
	int workload_type = 0;
	int result = 0;

	if (input[size] > PP_SMC_POWER_PROFILE_COMPUTE) {
		pr_err("Invalid power profile mode %ld\n", input[size]);
		return -EINVAL;
	}
	if (hwmgr->power_profile_mode == input[size])
		return 0;

	/* conv PP_SMC_POWER_PROFILE* to WORKLOAD_PPLIB_*_BIT */
	workload_type =
		conv_power_profile_to_pplib_workload(input[size]);
	if (workload_type &&
	    smu10_is_raven1_refresh(hwmgr) &&
	    !hwmgr->gfxoff_state_changed_by_workload) {
		smu10_gfx_off_control(hwmgr, false);
		hwmgr->gfxoff_state_changed_by_workload = true;
	}
	result = smum_send_msg_to_smc_with_parameter(hwmgr, PPSMC_MSG_ActiveProcessNotify,
						1 << workload_type,
						NULL);
	if (!result)
		hwmgr->power_profile_mode = input[size];
	if (workload_type && hwmgr->gfxoff_state_changed_by_workload) {
		smu10_gfx_off_control(hwmgr, true);
		hwmgr->gfxoff_state_changed_by_workload = false;
	}

	return 0;
}

static int smu10_asic_reset(struct pp_hwmgr *hwmgr, enum SMU_ASIC_RESET_MODE mode)
{
	return smum_send_msg_to_smc_with_parameter(hwmgr,
						   PPSMC_MSG_DeviceDriverReset,
						   mode,
						   NULL);
}

static int smu10_set_fine_grain_clk_vol(struct pp_hwmgr *hwmgr,
					enum PP_OD_DPM_TABLE_COMMAND type,
					long *input, uint32_t size)
{
	uint32_t min_freq, max_freq = 0;
	struct smu10_hwmgr *smu10_data = (struct smu10_hwmgr *)(hwmgr->backend);
	int ret = 0;

	if (!hwmgr->od_enabled) {
		pr_err("Fine grain not support\n");
		return -EINVAL;
	}

	if (!smu10_data->fine_grain_enabled) {
		pr_err("pp_od_clk_voltage is not accessible if power_dpm_force_performance_level is not in manual mode!\n");
		return -EINVAL;
	}

	if (type == PP_OD_EDIT_SCLK_VDDC_TABLE) {
		if (size != 2) {
			pr_err("Input parameter number not correct\n");
			return -EINVAL;
		}

		if (input[0] == 0) {
			ret = smum_send_msg_to_smc(hwmgr, PPSMC_MSG_GetMinGfxclkFrequency, &min_freq);
			if (ret)
				return ret;

			if (input[1] < min_freq) {
				pr_err("Fine grain setting minimum sclk (%ld) MHz is less than the minimum allowed (%d) MHz\n",
					input[1], min_freq);
				return -EINVAL;
			}
			smu10_data->gfx_actual_soft_min_freq = input[1];
		} else if (input[0] == 1) {
			ret = smum_send_msg_to_smc(hwmgr, PPSMC_MSG_GetMaxGfxclkFrequency, &max_freq);
			if (ret)
				return ret;

			if (input[1] > max_freq) {
				pr_err("Fine grain setting maximum sclk (%ld) MHz is greater than the maximum allowed (%d) MHz\n",
					input[1], max_freq);
				return -EINVAL;
			}
			smu10_data->gfx_actual_soft_max_freq = input[1];
		} else {
			return -EINVAL;
		}
	} else if (type == PP_OD_RESTORE_DEFAULT_TABLE) {
		if (size != 0) {
			pr_err("Input parameter number not correct\n");
			return -EINVAL;
		}
		ret = smum_send_msg_to_smc(hwmgr, PPSMC_MSG_GetMinGfxclkFrequency, &min_freq);
		if (ret)
			return ret;
		smu10_data->gfx_actual_soft_min_freq = min_freq;

		ret = smum_send_msg_to_smc(hwmgr, PPSMC_MSG_GetMaxGfxclkFrequency, &max_freq);
		if (ret)
			return ret;

		smu10_data->gfx_actual_soft_max_freq = max_freq;
	} else if (type == PP_OD_COMMIT_DPM_TABLE) {
		if (size != 0) {
			pr_err("Input parameter number not correct\n");
			return -EINVAL;
		}

		if (smu10_data->gfx_actual_soft_min_freq > smu10_data->gfx_actual_soft_max_freq) {
			pr_err("The setting minimum sclk (%d) MHz is greater than the setting maximum sclk (%d) MHz\n",
					smu10_data->gfx_actual_soft_min_freq, smu10_data->gfx_actual_soft_max_freq);
			return -EINVAL;
		}

		ret = smum_send_msg_to_smc_with_parameter(hwmgr,
					PPSMC_MSG_SetHardMinGfxClk,
					smu10_data->gfx_actual_soft_min_freq,
					NULL);
		if (ret)
			return ret;

		ret = smum_send_msg_to_smc_with_parameter(hwmgr,
					PPSMC_MSG_SetSoftMaxGfxClk,
					smu10_data->gfx_actual_soft_max_freq,
					NULL);
		if (ret)
			return ret;
	} else {
		return -EINVAL;
	}

	return 0;
}

static int smu10_gfx_state_change(struct pp_hwmgr *hwmgr, uint32_t state)
{
	smum_send_msg_to_smc_with_parameter(hwmgr, PPSMC_MSG_GpuChangeState, state, NULL);

	return 0;
}

static const struct pp_hwmgr_func smu10_hwmgr_funcs = {
	.backend_init = smu10_hwmgr_backend_init,
	.backend_fini = smu10_hwmgr_backend_fini,
	.apply_state_adjust_rules = smu10_apply_state_adjust_rules,
	.force_dpm_level = smu10_dpm_force_dpm_level,
	.get_power_state_size = smu10_get_power_state_size,
	.powerdown_uvd = NULL,
	.powergate_uvd = smu10_powergate_vcn,
	.powergate_vce = NULL,
	.get_mclk = smu10_dpm_get_mclk,
	.get_sclk = smu10_dpm_get_sclk,
	.patch_boot_state = smu10_dpm_patch_boot_state,
	.get_pp_table_entry = smu10_dpm_get_pp_table_entry,
	.get_num_of_pp_table_entries = smu10_dpm_get_num_of_pp_table_entries,
	.set_cpu_power_state = smu10_set_cpu_power_state,
	.store_cc6_data = smu10_store_cc6_data,
	.force_clock_level = smu10_force_clock_level,
	.print_clock_levels = smu10_print_clock_levels,
	.get_dal_power_level = smu10_get_dal_power_level,
	.get_performance_level = smu10_get_performance_level,
	.get_current_shallow_sleep_clocks = smu10_get_current_shallow_sleep_clocks,
	.get_clock_by_type_with_latency = smu10_get_clock_by_type_with_latency,
	.get_clock_by_type_with_voltage = smu10_get_clock_by_type_with_voltage,
	.set_watermarks_for_clocks_ranges = smu10_set_watermarks_for_clocks_ranges,
	.get_max_high_clocks = smu10_get_max_high_clocks,
	.read_sensor = smu10_read_sensor,
	.set_active_display_count = smu10_set_active_display_count,
	.set_min_deep_sleep_dcefclk = smu10_set_min_deep_sleep_dcefclk,
	.dynamic_state_management_enable = smu10_enable_dpm_tasks,
	.power_off_asic = smu10_power_off_asic,
	.asic_setup = smu10_setup_asic_task,
	.power_state_set = smu10_set_power_state_tasks,
	.dynamic_state_management_disable = smu10_disable_dpm_tasks,
	.powergate_mmhub = smu10_powergate_mmhub,
	.smus_notify_pwe = smu10_smus_notify_pwe,
	.display_clock_voltage_request = smu10_display_clock_voltage_request,
	.powergate_gfx = smu10_gfx_off_control,
	.powergate_sdma = smu10_powergate_sdma,
	.set_hard_min_dcefclk_by_freq = smu10_set_hard_min_dcefclk_by_freq,
	.set_hard_min_fclk_by_freq = smu10_set_hard_min_fclk_by_freq,
	.set_hard_min_gfxclk_by_freq = smu10_set_hard_min_gfxclk_by_freq,
	.set_soft_max_gfxclk_by_freq = smu10_set_soft_max_gfxclk_by_freq,
	.get_power_profile_mode = smu10_get_power_profile_mode,
	.set_power_profile_mode = smu10_set_power_profile_mode,
	.asic_reset = smu10_asic_reset,
	.set_fine_grain_clk_vol = smu10_set_fine_grain_clk_vol,
	.gfx_state_change = smu10_gfx_state_change,
};

int smu10_init_function_pointers(struct pp_hwmgr *hwmgr)
{
	hwmgr->hwmgr_func = &smu10_hwmgr_funcs;
	hwmgr->pptable_func = &pptable_funcs;
	return 0;
}
