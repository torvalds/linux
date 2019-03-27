/*-
 * Copyright (c) 2016 Mariusz Zaborski <oshogbo@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _CAPSICUM_HELPERS_H_
#define	_CAPSICUM_HELPERS_H_

#include <sys/param.h>
#include <sys/capsicum.h>
#include <sys/ioctl.h>

#include <errno.h>
#include <nl_types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include <libcasper.h>

#define	CAPH_IGNORE_EBADF	0x0001
#define	CAPH_READ		0x0002
#define	CAPH_WRITE		0x0004
#define	CAPH_LOOKUP		0x0008

__BEGIN_DECLS

static __inline int
caph_limit_stream(int fd, int flags)
{
	cap_rights_t rights;
	unsigned long cmds[] = { TIOCGETA, TIOCGWINSZ, FIODTYPE };

	cap_rights_init(&rights, CAP_EVENT, CAP_FCNTL, CAP_FSTAT,
	    CAP_IOCTL, CAP_SEEK);

	if ((flags & CAPH_READ) != 0)
		cap_rights_set(&rights, CAP_READ);
	if ((flags & CAPH_WRITE) != 0)
		cap_rights_set(&rights, CAP_WRITE);
	if ((flags & CAPH_LOOKUP) != 0)
		cap_rights_set(&rights, CAP_LOOKUP);

	if (cap_rights_limit(fd, &rights) < 0 && errno != ENOSYS) {
		if (errno == EBADF && (flags & CAPH_IGNORE_EBADF) != 0)
			return (0);
		return (-1);
	}

	if (cap_ioctls_limit(fd, cmds, nitems(cmds)) < 0 && errno != ENOSYS)
		return (-1);

	if (cap_fcntls_limit(fd, CAP_FCNTL_GETFL) < 0 && errno != ENOSYS)
		return (-1);

	return (0);
}

static __inline int
caph_limit_stdin(void)
{

	return (caph_limit_stream(STDIN_FILENO, CAPH_READ));
}

static __inline int
caph_limit_stderr(void)
{

	return (caph_limit_stream(STDERR_FILENO, CAPH_WRITE));
}

static __inline int
caph_limit_stdout(void)
{

	return (caph_limit_stream(STDOUT_FILENO, CAPH_WRITE));
}

static __inline int
caph_limit_stdio(void)
{
	const int iebadf = CAPH_IGNORE_EBADF;

	if (caph_limit_stream(STDIN_FILENO, CAPH_READ | iebadf) == -1 ||
	    caph_limit_stream(STDOUT_FILENO, CAPH_WRITE | iebadf) == -1 ||
	    caph_limit_stream(STDERR_FILENO, CAPH_WRITE | iebadf) == -1)
		return (-1);
	return (0);
}

static __inline void
caph_cache_tzdata(void)
{

	tzset();
}

static __inline void
caph_cache_catpages(void)
{

	(void)catopen("libc", NL_CAT_LOCALE);
}

static __inline int
caph_enter(void)
{

	if (cap_enter() < 0 && errno != ENOSYS)
		return (-1);

	return (0);
}

static __inline int
caph_rights_limit(int fd, const cap_rights_t *rights)
{

	if (cap_rights_limit(fd, rights) < 0 && errno != ENOSYS)
		return (-1);

	return (0);
}

static __inline int
caph_ioctls_limit(int fd, const unsigned long *cmds, size_t ncmds)
{

	if (cap_ioctls_limit(fd, cmds, ncmds) < 0 && errno != ENOSYS)
		return (-1);

	return (0);
}

static __inline int
caph_fcntls_limit(int fd, uint32_t fcntlrights)
{

	if (cap_fcntls_limit(fd, fcntlrights) < 0 && errno != ENOSYS)
		return (-1);

	return (0);
}

static __inline int
caph_enter_casper(void)
{

	return (CASPER_SUPPORT == 0 ? 0 : caph_enter());
}

__END_DECLS

#endif /* _CAPSICUM_HELPERS_H_ */
