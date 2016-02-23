#ifndef _NF_CONNTRACK_ZONES_COMMON_H
#define _NF_CONNTRACK_ZONES_COMMON_H

#include <uapi/linux/netfilter/nf_conntrack_tuple_common.h>

#define NF_CT_DEFAULT_ZONE_ID	0

#define NF_CT_ZONE_DIR_ORIG	(1 << IP_CT_DIR_ORIGINAL)
#define NF_CT_ZONE_DIR_REPL	(1 << IP_CT_DIR_REPLY)

#define NF_CT_DEFAULT_ZONE_DIR	(NF_CT_ZONE_DIR_ORIG | NF_CT_ZONE_DIR_REPL)

#define NF_CT_FLAG_MARK		1

struct nf_conntrack_zone {
	u16	id;
	u8	flags;
	u8	dir;
};

extern const struct nf_conntrack_zone nf_ct_zone_dflt;

#endif /* _NF_CONNTRACK_ZONES_COMMON_H */
