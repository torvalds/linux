/*
 * DECnet       An implementation of the DECnet protocol suite for the LINUX
 *              operating system.  DECnet is implemented using the  BSD Socket
 *              interface as the means of communication with the user level.
 *
 *              DECnet Routing Forwarding Information Base (Routing Tables)
 *
 * Author:      Steve Whitehouse <SteveW@ACM.org>
 *              Mostly copied from the IPv4 routing code
 *
 *
 * Changes:
 *
 */
#include <linux/string.h>
#include <linux/net.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/init.h>
#include <linux/skbuff.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/proc_fs.h>
#include <linux/netdevice.h>
#include <linux/timer.h>
#include <linux/spinlock.h>
#include <asm/atomic.h>
#include <asm/uaccess.h>
#include <linux/route.h> /* RTF_xxx */
#include <net/neighbour.h>
#include <net/netlink.h>
#include <net/dst.h>
#include <net/flow.h>
#include <net/fib_rules.h>
#include <net/dn.h>
#include <net/dn_route.h>
#include <net/dn_fib.h>
#include <net/dn_neigh.h>
#include <net/dn_dev.h>

struct dn_zone
{
	struct dn_zone		*dz_next;
	struct dn_fib_node 	**dz_hash;
	int			dz_nent;
	int			dz_divisor;
	u32			dz_hashmask;
#define DZ_HASHMASK(dz)	((dz)->dz_hashmask)
	int			dz_order;
	__le16			dz_mask;
#define DZ_MASK(dz)	((dz)->dz_mask)
};

struct dn_hash
{
	struct dn_zone	*dh_zones[17];
	struct dn_zone	*dh_zone_list;
};

#define dz_key_0(key)		((key).datum = 0)
#define dz_prefix(key,dz)	((key).datum)

#define for_nexthops(fi) { int nhsel; const struct dn_fib_nh *nh;\
	for(nhsel = 0, nh = (fi)->fib_nh; nhsel < (fi)->fib_nhs; nh++, nhsel++)

#define endfor_nexthops(fi) }

#define DN_MAX_DIVISOR 1024
#define DN_S_ZOMBIE 1
#define DN_S_ACCESSED 2

#define DN_FIB_SCAN(f, fp) \
for( ; ((f) = *(fp)) != NULL; (fp) = &(f)->fn_next)

#define DN_FIB_SCAN_KEY(f, fp, key) \
for( ; ((f) = *(fp)) != NULL && dn_key_eq((f)->fn_key, (key)); (fp) = &(f)->fn_next)

#define RT_TABLE_MIN 1
#define DN_FIB_TABLE_HASHSZ 256
static struct hlist_head dn_fib_table_hash[DN_FIB_TABLE_HASHSZ];
static DEFINE_RWLOCK(dn_fib_tables_lock);

static struct kmem_cache *dn_hash_kmem __read_mostly;
static int dn_fib_hash_zombies;

static inline dn_fib_idx_t dn_hash(dn_fib_key_t key, struct dn_zone *dz)
{
	u16 h = le16_to_cpu(key.datum)>>(16 - dz->dz_order);
	h ^= (h >> 10);
	h ^= (h >> 6);
	h &= DZ_HASHMASK(dz);
	return *(dn_fib_idx_t *)&h;
}

static inline dn_fib_key_t dz_key(__le16 dst, struct dn_zone *dz)
{
	dn_fib_key_t k;
	k.datum = dst & DZ_MASK(dz);
	return k;
}

static inline struct dn_fib_node **dn_chain_p(dn_fib_key_t key, struct dn_zone *dz)
{
	return &dz->dz_hash[dn_hash(key, dz).datum];
}

static inline struct dn_fib_node *dz_chain(dn_fib_key_t key, struct dn_zone *dz)
{
	return dz->dz_hash[dn_hash(key, dz).datum];
}

static inline int dn_key_eq(dn_fib_key_t a, dn_fib_key_t b)
{
	return a.datum == b.datum;
}

static inline int dn_key_leq(dn_fib_key_t a, dn_fib_key_t b)
{
	return a.datum <= b.datum;
}

static inline void dn_rebuild_zone(struct dn_zone *dz,
				   struct dn_fib_node **old_ht,
				   int old_divisor)
{
	int i;
	struct dn_fib_node *f, **fp, *next;

	for(i = 0; i < old_divisor; i++) {
		for(f = old_ht[i]; f; f = f->fn_next) {
			next = f->fn_next;
			for(fp = dn_chain_p(f->fn_key, dz);
				*fp && dn_key_leq((*fp)->fn_key, f->fn_key);
				fp = &(*fp)->fn_next)
				/* NOTHING */;
			f->fn_next = *fp;
			*fp = f;
		}
	}
}

static void dn_rehash_zone(struct dn_zone *dz)
{
	struct dn_fib_node **ht, **old_ht;
	int old_divisor, new_divisor;
	u32 new_hashmask;

	old_divisor = dz->dz_divisor;

	switch(old_divisor) {
		case 16:
			new_divisor = 256;
			new_hashmask = 0xFF;
			break;
		default:
			printk(KERN_DEBUG "DECnet: dn_rehash_zone: BUG! %d\n", old_divisor);
		case 256:
			new_divisor = 1024;
			new_hashmask = 0x3FF;
			break;
	}

	ht = kcalloc(new_divisor, sizeof(struct dn_fib_node*), GFP_KERNEL);
	if (ht == NULL)
		return;

	write_lock_bh(&dn_fib_tables_lock);
	old_ht = dz->dz_hash;
	dz->dz_hash = ht;
	dz->dz_hashmask = new_hashmask;
	dz->dz_divisor = new_divisor;
	dn_rebuild_zone(dz, old_ht, old_divisor);
	write_unlock_bh(&dn_fib_tables_lock);
	kfree(old_ht);
}

static void dn_free_node(struct dn_fib_node *f)
{
	dn_fib_release_info(DN_FIB_INFO(f));
	kmem_cache_free(dn_hash_kmem, f);
}


static struct dn_zone *dn_new_zone(struct dn_hash *table, int z)
{
	int i;
	struct dn_zone *dz = kzalloc(sizeof(struct dn_zone), GFP_KERNEL);
	if (!dz)
		return NULL;

	if (z) {
		dz->dz_divisor = 16;
		dz->dz_hashmask = 0x0F;
	} else {
		dz->dz_divisor = 1;
		dz->dz_hashmask = 0;
	}

	dz->dz_hash = kcalloc(dz->dz_divisor, sizeof(struct dn_fib_node *), GFP_KERNEL);
	if (!dz->dz_hash) {
		kfree(dz);
		return NULL;
	}

	dz->dz_order = z;
	dz->dz_mask = dnet_make_mask(z);

	for(i = z + 1; i <= 16; i++)
		if (table->dh_zones[i])
			break;

	write_lock_bh(&dn_fib_tables_lock);
	if (i>16) {
		dz->dz_next = table->dh_zone_list;
		table->dh_zone_list = dz;
	} else {
		dz->dz_next = table->dh_zones[i]->dz_next;
		table->dh_zones[i]->dz_next = dz;
	}
	table->dh_zones[z] = dz;
	write_unlock_bh(&dn_fib_tables_lock);
	return dz;
}


static int dn_fib_nh_match(struct rtmsg *r, struct nlmsghdr *nlh, struct dn_kern_rta *rta, struct dn_fib_info *fi)
{
	struct rtnexthop *nhp;
	int nhlen;

	if (rta->rta_priority && *rta->rta_priority != fi->fib_priority)
		return 1;

	if (rta->rta_oif || rta->rta_gw) {
		if ((!rta->rta_oif || *rta->rta_oif == fi->fib_nh->nh_oif) &&
		    (!rta->rta_gw  || memcmp(rta->rta_gw, &fi->fib_nh->nh_gw, 2) == 0))
			return 0;
		return 1;
	}

	if (rta->rta_mp == NULL)
		return 0;

	nhp = RTA_DATA(rta->rta_mp);
	nhlen = RTA_PAYLOAD(rta->rta_mp);

	for_nexthops(fi) {
		int attrlen = nhlen - sizeof(struct rtnexthop);
		__le16 gw;

		if (attrlen < 0 || (nhlen -= nhp->rtnh_len) < 0)
			return -EINVAL;
		if (nhp->rtnh_ifindex && nhp->rtnh_ifindex != nh->nh_oif)
			return 1;
		if (attrlen) {
			gw = dn_fib_get_attr16(RTNH_DATA(nhp), attrlen, RTA_GATEWAY);

			if (gw && gw != nh->nh_gw)
				return 1;
		}
		nhp = RTNH_NEXT(nhp);
	} endfor_nexthops(fi);

	return 0;
}

static inline size_t dn_fib_nlmsg_size(struct dn_fib_info *fi)
{
	size_t payload = NLMSG_ALIGN(sizeof(struct rtmsg))
			 + nla_total_size(4) /* RTA_TABLE */
			 + nla_total_size(2) /* RTA_DST */
			 + nla_total_size(4); /* RTA_PRIORITY */

	/* space for nested metrics */
	payload += nla_total_size((RTAX_MAX * nla_total_size(4)));

	if (fi->fib_nhs) {
		/* Also handles the special case fib_nhs == 1 */

		/* each nexthop is packed in an attribute */
		size_t nhsize = nla_total_size(sizeof(struct rtnexthop));

		/* may contain a gateway attribute */
		nhsize += nla_total_size(4);

		/* all nexthops are packed in a nested attribute */
		payload += nla_total_size(fi->fib_nhs * nhsize);
	}

	return payload;
}

static int dn_fib_dump_info(struct sk_buff *skb, u32 pid, u32 seq, int event,
			u32 tb_id, u8 type, u8 scope, void *dst, int dst_len,
			struct dn_fib_info *fi, unsigned int flags)
{
	struct rtmsg *rtm;
	struct nlmsghdr *nlh;
	unsigned char *b = skb_tail_pointer(skb);

	nlh = NLMSG_NEW(skb, pid, seq, event, sizeof(*rtm), flags);
	rtm = NLMSG_DATA(nlh);
	rtm->rtm_family = AF_DECnet;
	rtm->rtm_dst_len = dst_len;
	rtm->rtm_src_len = 0;
	rtm->rtm_tos = 0;
	rtm->rtm_table = tb_id;
	RTA_PUT_U32(skb, RTA_TABLE, tb_id);
	rtm->rtm_flags = fi->fib_flags;
	rtm->rtm_scope = scope;
	rtm->rtm_type  = type;
	if (rtm->rtm_dst_len)
		RTA_PUT(skb, RTA_DST, 2, dst);
	rtm->rtm_protocol = fi->fib_protocol;
	if (fi->fib_priority)
		RTA_PUT(skb, RTA_PRIORITY, 4, &fi->fib_priority);
	if (rtnetlink_put_metrics(skb, fi->fib_metrics) < 0)
		goto rtattr_failure;
	if (fi->fib_nhs == 1) {
		if (fi->fib_nh->nh_gw)
			RTA_PUT(skb, RTA_GATEWAY, 2, &fi->fib_nh->nh_gw);
		if (fi->fib_nh->nh_oif)
			RTA_PUT(skb, RTA_OIF, sizeof(int), &fi->fib_nh->nh_oif);
	}
	if (fi->fib_nhs > 1) {
		struct rtnexthop *nhp;
		struct rtattr *mp_head;
		if (skb_tailroom(skb) <= RTA_SPACE(0))
			goto rtattr_failure;
		mp_head = (struct rtattr *)skb_put(skb, RTA_SPACE(0));

		for_nexthops(fi) {
			if (skb_tailroom(skb) < RTA_ALIGN(RTA_ALIGN(sizeof(*nhp)) + 4))
				goto rtattr_failure;
			nhp = (struct rtnexthop *)skb_put(skb, RTA_ALIGN(sizeof(*nhp)));
			nhp->rtnh_flags = nh->nh_flags & 0xFF;
			nhp->rtnh_hops = nh->nh_weight - 1;
			nhp->rtnh_ifindex = nh->nh_oif;
			if (nh->nh_gw)
				RTA_PUT(skb, RTA_GATEWAY, 2, &nh->nh_gw);
			nhp->rtnh_len = skb_tail_pointer(skb) - (unsigned char *)nhp;
		} endfor_nexthops(fi);
		mp_head->rta_type = RTA_MULTIPATH;
		mp_head->rta_len = skb_tail_pointer(skb) - (u8 *)mp_head;
	}

	nlh->nlmsg_len = skb_tail_pointer(skb) - b;
	return skb->len;


nlmsg_failure:
rtattr_failure:
	nlmsg_trim(skb, b);
	return -EMSGSIZE;
}


static void dn_rtmsg_fib(int event, struct dn_fib_node *f, int z, u32 tb_id,
			struct nlmsghdr *nlh, struct netlink_skb_parms *req)
{
	struct sk_buff *skb;
	u32 pid = req ? req->pid : 0;
	int err = -ENOBUFS;

	skb = nlmsg_new(dn_fib_nlmsg_size(DN_FIB_INFO(f)), GFP_KERNEL);
	if (skb == NULL)
		goto errout;

	err = dn_fib_dump_info(skb, pid, nlh->nlmsg_seq, event, tb_id,
			       f->fn_type, f->fn_scope, &f->fn_key, z,
			       DN_FIB_INFO(f), 0);
	if (err < 0) {
		/* -EMSGSIZE implies BUG in dn_fib_nlmsg_size() */
		WARN_ON(err == -EMSGSIZE);
		kfree_skb(skb);
		goto errout;
	}
	rtnl_notify(skb, &init_net, pid, RTNLGRP_DECnet_ROUTE, nlh, GFP_KERNEL);
	return;
errout:
	if (err < 0)
		rtnl_set_sk_err(&init_net, RTNLGRP_DECnet_ROUTE, err);
}

static __inline__ int dn_hash_dump_bucket(struct sk_buff *skb,
				struct netlink_callback *cb,
				struct dn_fib_table *tb,
				struct dn_zone *dz,
				struct dn_fib_node *f)
{
	int i, s_i;

	s_i = cb->args[4];
	for(i = 0; f; i++, f = f->fn_next) {
		if (i < s_i)
			continue;
		if (f->fn_state & DN_S_ZOMBIE)
			continue;
		if (dn_fib_dump_info(skb, NETLINK_CB(cb->skb).pid,
				cb->nlh->nlmsg_seq,
				RTM_NEWROUTE,
				tb->n,
				(f->fn_state & DN_S_ZOMBIE) ? 0 : f->fn_type,
				f->fn_scope, &f->fn_key, dz->dz_order,
				f->fn_info, NLM_F_MULTI) < 0) {
			cb->args[4] = i;
			return -1;
		}
	}
	cb->args[4] = i;
	return skb->len;
}

static __inline__ int dn_hash_dump_zone(struct sk_buff *skb,
				struct netlink_callback *cb,
				struct dn_fib_table *tb,
				struct dn_zone *dz)
{
	int h, s_h;

	s_h = cb->args[3];
	for(h = 0; h < dz->dz_divisor; h++) {
		if (h < s_h)
			continue;
		if (h > s_h)
			memset(&cb->args[4], 0, sizeof(cb->args) - 4*sizeof(cb->args[0]));
		if (dz->dz_hash == NULL || dz->dz_hash[h] == NULL)
			continue;
		if (dn_hash_dump_bucket(skb, cb, tb, dz, dz->dz_hash[h]) < 0) {
			cb->args[3] = h;
			return -1;
		}
	}
	cb->args[3] = h;
	return skb->len;
}

static int dn_fib_table_dump(struct dn_fib_table *tb, struct sk_buff *skb,
				struct netlink_callback *cb)
{
	int m, s_m;
	struct dn_zone *dz;
	struct dn_hash *table = (struct dn_hash *)tb->data;

	s_m = cb->args[2];
	read_lock(&dn_fib_tables_lock);
	for(dz = table->dh_zone_list, m = 0; dz; dz = dz->dz_next, m++) {
		if (m < s_m)
			continue;
		if (m > s_m)
			memset(&cb->args[3], 0, sizeof(cb->args) - 3*sizeof(cb->args[0]));

		if (dn_hash_dump_zone(skb, cb, tb, dz) < 0) {
			cb->args[2] = m;
			read_unlock(&dn_fib_tables_lock);
			return -1;
		}
	}
	read_unlock(&dn_fib_tables_lock);
	cb->args[2] = m;

	return skb->len;
}

int dn_fib_dump(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct net *net = sock_net(skb->sk);
	unsigned int h, s_h;
	unsigned int e = 0, s_e;
	struct dn_fib_table *tb;
	struct hlist_node *node;
	int dumped = 0;

	if (!net_eq(net, &init_net))
		return 0;

	if (NLMSG_PAYLOAD(cb->nlh, 0) >= sizeof(struct rtmsg) &&
		((struct rtmsg *)NLMSG_DATA(cb->nlh))->rtm_flags&RTM_F_CLONED)
			return dn_cache_dump(skb, cb);

	s_h = cb->args[0];
	s_e = cb->args[1];

	for (h = s_h; h < DN_FIB_TABLE_HASHSZ; h++, s_h = 0) {
		e = 0;
		hlist_for_each_entry(tb, node, &dn_fib_table_hash[h], hlist) {
			if (e < s_e)
				goto next;
			if (dumped)
				memset(&cb->args[2], 0, sizeof(cb->args) -
						 2 * sizeof(cb->args[0]));
			if (tb->dump(tb, skb, cb) < 0)
				goto out;
			dumped = 1;
next:
			e++;
		}
	}
out:
	cb->args[1] = e;
	cb->args[0] = h;

	return skb->len;
}

static int dn_fib_table_insert(struct dn_fib_table *tb, struct rtmsg *r, struct dn_kern_rta *rta, struct nlmsghdr *n, struct netlink_skb_parms *req)
{
	struct dn_hash *table = (struct dn_hash *)tb->data;
	struct dn_fib_node *new_f, *f, **fp, **del_fp;
	struct dn_zone *dz;
	struct dn_fib_info *fi;
	int z = r->rtm_dst_len;
	int type = r->rtm_type;
	dn_fib_key_t key;
	int err;

	if (z > 16)
		return -EINVAL;

	dz = table->dh_zones[z];
	if (!dz && !(dz = dn_new_zone(table, z)))
		return -ENOBUFS;

	dz_key_0(key);
	if (rta->rta_dst) {
		__le16 dst;
		memcpy(&dst, rta->rta_dst, 2);
		if (dst & ~DZ_MASK(dz))
			return -EINVAL;
		key = dz_key(dst, dz);
	}

	if ((fi = dn_fib_create_info(r, rta, n, &err)) == NULL)
		return err;

	if (dz->dz_nent > (dz->dz_divisor << 2) &&
			dz->dz_divisor > DN_MAX_DIVISOR &&
			(z==16 || (1<<z) > dz->dz_divisor))
		dn_rehash_zone(dz);

	fp = dn_chain_p(key, dz);

	DN_FIB_SCAN(f, fp) {
		if (dn_key_leq(key, f->fn_key))
			break;
	}

	del_fp = NULL;

	if (f && (f->fn_state & DN_S_ZOMBIE) &&
			dn_key_eq(f->fn_key, key)) {
		del_fp = fp;
		fp = &f->fn_next;
		f = *fp;
		goto create;
	}

	DN_FIB_SCAN_KEY(f, fp, key) {
		if (fi->fib_priority <= DN_FIB_INFO(f)->fib_priority)
			break;
	}

	if (f && dn_key_eq(f->fn_key, key) &&
			fi->fib_priority == DN_FIB_INFO(f)->fib_priority) {
		struct dn_fib_node **ins_fp;

		err = -EEXIST;
		if (n->nlmsg_flags & NLM_F_EXCL)
			goto out;

		if (n->nlmsg_flags & NLM_F_REPLACE) {
			del_fp = fp;
			fp = &f->fn_next;
			f = *fp;
			goto replace;
		}

		ins_fp = fp;
		err = -EEXIST;

		DN_FIB_SCAN_KEY(f, fp, key) {
			if (fi->fib_priority != DN_FIB_INFO(f)->fib_priority)
				break;
			if (f->fn_type == type &&
			    f->fn_scope == r->rtm_scope &&
			    DN_FIB_INFO(f) == fi)
				goto out;
		}

		if (!(n->nlmsg_flags & NLM_F_APPEND)) {
			fp = ins_fp;
			f = *fp;
		}
	}

create:
	err = -ENOENT;
	if (!(n->nlmsg_flags & NLM_F_CREATE))
		goto out;

replace:
	err = -ENOBUFS;
	new_f = kmem_cache_zalloc(dn_hash_kmem, GFP_KERNEL);
	if (new_f == NULL)
		goto out;

	new_f->fn_key = key;
	new_f->fn_type = type;
	new_f->fn_scope = r->rtm_scope;
	DN_FIB_INFO(new_f) = fi;

	new_f->fn_next = f;
	write_lock_bh(&dn_fib_tables_lock);
	*fp = new_f;
	write_unlock_bh(&dn_fib_tables_lock);
	dz->dz_nent++;

	if (del_fp) {
		f = *del_fp;
		write_lock_bh(&dn_fib_tables_lock);
		*del_fp = f->fn_next;
		write_unlock_bh(&dn_fib_tables_lock);

		if (!(f->fn_state & DN_S_ZOMBIE))
			dn_rtmsg_fib(RTM_DELROUTE, f, z, tb->n, n, req);
		if (f->fn_state & DN_S_ACCESSED)
			dn_rt_cache_flush(-1);
		dn_free_node(f);
		dz->dz_nent--;
	} else {
		dn_rt_cache_flush(-1);
	}

	dn_rtmsg_fib(RTM_NEWROUTE, new_f, z, tb->n, n, req);

	return 0;
out:
	dn_fib_release_info(fi);
	return err;
}


static int dn_fib_table_delete(struct dn_fib_table *tb, struct rtmsg *r, struct dn_kern_rta *rta, struct nlmsghdr *n, struct netlink_skb_parms *req)
{
	struct dn_hash *table = (struct dn_hash*)tb->data;
	struct dn_fib_node **fp, **del_fp, *f;
	int z = r->rtm_dst_len;
	struct dn_zone *dz;
	dn_fib_key_t key;
	int matched;


	if (z > 16)
		return -EINVAL;

	if ((dz = table->dh_zones[z]) == NULL)
		return -ESRCH;

	dz_key_0(key);
	if (rta->rta_dst) {
		__le16 dst;
		memcpy(&dst, rta->rta_dst, 2);
		if (dst & ~DZ_MASK(dz))
			return -EINVAL;
		key = dz_key(dst, dz);
	}

	fp = dn_chain_p(key, dz);

	DN_FIB_SCAN(f, fp) {
		if (dn_key_eq(f->fn_key, key))
			break;
		if (dn_key_leq(key, f->fn_key))
			return -ESRCH;
	}

	matched = 0;
	del_fp = NULL;
	DN_FIB_SCAN_KEY(f, fp, key) {
		struct dn_fib_info *fi = DN_FIB_INFO(f);

		if (f->fn_state & DN_S_ZOMBIE)
			return -ESRCH;

		matched++;

		if (del_fp == NULL &&
				(!r->rtm_type || f->fn_type == r->rtm_type) &&
				(r->rtm_scope == RT_SCOPE_NOWHERE || f->fn_scope == r->rtm_scope) &&
				(!r->rtm_protocol ||
					fi->fib_protocol == r->rtm_protocol) &&
				dn_fib_nh_match(r, n, rta, fi) == 0)
			del_fp = fp;
	}

	if (del_fp) {
		f = *del_fp;
		dn_rtmsg_fib(RTM_DELROUTE, f, z, tb->n, n, req);

		if (matched != 1) {
			write_lock_bh(&dn_fib_tables_lock);
			*del_fp = f->fn_next;
			write_unlock_bh(&dn_fib_tables_lock);

			if (f->fn_state & DN_S_ACCESSED)
				dn_rt_cache_flush(-1);
			dn_free_node(f);
			dz->dz_nent--;
		} else {
			f->fn_state |= DN_S_ZOMBIE;
			if (f->fn_state & DN_S_ACCESSED) {
				f->fn_state &= ~DN_S_ACCESSED;
				dn_rt_cache_flush(-1);
			}
			if (++dn_fib_hash_zombies > 128)
				dn_fib_flush();
		}

		return 0;
	}

	return -ESRCH;
}

static inline int dn_flush_list(struct dn_fib_node **fp, int z, struct dn_hash *table)
{
	int found = 0;
	struct dn_fib_node *f;

	while((f = *fp) != NULL) {
		struct dn_fib_info *fi = DN_FIB_INFO(f);

		if (fi && ((f->fn_state & DN_S_ZOMBIE) || (fi->fib_flags & RTNH_F_DEAD))) {
			write_lock_bh(&dn_fib_tables_lock);
			*fp = f->fn_next;
			write_unlock_bh(&dn_fib_tables_lock);

			dn_free_node(f);
			found++;
			continue;
		}
		fp = &f->fn_next;
	}

	return found;
}

static int dn_fib_table_flush(struct dn_fib_table *tb)
{
	struct dn_hash *table = (struct dn_hash *)tb->data;
	struct dn_zone *dz;
	int found = 0;

	dn_fib_hash_zombies = 0;
	for(dz = table->dh_zone_list; dz; dz = dz->dz_next) {
		int i;
		int tmp = 0;
		for(i = dz->dz_divisor-1; i >= 0; i--)
			tmp += dn_flush_list(&dz->dz_hash[i], dz->dz_order, table);
		dz->dz_nent -= tmp;
		found += tmp;
	}

	return found;
}

static int dn_fib_table_lookup(struct dn_fib_table *tb, const struct flowi *flp, struct dn_fib_res *res)
{
	int err;
	struct dn_zone *dz;
	struct dn_hash *t = (struct dn_hash *)tb->data;

	read_lock(&dn_fib_tables_lock);
	for(dz = t->dh_zone_list; dz; dz = dz->dz_next) {
		struct dn_fib_node *f;
		dn_fib_key_t k = dz_key(flp->fld_dst, dz);

		for(f = dz_chain(k, dz); f; f = f->fn_next) {
			if (!dn_key_eq(k, f->fn_key)) {
				if (dn_key_leq(k, f->fn_key))
					break;
				else
					continue;
			}

			f->fn_state |= DN_S_ACCESSED;

			if (f->fn_state&DN_S_ZOMBIE)
				continue;

			if (f->fn_scope < flp->fld_scope)
				continue;

			err = dn_fib_semantic_match(f->fn_type, DN_FIB_INFO(f), flp, res);

			if (err == 0) {
				res->type = f->fn_type;
				res->scope = f->fn_scope;
				res->prefixlen = dz->dz_order;
				goto out;
			}
			if (err < 0)
				goto out;
		}
	}
	err = 1;
out:
	read_unlock(&dn_fib_tables_lock);
	return err;
}


struct dn_fib_table *dn_fib_get_table(u32 n, int create)
{
	struct dn_fib_table *t;
	struct hlist_node *node;
	unsigned int h;

	if (n < RT_TABLE_MIN)
		return NULL;

	if (n > RT_TABLE_MAX)
		return NULL;

	h = n & (DN_FIB_TABLE_HASHSZ - 1);
	rcu_read_lock();
	hlist_for_each_entry_rcu(t, node, &dn_fib_table_hash[h], hlist) {
		if (t->n == n) {
			rcu_read_unlock();
			return t;
		}
	}
	rcu_read_unlock();

	if (!create)
		return NULL;

	if (in_interrupt() && net_ratelimit()) {
		printk(KERN_DEBUG "DECnet: BUG! Attempt to create routing table from interrupt\n");
		return NULL;
	}

	t = kzalloc(sizeof(struct dn_fib_table) + sizeof(struct dn_hash),
		    GFP_KERNEL);
	if (t == NULL)
		return NULL;

	t->n = n;
	t->insert = dn_fib_table_insert;
	t->delete = dn_fib_table_delete;
	t->lookup = dn_fib_table_lookup;
	t->flush  = dn_fib_table_flush;
	t->dump = dn_fib_table_dump;
	hlist_add_head_rcu(&t->hlist, &dn_fib_table_hash[h]);

	return t;
}

struct dn_fib_table *dn_fib_empty_table(void)
{
	u32 id;

	for(id = RT_TABLE_MIN; id <= RT_TABLE_MAX; id++)
		if (dn_fib_get_table(id, 0) == NULL)
			return dn_fib_get_table(id, 1);
	return NULL;
}

void dn_fib_flush(void)
{
	int flushed = 0;
	struct dn_fib_table *tb;
	struct hlist_node *node;
	unsigned int h;

	for (h = 0; h < DN_FIB_TABLE_HASHSZ; h++) {
		hlist_for_each_entry(tb, node, &dn_fib_table_hash[h], hlist)
			flushed += tb->flush(tb);
	}

	if (flushed)
		dn_rt_cache_flush(-1);
}

void __init dn_fib_table_init(void)
{
	dn_hash_kmem = kmem_cache_create("dn_fib_info_cache",
					sizeof(struct dn_fib_info),
					0, SLAB_HWCACHE_ALIGN,
					NULL);
}

void __exit dn_fib_table_cleanup(void)
{
	struct dn_fib_table *t;
	struct hlist_node *node, *next;
	unsigned int h;

	write_lock(&dn_fib_tables_lock);
	for (h = 0; h < DN_FIB_TABLE_HASHSZ; h++) {
		hlist_for_each_entry_safe(t, node, next, &dn_fib_table_hash[h],
					  hlist) {
			hlist_del(&t->hlist);
			kfree(t);
		}
	}
	write_unlock(&dn_fib_tables_lock);
}
