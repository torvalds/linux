// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#ifndef __DC_HWSS_DCN401_H__
#define __DC_HWSS_DCN401_H__

#include "inc/core_types.h"
#include "dc.h"
#include "dc_stream.h"
#include "hw_sequencer_private.h"
#include "dcn401/dcn401_dccg.h"

struct dc;

enum ips_ono_state {
	ONO_ON = 0,
	ONO_ON_IN_PROGRESS = 1,
	ONO_OFF = 2,
	ONO_OFF_IN_PROGRESS = 3
};

struct ips_ono_region_state {
	/**
	 * @desire_pwr_state: desired power state based on configured value
	 */
	uint32_t desire_pwr_state;
	/**
	 * @current_pwr_state: current power gate status
	 */
	uint32_t current_pwr_state;
};

void dcn401_program_gamut_remap(struct pipe_ctx *pipe_ctx);

void dcn401_init_hw(struct dc *dc);

bool dcn401_set_mcm_luts(struct pipe_ctx *pipe_ctx,
				const struct dc_plane_state *plane_state);
bool dcn401_set_output_transfer_func(struct dc *dc,
				struct pipe_ctx *pipe_ctx,
				const struct dc_stream_state *stream);
void dcn401_trigger_3dlut_dma_load(struct dc *dc,
				struct pipe_ctx *pipe_ctx);
void dcn401_calculate_dccg_tmds_div_value(struct pipe_ctx *pipe_ctx,
				unsigned int *tmds_div);
enum dc_status dcn401_enable_stream_timing(
				struct pipe_ctx *pipe_ctx,
				struct dc_state *context,
				struct dc *dc);
void dcn401_enable_stream(struct pipe_ctx *pipe_ctx);
void dcn401_populate_mcm_luts(struct dc *dc,
		struct pipe_ctx *pipe_ctx,
		struct dc_cm2_func_luts mcm_luts,
		bool lut_bank_a);
void dcn401_setup_hpo_hw_control(const struct dce_hwseq *hws, bool enable);

void dcn401_set_cursor_position(struct pipe_ctx *pipe_ctx);

bool dcn401_apply_idle_power_optimizations(struct dc *dc, bool enable);

struct ips_ono_region_state dcn401_read_ono_state(struct dc *dc,
						  uint8_t region);
void dcn401_wait_for_dcc_meta_propagation(const struct dc *dc,
		const struct pipe_ctx *top_pipe_to_program);

void dcn401_prepare_bandwidth(struct dc *dc,
		struct dc_state *context);

void dcn401_optimize_bandwidth(
		struct dc *dc,
		struct dc_state *context);

void dcn401_fams2_global_control_lock(struct dc *dc,
		struct dc_state *context,
		bool lock);
void dcn401_fams2_update_config(struct dc *dc, struct dc_state *context, bool enable);
void dcn401_fams2_global_control_lock_fast(union block_sequence_params *params);
void dcn401_unblank_stream(struct pipe_ctx *pipe_ctx, struct dc_link_settings *link_settings);
void dcn401_hardware_release(struct dc *dc);
void dcn401_update_odm(struct dc *dc, struct dc_state *context,
		struct pipe_ctx *otg_master);
void adjust_hotspot_between_slices_for_2x_magnify(uint32_t cursor_width, struct dc_cursor_position *pos_cpy);
void dcn401_wait_for_det_buffer_update(struct dc *dc, struct dc_state *context, struct pipe_ctx *otg_master);
void dcn401_interdependent_update_lock(struct dc *dc, struct dc_state *context, bool lock);
void dcn401_program_outstanding_updates(struct dc *dc, struct dc_state *context);
void dcn401_reset_back_end_for_pipe(
		struct dc *dc,
		struct pipe_ctx *pipe_ctx,
		struct dc_state *context);
void dcn401_reset_hw_ctx_wrap(
		struct dc *dc,
		struct dc_state *context);
#endif /* __DC_HWSS_DCN401_H__ */
