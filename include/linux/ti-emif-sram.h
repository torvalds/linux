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
	u32 enter_sr;
	u32 exit_sr;
	u32 abort_sr;
} __packed __aligned(8);

struct gen_pool;

int ti_emif_copy_pm_function_table(struct gen_pool *sram_pool, void *dst);
int ti_emif_get_mem_type(void);

#endif
#endif /* __LINUX_TI_EMIF_H */
