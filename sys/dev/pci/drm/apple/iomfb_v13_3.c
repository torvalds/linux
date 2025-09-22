// SPDX-License-Identifier: GPL-2.0-only OR MIT
/* Copyright The Asahi Linux Contributors */

#include "iomfb_v12_3.h"
#include "iomfb_v13_3.h"
#include "version_utils.h"

static const struct dcp_method_entry dcp_methods[dcpep_num_methods] = {
	IOMFB_METHOD("A000", dcpep_late_init_signal),
	IOMFB_METHOD("A029", dcpep_setup_video_limits),
	IOMFB_METHOD("A131", iomfbep_a131_pmu_service_matched),
	IOMFB_METHOD("A132", iomfbep_a132_backlight_service_matched),
	IOMFB_METHOD("A373", dcpep_set_create_dfb),
	IOMFB_METHOD("A374", iomfbep_a358_vi_set_temperature_hint),
	IOMFB_METHOD("A401", dcpep_start_signal),
	IOMFB_METHOD("A407", dcpep_swap_start),
	IOMFB_METHOD("A408", dcpep_swap_submit),
	IOMFB_METHOD("A410", dcpep_set_display_device),
	IOMFB_METHOD("A411", dcpep_is_main_display),
	IOMFB_METHOD("A412", dcpep_set_digital_out_mode),
	IOMFB_METHOD("A422", iomfbep_set_matrix),
	IOMFB_METHOD("A426", iomfbep_get_color_remap_mode),
	IOMFB_METHOD("A441", dcpep_set_parameter_dcp),
	IOMFB_METHOD("A445", dcpep_create_default_fb),
	IOMFB_METHOD("A449", dcpep_enable_disable_video_power_savings),
	IOMFB_METHOD("A456", dcpep_first_client_open),
	IOMFB_METHOD("A457", iomfbep_last_client_close),
	IOMFB_METHOD("A463", dcpep_set_display_refresh_properties),
	IOMFB_METHOD("A466", dcpep_flush_supports_power),
	IOMFB_METHOD("A467", iomfbep_abort_swaps_dcp),
	IOMFB_METHOD("A472", dcpep_set_power_state),
};

#define DCP_FW v13_3
#define DCP_FW_VER DCP_FW_VERSION(13, 3, 0)

#include "iomfb_template.c"

static const iomfb_cb_handler cb_handlers[IOMFB_MAX_CB] = {
	[0] = trampoline_true, /* did_boot_signal */
	[1] = trampoline_true, /* did_power_on_signal */
	[2] = trampoline_nop, /* will_power_off_signal */
	[3] = trampoline_rt_bandwidth,
	[6] = trampoline_set_frame_sync_props,
	[100] = iomfbep_cb_match_pmu_service,
	[101] = trampoline_zero, /* get_display_default_stride */
	[102] = trampoline_nop, /* set_number_property */
	[103] = trampoline_nop, /* trigger_user_cal_loader */
	[104] = trampoline_nop, /* set_boolean_property */
	[107] = trampoline_nop, /* remove_property */
	[108] = trampoline_true, /* create_provider_service */
	[109] = trampoline_true, /* create_product_service */
	[110] = trampoline_true, /* create_pmu_service */
	[111] = trampoline_true, /* create_iomfb_service */
	[112] = trampoline_create_backlight_service,
	[113] = trampoline_true, /* create_nvram_service? */
	[114] = trampoline_get_tiling_state,
	[115] = trampoline_false, /* set_tiling_state */
	[120] = dcpep_cb_boot_1,
	[121] = trampoline_false, /* is_dark_boot */
	[122] = trampoline_false, /* is_dark_boot / is_waking_from_hibernate*/
	[124] = trampoline_read_edt_data,
	[126] = trampoline_prop_start,
	[127] = trampoline_prop_chunk,
	[128] = trampoline_prop_end,
	[129] = trampoline_allocate_bandwidth,
	[201] = trampoline_map_piodma,
	[202] = trampoline_unmap_piodma,
	[206] = iomfbep_cb_match_pmu_service_2,
	[207] = iomfbep_cb_match_backlight_service,
	[208] = trampoline_nop, /* update_backlight_factor_prop */
	[209] = trampoline_get_time,
	[300] = trampoline_pr_publish,
	[401] = trampoline_get_uint_prop,
	[404] = trampoline_nop, /* sr_set_uint_prop */
	[406] = trampoline_set_fx_prop,
	[408] = trampoline_get_frequency,
	[411] = trampoline_map_reg,
	[413] = trampoline_true, /* sr_set_property_dict */
	[414] = trampoline_sr_set_property_int,
	[415] = trampoline_true, /* sr_set_property_bool */
	[451] = trampoline_allocate_buffer,
	[452] = trampoline_map_physical,
	[456] = trampoline_release_mem_desc,
	[552] = trampoline_true, /* set_property_dict_0 */
	[561] = trampoline_true, /* set_property_dict */
	[563] = trampoline_true, /* set_property_int */
	[565] = trampoline_true, /* set_property_bool */
	[567] = trampoline_true, /* set_property_str */
	[574] = trampoline_zero, /* power_up_dart */
	[576] = trampoline_hotplug,
	[577] = trampoline_nop, /* powerstate_notify */
	[582] = trampoline_true, /* create_default_fb_surface */
	[584] = trampoline_nop, /* IOMobileFramebufferAP::clear_default_surface */
	[588] = trampoline_nop, /* resize_default_fb_surface_gated */
	[589] = trampoline_swap_complete,
	[591] = trampoline_swap_complete_intent_gated,
	[592] = trampoline_abort_swap_ap_gated,
	[593] = trampoline_enable_backlight_message_ap_gated,
	[594] = trampoline_nop, /* IOMobileFramebufferAP::setSystemConsoleMode */
	[596] = trampoline_false, /* IOMobileFramebufferAP::isDFBAllocated */
	[597] = trampoline_false, /* IOMobileFramebufferAP::preserveContents */
	[598] = trampoline_nop, /* find_swap_function_gated */
};
void DCP_FW_NAME(iomfb_start)(struct apple_dcp *dcp)
{
	dcp->cb_handlers = cb_handlers;

	dcp_start_signal(dcp, false, dcp_started, NULL);
}

#undef DCP_FW_VER
#undef DCP_FW
