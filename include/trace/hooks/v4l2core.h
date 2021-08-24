/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM v4l2core

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_V4L2CORE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_V4L2CORE_H

#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

struct v4l2_format;
DECLARE_HOOK(android_vh_clear_reserved_fmt_fields,
	TP_PROTO(struct v4l2_format *fmt, int *ret),
	TP_ARGS(fmt, ret));

struct v4l2_fmtdesc;
DECLARE_HOOK(android_vh_fill_ext_fmtdesc,
	TP_PROTO(struct v4l2_fmtdesc *fmtd, const char **descr),
	TP_ARGS(fmtd, descr));

DECLARE_HOOK(android_vh_clear_mask_adjust,
	TP_PROTO(unsigned int ctrl, int *n),
	TP_ARGS(ctrl, n));

struct v4l2_subdev;
struct v4l2_subdev_pad_config;
struct v4l2_subdev_selection;
DECLARE_HOOK(android_vh_v4l2subdev_set_selection,
	TP_PROTO(struct v4l2_subdev *sd, struct v4l2_subdev_pad_config *pad,
	struct v4l2_subdev_selection *sel, int *ret),
	TP_ARGS(sd, pad, sel, ret));

struct v4l2_subdev_format;
DECLARE_HOOK(android_vh_v4l2subdev_set_fmt,
	TP_PROTO(struct v4l2_subdev *sd, struct v4l2_subdev_pad_config *pad,
	struct v4l2_subdev_format *format, int *ret),
	TP_ARGS(sd, pad, format, ret));

struct v4l2_subdev_frame_interval;
DECLARE_HOOK(android_vh_v4l2subdev_set_frame_interval,
	TP_PROTO(struct v4l2_subdev *sd, struct v4l2_subdev_frame_interval *fi,
	int *ret),
	TP_ARGS(sd, fi, ret));

DECLARE_RESTRICTED_HOOK(android_rvh_v4l2subdev_set_selection,
	TP_PROTO(struct v4l2_subdev *sd, struct v4l2_subdev_pad_config *pad,
	struct v4l2_subdev_selection *sel, int *ret),
	TP_ARGS(sd, pad, sel, ret), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_v4l2subdev_set_fmt,
	TP_PROTO(struct v4l2_subdev *sd, struct v4l2_subdev_pad_config *pad,
	struct v4l2_subdev_format *format, int *ret),
	TP_ARGS(sd, pad, format, ret), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_v4l2subdev_set_frame_interval,
	TP_PROTO(struct v4l2_subdev *sd, struct v4l2_subdev_frame_interval *fi,
	int *ret),
	TP_ARGS(sd, fi, ret), 1);

#endif /* _TRACE_HOOK_V4L2CORE_H */
/* This part must be outside protection */
#include <trace/define_trace.h>

