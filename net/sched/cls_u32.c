/*
 * net/sched/cls_u32.c	Ugly (or Universal) 32bit key Packet Classifier.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 *	The filters are packed to hash tables of key nodes
 *	with a set of 32bit key/mask pairs at every node.
 *	Nodes reference next level hash tables etc.
 *
 *	This scheme is the best universal classifier I managed to
 *	invent; it is not super-fast, but it is not slow (provided you
 *	program it correctly), and general enough.  And its relative
 *	speed grows as the number of rules becomes larger.
 *
 *	It seems that it represents the best middle point between
 *	speed and manageability both by human and by machine.
 *
 *	It is especially useful for link sharing combined with QoS;
 *	pure RSVP doesn't need such a general approach and can use
 *	much simpler (and faster) schemes, sort of cls_rsvp.c.
 *
 *	JHS: We should remove the CONFIG_NET_CLS_IND from here
 *	eventually when the meta match extension is made available
 *
 *	nfmark match added by Catalin(ux aka Dino) BOIE <catab at umbrella.ro>
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/rtnetlink.h>
#include <linux/skbuff.h>
#include <net/netlink.h>
#include <net/act_api.h>
#include <net/pkt_cls.h>

struct tc_u_knode
{
	struct tc_u_knode	*next;
	u32			handle;
	struct tc_u_hnode	*ht_up;
	struct tcf_exts		exts;
#ifdef CONFIG_NET_CLS_IND
	char                     indev[IFNAMSIZ];
#endif
	u8			fshift;
	struct tcf_result	res;
	struct tc_u_hnode	*ht_down;
#ifdef CONFIG_CLS_U32_PERF
	struct tc_u32_pcnt	*pf;
#endif
#ifdef CONFIG_CLS_U32_MARK
	struct tc_u32_mark	mark;
#endif
	struct tc_u32_sel	sel;
};

struct tc_u_hnode
{
	struct tc_u_hnode	*next;
	u32			handle;
	u32			prio;
	struct tc_u_common	*tp_c;
	int			refcnt;
	unsigned		divisor;
	struct tc_u_knode	*ht[1];
};

struct tc_u_common
{
	struct tc_u_common	*next;
	struct tc_u_hnode	*hlist;
	struct Qdisc		*q;
	int			refcnt;
	u32			hgenerator;
};

static struct tcf_ext_map u32_ext_map = {
	.action = TCA_U32_ACT,
	.police = TCA_U32_POLICE
};

static struct tc_u_common *u32_list;

static __inline__ unsigned u32_hash_fold(u32 key, struct tc_u32_sel *sel, u8 fshift)
{
	unsigned h = ntohl(key & sel->hmask)>>fshift;

	return h;
}

static int u32_classify(struct sk_buff *skb, struct tcf_proto *tp, struct tcf_result *res)
{
	struct {
		struct tc_u_knode *knode;
		u8		  *ptr;
	} stack[TC_U32_MAXDEPTH];

	struct tc_u_hnode *ht = (struct tc_u_hnode*)tp->root;
	u8 *ptr = skb_network_header(skb);
	struct tc_u_knode *n;
	int sdepth = 0;
	int off2 = 0;
	int sel = 0;
#ifdef CONFIG_CLS_U32_PERF
	int j;
#endif
	int i, r;

next_ht:
	n = ht->ht[sel];

next_knode:
	if (n) {
		struct tc_u32_key *key = n->sel.keys;

#ifdef CONFIG_CLS_U32_PERF
		n->pf->rcnt +=1;
		j = 0;
#endif

#ifdef CONFIG_CLS_U32_MARK
		if ((skb->mark & n->mark.mask) != n->mark.val) {
			n = n->next;
			goto next_knode;
		} else {
			n->mark.success++;
		}
#endif

		for (i = n->sel.nkeys; i>0; i--, key++) {

			if ((*(u32*)(ptr+key->off+(off2&key->offmask))^key->val)&key->mask) {
				n = n->next;
				goto next_knode;
			}
#ifdef CONFIG_CLS_U32_PERF
			n->pf->kcnts[j] +=1;
			j++;
#endif
		}
		if (n->ht_down == NULL) {
check_terminal:
			if (n->sel.flags&TC_U32_TERMINAL) {

				*res = n->res;
#ifdef CONFIG_NET_CLS_IND
				if (!tcf_match_indev(skb, n->indev)) {
					n = n->next;
					goto next_knode;
				}
#endif
#ifdef CONFIG_CLS_U32_PERF
				n->pf->rhit +=1;
#endif
				r = tcf_exts_exec(skb, &n->exts, res);
				if (r < 0) {
					n = n->next;
					goto next_knode;
				}

				return r;
			}
			n = n->next;
			goto next_knode;
		}

		/* PUSH */
		if (sdepth >= TC_U32_MAXDEPTH)
			goto deadloop;
		stack[sdepth].knode = n;
		stack[sdepth].ptr = ptr;
		sdepth++;

		ht = n->ht_down;
		sel = 0;
		if (ht->divisor)
			sel = ht->divisor&u32_hash_fold(*(u32*)(ptr+n->sel.hoff), &n->sel,n->fshift);

		if (!(n->sel.flags&(TC_U32_VAROFFSET|TC_U32_OFFSET|TC_U32_EAT)))
			goto next_ht;

		if (n->sel.flags&(TC_U32_OFFSET|TC_U32_VAROFFSET)) {
			off2 = n->sel.off + 3;
			if (n->sel.flags&TC_U32_VAROFFSET)
				off2 += ntohs(n->sel.offmask & *(u16*)(ptr+n->sel.offoff)) >>n->sel.offshift;
			off2 &= ~3;
		}
		if (n->sel.flags&TC_U32_EAT) {
			ptr += off2;
			off2 = 0;
		}

		if (ptr < skb_tail_pointer(skb))
			goto next_ht;
	}

	/* POP */
	if (sdepth--) {
		n = stack[sdepth].knode;
		ht = n->ht_up;
		ptr = stack[sdepth].ptr;
		goto check_terminal;
	}
	return -1;

deadloop:
	if (net_ratelimit())
		printk("cls_u32: dead loop\n");
	return -1;
}

static __inline__ struct tc_u_hnode *
u32_lookup_ht(struct tc_u_common *tp_c, u32 handle)
{
	struct tc_u_hnode *ht;

	for (ht = tp_c->hlist; ht; ht = ht->next)
		if (ht->handle == handle)
			break;

	return ht;
}

static __inline__ struct tc_u_knode *
u32_lookup_key(struct tc_u_hnode *ht, u32 handle)
{
	unsigned sel;
	struct tc_u_knode *n = NULL;

	sel = TC_U32_HASH(handle);
	if (sel > ht->divisor)
		goto out;

	for (n = ht->ht[sel]; n; n = n->next)
		if (n->handle == handle)
			break;
out:
	return n;
}


static unsigned long u32_get(struct tcf_proto *tp, u32 handle)
{
	struct tc_u_hnode *ht;
	struct tc_u_common *tp_c = tp->data;

	if (TC_U32_HTID(handle) == TC_U32_ROOT)
		ht = tp->root;
	else
		ht = u32_lookup_ht(tp_c, TC_U32_HTID(handle));

	if (!ht)
		return 0;

	if (TC_U32_KEY(handle) == 0)
		return (unsigned long)ht;

	return (unsigned long)u32_lookup_key(ht, handle);
}

static void u32_put(struct tcf_proto *tp, unsigned long f)
{
}

static u32 gen_new_htid(struct tc_u_common *tp_c)
{
	int i = 0x800;

	do {
		if (++tp_c->hgenerator == 0x7FF)
			tp_c->hgenerator = 1;
	} while (--i>0 && u32_lookup_ht(tp_c, (tp_c->hgenerator|0x800)<<20));

	return i > 0 ? (tp_c->hgenerator|0x800)<<20 : 0;
}

static int u32_init(struct tcf_proto *tp)
{
	struct tc_u_hnode *root_ht;
	struct tc_u_common *tp_c;

	for (tp_c = u32_list; tp_c; tp_c = tp_c->next)
		if (tp_c->q == tp->q)
			break;

	root_ht = kzalloc(sizeof(*root_ht), GFP_KERNEL);
	if (root_ht == NULL)
		return -ENOBUFS;

	root_ht->divisor = 0;
	root_ht->refcnt++;
	root_ht->handle = tp_c ? gen_new_htid(tp_c) : 0x80000000;
	root_ht->prio = tp->prio;

	if (tp_c == NULL) {
		tp_c = kzalloc(sizeof(*tp_c), GFP_KERNEL);
		if (tp_c == NULL) {
			kfree(root_ht);
			return -ENOBUFS;
		}
		tp_c->q = tp->q;
		tp_c->next = u32_list;
		u32_list = tp_c;
	}

	tp_c->refcnt++;
	root_ht->next = tp_c->hlist;
	tp_c->hlist = root_ht;
	root_ht->tp_c = tp_c;

	tp->root = root_ht;
	tp->data = tp_c;
	return 0;
}

static int u32_destroy_key(struct tcf_proto *tp, struct tc_u_knode *n)
{
	tcf_unbind_filter(tp, &n->res);
	tcf_exts_destroy(tp, &n->exts);
	if (n->ht_down)
		n->ht_down->refcnt--;
#ifdef CONFIG_CLS_U32_PERF
	kfree(n->pf);
#endif
	kfree(n);
	return 0;
}

static int u32_delete_key(struct tcf_proto *tp, struct tc_u_knode* key)
{
	struct tc_u_knode **kp;
	struct tc_u_hnode *ht = key->ht_up;

	if (ht) {
		for (kp = &ht->ht[TC_U32_HASH(key->handle)]; *kp; kp = &(*kp)->next) {
			if (*kp == key) {
				tcf_tree_lock(tp);
				*kp = key->next;
				tcf_tree_unlock(tp);

				u32_destroy_key(tp, key);
				return 0;
			}
		}
	}
	BUG_TRAP(0);
	return 0;
}

static void u32_clear_hnode(struct tcf_proto *tp, struct tc_u_hnode *ht)
{
	struct tc_u_knode *n;
	unsigned h;

	for (h=0; h<=ht->divisor; h++) {
		while ((n = ht->ht[h]) != NULL) {
			ht->ht[h] = n->next;

			u32_destroy_key(tp, n);
		}
	}
}

static int u32_destroy_hnode(struct tcf_proto *tp, struct tc_u_hnode *ht)
{
	struct tc_u_common *tp_c = tp->data;
	struct tc_u_hnode **hn;

	BUG_TRAP(!ht->refcnt);

	u32_clear_hnode(tp, ht);

	for (hn = &tp_c->hlist; *hn; hn = &(*hn)->next) {
		if (*hn == ht) {
			*hn = ht->next;
			kfree(ht);
			return 0;
		}
	}

	BUG_TRAP(0);
	return -ENOENT;
}

static void u32_destroy(struct tcf_proto *tp)
{
	struct tc_u_common *tp_c = tp->data;
	struct tc_u_hnode *root_ht = xchg(&tp->root, NULL);

	BUG_TRAP(root_ht != NULL);

	if (root_ht && --root_ht->refcnt == 0)
		u32_destroy_hnode(tp, root_ht);

	if (--tp_c->refcnt == 0) {
		struct tc_u_hnode *ht;
		struct tc_u_common **tp_cp;

		for (tp_cp = &u32_list; *tp_cp; tp_cp = &(*tp_cp)->next) {
			if (*tp_cp == tp_c) {
				*tp_cp = tp_c->next;
				break;
			}
		}

		for (ht=tp_c->hlist; ht; ht = ht->next)
			u32_clear_hnode(tp, ht);

		while ((ht = tp_c->hlist) != NULL) {
			tp_c->hlist = ht->next;

			BUG_TRAP(ht->refcnt == 0);

			kfree(ht);
		}

		kfree(tp_c);
	}

	tp->data = NULL;
}

static int u32_delete(struct tcf_proto *tp, unsigned long arg)
{
	struct tc_u_hnode *ht = (struct tc_u_hnode*)arg;

	if (ht == NULL)
		return 0;

	if (TC_U32_KEY(ht->handle))
		return u32_delete_key(tp, (struct tc_u_knode*)ht);

	if (tp->root == ht)
		return -EINVAL;

	if (--ht->refcnt == 0)
		u32_destroy_hnode(tp, ht);

	return 0;
}

static u32 gen_new_kid(struct tc_u_hnode *ht, u32 handle)
{
	struct tc_u_knode *n;
	unsigned i = 0x7FF;

	for (n=ht->ht[TC_U32_HASH(handle)]; n; n = n->next)
		if (i < TC_U32_NODE(n->handle))
			i = TC_U32_NODE(n->handle);
	i++;

	return handle|(i>0xFFF ? 0xFFF : i);
}

static int u32_set_parms(struct tcf_proto *tp, unsigned long base,
			 struct tc_u_hnode *ht,
			 struct tc_u_knode *n, struct rtattr **tb,
			 struct rtattr *est)
{
	int err;
	struct tcf_exts e;

	err = tcf_exts_validate(tp, tb, est, &e, &u32_ext_map);
	if (err < 0)
		return err;

	err = -EINVAL;
	if (tb[TCA_U32_LINK-1]) {
		u32 handle = *(u32*)RTA_DATA(tb[TCA_U32_LINK-1]);
		struct tc_u_hnode *ht_down = NULL;

		if (TC_U32_KEY(handle))
			goto errout;

		if (handle) {
			ht_down = u32_lookup_ht(ht->tp_c, handle);

			if (ht_down == NULL)
				goto errout;
			ht_down->refcnt++;
		}

		tcf_tree_lock(tp);
		ht_down = xchg(&n->ht_down, ht_down);
		tcf_tree_unlock(tp);

		if (ht_down)
			ht_down->refcnt--;
	}
	if (tb[TCA_U32_CLASSID-1]) {
		n->res.classid = *(u32*)RTA_DATA(tb[TCA_U32_CLASSID-1]);
		tcf_bind_filter(tp, &n->res, base);
	}

#ifdef CONFIG_NET_CLS_IND
	if (tb[TCA_U32_INDEV-1]) {
		err = tcf_change_indev(tp, n->indev, tb[TCA_U32_INDEV-1]);
		if (err < 0)
			goto errout;
	}
#endif
	tcf_exts_change(tp, &n->exts, &e);

	return 0;
errout:
	tcf_exts_destroy(tp, &e);
	return err;
}

static int u32_change(struct tcf_proto *tp, unsigned long base, u32 handle,
		      struct rtattr **tca,
		      unsigned long *arg)
{
	struct tc_u_common *tp_c = tp->data;
	struct tc_u_hnode *ht;
	struct tc_u_knode *n;
	struct tc_u32_sel *s;
	struct rtattr *opt = tca[TCA_OPTIONS-1];
	struct rtattr *tb[TCA_U32_MAX];
	u32 htid;
	int err;

	if (opt == NULL)
		return handle ? -EINVAL : 0;

	if (rtattr_parse_nested(tb, TCA_U32_MAX, opt) < 0)
		return -EINVAL;

	if ((n = (struct tc_u_knode*)*arg) != NULL) {
		if (TC_U32_KEY(n->handle) == 0)
			return -EINVAL;

		return u32_set_parms(tp, base, n->ht_up, n, tb, tca[TCA_RATE-1]);
	}

	if (tb[TCA_U32_DIVISOR-1]) {
		unsigned divisor = *(unsigned*)RTA_DATA(tb[TCA_U32_DIVISOR-1]);

		if (--divisor > 0x100)
			return -EINVAL;
		if (TC_U32_KEY(handle))
			return -EINVAL;
		if (handle == 0) {
			handle = gen_new_htid(tp->data);
			if (handle == 0)
				return -ENOMEM;
		}
		ht = kzalloc(sizeof(*ht) + divisor*sizeof(void*), GFP_KERNEL);
		if (ht == NULL)
			return -ENOBUFS;
		ht->tp_c = tp_c;
		ht->refcnt = 0;
		ht->divisor = divisor;
		ht->handle = handle;
		ht->prio = tp->prio;
		ht->next = tp_c->hlist;
		tp_c->hlist = ht;
		*arg = (unsigned long)ht;
		return 0;
	}

	if (tb[TCA_U32_HASH-1]) {
		htid = *(unsigned*)RTA_DATA(tb[TCA_U32_HASH-1]);
		if (TC_U32_HTID(htid) == TC_U32_ROOT) {
			ht = tp->root;
			htid = ht->handle;
		} else {
			ht = u32_lookup_ht(tp->data, TC_U32_HTID(htid));
			if (ht == NULL)
				return -EINVAL;
		}
	} else {
		ht = tp->root;
		htid = ht->handle;
	}

	if (ht->divisor < TC_U32_HASH(htid))
		return -EINVAL;

	if (handle) {
		if (TC_U32_HTID(handle) && TC_U32_HTID(handle^htid))
			return -EINVAL;
		handle = htid | TC_U32_NODE(handle);
	} else
		handle = gen_new_kid(ht, htid);

	if (tb[TCA_U32_SEL-1] == NULL ||
	    RTA_PAYLOAD(tb[TCA_U32_SEL-1]) < sizeof(struct tc_u32_sel))
		return -EINVAL;

	s = RTA_DATA(tb[TCA_U32_SEL-1]);

	n = kzalloc(sizeof(*n) + s->nkeys*sizeof(struct tc_u32_key), GFP_KERNEL);
	if (n == NULL)
		return -ENOBUFS;

#ifdef CONFIG_CLS_U32_PERF
	n->pf = kzalloc(sizeof(struct tc_u32_pcnt) + s->nkeys*sizeof(u64), GFP_KERNEL);
	if (n->pf == NULL) {
		kfree(n);
		return -ENOBUFS;
	}
#endif

	memcpy(&n->sel, s, sizeof(*s) + s->nkeys*sizeof(struct tc_u32_key));
	n->ht_up = ht;
	n->handle = handle;
	n->fshift = s->hmask ? ffs(ntohl(s->hmask)) - 1 : 0;

#ifdef CONFIG_CLS_U32_MARK
	if (tb[TCA_U32_MARK-1]) {
		struct tc_u32_mark *mark;

		if (RTA_PAYLOAD(tb[TCA_U32_MARK-1]) < sizeof(struct tc_u32_mark)) {
#ifdef CONFIG_CLS_U32_PERF
			kfree(n->pf);
#endif
			kfree(n);
			return -EINVAL;
		}
		mark = RTA_DATA(tb[TCA_U32_MARK-1]);
		memcpy(&n->mark, mark, sizeof(struct tc_u32_mark));
		n->mark.success = 0;
	}
#endif

	err = u32_set_parms(tp, base, ht, n, tb, tca[TCA_RATE-1]);
	if (err == 0) {
		struct tc_u_knode **ins;
		for (ins = &ht->ht[TC_U32_HASH(handle)]; *ins; ins = &(*ins)->next)
			if (TC_U32_NODE(handle) < TC_U32_NODE((*ins)->handle))
				break;

		n->next = *ins;
		wmb();
		*ins = n;

		*arg = (unsigned long)n;
		return 0;
	}
#ifdef CONFIG_CLS_U32_PERF
	kfree(n->pf);
#endif
	kfree(n);
	return err;
}

static void u32_walk(struct tcf_proto *tp, struct tcf_walker *arg)
{
	struct tc_u_common *tp_c = tp->data;
	struct tc_u_hnode *ht;
	struct tc_u_knode *n;
	unsigned h;

	if (arg->stop)
		return;

	for (ht = tp_c->hlist; ht; ht = ht->next) {
		if (ht->prio != tp->prio)
			continue;
		if (arg->count >= arg->skip) {
			if (arg->fn(tp, (unsigned long)ht, arg) < 0) {
				arg->stop = 1;
				return;
			}
		}
		arg->count++;
		for (h = 0; h <= ht->divisor; h++) {
			for (n = ht->ht[h]; n; n = n->next) {
				if (arg->count < arg->skip) {
					arg->count++;
					continue;
				}
				if (arg->fn(tp, (unsigned long)n, arg) < 0) {
					arg->stop = 1;
					return;
				}
				arg->count++;
			}
		}
	}
}

static int u32_dump(struct tcf_proto *tp, unsigned long fh,
		     struct sk_buff *skb, struct tcmsg *t)
{
	struct tc_u_knode *n = (struct tc_u_knode*)fh;
	unsigned char *b = skb_tail_pointer(skb);
	struct rtattr *rta;

	if (n == NULL)
		return skb->len;

	t->tcm_handle = n->handle;

	rta = (struct rtattr*)b;
	RTA_PUT(skb, TCA_OPTIONS, 0, NULL);

	if (TC_U32_KEY(n->handle) == 0) {
		struct tc_u_hnode *ht = (struct tc_u_hnode*)fh;
		u32 divisor = ht->divisor+1;
		RTA_PUT(skb, TCA_U32_DIVISOR, 4, &divisor);
	} else {
		RTA_PUT(skb, TCA_U32_SEL,
			sizeof(n->sel) + n->sel.nkeys*sizeof(struct tc_u32_key),
			&n->sel);
		if (n->ht_up) {
			u32 htid = n->handle & 0xFFFFF000;
			RTA_PUT(skb, TCA_U32_HASH, 4, &htid);
		}
		if (n->res.classid)
			RTA_PUT(skb, TCA_U32_CLASSID, 4, &n->res.classid);
		if (n->ht_down)
			RTA_PUT(skb, TCA_U32_LINK, 4, &n->ht_down->handle);

#ifdef CONFIG_CLS_U32_MARK
		if (n->mark.val || n->mark.mask)
			RTA_PUT(skb, TCA_U32_MARK, sizeof(n->mark), &n->mark);
#endif

		if (tcf_exts_dump(skb, &n->exts, &u32_ext_map) < 0)
			goto rtattr_failure;

#ifdef CONFIG_NET_CLS_IND
		if(strlen(n->indev))
			RTA_PUT(skb, TCA_U32_INDEV, IFNAMSIZ, n->indev);
#endif
#ifdef CONFIG_CLS_U32_PERF
		RTA_PUT(skb, TCA_U32_PCNT,
		sizeof(struct tc_u32_pcnt) + n->sel.nkeys*sizeof(u64),
			n->pf);
#endif
	}

	rta->rta_len = skb_tail_pointer(skb) - b;
	if (TC_U32_KEY(n->handle))
		if (tcf_exts_dump_stats(skb, &n->exts, &u32_ext_map) < 0)
			goto rtattr_failure;
	return skb->len;

rtattr_failure:
	nlmsg_trim(skb, b);
	return -1;
}

static struct tcf_proto_ops cls_u32_ops = {
	.next		=	NULL,
	.kind		=	"u32",
	.classify	=	u32_classify,
	.init		=	u32_init,
	.destroy	=	u32_destroy,
	.get		=	u32_get,
	.put		=	u32_put,
	.change		=	u32_change,
	.delete		=	u32_delete,
	.walk		=	u32_walk,
	.dump		=	u32_dump,
	.owner		=	THIS_MODULE,
};

static int __init init_u32(void)
{
	printk("u32 classifier\n");
#ifdef CONFIG_CLS_U32_PERF
	printk("    Performance counters on\n");
#endif
#ifdef CONFIG_NET_CLS_IND
	printk("    input device check on \n");
#endif
#ifdef CONFIG_NET_CLS_ACT
	printk("    Actions configured \n");
#endif
	return register_tcf_proto_ops(&cls_u32_ops);
}

static void __exit exit_u32(void)
{
	unregister_tcf_proto_ops(&cls_u32_ops);
}

module_init(init_u32)
module_exit(exit_u32)
MODULE_LICENSE("GPL");
