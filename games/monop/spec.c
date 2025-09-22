/*	$OpenBSD: spec.c,v 1.7 2016/01/08 18:20:33 mestre Exp $	*/
/*	$NetBSD: spec.c,v 1.3 1995/03/23 08:35:16 cgd Exp $	*/

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

#include "monop.ext"

static char	*perc[]	= {
	"10%", "ten percent", "%", "$200", "200", 0
	};

void
inc_tax(void)			/* collect income tax			*/
{
	int	worth, com_num;

	com_num = getinp("Do you wish to lose 10% of your total worth or $200? ", perc);
	worth = cur_p->money + prop_worth(cur_p);
	printf("You were worth $%d", worth);
	worth /= 10;
	if (com_num > 2) {
		if (worth < 200)
			printf(".  Good try, but not quite.\n");
		else if (worth > 200)
			lucky(".\nGood guess.  ");
		cur_p->money -= 200;
	}
	else {
		printf(", so you pay $%d", worth);
		if (worth > 200)
			printf("  OUCH!!!!.\n");
		else if (worth < 200)
			lucky("\nGood guess.  ");
		cur_p->money -= worth;
	}
	if (worth == 200)
		lucky("\nIt makes no difference!  ");
}

void
goto_jail(void)			/* move player to jail			*/
{
	cur_p->loc = JAIL;
}

void
lux_tax(void)			/* landing on luxury tax		*/
{
	printf("You lose $75\n");
	cur_p->money -= 75;
}

void
cc(void)				/* draw community chest card		*/
{
	get_card(&CC_D);
}

void
chance(void)			/* draw chance card			*/
{
	get_card(&CH_D);
}
