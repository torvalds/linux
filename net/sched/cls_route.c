/*
 * net/sched/cls_route.c	ROUTE4 classifier.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 */

#include <linux/module.h>
#include <linux/config.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <linux/bitops.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/in.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/if_ether.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/notifier.h>
#include <net/ip.h>
#include <net/route.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/act_api.h>
#include <net/pkt_cls.h>

/*
   1. For now we assume that route tags < 256.
      It allows to use direct table lookups, instead of hash tables.
   2. For now we assume that "from TAG" and "fromdev DEV" statements
      are mutually  exclusive.
   3. "to TAG from ANY" has higher priority, than "to ANY from XXX"
 */

struct route4_fastmap
{
	struct route4_filter	*filter;
	u32			id;
	int			iif;
};

struct route4_head
{
	struct route4_fastmap	fastmap[16];
	struct route4_bucket	*table[256+1];
};

struct route4_bucket
{
	/* 16 FROM buckets + 16 IIF buckets + 1 wildcard bucket */
	struct route4_filter	*ht[16+16+1];
};

struct route4_filter
{
	struct route4_filter	*next;
	u32			id;
	int			iif;

	struct tcf_result	res;
	struct tcf_exts		exts;
	u32			handle;
	struct route4_bucket	*bkt;
};

#define ROUTE4_FAILURE ((struct route4_filter*)(-1L))

static struct tcf_ext_map route_ext_map = {
	.police = TCA_ROUTE4_POLICE,
	.action = TCA_ROUTE4_ACT
};

static __inline__ int route4_fastmap_hash(u32 id, int iif)
{
	return id&0xF;
}

static inline
void route4_reset_fastmap(struct net_device *dev, struct route4_head *head, u32 id)
{
	spin_lock_bh(&dev->queue_lock);
	memset(head->fastmap, 0, sizeof(head->fastmap));
	spin_unlock_bh(&dev->queue_lock);
}

static void __inline__
route4_set_fastmap(struct route4_head *head, u32 id, int iif,
		   struct route4_filter *f)
{
	int h = route4_fastmap_hash(id, iif);
	head->fastmap[h].id = id;
	head->fastmap[h].iif = iif;
	head->fastmap[h].filter = f;
}

static __inline__ int route4_hash_to(u32 id)
{
	return id&0xFF;
}

static __inline__ int route4_hash_from(u32 id)
{
	return (id>>16)&0xF;
}

static __inline__ int route4_hash_iif(int iif)
{
	return 16 + ((iif>>16)&0xF);
}

static __inline__ int route4_hash_wild(void)
{
	return 32;
}

#define ROUTE4_APPLY_RESULT()					\
{								\
	*res = f->res;						\
	if (tcf_exts_is_available(&f->exts)) {			\
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

static int route4_classify(struct sk_buff *skb, struct tcf_proto *tp,
			   struct tcf_result *res)
{
	struct route4_head *head = (struct route4_head*)tp->root;
	struct dst_entry *dst;
	struct route4_bucket *b;
	struct route4_filter *f;
	u32 id, h;
	int iif, dont_cache = 0;

	if ((dst = skb->dst) == NULL)
		goto failure;

	id = dst->tclassid;
	if (head == NULL)
		goto old_method;

	iif = ((struct rtable*)dst)->fl.iif;

	h = route4_fastmap_hash(id, iif);
	if (id == head->fastmap[h].id &&
	    iif == head->fastmap[h].iif &&
	    (f = head->fastmap[h].filter) != NULL) {
		if (f == ROUTE4_FAILURE)
			goto failure;

		*res = f->res;
		return 0;
	}

	h = route4_hash_to(id);

restart:
	if ((b = head->table[h]) != NULL) {
		for (f = b->ht[route4_hash_from(id)]; f; f = f->next)
			if (f->id == id)
				ROUTE4_APPLY_RESULT();

		for (f = b->ht[route4_hash_iif(iif)]; f; f = f->next)
			if (f->iif == iif)
				ROUTE4_APPLY_RESULT();

		for (f = b->ht[route4_hash_wild()]; f; f = f->next)
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

old_method:
	if (id && (TC_H_MAJ(id) == 0 ||
		   !(TC_H_MAJ(id^tp->q->handle)))) {
		res->classid = id;
		res->class = 0;
		return 0;
	}
	return -1;
}

static inline u32 to_hash(u32 id)
{
	u32 h = id&0xFF;
	if (id&0x8000)
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
		return id&0xF;
	}
	return 16 + (id&0xF);
}

static unsigned long route4_get(struct tcf_proto *tp, u32 handle)
{
	struct route4_head *head = (struct route4_head*)tp->root;
	struct route4_bucket *b;
	struct route4_filter *f;
	unsigned h1, h2;

	if (!head)
		return 0;

	h1 = to_hash(handle);
	if (h1 > 256)
		return 0;

	h2 = from_hash(handle>>16);
	if (h2 > 32)
		return 0;

	if ((b = head->table[h1]) != NULL) {
		for (f = b->ht[h2]; f; f = f->next)
			if (f->handle == handle)
				return (unsigned long)f;
	}
	return 0;
}

static void route4_put(struct tcf_proto *tp, unsigned long f)
{
}

static int route4_init(struct tcf_proto *tp)
{
	return 0;
}

static inline void
route4_delete_filter(struct tcf_proto *tp, struct route4_filter *f)
{
	tcf_unbind_filter(tp, &f->res);
	tcf_exts_destroy(tp, &f->exts);
	kfree(f);
}

static void route4_destroy(struct tcf_proto *tp)
{
	struct route4_head *head = xchg(&tp->root, NULL);
	int h1, h2;

	if (head == NULL)
		return;

	for (h1=0; h1<=256; h1++) {
		struct route4_bucket *b;

		if ((b = head->table[h1]) != NULL) {
			for (h2=0; h2<=32; h2++) {
				struct route4_filter *f;

				while ((f = b->ht[h2]) != NULL) {
					b->ht[h2] = f->next;
					route4_delete_filter(tp, f);
				}
			}
			kfree(b);
		}
	}
	kfree(head);
}

static int route4_delete(struct tcf_proto *tp, unsigned long arg)
{
	struct route4_head *head = (struct route4_head*)tp->root;
	struct route4_filter **fp, *f = (struct route4_filter*)arg;
	unsigned h = 0;
	struct route4_bucket *b;
	int i;

	if (!head || !f)
		return -EINVAL;

	h = f->handle;
	b = f->bkt;

	for (fp = &b->ht[from_hash(h>>16)]; *fp; fp = &(*fp)->next) {
		if (*fp == f) {
			tcf_tree_lock(tp);
			*fp = f->next;
			tcf_tree_unlock(tp);

			route4_reset_fastmap(tp->q->dev, head, f->id);
			route4_delete_filter(tp, f);

			/* Strip tree */

			for (i=0; i<=32; i++)
				if (b->ht[i])
					return 0;

			/* OK, session has no flows */
			tcf_tree_lock(tp);
			head->table[to_hash(h)] = NULL;
			tcf_tree_unlock(tp);

			kfree(b);
			return 0;
		}
	}
	return 0;
}

static int route4_set_parms(struct tcf_proto *tp, unsigned long base,
	struct route4_filter *f, u32 handle, struct route4_head *head,
	struct rtattr **tb, struct rtattr *est, int new)
{
	int err;
	u32 id = 0, to = 0, nhandle = 0x8000;
	struct route4_filter *fp;
	unsigned int h1;
	struct route4_bucket *b;
	struct tcf_exts e;

	err = tcf_exts_validate(tp, tb, est, &e, &route_ext_map);
	if (err < 0)
		return err;

	err = -EINVAL;
	if (tb[TCA_ROUTE4_CLASSID-1])
		if (RTA_PAYLOAD(tb[TCA_ROUTE4_CLASSID-1]) < sizeof(u32))
			goto errout;

	if (tb[TCA_ROUTE4_TO-1]) {
		if (new && handle & 0x8000)
			goto errout;
		if (RTA_PAYLOAD(tb[TCA_ROUTE4_TO-1]) < sizeof(u32))
			goto errout;
		to = *(u32*)RTA_DATA(tb[TCA_ROUTE4_TO-1]);
		if (to > 0xFF)
			goto errout;
		nhandle = to;
	}

	if (tb[TCA_ROUTE4_FROM-1]) {
		if (tb[TCA_ROUTE4_IIF-1])
			goto errout;
		if (RTA_PAYLOAD(tb[TCA_ROUTE4_FROM-1]) < sizeof(u32))
			goto errout;
		id = *(u32*)RTA_DATA(tb[TCA_ROUTE4_FROM-1]);
		if (id > 0xFF)
			goto errout;
		nhandle |= id << 16;
	} else if (tb[TCA_ROUTE4_IIF-1]) {
		if (RTA_PAYLOAD(tb[TCA_ROUTE4_IIF-1]) < sizeof(u32))
			goto errout;
		id = *(u32*)RTA_DATA(tb[TCA_ROUTE4_IIF-1]);
		if (id > 0x7FFF)
			goto errout;
		nhandle |= (id | 0x8000) << 16;
	} else
		nhandle |= 0xFFFF << 16;

	if (handle && new) {
		nhandle |= handle & 0x7F00;
		if (nhandle != handle)
			goto errout;
	}

	h1 = to_hash(nhandle);
	if ((b = head->table[h1]) == NULL) {
		err = -ENOBUFS;
		b = kmalloc(sizeof(struct route4_bucket), GFP_KERNEL);
		if (b == NULL)
			goto errout;
		memset(b, 0, sizeof(*b));

		tcf_tree_lock(tp);
		head->table[h1] = b;
		tcf_tree_unlock(tp);
	} else {
		unsigned int h2 = from_hash(nhandle >> 16);
		err = -EEXIST;
		for (fp = b->ht[h2]; fp; fp = fp->next)
			if (fp->handle == f->handle)
				goto errout;
	}

	tcf_tree_lock(tp);
	if (tb[TCA_ROUTE4_TO-1])
		f->id = to;

	if (tb[TCA_ROUTE4_FROM-1])
		f->id = to | id<<16;
	else if (tb[TCA_ROUTE4_IIF-1])
		f->iif = id;

	f->handle = nhandle;
	f->bkt = b;
	tcf_tree_unlock(tp);

	if (tb[TCA_ROUTE4_CLASSID-1]) {
		f->res.classid = *(u32*)RTA_DATA(tb[TCA_ROUTE4_CLASSID-1]);
		tcf_bind_filter(tp, &f->res, base);
	}

	tcf_exts_change(tp, &f->exts, &e);

	return 0;
errout:
	tcf_exts_destroy(tp, &e);
	return err;
}

static int route4_change(struct tcf_proto *tp, unsigned long base,
		       u32 handle,
		       struct rtattr **tca,
		       unsigned long *arg)
{
	struct route4_head *head = tp->root;
	struct route4_filter *f, *f1, **fp;
	struct route4_bucket *b;
	struct rtattr *opt = tca[TCA_OPTIONS-1];
	struct rtattr *tb[TCA_ROUTE4_MAX];
	unsigned int h, th;
	u32 old_handle = 0;
	int err;

	if (opt == NULL)
		return handle ? -EINVAL : 0;

	if (rtattr_parse_nested(tb, TCA_ROUTE4_MAX, opt) < 0)
		return -EINVAL;

	if ((f = (struct route4_filter*)*arg) != NULL) {
		if (f->handle != handle && handle)
			return -EINVAL;

		if (f->bkt)
			old_handle = f->handle;

		err = route4_set_parms(tp, base, f, handle, head, tb,
			tca[TCA_RATE-1], 0);
		if (err < 0)
			return err;

		goto reinsert;
	}

	err = -ENOBUFS;
	if (head == NULL) {
		head = kmalloc(sizeof(struct route4_head), GFP_KERNEL);
		if (head == NULL)
			goto errout;
		memset(head, 0, sizeof(struct route4_head));

		tcf_tree_lock(tp);
		tp->root = head;
		tcf_tree_unlock(tp);
	}

	f = kmalloc(sizeof(struct route4_filter), GFP_KERNEL);
	if (f == NULL)
		goto errout;
	memset(f, 0, sizeof(*f));

	err = route4_set_parms(tp, base, f, handle, head, tb,
		tca[TCA_RATE-1], 1);
	if (err < 0)
		goto errout;

reinsert:
	h = from_hash(f->handle >> 16);
	for (fp = &f->bkt->ht[h]; (f1=*fp) != NULL; fp = &f1->next)
		if (f->handle < f1->handle)
			break;

	f->next = f1;
	tcf_tree_lock(tp);
	*fp = f;

	if (old_handle && f->handle != old_handle) {
		th = to_hash(old_handle);
		h = from_hash(old_handle >> 16);
		if ((b = head->table[th]) != NULL) {
			for (fp = &b->ht[h]; *fp; fp = &(*fp)->next) {
				if (*fp == f) {
					*fp = f->next;
					break;
				}
			}
		}
	}
	tcf_tree_unlock(tp);

	route4_reset_fastmap(tp->q->dev, head, f->id);
	*arg = (unsigned long)f;
	return 0;

errout:
	if (f)
		kfree(f);
	return err;
}

static void route4_walk(struct tcf_proto *tp, struct tcf_walker *arg)
{
	struct route4_head *head = tp->root;
	unsigned h, h1;

	if (head == NULL)
		arg->stop = 1;

	if (arg->stop)
		return;

	for (h = 0; h <= 256; h++) {
		struct route4_bucket *b = head->table[h];

		if (b) {
			for (h1 = 0; h1 <= 32; h1++) {
				struct route4_filter *f;

				for (f = b->ht[h1]; f; f = f->next) {
					if (arg->count < arg->skip) {
						arg->count++;
						continue;
					}
					if (arg->fn(tp, (unsigned long)f, arg) < 0) {
						arg->stop = 1;
						return;
					}
					arg->count++;
				}
			}
		}
	}
}

static int route4_dump(struct tcf_proto *tp, unsigned long fh,
		       struct sk_buff *skb, struct tcmsg *t)
{
	struct route4_filter *f = (struct route4_filter*)fh;
	unsigned char	 *b = skb->tail;
	struct rtattr *rta;
	u32 id;

	if (f == NULL)
		return skb->len;

	t->tcm_handle = f->handle;

	rta = (struct rtattr*)b;
	RTA_PUT(skb, TCA_OPTIONS, 0, NULL);

	if (!(f->handle&0x8000)) {
		id = f->id&0xFF;
		RTA_PUT(skb, TCA_ROUTE4_TO, sizeof(id), &id);
	}
	if (f->handle&0x80000000) {
		if ((f->handle>>16) != 0xFFFF)
			RTA_PUT(skb, TCA_ROUTE4_IIF, sizeof(f->iif), &f->iif);
	} else {
		id = f->id>>16;
		RTA_PUT(skb, TCA_ROUTE4_FROM, sizeof(id), &id);
	}
	if (f->res.classid)
		RTA_PUT(skb, TCA_ROUTE4_CLASSID, 4, &f->res.classid);

	if (tcf_exts_dump(skb, &f->exts, &route_ext_map) < 0)
		goto rtattr_failure;

	rta->rta_len = skb->tail - b;

	if (tcf_exts_dump_stats(skb, &f->exts, &route_ext_map) < 0)
		goto rtattr_failure;

	return skb->len;

rtattr_failure:
	skb_trim(skb, b - skb->data);
	return -1;
}

static struct tcf_proto_ops cls_route4_ops = {
	.next		=	NULL,
	.kind		=	"route",
	.classify	=	route4_classify,
	.init		=	route4_init,
	.destroy	=	route4_destroy,
	.get		=	route4_get,
	.put		=	route4_put,
	.change		=	route4_change,
	.delete		=	route4_delete,
	.walk		=	route4_walk,
	.dump		=	route4_dump,
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
