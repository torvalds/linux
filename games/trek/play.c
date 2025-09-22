/*	$OpenBSD: play.c,v 1.8 2017/05/26 19:19:23 tedu Exp $	*/
/*	$NetBSD: play.c,v 1.3 1995/04/22 10:59:18 cgd Exp $	*/

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

#include <setjmp.h>
#include <stdio.h>

#include "getpar.h"
#include "trek.h"

/*
**  INSTRUCTION READ AND MAIN PLAY LOOP
**
**	Well folks, this is it.  Here we have the guts of the game.
**	This routine executes moves.  It sets up per-move variables,
**	gets the command, and executes the command.  After the command,
**	it calls events() to use up time, attack() to have Klingons
**	attack if the move was not free, and checkcond() to check up
**	on how we are doing after the move.
*/

const struct cvntab	Comtab[] =
{
	{ "abandon",		"",		abandon,	0 },
	{ "ca",			"pture",	capture,	0 },
	{ "cl",			"oak",		shield,		-1 },
	{ "c",			"omputer",	computer,	0 },
	{ "da",			"mages",	dcrept,		0 },
	{ "destruct",		"",		destruct,	0 },
	{ "do",			"ck",		dock,		0 },
	{ "help",		"",		help,		0 },
	{ "i",			"mpulse",	impulse,	0 },
	{ "l",			"rscan",	lrscan,		0 },
	{ "m",			"ove",		dowarp,		0 },
	{ "p",			"hasers",	phaser,		0 },
	{ "ram",		"",		dowarp,		1 },
	{ "r",			"est",		rest,		0 },
	{ "sh",			"ield",		shield,		0 },
	{ "s",			"rscan",	srscan,		0 },
	{ "st",			"atus",		srscan,		-1 },
	{ "terminate",		"",		myreset,	0 },
	{ "t",			"orpedo",	torped,		0 },
	{ "u",			"ndock",	undock,		0 },
	{ "v",			"isual",	visual,		0 },
	{ "w",			"arp",		setwarp,	0 },
	{ NULL,			NULL,		NULL,		0 }
};

void
myreset(int v)
{
	extern jmp_buf env;

	longjmp(env, 1);
}

void
play(void)
{
	const struct cvntab	*r;

	while (1)
	{
		Move.free = 1;
		Move.time = 0.0;
		Move.shldchg = 0;
		Move.newquad = 0;
		Move.resting = 0;
		skiptonl(0);
		r = getcodpar("\nCommand", Comtab);
		(*r->value)(r->value2);
		events(0);
		attack(0);
		checkcond();
	}
}
