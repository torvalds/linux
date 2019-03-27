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
static const char sccsid[] = "@(#)io.c	8.1 (Berkeley) 6/6/93";
#endif

/*
 * This file contains the I/O handling and the exchange of
 * edit characters. This connection itself is established in
 * ctl.c
 */

#include <sys/filio.h>

#include <errno.h>
#include <signal.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define _XOPEN_SOURCE_EXTENDED
#include <curses.h>

#include "talk.h"
#include "talk_ctl.h"

extern void	display(xwin_t *, wchar_t *);

volatile sig_atomic_t gotwinch = 0;

/*
 * The routine to do the actual talking
 */
void
talk(void)
{
	struct hostent *hp, *hp2;
	struct pollfd fds[2];
	int nb;
	wchar_t buf[BUFSIZ];
	char **addr, *his_machine_name;
	FILE *sockfp;

	his_machine_name = NULL;
	hp = gethostbyaddr((const char *)&his_machine_addr.s_addr,
	    sizeof(his_machine_addr.s_addr), AF_INET);
	if (hp != NULL) {
		hp2 = gethostbyname(hp->h_name);
		if (hp2 != NULL && hp2->h_addrtype == AF_INET &&
		    hp2->h_length == sizeof(his_machine_addr))
			for (addr = hp2->h_addr_list; *addr != NULL; addr++)
				if (memcmp(*addr, &his_machine_addr,
				    sizeof(his_machine_addr)) == 0) {
					his_machine_name = strdup(hp->h_name);
					break;
				}
	}
	if (his_machine_name == NULL)
		his_machine_name = strdup(inet_ntoa(his_machine_addr));
	snprintf((char *)buf, sizeof(buf), "Connection established with %s@%s.",
	    msg.r_name, his_machine_name);
	free(his_machine_name);
	message((char *)buf);
	write(STDOUT_FILENO, "\007\007\007", 3);
	
	current_line = 0;

	if ((sockfp = fdopen(sockt, "w+")) == NULL)
		p_error("fdopen");

	setvbuf(sockfp, NULL, _IONBF, 0);
	setvbuf(stdin, NULL, _IONBF, 0);

	/*
	 * Wait on both the other process (sockt) and standard input.
	 */
	for (;;) {
		fds[0].fd = fileno(stdin);
		fds[0].events = POLLIN;
		fds[1].fd = sockt;
		fds[1].events = POLLIN;
		nb = poll(fds, 2, INFTIM);
		if (gotwinch) {
			resize_display();
			gotwinch = 0;
		}
		if (nb <= 0) {
			if (errno == EINTR)
				continue;
			/* Panic, we don't know what happened. */
			p_error("Unexpected error from poll");
			quit();
		}
		if (fds[1].revents & POLLIN) {
			wint_t w;

			/* There is data on sockt. */
			w = fgetwc(sockfp);
			if (w == WEOF) {
				message("Connection closed. Exiting");
				quit();
			}
			display(&his_win, &w);
		}
		if (fds[0].revents & POLLIN) {
			wint_t w;

			if ((w = getwchar()) != WEOF) {
				display(&my_win, &w);
				(void )fputwc(w, sockfp);
				(void )fflush(sockfp);
			}
		}
	}
}

/*
 * p_error prints the system error message on the standard location
 * on the screen and then exits. (i.e. a curses version of perror)
 */
void
p_error(const char *string)
{
	wmove(my_win.x_win, current_line, 0);
	wprintw(my_win.x_win, "[%s : %s (%d)]\n",
	    string, strerror(errno), errno);
	wrefresh(my_win.x_win);
	move(LINES-1, 0);
	refresh();
	quit();
}

/*
 * Display string in the standard location
 */
void
message(const char *string)
{
	wmove(my_win.x_win, current_line, 0);
	wprintw(my_win.x_win, "[%s]\n", string);
	if (current_line < my_win.x_nlines - 1)
		current_line++;
	wrefresh(my_win.x_win);
}
