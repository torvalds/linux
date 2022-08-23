// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * net/sched/cls_route.c	ROUTE4 classifier.
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/skbuff.h>
#include <net/dst.h>
#include <net/route.h>
#include <net/netlink.h>
#include <net/act_api.h>
#include <net/pkt_cls.h>

/*
 * 1. For now we assume that route tags < 256.
 *    It allows to use direct table lookups, instead of hash tables.
 * 2. For now we assume that "from TAG" and "fromdev DEV" statements
 *    are mutually  exclusive.
 * 3. "to TAG from ANY" has higher priority, than "to ANY from XXX"
 */
struct route4_fastmap {
	struct route4_filter		*filter;
	u32				id;
	int				iif;
};

struct route4_head {
	struct route4_fastmap		fastmap[16];
	struct route4_bucket __rcu	*table[256 + 1];
	struct rcu_head			rcu;
};

struct route4_bucket {
	/* 16 FROM buckets + 16 IIF buckets + 1 wildcard bucket */
	struct route4_filter __rcu	*ht[16 + 16 + 1];
	struct rcu_head			rcu;
};

struct route4_filter {
	struct route4_filter __rcu	*next;
	u32			id;
	int			iif;

	struct tcf_result	res;
	struct tcf_exts		exts;
	u32			handle;
	struct route4_bucket	*bkt;
	struct tcf_proto	*tp;
	struct rcu_work		rwork;
};

#define ROUTE4_FAILURE ((struct route4_filter *)(-1L))

static inline int route4_fastmap_hash(u32 id, int iif)
{
	return id & 0xF;
}

static DEFINE_SPINLOCK(fastmap_lock);
static void
route4_reset_fastmap(struct route4_head *head)
{
	spin_lock_bh(&fastmap_lock);
	memset(head->fastmap, 0, sizeof(head->fastmap));
	spin_unlock_bh(&fastmap_lock);
}

static void
route4_set_fastmap(struct route4_head *head, u32 id, int iif,
		   struct route4_filter *f)
{
	int h = route4_fastmap_hash(id, iif);

	/* fastmap updates must look atomic to aling id, iff, filter */
	spin_lock_bh(&fastmap_lock);
	head->fastmap[h].id = id;
	head->fastmap[h].iif = iif;
	head->fastmap[h].filter = f;
	spin_unlock_bh(&fastmap_lock);
}

static inline int route4_hash_to(u32 id)
{
	return id & 0xFF;
}

static inline int route4_hash_from(u32 id)
{
	return (id >> 16) & 0xF;
}

static inline int route4_hash_iif(int iif)
{
	return 16 + ((iif >> 16) & 0xF);
}

static inline int route4_hash_wild(void)
{
	return 32;
}

#define ROUTE4_APPLY_RESULT()					\
{								\
	*res = f->res;						\
	if (tcf_exts_has_actions(&f->exts)) {			\
		int r = tcf_exts_exec(skb, &f->exts, res);	\
		if (r < 0) {					\
			dont_cache = 1;				\
			continue;				\
		}						\
		return r;					\
	} else if (!dont_cache)					\
		route4_set_fastmap(head, id, iif, f);		\
	return 0;						\
}

static int route4_classify(struct sk_buff *skb, const struct tcf_proto *tp,
			   struct tcf_result *res)
{
	struct route4_head *head = rcu_dereference_bh(tp->root);
	struct dst_entry *dst;
	struct route4_bucket *b;
	struct route4_filter *f;
	u32 id, h;
	int iif, dont_cache = 0;

	dst = skb_dst(skb);
	if (!dst)
		goto failure;

	id = dst->tclassid;

	iif = inet_iif(skb);

	h = route4_fastmap_hash(id, iif);

	spin_lock(&fastmap_lock);
	if (id == head->fastmap[h].id &&
	    iif == head->fastmap[h].iif &&
	    (f = head->fastmap[h].filter) != NULL) {
		if (f == ROUTE4_FAILURE) {
			spin_unlock(&fastmap_lock);
			goto failure;
		}

		*res = f->res;
		spin_unlock(&fastmap_lock);
		return 0;
	}
	spin_unlock(&fastmap_lock);

	h = route4_hash_to(id);

restart:
	b = rcu_dereference_bh(head->table[h]);
	if (b) {
		for (f = rcu_dereference_bh(b->ht[route4_hash_from(id)]);
		     f;
		     f = rcu_dereference_bh(f->next))
			if (f->id == id)
				ROUTE4_APPLY_RESULT();

		for (f = rcu_dereference_bh(b->ht[route4_hash_iif(iif)]);
		     f;
		     f = rcu_dereference_bh(f->next))
			if (f->iif == iif)
				ROUTE4_APPLY_RESULT();

		for (f = rcu_dereference_bh(b->ht[route4_hash_wild()]);
		     f;
		     f = rcu_dereference_bh(f->next))
			ROUTE4_APPLY_RESULT();
	}
	if (h < 256) {
		h = 256;
		id &= ~0xFFFF;
		goto restart;
	}

	if (!dont_cache)
		route4_set_fastmap(head, id, iif, ROUTE4_FAILURE);
failure:
	return -1;
}

static inline u32 to_hash(u32 id)
{
	u32 h = id & 0xFF;

	if (id & 0x8000)
		h += 256;
	return h;
}

static inline u32 from_hash(u32 id)
{
	id &= 0xFFFF;
	if (id == 0xFFFF)
		return 32;
	if (!(id & 0x8000)) {
		if (id > 255)
			return 256;
		return id & 0xF;
	}
	return 16 + (id & 0xF);
}

static void *route4_get(struct tcf_proto *tp, u32 handle)
{
	struct route4_head *head = rtnl_dereference(tp->root);
	struct route4_bucket *b;
	struct route4_filter *f;
	unsigned int h1, h2;

	h1 = to_hash(handle);
	if (h1 > 256)
		return NULL;

	h2 = from_hash(handle >> 16);
	if (h2 > 32)
		return NULL;

	b = rtnl_dereference(head->table[h1]);
	if (b) {
		for (f = rtnl_dereference(b->ht[h2]);
		     f;
		     f = rtnl_dereference(f->next))
			if (f->handle == handle)
				return f;
	}
	return NULL;
}

static int route4_init(struct tcf_proto *tp)
{
	struct route4_head *head;

	head = kzalloc(sizeof(struct route4_head), GFP_KERNEL);
	if (head == NULL)
		return -ENOBUFS;

	rcu_assign_pointer(tp->root, head);
	return 0;
}

static void __route4_delete_filter(struct route4_filter *f)
{
	tcf_exts_destroy(&f->exts);
	tcf_exts_put_net(&f->exts);
	kfree(f);
}

static void route4_delete_filter_work(struct work_struct *work)
{
	struct route4_filter *f = container_of(to_rcu_work(work),
					       struct route4_filter,
					       rwork);
	rtnl_lock();
	__route4_delete_filter(f);
	rtnl_unlock();
}

static void route4_queue_work(struct route4_filter *f)
{
	tcf_queue_work(&f->rwork, route4_delete_filter_work);
}

static void route4_destroy(struct tcf_proto *tp, bool rtnl_held,
			   struct netlink_ext_ack *extack)
{
	struct route4_head *head = rtnl_dereference(tp->root);
	int h1, h2;

	if (head == NULL)
		return;

	for (h1 = 0; h1 <= 256; h1++) {
		struct route4_bucket *b;

		b = rtnl_dereference(head->table[h1]);
		if (b) {
			for (h2 = 0; h2 <= 32; h2++) {
				struct route4_filter *f;

				while ((f = rtnl_dereference(b->ht[h2])) != NULL) {
					struct route4_filter *next;

					next = rtnl_dereference(f->next);
					RCU_INIT_POINTER(b->ht[h2], next);
					tcf_unbind_filter(tp, &f->res);
					if (tcf_exts_get_net(&f->exts))
						route4_queue_work(f);
					else
						__route4_delete_filter(f);
				}
			}
			RCU_INIT_POINTER(head->table[h1], NULL);
			kfree_rcu(b, rcu);
		}
	}
	kfree_rcu(head, rcu);
}

static int route4_delete(struct tcf_proto *tp, void *arg, bool *last,
			 bool rtnl_held, struct netlink_ext_ack *extack)
{
	struct route4_head *head = rtnl_dereference(tp->root);
	struct route4_filter *f = arg;
	struct route4_filter __rcu **fp;
	struct route4_filter *nf;
	struct route4_bucket *b;
	unsigned int h = 0;
	int i, h1;

	if (!head || !f)
		return -EINVAL;

	h = f->handle;
	b = f->bkt;

	fp = &b->ht[from_hash(h >> 16)];
	for (nf = rtnl_dereference(*fp); nf;
	     fp = &nf->next, nf = rtnl_dereference(*fp)) {
		if (nf == f) {
			/* unlink it */
			RCU_INIT_POINTER(*fp, rtnl_dereference(f->next));

			/* Remove any fastmap lookups that might ref filter
			 * notice we unlink'd the filter so we can't get it
			 * back in the fastmap.
			 */
			route4_reset_fastmap(head);

			/* Delete it */
			tcf_unbind_filter(tp, &f->res);
			tcf_exts_get_net(&f->exts);
			tcf_queue_work(&f->rwork, route4_delete_filter_work);

			/* Strip RTNL protected tree */
			for (i = 0; i <= 32; i++) {
				struct route4_filter *rt;

				rt = rtnl_dereference(b->ht[i]);
				if (rt)
					goto out;
			}

			/* OK, session has no flows */
			RCU_INIT_POINTER(head->table[to_hash(h)], NULL);
			kfree_rcu(b, rcu);
			break;
		}
	}

out:
	*last = true;
	for (h1 = 0; h1 <= 256; h1++) {
		if (rcu_access_pointer(head->table[h1])) {
			*last = false;
			break;
		}
	}

	return 0;
}

static const struct nla_policy route4_policy[TCA_ROUTE4_MAX + 1] = {
	[TCA_ROUTE4_CLASSID]	= { .type = NLA_U32 },
	[TCA_ROUTE4_TO]		= { .type = NLA_U32 },
	[TCA_ROUTE4_FROM]	= { .type = NLA_U32 },
	[TCA_ROUTE4_IIF]	= { .type = NLA_U32 },
};

static int route4_set_parms(struct net *net, struct tcf_proto *tp,
			    unsigned long base, struct route4_filter *f,
			    u32 handle, struct route4_head *head,
			    struct nlattr **tb, struct nlattr *est, int new,
			    u32 flags, struct netlink_ext_ack *extack)
{
	u32 id = 0, to = 0, nhandle = 0x8000;
	struct route4_filter *fp;
	unsigned int h1;
	struct route4_bucket *b;
	int err;

	err = tcf_exts_validate(net, tp, tb, est, &f->exts, flags, extack);
	if (err < 0)
		return err;

	if (tb[TCA_ROUTE4_TO]) {
		if (new && handle & 0x8000)
			return -EINVAL;
		to = nla_get_u32(tb[TCA_ROUTE4_TO]);
		if (to > 0xFF)
			return -EINVAL;
		nhandle = to;
	}

	if (tb[TCA_ROUTE4_FROM]) {
		if (tb[TCA_ROUTE4_IIF])
			return -EINVAL;
		id = nla_get_u32(tb[TCA_ROUTE4_FROM]);
		if (id > 0xFF)
			return -EINVAL;
		nhandle |= id << 16;
	} else if (tb[TCA_ROUTE4_IIF]) {
		id = nla_get_u32(tb[TCA_ROUTE4_IIF]);
		if (id > 0x7FFF)
			return -EINVAL;
		nhandle |= (id | 0x8000) << 16;
	} else
		nhandle |= 0xFFFF << 16;

	if (handle && new) {
		nhandle |= handle & 0x7F00;
		if (nhandle != handle)
			return -EINVAL;
	}

	h1 = to_hash(nhandle);
	b = rtnl_dereference(head->table[h1]);
	if (!b) {
		b = kzalloc(sizeof(struct route4_bucket), GFP_KERNEL);
		if (b == NULL)
			return -ENOBUFS;

		rcu_assign_pointer(head->table[h1], b);
	} else {
		unsigned int h2 = from_hash(nhandle >> 16);

		for (fp = rtnl_dereference(b->ht[h2]);
		     fp;
		     fp = rtnl_dereference(fp->next))
			if (fp->handle == f->handle)
				return -EEXIST;
	}

	if (tb[TCA_ROUTE4_TO])
		f->id = to;

	if (tb[TCA_ROUTE4_FROM])
		f->id = to | id<<16;
	else if (tb[TCA_ROUTE4_IIF])
		f->iif = id;

	f->handle = nhandle;
	f->bkt = b;
	f->tp = tp;

	if (tb[TCA_ROUTE4_CLASSID]) {
		f->res.classid = nla_get_u32(tb[TCA_ROUTE4_CLASSID]);
		tcf_bind_filter(tp, &f->res, base);
	}

	return 0;
}

static int route4_change(struct net *net, struct sk_buff *in_skb,
			 struct tcf_proto *tp, unsigned long base, u32 handle,
			 struct nlattr **tca, void **arg, u32 flags,
			 struct netlink_ext_ack *extack)
{
	struct route4_head *head = rtnl_dereference(tp->root);
	struct route4_filter __rcu **fp;
	struct route4_filter *fold, *f1, *pfp, *f = NULL;
	struct route4_bucket *b;
	struct nlattr *opt = tca[TCA_OPTIONS];
	struct nlattr *tb[TCA_ROUTE4_MAX + 1];
	unsigned int h, th;
	int err;
	bool new = true;

	if (opt == NULL)
		return handle ? -EINVAL : 0;

	err = nla_parse_nested_deprecated(tb, TCA_ROUTE4_MAX, opt,
					  route4_policy, NULL);
	if (err < 0)
		return err;

	fold = *arg;
	if (fold && handle && fold->handle != handle)
			return -EINVAL;

	err = -ENOBUFS;
	f = kzalloc(sizeof(struct route4_filter), GFP_KERNEL);
	if (!f)
		goto errout;

	err = tcf_exts_init(&f->exts, net, TCA_ROUTE4_ACT, TCA_ROUTE4_POLICE);
	if (err < 0)
		goto errout;

	if (fold) {
		f->id = fold->id;
		f->iif = fold->iif;
		f->res = fold->res;
		f->handle = fold->handle;

		f->tp = fold->tp;
		f->bkt = fold->bkt;
		new = false;
	}

	err = route4_set_parms(net, tp, base, f, handle, head, tb,
			       tca[TCA_RATE], new, flags, extack);
	if (err < 0)
		goto errout;

	h = from_hash(f->handle >> 16);
	fp = &f->bkt->ht[h];
	for (pfp = rtnl_dereference(*fp);
	     (f1 = rtnl_dereference(*fp)) != NULL;
	     fp = &f1->next)
		if (f->handle < f1->handle)
			break;

	tcf_block_netif_keep_dst(tp->chain->block);
	rcu_assign_pointer(f->next, f1);
	rcu_assign_pointer(*fp, f);

	if (fold) {
		th = to_hash(fold->handle);
		h = from_hash(fold->handle >> 16);
		b = rtnl_dereference(head->table[th]);
		if (b) {
			fp = &b->ht[h];
			for (pfp = rtnl_dereference(*fp); pfp;
			     fp = &pfp->next, pfp = rtnl_dereference(*fp)) {
				if (pfp == fold) {
					rcu_assign_pointer(*fp, fold->next);
					break;
				}
			}
		}
	}

	route4_reset_fastmap(head);
	*arg = f;
	if (fold) {
		tcf_unbind_filter(tp, &fold->res);
		tcf_exts_get_net(&fold->exts);
		tcf_queue_work(&fold->rwork, route4_delete_filter_work);
	}
	return 0;

errout:
	if (f)
		tcf_exts_destroy(&f->exts);
	kfree(f);
	return err;
}

static void route4_walk(struct tcf_proto *tp, struct tcf_walker *arg,
			bool rtnl_held)
{
	struct route4_head *head = rtnl_dereference(tp->root);
	unsigned int h, h1;

	if (head == NULL || arg->stop)
		return;

	for (h = 0; h <= 256; h++) {
		struct route4_bucket *b = rtnl_dereference(head->table[h]);

		if (b) {
			for (h1 = 0; h1 <= 32; h1++) {
				struct route4_filter *f;

				for (f = rtnl_dereference(b->ht[h1]);
				     f;
				     f = rtnl_dereference(f->next)) {
					if (arg->count < arg->skip) {
						arg->count++;
						continue;
					}
					if (arg->fn(tp, f, arg) < 0) {
						arg->stop = 1;
						return;
					}
					arg->count++;
				}
			}
		}
	}
}

static int route4_dump(struct net *net, struct tcf_proto *tp, void *fh,
		       struct sk_buff *skb, struct tcmsg *t, bool rtnl_held)
{
	struct route4_filter *f = fh;
	struct nlattr *nest;
	u32 id;

	if (f == NULL)
		return skb->len;

	t->tcm_handle = f->handle;

	nest = nla_nest_start_noflag(skb, TCA_OPTIONS);
	if (nest == NULL)
		goto nla_put_failure;

	if (!(f->handle & 0x8000)) {
		id = f->id & 0xFF;
		if (nla_put_u32(skb, TCA_ROUTE4_TO, id))
			goto nla_put_failure;
	}
	if (f->handle & 0x80000000) {
		if ((f->handle >> 16) != 0xFFFF &&
		    nla_put_u32(skb, TCA_ROUTE4_IIF, f->iif))
			goto nla_put_failure;
	} else {
		id = f->id >> 16;
		if (nla_put_u32(skb, TCA_ROUTE4_FROM, id))
			goto nla_put_failure;
	}
	if (f->res.classid &&
	    nla_put_u32(skb, TCA_ROUTE4_CLASSID, f->res.classid))
		goto nla_put_failure;

	if (tcf_exts_dump(skb, &f->exts) < 0)
		goto nla_put_failure;

	nla_nest_end(skb, nest);

	if (tcf_exts_dump_stats(skb, &f->exts) < 0)
		goto nla_put_failure;

	return skb->len;

nla_put_failure:
	nla_nest_cancel(skb, nest);
	return -1;
}

static void route4_bind_class(void *fh, u32 classid, unsigned long cl, void *q,
			      unsigned long base)
{
	struct route4_filter *f = fh;

	if (f && f->res.classid == classid) {
		if (cl)
			__tcf_bind_filter(q, &f->res, base);
		else
			__tcf_unbind_filter(q, &f->res);
	}
}

static struct tcf_proto_ops cls_route4_ops __read_mostly = {
	.kind		=	"route",
	.classify	=	route4_classify,
	.init		=	route4_init,
	.destroy	=	route4_destroy,
	.get		=	route4_get,
	.change		=	route4_change,
	.delete		=	route4_delete,
	.walk		=	route4_walk,
	.dump		=	route4_dump,
	.bind_class	=	route4_bind_class,
	.owner		=	THIS_MODULE,
};

static int __init init_route4(void)
{
	return register_tcf_proto_ops(&cls_route4_ops);
}

static void __exit exit_route4(void)
{
	unregister_tcf_proto_ops(&cls_route4_ops);
}

module_init(init_route4)
module_exit(exit_route4)
MODULE_LICENSE("GPL");
