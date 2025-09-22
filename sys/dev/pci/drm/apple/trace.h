// SPDX-License-Identifier: GPL-2.0-only OR MIT
/* Copyright (C) The Asahi Linux Contributors */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM dcp

#if !defined(_TRACE_DCP_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_DCP_H

#include "afk.h"
#include "dptxep.h"
#include "dcp-internal.h"
#include "parser.h"

#include <linux/stringify.h>
#include <linux/types.h>
#include <linux/tracepoint.h>

#define show_dcp_endpoint(ep)                                      \
	__print_symbolic(ep, { SYSTEM_ENDPOINT, "system" },        \
			 { TEST_ENDPOINT, "test" },                \
			 { DCP_EXPERT_ENDPOINT, "dcpexpert" },     \
			 { DISP0_ENDPOINT, "disp0" },              \
			 { DPTX_ENDPOINT, "dptxport" },            \
			 { HDCP_ENDPOINT, "hdcp" },                \
			 { REMOTE_ALLOC_ENDPOINT, "remotealloc" }, \
			 { IOMFB_ENDPOINT, "iomfb" })
#define print_epic_type(etype)                                  \
	__print_symbolic(etype, { EPIC_TYPE_NOTIFY, "notify" }, \
			 { EPIC_TYPE_COMMAND, "command" },      \
			 { EPIC_TYPE_REPLY, "reply" },          \
			 { EPIC_TYPE_NOTIFY_ACK, "notify-ack" })

#define print_epic_category(ecat)                             \
	__print_symbolic(ecat, { EPIC_CAT_REPORT, "report" }, \
			 { EPIC_CAT_NOTIFY, "notify" },       \
			 { EPIC_CAT_REPLY, "reply" },         \
			 { EPIC_CAT_COMMAND, "command" })

#define show_dptxport_apcall(idx)                                              \
	__print_symbolic(                                                     \
		idx, { DPTX_APCALL_ACTIVATE, "activate" },                    \
		{ DPTX_APCALL_DEACTIVATE, "deactivate" },                     \
		{ DPTX_APCALL_GET_MAX_DRIVE_SETTINGS,                         \
		  "get_max_drive_settings" },                                 \
		{ DPTX_APCALL_SET_DRIVE_SETTINGS, "set_drive_settings" },     \
		{ DPTX_APCALL_GET_DRIVE_SETTINGS, "get_drive_settings" },     \
		{ DPTX_APCALL_WILL_CHANGE_LINKG_CONFIG,                       \
		  "will_change_link_config" },                                \
		{ DPTX_APCALL_DID_CHANGE_LINK_CONFIG,                         \
		  "did_change_link_config" },                                 \
		{ DPTX_APCALL_GET_MAX_LINK_RATE, "get_max_link_rate" },       \
		{ DPTX_APCALL_GET_LINK_RATE, "get_link_rate" },               \
		{ DPTX_APCALL_SET_LINK_RATE, "set_link_rate" },               \
		{ DPTX_APCALL_GET_MAX_LANE_COUNT,                             \
		  "get_max_lane_count" },                                     \
		{ DPTX_APCALL_GET_ACTIVE_LANE_COUNT,                          \
		  "get_active_lane_count" },                                  \
		{ DPTX_APCALL_SET_ACTIVE_LANE_COUNT,                          \
		  "set_active_lane_count" },                                  \
		{ DPTX_APCALL_GET_SUPPORTS_DOWN_SPREAD,                       \
		  "get_supports_downspread" },                                \
		{ DPTX_APCALL_GET_DOWN_SPREAD, "get_downspread" },            \
		{ DPTX_APCALL_SET_DOWN_SPREAD, "set_downspread" },            \
		{ DPTX_APCALL_GET_SUPPORTS_LANE_MAPPING,                      \
		  "get_supports_lane_mapping" },                              \
		{ DPTX_APCALL_SET_LANE_MAP, "set_lane_map" },                 \
		{ DPTX_APCALL_GET_SUPPORTS_HPD, "get_supports_hpd" },         \
		{ DPTX_APCALL_FORCE_HOTPLUG_DETECT, "force_hotplug_detect" }, \
		{ DPTX_APCALL_INACTIVE_SINK_DETECTED,                         \
		  "inactive_sink_detected" },                                 \
		{ DPTX_APCALL_SET_TILED_DISPLAY_HINTS,                        \
		  "set_tiled_display_hints" },                                \
		{ DPTX_APCALL_DEVICE_NOT_RESPONDING,                          \
		  "device_not_responding" },                                  \
		{ DPTX_APCALL_DEVICE_BUSY_TIMEOUT, "device_busy_timeout" },   \
		{ DPTX_APCALL_DEVICE_NOT_STARTED, "device_not_started" })

TRACE_EVENT(dcp_recv_msg,
	    TP_PROTO(struct apple_dcp *dcp, u8 endpoint, u64 message),
	    TP_ARGS(dcp, endpoint, message),

	    TP_STRUCT__entry(__string(devname, dev_name(dcp->dev))
			     __field(u8, endpoint)
			     __field(u64, message)),

	    TP_fast_assign(__assign_str(devname, dev_name(dcp->dev));
			   __entry->endpoint = endpoint;
			   __entry->message = message;),

	    TP_printk("%s: endpoint 0x%x (%s): received message 0x%016llx",
		      __get_str(devname), __entry->endpoint,
		      show_dcp_endpoint(__entry->endpoint), __entry->message));

TRACE_EVENT(dcp_send_msg,
	    TP_PROTO(struct apple_dcp *dcp, u8 endpoint, u64 message),
	    TP_ARGS(dcp, endpoint, message),

	    TP_STRUCT__entry(__string(devname, dev_name(dcp->dev))
			     __field(u8, endpoint)
			     __field(u64, message)),

	    TP_fast_assign(__assign_str(devname, dev_name(dcp->dev));
			   __entry->endpoint = endpoint;
			   __entry->message = message;),

	    TP_printk("%s: endpoint 0x%x (%s): will send message 0x%016llx",
		      __get_str(devname), __entry->endpoint,
		      show_dcp_endpoint(__entry->endpoint), __entry->message));

TRACE_EVENT(
	afk_getbuf, TP_PROTO(struct apple_dcp_afkep *ep, u16 size, u16 tag),
	TP_ARGS(ep, size, tag),

	TP_STRUCT__entry(__string(devname, dev_name(ep->dcp->dev))
				 __field(u8, endpoint) __field(u16, size)
					 __field(u16, tag)),

	TP_fast_assign(__assign_str(devname, dev_name(ep->dcp->dev));
		       __entry->endpoint = ep->endpoint; __entry->size = size;
		       __entry->tag = tag;),

	TP_printk(
		"%s: endpoint 0x%x (%s): get buffer with size 0x%x and tag 0x%x",
		__get_str(devname), __entry->endpoint,
		show_dcp_endpoint(__entry->endpoint), __entry->size,
		__entry->tag));

DECLARE_EVENT_CLASS(afk_rwptr_template,
	    TP_PROTO(struct apple_dcp_afkep *ep, u32 rptr, u32 wptr),
	    TP_ARGS(ep, rptr, wptr),

	    TP_STRUCT__entry(__string(devname, dev_name(ep->dcp->dev))
				     __field(u8, endpoint) __field(u32, rptr)
					     __field(u32, wptr)),

	    TP_fast_assign(__assign_str(devname, dev_name(ep->dcp->dev));
			   __entry->endpoint = ep->endpoint;
			   __entry->rptr = rptr; __entry->wptr = wptr;),

	    TP_printk("%s: endpoint 0x%x (%s): rptr 0x%x, wptr 0x%x",
		      __get_str(devname), __entry->endpoint,
		      show_dcp_endpoint(__entry->endpoint), __entry->rptr,
		      __entry->wptr));

DEFINE_EVENT(afk_rwptr_template, afk_recv_rwptr_pre,
	TP_PROTO(struct apple_dcp_afkep *ep, u32 rptr, u32 wptr),
	TP_ARGS(ep, rptr, wptr));
DEFINE_EVENT(afk_rwptr_template, afk_recv_rwptr_post,
	TP_PROTO(struct apple_dcp_afkep *ep, u32 rptr, u32 wptr),
	TP_ARGS(ep, rptr, wptr));
DEFINE_EVENT(afk_rwptr_template, afk_send_rwptr_pre,
	TP_PROTO(struct apple_dcp_afkep *ep, u32 rptr, u32 wptr),
	TP_ARGS(ep, rptr, wptr));
DEFINE_EVENT(afk_rwptr_template, afk_send_rwptr_post,
	TP_PROTO(struct apple_dcp_afkep *ep, u32 rptr, u32 wptr),
	TP_ARGS(ep, rptr, wptr));

TRACE_EVENT(
	afk_recv_qe,
	TP_PROTO(struct apple_dcp_afkep *ep, u32 rptr, u32 magic, u32 size),
	TP_ARGS(ep, rptr, magic, size),

	TP_STRUCT__entry(__string(devname, dev_name(ep->dcp->dev))
				 __field(u8, endpoint) __field(u32, rptr)
					 __field(u32, magic)
						 __field(u32, size)),

	TP_fast_assign(__assign_str(devname, dev_name(ep->dcp->dev));
		       __entry->endpoint = ep->endpoint; __entry->rptr = rptr;
		       __entry->magic = magic; __entry->size = size;),

	TP_printk("%s: endpoint 0x%x (%s): QE rptr 0x%x, magic 0x%x, size 0x%x",
		  __get_str(devname), __entry->endpoint,
		  show_dcp_endpoint(__entry->endpoint), __entry->rptr,
		  __entry->magic, __entry->size));

TRACE_EVENT(
	afk_recv_handle,
	TP_PROTO(struct apple_dcp_afkep *ep, u32 channel, u32 type,
		 u32 data_size, struct epic_hdr *ehdr,
		 struct epic_sub_hdr *eshdr),
	TP_ARGS(ep, channel, type, data_size, ehdr, eshdr),

	TP_STRUCT__entry(__string(devname, dev_name(ep->dcp->dev)) __field(
		u8, endpoint) __field(u32, channel) __field(u32, type)
				 __field(u32, data_size) __field(u8, category)
					 __field(u16, subtype)
						 __field(u16, tag)),

	TP_fast_assign(__assign_str(devname, dev_name(ep->dcp->dev));
		       __entry->endpoint = ep->endpoint;
		       __entry->channel = channel; __entry->type = type;
		       __entry->data_size = data_size;
		       __entry->category = eshdr->category,
		       __entry->subtype = le16_to_cpu(eshdr->type),
		       __entry->tag = le16_to_cpu(eshdr->tag)),

	TP_printk(
		"%s: endpoint 0x%x (%s): channel 0x%x, type 0x%x (%s), data_size 0x%x, category: 0x%x (%s), subtype: 0x%x, seq: 0x%x",
		__get_str(devname), __entry->endpoint,
		show_dcp_endpoint(__entry->endpoint), __entry->channel,
		__entry->type, print_epic_type(__entry->type),
		__entry->data_size, __entry->category,
		print_epic_category(__entry->category), __entry->subtype,
		__entry->tag));

TRACE_EVENT(iomfb_callback,
	    TP_PROTO(struct apple_dcp *dcp, int tag, const char *name),
	    TP_ARGS(dcp, tag, name),

	    TP_STRUCT__entry(
				__string(devname, dev_name(dcp->dev))
				__field(int, tag)
				__field(const char *, name)
			),

	    TP_fast_assign(
				__assign_str(devname, dev_name(dcp->dev));
				__entry->tag = tag; __entry->name = name;
			),

	    TP_printk("%s: Callback D%03d %s", __get_str(devname), __entry->tag,
		      __entry->name));

TRACE_EVENT(iomfb_push,
	    TP_PROTO(struct apple_dcp *dcp,
		     const struct dcp_method_entry *method, int context,
		     int offset, int depth),
	    TP_ARGS(dcp, method, context, offset, depth),

	    TP_STRUCT__entry(
				__string(devname, dev_name(dcp->dev))
				__string(name, method->name)
				__field(int, context)
				__field(int, offset)
				__field(int, depth)),

	    TP_fast_assign(
				__assign_str(devname, dev_name(dcp->dev));
				__assign_str(name, method->name);
				__entry->context = context; __entry->offset = offset;
				__entry->depth = depth;
			),

	    TP_printk("%s: Method %s: context %u, offset %u, depth %u",
		      __get_str(devname), __get_str(name), __entry->context,
		      __entry->offset, __entry->depth));

TRACE_EVENT(iomfb_swap_submit,
	    TP_PROTO(struct apple_dcp *dcp, u32 swap_id),
	    TP_ARGS(dcp, swap_id),
	    TP_STRUCT__entry(
			     __field(u64, dcp)
			     __field(u32, swap_id)
	    ),
	    TP_fast_assign(
			   __entry->dcp = (u64)dcp;
			   __entry->swap_id = swap_id;
	    ),
	    TP_printk("dcp=%llx, swap_id=%d",
		      __entry->dcp,
		      __entry->swap_id)
);

TRACE_EVENT(iomfb_swap_complete,
	    TP_PROTO(struct apple_dcp *dcp, u32 swap_id),
	    TP_ARGS(dcp, swap_id),
	    TP_STRUCT__entry(
			     __field(u64, dcp)
			     __field(u32, swap_id)
	    ),
	    TP_fast_assign(
			   __entry->dcp = (u64)dcp;
			   __entry->swap_id = swap_id;
	    ),
	    TP_printk("dcp=%llx, swap_id=%d",
		      __entry->dcp,
		      __entry->swap_id
	    )
);

TRACE_EVENT(iomfb_swap_complete_intent_gated,
	    TP_PROTO(struct apple_dcp *dcp, u32 swap_id, u32 width, u32 height),
	    TP_ARGS(dcp, swap_id, width, height),
	    TP_STRUCT__entry(
			     __field(u64, dcp)
			     __field(u32, swap_id)
			     __field(u32, width)
			     __field(u32, height)
	    ),
	    TP_fast_assign(
			   __entry->dcp = (u64)dcp;
			   __entry->swap_id = swap_id;
			   __entry->height = height;
			   __entry->width = width;
	    ),
	    TP_printk("dcp=%llx, swap_id=%u %ux%u",
		      __entry->dcp,
		      __entry->swap_id,
		      __entry->width,
		      __entry->height
	    )
);

TRACE_EVENT(iomfb_abort_swap_ap_gated,
	    TP_PROTO(struct apple_dcp *dcp, u32 swap_id),
	    TP_ARGS(dcp, swap_id),
	    TP_STRUCT__entry(
			     __field(u64, dcp)
			     __field(u32, swap_id)
	    ),
	    TP_fast_assign(
			   __entry->dcp = (u64)dcp;
			   __entry->swap_id = swap_id;
	    ),
	    TP_printk("dcp=%llx, swap_id=%u",
		      __entry->dcp,
		      __entry->swap_id
	    )
);

DECLARE_EVENT_CLASS(iomfb_parse_mode_template,
	    TP_PROTO(s64 id, struct dimension *horiz, struct dimension *vert, s64 best_color_mode, bool is_virtual, s64 score),
	    TP_ARGS(id, horiz, vert, best_color_mode, is_virtual, score),

	    TP_STRUCT__entry(__field(s64, id)
			     __field_struct(struct dimension, horiz)
			     __field_struct(struct dimension, vert)
			     __field(s64, best_color_mode)
			     __field(bool, is_virtual)
			     __field(s64, score)),

	    TP_fast_assign(__entry->id = id;
			   __entry->horiz = *horiz;
			   __entry->vert = *vert;
			   __entry->best_color_mode = best_color_mode;
			   __entry->is_virtual = is_virtual;
			   __entry->score = score;),

	    TP_printk("id: %lld, best_color_mode: %lld, resolution:%lldx%lld virtual: %d, score: %lld",
		      __entry->id, __entry->best_color_mode,
		      __entry->horiz.active, __entry->vert.active,
		      __entry->is_virtual, __entry->score));

DEFINE_EVENT(iomfb_parse_mode_template, iomfb_parse_mode_success,
	    TP_PROTO(s64 id, struct dimension *horiz, struct dimension *vert, s64 best_color_mode, bool is_virtual, s64 score),
	    TP_ARGS(id, horiz, vert, best_color_mode, is_virtual, score));

DEFINE_EVENT(iomfb_parse_mode_template, iomfb_parse_mode_fail,
	    TP_PROTO(s64 id, struct dimension *horiz, struct dimension *vert, s64 best_color_mode, bool is_virtual, s64 score),
	    TP_ARGS(id, horiz, vert, best_color_mode, is_virtual, score));

TRACE_EVENT(dptxport_init, TP_PROTO(struct apple_dcp *dcp, u64 unit),
	    TP_ARGS(dcp, unit),

	    TP_STRUCT__entry(__string(devname, dev_name(dcp->dev))
				     __field(u64, unit)),

	    TP_fast_assign(__assign_str(devname, dev_name(dcp->dev));
			   __entry->unit = unit;),

	    TP_printk("%s: dptxport unit %lld initialized", __get_str(devname),
		      __entry->unit));

TRACE_EVENT(
	dptxport_apcall,
	TP_PROTO(struct dptx_port *dptx, int idx, size_t len),
	TP_ARGS(dptx, idx, len),

	TP_STRUCT__entry(__string(devname, dev_name(dptx->service->ep->dcp->dev))
			__field(u32, unit) __field(int, idx) __field(size_t, len)),

	TP_fast_assign(__assign_str(devname, dev_name(dptx->service->ep->dcp->dev));
		       __entry->unit = dptx->unit; __entry->idx = idx; __entry->len = len;),

	TP_printk("%s: dptx%d: AP Call %d (%s) with len %lu", __get_str(devname),
		  __entry->unit,
		  __entry->idx, show_dptxport_apcall(__entry->idx), __entry->len));

TRACE_EVENT(
	dptxport_validate_connection,
	TP_PROTO(struct dptx_port *dptx, u8 core, u8 atc, u8 die),
	TP_ARGS(dptx, core, atc, die),

	TP_STRUCT__entry(__string(devname, dev_name(dptx->service->ep->dcp->dev))
			 __field(u32, unit) __field(u8, core) __field(u8, atc) __field(u8, die)),

	TP_fast_assign(__assign_str(devname, dev_name(dptx->service->ep->dcp->dev));
		       __entry->unit = dptx->unit; __entry->core = core; __entry->atc = atc; __entry->die = die;),

	TP_printk("%s: dptx%d: core %d, atc %d, die %d", __get_str(devname),
		  __entry->unit, __entry->core, __entry->atc, __entry->die));

TRACE_EVENT(
	dptxport_connect,
	TP_PROTO(struct dptx_port *dptx, u8 core, u8 atc, u8 die),
	TP_ARGS(dptx, core, atc, die),

	TP_STRUCT__entry(__string(devname, dev_name(dptx->service->ep->dcp->dev))
			 __field(u32, unit) __field(u8, core) __field(u8, atc) __field(u8, die)),

	TP_fast_assign(__assign_str(devname, dev_name(dptx->service->ep->dcp->dev));
		       __entry->unit = dptx->unit; __entry->core = core; __entry->atc = atc; __entry->die = die;),

	TP_printk("%s: dptx%d: core %d, atc %d, die %d", __get_str(devname),
		  __entry->unit, __entry->core, __entry->atc, __entry->die));

TRACE_EVENT(
	dptxport_call_set_link_rate,
	TP_PROTO(struct dptx_port *dptx, u32 link_rate),
	TP_ARGS(dptx, link_rate),

	TP_STRUCT__entry(__string(devname, dev_name(dptx->service->ep->dcp->dev))
			 __field(u32, unit)
			 __field(u32, link_rate)),

	TP_fast_assign(__assign_str(devname, dev_name(dptx->service->ep->dcp->dev));
		       __entry->unit = dptx->unit;
		       __entry->link_rate = link_rate;),

	TP_printk("%s: dptx%d: link rate 0x%x", __get_str(devname), __entry->unit,
		  __entry->link_rate));

TRACE_EVENT(iomfb_brightness,
	    TP_PROTO(struct apple_dcp *dcp, u32 nits),
	    TP_ARGS(dcp, nits),
	    TP_STRUCT__entry(
			     __field(u64, dcp)
			     __field(u32, nits)
	    ),
	    TP_fast_assign(
			   __entry->dcp = (u64)dcp;
			   __entry->nits = nits;
	    ),
	    TP_printk("dcp=%llx, nits=%u (raw=0x%05x)",
		      __entry->dcp,
		      __entry->nits >> 16,
		      __entry->nits
	    )
);

#define show_eotf(eotf)					\
	__print_symbolic(eotf, { 0, "SDR gamma"},	\
			       { 1, "HDR gamma"},	\
			       { 2, "ST 2084 (PQ)"},	\
			       { 3, "BT.2100 (HLG)"},	\
			       { 4, "unexpected"})

#define show_encoding(enc)							\
	__print_symbolic(enc, { 0, "RGB"},					\
			      { 1, "YUV 4:2:0"},				\
			      { 3, "YUV 4:2:2"},				\
			      { 2, "YUV 4:4:4"},				\
			      { 4, "DolbyVision (native)"},			\
			      { 5, "DolbyVision (HDMI)"},			\
			      { 6, "YCbCr 4:2:2 (DP tunnel)"},			\
			      { 7, "YCbCr 4:2:2 (HDMI tunnel)"},		\
			      { 8, "DolbyVision LL YCbCr 4:2:2"},		\
			      { 9, "DolbyVision LL YCbCr 4:2:2 (DP)"},		\
			      {10, "DolbyVision LL YCbCr 4:2:2 (HDMI)"},	\
			      {11, "DolbyVision LL YCbCr 4:4:4"},		\
			      {12, "DolbyVision LL RGB 4:2:2"},			\
			      {13, "GRGB as YCbCr422 (Even line blue)"},	\
			      {14, "GRGB as YCbCr422 (Even line red)"},		\
			      {15, "unexpected"})

#define show_colorimetry(col)					\
	__print_symbolic(col, { 0, "SMPTE 170M/BT.601"},	\
			      { 1, "BT.701"},			\
			      { 2, "xvYCC601"},			\
			      { 3, "xvYCC709"},			\
			      { 4, "sYCC601"},			\
			      { 5, "AdobeYCC601"},		\
			      { 6, "BT.2020 (c)"},		\
			      { 7, "BT.2020 (nc)"},		\
			      { 8, "DolbyVision VSVDB"},	\
			      { 9, "BT.2020 (RGB)"},		\
			      {10, "sRGB"},			\
			      {11, "scRGB"},			\
			      {12, "scRGBfixed"},		\
			      {13, "AdobeRGB"},			\
			      {14, "DCI-P3 (D65)"},		\
			      {15, "DCI-P3 (Theater)"},		\
			      {16, "Default RGB"},		\
			      {17, "unexpected"})

#define show_range(range)				\
	__print_symbolic(range, { 0, "Full"},		\
				{ 1, "Limited"},	\
				{ 2, "unexpected"})

TRACE_EVENT(iomfb_color_mode,
	    TP_PROTO(struct apple_dcp *dcp, u32 id, u32 score, u32 depth,
		     u32 colorimetry, u32 eotf, u32 range, u32 pixel_enc),
	    TP_ARGS(dcp, id, score, depth, colorimetry, eotf, range, pixel_enc),
	    TP_STRUCT__entry(
			     __field(u64, dcp)
			     __field(u32, id)
			     __field(u32, score)
			     __field(u32, depth)
			     __field(u32, colorimetry)
			     __field(u32, eotf)
			     __field(u32, range)
			     __field(u32, pixel_enc)
	    ),
	    TP_fast_assign(
			   __entry->dcp = (u64)dcp;
			   __entry->id = id;
			   __entry->score = score;
			   __entry->depth = depth;
			   __entry->colorimetry = min_t(u32, colorimetry, 17U);
			   __entry->eotf = min_t(u32, eotf, 4U);
			   __entry->range = min_t(u32, range, 2U);
			   __entry->pixel_enc = min_t(u32, pixel_enc, 15U);
	    ),
	    TP_printk("dcp=%llx, id=%u, score=%u,  depth=%u, colorimetry=%s, eotf=%s, range=%s, pixel_enc=%s",
		      __entry->dcp,
		      __entry->id,
		      __entry->score,
		      __entry->depth,
		      show_colorimetry(__entry->colorimetry),
		      show_eotf(__entry->eotf),
		      show_range(__entry->range),
		      show_encoding(__entry->pixel_enc)
	    )
);

TRACE_EVENT(iomfb_timing_mode,
	    TP_PROTO(struct apple_dcp *dcp, u32 id, u32 score, u32 width,
		     u32 height, u32 clock, u32 color_mode),
	    TP_ARGS(dcp, id, score, width, height, clock, color_mode),
	    TP_STRUCT__entry(
			     __field(u64, dcp)
			     __field(u32, id)
			     __field(u32, score)
			     __field(u32, width)
			     __field(u32, height)
			     __field(u32, clock)
			     __field(u32, color_mode)
	    ),
	    TP_fast_assign(
			   __entry->dcp = (u64)dcp;
			   __entry->id = id;
			   __entry->score = score;
			   __entry->width = width;
			   __entry->height = height;
			   __entry->clock = clock;
			   __entry->color_mode = color_mode;
	    ),
	    TP_printk("dcp=%llx, id=%u, score=%u,  %ux%u@%u.%u, color_mode=%u",
		      __entry->dcp,
		      __entry->id,
		      __entry->score,
		      __entry->width,
		      __entry->height,
		      __entry->clock >> 16,
		      ((__entry->clock & 0xffff) * 1000) >> 16,
		      __entry->color_mode
	    )
);

TRACE_EVENT(avep_sound_mode,
	    TP_PROTO(struct apple_dcp *dcp, u32 rates, u64 formats, unsigned int nchans),
	    TP_ARGS(dcp, rates, formats, nchans),
	    TP_STRUCT__entry(
			     __field(u64, dcp)
			     __field(u32, rates)
			     __field(u64, formats)
			     __field(unsigned int, nchans)
	    ),
	    TP_fast_assign(
			   __entry->dcp = (u64)dcp;
			   __entry->rates = rates;
			   __entry->formats = formats;
			   __entry->nchans = nchans;
	    ),
	    TP_printk("dcp=%llx, rates=%#x, formats=%#llx, nchans=%#x",
		      __entry->dcp,
		      __entry->rates,
		      __entry->formats,
		      __entry->nchans
	    )
);

#endif /* _TRACE_DCP_H */

/* This part must be outside protection */

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

#include <trace/define_trace.h>
