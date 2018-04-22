// SPDX-License-Identifier: GPL-2.0
/*
 * Generic Reed Solomon encoder / decoder library
 *
 * Copyright (C) 2004 Thomas Gleixner (tglx@linutronix.de)
 *
 * RS code lifted from reed solomon library written by Phil Karn
 * Copyright 2002 Phil Karn, KA9Q
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _RSLIB_H_
#define _RSLIB_H_

#include <linux/list.h>
#include <linux/types.h>	/* for gfp_t */
#include <linux/gfp.h>		/* for GFP_KERNEL */

/**
 * struct rs_control - rs control structure
 *
 * @mm:		Bits per symbol
 * @nn:		Symbols per block (= (1<<mm)-1)
 * @alpha_to:	log lookup table
 * @index_of:	Antilog lookup table
 * @genpoly:	Generator polynomial
 * @nroots:	Number of generator roots = number of parity symbols
 * @fcr:	First consecutive root, index form
 * @prim:	Primitive element, index form
 * @iprim:	prim-th root of 1, index form
 * @gfpoly:	The primitive generator polynominal
 * @gffunc:	Function to generate the field, if non-canonical representation
 * @users:	Users of this structure
 * @list:	List entry for the rs control list
*/
struct rs_control {
	int		mm;
	int		nn;
	uint16_t	*alpha_to;
	uint16_t	*index_of;
	uint16_t	*genpoly;
	int		nroots;
	int		fcr;
	int		prim;
	int		iprim;
	int		gfpoly;
	int		(*gffunc)(int);
	int		users;
	struct list_head list;
};

/* General purpose RS codec, 8-bit data width, symbol width 1-15 bit  */
#ifdef CONFIG_REED_SOLOMON_ENC8
int encode_rs8(struct rs_control *rs, uint8_t *data, int len, uint16_t *par,
	       uint16_t invmsk);
#endif
#ifdef CONFIG_REED_SOLOMON_DEC8
int decode_rs8(struct rs_control *rs, uint8_t *data, uint16_t *par, int len,
		uint16_t *s, int no_eras, int *eras_pos, uint16_t invmsk,
	       uint16_t *corr);
#endif

/* General purpose RS codec, 16-bit data width, symbol width 1-15 bit  */
#ifdef CONFIG_REED_SOLOMON_ENC16
int encode_rs16(struct rs_control *rs, uint16_t *data, int len, uint16_t *par,
		uint16_t invmsk);
#endif
#ifdef CONFIG_REED_SOLOMON_DEC16
int decode_rs16(struct rs_control *rs, uint16_t *data, uint16_t *par, int len,
		uint16_t *s, int no_eras, int *eras_pos, uint16_t invmsk,
		uint16_t *corr);
#endif

/* Create or get a matching rs control structure */
struct rs_control *init_rs_gfp(int symsize, int gfpoly, int fcr, int prim,
			       int nroots, gfp_t gfp);

/**
 * init_rs - Create a RS control struct and initialize it
 *  @symsize:	the symbol size (number of bits)
 *  @gfpoly:	the extended Galois field generator polynomial coefficients,
 *		with the 0th coefficient in the low order bit. The polynomial
 *		must be primitive;
 *  @fcr:	the first consecutive root of the rs code generator polynomial
 *		in index form
 *  @prim:	primitive element to generate polynomial roots
 *  @nroots:	RS code generator polynomial degree (number of roots)
 *
 * Allocations use GFP_KERNEL.
 */
static inline struct rs_control *init_rs(int symsize, int gfpoly, int fcr,
					 int prim, int nroots)
{
	return init_rs_gfp(symsize, gfpoly, fcr, prim, nroots, GFP_KERNEL);
}

struct rs_control *init_rs_non_canonical(int symsize, int (*func)(int),
					 int fcr, int prim, int nroots);

/* Release a rs control structure */
void free_rs(struct rs_control *rs);

/** modulo replacement for galois field arithmetics
 *
 *  @rs:	the rs control structure
 *  @x:		the value to reduce
 *
 *  where
 *  rs->mm = number of bits per symbol
 *  rs->nn = (2^rs->mm) - 1
 *
 *  Simple arithmetic modulo would return a wrong result for values
 *  >= 3 * rs->nn
*/
static inline int rs_modnn(struct rs_control *rs, int x)
{
	while (x >= rs->nn) {
		x -= rs->nn;
		x = (x >> rs->mm) + (x & rs->nn);
	}
	return x;
}

#endif
