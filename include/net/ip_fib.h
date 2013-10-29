/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET  is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Definitions for the Forwarding Information Base.
 *
 * Authors:	A.N.Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

#ifndef _NET_IP_FIB_H
#define _NET_IP_FIB_H

#include <net/flow.h>
#include <linux/seq_file.h>
#include <net/fib_rules.h>

struct fib_config {
	u8			fc_dst_len;
	u8			fc_tos;
	u8			fc_protocol;
	u8			fc_scope;
	u8			fc_type;
	/* 3 bytes unused */
	u32			fc_table;
	__be32			fc_dst;
	__be32			fc_gw;
	int			fc_oif;
	u32			fc_flags;
	u32			fc_priority;
	__be32			fc_prefsrc;
	struct nlattr		*fc_mx;
	struct rtnexthop	*fc_mp;
	int			fc_mx_len;
	int			fc_mp_len;
	u32			fc_flow;
	u32			fc_nlflags;
	struct nl_info		fc_nlinfo;
 };

struct fib_info;

struct fib_nh {
	struct net_device	*nh_dev;
	struct hlist_node	nh_hash;
	struct fib_info		*nh_parent;
	unsigned		nh_flags;
	unsigned char		nh_scope;
#ifdef CONFIG_IP_ROUTE_MULTIPATH
	int			nh_weight;
	int			nh_power;
#endif
#ifdef CONFIG_IP_ROUTE_CLASSID
	__u32			nh_tclassid;
#endif
	int			nh_oif;
	__be32			nh_gw;
	__be32			nh_saddr;
	int			nh_saddr_genid;
};

/*
 * This structure contains data shared by many of routes.
 */

struct fib_info {
	struct hlist_node	fib_hash;
	struct hlist_node	fib_lhash;
	struct net		*fib_net;
	int			fib_treeref;
	atomic_t		fib_clntref;
	unsigned		fib_flags;
	unsigned char		fib_dead;
	unsigned char		fib_protocol;
	unsigned char		fib_scope;
	__be32			fib_prefsrc;
	u32			fib_priority;
	u32			*fib_metrics;
#define fib_mtu fib_metrics[RTAX_MTU-1]
#define fib_window fib_metrics[RTAX_WINDOW-1]
#define fib_rtt fib_metrics[RTAX_RTT-1]
#define fib_advmss fib_metrics[RTAX_ADVMSS-1]
	int			fib_nhs;
#ifdef CONFIG_IP_ROUTE_MULTIPATH
	int			fib_power;
#endif
	struct rcu_head		rcu;
	struct fib_nh		fib_nh[0];
#define fib_dev		fib_nh[0].nh_dev
};


#ifdef CONFIG_IP_MULTIPLE_TABLES
struct fib_rule;
#endif

struct fib_table;
struct fib_result {
	unsigned char	prefixlen;
	unsigned char	nh_sel;
	unsigned char	type;
	unsigned char	scope;
	struct fib_info *fi;
	struct fib_table *table;
	struct list_head *fa_head;
#ifdef CONFIG_IP_MULTIPLE_TABLES
	struct fib_rule	*r;
#endif
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

#ifdef CONFIG_IP_ROUTE_MULTIPATH
#define FIB_RES_NH(res)		((res).fi->fib_nh[(res).nh_sel])
#else /* CONFIG_IP_ROUTE_MULTIPATH */
#define FIB_RES_NH(res)		((res).fi->fib_nh[0])
#endif /* CONFIG_IP_ROUTE_MULTIPATH */

#ifdef CONFIG_IP_MULTIPLE_TABLES
#define FIB_TABLE_HASHSZ 256
#else
#define FIB_TABLE_HASHSZ 2
#endif

extern __be32 fib_info_update_nh_saddr(struct net *net, struct fib_nh *nh);

#define FIB_RES_SADDR(net, res)				\
	((FIB_RES_NH(res).nh_saddr_genid ==		\
	  atomic_read(&(net)->ipv4.dev_addr_genid)) ?	\
	 FIB_RES_NH(res).nh_saddr :			\
	 fib_info_update_nh_saddr((net), &FIB_RES_NH(res)))
#define FIB_RES_GW(res)			(FIB_RES_NH(res).nh_gw)
#define FIB_RES_DEV(res)		(FIB_RES_NH(res).nh_dev)
#define FIB_RES_OIF(res)		(FIB_RES_NH(res).nh_oif)

#define FIB_RES_PREFSRC(net, res)	((res).fi->fib_prefsrc ? : \
					 FIB_RES_SADDR(net, res))

struct fib_table {
	struct hlist_node tb_hlist;
	u32		tb_id;
	int		tb_default;
	int		tb_num_default;
	unsigned long	tb_data[0];
};

extern int fib_table_lookup(struct fib_table *tb, const struct flowi4 *flp,
			    struct fib_result *res, int fib_flags);
extern int fib_table_insert(struct fib_table *, struct fib_config *);
extern int fib_table_delete(struct fib_table *, struct fib_config *);
extern int fib_table_dump(struct fib_table *table, struct sk_buff *skb,
			  struct netlink_callback *cb);
extern int fib_table_flush(struct fib_table *table);
extern void fib_free_table(struct fib_table *tb);



#ifndef CONFIG_IP_MULTIPLE_TABLES

#define TABLE_LOCAL_INDEX	0
#define TABLE_MAIN_INDEX	1

static inline struct fib_table *fib_get_table(struct net *net, u32 id)
{
	struct hlist_head *ptr;

	ptr = id == RT_TABLE_LOCAL ?
		&net->ipv4.fib_table_hash[TABLE_LOCAL_INDEX] :
		&net->ipv4.fib_table_hash[TABLE_MAIN_INDEX];
	return hlist_entry(ptr->first, struct fib_table, tb_hlist);
}

static inline struct fib_table *fib_new_table(struct net *net, u32 id)
{
	return fib_get_table(net, id);
}

static inline int fib_lookup(struct net *net, const struct flowi4 *flp,
			     struct fib_result *res)
{
	struct fib_table *table;

	table = fib_get_table(net, RT_TABLE_LOCAL);
	if (!fib_table_lookup(table, flp, res, FIB_LOOKUP_NOREF))
		return 0;

	table = fib_get_table(net, RT_TABLE_MAIN);
	if (!fib_table_lookup(table, flp, res, FIB_LOOKUP_NOREF))
		return 0;
	return -ENETUNREACH;
}

#else /* CONFIG_IP_MULTIPLE_TABLES */
extern int __net_init fib4_rules_init(struct net *net);
extern void __net_exit fib4_rules_exit(struct net *net);

#ifdef CONFIG_IP_ROUTE_CLASSID
extern u32 fib_rules_tclass(const struct fib_result *res);
#endif

extern int fib_lookup(struct net *n, struct flowi4 *flp, struct fib_result *res);

extern struct fib_table *fib_new_table(struct net *net, u32 id);
extern struct fib_table *fib_get_table(struct net *net, u32 id);

#endif /* CONFIG_IP_MULTIPLE_TABLES */

/* Exported by fib_frontend.c */
extern const struct nla_policy rtm_ipv4_policy[];
extern void		ip_fib_init(void);
extern int fib_validate_source(struct sk_buff *skb, __be32 src, __be32 dst,
			       u8 tos, int oif, struct net_device *dev,
			       __be32 *spec_dst, u32 *itag);
extern void fib_select_default(struct fib_result *res);

/* Exported by fib_semantics.c */
extern int ip_fib_check_default(__be32 gw, struct net_device *dev);
extern int fib_sync_down_dev(struct net_device *dev, int force);
extern int fib_sync_down_addr(struct net *net, __be32 local);
extern void fib_update_nh_saddrs(struct net_device *dev);
extern int fib_sync_up(struct net_device *dev);
extern void fib_select_multipath(struct fib_result *res);

/* Exported by fib_trie.c */
extern void fib_trie_init(void);
extern struct fib_table *fib_trie_table(u32 id);

static inline void fib_combine_itag(u32 *itag, const struct fib_result *res)
{
#ifdef CONFIG_IP_ROUTE_CLASSID
#ifdef CONFIG_IP_MULTIPLE_TABLES
	u32 rtag;
#endif
	*itag = FIB_RES_NH(*res).nh_tclassid<<16;
#ifdef CONFIG_IP_MULTIPLE_TABLES
	rtag = fib_rules_tclass(res);
	if (*itag == 0)
		*itag = (rtag<<16);
	*itag |= (rtag>>16);
#endif
#endif
}

extern void free_fib_info(struct fib_info *fi);

static inline void fib_info_put(struct fib_info *fi)
{
	if (atomic_dec_and_test(&fi->fib_clntref))
		free_fib_info(fi);
}

#ifdef CONFIG_PROC_FS
extern int __net_init  fib_proc_init(struct net *net);
extern void __net_exit fib_proc_exit(struct net *net);
#else
static inline int fib_proc_init(struct net *net)
{
	return 0;
}
static inline void fib_proc_exit(struct net *net)
{
}
#endif

#endif  /* _NET_FIB_H */
