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

#ifndef __RN_CLK_MGR_H__
#define __RN_CLK_MGR_H__

#include "clk_mgr.h"
#include "dm_pp_smu.h"
#include "clk_mgr_internal.h"

extern struct wm_table ddr4_wm_table_gs;
extern struct wm_table lpddr4_wm_table_gs;
extern struct wm_table lpddr4_wm_table_with_disabled_ppt;
extern struct wm_table ddr4_wm_table_rn;
extern struct wm_table ddr4_1R_wm_table_rn;
extern struct wm_table lpddr4_wm_table_rn;

struct rn_clk_registers {
	uint32_t CLK1_CLK0_CURRENT_CNT; /* DPREFCLK */
};

void rn_clk_mgr_construct(struct dc_context *ctx,
		struct clk_mgr_internal *clk_mgr,
		struct pp_smu_funcs *pp_smu,
		struct dccg *dccg);

#endif //__RN_CLK_MGR_H__
