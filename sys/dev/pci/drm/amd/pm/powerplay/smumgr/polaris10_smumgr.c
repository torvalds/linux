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

#include <linux/pci.h>

#include "pp_debug.h"
#include "smumgr.h"
#include "smu74.h"
#include "smu_ucode_xfer_vi.h"
#include "polaris10_smumgr.h"
#include "smu74_discrete.h"
#include "smu/smu_7_1_3_d.h"
#include "smu/smu_7_1_3_sh_mask.h"
#include "gmc/gmc_8_1_d.h"
#include "gmc/gmc_8_1_sh_mask.h"
#include "oss/oss_3_0_d.h"
#include "gca/gfx_8_0_d.h"
#include "bif/bif_5_0_d.h"
#include "bif/bif_5_0_sh_mask.h"
#include "ppatomctrl.h"
#include "cgs_common.h"
#include "smu7_ppsmc.h"
#include "smu7_smumgr.h"

#include "smu7_dyn_defaults.h"

#include "smu7_hwmgr.h"
#include "hardwaremanager.h"
#include "atombios.h"
#include "pppcielanes.h"

#include "dce/dce_10_0_d.h"
#include "dce/dce_10_0_sh_mask.h"

#define POLARIS10_SMC_SIZE 0x20000
#define POWERTUNE_DEFAULT_SET_MAX    1
#define VDDC_VDDCI_DELTA            200
#define MC_CG_ARB_FREQ_F1           0x0b

static const struct polaris10_pt_defaults polaris10_power_tune_data_set_array[POWERTUNE_DEFAULT_SET_MAX] = {
	/* sviLoadLIneEn, SviLoadLineVddC, TDC_VDDC_ThrottleReleaseLimitPerc, TDC_MAWt,
	 * TdcWaterfallCtl, DTEAmbientTempBase, DisplayCac, BAPM_TEMP_GRADIENT */
	{ 1, 0xF, 0xFD, 0x19, 5, 45, 0, 0xB0000,
	{ 0x79, 0x253, 0x25D, 0xAE, 0x72, 0x80, 0x83, 0x86, 0x6F, 0xC8, 0xC9, 0xC9, 0x2F, 0x4D, 0x61},
	{ 0x17C, 0x172, 0x180, 0x1BC, 0x1B3, 0x1BD, 0x206, 0x200, 0x203, 0x25D, 0x25A, 0x255, 0x2C3, 0x2C5, 0x2B4 } },
};

static const sclkFcwRange_t Range_Table[NUM_SCLK_RANGE] = {
			{VCO_2_4, POSTDIV_DIV_BY_16,  75, 160, 112},
			{VCO_3_6, POSTDIV_DIV_BY_16, 112, 224, 160},
			{VCO_2_4, POSTDIV_DIV_BY_8,   75, 160, 112},
			{VCO_3_6, POSTDIV_DIV_BY_8,  112, 224, 160},
			{VCO_2_4, POSTDIV_DIV_BY_4,   75, 160, 112},
			{VCO_3_6, POSTDIV_DIV_BY_4,  112, 216, 160},
			{VCO_2_4, POSTDIV_DIV_BY_2,   75, 160, 108},
			{VCO_3_6, POSTDIV_DIV_BY_2,  112, 216, 160} };

#define PPPOLARIS10_TARGETACTIVITY_DFLT                     50

static const SMU74_Discrete_GraphicsLevel avfs_graphics_level_polaris10[8] = {
	/*  Min      pcie   DeepSleep Activity  CgSpll      CgSpll    CcPwr  CcPwr  Sclk         Enabled      Enabled                       Voltage    Power */
	/* Voltage, DpmLevel, DivId,  Level,  FuncCntl3,  FuncCntl4,  DynRm, DynRm1 Did, Padding,ForActivity, ForThrottle, UpHyst, DownHyst, DownHyst, Throttle */
	{ 0x100ea446, 0x00, 0x03, 0x3200, 0, 0, 0, 0, 0, 0, 0x01, 0x01, 0x0a, 0x00, 0x00, 0x00, { 0x30750000, 0x3000, 0, 0x2600, 0, 0, 0x0004, 0x8f02, 0xffff, 0x2f00, 0x300e, 0x2700 } },
	{ 0x400ea446, 0x01, 0x04, 0x3200, 0, 0, 0, 0, 0, 0, 0x01, 0x01, 0x0a, 0x00, 0x00, 0x00, { 0x409c0000, 0x2000, 0, 0x1e00, 1, 1, 0x0004, 0x8300, 0xffff, 0x1f00, 0xcb5e, 0x1a00 } },
	{ 0x740ea446, 0x01, 0x00, 0x3200, 0, 0, 0, 0, 0, 0, 0x01, 0x01, 0x0a, 0x00, 0x00, 0x00, { 0x50c30000, 0x2800, 0, 0x2000, 1, 1, 0x0004, 0x0c02, 0xffff, 0x2700, 0x6433, 0x2100 } },
	{ 0xa40ea446, 0x01, 0x00, 0x3200, 0, 0, 0, 0, 0, 0, 0x01, 0x01, 0x0a, 0x00, 0x00, 0x00, { 0x60ea0000, 0x3000, 0, 0x2600, 1, 1, 0x0004, 0x8f02, 0xffff, 0x2f00, 0x300e, 0x2700 } },
	{ 0xd80ea446, 0x01, 0x00, 0x3200, 0, 0, 0, 0, 0, 0, 0x01, 0x01, 0x0a, 0x00, 0x00, 0x00, { 0x70110100, 0x3800, 0, 0x2c00, 1, 1, 0x0004, 0x1203, 0xffff, 0x3600, 0xc9e2, 0x2e00 } },
	{ 0x3c0fa446, 0x01, 0x00, 0x3200, 0, 0, 0, 0, 0, 0, 0x01, 0x01, 0x0a, 0x00, 0x00, 0x00, { 0x80380100, 0x2000, 0, 0x1e00, 2, 1, 0x0004, 0x8300, 0xffff, 0x1f00, 0xcb5e, 0x1a00 } },
	{ 0x6c0fa446, 0x01, 0x00, 0x3200, 0, 0, 0, 0, 0, 0, 0x01, 0x01, 0x0a, 0x00, 0x00, 0x00, { 0x905f0100, 0x2400, 0, 0x1e00, 2, 1, 0x0004, 0x8901, 0xffff, 0x2300, 0x314c, 0x1d00 } },
	{ 0xa00fa446, 0x01, 0x00, 0x3200, 0, 0, 0, 0, 0, 0, 0x01, 0x01, 0x0a, 0x00, 0x00, 0x00, { 0xa0860100, 0x2800, 0, 0x2000, 2, 1, 0x0004, 0x0c02, 0xffff, 0x2700, 0x6433, 0x2100 } }
};

static const SMU74_Discrete_MemoryLevel avfs_memory_level_polaris10 = {
	0x100ea446, 0, 0x30750000, 0x01, 0x01, 0x01, 0x00, 0x00, 0x64, 0x00, 0x00, 0x1f00, 0x00, 0x00};

static int polaris10_perform_btc(struct pp_hwmgr *hwmgr)
{
	int result = 0;
	struct smu7_smumgr *smu_data = (struct smu7_smumgr *)(hwmgr->smu_backend);

	if (0 != smu_data->avfs_btc_param) {
		if (0 != smum_send_msg_to_smc_with_parameter(hwmgr, PPSMC_MSG_PerformBtc, smu_data->avfs_btc_param,
					NULL)) {
			pr_info("[AVFS][SmuPolaris10_PerformBtc] PerformBTC SMU msg failed");
			result = -1;
		}
	}
	if (smu_data->avfs_btc_param > 1) {
		/* Soft-Reset to reset the engine before loading uCode */
		/* halt */
		cgs_write_register(hwmgr->device, mmCP_MEC_CNTL, 0x50000000);
		/* reset everything */
		cgs_write_register(hwmgr->device, mmGRBM_SOFT_RESET, 0xffffffff);
		cgs_write_register(hwmgr->device, mmGRBM_SOFT_RESET, 0);
	}
	return result;
}


static int polaris10_setup_graphics_level_structure(struct pp_hwmgr *hwmgr)
{
	uint32_t vr_config;
	uint32_t dpm_table_start;

	uint16_t u16_boot_mvdd;
	uint32_t graphics_level_address, vr_config_address, graphics_level_size;

	graphics_level_size = sizeof(avfs_graphics_level_polaris10);
	u16_boot_mvdd = PP_HOST_TO_SMC_US(1300 * VOLTAGE_SCALE);

	PP_ASSERT_WITH_CODE(0 == smu7_read_smc_sram_dword(hwmgr,
				SMU7_FIRMWARE_HEADER_LOCATION + offsetof(SMU74_Firmware_Header, DpmTable),
				&dpm_table_start, 0x40000),
			"[AVFS][Polaris10_SetupGfxLvlStruct] SMU could not communicate starting address of DPM table",
			return -1);

	/*  Default value for VRConfig = VR_MERGED_WITH_VDDC + VR_STATIC_VOLTAGE(VDDCI) */
	vr_config = 0x01000500; /* Real value:0x50001 */

	vr_config_address = dpm_table_start + offsetof(SMU74_Discrete_DpmTable, VRConfig);

	PP_ASSERT_WITH_CODE(0 == smu7_copy_bytes_to_smc(hwmgr, vr_config_address,
				(uint8_t *)&vr_config, sizeof(uint32_t), 0x40000),
			"[AVFS][Polaris10_SetupGfxLvlStruct] Problems copying VRConfig value over to SMC",
			return -1);

	graphics_level_address = dpm_table_start + offsetof(SMU74_Discrete_DpmTable, GraphicsLevel);

	PP_ASSERT_WITH_CODE(0 == smu7_copy_bytes_to_smc(hwmgr, graphics_level_address,
				(uint8_t *)(&avfs_graphics_level_polaris10),
				graphics_level_size, 0x40000),
			"[AVFS][Polaris10_SetupGfxLvlStruct] Copying of SCLK DPM table failed!",
			return -1);

	graphics_level_address = dpm_table_start + offsetof(SMU74_Discrete_DpmTable, MemoryLevel);

	PP_ASSERT_WITH_CODE(0 == smu7_copy_bytes_to_smc(hwmgr, graphics_level_address,
				(uint8_t *)(&avfs_memory_level_polaris10), sizeof(avfs_memory_level_polaris10), 0x40000),
				"[AVFS][Polaris10_SetupGfxLvlStruct] Copying of MCLK DPM table failed!",
			return -1);

	/* MVDD Boot value - neccessary for getting rid of the hang that occurs during Mclk DPM enablement */

	graphics_level_address = dpm_table_start + offsetof(SMU74_Discrete_DpmTable, BootMVdd);

	PP_ASSERT_WITH_CODE(0 == smu7_copy_bytes_to_smc(hwmgr, graphics_level_address,
			(uint8_t *)(&u16_boot_mvdd), sizeof(u16_boot_mvdd), 0x40000),
			"[AVFS][Polaris10_SetupGfxLvlStruct] Copying of DPM table failed!",
			return -1);

	return 0;
}


static int polaris10_avfs_event_mgr(struct pp_hwmgr *hwmgr)
{
	struct smu7_smumgr *smu_data = (struct smu7_smumgr *)(hwmgr->smu_backend);

	if (!hwmgr->avfs_supported)
		return 0;

	PP_ASSERT_WITH_CODE(0 == polaris10_setup_graphics_level_structure(hwmgr),
		"[AVFS][Polaris10_AVFSEventMgr] Could not Copy Graphics Level table over to SMU",
		return -EINVAL);

	if (smu_data->avfs_btc_param > 1) {
		pr_info("[AVFS][Polaris10_AVFSEventMgr] AC BTC has not been successfully verified on Fiji. There may be in this setting.");
		PP_ASSERT_WITH_CODE(0 == smu7_setup_pwr_virus(hwmgr),
		"[AVFS][Polaris10_AVFSEventMgr] Could not setup Pwr Virus for AVFS ",
		return -EINVAL);
	}

	PP_ASSERT_WITH_CODE(0 == polaris10_perform_btc(hwmgr),
				"[AVFS][Polaris10_AVFSEventMgr] Failure at SmuPolaris10_PerformBTC. AVFS Disabled",
			 return -EINVAL);

	return 0;
}

static int polaris10_start_smu_in_protection_mode(struct pp_hwmgr *hwmgr)
{
	int result = 0;

	/* Wait for smc boot up */
	/* PHM_WAIT_VFPF_INDIRECT_FIELD_UNEQUAL(smumgr, SMC_IND, RCU_UC_EVENTS, boot_seq_done, 0) */

	/* Assert reset */
	PHM_WRITE_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
					SMC_SYSCON_RESET_CNTL, rst_reg, 1);

	result = smu7_upload_smu_firmware_image(hwmgr);
	if (result != 0)
		return result;

	/* Clear status */
	cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC, ixSMU_STATUS, 0);

	PHM_WRITE_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
					SMC_SYSCON_CLOCK_CNTL_0, ck_disable, 0);

	/* De-assert reset */
	PHM_WRITE_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
					SMC_SYSCON_RESET_CNTL, rst_reg, 0);


	PHM_WAIT_VFPF_INDIRECT_FIELD(hwmgr, SMC_IND, RCU_UC_EVENTS, INTERRUPTS_ENABLED, 1);


	/* Call Test SMU message with 0x20000 offset to trigger SMU start */
	smu7_send_msg_to_smc_offset(hwmgr);

	/* Wait done bit to be set */
	/* Check pass/failed indicator */

	PHM_WAIT_VFPF_INDIRECT_FIELD_UNEQUAL(hwmgr, SMC_IND, SMU_STATUS, SMU_DONE, 0);

	if (1 != PHM_READ_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
						SMU_STATUS, SMU_PASS))
		PP_ASSERT_WITH_CODE(false, "SMU Firmware start failed!", return -1);

	cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC, ixFIRMWARE_FLAGS, 0);

	PHM_WRITE_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
					SMC_SYSCON_RESET_CNTL, rst_reg, 1);

	PHM_WRITE_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
					SMC_SYSCON_RESET_CNTL, rst_reg, 0);

	/* Wait for firmware to initialize */
	PHM_WAIT_VFPF_INDIRECT_FIELD(hwmgr, SMC_IND, FIRMWARE_FLAGS, INTERRUPTS_ENABLED, 1);

	return result;
}

static int polaris10_start_smu_in_non_protection_mode(struct pp_hwmgr *hwmgr)
{
	int result = 0;

	/* wait for smc boot up */
	PHM_WAIT_VFPF_INDIRECT_FIELD_UNEQUAL(hwmgr, SMC_IND, RCU_UC_EVENTS, boot_seq_done, 0);

	/* Clear firmware interrupt enable flag */
	/* PHM_WRITE_VFPF_INDIRECT_FIELD(pSmuMgr, SMC_IND, SMC_SYSCON_MISC_CNTL, pre_fetcher_en, 1); */
	cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
				ixFIRMWARE_FLAGS, 0);

	PHM_WRITE_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
					SMC_SYSCON_RESET_CNTL,
					rst_reg, 1);

	result = smu7_upload_smu_firmware_image(hwmgr);
	if (result != 0)
		return result;

	/* Set smc instruct start point at 0x0 */
	smu7_program_jump_on_start(hwmgr);

	PHM_WRITE_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
					SMC_SYSCON_CLOCK_CNTL_0, ck_disable, 0);

	PHM_WRITE_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
					SMC_SYSCON_RESET_CNTL, rst_reg, 0);

	/* Wait for firmware to initialize */

	PHM_WAIT_VFPF_INDIRECT_FIELD(hwmgr, SMC_IND,
					FIRMWARE_FLAGS, INTERRUPTS_ENABLED, 1);

	return result;
}

static int polaris10_start_smu(struct pp_hwmgr *hwmgr)
{
	int result = 0;
	struct polaris10_smumgr *smu_data = (struct polaris10_smumgr *)(hwmgr->smu_backend);

	/* Only start SMC if SMC RAM is not running */
	if (!smu7_is_smc_ram_running(hwmgr) && hwmgr->not_vf) {
		smu_data->protected_mode = (uint8_t) (PHM_READ_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC, SMU_FIRMWARE, SMU_MODE));
		smu_data->smu7_data.security_hard_key = (uint8_t) (PHM_READ_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC, SMU_FIRMWARE, SMU_SEL));

		/* Check if SMU is running in protected mode */
		if (smu_data->protected_mode == 0)
			result = polaris10_start_smu_in_non_protection_mode(hwmgr);
		else
			result = polaris10_start_smu_in_protection_mode(hwmgr);

		if (result != 0)
			PP_ASSERT_WITH_CODE(0, "Failed to load SMU ucode.", return result);

		polaris10_avfs_event_mgr(hwmgr);
	}

	/* Setup SoftRegsStart here for register lookup in case DummyBackEnd is used and ProcessFirmwareHeader is not executed */
	smu7_read_smc_sram_dword(hwmgr, SMU7_FIRMWARE_HEADER_LOCATION + offsetof(SMU74_Firmware_Header, SoftRegisters),
					&(smu_data->smu7_data.soft_regs_start), 0x40000);

	result = smu7_request_smu_load_fw(hwmgr);

	return result;
}

static bool polaris10_is_hw_avfs_present(struct pp_hwmgr *hwmgr)
{
	uint32_t efuse;

	efuse = cgs_read_ind_register(hwmgr->device, CGS_IND_REG__SMC, ixSMU_EFUSE_0 + (49*4));
	efuse &= 0x00000001;
	if (efuse)
		return true;

	return false;
}

static int polaris10_smu_init(struct pp_hwmgr *hwmgr)
{
	struct polaris10_smumgr *smu_data;

	smu_data = kzalloc(sizeof(struct polaris10_smumgr), GFP_KERNEL);
	if (smu_data == NULL)
		return -ENOMEM;

	hwmgr->smu_backend = smu_data;

	if (smu7_init(hwmgr)) {
		kfree(smu_data);
		return -EINVAL;
	}

	return 0;
}

static int polaris10_get_dependency_volt_by_clk(struct pp_hwmgr *hwmgr,
		struct phm_ppt_v1_clock_voltage_dependency_table *dep_table,
		uint32_t clock, SMU_VoltageLevel *voltage, uint32_t *mvdd)
{
	uint32_t i;
	uint16_t vddci;
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);

	*voltage = *mvdd = 0;

	/* clock - voltage dependency table is empty table */
	if (dep_table->count == 0)
		return -EINVAL;

	for (i = 0; i < dep_table->count; i++) {
		/* find first sclk bigger than request */
		if (dep_table->entries[i].clk >= clock) {
			*voltage |= (dep_table->entries[i].vddc *
					VOLTAGE_SCALE) << VDDC_SHIFT;
			if (SMU7_VOLTAGE_CONTROL_NONE == data->vddci_control)
				*voltage |= (data->vbios_boot_state.vddci_bootup_value *
						VOLTAGE_SCALE) << VDDCI_SHIFT;
			else if (dep_table->entries[i].vddci)
				*voltage |= (dep_table->entries[i].vddci *
						VOLTAGE_SCALE) << VDDCI_SHIFT;
			else {
				vddci = phm_find_closest_vddci(&(data->vddci_voltage_table),
						(dep_table->entries[i].vddc -
								(uint16_t)VDDC_VDDCI_DELTA));
				*voltage |= (vddci * VOLTAGE_SCALE) << VDDCI_SHIFT;
			}

			if (SMU7_VOLTAGE_CONTROL_NONE == data->mvdd_control)
				*mvdd = data->vbios_boot_state.mvdd_bootup_value *
					VOLTAGE_SCALE;
			else if (dep_table->entries[i].mvdd)
				*mvdd = (uint32_t) dep_table->entries[i].mvdd *
					VOLTAGE_SCALE;

			*voltage |= 1 << PHASES_SHIFT;
			return 0;
		}
	}

	/* sclk is bigger than max sclk in the dependence table */
	*voltage |= (dep_table->entries[i - 1].vddc * VOLTAGE_SCALE) << VDDC_SHIFT;

	if (SMU7_VOLTAGE_CONTROL_NONE == data->vddci_control)
		*voltage |= (data->vbios_boot_state.vddci_bootup_value *
				VOLTAGE_SCALE) << VDDCI_SHIFT;
	else if (dep_table->entries[i-1].vddci) {
		*voltage |= (dep_table->entries[i - 1].vddci * VOLTAGE_SCALE) << VDDC_SHIFT;
	} else {
		vddci = phm_find_closest_vddci(&(data->vddci_voltage_table),
				(dep_table->entries[i].vddc -
						(uint16_t)VDDC_VDDCI_DELTA));
		*voltage |= (vddci * VOLTAGE_SCALE) << VDDCI_SHIFT;
	}

	if (SMU7_VOLTAGE_CONTROL_NONE == data->mvdd_control)
		*mvdd = data->vbios_boot_state.mvdd_bootup_value * VOLTAGE_SCALE;
	else if (dep_table->entries[i].mvdd)
		*mvdd = (uint32_t) dep_table->entries[i - 1].mvdd * VOLTAGE_SCALE;

	return 0;
}

static uint16_t scale_fan_gain_settings(uint16_t raw_setting)
{
	uint32_t tmp;
	tmp = raw_setting * 4096 / 100;
	return (uint16_t)tmp;
}

static int polaris10_populate_bapm_parameters_in_dpm_table(struct pp_hwmgr *hwmgr)
{
	struct polaris10_smumgr *smu_data = (struct polaris10_smumgr *)(hwmgr->smu_backend);

	const struct polaris10_pt_defaults *defaults = smu_data->power_tune_defaults;
	SMU74_Discrete_DpmTable  *table = &(smu_data->smc_state_table);
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	struct phm_cac_tdp_table *cac_dtp_table = table_info->cac_dtp_table;
	struct pp_advance_fan_control_parameters *fan_table =
			&hwmgr->thermal_controller.advanceFanControlParameters;
	int i, j, k;
	const uint16_t *pdef1;
	const uint16_t *pdef2;

	table->DefaultTdp = PP_HOST_TO_SMC_US((uint16_t)(cac_dtp_table->usTDP * 128));
	table->TargetTdp  = PP_HOST_TO_SMC_US((uint16_t)(cac_dtp_table->usTDP * 128));

	PP_ASSERT_WITH_CODE(cac_dtp_table->usTargetOperatingTemp <= 255,
				"Target Operating Temp is out of Range!",
				);

	table->TemperatureLimitEdge = PP_HOST_TO_SMC_US(
			cac_dtp_table->usTargetOperatingTemp * 256);
	table->TemperatureLimitHotspot = PP_HOST_TO_SMC_US(
			cac_dtp_table->usTemperatureLimitHotspot * 256);
	table->FanGainEdge = PP_HOST_TO_SMC_US(
			scale_fan_gain_settings(fan_table->usFanGainEdge));
	table->FanGainHotspot = PP_HOST_TO_SMC_US(
			scale_fan_gain_settings(fan_table->usFanGainHotspot));

	pdef1 = defaults->BAPMTI_R;
	pdef2 = defaults->BAPMTI_RC;

	for (i = 0; i < SMU74_DTE_ITERATIONS; i++) {
		for (j = 0; j < SMU74_DTE_SOURCES; j++) {
			for (k = 0; k < SMU74_DTE_SINKS; k++) {
				table->BAPMTI_R[i][j][k] = PP_HOST_TO_SMC_US(*pdef1);
				table->BAPMTI_RC[i][j][k] = PP_HOST_TO_SMC_US(*pdef2);
				pdef1++;
				pdef2++;
			}
		}
	}

	return 0;
}

static void polaris10_populate_zero_rpm_parameters(struct pp_hwmgr *hwmgr)
{
	struct polaris10_smumgr *smu_data = (struct polaris10_smumgr *)(hwmgr->smu_backend);
	SMU74_Discrete_DpmTable  *table = &(smu_data->smc_state_table);
	uint16_t fan_stop_temp =
		((uint16_t)hwmgr->thermal_controller.advanceFanControlParameters.ucFanStopTemperature) << 8;
	uint16_t fan_start_temp =
		((uint16_t)hwmgr->thermal_controller.advanceFanControlParameters.ucFanStartTemperature) << 8;

	if (hwmgr->thermal_controller.advanceFanControlParameters.ucEnableZeroRPM) {
		table->FanStartTemperature = PP_HOST_TO_SMC_US(fan_start_temp);
		table->FanStopTemperature = PP_HOST_TO_SMC_US(fan_stop_temp);
	}
}

static int polaris10_populate_svi_load_line(struct pp_hwmgr *hwmgr)
{
	struct polaris10_smumgr *smu_data = (struct polaris10_smumgr *)(hwmgr->smu_backend);
	const struct polaris10_pt_defaults *defaults = smu_data->power_tune_defaults;

	smu_data->power_tune_table.SviLoadLineEn = defaults->SviLoadLineEn;
	smu_data->power_tune_table.SviLoadLineVddC = defaults->SviLoadLineVddC;
	smu_data->power_tune_table.SviLoadLineTrimVddC = 3;
	smu_data->power_tune_table.SviLoadLineOffsetVddC = 0;

	return 0;
}

static int polaris10_populate_tdc_limit(struct pp_hwmgr *hwmgr)
{
	uint16_t tdc_limit;
	struct polaris10_smumgr *smu_data = (struct polaris10_smumgr *)(hwmgr->smu_backend);
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	const struct polaris10_pt_defaults *defaults = smu_data->power_tune_defaults;

	tdc_limit = (uint16_t)(table_info->cac_dtp_table->usTDC * 128);
	smu_data->power_tune_table.TDC_VDDC_PkgLimit =
			CONVERT_FROM_HOST_TO_SMC_US(tdc_limit);
	smu_data->power_tune_table.TDC_VDDC_ThrottleReleaseLimitPerc =
			defaults->TDC_VDDC_ThrottleReleaseLimitPerc;
	smu_data->power_tune_table.TDC_MAWt = defaults->TDC_MAWt;

	return 0;
}

static int polaris10_populate_dw8(struct pp_hwmgr *hwmgr, uint32_t fuse_table_offset)
{
	struct polaris10_smumgr *smu_data = (struct polaris10_smumgr *)(hwmgr->smu_backend);
	const struct polaris10_pt_defaults *defaults = smu_data->power_tune_defaults;
	uint32_t temp;

	if (smu7_read_smc_sram_dword(hwmgr,
			fuse_table_offset +
			offsetof(SMU74_Discrete_PmFuses, TdcWaterfallCtl),
			(uint32_t *)&temp, SMC_RAM_END))
		PP_ASSERT_WITH_CODE(false,
				"Attempt to read PmFuses.DW6 (SviLoadLineEn) from SMC Failed!",
				return -EINVAL);
	else {
		smu_data->power_tune_table.TdcWaterfallCtl = defaults->TdcWaterfallCtl;
		smu_data->power_tune_table.LPMLTemperatureMin =
				(uint8_t)((temp >> 16) & 0xff);
		smu_data->power_tune_table.LPMLTemperatureMax =
				(uint8_t)((temp >> 8) & 0xff);
		smu_data->power_tune_table.Reserved = (uint8_t)(temp & 0xff);
	}
	return 0;
}

static int polaris10_populate_temperature_scaler(struct pp_hwmgr *hwmgr)
{
	int i;
	struct polaris10_smumgr *smu_data = (struct polaris10_smumgr *)(hwmgr->smu_backend);

	/* Currently not used. Set all to zero. */
	for (i = 0; i < 16; i++)
		smu_data->power_tune_table.LPMLTemperatureScaler[i] = 0;

	return 0;
}

static int polaris10_populate_fuzzy_fan(struct pp_hwmgr *hwmgr)
{
	struct polaris10_smumgr *smu_data = (struct polaris10_smumgr *)(hwmgr->smu_backend);

/* TO DO move to hwmgr */
	if ((hwmgr->thermal_controller.advanceFanControlParameters.usFanOutputSensitivity & (1 << 15))
		|| 0 == hwmgr->thermal_controller.advanceFanControlParameters.usFanOutputSensitivity)
		hwmgr->thermal_controller.advanceFanControlParameters.usFanOutputSensitivity =
			hwmgr->thermal_controller.advanceFanControlParameters.usDefaultFanOutputSensitivity;

	smu_data->power_tune_table.FuzzyFan_PwmSetDelta = PP_HOST_TO_SMC_US(
				hwmgr->thermal_controller.advanceFanControlParameters.usFanOutputSensitivity);
	return 0;
}

static int polaris10_populate_gnb_lpml(struct pp_hwmgr *hwmgr)
{
	int i;
	struct polaris10_smumgr *smu_data = (struct polaris10_smumgr *)(hwmgr->smu_backend);

	/* Currently not used. Set all to zero. */
	for (i = 0; i < 16; i++)
		smu_data->power_tune_table.GnbLPML[i] = 0;

	return 0;
}

static int polaris10_populate_bapm_vddc_base_leakage_sidd(struct pp_hwmgr *hwmgr)
{
	struct polaris10_smumgr *smu_data = (struct polaris10_smumgr *)(hwmgr->smu_backend);
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	uint16_t hi_sidd = smu_data->power_tune_table.BapmVddCBaseLeakageHiSidd;
	uint16_t lo_sidd = smu_data->power_tune_table.BapmVddCBaseLeakageLoSidd;
	struct phm_cac_tdp_table *cac_table = table_info->cac_dtp_table;

	hi_sidd = (uint16_t)(cac_table->usHighCACLeakage / 100 * 256);
	lo_sidd = (uint16_t)(cac_table->usLowCACLeakage / 100 * 256);

	smu_data->power_tune_table.BapmVddCBaseLeakageHiSidd =
			CONVERT_FROM_HOST_TO_SMC_US(hi_sidd);
	smu_data->power_tune_table.BapmVddCBaseLeakageLoSidd =
			CONVERT_FROM_HOST_TO_SMC_US(lo_sidd);

	return 0;
}

static int polaris10_populate_pm_fuses(struct pp_hwmgr *hwmgr)
{
	struct polaris10_smumgr *smu_data = (struct polaris10_smumgr *)(hwmgr->smu_backend);
	uint32_t pm_fuse_table_offset;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_PowerContainment)) {
		if (smu7_read_smc_sram_dword(hwmgr,
				SMU7_FIRMWARE_HEADER_LOCATION +
				offsetof(SMU74_Firmware_Header, PmFuseTable),
				&pm_fuse_table_offset, SMC_RAM_END))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to get pm_fuse_table_offset Failed!",
					return -EINVAL);

		if (polaris10_populate_svi_load_line(hwmgr))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to populate SviLoadLine Failed!",
					return -EINVAL);

		if (polaris10_populate_tdc_limit(hwmgr))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to populate TDCLimit Failed!", return -EINVAL);

		if (polaris10_populate_dw8(hwmgr, pm_fuse_table_offset))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to populate TdcWaterfallCtl, "
					"LPMLTemperature Min and Max Failed!",
					return -EINVAL);

		if (0 != polaris10_populate_temperature_scaler(hwmgr))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to populate LPMLTemperatureScaler Failed!",
					return -EINVAL);

		if (polaris10_populate_fuzzy_fan(hwmgr))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to populate Fuzzy Fan Control parameters Failed!",
					return -EINVAL);

		if (polaris10_populate_gnb_lpml(hwmgr))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to populate GnbLPML Failed!",
					return -EINVAL);

		if (polaris10_populate_bapm_vddc_base_leakage_sidd(hwmgr))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to populate BapmVddCBaseLeakage Hi and Lo "
					"Sidd Failed!", return -EINVAL);

		if (smu7_copy_bytes_to_smc(hwmgr, pm_fuse_table_offset,
				(uint8_t *)&smu_data->power_tune_table,
				(sizeof(struct SMU74_Discrete_PmFuses) - 92), SMC_RAM_END))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to download PmFuseTable Failed!",
					return -EINVAL);
	}
	return 0;
}

static int polaris10_populate_smc_mvdd_table(struct pp_hwmgr *hwmgr,
			SMU74_Discrete_DpmTable *table)
{
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);
	uint32_t count, level;

	if (SMU7_VOLTAGE_CONTROL_BY_GPIO == data->mvdd_control) {
		count = data->mvdd_voltage_table.count;
		if (count > SMU_MAX_SMIO_LEVELS)
			count = SMU_MAX_SMIO_LEVELS;
		for (level = 0; level < count; level++) {
			table->SmioTable2.Pattern[level].Voltage =
				PP_HOST_TO_SMC_US(data->mvdd_voltage_table.entries[level].value * VOLTAGE_SCALE);
			/* Index into DpmTable.Smio. Drive bits from Smio entry to get this voltage level.*/
			table->SmioTable2.Pattern[level].Smio =
				(uint8_t) level;
			table->Smio[level] |=
				data->mvdd_voltage_table.entries[level].smio_low;
		}
		table->SmioMask2 = data->mvdd_voltage_table.mask_low;

		table->MvddLevelCount = (uint32_t) PP_HOST_TO_SMC_UL(count);
	}

	return 0;
}

static int polaris10_populate_smc_vddc_table(struct pp_hwmgr *hwmgr,
					struct SMU74_Discrete_DpmTable *table)
{
	uint32_t count, level;
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);

	count = data->vddc_voltage_table.count;

	if (SMU7_VOLTAGE_CONTROL_BY_GPIO == data->voltage_control) {
		if (count > SMU_MAX_SMIO_LEVELS)
			count = SMU_MAX_SMIO_LEVELS;
		for (level = 0; level < count; ++level) {
			table->SmioTable1.Pattern[level].Voltage =
				PP_HOST_TO_SMC_US(data->vddc_voltage_table.entries[level].value * VOLTAGE_SCALE);
			table->SmioTable1.Pattern[level].Smio = (uint8_t) level;

			table->Smio[level] |= data->vddc_voltage_table.entries[level].smio_low;
		}

		table->SmioMask1 = data->vddc_voltage_table.mask_low;
	}

	return 0;
}

static int polaris10_populate_smc_vddci_table(struct pp_hwmgr *hwmgr,
					struct SMU74_Discrete_DpmTable *table)
{
	uint32_t count, level;
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);

	count = data->vddci_voltage_table.count;

	if (SMU7_VOLTAGE_CONTROL_BY_GPIO == data->vddci_control) {
		if (count > SMU_MAX_SMIO_LEVELS)
			count = SMU_MAX_SMIO_LEVELS;
		for (level = 0; level < count; ++level) {
			table->SmioTable1.Pattern[level].Voltage =
				PP_HOST_TO_SMC_US(data->vddci_voltage_table.entries[level].value * VOLTAGE_SCALE);
			table->SmioTable1.Pattern[level].Smio = (uint8_t) level;

			table->Smio[level] |= data->vddci_voltage_table.entries[level].smio_low;
		}

		table->SmioMask1 = data->vddci_voltage_table.mask_low;
	}

	return 0;
}

static int polaris10_populate_cac_table(struct pp_hwmgr *hwmgr,
		struct SMU74_Discrete_DpmTable *table)
{
	uint32_t count;
	uint8_t index;
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	struct phm_ppt_v1_voltage_lookup_table *lookup_table =
			table_info->vddc_lookup_table;
	/* tables is already swapped, so in order to use the value from it,
	 * we need to swap it back.
	 * We are populating vddc CAC data to BapmVddc table
	 * in split and merged mode
	 */
	for (count = 0; count < lookup_table->count; count++) {
		index = phm_get_voltage_index(lookup_table,
				data->vddc_voltage_table.entries[count].value);
		table->BapmVddcVidLoSidd[count] = convert_to_vid(lookup_table->entries[index].us_cac_low);
		table->BapmVddcVidHiSidd[count] = convert_to_vid(lookup_table->entries[index].us_cac_mid);
		table->BapmVddcVidHiSidd2[count] = convert_to_vid(lookup_table->entries[index].us_cac_high);
	}

	return 0;
}

static int polaris10_populate_smc_voltage_tables(struct pp_hwmgr *hwmgr,
		struct SMU74_Discrete_DpmTable *table)
{
	polaris10_populate_smc_vddc_table(hwmgr, table);
	polaris10_populate_smc_vddci_table(hwmgr, table);
	polaris10_populate_smc_mvdd_table(hwmgr, table);
	polaris10_populate_cac_table(hwmgr, table);

	return 0;
}

static int polaris10_populate_ulv_level(struct pp_hwmgr *hwmgr,
		struct SMU74_Discrete_Ulv *state)
{
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	struct amdgpu_device *adev = hwmgr->adev;

	state->CcPwrDynRm = 0;
	state->CcPwrDynRm1 = 0;

	state->VddcOffset = (uint16_t) table_info->us_ulv_voltage_offset;
	state->VddcOffsetVid = (uint8_t)(table_info->us_ulv_voltage_offset *
			VOLTAGE_VID_OFFSET_SCALE2 / VOLTAGE_VID_OFFSET_SCALE1);

	if ((hwmgr->chip_id == CHIP_POLARIS12) ||
	    ASICID_IS_P20(adev->pdev->device, adev->pdev->revision) ||
	    ASICID_IS_P21(adev->pdev->device, adev->pdev->revision) ||
	    ASICID_IS_P30(adev->pdev->device, adev->pdev->revision) ||
	    ASICID_IS_P31(adev->pdev->device, adev->pdev->revision))
		state->VddcPhase = data->vddc_phase_shed_control ^ 0x3;
	else
		state->VddcPhase = (data->vddc_phase_shed_control) ? 0 : 1;

	CONVERT_FROM_HOST_TO_SMC_UL(state->CcPwrDynRm);
	CONVERT_FROM_HOST_TO_SMC_UL(state->CcPwrDynRm1);
	CONVERT_FROM_HOST_TO_SMC_US(state->VddcOffset);

	return 0;
}

static int polaris10_populate_ulv_state(struct pp_hwmgr *hwmgr,
		struct SMU74_Discrete_DpmTable *table)
{
	return polaris10_populate_ulv_level(hwmgr, &table->Ulv);
}

static int polaris10_populate_smc_link_level(struct pp_hwmgr *hwmgr,
		struct SMU74_Discrete_DpmTable *table)
{
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);
	struct polaris10_smumgr *smu_data = (struct polaris10_smumgr *)(hwmgr->smu_backend);
	struct smu7_dpm_table *dpm_table = &data->dpm_table;
	int i;

	/* Index (dpm_table->pcie_speed_table.count)
	 * is reserved for PCIE boot level. */
	for (i = 0; i <= dpm_table->pcie_speed_table.count; i++) {
		table->LinkLevel[i].PcieGenSpeed  =
				(uint8_t)dpm_table->pcie_speed_table.dpm_levels[i].value;
		table->LinkLevel[i].PcieLaneCount = (uint8_t)encode_pcie_lane_width(
				dpm_table->pcie_speed_table.dpm_levels[i].param1);
		table->LinkLevel[i].EnabledForActivity = 1;
		table->LinkLevel[i].SPC = (uint8_t)(data->pcie_spc_cap & 0xff);
		table->LinkLevel[i].DownThreshold = PP_HOST_TO_SMC_UL(5);
		table->LinkLevel[i].UpThreshold = PP_HOST_TO_SMC_UL(30);
	}

	smu_data->smc_state_table.LinkLevelCount =
			(uint8_t)dpm_table->pcie_speed_table.count;

/* To Do move to hwmgr */
	data->dpm_level_enable_mask.pcie_dpm_enable_mask =
			phm_get_dpm_level_enable_mask_value(&dpm_table->pcie_speed_table);

	return 0;
}


static void polaris10_get_sclk_range_table(struct pp_hwmgr *hwmgr,
				   SMU74_Discrete_DpmTable  *table)
{
	struct polaris10_smumgr *smu_data = (struct polaris10_smumgr *)(hwmgr->smu_backend);
	uint32_t i, ref_clk;

	struct pp_atom_ctrl_sclk_range_table range_table_from_vbios = { { {0} } };

	ref_clk = amdgpu_asic_get_xclk((struct amdgpu_device *)hwmgr->adev);

	if (0 == atomctrl_get_smc_sclk_range_table(hwmgr, &range_table_from_vbios)) {
		for (i = 0; i < NUM_SCLK_RANGE; i++) {
			table->SclkFcwRangeTable[i].vco_setting = range_table_from_vbios.entry[i].ucVco_setting;
			table->SclkFcwRangeTable[i].postdiv = range_table_from_vbios.entry[i].ucPostdiv;
			table->SclkFcwRangeTable[i].fcw_pcc = range_table_from_vbios.entry[i].usFcw_pcc;

			table->SclkFcwRangeTable[i].fcw_trans_upper = range_table_from_vbios.entry[i].usFcw_trans_upper;
			table->SclkFcwRangeTable[i].fcw_trans_lower = range_table_from_vbios.entry[i].usRcw_trans_lower;

			CONVERT_FROM_HOST_TO_SMC_US(table->SclkFcwRangeTable[i].fcw_pcc);
			CONVERT_FROM_HOST_TO_SMC_US(table->SclkFcwRangeTable[i].fcw_trans_upper);
			CONVERT_FROM_HOST_TO_SMC_US(table->SclkFcwRangeTable[i].fcw_trans_lower);
		}
		return;
	}

	for (i = 0; i < NUM_SCLK_RANGE; i++) {
		smu_data->range_table[i].trans_lower_frequency = (ref_clk * Range_Table[i].fcw_trans_lower) >> Range_Table[i].postdiv;
		smu_data->range_table[i].trans_upper_frequency = (ref_clk * Range_Table[i].fcw_trans_upper) >> Range_Table[i].postdiv;

		table->SclkFcwRangeTable[i].vco_setting = Range_Table[i].vco_setting;
		table->SclkFcwRangeTable[i].postdiv = Range_Table[i].postdiv;
		table->SclkFcwRangeTable[i].fcw_pcc = Range_Table[i].fcw_pcc;

		table->SclkFcwRangeTable[i].fcw_trans_upper = Range_Table[i].fcw_trans_upper;
		table->SclkFcwRangeTable[i].fcw_trans_lower = Range_Table[i].fcw_trans_lower;

		CONVERT_FROM_HOST_TO_SMC_US(table->SclkFcwRangeTable[i].fcw_pcc);
		CONVERT_FROM_HOST_TO_SMC_US(table->SclkFcwRangeTable[i].fcw_trans_upper);
		CONVERT_FROM_HOST_TO_SMC_US(table->SclkFcwRangeTable[i].fcw_trans_lower);
	}
}

static int polaris10_calculate_sclk_params(struct pp_hwmgr *hwmgr,
		uint32_t clock, SMU_SclkSetting *sclk_setting)
{
	struct polaris10_smumgr *smu_data = (struct polaris10_smumgr *)(hwmgr->smu_backend);
	const SMU74_Discrete_DpmTable *table = &(smu_data->smc_state_table);
	struct pp_atomctrl_clock_dividers_ai dividers;
	uint32_t ref_clock;
	uint32_t pcc_target_percent, pcc_target_freq, ss_target_percent, ss_target_freq;
	uint8_t i;
	int result;
	uint64_t temp;

	sclk_setting->SclkFrequency = clock;
	/* get the engine clock dividers for this clock value */
	result = atomctrl_get_engine_pll_dividers_ai(hwmgr, clock,  &dividers);
	if (result == 0) {
		sclk_setting->Fcw_int = dividers.usSclk_fcw_int;
		sclk_setting->Fcw_frac = dividers.usSclk_fcw_frac;
		sclk_setting->Pcc_fcw_int = dividers.usPcc_fcw_int;
		sclk_setting->PllRange = dividers.ucSclkPllRange;
		sclk_setting->Sclk_slew_rate = 0x400;
		sclk_setting->Pcc_up_slew_rate = dividers.usPcc_fcw_slew_frac;
		sclk_setting->Pcc_down_slew_rate = 0xffff;
		sclk_setting->SSc_En = dividers.ucSscEnable;
		sclk_setting->Fcw1_int = dividers.usSsc_fcw1_int;
		sclk_setting->Fcw1_frac = dividers.usSsc_fcw1_frac;
		sclk_setting->Sclk_ss_slew_rate = dividers.usSsc_fcw_slew_frac;
		return result;
	}

	ref_clock = amdgpu_asic_get_xclk((struct amdgpu_device *)hwmgr->adev);

	for (i = 0; i < NUM_SCLK_RANGE; i++) {
		if (clock > smu_data->range_table[i].trans_lower_frequency
		&& clock <= smu_data->range_table[i].trans_upper_frequency) {
			sclk_setting->PllRange = i;
			break;
		}
	}

	sclk_setting->Fcw_int = (uint16_t)((clock << table->SclkFcwRangeTable[sclk_setting->PllRange].postdiv) / ref_clock);
	temp = clock << table->SclkFcwRangeTable[sclk_setting->PllRange].postdiv;
	temp <<= 0x10;
	do_div(temp, ref_clock);
	sclk_setting->Fcw_frac = temp & 0xffff;

	pcc_target_percent = 10; /*  Hardcode 10% for now. */
	pcc_target_freq = clock - (clock * pcc_target_percent / 100);
	sclk_setting->Pcc_fcw_int = (uint16_t)((pcc_target_freq << table->SclkFcwRangeTable[sclk_setting->PllRange].postdiv) / ref_clock);

	ss_target_percent = 2; /*  Hardcode 2% for now. */
	sclk_setting->SSc_En = 0;
	if (ss_target_percent) {
		sclk_setting->SSc_En = 1;
		ss_target_freq = clock - (clock * ss_target_percent / 100);
		sclk_setting->Fcw1_int = (uint16_t)((ss_target_freq << table->SclkFcwRangeTable[sclk_setting->PllRange].postdiv) / ref_clock);
		temp = ss_target_freq << table->SclkFcwRangeTable[sclk_setting->PllRange].postdiv;
		temp <<= 0x10;
		do_div(temp, ref_clock);
		sclk_setting->Fcw1_frac = temp & 0xffff;
	}

	return 0;
}

static int polaris10_populate_single_graphic_level(struct pp_hwmgr *hwmgr,
		uint32_t clock, struct SMU74_Discrete_GraphicsLevel *level)
{
	int result;
	/* PP_Clocks minClocks; */
	uint32_t mvdd;
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	SMU_SclkSetting curr_sclk_setting = { 0 };
	phm_ppt_v1_clock_voltage_dependency_table *vdd_dep_table = NULL;

	result = polaris10_calculate_sclk_params(hwmgr, clock, &curr_sclk_setting);

	if (hwmgr->od_enabled)
		vdd_dep_table = (phm_ppt_v1_clock_voltage_dependency_table *)&data->odn_dpm_table.vdd_dependency_on_sclk;
	else
		vdd_dep_table = table_info->vdd_dep_on_sclk;

	/* populate graphics levels */
	result = polaris10_get_dependency_volt_by_clk(hwmgr,
			vdd_dep_table, clock,
			&level->MinVoltage, &mvdd);

	PP_ASSERT_WITH_CODE((0 == result),
			"can not find VDDC voltage value for "
			"VDDC engine clock dependency table",
			return result);
	level->ActivityLevel = data->current_profile_setting.sclk_activity;

	level->CcPwrDynRm = 0;
	level->CcPwrDynRm1 = 0;
	level->EnabledForActivity = 0;
	level->EnabledForThrottle = 1;
	level->UpHyst = data->current_profile_setting.sclk_up_hyst;
	level->DownHyst = data->current_profile_setting.sclk_down_hyst;
	level->VoltageDownHyst = 0;
	level->PowerThrottle = 0;
	data->display_timing.min_clock_in_sr = hwmgr->display_config->min_core_set_clock_in_sr;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_SclkDeepSleep))
		level->DeepSleepDivId = smu7_get_sleep_divider_id_from_clock(clock,
								hwmgr->display_config->min_core_set_clock_in_sr);

	/* Default to slow, highest DPM level will be
	 * set to PPSMC_DISPLAY_WATERMARK_LOW later.
	 */
	if (data->update_up_hyst)
		level->UpHyst = (uint8_t)data->up_hyst;
	if (data->update_down_hyst)
		level->DownHyst = (uint8_t)data->down_hyst;

	level->SclkSetting = curr_sclk_setting;

	CONVERT_FROM_HOST_TO_SMC_UL(level->MinVoltage);
	CONVERT_FROM_HOST_TO_SMC_UL(level->CcPwrDynRm);
	CONVERT_FROM_HOST_TO_SMC_UL(level->CcPwrDynRm1);
	CONVERT_FROM_HOST_TO_SMC_US(level->ActivityLevel);
	CONVERT_FROM_HOST_TO_SMC_UL(level->SclkSetting.SclkFrequency);
	CONVERT_FROM_HOST_TO_SMC_US(level->SclkSetting.Fcw_int);
	CONVERT_FROM_HOST_TO_SMC_US(level->SclkSetting.Fcw_frac);
	CONVERT_FROM_HOST_TO_SMC_US(level->SclkSetting.Pcc_fcw_int);
	CONVERT_FROM_HOST_TO_SMC_US(level->SclkSetting.Sclk_slew_rate);
	CONVERT_FROM_HOST_TO_SMC_US(level->SclkSetting.Pcc_up_slew_rate);
	CONVERT_FROM_HOST_TO_SMC_US(level->SclkSetting.Pcc_down_slew_rate);
	CONVERT_FROM_HOST_TO_SMC_US(level->SclkSetting.Fcw1_int);
	CONVERT_FROM_HOST_TO_SMC_US(level->SclkSetting.Fcw1_frac);
	CONVERT_FROM_HOST_TO_SMC_US(level->SclkSetting.Sclk_ss_slew_rate);
	return 0;
}

static void polaris10_get_vddc_shared_railinfo(struct pp_hwmgr *hwmgr)
{
	struct polaris10_smumgr *smu_data = (struct polaris10_smumgr *)(hwmgr->smu_backend);
	SMU74_Discrete_DpmTable *table = &(smu_data->smc_state_table);
	uint8_t shared_rail;

	if (!atomctrl_get_vddc_shared_railinfo(hwmgr, &shared_rail))
		table->SharedRails = shared_rail;
}

static int polaris10_populate_all_graphic_levels(struct pp_hwmgr *hwmgr)
{
	struct smu7_hwmgr *hw_data = (struct smu7_hwmgr *)(hwmgr->backend);
	struct polaris10_smumgr *smu_data = (struct polaris10_smumgr *)(hwmgr->smu_backend);
	struct smu7_dpm_table *dpm_table = &hw_data->dpm_table;
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	struct phm_ppt_v1_pcie_table *pcie_table = table_info->pcie_table;
	uint8_t pcie_entry_cnt = (uint8_t) hw_data->dpm_table.pcie_speed_table.count;
	int result = 0;
	uint32_t array = smu_data->smu7_data.dpm_table_start +
			offsetof(SMU74_Discrete_DpmTable, GraphicsLevel);
	uint32_t array_size = sizeof(struct SMU74_Discrete_GraphicsLevel) *
			SMU74_MAX_LEVELS_GRAPHICS;
	struct SMU74_Discrete_GraphicsLevel *levels =
			smu_data->smc_state_table.GraphicsLevel;
	uint32_t i, max_entry;
	uint8_t hightest_pcie_level_enabled = 0,
		lowest_pcie_level_enabled = 0,
		mid_pcie_level_enabled = 0,
		count = 0;
	struct amdgpu_device *adev = hwmgr->adev;
	pp_atomctrl_clock_dividers_vi dividers;
	uint32_t dpm0_sclkfrequency = levels[0].SclkSetting.SclkFrequency;

	if (ASICID_IS_P20(adev->pdev->device, adev->pdev->revision) ||
	    ASICID_IS_P30(adev->pdev->device, adev->pdev->revision))
		polaris10_get_vddc_shared_railinfo(hwmgr);

	polaris10_get_sclk_range_table(hwmgr, &(smu_data->smc_state_table));

	for (i = 0; i < dpm_table->sclk_table.count; i++) {

		result = polaris10_populate_single_graphic_level(hwmgr,
				dpm_table->sclk_table.dpm_levels[i].value,
				&(smu_data->smc_state_table.GraphicsLevel[i]));
		if (result)
			return result;

		/* Making sure only DPM level 0-1 have Deep Sleep Div ID populated. */
		if (i > 1)
			levels[i].DeepSleepDivId = 0;
	}
	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
					PHM_PlatformCaps_SPLLShutdownSupport)) {
		smu_data->smc_state_table.GraphicsLevel[0].SclkSetting.SSc_En = 0;
		if (dpm0_sclkfrequency != levels[0].SclkSetting.SclkFrequency) {
			result = atomctrl_get_dfs_pll_dividers_vi(hwmgr,
					dpm_table->sclk_table.dpm_levels[0].value,
					&dividers);
			PP_ASSERT_WITH_CODE((0 == result),
					"can not find divide id for sclk",
					return result);
			smum_send_msg_to_smc_with_parameter(hwmgr,
					PPSMC_MSG_SetGpuPllDfsForSclk,
					dividers.real_clock < dpm_table->sclk_table.dpm_levels[0].value ?
					dividers.pll_post_divider - 1 : dividers.pll_post_divider,
					NULL);
		}
	}

	smu_data->smc_state_table.GraphicsDpmLevelCount =
			(uint8_t)dpm_table->sclk_table.count;
	hw_data->dpm_level_enable_mask.sclk_dpm_enable_mask =
			phm_get_dpm_level_enable_mask_value(&dpm_table->sclk_table);

	for (i = 0; i < smu_data->smc_state_table.GraphicsDpmLevelCount; i++)
		smu_data->smc_state_table.GraphicsLevel[i].EnabledForActivity =
			(hw_data->dpm_level_enable_mask.sclk_dpm_enable_mask & (1 << i)) >> i;

	if (pcie_table != NULL) {
		PP_ASSERT_WITH_CODE((1 <= pcie_entry_cnt),
				"There must be 1 or more PCIE levels defined in PPTable.",
				return -EINVAL);
		max_entry = pcie_entry_cnt - 1;
		for (i = 0; i < dpm_table->sclk_table.count; i++)
			levels[i].pcieDpmLevel =
					(uint8_t) ((i < max_entry) ? i : max_entry);
	} else {
		while (hw_data->dpm_level_enable_mask.pcie_dpm_enable_mask &&
				((hw_data->dpm_level_enable_mask.pcie_dpm_enable_mask &
						(1 << (hightest_pcie_level_enabled + 1))) != 0))
			hightest_pcie_level_enabled++;

		while (hw_data->dpm_level_enable_mask.pcie_dpm_enable_mask &&
				((hw_data->dpm_level_enable_mask.pcie_dpm_enable_mask &
						(1 << lowest_pcie_level_enabled)) == 0))
			lowest_pcie_level_enabled++;

		while ((count < hightest_pcie_level_enabled) &&
				((hw_data->dpm_level_enable_mask.pcie_dpm_enable_mask &
						(1 << (lowest_pcie_level_enabled + 1 + count))) == 0))
			count++;

		mid_pcie_level_enabled = (lowest_pcie_level_enabled + 1 + count) <
				hightest_pcie_level_enabled ?
						(lowest_pcie_level_enabled + 1 + count) :
						hightest_pcie_level_enabled;

		/* set pcieDpmLevel to hightest_pcie_level_enabled */
		for (i = 2; i < dpm_table->sclk_table.count; i++)
			levels[i].pcieDpmLevel = hightest_pcie_level_enabled;

		/* set pcieDpmLevel to lowest_pcie_level_enabled */
		levels[0].pcieDpmLevel = lowest_pcie_level_enabled;

		/* set pcieDpmLevel to mid_pcie_level_enabled */
		levels[1].pcieDpmLevel = mid_pcie_level_enabled;
	}
	/* level count will send to smc once at init smc table and never change */
	result = smu7_copy_bytes_to_smc(hwmgr, array, (uint8_t *)levels,
			(uint32_t)array_size, SMC_RAM_END);

	return result;
}


static int polaris10_populate_single_memory_level(struct pp_hwmgr *hwmgr,
		uint32_t clock, struct SMU74_Discrete_MemoryLevel *mem_level)
{
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	int result = 0;
	uint32_t mclk_stutter_mode_threshold = 40000;
	phm_ppt_v1_clock_voltage_dependency_table *vdd_dep_table = NULL;


	if (hwmgr->od_enabled)
		vdd_dep_table = (phm_ppt_v1_clock_voltage_dependency_table *)&data->odn_dpm_table.vdd_dependency_on_mclk;
	else
		vdd_dep_table = table_info->vdd_dep_on_mclk;

	if (vdd_dep_table) {
		result = polaris10_get_dependency_volt_by_clk(hwmgr,
				vdd_dep_table, clock,
				&mem_level->MinVoltage, &mem_level->MinMvdd);
		PP_ASSERT_WITH_CODE((0 == result),
				"can not find MinVddc voltage value from memory "
				"VDDC voltage dependency table", return result);
	}

	mem_level->MclkFrequency = clock;
	mem_level->EnabledForThrottle = 1;
	mem_level->EnabledForActivity = 0;
	mem_level->UpHyst = data->current_profile_setting.mclk_up_hyst;
	mem_level->DownHyst = data->current_profile_setting.mclk_down_hyst;
	mem_level->VoltageDownHyst = 0;
	mem_level->ActivityLevel = data->current_profile_setting.mclk_activity;
	mem_level->StutterEnable = false;
	mem_level->DisplayWatermark = PPSMC_DISPLAY_WATERMARK_LOW;

	data->display_timing.num_existing_displays = hwmgr->display_config->num_display;
	data->display_timing.vrefresh = hwmgr->display_config->vrefresh;

	if (mclk_stutter_mode_threshold &&
		(clock <= mclk_stutter_mode_threshold) &&
		(PHM_READ_FIELD(hwmgr->device, DPG_PIPE_STUTTER_CONTROL,
				STUTTER_ENABLE) & 0x1) &&
		(data->display_timing.num_existing_displays <= 2) &&
		data->display_timing.num_existing_displays)
		mem_level->StutterEnable = true;

	if (!result) {
		CONVERT_FROM_HOST_TO_SMC_UL(mem_level->MinMvdd);
		CONVERT_FROM_HOST_TO_SMC_UL(mem_level->MclkFrequency);
		CONVERT_FROM_HOST_TO_SMC_US(mem_level->ActivityLevel);
		CONVERT_FROM_HOST_TO_SMC_UL(mem_level->MinVoltage);
	}
	return result;
}

static int polaris10_populate_all_memory_levels(struct pp_hwmgr *hwmgr)
{
	struct smu7_hwmgr *hw_data = (struct smu7_hwmgr *)(hwmgr->backend);
	struct polaris10_smumgr *smu_data = (struct polaris10_smumgr *)(hwmgr->smu_backend);
	struct smu7_dpm_table *dpm_table = &hw_data->dpm_table;
	int result;
	/* populate MCLK dpm table to SMU7 */
	uint32_t array = smu_data->smu7_data.dpm_table_start +
			offsetof(SMU74_Discrete_DpmTable, MemoryLevel);
	uint32_t array_size = sizeof(SMU74_Discrete_MemoryLevel) *
			SMU74_MAX_LEVELS_MEMORY;
	struct SMU74_Discrete_MemoryLevel *levels =
			smu_data->smc_state_table.MemoryLevel;
	uint32_t i;

	for (i = 0; i < dpm_table->mclk_table.count; i++) {
		PP_ASSERT_WITH_CODE((0 != dpm_table->mclk_table.dpm_levels[i].value),
				"can not populate memory level as memory clock is zero",
				return -EINVAL);
		result = polaris10_populate_single_memory_level(hwmgr,
				dpm_table->mclk_table.dpm_levels[i].value,
				&levels[i]);
		if (i == dpm_table->mclk_table.count - 1)
			levels[i].DisplayWatermark = PPSMC_DISPLAY_WATERMARK_HIGH;
		if (result)
			return result;
	}

	smu_data->smc_state_table.MemoryDpmLevelCount =
			(uint8_t)dpm_table->mclk_table.count;
	hw_data->dpm_level_enable_mask.mclk_dpm_enable_mask =
			phm_get_dpm_level_enable_mask_value(&dpm_table->mclk_table);

	for (i = 0; i < smu_data->smc_state_table.MemoryDpmLevelCount; i++)
		smu_data->smc_state_table.MemoryLevel[i].EnabledForActivity =
			(hw_data->dpm_level_enable_mask.mclk_dpm_enable_mask & (1 << i)) >> i;

	/* level count will send to smc once at init smc table and never change */
	result = smu7_copy_bytes_to_smc(hwmgr, array, (uint8_t *)levels,
			(uint32_t)array_size, SMC_RAM_END);

	return result;
}

static int polaris10_populate_mvdd_value(struct pp_hwmgr *hwmgr,
		uint32_t mclk, SMIO_Pattern *smio_pat)
{
	const struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	uint32_t i = 0;

	if (SMU7_VOLTAGE_CONTROL_NONE != data->mvdd_control) {
		/* find mvdd value which clock is more than request */
		for (i = 0; i < table_info->vdd_dep_on_mclk->count; i++) {
			if (mclk <= table_info->vdd_dep_on_mclk->entries[i].clk) {
				smio_pat->Voltage = data->mvdd_voltage_table.entries[i].value;
				break;
			}
		}
		PP_ASSERT_WITH_CODE(i < table_info->vdd_dep_on_mclk->count,
				"MVDD Voltage is outside the supported range.",
				return -EINVAL);
	} else
		return -EINVAL;

	return 0;
}

static int polaris10_populate_smc_acpi_level(struct pp_hwmgr *hwmgr,
		SMU74_Discrete_DpmTable *table)
{
	int result = 0;
	uint32_t sclk_frequency;
	const struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	SMIO_Pattern vol_level;
	uint32_t mvdd;

	table->ACPILevel.Flags &= ~PPSMC_SWSTATE_FLAG_DC;

	/* Get MinVoltage and Frequency from DPM0,
	 * already converted to SMC_UL */
	sclk_frequency = data->vbios_boot_state.sclk_bootup_value;
	result = polaris10_get_dependency_volt_by_clk(hwmgr,
			table_info->vdd_dep_on_sclk,
			sclk_frequency,
			&table->ACPILevel.MinVoltage, &mvdd);
	PP_ASSERT_WITH_CODE((0 == result),
			"Cannot find ACPI VDDC voltage value "
			"in Clock Dependency Table",
			);

	result = polaris10_calculate_sclk_params(hwmgr, sclk_frequency,  &(table->ACPILevel.SclkSetting));
	PP_ASSERT_WITH_CODE(result == 0, "Error retrieving Engine Clock dividers from VBIOS.", return result);

	table->ACPILevel.DeepSleepDivId = 0;
	table->ACPILevel.CcPwrDynRm = 0;
	table->ACPILevel.CcPwrDynRm1 = 0;

	CONVERT_FROM_HOST_TO_SMC_UL(table->ACPILevel.Flags);
	CONVERT_FROM_HOST_TO_SMC_UL(table->ACPILevel.MinVoltage);
	CONVERT_FROM_HOST_TO_SMC_UL(table->ACPILevel.CcPwrDynRm);
	CONVERT_FROM_HOST_TO_SMC_UL(table->ACPILevel.CcPwrDynRm1);

	CONVERT_FROM_HOST_TO_SMC_UL(table->ACPILevel.SclkSetting.SclkFrequency);
	CONVERT_FROM_HOST_TO_SMC_US(table->ACPILevel.SclkSetting.Fcw_int);
	CONVERT_FROM_HOST_TO_SMC_US(table->ACPILevel.SclkSetting.Fcw_frac);
	CONVERT_FROM_HOST_TO_SMC_US(table->ACPILevel.SclkSetting.Pcc_fcw_int);
	CONVERT_FROM_HOST_TO_SMC_US(table->ACPILevel.SclkSetting.Sclk_slew_rate);
	CONVERT_FROM_HOST_TO_SMC_US(table->ACPILevel.SclkSetting.Pcc_up_slew_rate);
	CONVERT_FROM_HOST_TO_SMC_US(table->ACPILevel.SclkSetting.Pcc_down_slew_rate);
	CONVERT_FROM_HOST_TO_SMC_US(table->ACPILevel.SclkSetting.Fcw1_int);
	CONVERT_FROM_HOST_TO_SMC_US(table->ACPILevel.SclkSetting.Fcw1_frac);
	CONVERT_FROM_HOST_TO_SMC_US(table->ACPILevel.SclkSetting.Sclk_ss_slew_rate);


	/* Get MinVoltage and Frequency from DPM0, already converted to SMC_UL */
	table->MemoryACPILevel.MclkFrequency = data->vbios_boot_state.mclk_bootup_value;
	result = polaris10_get_dependency_volt_by_clk(hwmgr,
			table_info->vdd_dep_on_mclk,
			table->MemoryACPILevel.MclkFrequency,
			&table->MemoryACPILevel.MinVoltage, &mvdd);
	PP_ASSERT_WITH_CODE((0 == result),
			"Cannot find ACPI VDDCI voltage value "
			"in Clock Dependency Table",
			);

	if (!((SMU7_VOLTAGE_CONTROL_NONE == data->mvdd_control) ||
			(data->mclk_dpm_key_disabled)))
		polaris10_populate_mvdd_value(hwmgr,
				data->dpm_table.mclk_table.dpm_levels[0].value,
				&vol_level);

	if (0 == polaris10_populate_mvdd_value(hwmgr, 0, &vol_level))
		table->MemoryACPILevel.MinMvdd = PP_HOST_TO_SMC_UL(vol_level.Voltage);
	else
		table->MemoryACPILevel.MinMvdd = 0;

	table->MemoryACPILevel.StutterEnable = false;

	table->MemoryACPILevel.EnabledForThrottle = 0;
	table->MemoryACPILevel.EnabledForActivity = 0;
	table->MemoryACPILevel.UpHyst = 0;
	table->MemoryACPILevel.DownHyst = 100;
	table->MemoryACPILevel.VoltageDownHyst = 0;
	/* To align with the settings from other OSes */
	table->MemoryACPILevel.ActivityLevel =
			PP_HOST_TO_SMC_US(data->current_profile_setting.sclk_activity);

	CONVERT_FROM_HOST_TO_SMC_UL(table->MemoryACPILevel.MclkFrequency);
	CONVERT_FROM_HOST_TO_SMC_UL(table->MemoryACPILevel.MinVoltage);

	return result;
}

static int polaris10_populate_smc_vce_level(struct pp_hwmgr *hwmgr,
		SMU74_Discrete_DpmTable *table)
{
	int result = -EINVAL;
	uint8_t count;
	struct pp_atomctrl_clock_dividers_vi dividers;
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	struct phm_ppt_v1_mm_clock_voltage_dependency_table *mm_table =
			table_info->mm_dep_table;
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);
	uint32_t vddci;

	table->VceLevelCount = (uint8_t)(mm_table->count);
	table->VceBootLevel = 0;

	for (count = 0; count < table->VceLevelCount; count++) {
		table->VceLevel[count].Frequency = mm_table->entries[count].eclk;
		table->VceLevel[count].MinVoltage = 0;
		table->VceLevel[count].MinVoltage |=
				(mm_table->entries[count].vddc * VOLTAGE_SCALE) << VDDC_SHIFT;

		if (SMU7_VOLTAGE_CONTROL_BY_GPIO == data->vddci_control)
			vddci = (uint32_t)phm_find_closest_vddci(&(data->vddci_voltage_table),
						mm_table->entries[count].vddc - VDDC_VDDCI_DELTA);
		else if (SMU7_VOLTAGE_CONTROL_BY_SVID2 == data->vddci_control)
			vddci = mm_table->entries[count].vddc - VDDC_VDDCI_DELTA;
		else
			vddci = (data->vbios_boot_state.vddci_bootup_value * VOLTAGE_SCALE) << VDDCI_SHIFT;


		table->VceLevel[count].MinVoltage |=
				(vddci * VOLTAGE_SCALE) << VDDCI_SHIFT;
		table->VceLevel[count].MinVoltage |= 1 << PHASES_SHIFT;

		/*retrieve divider value for VBIOS */
		result = atomctrl_get_dfs_pll_dividers_vi(hwmgr,
				table->VceLevel[count].Frequency, &dividers);
		PP_ASSERT_WITH_CODE((0 == result),
				"can not find divide id for VCE engine clock",
				return result);

		table->VceLevel[count].Divider = (uint8_t)dividers.pll_post_divider;

		CONVERT_FROM_HOST_TO_SMC_UL(table->VceLevel[count].Frequency);
		CONVERT_FROM_HOST_TO_SMC_UL(table->VceLevel[count].MinVoltage);
	}
	return result;
}

static int polaris10_populate_smc_samu_level(struct pp_hwmgr *hwmgr,
		SMU74_Discrete_DpmTable *table)
{
	int result = -EINVAL;
	uint8_t count;
	struct pp_atomctrl_clock_dividers_vi dividers;
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	struct phm_ppt_v1_mm_clock_voltage_dependency_table *mm_table =
			table_info->mm_dep_table;
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);
	uint32_t vddci;

	table->SamuLevelCount = (uint8_t)(mm_table->count);
	table->SamuBootLevel = 0;

	for (count = 0; count < table->SamuLevelCount; count++) {
		table->SamuLevel[count].Frequency = mm_table->entries[count].samclock;
		table->SamuLevel[count].MinVoltage |=
				(mm_table->entries[count].vddc * VOLTAGE_SCALE) << VDDC_SHIFT;

		if (SMU7_VOLTAGE_CONTROL_BY_GPIO == data->vddci_control)
			vddci = (uint32_t)phm_find_closest_vddci(&(data->vddci_voltage_table),
						mm_table->entries[count].vddc - VDDC_VDDCI_DELTA);
		else if (SMU7_VOLTAGE_CONTROL_BY_SVID2 == data->vddci_control)
			vddci = mm_table->entries[count].vddc - VDDC_VDDCI_DELTA;
		else
			vddci = (data->vbios_boot_state.vddci_bootup_value * VOLTAGE_SCALE) << VDDCI_SHIFT;


		table->SamuLevel[count].MinVoltage |=
				(vddci * VOLTAGE_SCALE) << VDDCI_SHIFT;
		table->SamuLevel[count].MinVoltage |= 1 << PHASES_SHIFT;

		/*retrieve divider value for VBIOS */
		result = atomctrl_get_dfs_pll_dividers_vi(hwmgr,
				table->SamuLevel[count].Frequency, &dividers);
		PP_ASSERT_WITH_CODE((0 == result),
				"can not find divide id for VCE engine clock",
				return result);

		table->SamuLevel[count].Divider = (uint8_t)dividers.pll_post_divider;

		CONVERT_FROM_HOST_TO_SMC_UL(table->SamuLevel[count].Frequency);
		CONVERT_FROM_HOST_TO_SMC_UL(table->SamuLevel[count].MinVoltage);
	}
	return result;
}

static int polaris10_populate_memory_timing_parameters(struct pp_hwmgr *hwmgr,
		int32_t eng_clock, int32_t mem_clock,
		SMU74_Discrete_MCArbDramTimingTableEntry *arb_regs)
{
	uint32_t dram_timing;
	uint32_t dram_timing2;
	uint32_t burst_time;
	int result;

	result = atomctrl_set_engine_dram_timings_rv770(hwmgr,
			eng_clock, mem_clock);
	PP_ASSERT_WITH_CODE(result == 0,
			"Error calling VBIOS to set DRAM_TIMING.", return result);

	dram_timing = cgs_read_register(hwmgr->device, mmMC_ARB_DRAM_TIMING);
	dram_timing2 = cgs_read_register(hwmgr->device, mmMC_ARB_DRAM_TIMING2);
	burst_time = PHM_READ_FIELD(hwmgr->device, MC_ARB_BURST_TIME, STATE0);


	arb_regs->McArbDramTiming  = PP_HOST_TO_SMC_UL(dram_timing);
	arb_regs->McArbDramTiming2 = PP_HOST_TO_SMC_UL(dram_timing2);
	arb_regs->McArbBurstTime   = (uint8_t)burst_time;

	return 0;
}

static int polaris10_program_memory_timing_parameters(struct pp_hwmgr *hwmgr)
{
	struct smu7_hwmgr *hw_data = (struct smu7_hwmgr *)(hwmgr->backend);
	struct polaris10_smumgr *smu_data = (struct polaris10_smumgr *)(hwmgr->smu_backend);
	struct SMU74_Discrete_MCArbDramTimingTable arb_regs;
	uint32_t i, j;
	int result = 0;

	for (i = 0; i < hw_data->dpm_table.sclk_table.count; i++) {
		for (j = 0; j < hw_data->dpm_table.mclk_table.count; j++) {
			result = polaris10_populate_memory_timing_parameters(hwmgr,
					hw_data->dpm_table.sclk_table.dpm_levels[i].value,
					hw_data->dpm_table.mclk_table.dpm_levels[j].value,
					&arb_regs.entries[i][j]);
			if (result == 0 && i == 0)
				result = atomctrl_set_ac_timing_ai(hwmgr, hw_data->dpm_table.mclk_table.dpm_levels[j].value, j);
			if (result != 0)
				return result;
		}
	}

	result = smu7_copy_bytes_to_smc(
			hwmgr,
			smu_data->smu7_data.arb_table_start,
			(uint8_t *)&arb_regs,
			sizeof(SMU74_Discrete_MCArbDramTimingTable),
			SMC_RAM_END);
	return result;
}

static int polaris10_populate_smc_uvd_level(struct pp_hwmgr *hwmgr,
		struct SMU74_Discrete_DpmTable *table)
{
	int result = -EINVAL;
	uint8_t count;
	struct pp_atomctrl_clock_dividers_vi dividers;
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	struct phm_ppt_v1_mm_clock_voltage_dependency_table *mm_table =
			table_info->mm_dep_table;
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);
	uint32_t vddci;

	table->UvdLevelCount = (uint8_t)(mm_table->count);
	table->UvdBootLevel = 0;

	for (count = 0; count < table->UvdLevelCount; count++) {
		table->UvdLevel[count].MinVoltage = 0;
		table->UvdLevel[count].VclkFrequency = mm_table->entries[count].vclk;
		table->UvdLevel[count].DclkFrequency = mm_table->entries[count].dclk;
		table->UvdLevel[count].MinVoltage |= (mm_table->entries[count].vddc *
				VOLTAGE_SCALE) << VDDC_SHIFT;

		if (SMU7_VOLTAGE_CONTROL_BY_GPIO == data->vddci_control)
			vddci = (uint32_t)phm_find_closest_vddci(&(data->vddci_voltage_table),
						mm_table->entries[count].vddc - VDDC_VDDCI_DELTA);
		else if (SMU7_VOLTAGE_CONTROL_BY_SVID2 == data->vddci_control)
			vddci = mm_table->entries[count].vddc - VDDC_VDDCI_DELTA;
		else
			vddci = (data->vbios_boot_state.vddci_bootup_value * VOLTAGE_SCALE) << VDDCI_SHIFT;

		table->UvdLevel[count].MinVoltage |= (vddci * VOLTAGE_SCALE) << VDDCI_SHIFT;
		table->UvdLevel[count].MinVoltage |= 1 << PHASES_SHIFT;

		/* retrieve divider value for VBIOS */
		result = atomctrl_get_dfs_pll_dividers_vi(hwmgr,
				table->UvdLevel[count].VclkFrequency, &dividers);
		PP_ASSERT_WITH_CODE((0 == result),
				"can not find divide id for Vclk clock", return result);

		table->UvdLevel[count].VclkDivider = (uint8_t)dividers.pll_post_divider;

		result = atomctrl_get_dfs_pll_dividers_vi(hwmgr,
				table->UvdLevel[count].DclkFrequency, &dividers);
		PP_ASSERT_WITH_CODE((0 == result),
				"can not find divide id for Dclk clock", return result);

		table->UvdLevel[count].DclkDivider = (uint8_t)dividers.pll_post_divider;

		CONVERT_FROM_HOST_TO_SMC_UL(table->UvdLevel[count].VclkFrequency);
		CONVERT_FROM_HOST_TO_SMC_UL(table->UvdLevel[count].DclkFrequency);
		CONVERT_FROM_HOST_TO_SMC_UL(table->UvdLevel[count].MinVoltage);
	}

	return result;
}

static int polaris10_populate_smc_boot_level(struct pp_hwmgr *hwmgr,
		struct SMU74_Discrete_DpmTable *table)
{
	int result = 0;
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);

	table->GraphicsBootLevel = 0;
	table->MemoryBootLevel = 0;

	/* find boot level from dpm table */
	result = phm_find_boot_level(&(data->dpm_table.sclk_table),
			data->vbios_boot_state.sclk_bootup_value,
			(uint32_t *)&(table->GraphicsBootLevel));
	if (result) {
		table->GraphicsBootLevel = 0;
		result = 0;
	}

	result = phm_find_boot_level(&(data->dpm_table.mclk_table),
			data->vbios_boot_state.mclk_bootup_value,
			(uint32_t *)&(table->MemoryBootLevel));
	if (result) {
		table->MemoryBootLevel = 0;
		result = 0;
	}

	table->BootVddc  = data->vbios_boot_state.vddc_bootup_value *
			VOLTAGE_SCALE;
	table->BootVddci = data->vbios_boot_state.vddci_bootup_value *
			VOLTAGE_SCALE;
	table->BootMVdd  = data->vbios_boot_state.mvdd_bootup_value *
			VOLTAGE_SCALE;

	CONVERT_FROM_HOST_TO_SMC_US(table->BootVddc);
	CONVERT_FROM_HOST_TO_SMC_US(table->BootVddci);
	CONVERT_FROM_HOST_TO_SMC_US(table->BootMVdd);

	return 0;
}

static int polaris10_populate_smc_initailial_state(struct pp_hwmgr *hwmgr)
{
	struct smu7_hwmgr *hw_data = (struct smu7_hwmgr *)(hwmgr->backend);
	struct polaris10_smumgr *smu_data = (struct polaris10_smumgr *)(hwmgr->smu_backend);
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	uint8_t count, level;

	count = (uint8_t)(table_info->vdd_dep_on_sclk->count);

	for (level = 0; level < count; level++) {
		if (table_info->vdd_dep_on_sclk->entries[level].clk >=
				hw_data->vbios_boot_state.sclk_bootup_value) {
			smu_data->smc_state_table.GraphicsBootLevel = level;
			break;
		}
	}

	count = (uint8_t)(table_info->vdd_dep_on_mclk->count);
	for (level = 0; level < count; level++) {
		if (table_info->vdd_dep_on_mclk->entries[level].clk >=
				hw_data->vbios_boot_state.mclk_bootup_value) {
			smu_data->smc_state_table.MemoryBootLevel = level;
			break;
		}
	}

	return 0;
}

#define STRAP_ASIC_RO_LSB    2168
#define STRAP_ASIC_RO_MSB    2175

static int polaris10_populate_clock_stretcher_data_table(struct pp_hwmgr *hwmgr)
{
	struct polaris10_smumgr *smu_data = (struct polaris10_smumgr *)(hwmgr->smu_backend);
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	struct phm_ppt_v1_clock_voltage_dependency_table *sclk_table =
			table_info->vdd_dep_on_sclk;
	uint32_t ro, efuse, volt_without_cks, volt_with_cks, value;
	uint8_t i, stretch_amount, volt_offset = 0;

	stretch_amount = (uint8_t)table_info->cac_dtp_table->usClockStretchAmount;

	/* Read SMU_Eefuse to read and calculate RO and determine
	 * if the part is SS or FF. if RO >= 1660MHz, part is FF.
	 */
	atomctrl_read_efuse(hwmgr, STRAP_ASIC_RO_LSB, STRAP_ASIC_RO_MSB, &efuse);
	ro = ((efuse * (data->ro_range_maximum - data->ro_range_minimum)) / 255) +
		data->ro_range_minimum;

	/* Populate Sclk_CKS_masterEn0_7 and Sclk_voltageOffset */
	for (i = 0; i < sclk_table->count; i++) {
		smu_data->smc_state_table.Sclk_CKS_masterEn0_7 |=
				sclk_table->entries[i].cks_enable << i;
		if (hwmgr->chip_id == CHIP_POLARIS10) {
			volt_without_cks = (uint32_t)((2753594000U + (sclk_table->entries[i].clk/100) * 136418 - (ro - 70) * 1000000) / \
						(2424180 - (sclk_table->entries[i].clk/100) * 1132925/1000));
			volt_with_cks = (uint32_t)((2797202000U + sclk_table->entries[i].clk/100 * 3232 - (ro - 65) * 1000000) / \
					(2522480 - sclk_table->entries[i].clk/100 * 115764/100));
		} else {
			volt_without_cks = (uint32_t)((2416794800U + (sclk_table->entries[i].clk/100) * 1476925/10 - (ro - 50) * 1000000) / \
						(2625416 - (sclk_table->entries[i].clk/100) * (12586807/10000)));
			volt_with_cks = (uint32_t)((2999656000U - sclk_table->entries[i].clk/100 * 392803 - (ro - 44) * 1000000) / \
					(3422454 - sclk_table->entries[i].clk/100 * (18886376/10000)));
		}

		if (volt_without_cks >= volt_with_cks)
			volt_offset = (uint8_t)(((volt_without_cks - volt_with_cks +
					sclk_table->entries[i].cks_voffset) * 100 + 624) / 625);

		smu_data->smc_state_table.Sclk_voltageOffset[i] = volt_offset;
	}

	smu_data->smc_state_table.LdoRefSel = (table_info->cac_dtp_table->ucCKS_LDO_REFSEL != 0) ? table_info->cac_dtp_table->ucCKS_LDO_REFSEL : 5;

	/* Populate CKS Lookup Table */
	if (stretch_amount == 0 || stretch_amount > 5) {
		phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_ClockStretcher);
		PP_ASSERT_WITH_CODE(false,
				"Stretch Amount in PPTable not supported",
				return -EINVAL);
	}

	value = cgs_read_ind_register(hwmgr->device, CGS_IND_REG__SMC, ixPWR_CKS_CNTL);
	value &= 0xFFFFFFFE;
	cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC, ixPWR_CKS_CNTL, value);

	return 0;
}

static int polaris10_populate_vr_config(struct pp_hwmgr *hwmgr,
		struct SMU74_Discrete_DpmTable *table)
{
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);
	struct polaris10_smumgr *smu_data = (struct polaris10_smumgr *)(hwmgr->smu_backend);
	uint16_t config;

	config = VR_MERGED_WITH_VDDC;
	table->VRConfig |= (config << VRCONF_VDDGFX_SHIFT);

	/* Set Vddc Voltage Controller */
	if (SMU7_VOLTAGE_CONTROL_BY_SVID2 == data->voltage_control) {
		config = VR_SVI2_PLANE_1;
		table->VRConfig |= config;
	} else if (SMU7_VOLTAGE_CONTROL_BY_GPIO == data->voltage_control) {
		config = VR_SMIO_PATTERN_1;
		table->VRConfig |= config;
	} else {
		PP_ASSERT_WITH_CODE(false,
				"VDDC should be on SVI2 control in merged mode!",
				);
	}
	/* Set Vddci Voltage Controller */
	if (SMU7_VOLTAGE_CONTROL_BY_SVID2 == data->vddci_control) {
		config = VR_SVI2_PLANE_2;  /* only in merged mode */
		table->VRConfig |= (config << VRCONF_VDDCI_SHIFT);
	} else if (SMU7_VOLTAGE_CONTROL_BY_GPIO == data->vddci_control) {
		config = VR_SMIO_PATTERN_1;
		table->VRConfig |= (config << VRCONF_VDDCI_SHIFT);
	} else {
		config = VR_STATIC_VOLTAGE;
		table->VRConfig |= (config << VRCONF_VDDCI_SHIFT);
	}
	/* Set Mvdd Voltage Controller */
	if (SMU7_VOLTAGE_CONTROL_BY_SVID2 == data->mvdd_control) {
		if (config != VR_SVI2_PLANE_2) {
			config = VR_SVI2_PLANE_2;
			table->VRConfig |= (config << VRCONF_MVDD_SHIFT);
			cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC, smu_data->smu7_data.soft_regs_start +
				offsetof(SMU74_SoftRegisters, AllowMvddSwitch), 0x1);
		} else {
			config = VR_STATIC_VOLTAGE;
			table->VRConfig |= (config << VRCONF_MVDD_SHIFT);
		}
	} else if (SMU7_VOLTAGE_CONTROL_BY_GPIO == data->mvdd_control) {
		config = VR_SMIO_PATTERN_2;
		table->VRConfig |= (config << VRCONF_MVDD_SHIFT);
		cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC, smu_data->smu7_data.soft_regs_start +
			offsetof(SMU74_SoftRegisters, AllowMvddSwitch), 0x1);
	} else {
		config = VR_STATIC_VOLTAGE;
		table->VRConfig |= (config << VRCONF_MVDD_SHIFT);
	}

	return 0;
}


static int polaris10_populate_avfs_parameters(struct pp_hwmgr *hwmgr)
{
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);
	struct polaris10_smumgr *smu_data = (struct polaris10_smumgr *)(hwmgr->smu_backend);
	struct amdgpu_device *adev = hwmgr->adev;

	SMU74_Discrete_DpmTable  *table = &(smu_data->smc_state_table);
	int result = 0;
	struct pp_atom_ctrl__avfs_parameters avfs_params = {0};
	AVFS_meanNsigma_t AVFS_meanNsigma = { {0} };
	AVFS_Sclk_Offset_t AVFS_SclkOffset = { {0} };
	uint32_t tmp, i;

	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)hwmgr->pptable;
	struct phm_ppt_v1_clock_voltage_dependency_table *sclk_table =
			table_info->vdd_dep_on_sclk;


	if (!hwmgr->avfs_supported)
		return 0;


	if (SMU7_VOLTAGE_CONTROL_BY_GPIO == data->voltage_control) {
		hwmgr->avfs_supported = 0;
		return 0;
	}

	result = atomctrl_get_avfs_information(hwmgr, &avfs_params);

	if (0 == result) {
		if (ASICID_IS_P20(adev->pdev->device, adev->pdev->revision) ||
		    ((hwmgr->chip_id == CHIP_POLARIS12) && !ASICID_IS_P23(adev->pdev->device, adev->pdev->revision)) ||
		    ASICID_IS_P21(adev->pdev->device, adev->pdev->revision)) {
			avfs_params.ucEnableApplyAVFS_CKS_OFF_Voltage = 1;
			if ((adev->pdev->device == 0x67ef && adev->pdev->revision == 0xe5) ||
			    (adev->pdev->device == 0x67ff && adev->pdev->revision == 0xef)) {
				if ((avfs_params.ulGB_VDROOP_TABLE_CKSOFF_a0 == 0xEA522DD3) &&
				    (avfs_params.ulGB_VDROOP_TABLE_CKSOFF_a1 == 0x5645A) &&
				    (avfs_params.ulGB_VDROOP_TABLE_CKSOFF_a2 == 0x33F9E) &&
				    (avfs_params.ulAVFSGB_FUSE_TABLE_CKSOFF_m1 == 0xFFFFC5CC) &&
				    (avfs_params.usAVFSGB_FUSE_TABLE_CKSOFF_m2 == 0x1B1A) &&
				    (avfs_params.ulAVFSGB_FUSE_TABLE_CKSOFF_b == 0xFFFFFCED)) {
					avfs_params.ulGB_VDROOP_TABLE_CKSOFF_a0   = 0xF718F1D4;
					avfs_params.ulGB_VDROOP_TABLE_CKSOFF_a1   = 0x323FD;
					avfs_params.ulGB_VDROOP_TABLE_CKSOFF_a2   = 0x1E455;
					avfs_params.ulAVFSGB_FUSE_TABLE_CKSOFF_m1 = 0;
					avfs_params.usAVFSGB_FUSE_TABLE_CKSOFF_m2 = 0;
					avfs_params.ulAVFSGB_FUSE_TABLE_CKSOFF_b  = 0x23;
				}
			} else if (hwmgr->chip_id == CHIP_POLARIS12 && !ASICID_IS_P23(adev->pdev->device, adev->pdev->revision)) {
				avfs_params.ulGB_VDROOP_TABLE_CKSOFF_a0   = 0xF6B024DD;
				avfs_params.ulGB_VDROOP_TABLE_CKSOFF_a1   = 0x3005E;
				avfs_params.ulGB_VDROOP_TABLE_CKSOFF_a2   = 0x18A5F;
				avfs_params.ulAVFSGB_FUSE_TABLE_CKSOFF_m1 = 0x315;
				avfs_params.usAVFSGB_FUSE_TABLE_CKSOFF_m2 = 0xFED1;
				avfs_params.ulAVFSGB_FUSE_TABLE_CKSOFF_b  = 0x3B;
			} else if (ASICID_IS_P20(adev->pdev->device, adev->pdev->revision)) {
				avfs_params.ulGB_VDROOP_TABLE_CKSOFF_a0   = 0xF843B66B;
				avfs_params.ulGB_VDROOP_TABLE_CKSOFF_a1   = 0x59CB5;
				avfs_params.ulGB_VDROOP_TABLE_CKSOFF_a2   = 0xFFFF287F;
				avfs_params.ulAVFSGB_FUSE_TABLE_CKSOFF_m1 = 0;
				avfs_params.usAVFSGB_FUSE_TABLE_CKSOFF_m2 = 0xFF23;
				avfs_params.ulAVFSGB_FUSE_TABLE_CKSOFF_b  = 0x58;
			}
		}
	}

	if (0 == result) {
		table->BTCGB_VDROOP_TABLE[0].a0  = PP_HOST_TO_SMC_UL(avfs_params.ulGB_VDROOP_TABLE_CKSON_a0);
		table->BTCGB_VDROOP_TABLE[0].a1  = PP_HOST_TO_SMC_UL(avfs_params.ulGB_VDROOP_TABLE_CKSON_a1);
		table->BTCGB_VDROOP_TABLE[0].a2  = PP_HOST_TO_SMC_UL(avfs_params.ulGB_VDROOP_TABLE_CKSON_a2);
		table->BTCGB_VDROOP_TABLE[1].a0  = PP_HOST_TO_SMC_UL(avfs_params.ulGB_VDROOP_TABLE_CKSOFF_a0);
		table->BTCGB_VDROOP_TABLE[1].a1  = PP_HOST_TO_SMC_UL(avfs_params.ulGB_VDROOP_TABLE_CKSOFF_a1);
		table->BTCGB_VDROOP_TABLE[1].a2  = PP_HOST_TO_SMC_UL(avfs_params.ulGB_VDROOP_TABLE_CKSOFF_a2);
		table->AVFSGB_VDROOP_TABLE[0].m1 = PP_HOST_TO_SMC_UL(avfs_params.ulAVFSGB_FUSE_TABLE_CKSON_m1);
		table->AVFSGB_VDROOP_TABLE[0].m2 = PP_HOST_TO_SMC_US(avfs_params.usAVFSGB_FUSE_TABLE_CKSON_m2);
		table->AVFSGB_VDROOP_TABLE[0].b  = PP_HOST_TO_SMC_UL(avfs_params.ulAVFSGB_FUSE_TABLE_CKSON_b);
		table->AVFSGB_VDROOP_TABLE[0].m1_shift = 24;
		table->AVFSGB_VDROOP_TABLE[0].m2_shift  = 12;
		table->AVFSGB_VDROOP_TABLE[1].m1 = PP_HOST_TO_SMC_UL(avfs_params.ulAVFSGB_FUSE_TABLE_CKSOFF_m1);
		table->AVFSGB_VDROOP_TABLE[1].m2 = PP_HOST_TO_SMC_US(avfs_params.usAVFSGB_FUSE_TABLE_CKSOFF_m2);
		table->AVFSGB_VDROOP_TABLE[1].b  = PP_HOST_TO_SMC_UL(avfs_params.ulAVFSGB_FUSE_TABLE_CKSOFF_b);
		table->AVFSGB_VDROOP_TABLE[1].m1_shift = 24;
		table->AVFSGB_VDROOP_TABLE[1].m2_shift  = 12;
		table->MaxVoltage                = PP_HOST_TO_SMC_US(avfs_params.usMaxVoltage_0_25mv);
		AVFS_meanNsigma.Aconstant[0]      = PP_HOST_TO_SMC_UL(avfs_params.ulAVFS_meanNsigma_Acontant0);
		AVFS_meanNsigma.Aconstant[1]      = PP_HOST_TO_SMC_UL(avfs_params.ulAVFS_meanNsigma_Acontant1);
		AVFS_meanNsigma.Aconstant[2]      = PP_HOST_TO_SMC_UL(avfs_params.ulAVFS_meanNsigma_Acontant2);
		AVFS_meanNsigma.DC_tol_sigma      = PP_HOST_TO_SMC_US(avfs_params.usAVFS_meanNsigma_DC_tol_sigma);
		AVFS_meanNsigma.Platform_mean     = PP_HOST_TO_SMC_US(avfs_params.usAVFS_meanNsigma_Platform_mean);
		AVFS_meanNsigma.PSM_Age_CompFactor = PP_HOST_TO_SMC_US(avfs_params.usPSM_Age_ComFactor);
		AVFS_meanNsigma.Platform_sigma     = PP_HOST_TO_SMC_US(avfs_params.usAVFS_meanNsigma_Platform_sigma);

		for (i = 0; i < NUM_VFT_COLUMNS; i++) {
			AVFS_meanNsigma.Static_Voltage_Offset[i] = (uint8_t)(sclk_table->entries[i].cks_voffset * 100 / 625);
			AVFS_SclkOffset.Sclk_Offset[i] = PP_HOST_TO_SMC_US((uint16_t)(sclk_table->entries[i].sclk_offset) / 100);
		}

		result = smu7_read_smc_sram_dword(hwmgr,
				SMU7_FIRMWARE_HEADER_LOCATION + offsetof(SMU74_Firmware_Header, AvfsMeanNSigma),
				&tmp, SMC_RAM_END);

		smu7_copy_bytes_to_smc(hwmgr,
					tmp,
					(uint8_t *)&AVFS_meanNsigma,
					sizeof(AVFS_meanNsigma_t),
					SMC_RAM_END);

		result = smu7_read_smc_sram_dword(hwmgr,
				SMU7_FIRMWARE_HEADER_LOCATION + offsetof(SMU74_Firmware_Header, AvfsSclkOffsetTable),
				&tmp, SMC_RAM_END);
		smu7_copy_bytes_to_smc(hwmgr,
					tmp,
					(uint8_t *)&AVFS_SclkOffset,
					sizeof(AVFS_Sclk_Offset_t),
					SMC_RAM_END);

		data->avfs_vdroop_override_setting = (avfs_params.ucEnableGB_VDROOP_TABLE_CKSON << BTCGB0_Vdroop_Enable_SHIFT) |
						(avfs_params.ucEnableGB_VDROOP_TABLE_CKSOFF << BTCGB1_Vdroop_Enable_SHIFT) |
						(avfs_params.ucEnableGB_FUSE_TABLE_CKSON << AVFSGB0_Vdroop_Enable_SHIFT) |
						(avfs_params.ucEnableGB_FUSE_TABLE_CKSOFF << AVFSGB1_Vdroop_Enable_SHIFT);
		data->apply_avfs_cks_off_voltage = avfs_params.ucEnableApplyAVFS_CKS_OFF_Voltage == 1;
	}
	return result;
}

static void polaris10_initialize_power_tune_defaults(struct pp_hwmgr *hwmgr)
{
	struct polaris10_smumgr *smu_data = (struct polaris10_smumgr *)(hwmgr->smu_backend);
	struct  phm_ppt_v1_information *table_info =
			(struct  phm_ppt_v1_information *)(hwmgr->pptable);

	if (table_info &&
			table_info->cac_dtp_table->usPowerTuneDataSetID <= POWERTUNE_DEFAULT_SET_MAX &&
			table_info->cac_dtp_table->usPowerTuneDataSetID)
		smu_data->power_tune_defaults =
				&polaris10_power_tune_data_set_array
				[table_info->cac_dtp_table->usPowerTuneDataSetID - 1];
	else
		smu_data->power_tune_defaults = &polaris10_power_tune_data_set_array[0];

}

static int polaris10_init_smc_table(struct pp_hwmgr *hwmgr)
{
	int result;
	struct smu7_hwmgr *hw_data = (struct smu7_hwmgr *)(hwmgr->backend);
	struct polaris10_smumgr *smu_data = (struct polaris10_smumgr *)(hwmgr->smu_backend);

	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	struct SMU74_Discrete_DpmTable *table = &(smu_data->smc_state_table);
	uint8_t i;
	struct pp_atomctrl_gpio_pin_assignment gpio_pin;
	pp_atomctrl_clock_dividers_vi dividers;
	struct phm_ppt_v1_gpio_table *gpio_table = table_info->gpio_table;

	polaris10_initialize_power_tune_defaults(hwmgr);

	if (SMU7_VOLTAGE_CONTROL_NONE != hw_data->voltage_control)
		polaris10_populate_smc_voltage_tables(hwmgr, table);

	table->SystemFlags = 0;
	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_AutomaticDCTransition))
		table->SystemFlags |= PPSMC_SYSTEMFLAG_GPIO_DC;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_StepVddc))
		table->SystemFlags |= PPSMC_SYSTEMFLAG_STEPVDDC;

	if (hw_data->is_memory_gddr5)
		table->SystemFlags |= PPSMC_SYSTEMFLAG_GDDR5;

	if (hw_data->ulv_supported && table_info->us_ulv_voltage_offset) {
		result = polaris10_populate_ulv_state(hwmgr, table);
		PP_ASSERT_WITH_CODE(0 == result,
				"Failed to initialize ULV state!", return result);
		cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
				ixCG_ULV_PARAMETER, SMU7_CGULVPARAMETER_DFLT);
	}

	result = polaris10_populate_smc_link_level(hwmgr, table);
	PP_ASSERT_WITH_CODE(0 == result,
			"Failed to initialize Link Level!", return result);

	result = polaris10_populate_all_graphic_levels(hwmgr);
	PP_ASSERT_WITH_CODE(0 == result,
			"Failed to initialize Graphics Level!", return result);

	result = polaris10_populate_all_memory_levels(hwmgr);
	PP_ASSERT_WITH_CODE(0 == result,
			"Failed to initialize Memory Level!", return result);

	result = polaris10_populate_smc_acpi_level(hwmgr, table);
	PP_ASSERT_WITH_CODE(0 == result,
			"Failed to initialize ACPI Level!", return result);

	result = polaris10_populate_smc_vce_level(hwmgr, table);
	PP_ASSERT_WITH_CODE(0 == result,
			"Failed to initialize VCE Level!", return result);

	result = polaris10_populate_smc_samu_level(hwmgr, table);
	PP_ASSERT_WITH_CODE(0 == result,
			"Failed to initialize SAMU Level!", return result);

	/* Since only the initial state is completely set up at this point
	 * (the other states are just copies of the boot state) we only
	 * need to populate the  ARB settings for the initial state.
	 */
	result = polaris10_program_memory_timing_parameters(hwmgr);
	PP_ASSERT_WITH_CODE(0 == result,
			"Failed to Write ARB settings for the initial state.", return result);

	result = polaris10_populate_smc_uvd_level(hwmgr, table);
	PP_ASSERT_WITH_CODE(0 == result,
			"Failed to initialize UVD Level!", return result);

	result = polaris10_populate_smc_boot_level(hwmgr, table);
	PP_ASSERT_WITH_CODE(0 == result,
			"Failed to initialize Boot Level!", return result);

	result = polaris10_populate_smc_initailial_state(hwmgr);
	PP_ASSERT_WITH_CODE(0 == result,
			"Failed to initialize Boot State!", return result);

	result = polaris10_populate_bapm_parameters_in_dpm_table(hwmgr);
	PP_ASSERT_WITH_CODE(0 == result,
			"Failed to populate BAPM Parameters!", return result);

	polaris10_populate_zero_rpm_parameters(hwmgr);

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_ClockStretcher)) {
		result = polaris10_populate_clock_stretcher_data_table(hwmgr);
		PP_ASSERT_WITH_CODE(0 == result,
				"Failed to populate Clock Stretcher Data Table!",
				return result);
	}

	result = polaris10_populate_avfs_parameters(hwmgr);
	PP_ASSERT_WITH_CODE(0 == result, "Failed to populate AVFS Parameters!", return result;);

	table->CurrSclkPllRange = 0xff;
	table->GraphicsVoltageChangeEnable  = 1;
	table->GraphicsThermThrottleEnable  = 1;
	table->GraphicsInterval = 1;
	table->VoltageInterval  = 1;
	table->ThermalInterval  = 1;
	table->TemperatureLimitHigh =
			table_info->cac_dtp_table->usTargetOperatingTemp *
			SMU7_Q88_FORMAT_CONVERSION_UNIT;
	table->TemperatureLimitLow  =
			(table_info->cac_dtp_table->usTargetOperatingTemp - 1) *
			SMU7_Q88_FORMAT_CONVERSION_UNIT;
	table->MemoryVoltageChangeEnable = 1;
	table->MemoryInterval = 1;
	table->VoltageResponseTime = 0;
	table->PhaseResponseTime = 0;
	table->MemoryThermThrottleEnable = 1;
	table->PCIeBootLinkLevel = hw_data->dpm_table.pcie_speed_table.count;
	table->PCIeGenInterval = 1;
	table->VRConfig = 0;

	result = polaris10_populate_vr_config(hwmgr, table);
	PP_ASSERT_WITH_CODE(0 == result,
			"Failed to populate VRConfig setting!", return result);
	hw_data->vr_config = table->VRConfig;
	table->ThermGpio = 17;
	table->SclkStepSize = 0x4000;

	if (atomctrl_get_pp_assign_pin(hwmgr, VDDC_VRHOT_GPIO_PINID, &gpio_pin)) {
		table->VRHotGpio = gpio_pin.uc_gpio_pin_bit_shift;
		if (gpio_table)
			table->VRHotLevel = gpio_table->vrhot_triggered_sclk_dpm_index;
	} else {
		table->VRHotGpio = SMU7_UNUSED_GPIO_PIN;
		phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_RegulatorHot);
	}

	if (atomctrl_get_pp_assign_pin(hwmgr, PP_AC_DC_SWITCH_GPIO_PINID,
			&gpio_pin)) {
		table->AcDcGpio = gpio_pin.uc_gpio_pin_bit_shift;
		if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_AutomaticDCTransition) &&
		    !smum_send_msg_to_smc(hwmgr, PPSMC_MSG_UseNewGPIOScheme, NULL))
			phm_cap_set(hwmgr->platform_descriptor.platformCaps,
					PHM_PlatformCaps_SMCtoPPLIBAcdcGpioScheme);
	} else {
		table->AcDcGpio = SMU7_UNUSED_GPIO_PIN;
		phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_AutomaticDCTransition);
	}

	/* Thermal Output GPIO */
	if (atomctrl_get_pp_assign_pin(hwmgr, THERMAL_INT_OUTPUT_GPIO_PINID,
			&gpio_pin)) {
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_ThermalOutGPIO);

		table->ThermOutGpio = gpio_pin.uc_gpio_pin_bit_shift;

		/* For porlarity read GPIOPAD_A with assigned Gpio pin
		 * since VBIOS will program this register to set 'inactive state',
		 * driver can then determine 'active state' from this and
		 * program SMU with correct polarity
		 */
		table->ThermOutPolarity = (0 == (cgs_read_register(hwmgr->device, mmGPIOPAD_A)
					& (1 << gpio_pin.uc_gpio_pin_bit_shift))) ? 1:0;
		table->ThermOutMode = SMU7_THERM_OUT_MODE_THERM_ONLY;

		/* if required, combine VRHot/PCC with thermal out GPIO */
		if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_RegulatorHot)
		&& phm_cap_enabled(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_CombinePCCWithThermalSignal))
			table->ThermOutMode = SMU7_THERM_OUT_MODE_THERM_VRHOT;
	} else {
		table->ThermOutGpio = 17;
		table->ThermOutPolarity = 1;
		table->ThermOutMode = SMU7_THERM_OUT_MODE_DISABLE;
	}

	/* Populate BIF_SCLK levels into SMC DPM table */
	for (i = 0; i <= hw_data->dpm_table.pcie_speed_table.count; i++) {
		result = atomctrl_get_dfs_pll_dividers_vi(hwmgr, smu_data->bif_sclk_table[i], &dividers);
		PP_ASSERT_WITH_CODE((result == 0), "Can not find DFS divide id for Sclk", return result);

		if (i == 0)
			table->Ulv.BifSclkDfs = PP_HOST_TO_SMC_US((USHORT)(dividers.pll_post_divider));
		else
			table->LinkLevel[i-1].BifSclkDfs = PP_HOST_TO_SMC_US((USHORT)(dividers.pll_post_divider));
	}

	for (i = 0; i < SMU74_MAX_ENTRIES_SMIO; i++)
		table->Smio[i] = PP_HOST_TO_SMC_UL(table->Smio[i]);

	CONVERT_FROM_HOST_TO_SMC_UL(table->SystemFlags);
	CONVERT_FROM_HOST_TO_SMC_UL(table->VRConfig);
	CONVERT_FROM_HOST_TO_SMC_UL(table->SmioMask1);
	CONVERT_FROM_HOST_TO_SMC_UL(table->SmioMask2);
	CONVERT_FROM_HOST_TO_SMC_UL(table->SclkStepSize);
	CONVERT_FROM_HOST_TO_SMC_UL(table->CurrSclkPllRange);
	CONVERT_FROM_HOST_TO_SMC_US(table->TemperatureLimitHigh);
	CONVERT_FROM_HOST_TO_SMC_US(table->TemperatureLimitLow);
	CONVERT_FROM_HOST_TO_SMC_US(table->VoltageResponseTime);
	CONVERT_FROM_HOST_TO_SMC_US(table->PhaseResponseTime);

	/* Upload all dpm data to SMC memory.(dpm level, dpm level count etc) */
	result = smu7_copy_bytes_to_smc(hwmgr,
			smu_data->smu7_data.dpm_table_start +
			offsetof(SMU74_Discrete_DpmTable, SystemFlags),
			(uint8_t *)&(table->SystemFlags),
			sizeof(SMU74_Discrete_DpmTable) - 3 * sizeof(SMU74_PIDController),
			SMC_RAM_END);
	PP_ASSERT_WITH_CODE(0 == result,
			"Failed to upload dpm data to SMC memory!", return result);

	result = polaris10_populate_pm_fuses(hwmgr);
	PP_ASSERT_WITH_CODE(0 == result,
			"Failed to  populate PM fuses to SMC memory!", return result);

	return 0;
}

static int polaris10_program_mem_timing_parameters(struct pp_hwmgr *hwmgr)
{
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);

	if (data->need_update_smu7_dpm_table &
		(DPMTABLE_OD_UPDATE_SCLK + DPMTABLE_OD_UPDATE_MCLK))
		return polaris10_program_memory_timing_parameters(hwmgr);

	return 0;
}

static int polaris10_thermal_avfs_enable(struct pp_hwmgr *hwmgr)
{
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);

	if (!hwmgr->avfs_supported)
		return 0;

	smum_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_SetGBDroopSettings, data->avfs_vdroop_override_setting,
			NULL);

	smum_send_msg_to_smc(hwmgr, PPSMC_MSG_EnableAvfs, NULL);

	/* Apply avfs cks-off voltages to avoid the overshoot
	 * when switching to the highest sclk frequency
	 */
	if (data->apply_avfs_cks_off_voltage)
		smum_send_msg_to_smc(hwmgr, PPSMC_MSG_ApplyAvfsCksOffVoltage, NULL);

	return 0;
}

static int polaris10_thermal_setup_fan_table(struct pp_hwmgr *hwmgr)
{
	struct polaris10_smumgr *smu_data = (struct polaris10_smumgr *)(hwmgr->smu_backend);
	SMU74_Discrete_FanTable fan_table = { FDO_MODE_HARDWARE };
	uint32_t duty100;
	uint32_t t_diff1, t_diff2, pwm_diff1, pwm_diff2;
	uint16_t fdo_min, slope1, slope2;
	uint32_t reference_clock;
	int res;
	uint64_t tmp64;

	if (hwmgr->thermal_controller.fanInfo.bNoFan) {
		phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_MicrocodeFanControl);
		return 0;
	}

	if (smu_data->smu7_data.fan_table_start == 0) {
		phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_MicrocodeFanControl);
		return 0;
	}

	duty100 = PHM_READ_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
			CG_FDO_CTRL1, FMAX_DUTY100);

	if (duty100 == 0) {
		phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_MicrocodeFanControl);
		return 0;
	}

	/* use hardware fan control */
	if (hwmgr->thermal_controller.use_hw_fan_control)
		return 0;

	tmp64 = hwmgr->thermal_controller.advanceFanControlParameters.
			usPWMMin * duty100;
	do_div(tmp64, 10000);
	fdo_min = (uint16_t)tmp64;

	t_diff1 = hwmgr->thermal_controller.advanceFanControlParameters.usTMed -
			hwmgr->thermal_controller.advanceFanControlParameters.usTMin;
	t_diff2 = hwmgr->thermal_controller.advanceFanControlParameters.usTHigh -
			hwmgr->thermal_controller.advanceFanControlParameters.usTMed;

	pwm_diff1 = hwmgr->thermal_controller.advanceFanControlParameters.usPWMMed -
			hwmgr->thermal_controller.advanceFanControlParameters.usPWMMin;
	pwm_diff2 = hwmgr->thermal_controller.advanceFanControlParameters.usPWMHigh -
			hwmgr->thermal_controller.advanceFanControlParameters.usPWMMed;

	slope1 = (uint16_t)((50 + ((16 * duty100 * pwm_diff1) / t_diff1)) / 100);
	slope2 = (uint16_t)((50 + ((16 * duty100 * pwm_diff2) / t_diff2)) / 100);

	fan_table.TempMin = cpu_to_be16((50 + hwmgr->
			thermal_controller.advanceFanControlParameters.usTMin) / 100);
	fan_table.TempMed = cpu_to_be16((50 + hwmgr->
			thermal_controller.advanceFanControlParameters.usTMed) / 100);
	fan_table.TempMax = cpu_to_be16((50 + hwmgr->
			thermal_controller.advanceFanControlParameters.usTMax) / 100);

	fan_table.Slope1 = cpu_to_be16(slope1);
	fan_table.Slope2 = cpu_to_be16(slope2);

	fan_table.FdoMin = cpu_to_be16(fdo_min);

	fan_table.HystDown = cpu_to_be16(hwmgr->
			thermal_controller.advanceFanControlParameters.ucTHyst);

	fan_table.HystUp = cpu_to_be16(1);

	fan_table.HystSlope = cpu_to_be16(1);

	fan_table.TempRespLim = cpu_to_be16(5);

	reference_clock = amdgpu_asic_get_xclk((struct amdgpu_device *)hwmgr->adev);

	fan_table.RefreshPeriod = cpu_to_be32((hwmgr->
			thermal_controller.advanceFanControlParameters.ulCycleDelay *
			reference_clock) / 1600);

	fan_table.FdoMax = cpu_to_be16((uint16_t)duty100);

	fan_table.TempSrc = (uint8_t)PHM_READ_VFPF_INDIRECT_FIELD(
			hwmgr->device, CGS_IND_REG__SMC,
			CG_MULT_THERMAL_CTRL, TEMP_SEL);

	res = smu7_copy_bytes_to_smc(hwmgr, smu_data->smu7_data.fan_table_start,
			(uint8_t *)&fan_table, (uint32_t)sizeof(fan_table),
			SMC_RAM_END);

	if (!res && hwmgr->thermal_controller.
			advanceFanControlParameters.ucMinimumPWMLimit)
		res = smum_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_SetFanMinPwm,
				hwmgr->thermal_controller.
				advanceFanControlParameters.ucMinimumPWMLimit,
				NULL);

	if (!res && hwmgr->thermal_controller.
			advanceFanControlParameters.ulMinFanSCLKAcousticLimit)
		res = smum_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_SetFanSclkTarget,
				hwmgr->thermal_controller.
				advanceFanControlParameters.ulMinFanSCLKAcousticLimit,
				NULL);

	if (res)
		phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_MicrocodeFanControl);

	return 0;
}

static int polaris10_update_uvd_smc_table(struct pp_hwmgr *hwmgr)
{
	struct polaris10_smumgr *smu_data = (struct polaris10_smumgr *)(hwmgr->smu_backend);
	uint32_t mm_boot_level_offset, mm_boot_level_value;
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);

	smu_data->smc_state_table.UvdBootLevel = 0;
	if (table_info->mm_dep_table->count > 0)
		smu_data->smc_state_table.UvdBootLevel =
				(uint8_t) (table_info->mm_dep_table->count - 1);
	mm_boot_level_offset = smu_data->smu7_data.dpm_table_start + offsetof(SMU74_Discrete_DpmTable,
						UvdBootLevel);
	mm_boot_level_offset /= 4;
	mm_boot_level_offset *= 4;
	mm_boot_level_value = cgs_read_ind_register(hwmgr->device,
			CGS_IND_REG__SMC, mm_boot_level_offset);
	mm_boot_level_value &= 0x00FFFFFF;
	mm_boot_level_value |= smu_data->smc_state_table.UvdBootLevel << 24;
	cgs_write_ind_register(hwmgr->device,
			CGS_IND_REG__SMC, mm_boot_level_offset, mm_boot_level_value);

	if (!phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_UVDDPM) ||
		phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_StablePState))
		smum_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_UVDDPM_SetEnabledMask,
				(uint32_t)(1 << smu_data->smc_state_table.UvdBootLevel),
				NULL);
	return 0;
}

static int polaris10_update_vce_smc_table(struct pp_hwmgr *hwmgr)
{
	struct polaris10_smumgr *smu_data = (struct polaris10_smumgr *)(hwmgr->smu_backend);
	uint32_t mm_boot_level_offset, mm_boot_level_value;
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
					PHM_PlatformCaps_StablePState))
		smu_data->smc_state_table.VceBootLevel =
			(uint8_t) (table_info->mm_dep_table->count - 1);
	else
		smu_data->smc_state_table.VceBootLevel = 0;

	mm_boot_level_offset = smu_data->smu7_data.dpm_table_start +
					offsetof(SMU74_Discrete_DpmTable, VceBootLevel);
	mm_boot_level_offset /= 4;
	mm_boot_level_offset *= 4;
	mm_boot_level_value = cgs_read_ind_register(hwmgr->device,
			CGS_IND_REG__SMC, mm_boot_level_offset);
	mm_boot_level_value &= 0xFF00FFFF;
	mm_boot_level_value |= smu_data->smc_state_table.VceBootLevel << 16;
	cgs_write_ind_register(hwmgr->device,
			CGS_IND_REG__SMC, mm_boot_level_offset, mm_boot_level_value);

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_StablePState))
		smum_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_VCEDPM_SetEnabledMask,
				(uint32_t)1 << smu_data->smc_state_table.VceBootLevel,
				NULL);
	return 0;
}

static int polaris10_update_bif_smc_table(struct pp_hwmgr *hwmgr)
{
	struct polaris10_smumgr *smu_data = (struct polaris10_smumgr *)(hwmgr->smu_backend);
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	struct phm_ppt_v1_pcie_table *pcie_table = table_info->pcie_table;
	int max_entry, i;

	max_entry = (SMU74_MAX_LEVELS_LINK < pcie_table->count) ?
						SMU74_MAX_LEVELS_LINK :
						pcie_table->count;
	/* Setup BIF_SCLK levels */
	for (i = 0; i < max_entry; i++)
		smu_data->bif_sclk_table[i] = pcie_table->entries[i].pcie_sclk;
	return 0;
}

static int polaris10_update_smc_table(struct pp_hwmgr *hwmgr, uint32_t type)
{
	switch (type) {
	case SMU_UVD_TABLE:
		polaris10_update_uvd_smc_table(hwmgr);
		break;
	case SMU_VCE_TABLE:
		polaris10_update_vce_smc_table(hwmgr);
		break;
	case SMU_BIF_TABLE:
		polaris10_update_bif_smc_table(hwmgr);
		break;
	default:
		break;
	}
	return 0;
}

static int polaris10_update_sclk_threshold(struct pp_hwmgr *hwmgr)
{
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);
	struct polaris10_smumgr *smu_data = (struct polaris10_smumgr *)(hwmgr->smu_backend);

	int result = 0;
	uint32_t low_sclk_interrupt_threshold = 0;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_SclkThrottleLowNotification)
		&& (data->low_sclk_interrupt_threshold != 0)) {
		low_sclk_interrupt_threshold =
				data->low_sclk_interrupt_threshold;

		CONVERT_FROM_HOST_TO_SMC_UL(low_sclk_interrupt_threshold);

		result = smu7_copy_bytes_to_smc(
				hwmgr,
				smu_data->smu7_data.dpm_table_start +
				offsetof(SMU74_Discrete_DpmTable,
					LowSclkInterruptThreshold),
				(uint8_t *)&low_sclk_interrupt_threshold,
				sizeof(uint32_t),
				SMC_RAM_END);
	}
	PP_ASSERT_WITH_CODE((result == 0),
			"Failed to update SCLK threshold!", return result);

	result = polaris10_program_mem_timing_parameters(hwmgr);
	PP_ASSERT_WITH_CODE((result == 0),
			"Failed to program memory timing parameters!",
			);

	return result;
}

static uint32_t polaris10_get_offsetof(uint32_t type, uint32_t member)
{
	switch (type) {
	case SMU_SoftRegisters:
		switch (member) {
		case HandshakeDisables:
			return offsetof(SMU74_SoftRegisters, HandshakeDisables);
		case VoltageChangeTimeout:
			return offsetof(SMU74_SoftRegisters, VoltageChangeTimeout);
		case AverageGraphicsActivity:
			return offsetof(SMU74_SoftRegisters, AverageGraphicsActivity);
		case AverageMemoryActivity:
			return offsetof(SMU74_SoftRegisters, AverageMemoryActivity);
		case PreVBlankGap:
			return offsetof(SMU74_SoftRegisters, PreVBlankGap);
		case VBlankTimeout:
			return offsetof(SMU74_SoftRegisters, VBlankTimeout);
		case UcodeLoadStatus:
			return offsetof(SMU74_SoftRegisters, UcodeLoadStatus);
		case DRAM_LOG_ADDR_H:
			return offsetof(SMU74_SoftRegisters, DRAM_LOG_ADDR_H);
		case DRAM_LOG_ADDR_L:
			return offsetof(SMU74_SoftRegisters, DRAM_LOG_ADDR_L);
		case DRAM_LOG_PHY_ADDR_H:
			return offsetof(SMU74_SoftRegisters, DRAM_LOG_PHY_ADDR_H);
		case DRAM_LOG_PHY_ADDR_L:
			return offsetof(SMU74_SoftRegisters, DRAM_LOG_PHY_ADDR_L);
		case DRAM_LOG_BUFF_SIZE:
			return offsetof(SMU74_SoftRegisters, DRAM_LOG_BUFF_SIZE);
		}
		break;
	case SMU_Discrete_DpmTable:
		switch (member) {
		case UvdBootLevel:
			return offsetof(SMU74_Discrete_DpmTable, UvdBootLevel);
		case VceBootLevel:
			return offsetof(SMU74_Discrete_DpmTable, VceBootLevel);
		case LowSclkInterruptThreshold:
			return offsetof(SMU74_Discrete_DpmTable, LowSclkInterruptThreshold);
		}
		break;
	}
	pr_warn("can't get the offset of type %x member %x\n", type, member);
	return 0;
}

static uint32_t polaris10_get_mac_definition(uint32_t value)
{
	switch (value) {
	case SMU_MAX_LEVELS_GRAPHICS:
		return SMU74_MAX_LEVELS_GRAPHICS;
	case SMU_MAX_LEVELS_MEMORY:
		return SMU74_MAX_LEVELS_MEMORY;
	case SMU_MAX_LEVELS_LINK:
		return SMU74_MAX_LEVELS_LINK;
	case SMU_MAX_ENTRIES_SMIO:
		return SMU74_MAX_ENTRIES_SMIO;
	case SMU_MAX_LEVELS_VDDC:
		return SMU74_MAX_LEVELS_VDDC;
	case SMU_MAX_LEVELS_VDDGFX:
		return SMU74_MAX_LEVELS_VDDGFX;
	case SMU_MAX_LEVELS_VDDCI:
		return SMU74_MAX_LEVELS_VDDCI;
	case SMU_MAX_LEVELS_MVDD:
		return SMU74_MAX_LEVELS_MVDD;
	case SMU_UVD_MCLK_HANDSHAKE_DISABLE:
		return SMU7_UVD_MCLK_HANDSHAKE_DISABLE |
				SMU7_VCE_MCLK_HANDSHAKE_DISABLE;
	}

	pr_warn("can't get the mac of %x\n", value);
	return 0;
}

static int polaris10_process_firmware_header(struct pp_hwmgr *hwmgr)
{
	struct polaris10_smumgr *smu_data = (struct polaris10_smumgr *)(hwmgr->smu_backend);
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);
	uint32_t tmp;
	int result;
	bool error = false;

	result = smu7_read_smc_sram_dword(hwmgr,
			SMU7_FIRMWARE_HEADER_LOCATION +
			offsetof(SMU74_Firmware_Header, DpmTable),
			&tmp, SMC_RAM_END);

	if (0 == result)
		smu_data->smu7_data.dpm_table_start = tmp;

	error |= (0 != result);

	result = smu7_read_smc_sram_dword(hwmgr,
			SMU7_FIRMWARE_HEADER_LOCATION +
			offsetof(SMU74_Firmware_Header, SoftRegisters),
			&tmp, SMC_RAM_END);

	if (!result) {
		data->soft_regs_start = tmp;
		smu_data->smu7_data.soft_regs_start = tmp;
	}

	error |= (0 != result);

	result = smu7_read_smc_sram_dword(hwmgr,
			SMU7_FIRMWARE_HEADER_LOCATION +
			offsetof(SMU74_Firmware_Header, mcRegisterTable),
			&tmp, SMC_RAM_END);

	if (!result)
		smu_data->smu7_data.mc_reg_table_start = tmp;

	result = smu7_read_smc_sram_dword(hwmgr,
			SMU7_FIRMWARE_HEADER_LOCATION +
			offsetof(SMU74_Firmware_Header, FanTable),
			&tmp, SMC_RAM_END);

	if (!result)
		smu_data->smu7_data.fan_table_start = tmp;

	error |= (0 != result);

	result = smu7_read_smc_sram_dword(hwmgr,
			SMU7_FIRMWARE_HEADER_LOCATION +
			offsetof(SMU74_Firmware_Header, mcArbDramTimingTable),
			&tmp, SMC_RAM_END);

	if (!result)
		smu_data->smu7_data.arb_table_start = tmp;

	error |= (0 != result);

	result = smu7_read_smc_sram_dword(hwmgr,
			SMU7_FIRMWARE_HEADER_LOCATION +
			offsetof(SMU74_Firmware_Header, Version),
			&tmp, SMC_RAM_END);

	if (!result)
		hwmgr->microcode_version_info.SMC = tmp;

	error |= (0 != result);

	return error ? -1 : 0;
}

static uint8_t polaris10_get_memory_modile_index(struct pp_hwmgr *hwmgr)
{
	return (uint8_t) (0xFF & (cgs_read_register(hwmgr->device, mmBIOS_SCRATCH_4) >> 16));
}

static int polaris10_initialize_mc_reg_table(struct pp_hwmgr *hwmgr)
{
	struct polaris10_smumgr *smu_data = (struct polaris10_smumgr *)(hwmgr->smu_backend);
	pp_atomctrl_mc_reg_table *mc_reg_table = &smu_data->mc_reg_table;
	uint8_t module_index = polaris10_get_memory_modile_index(hwmgr);

	memset(mc_reg_table, 0, sizeof(pp_atomctrl_mc_reg_table));

	return atomctrl_initialize_mc_reg_table_v2_2(hwmgr, module_index, mc_reg_table);
}

static bool polaris10_is_dpm_running(struct pp_hwmgr *hwmgr)
{
	return (1 == PHM_READ_INDIRECT_FIELD(hwmgr->device,
			CGS_IND_REG__SMC, FEATURE_STATUS, VOLTAGE_CONTROLLER_ON))
			? true : false;
}

static int polaris10_update_dpm_settings(struct pp_hwmgr *hwmgr,
				void *profile_setting)
{
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);
	struct polaris10_smumgr *smu_data = (struct polaris10_smumgr *)
			(hwmgr->smu_backend);
	struct profile_mode_setting *setting;
	struct SMU74_Discrete_GraphicsLevel *levels =
			smu_data->smc_state_table.GraphicsLevel;
	uint32_t array = smu_data->smu7_data.dpm_table_start +
			offsetof(SMU74_Discrete_DpmTable, GraphicsLevel);

	uint32_t mclk_array = smu_data->smu7_data.dpm_table_start +
			offsetof(SMU74_Discrete_DpmTable, MemoryLevel);
	struct SMU74_Discrete_MemoryLevel *mclk_levels =
			smu_data->smc_state_table.MemoryLevel;
	uint32_t i;
	uint32_t offset, up_hyst_offset, down_hyst_offset, clk_activity_offset, tmp;

	if (profile_setting == NULL)
		return -EINVAL;

	setting = (struct profile_mode_setting *)profile_setting;

	if (setting->bupdate_sclk) {
		if (!data->sclk_dpm_key_disabled)
			smum_send_msg_to_smc(hwmgr, PPSMC_MSG_SCLKDPM_FreezeLevel, NULL);
		for (i = 0; i < smu_data->smc_state_table.GraphicsDpmLevelCount; i++) {
			if (levels[i].ActivityLevel !=
				cpu_to_be16(setting->sclk_activity)) {
				levels[i].ActivityLevel = cpu_to_be16(setting->sclk_activity);

				clk_activity_offset = array + (sizeof(SMU74_Discrete_GraphicsLevel) * i)
						+ offsetof(SMU74_Discrete_GraphicsLevel, ActivityLevel);
				offset = clk_activity_offset & ~0x3;
				tmp = PP_HOST_TO_SMC_UL(cgs_read_ind_register(hwmgr->device, CGS_IND_REG__SMC, offset));
				tmp = phm_set_field_to_u32(clk_activity_offset, tmp, levels[i].ActivityLevel, sizeof(uint16_t));
				cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC, offset, PP_HOST_TO_SMC_UL(tmp));

			}
			if (levels[i].UpHyst != setting->sclk_up_hyst ||
				levels[i].DownHyst != setting->sclk_down_hyst) {
				levels[i].UpHyst = setting->sclk_up_hyst;
				levels[i].DownHyst = setting->sclk_down_hyst;
				up_hyst_offset = array + (sizeof(SMU74_Discrete_GraphicsLevel) * i)
						+ offsetof(SMU74_Discrete_GraphicsLevel, UpHyst);
				down_hyst_offset = array + (sizeof(SMU74_Discrete_GraphicsLevel) * i)
						+ offsetof(SMU74_Discrete_GraphicsLevel, DownHyst);
				offset = up_hyst_offset & ~0x3;
				tmp = PP_HOST_TO_SMC_UL(cgs_read_ind_register(hwmgr->device, CGS_IND_REG__SMC, offset));
				tmp = phm_set_field_to_u32(up_hyst_offset, tmp, levels[i].UpHyst, sizeof(uint8_t));
				tmp = phm_set_field_to_u32(down_hyst_offset, tmp, levels[i].DownHyst, sizeof(uint8_t));
				cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC, offset, PP_HOST_TO_SMC_UL(tmp));
			}
		}
		if (!data->sclk_dpm_key_disabled)
			smum_send_msg_to_smc(hwmgr, PPSMC_MSG_SCLKDPM_UnfreezeLevel, NULL);
	}

	if (setting->bupdate_mclk) {
		if (!data->mclk_dpm_key_disabled)
			smum_send_msg_to_smc(hwmgr, PPSMC_MSG_MCLKDPM_FreezeLevel, NULL);
		for (i = 0; i < smu_data->smc_state_table.MemoryDpmLevelCount; i++) {
			if (mclk_levels[i].ActivityLevel !=
				cpu_to_be16(setting->mclk_activity)) {
				mclk_levels[i].ActivityLevel = cpu_to_be16(setting->mclk_activity);

				clk_activity_offset = mclk_array + (sizeof(SMU74_Discrete_MemoryLevel) * i)
						+ offsetof(SMU74_Discrete_MemoryLevel, ActivityLevel);
				offset = clk_activity_offset & ~0x3;
				tmp = PP_HOST_TO_SMC_UL(cgs_read_ind_register(hwmgr->device, CGS_IND_REG__SMC, offset));
				tmp = phm_set_field_to_u32(clk_activity_offset, tmp, mclk_levels[i].ActivityLevel, sizeof(uint16_t));
				cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC, offset, PP_HOST_TO_SMC_UL(tmp));

			}
			if (mclk_levels[i].UpHyst != setting->mclk_up_hyst ||
				mclk_levels[i].DownHyst != setting->mclk_down_hyst) {
				mclk_levels[i].UpHyst = setting->mclk_up_hyst;
				mclk_levels[i].DownHyst = setting->mclk_down_hyst;
				up_hyst_offset = mclk_array + (sizeof(SMU74_Discrete_MemoryLevel) * i)
						+ offsetof(SMU74_Discrete_MemoryLevel, UpHyst);
				down_hyst_offset = mclk_array + (sizeof(SMU74_Discrete_MemoryLevel) * i)
						+ offsetof(SMU74_Discrete_MemoryLevel, DownHyst);
				offset = up_hyst_offset & ~0x3;
				tmp = PP_HOST_TO_SMC_UL(cgs_read_ind_register(hwmgr->device, CGS_IND_REG__SMC, offset));
				tmp = phm_set_field_to_u32(up_hyst_offset, tmp, mclk_levels[i].UpHyst, sizeof(uint8_t));
				tmp = phm_set_field_to_u32(down_hyst_offset, tmp, mclk_levels[i].DownHyst, sizeof(uint8_t));
				cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC, offset, PP_HOST_TO_SMC_UL(tmp));
			}
		}
		if (!data->mclk_dpm_key_disabled)
			smum_send_msg_to_smc(hwmgr, PPSMC_MSG_MCLKDPM_UnfreezeLevel, NULL);
	}
	return 0;
}

const struct pp_smumgr_func polaris10_smu_funcs = {
	.name = "polaris10_smu",
	.smu_init = polaris10_smu_init,
	.smu_fini = smu7_smu_fini,
	.start_smu = polaris10_start_smu,
	.check_fw_load_finish = smu7_check_fw_load_finish,
	.request_smu_load_fw = smu7_reload_firmware,
	.request_smu_load_specific_fw = NULL,
	.send_msg_to_smc = smu7_send_msg_to_smc,
	.send_msg_to_smc_with_parameter = smu7_send_msg_to_smc_with_parameter,
	.get_argument = smu7_get_argument,
	.download_pptable_settings = NULL,
	.upload_pptable_settings = NULL,
	.update_smc_table = polaris10_update_smc_table,
	.get_offsetof = polaris10_get_offsetof,
	.process_firmware_header = polaris10_process_firmware_header,
	.init_smc_table = polaris10_init_smc_table,
	.update_sclk_threshold = polaris10_update_sclk_threshold,
	.thermal_avfs_enable = polaris10_thermal_avfs_enable,
	.thermal_setup_fan_table = polaris10_thermal_setup_fan_table,
	.populate_all_graphic_levels = polaris10_populate_all_graphic_levels,
	.populate_all_memory_levels = polaris10_populate_all_memory_levels,
	.get_mac_definition = polaris10_get_mac_definition,
	.initialize_mc_reg_table = polaris10_initialize_mc_reg_table,
	.is_dpm_running = polaris10_is_dpm_running,
	.is_hw_avfs_present = polaris10_is_hw_avfs_present,
	.update_dpm_settings = polaris10_update_dpm_settings,
};
