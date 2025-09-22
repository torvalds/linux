// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#include "dcn401_optc.h"
#include "dcn30/dcn30_optc.h"
#include "dcn31/dcn31_optc.h"
#include "dcn32/dcn32_optc.h"
#include "reg_helper.h"
#include "dc.h"
#include "dcn_calc_math.h"
#include "dc_dmub_srv.h"

#define REG(reg)\
	optc1->tg_regs->reg

#define CTX \
	optc1->base.ctx

#undef FN
#define FN(reg_name, field_name) \
	optc1->tg_shift->field_name, optc1->tg_mask->field_name

/*
 * OPTC uses ODM_MEM sub block to merge pixel data coming from different OPPs
 * into unified memory location per horizontal line. ODM_MEM contains shared
 * memory resources global to the ASIC. Each memory resource is capable of
 * storing 2048 pixels independent from actual pixel data size. Total number of
 * memory allocated must be even. The memory resource allocation is described in
 * a memory bit map per OPTC instance. Driver has to make sure that there is no
 * double allocation across different OPTC instances. Bit offset in the map
 * represents memory instance id. Driver allocates a memory instance to the
 * current OPTC by setting the bit with offset associated with the desired
 * memory instance to 1 in the current OPTC memory map register.
 *
 * It is upto software to decide how to allocate the shared memory resources
 * across different OPTC instances. Driver understands that the total number
 * of memory available is always 2 times the max number of OPP pipes. So each
 * OPP pipe can be mapped 2 pieces of memory. However there exists cases such as
 * 11520x2160 which could use 6 pieces of memory for 2 OPP pipes i.e. 3 pieces
 * for each OPP pipe.
 *
 * Driver will reserve the first and second preferred memory instances for each
 * OPP pipe. For example, OPP0's first and second preferred memory is ODM_MEM0
 * and ODM_MEM1. OPP1's first and second preferred memory is  ODM_MEM2 and
 * ODM_MEM3 so on so forth.
 *
 * Driver will first allocate from first preferred memory instances associated
 * with current OPP pipes in use. If needed driver will then allocate from
 * second preferred memory instances associated with current OPP pipes in use.
 * Finally if still needed, driver will allocate from second preferred memory
 * instances not associated with current OPP pipes. So if memory instances are
 * enough other OPTCs can still allocate from their OPPs' first preferred memory
 * instances without worrying about double allocation.
 */

static uint32_t decide_odm_mem_bit_map(int *opp_id, int opp_cnt, int h_active)
{
	bool first_preferred_memory_for_opp[MAX_PIPES] = {0};
	bool second_preferred_memory_for_opp[MAX_PIPES] = {0};
	uint32_t memory_bit_map = 0;
	int total_required = ((h_active + 4095) / 4096) * 2;
	int total_allocated = 0;
	int i;

	for (i = 0; i < opp_cnt; i++) {
		first_preferred_memory_for_opp[opp_id[i]] = true;
		total_allocated++;
		if (total_required == total_allocated)
			break;
	}

	if (total_required > total_allocated) {
		for (i = 0; i < opp_cnt; i++) {
			second_preferred_memory_for_opp[opp_id[i]] = true;
			total_allocated++;
			if (total_required == total_allocated)
				break;
		}
	}

	if (total_required > total_allocated) {
		for (i = 0; i < MAX_PIPES; i++) {
			if (second_preferred_memory_for_opp[i] == false) {
				second_preferred_memory_for_opp[i] = true;
				total_allocated++;
				if (total_required == total_allocated)
					break;
			}
		}
	}
	ASSERT(total_required == total_allocated);

	for (i = 0; i < MAX_PIPES; i++) {
		if (first_preferred_memory_for_opp[i])
			memory_bit_map |= 0x1 << (i * 2);
		if (second_preferred_memory_for_opp[i])
			memory_bit_map |= 0x2 << (i * 2);
	}

	return memory_bit_map;
}

static void optc401_set_odm_combine(struct timing_generator *optc, int *opp_id,
		int opp_cnt, int segment_width, int last_segment_width)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);
	uint32_t h_active = segment_width * (opp_cnt - 1) + last_segment_width;
	uint32_t odm_mem_bit_map = decide_odm_mem_bit_map(
			opp_id, opp_cnt, h_active);

	REG_SET(OPTC_MEMORY_CONFIG, 0,
		OPTC_MEM_SEL, odm_mem_bit_map);

	switch (opp_cnt) {
	case 2: /* ODM Combine 2:1 */
		REG_SET_3(OPTC_DATA_SOURCE_SELECT, 0,
				OPTC_NUM_OF_INPUT_SEGMENT, 1,
				OPTC_SEG0_SRC_SEL, opp_id[0],
				OPTC_SEG1_SRC_SEL, opp_id[1]);
		REG_UPDATE(OPTC_WIDTH_CONTROL,
					OPTC_SEGMENT_WIDTH, segment_width);

		REG_UPDATE(OTG_H_TIMING_CNTL,
				OTG_H_TIMING_DIV_MODE, H_TIMING_DIV_BY2);
		break;
	case 3: /* ODM Combine 3:1 */
		REG_SET_4(OPTC_DATA_SOURCE_SELECT, 0,
				OPTC_NUM_OF_INPUT_SEGMENT, 2,
				OPTC_SEG0_SRC_SEL, opp_id[0],
				OPTC_SEG1_SRC_SEL, opp_id[1],
				OPTC_SEG2_SRC_SEL, opp_id[2]);
		REG_UPDATE(OPTC_WIDTH_CONTROL,
				OPTC_SEGMENT_WIDTH, segment_width);
		REG_UPDATE(OPTC_WIDTH_CONTROL2,
				OPTC_SEGMENT_WIDTH_LAST,
				last_segment_width);
		/* In ODM combine 3:1 mode ODM packs 4 pixels per data transfer
		 * so OTG_H_TIMING_DIV_MODE should be configured to
		 * H_TIMING_DIV_BY4 even though ODM combines 3 OPP inputs, it
		 * outputs 4 pixels from single OPP at a time.
		 */
		REG_UPDATE(OTG_H_TIMING_CNTL,
				OTG_H_TIMING_DIV_MODE, H_TIMING_DIV_BY4);
		break;
	case 4: /* ODM Combine 4:1 */
		REG_SET_5(OPTC_DATA_SOURCE_SELECT, 0,
				OPTC_NUM_OF_INPUT_SEGMENT, 3,
				OPTC_SEG0_SRC_SEL, opp_id[0],
				OPTC_SEG1_SRC_SEL, opp_id[1],
				OPTC_SEG2_SRC_SEL, opp_id[2],
				OPTC_SEG3_SRC_SEL, opp_id[3]);
		REG_UPDATE(OPTC_WIDTH_CONTROL,
					OPTC_SEGMENT_WIDTH, segment_width);
		REG_UPDATE(OTG_H_TIMING_CNTL,
				OTG_H_TIMING_DIV_MODE, H_TIMING_DIV_BY4);
		break;
	default:
		ASSERT(false);
	}
;
	optc1->opp_count = opp_cnt;
}

static void optc401_set_h_timing_div_manual_mode(struct timing_generator *optc, bool manual_mode)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	REG_UPDATE(OTG_H_TIMING_CNTL,
			OTG_H_TIMING_DIV_MODE_MANUAL, manual_mode ? 1 : 0);
}
/**
 * optc401_enable_crtc() - Enable CRTC
 * @optc: Pointer to the timing generator structure
 *
 * This function calls ASIC Control Object to enable Timing generator.
 *
 * Return: Always returns true
 */
static bool optc401_enable_crtc(struct timing_generator *optc)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	/* opp instance for OTG, 1 to 1 mapping and odm will adjust */
	REG_UPDATE(OPTC_DATA_SOURCE_SELECT,
			OPTC_SEG0_SRC_SEL, optc->inst);

	/* VTG enable first is for HW workaround */
	REG_UPDATE(CONTROL,
			VTG0_ENABLE, 1);

	REG_SEQ_START();

	/* Enable CRTC */
	REG_UPDATE_2(OTG_CONTROL,
			OTG_DISABLE_POINT_CNTL, 2,
			OTG_MASTER_EN, 1);

	REG_SEQ_SUBMIT();
	REG_SEQ_WAIT_DONE();

	return true;
}

/* disable_crtc */
static bool optc401_disable_crtc(struct timing_generator *optc)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	REG_UPDATE_5(OPTC_DATA_SOURCE_SELECT,
			OPTC_SEG0_SRC_SEL, 0xf,
			OPTC_SEG1_SRC_SEL, 0xf,
			OPTC_SEG2_SRC_SEL, 0xf,
			OPTC_SEG3_SRC_SEL, 0xf,
			OPTC_NUM_OF_INPUT_SEGMENT, 0);

	REG_UPDATE(OPTC_MEMORY_CONFIG,
			OPTC_MEM_SEL, 0);

	/* disable otg request until end of the first line
	 * in the vertical blank region
	 */
	REG_UPDATE(OTG_CONTROL,
			OTG_MASTER_EN, 0);

	REG_UPDATE(CONTROL,
			VTG0_ENABLE, 0);

	/* CRTC disabled, so disable  clock. */
	REG_WAIT(OTG_CLOCK_CONTROL,
			OTG_BUSY, 0,
			1, 150000);

	return true;
}

static void optc401_phantom_crtc_post_enable(struct timing_generator *optc)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	/* Disable immediately. */
	REG_UPDATE_2(OTG_CONTROL, OTG_DISABLE_POINT_CNTL, 0, OTG_MASTER_EN, 0);

	/* CRTC disabled, so disable  clock. */
	REG_WAIT(OTG_CLOCK_CONTROL, OTG_BUSY, 0, 1, 100000);
}

static void optc401_disable_phantom_otg(struct timing_generator *optc)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	REG_UPDATE_5(OPTC_DATA_SOURCE_SELECT,
			OPTC_SEG0_SRC_SEL, 0xf,
			OPTC_SEG1_SRC_SEL, 0xf,
			OPTC_SEG2_SRC_SEL, 0xf,
			OPTC_SEG3_SRC_SEL, 0xf,
			OPTC_NUM_OF_INPUT_SEGMENT, 0);

	REG_UPDATE(OTG_CONTROL, OTG_MASTER_EN, 0);
}

static void optc401_set_odm_bypass(struct timing_generator *optc,
		const struct dc_crtc_timing *dc_crtc_timing)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);
	enum h_timing_div_mode h_div = H_TIMING_NO_DIV;

	REG_SET_5(OPTC_DATA_SOURCE_SELECT, 0,
			OPTC_NUM_OF_INPUT_SEGMENT, 0,
			OPTC_SEG0_SRC_SEL, optc->inst,
			OPTC_SEG1_SRC_SEL, 0xf,
			OPTC_SEG2_SRC_SEL, 0xf,
			OPTC_SEG3_SRC_SEL, 0xf
			);

	h_div = optc->funcs->is_two_pixels_per_container(dc_crtc_timing);
	REG_UPDATE(OTG_H_TIMING_CNTL,
			OTG_H_TIMING_DIV_MODE, h_div);

	REG_SET(OPTC_MEMORY_CONFIG, 0,
			OPTC_MEM_SEL, 0);
	optc1->opp_count = 1;
}

/* only to be used when FAMS2 is disabled or unsupported */
void optc401_setup_manual_trigger(struct timing_generator *optc)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);
	struct dc *dc = optc->ctx->dc;

	if (dc->caps.dmub_caps.fams_ver == 1 && !dc->debug.disable_fams)
		/* FAMS */
		dc_dmub_srv_set_drr_manual_trigger_cmd(dc, optc->inst);
	else {
		/*
		 * MIN_MASK_EN is gone and MASK is now always enabled.
		 *
		 * To get it to it work with manual trigger we need to make sure
		 * we program the correct bit.
		 */
		REG_UPDATE_4(OTG_V_TOTAL_CONTROL,
				OTG_V_TOTAL_MIN_SEL, 1,
				OTG_V_TOTAL_MAX_SEL, 1,
				OTG_FORCE_LOCK_ON_EVENT, 0,
				OTG_SET_V_TOTAL_MIN_MASK, (1 << 1)); /* TRIGA */
	}
}

void optc401_set_drr(
	struct timing_generator *optc,
	const struct drr_params *params)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);
	struct dc *dc = optc->ctx->dc;
	struct drr_params amended_params = { 0 };
	bool program_manual_trigger = false;

	if (dc->caps.dmub_caps.fams_ver >= 2 && dc->debug.fams2_config.bits.enable) {
		if (params != NULL &&
				params->vertical_total_max > 0 &&
				params->vertical_total_min > 0) {
			amended_params.vertical_total_max = params->vertical_total_max - 1;
			amended_params.vertical_total_min = params->vertical_total_min - 1;
			if (params->vertical_total_mid != 0) {
				amended_params.vertical_total_mid = params->vertical_total_mid - 1;
				amended_params.vertical_total_mid_frame_num = params->vertical_total_mid_frame_num;
			}
			program_manual_trigger = true;
		}

		dc_dmub_srv_fams2_drr_update(dc, optc->inst,
				amended_params.vertical_total_min,
				amended_params.vertical_total_max,
				amended_params.vertical_total_mid,
				amended_params.vertical_total_mid_frame_num,
				program_manual_trigger);
	} else {
		if (params != NULL &&
			params->vertical_total_max > 0 &&
			params->vertical_total_min > 0) {

			if (params->vertical_total_mid != 0) {

				REG_SET(OTG_V_TOTAL_MID, 0,
					OTG_V_TOTAL_MID, params->vertical_total_mid - 1);

				REG_UPDATE_2(OTG_V_TOTAL_CONTROL,
						OTG_VTOTAL_MID_REPLACING_MAX_EN, 1,
						OTG_VTOTAL_MID_FRAME_NUM,
						(uint8_t)params->vertical_total_mid_frame_num);

			}

			optc->funcs->set_vtotal_min_max(optc, params->vertical_total_min - 1, params->vertical_total_max - 1);
			optc401_setup_manual_trigger(optc);
		} else {
			REG_UPDATE_4(OTG_V_TOTAL_CONTROL,
					OTG_SET_V_TOTAL_MIN_MASK, 0,
					OTG_V_TOTAL_MIN_SEL, 0,
					OTG_V_TOTAL_MAX_SEL, 0,
					OTG_FORCE_LOCK_ON_EVENT, 0);

			optc->funcs->set_vtotal_min_max(optc, 0, 0);
		}
	}
}

static void optc401_set_out_mux(struct timing_generator *optc, enum otg_out_mux_dest dest)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	/* 00 - OTG_CONTROL_OTG_OUT_MUX_0 : Connects to DIO.
	   01 - OTG_CONTROL_OTG_OUT_MUX_1 : Reserved.
	   02 - OTG_CONTROL_OTG_OUT_MUX_2 : Connects to HPO.
	*/
	REG_UPDATE(OTG_CONTROL, OTG_OUT_MUX, dest);
}

void optc401_set_vtotal_min_max(struct timing_generator *optc, int vtotal_min, int vtotal_max)
{
	struct dc *dc = optc->ctx->dc;

	if (dc->caps.dmub_caps.fams_ver >= 2 && dc->debug.fams2_config.bits.enable) {
		/* FAMS2 */
		dc_dmub_srv_fams2_drr_update(dc, optc->inst,
				vtotal_min,
				vtotal_max,
				0,
				0,
				false);
	} else if (dc->caps.dmub_caps.fams_ver == 1 && !dc->debug.disable_fams) {
		/* FAMS */
		dc_dmub_srv_drr_update_cmd(dc, optc->inst, vtotal_min, vtotal_max);
	} else {
		optc1_set_vtotal_min_max(optc, vtotal_min, vtotal_max);
	}
}

static void optc401_program_global_sync(
		struct timing_generator *optc,
		int vready_offset,
		int vstartup_start,
		int vupdate_offset,
		int vupdate_width,
		int pstate_keepout)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	optc1->vready_offset = vready_offset;
	optc1->vstartup_start = vstartup_start;
	optc1->vupdate_offset = vupdate_offset;
	optc1->vupdate_width = vupdate_width;
	optc1->pstate_keepout = pstate_keepout;

	if (optc1->vstartup_start == 0) {
		BREAK_TO_DEBUGGER();
		return;
	}

	REG_SET(OTG_VSTARTUP_PARAM, 0,
		VSTARTUP_START, optc1->vstartup_start);

	REG_SET_2(OTG_VUPDATE_PARAM, 0,
			VUPDATE_OFFSET, optc1->vupdate_offset,
			VUPDATE_WIDTH, optc1->vupdate_width);

	REG_SET(OTG_VREADY_PARAM, 0,
			VREADY_OFFSET, optc1->vready_offset);

	REG_UPDATE(OTG_PSTATE_REGISTER, OTG_PSTATE_KEEPOUT_START, pstate_keepout);
}

static struct timing_generator_funcs dcn401_tg_funcs = {
		.validate_timing = optc1_validate_timing,
		.program_timing = optc1_program_timing,
		.setup_vertical_interrupt0 = optc1_setup_vertical_interrupt0,
		.setup_vertical_interrupt1 = optc1_setup_vertical_interrupt1,
		.setup_vertical_interrupt2 = optc1_setup_vertical_interrupt2,
		.program_global_sync = optc401_program_global_sync,
		.enable_crtc = optc401_enable_crtc,
		.disable_crtc = optc401_disable_crtc,
		.phantom_crtc_post_enable = optc401_phantom_crtc_post_enable,
		.disable_phantom_crtc = optc401_disable_phantom_otg,
		/* used by enable_timing_synchronization. Not need for FPGA */
		.is_counter_moving = optc1_is_counter_moving,
		.get_position = optc1_get_position,
		.get_frame_count = optc1_get_vblank_counter,
		.get_scanoutpos = optc1_get_crtc_scanoutpos,
		.get_otg_active_size = optc1_get_otg_active_size,
		.set_early_control = optc1_set_early_control,
		/* used by enable_timing_synchronization. Not need for FPGA */
		.wait_for_state = optc1_wait_for_state,
		.set_blank_color = optc3_program_blank_color,
		.did_triggered_reset_occur = optc1_did_triggered_reset_occur,
		.triplebuffer_lock = optc3_triplebuffer_lock,
		.triplebuffer_unlock = optc2_triplebuffer_unlock,
		.enable_reset_trigger = optc1_enable_reset_trigger,
		.enable_crtc_reset = optc1_enable_crtc_reset,
		.disable_reset_trigger = optc1_disable_reset_trigger,
		.lock = optc3_lock,
		.unlock = optc1_unlock,
		.lock_doublebuffer_enable = optc3_lock_doublebuffer_enable,
		.lock_doublebuffer_disable = optc3_lock_doublebuffer_disable,
		.enable_optc_clock = optc1_enable_optc_clock,
		.set_drr = optc401_set_drr,
		.get_last_used_drr_vtotal = optc2_get_last_used_drr_vtotal,
		.set_vtotal_min_max = optc401_set_vtotal_min_max,
		.set_static_screen_control = optc1_set_static_screen_control,
		.program_stereo = optc1_program_stereo,
		.is_stereo_left_eye = optc1_is_stereo_left_eye,
		.tg_init = optc3_tg_init,
		.is_tg_enabled = optc1_is_tg_enabled,
		.is_optc_underflow_occurred = optc1_is_optc_underflow_occurred,
		.clear_optc_underflow = optc1_clear_optc_underflow,
		.setup_global_swap_lock = NULL,
		.get_crc = optc1_get_crc,
		.configure_crc = optc1_configure_crc,
		.set_dsc_config = optc3_set_dsc_config,
		.get_dsc_status = optc2_get_dsc_status,
		.set_dwb_source = NULL,
		.set_odm_bypass = optc401_set_odm_bypass,
		.set_odm_combine = optc401_set_odm_combine,
		.wait_odm_doublebuffer_pending_clear = optc32_wait_odm_doublebuffer_pending_clear,
		.set_h_timing_div_manual_mode = optc401_set_h_timing_div_manual_mode,
		.get_optc_source = optc2_get_optc_source,
		.set_out_mux = optc401_set_out_mux,
		.set_drr_trigger_window = optc3_set_drr_trigger_window,
		.set_vtotal_change_limit = optc3_set_vtotal_change_limit,
		.set_gsl = optc2_set_gsl,
		.set_gsl_source_select = optc2_set_gsl_source_select,
		.set_vtg_params = optc1_set_vtg_params,
		.program_manual_trigger = optc2_program_manual_trigger,
		.setup_manual_trigger = optc2_setup_manual_trigger,
		.get_hw_timing = optc1_get_hw_timing,
		.is_two_pixels_per_container = optc1_is_two_pixels_per_container,
		.get_double_buffer_pending = optc32_get_double_buffer_pending,
};

void dcn401_timing_generator_init(struct optc *optc1)
{
	optc1->base.funcs = &dcn401_tg_funcs;

	optc1->max_h_total = optc1->tg_mask->OTG_H_TOTAL + 1;
	optc1->max_v_total = optc1->tg_mask->OTG_V_TOTAL + 1;

	optc1->min_h_blank = 32;
	optc1->min_v_blank = 3;
	optc1->min_v_blank_interlace = 5;
	optc1->min_h_sync_width = 4;
	optc1->min_v_sync_width = 1;
}

