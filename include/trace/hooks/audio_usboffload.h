/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM audio_usboffload

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_AUDIO_USBOFFLOAD_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_AUDIO_USBOFFLOAD_H

#include <trace/hooks/vendor_hooks.h>

struct usb_interface;
struct snd_usb_audio;

DECLARE_HOOK(android_vh_audio_usb_offload_vendor_set,
	TP_PROTO(void *arg),
	TP_ARGS(arg));

DECLARE_HOOK(android_vh_audio_usb_offload_ep_action,
	TP_PROTO(void *arg, bool action),
	TP_ARGS(arg, action));

DECLARE_HOOK(android_vh_audio_usb_offload_synctype,
	TP_PROTO(void *arg, int attr, bool *need_ignore),
	TP_ARGS(arg, attr, need_ignore));

DECLARE_HOOK(android_vh_audio_usb_offload_connect,
	TP_PROTO(struct usb_interface *intf, struct snd_usb_audio *chip),
	TP_ARGS(intf, chip));

DECLARE_RESTRICTED_HOOK(android_rvh_audio_usb_offload_disconnect,
	TP_PROTO(struct usb_interface *intf),
	TP_ARGS(intf), 1);

#endif /* _TRACE_HOOK_AUDIO_USBOFFLOAD_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
