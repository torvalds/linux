/* Expectation handling for nf_conntrack. */

/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2006 Netfilter Core Team <coreteam@netfilter.org>
 * (C) 2003,2004 USAGI/WIDE Project <http://www.linux-ipv6.org>
 * (c) 2005-2012 Patrick McHardy <kaber@trash.net>
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
#include <linux/moduleparam.h>
#include <linux/export.h>
#include <net/net_namespace.h>

#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_conntrack_expect.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <net/netfilter/nf_conntrack_tuple.h>
#include <net/netfilter/nf_conntrack_zones.h>

unsigned int nf_ct_expect_hsize __read_mostly;
EXPORT_SYMBOL_GPL(nf_ct_expect_hsize);

unsigned int nf_ct_expect_max __read_mostly;

static struct kmem_cache *nf_ct_expect_cachep __read_mostly;

/* nf_conntrack_expect helper functions */
void nf_ct_unlink_expect_report(struct nf_conntrack_expect *exp,
				u32 portid, int report)
{
	struct nf_conn_help *master_help = nfct_help(exp->master);
	struct net *net = nf_ct_exp_net(exp);

	NF_CT_ASSERT(master_help);
	NF_CT_ASSERT(!timer_pending(&exp->timeout));

	hlist_del_rcu(&exp->hnode);
	net->ct.expect_count--;

	hlist_del(&exp->lnode);
	master_help->expecting[exp->class]--;

	nf_ct_expect_event_report(IPEXP_DESTROY, exp, portid, report);
	nf_ct_expect_put(exp);

	NF_CT_STAT_INC(net, expect_delete);
}
EXPORT_SYMBOL_GPL(nf_ct_unlink_expect_report);

static void nf_ct_expectation_timed_out(unsigned long ul_expect)
{
	struct nf_conntrack_expect *exp = (void *)ul_expect;

	spin_lock_bh(&nf_conntrack_lock);
	nf_ct_unlink_expect(exp);
	spin_unlock_bh(&nf_conntrack_lock);
	nf_ct_expect_put(exp);
}

static unsigned int nf_ct_expect_dst_hash(const struct nf_conntrack_tuple *tuple)
{
	unsigned int hash;

	if (unlikely(!nf_conntrack_hash_rnd)) {
		init_nf_conntrack_hash_rnd();
	}

	hash = jhash2(tuple->dst.u3.all, ARRAY_SIZE(tuple->dst.u3.all),
		      (((tuple->dst.protonum ^ tuple->src.l3num) << 16) |
		       (__force __u16)tuple->dst.u.all) ^ nf_conntrack_hash_rnd);
	return ((u64)hash * nf_ct_expect_hsize) >> 32;
}

struct nf_conntrack_expect *
__nf_ct_expect_find(struct net *net, u16 zone,
		    const struct nf_conntrack_tuple *tuple)
{
	struct nf_conntrack_expect *i;
	unsigned int h;

	if (!net->ct.expect_count)
		return NULL;

	h = nf_ct_expect_dst_hash(tuple);
	hlist_for_each_entry_rcu(i, &net->ct.expect_hash[h], hnode) {
		if (nf_ct_tuple_mask_cmp(tuple, &i->tuple, &i->mask) &&
		    nf_ct_zone(i->master) == zone)
			return i;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(__nf_ct_expect_find);

/* Just find a expectation corresponding to a tuple. */
struct nf_conntrack_expect *
nf_ct_expect_find_get(struct net *net, u16 zone,
		      const struct nf_conntrack_tuple *tuple)
{
	struct nf_conntrack_expect *i;

	rcu_read_lock();
	i = __nf_ct_expect_find(net, zone, tuple);
	if (i && !atomic_inc_not_zero(&i->use))
		i = NULL;
	rcu_read_unlock();

	return i;
}
EXPORT_SYMBOL_GPL(nf_ct_expect_find_get);

/* If an expectation for this connection is found, it gets delete from
 * global list then returned. */
struct nf_conntrack_expect *
nf_ct_find_expectation(struct net *net, u16 zone,
		       const struct nf_conntrack_tuple *tuple)
{
	struct nf_conntrack_expect *i, *exp = NULL;
	unsigned int h;

	if (!net->ct.expect_count)
		return NULL;

	h = nf_ct_expect_dst_hash(tuple);
	hlist_for_each_entry(i, &net->ct.expect_hash[h], hnode) {
		if (!(i->flags & NF_CT_EXPECT_INACTIVE) &&
		    nf_ct_tuple_mask_cmp(tuple, &i->tuple, &i->mask) &&
		    nf_ct_zone(i->master) == zone) {
			exp = i;
			break;
		}
	}
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
	struct hlist_node *next;

	/* Optimization: most connection never expect any others. */
	if (!help)
		return;

	hlist_for_each_entry_safe(exp, next, &help->expectations, lnode) {
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

	return nf_ct_tuple_mask_cmp(&a->tuple, &b->tuple, &intersect_mask) &&
	       nf_ct_zone(a->master) == nf_ct_zone(b->master);
}

static inline int expect_matches(const struct nf_conntrack_expect *a,
				 const struct nf_conntrack_expect *b)
{
	return a->master == b->master && a->class == b->class &&
		nf_ct_tuple_equal(&a->tuple, &b->tuple) &&
		nf_ct_tuple_mask_equal(&a->mask, &b->mask) &&
		nf_ct_zone(a->master) == nf_ct_zone(b->master);
}

/* Generally a bad idea to call this: could have matched already. */
void nf_ct_unexpect_related(struct nf_conntrack_expect *exp)
{
	spin_lock_bh(&nf_conntrack_lock);
	if (del_timer(&exp->timeout)) {
		nf_ct_unlink_expect(exp);
		nf_ct_expect_put(exp);
	}
	spin_unlock_bh(&nf_conntrack_lock);
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

void nf_ct_expect_init(struct nf_conntrack_expect *exp, unsigned int class,
		       u_int8_t family,
		       const union nf_inet_addr *saddr,
		       const union nf_inet_addr *daddr,
		       u_int8_t proto, const __be16 *src, const __be16 *dst)
{
	int len;

	if (family == AF_INET)
		len = 4;
	else
		len = 16;

	exp->flags = 0;
	exp->class = class;
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

static void nf_ct_expect_free_rcu(struct rcu_head *head)
{
	struct nf_conntrack_expect *exp;

	exp = container_of(head, struct nf_conntrack_expect, rcu);
	kmem_cache_free(nf_ct_expect_cachep, exp);
}

void nf_ct_expect_put(struct nf_conntrack_expect *exp)
{
	if (atomic_dec_and_test(&exp->use))
		call_rcu(&exp->rcu, nf_ct_expect_free_rcu);
}
EXPORT_SYMBOL_GPL(nf_ct_expect_put);

static int nf_ct_expect_insert(struct nf_conntrack_expect *exp)
{
	struct nf_conn_help *master_help = nfct_help(exp->master);
	struct nf_conntrack_helper *helper;
	struct net *net = nf_ct_exp_net(exp);
	unsigned int h = nf_ct_expect_dst_hash(&exp->tuple);

	/* two references : one for hash insert, one for the timer */
	atomic_add(2, &exp->use);

	hlist_add_head(&exp->lnode, &master_help->expectations);
	master_help->expecting[exp->class]++;

	hlist_add_head_rcu(&exp->hnode, &net->ct.expect_hash[h]);
	net->ct.expect_count++;

	setup_timer(&exp->timeout, nf_ct_expectation_timed_out,
		    (unsigned long)exp);
	helper = rcu_dereference_protected(master_help->helper,
					   lockdep_is_held(&nf_conntrack_lock));
	if (helper) {
		exp->timeout.expires = jiffies +
			helper->expect_policy[exp->class].timeout * HZ;
	}
	add_timer(&exp->timeout);

	NF_CT_STAT_INC(net, expect_create);
	return 0;
}

/* Race with expectations being used means we could have none to find; OK. */
static void evict_oldest_expect(struct nf_conn *master,
				struct nf_conntrack_expect *new)
{
	struct nf_conn_help *master_help = nfct_help(master);
	struct nf_conntrack_expect *exp, *last = NULL;

	hlist_for_each_entry(exp, &master_help->expectations, lnode) {
		if (exp->class == new->class)
			last = exp;
	}

	if (last && del_timer(&last->timeout)) {
		nf_ct_unlink_expect(last);
		nf_ct_expect_put(last);
	}
}

static inline int __nf_ct_expect_check(struct nf_conntrack_expect *expect)
{
	const struct nf_conntrack_expect_policy *p;
	struct nf_conntrack_expect *i;
	struct nf_conn *master = expect->master;
	struct nf_conn_help *master_help = nfct_help(master);
	struct nf_conntrack_helper *helper;
	struct net *net = nf_ct_exp_net(expect);
	struct hlist_node *next;
	unsigned int h;
	int ret = 1;

	if (!master_help) {
		ret = -ESHUTDOWN;
		goto out;
	}
	h = nf_ct_expect_dst_hash(&expect->tuple);
	hlist_for_each_entry_safe(i, next, &net->ct.expect_hash[h], hnode) {
		if (expect_matches(i, expect)) {
			if (del_timer(&i->timeout)) {
				nf_ct_unlink_expect(i);
				nf_ct_expect_put(i);
				break;
			}
		} else if (expect_clash(i, expect)) {
			ret = -EBUSY;
			goto out;
		}
	}
	/* Will be over limit? */
	helper = rcu_dereference_protected(master_help->helper,
					   lockdep_is_held(&nf_conntrack_lock));
	if (helper) {
		p = &helper->expect_policy[expect->class];
		if (p->max_expected &&
		    master_help->expecting[expect->class] >= p->max_expected) {
			evict_oldest_expect(master, expect);
			if (master_help->expecting[expect->class]
						>= p->max_expected) {
				ret = -EMFILE;
				goto out;
			}
		}
	}

	if (net->ct.expect_count >= nf_ct_expect_max) {
		net_warn_ratelimited("nf_conntrack: expectation table full\n");
		ret = -EMFILE;
	}
out:
	return ret;
}

int nf_ct_expect_related_report(struct nf_conntrack_expect *expect, 
				u32 portid, int report)
{
	int ret;

	spin_lock_bh(&nf_conntrack_lock);
	ret = __nf_ct_expect_check(expect);
	if (ret <= 0)
		goto out;

	ret = nf_ct_expect_insert(expect);
	if (ret < 0)
		goto out;
	spin_unlock_bh(&nf_conntrack_lock);
	nf_ct_expect_event_report(IPEXP_NEW, expect, portid, report);
	return ret;
out:
	spin_unlock_bh(&nf_conntrack_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(nf_ct_expect_related_report);

#ifdef CONFIG_NF_CONNTRACK_PROCFS
struct ct_expect_iter_state {
	struct seq_net_private p;
	unsigned int bucket;
};

static struct hlist_node *ct_expect_get_first(struct seq_file *seq)
{
	struct net *net = seq_file_net(seq);
	struct ct_expect_iter_state *st = seq->private;
	struct hlist_node *n;

	for (st->bucket = 0; st->bucket < nf_ct_expect_hsize; st->bucket++) {
		n = rcu_dereference(hlist_first_rcu(&net->ct.expect_hash[st->bucket]));
		if (n)
			return n;
	}
	return NULL;
}

static struct hlist_node *ct_expect_get_next(struct seq_file *seq,
					     struct hlist_node *head)
{
	struct net *net = seq_file_net(seq);
	struct ct_expect_iter_state *st = seq->private;

	head = rcu_dereference(hlist_next_rcu(head));
	while (head == NULL) {
		if (++st->bucket >= nf_ct_expect_hsize)
			return NULL;
		head = rcu_dereference(hlist_first_rcu(&net->ct.expect_hash[st->bucket]));
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
	__acquires(RCU)
{
	rcu_read_lock();
	return ct_expect_get_idx(seq, *pos);
}

static void *exp_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	(*pos)++;
	return ct_expect_get_next(seq, v);
}

static void exp_seq_stop(struct seq_file *seq, void *v)
	__releases(RCU)
{
	rcu_read_unlock();
}

static int exp_seq_show(struct seq_file *s, void *v)
{
	struct nf_conntrack_expect *expect;
	struct nf_conntrack_helper *helper;
	struct hlist_node *n = v;
	char *delim = "";

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

	if (expect->flags & NF_CT_EXPECT_PERMANENT) {
		seq_printf(s, "PERMANENT");
		delim = ",";
	}
	if (expect->flags & NF_CT_EXPECT_INACTIVE) {
		seq_printf(s, "%sINACTIVE", delim);
		delim = ",";
	}
	if (expect->flags & NF_CT_EXPECT_USERSPACE)
		seq_printf(s, "%sUSERSPACE", delim);

	helper = rcu_dereference(nfct_help(expect->master)->helper);
	if (helper) {
		seq_printf(s, "%s%s", expect->flags ? " " : "", helper->name);
		if (helper->expect_policy[expect->class].name)
			seq_printf(s, "/%s",
				   helper->expect_policy[expect->class].name);
	}

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
	return seq_open_net(inode, file, &exp_seq_ops,
			sizeof(struct ct_expect_iter_state));
}

static const struct file_operations exp_file_ops = {
	.owner   = THIS_MODULE,
	.open    = exp_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release_net,
};
#endif /* CONFIG_NF_CONNTRACK_PROCFS */

static int exp_proc_init(struct net *net)
{
#ifdef CONFIG_NF_CONNTRACK_PROCFS
	struct proc_dir_entry *proc;

	proc = proc_create("nf_conntrack_expect", 0440, net->proc_net,
			   &exp_file_ops);
	if (!proc)
		return -ENOMEM;
#endif /* CONFIG_NF_CONNTRACK_PROCFS */
	return 0;
}

static void exp_proc_remove(struct net *net)
{
#ifdef CONFIG_NF_CONNTRACK_PROCFS
	remove_proc_entry("nf_conntrack_expect", net->proc_net);
#endif /* CONFIG_NF_CONNTRACK_PROCFS */
}

module_param_named(expect_hashsize, nf_ct_expect_hsize, uint, 0400);

int nf_conntrack_expect_pernet_init(struct net *net)
{
	int err = -ENOMEM;

	net->ct.expect_count = 0;
	net->ct.expect_hash = nf_ct_alloc_hashtable(&nf_ct_expect_hsize, 0);
	if (net->ct.expect_hash == NULL)
		goto err1;

	err = exp_proc_init(net);
	if (err < 0)
		goto err2;

	return 0;
err2:
	nf_ct_free_hashtable(net->ct.expect_hash, nf_ct_expect_hsize);
err1:
	return err;
}

void nf_conntrack_expect_pernet_fini(struct net *net)
{
	exp_proc_remove(net);
	nf_ct_free_hashtable(net->ct.expect_hash, nf_ct_expect_hsize);
}

int nf_conntrack_expect_init(void)
{
	if (!nf_ct_expect_hsize) {
		nf_ct_expect_hsize = nf_conntrack_htable_size / 256;
		if (!nf_ct_expect_hsize)
			nf_ct_expect_hsize = 1;
	}
	nf_ct_expect_max = nf_ct_expect_hsize * 4;
	nf_ct_expect_cachep = kmem_cache_create("nf_conntrack_expect",
				sizeof(struct nf_conntrack_expect),
				0, 0, NULL);
	if (!nf_ct_expect_cachep)
		return -ENOMEM;
	return 0;
}

void nf_conntrack_expect_fini(void)
{
	rcu_barrier(); /* Wait for call_rcu() before destroy */
	kmem_cache_destroy(nf_ct_expect_cachep);
}
