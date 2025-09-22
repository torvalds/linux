// SPDX-License-Identifier: GPL-2.0-only OR MIT
/*
 * Copyright 2021 Alyssa Rosenzweig <alyssa@rosenzweig.io>
 * Copyright The Asahi Linux Contributors
 */

#include <linux/align.h>
#include <linux/bitmap.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/iommu.h>
#include <linux/kref.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>

#include <drm/drm_fb_dma_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>

#include "dcp.h"
#include "dcp-internal.h"
#include "iomfb.h"
#include "iomfb_internal.h"
#include "parser.h"
#include "trace.h"
#include "version_utils.h"

/* Register defines used in bandwidth setup structure */
#define REG_DOORBELL_BIT(idx) (2 + (idx))

struct dcp_wait_cookie {
	struct kref refcount;
	struct completion done;
};

static void release_wait_cookie(struct kref *ref)
{
	struct dcp_wait_cookie *cookie;
	cookie = container_of(ref, struct dcp_wait_cookie, refcount);

        kfree(cookie);
}

DCP_THUNK_OUT(iomfb_a131_pmu_service_matched, iomfbep_a131_pmu_service_matched, u32);
DCP_THUNK_OUT(iomfb_a132_backlight_service_matched, iomfbep_a132_backlight_service_matched, u32);
DCP_THUNK_OUT(iomfb_a358_vi_set_temperature_hint, iomfbep_a358_vi_set_temperature_hint, u32);

IOMFB_THUNK_INOUT(set_matrix);
IOMFB_THUNK_INOUT(get_color_remap_mode);
IOMFB_THUNK_INOUT(last_client_close);
IOMFB_THUNK_INOUT(abort_swaps_dcp);

DCP_THUNK_INOUT(dcp_swap_submit, dcpep_swap_submit,
		struct DCP_FW_NAME(dcp_swap_submit_req),
		struct DCP_FW_NAME(dcp_swap_submit_resp));

DCP_THUNK_INOUT(dcp_swap_start, dcpep_swap_start, struct dcp_swap_start_req,
		struct dcp_swap_start_resp);

DCP_THUNK_INOUT(dcp_set_power_state, dcpep_set_power_state,
		struct dcp_set_power_state_req,
		struct dcp_set_power_state_resp);

DCP_THUNK_INOUT(dcp_set_digital_out_mode, dcpep_set_digital_out_mode,
		struct dcp_set_digital_out_mode_req, u32);

DCP_THUNK_INOUT(dcp_set_display_device, dcpep_set_display_device, u32, u32);

DCP_THUNK_OUT(dcp_set_display_refresh_properties,
	      dcpep_set_display_refresh_properties, u32);

#if DCP_FW_VER >= DCP_FW_VERSION(13, 2, 0)
DCP_THUNK_INOUT(dcp_late_init_signal, dcpep_late_init_signal, u32, u32);
#else
DCP_THUNK_OUT(dcp_late_init_signal, dcpep_late_init_signal, u32);
#endif
DCP_THUNK_IN(dcp_flush_supports_power, dcpep_flush_supports_power, u32);
DCP_THUNK_OUT(dcp_create_default_fb, dcpep_create_default_fb, u32);
DCP_THUNK_OUT(dcp_start_signal, dcpep_start_signal, u32);
DCP_THUNK_VOID(dcp_setup_video_limits, dcpep_setup_video_limits);
DCP_THUNK_VOID(dcp_set_create_dfb, dcpep_set_create_dfb);
DCP_THUNK_VOID(dcp_first_client_open, dcpep_first_client_open);

DCP_THUNK_INOUT(dcp_set_parameter_dcp, dcpep_set_parameter_dcp,
		struct dcp_set_parameter_dcp, u32);

DCP_THUNK_INOUT(dcp_enable_disable_video_power_savings,
		dcpep_enable_disable_video_power_savings, u32, int);

DCP_THUNK_OUT(dcp_is_main_display, dcpep_is_main_display, u32);

/* DCP callback handlers */
static void dcpep_cb_nop(struct apple_dcp *dcp)
{
	/* No operation */
}

static u8 dcpep_cb_true(struct apple_dcp *dcp)
{
	return true;
}

static u8 dcpep_cb_false(struct apple_dcp *dcp)
{
	return false;
}

static u32 dcpep_cb_zero(struct apple_dcp *dcp)
{
	return 0;
}

static void dcpep_cb_swap_complete(struct apple_dcp *dcp,
				   struct DCP_FW_NAME(dc_swap_complete_resp) *resp)
{
	trace_iomfb_swap_complete(dcp, resp->swap_id);
	dcp->last_swap_id = resp->swap_id;

	dcp_drm_crtc_vblank(dcp->crtc);
}

/* special */
static void complete_vi_set_temperature_hint(struct apple_dcp *dcp, void *out, void *cookie)
{
	// ack D100 cb_match_pmu_service
	dcp_ack(dcp, DCP_CONTEXT_CB);
}

static bool iomfbep_cb_match_pmu_service(struct apple_dcp *dcp, int tag, void *out, void *in)
{
	trace_iomfb_callback(dcp, tag, __func__);
	iomfb_a358_vi_set_temperature_hint(dcp, false,
					   complete_vi_set_temperature_hint,
					   NULL);

	// return false for deferred ACK
	return false;
}

static void complete_pmu_service_matched(struct apple_dcp *dcp, void *out, void *cookie)
{
	struct dcp_channel *ch = &dcp->ch_cb;
	u8 *succ = ch->output[ch->depth - 1];

	*succ = true;

	// ack D206 cb_match_pmu_service_2
	dcp_ack(dcp, DCP_CONTEXT_CB);
}

static bool iomfbep_cb_match_pmu_service_2(struct apple_dcp *dcp, int tag, void *out, void *in)
{
	trace_iomfb_callback(dcp, tag, __func__);

	iomfb_a131_pmu_service_matched(dcp, false, complete_pmu_service_matched,
				       out);

	// return false for deferred ACK
	return false;
}

static void complete_backlight_service_matched(struct apple_dcp *dcp, void *out, void *cookie)
{
	struct dcp_channel *ch = &dcp->ch_cb;
	u8 *succ = ch->output[ch->depth - 1];

	*succ = true;

	// ack D206 cb_match_backlight_service
	dcp_ack(dcp, DCP_CONTEXT_CB);
}

static bool iomfbep_cb_match_backlight_service(struct apple_dcp *dcp, int tag, void *out, void *in)
{
	trace_iomfb_callback(dcp, tag, __func__);

	if (!dcp_has_panel(dcp)) {
		u8 *succ = out;
		*succ = true;
		return true;
	}

	iomfb_a132_backlight_service_matched(dcp, false, complete_backlight_service_matched, out);

	// return false for deferred ACK
	return false;
}

static void iomfb_cb_pr_publish(struct apple_dcp *dcp, struct iomfb_property *prop)
{
	switch (prop->id) {
	case IOMFB_PROPERTY_NITS:
	{
		if (dcp_has_panel(dcp)) {
			dcp->brightness.nits = prop->value / dcp->brightness.scale;
			/* notify backlight device of the initial brightness */
			if (!dcp->brightness.bl_dev && dcp->brightness.maximum > 0)
				schedule_work(&dcp->bl_register_wq);
			trace_iomfb_brightness(dcp, prop->value);
		}
		break;
	}
	default:
		dev_dbg(dcp->dev, "pr_publish: id: %d = %u\n", prop->id, prop->value);
	}
}

static struct dcp_get_uint_prop_resp
dcpep_cb_get_uint_prop(struct apple_dcp *dcp, struct dcp_get_uint_prop_req *req)
{
	struct dcp_get_uint_prop_resp resp = (struct dcp_get_uint_prop_resp){
	    .value = 0
	};

	if (dcp->panel.has_mini_led &&
	    memcmp(req->obj, "SUMP", sizeof(req->obj)) == 0) { /* "PMUS */
	    if (strncmp(req->key, "Temperature", sizeof(req->key)) == 0) {
		/*
		 * TODO: value from j314c, find out if it is temperature in
		 *       centigrade C and which temperature sensor reports it
		 */
		resp.value = 3029;
		resp.ret = true;
	    }
	}

	return resp;
}

static u8 iomfbep_cb_sr_set_property_int(struct apple_dcp *dcp,
					 struct iomfb_sr_set_property_int_req *req)
{
	if (memcmp(req->obj, "FMOI", sizeof(req->obj)) == 0) { /* "IOMF */
		if (strncmp(req->key, "Brightness_Scale", sizeof(req->key)) == 0) {
			if (!req->value_null)
				dcp->brightness.scale = req->value;
		}
	}

	return 1;
}

static void iomfbep_cb_set_fx_prop(struct apple_dcp *dcp, struct iomfb_set_fx_prop_req *req)
{
    // TODO: trace this, see if there properties which needs to used later
}

/*
 * Callback to map a buffer allocated with allocate_buf for PIODMA usage.
 * PIODMA is separate from the main DCP and uses own IOVA space on a dedicated
 * stream of the display DART, rather than the expected DCP DART.
 */
static struct dcp_map_buf_resp dcpep_cb_map_piodma(struct apple_dcp *dcp,
						   struct dcp_map_buf_req *req)
{
	struct dcp_mem_descriptor *memdesc;
	struct sg_table *map;
	ssize_t ret;

	if (req->buffer >= ARRAY_SIZE(dcp->memdesc))
		goto reject;

	memdesc = &dcp->memdesc[req->buffer];
	map = &memdesc->map;

	if (!map->sgl)
		goto reject;

	/* use the piodma iommu domain to map against the right IOMMU */
	ret = iommu_map_sgtable(dcp->iommu_dom, memdesc->dva, map,
				IOMMU_READ | IOMMU_WRITE);

	/* HACK: expect size to be 16K aligned since the iommu API only maps
	 *       full pages
	 */
	if (ret < 0 || ret != ALIGN(memdesc->size, SZ_16K)) {
		dev_err(dcp->dev, "iommu_map_sgtable() returned %zd instead of expected buffer size of %zu\n", ret, memdesc->size);
		goto reject;
	}

	return (struct dcp_map_buf_resp){ .dva = memdesc->dva };

reject:
	dev_err(dcp->dev, "denying map of invalid buffer %llx for pidoma\n",
		req->buffer);
	return (struct dcp_map_buf_resp){ .ret = EINVAL };
}

static void dcpep_cb_unmap_piodma(struct apple_dcp *dcp,
				  struct dcp_unmap_buf_resp *resp)
{
	struct dcp_mem_descriptor *memdesc;

	if (resp->buffer >= ARRAY_SIZE(dcp->memdesc)) {
		dev_warn(dcp->dev, "unmap request for out of range buffer %llu\n",
			 resp->buffer);
		return;
	}

	memdesc = &dcp->memdesc[resp->buffer];

	if (!memdesc->buf) {
		dev_warn(dcp->dev,
			 "unmap for non-mapped buffer %llu iova:0x%08llx\n",
			 resp->buffer, resp->dva);
		return;
	}

	if (memdesc->dva != resp->dva) {
		dev_warn(dcp->dev, "unmap buffer %llu address mismatch "
			 "memdesc.dva:%llx dva:%llx\n", resp->buffer,
			 memdesc->dva, resp->dva);
		return;
	}

	/* use the piodma iommu domain to unmap from the right IOMMU */
	iommu_unmap(dcp->iommu_dom, memdesc->dva, memdesc->size);
}

/*
 * Allocate an IOVA contiguous buffer mapped to the DCP. The buffer need not be
 * physically contiguous, however we should save the sgtable in case the
 * buffer needs to be later mapped for PIODMA.
 */
static struct dcp_allocate_buffer_resp
dcpep_cb_allocate_buffer(struct apple_dcp *dcp,
			 struct dcp_allocate_buffer_req *req)
{
	struct dcp_allocate_buffer_resp resp = { 0 };
	struct dcp_mem_descriptor *memdesc;
	size_t size;
	u32 id;

	resp.dva_size = ALIGN(req->size, 4096);
	resp.mem_desc_id =
		find_first_zero_bit(dcp->memdesc_map, DCP_MAX_MAPPINGS);

	if (resp.mem_desc_id >= DCP_MAX_MAPPINGS) {
		dev_warn(dcp->dev, "DCP overflowed mapping table, ignoring\n");
		resp.dva_size = 0;
		resp.mem_desc_id = 0;
		return resp;
	}
	id = resp.mem_desc_id;
	set_bit(id, dcp->memdesc_map);

	memdesc = &dcp->memdesc[id];

	memdesc->size = resp.dva_size;
	/* HACK: align size to 16K since the iommu API only maps full pages */
	size = ALIGN(resp.dva_size, SZ_16K);
	memdesc->buf = dma_alloc_coherent(dcp->dev, size,
					  &memdesc->dva, GFP_KERNEL);

	dma_get_sgtable(dcp->dev, &memdesc->map, memdesc->buf, memdesc->dva,
			size);
	resp.dva = memdesc->dva;

	return resp;
}

static u8 dcpep_cb_release_mem_desc(struct apple_dcp *dcp, u32 *mem_desc_id)
{
	struct dcp_mem_descriptor *memdesc;
	u32 id = *mem_desc_id;

	if (id >= DCP_MAX_MAPPINGS) {
		dev_warn(dcp->dev,
			 "unmap request for out of range mem_desc_id %u", id);
		return 0;
	}

	if (!test_and_clear_bit(id, dcp->memdesc_map)) {
		dev_warn(dcp->dev, "unmap request for unused mem_desc_id %u\n",
			 id);
		return 0;
	}

	memdesc = &dcp->memdesc[id];
	if (memdesc->buf) {
		dma_free_coherent(dcp->dev, memdesc->size, memdesc->buf,
				  memdesc->dva);

		memdesc->buf = NULL;
		memset(&memdesc->map, 0, sizeof(memdesc->map));
	} else {
		memdesc->reg = 0;
	}

	memdesc->size = 0;

	return 1;
}

/* Validate that the specified region is a display register */
static bool is_disp_register(struct apple_dcp *dcp, u64 start, u64 end)
{
	int i;

	for (i = 0; i < dcp->nr_disp_registers; ++i) {
		struct resource *r = dcp->disp_registers[i];

		if ((start >= r->start) && (end <= r->end))
			return true;
	}

	return false;
}

/*
 * Map contiguous physical memory into the DCP's address space. The firmware
 * uses this to map the display registers we advertise in
 * sr_map_device_memory_with_index, so we bounds check against that to guard
 * safe against malicious coprocessors.
 */
static struct dcp_map_physical_resp
dcpep_cb_map_physical(struct apple_dcp *dcp, struct dcp_map_physical_req *req)
{
	int size = ALIGN(req->size, 4096);
	dma_addr_t dva;
	u32 id;

	if (!is_disp_register(dcp, req->paddr, req->paddr + size - 1)) {
		dev_err(dcp->dev, "refusing to map phys address %llx size %llx\n",
			req->paddr, req->size);
		return (struct dcp_map_physical_resp){};
	}

	id = find_first_zero_bit(dcp->memdesc_map, DCP_MAX_MAPPINGS);
	set_bit(id, dcp->memdesc_map);
	dcp->memdesc[id].size = size;
	dcp->memdesc[id].reg = req->paddr;

	dva = dma_map_resource(dcp->dev, req->paddr, size, DMA_BIDIRECTIONAL, 0);
	WARN_ON(dva == DMA_MAPPING_ERROR);

	return (struct dcp_map_physical_resp){
		.dva_size = size,
		.mem_desc_id = id,
		.dva = dva,
	};
}

static u64 dcpep_cb_get_frequency(struct apple_dcp *dcp)
{
	return clk_get_rate(dcp->clk);
}

static struct DCP_FW_NAME(dcp_map_reg_resp) dcpep_cb_map_reg(struct apple_dcp *dcp,
						struct DCP_FW_NAME(dcp_map_reg_req) *req)
{
	if (req->index >= dcp->nr_disp_registers) {
		dev_warn(dcp->dev, "attempted to read invalid reg index %u\n",
			 req->index);

		return (struct DCP_FW_NAME(dcp_map_reg_resp)){ .ret = 1 };
	} else {
		struct resource *rsrc = dcp->disp_registers[req->index];
#if DCP_FW_VER >= DCP_FW_VERSION(13, 2, 0)
		dma_addr_t dva = dma_map_resource(dcp->dev, rsrc->start, resource_size(rsrc),
						  DMA_BIDIRECTIONAL, 0);
		WARN_ON(dva == DMA_MAPPING_ERROR);
#endif

		return (struct DCP_FW_NAME(dcp_map_reg_resp)){
			.addr = rsrc->start,
			.length = resource_size(rsrc),
#if DCP_FW_VER >= DCP_FW_VERSION(13, 2, 0)
			.dva = dva,
#endif
		};
	}
}

static struct dcp_read_edt_data_resp
dcpep_cb_read_edt_data(struct apple_dcp *dcp, struct dcp_read_edt_data_req *req)
{
	return (struct dcp_read_edt_data_resp){
		.value[0] = req->value[0],
		.ret = 0,
	};
}

static void iomfbep_cb_enable_backlight_message_ap_gated(struct apple_dcp *dcp,
							 u8 *enabled)
{
	/*
	 * update backlight brightness on next swap, on non mini-LED displays
	 * DCP seems to set an invalid iDAC value after coming out of DPMS.
	 * syslog: "[BrightnessLCD.cpp:743][AFK]nitsToDBV: iDAC out of range"
	 */
	dcp->brightness.update = true;
	schedule_work(&dcp->bl_update_wq);
}

/* Chunked data transfer for property dictionaries */
static u8 dcpep_cb_prop_start(struct apple_dcp *dcp, u32 *length)
{
	if (dcp->chunks.data != NULL) {
		dev_warn(dcp->dev, "ignoring spurious transfer start\n");
		return false;
	}

	dcp->chunks.length = *length;
	dcp->chunks.data = devm_kzalloc(dcp->dev, *length, GFP_KERNEL);

	if (!dcp->chunks.data) {
		dev_warn(dcp->dev, "failed to allocate chunks\n");
		return false;
	}

	return true;
}

static u8 dcpep_cb_prop_chunk(struct apple_dcp *dcp,
			      struct dcp_set_dcpav_prop_chunk_req *req)
{
	if (!dcp->chunks.data) {
		dev_warn(dcp->dev, "ignoring spurious chunk\n");
		return false;
	}

	if (req->offset + req->length > dcp->chunks.length) {
		dev_warn(dcp->dev, "ignoring overflowing chunk\n");
		return false;
	}

	memcpy(dcp->chunks.data + req->offset, req->data, req->length);
	return true;
}

static bool dcpep_process_chunks(struct apple_dcp *dcp,
				 struct dcp_set_dcpav_prop_end_req *req)
{
	struct dcp_parse_ctx ctx;
	int ret;

	if (!dcp->chunks.data) {
		dev_warn(dcp->dev, "ignoring spurious end\n");
		return false;
	}

	/* used just as opaque pointer for tracing */
	ctx.dcp = dcp;

	ret = parse(dcp->chunks.data, dcp->chunks.length, &ctx);

	if (ret) {
		dev_warn(dcp->dev, "bad header on dcpav props\n");
		return false;
	}

	if (!strcmp(req->key, "TimingElements")) {
		dcp->modes = enumerate_modes(&ctx, &dcp->nr_modes,
					     dcp->width_mm, dcp->height_mm,
					     dcp->notch_height);

		if (IS_ERR(dcp->modes)) {
			dev_warn(dcp->dev, "failed to parse modes\n");
			dcp->modes = NULL;
			dcp->nr_modes = 0;
			return false;
		}
		if (dcp->nr_modes == 0)
			dev_warn(dcp->dev, "TimingElements without valid modes!\n");
	} else if (!strcmp(req->key, "DisplayAttributes")) {
		ret = parse_display_attributes(&ctx, &dcp->width_mm,
					&dcp->height_mm);

		if (ret) {
			dev_warn(dcp->dev, "failed to parse display attribs\n");
			return false;
		}

		dcp_set_dimensions(dcp);
	}

	return true;
}

static u8 dcpep_cb_prop_end(struct apple_dcp *dcp,
			    struct dcp_set_dcpav_prop_end_req *req)
{
	u8 resp = dcpep_process_chunks(dcp, req);

	/* Reset for the next transfer */
	devm_kfree(dcp->dev, dcp->chunks.data);
	dcp->chunks.data = NULL;

	return resp;
}

/* Boot sequence */
static void boot_done(struct apple_dcp *dcp, void *out, void *cookie)
{
	struct dcp_channel *ch = &dcp->ch_cb;
	u8 *succ = ch->output[ch->depth - 1];
	dev_dbg(dcp->dev, "boot done\n");

	*succ = true;
	dcp_ack(dcp, DCP_CONTEXT_CB);
}

static void boot_5(struct apple_dcp *dcp, void *out, void *cookie)
{
	dcp_set_display_refresh_properties(dcp, false, boot_done, NULL);
}

static void boot_4(struct apple_dcp *dcp, void *out, void *cookie)
{
#if DCP_FW_VER >= DCP_FW_VERSION(13, 2, 0)
	u32 v_true = 1;
	dcp_late_init_signal(dcp, false, &v_true, boot_5, NULL);
#else
	dcp_late_init_signal(dcp, false, boot_5, NULL);
#endif
}

static void boot_3(struct apple_dcp *dcp, void *out, void *cookie)
{
	u32 v_true = true;

	dcp_flush_supports_power(dcp, false, &v_true, boot_4, NULL);
}

static void boot_2(struct apple_dcp *dcp, void *out, void *cookie)
{
	dcp_setup_video_limits(dcp, false, boot_3, NULL);
}

static void boot_1_5(struct apple_dcp *dcp, void *out, void *cookie)
{
	dcp_create_default_fb(dcp, false, boot_2, NULL);
}

/* Use special function signature to defer the ACK */
static bool dcpep_cb_boot_1(struct apple_dcp *dcp, int tag, void *out, void *in)
{
	trace_iomfb_callback(dcp, tag, __func__);
	dcp_set_create_dfb(dcp, false, boot_1_5, NULL);
	return false;
}

static struct dcp_allocate_bandwidth_resp dcpep_cb_allocate_bandwidth(struct apple_dcp *dcp,
						struct dcp_allocate_bandwidth_req *req)
{
	return (struct dcp_allocate_bandwidth_resp){
		.unk1 = req->unk1,
		.unk2 = req->unk2,
		.ret = 1,
	};
}

static struct dcp_rt_bandwidth dcpep_cb_rt_bandwidth(struct apple_dcp *dcp)
{
	struct dcp_rt_bandwidth rt_bw = (struct dcp_rt_bandwidth){
			.reg_scratch = 0,
			.reg_doorbell = 0,
			.doorbell_bit = 0,
	};

	if (dcp->disp_bw_scratch_index) {
		u32 offset = dcp->disp_bw_scratch_offset;
		u32 index = dcp->disp_bw_scratch_index;
		rt_bw.reg_scratch = dcp->disp_registers[index]->start + offset;
	}

	if (dcp->disp_bw_doorbell_index) {
		u32 index = dcp->disp_bw_doorbell_index;
		rt_bw.reg_doorbell = dcp->disp_registers[index]->start;
		rt_bw.doorbell_bit = REG_DOORBELL_BIT(dcp->index);
		/*
		 * This is most certainly not padding. t8103-dcp crashes without
		 * setting this immediately during modeset on 12.3 and 13.5
		 * firmware.
		 */
		rt_bw.padding[3] = 0x4;
	}

	return rt_bw;
}

static struct dcp_set_frame_sync_props_resp
dcpep_cb_set_frame_sync_props(struct apple_dcp *dcp,
			      struct dcp_set_frame_sync_props_req *req)
{
	return (struct dcp_set_frame_sync_props_resp){};
}

/* Callback to get the current time as milliseconds since the UNIX epoch */
static u64 dcpep_cb_get_time(struct apple_dcp *dcp)
{
	return ktime_to_ms(ktime_get_real());
}

struct dcp_swap_cookie {
	struct kref refcount;
	struct completion done;
	u32 swap_id;
};

static void release_swap_cookie(struct kref *ref)
{
	struct dcp_swap_cookie *cookie;
	cookie = container_of(ref, struct dcp_swap_cookie, refcount);

        kfree(cookie);
}

static void dcp_swap_cleared(struct apple_dcp *dcp, void *data, void *cookie)
{
	struct DCP_FW_NAME(dcp_swap_submit_resp) *resp = data;

	if (cookie) {
		struct dcp_swap_cookie *info = cookie;
		complete(&info->done);
		kref_put(&info->refcount, release_swap_cookie);
	}

	if (resp->ret) {
		dev_err(dcp->dev, "swap_clear failed! status %u\n", resp->ret);
		dcp_drm_crtc_vblank(dcp->crtc);
		return;
	}

	while (!list_empty(&dcp->swapped_out_fbs)) {
		struct dcp_fb_reference *entry;
		entry = list_first_entry(&dcp->swapped_out_fbs,
					 struct dcp_fb_reference, head);
		if (entry->swap_id == dcp->last_swap_id)
			break;
		if (entry->fb)
			drm_framebuffer_put(entry->fb);
		list_del(&entry->head);
		kfree(entry);
	}
}

static void dcp_swap_clear_started(struct apple_dcp *dcp, void *data,
				   void *cookie)
{
	struct dcp_swap_start_resp *resp = data;
	DCP_FW_UNION(dcp->swap).swap.swap_id = resp->swap_id;

	if (cookie) {
		struct dcp_swap_cookie *info = cookie;
		info->swap_id = resp->swap_id;
	}

	dcp_swap_submit(dcp, false, &DCP_FW_UNION(dcp->swap), dcp_swap_cleared, cookie);
}

static void dcp_on_final(struct apple_dcp *dcp, void *out, void *cookie)
{
	struct dcp_wait_cookie *wait = cookie;

	if (wait) {
		complete(&wait->done);
		kref_put(&wait->refcount, release_wait_cookie);
	}
}

static void dcp_on_set_power_state(struct apple_dcp *dcp, void *out, void *cookie)
{
	struct dcp_set_power_state_req req = {
		.unklong = 1,
	};

	dcp_set_power_state(dcp, false, &req, dcp_on_final, cookie);
}

static void dcp_on_set_parameter(struct apple_dcp *dcp, void *out, void *cookie)
{
	struct dcp_set_parameter_dcp param = {
		.param = 14,
		.value = { 0 },
#if DCP_FW_VER >= DCP_FW_VERSION(13, 2, 0)
		.count = 3,
#else
		.count = 1,
#endif
	};

	dcp_set_parameter_dcp(dcp, false, &param, dcp_on_set_power_state, cookie);
}

void DCP_FW_NAME(iomfb_poweron)(struct apple_dcp *dcp)
{
	struct dcp_wait_cookie *cookie;
	int ret;
	u32 handle;
	dev_info(dcp->dev, "dcp_poweron() starting\n");

	cookie = kzalloc(sizeof(*cookie), GFP_KERNEL);
	if (!cookie)
		return;

	init_completion(&cookie->done);
	kref_init(&cookie->refcount);
	/* increase refcount to ensure the receiver has a reference */
	kref_get(&cookie->refcount);

	if (dcp->main_display) {
		handle = 0;
		dcp_set_display_device(dcp, false, &handle, dcp_on_set_power_state,
				       cookie);
	} else {
		handle = 2;
		dcp_set_display_device(dcp, false, &handle,
				       dcp_on_set_parameter, cookie);
	}
	ret = wait_for_completion_timeout(&cookie->done, msecs_to_jiffies(500));

	if (ret == 0)
		dev_warn(dcp->dev, "wait for power timed out\n");

	kref_put(&cookie->refcount, release_wait_cookie);;

	/* Force a brightness update after poweron, to restore the brightness */
	dcp->brightness.update = true;
}

static void complete_set_powerstate(struct apple_dcp *dcp, void *out,
				    void *cookie)
{
	struct dcp_wait_cookie *wait = cookie;

	if (wait) {
		complete(&wait->done);
		kref_put(&wait->refcount, release_wait_cookie);
	}
}

static void last_client_closed_poff(struct apple_dcp *dcp, void *out, void *cookie)
{
	struct dcp_set_power_state_req power_req = {
		.unklong = 0,
	};
	dcp_set_power_state(dcp, false, &power_req, complete_set_powerstate,
			    cookie);
}

static void aborted_swaps_dcp_poff(struct apple_dcp *dcp, void *out, void *cookie)
{
	struct iomfb_last_client_close_req last_client_req = {};
	iomfb_last_client_close(dcp, false, &last_client_req,
				last_client_closed_poff, cookie);
}

void DCP_FW_NAME(iomfb_poweroff)(struct apple_dcp *dcp)
{
	int ret, swap_id;
	struct iomfb_abort_swaps_dcp_req abort_req = {
		.client = {
			.flag2 = 1,
		},
	};
	struct dcp_swap_cookie *cookie;
	struct dcp_wait_cookie *poff_cookie;
	struct dcp_swap_start_req swap_req = { 0 };
	struct DCP_FW_NAME(dcp_swap_submit_req) *swap = &DCP_FW_UNION(dcp->swap);

	cookie = kzalloc(sizeof(*cookie), GFP_KERNEL);
	if (!cookie)
		return;
	init_completion(&cookie->done);
	kref_init(&cookie->refcount);
	/* increase refcount to ensure the receiver has a reference */
	kref_get(&cookie->refcount);

	// clear surfaces
	memset(swap, 0, sizeof(*swap));

	swap->swap.swap_enabled =
		swap->swap.swap_completed = IOMFB_SET_BACKGROUND | 0x7;
	swap->swap.bg_color = 0xFF000000;

	/*
	 * Turn off the backlight. This matters because the DCP's idea of
	 * backlight brightness gets desynced after a power change, and it
	 * needs to be told it's going to turn off so it will consider the
	 * subsequent update on poweron an actual change and restore the
	 * brightness.
	 */
	if (dcp_has_panel(dcp)) {
		swap->swap.bl_unk = 1;
		swap->swap.bl_value = 0;
		swap->swap.bl_power = 0;
	}

	for (int l = 0; l < SWAP_SURFACES; l++)
		swap->surf_null[l] = true;
#if DCP_FW_VER >= DCP_FW_VERSION(13, 2, 0)
	for (int l = 0; l < 5; l++)
		swap->surf2_null[l] = true;
	swap->unkU32Ptr_null = true;
	swap->unkU32out_null = true;
#endif

	dcp_swap_start(dcp, false, &swap_req, dcp_swap_clear_started, cookie);

	ret = wait_for_completion_timeout(&cookie->done, msecs_to_jiffies(50));
	swap_id = cookie->swap_id;
	kref_put(&cookie->refcount, release_swap_cookie);
	if (ret <= 0) {
		dcp->crashed = true;
		return;
	}

	dev_dbg(dcp->dev, "%s: clear swap submitted: %u\n", __func__, swap_id);

	poff_cookie = kzalloc(sizeof(*poff_cookie), GFP_KERNEL);
	if (!poff_cookie)
		return;
	init_completion(&poff_cookie->done);
	kref_init(&poff_cookie->refcount);
	/* increase refcount to ensure the receiver has a reference */
	kref_get(&poff_cookie->refcount);

	iomfb_abort_swaps_dcp(dcp, false, &abort_req,
				aborted_swaps_dcp_poff, poff_cookie);
	ret = wait_for_completion_timeout(&poff_cookie->done,
					  msecs_to_jiffies(1000));

	if (ret == 0)
		dev_warn(dcp->dev, "setPowerState(0) timeout %u ms\n", 1000);
	else if (ret > 0)
		dev_dbg(dcp->dev,
			"setPowerState(0) finished with %d ms to spare",
			jiffies_to_msecs(ret));

	kref_put(&poff_cookie->refcount, release_wait_cookie);

	dev_info(dcp->dev, "dcp_poweroff() done\n");
}

static void last_client_closed_sleep(struct apple_dcp *dcp, void *out, void *cookie)
{
	struct dcp_set_power_state_req power_req = {
		.unklong = 0,
	};
	dcp_set_power_state(dcp, false, &power_req, complete_set_powerstate, cookie);
}

static void aborted_swaps_dcp_sleep(struct apple_dcp *dcp, void *out, void *cookie)
{
	struct iomfb_last_client_close_req req = { 0 };
	iomfb_last_client_close(dcp, false, &req, last_client_closed_sleep, cookie);
}

void DCP_FW_NAME(iomfb_sleep)(struct apple_dcp *dcp)
{
	int ret;
	struct iomfb_abort_swaps_dcp_req req = {
		.client = {
			.flag2 = 1,
		},
	};

	struct dcp_wait_cookie *cookie;

	cookie = kzalloc(sizeof(*cookie), GFP_KERNEL);
	if (!cookie)
		return;
	init_completion(&cookie->done);
	kref_init(&cookie->refcount);
	/* increase refcount to ensure the receiver has a reference */
	kref_get(&cookie->refcount);

	iomfb_abort_swaps_dcp(dcp, false, &req, aborted_swaps_dcp_sleep,
				cookie);
	ret = wait_for_completion_timeout(&cookie->done,
					  msecs_to_jiffies(1000));

	if (ret == 0)
		dev_warn(dcp->dev, "setDCPPower(0) timeout %u ms\n", 1000);

	kref_put(&cookie->refcount, release_wait_cookie);
	dev_info(dcp->dev, "dcp_sleep() done\n");
}

static void dcpep_cb_hotplug(struct apple_dcp *dcp, u64 *connected)
{
	struct apple_connector *connector = dcp->connector;

	/* DCP issues hotplug_gated callbacks after SetPowerState() calls on
	 * devices with display (macbooks, imacs). This must not result in
	 * connector state changes on DRM side. Some applications won't enable
	 * a CRTC with a connector in disconnected state. Weston after DPMS off
	 * is one example. dcp_is_main_display() returns true on devices with
	 * integrated display. Ignore the hotplug_gated() callbacks there.
	 */
	if (dcp->main_display)
		return;

	if (dcp->during_modeset) {
		dev_info(dcp->dev,
			 "cb_hotplug() ignored during modeset connected:%llu\n",
			 *connected);
		return;
	}

	dev_info(dcp->dev, "cb_hotplug() connected:%llu, valid_mode:%d\n",
		 *connected, dcp->valid_mode);

	/* Hotplug invalidates mode. DRM doesn't always handle this. */
	if (!(*connected)) {
		dcp->valid_mode = false;
		/* after unplug swap will not complete until the next
		 * set_digital_out_mode */
		schedule_work(&dcp->vblank_wq);
	}

	if (connector && connector->connected != !!(*connected)) {
		connector->connected = !!(*connected);
		dcp->valid_mode = false;
		schedule_work(&connector->hotplug_wq);
	}
}

static void
dcpep_cb_swap_complete_intent_gated(struct apple_dcp *dcp,
				    struct dcp_swap_complete_intent_gated *info)
{
	trace_iomfb_swap_complete_intent_gated(dcp, info->swap_id,
		info->width, info->height);
}

static void
dcpep_cb_abort_swap_ap_gated(struct apple_dcp *dcp, u32 *swap_id)
{
	trace_iomfb_abort_swap_ap_gated(dcp, *swap_id);
}

static struct dcpep_get_tiling_state_resp
dcpep_cb_get_tiling_state(struct apple_dcp *dcp,
			  struct dcpep_get_tiling_state_req *req)
{
	return (struct dcpep_get_tiling_state_resp){
		.value = 0,
		.ret = 1,
	};
}

static u8 dcpep_cb_create_backlight_service(struct apple_dcp *dcp)
{
	return dcp_has_panel(dcp);
}

TRAMPOLINE_VOID(trampoline_nop, dcpep_cb_nop);
TRAMPOLINE_OUT(trampoline_true, dcpep_cb_true, u8);
TRAMPOLINE_OUT(trampoline_false, dcpep_cb_false, u8);
TRAMPOLINE_OUT(trampoline_zero, dcpep_cb_zero, u32);
TRAMPOLINE_IN(trampoline_swap_complete, dcpep_cb_swap_complete,
	      struct DCP_FW_NAME(dc_swap_complete_resp));
TRAMPOLINE_INOUT(trampoline_get_uint_prop, dcpep_cb_get_uint_prop,
		 struct dcp_get_uint_prop_req, struct dcp_get_uint_prop_resp);
TRAMPOLINE_IN(trampoline_set_fx_prop, iomfbep_cb_set_fx_prop,
	      struct iomfb_set_fx_prop_req)
TRAMPOLINE_INOUT(trampoline_map_piodma, dcpep_cb_map_piodma,
		 struct dcp_map_buf_req, struct dcp_map_buf_resp);
TRAMPOLINE_IN(trampoline_unmap_piodma, dcpep_cb_unmap_piodma,
	      struct dcp_unmap_buf_resp);
TRAMPOLINE_INOUT(trampoline_sr_set_property_int, iomfbep_cb_sr_set_property_int,
		 struct iomfb_sr_set_property_int_req, u8);
TRAMPOLINE_INOUT(trampoline_allocate_buffer, dcpep_cb_allocate_buffer,
		 struct dcp_allocate_buffer_req,
		 struct dcp_allocate_buffer_resp);
TRAMPOLINE_INOUT(trampoline_map_physical, dcpep_cb_map_physical,
		 struct dcp_map_physical_req, struct dcp_map_physical_resp);
TRAMPOLINE_INOUT(trampoline_release_mem_desc, dcpep_cb_release_mem_desc, u32,
		 u8);
TRAMPOLINE_INOUT(trampoline_map_reg, dcpep_cb_map_reg,
		 struct DCP_FW_NAME(dcp_map_reg_req),
		 struct DCP_FW_NAME(dcp_map_reg_resp));
TRAMPOLINE_INOUT(trampoline_read_edt_data, dcpep_cb_read_edt_data,
		 struct dcp_read_edt_data_req, struct dcp_read_edt_data_resp);
TRAMPOLINE_INOUT(trampoline_prop_start, dcpep_cb_prop_start, u32, u8);
TRAMPOLINE_INOUT(trampoline_prop_chunk, dcpep_cb_prop_chunk,
		 struct dcp_set_dcpav_prop_chunk_req, u8);
TRAMPOLINE_INOUT(trampoline_prop_end, dcpep_cb_prop_end,
		 struct dcp_set_dcpav_prop_end_req, u8);
TRAMPOLINE_INOUT(trampoline_allocate_bandwidth, dcpep_cb_allocate_bandwidth,
	       struct dcp_allocate_bandwidth_req, struct dcp_allocate_bandwidth_resp);
TRAMPOLINE_OUT(trampoline_rt_bandwidth, dcpep_cb_rt_bandwidth,
	       struct dcp_rt_bandwidth);
TRAMPOLINE_INOUT(trampoline_set_frame_sync_props, dcpep_cb_set_frame_sync_props,
	       struct dcp_set_frame_sync_props_req,
	       struct dcp_set_frame_sync_props_resp);
TRAMPOLINE_OUT(trampoline_get_frequency, dcpep_cb_get_frequency, u64);
TRAMPOLINE_OUT(trampoline_get_time, dcpep_cb_get_time, u64);
TRAMPOLINE_IN(trampoline_hotplug, dcpep_cb_hotplug, u64);
TRAMPOLINE_IN(trampoline_swap_complete_intent_gated,
	      dcpep_cb_swap_complete_intent_gated,
	      struct dcp_swap_complete_intent_gated);
TRAMPOLINE_IN(trampoline_abort_swap_ap_gated, dcpep_cb_abort_swap_ap_gated, u32);
TRAMPOLINE_IN(trampoline_enable_backlight_message_ap_gated,
	      iomfbep_cb_enable_backlight_message_ap_gated, u8);
TRAMPOLINE_IN(trampoline_pr_publish, iomfb_cb_pr_publish,
	      struct iomfb_property);
TRAMPOLINE_INOUT(trampoline_get_tiling_state, dcpep_cb_get_tiling_state,
		 struct dcpep_get_tiling_state_req, struct dcpep_get_tiling_state_resp);
TRAMPOLINE_OUT(trampoline_create_backlight_service, dcpep_cb_create_backlight_service, u8);

/*
 * Callback for swap requests. If a swap failed, we'll never get a swap
 * complete event so we need to fake a vblank event early to avoid a hang.
 */

static void dcp_swapped(struct apple_dcp *dcp, void *data, void *cookie)
{
	struct DCP_FW_NAME(dcp_swap_submit_resp) *resp = data;

	if (resp->ret) {
		dev_err(dcp->dev, "swap failed! status %u\n", resp->ret);
		dcp_drm_crtc_vblank(dcp->crtc);
		return;
	}

	while (!list_empty(&dcp->swapped_out_fbs)) {
		struct dcp_fb_reference *entry;
		entry = list_first_entry(&dcp->swapped_out_fbs,
					 struct dcp_fb_reference, head);
		if (entry->swap_id == dcp->last_swap_id)
			break;
		if (entry->fb)
			drm_framebuffer_put(entry->fb);
		list_del(&entry->head);
		kfree(entry);
	}
}

static void dcp_swap_started(struct apple_dcp *dcp, void *data, void *cookie)
{
	struct dcp_swap_start_resp *resp = data;

	DCP_FW_UNION(dcp->swap).swap.swap_id = resp->swap_id;

	trace_iomfb_swap_submit(dcp, resp->swap_id);
	dcp_swap_submit(dcp, false, &DCP_FW_UNION(dcp->swap), dcp_swapped, NULL);
}

/* Helpers to modeset and swap, used to flush */
static void do_swap(struct apple_dcp *dcp, void *data, void *cookie)
{
	struct dcp_swap_start_req start_req = { 0 };

	if (dcp->connector && dcp->connector->connected)
		dcp_swap_start(dcp, false, &start_req, dcp_swap_started, NULL);
	else
		dcp_drm_crtc_vblank(dcp->crtc);
}

static void complete_set_digital_out_mode(struct apple_dcp *dcp, void *data,
					  void *cookie)
{
	struct dcp_wait_cookie *wait = cookie;

	if (wait) {
		complete(&wait->done);
		kref_put(&wait->refcount, release_wait_cookie);
	}
}

int DCP_FW_NAME(iomfb_modeset)(struct apple_dcp *dcp,
			       struct drm_crtc_state *crtc_state)
{
	struct dcp_display_mode *mode;
	struct dcp_wait_cookie *cookie;
	struct dcp_color_mode *cmode = NULL;
	int ret;

	mode = lookup_mode(dcp, &crtc_state->mode);
	if (!mode) {
		dev_err(dcp->dev, "no match for " DRM_MODE_FMT "\n",
			DRM_MODE_ARG(&crtc_state->mode));
		return -EIO;
	}

	dev_info(dcp->dev,
		 "set_digital_out_mode(color:%d timing:%d) " DRM_MODE_FMT "\n",
		 mode->color_mode_id, mode->timing_mode_id,
		 DRM_MODE_ARG(&crtc_state->mode));
	if (mode->color_mode_id == mode->sdr_rgb.id)
		cmode = &mode->sdr_rgb;
	else if (mode->color_mode_id == mode->sdr_444.id)
		cmode = &mode->sdr_444;
	else if (mode->color_mode_id == mode->sdr.id)
		cmode = &mode->sdr;
	else if (mode->color_mode_id == mode->best.id)
		cmode = &mode->best;
	if (cmode)
		dev_info(dcp->dev,
			"set_digital_out_mode() color mode depth:%hhu format:%u "
			"colorimetry:%u eotf:%u range:%u\n", cmode->depth,
			cmode->format, cmode->colorimetry, cmode->eotf,
			cmode->range);

	dcp->mode = (struct dcp_set_digital_out_mode_req){
		.color_mode_id = mode->color_mode_id,
		.timing_mode_id = mode->timing_mode_id
	};

	cookie = kzalloc(sizeof(*cookie), GFP_KERNEL);
	if (!cookie) {
		return -ENOMEM;
	}

	init_completion(&cookie->done);
	kref_init(&cookie->refcount);
	/* increase refcount to ensure the receiver has a reference */
	kref_get(&cookie->refcount);

	dcp->during_modeset = true;

	dcp_set_digital_out_mode(dcp, false, &dcp->mode,
				 complete_set_digital_out_mode, cookie);

	/*
	 * The DCP firmware has an internal timeout of ~8 seconds for
	 * modesets. Add an extra 500ms to safe side that the modeset
	 * call has returned.
	 */
	ret = wait_for_completion_timeout(&cookie->done,
					  msecs_to_jiffies(8500));

	kref_put(&cookie->refcount, release_wait_cookie);
	dcp->during_modeset = false;
	dev_info(dcp->dev, "set_digital_out_mode finished:%d\n", ret);

	if (ret == 0) {
		dev_info(dcp->dev, "set_digital_out_mode timed out\n");
		return -EIO;
	} else if (ret < 0) {
		dev_info(dcp->dev,
			 "waiting on set_digital_out_mode failed:%d\n", ret);
		return -EIO;

	} else if (ret > 0) {
		dev_dbg(dcp->dev,
			"set_digital_out_mode finished with %d to spare\n",
			jiffies_to_msecs(ret));
	}
	dcp->valid_mode = true;

	return 0;
}

void DCP_FW_NAME(iomfb_flush)(struct apple_dcp *dcp, struct drm_crtc *crtc, struct drm_atomic_state *state)
{
	struct drm_plane *plane;
	struct drm_plane_state *new_state, *old_state;
	struct drm_crtc_state *crtc_state;
	struct DCP_FW_NAME(dcp_swap_submit_req) *req = &DCP_FW_UNION(dcp->swap);
	int plane_idx, l;
	int has_surface = 0;

	crtc_state = drm_atomic_get_new_crtc_state(state, crtc);

	/* Reset to defaults */
	memset(req, 0, sizeof(*req));
	for (l = 0; l < SWAP_SURFACES; l++)
		req->surf_null[l] = true;
#if DCP_FW_VER >= DCP_FW_VERSION(13, 2, 0)
	for (l = 0; l < 5; l++)
		req->surf2_null[l] = true;
	req->unkU32Ptr_null = true;
	req->unkU32out_null = true;
#endif

	/*
	 * Clear all surfaces on startup. The boot framebuffer in surface 0
	 * sticks around.
	 */
	if (!dcp->surfaces_cleared) {
		req->swap.swap_enabled = IOMFB_SET_BACKGROUND | 0x7;
		req->swap.bg_color = 0xFF000000;
		dcp->surfaces_cleared = true;
	}

	// Surface 0 has limitations at least on t600x.
	l = 1;
	for_each_oldnew_plane_in_state(state, plane, old_state, new_state, plane_idx) {
		struct drm_framebuffer *fb = new_state->fb;
		struct drm_gem_dma_object *obj;
		struct drm_rect src_rect;
		bool is_premultiplied = false;

		/* skip planes not for this crtc */
		if (old_state->crtc != crtc && new_state->crtc != crtc)
			continue;

		WARN_ON(l >= SWAP_SURFACES);

		req->swap.swap_enabled |= BIT(l);

		if (old_state->fb && fb != old_state->fb) {
			/*
			 * Race condition between a framebuffer unbind getting
			 * swapped out and GEM unreferencing a framebuffer. If
			 * we lose the race, the display gets IOVA faults and
			 * the DCP crashes. We need to extend the lifetime of
			 * the drm_framebuffer (and hence the GEM object) until
			 * after we get a swap complete for the swap unbinding
			 * it.
			 */
			struct dcp_fb_reference *entry =
				kzalloc(sizeof(*entry), GFP_KERNEL);
			if (entry) {
				entry->fb = old_state->fb;
				entry->swap_id = dcp->last_swap_id;
				list_add_tail(&entry->head,
					      &dcp->swapped_out_fbs);
			}
			drm_framebuffer_get(old_state->fb);
		}

		if (!new_state->fb) {
			l += 1;
			continue;
		}
		req->surf_null[l] = false;
		has_surface = 1;

		/*
		 * DCP doesn't support XBGR8 / XRGB8 natively. Blending as
		 * pre-multiplied alpha with a black background can be used as
		 * workaround for the bottommost plane.
		 */
		if (fb->format->format == DRM_FORMAT_XRGB8888 ||
		    fb->format->format == DRM_FORMAT_XBGR8888)
		    is_premultiplied = true;

		drm_rect_fp_to_int(&src_rect, &new_state->src);

		req->swap.src_rect[l] = drm_to_dcp_rect(&src_rect);
		req->swap.dst_rect[l] = drm_to_dcp_rect(&new_state->dst);

		if (dcp->notch_height > 0)
			req->swap.dst_rect[l].y += dcp->notch_height;

		/* the obvious helper call drm_fb_dma_get_gem_addr() adjusts
		 * the address for source x/y offsets. Since IOMFB has a direct
		 * support source position prefer that.
		 */
		obj = drm_fb_dma_get_gem_obj(fb, 0);
		if (obj)
			req->surf_iova[l] = obj->dma_addr + fb->offsets[0];

		req->surf[l] = (struct DCP_FW_NAME(dcp_surface)){
			.is_premultiplied = is_premultiplied,
			.format = drm_format_to_dcp(fb->format->format),
			.xfer_func = DCP_XFER_FUNC_SDR,
			.colorspace = DCP_COLORSPACE_NATIVE,
			.stride = fb->pitches[0],
			.width = fb->width,
			.height = fb->height,
			.buf_size = fb->height * fb->pitches[0],
			.surface_id = req->swap.surf_ids[l],

			/* Only used for compressed or multiplanar surfaces */
			.pix_size = 1,
			.pel_w = 1,
			.pel_h = 1,
			.has_comp = 1,
			.has_planes = 1,
		};

		l += 1;
	}

	if (!has_surface && !crtc_state->color_mgmt_changed) {
		if (crtc_state->enable && crtc_state->active &&
		    !crtc_state->planes_changed) {
			schedule_work(&dcp->vblank_wq);
			return;
		}

		/* Set black background */
		req->swap.swap_enabled |= IOMFB_SET_BACKGROUND;
		req->swap.bg_color = 0xFF000000;
		req->clear = 1;
	}

	/* These fields should be set together */
	req->swap.swap_completed = req->swap.swap_enabled;

	/* update brightness if changed */
	if (dcp_has_panel(dcp) && dcp->brightness.update) {
		req->swap.bl_unk = 1;
		req->swap.bl_value = dcp->brightness.dac;
		req->swap.bl_power = 0x40;
		dcp->brightness.update = false;
	}

	if (crtc_state->color_mgmt_changed && crtc_state->ctm) {
		struct iomfb_set_matrix_req mat;
		struct drm_color_ctm *ctm = (struct drm_color_ctm *)crtc_state->ctm->data;

		mat.unk_u32 = 9;
		mat.r[0] = ctm->matrix[0];
		mat.r[1] = ctm->matrix[1];
		mat.r[2] = ctm->matrix[2];
		mat.g[0] = ctm->matrix[3];
		mat.g[1] = ctm->matrix[4];
		mat.g[2] = ctm->matrix[5];
		mat.b[0] = ctm->matrix[6];
		mat.b[1] = ctm->matrix[7];
		mat.b[2] = ctm->matrix[8];

		iomfb_set_matrix(dcp, false, &mat, do_swap, NULL);
	} else
		do_swap(dcp, NULL, NULL);
}

static void res_is_main_display(struct apple_dcp *dcp, void *out, void *cookie)
{
	struct apple_connector *connector;
	int result = *(int *)out;
	dev_info(dcp->dev, "DCP is_main_display: %d\n", result);

	dcp->main_display = result != 0;

	connector = dcp->connector;
	if (connector) {
		connector->connected = dcp->nr_modes > 0;
		schedule_work(&connector->hotplug_wq);
	}

	dcp->active = true;
	complete(&dcp->start_done);
}

static void init_3(struct apple_dcp *dcp, void *out, void *cookie)
{
	dcp_is_main_display(dcp, false, res_is_main_display, NULL);
}

static void init_2(struct apple_dcp *dcp, void *out, void *cookie)
{
	dcp_first_client_open(dcp, false, init_3, NULL);
}

static void init_1(struct apple_dcp *dcp, void *out, void *cookie)
{
	u32 val = 0;
	dcp_enable_disable_video_power_savings(dcp, false, &val, init_2, NULL);
}

static void dcp_started(struct apple_dcp *dcp, void *data, void *cookie)
{
	struct iomfb_get_color_remap_mode_req color_remap =
		(struct iomfb_get_color_remap_mode_req){
			.mode = 6,
		};

	dev_info(dcp->dev, "DCP booted\n");

	iomfb_get_color_remap_mode(dcp, false, &color_remap, init_1, cookie);
}

void DCP_FW_NAME(iomfb_shutdown)(struct apple_dcp *dcp)
{
	struct dcp_set_power_state_req req = {
		/* defaults are ok */
	};

	dcp_set_power_state(dcp, false, &req, NULL, NULL);
}
