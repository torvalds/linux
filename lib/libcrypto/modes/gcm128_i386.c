/* $OpenBSD: gcm128_i386.c,v 1.1 2025/06/28 12:39:10 jsing Exp $ */
/*
 * Copyright (c) 2025 Joel Sing <jsing@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "crypto_arch.h"
#include "modes_local.h"

void gcm_init_4bit(u128 Htable[16], uint64_t H[2]);

void gcm_gmult_4bit_mmx(uint64_t Xi[2], const u128 Htable[16]);
void gcm_ghash_4bit_mmx(uint64_t Xi[2], const u128 Htable[16], const uint8_t *inp,
    size_t len);

void gcm_gmult_4bit_x86(uint64_t Xi[2], const u128 Htable[16]);
void gcm_ghash_4bit_x86(uint64_t Xi[2], const u128 Htable[16], const uint8_t *inp,
    size_t len);

void gcm_init_clmul(u128 Htable[16], const uint64_t Xi[2]);
void gcm_gmult_clmul(uint64_t Xi[2], const u128 Htable[16]);
void gcm_ghash_clmul(uint64_t Xi[2], const u128 Htable[16], const uint8_t *inp,
    size_t len);

void
gcm128_init(GCM128_CONTEXT *ctx)
{
	if ((crypto_cpu_caps_i386 & CRYPTO_CPU_CAPS_I386_CLMUL) != 0) {
		gcm_init_clmul(ctx->Htable, ctx->H.u);
		ctx->gmult = gcm_gmult_clmul;
		ctx->ghash = gcm_ghash_clmul;
		return;
	}

	if ((crypto_cpu_caps_i386 & CRYPTO_CPU_CAPS_I386_MMX) != 0) {
		gcm_init_4bit(ctx->Htable, ctx->H.u);
		ctx->gmult = gcm_gmult_4bit_mmx;
		ctx->ghash = gcm_ghash_4bit_mmx;
		return;
	}

	gcm_init_4bit(ctx->Htable, ctx->H.u);
	ctx->gmult = gcm_gmult_4bit_x86;
	ctx->ghash = gcm_ghash_4bit_x86;
}
