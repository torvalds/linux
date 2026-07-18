/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 2003 H. Peter Anvin - All Rights Reserved
 *
 * Galois field tables for the Linux RAID6 P/Q parity algorithm.
 */
#ifndef _LINUX_RAID_PQ_TABLES_H
#define _LINUX_RAID_PQ_TABLES_H

#include <linux/types.h>

extern const u8 raid6_gfmul[256][256] __attribute__((aligned(256)));
extern const u8 raid6_vgfmul[256][32] __attribute__((aligned(256)));
extern const u8 raid6_gfexp[256]      __attribute__((aligned(256)));
extern const u8 raid6_gflog[256]      __attribute__((aligned(256)));
extern const u8 raid6_gfinv[256]      __attribute__((aligned(256)));
extern const u8 raid6_gfexi[256]      __attribute__((aligned(256)));

#endif /* _LINUX_RAID_PQ_TABLES_H */
