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
#include <linux/jhash.h>

#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_conntrack_expect.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <net/netfilter/nf_conntrack_tuple.h>

struct hlist_head *nf_ct_expect_hash __read_mostly;
EXPORT_SYMBOL_GPL(nf_ct_expect_hash);

unsigned int nf_ct_expect_hsize __read_mostly;
EXPORT_SYMBOL_GPL(nf_ct_expect_hsize);

static unsigned int nf_ct_expect_hash_rnd __read_mostly;
static unsigned int nf_ct_expect_count;
unsigned int nf_ct_expect_max __read_mostly;
static int nf_ct_expect_hash_rnd_initted __read_mostly;
static int nf_ct_expect_vmalloc;

static struct kmem_cache *nf_ct_expect_cachep __read_mostly;
static unsigned int nf_ct_expect_next_id;

/* nf_conntrack_expect helper functions */
void nf_ct_unlink_expect(struct nf_conntrack_expect *exp)
{
	struct nf_conn_help *master_help = nfct_help(exp->master);

	NF_CT_ASSERT(master_help);
	NF_CT_ASSERT(!timer_pending(&exp->timeout));

	hlist_del(&exp->hnode);
	nf_ct_expect_count--;

	hlist_del(&exp->lnode);
	master_help->expecting--;
	nf_ct_expect_put(exp);

	NF_CT_STAT_INC(expect_delete);
}
EXPORT_SYMBOL_GPL(nf_ct_unlink_expect);

static void nf_ct_expectation_timed_out(unsigned long ul_expect)
{
	struct nf_conntrack_expect *exp = (void *)ul_expect;

	write_lock_bh(&nf_conntrack_lock);
	nf_ct_unlink_expect(exp);
	write_unlock_bh(&nf_conntrack_lock);
	nf_ct_expect_put(exp);
}

static unsigned int nf_ct_expect_dst_hash(const struct nf_conntrack_tuple *tuple)
{
	if (unlikely(!nf_ct_expect_hash_rnd_initted)) {
		get_random_bytes(&nf_ct_expect_hash_rnd, 4);
		nf_ct_expect_hash_rnd_initted = 1;
	}

	return jhash2(tuple->dst.u3.all, ARRAY_SIZE(tuple->dst.u3.all),
		      (((tuple->dst.protonum ^ tuple->src.l3num) << 16) |
		       (__force __u16)tuple->dst.u.all) ^ nf_ct_expect_hash_rnd) %
	       nf_ct_expect_hsize;
}

struct nf_conntrack_expect *
__nf_ct_expect_find(const struct nf_conntrack_tuple *tuple)
{
	struct nf_conntrack_expect *i;
	struct hlist_node *n;
	unsigned int h;

	if (!nf_ct_expect_count)
		return NULL;

	h = nf_ct_expect_dst_hash(tuple);
	hlist_for_each_entry(i, n, &nf_ct_expect_hash[h], hnode) {
		if (nf_ct_tuple_mask_cmp(tuple, &i->tuple, &i->mask))
			return i;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(__nf_ct_expect_find);

/* Just find a expectation corresponding to a tuple. */
struct nf_conntrack_expect *
nf_ct_expect_find_get(const struct nf_conntrack_tuple *tuple)
{
	struct nf_conntrack_expect *i;

	read_lock_bh(&nf_conntrack_lock);
	i = __nf_ct_expect_find(tuple);
	if (i)
		atomic_inc(&i->use);
	read_unlock_bh(&nf_conntrack_lock);

	return i;
}
EXPORT_SYMBOL_GPL(nf_ct_expect_find_get);

/* If an expectation for this connection is found, it gets delete from
 * global list then returned. */
struct nf_conntrack_expect *
nf_ct_find_expectation(const struct nf_conntrack_tuple *tuple)
{
	struct nf_conntrack_expect *exp;

	exp = __nf_ct_expect_find(tuple);
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
	struct nf_conn_help *help = nfct_help(ct);
	struct nf_conntrack_expect *exp;
	struct hlist_node *n, *next;

	/* Optimization: most connection never expect any others. */
	if (!help || help->expecting == 0)
		return;

	hlist_for_each_entry_safe(exp, n, next, &help->expectations, lnode) {
		if (del_timer(&exp->timeout)) {
			nf_ct_unlink_expect(exp);
			nf_ct_expect_put(exp);
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
	struct nf_conntrack_tuple_mask intersect_mask;
	int count;

	intersect_mask.src.u.all = a->mask.src.u.all & b->mask.src.u.all;

	for (count = 0; count < NF_CT_TUPLE_L3SIZE; count++){
		intersect_mask.src.u3.all[count] =
			a->mask.src.u3.all[count] & b->mask.src.u3.all[count];
	}

	return nf_ct_tuple_mask_cmp(&a->tuple, &b->tuple, &intersect_mask);
}

static inline int expect_matches(const struct nf_conntrack_expect *a,
				 const struct nf_conntrack_expect *b)
{
	return a->master == b->master
		&& nf_ct_tuple_equal(&a->tuple, &b->tuple)
		&& nf_ct_tuple_mask_equal(&a->mask, &b->mask);
}

/* Generally a bad idea to call this: could have matched already. */
void nf_ct_unexpect_related(struct nf_conntrack_expect *exp)
{
	write_lock_bh(&nf_conntrack_lock);
	if (del_timer(&exp->timeout)) {
		nf_ct_unlink_expect(exp);
		nf_ct_expect_put(exp);
	}
	write_unlock_bh(&nf_conntrack_lock);
}
EXPORT_SYMBOL_GPL(nf_ct_unexpect_related);

/* We don't increase the master conntrack refcount for non-fulfilled
 * conntracks. During the conntrack destruction, the expectations are
 * always killed before the conntrack itself */
struct nf_conntrack_expect *nf_ct_expect_alloc(struct nf_conn *me)
{
	struct nf_conntrack_expect *new;

	new = kmem_cache_alloc(nf_ct_expect_cachep, GFP_ATOMIC);
	if (!new)
		return NULL;

	new->master = me;
	atomic_set(&new->use, 1);
	return new;
}
EXPORT_SYMBOL_GPL(nf_ct_expect_alloc);

void nf_ct_expect_init(struct nf_conntrack_expect *exp, int family,
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

	if (src) {
		exp->tuple.src.u.all = *src;
		exp->mask.src.u.all = htons(0xFFFF);
	} else {
		exp->tuple.src.u.all = 0;
		exp->mask.src.u.all = 0;
	}

	memcpy(&exp->tuple.dst.u3, daddr, len);
	if (sizeof(exp->tuple.dst.u3) > len)
		/* address needs to be cleared for nf_ct_tuple_equal */
		memset((void *)&exp->tuple.dst.u3 + len, 0x00,
		       sizeof(exp->tuple.dst.u3) - len);

	exp->tuple.dst.u.all = *dst;
}
EXPORT_SYMBOL_GPL(nf_ct_expect_init);

void nf_ct_expect_put(struct nf_conntrack_expect *exp)
{
	if (atomic_dec_and_test(&exp->use))
		kmem_cache_free(nf_ct_expect_cachep, exp);
}
EXPORT_SYMBOL_GPL(nf_ct_expect_put);

static void nf_ct_expect_insert(struct nf_conntrack_expect *exp)
{
	struct nf_conn_help *master_help = nfct_help(exp->master);
	unsigned int h = nf_ct_expect_dst_hash(&exp->tuple);

	atomic_inc(&exp->use);

	hlist_add_head(&exp->lnode, &master_help->expectations);
	master_help->expecting++;

	hlist_add_head(&exp->hnode, &nf_ct_expect_hash[h]);
	nf_ct_expect_count++;

	setup_timer(&exp->timeout, nf_ct_expectation_timed_out,
		    (unsigned long)exp);
	exp->timeout.expires = jiffies + master_help->helper->timeout * HZ;
	add_timer(&exp->timeout);

	exp->id = ++nf_ct_expect_next_id;
	atomic_inc(&exp->use);
	NF_CT_STAT_INC(expect_create);
}

/* Race with expectations being used means we could have none to find; OK. */
static void evict_oldest_expect(struct nf_conn *master)
{
	struct nf_conn_help *master_help = nfct_help(master);
	struct nf_conntrack_expect *exp = NULL;
	struct hlist_node *n;

	hlist_for_each_entry(exp, n, &master_help->expectations, lnode)
		; /* nothing */

	if (exp && del_timer(&exp->timeout)) {
		nf_ct_unlink_expect(exp);
		nf_ct_expect_put(exp);
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

int nf_ct_expect_related(struct nf_conntrack_expect *expect)
{
	struct nf_conntrack_expect *i;
	struct nf_conn *master = expect->master;
	struct nf_conn_help *master_help = nfct_help(master);
	struct hlist_node *n;
	unsigned int h;
	int ret;

	NF_CT_ASSERT(master_help);

	write_lock_bh(&nf_conntrack_lock);
	if (!master_help->helper) {
		ret = -ESHUTDOWN;
		goto out;
	}
	h = nf_ct_expect_dst_hash(&expect->tuple);
	hlist_for_each_entry(i, n, &nf_ct_expect_hash[h], hnode) {
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

	if (nf_ct_expect_count >= nf_ct_expect_max) {
		if (net_ratelimit())
			printk(KERN_WARNING
			       "nf_conntrack: expectation table full");
		ret = -EMFILE;
		goto out;
	}

	nf_ct_expect_insert(expect);
	nf_ct_expect_event(IPEXP_NEW, expect);
	ret = 0;
out:
	write_unlock_bh(&nf_conntrack_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(nf_ct_expect_related);

#ifdef CONFIG_PROC_FS
struct ct_expect_iter_state {
	unsigned int bucket;
};

static struct hlist_node *ct_expect_get_first(struct seq_file *seq)
{
	struct ct_expect_iter_state *st = seq->private;

	for (st->bucket = 0; st->bucket < nf_ct_expect_hsize; st->bucket++) {
		if (!hlist_empty(&nf_ct_expect_hash[st->bucket]))
			return nf_ct_expect_hash[st->bucket].first;
	}
	return NULL;
}

static struct hlist_node *ct_expect_get_next(struct seq_file *seq,
					     struct hlist_node *head)
{
	struct ct_expect_iter_state *st = seq->private;

	head = head->next;
	while (head == NULL) {
		if (++st->bucket >= nf_ct_expect_hsize)
			return NULL;
		head = nf_ct_expect_hash[st->bucket].first;
	}
	return head;
}

static struct hlist_node *ct_expect_get_idx(struct seq_file *seq, loff_t pos)
{
	struct hlist_node *head = ct_expect_get_first(seq);

	if (head)
		while (pos && (head = ct_expect_get_next(seq, head)))
			pos--;
	return pos ? NULL : head;
}

static void *exp_seq_start(struct seq_file *seq, loff_t *pos)
{
	read_lock_bh(&nf_conntrack_lock);
	return ct_expect_get_idx(seq, *pos);
}

static void *exp_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	(*pos)++;
	return ct_expect_get_next(seq, v);
}

static void exp_seq_stop(struct seq_file *seq, void *v)
{
	read_unlock_bh(&nf_conntrack_lock);
}

static int exp_seq_show(struct seq_file *s, void *v)
{
	struct nf_conntrack_expect *expect;
	struct hlist_node *n = v;

	expect = hlist_entry(n, struct nf_conntrack_expect, hnode);

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

static const struct seq_operations exp_seq_ops = {
	.start = exp_seq_start,
	.next = exp_seq_next,
	.stop = exp_seq_stop,
	.show = exp_seq_show
};

static int exp_open(struct inode *inode, struct file *file)
{
	struct seq_file *seq;
	struct ct_expect_iter_state *st;
	int ret;

	st = kmalloc(sizeof(struct ct_expect_iter_state), GFP_KERNEL);
	if (st == NULL)
		return -ENOMEM;
	ret = seq_open(file, &exp_seq_ops);
	if (ret)
		goto out_free;
	seq          = file->private_data;
	seq->private = st;
	memset(st, 0, sizeof(struct ct_expect_iter_state));
	return ret;
out_free:
	kfree(st);
	return ret;
}

static const struct file_operations exp_file_ops = {
	.owner   = THIS_MODULE,
	.open    = exp_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release_private,
};
#endif /* CONFIG_PROC_FS */

static int __init exp_proc_init(void)
{
#ifdef CONFIG_PROC_FS
	struct proc_dir_entry *proc;

	proc = proc_net_fops_create("nf_conntrack_expect", 0440, &exp_file_ops);
	if (!proc)
		return -ENOMEM;
#endif /* CONFIG_PROC_FS */
	return 0;
}

static void exp_proc_remove(void)
{
#ifdef CONFIG_PROC_FS
	proc_net_remove("nf_conntrack_expect");
#endif /* CONFIG_PROC_FS */
}

module_param_named(expect_hashsize, nf_ct_expect_hsize, uint, 0600);

int __init nf_conntrack_expect_init(void)
{
	int err = -ENOMEM;

	if (!nf_ct_expect_hsize) {
		nf_ct_expect_hsize = nf_conntrack_htable_size / 256;
		if (!nf_ct_expect_hsize)
			nf_ct_expect_hsize = 1;
	}
	nf_ct_expect_max = nf_ct_expect_hsize * 4;

	nf_ct_expect_hash = nf_ct_alloc_hashtable(&nf_ct_expect_hsize,
						  &nf_ct_expect_vmalloc);
	if (nf_ct_expect_hash == NULL)
		goto err1;

	nf_ct_expect_cachep = kmem_cache_create("nf_conntrack_expect",
					sizeof(struct nf_conntrack_expect),
					0, 0, NULL);
	if (!nf_ct_expect_cachep)
		goto err2;

	err = exp_proc_init();
	if (err < 0)
		goto err3;

	return 0;

err3:
	nf_ct_free_hashtable(nf_ct_expect_hash, nf_ct_expect_vmalloc,
			     nf_ct_expect_hsize);
err2:
	kmem_cache_destroy(nf_ct_expect_cachep);
err1:
	return err;
}

void nf_conntrack_expect_fini(void)
{
	exp_proc_remove();
	kmem_cache_destroy(nf_ct_expect_cachep);
	nf_ct_free_hashtable(nf_ct_expect_hash, nf_ct_expect_vmalloc,
			     nf_ct_expect_hsize);
}
