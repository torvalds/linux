/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET  is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Definitions for the Forwarding Information Base.
 *
 * Authors:	A.N.Kuznetsov, <kuznet@ms2.inr.ac.ru>
 */

#ifndef _NET_IP_FIB_H
#define _NET_IP_FIB_H

#include <net/flow.h>
#include <linux/seq_file.h>
#include <linux/rcupdate.h>
#include <net/fib_notifier.h>
#include <net/fib_rules.h>
#include <net/inet_dscp.h>
#include <net/inetpeer.h>
#include <linux/percpu.h>
#include <linux/notifier.h>
#include <linux/refcount.h>
#include <linux/ip.h>
#include <linux/in_route.h>

struct fib_config {
	u8			fc_dst_len;
	dscp_t			fc_dscp;
	u8			fc_protocol;
	u8			fc_scope;
	u8			fc_type;
	u8			fc_gw_family;
	/* 2 bytes unused */
	u32			fc_table;
	__be32			fc_dst;
	union {
		__be32		fc_gw4;
		struct in6_addr	fc_gw6;
	};
	int			fc_oif;
	u32			fc_flags;
	u32			fc_priority;
	__be32			fc_prefsrc;
	u32			fc_nh_id;
	struct nlattr		*fc_mx;
	struct rtnexthop	*fc_mp;
	int			fc_mx_len;
	int			fc_mp_len;
	u32			fc_flow;
	u32			fc_nlflags;
	struct nl_info		fc_nlinfo;
	struct nlattr		*fc_encap;
	u16			fc_encap_type;
};

struct fib_info;
struct rtable;

struct fib_nh_exception {
	struct fib_nh_exception __rcu	*fnhe_next;
	int				fnhe_genid;
	__be32				fnhe_daddr;
	u32				fnhe_pmtu;
	bool				fnhe_mtu_locked;
	__be32				fnhe_gw;
	unsigned long			fnhe_expires;
	struct rtable __rcu		*fnhe_rth_input;
	struct rtable __rcu		*fnhe_rth_output;
	unsigned long			fnhe_stamp;
	struct rcu_head			rcu;
};

struct fnhe_hash_bucket {
	struct fib_nh_exception __rcu	*chain;
};

#define FNHE_HASH_SHIFT		11
#define FNHE_HASH_SIZE		(1 << FNHE_HASH_SHIFT)
#define FNHE_RECLAIM_DEPTH	5

struct fib_nh_common {
	struct net_device	*nhc_dev;
	netdevice_tracker	nhc_dev_tracker;
	int			nhc_oif;
	unsigned char		nhc_scope;
	u8			nhc_family;
	u8			nhc_gw_family;
	unsigned char		nhc_flags;
	struct lwtunnel_state	*nhc_lwtstate;

	union {
		__be32          ipv4;
		struct in6_addr ipv6;
	} nhc_gw;

	int			nhc_weight;
	atomic_t		nhc_upper_bound;

	/* v4 specific, but allows fib6_nh with v4 routes */
	struct rtable __rcu * __percpu *nhc_pcpu_rth_output;
	struct rtable __rcu     *nhc_rth_input;
	struct fnhe_hash_bucket	__rcu *nhc_exceptions;
};

struct fib_nh {
	struct fib_nh_common	nh_common;
	struct hlist_node	nh_hash;
	struct fib_info		*nh_parent;
#ifdef CONFIG_IP_ROUTE_CLASSID
	__u32			nh_tclassid;
#endif
	__be32			nh_saddr;
	int			nh_saddr_genid;
#define fib_nh_family		nh_common.nhc_family
#define fib_nh_dev		nh_common.nhc_dev
#define fib_nh_dev_tracker	nh_common.nhc_dev_tracker
#define fib_nh_oif		nh_common.nhc_oif
#define fib_nh_flags		nh_common.nhc_flags
#define fib_nh_lws		nh_common.nhc_lwtstate
#define fib_nh_scope		nh_common.nhc_scope
#define fib_nh_gw_family	nh_common.nhc_gw_family
#define fib_nh_gw4		nh_common.nhc_gw.ipv4
#define fib_nh_gw6		nh_common.nhc_gw.ipv6
#define fib_nh_weight		nh_common.nhc_weight
#define fib_nh_upper_bound	nh_common.nhc_upper_bound
};

/*
 * This structure contains data shared by many of routes.
 */

struct nexthop;

struct fib_info {
	struct hlist_node	fib_hash;
	struct hlist_node	fib_lhash;
	struct list_head	nh_list;
	struct net		*fib_net;
	refcount_t		fib_treeref;
	refcount_t		fib_clntref;
	unsigned int		fib_flags;
	unsigned char		fib_dead;
	unsigned char		fib_protocol;
	unsigned char		fib_scope;
	unsigned char		fib_type;
	__be32			fib_prefsrc;
	u32			fib_tb_id;
	u32			fib_priority;
	struct dst_metrics	*fib_metrics;
#define fib_mtu fib_metrics->metrics[RTAX_MTU-1]
#define fib_window fib_metrics->metrics[RTAX_WINDOW-1]
#define fib_rtt fib_metrics->metrics[RTAX_RTT-1]
#define fib_advmss fib_metrics->metrics[RTAX_ADVMSS-1]
	int			fib_nhs;
	bool			fib_nh_is_v6;
	bool			nh_updated;
	bool			pfsrc_removed;
	struct nexthop		*nh;
	struct rcu_head		rcu;
	struct fib_nh		fib_nh[] __counted_by(fib_nhs);
};


#ifdef CONFIG_IP_MULTIPLE_TABLES
struct fib_rule;
#endif

struct fib_table;
struct fib_result {
	__be32			prefix;
	unsigned char		prefixlen;
	unsigned char		nh_sel;
	unsigned char		type;
	unsigned char		scope;
	u32			tclassid;
	dscp_t			dscp;
	struct fib_nh_common	*nhc;
	struct fib_info		*fi;
	struct fib_table	*table;
	struct hlist_head	*fa_head;
};

struct fib_result_nl {
	__be32		fl_addr;   /* To be looked up*/
	u32		fl_mark;
	unsigned char	fl_tos;
	unsigned char   fl_scope;
	unsigned char   tb_id_in;

	unsigned char   tb_id;      /* Results */
	unsigned char	prefixlen;
	unsigned char	nh_sel;
	unsigned char	type;
	unsigned char	scope;
	int             err;
};

#ifdef CONFIG_IP_MULTIPLE_TABLES
#define FIB_TABLE_HASHSZ 256
#else
#define FIB_TABLE_HASHSZ 2
#endif

__be32 fib_info_update_nhc_saddr(struct net *net, struct fib_nh_common *nhc,
				 unsigned char scope);
__be32 fib_result_prefsrc(struct net *net, struct fib_result *res);

#define FIB_RES_NHC(res)		((res).nhc)
#define FIB_RES_DEV(res)	(FIB_RES_NHC(res)->nhc_dev)
#define FIB_RES_OIF(res)	(FIB_RES_NHC(res)->nhc_oif)

struct fib_rt_info {
	struct fib_info		*fi;
	u32			tb_id;
	__be32			dst;
	int			dst_len;
	dscp_t			dscp;
	u8			type;
	u8			offload:1,
				trap:1,
				offload_failed:1,
				unused:5;
};

struct fib_entry_notifier_info {
	struct fib_notifier_info info; /* must be first */
	u32 dst;
	int dst_len;
	struct fib_info *fi;
	dscp_t dscp;
	u8 type;
	u32 tb_id;
};

struct fib_nh_notifier_info {
	struct fib_notifier_info info; /* must be first */
	struct fib_nh *fib_nh;
};

int call_fib4_notifier(struct notifier_block *nb,
		       enum fib_event_type event_type,
		       struct fib_notifier_info *info);
int call_fib4_notifiers(struct net *net, enum fib_event_type event_type,
			struct fib_notifier_info *info);

int __net_init fib4_notifier_init(struct net *net);
void __net_exit fib4_notifier_exit(struct net *net);

void fib_info_notify_update(struct net *net, struct nl_info *info);
int fib_notify(struct net *net, struct notifier_block *nb,
	       struct netlink_ext_ack *extack);

struct fib_table {
	struct hlist_node	tb_hlist;
	u32			tb_id;
	int			tb_num_default;
	struct rcu_head		rcu;
	unsigned long 		*tb_data;
	unsigned long		__data[];
};

struct fib_dump_filter {
	u32			table_id;
	/* filter_set is an optimization that an entry is set */
	bool			filter_set;
	bool			dump_routes;
	bool			dump_exceptions;
	bool			rtnl_held;
	unsigned char		protocol;
	unsigned char		rt_type;
	unsigned int		flags;
	struct net_device	*dev;
};

int fib_table_lookup(struct fib_table *tb, const struct flowi4 *flp,
		     struct fib_result *res, int fib_flags);
int fib_table_insert(struct net *, struct fib_table *, struct fib_config *,
		     struct netlink_ext_ack *extack);
int fib_table_delete(struct net *, struct fib_table *, struct fib_config *,
		     struct netlink_ext_ack *extack);
int fib_table_dump(struct fib_table *table, struct sk_buff *skb,
		   struct netlink_callback *cb, struct fib_dump_filter *filter);
int fib_table_flush(struct net *net, struct fib_table *table, bool flush_all);
struct fib_table *fib_trie_unmerge(struct fib_table *main_tb);
void fib_table_flush_external(struct fib_table *table);
void fib_free_table(struct fib_table *tb);

#ifndef CONFIG_IP_MULTIPLE_TABLES

#define TABLE_LOCAL_INDEX	(RT_TABLE_LOCAL & (FIB_TABLE_HASHSZ - 1))
#define TABLE_MAIN_INDEX	(RT_TABLE_MAIN  & (FIB_TABLE_HASHSZ - 1))

static inline struct fib_table *fib_get_table(struct net *net, u32 id)
{
	struct hlist_node *tb_hlist;
	struct hlist_head *ptr;

	ptr = id == RT_TABLE_LOCAL ?
		&net->ipv4.fib_table_hash[TABLE_LOCAL_INDEX] :
		&net->ipv4.fib_table_hash[TABLE_MAIN_INDEX];

	tb_hlist = rcu_dereference_rtnl(hlist_first_rcu(ptr));

	return hlist_entry(tb_hlist, struct fib_table, tb_hlist);
}

static inline struct fib_table *fib_new_table(struct net *net, u32 id)
{
	return fib_get_table(net, id);
}

static inline int fib_lookup(struct net *net, const struct flowi4 *flp,
			     struct fib_result *res, unsigned int flags)
{
	struct fib_table *tb;
	int err = -ENETUNREACH;

	rcu_read_lock();

	tb = fib_get_table(net, RT_TABLE_MAIN);
	if (tb)
		err = fib_table_lookup(tb, flp, res, flags | FIB_LOOKUP_NOREF);

	if (err == -EAGAIN)
		err = -ENETUNREACH;

	rcu_read_unlock();

	return err;
}

static inline bool fib4_has_custom_rules(const struct net *net)
{
	return false;
}

static inline bool fib4_rule_default(const struct fib_rule *rule)
{
	return true;
}

static inline int fib4_rules_dump(struct net *net, struct notifier_block *nb,
				  struct netlink_ext_ack *extack)
{
	return 0;
}

static inline unsigned int fib4_rules_seq_read(struct net *net)
{
	return 0;
}

static inline bool fib4_rules_early_flow_dissect(struct net *net,
						 struct sk_buff *skb,
						 struct flowi4 *fl4,
						 struct flow_keys *flkeys)
{
	return false;
}
#else /* CONFIG_IP_MULTIPLE_TABLES */
int __net_init fib4_rules_init(struct net *net);
void __net_exit fib4_rules_exit(struct net *net);

struct fib_table *fib_new_table(struct net *net, u32 id);
struct fib_table *fib_get_table(struct net *net, u32 id);

int __fib_lookup(struct net *net, struct flowi4 *flp,
		 struct fib_result *res, unsigned int flags);

static inline int fib_lookup(struct net *net, struct flowi4 *flp,
			     struct fib_result *res, unsigned int flags)
{
	struct fib_table *tb;
	int err = -ENETUNREACH;

	flags |= FIB_LOOKUP_NOREF;
	if (net->ipv4.fib_has_custom_rules)
		return __fib_lookup(net, flp, res, flags);

	rcu_read_lock();

	res->tclassid = 0;

	tb = rcu_dereference_rtnl(net->ipv4.fib_main);
	if (tb)
		err = fib_table_lookup(tb, flp, res, flags);

	if (!err)
		goto out;

	tb = rcu_dereference_rtnl(net->ipv4.fib_default);
	if (tb)
		err = fib_table_lookup(tb, flp, res, flags);

out:
	if (err == -EAGAIN)
		err = -ENETUNREACH;

	rcu_read_unlock();

	return err;
}

static inline bool fib4_has_custom_rules(const struct net *net)
{
	return net->ipv4.fib_has_custom_rules;
}

bool fib4_rule_default(const struct fib_rule *rule);
int fib4_rules_dump(struct net *net, struct notifier_block *nb,
		    struct netlink_ext_ack *extack);
unsigned int fib4_rules_seq_read(struct net *net);

static inline bool fib4_rules_early_flow_dissect(struct net *net,
						 struct sk_buff *skb,
						 struct flowi4 *fl4,
						 struct flow_keys *flkeys)
{
	unsigned int flag = FLOW_DISSECTOR_F_STOP_AT_ENCAP;

	if (!net->ipv4.fib_rules_require_fldissect)
		return false;

	memset(flkeys, 0, sizeof(*flkeys));
	__skb_flow_dissect(net, skb, &flow_keys_dissector,
			   flkeys, NULL, 0, 0, 0, flag);

	fl4->fl4_sport = flkeys->ports.src;
	fl4->fl4_dport = flkeys->ports.dst;
	fl4->flowi4_proto = flkeys->basic.ip_proto;

	return true;
}

#endif /* CONFIG_IP_MULTIPLE_TABLES */

static inline bool fib_dscp_masked_match(dscp_t dscp, const struct flowi4 *fl4)
{
	return dscp == inet_dsfield_to_dscp(RT_TOS(fl4->flowi4_tos));
}

/* Exported by fib_frontend.c */
extern const struct nla_policy rtm_ipv4_policy[];
void ip_fib_init(void);
int fib_gw_from_via(struct fib_config *cfg, struct nlattr *nla,
		    struct netlink_ext_ack *extack);
__be32 fib_compute_spec_dst(struct sk_buff *skb);
bool fib_info_nh_uses_dev(struct fib_info *fi, const struct net_device *dev);
int fib_validate_source(struct sk_buff *skb, __be32 src, __be32 dst,
			u8 tos, int oif, struct net_device *dev,
			struct in_device *idev, u32 *itag);
#ifdef CONFIG_IP_ROUTE_CLASSID
static inline int fib_num_tclassid_users(struct net *net)
{
	return atomic_read(&net->ipv4.fib_num_tclassid_users);
}
#else
static inline int fib_num_tclassid_users(struct net *net)
{
	return 0;
}
#endif
int fib_unmerge(struct net *net);

static inline bool nhc_l3mdev_matches_dev(const struct fib_nh_common *nhc,
const struct net_device *dev)
{
	if (nhc->nhc_dev == dev ||
	    l3mdev_master_ifindex_rcu(nhc->nhc_dev) == dev->ifindex)
		return true;

	return false;
}

/* Exported by fib_semantics.c */
int ip_fib_check_default(__be32 gw, struct net_device *dev);
int fib_sync_down_dev(struct net_device *dev, unsigned long event, bool force);
int fib_sync_down_addr(struct net_device *dev, __be32 local);
int fib_sync_up(struct net_device *dev, unsigned char nh_flags);
void fib_sync_mtu(struct net_device *dev, u32 orig_mtu);
void fib_nhc_update_mtu(struct fib_nh_common *nhc, u32 new, u32 orig);

/* Fields used for sysctl_fib_multipath_hash_fields.
 * Common to IPv4 and IPv6.
 *
 * Add new fields at the end. This is user API.
 */
#define FIB_MULTIPATH_HASH_FIELD_SRC_IP			BIT(0)
#define FIB_MULTIPATH_HASH_FIELD_DST_IP			BIT(1)
#define FIB_MULTIPATH_HASH_FIELD_IP_PROTO		BIT(2)
#define FIB_MULTIPATH_HASH_FIELD_FLOWLABEL		BIT(3)
#define FIB_MULTIPATH_HASH_FIELD_SRC_PORT		BIT(4)
#define FIB_MULTIPATH_HASH_FIELD_DST_PORT		BIT(5)
#define FIB_MULTIPATH_HASH_FIELD_INNER_SRC_IP		BIT(6)
#define FIB_MULTIPATH_HASH_FIELD_INNER_DST_IP		BIT(7)
#define FIB_MULTIPATH_HASH_FIELD_INNER_IP_PROTO		BIT(8)
#define FIB_MULTIPATH_HASH_FIELD_INNER_FLOWLABEL	BIT(9)
#define FIB_MULTIPATH_HASH_FIELD_INNER_SRC_PORT		BIT(10)
#define FIB_MULTIPATH_HASH_FIELD_INNER_DST_PORT		BIT(11)

#define FIB_MULTIPATH_HASH_FIELD_OUTER_MASK		\
	(FIB_MULTIPATH_HASH_FIELD_SRC_IP |		\
	 FIB_MULTIPATH_HASH_FIELD_DST_IP |		\
	 FIB_MULTIPATH_HASH_FIELD_IP_PROTO |		\
	 FIB_MULTIPATH_HASH_FIELD_FLOWLABEL |		\
	 FIB_MULTIPATH_HASH_FIELD_SRC_PORT |		\
	 FIB_MULTIPATH_HASH_FIELD_DST_PORT)

#define FIB_MULTIPATH_HASH_FIELD_INNER_MASK		\
	(FIB_MULTIPATH_HASH_FIELD_INNER_SRC_IP |	\
	 FIB_MULTIPATH_HASH_FIELD_INNER_DST_IP |	\
	 FIB_MULTIPATH_HASH_FIELD_INNER_IP_PROTO |	\
	 FIB_MULTIPATH_HASH_FIELD_INNER_FLOWLABEL |	\
	 FIB_MULTIPATH_HASH_FIELD_INNER_SRC_PORT |	\
	 FIB_MULTIPATH_HASH_FIELD_INNER_DST_PORT)

#define FIB_MULTIPATH_HASH_FIELD_ALL_MASK		\
	(FIB_MULTIPATH_HASH_FIELD_OUTER_MASK |		\
	 FIB_MULTIPATH_HASH_FIELD_INNER_MASK)

#define FIB_MULTIPATH_HASH_FIELD_DEFAULT_MASK		\
	(FIB_MULTIPATH_HASH_FIELD_SRC_IP |		\
	 FIB_MULTIPATH_HASH_FIELD_DST_IP |		\
	 FIB_MULTIPATH_HASH_FIELD_IP_PROTO)

#ifdef CONFIG_IP_ROUTE_MULTIPATH
int fib_multipath_hash(const struct net *net, const struct flowi4 *fl4,
		       const struct sk_buff *skb, struct flow_keys *flkeys);

static void
fib_multipath_hash_construct_key(siphash_key_t *key, u32 mp_seed)
{
	u64 mp_seed_64 = mp_seed;

	key->key[0] = (mp_seed_64 << 32) | mp_seed_64;
	key->key[1] = key->key[0];
}

static inline u32 fib_multipath_hash_from_keys(const struct net *net,
					       struct flow_keys *keys)
{
	siphash_aligned_key_t hash_key;
	u32 mp_seed;

	mp_seed = READ_ONCE(net->ipv4.sysctl_fib_multipath_hash_seed).mp_seed;
	fib_multipath_hash_construct_key(&hash_key, mp_seed);

	return flow_hash_from_keys_seed(keys, &hash_key);
}
#else
static inline u32 fib_multipath_hash_from_keys(const struct net *net,
					       struct flow_keys *keys)
{
	return flow_hash_from_keys(keys);
}
#endif

int fib_check_nh(struct net *net, struct fib_nh *nh, u32 table, u8 scope,
		 struct netlink_ext_ack *extack);
void fib_select_multipath(struct fib_result *res, int hash);
void fib_select_path(struct net *net, struct fib_result *res,
		     struct flowi4 *fl4, const struct sk_buff *skb);

int fib_nh_init(struct net *net, struct fib_nh *fib_nh,
		struct fib_config *cfg, int nh_weight,
		struct netlink_ext_ack *extack);
void fib_nh_release(struct net *net, struct fib_nh *fib_nh);
int fib_nh_common_init(struct net *net, struct fib_nh_common *nhc,
		       struct nlattr *fc_encap, u16 fc_encap_type,
		       void *cfg, gfp_t gfp_flags,
		       struct netlink_ext_ack *extack);
void fib_nh_common_release(struct fib_nh_common *nhc);

/* Exported by fib_trie.c */
void fib_alias_hw_flags_set(struct net *net, const struct fib_rt_info *fri);
void fib_trie_init(void);
struct fib_table *fib_trie_table(u32 id, struct fib_table *alias);
bool fib_lookup_good_nhc(const struct fib_nh_common *nhc, int fib_flags,
			 const struct flowi4 *flp);

static inline void fib_combine_itag(u32 *itag, const struct fib_result *res)
{
#ifdef CONFIG_IP_ROUTE_CLASSID
	struct fib_nh_common *nhc = res->nhc;
#ifdef CONFIG_IP_MULTIPLE_TABLES
	u32 rtag;
#endif
	if (nhc->nhc_family == AF_INET) {
		struct fib_nh *nh;

		nh = container_of(nhc, struct fib_nh, nh_common);
		*itag = nh->nh_tclassid << 16;
	} else {
		*itag = 0;
	}

#ifdef CONFIG_IP_MULTIPLE_TABLES
	rtag = res->tclassid;
	if (*itag == 0)
		*itag = (rtag<<16);
	*itag |= (rtag>>16);
#endif
#endif
}

void fib_flush(struct net *net);
void free_fib_info(struct fib_info *fi);

static inline void fib_info_hold(struct fib_info *fi)
{
	refcount_inc(&fi->fib_clntref);
}

static inline void fib_info_put(struct fib_info *fi)
{
	if (refcount_dec_and_test(&fi->fib_clntref))
		free_fib_info(fi);
}

#ifdef CONFIG_PROC_FS
int __net_init fib_proc_init(struct net *net);
void __net_exit fib_proc_exit(struct net *net);
#else
static inline int fib_proc_init(struct net *net)
{
	return 0;
}
static inline void fib_proc_exit(struct net *net)
{
}
#endif

u32 ip_mtu_from_fib_result(struct fib_result *res, __be32 daddr);

int ip_valid_fib_dump_req(struct net *net, const struct nlmsghdr *nlh,
			  struct fib_dump_filter *filter,
			  struct netlink_callback *cb);

int fib_nexthop_info(struct sk_buff *skb, const struct fib_nh_common *nh,
		     u8 rt_family, unsigned char *flags, bool skip_oif);
int fib_add_nexthop(struct sk_buff *skb, const struct fib_nh_common *nh,
		    int nh_weight, u8 rt_family, u32 nh_tclassid);
#endif  /* _NET_FIB_H */
