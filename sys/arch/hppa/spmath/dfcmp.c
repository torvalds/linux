/*	$OpenBSD: dfcmp.c,v 1.7 2025/06/28 13:24:21 miod Exp $	*/
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
/* @(#)dfcmp.c: Revision: 1.7.88.2 Date: 93/12/08 13:26:38 */

#include "float.h"
#include "dbl_float.h"
    
/*
 * dbl_cmp: compare two values
 */
int
dbl_fcmp(dbl_floating_point *leftptr, dbl_floating_point *rightptr,
    unsigned int cond, unsigned int *status)
    /* cond is the predicate to be tested */
{
    register unsigned int leftp1, leftp2, rightp1, rightp2;
    register int xorresult;

    /* Create local copies of the numbers */
    Dbl_copyfromptr(leftptr,leftp1,leftp2);
    Dbl_copyfromptr(rightptr,rightp1,rightp2);
    /*
     * Test for NaN
     */
    if(    (Dbl_exponent(leftp1) == DBL_INFINITY_EXPONENT)
	|| (Dbl_exponent(rightp1) == DBL_INFINITY_EXPONENT) )
	{
	/* Check if a NaN is involved.  Signal an invalid exception when
	 * comparing a signaling NaN or when comparing quiet NaNs and the
	 * low bit of the condition is set */
	if( ((Dbl_exponent(leftp1) == DBL_INFINITY_EXPONENT)
	    && Dbl_isnotzero_mantissa(leftp1,leftp2)
	    && (Exception(cond) || Dbl_isone_signaling(leftp1)))
	   ||
	    ((Dbl_exponent(rightp1) == DBL_INFINITY_EXPONENT)
	    && Dbl_isnotzero_mantissa(rightp1,rightp2)
	    && (Exception(cond) || Dbl_isone_signaling(rightp1))) )
	    {
	    if( Is_invalidtrap_enabled() ) {
		Set_status_cbit(Unordered(cond));
		return(INVALIDEXCEPTION);
	    }
	    else Set_invalidflag();
	    Set_status_cbit(Unordered(cond));
	    return(NOEXCEPTION);
	    }
	/* All the exceptional conditions are handled, now special case
	   NaN compares */
	else if( ((Dbl_exponent(leftp1) == DBL_INFINITY_EXPONENT)
	    && Dbl_isnotzero_mantissa(leftp1,leftp2))
	   ||
	    ((Dbl_exponent(rightp1) == DBL_INFINITY_EXPONENT)
	    && Dbl_isnotzero_mantissa(rightp1,rightp2)) )
	    {
	    /* NaNs always compare unordered. */
	    Set_status_cbit(Unordered(cond));
	    return(NOEXCEPTION);
	    }
	/* infinities will drop down to the normal compare mechanisms */
	}
    /* First compare for unequal signs => less or greater or
     * special equal case */
    Dbl_xortointp1(leftp1,rightp1,xorresult);
    if( xorresult < 0 )
	{
	/* left negative => less, left positive => greater.
	 * equal is possible if both operands are zeros. */
	if( Dbl_iszero_exponentmantissa(leftp1,leftp2)
	  && Dbl_iszero_exponentmantissa(rightp1,rightp2) )
	    {
	    Set_status_cbit(Equal(cond));
	    }
	else if( Dbl_isone_sign(leftp1) )
	    {
	    Set_status_cbit(Lessthan(cond));
	    }
	else
	    {
	    Set_status_cbit(Greaterthan(cond));
	    }
	}
    /* Signs are the same.  Treat negative numbers separately
     * from the positives because of the reversed sense.  */
    else if(Dbl_isequal(leftp1,leftp2,rightp1,rightp2))
	{
	Set_status_cbit(Equal(cond));
	}
    else if( Dbl_iszero_sign(leftp1) )
	{
	/* Positive compare */
	if( Dbl_allp1(leftp1) < Dbl_allp1(rightp1) )
	    {
	    Set_status_cbit(Lessthan(cond));
	    }
	else if( Dbl_allp1(leftp1) > Dbl_allp1(rightp1) )
	    {
	    Set_status_cbit(Greaterthan(cond));
	    }
	else
	    {
	    /* Equal first parts.  Now we must use unsigned compares to
	     * resolve the two possibilities. */
	    if( Dbl_allp2(leftp2) < Dbl_allp2(rightp2) )
		{
		Set_status_cbit(Lessthan(cond));
		}
	    else
		{
		Set_status_cbit(Greaterthan(cond));
		}
	    }
	}
    else
	{
	/* Negative compare.  Signed or unsigned compares
	 * both work the same.  That distinction is only
	 * important when the sign bits differ. */
	if( Dbl_allp1(leftp1) > Dbl_allp1(rightp1) )
	    {
	    Set_status_cbit(Lessthan(cond));
	    }
	else if( Dbl_allp1(leftp1) < Dbl_allp1(rightp1) )
	    {
	    Set_status_cbit(Greaterthan(cond));
	    }
	else
	    {
	    /* Equal first parts.  Now we must use unsigned compares to
	     * resolve the two possibilities. */
	    if( Dbl_allp2(leftp2) > Dbl_allp2(rightp2) )
		{
		Set_status_cbit(Lessthan(cond));
		}
	    else
		{
		Set_status_cbit(Greaterthan(cond));
		}
	    }
	}
	return(NOEXCEPTION);
}
