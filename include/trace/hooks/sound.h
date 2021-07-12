/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM sound
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_SOUND_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_SOUND_H
#include <trace/hooks/vendor_hooks.h>

DECLARE_HOOK(android_vh_sound_usb_support_cpu_suspend,
	TP_PROTO(struct usb_device *udev,
		int direction,
		bool *is_support),
	TP_ARGS(udev, direction, is_support));

#endif /* _TRACE_HOOK_SOUND_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
