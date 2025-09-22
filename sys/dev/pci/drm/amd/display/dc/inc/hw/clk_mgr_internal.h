/*
 * Copyright 2018 Advanced Micro Devices, Inc.
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
 * Authors: AMD
 *
 */

#ifndef __DAL_CLK_MGR_INTERNAL_H__
#define __DAL_CLK_MGR_INTERNAL_H__

#include "clk_mgr.h"
#include "dc.h"

/*
 * only thing needed from here is MEMORY_TYPE_MULTIPLIER_CZ, which is also
 * used in resource, perhaps this should be defined somewhere more common.
 */
#include "resource.h"


/* Starting DID for each range */
enum dentist_base_divider_id {
	DENTIST_BASE_DID_1 = 0x08,
	DENTIST_BASE_DID_2 = 0x40,
	DENTIST_BASE_DID_3 = 0x60,
	DENTIST_BASE_DID_4 = 0x7e,
	DENTIST_MAX_DID = 0x7f
};

/* Starting point and step size for each divider range.*/
enum dentist_divider_range {
	DENTIST_DIVIDER_RANGE_1_START = 8,   /* 2.00  */
	DENTIST_DIVIDER_RANGE_1_STEP  = 1,   /* 0.25  */
	DENTIST_DIVIDER_RANGE_2_START = 64,  /* 16.00 */
	DENTIST_DIVIDER_RANGE_2_STEP  = 2,   /* 0.50  */
	DENTIST_DIVIDER_RANGE_3_START = 128, /* 32.00 */
	DENTIST_DIVIDER_RANGE_3_STEP  = 4,   /* 1.00  */
	DENTIST_DIVIDER_RANGE_4_START = 248, /* 62.00 */
	DENTIST_DIVIDER_RANGE_4_STEP  = 264, /* 66.00 */
	DENTIST_DIVIDER_RANGE_SCALE_FACTOR = 4
};

/*
 ***************************************************************************************
 ****************** Clock Manager Private Macros and Defines ***************************
 ***************************************************************************************
 */

/* Macros */

#define TO_CLK_MGR_INTERNAL(clk_mgr)\
	container_of(clk_mgr, struct clk_mgr_internal, base)

#define CTX \
	clk_mgr->base.ctx

#define DC_LOGGER \
	dc->ctx->logger




#define CLK_BASE(inst) \
	CLK_BASE_INNER(inst)

#define CLK_SRI(reg_name, block, inst)\
	.reg_name = CLK_BASE(mm ## block ## _ ## inst ## _ ## reg_name ## _BASE_IDX) + \
					mm ## block ## _ ## inst ## _ ## reg_name

#define CLK_COMMON_REG_LIST_DCE_BASE() \
	.DPREFCLK_CNTL = mmDPREFCLK_CNTL, \
	.DENTIST_DISPCLK_CNTL = mmDENTIST_DISPCLK_CNTL

#if defined(CONFIG_DRM_AMD_DC_SI)
#define CLK_COMMON_REG_LIST_DCE60_BASE() \
	SR(DENTIST_DISPCLK_CNTL)
#endif

#define CLK_COMMON_REG_LIST_DCN_BASE() \
	SR(DENTIST_DISPCLK_CNTL)

#define CLK_COMMON_REG_LIST_DCN_201() \
	SR(DENTIST_DISPCLK_CNTL), \
	CLK_SRI(CLK4_CLK_PLL_REQ, CLK4, 0), \
	CLK_SRI(CLK4_CLK2_CURRENT_CNT, CLK4, 0)

#define CLK_REG_LIST_NV10() \
	SR(DENTIST_DISPCLK_CNTL), \
	CLK_SRI(CLK3_CLK_PLL_REQ, CLK3, 0), \
	CLK_SRI(CLK3_CLK2_DFS_CNTL, CLK3, 0)

#define CLK_REG_LIST_DCN3()	  \
	SR(DENTIST_DISPCLK_CNTL), \
	CLK_SRI(CLK0_CLK_PLL_REQ,   CLK02, 0), \
	CLK_SRI(CLK0_CLK2_DFS_CNTL, CLK02, 0)

#define CLK_SF(reg_name, field_name, post_fix)\
	.field_name = reg_name ## __ ## field_name ## post_fix

#define CLK_COMMON_MASK_SH_LIST_DCE_COMMON_BASE(mask_sh) \
	CLK_SF(DPREFCLK_CNTL, DPREFCLK_SRC_SEL, mask_sh), \
	CLK_SF(DENTIST_DISPCLK_CNTL, DENTIST_DPREFCLK_WDIVIDER, mask_sh)

#if defined(CONFIG_DRM_AMD_DC_SI)
#define CLK_COMMON_MASK_SH_LIST_DCE60_COMMON_BASE(mask_sh) \
	CLK_SF(DENTIST_DISPCLK_CNTL, DENTIST_DISPCLK_WDIVIDER, mask_sh),\
	CLK_SF(DENTIST_DISPCLK_CNTL, DENTIST_DISPCLK_CHG_DONE, mask_sh)
#endif

#define CLK_COMMON_MASK_SH_LIST_DCN_COMMON_BASE(mask_sh) \
	CLK_SF(DENTIST_DISPCLK_CNTL, DENTIST_DISPCLK_WDIVIDER, mask_sh),\
	CLK_SF(DENTIST_DISPCLK_CNTL, DENTIST_DISPCLK_CHG_DONE, mask_sh)

#define CLK_MASK_SH_LIST_RV1(mask_sh) \
	CLK_COMMON_MASK_SH_LIST_DCN_COMMON_BASE(mask_sh),\
	CLK_SF(MP1_SMN_C2PMSG_67, CONTENT, mask_sh),\
	CLK_SF(MP1_SMN_C2PMSG_83, CONTENT, mask_sh),\
	CLK_SF(MP1_SMN_C2PMSG_91, CONTENT, mask_sh),

#define CLK_COMMON_MASK_SH_LIST_DCN20_BASE(mask_sh) \
	CLK_COMMON_MASK_SH_LIST_DCN_COMMON_BASE(mask_sh),\
	CLK_SF(DENTIST_DISPCLK_CNTL, DENTIST_DPPCLK_WDIVIDER, mask_sh),\
	CLK_SF(DENTIST_DISPCLK_CNTL, DENTIST_DPPCLK_CHG_DONE, mask_sh)

#define CLK_MASK_SH_LIST_NV10(mask_sh) \
	CLK_COMMON_MASK_SH_LIST_DCN20_BASE(mask_sh),\
	CLK_SF(CLK3_0_CLK3_CLK_PLL_REQ, FbMult_int, mask_sh),\
	CLK_SF(CLK3_0_CLK3_CLK_PLL_REQ, FbMult_frac, mask_sh)

#define CLK_COMMON_MASK_SH_LIST_DCN201_BASE(mask_sh) \
	CLK_COMMON_MASK_SH_LIST_DCN_COMMON_BASE(mask_sh),\
	CLK_SF(DENTIST_DISPCLK_CNTL, DENTIST_DPPCLK_WDIVIDER, mask_sh),\
	CLK_SF(DENTIST_DISPCLK_CNTL, DENTIST_DPPCLK_CHG_DONE, mask_sh),\
	CLK_SF(CLK4_0_CLK4_CLK_PLL_REQ, FbMult_int, mask_sh)

#define CLK_REG_LIST_DCN32()	  \
	SR(DENTIST_DISPCLK_CNTL), \
	CLK_SR_DCN32(CLK1_CLK_PLL_REQ), \
	CLK_SR_DCN32(CLK1_CLK0_DFS_CNTL), \
	CLK_SR_DCN32(CLK1_CLK1_DFS_CNTL), \
	CLK_SR_DCN32(CLK1_CLK2_DFS_CNTL), \
	CLK_SR_DCN32(CLK1_CLK3_DFS_CNTL), \
	CLK_SR_DCN32(CLK1_CLK4_DFS_CNTL), \
    CLK_SR_DCN32(CLK1_CLK0_CURRENT_CNT), \
    CLK_SR_DCN32(CLK1_CLK1_CURRENT_CNT), \
    CLK_SR_DCN32(CLK1_CLK2_CURRENT_CNT), \
    CLK_SR_DCN32(CLK1_CLK3_CURRENT_CNT), \
    CLK_SR_DCN32(CLK1_CLK4_CURRENT_CNT), \
    CLK_SR_DCN32(CLK4_CLK0_CURRENT_CNT)

#define CLK_REG_LIST_DCN35()	  \
	CLK_SR_DCN35(CLK1_CLK_PLL_REQ), \
	CLK_SR_DCN35(CLK1_CLK0_DFS_CNTL), \
	CLK_SR_DCN35(CLK1_CLK1_DFS_CNTL), \
	CLK_SR_DCN35(CLK1_CLK2_DFS_CNTL), \
	CLK_SR_DCN35(CLK1_CLK3_DFS_CNTL), \
	CLK_SR_DCN35(CLK1_CLK4_DFS_CNTL), \
	CLK_SR_DCN35(CLK1_CLK5_DFS_CNTL), \
	CLK_SR_DCN35(CLK1_CLK0_CURRENT_CNT), \
	CLK_SR_DCN35(CLK1_CLK1_CURRENT_CNT), \
	CLK_SR_DCN35(CLK1_CLK2_CURRENT_CNT), \
	CLK_SR_DCN35(CLK1_CLK3_CURRENT_CNT), \
	CLK_SR_DCN35(CLK1_CLK4_CURRENT_CNT), \
	CLK_SR_DCN35(CLK1_CLK5_CURRENT_CNT), \
	CLK_SR_DCN35(CLK1_CLK0_BYPASS_CNTL), \
	CLK_SR_DCN35(CLK1_CLK1_BYPASS_CNTL), \
	CLK_SR_DCN35(CLK1_CLK2_BYPASS_CNTL), \
	CLK_SR_DCN35(CLK1_CLK3_BYPASS_CNTL), \
	CLK_SR_DCN35(CLK1_CLK4_BYPASS_CNTL),\
	CLK_SR_DCN35(CLK1_CLK5_BYPASS_CNTL), \
	CLK_SR_DCN35(CLK1_CLK0_DS_CNTL), \
	CLK_SR_DCN35(CLK1_CLK1_DS_CNTL), \
	CLK_SR_DCN35(CLK1_CLK2_DS_CNTL), \
	CLK_SR_DCN35(CLK1_CLK3_DS_CNTL), \
	CLK_SR_DCN35(CLK1_CLK4_DS_CNTL), \
	CLK_SR_DCN35(CLK1_CLK5_DS_CNTL), \
	CLK_SR_DCN35(CLK1_CLK0_ALLOW_DS), \
	CLK_SR_DCN35(CLK1_CLK1_ALLOW_DS), \
	CLK_SR_DCN35(CLK1_CLK2_ALLOW_DS), \
	CLK_SR_DCN35(CLK1_CLK3_ALLOW_DS), \
	CLK_SR_DCN35(CLK1_CLK4_ALLOW_DS), \
	CLK_SR_DCN35(CLK1_CLK5_ALLOW_DS), \
	CLK_SR_DCN35(CLK5_spll_field_8), \
	SR(DENTIST_DISPCLK_CNTL), \

#define CLK_COMMON_MASK_SH_LIST_DCN32(mask_sh) \
	CLK_COMMON_MASK_SH_LIST_DCN20_BASE(mask_sh),\
	CLK_SF(CLK1_CLK_PLL_REQ, FbMult_int, mask_sh),\
	CLK_SF(CLK1_CLK_PLL_REQ, FbMult_frac, mask_sh)

#define CLK_REG_LIST_DCN321()	  \
	SR(DENTIST_DISPCLK_CNTL), \
	CLK_SR_DCN321(CLK0_CLK_PLL_REQ,   CLK01, 0), \
	CLK_SR_DCN321(CLK0_CLK0_DFS_CNTL, CLK01, 0), \
	CLK_SR_DCN321(CLK0_CLK1_DFS_CNTL, CLK01, 0), \
	CLK_SR_DCN321(CLK0_CLK2_DFS_CNTL, CLK01, 0), \
	CLK_SR_DCN321(CLK0_CLK3_DFS_CNTL, CLK01, 0), \
	CLK_SR_DCN321(CLK0_CLK4_DFS_CNTL, CLK01, 0)

#define CLK_COMMON_MASK_SH_LIST_DCN321(mask_sh) \
	CLK_COMMON_MASK_SH_LIST_DCN20_BASE(mask_sh),\
	CLK_SF(CLK0_CLK_PLL_REQ, FbMult_int, mask_sh),\
	CLK_SF(CLK0_CLK_PLL_REQ, FbMult_frac, mask_sh)

#define CLK_REG_LIST_DCN401()	  \
	SR(DENTIST_DISPCLK_CNTL), \
	CLK_SR_DCN401(CLK0_CLK_PLL_REQ,   CLK01, 0), \
	CLK_SR_DCN401(CLK0_CLK0_DFS_CNTL, CLK01, 0), \
	CLK_SR_DCN401(CLK0_CLK1_DFS_CNTL,  CLK01, 0), \
	CLK_SR_DCN401(CLK0_CLK2_DFS_CNTL,  CLK01, 0), \
	CLK_SR_DCN401(CLK0_CLK3_DFS_CNTL,  CLK01, 0), \
	CLK_SR_DCN401(CLK0_CLK4_DFS_CNTL,  CLK01, 0), \
	CLK_SR_DCN401(CLK2_CLK2_DFS_CNTL,  CLK20, 0)

#define CLK_COMMON_MASK_SH_LIST_DCN401(mask_sh) \
	CLK_COMMON_MASK_SH_LIST_DCN321(mask_sh)

#define CLK_REG_FIELD_LIST(type) \
	type DPREFCLK_SRC_SEL; \
	type DENTIST_DPREFCLK_WDIVIDER; \
	type DENTIST_DISPCLK_WDIVIDER; \
	type DENTIST_DISPCLK_CHG_DONE;

#define CLK20_REG_FIELD_LIST(type) \
	type DENTIST_DPPCLK_WDIVIDER; \
	type DENTIST_DPPCLK_CHG_DONE; \
	type FbMult_int; \
	type FbMult_frac;

/*
 ***************************************************************************************
 ****************** Clock Manager Private Structures ***********************************
 ***************************************************************************************
 */

struct clk_mgr_registers {
	uint32_t DPREFCLK_CNTL;
	uint32_t DENTIST_DISPCLK_CNTL;

	uint32_t CLK4_CLK2_CURRENT_CNT;
	uint32_t CLK4_CLK_PLL_REQ;

	uint32_t CLK4_CLK0_CURRENT_CNT;

	uint32_t CLK3_CLK2_DFS_CNTL;
	uint32_t CLK3_CLK_PLL_REQ;

	uint32_t CLK0_CLK2_DFS_CNTL;
	uint32_t CLK0_CLK_PLL_REQ;

	uint32_t CLK1_CLK_PLL_REQ;
	uint32_t CLK1_CLK0_DFS_CNTL;
	uint32_t CLK1_CLK1_DFS_CNTL;
	uint32_t CLK1_CLK2_DFS_CNTL;
	uint32_t CLK1_CLK3_DFS_CNTL;
	uint32_t CLK1_CLK4_DFS_CNTL;
	uint32_t CLK1_CLK5_DFS_CNTL;
	uint32_t CLK2_CLK2_DFS_CNTL;

	uint32_t CLK1_CLK0_CURRENT_CNT;
    uint32_t CLK1_CLK1_CURRENT_CNT;
    uint32_t CLK1_CLK2_CURRENT_CNT;
    uint32_t CLK1_CLK3_CURRENT_CNT;
    uint32_t CLK1_CLK4_CURRENT_CNT;
	uint32_t CLK1_CLK5_CURRENT_CNT;

	uint32_t CLK0_CLK0_DFS_CNTL;
	uint32_t CLK0_CLK1_DFS_CNTL;
	uint32_t CLK0_CLK3_DFS_CNTL;
	uint32_t CLK0_CLK4_DFS_CNTL;
	uint32_t CLK1_CLK0_BYPASS_CNTL;
	uint32_t CLK1_CLK1_BYPASS_CNTL;
	uint32_t CLK1_CLK2_BYPASS_CNTL;
	uint32_t CLK1_CLK3_BYPASS_CNTL;
	uint32_t CLK1_CLK4_BYPASS_CNTL;
	uint32_t CLK1_CLK5_BYPASS_CNTL;

	uint32_t CLK1_CLK0_DS_CNTL;
	uint32_t CLK1_CLK1_DS_CNTL;
	uint32_t CLK1_CLK2_DS_CNTL;
	uint32_t CLK1_CLK3_DS_CNTL;
	uint32_t CLK1_CLK4_DS_CNTL;
	uint32_t CLK1_CLK5_DS_CNTL;

	uint32_t CLK1_CLK0_ALLOW_DS;
	uint32_t CLK1_CLK1_ALLOW_DS;
	uint32_t CLK1_CLK2_ALLOW_DS;
	uint32_t CLK1_CLK3_ALLOW_DS;
	uint32_t CLK1_CLK4_ALLOW_DS;
	uint32_t CLK1_CLK5_ALLOW_DS;
	uint32_t CLK5_spll_field_8;

};

struct clk_mgr_shift {
	CLK_REG_FIELD_LIST(uint8_t)
	CLK20_REG_FIELD_LIST(uint8_t)
};

struct clk_mgr_mask {
	CLK_REG_FIELD_LIST(uint32_t)
	CLK20_REG_FIELD_LIST(uint32_t)
};

enum clock_type {
	clock_type_dispclk = 1,
	clock_type_dcfclk,
	clock_type_socclk,
	clock_type_pixelclk,
	clock_type_phyclk,
	clock_type_dppclk,
	clock_type_fclk,
	clock_type_dcfdsclk,
	clock_type_dscclk,
	clock_type_uclk,
	clock_type_dramclk,
};


struct state_dependent_clocks {
	int display_clk_khz;
	int pixel_clk_khz;
};

struct clk_mgr_internal {
	struct clk_mgr base;
	int smu_ver;
	struct pp_smu_funcs *pp_smu;
	struct clk_mgr_internal_funcs *funcs;

	struct dccg *dccg;

	/*
	 * For backwards compatbility with previous implementation
	 * TODO: remove these after everything transitions to new pattern
	 * Rationale is that clk registers change a lot across DCE versions
	 * and a shared data structure doesn't really make sense.
	 */
	const struct clk_mgr_registers *regs;
	const struct clk_mgr_shift *clk_mgr_shift;
	const struct clk_mgr_mask *clk_mgr_mask;

	struct state_dependent_clocks max_clks_by_state[DM_PP_CLOCKS_MAX_STATES];

	/*TODO: figure out which of the below fields should be here vs in asic specific portion */
	/* Cache the status of DFS-bypass feature*/
	bool dfs_bypass_enabled;
	/* True if the DFS-bypass feature is enabled and active. */
	bool dfs_bypass_active;

	uint32_t dfs_ref_freq_khz;
	/*
	 * Cache the display clock returned by VBIOS if DFS-bypass is enabled.
	 * This is basically "Crystal Frequency In KHz" (XTALIN) frequency
	 */
	int dfs_bypass_disp_clk;

	/**
	 * @ss_on_dprefclk:
	 *
	 * True if spread spectrum is enabled on the DP ref clock.
	 */
	bool ss_on_dprefclk;

	/**
	 * @xgmi_enabled:
	 *
	 * True if xGMI is enabled. On VG20, both audio and display clocks need
	 * to be adjusted with the WAFL link's SS info if xGMI is enabled.
	 */
	bool xgmi_enabled;

	/**
	 * @dprefclk_ss_percentage:
	 *
	 * DPREFCLK SS percentage (if down-spread enabled).
	 *
	 * Note that if XGMI is enabled, the SS info (percentage and divider)
	 * from the WAFL link is used instead. This is decided during
	 * dce_clk_mgr initialization.
	 */
	int dprefclk_ss_percentage;

	/**
	 * @dprefclk_ss_divider:
	 *
	 * DPREFCLK SS percentage Divider (100 or 1000).
	 */
	int dprefclk_ss_divider;

	enum dm_pp_clocks_state max_clks_state;
	enum dm_pp_clocks_state cur_min_clks_state;
	bool periodic_retraining_disabled;

	unsigned int cur_phyclk_req_table[MAX_LINKS];

	bool smu_present;
	void *wm_range_table;
	long long wm_range_table_addr;

	bool dpm_present;
	bool pme_trigger_pending;
};

struct clk_mgr_internal_funcs {
	int (*set_dispclk)(struct clk_mgr_internal *clk_mgr, int requested_dispclk_khz);
	int (*set_dprefclk)(struct clk_mgr_internal *clk_mgr);
};


/*
 ***************************************************************************************
 ****************** Clock Manager Level Helper functions *******************************
 ***************************************************************************************
 */


static inline bool should_set_clock(bool safe_to_lower, int calc_clk, int cur_clk)
{
	return ((safe_to_lower && calc_clk < cur_clk) || calc_clk > cur_clk);
}

static inline bool should_update_pstate_support(bool safe_to_lower, bool calc_support, bool cur_support)
{
	if (cur_support != calc_support) {
		if (calc_support && safe_to_lower)
			return true;
		else if (!calc_support && !safe_to_lower)
			return true;
	}

	return false;
}

static inline int khz_to_mhz_ceil(int khz)
{
	return (khz + 999) / 1000;
}

static inline int khz_to_mhz_floor(int khz)
{
	return khz / 1000;
}

int clk_mgr_helper_get_active_display_cnt(
		struct dc *dc,
		struct dc_state *context);

int clk_mgr_helper_get_active_plane_cnt(
		struct dc *dc,
		struct dc_state *context);



#endif //__DAL_CLK_MGR_INTERNAL_H__
