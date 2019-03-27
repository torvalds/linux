/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Mark Johnston <markj@FreeBSD.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_sctp.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sdt.h>

SDT_PROVIDER_DEFINE(ip);
#ifdef SCTP
SDT_PROVIDER_DEFINE(sctp);
#endif
SDT_PROVIDER_DEFINE(tcp);
SDT_PROVIDER_DEFINE(udp);
SDT_PROVIDER_DEFINE(udplite);

SDT_PROBE_DEFINE6_XLATE(ip, , , receive,
    "void *", "pktinfo_t *",
    "void *", "csinfo_t *",
    "uint8_t *", "ipinfo_t *",
    "struct ifnet *", "ifinfo_t *",
    "struct ip *", "ipv4info_t *",
    "struct ip6_hdr *", "ipv6info_t *");

SDT_PROBE_DEFINE6_XLATE(ip, , , send,
    "void *", "pktinfo_t *",
    "void *", "csinfo_t *",
    "uint8_t *", "ipinfo_t *",
    "struct ifnet *", "ifinfo_t *",
    "struct ip *", "ipv4info_t *",
    "struct ip6_hdr *", "ipv6info_t *");

#ifdef SCTP
SDT_PROBE_DEFINE5_XLATE(sctp, , , receive,
    "void *", "pktinfo_t *",
    "struct sctp_tcb *", "csinfo_t *",
    "struct mbuf *", "ipinfo_t *",
    "struct sctp_tcb *", "sctpsinfo_t *" ,
    "struct sctphdr *", "sctpinfo_t *");

SDT_PROBE_DEFINE5_XLATE(sctp, , , send,
    "void *", "pktinfo_t *",
    "struct sctp_tcb *", "csinfo_t *",
    "uint8_t *", "ipinfo_t *",
    "struct sctp_tcb *", "sctpsinfo_t *" ,
    "struct sctphdr *", "sctpinfo_t *");

SDT_PROBE_DEFINE6_XLATE(sctp, , , state__change,
    "void *", "void *",
    "struct sctp_tcb *", "csinfo_t *",
    "void *", "void *",
    "struct sctp_tcb *", "sctpsinfo_t *",
    "void *", "void *",
    "int", "sctplsinfo_t *");
#endif

SDT_PROBE_DEFINE5_XLATE(tcp, , , accept__established,
    "void *", "pktinfo_t *",
    "struct tcpcb *", "csinfo_t *",
    "struct mbuf *", "ipinfo_t *",
    "struct tcpcb *", "tcpsinfo_t *" ,
    "struct tcphdr *", "tcpinfoh_t *");

SDT_PROBE_DEFINE5_XLATE(tcp, , , accept__refused,
    "void *", "pktinfo_t *",
    "struct tcpcb *", "csinfo_t *",
    "struct mbuf *", "ipinfo_t *",
    "struct tcpcb *", "tcpsinfo_t *" ,
    "struct tcphdr *", "tcpinfo_t *");

SDT_PROBE_DEFINE5_XLATE(tcp, , , connect__established,
    "void *", "pktinfo_t *",
    "struct tcpcb *", "csinfo_t *",
    "struct mbuf *", "ipinfo_t *",
    "struct tcpcb *", "tcpsinfo_t *" ,
    "struct tcphdr *", "tcpinfoh_t *");

SDT_PROBE_DEFINE5_XLATE(tcp, , , connect__refused,
    "void *", "pktinfo_t *",
    "struct tcpcb *", "csinfo_t *",
    "struct mbuf *", "ipinfo_t *",
    "struct tcpcb *", "tcpsinfo_t *" ,
    "struct tcphdr *", "tcpinfoh_t *");

SDT_PROBE_DEFINE5_XLATE(tcp, , , connect__request,
    "void *", "pktinfo_t *",
    "struct tcpcb *", "csinfo_t *",
    "uint8_t *", "ipinfo_t *",
    "struct tcpcb *", "tcpsinfo_t *" ,
    "struct tcphdr *", "tcpinfo_t *");

SDT_PROBE_DEFINE5_XLATE(tcp, , , receive,
    "void *", "pktinfo_t *",
    "struct tcpcb *", "csinfo_t *",
    "struct mbuf *", "ipinfo_t *",
    "struct tcpcb *", "tcpsinfo_t *" ,
    "struct tcphdr *", "tcpinfoh_t *");

SDT_PROBE_DEFINE5_XLATE(tcp, , , send,
    "void *", "pktinfo_t *",
    "struct tcpcb *", "csinfo_t *",
    "uint8_t *", "ipinfo_t *",
    "struct tcpcb *", "tcpsinfo_t *" ,
    "struct tcphdr *", "tcpinfo_t *");

SDT_PROBE_DEFINE1_XLATE(tcp, , , siftr,
    "struct pkt_node *", "siftrinfo_t *");

SDT_PROBE_DEFINE3_XLATE(tcp, , , debug__input,
    "struct tcpcb *", "tcpsinfo_t *" ,
    "struct tcphdr *", "tcpinfoh_t *",
    "uint8_t *", "ipinfo_t *");

SDT_PROBE_DEFINE3_XLATE(tcp, , , debug__output,
    "struct tcpcb *", "tcpsinfo_t *" ,
    "struct tcphdr *", "tcpinfo_t *",
    "struct mbuf *", "ipinfo_t *");

SDT_PROBE_DEFINE2_XLATE(tcp, , , debug__user,
    "struct tcpcb *", "tcpsinfo_t *" ,
    "int", "int");

SDT_PROBE_DEFINE3_XLATE(tcp, , , debug__drop,
    "struct tcpcb *", "tcpsinfo_t *" ,
    "struct tcphdr *", "tcpinfoh_t *",
    "struct mbuf *", "ipinfo_t *");

SDT_PROBE_DEFINE6_XLATE(tcp, , , state__change,
    "void *", "void *",
    "struct tcpcb *", "csinfo_t *",
    "void *", "void *",
    "struct tcpcb *", "tcpsinfo_t *",
    "void *", "void *",
    "int", "tcplsinfo_t *");

SDT_PROBE_DEFINE6_XLATE(tcp, , , receive__autoresize,
    "void *", "void *",
    "struct tcpcb *", "csinfo_t *",
    "struct mbuf *", "ipinfo_t *",
    "struct tcpcb *", "tcpsinfo_t *" ,
    "struct tcphdr *", "tcpinfoh_t *",
    "int", "int");

SDT_PROBE_DEFINE5_XLATE(udp, , , receive,
    "void *", "pktinfo_t *",
    "struct inpcb *", "csinfo_t *",
    "uint8_t *", "ipinfo_t *",
    "struct inpcb *", "udpsinfo_t *",
    "struct udphdr *", "udpinfo_t *");

SDT_PROBE_DEFINE5_XLATE(udp, , , send,
    "void *", "pktinfo_t *",
    "struct inpcb *", "csinfo_t *",
    "uint8_t *", "ipinfo_t *",
    "struct inpcb *", "udpsinfo_t *",
    "struct udphdr *", "udpinfo_t *");

SDT_PROBE_DEFINE5_XLATE(udplite, , , receive,
    "void *", "pktinfo_t *",
    "struct inpcb *", "csinfo_t *",
    "uint8_t *", "ipinfo_t *",
    "struct inpcb *", "udplitesinfo_t *",
    "struct udphdr *", "udpliteinfo_t *");

SDT_PROBE_DEFINE5_XLATE(udplite, , , send,
    "void *", "pktinfo_t *",
    "struct inpcb *", "csinfo_t *",
    "uint8_t *", "ipinfo_t *",
    "struct inpcb *", "udplitesinfo_t *",
    "struct udphdr *", "udpliteinfo_t *");
