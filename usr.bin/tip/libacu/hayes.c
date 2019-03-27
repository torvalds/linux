/*	$OpenBSD: hayes.c,v 1.13 2006/03/17 19:17:13 moritz Exp $	*/
/*	$NetBSD: hayes.c,v 1.6 1997/02/11 09:24:17 mrg Exp $	*/

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
static char sccsid[] = "@(#)hayes.c	8.1 (Berkeley) 6/6/93";
static const char rcsid[] = "$OpenBSD: hayes.c,v 1.13 2006/03/17 19:17:13 moritz Exp $";
#endif
#endif /* not lint */

/*
 * Routines for calling up on a Hayes Modem
 * (based on the old VenTel driver).
 * The modem is expected to be strapped for "echo".
 * Also, the switches enabling the DTR and CD lines
 * must be set correctly.
 * NOTICE:
 * The easy way to hang up a modem is always simply to
 * clear the DTR signal. However, if the +++ sequence
 * (which switches the modem back to local mode) is sent
 * before modem is hung up, removal of the DTR signal
 * has no effect (except that it prevents the modem from
 * recognizing commands).
 * (by Helge Skrivervik, Calma Company, Sunnyvale, CA. 1984) 
 */
/*
 * TODO:
 * It is probably not a good idea to switch the modem
 * state between 'verbose' and terse (status messages).
 * This should be kicked out and we should use verbose 
 * mode only. This would make it consistent with normal
 * interactive use thru the command 'tip dialer'.
 */
#include "tip.h"

#include <termios.h>
#include <sys/ioctl.h>

#define	min(a,b)	((a < b) ? a : b)

static	int dialtimeout = 0;
static	jmp_buf timeoutbuf;

#define DUMBUFLEN	40
static char dumbuf[DUMBUFLEN];

#define	DIALING		1
#define IDLE		2
#define CONNECTED	3
#define	FAILED		4
static	int state = IDLE;

static void	sigALRM(int);
static char	gobble(char *);
static void	error_rep(char);
static void	goodbye(void);
static int	hay_sync(void);

int
hay_dialer(char *num, char *acu)
{
	char *cp;
	int connected = 0;
	char dummy;
	struct termios cntrl;
#ifdef ACULOG
	char line[80];
#endif
	if (hay_sync() == 0)		/* make sure we can talk to the modem */
		return(0);
	if (boolean(value(VERBOSE)))
		printf("\ndialing...");
	fflush(stdout);
	tcgetattr(FD, &cntrl);
	cntrl.c_cflag |= HUPCL;
	tcsetattr(FD, TCSANOW, &cntrl);
	tcflush(FD, TCIOFLUSH);
	write(FD, "ATv0\r", 5);	/* tell modem to use short status codes */
	gobble("\r");
	gobble("\r");
	write(FD, "ATTD", 4);	/* send dial command */
	for (cp = num; *cp; cp++)
		if (*cp == '=')
			*cp = ',';
	write(FD, num, strlen(num));
	state = DIALING;
	write(FD, "\r", 1);
	connected = 0;
	if (gobble("\r")) {
		if ((dummy = gobble("01234")) != '1')
			error_rep(dummy);
		else
			connected = 1;
	}
	if (connected)
		state = CONNECTED;
	else {
		state = FAILED;
		return (connected);	/* lets get out of here.. */
	}
	tcflush(FD, TCIOFLUSH);
#ifdef ACULOG
	if (dialtimeout) {
		(void)snprintf(line, sizeof line, "%ld second dial timeout",
			number(value(DIALTIMEOUT)));
		logent(value(HOST), num, "hayes", line);
	}
#endif
	if (dialtimeout)
		hay_disconnect();	/* insurance */
	return (connected);
}

void
hay_disconnect(void)
{
	/* first hang up the modem*/
#ifdef DEBUG
	printf("\rdisconnecting modem....\n\r");
#endif
	ioctl(FD, TIOCCDTR, 0);
	sleep(1);
	ioctl(FD, TIOCSDTR, 0);
	goodbye();
}

void
hay_abort(void)
{
	write(FD, "\r", 1);	/* send anything to abort the call */
	hay_disconnect();
}

/*ARGSUSED*/
static void
sigALRM(int signo)
{
	printf("\07timeout waiting for reply\n\r");
	dialtimeout = 1;
	longjmp(timeoutbuf, 1);
}

static char
gobble(char *match)
{
	char c;
	sig_t f;
	size_t i;
	int status = 0;

	f = signal(SIGALRM, sigALRM);
	dialtimeout = 0;
#ifdef DEBUG
	printf("\ngobble: waiting for %s\n", match);
#endif
	do {
		if (setjmp(timeoutbuf)) {
			signal(SIGALRM, f);
			return (0);
		}
		alarm(number(value(DIALTIMEOUT)));
		read(FD, &c, 1);
		alarm(0);
		c &= 0177;
#ifdef DEBUG
		printf("%c 0x%x ", c, c);
#endif
		for (i = 0; i < strlen(match); i++)
			if (c == match[i])
				status = c;
	} while (status == 0);
	signal(SIGALRM, SIG_DFL);
#ifdef DEBUG
	printf("\n");
#endif
	return (status);
}

static void
error_rep(char c)
{
	printf("\n\r");
	switch (c) {

	case '0':
		printf("OK");
		break;

	case '1':
		printf("CONNECT");
		break;

	case '2':
		printf("RING");
		break;

	case '3':
		printf("NO CARRIER");
		break;

	case '4':
		printf("ERROR in input");
		break;

	case '5':
		printf("CONNECT 1200");
		break;

	default:
		printf("Unknown Modem error: %c (0x%x)", c, c);
	}
	printf("\n\r");
	return;
}

/*
 * set modem back to normal verbose status codes.
 */
static void
goodbye(void)
{
	int len;
	char c;

	tcflush(FD, TCIOFLUSH);
	if (hay_sync()) {
		sleep(1);
#ifndef DEBUG
		tcflush(FD, TCIOFLUSH);
#endif
		write(FD, "ATH0\r", 5);		/* insurance */
#ifndef DEBUG
		c = gobble("03");
		if (c != '0' && c != '3') {
			printf("cannot hang up modem\n\r");
			printf("please use 'tip dialer' to make sure the line is hung up\n\r");
		}
#endif
		sleep(1);
		ioctl(FD, FIONREAD, &len);
#ifdef DEBUG
		printf("goodbye1: len=%d -- ", len);
		rlen = read(FD, dumbuf, min(len, DUMBUFLEN));
		dumbuf[rlen] = '\0';
		printf("read (%d): %s\r\n", rlen, dumbuf);
#endif
		write(FD, "ATv1\r", 5);
		sleep(1);
#ifdef DEBUG
		ioctl(FD, FIONREAD, &len);
		printf("goodbye2: len=%d -- ", len);
		rlen = read(FD, dumbuf, min(len, DUMBUFLEN));
		dumbuf[rlen] = '\0';
		printf("read (%d): %s\r\n", rlen, dumbuf);
#endif
	}
	tcflush(FD, TCIOFLUSH);
	ioctl(FD, TIOCCDTR, 0);		/* clear DTR (insurance) */
	close(FD);
}

#define MAXRETRY	5

static int
hay_sync(void)
{
	int len, retry = 0;

	while (retry++ <= MAXRETRY) {
		write(FD, "AT\r", 3);
		sleep(1);
		ioctl(FD, FIONREAD, &len);
		if (len) {
			len = read(FD, dumbuf, min(len, DUMBUFLEN));
			if (strchr(dumbuf, '0') || 
		   	(strchr(dumbuf, 'O') && strchr(dumbuf, 'K')))
				return(1);
#ifdef DEBUG
			dumbuf[len] = '\0';
			printf("hay_sync: (\"%s\") %d\n\r", dumbuf, retry);
#endif
		}
		ioctl(FD, TIOCCDTR, 0);
		ioctl(FD, TIOCSDTR, 0);
	}
	printf("Cannot synchronize with hayes...\n\r");
	return(0);
}
