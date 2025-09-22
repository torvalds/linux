/*	$OpenBSD: save.c,v 1.14 2019/06/28 13:32:52 deraadt Exp $	*/
/*	$NetBSD: save.c,v 1.4 1995/03/24 05:02:13 cgd Exp $	*/

/*
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

#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "mille.h"

/*
 * @(#)save.c	1.2 (Berkeley) 3/28/83
 */

typedef	struct stat	STAT;

/*
 *	This routine saves the current game for use at a later date.
 *	Returns FALSE if it couldn't be done.
 */
bool
save(void)
{
	char	*sp;
	int	outf;
	time_t	*tp;
	char	buf[256];
	time_t	tme;
	STAT	junk;
	bool	rv;

	sp = NULL;
	tp = &tme;
	if (Fromfile && getyn(SAMEFILEPROMPT))
		strlcpy(buf, Fromfile, sizeof(buf));
	else {
over:
		prompt(FILEPROMPT);
		leaveok(Board, FALSE);
		refresh();
		sp = buf;
		while ((*sp = readch()) != '\n' && *sp != '\r' &&
		    (sp - buf < (int)sizeof(buf))) {
			if (*sp == killchar())
				goto over;
			else if (*sp == erasechar()) {
				if (--sp < buf)
					sp = buf;
				else {
					addch('\b');
					/*
					 * if the previous char was a control
					 * char, cover up two characters.
					 */
					if (*sp < ' ')
						addch('\b');
					clrtoeol();
				}
			}
			else {
				addstr(unctrl(*sp));
				++sp;
			}
			refresh();
		}
		*sp = '\0';
		leaveok(Board, TRUE);
	}

	/*
	 * check for existing files, and confirm overwrite if needed
	 */

	if (sp == buf || (!Fromfile && stat(buf, &junk) > -1
	    && getyn(OVERWRITEFILEPROMPT) == FALSE))
		return FALSE;

	if ((outf = open(buf, O_CREAT | O_TRUNC | O_WRONLY, 0644)) == -1) {
		error(strerror(errno));
		return FALSE;
	}
	mvwaddstr(Score, ERR_Y, ERR_X, buf);
	wrefresh(Score);
	time(tp);			/* get current time		*/
	rv = varpush(outf, writev);
	close(outf);
	if (!rv)
		unlink(buf);
	else {
		strlcpy(buf, ctime(tp), sizeof buf);
		for (sp = buf; *sp != '\n'; sp++)
			continue;
		*sp = '\0';
		wprintw(Score, " [%s]", buf);
	}
	wclrtoeol(Score);
	wrefresh(Score);
	return rv;
}

/*
 *	This does the actual restoring.  It returns TRUE if the
 * backup was made on exiting, in which case certain things must
 * be cleaned up before the game starts.
 */
bool
rest_f(const char *file)
{
	char	*sp;
	int	inf;
	char	buf[80];
	STAT	sbuf;

	if ((inf = open(file, O_RDONLY)) == -1)
		err(1, "%s", file);
	if (fstat(inf, &sbuf) == -1)		/* get file stats	*/
		err(1, "%s", file);
	varpush(inf, readv);
	close(inf);
	strlcpy(buf, ctime(&sbuf.st_mtime), sizeof buf);
	for (sp = buf; *sp != '\n'; sp++)
		continue;
	*sp = '\0';
	/*
	 * initialize some necessary values
	 */
	(void)snprintf(Initstr, sizeof Initstr, "%s [%s]\n", file, buf);
	Fromfile = file;
	return !On_exit;
}
