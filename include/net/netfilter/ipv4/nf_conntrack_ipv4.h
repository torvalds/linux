/* SPDX-License-Identifier: GPL-2.0 */
/*
 * IPv4 support for nf_conntrack.
 *
 * 23 Mar 2004: Yasuyuki Kozakai @ USAGI <yasuyuki.kozakai@toshiba.co.jp>
 *	- move L3 protocol dependent part from include/linux/netfilter_ipv4/
 *	  ip_conntarck.h
 */

#ifndef _NF_CONNTRACK_IPV4_H
#define _NF_CONNTRACK_IPV4_H


const extern struct nf_conntrack_l3proto nf_conntrack_l3proto_ipv4;

extern const struct nf_conntrack_l4proto nf_conntrack_l4proto_tcp4;
extern const struct nf_conntrack_l4proto nf_conntrack_l4proto_udp4;
extern const struct nf_conntrack_l4proto nf_conntrack_l4proto_icmp;
#ifdef CONFIG_NF_CT_PROTO_DCCP
extern const struct nf_conntrack_l4proto nf_conntrack_l4proto_dccp4;
#endif
#ifdef CONFIG_NF_CT_PROTO_SCTP
extern const struct nf_conntrack_l4proto nf_conntrack_l4proto_sctp4;
#endif
#ifdef CONFIG_NF_CT_PROTO_UDPLITE
extern const struct nf_conntrack_l4proto nf_conntrack_l4proto_udplite4;
#endif

int nf_conntrack_ipv4_compat_init(void);
void nf_conntrack_ipv4_compat_fini(void);

#endif /*_NF_CONNTRACK_IPV4_H*/
