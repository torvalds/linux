/*	$OpenBSD: traceroute.h,v 1.8 2024/05/21 05:00:48 jsg Exp $	*/
/*	$NetBSD: traceroute.c,v 1.10 1995/05/21 15:50:45 mycroft Exp $	*/

/*
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
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

/*-
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Van Jacobson.
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

#include <sys/types.h>

#include <netinet/ip_var.h>
#include <netmpls/mpls.h>

#define ICMP_CODE 0;

#define DUMMY_PORT 10010

#define MAX_LSRR		((MAX_IPOPTLEN - 4) / 4)

#define MPLS_LABEL(m)		((m & MPLS_LABEL_MASK) >> MPLS_LABEL_OFFSET)
#define MPLS_EXP(m)		((m & MPLS_EXP_MASK) >> MPLS_EXP_OFFSET)

/*
 * Format of the data in a (udp) probe packet.
 */
struct packetdata {
	u_char seq;		/* sequence number of this packet */
	u_int8_t ttl;		/* ttl packet left with */
	u_char pad[2];
	u_int32_t sec;		/* time packet left */
	u_int32_t usec;
} __packed;

struct tr_conf {
	int		 first_ttl;	/* Set the first TTL or hop limit */
	u_char		 proto;		/* IP payload protocol to use */
	u_int8_t	 max_ttl;	/* Set the maximum TTL / hop limit */
	int		 nprobes;
	u_int16_t 	 port;		/* start udp dest port */
	int		 waittime;	/* time to wait for a response */
	int		 Aflag;		/* lookup ASN */
	int		 dflag;		/* set SO_DEBUG */
	int		 dump;
	int		 lsrr;		/* Loose Source Record Route */
	struct in_addr	 gateway[MAX_LSRR + 1];
	int		 lsrrlen;
	int		 protoset;
	int		 ttl_flag;	/* display ttl on returned packet */
	int		 nflag;		/* print addresses numerically */
	char		*source;
	int		 sump;
	int		 tos;
	int		 tflag;		/* tos value was set */
	int		 xflag;		/* show ICMP extension header */
	int		 verbose;
	u_int		 rtableid;	/* Set the routing table */
	u_short		 ident;
	int		 expected_responses;
};

struct tr_result {
	int		 seq;
	int		 row;
	int		 dup;
	int		 timeout;
	uint8_t		 ttl;
	uint8_t		 resp_ttl;
	char		 hbuf[NI_MAXHOST];
	char		 inetname[NI_MAXHOST];
	char		*asn;
	char		*exthdr;
	char		 to[NI_MAXHOST];
	int		 cc;
	struct timeval	 t1;
	struct timeval	 t2;
	char		 icmp_code[sizeof("!<255>")];
	char		 tos[sizeof(" (TOS=255!)")];
	int		 got_there;
	int		 unreachable;
	int		 inetname_done;
	int		 asn_done;
};

extern int		*waiting_ttls;
extern int32_t		 sec_perturb;
extern int32_t		 usec_perturb;

extern u_char		 packet[512];
extern u_char		*outpacket;	/* last inbound (icmp) packet */

void		 send_probe(struct tr_conf *, int, u_int8_t, struct sockaddr *);
int		 packet_ok(struct tr_conf *, int, struct msghdr *, int, int *);
void		 icmp_code(int, int, int *, int *, struct tr_result *);
void		 check_tos(struct ip*, int *, struct tr_result *);
int		 map_tos(char *, int *);
void		 print(struct tr_conf *, struct sockaddr *, int, const char *,
		     struct tr_result *);
void		 print_exthdr(u_char *, int, struct tr_result *);
void		 gettime(struct timeval *);

void		 catchup_result_rows(struct tr_result *, struct tr_conf *);

extern int		 sndsock;  /* send (udp) socket file descriptor */

extern int		 rcvhlim;
extern struct in6_pktinfo *rcvpktinfo;

extern int		 datalen;  /* How much data */

extern char		*hostname;

extern u_int16_t	 srcport;

extern char *__progname;
