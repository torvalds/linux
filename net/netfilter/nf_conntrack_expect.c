/* Expectation handling for nf_conntrack. */

/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2006 Netfilter Core Team <coreteam@netfilter.org>
 * (C) 2003,2004 USAGI/WIDE Project <http://www.linux-ipv6.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/types.h>
#include <linux/netfilter.h>
#include <linux/skbuff.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/stddef.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/percpu.h>
#include <linux/kernel.h>

#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_conntrack_expect.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <net/netfilter/nf_conntrack_tuple.h>

LIST_HEAD(nf_conntrack_expect_list);
EXPORT_SYMBOL_GPL(nf_conntrack_expect_list);

struct kmem_cache *nf_conntrack_expect_cachep __read_mostly;
static unsigned int nf_conntrack_expect_next_id;

/* nf_conntrack_expect helper functions */
void nf_ct_unlink_expect(struct nf_conntrack_expect *exp)
{
	struct nf_conn_help *master_help = nfct_help(exp->master);

	NF_CT_ASSERT(master_help);
	NF_CT_ASSERT(!timer_pending(&exp->timeout));

	list_del(&exp->list);
	NF_CT_STAT_INC(expect_delete);
	master_help->expecting--;
	nf_conntrack_expect_put(exp);
}
EXPORT_SYMBOL_GPL(nf_ct_unlink_expect);

static void expectation_timed_out(unsigned long ul_expect)
{
	struct nf_conntrack_expect *exp = (void *)ul_expect;

	write_lock_bh(&nf_conntrack_lock);
	nf_ct_unlink_expect(exp);
	write_unlock_bh(&nf_conntrack_lock);
	nf_conntrack_expect_put(exp);
}

struct nf_conntrack_expect *
__nf_conntrack_expect_find(const struct nf_conntrack_tuple *tuple)
{
	struct nf_conntrack_expect *i;

	list_for_each_entry(i, &nf_conntrack_expect_list, list) {
		if (nf_ct_tuple_mask_cmp(tuple, &i->tuple, &i->mask))
			return i;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(__nf_conntrack_expect_find);

/* Just find a expectation corresponding to a tuple. */
struct nf_conntrack_expect *
nf_conntrack_expect_find_get(const struct nf_conntrack_tuple *tuple)
{
	struct nf_conntrack_expect *i;

	read_lock_bh(&nf_conntrack_lock);
	i = __nf_conntrack_expect_find(tuple);
	if (i)
		atomic_inc(&i->use);
	read_unlock_bh(&nf_conntrack_lock);

	return i;
}
EXPORT_SYMBOL_GPL(nf_conntrack_expect_find_get);

/* If an expectation for this connection is found, it gets delete from
 * global list then returned. */
struct nf_conntrack_expect *
find_expectation(const struct nf_conntrack_tuple *tuple)
{
	struct nf_conntrack_expect *exp;

	exp = __nf_conntrack_expect_find(tuple);
	if (!exp)
		return NULL;

	/* If master is not in hash table yet (ie. packet hasn't left
	   this machine yet), how can other end know about expected?
	   Hence these are not the droids you are looking for (if
	   master ct never got confirmed, we'd hold a reference to it
	   and weird things would happen to future packets). */
	if (!nf_ct_is_confirmed(exp->master))
		return NULL;

	if (exp->flags & NF_CT_EXPECT_PERMANENT) {
		atomic_inc(&exp->use);
		return exp;
	} else if (del_timer(&exp->timeout)) {
		nf_ct_unlink_expect(exp);
		return exp;
	}

	return NULL;
}

/* delete all expectations for this conntrack */
void nf_ct_remove_expectations(struct nf_conn *ct)
{
	struct nf_conntrack_expect *i, *tmp;
	struct nf_conn_help *help = nfct_help(ct);

	/* Optimization: most connection never expect any others. */
	if (!help || help->expecting == 0)
		return;

	list_for_each_entry_safe(i, tmp, &nf_conntrack_expect_list, list) {
		if (i->master == ct && del_timer(&i->timeout)) {
			nf_ct_unlink_expect(i);
			nf_conntrack_expect_put(i);
		}
	}
}
EXPORT_SYMBOL_GPL(nf_ct_remove_expectations);

/* Would two expected things clash? */
static inline int expect_clash(const struct nf_conntrack_expect *a,
			       const struct nf_conntrack_expect *b)
{
	/* Part covered by intersection of masks must be unequal,
	   otherwise they clash */
	struct nf_conntrack_tuple intersect_mask;
	int count;

	intersect_mask.src.l3num = a->mask.src.l3num & b->mask.src.l3num;
	intersect_mask.src.u.all = a->mask.src.u.all & b->mask.src.u.all;
	intersect_mask.dst.u.all = a->mask.dst.u.all & b->mask.dst.u.all;
	intersect_mask.dst.protonum = a->mask.dst.protonum
					& b->mask.dst.protonum;

	for (count = 0; count < NF_CT_TUPLE_L3SIZE; count++){
		intersect_mask.src.u3.all[count] =
			a->mask.src.u3.all[count] & b->mask.src.u3.all[count];
	}

	for (count = 0; count < NF_CT_TUPLE_L3SIZE; count++){
		intersect_mask.dst.u3.all[count] =
			a->mask.dst.u3.all[count] & b->mask.dst.u3.all[count];
	}

	return nf_ct_tuple_mask_cmp(&a->tuple, &b->tuple, &intersect_mask);
}

static inline int expect_matches(const struct nf_conntrack_expect *a,
				 const struct nf_conntrack_expect *b)
{
	return a->master == b->master
		&& nf_ct_tuple_equal(&a->tuple, &b->tuple)
		&& nf_ct_tuple_equal(&a->mask, &b->mask);
}

/* Generally a bad idea to call this: could have matched already. */
void nf_conntrack_unexpect_related(struct nf_conntrack_expect *exp)
{
	struct nf_conntrack_expect *i;

	write_lock_bh(&nf_conntrack_lock);
	/* choose the the oldest expectation to evict */
	list_for_each_entry_reverse(i, &nf_conntrack_expect_list, list) {
		if (expect_matches(i, exp) && del_timer(&i->timeout)) {
			nf_ct_unlink_expect(i);
			write_unlock_bh(&nf_conntrack_lock);
			nf_conntrack_expect_put(i);
			return;
		}
	}
	write_unlock_bh(&nf_conntrack_lock);
}
EXPORT_SYMBOL_GPL(nf_conntrack_unexpect_related);

/* We don't increase the master conntrack refcount for non-fulfilled
 * conntracks. During the conntrack destruction, the expectations are
 * always killed before the conntrack itself */
struct nf_conntrack_expect *nf_conntrack_expect_alloc(struct nf_conn *me)
{
	struct nf_conntrack_expect *new;

	new = kmem_cache_alloc(nf_conntrack_expect_cachep, GFP_ATOMIC);
	if (!new)
		return NULL;

	new->master = me;
	atomic_set(&new->use, 1);
	return new;
}
EXPORT_SYMBOL_GPL(nf_conntrack_expect_alloc);

void nf_conntrack_expect_init(struct nf_conntrack_expect *exp, int family,
			      union nf_conntrack_address *saddr,
			      union nf_conntrack_address *daddr,
			      u_int8_t proto, __be16 *src, __be16 *dst)
{
	int len;

	if (family == AF_INET)
		len = 4;
	else
		len = 16;

	exp->flags = 0;
	exp->expectfn = NULL;
	exp->helper = NULL;
	exp->tuple.src.l3num = family;
	exp->tuple.dst.protonum = proto;
	exp->mask.src.l3num = 0xFFFF;
	exp->mask.dst.protonum = 0xFF;

	if (saddr) {
		memcpy(&exp->tuple.src.u3, saddr, len);
		if (sizeof(exp->tuple.src.u3) > len)
			/* address needs to be cleared for nf_ct_tuple_equal */
			memset((void *)&exp->tuple.src.u3 + len, 0x00,
			       sizeof(exp->tuple.src.u3) - len);
		memset(&exp->mask.src.u3, 0xFF, len);
		if (sizeof(exp->mask.src.u3) > len)
			memset((void *)&exp->mask.src.u3 + len, 0x00,
			       sizeof(exp->mask.src.u3) - len);
	} else {
		memset(&exp->tuple.src.u3, 0x00, sizeof(exp->tuple.src.u3));
		memset(&exp->mask.src.u3, 0x00, sizeof(exp->mask.src.u3));
	}

	if (daddr) {
		memcpy(&exp->tuple.dst.u3, daddr, len);
		if (sizeof(exp->tuple.dst.u3) > len)
			/* address needs to be cleared for nf_ct_tuple_equal */
			memset((void *)&exp->tuple.dst.u3 + len, 0x00,
			       sizeof(exp->tuple.dst.u3) - len);
		memset(&exp->mask.dst.u3, 0xFF, len);
		if (sizeof(exp->mask.dst.u3) > len)
			memset((void *)&exp->mask.dst.u3 + len, 0x00,
			       sizeof(exp->mask.dst.u3) - len);
	} else {
		memset(&exp->tuple.dst.u3, 0x00, sizeof(exp->tuple.dst.u3));
		memset(&exp->mask.dst.u3, 0x00, sizeof(exp->mask.dst.u3));
	}

	if (src) {
		exp->tuple.src.u.all = (__force u16)*src;
		exp->mask.src.u.all = 0xFFFF;
	} else {
		exp->tuple.src.u.all = 0;
		exp->mask.src.u.all = 0;
	}

	if (dst) {
		exp->tuple.dst.u.all = (__force u16)*dst;
		exp->mask.dst.u.all = 0xFFFF;
	} else {
		exp->tuple.dst.u.all = 0;
		exp->mask.dst.u.all = 0;
	}
}
EXPORT_SYMBOL_GPL(nf_conntrack_expect_init);

void nf_conntrack_expect_put(struct nf_conntrack_expect *exp)
{
	if (atomic_dec_and_test(&exp->use))
		kmem_cache_free(nf_conntrack_expect_cachep, exp);
}
EXPORT_SYMBOL_GPL(nf_conntrack_expect_put);

static void nf_conntrack_expect_insert(struct nf_conntrack_expect *exp)
{
	struct nf_conn_help *master_help = nfct_help(exp->master);

	atomic_inc(&exp->use);
	master_help->expecting++;
	list_add(&exp->list, &nf_conntrack_expect_list);

	setup_timer(&exp->timeout, expectation_timed_out, (unsigned long)exp);
	exp->timeout.expires = jiffies + master_help->helper->timeout * HZ;
	add_timer(&exp->timeout);

	exp->id = ++nf_conntrack_expect_next_id;
	atomic_inc(&exp->use);
	NF_CT_STAT_INC(expect_create);
}

/* Race with expectations being used means we could have none to find; OK. */
static void evict_oldest_expect(struct nf_conn *master)
{
	struct nf_conntrack_expect *i;

	list_for_each_entry_reverse(i, &nf_conntrack_expect_list, list) {
		if (i->master == master) {
			if (del_timer(&i->timeout)) {
				nf_ct_unlink_expect(i);
				nf_conntrack_expect_put(i);
			}
			break;
		}
	}
}

static inline int refresh_timer(struct nf_conntrack_expect *i)
{
	struct nf_conn_help *master_help = nfct_help(i->master);

	if (!del_timer(&i->timeout))
		return 0;

	i->timeout.expires = jiffies + master_help->helper->timeout*HZ;
	add_timer(&i->timeout);
	return 1;
}

int nf_conntrack_expect_related(struct nf_conntrack_expect *expect)
{
	struct nf_conntrack_expect *i;
	struct nf_conn *master = expect->master;
	struct nf_conn_help *master_help = nfct_help(master);
	int ret;

	NF_CT_ASSERT(master_help);

	write_lock_bh(&nf_conntrack_lock);
	list_for_each_entry(i, &nf_conntrack_expect_list, list) {
		if (expect_matches(i, expect)) {
			/* Refresh timer: if it's dying, ignore.. */
			if (refresh_timer(i)) {
				ret = 0;
				goto out;
			}
		} else if (expect_clash(i, expect)) {
			ret = -EBUSY;
			goto out;
		}
	}
	/* Will be over limit? */
	if (master_help->helper->max_expected &&
	    master_help->expecting >= master_help->helper->max_expected)
		evict_oldest_expect(master);

	nf_conntrack_expect_insert(expect);
	nf_conntrack_expect_event(IPEXP_NEW, expect);
	ret = 0;
out:
	write_unlock_bh(&nf_conntrack_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(nf_conntrack_expect_related);

#ifdef CONFIG_PROC_FS
static void *exp_seq_start(struct seq_file *s, loff_t *pos)
{
	struct list_head *e = &nf_conntrack_expect_list;
	loff_t i;

	/* strange seq_file api calls stop even if we fail,
	 * thus we need to grab lock since stop unlocks */
	read_lock_bh(&nf_conntrack_lock);

	if (list_empty(e))
		return NULL;

	for (i = 0; i <= *pos; i++) {
		e = e->next;
		if (e == &nf_conntrack_expect_list)
			return NULL;
	}
	return e;
}

static void *exp_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	struct list_head *e = v;

	++*pos;
	e = e->next;

	if (e == &nf_conntrack_expect_list)
		return NULL;

	return e;
}

static void exp_seq_stop(struct seq_file *s, void *v)
{
	read_unlock_bh(&nf_conntrack_lock);
}

static int exp_seq_show(struct seq_file *s, void *v)
{
	struct nf_conntrack_expect *expect = v;

	if (expect->timeout.function)
		seq_printf(s, "%ld ", timer_pending(&expect->timeout)
			   ? (long)(expect->timeout.expires - jiffies)/HZ : 0);
	else
		seq_printf(s, "- ");
	seq_printf(s, "l3proto = %u proto=%u ",
		   expect->tuple.src.l3num,
		   expect->tuple.dst.protonum);
	print_tuple(s, &expect->tuple,
		    __nf_ct_l3proto_find(expect->tuple.src.l3num),
		    __nf_ct_l4proto_find(expect->tuple.src.l3num,
				       expect->tuple.dst.protonum));
	return seq_putc(s, '\n');
}

static struct seq_operations exp_seq_ops = {
	.start = exp_seq_start,
	.next = exp_seq_next,
	.stop = exp_seq_stop,
	.show = exp_seq_show
};

static int exp_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &exp_seq_ops);
}

const struct file_operations exp_file_ops = {
	.owner   = THIS_MODULE,
	.open    = exp_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release
};
#endif /* CONFIG_PROC_FS */
