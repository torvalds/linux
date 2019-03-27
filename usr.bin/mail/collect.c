/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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

#ifndef lint
#if 0
static char sccsid[] = "@(#)collect.c	8.2 (Berkeley) 4/19/94";
#endif
#endif /* not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Mail -- a mail program
 *
 * Collect input from standard input, handling
 * ~ escapes.
 */

#include "rcv.h"
#include <fcntl.h>
#include "extern.h"

/*
 * Read a message from standard input and return a read file to it
 * or NULL on error.
 */

/*
 * The following hokiness with global variables is so that on
 * receipt of an interrupt signal, the partial message can be salted
 * away on dead.letter.
 */

static	sig_t	saveint;		/* Previous SIGINT value */
static	sig_t	savehup;		/* Previous SIGHUP value */
static	sig_t	savetstp;		/* Previous SIGTSTP value */
static	sig_t	savettou;		/* Previous SIGTTOU value */
static	sig_t	savettin;		/* Previous SIGTTIN value */
static	FILE	*collf;			/* File for saving away */
static	int	hadintr;		/* Have seen one SIGINT so far */

static	jmp_buf	colljmp;		/* To get back to work */
static	int	colljmp_p;		/* whether to long jump */
static	jmp_buf	collabort;		/* To end collection with error */

FILE *
collect(struct header *hp, int printheaders)
{
	FILE *fbuf;
	int lc, cc, escape, eofcount, fd, c, t;
	char linebuf[LINESIZE], tempname[PATHSIZE], *cp, getsub;
	sigset_t nset;
	int longline, lastlong, rc;	/* So we don't make 2 or more lines
					   out of a long input line. */

	collf = NULL;
	/*
	 * Start catching signals from here, but we're still die on interrupts
	 * until we're in the main loop.
	 */
	(void)sigemptyset(&nset);
	(void)sigaddset(&nset, SIGINT);
	(void)sigaddset(&nset, SIGHUP);
	(void)sigprocmask(SIG_BLOCK, &nset, NULL);
	if ((saveint = signal(SIGINT, SIG_IGN)) != SIG_IGN)
		(void)signal(SIGINT, collint);
	if ((savehup = signal(SIGHUP, SIG_IGN)) != SIG_IGN)
		(void)signal(SIGHUP, collhup);
	savetstp = signal(SIGTSTP, collstop);
	savettou = signal(SIGTTOU, collstop);
	savettin = signal(SIGTTIN, collstop);
	if (setjmp(collabort) || setjmp(colljmp)) {
		(void)rm(tempname);
		goto err;
	}
	(void)sigprocmask(SIG_UNBLOCK, &nset, NULL);

	noreset++;
	(void)snprintf(tempname, sizeof(tempname),
	    "%s/mail.RsXXXXXXXXXX", tmpdir);
	if ((fd = mkstemp(tempname)) == -1 ||
	    (collf = Fdopen(fd, "w+")) == NULL) {
		warn("%s", tempname);
		goto err;
	}
	(void)rm(tempname);

	/*
	 * If we are going to prompt for a subject,
	 * refrain from printing a newline after
	 * the headers (since some people mind).
	 */
	t = GTO|GSUBJECT|GCC|GNL;
	getsub = 0;
	if (hp->h_subject == NULL && value("interactive") != NULL &&
	    (value("ask") != NULL || value("asksub") != NULL))
		t &= ~GNL, getsub++;
	if (printheaders) {
		puthead(hp, stdout, t);
		(void)fflush(stdout);
	}
	if ((cp = value("escape")) != NULL)
		escape = *cp;
	else
		escape = ESCAPE;
	eofcount = 0;
	hadintr = 0;
	longline = 0;

	if (!setjmp(colljmp)) {
		if (getsub)
			grabh(hp, GSUBJECT);
	} else {
		/*
		 * Come here for printing the after-signal message.
		 * Duplicate messages won't be printed because
		 * the write is aborted if we get a SIGTTOU.
		 */
cont:
		if (hadintr) {
			(void)fflush(stdout);
			fprintf(stderr,
			"\n(Interrupt -- one more to kill letter)\n");
		} else {
			printf("(continue)\n");
			(void)fflush(stdout);
		}
	}
	for (;;) {
		colljmp_p = 1;
		c = readline(stdin, linebuf, LINESIZE);
		colljmp_p = 0;
		if (c < 0) {
			if (value("interactive") != NULL &&
			    value("ignoreeof") != NULL && ++eofcount < 25) {
				printf("Use \".\" to terminate letter\n");
				continue;
			}
			break;
		}
		lastlong = longline;
		longline = c == LINESIZE - 1;
		eofcount = 0;
		hadintr = 0;
		if (linebuf[0] == '.' && linebuf[1] == '\0' &&
		    value("interactive") != NULL && !lastlong &&
		    (value("dot") != NULL || value("ignoreeof") != NULL))
			break;
		if (linebuf[0] != escape || value("interactive") == NULL ||
		    lastlong) {
			if (putline(collf, linebuf, !longline) < 0)
				goto err;
			continue;
		}
		c = linebuf[1];
		switch (c) {
		default:
			/*
			 * On double escape, just send the single one.
			 * Otherwise, it's an error.
			 */
			if (c == escape) {
				if (putline(collf, &linebuf[1], !longline) < 0)
					goto err;
				else
					break;
			}
			printf("Unknown tilde escape.\n");
			break;
		case 'C':
			/*
			 * Dump core.
			 */
			core();
			break;
		case '!':
			/*
			 * Shell escape, send the balance of the
			 * line to sh -c.
			 */
			shell(&linebuf[2]);
			break;
		case ':':
		case '_':
			/*
			 * Escape to command mode, but be nice!
			 */
			execute(&linebuf[2], 1);
			goto cont;
		case '.':
			/*
			 * Simulate end of file on input.
			 */
			goto out;
		case 'q':
			/*
			 * Force a quit of sending mail.
			 * Act like an interrupt happened.
			 */
			hadintr++;
			collint(SIGINT);
			exit(1);
		case 'x':
			/*
			 * Exit, do not save in dead.letter.
			 */
			goto err;
		case 'h':
			/*
			 * Grab a bunch of headers.
			 */
			grabh(hp, GTO|GSUBJECT|GCC|GBCC);
			goto cont;
		case 't':
			/*
			 * Add to the To list.
			 */
			hp->h_to = cat(hp->h_to, extract(&linebuf[2], GTO));
			break;
		case 's':
			/*
			 * Set the Subject line.
			 */
			cp = &linebuf[2];
			while (isspace((unsigned char)*cp))
				cp++;
			hp->h_subject = savestr(cp);
			break;
		case 'R':
			/*
			 * Set the Reply-To line.
			 */
			cp = &linebuf[2];
			while (isspace((unsigned char)*cp))
				cp++;
			hp->h_replyto = savestr(cp);
			break;
		case 'c':
			/*
			 * Add to the CC list.
			 */
			hp->h_cc = cat(hp->h_cc, extract(&linebuf[2], GCC));
			break;
		case 'b':
			/*
			 * Add to the BCC list.
			 */
			hp->h_bcc = cat(hp->h_bcc, extract(&linebuf[2], GBCC));
			break;
		case 'i':
		case 'A':
		case 'a':
			/*
			 * Insert named variable in message.
			 */
			switch(c) {
				case 'i':
					cp = &linebuf[2];
					while(isspace((unsigned char)*cp))
						cp++;
					break;
				case 'a':
					cp = "sign";
					break;
				case 'A':
					cp = "Sign";
					break;
				default:
					goto err;
			}

			if(*cp != '\0' && (cp = value(cp)) != NULL) {
				printf("%s\n", cp);
				if(putline(collf, cp, 1) < 0)
					goto err;
			}

			break;
		case 'd':
			/*
			 * Read in the dead letter file.
			 */
			if (strlcpy(linebuf + 2, getdeadletter(),
				sizeof(linebuf) - 2)
			    >= sizeof(linebuf) - 2) {
				printf("Line buffer overflow\n");
				break;
			}
			/* FALLTHROUGH */
		case 'r':
		case '<':
			/*
			 * Invoke a file:
			 * Search for the file name,
			 * then open it and copy the contents to collf.
			 */
			cp = &linebuf[2];
			while (isspace((unsigned char)*cp))
				cp++;
			if (*cp == '\0') {
				printf("Interpolate what file?\n");
				break;
			}
			cp = expand(cp);
			if (cp == NULL)
				break;
			if (*cp == '!') {
				/*
				 * Insert stdout of command.
				 */
				char *sh;
				int nullfd, tempfd, rc;
				char tempname2[PATHSIZE];

				if ((nullfd = open(_PATH_DEVNULL, O_RDONLY, 0))
				    == -1) {
					warn(_PATH_DEVNULL);
					break;
				}

				(void)snprintf(tempname2, sizeof(tempname2),
				    "%s/mail.ReXXXXXXXXXX", tmpdir);
				if ((tempfd = mkstemp(tempname2)) == -1 ||
				    (fbuf = Fdopen(tempfd, "w+")) == NULL) {
					warn("%s", tempname2);
					break;
				}
				(void)unlink(tempname2);

				if ((sh = value("SHELL")) == NULL)
					sh = _PATH_CSHELL;

				rc = run_command(sh, 0, nullfd, fileno(fbuf),
				    "-c", cp+1, NULL);

				close(nullfd);

				if (rc < 0) {
					(void)Fclose(fbuf);
					break;
				}

				if (fsize(fbuf) == 0) {
					fprintf(stderr,
					    "No bytes from command \"%s\"\n",
					    cp+1);
					(void)Fclose(fbuf);
					break;
				}

				rewind(fbuf);
			} else if (isdir(cp)) {
				printf("%s: Directory\n", cp);
				break;
			} else if ((fbuf = Fopen(cp, "r")) == NULL) {
				warn("%s", cp);
				break;
			}
			printf("\"%s\" ", cp);
			(void)fflush(stdout);
			lc = 0;
			cc = 0;
			while ((rc = readline(fbuf, linebuf, LINESIZE)) >= 0) {
				if (rc != LINESIZE - 1)
					lc++;
				if ((t = putline(collf, linebuf,
					 rc != LINESIZE - 1)) < 0) {
					(void)Fclose(fbuf);
					goto err;
				}
				cc += t;
			}
			(void)Fclose(fbuf);
			printf("%d/%d\n", lc, cc);
			break;
		case 'w':
			/*
			 * Write the message on a file.
			 */
			cp = &linebuf[2];
			while (*cp == ' ' || *cp == '\t')
				cp++;
			if (*cp == '\0') {
				fprintf(stderr, "Write what file!?\n");
				break;
			}
			if ((cp = expand(cp)) == NULL)
				break;
			rewind(collf);
			exwrite(cp, collf, 1);
			break;
		case 'm':
		case 'M':
		case 'f':
		case 'F':
			/*
			 * Interpolate the named messages, if we
			 * are in receiving mail mode.  Does the
			 * standard list processing garbage.
			 * If ~f is given, we don't shift over.
			 */
			if (forward(linebuf + 2, collf, tempname, c) < 0)
				goto err;
			goto cont;
		case '?':
			if ((fbuf = Fopen(_PATH_TILDE, "r")) == NULL) {
				warn("%s", _PATH_TILDE);
				break;
			}
			while ((t = getc(fbuf)) != EOF)
				(void)putchar(t);
			(void)Fclose(fbuf);
			break;
		case 'p':
			/*
			 * Print out the current state of the
			 * message without altering anything.
			 */
			rewind(collf);
			printf("-------\nMessage contains:\n");
			puthead(hp, stdout, GTO|GSUBJECT|GCC|GBCC|GNL);
			while ((t = getc(collf)) != EOF)
				(void)putchar(t);
			goto cont;
		case '|':
			/*
			 * Pipe message through command.
			 * Collect output as new message.
			 */
			rewind(collf);
			mespipe(collf, &linebuf[2]);
			goto cont;
		case 'v':
		case 'e':
			/*
			 * Edit the current message.
			 * 'e' means to use EDITOR
			 * 'v' means to use VISUAL
			 */
			rewind(collf);
			mesedit(collf, c);
			goto cont;
		}
	}
	goto out;
err:
	if (collf != NULL) {
		(void)Fclose(collf);
		collf = NULL;
	}
out:
	if (collf != NULL)
		rewind(collf);
	noreset--;
	(void)sigprocmask(SIG_BLOCK, &nset, NULL);
	(void)signal(SIGINT, saveint);
	(void)signal(SIGHUP, savehup);
	(void)signal(SIGTSTP, savetstp);
	(void)signal(SIGTTOU, savettou);
	(void)signal(SIGTTIN, savettin);
	(void)sigprocmask(SIG_UNBLOCK, &nset, NULL);
	return (collf);
}

/*
 * Write a file, ex-like if f set.
 */
int
exwrite(char name[], FILE *fp, int f)
{
	FILE *of;
	int c, lc;
	long cc;
	struct stat junk;

	if (f) {
		printf("\"%s\" ", name);
		(void)fflush(stdout);
	}
	if (stat(name, &junk) >= 0 && S_ISREG(junk.st_mode)) {
		if (!f)
			fprintf(stderr, "%s: ", name);
		fprintf(stderr, "File exists\n");
		return (-1);
	}
	if ((of = Fopen(name, "w")) == NULL) {
		warn((char *)NULL);
		return (-1);
	}
	lc = 0;
	cc = 0;
	while ((c = getc(fp)) != EOF) {
		cc++;
		if (c == '\n')
			lc++;
		(void)putc(c, of);
		if (ferror(of)) {
			warnx("%s", name);
			(void)Fclose(of);
			return (-1);
		}
	}
	(void)Fclose(of);
	printf("%d/%ld\n", lc, cc);
	(void)fflush(stdout);
	return (0);
}

/*
 * Edit the message being collected on fp.
 * On return, make the edit file the new temp file.
 */
void
mesedit(FILE *fp, int c)
{
	sig_t sigint = signal(SIGINT, SIG_IGN);
	FILE *nf = run_editor(fp, (off_t)-1, c, 0);

	if (nf != NULL) {
		(void)fseeko(nf, (off_t)0, SEEK_END);
		collf = nf;
		(void)Fclose(fp);
	}
	(void)signal(SIGINT, sigint);
}

/*
 * Pipe the message through the command.
 * Old message is on stdin of command;
 * New message collected from stdout.
 * Sh -c must return 0 to accept the new message.
 */
void
mespipe(FILE *fp, char cmd[])
{
	FILE *nf;
	int fd;
	sig_t sigint = signal(SIGINT, SIG_IGN);
	char *sh, tempname[PATHSIZE];

	(void)snprintf(tempname, sizeof(tempname),
	    "%s/mail.ReXXXXXXXXXX", tmpdir);
	if ((fd = mkstemp(tempname)) == -1 ||
	    (nf = Fdopen(fd, "w+")) == NULL) {
		warn("%s", tempname);
		goto out;
	}
	(void)rm(tempname);
	/*
	 * stdin = current message.
	 * stdout = new message.
	 */
	if ((sh = value("SHELL")) == NULL)
		sh = _PATH_CSHELL;
	if (run_command(sh,
	    0, fileno(fp), fileno(nf), "-c", cmd, NULL) < 0) {
		(void)Fclose(nf);
		goto out;
	}
	if (fsize(nf) == 0) {
		fprintf(stderr, "No bytes from \"%s\" !?\n", cmd);
		(void)Fclose(nf);
		goto out;
	}
	/*
	 * Take new files.
	 */
	(void)fseeko(nf, (off_t)0, SEEK_END);
	collf = nf;
	(void)Fclose(fp);
out:
	(void)signal(SIGINT, sigint);
}

/*
 * Interpolate the named messages into the current
 * message, preceding each line with a tab.
 * Return a count of the number of characters now in
 * the message, or -1 if an error is encountered writing
 * the message temporary.  The flag argument is 'm' if we
 * should shift over and 'f' if not.
 */
int
forward(char ms[], FILE *fp, char *fn, int f)
{
	int *msgvec;
	struct ignoretab *ig;
	char *tabst;

	msgvec = (int *)salloc((msgCount+1) * sizeof(*msgvec));
	if (msgvec == NULL)
		return (0);
	if (getmsglist(ms, msgvec, 0) < 0)
		return (0);
	if (*msgvec == 0) {
		*msgvec = first(0, MMNORM);
		if (*msgvec == 0) {
			printf("No appropriate messages\n");
			return (0);
		}
		msgvec[1] = 0;
	}
	if (f == 'f' || f == 'F')
		tabst = NULL;
	else if ((tabst = value("indentprefix")) == NULL)
		tabst = "\t";
	ig = isupper((unsigned char)f) ? NULL : ignore;
	printf("Interpolating:");
	for (; *msgvec != 0; msgvec++) {
		struct message *mp = message + *msgvec - 1;

		touch(mp);
		printf(" %d", *msgvec);
		if (sendmessage(mp, fp, ig, tabst) < 0) {
			warnx("%s", fn);
			return (-1);
		}
	}
	printf("\n");
	return (0);
}

/*
 * Print (continue) when continued after ^Z.
 */
/*ARGSUSED*/
void
collstop(int s)
{
	sig_t old_action = signal(s, SIG_DFL);
	sigset_t nset;

	(void)sigemptyset(&nset);
	(void)sigaddset(&nset, s);
	(void)sigprocmask(SIG_UNBLOCK, &nset, NULL);
	(void)kill(0, s);
	(void)sigprocmask(SIG_BLOCK, &nset, NULL);
	(void)signal(s, old_action);
	if (colljmp_p) {
		colljmp_p = 0;
		hadintr = 0;
		longjmp(colljmp, 1);
	}
}

/*
 * On interrupt, come here to save the partial message in ~/dead.letter.
 * Then jump out of the collection loop.
 */
/*ARGSUSED*/
void
collint(int s __unused)
{
	/*
	 * the control flow is subtle, because we can be called from ~q.
	 */
	if (!hadintr) {
		if (value("ignore") != NULL) {
			printf("@");
			(void)fflush(stdout);
			clearerr(stdin);
			return;
		}
		hadintr = 1;
		longjmp(colljmp, 1);
	}
	rewind(collf);
	if (value("nosave") == NULL)
		savedeadletter(collf);
	longjmp(collabort, 1);
}

/*ARGSUSED*/
void
collhup(int s __unused)
{
	rewind(collf);
	savedeadletter(collf);
	/*
	 * Let's pretend nobody else wants to clean up,
	 * a true statement at this time.
	 */
	exit(1);
}

void
savedeadletter(FILE *fp)
{
	FILE *dbuf;
	int c;
	char *cp;

	if (fsize(fp) == 0)
		return;
	cp = getdeadletter();
	c = umask(077);
	dbuf = Fopen(cp, "a");
	(void)umask(c);
	if (dbuf == NULL)
		return;
	while ((c = getc(fp)) != EOF)
		(void)putc(c, dbuf);
	(void)Fclose(dbuf);
	rewind(fp);
}
