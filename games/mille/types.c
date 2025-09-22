/*	$OpenBSD: types.c,v 1.10 2016/01/08 18:09:59 mestre Exp $	*/
/*	$NetBSD: types.c,v 1.4 1995/03/24 05:02:22 cgd Exp $	*/

/*
 * Copyright (c) 1982, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <err.h>

#include "mille.h"

/*
 * @(#)types.c	1.1 (Berkeley) 4/1/82
 */

int
is_repair(CARD card)
{
	return card == C_GAS || card == C_SPARE ||
	    card == C_REPAIRS || card == C_INIT;
}

int
safety(CARD card)
{
	switch (card) {
	  case C_EMPTY:
	  case C_GAS:
	  case C_GAS_SAFE:
		return C_GAS_SAFE;
	  case C_FLAT:
	  case C_SPARE:
	  case C_SPARE_SAFE:
		return C_SPARE_SAFE;
	  case C_CRASH:
	  case C_REPAIRS:
	  case C_DRIVE_SAFE:
		return C_DRIVE_SAFE;
	  case C_GO:
	  case C_STOP:
	  case C_RIGHT_WAY:
	  case C_LIMIT:
	  case C_END_LIMIT:
		return C_RIGHT_WAY;
	}
	errx(1, "safety() failed; please submit bug report.");
}
