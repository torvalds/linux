/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1996 Wolfram Schneider <wosch@FreeBSD.org>. Berlin.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "calendar.h"

/* return year day for Easter */

/*
 * This code is based on the Calendar FAQ's code for how to calculate
 * easter is. This is the Gregorian calendar version. They refer to
 * the Algorithm of Oudin in the "Explanatory Supplement to the
 * Astronomical Almanac".
 */

int
easter(int year) /* 0 ... abcd, NOT since 1900 */
{
	int G,	/* Golden number - 1 */
	    C,	/* Century */
	    H,	/* 23 - epact % 30 */
	    I,	/* days from 21 March to Paschal full moon */
	    J,	/* weekday of full moon */
	    L;	/* days from 21 March to Sunday on of before full moon */

	G = year % 19;
	C = year / 100;
	H = (C - C / 4 - (8 * C + 13) / 25 + 19 * G + 15) % 30;
	I = H - (H / 28) * (1 - (H / 28) * (29 / (H + 1)) * ((21 - G) / 11));
	J = (year + year / 4 + I + 2 - C + C / 4) % 7;

	L = I - J;

	if (isleap(year))
		return 31 + 29 + 21 + L + 7;
	else
		return 31 + 28 + 21 + L + 7;
}
