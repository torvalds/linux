/*	$OpenBSD: tip.h,v 1.27 2006/08/18 03:06:18 jason Exp $	*/
/*	$NetBSD: tip.h,v 1.7 1997/04/20 00:02:46 mellon Exp $	*/
/*	$FreeBSD$	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
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
 *      @(#)tip.h	8.1 (Berkeley) 6/6/93
 */

/*
 * tip - terminal interface program
 */

#include <sys/types.h>
#include <sys/file.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/ioctl.h>

#include <termios.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pwd.h>
#include <ctype.h>
#include <setjmp.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>

/*
 * Remote host attributes
 */
char	*DV;			/* UNIX device(s) to open */
char	*EL;			/* chars marking an EOL */
char	*CM;			/* initial connection message */
char	*IE;			/* EOT to expect on input */
char	*OE;			/* EOT to send to complete FT */
char	*CU;			/* call unit if making a phone call */
char	*AT;			/* acu type */
char	*PN;			/* phone number(s) */
char	*DI;			/* disconnect string */
char	*PA;			/* parity to be generated */

char	*PH;			/* phone number file */
char	*RM;			/* remote file name */
char	*HO;			/* host name */

long	BR;			/* line speed for conversation */
long	FS;			/* frame size for transfers */

short	DU;			/* this host is dialed up */
short	HW;			/* this device is hardwired, see hunt.c */
char	*ES;			/* escape character */
char	*EX;			/* exceptions */
char	*FO;			/* force (literal next) char*/
char	*RC;			/* raise character */
char	*RE;			/* script record file */
char	*PR;			/* remote prompt */
long	DL;			/* line delay for file transfers to remote */
long	CL;			/* char delay for file transfers to remote */
long	ET;			/* echocheck timeout */
long	LD;			/* line disc */
short	HD;			/* this host is half duplex - do local echo */

/*
 * String value table
 */
typedef
	struct {
		char	*v_name;	/* whose name is it */
		char	v_type;		/* for interpreting set's */
		char	v_access;	/* protection of touchy ones */
		char	*v_abrev;	/* possible abbreviation */
		char	*v_value;	/* casted to a union later */
	}
	value_t;

#define STRING	01		/* string valued */
#define BOOL	02		/* true-false value */
#define NUMBER	04		/* numeric value */
#define CHAR	010		/* character value */

#define WRITE	01		/* write access to variable */
#define	READ	02		/* read access */

#define CHANGED	01		/* low bit is used to show modification */
#define PUBLIC	1		/* public access rights */
#define PRIVATE	03		/* private to definer */
#define ROOT	05		/* root defined */

#define	TRUE	1
#define FALSE	0

#define ENVIRON	020		/* initialize out of the environment */
#define IREMOTE	040		/* initialize out of remote structure */
#define INIT	0100		/* static data space used for initialization */
#define TMASK	017

/*
 * Definition of ACU line description
 */
typedef
	struct {
		char	*acu_name;
		int	(*acu_dialer)(char *, char *);
		void	(*acu_disconnect)(void);
		void	(*acu_abort)(void);
	}
	acu_t;

#define	equal(a, b)	(strcmp(a,b)==0)/* A nice function to string compare */

/*
 * variable manipulation stuff --
 *   if we defined the value entry in value_t, then we couldn't
 *   initialize it in vars.c, so we cast it as needed to keep lint
 *   happy.
 */

#define value(v)	vtable[v].v_value
#define lvalue(v)	(long)vtable[v].v_value

#define	number(v)	((long)(v))
#define	boolean(v)      ((short)(long)(v))
#define	character(v)    ((char)(long)(v))
#define	address(v)      ((long *)(v))

#define	setnumber(v,n)		do { (v) = (char *)(long)(n); } while (0)
#define	setboolean(v,n)		do { (v) = (char *)(long)(n); } while (0)
#define	setcharacter(v,n)	do { (v) = (char *)(long)(n); } while (0)
#define	setaddress(v,n)		do { (v) = (char *)(n); } while (0)

/*
 * Escape command table definitions --
 *   lookup in this table is performed when ``escapec'' is recognized
 *   at the beginning of a line (as defined by the eolmarks variable).
*/

typedef
	struct {
		char	e_char;			/* char to match on */
		char	e_flags;		/* experimental, privileged */
		char	*e_help;		/* help string */
		void	(*e_func)(int);		/* command */
	}
	esctable_t;

#define NORM	00		/* normal protection, execute anyone */
#define EXP	01		/* experimental, mark it with a `*' on help */
#define PRIV	02		/* privileged, root execute only */

extern int	vflag;		/* verbose during reading of .tiprc file */
extern int	noesc;		/* no escape `~' char */
extern value_t	vtable[];	/* variable table */

#ifndef ACULOG
#define logent(a, b, c, d)
#define loginit()
#endif

/*
 * Definition of indices into variable table so
 *  value(DEFINE) turns into a static address.
 */

#define BEAUTIFY	0
#define BAUDRATE	1
#define DIALTIMEOUT	2
#define EOFREAD		3
#define EOFWRITE	4
#define EOL		5
#define ESCAPE		6
#define EXCEPTIONS	7
#define FORCE		8
#define FRAMESIZE	9
#define HOST		10
#define LOG		11
#define PHONES		12
#define PROMPT		13
#define RAISE		14
#define RAISECHAR	15
#define RECORD		16
#define REMOTE		17
#define SCRIPT		18
#define TABEXPAND	19
#define VERBOSE		20
#define SHELL		21
#define HOME		22
#define ECHOCHECK	23
#define DISCONNECT	24
#define TAND		25
#define LDELAY		26
#define CDELAY		27
#define ETIMEOUT	28
#define RAWFTP		29
#define HALFDUPLEX	30
#define	LECHO		31
#define	PARITY		32
#define	HARDWAREFLOW	33
#define	LINEDISC	34
#define	DC		35

#define NOVAL	((value_t *)NULL)
#define NOACU	((acu_t *)NULL)
#define NOSTR	((char *)NULL)
#define NOFILE	((FILE *)NULL)
#define NOPWD	((struct passwd *)0)

struct termios	term;		/* current mode of terminal */
struct termios	defterm;	/* initial mode of terminal */
struct termios	defchars;	/* current mode with initial chars */
int	gotdefterm;

FILE	*fscript;		/* FILE for scripting */

int	fildes[2];		/* file transfer synchronization channel */
int	repdes[2];		/* read process sychronization channel */
int	FD;			/* open file descriptor to remote host */
int	AC;			/* open file descriptor to dialer (v831 only) */
int	vflag;			/* print .tiprc initialization sequence */
int	noesc;			/* no `~' escape char */
int	sfd;			/* for ~< operation */
pid_t	tipin_pid;		/* pid of tipin */
pid_t	tipout_pid;		/* pid of tipout */
uid_t	uid, euid;		/* real and effective user id's */
gid_t	gid, egid;		/* real and effective group id's */
int	stop;			/* stop transfer session flag */
int	quit;			/* same; but on other end */
int	intflag;		/* recognized interrupt */
int	stoprompt;		/* for interrupting a prompt session */
int	timedout;		/* ~> transfer timedout */
int	cumode;			/* simulating the "cu" program */
int	bits8;			/* terminal is 8-bit mode */
#define STRIP_PAR	(bits8 ? 0377 : 0177)

char	fname[PATH_MAX];	/* file name buffer for ~< */
char	copyname[PATH_MAX];	/* file name buffer for ~> */
char	ccc;			/* synchronization character */
char	*uucplock;		/* name of lock file for uucp's */

int	odisc;			/* initial tty line discipline */
extern	int disc;		/* current tty discpline */

extern	char *__progname;	/* program name */

char	*con(void);
char	*ctrl(char);
char	*expand(char *);
char	*getremote(char *);
char	*interp(char *);
int	any(int, char *);
int	biz22w_dialer(char *, char *);
int	biz22f_dialer(char *, char *);
int	biz31w_dialer(char *, char *);
int	biz31f_dialer(char *, char *);
int	cour_dialer(char *, char *);
int	df02_dialer(char *, char *);
int	df03_dialer(char *, char *);
int	dn_dialer(char *, char *);
int	hay_dialer(char *, char *);
int	prompt(char *, char *, size_t);
size_t	size(char *);
int	t3000_dialer(char *, char *);
int	ttysetup(int);
int	uu_lock(char *);
int	uu_unlock(char *);
int	v3451_dialer(char *, char *);
int	v831_dialer(char *, char *);
int	ven_dialer(char *, char *);
int	vstring(char *, char *);
long	hunt(char *);
void	biz22_disconnect(void);
void	biz22_abort(void);
void	biz31_disconnect(void);
void	biz31_abort(void);
void	chdirectory(int);
void	cleanup(int);
void	consh(int);
void	cour_abort(void);
void	cour_disconnect(void);
void	cu_put(int);
void	cu_take(int);
void	cumain(int, char **);
void	daemon_uid(void);
void	df_abort(void);
void	df_disconnect(void);
void	disconnect(char *);
void	dn_abort(void);
void	dn_disconnect(void);
void	finish(int);
void	genbrk(int);
void	getfl(int);
void	hay_abort(void);
void	hay_disconnect(void);
void	help(int);
void	listvariables(int);
void	logent(char *, char *, char *, char *);
void	loginit(void);
void	parwrite(int, char *, size_t);
void	pipefile(int);
void	pipeout(int);
void	raw(void);
void	sendfile(int);
void	setparity(char *);
void	setscript(void);
void	shell(int);
void	shell_uid(void);
void	suspend(int);
void	t3000_disconnect(void);
void	t3000_abort(void);
void	timeout(int);
void	tipabort(char *);
void	tipout(void);
void	user_uid(void);
void	unexcl(void);
void	unraw(void);
void	v3451_abort(void);
void	v3451_disconnect(void);
void	v831_disconnect(void);
void	v831_abort(void);
void	variable(int);
void	ven_disconnect(void);
void	ven_abort(void);
void	vinit(void);
void	vlex(char *);
