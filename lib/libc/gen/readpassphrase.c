/*	$OpenBSD: readpassphrase.c,v 1.24 2013/11/24 23:51:29 deraadt Exp $	*/

/*
 * Copyright (c) 2000-2002, 2007, 2010
 *	Todd C. Miller <Todd.Miller@courtesan.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Sponsored in part by the Defense Advanced Research Projects
 * Agency (DARPA) and Air Force Research Laboratory, Air Force
 * Materiel Command, USAF, under agreement number F39502-99-1-0512.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "namespace.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <pwd.h>
#include <signal.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <readpassphrase.h>
#include "un-namespace.h"
#include "libc_private.h"

static volatile sig_atomic_t signo[NSIG];

static void handler(int);

char *
readpassphrase(const char *prompt, char *buf, size_t bufsiz, int flags)
{
	ssize_t nr;
	int input, output, save_errno, i, need_restart, input_is_tty;
	char ch, *p, *end;
	struct termios term, oterm;
	struct sigaction sa, savealrm, saveint, savehup, savequit, saveterm;
	struct sigaction savetstp, savettin, savettou, savepipe;

	/* I suppose we could alloc on demand in this case (XXX). */
	if (bufsiz == 0) {
		errno = EINVAL;
		return(NULL);
	}

restart:
	for (i = 0; i < NSIG; i++)
		signo[i] = 0;
	nr = -1;
	save_errno = 0;
	need_restart = 0;
	/*
	 * Read and write to /dev/tty if available.  If not, read from
	 * stdin and write to stderr unless a tty is required.
	 */
	input_is_tty = 0;
	if (!(flags & RPP_STDIN)) {
        	input = output = _open(_PATH_TTY, O_RDWR | O_CLOEXEC);
		if (input == -1) {
			if (flags & RPP_REQUIRE_TTY) {
				errno = ENOTTY;
				return(NULL);
			}
			input = STDIN_FILENO;
			output = STDERR_FILENO;
		} else {
			input_is_tty = 1;
		}
	} else {
		input = STDIN_FILENO;
		output = STDERR_FILENO;
	}

	/*
	 * Turn off echo if possible.
	 * If we are using a tty but are not the foreground pgrp this will
	 * generate SIGTTOU, so do it *before* installing the signal handlers.
	 */
	if (input_is_tty && tcgetattr(input, &oterm) == 0) {
		memcpy(&term, &oterm, sizeof(term));
		if (!(flags & RPP_ECHO_ON))
			term.c_lflag &= ~(ECHO | ECHONL);
		if (term.c_cc[VSTATUS] != _POSIX_VDISABLE)
			term.c_cc[VSTATUS] = _POSIX_VDISABLE;
		(void)tcsetattr(input, TCSAFLUSH|TCSASOFT, &term);
	} else {
		memset(&term, 0, sizeof(term));
		term.c_lflag |= ECHO;
		memset(&oterm, 0, sizeof(oterm));
		oterm.c_lflag |= ECHO;
	}

	/*
	 * Catch signals that would otherwise cause the user to end
	 * up with echo turned off in the shell.  Don't worry about
	 * things like SIGXCPU and SIGVTALRM for now.
	 */
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;		/* don't restart system calls */
	sa.sa_handler = handler;
	(void)__libc_sigaction(SIGALRM, &sa, &savealrm);
	(void)__libc_sigaction(SIGHUP, &sa, &savehup);
	(void)__libc_sigaction(SIGINT, &sa, &saveint);
	(void)__libc_sigaction(SIGPIPE, &sa, &savepipe);
	(void)__libc_sigaction(SIGQUIT, &sa, &savequit);
	(void)__libc_sigaction(SIGTERM, &sa, &saveterm);
	(void)__libc_sigaction(SIGTSTP, &sa, &savetstp);
	(void)__libc_sigaction(SIGTTIN, &sa, &savettin);
	(void)__libc_sigaction(SIGTTOU, &sa, &savettou);

	if (!(flags & RPP_STDIN))
		(void)_write(output, prompt, strlen(prompt));
	end = buf + bufsiz - 1;
	p = buf;
	while ((nr = _read(input, &ch, 1)) == 1 && ch != '\n' && ch != '\r') {
		if (p < end) {
			if ((flags & RPP_SEVENBIT))
				ch &= 0x7f;
			if (isalpha((unsigned char)ch)) {
				if ((flags & RPP_FORCELOWER))
					ch = (char)tolower((unsigned char)ch);
				if ((flags & RPP_FORCEUPPER))
					ch = (char)toupper((unsigned char)ch);
			}
			*p++ = ch;
		}
	}
	*p = '\0';
	save_errno = errno;
	if (!(term.c_lflag & ECHO))
		(void)_write(output, "\n", 1);

	/* Restore old terminal settings and signals. */
	if (memcmp(&term, &oterm, sizeof(term)) != 0) {
		while (tcsetattr(input, TCSAFLUSH|TCSASOFT, &oterm) == -1 &&
		    errno == EINTR && !signo[SIGTTOU])
			continue;
	}
	(void)__libc_sigaction(SIGALRM, &savealrm, NULL);
	(void)__libc_sigaction(SIGHUP, &savehup, NULL);
	(void)__libc_sigaction(SIGINT, &saveint, NULL);
	(void)__libc_sigaction(SIGQUIT, &savequit, NULL);
	(void)__libc_sigaction(SIGPIPE, &savepipe, NULL);
	(void)__libc_sigaction(SIGTERM, &saveterm, NULL);
	(void)__libc_sigaction(SIGTSTP, &savetstp, NULL);
	(void)__libc_sigaction(SIGTTIN, &savettin, NULL);
	(void)__libc_sigaction(SIGTTOU, &savettou, NULL);
	if (input_is_tty)
		(void)_close(input);

	/*
	 * If we were interrupted by a signal, resend it to ourselves
	 * now that we have restored the signal handlers.
	 */
	for (i = 0; i < NSIG; i++) {
		if (signo[i]) {
			kill(getpid(), i);
			switch (i) {
			case SIGTSTP:
			case SIGTTIN:
			case SIGTTOU:
				need_restart = 1;
			}
		}
	}
	if (need_restart)
		goto restart;

	if (save_errno)
		errno = save_errno;
	return(nr == -1 ? NULL : buf);
}

char *
getpass(const char *prompt)
{
	static char buf[_PASSWORD_LEN + 1];

	if (readpassphrase(prompt, buf, sizeof(buf), RPP_ECHO_OFF) == NULL)
		buf[0] = '\0';
	return(buf);
}

static void handler(int s)
{

	signo[s] = 1;
}
