/*
 * net/sched/cls_fw.c	Classifier mapping ipchains' fwmark to traffic class.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 * Changes:
 * Karlis Peisenieks <karlis@mt.lv> : 990415 : fw_walk off by one
 * Karlis Peisenieks <karlis@mt.lv> : 990415 : fw_delete killed all the filter (and kernel).
 * Alex <alex@pilotsoft.com> : 2004xxyy: Added Action extension
 *
 * JHS: We should remove the CONFIG_NET_CLS_IND from here
 * eventually when the meta match extension is made available
 *
 */

#include <linux/config.h>
#include <linux/module.h>
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
#include <linux/netfilter.h>
#include <net/ip.h>
#include <net/route.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/act_api.h>
#include <net/pkt_cls.h>

struct fw_head
{
	struct fw_filter *ht[256];
};

struct fw_filter
{
	struct fw_filter	*next;
	u32			id;
	struct tcf_result	res;
#ifdef CONFIG_NET_CLS_IND
	char			indev[IFNAMSIZ];
#endif /* CONFIG_NET_CLS_IND */
	struct tcf_exts		exts;
};

static struct tcf_ext_map fw_ext_map = {
	.action = TCA_FW_ACT,
	.police = TCA_FW_POLICE
};

static __inline__ int fw_hash(u32 handle)
{
	return handle&0xFF;
}

static int fw_classify(struct sk_buff *skb, struct tcf_proto *tp,
			  struct tcf_result *res)
{
	struct fw_head *head = (struct fw_head*)tp->root;
	struct fw_filter *f;
	int r;
#ifdef CONFIG_NETFILTER
	u32 id = skb->nfmark;
#else
	u32 id = 0;
#endif

	if (head != NULL) {
		for (f=head->ht[fw_hash(id)]; f; f=f->next) {
			if (f->id == id) {
				*res = f->res;
#ifdef CONFIG_NET_CLS_IND
				if (!tcf_match_indev(skb, f->indev))
					continue;
#endif /* CONFIG_NET_CLS_IND */
				r = tcf_exts_exec(skb, &f->exts, res);
				if (r < 0)
					continue;

				return r;
			}
		}
	} else {
		/* old method */
		if (id && (TC_H_MAJ(id) == 0 || !(TC_H_MAJ(id^tp->q->handle)))) {
			res->classid = id;
			res->class = 0;
			return 0;
		}
	}

	return -1;
}

static unsigned long fw_get(struct tcf_proto *tp, u32 handle)
{
	struct fw_head *head = (struct fw_head*)tp->root;
	struct fw_filter *f;

	if (head == NULL)
		return 0;

	for (f=head->ht[fw_hash(handle)]; f; f=f->next) {
		if (f->id == handle)
			return (unsigned long)f;
	}
	return 0;
}

static void fw_put(struct tcf_proto *tp, unsigned long f)
{
}

static int fw_init(struct tcf_proto *tp)
{
	return 0;
}

static inline void
fw_delete_filter(struct tcf_proto *tp, struct fw_filter *f)
{
	tcf_unbind_filter(tp, &f->res);
	tcf_exts_destroy(tp, &f->exts);
	kfree(f);
}

static void fw_destroy(struct tcf_proto *tp)
{
	struct fw_head *head = (struct fw_head*)xchg(&tp->root, NULL);
	struct fw_filter *f;
	int h;

	if (head == NULL)
		return;

	for (h=0; h<256; h++) {
		while ((f=head->ht[h]) != NULL) {
			head->ht[h] = f->next;
			fw_delete_filter(tp, f);
		}
	}
	kfree(head);
}

static int fw_delete(struct tcf_proto *tp, unsigned long arg)
{
	struct fw_head *head = (struct fw_head*)tp->root;
	struct fw_filter *f = (struct fw_filter*)arg;
	struct fw_filter **fp;

	if (head == NULL || f == NULL)
		goto out;

	for (fp=&head->ht[fw_hash(f->id)]; *fp; fp = &(*fp)->next) {
		if (*fp == f) {
			tcf_tree_lock(tp);
			*fp = f->next;
			tcf_tree_unlock(tp);
			fw_delete_filter(tp, f);
			return 0;
		}
	}
out:
	return -EINVAL;
}

static int
fw_change_attrs(struct tcf_proto *tp, struct fw_filter *f,
	struct rtattr **tb, struct rtattr **tca, unsigned long base)
{
	struct tcf_exts e;
	int err;

	err = tcf_exts_validate(tp, tb, tca[TCA_RATE-1], &e, &fw_ext_map);
	if (err < 0)
		return err;

	err = -EINVAL;
	if (tb[TCA_FW_CLASSID-1]) {
		if (RTA_PAYLOAD(tb[TCA_FW_CLASSID-1]) != sizeof(u32))
			goto errout;
		f->res.classid = *(u32*)RTA_DATA(tb[TCA_FW_CLASSID-1]);
		tcf_bind_filter(tp, &f->res, base);
	}

#ifdef CONFIG_NET_CLS_IND
	if (tb[TCA_FW_INDEV-1]) {
		err = tcf_change_indev(tp, f->indev, tb[TCA_FW_INDEV-1]);
		if (err < 0)
			goto errout;
	}
#endif /* CONFIG_NET_CLS_IND */

	tcf_exts_change(tp, &f->exts, &e);

	return 0;
errout:
	tcf_exts_destroy(tp, &e);
	return err;
}

static int fw_change(struct tcf_proto *tp, unsigned long base,
		     u32 handle,
		     struct rtattr **tca,
		     unsigned long *arg)
{
	struct fw_head *head = (struct fw_head*)tp->root;
	struct fw_filter *f = (struct fw_filter *) *arg;
	struct rtattr *opt = tca[TCA_OPTIONS-1];
	struct rtattr *tb[TCA_FW_MAX];
	int err;

	if (!opt)
		return handle ? -EINVAL : 0;

	if (rtattr_parse_nested(tb, TCA_FW_MAX, opt) < 0)
		return -EINVAL;

	if (f != NULL) {
		if (f->id != handle && handle)
			return -EINVAL;
		return fw_change_attrs(tp, f, tb, tca, base);
	}

	if (!handle)
		return -EINVAL;

	if (head == NULL) {
		head = kmalloc(sizeof(struct fw_head), GFP_KERNEL);
		if (head == NULL)
			return -ENOBUFS;
		memset(head, 0, sizeof(*head));

		tcf_tree_lock(tp);
		tp->root = head;
		tcf_tree_unlock(tp);
	}

	f = kmalloc(sizeof(struct fw_filter), GFP_KERNEL);
	if (f == NULL)
		return -ENOBUFS;
	memset(f, 0, sizeof(*f));

	f->id = handle;

	err = fw_change_attrs(tp, f, tb, tca, base);
	if (err < 0)
		goto errout;

	f->next = head->ht[fw_hash(handle)];
	tcf_tree_lock(tp);
	head->ht[fw_hash(handle)] = f;
	tcf_tree_unlock(tp);

	*arg = (unsigned long)f;
	return 0;

errout:
	if (f)
		kfree(f);
	return err;
}

static void fw_walk(struct tcf_proto *tp, struct tcf_walker *arg)
{
	struct fw_head *head = (struct fw_head*)tp->root;
	int h;

	if (head == NULL)
		arg->stop = 1;

	if (arg->stop)
		return;

	for (h = 0; h < 256; h++) {
		struct fw_filter *f;

		for (f = head->ht[h]; f; f = f->next) {
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

static int fw_dump(struct tcf_proto *tp, unsigned long fh,
		   struct sk_buff *skb, struct tcmsg *t)
{
	struct fw_filter *f = (struct fw_filter*)fh;
	unsigned char	 *b = skb->tail;
	struct rtattr *rta;

	if (f == NULL)
		return skb->len;

	t->tcm_handle = f->id;

	if (!f->res.classid && !tcf_exts_is_available(&f->exts))
		return skb->len;

	rta = (struct rtattr*)b;
	RTA_PUT(skb, TCA_OPTIONS, 0, NULL);

	if (f->res.classid)
		RTA_PUT(skb, TCA_FW_CLASSID, 4, &f->res.classid);
#ifdef CONFIG_NET_CLS_IND
	if (strlen(f->indev))
		RTA_PUT(skb, TCA_FW_INDEV, IFNAMSIZ, f->indev);
#endif /* CONFIG_NET_CLS_IND */

	if (tcf_exts_dump(skb, &f->exts, &fw_ext_map) < 0)
		goto rtattr_failure;

	rta->rta_len = skb->tail - b;

	if (tcf_exts_dump_stats(skb, &f->exts, &fw_ext_map) < 0)
		goto rtattr_failure;

	return skb->len;

rtattr_failure:
	skb_trim(skb, b - skb->data);
	return -1;
}

static struct tcf_proto_ops cls_fw_ops = {
	.next		=	NULL,
	.kind		=	"fw",
	.classify	=	fw_classify,
	.init		=	fw_init,
	.destroy	=	fw_destroy,
	.get		=	fw_get,
	.put		=	fw_put,
	.change		=	fw_change,
	.delete		=	fw_delete,
	.walk		=	fw_walk,
	.dump		=	fw_dump,
	.owner		=	THIS_MODULE,
};

static int __init init_fw(void)
{
	return register_tcf_proto_ops(&cls_fw_ops);
}

static void __exit exit_fw(void) 
{
	unregister_tcf_proto_ops(&cls_fw_ops);
}

module_init(init_fw)
module_exit(exit_fw)
MODULE_LICENSE("GPL");
