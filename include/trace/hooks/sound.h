/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM sound
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_SOUND_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_SOUND_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>
#include <linux/usb.h>

DECLARE_HOOK(android_vh_sound_usb_support_cpu_suspend,
	TP_PROTO(struct usb_device *udev,
		int direction,
		bool *is_support),
	TP_ARGS(udev, direction, is_support));

DECLARE_HOOK(android_vh_snd_soc_card_get_comp_chain,
	TP_PROTO(bool *component_chaining),
	TP_ARGS(component_chaining));

#endif /* _TRACE_HOOK_SOUND_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
