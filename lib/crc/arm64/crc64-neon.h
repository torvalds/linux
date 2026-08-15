// SPDX-License-Identifier: GPL-2.0-only

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
