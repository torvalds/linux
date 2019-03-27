/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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

#ifndef lint
#if 0
static char sccsid[] = "@(#)from: subr.c	8.1 (Berkeley) 6/4/93";
#endif
static const char rcsid[] =
  "$FreeBSD$";
#endif /* not lint */

/*
 * Melbourne getty.
 */
#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/time.h>

#include <poll.h>
#include <regex.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <termios.h>
#include <unistd.h>

#include "gettytab.h"
#include "pathnames.h"
#include "extern.h"

/*
 * Get a table entry.
 */
void
gettable(const char *name, char *buf)
{
	struct gettystrs *sp;
	struct gettynums *np;
	struct gettyflags *fp;
	long n;
	int l;
	char *p;
	static char path_gettytab[PATH_MAX];
	char *dba[2];

	static int firsttime = 1;

	strlcpy(path_gettytab, _PATH_GETTYTAB, sizeof(path_gettytab));
	dba[0] = path_gettytab;
	dba[1] = NULL;

	if (firsttime) {
		/*
		 * we need to strdup() anything in the strings array
		 * initially in order to simplify things later
		 */
		for (sp = gettystrs; sp->field; sp++)
			if (sp->value != NULL) {
				/* handle these ones more carefully */
				if (sp >= &gettystrs[4] && sp <= &gettystrs[6])
					l = 2;
				else
					l = strlen(sp->value) + 1;
				if ((p = malloc(l)) != NULL) {
					strncpy(p, sp->value, l);
					p[l-1] = '\0';
				}
				/*
				 * replace, even if NULL, else we'll
				 * have problems with free()ing static mem
				 */
				sp->value = p;
			}
		firsttime = 0;
	}

	switch (cgetent(&buf, dba, name)) {
	case 1:
		syslog(LOG_ERR, "getty: couldn't resolve 'tc=' in gettytab '%s'", name);
		return;
	case 0:
		break;
	case -1:
		syslog(LOG_ERR, "getty: unknown gettytab entry '%s'", name);
		return;
	case -2:
		syslog(LOG_ERR, "getty: retrieving gettytab entry '%s': %m", name);
		return;
	case -3:
		syslog(LOG_ERR, "getty: recursive 'tc=' reference gettytab entry '%s'", name);
		return;
	default:
		syslog(LOG_ERR, "getty: unexpected cgetent() error for entry '%s'", name);
		return;
	}

	for (sp = gettystrs; sp->field; sp++) {
		if ((l = cgetstr(buf, sp->field, &p)) >= 0) {
			if (sp->value) {
				/* prefer existing value */
				if (strcmp(p, sp->value) != 0)
					free(sp->value);
				else {
					free(p);
					p = sp->value;
				}
			}
			sp->value = p;
		} else if (l == -1) {
			free(sp->value);
			sp->value = NULL;
		}
	}

	for (np = gettynums; np->field; np++) {
		if (cgetnum(buf, np->field, &n) == -1)
			np->set = 0;
		else {
			np->set = 1;
			np->value = n;
		}
	}

	for (fp = gettyflags; fp->field; fp++) {
		if (cgetcap(buf, fp->field, ':') == NULL)
			fp->set = 0;
		else {
			fp->set = 1;
			fp->value = 1 ^ fp->invrt;
		}
	}
}

void
gendefaults(void)
{
	struct gettystrs *sp;
	struct gettynums *np;
	struct gettyflags *fp;

	for (sp = gettystrs; sp->field; sp++)
		if (sp->value)
			sp->defalt = strdup(sp->value);
	for (np = gettynums; np->field; np++)
		if (np->set)
			np->defalt = np->value;
	for (fp = gettyflags; fp->field; fp++)
		if (fp->set)
			fp->defalt = fp->value;
		else
			fp->defalt = fp->invrt;
}

void
setdefaults(void)
{
	struct gettystrs *sp;
	struct gettynums *np;
	struct gettyflags *fp;

	for (sp = gettystrs; sp->field; sp++)
		if (!sp->value)
			sp->value = !sp->defalt ? sp->defalt
						: strdup(sp->defalt);
	for (np = gettynums; np->field; np++)
		if (!np->set)
			np->value = np->defalt;
	for (fp = gettyflags; fp->field; fp++)
		if (!fp->set)
			fp->value = fp->defalt;
}

static char **
charnames[] = {
	&ER, &KL, &IN, &QU, &XN, &XF, &ET, &BK,
	&SU, &DS, &RP, &FL, &WE, &LN, 0
};

static char *
charvars[] = {
	&tmode.c_cc[VERASE], &tmode.c_cc[VKILL], &tmode.c_cc[VINTR],
	&tmode.c_cc[VQUIT], &tmode.c_cc[VSTART], &tmode.c_cc[VSTOP],
	&tmode.c_cc[VEOF], &tmode.c_cc[VEOL], &tmode.c_cc[VSUSP],
	&tmode.c_cc[VDSUSP], &tmode.c_cc[VREPRINT], &tmode.c_cc[VDISCARD],
	&tmode.c_cc[VWERASE], &tmode.c_cc[VLNEXT], 0
};

void
setchars(void)
{
	int i;
	const char *p;

	for (i = 0; charnames[i]; i++) {
		p = *charnames[i];
		if (p && *p)
			*charvars[i] = *p;
		else
			*charvars[i] = _POSIX_VDISABLE;
	}
}

/* Macros to clear/set/test flags. */
#define	SET(t, f)	(t) |= (f)
#define	CLR(t, f)	(t) &= ~(f)
#define	ISSET(t, f)	((t) & (f))

void
set_flags(int n)
{
	tcflag_t iflag, oflag, cflag, lflag;


	switch (n) {
	case 0:
		if (C0set && I0set && L0set && O0set) {
			tmode.c_cflag = C0;
			tmode.c_iflag = I0;
			tmode.c_lflag = L0;
			tmode.c_oflag = O0;
			return;
		}
		break;
	case 1:
		if (C1set && I1set && L1set && O1set) {
			tmode.c_cflag = C1;
			tmode.c_iflag = I1;
			tmode.c_lflag = L1;
			tmode.c_oflag = O1;
			return;
		}
		break;
	default:
		if (C2set && I2set && L2set && O2set) {
			tmode.c_cflag = C2;
			tmode.c_iflag = I2;
			tmode.c_lflag = L2;
			tmode.c_oflag = O2;
			return;
		}
		break;
	}

	iflag = omode.c_iflag;
	oflag = omode.c_oflag;
	cflag = omode.c_cflag;
	lflag = omode.c_lflag;

	if (NP) {
		CLR(cflag, CSIZE|PARENB);
		SET(cflag, CS8);
		CLR(iflag, ISTRIP|INPCK|IGNPAR);
	} else if (AP || EP || OP) {
		CLR(cflag, CSIZE);
		SET(cflag, CS7|PARENB);
		SET(iflag, ISTRIP);
		if (OP && !EP) {
			SET(iflag, INPCK|IGNPAR);
			SET(cflag, PARODD);
			if (AP)
				CLR(iflag, INPCK);
		} else if (EP && !OP) {
			SET(iflag, INPCK|IGNPAR);
			CLR(cflag, PARODD);
			if (AP)
				CLR(iflag, INPCK);
		} else if (AP || (EP && OP)) {
			CLR(iflag, INPCK|IGNPAR);
			CLR(cflag, PARODD);
		}
	} /* else, leave as is */

#if 0
	if (UC)
		f |= LCASE;
#endif

	if (HC)
		SET(cflag, HUPCL);
	else
		CLR(cflag, HUPCL);

	if (MB)
		SET(cflag, MDMBUF);
	else
		CLR(cflag, MDMBUF);

	if (HW)
		SET(cflag, CRTSCTS);
	else
		CLR(cflag, CRTSCTS);

	if (NL) {
		SET(iflag, ICRNL);
		SET(oflag, ONLCR|OPOST);
	} else {
		CLR(iflag, ICRNL);
		CLR(oflag, ONLCR);
	}

	if (!HT)
		SET(oflag, OXTABS|OPOST);
	else
		CLR(oflag, OXTABS);

#ifdef XXX_DELAY
	SET(f, delaybits());
#endif

	if (n == 1) {		/* read mode flags */
		if (RW) {
			iflag = 0;
			CLR(oflag, OPOST);
			CLR(cflag, CSIZE|PARENB);
			SET(cflag, CS8);
			lflag = 0;
		} else {
			CLR(lflag, ICANON);
		}
		goto out;
	}

	if (n == 0)
		goto out;

#if 0
	if (CB)
		SET(f, CRTBS);
#endif

	if (CE)
		SET(lflag, ECHOE);
	else
		CLR(lflag, ECHOE);

	if (CK)
		SET(lflag, ECHOKE);
	else
		CLR(lflag, ECHOKE);

	if (PE)
		SET(lflag, ECHOPRT);
	else
		CLR(lflag, ECHOPRT);

	if (EC)
		SET(lflag, ECHO);
	else
		CLR(lflag, ECHO);

	if (XC)
		SET(lflag, ECHOCTL);
	else
		CLR(lflag, ECHOCTL);

	if (DX)
		SET(lflag, IXANY);
	else
		CLR(lflag, IXANY);

out:
	tmode.c_iflag = iflag;
	tmode.c_oflag = oflag;
	tmode.c_cflag = cflag;
	tmode.c_lflag = lflag;
}


#ifdef XXX_DELAY
struct delayval {
	unsigned	delay;		/* delay in ms */
	int		bits;
};

/*
 * below are random guesses, I can't be bothered checking
 */

struct delayval	crdelay[] = {
	{ 1,		CR1 },
	{ 2,		CR2 },
	{ 3,		CR3 },
	{ 83,		CR1 },
	{ 166,		CR2 },
	{ 0,		CR3 },
};

struct delayval nldelay[] = {
	{ 1,		NL1 },		/* special, calculated */
	{ 2,		NL2 },
	{ 3,		NL3 },
	{ 100,		NL2 },
	{ 0,		NL3 },
};

struct delayval	bsdelay[] = {
	{ 1,		BS1 },
	{ 0,		0 },
};

struct delayval	ffdelay[] = {
	{ 1,		FF1 },
	{ 1750,		FF1 },
	{ 0,		FF1 },
};

struct delayval	tbdelay[] = {
	{ 1,		TAB1 },
	{ 2,		TAB2 },
	{ 3,		XTABS },	/* this is expand tabs */
	{ 100,		TAB1 },
	{ 0,		TAB2 },
};

int
delaybits(void)
{
	int f;

	f  = adelay(CD, crdelay);
	f |= adelay(ND, nldelay);
	f |= adelay(FD, ffdelay);
	f |= adelay(TD, tbdelay);
	f |= adelay(BD, bsdelay);
	return (f);
}

int
adelay(int ms, struct delayval *dp)
{
	if (ms == 0)
		return (0);
	while (dp->delay && ms > dp->delay)
		dp++;
	return (dp->bits);
}
#endif

char	editedhost[MAXHOSTNAMELEN];

void
edithost(const char *pattern)
{
	regex_t regex;
	regmatch_t *match;
	int found;

	if (pattern == NULL || *pattern == '\0')
		goto copyasis;
	if (regcomp(&regex, pattern, REG_EXTENDED) != 0)
		goto copyasis;

	match = calloc(regex.re_nsub + 1, sizeof(*match));
	if (match == NULL) {
		regfree(&regex);
		goto copyasis;
	}

	found = !regexec(&regex, HN, regex.re_nsub + 1, match, 0);
	if (found) {
		size_t subex, totalsize;

		/*
		 * We found a match.  If there were no parenthesized
		 * subexpressions in the pattern, use entire matched
		 * string as ``editedhost''; otherwise use the first
		 * matched subexpression.
		 */
		subex = !!regex.re_nsub;
		totalsize = match[subex].rm_eo - match[subex].rm_so + 1;
		strlcpy(editedhost, HN + match[subex].rm_so, totalsize >
		    sizeof(editedhost) ? sizeof(editedhost) : totalsize);
	}
	free(match);
	regfree(&regex);
	if (found)
		return;
	/*
	 * In case of any errors, or if the pattern did not match, pass
	 * the original hostname as is.
	 */
 copyasis:
	strlcpy(editedhost, HN, sizeof(editedhost));
}

static struct speedtab {
	int	speed;
	int	uxname;
} speedtab[] = {
	{ 50,	B50 },
	{ 75,	B75 },
	{ 110,	B110 },
	{ 134,	B134 },
	{ 150,	B150 },
	{ 200,	B200 },
	{ 300,	B300 },
	{ 600,	B600 },
	{ 1200,	B1200 },
	{ 1800,	B1800 },
	{ 2400,	B2400 },
	{ 4800,	B4800 },
	{ 9600,	B9600 },
	{ 19200, EXTA },
	{ 19,	EXTA },		/* for people who say 19.2K */
	{ 38400, EXTB },
	{ 38,	EXTB },
	{ 7200,	EXTB },		/* alternative */
	{ 57600, B57600 },
	{ 115200, B115200 },
	{ 230400, B230400 },
	{ 0, 0 }
};

int
speed(int val)
{
	struct speedtab *sp;

	if (val <= B230400)
		return (val);

	for (sp = speedtab; sp->speed; sp++)
		if (sp->speed == val)
			return (sp->uxname);

	return (B300);		/* default in impossible cases */
}

void
makeenv(char *env[])
{
	static char termbuf[128] = "TERM=";
	char *p, *q;
	char **ep;

	ep = env;
	if (TT && *TT) {
		strlcat(termbuf, TT, sizeof(termbuf));
		*ep++ = termbuf;
	}
	if ((p = EV)) {
		q = p;
		while ((q = strchr(q, ','))) {
			*q++ = '\0';
			*ep++ = p;
			p = q;
		}
		if (*p)
			*ep++ = p;
	}
	*ep = (char *)0;
}

/*
 * This speed select mechanism is written for the Develcon DATASWITCH.
 * The Develcon sends a string of the form "B{speed}\n" at a predefined
 * baud rate. This string indicates the user's actual speed.
 * The routine below returns the terminal type mapped from derived speed.
 */
static struct	portselect {
	const char	*ps_baud;
	const char	*ps_type;
} portspeeds[] = {
	{ "B110",	"std.110" },
	{ "B134",	"std.134" },
	{ "B150",	"std.150" },
	{ "B300",	"std.300" },
	{ "B600",	"std.600" },
	{ "B1200",	"std.1200" },
	{ "B2400",	"std.2400" },
	{ "B4800",	"std.4800" },
	{ "B9600",	"std.9600" },
	{ "B19200",	"std.19200" },
	{ NULL, NULL }
};

const char *
portselector(void)
{
	char c, baud[20];
	const char *type = "default";
	struct portselect *ps;
	size_t len;

	alarm(5*60);
	for (len = 0; len < sizeof (baud) - 1; len++) {
		if (read(STDIN_FILENO, &c, 1) <= 0)
			break;
		c &= 0177;
		if (c == '\n' || c == '\r')
			break;
		if (c == 'B')
			len = 0;	/* in case of leading garbage */
		baud[len] = c;
	}
	baud[len] = '\0';
	for (ps = portspeeds; ps->ps_baud; ps++)
		if (strcmp(ps->ps_baud, baud) == 0) {
			type = ps->ps_type;
			break;
		}
	sleep(2);	/* wait for connection to complete */
	return (type);
}

/*
 * This auto-baud speed select mechanism is written for the Micom 600
 * portselector. Selection is done by looking at how the character '\r'
 * is garbled at the different speeds.
 */
const char *
autobaud(void)
{
	struct pollfd set[1];
	struct timespec timeout;
	char c;
	const char *type = "9600-baud";

	(void)tcflush(0, TCIOFLUSH);
	set[0].fd = STDIN_FILENO;
	set[0].events = POLLIN;
	if (poll(set, 1, 5000) <= 0)
		return (type);
	if (read(STDIN_FILENO, &c, sizeof(char)) != sizeof(char))
		return (type);
	timeout.tv_sec = 0;
	timeout.tv_nsec = 20000;
	(void)nanosleep(&timeout, NULL);
	(void)tcflush(0, TCIOFLUSH);
	switch (c & 0377) {

	case 0200:		/* 300-baud */
		type = "300-baud";
		break;

	case 0346:		/* 1200-baud */
		type = "1200-baud";
		break;

	case  015:		/* 2400-baud */
	case 0215:
		type = "2400-baud";
		break;

	default:		/* 4800-baud */
		type = "4800-baud";
		break;

	case 0377:		/* 9600-baud */
		type = "9600-baud";
		break;
	}
	return (type);
}
