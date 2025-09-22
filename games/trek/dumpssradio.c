/*	$OpenBSD: dumpssradio.c,v 1.7 2016/01/07 14:37:51 mestre Exp $	*/
/*	$NetBSD: dumpssradio.c,v 1.3 1995/04/22 10:58:49 cgd Exp $	*/

/*
 * Copyright (c) 1980, 1993
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

#include <stdio.h>

#include "trek.h"

/**
 **	output hidden distress calls
 **/

int
dumpssradio(void)
{
	struct event	*e;
	int		j, chkrest;

	chkrest = 0;
	for (j = 0; j < MAXEVENTS; j++)
	{
		e = &Event[j];
		/* if it is not hidden, then just ignore it */
		if ((e->evcode & E_HIDDEN) == 0)
			continue;
		if (e->evcode & E_GHOST)
		{
			unschedule(e);
			printf("Starsystem %s in quadrant %d,%d is no longer distressed\n",
				systemname(&Quad[e->x][e->y]), e->x, e->y);
			continue;
		}

		switch (e->evcode)
		{

		  case E_KDESB:
			printf("Starbase in quadrant %d,%d is under attack\n",
				e->x, e->y);
			chkrest++;
			break;

		  case E_ENSLV:
		  case E_REPRO:
			printf("Starsystem %s in quadrant %d,%d is distressed\n",
				systemname(&Quad[e->x][e->y]), e->x, e->y);
			chkrest++;
			break;

		}
	}

	return (chkrest);
}
