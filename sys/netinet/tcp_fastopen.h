/*-
 * Copyright (c) 2015-2017 Patrick Kelsey
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

#ifndef _TCP_FASTOPEN_H_
#define _TCP_FASTOPEN_H_

#ifdef _KERNEL

#include "opt_inet.h"

#define	TCP_FASTOPEN_COOKIE_LEN		8	/* SipHash24 64-bit output */

#ifdef TCP_RFC7413
VNET_DECLARE(unsigned int, tcp_fastopen_client_enable);
#define	V_tcp_fastopen_client_enable	VNET(tcp_fastopen_client_enable)

VNET_DECLARE(unsigned int, tcp_fastopen_server_enable);
#define	V_tcp_fastopen_server_enable	VNET(tcp_fastopen_server_enable)
#else
#define	V_tcp_fastopen_client_enable	0
#define	V_tcp_fastopen_server_enable	0
#endif  /* TCP_RFC7413 */

union tcp_fastopen_ip_addr {
	struct in_addr v4;
	struct in6_addr v6;
};

struct tcp_fastopen_ccache_entry {
	TAILQ_ENTRY(tcp_fastopen_ccache_entry) cce_link;
	union tcp_fastopen_ip_addr cce_client_ip;	/* network byte order */
	union tcp_fastopen_ip_addr cce_server_ip;	/* network byte order */
	uint16_t server_port;				/* network byte order */
	uint16_t server_mss;				/* host byte order */
	uint8_t af;
	uint8_t cookie_len;
	uint8_t cookie[TCP_FASTOPEN_MAX_COOKIE_LEN];
	sbintime_t disable_time; /* non-zero value means path is disabled */
};

struct tcp_fastopen_ccache;

struct tcp_fastopen_ccache_bucket {
	struct mtx	ccb_mtx;
	TAILQ_HEAD(bucket_entries, tcp_fastopen_ccache_entry) ccb_entries;
	int		ccb_num_entries;
	struct tcp_fastopen_ccache *ccb_ccache;
};

struct tcp_fastopen_ccache {
	uma_zone_t 	zone;
	struct tcp_fastopen_ccache_bucket *base;
	unsigned int 	bucket_limit;
	unsigned int 	buckets;
	unsigned int 	mask;
	uint32_t 	secret;
};

#ifdef TCP_RFC7413
void	tcp_fastopen_init(void);
void	tcp_fastopen_destroy(void);
unsigned int *tcp_fastopen_alloc_counter(void);
void	tcp_fastopen_decrement_counter(unsigned int *);
int	tcp_fastopen_check_cookie(struct in_conninfo *, uint8_t *, unsigned int,
	    uint64_t *);
void	tcp_fastopen_connect(struct tcpcb *);
void	tcp_fastopen_disable_path(struct tcpcb *);
void	tcp_fastopen_update_cache(struct tcpcb *, uint16_t, uint8_t,
	    uint8_t *);
#else
#define tcp_fastopen_init()			((void)0)
#define tcp_fastopen_destroy()			((void)0)
#define tcp_fastopen_alloc_counter()		NULL
#define tcp_fastopen_decrement_counter(c)	((void)0)
#define tcp_fastopen_check_cookie(i, c, l, lc)	(-1)
#define tcp_fastopen_connect(t)			((void)0)
#define tcp_fastopen_disable_path(t)		((void)0)
#define tcp_fastopen_update_cache(t, m, l, c)	((void)0)
#endif /* TCP_RFC7413 */

#endif /* _KERNEL */

#endif /* _TCP_FASTOPEN_H_ */
