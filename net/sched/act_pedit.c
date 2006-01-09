/*
 * net/sched/pedit.c	Generic packet editor
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Authors:	Jamal Hadi Salim (2002-4)
 */

#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <linux/config.h>
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
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <net/sock.h>
#include <net/pkt_sched.h>
#include <linux/tc_act/tc_pedit.h>
#include <net/tc_act/tc_pedit.h>


#define PEDIT_DEB 1

/* use generic hash table */
#define MY_TAB_SIZE     16
#define MY_TAB_MASK     15
static u32 idx_gen;
static struct tcf_pedit *tcf_pedit_ht[MY_TAB_SIZE];
static DEFINE_RWLOCK(pedit_lock);

#define tcf_st		tcf_pedit
#define tc_st		tc_pedit
#define tcf_t_lock	pedit_lock
#define tcf_ht		tcf_pedit_ht

#define CONFIG_NET_ACT_INIT 1
#include <net/pkt_act.h>

static int
tcf_pedit_init(struct rtattr *rta, struct rtattr *est, struct tc_action *a,
               int ovr, int bind)
{
	struct rtattr *tb[TCA_PEDIT_MAX];
	struct tc_pedit *parm;
	int ret = 0;
	struct tcf_pedit *p;
	struct tc_pedit_key *keys = NULL;
	int ksize;

	if (rta == NULL || rtattr_parse_nested(tb, TCA_PEDIT_MAX, rta) < 0)
		return -EINVAL;

	if (tb[TCA_PEDIT_PARMS - 1] == NULL ||
	    RTA_PAYLOAD(tb[TCA_PEDIT_PARMS-1]) < sizeof(*parm))
		return -EINVAL;
	parm = RTA_DATA(tb[TCA_PEDIT_PARMS-1]);
	ksize = parm->nkeys * sizeof(struct tc_pedit_key);
	if (RTA_PAYLOAD(tb[TCA_PEDIT_PARMS-1]) < sizeof(*parm) + ksize)
		return -EINVAL;

	p = tcf_hash_check(parm->index, a, ovr, bind);
	if (p == NULL) {
		if (!parm->nkeys)
			return -EINVAL;
		p = tcf_hash_create(parm->index, est, a, sizeof(*p), ovr, bind);
		if (p == NULL)
			return -ENOMEM;
		keys = kmalloc(ksize, GFP_KERNEL);
		if (keys == NULL) {
			kfree(p);
			return -ENOMEM;
		}
		ret = ACT_P_CREATED;
	} else {
		if (!ovr) {
			tcf_hash_release(p, bind);
			return -EEXIST;
		}
		if (p->nkeys && p->nkeys != parm->nkeys) {
			keys = kmalloc(ksize, GFP_KERNEL);
			if (keys == NULL)
				return -ENOMEM;
		}
	}

	spin_lock_bh(&p->lock);
	p->flags = parm->flags;
	p->action = parm->action;
	if (keys) {
		kfree(p->keys);
		p->keys = keys;
		p->nkeys = parm->nkeys;
	}
	memcpy(p->keys, parm->keys, ksize);
	spin_unlock_bh(&p->lock);
	if (ret == ACT_P_CREATED)
		tcf_hash_insert(p);
	return ret;
}

static int
tcf_pedit_cleanup(struct tc_action *a, int bind)
{
	struct tcf_pedit *p = PRIV(a, pedit);

	if (p != NULL) {
		struct tc_pedit_key *keys = p->keys;
		if (tcf_hash_release(p, bind)) {
			kfree(keys);
			return 1;
		}
	}
	return 0;
}

static int
tcf_pedit(struct sk_buff *skb, struct tc_action *a, struct tcf_result *res)
{
	struct tcf_pedit *p = PRIV(a, pedit);
	int i, munged = 0;
	u8 *pptr;

	if (!(skb->tc_verd & TC_OK2MUNGE)) {
		/* should we set skb->cloned? */
		if (pskb_expand_head(skb, 0, 0, GFP_ATOMIC)) {
			return p->action;
		}
	}

	pptr = skb->nh.raw;

	spin_lock(&p->lock);

	p->tm.lastuse = jiffies;

	if (p->nkeys > 0) {
		struct tc_pedit_key *tkey = p->keys;

		for (i = p->nkeys; i > 0; i--, tkey++) {
			u32 *ptr;
			int offset = tkey->off;

			if (tkey->offmask) {
				if (skb->len > tkey->at) {
					 char *j = pptr + tkey->at;
					 offset += ((*j & tkey->offmask) >> 
					           tkey->shift);
				} else {
					goto bad;
				}
			}

			if (offset % 4) {
				printk("offset must be on 32 bit boundaries\n");
				goto bad;
			}
			if (skb->len < 0 || (offset > 0 && offset > skb->len)) {
				printk("offset %d cant exceed pkt length %d\n",
				       offset, skb->len);
				goto bad;
			}

			ptr = (u32 *)(pptr+offset);
			/* just do it, baby */
			*ptr = ((*ptr & tkey->mask) ^ tkey->val);
			munged++;
		}
		
		if (munged)
			skb->tc_verd = SET_TC_MUNGED(skb->tc_verd);
		goto done;
	} else {
		printk("pedit BUG: index %d\n",p->index);
	}

bad:
	p->qstats.overlimits++;
done:
	p->bstats.bytes += skb->len;
	p->bstats.packets++;
	spin_unlock(&p->lock);
	return p->action;
}

static int
tcf_pedit_dump(struct sk_buff *skb, struct tc_action *a,int bind, int ref)
{
	unsigned char *b = skb->tail;
	struct tc_pedit *opt;
	struct tcf_pedit *p = PRIV(a, pedit);
	struct tcf_t t;
	int s; 
		
	s = sizeof(*opt) + p->nkeys * sizeof(struct tc_pedit_key);

	/* netlink spinlocks held above us - must use ATOMIC */
	opt = kmalloc(s, GFP_ATOMIC);
	if (opt == NULL)
		return -ENOBUFS;
	memset(opt, 0, s);

	memcpy(opt->keys, p->keys, p->nkeys * sizeof(struct tc_pedit_key));
	opt->index = p->index;
	opt->nkeys = p->nkeys;
	opt->flags = p->flags;
	opt->action = p->action;
	opt->refcnt = p->refcnt - ref;
	opt->bindcnt = p->bindcnt - bind;


#ifdef PEDIT_DEB
	{                
		/* Debug - get rid of later */
		int i;
		struct tc_pedit_key *key = opt->keys;

		for (i=0; i<opt->nkeys; i++, key++) {
			printk( "\n key #%d",i);
			printk( "  at %d: val %08x mask %08x",
			(unsigned int)key->off,
			(unsigned int)key->val,
			(unsigned int)key->mask);
		}
	}
#endif

	RTA_PUT(skb, TCA_PEDIT_PARMS, s, opt);
	t.install = jiffies_to_clock_t(jiffies - p->tm.install);
	t.lastuse = jiffies_to_clock_t(jiffies - p->tm.lastuse);
	t.expires = jiffies_to_clock_t(p->tm.expires);
	RTA_PUT(skb, TCA_PEDIT_TM, sizeof(t), &t);
	kfree(opt);
	return skb->len;

rtattr_failure:
	skb_trim(skb, b - skb->data);
	kfree(opt);
	return -1;
}

static
struct tc_action_ops act_pedit_ops = {
	.kind		=	"pedit",
	.type		=	TCA_ACT_PEDIT,
	.capab		=	TCA_CAP_NONE,
	.owner		=	THIS_MODULE,
	.act		=	tcf_pedit,
	.dump		=	tcf_pedit_dump,
	.cleanup	=	tcf_pedit_cleanup,
	.lookup		=	tcf_hash_search,
	.init		=	tcf_pedit_init,
	.walk		=	tcf_generic_walker
};

MODULE_AUTHOR("Jamal Hadi Salim(2002-4)");
MODULE_DESCRIPTION("Generic Packet Editor actions");
MODULE_LICENSE("GPL");

static int __init
pedit_init_module(void)
{
	return tcf_register_action(&act_pedit_ops);
}

static void __exit
pedit_cleanup_module(void)
{
	tcf_unregister_action(&act_pedit_ops);
}

module_init(pedit_init_module);
module_exit(pedit_cleanup_module);

