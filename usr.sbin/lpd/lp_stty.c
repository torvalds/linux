/* $OpenBSD: lp_stty.c,v 1.1.1.1 2018/04/27 16:14:36 eric Exp $ */

/*
 * Adapted from the following files in src/usr.sbin/lpr/lpd:
 *
 * extern.h,v 1.9 2015/09/29 02:37:29
 * key.c,v 1.8 2015/01/16 06:40:18 
 * modes.c,v 1.8 2015/01/16 06:40:18
 * printjob.c,v 1.58 2016/11/22 16:03:57
 */

static const char *printer;

/*-
 *
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1991, 1993, 1994
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

#include <sys/types.h>
#include <sys/ioctl.h>

#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <limits.h>
#include <termios.h>

#include "lp.h"
#include "log.h"

/*
 * from extern.h
 */

struct info {
	int fd;					/* file descriptor */
	int ldisc;				/* line discipline */
	int off;				/* turn off */
	int set;				/* need set */
	int wset;				/* need window set */
	char *arg;				/* argument */
	struct termios t;			/* terminal info */
	struct winsize win;			/* window info */
};


/*
 * from key.c
 */

static int  c_key(const void *, const void *);
static void f_cbreak(struct info *);
static void f_columns(struct info *);
static void f_dec(struct info *);
static void f_extproc(struct info *);
static void f_ispeed(struct info *);
static void f_nl(struct info *);
static void f_ospeed(struct info *);
static void f_raw(struct info *);
static void f_rows(struct info *);
static void f_sane(struct info *);
static void f_tty(struct info *);

static struct key {
	char *name;				/* name */
	void (*f)(struct info *);		/* function */
#define	F_NEEDARG	0x01			/* needs an argument */
#define	F_OFFOK		0x02			/* can turn off */
	int flags;
} const keys[] = {
	{ "cbreak",	f_cbreak,	F_OFFOK },
	{ "cols",	f_columns,	F_NEEDARG },
	{ "columns",	f_columns,	F_NEEDARG },
	{ "cooked", 	f_sane,		0 },
	{ "dec",	f_dec,		0 },
	{ "extproc",	f_extproc,	F_OFFOK },
	{ "ispeed",	f_ispeed,	F_NEEDARG },
	{ "new",	f_tty,		0 },
	{ "nl",		f_nl,		F_OFFOK },
	{ "old",	f_tty,		0 },
	{ "ospeed",	f_ospeed,	F_NEEDARG },
	{ "raw",	f_raw,		F_OFFOK },
	{ "rows",	f_rows,		F_NEEDARG },
	{ "sane",	f_sane,		0 },
	{ "tty",	f_tty,		0 },
};

static int
c_key(const void *a, const void *b)
{

        return (strcmp(((const struct key *)a)->name,
	    ((const struct key *)b)->name));
}

static int
ksearch(char ***argvp, struct info *ip)
{
	char *name;
	struct key *kp, tmp;

	name = **argvp;
	if (*name == '-') {
		ip->off = 1;
		++name;
	} else
		ip->off = 0;

	tmp.name = name;
	if (!(kp = (struct key *)bsearch(&tmp, keys,
	    sizeof(keys)/sizeof(struct key), sizeof(struct key), c_key)))
		return (0);
	if (!(kp->flags & F_OFFOK) && ip->off) {
		log_warnx("%s: illegal option: %s", printer, name);
		return (1);
	}
	if (kp->flags & F_NEEDARG && !(ip->arg = *++*argvp)) {
		log_warnx("%s: option requires an argument: %s", printer, name);
		return (1);
	}
	kp->f(ip);
	return (1);
}

static void
f_cbreak(struct info *ip)
{

	if (ip->off)
		f_sane(ip);
	else {
		ip->t.c_iflag |= BRKINT|IXON|IMAXBEL;
		ip->t.c_oflag |= OPOST;
		ip->t.c_lflag |= ISIG|IEXTEN;
		ip->t.c_lflag &= ~ICANON;
		ip->set = 1;
	}
}

static void
f_columns(struct info *ip)
{

	ip->win.ws_col = atoi(ip->arg);
	ip->wset = 1;
}

static void
f_dec(struct info *ip)
{

	ip->t.c_cc[VERASE] = (u_char)0177;
	ip->t.c_cc[VKILL] = CTRL('u');
	ip->t.c_cc[VINTR] = CTRL('c');
	ip->t.c_lflag &= ~ECHOPRT;
	ip->t.c_lflag |= ECHOE|ECHOKE|ECHOCTL;
	ip->t.c_iflag &= ~IXANY;
	ip->set = 1;
}

static void
f_extproc(struct info *ip)
{

	if (ip->set) {
		int tmp = 1;
		(void)ioctl(ip->fd, TIOCEXT, &tmp);
	} else {
		int tmp = 0;
		(void)ioctl(ip->fd, TIOCEXT, &tmp);
	}
}

static void
f_ispeed(struct info *ip)
{

	cfsetispeed(&ip->t, atoi(ip->arg));
	ip->set = 1;
}

static void
f_nl(struct info *ip)
{

	if (ip->off) {
		ip->t.c_iflag |= ICRNL;
		ip->t.c_oflag |= ONLCR;
	} else {
		ip->t.c_iflag &= ~ICRNL;
		ip->t.c_oflag &= ~ONLCR;
	}
	ip->set = 1;
}

static void
f_ospeed(struct info *ip)
{

	cfsetospeed(&ip->t, atoi(ip->arg));
	ip->set = 1;
}

static void
f_raw(struct info *ip)
{

	if (ip->off)
		f_sane(ip);
	else {
		cfmakeraw(&ip->t);
		ip->t.c_cflag &= ~(CSIZE|PARENB);
		ip->t.c_cflag |= CS8;
		ip->set = 1;
	}
}

static void
f_rows(struct info *ip)
{

	ip->win.ws_row = atoi(ip->arg);
	ip->wset = 1;
}

static void
f_sane(struct info *ip)
{

	ip->t.c_cflag = TTYDEF_CFLAG | (ip->t.c_cflag & (CLOCAL|CRTSCTS));
	ip->t.c_iflag = TTYDEF_IFLAG;
	ip->t.c_iflag |= ICRNL;
	/* preserve user-preference flags in lflag */
#define	LKEEP	(ECHOKE|ECHOE|ECHOK|ECHOPRT|ECHOCTL|ALTWERASE|TOSTOP|NOFLSH)
	ip->t.c_lflag = TTYDEF_LFLAG | (ip->t.c_lflag & LKEEP);
	ip->t.c_oflag = TTYDEF_OFLAG;
	ip->set = 1;
}

static void
f_tty(struct info *ip)
{
	int tmp;

	tmp = TTYDISC;
	if (ioctl(0, TIOCSETD, &tmp) == -1)
		log_warn("%s: ioctl(TIOCSETD)", printer);
}

/*
 * from key.c
 */

struct modes {
	char *name;
	long set;
	long unset;
};

/*
 * The code in optlist() depends on minus options following regular
 * options, i.e. "foo" must immediately precede "-foo".
 */
const struct modes cmodes[] = {
	{ "cs5",	CS5, CSIZE },
	{ "cs6",	CS6, CSIZE },
	{ "cs7",	CS7, CSIZE },
	{ "cs8",	CS8, CSIZE },
	{ "cstopb",	CSTOPB, 0 },
	{ "-cstopb",	0, CSTOPB },
	{ "cread",	CREAD, 0 },
	{ "-cread",	0, CREAD },
	{ "parenb",	PARENB, 0 },
	{ "-parenb",	0, PARENB },
	{ "parodd",	PARODD, 0 },
	{ "-parodd",	0, PARODD },
	{ "parity",	PARENB | CS7, PARODD | CSIZE },
	{ "-parity",	CS8, PARODD | PARENB | CSIZE },
	{ "evenp",	PARENB | CS7, PARODD | CSIZE },
	{ "-evenp",	CS8, PARODD | PARENB | CSIZE },
	{ "oddp",	PARENB | CS7 | PARODD, CSIZE },
	{ "-oddp",	CS8, PARODD | PARENB | CSIZE },
	{ "pass8",	CS8, PARODD | PARENB | CSIZE },
	{ "-pass8",	PARENB | CS7, PARODD | CSIZE },
	{ "hupcl",	HUPCL, 0 },
	{ "-hupcl",	0, HUPCL },
	{ "hup",	HUPCL, 0 },
	{ "-hup",	0, HUPCL },
	{ "clocal",	CLOCAL, 0 },
	{ "-clocal",	0, CLOCAL },
	{ "crtscts",	CRTSCTS, 0 },
	{ "-crtscts",	0, CRTSCTS },
	{ "mdmbuf",	MDMBUF, 0 },
	{ "-mdmbuf",	0, MDMBUF },
	{ NULL },
};

const struct modes imodes[] = {
	{ "ignbrk",	IGNBRK, 0 },
	{ "-ignbrk",	0, IGNBRK },
	{ "brkint",	BRKINT, 0 },
	{ "-brkint",	0, BRKINT },
	{ "ignpar",	IGNPAR, 0 },
	{ "-ignpar",	0, IGNPAR },
	{ "parmrk",	PARMRK, 0 },
	{ "-parmrk",	0, PARMRK },
	{ "inpck",	INPCK, 0 },
	{ "-inpck",	0, INPCK },
	{ "istrip",	ISTRIP, 0 },
	{ "-istrip",	0, ISTRIP },
	{ "inlcr",	INLCR, 0 },
	{ "-inlcr",	0, INLCR },
	{ "igncr",	IGNCR, 0 },
	{ "-igncr",	0, IGNCR },
	{ "icrnl",	ICRNL, 0 },
	{ "-icrnl",	0, ICRNL },
	{ "iuclc",	IUCLC, 0 },
	{ "-iuclc",	0, IUCLC },
	{ "ixon",	IXON, 0 },
	{ "-ixon",	0, IXON },
	{ "flow",	IXON, 0 },
	{ "-flow",	0, IXON },
	{ "ixoff",	IXOFF, 0 },
	{ "-ixoff",	0, IXOFF },
	{ "tandem",	IXOFF, 0 },
	{ "-tandem",	0, IXOFF },
	{ "ixany",	IXANY, 0 },
	{ "-ixany",	0, IXANY },
	{ "decctlq",	0, IXANY },
	{ "-decctlq",	IXANY, 0 },
	{ "imaxbel",	IMAXBEL, 0 },
	{ "-imaxbel",	0, IMAXBEL },
	{ NULL },
};

const struct modes lmodes[] = {
	{ "echo",	ECHO, 0 },
	{ "-echo",	0, ECHO },
	{ "echoe",	ECHOE, 0 },
	{ "-echoe",	0, ECHOE },
	{ "crterase",	ECHOE, 0 },
	{ "-crterase",	0, ECHOE },
	{ "crtbs",	ECHOE, 0 },	/* crtbs not supported, close enough */
	{ "-crtbs",	0, ECHOE },
	{ "echok",	ECHOK, 0 },
	{ "-echok",	0, ECHOK },
	{ "echoke",	ECHOKE, 0 },
	{ "-echoke",	0, ECHOKE },
	{ "crtkill",	ECHOKE, 0 },
	{ "-crtkill",	0, ECHOKE },
	{ "altwerase",	ALTWERASE, 0 },
	{ "-altwerase",	0, ALTWERASE },
	{ "iexten",	IEXTEN, 0 },
	{ "-iexten",	0, IEXTEN },
	{ "echonl",	ECHONL, 0 },
	{ "-echonl",	0, ECHONL },
	{ "echoctl",	ECHOCTL, 0 },
	{ "-echoctl",	0, ECHOCTL },
	{ "ctlecho",	ECHOCTL, 0 },
	{ "-ctlecho",	0, ECHOCTL },
	{ "echoprt",	ECHOPRT, 0 },
	{ "-echoprt",	0, ECHOPRT },
	{ "prterase",	ECHOPRT, 0 },
	{ "-prterase",	0, ECHOPRT },
	{ "isig",	ISIG, 0 },
	{ "-isig",	0, ISIG },
	{ "icanon",	ICANON, 0 },
	{ "-icanon",	0, ICANON },
	{ "noflsh",	NOFLSH, 0 },
	{ "-noflsh",	0, NOFLSH },
	{ "tostop",	TOSTOP, 0 },
	{ "-tostop",	0, TOSTOP },
	{ "flusho",	FLUSHO, 0 },
	{ "-flusho",	0, FLUSHO },
	{ "pendin",	PENDIN, 0 },
	{ "-pendin",	0, PENDIN },
	{ "crt",	ECHOE|ECHOKE|ECHOCTL, ECHOK|ECHOPRT },
	{ "-crt",	ECHOK, ECHOE|ECHOKE|ECHOCTL },
	{ "newcrt",	ECHOE|ECHOKE|ECHOCTL, ECHOK|ECHOPRT },
	{ "-newcrt",	ECHOK, ECHOE|ECHOKE|ECHOCTL },
	{ "nokerninfo",	NOKERNINFO, 0 },
	{ "-nokerninfo",0, NOKERNINFO },
	{ "kerninfo",	0, NOKERNINFO },
	{ "-kerninfo",	NOKERNINFO, 0 },
	{ "xcase",	XCASE, 0 },
	{ "-xcase",	0, XCASE },
	{ NULL },
};

const struct modes omodes[] = {
	{ "opost",	OPOST, 0 },
	{ "-opost",	0, OPOST },
	{ "litout",	0, OPOST },
	{ "-litout",	OPOST, 0 },
	{ "ocrnl",	OCRNL, 0 },
	{ "-ocrnl",	0, OCRNL },
	{ "olcuc",	OLCUC, 0 },
	{ "-olcuc",	0, OLCUC },
	{ "onlcr",	ONLCR, 0 },
	{ "-onlcr",	0, ONLCR },
	{ "onlret",	ONLRET, 0 },
	{ "-onlret",	0, ONLRET },
	{ "onocr",	ONOCR, 0 },
	{ "-onocr",	0, ONOCR },
	{ "tabs",	0, OXTABS },		/* "preserve" tabs */
	{ "-tabs",	OXTABS, 0 },
	{ "oxtabs",	OXTABS, 0 },
	{ "-oxtabs",	0, OXTABS },
	{ NULL },
};

#define	CHK(s)	(*name == s[0] && !strcmp(name, s))

static int
msearch(char ***argvp, struct info *ip)
{
	const struct modes *mp;
	char *name;

	name = **argvp;

	for (mp = cmodes; mp->name; ++mp)
		if (CHK(mp->name)) {
			ip->t.c_cflag &= ~mp->unset;
			ip->t.c_cflag |= mp->set;
			ip->set = 1;
			return (1);
		}
	for (mp = imodes; mp->name; ++mp)
		if (CHK(mp->name)) {
			ip->t.c_iflag &= ~mp->unset;
			ip->t.c_iflag |= mp->set;
			ip->set = 1;
			return (1);
		}
	for (mp = lmodes; mp->name; ++mp)
		if (CHK(mp->name)) {
			ip->t.c_lflag &= ~mp->unset;
			ip->t.c_lflag |= mp->set;
			ip->set = 1;
			return (1);
		}
	for (mp = omodes; mp->name; ++mp)
		if (CHK(mp->name)) {
			ip->t.c_oflag &= ~mp->unset;
			ip->t.c_oflag |= mp->set;
			ip->set = 1;
			return (1);
		}
	return (0);
}

/*
 * from prinjob.c
 */

void
lp_stty(struct lp_printer *lp, int fd)
{
	struct info i;
	char **argv, **ap, **ep, *p, *val;

	printer = lp->lp_name;

	i.fd = fd;
	i.set = i.wset = 0;
	if (ioctl(i.fd, TIOCEXCL, (char *)0) == -1)
		fatal("%s: ioctl(TIOCEXCL)", printer);

	if (tcgetattr(i.fd, &i.t) == -1)
		fatal("%s: tcgetattr", printer);

	if (lp->lp_br > 0) {
		cfsetspeed(&i.t, lp->lp_br);
		i.set = 1;
	}
	if (lp->lp_ms) {
		if (ioctl(i.fd, TIOCGETD, &i.ldisc) == -1)
			fatal("%s: ioctl(TIOCGETD)", printer);

		if (ioctl(i.fd, TIOCGWINSZ, &i.win) == -1)
			log_warn("%s: ioctl(TIOCGWINSZ)", printer);

		argv = calloc(256, sizeof(char *));
		if (argv == NULL)
			fatal("%s: malloc", printer);

		p = strdup(lp->lp_ms);
		ap = argv;
		ep = argv + 255;
		while ((val = strsep(&p, " \t,")) != NULL) {
			if ((*ap++ = strdup(val)) == NULL)
				fatal("%s: strdup", printer);
			if (ap == ep)
				fatal("%s: too many \"ms\" entries", printer);
		}
		*ap = NULL;

		for (; *argv; ++argv) {
			if (ksearch(&argv, &i))
				continue;
			if (msearch(&argv, &i))
				continue;
			log_warnx("%s: unknown stty flag: %s", printer, *argv);
		}
	}

	if (i.set && tcsetattr(i.fd, TCSANOW, &i.t) == -1)
		fatal("%s: tcsetattr", printer);

	if (i.wset && ioctl(i.fd, TIOCSWINSZ, &i.win) == -1)
		log_warn("%s: ioctl(TIOCSWINSZ)", printer);
}
