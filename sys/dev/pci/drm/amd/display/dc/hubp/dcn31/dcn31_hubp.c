/*
 * Copyright 2012-20 Advanced Micro Devices, Inc.
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

#include "dm_services.h"
#include "dce_calcs.h"
#include "reg_helper.h"
#include "basics/conversion.h"
#include "dcn31_hubp.h"

#define REG(reg)\
	hubp2->hubp_regs->reg

#define CTX \
	hubp2->base.ctx

#undef FN
#define FN(reg_name, field_name) \
	hubp2->hubp_shift->field_name, hubp2->hubp_mask->field_name

void hubp31_set_unbounded_requesting(struct hubp *hubp, bool enable)
{
	struct dcn20_hubp *hubp2 = TO_DCN20_HUBP(hubp);

	REG_UPDATE(DCHUBP_CNTL, HUBP_UNBOUNDED_REQ_MODE, enable);
	REG_UPDATE(CURSOR_CONTROL, CURSOR_REQ_MODE, 1);
}

void hubp31_soft_reset(struct hubp *hubp, bool reset)
{
	struct dcn20_hubp *hubp2 = TO_DCN20_HUBP(hubp);

	REG_UPDATE(DCHUBP_CNTL, HUBP_SOFT_RESET, reset);
}

static void hubp31_program_extended_blank(struct hubp *hubp,
					  unsigned int min_dst_y_next_start_optimized)
{
	struct dcn20_hubp *hubp2 = TO_DCN20_HUBP(hubp);

	REG_UPDATE(BLANK_OFFSET_1, MIN_DST_Y_NEXT_START, min_dst_y_next_start_optimized);
}

void hubp31_program_extended_blank_value(
	struct hubp *hubp, unsigned int min_dst_y_next_start_optimized)
{
	hubp31_program_extended_blank(hubp, min_dst_y_next_start_optimized);
}

static struct hubp_funcs dcn31_hubp_funcs = {
	.hubp_enable_tripleBuffer = hubp2_enable_triplebuffer,
	.hubp_is_triplebuffer_enabled = hubp2_is_triplebuffer_enabled,
	.hubp_program_surface_flip_and_addr = hubp3_program_surface_flip_and_addr,
	.hubp_program_surface_config = hubp3_program_surface_config,
	.hubp_is_flip_pending = hubp2_is_flip_pending,
	.hubp_setup = hubp3_setup,
	.hubp_setup_interdependent = hubp2_setup_interdependent,
	.hubp_set_vm_system_aperture_settings = hubp3_set_vm_system_aperture_settings,
	.set_blank = hubp2_set_blank,
	.dcc_control = hubp3_dcc_control,
	.hubp_reset = hubp_reset,
	.mem_program_viewport = min_set_viewport,
	.set_cursor_attributes	= hubp2_cursor_set_attributes,
	.set_cursor_position	= hubp2_cursor_set_position,
	.hubp_clk_cntl = hubp2_clk_cntl,
	.hubp_vtg_sel = hubp2_vtg_sel,
	.dmdata_set_attributes = hubp3_dmdata_set_attributes,
	.dmdata_load = hubp2_dmdata_load,
	.dmdata_status_done = hubp2_dmdata_status_done,
	.hubp_read_state = hubp3_read_state,
	.hubp_clear_underflow = hubp2_clear_underflow,
	.hubp_set_flip_control_surface_gsl = hubp2_set_flip_control_surface_gsl,
	.hubp_init = hubp3_init,
	.set_unbounded_requesting = hubp31_set_unbounded_requesting,
	.hubp_soft_reset = hubp31_soft_reset,
	.hubp_set_flip_int = hubp1_set_flip_int,
	.hubp_in_blank = hubp1_in_blank,
	.program_extended_blank = hubp31_program_extended_blank,
};

bool hubp31_construct(
	struct dcn20_hubp *hubp2,
	struct dc_context *ctx,
	uint32_t inst,
	const struct dcn_hubp2_registers *hubp_regs,
	const struct dcn_hubp2_shift *hubp_shift,
	const struct dcn_hubp2_mask *hubp_mask)
{
	hubp2->base.funcs = &dcn31_hubp_funcs;
	hubp2->base.ctx = ctx;
	hubp2->hubp_regs = hubp_regs;
	hubp2->hubp_shift = hubp_shift;
	hubp2->hubp_mask = hubp_mask;
	hubp2->base.inst = inst;
	hubp2->base.opp_id = OPP_ID_INVALID;
	hubp2->base.mpcc_id = 0xf;

	return true;
}
