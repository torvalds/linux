#ifndef _NF_CONNTRACK_TUPLE_COMMON_H
#define _NF_CONNTRACK_TUPLE_COMMON_H

#include <linux/types.h>
#ifndef __KERNEL__
#include <linux/netfilter.h>
#endif
#include <linux/netfilter/nf_conntrack_common.h> /* IP_CT_IS_REPLY */

enum ip_conntrack_dir {
	IP_CT_DIR_ORIGINAL,
	IP_CT_DIR_REPLY,
	IP_CT_DIR_MAX
};

/* The protocol-specific manipulable parts of the tuple: always in
 * network order
 */
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

#define CTINFO2DIR(ctinfo) ((ctinfo) >= IP_CT_IS_REPLY ? IP_CT_DIR_REPLY : IP_CT_DIR_ORIGINAL)

#endif /* _NF_CONNTRACK_TUPLE_COMMON_H */
