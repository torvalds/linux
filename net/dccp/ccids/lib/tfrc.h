#ifndef _TFRC_H_
#define _TFRC_H_
/*
 *  net/dccp/ccids/lib/tfrc.h
 *
 *  Copyright (c) 2005 The University of Waikato, Hamilton, New Zealand.
 *  Copyright (c) 2005 Ian McDonald <ian.mcdonald@jandi.co.nz>
 *  Copyright (c) 2005 Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *  Copyright (c) 2003 Nils-Erik Mattsson, Joacim Haggmark, Magnus Erixzon
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */
#include <linux/types.h>
#include <asm/div64.h>

/* integer-arithmetic divisions of type (a * 1000000)/b */
static inline u64 scaled_div(u64 a, u32 b)
{
	BUG_ON(b==0);
	a *= 1000000;
	do_div(a, b);
	return a;
}

static inline u32 scaled_div32(u64 a, u32 b)
{
	u64 result = scaled_div(a, b);

	if (result > UINT_MAX) {
		DCCP_CRIT("Overflow: a(%llu)/b(%u) > ~0U",
			  (unsigned long long)a, b);
		return UINT_MAX;
	}
	return result;
}

extern u32 tfrc_calc_x(u16 s, u32 R, u32 p);
extern u32 tfrc_calc_x_reverse_lookup(u32 fvalue);

#endif /* _TFRC_H_ */
