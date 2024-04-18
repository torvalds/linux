/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2024 Intel Corporation */

#ifndef __LIBIE_RX_H
#define __LIBIE_RX_H

#include <net/libeth/rx.h>

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
