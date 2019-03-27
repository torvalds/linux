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
static char sccsid[] = "@(#)cmd1.c	8.2 (Berkeley) 4/20/95";
#endif
#endif /* not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "rcv.h"
#include "extern.h"

/*
 * Mail -- a mail program
 *
 * User commands.
 */

/*
 * Print the current active headings.
 * Don't change dot if invoker didn't give an argument.
 */

static int screen;

int
headers(void *v)
{
	int *msgvec = v;
	int n, mesg, flag, size;
	struct message *mp;

	size = screensize();
	n = msgvec[0];
	if (n != 0)
		screen = (n-1)/size;
	if (screen < 0)
		screen = 0;
	mp = &message[screen * size];
	if (mp >= &message[msgCount])
		mp = &message[msgCount - size];
	if (mp < &message[0])
		mp = &message[0];
	flag = 0;
	mesg = mp - &message[0];
	if (dot != &message[n-1])
		dot = mp;
	for (; mp < &message[msgCount]; mp++) {
		mesg++;
		if (mp->m_flag & MDELETED)
			continue;
		if (flag++ >= size)
			break;
		printhead(mesg);
	}
	if (flag == 0) {
		printf("No more mail.\n");
		return (1);
	}
	return (0);
}

/*
 * Scroll to the next/previous screen
 */
int
scroll(void *v)
{
	char *arg = v;
	int s, size;
	int cur[1];

	cur[0] = 0;
	size = screensize();
	s = screen;
	switch (*arg) {
	case 0:
	case '+':
		s++;
		if (s * size >= msgCount) {
			printf("On last screenful of messages\n");
			return (0);
		}
		screen = s;
		break;

	case '-':
		if (--s < 0) {
			printf("On first screenful of messages\n");
			return (0);
		}
		screen = s;
		break;

	default:
		printf("Unrecognized scrolling command \"%s\"\n", arg);
		return (1);
	}
	return (headers(cur));
}

/*
 * Compute screen size.
 */
int
screensize(void)
{
	int s;
	char *cp;

	if ((cp = value("screen")) != NULL && (s = atoi(cp)) > 0)
		return (s);
	return (screenheight - 4);
}

/*
 * Print out the headlines for each message
 * in the passed message list.
 */
int
from(void *v)
{
	int *msgvec = v;
	int *ip;

	for (ip = msgvec; *ip != 0; ip++)
		printhead(*ip);
	if (--ip >= msgvec)
		dot = &message[*ip - 1];
	return (0);
}

/*
 * Print out the header of a specific message.
 * This is a slight improvement to the standard one.
 */
void
printhead(int mesg)
{
	struct message *mp;
	char headline[LINESIZE], wcount[LINESIZE], *subjline, dispc, curind;
	char pbuf[BUFSIZ];
	struct headline hl;
	int subjlen;
	char *name;

	mp = &message[mesg-1];
	(void)readline(setinput(mp), headline, LINESIZE);
	if ((subjline = hfield("subject", mp)) == NULL)
		subjline = hfield("subj", mp);
	/*
	 * Bletch!
	 */
	curind = dot == mp ? '>' : ' ';
	dispc = ' ';
	if (mp->m_flag & MSAVED)
		dispc = '*';
	if (mp->m_flag & MPRESERVE)
		dispc = 'P';
	if ((mp->m_flag & (MREAD|MNEW)) == MNEW)
		dispc = 'N';
	if ((mp->m_flag & (MREAD|MNEW)) == 0)
		dispc = 'U';
	if (mp->m_flag & MBOX)
		dispc = 'M';
	parse(headline, &hl, pbuf);
	sprintf(wcount, "%3ld/%-5ld", mp->m_lines, mp->m_size);
	subjlen = screenwidth - 50 - strlen(wcount);
	name = value("show-rcpt") != NULL ?
		skin(hfield("to", mp)) : nameof(mp, 0);
	if (subjline == NULL || subjlen < 0)		/* pretty pathetic */
		printf("%c%c%3d %-20.20s  %16.16s %s\n",
			curind, dispc, mesg, name, hl.l_date, wcount);
	else
		printf("%c%c%3d %-20.20s  %16.16s %s \"%.*s\"\n",
			curind, dispc, mesg, name, hl.l_date, wcount,
			subjlen, subjline);
}

/*
 * Print out the value of dot.
 */
int
pdot(void)
{
	printf("%td\n", dot - &message[0] + 1);
	return (0);
}

/*
 * Print out all the possible commands.
 */
int
pcmdlist(void)
{
	extern const struct cmd cmdtab[];
	const struct cmd *cp;
	int cc;

	printf("Commands are:\n");
	for (cc = 0, cp = cmdtab; cp->c_name != NULL; cp++) {
		cc += strlen(cp->c_name) + 2;
		if (cc > 72) {
			printf("\n");
			cc = strlen(cp->c_name) + 2;
		}
		if ((cp+1)->c_name != NULL)
			printf("%s, ", cp->c_name);
		else
			printf("%s\n", cp->c_name);
	}
	return (0);
}

/*
 * Paginate messages, honor ignored fields.
 */
int
more(void *v)
{
	int *msgvec = v;

	return (type1(msgvec, 1, 1));
}

/*
 * Paginate messages, even printing ignored fields.
 */
int
More(void *v)
{
	int *msgvec = v;

	return (type1(msgvec, 0, 1));
}

/*
 * Type out messages, honor ignored fields.
 */
int
type(void *v)
{
	int *msgvec = v;

	return (type1(msgvec, 1, 0));
}

/*
 * Type out messages, even printing ignored fields.
 */
int
Type(void *v)
{
	int *msgvec = v;

	return (type1(msgvec, 0, 0));
}

/*
 * Type out the messages requested.
 */
static jmp_buf	pipestop;
int
type1(int *msgvec, int doign, int page)
{
	int nlines, *ip;
	struct message *mp;
	char *cp;
	FILE *obuf;

	obuf = stdout;
	if (setjmp(pipestop))
		goto close_pipe;
	if (value("interactive") != NULL &&
	    (page || (cp = value("crt")) != NULL)) {
		nlines = 0;
		if (!page) {
			for (ip = msgvec; *ip && ip-msgvec < msgCount; ip++)
				nlines += message[*ip - 1].m_lines;
		}
		if (page || nlines > (*cp ? atoi(cp) : realscreenheight)) {
			cp = value("PAGER");
			if (cp == NULL || *cp == '\0')
				cp = _PATH_LESS;
			obuf = Popen(cp, "w");
			if (obuf == NULL) {
				warnx("%s", cp);
				obuf = stdout;
			} else
				(void)signal(SIGPIPE, brokpipe);
		}
	}

	/*
	 * Send messages to the output.
	 *
	 */
	for (ip = msgvec; *ip && ip - msgvec < msgCount; ip++) {
		mp = &message[*ip - 1];
		touch(mp);
		dot = mp;
		if (value("quiet") == NULL)
			fprintf(obuf, "Message %d:\n", *ip);
		(void)sendmessage(mp, obuf, doign ? ignore : 0, NULL);
	}

close_pipe:
	if (obuf != stdout) {
		/*
		 * Ignore SIGPIPE so it can't cause a duplicate close.
		 */
		(void)signal(SIGPIPE, SIG_IGN);
		(void)Pclose(obuf);
		(void)signal(SIGPIPE, SIG_DFL);
	}
	return (0);
}

/*
 * Respond to a broken pipe signal --
 * probably caused by quitting more.
 */
/*ARGSUSED*/
void
brokpipe(int signo __unused)
{
	longjmp(pipestop, 1);
}

/*
 * Print the top so many lines of each desired message.
 * The number of lines is taken from the variable "toplines"
 * and defaults to 5.
 */
int
top(void *v)
{
	int *msgvec = v;
	int *ip;
	struct message *mp;
	int c, topl, lines, lineb;
	char *valtop, linebuf[LINESIZE];
	FILE *ibuf;

	topl = 5;
	valtop = value("toplines");
	if (valtop != NULL) {
		topl = atoi(valtop);
		if (topl < 0 || topl > 10000)
			topl = 5;
	}
	lineb = 1;
	for (ip = msgvec; *ip && ip-msgvec < msgCount; ip++) {
		mp = &message[*ip - 1];
		touch(mp);
		dot = mp;
		if (value("quiet") == NULL)
			printf("Message %d:\n", *ip);
		ibuf = setinput(mp);
		c = mp->m_lines;
		if (!lineb)
			printf("\n");
		for (lines = 0; lines < c && lines <= topl; lines++) {
			if (readline(ibuf, linebuf, sizeof(linebuf)) < 0)
				break;
			puts(linebuf);
			lineb = strspn(linebuf, " \t") == strlen(linebuf);
		}
	}
	return (0);
}

/*
 * Touch all the given messages so that they will
 * get mboxed.
 */
int
stouch(void *v)
{
	int *msgvec = v;
	int *ip;

	for (ip = msgvec; *ip != 0; ip++) {
		dot = &message[*ip-1];
		dot->m_flag |= MTOUCH;
		dot->m_flag &= ~MPRESERVE;
	}
	return (0);
}

/*
 * Make sure all passed messages get mboxed.
 */
int
mboxit(void *v)
{
	int *msgvec = v;
	int *ip;

	for (ip = msgvec; *ip != 0; ip++) {
		dot = &message[*ip-1];
		dot->m_flag |= MTOUCH|MBOX;
		dot->m_flag &= ~MPRESERVE;
	}
	return (0);
}

/*
 * List the folders the user currently has.
 */
int
folders(void)
{
	char dirname[PATHSIZE];
	char *cmd;

	if (getfold(dirname, sizeof(dirname)) < 0) {
		printf("No value set for \"folder\"\n");
		return (1);
	}
	if ((cmd = value("LISTER")) == NULL)
		cmd = "ls";
	(void)run_command(cmd, 0, -1, -1, dirname, NULL);
	return (0);
}

/*
 * Update the mail file with any new messages that have
 * come in since we started reading mail.
 */
int
inc(void *v __unused)
{
	int nmsg, mdot;

	nmsg = incfile();

	if (nmsg == 0)
		printf("No new mail.\n");
	else if (nmsg > 0) {
		mdot = newfileinfo(msgCount - nmsg);
		dot = &message[mdot - 1];
	} else
		printf("\"inc\" command failed...\n");

	return (0);
}
