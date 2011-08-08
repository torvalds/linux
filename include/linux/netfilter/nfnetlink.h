#ifndef _NFNETLINK_H
#define _NFNETLINK_H
#include <linux/types.h>
#include <linux/netfilter/nfnetlink_compat.h>

enum nfnetlink_groups {
	NFNLGRP_NONE,
#define NFNLGRP_NONE			NFNLGRP_NONE
	NFNLGRP_CONNTRACK_NEW,
#define NFNLGRP_CONNTRACK_NEW		NFNLGRP_CONNTRACK_NEW
	NFNLGRP_CONNTRACK_UPDATE,
#define NFNLGRP_CONNTRACK_UPDATE	NFNLGRP_CONNTRACK_UPDATE
	NFNLGRP_CONNTRACK_DESTROY,
#define NFNLGRP_CONNTRACK_DESTROY	NFNLGRP_CONNTRACK_DESTROY
	NFNLGRP_CONNTRACK_EXP_NEW,
#define	NFNLGRP_CONNTRACK_EXP_NEW	NFNLGRP_CONNTRACK_EXP_NEW
	NFNLGRP_CONNTRACK_EXP_UPDATE,
#define NFNLGRP_CONNTRACK_EXP_UPDATE	NFNLGRP_CONNTRACK_EXP_UPDATE
	NFNLGRP_CONNTRACK_EXP_DESTROY,
#define NFNLGRP_CONNTRACK_EXP_DESTROY	NFNLGRP_CONNTRACK_EXP_DESTROY
	__NFNLGRP_MAX,
};
#define NFNLGRP_MAX	(__NFNLGRP_MAX - 1)

/* General form of address family dependent message.
 */
struct nfgenmsg {
	__u8  nfgen_family;		/* AF_xxx */
	__u8  version;		/* nfnetlink version */
	__be16    res_id;		/* resource id */
};

#define NFNETLINK_V0	0

/* netfilter netlink message types are split in two pieces:
 * 8 bit subsystem, 8bit operation.
 */

#define NFNL_SUBSYS_ID(x)	((x & 0xff00) >> 8)
#define NFNL_MSG_TYPE(x)	(x & 0x00ff)

/* No enum here, otherwise __stringify() trick of MODULE_ALIAS_NFNL_SUBSYS()
 * won't work anymore */
#define NFNL_SUBSYS_NONE 		0
#define NFNL_SUBSYS_CTNETLINK		1
#define NFNL_SUBSYS_CTNETLINK_EXP	2
#define NFNL_SUBSYS_QUEUE		3
#define NFNL_SUBSYS_ULOG		4
#define NFNL_SUBSYS_OSF			5
#define NFNL_SUBSYS_IPSET		6
#define NFNL_SUBSYS_COUNT		7

#ifdef __KERNEL__

#include <linux/netlink.h>
#include <linux/capability.h>
#include <net/netlink.h>

struct nfnl_callback {
	int (*call)(struct sock *nl, struct sk_buff *skb, 
		    const struct nlmsghdr *nlh,
		    const struct nlattr * const cda[]);
	int (*call_rcu)(struct sock *nl, struct sk_buff *skb, 
		    const struct nlmsghdr *nlh,
		    const struct nlattr * const cda[]);
	const struct nla_policy *policy;	/* netlink attribute policy */
	const u_int16_t attr_count;		/* number of nlattr's */
};

struct nfnetlink_subsystem {
	const char *name;
	__u8 subsys_id;			/* nfnetlink subsystem ID */
	__u8 cb_count;			/* number of callbacks */
	const struct nfnl_callback *cb;	/* callback for individual types */
};

extern int nfnetlink_subsys_register(const struct nfnetlink_subsystem *n);
extern int nfnetlink_subsys_unregister(const struct nfnetlink_subsystem *n);

extern int nfnetlink_has_listeners(struct net *net, unsigned int group);
extern int nfnetlink_send(struct sk_buff *skb, struct net *net, u32 pid, unsigned group,
			  int echo, gfp_t flags);
extern int nfnetlink_set_err(struct net *net, u32 pid, u32 group, int error);
extern int nfnetlink_unicast(struct sk_buff *skb, struct net *net, u_int32_t pid, int flags);

extern void nfnl_lock(void);
extern void nfnl_unlock(void);

#define MODULE_ALIAS_NFNL_SUBSYS(subsys) \
	MODULE_ALIAS("nfnetlink-subsys-" __stringify(subsys))

#endif	/* __KERNEL__ */
#endif	/* _NFNETLINK_H */
