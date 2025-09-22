/*	$OpenBSD: mpyaccs.c,v 1.7 2025/06/28 13:24:21 miod Exp $	*/
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
/* @(#)mpyaccs.c: Revision: 1.6.88.1 Date: 93/12/07 15:06:39 */

#include "md.h"

void
mpyaccs(int opnd1, int opnd2, struct mdsfu_register *result)
{
	struct mdsfu_register temp;
	int carry, sign;

	s_xmpy(&opnd1,&opnd2,&temp);

	/* get result of low word add, and check for carry out */
	if ((result_lo += (unsigned)temp.rslt_lo) < (unsigned)temp.rslt_lo)
		carry = 1;
	else
		carry = 0;

	/* get result of high word add, and determine overflow status */
	sign = result_hi ^ temp.rslt_hi;
	result_hi += temp.rslt_hi + carry;
	if (sign >= 0 && (temp.rslt_hi ^ result_hi) < 0)
		overflow = TRUE;
}
