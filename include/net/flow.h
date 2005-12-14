/*
 *
 *	Generic internet FLOW.
 *
 */

#ifndef _NET_FLOW_H
#define _NET_FLOW_H

#include <linux/in6.h>
#include <asm/atomic.h>

struct flowi {
	int	oif;
	int	iif;

	union {
		struct {
			__u32			daddr;
			__u32			saddr;
			__u32			fwmark;
			__u8			tos;
			__u8			scope;
		} ip4_u;
		
		struct {
			struct in6_addr		daddr;
			struct in6_addr		saddr;
			__u32			flowlabel;
		} ip6_u;

		struct {
			__u16			daddr;
			__u16			saddr;
			__u32			fwmark;
			__u8			scope;
		} dn_u;
	} nl_u;
#define fld_dst		nl_u.dn_u.daddr
#define fld_src		nl_u.dn_u.saddr
#define fld_fwmark	nl_u.dn_u.fwmark
#define fld_scope	nl_u.dn_u.scope
#define fl6_dst		nl_u.ip6_u.daddr
#define fl6_src		nl_u.ip6_u.saddr
#define fl6_flowlabel	nl_u.ip6_u.flowlabel
#define fl4_dst		nl_u.ip4_u.daddr
#define fl4_src		nl_u.ip4_u.saddr
#define fl4_fwmark	nl_u.ip4_u.fwmark
#define fl4_tos		nl_u.ip4_u.tos
#define fl4_scope	nl_u.ip4_u.scope

	__u8	proto;
	__u8	flags;
#define FLOWI_FLAG_MULTIPATHOLDROUTE 0x01
	union {
		struct {
			__u16	sport;
			__u16	dport;
		} ports;

		struct {
			__u8	type;
			__u8	code;
		} icmpt;

		struct {
			__u16	sport;
			__u16	dport;
			__u8	objnum;
			__u8	objnamel; /* Not 16 bits since max val is 16 */
			__u8	objname[16]; /* Not zero terminated */
		} dnports;

		__u32		spi;
	} uli_u;
#define fl_ip_sport	uli_u.ports.sport
#define fl_ip_dport	uli_u.ports.dport
#define fl_icmp_type	uli_u.icmpt.type
#define fl_icmp_code	uli_u.icmpt.code
#define fl_ipsec_spi	uli_u.spi
} __attribute__((__aligned__(BITS_PER_LONG/8)));

#define FLOW_DIR_IN	0
#define FLOW_DIR_OUT	1
#define FLOW_DIR_FWD	2

struct sock;
typedef void (*flow_resolve_t)(struct flowi *key, u32 sk_sid, u16 family, u8 dir,
			       void **objp, atomic_t **obj_refp);

extern void *flow_cache_lookup(struct flowi *key, u32 sk_sid, u16 family, u8 dir,
	 		       flow_resolve_t resolver);
extern void flow_cache_flush(void);
extern atomic_t flow_cache_genid;

#endif
