/*	$OpenBSD: pcap-namedb.h,v 1.8 2014/04/11 04:08:58 lteo Exp $	*/

/*
 * Copyright (c) 1994, 1996
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the Computer Systems
 *	Engineering Group at Lawrence Berkeley Laboratory.
 * 4. Neither the name of the University nor of the Laboratory may be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
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

#ifndef lib_pcap_ethers_h
#define lib_pcap_ethers_h

/*
 * As returned by the pcap_next_etherent()
 * XXX this stuff doesn't belong in this interface, but this
 * library already must do name to address translation, so
 * on systems that don't have support for /etc/ethers, we
 * export these hooks since they'll
 */
struct pcap_etherent {
	u_char addr[6];
	char name[122];
};
#ifndef PCAP_ETHERS_FILE
#define PCAP_ETHERS_FILE "/etc/ethers"
#endif
struct	pcap_etherent *pcap_next_etherent(FILE *);
u_char *pcap_ether_hostton(const char*);
u_char *pcap_ether_aton(const char *);

bpf_u_int32 **pcap_nametoaddr(const char *);
#ifdef INET6
struct addrinfo *pcap_nametoaddrinfo(const char *);
#endif
bpf_u_int32 pcap_nametonetaddr(const char *);

int	pcap_nametoport(const char *, int *, int *);
int	pcap_nametoproto(const char *);
int	pcap_nametoeproto(const char *);
int	pcap_nametollc(const char *);

/*
 * If a protocol is unknown, PROTO_UNDEF is returned.
 * Also, pcap_nametoport() returns the protocol along with the port number.
 * If there are ambiguous entried in /etc/services (i.e. domain
 * can be either tcp or udp) PROTO_UNDEF is returned.
 */
#define PROTO_UNDEF		-1

/* XXX move these to pcap-int.h? */
int __pcap_atodn(const char *, bpf_u_int32 *);
int __pcap_atoin(const char *, bpf_u_int32 *);
u_short	__pcap_nametodnaddr(const char *);

#endif
