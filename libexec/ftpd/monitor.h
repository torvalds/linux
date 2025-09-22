/*	$OpenBSD: monitor.h,v 1.5 2007/03/01 20:06:27 otto Exp $	*/

/*
 * Copyright (c) 2004 Moritz Jodeit <moritz@openbsd.org>
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
 */

#ifndef _MONITOR_H
#define _MONITOR_H

#define FTPD_PRIVSEP_USER "_ftp"

enum auth_ret {
	AUTH_FAILED,
	AUTH_SLAVE,
	AUTH_MONITOR
};

int	monitor_init(void);
int	monitor_post_auth(void);
void	monitor_user(char *);
int	monitor_pass(char *);
int	monitor_socket(int);
int	monitor_bind(int, struct sockaddr *, socklen_t);

void	kill_slave(char *);

void	send_fd(int, int);
int	recv_fd(int);

#endif	/* _MONITOR_H */
