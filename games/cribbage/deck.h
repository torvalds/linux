/*	$OpenBSD: deck.h,v 1.4 2015/12/31 18:10:19 mestre Exp $	*/
/*	$NetBSD: deck.h,v 1.3 1995/03/21 15:08:49 cgd Exp $	*/

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
 *
 *	@(#)deck.h	8.1 (Berkeley) 5/31/93
 */

/*
 * define structure of a deck of cards and other related things
 */

#define		CARDS		52		/* number cards in deck */
#define		RANKS		13		/* number ranks in deck */
#define		SUITS		4		/* number suits in deck */

#define		CINHAND		4		/* # cards in cribbage hand */
#define		FULLHAND	6		/* # cards in dealt hand */

#define		LGAME		121		/* number points in a game */
#define		SGAME		61		/* # points in a short game */

#define		SPADES		0		/* value of each suit */
#define		HEARTS		1
#define		DIAMONDS	2
#define		CLUBS		3

#define		ACE		0		/* value of each rank */
#define		TWO		1
#define		THREE		2
#define		FOUR		3
#define		FIVE		4
#define		SIX		5
#define		SEVEN		6
#define		EIGHT		7
#define		NINE		8
#define		TEN		9
#define		JACK		10
#define		QUEEN		11
#define		KING		12
#define		EMPTY		13

#define		VAL(c)		( (c) < 9 ? (c)+1 : 10 )    /* val of rank */


#ifndef TRUE
#	define		TRUE		1
#	define		FALSE		0
#endif

typedef		struct  {
			int		rank;
			int		suit;
		}		CARD;
