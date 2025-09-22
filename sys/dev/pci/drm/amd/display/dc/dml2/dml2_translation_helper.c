/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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

#include "display_mode_core.h"
#include "dml2_internal_types.h"
#include "dml2_translation_helper.h"

#define NUM_DCFCLK_STAS 5
#define NUM_DCFCLK_STAS_NEW 8

void dml2_init_ip_params(struct dml2_context *dml2, const struct dc *in_dc, struct ip_params_st *out)
{
	switch (dml2->v20.dml_core_ctx.project) {
	case dml_project_dcn32:
	case dml_project_dcn321:
	default:
		// Hardcoded values for DCN32x
		out->vblank_nom_default_us = 600;
		out->rob_buffer_size_kbytes = 128;
		out->config_return_buffer_size_in_kbytes = 1280;
		out->config_return_buffer_segment_size_in_kbytes = 64;
		out->compressed_buffer_segment_size_in_kbytes = 64;
		out->meta_fifo_size_in_kentries = 22;
		out->zero_size_buffer_entries = 512;
		out->dpte_buffer_size_in_pte_reqs_luma = 68;
		out->dpte_buffer_size_in_pte_reqs_chroma = 36;
		out->dcc_meta_buffer_size_bytes = 6272;
		out->gpuvm_max_page_table_levels = 4;
		out->hostvm_max_page_table_levels = 0;
		out->pixel_chunk_size_kbytes = 8;
		//out->alpha_pixel_chunk_size_kbytes;
		out->min_pixel_chunk_size_bytes = 1024;
		out->meta_chunk_size_kbytes = 2;
		out->min_meta_chunk_size_bytes = 256;
		out->writeback_chunk_size_kbytes = 8;
		out->line_buffer_size_bits = 1171920;
		out->max_line_buffer_lines = 32;
		out->writeback_interface_buffer_size_kbytes = 90;
		//Number of pipes after DCN Pipe harvesting
		out->max_num_dpp = dml2->config.dcn_pipe_count;
		out->max_num_otg = dml2->config.dcn_pipe_count;
		out->max_num_wb = 1;
		out->max_dchub_pscl_bw_pix_per_clk = 4;
		out->max_pscl_lb_bw_pix_per_clk = 2;
		out->max_lb_vscl_bw_pix_per_clk = 4;
		out->max_vscl_hscl_bw_pix_per_clk = 4;
		out->max_hscl_ratio = 6;
		out->max_vscl_ratio = 6;
		out->max_hscl_taps = 8;
		out->max_vscl_taps = 8;
		out->dispclk_ramp_margin_percent = 1;
		out->dppclk_delay_subtotal = 47;
		out->dppclk_delay_scl = 50;
		out->dppclk_delay_scl_lb_only = 16;
		out->dppclk_delay_cnvc_formatter = 28;
		out->dppclk_delay_cnvc_cursor = 6;
		out->cursor_buffer_size = 16;
		out->cursor_chunk_size = 2;
		out->dispclk_delay_subtotal = 125;
		out->max_inter_dcn_tile_repeaters = 8;
		out->writeback_max_hscl_ratio = 1;
		out->writeback_max_vscl_ratio = 1;
		out->writeback_min_hscl_ratio = 1;
		out->writeback_min_vscl_ratio = 1;
		out->writeback_max_hscl_taps = 1;
		out->writeback_max_vscl_taps = 1;
		out->writeback_line_buffer_buffer_size = 0;
		out->num_dsc = 4;
		out->maximum_dsc_bits_per_component = 12;
		out->maximum_pixels_per_line_per_dsc_unit = 6016;
		out->dsc422_native_support = true;
		out->dcc_supported = true;
		out->ptoi_supported = false;

		out->gpuvm_enable = false;
		out->hostvm_enable = false;
		out->cursor_64bpp_support = false;
		out->dynamic_metadata_vm_enabled = false;

		out->max_num_hdmi_frl_outputs = 1;
		out->max_num_dp2p0_outputs = 2;
		out->max_num_dp2p0_streams = 4;
		break;

	case dml_project_dcn35:
	case dml_project_dcn351:
		out->rob_buffer_size_kbytes = 64;
		out->config_return_buffer_size_in_kbytes = 1792;
		out->compressed_buffer_segment_size_in_kbytes = 64;
		out->meta_fifo_size_in_kentries = 32;
		out->zero_size_buffer_entries = 512;
		out->pixel_chunk_size_kbytes = 8;
		out->alpha_pixel_chunk_size_kbytes = 4;
		out->min_pixel_chunk_size_bytes = 1024;
		out->meta_chunk_size_kbytes = 2;
		out->min_meta_chunk_size_bytes = 256;
		out->writeback_chunk_size_kbytes = 8;
		out->dpte_buffer_size_in_pte_reqs_luma = 68;
		out->dpte_buffer_size_in_pte_reqs_chroma = 36;
		out->dcc_meta_buffer_size_bytes = 6272;
		out->gpuvm_enable = 1;
		out->hostvm_enable = 1;
		out->gpuvm_max_page_table_levels = 1;
		out->hostvm_max_page_table_levels = 2;
		out->num_dsc = 4;
		out->maximum_dsc_bits_per_component = 12;
		out->maximum_pixels_per_line_per_dsc_unit = 6016;
		out->dsc422_native_support = 1;
		out->line_buffer_size_bits = 986880;
		out->dcc_supported = 1;
		out->max_line_buffer_lines = 32;
		out->writeback_interface_buffer_size_kbytes = 90;
		out->max_num_dpp = 4;
		out->max_num_otg = 4;
		out->max_num_hdmi_frl_outputs = 1;
		out->max_num_dp2p0_outputs = 2;
		out->max_num_dp2p0_streams = 4;
		out->max_num_wb = 1;

		out->max_dchub_pscl_bw_pix_per_clk = 4;
		out->max_pscl_lb_bw_pix_per_clk = 2;
		out->max_lb_vscl_bw_pix_per_clk = 4;
		out->max_vscl_hscl_bw_pix_per_clk = 4;
		out->max_hscl_ratio = 6;
		out->max_vscl_ratio = 6;
		out->max_hscl_taps = 8;
		out->max_vscl_taps = 8;
		out->dispclk_ramp_margin_percent = 1.11;

		out->dppclk_delay_subtotal = 47;
		out->dppclk_delay_scl = 50;
		out->dppclk_delay_scl_lb_only = 16;
		out->dppclk_delay_cnvc_formatter = 28;
		out->dppclk_delay_cnvc_cursor = 6;
		out->dispclk_delay_subtotal = 125;

		out->dynamic_metadata_vm_enabled = false;
		out->max_inter_dcn_tile_repeaters = 8;
		out->cursor_buffer_size = 16; // kBytes
		out->cursor_chunk_size = 2; // kBytes

		out->writeback_line_buffer_buffer_size = 0;
		out->writeback_max_hscl_ratio = 1;
		out->writeback_max_vscl_ratio = 1;
		out->writeback_min_hscl_ratio = 1;
		out->writeback_min_vscl_ratio = 1;
		out->writeback_max_hscl_taps  = 1;
		out->writeback_max_vscl_taps  = 1;
		out->ptoi_supported	= 0;

		out->vblank_nom_default_us = 668; /*not in dml, but in programming guide, hard coded in dml2_translate_ip_params*/
		out->config_return_buffer_segment_size_in_kbytes = 64; /*required, but not exist,, hard coded in dml2_translate_ip_params*/
		break;

	case dml_project_dcn401:
		// Hardcoded values for DCN4m
		out->vblank_nom_default_us = 668;	//600;
		out->rob_buffer_size_kbytes = 192;	//128;
		out->config_return_buffer_size_in_kbytes = 1344;	//1280;
		out->config_return_buffer_segment_size_in_kbytes = 64;
		out->compressed_buffer_segment_size_in_kbytes = 64;
		out->meta_fifo_size_in_kentries = 22;
		out->dpte_buffer_size_in_pte_reqs_luma = 68;
		out->dpte_buffer_size_in_pte_reqs_chroma = 36;
		out->gpuvm_max_page_table_levels = 4;
		out->pixel_chunk_size_kbytes = 8;
		out->alpha_pixel_chunk_size_kbytes = 4;
		out->min_pixel_chunk_size_bytes = 1024;
		out->writeback_chunk_size_kbytes = 8;
		out->line_buffer_size_bits = 1171920;
		out->max_line_buffer_lines = 32;
		out->writeback_interface_buffer_size_kbytes = 90;
		//Number of pipes after DCN Pipe harvesting
		out->max_num_dpp = dml2->config.dcn_pipe_count;
		out->max_num_otg = dml2->config.dcn_pipe_count;
		out->max_num_wb = 1;
		out->max_dchub_pscl_bw_pix_per_clk = 4;
		out->max_pscl_lb_bw_pix_per_clk = 2;
		out->max_lb_vscl_bw_pix_per_clk = 4;
		out->max_vscl_hscl_bw_pix_per_clk = 4;
		out->max_hscl_ratio = 6;
		out->max_vscl_ratio = 6;
		out->max_hscl_taps = 8;
		out->max_vscl_taps = 8;
		out->dispclk_ramp_margin_percent = 1;
		out->dppclk_delay_subtotal = 47;
		out->dppclk_delay_scl = 50;
		out->dppclk_delay_scl_lb_only = 16;
		out->dppclk_delay_cnvc_formatter = 28;
		out->dppclk_delay_cnvc_cursor = 6;
		out->dispclk_delay_subtotal = 125;
		out->cursor_buffer_size = 24;	//16
		out->cursor_chunk_size = 2;
		out->max_inter_dcn_tile_repeaters = 8;
		out->writeback_max_hscl_ratio = 1;
		out->writeback_max_vscl_ratio = 1;
		out->writeback_min_hscl_ratio = 1;
		out->writeback_min_vscl_ratio = 1;
		out->writeback_max_hscl_taps = 1;
		out->writeback_max_vscl_taps = 1;
		out->writeback_line_buffer_buffer_size = 0;
		out->num_dsc = 4;
		out->maximum_dsc_bits_per_component = 12;
		out->maximum_pixels_per_line_per_dsc_unit = 5760;
		out->dsc422_native_support = true;
		out->dcc_supported = true;
		out->ptoi_supported = false;

		out->gpuvm_enable = false;
		out->hostvm_enable = false;
		out->cursor_64bpp_support = true;	//false;
		out->dynamic_metadata_vm_enabled = false;

		out->max_num_hdmi_frl_outputs = 1;
		out->max_num_dp2p0_outputs = 4;		//2;
		out->max_num_dp2p0_streams = 4;
		break;
	}
}

void dml2_init_socbb_params(struct dml2_context *dml2, const struct dc *in_dc, struct soc_bounding_box_st *out)
{
	out->dprefclk_mhz = dml2->config.bbox_overrides.dprefclk_mhz;
	out->xtalclk_mhz = dml2->config.bbox_overrides.xtalclk_mhz;
	out->pcierefclk_mhz = 100;
	out->refclk_mhz = dml2->config.bbox_overrides.dchub_refclk_mhz;

	out->max_outstanding_reqs = 512;
	out->pct_ideal_sdp_bw_after_urgent = 100;
	out->pct_ideal_fabric_bw_after_urgent = 67;
	out->pct_ideal_dram_bw_after_urgent_pixel_only = 20;
	out->pct_ideal_dram_bw_after_urgent_pixel_and_vm = 60;
	out->pct_ideal_dram_bw_after_urgent_vm_only = 30;
	out->pct_ideal_dram_bw_after_urgent_strobe = 67;
	out->max_avg_sdp_bw_use_normal_percent = 80;
	out->max_avg_fabric_bw_use_normal_percent = 60;
	out->max_avg_dram_bw_use_normal_percent = 15;
	out->max_avg_dram_bw_use_normal_strobe_percent = 50;

	out->urgent_out_of_order_return_per_channel_pixel_only_bytes = 4096;
	out->urgent_out_of_order_return_per_channel_pixel_and_vm_bytes = 4096;
	out->urgent_out_of_order_return_per_channel_vm_only_bytes = 4096;
	out->return_bus_width_bytes = 64;
	out->dram_channel_width_bytes = 2;
	out->fabric_datapath_to_dcn_data_return_bytes = 64;
	out->hostvm_min_page_size_kbytes = 0;
	out->gpuvm_min_page_size_kbytes = 256;
	out->phy_downspread_percent = 0.38;
	out->dcn_downspread_percent = 0.5;
	out->dispclk_dppclk_vco_speed_mhz = dml2->config.bbox_overrides.disp_pll_vco_speed_mhz;
	out->mall_allocated_for_dcn_mbytes = dml2->config.mall_cfg.max_cab_allocation_bytes / 1048576; // 64 or 32 MB;

	out->do_urgent_latency_adjustment = true;

	switch (dml2->v20.dml_core_ctx.project) {

	case dml_project_dcn32:
	default:
		out->num_chans = 24;
		out->round_trip_ping_latency_dcfclk_cycles = 263;
		out->smn_latency_us = 2;
		break;

	case dml_project_dcn321:
		out->num_chans = 8;
		out->round_trip_ping_latency_dcfclk_cycles = 207;
		out->smn_latency_us = 0;
		break;

	case dml_project_dcn35:
	case dml_project_dcn351:
		out->num_chans = 4;
		out->round_trip_ping_latency_dcfclk_cycles = 106;
		out->smn_latency_us = 2;
		out->dispclk_dppclk_vco_speed_mhz = 3600;
		out->pct_ideal_dram_bw_after_urgent_pixel_only = 65.0;
		break;

	case dml_project_dcn401:
		out->pct_ideal_fabric_bw_after_urgent = 76;			//67;
		out->max_avg_sdp_bw_use_normal_percent = 75;		//80;
		out->max_avg_fabric_bw_use_normal_percent = 57;		//60;

		out->urgent_out_of_order_return_per_channel_pixel_only_bytes = 0;	//4096;
		out->urgent_out_of_order_return_per_channel_pixel_and_vm_bytes = 0;	//4096;
		out->urgent_out_of_order_return_per_channel_vm_only_bytes = 0;		//4096;

		out->num_chans = 16;
		out->round_trip_ping_latency_dcfclk_cycles = 1000;	//263;
		out->smn_latency_us = 0;							//2 us
		out->mall_allocated_for_dcn_mbytes = dml2->config.mall_cfg.max_cab_allocation_bytes / 1048576; // 64;
		break;
	}
	/* ---Overrides if available--- */
	if (dml2->config.bbox_overrides.dram_num_chan)
		out->num_chans = dml2->config.bbox_overrides.dram_num_chan;

	if (dml2->config.bbox_overrides.dram_chanel_width_bytes)
		out->dram_channel_width_bytes = dml2->config.bbox_overrides.dram_chanel_width_bytes;
}

void dml2_init_soc_states(struct dml2_context *dml2, const struct dc *in_dc,
	const struct soc_bounding_box_st *in_bbox, struct soc_states_st *out)
{
	struct dml2_policy_build_synthetic_soc_states_scratch *s = &dml2->v20.scratch.create_scratch.build_synthetic_socbb_scratch;
	struct dml2_policy_build_synthetic_soc_states_params *p = &dml2->v20.scratch.build_synthetic_socbb_params;
	unsigned int dcfclk_stas_mhz[NUM_DCFCLK_STAS] = {0};
	unsigned int dcfclk_stas_mhz_new[NUM_DCFCLK_STAS_NEW] = {0};
	unsigned int dml_project = dml2->v20.dml_core_ctx.project;

	unsigned int i = 0;
	unsigned int transactions_per_mem_clock = 16; // project specific, depends on used Memory type

	if (dml_project == dml_project_dcn351) {
		p->dcfclk_stas_mhz = dcfclk_stas_mhz_new;
		p->num_dcfclk_stas = NUM_DCFCLK_STAS_NEW;
	} else {
		p->dcfclk_stas_mhz = dcfclk_stas_mhz;
		p->num_dcfclk_stas = NUM_DCFCLK_STAS;
	}

	p->in_bbox = in_bbox;
	p->out_states = out;
	p->in_states = &dml2->v20.scratch.create_scratch.in_states;


	/* Initial hardcoded values */
	switch (dml2->v20.dml_core_ctx.project) {

	case dml_project_dcn32:
	default:
		p->in_states->num_states = 2;
		transactions_per_mem_clock = 16;
		p->in_states->state_array[0].socclk_mhz = 620.0;
		p->in_states->state_array[0].dscclk_mhz = 716.667;
		p->in_states->state_array[0].phyclk_mhz = 810;
		p->in_states->state_array[0].phyclk_d18_mhz = 667;
		p->in_states->state_array[0].phyclk_d32_mhz = 625;
		p->in_states->state_array[0].dtbclk_mhz = 1564.0;
		p->in_states->state_array[0].fabricclk_mhz = 450.0;
		p->in_states->state_array[0].dcfclk_mhz = 300.0;
		p->in_states->state_array[0].dispclk_mhz = 2150.0;
		p->in_states->state_array[0].dppclk_mhz = 2150.0;
		p->in_states->state_array[0].dram_speed_mts = 100 * transactions_per_mem_clock;

		p->in_states->state_array[0].urgent_latency_pixel_data_only_us = 4;
		p->in_states->state_array[0].urgent_latency_pixel_mixed_with_vm_data_us = 0;
		p->in_states->state_array[0].urgent_latency_vm_data_only_us = 0;
		p->in_states->state_array[0].writeback_latency_us = 12;
		p->in_states->state_array[0].urgent_latency_adjustment_fabric_clock_component_us = 1;
		p->in_states->state_array[0].urgent_latency_adjustment_fabric_clock_reference_mhz = 3000;
		p->in_states->state_array[0].sr_exit_z8_time_us = 0;
		p->in_states->state_array[0].sr_enter_plus_exit_z8_time_us = 0;
		p->in_states->state_array[0].dram_clock_change_latency_us = 400;
		p->in_states->state_array[0].use_ideal_dram_bw_strobe = true;
		p->in_states->state_array[0].sr_exit_time_us = 42.97;
		p->in_states->state_array[0].sr_enter_plus_exit_time_us = 49.94;
		p->in_states->state_array[0].fclk_change_latency_us = 20;
		p->in_states->state_array[0].usr_retraining_latency_us = 2;

		p->in_states->state_array[1].socclk_mhz = 1200.0;
		p->in_states->state_array[1].fabricclk_mhz = 2500.0;
		p->in_states->state_array[1].dcfclk_mhz = 1564.0;
		p->in_states->state_array[1].dram_speed_mts = 1125 * transactions_per_mem_clock;
		break;

	case dml_project_dcn321:
		p->in_states->num_states = 2;
		transactions_per_mem_clock = 16;
		p->in_states->state_array[0].socclk_mhz = 582.0;
		p->in_states->state_array[0].dscclk_mhz = 573.333;
		p->in_states->state_array[0].phyclk_mhz = 810;
		p->in_states->state_array[0].phyclk_d18_mhz = 667;
		p->in_states->state_array[0].phyclk_d32_mhz = 313;
		p->in_states->state_array[0].dtbclk_mhz = 1564.0;
		p->in_states->state_array[0].fabricclk_mhz = 450.0;
		p->in_states->state_array[0].dcfclk_mhz = 300.0;
		p->in_states->state_array[0].dispclk_mhz = 1720.0;
		p->in_states->state_array[0].dppclk_mhz = 1720.0;
		p->in_states->state_array[0].dram_speed_mts = 100 * transactions_per_mem_clock;

		p->in_states->state_array[0].urgent_latency_pixel_data_only_us = 4;
		p->in_states->state_array[0].urgent_latency_pixel_mixed_with_vm_data_us = 0;
		p->in_states->state_array[0].urgent_latency_vm_data_only_us = 0;
		p->in_states->state_array[0].writeback_latency_us = 12;
		p->in_states->state_array[0].urgent_latency_adjustment_fabric_clock_component_us = 1;
		p->in_states->state_array[0].urgent_latency_adjustment_fabric_clock_reference_mhz = 3000;
		p->in_states->state_array[0].sr_exit_z8_time_us = 0;
		p->in_states->state_array[0].sr_enter_plus_exit_z8_time_us = 0;
		p->in_states->state_array[0].dram_clock_change_latency_us = 400;
		p->in_states->state_array[0].use_ideal_dram_bw_strobe = true;
		p->in_states->state_array[0].sr_exit_time_us = 19.95;
		p->in_states->state_array[0].sr_enter_plus_exit_time_us = 24.36;
		p->in_states->state_array[0].fclk_change_latency_us = 7;
		p->in_states->state_array[0].usr_retraining_latency_us = 0;

		p->in_states->state_array[1].socclk_mhz = 1200.0;
		p->in_states->state_array[1].fabricclk_mhz = 2250.0;
		p->in_states->state_array[1].dcfclk_mhz = 1434.0;
		p->in_states->state_array[1].dram_speed_mts = 1000 * transactions_per_mem_clock;
		break;
	case dml_project_dcn401:
		p->in_states->num_states = 2;
		transactions_per_mem_clock = 16;
		p->in_states->state_array[0].socclk_mhz = 300;		//620.0;
		p->in_states->state_array[0].dscclk_mhz = 666.667;	//716.667;
		p->in_states->state_array[0].phyclk_mhz = 810;
		p->in_states->state_array[0].phyclk_d18_mhz = 667;
		p->in_states->state_array[0].phyclk_d32_mhz = 625;
		p->in_states->state_array[0].dtbclk_mhz = 2000;		//1564.0;
		p->in_states->state_array[0].fabricclk_mhz = 300;	//450.0;
		p->in_states->state_array[0].dcfclk_mhz = 200;		//300.0;
		p->in_states->state_array[0].dispclk_mhz = 2000;	//2150.0;
		p->in_states->state_array[0].dppclk_mhz = 2000;		//2150.0;
		p->in_states->state_array[0].dram_speed_mts = 97 * transactions_per_mem_clock; //100 *

		p->in_states->state_array[0].urgent_latency_pixel_data_only_us = 4;
		p->in_states->state_array[0].urgent_latency_pixel_mixed_with_vm_data_us = 0;
		p->in_states->state_array[0].urgent_latency_vm_data_only_us = 0;
		p->in_states->state_array[0].writeback_latency_us = 12;
		p->in_states->state_array[0].urgent_latency_adjustment_fabric_clock_component_us = 1;
		p->in_states->state_array[0].urgent_latency_adjustment_fabric_clock_reference_mhz = 1000;	//3000;
		p->in_states->state_array[0].sr_exit_z8_time_us = 0;
		p->in_states->state_array[0].sr_enter_plus_exit_z8_time_us = 0;
		p->in_states->state_array[0].dram_clock_change_latency_us = 400;
		p->in_states->state_array[0].use_ideal_dram_bw_strobe = true;
		p->in_states->state_array[0].sr_exit_time_us = 15.70;	//42.97;
		p->in_states->state_array[0].sr_enter_plus_exit_time_us = 20.20;	//49.94;
		p->in_states->state_array[0].fclk_change_latency_us = 0;	//20;
		p->in_states->state_array[0].usr_retraining_latency_us = 0;	//2;

		p->in_states->state_array[1].socclk_mhz = 1600;		//1200.0;
		p->in_states->state_array[1].fabricclk_mhz = 2500;	//2500.0;
		p->in_states->state_array[1].dcfclk_mhz = 1800;		//1564.0;
		p->in_states->state_array[1].dram_speed_mts = 1125 * transactions_per_mem_clock;
		break;
	}

	/* Override from passed values, if available */
	for (i = 0; i < p->in_states->num_states; i++) {
		if (dml2->config.bbox_overrides.sr_exit_latency_us) {
			p->in_states->state_array[i].sr_exit_time_us =
				dml2->config.bbox_overrides.sr_exit_latency_us;
		}

		if (dml2->config.bbox_overrides.sr_enter_plus_exit_latency_us) {
			p->in_states->state_array[i].sr_enter_plus_exit_time_us =
				dml2->config.bbox_overrides.sr_enter_plus_exit_latency_us;
		}

		if (dml2->config.bbox_overrides.sr_exit_z8_time_us) {
			p->in_states->state_array[i].sr_exit_z8_time_us =
				dml2->config.bbox_overrides.sr_exit_z8_time_us;
		}

		if (dml2->config.bbox_overrides.sr_enter_plus_exit_z8_time_us) {
			p->in_states->state_array[i].sr_enter_plus_exit_z8_time_us =
				dml2->config.bbox_overrides.sr_enter_plus_exit_z8_time_us;
		}

		if (dml2->config.bbox_overrides.urgent_latency_us) {
			p->in_states->state_array[i].urgent_latency_pixel_data_only_us =
				dml2->config.bbox_overrides.urgent_latency_us;
		}

		if (dml2->config.bbox_overrides.dram_clock_change_latency_us) {
			p->in_states->state_array[i].dram_clock_change_latency_us =
				dml2->config.bbox_overrides.dram_clock_change_latency_us;
		}

		if (dml2->config.bbox_overrides.fclk_change_latency_us) {
			p->in_states->state_array[i].fclk_change_latency_us =
				dml2->config.bbox_overrides.fclk_change_latency_us;
		}
	}

	/* DCFCLK stas values are project specific */
	if ((dml2->v20.dml_core_ctx.project == dml_project_dcn32) ||
		(dml2->v20.dml_core_ctx.project == dml_project_dcn321)) {
		p->dcfclk_stas_mhz[0] = p->in_states->state_array[0].dcfclk_mhz;
		p->dcfclk_stas_mhz[1] = 615;
		p->dcfclk_stas_mhz[2] = 906;
		p->dcfclk_stas_mhz[3] = 1324;
		p->dcfclk_stas_mhz[4] = p->in_states->state_array[1].dcfclk_mhz;
	} else if (dml2->v20.dml_core_ctx.project != dml_project_dcn35 &&
			dml2->v20.dml_core_ctx.project != dml_project_dcn351) {
		p->dcfclk_stas_mhz[0] = 300;
		p->dcfclk_stas_mhz[1] = 615;
		p->dcfclk_stas_mhz[2] = 906;
		p->dcfclk_stas_mhz[3] = 1324;
		p->dcfclk_stas_mhz[4] = 1500;
	}
	/* Copy clocks tables entries, if available */
	if (dml2->config.bbox_overrides.clks_table.num_states) {
		p->in_states->num_states = dml2->config.bbox_overrides.clks_table.num_states;
		for (i = 0; i < dml2->config.bbox_overrides.clks_table.num_entries_per_clk.num_dcfclk_levels; i++) {
			p->in_states->state_array[i].dcfclk_mhz = dml2->config.bbox_overrides.clks_table.clk_entries[i].dcfclk_mhz;
		}

		p->dcfclk_stas_mhz[0] = dml2->config.bbox_overrides.clks_table.clk_entries[0].dcfclk_mhz;
		if (i > 1)
			p->dcfclk_stas_mhz[4] = dml2->config.bbox_overrides.clks_table.clk_entries[i-1].dcfclk_mhz;

		for (i = 0; i < dml2->config.bbox_overrides.clks_table.num_entries_per_clk.num_fclk_levels; i++) {
			p->in_states->state_array[i].fabricclk_mhz =
				dml2->config.bbox_overrides.clks_table.clk_entries[i].fclk_mhz;
		}

		for (i = 0; i < dml2->config.bbox_overrides.clks_table.num_entries_per_clk.num_memclk_levels; i++) {
			p->in_states->state_array[i].dram_speed_mts =
				dml2->config.bbox_overrides.clks_table.clk_entries[i].memclk_mhz * transactions_per_mem_clock;
		}

		for (i = 0; i < dml2->config.bbox_overrides.clks_table.num_entries_per_clk.num_socclk_levels; i++) {
			p->in_states->state_array[i].socclk_mhz =
				dml2->config.bbox_overrides.clks_table.clk_entries[i].socclk_mhz;
		}

		for (i = 0; i < dml2->config.bbox_overrides.clks_table.num_entries_per_clk.num_dtbclk_levels; i++) {
			if (dml2->config.bbox_overrides.clks_table.clk_entries[i].dtbclk_mhz > 0)
				p->in_states->state_array[i].dtbclk_mhz =
					dml2->config.bbox_overrides.clks_table.clk_entries[i].dtbclk_mhz;
		}

		for (i = 0; i < dml2->config.bbox_overrides.clks_table.num_entries_per_clk.num_dispclk_levels; i++) {
			p->in_states->state_array[i].dispclk_mhz =
				dml2->config.bbox_overrides.clks_table.clk_entries[i].dispclk_mhz;
			p->in_states->state_array[i].dppclk_mhz =
				dml2->config.bbox_overrides.clks_table.clk_entries[i].dppclk_mhz;
		}
	}

	dml2_policy_build_synthetic_soc_states(s, p);
	if (dml2->v20.dml_core_ctx.project == dml_project_dcn35) {
		// Override last out_state with data from last in_state
		// This will ensure that out_state contains max fclk
		memcpy(&p->out_states->state_array[p->out_states->num_states - 1],
				&p->in_states->state_array[p->in_states->num_states - 1],
				sizeof(struct soc_state_bounding_box_st));
	}
}

void dml2_translate_ip_params(const struct dc *in, struct ip_params_st *out)
{
	const struct _vcs_dpi_ip_params_st *in_ip_params = &in->dml.ip;
	/* Copy over the IP params tp dml2_ctx */
	out->compressed_buffer_segment_size_in_kbytes = in_ip_params->compressed_buffer_segment_size_in_kbytes;
	out->config_return_buffer_size_in_kbytes = in_ip_params->config_return_buffer_size_in_kbytes;
	out->cursor_buffer_size = in_ip_params->cursor_buffer_size;
	out->cursor_chunk_size = in_ip_params->cursor_chunk_size;
	out->dcc_meta_buffer_size_bytes = in_ip_params->dcc_meta_buffer_size_bytes;
	out->dcc_supported = in_ip_params->dcc_supported;
	out->dispclk_delay_subtotal = in_ip_params->dispclk_delay_subtotal;
	out->dispclk_ramp_margin_percent = in_ip_params->dispclk_ramp_margin_percent;
	out->dppclk_delay_cnvc_cursor = in_ip_params->dppclk_delay_cnvc_cursor;
	out->dppclk_delay_cnvc_formatter = in_ip_params->dppclk_delay_cnvc_formatter;
	out->dppclk_delay_scl = in_ip_params->dppclk_delay_scl;
	out->dppclk_delay_scl_lb_only = in_ip_params->dppclk_delay_scl_lb_only;
	out->dppclk_delay_subtotal = in_ip_params->dppclk_delay_subtotal;
	out->dpte_buffer_size_in_pte_reqs_chroma = in_ip_params->dpte_buffer_size_in_pte_reqs_chroma;
	out->dpte_buffer_size_in_pte_reqs_luma = in_ip_params->dpte_buffer_size_in_pte_reqs_luma;
	out->dsc422_native_support = in_ip_params->dsc422_native_support;
	out->dynamic_metadata_vm_enabled = in_ip_params->dynamic_metadata_vm_enabled;
	out->gpuvm_enable = in_ip_params->gpuvm_enable;
	out->gpuvm_max_page_table_levels = in_ip_params->gpuvm_max_page_table_levels;
	out->hostvm_enable = in_ip_params->hostvm_enable;
	out->hostvm_max_page_table_levels = in_ip_params->hostvm_max_page_table_levels;
	out->line_buffer_size_bits = in_ip_params->line_buffer_size_bits;
	out->maximum_dsc_bits_per_component = in_ip_params->maximum_dsc_bits_per_component;
	out->maximum_pixels_per_line_per_dsc_unit = in_ip_params->maximum_pixels_per_line_per_dsc_unit;
	out->max_dchub_pscl_bw_pix_per_clk = in_ip_params->max_dchub_pscl_bw_pix_per_clk;
	out->max_hscl_ratio = in_ip_params->max_hscl_ratio;
	out->max_hscl_taps = in_ip_params->max_hscl_taps;
	out->max_inter_dcn_tile_repeaters = in_ip_params->max_inter_dcn_tile_repeaters;
	out->max_lb_vscl_bw_pix_per_clk = in_ip_params->max_lb_vscl_bw_pix_per_clk;
	out->max_line_buffer_lines = in_ip_params->max_line_buffer_lines;
	out->max_num_dp2p0_outputs = in_ip_params->max_num_dp2p0_outputs;
	out->max_num_dp2p0_streams = in_ip_params->max_num_dp2p0_streams;
	out->max_num_dpp = in_ip_params->max_num_dpp;
	out->max_num_hdmi_frl_outputs = in_ip_params->max_num_hdmi_frl_outputs;
	out->max_num_otg = in_ip_params->max_num_otg;
	out->max_num_wb = in_ip_params->max_num_wb;
	out->max_pscl_lb_bw_pix_per_clk = in_ip_params->max_pscl_lb_bw_pix_per_clk;
	out->max_vscl_hscl_bw_pix_per_clk = in_ip_params->max_vscl_hscl_bw_pix_per_clk;
	out->max_vscl_ratio = in_ip_params->max_vscl_ratio;
	out->max_vscl_taps = in_ip_params->max_vscl_taps;
	out->meta_chunk_size_kbytes = in_ip_params->meta_chunk_size_kbytes;
	out->meta_fifo_size_in_kentries = in_ip_params->meta_fifo_size_in_kentries;
	out->min_meta_chunk_size_bytes = in_ip_params->min_meta_chunk_size_bytes;
	out->min_pixel_chunk_size_bytes = in_ip_params->min_pixel_chunk_size_bytes;
	out->num_dsc = in_ip_params->num_dsc;
	out->pixel_chunk_size_kbytes = in_ip_params->pixel_chunk_size_kbytes;
	out->ptoi_supported = in_ip_params->ptoi_supported;
	out->rob_buffer_size_kbytes = in_ip_params->rob_buffer_size_kbytes;
	out->writeback_chunk_size_kbytes = in_ip_params->writeback_chunk_size_kbytes;
	out->writeback_interface_buffer_size_kbytes = in_ip_params->writeback_interface_buffer_size_kbytes;
	out->writeback_line_buffer_buffer_size = in_ip_params->writeback_line_buffer_buffer_size;
	out->writeback_max_hscl_ratio = in_ip_params->writeback_max_hscl_ratio;
	out->writeback_max_hscl_taps = in_ip_params->writeback_max_hscl_taps;
	out->writeback_max_vscl_ratio = in_ip_params->writeback_max_vscl_ratio;
	out->writeback_max_vscl_taps = in_ip_params->writeback_max_vscl_taps;
	out->writeback_min_hscl_ratio = in_ip_params->writeback_min_hscl_ratio;
	out->writeback_min_vscl_ratio = in_ip_params->writeback_min_vscl_ratio;
	out->zero_size_buffer_entries = in_ip_params->zero_size_buffer_entries;

	/* As per hardcoded reference / discussions */
	out->config_return_buffer_segment_size_in_kbytes = 64;
	//out->vblank_nom_default_us = 600;
	out->vblank_nom_default_us = in_ip_params->VBlankNomDefaultUS;
}

void dml2_translate_socbb_params(const struct dc *in, struct soc_bounding_box_st *out)
{
	const struct _vcs_dpi_soc_bounding_box_st *in_soc_params = &in->dml.soc;
	/* Copy over the SOCBB params to dml2_ctx */
	out->dispclk_dppclk_vco_speed_mhz = in_soc_params->dispclk_dppclk_vco_speed_mhz;
	out->do_urgent_latency_adjustment = in_soc_params->do_urgent_latency_adjustment;
	out->dram_channel_width_bytes = (dml_uint_t)in_soc_params->dram_channel_width_bytes;
	out->fabric_datapath_to_dcn_data_return_bytes = (dml_uint_t)in_soc_params->fabric_datapath_to_dcn_data_return_bytes;
	out->gpuvm_min_page_size_kbytes = in_soc_params->gpuvm_min_page_size_bytes / 1024;
	out->hostvm_min_page_size_kbytes = in_soc_params->hostvm_min_page_size_bytes / 1024;
	out->mall_allocated_for_dcn_mbytes = (dml_uint_t)in_soc_params->mall_allocated_for_dcn_mbytes;
	out->max_avg_dram_bw_use_normal_percent = in_soc_params->max_avg_dram_bw_use_normal_percent;
	out->max_avg_fabric_bw_use_normal_percent = in_soc_params->max_avg_fabric_bw_use_normal_percent;
	out->max_avg_dram_bw_use_normal_strobe_percent = in_soc_params->max_avg_dram_bw_use_normal_strobe_percent;
	out->max_avg_sdp_bw_use_normal_percent = in_soc_params->max_avg_sdp_bw_use_normal_percent;
	out->max_outstanding_reqs = in_soc_params->max_request_size_bytes;
	out->num_chans = in_soc_params->num_chans;
	out->pct_ideal_dram_bw_after_urgent_strobe = in_soc_params->pct_ideal_dram_bw_after_urgent_strobe;
	out->pct_ideal_dram_bw_after_urgent_vm_only = in_soc_params->pct_ideal_dram_sdp_bw_after_urgent_vm_only;
	out->pct_ideal_fabric_bw_after_urgent = in_soc_params->pct_ideal_fabric_bw_after_urgent;
	out->pct_ideal_sdp_bw_after_urgent = in_soc_params->pct_ideal_sdp_bw_after_urgent;
	out->phy_downspread_percent = in_soc_params->downspread_percent;
	out->refclk_mhz = 50; // As per hardcoded reference.
	out->return_bus_width_bytes = in_soc_params->return_bus_width_bytes;
	out->round_trip_ping_latency_dcfclk_cycles = in_soc_params->round_trip_ping_latency_dcfclk_cycles;
	out->smn_latency_us = in_soc_params->smn_latency_us;
	out->urgent_out_of_order_return_per_channel_pixel_and_vm_bytes = in_soc_params->urgent_out_of_order_return_per_channel_pixel_and_vm_bytes;
	out->urgent_out_of_order_return_per_channel_pixel_only_bytes = in_soc_params->urgent_out_of_order_return_per_channel_pixel_only_bytes;
	out->urgent_out_of_order_return_per_channel_vm_only_bytes = in_soc_params->urgent_out_of_order_return_per_channel_vm_only_bytes;
	out->pct_ideal_dram_bw_after_urgent_pixel_and_vm = in_soc_params->pct_ideal_dram_sdp_bw_after_urgent_pixel_and_vm;
	out->pct_ideal_dram_bw_after_urgent_pixel_only = in_soc_params->pct_ideal_dram_sdp_bw_after_urgent_pixel_only;
	out->dcn_downspread_percent = in_soc_params->dcn_downspread_percent;
}

void dml2_translate_soc_states(const struct dc *dc, struct soc_states_st *out, int num_states)
{
	unsigned int i = 0;
	out->num_states = num_states;

	for (i = 0; i < out->num_states; i++) {
		out->state_array[i].dcfclk_mhz = dc->dml.soc.clock_limits[i].dcfclk_mhz;
		out->state_array[i].dispclk_mhz = dc->dml.soc.clock_limits[i].dispclk_mhz;
		out->state_array[i].dppclk_mhz = dc->dml.soc.clock_limits[i].dppclk_mhz;
		out->state_array[i].dram_speed_mts = dc->dml.soc.clock_limits[i].dram_speed_mts;
		out->state_array[i].dtbclk_mhz = dc->dml.soc.clock_limits[i].dtbclk_mhz;
		out->state_array[i].socclk_mhz = dc->dml.soc.clock_limits[i].socclk_mhz;
		out->state_array[i].fabricclk_mhz = dc->dml.soc.clock_limits[i].fabricclk_mhz;
		out->state_array[i].dscclk_mhz = dc->dml.soc.clock_limits[i].dscclk_mhz;
		out->state_array[i].phyclk_d18_mhz = dc->dml.soc.clock_limits[i].phyclk_d18_mhz;
		out->state_array[i].phyclk_d32_mhz = dc->dml.soc.clock_limits[i].phyclk_d32_mhz;
		out->state_array[i].phyclk_mhz = dc->dml.soc.clock_limits[i].phyclk_mhz;
		out->state_array[i].sr_enter_plus_exit_time_us = dc->dml.soc.sr_enter_plus_exit_time_us;
		out->state_array[i].sr_exit_time_us = dc->dml.soc.sr_exit_time_us;
		out->state_array[i].fclk_change_latency_us = dc->dml.soc.fclk_change_latency_us;
		out->state_array[i].dram_clock_change_latency_us = dc->dml.soc.dram_clock_change_latency_us;
		out->state_array[i].usr_retraining_latency_us = dc->dml.soc.usr_retraining_latency_us;
		out->state_array[i].writeback_latency_us = dc->dml.soc.writeback_latency_us;
		/* Driver initialized values for these are different than the spreadsheet. Use the
		 * spreadsheet ones for now. We need to decided which ones to use.
		 */
		out->state_array[i].sr_exit_z8_time_us = dc->dml.soc.sr_exit_z8_time_us;
		out->state_array[i].sr_enter_plus_exit_z8_time_us = dc->dml.soc.sr_enter_plus_exit_z8_time_us;
		//out->state_array[i].sr_exit_z8_time_us = 5.20;
		//out->state_array[i].sr_enter_plus_exit_z8_time_us = 9.60;
		out->state_array[i].use_ideal_dram_bw_strobe = true;
		out->state_array[i].urgent_latency_pixel_data_only_us = dc->dml.soc.urgent_latency_pixel_data_only_us;
		out->state_array[i].urgent_latency_pixel_mixed_with_vm_data_us = dc->dml.soc.urgent_latency_pixel_mixed_with_vm_data_us;
		out->state_array[i].urgent_latency_vm_data_only_us = dc->dml.soc.urgent_latency_vm_data_only_us;
		out->state_array[i].urgent_latency_adjustment_fabric_clock_component_us = dc->dml.soc.urgent_latency_adjustment_fabric_clock_component_us;
		out->state_array[i].urgent_latency_adjustment_fabric_clock_reference_mhz = dc->dml.soc.urgent_latency_adjustment_fabric_clock_reference_mhz;
	}
}

static void populate_dml_timing_cfg_from_stream_state(struct dml_timing_cfg_st *out, unsigned int location, const struct dc_stream_state *in)
{
	dml_uint_t hblank_start, vblank_start;

	out->HActive[location] = in->timing.h_addressable + in->timing.h_border_left + in->timing.h_border_right;
	out->VActive[location] = in->timing.v_addressable + in->timing.v_border_bottom + in->timing.v_border_top;
	out->RefreshRate[location] = ((in->timing.pix_clk_100hz * 100) / in->timing.h_total) / in->timing.v_total;
	out->VFrontPorch[location] = in->timing.v_front_porch;
	out->PixelClock[location] = in->timing.pix_clk_100hz / 10000.00;
	if (in->timing.timing_3d_format == TIMING_3D_FORMAT_HW_FRAME_PACKING)
		out->PixelClock[location] *= 2;
	out->HTotal[location] = in->timing.h_total;
	out->VTotal[location] = in->timing.v_total;
	out->Interlace[location] = in->timing.flags.INTERLACE;
	hblank_start = in->timing.h_total - in->timing.h_front_porch;
	out->HBlankEnd[location] = hblank_start
					- in->timing.h_addressable
					- in->timing.h_border_left
					- in->timing.h_border_right;
	vblank_start = in->timing.v_total - in->timing.v_front_porch;
	out->VBlankEnd[location] = vblank_start
					- in->timing.v_addressable
					- in->timing.v_border_top
					- in->timing.v_border_bottom;
	out->DRRDisplay[location] = false;
}

static void populate_dml_output_cfg_from_stream_state(struct dml_output_cfg_st *out, unsigned int location,
				const struct dc_stream_state *in, const struct pipe_ctx *pipe, struct dml2_context *dml2)
{
	unsigned int output_bpc;

	out->DSCEnable[location] = (enum dml_dsc_enable)in->timing.flags.DSC;
	out->OutputLinkDPLanes[location] = 4; // As per code in dcn20_resource.c
	out->DSCInputBitPerComponent[location] = 12; // As per code in dcn20_resource.c
	out->DSCSlices[location] = in->timing.dsc_cfg.num_slices_h;

	switch (in->signal) {
	case SIGNAL_TYPE_DISPLAY_PORT_MST:
	case SIGNAL_TYPE_DISPLAY_PORT:
		out->OutputEncoder[location] = dml_dp;
		if (location < MAX_HPO_DP2_ENCODERS && dml2->v20.scratch.hpo_stream_to_link_encoder_mapping[location] != -1)
			out->OutputEncoder[dml2->v20.scratch.hpo_stream_to_link_encoder_mapping[location]] = dml_dp2p0;
		break;
	case SIGNAL_TYPE_EDP:
		out->OutputEncoder[location] = dml_edp;
		break;
	case SIGNAL_TYPE_HDMI_TYPE_A:
	case SIGNAL_TYPE_DVI_SINGLE_LINK:
	case SIGNAL_TYPE_DVI_DUAL_LINK:
		out->OutputEncoder[location] = dml_hdmi;
		break;
	default:
		out->OutputEncoder[location] = dml_dp;
	}

	switch (in->timing.display_color_depth) {
	case COLOR_DEPTH_666:
		output_bpc = 6;
		break;
	case COLOR_DEPTH_888:
		output_bpc = 8;
		break;
	case COLOR_DEPTH_101010:
		output_bpc = 10;
		break;
	case COLOR_DEPTH_121212:
		output_bpc = 12;
		break;
	case COLOR_DEPTH_141414:
		output_bpc = 14;
		break;
	case COLOR_DEPTH_161616:
		output_bpc = 16;
		break;
	case COLOR_DEPTH_999:
		output_bpc = 9;
		break;
	case COLOR_DEPTH_111111:
		output_bpc = 11;
		break;
	default:
		output_bpc = 8;
		break;
	}

	switch (in->timing.pixel_encoding) {
	case PIXEL_ENCODING_RGB:
	case PIXEL_ENCODING_YCBCR444:
		out->OutputFormat[location] = dml_444;
		out->OutputBpp[location] = (dml_float_t)output_bpc * 3;
		break;
	case PIXEL_ENCODING_YCBCR420:
		out->OutputFormat[location] = dml_420;
		out->OutputBpp[location] = (output_bpc * 3.0) / 2;
		break;
	case PIXEL_ENCODING_YCBCR422:
		if (in->timing.flags.DSC && !in->timing.dsc_cfg.ycbcr422_simple)
			out->OutputFormat[location] = dml_n422;
		else
			out->OutputFormat[location] = dml_s422;
		out->OutputBpp[location] = (dml_float_t)output_bpc * 2;
		break;
	default:
		out->OutputFormat[location] = dml_444;
		out->OutputBpp[location] = (dml_float_t)output_bpc * 3;
		break;
	}

	if (in->timing.flags.DSC) {
		out->OutputBpp[location] = in->timing.dsc_cfg.bits_per_pixel / 16.0;
	}

	// This has been false throughout DCN32x development. If needed we can change this later on.
	out->OutputMultistreamEn[location] = false;

	switch (in->signal) {
	case SIGNAL_TYPE_NONE:
	case SIGNAL_TYPE_DVI_SINGLE_LINK:
	case SIGNAL_TYPE_DVI_DUAL_LINK:
	case SIGNAL_TYPE_HDMI_TYPE_A:
	case SIGNAL_TYPE_LVDS:
	case SIGNAL_TYPE_RGB:
	case SIGNAL_TYPE_DISPLAY_PORT:
	case SIGNAL_TYPE_DISPLAY_PORT_MST:
	case SIGNAL_TYPE_EDP:
	case SIGNAL_TYPE_VIRTUAL:
	default:
		out->OutputLinkDPRate[location] = dml_dp_rate_na;
		break;
	}

	out->PixelClockBackEnd[location] = in->timing.pix_clk_100hz / 10000.00;

	out->AudioSampleLayout[location] = in->audio_info.modes->sample_size;
	out->AudioSampleRate[location] = in->audio_info.modes->max_bit_rate;

	out->OutputDisabled[location] = true;
}

static void populate_dummy_dml_surface_cfg(struct dml_surface_cfg_st *out, unsigned int location, const struct dc_stream_state *in)
{
	out->SurfaceWidthY[location] = in->timing.h_addressable;
	out->SurfaceHeightY[location] = in->timing.v_addressable;
	out->SurfaceWidthC[location] = in->timing.h_addressable;
	out->SurfaceHeightC[location] = in->timing.v_addressable;
	out->PitchY[location] = ((out->SurfaceWidthY[location] + 127) / 128) * 128;
	out->PitchC[location] = 0;
	out->DCCEnable[location] = false;
	out->DCCMetaPitchY[location] = 0;
	out->DCCMetaPitchC[location] = 0;
	out->DCCRateLuma[location] = 1.0;
	out->DCCRateChroma[location] = 1.0;
	out->DCCFractionOfZeroSizeRequestsLuma[location] = 0;
	out->DCCFractionOfZeroSizeRequestsChroma[location] = 0;
	out->SurfaceTiling[location] = dml_sw_64kb_r_x;
	out->SourcePixelFormat[location] = dml_444_32;
}

static void populate_dml_surface_cfg_from_plane_state(enum dml_project_id dml2_project, struct dml_surface_cfg_st *out, unsigned int location, const struct dc_plane_state *in)
{
	out->PitchY[location] = in->plane_size.surface_pitch;
	out->SurfaceHeightY[location] = in->plane_size.surface_size.height;
	out->SurfaceWidthY[location] = in->plane_size.surface_size.width;
	out->SurfaceHeightC[location] = in->plane_size.chroma_size.height;
	out->SurfaceWidthC[location] = in->plane_size.chroma_size.width;
	out->PitchC[location] = in->plane_size.chroma_pitch;
	out->DCCEnable[location] = in->dcc.enable;
	out->DCCMetaPitchY[location] = in->dcc.meta_pitch;
	out->DCCMetaPitchC[location] = in->dcc.meta_pitch_c;
	out->DCCRateLuma[location] = 1.0;
	out->DCCRateChroma[location] = 1.0;
	out->DCCFractionOfZeroSizeRequestsLuma[location] = in->dcc.independent_64b_blks;
	out->DCCFractionOfZeroSizeRequestsChroma[location] = in->dcc.independent_64b_blks_c;

	switch (dml2_project) {
	default:
		out->SurfaceTiling[location] = (enum dml_swizzle_mode)in->tiling_info.gfx9.swizzle;
		break;
	case dml_project_dcn401:
		// Temporary use gfx11 swizzle in dml, until proper dml for DCN4x is integrated/implemented
		switch (in->tiling_info.gfx_addr3.swizzle) {
		case DC_ADDR3_SW_4KB_2D:
		case DC_ADDR3_SW_64KB_2D:
		case DC_ADDR3_SW_256KB_2D:
		default:
			out->SurfaceTiling[location] = dml_sw_64kb_r_x;
			break;
		case DC_ADDR3_SW_LINEAR:
			out->SurfaceTiling[location] = dml_sw_linear;
			break;
		}
	}

	switch (in->format) {
	case SURFACE_PIXEL_FORMAT_VIDEO_420_YCbCr:
	case SURFACE_PIXEL_FORMAT_VIDEO_420_YCrCb:
		out->SourcePixelFormat[location] = dml_420_8;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCbCr:
	case SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCrCb:
		out->SourcePixelFormat[location] = dml_420_10;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616:
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616:
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616F:
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616F:
		out->SourcePixelFormat[location] = dml_444_64;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB1555:
	case SURFACE_PIXEL_FORMAT_GRPH_RGB565:
		out->SourcePixelFormat[location] = dml_444_16;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_PALETA_256_COLORS:
		out->SourcePixelFormat[location] = dml_444_8;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_RGBE_ALPHA:
		out->SourcePixelFormat[location] = dml_rgbe_alpha;
		break;
	default:
		out->SourcePixelFormat[location] = dml_444_32;
		break;
	}
}

static struct scaler_data *get_scaler_data_for_plane(
		const struct dc_plane_state *in,
		struct dc_state *context)
{
	int i;
	struct pipe_ctx *temp_pipe = &context->res_ctx.temp_pipe;

	memset(temp_pipe, 0, sizeof(struct pipe_ctx));

	for (i = 0; i < MAX_PIPES; i++)	{
		const struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];

		if (pipe->plane_state == in && !pipe->prev_odm_pipe) {
			temp_pipe->stream = pipe->stream;
			temp_pipe->plane_state = pipe->plane_state;
			temp_pipe->plane_res.scl_data.taps = pipe->plane_res.scl_data.taps;
			temp_pipe->stream_res = pipe->stream_res;
			resource_build_scaling_params(temp_pipe);
			break;
		}
	}

	ASSERT(i < MAX_PIPES);
	return &temp_pipe->plane_res.scl_data;
}

static void populate_dummy_dml_plane_cfg(struct dml_plane_cfg_st *out, unsigned int location,
					 const struct dc_stream_state *in,
					 const struct soc_bounding_box_st *soc)
{
	dml_uint_t width, height;

	if (in->timing.h_addressable > 3840)
		width = 3840;
	else
		width = in->timing.h_addressable;	// 4K max

	if (in->timing.v_addressable > 2160)
		height = 2160;
	else
		height = in->timing.v_addressable;	// 4K max

	out->CursorBPP[location] = dml_cur_32bit;
	out->CursorWidth[location] = 256;

	out->GPUVMMinPageSizeKBytes[location] = soc->gpuvm_min_page_size_kbytes;

	out->ViewportWidth[location] = width;
	out->ViewportHeight[location] = height;
	out->ViewportStationary[location] = false;
	out->ViewportWidthChroma[location] = 0;
	out->ViewportHeightChroma[location] = 0;
	out->ViewportXStart[location] = 0;
	out->ViewportXStartC[location] = 0;
	out->ViewportYStart[location] = 0;
	out->ViewportYStartC[location] = 0;

	out->ScalerEnabled[location] = false;
	out->HRatio[location] = 1.0;
	out->VRatio[location] = 1.0;
	out->HRatioChroma[location] = 0;
	out->VRatioChroma[location] = 0;
	out->HTaps[location] = 1;
	out->VTaps[location] = 1;
	out->HTapsChroma[location] = 0;
	out->VTapsChroma[location] = 0;
	out->SourceScan[location] = dml_rotation_0;
	out->ScalerRecoutWidth[location] = width;

	out->LBBitPerPixel[location] = 57;

	out->DynamicMetadataEnable[location] = false;

	out->NumberOfCursors[location] = 1;
	out->UseMALLForStaticScreen[location] = dml_use_mall_static_screen_disable;
	out->UseMALLForPStateChange[location] = dml_use_mall_pstate_change_disable;

	out->DETSizeOverride[location] = 256;

	out->ScalerEnabled[location] = false;
}

static void populate_dml_plane_cfg_from_plane_state(struct dml_plane_cfg_st *out, unsigned int location,
						    const struct dc_plane_state *in, struct dc_state *context,
						    const struct soc_bounding_box_st *soc)
{
	struct scaler_data *scaler_data = get_scaler_data_for_plane(in, context);

	out->CursorBPP[location] = dml_cur_32bit;
	out->CursorWidth[location] = 256;

	out->GPUVMMinPageSizeKBytes[location] = soc->gpuvm_min_page_size_kbytes;

	out->ViewportWidth[location] = scaler_data->viewport.width;
	out->ViewportHeight[location] = scaler_data->viewport.height;
	out->ViewportWidthChroma[location] = scaler_data->viewport_c.width;
	out->ViewportHeightChroma[location] = scaler_data->viewport_c.height;
	out->ViewportXStart[location] = scaler_data->viewport.x;
	out->ViewportYStart[location] = scaler_data->viewport.y;
	out->ViewportXStartC[location] = scaler_data->viewport_c.x;
	out->ViewportYStartC[location] = scaler_data->viewport_c.y;
	out->ViewportStationary[location] = false;

	out->ScalerEnabled[location] = scaler_data->ratios.horz.value != dc_fixpt_one.value ||
				scaler_data->ratios.horz_c.value != dc_fixpt_one.value ||
				scaler_data->ratios.vert.value != dc_fixpt_one.value ||
				scaler_data->ratios.vert_c.value != dc_fixpt_one.value;

	/* Current driver code base uses LBBitPerPixel as 57. There is a discrepancy
	 * from the HW/DML teams about this value. Initialize LBBitPerPixel with the
	 * value current used in Navi3x .
	 */

	out->LBBitPerPixel[location] = 57;

	if (out->ScalerEnabled[location] == false) {
		out->HRatio[location] = 1;
		out->HRatioChroma[location] = 1;
		out->VRatio[location] = 1;
		out->VRatioChroma[location] = 1;
	} else {
		/* Follow the original dml_wrapper.c code direction to fix scaling issues */
		out->HRatio[location] = (dml_float_t)scaler_data->ratios.horz.value / (1ULL << 32);
		out->HRatioChroma[location] = (dml_float_t)scaler_data->ratios.horz_c.value / (1ULL << 32);
		out->VRatio[location] = (dml_float_t)scaler_data->ratios.vert.value / (1ULL << 32);
		out->VRatioChroma[location] = (dml_float_t)scaler_data->ratios.vert_c.value / (1ULL << 32);
	}

	if (!scaler_data->taps.h_taps) {
		out->HTaps[location] = 1;
		out->HTapsChroma[location] = 1;
	} else {
		out->HTaps[location] = scaler_data->taps.h_taps;
		out->HTapsChroma[location] = scaler_data->taps.h_taps_c;
	}
	if (!scaler_data->taps.v_taps) {
		out->VTaps[location] = 1;
		out->VTapsChroma[location] = 1;
	} else {
		out->VTaps[location] = scaler_data->taps.v_taps;
		out->VTapsChroma[location] = scaler_data->taps.v_taps_c;
	}

	out->SourceScan[location] = (enum dml_rotation_angle)in->rotation;
	out->ScalerRecoutWidth[location] = in->dst_rect.width;

	out->DynamicMetadataEnable[location] = false;
	out->DynamicMetadataLinesBeforeActiveRequired[location] = 0;
	out->DynamicMetadataTransmittedBytes[location] = 0;

	out->NumberOfCursors[location] = 1;
}

static unsigned int map_stream_to_dml_display_cfg(const struct dml2_context *dml2,
		const struct dc_stream_state *stream, const struct dml_display_cfg_st *dml_dispcfg)
{
	int i = 0;
	int location = -1;

	for (i = 0; i < __DML2_WRAPPER_MAX_STREAMS_PLANES__; i++) {
		if (dml2->v20.scratch.dml_to_dc_pipe_mapping.disp_cfg_to_stream_id_valid[i] && dml2->v20.scratch.dml_to_dc_pipe_mapping.disp_cfg_to_stream_id[i] == stream->stream_id) {
			location = i;
			break;
		}
	}

	return location;
}

static bool get_plane_id(struct dml2_context *dml2, const struct dc_state *context, const struct dc_plane_state *plane,
		unsigned int stream_id, unsigned int plane_index, unsigned int *plane_id)
{
	int i, j;
	bool is_plane_duplicate = dml2->v20.scratch.plane_duplicate_exists;

	if (!plane_id)
		return false;

	for (i = 0; i < context->stream_count; i++) {
		if (context->streams[i]->stream_id == stream_id) {
			for (j = 0; j < context->stream_status[i].plane_count; j++) {
				if (context->stream_status[i].plane_states[j] == plane &&
					(!is_plane_duplicate || (j == plane_index))) {
					*plane_id = (i << 16) | j;
					return true;
				}
			}
		}
	}

	return false;
}

static unsigned int map_plane_to_dml_display_cfg(const struct dml2_context *dml2, const struct dc_plane_state *plane,
		const struct dc_state *context, const struct dml_display_cfg_st *dml_dispcfg, unsigned int stream_id, int plane_index)
{
	unsigned int plane_id;
	int i = 0;
	int location = -1;

	if (!get_plane_id(context->bw_ctx.dml2, context, plane, stream_id, plane_index, &plane_id)) {
		ASSERT(false);
		return -1;
	}

	for (i = 0; i < __DML2_WRAPPER_MAX_STREAMS_PLANES__; i++) {
		if (dml2->v20.scratch.dml_to_dc_pipe_mapping.disp_cfg_to_plane_id_valid[i] && dml2->v20.scratch.dml_to_dc_pipe_mapping.disp_cfg_to_plane_id[i] == plane_id) {
			location = i;
			break;
		}
	}

	return location;
}

static void apply_legacy_svp_drr_settings(struct dml2_context *dml2, const struct dc_state *state, struct dml_display_cfg_st *dml_dispcfg)
{
	int i;

	if (state->bw_ctx.bw.dcn.clk.fw_based_mclk_switching) {
		ASSERT(state->stream_count == 1);
		dml_dispcfg->timing.DRRDisplay[0] = true;
	} else if (state->bw_ctx.bw.dcn.legacy_svp_drr_stream_index_valid) {

		for (i = 0; i < dml_dispcfg->num_timings; i++) {
			if (dml2->v20.scratch.dml_to_dc_pipe_mapping.disp_cfg_to_stream_id[i] == state->streams[state->bw_ctx.bw.dcn.legacy_svp_drr_stream_index]->stream_id)
				dml_dispcfg->timing.DRRDisplay[i] = true;
		}
	}
}

static void dml2_populate_pipe_to_plane_index_mapping(struct dml2_context *dml2, struct dc_state *state)
{
	unsigned int i;
	unsigned int pipe_index = 0;
	unsigned int plane_index = 0;
	struct dml2_dml_to_dc_pipe_mapping *dml_to_dc_pipe_mapping = &dml2->v20.scratch.dml_to_dc_pipe_mapping;

	for (i = 0; i < __DML2_WRAPPER_MAX_STREAMS_PLANES__; i++) {
		dml_to_dc_pipe_mapping->dml_pipe_idx_to_plane_index_valid[i] = false;
		dml_to_dc_pipe_mapping->dml_pipe_idx_to_plane_index[i] = 0;
	}

	for (i = 0; i < __DML2_WRAPPER_MAX_STREAMS_PLANES__; i++) {
		struct pipe_ctx *pipe = &state->res_ctx.pipe_ctx[i];

		if (!pipe || !pipe->stream || !pipe->plane_state)
			continue;

		while (pipe) {
			pipe_index = pipe->pipe_idx;

			if (pipe->stream && dml_to_dc_pipe_mapping->dml_pipe_idx_to_plane_index_valid[pipe_index] == false) {
				dml_to_dc_pipe_mapping->dml_pipe_idx_to_plane_index[pipe_index] = plane_index;
				plane_index++;
				dml_to_dc_pipe_mapping->dml_pipe_idx_to_plane_index_valid[pipe_index] = true;
			}

			pipe = pipe->bottom_pipe;
		}

		plane_index = 0;
	}
}

static void populate_dml_writeback_cfg_from_stream_state(struct dml_writeback_cfg_st *out,
		unsigned int location, const struct dc_stream_state *in)
{
	if (in->num_wb_info > 0) {
		for (int i = 0; i < __DML_NUM_DMB__; i++) {
			const struct dc_writeback_info *wb_info = &in->writeback_info[i];
			/*current dml support 1 dwb per stream, limitation*/
			if (wb_info->wb_enabled) {
				out->WritebackEnable[location] = wb_info->wb_enabled;
				out->ActiveWritebacksPerSurface[location] = wb_info->dwb_params.cnv_params.src_width;
				out->WritebackDestinationWidth[location] = wb_info->dwb_params.dest_width;
				out->WritebackDestinationHeight[location] = wb_info->dwb_params.dest_height;

				out->WritebackSourceWidth[location] = wb_info->dwb_params.cnv_params.crop_en ?
					wb_info->dwb_params.cnv_params.crop_width :
					wb_info->dwb_params.cnv_params.src_width;

				out->WritebackSourceHeight[location] = wb_info->dwb_params.cnv_params.crop_en ?
					wb_info->dwb_params.cnv_params.crop_height :
					wb_info->dwb_params.cnv_params.src_height;
				/*current design does not have chroma scaling, need to follow up*/
				out->WritebackHTaps[location] = wb_info->dwb_params.scaler_taps.h_taps > 0 ?
					wb_info->dwb_params.scaler_taps.h_taps : 1;
				out->WritebackVTaps[location] = wb_info->dwb_params.scaler_taps.v_taps > 0 ?
					wb_info->dwb_params.scaler_taps.v_taps : 1;

				out->WritebackHRatio[location] = wb_info->dwb_params.cnv_params.crop_en ?
					(double)wb_info->dwb_params.cnv_params.crop_width /
						(double)wb_info->dwb_params.dest_width :
					(double)wb_info->dwb_params.cnv_params.src_width /
						(double)wb_info->dwb_params.dest_width;
				out->WritebackVRatio[location] = wb_info->dwb_params.cnv_params.crop_en ?
					(double)wb_info->dwb_params.cnv_params.crop_height /
						(double)wb_info->dwb_params.dest_height :
					(double)wb_info->dwb_params.cnv_params.src_height /
						(double)wb_info->dwb_params.dest_height;
			}
		}
	}
}

static void dml2_map_hpo_stream_encoder_to_hpo_link_encoder_index(struct dml2_context *dml2, struct dc_state *context)
{
	int i;
	struct pipe_ctx *current_pipe_context;

	/* Scratch gets reset to zero in dml, but link encoder instance can be zero, so reset to -1 */
	for (i = 0; i < MAX_HPO_DP2_ENCODERS; i++) {
		dml2->v20.scratch.hpo_stream_to_link_encoder_mapping[i] = -1;
	}

	/* If an HPO stream encoder is allocated to a pipe, get the instance of it's allocated HPO Link encoder */
	for (i = 0; i < MAX_PIPES; i++) {
		current_pipe_context = &context->res_ctx.pipe_ctx[i];
		if (current_pipe_context->stream &&
			current_pipe_context->stream_res.hpo_dp_stream_enc &&
			current_pipe_context->link_res.hpo_dp_link_enc &&
			dc_is_dp_signal(current_pipe_context->stream->signal)) {
				dml2->v20.scratch.hpo_stream_to_link_encoder_mapping[current_pipe_context->stream_res.hpo_dp_stream_enc->inst] =
					current_pipe_context->link_res.hpo_dp_link_enc->inst;
			}
	}
}

void map_dc_state_into_dml_display_cfg(struct dml2_context *dml2, struct dc_state *context, struct dml_display_cfg_st *dml_dispcfg)
{
	int i = 0, j = 0, k = 0;
	int disp_cfg_stream_location, disp_cfg_plane_location;
	enum mall_stream_type stream_mall_type;
	struct pipe_ctx *current_pipe_context;

	for (i = 0; i < __DML2_WRAPPER_MAX_STREAMS_PLANES__; i++) {
		dml2->v20.scratch.dml_to_dc_pipe_mapping.disp_cfg_to_stream_id_valid[i] = false;
		dml2->v20.scratch.dml_to_dc_pipe_mapping.disp_cfg_to_plane_id_valid[i] = false;
		dml2->v20.scratch.dml_to_dc_pipe_mapping.dml_pipe_idx_to_stream_id_valid[i] = false;
		dml2->v20.scratch.dml_to_dc_pipe_mapping.dml_pipe_idx_to_plane_id_valid[i] = false;
	}

	//Generally these are set by referencing our latest BB/IP params in dcn32_resource.c file
	dml_dispcfg->plane.GPUVMEnable = dml2->v20.dml_core_ctx.ip.gpuvm_enable;
	dml_dispcfg->plane.GPUVMMaxPageTableLevels = dml2->v20.dml_core_ctx.ip.gpuvm_max_page_table_levels;
	dml_dispcfg->plane.HostVMEnable = dml2->v20.dml_core_ctx.ip.hostvm_enable;
	dml_dispcfg->plane.HostVMMaxPageTableLevels = dml2->v20.dml_core_ctx.ip.hostvm_max_page_table_levels;
	if (dml2->v20.dml_core_ctx.ip.hostvm_enable)
		dml2->v20.dml_core_ctx.policy.AllowForPStateChangeOrStutterInVBlankFinal = dml_prefetch_support_uclk_fclk_and_stutter;

	dml2_populate_pipe_to_plane_index_mapping(dml2, context);
	dml2_map_hpo_stream_encoder_to_hpo_link_encoder_index(dml2, context);

	for (i = 0; i < context->stream_count; i++) {
		current_pipe_context = NULL;
		for (k = 0; k < MAX_PIPES; k++) {
			/* find one pipe allocated to this stream for the purpose of getting
			info about the link later */
			if (context->streams[i] == context->res_ctx.pipe_ctx[k].stream) {
				current_pipe_context = &context->res_ctx.pipe_ctx[k];
				break;
			}
		}
		disp_cfg_stream_location = map_stream_to_dml_display_cfg(dml2, context->streams[i], dml_dispcfg);
		stream_mall_type = dc_state_get_stream_subvp_type(context, context->streams[i]);

		if (disp_cfg_stream_location < 0)
			disp_cfg_stream_location = dml_dispcfg->num_timings++;

		ASSERT(disp_cfg_stream_location >= 0 && disp_cfg_stream_location < __DML2_WRAPPER_MAX_STREAMS_PLANES__);

		populate_dml_timing_cfg_from_stream_state(&dml_dispcfg->timing, disp_cfg_stream_location, context->streams[i]);
		populate_dml_output_cfg_from_stream_state(&dml_dispcfg->output, disp_cfg_stream_location, context->streams[i], current_pipe_context, dml2);
		/*Call site for populate_dml_writeback_cfg_from_stream_state*/
		populate_dml_writeback_cfg_from_stream_state(&dml_dispcfg->writeback,
			disp_cfg_stream_location, context->streams[i]);

		switch (context->streams[i]->debug.force_odm_combine_segments) {
		case 2:
			dml2->v20.dml_core_ctx.policy.ODMUse[disp_cfg_stream_location] = dml_odm_use_policy_combine_2to1;
			break;
		case 4:
			dml2->v20.dml_core_ctx.policy.ODMUse[disp_cfg_stream_location] = dml_odm_use_policy_combine_4to1;
			break;
		default:
			break;
		}

		dml2->v20.scratch.dml_to_dc_pipe_mapping.disp_cfg_to_stream_id[disp_cfg_stream_location] = context->streams[i]->stream_id;
		dml2->v20.scratch.dml_to_dc_pipe_mapping.disp_cfg_to_stream_id_valid[disp_cfg_stream_location] = true;

		if (context->stream_status[i].plane_count == 0) {
			disp_cfg_plane_location = dml_dispcfg->num_surfaces++;

			populate_dummy_dml_surface_cfg(&dml_dispcfg->surface, disp_cfg_plane_location, context->streams[i]);
			populate_dummy_dml_plane_cfg(&dml_dispcfg->plane, disp_cfg_plane_location,
						     context->streams[i], &dml2->v20.dml_core_ctx.soc);

			dml_dispcfg->plane.BlendingAndTiming[disp_cfg_plane_location] = disp_cfg_stream_location;

			dml2->v20.scratch.dml_to_dc_pipe_mapping.disp_cfg_to_plane_id_valid[disp_cfg_plane_location] = true;
		} else {
			for (j = 0; j < context->stream_status[i].plane_count; j++) {
				disp_cfg_plane_location = map_plane_to_dml_display_cfg(dml2,
					context->stream_status[i].plane_states[j], context, dml_dispcfg, context->streams[i]->stream_id, j);

				if (disp_cfg_plane_location < 0)
					disp_cfg_plane_location = dml_dispcfg->num_surfaces++;

				ASSERT(disp_cfg_plane_location >= 0 && disp_cfg_plane_location < __DML2_WRAPPER_MAX_STREAMS_PLANES__);

				populate_dml_surface_cfg_from_plane_state(dml2->v20.dml_core_ctx.project, &dml_dispcfg->surface, disp_cfg_plane_location, context->stream_status[i].plane_states[j]);
				populate_dml_plane_cfg_from_plane_state(
					&dml_dispcfg->plane, disp_cfg_plane_location,
					context->stream_status[i].plane_states[j], context,
					&dml2->v20.dml_core_ctx.soc);

				if (stream_mall_type == SUBVP_MAIN) {
					dml_dispcfg->plane.UseMALLForPStateChange[disp_cfg_plane_location] = dml_use_mall_pstate_change_sub_viewport;
					dml_dispcfg->plane.UseMALLForStaticScreen[disp_cfg_plane_location] = dml_use_mall_static_screen_optimize;
				} else if (stream_mall_type == SUBVP_PHANTOM) {
					dml_dispcfg->plane.UseMALLForPStateChange[disp_cfg_plane_location] = dml_use_mall_pstate_change_phantom_pipe;
					dml_dispcfg->plane.UseMALLForStaticScreen[disp_cfg_plane_location] = dml_use_mall_static_screen_disable;
					dml2->v20.dml_core_ctx.policy.ImmediateFlipRequirement[disp_cfg_plane_location] = dml_immediate_flip_not_required;
				} else {
					dml_dispcfg->plane.UseMALLForPStateChange[disp_cfg_plane_location] = dml_use_mall_pstate_change_disable;
					dml_dispcfg->plane.UseMALLForStaticScreen[disp_cfg_plane_location] = dml_use_mall_static_screen_optimize;
				}

				dml_dispcfg->plane.BlendingAndTiming[disp_cfg_plane_location] = disp_cfg_stream_location;

				if (get_plane_id(dml2, context, context->stream_status[i].plane_states[j], context->streams[i]->stream_id, j,
					&dml2->v20.scratch.dml_to_dc_pipe_mapping.disp_cfg_to_plane_id[disp_cfg_plane_location]))
					dml2->v20.scratch.dml_to_dc_pipe_mapping.disp_cfg_to_plane_id_valid[disp_cfg_plane_location] = true;

				if (j >= 1) {
					populate_dml_timing_cfg_from_stream_state(&dml_dispcfg->timing, disp_cfg_plane_location, context->streams[i]);
					populate_dml_output_cfg_from_stream_state(&dml_dispcfg->output, disp_cfg_plane_location, context->streams[i], current_pipe_context, dml2);
					switch (context->streams[i]->debug.force_odm_combine_segments) {
					case 2:
						dml2->v20.dml_core_ctx.policy.ODMUse[disp_cfg_plane_location] = dml_odm_use_policy_combine_2to1;
						break;
					case 4:
						dml2->v20.dml_core_ctx.policy.ODMUse[disp_cfg_plane_location] = dml_odm_use_policy_combine_4to1;
						break;
					default:
						break;
					}

					if (stream_mall_type == SUBVP_MAIN)
						dml_dispcfg->plane.UseMALLForPStateChange[disp_cfg_plane_location] = dml_use_mall_pstate_change_sub_viewport;
					else if (stream_mall_type == SUBVP_PHANTOM)
						dml_dispcfg->plane.UseMALLForPStateChange[disp_cfg_plane_location] = dml_use_mall_pstate_change_phantom_pipe;

					dml2->v20.scratch.dml_to_dc_pipe_mapping.disp_cfg_to_stream_id[disp_cfg_plane_location] = context->streams[i]->stream_id;
					dml2->v20.scratch.dml_to_dc_pipe_mapping.disp_cfg_to_stream_id_valid[disp_cfg_plane_location] = true;

					dml_dispcfg->num_timings++;
				}
			}
		}
	}

	if (!dml2->config.use_native_pstate_optimization)
		apply_legacy_svp_drr_settings(dml2, context, dml_dispcfg);
}

void dml2_update_pipe_ctx_dchub_regs(struct _vcs_dpi_dml_display_rq_regs_st *rq_regs,
	struct _vcs_dpi_dml_display_dlg_regs_st *disp_dlg_regs,
	struct _vcs_dpi_dml_display_ttu_regs_st *disp_ttu_regs,
	struct pipe_ctx *out)
{
	memset(&out->rq_regs, 0, sizeof(out->rq_regs));
	out->rq_regs.rq_regs_l.chunk_size = rq_regs->rq_regs_l.chunk_size;
	out->rq_regs.rq_regs_l.min_chunk_size = rq_regs->rq_regs_l.min_chunk_size;
	out->rq_regs.rq_regs_l.meta_chunk_size = rq_regs->rq_regs_l.meta_chunk_size;
	out->rq_regs.rq_regs_l.min_meta_chunk_size = rq_regs->rq_regs_l.min_meta_chunk_size;
	out->rq_regs.rq_regs_l.dpte_group_size = rq_regs->rq_regs_l.dpte_group_size;
	out->rq_regs.rq_regs_l.mpte_group_size = rq_regs->rq_regs_l.mpte_group_size;
	out->rq_regs.rq_regs_l.swath_height = rq_regs->rq_regs_l.swath_height;
	out->rq_regs.rq_regs_l.pte_row_height_linear = rq_regs->rq_regs_l.pte_row_height_linear;

	out->rq_regs.rq_regs_c.chunk_size = rq_regs->rq_regs_c.chunk_size;
	out->rq_regs.rq_regs_c.min_chunk_size = rq_regs->rq_regs_c.min_chunk_size;
	out->rq_regs.rq_regs_c.meta_chunk_size = rq_regs->rq_regs_c.meta_chunk_size;
	out->rq_regs.rq_regs_c.min_meta_chunk_size = rq_regs->rq_regs_c.min_meta_chunk_size;
	out->rq_regs.rq_regs_c.dpte_group_size = rq_regs->rq_regs_c.dpte_group_size;
	out->rq_regs.rq_regs_c.mpte_group_size = rq_regs->rq_regs_c.mpte_group_size;
	out->rq_regs.rq_regs_c.swath_height = rq_regs->rq_regs_c.swath_height;
	out->rq_regs.rq_regs_c.pte_row_height_linear = rq_regs->rq_regs_c.pte_row_height_linear;

	out->rq_regs.drq_expansion_mode = rq_regs->drq_expansion_mode;
	out->rq_regs.prq_expansion_mode = rq_regs->prq_expansion_mode;
	out->rq_regs.mrq_expansion_mode = rq_regs->mrq_expansion_mode;
	out->rq_regs.crq_expansion_mode = rq_regs->crq_expansion_mode;
	out->rq_regs.plane1_base_address = rq_regs->plane1_base_address;

	memset(&out->dlg_regs, 0, sizeof(out->dlg_regs));
	out->dlg_regs.refcyc_h_blank_end = disp_dlg_regs->refcyc_h_blank_end;
	out->dlg_regs.dlg_vblank_end = disp_dlg_regs->dlg_vblank_end;
	out->dlg_regs.min_dst_y_next_start = disp_dlg_regs->min_dst_y_next_start;
	out->dlg_regs.refcyc_per_htotal = disp_dlg_regs->refcyc_per_htotal;
	out->dlg_regs.refcyc_x_after_scaler = disp_dlg_regs->refcyc_x_after_scaler;
	out->dlg_regs.dst_y_after_scaler = disp_dlg_regs->dst_y_after_scaler;
	out->dlg_regs.dst_y_prefetch = disp_dlg_regs->dst_y_prefetch;
	out->dlg_regs.dst_y_per_vm_vblank = disp_dlg_regs->dst_y_per_vm_vblank;
	out->dlg_regs.dst_y_per_row_vblank = disp_dlg_regs->dst_y_per_row_vblank;
	out->dlg_regs.dst_y_per_vm_flip = disp_dlg_regs->dst_y_per_vm_flip;
	out->dlg_regs.dst_y_per_row_flip = disp_dlg_regs->dst_y_per_row_flip;
	out->dlg_regs.ref_freq_to_pix_freq = disp_dlg_regs->ref_freq_to_pix_freq;
	out->dlg_regs.vratio_prefetch = disp_dlg_regs->vratio_prefetch;
	out->dlg_regs.vratio_prefetch_c = disp_dlg_regs->vratio_prefetch_c;
	out->dlg_regs.refcyc_per_pte_group_vblank_l = disp_dlg_regs->refcyc_per_pte_group_vblank_l;
	out->dlg_regs.refcyc_per_pte_group_vblank_c = disp_dlg_regs->refcyc_per_pte_group_vblank_c;
	out->dlg_regs.refcyc_per_meta_chunk_vblank_l = disp_dlg_regs->refcyc_per_meta_chunk_vblank_l;
	out->dlg_regs.refcyc_per_meta_chunk_vblank_c = disp_dlg_regs->refcyc_per_meta_chunk_vblank_c;
	out->dlg_regs.refcyc_per_pte_group_flip_l = disp_dlg_regs->refcyc_per_pte_group_flip_l;
	out->dlg_regs.refcyc_per_pte_group_flip_c = disp_dlg_regs->refcyc_per_pte_group_flip_c;
	out->dlg_regs.refcyc_per_meta_chunk_flip_l = disp_dlg_regs->refcyc_per_meta_chunk_flip_l;
	out->dlg_regs.refcyc_per_meta_chunk_flip_c = disp_dlg_regs->refcyc_per_meta_chunk_flip_c;
	out->dlg_regs.dst_y_per_pte_row_nom_l = disp_dlg_regs->dst_y_per_pte_row_nom_l;
	out->dlg_regs.dst_y_per_pte_row_nom_c = disp_dlg_regs->dst_y_per_pte_row_nom_c;
	out->dlg_regs.refcyc_per_pte_group_nom_l = disp_dlg_regs->refcyc_per_pte_group_nom_l;
	out->dlg_regs.refcyc_per_pte_group_nom_c = disp_dlg_regs->refcyc_per_pte_group_nom_c;
	out->dlg_regs.dst_y_per_meta_row_nom_l = disp_dlg_regs->dst_y_per_meta_row_nom_l;
	out->dlg_regs.dst_y_per_meta_row_nom_c = disp_dlg_regs->dst_y_per_meta_row_nom_c;
	out->dlg_regs.refcyc_per_meta_chunk_nom_l = disp_dlg_regs->refcyc_per_meta_chunk_nom_l;
	out->dlg_regs.refcyc_per_meta_chunk_nom_c = disp_dlg_regs->refcyc_per_meta_chunk_nom_c;
	out->dlg_regs.refcyc_per_line_delivery_pre_l = disp_dlg_regs->refcyc_per_line_delivery_pre_l;
	out->dlg_regs.refcyc_per_line_delivery_pre_c = disp_dlg_regs->refcyc_per_line_delivery_pre_c;
	out->dlg_regs.refcyc_per_line_delivery_l = disp_dlg_regs->refcyc_per_line_delivery_l;
	out->dlg_regs.refcyc_per_line_delivery_c = disp_dlg_regs->refcyc_per_line_delivery_c;
	out->dlg_regs.refcyc_per_vm_group_vblank = disp_dlg_regs->refcyc_per_vm_group_vblank;
	out->dlg_regs.refcyc_per_vm_group_flip = disp_dlg_regs->refcyc_per_vm_group_flip;
	out->dlg_regs.refcyc_per_vm_req_vblank = disp_dlg_regs->refcyc_per_vm_req_vblank;
	out->dlg_regs.refcyc_per_vm_req_flip = disp_dlg_regs->refcyc_per_vm_req_flip;
	out->dlg_regs.dst_y_offset_cur0 = disp_dlg_regs->dst_y_offset_cur0;
	out->dlg_regs.chunk_hdl_adjust_cur0 = disp_dlg_regs->chunk_hdl_adjust_cur0;
	out->dlg_regs.dst_y_offset_cur1 = disp_dlg_regs->dst_y_offset_cur1;
	out->dlg_regs.chunk_hdl_adjust_cur1 = disp_dlg_regs->chunk_hdl_adjust_cur1;
	out->dlg_regs.vready_after_vcount0 = disp_dlg_regs->vready_after_vcount0;
	out->dlg_regs.dst_y_delta_drq_limit = disp_dlg_regs->dst_y_delta_drq_limit;
	out->dlg_regs.refcyc_per_vm_dmdata = disp_dlg_regs->refcyc_per_vm_dmdata;
	out->dlg_regs.dmdata_dl_delta = disp_dlg_regs->dmdata_dl_delta;

	memset(&out->ttu_regs, 0, sizeof(out->ttu_regs));
	out->ttu_regs.qos_level_low_wm = disp_ttu_regs->qos_level_low_wm;
	out->ttu_regs.qos_level_high_wm = disp_ttu_regs->qos_level_high_wm;
	out->ttu_regs.min_ttu_vblank = disp_ttu_regs->min_ttu_vblank;
	out->ttu_regs.qos_level_flip = disp_ttu_regs->qos_level_flip;
	out->ttu_regs.refcyc_per_req_delivery_l = disp_ttu_regs->refcyc_per_req_delivery_l;
	out->ttu_regs.refcyc_per_req_delivery_c = disp_ttu_regs->refcyc_per_req_delivery_c;
	out->ttu_regs.refcyc_per_req_delivery_cur0 = disp_ttu_regs->refcyc_per_req_delivery_cur0;
	out->ttu_regs.refcyc_per_req_delivery_cur1 = disp_ttu_regs->refcyc_per_req_delivery_cur1;
	out->ttu_regs.refcyc_per_req_delivery_pre_l = disp_ttu_regs->refcyc_per_req_delivery_pre_l;
	out->ttu_regs.refcyc_per_req_delivery_pre_c = disp_ttu_regs->refcyc_per_req_delivery_pre_c;
	out->ttu_regs.refcyc_per_req_delivery_pre_cur0 = disp_ttu_regs->refcyc_per_req_delivery_pre_cur0;
	out->ttu_regs.refcyc_per_req_delivery_pre_cur1 = disp_ttu_regs->refcyc_per_req_delivery_pre_cur1;
	out->ttu_regs.qos_level_fixed_l = disp_ttu_regs->qos_level_fixed_l;
	out->ttu_regs.qos_level_fixed_c = disp_ttu_regs->qos_level_fixed_c;
	out->ttu_regs.qos_level_fixed_cur0 = disp_ttu_regs->qos_level_fixed_cur0;
	out->ttu_regs.qos_level_fixed_cur1 = disp_ttu_regs->qos_level_fixed_cur1;
	out->ttu_regs.qos_ramp_disable_l = disp_ttu_regs->qos_ramp_disable_l;
	out->ttu_regs.qos_ramp_disable_c = disp_ttu_regs->qos_ramp_disable_c;
	out->ttu_regs.qos_ramp_disable_cur0 = disp_ttu_regs->qos_ramp_disable_cur0;
	out->ttu_regs.qos_ramp_disable_cur1 = disp_ttu_regs->qos_ramp_disable_cur1;
}
