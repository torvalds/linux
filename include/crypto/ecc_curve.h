/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2021 HiSilicon */

#ifndef _CRYTO_ECC_CURVE_H
#define _CRYTO_ECC_CURVE_H

#include <linux/types.h>

/**
 * struct ecc_point - elliptic curve point in affine coordinates
 *
 * @x:		X coordinate in vli form.
 * @y:		Y coordinate in vli form.
 * @ndigits:	Length of vlis in u64 qwords.
 */
struct ecc_point {
	u64 *x;
	u64 *y;
	u8 ndigits;
};

/**
 * struct ecc_curve - definition of elliptic curve
 *
 * @name:	Short name of the curve.
 * @g:		Generator point of the curve.
 * @p:		Prime number, if Barrett's reduction is used for this curve
 *		pre-calculated value 'mu' is appended to the @p after ndigits.
 *		Use of Barrett's reduction is heuristically determined in
 *		vli_mmod_fast().
 * @n:		Order of the curve group.
 * @a:		Curve parameter a.
 * @b:		Curve parameter b.
 */
struct ecc_curve {
	char *name;
	struct ecc_point g;
	u64 *p;
	u64 *n;
	u64 *a;
	u64 *b;
};

/**
 * ecc_get_curve() - get elliptic curve;
 * @curve_id:           Curves IDs:
 *                      defined in 'include/crypto/ecdh.h';
 *
 * Returns curve if get curve succssful, NULL otherwise
 */
const struct ecc_curve *ecc_get_curve(unsigned int curve_id);

#endif
