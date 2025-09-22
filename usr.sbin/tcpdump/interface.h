/*	$OpenBSD: interface.h,v 1.88 2024/05/21 05:00:48 jsg Exp $	*/

/*
 * Copyright (c) 1988, 1989, 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code distributions
 * retain the above copyright notice and this paragraph in its entirety, (2)
 * distributions including binary code include the above copyright notice and
 * this paragraph in its entirety in the documentation or other materials
 * provided with the distribution, and (3) all advertising materials mentioning
 * features or use of this software display the following acknowledgement:
 * ``This product includes software developed by the University of California,
 * Lawrence Berkeley Laboratory and its contributors.'' Neither the name of
 * the University nor the names of its contributors may be used to endorse
 * or promote products derived from this software without specific prior
 * written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 * @(#) $Id: interface.h,v 1.88 2024/05/21 05:00:48 jsg Exp $ (LBL)
 */

#ifndef tcpdump_interface_h
#define tcpdump_interface_h

#ifdef HAVE_OS_PROTO_H
#include "os-proto.h"
#endif

struct tok {
	int v;			/* value */
	char *s;		/* string */
};

extern int aflag;		/* translate network and broadcast addresses */
extern int dflag;		/* print filter code */
extern int eflag;		/* print ethernet header */
extern int fflag;		/* don't translate "foreign" IP address */
extern int Iflag;		/* include interface in output */
extern int nflag;		/* leave addresses as numbers */
extern int Nflag;		/* remove domains from printed host names */
extern int oflag;		/* OS fingerprint */
extern int qflag;		/* quick (shorter) output */
extern int Sflag;		/* print raw TCP sequence numbers */
extern int tflag;		/* print packet arrival time */
extern int vflag;		/* verbose */
extern int xflag;		/* print packet in hex */
extern int Xflag;		/* print packet in hex/ascii */

extern int packettype;		/* as specified by -T */
extern char *device;		/* as specified by -i  */
#define PT_VAT		1	/* Visual Audio Tool */
#define PT_WB		2	/* distributed White Board */
#define PT_RPC		3	/* Remote Procedure Call */
#define PT_RTP		4	/* Real-Time Applications protocol */
#define PT_RTCP		5	/* Real-Time Applications control protocol */
#define PT_CNFP		6	/* Cisco NetFlow protocol */
#define PT_VRRP		7	/* Virtual Router Redundancy protocol */
#define PT_TCP		8	/* TCP */
#define PT_GRE		9	/* Generic Routing Encapsulation (over UDP) */
#define PT_MPLS		10	/* MPLS (over UDP) */
#define PT_TFTP		11	/* Trivial File Transfer Protocol */
#define PT_VXLAN	12	/* Virtual eXtensible Local Area Network */
#define PT_ERSPAN	13	/* GRE ERSPAN Type I or II */
#define PT_WIREGUARD	14	/* WireGuard tunnel */
#define PT_GENEVE	15	/* Geneve */

#ifndef min
#define min(a,b) ((a)>(b)?(b):(a))
#endif
#ifndef max
#define max(a,b) ((b)>(a)?(b):(a))
#endif

/*
 * The default snapshot length.  This value allows most printers to print
 * useful information while keeping the amount of unwanted data down.
 * In particular, it allows ethernet, tcp/ip headers, and a small amount
 * of data, or to capture IPv6 and TCP headers after pflog encapsulation.
 */
#define DEFAULT_SNAPLEN 116
#define IEEE802_11_SNAPLEN (DEFAULT_SNAPLEN + 30)
#define IEEE802_11_RADIO_SNAPLEN (IEEE802_11_SNAPLEN + 64)

#ifndef BIG_ENDIAN
#define BIG_ENDIAN 4321
#define LITTLE_ENDIAN 1234
#endif

#ifdef ETHER_HEADER_HAS_EA
#define ESRC(ep) ((ep)->ether_shost.ether_addr_octet)
#define EDST(ep) ((ep)->ether_dhost.ether_addr_octet)
#else
#define ESRC(ep) ((ep)->ether_shost)
#define EDST(ep) ((ep)->ether_dhost)
#endif

#ifdef ETHER_ARP_HAS_X
#define SHA(ap) ((ap)->arp_xsha)
#define THA(ap) ((ap)->arp_xtha)
#define SPA(ap) ((ap)->arp_xspa)
#define TPA(ap) ((ap)->arp_xtpa)
#else
#ifdef ETHER_ARP_HAS_EA
#define SHA(ap) ((ap)->arp_sha.ether_addr_octet)
#define THA(ap) ((ap)->arp_tha.ether_addr_octet)
#else
#define SHA(ap) ((ap)->arp_sha)
#define THA(ap) ((ap)->arp_tha)
#endif
#define SPA(ap) ((ap)->arp_spa)
#define TPA(ap) ((ap)->arp_tpa)
#endif

#ifndef NTOHL
#define NTOHL(x)	(x) = ntohl(x)
#define NTOHS(x)	(x) = ntohs(x)
#define HTONL(x)	(x) = htonl(x)
#define HTONS(x)	(x) = htons(x)
#endif
#endif

extern char *program_name;	/* used to generate self-identifying messages */

extern int32_t thiszone;	/* seconds offset from gmt to local time */

extern int snaplen;
/* global pointers to beginning and end of current packet (during printing) */
extern const u_char *packetp;
extern const u_char *snapend;

/*
 * True if  "l" bytes of "var" were captured.
 *
 * The "snapend - (l) <= snapend" checks to make sure "l" isn't so large
 * that "snapend - (l)" underflows.
 *
 * The check is for <= rather than < because "l" might be 0.
 */
#define TTEST2(var, l) (snapend - (l) <= snapend && \
			(const u_char *)&(var) <= snapend - (l))

/* True if "var" was captured */
#define TTEST(var) TTEST2(var, sizeof(var))

/* Bail if "l" bytes of "var" were not captured */
#define TCHECK2(var, l) if (!TTEST2(var, l)) goto trunc

/* Bail if "var" was not captured */
#define TCHECK(var) TCHECK2(var, sizeof(var))

struct timeval;
struct bpf_timeval;

extern void ts_print(const struct bpf_timeval *);

extern int fn_print(const u_char *, const u_char *);
extern int fn_printn(const u_char *, u_int, const u_char *);
extern void relts_print(int);
extern const char *tok2str(const struct tok *, const char *, int);
extern char *dnaddr_string(u_short);
extern void safeputs(const char *);
extern void safeputchar(int);
extern void printb(char *, unsigned short, char *);

extern __dead void error(const char *, ...)
    __attribute__((__format__ (printf, 1, 2)));
extern void warning(const char *, ...)
    __attribute__ ((__format__ (printf, 1, 2)));

extern char *read_infile(char *);
extern char *copy_argv(char * const *);

extern char *isonsap_string(const u_char *);
extern char *llcsap_string(u_char);
extern char *protoid_string(const u_char *);
extern char *dnname_string(u_short);
extern char *dnnum_string(u_short);

/* The printer routines. */

struct pcap_pkthdr;

extern int ether_encap_print(u_short, const u_char *, u_int, u_int);
extern int llc_print(const u_char *, u_int, u_int, const u_char *,
	const u_char *);
extern int pppoe_if_print(u_short, const u_char *, u_int, u_int);
extern void aarp_print(const u_char *, u_int);
extern void arp_print(const u_char *, u_int, u_int);
extern void atalk_print(const u_char *, u_int);
extern void atalk_print_llap(const u_char *, u_int);
extern void atm_if_print(u_char *, const struct pcap_pkthdr *, const u_char *);
extern void bootp_print(const u_char *, u_int, u_short, u_short);
extern void bgp_print(const u_char *, int);
extern void decnet_print(const u_char *, u_int, u_int);
extern void default_print(const u_char *, u_int);
extern void dvmrp_print(const u_char *, u_int);
extern void enc_if_print(u_char *, const struct pcap_pkthdr *, const u_char *);
extern void pflog_if_print(u_char *, const struct pcap_pkthdr *,
        const u_char *);
extern void pfsync_if_print(u_char *, const struct pcap_pkthdr *,
        const u_char *);
extern void pfsync_ip_print(const u_char *, u_int, const u_char *);
extern void ether_if_print(u_char *, const struct pcap_pkthdr *,
	const u_char *);
extern void ether_tryprint(const u_char *, u_int, int);
extern void fddi_if_print(u_char *, const struct pcap_pkthdr *, const u_char *);
extern void ppp_ether_if_print(u_char *, const struct pcap_pkthdr *,
	const u_char *);
extern void gre_print(const u_char *, u_int);
extern void vxlan_print(const u_char *, u_int);
extern void geneve_print(const u_char *, u_int);
extern void nsh_print(const u_char *, u_int);
extern void nhrp_print(const u_char *, u_int);
extern void icmp_print(const u_char *, u_int, const u_char *);
extern void ieee802_11_if_print(u_char *, const struct pcap_pkthdr *,
    const u_char *);
extern void ieee802_11_radio_if_print(u_char *, const struct pcap_pkthdr *,
    const u_char *);
extern void iapp_print(const u_char *, u_int);
extern void igrp_print(const u_char *, u_int, const u_char *);
extern void ip_print(const u_char *, u_int);
extern void ipx_print(const u_char *, u_int);
extern void isoclns_print(const u_char *, u_int, u_int, const u_char *,
	const u_char *);
extern void krb_print(const u_char *, u_int);
extern void netbeui_print(u_short, const u_char *, const u_char *);
extern void ipx_netbios_print(const u_char *, const u_char *);
extern void nbt_tcp_print(const u_char *, int);
extern void nbt_udp137_print(const u_char *data, int);
extern void nbt_udp138_print(const u_char *data, int);
extern char *smb_errstr(int, int);
extern void print_data(const unsigned char *, int);
extern void l2tp_print(const u_char *dat, u_int length);
extern void vrrp_print(const u_char *bp, u_int len, int ttl);
extern void carp_print(const u_char *bp, u_int len, int ttl);
extern void hsrp_print(const u_char *, u_int);
extern void vqp_print(const u_char *, u_int);
extern void nfsreply_print(const u_char *, u_int, const u_char *);
extern void nfsreq_print(const u_char *, u_int, const u_char *);
extern void ns_print(const u_char *, u_int, int);
extern void ntp_print(const u_char *, u_int);
extern void loop_if_print(u_char *, const struct pcap_pkthdr *, const u_char *);
extern void null_if_print(u_char *, const struct pcap_pkthdr *, const u_char *);
extern void ospf_print(const u_char *, u_int, const u_char *);
extern void mobile_print(const u_char *, u_int);
extern void pim_print(const u_char *, u_int);
extern void ppp_if_print(u_char *, const struct pcap_pkthdr *, const u_char *);
extern void ppp_hdlc_if_print(u_char *, const struct pcap_pkthdr *,
    const u_char *);
extern void ppp_print(const u_char *, u_int);
extern void ppp_hdlc_print(const u_char *, u_int);
extern void raw_if_print(u_char *, const struct pcap_pkthdr *, const u_char *);
extern void rip_print(const u_char *, u_int);
extern void sl_if_print(u_char *, const struct pcap_pkthdr *, const u_char *);
extern void sl_bsdos_if_print(u_char *, const struct pcap_pkthdr *,
    const u_char *);
extern void snmp_print(const u_char *, u_int);
extern void sunrpcrequest_print(const u_char *, u_int, const u_char *);
extern void cnfp_print(const u_char *, u_int);
extern void tcp_print(const u_char *, u_int, const u_char *);
extern void tftp_print(const u_char *, u_int);
extern void wg_print(const u_char *, u_int);
extern void timed_print(const u_char *, u_int);
extern void udp_print(const u_char *, u_int, const void *);
extern void wb_print(const void *, u_int);
extern void ike_print(const u_char *, u_int);
extern void udpencap_print(const u_char *, u_int, const u_char *);
extern void ah_print(const u_char *, u_int, const u_char *);
extern void esp_print(const u_char *, u_int, const u_char *);
extern void cdp_print(const u_char *, u_int, u_int, int);
extern void stp_print(const u_char *, u_int);
extern void radius_print(const u_char *, u_int);
extern void lwres_print(const u_char *, u_int);
extern void ether_print(const u_char *, u_int);
extern void etherip_print(const u_char *, u_int, u_int);
extern void ipcomp_print(const u_char *, u_int, const u_char *);
extern void mpls_print(const u_char *, u_int);
extern void lldp_print(const u_char *, u_int);
extern void slow_print(const u_char *, u_int);
extern void gtp_print(const u_char *, u_int, u_short, u_short);
extern void ofp_print(const u_char *, u_int);
extern void ofp_if_print(u_char *, const struct pcap_pkthdr *, const u_char *);
extern void usbpcap_if_print(u_char *, const struct pcap_pkthdr *,
    const u_char *);

extern void ip6_print(const u_char *, u_int);
extern void ip6_opt_print(const u_char *, int);
extern int hbhopt_print(const u_char *);
extern int dstopt_print(const u_char *);
extern int frag6_print(const u_char *, const u_char *);
extern void icmp6_print(const u_char *, u_int, const u_char *);
extern void ripng_print(const u_char *, int);
extern int rt6_print(const u_char *, const u_char *);
extern void ospf6_print(const u_char *, u_int);
extern void dhcp6_print(const u_char *, u_int);

extern uint32_t in_cksum_add(const void *, size_t, uint32_t);
extern uint16_t in_cksum_fini(uint32_t);
extern uint16_t in_cksum(const void *, size_t, uint32_t);
extern u_int16_t in_cksum_shouldbe(u_int16_t, u_int16_t);

extern uint32_t wg_match(const u_char *, u_int);
