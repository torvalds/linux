#ifndef _TFRC_H_
#define _TFRC_H_
/*
 *  net/dccp/ccids/lib/tfrc.h
 *
 *  Copyright (c) 2005 The University of Waikato, Hamilton, New Zealand.
 *  Copyright (c) 2005 Ian McDonald <iam4@cs.waikato.ac.nz>
 *  Copyright (c) 2005 Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *  Copyright (c) 2003 Nils-Erik Mattsson, Joacim Haggmark, Magnus Erixzon
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#include <linux/types.h>

extern u32 tfrc_calc_x(u16 s, u32 R, u32 p);
extern u32 tfrc_calc_x_reverse_lookup(u32 fvalue);

#endif /* _TFRC_H_ */
