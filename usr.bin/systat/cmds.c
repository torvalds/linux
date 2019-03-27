/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1980, 1992, 1993
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

#ifdef lint
static const char sccsid[] = "@(#)cmds.c	8.2 (Berkeley) 4/29/95";
#endif

#include <sys/param.h>

#include <ctype.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "systat.h"
#include "extern.h"

void
command(const char *cmd)
{
	struct cmdtab *p;
	char *cp, *tmpstr, *tmpstr1;
	double t;

	tmpstr = tmpstr1 = strdup(cmd);
	for (cp = tmpstr1; *cp && !isspace(*cp); cp++)
		;
	if (*cp)
		*cp++ = '\0';
	if (*tmpstr1 == '\0')
		goto done;
	for (; *cp && isspace(*cp); cp++)
		;
	if (strcmp(tmpstr1, "quit") == 0 || strcmp(tmpstr1, "q") == 0)
		die(0);
	if (strcmp(tmpstr1, "load") == 0) {
		load();
		goto done;
	}
	if (strcmp(tmpstr1, "stop") == 0) {
		delay = 0;
		mvaddstr(CMDLINE, 0, "Refresh disabled.");
		clrtoeol();
		goto done;
	}
	if (strcmp(tmpstr1, "help") == 0) {
		int _col, _len;

		move(CMDLINE, _col = 0);
		for (p = cmdtab; p->c_name; p++) {
			_len = strlen(p->c_name);
			if (_col + _len > COLS)
				break;
			addstr(p->c_name); _col += _len;
			if (_col + 1 < COLS)
				addch(' ');
		}
		clrtoeol();
		goto done;
	}
	t = strtod(tmpstr1, NULL) * 1000000.0;
	if (t > 0 && t < (double)UINT_MAX)
		delay = (unsigned int)t;
	if ((t <= 0 || t > (double)UINT_MAX) &&
	    (strcmp(tmpstr1, "start") == 0 ||
	    strcmp(tmpstr1, "interval") == 0)) {
		if (*cp != '\0') {
			t = strtod(cp, NULL) * 1000000.0;
			if (t <= 0 || t >= (double)UINT_MAX) {
				error("%d: bad interval.", (int)t);
				goto done;
			}
		}
	}
	if (t > 0) {
		delay = (unsigned int)t;
		display();
		status();
		goto done;
	}
	p = lookup(tmpstr1);
	if (p == (struct cmdtab *)-1) {
		error("%s: Ambiguous command.", tmpstr1);
		goto done;
	}
	if (p) {
		if (curcmd == p)
			goto done;
		(*curcmd->c_close)(wnd);
		curcmd->c_flags &= ~CF_INIT;
		wnd = (*p->c_open)();
		if (wnd == NULL) {
			error("Couldn't open new display");
			wnd = (*curcmd->c_open)();
			if (wnd == NULL) {
				error("Couldn't change back to previous cmd");
				exit(1);
			}
			p = curcmd;
		}
		if ((p->c_flags & CF_INIT) == 0) {
			if ((*p->c_init)())
				p->c_flags |= CF_INIT;
			else
				goto done;
		}
		curcmd = p;
		labels();
		display();
		status();
		goto done;
	}
	if (curcmd->c_cmd == NULL || !(*curcmd->c_cmd)(tmpstr1, cp))
		error("%s: Unknown command.", tmpstr1);
done:
	free(tmpstr);
}

struct cmdtab *
lookup(const char *name)
{
	const char *p, *q;
	struct cmdtab *ct, *found;
	int nmatches, longest;

	longest = 0;
	nmatches = 0;
	found = (struct cmdtab *) 0;
	for (ct = cmdtab; (p = ct->c_name); ct++) {
		for (q = name; *q == *p++; q++)
			if (*q == 0)		/* exact match? */
				return (ct);
		if (!*q) {			/* the name was a prefix */
			if (q - name > longest) {
				longest = q - name;
				nmatches = 1;
				found = ct;
			} else if (q - name == longest)
				nmatches++;
		}
	}
	if (nmatches > 1)
		return ((struct cmdtab *)-1);
	return (found);
}

void
status(void)
{

	error("Showing %s, refresh every %d seconds.",
	  curcmd->c_name, delay / 1000000);
}

int
prefix(const char *s1, const char *s2)
{

	while (*s1 == *s2) {
		if (*s1 == '\0')
			return (1);
		s1++, s2++;
	}
	return (*s1 == '\0');
}
