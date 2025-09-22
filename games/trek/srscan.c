/*	$OpenBSD: srscan.c,v 1.9 2016/01/07 14:37:51 mestre Exp $	*/
/*	$NetBSD: srscan.c,v 1.3 1995/04/22 10:59:31 cgd Exp $	*/

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

#include "getpar.h"
#include "trek.h"

/*
**  SHORT RANGE SENSOR SCAN
**
**	A short range scan is taken of the current quadrant.  If the
**	flag 'f' is one, it is an "auto srscan", which is not done
**	unless in 'fast' mode.  It does a status report and a srscan.
**	If 'f' is -1, you get a status report only.  If it is zero,
**	you get a srscan and an optional status report.  The status
**	report is taken if you enter "srscan yes"; for all srscans
**	thereafter you get a status report with your srscan until
**	you type "srscan no".  It defaults to on.
**
**	The current quadrant is filled in on the computer chart.
*/

const char	*const Color[4] =
{
	"GREEN",
	"DOCKED",
	"YELLOW",
	"RED"
};

void
srscan(int f)
{
	int		i, j;
	int		statinfo;
	const char	*s;
	int		percent;
	struct quad	*q = NULL;
	const struct cvntab	*p;

	if (f >= 0 && check_out(SRSCAN))
		return;
	if (f)
		statinfo = 1;
	else
	{
		if (!testnl())
			Etc.statreport = getynpar("status report");
		statinfo = Etc.statreport;
	}
	if (f > 0)
		Etc.statreport = 1;
	if (f >= 0)
	{
		printf("\nShort range sensor scan\n");
		q = &Quad[Ship.quadx][Ship.quady];
		q->scanned = q->klings * 100 + q->bases * 10 + q->stars;
		printf("  ");
		for (i = 0; i < NSECTS; i++)
		{
			printf("%d ", i);
		}
		printf("\n");
	}

	for (i = 0; i < NSECTS; i++)
	{
		if (f >= 0)
		{
			printf("%d ", i);
			for (j = 0; j < NSECTS; j++)
				printf("%c ", Sect[i][j]);
			printf("%d", i);
			if (statinfo)
				printf("   ");
		}
		if (statinfo)
			switch (i)
			{
			  case 0:
				printf("stardate      %.2f", Now.date);
				break;
			  case 1:
				printf("condition     %s", Color[Ship.cond]);
				if (Ship.cloaked)
					printf(", CLOAKED");
				break;
			  case 2:
				printf("position      %d,%d/%d,%d",Ship.quadx, Ship.quady, Ship.sectx, Ship.secty);
				break;
			  case 3:
				printf("warp factor   %.1f", Ship.warp);
				break;
			  case 4:
				printf("total energy  %d", Ship.energy);
				break;
			  case 5:
				printf("torpedoes     %d", Ship.torped);
				break;
			  case 6:
				s = "down";
				if (Ship.shldup)
					s = "up";
				if (damaged(SHIELD))
					s = "damaged";
				percent = 100.0 * Ship.shield / Param.shield;
				printf("shields       %s, %d%%", s, percent);
				break;
			  case 7:
				printf("Klingons left %d", Now.klings);
				break;
			  case 8:
				printf("time left     %.2f", Now.time);
				break;
			  case 9:
				printf("life support  ");
				if (damaged(LIFESUP))
				{
					printf("damaged, reserves = %.2f", Ship.reserves);
					break;
				}
				printf("active");
				break;
			}
		printf("\n");
	}
	if (f < 0)
	{
		printf("current crew  %d\n", Ship.crew);
		printf("brig space    %d\n", Ship.brigfree);
		printf("Klingon power %d\n", Param.klingpwr);
		p = &Lentab[Game.length - 1];
		if (Game.length > 2)
			p--;
		printf("Length, Skill %s%s, ", p->abrev, p->full);
		p = &Skitab[Game.skill - 1];
		printf("%s%s\n", p->abrev, p->full);
		return;
	}
	printf("  ");
	for (i = 0; i < NSECTS; i++)
		printf("%d ", i);
	printf("\n");

	if (q->qsystemname & Q_DISTRESSED)
		printf("Distressed ");
	if (q->qsystemname)
		printf("Starsystem %s\n", systemname(q));
}
