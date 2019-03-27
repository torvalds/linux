/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Kazutaka YOKOTA <yokota@zodiac.mech.utsunomiya-u.ac.jp>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer as
 *    the first lines of this file unmodified.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_syscons.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/consio.h>

#include <dev/syscons/syscons.h>

SET_DECLARE(scterm_set, sc_term_sw_t);

/* exported subroutines */

void
sc_move_cursor(scr_stat *scp, int x, int y)
{
	if (x < 0)
		x = 0;
	if (y < 0)
		y = 0;
	if (x >= scp->xsize)
		x = scp->xsize - 1;
	if (y >= scp->ysize)
		y = scp->ysize - 1;
	scp->xpos = x;
	scp->ypos = y;
	scp->cursor_pos = scp->ypos*scp->xsize + scp->xpos;
}

void
sc_clear_screen(scr_stat *scp)
{
	(*scp->tsw->te_clear)(scp);
	scp->cursor_oldpos = scp->cursor_pos;
	sc_remove_cutmarking(scp);
}

/* terminal emulator manager routines */

static LIST_HEAD(, sc_term_sw) sc_term_list = 
	LIST_HEAD_INITIALIZER(sc_term_list);

int
sc_term_add(sc_term_sw_t *sw)
{
	LIST_INSERT_HEAD(&sc_term_list, sw, link);
	return 0;
}

int
sc_term_remove(sc_term_sw_t *sw)
{
	LIST_REMOVE(sw, link);
	return 0;
}

sc_term_sw_t
*sc_term_match(char *name)
{
	sc_term_sw_t **list;
	sc_term_sw_t *p;

	if (!LIST_EMPTY(&sc_term_list)) {
		LIST_FOREACH(p, &sc_term_list, link) {
			if ((strcmp(name, p->te_name) == 0)
			    || (strcmp(name, "*") == 0)) {
				return p;
			}
		}
	} else {
		SET_FOREACH(list, scterm_set) {
			p = *list;
			if ((strcmp(name, p->te_name) == 0)
			    || (strcmp(name, "*") == 0)) {
				return p;
			}
		}
	}

	return NULL;
}

sc_term_sw_t
*sc_term_match_by_number(int index)
{
	sc_term_sw_t *p;

	if (index <= 0)
		return NULL;
	LIST_FOREACH(p, &sc_term_list, link) {
		if (--index <= 0)
			return p;
	}

	return NULL;
}
