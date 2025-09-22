/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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

/* The caprices of the preprocessor require that this be declared right here */
#define CREATE_TRACE_POINTS

#include "dm_services_types.h"
#include "dc.h"
#include "link_enc_cfg.h"
#include "dc/inc/core_types.h"
#include "dal_asic_id.h"
#include "dmub/dmub_srv.h"
#include "dc/inc/hw/dmcu.h"
#include "dc/inc/hw/abm.h"
#include "dc/dc_dmub_srv.h"
#include "dc/dc_edid_parser.h"
#include "dc/dc_stat.h"
#include "dc/dc_state.h"
#include "amdgpu_dm_trace.h"
#include "dpcd_defs.h"
#include "link/protocols/link_dpcd.h"
#include "link_service_types.h"
#include "link/protocols/link_dp_capability.h"
#include "link/protocols/link_ddc.h"

#include "vid.h"
#include "amdgpu.h"
#include "amdgpu_display.h"
#include "amdgpu_ucode.h"
#include "atom.h"
#include "amdgpu_dm.h"
#include "amdgpu_dm_plane.h"
#include "amdgpu_dm_crtc.h"
#include "amdgpu_dm_hdcp.h"
#include <drm/display/drm_hdcp_helper.h>
#include "amdgpu_dm_wb.h"
#include "amdgpu_pm.h"
#include "amdgpu_atombios.h"

#include "amd_shared.h"
#include "amdgpu_dm_irq.h"
#include "dm_helpers.h"
#include "amdgpu_dm_mst_types.h"
#if defined(CONFIG_DEBUG_FS)
#include "amdgpu_dm_debugfs.h"
#endif
#include "amdgpu_dm_psr.h"
#include "amdgpu_dm_replay.h"

#include "ivsrcid/ivsrcid_vislands30.h"

#include <linux/backlight.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/pm_runtime.h>
#include <linux/pci.h>
#include <linux/power_supply.h>
#include <linux/firmware.h>
#include <linux/component.h>
#include <linux/dmi.h>
#include <linux/sort.h>

#include <drm/display/drm_dp_mst_helper.h>
#include <drm/display/drm_hdmi_helper.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_uapi.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_blend.h>
#include <drm/drm_fixed.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_edid.h>
#include <drm/drm_eld.h>
#include <drm/drm_vblank.h>
#include <drm/drm_audio_component.h>
#include <drm/drm_gem_atomic_helper.h>

#include <acpi/video.h>

#include "ivsrcid/dcn/irqsrcs_dcn_1_0.h"

#include "dcn/dcn_1_0_offset.h"
#include "dcn/dcn_1_0_sh_mask.h"
#include "soc15_hw_ip.h"
#include "soc15_common.h"
#include "vega10_ip_offset.h"

#include "gc/gc_11_0_0_offset.h"
#include "gc/gc_11_0_0_sh_mask.h"

#include "modules/inc/mod_freesync.h"
#include "modules/power/power_helpers.h"

#define FIRMWARE_RENOIR_DMUB "amdgpu/renoir_dmcub.bin"
MODULE_FIRMWARE(FIRMWARE_RENOIR_DMUB);
#define FIRMWARE_SIENNA_CICHLID_DMUB "amdgpu/sienna_cichlid_dmcub.bin"
MODULE_FIRMWARE(FIRMWARE_SIENNA_CICHLID_DMUB);
#define FIRMWARE_NAVY_FLOUNDER_DMUB "amdgpu/navy_flounder_dmcub.bin"
MODULE_FIRMWARE(FIRMWARE_NAVY_FLOUNDER_DMUB);
#define FIRMWARE_GREEN_SARDINE_DMUB "amdgpu/green_sardine_dmcub.bin"
MODULE_FIRMWARE(FIRMWARE_GREEN_SARDINE_DMUB);
#define FIRMWARE_VANGOGH_DMUB "amdgpu/vangogh_dmcub.bin"
MODULE_FIRMWARE(FIRMWARE_VANGOGH_DMUB);
#define FIRMWARE_DIMGREY_CAVEFISH_DMUB "amdgpu/dimgrey_cavefish_dmcub.bin"
MODULE_FIRMWARE(FIRMWARE_DIMGREY_CAVEFISH_DMUB);
#define FIRMWARE_BEIGE_GOBY_DMUB "amdgpu/beige_goby_dmcub.bin"
MODULE_FIRMWARE(FIRMWARE_BEIGE_GOBY_DMUB);
#define FIRMWARE_YELLOW_CARP_DMUB "amdgpu/yellow_carp_dmcub.bin"
MODULE_FIRMWARE(FIRMWARE_YELLOW_CARP_DMUB);
#define FIRMWARE_DCN_314_DMUB "amdgpu/dcn_3_1_4_dmcub.bin"
MODULE_FIRMWARE(FIRMWARE_DCN_314_DMUB);
#define FIRMWARE_DCN_315_DMUB "amdgpu/dcn_3_1_5_dmcub.bin"
MODULE_FIRMWARE(FIRMWARE_DCN_315_DMUB);
#define FIRMWARE_DCN316_DMUB "amdgpu/dcn_3_1_6_dmcub.bin"
MODULE_FIRMWARE(FIRMWARE_DCN316_DMUB);

#define FIRMWARE_DCN_V3_2_0_DMCUB "amdgpu/dcn_3_2_0_dmcub.bin"
MODULE_FIRMWARE(FIRMWARE_DCN_V3_2_0_DMCUB);
#define FIRMWARE_DCN_V3_2_1_DMCUB "amdgpu/dcn_3_2_1_dmcub.bin"
MODULE_FIRMWARE(FIRMWARE_DCN_V3_2_1_DMCUB);

#define FIRMWARE_RAVEN_DMCU		"amdgpu/raven_dmcu.bin"
MODULE_FIRMWARE(FIRMWARE_RAVEN_DMCU);

#define FIRMWARE_NAVI12_DMCU            "amdgpu/navi12_dmcu.bin"
MODULE_FIRMWARE(FIRMWARE_NAVI12_DMCU);

#define FIRMWARE_DCN_35_DMUB "amdgpu/dcn_3_5_dmcub.bin"
MODULE_FIRMWARE(FIRMWARE_DCN_35_DMUB);

#define FIRMWARE_DCN_351_DMUB "amdgpu/dcn_3_5_1_dmcub.bin"
MODULE_FIRMWARE(FIRMWARE_DCN_351_DMUB);

#define FIRMWARE_DCN_401_DMUB "amdgpu/dcn_4_0_1_dmcub.bin"
MODULE_FIRMWARE(FIRMWARE_DCN_401_DMUB);

/* Number of bytes in PSP header for firmware. */
#define PSP_HEADER_BYTES 0x100

/* Number of bytes in PSP footer for firmware. */
#define PSP_FOOTER_BYTES 0x100

/**
 * DOC: overview
 *
 * The AMDgpu display manager, **amdgpu_dm** (or even simpler,
 * **dm**) sits between DRM and DC. It acts as a liaison, converting DRM
 * requests into DC requests, and DC responses into DRM responses.
 *
 * The root control structure is &struct amdgpu_display_manager.
 */

/* basic init/fini API */
static int amdgpu_dm_init(struct amdgpu_device *adev);
static void amdgpu_dm_fini(struct amdgpu_device *adev);
static bool is_freesync_video_mode(const struct drm_display_mode *mode, struct amdgpu_dm_connector *aconnector);
static void reset_freesync_config_for_crtc(struct dm_crtc_state *new_crtc_state);

static enum drm_mode_subconnector get_subconnector_type(struct dc_link *link)
{
	switch (link->dpcd_caps.dongle_type) {
	case DISPLAY_DONGLE_NONE:
		return DRM_MODE_SUBCONNECTOR_Native;
	case DISPLAY_DONGLE_DP_VGA_CONVERTER:
		return DRM_MODE_SUBCONNECTOR_VGA;
	case DISPLAY_DONGLE_DP_DVI_CONVERTER:
	case DISPLAY_DONGLE_DP_DVI_DONGLE:
		return DRM_MODE_SUBCONNECTOR_DVID;
	case DISPLAY_DONGLE_DP_HDMI_CONVERTER:
	case DISPLAY_DONGLE_DP_HDMI_DONGLE:
		return DRM_MODE_SUBCONNECTOR_HDMIA;
	case DISPLAY_DONGLE_DP_HDMI_MISMATCHED_DONGLE:
	default:
		return DRM_MODE_SUBCONNECTOR_Unknown;
	}
}

static void update_subconnector_property(struct amdgpu_dm_connector *aconnector)
{
	struct dc_link *link = aconnector->dc_link;
	struct drm_connector *connector = &aconnector->base;
	enum drm_mode_subconnector subconnector = DRM_MODE_SUBCONNECTOR_Unknown;

	if (connector->connector_type != DRM_MODE_CONNECTOR_DisplayPort)
		return;

	if (aconnector->dc_sink)
		subconnector = get_subconnector_type(link);

	drm_object_property_set_value(&connector->base,
			connector->dev->mode_config.dp_subconnector_property,
			subconnector);
}

/*
 * initializes drm_device display related structures, based on the information
 * provided by DAL. The drm strcutures are: drm_crtc, drm_connector,
 * drm_encoder, drm_mode_config
 *
 * Returns 0 on success
 */
static int amdgpu_dm_initialize_drm_device(struct amdgpu_device *adev);
/* removes and deallocates the drm structures, created by the above function */
static void amdgpu_dm_destroy_drm_device(struct amdgpu_display_manager *dm);

static int amdgpu_dm_connector_init(struct amdgpu_display_manager *dm,
				    struct amdgpu_dm_connector *amdgpu_dm_connector,
				    u32 link_index,
				    struct amdgpu_encoder *amdgpu_encoder);
static int amdgpu_dm_encoder_init(struct drm_device *dev,
				  struct amdgpu_encoder *aencoder,
				  uint32_t link_index);

static int amdgpu_dm_connector_get_modes(struct drm_connector *connector);

static void amdgpu_dm_atomic_commit_tail(struct drm_atomic_state *state);

static int amdgpu_dm_atomic_check(struct drm_device *dev,
				  struct drm_atomic_state *state);

static void handle_hpd_irq_helper(struct amdgpu_dm_connector *aconnector);
static void handle_hpd_rx_irq(void *param);

static void amdgpu_dm_backlight_set_level(struct amdgpu_display_manager *dm,
					 int bl_idx,
					 u32 user_brightness);

static bool
is_timing_unchanged_for_freesync(struct drm_crtc_state *old_crtc_state,
				 struct drm_crtc_state *new_crtc_state);
/*
 * dm_vblank_get_counter
 *
 * @brief
 * Get counter for number of vertical blanks
 *
 * @param
 * struct amdgpu_device *adev - [in] desired amdgpu device
 * int disp_idx - [in] which CRTC to get the counter from
 *
 * @return
 * Counter for vertical blanks
 */
static u32 dm_vblank_get_counter(struct amdgpu_device *adev, int crtc)
{
	struct amdgpu_crtc *acrtc = NULL;

	if (crtc >= adev->mode_info.num_crtc)
		return 0;

	acrtc = adev->mode_info.crtcs[crtc];

	if (!acrtc->dm_irq_params.stream) {
		DRM_ERROR("dc_stream_state is NULL for crtc '%d'!\n",
			  crtc);
		return 0;
	}

	return dc_stream_get_vblank_counter(acrtc->dm_irq_params.stream);
}

static int dm_crtc_get_scanoutpos(struct amdgpu_device *adev, int crtc,
				  u32 *vbl, u32 *position)
{
	u32 v_blank_start = 0, v_blank_end = 0, h_position = 0, v_position = 0;
	struct amdgpu_crtc *acrtc = NULL;
	struct dc *dc = adev->dm.dc;

	if ((crtc < 0) || (crtc >= adev->mode_info.num_crtc))
		return -EINVAL;

	acrtc = adev->mode_info.crtcs[crtc];

	if (!acrtc->dm_irq_params.stream) {
		DRM_ERROR("dc_stream_state is NULL for crtc '%d'!\n",
			  crtc);
		return 0;
	}

	if (dc && dc->caps.ips_support && dc->idle_optimizations_allowed)
		dc_allow_idle_optimizations(dc, false);

	/*
	 * TODO rework base driver to use values directly.
	 * for now parse it back into reg-format
	 */
	dc_stream_get_scanoutpos(acrtc->dm_irq_params.stream,
				 &v_blank_start,
				 &v_blank_end,
				 &h_position,
				 &v_position);

	*position = v_position | (h_position << 16);
	*vbl = v_blank_start | (v_blank_end << 16);

	return 0;
}

static bool dm_is_idle(void *handle)
{
	/* XXX todo */
	return true;
}

static int dm_wait_for_idle(void *handle)
{
	/* XXX todo */
	return 0;
}

static bool dm_check_soft_reset(void *handle)
{
	return false;
}

static int dm_soft_reset(void *handle)
{
	/* XXX todo */
	return 0;
}

static struct amdgpu_crtc *
get_crtc_by_otg_inst(struct amdgpu_device *adev,
		     int otg_inst)
{
	struct drm_device *dev = adev_to_drm(adev);
	struct drm_crtc *crtc;
	struct amdgpu_crtc *amdgpu_crtc;

	if (WARN_ON(otg_inst == -1))
		return adev->mode_info.crtcs[0];

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		amdgpu_crtc = to_amdgpu_crtc(crtc);

		if (amdgpu_crtc->otg_inst == otg_inst)
			return amdgpu_crtc;
	}

	return NULL;
}

static inline bool is_dc_timing_adjust_needed(struct dm_crtc_state *old_state,
					      struct dm_crtc_state *new_state)
{
	if (new_state->stream->adjust.timing_adjust_pending)
		return true;
	if (new_state->freesync_config.state ==  VRR_STATE_ACTIVE_FIXED)
		return true;
	else if (amdgpu_dm_crtc_vrr_active(old_state) != amdgpu_dm_crtc_vrr_active(new_state))
		return true;
	else
		return false;
}

/*
 * DC will program planes with their z-order determined by their ordering
 * in the dc_surface_updates array. This comparator is used to sort them
 * by descending zpos.
 */
static int dm_plane_layer_index_cmp(const void *a, const void *b)
{
	const struct dc_surface_update *sa = (struct dc_surface_update *)a;
	const struct dc_surface_update *sb = (struct dc_surface_update *)b;

	/* Sort by descending dc_plane layer_index (i.e. normalized_zpos) */
	return sb->surface->layer_index - sa->surface->layer_index;
}

/**
 * update_planes_and_stream_adapter() - Send planes to be updated in DC
 *
 * DC has a generic way to update planes and stream via
 * dc_update_planes_and_stream function; however, DM might need some
 * adjustments and preparation before calling it. This function is a wrapper
 * for the dc_update_planes_and_stream that does any required configuration
 * before passing control to DC.
 *
 * @dc: Display Core control structure
 * @update_type: specify whether it is FULL/MEDIUM/FAST update
 * @planes_count: planes count to update
 * @stream: stream state
 * @stream_update: stream update
 * @array_of_surface_update: dc surface update pointer
 *
 */
static inline bool update_planes_and_stream_adapter(struct dc *dc,
						    int update_type,
						    int planes_count,
						    struct dc_stream_state *stream,
						    struct dc_stream_update *stream_update,
						    struct dc_surface_update *array_of_surface_update)
{
	sort(array_of_surface_update, planes_count,
	     sizeof(*array_of_surface_update), dm_plane_layer_index_cmp, NULL);

	/*
	 * Previous frame finished and HW is ready for optimization.
	 */
	if (update_type == UPDATE_TYPE_FAST)
		dc_post_update_surfaces_to_stream(dc);

	return dc_update_planes_and_stream(dc,
					   array_of_surface_update,
					   planes_count,
					   stream,
					   stream_update);
}

/**
 * dm_pflip_high_irq() - Handle pageflip interrupt
 * @interrupt_params: ignored
 *
 * Handles the pageflip interrupt by notifying all interested parties
 * that the pageflip has been completed.
 */
static void dm_pflip_high_irq(void *interrupt_params)
{
	struct amdgpu_crtc *amdgpu_crtc;
	struct common_irq_params *irq_params = interrupt_params;
	struct amdgpu_device *adev = irq_params->adev;
	struct drm_device *dev = adev_to_drm(adev);
	unsigned long flags;
	struct drm_pending_vblank_event *e;
	u32 vpos, hpos, v_blank_start, v_blank_end;
	bool vrr_active;

	amdgpu_crtc = get_crtc_by_otg_inst(adev, irq_params->irq_src - IRQ_TYPE_PFLIP);

	/* IRQ could occur when in initial stage */
	/* TODO work and BO cleanup */
	if (amdgpu_crtc == NULL) {
		drm_dbg_state(dev, "CRTC is null, returning.\n");
		return;
	}

	spin_lock_irqsave(&adev_to_drm(adev)->event_lock, flags);

	if (amdgpu_crtc->pflip_status != AMDGPU_FLIP_SUBMITTED) {
		drm_dbg_state(dev,
			      "amdgpu_crtc->pflip_status = %d != AMDGPU_FLIP_SUBMITTED(%d) on crtc:%d[%p]\n",
			      amdgpu_crtc->pflip_status, AMDGPU_FLIP_SUBMITTED,
			      amdgpu_crtc->crtc_id, amdgpu_crtc);
		spin_unlock_irqrestore(&adev_to_drm(adev)->event_lock, flags);
		return;
	}

	/* page flip completed. */
	e = amdgpu_crtc->event;
	amdgpu_crtc->event = NULL;

	WARN_ON(!e);

	vrr_active = amdgpu_dm_crtc_vrr_active_irq(amdgpu_crtc);

	/* Fixed refresh rate, or VRR scanout position outside front-porch? */
	if (!vrr_active ||
	    !dc_stream_get_scanoutpos(amdgpu_crtc->dm_irq_params.stream, &v_blank_start,
				      &v_blank_end, &hpos, &vpos) ||
	    (vpos < v_blank_start)) {
		/* Update to correct count and vblank timestamp if racing with
		 * vblank irq. This also updates to the correct vblank timestamp
		 * even in VRR mode, as scanout is past the front-porch atm.
		 */
		drm_crtc_accurate_vblank_count(&amdgpu_crtc->base);

		/* Wake up userspace by sending the pageflip event with proper
		 * count and timestamp of vblank of flip completion.
		 */
		if (e) {
			drm_crtc_send_vblank_event(&amdgpu_crtc->base, e);

			/* Event sent, so done with vblank for this flip */
			drm_crtc_vblank_put(&amdgpu_crtc->base);
		}
	} else if (e) {
		/* VRR active and inside front-porch: vblank count and
		 * timestamp for pageflip event will only be up to date after
		 * drm_crtc_handle_vblank() has been executed from late vblank
		 * irq handler after start of back-porch (vline 0). We queue the
		 * pageflip event for send-out by drm_crtc_handle_vblank() with
		 * updated timestamp and count, once it runs after us.
		 *
		 * We need to open-code this instead of using the helper
		 * drm_crtc_arm_vblank_event(), as that helper would
		 * call drm_crtc_accurate_vblank_count(), which we must
		 * not call in VRR mode while we are in front-porch!
		 */

		/* sequence will be replaced by real count during send-out. */
		e->sequence = drm_crtc_vblank_count(&amdgpu_crtc->base);
		e->pipe = amdgpu_crtc->crtc_id;

		list_add_tail(&e->base.link, &adev_to_drm(adev)->vblank_event_list);
		e = NULL;
	}

	/* Keep track of vblank of this flip for flip throttling. We use the
	 * cooked hw counter, as that one incremented at start of this vblank
	 * of pageflip completion, so last_flip_vblank is the forbidden count
	 * for queueing new pageflips if vsync + VRR is enabled.
	 */
	amdgpu_crtc->dm_irq_params.last_flip_vblank =
		amdgpu_get_vblank_counter_kms(&amdgpu_crtc->base);

	amdgpu_crtc->pflip_status = AMDGPU_FLIP_NONE;
	spin_unlock_irqrestore(&adev_to_drm(adev)->event_lock, flags);

	drm_dbg_state(dev,
		      "crtc:%d[%p], pflip_stat:AMDGPU_FLIP_NONE, vrr[%d]-fp %d\n",
		      amdgpu_crtc->crtc_id, amdgpu_crtc, vrr_active, (int)!e);
}

static void dm_vupdate_high_irq(void *interrupt_params)
{
	struct common_irq_params *irq_params = interrupt_params;
	struct amdgpu_device *adev = irq_params->adev;
	struct amdgpu_crtc *acrtc;
	struct drm_device *drm_dev;
	struct drm_vblank_crtc *vblank;
	ktime_t frame_duration_ns, previous_timestamp;
	unsigned long flags;
	int vrr_active;

	acrtc = get_crtc_by_otg_inst(adev, irq_params->irq_src - IRQ_TYPE_VUPDATE);

	if (acrtc) {
		vrr_active = amdgpu_dm_crtc_vrr_active_irq(acrtc);
		drm_dev = acrtc->base.dev;
		vblank = drm_crtc_vblank_crtc(&acrtc->base);
		previous_timestamp = atomic64_read(&irq_params->previous_timestamp);
		frame_duration_ns = vblank->time - previous_timestamp;

		if (frame_duration_ns > 0) {
			trace_amdgpu_refresh_rate_track(acrtc->base.index,
						frame_duration_ns,
						ktime_divns(NSEC_PER_SEC, frame_duration_ns));
			atomic64_set(&irq_params->previous_timestamp, vblank->time);
		}

		drm_dbg_vbl(drm_dev,
			    "crtc:%d, vupdate-vrr:%d\n", acrtc->crtc_id,
			    vrr_active);

		/* Core vblank handling is done here after end of front-porch in
		 * vrr mode, as vblank timestamping will give valid results
		 * while now done after front-porch. This will also deliver
		 * page-flip completion events that have been queued to us
		 * if a pageflip happened inside front-porch.
		 */
		if (vrr_active) {
			amdgpu_dm_crtc_handle_vblank(acrtc);

			/* BTR processing for pre-DCE12 ASICs */
			if (acrtc->dm_irq_params.stream &&
			    adev->family < AMDGPU_FAMILY_AI) {
				spin_lock_irqsave(&adev_to_drm(adev)->event_lock, flags);
				mod_freesync_handle_v_update(
				    adev->dm.freesync_module,
				    acrtc->dm_irq_params.stream,
				    &acrtc->dm_irq_params.vrr_params);

				dc_stream_adjust_vmin_vmax(
				    adev->dm.dc,
				    acrtc->dm_irq_params.stream,
				    &acrtc->dm_irq_params.vrr_params.adjust);
				spin_unlock_irqrestore(&adev_to_drm(adev)->event_lock, flags);
			}
		}
	}
}

/**
 * dm_crtc_high_irq() - Handles CRTC interrupt
 * @interrupt_params: used for determining the CRTC instance
 *
 * Handles the CRTC/VSYNC interrupt by notfying DRM's VBLANK
 * event handler.
 */
static void dm_crtc_high_irq(void *interrupt_params)
{
	struct common_irq_params *irq_params = interrupt_params;
	struct amdgpu_device *adev = irq_params->adev;
	struct drm_writeback_job *job;
	struct amdgpu_crtc *acrtc;
	unsigned long flags;
	int vrr_active;

	acrtc = get_crtc_by_otg_inst(adev, irq_params->irq_src - IRQ_TYPE_VBLANK);
	if (!acrtc)
		return;

	if (acrtc->wb_conn) {
		STUB();
		return;
#ifdef notyet
		spin_lock_irqsave(&acrtc->wb_conn->job_lock, flags);

		if (acrtc->wb_pending) {
			job = list_first_entry_or_null(&acrtc->wb_conn->job_queue,
						       struct drm_writeback_job,
						       list_entry);
			acrtc->wb_pending = false;
			spin_unlock_irqrestore(&acrtc->wb_conn->job_lock, flags);

			if (job) {
				unsigned int v_total, refresh_hz;
				struct dc_stream_state *stream = acrtc->dm_irq_params.stream;

				v_total = stream->adjust.v_total_max ?
					  stream->adjust.v_total_max : stream->timing.v_total;
				refresh_hz = div_u64((uint64_t) stream->timing.pix_clk_100hz *
					     100LL, (v_total * stream->timing.h_total));
				mdelay(1000 / refresh_hz);

				drm_writeback_signal_completion(acrtc->wb_conn, 0);
				dc_stream_fc_disable_writeback(adev->dm.dc,
							       acrtc->dm_irq_params.stream, 0);
			}
		} else
			spin_unlock_irqrestore(&acrtc->wb_conn->job_lock, flags);
#endif
	}

	vrr_active = amdgpu_dm_crtc_vrr_active_irq(acrtc);

	drm_dbg_vbl(adev_to_drm(adev),
		    "crtc:%d, vupdate-vrr:%d, planes:%d\n", acrtc->crtc_id,
		    vrr_active, acrtc->dm_irq_params.active_planes);

	/**
	 * Core vblank handling at start of front-porch is only possible
	 * in non-vrr mode, as only there vblank timestamping will give
	 * valid results while done in front-porch. Otherwise defer it
	 * to dm_vupdate_high_irq after end of front-porch.
	 */
	if (!vrr_active)
		amdgpu_dm_crtc_handle_vblank(acrtc);

	/**
	 * Following stuff must happen at start of vblank, for crc
	 * computation and below-the-range btr support in vrr mode.
	 */
	amdgpu_dm_crtc_handle_crc_irq(&acrtc->base);

	/* BTR updates need to happen before VUPDATE on Vega and above. */
	if (adev->family < AMDGPU_FAMILY_AI)
		return;

	spin_lock_irqsave(&adev_to_drm(adev)->event_lock, flags);

	if (acrtc->dm_irq_params.stream &&
	    acrtc->dm_irq_params.vrr_params.supported &&
	    acrtc->dm_irq_params.freesync_config.state ==
		    VRR_STATE_ACTIVE_VARIABLE) {
		mod_freesync_handle_v_update(adev->dm.freesync_module,
					     acrtc->dm_irq_params.stream,
					     &acrtc->dm_irq_params.vrr_params);

		dc_stream_adjust_vmin_vmax(adev->dm.dc, acrtc->dm_irq_params.stream,
					   &acrtc->dm_irq_params.vrr_params.adjust);
	}

	/*
	 * If there aren't any active_planes then DCH HUBP may be clock-gated.
	 * In that case, pageflip completion interrupts won't fire and pageflip
	 * completion events won't get delivered. Prevent this by sending
	 * pending pageflip events from here if a flip is still pending.
	 *
	 * If any planes are enabled, use dm_pflip_high_irq() instead, to
	 * avoid race conditions between flip programming and completion,
	 * which could cause too early flip completion events.
	 */
	if (adev->family >= AMDGPU_FAMILY_RV &&
	    acrtc->pflip_status == AMDGPU_FLIP_SUBMITTED &&
	    acrtc->dm_irq_params.active_planes == 0) {
		if (acrtc->event) {
			drm_crtc_send_vblank_event(&acrtc->base, acrtc->event);
			acrtc->event = NULL;
			drm_crtc_vblank_put(&acrtc->base);
		}
		acrtc->pflip_status = AMDGPU_FLIP_NONE;
	}

	spin_unlock_irqrestore(&adev_to_drm(adev)->event_lock, flags);
}

#if defined(CONFIG_DRM_AMD_SECURE_DISPLAY)
/**
 * dm_dcn_vertical_interrupt0_high_irq() - Handles OTG Vertical interrupt0 for
 * DCN generation ASICs
 * @interrupt_params: interrupt parameters
 *
 * Used to set crc window/read out crc value at vertical line 0 position
 */
static void dm_dcn_vertical_interrupt0_high_irq(void *interrupt_params)
{
	struct common_irq_params *irq_params = interrupt_params;
	struct amdgpu_device *adev = irq_params->adev;
	struct amdgpu_crtc *acrtc;

	acrtc = get_crtc_by_otg_inst(adev, irq_params->irq_src - IRQ_TYPE_VLINE0);

	if (!acrtc)
		return;

	amdgpu_dm_crtc_handle_crc_window_irq(&acrtc->base);
}
#endif /* CONFIG_DRM_AMD_SECURE_DISPLAY */

/**
 * dmub_aux_setconfig_callback - Callback for AUX or SET_CONFIG command.
 * @adev: amdgpu_device pointer
 * @notify: dmub notification structure
 *
 * Dmub AUX or SET_CONFIG command completion processing callback
 * Copies dmub notification to DM which is to be read by AUX command.
 * issuing thread and also signals the event to wake up the thread.
 */
static void dmub_aux_setconfig_callback(struct amdgpu_device *adev,
					struct dmub_notification *notify)
{
	if (adev->dm.dmub_notify)
		memcpy(adev->dm.dmub_notify, notify, sizeof(struct dmub_notification));
	if (notify->type == DMUB_NOTIFICATION_AUX_REPLY)
		complete(&adev->dm.dmub_aux_transfer_done);
}

/**
 * dmub_hpd_callback - DMUB HPD interrupt processing callback.
 * @adev: amdgpu_device pointer
 * @notify: dmub notification structure
 *
 * Dmub Hpd interrupt processing callback. Gets displayindex through the
 * ink index and calls helper to do the processing.
 */
static void dmub_hpd_callback(struct amdgpu_device *adev,
			      struct dmub_notification *notify)
{
	struct amdgpu_dm_connector *aconnector;
	struct amdgpu_dm_connector *hpd_aconnector = NULL;
	struct drm_connector *connector;
	struct drm_connector_list_iter iter;
	struct dc_link *link;
	u8 link_index = 0;
	struct drm_device *dev;

	if (adev == NULL)
		return;

	if (notify == NULL) {
		DRM_ERROR("DMUB HPD callback notification was NULL");
		return;
	}

	if (notify->link_index > adev->dm.dc->link_count) {
		DRM_ERROR("DMUB HPD index (%u)is abnormal", notify->link_index);
		return;
	}

	/* Skip DMUB HPD IRQ in suspend/resume. We will probe them later. */
	if (notify->type == DMUB_NOTIFICATION_HPD && adev->in_suspend) {
		DRM_INFO("Skip DMUB HPD IRQ callback in suspend/resume\n");
		return;
	}

	link_index = notify->link_index;
	link = adev->dm.dc->links[link_index];
	dev = adev->dm.ddev;

	drm_connector_list_iter_begin(dev, &iter);
	drm_for_each_connector_iter(connector, &iter) {

		if (connector->connector_type == DRM_MODE_CONNECTOR_WRITEBACK)
			continue;

		aconnector = to_amdgpu_dm_connector(connector);
		if (link && aconnector->dc_link == link) {
			if (notify->type == DMUB_NOTIFICATION_HPD)
				DRM_INFO("DMUB HPD IRQ callback: link_index=%u\n", link_index);
			else if (notify->type == DMUB_NOTIFICATION_HPD_IRQ)
				DRM_INFO("DMUB HPD RX IRQ callback: link_index=%u\n", link_index);
			else
				DRM_WARN("DMUB Unknown HPD callback type %d, link_index=%u\n",
						notify->type, link_index);

			hpd_aconnector = aconnector;
			break;
		}
	}
	drm_connector_list_iter_end(&iter);

	if (hpd_aconnector) {
		if (notify->type == DMUB_NOTIFICATION_HPD) {
			if (hpd_aconnector->dc_link->hpd_status == (notify->hpd_status == DP_HPD_PLUG))
				DRM_WARN("DMUB reported hpd status unchanged. link_index=%u\n", link_index);
			handle_hpd_irq_helper(hpd_aconnector);
		} else if (notify->type == DMUB_NOTIFICATION_HPD_IRQ) {
			handle_hpd_rx_irq(hpd_aconnector);
		}
	}
}

/**
 * dmub_hpd_sense_callback - DMUB HPD sense processing callback.
 * @adev: amdgpu_device pointer
 * @notify: dmub notification structure
 *
 * HPD sense changes can occur during low power states and need to be
 * notified from firmware to driver.
 */
static void dmub_hpd_sense_callback(struct amdgpu_device *adev,
			      struct dmub_notification *notify)
{
	DRM_DEBUG_DRIVER("DMUB HPD SENSE callback.\n");
}

/**
 * register_dmub_notify_callback - Sets callback for DMUB notify
 * @adev: amdgpu_device pointer
 * @type: Type of dmub notification
 * @callback: Dmub interrupt callback function
 * @dmub_int_thread_offload: offload indicator
 *
 * API to register a dmub callback handler for a dmub notification
 * Also sets indicator whether callback processing to be offloaded.
 * to dmub interrupt handling thread
 * Return: true if successfully registered, false if there is existing registration
 */
static bool register_dmub_notify_callback(struct amdgpu_device *adev,
					  enum dmub_notification_type type,
					  dmub_notify_interrupt_callback_t callback,
					  bool dmub_int_thread_offload)
{
	if (callback != NULL && type < ARRAY_SIZE(adev->dm.dmub_thread_offload)) {
		adev->dm.dmub_callback[type] = callback;
		adev->dm.dmub_thread_offload[type] = dmub_int_thread_offload;
	} else
		return false;

	return true;
}

static void dm_handle_hpd_work(struct work_struct *work)
{
	struct dmub_hpd_work *dmub_hpd_wrk;

	dmub_hpd_wrk = container_of(work, struct dmub_hpd_work, handle_hpd_work);

	if (!dmub_hpd_wrk->dmub_notify) {
		DRM_ERROR("dmub_hpd_wrk dmub_notify is NULL");
		return;
	}

	if (dmub_hpd_wrk->dmub_notify->type < ARRAY_SIZE(dmub_hpd_wrk->adev->dm.dmub_callback)) {
		dmub_hpd_wrk->adev->dm.dmub_callback[dmub_hpd_wrk->dmub_notify->type](dmub_hpd_wrk->adev,
		dmub_hpd_wrk->dmub_notify);
	}

	kfree(dmub_hpd_wrk->dmub_notify);
	kfree(dmub_hpd_wrk);

}

#define DMUB_TRACE_MAX_READ 64
/**
 * dm_dmub_outbox1_low_irq() - Handles Outbox interrupt
 * @interrupt_params: used for determining the Outbox instance
 *
 * Handles the Outbox Interrupt
 * event handler.
 */
static void dm_dmub_outbox1_low_irq(void *interrupt_params)
{
	struct dmub_notification notify = {0};
	struct common_irq_params *irq_params = interrupt_params;
	struct amdgpu_device *adev = irq_params->adev;
	struct amdgpu_display_manager *dm = &adev->dm;
	struct dmcub_trace_buf_entry entry = { 0 };
	u32 count = 0;
	struct dmub_hpd_work *dmub_hpd_wrk;
	static const char *const event_type[] = {
		"NO_DATA",
		"AUX_REPLY",
		"HPD",
		"HPD_IRQ",
		"SET_CONFIGC_REPLY",
		"DPIA_NOTIFICATION",
		"HPD_SENSE_NOTIFY",
	};

	do {
		if (dc_dmub_srv_get_dmub_outbox0_msg(dm->dc, &entry)) {
			trace_amdgpu_dmub_trace_high_irq(entry.trace_code, entry.tick_count,
							entry.param0, entry.param1);

			DRM_DEBUG_DRIVER("trace_code:%u, tick_count:%u, param0:%u, param1:%u\n",
				 entry.trace_code, entry.tick_count, entry.param0, entry.param1);
		} else
			break;

		count++;

	} while (count <= DMUB_TRACE_MAX_READ);

	if (count > DMUB_TRACE_MAX_READ)
		DRM_DEBUG_DRIVER("Warning : count > DMUB_TRACE_MAX_READ");

	if (dc_enable_dmub_notifications(adev->dm.dc) &&
		irq_params->irq_src == DC_IRQ_SOURCE_DMCUB_OUTBOX) {

		do {
			dc_stat_get_dmub_notification(adev->dm.dc, &notify);
			if (notify.type >= ARRAY_SIZE(dm->dmub_thread_offload)) {
				DRM_ERROR("DM: notify type %d invalid!", notify.type);
				continue;
			}
			if (!dm->dmub_callback[notify.type]) {
				DRM_WARN("DMUB notification skipped due to no handler: type=%s\n",
					event_type[notify.type]);
				continue;
			}
			if (dm->dmub_thread_offload[notify.type] == true) {
				dmub_hpd_wrk = kzalloc(sizeof(*dmub_hpd_wrk), GFP_ATOMIC);
				if (!dmub_hpd_wrk) {
					DRM_ERROR("Failed to allocate dmub_hpd_wrk");
					return;
				}
				dmub_hpd_wrk->dmub_notify = kmemdup(&notify, sizeof(struct dmub_notification),
								    GFP_ATOMIC);
				if (!dmub_hpd_wrk->dmub_notify) {
					kfree(dmub_hpd_wrk);
					DRM_ERROR("Failed to allocate dmub_hpd_wrk->dmub_notify");
					return;
				}
				INIT_WORK(&dmub_hpd_wrk->handle_hpd_work, dm_handle_hpd_work);
				dmub_hpd_wrk->adev = adev;
				queue_work(adev->dm.delayed_hpd_wq, &dmub_hpd_wrk->handle_hpd_work);
			} else {
				dm->dmub_callback[notify.type](adev, &notify);
			}
		} while (notify.pending_notification);
	}
}

static int dm_set_clockgating_state(void *handle,
		  enum amd_clockgating_state state)
{
	return 0;
}

static int dm_set_powergating_state(void *handle,
		  enum amd_powergating_state state)
{
	return 0;
}

/* Prototypes of private functions */
static int dm_early_init(void *handle);

/* Allocate memory for FBC compressed data  */
static void amdgpu_dm_fbc_init(struct drm_connector *connector)
{
	struct amdgpu_device *adev = drm_to_adev(connector->dev);
	struct dm_compressor_info *compressor = &adev->dm.compressor;
	struct amdgpu_dm_connector *aconn = to_amdgpu_dm_connector(connector);
	struct drm_display_mode *mode;
	unsigned long max_size = 0;

	if (adev->dm.dc->fbc_compressor == NULL)
		return;

	if (aconn->dc_link->connector_signal != SIGNAL_TYPE_EDP)
		return;

	if (compressor->bo_ptr)
		return;


	list_for_each_entry(mode, &connector->modes, head) {
		if (max_size < (unsigned long) mode->htotal * mode->vtotal)
			max_size = (unsigned long) mode->htotal * mode->vtotal;
	}

	if (max_size) {
		int r = amdgpu_bo_create_kernel(adev, max_size * 4, PAGE_SIZE,
			    AMDGPU_GEM_DOMAIN_GTT, &compressor->bo_ptr,
			    &compressor->gpu_addr, &compressor->cpu_addr);

		if (r)
			DRM_ERROR("DM: Failed to initialize FBC\n");
		else {
			adev->dm.dc->ctx->fbc_gpu_addr = compressor->gpu_addr;
			DRM_INFO("DM: FBC alloc %lu\n", max_size*4);
		}

	}

}

static int amdgpu_dm_audio_component_get_eld(struct device *kdev, int port,
					  int pipe, bool *enabled,
					  unsigned char *buf, int max_bytes)
{
	struct drm_device *dev = dev_get_drvdata(kdev);
	struct amdgpu_device *adev = drm_to_adev(dev);
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;
	struct amdgpu_dm_connector *aconnector;
	int ret = 0;

	*enabled = false;

	mutex_lock(&adev->dm.audio_lock);

	drm_connector_list_iter_begin(dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {

		if (connector->connector_type == DRM_MODE_CONNECTOR_WRITEBACK)
			continue;

		aconnector = to_amdgpu_dm_connector(connector);
		if (aconnector->audio_inst != port)
			continue;

		*enabled = true;
		mutex_lock(&connector->eld_mutex);
		ret = drm_eld_size(connector->eld);
		memcpy(buf, connector->eld, min(max_bytes, ret));
		mutex_unlock(&connector->eld_mutex);

		break;
	}
	drm_connector_list_iter_end(&conn_iter);

	mutex_unlock(&adev->dm.audio_lock);

	DRM_DEBUG_KMS("Get ELD : idx=%d ret=%d en=%d\n", port, ret, *enabled);

	return ret;
}

static const struct drm_audio_component_ops amdgpu_dm_audio_component_ops = {
	.get_eld = amdgpu_dm_audio_component_get_eld,
};

static int amdgpu_dm_audio_component_bind(struct device *kdev,
				       struct device *hda_kdev, void *data)
{
	struct drm_device *dev = dev_get_drvdata(kdev);
	struct amdgpu_device *adev = drm_to_adev(dev);
	struct drm_audio_component *acomp = data;

	acomp->ops = &amdgpu_dm_audio_component_ops;
	acomp->dev = kdev;
	adev->dm.audio_component = acomp;

	return 0;
}

static void amdgpu_dm_audio_component_unbind(struct device *kdev,
					  struct device *hda_kdev, void *data)
{
	struct amdgpu_device *adev = drm_to_adev(dev_get_drvdata(kdev));
	struct drm_audio_component *acomp = data;

	acomp->ops = NULL;
	acomp->dev = NULL;
	adev->dm.audio_component = NULL;
}

static const struct component_ops amdgpu_dm_audio_component_bind_ops = {
	.bind	= amdgpu_dm_audio_component_bind,
	.unbind	= amdgpu_dm_audio_component_unbind,
};

static int amdgpu_dm_audio_init(struct amdgpu_device *adev)
{
	int i, ret;

	if (!amdgpu_audio)
		return 0;

	adev->mode_info.audio.enabled = true;

	adev->mode_info.audio.num_pins = adev->dm.dc->res_pool->audio_count;

	for (i = 0; i < adev->mode_info.audio.num_pins; i++) {
		adev->mode_info.audio.pin[i].channels = -1;
		adev->mode_info.audio.pin[i].rate = -1;
		adev->mode_info.audio.pin[i].bits_per_sample = -1;
		adev->mode_info.audio.pin[i].status_bits = 0;
		adev->mode_info.audio.pin[i].category_code = 0;
		adev->mode_info.audio.pin[i].connected = false;
		adev->mode_info.audio.pin[i].id =
			adev->dm.dc->res_pool->audios[i]->inst;
		adev->mode_info.audio.pin[i].offset = 0;
	}

	ret = component_add(adev->dev, &amdgpu_dm_audio_component_bind_ops);
	if (ret < 0)
		return ret;

	adev->dm.audio_registered = true;

	return 0;
}

static void amdgpu_dm_audio_fini(struct amdgpu_device *adev)
{
	if (!amdgpu_audio)
		return;

	if (!adev->mode_info.audio.enabled)
		return;

	if (adev->dm.audio_registered) {
		component_del(adev->dev, &amdgpu_dm_audio_component_bind_ops);
		adev->dm.audio_registered = false;
	}

	/* TODO: Disable audio? */

	adev->mode_info.audio.enabled = false;
}

static  void amdgpu_dm_audio_eld_notify(struct amdgpu_device *adev, int pin)
{
	struct drm_audio_component *acomp = adev->dm.audio_component;

	if (acomp && acomp->audio_ops && acomp->audio_ops->pin_eld_notify) {
		DRM_DEBUG_KMS("Notify ELD: %d\n", pin);

		acomp->audio_ops->pin_eld_notify(acomp->audio_ops->audio_ptr,
						 pin, -1);
	}
}

static int dm_dmub_hw_init(struct amdgpu_device *adev)
{
	const struct dmcub_firmware_header_v1_0 *hdr;
	struct dmub_srv *dmub_srv = adev->dm.dmub_srv;
	struct dmub_srv_fb_info *fb_info = adev->dm.dmub_fb_info;
	const struct firmware *dmub_fw = adev->dm.dmub_fw;
	struct dmcu *dmcu = adev->dm.dc->res_pool->dmcu;
	struct abm *abm = adev->dm.dc->res_pool->abm;
	struct dc_context *ctx = adev->dm.dc->ctx;
	struct dmub_srv_hw_params hw_params;
	enum dmub_status status;
	const unsigned char *fw_inst_const, *fw_bss_data;
	u32 i, fw_inst_const_size, fw_bss_data_size;
	bool has_hw_support;

	if (!dmub_srv)
		/* DMUB isn't supported on the ASIC. */
		return 0;

	if (!fb_info) {
		DRM_ERROR("No framebuffer info for DMUB service.\n");
		return -EINVAL;
	}

	if (!dmub_fw) {
		/* Firmware required for DMUB support. */
		DRM_ERROR("No firmware provided for DMUB.\n");
		return -EINVAL;
	}

	/* initialize register offsets for ASICs with runtime initialization available */
	if (dmub_srv->hw_funcs.init_reg_offsets)
		dmub_srv->hw_funcs.init_reg_offsets(dmub_srv, ctx);

	status = dmub_srv_has_hw_support(dmub_srv, &has_hw_support);
	if (status != DMUB_STATUS_OK) {
		DRM_ERROR("Error checking HW support for DMUB: %d\n", status);
		return -EINVAL;
	}

	if (!has_hw_support) {
		DRM_INFO("DMUB unsupported on ASIC\n");
		return 0;
	}

	/* Reset DMCUB if it was previously running - before we overwrite its memory. */
	status = dmub_srv_hw_reset(dmub_srv);
	if (status != DMUB_STATUS_OK)
		DRM_WARN("Error resetting DMUB HW: %d\n", status);

	hdr = (const struct dmcub_firmware_header_v1_0 *)dmub_fw->data;

	fw_inst_const = dmub_fw->data +
			le32_to_cpu(hdr->header.ucode_array_offset_bytes) +
			PSP_HEADER_BYTES;

	fw_bss_data = dmub_fw->data +
		      le32_to_cpu(hdr->header.ucode_array_offset_bytes) +
		      le32_to_cpu(hdr->inst_const_bytes);

	/* Copy firmware and bios info into FB memory. */
	fw_inst_const_size = le32_to_cpu(hdr->inst_const_bytes) -
			     PSP_HEADER_BYTES - PSP_FOOTER_BYTES;

	fw_bss_data_size = le32_to_cpu(hdr->bss_data_bytes);

	/* if adev->firmware.load_type == AMDGPU_FW_LOAD_PSP,
	 * amdgpu_ucode_init_single_fw will load dmub firmware
	 * fw_inst_const part to cw0; otherwise, the firmware back door load
	 * will be done by dm_dmub_hw_init
	 */
	if (adev->firmware.load_type != AMDGPU_FW_LOAD_PSP) {
		memcpy(fb_info->fb[DMUB_WINDOW_0_INST_CONST].cpu_addr, fw_inst_const,
				fw_inst_const_size);
	}

	if (fw_bss_data_size)
		memcpy(fb_info->fb[DMUB_WINDOW_2_BSS_DATA].cpu_addr,
		       fw_bss_data, fw_bss_data_size);

	/* Copy firmware bios info into FB memory. */
	memcpy(fb_info->fb[DMUB_WINDOW_3_VBIOS].cpu_addr, adev->bios,
	       adev->bios_size);

	/* Reset regions that need to be reset. */
	memset(fb_info->fb[DMUB_WINDOW_4_MAILBOX].cpu_addr, 0,
	fb_info->fb[DMUB_WINDOW_4_MAILBOX].size);

	memset(fb_info->fb[DMUB_WINDOW_5_TRACEBUFF].cpu_addr, 0,
	       fb_info->fb[DMUB_WINDOW_5_TRACEBUFF].size);

	memset(fb_info->fb[DMUB_WINDOW_6_FW_STATE].cpu_addr, 0,
	       fb_info->fb[DMUB_WINDOW_6_FW_STATE].size);

	memset(fb_info->fb[DMUB_WINDOW_SHARED_STATE].cpu_addr, 0,
	       fb_info->fb[DMUB_WINDOW_SHARED_STATE].size);

	/* Initialize hardware. */
	memset(&hw_params, 0, sizeof(hw_params));
	hw_params.fb_base = adev->gmc.fb_start;
	hw_params.fb_offset = adev->vm_manager.vram_base_offset;

	/* backdoor load firmware and trigger dmub running */
	if (adev->firmware.load_type != AMDGPU_FW_LOAD_PSP)
		hw_params.load_inst_const = true;

	if (dmcu)
		hw_params.psp_version = dmcu->psp_version;

	for (i = 0; i < fb_info->num_fb; ++i)
		hw_params.fb[i] = &fb_info->fb[i];

	switch (amdgpu_ip_version(adev, DCE_HWIP, 0)) {
	case IP_VERSION(3, 1, 3):
	case IP_VERSION(3, 1, 4):
	case IP_VERSION(3, 5, 0):
	case IP_VERSION(3, 5, 1):
	case IP_VERSION(4, 0, 1):
		hw_params.dpia_supported = true;
		hw_params.disable_dpia = adev->dm.dc->debug.dpia_debug.bits.disable_dpia;
		break;
	default:
		break;
	}

	switch (amdgpu_ip_version(adev, DCE_HWIP, 0)) {
	case IP_VERSION(3, 5, 0):
	case IP_VERSION(3, 5, 1):
		hw_params.ips_sequential_ono = adev->external_rev_id > 0x10;
		break;
	default:
		break;
	}

	status = dmub_srv_hw_init(dmub_srv, &hw_params);
	if (status != DMUB_STATUS_OK) {
		DRM_ERROR("Error initializing DMUB HW: %d\n", status);
		return -EINVAL;
	}

	/* Wait for firmware load to finish. */
	status = dmub_srv_wait_for_auto_load(dmub_srv, 100000);
	if (status != DMUB_STATUS_OK)
		DRM_WARN("Wait for DMUB auto-load failed: %d\n", status);

	/* Init DMCU and ABM if available. */
	if (dmcu && abm) {
		dmcu->funcs->dmcu_init(dmcu);
		abm->dmcu_is_running = dmcu->funcs->is_dmcu_initialized(dmcu);
	}

	if (!adev->dm.dc->ctx->dmub_srv)
		adev->dm.dc->ctx->dmub_srv = dc_dmub_srv_create(adev->dm.dc, dmub_srv);
	if (!adev->dm.dc->ctx->dmub_srv) {
		DRM_ERROR("Couldn't allocate DC DMUB server!\n");
		return -ENOMEM;
	}

	DRM_INFO("DMUB hardware initialized: version=0x%08X\n",
		 adev->dm.dmcub_fw_version);

	return 0;
}

static void dm_dmub_hw_resume(struct amdgpu_device *adev)
{
	struct dmub_srv *dmub_srv = adev->dm.dmub_srv;
	enum dmub_status status;
	bool init;
	int r;

	if (!dmub_srv) {
		/* DMUB isn't supported on the ASIC. */
		return;
	}

	status = dmub_srv_is_hw_init(dmub_srv, &init);
	if (status != DMUB_STATUS_OK)
		DRM_WARN("DMUB hardware init check failed: %d\n", status);

	if (status == DMUB_STATUS_OK && init) {
		/* Wait for firmware load to finish. */
		status = dmub_srv_wait_for_auto_load(dmub_srv, 100000);
		if (status != DMUB_STATUS_OK)
			DRM_WARN("Wait for DMUB auto-load failed: %d\n", status);
	} else {
		/* Perform the full hardware initialization. */
		r = dm_dmub_hw_init(adev);
		if (r)
			DRM_ERROR("DMUB interface failed to initialize: status=%d\n", r);
	}
}

static void mmhub_read_system_context(struct amdgpu_device *adev, struct dc_phy_addr_space_config *pa_config)
{
	u64 pt_base;
	u32 logical_addr_low;
	u32 logical_addr_high;
	u32 agp_base, agp_bot, agp_top;
	PHYSICAL_ADDRESS_LOC page_table_start, page_table_end, page_table_base;

	memset(pa_config, 0, sizeof(*pa_config));

	agp_base = 0;
	agp_bot = adev->gmc.agp_start >> 24;
	agp_top = adev->gmc.agp_end >> 24;

	/* AGP aperture is disabled */
	if (agp_bot > agp_top) {
		logical_addr_low = adev->gmc.fb_start >> 18;
		if (adev->apu_flags & (AMD_APU_IS_RAVEN2 |
				       AMD_APU_IS_RENOIR |
				       AMD_APU_IS_GREEN_SARDINE))
			/*
			 * Raven2 has a HW issue that it is unable to use the vram which
			 * is out of MC_VM_SYSTEM_APERTURE_HIGH_ADDR. So here is the
			 * workaround that increase system aperture high address (add 1)
			 * to get rid of the VM fault and hardware hang.
			 */
			logical_addr_high = (adev->gmc.fb_end >> 18) + 0x1;
		else
			logical_addr_high = adev->gmc.fb_end >> 18;
	} else {
		logical_addr_low = min(adev->gmc.fb_start, adev->gmc.agp_start) >> 18;
		if (adev->apu_flags & (AMD_APU_IS_RAVEN2 |
				       AMD_APU_IS_RENOIR |
				       AMD_APU_IS_GREEN_SARDINE))
			/*
			 * Raven2 has a HW issue that it is unable to use the vram which
			 * is out of MC_VM_SYSTEM_APERTURE_HIGH_ADDR. So here is the
			 * workaround that increase system aperture high address (add 1)
			 * to get rid of the VM fault and hardware hang.
			 */
			logical_addr_high = max((adev->gmc.fb_end >> 18) + 0x1, adev->gmc.agp_end >> 18);
		else
			logical_addr_high = max(adev->gmc.fb_end, adev->gmc.agp_end) >> 18;
	}

	pt_base = amdgpu_gmc_pd_addr(adev->gart.bo);

	page_table_start.high_part = upper_32_bits(adev->gmc.gart_start >>
						   AMDGPU_GPU_PAGE_SHIFT);
	page_table_start.low_part = lower_32_bits(adev->gmc.gart_start >>
						  AMDGPU_GPU_PAGE_SHIFT);
	page_table_end.high_part = upper_32_bits(adev->gmc.gart_end >>
						 AMDGPU_GPU_PAGE_SHIFT);
	page_table_end.low_part = lower_32_bits(adev->gmc.gart_end >>
						AMDGPU_GPU_PAGE_SHIFT);
	page_table_base.high_part = upper_32_bits(pt_base);
	page_table_base.low_part = lower_32_bits(pt_base);

	pa_config->system_aperture.start_addr = (uint64_t)logical_addr_low << 18;
	pa_config->system_aperture.end_addr = (uint64_t)logical_addr_high << 18;

	pa_config->system_aperture.agp_base = (uint64_t)agp_base << 24;
	pa_config->system_aperture.agp_bot = (uint64_t)agp_bot << 24;
	pa_config->system_aperture.agp_top = (uint64_t)agp_top << 24;

	pa_config->system_aperture.fb_base = adev->gmc.fb_start;
	pa_config->system_aperture.fb_offset = adev->vm_manager.vram_base_offset;
	pa_config->system_aperture.fb_top = adev->gmc.fb_end;

	pa_config->gart_config.page_table_start_addr = page_table_start.quad_part << 12;
	pa_config->gart_config.page_table_end_addr = page_table_end.quad_part << 12;
	pa_config->gart_config.page_table_base_addr = page_table_base.quad_part;

	pa_config->is_hvm_enabled = adev->mode_info.gpu_vm_support;

}

static void force_connector_state(
	struct amdgpu_dm_connector *aconnector,
	enum drm_connector_force force_state)
{
	struct drm_connector *connector = &aconnector->base;

	mutex_lock(&connector->dev->mode_config.mutex);
	aconnector->base.force = force_state;
	mutex_unlock(&connector->dev->mode_config.mutex);

	mutex_lock(&aconnector->hpd_lock);
	drm_kms_helper_connector_hotplug_event(connector);
	mutex_unlock(&aconnector->hpd_lock);
}

static void dm_handle_hpd_rx_offload_work(struct work_struct *work)
{
	struct hpd_rx_irq_offload_work *offload_work;
	struct amdgpu_dm_connector *aconnector;
	struct dc_link *dc_link;
	struct amdgpu_device *adev;
	enum dc_connection_type new_connection_type = dc_connection_none;
	unsigned long flags;
	union test_response test_response;

	memset(&test_response, 0, sizeof(test_response));

	offload_work = container_of(work, struct hpd_rx_irq_offload_work, work);
	aconnector = offload_work->offload_wq->aconnector;

	if (!aconnector) {
		DRM_ERROR("Can't retrieve aconnector in hpd_rx_irq_offload_work");
		goto skip;
	}

	adev = drm_to_adev(aconnector->base.dev);
	dc_link = aconnector->dc_link;

	mutex_lock(&aconnector->hpd_lock);
	if (!dc_link_detect_connection_type(dc_link, &new_connection_type))
		DRM_ERROR("KMS: Failed to detect connector\n");
	mutex_unlock(&aconnector->hpd_lock);

	if (new_connection_type == dc_connection_none)
		goto skip;

	if (amdgpu_in_reset(adev))
		goto skip;

	if (offload_work->data.bytes.device_service_irq.bits.UP_REQ_MSG_RDY ||
		offload_work->data.bytes.device_service_irq.bits.DOWN_REP_MSG_RDY) {
		dm_handle_mst_sideband_msg_ready_event(&aconnector->mst_mgr, DOWN_OR_UP_MSG_RDY_EVENT);
		spin_lock_irqsave(&offload_work->offload_wq->offload_lock, flags);
		offload_work->offload_wq->is_handling_mst_msg_rdy_event = false;
		spin_unlock_irqrestore(&offload_work->offload_wq->offload_lock, flags);
		goto skip;
	}

	mutex_lock(&adev->dm.dc_lock);
	if (offload_work->data.bytes.device_service_irq.bits.AUTOMATED_TEST) {
		dc_link_dp_handle_automated_test(dc_link);

		if (aconnector->timing_changed) {
			/* force connector disconnect and reconnect */
			force_connector_state(aconnector, DRM_FORCE_OFF);
			drm_msleep(100);
			force_connector_state(aconnector, DRM_FORCE_UNSPECIFIED);
		}

		test_response.bits.ACK = 1;

		core_link_write_dpcd(
		dc_link,
		DP_TEST_RESPONSE,
		&test_response.raw,
		sizeof(test_response));
	} else if ((dc_link->connector_signal != SIGNAL_TYPE_EDP) &&
			dc_link_check_link_loss_status(dc_link, &offload_work->data) &&
			dc_link_dp_allow_hpd_rx_irq(dc_link)) {
		/* offload_work->data is from handle_hpd_rx_irq->
		 * schedule_hpd_rx_offload_work.this is defer handle
		 * for hpd short pulse. upon here, link status may be
		 * changed, need get latest link status from dpcd
		 * registers. if link status is good, skip run link
		 * training again.
		 */
		union hpd_irq_data irq_data;

		memset(&irq_data, 0, sizeof(irq_data));

		/* before dc_link_dp_handle_link_loss, allow new link lost handle
		 * request be added to work queue if link lost at end of dc_link_
		 * dp_handle_link_loss
		 */
		spin_lock_irqsave(&offload_work->offload_wq->offload_lock, flags);
		offload_work->offload_wq->is_handling_link_loss = false;
		spin_unlock_irqrestore(&offload_work->offload_wq->offload_lock, flags);

		if ((dc_link_dp_read_hpd_rx_irq_data(dc_link, &irq_data) == DC_OK) &&
			dc_link_check_link_loss_status(dc_link, &irq_data))
			dc_link_dp_handle_link_loss(dc_link);
	}
	mutex_unlock(&adev->dm.dc_lock);

skip:
	kfree(offload_work);

}

static struct hpd_rx_irq_offload_work_queue *hpd_rx_irq_create_workqueue(struct dc *dc)
{
	int max_caps = dc->caps.max_links;
	int i = 0;
	struct hpd_rx_irq_offload_work_queue *hpd_rx_offload_wq = NULL;

	hpd_rx_offload_wq = kcalloc(max_caps, sizeof(*hpd_rx_offload_wq), GFP_KERNEL);

	if (!hpd_rx_offload_wq)
		return NULL;


	for (i = 0; i < max_caps; i++) {
		hpd_rx_offload_wq[i].wq =
				    create_singlethread_workqueue("amdgpu_dm_hpd_rx_offload_wq");

		if (hpd_rx_offload_wq[i].wq == NULL) {
			DRM_ERROR("create amdgpu_dm_hpd_rx_offload_wq fail!");
			goto out_err;
		}

		mtx_init(&hpd_rx_offload_wq[i].offload_lock, IPL_TTY);
	}

	return hpd_rx_offload_wq;

out_err:
	for (i = 0; i < max_caps; i++) {
		if (hpd_rx_offload_wq[i].wq)
			destroy_workqueue(hpd_rx_offload_wq[i].wq);
	}
	kfree(hpd_rx_offload_wq);
	return NULL;
}

struct amdgpu_stutter_quirk {
	u16 chip_vendor;
	u16 chip_device;
	u16 subsys_vendor;
	u16 subsys_device;
	u8 revision;
};

static const struct amdgpu_stutter_quirk amdgpu_stutter_quirk_list[] = {
	/* https://bugzilla.kernel.org/show_bug.cgi?id=214417 */
	{ 0x1002, 0x15dd, 0x1002, 0x15dd, 0xc8 },
	{ 0, 0, 0, 0, 0 },
};

static bool dm_should_disable_stutter(struct pci_dev *pdev)
{
	const struct amdgpu_stutter_quirk *p = amdgpu_stutter_quirk_list;

	while (p && p->chip_device != 0) {
		if (pdev->vendor == p->chip_vendor &&
		    pdev->device == p->chip_device &&
		    pdev->subsystem_vendor == p->subsys_vendor &&
		    pdev->subsystem_device == p->subsys_device &&
		    pdev->revision == p->revision) {
			return true;
		}
		++p;
	}
	return false;
}

struct amdgpu_dm_quirks {
	bool aux_hpd_discon;
	bool support_edp0_on_dp1;
};

static struct amdgpu_dm_quirks quirk_entries = {
	.aux_hpd_discon = false,
	.support_edp0_on_dp1 = false
};

static int edp0_on_dp1_callback(const struct dmi_system_id *id)
{
	quirk_entries.support_edp0_on_dp1 = true;
	return 0;
}

static int aux_hpd_discon_callback(const struct dmi_system_id *id)
{
	quirk_entries.aux_hpd_discon = true;
	return 0;
}

static const struct dmi_system_id dmi_quirk_table[] = {
	{
		.callback = aux_hpd_discon_callback,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Precision 3660"),
		},
	},
	{
		.callback = aux_hpd_discon_callback,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Precision 3260"),
		},
	},
	{
		.callback = aux_hpd_discon_callback,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Precision 3460"),
		},
	},
	{
		.callback = aux_hpd_discon_callback,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "OptiPlex Tower Plus 7010"),
		},
	},
	{
		.callback = aux_hpd_discon_callback,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "OptiPlex Tower 7010"),
		},
	},
	{
		.callback = aux_hpd_discon_callback,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "OptiPlex SFF Plus 7010"),
		},
	},
	{
		.callback = aux_hpd_discon_callback,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "OptiPlex SFF 7010"),
		},
	},
	{
		.callback = aux_hpd_discon_callback,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "OptiPlex Micro Plus 7010"),
		},
	},
	{
		.callback = aux_hpd_discon_callback,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "OptiPlex Micro 7010"),
		},
	},
	{
		.callback = edp0_on_dp1_callback,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
			DMI_MATCH(DMI_PRODUCT_NAME, "HP Elite mt645 G8 Mobile Thin Client"),
		},
	},
	{
		.callback = edp0_on_dp1_callback,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
			DMI_MATCH(DMI_PRODUCT_NAME, "HP EliteBook 645 14 inch G11 Notebook PC"),
		},
	},
	{
		.callback = edp0_on_dp1_callback,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
			DMI_MATCH(DMI_PRODUCT_NAME, "HP EliteBook 665 16 inch G11 Notebook PC"),
		},
	},
	{
		.callback = edp0_on_dp1_callback,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
			DMI_MATCH(DMI_PRODUCT_NAME, "HP ProBook 445 14 inch G11 Notebook PC"),
		},
	},
	{
		.callback = edp0_on_dp1_callback,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
			DMI_MATCH(DMI_PRODUCT_NAME, "HP ProBook 465 16 inch G11 Notebook PC"),
		},
	},
	{}
	/* TODO: refactor this from a fixed table to a dynamic option */
};

static void retrieve_dmi_info(struct amdgpu_display_manager *dm, struct dc_init_data *init_data)
{
	int dmi_id;
	struct drm_device *dev = dm->ddev;

	dm->aux_hpd_discon_quirk = false;
	init_data->flags.support_edp0_on_dp1 = false;

	dmi_id = dmi_check_system(dmi_quirk_table);

	if (!dmi_id)
		return;

	if (quirk_entries.aux_hpd_discon) {
		dm->aux_hpd_discon_quirk = true;
		drm_info(dev, "aux_hpd_discon_quirk attached\n");
	}
	if (quirk_entries.support_edp0_on_dp1) {
		init_data->flags.support_edp0_on_dp1 = true;
		drm_info(dev, "support_edp0_on_dp1 attached\n");
	}
}

void*
dm_allocate_gpu_mem(
		struct amdgpu_device *adev,
		enum dc_gpu_mem_alloc_type type,
		size_t size,
		long long *addr)
{
	struct dal_allocation *da;
	u32 domain = (type == DC_MEM_ALLOC_TYPE_GART) ?
		AMDGPU_GEM_DOMAIN_GTT : AMDGPU_GEM_DOMAIN_VRAM;
	int ret;

	da = kzalloc(sizeof(struct dal_allocation), GFP_KERNEL);
	if (!da)
		return NULL;

	ret = amdgpu_bo_create_kernel(adev, size, PAGE_SIZE,
				      domain, &da->bo,
				      &da->gpu_addr, &da->cpu_ptr);

	*addr = da->gpu_addr;

	if (ret) {
		kfree(da);
		return NULL;
	}

	/* add da to list in dm */
	list_add(&da->list, &adev->dm.da_list);

	return da->cpu_ptr;
}

void
dm_free_gpu_mem(
		struct amdgpu_device *adev,
		enum dc_gpu_mem_alloc_type type,
		void *pvMem)
{
	struct dal_allocation *da;

	/* walk the da list in DM */
	list_for_each_entry(da, &adev->dm.da_list, list) {
		if (pvMem == da->cpu_ptr) {
			amdgpu_bo_free_kernel(&da->bo, &da->gpu_addr, &da->cpu_ptr);
			list_del(&da->list);
			kfree(da);
			break;
		}
	}

}

static enum dmub_status
dm_dmub_send_vbios_gpint_command(struct amdgpu_device *adev,
				 enum dmub_gpint_command command_code,
				 uint16_t param,
				 uint32_t timeout_us)
{
	union dmub_gpint_data_register reg, test;
	uint32_t i;

	/* Assume that VBIOS DMUB is ready to take commands */

	reg.bits.status = 1;
	reg.bits.command_code = command_code;
	reg.bits.param = param;

	cgs_write_register(adev->dm.cgs_device, 0x34c0 + 0x01f8, reg.all);

	for (i = 0; i < timeout_us; ++i) {
		udelay(1);

		/* Check if our GPINT got acked */
		reg.bits.status = 0;
		test = (union dmub_gpint_data_register)
			cgs_read_register(adev->dm.cgs_device, 0x34c0 + 0x01f8);

		if (test.all == reg.all)
			return DMUB_STATUS_OK;
	}

	return DMUB_STATUS_TIMEOUT;
}

static struct dml2_soc_bb *dm_dmub_get_vbios_bounding_box(struct amdgpu_device *adev)
{
	struct dml2_soc_bb *bb;
	long long addr;
	int i = 0;
	uint16_t chunk;
	enum dmub_gpint_command send_addrs[] = {
		DMUB_GPINT__SET_BB_ADDR_WORD0,
		DMUB_GPINT__SET_BB_ADDR_WORD1,
		DMUB_GPINT__SET_BB_ADDR_WORD2,
		DMUB_GPINT__SET_BB_ADDR_WORD3,
	};
	enum dmub_status ret;

	switch (amdgpu_ip_version(adev, DCE_HWIP, 0)) {
	case IP_VERSION(4, 0, 1):
		break;
	default:
		return NULL;
	}

	bb =  dm_allocate_gpu_mem(adev,
				  DC_MEM_ALLOC_TYPE_GART,
				  sizeof(struct dml2_soc_bb),
				  &addr);
	if (!bb)
		return NULL;

	for (i = 0; i < 4; i++) {
		/* Extract 16-bit chunk */
		chunk = ((uint64_t) addr >> (i * 16)) & 0xFFFF;
		/* Send the chunk */
		ret = dm_dmub_send_vbios_gpint_command(adev, send_addrs[i], chunk, 30000);
		if (ret != DMUB_STATUS_OK)
			goto free_bb;
	}

	/* Now ask DMUB to copy the bb */
	ret = dm_dmub_send_vbios_gpint_command(adev, DMUB_GPINT__BB_COPY, 1, 200000);
	if (ret != DMUB_STATUS_OK)
		goto free_bb;

	return bb;

free_bb:
	dm_free_gpu_mem(adev, DC_MEM_ALLOC_TYPE_GART, (void *) bb);
	return NULL;

}

static enum dmub_ips_disable_type dm_get_default_ips_mode(
	struct amdgpu_device *adev)
{
	enum dmub_ips_disable_type ret = DMUB_IPS_ENABLE;

	switch (amdgpu_ip_version(adev, DCE_HWIP, 0)) {
	case IP_VERSION(3, 5, 0):
	case IP_VERSION(3, 5, 1):
		ret =  DMUB_IPS_RCG_IN_ACTIVE_IPS2_IN_OFF;
		break;
	default:
		/* ASICs older than DCN35 do not have IPSs */
		if (amdgpu_ip_version(adev, DCE_HWIP, 0) < IP_VERSION(3, 5, 0))
			ret = DMUB_IPS_DISABLE_ALL;
		break;
	}

	return ret;
}

static int amdgpu_dm_init(struct amdgpu_device *adev)
{
	struct dc_init_data init_data;
	struct dc_callback_init init_params;
	int r;

	adev->dm.ddev = adev_to_drm(adev);
	adev->dm.adev = adev;

	/* Zero all the fields */
	memset(&init_data, 0, sizeof(init_data));
	memset(&init_params, 0, sizeof(init_params));

	rw_init(&adev->dm.dpia_aux_lock, "dmdpia");
	rw_init(&adev->dm.dc_lock, "dmdc");
	rw_init(&adev->dm.audio_lock, "dmaud");

	if (amdgpu_dm_irq_init(adev)) {
		DRM_ERROR("amdgpu: failed to initialize DM IRQ support.\n");
		goto error;
	}

	init_data.asic_id.chip_family = adev->family;

	init_data.asic_id.pci_revision_id = adev->pdev->revision;
	init_data.asic_id.hw_internal_rev = adev->external_rev_id;
	init_data.asic_id.chip_id = adev->pdev->device;

	init_data.asic_id.vram_width = adev->gmc.vram_width;
	/* TODO: initialize init_data.asic_id.vram_type here!!!! */
	init_data.asic_id.atombios_base_address =
		adev->mode_info.atom_context->bios;

	init_data.driver = adev;

	/* cgs_device was created in dm_sw_init() */
	init_data.cgs_device = adev->dm.cgs_device;

	init_data.dce_environment = DCE_ENV_PRODUCTION_DRV;

	switch (amdgpu_ip_version(adev, DCE_HWIP, 0)) {
	case IP_VERSION(2, 1, 0):
		switch (adev->dm.dmcub_fw_version) {
		case 0: /* development */
		case 0x1: /* linux-firmware.git hash 6d9f399 */
		case 0x01000000: /* linux-firmware.git hash 9a0b0f4 */
			init_data.flags.disable_dmcu = false;
			break;
		default:
			init_data.flags.disable_dmcu = true;
		}
		break;
	case IP_VERSION(2, 0, 3):
		init_data.flags.disable_dmcu = true;
		break;
	default:
		break;
	}

	/* APU support S/G display by default except:
	 * ASICs before Carrizo,
	 * RAVEN1 (Users reported stability issue)
	 */

	if (adev->asic_type < CHIP_CARRIZO) {
		init_data.flags.gpu_vm_support = false;
	} else if (adev->asic_type == CHIP_RAVEN) {
		if (adev->apu_flags & AMD_APU_IS_RAVEN)
			init_data.flags.gpu_vm_support = false;
		else
			init_data.flags.gpu_vm_support = (amdgpu_sg_display != 0);
	} else {
		if (amdgpu_ip_version(adev, DCE_HWIP, 0) == IP_VERSION(2, 0, 3))
			init_data.flags.gpu_vm_support = (amdgpu_sg_display == 1);
		else
			init_data.flags.gpu_vm_support =
				(amdgpu_sg_display != 0) && (adev->flags & AMD_IS_APU);
	}

	adev->mode_info.gpu_vm_support = init_data.flags.gpu_vm_support;

	if (amdgpu_dc_feature_mask & DC_FBC_MASK)
		init_data.flags.fbc_support = true;

	if (amdgpu_dc_feature_mask & DC_MULTI_MON_PP_MCLK_SWITCH_MASK)
		init_data.flags.multi_mon_pp_mclk_switch = true;

	if (amdgpu_dc_feature_mask & DC_DISABLE_FRACTIONAL_PWM_MASK)
		init_data.flags.disable_fractional_pwm = true;

	if (amdgpu_dc_feature_mask & DC_EDP_NO_POWER_SEQUENCING)
		init_data.flags.edp_no_power_sequencing = true;

	if (amdgpu_dc_feature_mask & DC_DISABLE_LTTPR_DP1_4A)
		init_data.flags.allow_lttpr_non_transparent_mode.bits.DP1_4A = true;
	if (amdgpu_dc_feature_mask & DC_DISABLE_LTTPR_DP2_0)
		init_data.flags.allow_lttpr_non_transparent_mode.bits.DP2_0 = true;

	init_data.flags.seamless_boot_edp_requested = false;

	if (amdgpu_device_seamless_boot_supported(adev)) {
		init_data.flags.seamless_boot_edp_requested = true;
		init_data.flags.allow_seamless_boot_optimization = true;
		DRM_INFO("Seamless boot condition check passed\n");
	}

	init_data.flags.enable_mipi_converter_optimization = true;

	init_data.dcn_reg_offsets = adev->reg_offset[DCE_HWIP][0];
	init_data.nbio_reg_offsets = adev->reg_offset[NBIO_HWIP][0];
	init_data.clk_reg_offsets = adev->reg_offset[CLK_HWIP][0];

	if (amdgpu_dc_debug_mask & DC_DISABLE_IPS)
		init_data.flags.disable_ips = DMUB_IPS_DISABLE_ALL;
	else if (amdgpu_dc_debug_mask & DC_DISABLE_IPS_DYNAMIC)
		init_data.flags.disable_ips = DMUB_IPS_DISABLE_DYNAMIC;
	else if (amdgpu_dc_debug_mask & DC_DISABLE_IPS2_DYNAMIC)
		init_data.flags.disable_ips = DMUB_IPS_RCG_IN_ACTIVE_IPS2_IN_OFF;
	else if (amdgpu_dc_debug_mask & DC_FORCE_IPS_ENABLE)
		init_data.flags.disable_ips = DMUB_IPS_ENABLE;
	else
		init_data.flags.disable_ips = dm_get_default_ips_mode(adev);

	init_data.flags.disable_ips_in_vpb = 0;

	/* Enable DWB for tested platforms only */
	if (amdgpu_ip_version(adev, DCE_HWIP, 0) >= IP_VERSION(3, 0, 0))
		init_data.num_virtual_links = 1;

	retrieve_dmi_info(&adev->dm, &init_data);

	if (adev->dm.bb_from_dmub)
		init_data.bb_from_dmub = adev->dm.bb_from_dmub;
	else
		init_data.bb_from_dmub = NULL;

	/* Display Core create. */
	adev->dm.dc = dc_create(&init_data);

	if (adev->dm.dc) {
		DRM_INFO("Display Core v%s initialized on %s\n", DC_VER,
			 dce_version_to_string(adev->dm.dc->ctx->dce_version));
	} else {
		DRM_INFO("Display Core failed to initialize with v%s!\n", DC_VER);
		goto error;
	}

	if (amdgpu_dc_debug_mask & DC_DISABLE_PIPE_SPLIT) {
		adev->dm.dc->debug.force_single_disp_pipe_split = false;
		adev->dm.dc->debug.pipe_split_policy = MPC_SPLIT_AVOID;
	}

	if (adev->asic_type != CHIP_CARRIZO && adev->asic_type != CHIP_STONEY)
		adev->dm.dc->debug.disable_stutter = amdgpu_pp_feature_mask & PP_STUTTER_MODE ? false : true;
	if (dm_should_disable_stutter(adev->pdev))
		adev->dm.dc->debug.disable_stutter = true;

	if (amdgpu_dc_debug_mask & DC_DISABLE_STUTTER)
		adev->dm.dc->debug.disable_stutter = true;

	if (amdgpu_dc_debug_mask & DC_DISABLE_DSC)
		adev->dm.dc->debug.disable_dsc = true;

	if (amdgpu_dc_debug_mask & DC_DISABLE_CLOCK_GATING)
		adev->dm.dc->debug.disable_clock_gate = true;

	if (amdgpu_dc_debug_mask & DC_FORCE_SUBVP_MCLK_SWITCH)
		adev->dm.dc->debug.force_subvp_mclk_switch = true;

	if (amdgpu_dc_debug_mask & DC_ENABLE_DML2) {
		adev->dm.dc->debug.using_dml2 = true;
		adev->dm.dc->debug.using_dml21 = true;
	}

	adev->dm.dc->debug.visual_confirm = amdgpu_dc_visual_confirm;

	/* TODO: Remove after DP2 receiver gets proper support of Cable ID feature */
	adev->dm.dc->debug.ignore_cable_id = true;

	if (adev->dm.dc->caps.dp_hdmi21_pcon_support)
		DRM_INFO("DP-HDMI FRL PCON supported\n");

	r = dm_dmub_hw_init(adev);
	if (r) {
		DRM_ERROR("DMUB interface failed to initialize: status=%d\n", r);
		goto error;
	}

	dc_hardware_init(adev->dm.dc);

	adev->dm.hpd_rx_offload_wq = hpd_rx_irq_create_workqueue(adev->dm.dc);
	if (!adev->dm.hpd_rx_offload_wq) {
		DRM_ERROR("amdgpu: failed to create hpd rx offload workqueue.\n");
		goto error;
	}

	if ((adev->flags & AMD_IS_APU) && (adev->asic_type >= CHIP_CARRIZO)) {
		struct dc_phy_addr_space_config pa_config;

		mmhub_read_system_context(adev, &pa_config);

		// Call the DC init_memory func
		dc_setup_system_context(adev->dm.dc, &pa_config);
	}

	adev->dm.freesync_module = mod_freesync_create(adev->dm.dc);
	if (!adev->dm.freesync_module) {
		DRM_ERROR(
		"amdgpu: failed to initialize freesync_module.\n");
	} else
		DRM_DEBUG_DRIVER("amdgpu: freesync_module init done %p.\n",
				adev->dm.freesync_module);

	amdgpu_dm_init_color_mod();

	if (adev->dm.dc->caps.max_links > 0) {
		adev->dm.vblank_control_workqueue =
			create_singlethread_workqueue("dm_vblank_control_workqueue");
		if (!adev->dm.vblank_control_workqueue)
			DRM_ERROR("amdgpu: failed to initialize vblank_workqueue.\n");
	}

	if (adev->dm.dc->caps.ips_support &&
	    adev->dm.dc->config.disable_ips != DMUB_IPS_DISABLE_ALL)
		adev->dm.idle_workqueue = idle_create_workqueue(adev);

	if (adev->dm.dc->caps.max_links > 0 && adev->family >= AMDGPU_FAMILY_RV) {
		adev->dm.hdcp_workqueue = hdcp_create_workqueue(adev, &init_params.cp_psp, adev->dm.dc);

		if (!adev->dm.hdcp_workqueue)
			DRM_ERROR("amdgpu: failed to initialize hdcp_workqueue.\n");
		else
			DRM_DEBUG_DRIVER("amdgpu: hdcp_workqueue init done %p.\n", adev->dm.hdcp_workqueue);

		dc_init_callbacks(adev->dm.dc, &init_params);
	}
	if (dc_is_dmub_outbox_supported(adev->dm.dc)) {
		init_completion(&adev->dm.dmub_aux_transfer_done);
		adev->dm.dmub_notify = kzalloc(sizeof(struct dmub_notification), GFP_KERNEL);
		if (!adev->dm.dmub_notify) {
			DRM_INFO("amdgpu: fail to allocate adev->dm.dmub_notify");
			goto error;
		}

		adev->dm.delayed_hpd_wq = create_singlethread_workqueue("amdgpu_dm_hpd_wq");
		if (!adev->dm.delayed_hpd_wq) {
			DRM_ERROR("amdgpu: failed to create hpd offload workqueue.\n");
			goto error;
		}

		amdgpu_dm_outbox_init(adev);
		if (!register_dmub_notify_callback(adev, DMUB_NOTIFICATION_AUX_REPLY,
			dmub_aux_setconfig_callback, false)) {
			DRM_ERROR("amdgpu: fail to register dmub aux callback");
			goto error;
		}
		/* Enable outbox notification only after IRQ handlers are registered and DMUB is alive.
		 * It is expected that DMUB will resend any pending notifications at this point. Note
		 * that hpd and hpd_irq handler registration are deferred to register_hpd_handlers() to
		 * align legacy interface initialization sequence. Connection status will be proactivly
		 * detected once in the amdgpu_dm_initialize_drm_device.
		 */
		dc_enable_dmub_outbox(adev->dm.dc);

		/* DPIA trace goes to dmesg logs only if outbox is enabled */
		if (amdgpu_dc_debug_mask & DC_ENABLE_DPIA_TRACE)
			dc_dmub_srv_enable_dpia_trace(adev->dm.dc);
	}

	if (amdgpu_dm_initialize_drm_device(adev)) {
		DRM_ERROR(
		"amdgpu: failed to initialize sw for display support.\n");
		goto error;
	}

	/* create fake encoders for MST */
	dm_dp_create_fake_mst_encoders(adev);

	/* TODO: Add_display_info? */

	/* TODO use dynamic cursor width */
	adev_to_drm(adev)->mode_config.cursor_width = adev->dm.dc->caps.max_cursor_size;
	adev_to_drm(adev)->mode_config.cursor_height = adev->dm.dc->caps.max_cursor_size;

	if (drm_vblank_init(adev_to_drm(adev), adev->dm.display_indexes_num)) {
		DRM_ERROR(
		"amdgpu: failed to initialize sw for display support.\n");
		goto error;
	}

#if defined(CONFIG_DRM_AMD_SECURE_DISPLAY)
	adev->dm.secure_display_ctxs = amdgpu_dm_crtc_secure_display_create_contexts(adev);
	if (!adev->dm.secure_display_ctxs)
		DRM_ERROR("amdgpu: failed to initialize secure display contexts.\n");
#endif

	DRM_DEBUG_DRIVER("KMS initialized.\n");

	return 0;
error:
	amdgpu_dm_fini(adev);

	return -EINVAL;
}

static int amdgpu_dm_early_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	amdgpu_dm_audio_fini(adev);

	return 0;
}

static void amdgpu_dm_fini(struct amdgpu_device *adev)
{
	int i;

	if (adev->dm.vblank_control_workqueue) {
		destroy_workqueue(adev->dm.vblank_control_workqueue);
		adev->dm.vblank_control_workqueue = NULL;
	}

	if (adev->dm.idle_workqueue) {
		if (adev->dm.idle_workqueue->running) {
			adev->dm.idle_workqueue->enable = false;
			flush_work(&adev->dm.idle_workqueue->work);
		}

		kfree(adev->dm.idle_workqueue);
		adev->dm.idle_workqueue = NULL;
	}

	amdgpu_dm_destroy_drm_device(&adev->dm);

#if defined(CONFIG_DRM_AMD_SECURE_DISPLAY)
	if (adev->dm.secure_display_ctxs) {
		for (i = 0; i < adev->mode_info.num_crtc; i++) {
			if (adev->dm.secure_display_ctxs[i].crtc) {
				flush_work(&adev->dm.secure_display_ctxs[i].notify_ta_work);
				flush_work(&adev->dm.secure_display_ctxs[i].forward_roi_work);
			}
		}
		kfree(adev->dm.secure_display_ctxs);
		adev->dm.secure_display_ctxs = NULL;
	}
#endif
	if (adev->dm.hdcp_workqueue) {
#ifdef notyet
		hdcp_destroy(&adev->dev->kobj, adev->dm.hdcp_workqueue);
#else
		hdcp_destroy(NULL, adev->dm.hdcp_workqueue);
#endif
		adev->dm.hdcp_workqueue = NULL;
	}

	if (adev->dm.dc) {
		dc_deinit_callbacks(adev->dm.dc);
		dc_dmub_srv_destroy(&adev->dm.dc->ctx->dmub_srv);
		if (dc_enable_dmub_notifications(adev->dm.dc)) {
			kfree(adev->dm.dmub_notify);
			adev->dm.dmub_notify = NULL;
			destroy_workqueue(adev->dm.delayed_hpd_wq);
			adev->dm.delayed_hpd_wq = NULL;
		}
	}

	if (adev->dm.dmub_bo)
		amdgpu_bo_free_kernel(&adev->dm.dmub_bo,
				      &adev->dm.dmub_bo_gpu_addr,
				      &adev->dm.dmub_bo_cpu_addr);

	if (adev->dm.hpd_rx_offload_wq && adev->dm.dc) {
		for (i = 0; i < adev->dm.dc->caps.max_links; i++) {
			if (adev->dm.hpd_rx_offload_wq[i].wq) {
				destroy_workqueue(adev->dm.hpd_rx_offload_wq[i].wq);
				adev->dm.hpd_rx_offload_wq[i].wq = NULL;
			}
		}

		kfree(adev->dm.hpd_rx_offload_wq);
		adev->dm.hpd_rx_offload_wq = NULL;
	}

	/* DC Destroy TODO: Replace destroy DAL */
	if (adev->dm.dc)
		dc_destroy(&adev->dm.dc);
	/*
	 * TODO: pageflip, vlank interrupt
	 *
	 * amdgpu_dm_irq_fini(adev);
	 */

	if (adev->dm.cgs_device) {
		amdgpu_cgs_destroy_device(adev->dm.cgs_device);
		adev->dm.cgs_device = NULL;
	}
	if (adev->dm.freesync_module) {
		mod_freesync_destroy(adev->dm.freesync_module);
		adev->dm.freesync_module = NULL;
	}

	mutex_destroy(&adev->dm.audio_lock);
	mutex_destroy(&adev->dm.dc_lock);
	mutex_destroy(&adev->dm.dpia_aux_lock);
}

static int load_dmcu_fw(struct amdgpu_device *adev)
{
	const char *fw_name_dmcu = NULL;
	int r;
	const struct dmcu_firmware_header_v1_0 *hdr;

	switch (adev->asic_type) {
#if defined(CONFIG_DRM_AMD_DC_SI)
	case CHIP_TAHITI:
	case CHIP_PITCAIRN:
	case CHIP_VERDE:
	case CHIP_OLAND:
#endif
	case CHIP_BONAIRE:
	case CHIP_HAWAII:
	case CHIP_KAVERI:
	case CHIP_KABINI:
	case CHIP_MULLINS:
	case CHIP_TONGA:
	case CHIP_FIJI:
	case CHIP_CARRIZO:
	case CHIP_STONEY:
	case CHIP_POLARIS11:
	case CHIP_POLARIS10:
	case CHIP_POLARIS12:
	case CHIP_VEGAM:
	case CHIP_VEGA10:
	case CHIP_VEGA12:
	case CHIP_VEGA20:
		return 0;
	case CHIP_NAVI12:
		fw_name_dmcu = FIRMWARE_NAVI12_DMCU;
		break;
	case CHIP_RAVEN:
		if (ASICREV_IS_PICASSO(adev->external_rev_id))
			fw_name_dmcu = FIRMWARE_RAVEN_DMCU;
		else if (ASICREV_IS_RAVEN2(adev->external_rev_id))
			fw_name_dmcu = FIRMWARE_RAVEN_DMCU;
		else
			return 0;
		break;
	default:
		switch (amdgpu_ip_version(adev, DCE_HWIP, 0)) {
		case IP_VERSION(2, 0, 2):
		case IP_VERSION(2, 0, 3):
		case IP_VERSION(2, 0, 0):
		case IP_VERSION(2, 1, 0):
		case IP_VERSION(3, 0, 0):
		case IP_VERSION(3, 0, 2):
		case IP_VERSION(3, 0, 3):
		case IP_VERSION(3, 0, 1):
		case IP_VERSION(3, 1, 2):
		case IP_VERSION(3, 1, 3):
		case IP_VERSION(3, 1, 4):
		case IP_VERSION(3, 1, 5):
		case IP_VERSION(3, 1, 6):
		case IP_VERSION(3, 2, 0):
		case IP_VERSION(3, 2, 1):
		case IP_VERSION(3, 5, 0):
		case IP_VERSION(3, 5, 1):
		case IP_VERSION(4, 0, 1):
			return 0;
		default:
			break;
		}
		DRM_ERROR("Unsupported ASIC type: 0x%X\n", adev->asic_type);
		return -EINVAL;
	}

	if (adev->firmware.load_type != AMDGPU_FW_LOAD_PSP) {
		DRM_DEBUG_KMS("dm: DMCU firmware not supported on direct or SMU loading\n");
		return 0;
	}

	r = amdgpu_ucode_request(adev, &adev->dm.fw_dmcu, "%s", fw_name_dmcu);
	if (r == -ENODEV) {
		/* DMCU firmware is not necessary, so don't raise a fuss if it's missing */
		DRM_DEBUG_KMS("dm: DMCU firmware not found\n");
		adev->dm.fw_dmcu = NULL;
		return 0;
	}
	if (r) {
		dev_err(adev->dev, "amdgpu_dm: Can't validate firmware \"%s\"\n",
			fw_name_dmcu);
		amdgpu_ucode_release(&adev->dm.fw_dmcu);
		return r;
	}

	hdr = (const struct dmcu_firmware_header_v1_0 *)adev->dm.fw_dmcu->data;
	adev->firmware.ucode[AMDGPU_UCODE_ID_DMCU_ERAM].ucode_id = AMDGPU_UCODE_ID_DMCU_ERAM;
	adev->firmware.ucode[AMDGPU_UCODE_ID_DMCU_ERAM].fw = adev->dm.fw_dmcu;
	adev->firmware.fw_size +=
		ALIGN(le32_to_cpu(hdr->header.ucode_size_bytes) - le32_to_cpu(hdr->intv_size_bytes), PAGE_SIZE);

	adev->firmware.ucode[AMDGPU_UCODE_ID_DMCU_INTV].ucode_id = AMDGPU_UCODE_ID_DMCU_INTV;
	adev->firmware.ucode[AMDGPU_UCODE_ID_DMCU_INTV].fw = adev->dm.fw_dmcu;
	adev->firmware.fw_size +=
		ALIGN(le32_to_cpu(hdr->intv_size_bytes), PAGE_SIZE);

	adev->dm.dmcu_fw_version = le32_to_cpu(hdr->header.ucode_version);

	DRM_DEBUG_KMS("PSP loading DMCU firmware\n");

	return 0;
}

static uint32_t amdgpu_dm_dmub_reg_read(void *ctx, uint32_t address)
{
	struct amdgpu_device *adev = ctx;

	return dm_read_reg(adev->dm.dc->ctx, address);
}

static void amdgpu_dm_dmub_reg_write(void *ctx, uint32_t address,
				     uint32_t value)
{
	struct amdgpu_device *adev = ctx;

	return dm_write_reg(adev->dm.dc->ctx, address, value);
}

static int dm_dmub_sw_init(struct amdgpu_device *adev)
{
	struct dmub_srv_create_params create_params;
	struct dmub_srv_region_params region_params;
	struct dmub_srv_region_info region_info;
	struct dmub_srv_memory_params memory_params;
	struct dmub_srv_fb_info *fb_info;
	struct dmub_srv *dmub_srv;
	const struct dmcub_firmware_header_v1_0 *hdr;
	enum dmub_asic dmub_asic;
	enum dmub_status status;
	static enum dmub_window_memory_type window_memory_type[DMUB_WINDOW_TOTAL] = {
		DMUB_WINDOW_MEMORY_TYPE_FB,		//DMUB_WINDOW_0_INST_CONST
		DMUB_WINDOW_MEMORY_TYPE_FB,		//DMUB_WINDOW_1_STACK
		DMUB_WINDOW_MEMORY_TYPE_FB,		//DMUB_WINDOW_2_BSS_DATA
		DMUB_WINDOW_MEMORY_TYPE_FB,		//DMUB_WINDOW_3_VBIOS
		DMUB_WINDOW_MEMORY_TYPE_FB,		//DMUB_WINDOW_4_MAILBOX
		DMUB_WINDOW_MEMORY_TYPE_FB,		//DMUB_WINDOW_5_TRACEBUFF
		DMUB_WINDOW_MEMORY_TYPE_FB,		//DMUB_WINDOW_6_FW_STATE
		DMUB_WINDOW_MEMORY_TYPE_FB,		//DMUB_WINDOW_7_SCRATCH_MEM
		DMUB_WINDOW_MEMORY_TYPE_FB,		//DMUB_WINDOW_SHARED_STATE
	};
	int r;

	switch (amdgpu_ip_version(adev, DCE_HWIP, 0)) {
	case IP_VERSION(2, 1, 0):
		dmub_asic = DMUB_ASIC_DCN21;
		break;
	case IP_VERSION(3, 0, 0):
		dmub_asic = DMUB_ASIC_DCN30;
		break;
	case IP_VERSION(3, 0, 1):
		dmub_asic = DMUB_ASIC_DCN301;
		break;
	case IP_VERSION(3, 0, 2):
		dmub_asic = DMUB_ASIC_DCN302;
		break;
	case IP_VERSION(3, 0, 3):
		dmub_asic = DMUB_ASIC_DCN303;
		break;
	case IP_VERSION(3, 1, 2):
	case IP_VERSION(3, 1, 3):
		dmub_asic = (adev->external_rev_id == YELLOW_CARP_B0) ? DMUB_ASIC_DCN31B : DMUB_ASIC_DCN31;
		break;
	case IP_VERSION(3, 1, 4):
		dmub_asic = DMUB_ASIC_DCN314;
		break;
	case IP_VERSION(3, 1, 5):
		dmub_asic = DMUB_ASIC_DCN315;
		break;
	case IP_VERSION(3, 1, 6):
		dmub_asic = DMUB_ASIC_DCN316;
		break;
	case IP_VERSION(3, 2, 0):
		dmub_asic = DMUB_ASIC_DCN32;
		break;
	case IP_VERSION(3, 2, 1):
		dmub_asic = DMUB_ASIC_DCN321;
		break;
	case IP_VERSION(3, 5, 0):
	case IP_VERSION(3, 5, 1):
		dmub_asic = DMUB_ASIC_DCN35;
		break;
	case IP_VERSION(4, 0, 1):
		dmub_asic = DMUB_ASIC_DCN401;
		break;

	default:
		/* ASIC doesn't support DMUB. */
		return 0;
	}

	hdr = (const struct dmcub_firmware_header_v1_0 *)adev->dm.dmub_fw->data;
	adev->dm.dmcub_fw_version = le32_to_cpu(hdr->header.ucode_version);

	if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) {
		adev->firmware.ucode[AMDGPU_UCODE_ID_DMCUB].ucode_id =
			AMDGPU_UCODE_ID_DMCUB;
		adev->firmware.ucode[AMDGPU_UCODE_ID_DMCUB].fw =
			adev->dm.dmub_fw;
		adev->firmware.fw_size +=
			ALIGN(le32_to_cpu(hdr->inst_const_bytes), PAGE_SIZE);

		DRM_INFO("Loading DMUB firmware via PSP: version=0x%08X\n",
			 adev->dm.dmcub_fw_version);
	}


	adev->dm.dmub_srv = kzalloc(sizeof(*adev->dm.dmub_srv), GFP_KERNEL);
	dmub_srv = adev->dm.dmub_srv;

	if (!dmub_srv) {
		DRM_ERROR("Failed to allocate DMUB service!\n");
		return -ENOMEM;
	}

	memset(&create_params, 0, sizeof(create_params));
	create_params.user_ctx = adev;
	create_params.funcs.reg_read = amdgpu_dm_dmub_reg_read;
	create_params.funcs.reg_write = amdgpu_dm_dmub_reg_write;
	create_params.asic = dmub_asic;

	/* Create the DMUB service. */
	status = dmub_srv_create(dmub_srv, &create_params);
	if (status != DMUB_STATUS_OK) {
		DRM_ERROR("Error creating DMUB service: %d\n", status);
		return -EINVAL;
	}

	/* Calculate the size of all the regions for the DMUB service. */
	memset(&region_params, 0, sizeof(region_params));

	region_params.inst_const_size = le32_to_cpu(hdr->inst_const_bytes) -
					PSP_HEADER_BYTES - PSP_FOOTER_BYTES;
	region_params.bss_data_size = le32_to_cpu(hdr->bss_data_bytes);
	region_params.vbios_size = adev->bios_size;
	region_params.fw_bss_data = region_params.bss_data_size ?
		adev->dm.dmub_fw->data +
		le32_to_cpu(hdr->header.ucode_array_offset_bytes) +
		le32_to_cpu(hdr->inst_const_bytes) : NULL;
	region_params.fw_inst_const =
		adev->dm.dmub_fw->data +
		le32_to_cpu(hdr->header.ucode_array_offset_bytes) +
		PSP_HEADER_BYTES;
	region_params.window_memory_type = window_memory_type;

	status = dmub_srv_calc_region_info(dmub_srv, &region_params,
					   &region_info);

	if (status != DMUB_STATUS_OK) {
		DRM_ERROR("Error calculating DMUB region info: %d\n", status);
		return -EINVAL;
	}

	/*
	 * Allocate a framebuffer based on the total size of all the regions.
	 * TODO: Move this into GART.
	 */
	r = amdgpu_bo_create_kernel(adev, region_info.fb_size, PAGE_SIZE,
				    AMDGPU_GEM_DOMAIN_VRAM |
				    AMDGPU_GEM_DOMAIN_GTT,
				    &adev->dm.dmub_bo,
				    &adev->dm.dmub_bo_gpu_addr,
				    &adev->dm.dmub_bo_cpu_addr);
	if (r)
		return r;

	/* Rebase the regions on the framebuffer address. */
	memset(&memory_params, 0, sizeof(memory_params));
	memory_params.cpu_fb_addr = adev->dm.dmub_bo_cpu_addr;
	memory_params.gpu_fb_addr = adev->dm.dmub_bo_gpu_addr;
	memory_params.region_info = &region_info;
	memory_params.window_memory_type = window_memory_type;

	adev->dm.dmub_fb_info =
		kzalloc(sizeof(*adev->dm.dmub_fb_info), GFP_KERNEL);
	fb_info = adev->dm.dmub_fb_info;

	if (!fb_info) {
		DRM_ERROR(
			"Failed to allocate framebuffer info for DMUB service!\n");
		return -ENOMEM;
	}

	status = dmub_srv_calc_mem_info(dmub_srv, &memory_params, fb_info);
	if (status != DMUB_STATUS_OK) {
		DRM_ERROR("Error calculating DMUB FB info: %d\n", status);
		return -EINVAL;
	}

	adev->dm.bb_from_dmub = dm_dmub_get_vbios_bounding_box(adev);

	return 0;
}

static int dm_sw_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int r;

	adev->dm.cgs_device = amdgpu_cgs_create_device(adev);

	if (!adev->dm.cgs_device) {
		DRM_ERROR("amdgpu: failed to create cgs device.\n");
		return -EINVAL;
	}

	/* Moved from dm init since we need to use allocations for storing bounding box data */
	INIT_LIST_HEAD(&adev->dm.da_list);

	r = dm_dmub_sw_init(adev);
	if (r)
		return r;

	return load_dmcu_fw(adev);
}

static int dm_sw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct dal_allocation *da;

	list_for_each_entry(da, &adev->dm.da_list, list) {
		if (adev->dm.bb_from_dmub == (void *) da->cpu_ptr) {
			amdgpu_bo_free_kernel(&da->bo, &da->gpu_addr, &da->cpu_ptr);
			list_del(&da->list);
			kfree(da);
			adev->dm.bb_from_dmub = NULL;
			break;
		}
	}


	kfree(adev->dm.dmub_fb_info);
	adev->dm.dmub_fb_info = NULL;

	if (adev->dm.dmub_srv) {
		dmub_srv_destroy(adev->dm.dmub_srv);
		kfree(adev->dm.dmub_srv);
		adev->dm.dmub_srv = NULL;
	}

	amdgpu_ucode_release(&adev->dm.dmub_fw);
	amdgpu_ucode_release(&adev->dm.fw_dmcu);

	return 0;
}

static int detect_mst_link_for_all_connectors(struct drm_device *dev)
{
	struct amdgpu_dm_connector *aconnector;
	struct drm_connector *connector;
	struct drm_connector_list_iter iter;
	int ret = 0;

	drm_connector_list_iter_begin(dev, &iter);
	drm_for_each_connector_iter(connector, &iter) {

		if (connector->connector_type == DRM_MODE_CONNECTOR_WRITEBACK)
			continue;

		aconnector = to_amdgpu_dm_connector(connector);
		if (aconnector->dc_link->type == dc_connection_mst_branch &&
		    aconnector->mst_mgr.aux) {
			drm_dbg_kms(dev, "DM_MST: starting TM on aconnector: %p [id: %d]\n",
					 aconnector,
					 aconnector->base.base.id);

			ret = drm_dp_mst_topology_mgr_set_mst(&aconnector->mst_mgr, true);
			if (ret < 0) {
				drm_err(dev, "DM_MST: Failed to start MST\n");
				aconnector->dc_link->type =
					dc_connection_single;
				ret = dm_helpers_dp_mst_stop_top_mgr(aconnector->dc_link->ctx,
								     aconnector->dc_link);
				break;
			}
		}
	}
	drm_connector_list_iter_end(&iter);

	return ret;
}

static int dm_late_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	struct dmcu_iram_parameters params;
	unsigned int linear_lut[16];
	int i;
	struct dmcu *dmcu = NULL;

	dmcu = adev->dm.dc->res_pool->dmcu;

	for (i = 0; i < 16; i++)
		linear_lut[i] = 0xFFFF * i / 15;

	params.set = 0;
	params.backlight_ramping_override = false;
	params.backlight_ramping_start = 0xCCCC;
	params.backlight_ramping_reduction = 0xCCCCCCCC;
	params.backlight_lut_array_size = 16;
	params.backlight_lut_array = linear_lut;

	/* Min backlight level after ABM reduction,  Don't allow below 1%
	 * 0xFFFF x 0.01 = 0x28F
	 */
	params.min_abm_backlight = 0x28F;
	/* In the case where abm is implemented on dmcub,
	 * dmcu object will be null.
	 * ABM 2.4 and up are implemented on dmcub.
	 */
	if (dmcu) {
		if (!dmcu_load_iram(dmcu, params))
			return -EINVAL;
	} else if (adev->dm.dc->ctx->dmub_srv) {
		struct dc_link *edp_links[MAX_NUM_EDP];
		int edp_num;

		dc_get_edp_links(adev->dm.dc, edp_links, &edp_num);
		for (i = 0; i < edp_num; i++) {
			if (!dmub_init_abm_config(adev->dm.dc->res_pool, params, i))
				return -EINVAL;
		}
	}

	return detect_mst_link_for_all_connectors(adev_to_drm(adev));
}

static void resume_mst_branch_status(struct drm_dp_mst_topology_mgr *mgr)
{
	u8 buf[UUID_SIZE];
	guid_t guid;
	int ret;

	mutex_lock(&mgr->lock);
	if (!mgr->mst_primary)
		goto out_fail;

	if (drm_dp_read_dpcd_caps(mgr->aux, mgr->dpcd) < 0) {
		drm_dbg_kms(mgr->dev, "dpcd read failed - undocked during suspend?\n");
		goto out_fail;
	}

	ret = drm_dp_dpcd_writeb(mgr->aux, DP_MSTM_CTRL,
				 DP_MST_EN |
				 DP_UP_REQ_EN |
				 DP_UPSTREAM_IS_SRC);
	if (ret < 0) {
		drm_dbg_kms(mgr->dev, "mst write failed - undocked during suspend?\n");
		goto out_fail;
	}

	/* Some hubs forget their guids after they resume */
	ret = drm_dp_dpcd_read(mgr->aux, DP_GUID, buf, sizeof(buf));
	if (ret != sizeof(buf)) {
		drm_dbg_kms(mgr->dev, "dpcd read failed - undocked during suspend?\n");
		goto out_fail;
	}

	import_guid(&guid, buf);

	if (guid_is_null(&guid)) {
		guid_gen(&guid);
		export_guid(buf, &guid);

		ret = drm_dp_dpcd_write(mgr->aux, DP_GUID, buf, sizeof(buf));

		if (ret != sizeof(buf)) {
			drm_dbg_kms(mgr->dev, "check mstb guid failed - undocked during suspend?\n");
			goto out_fail;
		}
	}

	guid_copy(&mgr->mst_primary->guid, &guid);

out_fail:
	mutex_unlock(&mgr->lock);
}

static void s3_handle_mst(struct drm_device *dev, bool suspend)
{
	struct amdgpu_dm_connector *aconnector;
	struct drm_connector *connector;
	struct drm_connector_list_iter iter;
	struct drm_dp_mst_topology_mgr *mgr;

	drm_connector_list_iter_begin(dev, &iter);
	drm_for_each_connector_iter(connector, &iter) {

		if (connector->connector_type == DRM_MODE_CONNECTOR_WRITEBACK)
			continue;

		aconnector = to_amdgpu_dm_connector(connector);
		if (aconnector->dc_link->type != dc_connection_mst_branch ||
		    aconnector->mst_root)
			continue;

		mgr = &aconnector->mst_mgr;

		if (suspend) {
			drm_dp_mst_topology_mgr_suspend(mgr);
		} else {
			/* if extended timeout is supported in hardware,
			 * default to LTTPR timeout (3.2ms) first as a W/A for DP link layer
			 * CTS 4.2.1.1 regression introduced by CTS specs requirement update.
			 */
			try_to_configure_aux_timeout(aconnector->dc_link->ddc, LINK_AUX_DEFAULT_LTTPR_TIMEOUT_PERIOD);
			if (!dp_is_lttpr_present(aconnector->dc_link))
				try_to_configure_aux_timeout(aconnector->dc_link->ddc, LINK_AUX_DEFAULT_TIMEOUT_PERIOD);

			/* TODO: move resume_mst_branch_status() into drm mst resume again
			 * once topology probing work is pulled out from mst resume into mst
			 * resume 2nd step. mst resume 2nd step should be called after old
			 * state getting restored (i.e. drm_atomic_helper_resume()).
			 */
			resume_mst_branch_status(mgr);
		}
	}
	drm_connector_list_iter_end(&iter);
}

static int amdgpu_dm_smu_write_watermarks_table(struct amdgpu_device *adev)
{
	int ret = 0;

	/* This interface is for dGPU Navi1x.Linux dc-pplib interface depends
	 * on window driver dc implementation.
	 * For Navi1x, clock settings of dcn watermarks are fixed. the settings
	 * should be passed to smu during boot up and resume from s3.
	 * boot up: dc calculate dcn watermark clock settings within dc_create,
	 * dcn20_resource_construct
	 * then call pplib functions below to pass the settings to smu:
	 * smu_set_watermarks_for_clock_ranges
	 * smu_set_watermarks_table
	 * navi10_set_watermarks_table
	 * smu_write_watermarks_table
	 *
	 * For Renoir, clock settings of dcn watermark are also fixed values.
	 * dc has implemented different flow for window driver:
	 * dc_hardware_init / dc_set_power_state
	 * dcn10_init_hw
	 * notify_wm_ranges
	 * set_wm_ranges
	 * -- Linux
	 * smu_set_watermarks_for_clock_ranges
	 * renoir_set_watermarks_table
	 * smu_write_watermarks_table
	 *
	 * For Linux,
	 * dc_hardware_init -> amdgpu_dm_init
	 * dc_set_power_state --> dm_resume
	 *
	 * therefore, this function apply to navi10/12/14 but not Renoir
	 * *
	 */
	switch (amdgpu_ip_version(adev, DCE_HWIP, 0)) {
	case IP_VERSION(2, 0, 2):
	case IP_VERSION(2, 0, 0):
		break;
	default:
		return 0;
	}

	ret = amdgpu_dpm_write_watermarks_table(adev);
	if (ret) {
		DRM_ERROR("Failed to update WMTABLE!\n");
		return ret;
	}

	return 0;
}

/**
 * dm_hw_init() - Initialize DC device
 * @handle: The base driver device containing the amdgpu_dm device.
 *
 * Initialize the &struct amdgpu_display_manager device. This involves calling
 * the initializers of each DM component, then populating the struct with them.
 *
 * Although the function implies hardware initialization, both hardware and
 * software are initialized here. Splitting them out to their relevant init
 * hooks is a future TODO item.
 *
 * Some notable things that are initialized here:
 *
 * - Display Core, both software and hardware
 * - DC modules that we need (freesync and color management)
 * - DRM software states
 * - Interrupt sources and handlers
 * - Vblank support
 * - Debug FS entries, if enabled
 */
static int dm_hw_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int r;

	/* Create DAL display manager */
	r = amdgpu_dm_init(adev);
	if (r)
		return r;
	amdgpu_dm_hpd_init(adev);

	return 0;
}

/**
 * dm_hw_fini() - Teardown DC device
 * @handle: The base driver device containing the amdgpu_dm device.
 *
 * Teardown components within &struct amdgpu_display_manager that require
 * cleanup. This involves cleaning up the DRM device, DC, and any modules that
 * were loaded. Also flush IRQ workqueues and disable them.
 */
static int dm_hw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	amdgpu_dm_hpd_fini(adev);

	amdgpu_dm_irq_fini(adev);
	amdgpu_dm_fini(adev);
	return 0;
}


static void dm_gpureset_toggle_interrupts(struct amdgpu_device *adev,
				 struct dc_state *state, bool enable)
{
	enum dc_irq_source irq_source;
	struct amdgpu_crtc *acrtc;
	int rc = -EBUSY;
	int i = 0;

	for (i = 0; i < state->stream_count; i++) {
		acrtc = get_crtc_by_otg_inst(
				adev, state->stream_status[i].primary_otg_inst);

		if (acrtc && state->stream_status[i].plane_count != 0) {
			irq_source = IRQ_TYPE_PFLIP + acrtc->otg_inst;
			rc = dc_interrupt_set(adev->dm.dc, irq_source, enable) ? 0 : -EBUSY;
			if (rc)
				DRM_WARN("Failed to %s pflip interrupts\n",
					 enable ? "enable" : "disable");

			if (enable) {
				if (amdgpu_dm_crtc_vrr_active(to_dm_crtc_state(acrtc->base.state)))
					rc = amdgpu_dm_crtc_set_vupdate_irq(&acrtc->base, true);
			} else
				rc = amdgpu_dm_crtc_set_vupdate_irq(&acrtc->base, false);

			if (rc)
				DRM_WARN("Failed to %sable vupdate interrupt\n", enable ? "en" : "dis");

			irq_source = IRQ_TYPE_VBLANK + acrtc->otg_inst;
			/* During gpu-reset we disable and then enable vblank irq, so
			 * don't use amdgpu_irq_get/put() to avoid refcount change.
			 */
			if (!dc_interrupt_set(adev->dm.dc, irq_source, enable))
				DRM_WARN("Failed to %sable vblank interrupt\n", enable ? "en" : "dis");
		}
	}

}

static enum dc_status amdgpu_dm_commit_zero_streams(struct dc *dc)
{
	struct dc_state *context = NULL;
	enum dc_status res = DC_ERROR_UNEXPECTED;
	int i;
	struct dc_stream_state *del_streams[MAX_PIPES];
	int del_streams_count = 0;
	struct dc_commit_streams_params params = {};

	memset(del_streams, 0, sizeof(del_streams));

	context = dc_state_create_current_copy(dc);
	if (context == NULL)
		goto context_alloc_fail;

	/* First remove from context all streams */
	for (i = 0; i < context->stream_count; i++) {
		struct dc_stream_state *stream = context->streams[i];

		del_streams[del_streams_count++] = stream;
	}

	/* Remove all planes for removed streams and then remove the streams */
	for (i = 0; i < del_streams_count; i++) {
		if (!dc_state_rem_all_planes_for_stream(dc, del_streams[i], context)) {
			res = DC_FAIL_DETACH_SURFACES;
			goto fail;
		}

		res = dc_state_remove_stream(dc, context, del_streams[i]);
		if (res != DC_OK)
			goto fail;
	}

	params.streams = context->streams;
	params.stream_count = context->stream_count;
	res = dc_commit_streams(dc, &params);

fail:
	dc_state_release(context);

context_alloc_fail:
	return res;
}

static void hpd_rx_irq_work_suspend(struct amdgpu_display_manager *dm)
{
	int i;

	if (dm->hpd_rx_offload_wq) {
		for (i = 0; i < dm->dc->caps.max_links; i++)
			flush_workqueue(dm->hpd_rx_offload_wq[i].wq);
	}
}

static int dm_suspend(void *handle)
{
	struct amdgpu_device *adev = handle;
	struct amdgpu_display_manager *dm = &adev->dm;
	int ret = 0;

	if (amdgpu_in_reset(adev)) {
		mutex_lock(&dm->dc_lock);

		dc_allow_idle_optimizations(adev->dm.dc, false);

		dm->cached_dc_state = dc_state_create_copy(dm->dc->current_state);

		if (dm->cached_dc_state)
			dm_gpureset_toggle_interrupts(adev, dm->cached_dc_state, false);

		amdgpu_dm_commit_zero_streams(dm->dc);

		amdgpu_dm_irq_suspend(adev);

		hpd_rx_irq_work_suspend(dm);

		return ret;
	}

	WARN_ON(adev->dm.cached_state);
	adev->dm.cached_state = drm_atomic_helper_suspend(adev_to_drm(adev));
	if (IS_ERR(adev->dm.cached_state))
		return PTR_ERR(adev->dm.cached_state);

	s3_handle_mst(adev_to_drm(adev), true);

	amdgpu_dm_irq_suspend(adev);

	hpd_rx_irq_work_suspend(dm);

	dc_set_power_state(dm->dc, DC_ACPI_CM_POWER_STATE_D3);

	if (dm->dc->caps.ips_support && adev->in_s0ix)
		dc_allow_idle_optimizations(dm->dc, true);

	dc_dmub_srv_set_power_state(dm->dc->ctx->dmub_srv, DC_ACPI_CM_POWER_STATE_D3);

	return 0;
}

struct drm_connector *
amdgpu_dm_find_first_crtc_matching_connector(struct drm_atomic_state *state,
					     struct drm_crtc *crtc)
{
	u32 i;
	struct drm_connector_state *new_con_state;
	struct drm_connector *connector;
	struct drm_crtc *crtc_from_state;

	for_each_new_connector_in_state(state, connector, new_con_state, i) {
		crtc_from_state = new_con_state->crtc;

		if (crtc_from_state == crtc)
			return connector;
	}

	return NULL;
}

static void emulated_link_detect(struct dc_link *link)
{
	struct dc_sink_init_data sink_init_data = { 0 };
	struct display_sink_capability sink_caps = { 0 };
	enum dc_edid_status edid_status;
	struct dc_context *dc_ctx = link->ctx;
	struct drm_device *dev = adev_to_drm(dc_ctx->driver_context);
	struct dc_sink *sink = NULL;
	struct dc_sink *prev_sink = NULL;

	link->type = dc_connection_none;
	prev_sink = link->local_sink;

	if (prev_sink)
		dc_sink_release(prev_sink);

	switch (link->connector_signal) {
	case SIGNAL_TYPE_HDMI_TYPE_A: {
		sink_caps.transaction_type = DDC_TRANSACTION_TYPE_I2C;
		sink_caps.signal = SIGNAL_TYPE_HDMI_TYPE_A;
		break;
	}

	case SIGNAL_TYPE_DVI_SINGLE_LINK: {
		sink_caps.transaction_type = DDC_TRANSACTION_TYPE_I2C;
		sink_caps.signal = SIGNAL_TYPE_DVI_SINGLE_LINK;
		break;
	}

	case SIGNAL_TYPE_DVI_DUAL_LINK: {
		sink_caps.transaction_type = DDC_TRANSACTION_TYPE_I2C;
		sink_caps.signal = SIGNAL_TYPE_DVI_DUAL_LINK;
		break;
	}

	case SIGNAL_TYPE_LVDS: {
		sink_caps.transaction_type = DDC_TRANSACTION_TYPE_I2C;
		sink_caps.signal = SIGNAL_TYPE_LVDS;
		break;
	}

	case SIGNAL_TYPE_EDP: {
		sink_caps.transaction_type =
			DDC_TRANSACTION_TYPE_I2C_OVER_AUX;
		sink_caps.signal = SIGNAL_TYPE_EDP;
		break;
	}

	case SIGNAL_TYPE_DISPLAY_PORT: {
		sink_caps.transaction_type =
			DDC_TRANSACTION_TYPE_I2C_OVER_AUX;
		sink_caps.signal = SIGNAL_TYPE_VIRTUAL;
		break;
	}

	default:
		drm_err(dev, "Invalid connector type! signal:%d\n",
			link->connector_signal);
		return;
	}

	sink_init_data.link = link;
	sink_init_data.sink_signal = sink_caps.signal;

	sink = dc_sink_create(&sink_init_data);
	if (!sink) {
		drm_err(dev, "Failed to create sink!\n");
		return;
	}

	/* dc_sink_create returns a new reference */
	link->local_sink = sink;

	edid_status = dm_helpers_read_local_edid(
			link->ctx,
			link,
			sink);

	if (edid_status != EDID_OK)
		drm_err(dev, "Failed to read EDID\n");

}

static void dm_gpureset_commit_state(struct dc_state *dc_state,
				     struct amdgpu_display_manager *dm)
{
	struct {
		struct dc_surface_update surface_updates[MAX_SURFACES];
		struct dc_plane_info plane_infos[MAX_SURFACES];
		struct dc_scaling_info scaling_infos[MAX_SURFACES];
		struct dc_flip_addrs flip_addrs[MAX_SURFACES];
		struct dc_stream_update stream_update;
	} *bundle;
	int k, m;

	bundle = kzalloc(sizeof(*bundle), GFP_KERNEL);

	if (!bundle) {
		drm_err(dm->ddev, "Failed to allocate update bundle\n");
		goto cleanup;
	}

	for (k = 0; k < dc_state->stream_count; k++) {
		bundle->stream_update.stream = dc_state->streams[k];

		for (m = 0; m < dc_state->stream_status[k].plane_count; m++) {
			bundle->surface_updates[m].surface =
				dc_state->stream_status[k].plane_states[m];
			bundle->surface_updates[m].surface->force_full_update =
				true;
		}

		update_planes_and_stream_adapter(dm->dc,
					 UPDATE_TYPE_FULL,
					 dc_state->stream_status[k].plane_count,
					 dc_state->streams[k],
					 &bundle->stream_update,
					 bundle->surface_updates);
	}

cleanup:
	kfree(bundle);
}

static int dm_resume(void *handle)
{
	struct amdgpu_device *adev = handle;
	struct drm_device *ddev = adev_to_drm(adev);
	struct amdgpu_display_manager *dm = &adev->dm;
	struct amdgpu_dm_connector *aconnector;
	struct drm_connector *connector;
	struct drm_connector_list_iter iter;
	struct drm_crtc *crtc;
	struct drm_crtc_state *new_crtc_state;
	struct dm_crtc_state *dm_new_crtc_state;
	struct drm_plane *plane;
	struct drm_plane_state *new_plane_state;
	struct dm_plane_state *dm_new_plane_state;
	struct dm_atomic_state *dm_state = to_dm_atomic_state(dm->atomic_obj.state);
	enum dc_connection_type new_connection_type = dc_connection_none;
	struct dc_state *dc_state;
	int i, r, j;
	struct dc_commit_streams_params commit_params = {};

	if (dm->dc->caps.ips_support) {
		dc_dmub_srv_apply_idle_power_optimizations(dm->dc, false);
	}

	if (amdgpu_in_reset(adev)) {
		dc_state = dm->cached_dc_state;

		/*
		 * The dc->current_state is backed up into dm->cached_dc_state
		 * before we commit 0 streams.
		 *
		 * DC will clear link encoder assignments on the real state
		 * but the changes won't propagate over to the copy we made
		 * before the 0 streams commit.
		 *
		 * DC expects that link encoder assignments are *not* valid
		 * when committing a state, so as a workaround we can copy
		 * off of the current state.
		 *
		 * We lose the previous assignments, but we had already
		 * commit 0 streams anyway.
		 */
		link_enc_cfg_copy(adev->dm.dc->current_state, dc_state);

		r = dm_dmub_hw_init(adev);
		if (r)
			DRM_ERROR("DMUB interface failed to initialize: status=%d\n", r);

		dc_dmub_srv_set_power_state(dm->dc->ctx->dmub_srv, DC_ACPI_CM_POWER_STATE_D0);
		dc_set_power_state(dm->dc, DC_ACPI_CM_POWER_STATE_D0);

		dc_resume(dm->dc);

		amdgpu_dm_irq_resume_early(adev);

		for (i = 0; i < dc_state->stream_count; i++) {
			dc_state->streams[i]->mode_changed = true;
			for (j = 0; j < dc_state->stream_status[i].plane_count; j++) {
				dc_state->stream_status[i].plane_states[j]->update_flags.raw
					= 0xffffffff;
			}
		}

		if (dc_is_dmub_outbox_supported(adev->dm.dc)) {
			amdgpu_dm_outbox_init(adev);
			dc_enable_dmub_outbox(adev->dm.dc);
		}

		commit_params.streams = dc_state->streams;
		commit_params.stream_count = dc_state->stream_count;
		dc_exit_ips_for_hw_access(dm->dc);
		WARN_ON(!dc_commit_streams(dm->dc, &commit_params));

		dm_gpureset_commit_state(dm->cached_dc_state, dm);

		dm_gpureset_toggle_interrupts(adev, dm->cached_dc_state, true);

		dc_state_release(dm->cached_dc_state);
		dm->cached_dc_state = NULL;

		amdgpu_dm_irq_resume_late(adev);

		mutex_unlock(&dm->dc_lock);

		/* set the backlight after a reset */
		for (i = 0; i < dm->num_of_edps; i++) {
			if (dm->backlight_dev[i])
				amdgpu_dm_backlight_set_level(dm, i, dm->brightness[i]);
		}

		return 0;
	}
	/* Recreate dc_state - DC invalidates it when setting power state to S3. */
	dc_state_release(dm_state->context);
	dm_state->context = dc_state_create(dm->dc, NULL);
	/* TODO: Remove dc_state->dccg, use dc->dccg directly. */

	/* Before powering on DC we need to re-initialize DMUB. */
	dm_dmub_hw_resume(adev);

	/* Re-enable outbox interrupts for DPIA. */
	if (dc_is_dmub_outbox_supported(adev->dm.dc)) {
		amdgpu_dm_outbox_init(adev);
		dc_enable_dmub_outbox(adev->dm.dc);
	}

	/* power on hardware */
	dc_dmub_srv_set_power_state(dm->dc->ctx->dmub_srv, DC_ACPI_CM_POWER_STATE_D0);
	dc_set_power_state(dm->dc, DC_ACPI_CM_POWER_STATE_D0);

	/* program HPD filter */
	dc_resume(dm->dc);

	/*
	 * early enable HPD Rx IRQ, should be done before set mode as short
	 * pulse interrupts are used for MST
	 */
	amdgpu_dm_irq_resume_early(adev);

	/* On resume we need to rewrite the MSTM control bits to enable MST*/
	s3_handle_mst(ddev, false);

	/* Do detection*/
	drm_connector_list_iter_begin(ddev, &iter);
	drm_for_each_connector_iter(connector, &iter) {

		if (connector->connector_type == DRM_MODE_CONNECTOR_WRITEBACK)
			continue;

		aconnector = to_amdgpu_dm_connector(connector);

		if (!aconnector->dc_link)
			continue;

		/*
		 * this is the case when traversing through already created end sink
		 * MST connectors, should be skipped
		 */
		if (aconnector->mst_root)
			continue;

		mutex_lock(&aconnector->hpd_lock);
		if (!dc_link_detect_connection_type(aconnector->dc_link, &new_connection_type))
			DRM_ERROR("KMS: Failed to detect connector\n");

		if (aconnector->base.force && new_connection_type == dc_connection_none) {
			emulated_link_detect(aconnector->dc_link);
		} else {
			mutex_lock(&dm->dc_lock);
			dc_exit_ips_for_hw_access(dm->dc);
			dc_link_detect(aconnector->dc_link, DETECT_REASON_RESUMEFROMS3S4);
			mutex_unlock(&dm->dc_lock);
		}

		if (aconnector->fake_enable && aconnector->dc_link->local_sink)
			aconnector->fake_enable = false;

		if (aconnector->dc_sink)
			dc_sink_release(aconnector->dc_sink);
		aconnector->dc_sink = NULL;
		amdgpu_dm_update_connector_after_detect(aconnector);
		mutex_unlock(&aconnector->hpd_lock);
	}
	drm_connector_list_iter_end(&iter);

	/* Force mode set in atomic commit */
	for_each_new_crtc_in_state(dm->cached_state, crtc, new_crtc_state, i) {
		new_crtc_state->active_changed = true;
		dm_new_crtc_state = to_dm_crtc_state(new_crtc_state);
		reset_freesync_config_for_crtc(dm_new_crtc_state);
	}

	/*
	 * atomic_check is expected to create the dc states. We need to release
	 * them here, since they were duplicated as part of the suspend
	 * procedure.
	 */
	for_each_new_crtc_in_state(dm->cached_state, crtc, new_crtc_state, i) {
		dm_new_crtc_state = to_dm_crtc_state(new_crtc_state);
		if (dm_new_crtc_state->stream) {
			WARN_ON(kref_read(&dm_new_crtc_state->stream->refcount) > 1);
			dc_stream_release(dm_new_crtc_state->stream);
			dm_new_crtc_state->stream = NULL;
		}
		dm_new_crtc_state->base.color_mgmt_changed = true;
	}

	for_each_new_plane_in_state(dm->cached_state, plane, new_plane_state, i) {
		dm_new_plane_state = to_dm_plane_state(new_plane_state);
		if (dm_new_plane_state->dc_state) {
			WARN_ON(kref_read(&dm_new_plane_state->dc_state->refcount) > 1);
			dc_plane_state_release(dm_new_plane_state->dc_state);
			dm_new_plane_state->dc_state = NULL;
		}
	}

	drm_atomic_helper_resume(ddev, dm->cached_state);

	dm->cached_state = NULL;

	/* Do mst topology probing after resuming cached state*/
	drm_connector_list_iter_begin(ddev, &iter);
	drm_for_each_connector_iter(connector, &iter) {

		if (connector->connector_type == DRM_MODE_CONNECTOR_WRITEBACK)
			continue;

		aconnector = to_amdgpu_dm_connector(connector);
		if (aconnector->dc_link->type != dc_connection_mst_branch ||
		    aconnector->mst_root)
			continue;

		drm_dp_mst_topology_queue_probe(&aconnector->mst_mgr);
	}
	drm_connector_list_iter_end(&iter);

	amdgpu_dm_irq_resume_late(adev);

	amdgpu_dm_smu_write_watermarks_table(adev);

	drm_kms_helper_hotplug_event(ddev);

	return 0;
}

/**
 * DOC: DM Lifecycle
 *
 * DM (and consequently DC) is registered in the amdgpu base driver as a IP
 * block. When CONFIG_DRM_AMD_DC is enabled, the DM device IP block is added to
 * the base driver's device list to be initialized and torn down accordingly.
 *
 * The functions to do so are provided as hooks in &struct amd_ip_funcs.
 */

static const struct amd_ip_funcs amdgpu_dm_funcs = {
	.name = "dm",
	.early_init = dm_early_init,
	.late_init = dm_late_init,
	.sw_init = dm_sw_init,
	.sw_fini = dm_sw_fini,
	.early_fini = amdgpu_dm_early_fini,
	.hw_init = dm_hw_init,
	.hw_fini = dm_hw_fini,
	.suspend = dm_suspend,
	.resume = dm_resume,
	.is_idle = dm_is_idle,
	.wait_for_idle = dm_wait_for_idle,
	.check_soft_reset = dm_check_soft_reset,
	.soft_reset = dm_soft_reset,
	.set_clockgating_state = dm_set_clockgating_state,
	.set_powergating_state = dm_set_powergating_state,
	.dump_ip_state = NULL,
	.print_ip_state = NULL,
};

const struct amdgpu_ip_block_version dm_ip_block = {
	.type = AMD_IP_BLOCK_TYPE_DCE,
	.major = 1,
	.minor = 0,
	.rev = 0,
	.funcs = &amdgpu_dm_funcs,
};


/**
 * DOC: atomic
 *
 * *WIP*
 */

static const struct drm_mode_config_funcs amdgpu_dm_mode_funcs = {
	.fb_create = amdgpu_display_user_framebuffer_create,
	.get_format_info = amdgpu_dm_plane_get_format_info,
	.atomic_check = amdgpu_dm_atomic_check,
	.atomic_commit = drm_atomic_helper_commit,
};

static struct drm_mode_config_helper_funcs amdgpu_dm_mode_config_helperfuncs = {
	.atomic_commit_tail = amdgpu_dm_atomic_commit_tail,
	.atomic_commit_setup = drm_dp_mst_atomic_setup_commit,
};

static void update_connector_ext_caps(struct amdgpu_dm_connector *aconnector)
{
	struct amdgpu_dm_backlight_caps *caps;
	struct drm_connector *conn_base;
	struct amdgpu_device *adev;
	struct drm_luminance_range_info *luminance_range;

	if (aconnector->bl_idx == -1 ||
	    aconnector->dc_link->connector_signal != SIGNAL_TYPE_EDP)
		return;

	conn_base = &aconnector->base;
	adev = drm_to_adev(conn_base->dev);

	caps = &adev->dm.backlight_caps[aconnector->bl_idx];
	caps->ext_caps = &aconnector->dc_link->dpcd_sink_ext_caps;
	caps->aux_support = false;

	if (caps->ext_caps->bits.oled == 1
	    /*
	     * ||
	     * caps->ext_caps->bits.sdr_aux_backlight_control == 1 ||
	     * caps->ext_caps->bits.hdr_aux_backlight_control == 1
	     */)
		caps->aux_support = true;

	if (amdgpu_backlight == 0)
		caps->aux_support = false;
	else if (amdgpu_backlight == 1)
		caps->aux_support = true;

	luminance_range = &conn_base->display_info.luminance_range;

	if (luminance_range->max_luminance) {
		caps->aux_min_input_signal = luminance_range->min_luminance;
		caps->aux_max_input_signal = luminance_range->max_luminance;
	} else {
		caps->aux_min_input_signal = 0;
		caps->aux_max_input_signal = 512;
	}
}

void amdgpu_dm_update_connector_after_detect(
		struct amdgpu_dm_connector *aconnector)
{
	struct drm_connector *connector = &aconnector->base;
	struct drm_device *dev = connector->dev;
	struct dc_sink *sink;

	/* MST handled by drm_mst framework */
	if (aconnector->mst_mgr.mst_state == true)
		return;

	sink = aconnector->dc_link->local_sink;
	if (sink)
		dc_sink_retain(sink);

	/*
	 * Edid mgmt connector gets first update only in mode_valid hook and then
	 * the connector sink is set to either fake or physical sink depends on link status.
	 * Skip if already done during boot.
	 */
	if (aconnector->base.force != DRM_FORCE_UNSPECIFIED
			&& aconnector->dc_em_sink) {

		/*
		 * For S3 resume with headless use eml_sink to fake stream
		 * because on resume connector->sink is set to NULL
		 */
		mutex_lock(&dev->mode_config.mutex);

		if (sink) {
			if (aconnector->dc_sink) {
				amdgpu_dm_update_freesync_caps(connector, NULL);
				/*
				 * retain and release below are used to
				 * bump up refcount for sink because the link doesn't point
				 * to it anymore after disconnect, so on next crtc to connector
				 * reshuffle by UMD we will get into unwanted dc_sink release
				 */
				dc_sink_release(aconnector->dc_sink);
			}
			aconnector->dc_sink = sink;
			dc_sink_retain(aconnector->dc_sink);
			amdgpu_dm_update_freesync_caps(connector,
					aconnector->edid);
		} else {
			amdgpu_dm_update_freesync_caps(connector, NULL);
			if (!aconnector->dc_sink) {
				aconnector->dc_sink = aconnector->dc_em_sink;
				dc_sink_retain(aconnector->dc_sink);
			}
		}

		mutex_unlock(&dev->mode_config.mutex);

		if (sink)
			dc_sink_release(sink);
		return;
	}

	/*
	 * TODO: temporary guard to look for proper fix
	 * if this sink is MST sink, we should not do anything
	 */
	if (sink && sink->sink_signal == SIGNAL_TYPE_DISPLAY_PORT_MST) {
		dc_sink_release(sink);
		return;
	}

	if (aconnector->dc_sink == sink) {
		/*
		 * We got a DP short pulse (Link Loss, DP CTS, etc...).
		 * Do nothing!!
		 */
		drm_dbg_kms(dev, "DCHPD: connector_id=%d: dc_sink didn't change.\n",
				 aconnector->connector_id);
		if (sink)
			dc_sink_release(sink);
		return;
	}

	drm_dbg_kms(dev, "DCHPD: connector_id=%d: Old sink=%p New sink=%p\n",
		    aconnector->connector_id, aconnector->dc_sink, sink);

	mutex_lock(&dev->mode_config.mutex);

	/*
	 * 1. Update status of the drm connector
	 * 2. Send an event and let userspace tell us what to do
	 */
	if (sink) {
		/*
		 * TODO: check if we still need the S3 mode update workaround.
		 * If yes, put it here.
		 */
		if (aconnector->dc_sink) {
			amdgpu_dm_update_freesync_caps(connector, NULL);
			dc_sink_release(aconnector->dc_sink);
		}

		aconnector->dc_sink = sink;
		dc_sink_retain(aconnector->dc_sink);
		if (sink->dc_edid.length == 0) {
			aconnector->edid = NULL;
			if (aconnector->dc_link->aux_mode) {
				drm_dp_cec_unset_edid(
					&aconnector->dm_dp_aux.aux);
			}
		} else {
			aconnector->edid =
				(struct edid *)sink->dc_edid.raw_edid;

			if (aconnector->dc_link->aux_mode)
				drm_dp_cec_set_edid(&aconnector->dm_dp_aux.aux,
						    aconnector->edid);
		}

		if (!aconnector->timing_requested) {
			aconnector->timing_requested =
				kzalloc(sizeof(struct dc_crtc_timing), GFP_KERNEL);
			if (!aconnector->timing_requested)
				drm_err(dev,
					"failed to create aconnector->requested_timing\n");
		}

		drm_connector_update_edid_property(connector, aconnector->edid);
		amdgpu_dm_update_freesync_caps(connector, aconnector->edid);
		update_connector_ext_caps(aconnector);
	} else {
		drm_dp_cec_unset_edid(&aconnector->dm_dp_aux.aux);
		amdgpu_dm_update_freesync_caps(connector, NULL);
		drm_connector_update_edid_property(connector, NULL);
		aconnector->num_modes = 0;
		dc_sink_release(aconnector->dc_sink);
		aconnector->dc_sink = NULL;
		aconnector->edid = NULL;
		kfree(aconnector->timing_requested);
		aconnector->timing_requested = NULL;
		/* Set CP to DESIRED if it was ENABLED, so we can re-enable it again on hotplug */
		if (connector->state->content_protection == DRM_MODE_CONTENT_PROTECTION_ENABLED)
			connector->state->content_protection = DRM_MODE_CONTENT_PROTECTION_DESIRED;
	}

	mutex_unlock(&dev->mode_config.mutex);

	update_subconnector_property(aconnector);

	if (sink)
		dc_sink_release(sink);
}

static void handle_hpd_irq_helper(struct amdgpu_dm_connector *aconnector)
{
	struct drm_connector *connector = &aconnector->base;
	struct drm_device *dev = connector->dev;
	enum dc_connection_type new_connection_type = dc_connection_none;
	struct amdgpu_device *adev = drm_to_adev(dev);
	struct dm_connector_state *dm_con_state = to_dm_connector_state(connector->state);
	struct dc *dc = aconnector->dc_link->ctx->dc;
	bool ret = false;

	if (adev->dm.disable_hpd_irq)
		return;

	/*
	 * In case of failure or MST no need to update connector status or notify the OS
	 * since (for MST case) MST does this in its own context.
	 */
	mutex_lock(&aconnector->hpd_lock);

	if (adev->dm.hdcp_workqueue) {
		hdcp_reset_display(adev->dm.hdcp_workqueue, aconnector->dc_link->link_index);
		dm_con_state->update_hdcp = true;
	}
	if (aconnector->fake_enable)
		aconnector->fake_enable = false;

	aconnector->timing_changed = false;

	if (!dc_link_detect_connection_type(aconnector->dc_link, &new_connection_type))
		DRM_ERROR("KMS: Failed to detect connector\n");

	if (aconnector->base.force && new_connection_type == dc_connection_none) {
		emulated_link_detect(aconnector->dc_link);

		drm_modeset_lock_all(dev);
		dm_restore_drm_connector_state(dev, connector);
		drm_modeset_unlock_all(dev);

		if (aconnector->base.force == DRM_FORCE_UNSPECIFIED)
			drm_kms_helper_connector_hotplug_event(connector);
	} else {
		mutex_lock(&adev->dm.dc_lock);
		dc_exit_ips_for_hw_access(dc);
		ret = dc_link_detect(aconnector->dc_link, DETECT_REASON_HPD);
		mutex_unlock(&adev->dm.dc_lock);
		if (ret) {
			amdgpu_dm_update_connector_after_detect(aconnector);

			drm_modeset_lock_all(dev);
			dm_restore_drm_connector_state(dev, connector);
			drm_modeset_unlock_all(dev);

			if (aconnector->base.force == DRM_FORCE_UNSPECIFIED)
				drm_kms_helper_connector_hotplug_event(connector);
		}
	}
	mutex_unlock(&aconnector->hpd_lock);

}

static void handle_hpd_irq(void *param)
{
	struct amdgpu_dm_connector *aconnector = (struct amdgpu_dm_connector *)param;

	handle_hpd_irq_helper(aconnector);

}

static void schedule_hpd_rx_offload_work(struct hpd_rx_irq_offload_work_queue *offload_wq,
							union hpd_irq_data hpd_irq_data)
{
	struct hpd_rx_irq_offload_work *offload_work =
				kzalloc(sizeof(*offload_work), GFP_KERNEL);

	if (!offload_work) {
		DRM_ERROR("Failed to allocate hpd_rx_irq_offload_work.\n");
		return;
	}

	INIT_WORK(&offload_work->work, dm_handle_hpd_rx_offload_work);
	offload_work->data = hpd_irq_data;
	offload_work->offload_wq = offload_wq;

	queue_work(offload_wq->wq, &offload_work->work);
	DRM_DEBUG_KMS("queue work to handle hpd_rx offload work");
}

static void handle_hpd_rx_irq(void *param)
{
	struct amdgpu_dm_connector *aconnector = (struct amdgpu_dm_connector *)param;
	struct drm_connector *connector = &aconnector->base;
	struct drm_device *dev = connector->dev;
	struct dc_link *dc_link = aconnector->dc_link;
	bool is_mst_root_connector = aconnector->mst_mgr.mst_state;
	bool result = false;
	enum dc_connection_type new_connection_type = dc_connection_none;
	struct amdgpu_device *adev = drm_to_adev(dev);
	union hpd_irq_data hpd_irq_data;
	bool link_loss = false;
	bool has_left_work = false;
	int idx = dc_link->link_index;
	struct hpd_rx_irq_offload_work_queue *offload_wq = &adev->dm.hpd_rx_offload_wq[idx];
	struct dc *dc = aconnector->dc_link->ctx->dc;

	memset(&hpd_irq_data, 0, sizeof(hpd_irq_data));

	if (adev->dm.disable_hpd_irq)
		return;

	/*
	 * TODO:Temporary add mutex to protect hpd interrupt not have a gpio
	 * conflict, after implement i2c helper, this mutex should be
	 * retired.
	 */
	mutex_lock(&aconnector->hpd_lock);

	result = dc_link_handle_hpd_rx_irq(dc_link, &hpd_irq_data,
						&link_loss, true, &has_left_work);

	if (!has_left_work)
		goto out;

	if (hpd_irq_data.bytes.device_service_irq.bits.AUTOMATED_TEST) {
		schedule_hpd_rx_offload_work(offload_wq, hpd_irq_data);
		goto out;
	}

	if (dc_link_dp_allow_hpd_rx_irq(dc_link)) {
		if (hpd_irq_data.bytes.device_service_irq.bits.UP_REQ_MSG_RDY ||
			hpd_irq_data.bytes.device_service_irq.bits.DOWN_REP_MSG_RDY) {
			bool skip = false;

			/*
			 * DOWN_REP_MSG_RDY is also handled by polling method
			 * mgr->cbs->poll_hpd_irq()
			 */
			spin_lock(&offload_wq->offload_lock);
			skip = offload_wq->is_handling_mst_msg_rdy_event;

			if (!skip)
				offload_wq->is_handling_mst_msg_rdy_event = true;

			spin_unlock(&offload_wq->offload_lock);

			if (!skip)
				schedule_hpd_rx_offload_work(offload_wq, hpd_irq_data);

			goto out;
		}

		if (link_loss) {
			bool skip = false;

			spin_lock(&offload_wq->offload_lock);
			skip = offload_wq->is_handling_link_loss;

			if (!skip)
				offload_wq->is_handling_link_loss = true;

			spin_unlock(&offload_wq->offload_lock);

			if (!skip)
				schedule_hpd_rx_offload_work(offload_wq, hpd_irq_data);

			goto out;
		}
	}

out:
	if (result && !is_mst_root_connector) {
		/* Downstream Port status changed. */
		if (!dc_link_detect_connection_type(dc_link, &new_connection_type))
			DRM_ERROR("KMS: Failed to detect connector\n");

		if (aconnector->base.force && new_connection_type == dc_connection_none) {
			emulated_link_detect(dc_link);

			if (aconnector->fake_enable)
				aconnector->fake_enable = false;

			amdgpu_dm_update_connector_after_detect(aconnector);


			drm_modeset_lock_all(dev);
			dm_restore_drm_connector_state(dev, connector);
			drm_modeset_unlock_all(dev);

			drm_kms_helper_connector_hotplug_event(connector);
		} else {
			bool ret = false;

			mutex_lock(&adev->dm.dc_lock);
			dc_exit_ips_for_hw_access(dc);
			ret = dc_link_detect(dc_link, DETECT_REASON_HPDRX);
			mutex_unlock(&adev->dm.dc_lock);

			if (ret) {
				if (aconnector->fake_enable)
					aconnector->fake_enable = false;

				amdgpu_dm_update_connector_after_detect(aconnector);

				drm_modeset_lock_all(dev);
				dm_restore_drm_connector_state(dev, connector);
				drm_modeset_unlock_all(dev);

				drm_kms_helper_connector_hotplug_event(connector);
			}
		}
	}
	if (hpd_irq_data.bytes.device_service_irq.bits.CP_IRQ) {
		if (adev->dm.hdcp_workqueue)
			hdcp_handle_cpirq(adev->dm.hdcp_workqueue,  aconnector->base.index);
	}

	if (dc_link->type != dc_connection_mst_branch)
		drm_dp_cec_irq(&aconnector->dm_dp_aux.aux);

	mutex_unlock(&aconnector->hpd_lock);
}

static int register_hpd_handlers(struct amdgpu_device *adev)
{
	struct drm_device *dev = adev_to_drm(adev);
	struct drm_connector *connector;
	struct amdgpu_dm_connector *aconnector;
	const struct dc_link *dc_link;
	struct dc_interrupt_params int_params = {0};

	int_params.requested_polarity = INTERRUPT_POLARITY_DEFAULT;
	int_params.current_polarity = INTERRUPT_POLARITY_DEFAULT;

	if (dc_is_dmub_outbox_supported(adev->dm.dc)) {
		if (!register_dmub_notify_callback(adev, DMUB_NOTIFICATION_HPD,
			dmub_hpd_callback, true)) {
			DRM_ERROR("amdgpu: fail to register dmub hpd callback");
			return -EINVAL;
		}

		if (!register_dmub_notify_callback(adev, DMUB_NOTIFICATION_HPD_IRQ,
			dmub_hpd_callback, true)) {
			DRM_ERROR("amdgpu: fail to register dmub hpd callback");
			return -EINVAL;
		}

		if (!register_dmub_notify_callback(adev, DMUB_NOTIFICATION_HPD_SENSE_NOTIFY,
			dmub_hpd_sense_callback, true)) {
			DRM_ERROR("amdgpu: fail to register dmub hpd sense callback");
			return -EINVAL;
		}
	}

	list_for_each_entry(connector,
			&dev->mode_config.connector_list, head)	{

		if (connector->connector_type == DRM_MODE_CONNECTOR_WRITEBACK)
			continue;

		aconnector = to_amdgpu_dm_connector(connector);
		dc_link = aconnector->dc_link;

		if (dc_link->irq_source_hpd != DC_IRQ_SOURCE_INVALID) {
			int_params.int_context = INTERRUPT_LOW_IRQ_CONTEXT;
			int_params.irq_source = dc_link->irq_source_hpd;

			if (int_params.irq_source == DC_IRQ_SOURCE_INVALID ||
				int_params.irq_source  < DC_IRQ_SOURCE_HPD1 ||
				int_params.irq_source  > DC_IRQ_SOURCE_HPD6) {
				DRM_ERROR("Failed to register hpd irq!\n");
				return -EINVAL;
			}

			if (!amdgpu_dm_irq_register_interrupt(adev, &int_params,
				handle_hpd_irq, (void *) aconnector))
				return -ENOMEM;
		}

		if (dc_link->irq_source_hpd_rx != DC_IRQ_SOURCE_INVALID) {

			/* Also register for DP short pulse (hpd_rx). */
			int_params.int_context = INTERRUPT_LOW_IRQ_CONTEXT;
			int_params.irq_source =	dc_link->irq_source_hpd_rx;

			if (int_params.irq_source == DC_IRQ_SOURCE_INVALID ||
				int_params.irq_source  < DC_IRQ_SOURCE_HPD1RX ||
				int_params.irq_source  > DC_IRQ_SOURCE_HPD6RX) {
				DRM_ERROR("Failed to register hpd rx irq!\n");
				return -EINVAL;
			}

			if (!amdgpu_dm_irq_register_interrupt(adev, &int_params,
				handle_hpd_rx_irq, (void *) aconnector))
				return -ENOMEM;
		}
	}
	return 0;
}

#if defined(CONFIG_DRM_AMD_DC_SI)
/* Register IRQ sources and initialize IRQ callbacks */
static int dce60_register_irq_handlers(struct amdgpu_device *adev)
{
	struct dc *dc = adev->dm.dc;
	struct common_irq_params *c_irq_params;
	struct dc_interrupt_params int_params = {0};
	int r;
	int i;
	unsigned int client_id = AMDGPU_IRQ_CLIENTID_LEGACY;

	int_params.requested_polarity = INTERRUPT_POLARITY_DEFAULT;
	int_params.current_polarity = INTERRUPT_POLARITY_DEFAULT;

	/*
	 * Actions of amdgpu_irq_add_id():
	 * 1. Register a set() function with base driver.
	 *    Base driver will call set() function to enable/disable an
	 *    interrupt in DC hardware.
	 * 2. Register amdgpu_dm_irq_handler().
	 *    Base driver will call amdgpu_dm_irq_handler() for ALL interrupts
	 *    coming from DC hardware.
	 *    amdgpu_dm_irq_handler() will re-direct the interrupt to DC
	 *    for acknowledging and handling.
	 */

	/* Use VBLANK interrupt */
	for (i = 0; i < adev->mode_info.num_crtc; i++) {
		r = amdgpu_irq_add_id(adev, client_id, i + 1, &adev->crtc_irq);
		if (r) {
			DRM_ERROR("Failed to add crtc irq id!\n");
			return r;
		}

		int_params.int_context = INTERRUPT_HIGH_IRQ_CONTEXT;
		int_params.irq_source =
			dc_interrupt_to_irq_source(dc, i + 1, 0);

		if (int_params.irq_source == DC_IRQ_SOURCE_INVALID ||
			int_params.irq_source  < DC_IRQ_SOURCE_VBLANK1 ||
			int_params.irq_source  > DC_IRQ_SOURCE_VBLANK6) {
			DRM_ERROR("Failed to register vblank irq!\n");
			return -EINVAL;
		}

		c_irq_params = &adev->dm.vblank_params[int_params.irq_source - DC_IRQ_SOURCE_VBLANK1];

		c_irq_params->adev = adev;
		c_irq_params->irq_src = int_params.irq_source;

		if (!amdgpu_dm_irq_register_interrupt(adev, &int_params,
			dm_crtc_high_irq, c_irq_params))
			return -ENOMEM;
	}

	/* Use GRPH_PFLIP interrupt */
	for (i = VISLANDS30_IV_SRCID_D1_GRPH_PFLIP;
			i <= VISLANDS30_IV_SRCID_D6_GRPH_PFLIP; i += 2) {
		r = amdgpu_irq_add_id(adev, client_id, i, &adev->pageflip_irq);
		if (r) {
			DRM_ERROR("Failed to add page flip irq id!\n");
			return r;
		}

		int_params.int_context = INTERRUPT_HIGH_IRQ_CONTEXT;
		int_params.irq_source =
			dc_interrupt_to_irq_source(dc, i, 0);

		if (int_params.irq_source == DC_IRQ_SOURCE_INVALID ||
			int_params.irq_source  < DC_IRQ_SOURCE_PFLIP_FIRST ||
			int_params.irq_source  > DC_IRQ_SOURCE_PFLIP_LAST) {
			DRM_ERROR("Failed to register pflip irq!\n");
			return -EINVAL;
		}

		c_irq_params = &adev->dm.pflip_params[int_params.irq_source - DC_IRQ_SOURCE_PFLIP_FIRST];

		c_irq_params->adev = adev;
		c_irq_params->irq_src = int_params.irq_source;

		if (!amdgpu_dm_irq_register_interrupt(adev, &int_params,
			dm_pflip_high_irq, c_irq_params))
			return -ENOMEM;
	}

	/* HPD */
	r = amdgpu_irq_add_id(adev, client_id,
			VISLANDS30_IV_SRCID_HOTPLUG_DETECT_A, &adev->hpd_irq);
	if (r) {
		DRM_ERROR("Failed to add hpd irq id!\n");
		return r;
	}

	r = register_hpd_handlers(adev);

	return r;
}
#endif

/* Register IRQ sources and initialize IRQ callbacks */
static int dce110_register_irq_handlers(struct amdgpu_device *adev)
{
	struct dc *dc = adev->dm.dc;
	struct common_irq_params *c_irq_params;
	struct dc_interrupt_params int_params = {0};
	int r;
	int i;
	unsigned int client_id = AMDGPU_IRQ_CLIENTID_LEGACY;

	if (adev->family >= AMDGPU_FAMILY_AI)
		client_id = SOC15_IH_CLIENTID_DCE;

	int_params.requested_polarity = INTERRUPT_POLARITY_DEFAULT;
	int_params.current_polarity = INTERRUPT_POLARITY_DEFAULT;

	/*
	 * Actions of amdgpu_irq_add_id():
	 * 1. Register a set() function with base driver.
	 *    Base driver will call set() function to enable/disable an
	 *    interrupt in DC hardware.
	 * 2. Register amdgpu_dm_irq_handler().
	 *    Base driver will call amdgpu_dm_irq_handler() for ALL interrupts
	 *    coming from DC hardware.
	 *    amdgpu_dm_irq_handler() will re-direct the interrupt to DC
	 *    for acknowledging and handling.
	 */

	/* Use VBLANK interrupt */
	for (i = VISLANDS30_IV_SRCID_D1_VERTICAL_INTERRUPT0; i <= VISLANDS30_IV_SRCID_D6_VERTICAL_INTERRUPT0; i++) {
		r = amdgpu_irq_add_id(adev, client_id, i, &adev->crtc_irq);
		if (r) {
			DRM_ERROR("Failed to add crtc irq id!\n");
			return r;
		}

		int_params.int_context = INTERRUPT_HIGH_IRQ_CONTEXT;
		int_params.irq_source =
			dc_interrupt_to_irq_source(dc, i, 0);

		if (int_params.irq_source == DC_IRQ_SOURCE_INVALID ||
			int_params.irq_source  < DC_IRQ_SOURCE_VBLANK1 ||
			int_params.irq_source  > DC_IRQ_SOURCE_VBLANK6) {
			DRM_ERROR("Failed to register vblank irq!\n");
			return -EINVAL;
		}

		c_irq_params = &adev->dm.vblank_params[int_params.irq_source - DC_IRQ_SOURCE_VBLANK1];

		c_irq_params->adev = adev;
		c_irq_params->irq_src = int_params.irq_source;

		if (!amdgpu_dm_irq_register_interrupt(adev, &int_params,
			dm_crtc_high_irq, c_irq_params))
			return -ENOMEM;
	}

	/* Use VUPDATE interrupt */
	for (i = VISLANDS30_IV_SRCID_D1_V_UPDATE_INT; i <= VISLANDS30_IV_SRCID_D6_V_UPDATE_INT; i += 2) {
		r = amdgpu_irq_add_id(adev, client_id, i, &adev->vupdate_irq);
		if (r) {
			DRM_ERROR("Failed to add vupdate irq id!\n");
			return r;
		}

		int_params.int_context = INTERRUPT_HIGH_IRQ_CONTEXT;
		int_params.irq_source =
			dc_interrupt_to_irq_source(dc, i, 0);

		if (int_params.irq_source == DC_IRQ_SOURCE_INVALID ||
			int_params.irq_source  < DC_IRQ_SOURCE_VUPDATE1 ||
			int_params.irq_source  > DC_IRQ_SOURCE_VUPDATE6) {
			DRM_ERROR("Failed to register vupdate irq!\n");
			return -EINVAL;
		}

		c_irq_params = &adev->dm.vupdate_params[int_params.irq_source - DC_IRQ_SOURCE_VUPDATE1];

		c_irq_params->adev = adev;
		c_irq_params->irq_src = int_params.irq_source;

		if (!amdgpu_dm_irq_register_interrupt(adev, &int_params,
			dm_vupdate_high_irq, c_irq_params))
			return -ENOMEM;
	}

	/* Use GRPH_PFLIP interrupt */
	for (i = VISLANDS30_IV_SRCID_D1_GRPH_PFLIP;
			i <= VISLANDS30_IV_SRCID_D6_GRPH_PFLIP; i += 2) {
		r = amdgpu_irq_add_id(adev, client_id, i, &adev->pageflip_irq);
		if (r) {
			DRM_ERROR("Failed to add page flip irq id!\n");
			return r;
		}

		int_params.int_context = INTERRUPT_HIGH_IRQ_CONTEXT;
		int_params.irq_source =
			dc_interrupt_to_irq_source(dc, i, 0);

		if (int_params.irq_source == DC_IRQ_SOURCE_INVALID ||
			int_params.irq_source  < DC_IRQ_SOURCE_PFLIP_FIRST ||
			int_params.irq_source  > DC_IRQ_SOURCE_PFLIP_LAST) {
			DRM_ERROR("Failed to register pflip irq!\n");
			return -EINVAL;
		}

		c_irq_params = &adev->dm.pflip_params[int_params.irq_source - DC_IRQ_SOURCE_PFLIP_FIRST];

		c_irq_params->adev = adev;
		c_irq_params->irq_src = int_params.irq_source;

		if (!amdgpu_dm_irq_register_interrupt(adev, &int_params,
			dm_pflip_high_irq, c_irq_params))
			return -ENOMEM;
	}

	/* HPD */
	r = amdgpu_irq_add_id(adev, client_id,
			VISLANDS30_IV_SRCID_HOTPLUG_DETECT_A, &adev->hpd_irq);
	if (r) {
		DRM_ERROR("Failed to add hpd irq id!\n");
		return r;
	}

	r = register_hpd_handlers(adev);

	return r;
}

/* Register IRQ sources and initialize IRQ callbacks */
static int dcn10_register_irq_handlers(struct amdgpu_device *adev)
{
	struct dc *dc = adev->dm.dc;
	struct common_irq_params *c_irq_params;
	struct dc_interrupt_params int_params = {0};
	int r;
	int i;
#if defined(CONFIG_DRM_AMD_SECURE_DISPLAY)
	static const unsigned int vrtl_int_srcid[] = {
		DCN_1_0__SRCID__OTG1_VERTICAL_INTERRUPT0_CONTROL,
		DCN_1_0__SRCID__OTG2_VERTICAL_INTERRUPT0_CONTROL,
		DCN_1_0__SRCID__OTG3_VERTICAL_INTERRUPT0_CONTROL,
		DCN_1_0__SRCID__OTG4_VERTICAL_INTERRUPT0_CONTROL,
		DCN_1_0__SRCID__OTG5_VERTICAL_INTERRUPT0_CONTROL,
		DCN_1_0__SRCID__OTG6_VERTICAL_INTERRUPT0_CONTROL
	};
#endif

	int_params.requested_polarity = INTERRUPT_POLARITY_DEFAULT;
	int_params.current_polarity = INTERRUPT_POLARITY_DEFAULT;

	/*
	 * Actions of amdgpu_irq_add_id():
	 * 1. Register a set() function with base driver.
	 *    Base driver will call set() function to enable/disable an
	 *    interrupt in DC hardware.
	 * 2. Register amdgpu_dm_irq_handler().
	 *    Base driver will call amdgpu_dm_irq_handler() for ALL interrupts
	 *    coming from DC hardware.
	 *    amdgpu_dm_irq_handler() will re-direct the interrupt to DC
	 *    for acknowledging and handling.
	 */

	/* Use VSTARTUP interrupt */
	for (i = DCN_1_0__SRCID__DC_D1_OTG_VSTARTUP;
			i <= DCN_1_0__SRCID__DC_D1_OTG_VSTARTUP + adev->mode_info.num_crtc - 1;
			i++) {
		r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_DCE, i, &adev->crtc_irq);

		if (r) {
			DRM_ERROR("Failed to add crtc irq id!\n");
			return r;
		}

		int_params.int_context = INTERRUPT_HIGH_IRQ_CONTEXT;
		int_params.irq_source =
			dc_interrupt_to_irq_source(dc, i, 0);

		if (int_params.irq_source == DC_IRQ_SOURCE_INVALID ||
			int_params.irq_source  < DC_IRQ_SOURCE_VBLANK1 ||
			int_params.irq_source  > DC_IRQ_SOURCE_VBLANK6) {
			DRM_ERROR("Failed to register vblank irq!\n");
			return -EINVAL;
		}

		c_irq_params = &adev->dm.vblank_params[int_params.irq_source - DC_IRQ_SOURCE_VBLANK1];

		c_irq_params->adev = adev;
		c_irq_params->irq_src = int_params.irq_source;

		if (!amdgpu_dm_irq_register_interrupt(adev, &int_params,
			dm_crtc_high_irq, c_irq_params))
			return -ENOMEM;
	}

	/* Use otg vertical line interrupt */
#if defined(CONFIG_DRM_AMD_SECURE_DISPLAY)
	for (i = 0; i <= adev->mode_info.num_crtc - 1; i++) {
		r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_DCE,
				vrtl_int_srcid[i], &adev->vline0_irq);

		if (r) {
			DRM_ERROR("Failed to add vline0 irq id!\n");
			return r;
		}

		int_params.int_context = INTERRUPT_HIGH_IRQ_CONTEXT;
		int_params.irq_source =
			dc_interrupt_to_irq_source(dc, vrtl_int_srcid[i], 0);

		if (int_params.irq_source == DC_IRQ_SOURCE_INVALID ||
			int_params.irq_source < DC_IRQ_SOURCE_DC1_VLINE0 ||
			int_params.irq_source > DC_IRQ_SOURCE_DC6_VLINE0) {
			DRM_ERROR("Failed to register vline0 irq!\n");
			return -EINVAL;
		}

		c_irq_params = &adev->dm.vline0_params[int_params.irq_source
					- DC_IRQ_SOURCE_DC1_VLINE0];

		c_irq_params->adev = adev;
		c_irq_params->irq_src = int_params.irq_source;

		if (!amdgpu_dm_irq_register_interrupt(adev, &int_params,
			dm_dcn_vertical_interrupt0_high_irq,
			c_irq_params))
			return -ENOMEM;
	}
#endif

	/* Use VUPDATE_NO_LOCK interrupt on DCN, which seems to correspond to
	 * the regular VUPDATE interrupt on DCE. We want DC_IRQ_SOURCE_VUPDATEx
	 * to trigger at end of each vblank, regardless of state of the lock,
	 * matching DCE behaviour.
	 */
	for (i = DCN_1_0__SRCID__OTG0_IHC_V_UPDATE_NO_LOCK_INTERRUPT;
	     i <= DCN_1_0__SRCID__OTG0_IHC_V_UPDATE_NO_LOCK_INTERRUPT + adev->mode_info.num_crtc - 1;
	     i++) {
		r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_DCE, i, &adev->vupdate_irq);

		if (r) {
			DRM_ERROR("Failed to add vupdate irq id!\n");
			return r;
		}

		int_params.int_context = INTERRUPT_HIGH_IRQ_CONTEXT;
		int_params.irq_source =
			dc_interrupt_to_irq_source(dc, i, 0);

		if (int_params.irq_source == DC_IRQ_SOURCE_INVALID ||
			int_params.irq_source  < DC_IRQ_SOURCE_VUPDATE1 ||
			int_params.irq_source  > DC_IRQ_SOURCE_VUPDATE6) {
			DRM_ERROR("Failed to register vupdate irq!\n");
			return -EINVAL;
		}

		c_irq_params = &adev->dm.vupdate_params[int_params.irq_source - DC_IRQ_SOURCE_VUPDATE1];

		c_irq_params->adev = adev;
		c_irq_params->irq_src = int_params.irq_source;

		if (!amdgpu_dm_irq_register_interrupt(adev, &int_params,
			dm_vupdate_high_irq, c_irq_params))
			return -ENOMEM;
	}

	/* Use GRPH_PFLIP interrupt */
	for (i = DCN_1_0__SRCID__HUBP0_FLIP_INTERRUPT;
			i <= DCN_1_0__SRCID__HUBP0_FLIP_INTERRUPT + dc->caps.max_otg_num - 1;
			i++) {
		r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_DCE, i, &adev->pageflip_irq);
		if (r) {
			DRM_ERROR("Failed to add page flip irq id!\n");
			return r;
		}

		int_params.int_context = INTERRUPT_HIGH_IRQ_CONTEXT;
		int_params.irq_source =
			dc_interrupt_to_irq_source(dc, i, 0);

		if (int_params.irq_source == DC_IRQ_SOURCE_INVALID ||
			int_params.irq_source  < DC_IRQ_SOURCE_PFLIP_FIRST ||
			int_params.irq_source  > DC_IRQ_SOURCE_PFLIP_LAST) {
			DRM_ERROR("Failed to register pflip irq!\n");
			return -EINVAL;
		}

		c_irq_params = &adev->dm.pflip_params[int_params.irq_source - DC_IRQ_SOURCE_PFLIP_FIRST];

		c_irq_params->adev = adev;
		c_irq_params->irq_src = int_params.irq_source;

		if (!amdgpu_dm_irq_register_interrupt(adev, &int_params,
			dm_pflip_high_irq, c_irq_params))
			return -ENOMEM;
	}

	/* HPD */
	r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_DCE, DCN_1_0__SRCID__DC_HPD1_INT,
			&adev->hpd_irq);
	if (r) {
		DRM_ERROR("Failed to add hpd irq id!\n");
		return r;
	}

	r = register_hpd_handlers(adev);

	return r;
}
/* Register Outbox IRQ sources and initialize IRQ callbacks */
static int register_outbox_irq_handlers(struct amdgpu_device *adev)
{
	struct dc *dc = adev->dm.dc;
	struct common_irq_params *c_irq_params;
	struct dc_interrupt_params int_params = {0};
	int r, i;

	int_params.requested_polarity = INTERRUPT_POLARITY_DEFAULT;
	int_params.current_polarity = INTERRUPT_POLARITY_DEFAULT;

	r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_DCE, DCN_1_0__SRCID__DMCUB_OUTBOX_LOW_PRIORITY_READY_INT,
			&adev->dmub_outbox_irq);
	if (r) {
		DRM_ERROR("Failed to add outbox irq id!\n");
		return r;
	}

	if (dc->ctx->dmub_srv) {
		i = DCN_1_0__SRCID__DMCUB_OUTBOX_LOW_PRIORITY_READY_INT;
		int_params.int_context = INTERRUPT_LOW_IRQ_CONTEXT;
		int_params.irq_source =
		dc_interrupt_to_irq_source(dc, i, 0);

		c_irq_params = &adev->dm.dmub_outbox_params[0];

		c_irq_params->adev = adev;
		c_irq_params->irq_src = int_params.irq_source;

		if (!amdgpu_dm_irq_register_interrupt(adev, &int_params,
			dm_dmub_outbox1_low_irq, c_irq_params))
			return -ENOMEM;
	}

	return 0;
}

/*
 * Acquires the lock for the atomic state object and returns
 * the new atomic state.
 *
 * This should only be called during atomic check.
 */
int dm_atomic_get_state(struct drm_atomic_state *state,
			struct dm_atomic_state **dm_state)
{
	struct drm_device *dev = state->dev;
	struct amdgpu_device *adev = drm_to_adev(dev);
	struct amdgpu_display_manager *dm = &adev->dm;
	struct drm_private_state *priv_state;

	if (*dm_state)
		return 0;

	priv_state = drm_atomic_get_private_obj_state(state, &dm->atomic_obj);
	if (IS_ERR(priv_state))
		return PTR_ERR(priv_state);

	*dm_state = to_dm_atomic_state(priv_state);

	return 0;
}

static struct dm_atomic_state *
dm_atomic_get_new_state(struct drm_atomic_state *state)
{
	struct drm_device *dev = state->dev;
	struct amdgpu_device *adev = drm_to_adev(dev);
	struct amdgpu_display_manager *dm = &adev->dm;
	struct drm_private_obj *obj;
	struct drm_private_state *new_obj_state;
	int i;

	for_each_new_private_obj_in_state(state, obj, new_obj_state, i) {
		if (obj->funcs == dm->atomic_obj.funcs)
			return to_dm_atomic_state(new_obj_state);
	}

	return NULL;
}

static struct drm_private_state *
dm_atomic_duplicate_state(struct drm_private_obj *obj)
{
	struct dm_atomic_state *old_state, *new_state;

	new_state = kzalloc(sizeof(*new_state), GFP_KERNEL);
	if (!new_state)
		return NULL;

	__drm_atomic_helper_private_obj_duplicate_state(obj, &new_state->base);

	old_state = to_dm_atomic_state(obj->state);

	if (old_state && old_state->context)
		new_state->context = dc_state_create_copy(old_state->context);

	if (!new_state->context) {
		kfree(new_state);
		return NULL;
	}

	return &new_state->base;
}

static void dm_atomic_destroy_state(struct drm_private_obj *obj,
				    struct drm_private_state *state)
{
	struct dm_atomic_state *dm_state = to_dm_atomic_state(state);

	if (dm_state && dm_state->context)
		dc_state_release(dm_state->context);

	kfree(dm_state);
}

static struct drm_private_state_funcs dm_atomic_state_funcs = {
	.atomic_duplicate_state = dm_atomic_duplicate_state,
	.atomic_destroy_state = dm_atomic_destroy_state,
};

static int amdgpu_dm_mode_config_init(struct amdgpu_device *adev)
{
	struct dm_atomic_state *state;
	int r;

	adev->mode_info.mode_config_initialized = true;

	adev_to_drm(adev)->mode_config.funcs = (void *)&amdgpu_dm_mode_funcs;
	adev_to_drm(adev)->mode_config.helper_private = &amdgpu_dm_mode_config_helperfuncs;

	adev_to_drm(adev)->mode_config.max_width = 16384;
	adev_to_drm(adev)->mode_config.max_height = 16384;

	adev_to_drm(adev)->mode_config.preferred_depth = 24;
	if (adev->asic_type == CHIP_HAWAII)
		/* disable prefer shadow for now due to hibernation issues */
		adev_to_drm(adev)->mode_config.prefer_shadow = 0;
	else
		adev_to_drm(adev)->mode_config.prefer_shadow = 1;
	/* indicates support for immediate flip */
	adev_to_drm(adev)->mode_config.async_page_flip = true;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	state->context = dc_state_create_current_copy(adev->dm.dc);
	if (!state->context) {
		kfree(state);
		return -ENOMEM;
	}

	drm_atomic_private_obj_init(adev_to_drm(adev),
				    &adev->dm.atomic_obj,
				    &state->base,
				    &dm_atomic_state_funcs);

	r = amdgpu_display_modeset_create_props(adev);
	if (r) {
		dc_state_release(state->context);
		kfree(state);
		return r;
	}

#ifdef AMD_PRIVATE_COLOR
	if (amdgpu_dm_create_color_properties(adev)) {
		dc_state_release(state->context);
		kfree(state);
		return -ENOMEM;
	}
#endif

	r = amdgpu_dm_audio_init(adev);
	if (r) {
		dc_state_release(state->context);
		kfree(state);
		return r;
	}

	return 0;
}

#define AMDGPU_DM_DEFAULT_MIN_BACKLIGHT 12
#define AMDGPU_DM_DEFAULT_MAX_BACKLIGHT 255
#define AMDGPU_DM_MIN_SPREAD ((AMDGPU_DM_DEFAULT_MAX_BACKLIGHT - AMDGPU_DM_DEFAULT_MIN_BACKLIGHT) / 2)
#define AUX_BL_DEFAULT_TRANSITION_TIME_MS 50

static void amdgpu_dm_update_backlight_caps(struct amdgpu_display_manager *dm,
					    int bl_idx)
{
#if defined(CONFIG_ACPI)
	struct amdgpu_dm_backlight_caps caps;

	memset(&caps, 0, sizeof(caps));

	if (dm->backlight_caps[bl_idx].caps_valid)
		return;

	amdgpu_acpi_get_backlight_caps(&caps);

	/* validate the firmware value is sane */
	if (caps.caps_valid) {
		int spread = caps.max_input_signal - caps.min_input_signal;

		if (caps.max_input_signal > AMDGPU_DM_DEFAULT_MAX_BACKLIGHT ||
		    caps.min_input_signal < 0 ||
		    spread > AMDGPU_DM_DEFAULT_MAX_BACKLIGHT ||
		    spread < AMDGPU_DM_MIN_SPREAD) {
			DRM_DEBUG_KMS("DM: Invalid backlight caps: min=%d, max=%d\n",
				      caps.min_input_signal, caps.max_input_signal);
			caps.caps_valid = false;
		}
	}

	if (caps.caps_valid) {
		dm->backlight_caps[bl_idx].caps_valid = true;
		if (caps.aux_support)
			return;
		dm->backlight_caps[bl_idx].min_input_signal = caps.min_input_signal;
		dm->backlight_caps[bl_idx].max_input_signal = caps.max_input_signal;
	} else {
		dm->backlight_caps[bl_idx].min_input_signal =
				AMDGPU_DM_DEFAULT_MIN_BACKLIGHT;
		dm->backlight_caps[bl_idx].max_input_signal =
				AMDGPU_DM_DEFAULT_MAX_BACKLIGHT;
	}
#else
	if (dm->backlight_caps[bl_idx].aux_support)
		return;

	dm->backlight_caps[bl_idx].min_input_signal = AMDGPU_DM_DEFAULT_MIN_BACKLIGHT;
	dm->backlight_caps[bl_idx].max_input_signal = AMDGPU_DM_DEFAULT_MAX_BACKLIGHT;
#endif
}

static int get_brightness_range(const struct amdgpu_dm_backlight_caps *caps,
				unsigned int *min, unsigned int *max)
{
	if (!caps)
		return 0;

	if (caps->aux_support) {
		// Firmware limits are in nits, DC API wants millinits.
		*max = 1000 * caps->aux_max_input_signal;
		*min = 1000 * caps->aux_min_input_signal;
	} else {
		// Firmware limits are 8-bit, PWM control is 16-bit.
		*max = 0x101 * caps->max_input_signal;
		*min = 0x101 * caps->min_input_signal;
	}
	return 1;
}

static u32 convert_brightness_from_user(const struct amdgpu_dm_backlight_caps *caps,
					uint32_t brightness)
{
	unsigned int min, max;

	if (!get_brightness_range(caps, &min, &max))
		return brightness;

	// Rescale 0..255 to min..max
	return min + DIV_ROUND_CLOSEST((max - min) * brightness,
				       AMDGPU_MAX_BL_LEVEL);
}

static u32 convert_brightness_to_user(const struct amdgpu_dm_backlight_caps *caps,
				      uint32_t brightness)
{
	unsigned int min, max;

	if (!get_brightness_range(caps, &min, &max))
		return brightness;

	if (brightness < min)
		return 0;
	// Rescale min..max to 0..255
	return DIV_ROUND_CLOSEST(AMDGPU_MAX_BL_LEVEL * (brightness - min),
				 max - min);
}

static void amdgpu_dm_backlight_set_level(struct amdgpu_display_manager *dm,
					 int bl_idx,
					 u32 user_brightness)
{
	struct amdgpu_dm_backlight_caps caps;
	struct dc_link *link;
	u32 brightness;
	bool rc, reallow_idle = false;

	amdgpu_dm_update_backlight_caps(dm, bl_idx);
	caps = dm->backlight_caps[bl_idx];

	dm->brightness[bl_idx] = user_brightness;
	/* update scratch register */
	if (bl_idx == 0)
		amdgpu_atombios_scratch_regs_set_backlight_level(dm->adev, dm->brightness[bl_idx]);
	brightness = convert_brightness_from_user(&caps, dm->brightness[bl_idx]);
	link = (struct dc_link *)dm->backlight_link[bl_idx];

	/* Change brightness based on AUX property */
	mutex_lock(&dm->dc_lock);
	if (dm->dc->caps.ips_support && dm->dc->ctx->dmub_srv->idle_allowed) {
		dc_allow_idle_optimizations(dm->dc, false);
		reallow_idle = true;
	}

	if (caps.aux_support) {
		rc = dc_link_set_backlight_level_nits(link, true, brightness,
						      AUX_BL_DEFAULT_TRANSITION_TIME_MS);
		if (!rc)
			DRM_DEBUG("DM: Failed to update backlight via AUX on eDP[%d]\n", bl_idx);
	} else {
		rc = dc_link_set_backlight_level(link, brightness, 0);
		if (!rc)
			DRM_DEBUG("DM: Failed to update backlight on eDP[%d]\n", bl_idx);
	}

	if (dm->dc->caps.ips_support && reallow_idle)
		dc_allow_idle_optimizations(dm->dc, true);

	mutex_unlock(&dm->dc_lock);

	if (rc)
		dm->actual_brightness[bl_idx] = user_brightness;
}

static int amdgpu_dm_backlight_update_status(struct backlight_device *bd)
{
	struct amdgpu_display_manager *dm = bl_get_data(bd);
	int i;

	for (i = 0; i < dm->num_of_edps; i++) {
		if (bd == dm->backlight_dev[i])
			break;
	}
	if (i >= AMDGPU_DM_MAX_NUM_EDP)
		i = 0;
	amdgpu_dm_backlight_set_level(dm, i, bd->props.brightness);

	return 0;
}

static u32 amdgpu_dm_backlight_get_level(struct amdgpu_display_manager *dm,
					 int bl_idx)
{
	int ret;
	struct amdgpu_dm_backlight_caps caps;
	struct dc_link *link = (struct dc_link *)dm->backlight_link[bl_idx];

	amdgpu_dm_update_backlight_caps(dm, bl_idx);
	caps = dm->backlight_caps[bl_idx];

	if (caps.aux_support) {
		u32 avg, peak;
		bool rc;

		rc = dc_link_get_backlight_level_nits(link, &avg, &peak);
		if (!rc)
			return dm->brightness[bl_idx];
		return convert_brightness_to_user(&caps, avg);
	}

	ret = dc_link_get_backlight_level(link);

	if (ret == DC_ERROR_UNEXPECTED)
		return dm->brightness[bl_idx];

	return convert_brightness_to_user(&caps, ret);
}

static int amdgpu_dm_backlight_get_brightness(struct backlight_device *bd)
{
	struct amdgpu_display_manager *dm = bl_get_data(bd);
	int i;

	for (i = 0; i < dm->num_of_edps; i++) {
		if (bd == dm->backlight_dev[i])
			break;
	}
	if (i >= AMDGPU_DM_MAX_NUM_EDP)
		i = 0;
	return amdgpu_dm_backlight_get_level(dm, i);
}

static const struct backlight_ops amdgpu_dm_backlight_ops = {
	.options = BL_CORE_SUSPENDRESUME,
	.get_brightness = amdgpu_dm_backlight_get_brightness,
	.update_status	= amdgpu_dm_backlight_update_status,
};

static void
amdgpu_dm_register_backlight_device(struct amdgpu_dm_connector *aconnector)
{
	struct drm_device *drm = aconnector->base.dev;
	struct amdgpu_display_manager *dm = &drm_to_adev(drm)->dm;
	struct backlight_properties props = { 0 };
	struct amdgpu_dm_backlight_caps caps = { 0 };
	char bl_name[16];

	if (aconnector->bl_idx == -1)
		return;

	if (!acpi_video_backlight_use_native()) {
		drm_info(drm, "Skipping amdgpu DM backlight registration\n");
		/* Try registering an ACPI video backlight device instead. */
		acpi_video_register_backlight();
		return;
	}

	amdgpu_acpi_get_backlight_caps(&caps);
#ifdef notyet
	if (caps.caps_valid) {
#else
	/*
	 * ATIF levels can be too low, on t495 ac: 100 (39%), dc: 32 (12%)
	 */
	if (0) {
#endif
		if (power_supply_is_system_supplied() > 0)
			props.brightness = caps.ac_level;
		else
			props.brightness = caps.dc_level;
	} else
		props.brightness = AMDGPU_MAX_BL_LEVEL;

	props.max_brightness = AMDGPU_MAX_BL_LEVEL;
	props.type = BACKLIGHT_RAW;

	snprintf(bl_name, sizeof(bl_name), "amdgpu_bl%d",
		 drm->primary->index + aconnector->bl_idx);

	dm->backlight_dev[aconnector->bl_idx] =
		backlight_device_register(bl_name, aconnector->base.kdev, dm,
					  &amdgpu_dm_backlight_ops, &props);
	dm->brightness[aconnector->bl_idx] = props.brightness;

	if (IS_ERR(dm->backlight_dev[aconnector->bl_idx])) {
		DRM_ERROR("DM: Backlight registration failed!\n");
		dm->backlight_dev[aconnector->bl_idx] = NULL;
	} else
		DRM_DEBUG_DRIVER("DM: Registered Backlight device: %s\n", bl_name);
}

static int initialize_plane(struct amdgpu_display_manager *dm,
			    struct amdgpu_mode_info *mode_info, int plane_id,
			    enum drm_plane_type plane_type,
			    const struct dc_plane_cap *plane_cap)
{
	struct drm_plane *plane;
	unsigned long possible_crtcs;
	int ret = 0;

	plane = kzalloc(sizeof(struct drm_plane), GFP_KERNEL);
	if (!plane) {
		DRM_ERROR("KMS: Failed to allocate plane\n");
		return -ENOMEM;
	}
	plane->type = plane_type;

	/*
	 * HACK: IGT tests expect that the primary plane for a CRTC
	 * can only have one possible CRTC. Only expose support for
	 * any CRTC if they're not going to be used as a primary plane
	 * for a CRTC - like overlay or underlay planes.
	 */
	possible_crtcs = 1 << plane_id;
	if (plane_id >= dm->dc->caps.max_streams)
		possible_crtcs = 0xff;

	ret = amdgpu_dm_plane_init(dm, plane, possible_crtcs, plane_cap);

	if (ret) {
		DRM_ERROR("KMS: Failed to initialize plane\n");
		kfree(plane);
		return ret;
	}

	if (mode_info)
		mode_info->planes[plane_id] = plane;

	return ret;
}


static void setup_backlight_device(struct amdgpu_display_manager *dm,
				   struct amdgpu_dm_connector *aconnector)
{
	struct dc_link *link = aconnector->dc_link;
	int bl_idx = dm->num_of_edps;

	if (!(link->connector_signal & (SIGNAL_TYPE_EDP | SIGNAL_TYPE_LVDS)) ||
	    link->type == dc_connection_none)
		return;

	if (dm->num_of_edps >= AMDGPU_DM_MAX_NUM_EDP) {
		drm_warn(adev_to_drm(dm->adev), "Too much eDP connections, skipping backlight setup for additional eDPs\n");
		return;
	}

	aconnector->bl_idx = bl_idx;

	amdgpu_dm_update_backlight_caps(dm, bl_idx);
	dm->backlight_link[bl_idx] = link;
	dm->num_of_edps++;

	update_connector_ext_caps(aconnector);
}

static void amdgpu_set_panel_orientation(struct drm_connector *connector);

/*
 * In this architecture, the association
 * connector -> encoder -> crtc
 * id not really requried. The crtc and connector will hold the
 * display_index as an abstraction to use with DAL component
 *
 * Returns 0 on success
 */
static int amdgpu_dm_initialize_drm_device(struct amdgpu_device *adev)
{
	struct amdgpu_display_manager *dm = &adev->dm;
	s32 i;
	struct amdgpu_dm_connector *aconnector = NULL;
	struct amdgpu_encoder *aencoder = NULL;
	struct amdgpu_mode_info *mode_info = &adev->mode_info;
	u32 link_cnt;
	s32 primary_planes;
	enum dc_connection_type new_connection_type = dc_connection_none;
	const struct dc_plane_cap *plane;
	bool psr_feature_enabled = false;
	bool replay_feature_enabled = false;
	int max_overlay = dm->dc->caps.max_slave_planes;

	dm->display_indexes_num = dm->dc->caps.max_streams;
	/* Update the actual used number of crtc */
	adev->mode_info.num_crtc = adev->dm.display_indexes_num;

	amdgpu_dm_set_irq_funcs(adev);

	link_cnt = dm->dc->caps.max_links;
	if (amdgpu_dm_mode_config_init(dm->adev)) {
		DRM_ERROR("DM: Failed to initialize mode config\n");
		return -EINVAL;
	}

	/* There is one primary plane per CRTC */
	primary_planes = dm->dc->caps.max_streams;
	if (primary_planes > AMDGPU_MAX_PLANES) {
		DRM_ERROR("DM: Plane nums out of 6 planes\n");
		return -EINVAL;
	}

	/*
	 * Initialize primary planes, implicit planes for legacy IOCTLS.
	 * Order is reversed to match iteration order in atomic check.
	 */
	for (i = (primary_planes - 1); i >= 0; i--) {
		plane = &dm->dc->caps.planes[i];

		if (initialize_plane(dm, mode_info, i,
				     DRM_PLANE_TYPE_PRIMARY, plane)) {
			DRM_ERROR("KMS: Failed to initialize primary plane\n");
			goto fail;
		}
	}

	/*
	 * Initialize overlay planes, index starting after primary planes.
	 * These planes have a higher DRM index than the primary planes since
	 * they should be considered as having a higher z-order.
	 * Order is reversed to match iteration order in atomic check.
	 *
	 * Only support DCN for now, and only expose one so we don't encourage
	 * userspace to use up all the pipes.
	 */
	for (i = 0; i < dm->dc->caps.max_planes; ++i) {
		struct dc_plane_cap *plane = &dm->dc->caps.planes[i];

		/* Do not create overlay if MPO disabled */
		if (amdgpu_dc_debug_mask & DC_DISABLE_MPO)
			break;

		if (plane->type != DC_PLANE_TYPE_DCN_UNIVERSAL)
			continue;

		if (!plane->pixel_format_support.argb8888)
			continue;

		if (max_overlay-- == 0)
			break;

		if (initialize_plane(dm, NULL, primary_planes + i,
				     DRM_PLANE_TYPE_OVERLAY, plane)) {
			DRM_ERROR("KMS: Failed to initialize overlay plane\n");
			goto fail;
		}
	}

	for (i = 0; i < dm->dc->caps.max_streams; i++)
		if (amdgpu_dm_crtc_init(dm, mode_info->planes[i], i)) {
			DRM_ERROR("KMS: Failed to initialize crtc\n");
			goto fail;
		}

	/* Use Outbox interrupt */
	switch (amdgpu_ip_version(adev, DCE_HWIP, 0)) {
	case IP_VERSION(3, 0, 0):
	case IP_VERSION(3, 1, 2):
	case IP_VERSION(3, 1, 3):
	case IP_VERSION(3, 1, 4):
	case IP_VERSION(3, 1, 5):
	case IP_VERSION(3, 1, 6):
	case IP_VERSION(3, 2, 0):
	case IP_VERSION(3, 2, 1):
	case IP_VERSION(2, 1, 0):
	case IP_VERSION(3, 5, 0):
	case IP_VERSION(3, 5, 1):
	case IP_VERSION(4, 0, 1):
		if (register_outbox_irq_handlers(dm->adev)) {
			DRM_ERROR("DM: Failed to initialize IRQ\n");
			goto fail;
		}
		break;
	default:
		DRM_DEBUG_KMS("Unsupported DCN IP version for outbox: 0x%X\n",
			      amdgpu_ip_version(adev, DCE_HWIP, 0));
	}

	/* Determine whether to enable PSR support by default. */
	if (!(amdgpu_dc_debug_mask & DC_DISABLE_PSR)) {
		switch (amdgpu_ip_version(adev, DCE_HWIP, 0)) {
		case IP_VERSION(3, 1, 2):
		case IP_VERSION(3, 1, 3):
		case IP_VERSION(3, 1, 4):
		case IP_VERSION(3, 1, 5):
		case IP_VERSION(3, 1, 6):
		case IP_VERSION(3, 2, 0):
		case IP_VERSION(3, 2, 1):
		case IP_VERSION(3, 5, 0):
		case IP_VERSION(3, 5, 1):
		case IP_VERSION(4, 0, 1):
			psr_feature_enabled = true;
			break;
		default:
			psr_feature_enabled = amdgpu_dc_feature_mask & DC_PSR_MASK;
			break;
		}
	}

	/* Determine whether to enable Replay support by default. */
	if (!(amdgpu_dc_debug_mask & DC_DISABLE_REPLAY)) {
		switch (amdgpu_ip_version(adev, DCE_HWIP, 0)) {
		case IP_VERSION(3, 1, 4):
		case IP_VERSION(3, 2, 0):
		case IP_VERSION(3, 2, 1):
		case IP_VERSION(3, 5, 0):
		case IP_VERSION(3, 5, 1):
			replay_feature_enabled = true;
			break;

		default:
			replay_feature_enabled = amdgpu_dc_feature_mask & DC_REPLAY_MASK;
			break;
		}
	}

	if (link_cnt > MAX_LINKS) {
		DRM_ERROR(
			"KMS: Cannot support more than %d display indexes\n",
				MAX_LINKS);
		goto fail;
	}

	/* loops over all connectors on the board */
	for (i = 0; i < link_cnt; i++) {
		struct dc_link *link = NULL;

		link = dc_get_link_at_index(dm->dc, i);

		if (link->connector_signal == SIGNAL_TYPE_VIRTUAL) {
		/* XXX writeback connector functions not implemented */
#ifdef notyet
			struct amdgpu_dm_wb_connector *wbcon = kzalloc(sizeof(*wbcon), GFP_KERNEL);

			if (!wbcon) {
				DRM_ERROR("KMS: Failed to allocate writeback connector\n");
				continue;
			}

			if (amdgpu_dm_wb_connector_init(dm, wbcon, i)) {
				DRM_ERROR("KMS: Failed to initialize writeback connector\n");
				kfree(wbcon);
				continue;
			}

			link->psr_settings.psr_feature_enabled = false;
			link->psr_settings.psr_version = DC_PSR_VERSION_UNSUPPORTED;
#endif

			continue;
		}

		aconnector = kzalloc(sizeof(*aconnector), GFP_KERNEL);
		if (!aconnector)
			goto fail;

		aencoder = kzalloc(sizeof(*aencoder), GFP_KERNEL);
		if (!aencoder)
			goto fail;

		if (amdgpu_dm_encoder_init(dm->ddev, aencoder, i)) {
			DRM_ERROR("KMS: Failed to initialize encoder\n");
			goto fail;
		}

		if (amdgpu_dm_connector_init(dm, aconnector, i, aencoder)) {
			DRM_ERROR("KMS: Failed to initialize connector\n");
			goto fail;
		}

		if (dm->hpd_rx_offload_wq)
			dm->hpd_rx_offload_wq[aconnector->base.index].aconnector =
				aconnector;

		if (!dc_link_detect_connection_type(link, &new_connection_type))
			DRM_ERROR("KMS: Failed to detect connector\n");

		if (aconnector->base.force && new_connection_type == dc_connection_none) {
			emulated_link_detect(link);
			amdgpu_dm_update_connector_after_detect(aconnector);
		} else {
			bool ret = false;

			mutex_lock(&dm->dc_lock);
			dc_exit_ips_for_hw_access(dm->dc);
			ret = dc_link_detect(link, DETECT_REASON_BOOT);
			mutex_unlock(&dm->dc_lock);

			if (ret) {
				amdgpu_dm_update_connector_after_detect(aconnector);
				setup_backlight_device(dm, aconnector);

				/* Disable PSR if Replay can be enabled */
				if (replay_feature_enabled)
					if (amdgpu_dm_set_replay_caps(link, aconnector))
						psr_feature_enabled = false;

				if (psr_feature_enabled)
					amdgpu_dm_set_psr_caps(link);
			}
		}
		amdgpu_set_panel_orientation(&aconnector->base);
	}

	/* Software is initialized. Now we can register interrupt handlers. */
	switch (adev->asic_type) {
#if defined(CONFIG_DRM_AMD_DC_SI)
	case CHIP_TAHITI:
	case CHIP_PITCAIRN:
	case CHIP_VERDE:
	case CHIP_OLAND:
		if (dce60_register_irq_handlers(dm->adev)) {
			DRM_ERROR("DM: Failed to initialize IRQ\n");
			goto fail;
		}
		break;
#endif
	case CHIP_BONAIRE:
	case CHIP_HAWAII:
	case CHIP_KAVERI:
	case CHIP_KABINI:
	case CHIP_MULLINS:
	case CHIP_TONGA:
	case CHIP_FIJI:
	case CHIP_CARRIZO:
	case CHIP_STONEY:
	case CHIP_POLARIS11:
	case CHIP_POLARIS10:
	case CHIP_POLARIS12:
	case CHIP_VEGAM:
	case CHIP_VEGA10:
	case CHIP_VEGA12:
	case CHIP_VEGA20:
		if (dce110_register_irq_handlers(dm->adev)) {
			DRM_ERROR("DM: Failed to initialize IRQ\n");
			goto fail;
		}
		break;
	default:
		switch (amdgpu_ip_version(adev, DCE_HWIP, 0)) {
		case IP_VERSION(1, 0, 0):
		case IP_VERSION(1, 0, 1):
		case IP_VERSION(2, 0, 2):
		case IP_VERSION(2, 0, 3):
		case IP_VERSION(2, 0, 0):
		case IP_VERSION(2, 1, 0):
		case IP_VERSION(3, 0, 0):
		case IP_VERSION(3, 0, 2):
		case IP_VERSION(3, 0, 3):
		case IP_VERSION(3, 0, 1):
		case IP_VERSION(3, 1, 2):
		case IP_VERSION(3, 1, 3):
		case IP_VERSION(3, 1, 4):
		case IP_VERSION(3, 1, 5):
		case IP_VERSION(3, 1, 6):
		case IP_VERSION(3, 2, 0):
		case IP_VERSION(3, 2, 1):
		case IP_VERSION(3, 5, 0):
		case IP_VERSION(3, 5, 1):
		case IP_VERSION(4, 0, 1):
			if (dcn10_register_irq_handlers(dm->adev)) {
				DRM_ERROR("DM: Failed to initialize IRQ\n");
				goto fail;
			}
			break;
		default:
			DRM_ERROR("Unsupported DCE IP versions: 0x%X\n",
					amdgpu_ip_version(adev, DCE_HWIP, 0));
			goto fail;
		}
		break;
	}

	return 0;
fail:
	kfree(aencoder);
	kfree(aconnector);

	return -EINVAL;
}

static void amdgpu_dm_destroy_drm_device(struct amdgpu_display_manager *dm)
{
	if (dm->atomic_obj.state)
		drm_atomic_private_obj_fini(&dm->atomic_obj);
}

/******************************************************************************
 * amdgpu_display_funcs functions
 *****************************************************************************/

/*
 * dm_bandwidth_update - program display watermarks
 *
 * @adev: amdgpu_device pointer
 *
 * Calculate and program the display watermarks and line buffer allocation.
 */
static void dm_bandwidth_update(struct amdgpu_device *adev)
{
	/* TODO: implement later */
}

static const struct amdgpu_display_funcs dm_display_funcs = {
	.bandwidth_update = dm_bandwidth_update, /* called unconditionally */
	.vblank_get_counter = dm_vblank_get_counter,/* called unconditionally */
	.backlight_set_level = NULL, /* never called for DC */
	.backlight_get_level = NULL, /* never called for DC */
	.hpd_sense = NULL,/* called unconditionally */
	.hpd_set_polarity = NULL, /* called unconditionally */
	.hpd_get_gpio_reg = NULL, /* VBIOS parsing. DAL does it. */
	.page_flip_get_scanoutpos =
		dm_crtc_get_scanoutpos,/* called unconditionally */
	.add_encoder = NULL, /* VBIOS parsing. DAL does it. */
	.add_connector = NULL, /* VBIOS parsing. DAL does it. */
};

#if defined(CONFIG_DEBUG_KERNEL_DC)

static ssize_t s3_debug_store(struct device *device,
			      struct device_attribute *attr,
			      const char *buf,
			      size_t count)
{
	int ret;
	int s3_state;
	struct drm_device *drm_dev = dev_get_drvdata(device);
	struct amdgpu_device *adev = drm_to_adev(drm_dev);

	ret = kstrtoint(buf, 0, &s3_state);

	if (ret == 0) {
		if (s3_state) {
			dm_resume(adev);
			drm_kms_helper_hotplug_event(adev_to_drm(adev));
		} else
			dm_suspend(adev);
	}

	return ret == 0 ? count : 0;
}

DEVICE_ATTR_WO(s3_debug);

#endif

static int dm_init_microcode(struct amdgpu_device *adev)
{
	char *fw_name_dmub;
	int r;

	switch (amdgpu_ip_version(adev, DCE_HWIP, 0)) {
	case IP_VERSION(2, 1, 0):
		fw_name_dmub = FIRMWARE_RENOIR_DMUB;
		if (ASICREV_IS_GREEN_SARDINE(adev->external_rev_id))
			fw_name_dmub = FIRMWARE_GREEN_SARDINE_DMUB;
		break;
	case IP_VERSION(3, 0, 0):
		if (amdgpu_ip_version(adev, GC_HWIP, 0) == IP_VERSION(10, 3, 0))
			fw_name_dmub = FIRMWARE_SIENNA_CICHLID_DMUB;
		else
			fw_name_dmub = FIRMWARE_NAVY_FLOUNDER_DMUB;
		break;
	case IP_VERSION(3, 0, 1):
		fw_name_dmub = FIRMWARE_VANGOGH_DMUB;
		break;
	case IP_VERSION(3, 0, 2):
		fw_name_dmub = FIRMWARE_DIMGREY_CAVEFISH_DMUB;
		break;
	case IP_VERSION(3, 0, 3):
		fw_name_dmub = FIRMWARE_BEIGE_GOBY_DMUB;
		break;
	case IP_VERSION(3, 1, 2):
	case IP_VERSION(3, 1, 3):
		fw_name_dmub = FIRMWARE_YELLOW_CARP_DMUB;
		break;
	case IP_VERSION(3, 1, 4):
		fw_name_dmub = FIRMWARE_DCN_314_DMUB;
		break;
	case IP_VERSION(3, 1, 5):
		fw_name_dmub = FIRMWARE_DCN_315_DMUB;
		break;
	case IP_VERSION(3, 1, 6):
		fw_name_dmub = FIRMWARE_DCN316_DMUB;
		break;
	case IP_VERSION(3, 2, 0):
		fw_name_dmub = FIRMWARE_DCN_V3_2_0_DMCUB;
		break;
	case IP_VERSION(3, 2, 1):
		fw_name_dmub = FIRMWARE_DCN_V3_2_1_DMCUB;
		break;
	case IP_VERSION(3, 5, 0):
		fw_name_dmub = FIRMWARE_DCN_35_DMUB;
		break;
	case IP_VERSION(3, 5, 1):
		fw_name_dmub = FIRMWARE_DCN_351_DMUB;
		break;
	case IP_VERSION(4, 0, 1):
		fw_name_dmub = FIRMWARE_DCN_401_DMUB;
		break;
	default:
		/* ASIC doesn't support DMUB. */
		return 0;
	}
	r = amdgpu_ucode_request(adev, &adev->dm.dmub_fw, "%s", fw_name_dmub);
	return r;
}

static int dm_early_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct amdgpu_mode_info *mode_info = &adev->mode_info;
	struct atom_context *ctx = mode_info->atom_context;
	int index = GetIndexIntoMasterTable(DATA, Object_Header);
	u16 data_offset;

	/* if there is no object header, skip DM */
	if (!amdgpu_atom_parse_data_header(ctx, index, NULL, NULL, NULL, &data_offset)) {
		adev->harvest_ip_mask |= AMD_HARVEST_IP_DMU_MASK;
		dev_info(adev->dev, "No object header, skipping DM\n");
		return -ENOENT;
	}

	switch (adev->asic_type) {
#if defined(CONFIG_DRM_AMD_DC_SI)
	case CHIP_TAHITI:
	case CHIP_PITCAIRN:
	case CHIP_VERDE:
		adev->mode_info.num_crtc = 6;
		adev->mode_info.num_hpd = 6;
		adev->mode_info.num_dig = 6;
		break;
	case CHIP_OLAND:
		adev->mode_info.num_crtc = 2;
		adev->mode_info.num_hpd = 2;
		adev->mode_info.num_dig = 2;
		break;
#endif
	case CHIP_BONAIRE:
	case CHIP_HAWAII:
		adev->mode_info.num_crtc = 6;
		adev->mode_info.num_hpd = 6;
		adev->mode_info.num_dig = 6;
		break;
	case CHIP_KAVERI:
		adev->mode_info.num_crtc = 4;
		adev->mode_info.num_hpd = 6;
		adev->mode_info.num_dig = 7;
		break;
	case CHIP_KABINI:
	case CHIP_MULLINS:
		adev->mode_info.num_crtc = 2;
		adev->mode_info.num_hpd = 6;
		adev->mode_info.num_dig = 6;
		break;
	case CHIP_FIJI:
	case CHIP_TONGA:
		adev->mode_info.num_crtc = 6;
		adev->mode_info.num_hpd = 6;
		adev->mode_info.num_dig = 7;
		break;
	case CHIP_CARRIZO:
		adev->mode_info.num_crtc = 3;
		adev->mode_info.num_hpd = 6;
		adev->mode_info.num_dig = 9;
		break;
	case CHIP_STONEY:
		adev->mode_info.num_crtc = 2;
		adev->mode_info.num_hpd = 6;
		adev->mode_info.num_dig = 9;
		break;
	case CHIP_POLARIS11:
	case CHIP_POLARIS12:
		adev->mode_info.num_crtc = 5;
		adev->mode_info.num_hpd = 5;
		adev->mode_info.num_dig = 5;
		break;
	case CHIP_POLARIS10:
	case CHIP_VEGAM:
		adev->mode_info.num_crtc = 6;
		adev->mode_info.num_hpd = 6;
		adev->mode_info.num_dig = 6;
		break;
	case CHIP_VEGA10:
	case CHIP_VEGA12:
	case CHIP_VEGA20:
		adev->mode_info.num_crtc = 6;
		adev->mode_info.num_hpd = 6;
		adev->mode_info.num_dig = 6;
		break;
	default:

		switch (amdgpu_ip_version(adev, DCE_HWIP, 0)) {
		case IP_VERSION(2, 0, 2):
		case IP_VERSION(3, 0, 0):
			adev->mode_info.num_crtc = 6;
			adev->mode_info.num_hpd = 6;
			adev->mode_info.num_dig = 6;
			break;
		case IP_VERSION(2, 0, 0):
		case IP_VERSION(3, 0, 2):
			adev->mode_info.num_crtc = 5;
			adev->mode_info.num_hpd = 5;
			adev->mode_info.num_dig = 5;
			break;
		case IP_VERSION(2, 0, 3):
		case IP_VERSION(3, 0, 3):
			adev->mode_info.num_crtc = 2;
			adev->mode_info.num_hpd = 2;
			adev->mode_info.num_dig = 2;
			break;
		case IP_VERSION(1, 0, 0):
		case IP_VERSION(1, 0, 1):
		case IP_VERSION(3, 0, 1):
		case IP_VERSION(2, 1, 0):
		case IP_VERSION(3, 1, 2):
		case IP_VERSION(3, 1, 3):
		case IP_VERSION(3, 1, 4):
		case IP_VERSION(3, 1, 5):
		case IP_VERSION(3, 1, 6):
		case IP_VERSION(3, 2, 0):
		case IP_VERSION(3, 2, 1):
		case IP_VERSION(3, 5, 0):
		case IP_VERSION(3, 5, 1):
		case IP_VERSION(4, 0, 1):
			adev->mode_info.num_crtc = 4;
			adev->mode_info.num_hpd = 4;
			adev->mode_info.num_dig = 4;
			break;
		default:
			DRM_ERROR("Unsupported DCE IP versions: 0x%x\n",
					amdgpu_ip_version(adev, DCE_HWIP, 0));
			return -EINVAL;
		}
		break;
	}

	if (adev->mode_info.funcs == NULL)
		adev->mode_info.funcs = &dm_display_funcs;

	/*
	 * Note: Do NOT change adev->audio_endpt_rreg and
	 * adev->audio_endpt_wreg because they are initialised in
	 * amdgpu_device_init()
	 */
#if defined(CONFIG_DEBUG_KERNEL_DC)
	device_create_file(
		adev_to_drm(adev)->dev,
		&dev_attr_s3_debug);
#endif
	adev->dc_enabled = true;

	return dm_init_microcode(adev);
}

static bool modereset_required(struct drm_crtc_state *crtc_state)
{
	return !crtc_state->active && drm_atomic_crtc_needs_modeset(crtc_state);
}

static void amdgpu_dm_encoder_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
	kfree(encoder);
}

static const struct drm_encoder_funcs amdgpu_dm_encoder_funcs = {
	.destroy = amdgpu_dm_encoder_destroy,
};

static int
fill_plane_color_attributes(const struct drm_plane_state *plane_state,
			    const enum surface_pixel_format format,
			    enum dc_color_space *color_space)
{
	bool full_range;

	*color_space = COLOR_SPACE_SRGB;

	/* DRM color properties only affect non-RGB formats. */
	if (format < SURFACE_PIXEL_FORMAT_VIDEO_BEGIN)
		return 0;

	full_range = (plane_state->color_range == DRM_COLOR_YCBCR_FULL_RANGE);

	switch (plane_state->color_encoding) {
	case DRM_COLOR_YCBCR_BT601:
		if (full_range)
			*color_space = COLOR_SPACE_YCBCR601;
		else
			*color_space = COLOR_SPACE_YCBCR601_LIMITED;
		break;

	case DRM_COLOR_YCBCR_BT709:
		if (full_range)
			*color_space = COLOR_SPACE_YCBCR709;
		else
			*color_space = COLOR_SPACE_YCBCR709_LIMITED;
		break;

	case DRM_COLOR_YCBCR_BT2020:
		if (full_range)
			*color_space = COLOR_SPACE_2020_YCBCR_FULL;
		else
			*color_space = COLOR_SPACE_2020_YCBCR_LIMITED;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int
fill_dc_plane_info_and_addr(struct amdgpu_device *adev,
			    const struct drm_plane_state *plane_state,
			    const u64 tiling_flags,
			    struct dc_plane_info *plane_info,
			    struct dc_plane_address *address,
			    bool tmz_surface)
{
	const struct drm_framebuffer *fb = plane_state->fb;
	const struct amdgpu_framebuffer *afb =
		to_amdgpu_framebuffer(plane_state->fb);
	int ret;

	memset(plane_info, 0, sizeof(*plane_info));

	switch (fb->format->format) {
	case DRM_FORMAT_C8:
		plane_info->format =
			SURFACE_PIXEL_FORMAT_GRPH_PALETA_256_COLORS;
		break;
	case DRM_FORMAT_RGB565:
		plane_info->format = SURFACE_PIXEL_FORMAT_GRPH_RGB565;
		break;
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_ARGB8888:
		plane_info->format = SURFACE_PIXEL_FORMAT_GRPH_ARGB8888;
		break;
	case DRM_FORMAT_XRGB2101010:
	case DRM_FORMAT_ARGB2101010:
		plane_info->format = SURFACE_PIXEL_FORMAT_GRPH_ARGB2101010;
		break;
	case DRM_FORMAT_XBGR2101010:
	case DRM_FORMAT_ABGR2101010:
		plane_info->format = SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010;
		break;
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_ABGR8888:
		plane_info->format = SURFACE_PIXEL_FORMAT_GRPH_ABGR8888;
		break;
	case DRM_FORMAT_NV21:
		plane_info->format = SURFACE_PIXEL_FORMAT_VIDEO_420_YCbCr;
		break;
	case DRM_FORMAT_NV12:
		plane_info->format = SURFACE_PIXEL_FORMAT_VIDEO_420_YCrCb;
		break;
	case DRM_FORMAT_P010:
		plane_info->format = SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCrCb;
		break;
	case DRM_FORMAT_XRGB16161616F:
	case DRM_FORMAT_ARGB16161616F:
		plane_info->format = SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616F;
		break;
	case DRM_FORMAT_XBGR16161616F:
	case DRM_FORMAT_ABGR16161616F:
		plane_info->format = SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616F;
		break;
	case DRM_FORMAT_XRGB16161616:
	case DRM_FORMAT_ARGB16161616:
		plane_info->format = SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616;
		break;
	case DRM_FORMAT_XBGR16161616:
	case DRM_FORMAT_ABGR16161616:
		plane_info->format = SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616;
		break;
	default:
		DRM_ERROR(
			"Unsupported screen format %p4cc\n",
			&fb->format->format);
		return -EINVAL;
	}

	switch (plane_state->rotation & DRM_MODE_ROTATE_MASK) {
	case DRM_MODE_ROTATE_0:
		plane_info->rotation = ROTATION_ANGLE_0;
		break;
	case DRM_MODE_ROTATE_90:
		plane_info->rotation = ROTATION_ANGLE_90;
		break;
	case DRM_MODE_ROTATE_180:
		plane_info->rotation = ROTATION_ANGLE_180;
		break;
	case DRM_MODE_ROTATE_270:
		plane_info->rotation = ROTATION_ANGLE_270;
		break;
	default:
		plane_info->rotation = ROTATION_ANGLE_0;
		break;
	}


	plane_info->visible = true;
	plane_info->stereo_format = PLANE_STEREO_FORMAT_NONE;

	plane_info->layer_index = plane_state->normalized_zpos;

	ret = fill_plane_color_attributes(plane_state, plane_info->format,
					  &plane_info->color_space);
	if (ret)
		return ret;

	ret = amdgpu_dm_plane_fill_plane_buffer_attributes(adev, afb, plane_info->format,
					   plane_info->rotation, tiling_flags,
					   &plane_info->tiling_info,
					   &plane_info->plane_size,
					   &plane_info->dcc, address,
					   tmz_surface);
	if (ret)
		return ret;

	amdgpu_dm_plane_fill_blending_from_plane_state(
		plane_state, &plane_info->per_pixel_alpha, &plane_info->pre_multiplied_alpha,
		&plane_info->global_alpha, &plane_info->global_alpha_value);

	return 0;
}

static int fill_dc_plane_attributes(struct amdgpu_device *adev,
				    struct dc_plane_state *dc_plane_state,
				    struct drm_plane_state *plane_state,
				    struct drm_crtc_state *crtc_state)
{
	struct dm_crtc_state *dm_crtc_state = to_dm_crtc_state(crtc_state);
	struct amdgpu_framebuffer *afb = (struct amdgpu_framebuffer *)plane_state->fb;
	struct dc_scaling_info scaling_info;
	struct dc_plane_info plane_info;
	int ret;

	ret = amdgpu_dm_plane_fill_dc_scaling_info(adev, plane_state, &scaling_info);
	if (ret)
		return ret;

	dc_plane_state->src_rect = scaling_info.src_rect;
	dc_plane_state->dst_rect = scaling_info.dst_rect;
	dc_plane_state->clip_rect = scaling_info.clip_rect;
	dc_plane_state->scaling_quality = scaling_info.scaling_quality;

	ret = fill_dc_plane_info_and_addr(adev, plane_state,
					  afb->tiling_flags,
					  &plane_info,
					  &dc_plane_state->address,
					  afb->tmz_surface);
	if (ret)
		return ret;

	dc_plane_state->format = plane_info.format;
	dc_plane_state->color_space = plane_info.color_space;
	dc_plane_state->format = plane_info.format;
	dc_plane_state->plane_size = plane_info.plane_size;
	dc_plane_state->rotation = plane_info.rotation;
	dc_plane_state->horizontal_mirror = plane_info.horizontal_mirror;
	dc_plane_state->stereo_format = plane_info.stereo_format;
	dc_plane_state->tiling_info = plane_info.tiling_info;
	dc_plane_state->visible = plane_info.visible;
	dc_plane_state->per_pixel_alpha = plane_info.per_pixel_alpha;
	dc_plane_state->pre_multiplied_alpha = plane_info.pre_multiplied_alpha;
	dc_plane_state->global_alpha = plane_info.global_alpha;
	dc_plane_state->global_alpha_value = plane_info.global_alpha_value;
	dc_plane_state->dcc = plane_info.dcc;
	dc_plane_state->layer_index = plane_info.layer_index;
	dc_plane_state->flip_int_enabled = true;

	/*
	 * Always set input transfer function, since plane state is refreshed
	 * every time.
	 */
	ret = amdgpu_dm_update_plane_color_mgmt(dm_crtc_state,
						plane_state,
						dc_plane_state);
	if (ret)
		return ret;

	return 0;
}

static inline void fill_dc_dirty_rect(struct drm_plane *plane,
				      struct rect *dirty_rect, int32_t x,
				      s32 y, s32 width, s32 height,
				      int *i, bool ffu)
{
	WARN_ON(*i >= DC_MAX_DIRTY_RECTS);

	dirty_rect->x = x;
	dirty_rect->y = y;
	dirty_rect->width = width;
	dirty_rect->height = height;

	if (ffu)
		drm_dbg(plane->dev,
			"[PLANE:%d] PSR FFU dirty rect size (%d, %d)\n",
			plane->base.id, width, height);
	else
		drm_dbg(plane->dev,
			"[PLANE:%d] PSR SU dirty rect at (%d, %d) size (%d, %d)",
			plane->base.id, x, y, width, height);

	(*i)++;
}

/**
 * fill_dc_dirty_rects() - Fill DC dirty regions for PSR selective updates
 *
 * @plane: DRM plane containing dirty regions that need to be flushed to the eDP
 *         remote fb
 * @old_plane_state: Old state of @plane
 * @new_plane_state: New state of @plane
 * @crtc_state: New state of CRTC connected to the @plane
 * @flip_addrs: DC flip tracking struct, which also tracts dirty rects
 * @is_psr_su: Flag indicating whether Panel Self Refresh Selective Update (PSR SU) is enabled.
 *             If PSR SU is enabled and damage clips are available, only the regions of the screen
 *             that have changed will be updated. If PSR SU is not enabled,
 *             or if damage clips are not available, the entire screen will be updated.
 * @dirty_regions_changed: dirty regions changed
 *
 * For PSR SU, DC informs the DMUB uController of dirty rectangle regions
 * (referred to as "damage clips" in DRM nomenclature) that require updating on
 * the eDP remote buffer. The responsibility of specifying the dirty regions is
 * amdgpu_dm's.
 *
 * A damage-aware DRM client should fill the FB_DAMAGE_CLIPS property on the
 * plane with regions that require flushing to the eDP remote buffer. In
 * addition, certain use cases - such as cursor and multi-plane overlay (MPO) -
 * implicitly provide damage clips without any client support via the plane
 * bounds.
 */
static void fill_dc_dirty_rects(struct drm_plane *plane,
				struct drm_plane_state *old_plane_state,
				struct drm_plane_state *new_plane_state,
				struct drm_crtc_state *crtc_state,
				struct dc_flip_addrs *flip_addrs,
				bool is_psr_su,
				bool *dirty_regions_changed)
{
	struct dm_crtc_state *dm_crtc_state = to_dm_crtc_state(crtc_state);
	struct rect *dirty_rects = flip_addrs->dirty_rects;
	u32 num_clips;
	struct drm_mode_rect *clips;
	bool bb_changed;
	bool fb_changed;
	u32 i = 0;
	*dirty_regions_changed = false;

	/*
	 * Cursor plane has it's own dirty rect update interface. See
	 * dcn10_dmub_update_cursor_data and dmub_cmd_update_cursor_info_data
	 */
	if (plane->type == DRM_PLANE_TYPE_CURSOR)
		return;

	if (new_plane_state->rotation != DRM_MODE_ROTATE_0)
		goto ffu;

	num_clips = drm_plane_get_damage_clips_count(new_plane_state);
	clips = drm_plane_get_damage_clips(new_plane_state);

	if (num_clips && (!amdgpu_damage_clips || (amdgpu_damage_clips < 0 &&
						   is_psr_su)))
		goto ffu;

	if (!dm_crtc_state->mpo_requested) {
		if (!num_clips || num_clips > DC_MAX_DIRTY_RECTS)
			goto ffu;

		for (; flip_addrs->dirty_rect_count < num_clips; clips++)
			fill_dc_dirty_rect(new_plane_state->plane,
					   &dirty_rects[flip_addrs->dirty_rect_count],
					   clips->x1, clips->y1,
					   clips->x2 - clips->x1, clips->y2 - clips->y1,
					   &flip_addrs->dirty_rect_count,
					   false);
		return;
	}

	/*
	 * MPO is requested. Add entire plane bounding box to dirty rects if
	 * flipped to or damaged.
	 *
	 * If plane is moved or resized, also add old bounding box to dirty
	 * rects.
	 */
	fb_changed = old_plane_state->fb->base.id !=
		     new_plane_state->fb->base.id;
	bb_changed = (old_plane_state->crtc_x != new_plane_state->crtc_x ||
		      old_plane_state->crtc_y != new_plane_state->crtc_y ||
		      old_plane_state->crtc_w != new_plane_state->crtc_w ||
		      old_plane_state->crtc_h != new_plane_state->crtc_h);

	drm_dbg(plane->dev,
		"[PLANE:%d] PSR bb_changed:%d fb_changed:%d num_clips:%d\n",
		new_plane_state->plane->base.id,
		bb_changed, fb_changed, num_clips);

	*dirty_regions_changed = bb_changed;

	if ((num_clips + (bb_changed ? 2 : 0)) > DC_MAX_DIRTY_RECTS)
		goto ffu;

	if (bb_changed) {
		fill_dc_dirty_rect(new_plane_state->plane, &dirty_rects[i],
				   new_plane_state->crtc_x,
				   new_plane_state->crtc_y,
				   new_plane_state->crtc_w,
				   new_plane_state->crtc_h, &i, false);

		/* Add old plane bounding-box if plane is moved or resized */
		fill_dc_dirty_rect(new_plane_state->plane, &dirty_rects[i],
				   old_plane_state->crtc_x,
				   old_plane_state->crtc_y,
				   old_plane_state->crtc_w,
				   old_plane_state->crtc_h, &i, false);
	}

	if (num_clips) {
		for (; i < num_clips; clips++)
			fill_dc_dirty_rect(new_plane_state->plane,
					   &dirty_rects[i], clips->x1,
					   clips->y1, clips->x2 - clips->x1,
					   clips->y2 - clips->y1, &i, false);
	} else if (fb_changed && !bb_changed) {
		fill_dc_dirty_rect(new_plane_state->plane, &dirty_rects[i],
				   new_plane_state->crtc_x,
				   new_plane_state->crtc_y,
				   new_plane_state->crtc_w,
				   new_plane_state->crtc_h, &i, false);
	}

	flip_addrs->dirty_rect_count = i;
	return;

ffu:
	fill_dc_dirty_rect(new_plane_state->plane, &dirty_rects[0], 0, 0,
			   dm_crtc_state->base.mode.crtc_hdisplay,
			   dm_crtc_state->base.mode.crtc_vdisplay,
			   &flip_addrs->dirty_rect_count, true);
}

static void update_stream_scaling_settings(const struct drm_display_mode *mode,
					   const struct dm_connector_state *dm_state,
					   struct dc_stream_state *stream)
{
	enum amdgpu_rmx_type rmx_type;

	struct rect src = { 0 }; /* viewport in composition space*/
	struct rect dst = { 0 }; /* stream addressable area */

	/* no mode. nothing to be done */
	if (!mode)
		return;

	/* Full screen scaling by default */
	src.width = mode->hdisplay;
	src.height = mode->vdisplay;
	dst.width = stream->timing.h_addressable;
	dst.height = stream->timing.v_addressable;

	if (dm_state) {
		rmx_type = dm_state->scaling;
		if (rmx_type == RMX_ASPECT || rmx_type == RMX_OFF) {
			if (src.width * dst.height <
					src.height * dst.width) {
				/* height needs less upscaling/more downscaling */
				dst.width = src.width *
						dst.height / src.height;
			} else {
				/* width needs less upscaling/more downscaling */
				dst.height = src.height *
						dst.width / src.width;
			}
		} else if (rmx_type == RMX_CENTER) {
			dst = src;
		}

		dst.x = (stream->timing.h_addressable - dst.width) / 2;
		dst.y = (stream->timing.v_addressable - dst.height) / 2;

		if (dm_state->underscan_enable) {
			dst.x += dm_state->underscan_hborder / 2;
			dst.y += dm_state->underscan_vborder / 2;
			dst.width -= dm_state->underscan_hborder;
			dst.height -= dm_state->underscan_vborder;
		}
	}

	stream->src = src;
	stream->dst = dst;

	DRM_DEBUG_KMS("Destination Rectangle x:%d  y:%d  width:%d  height:%d\n",
		      dst.x, dst.y, dst.width, dst.height);

}

static enum dc_color_depth
convert_color_depth_from_display_info(const struct drm_connector *connector,
				      bool is_y420, int requested_bpc)
{
	u8 bpc;

	if (is_y420) {
		bpc = 8;

		/* Cap display bpc based on HDMI 2.0 HF-VSDB */
		if (connector->display_info.hdmi.y420_dc_modes & DRM_EDID_YCBCR420_DC_48)
			bpc = 16;
		else if (connector->display_info.hdmi.y420_dc_modes & DRM_EDID_YCBCR420_DC_36)
			bpc = 12;
		else if (connector->display_info.hdmi.y420_dc_modes & DRM_EDID_YCBCR420_DC_30)
			bpc = 10;
	} else {
		bpc = (uint8_t)connector->display_info.bpc;
		/* Assume 8 bpc by default if no bpc is specified. */
		bpc = bpc ? bpc : 8;
	}

	if (requested_bpc > 0) {
		/*
		 * Cap display bpc based on the user requested value.
		 *
		 * The value for state->max_bpc may not correctly updated
		 * depending on when the connector gets added to the state
		 * or if this was called outside of atomic check, so it
		 * can't be used directly.
		 */
		bpc = min_t(u8, bpc, requested_bpc);

		/* Round down to the nearest even number. */
		bpc = bpc - (bpc & 1);
	}

	switch (bpc) {
	case 0:
		/*
		 * Temporary Work around, DRM doesn't parse color depth for
		 * EDID revision before 1.4
		 * TODO: Fix edid parsing
		 */
		return COLOR_DEPTH_888;
	case 6:
		return COLOR_DEPTH_666;
	case 8:
		return COLOR_DEPTH_888;
	case 10:
		return COLOR_DEPTH_101010;
	case 12:
		return COLOR_DEPTH_121212;
	case 14:
		return COLOR_DEPTH_141414;
	case 16:
		return COLOR_DEPTH_161616;
	default:
		return COLOR_DEPTH_UNDEFINED;
	}
}

static enum dc_aspect_ratio
get_aspect_ratio(const struct drm_display_mode *mode_in)
{
	/* 1-1 mapping, since both enums follow the HDMI spec. */
	return (enum dc_aspect_ratio) mode_in->picture_aspect_ratio;
}

static enum dc_color_space
get_output_color_space(const struct dc_crtc_timing *dc_crtc_timing,
		       const struct drm_connector_state *connector_state)
{
	enum dc_color_space color_space = COLOR_SPACE_SRGB;

	switch (connector_state->colorspace) {
	case DRM_MODE_COLORIMETRY_BT601_YCC:
		if (dc_crtc_timing->flags.Y_ONLY)
			color_space = COLOR_SPACE_YCBCR601_LIMITED;
		else
			color_space = COLOR_SPACE_YCBCR601;
		break;
	case DRM_MODE_COLORIMETRY_BT709_YCC:
		if (dc_crtc_timing->flags.Y_ONLY)
			color_space = COLOR_SPACE_YCBCR709_LIMITED;
		else
			color_space = COLOR_SPACE_YCBCR709;
		break;
	case DRM_MODE_COLORIMETRY_OPRGB:
		color_space = COLOR_SPACE_ADOBERGB;
		break;
	case DRM_MODE_COLORIMETRY_BT2020_RGB:
	case DRM_MODE_COLORIMETRY_BT2020_YCC:
		if (dc_crtc_timing->pixel_encoding == PIXEL_ENCODING_RGB)
			color_space = COLOR_SPACE_2020_RGB_FULLRANGE;
		else
			color_space = COLOR_SPACE_2020_YCBCR_LIMITED;
		break;
	case DRM_MODE_COLORIMETRY_DEFAULT: // ITU601
	default:
		if (dc_crtc_timing->pixel_encoding == PIXEL_ENCODING_RGB) {
			color_space = COLOR_SPACE_SRGB;
		/*
		 * 27030khz is the separation point between HDTV and SDTV
		 * according to HDMI spec, we use YCbCr709 and YCbCr601
		 * respectively
		 */
		} else if (dc_crtc_timing->pix_clk_100hz > 270300) {
			if (dc_crtc_timing->flags.Y_ONLY)
				color_space =
					COLOR_SPACE_YCBCR709_LIMITED;
			else
				color_space = COLOR_SPACE_YCBCR709;
		} else {
			if (dc_crtc_timing->flags.Y_ONLY)
				color_space =
					COLOR_SPACE_YCBCR601_LIMITED;
			else
				color_space = COLOR_SPACE_YCBCR601;
		}
		break;
	}

	return color_space;
}

static enum display_content_type
get_output_content_type(const struct drm_connector_state *connector_state)
{
	switch (connector_state->content_type) {
	default:
	case DRM_MODE_CONTENT_TYPE_NO_DATA:
		return DISPLAY_CONTENT_TYPE_NO_DATA;
	case DRM_MODE_CONTENT_TYPE_GRAPHICS:
		return DISPLAY_CONTENT_TYPE_GRAPHICS;
	case DRM_MODE_CONTENT_TYPE_PHOTO:
		return DISPLAY_CONTENT_TYPE_PHOTO;
	case DRM_MODE_CONTENT_TYPE_CINEMA:
		return DISPLAY_CONTENT_TYPE_CINEMA;
	case DRM_MODE_CONTENT_TYPE_GAME:
		return DISPLAY_CONTENT_TYPE_GAME;
	}
}

static bool adjust_colour_depth_from_display_info(
	struct dc_crtc_timing *timing_out,
	const struct drm_display_info *info)
{
	enum dc_color_depth depth = timing_out->display_color_depth;
	int normalized_clk;

	do {
		normalized_clk = timing_out->pix_clk_100hz / 10;
		/* YCbCr 4:2:0 requires additional adjustment of 1/2 */
		if (timing_out->pixel_encoding == PIXEL_ENCODING_YCBCR420)
			normalized_clk /= 2;
		/* Adjusting pix clock following on HDMI spec based on colour depth */
		switch (depth) {
		case COLOR_DEPTH_888:
			break;
		case COLOR_DEPTH_101010:
			normalized_clk = (normalized_clk * 30) / 24;
			break;
		case COLOR_DEPTH_121212:
			normalized_clk = (normalized_clk * 36) / 24;
			break;
		case COLOR_DEPTH_161616:
			normalized_clk = (normalized_clk * 48) / 24;
			break;
		default:
			/* The above depths are the only ones valid for HDMI. */
			return false;
		}
		if (normalized_clk <= info->max_tmds_clock) {
			timing_out->display_color_depth = depth;
			return true;
		}
	} while (--depth > COLOR_DEPTH_666);
	return false;
}

static void fill_stream_properties_from_drm_display_mode(
	struct dc_stream_state *stream,
	const struct drm_display_mode *mode_in,
	const struct drm_connector *connector,
	const struct drm_connector_state *connector_state,
	const struct dc_stream_state *old_stream,
	int requested_bpc)
{
	struct dc_crtc_timing *timing_out = &stream->timing;
	const struct drm_display_info *info = &connector->display_info;
	struct amdgpu_dm_connector *aconnector = NULL;
	struct hdmi_vendor_infoframe hv_frame;
	struct hdmi_avi_infoframe avi_frame;

	if (connector->connector_type != DRM_MODE_CONNECTOR_WRITEBACK)
		aconnector = to_amdgpu_dm_connector(connector);

	memset(&hv_frame, 0, sizeof(hv_frame));
	memset(&avi_frame, 0, sizeof(avi_frame));

	timing_out->h_border_left = 0;
	timing_out->h_border_right = 0;
	timing_out->v_border_top = 0;
	timing_out->v_border_bottom = 0;
	/* TODO: un-hardcode */
	if (drm_mode_is_420_only(info, mode_in)
			&& stream->signal == SIGNAL_TYPE_HDMI_TYPE_A)
		timing_out->pixel_encoding = PIXEL_ENCODING_YCBCR420;
	else if (drm_mode_is_420_also(info, mode_in)
			&& aconnector
			&& aconnector->force_yuv420_output)
		timing_out->pixel_encoding = PIXEL_ENCODING_YCBCR420;
	else if ((connector->display_info.color_formats & DRM_COLOR_FORMAT_YCBCR444)
			&& stream->signal == SIGNAL_TYPE_HDMI_TYPE_A)
		timing_out->pixel_encoding = PIXEL_ENCODING_YCBCR444;
	else
		timing_out->pixel_encoding = PIXEL_ENCODING_RGB;

	timing_out->timing_3d_format = TIMING_3D_FORMAT_NONE;
	timing_out->display_color_depth = convert_color_depth_from_display_info(
		connector,
		(timing_out->pixel_encoding == PIXEL_ENCODING_YCBCR420),
		requested_bpc);
	timing_out->scan_type = SCANNING_TYPE_NODATA;
	timing_out->hdmi_vic = 0;

	if (old_stream) {
		timing_out->vic = old_stream->timing.vic;
		timing_out->flags.HSYNC_POSITIVE_POLARITY = old_stream->timing.flags.HSYNC_POSITIVE_POLARITY;
		timing_out->flags.VSYNC_POSITIVE_POLARITY = old_stream->timing.flags.VSYNC_POSITIVE_POLARITY;
	} else {
		timing_out->vic = drm_match_cea_mode(mode_in);
		if (mode_in->flags & DRM_MODE_FLAG_PHSYNC)
			timing_out->flags.HSYNC_POSITIVE_POLARITY = 1;
		if (mode_in->flags & DRM_MODE_FLAG_PVSYNC)
			timing_out->flags.VSYNC_POSITIVE_POLARITY = 1;
	}

	if (stream->signal == SIGNAL_TYPE_HDMI_TYPE_A) {
		drm_hdmi_avi_infoframe_from_display_mode(&avi_frame, (struct drm_connector *)connector, mode_in);
		timing_out->vic = avi_frame.video_code;
		drm_hdmi_vendor_infoframe_from_display_mode(&hv_frame, (struct drm_connector *)connector, mode_in);
		timing_out->hdmi_vic = hv_frame.vic;
	}

	if (aconnector && is_freesync_video_mode(mode_in, aconnector)) {
		timing_out->h_addressable = mode_in->hdisplay;
		timing_out->h_total = mode_in->htotal;
		timing_out->h_sync_width = mode_in->hsync_end - mode_in->hsync_start;
		timing_out->h_front_porch = mode_in->hsync_start - mode_in->hdisplay;
		timing_out->v_total = mode_in->vtotal;
		timing_out->v_addressable = mode_in->vdisplay;
		timing_out->v_front_porch = mode_in->vsync_start - mode_in->vdisplay;
		timing_out->v_sync_width = mode_in->vsync_end - mode_in->vsync_start;
		timing_out->pix_clk_100hz = mode_in->clock * 10;
	} else {
		timing_out->h_addressable = mode_in->crtc_hdisplay;
		timing_out->h_total = mode_in->crtc_htotal;
		timing_out->h_sync_width = mode_in->crtc_hsync_end - mode_in->crtc_hsync_start;
		timing_out->h_front_porch = mode_in->crtc_hsync_start - mode_in->crtc_hdisplay;
		timing_out->v_total = mode_in->crtc_vtotal;
		timing_out->v_addressable = mode_in->crtc_vdisplay;
		timing_out->v_front_porch = mode_in->crtc_vsync_start - mode_in->crtc_vdisplay;
		timing_out->v_sync_width = mode_in->crtc_vsync_end - mode_in->crtc_vsync_start;
		timing_out->pix_clk_100hz = mode_in->crtc_clock * 10;
	}

	timing_out->aspect_ratio = get_aspect_ratio(mode_in);

	stream->out_transfer_func.type = TF_TYPE_PREDEFINED;
	stream->out_transfer_func.tf = TRANSFER_FUNCTION_SRGB;
	if (stream->signal == SIGNAL_TYPE_HDMI_TYPE_A) {
		if (!adjust_colour_depth_from_display_info(timing_out, info) &&
		    drm_mode_is_420_also(info, mode_in) &&
		    timing_out->pixel_encoding != PIXEL_ENCODING_YCBCR420) {
			timing_out->pixel_encoding = PIXEL_ENCODING_YCBCR420;
			adjust_colour_depth_from_display_info(timing_out, info);
		}
	}

	stream->output_color_space = get_output_color_space(timing_out, connector_state);
	stream->content_type = get_output_content_type(connector_state);
}

static void fill_audio_info(struct audio_info *audio_info,
			    const struct drm_connector *drm_connector,
			    const struct dc_sink *dc_sink)
{
	int i = 0;
	int cea_revision = 0;
	const struct dc_edid_caps *edid_caps = &dc_sink->edid_caps;

	audio_info->manufacture_id = edid_caps->manufacturer_id;
	audio_info->product_id = edid_caps->product_id;

	cea_revision = drm_connector->display_info.cea_rev;

	strscpy(audio_info->display_name,
		edid_caps->display_name,
		AUDIO_INFO_DISPLAY_NAME_SIZE_IN_CHARS);

	if (cea_revision >= 3) {
		audio_info->mode_count = edid_caps->audio_mode_count;

		for (i = 0; i < audio_info->mode_count; ++i) {
			audio_info->modes[i].format_code =
					(enum audio_format_code)
					(edid_caps->audio_modes[i].format_code);
			audio_info->modes[i].channel_count =
					edid_caps->audio_modes[i].channel_count;
			audio_info->modes[i].sample_rates.all =
					edid_caps->audio_modes[i].sample_rate;
			audio_info->modes[i].sample_size =
					edid_caps->audio_modes[i].sample_size;
		}
	}

	audio_info->flags.all = edid_caps->speaker_flags;

	/* TODO: We only check for the progressive mode, check for interlace mode too */
	if (drm_connector->latency_present[0]) {
		audio_info->video_latency = drm_connector->video_latency[0];
		audio_info->audio_latency = drm_connector->audio_latency[0];
	}

	/* TODO: For DP, video and audio latency should be calculated from DPCD caps */

}

static void
copy_crtc_timing_for_drm_display_mode(const struct drm_display_mode *src_mode,
				      struct drm_display_mode *dst_mode)
{
	dst_mode->crtc_hdisplay = src_mode->crtc_hdisplay;
	dst_mode->crtc_vdisplay = src_mode->crtc_vdisplay;
	dst_mode->crtc_clock = src_mode->crtc_clock;
	dst_mode->crtc_hblank_start = src_mode->crtc_hblank_start;
	dst_mode->crtc_hblank_end = src_mode->crtc_hblank_end;
	dst_mode->crtc_hsync_start =  src_mode->crtc_hsync_start;
	dst_mode->crtc_hsync_end = src_mode->crtc_hsync_end;
	dst_mode->crtc_htotal = src_mode->crtc_htotal;
	dst_mode->crtc_hskew = src_mode->crtc_hskew;
	dst_mode->crtc_vblank_start = src_mode->crtc_vblank_start;
	dst_mode->crtc_vblank_end = src_mode->crtc_vblank_end;
	dst_mode->crtc_vsync_start = src_mode->crtc_vsync_start;
	dst_mode->crtc_vsync_end = src_mode->crtc_vsync_end;
	dst_mode->crtc_vtotal = src_mode->crtc_vtotal;
}

static void
decide_crtc_timing_for_drm_display_mode(struct drm_display_mode *drm_mode,
					const struct drm_display_mode *native_mode,
					bool scale_enabled)
{
	if (scale_enabled) {
		copy_crtc_timing_for_drm_display_mode(native_mode, drm_mode);
	} else if (native_mode->clock == drm_mode->clock &&
			native_mode->htotal == drm_mode->htotal &&
			native_mode->vtotal == drm_mode->vtotal) {
		copy_crtc_timing_for_drm_display_mode(native_mode, drm_mode);
	} else {
		/* no scaling nor amdgpu inserted, no need to patch */
	}
}

static struct dc_sink *
create_fake_sink(struct dc_link *link)
{
	struct dc_sink_init_data sink_init_data = { 0 };
	struct dc_sink *sink = NULL;

	sink_init_data.link = link;
	sink_init_data.sink_signal = link->connector_signal;

	sink = dc_sink_create(&sink_init_data);
	if (!sink) {
		DRM_ERROR("Failed to create sink!\n");
		return NULL;
	}
	sink->sink_signal = SIGNAL_TYPE_VIRTUAL;

	return sink;
}

static void set_multisync_trigger_params(
		struct dc_stream_state *stream)
{
	struct dc_stream_state *master = NULL;

	if (stream->triggered_crtc_reset.enabled) {
		master = stream->triggered_crtc_reset.event_source;
		stream->triggered_crtc_reset.event =
			master->timing.flags.VSYNC_POSITIVE_POLARITY ?
			CRTC_EVENT_VSYNC_RISING : CRTC_EVENT_VSYNC_FALLING;
		stream->triggered_crtc_reset.delay = TRIGGER_DELAY_NEXT_PIXEL;
	}
}

static void set_master_stream(struct dc_stream_state *stream_set[],
			      int stream_count)
{
	int j, highest_rfr = 0, master_stream = 0;

	for (j = 0;  j < stream_count; j++) {
		if (stream_set[j] && stream_set[j]->triggered_crtc_reset.enabled) {
			int refresh_rate = 0;

			refresh_rate = (stream_set[j]->timing.pix_clk_100hz*100)/
				(stream_set[j]->timing.h_total*stream_set[j]->timing.v_total);
			if (refresh_rate > highest_rfr) {
				highest_rfr = refresh_rate;
				master_stream = j;
			}
		}
	}
	for (j = 0;  j < stream_count; j++) {
		if (stream_set[j])
			stream_set[j]->triggered_crtc_reset.event_source = stream_set[master_stream];
	}
}

static void dm_enable_per_frame_crtc_master_sync(struct dc_state *context)
{
	int i = 0;
	struct dc_stream_state *stream;

	if (context->stream_count < 2)
		return;
	for (i = 0; i < context->stream_count ; i++) {
		if (!context->streams[i])
			continue;
		/*
		 * TODO: add a function to read AMD VSDB bits and set
		 * crtc_sync_master.multi_sync_enabled flag
		 * For now it's set to false
		 */
	}

	set_master_stream(context->streams, context->stream_count);

	for (i = 0; i < context->stream_count ; i++) {
		stream = context->streams[i];

		if (!stream)
			continue;

		set_multisync_trigger_params(stream);
	}
}

/**
 * DOC: FreeSync Video
 *
 * When a userspace application wants to play a video, the content follows a
 * standard format definition that usually specifies the FPS for that format.
 * The below list illustrates some video format and the expected FPS,
 * respectively:
 *
 * - TV/NTSC (23.976 FPS)
 * - Cinema (24 FPS)
 * - TV/PAL (25 FPS)
 * - TV/NTSC (29.97 FPS)
 * - TV/NTSC (30 FPS)
 * - Cinema HFR (48 FPS)
 * - TV/PAL (50 FPS)
 * - Commonly used (60 FPS)
 * - Multiples of 24 (48,72,96 FPS)
 *
 * The list of standards video format is not huge and can be added to the
 * connector modeset list beforehand. With that, userspace can leverage
 * FreeSync to extends the front porch in order to attain the target refresh
 * rate. Such a switch will happen seamlessly, without screen blanking or
 * reprogramming of the output in any other way. If the userspace requests a
 * modesetting change compatible with FreeSync modes that only differ in the
 * refresh rate, DC will skip the full update and avoid blink during the
 * transition. For example, the video player can change the modesetting from
 * 60Hz to 30Hz for playing TV/NTSC content when it goes full screen without
 * causing any display blink. This same concept can be applied to a mode
 * setting change.
 */
static struct drm_display_mode *
get_highest_refresh_rate_mode(struct amdgpu_dm_connector *aconnector,
		bool use_probed_modes)
{
	struct drm_display_mode *m, *m_pref = NULL;
	u16 current_refresh, highest_refresh;
	struct list_head *list_head = use_probed_modes ?
		&aconnector->base.probed_modes :
		&aconnector->base.modes;

	if (aconnector->base.connector_type == DRM_MODE_CONNECTOR_WRITEBACK)
		return NULL;

	if (aconnector->freesync_vid_base.clock != 0)
		return &aconnector->freesync_vid_base;

	/* Find the preferred mode */
	list_for_each_entry(m, list_head, head) {
		if (m->type & DRM_MODE_TYPE_PREFERRED) {
			m_pref = m;
			break;
		}
	}

	if (!m_pref) {
		/* Probably an EDID with no preferred mode. Fallback to first entry */
		m_pref = list_first_entry_or_null(
				&aconnector->base.modes, struct drm_display_mode, head);
		if (!m_pref) {
			DRM_DEBUG_DRIVER("No preferred mode found in EDID\n");
			return NULL;
		}
	}

	highest_refresh = drm_mode_vrefresh(m_pref);

	/*
	 * Find the mode with highest refresh rate with same resolution.
	 * For some monitors, preferred mode is not the mode with highest
	 * supported refresh rate.
	 */
	list_for_each_entry(m, list_head, head) {
		current_refresh  = drm_mode_vrefresh(m);

		if (m->hdisplay == m_pref->hdisplay &&
		    m->vdisplay == m_pref->vdisplay &&
		    highest_refresh < current_refresh) {
			highest_refresh = current_refresh;
			m_pref = m;
		}
	}

	drm_mode_copy(&aconnector->freesync_vid_base, m_pref);
	return m_pref;
}

static bool is_freesync_video_mode(const struct drm_display_mode *mode,
		struct amdgpu_dm_connector *aconnector)
{
	struct drm_display_mode *high_mode;
	int timing_diff;

	high_mode = get_highest_refresh_rate_mode(aconnector, false);
	if (!high_mode || !mode)
		return false;

	timing_diff = high_mode->vtotal - mode->vtotal;

	if (high_mode->clock == 0 || high_mode->clock != mode->clock ||
	    high_mode->hdisplay != mode->hdisplay ||
	    high_mode->vdisplay != mode->vdisplay ||
	    high_mode->hsync_start != mode->hsync_start ||
	    high_mode->hsync_end != mode->hsync_end ||
	    high_mode->htotal != mode->htotal ||
	    high_mode->hskew != mode->hskew ||
	    high_mode->vscan != mode->vscan ||
	    high_mode->vsync_start - mode->vsync_start != timing_diff ||
	    high_mode->vsync_end - mode->vsync_end != timing_diff)
		return false;
	else
		return true;
}

#if defined(CONFIG_DRM_AMD_DC_FP)
static void update_dsc_caps(struct amdgpu_dm_connector *aconnector,
			    struct dc_sink *sink, struct dc_stream_state *stream,
			    struct dsc_dec_dpcd_caps *dsc_caps)
{
	stream->timing.flags.DSC = 0;
	dsc_caps->is_dsc_supported = false;

	if (aconnector->dc_link && (sink->sink_signal == SIGNAL_TYPE_DISPLAY_PORT ||
	    sink->sink_signal == SIGNAL_TYPE_EDP)) {
		if (sink->link->dpcd_caps.dongle_type == DISPLAY_DONGLE_NONE ||
			sink->link->dpcd_caps.dongle_type == DISPLAY_DONGLE_DP_HDMI_CONVERTER)
			dc_dsc_parse_dsc_dpcd(aconnector->dc_link->ctx->dc,
				aconnector->dc_link->dpcd_caps.dsc_caps.dsc_basic_caps.raw,
				aconnector->dc_link->dpcd_caps.dsc_caps.dsc_branch_decoder_caps.raw,
				dsc_caps);
	}
}

static void apply_dsc_policy_for_edp(struct amdgpu_dm_connector *aconnector,
				    struct dc_sink *sink, struct dc_stream_state *stream,
				    struct dsc_dec_dpcd_caps *dsc_caps,
				    uint32_t max_dsc_target_bpp_limit_override)
{
	const struct dc_link_settings *verified_link_cap = NULL;
	u32 link_bw_in_kbps;
	u32 edp_min_bpp_x16, edp_max_bpp_x16;
	struct dc *dc = sink->ctx->dc;
	struct dc_dsc_bw_range bw_range = {0};
	struct dc_dsc_config dsc_cfg = {0};
	struct dc_dsc_config_options dsc_options = {0};

	dc_dsc_get_default_config_option(dc, &dsc_options);
	dsc_options.max_target_bpp_limit_override_x16 = max_dsc_target_bpp_limit_override * 16;

	verified_link_cap = dc_link_get_link_cap(stream->link);
	link_bw_in_kbps = dc_link_bandwidth_kbps(stream->link, verified_link_cap);
	edp_min_bpp_x16 = 8 * 16;
	edp_max_bpp_x16 = 8 * 16;

	if (edp_max_bpp_x16 > dsc_caps->edp_max_bits_per_pixel)
		edp_max_bpp_x16 = dsc_caps->edp_max_bits_per_pixel;

	if (edp_max_bpp_x16 < edp_min_bpp_x16)
		edp_min_bpp_x16 = edp_max_bpp_x16;

	if (dc_dsc_compute_bandwidth_range(dc->res_pool->dscs[0],
				dc->debug.dsc_min_slice_height_override,
				edp_min_bpp_x16, edp_max_bpp_x16,
				dsc_caps,
				&stream->timing,
				dc_link_get_highest_encoding_format(aconnector->dc_link),
				&bw_range)) {

		if (bw_range.max_kbps < link_bw_in_kbps) {
			if (dc_dsc_compute_config(dc->res_pool->dscs[0],
					dsc_caps,
					&dsc_options,
					0,
					&stream->timing,
					dc_link_get_highest_encoding_format(aconnector->dc_link),
					&dsc_cfg)) {
				stream->timing.dsc_cfg = dsc_cfg;
				stream->timing.flags.DSC = 1;
				stream->timing.dsc_cfg.bits_per_pixel = edp_max_bpp_x16;
			}
			return;
		}
	}

	if (dc_dsc_compute_config(dc->res_pool->dscs[0],
				dsc_caps,
				&dsc_options,
				link_bw_in_kbps,
				&stream->timing,
				dc_link_get_highest_encoding_format(aconnector->dc_link),
				&dsc_cfg)) {
		stream->timing.dsc_cfg = dsc_cfg;
		stream->timing.flags.DSC = 1;
	}
}

static void apply_dsc_policy_for_stream(struct amdgpu_dm_connector *aconnector,
					struct dc_sink *sink, struct dc_stream_state *stream,
					struct dsc_dec_dpcd_caps *dsc_caps)
{
	struct drm_connector *drm_connector = &aconnector->base;
	u32 link_bandwidth_kbps;
	struct dc *dc = sink->ctx->dc;
	u32 max_supported_bw_in_kbps, timing_bw_in_kbps;
	u32 dsc_max_supported_bw_in_kbps;
	u32 max_dsc_target_bpp_limit_override =
		drm_connector->display_info.max_dsc_bpp;
	struct dc_dsc_config_options dsc_options = {0};

	dc_dsc_get_default_config_option(dc, &dsc_options);
	dsc_options.max_target_bpp_limit_override_x16 = max_dsc_target_bpp_limit_override * 16;

	link_bandwidth_kbps = dc_link_bandwidth_kbps(aconnector->dc_link,
							dc_link_get_link_cap(aconnector->dc_link));

	/* Set DSC policy according to dsc_clock_en */
	dc_dsc_policy_set_enable_dsc_when_not_needed(
		aconnector->dsc_settings.dsc_force_enable == DSC_CLK_FORCE_ENABLE);

	if (sink->sink_signal == SIGNAL_TYPE_EDP &&
	    !aconnector->dc_link->panel_config.dsc.disable_dsc_edp &&
	    dc->caps.edp_dsc_support && aconnector->dsc_settings.dsc_force_enable != DSC_CLK_FORCE_DISABLE) {

		apply_dsc_policy_for_edp(aconnector, sink, stream, dsc_caps, max_dsc_target_bpp_limit_override);

	} else if (sink->sink_signal == SIGNAL_TYPE_DISPLAY_PORT) {
		if (sink->link->dpcd_caps.dongle_type == DISPLAY_DONGLE_NONE) {
			if (dc_dsc_compute_config(aconnector->dc_link->ctx->dc->res_pool->dscs[0],
						dsc_caps,
						&dsc_options,
						link_bandwidth_kbps,
						&stream->timing,
						dc_link_get_highest_encoding_format(aconnector->dc_link),
						&stream->timing.dsc_cfg)) {
				stream->timing.flags.DSC = 1;
				DRM_DEBUG_DRIVER("%s: SST_DSC [%s] DSC is selected from SST RX\n",
							__func__, drm_connector->name);
			}
		} else if (sink->link->dpcd_caps.dongle_type == DISPLAY_DONGLE_DP_HDMI_CONVERTER) {
			timing_bw_in_kbps = dc_bandwidth_in_kbps_from_timing(&stream->timing,
					dc_link_get_highest_encoding_format(aconnector->dc_link));
			max_supported_bw_in_kbps = link_bandwidth_kbps;
			dsc_max_supported_bw_in_kbps = link_bandwidth_kbps;

			if (timing_bw_in_kbps > max_supported_bw_in_kbps &&
					max_supported_bw_in_kbps > 0 &&
					dsc_max_supported_bw_in_kbps > 0)
				if (dc_dsc_compute_config(aconnector->dc_link->ctx->dc->res_pool->dscs[0],
						dsc_caps,
						&dsc_options,
						dsc_max_supported_bw_in_kbps,
						&stream->timing,
						dc_link_get_highest_encoding_format(aconnector->dc_link),
						&stream->timing.dsc_cfg)) {
					stream->timing.flags.DSC = 1;
					DRM_DEBUG_DRIVER("%s: SST_DSC [%s] DSC is selected from DP-HDMI PCON\n",
									 __func__, drm_connector->name);
				}
		}
	}

	/* Overwrite the stream flag if DSC is enabled through debugfs */
	if (aconnector->dsc_settings.dsc_force_enable == DSC_CLK_FORCE_ENABLE)
		stream->timing.flags.DSC = 1;

	if (stream->timing.flags.DSC && aconnector->dsc_settings.dsc_num_slices_h)
		stream->timing.dsc_cfg.num_slices_h = aconnector->dsc_settings.dsc_num_slices_h;

	if (stream->timing.flags.DSC && aconnector->dsc_settings.dsc_num_slices_v)
		stream->timing.dsc_cfg.num_slices_v = aconnector->dsc_settings.dsc_num_slices_v;

	if (stream->timing.flags.DSC && aconnector->dsc_settings.dsc_bits_per_pixel)
		stream->timing.dsc_cfg.bits_per_pixel = aconnector->dsc_settings.dsc_bits_per_pixel;
}
#endif

static struct dc_stream_state *
create_stream_for_sink(struct drm_connector *connector,
		       const struct drm_display_mode *drm_mode,
		       const struct dm_connector_state *dm_state,
		       const struct dc_stream_state *old_stream,
		       int requested_bpc)
{
	struct amdgpu_dm_connector *aconnector = NULL;
	struct drm_display_mode *preferred_mode = NULL;
	const struct drm_connector_state *con_state = &dm_state->base;
	struct dc_stream_state *stream = NULL;
	struct drm_display_mode mode;
	struct drm_display_mode saved_mode;
	struct drm_display_mode *freesync_mode = NULL;
	bool native_mode_found = false;
	bool recalculate_timing = false;
	bool scale = dm_state->scaling != RMX_OFF;
	int mode_refresh;
	int preferred_refresh = 0;
	enum color_transfer_func tf = TRANSFER_FUNC_UNKNOWN;
#if defined(CONFIG_DRM_AMD_DC_FP)
	struct dsc_dec_dpcd_caps dsc_caps;
#endif
	struct dc_link *link = NULL;
	struct dc_sink *sink = NULL;

	drm_mode_init(&mode, drm_mode);
	memset(&saved_mode, 0, sizeof(saved_mode));

	if (connector == NULL) {
		DRM_ERROR("connector is NULL!\n");
		return stream;
	}

	if (connector->connector_type != DRM_MODE_CONNECTOR_WRITEBACK) {
		aconnector = NULL;
		aconnector = to_amdgpu_dm_connector(connector);
		link = aconnector->dc_link;
	} else {
		struct drm_writeback_connector *wbcon = NULL;
		struct amdgpu_dm_wb_connector *dm_wbcon = NULL;

		wbcon = drm_connector_to_writeback(connector);
		dm_wbcon = to_amdgpu_dm_wb_connector(wbcon);
		link = dm_wbcon->link;
	}

	if (!aconnector || !aconnector->dc_sink) {
		sink = create_fake_sink(link);
		if (!sink)
			return stream;

	} else {
		sink = aconnector->dc_sink;
		dc_sink_retain(sink);
	}

	stream = dc_create_stream_for_sink(sink);

	if (stream == NULL) {
		DRM_ERROR("Failed to create stream for sink!\n");
		goto finish;
	}

	/* We leave this NULL for writeback connectors */
	stream->dm_stream_context = aconnector;

	stream->timing.flags.LTE_340MCSC_SCRAMBLE =
		connector->display_info.hdmi.scdc.scrambling.low_rates;

	list_for_each_entry(preferred_mode, &connector->modes, head) {
		/* Search for preferred mode */
		if (preferred_mode->type & DRM_MODE_TYPE_PREFERRED) {
			native_mode_found = true;
			break;
		}
	}
	if (!native_mode_found)
		preferred_mode = list_first_entry_or_null(
				&connector->modes,
				struct drm_display_mode,
				head);

	mode_refresh = drm_mode_vrefresh(&mode);

	if (preferred_mode == NULL) {
		/*
		 * This may not be an error, the use case is when we have no
		 * usermode calls to reset and set mode upon hotplug. In this
		 * case, we call set mode ourselves to restore the previous mode
		 * and the modelist may not be filled in time.
		 */
		DRM_DEBUG_DRIVER("No preferred mode found\n");
	} else if (aconnector) {
		recalculate_timing = amdgpu_freesync_vid_mode &&
				 is_freesync_video_mode(&mode, aconnector);
		if (recalculate_timing) {
			freesync_mode = get_highest_refresh_rate_mode(aconnector, false);
			drm_mode_copy(&saved_mode, &mode);
			saved_mode.picture_aspect_ratio = mode.picture_aspect_ratio;
			drm_mode_copy(&mode, freesync_mode);
			mode.picture_aspect_ratio = saved_mode.picture_aspect_ratio;
		} else {
			decide_crtc_timing_for_drm_display_mode(
					&mode, preferred_mode, scale);

			preferred_refresh = drm_mode_vrefresh(preferred_mode);
		}
	}

	if (recalculate_timing)
		drm_mode_set_crtcinfo(&saved_mode, 0);

	/*
	 * If scaling is enabled and refresh rate didn't change
	 * we copy the vic and polarities of the old timings
	 */
	if (!scale || mode_refresh != preferred_refresh)
		fill_stream_properties_from_drm_display_mode(
			stream, &mode, connector, con_state, NULL,
			requested_bpc);
	else
		fill_stream_properties_from_drm_display_mode(
			stream, &mode, connector, con_state, old_stream,
			requested_bpc);

	/* The rest isn't needed for writeback connectors */
	if (!aconnector)
		goto finish;

	if (aconnector->timing_changed) {
		drm_dbg(aconnector->base.dev,
			"overriding timing for automated test, bpc %d, changing to %d\n",
			stream->timing.display_color_depth,
			aconnector->timing_requested->display_color_depth);
		stream->timing = *aconnector->timing_requested;
	}

#if defined(CONFIG_DRM_AMD_DC_FP)
	/* SST DSC determination policy */
	update_dsc_caps(aconnector, sink, stream, &dsc_caps);
	if (aconnector->dsc_settings.dsc_force_enable != DSC_CLK_FORCE_DISABLE && dsc_caps.is_dsc_supported)
		apply_dsc_policy_for_stream(aconnector, sink, stream, &dsc_caps);
#endif

	update_stream_scaling_settings(&mode, dm_state, stream);

	fill_audio_info(
		&stream->audio_info,
		connector,
		sink);

	update_stream_signal(stream, sink);

	if (stream->signal == SIGNAL_TYPE_HDMI_TYPE_A)
		mod_build_hf_vsif_infopacket(stream, &stream->vsp_infopacket);

	if (stream->signal == SIGNAL_TYPE_DISPLAY_PORT ||
	    stream->signal == SIGNAL_TYPE_DISPLAY_PORT_MST ||
	    stream->signal == SIGNAL_TYPE_EDP) {
		const struct dc_edid_caps *edid_caps;
		unsigned int disable_colorimetry = 0;

		if (aconnector->dc_sink) {
			edid_caps = &aconnector->dc_sink->edid_caps;
			disable_colorimetry = edid_caps->panel_patch.disable_colorimetry;
		}

		//
		// should decide stream support vsc sdp colorimetry capability
		// before building vsc info packet
		//
		stream->use_vsc_sdp_for_colorimetry = stream->link->dpcd_caps.dpcd_rev.raw >= 0x14 &&
						      stream->link->dpcd_caps.dprx_feature.bits.VSC_SDP_COLORIMETRY_SUPPORTED &&
						      !disable_colorimetry;

		if (stream->out_transfer_func.tf == TRANSFER_FUNCTION_GAMMA22)
			tf = TRANSFER_FUNC_GAMMA_22;
		mod_build_vsc_infopacket(stream, &stream->vsc_infopacket, stream->output_color_space, tf);
		aconnector->sr_skip_count = AMDGPU_DM_PSR_ENTRY_DELAY;

	}
finish:
	dc_sink_release(sink);

	return stream;
}

static enum drm_connector_status
amdgpu_dm_connector_detect(struct drm_connector *connector, bool force)
{
	bool connected;
	struct amdgpu_dm_connector *aconnector = to_amdgpu_dm_connector(connector);

	/*
	 * Notes:
	 * 1. This interface is NOT called in context of HPD irq.
	 * 2. This interface *is called* in context of user-mode ioctl. Which
	 * makes it a bad place for *any* MST-related activity.
	 */

	if (aconnector->base.force == DRM_FORCE_UNSPECIFIED &&
	    !aconnector->fake_enable)
		connected = (aconnector->dc_sink != NULL);
	else
		connected = (aconnector->base.force == DRM_FORCE_ON ||
				aconnector->base.force == DRM_FORCE_ON_DIGITAL);

	update_subconnector_property(aconnector);

	return (connected ? connector_status_connected :
			connector_status_disconnected);
}

int amdgpu_dm_connector_atomic_set_property(struct drm_connector *connector,
					    struct drm_connector_state *connector_state,
					    struct drm_property *property,
					    uint64_t val)
{
	struct drm_device *dev = connector->dev;
	struct amdgpu_device *adev = drm_to_adev(dev);
	struct dm_connector_state *dm_old_state =
		to_dm_connector_state(connector->state);
	struct dm_connector_state *dm_new_state =
		to_dm_connector_state(connector_state);

	int ret = -EINVAL;

	if (property == dev->mode_config.scaling_mode_property) {
		enum amdgpu_rmx_type rmx_type;

		switch (val) {
		case DRM_MODE_SCALE_CENTER:
			rmx_type = RMX_CENTER;
			break;
		case DRM_MODE_SCALE_ASPECT:
			rmx_type = RMX_ASPECT;
			break;
		case DRM_MODE_SCALE_FULLSCREEN:
			rmx_type = RMX_FULL;
			break;
		case DRM_MODE_SCALE_NONE:
		default:
			rmx_type = RMX_OFF;
			break;
		}

		if (dm_old_state->scaling == rmx_type)
			return 0;

		dm_new_state->scaling = rmx_type;
		ret = 0;
	} else if (property == adev->mode_info.underscan_hborder_property) {
		dm_new_state->underscan_hborder = val;
		ret = 0;
	} else if (property == adev->mode_info.underscan_vborder_property) {
		dm_new_state->underscan_vborder = val;
		ret = 0;
	} else if (property == adev->mode_info.underscan_property) {
		dm_new_state->underscan_enable = val;
		ret = 0;
	}

	return ret;
}

int amdgpu_dm_connector_atomic_get_property(struct drm_connector *connector,
					    const struct drm_connector_state *state,
					    struct drm_property *property,
					    uint64_t *val)
{
	struct drm_device *dev = connector->dev;
	struct amdgpu_device *adev = drm_to_adev(dev);
	struct dm_connector_state *dm_state =
		to_dm_connector_state(state);
	int ret = -EINVAL;

	if (property == dev->mode_config.scaling_mode_property) {
		switch (dm_state->scaling) {
		case RMX_CENTER:
			*val = DRM_MODE_SCALE_CENTER;
			break;
		case RMX_ASPECT:
			*val = DRM_MODE_SCALE_ASPECT;
			break;
		case RMX_FULL:
			*val = DRM_MODE_SCALE_FULLSCREEN;
			break;
		case RMX_OFF:
		default:
			*val = DRM_MODE_SCALE_NONE;
			break;
		}
		ret = 0;
	} else if (property == adev->mode_info.underscan_hborder_property) {
		*val = dm_state->underscan_hborder;
		ret = 0;
	} else if (property == adev->mode_info.underscan_vborder_property) {
		*val = dm_state->underscan_vborder;
		ret = 0;
	} else if (property == adev->mode_info.underscan_property) {
		*val = dm_state->underscan_enable;
		ret = 0;
	}

	return ret;
}

/**
 * DOC: panel power savings
 *
 * The display manager allows you to set your desired **panel power savings**
 * level (between 0-4, with 0 representing off), e.g. using the following::
 *
 *   # echo 3 > /sys/class/drm/card0-eDP-1/amdgpu/panel_power_savings
 *
 * Modifying this value can have implications on color accuracy, so tread
 * carefully.
 */

static ssize_t panel_power_savings_show(struct device *device,
					struct device_attribute *attr,
					char *buf)
{
	struct drm_connector *connector = dev_get_drvdata(device);
	struct drm_device *dev = connector->dev;
	u8 val;

	drm_modeset_lock(&dev->mode_config.connection_mutex, NULL);
	val = to_dm_connector_state(connector->state)->abm_level ==
		ABM_LEVEL_IMMEDIATE_DISABLE ? 0 :
		to_dm_connector_state(connector->state)->abm_level;
	drm_modeset_unlock(&dev->mode_config.connection_mutex);

	return sysfs_emit(buf, "%u\n", val);
}

#ifdef __linux__

static ssize_t panel_power_savings_store(struct device *device,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	struct drm_connector *connector = dev_get_drvdata(device);
	struct drm_device *dev = connector->dev;
	long val;
	int ret;

	ret = kstrtol(buf, 0, &val);

	if (ret)
		return ret;

	if (val < 0 || val > 4)
		return -EINVAL;

	drm_modeset_lock(&dev->mode_config.connection_mutex, NULL);
	to_dm_connector_state(connector->state)->abm_level = val ?:
		ABM_LEVEL_IMMEDIATE_DISABLE;
	drm_modeset_unlock(&dev->mode_config.connection_mutex);

	drm_kms_helper_hotplug_event(dev);

	return count;
}

static DEVICE_ATTR_RW(panel_power_savings);

static struct attribute *amdgpu_attrs[] = {
	&dev_attr_panel_power_savings.attr,
	NULL
};

static const struct attribute_group amdgpu_group = {
	.name = "amdgpu",
	.attrs = amdgpu_attrs
};

#endif

static bool
amdgpu_dm_should_create_sysfs(struct amdgpu_dm_connector *amdgpu_dm_connector)
{
	if (amdgpu_dm_abm_level >= 0)
		return false;

	if (amdgpu_dm_connector->base.connector_type != DRM_MODE_CONNECTOR_eDP)
		return false;

	/* check for OLED panels */
	if (amdgpu_dm_connector->bl_idx >= 0) {
		struct drm_device *drm = amdgpu_dm_connector->base.dev;
		struct amdgpu_display_manager *dm = &drm_to_adev(drm)->dm;
		struct amdgpu_dm_backlight_caps *caps;

		caps = &dm->backlight_caps[amdgpu_dm_connector->bl_idx];
		if (caps->aux_support)
			return false;
	}

	return true;
}

static void amdgpu_dm_connector_unregister(struct drm_connector *connector)
{
	struct amdgpu_dm_connector *amdgpu_dm_connector = to_amdgpu_dm_connector(connector);

	if (amdgpu_dm_should_create_sysfs(amdgpu_dm_connector))
		sysfs_remove_group(&connector->kdev->kobj, &amdgpu_group);

	drm_dp_aux_unregister(&amdgpu_dm_connector->dm_dp_aux.aux);
}

static void amdgpu_dm_connector_destroy(struct drm_connector *connector)
{
	struct amdgpu_dm_connector *aconnector = to_amdgpu_dm_connector(connector);
	struct amdgpu_device *adev = drm_to_adev(connector->dev);
	struct amdgpu_display_manager *dm = &adev->dm;

	/*
	 * Call only if mst_mgr was initialized before since it's not done
	 * for all connector types.
	 */
	if (aconnector->mst_mgr.dev)
		drm_dp_mst_topology_mgr_destroy(&aconnector->mst_mgr);

	if (aconnector->bl_idx != -1) {
		backlight_device_unregister(dm->backlight_dev[aconnector->bl_idx]);
		dm->backlight_dev[aconnector->bl_idx] = NULL;
	}

	if (aconnector->dc_em_sink)
		dc_sink_release(aconnector->dc_em_sink);
	aconnector->dc_em_sink = NULL;
	if (aconnector->dc_sink)
		dc_sink_release(aconnector->dc_sink);
	aconnector->dc_sink = NULL;

	drm_dp_cec_unregister_connector(&aconnector->dm_dp_aux.aux);
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
	if (aconnector->i2c) {
		i2c_del_adapter(&aconnector->i2c->base);
		kfree(aconnector->i2c);
	}
	kfree(aconnector->dm_dp_aux.aux.name);

	kfree(connector);
}

void amdgpu_dm_connector_funcs_reset(struct drm_connector *connector)
{
	struct dm_connector_state *state =
		to_dm_connector_state(connector->state);

	if (connector->state)
		__drm_atomic_helper_connector_destroy_state(connector->state);

	kfree(state);

	state = kzalloc(sizeof(*state), GFP_KERNEL);

	if (state) {
		state->scaling = RMX_OFF;
		state->underscan_enable = false;
		state->underscan_hborder = 0;
		state->underscan_vborder = 0;
		state->base.max_requested_bpc = 8;
		state->vcpi_slots = 0;
		state->pbn = 0;

		if (connector->connector_type == DRM_MODE_CONNECTOR_eDP) {
			if (amdgpu_dm_abm_level <= 0)
				state->abm_level = ABM_LEVEL_IMMEDIATE_DISABLE;
			else
				state->abm_level = amdgpu_dm_abm_level;
		}

		__drm_atomic_helper_connector_reset(connector, &state->base);
	}
}

struct drm_connector_state *
amdgpu_dm_connector_atomic_duplicate_state(struct drm_connector *connector)
{
	struct dm_connector_state *state =
		to_dm_connector_state(connector->state);

	struct dm_connector_state *new_state =
			kmemdup(state, sizeof(*state), GFP_KERNEL);

	if (!new_state)
		return NULL;

	__drm_atomic_helper_connector_duplicate_state(connector, &new_state->base);

	new_state->freesync_capable = state->freesync_capable;
	new_state->abm_level = state->abm_level;
	new_state->scaling = state->scaling;
	new_state->underscan_enable = state->underscan_enable;
	new_state->underscan_hborder = state->underscan_hborder;
	new_state->underscan_vborder = state->underscan_vborder;
	new_state->vcpi_slots = state->vcpi_slots;
	new_state->pbn = state->pbn;
	return &new_state->base;
}

static int
amdgpu_dm_connector_late_register(struct drm_connector *connector)
{
	struct amdgpu_dm_connector *amdgpu_dm_connector =
		to_amdgpu_dm_connector(connector);
	int r;

	if (amdgpu_dm_should_create_sysfs(amdgpu_dm_connector)) {
		r = sysfs_create_group(&connector->kdev->kobj,
				       &amdgpu_group);
		if (r)
			return r;
	}

	amdgpu_dm_register_backlight_device(amdgpu_dm_connector);

	if ((connector->connector_type == DRM_MODE_CONNECTOR_DisplayPort) ||
	    (connector->connector_type == DRM_MODE_CONNECTOR_eDP)) {
		amdgpu_dm_connector->dm_dp_aux.aux.dev = connector->kdev;
		r = drm_dp_aux_register(&amdgpu_dm_connector->dm_dp_aux.aux);
		if (r)
			return r;
	}

#if defined(CONFIG_DEBUG_FS)
	connector_debugfs_init(amdgpu_dm_connector);
#endif

	return 0;
}

static void amdgpu_dm_connector_funcs_force(struct drm_connector *connector)
{
	struct amdgpu_dm_connector *aconnector = to_amdgpu_dm_connector(connector);
	struct dc_link *dc_link = aconnector->dc_link;
	struct dc_sink *dc_em_sink = aconnector->dc_em_sink;
	struct edid *edid;
	struct i2c_adapter *ddc;

	if (dc_link && dc_link->aux_mode)
		ddc = &aconnector->dm_dp_aux.aux.ddc;
	else
		ddc = &aconnector->i2c->base;

	/*
	 * Note: drm_get_edid gets edid in the following order:
	 * 1) override EDID if set via edid_override debugfs,
	 * 2) firmware EDID if set via edid_firmware module parameter
	 * 3) regular DDC read.
	 */
	edid = drm_get_edid(connector, ddc);
	if (!edid) {
		DRM_ERROR("No EDID found on connector: %s.\n", connector->name);
		return;
	}

	aconnector->edid = edid;

	/* Update emulated (virtual) sink's EDID */
	if (dc_em_sink && dc_link) {
		memset(&dc_em_sink->edid_caps, 0, sizeof(struct dc_edid_caps));
		memmove(dc_em_sink->dc_edid.raw_edid, edid, (edid->extensions + 1) * EDID_LENGTH);
		dm_helpers_parse_edid_caps(
			dc_link,
			&dc_em_sink->dc_edid,
			&dc_em_sink->edid_caps);
	}
}

static const struct drm_connector_funcs amdgpu_dm_connector_funcs = {
	.reset = amdgpu_dm_connector_funcs_reset,
	.detect = amdgpu_dm_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = amdgpu_dm_connector_destroy,
	.atomic_duplicate_state = amdgpu_dm_connector_atomic_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
	.atomic_set_property = amdgpu_dm_connector_atomic_set_property,
	.atomic_get_property = amdgpu_dm_connector_atomic_get_property,
	.late_register = amdgpu_dm_connector_late_register,
	.early_unregister = amdgpu_dm_connector_unregister,
	.force = amdgpu_dm_connector_funcs_force
};

static int get_modes(struct drm_connector *connector)
{
	return amdgpu_dm_connector_get_modes(connector);
}

static void create_eml_sink(struct amdgpu_dm_connector *aconnector)
{
	struct drm_connector *connector = &aconnector->base;
	struct dc_link *dc_link = aconnector->dc_link;
	struct dc_sink_init_data init_params = {
			.link = aconnector->dc_link,
			.sink_signal = SIGNAL_TYPE_VIRTUAL
	};
	struct edid *edid;
	struct i2c_adapter *ddc;

	if (dc_link->aux_mode)
		ddc = &aconnector->dm_dp_aux.aux.ddc;
	else
		ddc = &aconnector->i2c->base;

	/*
	 * Note: drm_get_edid gets edid in the following order:
	 * 1) override EDID if set via edid_override debugfs,
	 * 2) firmware EDID if set via edid_firmware module parameter
	 * 3) regular DDC read.
	 */
	edid = drm_get_edid(connector, ddc);
	if (!edid) {
		DRM_ERROR("No EDID found on connector: %s.\n", connector->name);
		return;
	}

	if (drm_detect_hdmi_monitor(edid))
		init_params.sink_signal = SIGNAL_TYPE_HDMI_TYPE_A;

	aconnector->edid = edid;

	aconnector->dc_em_sink = dc_link_add_remote_sink(
		aconnector->dc_link,
		(uint8_t *)edid,
		(edid->extensions + 1) * EDID_LENGTH,
		&init_params);

	if (aconnector->base.force == DRM_FORCE_ON) {
		aconnector->dc_sink = aconnector->dc_link->local_sink ?
		aconnector->dc_link->local_sink :
		aconnector->dc_em_sink;
		if (aconnector->dc_sink)
			dc_sink_retain(aconnector->dc_sink);
	}
}

static void handle_edid_mgmt(struct amdgpu_dm_connector *aconnector)
{
	struct dc_link *link = (struct dc_link *)aconnector->dc_link;

	/*
	 * In case of headless boot with force on for DP managed connector
	 * Those settings have to be != 0 to get initial modeset
	 */
	if (link->connector_signal == SIGNAL_TYPE_DISPLAY_PORT) {
		link->verified_link_cap.lane_count = LANE_COUNT_FOUR;
		link->verified_link_cap.link_rate = LINK_RATE_HIGH2;
	}

	create_eml_sink(aconnector);
}

static enum dc_status dm_validate_stream_and_context(struct dc *dc,
						struct dc_stream_state *stream)
{
	enum dc_status dc_result = DC_ERROR_UNEXPECTED;
	struct dc_plane_state *dc_plane_state = NULL;
	struct dc_state *dc_state = NULL;

	if (!stream)
		goto cleanup;

	dc_plane_state = dc_create_plane_state(dc);
	if (!dc_plane_state)
		goto cleanup;

	dc_state = dc_state_create(dc, NULL);
	if (!dc_state)
		goto cleanup;

	/* populate stream to plane */
	dc_plane_state->src_rect.height  = stream->src.height;
	dc_plane_state->src_rect.width   = stream->src.width;
	dc_plane_state->dst_rect.height  = stream->src.height;
	dc_plane_state->dst_rect.width   = stream->src.width;
	dc_plane_state->clip_rect.height = stream->src.height;
	dc_plane_state->clip_rect.width  = stream->src.width;
	dc_plane_state->plane_size.surface_pitch = ((stream->src.width + 255) / 256) * 256;
	dc_plane_state->plane_size.surface_size.height = stream->src.height;
	dc_plane_state->plane_size.surface_size.width  = stream->src.width;
	dc_plane_state->plane_size.chroma_size.height  = stream->src.height;
	dc_plane_state->plane_size.chroma_size.width   = stream->src.width;
	dc_plane_state->format = SURFACE_PIXEL_FORMAT_GRPH_ARGB8888;
	dc_plane_state->tiling_info.gfx9.swizzle = DC_SW_UNKNOWN;
	dc_plane_state->rotation = ROTATION_ANGLE_0;
	dc_plane_state->is_tiling_rotated = false;
	dc_plane_state->tiling_info.gfx8.array_mode = DC_ARRAY_LINEAR_GENERAL;

	dc_result = dc_validate_stream(dc, stream);
	if (dc_result == DC_OK)
		dc_result = dc_validate_plane(dc, dc_plane_state);

	if (dc_result == DC_OK)
		dc_result = dc_state_add_stream(dc, dc_state, stream);

	if (dc_result == DC_OK && !dc_state_add_plane(
						dc,
						stream,
						dc_plane_state,
						dc_state))
		dc_result = DC_FAIL_ATTACH_SURFACES;

	if (dc_result == DC_OK)
		dc_result = dc_validate_global_state(dc, dc_state, true);

cleanup:
	if (dc_state)
		dc_state_release(dc_state);

	if (dc_plane_state)
		dc_plane_state_release(dc_plane_state);

	return dc_result;
}

struct dc_stream_state *
create_validate_stream_for_sink(struct drm_connector *connector,
				const struct drm_display_mode *drm_mode,
				const struct dm_connector_state *dm_state,
				const struct dc_stream_state *old_stream)
{
	struct amdgpu_dm_connector *aconnector = NULL;
	struct amdgpu_device *adev = drm_to_adev(connector->dev);
	struct dc_stream_state *stream;
	const struct drm_connector_state *drm_state = dm_state ? &dm_state->base : NULL;
	int requested_bpc = drm_state ? drm_state->max_requested_bpc : 8;
	enum dc_status dc_result = DC_OK;
	uint8_t bpc_limit = 6;

	if (!dm_state)
		return NULL;

	if (connector->connector_type != DRM_MODE_CONNECTOR_WRITEBACK)
		aconnector = to_amdgpu_dm_connector(connector);

	if (aconnector &&
	    (aconnector->dc_link->connector_signal == SIGNAL_TYPE_HDMI_TYPE_A ||
	     aconnector->dc_link->dpcd_caps.dongle_type == DISPLAY_DONGLE_DP_HDMI_CONVERTER))
		bpc_limit = 8;

	do {
		stream = create_stream_for_sink(connector, drm_mode,
						dm_state, old_stream,
						requested_bpc);
		if (stream == NULL) {
			DRM_ERROR("Failed to create stream for sink!\n");
			break;
		}

		dc_result = dc_validate_stream(adev->dm.dc, stream);

		if (!aconnector) /* writeback connector */
			return stream;

		if (dc_result == DC_OK && stream->signal == SIGNAL_TYPE_DISPLAY_PORT_MST)
			dc_result = dm_dp_mst_is_port_support_mode(aconnector, stream);

		if (dc_result == DC_OK)
			dc_result = dm_validate_stream_and_context(adev->dm.dc, stream);

		if (dc_result != DC_OK) {
			DRM_DEBUG_KMS("Mode %dx%d (clk %d) pixel_encoding:%s color_depth:%s failed validation -- %s\n",
				      drm_mode->hdisplay,
				      drm_mode->vdisplay,
				      drm_mode->clock,
				      dc_pixel_encoding_to_str(stream->timing.pixel_encoding),
				      dc_color_depth_to_str(stream->timing.display_color_depth),
				      dc_status_to_str(dc_result));

			dc_stream_release(stream);
			stream = NULL;
			requested_bpc -= 2; /* lower bpc to retry validation */
		}

	} while (stream == NULL && requested_bpc >= bpc_limit);

	if ((dc_result == DC_FAIL_ENC_VALIDATE ||
	     dc_result == DC_EXCEED_DONGLE_CAP) &&
	     !aconnector->force_yuv420_output) {
		DRM_DEBUG_KMS("%s:%d Retry forcing yuv420 encoding\n",
				     __func__, __LINE__);

		aconnector->force_yuv420_output = true;
		stream = create_validate_stream_for_sink(connector, drm_mode,
						dm_state, old_stream);
		aconnector->force_yuv420_output = false;
	}

	return stream;
}

enum drm_mode_status amdgpu_dm_connector_mode_valid(struct drm_connector *connector,
				   struct drm_display_mode *mode)
{
	int result = MODE_ERROR;
	struct dc_sink *dc_sink;
	/* TODO: Unhardcode stream count */
	struct dc_stream_state *stream;
	/* we always have an amdgpu_dm_connector here since we got
	 * here via the amdgpu_dm_connector_helper_funcs
	 */
	struct amdgpu_dm_connector *aconnector = to_amdgpu_dm_connector(connector);

	if ((mode->flags & DRM_MODE_FLAG_INTERLACE) ||
			(mode->flags & DRM_MODE_FLAG_DBLSCAN))
		return result;

	/*
	 * Only run this the first time mode_valid is called to initilialize
	 * EDID mgmt
	 */
	if (aconnector->base.force != DRM_FORCE_UNSPECIFIED &&
		!aconnector->dc_em_sink)
		handle_edid_mgmt(aconnector);

	dc_sink = to_amdgpu_dm_connector(connector)->dc_sink;

	if (dc_sink == NULL && aconnector->base.force != DRM_FORCE_ON_DIGITAL &&
				aconnector->base.force != DRM_FORCE_ON) {
		DRM_ERROR("dc_sink is NULL!\n");
		goto fail;
	}

	drm_mode_set_crtcinfo(mode, 0);

	stream = create_validate_stream_for_sink(connector, mode,
						 to_dm_connector_state(connector->state),
						 NULL);
	if (stream) {
		dc_stream_release(stream);
		result = MODE_OK;
	}

fail:
	/* TODO: error handling*/
	return result;
}

static int fill_hdr_info_packet(const struct drm_connector_state *state,
				struct dc_info_packet *out)
{
	struct hdmi_drm_infoframe frame;
	unsigned char buf[30]; /* 26 + 4 */
	ssize_t len;
	int ret, i;

	memset(out, 0, sizeof(*out));

	if (!state->hdr_output_metadata)
		return 0;

	ret = drm_hdmi_infoframe_set_hdr_metadata(&frame, state);
	if (ret)
		return ret;

	len = hdmi_drm_infoframe_pack_only(&frame, buf, sizeof(buf));
	if (len < 0)
		return (int)len;

	/* Static metadata is a fixed 26 bytes + 4 byte header. */
	if (len != 30)
		return -EINVAL;

	/* Prepare the infopacket for DC. */
	switch (state->connector->connector_type) {
	case DRM_MODE_CONNECTOR_HDMIA:
		out->hb0 = 0x87; /* type */
		out->hb1 = 0x01; /* version */
		out->hb2 = 0x1A; /* length */
		out->sb[0] = buf[3]; /* checksum */
		i = 1;
		break;

	case DRM_MODE_CONNECTOR_DisplayPort:
	case DRM_MODE_CONNECTOR_eDP:
		out->hb0 = 0x00; /* sdp id, zero */
		out->hb1 = 0x87; /* type */
		out->hb2 = 0x1D; /* payload len - 1 */
		out->hb3 = (0x13 << 2); /* sdp version */
		out->sb[0] = 0x01; /* version */
		out->sb[1] = 0x1A; /* length */
		i = 2;
		break;

	default:
		return -EINVAL;
	}

	memcpy(&out->sb[i], &buf[4], 26);
	out->valid = true;

	print_hex_dump(KERN_DEBUG, "HDR SB:", DUMP_PREFIX_NONE, 16, 1, out->sb,
		       sizeof(out->sb), false);

	return 0;
}

static int
amdgpu_dm_connector_atomic_check(struct drm_connector *conn,
				 struct drm_atomic_state *state)
{
	struct drm_connector_state *new_con_state =
		drm_atomic_get_new_connector_state(state, conn);
	struct drm_connector_state *old_con_state =
		drm_atomic_get_old_connector_state(state, conn);
	struct drm_crtc *crtc = new_con_state->crtc;
	struct drm_crtc_state *new_crtc_state;
	struct amdgpu_dm_connector *aconn = to_amdgpu_dm_connector(conn);
	int ret;

	if (WARN_ON(unlikely(!old_con_state || !new_con_state)))
		return -EINVAL;

	trace_amdgpu_dm_connector_atomic_check(new_con_state);

	if (conn->connector_type == DRM_MODE_CONNECTOR_DisplayPort) {
		ret = drm_dp_mst_root_conn_atomic_check(new_con_state, &aconn->mst_mgr);
		if (ret < 0)
			return ret;
	}

	if (!crtc)
		return 0;

	if (new_con_state->colorspace != old_con_state->colorspace) {
		new_crtc_state = drm_atomic_get_crtc_state(state, crtc);
		if (IS_ERR(new_crtc_state))
			return PTR_ERR(new_crtc_state);

		new_crtc_state->mode_changed = true;
	}

	if (new_con_state->content_type != old_con_state->content_type) {
		new_crtc_state = drm_atomic_get_crtc_state(state, crtc);
		if (IS_ERR(new_crtc_state))
			return PTR_ERR(new_crtc_state);

		new_crtc_state->mode_changed = true;
	}

	if (!drm_connector_atomic_hdr_metadata_equal(old_con_state, new_con_state)) {
		struct dc_info_packet hdr_infopacket;

		ret = fill_hdr_info_packet(new_con_state, &hdr_infopacket);
		if (ret)
			return ret;

		new_crtc_state = drm_atomic_get_crtc_state(state, crtc);
		if (IS_ERR(new_crtc_state))
			return PTR_ERR(new_crtc_state);

		/*
		 * DC considers the stream backends changed if the
		 * static metadata changes. Forcing the modeset also
		 * gives a simple way for userspace to switch from
		 * 8bpc to 10bpc when setting the metadata to enter
		 * or exit HDR.
		 *
		 * Changing the static metadata after it's been
		 * set is permissible, however. So only force a
		 * modeset if we're entering or exiting HDR.
		 */
		new_crtc_state->mode_changed = new_crtc_state->mode_changed ||
			!old_con_state->hdr_output_metadata ||
			!new_con_state->hdr_output_metadata;
	}

	return 0;
}

static const struct drm_connector_helper_funcs
amdgpu_dm_connector_helper_funcs = {
	/*
	 * If hotplugging a second bigger display in FB Con mode, bigger resolution
	 * modes will be filtered by drm_mode_validate_size(), and those modes
	 * are missing after user start lightdm. So we need to renew modes list.
	 * in get_modes call back, not just return the modes count
	 */
	.get_modes = get_modes,
	.mode_valid = amdgpu_dm_connector_mode_valid,
	.atomic_check = amdgpu_dm_connector_atomic_check,
};

static void dm_encoder_helper_disable(struct drm_encoder *encoder)
{

}

int convert_dc_color_depth_into_bpc(enum dc_color_depth display_color_depth)
{
	switch (display_color_depth) {
	case COLOR_DEPTH_666:
		return 6;
	case COLOR_DEPTH_888:
		return 8;
	case COLOR_DEPTH_101010:
		return 10;
	case COLOR_DEPTH_121212:
		return 12;
	case COLOR_DEPTH_141414:
		return 14;
	case COLOR_DEPTH_161616:
		return 16;
	default:
		break;
	}
	return 0;
}

static int dm_encoder_helper_atomic_check(struct drm_encoder *encoder,
					  struct drm_crtc_state *crtc_state,
					  struct drm_connector_state *conn_state)
{
	struct drm_atomic_state *state = crtc_state->state;
	struct drm_connector *connector = conn_state->connector;
	struct amdgpu_dm_connector *aconnector = to_amdgpu_dm_connector(connector);
	struct dm_connector_state *dm_new_connector_state = to_dm_connector_state(conn_state);
	const struct drm_display_mode *adjusted_mode = &crtc_state->adjusted_mode;
	struct drm_dp_mst_topology_mgr *mst_mgr;
	struct drm_dp_mst_port *mst_port;
	struct drm_dp_mst_topology_state *mst_state;
	enum dc_color_depth color_depth;
	int clock, bpp = 0;
	bool is_y420 = false;

	if (!aconnector->mst_output_port)
		return 0;

	mst_port = aconnector->mst_output_port;
	mst_mgr = &aconnector->mst_root->mst_mgr;

	if (!crtc_state->connectors_changed && !crtc_state->mode_changed)
		return 0;

	mst_state = drm_atomic_get_mst_topology_state(state, mst_mgr);
	if (IS_ERR(mst_state))
		return PTR_ERR(mst_state);

	mst_state->pbn_div.full = dfixed_const(dm_mst_get_pbn_divider(aconnector->mst_root->dc_link));

	if (!state->duplicated) {
		int max_bpc = conn_state->max_requested_bpc;

		is_y420 = drm_mode_is_420_also(&connector->display_info, adjusted_mode) &&
			  aconnector->force_yuv420_output;
		color_depth = convert_color_depth_from_display_info(connector,
								    is_y420,
								    max_bpc);
		bpp = convert_dc_color_depth_into_bpc(color_depth) * 3;
		clock = adjusted_mode->clock;
		dm_new_connector_state->pbn = drm_dp_calc_pbn_mode(clock, bpp << 4);
	}

	dm_new_connector_state->vcpi_slots =
		drm_dp_atomic_find_time_slots(state, mst_mgr, mst_port,
					      dm_new_connector_state->pbn);
	if (dm_new_connector_state->vcpi_slots < 0) {
		DRM_DEBUG_ATOMIC("failed finding vcpi slots: %d\n", (int)dm_new_connector_state->vcpi_slots);
		return dm_new_connector_state->vcpi_slots;
	}
	return 0;
}

const struct drm_encoder_helper_funcs amdgpu_dm_encoder_helper_funcs = {
	.disable = dm_encoder_helper_disable,
	.atomic_check = dm_encoder_helper_atomic_check
};

static int dm_update_mst_vcpi_slots_for_dsc(struct drm_atomic_state *state,
					    struct dc_state *dc_state,
					    struct dsc_mst_fairness_vars *vars)
{
	struct dc_stream_state *stream = NULL;
	struct drm_connector *connector;
	struct drm_connector_state *new_con_state;
	struct amdgpu_dm_connector *aconnector;
	struct dm_connector_state *dm_conn_state;
	int i, j, ret;
	int vcpi, pbn_div, pbn = 0, slot_num = 0;

	for_each_new_connector_in_state(state, connector, new_con_state, i) {

		if (connector->connector_type == DRM_MODE_CONNECTOR_WRITEBACK)
			continue;

		aconnector = to_amdgpu_dm_connector(connector);

		if (!aconnector->mst_output_port)
			continue;

		if (!new_con_state || !new_con_state->crtc)
			continue;

		dm_conn_state = to_dm_connector_state(new_con_state);

		for (j = 0; j < dc_state->stream_count; j++) {
			stream = dc_state->streams[j];
			if (!stream)
				continue;

			if ((struct amdgpu_dm_connector *)stream->dm_stream_context == aconnector)
				break;

			stream = NULL;
		}

		if (!stream)
			continue;

		pbn_div = dm_mst_get_pbn_divider(stream->link);
		/* pbn is calculated by compute_mst_dsc_configs_for_state*/
		for (j = 0; j < dc_state->stream_count; j++) {
			if (vars[j].aconnector == aconnector) {
				pbn = vars[j].pbn;
				break;
			}
		}

		if (j == dc_state->stream_count || pbn_div == 0)
			continue;

		slot_num = DIV_ROUND_UP(pbn, pbn_div);

		if (stream->timing.flags.DSC != 1) {
			dm_conn_state->pbn = pbn;
			dm_conn_state->vcpi_slots = slot_num;

			ret = drm_dp_mst_atomic_enable_dsc(state, aconnector->mst_output_port,
							   dm_conn_state->pbn, false);
			if (ret < 0)
				return ret;

			continue;
		}

		vcpi = drm_dp_mst_atomic_enable_dsc(state, aconnector->mst_output_port, pbn, true);
		if (vcpi < 0)
			return vcpi;

		dm_conn_state->pbn = pbn;
		dm_conn_state->vcpi_slots = vcpi;
	}
	return 0;
}

static int to_drm_connector_type(enum amd_signal_type st)
{
	switch (st) {
	case SIGNAL_TYPE_HDMI_TYPE_A:
		return DRM_MODE_CONNECTOR_HDMIA;
	case SIGNAL_TYPE_EDP:
		return DRM_MODE_CONNECTOR_eDP;
	case SIGNAL_TYPE_LVDS:
		return DRM_MODE_CONNECTOR_LVDS;
	case SIGNAL_TYPE_RGB:
		return DRM_MODE_CONNECTOR_VGA;
	case SIGNAL_TYPE_DISPLAY_PORT:
	case SIGNAL_TYPE_DISPLAY_PORT_MST:
		return DRM_MODE_CONNECTOR_DisplayPort;
	case SIGNAL_TYPE_DVI_DUAL_LINK:
	case SIGNAL_TYPE_DVI_SINGLE_LINK:
		return DRM_MODE_CONNECTOR_DVID;
	case SIGNAL_TYPE_VIRTUAL:
		return DRM_MODE_CONNECTOR_VIRTUAL;

	default:
		return DRM_MODE_CONNECTOR_Unknown;
	}
}

static struct drm_encoder *amdgpu_dm_connector_to_encoder(struct drm_connector *connector)
{
	struct drm_encoder *encoder;

	/* There is only one encoder per connector */
	drm_connector_for_each_possible_encoder(connector, encoder)
		return encoder;

	return NULL;
}

static void amdgpu_dm_get_native_mode(struct drm_connector *connector)
{
	struct drm_encoder *encoder;
	struct amdgpu_encoder *amdgpu_encoder;

	encoder = amdgpu_dm_connector_to_encoder(connector);

	if (encoder == NULL)
		return;

	amdgpu_encoder = to_amdgpu_encoder(encoder);

	amdgpu_encoder->native_mode.clock = 0;

	if (!list_empty(&connector->probed_modes)) {
		struct drm_display_mode *preferred_mode = NULL;

		list_for_each_entry(preferred_mode,
				    &connector->probed_modes,
				    head) {
			if (preferred_mode->type & DRM_MODE_TYPE_PREFERRED)
				amdgpu_encoder->native_mode = *preferred_mode;

			break;
		}

	}
}

static struct drm_display_mode *
amdgpu_dm_create_common_mode(struct drm_encoder *encoder,
			     char *name,
			     int hdisplay, int vdisplay)
{
	struct drm_device *dev = encoder->dev;
	struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(encoder);
	struct drm_display_mode *mode = NULL;
	struct drm_display_mode *native_mode = &amdgpu_encoder->native_mode;

	mode = drm_mode_duplicate(dev, native_mode);

	if (mode == NULL)
		return NULL;

	mode->hdisplay = hdisplay;
	mode->vdisplay = vdisplay;
	mode->type &= ~DRM_MODE_TYPE_PREFERRED;
	strscpy(mode->name, name, DRM_DISPLAY_MODE_LEN);

	return mode;

}

static void amdgpu_dm_connector_add_common_modes(struct drm_encoder *encoder,
						 struct drm_connector *connector)
{
	struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(encoder);
	struct drm_display_mode *mode = NULL;
	struct drm_display_mode *native_mode = &amdgpu_encoder->native_mode;
	struct amdgpu_dm_connector *amdgpu_dm_connector =
				to_amdgpu_dm_connector(connector);
	int i;
	int n;
	struct mode_size {
		char name[DRM_DISPLAY_MODE_LEN];
		int w;
		int h;
	} common_modes[] = {
		{  "640x480",  640,  480},
		{  "800x600",  800,  600},
		{ "1024x768", 1024,  768},
		{ "1280x720", 1280,  720},
		{ "1280x800", 1280,  800},
		{"1280x1024", 1280, 1024},
		{ "1440x900", 1440,  900},
		{"1680x1050", 1680, 1050},
		{"1600x1200", 1600, 1200},
		{"1920x1080", 1920, 1080},
		{"1920x1200", 1920, 1200}
	};

	n = ARRAY_SIZE(common_modes);

	for (i = 0; i < n; i++) {
		struct drm_display_mode *curmode = NULL;
		bool mode_existed = false;

		if (common_modes[i].w > native_mode->hdisplay ||
		    common_modes[i].h > native_mode->vdisplay ||
		   (common_modes[i].w == native_mode->hdisplay &&
		    common_modes[i].h == native_mode->vdisplay))
			continue;

		list_for_each_entry(curmode, &connector->probed_modes, head) {
			if (common_modes[i].w == curmode->hdisplay &&
			    common_modes[i].h == curmode->vdisplay) {
				mode_existed = true;
				break;
			}
		}

		if (mode_existed)
			continue;

		mode = amdgpu_dm_create_common_mode(encoder,
				common_modes[i].name, common_modes[i].w,
				common_modes[i].h);
		if (!mode)
			continue;

		drm_mode_probed_add(connector, mode);
		amdgpu_dm_connector->num_modes++;
	}
}

static void amdgpu_set_panel_orientation(struct drm_connector *connector)
{
	struct drm_encoder *encoder;
	struct amdgpu_encoder *amdgpu_encoder;
	const struct drm_display_mode *native_mode;

	if (connector->connector_type != DRM_MODE_CONNECTOR_eDP &&
	    connector->connector_type != DRM_MODE_CONNECTOR_LVDS)
		return;

	mutex_lock(&connector->dev->mode_config.mutex);
	amdgpu_dm_connector_get_modes(connector);
	mutex_unlock(&connector->dev->mode_config.mutex);

	encoder = amdgpu_dm_connector_to_encoder(connector);
	if (!encoder)
		return;

	amdgpu_encoder = to_amdgpu_encoder(encoder);

	native_mode = &amdgpu_encoder->native_mode;
	if (native_mode->hdisplay == 0 || native_mode->vdisplay == 0)
		return;

	drm_connector_set_panel_orientation_with_quirk(connector,
						       DRM_MODE_PANEL_ORIENTATION_UNKNOWN,
						       native_mode->hdisplay,
						       native_mode->vdisplay);
}

static void amdgpu_dm_connector_ddc_get_modes(struct drm_connector *connector,
					      struct edid *edid)
{
	struct amdgpu_dm_connector *amdgpu_dm_connector =
			to_amdgpu_dm_connector(connector);

	if (edid) {
		/* empty probed_modes */
		INIT_LIST_HEAD(&connector->probed_modes);
		amdgpu_dm_connector->num_modes =
				drm_add_edid_modes(connector, edid);

		/* sorting the probed modes before calling function
		 * amdgpu_dm_get_native_mode() since EDID can have
		 * more than one preferred mode. The modes that are
		 * later in the probed mode list could be of higher
		 * and preferred resolution. For example, 3840x2160
		 * resolution in base EDID preferred timing and 4096x2160
		 * preferred resolution in DID extension block later.
		 */
		drm_mode_sort(&connector->probed_modes);
		amdgpu_dm_get_native_mode(connector);

		/* Freesync capabilities are reset by calling
		 * drm_add_edid_modes() and need to be
		 * restored here.
		 */
		amdgpu_dm_update_freesync_caps(connector, edid);
	} else {
		amdgpu_dm_connector->num_modes = 0;
	}
}

static bool is_duplicate_mode(struct amdgpu_dm_connector *aconnector,
			      struct drm_display_mode *mode)
{
	struct drm_display_mode *m;

	list_for_each_entry(m, &aconnector->base.probed_modes, head) {
		if (drm_mode_equal(m, mode))
			return true;
	}

	return false;
}

static uint add_fs_modes(struct amdgpu_dm_connector *aconnector)
{
	const struct drm_display_mode *m;
	struct drm_display_mode *new_mode;
	uint i;
	u32 new_modes_count = 0;

	/* Standard FPS values
	 *
	 * 23.976       - TV/NTSC
	 * 24           - Cinema
	 * 25           - TV/PAL
	 * 29.97        - TV/NTSC
	 * 30           - TV/NTSC
	 * 48           - Cinema HFR
	 * 50           - TV/PAL
	 * 60           - Commonly used
	 * 48,72,96,120 - Multiples of 24
	 */
	static const u32 common_rates[] = {
		23976, 24000, 25000, 29970, 30000,
		48000, 50000, 60000, 72000, 96000, 120000
	};

	/*
	 * Find mode with highest refresh rate with the same resolution
	 * as the preferred mode. Some monitors report a preferred mode
	 * with lower resolution than the highest refresh rate supported.
	 */

	m = get_highest_refresh_rate_mode(aconnector, true);
	if (!m)
		return 0;

	for (i = 0; i < ARRAY_SIZE(common_rates); i++) {
		u64 target_vtotal, target_vtotal_diff;
		u64 num, den;

		if (drm_mode_vrefresh(m) * 1000 < common_rates[i])
			continue;

		if (common_rates[i] < aconnector->min_vfreq * 1000 ||
		    common_rates[i] > aconnector->max_vfreq * 1000)
			continue;

		num = (unsigned long long)m->clock * 1000 * 1000;
		den = common_rates[i] * (unsigned long long)m->htotal;
		target_vtotal = div_u64(num, den);
		target_vtotal_diff = target_vtotal - m->vtotal;

		/* Check for illegal modes */
		if (m->vsync_start + target_vtotal_diff < m->vdisplay ||
		    m->vsync_end + target_vtotal_diff < m->vsync_start ||
		    m->vtotal + target_vtotal_diff < m->vsync_end)
			continue;

		new_mode = drm_mode_duplicate(aconnector->base.dev, m);
		if (!new_mode)
			goto out;

		new_mode->vtotal += (u16)target_vtotal_diff;
		new_mode->vsync_start += (u16)target_vtotal_diff;
		new_mode->vsync_end += (u16)target_vtotal_diff;
		new_mode->type &= ~DRM_MODE_TYPE_PREFERRED;
		new_mode->type |= DRM_MODE_TYPE_DRIVER;

		if (!is_duplicate_mode(aconnector, new_mode)) {
			drm_mode_probed_add(&aconnector->base, new_mode);
			new_modes_count += 1;
		} else
			drm_mode_destroy(aconnector->base.dev, new_mode);
	}
 out:
	return new_modes_count;
}

static void amdgpu_dm_connector_add_freesync_modes(struct drm_connector *connector,
						   struct edid *edid)
{
	struct amdgpu_dm_connector *amdgpu_dm_connector =
		to_amdgpu_dm_connector(connector);

	if (!(amdgpu_freesync_vid_mode && edid))
		return;

	if (amdgpu_dm_connector->max_vfreq - amdgpu_dm_connector->min_vfreq > 10)
		amdgpu_dm_connector->num_modes +=
			add_fs_modes(amdgpu_dm_connector);
}

static int amdgpu_dm_connector_get_modes(struct drm_connector *connector)
{
	struct amdgpu_dm_connector *amdgpu_dm_connector =
			to_amdgpu_dm_connector(connector);
	struct drm_encoder *encoder;
	struct edid *edid = amdgpu_dm_connector->edid;
	struct dc_link_settings *verified_link_cap =
			&amdgpu_dm_connector->dc_link->verified_link_cap;
	const struct dc *dc = amdgpu_dm_connector->dc_link->dc;

	encoder = amdgpu_dm_connector_to_encoder(connector);

	if (!drm_edid_is_valid(edid)) {
		amdgpu_dm_connector->num_modes =
				drm_add_modes_noedid(connector, 640, 480);
		if (dc->link_srv->dp_get_encoding_format(verified_link_cap) == DP_128b_132b_ENCODING)
			amdgpu_dm_connector->num_modes +=
				drm_add_modes_noedid(connector, 1920, 1080);
	} else {
		amdgpu_dm_connector_ddc_get_modes(connector, edid);
		if (encoder)
			amdgpu_dm_connector_add_common_modes(encoder, connector);
		amdgpu_dm_connector_add_freesync_modes(connector, edid);
	}
	amdgpu_dm_fbc_init(connector);

	return amdgpu_dm_connector->num_modes;
}

static const u32 supported_colorspaces =
	BIT(DRM_MODE_COLORIMETRY_BT709_YCC) |
	BIT(DRM_MODE_COLORIMETRY_OPRGB) |
	BIT(DRM_MODE_COLORIMETRY_BT2020_RGB) |
	BIT(DRM_MODE_COLORIMETRY_BT2020_YCC);

void amdgpu_dm_connector_init_helper(struct amdgpu_display_manager *dm,
				     struct amdgpu_dm_connector *aconnector,
				     int connector_type,
				     struct dc_link *link,
				     int link_index)
{
	struct amdgpu_device *adev = drm_to_adev(dm->ddev);

	/*
	 * Some of the properties below require access to state, like bpc.
	 * Allocate some default initial connector state with our reset helper.
	 */
	if (aconnector->base.funcs->reset)
		aconnector->base.funcs->reset(&aconnector->base);

	aconnector->connector_id = link_index;
	aconnector->bl_idx = -1;
	aconnector->dc_link = link;
	aconnector->base.interlace_allowed = false;
	aconnector->base.doublescan_allowed = false;
	aconnector->base.stereo_allowed = false;
	aconnector->base.dpms = DRM_MODE_DPMS_OFF;
	aconnector->hpd.hpd = AMDGPU_HPD_NONE; /* not used */
	aconnector->audio_inst = -1;
	aconnector->pack_sdp_v1_3 = false;
	aconnector->as_type = ADAPTIVE_SYNC_TYPE_NONE;
	memset(&aconnector->vsdb_info, 0, sizeof(aconnector->vsdb_info));
	rw_init(&aconnector->hpd_lock, "dmhpd");
	rw_init(&aconnector->handle_mst_msg_ready, "dmmr");

	/*
	 * configure support HPD hot plug connector_>polled default value is 0
	 * which means HPD hot plug not supported
	 */
	switch (connector_type) {
	case DRM_MODE_CONNECTOR_HDMIA:
		aconnector->base.polled = DRM_CONNECTOR_POLL_HPD;
		aconnector->base.ycbcr_420_allowed =
			link->link_enc->features.hdmi_ycbcr420_supported ? true : false;
		break;
	case DRM_MODE_CONNECTOR_DisplayPort:
		aconnector->base.polled = DRM_CONNECTOR_POLL_HPD;
		link->link_enc = link_enc_cfg_get_link_enc(link);
		ASSERT(link->link_enc);
		if (link->link_enc)
			aconnector->base.ycbcr_420_allowed =
			link->link_enc->features.dp_ycbcr420_supported ? true : false;
		break;
	case DRM_MODE_CONNECTOR_DVID:
		aconnector->base.polled = DRM_CONNECTOR_POLL_HPD;
		break;
	default:
		break;
	}

	drm_object_attach_property(&aconnector->base.base,
				dm->ddev->mode_config.scaling_mode_property,
				DRM_MODE_SCALE_NONE);

	drm_object_attach_property(&aconnector->base.base,
				adev->mode_info.underscan_property,
				UNDERSCAN_OFF);
	drm_object_attach_property(&aconnector->base.base,
				adev->mode_info.underscan_hborder_property,
				0);
	drm_object_attach_property(&aconnector->base.base,
				adev->mode_info.underscan_vborder_property,
				0);

	if (!aconnector->mst_root)
		drm_connector_attach_max_bpc_property(&aconnector->base, 8, 16);

	aconnector->base.state->max_bpc = 16;
	aconnector->base.state->max_requested_bpc = aconnector->base.state->max_bpc;

	if (connector_type == DRM_MODE_CONNECTOR_HDMIA) {
		/* Content Type is currently only implemented for HDMI. */
		drm_connector_attach_content_type_property(&aconnector->base);
	}

	if (connector_type == DRM_MODE_CONNECTOR_HDMIA) {
		if (!drm_mode_create_hdmi_colorspace_property(&aconnector->base, supported_colorspaces))
			drm_connector_attach_colorspace_property(&aconnector->base);
	} else if ((connector_type == DRM_MODE_CONNECTOR_DisplayPort && !aconnector->mst_root) ||
		   connector_type == DRM_MODE_CONNECTOR_eDP) {
		if (!drm_mode_create_dp_colorspace_property(&aconnector->base, supported_colorspaces))
			drm_connector_attach_colorspace_property(&aconnector->base);
	}

	if (connector_type == DRM_MODE_CONNECTOR_HDMIA ||
	    connector_type == DRM_MODE_CONNECTOR_DisplayPort ||
	    connector_type == DRM_MODE_CONNECTOR_eDP) {
		drm_connector_attach_hdr_output_metadata_property(&aconnector->base);

		if (!aconnector->mst_root)
			drm_connector_attach_vrr_capable_property(&aconnector->base);

		if (adev->dm.hdcp_workqueue)
			drm_connector_attach_content_protection_property(&aconnector->base, true);
	}
}

static int amdgpu_dm_i2c_xfer(struct i2c_adapter *i2c_adap,
			      struct i2c_msg *msgs, int num)
{
	struct amdgpu_i2c_adapter *i2c = i2c_get_adapdata(i2c_adap);
	struct ddc_service *ddc_service = i2c->ddc_service;
	struct i2c_command cmd;
	int i;
	int result = -EIO;

	if (!ddc_service->ddc_pin)
		return result;

	cmd.payloads = kcalloc(num, sizeof(struct i2c_payload), GFP_KERNEL);

	if (!cmd.payloads)
		return result;

	cmd.number_of_payloads = num;
	cmd.engine = I2C_COMMAND_ENGINE_DEFAULT;
	cmd.speed = 100;

	for (i = 0; i < num; i++) {
		cmd.payloads[i].write = !(msgs[i].flags & I2C_M_RD);
		cmd.payloads[i].address = msgs[i].addr;
		cmd.payloads[i].length = msgs[i].len;
		cmd.payloads[i].data = msgs[i].buf;
	}

	if (dc_submit_i2c(
			ddc_service->ctx->dc,
			ddc_service->link->link_index,
			&cmd))
		result = num;

	kfree(cmd.payloads);
	return result;
}

static u32 amdgpu_dm_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm amdgpu_dm_i2c_algo = {
	.master_xfer = amdgpu_dm_i2c_xfer,
	.functionality = amdgpu_dm_i2c_func,
};

static struct amdgpu_i2c_adapter *
create_i2c(struct ddc_service *ddc_service,
	   int link_index,
	   int *res)
{
	struct amdgpu_device *adev = ddc_service->ctx->driver_context;
	struct amdgpu_i2c_adapter *i2c;

	i2c = kzalloc(sizeof(struct amdgpu_i2c_adapter), GFP_KERNEL);
	if (!i2c)
		return NULL;
#ifdef notyet
	i2c->base.owner = THIS_MODULE;
	i2c->base.dev.parent = &adev->pdev->dev;
#endif
	i2c->base.algo = &amdgpu_dm_i2c_algo;
	snprintf(i2c->base.name, sizeof(i2c->base.name), "AMDGPU DM i2c hw bus %d", link_index);
	i2c_set_adapdata(&i2c->base, i2c);
	i2c->ddc_service = ddc_service;

	return i2c;
}


/*
 * Note: this function assumes that dc_link_detect() was called for the
 * dc_link which will be represented by this aconnector.
 */
static int amdgpu_dm_connector_init(struct amdgpu_display_manager *dm,
				    struct amdgpu_dm_connector *aconnector,
				    u32 link_index,
				    struct amdgpu_encoder *aencoder)
{
	int res = 0;
	int connector_type;
	struct dc *dc = dm->dc;
	struct dc_link *link = dc_get_link_at_index(dc, link_index);
	struct amdgpu_i2c_adapter *i2c;

	/* Not needed for writeback connector */
	link->priv = aconnector;


	i2c = create_i2c(link->ddc, link->link_index, &res);
	if (!i2c) {
		DRM_ERROR("Failed to create i2c adapter data\n");
		return -ENOMEM;
	}

	aconnector->i2c = i2c;
	res = i2c_add_adapter(&i2c->base);

	if (res) {
		DRM_ERROR("Failed to register hw i2c %d\n", link->link_index);
		goto out_free;
	}

	connector_type = to_drm_connector_type(link->connector_signal);

	res = drm_connector_init_with_ddc(
			dm->ddev,
			&aconnector->base,
			&amdgpu_dm_connector_funcs,
			connector_type,
			&i2c->base);

	if (res) {
		DRM_ERROR("connector_init failed\n");
		aconnector->connector_id = -1;
		goto out_free;
	}

	drm_connector_helper_add(
			&aconnector->base,
			&amdgpu_dm_connector_helper_funcs);

	amdgpu_dm_connector_init_helper(
		dm,
		aconnector,
		connector_type,
		link,
		link_index);

	drm_connector_attach_encoder(
		&aconnector->base, &aencoder->base);

	if (connector_type == DRM_MODE_CONNECTOR_DisplayPort
		|| connector_type == DRM_MODE_CONNECTOR_eDP)
		amdgpu_dm_initialize_dp_connector(dm, aconnector, link->link_index);

out_free:
	if (res) {
		kfree(i2c);
		aconnector->i2c = NULL;
	}
	return res;
}

int amdgpu_dm_get_encoder_crtc_mask(struct amdgpu_device *adev)
{
	switch (adev->mode_info.num_crtc) {
	case 1:
		return 0x1;
	case 2:
		return 0x3;
	case 3:
		return 0x7;
	case 4:
		return 0xf;
	case 5:
		return 0x1f;
	case 6:
	default:
		return 0x3f;
	}
}

static int amdgpu_dm_encoder_init(struct drm_device *dev,
				  struct amdgpu_encoder *aencoder,
				  uint32_t link_index)
{
	struct amdgpu_device *adev = drm_to_adev(dev);

	int res = drm_encoder_init(dev,
				   &aencoder->base,
				   &amdgpu_dm_encoder_funcs,
				   DRM_MODE_ENCODER_TMDS,
				   NULL);

	aencoder->base.possible_crtcs = amdgpu_dm_get_encoder_crtc_mask(adev);

	if (!res)
		aencoder->encoder_id = link_index;
	else
		aencoder->encoder_id = -1;

	drm_encoder_helper_add(&aencoder->base, &amdgpu_dm_encoder_helper_funcs);

	return res;
}

static void manage_dm_interrupts(struct amdgpu_device *adev,
				 struct amdgpu_crtc *acrtc,
				 struct dm_crtc_state *acrtc_state)
{
	struct drm_vblank_crtc_config config = {0};
	struct dc_crtc_timing *timing;
	int offdelay;

	if (acrtc_state) {
		timing = &acrtc_state->stream->timing;

		/*
		 * Depending on when the HW latching event of double-buffered
		 * registers happen relative to the PSR SDP deadline, and how
		 * bad the Panel clock has drifted since the last ALPM off
		 * event, there can be up to 3 frames of delay between sending
		 * the PSR exit cmd to DMUB fw, and when the panel starts
		 * displaying live frames.
		 *
		 * We can set:
		 *
		 * 20/100 * offdelay_ms = 3_frames_ms
		 * => offdelay_ms = 5 * 3_frames_ms
		 *
		 * This ensures that `3_frames_ms` will only be experienced as a
		 * 20% delay on top how long the display has been static, and
		 * thus make the delay less perceivable.
		 */
		if (acrtc_state->stream->link->psr_settings.psr_version <
		    DC_PSR_VERSION_UNSUPPORTED) {
			offdelay = DIV64_U64_ROUND_UP((u64)5 * 3 * 10 *
						      timing->v_total *
						      timing->h_total,
						      timing->pix_clk_100hz);
			config.offdelay_ms = offdelay ?: 30;
		} else if (amdgpu_ip_version(adev, DCE_HWIP, 0) <
			   IP_VERSION(3, 5, 0) ||
			   !(adev->flags & AMD_IS_APU)) {
			/*
			 * Older HW and DGPU have issues with instant off;
			 * use a 2 frame offdelay.
			 */
			offdelay = DIV64_U64_ROUND_UP((u64)20 *
						      timing->v_total *
						      timing->h_total,
						      timing->pix_clk_100hz);

			config.offdelay_ms = offdelay ?: 30;
		} else {
			/* offdelay_ms = 0 will never disable vblank */
			config.offdelay_ms = 1;
			config.disable_immediate = true;
		}

		drm_crtc_vblank_on_config(&acrtc->base,
					  &config);
	} else {
		drm_crtc_vblank_off(&acrtc->base);
	}
}

static void dm_update_pflip_irq_state(struct amdgpu_device *adev,
				      struct amdgpu_crtc *acrtc)
{
	int irq_type =
		amdgpu_display_crtc_idx_to_irq_type(adev, acrtc->crtc_id);

	/**
	 * This reads the current state for the IRQ and force reapplies
	 * the setting to hardware.
	 */
	amdgpu_irq_update(adev, &adev->pageflip_irq, irq_type);
}

static bool
is_scaling_state_different(const struct dm_connector_state *dm_state,
			   const struct dm_connector_state *old_dm_state)
{
	if (dm_state->scaling != old_dm_state->scaling)
		return true;
	if (!dm_state->underscan_enable && old_dm_state->underscan_enable) {
		if (old_dm_state->underscan_hborder != 0 && old_dm_state->underscan_vborder != 0)
			return true;
	} else  if (dm_state->underscan_enable && !old_dm_state->underscan_enable) {
		if (dm_state->underscan_hborder != 0 && dm_state->underscan_vborder != 0)
			return true;
	} else if (dm_state->underscan_hborder != old_dm_state->underscan_hborder ||
		   dm_state->underscan_vborder != old_dm_state->underscan_vborder)
		return true;
	return false;
}

static bool is_content_protection_different(struct drm_crtc_state *new_crtc_state,
					    struct drm_crtc_state *old_crtc_state,
					    struct drm_connector_state *new_conn_state,
					    struct drm_connector_state *old_conn_state,
					    const struct drm_connector *connector,
					    struct hdcp_workqueue *hdcp_w)
{
	struct amdgpu_dm_connector *aconnector = to_amdgpu_dm_connector(connector);
	struct dm_connector_state *dm_con_state = to_dm_connector_state(connector->state);

	pr_debug("[HDCP_DM] connector->index: %x connect_status: %x dpms: %x\n",
		connector->index, connector->status, connector->dpms);
	pr_debug("[HDCP_DM] state protection old: %x new: %x\n",
		old_conn_state->content_protection, new_conn_state->content_protection);

	if (old_crtc_state)
		pr_debug("[HDCP_DM] old crtc en: %x a: %x m: %x a-chg: %x c-chg: %x\n",
		old_crtc_state->enable,
		old_crtc_state->active,
		old_crtc_state->mode_changed,
		old_crtc_state->active_changed,
		old_crtc_state->connectors_changed);

	if (new_crtc_state)
		pr_debug("[HDCP_DM] NEW crtc en: %x a: %x m: %x a-chg: %x c-chg: %x\n",
		new_crtc_state->enable,
		new_crtc_state->active,
		new_crtc_state->mode_changed,
		new_crtc_state->active_changed,
		new_crtc_state->connectors_changed);

	/* hdcp content type change */
	if (old_conn_state->hdcp_content_type != new_conn_state->hdcp_content_type &&
	    new_conn_state->content_protection != DRM_MODE_CONTENT_PROTECTION_UNDESIRED) {
		new_conn_state->content_protection = DRM_MODE_CONTENT_PROTECTION_DESIRED;
		pr_debug("[HDCP_DM] Type0/1 change %s :true\n", __func__);
		return true;
	}

	/* CP is being re enabled, ignore this */
	if (old_conn_state->content_protection == DRM_MODE_CONTENT_PROTECTION_ENABLED &&
	    new_conn_state->content_protection == DRM_MODE_CONTENT_PROTECTION_DESIRED) {
		if (new_crtc_state && new_crtc_state->mode_changed) {
			new_conn_state->content_protection = DRM_MODE_CONTENT_PROTECTION_DESIRED;
			pr_debug("[HDCP_DM] ENABLED->DESIRED & mode_changed %s :true\n", __func__);
			return true;
		}
		new_conn_state->content_protection = DRM_MODE_CONTENT_PROTECTION_ENABLED;
		pr_debug("[HDCP_DM] ENABLED -> DESIRED %s :false\n", __func__);
		return false;
	}

	/* S3 resume case, since old state will always be 0 (UNDESIRED) and the restored state will be ENABLED
	 *
	 * Handles:	UNDESIRED -> ENABLED
	 */
	if (old_conn_state->content_protection == DRM_MODE_CONTENT_PROTECTION_UNDESIRED &&
	    new_conn_state->content_protection == DRM_MODE_CONTENT_PROTECTION_ENABLED)
		new_conn_state->content_protection = DRM_MODE_CONTENT_PROTECTION_DESIRED;

	/* Stream removed and re-enabled
	 *
	 * Can sometimes overlap with the HPD case,
	 * thus set update_hdcp to false to avoid
	 * setting HDCP multiple times.
	 *
	 * Handles:	DESIRED -> DESIRED (Special case)
	 */
	if (!(old_conn_state->crtc && old_conn_state->crtc->enabled) &&
		new_conn_state->crtc && new_conn_state->crtc->enabled &&
		connector->state->content_protection == DRM_MODE_CONTENT_PROTECTION_DESIRED) {
		dm_con_state->update_hdcp = false;
		pr_debug("[HDCP_DM] DESIRED->DESIRED (Stream removed and re-enabled) %s :true\n",
			__func__);
		return true;
	}

	/* Hot-plug, headless s3, dpms
	 *
	 * Only start HDCP if the display is connected/enabled.
	 * update_hdcp flag will be set to false until the next
	 * HPD comes in.
	 *
	 * Handles:	DESIRED -> DESIRED (Special case)
	 */
	if (dm_con_state->update_hdcp &&
	new_conn_state->content_protection == DRM_MODE_CONTENT_PROTECTION_DESIRED &&
	connector->dpms == DRM_MODE_DPMS_ON && aconnector->dc_sink != NULL) {
		dm_con_state->update_hdcp = false;
		pr_debug("[HDCP_DM] DESIRED->DESIRED (Hot-plug, headless s3, dpms) %s :true\n",
			__func__);
		return true;
	}

	if (old_conn_state->content_protection == new_conn_state->content_protection) {
		if (new_conn_state->content_protection >= DRM_MODE_CONTENT_PROTECTION_DESIRED) {
			if (new_crtc_state && new_crtc_state->mode_changed) {
				pr_debug("[HDCP_DM] DESIRED->DESIRED or ENABLE->ENABLE mode_change %s :true\n",
					__func__);
				return true;
			}
			pr_debug("[HDCP_DM] DESIRED->DESIRED & ENABLE->ENABLE %s :false\n",
				__func__);
			return false;
		}

		pr_debug("[HDCP_DM] UNDESIRED->UNDESIRED %s :false\n", __func__);
		return false;
	}

	if (new_conn_state->content_protection != DRM_MODE_CONTENT_PROTECTION_ENABLED) {
		pr_debug("[HDCP_DM] UNDESIRED->DESIRED or DESIRED->UNDESIRED or ENABLED->UNDESIRED %s :true\n",
			__func__);
		return true;
	}

	pr_debug("[HDCP_DM] DESIRED->ENABLED %s :false\n", __func__);
	return false;
}

static void remove_stream(struct amdgpu_device *adev,
			  struct amdgpu_crtc *acrtc,
			  struct dc_stream_state *stream)
{
	/* this is the update mode case */

	acrtc->otg_inst = -1;
	acrtc->enabled = false;
}

static void prepare_flip_isr(struct amdgpu_crtc *acrtc)
{

	assert_spin_locked(&acrtc->base.dev->event_lock);
	WARN_ON(acrtc->event);

	acrtc->event = acrtc->base.state->event;

	/* Set the flip status */
	acrtc->pflip_status = AMDGPU_FLIP_SUBMITTED;

	/* Mark this event as consumed */
	acrtc->base.state->event = NULL;

	drm_dbg_state(acrtc->base.dev,
		      "crtc:%d, pflip_stat:AMDGPU_FLIP_SUBMITTED\n",
		      acrtc->crtc_id);
}

static void update_freesync_state_on_stream(
	struct amdgpu_display_manager *dm,
	struct dm_crtc_state *new_crtc_state,
	struct dc_stream_state *new_stream,
	struct dc_plane_state *surface,
	u32 flip_timestamp_in_us)
{
	struct mod_vrr_params vrr_params;
	struct dc_info_packet vrr_infopacket = {0};
	struct amdgpu_device *adev = dm->adev;
	struct amdgpu_crtc *acrtc = to_amdgpu_crtc(new_crtc_state->base.crtc);
	unsigned long flags;
	bool pack_sdp_v1_3 = false;
	struct amdgpu_dm_connector *aconn;
	enum vrr_packet_type packet_type = PACKET_TYPE_VRR;

	if (!new_stream)
		return;

	/*
	 * TODO: Determine why min/max totals and vrefresh can be 0 here.
	 * For now it's sufficient to just guard against these conditions.
	 */

	if (!new_stream->timing.h_total || !new_stream->timing.v_total)
		return;

	spin_lock_irqsave(&adev_to_drm(adev)->event_lock, flags);
	vrr_params = acrtc->dm_irq_params.vrr_params;

	if (surface) {
		mod_freesync_handle_preflip(
			dm->freesync_module,
			surface,
			new_stream,
			flip_timestamp_in_us,
			&vrr_params);

		if (adev->family < AMDGPU_FAMILY_AI &&
		    amdgpu_dm_crtc_vrr_active(new_crtc_state)) {
			mod_freesync_handle_v_update(dm->freesync_module,
						     new_stream, &vrr_params);

			/* Need to call this before the frame ends. */
			dc_stream_adjust_vmin_vmax(dm->dc,
						   new_crtc_state->stream,
						   &vrr_params.adjust);
		}
	}

	aconn = (struct amdgpu_dm_connector *)new_stream->dm_stream_context;

	if (aconn && (aconn->as_type == FREESYNC_TYPE_PCON_IN_WHITELIST || aconn->vsdb_info.replay_mode)) {
		pack_sdp_v1_3 = aconn->pack_sdp_v1_3;

		if (aconn->vsdb_info.amd_vsdb_version == 1)
			packet_type = PACKET_TYPE_FS_V1;
		else if (aconn->vsdb_info.amd_vsdb_version == 2)
			packet_type = PACKET_TYPE_FS_V2;
		else if (aconn->vsdb_info.amd_vsdb_version == 3)
			packet_type = PACKET_TYPE_FS_V3;

		mod_build_adaptive_sync_infopacket(new_stream, aconn->as_type, NULL,
					&new_stream->adaptive_sync_infopacket);
	}

	mod_freesync_build_vrr_infopacket(
		dm->freesync_module,
		new_stream,
		&vrr_params,
		packet_type,
		TRANSFER_FUNC_UNKNOWN,
		&vrr_infopacket,
		pack_sdp_v1_3);

	new_crtc_state->freesync_vrr_info_changed |=
		(memcmp(&new_crtc_state->vrr_infopacket,
			&vrr_infopacket,
			sizeof(vrr_infopacket)) != 0);

	acrtc->dm_irq_params.vrr_params = vrr_params;
	new_crtc_state->vrr_infopacket = vrr_infopacket;

	new_stream->vrr_infopacket = vrr_infopacket;
	new_stream->allow_freesync = mod_freesync_get_freesync_enabled(&vrr_params);

	if (new_crtc_state->freesync_vrr_info_changed)
		DRM_DEBUG_KMS("VRR packet update: crtc=%u enabled=%d state=%d",
			      new_crtc_state->base.crtc->base.id,
			      (int)new_crtc_state->base.vrr_enabled,
			      (int)vrr_params.state);

	spin_unlock_irqrestore(&adev_to_drm(adev)->event_lock, flags);
}

static void update_stream_irq_parameters(
	struct amdgpu_display_manager *dm,
	struct dm_crtc_state *new_crtc_state)
{
	struct dc_stream_state *new_stream = new_crtc_state->stream;
	struct mod_vrr_params vrr_params;
	struct mod_freesync_config config = new_crtc_state->freesync_config;
	struct amdgpu_device *adev = dm->adev;
	struct amdgpu_crtc *acrtc = to_amdgpu_crtc(new_crtc_state->base.crtc);
	unsigned long flags;

	if (!new_stream)
		return;

	/*
	 * TODO: Determine why min/max totals and vrefresh can be 0 here.
	 * For now it's sufficient to just guard against these conditions.
	 */
	if (!new_stream->timing.h_total || !new_stream->timing.v_total)
		return;

	spin_lock_irqsave(&adev_to_drm(adev)->event_lock, flags);
	vrr_params = acrtc->dm_irq_params.vrr_params;

	if (new_crtc_state->vrr_supported &&
	    config.min_refresh_in_uhz &&
	    config.max_refresh_in_uhz) {
		/*
		 * if freesync compatible mode was set, config.state will be set
		 * in atomic check
		 */
		if (config.state == VRR_STATE_ACTIVE_FIXED && config.fixed_refresh_in_uhz &&
		    (!drm_atomic_crtc_needs_modeset(&new_crtc_state->base) ||
		     new_crtc_state->freesync_config.state == VRR_STATE_ACTIVE_FIXED)) {
			vrr_params.max_refresh_in_uhz = config.max_refresh_in_uhz;
			vrr_params.min_refresh_in_uhz = config.min_refresh_in_uhz;
			vrr_params.fixed_refresh_in_uhz = config.fixed_refresh_in_uhz;
			vrr_params.state = VRR_STATE_ACTIVE_FIXED;
		} else {
			config.state = new_crtc_state->base.vrr_enabled ?
						     VRR_STATE_ACTIVE_VARIABLE :
						     VRR_STATE_INACTIVE;
		}
	} else {
		config.state = VRR_STATE_UNSUPPORTED;
	}

	mod_freesync_build_vrr_params(dm->freesync_module,
				      new_stream,
				      &config, &vrr_params);

	new_crtc_state->freesync_config = config;
	/* Copy state for access from DM IRQ handler */
	acrtc->dm_irq_params.freesync_config = config;
	acrtc->dm_irq_params.active_planes = new_crtc_state->active_planes;
	acrtc->dm_irq_params.vrr_params = vrr_params;
	spin_unlock_irqrestore(&adev_to_drm(adev)->event_lock, flags);
}

static void amdgpu_dm_handle_vrr_transition(struct dm_crtc_state *old_state,
					    struct dm_crtc_state *new_state)
{
	bool old_vrr_active = amdgpu_dm_crtc_vrr_active(old_state);
	bool new_vrr_active = amdgpu_dm_crtc_vrr_active(new_state);

	if (!old_vrr_active && new_vrr_active) {
		/* Transition VRR inactive -> active:
		 * While VRR is active, we must not disable vblank irq, as a
		 * reenable after disable would compute bogus vblank/pflip
		 * timestamps if it likely happened inside display front-porch.
		 *
		 * We also need vupdate irq for the actual core vblank handling
		 * at end of vblank.
		 */
		WARN_ON(amdgpu_dm_crtc_set_vupdate_irq(new_state->base.crtc, true) != 0);
		WARN_ON(drm_crtc_vblank_get(new_state->base.crtc) != 0);
		DRM_DEBUG_DRIVER("%s: crtc=%u VRR off->on: Get vblank ref\n",
				 __func__, new_state->base.crtc->base.id);
	} else if (old_vrr_active && !new_vrr_active) {
		/* Transition VRR active -> inactive:
		 * Allow vblank irq disable again for fixed refresh rate.
		 */
		WARN_ON(amdgpu_dm_crtc_set_vupdate_irq(new_state->base.crtc, false) != 0);
		drm_crtc_vblank_put(new_state->base.crtc);
		DRM_DEBUG_DRIVER("%s: crtc=%u VRR on->off: Drop vblank ref\n",
				 __func__, new_state->base.crtc->base.id);
	}
}

static void amdgpu_dm_commit_cursors(struct drm_atomic_state *state)
{
	struct drm_plane *plane;
	struct drm_plane_state *old_plane_state;
	int i;

	/*
	 * TODO: Make this per-stream so we don't issue redundant updates for
	 * commits with multiple streams.
	 */
	for_each_old_plane_in_state(state, plane, old_plane_state, i)
		if (plane->type == DRM_PLANE_TYPE_CURSOR)
			amdgpu_dm_plane_handle_cursor_update(plane, old_plane_state);
}

static inline uint32_t get_mem_type(struct drm_framebuffer *fb)
{
	struct amdgpu_bo *abo = gem_to_amdgpu_bo(fb->obj[0]);

	return abo->tbo.resource ? abo->tbo.resource->mem_type : 0;
}

static void amdgpu_dm_update_cursor(struct drm_plane *plane,
				    struct drm_plane_state *old_plane_state,
				    struct dc_stream_update *update)
{
	struct amdgpu_device *adev = drm_to_adev(plane->dev);
	struct amdgpu_framebuffer *afb = to_amdgpu_framebuffer(plane->state->fb);
	struct drm_crtc *crtc = afb ? plane->state->crtc : old_plane_state->crtc;
	struct dm_crtc_state *crtc_state = crtc ? to_dm_crtc_state(crtc->state) : NULL;
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);
	uint64_t address = afb ? afb->address : 0;
	struct dc_cursor_position position = {0};
	struct dc_cursor_attributes attributes;
	int ret;

	if (!plane->state->fb && !old_plane_state->fb)
		return;

	drm_dbg_atomic(plane->dev, "crtc_id=%d with size %d to %d\n",
		       amdgpu_crtc->crtc_id, plane->state->crtc_w,
		       plane->state->crtc_h);

	ret = amdgpu_dm_plane_get_cursor_position(plane, crtc, &position);
	if (ret)
		return;

	if (!position.enable) {
		/* turn off cursor */
		if (crtc_state && crtc_state->stream) {
			dc_stream_set_cursor_position(crtc_state->stream,
						      &position);
			update->cursor_position = &crtc_state->stream->cursor_position;
		}
		return;
	}

	amdgpu_crtc->cursor_width = plane->state->crtc_w;
	amdgpu_crtc->cursor_height = plane->state->crtc_h;

	memset(&attributes, 0, sizeof(attributes));
	attributes.address.high_part = upper_32_bits(address);
	attributes.address.low_part  = lower_32_bits(address);
	attributes.width             = plane->state->crtc_w;
	attributes.height            = plane->state->crtc_h;
	attributes.color_format      = CURSOR_MODE_COLOR_PRE_MULTIPLIED_ALPHA;
	attributes.rotation_angle    = 0;
	attributes.attribute_flags.value = 0;

	/* Enable cursor degamma ROM on DCN3+ for implicit sRGB degamma in DRM
	 * legacy gamma setup.
	 */
	if (crtc_state->cm_is_degamma_srgb &&
	    adev->dm.dc->caps.color.dpp.gamma_corr)
		attributes.attribute_flags.bits.ENABLE_CURSOR_DEGAMMA = 1;

	if (afb)
		attributes.pitch = afb->base.pitches[0] / afb->base.format->cpp[0];

	if (crtc_state->stream) {
		if (!dc_stream_set_cursor_attributes(crtc_state->stream,
						     &attributes))
			DRM_ERROR("DC failed to set cursor attributes\n");

		update->cursor_attributes = &crtc_state->stream->cursor_attributes;

		if (!dc_stream_set_cursor_position(crtc_state->stream,
						   &position))
			DRM_ERROR("DC failed to set cursor position\n");

		update->cursor_position = &crtc_state->stream->cursor_position;
	}
}

static void amdgpu_dm_enable_self_refresh(struct amdgpu_crtc *acrtc_attach,
					  const struct dm_crtc_state *acrtc_state,
					  const u64 current_ts)
{
	struct psr_settings *psr = &acrtc_state->stream->link->psr_settings;
	struct replay_settings *pr = &acrtc_state->stream->link->replay_settings;
	struct amdgpu_dm_connector *aconn =
		(struct amdgpu_dm_connector *)acrtc_state->stream->dm_stream_context;
	bool vrr_active = amdgpu_dm_crtc_vrr_active(acrtc_state);

	if (acrtc_state->update_type > UPDATE_TYPE_FAST) {
		if (pr->config.replay_supported && !pr->replay_feature_enabled)
			amdgpu_dm_link_setup_replay(acrtc_state->stream->link, aconn);
		else if (psr->psr_version != DC_PSR_VERSION_UNSUPPORTED &&
			     !psr->psr_feature_enabled)
			if (!aconn->disallow_edp_enter_psr)
				amdgpu_dm_link_setup_psr(acrtc_state->stream);
	}

	/* Decrement skip count when SR is enabled and we're doing fast updates. */
	if (acrtc_state->update_type == UPDATE_TYPE_FAST &&
	    (psr->psr_feature_enabled || pr->config.replay_supported)) {
		if (aconn->sr_skip_count > 0)
			aconn->sr_skip_count--;

		/* Allow SR when skip count is 0. */
		acrtc_attach->dm_irq_params.allow_sr_entry = !aconn->sr_skip_count;

		/*
		 * If sink supports PSR SU/Panel Replay, there is no need to rely on
		 * a vblank event disable request to enable PSR/RP. PSR SU/RP
		 * can be enabled immediately once OS demonstrates an
		 * adequate number of fast atomic commits to notify KMD
		 * of update events. See `vblank_control_worker()`.
		 */
		if (!vrr_active &&
		    acrtc_attach->dm_irq_params.allow_sr_entry &&
#ifdef CONFIG_DRM_AMD_SECURE_DISPLAY
		    !amdgpu_dm_crc_window_is_activated(acrtc_state->base.crtc) &&
#endif
		    (current_ts - psr->psr_dirty_rects_change_timestamp_ns) > 500000000) {
			if (pr->replay_feature_enabled && !pr->replay_allow_active)
				amdgpu_dm_replay_enable(acrtc_state->stream, true);
			if (psr->psr_version == DC_PSR_VERSION_SU_1 &&
			    !psr->psr_allow_active && !aconn->disallow_edp_enter_psr)
				amdgpu_dm_psr_enable(acrtc_state->stream);
		}
	} else {
		acrtc_attach->dm_irq_params.allow_sr_entry = false;
	}
}

static void amdgpu_dm_commit_planes(struct drm_atomic_state *state,
				    struct drm_device *dev,
				    struct amdgpu_display_manager *dm,
				    struct drm_crtc *pcrtc,
				    bool wait_for_vblank)
{
	u32 i;
	u64 timestamp_ns = ktime_get_ns();
	struct drm_plane *plane;
	struct drm_plane_state *old_plane_state, *new_plane_state;
	struct amdgpu_crtc *acrtc_attach = to_amdgpu_crtc(pcrtc);
	struct drm_crtc_state *new_pcrtc_state =
			drm_atomic_get_new_crtc_state(state, pcrtc);
	struct dm_crtc_state *acrtc_state = to_dm_crtc_state(new_pcrtc_state);
	struct dm_crtc_state *dm_old_crtc_state =
			to_dm_crtc_state(drm_atomic_get_old_crtc_state(state, pcrtc));
	int planes_count = 0, vpos, hpos;
	unsigned long flags;
	u32 target_vblank, last_flip_vblank;
	bool vrr_active = amdgpu_dm_crtc_vrr_active(acrtc_state);
	bool cursor_update = false;
	bool pflip_present = false;
	bool dirty_rects_changed = false;
	bool updated_planes_and_streams = false;
	struct {
		struct dc_surface_update surface_updates[MAX_SURFACES];
		struct dc_plane_info plane_infos[MAX_SURFACES];
		struct dc_scaling_info scaling_infos[MAX_SURFACES];
		struct dc_flip_addrs flip_addrs[MAX_SURFACES];
		struct dc_stream_update stream_update;
	} *bundle;

	bundle = kzalloc(sizeof(*bundle), GFP_KERNEL);

	if (!bundle) {
		drm_err(dev, "Failed to allocate update bundle\n");
		goto cleanup;
	}

	/*
	 * Disable the cursor first if we're disabling all the planes.
	 * It'll remain on the screen after the planes are re-enabled
	 * if we don't.
	 *
	 * If the cursor is transitioning from native to overlay mode, the
	 * native cursor needs to be disabled first.
	 */
	if (acrtc_state->cursor_mode == DM_CURSOR_OVERLAY_MODE &&
	    dm_old_crtc_state->cursor_mode == DM_CURSOR_NATIVE_MODE) {
		struct dc_cursor_position cursor_position = {0};

		if (!dc_stream_set_cursor_position(acrtc_state->stream,
						   &cursor_position))
			drm_err(dev, "DC failed to disable native cursor\n");

		bundle->stream_update.cursor_position =
				&acrtc_state->stream->cursor_position;
	}

	if (acrtc_state->active_planes == 0 &&
	    dm_old_crtc_state->cursor_mode == DM_CURSOR_NATIVE_MODE)
		amdgpu_dm_commit_cursors(state);

	/* update planes when needed */
	for_each_oldnew_plane_in_state(state, plane, old_plane_state, new_plane_state, i) {
		struct drm_crtc *crtc = new_plane_state->crtc;
		struct drm_crtc_state *new_crtc_state;
		struct drm_framebuffer *fb = new_plane_state->fb;
		struct amdgpu_framebuffer *afb = (struct amdgpu_framebuffer *)fb;
		bool plane_needs_flip;
		struct dc_plane_state *dc_plane;
		struct dm_plane_state *dm_new_plane_state = to_dm_plane_state(new_plane_state);

		/* Cursor plane is handled after stream updates */
		if (plane->type == DRM_PLANE_TYPE_CURSOR &&
		    acrtc_state->cursor_mode == DM_CURSOR_NATIVE_MODE) {
			if ((fb && crtc == pcrtc) ||
			    (old_plane_state->fb && old_plane_state->crtc == pcrtc)) {
				cursor_update = true;
				if (amdgpu_ip_version(dm->adev, DCE_HWIP, 0) != 0)
					amdgpu_dm_update_cursor(plane, old_plane_state, &bundle->stream_update);
			}

			continue;
		}

		if (!fb || !crtc || pcrtc != crtc)
			continue;

		new_crtc_state = drm_atomic_get_new_crtc_state(state, crtc);
		if (!new_crtc_state->active)
			continue;

		dc_plane = dm_new_plane_state->dc_state;
		if (!dc_plane)
			continue;

		bundle->surface_updates[planes_count].surface = dc_plane;
		if (new_pcrtc_state->color_mgmt_changed) {
			bundle->surface_updates[planes_count].gamma = &dc_plane->gamma_correction;
			bundle->surface_updates[planes_count].in_transfer_func = &dc_plane->in_transfer_func;
			bundle->surface_updates[planes_count].gamut_remap_matrix = &dc_plane->gamut_remap_matrix;
			bundle->surface_updates[planes_count].hdr_mult = dc_plane->hdr_mult;
			bundle->surface_updates[planes_count].func_shaper = &dc_plane->in_shaper_func;
			bundle->surface_updates[planes_count].lut3d_func = &dc_plane->lut3d_func;
			bundle->surface_updates[planes_count].blend_tf = &dc_plane->blend_tf;
		}

		amdgpu_dm_plane_fill_dc_scaling_info(dm->adev, new_plane_state,
				     &bundle->scaling_infos[planes_count]);

		bundle->surface_updates[planes_count].scaling_info =
			&bundle->scaling_infos[planes_count];

		plane_needs_flip = old_plane_state->fb && new_plane_state->fb;

		pflip_present = pflip_present || plane_needs_flip;

		if (!plane_needs_flip) {
			planes_count += 1;
			continue;
		}

		fill_dc_plane_info_and_addr(
			dm->adev, new_plane_state,
			afb->tiling_flags,
			&bundle->plane_infos[planes_count],
			&bundle->flip_addrs[planes_count].address,
			afb->tmz_surface);

		drm_dbg_state(state->dev, "plane: id=%d dcc_en=%d\n",
				 new_plane_state->plane->index,
				 bundle->plane_infos[planes_count].dcc.enable);

		bundle->surface_updates[planes_count].plane_info =
			&bundle->plane_infos[planes_count];

		if (acrtc_state->stream->link->psr_settings.psr_feature_enabled ||
		    acrtc_state->stream->link->replay_settings.replay_feature_enabled) {
			fill_dc_dirty_rects(plane, old_plane_state,
					    new_plane_state, new_crtc_state,
					    &bundle->flip_addrs[planes_count],
					    acrtc_state->stream->link->psr_settings.psr_version ==
					    DC_PSR_VERSION_SU_1,
					    &dirty_rects_changed);

			/*
			 * If the dirty regions changed, PSR-SU need to be disabled temporarily
			 * and enabled it again after dirty regions are stable to avoid video glitch.
			 * PSR-SU will be enabled in vblank_control_worker() if user pause the video
			 * during the PSR-SU was disabled.
			 */
			if (acrtc_state->stream->link->psr_settings.psr_version >= DC_PSR_VERSION_SU_1 &&
			    acrtc_attach->dm_irq_params.allow_sr_entry &&
#ifdef CONFIG_DRM_AMD_SECURE_DISPLAY
			    !amdgpu_dm_crc_window_is_activated(acrtc_state->base.crtc) &&
#endif
			    dirty_rects_changed) {
				mutex_lock(&dm->dc_lock);
				acrtc_state->stream->link->psr_settings.psr_dirty_rects_change_timestamp_ns =
				timestamp_ns;
				if (acrtc_state->stream->link->psr_settings.psr_allow_active)
					amdgpu_dm_psr_disable(acrtc_state->stream, true);
				mutex_unlock(&dm->dc_lock);
			}
		}

		/*
		 * Only allow immediate flips for fast updates that don't
		 * change memory domain, FB pitch, DCC state, rotation or
		 * mirroring.
		 *
		 * dm_crtc_helper_atomic_check() only accepts async flips with
		 * fast updates.
		 */
		if (crtc->state->async_flip &&
		    (acrtc_state->update_type != UPDATE_TYPE_FAST ||
		     get_mem_type(old_plane_state->fb) != get_mem_type(fb)))
			drm_warn_once(state->dev,
				      "[PLANE:%d:%s] async flip with non-fast update\n",
				      plane->base.id, plane->name);

		bundle->flip_addrs[planes_count].flip_immediate =
			crtc->state->async_flip &&
			acrtc_state->update_type == UPDATE_TYPE_FAST &&
			get_mem_type(old_plane_state->fb) == get_mem_type(fb);

		timestamp_ns = ktime_get_ns();
		bundle->flip_addrs[planes_count].flip_timestamp_in_us = div_u64(timestamp_ns, 1000);
		bundle->surface_updates[planes_count].flip_addr = &bundle->flip_addrs[planes_count];
		bundle->surface_updates[planes_count].surface = dc_plane;

		if (!bundle->surface_updates[planes_count].surface) {
			DRM_ERROR("No surface for CRTC: id=%d\n",
					acrtc_attach->crtc_id);
			continue;
		}

		if (plane == pcrtc->primary)
			update_freesync_state_on_stream(
				dm,
				acrtc_state,
				acrtc_state->stream,
				dc_plane,
				bundle->flip_addrs[planes_count].flip_timestamp_in_us);

		drm_dbg_state(state->dev, "%s Flipping to hi: 0x%x, low: 0x%x\n",
				 __func__,
				 bundle->flip_addrs[planes_count].address.grph.addr.high_part,
				 bundle->flip_addrs[planes_count].address.grph.addr.low_part);

		planes_count += 1;

	}

	if (pflip_present) {
		if (!vrr_active) {
			/* Use old throttling in non-vrr fixed refresh rate mode
			 * to keep flip scheduling based on target vblank counts
			 * working in a backwards compatible way, e.g., for
			 * clients using the GLX_OML_sync_control extension or
			 * DRI3/Present extension with defined target_msc.
			 */
			last_flip_vblank = amdgpu_get_vblank_counter_kms(pcrtc);
		} else {
			/* For variable refresh rate mode only:
			 * Get vblank of last completed flip to avoid > 1 vrr
			 * flips per video frame by use of throttling, but allow
			 * flip programming anywhere in the possibly large
			 * variable vrr vblank interval for fine-grained flip
			 * timing control and more opportunity to avoid stutter
			 * on late submission of flips.
			 */
			spin_lock_irqsave(&pcrtc->dev->event_lock, flags);
			last_flip_vblank = acrtc_attach->dm_irq_params.last_flip_vblank;
			spin_unlock_irqrestore(&pcrtc->dev->event_lock, flags);
		}

		target_vblank = last_flip_vblank + wait_for_vblank;

		/*
		 * Wait until we're out of the vertical blank period before the one
		 * targeted by the flip
		 */
		while ((acrtc_attach->enabled &&
			(amdgpu_display_get_crtc_scanoutpos(dm->ddev, acrtc_attach->crtc_id,
							    0, &vpos, &hpos, NULL,
							    NULL, &pcrtc->hwmode)
			 & (DRM_SCANOUTPOS_VALID | DRM_SCANOUTPOS_IN_VBLANK)) ==
			(DRM_SCANOUTPOS_VALID | DRM_SCANOUTPOS_IN_VBLANK) &&
			(int)(target_vblank -
			  amdgpu_get_vblank_counter_kms(pcrtc)) > 0)) {
			usleep_range(1000, 1100);
		}

		/**
		 * Prepare the flip event for the pageflip interrupt to handle.
		 *
		 * This only works in the case where we've already turned on the
		 * appropriate hardware blocks (eg. HUBP) so in the transition case
		 * from 0 -> n planes we have to skip a hardware generated event
		 * and rely on sending it from software.
		 */
		if (acrtc_attach->base.state->event &&
		    acrtc_state->active_planes > 0) {
			drm_crtc_vblank_get(pcrtc);

			spin_lock_irqsave(&pcrtc->dev->event_lock, flags);

			WARN_ON(acrtc_attach->pflip_status != AMDGPU_FLIP_NONE);
			prepare_flip_isr(acrtc_attach);

			spin_unlock_irqrestore(&pcrtc->dev->event_lock, flags);
		}

		if (acrtc_state->stream) {
			if (acrtc_state->freesync_vrr_info_changed)
				bundle->stream_update.vrr_infopacket =
					&acrtc_state->stream->vrr_infopacket;
		}
	} else if (cursor_update && acrtc_state->active_planes > 0) {
		spin_lock_irqsave(&pcrtc->dev->event_lock, flags);
		if (acrtc_attach->base.state->event) {
			drm_crtc_vblank_get(pcrtc);
			acrtc_attach->event = acrtc_attach->base.state->event;
			acrtc_attach->base.state->event = NULL;
		}
		spin_unlock_irqrestore(&pcrtc->dev->event_lock, flags);
	}

	/* Update the planes if changed or disable if we don't have any. */
	if ((planes_count || acrtc_state->active_planes == 0) &&
		acrtc_state->stream) {
		/*
		 * If PSR or idle optimizations are enabled then flush out
		 * any pending work before hardware programming.
		 */
		if (dm->vblank_control_workqueue)
			flush_workqueue(dm->vblank_control_workqueue);

		bundle->stream_update.stream = acrtc_state->stream;
		if (new_pcrtc_state->mode_changed) {
			bundle->stream_update.src = acrtc_state->stream->src;
			bundle->stream_update.dst = acrtc_state->stream->dst;
		}

		if (new_pcrtc_state->color_mgmt_changed) {
			/*
			 * TODO: This isn't fully correct since we've actually
			 * already modified the stream in place.
			 */
			bundle->stream_update.gamut_remap =
				&acrtc_state->stream->gamut_remap_matrix;
			bundle->stream_update.output_csc_transform =
				&acrtc_state->stream->csc_color_matrix;
			bundle->stream_update.out_transfer_func =
				&acrtc_state->stream->out_transfer_func;
			bundle->stream_update.lut3d_func =
				(struct dc_3dlut *) acrtc_state->stream->lut3d_func;
			bundle->stream_update.func_shaper =
				(struct dc_transfer_func *) acrtc_state->stream->func_shaper;
		}

		acrtc_state->stream->abm_level = acrtc_state->abm_level;
		if (acrtc_state->abm_level != dm_old_crtc_state->abm_level)
			bundle->stream_update.abm_level = &acrtc_state->abm_level;

		mutex_lock(&dm->dc_lock);
		if ((acrtc_state->update_type > UPDATE_TYPE_FAST) || vrr_active) {
			if (acrtc_state->stream->link->replay_settings.replay_allow_active)
				amdgpu_dm_replay_disable(acrtc_state->stream);
			if (acrtc_state->stream->link->psr_settings.psr_allow_active)
				amdgpu_dm_psr_disable(acrtc_state->stream, true);
		}
		mutex_unlock(&dm->dc_lock);

		/*
		 * If FreeSync state on the stream has changed then we need to
		 * re-adjust the min/max bounds now that DC doesn't handle this
		 * as part of commit.
		 */
		if (is_dc_timing_adjust_needed(dm_old_crtc_state, acrtc_state)) {
			spin_lock_irqsave(&pcrtc->dev->event_lock, flags);
			dc_stream_adjust_vmin_vmax(
				dm->dc, acrtc_state->stream,
				&acrtc_attach->dm_irq_params.vrr_params.adjust);
			spin_unlock_irqrestore(&pcrtc->dev->event_lock, flags);
		}
		mutex_lock(&dm->dc_lock);
		update_planes_and_stream_adapter(dm->dc,
					 acrtc_state->update_type,
					 planes_count,
					 acrtc_state->stream,
					 &bundle->stream_update,
					 bundle->surface_updates);
		updated_planes_and_streams = true;

		/**
		 * Enable or disable the interrupts on the backend.
		 *
		 * Most pipes are put into power gating when unused.
		 *
		 * When power gating is enabled on a pipe we lose the
		 * interrupt enablement state when power gating is disabled.
		 *
		 * So we need to update the IRQ control state in hardware
		 * whenever the pipe turns on (since it could be previously
		 * power gated) or off (since some pipes can't be power gated
		 * on some ASICs).
		 */
		if (dm_old_crtc_state->active_planes != acrtc_state->active_planes)
			dm_update_pflip_irq_state(drm_to_adev(dev),
						  acrtc_attach);

		amdgpu_dm_enable_self_refresh(acrtc_attach, acrtc_state, timestamp_ns);
		mutex_unlock(&dm->dc_lock);
	}

	/*
	 * Update cursor state *after* programming all the planes.
	 * This avoids redundant programming in the case where we're going
	 * to be disabling a single plane - those pipes are being disabled.
	 */
	if (acrtc_state->active_planes &&
	    (!updated_planes_and_streams || amdgpu_ip_version(dm->adev, DCE_HWIP, 0) == 0) &&
	    acrtc_state->cursor_mode == DM_CURSOR_NATIVE_MODE)
		amdgpu_dm_commit_cursors(state);

cleanup:
	kfree(bundle);
}

static void amdgpu_dm_commit_audio(struct drm_device *dev,
				   struct drm_atomic_state *state)
{
	struct amdgpu_device *adev = drm_to_adev(dev);
	struct amdgpu_dm_connector *aconnector;
	struct drm_connector *connector;
	struct drm_connector_state *old_con_state, *new_con_state;
	struct drm_crtc_state *new_crtc_state;
	struct dm_crtc_state *new_dm_crtc_state;
	const struct dc_stream_status *status;
	int i, inst;

	/* Notify device removals. */
	for_each_oldnew_connector_in_state(state, connector, old_con_state, new_con_state, i) {
		if (old_con_state->crtc != new_con_state->crtc) {
			/* CRTC changes require notification. */
			goto notify;
		}

		if (!new_con_state->crtc)
			continue;

		new_crtc_state = drm_atomic_get_new_crtc_state(
			state, new_con_state->crtc);

		if (!new_crtc_state)
			continue;

		if (!drm_atomic_crtc_needs_modeset(new_crtc_state))
			continue;

notify:
		if (connector->connector_type == DRM_MODE_CONNECTOR_WRITEBACK)
			continue;

		aconnector = to_amdgpu_dm_connector(connector);

		mutex_lock(&adev->dm.audio_lock);
		inst = aconnector->audio_inst;
		aconnector->audio_inst = -1;
		mutex_unlock(&adev->dm.audio_lock);

		amdgpu_dm_audio_eld_notify(adev, inst);
	}

	/* Notify audio device additions. */
	for_each_new_connector_in_state(state, connector, new_con_state, i) {
		if (!new_con_state->crtc)
			continue;

		new_crtc_state = drm_atomic_get_new_crtc_state(
			state, new_con_state->crtc);

		if (!new_crtc_state)
			continue;

		if (!drm_atomic_crtc_needs_modeset(new_crtc_state))
			continue;

		new_dm_crtc_state = to_dm_crtc_state(new_crtc_state);
		if (!new_dm_crtc_state->stream)
			continue;

		status = dc_stream_get_status(new_dm_crtc_state->stream);
		if (!status)
			continue;

		if (connector->connector_type == DRM_MODE_CONNECTOR_WRITEBACK)
			continue;

		aconnector = to_amdgpu_dm_connector(connector);

		mutex_lock(&adev->dm.audio_lock);
		inst = status->audio_inst;
		aconnector->audio_inst = inst;
		mutex_unlock(&adev->dm.audio_lock);

		amdgpu_dm_audio_eld_notify(adev, inst);
	}
}

/*
 * amdgpu_dm_crtc_copy_transient_flags - copy mirrored flags from DRM to DC
 * @crtc_state: the DRM CRTC state
 * @stream_state: the DC stream state.
 *
 * Copy the mirrored transient state flags from DRM, to DC. It is used to bring
 * a dc_stream_state's flags in sync with a drm_crtc_state's flags.
 */
static void amdgpu_dm_crtc_copy_transient_flags(struct drm_crtc_state *crtc_state,
						struct dc_stream_state *stream_state)
{
	stream_state->mode_changed = drm_atomic_crtc_needs_modeset(crtc_state);
}

static void dm_clear_writeback(struct amdgpu_display_manager *dm,
			      struct dm_crtc_state *crtc_state)
{
	dc_stream_remove_writeback(dm->dc, crtc_state->stream, 0);
}

static void amdgpu_dm_commit_streams(struct drm_atomic_state *state,
					struct dc_state *dc_state)
{
	struct drm_device *dev = state->dev;
	struct amdgpu_device *adev = drm_to_adev(dev);
	struct amdgpu_display_manager *dm = &adev->dm;
	struct drm_crtc *crtc;
	struct drm_crtc_state *old_crtc_state, *new_crtc_state;
	struct dm_crtc_state *dm_old_crtc_state, *dm_new_crtc_state;
	struct drm_connector_state *old_con_state;
	struct drm_connector *connector;
	bool mode_set_reset_required = false;
	u32 i;
	struct dc_commit_streams_params params = {dc_state->streams, dc_state->stream_count};
	bool set_backlight_level = false;

	/* Disable writeback */
	for_each_old_connector_in_state(state, connector, old_con_state, i) {
		struct dm_connector_state *dm_old_con_state;
		struct amdgpu_crtc *acrtc;

		if (connector->connector_type != DRM_MODE_CONNECTOR_WRITEBACK)
			continue;

		old_crtc_state = NULL;

		dm_old_con_state = to_dm_connector_state(old_con_state);
		if (!dm_old_con_state->base.crtc)
			continue;

		acrtc = to_amdgpu_crtc(dm_old_con_state->base.crtc);
		if (acrtc)
			old_crtc_state = drm_atomic_get_old_crtc_state(state, &acrtc->base);

		if (!acrtc || !acrtc->wb_enabled)
			continue;

		dm_old_crtc_state = to_dm_crtc_state(old_crtc_state);

		dm_clear_writeback(dm, dm_old_crtc_state);
		acrtc->wb_enabled = false;
	}

	for_each_oldnew_crtc_in_state(state, crtc, old_crtc_state,
				      new_crtc_state, i) {
		struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);

		dm_old_crtc_state = to_dm_crtc_state(old_crtc_state);

		if (old_crtc_state->active &&
		    (!new_crtc_state->active ||
		     drm_atomic_crtc_needs_modeset(new_crtc_state))) {
			manage_dm_interrupts(adev, acrtc, NULL);
			dc_stream_release(dm_old_crtc_state->stream);
		}
	}

	drm_atomic_helper_calc_timestamping_constants(state);

	/* update changed items */
	for_each_oldnew_crtc_in_state(state, crtc, old_crtc_state, new_crtc_state, i) {
		struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);

		dm_new_crtc_state = to_dm_crtc_state(new_crtc_state);
		dm_old_crtc_state = to_dm_crtc_state(old_crtc_state);

		drm_dbg_state(state->dev,
			"amdgpu_crtc id:%d crtc_state_flags: enable:%d, active:%d, planes_changed:%d, mode_changed:%d,active_changed:%d,connectors_changed:%d\n",
			acrtc->crtc_id,
			new_crtc_state->enable,
			new_crtc_state->active,
			new_crtc_state->planes_changed,
			new_crtc_state->mode_changed,
			new_crtc_state->active_changed,
			new_crtc_state->connectors_changed);

		/* Disable cursor if disabling crtc */
		if (old_crtc_state->active && !new_crtc_state->active) {
			struct dc_cursor_position position;

			memset(&position, 0, sizeof(position));
			mutex_lock(&dm->dc_lock);
			dc_exit_ips_for_hw_access(dm->dc);
			dc_stream_program_cursor_position(dm_old_crtc_state->stream, &position);
			mutex_unlock(&dm->dc_lock);
		}

		/* Copy all transient state flags into dc state */
		if (dm_new_crtc_state->stream) {
			amdgpu_dm_crtc_copy_transient_flags(&dm_new_crtc_state->base,
							    dm_new_crtc_state->stream);
		}

		/* handles headless hotplug case, updating new_state and
		 * aconnector as needed
		 */

		if (amdgpu_dm_crtc_modeset_required(new_crtc_state, dm_new_crtc_state->stream, dm_old_crtc_state->stream)) {

			drm_dbg_atomic(dev,
				       "Atomic commit: SET crtc id %d: [%p]\n",
				       acrtc->crtc_id, acrtc);

			if (!dm_new_crtc_state->stream) {
				/*
				 * this could happen because of issues with
				 * userspace notifications delivery.
				 * In this case userspace tries to set mode on
				 * display which is disconnected in fact.
				 * dc_sink is NULL in this case on aconnector.
				 * We expect reset mode will come soon.
				 *
				 * This can also happen when unplug is done
				 * during resume sequence ended
				 *
				 * In this case, we want to pretend we still
				 * have a sink to keep the pipe running so that
				 * hw state is consistent with the sw state
				 */
				drm_dbg_atomic(dev,
					       "Failed to create new stream for crtc %d\n",
						acrtc->base.base.id);
				continue;
			}

			if (dm_old_crtc_state->stream)
				remove_stream(adev, acrtc, dm_old_crtc_state->stream);

			pm_runtime_get_noresume(dev->dev);

			acrtc->enabled = true;
			acrtc->hw_mode = new_crtc_state->mode;
			crtc->hwmode = new_crtc_state->mode;
			mode_set_reset_required = true;
			set_backlight_level = true;
		} else if (modereset_required(new_crtc_state)) {
			drm_dbg_atomic(dev,
				       "Atomic commit: RESET. crtc id %d:[%p]\n",
				       acrtc->crtc_id, acrtc);
			/* i.e. reset mode */
			if (dm_old_crtc_state->stream)
				remove_stream(adev, acrtc, dm_old_crtc_state->stream);

			mode_set_reset_required = true;
		}
	} /* for_each_crtc_in_state() */

	/* if there mode set or reset, disable eDP PSR, Replay */
	if (mode_set_reset_required) {
		if (dm->vblank_control_workqueue)
			flush_workqueue(dm->vblank_control_workqueue);

		amdgpu_dm_replay_disable_all(dm);
		amdgpu_dm_psr_disable_all(dm);
	}

	dm_enable_per_frame_crtc_master_sync(dc_state);
	mutex_lock(&dm->dc_lock);
	dc_exit_ips_for_hw_access(dm->dc);
	WARN_ON(!dc_commit_streams(dm->dc, &params));

	/* Allow idle optimization when vblank count is 0 for display off */
	if (dm->active_vblank_irq_count == 0)
		dc_allow_idle_optimizations(dm->dc, true);
	mutex_unlock(&dm->dc_lock);

	for_each_new_crtc_in_state(state, crtc, new_crtc_state, i) {
		struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);

		dm_new_crtc_state = to_dm_crtc_state(new_crtc_state);

		if (dm_new_crtc_state->stream != NULL) {
			const struct dc_stream_status *status =
					dc_stream_get_status(dm_new_crtc_state->stream);

			if (!status)
				status = dc_state_get_stream_status(dc_state,
									 dm_new_crtc_state->stream);
			if (!status)
				drm_err(dev,
					"got no status for stream %p on acrtc%p\n",
					dm_new_crtc_state->stream, acrtc);
			else
				acrtc->otg_inst = status->primary_otg_inst;
		}
	}

	/* During boot up and resume the DC layer will reset the panel brightness
	 * to fix a flicker issue.
	 * It will cause the dm->actual_brightness is not the current panel brightness
	 * level. (the dm->brightness is the correct panel level)
	 * So we set the backlight level with dm->brightness value after set mode
	 */
	if (set_backlight_level) {
		for (i = 0; i < dm->num_of_edps; i++) {
			if (dm->backlight_dev[i])
				amdgpu_dm_backlight_set_level(dm, i, dm->brightness[i]);
		}
	}
}

static void dm_set_writeback(struct amdgpu_display_manager *dm,
			      struct dm_crtc_state *crtc_state,
			      struct drm_connector *connector,
			      struct drm_connector_state *new_con_state)
{
	STUB();
#ifdef notyet
	struct drm_writeback_connector *wb_conn = drm_connector_to_writeback(connector);
	struct amdgpu_device *adev = dm->adev;
	struct amdgpu_crtc *acrtc;
	struct dc_writeback_info *wb_info;
	struct pipe_ctx *pipe = NULL;
	struct amdgpu_framebuffer *afb;
	int i = 0;

	wb_info = kzalloc(sizeof(*wb_info), GFP_KERNEL);
	if (!wb_info) {
		DRM_ERROR("Failed to allocate wb_info\n");
		return;
	}

	acrtc = to_amdgpu_crtc(wb_conn->encoder.crtc);
	if (!acrtc) {
		DRM_ERROR("no amdgpu_crtc found\n");
		kfree(wb_info);
		return;
	}

	afb = to_amdgpu_framebuffer(new_con_state->writeback_job->fb);
	if (!afb) {
		DRM_ERROR("No amdgpu_framebuffer found\n");
		kfree(wb_info);
		return;
	}

	for (i = 0; i < MAX_PIPES; i++) {
		if (dm->dc->current_state->res_ctx.pipe_ctx[i].stream == crtc_state->stream) {
			pipe = &dm->dc->current_state->res_ctx.pipe_ctx[i];
			break;
		}
	}

	/* fill in wb_info */
	wb_info->wb_enabled = true;

	wb_info->dwb_pipe_inst = 0;
	wb_info->dwb_params.dwbscl_black_color = 0;
	wb_info->dwb_params.hdr_mult = 0x1F000;
	wb_info->dwb_params.csc_params.gamut_adjust_type = CM_GAMUT_ADJUST_TYPE_BYPASS;
	wb_info->dwb_params.csc_params.gamut_coef_format = CM_GAMUT_REMAP_COEF_FORMAT_S2_13;
	wb_info->dwb_params.output_depth = DWB_OUTPUT_PIXEL_DEPTH_10BPC;
	wb_info->dwb_params.cnv_params.cnv_out_bpc = DWB_CNV_OUT_BPC_10BPC;

	/* width & height from crtc */
	wb_info->dwb_params.cnv_params.src_width = acrtc->base.mode.crtc_hdisplay;
	wb_info->dwb_params.cnv_params.src_height = acrtc->base.mode.crtc_vdisplay;
	wb_info->dwb_params.dest_width = acrtc->base.mode.crtc_hdisplay;
	wb_info->dwb_params.dest_height = acrtc->base.mode.crtc_vdisplay;

	wb_info->dwb_params.cnv_params.crop_en = false;
	wb_info->dwb_params.stereo_params.stereo_enabled = false;

	wb_info->dwb_params.cnv_params.out_max_pix_val = 0x3ff;	// 10 bits
	wb_info->dwb_params.cnv_params.out_min_pix_val = 0;
	wb_info->dwb_params.cnv_params.fc_out_format = DWB_OUT_FORMAT_32BPP_ARGB;
	wb_info->dwb_params.cnv_params.out_denorm_mode = DWB_OUT_DENORM_BYPASS;

	wb_info->dwb_params.out_format = dwb_scaler_mode_bypass444;

	wb_info->dwb_params.capture_rate = dwb_capture_rate_0;

	wb_info->dwb_params.scaler_taps.h_taps = 4;
	wb_info->dwb_params.scaler_taps.v_taps = 4;
	wb_info->dwb_params.scaler_taps.h_taps_c = 2;
	wb_info->dwb_params.scaler_taps.v_taps_c = 2;
	wb_info->dwb_params.subsample_position = DWB_INTERSTITIAL_SUBSAMPLING;

	wb_info->mcif_buf_params.luma_pitch = afb->base.pitches[0];
	wb_info->mcif_buf_params.chroma_pitch = afb->base.pitches[1];

	for (i = 0; i < DWB_MCIF_BUF_COUNT; i++) {
		wb_info->mcif_buf_params.luma_address[i] = afb->address;
		wb_info->mcif_buf_params.chroma_address[i] = 0;
	}

	wb_info->mcif_buf_params.p_vmid = 1;
	if (amdgpu_ip_version(adev, DCE_HWIP, 0) >= IP_VERSION(3, 0, 0)) {
		wb_info->mcif_warmup_params.start_address.quad_part = afb->address;
		wb_info->mcif_warmup_params.region_size =
			wb_info->mcif_buf_params.luma_pitch * wb_info->dwb_params.dest_height;
	}
	wb_info->mcif_warmup_params.p_vmid = 1;
	wb_info->writeback_source_plane = pipe->plane_state;

	dc_stream_add_writeback(dm->dc, crtc_state->stream, wb_info);

	acrtc->wb_pending = true;
	acrtc->wb_conn = wb_conn;
	drm_writeback_queue_job(wb_conn, new_con_state);
#endif
}

/**
 * amdgpu_dm_atomic_commit_tail() - AMDgpu DM's commit tail implementation.
 * @state: The atomic state to commit
 *
 * This will tell DC to commit the constructed DC state from atomic_check,
 * programming the hardware. Any failures here implies a hardware failure, since
 * atomic check should have filtered anything non-kosher.
 */
static void amdgpu_dm_atomic_commit_tail(struct drm_atomic_state *state)
{
	struct drm_device *dev = state->dev;
	struct amdgpu_device *adev = drm_to_adev(dev);
	struct amdgpu_display_manager *dm = &adev->dm;
	struct dm_atomic_state *dm_state;
	struct dc_state *dc_state = NULL;
	u32 i, j;
	struct drm_crtc *crtc;
	struct drm_crtc_state *old_crtc_state, *new_crtc_state;
	unsigned long flags;
	bool wait_for_vblank = true;
	struct drm_connector *connector;
	struct drm_connector_state *old_con_state, *new_con_state;
	struct dm_crtc_state *dm_old_crtc_state, *dm_new_crtc_state;
	int crtc_disable_count = 0;

	trace_amdgpu_dm_atomic_commit_tail_begin(state);

	drm_atomic_helper_update_legacy_modeset_state(dev, state);
	drm_dp_mst_atomic_wait_for_dependencies(state);

	dm_state = dm_atomic_get_new_state(state);
	if (dm_state && dm_state->context) {
		dc_state = dm_state->context;
		amdgpu_dm_commit_streams(state, dc_state);
	}

	for_each_oldnew_connector_in_state(state, connector, old_con_state, new_con_state, i) {
		struct dm_connector_state *dm_new_con_state = to_dm_connector_state(new_con_state);
		struct amdgpu_crtc *acrtc = to_amdgpu_crtc(dm_new_con_state->base.crtc);
		struct amdgpu_dm_connector *aconnector;

		if (connector->connector_type == DRM_MODE_CONNECTOR_WRITEBACK)
			continue;

		aconnector = to_amdgpu_dm_connector(connector);

		if (!adev->dm.hdcp_workqueue)
			continue;

		pr_debug("[HDCP_DM] -------------- i : %x ----------\n", i);

		if (!connector)
			continue;

		pr_debug("[HDCP_DM] connector->index: %x connect_status: %x dpms: %x\n",
			connector->index, connector->status, connector->dpms);
		pr_debug("[HDCP_DM] state protection old: %x new: %x\n",
			old_con_state->content_protection, new_con_state->content_protection);

		if (aconnector->dc_sink) {
			if (aconnector->dc_sink->sink_signal != SIGNAL_TYPE_VIRTUAL &&
				aconnector->dc_sink->sink_signal != SIGNAL_TYPE_NONE) {
				pr_debug("[HDCP_DM] pipe_ctx dispname=%s\n",
				aconnector->dc_sink->edid_caps.display_name);
			}
		}

		new_crtc_state = NULL;
		old_crtc_state = NULL;

		if (acrtc) {
			new_crtc_state = drm_atomic_get_new_crtc_state(state, &acrtc->base);
			old_crtc_state = drm_atomic_get_old_crtc_state(state, &acrtc->base);
		}

		if (old_crtc_state)
			pr_debug("old crtc en: %x a: %x m: %x a-chg: %x c-chg: %x\n",
			old_crtc_state->enable,
			old_crtc_state->active,
			old_crtc_state->mode_changed,
			old_crtc_state->active_changed,
			old_crtc_state->connectors_changed);

		if (new_crtc_state)
			pr_debug("NEW crtc en: %x a: %x m: %x a-chg: %x c-chg: %x\n",
			new_crtc_state->enable,
			new_crtc_state->active,
			new_crtc_state->mode_changed,
			new_crtc_state->active_changed,
			new_crtc_state->connectors_changed);
	}

	for_each_oldnew_connector_in_state(state, connector, old_con_state, new_con_state, i) {
		struct dm_connector_state *dm_new_con_state = to_dm_connector_state(new_con_state);
		struct amdgpu_crtc *acrtc = to_amdgpu_crtc(dm_new_con_state->base.crtc);
		struct amdgpu_dm_connector *aconnector = to_amdgpu_dm_connector(connector);

		if (!adev->dm.hdcp_workqueue)
			continue;

		new_crtc_state = NULL;
		old_crtc_state = NULL;

		if (acrtc) {
			new_crtc_state = drm_atomic_get_new_crtc_state(state, &acrtc->base);
			old_crtc_state = drm_atomic_get_old_crtc_state(state, &acrtc->base);
		}

		dm_new_crtc_state = to_dm_crtc_state(new_crtc_state);

		if (dm_new_crtc_state && dm_new_crtc_state->stream == NULL &&
		    connector->state->content_protection == DRM_MODE_CONTENT_PROTECTION_ENABLED) {
			hdcp_reset_display(adev->dm.hdcp_workqueue, aconnector->dc_link->link_index);
			new_con_state->content_protection = DRM_MODE_CONTENT_PROTECTION_DESIRED;
			dm_new_con_state->update_hdcp = true;
			continue;
		}

		if (is_content_protection_different(new_crtc_state, old_crtc_state, new_con_state,
											old_con_state, connector, adev->dm.hdcp_workqueue)) {
			/* when display is unplugged from mst hub, connctor will
			 * be destroyed within dm_dp_mst_connector_destroy. connector
			 * hdcp perperties, like type, undesired, desired, enabled,
			 * will be lost. So, save hdcp properties into hdcp_work within
			 * amdgpu_dm_atomic_commit_tail. if the same display is
			 * plugged back with same display index, its hdcp properties
			 * will be retrieved from hdcp_work within dm_dp_mst_get_modes
			 */

			bool enable_encryption = false;

			if (new_con_state->content_protection == DRM_MODE_CONTENT_PROTECTION_DESIRED)
				enable_encryption = true;

			if (aconnector->dc_link && aconnector->dc_sink &&
				aconnector->dc_link->type == dc_connection_mst_branch) {
				struct hdcp_workqueue *hdcp_work = adev->dm.hdcp_workqueue;
				struct hdcp_workqueue *hdcp_w =
					&hdcp_work[aconnector->dc_link->link_index];

				hdcp_w->hdcp_content_type[connector->index] =
					new_con_state->hdcp_content_type;
				hdcp_w->content_protection[connector->index] =
					new_con_state->content_protection;
			}

			if (new_crtc_state && new_crtc_state->mode_changed &&
				new_con_state->content_protection >= DRM_MODE_CONTENT_PROTECTION_DESIRED)
				enable_encryption = true;

			DRM_INFO("[HDCP_DM] hdcp_update_display enable_encryption = %x\n", enable_encryption);

			if (aconnector->dc_link)
				hdcp_update_display(
					adev->dm.hdcp_workqueue, aconnector->dc_link->link_index, aconnector,
					new_con_state->hdcp_content_type, enable_encryption);
		}
	}

	/* Handle connector state changes */
	for_each_oldnew_connector_in_state(state, connector, old_con_state, new_con_state, i) {
		struct dm_connector_state *dm_new_con_state = to_dm_connector_state(new_con_state);
		struct dm_connector_state *dm_old_con_state = to_dm_connector_state(old_con_state);
		struct amdgpu_crtc *acrtc = to_amdgpu_crtc(dm_new_con_state->base.crtc);
		struct dc_surface_update *dummy_updates;
		struct dc_stream_update stream_update;
		struct dc_info_packet hdr_packet;
		struct dc_stream_status *status = NULL;
		bool abm_changed, hdr_changed, scaling_changed;

		memset(&stream_update, 0, sizeof(stream_update));

		if (acrtc) {
			new_crtc_state = drm_atomic_get_new_crtc_state(state, &acrtc->base);
			old_crtc_state = drm_atomic_get_old_crtc_state(state, &acrtc->base);
		}

		/* Skip any modesets/resets */
		if (!acrtc || drm_atomic_crtc_needs_modeset(new_crtc_state))
			continue;

		dm_new_crtc_state = to_dm_crtc_state(new_crtc_state);
		dm_old_crtc_state = to_dm_crtc_state(old_crtc_state);

		scaling_changed = is_scaling_state_different(dm_new_con_state,
							     dm_old_con_state);

		abm_changed = dm_new_crtc_state->abm_level !=
			      dm_old_crtc_state->abm_level;

		hdr_changed =
			!drm_connector_atomic_hdr_metadata_equal(old_con_state, new_con_state);

		if (!scaling_changed && !abm_changed && !hdr_changed)
			continue;

		stream_update.stream = dm_new_crtc_state->stream;
		if (scaling_changed) {
			update_stream_scaling_settings(&dm_new_con_state->base.crtc->mode,
					dm_new_con_state, dm_new_crtc_state->stream);

			stream_update.src = dm_new_crtc_state->stream->src;
			stream_update.dst = dm_new_crtc_state->stream->dst;
		}

		if (abm_changed) {
			dm_new_crtc_state->stream->abm_level = dm_new_crtc_state->abm_level;

			stream_update.abm_level = &dm_new_crtc_state->abm_level;
		}

		if (hdr_changed) {
			fill_hdr_info_packet(new_con_state, &hdr_packet);
			stream_update.hdr_static_metadata = &hdr_packet;
		}

		status = dc_stream_get_status(dm_new_crtc_state->stream);

		if (WARN_ON(!status))
			continue;

		WARN_ON(!status->plane_count);

		/*
		 * TODO: DC refuses to perform stream updates without a dc_surface_update.
		 * Here we create an empty update on each plane.
		 * To fix this, DC should permit updating only stream properties.
		 */
		dummy_updates = kzalloc(sizeof(struct dc_surface_update) * MAX_SURFACES, GFP_ATOMIC);
		if (!dummy_updates) {
			DRM_ERROR("Failed to allocate memory for dummy_updates.\n");
			continue;
		}
		for (j = 0; j < status->plane_count; j++)
			dummy_updates[j].surface = status->plane_states[0];

		sort(dummy_updates, status->plane_count,
		     sizeof(*dummy_updates), dm_plane_layer_index_cmp, NULL);

		mutex_lock(&dm->dc_lock);
		dc_exit_ips_for_hw_access(dm->dc);
		dc_update_planes_and_stream(dm->dc,
					    dummy_updates,
					    status->plane_count,
					    dm_new_crtc_state->stream,
					    &stream_update);
		mutex_unlock(&dm->dc_lock);
		kfree(dummy_updates);
	}

	/**
	 * Enable interrupts for CRTCs that are newly enabled or went through
	 * a modeset. It was intentionally deferred until after the front end
	 * state was modified to wait until the OTG was on and so the IRQ
	 * handlers didn't access stale or invalid state.
	 */
	for_each_oldnew_crtc_in_state(state, crtc, old_crtc_state, new_crtc_state, i) {
		struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);
#ifdef CONFIG_DEBUG_FS
		enum amdgpu_dm_pipe_crc_source cur_crc_src;
#endif
		/* Count number of newly disabled CRTCs for dropping PM refs later. */
		if (old_crtc_state->active && !new_crtc_state->active)
			crtc_disable_count++;

		dm_new_crtc_state = to_dm_crtc_state(new_crtc_state);
		dm_old_crtc_state = to_dm_crtc_state(old_crtc_state);

		/* For freesync config update on crtc state and params for irq */
		update_stream_irq_parameters(dm, dm_new_crtc_state);

#ifdef CONFIG_DEBUG_FS
		spin_lock_irqsave(&adev_to_drm(adev)->event_lock, flags);
		cur_crc_src = acrtc->dm_irq_params.crc_src;
		spin_unlock_irqrestore(&adev_to_drm(adev)->event_lock, flags);
#endif

		if (new_crtc_state->active &&
		    (!old_crtc_state->active ||
		     drm_atomic_crtc_needs_modeset(new_crtc_state))) {
			dc_stream_retain(dm_new_crtc_state->stream);
			acrtc->dm_irq_params.stream = dm_new_crtc_state->stream;
			manage_dm_interrupts(adev, acrtc, dm_new_crtc_state);
		}
		/* Handle vrr on->off / off->on transitions */
		amdgpu_dm_handle_vrr_transition(dm_old_crtc_state, dm_new_crtc_state);

#ifdef CONFIG_DEBUG_FS
		if (new_crtc_state->active &&
		    (!old_crtc_state->active ||
		     drm_atomic_crtc_needs_modeset(new_crtc_state))) {
			/**
			 * Frontend may have changed so reapply the CRC capture
			 * settings for the stream.
			 */
			if (amdgpu_dm_is_valid_crc_source(cur_crc_src)) {
#if defined(CONFIG_DRM_AMD_SECURE_DISPLAY)
				if (amdgpu_dm_crc_window_is_activated(crtc)) {
					spin_lock_irqsave(&adev_to_drm(adev)->event_lock, flags);
					acrtc->dm_irq_params.window_param.update_win = true;

					/**
					 * It takes 2 frames for HW to stably generate CRC when
					 * resuming from suspend, so we set skip_frame_cnt 2.
					 */
					acrtc->dm_irq_params.window_param.skip_frame_cnt = 2;
					spin_unlock_irqrestore(&adev_to_drm(adev)->event_lock, flags);
				}
#endif
				if (amdgpu_dm_crtc_configure_crc_source(
					crtc, dm_new_crtc_state, cur_crc_src))
					drm_dbg_atomic(dev, "Failed to configure crc source");
			}
		}
#endif
	}

	for_each_new_crtc_in_state(state, crtc, new_crtc_state, j)
		if (new_crtc_state->async_flip)
			wait_for_vblank = false;

	/* update planes when needed per crtc*/
	for_each_new_crtc_in_state(state, crtc, new_crtc_state, j) {
		dm_new_crtc_state = to_dm_crtc_state(new_crtc_state);

		if (dm_new_crtc_state->stream)
			amdgpu_dm_commit_planes(state, dev, dm, crtc, wait_for_vblank);
	}

	/* Enable writeback */
	for_each_new_connector_in_state(state, connector, new_con_state, i) {
		struct dm_connector_state *dm_new_con_state = to_dm_connector_state(new_con_state);
		struct amdgpu_crtc *acrtc = to_amdgpu_crtc(dm_new_con_state->base.crtc);

		if (connector->connector_type != DRM_MODE_CONNECTOR_WRITEBACK)
			continue;

		if (!new_con_state->writeback_job)
			continue;

		new_crtc_state = drm_atomic_get_new_crtc_state(state, &acrtc->base);

		if (!new_crtc_state)
			continue;

		if (acrtc->wb_enabled)
			continue;

		dm_new_crtc_state = to_dm_crtc_state(new_crtc_state);

		dm_set_writeback(dm, dm_new_crtc_state, connector, new_con_state);
		acrtc->wb_enabled = true;
	}

	/* Update audio instances for each connector. */
	amdgpu_dm_commit_audio(dev, state);

	/* restore the backlight level */
	for (i = 0; i < dm->num_of_edps; i++) {
		if (dm->backlight_dev[i] &&
		    (dm->actual_brightness[i] != dm->brightness[i]))
			amdgpu_dm_backlight_set_level(dm, i, dm->brightness[i]);
	}

	/*
	 * send vblank event on all events not handled in flip and
	 * mark consumed event for drm_atomic_helper_commit_hw_done
	 */
	spin_lock_irqsave(&adev_to_drm(adev)->event_lock, flags);
	for_each_new_crtc_in_state(state, crtc, new_crtc_state, i) {

		if (new_crtc_state->event)
			drm_send_event_locked(dev, &new_crtc_state->event->base);

		new_crtc_state->event = NULL;
	}
	spin_unlock_irqrestore(&adev_to_drm(adev)->event_lock, flags);

	/* Signal HW programming completion */
	drm_atomic_helper_commit_hw_done(state);

	if (wait_for_vblank)
		drm_atomic_helper_wait_for_flip_done(dev, state);

	drm_atomic_helper_cleanup_planes(dev, state);

	/* Don't free the memory if we are hitting this as part of suspend.
	 * This way we don't free any memory during suspend; see
	 * amdgpu_bo_free_kernel().  The memory will be freed in the first
	 * non-suspend modeset or when the driver is torn down.
	 */
	if (!adev->in_suspend) {
		/* return the stolen vga memory back to VRAM */
		if (!adev->mman.keep_stolen_vga_memory)
			amdgpu_bo_free_kernel(&adev->mman.stolen_vga_memory, NULL, NULL);
		amdgpu_bo_free_kernel(&adev->mman.stolen_extended_memory, NULL, NULL);
	}

	/*
	 * Finally, drop a runtime PM reference for each newly disabled CRTC,
	 * so we can put the GPU into runtime suspend if we're not driving any
	 * displays anymore
	 */
	for (i = 0; i < crtc_disable_count; i++)
		pm_runtime_put_autosuspend(dev->dev);
	pm_runtime_mark_last_busy(dev->dev);
}

static int dm_force_atomic_commit(struct drm_connector *connector)
{
	int ret = 0;
	struct drm_device *ddev = connector->dev;
	struct drm_atomic_state *state = drm_atomic_state_alloc(ddev);
	struct amdgpu_crtc *disconnected_acrtc = to_amdgpu_crtc(connector->encoder->crtc);
	struct drm_plane *plane = disconnected_acrtc->base.primary;
	struct drm_connector_state *conn_state;
	struct drm_crtc_state *crtc_state;
	struct drm_plane_state *plane_state;

	if (!state)
		return -ENOMEM;

	state->acquire_ctx = ddev->mode_config.acquire_ctx;

	/* Construct an atomic state to restore previous display setting */

	/*
	 * Attach connectors to drm_atomic_state
	 */
	conn_state = drm_atomic_get_connector_state(state, connector);

	ret = PTR_ERR_OR_ZERO(conn_state);
	if (ret)
		goto out;

	/* Attach crtc to drm_atomic_state*/
	crtc_state = drm_atomic_get_crtc_state(state, &disconnected_acrtc->base);

	ret = PTR_ERR_OR_ZERO(crtc_state);
	if (ret)
		goto out;

	/* force a restore */
	crtc_state->mode_changed = true;

	/* Attach plane to drm_atomic_state */
	plane_state = drm_atomic_get_plane_state(state, plane);

	ret = PTR_ERR_OR_ZERO(plane_state);
	if (ret)
		goto out;

	/* Call commit internally with the state we just constructed */
	ret = drm_atomic_commit(state);

out:
	drm_atomic_state_put(state);
	if (ret)
		DRM_ERROR("Restoring old state failed with %i\n", ret);

	return ret;
}

/*
 * This function handles all cases when set mode does not come upon hotplug.
 * This includes when a display is unplugged then plugged back into the
 * same port and when running without usermode desktop manager supprot
 */
void dm_restore_drm_connector_state(struct drm_device *dev,
				    struct drm_connector *connector)
{
	struct amdgpu_dm_connector *aconnector;
	struct amdgpu_crtc *disconnected_acrtc;
	struct dm_crtc_state *acrtc_state;

	if (connector->connector_type == DRM_MODE_CONNECTOR_WRITEBACK)
		return;

	aconnector = to_amdgpu_dm_connector(connector);

	if (!aconnector->dc_sink || !connector->state || !connector->encoder)
		return;

	disconnected_acrtc = to_amdgpu_crtc(connector->encoder->crtc);
	if (!disconnected_acrtc)
		return;

	acrtc_state = to_dm_crtc_state(disconnected_acrtc->base.state);
	if (!acrtc_state->stream)
		return;

	/*
	 * If the previous sink is not released and different from the current,
	 * we deduce we are in a state where we can not rely on usermode call
	 * to turn on the display, so we do it here
	 */
	if (acrtc_state->stream->sink != aconnector->dc_sink)
		dm_force_atomic_commit(&aconnector->base);
}

/*
 * Grabs all modesetting locks to serialize against any blocking commits,
 * Waits for completion of all non blocking commits.
 */
static int do_aquire_global_lock(struct drm_device *dev,
				 struct drm_atomic_state *state)
{
	struct drm_crtc *crtc;
	struct drm_crtc_commit *commit;
	long ret;

	/*
	 * Adding all modeset locks to aquire_ctx will
	 * ensure that when the framework release it the
	 * extra locks we are locking here will get released to
	 */
	ret = drm_modeset_lock_all_ctx(dev, state->acquire_ctx);
	if (ret)
		return ret;

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		spin_lock(&crtc->commit_lock);
		commit = list_first_entry_or_null(&crtc->commit_list,
				struct drm_crtc_commit, commit_entry);
		if (commit)
			drm_crtc_commit_get(commit);
		spin_unlock(&crtc->commit_lock);

		if (!commit)
			continue;

		/*
		 * Make sure all pending HW programming completed and
		 * page flips done
		 */
		ret = wait_for_completion_interruptible_timeout(&commit->hw_done, 10*HZ);

		if (ret > 0)
			ret = wait_for_completion_interruptible_timeout(
					&commit->flip_done, 10*HZ);

		if (ret == 0)
			DRM_ERROR("[CRTC:%d:%s] hw_done or flip_done timed out\n",
				  crtc->base.id, crtc->name);

		drm_crtc_commit_put(commit);
	}

	return ret < 0 ? ret : 0;
}

static void get_freesync_config_for_crtc(
	struct dm_crtc_state *new_crtc_state,
	struct dm_connector_state *new_con_state)
{
	struct mod_freesync_config config = {0};
	struct amdgpu_dm_connector *aconnector;
	struct drm_display_mode *mode = &new_crtc_state->base.mode;
	int vrefresh = drm_mode_vrefresh(mode);
	bool fs_vid_mode = false;

	if (new_con_state->base.connector->connector_type == DRM_MODE_CONNECTOR_WRITEBACK)
		return;

	aconnector = to_amdgpu_dm_connector(new_con_state->base.connector);

	new_crtc_state->vrr_supported = new_con_state->freesync_capable &&
					vrefresh >= aconnector->min_vfreq &&
					vrefresh <= aconnector->max_vfreq;

	if (new_crtc_state->vrr_supported) {
		new_crtc_state->stream->ignore_msa_timing_param = true;
		fs_vid_mode = new_crtc_state->freesync_config.state == VRR_STATE_ACTIVE_FIXED;

		config.min_refresh_in_uhz = aconnector->min_vfreq * 1000000;
		config.max_refresh_in_uhz = aconnector->max_vfreq * 1000000;
		config.vsif_supported = true;
		config.btr = true;

		if (fs_vid_mode) {
			config.state = VRR_STATE_ACTIVE_FIXED;
			config.fixed_refresh_in_uhz = new_crtc_state->freesync_config.fixed_refresh_in_uhz;
			goto out;
		} else if (new_crtc_state->base.vrr_enabled) {
			config.state = VRR_STATE_ACTIVE_VARIABLE;
		} else {
			config.state = VRR_STATE_INACTIVE;
		}
	}
out:
	new_crtc_state->freesync_config = config;
}

static void reset_freesync_config_for_crtc(
	struct dm_crtc_state *new_crtc_state)
{
	new_crtc_state->vrr_supported = false;

	memset(&new_crtc_state->vrr_infopacket, 0,
	       sizeof(new_crtc_state->vrr_infopacket));
}

static bool
is_timing_unchanged_for_freesync(struct drm_crtc_state *old_crtc_state,
				 struct drm_crtc_state *new_crtc_state)
{
	const struct drm_display_mode *old_mode, *new_mode;

	if (!old_crtc_state || !new_crtc_state)
		return false;

	old_mode = &old_crtc_state->mode;
	new_mode = &new_crtc_state->mode;

	if (old_mode->clock       == new_mode->clock &&
	    old_mode->hdisplay    == new_mode->hdisplay &&
	    old_mode->vdisplay    == new_mode->vdisplay &&
	    old_mode->htotal      == new_mode->htotal &&
	    old_mode->vtotal      != new_mode->vtotal &&
	    old_mode->hsync_start == new_mode->hsync_start &&
	    old_mode->vsync_start != new_mode->vsync_start &&
	    old_mode->hsync_end   == new_mode->hsync_end &&
	    old_mode->vsync_end   != new_mode->vsync_end &&
	    old_mode->hskew       == new_mode->hskew &&
	    old_mode->vscan       == new_mode->vscan &&
	    (old_mode->vsync_end - old_mode->vsync_start) ==
	    (new_mode->vsync_end - new_mode->vsync_start))
		return true;

	return false;
}

static void set_freesync_fixed_config(struct dm_crtc_state *dm_new_crtc_state)
{
	u64 num, den, res;
	struct drm_crtc_state *new_crtc_state = &dm_new_crtc_state->base;

	dm_new_crtc_state->freesync_config.state = VRR_STATE_ACTIVE_FIXED;

	num = (unsigned long long)new_crtc_state->mode.clock * 1000 * 1000000;
	den = (unsigned long long)new_crtc_state->mode.htotal *
	      (unsigned long long)new_crtc_state->mode.vtotal;

	res = div_u64(num, den);
	dm_new_crtc_state->freesync_config.fixed_refresh_in_uhz = res;
}

static int dm_update_crtc_state(struct amdgpu_display_manager *dm,
			 struct drm_atomic_state *state,
			 struct drm_crtc *crtc,
			 struct drm_crtc_state *old_crtc_state,
			 struct drm_crtc_state *new_crtc_state,
			 bool enable,
			 bool *lock_and_validation_needed)
{
	struct dm_atomic_state *dm_state = NULL;
	struct dm_crtc_state *dm_old_crtc_state, *dm_new_crtc_state;
	struct dc_stream_state *new_stream;
	int ret = 0;

	/*
	 * TODO Move this code into dm_crtc_atomic_check once we get rid of dc_validation_set
	 * update changed items
	 */
	struct amdgpu_crtc *acrtc = NULL;
	struct drm_connector *connector = NULL;
	struct amdgpu_dm_connector *aconnector = NULL;
	struct drm_connector_state *drm_new_conn_state = NULL, *drm_old_conn_state = NULL;
	struct dm_connector_state *dm_new_conn_state = NULL, *dm_old_conn_state = NULL;

	new_stream = NULL;

	dm_old_crtc_state = to_dm_crtc_state(old_crtc_state);
	dm_new_crtc_state = to_dm_crtc_state(new_crtc_state);
	acrtc = to_amdgpu_crtc(crtc);
	connector = amdgpu_dm_find_first_crtc_matching_connector(state, crtc);
	if (connector)
		aconnector = to_amdgpu_dm_connector(connector);

	/* TODO This hack should go away */
	if (connector && enable) {
		/* Make sure fake sink is created in plug-in scenario */
		drm_new_conn_state = drm_atomic_get_new_connector_state(state,
									connector);
		drm_old_conn_state = drm_atomic_get_old_connector_state(state,
									connector);

		if (IS_ERR(drm_new_conn_state)) {
			ret = PTR_ERR_OR_ZERO(drm_new_conn_state);
			goto fail;
		}

		dm_new_conn_state = to_dm_connector_state(drm_new_conn_state);
		dm_old_conn_state = to_dm_connector_state(drm_old_conn_state);

		if (!drm_atomic_crtc_needs_modeset(new_crtc_state))
			goto skip_modeset;

		new_stream = create_validate_stream_for_sink(connector,
							     &new_crtc_state->mode,
							     dm_new_conn_state,
							     dm_old_crtc_state->stream);

		/*
		 * we can have no stream on ACTION_SET if a display
		 * was disconnected during S3, in this case it is not an
		 * error, the OS will be updated after detection, and
		 * will do the right thing on next atomic commit
		 */

		if (!new_stream) {
			DRM_DEBUG_DRIVER("%s: Failed to create new stream for crtc %d\n",
					__func__, acrtc->base.base.id);
			ret = -ENOMEM;
			goto fail;
		}

		/*
		 * TODO: Check VSDB bits to decide whether this should
		 * be enabled or not.
		 */
		new_stream->triggered_crtc_reset.enabled =
			dm->force_timing_sync;

		dm_new_crtc_state->abm_level = dm_new_conn_state->abm_level;

		ret = fill_hdr_info_packet(drm_new_conn_state,
					   &new_stream->hdr_static_metadata);
		if (ret)
			goto fail;

		/*
		 * If we already removed the old stream from the context
		 * (and set the new stream to NULL) then we can't reuse
		 * the old stream even if the stream and scaling are unchanged.
		 * We'll hit the BUG_ON and black screen.
		 *
		 * TODO: Refactor this function to allow this check to work
		 * in all conditions.
		 */
		if (amdgpu_freesync_vid_mode &&
		    dm_new_crtc_state->stream &&
		    is_timing_unchanged_for_freesync(new_crtc_state, old_crtc_state))
			goto skip_modeset;

		if (dm_new_crtc_state->stream &&
		    dc_is_stream_unchanged(new_stream, dm_old_crtc_state->stream) &&
		    dc_is_stream_scaling_unchanged(new_stream, dm_old_crtc_state->stream)) {
			new_crtc_state->mode_changed = false;
			DRM_DEBUG_DRIVER("Mode change not required, setting mode_changed to %d",
					 new_crtc_state->mode_changed);
		}
	}

	/* mode_changed flag may get updated above, need to check again */
	if (!drm_atomic_crtc_needs_modeset(new_crtc_state))
		goto skip_modeset;

	drm_dbg_state(state->dev,
		"amdgpu_crtc id:%d crtc_state_flags: enable:%d, active:%d, planes_changed:%d, mode_changed:%d,active_changed:%d,connectors_changed:%d\n",
		acrtc->crtc_id,
		new_crtc_state->enable,
		new_crtc_state->active,
		new_crtc_state->planes_changed,
		new_crtc_state->mode_changed,
		new_crtc_state->active_changed,
		new_crtc_state->connectors_changed);

	/* Remove stream for any changed/disabled CRTC */
	if (!enable) {

		if (!dm_old_crtc_state->stream)
			goto skip_modeset;

		/* Unset freesync video if it was active before */
		if (dm_old_crtc_state->freesync_config.state == VRR_STATE_ACTIVE_FIXED) {
			dm_new_crtc_state->freesync_config.state = VRR_STATE_INACTIVE;
			dm_new_crtc_state->freesync_config.fixed_refresh_in_uhz = 0;
		}

		/* Now check if we should set freesync video mode */
		if (amdgpu_freesync_vid_mode && dm_new_crtc_state->stream &&
		    dc_is_stream_unchanged(new_stream, dm_old_crtc_state->stream) &&
		    dc_is_stream_scaling_unchanged(new_stream, dm_old_crtc_state->stream) &&
		    is_timing_unchanged_for_freesync(new_crtc_state,
						     old_crtc_state)) {
			new_crtc_state->mode_changed = false;
			DRM_DEBUG_DRIVER(
				"Mode change not required for front porch change, setting mode_changed to %d",
				new_crtc_state->mode_changed);

			set_freesync_fixed_config(dm_new_crtc_state);

			goto skip_modeset;
		} else if (amdgpu_freesync_vid_mode && aconnector &&
			   is_freesync_video_mode(&new_crtc_state->mode,
						  aconnector)) {
			struct drm_display_mode *high_mode;

			high_mode = get_highest_refresh_rate_mode(aconnector, false);
			if (!drm_mode_equal(&new_crtc_state->mode, high_mode))
				set_freesync_fixed_config(dm_new_crtc_state);
		}

		ret = dm_atomic_get_state(state, &dm_state);
		if (ret)
			goto fail;

		DRM_DEBUG_DRIVER("Disabling DRM crtc: %d\n",
				crtc->base.id);

		/* i.e. reset mode */
		if (dc_state_remove_stream(
				dm->dc,
				dm_state->context,
				dm_old_crtc_state->stream) != DC_OK) {
			ret = -EINVAL;
			goto fail;
		}

		dc_stream_release(dm_old_crtc_state->stream);
		dm_new_crtc_state->stream = NULL;

		reset_freesync_config_for_crtc(dm_new_crtc_state);

		*lock_and_validation_needed = true;

	} else {/* Add stream for any updated/enabled CRTC */
		/*
		 * Quick fix to prevent NULL pointer on new_stream when
		 * added MST connectors not found in existing crtc_state in the chained mode
		 * TODO: need to dig out the root cause of that
		 */
		if (!connector)
			goto skip_modeset;

		if (modereset_required(new_crtc_state))
			goto skip_modeset;

		if (amdgpu_dm_crtc_modeset_required(new_crtc_state, new_stream,
				     dm_old_crtc_state->stream)) {

			WARN_ON(dm_new_crtc_state->stream);

			ret = dm_atomic_get_state(state, &dm_state);
			if (ret)
				goto fail;

			dm_new_crtc_state->stream = new_stream;

			dc_stream_retain(new_stream);

			DRM_DEBUG_ATOMIC("Enabling DRM crtc: %d\n",
					 crtc->base.id);

			if (dc_state_add_stream(
					dm->dc,
					dm_state->context,
					dm_new_crtc_state->stream) != DC_OK) {
				ret = -EINVAL;
				goto fail;
			}

			*lock_and_validation_needed = true;
		}
	}

skip_modeset:
	/* Release extra reference */
	if (new_stream)
		dc_stream_release(new_stream);

	/*
	 * We want to do dc stream updates that do not require a
	 * full modeset below.
	 */
	if (!(enable && connector && new_crtc_state->active))
		return 0;
	/*
	 * Given above conditions, the dc state cannot be NULL because:
	 * 1. We're in the process of enabling CRTCs (just been added
	 *    to the dc context, or already is on the context)
	 * 2. Has a valid connector attached, and
	 * 3. Is currently active and enabled.
	 * => The dc stream state currently exists.
	 */
	BUG_ON(dm_new_crtc_state->stream == NULL);

	/* Scaling or underscan settings */
	if (is_scaling_state_different(dm_old_conn_state, dm_new_conn_state) ||
				drm_atomic_crtc_needs_modeset(new_crtc_state))
		update_stream_scaling_settings(
			&new_crtc_state->mode, dm_new_conn_state, dm_new_crtc_state->stream);

	/* ABM settings */
	dm_new_crtc_state->abm_level = dm_new_conn_state->abm_level;

	/*
	 * Color management settings. We also update color properties
	 * when a modeset is needed, to ensure it gets reprogrammed.
	 */
	if (dm_new_crtc_state->base.color_mgmt_changed ||
	    dm_old_crtc_state->regamma_tf != dm_new_crtc_state->regamma_tf ||
	    drm_atomic_crtc_needs_modeset(new_crtc_state)) {
		ret = amdgpu_dm_update_crtc_color_mgmt(dm_new_crtc_state);
		if (ret)
			goto fail;
	}

	/* Update Freesync settings. */
	get_freesync_config_for_crtc(dm_new_crtc_state,
				     dm_new_conn_state);

	return ret;

fail:
	if (new_stream)
		dc_stream_release(new_stream);
	return ret;
}

static bool should_reset_plane(struct drm_atomic_state *state,
			       struct drm_plane *plane,
			       struct drm_plane_state *old_plane_state,
			       struct drm_plane_state *new_plane_state)
{
	struct drm_plane *other;
	struct drm_plane_state *old_other_state, *new_other_state;
	struct drm_crtc_state *old_crtc_state, *new_crtc_state;
	struct dm_crtc_state *old_dm_crtc_state, *new_dm_crtc_state;
	struct amdgpu_device *adev = drm_to_adev(plane->dev);
	int i;

	/*
	 * TODO: Remove this hack for all asics once it proves that the
	 * fast updates works fine on DCN3.2+.
	 */
	if (amdgpu_ip_version(adev, DCE_HWIP, 0) < IP_VERSION(3, 2, 0) &&
	    state->allow_modeset)
		return true;

	if (amdgpu_in_reset(adev) && state->allow_modeset)
		return true;

	/* Exit early if we know that we're adding or removing the plane. */
	if (old_plane_state->crtc != new_plane_state->crtc)
		return true;

	/* old crtc == new_crtc == NULL, plane not in context. */
	if (!new_plane_state->crtc)
		return false;

	new_crtc_state =
		drm_atomic_get_new_crtc_state(state, new_plane_state->crtc);
	old_crtc_state =
		drm_atomic_get_old_crtc_state(state, old_plane_state->crtc);

	if (!new_crtc_state)
		return true;

	/*
	 * A change in cursor mode means a new dc pipe needs to be acquired or
	 * released from the state
	 */
	old_dm_crtc_state = to_dm_crtc_state(old_crtc_state);
	new_dm_crtc_state = to_dm_crtc_state(new_crtc_state);
	if (plane->type == DRM_PLANE_TYPE_CURSOR &&
	    old_dm_crtc_state != NULL &&
	    old_dm_crtc_state->cursor_mode != new_dm_crtc_state->cursor_mode) {
		return true;
	}

	/* CRTC Degamma changes currently require us to recreate planes. */
	if (new_crtc_state->color_mgmt_changed)
		return true;

	/*
	 * On zpos change, planes need to be reordered by removing and re-adding
	 * them one by one to the dc state, in order of descending zpos.
	 *
	 * TODO: We can likely skip bandwidth validation if the only thing that
	 * changed about the plane was it'z z-ordering.
	 */
	if (old_plane_state->normalized_zpos != new_plane_state->normalized_zpos)
		return true;

	if (drm_atomic_crtc_needs_modeset(new_crtc_state))
		return true;

	/*
	 * If there are any new primary or overlay planes being added or
	 * removed then the z-order can potentially change. To ensure
	 * correct z-order and pipe acquisition the current DC architecture
	 * requires us to remove and recreate all existing planes.
	 *
	 * TODO: Come up with a more elegant solution for this.
	 */
	for_each_oldnew_plane_in_state(state, other, old_other_state, new_other_state, i) {
		struct amdgpu_framebuffer *old_afb, *new_afb;
		struct dm_plane_state *dm_new_other_state, *dm_old_other_state;

		dm_new_other_state = to_dm_plane_state(new_other_state);
		dm_old_other_state = to_dm_plane_state(old_other_state);

		if (other->type == DRM_PLANE_TYPE_CURSOR)
			continue;

		if (old_other_state->crtc != new_plane_state->crtc &&
		    new_other_state->crtc != new_plane_state->crtc)
			continue;

		if (old_other_state->crtc != new_other_state->crtc)
			return true;

		/* Src/dst size and scaling updates. */
		if (old_other_state->src_w != new_other_state->src_w ||
		    old_other_state->src_h != new_other_state->src_h ||
		    old_other_state->crtc_w != new_other_state->crtc_w ||
		    old_other_state->crtc_h != new_other_state->crtc_h)
			return true;

		/* Rotation / mirroring updates. */
		if (old_other_state->rotation != new_other_state->rotation)
			return true;

		/* Blending updates. */
		if (old_other_state->pixel_blend_mode !=
		    new_other_state->pixel_blend_mode)
			return true;

		/* Alpha updates. */
		if (old_other_state->alpha != new_other_state->alpha)
			return true;

		/* Colorspace changes. */
		if (old_other_state->color_range != new_other_state->color_range ||
		    old_other_state->color_encoding != new_other_state->color_encoding)
			return true;

		/* HDR/Transfer Function changes. */
		if (dm_old_other_state->degamma_tf != dm_new_other_state->degamma_tf ||
		    dm_old_other_state->degamma_lut != dm_new_other_state->degamma_lut ||
		    dm_old_other_state->hdr_mult != dm_new_other_state->hdr_mult ||
		    dm_old_other_state->ctm != dm_new_other_state->ctm ||
		    dm_old_other_state->shaper_lut != dm_new_other_state->shaper_lut ||
		    dm_old_other_state->shaper_tf != dm_new_other_state->shaper_tf ||
		    dm_old_other_state->lut3d != dm_new_other_state->lut3d ||
		    dm_old_other_state->blend_lut != dm_new_other_state->blend_lut ||
		    dm_old_other_state->blend_tf != dm_new_other_state->blend_tf)
			return true;

		/* Framebuffer checks fall at the end. */
		if (!old_other_state->fb || !new_other_state->fb)
			continue;

		/* Pixel format changes can require bandwidth updates. */
		if (old_other_state->fb->format != new_other_state->fb->format)
			return true;

		old_afb = (struct amdgpu_framebuffer *)old_other_state->fb;
		new_afb = (struct amdgpu_framebuffer *)new_other_state->fb;

		/* Tiling and DCC changes also require bandwidth updates. */
		if (old_afb->tiling_flags != new_afb->tiling_flags ||
		    old_afb->base.modifier != new_afb->base.modifier)
			return true;
	}

	return false;
}

static int dm_check_cursor_fb(struct amdgpu_crtc *new_acrtc,
			      struct drm_plane_state *new_plane_state,
			      struct drm_framebuffer *fb)
{
	struct amdgpu_device *adev = drm_to_adev(new_acrtc->base.dev);
	struct amdgpu_framebuffer *afb = to_amdgpu_framebuffer(fb);
	unsigned int pitch;
	bool linear;

	if (fb->width > new_acrtc->max_cursor_width ||
	    fb->height > new_acrtc->max_cursor_height) {
		DRM_DEBUG_ATOMIC("Bad cursor FB size %dx%d\n",
				 new_plane_state->fb->width,
				 new_plane_state->fb->height);
		return -EINVAL;
	}
	if (new_plane_state->src_w != fb->width << 16 ||
	    new_plane_state->src_h != fb->height << 16) {
		DRM_DEBUG_ATOMIC("Cropping not supported for cursor plane\n");
		return -EINVAL;
	}

	/* Pitch in pixels */
	pitch = fb->pitches[0] / fb->format->cpp[0];

	if (fb->width != pitch) {
		DRM_DEBUG_ATOMIC("Cursor FB width %d doesn't match pitch %d",
				 fb->width, pitch);
		return -EINVAL;
	}

	switch (pitch) {
	case 64:
	case 128:
	case 256:
		/* FB pitch is supported by cursor plane */
		break;
	default:
		DRM_DEBUG_ATOMIC("Bad cursor FB pitch %d px\n", pitch);
		return -EINVAL;
	}

	/* Core DRM takes care of checking FB modifiers, so we only need to
	 * check tiling flags when the FB doesn't have a modifier.
	 */
	if (!(fb->flags & DRM_MODE_FB_MODIFIERS)) {
		if (adev->family >= AMDGPU_FAMILY_GC_12_0_0) {
			linear = AMDGPU_TILING_GET(afb->tiling_flags, GFX12_SWIZZLE_MODE) == 0;
		} else if (adev->family >= AMDGPU_FAMILY_AI) {
			linear = AMDGPU_TILING_GET(afb->tiling_flags, SWIZZLE_MODE) == 0;
		} else {
			linear = AMDGPU_TILING_GET(afb->tiling_flags, ARRAY_MODE) != DC_ARRAY_2D_TILED_THIN1 &&
				 AMDGPU_TILING_GET(afb->tiling_flags, ARRAY_MODE) != DC_ARRAY_1D_TILED_THIN1 &&
				 AMDGPU_TILING_GET(afb->tiling_flags, MICRO_TILE_MODE) == 0;
		}
		if (!linear) {
			DRM_DEBUG_ATOMIC("Cursor FB not linear");
			return -EINVAL;
		}
	}

	return 0;
}

/*
 * Helper function for checking the cursor in native mode
 */
static int dm_check_native_cursor_state(struct drm_crtc *new_plane_crtc,
					struct drm_plane *plane,
					struct drm_plane_state *new_plane_state,
					bool enable)
{

	struct amdgpu_crtc *new_acrtc;
	int ret;

	if (!enable || !new_plane_crtc ||
	    drm_atomic_plane_disabling(plane->state, new_plane_state))
		return 0;

	new_acrtc = to_amdgpu_crtc(new_plane_crtc);

	if (new_plane_state->src_x != 0 || new_plane_state->src_y != 0) {
		DRM_DEBUG_ATOMIC("Cropping not supported for cursor plane\n");
		return -EINVAL;
	}

	if (new_plane_state->fb) {
		ret = dm_check_cursor_fb(new_acrtc, new_plane_state,
						new_plane_state->fb);
		if (ret)
			return ret;
	}

	return 0;
}

static bool dm_should_update_native_cursor(struct drm_atomic_state *state,
					   struct drm_crtc *old_plane_crtc,
					   struct drm_crtc *new_plane_crtc,
					   bool enable)
{
	struct drm_crtc_state *old_crtc_state, *new_crtc_state;
	struct dm_crtc_state *dm_old_crtc_state, *dm_new_crtc_state;

	if (!enable) {
		if (old_plane_crtc == NULL)
			return true;

		old_crtc_state = drm_atomic_get_old_crtc_state(
			state, old_plane_crtc);
		dm_old_crtc_state = to_dm_crtc_state(old_crtc_state);

		return dm_old_crtc_state->cursor_mode == DM_CURSOR_NATIVE_MODE;
	} else {
		if (new_plane_crtc == NULL)
			return true;

		new_crtc_state = drm_atomic_get_new_crtc_state(
			state, new_plane_crtc);
		dm_new_crtc_state = to_dm_crtc_state(new_crtc_state);

		return dm_new_crtc_state->cursor_mode == DM_CURSOR_NATIVE_MODE;
	}
}

static int dm_update_plane_state(struct dc *dc,
				 struct drm_atomic_state *state,
				 struct drm_plane *plane,
				 struct drm_plane_state *old_plane_state,
				 struct drm_plane_state *new_plane_state,
				 bool enable,
				 bool *lock_and_validation_needed,
				 bool *is_top_most_overlay)
{

	struct dm_atomic_state *dm_state = NULL;
	struct drm_crtc *new_plane_crtc, *old_plane_crtc;
	struct drm_crtc_state *old_crtc_state, *new_crtc_state;
	struct dm_crtc_state *dm_new_crtc_state, *dm_old_crtc_state;
	struct dm_plane_state *dm_new_plane_state, *dm_old_plane_state;
	bool needs_reset, update_native_cursor;
	int ret = 0;


	new_plane_crtc = new_plane_state->crtc;
	old_plane_crtc = old_plane_state->crtc;
	dm_new_plane_state = to_dm_plane_state(new_plane_state);
	dm_old_plane_state = to_dm_plane_state(old_plane_state);

	update_native_cursor = dm_should_update_native_cursor(state,
							      old_plane_crtc,
							      new_plane_crtc,
							      enable);

	if (plane->type == DRM_PLANE_TYPE_CURSOR && update_native_cursor) {
		ret = dm_check_native_cursor_state(new_plane_crtc, plane,
						    new_plane_state, enable);
		if (ret)
			return ret;

		return 0;
	}

	needs_reset = should_reset_plane(state, plane, old_plane_state,
					 new_plane_state);

	/* Remove any changed/removed planes */
	if (!enable) {
		if (!needs_reset)
			return 0;

		if (!old_plane_crtc)
			return 0;

		old_crtc_state = drm_atomic_get_old_crtc_state(
				state, old_plane_crtc);
		dm_old_crtc_state = to_dm_crtc_state(old_crtc_state);

		if (!dm_old_crtc_state->stream)
			return 0;

		DRM_DEBUG_ATOMIC("Disabling DRM plane: %d on DRM crtc %d\n",
				plane->base.id, old_plane_crtc->base.id);

		ret = dm_atomic_get_state(state, &dm_state);
		if (ret)
			return ret;

		if (!dc_state_remove_plane(
				dc,
				dm_old_crtc_state->stream,
				dm_old_plane_state->dc_state,
				dm_state->context)) {

			return -EINVAL;
		}

		if (dm_old_plane_state->dc_state)
			dc_plane_state_release(dm_old_plane_state->dc_state);

		dm_new_plane_state->dc_state = NULL;

		*lock_and_validation_needed = true;

	} else { /* Add new planes */
		struct dc_plane_state *dc_new_plane_state;

		if (drm_atomic_plane_disabling(plane->state, new_plane_state))
			return 0;

		if (!new_plane_crtc)
			return 0;

		new_crtc_state = drm_atomic_get_new_crtc_state(state, new_plane_crtc);
		dm_new_crtc_state = to_dm_crtc_state(new_crtc_state);

		if (!dm_new_crtc_state->stream)
			return 0;

		if (!needs_reset)
			return 0;

		ret = amdgpu_dm_plane_helper_check_state(new_plane_state, new_crtc_state);
		if (ret)
			goto out;

		WARN_ON(dm_new_plane_state->dc_state);

		dc_new_plane_state = dc_create_plane_state(dc);
		if (!dc_new_plane_state) {
			ret = -ENOMEM;
			goto out;
		}

		DRM_DEBUG_ATOMIC("Enabling DRM plane: %d on DRM crtc %d\n",
				 plane->base.id, new_plane_crtc->base.id);

		ret = fill_dc_plane_attributes(
			drm_to_adev(new_plane_crtc->dev),
			dc_new_plane_state,
			new_plane_state,
			new_crtc_state);
		if (ret) {
			dc_plane_state_release(dc_new_plane_state);
			goto out;
		}

		ret = dm_atomic_get_state(state, &dm_state);
		if (ret) {
			dc_plane_state_release(dc_new_plane_state);
			goto out;
		}

		/*
		 * Any atomic check errors that occur after this will
		 * not need a release. The plane state will be attached
		 * to the stream, and therefore part of the atomic
		 * state. It'll be released when the atomic state is
		 * cleaned.
		 */
		if (!dc_state_add_plane(
				dc,
				dm_new_crtc_state->stream,
				dc_new_plane_state,
				dm_state->context)) {

			dc_plane_state_release(dc_new_plane_state);
			ret = -EINVAL;
			goto out;
		}

		dm_new_plane_state->dc_state = dc_new_plane_state;

		dm_new_crtc_state->mpo_requested |= (plane->type == DRM_PLANE_TYPE_OVERLAY);

		/* Tell DC to do a full surface update every time there
		 * is a plane change. Inefficient, but works for now.
		 */
		dm_new_plane_state->dc_state->update_flags.bits.full_update = 1;

		*lock_and_validation_needed = true;
	}

out:
	/* If enabling cursor overlay failed, attempt fallback to native mode */
	if (enable && ret == -EINVAL && plane->type == DRM_PLANE_TYPE_CURSOR) {
		ret = dm_check_native_cursor_state(new_plane_crtc, plane,
						    new_plane_state, enable);
		if (ret)
			return ret;

		dm_new_crtc_state->cursor_mode = DM_CURSOR_NATIVE_MODE;
	}

	return ret;
}

static void dm_get_oriented_plane_size(struct drm_plane_state *plane_state,
				       int *src_w, int *src_h)
{
	switch (plane_state->rotation & DRM_MODE_ROTATE_MASK) {
	case DRM_MODE_ROTATE_90:
	case DRM_MODE_ROTATE_270:
		*src_w = plane_state->src_h >> 16;
		*src_h = plane_state->src_w >> 16;
		break;
	case DRM_MODE_ROTATE_0:
	case DRM_MODE_ROTATE_180:
	default:
		*src_w = plane_state->src_w >> 16;
		*src_h = plane_state->src_h >> 16;
		break;
	}
}

static void
dm_get_plane_scale(struct drm_plane_state *plane_state,
		   int *out_plane_scale_w, int *out_plane_scale_h)
{
	int plane_src_w, plane_src_h;

	dm_get_oriented_plane_size(plane_state, &plane_src_w, &plane_src_h);
	*out_plane_scale_w = plane_src_w ? plane_state->crtc_w * 1000 / plane_src_w : 0;
	*out_plane_scale_h = plane_src_h ? plane_state->crtc_h * 1000 / plane_src_h : 0;
}

/*
 * The normalized_zpos value cannot be used by this iterator directly. It's only
 * calculated for enabled planes, potentially causing normalized_zpos collisions
 * between enabled/disabled planes in the atomic state. We need a unique value
 * so that the iterator will not generate the same object twice, or loop
 * indefinitely.
 */
static inline struct __drm_planes_state *__get_next_zpos(
	struct drm_atomic_state *state,
	struct __drm_planes_state *prev)
{
	unsigned int highest_zpos = 0, prev_zpos = 256;
	uint32_t highest_id = 0, prev_id = UINT_MAX;
	struct drm_plane_state *new_plane_state;
	struct drm_plane *plane;
	int i, highest_i = -1;

	if (prev != NULL) {
		prev_zpos = prev->new_state->zpos;
		prev_id = prev->ptr->base.id;
	}

	for_each_new_plane_in_state(state, plane, new_plane_state, i) {
		/* Skip planes with higher zpos than the previously returned */
		if (new_plane_state->zpos > prev_zpos ||
		    (new_plane_state->zpos == prev_zpos &&
		     plane->base.id >= prev_id))
			continue;

		/* Save the index of the plane with highest zpos */
		if (new_plane_state->zpos > highest_zpos ||
		    (new_plane_state->zpos == highest_zpos &&
		     plane->base.id > highest_id)) {
			highest_zpos = new_plane_state->zpos;
			highest_id = plane->base.id;
			highest_i = i;
		}
	}

	if (highest_i < 0)
		return NULL;

	return &state->planes[highest_i];
}

/*
 * Use the uniqueness of the plane's (zpos, drm obj ID) combination to iterate
 * by descending zpos, as read from the new plane state. This is the same
 * ordering as defined by drm_atomic_normalize_zpos().
 */
#define for_each_oldnew_plane_in_descending_zpos(__state, plane, old_plane_state, new_plane_state) \
	for (struct __drm_planes_state *__i = __get_next_zpos((__state), NULL); \
	     __i != NULL; __i = __get_next_zpos((__state), __i))		\
		for_each_if(((plane) = __i->ptr,				\
			     (void)(plane) /* Only to avoid unused-but-set-variable warning */, \
			     (old_plane_state) = __i->old_state,		\
			     (new_plane_state) = __i->new_state, 1))

static int add_affected_mst_dsc_crtcs(struct drm_atomic_state *state, struct drm_crtc *crtc)
{
	struct drm_connector *connector;
	struct drm_connector_state *conn_state, *old_conn_state;
	struct amdgpu_dm_connector *aconnector = NULL;
	int i;

	for_each_oldnew_connector_in_state(state, connector, old_conn_state, conn_state, i) {
		if (!conn_state->crtc)
			conn_state = old_conn_state;

		if (conn_state->crtc != crtc)
			continue;

		if (connector->connector_type == DRM_MODE_CONNECTOR_WRITEBACK)
			continue;

		aconnector = to_amdgpu_dm_connector(connector);
		if (!aconnector->mst_output_port || !aconnector->mst_root)
			aconnector = NULL;
		else
			break;
	}

	if (!aconnector)
		return 0;

	return drm_dp_mst_add_affected_dsc_crtcs(state, &aconnector->mst_root->mst_mgr);
}

/**
 * DOC: Cursor Modes - Native vs Overlay
 *
 * In native mode, the cursor uses a integrated cursor pipe within each DCN hw
 * plane. It does not require a dedicated hw plane to enable, but it is
 * subjected to the same z-order and scaling as the hw plane. It also has format
 * restrictions, a RGB cursor in native mode cannot be enabled within a non-RGB
 * hw plane.
 *
 * In overlay mode, the cursor uses a separate DCN hw plane, and thus has its
 * own scaling and z-pos. It also has no blending restrictions. It lends to a
 * cursor behavior more akin to a DRM client's expectations. However, it does
 * occupy an extra DCN plane, and therefore will only be used if a DCN plane is
 * available.
 */

/**
 * dm_crtc_get_cursor_mode() - Determine the required cursor mode on crtc
 * @adev: amdgpu device
 * @state: DRM atomic state
 * @dm_crtc_state: amdgpu state for the CRTC containing the cursor
 * @cursor_mode: Returns the required cursor mode on dm_crtc_state
 *
 * Get whether the cursor should be enabled in native mode, or overlay mode, on
 * the dm_crtc_state.
 *
 * The cursor should be enabled in overlay mode if there exists an underlying
 * plane - on which the cursor may be blended - that is either YUV formatted, or
 * scaled differently from the cursor.
 *
 * Since zpos info is required, drm_atomic_normalize_zpos must be called before
 * calling this function.
 *
 * Return: 0 on success, or an error code if getting the cursor plane state
 * failed.
 */
static int dm_crtc_get_cursor_mode(struct amdgpu_device *adev,
				   struct drm_atomic_state *state,
				   struct dm_crtc_state *dm_crtc_state,
				   enum amdgpu_dm_cursor_mode *cursor_mode)
{
	struct drm_plane_state *old_plane_state, *plane_state, *cursor_state;
	struct drm_crtc_state *crtc_state = &dm_crtc_state->base;
	struct drm_plane *plane;
	bool consider_mode_change = false;
	bool entire_crtc_covered = false;
	bool cursor_changed = false;
	int underlying_scale_w, underlying_scale_h;
	int cursor_scale_w, cursor_scale_h;
	int i;

	/* Overlay cursor not supported on HW before DCN
	 * DCN401 does not have the cursor-on-scaled-plane or cursor-on-yuv-plane restrictions
	 * as previous DCN generations, so enable native mode on DCN401 in addition to DCE
	 */
	if (amdgpu_ip_version(adev, DCE_HWIP, 0) == 0 ||
	    amdgpu_ip_version(adev, DCE_HWIP, 0) == IP_VERSION(4, 0, 1)) {
		*cursor_mode = DM_CURSOR_NATIVE_MODE;
		return 0;
	}

	/* Init cursor_mode to be the same as current */
	*cursor_mode = dm_crtc_state->cursor_mode;

	/*
	 * Cursor mode can change if a plane's format changes, scale changes, is
	 * enabled/disabled, or z-order changes.
	 */
	for_each_oldnew_plane_in_state(state, plane, old_plane_state, plane_state, i) {
		int new_scale_w, new_scale_h, old_scale_w, old_scale_h;

		/* Only care about planes on this CRTC */
		if ((drm_plane_mask(plane) & crtc_state->plane_mask) == 0)
			continue;

		if (plane->type == DRM_PLANE_TYPE_CURSOR)
			cursor_changed = true;

		if (drm_atomic_plane_enabling(old_plane_state, plane_state) ||
		    drm_atomic_plane_disabling(old_plane_state, plane_state) ||
		    old_plane_state->fb->format != plane_state->fb->format) {
			consider_mode_change = true;
			break;
		}

		dm_get_plane_scale(plane_state, &new_scale_w, &new_scale_h);
		dm_get_plane_scale(old_plane_state, &old_scale_w, &old_scale_h);
		if (new_scale_w != old_scale_w || new_scale_h != old_scale_h) {
			consider_mode_change = true;
			break;
		}
	}

	if (!consider_mode_change && !crtc_state->zpos_changed)
		return 0;

	/*
	 * If no cursor change on this CRTC, and not enabled on this CRTC, then
	 * no need to set cursor mode. This avoids needlessly locking the cursor
	 * state.
	 */
	if (!cursor_changed &&
	    !(drm_plane_mask(crtc_state->crtc->cursor) & crtc_state->plane_mask)) {
		return 0;
	}

	cursor_state = drm_atomic_get_plane_state(state,
						  crtc_state->crtc->cursor);
	if (IS_ERR(cursor_state))
		return PTR_ERR(cursor_state);

	/* Cursor is disabled */
	if (!cursor_state->fb)
		return 0;

	/* For all planes in descending z-order (all of which are below cursor
	 * as per zpos definitions), check their scaling and format
	 */
	for_each_oldnew_plane_in_descending_zpos(state, plane, old_plane_state, plane_state) {

		/* Only care about non-cursor planes on this CRTC */
		if ((drm_plane_mask(plane) & crtc_state->plane_mask) == 0 ||
		    plane->type == DRM_PLANE_TYPE_CURSOR)
			continue;

		/* Underlying plane is YUV format - use overlay cursor */
		if (amdgpu_dm_plane_is_video_format(plane_state->fb->format->format)) {
			*cursor_mode = DM_CURSOR_OVERLAY_MODE;
			return 0;
		}

		dm_get_plane_scale(plane_state,
				   &underlying_scale_w, &underlying_scale_h);
		dm_get_plane_scale(cursor_state,
				   &cursor_scale_w, &cursor_scale_h);

		/* Underlying plane has different scale - use overlay cursor */
		if (cursor_scale_w != underlying_scale_w &&
		    cursor_scale_h != underlying_scale_h) {
			*cursor_mode = DM_CURSOR_OVERLAY_MODE;
			return 0;
		}

		/* If this plane covers the whole CRTC, no need to check planes underneath */
		if (plane_state->crtc_x <= 0 && plane_state->crtc_y <= 0 &&
		    plane_state->crtc_x + plane_state->crtc_w >= crtc_state->mode.hdisplay &&
		    plane_state->crtc_y + plane_state->crtc_h >= crtc_state->mode.vdisplay) {
			entire_crtc_covered = true;
			break;
		}
	}

	/* If planes do not cover the entire CRTC, use overlay mode to enable
	 * cursor over holes
	 */
	if (entire_crtc_covered)
		*cursor_mode = DM_CURSOR_NATIVE_MODE;
	else
		*cursor_mode = DM_CURSOR_OVERLAY_MODE;

	return 0;
}

static bool amdgpu_dm_crtc_mem_type_changed(struct drm_device *dev,
					    struct drm_atomic_state *state,
					    struct drm_crtc_state *crtc_state)
{
	struct drm_plane *plane;
	struct drm_plane_state *new_plane_state, *old_plane_state;

	drm_for_each_plane_mask(plane, dev, crtc_state->plane_mask) {
		new_plane_state = drm_atomic_get_plane_state(state, plane);
		old_plane_state = drm_atomic_get_plane_state(state, plane);

		if (IS_ERR(new_plane_state) || IS_ERR(old_plane_state)) {
			DRM_ERROR("Failed to get plane state for plane %s\n", plane->name);
			return false;
		}

		if (old_plane_state->fb && new_plane_state->fb &&
		    get_mem_type(old_plane_state->fb) != get_mem_type(new_plane_state->fb))
			return true;
	}

	return false;
}

/**
 * amdgpu_dm_atomic_check() - Atomic check implementation for AMDgpu DM.
 *
 * @dev: The DRM device
 * @state: The atomic state to commit
 *
 * Validate that the given atomic state is programmable by DC into hardware.
 * This involves constructing a &struct dc_state reflecting the new hardware
 * state we wish to commit, then querying DC to see if it is programmable. It's
 * important not to modify the existing DC state. Otherwise, atomic_check
 * may unexpectedly commit hardware changes.
 *
 * When validating the DC state, it's important that the right locks are
 * acquired. For full updates case which removes/adds/updates streams on one
 * CRTC while flipping on another CRTC, acquiring global lock will guarantee
 * that any such full update commit will wait for completion of any outstanding
 * flip using DRMs synchronization events.
 *
 * Note that DM adds the affected connectors for all CRTCs in state, when that
 * might not seem necessary. This is because DC stream creation requires the
 * DC sink, which is tied to the DRM connector state. Cleaning this up should
 * be possible but non-trivial - a possible TODO item.
 *
 * Return: -Error code if validation failed.
 */
static int amdgpu_dm_atomic_check(struct drm_device *dev,
				  struct drm_atomic_state *state)
{
	struct amdgpu_device *adev = drm_to_adev(dev);
	struct dm_atomic_state *dm_state = NULL;
	struct dc *dc = adev->dm.dc;
	struct drm_connector *connector;
	struct drm_connector_state *old_con_state, *new_con_state;
	struct drm_crtc *crtc;
	struct drm_crtc_state *old_crtc_state, *new_crtc_state;
	struct drm_plane *plane;
	struct drm_plane_state *old_plane_state, *new_plane_state, *new_cursor_state;
	enum dc_status status;
	int ret, i;
	bool lock_and_validation_needed = false;
	bool is_top_most_overlay = true;
	struct dm_crtc_state *dm_old_crtc_state, *dm_new_crtc_state;
	struct drm_dp_mst_topology_mgr *mgr;
	struct drm_dp_mst_topology_state *mst_state;
	struct dsc_mst_fairness_vars vars[MAX_PIPES] = {0};

	trace_amdgpu_dm_atomic_check_begin(state);

	ret = drm_atomic_helper_check_modeset(dev, state);
	if (ret) {
		drm_dbg_atomic(dev, "drm_atomic_helper_check_modeset() failed\n");
		goto fail;
	}

	/* Check connector changes */
	for_each_oldnew_connector_in_state(state, connector, old_con_state, new_con_state, i) {
		struct dm_connector_state *dm_old_con_state = to_dm_connector_state(old_con_state);
		struct dm_connector_state *dm_new_con_state = to_dm_connector_state(new_con_state);

		/* Skip connectors that are disabled or part of modeset already. */
		if (!new_con_state->crtc)
			continue;

		new_crtc_state = drm_atomic_get_crtc_state(state, new_con_state->crtc);
		if (IS_ERR(new_crtc_state)) {
			drm_dbg_atomic(dev, "drm_atomic_get_crtc_state() failed\n");
			ret = PTR_ERR(new_crtc_state);
			goto fail;
		}

		if (dm_old_con_state->abm_level != dm_new_con_state->abm_level ||
		    dm_old_con_state->scaling != dm_new_con_state->scaling)
			new_crtc_state->connectors_changed = true;
	}

	if (dc_resource_is_dsc_encoding_supported(dc)) {
		for_each_oldnew_crtc_in_state(state, crtc, old_crtc_state, new_crtc_state, i) {
			if (drm_atomic_crtc_needs_modeset(new_crtc_state)) {
				ret = add_affected_mst_dsc_crtcs(state, crtc);
				if (ret) {
					drm_dbg_atomic(dev, "add_affected_mst_dsc_crtcs() failed\n");
					goto fail;
				}
			}
		}
	}
	for_each_oldnew_crtc_in_state(state, crtc, old_crtc_state, new_crtc_state, i) {
		dm_old_crtc_state = to_dm_crtc_state(old_crtc_state);

		if (!drm_atomic_crtc_needs_modeset(new_crtc_state) &&
		    !new_crtc_state->color_mgmt_changed &&
		    old_crtc_state->vrr_enabled == new_crtc_state->vrr_enabled &&
			dm_old_crtc_state->dsc_force_changed == false)
			continue;

		ret = amdgpu_dm_verify_lut_sizes(new_crtc_state);
		if (ret) {
			drm_dbg_atomic(dev, "amdgpu_dm_verify_lut_sizes() failed\n");
			goto fail;
		}

		if (!new_crtc_state->enable)
			continue;

		ret = drm_atomic_add_affected_connectors(state, crtc);
		if (ret) {
			drm_dbg_atomic(dev, "drm_atomic_add_affected_connectors() failed\n");
			goto fail;
		}

		ret = drm_atomic_add_affected_planes(state, crtc);
		if (ret) {
			drm_dbg_atomic(dev, "drm_atomic_add_affected_planes() failed\n");
			goto fail;
		}

		if (dm_old_crtc_state->dsc_force_changed)
			new_crtc_state->mode_changed = true;
	}

	/*
	 * Add all primary and overlay planes on the CRTC to the state
	 * whenever a plane is enabled to maintain correct z-ordering
	 * and to enable fast surface updates.
	 */
	drm_for_each_crtc(crtc, dev) {
		bool modified = false;

		for_each_oldnew_plane_in_state(state, plane, old_plane_state, new_plane_state, i) {
			if (plane->type == DRM_PLANE_TYPE_CURSOR)
				continue;

			if (new_plane_state->crtc == crtc ||
			    old_plane_state->crtc == crtc) {
				modified = true;
				break;
			}
		}

		if (!modified)
			continue;

		drm_for_each_plane_mask(plane, state->dev, crtc->state->plane_mask) {
			if (plane->type == DRM_PLANE_TYPE_CURSOR)
				continue;

			new_plane_state =
				drm_atomic_get_plane_state(state, plane);

			if (IS_ERR(new_plane_state)) {
				ret = PTR_ERR(new_plane_state);
				drm_dbg_atomic(dev, "new_plane_state is BAD\n");
				goto fail;
			}
		}
	}

	/*
	 * DC consults the zpos (layer_index in DC terminology) to determine the
	 * hw plane on which to enable the hw cursor (see
	 * `dcn10_can_pipe_disable_cursor`). By now, all modified planes are in
	 * atomic state, so call drm helper to normalize zpos.
	 */
	ret = drm_atomic_normalize_zpos(dev, state);
	if (ret) {
		drm_dbg(dev, "drm_atomic_normalize_zpos() failed\n");
		goto fail;
	}

	/*
	 * Determine whether cursors on each CRTC should be enabled in native or
	 * overlay mode.
	 */
	for_each_new_crtc_in_state(state, crtc, new_crtc_state, i) {
		dm_new_crtc_state = to_dm_crtc_state(new_crtc_state);

		ret = dm_crtc_get_cursor_mode(adev, state, dm_new_crtc_state,
					      &dm_new_crtc_state->cursor_mode);
		if (ret) {
			drm_dbg(dev, "Failed to determine cursor mode\n");
			goto fail;
		}

		/*
		 * If overlay cursor is needed, DC cannot go through the
		 * native cursor update path. All enabled planes on the CRTC
		 * need to be added for DC to not disable a plane by mistake
		 */
		if (dm_new_crtc_state->cursor_mode == DM_CURSOR_OVERLAY_MODE) {
			ret = drm_atomic_add_affected_planes(state, crtc);
			if (ret)
				goto fail;
		}
	}

	/* Remove exiting planes if they are modified */
	for_each_oldnew_plane_in_descending_zpos(state, plane, old_plane_state, new_plane_state) {

		ret = dm_update_plane_state(dc, state, plane,
					    old_plane_state,
					    new_plane_state,
					    false,
					    &lock_and_validation_needed,
					    &is_top_most_overlay);
		if (ret) {
			drm_dbg_atomic(dev, "dm_update_plane_state() failed\n");
			goto fail;
		}
	}

	/* Disable all crtcs which require disable */
	for_each_oldnew_crtc_in_state(state, crtc, old_crtc_state, new_crtc_state, i) {
		ret = dm_update_crtc_state(&adev->dm, state, crtc,
					   old_crtc_state,
					   new_crtc_state,
					   false,
					   &lock_and_validation_needed);
		if (ret) {
			drm_dbg_atomic(dev, "DISABLE: dm_update_crtc_state() failed\n");
			goto fail;
		}
	}

	/* Enable all crtcs which require enable */
	for_each_oldnew_crtc_in_state(state, crtc, old_crtc_state, new_crtc_state, i) {
		ret = dm_update_crtc_state(&adev->dm, state, crtc,
					   old_crtc_state,
					   new_crtc_state,
					   true,
					   &lock_and_validation_needed);
		if (ret) {
			drm_dbg_atomic(dev, "ENABLE: dm_update_crtc_state() failed\n");
			goto fail;
		}
	}

	/* Add new/modified planes */
	for_each_oldnew_plane_in_descending_zpos(state, plane, old_plane_state, new_plane_state) {
		ret = dm_update_plane_state(dc, state, plane,
					    old_plane_state,
					    new_plane_state,
					    true,
					    &lock_and_validation_needed,
					    &is_top_most_overlay);
		if (ret) {
			drm_dbg_atomic(dev, "dm_update_plane_state() failed\n");
			goto fail;
		}
	}

#if defined(CONFIG_DRM_AMD_DC_FP)
	if (dc_resource_is_dsc_encoding_supported(dc)) {
		ret = pre_validate_dsc(state, &dm_state, vars);
		if (ret != 0)
			goto fail;
	}
#endif

	/* Run this here since we want to validate the streams we created */
	ret = drm_atomic_helper_check_planes(dev, state);
	if (ret) {
		drm_dbg_atomic(dev, "drm_atomic_helper_check_planes() failed\n");
		goto fail;
	}

	for_each_new_crtc_in_state(state, crtc, new_crtc_state, i) {
		dm_new_crtc_state = to_dm_crtc_state(new_crtc_state);
		if (dm_new_crtc_state->mpo_requested)
			drm_dbg_atomic(dev, "MPO enablement requested on crtc:[%p]\n", crtc);
	}

	/* Check cursor restrictions */
	for_each_new_crtc_in_state(state, crtc, new_crtc_state, i) {
		enum amdgpu_dm_cursor_mode required_cursor_mode;
		int is_rotated, is_scaled;

		/* Overlay cusor not subject to native cursor restrictions */
		dm_new_crtc_state = to_dm_crtc_state(new_crtc_state);
		if (dm_new_crtc_state->cursor_mode == DM_CURSOR_OVERLAY_MODE)
			continue;

		/* Check if rotation or scaling is enabled on DCN401 */
		if ((drm_plane_mask(crtc->cursor) & new_crtc_state->plane_mask) &&
		    amdgpu_ip_version(adev, DCE_HWIP, 0) == IP_VERSION(4, 0, 1)) {
			new_cursor_state = drm_atomic_get_new_plane_state(state, crtc->cursor);

			is_rotated = new_cursor_state &&
				((new_cursor_state->rotation & DRM_MODE_ROTATE_MASK) != DRM_MODE_ROTATE_0);
			is_scaled = new_cursor_state && ((new_cursor_state->src_w >> 16 != new_cursor_state->crtc_w) ||
				(new_cursor_state->src_h >> 16 != new_cursor_state->crtc_h));

			if (is_rotated || is_scaled) {
				drm_dbg_driver(
					crtc->dev,
					"[CRTC:%d:%s] cannot enable hardware cursor due to rotation/scaling\n",
					crtc->base.id, crtc->name);
				ret = -EINVAL;
				goto fail;
			}
		}

		/* If HW can only do native cursor, check restrictions again */
		ret = dm_crtc_get_cursor_mode(adev, state, dm_new_crtc_state,
					      &required_cursor_mode);
		if (ret) {
			drm_dbg_driver(crtc->dev,
				       "[CRTC:%d:%s] Checking cursor mode failed\n",
				       crtc->base.id, crtc->name);
			goto fail;
		} else if (required_cursor_mode == DM_CURSOR_OVERLAY_MODE) {
			drm_dbg_driver(crtc->dev,
				       "[CRTC:%d:%s] Cannot enable native cursor due to scaling or YUV restrictions\n",
				       crtc->base.id, crtc->name);
			ret = -EINVAL;
			goto fail;
		}
	}

	if (state->legacy_cursor_update) {
		/*
		 * This is a fast cursor update coming from the plane update
		 * helper, check if it can be done asynchronously for better
		 * performance.
		 */
		state->async_update =
			!drm_atomic_helper_async_check(dev, state);

		/*
		 * Skip the remaining global validation if this is an async
		 * update. Cursor updates can be done without affecting
		 * state or bandwidth calcs and this avoids the performance
		 * penalty of locking the private state object and
		 * allocating a new dc_state.
		 */
		if (state->async_update)
			return 0;
	}

	/* Check scaling and underscan changes*/
	/* TODO Removed scaling changes validation due to inability to commit
	 * new stream into context w\o causing full reset. Need to
	 * decide how to handle.
	 */
	for_each_oldnew_connector_in_state(state, connector, old_con_state, new_con_state, i) {
		struct dm_connector_state *dm_old_con_state = to_dm_connector_state(old_con_state);
		struct dm_connector_state *dm_new_con_state = to_dm_connector_state(new_con_state);
		struct amdgpu_crtc *acrtc = to_amdgpu_crtc(dm_new_con_state->base.crtc);

		/* Skip any modesets/resets */
		if (!acrtc || drm_atomic_crtc_needs_modeset(
				drm_atomic_get_new_crtc_state(state, &acrtc->base)))
			continue;

		/* Skip any thing not scale or underscan changes */
		if (!is_scaling_state_different(dm_new_con_state, dm_old_con_state))
			continue;

		lock_and_validation_needed = true;
	}

	/* set the slot info for each mst_state based on the link encoding format */
	for_each_new_mst_mgr_in_state(state, mgr, mst_state, i) {
		struct amdgpu_dm_connector *aconnector;
		struct drm_connector *connector;
		struct drm_connector_list_iter iter;
		u8 link_coding_cap;

		drm_connector_list_iter_begin(dev, &iter);
		drm_for_each_connector_iter(connector, &iter) {
			if (connector->index == mst_state->mgr->conn_base_id) {
				aconnector = to_amdgpu_dm_connector(connector);
				link_coding_cap = dc_link_dp_mst_decide_link_encoding_format(aconnector->dc_link);
				drm_dp_mst_update_slots(mst_state, link_coding_cap);

				break;
			}
		}
		drm_connector_list_iter_end(&iter);
	}

	/**
	 * Streams and planes are reset when there are changes that affect
	 * bandwidth. Anything that affects bandwidth needs to go through
	 * DC global validation to ensure that the configuration can be applied
	 * to hardware.
	 *
	 * We have to currently stall out here in atomic_check for outstanding
	 * commits to finish in this case because our IRQ handlers reference
	 * DRM state directly - we can end up disabling interrupts too early
	 * if we don't.
	 *
	 * TODO: Remove this stall and drop DM state private objects.
	 */
	if (lock_and_validation_needed) {
		ret = dm_atomic_get_state(state, &dm_state);
		if (ret) {
			drm_dbg_atomic(dev, "dm_atomic_get_state() failed\n");
			goto fail;
		}

		ret = do_aquire_global_lock(dev, state);
		if (ret) {
			drm_dbg_atomic(dev, "do_aquire_global_lock() failed\n");
			goto fail;
		}

#if defined(CONFIG_DRM_AMD_DC_FP)
		if (dc_resource_is_dsc_encoding_supported(dc)) {
			ret = compute_mst_dsc_configs_for_state(state, dm_state->context, vars);
			if (ret) {
				drm_dbg_atomic(dev, "MST_DSC compute_mst_dsc_configs_for_state() failed\n");
				ret = -EINVAL;
				goto fail;
			}
		}
#endif

		ret = dm_update_mst_vcpi_slots_for_dsc(state, dm_state->context, vars);
		if (ret) {
			drm_dbg_atomic(dev, "dm_update_mst_vcpi_slots_for_dsc() failed\n");
			goto fail;
		}

		/*
		 * Perform validation of MST topology in the state:
		 * We need to perform MST atomic check before calling
		 * dc_validate_global_state(), or there is a chance
		 * to get stuck in an infinite loop and hang eventually.
		 */
		ret = drm_dp_mst_atomic_check(state);
		if (ret) {
			drm_dbg_atomic(dev, "MST drm_dp_mst_atomic_check() failed\n");
			goto fail;
		}
		status = dc_validate_global_state(dc, dm_state->context, true);
		if (status != DC_OK) {
			drm_dbg_atomic(dev, "DC global validation failure: %s (%d)",
				       dc_status_to_str(status), status);
			ret = -EINVAL;
			goto fail;
		}
	} else {
		/*
		 * The commit is a fast update. Fast updates shouldn't change
		 * the DC context, affect global validation, and can have their
		 * commit work done in parallel with other commits not touching
		 * the same resource. If we have a new DC context as part of
		 * the DM atomic state from validation we need to free it and
		 * retain the existing one instead.
		 *
		 * Furthermore, since the DM atomic state only contains the DC
		 * context and can safely be annulled, we can free the state
		 * and clear the associated private object now to free
		 * some memory and avoid a possible use-after-free later.
		 */

		for (i = 0; i < state->num_private_objs; i++) {
			struct drm_private_obj *obj = state->private_objs[i].ptr;

			if (obj->funcs == adev->dm.atomic_obj.funcs) {
				int j = state->num_private_objs-1;

				dm_atomic_destroy_state(obj,
						state->private_objs[i].state);

				/* If i is not at the end of the array then the
				 * last element needs to be moved to where i was
				 * before the array can safely be truncated.
				 */
				if (i != j)
					state->private_objs[i] =
						state->private_objs[j];

				state->private_objs[j].ptr = NULL;
				state->private_objs[j].state = NULL;
				state->private_objs[j].old_state = NULL;
				state->private_objs[j].new_state = NULL;

				state->num_private_objs = j;
				break;
			}
		}
	}

	/* Store the overall update type for use later in atomic check. */
	for_each_new_crtc_in_state(state, crtc, new_crtc_state, i) {
		struct dm_crtc_state *dm_new_crtc_state =
			to_dm_crtc_state(new_crtc_state);

		/*
		 * Only allow async flips for fast updates that don't change
		 * the FB pitch, the DCC state, rotation, mem_type, etc.
		 */
		if (new_crtc_state->async_flip &&
		    (lock_and_validation_needed ||
		     amdgpu_dm_crtc_mem_type_changed(dev, state, new_crtc_state))) {
			drm_dbg_atomic(crtc->dev,
				       "[CRTC:%d:%s] async flips are only supported for fast updates\n",
				       crtc->base.id, crtc->name);
			ret = -EINVAL;
			goto fail;
		}

		dm_new_crtc_state->update_type = lock_and_validation_needed ?
			UPDATE_TYPE_FULL : UPDATE_TYPE_FAST;
	}

	/* Must be success */
	WARN_ON(ret);

	trace_amdgpu_dm_atomic_check_finish(state, ret);

	return ret;

fail:
	if (ret == -EDEADLK)
		drm_dbg_atomic(dev, "Atomic check stopped to avoid deadlock.\n");
	else if (ret == -EINTR || ret == -EAGAIN || ret == -ERESTARTSYS)
		drm_dbg_atomic(dev, "Atomic check stopped due to signal.\n");
	else
		drm_dbg_atomic(dev, "Atomic check failed with err: %d\n", ret);

	trace_amdgpu_dm_atomic_check_finish(state, ret);

	return ret;
}

static bool dm_edid_parser_send_cea(struct amdgpu_display_manager *dm,
		unsigned int offset,
		unsigned int total_length,
		u8 *data,
		unsigned int length,
		struct amdgpu_hdmi_vsdb_info *vsdb)
{
	bool res;
	union dmub_rb_cmd cmd;
	struct dmub_cmd_send_edid_cea *input;
	struct dmub_cmd_edid_cea_output *output;

	if (length > DMUB_EDID_CEA_DATA_CHUNK_BYTES)
		return false;

	memset(&cmd, 0, sizeof(cmd));

	input = &cmd.edid_cea.data.input;

	cmd.edid_cea.header.type = DMUB_CMD__EDID_CEA;
	cmd.edid_cea.header.sub_type = 0;
	cmd.edid_cea.header.payload_bytes =
		sizeof(cmd.edid_cea) - sizeof(cmd.edid_cea.header);
	input->offset = offset;
	input->length = length;
	input->cea_total_length = total_length;
	memcpy(input->payload, data, length);

	res = dc_wake_and_execute_dmub_cmd(dm->dc->ctx, &cmd, DM_DMUB_WAIT_TYPE_WAIT_WITH_REPLY);
	if (!res) {
		DRM_ERROR("EDID CEA parser failed\n");
		return false;
	}

	output = &cmd.edid_cea.data.output;

	if (output->type == DMUB_CMD__EDID_CEA_ACK) {
		if (!output->ack.success) {
			DRM_ERROR("EDID CEA ack failed at offset %d\n",
					output->ack.offset);
		}
	} else if (output->type == DMUB_CMD__EDID_CEA_AMD_VSDB) {
		if (!output->amd_vsdb.vsdb_found)
			return false;

		vsdb->freesync_supported = output->amd_vsdb.freesync_supported;
		vsdb->amd_vsdb_version = output->amd_vsdb.amd_vsdb_version;
		vsdb->min_refresh_rate_hz = output->amd_vsdb.min_frame_rate;
		vsdb->max_refresh_rate_hz = output->amd_vsdb.max_frame_rate;
	} else {
		if (output->type != 0)
			DRM_WARN("Unknown EDID CEA parser results\n");
		return false;
	}

	return true;
}

static bool parse_edid_cea_dmcu(struct amdgpu_display_manager *dm,
		u8 *edid_ext, int len,
		struct amdgpu_hdmi_vsdb_info *vsdb_info)
{
	int i;

	/* send extension block to DMCU for parsing */
	for (i = 0; i < len; i += 8) {
		bool res;
		int offset;

		/* send 8 bytes a time */
		if (!dc_edid_parser_send_cea(dm->dc, i, len, &edid_ext[i], 8))
			return false;

		if (i+8 == len) {
			/* EDID block sent completed, expect result */
			int version, min_rate, max_rate;

			res = dc_edid_parser_recv_amd_vsdb(dm->dc, &version, &min_rate, &max_rate);
			if (res) {
				/* amd vsdb found */
				vsdb_info->freesync_supported = 1;
				vsdb_info->amd_vsdb_version = version;
				vsdb_info->min_refresh_rate_hz = min_rate;
				vsdb_info->max_refresh_rate_hz = max_rate;
				return true;
			}
			/* not amd vsdb */
			return false;
		}

		/* check for ack*/
		res = dc_edid_parser_recv_cea_ack(dm->dc, &offset);
		if (!res)
			return false;
	}

	return false;
}

static bool parse_edid_cea_dmub(struct amdgpu_display_manager *dm,
		u8 *edid_ext, int len,
		struct amdgpu_hdmi_vsdb_info *vsdb_info)
{
	int i;

	/* send extension block to DMCU for parsing */
	for (i = 0; i < len; i += 8) {
		/* send 8 bytes a time */
		if (!dm_edid_parser_send_cea(dm, i, len, &edid_ext[i], 8, vsdb_info))
			return false;
	}

	return vsdb_info->freesync_supported;
}

static bool parse_edid_cea(struct amdgpu_dm_connector *aconnector,
		u8 *edid_ext, int len,
		struct amdgpu_hdmi_vsdb_info *vsdb_info)
{
	struct amdgpu_device *adev = drm_to_adev(aconnector->base.dev);
	bool ret;

	mutex_lock(&adev->dm.dc_lock);
	if (adev->dm.dmub_srv)
		ret = parse_edid_cea_dmub(&adev->dm, edid_ext, len, vsdb_info);
	else
		ret = parse_edid_cea_dmcu(&adev->dm, edid_ext, len, vsdb_info);
	mutex_unlock(&adev->dm.dc_lock);
	return ret;
}

static void parse_edid_displayid_vrr(struct drm_connector *connector,
		struct edid *edid)
{
	u8 *edid_ext = NULL;
	int i;
	int j = 0;
	u16 min_vfreq;
	u16 max_vfreq;

	if (edid == NULL || edid->extensions == 0)
		return;

	/* Find DisplayID extension */
	for (i = 0; i < edid->extensions; i++) {
		edid_ext = (void *)(edid + (i + 1));
		if (edid_ext[0] == DISPLAYID_EXT)
			break;
	}

	if (edid_ext == NULL)
		return;

	while (j < EDID_LENGTH) {
		/* Get dynamic video timing range from DisplayID if available */
		if (EDID_LENGTH - j > 13 && edid_ext[j] == 0x25	&&
		    (edid_ext[j+1] & 0xFE) == 0 && (edid_ext[j+2] == 9)) {
			min_vfreq = edid_ext[j+9];
			if (edid_ext[j+1] & 7)
				max_vfreq = edid_ext[j+10] + ((edid_ext[j+11] & 3) << 8);
			else
				max_vfreq = edid_ext[j+10];

			if (max_vfreq && min_vfreq) {
				connector->display_info.monitor_range.max_vfreq = max_vfreq;
				connector->display_info.monitor_range.min_vfreq = min_vfreq;

				return;
			}
		}
		j++;
	}
}

static int parse_amd_vsdb(struct amdgpu_dm_connector *aconnector,
			  struct edid *edid, struct amdgpu_hdmi_vsdb_info *vsdb_info)
{
	u8 *edid_ext = NULL;
	int i;
	int j = 0;

	if (edid == NULL || edid->extensions == 0)
		return -ENODEV;

	/* Find DisplayID extension */
	for (i = 0; i < edid->extensions; i++) {
		edid_ext = (void *)(edid + (i + 1));
		if (edid_ext[0] == DISPLAYID_EXT)
			break;
	}

	while (j < EDID_LENGTH - sizeof(struct amd_vsdb_block)) {
		struct amd_vsdb_block *amd_vsdb = (struct amd_vsdb_block *)&edid_ext[j];
		unsigned int ieeeId = (amd_vsdb->ieee_id[2] << 16) | (amd_vsdb->ieee_id[1] << 8) | (amd_vsdb->ieee_id[0]);

		if (ieeeId == HDMI_AMD_VENDOR_SPECIFIC_DATA_BLOCK_IEEE_REGISTRATION_ID &&
				amd_vsdb->version == HDMI_AMD_VENDOR_SPECIFIC_DATA_BLOCK_VERSION_3) {
			vsdb_info->replay_mode = (amd_vsdb->feature_caps & AMD_VSDB_VERSION_3_FEATURECAP_REPLAYMODE) ? true : false;
			vsdb_info->amd_vsdb_version = HDMI_AMD_VENDOR_SPECIFIC_DATA_BLOCK_VERSION_3;
			DRM_DEBUG_KMS("Panel supports Replay Mode: %d\n", vsdb_info->replay_mode);

			return true;
		}
		j++;
	}

	return false;
}

static int parse_hdmi_amd_vsdb(struct amdgpu_dm_connector *aconnector,
		struct edid *edid, struct amdgpu_hdmi_vsdb_info *vsdb_info)
{
	u8 *edid_ext = NULL;
	int i;
	bool valid_vsdb_found = false;

	/*----- drm_find_cea_extension() -----*/
	/* No EDID or EDID extensions */
	if (edid == NULL || edid->extensions == 0)
		return -ENODEV;

	/* Find CEA extension */
	for (i = 0; i < edid->extensions; i++) {
		edid_ext = (uint8_t *)edid + EDID_LENGTH * (i + 1);
		if (edid_ext[0] == CEA_EXT)
			break;
	}

	if (i == edid->extensions)
		return -ENODEV;

	/*----- cea_db_offsets() -----*/
	if (edid_ext[0] != CEA_EXT)
		return -ENODEV;

	valid_vsdb_found = parse_edid_cea(aconnector, edid_ext, EDID_LENGTH, vsdb_info);

	return valid_vsdb_found ? i : -ENODEV;
}

/**
 * amdgpu_dm_update_freesync_caps - Update Freesync capabilities
 *
 * @connector: Connector to query.
 * @edid: EDID from monitor
 *
 * Amdgpu supports Freesync in DP and HDMI displays, and it is required to keep
 * track of some of the display information in the internal data struct used by
 * amdgpu_dm. This function checks which type of connector we need to set the
 * FreeSync parameters.
 */
void amdgpu_dm_update_freesync_caps(struct drm_connector *connector,
				    struct edid *edid)
{
	int i = 0;
	struct detailed_timing *timing;
	struct detailed_non_pixel *data;
	struct detailed_data_monitor_range *range;
	struct amdgpu_dm_connector *amdgpu_dm_connector =
			to_amdgpu_dm_connector(connector);
	struct dm_connector_state *dm_con_state = NULL;
	struct dc_sink *sink;

	struct amdgpu_device *adev = drm_to_adev(connector->dev);
	struct amdgpu_hdmi_vsdb_info vsdb_info = {0};
	bool freesync_capable = false;
	enum adaptive_sync_type as_type = ADAPTIVE_SYNC_TYPE_NONE;

	if (!connector->state) {
		DRM_ERROR("%s - Connector has no state", __func__);
		goto update;
	}

	sink = amdgpu_dm_connector->dc_sink ?
		amdgpu_dm_connector->dc_sink :
		amdgpu_dm_connector->dc_em_sink;

	if (!edid || !sink) {
		dm_con_state = to_dm_connector_state(connector->state);

		amdgpu_dm_connector->min_vfreq = 0;
		amdgpu_dm_connector->max_vfreq = 0;
		connector->display_info.monitor_range.min_vfreq = 0;
		connector->display_info.monitor_range.max_vfreq = 0;
		freesync_capable = false;

		goto update;
	}

	dm_con_state = to_dm_connector_state(connector->state);

	if (!adev->dm.freesync_module)
		goto update;

	/* Some eDP panels only have the refresh rate range info in DisplayID */
	if ((connector->display_info.monitor_range.min_vfreq == 0 ||
	     connector->display_info.monitor_range.max_vfreq == 0))
		parse_edid_displayid_vrr(connector, edid);

	if (edid && (sink->sink_signal == SIGNAL_TYPE_DISPLAY_PORT ||
		     sink->sink_signal == SIGNAL_TYPE_EDP)) {
		bool edid_check_required = false;

		if (amdgpu_dm_connector->dc_link &&
		    amdgpu_dm_connector->dc_link->dpcd_caps.allow_invalid_MSA_timing_param) {
			if (edid->features & DRM_EDID_FEATURE_CONTINUOUS_FREQ) {
				amdgpu_dm_connector->min_vfreq = connector->display_info.monitor_range.min_vfreq;
				amdgpu_dm_connector->max_vfreq = connector->display_info.monitor_range.max_vfreq;
				if (amdgpu_dm_connector->max_vfreq -
				    amdgpu_dm_connector->min_vfreq > 10)
					freesync_capable = true;
			} else {
				edid_check_required = edid->version > 1 ||
						      (edid->version == 1 &&
						       edid->revision > 1);
			}
		}

		if (edid_check_required) {
			for (i = 0; i < 4; i++) {

				timing	= &edid->detailed_timings[i];
				data	= &timing->data.other_data;
				range	= &data->data.range;
				/*
				 * Check if monitor has continuous frequency mode
				 */
				if (data->type != EDID_DETAIL_MONITOR_RANGE)
					continue;
				/*
				 * Check for flag range limits only. If flag == 1 then
				 * no additional timing information provided.
				 * Default GTF, GTF Secondary curve and CVT are not
				 * supported
				 */
				if (range->flags != 1)
					continue;

				connector->display_info.monitor_range.min_vfreq = range->min_vfreq;
				connector->display_info.monitor_range.max_vfreq = range->max_vfreq;

				if (edid->revision >= 4) {
					if (data->pad2 & DRM_EDID_RANGE_OFFSET_MIN_VFREQ)
						connector->display_info.monitor_range.min_vfreq += 255;
					if (data->pad2 & DRM_EDID_RANGE_OFFSET_MAX_VFREQ)
						connector->display_info.monitor_range.max_vfreq += 255;
				}

				amdgpu_dm_connector->min_vfreq =
					connector->display_info.monitor_range.min_vfreq;
				amdgpu_dm_connector->max_vfreq =
					connector->display_info.monitor_range.max_vfreq;

				break;
			}

			if (amdgpu_dm_connector->max_vfreq -
			    amdgpu_dm_connector->min_vfreq > 10) {

				freesync_capable = true;
			}
		}
		parse_amd_vsdb(amdgpu_dm_connector, edid, &vsdb_info);

		if (vsdb_info.replay_mode) {
			amdgpu_dm_connector->vsdb_info.replay_mode = vsdb_info.replay_mode;
			amdgpu_dm_connector->vsdb_info.amd_vsdb_version = vsdb_info.amd_vsdb_version;
			amdgpu_dm_connector->as_type = ADAPTIVE_SYNC_TYPE_EDP;
		}

	} else if (edid && sink->sink_signal == SIGNAL_TYPE_HDMI_TYPE_A) {
		i = parse_hdmi_amd_vsdb(amdgpu_dm_connector, edid, &vsdb_info);
		if (i >= 0 && vsdb_info.freesync_supported) {
			timing  = &edid->detailed_timings[i];
			data    = &timing->data.other_data;

			amdgpu_dm_connector->min_vfreq = vsdb_info.min_refresh_rate_hz;
			amdgpu_dm_connector->max_vfreq = vsdb_info.max_refresh_rate_hz;
			if (amdgpu_dm_connector->max_vfreq - amdgpu_dm_connector->min_vfreq > 10)
				freesync_capable = true;

			connector->display_info.monitor_range.min_vfreq = vsdb_info.min_refresh_rate_hz;
			connector->display_info.monitor_range.max_vfreq = vsdb_info.max_refresh_rate_hz;
		}
	}

	if (amdgpu_dm_connector->dc_link)
		as_type = dm_get_adaptive_sync_support_type(amdgpu_dm_connector->dc_link);

	if (as_type == FREESYNC_TYPE_PCON_IN_WHITELIST) {
		i = parse_hdmi_amd_vsdb(amdgpu_dm_connector, edid, &vsdb_info);
		if (i >= 0 && vsdb_info.freesync_supported && vsdb_info.amd_vsdb_version > 0) {

			amdgpu_dm_connector->pack_sdp_v1_3 = true;
			amdgpu_dm_connector->as_type = as_type;
			amdgpu_dm_connector->vsdb_info = vsdb_info;

			amdgpu_dm_connector->min_vfreq = vsdb_info.min_refresh_rate_hz;
			amdgpu_dm_connector->max_vfreq = vsdb_info.max_refresh_rate_hz;
			if (amdgpu_dm_connector->max_vfreq - amdgpu_dm_connector->min_vfreq > 10)
				freesync_capable = true;

			connector->display_info.monitor_range.min_vfreq = vsdb_info.min_refresh_rate_hz;
			connector->display_info.monitor_range.max_vfreq = vsdb_info.max_refresh_rate_hz;
		}
	}

update:
	if (dm_con_state)
		dm_con_state->freesync_capable = freesync_capable;

	if (connector->state && amdgpu_dm_connector->dc_link && !freesync_capable &&
	    amdgpu_dm_connector->dc_link->replay_settings.config.replay_supported) {
		amdgpu_dm_connector->dc_link->replay_settings.config.replay_supported = false;
		amdgpu_dm_connector->dc_link->replay_settings.replay_feature_enabled = false;
	}

	if (connector->vrr_capable_property)
		drm_connector_set_vrr_capable_property(connector,
						       freesync_capable);
}

void amdgpu_dm_trigger_timing_sync(struct drm_device *dev)
{
	struct amdgpu_device *adev = drm_to_adev(dev);
	struct dc *dc = adev->dm.dc;
	int i;

	mutex_lock(&adev->dm.dc_lock);
	if (dc->current_state) {
		for (i = 0; i < dc->current_state->stream_count; ++i)
			dc->current_state->streams[i]
				->triggered_crtc_reset.enabled =
				adev->dm.force_timing_sync;

		dm_enable_per_frame_crtc_master_sync(dc->current_state);
		dc_trigger_sync(dc, dc->current_state);
	}
	mutex_unlock(&adev->dm.dc_lock);
}

static inline void amdgpu_dm_exit_ips_for_hw_access(struct dc *dc)
{
	if (dc->ctx->dmub_srv && !dc->ctx->dmub_srv->idle_exit_counter)
		dc_exit_ips_for_hw_access(dc);
}

void dm_write_reg_func(const struct dc_context *ctx, uint32_t address,
		       u32 value, const char *func_name)
{
#ifdef DM_CHECK_ADDR_0
	if (address == 0) {
		drm_err(adev_to_drm(ctx->driver_context),
			"invalid register write. address = 0");
		return;
	}
#endif

	amdgpu_dm_exit_ips_for_hw_access(ctx->dc);
	cgs_write_register(ctx->cgs_device, address, value);
	trace_amdgpu_dc_wreg(&ctx->perf_trace->write_count, address, value);
}

uint32_t dm_read_reg_func(const struct dc_context *ctx, uint32_t address,
			  const char *func_name)
{
	u32 value;
#ifdef DM_CHECK_ADDR_0
	if (address == 0) {
		drm_err(adev_to_drm(ctx->driver_context),
			"invalid register read; address = 0\n");
		return 0;
	}
#endif

	if (ctx->dmub_srv &&
	    ctx->dmub_srv->reg_helper_offload.gather_in_progress &&
	    !ctx->dmub_srv->reg_helper_offload.should_burst_write) {
		ASSERT(false);
		return 0;
	}

	amdgpu_dm_exit_ips_for_hw_access(ctx->dc);

	value = cgs_read_register(ctx->cgs_device, address);

	trace_amdgpu_dc_rreg(&ctx->perf_trace->read_count, address, value);

	return value;
}

int amdgpu_dm_process_dmub_aux_transfer_sync(
		struct dc_context *ctx,
		unsigned int link_index,
		struct aux_payload *payload,
		enum aux_return_code_type *operation_result)
{
	struct amdgpu_device *adev = ctx->driver_context;
	struct dmub_notification *p_notify = adev->dm.dmub_notify;
	int ret = -1;

	mutex_lock(&adev->dm.dpia_aux_lock);
	if (!dc_process_dmub_aux_transfer_async(ctx->dc, link_index, payload)) {
		*operation_result = AUX_RET_ERROR_ENGINE_ACQUIRE;
		goto out;
	}

	if (!wait_for_completion_timeout(&adev->dm.dmub_aux_transfer_done, 10 * HZ)) {
		DRM_ERROR("wait_for_completion_timeout timeout!");
		*operation_result = AUX_RET_ERROR_TIMEOUT;
		goto out;
	}

	if (p_notify->result != AUX_RET_SUCCESS) {
		/*
		 * Transient states before tunneling is enabled could
		 * lead to this error. We can ignore this for now.
		 */
		if (p_notify->result == AUX_RET_ERROR_PROTOCOL_ERROR) {
			DRM_WARN("DPIA AUX failed on 0x%x(%d), error %d\n",
					payload->address, payload->length,
					p_notify->result);
		}
		*operation_result = AUX_RET_ERROR_INVALID_REPLY;
		goto out;
	}

	payload->reply[0] = adev->dm.dmub_notify->aux_reply.command & 0xF;
	if (adev->dm.dmub_notify->aux_reply.command & 0xF0)
		/* The reply is stored in the top nibble of the command. */
		payload->reply[0] = (adev->dm.dmub_notify->aux_reply.command >> 4) & 0xF;

	/*write req may receive a byte indicating partially written number as well*/
	if (p_notify->aux_reply.length)
		memcpy(payload->data, p_notify->aux_reply.data,
				p_notify->aux_reply.length);

	/* success */
	ret = p_notify->aux_reply.length;
	*operation_result = p_notify->result;
out:
	reinit_completion(&adev->dm.dmub_aux_transfer_done);
	mutex_unlock(&adev->dm.dpia_aux_lock);
	return ret;
}

int amdgpu_dm_process_dmub_set_config_sync(
		struct dc_context *ctx,
		unsigned int link_index,
		struct set_config_cmd_payload *payload,
		enum set_config_status *operation_result)
{
	struct amdgpu_device *adev = ctx->driver_context;
	bool is_cmd_complete;
	int ret;

	mutex_lock(&adev->dm.dpia_aux_lock);
	is_cmd_complete = dc_process_dmub_set_config_async(ctx->dc,
			link_index, payload, adev->dm.dmub_notify);

	if (is_cmd_complete || wait_for_completion_timeout(&adev->dm.dmub_aux_transfer_done, 10 * HZ)) {
		ret = 0;
		*operation_result = adev->dm.dmub_notify->sc_status;
	} else {
		DRM_ERROR("wait_for_completion_timeout timeout!");
		ret = -1;
		*operation_result = SET_CONFIG_UNKNOWN_ERROR;
	}

	if (!is_cmd_complete)
		reinit_completion(&adev->dm.dmub_aux_transfer_done);
	mutex_unlock(&adev->dm.dpia_aux_lock);
	return ret;
}

bool dm_execute_dmub_cmd(const struct dc_context *ctx, union dmub_rb_cmd *cmd, enum dm_dmub_wait_type wait_type)
{
	return dc_dmub_srv_cmd_run(ctx->dmub_srv, cmd, wait_type);
}

bool dm_execute_dmub_cmd_list(const struct dc_context *ctx, unsigned int count, union dmub_rb_cmd *cmd, enum dm_dmub_wait_type wait_type)
{
	return dc_dmub_srv_cmd_run_list(ctx->dmub_srv, count, cmd, wait_type);
}
