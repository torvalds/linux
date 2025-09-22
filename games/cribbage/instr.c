/*	$OpenBSD: instr.c,v 1.14 2016/01/10 13:35:09 mestre Exp $	*/
/*	$NetBSD: instr.c,v 1.5 1997/07/10 06:47:30 mikel Exp $	*/

/*-
 * Copyright (c) 1990, 1993
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

#include <sys/wait.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <stdlib.h>
#include <unistd.h>

#include "pathnames.h"

void
instructions(void)
{
	int pstat;
	int fd;
	pid_t pid;
	const char *pager;

	if ((fd = open(_PATH_INSTR, O_RDONLY)) == - 1)
		errx(1, "can't open %s", _PATH_INSTR);

	switch (pid = vfork()) {
	case -1:
		err(1, "vfork");
	case 0:
		if (!isatty(1))
			pager = "/bin/cat";
		else {
			if (!(pager = getenv("PAGER")) || (*pager == 0))
				pager = _PATH_MORE;
		}
		if (dup2(fd, 0) == -1)
			err(1, "dup2");
		execl(_PATH_BSHELL, "sh", "-c", pager, (char *)NULL);
		err(1, "exec sh -c %s", pager);
	default:
		do {
			pid = waitpid(pid, &pstat, 0);
		} while (pid == -1 && errno == EINTR);
		close(fd);
		if (pid == -1 || WEXITSTATUS(pstat))
			exit(1);
		break;
	}
}
