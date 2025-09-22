/*	$OpenBSD: fpu.h,v 1.2 2024/05/22 14:25:47 jsg Exp $	*/

/*
 * Copyright (c) 2014 Miodrag Vallat.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Internal defines for the floating-point completion code.
 */

/*
 * Data width (matching the TD field of the instructions)
 */
#define	FTYPE_SNG	0
#define	FTYPE_DBL	1
#define	FTYPE_EXT	2
#define	FTYPE_INT	3	/* not a real T value */

#define	IGNORE_PRECISION	FTYPE_SNG

/* floating point value */
typedef union {
	float32		sng;
	float64		dbl;
} fparg;

void	fpu_compare(struct trapframe *, fparg *, fparg *, u_int, u_int, u_int);
u_int	fpu_precision(u_int, u_int, u_int);
void	fpu_store(struct trapframe *, u_int, u_int, u_int, fparg *);
