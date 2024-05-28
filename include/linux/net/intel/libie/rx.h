/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2024 Intel Corporation */

#ifndef __LIBIE_RX_H
#define __LIBIE_RX_H

#include <net/libeth/rx.h>

/* Rx buffer management */

/* The largest size for a single descriptor as per HW */
#define LIBIE_MAX_RX_BUF_LEN	9728U
/* "True" HW-writeable space: minimum from SW and HW values */
#define LIBIE_RX_BUF_LEN(hr)	min_t(u32, LIBETH_RX_PAGE_LEN(hr),	\
				      LIBIE_MAX_RX_BUF_LEN)

/* The maximum frame size as per HW (S/G) */
#define __LIBIE_MAX_RX_FRM_LEN	16382U
/* ATST, HW can chain up to 5 Rx descriptors */
#define LIBIE_MAX_RX_FRM_LEN(hr)					\
	min_t(u32, __LIBIE_MAX_RX_FRM_LEN, LIBIE_RX_BUF_LEN(hr) * 5)
/* Maximum frame size minus LL overhead */
#define LIBIE_MAX_MTU							\
	(LIBIE_MAX_RX_FRM_LEN(LIBETH_MAX_HEADROOM) - LIBETH_RX_LL_LEN)

/* O(1) converting i40e/ice/iavf's 8/10-bit hardware packet type to a parsed
 * bitfield struct.
 */

#define LIBIE_RX_PT_NUM		154

extern const struct libeth_rx_pt libie_rx_pt_lut[LIBIE_RX_PT_NUM];

/**
 * libie_rx_pt_parse - convert HW packet type to software bitfield structure
 * @pt: 10-bit hardware packet type value from the descriptor
 *
 * ```libie_rx_pt_lut``` must be accessed only using this wrapper.
 *
 * Return: parsed bitfield struct corresponding to the provided ptype.
 */
static inline struct libeth_rx_pt libie_rx_pt_parse(u32 pt)
{
	if (unlikely(pt >= LIBIE_RX_PT_NUM))
		pt = 0;

	return libie_rx_pt_lut[pt];
}

#endif /* __LIBIE_RX_H */
