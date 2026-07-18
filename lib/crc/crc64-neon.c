// SPDX-License-Identifier: GPL-2.0-only
/*
 * Accelerated CRC64 (NVMe) using ARM NEON C intrinsics
 */

#include <linux/types.h>
#include <asm/neon-intrinsics.h>

#include "crc64-neon.h"

u64 crc64_nvme_neon(u64 crc, const u8 *p, size_t len);

/* x^191 mod G, x^127 mod G */
static const u64 fold_consts_val[2] = { 0xeadc41fd2ba3d420ULL,
					0x21e9761e252621acULL };
/* floor(x^127 / G), (G - x^64) / x */
static const u64 bconsts_val[2] = { 0x27ecfa329aef9f77ULL,
				    0x34d926535897936aULL };

u64 crc64_nvme_neon(u64 crc, const u8 *p, size_t len)
{
	uint64x2_t fold_consts = vld1q_u64(fold_consts_val);
	uint64x2_t v0 = { crc, 0 };
	uint64x2_t zero = { };

	for (;;) {
		v0 ^= vreinterpretq_u64_u8(vld1q_u8(p));

		p += 16;
		len -= 16;
		if (len < 16)
			break;

		v0 = pmull64(fold_consts, v0) ^ pmull64_high(fold_consts, v0);
	}

	/* Multiply the 128-bit value by x^64 and reduce it back to 128 bits. */
	v0 = vextq_u64(v0, zero, 1) ^ pmull64_hi_lo(fold_consts, v0);

	/* Final Barrett reduction */
	uint64x2_t bconsts = vld1q_u64(bconsts_val);
	uint64x2_t final = pmull64(bconsts, v0);

	v0 ^= vextq_u64(zero, final, 1) ^ pmull64_hi_lo(bconsts, final);

	return vgetq_lane_u64(v0, 1);
}
