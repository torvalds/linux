/*	$OpenBSD: extern.c,v 1.9 2015/12/31 15:20:36 mestre Exp $	*/
/*	$NetBSD: extern.c,v 1.3 1995/03/23 08:32:41 cgd Exp $	*/

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

#include "hangman.h"
#include "pathnames.h"

bool	Guessed[26 + 10];

char	Word[BUFSIZ], Known[BUFSIZ];
const char	*const Noose_pict[] = {
	"     ______",
	"     |    |",
	"     |",
	"     |",
	"     |",
	"     |",
	"   __|_____",
	"   |      |___",
	"   |_________|",
		NULL
	};

int	Errors, Wordnum = 0;

double	Average = 0.0;

const ERR_POS	Err_pos[MAXERRS] = {
	{ 2, 10, 'O' },
	{ 3, 10, '|' },
	{ 4, 10, '|' },
	{ 5,  9, '/' },
	{ 3,  9, '/' },
	{ 3, 11, '\\'},
	{ 5, 11, '\\'}
};

const char *Dict_name = _PATH_DICT;

FILE	*Dict = NULL;

off_t	Dict_size;

int	syms;
int	symfd = -1;
off_t	symoffs, symsize;

