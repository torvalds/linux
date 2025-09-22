/*	$OpenBSD: parser.h,v 1.29 2014/02/04 15:22:39 eric Exp $	*/

/*
 * Copyright (c) 2013 Eric Faurot	<eric@openbsd.org>
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

enum {
	P_TOKEN,
	P_STR,
	P_INT,
	P_MSGID,
	P_EVPID,
	P_ROUTEID,
	P_ADDR,
};

struct parameter {
	int	type;
	union {
		const char	*u_str;
		int		 u_int;
		uint32_t	 u_msgid;
		uint64_t	 u_evpid;
		uint64_t	 u_routeid;
		struct sockaddr_storage u_ss;
	} u;
};

int cmd_install(const char *, int (*)(int, struct parameter *));
int cmd_run(int, char **);
int cmd_show_params(int argc, struct parameter *argv);
