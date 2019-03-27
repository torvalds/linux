/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1980, 1991, 1993
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

#include <sys/cdefs.h>

__FBSDID("$FreeBSD$");

#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1980, 1991, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif

#ifndef lint
static const char sccsid[] = "@(#)tset.c	8.1 (Berkeley) 6/9/93";
#endif

#include <sys/types.h>
#include <sys/ioctl.h>

#include <ctype.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termcap.h>
#include <termios.h>
#include <unistd.h>

#include "extern.h"

void	obsolete(char *[]);
void	report(const char *, int, u_int);
void	usage(void);

struct termios mode, oldmode;

int	erasech;		/* new erase character */
int	intrchar;		/* new interrupt character */
int	isreset;		/* invoked as reset */
int	killch;			/* new kill character */
int	Lines, Columns;		/* window size */
speed_t	Ospeed;

int
main(int argc, char *argv[])
{
#ifdef TIOCGWINSZ
	struct winsize win;
#endif
	int ch, noinit, noset, quiet, Sflag, sflag, showterm, usingupper;
	char *p, *tcapbuf;
	const char *ttype;

	if (tcgetattr(STDERR_FILENO, &mode) < 0)
		err(1, "standard error");

	oldmode = mode;
	Ospeed = cfgetospeed(&mode);

	if ((p = strrchr(*argv, '/')))
		++p;
	else
		p = *argv;
	usingupper = isupper(*p);
	if (!strcasecmp(p, "reset")) {
		isreset = 1;
		reset_mode();
	}

	obsolete(argv);
	noinit = noset = quiet = Sflag = sflag = showterm = 0;
	while ((ch = getopt(argc, argv, "-a:d:e:Ii:k:m:np:QSrs")) != -1) {
		switch (ch) {
		case '-':		/* display term only */
			noset = 1;
			break;
		case 'a':		/* OBSOLETE: map identifier to type */
			add_mapping("arpanet", optarg);
			break;
		case 'd':		/* OBSOLETE: map identifier to type */
			add_mapping("dialup", optarg);
			break;
		case 'e':		/* erase character */
			erasech = optarg[0] == '^' && optarg[1] != '\0' ?
			    optarg[1] == '?' ? '\177' : CTRL(optarg[1]) :
			    optarg[0];
			break;
		case 'I':		/* no initialization strings */
			noinit = 1;
			break;
		case 'i':		/* interrupt character */
			intrchar = optarg[0] == '^' && optarg[1] != '\0' ?
			    optarg[1] == '?' ? '\177' : CTRL(optarg[1]) :
			    optarg[0];
			break;
		case 'k':		/* kill character */
			killch = optarg[0] == '^' && optarg[1] != '\0' ?
			    optarg[1] == '?' ? '\177' : CTRL(optarg[1]) :
			    optarg[0];
			break;
		case 'm':		/* map identifier to type */
			add_mapping(NULL, optarg);
			break;
		case 'n':		/* OBSOLETE: set new tty driver */
			break;
		case 'p':		/* OBSOLETE: map identifier to type */
			add_mapping("plugboard", optarg);
			break;
		case 'Q':		/* don't output control key settings */
			quiet = 1;
			break;
		case 'S':		/* output TERM/TERMCAP strings */
			Sflag = 1;
			break;
		case 'r':		/* display term on stderr */
			showterm = 1;
			break;
		case 's':		/* output TERM/TERMCAP strings */
			sflag = 1;
			break;
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc > 1)
		usage();

	ttype = get_termcap_entry(*argv, &tcapbuf);

	if (!noset) {
		Columns = tgetnum("co");
		Lines = tgetnum("li");

#ifdef TIOCGWINSZ
		/* Set window size */
		(void)ioctl(STDERR_FILENO, TIOCGWINSZ, &win);
		if (win.ws_row == 0 && win.ws_col == 0 &&
		    Lines > 0 && Columns > 0) {
			win.ws_row = Lines;
			win.ws_col = Columns;
			(void)ioctl(STDERR_FILENO, TIOCSWINSZ, &win);
		}
#endif
		set_control_chars();
		set_conversions(usingupper);

		if (!noinit)
			set_init();

		/* Set the modes if they've changed. */
		if (memcmp(&mode, &oldmode, sizeof(mode)))
			tcsetattr(STDERR_FILENO, TCSADRAIN, &mode);
	}

	if (noset)
		(void)printf("%s\n", ttype);
	else {
		if (showterm)
			(void)fprintf(stderr, "Terminal type is %s.\n", ttype);
		/*
		 * If erase, kill and interrupt characters could have been
		 * modified and not -Q, display the changes.
		 */
		if (!quiet) {
			report("Erase", VERASE, CERASE);
			report("Kill", VKILL, CKILL);
			report("Interrupt", VINTR, CINTR);
		}
	}

	if (Sflag) {
		(void)printf("%s ", ttype);
		if (strlen(tcapbuf) > 0)
			wrtermcap(tcapbuf);
	}

	if (sflag) {
		/*
		 * Figure out what shell we're using.  A hack, we look for an
		 * environmental variable SHELL ending in "csh".
		 */
		if ((p = getenv("SHELL")) &&
		    !strcmp(p + strlen(p) - 3, "csh")) {
			printf("set noglob;\nsetenv TERM %s;\n", ttype);
			if (strlen(tcapbuf) > 0) {
				printf("setenv TERMCAP '");
				wrtermcap(tcapbuf);
				printf("';\n");
			}
			printf("unset noglob;\n");
		} else {
			printf("TERM=%s;\n", ttype);
			if (strlen(tcapbuf) > 0) {
				printf("TERMCAP='");
				wrtermcap(tcapbuf);
				printf("';\nexport TERMCAP;\n");
			}
			printf("export TERM;\n");
		}
	}

	exit(0);
}

/*
 * Tell the user if a control key has been changed from the default value.
 */
void
report(const char *name, int which, u_int def)
{
	u_int old, new;

	new = mode.c_cc[which];
	old = oldmode.c_cc[which];

	if (old == new && old == def)
		return;

	(void)fprintf(stderr, "%s %s ", name, old == new ? "is" : "set to");

	if (new == 010)
		(void)fprintf(stderr, "backspace.\n");
	else if (new == 0177)
		(void)fprintf(stderr, "delete.\n");
	else if (new < 040) {
		new ^= 0100;
		(void)fprintf(stderr, "control-%c (^%c).\n", new, new);
	} else
		(void)fprintf(stderr, "%c.\n", new);
}

/*
 * Convert the obsolete argument form into something that getopt can handle.
 * This means that -e, -i and -k get default arguments supplied for them.
 */
void
obsolete(char *argv[])
{
	for (; *argv; ++argv) {
		if (argv[0][0] != '-' || (argv[1] && argv[1][0] != '-') ||
		    (argv[0][1] != 'e' && argv[0][1] != 'i' && argv[0][1] != 'k') ||
			argv[0][2] != '\0')
			continue;
		switch(argv[0][1]) {
		case 'e':
			argv[0] = strdup("-e^H");
			break;
		case 'i':
			argv[0] = strdup("-i^C");
			break;
		case 'k':
			argv[0] = strdup("-k^U");
			break;
		}
	}
}

void
usage(void)
{
	(void)fprintf(stderr, "%s\n%s\n",
"usage: tset  [-IQrSs] [-] [-e ch] [-i ch] [-k ch] [-m mapping] [terminal]",
"       reset [-IQrSs] [-] [-e ch] [-i ch] [-k ch] [-m mapping] [terminal]");
	exit(1);
}

