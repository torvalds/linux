/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM fips140
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_FIPS140_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_FIPS140_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

/*
 * This hook exists only for the benefit of the FIPS140 crypto module, which
 * uses it to swap out the underlying implementation with one that is integrity
 * checked as per FIPS 140 requirements. No other uses are allowed or
 * supported.
 */
DECLARE_HOOK(android_vh_sha256,
	     TP_PROTO(const u8 *data,
		      unsigned int len,
		      u8 *out,
		      int *hook_inuse),
	     TP_ARGS(data, len, out, hook_inuse));

#endif /* _TRACE_HOOK_FIPS140_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
