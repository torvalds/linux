/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * CRC64 using ARM64 PMULL instructions
 */

#include <linux/cpufeature.h>
#include <asm/simd.h>
#include <linux/minmax.h>
#include <linux/sizes.h>

u64 crc64_nvme_arm64_c(u64 crc, const u8 *p, size_t len);

#define crc64_be_arch crc64_be_generic

static inline u64 crc64_nvme_arch(u64 crc, const u8 *p, size_t len)
{
	if (len >= 128 && cpu_have_named_feature(PMULL) &&
	    likely(may_use_simd())) {
		do {
			size_t chunk = min_t(size_t, len & ~15, SZ_4K);

			scoped_ksimd()
				crc = crc64_nvme_arm64_c(crc, p, chunk);

			p += chunk;
			len -= chunk;
		} while (len >= 128);
	}
	return crc64_nvme_generic(crc, p, len);
}
