/*
 * net/sched/cls_basic.c	Basic Packet Classifier.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Authors:	Thomas Graf <tgraf@suug.ch>
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/rtnetlink.h>
#include <linux/skbuff.h>
#include <net/act_api.h>
#include <net/pkt_cls.h>

struct basic_head
{
	u32			hgenerator;
	struct list_head	flist;
};

struct basic_filter
{
	u32			handle;
	struct tcf_exts		exts;
	struct tcf_ematch_tree	ematches;
	struct tcf_result	res;
	struct list_head	link;
};

static struct tcf_ext_map basic_ext_map = {
	.action = TCA_BASIC_ACT,
	.police = TCA_BASIC_POLICE
};

static int basic_classify(struct sk_buff *skb, struct tcf_proto *tp,
			  struct tcf_result *res)
{
	int r;
	struct basic_head *head = (struct basic_head *) tp->root;
	struct basic_filter *f;

	list_for_each_entry(f, &head->flist, link) {
		if (!tcf_em_tree_match(skb, &f->ematches, NULL))
			continue;
		*res = f->res;
		r = tcf_exts_exec(skb, &f->exts, res);
		if (r < 0)
			continue;
		return r;
	}
	return -1;
}

static unsigned long basic_get(struct tcf_proto *tp, u32 handle)
{
	unsigned long l = 0UL;
	struct basic_head *head = (struct basic_head *) tp->root;
	struct basic_filter *f;

	if (head == NULL)
		return 0UL;

	list_for_each_entry(f, &head->flist, link)
		if (f->handle == handle)
			l = (unsigned long) f;

	return l;
}

static void basic_put(struct tcf_proto *tp, unsigned long f)
{
}

static int basic_init(struct tcf_proto *tp)
{
	struct basic_head *head;

	head = kzalloc(sizeof(*head), GFP_KERNEL);
	if (head == NULL)
		return -ENOBUFS;
	INIT_LIST_HEAD(&head->flist);
	tp->root = head;
	return 0;
}

static inline void basic_delete_filter(struct tcf_proto *tp,
				       struct basic_filter *f)
{
	tcf_unbind_filter(tp, &f->res);
	tcf_exts_destroy(tp, &f->exts);
	tcf_em_tree_destroy(tp, &f->ematches);
	kfree(f);
}

static void basic_destroy(struct tcf_proto *tp)
{
	struct basic_head *head = (struct basic_head *) xchg(&tp->root, NULL);
	struct basic_filter *f, *n;

	list_for_each_entry_safe(f, n, &head->flist, link) {
		list_del(&f->link);
		basic_delete_filter(tp, f);
	}
	kfree(head);
}

static int basic_delete(struct tcf_proto *tp, unsigned long arg)
{
	struct basic_head *head = (struct basic_head *) tp->root;
	struct basic_filter *t, *f = (struct basic_filter *) arg;

	list_for_each_entry(t, &head->flist, link)
		if (t == f) {
			tcf_tree_lock(tp);
			list_del(&t->link);
			tcf_tree_unlock(tp);
			basic_delete_filter(tp, t);
			return 0;
		}

	return -ENOENT;
}

static inline int basic_set_parms(struct tcf_proto *tp, struct basic_filter *f,
				  unsigned long base, struct rtattr **tb,
				  struct rtattr *est)
{
	int err = -EINVAL;
	struct tcf_exts e;
	struct tcf_ematch_tree t;

	if (tb[TCA_BASIC_CLASSID-1])
		if (RTA_PAYLOAD(tb[TCA_BASIC_CLASSID-1]) < sizeof(u32))
			return err;

	err = tcf_exts_validate(tp, tb, est, &e, &basic_ext_map);
	if (err < 0)
		return err;

	err = tcf_em_tree_validate(tp, tb[TCA_BASIC_EMATCHES-1], &t);
	if (err < 0)
		goto errout;

	if (tb[TCA_BASIC_CLASSID-1]) {
		f->res.classid = *(u32*)RTA_DATA(tb[TCA_BASIC_CLASSID-1]);
		tcf_bind_filter(tp, &f->res, base);
	}

	tcf_exts_change(tp, &f->exts, &e);
	tcf_em_tree_change(tp, &f->ematches, &t);

	return 0;
errout:
	tcf_exts_destroy(tp, &e);
	return err;
}

static int basic_change(struct tcf_proto *tp, unsigned long base, u32 handle,
			struct rtattr **tca, unsigned long *arg)
{
	int err = -EINVAL;
	struct basic_head *head = (struct basic_head *) tp->root;
	struct rtattr *tb[TCA_BASIC_MAX];
	struct basic_filter *f = (struct basic_filter *) *arg;

	if (tca[TCA_OPTIONS-1] == NULL)
		return -EINVAL;

	if (rtattr_parse_nested(tb, TCA_BASIC_MAX, tca[TCA_OPTIONS-1]) < 0)
		return -EINVAL;

	if (f != NULL) {
		if (handle && f->handle != handle)
			return -EINVAL;
		return basic_set_parms(tp, f, base, tb, tca[TCA_RATE-1]);
	}

	err = -ENOBUFS;
	f = kzalloc(sizeof(*f), GFP_KERNEL);
	if (f == NULL)
		goto errout;

	err = -EINVAL;
	if (handle)
		f->handle = handle;
	else {
		unsigned int i = 0x80000000;
		do {
			if (++head->hgenerator == 0x7FFFFFFF)
				head->hgenerator = 1;
		} while (--i > 0 && basic_get(tp, head->hgenerator));

		if (i <= 0) {
			printk(KERN_ERR "Insufficient number of handles\n");
			goto errout;
		}

		f->handle = head->hgenerator;
	}

	err = basic_set_parms(tp, f, base, tb, tca[TCA_RATE-1]);
	if (err < 0)
		goto errout;

	tcf_tree_lock(tp);
	list_add(&f->link, &head->flist);
	tcf_tree_unlock(tp);
	*arg = (unsigned long) f;

	return 0;
errout:
	if (*arg == 0UL && f)
		kfree(f);

	return err;
}

static void basic_walk(struct tcf_proto *tp, struct tcf_walker *arg)
{
	struct basic_head *head = (struct basic_head *) tp->root;
	struct basic_filter *f;

	list_for_each_entry(f, &head->flist, link) {
		if (arg->count < arg->skip)
			goto skip;

		if (arg->fn(tp, (unsigned long) f, arg) < 0) {
			arg->stop = 1;
			break;
		}
skip:
		arg->count++;
	}
}

static int basic_dump(struct tcf_proto *tp, unsigned long fh,
		      struct sk_buff *skb, struct tcmsg *t)
{
	struct basic_filter *f = (struct basic_filter *) fh;
	unsigned char *b = skb->tail;
	struct rtattr *rta;

	if (f == NULL)
		return skb->len;

	t->tcm_handle = f->handle;

	rta = (struct rtattr *) b;
	RTA_PUT(skb, TCA_OPTIONS, 0, NULL);

	if (f->res.classid)
		RTA_PUT(skb, TCA_BASIC_CLASSID, sizeof(u32), &f->res.classid);

	if (tcf_exts_dump(skb, &f->exts, &basic_ext_map) < 0 ||
	    tcf_em_tree_dump(skb, &f->ematches, TCA_BASIC_EMATCHES) < 0)
		goto rtattr_failure;

	rta->rta_len = (skb->tail - b);
	return skb->len;

rtattr_failure:
	skb_trim(skb, b - skb->data);
	return -1;
}

static struct tcf_proto_ops cls_basic_ops = {
	.kind		=	"basic",
	.classify	=	basic_classify,
	.init		=	basic_init,
	.destroy	=	basic_destroy,
	.get		=	basic_get,
	.put		=	basic_put,
	.change		=	basic_change,
	.delete		=	basic_delete,
	.walk		=	basic_walk,
	.dump		=	basic_dump,
	.owner		=	THIS_MODULE,
};

static int __init init_basic(void)
{
	return register_tcf_proto_ops(&cls_basic_ops);
}

static void __exit exit_basic(void)
{
	unregister_tcf_proto_ops(&cls_basic_ops);
}

module_init(init_basic)
module_exit(exit_basic)
MODULE_LICENSE("GPL");

