/*
* Copyright 2016 Advanced Micro Devices, Inc.
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

#ifndef __DC_HWSS_DCN21_H__
#define __DC_HWSS_DCN21_H__

#include "hw_sequencer_private.h"

struct dc;

int dcn21_init_sys_ctx(struct dce_hwseq *hws,
		struct dc *dc,
		struct dc_phy_addr_space_config *pa_config);

bool dcn21_s0i3_golden_init_wa(struct dc *dc);

void dcn21_exit_optimized_pwr_state(
		const struct dc *dc,
		struct dc_state *context);

void dcn21_optimize_pwr_state(
		const struct dc *dc,
		struct dc_state *context);

void dcn21_PLAT_58856_wa(struct dc_state *context,
		struct pipe_ctx *pipe_ctx);

void dcn21_set_pipe(struct pipe_ctx *pipe_ctx);
void dcn21_set_abm_immediate_disable(struct pipe_ctx *pipe_ctx);
bool dcn21_set_backlight_level(struct pipe_ctx *pipe_ctx,
		uint32_t backlight_pwm_u16_16,
		uint32_t frame_ramp);
bool dcn21_is_abm_supported(struct dc *dc,
		struct dc_state *context, struct dc_stream_state *stream);

#endif /* __DC_HWSS_DCN21_H__ */
