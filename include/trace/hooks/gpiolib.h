/* SPDX-License-Identifier: GPL-2.0-only */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM gpiolib

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_GPIOLIB_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_GPIOLIB_H

#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>
#include "../drivers/gpio/gpiolib.h"

#if defined(CONFIG_TRACEPOINTS) && defined(CONFIG_ANDROID_VENDOR_HOOKS)
DECLARE_HOOK(android_vh_gpio_block_read,
	TP_PROTO(struct gpio_device *gdev, bool *block_gpio_read),
	TP_ARGS(gdev, block_gpio_read));
#else
#define trace_android_vh_gpio_block_read(gdev, block_gpio_read)
#endif

#endif /* _TRACE_HOOK_GPIOLIB_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
