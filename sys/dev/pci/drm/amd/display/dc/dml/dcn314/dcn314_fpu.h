/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2022 Advanced Micro Devices, Inc.
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

#ifndef __DCN314_FPU_H__
#define __DCN314_FPU_H__

#define DCN3_14_DEFAULT_DET_SIZE 384
#define DCN3_14_MAX_DET_SIZE 384
#define DCN3_14_MIN_COMPBUF_SIZE_KB 128
#define DCN3_14_CRB_SEGMENT_SIZE_KB 64

void dcn314_update_bw_bounding_box_fpu(struct dc *dc, struct clk_bw_params *bw_params);
int dcn314_populate_dml_pipes_from_context_fpu(struct dc *dc, struct dc_state *context,
					       display_e2e_pipe_params_st *pipes,
					       bool fast_validate);

#endif
