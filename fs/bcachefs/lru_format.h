/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_LRU_FORMAT_H
#define _BCACHEFS_LRU_FORMAT_H

struct bch_lru {
	struct bch_val		v;
	__le64			idx;
} __packed __aligned(8);

#define BCH_LRU_TYPES()		\
	x(read)			\
	x(fragmentation)

enum bch_lru_type {
#define x(n) BCH_LRU_##n,
	BCH_LRU_TYPES()
#undef x
};

#define BCH_LRU_FRAGMENTATION_START	((1U << 16) - 1)

#define LRU_TIME_BITS			48
#define LRU_TIME_MAX			((1ULL << LRU_TIME_BITS) - 1)

#endif /* _BCACHEFS_LRU_FORMAT_H */
