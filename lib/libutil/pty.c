/*	$OpenBSD: pty.c,v 1.22 2022/04/20 14:00:19 millert Exp $	*/

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

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <grp.h>
#include <sys/tty.h>

#include "util.h"

int
getptmfd(void)
{
	return (open(PATH_PTMDEV, O_RDWR|O_CLOEXEC));
}

int
openpty(int *amaster, int *aslave, char *name, const struct termios *termp,
    const struct winsize *winp)
{
	int ptmfd;

	if ((ptmfd = getptmfd()) == -1)
		return (-1);
	if (fdopenpty(ptmfd, amaster, aslave, name, termp, winp) == -1) {
		close(ptmfd);
		return (-1);
	}
	close(ptmfd);
	return (0);
}

int
fdopenpty(int ptmfd, int *amaster, int *aslave, char *name,
    const struct termios *termp, const struct winsize *winp)
{
	int master, slave;
	struct ptmget ptm;

	/*
	 * Use /dev/ptm and the PTMGET ioctl to get a properly set up and
	 * owned pty/tty pair.
	 */
	if (ioctl(ptmfd, PTMGET, &ptm) == -1)
		return (-1);

	master = ptm.cfd;
	slave = ptm.sfd;
	if (name) {
		/*
		 * Manual page says "at least 16 characters".
		 */
		strlcpy(name, ptm.sn, 16);
	}
	*amaster = master;
	*aslave = slave;
	if (termp)
		(void) tcsetattr(slave, TCSAFLUSH, termp);
	if (winp)
		(void) ioctl(slave, TIOCSWINSZ, winp);
	return (0);
}

pid_t
forkpty(int *amaster, char *name, const struct termios *termp,
    const struct winsize *winp)
{
	int ptmfd;
	pid_t pid;

	if ((ptmfd = getptmfd()) == -1)
		return (-1);
	if ((pid = fdforkpty(ptmfd, amaster, name, termp, winp)) == -1) {
		close(ptmfd);
		return (-1);
	}
	close(ptmfd);
	return (pid);
}

pid_t
fdforkpty(int ptmfd, int *amaster, char *name, const struct termios *termp,
    const struct winsize *winp)
{
	int master, slave;
	pid_t pid;

	if (fdopenpty(ptmfd, &master, &slave, name, termp, winp) == -1)
		return (-1);
	switch (pid = fork()) {
	case -1:
		(void) close(master);
		(void) close(slave);
		return (-1);
	case 0:
		/*
		 * child
		 */
		(void) close(master);
		login_tty(slave);
		return (0);
	}
	/*
	 * parent
	 */
	*amaster = master;
	(void) close(slave);
	return (pid);
}
