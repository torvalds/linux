// SPDX-License-Identifier: GPL-2.0-only OR MIT
/* Copyright The Asahi Linux Contributors */

#include <drm/drm_modes.h>
#include <drm/drm_rect.h>

#include "dcp-internal.h"

struct apple_dcp;

typedef void (*dcp_callback_t)(struct apple_dcp *, void *, void *);


#define DCP_THUNK_VOID(func, handle)                                         \
	static void func(struct apple_dcp *dcp, bool oob, dcp_callback_t cb, \
			 void *cookie)                                       \
	{                                                                    \
		dcp_push(dcp, oob, &dcp_methods[handle], 0, 0, NULL, cb, cookie);          \
	}

#define DCP_THUNK_OUT(func, handle, T)                                       \
	static void func(struct apple_dcp *dcp, bool oob, dcp_callback_t cb, \
			 void *cookie)                                       \
	{                                                                    \
		dcp_push(dcp, oob, &dcp_methods[handle], 0, sizeof(T), NULL, cb, cookie);  \
	}

#define DCP_THUNK_IN(func, handle, T)                                       \
	static void func(struct apple_dcp *dcp, bool oob, T *data,          \
			 dcp_callback_t cb, void *cookie)                   \
	{                                                                   \
		dcp_push(dcp, oob, &dcp_methods[handle], sizeof(T), 0, data, cb, cookie); \
	}

#define DCP_THUNK_INOUT(func, handle, T_in, T_out)                            \
	static void func(struct apple_dcp *dcp, bool oob, T_in *data,         \
			 dcp_callback_t cb, void *cookie)                     \
	{                                                                     \
		dcp_push(dcp, oob, &dcp_methods[handle], sizeof(T_in), sizeof(T_out), data, \
			 cb, cookie);                                         \
	}

#define IOMFB_THUNK_INOUT(name)                                     \
	static void iomfb_ ## name(struct apple_dcp *dcp, bool oob, \
			struct iomfb_ ## name ## _req *data,        \
			dcp_callback_t cb, void *cookie)            \
	{                                                           \
		dcp_push(dcp, oob, &dcp_methods[iomfbep_ ## name],                \
			 sizeof(struct iomfb_ ## name ## _req),     \
			 sizeof(struct iomfb_ ## name ## _resp),    \
			 data,  cb, cookie);                        \
	}

/*
 * Define type-safe trampolines. Define typedefs to enforce type-safety on the
 * input data (so if the types don't match, gcc errors out).
 */

#define TRAMPOLINE_VOID(func, handler)                                        \
	static bool __maybe_unused func(struct apple_dcp *dcp, int tag, void *out, void *in) \
	{                                                                     \
		trace_iomfb_callback(dcp, tag, #handler);                     \
		handler(dcp);                                                 \
		return true;                                                  \
	}

#define TRAMPOLINE_IN(func, handler, T_in)                                    \
	typedef void (*callback_##handler)(struct apple_dcp *, T_in *);       \
                                                                              \
	static bool __maybe_unused func(struct apple_dcp *dcp, int tag, void *out, void *in) \
	{                                                                     \
		callback_##handler cb = handler;                              \
                                                                              \
		trace_iomfb_callback(dcp, tag, #handler);                     \
		cb(dcp, in);                                                  \
		return true;                                                  \
	}

#define TRAMPOLINE_INOUT(func, handler, T_in, T_out)                          \
	typedef T_out (*callback_##handler)(struct apple_dcp *, T_in *);      \
                                                                              \
	static bool __maybe_unused func(struct apple_dcp *dcp, int tag, void *out, void *in) \
	{                                                                     \
		T_out *typed_out = out;                                       \
		callback_##handler cb = handler;                              \
                                                                              \
		trace_iomfb_callback(dcp, tag, #handler);                     \
		*typed_out = cb(dcp, in);                                     \
		return true;                                                  \
	}

#define TRAMPOLINE_OUT(func, handler, T_out)                                  \
	static bool __maybe_unused func(struct apple_dcp *dcp, int tag, void *out, void *in) \
	{                                                                     \
		T_out *typed_out = out;                                       \
                                                                              \
		trace_iomfb_callback(dcp, tag, #handler);                     \
		*typed_out = handler(dcp);                                    \
		return true;                                                  \
	}

/* Call a DCP function given by a tag */
void dcp_push(struct apple_dcp *dcp, bool oob, const struct dcp_method_entry *call,
		     u32 in_len, u32 out_len, void *data, dcp_callback_t cb,
		     void *cookie);

/* Parse a callback tag "D123" into the ID 123. Returns -EINVAL on failure. */
int dcp_parse_tag(char tag[4]);

void dcp_ack(struct apple_dcp *dcp, enum dcp_context_id context);

/*
 * DRM specifies rectangles as start and end coordinates.  DCP specifies
 * rectangles as a start coordinate and a width/height. Convert a DRM rectangle
 * to a DCP rectangle.
 */
struct dcp_rect drm_to_dcp_rect(struct drm_rect *rect);

u32 drm_format_to_dcp(u32 drm);

/* The user may own drm_display_mode, so we need to search for our copy */
struct dcp_display_mode *lookup_mode(struct apple_dcp *dcp,
					    const struct drm_display_mode *mode);
