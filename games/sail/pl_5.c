/*	$OpenBSD: pl_5.c,v 1.9 2016/01/08 20:26:33 mestre Exp $	*/
/*	$NetBSD: pl_5.c,v 1.4 1995/04/24 12:25:21 cgd Exp $	*/

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

#include <ctype.h>
#include <signal.h>
#include <string.h>

#include "extern.h"
#include "machdep.h"
#include "player.h"

#define turnfirst(x) (*x == 'r' || *x == 'l')

void
acceptmove(void)
{
	int ta;
	int ma;
	char af;
	int moved = 0;
	int vma, dir;
	char prompt[60];
	char buf[60], last = '\0';
	char *p;

	if (!mc->crew3 || snagged(ms) || !windspeed) {
		Msg("Unable to move");
		return;
	}

	ta = maxturns(ms, &af);
	ma = maxmove(ms, mf->dir, 0);
	(void) snprintf(prompt, sizeof prompt,
		"move (%d,%c%d): ", ma, af ? '\'' : ' ', ta);
	sgetstr(prompt, buf, sizeof buf);
	dir = mf->dir;
	vma = ma;
	for (p = buf; *p; p++)
		switch (*p) {
		case 'l':
			dir -= 2;
		case 'r':
			if (++dir == 0)
				dir = 8;
			else if (dir == 9)
				dir = 1;
			if (last == 't') {
				Msg("Ship can't turn that fast.");
				*p-- = '\0';
			}
			last = 't';
			ma--;
			ta--;
			vma = min(ma, maxmove(ms, dir, 0));
			if ((ta < 0 && moved) || (vma < 0 && moved))
				*p-- = '\0';
			break;
		case 'b':
			ma--;
			vma--;
			last = 'b';
			if ((ta < 0 && moved) || (vma < 0 && moved))
				*p-- = '\0';
			break;
		case '0':
		case 'd':
			*p-- = '\0';
			break;
		case '\n':
			*p-- = '\0';
			break;
		case '1': case '2': case '3': case '4':
		case '5': case '6': case '7':
			if (last == '0') {
				Msg("Can't move that fast.");
				*p-- = '\0';
			}
			last = '0';
			moved = 1;
			ma -= *p - '0';
			vma -= *p - '0';
			if ((ta < 0 && moved) || (vma < 0 && moved))
				*p-- = '\0';
			break;
		default:
			if (!isspace((unsigned char)*p)) {
				Msg("Input error.");
				*p-- = '\0';
			}
		}
	if ((ta < 0 && moved) || (vma < 0 && moved)
	    || (af && turnfirst(buf) && moved)) {
		Msg("Movement error.");
		if (ta < 0 && moved) {
			if (mf->FS == 1) {
				Write(W_FS, ms, 0, 0, 0, 0);
				Msg("No hands to set full sails.");
			}
		} else if (ma >= 0)
			buf[1] = '\0';
	}
	if (af && !moved) {
		if (mf->FS == 1) {
			Write(W_FS, ms, 0, 0, 0, 0);
			Msg("No hands to set full sails.");
		}
	}
	if (*buf)
		(void) strlcpy(movebuf, buf, sizeof movebuf);
	else
		(void) strlcpy(movebuf, "d", sizeof movebuf);
	Writestr(W_MOVE, ms, movebuf);
	Msg("Helm: %s.", movebuf);
}

void
acceptboard(void)
{
	struct ship *sp;
	int n;
	int crew[3];
	int men = 0;
	char c;

	crew[0] = mc->crew1;
	crew[1] = mc->crew2;
	crew[2] = mc->crew3;
	for (n = 0; n < NBP; n++) {
		if (mf->OBP[n].turnsent)
			    men += mf->OBP[n].mensent;
	}
	for (n = 0; n < NBP; n++) {
		if (mf->DBP[n].turnsent)
			    men += mf->DBP[n].mensent;
	}
	if (men) {
		crew[0] = men/100 ? 0 : crew[0] != 0;
		crew[1] = (men%100)/10 ? 0 : crew[1] != 0;
		crew[2] = men%10 ? 0 : crew[2] != 0;
	} else {
		crew[0] = crew[0] != 0;
		crew[1] = crew[1] != 0;
		crew[2] = crew[2] != 0;
	}
	foreachship(sp) {
		if (sp == ms || sp->file->dir == 0 || range(ms, sp) > 1)
			continue;
		if (ms->nationality == capship(sp)->nationality)
			continue;
		if (meleeing(ms, sp) && crew[2]) {
			c = sgetch("How many more to board the $$? ",
				sp, 1);
			parties(crew, sp, 0, c);
		} else if ((fouled2(ms, sp) || grappled2(ms, sp)) && crew[2]) {
			c = sgetch("Crew sections to board the $$ (3 max) ?", sp, 1);
			parties(crew, sp, 0, c);
		}
	}
	if (crew[2]) {
		c = sgetch("How many sections to repel boarders? ",
			(struct ship *)0, 1);
		parties(crew, ms, 1, c);
	}
	blockalarm();
	draw_slot();
	unblockalarm();
}

void
parties(int crew[3], struct ship *to, int isdefense, int buf)
{
	int k, j, men; 
	struct BP *ptr;
	int temp[3];

	for (k = 0; k < 3; k++)
		temp[k] = crew[k];
	if (isdigit((unsigned char)buf)) {
		ptr = isdefense ? to->file->DBP : to->file->OBP; 
		for (j = 0; j < NBP && ptr[j].turnsent; j++)
			;
		if (j < NBP && buf > '0') {
			men = 0;
			for (k = 0; k < 3 && buf > '0'; k++) {
				men += crew[k]
					* (k == 0 ? 100 : (k == 1 ? 10 : 1));
				crew[k] = 0;
				if (men)
					buf--;
			}
			if (buf > '0')
				Msg("Sending all crew sections.");
			Write(isdefense ? W_DBP : W_OBP, ms,
				j, turn, to->file->index, men);
			if (isdefense) {
				(void) wmove(slot_w, 2, 0);
				for (k=0; k < NBP; k++)
					if (temp[k] && !crew[k])
						(void) waddch(slot_w, k + '1');
					else
						(void) wmove(slot_w, 2, 1 + k);
				(void) mvwaddstr(slot_w, 3, 0, "DBP");
				makemsg(ms, "repelling boarders");
			} else {
				(void) wmove(slot_w, 0, 0);
				for (k=0; k < NBP; k++)
					if (temp[k] && !crew[k])
						(void) waddch(slot_w, k + '1');
					else
						(void) wmove(slot_w, 0, 1 + k);
				(void) mvwaddstr(slot_w, 1, 0, "OBP");
				makesignal(ms, "boarding the $$", to);
			}
			blockalarm();
			(void) wrefresh(slot_w);
			unblockalarm();
		} else
			Msg("Sending no crew sections.");
	}
}
