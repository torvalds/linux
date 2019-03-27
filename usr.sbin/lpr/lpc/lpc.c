/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1983, 1993
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
 */

#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1983, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#if 0
#ifndef lint
static char sccsid[] = "@(#)lpc.c	8.3 (Berkeley) 4/28/95";
#endif /* not lint */
#endif

#include "lp.cdefs.h"		/* A cross-platform version of <sys/cdefs.h> */
__FBSDID("$FreeBSD$");

#include <sys/param.h>

#include <ctype.h>
#include <dirent.h>
#include <err.h>
#include <grp.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <string.h>
#include <unistd.h>
#include <histedit.h>

#include "lp.h"
#include "lpc.h"
#include "extern.h"

#ifndef LPR_OPER
#define LPR_OPER	"operator"	/* group name of lpr operators */
#endif

/*
 * lpc -- line printer control program
 */

#define MAX_CMDLINE	200
#define MAX_MARGV	20
static int	fromatty;

static char	cmdline[MAX_CMDLINE];
static int	margc;
static char	*margv[MAX_MARGV];
uid_t		uid, euid;

int			 main(int _argc, char *_argv[]);
static void		 cmdscanner(void);
static struct cmd	*getcmd(const char *_name);
static void		 intr(int _signo);
static void		 makeargv(void);
static int		 ingroup(const char *_grname);

int
main(int argc, char *argv[])
{
	register struct cmd *c;

	euid = geteuid();
	uid = getuid();
	PRIV_END
	progname = argv[0];
	openlog("lpd", 0, LOG_LPR);

	if (--argc > 0) {
		c = getcmd(*++argv);
		if (c == (struct cmd *)-1) {
			printf("?Ambiguous command\n");
			exit(1);
		}
		if (c == NULL) {
			printf("?Invalid command\n");
			exit(1);
		}
		if ((c->c_opts & LPC_PRIVCMD) && getuid() &&
		    ingroup(LPR_OPER) == 0) {
			printf("?Privileged command\n");
			exit(1);
		}
		if (c->c_generic != NULL)
			generic(c->c_generic, c->c_opts, c->c_handler,
			    argc, argv);
		else
			(*c->c_handler)(argc, argv);
		exit(0);
	}
	fromatty = isatty(fileno(stdin));
	if (!fromatty)
		signal(SIGINT, intr);
	for (;;) {
		cmdscanner();
	}
}

static void
intr(int signo __unused)
{
	/* (the '__unused' is just to avoid a compile-time warning) */
	exit(0);
}

static const char *
lpc_prompt(void)
{
	return ("lpc> ");
}

/*
 * Command parser.
 */
static void
cmdscanner(void)
{
	register struct cmd *c;
	static EditLine *el;
	static History *hist;
	HistEvent he;
	size_t len;
	int num;
	const char *bp;

	num = 0;
	bp = NULL;
	el = NULL;
	hist = NULL;
	for (;;) {
		if (fromatty) {
			if (!el) {
				el = el_init("lpc", stdin, stdout, stderr);
				hist = history_init();
				history(hist, &he, H_SETSIZE, 100);
				el_set(el, EL_HIST, history, hist);
				el_set(el, EL_EDITOR, "emacs");
				el_set(el, EL_PROMPT, lpc_prompt);
				el_set(el, EL_SIGNAL, 1);
				el_source(el, NULL);
				/*
				 * EditLine init may call 'cgetset()' to set a
				 * capability-db meant for termcap (eg: to set
				 * terminal type 'xterm').  Reset that now, or
				 * that same db-information will be used for
				 * printcap (giving us an "xterm" printer, with
				 * all kinds of invalid capabilities...).
				 */
				cgetset(NULL);
			}
			if ((bp = el_gets(el, &num)) == NULL || num == 0)
				quit(0, NULL);

			len = MIN(MAX_CMDLINE - 1, num);
			memcpy(cmdline, bp, len);
			cmdline[len] = 0; 
			history(hist, &he, H_ENTER, bp);

		} else {
			if (fgets(cmdline, MAX_CMDLINE, stdin) == NULL)
				quit(0, NULL);
			if (cmdline[0] == 0 || cmdline[0] == '\n')
				break;
		}

		makeargv();
		if (margc == 0)
			continue;
		if (el != NULL && el_parse(el, margc, (const char **)margv) != -1)
			continue;

		c = getcmd(margv[0]);
		if (c == (struct cmd *)-1) {
			printf("?Ambiguous command\n");
			continue;
		}
		if (c == NULL) {
			printf("?Invalid command\n");
			continue;
		}
		if ((c->c_opts & LPC_PRIVCMD) && getuid() &&
		    ingroup(LPR_OPER) == 0) {
			printf("?Privileged command\n");
			continue;
		}

		/*
		 * Two different commands might have the same generic rtn
		 * (eg: "clean" and "tclean"), and just use different
		 * handler routines for distinct command-setup.  The handler
		 * routine might also be set on a generic routine for
		 * initial parameter processing.
		 */
		if (c->c_generic != NULL)
			generic(c->c_generic, c->c_opts, c->c_handler,
			    margc, margv);
		else
			(*c->c_handler)(margc, margv);
	}
}

static struct cmd *
getcmd(const char *name)
{
	register const char *p, *q;
	register struct cmd *c, *found;
	register int nmatches, longest;

	longest = 0;
	nmatches = 0;
	found = NULL;
	for (c = cmdtab; (p = c->c_name); c++) {
		for (q = name; *q == *p++; q++)
			if (*q == 0)		/* exact match? */
				return(c);
		if (!*q) {			/* the name was a prefix */
			if (q - name > longest) {
				longest = q - name;
				nmatches = 1;
				found = c;
			} else if (q - name == longest)
				nmatches++;
		}
	}
	if (nmatches > 1)
		return((struct cmd *)-1);
	return(found);
}

/*
 * Slice a string up into argc/argv.
 */
static void
makeargv(void)
{
	register char *cp;
	register char **argp = margv;
	register int n = 0;

	margc = 0;
	for (cp = cmdline; *cp && (size_t)(cp - cmdline) < sizeof(cmdline) &&
	    n < MAX_MARGV - 1; n++) {
		while (isspace(*cp))
			cp++;
		if (*cp == '\0')
			break;
		*argp++ = cp;
		margc += 1;
		while (*cp != '\0' && !isspace(*cp))
			cp++;
		if (*cp == '\0')
			break;
		*cp++ = '\0';
	}
	*argp++ = NULL;
}

#define HELPINDENT (sizeof ("directory"))

/*
 * Help command.
 */
void
help(int argc, char *argv[])
{
	register struct cmd *c;

	if (argc == 1) {
		register int i, j, w;
		int columns, width = 0, lines;

		printf("Commands may be abbreviated.  Commands are:\n\n");
		for (c = cmdtab; c->c_name; c++) {
			int len = strlen(c->c_name);

			if (len > width)
				width = len;
		}
		width = (width + 8) &~ 7;
		columns = 80 / width;
		if (columns == 0)
			columns = 1;
		lines = (NCMDS + columns - 1) / columns;
		for (i = 0; i < lines; i++) {
			for (j = 0; j < columns; j++) {
				c = cmdtab + j * lines + i;
				if (c->c_name)
					printf("%s", c->c_name);
				if (c + lines >= &cmdtab[NCMDS]) {
					printf("\n");
					break;
				}
				w = strlen(c->c_name);
				while (w < width) {
					w = (w + 8) &~ 7;
					putchar('\t');
				}
			}
		}
		return;
	}
	while (--argc > 0) {
		register char *arg;
		arg = *++argv;
		c = getcmd(arg);
		if (c == (struct cmd *)-1)
			printf("?Ambiguous help command %s\n", arg);
		else if (c == (struct cmd *)0)
			printf("?Invalid help command %s\n", arg);
		else
			printf("%-*s\t%s\n", (int) HELPINDENT,
				c->c_name, c->c_help);
	}
}

/*
 * return non-zero if the user is a member of the given group
 */
static int
ingroup(const char *grname)
{
	static struct group *gptr=NULL;
	static int ngroups = 0;
	static long ngroups_max;
	static gid_t *groups;
	register gid_t gid;
	register int i;

	if (gptr == NULL) {
		if ((gptr = getgrnam(grname)) == NULL) {
			warnx("warning: unknown group '%s'", grname);
			return(0);
		}
		ngroups_max = sysconf(_SC_NGROUPS_MAX);
		if ((groups = malloc(sizeof(gid_t) * ngroups_max)) == NULL)
			err(1, "malloc");
		ngroups = getgroups(ngroups_max, groups);
		if (ngroups < 0)
			err(1, "getgroups");
	}
	gid = gptr->gr_gid;
	for (i = 0; i < ngroups; i++)
		if (gid == groups[i])
			return(1);
	return(0);
}

/*
 * Routine to get the information for a single printer (which will be
 * called by the routines which implement individual commands).
 * Note: This is for commands operating on a *single* printer.
 */
struct printer *
setup_myprinter(char *pwanted, struct printer *pp, int sump_opts)
{
	int cdres, cmdstatus;

	init_printer(pp);
	cmdstatus = getprintcap(pwanted, pp);
	switch (cmdstatus) {
	default:
		fatal(pp, "%s", pcaperr(cmdstatus));
		/* NOTREACHED */
	case PCAPERR_NOTFOUND:
		printf("unknown printer %s\n", pwanted);
		return (NULL);
	case PCAPERR_TCOPEN:
		printf("warning: %s: unresolved tc= reference(s)", pwanted);
		break;
	case PCAPERR_SUCCESS:
		break;
	}
	if ((sump_opts & SUMP_NOHEADER) == 0)
		printf("%s:\n", pp->printer);

	if (sump_opts & SUMP_CHDIR_SD) {
		PRIV_START
		cdres = chdir(pp->spool_dir);
		PRIV_END
		if (cdres < 0) {
			printf("\tcannot chdir to %s\n", pp->spool_dir);
			free_printer(pp);
			return (NULL);
		}
	}

	return (pp);
}
