/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NET_DN_FIB_H
#define _NET_DN_FIB_H

#include <linux/netlink.h>
#include <linux/refcount.h>
#include <linux/rtnetlink.h>
#include <net/fib_rules.h>

extern const struct nla_policy rtm_dn_policy[];

struct dn_fib_res {
	struct fib_rule *r;
	struct dn_fib_info *fi;
	unsigned char prefixlen;
	unsigned char nh_sel;
	unsigned char type;
	unsigned char scope;
};

struct dn_fib_nh {
	struct net_device	*nh_dev;
	unsigned int		nh_flags;
	unsigned char		nh_scope;
	int			nh_weight;
	int			nh_power;
	int			nh_oif;
	__le16			nh_gw;
};

struct dn_fib_info {
	struct dn_fib_info	*fib_next;
	struct dn_fib_info	*fib_prev;
	refcount_t		fib_treeref;
	refcount_t		fib_clntref;
	int			fib_dead;
	unsigned int		fib_flags;
	int			fib_protocol;
	__le16			fib_prefsrc;
	__u32			fib_priority;
	__u32			fib_metrics[RTAX_MAX];
	int			fib_nhs;
	int			fib_power;
	struct dn_fib_nh	fib_nh[0];
#define dn_fib_dev		fib_nh[0].nh_dev
};


#define DN_FIB_RES_RESET(res)	((res).nh_sel = 0)
#define DN_FIB_RES_NH(res)	((res).fi->fib_nh[(res).nh_sel])

#define DN_FIB_RES_PREFSRC(res)	((res).fi->fib_prefsrc ? : __dn_fib_res_prefsrc(&res))
#define DN_FIB_RES_GW(res)	(DN_FIB_RES_NH(res).nh_gw)
#define DN_FIB_RES_DEV(res)	(DN_FIB_RES_NH(res).nh_dev)
#define DN_FIB_RES_OIF(res)	(DN_FIB_RES_NH(res).nh_oif)

typedef struct {
	__le16	datum;
} dn_fib_key_t;

typedef struct {
	__le16	datum;
} dn_fib_hash_t;

typedef struct {
	__u16	datum;
} dn_fib_idx_t;

struct dn_fib_node {
	struct dn_fib_node *fn_next;
	struct dn_fib_info *fn_info;
#define DN_FIB_INFO(f) ((f)->fn_info)
	dn_fib_key_t	fn_key;
	u8		fn_type;
	u8		fn_scope;
	u8		fn_state;
};


struct dn_fib_table {
	struct hlist_node hlist;
	u32 n;

	int (*insert)(struct dn_fib_table *t, struct rtmsg *r, 
			struct nlattr *attrs[], struct nlmsghdr *n,
			struct netlink_skb_parms *req);
	int (*delete)(struct dn_fib_table *t, struct rtmsg *r,
			struct nlattr *attrs[], struct nlmsghdr *n,
			struct netlink_skb_parms *req);
	int (*lookup)(struct dn_fib_table *t, const struct flowidn *fld,
			struct dn_fib_res *res);
	int (*flush)(struct dn_fib_table *t);
	int (*dump)(struct dn_fib_table *t, struct sk_buff *skb, struct netlink_callback *cb);

	unsigned char data[];
};

#ifdef CONFIG_DECNET_ROUTER
/*
 * dn_fib.c
 */
void dn_fib_init(void);
void dn_fib_cleanup(void);

int dn_fib_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg);
struct dn_fib_info *dn_fib_create_info(const struct rtmsg *r,
				       struct nlattr *attrs[],
				       const struct nlmsghdr *nlh, int *errp);
int dn_fib_semantic_match(int type, struct dn_fib_info *fi,
			  const struct flowidn *fld, struct dn_fib_res *res);
void dn_fib_release_info(struct dn_fib_info *fi);
void dn_fib_flush(void);
void dn_fib_select_multipath(const struct flowidn *fld, struct dn_fib_res *res);

/*
 * dn_tables.c
 */
struct dn_fib_table *dn_fib_get_table(u32 n, int creat);
struct dn_fib_table *dn_fib_empty_table(void);
void dn_fib_table_init(void);
void dn_fib_table_cleanup(void);

/*
 * dn_rules.c
 */
void dn_fib_rules_init(void);
void dn_fib_rules_cleanup(void);
unsigned int dnet_addr_type(__le16 addr);
int dn_fib_lookup(struct flowidn *fld, struct dn_fib_res *res);

int dn_fib_dump(struct sk_buff *skb, struct netlink_callback *cb);

void dn_fib_free_info(struct dn_fib_info *fi);

static inline void dn_fib_info_put(struct dn_fib_info *fi)
{
	if (refcount_dec_and_test(&fi->fib_clntref))
		dn_fib_free_info(fi);
}

static inline void dn_fib_res_put(struct dn_fib_res *res)
{
	if (res->fi)
		dn_fib_info_put(res->fi);
	if (res->r)
		fib_rule_put(res->r);
}

#else /* Endnode */

#define dn_fib_init()  do { } while(0)
#define dn_fib_cleanup() do { } while(0)

#define dn_fib_lookup(fl, res) (-ESRCH)
#define dn_fib_info_put(fi) do { } while(0)
#define dn_fib_select_multipath(fl, res) do { } while(0)
#define dn_fib_rules_policy(saddr,res,flags) (0)
#define dn_fib_res_put(res) do { } while(0)

#endif /* CONFIG_DECNET_ROUTER */

static inline __le16 dnet_make_mask(int n)
{
	if (n)
		return cpu_to_le16(~((1 << (16 - n)) - 1));
	return cpu_to_le16(0);
}

#endif /* _NET_DN_FIB_H */
