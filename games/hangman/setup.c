/*	$OpenBSD: setup.c,v 1.13 2015/12/31 15:20:36 mestre Exp $	*/
/*	$NetBSD: setup.c,v 1.3 1995/03/23 08:32:59 cgd Exp $	*/

/*-
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

#include <sys/stat.h>

#include <curses.h>
#include <err.h>

#include "hangman.h"

/*
 * setup:
 *	Set up the strings on the screen.
 */
void
setup(void)
{
	const char		*const *sp;
	static struct stat	sbuf;

	noecho();
	cbreak();

	mvaddstr(PROMPTY, PROMPTX, "Guess:");
	mvaddstr(GUESSY, GUESSX, "Guessed:");
	mvaddstr(NUMBERY, NUMBERX, "Word #:");
	mvaddstr(AVGY, AVGX, "Current Average:");
	mvaddstr(AVGY + 1, AVGX, "Overall Average:");
	mvaddstr(KNOWNY, KNOWNX, "Word: ");

	for (sp = Noose_pict; *sp != NULL; sp++) {
		move(sp - Noose_pict, 0);
		addstr(*sp);
	}

	/* always check for an ELF file */
	if (sym_setup() != 0) {
		if (syms) {
			endwin();
			err(1, "open %s", Dict_name);
		}
	} else
		syms = 1;

	if (!syms) {
		if ((Dict = fopen(Dict_name, "r")) == NULL) {
			endwin();
			err(1, "fopen %s", Dict_name);
		}
		fstat(fileno(Dict), &sbuf);
		Dict_size = sbuf.st_size;
	}
}
