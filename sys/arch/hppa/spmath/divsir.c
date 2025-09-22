/*	$OpenBSD: divsir.c,v 1.7 2025/06/28 13:24:21 miod Exp $	*/
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
/* @(#)divsir.c: Revision: 1.6.88.1 Date: 93/12/07 15:05:58 */

#include "md.h"

void
divsir(int opnd1, int opnd2, struct mdsfu_register *result)
{
	int sign, op1_sign;

	/* check divisor for zero */
	if (opnd2 == 0) {
		overflow = TRUE;
		return;
	}

	/* get sign of result */
	sign = opnd1 ^ opnd2;

	/* get absolute value of operands */
	if (opnd1 < 0) {
		opnd1 = -opnd1;
		op1_sign = TRUE;
	}
	else op1_sign = FALSE;
	if (opnd2 < 0) opnd2 = -opnd2;

	/* check for opnd2 = -2**31 */
	if (opnd2 + opnd2 == 0) {
		if (opnd1 == opnd2) {
			result_hi = 0;    /* remainder = 0 */
			result_lo = 1;
		}
		else {
			result_hi = opnd1;  /* remainder = opnd1 */
			result_lo = 0;
		}
	}
	else {
		/* do the divide */
		divu(0,opnd1,opnd2,result);

		/*
		 * check for overflow
		 *
		 * at this point, the only way we can get overflow
		 * is with opnd1 = -2**31 and opnd2 = -1
		 */
		if (sign>0 && result_lo<0) {
			overflow = TRUE;
			return;
		}
	}
	overflow = FALSE;

	/* return appropriately signed remainder and result */
	if (op1_sign) result_hi = -result_hi;
	if (sign<0) result_lo = -result_lo;
	return;
}
