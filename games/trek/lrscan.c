/*	$OpenBSD: lrscan.c,v 1.7 2016/01/07 14:37:51 mestre Exp $	*/
/*	$NetBSD: lrscan.c,v 1.3 1995/04/22 10:59:09 cgd Exp $	*/

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

/*
**  LONG RANGE OF SCANNERS
**
**	A summary of the quadrants that surround you is printed.  The
**	hundreds digit is the number of Klingons in the quadrant,
**	the tens digit is the number of starbases, and the units digit
**	is the number of stars.  If the printout is "///" it means
**	that that quadrant is rendered uninhabitable by a supernova.
**	It also updates the "scanned" field of the quadrants it scans,
**	for future use by the "chart" option of the computer.
*/

void
lrscan(int v)
{
	int			i, j;
	struct quad		*q;

	if (check_out(LRSCAN))
		return;
	printf("Long range scan for quadrant %d,%d\n\n", Ship.quadx, Ship.quady);

	/* print the header on top */
	for (j = Ship.quady - 1; j <= Ship.quady + 1; j++)
	{
		if (j < 0 || j >= NQUADS)
			printf("      ");
		else
			printf("     %1d", j);
	}

	/* scan the quadrants */
	for (i = Ship.quadx - 1; i <= Ship.quadx + 1; i++)
	{
		printf("\n  -------------------\n");
		if (i < 0 || i >= NQUADS)
		{
			/* negative energy barrier */
			printf("  !  *  !  *  !  *  !");
			continue;
		}

		/* print the left hand margin */
		printf("%1d !", i);
		for (j = Ship.quady - 1; j <= Ship.quady + 1; j++)
		{
			if (j < 0 || j >= NQUADS)
			{
				/* negative energy barrier again */
				printf("  *  !");
				continue;
			}
			q = &Quad[i][j];
			if (q->stars < 0)
			{
				/* supernova */
				printf(" /// !");
				q->scanned = 1000;
				continue;
			}
			q->scanned = q->klings * 100 + q->bases * 10 + q->stars;
			printf(" %3d !", q->scanned);
		}
	}
	printf("\n  -------------------\n");
}
