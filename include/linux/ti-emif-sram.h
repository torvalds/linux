/*
 * TI AM33XX EMIF Routines
 *
 * Copyright (C) 2016-2017 Texas Instruments Inc.
 *	Dave Gerlach
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef __LINUX_TI_EMIF_H
#define __LINUX_TI_EMIF_H

#include <linux/kbuild.h>
#include <linux/types.h>
#ifndef __ASSEMBLY__

struct emif_regs_amx3 {
	u32 emif_sdcfg_val;
	u32 emif_timing1_val;
	u32 emif_timing2_val;
	u32 emif_timing3_val;
	u32 emif_ref_ctrl_val;
	u32 emif_zqcfg_val;
	u32 emif_pmcr_val;
	u32 emif_pmcr_shdw_val;
	u32 emif_rd_wr_level_ramp_ctrl;
	u32 emif_rd_wr_exec_thresh;
	u32 emif_cos_config;
	u32 emif_priority_to_cos_mapping;
	u32 emif_connect_id_serv_1_map;
	u32 emif_connect_id_serv_2_map;
	u32 emif_ocp_config_val;
	u32 emif_lpddr2_nvm_tim;
	u32 emif_lpddr2_nvm_tim_shdw;
	u32 emif_dll_calib_ctrl_val;
	u32 emif_dll_calib_ctrl_val_shdw;
	u32 emif_ddr_phy_ctlr_1;
	u32 emif_ext_phy_ctrl_vals[120];
};

struct ti_emif_pm_data {
	void __iomem *ti_emif_base_addr_virt;
	phys_addr_t ti_emif_base_addr_phys;
	unsigned long ti_emif_sram_config;
	struct emif_regs_amx3 *regs_virt;
	phys_addr_t regs_phys;
} __packed __aligned(8);

struct ti_emif_pm_functions {
	u32 save_context;
	u32 restore_context;
	u32 run_hw_leveling;
	u32 enter_sr;
	u32 exit_sr;
	u32 abort_sr;
} __packed __aligned(8);

static inline void ti_emif_asm_offsets(void)
{
	DEFINE(EMIF_SDCFG_VAL_OFFSET,
	       offsetof(struct emif_regs_amx3, emif_sdcfg_val));
	DEFINE(EMIF_TIMING1_VAL_OFFSET,
	       offsetof(struct emif_regs_amx3, emif_timing1_val));
	DEFINE(EMIF_TIMING2_VAL_OFFSET,
	       offsetof(struct emif_regs_amx3, emif_timing2_val));
	DEFINE(EMIF_TIMING3_VAL_OFFSET,
	       offsetof(struct emif_regs_amx3, emif_timing3_val));
	DEFINE(EMIF_REF_CTRL_VAL_OFFSET,
	       offsetof(struct emif_regs_amx3, emif_ref_ctrl_val));
	DEFINE(EMIF_ZQCFG_VAL_OFFSET,
	       offsetof(struct emif_regs_amx3, emif_zqcfg_val));
	DEFINE(EMIF_PMCR_VAL_OFFSET,
	       offsetof(struct emif_regs_amx3, emif_pmcr_val));
	DEFINE(EMIF_PMCR_SHDW_VAL_OFFSET,
	       offsetof(struct emif_regs_amx3, emif_pmcr_shdw_val));
	DEFINE(EMIF_RD_WR_LEVEL_RAMP_CTRL_OFFSET,
	       offsetof(struct emif_regs_amx3, emif_rd_wr_level_ramp_ctrl));
	DEFINE(EMIF_RD_WR_EXEC_THRESH_OFFSET,
	       offsetof(struct emif_regs_amx3, emif_rd_wr_exec_thresh));
	DEFINE(EMIF_COS_CONFIG_OFFSET,
	       offsetof(struct emif_regs_amx3, emif_cos_config));
	DEFINE(EMIF_PRIORITY_TO_COS_MAPPING_OFFSET,
	       offsetof(struct emif_regs_amx3, emif_priority_to_cos_mapping));
	DEFINE(EMIF_CONNECT_ID_SERV_1_MAP_OFFSET,
	       offsetof(struct emif_regs_amx3, emif_connect_id_serv_1_map));
	DEFINE(EMIF_CONNECT_ID_SERV_2_MAP_OFFSET,
	       offsetof(struct emif_regs_amx3, emif_connect_id_serv_2_map));
	DEFINE(EMIF_OCP_CONFIG_VAL_OFFSET,
	       offsetof(struct emif_regs_amx3, emif_ocp_config_val));
	DEFINE(EMIF_LPDDR2_NVM_TIM_OFFSET,
	       offsetof(struct emif_regs_amx3, emif_lpddr2_nvm_tim));
	DEFINE(EMIF_LPDDR2_NVM_TIM_SHDW_OFFSET,
	       offsetof(struct emif_regs_amx3, emif_lpddr2_nvm_tim_shdw));
	DEFINE(EMIF_DLL_CALIB_CTRL_VAL_OFFSET,
	       offsetof(struct emif_regs_amx3, emif_dll_calib_ctrl_val));
	DEFINE(EMIF_DLL_CALIB_CTRL_VAL_SHDW_OFFSET,
	       offsetof(struct emif_regs_amx3, emif_dll_calib_ctrl_val_shdw));
	DEFINE(EMIF_DDR_PHY_CTLR_1_OFFSET,
	       offsetof(struct emif_regs_amx3, emif_ddr_phy_ctlr_1));
	DEFINE(EMIF_EXT_PHY_CTRL_VALS_OFFSET,
	       offsetof(struct emif_regs_amx3, emif_ext_phy_ctrl_vals));
	DEFINE(EMIF_REGS_AMX3_SIZE, sizeof(struct emif_regs_amx3));

	BLANK();

	DEFINE(EMIF_PM_BASE_ADDR_VIRT_OFFSET,
	       offsetof(struct ti_emif_pm_data, ti_emif_base_addr_virt));
	DEFINE(EMIF_PM_BASE_ADDR_PHYS_OFFSET,
	       offsetof(struct ti_emif_pm_data, ti_emif_base_addr_phys));
	DEFINE(EMIF_PM_CONFIG_OFFSET,
	       offsetof(struct ti_emif_pm_data, ti_emif_sram_config));
	DEFINE(EMIF_PM_REGS_VIRT_OFFSET,
	       offsetof(struct ti_emif_pm_data, regs_virt));
	DEFINE(EMIF_PM_REGS_PHYS_OFFSET,
	       offsetof(struct ti_emif_pm_data, regs_phys));
	DEFINE(EMIF_PM_DATA_SIZE, sizeof(struct ti_emif_pm_data));

	BLANK();

	DEFINE(EMIF_PM_SAVE_CONTEXT_OFFSET,
	       offsetof(struct ti_emif_pm_functions, save_context));
	DEFINE(EMIF_PM_RESTORE_CONTEXT_OFFSET,
	       offsetof(struct ti_emif_pm_functions, restore_context));
	DEFINE(EMIF_PM_RUN_HW_LEVELING,
	       offsetof(struct ti_emif_pm_functions, run_hw_leveling));
	DEFINE(EMIF_PM_ENTER_SR_OFFSET,
	       offsetof(struct ti_emif_pm_functions, enter_sr));
	DEFINE(EMIF_PM_EXIT_SR_OFFSET,
	       offsetof(struct ti_emif_pm_functions, exit_sr));
	DEFINE(EMIF_PM_ABORT_SR_OFFSET,
	       offsetof(struct ti_emif_pm_functions, abort_sr));
	DEFINE(EMIF_PM_FUNCTIONS_SIZE, sizeof(struct ti_emif_pm_functions));
}

struct gen_pool;

int ti_emif_copy_pm_function_table(struct gen_pool *sram_pool, void *dst);
int ti_emif_get_mem_type(void);

#endif
#endif /* __LINUX_TI_EMIF_H */
