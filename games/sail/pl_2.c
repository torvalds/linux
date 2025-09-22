/*	$OpenBSD: pl_2.c,v 1.6 2016/01/08 20:26:33 mestre Exp $	*/
/*	$NetBSD: pl_2.c,v 1.3 1995/04/22 10:37:08 cgd Exp $	*/

/*
 * Copyright (c) 1983, 1993
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

#include <signal.h>

#include "extern.h"
#include "machdep.h"
#include "player.h"

void
play(void)
{
	struct ship *sp;

	for (;;) {
		switch (sgetch("~\b", (struct ship *)0, 0)) {
		case 'm':
			acceptmove();
			break;
		case 's':
			acceptsignal();
			break;
		case 'g':
			grapungrap();
			break;
		case 'u':
			unfoulplayer();
			break;
		case 'v':
			Msg("%s", version);
			break;
		case 'b':
			acceptboard();
			break;
		case 'f':
			acceptcombat();
			break;
		case 'l':
			loadplayer();
			break;
		case 'c':
			changesail();
			break;
		case 'r':
			repair();
			break;
		case 'B':
			Msg("'Hands to stations!'");
			unboard(ms, ms, 1);	/* cancel DBP's */
			unboard(ms, ms, 0);	/* cancel offense */
			break;
		case '\f':
			centerview();
			blockalarm();
			draw_board();
			draw_screen();
			unblockalarm();
			break;
		case 'L':
			mf->loadL = L_EMPTY;
			mf->loadR = L_EMPTY;
			mf->readyL = R_EMPTY;
			mf->readyR = R_EMPTY;
			Msg("Broadsides unloaded");
			break;
		case 'q':
			Msg("Type 'Q' to quit");
			break;
		case 'Q':
			leave(LEAVE_QUIT);
			break;
		case 'I':
			foreachship(sp)
				if (sp != ms)
					eyeball(sp);
			break;
		case 'i':
			if ((sp = closestenemy(ms, 0, 1)) == 0)
				Msg("No more ships left.");
			else
				eyeball(sp);
			break;
		case 'C':
			centerview();
			blockalarm();
			draw_view();
			unblockalarm();
			break;
		case 'U':
			upview();
			blockalarm();
			draw_view();
			unblockalarm();
			break;
		case 'D':
		case 'N':
			downview();
			blockalarm();
			draw_view();
			unblockalarm();
			break;
		case 'H':
			leftview();
			blockalarm();
			draw_view();
			unblockalarm();
			break;
		case 'J':
			rightview();
			blockalarm();
			draw_view();
			unblockalarm();
			break;
		case 'F':
			lookout();
			break;
		case 'S':
			dont_adjust = !dont_adjust;
			blockalarm();
			draw_turn();
			unblockalarm();
			break;
		}
	}
}
