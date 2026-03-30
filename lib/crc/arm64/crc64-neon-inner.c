// SPDX-License-Identifier: GPL-2.0-only
/*
 * Accelerated CRC64 (NVMe) using ARM NEON C intrinsics
 */

#include <linux/types.h>
#include <asm/neon-intrinsics.h>

u64 crc64_nvme_arm64_c(u64 crc, const u8 *p, size_t len);

/* x^191 mod G, x^127 mod G */
static const u64 fold_consts_val[2] = { 0xeadc41fd2ba3d420ULL,
					0x21e9761e252621acULL };
/* floor(x^127 / G), (G - x^64) / x */
static const u64 bconsts_val[2] = { 0x27ecfa329aef9f77ULL,
				    0x34d926535897936aULL };

static inline uint64x2_t pmull64(uint64x2_t a, uint64x2_t b)
{
	return vreinterpretq_u64_p128(vmull_p64(vgetq_lane_u64(a, 0),
						vgetq_lane_u64(b, 0)));
}

static inline uint64x2_t pmull64_high(uint64x2_t a, uint64x2_t b)
{
	poly64x2_t l = vreinterpretq_p64_u64(a);
	poly64x2_t m = vreinterpretq_p64_u64(b);

	return vreinterpretq_u64_p128(vmull_high_p64(l, m));
}

static inline uint64x2_t pmull64_hi_lo(uint64x2_t a, uint64x2_t b)
{
	return vreinterpretq_u64_p128(vmull_p64(vgetq_lane_u64(a, 1),
						vgetq_lane_u64(b, 0)));
}

u64 crc64_nvme_arm64_c(u64 crc, const u8 *p, size_t len)
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
