/*	$OpenBSD: hack.pager.c,v 1.25 2019/06/28 13:32:52 deraadt Exp $	*/

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

/* This file contains the command routine dowhatis() and a pager. */
/* Also readmail() and doshell(), and generally the things that
   contact the outside world. */

#include <libgen.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "hack.h"

extern int CO, LI;	/* usually COLNO and ROWNO+2 */
extern char *CD;
extern char quitchars[];

static void page_more(FILE *, int);

int
dowhatis(void)
{
	FILE *fp;
	char bufr[BUFSZ+6];
	char *buf = &bufr[6], q;
	size_t len;
	extern char readchar();

	if (!(fp = fopen(DATAFILE, "r")))
		pline("Cannot open data file!");
	else {
		pline("Specify what? ");
		q = readchar();
		if (q != '\t')
			while (fgets(buf,BUFSZ,fp))
				if (*buf == q) {
					len = strcspn(buf, "\n");
					/* bad data file */
					if (len == 0)
						continue;
					buf[len] = '\0';
					/* Expand tab 'by hand' */
					if (buf[1] == '\t'){
						buf = bufr;
						buf[0] = q;
						(void) strncpy(buf+1, "       ", 7);
						len = strlen(buf);
					}
					pline("%s", buf);
					if (buf[len - 1] == ';') {
						pline("More info? ");
						if (readchar() == 'y') {
							page_more(fp,1); /* does fclose() */
							return(0);
						}
					}
					(void) fclose(fp); 	/* kopper@psuvax1 */
					return(0);
				}
		pline("I've never heard of such things.");
		(void) fclose(fp);
	}
	return(0);
}

/* make the paging of a file interruptible */
static int got_intrup;

void
intruph(int notused)
{
	got_intrup++;
}

/* simple pager, also used from dohelp() */
/* strip: nr of chars to be stripped from each line (0 or 1) */
static void
page_more(FILE *fp, int strip)
{
	char *bufr;
	sig_t prevsig = signal(SIGINT, intruph);

	set_pager(0);
	bufr = (char *) alloc((unsigned) CO);
	while (fgets(bufr, CO, fp) && (!strip || *bufr == '\t') &&
	    !got_intrup) {
		bufr[strcspn(bufr, "\n")] = '\0';
		if (page_line(bufr+strip)) {
			set_pager(2);
			goto ret;
		}
	}
	set_pager(1);
ret:
	free(bufr);
	(void) fclose(fp);
	(void) signal(SIGINT, prevsig);
	got_intrup = 0;
}

static boolean whole_screen = TRUE;
#define	PAGMIN	12	/* minimum # of lines for page below level map */

void
set_whole_screen(void)
{	/* called in termcap as soon as LI is known */
	whole_screen = (LI-ROWNO-2 <= PAGMIN || !CD);
}

#ifdef NEWS
int
readnews(void)
{
	int ret;

	whole_screen = TRUE;	/* force a docrt(), our first */
	ret = page_file(NEWS, TRUE);
	set_whole_screen();
	return(ret);		/* report whether we did docrt() */
}
#endif /* NEWS */

/* 0: open  1: wait+close  2: close */
void
set_pager(int mode)
{
	static boolean so;
	if(mode == 0) {
		if(!whole_screen) {
			/* clear topline */
			clrlin();
			/* use part of screen below level map */
			curs(1, ROWNO+4);
		} else {
			cls();
		}
		so = flags.standout;
		flags.standout = 1;
	} else {
		if(mode == 1) {
			curs(1, LI);
			more();
		}
		flags.standout = so;
		if(whole_screen)
			docrt();
		else {
			curs(1, ROWNO+4);
			cl_eos();
		}
	}
}

int
page_line(char *s)		/* returns 1 if we should quit */
{
	extern char morc;

	if(cury == LI-1) {
		if(!*s)
			return(0);	/* suppress blank lines at top */
		putchar('\n');
		cury++;
		cmore("q\033");
		if(morc) {
			morc = 0;
			return(1);
		}
		if(whole_screen)
			cls();
		else {
			curs(1, ROWNO+4);
			cl_eos();
		}
	}
	puts(s);
	cury++;
	return(0);
}

/*
 * Flexible pager: feed it with a number of lines and it will decide
 * whether these should be fed to the pager above, or displayed in a
 * corner.
 * Call:
 *	cornline(0, title or 0)	: initialize
 *	cornline(1, text)	: add text to the chain of texts
 *	cornline(2, morcs)	: output everything and cleanup
 *	cornline(3, 0)		: cleanup
 */
void
cornline(int mode, char *text)
{
	static struct line {
		struct line *next_line;
		char *line_text;
	} *texthead, *texttail;
	static int maxlen;
	static int linect;
	struct line *tl;

	if(mode == 0) {
		texthead = 0;
		maxlen = 0;
		linect = 0;
		if(text) {
			cornline(1, text);	/* title */
			cornline(1, "");	/* blank line */
		}
		return;
	}

	if(mode == 1) {
	    int len;

	    if(!text) return;	/* superfluous, just to be sure */
	    linect++;
	    len = strlen(text);
	    if(len > maxlen)
		maxlen = len;
	    tl = (struct line *)
		alloc((unsigned)(len + sizeof(struct line) + 1));
	    tl->next_line = 0;
	    tl->line_text = (char *)(tl + 1);
	    (void) strlcpy(tl->line_text, text, len + 1);
	    if(!texthead)
		texthead = tl;
	    else
		texttail->next_line = tl;
	    texttail = tl;
	    return;
	}

	/* --- now we really do it --- */
	if(mode == 2 && linect == 1)			    /* topline only */
		pline("%s", texthead->line_text);
	else
	if(mode == 2) {
	    int curline, lth;

	    if(flags.toplin == 1) more();	/* ab@unido */
	    remember_topl();

	    lth = CO - maxlen - 2;		   /* Use full screen width */
	    if (linect < LI && lth >= 10) {		     /* in a corner */
		home();
		cl_end();
		flags.toplin = 0;
		curline = 1;
		for (tl = texthead; tl; tl = tl->next_line) {
		    curs(lth, curline);
		    if(curline > 1)
			cl_end();
		    putsym(' ');
		    putstr (tl->line_text);
		    curline++;
		}
		curs(lth, curline);
		cl_end();
		cmore(text);
		home();
		cl_end();
		docorner(lth, curline-1);
	    } else {					/* feed to pager */
		set_pager(0);
		for (tl = texthead; tl; tl = tl->next_line) {
		    if (page_line (tl->line_text)) {
			set_pager(2);
			goto cleanup;
		    }
		}
		if(text) {
			cgetret(text);
			set_pager(2);
		} else
			set_pager(1);
	    }
	}

cleanup:
	while ((tl = texthead)) {
		texthead = tl->next_line;
		free(tl);
	}
}

int
dohelp(void)
{
	char c;

	pline ("Long or short help? ");
	while (((c = readchar ()) != 'l') && (c != 's') && !strchr(quitchars,c))
		hackbell ();
	if (!strchr(quitchars, c))
		(void) page_file((c == 'l') ? HELP : SHELP, FALSE);
	return(0);
}

/* return: 0 - cannot open fnam; 1 - otherwise */
int
page_file(char *fnam, boolean silent)
{
#ifdef DEF_PAGER			/* this implies that UNIX is defined */
      {
	/* use external pager; this may give security problems */

	int fd = open(fnam, O_RDONLY);

	if(fd == -1) {
		if(!silent) pline("Cannot open %s.", fnam);
		return(0);
	}
	if(child(1)){
		extern char *catmore;

		/* Now that child() does a setuid(getuid()) and a chdir(),
		   we may not be able to open file fnam anymore, so make
		   it stdin. */
		(void) close(0);
		if(dup(fd)) {
			if(!silent) printf("Cannot open %s as stdin.\n", fnam);
		} else {
			execlp(catmore, basename(catmore), (char *)NULL);
			if(!silent) printf("Cannot exec %s.\n", catmore);
		}
		exit(1);
	}
	(void) close(fd);
      }
#else /* DEF_PAGER */
      {
	FILE *f;			/* free after Robert Viduya */

	if ((f = fopen (fnam, "r")) == (FILE *) 0) {
		if(!silent) {
			home(); perror (fnam); flags.toplin = 1;
			pline ("Cannot open %s.", fnam);
		}
		return(0);
	}
	page_more(f, 0);
      }
#endif /* DEF_PAGER */

	return(1);
}

#ifdef UNIX
#ifdef SHELL
int
dosh(void)
{
	char *str;

	if(child(0)) {
		if ((str = getenv("SHELL")))
			execlp(str, str, (char *)NULL);
		else
			execl("/bin/sh", "sh", (char *)NULL);
		pline("sh: cannot execute.");
		exit(1);
	}
	return(0);
}
#endif /* SHELL */

#include <sys/wait.h>

int
child(int wt)
{
	int status;
	int f;
	char *home;
	gid_t gid;

	f = fork();
	if(f == 0){		/* child */
		settty(NULL);		/* also calls end_screen() */
		/* revoke privs */
		gid = getgid();
		setresgid(gid, gid, gid);
#ifdef CHDIR
		home = getenv("HOME");
		if (home == NULL || *home == '\0')
			home = "/";
		(void) chdir(home);
#endif /* CHDIR */
		return(1);
	}
	if(f == -1) {	/* cannot fork */
		pline("Fork failed. Try again.");
		return(0);
	}
	/* fork succeeded; wait for child to exit */
	(void) signal(SIGINT,SIG_IGN);
	(void) signal(SIGQUIT,SIG_IGN);
	(void) wait(&status);
	gettty();
	setftty();
	(void) signal(SIGINT,done1);
#ifdef WIZARD
	if(wizard) (void) signal(SIGQUIT,SIG_DFL);
#endif /* WIZARD */
	if(wt) getret();
	docrt();
	return(0);
}
#endif /* UNIX */
