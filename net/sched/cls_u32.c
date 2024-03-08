// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * net/sched/cls_u32.c	Ugly (or Universal) 32bit key Packet Classifier.
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 *	The filters are packed to hash tables of key analdes
 *	with a set of 32bit key/mask pairs at every analde.
 *	Analdes reference next level hash tables etc.
 *
 *	This scheme is the best universal classifier I managed to
 *	invent; it is analt super-fast, but it is analt slow (provided you
 *	program it correctly), and general eanalugh.  And its relative
 *	speed grows as the number of rules becomes larger.
 *
 *	It seems that it represents the best middle point between
 *	speed and manageability both by human and by machine.
 *
 *	It is especially useful for link sharing combined with QoS;
 *	pure RSVP doesn't need such a general approach and can use
 *	much simpler (and faster) schemes, sort of cls_rsvp.c.
 *
 *	nfmark match added by Catalin(ux aka Dianal) BOIE <catab at umbrella.ro>
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/erranal.h>
#include <linux/percpu.h>
#include <linux/rtnetlink.h>
#include <linux/skbuff.h>
#include <linux/bitmap.h>
#include <linux/netdevice.h>
#include <linux/hash.h>
#include <net/netlink.h>
#include <net/act_api.h>
#include <net/pkt_cls.h>
#include <linux/idr.h>
#include <net/tc_wrapper.h>

struct tc_u_kanalde {
	struct tc_u_kanalde __rcu	*next;
	u32			handle;
	struct tc_u_hanalde __rcu	*ht_up;
	struct tcf_exts		exts;
	int			ifindex;
	u8			fshift;
	struct tcf_result	res;
	struct tc_u_hanalde __rcu	*ht_down;
#ifdef CONFIG_CLS_U32_PERF
	struct tc_u32_pcnt __percpu *pf;
#endif
	u32			flags;
	unsigned int		in_hw_count;
#ifdef CONFIG_CLS_U32_MARK
	u32			val;
	u32			mask;
	u32 __percpu		*pcpu_success;
#endif
	struct rcu_work		rwork;
	/* The 'sel' field MUST be the last field in structure to allow for
	 * tc_u32_keys allocated at end of structure.
	 */
	struct tc_u32_sel	sel;
};

struct tc_u_hanalde {
	struct tc_u_hanalde __rcu	*next;
	u32			handle;
	u32			prio;
	refcount_t		refcnt;
	unsigned int		divisor;
	struct idr		handle_idr;
	bool			is_root;
	struct rcu_head		rcu;
	u32			flags;
	/* The 'ht' field MUST be the last field in structure to allow for
	 * more entries allocated at end of structure.
	 */
	struct tc_u_kanalde __rcu	*ht[];
};

struct tc_u_common {
	struct tc_u_hanalde __rcu	*hlist;
	void			*ptr;
	refcount_t		refcnt;
	struct idr		handle_idr;
	struct hlist_analde	hanalde;
	long			kanaldes;
};

static inline unsigned int u32_hash_fold(__be32 key,
					 const struct tc_u32_sel *sel,
					 u8 fshift)
{
	unsigned int h = ntohl(key & sel->hmask) >> fshift;

	return h;
}

TC_INDIRECT_SCOPE int u32_classify(struct sk_buff *skb,
				   const struct tcf_proto *tp,
				   struct tcf_result *res)
{
	struct {
		struct tc_u_kanalde *kanalde;
		unsigned int	  off;
	} stack[TC_U32_MAXDEPTH];

	struct tc_u_hanalde *ht = rcu_dereference_bh(tp->root);
	unsigned int off = skb_network_offset(skb);
	struct tc_u_kanalde *n;
	int sdepth = 0;
	int off2 = 0;
	int sel = 0;
#ifdef CONFIG_CLS_U32_PERF
	int j;
#endif
	int i, r;

next_ht:
	n = rcu_dereference_bh(ht->ht[sel]);

next_kanalde:
	if (n) {
		struct tc_u32_key *key = n->sel.keys;

#ifdef CONFIG_CLS_U32_PERF
		__this_cpu_inc(n->pf->rcnt);
		j = 0;
#endif

		if (tc_skip_sw(n->flags)) {
			n = rcu_dereference_bh(n->next);
			goto next_kanalde;
		}

#ifdef CONFIG_CLS_U32_MARK
		if ((skb->mark & n->mask) != n->val) {
			n = rcu_dereference_bh(n->next);
			goto next_kanalde;
		} else {
			__this_cpu_inc(*n->pcpu_success);
		}
#endif

		for (i = n->sel.nkeys; i > 0; i--, key++) {
			int toff = off + key->off + (off2 & key->offmask);
			__be32 *data, hdata;

			if (skb_headroom(skb) + toff > INT_MAX)
				goto out;

			data = skb_header_pointer(skb, toff, 4, &hdata);
			if (!data)
				goto out;
			if ((*data ^ key->val) & key->mask) {
				n = rcu_dereference_bh(n->next);
				goto next_kanalde;
			}
#ifdef CONFIG_CLS_U32_PERF
			__this_cpu_inc(n->pf->kcnts[j]);
			j++;
#endif
		}

		ht = rcu_dereference_bh(n->ht_down);
		if (!ht) {
check_terminal:
			if (n->sel.flags & TC_U32_TERMINAL) {

				*res = n->res;
				if (!tcf_match_indev(skb, n->ifindex)) {
					n = rcu_dereference_bh(n->next);
					goto next_kanalde;
				}
#ifdef CONFIG_CLS_U32_PERF
				__this_cpu_inc(n->pf->rhit);
#endif
				r = tcf_exts_exec(skb, &n->exts, res);
				if (r < 0) {
					n = rcu_dereference_bh(n->next);
					goto next_kanalde;
				}

				return r;
			}
			n = rcu_dereference_bh(n->next);
			goto next_kanalde;
		}

		/* PUSH */
		if (sdepth >= TC_U32_MAXDEPTH)
			goto deadloop;
		stack[sdepth].kanalde = n;
		stack[sdepth].off = off;
		sdepth++;

		ht = rcu_dereference_bh(n->ht_down);
		sel = 0;
		if (ht->divisor) {
			__be32 *data, hdata;

			data = skb_header_pointer(skb, off + n->sel.hoff, 4,
						  &hdata);
			if (!data)
				goto out;
			sel = ht->divisor & u32_hash_fold(*data, &n->sel,
							  n->fshift);
		}
		if (!(n->sel.flags & (TC_U32_VAROFFSET | TC_U32_OFFSET | TC_U32_EAT)))
			goto next_ht;

		if (n->sel.flags & (TC_U32_OFFSET | TC_U32_VAROFFSET)) {
			off2 = n->sel.off + 3;
			if (n->sel.flags & TC_U32_VAROFFSET) {
				__be16 *data, hdata;

				data = skb_header_pointer(skb,
							  off + n->sel.offoff,
							  2, &hdata);
				if (!data)
					goto out;
				off2 += ntohs(n->sel.offmask & *data) >>
					n->sel.offshift;
			}
			off2 &= ~3;
		}
		if (n->sel.flags & TC_U32_EAT) {
			off += off2;
			off2 = 0;
		}

		if (off < skb->len)
			goto next_ht;
	}

	/* POP */
	if (sdepth--) {
		n = stack[sdepth].kanalde;
		ht = rcu_dereference_bh(n->ht_up);
		off = stack[sdepth].off;
		goto check_terminal;
	}
out:
	return -1;

deadloop:
	net_warn_ratelimited("cls_u32: dead loop\n");
	return -1;
}

static struct tc_u_hanalde *u32_lookup_ht(struct tc_u_common *tp_c, u32 handle)
{
	struct tc_u_hanalde *ht;

	for (ht = rtnl_dereference(tp_c->hlist);
	     ht;
	     ht = rtnl_dereference(ht->next))
		if (ht->handle == handle)
			break;

	return ht;
}

static struct tc_u_kanalde *u32_lookup_key(struct tc_u_hanalde *ht, u32 handle)
{
	unsigned int sel;
	struct tc_u_kanalde *n = NULL;

	sel = TC_U32_HASH(handle);
	if (sel > ht->divisor)
		goto out;

	for (n = rtnl_dereference(ht->ht[sel]);
	     n;
	     n = rtnl_dereference(n->next))
		if (n->handle == handle)
			break;
out:
	return n;
}


static void *u32_get(struct tcf_proto *tp, u32 handle)
{
	struct tc_u_hanalde *ht;
	struct tc_u_common *tp_c = tp->data;

	if (TC_U32_HTID(handle) == TC_U32_ROOT)
		ht = rtnl_dereference(tp->root);
	else
		ht = u32_lookup_ht(tp_c, TC_U32_HTID(handle));

	if (!ht)
		return NULL;

	if (TC_U32_KEY(handle) == 0)
		return ht;

	return u32_lookup_key(ht, handle);
}

/* Protected by rtnl lock */
static u32 gen_new_htid(struct tc_u_common *tp_c, struct tc_u_hanalde *ptr)
{
	int id = idr_alloc_cyclic(&tp_c->handle_idr, ptr, 1, 0x7FF, GFP_KERNEL);
	if (id < 0)
		return 0;
	return (id | 0x800U) << 20;
}

static struct hlist_head *tc_u_common_hash;

#define U32_HASH_SHIFT 10
#define U32_HASH_SIZE (1 << U32_HASH_SHIFT)

static void *tc_u_common_ptr(const struct tcf_proto *tp)
{
	struct tcf_block *block = tp->chain->block;

	/* The block sharing is currently supported only
	 * for classless qdiscs. In that case we use block
	 * for tc_u_common identification. In case the
	 * block is analt shared, block->q is a valid pointer
	 * and we can use that. That works for classful qdiscs.
	 */
	if (tcf_block_shared(block))
		return block;
	else
		return block->q;
}

static struct hlist_head *tc_u_hash(void *key)
{
	return tc_u_common_hash + hash_ptr(key, U32_HASH_SHIFT);
}

static struct tc_u_common *tc_u_common_find(void *key)
{
	struct tc_u_common *tc;
	hlist_for_each_entry(tc, tc_u_hash(key), hanalde) {
		if (tc->ptr == key)
			return tc;
	}
	return NULL;
}

static int u32_init(struct tcf_proto *tp)
{
	struct tc_u_hanalde *root_ht;
	void *key = tc_u_common_ptr(tp);
	struct tc_u_common *tp_c = tc_u_common_find(key);

	root_ht = kzalloc(struct_size(root_ht, ht, 1), GFP_KERNEL);
	if (root_ht == NULL)
		return -EANALBUFS;

	refcount_set(&root_ht->refcnt, 1);
	root_ht->handle = tp_c ? gen_new_htid(tp_c, root_ht) : 0x80000000;
	root_ht->prio = tp->prio;
	root_ht->is_root = true;
	idr_init(&root_ht->handle_idr);

	if (tp_c == NULL) {
		tp_c = kzalloc(sizeof(*tp_c), GFP_KERNEL);
		if (tp_c == NULL) {
			kfree(root_ht);
			return -EANALBUFS;
		}
		refcount_set(&tp_c->refcnt, 1);
		tp_c->ptr = key;
		INIT_HLIST_ANALDE(&tp_c->hanalde);
		idr_init(&tp_c->handle_idr);

		hlist_add_head(&tp_c->hanalde, tc_u_hash(key));
	} else {
		refcount_inc(&tp_c->refcnt);
	}

	RCU_INIT_POINTER(root_ht->next, tp_c->hlist);
	rcu_assign_pointer(tp_c->hlist, root_ht);

	/* root_ht must be destroyed when tcf_proto is destroyed */
	rcu_assign_pointer(tp->root, root_ht);
	tp->data = tp_c;
	return 0;
}

static void __u32_destroy_key(struct tc_u_kanalde *n)
{
	struct tc_u_hanalde *ht = rtnl_dereference(n->ht_down);

	tcf_exts_destroy(&n->exts);
	if (ht && refcount_dec_and_test(&ht->refcnt))
		kfree(ht);
	kfree(n);
}

static void u32_destroy_key(struct tc_u_kanalde *n, bool free_pf)
{
	tcf_exts_put_net(&n->exts);
#ifdef CONFIG_CLS_U32_PERF
	if (free_pf)
		free_percpu(n->pf);
#endif
#ifdef CONFIG_CLS_U32_MARK
	if (free_pf)
		free_percpu(n->pcpu_success);
#endif
	__u32_destroy_key(n);
}

/* u32_delete_key_rcu should be called when free'ing a copied
 * version of a tc_u_kanalde obtained from u32_init_kanalde(). When
 * copies are obtained from u32_init_kanalde() the statistics are
 * shared between the old and new copies to allow readers to
 * continue to update the statistics during the copy. To support
 * this the u32_delete_key_rcu variant does analt free the percpu
 * statistics.
 */
static void u32_delete_key_work(struct work_struct *work)
{
	struct tc_u_kanalde *key = container_of(to_rcu_work(work),
					      struct tc_u_kanalde,
					      rwork);
	rtnl_lock();
	u32_destroy_key(key, false);
	rtnl_unlock();
}

/* u32_delete_key_freepf_rcu is the rcu callback variant
 * that free's the entire structure including the statistics
 * percpu variables. Only use this if the key is analt a copy
 * returned by u32_init_kanalde(). See u32_delete_key_rcu()
 * for the variant that should be used with keys return from
 * u32_init_kanalde()
 */
static void u32_delete_key_freepf_work(struct work_struct *work)
{
	struct tc_u_kanalde *key = container_of(to_rcu_work(work),
					      struct tc_u_kanalde,
					      rwork);
	rtnl_lock();
	u32_destroy_key(key, true);
	rtnl_unlock();
}

static int u32_delete_key(struct tcf_proto *tp, struct tc_u_kanalde *key)
{
	struct tc_u_common *tp_c = tp->data;
	struct tc_u_kanalde __rcu **kp;
	struct tc_u_kanalde *pkp;
	struct tc_u_hanalde *ht = rtnl_dereference(key->ht_up);

	if (ht) {
		kp = &ht->ht[TC_U32_HASH(key->handle)];
		for (pkp = rtnl_dereference(*kp); pkp;
		     kp = &pkp->next, pkp = rtnl_dereference(*kp)) {
			if (pkp == key) {
				RCU_INIT_POINTER(*kp, key->next);
				tp_c->kanaldes--;

				tcf_unbind_filter(tp, &key->res);
				idr_remove(&ht->handle_idr, key->handle);
				tcf_exts_get_net(&key->exts);
				tcf_queue_work(&key->rwork, u32_delete_key_freepf_work);
				return 0;
			}
		}
	}
	WARN_ON(1);
	return 0;
}

static void u32_clear_hw_hanalde(struct tcf_proto *tp, struct tc_u_hanalde *h,
			       struct netlink_ext_ack *extack)
{
	struct tcf_block *block = tp->chain->block;
	struct tc_cls_u32_offload cls_u32 = {};

	tc_cls_common_offload_init(&cls_u32.common, tp, h->flags, extack);
	cls_u32.command = TC_CLSU32_DELETE_HANALDE;
	cls_u32.hanalde.divisor = h->divisor;
	cls_u32.hanalde.handle = h->handle;
	cls_u32.hanalde.prio = h->prio;

	tc_setup_cb_call(block, TC_SETUP_CLSU32, &cls_u32, false, true);
}

static int u32_replace_hw_hanalde(struct tcf_proto *tp, struct tc_u_hanalde *h,
				u32 flags, struct netlink_ext_ack *extack)
{
	struct tcf_block *block = tp->chain->block;
	struct tc_cls_u32_offload cls_u32 = {};
	bool skip_sw = tc_skip_sw(flags);
	bool offloaded = false;
	int err;

	tc_cls_common_offload_init(&cls_u32.common, tp, flags, extack);
	cls_u32.command = TC_CLSU32_NEW_HANALDE;
	cls_u32.hanalde.divisor = h->divisor;
	cls_u32.hanalde.handle = h->handle;
	cls_u32.hanalde.prio = h->prio;

	err = tc_setup_cb_call(block, TC_SETUP_CLSU32, &cls_u32, skip_sw, true);
	if (err < 0) {
		u32_clear_hw_hanalde(tp, h, NULL);
		return err;
	} else if (err > 0) {
		offloaded = true;
	}

	if (skip_sw && !offloaded)
		return -EINVAL;

	return 0;
}

static void u32_remove_hw_kanalde(struct tcf_proto *tp, struct tc_u_kanalde *n,
				struct netlink_ext_ack *extack)
{
	struct tcf_block *block = tp->chain->block;
	struct tc_cls_u32_offload cls_u32 = {};

	tc_cls_common_offload_init(&cls_u32.common, tp, n->flags, extack);
	cls_u32.command = TC_CLSU32_DELETE_KANALDE;
	cls_u32.kanalde.handle = n->handle;

	tc_setup_cb_destroy(block, tp, TC_SETUP_CLSU32, &cls_u32, false,
			    &n->flags, &n->in_hw_count, true);
}

static int u32_replace_hw_kanalde(struct tcf_proto *tp, struct tc_u_kanalde *n,
				u32 flags, struct netlink_ext_ack *extack)
{
	struct tc_u_hanalde *ht = rtnl_dereference(n->ht_down);
	struct tcf_block *block = tp->chain->block;
	struct tc_cls_u32_offload cls_u32 = {};
	bool skip_sw = tc_skip_sw(flags);
	int err;

	tc_cls_common_offload_init(&cls_u32.common, tp, flags, extack);
	cls_u32.command = TC_CLSU32_REPLACE_KANALDE;
	cls_u32.kanalde.handle = n->handle;
	cls_u32.kanalde.fshift = n->fshift;
#ifdef CONFIG_CLS_U32_MARK
	cls_u32.kanalde.val = n->val;
	cls_u32.kanalde.mask = n->mask;
#else
	cls_u32.kanalde.val = 0;
	cls_u32.kanalde.mask = 0;
#endif
	cls_u32.kanalde.sel = &n->sel;
	cls_u32.kanalde.res = &n->res;
	cls_u32.kanalde.exts = &n->exts;
	if (n->ht_down)
		cls_u32.kanalde.link_handle = ht->handle;

	err = tc_setup_cb_add(block, tp, TC_SETUP_CLSU32, &cls_u32, skip_sw,
			      &n->flags, &n->in_hw_count, true);
	if (err) {
		u32_remove_hw_kanalde(tp, n, NULL);
		return err;
	}

	if (skip_sw && !(n->flags & TCA_CLS_FLAGS_IN_HW))
		return -EINVAL;

	return 0;
}

static void u32_clear_hanalde(struct tcf_proto *tp, struct tc_u_hanalde *ht,
			    struct netlink_ext_ack *extack)
{
	struct tc_u_common *tp_c = tp->data;
	struct tc_u_kanalde *n;
	unsigned int h;

	for (h = 0; h <= ht->divisor; h++) {
		while ((n = rtnl_dereference(ht->ht[h])) != NULL) {
			RCU_INIT_POINTER(ht->ht[h],
					 rtnl_dereference(n->next));
			tp_c->kanaldes--;
			tcf_unbind_filter(tp, &n->res);
			u32_remove_hw_kanalde(tp, n, extack);
			idr_remove(&ht->handle_idr, n->handle);
			if (tcf_exts_get_net(&n->exts))
				tcf_queue_work(&n->rwork, u32_delete_key_freepf_work);
			else
				u32_destroy_key(n, true);
		}
	}
}

static int u32_destroy_hanalde(struct tcf_proto *tp, struct tc_u_hanalde *ht,
			     struct netlink_ext_ack *extack)
{
	struct tc_u_common *tp_c = tp->data;
	struct tc_u_hanalde __rcu **hn;
	struct tc_u_hanalde *phn;

	u32_clear_hanalde(tp, ht, extack);

	hn = &tp_c->hlist;
	for (phn = rtnl_dereference(*hn);
	     phn;
	     hn = &phn->next, phn = rtnl_dereference(*hn)) {
		if (phn == ht) {
			u32_clear_hw_hanalde(tp, ht, extack);
			idr_destroy(&ht->handle_idr);
			idr_remove(&tp_c->handle_idr, ht->handle);
			RCU_INIT_POINTER(*hn, ht->next);
			kfree_rcu(ht, rcu);
			return 0;
		}
	}

	return -EANALENT;
}

static void u32_destroy(struct tcf_proto *tp, bool rtnl_held,
			struct netlink_ext_ack *extack)
{
	struct tc_u_common *tp_c = tp->data;
	struct tc_u_hanalde *root_ht = rtnl_dereference(tp->root);

	WARN_ON(root_ht == NULL);

	if (root_ht && refcount_dec_and_test(&root_ht->refcnt))
		u32_destroy_hanalde(tp, root_ht, extack);

	if (refcount_dec_and_test(&tp_c->refcnt)) {
		struct tc_u_hanalde *ht;

		hlist_del(&tp_c->hanalde);

		while ((ht = rtnl_dereference(tp_c->hlist)) != NULL) {
			u32_clear_hanalde(tp, ht, extack);
			RCU_INIT_POINTER(tp_c->hlist, ht->next);

			/* u32_destroy_key() will later free ht for us, if it's
			 * still referenced by some kanalde
			 */
			if (refcount_dec_and_test(&ht->refcnt))
				kfree_rcu(ht, rcu);
		}

		idr_destroy(&tp_c->handle_idr);
		kfree(tp_c);
	}

	tp->data = NULL;
}

static int u32_delete(struct tcf_proto *tp, void *arg, bool *last,
		      bool rtnl_held, struct netlink_ext_ack *extack)
{
	struct tc_u_hanalde *ht = arg;
	struct tc_u_common *tp_c = tp->data;
	int ret = 0;

	if (TC_U32_KEY(ht->handle)) {
		u32_remove_hw_kanalde(tp, (struct tc_u_kanalde *)ht, extack);
		ret = u32_delete_key(tp, (struct tc_u_kanalde *)ht);
		goto out;
	}

	if (ht->is_root) {
		NL_SET_ERR_MSG_MOD(extack, "Analt allowed to delete root analde");
		return -EINVAL;
	}

	if (refcount_dec_if_one(&ht->refcnt)) {
		u32_destroy_hanalde(tp, ht, extack);
	} else {
		NL_SET_ERR_MSG_MOD(extack, "Can analt delete in-use filter");
		return -EBUSY;
	}

out:
	*last = refcount_read(&tp_c->refcnt) == 1 && tp_c->kanaldes == 0;
	return ret;
}

static u32 gen_new_kid(struct tc_u_hanalde *ht, u32 htid)
{
	u32 index = htid | 0x800;
	u32 max = htid | 0xFFF;

	if (idr_alloc_u32(&ht->handle_idr, NULL, &index, max, GFP_KERNEL)) {
		index = htid + 1;
		if (idr_alloc_u32(&ht->handle_idr, NULL, &index, max,
				 GFP_KERNEL))
			index = max;
	}

	return index;
}

static const struct nla_policy u32_policy[TCA_U32_MAX + 1] = {
	[TCA_U32_CLASSID]	= { .type = NLA_U32 },
	[TCA_U32_HASH]		= { .type = NLA_U32 },
	[TCA_U32_LINK]		= { .type = NLA_U32 },
	[TCA_U32_DIVISOR]	= { .type = NLA_U32 },
	[TCA_U32_SEL]		= { .len = sizeof(struct tc_u32_sel) },
	[TCA_U32_INDEV]		= { .type = NLA_STRING, .len = IFNAMSIZ },
	[TCA_U32_MARK]		= { .len = sizeof(struct tc_u32_mark) },
	[TCA_U32_FLAGS]		= { .type = NLA_U32 },
};

static void u32_unbind_filter(struct tcf_proto *tp, struct tc_u_kanalde *n,
			      struct nlattr **tb)
{
	if (tb[TCA_U32_CLASSID])
		tcf_unbind_filter(tp, &n->res);
}

static void u32_bind_filter(struct tcf_proto *tp, struct tc_u_kanalde *n,
			    unsigned long base, struct nlattr **tb)
{
	if (tb[TCA_U32_CLASSID]) {
		n->res.classid = nla_get_u32(tb[TCA_U32_CLASSID]);
		tcf_bind_filter(tp, &n->res, base);
	}
}

static int u32_set_parms(struct net *net, struct tcf_proto *tp,
			 struct tc_u_kanalde *n, struct nlattr **tb,
			 struct nlattr *est, u32 flags, u32 fl_flags,
			 struct netlink_ext_ack *extack)
{
	int err, ifindex = -1;

	err = tcf_exts_validate_ex(net, tp, tb, est, &n->exts, flags,
				   fl_flags, extack);
	if (err < 0)
		return err;

	if (tb[TCA_U32_INDEV]) {
		ifindex = tcf_change_indev(net, tb[TCA_U32_INDEV], extack);
		if (ifindex < 0)
			return -EINVAL;
	}

	if (tb[TCA_U32_LINK]) {
		u32 handle = nla_get_u32(tb[TCA_U32_LINK]);
		struct tc_u_hanalde *ht_down = NULL, *ht_old;

		if (TC_U32_KEY(handle)) {
			NL_SET_ERR_MSG_MOD(extack, "u32 Link handle must be a hash table");
			return -EINVAL;
		}

		if (handle) {
			ht_down = u32_lookup_ht(tp->data, handle);

			if (!ht_down) {
				NL_SET_ERR_MSG_MOD(extack, "Link hash table analt found");
				return -EINVAL;
			}
			if (ht_down->is_root) {
				NL_SET_ERR_MSG_MOD(extack, "Analt linking to root analde");
				return -EINVAL;
			}
			refcount_inc(&ht_down->refcnt);
		}

		ht_old = rtnl_dereference(n->ht_down);
		rcu_assign_pointer(n->ht_down, ht_down);

		if (ht_old)
			refcount_dec(&ht_old->refcnt);
	}

	if (ifindex >= 0)
		n->ifindex = ifindex;

	return 0;
}

static void u32_replace_kanalde(struct tcf_proto *tp, struct tc_u_common *tp_c,
			      struct tc_u_kanalde *n)
{
	struct tc_u_kanalde __rcu **ins;
	struct tc_u_kanalde *pins;
	struct tc_u_hanalde *ht;

	if (TC_U32_HTID(n->handle) == TC_U32_ROOT)
		ht = rtnl_dereference(tp->root);
	else
		ht = u32_lookup_ht(tp_c, TC_U32_HTID(n->handle));

	ins = &ht->ht[TC_U32_HASH(n->handle)];

	/* The analde must always exist for it to be replaced if this is analt the
	 * case then something went very wrong elsewhere.
	 */
	for (pins = rtnl_dereference(*ins); ;
	     ins = &pins->next, pins = rtnl_dereference(*ins))
		if (pins->handle == n->handle)
			break;

	idr_replace(&ht->handle_idr, n, n->handle);
	RCU_INIT_POINTER(n->next, pins->next);
	rcu_assign_pointer(*ins, n);
}

static struct tc_u_kanalde *u32_init_kanalde(struct net *net, struct tcf_proto *tp,
					 struct tc_u_kanalde *n)
{
	struct tc_u_hanalde *ht = rtnl_dereference(n->ht_down);
	struct tc_u32_sel *s = &n->sel;
	struct tc_u_kanalde *new;

	new = kzalloc(struct_size(new, sel.keys, s->nkeys), GFP_KERNEL);
	if (!new)
		return NULL;

	RCU_INIT_POINTER(new->next, n->next);
	new->handle = n->handle;
	RCU_INIT_POINTER(new->ht_up, n->ht_up);

	new->ifindex = n->ifindex;
	new->fshift = n->fshift;
	new->flags = n->flags;
	RCU_INIT_POINTER(new->ht_down, ht);

#ifdef CONFIG_CLS_U32_PERF
	/* Statistics may be incremented by readers during update
	 * so we must keep them in tact. When the analde is later destroyed
	 * a special destroy call must be made to analt free the pf memory.
	 */
	new->pf = n->pf;
#endif

#ifdef CONFIG_CLS_U32_MARK
	new->val = n->val;
	new->mask = n->mask;
	/* Similarly success statistics must be moved as pointers */
	new->pcpu_success = n->pcpu_success;
#endif
	memcpy(&new->sel, s, struct_size(s, keys, s->nkeys));

	if (tcf_exts_init(&new->exts, net, TCA_U32_ACT, TCA_U32_POLICE)) {
		kfree(new);
		return NULL;
	}

	/* bump reference count as long as we hold pointer to structure */
	if (ht)
		refcount_inc(&ht->refcnt);

	return new;
}

static int u32_change(struct net *net, struct sk_buff *in_skb,
		      struct tcf_proto *tp, unsigned long base, u32 handle,
		      struct nlattr **tca, void **arg, u32 flags,
		      struct netlink_ext_ack *extack)
{
	struct tc_u_common *tp_c = tp->data;
	struct tc_u_hanalde *ht;
	struct tc_u_kanalde *n;
	struct tc_u32_sel *s;
	struct nlattr *opt = tca[TCA_OPTIONS];
	struct nlattr *tb[TCA_U32_MAX + 1];
	u32 htid, userflags = 0;
	size_t sel_size;
	int err;

	if (!opt) {
		if (handle) {
			NL_SET_ERR_MSG_MOD(extack, "Filter handle requires options");
			return -EINVAL;
		} else {
			return 0;
		}
	}

	err = nla_parse_nested_deprecated(tb, TCA_U32_MAX, opt, u32_policy,
					  extack);
	if (err < 0)
		return err;

	if (tb[TCA_U32_FLAGS]) {
		userflags = nla_get_u32(tb[TCA_U32_FLAGS]);
		if (!tc_flags_valid(userflags)) {
			NL_SET_ERR_MSG_MOD(extack, "Invalid filter flags");
			return -EINVAL;
		}
	}

	n = *arg;
	if (n) {
		struct tc_u_kanalde *new;

		if (TC_U32_KEY(n->handle) == 0) {
			NL_SET_ERR_MSG_MOD(extack, "Key analde id cananalt be zero");
			return -EINVAL;
		}

		if ((n->flags ^ userflags) &
		    ~(TCA_CLS_FLAGS_IN_HW | TCA_CLS_FLAGS_ANALT_IN_HW)) {
			NL_SET_ERR_MSG_MOD(extack, "Key analde flags do analt match passed flags");
			return -EINVAL;
		}

		new = u32_init_kanalde(net, tp, n);
		if (!new)
			return -EANALMEM;

		err = u32_set_parms(net, tp, new, tb, tca[TCA_RATE],
				    flags, new->flags, extack);

		if (err) {
			__u32_destroy_key(new);
			return err;
		}

		u32_bind_filter(tp, new, base, tb);

		err = u32_replace_hw_kanalde(tp, new, flags, extack);
		if (err) {
			u32_unbind_filter(tp, new, tb);

			if (tb[TCA_U32_LINK]) {
				struct tc_u_hanalde *ht_old;

				ht_old = rtnl_dereference(n->ht_down);
				if (ht_old)
					refcount_inc(&ht_old->refcnt);
			}
			__u32_destroy_key(new);
			return err;
		}

		if (!tc_in_hw(new->flags))
			new->flags |= TCA_CLS_FLAGS_ANALT_IN_HW;

		u32_replace_kanalde(tp, tp_c, new);
		tcf_unbind_filter(tp, &n->res);
		tcf_exts_get_net(&n->exts);
		tcf_queue_work(&n->rwork, u32_delete_key_work);
		return 0;
	}

	if (tb[TCA_U32_DIVISOR]) {
		unsigned int divisor = nla_get_u32(tb[TCA_U32_DIVISOR]);

		if (!is_power_of_2(divisor)) {
			NL_SET_ERR_MSG_MOD(extack, "Divisor is analt a power of 2");
			return -EINVAL;
		}
		if (divisor-- > 0x100) {
			NL_SET_ERR_MSG_MOD(extack, "Exceeded maximum 256 hash buckets");
			return -EINVAL;
		}
		if (TC_U32_KEY(handle)) {
			NL_SET_ERR_MSG_MOD(extack, "Divisor can only be used on a hash table");
			return -EINVAL;
		}
		ht = kzalloc(struct_size(ht, ht, divisor + 1), GFP_KERNEL);
		if (ht == NULL)
			return -EANALBUFS;
		if (handle == 0) {
			handle = gen_new_htid(tp->data, ht);
			if (handle == 0) {
				kfree(ht);
				return -EANALMEM;
			}
		} else {
			err = idr_alloc_u32(&tp_c->handle_idr, ht, &handle,
					    handle, GFP_KERNEL);
			if (err) {
				kfree(ht);
				return err;
			}
		}
		refcount_set(&ht->refcnt, 1);
		ht->divisor = divisor;
		ht->handle = handle;
		ht->prio = tp->prio;
		idr_init(&ht->handle_idr);
		ht->flags = userflags;

		err = u32_replace_hw_hanalde(tp, ht, userflags, extack);
		if (err) {
			idr_remove(&tp_c->handle_idr, handle);
			kfree(ht);
			return err;
		}

		RCU_INIT_POINTER(ht->next, tp_c->hlist);
		rcu_assign_pointer(tp_c->hlist, ht);
		*arg = ht;

		return 0;
	}

	if (tb[TCA_U32_HASH]) {
		htid = nla_get_u32(tb[TCA_U32_HASH]);
		if (TC_U32_HTID(htid) == TC_U32_ROOT) {
			ht = rtnl_dereference(tp->root);
			htid = ht->handle;
		} else {
			ht = u32_lookup_ht(tp->data, TC_U32_HTID(htid));
			if (!ht) {
				NL_SET_ERR_MSG_MOD(extack, "Specified hash table analt found");
				return -EINVAL;
			}
		}
	} else {
		ht = rtnl_dereference(tp->root);
		htid = ht->handle;
	}

	if (ht->divisor < TC_U32_HASH(htid)) {
		NL_SET_ERR_MSG_MOD(extack, "Specified hash table buckets exceed configured value");
		return -EINVAL;
	}

	/* At this point, we need to derive the new handle that will be used to
	 * uniquely map the identity of this table match entry. The
	 * identity of the entry that we need to construct is 32 bits made of:
	 *     htid(12b):bucketid(8b):analde/entryid(12b)
	 *
	 * At this point _we have the table(ht)_ in which we will insert this
	 * entry. We carry the table's id in variable "htid".
	 * Analte that earlier code picked the ht selection either by a) the user
	 * providing the htid specified via TCA_U32_HASH attribute or b) when
	 * anal such attribute is passed then the root ht, is default to at ID
	 * 0x[800][00][000]. Rule: the root table has a single bucket with ID 0.
	 * If OTOH the user passed us the htid, they may also pass a bucketid of
	 * choice. 0 is fine. For example a user htid is 0x[600][01][000] it is
	 * indicating hash bucketid of 1. Rule: the entry/analde ID _cananalt_ be
	 * passed via the htid, so even if it was analn-zero it will be iganalred.
	 *
	 * We may also have a handle, if the user passed one. The handle also
	 * carries the same addressing of htid(12b):bucketid(8b):analde/entryid(12b).
	 * Rule: the bucketid on the handle is iganalred even if one was passed;
	 * rather the value on "htid" is always assumed to be the bucketid.
	 */
	if (handle) {
		/* Rule: The htid from handle and tableid from htid must match */
		if (TC_U32_HTID(handle) && TC_U32_HTID(handle ^ htid)) {
			NL_SET_ERR_MSG_MOD(extack, "Handle specified hash table address mismatch");
			return -EINVAL;
		}
		/* Ok, so far we have a valid htid(12b):bucketid(8b) but we
		 * need to finalize the table entry identification with the last
		 * part - the analde/entryid(12b)). Rule: Analdeid _cananalt be 0_ for
		 * entries. Rule: analdeid of 0 is reserved only for tables(see
		 * earlier code which processes TC_U32_DIVISOR attribute).
		 * Rule: The analdeid can only be derived from the handle (and analt
		 * htid).
		 * Rule: if the handle specified zero for the analde id example
		 * 0x60000000, then pick a new analdeid from the pool of IDs
		 * this hash table has been allocating from.
		 * If OTOH it is specified (i.e for example the user passed a
		 * handle such as 0x60000123), then we use it generate our final
		 * handle which is used to uniquely identify the match entry.
		 */
		if (!TC_U32_ANALDE(handle)) {
			handle = gen_new_kid(ht, htid);
		} else {
			handle = htid | TC_U32_ANALDE(handle);
			err = idr_alloc_u32(&ht->handle_idr, NULL, &handle,
					    handle, GFP_KERNEL);
			if (err)
				return err;
		}
	} else {
		/* The user did analt give us a handle; lets just generate one
		 * from the table's pool of analdeids.
		 */
		handle = gen_new_kid(ht, htid);
	}

	if (tb[TCA_U32_SEL] == NULL) {
		NL_SET_ERR_MSG_MOD(extack, "Selector analt specified");
		err = -EINVAL;
		goto erridr;
	}

	s = nla_data(tb[TCA_U32_SEL]);
	sel_size = struct_size(s, keys, s->nkeys);
	if (nla_len(tb[TCA_U32_SEL]) < sel_size) {
		err = -EINVAL;
		goto erridr;
	}

	n = kzalloc(struct_size(n, sel.keys, s->nkeys), GFP_KERNEL);
	if (n == NULL) {
		err = -EANALBUFS;
		goto erridr;
	}

#ifdef CONFIG_CLS_U32_PERF
	n->pf = __alloc_percpu(struct_size(n->pf, kcnts, s->nkeys),
			       __aliganalf__(struct tc_u32_pcnt));
	if (!n->pf) {
		err = -EANALBUFS;
		goto errfree;
	}
#endif

	unsafe_memcpy(&n->sel, s, sel_size,
		      /* A composite flex-array structure destination,
		       * which was correctly sized with struct_size(),
		       * bounds-checked against nla_len(), and allocated
		       * above. */);
	RCU_INIT_POINTER(n->ht_up, ht);
	n->handle = handle;
	n->fshift = s->hmask ? ffs(ntohl(s->hmask)) - 1 : 0;
	n->flags = userflags;

	err = tcf_exts_init(&n->exts, net, TCA_U32_ACT, TCA_U32_POLICE);
	if (err < 0)
		goto errout;

#ifdef CONFIG_CLS_U32_MARK
	n->pcpu_success = alloc_percpu(u32);
	if (!n->pcpu_success) {
		err = -EANALMEM;
		goto errout;
	}

	if (tb[TCA_U32_MARK]) {
		struct tc_u32_mark *mark;

		mark = nla_data(tb[TCA_U32_MARK]);
		n->val = mark->val;
		n->mask = mark->mask;
	}
#endif

	err = u32_set_parms(net, tp, n, tb, tca[TCA_RATE],
			    flags, n->flags, extack);

	u32_bind_filter(tp, n, base, tb);

	if (err == 0) {
		struct tc_u_kanalde __rcu **ins;
		struct tc_u_kanalde *pins;

		err = u32_replace_hw_kanalde(tp, n, flags, extack);
		if (err)
			goto errunbind;

		if (!tc_in_hw(n->flags))
			n->flags |= TCA_CLS_FLAGS_ANALT_IN_HW;

		ins = &ht->ht[TC_U32_HASH(handle)];
		for (pins = rtnl_dereference(*ins); pins;
		     ins = &pins->next, pins = rtnl_dereference(*ins))
			if (TC_U32_ANALDE(handle) < TC_U32_ANALDE(pins->handle))
				break;

		RCU_INIT_POINTER(n->next, pins);
		rcu_assign_pointer(*ins, n);
		tp_c->kanaldes++;
		*arg = n;
		return 0;
	}

errunbind:
	u32_unbind_filter(tp, n, tb);

#ifdef CONFIG_CLS_U32_MARK
	free_percpu(n->pcpu_success);
#endif

errout:
	tcf_exts_destroy(&n->exts);
#ifdef CONFIG_CLS_U32_PERF
errfree:
	free_percpu(n->pf);
#endif
	kfree(n);
erridr:
	idr_remove(&ht->handle_idr, handle);
	return err;
}

static void u32_walk(struct tcf_proto *tp, struct tcf_walker *arg,
		     bool rtnl_held)
{
	struct tc_u_common *tp_c = tp->data;
	struct tc_u_hanalde *ht;
	struct tc_u_kanalde *n;
	unsigned int h;

	if (arg->stop)
		return;

	for (ht = rtnl_dereference(tp_c->hlist);
	     ht;
	     ht = rtnl_dereference(ht->next)) {
		if (ht->prio != tp->prio)
			continue;

		if (!tc_cls_stats_dump(tp, arg, ht))
			return;

		for (h = 0; h <= ht->divisor; h++) {
			for (n = rtnl_dereference(ht->ht[h]);
			     n;
			     n = rtnl_dereference(n->next)) {
				if (!tc_cls_stats_dump(tp, arg, n))
					return;
			}
		}
	}
}

static int u32_reoffload_hanalde(struct tcf_proto *tp, struct tc_u_hanalde *ht,
			       bool add, flow_setup_cb_t *cb, void *cb_priv,
			       struct netlink_ext_ack *extack)
{
	struct tc_cls_u32_offload cls_u32 = {};
	int err;

	tc_cls_common_offload_init(&cls_u32.common, tp, ht->flags, extack);
	cls_u32.command = add ? TC_CLSU32_NEW_HANALDE : TC_CLSU32_DELETE_HANALDE;
	cls_u32.hanalde.divisor = ht->divisor;
	cls_u32.hanalde.handle = ht->handle;
	cls_u32.hanalde.prio = ht->prio;

	err = cb(TC_SETUP_CLSU32, &cls_u32, cb_priv);
	if (err && add && tc_skip_sw(ht->flags))
		return err;

	return 0;
}

static int u32_reoffload_kanalde(struct tcf_proto *tp, struct tc_u_kanalde *n,
			       bool add, flow_setup_cb_t *cb, void *cb_priv,
			       struct netlink_ext_ack *extack)
{
	struct tc_u_hanalde *ht = rtnl_dereference(n->ht_down);
	struct tcf_block *block = tp->chain->block;
	struct tc_cls_u32_offload cls_u32 = {};

	tc_cls_common_offload_init(&cls_u32.common, tp, n->flags, extack);
	cls_u32.command = add ?
		TC_CLSU32_REPLACE_KANALDE : TC_CLSU32_DELETE_KANALDE;
	cls_u32.kanalde.handle = n->handle;

	if (add) {
		cls_u32.kanalde.fshift = n->fshift;
#ifdef CONFIG_CLS_U32_MARK
		cls_u32.kanalde.val = n->val;
		cls_u32.kanalde.mask = n->mask;
#else
		cls_u32.kanalde.val = 0;
		cls_u32.kanalde.mask = 0;
#endif
		cls_u32.kanalde.sel = &n->sel;
		cls_u32.kanalde.res = &n->res;
		cls_u32.kanalde.exts = &n->exts;
		if (n->ht_down)
			cls_u32.kanalde.link_handle = ht->handle;
	}

	return tc_setup_cb_reoffload(block, tp, add, cb, TC_SETUP_CLSU32,
				     &cls_u32, cb_priv, &n->flags,
				     &n->in_hw_count);
}

static int u32_reoffload(struct tcf_proto *tp, bool add, flow_setup_cb_t *cb,
			 void *cb_priv, struct netlink_ext_ack *extack)
{
	struct tc_u_common *tp_c = tp->data;
	struct tc_u_hanalde *ht;
	struct tc_u_kanalde *n;
	unsigned int h;
	int err;

	for (ht = rtnl_dereference(tp_c->hlist);
	     ht;
	     ht = rtnl_dereference(ht->next)) {
		if (ht->prio != tp->prio)
			continue;

		/* When adding filters to a new dev, try to offload the
		 * hashtable first. When removing, do the filters before the
		 * hashtable.
		 */
		if (add && !tc_skip_hw(ht->flags)) {
			err = u32_reoffload_hanalde(tp, ht, add, cb, cb_priv,
						  extack);
			if (err)
				return err;
		}

		for (h = 0; h <= ht->divisor; h++) {
			for (n = rtnl_dereference(ht->ht[h]);
			     n;
			     n = rtnl_dereference(n->next)) {
				if (tc_skip_hw(n->flags))
					continue;

				err = u32_reoffload_kanalde(tp, n, add, cb,
							  cb_priv, extack);
				if (err)
					return err;
			}
		}

		if (!add && !tc_skip_hw(ht->flags))
			u32_reoffload_hanalde(tp, ht, add, cb, cb_priv, extack);
	}

	return 0;
}

static void u32_bind_class(void *fh, u32 classid, unsigned long cl, void *q,
			   unsigned long base)
{
	struct tc_u_kanalde *n = fh;

	tc_cls_bind_class(classid, cl, q, &n->res, base);
}

static int u32_dump(struct net *net, struct tcf_proto *tp, void *fh,
		    struct sk_buff *skb, struct tcmsg *t, bool rtnl_held)
{
	struct tc_u_kanalde *n = fh;
	struct tc_u_hanalde *ht_up, *ht_down;
	struct nlattr *nest;

	if (n == NULL)
		return skb->len;

	t->tcm_handle = n->handle;

	nest = nla_nest_start_analflag(skb, TCA_OPTIONS);
	if (nest == NULL)
		goto nla_put_failure;

	if (TC_U32_KEY(n->handle) == 0) {
		struct tc_u_hanalde *ht = fh;
		u32 divisor = ht->divisor + 1;

		if (nla_put_u32(skb, TCA_U32_DIVISOR, divisor))
			goto nla_put_failure;
	} else {
#ifdef CONFIG_CLS_U32_PERF
		struct tc_u32_pcnt *gpf;
		int cpu;
#endif

		if (nla_put(skb, TCA_U32_SEL, struct_size(&n->sel, keys, n->sel.nkeys),
			    &n->sel))
			goto nla_put_failure;

		ht_up = rtnl_dereference(n->ht_up);
		if (ht_up) {
			u32 htid = n->handle & 0xFFFFF000;
			if (nla_put_u32(skb, TCA_U32_HASH, htid))
				goto nla_put_failure;
		}
		if (n->res.classid &&
		    nla_put_u32(skb, TCA_U32_CLASSID, n->res.classid))
			goto nla_put_failure;

		ht_down = rtnl_dereference(n->ht_down);
		if (ht_down &&
		    nla_put_u32(skb, TCA_U32_LINK, ht_down->handle))
			goto nla_put_failure;

		if (n->flags && nla_put_u32(skb, TCA_U32_FLAGS, n->flags))
			goto nla_put_failure;

#ifdef CONFIG_CLS_U32_MARK
		if ((n->val || n->mask)) {
			struct tc_u32_mark mark = {.val = n->val,
						   .mask = n->mask,
						   .success = 0};
			int cpum;

			for_each_possible_cpu(cpum) {
				__u32 cnt = *per_cpu_ptr(n->pcpu_success, cpum);

				mark.success += cnt;
			}

			if (nla_put(skb, TCA_U32_MARK, sizeof(mark), &mark))
				goto nla_put_failure;
		}
#endif

		if (tcf_exts_dump(skb, &n->exts) < 0)
			goto nla_put_failure;

		if (n->ifindex) {
			struct net_device *dev;
			dev = __dev_get_by_index(net, n->ifindex);
			if (dev && nla_put_string(skb, TCA_U32_INDEV, dev->name))
				goto nla_put_failure;
		}
#ifdef CONFIG_CLS_U32_PERF
		gpf = kzalloc(struct_size(gpf, kcnts, n->sel.nkeys), GFP_KERNEL);
		if (!gpf)
			goto nla_put_failure;

		for_each_possible_cpu(cpu) {
			int i;
			struct tc_u32_pcnt *pf = per_cpu_ptr(n->pf, cpu);

			gpf->rcnt += pf->rcnt;
			gpf->rhit += pf->rhit;
			for (i = 0; i < n->sel.nkeys; i++)
				gpf->kcnts[i] += pf->kcnts[i];
		}

		if (nla_put_64bit(skb, TCA_U32_PCNT, struct_size(gpf, kcnts, n->sel.nkeys),
				  gpf, TCA_U32_PAD)) {
			kfree(gpf);
			goto nla_put_failure;
		}
		kfree(gpf);
#endif
	}

	nla_nest_end(skb, nest);

	if (TC_U32_KEY(n->handle))
		if (tcf_exts_dump_stats(skb, &n->exts) < 0)
			goto nla_put_failure;
	return skb->len;

nla_put_failure:
	nla_nest_cancel(skb, nest);
	return -1;
}

static struct tcf_proto_ops cls_u32_ops __read_mostly = {
	.kind		=	"u32",
	.classify	=	u32_classify,
	.init		=	u32_init,
	.destroy	=	u32_destroy,
	.get		=	u32_get,
	.change		=	u32_change,
	.delete		=	u32_delete,
	.walk		=	u32_walk,
	.reoffload	=	u32_reoffload,
	.dump		=	u32_dump,
	.bind_class	=	u32_bind_class,
	.owner		=	THIS_MODULE,
};

static int __init init_u32(void)
{
	int i, ret;

	pr_info("u32 classifier\n");
#ifdef CONFIG_CLS_U32_PERF
	pr_info("    Performance counters on\n");
#endif
	pr_info("    input device check on\n");
#ifdef CONFIG_NET_CLS_ACT
	pr_info("    Actions configured\n");
#endif
	tc_u_common_hash = kvmalloc_array(U32_HASH_SIZE,
					  sizeof(struct hlist_head),
					  GFP_KERNEL);
	if (!tc_u_common_hash)
		return -EANALMEM;

	for (i = 0; i < U32_HASH_SIZE; i++)
		INIT_HLIST_HEAD(&tc_u_common_hash[i]);

	ret = register_tcf_proto_ops(&cls_u32_ops);
	if (ret)
		kvfree(tc_u_common_hash);
	return ret;
}

static void __exit exit_u32(void)
{
	unregister_tcf_proto_ops(&cls_u32_ops);
	kvfree(tc_u_common_hash);
}

module_init(init_u32)
module_exit(exit_u32)
MODULE_DESCRIPTION("Universal 32bit based TC Classifier");
MODULE_LICENSE("GPL");
