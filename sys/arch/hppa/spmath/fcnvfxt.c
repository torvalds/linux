/*	$OpenBSD: fcnvfxt.c,v 1.10 2025/06/28 13:24:21 miod Exp $	*/
/*
  (c) Copyright 1986 HEWLETT-PACKARD COMPANY
  To anyone who acknowledges that this file is provided "AS IS"
  without any express or implied warranty:
      permission to use, copy, modify, and distribute this file
  for any purpose is hereby granted without fee, provided that
  the above copyright notice and this notice appears in all
  copies, and that the name of Hewlett-Packard Company not be
  used in advertising or publicity pertaining to distribution
  of the software without specific, written prior permission.
  Hewlett-Packard Company makes no representations about the
  suitability of this software for any purpose.
*/
/* @(#)fcnvfxt.c: Revision: 2.8.88.2 Date: 93/12/08 13:27:34 */

#include "float.h"
#include "sgl_float.h"
#include "dbl_float.h"
#include "cnv_float.h"

/*
 *  Convert single floating-point to single fixed-point format
 *  with truncated result
 */
int
sgl_to_sgl_fcnvfxt(sgl_floating_point *srcptr, sgl_floating_point *null,
    int *dstptr, unsigned int *status)
{
	register unsigned int src, temp;
	register int src_exponent, result;

	src = *srcptr;
	src_exponent = Sgl_exponent(src) - SGL_BIAS;

	/*
	 * Test for overflow
	 */
	if (src_exponent > SGL_FX_MAX_EXP) {
		/* check for MININT */
		if ((src_exponent > SGL_FX_MAX_EXP + 1) ||
		Sgl_isnotzero_mantissa(src) || Sgl_iszero_sign(src)) {
			if (Sgl_iszero_sign(src)) result = 0x7fffffff;
			else result = 0x80000000;

			if (Is_invalidtrap_enabled()) {
				return(INVALIDEXCEPTION);
			}
			Set_invalidflag();
			*dstptr = result;
			return(NOEXCEPTION);
		}
	}
	/*
	 * Generate result
	 */
	if (src_exponent >= 0) {
		temp = src;
		Sgl_clear_signexponent_set_hidden(temp);
		Int_from_sgl_mantissa(temp,src_exponent);
		if (Sgl_isone_sign(src))  result = -Sgl_all(temp);
		else result = Sgl_all(temp);
		*dstptr = result;

		/* check for inexact */
		if (Sgl_isinexact_to_fix(src,src_exponent)) {
			if (Is_inexacttrap_enabled()) return(INEXACTEXCEPTION);
			else Set_inexactflag();
		}
	}
	else {
		*dstptr = 0;

		/* check for inexact */
		if (Sgl_isnotzero_exponentmantissa(src)) {
			if (Is_inexacttrap_enabled()) return(INEXACTEXCEPTION);
			else Set_inexactflag();
		}
	}
	return(NOEXCEPTION);
}

/*
 *  Single Floating-point to Double Fixed-point
 */
int
sgl_to_dbl_fcnvfxt(sgl_floating_point *srcptr, sgl_floating_point *null,
    dbl_integer *dstptr, unsigned int *status)
{
	register int src_exponent, resultp1;
	register unsigned int src, temp, resultp2;

	src = *srcptr;
	src_exponent = Sgl_exponent(src) - SGL_BIAS;

	/*
	 * Test for overflow
	 */
	if (src_exponent > DBL_FX_MAX_EXP) {
		/* check for MININT */
		if ((src_exponent > DBL_FX_MAX_EXP + 1) ||
		Sgl_isnotzero_mantissa(src) || Sgl_iszero_sign(src)) {
			if (Sgl_iszero_sign(src)) {
				resultp1 = 0x7fffffff;
				resultp2 = 0xffffffff;
			}
			else {
				resultp1 = 0x80000000;
				resultp2 = 0;
			}

			if (Is_invalidtrap_enabled()) {
				return(INVALIDEXCEPTION);
			}
			Set_invalidflag();
			Dint_copytoptr(resultp1,resultp2,dstptr);
			return(NOEXCEPTION);
		}
		Dint_set_minint(resultp1,resultp2);
		Dint_copytoptr(resultp1,resultp2,dstptr);
		return(NOEXCEPTION);
	}
	/*
	 * Generate result
	 */
	if (src_exponent >= 0) {
		temp = src;
		Sgl_clear_signexponent_set_hidden(temp);
		Dint_from_sgl_mantissa(temp,src_exponent,resultp1,resultp2);
		if (Sgl_isone_sign(src)) {
			Dint_setone_sign(resultp1,resultp2);
		}
		Dint_copytoptr(resultp1,resultp2,dstptr);

		/* check for inexact */
		if (Sgl_isinexact_to_fix(src,src_exponent)) {
			if (Is_inexacttrap_enabled()) return(INEXACTEXCEPTION);
			else Set_inexactflag();
		}
	}
	else {
		Dint_setzero(resultp1,resultp2);
		Dint_copytoptr(resultp1,resultp2,dstptr);

		/* check for inexact */
		if (Sgl_isnotzero_exponentmantissa(src)) {
			if (Is_inexacttrap_enabled()) return(INEXACTEXCEPTION);
			else Set_inexactflag();
		}
	}
	return(NOEXCEPTION);
}

/*
 *  Double Floating-point to Single Fixed-point
 */
int
dbl_to_sgl_fcnvfxt(dbl_floating_point *srcptr, dbl_floating_point *null,
    int *dstptr, unsigned int *status)
{
	register unsigned int srcp1, srcp2, tempp1, tempp2;
	register int src_exponent, result;

	Dbl_copyfromptr(srcptr,srcp1,srcp2);
	src_exponent = Dbl_exponent(srcp1) - DBL_BIAS;

	/*
	 * Test for overflow
	 */
	if (src_exponent > SGL_FX_MAX_EXP) {
		/* check for MININT */
		if (Dbl_isoverflow_to_int(src_exponent,srcp1,srcp2)) {
			if (Dbl_iszero_sign(srcp1)) result = 0x7fffffff;
			else result = 0x80000000;

			if (Is_invalidtrap_enabled()) {
				return(INVALIDEXCEPTION);
			}
			Set_invalidflag();
			*dstptr = result;
			return(NOEXCEPTION);
		}
	}
	/*
	 * Generate result
	 */
	if (src_exponent >= 0) {
		tempp1 = srcp1;
		tempp2 = srcp2;
		Dbl_clear_signexponent_set_hidden(tempp1);
		Int_from_dbl_mantissa(tempp1,tempp2,src_exponent);
		if (Dbl_isone_sign(srcp1) && (src_exponent <= SGL_FX_MAX_EXP))
			result = -Dbl_allp1(tempp1);
		else result = Dbl_allp1(tempp1);
		*dstptr = result;

		/* check for inexact */
		if (Dbl_isinexact_to_fix(srcp1,srcp2,src_exponent)) {
			if (Is_inexacttrap_enabled()) return(INEXACTEXCEPTION);
			else Set_inexactflag();
		}
	}
	else {
		*dstptr = 0;

		/* check for inexact */
		if (Dbl_isnotzero_exponentmantissa(srcp1,srcp2)) {
			if (Is_inexacttrap_enabled()) return(INEXACTEXCEPTION);
			else Set_inexactflag();
		}
	}
	return(NOEXCEPTION);
}

/*
 *  Double Floating-point to Double Fixed-point
 */
int
dbl_to_dbl_fcnvfxt(dbl_floating_point *srcptr, dbl_floating_point *null,
    dbl_integer *dstptr, unsigned int *status)
{
	register int src_exponent, resultp1;
	register unsigned int srcp1, srcp2, tempp1, tempp2, resultp2;

	Dbl_copyfromptr(srcptr,srcp1,srcp2);
	src_exponent = Dbl_exponent(srcp1) - DBL_BIAS;

	/*
	 * Test for overflow
	 */
	if (src_exponent > DBL_FX_MAX_EXP) {
		/* check for MININT */
		if ((src_exponent > DBL_FX_MAX_EXP + 1) ||
		Dbl_isnotzero_mantissa(srcp1,srcp2) || Dbl_iszero_sign(srcp1)) {
			if (Dbl_iszero_sign(srcp1)) {
				resultp1 = 0x7fffffff;
				resultp2 = 0xffffffff;
			}
			else {
				resultp1 = 0x80000000;
				resultp2 = 0;
			}

			if (Is_invalidtrap_enabled()) {
				return(INVALIDEXCEPTION);
			}
			Set_invalidflag();
			Dint_copytoptr(resultp1,resultp2,dstptr);
			return(NOEXCEPTION);
		}
	}
	/*
	 * Generate result
	 */
	if (src_exponent >= 0) {
		tempp1 = srcp1;
		tempp2 = srcp2;
		Dbl_clear_signexponent_set_hidden(tempp1);
		Dint_from_dbl_mantissa(tempp1,tempp2,src_exponent,
		resultp1,resultp2);
		if (Dbl_isone_sign(srcp1)) {
			Dint_setone_sign(resultp1,resultp2);
		}
		Dint_copytoptr(resultp1,resultp2,dstptr);

		/* check for inexact */
		if (Dbl_isinexact_to_fix(srcp1,srcp2,src_exponent)) {
			if (Is_inexacttrap_enabled()) return(INEXACTEXCEPTION);
			else Set_inexactflag();
		}
	}
	else {
		Dint_setzero(resultp1,resultp2);
		Dint_copytoptr(resultp1,resultp2,dstptr);

		/* check for inexact */
		if (Dbl_isnotzero_exponentmantissa(srcp1,srcp2)) {
			if (Is_inexacttrap_enabled()) return(INEXACTEXCEPTION);
			else Set_inexactflag();
		}
	}
	return(NOEXCEPTION);
}
