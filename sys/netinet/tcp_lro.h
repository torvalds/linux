/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006, Myricom Inc.
 * Copyright (c) 2008, Intel Corporation.
 * Copyright (c) 2016 Mellanox Technologies.
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

#ifndef _TCP_LRO_H_
#define _TCP_LRO_H_

#include <sys/time.h>

#ifndef TCP_LRO_ENTRIES
/* Define default number of LRO entries per RX queue */
#define	TCP_LRO_ENTRIES	8
#endif

struct lro_entry {
	LIST_ENTRY(lro_entry)	next;
	LIST_ENTRY(lro_entry)	hash_next;
	struct mbuf		*m_head;
	struct mbuf		*m_tail;
	union {
		struct ip	*ip4;
		struct ip6_hdr	*ip6;
	} leip;
	union {
		in_addr_t	s_ip4;
		struct in6_addr	s_ip6;
	} lesource;
	union {
		in_addr_t	d_ip4;
		struct in6_addr	d_ip6;
	} ledest;
	uint16_t		source_port;
	uint16_t		dest_port;
	uint16_t		eh_type;	/* EthernetHeader type. */
	uint16_t		append_cnt;
	uint32_t		p_len;		/* IP header payload length. */
	uint32_t		ulp_csum;	/* TCP, etc. checksum. */
	uint32_t		next_seq;	/* tcp_seq */
	uint32_t		ack_seq;	/* tcp_seq */
	uint32_t		tsval;
	uint32_t		tsecr;
	uint16_t		window;
	uint16_t		timestamp;	/* flag, not a TCP hdr field. */
	struct timeval		mtime;
};
LIST_HEAD(lro_head, lro_entry);

#define	le_ip4			leip.ip4
#define	le_ip6			leip.ip6
#define	source_ip4		lesource.s_ip4
#define	dest_ip4		ledest.d_ip4
#define	source_ip6		lesource.s_ip6
#define	dest_ip6		ledest.d_ip6

struct lro_mbuf_sort {
	uint64_t seq;
	struct mbuf *mb;
};

/* NB: This is part of driver structs. */
struct lro_ctrl {
	struct ifnet	*ifp;
	struct lro_mbuf_sort *lro_mbuf_data;
	uint64_t	lro_queued;
	uint64_t	lro_flushed;
	uint64_t	lro_bad_csum;
	unsigned	lro_cnt;
	unsigned	lro_mbuf_count;
	unsigned	lro_mbuf_max;
	unsigned short	lro_ackcnt_lim;		/* max # of aggregated ACKs */
	unsigned 	lro_length_lim;		/* max len of aggregated data */

	u_long		lro_hashsz;
	struct lro_head	*lro_hash;
	struct lro_head	lro_active;
	struct lro_head	lro_free;
};

#define	TCP_LRO_LENGTH_MAX	65535
#define	TCP_LRO_ACKCNT_MAX	65535		/* unlimited */

int tcp_lro_init(struct lro_ctrl *);
int tcp_lro_init_args(struct lro_ctrl *, struct ifnet *, unsigned, unsigned);
void tcp_lro_free(struct lro_ctrl *);
void tcp_lro_flush_inactive(struct lro_ctrl *, const struct timeval *);
void tcp_lro_flush(struct lro_ctrl *, struct lro_entry *);
void tcp_lro_flush_all(struct lro_ctrl *);
int tcp_lro_rx(struct lro_ctrl *, struct mbuf *, uint32_t);
void tcp_lro_queue_mbuf(struct lro_ctrl *, struct mbuf *);

#define	TCP_LRO_NO_ENTRIES	-2
#define	TCP_LRO_CANNOT		-1
#define	TCP_LRO_NOT_SUPPORTED	1

#endif /* _TCP_LRO_H_ */
