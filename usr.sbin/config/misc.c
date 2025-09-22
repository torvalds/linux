/*	$OpenBSD: misc.c,v 1.11 2021/11/20 03:13:37 jcs Exp $	*/

/*
 * Copyright (c) 1997 Tobias Weingartner
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>

#include <ctype.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "misc.h"

extern int verbose;

int
parse_cmd(cmd_t *cmd, char *lbuf)
{
	char *cp, *buf;

	lbuf[strcspn(lbuf, "\n")] = '\0';
	if (verbose)
		printf("%s\n", lbuf);

	/* Parse input */
	buf = lbuf;
	buf = &buf[strspn(buf, " \t")];
	cp = &buf[strcspn(buf, " \t")];
	*cp++ = '\0';
	strlcpy(cmd->cmd, buf, sizeof cmd->cmd);
	buf = &cp[strspn(cp, " \t")];
	strlcpy(cmd->args, buf, sizeof cmd->args);

	return (0);
}

int
ask_cmd(cmd_t *cmd)
{
	char lbuf[100];
	extern FILE *cmdfp;

	/* Get input */
	if (fgets(lbuf, sizeof lbuf, cmdfp ? cmdfp : stdin) == NULL) {
		if (cmdfp) {
			cmd->cmd[0] = '\0';
			return -1;
		}
		errx(1, "eof");
	}
	if (cmdfp)
		printf("%s", lbuf);
	return parse_cmd(cmd, lbuf);
}

int
ask_yn(const char *str)
{
	extern FILE *cmdfp;
	int ch, first;

	printf("%s [n] ", str);
	fflush(stdout);

	first = ch = getc(cmdfp ? cmdfp : stdin);
	if (verbose || (cmdfp && ch != EOF)) {
		printf("%c", ch);
		fflush(stdout);
	}
	while (ch != '\n' && ch != EOF) {
		ch = getc(cmdfp ? cmdfp : stdin);
		if (verbose) {
			printf("%c\n", ch);
			fflush(stdout);
		} else if (cmdfp)
			putchar('\n');
	}
	if (ch == EOF || first == EOF)
		errx(1, "eof");

	return (first == 'y' || first == 'Y');
}
