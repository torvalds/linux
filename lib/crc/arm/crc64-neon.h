// SPDX-License-Identifier: GPL-2.0-only

static inline uint64x2_t pmull64(uint64x2_t a, uint64x2_t b)
{
	uint64_t l = vgetq_lane_u64(a, 0);
	uint64_t m = vgetq_lane_u64(b, 0);
	uint64x2_t result;

	asm("vmull.p64	%q0, %P1, %P2" : "=w"(result) : "w"(l), "w"(m));

	return result;
}

static inline uint64x2_t pmull64_high(uint64x2_t a, uint64x2_t b)
{
	uint64_t l = vgetq_lane_u64(a, 1);
	uint64_t m = vgetq_lane_u64(b, 1);
	uint64x2_t result;

	asm("vmull.p64	%q0, %P1, %P2" : "=w"(result) : "w"(l), "w"(m));

	return result;
}

static inline uint64x2_t pmull64_hi_lo(uint64x2_t a, uint64x2_t b)
{
	uint64_t l = vgetq_lane_u64(a, 1);
	uint64_t m = vgetq_lane_u64(b, 0);
	uint64x2_t result;

	asm("vmull.p64	%q0, %P1, %P2" : "=w"(result) : "w"(l), "w"(m));

	return result;
}
