// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#include "dm_services.h"
#include "dm_helpers.h"
#include "core_types.h"
#include "resource.h"
#include "dccg.h"
#include "dce/dce_hwseq.h"
#include "reg_helper.h"
#include "abm.h"
#include "hubp.h"
#include "dchubbub.h"
#include "timing_generator.h"
#include "opp.h"
#include "ipp.h"
#include "mpc.h"
#include "mcif_wb.h"
#include "dc_dmub_srv.h"
#include "link_hwss.h"
#include "dpcd_defs.h"
#include "clk_mgr.h"
#include "dsc.h"
#include "link.h"

#include "dce/dmub_hw_lock_mgr.h"
#include "dcn10/dcn10_cm_common.h"
#include "dcn20/dcn20_optc.h"
#include "dcn30/dcn30_cm_common.h"
#include "dcn32/dcn32_hwseq.h"
#include "dcn401_hwseq.h"
#include "dcn401/dcn401_resource.h"
#include "dc_state_priv.h"
#include "link_enc_cfg.h"

#define DC_LOGGER_INIT(logger)

#define CTX \
	hws->ctx
#define REG(reg)\
	hws->regs->reg
#define DC_LOGGER \
	dc->ctx->logger


#undef FN
#define FN(reg_name, field_name) \
	hws->shifts->field_name, hws->masks->field_name

static void dcn401_initialize_min_clocks(struct dc *dc)
{
	struct dc_clocks *clocks = &dc->current_state->bw_ctx.bw.dcn.clk;

	clocks->dcfclk_deep_sleep_khz = DCN3_2_DCFCLK_DS_INIT_KHZ;
	clocks->dcfclk_khz = dc->clk_mgr->bw_params->clk_table.entries[0].dcfclk_mhz * 1000;
	clocks->socclk_khz = dc->clk_mgr->bw_params->clk_table.entries[0].socclk_mhz * 1000;
	clocks->dramclk_khz = dc->clk_mgr->bw_params->clk_table.entries[0].memclk_mhz * 1000;
	clocks->dppclk_khz = dc->clk_mgr->bw_params->clk_table.entries[0].dppclk_mhz * 1000;
	if (dc->debug.disable_boot_optimizations) {
		clocks->dispclk_khz = dc->clk_mgr->bw_params->clk_table.entries[0].dispclk_mhz * 1000;
	} else {
		/* Even though DPG_EN = 1 for the connected display, it still requires the
		 * correct timing so we cannot set DISPCLK to min freq or it could cause
		 * audio corruption. Read current DISPCLK from DENTIST and request the same
		 * freq to ensure that the timing is valid and unchanged.
		 */
		clocks->dispclk_khz = dc->clk_mgr->funcs->get_dispclk_from_dentist(dc->clk_mgr);
	}
	clocks->ref_dtbclk_khz = dc->clk_mgr->bw_params->clk_table.entries[0].dtbclk_mhz * 1000;
	clocks->fclk_p_state_change_support = true;
	clocks->p_state_change_support = true;

	dc->clk_mgr->funcs->update_clocks(
			dc->clk_mgr,
			dc->current_state,
			true);
}

void dcn401_program_gamut_remap(struct pipe_ctx *pipe_ctx)
{
	unsigned int i = 0;
	struct mpc_grph_gamut_adjustment mpc_adjust;
	unsigned int mpcc_id = pipe_ctx->plane_res.mpcc_inst;
	struct mpc *mpc = pipe_ctx->stream_res.opp->ctx->dc->res_pool->mpc;

	//For now assert if location is not pre-blend
	if (pipe_ctx->plane_state)
		ASSERT(pipe_ctx->plane_state->mcm_location == MPCC_MOVABLE_CM_LOCATION_BEFORE);

	// program MPCC_MCM_FIRST_GAMUT_REMAP
	memset(&mpc_adjust, 0, sizeof(mpc_adjust));
	mpc_adjust.gamut_adjust_type = GRAPHICS_GAMUT_ADJUST_TYPE_BYPASS;
	mpc_adjust.mpcc_gamut_remap_block_id = MPCC_MCM_FIRST_GAMUT_REMAP;

	if (pipe_ctx->plane_state &&
		pipe_ctx->plane_state->gamut_remap_matrix.enable_remap == true) {
		mpc_adjust.gamut_adjust_type = GRAPHICS_GAMUT_ADJUST_TYPE_SW;
		for (i = 0; i < CSC_TEMPERATURE_MATRIX_SIZE; i++)
			mpc_adjust.temperature_matrix[i] =
			pipe_ctx->plane_state->gamut_remap_matrix.matrix[i];
	}

	mpc->funcs->set_gamut_remap(mpc, mpcc_id, &mpc_adjust);

	// program MPCC_MCM_SECOND_GAMUT_REMAP for Bypass / Disable for now
	mpc_adjust.gamut_adjust_type = GRAPHICS_GAMUT_ADJUST_TYPE_BYPASS;
	mpc_adjust.mpcc_gamut_remap_block_id = MPCC_MCM_SECOND_GAMUT_REMAP;

	mpc->funcs->set_gamut_remap(mpc, mpcc_id, &mpc_adjust);

	// program MPCC_OGAM_GAMUT_REMAP same as is currently used on DCN3x
	memset(&mpc_adjust, 0, sizeof(mpc_adjust));
	mpc_adjust.gamut_adjust_type = GRAPHICS_GAMUT_ADJUST_TYPE_BYPASS;
	mpc_adjust.mpcc_gamut_remap_block_id = MPCC_OGAM_GAMUT_REMAP;

	if (pipe_ctx->top_pipe == NULL) {
		if (pipe_ctx->stream->gamut_remap_matrix.enable_remap == true) {
			mpc_adjust.gamut_adjust_type = GRAPHICS_GAMUT_ADJUST_TYPE_SW;
			for (i = 0; i < CSC_TEMPERATURE_MATRIX_SIZE; i++)
				mpc_adjust.temperature_matrix[i] =
				pipe_ctx->stream->gamut_remap_matrix.matrix[i];
		}
	}

	mpc->funcs->set_gamut_remap(mpc, mpcc_id, &mpc_adjust);
}

struct ips_ono_region_state dcn401_read_ono_state(struct dc *dc, uint8_t region)
{
	struct dce_hwseq *hws = dc->hwseq;
	struct ips_ono_region_state state = {0, 0};

	switch (region) {
	case 0:
		/* dccg, dio, dcio */
		REG_GET_2(DOMAIN22_PG_STATUS,
			DOMAIN_DESIRED_PWR_STATE, &state.desire_pwr_state,
			DOMAIN_PGFSM_PWR_STATUS, &state.current_pwr_state);
		break;
	case 1:
		/* dchubbub, dchvm, dchubbubmem */
		REG_GET_2(DOMAIN23_PG_STATUS,
			DOMAIN_DESIRED_PWR_STATE, &state.desire_pwr_state,
			DOMAIN_PGFSM_PWR_STATUS, &state.current_pwr_state);
		break;
	case 2:
		/* mpc, opp, optc, dwb */
		REG_GET_2(DOMAIN24_PG_STATUS,
			DOMAIN_DESIRED_PWR_STATE, &state.desire_pwr_state,
			DOMAIN_PGFSM_PWR_STATUS, &state.current_pwr_state);
		break;
	case 3:
		/* hpo */
		REG_GET_2(DOMAIN25_PG_STATUS,
			DOMAIN_DESIRED_PWR_STATE, &state.desire_pwr_state,
			DOMAIN_PGFSM_PWR_STATUS, &state.current_pwr_state);
		break;
	case 4:
		/* dchubp0, dpp0 */
		REG_GET_2(DOMAIN0_PG_STATUS,
			DOMAIN_DESIRED_PWR_STATE, &state.desire_pwr_state,
			DOMAIN_PGFSM_PWR_STATUS, &state.current_pwr_state);
		break;
	case 5:
		/* dsc0 */
		REG_GET_2(DOMAIN16_PG_STATUS,
			DOMAIN_DESIRED_PWR_STATE, &state.desire_pwr_state,
			DOMAIN_PGFSM_PWR_STATUS, &state.current_pwr_state);
		break;
	case 6:
		/* dchubp1, dpp1 */
		REG_GET_2(DOMAIN1_PG_STATUS,
			DOMAIN_DESIRED_PWR_STATE, &state.desire_pwr_state,
			DOMAIN_PGFSM_PWR_STATUS, &state.current_pwr_state);
		break;
	case 7:
		/* dsc1 */
		REG_GET_2(DOMAIN17_PG_STATUS,
			DOMAIN_DESIRED_PWR_STATE, &state.desire_pwr_state,
			DOMAIN_PGFSM_PWR_STATUS, &state.current_pwr_state);
		break;
	case 8:
		/* dchubp2, dpp2 */
		REG_GET_2(DOMAIN2_PG_STATUS,
			DOMAIN_DESIRED_PWR_STATE, &state.desire_pwr_state,
			DOMAIN_PGFSM_PWR_STATUS, &state.current_pwr_state);
		break;
	case 9:
		/* dsc2 */
		REG_GET_2(DOMAIN18_PG_STATUS,
			DOMAIN_DESIRED_PWR_STATE, &state.desire_pwr_state,
			DOMAIN_PGFSM_PWR_STATUS, &state.current_pwr_state);
		break;
	case 10:
		/* dchubp3, dpp3 */
		REG_GET_2(DOMAIN3_PG_STATUS,
			DOMAIN_DESIRED_PWR_STATE, &state.desire_pwr_state,
			DOMAIN_PGFSM_PWR_STATUS, &state.current_pwr_state);
		break;
	case 11:
		/* dsc3 */
		REG_GET_2(DOMAIN19_PG_STATUS,
			DOMAIN_DESIRED_PWR_STATE, &state.desire_pwr_state,
			DOMAIN_PGFSM_PWR_STATUS, &state.current_pwr_state);
		break;
	default:
		break;
	}

	return state;
}

void dcn401_init_hw(struct dc *dc)
{
	struct abm **abms = dc->res_pool->multiple_abms;
	struct dce_hwseq *hws = dc->hwseq;
	struct dc_bios *dcb = dc->ctx->dc_bios;
	struct resource_pool *res_pool = dc->res_pool;
	int i;
	int edp_num;
	uint32_t backlight = MAX_BACKLIGHT_LEVEL;
	uint32_t user_level = MAX_BACKLIGHT_LEVEL;
	int current_dchub_ref_freq = 0;

	if (dc->clk_mgr && dc->clk_mgr->funcs && dc->clk_mgr->funcs->init_clocks) {
		dc->clk_mgr->funcs->init_clocks(dc->clk_mgr);

		// mark dcmode limits present if any clock has distinct AC and DC values from SMU
		dc->caps.dcmode_power_limits_present =
				(dc->clk_mgr->bw_params->clk_table.num_entries_per_clk.num_dcfclk_levels && dc->clk_mgr->bw_params->dc_mode_limit.dcfclk_mhz) ||
				(dc->clk_mgr->bw_params->clk_table.num_entries_per_clk.num_dispclk_levels && dc->clk_mgr->bw_params->dc_mode_limit.dispclk_mhz) ||
				(dc->clk_mgr->bw_params->clk_table.num_entries_per_clk.num_dtbclk_levels && dc->clk_mgr->bw_params->dc_mode_limit.dtbclk_mhz) ||
				(dc->clk_mgr->bw_params->clk_table.num_entries_per_clk.num_fclk_levels && dc->clk_mgr->bw_params->dc_mode_limit.fclk_mhz) ||
				(dc->clk_mgr->bw_params->clk_table.num_entries_per_clk.num_memclk_levels && dc->clk_mgr->bw_params->dc_mode_limit.memclk_mhz) ||
				(dc->clk_mgr->bw_params->clk_table.num_entries_per_clk.num_socclk_levels && dc->clk_mgr->bw_params->dc_mode_limit.socclk_mhz);
	}

	// Initialize the dccg
	if (res_pool->dccg->funcs->dccg_init)
		res_pool->dccg->funcs->dccg_init(res_pool->dccg);

	// Disable DMUB Initialization until IPS state programming is finalized
	//if (!dcb->funcs->is_accelerated_mode(dcb)) {
	//	hws->funcs.bios_golden_init(dc);
	//}

	// Set default OPTC memory power states
	if (dc->debug.enable_mem_low_power.bits.optc) {
		// Shutdown when unassigned and light sleep in VBLANK
		REG_SET_2(ODM_MEM_PWR_CTRL3, 0, ODM_MEM_UNASSIGNED_PWR_MODE, 3, ODM_MEM_VBLANK_PWR_MODE, 1);
	}

	if (dc->debug.enable_mem_low_power.bits.vga) {
		// Power down VGA memory
		REG_UPDATE(MMHUBBUB_MEM_PWR_CNTL, VGA_MEM_PWR_FORCE, 1);
	}

	if (dc->ctx->dc_bios->fw_info_valid) {
		res_pool->ref_clocks.xtalin_clock_inKhz =
				dc->ctx->dc_bios->fw_info.pll_info.crystal_frequency;

		if (res_pool->hubbub) {
			(res_pool->dccg->funcs->get_dccg_ref_freq)(res_pool->dccg,
					dc->ctx->dc_bios->fw_info.pll_info.crystal_frequency,
					&res_pool->ref_clocks.dccg_ref_clock_inKhz);

			current_dchub_ref_freq = res_pool->ref_clocks.dchub_ref_clock_inKhz / 1000;

			(res_pool->hubbub->funcs->get_dchub_ref_freq)(res_pool->hubbub,
					res_pool->ref_clocks.dccg_ref_clock_inKhz,
					&res_pool->ref_clocks.dchub_ref_clock_inKhz);
		} else {
			// Not all ASICs have DCCG sw component
			res_pool->ref_clocks.dccg_ref_clock_inKhz =
					res_pool->ref_clocks.xtalin_clock_inKhz;
			res_pool->ref_clocks.dchub_ref_clock_inKhz =
					res_pool->ref_clocks.xtalin_clock_inKhz;
		}
	} else
		ASSERT_CRITICAL(false);

	for (i = 0; i < dc->link_count; i++) {
		/* Power up AND update implementation according to the
		 * required signal (which may be different from the
		 * default signal on connector).
		 */
		struct dc_link *link = dc->links[i];

		link->link_enc->funcs->hw_init(link->link_enc);

		/* Check for enabled DIG to identify enabled display */
		if (link->link_enc->funcs->is_dig_enabled &&
			link->link_enc->funcs->is_dig_enabled(link->link_enc)) {
			link->link_status.link_active = true;
			link->phy_state.symclk_state = SYMCLK_ON_TX_ON;
			if (link->link_enc->funcs->fec_is_active &&
					link->link_enc->funcs->fec_is_active(link->link_enc))
				link->fec_state = dc_link_fec_enabled;
		}
	}

	/* enable_power_gating_plane before dsc_pg_control because
	 * FORCEON = 1 with hw default value on bootup, resume from s3
	 */
	if (hws->funcs.enable_power_gating_plane)
		hws->funcs.enable_power_gating_plane(dc->hwseq, true);

	/* we want to turn off all dp displays before doing detection */
	dc->link_srv->blank_all_dp_displays(dc);

	/* If taking control over from VBIOS, we may want to optimize our first
	 * mode set, so we need to skip powering down pipes until we know which
	 * pipes we want to use.
	 * Otherwise, if taking control is not possible, we need to power
	 * everything down.
	 */
	if (dcb->funcs->is_accelerated_mode(dcb) || !dc->config.seamless_boot_edp_requested) {
		/* Disable boot optimizations means power down everything including PHY, DIG,
		 * and OTG (i.e. the boot is not optimized because we do a full power down).
		 */
		if (dc->hwss.enable_accelerated_mode && dc->debug.disable_boot_optimizations)
			dc->hwss.enable_accelerated_mode(dc, dc->current_state);
		else
			hws->funcs.init_pipes(dc, dc->current_state);

		if (dc->res_pool->hubbub->funcs->allow_self_refresh_control)
			dc->res_pool->hubbub->funcs->allow_self_refresh_control(dc->res_pool->hubbub,
					!dc->res_pool->hubbub->ctx->dc->debug.disable_stutter);

		dcn401_initialize_min_clocks(dc);

		/* On HW init, allow idle optimizations after pipes have been turned off.
		 *
		 * In certain D3 cases (i.e. BOCO / BOMACO) it's possible that hardware state
		 * is reset (i.e. not in idle at the time hw init is called), but software state
		 * still has idle_optimizations = true, so we must disable idle optimizations first
		 * (i.e. set false), then re-enable (set true).
		 */
		dc_allow_idle_optimizations(dc, false);
		dc_allow_idle_optimizations(dc, true);
	}

	/* In headless boot cases, DIG may be turned
	 * on which causes HW/SW discrepancies.
	 * To avoid this, power down hardware on boot
	 * if DIG is turned on and seamless boot not enabled
	 */
	if (!dc->config.seamless_boot_edp_requested) {
		struct dc_link *edp_links[MAX_NUM_EDP];
		struct dc_link *edp_link;

		dc_get_edp_links(dc, edp_links, &edp_num);
		if (edp_num) {
			for (i = 0; i < edp_num; i++) {
				edp_link = edp_links[i];
				if (edp_link->link_enc->funcs->is_dig_enabled &&
						edp_link->link_enc->funcs->is_dig_enabled(edp_link->link_enc) &&
						dc->hwss.edp_backlight_control &&
						hws->funcs.power_down &&
						dc->hwss.edp_power_control) {
					dc->hwss.edp_backlight_control(edp_link, false);
					hws->funcs.power_down(dc);
					dc->hwss.edp_power_control(edp_link, false);
				}
			}
		} else {
			for (i = 0; i < dc->link_count; i++) {
				struct dc_link *link = dc->links[i];

				if (link->link_enc->funcs->is_dig_enabled &&
						link->link_enc->funcs->is_dig_enabled(link->link_enc) &&
						hws->funcs.power_down) {
					hws->funcs.power_down(dc);
					break;
				}

			}
		}
	}

	for (i = 0; i < res_pool->audio_count; i++) {
		struct audio *audio = res_pool->audios[i];

		audio->funcs->hw_init(audio);
	}

	for (i = 0; i < dc->link_count; i++) {
		struct dc_link *link = dc->links[i];

		if (link->panel_cntl) {
			backlight = link->panel_cntl->funcs->hw_init(link->panel_cntl);
			user_level = link->panel_cntl->stored_backlight_registers.USER_LEVEL;
		}
	}

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		if (abms[i] != NULL && abms[i]->funcs != NULL)
			abms[i]->funcs->abm_init(abms[i], backlight, user_level);
	}

	/* power AFMT HDMI memory TODO: may move to dis/en output save power*/
	REG_WRITE(DIO_MEM_PWR_CTRL, 0);

	if (!dc->debug.disable_clock_gate) {
		/* enable all DCN clock gating */
		REG_WRITE(DCCG_GATE_DISABLE_CNTL, 0);

		REG_WRITE(DCCG_GATE_DISABLE_CNTL2, 0);

		REG_UPDATE(DCFCLK_CNTL, DCFCLK_GATE_DIS, 0);
	}

	dcn401_setup_hpo_hw_control(hws, true);

	if (!dcb->funcs->is_accelerated_mode(dcb) && dc->res_pool->hubbub->funcs->init_watermarks)
		dc->res_pool->hubbub->funcs->init_watermarks(dc->res_pool->hubbub);

	if (dc->clk_mgr && dc->clk_mgr->funcs && dc->clk_mgr->funcs->notify_wm_ranges)
		dc->clk_mgr->funcs->notify_wm_ranges(dc->clk_mgr);

	if (dc->res_pool->hubbub->funcs->force_pstate_change_control)
		dc->res_pool->hubbub->funcs->force_pstate_change_control(
				dc->res_pool->hubbub, false, false);

	if (dc->res_pool->hubbub->funcs->init_crb)
		dc->res_pool->hubbub->funcs->init_crb(dc->res_pool->hubbub);

	if (dc->res_pool->hubbub->funcs->set_request_limit && dc->config.sdpif_request_limit_words_per_umc > 0)
		dc->res_pool->hubbub->funcs->set_request_limit(dc->res_pool->hubbub, dc->ctx->dc_bios->vram_info.num_chans, dc->config.sdpif_request_limit_words_per_umc);

	// Get DMCUB capabilities
	if (dc->ctx->dmub_srv) {
		dc_dmub_srv_query_caps_cmd(dc->ctx->dmub_srv);
		dc->caps.dmub_caps.psr = dc->ctx->dmub_srv->dmub->feature_caps.psr;
		dc->caps.dmub_caps.mclk_sw = dc->ctx->dmub_srv->dmub->feature_caps.fw_assisted_mclk_switch_ver > 0;
		dc->caps.dmub_caps.fams_ver = dc->ctx->dmub_srv->dmub->feature_caps.fw_assisted_mclk_switch_ver;
		dc->debug.fams2_config.bits.enable &= dc->ctx->dmub_srv->dmub->feature_caps.fw_assisted_mclk_switch_ver == 2;
		if ((!dc->debug.fams2_config.bits.enable && dc->res_pool->funcs->update_bw_bounding_box)
			|| res_pool->ref_clocks.dchub_ref_clock_inKhz / 1000 != current_dchub_ref_freq) {
			/* update bounding box if FAMS2 disabled, or if dchub clk has changed */
			if (dc->clk_mgr)
				dc->res_pool->funcs->update_bw_bounding_box(dc,
									    dc->clk_mgr->bw_params);
		}
	}
}

static void dcn401_get_mcm_lut_xable_from_pipe_ctx(struct dc *dc, struct pipe_ctx *pipe_ctx,
		enum MCM_LUT_XABLE *shaper_xable,
		enum MCM_LUT_XABLE *lut3d_xable,
		enum MCM_LUT_XABLE *lut1d_xable)
{
	enum dc_cm2_shaper_3dlut_setting shaper_3dlut_setting = DC_CM2_SHAPER_3DLUT_SETTING_BYPASS_ALL;
	bool lut1d_enable = false;
	struct mpc *mpc = dc->res_pool->mpc;
	int mpcc_id = pipe_ctx->plane_res.hubp->inst;

	if (!pipe_ctx->plane_state)
		return;
	shaper_3dlut_setting = pipe_ctx->plane_state->mcm_shaper_3dlut_setting;
	lut1d_enable = pipe_ctx->plane_state->mcm_lut1d_enable;
	mpc->funcs->set_movable_cm_location(mpc, MPCC_MOVABLE_CM_LOCATION_BEFORE, mpcc_id);
	pipe_ctx->plane_state->mcm_location = MPCC_MOVABLE_CM_LOCATION_BEFORE;

	*lut1d_xable = lut1d_enable ? MCM_LUT_ENABLE : MCM_LUT_DISABLE;

	switch (shaper_3dlut_setting) {
	case DC_CM2_SHAPER_3DLUT_SETTING_BYPASS_ALL:
		*lut3d_xable = *shaper_xable = MCM_LUT_DISABLE;
		break;
	case DC_CM2_SHAPER_3DLUT_SETTING_ENABLE_SHAPER:
		*lut3d_xable = MCM_LUT_DISABLE;
		*shaper_xable = MCM_LUT_ENABLE;
		break;
	case DC_CM2_SHAPER_3DLUT_SETTING_ENABLE_SHAPER_3DLUT:
		*lut3d_xable = *shaper_xable = MCM_LUT_ENABLE;
		break;
	}
}

void dcn401_populate_mcm_luts(struct dc *dc,
		struct pipe_ctx *pipe_ctx,
		struct dc_cm2_func_luts mcm_luts,
		bool lut_bank_a)
{
	struct dpp *dpp_base = pipe_ctx->plane_res.dpp;
	struct hubp *hubp = pipe_ctx->plane_res.hubp;
	int mpcc_id = hubp->inst;
	struct mpc *mpc = dc->res_pool->mpc;
	union mcm_lut_params m_lut_params;
	enum dc_cm2_transfer_func_source lut3d_src = mcm_luts.lut3d_data.lut3d_src;
	enum hubp_3dlut_fl_format format;
	enum hubp_3dlut_fl_mode mode;
	enum hubp_3dlut_fl_width width;
	enum hubp_3dlut_fl_addressing_mode addr_mode;
	enum hubp_3dlut_fl_crossbar_bit_slice crossbar_bit_slice_y_g;
	enum hubp_3dlut_fl_crossbar_bit_slice crossbar_bit_slice_cb_b;
	enum hubp_3dlut_fl_crossbar_bit_slice crossbar_bit_slice_cr_r;
	enum MCM_LUT_XABLE shaper_xable = MCM_LUT_DISABLE;
	enum MCM_LUT_XABLE lut3d_xable = MCM_LUT_DISABLE;
	enum MCM_LUT_XABLE lut1d_xable = MCM_LUT_DISABLE;
	bool is_17x17x17 = true;
	bool rval;

	dcn401_get_mcm_lut_xable_from_pipe_ctx(dc, pipe_ctx, &shaper_xable, &lut3d_xable, &lut1d_xable);

	/* 1D LUT */
	if (mcm_luts.lut1d_func && lut3d_xable != MCM_LUT_DISABLE) {
		memset(&m_lut_params, 0, sizeof(m_lut_params));
		if (mcm_luts.lut1d_func->type == TF_TYPE_HWPWL)
			m_lut_params.pwl = &mcm_luts.lut1d_func->pwl;
		else if (mcm_luts.lut1d_func->type == TF_TYPE_DISTRIBUTED_POINTS) {
			rval = cm3_helper_translate_curve_to_hw_format(
					mcm_luts.lut1d_func,
					&dpp_base->regamma_params, false);
			m_lut_params.pwl = rval ? &dpp_base->regamma_params : NULL;
		}
		if (m_lut_params.pwl) {
			if (mpc->funcs->populate_lut)
				mpc->funcs->populate_lut(mpc, MCM_LUT_1DLUT, m_lut_params, lut_bank_a, mpcc_id);
		}
		if (mpc->funcs->program_lut_mode)
			mpc->funcs->program_lut_mode(mpc, MCM_LUT_1DLUT, lut1d_xable, lut_bank_a, mpcc_id);
	}

	/* Shaper */
	if (mcm_luts.shaper) {
		memset(&m_lut_params, 0, sizeof(m_lut_params));
		if (mcm_luts.shaper->type == TF_TYPE_HWPWL)
			m_lut_params.pwl = &mcm_luts.shaper->pwl;
		else if (mcm_luts.shaper->type == TF_TYPE_DISTRIBUTED_POINTS) {
			ASSERT(false);
			rval = cm3_helper_translate_curve_to_hw_format(
					mcm_luts.shaper,
					&dpp_base->regamma_params, true);
			m_lut_params.pwl = rval ? &dpp_base->regamma_params : NULL;
		}
		if (m_lut_params.pwl) {
			if (mpc->funcs->populate_lut)
				mpc->funcs->populate_lut(mpc, MCM_LUT_SHAPER, m_lut_params, lut_bank_a, mpcc_id);
		}
		if (mpc->funcs->program_lut_mode)
			mpc->funcs->program_lut_mode(mpc, MCM_LUT_SHAPER, shaper_xable, lut_bank_a, mpcc_id);
	}

	/* 3DLUT */
	switch (lut3d_src) {
	case DC_CM2_TRANSFER_FUNC_SOURCE_SYSMEM:
		memset(&m_lut_params, 0, sizeof(m_lut_params));
		if (hubp->funcs->hubp_enable_3dlut_fl)
			hubp->funcs->hubp_enable_3dlut_fl(hubp, false);
		if (mcm_luts.lut3d_data.lut3d_func && mcm_luts.lut3d_data.lut3d_func->state.bits.initialized) {
			m_lut_params.lut3d = &mcm_luts.lut3d_data.lut3d_func->lut_3d;
			if (mpc->funcs->populate_lut)
				mpc->funcs->populate_lut(mpc, MCM_LUT_3DLUT, m_lut_params, lut_bank_a, mpcc_id);
			if (mpc->funcs->program_lut_mode)
				mpc->funcs->program_lut_mode(mpc, MCM_LUT_3DLUT, lut3d_xable, lut_bank_a,
						mpcc_id);
		}
		break;
	case DC_CM2_TRANSFER_FUNC_SOURCE_VIDMEM:

		if (mpc->funcs->program_lut_read_write_control)
			mpc->funcs->program_lut_read_write_control(mpc, MCM_LUT_3DLUT, lut_bank_a, mpcc_id);
		if (mpc->funcs->program_lut_mode)
			mpc->funcs->program_lut_mode(mpc, MCM_LUT_3DLUT, lut3d_xable, lut_bank_a, mpcc_id);
		if (mpc->funcs->program_3dlut_size)
			mpc->funcs->program_3dlut_size(mpc, is_17x17x17, mpcc_id);
		if (hubp->funcs->hubp_program_3dlut_fl_addr)
			hubp->funcs->hubp_program_3dlut_fl_addr(hubp, mcm_luts.lut3d_data.gpu_mem_params.addr);
		switch (mcm_luts.lut3d_data.gpu_mem_params.layout) {
		case DC_CM2_GPU_MEM_LAYOUT_3D_SWIZZLE_LINEAR_RGB:
			mode = hubp_3dlut_fl_mode_native_1;
			addr_mode = hubp_3dlut_fl_addressing_mode_sw_linear;
			break;
		case DC_CM2_GPU_MEM_LAYOUT_3D_SWIZZLE_LINEAR_BGR:
			mode = hubp_3dlut_fl_mode_native_2;
			addr_mode = hubp_3dlut_fl_addressing_mode_sw_linear;
			break;
		case DC_CM2_GPU_MEM_LAYOUT_1D_PACKED_LINEAR:
			mode = hubp_3dlut_fl_mode_transform;
			addr_mode = hubp_3dlut_fl_addressing_mode_simple_linear;
			break;
		default:
			mode = hubp_3dlut_fl_mode_disable;
			addr_mode = hubp_3dlut_fl_addressing_mode_sw_linear;
			break;
		}
		if (hubp->funcs->hubp_program_3dlut_fl_mode)
			hubp->funcs->hubp_program_3dlut_fl_mode(hubp, mode);

		if (hubp->funcs->hubp_program_3dlut_fl_addressing_mode)
			hubp->funcs->hubp_program_3dlut_fl_addressing_mode(hubp, addr_mode);

		switch (mcm_luts.lut3d_data.gpu_mem_params.format_params.format) {
		case DC_CM2_GPU_MEM_FORMAT_16161616_UNORM_12MSB:
		default:
			format = hubp_3dlut_fl_format_unorm_12msb_bitslice;
			break;
		case DC_CM2_GPU_MEM_FORMAT_16161616_UNORM_12LSB:
			format = hubp_3dlut_fl_format_unorm_12lsb_bitslice;
			break;
		case DC_CM2_GPU_MEM_FORMAT_16161616_FLOAT_FP1_5_10:
			format = hubp_3dlut_fl_format_float_fp1_5_10;
			break;
		}
		if (hubp->funcs->hubp_program_3dlut_fl_format)
			hubp->funcs->hubp_program_3dlut_fl_format(hubp, format);
		if (hubp->funcs->hubp_update_3dlut_fl_bias_scale)
			hubp->funcs->hubp_update_3dlut_fl_bias_scale(hubp,
					mcm_luts.lut3d_data.gpu_mem_params.format_params.float_params.bias,
					mcm_luts.lut3d_data.gpu_mem_params.format_params.float_params.scale);

		switch (mcm_luts.lut3d_data.gpu_mem_params.component_order) {
		case DC_CM2_GPU_MEM_PIXEL_COMPONENT_ORDER_RGBA:
		default:
			crossbar_bit_slice_cr_r = hubp_3dlut_fl_crossbar_bit_slice_0_15;
			crossbar_bit_slice_y_g = hubp_3dlut_fl_crossbar_bit_slice_16_31;
			crossbar_bit_slice_cb_b = hubp_3dlut_fl_crossbar_bit_slice_32_47;
			break;
		}

		if (hubp->funcs->hubp_program_3dlut_fl_crossbar)
			hubp->funcs->hubp_program_3dlut_fl_crossbar(hubp,
					crossbar_bit_slice_y_g,
					crossbar_bit_slice_cb_b,
					crossbar_bit_slice_cr_r);

		switch (mcm_luts.lut3d_data.gpu_mem_params.size) {
		case DC_CM2_GPU_MEM_SIZE_171717:
		default:
			width = hubp_3dlut_fl_width_17;
			break;
		case DC_CM2_GPU_MEM_SIZE_TRANSFORMED:
			width = hubp_3dlut_fl_width_transformed;
			break;
		}
		if (hubp->funcs->hubp_program_3dlut_fl_width)
			hubp->funcs->hubp_program_3dlut_fl_width(hubp, width);
		if (mpc->funcs->update_3dlut_fast_load_select)
			mpc->funcs->update_3dlut_fast_load_select(mpc, mpcc_id, hubp->inst);

		if (hubp->funcs->hubp_enable_3dlut_fl)
			hubp->funcs->hubp_enable_3dlut_fl(hubp, true);
		else {
			if (mpc->funcs->program_lut_mode) {
				mpc->funcs->program_lut_mode(mpc, MCM_LUT_SHAPER, MCM_LUT_DISABLE, lut_bank_a, mpcc_id);
				mpc->funcs->program_lut_mode(mpc, MCM_LUT_3DLUT, MCM_LUT_DISABLE, lut_bank_a, mpcc_id);
				mpc->funcs->program_lut_mode(mpc, MCM_LUT_1DLUT, MCM_LUT_DISABLE, lut_bank_a, mpcc_id);
			}
		}
		break;

	}
}

void dcn401_trigger_3dlut_dma_load(struct dc *dc, struct pipe_ctx *pipe_ctx)
{
	struct hubp *hubp = pipe_ctx->plane_res.hubp;

	if (hubp->funcs->hubp_enable_3dlut_fl) {
		hubp->funcs->hubp_enable_3dlut_fl(hubp, true);
	}
}

bool dcn401_set_mcm_luts(struct pipe_ctx *pipe_ctx,
				const struct dc_plane_state *plane_state)
{
	struct dpp *dpp_base = pipe_ctx->plane_res.dpp;
	int mpcc_id = pipe_ctx->plane_res.hubp->inst;
	struct mpc *mpc = pipe_ctx->stream_res.opp->ctx->dc->res_pool->mpc;
	bool result;
	const struct pwl_params *lut_params = NULL;
	bool rval;

	mpc->funcs->set_movable_cm_location(mpc, MPCC_MOVABLE_CM_LOCATION_BEFORE, mpcc_id);
	pipe_ctx->plane_state->mcm_location = MPCC_MOVABLE_CM_LOCATION_BEFORE;
	// 1D LUT
	if (plane_state->blend_tf.type == TF_TYPE_HWPWL)
		lut_params = &plane_state->blend_tf.pwl;
	else if (plane_state->blend_tf.type == TF_TYPE_DISTRIBUTED_POINTS) {
		rval = cm3_helper_translate_curve_to_hw_format(&plane_state->blend_tf,
				&dpp_base->regamma_params, false);
		lut_params = rval ? &dpp_base->regamma_params : NULL;
	}
	result = mpc->funcs->program_1dlut(mpc, lut_params, mpcc_id);
	lut_params = NULL;

	// Shaper
	if (plane_state->in_shaper_func.type == TF_TYPE_HWPWL)
		lut_params = &plane_state->in_shaper_func.pwl;
	else if (plane_state->in_shaper_func.type == TF_TYPE_DISTRIBUTED_POINTS) {
		// TODO: dpp_base replace
		rval = cm3_helper_translate_curve_to_hw_format(&plane_state->in_shaper_func,
				&dpp_base->shaper_params, true);
		lut_params = rval ? &dpp_base->shaper_params : NULL;
	}
	result &= mpc->funcs->program_shaper(mpc, lut_params, mpcc_id);

	// 3D
	if (mpc->funcs->program_3dlut) {
		if (plane_state->lut3d_func.state.bits.initialized == 1)
			result &= mpc->funcs->program_3dlut(mpc, &plane_state->lut3d_func.lut_3d, mpcc_id);
		else
			result &= mpc->funcs->program_3dlut(mpc, NULL, mpcc_id);
	}

	return result;
}

bool dcn401_set_output_transfer_func(struct dc *dc,
				struct pipe_ctx *pipe_ctx,
				const struct dc_stream_state *stream)
{
	int mpcc_id = pipe_ctx->plane_res.hubp->inst;
	struct mpc *mpc = pipe_ctx->stream_res.opp->ctx->dc->res_pool->mpc;
	const struct pwl_params *params = NULL;
	bool ret = false;

	/* program OGAM or 3DLUT only for the top pipe*/
	if (resource_is_pipe_type(pipe_ctx, OPP_HEAD)) {
		/*program shaper and 3dlut in MPC*/
		ret = dcn32_set_mpc_shaper_3dlut(pipe_ctx, stream);
		if (ret == false && mpc->funcs->set_output_gamma) {
			if (stream->out_transfer_func.type == TF_TYPE_HWPWL)
				params = &stream->out_transfer_func.pwl;
			else if (pipe_ctx->stream->out_transfer_func.type ==
					TF_TYPE_DISTRIBUTED_POINTS &&
					cm3_helper_translate_curve_to_hw_format(
					&stream->out_transfer_func,
					&mpc->blender_params, false))
				params = &mpc->blender_params;
			/* there are no ROM LUTs in OUTGAM */
			if (stream->out_transfer_func.type == TF_TYPE_PREDEFINED)
				BREAK_TO_DEBUGGER();
		}
	}

	if (mpc->funcs->set_output_gamma)
		mpc->funcs->set_output_gamma(mpc, mpcc_id, params);

	return ret;
}

void dcn401_calculate_dccg_tmds_div_value(struct pipe_ctx *pipe_ctx,
				unsigned int *tmds_div)
{
	struct dc_stream_state *stream = pipe_ctx->stream;

	if (dc_is_tmds_signal(stream->signal) || dc_is_virtual_signal(stream->signal)) {
		if (stream->timing.pixel_encoding == PIXEL_ENCODING_YCBCR420)
			*tmds_div = PIXEL_RATE_DIV_BY_2;
		else
			*tmds_div = PIXEL_RATE_DIV_BY_4;
	} else {
		*tmds_div = PIXEL_RATE_DIV_BY_1;
	}

	if (*tmds_div == PIXEL_RATE_DIV_NA)
		ASSERT(false);

}

static void enable_stream_timing_calc(
		struct pipe_ctx *pipe_ctx,
		struct dc_state *context,
		struct dc *dc,
		unsigned int *tmds_div,
		int *opp_inst,
		int *opp_cnt,
		struct pipe_ctx *opp_heads[MAX_PIPES],
		bool *manual_mode,
		struct drr_params *params,
		unsigned int *event_triggers)
{
	struct dc_stream_state *stream = pipe_ctx->stream;
	int i;

	if (dc_is_tmds_signal(stream->signal) || dc_is_virtual_signal(stream->signal))
		dcn401_calculate_dccg_tmds_div_value(pipe_ctx, tmds_div);

	*opp_cnt = resource_get_opp_heads_for_otg_master(pipe_ctx, &context->res_ctx, opp_heads);
	for (i = 0; i < *opp_cnt; i++)
		opp_inst[i] = opp_heads[i]->stream_res.opp->inst;

	if (dc_is_tmds_signal(stream->signal)) {
		stream->link->phy_state.symclk_ref_cnts.otg = 1;
		if (stream->link->phy_state.symclk_state == SYMCLK_OFF_TX_OFF)
			stream->link->phy_state.symclk_state = SYMCLK_ON_TX_OFF;
		else
			stream->link->phy_state.symclk_state = SYMCLK_ON_TX_ON;
	}

	params->vertical_total_min = stream->adjust.v_total_min;
	params->vertical_total_max = stream->adjust.v_total_max;
	params->vertical_total_mid = stream->adjust.v_total_mid;
	params->vertical_total_mid_frame_num = stream->adjust.v_total_mid_frame_num;

	// DRR should set trigger event to monitor surface update event
	if (stream->adjust.v_total_min != 0 && stream->adjust.v_total_max != 0)
		*event_triggers = 0x80;
}

enum dc_status dcn401_enable_stream_timing(
		struct pipe_ctx *pipe_ctx,
		struct dc_state *context,
		struct dc *dc)
{
	struct dce_hwseq *hws = dc->hwseq;
	struct dc_stream_state *stream = pipe_ctx->stream;
	struct drr_params params = {0};
	unsigned int event_triggers = 0;
	int opp_cnt = 1;
	int opp_inst[MAX_PIPES] = {0};
	struct pipe_ctx *opp_heads[MAX_PIPES] = {0};
	bool manual_mode;
	unsigned int tmds_div = PIXEL_RATE_DIV_NA;
	unsigned int unused_div = PIXEL_RATE_DIV_NA;
	int odm_slice_width;
	int last_odm_slice_width;
	int i;

	if (!resource_is_pipe_type(pipe_ctx, OTG_MASTER))
		return DC_OK;

	enable_stream_timing_calc(pipe_ctx, context, dc, &tmds_div, opp_inst,
			&opp_cnt, opp_heads, &manual_mode, &params, &event_triggers);

	if (dc->res_pool->dccg->funcs->set_pixel_rate_div) {
		dc->res_pool->dccg->funcs->set_pixel_rate_div(
			dc->res_pool->dccg, pipe_ctx->stream_res.tg->inst,
			tmds_div, unused_div);
	}

	/* TODO check if timing_changed, disable stream if timing changed */

	if (opp_cnt > 1) {
		odm_slice_width = resource_get_odm_slice_dst_width(pipe_ctx, false);
		last_odm_slice_width = resource_get_odm_slice_dst_width(pipe_ctx, true);
		pipe_ctx->stream_res.tg->funcs->set_odm_combine(
				pipe_ctx->stream_res.tg,
				opp_inst, opp_cnt,
				odm_slice_width, last_odm_slice_width);
	}

	/* set DTBCLK_P */
	if (dc->res_pool->dccg->funcs->set_dtbclk_p_src) {
		if (dc_is_dp_signal(stream->signal) || dc_is_virtual_signal(stream->signal)) {
			dc->res_pool->dccg->funcs->set_dtbclk_p_src(dc->res_pool->dccg, DPREFCLK, pipe_ctx->stream_res.tg->inst);
		}
	}

	/* HW program guide assume display already disable
	 * by unplug sequence. OTG assume stop.
	 */
	pipe_ctx->stream_res.tg->funcs->enable_optc_clock(pipe_ctx->stream_res.tg, true);

	if (false == pipe_ctx->clock_source->funcs->program_pix_clk(
			pipe_ctx->clock_source,
			&pipe_ctx->stream_res.pix_clk_params,
			dc->link_srv->dp_get_encoding_format(&pipe_ctx->link_config.dp_link_settings),
			&pipe_ctx->pll_settings)) {
		BREAK_TO_DEBUGGER();
		return DC_ERROR_UNEXPECTED;
	}

	if (dc->hwseq->funcs.PLAT_58856_wa && (!dc_is_dp_signal(stream->signal)))
		dc->hwseq->funcs.PLAT_58856_wa(context, pipe_ctx);

	pipe_ctx->stream_res.tg->funcs->program_timing(
			pipe_ctx->stream_res.tg,
			&stream->timing,
			pipe_ctx->pipe_dlg_param.vready_offset,
			pipe_ctx->pipe_dlg_param.vstartup_start,
			pipe_ctx->pipe_dlg_param.vupdate_offset,
			pipe_ctx->pipe_dlg_param.vupdate_width,
			pipe_ctx->pipe_dlg_param.pstate_keepout,
			pipe_ctx->stream->signal,
			true);

	for (i = 0; i < opp_cnt; i++) {
		opp_heads[i]->stream_res.opp->funcs->opp_pipe_clock_control(
				opp_heads[i]->stream_res.opp,
				true);
		opp_heads[i]->stream_res.opp->funcs->opp_program_left_edge_extra_pixel(
				opp_heads[i]->stream_res.opp,
				stream->timing.pixel_encoding,
				resource_is_pipe_type(opp_heads[i], OTG_MASTER));
	}

	pipe_ctx->stream_res.opp->funcs->opp_pipe_clock_control(
			pipe_ctx->stream_res.opp,
			true);

	hws->funcs.blank_pixel_data(dc, pipe_ctx, true);

	/* VTG is  within DCHUB command block. DCFCLK is always on */
	if (false == pipe_ctx->stream_res.tg->funcs->enable_crtc(pipe_ctx->stream_res.tg)) {
		BREAK_TO_DEBUGGER();
		return DC_ERROR_UNEXPECTED;
	}

	hws->funcs.wait_for_blank_complete(pipe_ctx->stream_res.opp);
	set_drr_and_clear_adjust_pending(pipe_ctx, stream, &params);

	/* Event triggers and num frames initialized for DRR, but can be
	 * later updated for PSR use. Note DRR trigger events are generated
	 * regardless of whether num frames met.
	 */
	if (pipe_ctx->stream_res.tg->funcs->set_static_screen_control)
		pipe_ctx->stream_res.tg->funcs->set_static_screen_control(
				pipe_ctx->stream_res.tg, event_triggers, 2);

	/* TODO program crtc source select for non-virtual signal*/
	/* TODO program FMT */
	/* TODO setup link_enc */
	/* TODO set stream attributes */
	/* TODO program audio */
	/* TODO enable stream if timing changed */
	/* TODO unblank stream if DP */

	if (dc_state_get_pipe_subvp_type(context, pipe_ctx) == SUBVP_PHANTOM) {
		if (pipe_ctx->stream_res.tg->funcs->phantom_crtc_post_enable)
			pipe_ctx->stream_res.tg->funcs->phantom_crtc_post_enable(pipe_ctx->stream_res.tg);
	}

	return DC_OK;
}

static enum phyd32clk_clock_source get_phyd32clk_src(struct dc_link *link)
{
	switch (link->link_enc->transmitter) {
	case TRANSMITTER_UNIPHY_A:
		return PHYD32CLKA;
	case TRANSMITTER_UNIPHY_B:
		return PHYD32CLKB;
	case TRANSMITTER_UNIPHY_C:
		return PHYD32CLKC;
	case TRANSMITTER_UNIPHY_D:
		return PHYD32CLKD;
	case TRANSMITTER_UNIPHY_E:
		return PHYD32CLKE;
	default:
		return PHYD32CLKA;
	}
}

static void dcn401_enable_stream_calc(
		struct pipe_ctx *pipe_ctx,
		int *dp_hpo_inst,
		enum phyd32clk_clock_source *phyd32clk,
		unsigned int *tmds_div,
		uint32_t *early_control)
{

	struct dc *dc = pipe_ctx->stream->ctx->dc;
	struct dc_crtc_timing *timing = &pipe_ctx->stream->timing;
	enum dc_lane_count lane_count =
			pipe_ctx->stream->link->cur_link_settings.lane_count;
	uint32_t active_total_with_borders;

	if (dc->link_srv->dp_is_128b_132b_signal(pipe_ctx))
		*dp_hpo_inst = pipe_ctx->stream_res.hpo_dp_stream_enc->inst;

	*phyd32clk = get_phyd32clk_src(pipe_ctx->stream->link);

	if (dc_is_tmds_signal(pipe_ctx->stream->signal))
		dcn401_calculate_dccg_tmds_div_value(pipe_ctx, tmds_div);
	else
		*tmds_div = PIXEL_RATE_DIV_BY_1;

	/* enable early control to avoid corruption on DP monitor*/
	active_total_with_borders =
			timing->h_addressable
				+ timing->h_border_left
				+ timing->h_border_right;

	if (lane_count != 0)
		*early_control = active_total_with_borders % lane_count;

	if (*early_control == 0)
		*early_control = lane_count;

}

void dcn401_enable_stream(struct pipe_ctx *pipe_ctx)
{
	uint32_t early_control = 0;
	struct timing_generator *tg = pipe_ctx->stream_res.tg;
	struct dc_link *link = pipe_ctx->stream->link;
	const struct link_hwss *link_hwss = get_link_hwss(link, &pipe_ctx->link_res);
	struct dc *dc = pipe_ctx->stream->ctx->dc;
	struct dccg *dccg = dc->res_pool->dccg;
	enum phyd32clk_clock_source phyd32clk;
	int dp_hpo_inst = 0;
	unsigned int tmds_div = PIXEL_RATE_DIV_NA;
	unsigned int unused_div = PIXEL_RATE_DIV_NA;
	struct link_encoder *link_enc = link_enc_cfg_get_link_enc(pipe_ctx->stream->link);
	struct stream_encoder *stream_enc = pipe_ctx->stream_res.stream_enc;

	dcn401_enable_stream_calc(pipe_ctx, &dp_hpo_inst, &phyd32clk,
				&tmds_div, &early_control);

	if (dc_is_dp_signal(pipe_ctx->stream->signal) || dc_is_virtual_signal(pipe_ctx->stream->signal)) {
		if (dc->link_srv->dp_is_128b_132b_signal(pipe_ctx)) {
			dccg->funcs->set_dpstreamclk(dccg, DPREFCLK, tg->inst, dp_hpo_inst);
			if (link->cur_link_settings.link_rate == LINK_RATE_UNKNOWN) {
				dccg->funcs->disable_symclk32_se(dccg, dp_hpo_inst);
			} else {
				dccg->funcs->enable_symclk32_se(dccg, dp_hpo_inst, phyd32clk);
			}
		} else {
			dccg->funcs->enable_symclk_se(dccg, stream_enc->stream_enc_inst,
					link_enc->transmitter - TRANSMITTER_UNIPHY_A);
		}
	}

	if (dc->res_pool->dccg->funcs->set_pixel_rate_div) {
		dc->res_pool->dccg->funcs->set_pixel_rate_div(
			dc->res_pool->dccg,
			pipe_ctx->stream_res.tg->inst,
			tmds_div,
			unused_div);
	}

	link_hwss->setup_stream_encoder(pipe_ctx);

	if (pipe_ctx->plane_state && pipe_ctx->plane_state->flip_immediate != 1) {
		if (dc->hwss.program_dmdata_engine)
			dc->hwss.program_dmdata_engine(pipe_ctx);
	}

	dc->hwss.update_info_frame(pipe_ctx);

	if (dc_is_dp_signal(pipe_ctx->stream->signal))
		dc->link_srv->dp_trace_source_sequence(link, DPCD_SOURCE_SEQ_AFTER_UPDATE_INFO_FRAME);

	tg->funcs->set_early_control(tg, early_control);
}

void dcn401_setup_hpo_hw_control(const struct dce_hwseq *hws, bool enable)
{
	REG_UPDATE(HPO_TOP_HW_CONTROL, HPO_IO_EN, enable);
}

static bool dcn401_can_pipe_disable_cursor(struct pipe_ctx *pipe_ctx)
{
	struct pipe_ctx *test_pipe, *split_pipe;
	const struct scaler_data *scl_data = &pipe_ctx->plane_res.scl_data;
	struct rect r1 = scl_data->recout, r2, r2_half;
	int r1_r = r1.x + r1.width, r1_b = r1.y + r1.height, r2_r, r2_b;
	int cur_layer = pipe_ctx->plane_state->layer_index;

	/**
	 * Disable the cursor if there's another pipe above this with a
	 * plane that contains this pipe's viewport to prevent double cursor
	 * and incorrect scaling artifacts.
	 */
	for (test_pipe = pipe_ctx->top_pipe; test_pipe;
		test_pipe = test_pipe->top_pipe) {
		// Skip invisible layer and pipe-split plane on same layer
		if (!test_pipe->plane_state ||
			!test_pipe->plane_state->visible ||
			test_pipe->plane_state->layer_index == cur_layer)
			continue;

		r2 = test_pipe->plane_res.scl_data.recout;
		r2_r = r2.x + r2.width;
		r2_b = r2.y + r2.height;
		split_pipe = test_pipe;

		/**
		 * There is another half plane on same layer because of
		 * pipe-split, merge together per same height.
		 */
		for (split_pipe = pipe_ctx->top_pipe; split_pipe;
			split_pipe = split_pipe->top_pipe)
			if (split_pipe->plane_state->layer_index == test_pipe->plane_state->layer_index) {
				r2_half = split_pipe->plane_res.scl_data.recout;
				r2.x = (r2_half.x < r2.x) ? r2_half.x : r2.x;
				r2.width = r2.width + r2_half.width;
				r2_r = r2.x + r2.width;
				break;
			}

		if (r1.x >= r2.x && r1.y >= r2.y && r1_r <= r2_r && r1_b <= r2_b)
			return true;
	}

	return false;
}

void adjust_hotspot_between_slices_for_2x_magnify(uint32_t cursor_width, struct dc_cursor_position *pos_cpy)
{
	if (cursor_width <= 128) {
		pos_cpy->x_hotspot /= 2;
		pos_cpy->x_hotspot += 1;
	} else {
		pos_cpy->x_hotspot /= 2;
		pos_cpy->x_hotspot += 2;
	}
}

void dcn401_set_cursor_position(struct pipe_ctx *pipe_ctx)
{
	struct dc_cursor_position pos_cpy = pipe_ctx->stream->cursor_position;
	struct hubp *hubp = pipe_ctx->plane_res.hubp;
	struct dpp *dpp = pipe_ctx->plane_res.dpp;
	struct dc_cursor_mi_param param = {
		.pixel_clk_khz = pipe_ctx->stream->timing.pix_clk_100hz / 10,
		.ref_clk_khz = pipe_ctx->stream->ctx->dc->res_pool->ref_clocks.dchub_ref_clock_inKhz,
		.viewport = pipe_ctx->plane_res.scl_data.viewport,
		.recout = pipe_ctx->plane_res.scl_data.recout,
		.h_scale_ratio = pipe_ctx->plane_res.scl_data.ratios.horz,
		.v_scale_ratio = pipe_ctx->plane_res.scl_data.ratios.vert,
		.rotation = pipe_ctx->plane_state->rotation,
		.mirror = pipe_ctx->plane_state->horizontal_mirror,
		.stream = pipe_ctx->stream
	};
	struct rect odm_slice_src = { 0 };
	bool odm_combine_on = (pipe_ctx->next_odm_pipe != NULL) ||
		(pipe_ctx->prev_odm_pipe != NULL);
	int prev_odm_width = 0;
	struct pipe_ctx *prev_odm_pipe = NULL;
	bool mpc_combine_on = false;
	int  bottom_pipe_x_pos = 0;

	int x_pos = pos_cpy.x;
	int y_pos = pos_cpy.y;
	int recout_x_pos = 0;
	int recout_y_pos = 0;

	if ((pipe_ctx->top_pipe != NULL) || (pipe_ctx->bottom_pipe != NULL)) {
		if ((pipe_ctx->plane_state->src_rect.width != pipe_ctx->plane_res.scl_data.viewport.width) ||
			(pipe_ctx->plane_state->src_rect.height != pipe_ctx->plane_res.scl_data.viewport.height)) {
			mpc_combine_on = true;
		}
	}

	/* DCN4 moved cursor composition after Scaler, so in HW it is in
	 * recout space and for HW Cursor position programming need to
	 * translate to recout space.
	 *
	 * Cursor X and Y position programmed into HW can't be negative,
	 * in fact it is X, Y coordinate shifted for the HW Cursor Hot spot
	 * position that goes into HW X and Y coordinates while HW Hot spot
	 * X and Y coordinates are length relative to the cursor top left
	 * corner, hotspot must be smaller than the cursor size.
	 *
	 * DMs/DC interface for Cursor position is in stream->src space, and
	 * DMs supposed to transform Cursor coordinates to stream->src space,
	 * then here we need to translate Cursor coordinates to stream->dst
	 * space, as now in HW, Cursor coordinates are in per pipe recout
	 * space, and for the given pipe valid coordinates are only in range
	 * from 0,0 - recout width, recout height space.
	 * If certain pipe combining is in place, need to further adjust per
	 * pipe to make sure each pipe enabling cursor on its part of the
	 * screen.
	 */
	x_pos = pipe_ctx->stream->dst.x + x_pos * pipe_ctx->stream->dst.width /
		pipe_ctx->stream->src.width;
	y_pos = pipe_ctx->stream->dst.y + y_pos * pipe_ctx->stream->dst.height /
		pipe_ctx->stream->src.height;

	/* If the cursor's source viewport is clipped then we need to
	 * translate the cursor to appear in the correct position on
	 * the screen.
	 *
	 * This translation isn't affected by scaling so it needs to be
	 * done *after* we adjust the position for the scale factor.
	 *
	 * This is only done by opt-in for now since there are still
	 * some usecases like tiled display that might enable the
	 * cursor on both streams while expecting dc to clip it.
	 */
	if (pos_cpy.translate_by_source) {
		x_pos += pipe_ctx->plane_state->src_rect.x;
		y_pos += pipe_ctx->plane_state->src_rect.y;
	}

	/* Adjust for ODM Combine
	 * next/prev_odm_offset is to account for scaled modes that have underscan
	 */
	if (odm_combine_on) {
		prev_odm_pipe = pipe_ctx->prev_odm_pipe;

		while (prev_odm_pipe != NULL) {
			odm_slice_src = resource_get_odm_slice_src_rect(prev_odm_pipe);
			prev_odm_width += odm_slice_src.width;
			prev_odm_pipe = prev_odm_pipe->prev_odm_pipe;
		}

		x_pos -= (prev_odm_width);
	}

	/* If the position is negative then we need to add to the hotspot
	 * to fix cursor size between ODM slices
	 */

	if (x_pos < 0) {
		pos_cpy.x_hotspot -= x_pos;
		if (hubp->curs_attr.attribute_flags.bits.ENABLE_MAGNIFICATION)
			adjust_hotspot_between_slices_for_2x_magnify(hubp->curs_attr.width, &pos_cpy);
		x_pos = 0;
	}

	if (y_pos < 0) {
		pos_cpy.y_hotspot -= y_pos;
		y_pos = 0;
	}

	/* If the position on bottom MPC pipe is negative then we need to add to the hotspot and
	 * adjust x_pos on bottom pipe to make cursor visible when crossing between MPC slices.
	 */
	if (mpc_combine_on &&
		pipe_ctx->top_pipe &&
		(pipe_ctx == pipe_ctx->top_pipe->bottom_pipe)) {

		bottom_pipe_x_pos = x_pos - pipe_ctx->plane_res.scl_data.recout.x;
		if (bottom_pipe_x_pos < 0) {
			x_pos = pipe_ctx->plane_res.scl_data.recout.x;
			pos_cpy.x_hotspot -= bottom_pipe_x_pos;
			if (hubp->curs_attr.attribute_flags.bits.ENABLE_MAGNIFICATION)
				adjust_hotspot_between_slices_for_2x_magnify(hubp->curs_attr.width, &pos_cpy);
		}
	}

	pos_cpy.x = (uint32_t)x_pos;
	pos_cpy.y = (uint32_t)y_pos;

	if (pos_cpy.enable && dcn401_can_pipe_disable_cursor(pipe_ctx))
		pos_cpy.enable = false;

	x_pos = pos_cpy.x - param.recout.x;
	y_pos = pos_cpy.y - param.recout.y;

	recout_x_pos = x_pos - pos_cpy.x_hotspot;
	recout_y_pos = y_pos - pos_cpy.y_hotspot;

	if (recout_x_pos >= (int)param.recout.width)
		pos_cpy.enable = false;  /* not visible beyond right edge*/

	if (recout_y_pos >= (int)param.recout.height)
		pos_cpy.enable = false;  /* not visible beyond bottom edge*/

	if (recout_x_pos + (int)hubp->curs_attr.width <= 0)
		pos_cpy.enable = false;  /* not visible beyond left edge*/

	if (recout_y_pos + (int)hubp->curs_attr.height <= 0)
		pos_cpy.enable = false;  /* not visible beyond top edge*/

	hubp->funcs->set_cursor_position(hubp, &pos_cpy, &param);
	dpp->funcs->set_cursor_position(dpp, &pos_cpy, &param, hubp->curs_attr.width, hubp->curs_attr.height);
}

static bool dcn401_check_no_memory_request_for_cab(struct dc *dc)
{
	int i;

	/* First, check no-memory-request case */
	for (i = 0; i < dc->current_state->stream_count; i++) {
		if ((dc->current_state->stream_status[i].plane_count) &&
			(dc->current_state->streams[i]->link->psr_settings.psr_version == DC_PSR_VERSION_UNSUPPORTED))
			/* Fail eligibility on a visible stream */
			return false;
	}

	return true;
}

static uint32_t dcn401_calculate_cab_allocation(struct dc *dc, struct dc_state *ctx)
{
	int i;
	uint8_t num_ways = 0;
	uint32_t mall_ss_size_bytes = 0;

	mall_ss_size_bytes = ctx->bw_ctx.bw.dcn.mall_ss_size_bytes;
	// TODO add additional logic for PSR active stream exclusion optimization
	// mall_ss_psr_active_size_bytes = ctx->bw_ctx.bw.dcn.mall_ss_psr_active_size_bytes;

	// Include cursor size for CAB allocation
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &ctx->res_ctx.pipe_ctx[i];

		if (!pipe->stream || !pipe->plane_state)
			continue;

		mall_ss_size_bytes += dcn32_helper_calculate_mall_bytes_for_cursor(dc, pipe, false);
	}

	// Convert number of cache lines required to number of ways
	if (dc->debug.force_mall_ss_num_ways > 0)
		num_ways = dc->debug.force_mall_ss_num_ways;
	else if (dc->res_pool->funcs->calculate_mall_ways_from_bytes)
		num_ways = dc->res_pool->funcs->calculate_mall_ways_from_bytes(dc, mall_ss_size_bytes);
	else
		num_ways = 0;

	return num_ways;
}

bool dcn401_apply_idle_power_optimizations(struct dc *dc, bool enable)
{
	union dmub_rb_cmd cmd;
	uint8_t ways, i;
	int j;
	bool mall_ss_unsupported = false;
	struct dc_plane_state *plane = NULL;

	if (!dc->ctx->dmub_srv || !dc->current_state)
		return false;

	for (i = 0; i < dc->current_state->stream_count; i++) {
		/* MALL SS messaging is not supported with PSR at this time */
		if (dc->current_state->streams[i] != NULL &&
				dc->current_state->streams[i]->link->psr_settings.psr_version != DC_PSR_VERSION_UNSUPPORTED) {
			DC_LOG_MALL("MALL SS not supported with PSR at this time\n");
			return false;
		}
	}

	memset(&cmd, 0, sizeof(cmd));
	cmd.cab.header.type = DMUB_CMD__CAB_FOR_SS;
	cmd.cab.header.payload_bytes = sizeof(cmd.cab) - sizeof(cmd.cab.header);

	if (enable) {
		if (dcn401_check_no_memory_request_for_cab(dc)) {
			/* 1. Check no memory request case for CAB.
			 * If no memory request case, send CAB_ACTION NO_DCN_REQ DMUB message
			 */
			DC_LOG_MALL("sending CAB action NO_DCN_REQ\n");
			cmd.cab.header.sub_type = DMUB_CMD__CAB_NO_DCN_REQ;
		} else {
			/* 2. Check if all surfaces can fit in CAB.
			 * If surfaces can fit into CAB, send CAB_ACTION_ALLOW DMUB message
			 * and configure HUBP's to fetch from MALL
			 */
			ways = dcn401_calculate_cab_allocation(dc, dc->current_state);

			/* MALL not supported with Stereo3D or TMZ surface. If any plane is using stereo,
			 * or TMZ surface, don't try to enter MALL.
			 */
			for (i = 0; i < dc->current_state->stream_count; i++) {
				for (j = 0; j < dc->current_state->stream_status[i].plane_count; j++) {
					plane = dc->current_state->stream_status[i].plane_states[j];

					if (plane->address.type == PLN_ADDR_TYPE_GRPH_STEREO ||
							plane->address.tmz_surface) {
						mall_ss_unsupported = true;
						break;
					}
				}
				if (mall_ss_unsupported)
					break;
			}
			if (ways <= dc->caps.cache_num_ways && !mall_ss_unsupported) {
				cmd.cab.header.sub_type = DMUB_CMD__CAB_DCN_SS_FIT_IN_CAB;
				cmd.cab.cab_alloc_ways = ways;
				DC_LOG_MALL("cab allocation: %d ways. CAB action: DCN_SS_FIT_IN_CAB\n", ways);
			} else {
				cmd.cab.header.sub_type = DMUB_CMD__CAB_DCN_SS_NOT_FIT_IN_CAB;
				DC_LOG_MALL("frame does not fit in CAB: %d ways required. CAB action: DCN_SS_NOT_FIT_IN_CAB\n", ways);
			}
		}
	} else {
		/* Disable CAB */
		cmd.cab.header.sub_type = DMUB_CMD__CAB_NO_IDLE_OPTIMIZATION;
		DC_LOG_MALL("idle optimization disabled\n");
	}

	dm_execute_dmub_cmd(dc->ctx, &cmd, DM_DMUB_WAIT_TYPE_WAIT);

	return true;
}

void dcn401_wait_for_dcc_meta_propagation(const struct dc *dc,
		const struct pipe_ctx *top_pipe)
{
	bool is_wait_needed = false;
	const struct pipe_ctx *pipe_ctx = top_pipe;

	/* check if any surfaces are updating address while using flip immediate and dcc */
	while (pipe_ctx != NULL) {
		if (pipe_ctx->plane_state &&
				pipe_ctx->plane_state->dcc.enable &&
				pipe_ctx->plane_state->flip_immediate &&
				pipe_ctx->plane_state->update_flags.bits.addr_update) {
			is_wait_needed = true;
			break;
		}

		/* check next pipe */
		pipe_ctx = pipe_ctx->bottom_pipe;
	}

	if (is_wait_needed && dc->debug.dcc_meta_propagation_delay_us > 0) {
		udelay(dc->debug.dcc_meta_propagation_delay_us);
	}
}

void dcn401_prepare_bandwidth(struct dc *dc,
	struct dc_state *context)
{
	struct hubbub *hubbub = dc->res_pool->hubbub;
	bool p_state_change_support = context->bw_ctx.bw.dcn.clk.p_state_change_support;
	unsigned int compbuf_size = 0;

	/* Any transition into P-State support should disable MCLK switching first to avoid hangs */
	if (p_state_change_support) {
		dc->optimized_required = true;
		context->bw_ctx.bw.dcn.clk.p_state_change_support = false;
	}

	if (dc->clk_mgr->dc_mode_softmax_enabled)
		if (dc->clk_mgr->clks.dramclk_khz <= dc->clk_mgr->bw_params->dc_mode_softmax_memclk * 1000 &&
				context->bw_ctx.bw.dcn.clk.dramclk_khz > dc->clk_mgr->bw_params->dc_mode_softmax_memclk * 1000)
			dc->clk_mgr->funcs->set_max_memclk(dc->clk_mgr, dc->clk_mgr->bw_params->clk_table.entries[dc->clk_mgr->bw_params->clk_table.num_entries - 1].memclk_mhz);

	/* Increase clocks */
	dc->clk_mgr->funcs->update_clocks(
			dc->clk_mgr,
			context,
			false);

	/* program dchubbub watermarks:
	 * For assigning wm_optimized_required, use |= operator since we don't want
	 * to clear the value if the optimize has not happened yet
	 */
	dc->wm_optimized_required |= hubbub->funcs->program_watermarks(hubbub,
					&context->bw_ctx.bw.dcn.watermarks,
					dc->res_pool->ref_clocks.dchub_ref_clock_inKhz / 1000,
					false);

	/* decrease compbuf size */
	if (hubbub->funcs->program_compbuf_segments) {
		compbuf_size = context->bw_ctx.bw.dcn.arb_regs.compbuf_size;
		dc->wm_optimized_required |= (compbuf_size != dc->current_state->bw_ctx.bw.dcn.arb_regs.compbuf_size);

		hubbub->funcs->program_compbuf_segments(hubbub, compbuf_size, false);
	}

	if (dc->debug.fams2_config.bits.enable) {
		dcn401_fams2_global_control_lock(dc, context, true);
		dcn401_fams2_update_config(dc, context, false);
		dcn401_fams2_global_control_lock(dc, context, false);
	}

	if (p_state_change_support != context->bw_ctx.bw.dcn.clk.p_state_change_support) {
		/* After disabling P-State, restore the original value to ensure we get the correct P-State
		 * on the next optimize. */
		context->bw_ctx.bw.dcn.clk.p_state_change_support = p_state_change_support;
	}
}

void dcn401_optimize_bandwidth(
		struct dc *dc,
		struct dc_state *context)
{
	int i;
	struct hubbub *hubbub = dc->res_pool->hubbub;

	/* enable fams2 if needed */
	if (dc->debug.fams2_config.bits.enable) {
		dcn401_fams2_global_control_lock(dc, context, true);
		dcn401_fams2_update_config(dc, context, true);
		dcn401_fams2_global_control_lock(dc, context, false);
	}

	/* program dchubbub watermarks */
	hubbub->funcs->program_watermarks(hubbub,
					&context->bw_ctx.bw.dcn.watermarks,
					dc->res_pool->ref_clocks.dchub_ref_clock_inKhz / 1000,
					true);

	if (dc->clk_mgr->dc_mode_softmax_enabled)
		if (dc->clk_mgr->clks.dramclk_khz > dc->clk_mgr->bw_params->dc_mode_softmax_memclk * 1000 &&
				context->bw_ctx.bw.dcn.clk.dramclk_khz <= dc->clk_mgr->bw_params->dc_mode_softmax_memclk * 1000)
			dc->clk_mgr->funcs->set_max_memclk(dc->clk_mgr, dc->clk_mgr->bw_params->dc_mode_softmax_memclk);

	/* increase compbuf size */
	if (hubbub->funcs->program_compbuf_segments)
		hubbub->funcs->program_compbuf_segments(hubbub, context->bw_ctx.bw.dcn.arb_regs.compbuf_size, true);

	dc->clk_mgr->funcs->update_clocks(
			dc->clk_mgr,
			context,
			true);
	if (context->bw_ctx.bw.dcn.clk.zstate_support == DCN_ZSTATE_SUPPORT_ALLOW) {
		for (i = 0; i < dc->res_pool->pipe_count; ++i) {
			struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];

			if (pipe_ctx->stream && pipe_ctx->plane_res.hubp->funcs->program_extended_blank
				&& pipe_ctx->stream->adjust.v_total_min == pipe_ctx->stream->adjust.v_total_max
				&& pipe_ctx->stream->adjust.v_total_max > pipe_ctx->stream->timing.v_total)
					pipe_ctx->plane_res.hubp->funcs->program_extended_blank(pipe_ctx->plane_res.hubp,
						pipe_ctx->dlg_regs.min_dst_y_next_start);
		}
	}
}

void dcn401_fams2_global_control_lock(struct dc *dc,
		struct dc_state *context,
		bool lock)
{
	/* use always for now */
	union dmub_inbox0_cmd_lock_hw hw_lock_cmd = { 0 };

	if (!dc->ctx || !dc->ctx->dmub_srv || !dc->debug.fams2_config.bits.enable)
		return;

	hw_lock_cmd.bits.command_code = DMUB_INBOX0_CMD__HW_LOCK;
	hw_lock_cmd.bits.hw_lock_client = HW_LOCK_CLIENT_DRIVER;
	hw_lock_cmd.bits.lock = lock;
	hw_lock_cmd.bits.should_release = !lock;
	dmub_hw_lock_mgr_inbox0_cmd(dc->ctx->dmub_srv, hw_lock_cmd);
}

void dcn401_fams2_global_control_lock_fast(union block_sequence_params *params)
{
	struct dc *dc = params->fams2_global_control_lock_fast_params.dc;
	bool lock = params->fams2_global_control_lock_fast_params.lock;

	if (params->fams2_global_control_lock_fast_params.is_required) {
		union dmub_inbox0_cmd_lock_hw hw_lock_cmd = { 0 };

		hw_lock_cmd.bits.command_code = DMUB_INBOX0_CMD__HW_LOCK;
		hw_lock_cmd.bits.hw_lock_client = HW_LOCK_CLIENT_DRIVER;
		hw_lock_cmd.bits.lock = lock;
		hw_lock_cmd.bits.should_release = !lock;
		dmub_hw_lock_mgr_inbox0_cmd(dc->ctx->dmub_srv, hw_lock_cmd);
	}
}

void dcn401_fams2_update_config(struct dc *dc, struct dc_state *context, bool enable)
{
	bool fams2_required;

	if (!dc->ctx || !dc->ctx->dmub_srv || !dc->debug.fams2_config.bits.enable)
		return;

	fams2_required = context->bw_ctx.bw.dcn.fams2_global_config.features.bits.enable;

	dc_dmub_srv_fams2_update_config(dc, context, enable && fams2_required);
}

static void update_dsc_for_odm_change(struct dc *dc, struct dc_state *context,
		struct pipe_ctx *otg_master)
{
	int i;
	struct pipe_ctx *old_pipe;
	struct pipe_ctx *new_pipe;
	struct pipe_ctx *old_opp_heads[MAX_PIPES];
	struct pipe_ctx *old_otg_master;
	int old_opp_head_count = 0;

	old_otg_master = &dc->current_state->res_ctx.pipe_ctx[otg_master->pipe_idx];

	if (resource_is_pipe_type(old_otg_master, OTG_MASTER)) {
		old_opp_head_count = resource_get_opp_heads_for_otg_master(old_otg_master,
									   &dc->current_state->res_ctx,
									   old_opp_heads);
	} else {
		// DC cannot assume that the current state and the new state
		// share the same OTG pipe since this is not true when called
		// in the context of a commit stream not checked. Hence, set
		// old_otg_master to NULL to skip the DSC configuration.
		old_otg_master = NULL;
	}


	if (otg_master->stream_res.dsc)
		dcn32_update_dsc_on_stream(otg_master,
				otg_master->stream->timing.flags.DSC);
	if (old_otg_master && old_otg_master->stream_res.dsc) {
		for (i = 0; i < old_opp_head_count; i++) {
			old_pipe = old_opp_heads[i];
			new_pipe = &context->res_ctx.pipe_ctx[old_pipe->pipe_idx];
			if (old_pipe->stream_res.dsc && !new_pipe->stream_res.dsc)
				old_pipe->stream_res.dsc->funcs->dsc_disconnect(
						old_pipe->stream_res.dsc);
		}
	}
}

void dcn401_update_odm(struct dc *dc, struct dc_state *context,
		struct pipe_ctx *otg_master)
{
	struct pipe_ctx *opp_heads[MAX_PIPES];
	int opp_inst[MAX_PIPES] = {0};
	int opp_head_count;
	int odm_slice_width = resource_get_odm_slice_dst_width(otg_master, false);
	int last_odm_slice_width = resource_get_odm_slice_dst_width(otg_master, true);
	int i;

	opp_head_count = resource_get_opp_heads_for_otg_master(
			otg_master, &context->res_ctx, opp_heads);

	for (i = 0; i < opp_head_count; i++)
		opp_inst[i] = opp_heads[i]->stream_res.opp->inst;
	if (opp_head_count > 1)
		otg_master->stream_res.tg->funcs->set_odm_combine(
				otg_master->stream_res.tg,
				opp_inst, opp_head_count,
				odm_slice_width, last_odm_slice_width);
	else
		otg_master->stream_res.tg->funcs->set_odm_bypass(
				otg_master->stream_res.tg,
				&otg_master->stream->timing);

	for (i = 0; i < opp_head_count; i++) {
		opp_heads[i]->stream_res.opp->funcs->opp_pipe_clock_control(
				opp_heads[i]->stream_res.opp,
				true);
		opp_heads[i]->stream_res.opp->funcs->opp_program_left_edge_extra_pixel(
				opp_heads[i]->stream_res.opp,
				opp_heads[i]->stream->timing.pixel_encoding,
				resource_is_pipe_type(opp_heads[i], OTG_MASTER));
	}

	update_dsc_for_odm_change(dc, context, otg_master);

	if (!resource_is_pipe_type(otg_master, DPP_PIPE))
		/*
		 * blank pattern is generated by OPP, reprogram blank pattern
		 * due to OPP count change
		 */
		dc->hwseq->funcs.blank_pixel_data(dc, otg_master, true);
}

void dcn401_unblank_stream(struct pipe_ctx *pipe_ctx,
		struct dc_link_settings *link_settings)
{
	struct encoder_unblank_param params = {0};
	struct dc_stream_state *stream = pipe_ctx->stream;
	struct dc_link *link = stream->link;
	struct dce_hwseq *hws = link->dc->hwseq;

	/* calculate parameters for unblank */
	params.opp_cnt = resource_get_odm_slice_count(pipe_ctx);

	params.timing = pipe_ctx->stream->timing;
	params.link_settings.link_rate = link_settings->link_rate;
	params.pix_per_cycle = pipe_ctx->stream_res.pix_clk_params.dio_se_pix_per_cycle;

	if (link->dc->link_srv->dp_is_128b_132b_signal(pipe_ctx)) {
		pipe_ctx->stream_res.hpo_dp_stream_enc->funcs->dp_unblank(
				pipe_ctx->stream_res.hpo_dp_stream_enc,
				pipe_ctx->stream_res.tg->inst);
	} else if (dc_is_dp_signal(pipe_ctx->stream->signal)) {
		pipe_ctx->stream_res.stream_enc->funcs->dp_unblank(link, pipe_ctx->stream_res.stream_enc, &params);
	}

	if (link->local_sink && link->local_sink->sink_signal == SIGNAL_TYPE_EDP)
		hws->funcs.edp_backlight_control(link, true);
}

void dcn401_hardware_release(struct dc *dc)
{
	dc_dmub_srv_fams2_update_config(dc, dc->current_state, false);

	/* If pstate unsupported, or still supported
	 * by firmware, force it supported by dcn
	 */
	if (dc->current_state) {
		if ((!dc->clk_mgr->clks.p_state_change_support ||
				dc->current_state->bw_ctx.bw.dcn.fams2_global_config.features.bits.enable) &&
				dc->res_pool->hubbub->funcs->force_pstate_change_control)
			dc->res_pool->hubbub->funcs->force_pstate_change_control(
					dc->res_pool->hubbub, true, true);

		dc->current_state->bw_ctx.bw.dcn.clk.p_state_change_support = true;
		dc->clk_mgr->funcs->update_clocks(dc->clk_mgr, dc->current_state, true);
	}
}

void dcn401_wait_for_det_buffer_update(struct dc *dc, struct dc_state *context, struct pipe_ctx *otg_master)
{
	struct pipe_ctx *opp_heads[MAX_PIPES];
	struct pipe_ctx *dpp_pipes[MAX_PIPES];
	struct hubbub *hubbub = dc->res_pool->hubbub;
	int dpp_count = 0;

	if (!otg_master->stream)
		return;

	int slice_count = resource_get_opp_heads_for_otg_master(otg_master,
			&context->res_ctx, opp_heads);

	for (int slice_idx = 0; slice_idx < slice_count; slice_idx++) {
		if (opp_heads[slice_idx]->plane_state) {
			dpp_count = resource_get_dpp_pipes_for_opp_head(
					opp_heads[slice_idx],
					&context->res_ctx,
					dpp_pipes);
			for (int dpp_idx = 0; dpp_idx < dpp_count; dpp_idx++) {
				struct pipe_ctx *dpp_pipe = dpp_pipes[dpp_idx];
					if (dpp_pipe && hubbub &&
						dpp_pipe->plane_res.hubp &&
						hubbub->funcs->wait_for_det_update)
						hubbub->funcs->wait_for_det_update(hubbub, dpp_pipe->plane_res.hubp->inst);
			}
		}
	}
}

void dcn401_interdependent_update_lock(struct dc *dc,
		struct dc_state *context, bool lock)
{
	unsigned int i = 0;
	struct pipe_ctx *pipe = NULL;
	struct timing_generator *tg = NULL;
	bool pipe_unlocked[MAX_PIPES] = {0};

	if (lock) {
		for (i = 0; i < dc->res_pool->pipe_count; i++) {
			pipe = &context->res_ctx.pipe_ctx[i];
			tg = pipe->stream_res.tg;

			if (!resource_is_pipe_type(pipe, OTG_MASTER) ||
					!tg->funcs->is_tg_enabled(tg) ||
					dc_state_get_pipe_subvp_type(context, pipe) == SUBVP_PHANTOM)
				continue;
			dc->hwss.pipe_control_lock(dc, pipe, true);
		}
	} else {
		/* Unlock pipes based on the change in DET allocation instead of pipe index
		 * Prevents over allocation of DET during unlock process
		 * e.g. 2 pipe config with different streams with a max of 20 DET segments
		 *	Before:								After:
		 *		- Pipe0: 10 DET segments			- Pipe0: 12 DET segments
		 *		- Pipe1: 10 DET segments			- Pipe1: 8 DET segments
		 * If Pipe0 gets updated first, 22 DET segments will be allocated
		 */
		for (i = 0; i < dc->res_pool->pipe_count; i++) {
			pipe = &context->res_ctx.pipe_ctx[i];
			tg = pipe->stream_res.tg;
			int current_pipe_idx = i;

			if (!resource_is_pipe_type(pipe, OTG_MASTER) ||
					!tg->funcs->is_tg_enabled(tg) ||
					dc_state_get_pipe_subvp_type(context, pipe) == SUBVP_PHANTOM) {
				pipe_unlocked[i] = true;
				continue;
			}

			// If the same stream exists in old context, ensure the OTG_MASTER pipes for the same stream get compared
			struct pipe_ctx *old_otg_master = resource_get_otg_master_for_stream(&dc->current_state->res_ctx, pipe->stream);

			if (old_otg_master)
				current_pipe_idx = old_otg_master->pipe_idx;
			if (resource_calculate_det_for_stream(context, pipe) <
					resource_calculate_det_for_stream(dc->current_state, &dc->current_state->res_ctx.pipe_ctx[current_pipe_idx])) {
				dc->hwss.pipe_control_lock(dc, pipe, false);
				pipe_unlocked[i] = true;
				dcn401_wait_for_det_buffer_update(dc, context, pipe);
			}
		}

		for (i = 0; i < dc->res_pool->pipe_count; i++) {
			if (pipe_unlocked[i])
				continue;
			pipe = &context->res_ctx.pipe_ctx[i];
			dc->hwss.pipe_control_lock(dc, pipe, false);
		}
	}
}

void dcn401_program_outstanding_updates(struct dc *dc,
		struct dc_state *context)
{
	struct hubbub *hubbub = dc->res_pool->hubbub;

	/* update compbuf if required */
	if (hubbub->funcs->program_compbuf_segments)
		hubbub->funcs->program_compbuf_segments(hubbub, context->bw_ctx.bw.dcn.arb_regs.compbuf_size, true);
}

void dcn401_reset_back_end_for_pipe(
		struct dc *dc,
		struct pipe_ctx *pipe_ctx,
		struct dc_state *context)
{
	int i;
	struct dc_link *link = pipe_ctx->stream->link;
	const struct link_hwss *link_hwss = get_link_hwss(link, &pipe_ctx->link_res);

	DC_LOGGER_INIT(dc->ctx->logger);
	if (pipe_ctx->stream_res.stream_enc == NULL) {
		pipe_ctx->stream = NULL;
		return;
	}

	/* DPMS may already disable or */
	/* dpms_off status is incorrect due to fastboot
	 * feature. When system resume from S4 with second
	 * screen only, the dpms_off would be true but
	 * VBIOS lit up eDP, so check link status too.
	 */
	if (!pipe_ctx->stream->dpms_off || link->link_status.link_active)
		dc->link_srv->set_dpms_off(pipe_ctx);
	else if (pipe_ctx->stream_res.audio)
		dc->hwss.disable_audio_stream(pipe_ctx);

	/* free acquired resources */
	if (pipe_ctx->stream_res.audio) {
		/*disable az_endpoint*/
		pipe_ctx->stream_res.audio->funcs->az_disable(pipe_ctx->stream_res.audio);

		/*free audio*/
		if (dc->caps.dynamic_audio == true) {
			/*we have to dynamic arbitrate the audio endpoints*/
			/*we free the resource, need reset is_audio_acquired*/
			update_audio_usage(&dc->current_state->res_ctx, dc->res_pool,
					pipe_ctx->stream_res.audio, false);
			pipe_ctx->stream_res.audio = NULL;
		}
	}

	/* by upper caller loop, parent pipe: pipe0, will be reset last.
	 * back end share by all pipes and will be disable only when disable
	 * parent pipe.
	 */
	if (pipe_ctx->top_pipe == NULL) {

		dc->hwss.set_abm_immediate_disable(pipe_ctx);

		pipe_ctx->stream_res.tg->funcs->disable_crtc(pipe_ctx->stream_res.tg);

		pipe_ctx->stream_res.tg->funcs->enable_optc_clock(pipe_ctx->stream_res.tg, false);
		if (pipe_ctx->stream_res.tg->funcs->set_odm_bypass)
			pipe_ctx->stream_res.tg->funcs->set_odm_bypass(
					pipe_ctx->stream_res.tg, &pipe_ctx->stream->timing);

		set_drr_and_clear_adjust_pending(pipe_ctx, pipe_ctx->stream, NULL);

		/* TODO - convert symclk_ref_cnts for otg to a bit map to solve
		 * the case where the same symclk is shared across multiple otg
		 * instances
		 */
		if (dc_is_hdmi_tmds_signal(pipe_ctx->stream->signal))
			link->phy_state.symclk_ref_cnts.otg = 0;
		if (link->phy_state.symclk_state == SYMCLK_ON_TX_OFF) {
			link_hwss->disable_link_output(link,
					&pipe_ctx->link_res, pipe_ctx->stream->signal);
			link->phy_state.symclk_state = SYMCLK_OFF_TX_OFF;
		}

		/* reset DTBCLK_P */
		if (dc->res_pool->dccg->funcs->set_dtbclk_p_src)
			dc->res_pool->dccg->funcs->set_dtbclk_p_src(dc->res_pool->dccg, REFCLK, pipe_ctx->stream_res.tg->inst);
	}

	for (i = 0; i < dc->res_pool->pipe_count; i++)
		if (&dc->current_state->res_ctx.pipe_ctx[i] == pipe_ctx)
			break;

	if (i == dc->res_pool->pipe_count)
		return;

/*
 * In case of a dangling plane, setting this to NULL unconditionally
 * causes failures during reset hw ctx where, if stream is NULL,
 * it is expected that the pipe_ctx pointers to pipes and plane are NULL.
 */
	pipe_ctx->stream = NULL;
	DC_LOG_DEBUG("Reset back end for pipe %d, tg:%d\n",
					pipe_ctx->pipe_idx, pipe_ctx->stream_res.tg->inst);
}

void dcn401_reset_hw_ctx_wrap(
		struct dc *dc,
		struct dc_state *context)
{
	int i;
	struct dce_hwseq *hws = dc->hwseq;

	/* Reset Back End*/
	for (i = dc->res_pool->pipe_count - 1; i >= 0 ; i--) {
		struct pipe_ctx *pipe_ctx_old =
			&dc->current_state->res_ctx.pipe_ctx[i];
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];

		if (!pipe_ctx_old->stream)
			continue;

		if (pipe_ctx_old->top_pipe || pipe_ctx_old->prev_odm_pipe)
			continue;

		if (!pipe_ctx->stream ||
				pipe_need_reprogram(pipe_ctx_old, pipe_ctx)) {
			struct clock_source *old_clk = pipe_ctx_old->clock_source;

			if (hws->funcs.reset_back_end_for_pipe)
				hws->funcs.reset_back_end_for_pipe(dc, pipe_ctx_old, dc->current_state);
			if (hws->funcs.enable_stream_gating)
				hws->funcs.enable_stream_gating(dc, pipe_ctx_old);
			if (old_clk)
				old_clk->funcs->cs_power_down(old_clk);
		}
	}
}
