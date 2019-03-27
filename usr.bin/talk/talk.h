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
 *
 *	@(#)talk.h	8.1 (Berkeley) 6/6/93
 * $FreeBSD$
 */

#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <protocols/talkd.h>
#include <curses.h>
#include <signal.h>

extern	int sockt;
extern	int curses_initialized;
extern	int invitation_waiting;

extern	const char *current_state;
extern	int current_line;

extern volatile sig_atomic_t gotwinch;

typedef struct xwin {
	WINDOW	*x_win;
	int	x_nlines;
	int	x_ncols;
	int	x_line;
	int	x_col;
	char	kill;
	char	cerase;
	char	werase;
} xwin_t;

extern	xwin_t my_win;
extern	xwin_t his_win;
extern	WINDOW *line_win;

extern	void	announce_invite(void);
extern	int	check_local(void);
extern	void	check_writeable(void);
extern	void	ctl_transact(struct in_addr,CTL_MSG,int,CTL_RESPONSE *);
extern	void	disp_msg(int);
extern	void	end_msgs(void);
extern	void	get_addrs(const char *, const char *);
extern	int	get_iface(struct in_addr *, struct in_addr *);
extern	void	get_names(int, char **);
extern	void	init_display(void);
extern	void	invite_remote(void);
extern	int	look_for_invite(CTL_RESPONSE *);
extern	int	max(int, int);
extern	void	message(const char *);
extern	void	open_ctl(void);
extern	void	open_sockt(void);
extern	void	p_error(const char *);
extern	void	print_addr(struct sockaddr_in);
extern	void	quit(void);
extern	int	readwin(WINDOW *, int, int);
extern	void	re_invite(int);
extern	void	send_delete(void);
extern	void	set_edit_chars(void);
extern	void	sig_sent(int);
extern	void	sig_winch(int);
extern	void	start_msgs(void);
extern	void	talk(void);
extern	void	resize_display(void);
