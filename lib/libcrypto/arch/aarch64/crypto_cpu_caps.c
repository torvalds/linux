/* $OpenBSD: crypto_cpu_caps.c,v 1.2 2024/11/12 13:52:31 jsing Exp $ */
/*
 * Copyright (c) 2023 Joel Sing <jsing@openbsd.org>
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

#include <sys/types.h>
#include <sys/sysctl.h>

#include <machine/cpu.h>

#include <stddef.h>
#include <stdio.h>

#include "crypto_arch.h"

/* Machine dependent CPU capabilities. */
uint64_t crypto_cpu_caps_aarch64;

static inline uint64_t
extract_bits(uint64_t val, int start, int end)
{
	return (val >> end) & (1ULL << (1 + start - end)) - 1;
}

static uint64_t
parse_isar0(uint64_t isar0)
{
	uint64_t caps = 0;
	uint64_t feature;

	/* AES - bits [7:4] */
	feature = extract_bits(isar0, 7, 4);
	if (feature >= 1)
		caps |= CRYPTO_CPU_CAPS_AARCH64_AES;
	if (feature >= 2)
		caps |= CRYPTO_CPU_CAPS_AARCH64_PMULL;

	/* SHA1 - bits [11:8] */
	feature = extract_bits(isar0, 11, 8);
	if (feature >= 1)
		caps |= CRYPTO_CPU_CAPS_AARCH64_SHA1;

	/* SHA2 - bits [15:12] */
	feature = extract_bits(isar0, 15, 12);
	if (feature >= 1)
		caps |= CRYPTO_CPU_CAPS_AARCH64_SHA2;
	if (feature >= 2)
		caps |= CRYPTO_CPU_CAPS_AARCH64_SHA512;

	/* SHA3 - bits [35:32] */
	feature = extract_bits(isar0, 35, 32);
	if (feature >= 1)
		caps |= CRYPTO_CPU_CAPS_AARCH64_SHA3;

	return caps;
}

static int
read_isar0(uint64_t *isar0)
{
	uint64_t isar;
	int mib[2];
	size_t len;

	mib[0] = CTL_MACHDEP;
	mib[1] = CPU_ID_AA64ISAR0;
	len = sizeof(isar);
	if (sysctl(mib, 2, &isar, &len, NULL, 0) == -1)
		return 0;

	*isar0 = isar;

	return 1;
}

void
crypto_cpu_caps_init(void)
{
	uint64_t isar = 0;

	if (!read_isar0(&isar))
		return;

	crypto_cpu_caps_aarch64 = parse_isar0(isar);
}
