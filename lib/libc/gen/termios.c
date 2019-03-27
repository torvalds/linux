/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1989, 1993
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
__SCCSID("@(#)termios.c	8.2 (Berkeley) 2/21/94");
__FBSDID("$FreeBSD$");

#include "namespace.h"
#include <sys/types.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <sys/time.h>

#include <errno.h>
#include <string.h>
#define	TTYDEFCHARS
#include <termios.h>
#include <unistd.h>
#include "un-namespace.h"

#include "libc_private.h"

int
tcgetattr(int fd, struct termios *t)
{

	return (_ioctl(fd, TIOCGETA, t));
}

int
tcsetattr(int fd, int opt, const struct termios *t)
{
	struct termios localterm;

	if (opt & TCSASOFT) {
		localterm = *t;
		localterm.c_cflag |= CIGNORE;
		t = &localterm;
	}
	switch (opt & ~TCSASOFT) {
	case TCSANOW:
		return (_ioctl(fd, TIOCSETA, t));
	case TCSADRAIN:
		return (_ioctl(fd, TIOCSETAW, t));
	case TCSAFLUSH:
		return (_ioctl(fd, TIOCSETAF, t));
	default:
		errno = EINVAL;
		return (-1);
	}
}

int
tcsetpgrp(int fd, pid_t pgrp)
{
	int s;

	s = pgrp;
	return (_ioctl(fd, TIOCSPGRP, &s));
}

pid_t
tcgetpgrp(int fd)
{
	int s;

	if (_ioctl(fd, TIOCGPGRP, &s) < 0)
		return ((pid_t)-1);

	return ((pid_t)s);
}

pid_t
tcgetsid(int fd)
{
	int s;

	if (_ioctl(fd, TIOCGSID, &s) < 0)
		return ((pid_t)-1);

	return ((pid_t)s);
}

int
tcsetsid(int fd, pid_t pid)
{

	if (pid != getsid(0)) {
		errno = EINVAL;
		return (-1);
	}

	return (_ioctl(fd, TIOCSCTTY, NULL));
}

speed_t
cfgetospeed(const struct termios *t)
{

	return (t->c_ospeed);
}

speed_t
cfgetispeed(const struct termios *t)
{

	return (t->c_ispeed);
}

int
cfsetospeed(struct termios *t, speed_t speed)
{

	t->c_ospeed = speed;
	return (0);
}

int
cfsetispeed(struct termios *t, speed_t speed)
{

	t->c_ispeed = speed;
	return (0);
}

int
cfsetspeed(struct termios *t, speed_t speed)
{

	t->c_ispeed = t->c_ospeed = speed;
	return (0);
}

/*
 * Make a pre-existing termios structure into "raw" mode: character-at-a-time
 * mode with no characters interpreted, 8-bit data path.
 */
void
cfmakeraw(struct termios *t)
{

	t->c_iflag &= ~(IMAXBEL|IXOFF|INPCK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL|IXON|IGNPAR);
	t->c_iflag |= IGNBRK;
	t->c_oflag &= ~OPOST;
	t->c_lflag &= ~(ECHO|ECHOE|ECHOK|ECHONL|ICANON|ISIG|IEXTEN|NOFLSH|TOSTOP|PENDIN);
	t->c_cflag &= ~(CSIZE|PARENB);
	t->c_cflag |= CS8|CREAD;
	t->c_cc[VMIN] = 1;
	t->c_cc[VTIME] = 0;
}

/*
 * Obtain a termios structure which is similar to the one provided by
 * the kernel.
 */
void
cfmakesane(struct termios *t)
{

	t->c_cflag = TTYDEF_CFLAG;
	t->c_iflag = TTYDEF_IFLAG;
	t->c_lflag = TTYDEF_LFLAG;
	t->c_oflag = TTYDEF_OFLAG;
	t->c_ispeed = TTYDEF_SPEED;
	t->c_ospeed = TTYDEF_SPEED;
	memcpy(&t->c_cc, ttydefchars, sizeof ttydefchars);
}

int
tcsendbreak(int fd, int len __unused)
{
	struct timeval sleepytime;

	sleepytime.tv_sec = 0;
	sleepytime.tv_usec = 400000;
	if (_ioctl(fd, TIOCSBRK, 0) == -1)
		return (-1);
	(void)_select(0, 0, 0, 0, &sleepytime);
	if (_ioctl(fd, TIOCCBRK, 0) == -1)
		return (-1);
	return (0);
}

int
__libc_tcdrain(int fd)
{

	return (_ioctl(fd, TIOCDRAIN, 0));
}

#pragma weak tcdrain
int
tcdrain(int fd)
{

	return (((int (*)(int))
	    __libc_interposing[INTERPOS_tcdrain])(fd));
}

__weak_reference(__libc_tcdrain, __tcdrain);
__weak_reference(__libc_tcdrain, _tcdrain);

int
tcflush(int fd, int which)
{
	int com;

	switch (which) {
	case TCIFLUSH:
		com = FREAD;
		break;
	case TCOFLUSH:
		com = FWRITE;
		break;
	case TCIOFLUSH:
		com = FREAD | FWRITE;
		break;
	default:
		errno = EINVAL;
		return (-1);
	}
	return (_ioctl(fd, TIOCFLUSH, &com));
}

int
tcflow(int fd, int action)
{
	struct termios term;
	u_char c;

	switch (action) {
	case TCOOFF:
		return (_ioctl(fd, TIOCSTOP, 0));
	case TCOON:
		return (_ioctl(fd, TIOCSTART, 0));
	case TCION:
	case TCIOFF:
		if (tcgetattr(fd, &term) == -1)
			return (-1);
		c = term.c_cc[action == TCIOFF ? VSTOP : VSTART];
		if (c != _POSIX_VDISABLE && _write(fd, &c, sizeof(c)) == -1)
			return (-1);
		return (0);
	default:
		errno = EINVAL;
		return (-1);
	}
	/* NOTREACHED */
}
