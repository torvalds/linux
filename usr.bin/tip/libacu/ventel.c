/*	$OpenBSD: ventel.c,v 1.12 2006/03/17 19:17:13 moritz Exp $	*/
/*	$NetBSD: ventel.c,v 1.6 1997/02/11 09:24:21 mrg Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#ifndef lint
#if 0
static char sccsid[] = "@(#)ventel.c	8.1 (Berkeley) 6/6/93";
static const char rcsid[] = "$OpenBSD: ventel.c,v 1.12 2006/03/17 19:17:13 moritz Exp $";
#endif
#endif /* not lint */

/*
 * Routines for calling up on a Ventel Modem
 * The Ventel is expected to be strapped for local echo (just like uucp)
 */
#include "tip.h"
#include <termios.h>
#include <sys/ioctl.h>

#define	MAXRETRY	5

static	int dialtimeout = 0;
static	jmp_buf timeoutbuf;

static void	echo(char *);
static void	sigALRM(int);
static int	gobble(char, char *);
static int	vensync(int);

/*
 * some sleep calls have been replaced by this macro
 * because some ventel modems require two <cr>s in less than
 * a second in order to 'wake up'... yes, it is dirty...
 */
#define delay(num,denom) busyloop(CPUSPEED*num/denom)
#define CPUSPEED 1000000	/* VAX 780 is 1MIPS */
#define DELAY(n) do { long N = (n); while (--N > 0); } while (0)
#define busyloop(n) do { DELAY(n); } while (0)

int
ven_dialer(char *num, char *acu)
{
	char *cp;
	int connected = 0;
	char *msg, line[80];
	struct termios	cntrl;

	/*
	 * Get in synch with a couple of carriage returns
	 */
	if (!vensync(FD)) {
		printf("can't synchronize with ventel\n");
#ifdef ACULOG
		logent(value(HOST), num, "ventel", "can't synch up");
#endif
		return (0);
	}
	if (boolean(value(VERBOSE)))
		printf("\ndialing...");
	fflush(stdout);
	tcgetattr(FD, &cntrl);
	cntrl.c_cflag |= HUPCL;
	tcsetattr(FD, TCSANOW, &cntrl);
	echo("#k$\r$\n$D$I$A$L$:$ ");
	for (cp = num; *cp; cp++) {
		delay(1, 10);
		write(FD, cp, 1);
	}
	delay(1, 10);
	write(FD, "\r", 1);
	gobble('\n', line);
	if (gobble('\n', line))
		connected = gobble('!', line);
	tcflush(FD, TCIOFLUSH);
#ifdef ACULOG
	if (dialtimeout) {
		(void)snprintf(line, sizeof line, "%ld second dial timeout",
			number(value(DIALTIMEOUT)));
		logent(value(HOST), num, "ventel", line);
	}
#endif
	if (dialtimeout)
		ven_disconnect();	/* insurance */
	if (connected || dialtimeout || !boolean(value(VERBOSE)))
		return (connected);
	/* call failed, parse response for user */
	cp = strchr(line, '\r');
	if (cp)
		*cp = '\0';
	for (cp = line; (cp = strchr(cp, ' ')) != NULL; cp++)
		if (cp[1] == ' ')
			break;
	if (cp) {
		while (*cp == ' ')
			cp++;
		msg = cp;
		while (*cp) {
			if (isupper(*cp))
				*cp = tolower(*cp);
			cp++;
		}
		printf("%s...", msg);
	}
	return (connected);
}

void
ven_disconnect(void)
{
	close(FD);
}

void
ven_abort(void)
{
	write(FD, "\03", 1);
	close(FD);
}

static void
echo(char *s)
{
	char c;

	while ((c = *s++) != '\0')
		switch (c) {
		case '$':
			read(FD, &c, 1);
			s++;
			break;

		case '#':
			c = *s++;
			write(FD, &c, 1);
			break;

		default:
			write(FD, &c, 1);
			read(FD, &c, 1);
		}
}

/*ARGSUSED*/
static void
sigALRM(int signo)
{
	printf("\07timeout waiting for reply\n");
	dialtimeout = 1;
	longjmp(timeoutbuf, 1);
}

static int
gobble(char match, char response[])
{
	char *cp = response;
	sig_t f;
	char c;

	f = signal(SIGALRM, sigALRM);
	dialtimeout = 0;
	do {
		if (setjmp(timeoutbuf)) {
			signal(SIGALRM, f);
			*cp = '\0';
			return (0);
		}
		alarm(number(value(DIALTIMEOUT)));
		read(FD, cp, 1);
		alarm(0);
		c = (*cp++ &= 0177);
#ifdef notdef
		if (boolean(value(VERBOSE)))
			putchar(c);
#endif
	} while (c != '\n' && c != match);
	signal(SIGALRM, SIG_DFL);
	*cp = '\0';
	return (c == match);
}

#define min(a,b)	((a)>(b)?(b):(a))
/*
 * This convoluted piece of code attempts to get
 * the ventel in sync.  If you don't have FIONREAD
 * there are gory ways to simulate this.
 */
static int
vensync(int fd)
{
	int already = 0, nread;
	char buf[60];

	/*
	 * Toggle DTR to force anyone off that might have left
	 * the modem connected, and insure a consistent state
	 * to start from.
	 *
	 * If you don't have the ioctl calls to diddle directly
	 * with DTR, you can always try setting the baud rate to 0.
	 */
	ioctl(FD, TIOCCDTR, 0);
	sleep(1);
	ioctl(FD, TIOCSDTR, 0);
	while (already < MAXRETRY) {
		/*
		 * After reseting the modem, send it two \r's to
		 * autobaud on. Make sure to delay between them
		 * so the modem can frame the incoming characters.
		 */
		write(fd, "\r", 1);
		delay(1,10);
		write(fd, "\r", 1);
		sleep(2);
		if (ioctl(fd, FIONREAD, (caddr_t)&nread) < 0) {
			perror("tip: ioctl");
			continue;
		}
		while (nread > 0) {
			read(fd, buf, min(nread, 60));
			if ((buf[nread - 1] & 0177) == '$')
				return (1);
			nread -= min(nread, 60);
		}
		sleep(1);
		already++;
	}
	return (0);
}
