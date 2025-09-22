/*	$OpenBSD: lose.c,v 1.7 2016/01/07 14:37:51 mestre Exp $	*/
/*	$NetBSD: lose.c,v 1.3 1995/04/22 10:59:08 cgd Exp $	*/

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
#include <unistd.h>

#include "getpar.h"
#include "trek.h"

/*
**  PRINT OUT LOSER MESSAGES
**
**	The messages are printed out, the score is computed and
**	printed, and the game is restarted.  Oh yeh, any special
**	actions which need be taken are taken.
*/

const char	*const Losemsg[] =
{
	"You ran out of time",
	"You ran out of energy",
	"You have been destroyed",
	"You ran into the negative energy barrier",
	"You destroyed yourself by nova'ing that star",
	"You have been caught in a supernova",
	"You just suffocated in outer space",
	"You could not be rematerialized",
	"\n\032\014 ***\07 Ship's hull has imploded\07 ***",
	"You have burned up in a star",
	"Well, you destroyed yourself, but it didn't do any good",
	"You have been captured by Klingons and mercilessly tortured",
	"Your last crew member died",
};

void
lose(int why)
{
	extern jmp_buf	env;

	Game.killed = 1;
	sleep(1);
	printf("\n%s\n", Losemsg[why - 1]);
	switch (why)
	{

	  case L_NOTIME:
		Game.killed = 0;
		break;
	}
	Move.endgame = -1;
	score();
	skiptonl(0);
	longjmp(env, 1);
}
