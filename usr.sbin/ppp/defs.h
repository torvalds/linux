/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1996 - 2001 Brian Somers <brian@Awfulhak.org>
 *          based on work by Toshiharu OHNO <tony-o@iij.ad.jp>
 *                           Internet Initiative Japan, Inc (IIJ)
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/* Check the following definitions for your machine environment */
#ifdef __FreeBSD__
# define  MODEM_LIST	"/dev/cuau1\0/dev/cuau0"	/* name of tty device */
#else
# ifdef __OpenBSD__
#  define MODEM_LIST	"/dev/cua01\0/dev/cua00"	/* name of tty device */
# else
#  define MODEM_LIST	"/dev/tty01\0/dev/tty00"	/* name of tty device */
# endif
#endif
#define NMODEMS		2

#ifndef PPP_CONFDIR
#define PPP_CONFDIR	"/etc/ppp"
#endif

#define TUN_NAME	"tun"
#define TUN_PREFIX	(_PATH_DEV TUN_NAME)	/* /dev/tun */

#define MODEM_SPEED	B38400	/* tty speed */
#define	SERVER_PORT	3000	/* Base server port no. */
#define	MODEM_CTSRTS	1	/* Default (true): use CTS/RTS signals */
#define	RECONNECT_TIMEOUT 3	/* Default timer for carrier loss */
#define	DIAL_TIMEOUT	30	/* Default and Max random time to redial */
#define	DIAL_NEXT_TIMEOUT 3	/* Default Hold time to next number redial */
#define SCRIPT_LEN 512		/* Size of login/dial/hangup scripts */
#define LINE_LEN SCRIPT_LEN 	/* Size of lines */
#define DEVICE_LEN SCRIPT_LEN	/* Size of individual devices */
#define AUTHLEN 100 		/* Size of authname/authkey */
#define CHAPDIGESTLEN 100	/* Maximum chap digest */
#define CHAPCHALLENGELEN 48	/* Maximum chap challenge */
#define CHAPAUTHRESPONSELEN 48	/* Maximum chap authresponse (chap81) */
#define MAXARGS 40		/* How many args per config line */
#define NCP_IDLE_TIMEOUT 180	/* Drop all links */
#define CHOKED_TIMEOUT 120	/* Delete queued packets w/ blocked tun */

#define MIN_LQRPERIOD 1		/* Minimum LQR frequency */
#define DEF_LQRPERIOD 30	/* Default LQR frequency */
#define MIN_FSMRETRY 1		/* Minimum FSM retry frequency */
#define DEF_FSMRETRY 3		/* FSM retry frequency */
#define DEF_FSMTRIES 5		/* Default max retries */
#define DEF_FSMAUTHTRIES 3	/* Default max auth retries */
#define DEF_IFQUEUE 30		/* Default interface queue size */

#define	CONFFILE 	"ppp.conf"
#define	LINKUPFILE 	"ppp.linkup"
#define	LINKDOWNFILE 	"ppp.linkdown"
#define	SECRETFILE	"ppp.secret"

#define	EX_SIG		-1
#define	EX_NORMAL	0
#define	EX_START	1
#define	EX_SOCK		2
#define	EX_MODEM	3
#define	EX_DIAL		4
#define	EX_DEAD		5
#define	EX_DONE		6
#define	EX_REBOOT	7
#define	EX_ERRDEAD	8
#define	EX_HANGUP	9
#define	EX_TERM		10
#define EX_NODIAL	11
#define EX_NOLOGIN	12
/* return values for -background mode, not really exits */
#define EX_REDIAL	13
#define EX_RECONNECT	14

/* physical::type values (OR'd in bundle::phys_type) */
#define PHYS_NONE		0
#define PHYS_INTERACTIVE	1  /* Manual link */
#define PHYS_AUTO		2  /* Dial-on-demand link */
#define	PHYS_DIRECT		4  /* Incoming link, deleted when closed */
#define	PHYS_DEDICATED		8  /* Dedicated link */
#define	PHYS_DDIAL		16 /* Dial immediately, stay connected */
#define PHYS_BACKGROUND		32 /* Dial immediately, deleted when closed */
#define PHYS_FOREGROUND		64 /* Pseudo mode, same as background */
#define PHYS_ALL		127

/* flags passed to findblank() and MakeArgs() */
#define PARSE_NORMAL	0
#define PARSE_REDUCE	1
#define PARSE_NOHASH	2

/* flags passed to loadmodules */
#define	LOAD_QUIETLY	1
#define	LOAD_VERBOSLY	2

#define ROUNDUP(x) ((x) ? (1 + (((x) - 1) | (sizeof(long) - 1))) : sizeof(long))

#define NCP_ASCIIBUFFERSIZE	52

#ifdef __NetBSD__
extern void randinit(void);
#else
#define random arc4random
#define randinit()
#endif

extern ssize_t fullread(int, void *, size_t);
extern const char *mode2Nam(int);
extern int Nam2mode(const char *);
extern struct in_addr GetIpAddr(const char *);
extern unsigned SpeedToUnsigned(speed_t);
extern speed_t UnsignedToSpeed(unsigned);
extern char *findblank(char *, int);
extern int MakeArgs(char *, char **, int, int);
extern const char *NumStr(long, char *, size_t);
extern const char *HexStr(long, char *, size_t);
extern const char *ex_desc(int);
extern void SetTitle(const char *);
extern fd_set *mkfdset(void);
extern void zerofdset(fd_set *);
extern void Concatinate(char *, size_t, int, const char *const *);
extern int loadmodules(int, const char *, ...);
