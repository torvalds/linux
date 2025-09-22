/*	$OpenBSD: show.h,v 1.16 2020/10/29 21:15:26 denis Exp $ */

/*
 * Copyright (c) 2004 Claudio Jeker <claudio@openbsd.org>
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

#ifndef __SHOW_H__
#define __SHOW_H__

union sockunion {
	struct sockaddr		sa;
	struct sockaddr_in	sin;
	struct sockaddr_in6	sin6;
	struct sockaddr_dl	sdl;
	struct sockaddr_rtlabel	rtlabel;
	struct sockaddr_mpls	smpls;
	struct sockaddr_storage	padding;
};

void	 printsource(int, u_int);
void	 get_rtaddrs(int, struct sockaddr *, struct sockaddr **);
void	 p_rttables(int, u_int, char);
void	 p_sockaddr(struct sockaddr *, struct sockaddr *, int, int);
char	*routename(struct sockaddr *);
char	*netname(struct sockaddr *, struct sockaddr *);
char	*mpls_op(u_int32_t);
size_t	 get_sysctl(const int *, u_int, char **);

extern int nflag;
extern int Fflag;
extern int verbose;
extern union sockunion so_label;

#define PLEN  (LONG_BIT / 4 + 2)

#endif /* __SHOW_H__ */
