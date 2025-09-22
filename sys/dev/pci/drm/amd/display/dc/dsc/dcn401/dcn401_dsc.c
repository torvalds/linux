// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#include <drm/display/drm_dsc_helper.h>

#include "reg_helper.h"
#include "dcn401_dsc.h"
#include "dsc/dscc_types.h"
#include "dsc/rc_calc.h"

#define MAX_THROUGHPUT_PER_DSC_100HZ 20000000
#define MAX_DSC_UNIT_COMBINE 4

static void dsc_write_to_registers(struct display_stream_compressor *dsc, const struct dsc_reg_values *reg_vals);

/* Object I/F functions */
//static void dsc401_get_enc_caps(struct dsc_enc_caps *dsc_enc_caps, int pixel_clock_100Hz);
static void dsc401_read_state(struct display_stream_compressor *dsc, struct dcn_dsc_state *s);
static bool dsc401_validate_stream(struct display_stream_compressor *dsc, const struct dsc_config *dsc_cfg);
static void dsc401_set_config(struct display_stream_compressor *dsc, const struct dsc_config *dsc_cfg,
		struct dsc_optc_config *dsc_optc_cfg);
//static bool dsc401_get_packed_pps(struct display_stream_compressor *dsc, const struct dsc_config *dsc_cfg, uint8_t *dsc_packed_pps);
static void dsc401_enable(struct display_stream_compressor *dsc, int opp_pipe);
static void dsc401_disable(struct display_stream_compressor *dsc);
static void dsc401_disconnect(struct display_stream_compressor *dsc);
static void dsc401_wait_disconnect_pending_clear(struct display_stream_compressor *dsc);
static void dsc401_get_enc_caps(struct dsc_enc_caps *dsc_enc_caps, int pixel_clock_100Hz);

static const struct dsc_funcs dcn401_dsc_funcs = {
	.dsc_get_enc_caps = dsc401_get_enc_caps,
	.dsc_read_state = dsc401_read_state,
	.dsc_validate_stream = dsc401_validate_stream,
	.dsc_set_config = dsc401_set_config,
	.dsc_get_packed_pps = dsc2_get_packed_pps,
	.dsc_enable = dsc401_enable,
	.dsc_disable = dsc401_disable,
	.dsc_disconnect = dsc401_disconnect,
	.dsc_wait_disconnect_pending_clear = dsc401_wait_disconnect_pending_clear,
};

/* Macro definitios for REG_SET macros*/
#define CTX \
	dsc401->base.ctx

#define REG(reg)\
	dsc401->dsc_regs->reg

#undef FN
#define FN(reg_name, field_name) \
	dsc401->dsc_shift->field_name, dsc401->dsc_mask->field_name
#define DC_LOGGER \
	dsc->ctx->logger

enum dsc_bits_per_comp {
	DSC_BPC_8 = 8,
	DSC_BPC_10 = 10,
	DSC_BPC_12 = 12,
	DSC_BPC_UNKNOWN
};

/* API functions (external or via structure->function_pointer) */

void dsc401_construct(struct dcn401_dsc *dsc,
		struct dc_context *ctx,
		int inst,
		const struct dcn401_dsc_registers *dsc_regs,
		const struct dcn401_dsc_shift *dsc_shift,
		const struct dcn401_dsc_mask *dsc_mask)
{
	dsc->base.ctx = ctx;
	dsc->base.inst = inst;
	dsc->base.funcs = &dcn401_dsc_funcs;

	dsc->dsc_regs = dsc_regs;
	dsc->dsc_shift = dsc_shift;
	dsc->dsc_mask = dsc_mask;

	dsc->max_image_width = 5184;
}

static void dsc401_get_enc_caps(struct dsc_enc_caps *dsc_enc_caps, int pixel_clock_100Hz)
{
	int min_dsc_unit_required = (pixel_clock_100Hz + MAX_THROUGHPUT_PER_DSC_100HZ - 1) / MAX_THROUGHPUT_PER_DSC_100HZ;

	dsc_enc_caps->dsc_version = 0x21; /* v1.2 - DP spec defined it in reverse order and we kept it */

	/* 1 slice is only supported with 1 DSC unit */
	dsc_enc_caps->slice_caps.bits.NUM_SLICES_1 = min_dsc_unit_required == 1 ? 1 : 0;
	/* 2 slice is only supported with 1 or 2 DSC units */
	dsc_enc_caps->slice_caps.bits.NUM_SLICES_2 = (min_dsc_unit_required == 1 || min_dsc_unit_required == 2) ? 1 : 0;
	/* 3 slice is only supported with 1 DSC unit */
	dsc_enc_caps->slice_caps.bits.NUM_SLICES_3 = min_dsc_unit_required == 1 ? 1 : 0;
	dsc_enc_caps->slice_caps.bits.NUM_SLICES_4 = 1;
	dsc_enc_caps->slice_caps.bits.NUM_SLICES_8 = 1;
	dsc_enc_caps->slice_caps.bits.NUM_SLICES_12 = 1;
	dsc_enc_caps->slice_caps.bits.NUM_SLICES_16 = 1;

	dsc_enc_caps->lb_bit_depth = 13;
	dsc_enc_caps->is_block_pred_supported = true;

	dsc_enc_caps->color_formats.bits.RGB = 1;
	dsc_enc_caps->color_formats.bits.YCBCR_444 = 1;
	dsc_enc_caps->color_formats.bits.YCBCR_SIMPLE_422 = 1;
	dsc_enc_caps->color_formats.bits.YCBCR_NATIVE_422 = 0;
	dsc_enc_caps->color_formats.bits.YCBCR_NATIVE_420 = 1;

	dsc_enc_caps->color_depth.bits.COLOR_DEPTH_8_BPC = 1;
	dsc_enc_caps->color_depth.bits.COLOR_DEPTH_10_BPC = 1;
	dsc_enc_caps->color_depth.bits.COLOR_DEPTH_12_BPC = 1;
	dsc_enc_caps->max_total_throughput_mps = MAX_THROUGHPUT_PER_DSC_100HZ * MAX_DSC_UNIT_COMBINE;

	dsc_enc_caps->max_slice_width = 5184; /* (including 64 overlap pixels for eDP MSO mode) */
	dsc_enc_caps->bpp_increment_div = 16; /* 1/16th of a bit */
}

/* this function read dsc related register fields to be logged later in dcn10_log_hw_state
 * into a dcn_dsc_state struct.
 */
static void dsc401_read_state(struct display_stream_compressor *dsc, struct dcn_dsc_state *s)
{
	struct dcn401_dsc *dsc401 = TO_DCN401_DSC(dsc);

	REG_GET(DSC_TOP_CONTROL, DSC_CLOCK_EN, &s->dsc_clock_en);
	REG_GET(DSCC_PPS_CONFIG3, SLICE_WIDTH, &s->dsc_slice_width);
	REG_GET(DSCC_PPS_CONFIG1, BITS_PER_PIXEL, &s->dsc_bits_per_pixel);
	REG_GET(DSCC_PPS_CONFIG3, SLICE_HEIGHT, &s->dsc_slice_height);
	REG_GET(DSCC_PPS_CONFIG1, CHUNK_SIZE, &s->dsc_chunk_size);
	REG_GET(DSCC_PPS_CONFIG2, PIC_WIDTH, &s->dsc_pic_width);
	REG_GET(DSCC_PPS_CONFIG2, PIC_HEIGHT, &s->dsc_pic_height);
	REG_GET(DSCC_PPS_CONFIG7, SLICE_BPG_OFFSET, &s->dsc_slice_bpg_offset);
	REG_GET_2(DSCRM_DSC_FORWARD_CONFIG, DSCRM_DSC_FORWARD_EN, &s->dsc_fw_en,
		DSCRM_DSC_OPP_PIPE_SOURCE, &s->dsc_opp_source);
}


static bool dsc401_validate_stream(struct display_stream_compressor *dsc, const struct dsc_config *dsc_cfg)
{
	struct dsc_optc_config dsc_optc_cfg;
	struct dcn401_dsc *dsc401 = TO_DCN401_DSC(dsc);

	if (dsc_cfg->pic_width > dsc401->max_image_width)
		return false;

	return dsc_prepare_config(dsc_cfg, &dsc401->reg_vals, &dsc_optc_cfg);
}

static void dsc401_set_config(struct display_stream_compressor *dsc, const struct dsc_config *dsc_cfg,
		struct dsc_optc_config *dsc_optc_cfg)
{
	bool is_config_ok;
	struct dcn401_dsc *dsc401 = TO_DCN401_DSC(dsc);

	DC_LOG_DSC("Setting DSC Config at DSC inst %d", dsc->inst);
	dsc_config_log(dsc, dsc_cfg);
	is_config_ok = dsc_prepare_config(dsc_cfg, &dsc401->reg_vals, dsc_optc_cfg);
	ASSERT(is_config_ok);
	DC_LOG_DSC("programming DSC Picture Parameter Set (PPS):");
	dsc_log_pps(dsc, &dsc401->reg_vals.pps);
	dsc_write_to_registers(dsc, &dsc401->reg_vals);
}

static void dsc401_enable(struct display_stream_compressor *dsc, int opp_pipe)
{
	struct dcn401_dsc *dsc401 = TO_DCN401_DSC(dsc);
	int dsc_clock_en;
	int dsc_fw_config;
	int enabled_opp_pipe;

	DC_LOG_DSC("enable DSC %d at opp pipe %d", dsc->inst, opp_pipe);

	REG_GET(DSC_TOP_CONTROL, DSC_CLOCK_EN, &dsc_clock_en);
	REG_GET_2(DSCRM_DSC_FORWARD_CONFIG, DSCRM_DSC_FORWARD_EN, &dsc_fw_config, DSCRM_DSC_OPP_PIPE_SOURCE, &enabled_opp_pipe);
	if ((dsc_clock_en || dsc_fw_config) && enabled_opp_pipe != opp_pipe) {
		DC_LOG_DSC("ERROR: DSC %d at opp pipe %d already enabled!", dsc->inst, enabled_opp_pipe);
		ASSERT(0);
	}

	REG_UPDATE(DSC_TOP_CONTROL,
		DSC_CLOCK_EN, 1);

	REG_UPDATE_2(DSCRM_DSC_FORWARD_CONFIG,
		DSCRM_DSC_FORWARD_EN, 1,
		DSCRM_DSC_OPP_PIPE_SOURCE, opp_pipe);
}


static void dsc401_disable(struct display_stream_compressor *dsc)
{
	struct dcn401_dsc *dsc401 = TO_DCN401_DSC(dsc);
	int dsc_clock_en;

	DC_LOG_DSC("disable DSC %d", dsc->inst);

	REG_GET(DSC_TOP_CONTROL, DSC_CLOCK_EN, &dsc_clock_en);
	if (!dsc_clock_en) {
		DC_LOG_DSC("DSC %d already disabled!", dsc->inst);
	}

	REG_UPDATE(DSCRM_DSC_FORWARD_CONFIG,
		DSCRM_DSC_FORWARD_EN, 0);

	REG_UPDATE(DSC_TOP_CONTROL,
		DSC_CLOCK_EN, 0);
}

static void dsc401_wait_disconnect_pending_clear(struct display_stream_compressor *dsc)
{
	struct dcn401_dsc *dsc401 = TO_DCN401_DSC(dsc);

	REG_WAIT(DSCRM_DSC_FORWARD_CONFIG, DSCRM_DSC_FORWARD_EN_STATUS, 0, 2, 50000);
}

static void dsc401_disconnect(struct display_stream_compressor *dsc)
{
	struct dcn401_dsc *dsc401 = TO_DCN401_DSC(dsc);

	DC_LOG_DSC("disconnect DSC %d", dsc->inst);

	REG_UPDATE(DSCRM_DSC_FORWARD_CONFIG,
		DSCRM_DSC_FORWARD_EN, 0);
}

static void dsc_write_to_registers(struct display_stream_compressor *dsc, const struct dsc_reg_values *reg_vals)
{
	uint32_t temp_int;
	struct dcn401_dsc *dsc401 = TO_DCN401_DSC(dsc);

	REG_SET(DSC_DEBUG_CONTROL, 0,
		DSC_DBG_EN, reg_vals->dsc_dbg_en);

	// dsccif registers
	REG_SET_2(DSCCIF_CONFIG0, 0,
		//INPUT_INTERFACE_UNDERFLOW_RECOVERY_EN, reg_vals->underflow_recovery_en,
		//INPUT_INTERFACE_UNDERFLOW_OCCURRED_INT_EN, reg_vals->underflow_occurred_int_en,
		//INPUT_INTERFACE_UNDERFLOW_OCCURRED_STATUS, reg_vals->underflow_occurred_status,
		INPUT_PIXEL_FORMAT, reg_vals->pixel_format,
		DSCCIF_CONFIG0__BITS_PER_COMPONENT, reg_vals->pps.bits_per_component);

	/* REG_SET_2(DSCCIF_CONFIG1, 0,
		PIC_WIDTH, reg_vals->pps.pic_width,
		PIC_HEIGHT, reg_vals->pps.pic_height);
	*/
	// dscc registers
	if (dsc401->dsc_mask->ICH_RESET_AT_END_OF_LINE == 0) {
		REG_SET_3(DSCC_CONFIG0, 0,
			  NUMBER_OF_SLICES_PER_LINE, reg_vals->num_slices_h - 1,
			  ALTERNATE_ICH_ENCODING_EN, reg_vals->alternate_ich_encoding_en,
			  NUMBER_OF_SLICES_IN_VERTICAL_DIRECTION, reg_vals->num_slices_v - 1);
	} else {
		REG_SET_4(DSCC_CONFIG0, 0, ICH_RESET_AT_END_OF_LINE,
			  reg_vals->ich_reset_at_eol, NUMBER_OF_SLICES_PER_LINE,
			  reg_vals->num_slices_h - 1, ALTERNATE_ICH_ENCODING_EN,
			  reg_vals->alternate_ich_encoding_en, NUMBER_OF_SLICES_IN_VERTICAL_DIRECTION,
			  reg_vals->num_slices_v - 1);
	}

	REG_SET(DSCC_CONFIG1, 0,
			DSCC_RATE_CONTROL_BUFFER_MODEL_SIZE, reg_vals->rc_buffer_model_size);
	/*REG_SET_2(DSCC_CONFIG1, 0,
		DSCC_RATE_CONTROL_BUFFER_MODEL_SIZE, reg_vals->rc_buffer_model_size,
		DSCC_DISABLE_ICH, reg_vals->disable_ich);*/

	REG_SET_4(DSCC_INTERRUPT_CONTROL0, 0,
		DSCC_RATE_CONTROL_BUFFER_MODEL_OVERFLOW_OCCURRED_INT_EN0, reg_vals->rc_buffer_model_overflow_int_en[0],
		DSCC_RATE_CONTROL_BUFFER_MODEL_OVERFLOW_OCCURRED_INT_EN1, reg_vals->rc_buffer_model_overflow_int_en[1],
		DSCC_RATE_CONTROL_BUFFER_MODEL_OVERFLOW_OCCURRED_INT_EN2, reg_vals->rc_buffer_model_overflow_int_en[2],
		DSCC_RATE_CONTROL_BUFFER_MODEL_OVERFLOW_OCCURRED_INT_EN3, reg_vals->rc_buffer_model_overflow_int_en[3]);

	REG_SET_3(DSCC_PPS_CONFIG0, 0,
		DSC_VERSION_MINOR, reg_vals->pps.dsc_version_minor,
		LINEBUF_DEPTH, reg_vals->pps.line_buf_depth,
		DSCC_PPS_CONFIG0__BITS_PER_COMPONENT, reg_vals->pps.bits_per_component);

	if (reg_vals->pixel_format == DSC_PIXFMT_NATIVE_YCBCR420 || reg_vals->pixel_format == DSC_PIXFMT_NATIVE_YCBCR422)
		temp_int = reg_vals->bpp_x32;
	else
		temp_int = reg_vals->bpp_x32 >> 1;

	REG_SET_7(DSCC_PPS_CONFIG1, 0,
		BITS_PER_PIXEL, temp_int,
		SIMPLE_422, reg_vals->pixel_format == DSC_PIXFMT_SIMPLE_YCBCR422,
		CONVERT_RGB, reg_vals->pixel_format == DSC_PIXFMT_RGB,
		BLOCK_PRED_ENABLE, reg_vals->pps.block_pred_enable,
		NATIVE_422, reg_vals->pixel_format == DSC_PIXFMT_NATIVE_YCBCR422,
		NATIVE_420, reg_vals->pixel_format == DSC_PIXFMT_NATIVE_YCBCR420,
		CHUNK_SIZE, reg_vals->pps.slice_chunk_size);

	REG_SET_2(DSCC_PPS_CONFIG2, 0,
		PIC_WIDTH, reg_vals->pps.pic_width,
		PIC_HEIGHT, reg_vals->pps.pic_height);

	REG_SET_2(DSCC_PPS_CONFIG3, 0,
		SLICE_WIDTH, reg_vals->pps.slice_width,
		SLICE_HEIGHT, reg_vals->pps.slice_height);

	REG_SET(DSCC_PPS_CONFIG4, 0,
		INITIAL_XMIT_DELAY, reg_vals->pps.initial_xmit_delay);

	REG_SET_2(DSCC_PPS_CONFIG5, 0,
		INITIAL_SCALE_VALUE, reg_vals->pps.initial_scale_value,
		SCALE_INCREMENT_INTERVAL, reg_vals->pps.scale_increment_interval);

	REG_SET_3(DSCC_PPS_CONFIG6, 0,
		SCALE_DECREMENT_INTERVAL, reg_vals->pps.scale_decrement_interval,
		FIRST_LINE_BPG_OFFSET, reg_vals->pps.first_line_bpg_offset,
		SECOND_LINE_BPG_OFFSET, reg_vals->pps.second_line_bpg_offset);

	REG_SET_2(DSCC_PPS_CONFIG7, 0,
		NFL_BPG_OFFSET, reg_vals->pps.nfl_bpg_offset,
		SLICE_BPG_OFFSET, reg_vals->pps.slice_bpg_offset);

	REG_SET_2(DSCC_PPS_CONFIG8, 0,
		NSL_BPG_OFFSET, reg_vals->pps.nsl_bpg_offset,
		SECOND_LINE_OFFSET_ADJ, reg_vals->pps.second_line_offset_adj);

	REG_SET_2(DSCC_PPS_CONFIG9, 0,
		INITIAL_OFFSET, reg_vals->pps.initial_offset,
		FINAL_OFFSET, reg_vals->pps.final_offset);

	REG_SET_3(DSCC_PPS_CONFIG10, 0,
		FLATNESS_MIN_QP, reg_vals->pps.flatness_min_qp,
		FLATNESS_MAX_QP, reg_vals->pps.flatness_max_qp,
		RC_MODEL_SIZE, reg_vals->pps.rc_model_size);

	REG_SET_5(DSCC_PPS_CONFIG11, 0,
		RC_EDGE_FACTOR, reg_vals->pps.rc_edge_factor,
		RC_QUANT_INCR_LIMIT0, reg_vals->pps.rc_quant_incr_limit0,
		RC_QUANT_INCR_LIMIT1, reg_vals->pps.rc_quant_incr_limit1,
		RC_TGT_OFFSET_LO, reg_vals->pps.rc_tgt_offset_low,
		RC_TGT_OFFSET_HI, reg_vals->pps.rc_tgt_offset_high);

	REG_SET_4(DSCC_PPS_CONFIG12, 0,
		RC_BUF_THRESH0, reg_vals->pps.rc_buf_thresh[0],
		RC_BUF_THRESH1, reg_vals->pps.rc_buf_thresh[1],
		RC_BUF_THRESH2, reg_vals->pps.rc_buf_thresh[2],
		RC_BUF_THRESH3, reg_vals->pps.rc_buf_thresh[3]);

	REG_SET_4(DSCC_PPS_CONFIG13, 0,
		RC_BUF_THRESH4, reg_vals->pps.rc_buf_thresh[4],
		RC_BUF_THRESH5, reg_vals->pps.rc_buf_thresh[5],
		RC_BUF_THRESH6, reg_vals->pps.rc_buf_thresh[6],
		RC_BUF_THRESH7, reg_vals->pps.rc_buf_thresh[7]);

	REG_SET_4(DSCC_PPS_CONFIG14, 0,
		RC_BUF_THRESH8, reg_vals->pps.rc_buf_thresh[8],
		RC_BUF_THRESH9, reg_vals->pps.rc_buf_thresh[9],
		RC_BUF_THRESH10, reg_vals->pps.rc_buf_thresh[10],
		RC_BUF_THRESH11, reg_vals->pps.rc_buf_thresh[11]);

	REG_SET_5(DSCC_PPS_CONFIG15, 0,
		RC_BUF_THRESH12, reg_vals->pps.rc_buf_thresh[12],
		RC_BUF_THRESH13, reg_vals->pps.rc_buf_thresh[13],
		RANGE_MIN_QP0, reg_vals->pps.rc_range_params[0].range_min_qp,
		RANGE_MAX_QP0, reg_vals->pps.rc_range_params[0].range_max_qp,
		RANGE_BPG_OFFSET0, reg_vals->pps.rc_range_params[0].range_bpg_offset);

	REG_SET_6(DSCC_PPS_CONFIG16, 0,
		RANGE_MIN_QP1, reg_vals->pps.rc_range_params[1].range_min_qp,
		RANGE_MAX_QP1, reg_vals->pps.rc_range_params[1].range_max_qp,
		RANGE_BPG_OFFSET1, reg_vals->pps.rc_range_params[1].range_bpg_offset,
		RANGE_MIN_QP2, reg_vals->pps.rc_range_params[2].range_min_qp,
		RANGE_MAX_QP2, reg_vals->pps.rc_range_params[2].range_max_qp,
		RANGE_BPG_OFFSET2, reg_vals->pps.rc_range_params[2].range_bpg_offset);

	REG_SET_6(DSCC_PPS_CONFIG17, 0,
		RANGE_MIN_QP3, reg_vals->pps.rc_range_params[3].range_min_qp,
		RANGE_MAX_QP3, reg_vals->pps.rc_range_params[3].range_max_qp,
		RANGE_BPG_OFFSET3, reg_vals->pps.rc_range_params[3].range_bpg_offset,
		RANGE_MIN_QP4, reg_vals->pps.rc_range_params[4].range_min_qp,
		RANGE_MAX_QP4, reg_vals->pps.rc_range_params[4].range_max_qp,
		RANGE_BPG_OFFSET4, reg_vals->pps.rc_range_params[4].range_bpg_offset);

	REG_SET_6(DSCC_PPS_CONFIG18, 0,
		RANGE_MIN_QP5, reg_vals->pps.rc_range_params[5].range_min_qp,
		RANGE_MAX_QP5, reg_vals->pps.rc_range_params[5].range_max_qp,
		RANGE_BPG_OFFSET5, reg_vals->pps.rc_range_params[5].range_bpg_offset,
		RANGE_MIN_QP6, reg_vals->pps.rc_range_params[6].range_min_qp,
		RANGE_MAX_QP6, reg_vals->pps.rc_range_params[6].range_max_qp,
		RANGE_BPG_OFFSET6, reg_vals->pps.rc_range_params[6].range_bpg_offset);

	REG_SET_6(DSCC_PPS_CONFIG19, 0,
		RANGE_MIN_QP7, reg_vals->pps.rc_range_params[7].range_min_qp,
		RANGE_MAX_QP7, reg_vals->pps.rc_range_params[7].range_max_qp,
		RANGE_BPG_OFFSET7, reg_vals->pps.rc_range_params[7].range_bpg_offset,
		RANGE_MIN_QP8, reg_vals->pps.rc_range_params[8].range_min_qp,
		RANGE_MAX_QP8, reg_vals->pps.rc_range_params[8].range_max_qp,
		RANGE_BPG_OFFSET8, reg_vals->pps.rc_range_params[8].range_bpg_offset);

	REG_SET_6(DSCC_PPS_CONFIG20, 0,
		RANGE_MIN_QP9, reg_vals->pps.rc_range_params[9].range_min_qp,
		RANGE_MAX_QP9, reg_vals->pps.rc_range_params[9].range_max_qp,
		RANGE_BPG_OFFSET9, reg_vals->pps.rc_range_params[9].range_bpg_offset,
		RANGE_MIN_QP10, reg_vals->pps.rc_range_params[10].range_min_qp,
		RANGE_MAX_QP10, reg_vals->pps.rc_range_params[10].range_max_qp,
		RANGE_BPG_OFFSET10, reg_vals->pps.rc_range_params[10].range_bpg_offset);

	REG_SET_6(DSCC_PPS_CONFIG21, 0,
		RANGE_MIN_QP11, reg_vals->pps.rc_range_params[11].range_min_qp,
		RANGE_MAX_QP11, reg_vals->pps.rc_range_params[11].range_max_qp,
		RANGE_BPG_OFFSET11, reg_vals->pps.rc_range_params[11].range_bpg_offset,
		RANGE_MIN_QP12, reg_vals->pps.rc_range_params[12].range_min_qp,
		RANGE_MAX_QP12, reg_vals->pps.rc_range_params[12].range_max_qp,
		RANGE_BPG_OFFSET12, reg_vals->pps.rc_range_params[12].range_bpg_offset);

	REG_SET_6(DSCC_PPS_CONFIG22, 0,
		RANGE_MIN_QP13, reg_vals->pps.rc_range_params[13].range_min_qp,
		RANGE_MAX_QP13, reg_vals->pps.rc_range_params[13].range_max_qp,
		RANGE_BPG_OFFSET13, reg_vals->pps.rc_range_params[13].range_bpg_offset,
		RANGE_MIN_QP14, reg_vals->pps.rc_range_params[14].range_min_qp,
		RANGE_MAX_QP14, reg_vals->pps.rc_range_params[14].range_max_qp,
		RANGE_BPG_OFFSET14, reg_vals->pps.rc_range_params[14].range_bpg_offset);
}

void dsc401_set_fgcg(struct dcn401_dsc *dsc401, bool enable)
{
	REG_UPDATE(DSC_TOP_CONTROL, DSC_FGCG_REP_DIS, !enable);
}
