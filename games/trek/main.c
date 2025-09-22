/*	$OpenBSD: main.c,v 1.19 2021/01/27 01:57:37 deraadt Exp $	*/
/*	$NetBSD: main.c,v 1.4 1995/04/22 10:59:10 cgd Exp $	*/

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

#include <err.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "getpar.h"
#include "trek.h"

struct quad Quad[NQUADS][NQUADS];
char Sect[NSECTS][NSECTS];
struct event  Event[MAXEVENTS];
struct Ship Ship;
struct Game Game;
struct Move Move;
struct Param Param;
struct Now Now;
struct Etc Etc;

/*
**	 ####  #####	#    ####	   #####  ####	 #####	#   #
**	#	 #     # #   #	 #	     #	  #   #	 #	#  #
**	 ###	 #    #####  ####	     #	  ####	 ###	###
**	    #	 #    #	  #  #	#	     #	  #  #	 #	#  #
**	####	 #    #	  #  #	 #	     #	  #   #	 #####	#   #
**
**	C version by Eric P. Allman 5/76 (U.C. Berkeley) with help
**		from Jeff Poskanzer and Pete Rubinstein.
**
**	I also want to thank everyone here at Berkeley who
**	where crazy enough to play the undebugged game.  I want to
**	particularly thank Nick Whyte, who made considerable
**	suggestions regarding the content of the game.  Why, I'll
**	never forget the time he suggested the name for the
**	"capture" command.
**
**	Please send comments, questions, and suggestions about this
**		game to:
**			Eric P. Allman
**			Project INGRES
**			Electronics Research Laboratory
**			Cory Hall
**			University of California
**			Berkeley, California  94720
**
**	If you make ANY changes in the game, I sure would like to
**	know about them.  It is sort of an ongoing project for me,
**	and I very much want to put in any bug fixes and improvements
**	that you might come up with.
**
**	FORTRASH version by Kay R. Fisher (DEC) "and countless others".
**	That was adapted from the "original BASIC program" (ha!) by
**		Mike Mayfield (Centerline Engineering).
**
**	Additional inspiration taken from FORTRAN version by
**		David Matuszek and Paul Reynolds which runs on the CDC
**		7600 at Lawrence Berkeley Lab, maintained there by
**		Andy Davidson.  This version is also available at LLL
**		and at LMSC.  In all fairness, this version was the
**		major inspiration for this version of the game (trans-
**		lation:  I ripped off a whole lot of code).
**
**	Minor other input from the "Battelle Version 7A" by Joe Miller
**		(Graphics Systems Group, Battelle-Columbus Labs) and
**		Ross Pavlac (Systems Programmer, Battelle Memorial
**		Institute).  That version was written in December '74
**		and extensively modified June '75.  It was adapted
**		from the FTN version by Ron Williams of CDC Sunnyvale,
**		which was adapted from the Basic version distributed
**		by DEC.  It also had "neat stuff swiped" from T. T.
**		Terry and Jim Korp (University of Texas), Hicks (Penn
**		U.), and Rick Maus (Georgia Tech).  Unfortunately, it
**		was not as readable as it could have been and so the
**		translation effort was severely hampered.  None the
**		less, I got the idea of inhabited starsystems from this
**		version.
**
**	Permission is given for use, copying, and modification of
**		all or part of this program and related documentation,
**		provided that all reference to the authors are maintained.
**
**
**********************************************************************
**
**  NOTES TO THE MAINTAINER:
**
**	There is a compilation option xTRACE which must be set for any
**	trace information to be generated (the -t option must also be
**	set on the command line).  It is no longer defined by default.
**
***********************************************************************
*/

jmp_buf env;

int
main(int argc, char **argv)
{
	int		ac;
	char		**av;

	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	av = argv;
	ac = argc;
	av++;

#ifdef xTRACE
	Trace = 0;
	while (ac > 1 && av[0][0] == '-')
	{
		switch (av[0][1])
		{
		  case 't':	/* trace */
			Trace++;
			break;

		  default:
			printf("Invalid option: %s\n", av[0]);

		}
		ac--;
		av++;
	}
#endif

	printf("\n   * * *   S T A R   T R E K   * * *\n\nPress return to continue.\n");

	if (setjmp(env))
	{
		if ( !getynpar("Another game") )
			return 0;
	}
	do
	{
		setup();
		play();
	} while (getynpar("Another game"));

	fflush(stdout);
	return 0;
}
