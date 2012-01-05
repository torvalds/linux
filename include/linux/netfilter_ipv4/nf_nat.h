#ifndef _LINUX_NF_NAT_H
#define _LINUX_NF_NAT_H

#include <linux/types.h>

#define IP_NAT_RANGE_MAP_IPS 1
#define IP_NAT_RANGE_PROTO_SPECIFIED 2
#define IP_NAT_RANGE_PROTO_RANDOM 4
#define IP_NAT_RANGE_PERSISTENT 8

/* The protocol-specific manipulable parts of the tuple. */
union nf_conntrack_man_proto {
	/* Add other protocols here. */
	__be16 all;

	struct {
		__be16 port;
	} tcp;
	struct {
		__be16 port;
	} udp;
	struct {
		__be16 id;
	} icmp;
	struct {
		__be16 port;
	} dccp;
	struct {
		__be16 port;
	} sctp;
	struct {
		__be16 key;	/* GRE key is 32bit, PPtP only uses 16bit */
	} gre;
};

/* Single range specification. */
struct nf_nat_range {
	/* Set to OR of flags above. */
	unsigned int flags;

	/* Inclusive: network order. */
	__be32 min_ip, max_ip;

	/* Inclusive: network order */
	union nf_conntrack_man_proto min, max;
};

/* For backwards compat: don't use in modern code. */
struct nf_nat_multi_range_compat {
	unsigned int rangesize; /* Must be 1. */

	/* hangs off end. */
	struct nf_nat_range range[1];
};

#define nf_nat_multi_range nf_nat_multi_range_compat

#endif
