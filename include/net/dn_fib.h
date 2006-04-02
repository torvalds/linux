#ifndef _NET_DN_FIB_H
#define _NET_DN_FIB_H

/* WARNING: The ordering of these elements must match ordering
 *          of RTA_* rtnetlink attribute numbers.
 */
struct dn_kern_rta
{
        void            *rta_dst;
        void            *rta_src;
        int             *rta_iif;
        int             *rta_oif;
        void            *rta_gw;
        u32             *rta_priority;
        void            *rta_prefsrc;
        struct rtattr   *rta_mx;
        struct rtattr   *rta_mp;
        unsigned char   *rta_protoinfo;
        u32             *rta_flow;
        struct rta_cacheinfo *rta_ci;
	struct rta_session *rta_sess;
};

struct dn_fib_res {
	struct dn_fib_rule *r;
	struct dn_fib_info *fi;
	unsigned char prefixlen;
	unsigned char nh_sel;
	unsigned char type;
	unsigned char scope;
};

struct dn_fib_nh {
	struct net_device	*nh_dev;
	unsigned		nh_flags;
	unsigned char		nh_scope;
	int			nh_weight;
	int			nh_power;
	int			nh_oif;
	__le16			nh_gw;
};

struct dn_fib_info {
	struct dn_fib_info	*fib_next;
	struct dn_fib_info	*fib_prev;
	int 			fib_treeref;
	atomic_t		fib_clntref;
	int			fib_dead;
	unsigned		fib_flags;
	int			fib_protocol;
	__le16			fib_prefsrc;
	__u32			fib_priority;
	__u32			fib_metrics[RTAX_MAX];
#define dn_fib_mtu  fib_metrics[RTAX_MTU-1]
#define dn_fib_window fib_metrics[RTAX_WINDOW-1]
#define dn_fib_rtt fib_metrics[RTAX_RTT-1]
#define dn_fib_advmss fib_metrics[RTAX_ADVMSS-1]
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
	int n;

	int (*insert)(struct dn_fib_table *t, struct rtmsg *r, 
			struct dn_kern_rta *rta, struct nlmsghdr *n, 
			struct netlink_skb_parms *req);
	int (*delete)(struct dn_fib_table *t, struct rtmsg *r,
			struct dn_kern_rta *rta, struct nlmsghdr *n,
			struct netlink_skb_parms *req);
	int (*lookup)(struct dn_fib_table *t, const struct flowi *fl,
			struct dn_fib_res *res);
	int (*flush)(struct dn_fib_table *t);
	int (*dump)(struct dn_fib_table *t, struct sk_buff *skb, struct netlink_callback *cb);

	unsigned char data[0];
};

#ifdef CONFIG_DECNET_ROUTER
/*
 * dn_fib.c
 */
extern void dn_fib_init(void);
extern void dn_fib_cleanup(void);

extern int dn_fib_ioctl(struct socket *sock, unsigned int cmd, 
			unsigned long arg);
extern struct dn_fib_info *dn_fib_create_info(const struct rtmsg *r, 
				struct dn_kern_rta *rta, 
				const struct nlmsghdr *nlh, int *errp);
extern int dn_fib_semantic_match(int type, struct dn_fib_info *fi, 
			const struct flowi *fl,
			struct dn_fib_res *res);
extern void dn_fib_release_info(struct dn_fib_info *fi);
extern __le16 dn_fib_get_attr16(struct rtattr *attr, int attrlen, int type);
extern void dn_fib_flush(void);
extern void dn_fib_select_multipath(const struct flowi *fl,
					struct dn_fib_res *res);
extern int dn_fib_sync_down(__le16 local, struct net_device *dev,
				int force);
extern int dn_fib_sync_up(struct net_device *dev);

/*
 * dn_tables.c
 */
extern struct dn_fib_table *dn_fib_get_table(int n, int creat);
extern struct dn_fib_table *dn_fib_empty_table(void);
extern void dn_fib_table_init(void);
extern void dn_fib_table_cleanup(void);

/*
 * dn_rules.c
 */
extern void dn_fib_rules_init(void);
extern void dn_fib_rules_cleanup(void);
extern void dn_fib_rule_put(struct dn_fib_rule *);
extern __le16 dn_fib_rules_policy(__le16 saddr, struct dn_fib_res *res, unsigned *flags);
extern unsigned dnet_addr_type(__le16 addr);
extern int dn_fib_lookup(const struct flowi *fl, struct dn_fib_res *res);

/*
 * rtnetlink interface
 */
extern int dn_fib_rtm_delroute(struct sk_buff *skb, struct nlmsghdr *nlh, void *arg);
extern int dn_fib_rtm_newroute(struct sk_buff *skb, struct nlmsghdr *nlh, void *arg);
extern int dn_fib_dump(struct sk_buff *skb, struct netlink_callback *cb);

extern int dn_fib_rtm_delrule(struct sk_buff *skb, struct nlmsghdr *nlh, void *arg);
extern int dn_fib_rtm_newrule(struct sk_buff *skb, struct nlmsghdr *nlh, void *arg);
extern int dn_fib_dump_rules(struct sk_buff *skb, struct netlink_callback *cb);

extern void dn_fib_free_info(struct dn_fib_info *fi);

static inline void dn_fib_info_put(struct dn_fib_info *fi)
{
	if (atomic_dec_and_test(&fi->fib_clntref))
		dn_fib_free_info(fi);
}

static inline void dn_fib_res_put(struct dn_fib_res *res)
{
	if (res->fi)
		dn_fib_info_put(res->fi);
	if (res->r)
		dn_fib_rule_put(res->r);
}

extern struct dn_fib_table *dn_fib_tables[];

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
                return dn_htons(~((1<<(16-n))-1));
        return 0;
}

#endif /* _NET_DN_FIB_H */
