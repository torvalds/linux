/*	$OpenBSD: crypto_cpu_caps.c,v 1.8 2025/08/14 15:11:01 jsing Exp $ */
/*
 * Copyright (c) 2024 Joel Sing <jsing@openbsd.org>
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

#include <stdio.h>

#include <openssl/crypto.h>

#include "crypto_arch.h"
#include "x86_arch.h"

/* Legacy architecture specific capabilities, used by perlasm. */
uint64_t OPENSSL_ia32cap_P;

/* Machine dependent CPU capabilities. */
uint64_t crypto_cpu_caps_amd64;

/* Machine independent CPU capabilities. */
extern uint64_t crypto_cpu_caps;

static inline void
cpuid(uint32_t eax, uint32_t *out_eax, uint32_t *out_ebx, uint32_t *out_ecx,
    uint32_t *out_edx)
{
	uint32_t ebx = 0, ecx = 0, edx = 0;

#ifndef OPENSSL_NO_ASM
	__asm__ ("cpuid": "+a"(eax), "+b"(ebx), "+c"(ecx), "+d"(edx));
#else
	eax = 0;
#endif

	if (out_eax != NULL)
		*out_eax = eax;
	if (out_ebx != NULL)
		*out_ebx = ebx;
	if (out_ecx != NULL)
		*out_ecx = ecx;
	if (out_edx != NULL)
		*out_edx = edx;
}

static inline void
xgetbv(uint32_t ecx, uint32_t *out_eax, uint32_t *out_edx)
{
	uint32_t eax = 0, edx = 0;

#ifndef OPENSSL_NO_ASM
	__asm__ ("xgetbv": "+a"(eax), "+c"(ecx), "+d"(edx));
#endif

	if (out_eax != NULL)
		*out_eax = eax;
	if (out_edx != NULL)
		*out_edx = edx;
}

void
crypto_cpu_caps_init(void)
{
	uint32_t eax, ebx, ecx, edx, max_cpuid;
	uint64_t caps = 0;

	cpuid(0, &eax, &ebx, &ecx, &edx);

	max_cpuid = eax;

	/* "GenuineIntel" in little endian. */
	if (ebx == 0x756e6547 && edx == 0x49656e69 && ecx == 0x6c65746e)
		caps |= CPUCAP_MASK_INTEL;

	if (max_cpuid < 1)
		return;

	cpuid(1, &eax, NULL, &ecx, &edx);

	if ((edx & IA32CAP_MASK0_FXSR) != 0)
		caps |= CPUCAP_MASK_FXSR;
	if ((edx & IA32CAP_MASK0_HT) != 0)
		caps |= CPUCAP_MASK_HT;
	if ((edx & IA32CAP_MASK0_MMX) != 0)
		caps |= CPUCAP_MASK_MMX;
	if ((edx & IA32CAP_MASK0_SSE) != 0)
		caps |= CPUCAP_MASK_SSE;
	if ((edx & IA32CAP_MASK0_SSE2) != 0)
		caps |= CPUCAP_MASK_SSE2;

	if ((ecx & IA32CAP_MASK1_AESNI) != 0) {
		caps |= CPUCAP_MASK_AESNI;
		crypto_cpu_caps_amd64 |= CRYPTO_CPU_CAPS_AMD64_AES;
	}
	if ((ecx & IA32CAP_MASK1_PCLMUL) != 0) {
		caps |= CPUCAP_MASK_PCLMUL;
		crypto_cpu_caps_amd64 |= CRYPTO_CPU_CAPS_AMD64_CLMUL;
	}
	if ((ecx & IA32CAP_MASK1_SSSE3) != 0)
		caps |= CPUCAP_MASK_SSSE3;

	/* AVX requires OSXSAVE and XMM/YMM state to be enabled. */
	if ((ecx & IA32CAP_MASK1_OSXSAVE) != 0) {
		xgetbv(0, &eax, NULL);
		if (((eax >> 1) & 3) == 3 && (ecx & IA32CAP_MASK1_AVX) != 0)
			caps |= CPUCAP_MASK_AVX;
	}

	if (max_cpuid >= 7) {
		cpuid(7, NULL, &ebx, NULL, NULL);

		/* Intel ADX feature bit - ebx[19]. */
		if (((ebx >> 19) & 1) != 0)
			crypto_cpu_caps_amd64 |= CRYPTO_CPU_CAPS_AMD64_ADX;

		/* Intel SHA extensions feature bit - ebx[29]. */
		if (((ebx >> 29) & 1) != 0)
			crypto_cpu_caps_amd64 |= CRYPTO_CPU_CAPS_AMD64_SHA;
	}

	/* Set machine independent CPU capabilities. */
	if ((caps & CPUCAP_MASK_AESNI) != 0)
		crypto_cpu_caps |= CRYPTO_CPU_CAPS_ACCELERATED_AES;

	OPENSSL_ia32cap_P = caps;
}
