/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM fips140
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_FIPS140_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_FIPS140_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

struct crypto_aes_ctx;

/*
 * These hooks exist only for the benefit of the FIPS140 crypto module, which
 * uses them to swap out the underlying implementation with one that is integrity
 * checked as per FIPS 140 requirements. No other uses are allowed or
 * supported.
 */

DECLARE_HOOK(android_vh_sha256,
	     TP_PROTO(const u8 *data,
		      unsigned int len,
		      u8 *out,
		      int *hook_inuse),
	     TP_ARGS(data, len, out, hook_inuse));

DECLARE_HOOK(android_vh_aes_expandkey,
	     TP_PROTO(struct crypto_aes_ctx *ctx,
		      const u8 *in_key,
		      unsigned int key_len,
		      int *err),
	     TP_ARGS(ctx, in_key, key_len, err));

DECLARE_HOOK(android_vh_aes_encrypt,
	     TP_PROTO(const struct crypto_aes_ctx *ctx,
		      u8 *out,
		      const u8 *in,
		      int *hook_inuse),
	     TP_ARGS(ctx, out, in, hook_inuse));

DECLARE_HOOK(android_vh_aes_decrypt,
	     TP_PROTO(const struct crypto_aes_ctx *ctx,
		      u8 *out,
		      const u8 *in,
		      int *hook_inuse),
	     TP_ARGS(ctx, out, in, hook_inuse));

#endif /* _TRACE_HOOK_FIPS140_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
