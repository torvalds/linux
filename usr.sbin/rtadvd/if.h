/*	$FreeBSD$	*/
/*	$KAME: if.h,v 1.10 2003/02/24 11:29:10 ono Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 1995, 1996, 1997, 1998, and 1999 WIDE Project.
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
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#define	UPDATE_IFINFO_ALL	0

struct sockinfo {
	int		si_fd;
	const char	*si_name;
};

extern struct sockinfo sock;
extern struct sockinfo rtsock;
extern struct sockinfo ctrlsock;

int lladdropt_length(struct sockaddr_dl *);
void lladdropt_fill(struct sockaddr_dl *, struct nd_opt_hdr *);
int rtbuf_len(void);
char *get_next_msg(char *, char *, int, size_t *, int);
struct in6_addr *get_addr(char *);
int get_rtm_ifindex(char *);
int get_prefixlen(char *);
int prefixlen(unsigned char *, unsigned char *);

struct ifinfo	*update_ifinfo(struct ifilist_head_t *, int);
int		update_ifinfo_nd_flags(struct ifinfo *);
struct ifinfo	*update_persist_ifinfo(struct ifilist_head_t *,
			const char *);

int		sock_mc_join(struct sockinfo *, int);
int		sock_mc_leave(struct sockinfo *, int);
int		sock_mc_rr_update(struct sockinfo *, char *);
int		getinet6sysctl(int);
