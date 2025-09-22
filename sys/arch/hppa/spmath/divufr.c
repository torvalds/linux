/*	$OpenBSD: divufr.c,v 1.7 2025/06/28 13:24:21 miod Exp $	*/
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
/* @(#)divufr.c: Revision: 1.6.88.1 Date: 93/12/07 15:06:03 */

#include "md.h"

void
divufr(unsigned int opnd1, unsigned int opnd2, struct mdsfu_register *result)
{
	/* check divisor for zero */
	if (opnd2 == 0) {
		overflow = TRUE;
		return;
	}

	/* check for overflow */
	if (opnd1 >= opnd2) {
		overflow = TRUE;
		return;
	}
	overflow = FALSE;

	/* do the divide */
	divu(opnd1,0,opnd2,result);
}
