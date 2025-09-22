/*
 * Copyright 2020 Advanced Micro Devices, Inc.
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

#ifndef __VG_CLK_MGR_H__
#define __VG_CLK_MGR_H__
#include "clk_mgr_internal.h"

struct watermarks;

extern struct wm_table ddr4_wm_table;
extern struct wm_table lpddr5_wm_table;

struct smu_watermark_set {
	struct watermarks *wm_set;
	union large_integer mc_address;
};

struct clk_mgr_vgh {
	struct clk_mgr_internal base;
	struct smu_watermark_set smu_wm_set;
};

void vg_clk_mgr_construct(struct dc_context *ctx,
		struct clk_mgr_vgh *clk_mgr,
		struct pp_smu_funcs *pp_smu,
		struct dccg *dccg);

void vg_clk_mgr_destroy(struct clk_mgr_internal *clk_mgr);

#endif //__VG_CLK_MGR_H__
