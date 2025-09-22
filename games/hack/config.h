/*	$OpenBSD: config.h,v 1.14 2019/04/05 09:02:27 bentley Exp $ */
/* *	$NetBSD: config.h,v 1.3 1995/03/23 08:29:15 cgd Exp $*/

/*
 * Copyright (c) 1985, Stichting Centrum voor Wiskunde en Informatica,
 * Amsterdam
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * - Neither the name of the Stichting Centrum voor Wiskunde en
 * Informatica, nor the names of its contributors may be used to endorse or
 * promote products derived from this software without specific prior
 * written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (c) 1982 Jay Fenlason <hack@gnu.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL
 * THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "pathnames.h"

#ifndef CONFIG	/* make sure the compiler doesn't see the typedefs twice */

#define	CONFIG
#define	UNIX		/* delete if no fork(), exec() available */
#define	CHDIR		/* delete if no chdir() available */

/* #define STUPID */	/* avoid some complicated expressions if
			   your C compiler chokes on them */
/* #define PYRAMID_BUG */	/* avoid a bug on the Pyramid */

#define WIZARD  "bruno"	/* the person allowed to use the -D option */
#define RECORD	"record"/* the file containing the list of topscorers */
#define	NEWS	"news"	/* the file containing the latest hack news */
#define	HELP	"/usr/share/games/hack/help"	/* the file containing a description of the commands */
#define	SHELP	"/usr/share/games/hack/hh"	/* abbreviated form of the same */
#define	RUMORFILE	"/usr/share/games/hack/rumors"	/* a file with fortune cookies */
#define	DATAFILE	"/usr/share/games/hack/data"	/* a file giving the meaning of symbols used */
#define	FMASK	0660	/* file creation mask */
#define	HLOCK	"perm"	/* an empty file used for locking purposes */
#define LLOCK	"safelock"	/* link to previous */

#ifdef UNIX
/*
 * Define DEF_PAGER as your default pager, e.g. "/bin/cat" or "/usr/ucb/more"
 * If defined, it can be overridden by the environment variable PAGER.
 * Hack will use its internal pager if DEF_PAGER is not defined.
 * (This might be preferable for security reasons.)
 * #define DEF_PAGER	".../mydir/mypager"
 */
/* #define	DEF_PAGER	_PATH_PAGER */

/*
 * If you define MAIL, then the player will be notified of new mail
 * when it arrives. If you also define DEF_MAILREADER then this will
 * be the default mail reader, and can be overridden by the environment
 * variable MAILREADER; otherwise an internal pager will be used.
 * A stat system call is done on the mailbox every MAILCKFREQ moves.
 */
/* #define	MAIL */
#define	DEF_MAILREADER	_PATH_MAIL		/* or e.g. /bin/mail */
#define	MAILCKFREQ	100	


/* #define SHELL */		/* do not delete the '!' command */

#define	SUSPEND		/* let ^Z suspend the game */
#endif /* UNIX */

#ifdef CHDIR
/*
 * If you define HACKDIR, then this will be the default playground;
 * otherwise it will be the current directory.
 */
#ifdef QUEST
#define HACKDIR _PATH_QUEST
#else /* QUEST */
/* #define HACKDIR	_PATH_HACK */
#endif /* QUEST */

/*
 * Some system administrators are stupid enough to make Hack suid root
 * or suid daemon, where daemon has other powers besides that of reading or
 * writing Hack files. In such cases one should be careful with chdir's
 * since the user might create files in a directory of his choice.
 * Of course SECURE is meaningful only if HACKDIR is defined.
 */
/* #define SECURE */			/* do setuid(getuid()) after chdir() */

/*
 * If it is desirable to limit the number of people that can play Hack
 * simultaneously, define HACKDIR, SECURE and MAX_NR_OF_PLAYERS.
 * #define MAX_NR_OF_PLAYERS	100
 */
#endif /* CHDIR */

/* size of terminal screen is (at least) (ROWNO+2) by COLNO */
#define	COLNO	80
#define	ROWNO	22

/*
 * small signed integers (8 bits suffice)
 *	typedef	char	schar;
 * will do when you have signed characters; otherwise use
 *	typedef	short int schar;
 */
typedef	signed char	schar;

/*
 * small unsigned integers (8 bits suffice - but 7 bits do not)
 * - these are usually object types; be careful with inequalities! -
 *	typedef	unsigned char	uchar;
 * will be satisfactory if you have an "unsigned char" type; otherwise use
 *	typedef unsigned short int uchar;
 */
typedef	unsigned char	uchar;

/*
 * small integers in the range 0 - 127, usually coordinates
 * although they are nonnegative they must not be declared unsigned
 * since otherwise comparisons with signed quantities are done incorrectly
 */
typedef schar	xchar;
typedef	xchar	boolean;		/* 0 or 1 */
#define	TRUE	1
#define	FALSE	0

/*
 * Declaration of bitfields in various structs; if your C compiler
 * doesn't handle bitfields well, e.g., if it is unable to initialize
 * structs containing bitfields, then you might use
 *	#define Bitfield(x,n)	uchar x
 * since the bitfields used never have more than 7 bits. (Most have 1 bit.)
 */
#define	Bitfield(x,n)	unsigned x:n

#define	SIZE(x)	(int)(sizeof(x) / sizeof(x[0]))

#endif /* CONFIG */
