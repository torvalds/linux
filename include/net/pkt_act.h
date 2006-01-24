#ifndef __NET_PKT_ACT_H
#define __NET_PKT_ACT_H

#include <asm/uaccess.h>
#include <asm/system.h>
#include <linux/bitops.h>
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
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <net/sock.h>
#include <net/pkt_sched.h>

#define tca_st(val) (struct tcf_##val *)
#define PRIV(a,name) ( tca_st(name) (a)->priv)

#if 0 /* control */
#define DPRINTK(format,args...) printk(KERN_DEBUG format,##args)
#else
#define DPRINTK(format,args...)
#endif

#if 0 /* data */
#define D2PRINTK(format,args...) printk(KERN_DEBUG format,##args)
#else
#define D2PRINTK(format,args...)
#endif

static __inline__ unsigned
tcf_hash(u32 index)
{
	return index & MY_TAB_MASK;
}

/* probably move this from being inline
 * and put into act_generic
*/
static inline void
tcf_hash_destroy(struct tcf_st *p)
{
	unsigned h = tcf_hash(p->index);
	struct tcf_st **p1p;

	for (p1p = &tcf_ht[h]; *p1p; p1p = &(*p1p)->next) {
		if (*p1p == p) {
			write_lock_bh(&tcf_t_lock);
			*p1p = p->next;
			write_unlock_bh(&tcf_t_lock);
#ifdef CONFIG_NET_ESTIMATOR
			gen_kill_estimator(&p->bstats, &p->rate_est);
#endif
			kfree(p);
			return;
		}
	}
	BUG_TRAP(0);
}

static inline int
tcf_hash_release(struct tcf_st *p, int bind )
{
	int ret = 0;
	if (p) {
		if (bind) {
			p->bindcnt--;
		}
		p->refcnt--;
	       	if(p->bindcnt <=0 && p->refcnt <= 0) {
			tcf_hash_destroy(p);
			ret = 1;
		}
	}
	return ret;
}

static __inline__ int
tcf_dump_walker(struct sk_buff *skb, struct netlink_callback *cb,
		struct tc_action *a)
{
	struct tcf_st *p;
	int err =0, index =  -1,i= 0, s_i = 0, n_i = 0;
	struct rtattr *r ;

	read_lock(&tcf_t_lock);

	s_i = cb->args[0];

	for (i = 0; i < MY_TAB_SIZE; i++) {
		p = tcf_ht[tcf_hash(i)];

		for (; p; p = p->next) {
			index++;
			if (index < s_i)
				continue;
			a->priv = p;
			a->order = n_i;
			r = (struct rtattr*) skb->tail;
			RTA_PUT(skb, a->order, 0, NULL);
			err = tcf_action_dump_1(skb, a, 0, 0);
			if (0 > err) {
				index--;
				skb_trim(skb, (u8*)r - skb->data);
				goto done;
			}
			r->rta_len = skb->tail - (u8*)r;
			n_i++;
			if (n_i >= TCA_ACT_MAX_PRIO) {
				goto done;
			}
		}
	}
done:
	read_unlock(&tcf_t_lock);
	if (n_i)
		cb->args[0] += n_i;
	return n_i;

rtattr_failure:
	skb_trim(skb, (u8*)r - skb->data);
	goto done;
}

static __inline__ int
tcf_del_walker(struct sk_buff *skb, struct tc_action *a)
{
	struct tcf_st *p, *s_p;
	struct rtattr *r ;
	int i= 0, n_i = 0;

	r = (struct rtattr*) skb->tail;
	RTA_PUT(skb, a->order, 0, NULL);
	RTA_PUT(skb, TCA_KIND, IFNAMSIZ, a->ops->kind);
	for (i = 0; i < MY_TAB_SIZE; i++) {
		p = tcf_ht[tcf_hash(i)];

		while (p != NULL) {
			s_p = p->next;
			if (ACT_P_DELETED == tcf_hash_release(p, 0)) {
				 module_put(a->ops->owner);
			}
			n_i++;
			p = s_p;
		}
	}
	RTA_PUT(skb, TCA_FCNT, 4, &n_i);
	r->rta_len = skb->tail - (u8*)r;

	return n_i;
rtattr_failure:
	skb_trim(skb, (u8*)r - skb->data);
	return -EINVAL;
}

static __inline__ int
tcf_generic_walker(struct sk_buff *skb, struct netlink_callback *cb, int type,
		struct tc_action *a)
{
		if (type == RTM_DELACTION) {
			return tcf_del_walker(skb,a);
		} else if (type == RTM_GETACTION) {
			return tcf_dump_walker(skb,cb,a);
		} else {
			printk("tcf_generic_walker: unknown action %d\n",type);
			return -EINVAL;
		}
}

static __inline__ struct tcf_st *
tcf_hash_lookup(u32 index)
{
	struct tcf_st *p;

	read_lock(&tcf_t_lock);
	for (p = tcf_ht[tcf_hash(index)]; p; p = p->next) {
		if (p->index == index)
			break;
	}
	read_unlock(&tcf_t_lock);
	return p;
}

static __inline__ u32
tcf_hash_new_index(void)
{
	do {
		if (++idx_gen == 0)
			idx_gen = 1;
	} while (tcf_hash_lookup(idx_gen));

	return idx_gen;
}


static inline int
tcf_hash_search(struct tc_action *a, u32 index)
{
	struct tcf_st *p = tcf_hash_lookup(index);

	if (p != NULL) {
		a->priv = p;
		return 1;
	}
	return 0;
}

#ifdef CONFIG_NET_ACT_INIT
static inline struct tcf_st *
tcf_hash_check(u32 index, struct tc_action *a, int ovr, int bind)
{
	struct tcf_st *p = NULL;
	if (index && (p = tcf_hash_lookup(index)) != NULL) {
		if (bind) {
			p->bindcnt++;
			p->refcnt++;
		}
		a->priv = p;
	}
	return p;
}

static inline struct tcf_st *
tcf_hash_create(u32 index, struct rtattr *est, struct tc_action *a, int size, int ovr, int bind)
{
	struct tcf_st *p = NULL;

	p = kmalloc(size, GFP_KERNEL);
	if (p == NULL)
		return p;

	memset(p, 0, size);
	p->refcnt = 1;

	if (bind) {
		p->bindcnt = 1;
	}

	spin_lock_init(&p->lock);
	p->stats_lock = &p->lock;
	p->index = index ? : tcf_hash_new_index();
	p->tm.install = jiffies;
	p->tm.lastuse = jiffies;
#ifdef CONFIG_NET_ESTIMATOR
	if (est)
		gen_new_estimator(&p->bstats, &p->rate_est, p->stats_lock, est);
#endif
	a->priv = (void *) p;
	return p;
}

static inline void tcf_hash_insert(struct tcf_st *p)
{
	unsigned h = tcf_hash(p->index);

	write_lock_bh(&tcf_t_lock);
	p->next = tcf_ht[h];
	tcf_ht[h] = p;
	write_unlock_bh(&tcf_t_lock);
}

#endif

#endif
