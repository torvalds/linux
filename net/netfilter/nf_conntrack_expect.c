// SPDX-License-Identifier: GPL-2.0-only
/* Expectation handling for nf_conntrack. */

/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2006 Netfilter Core Team <coreteam@netfilter.org>
 * (C) 2003,2004 USAGI/WIDE Project <http://www.linux-ipv6.org>
 * (c) 2005-2012 Patrick McHardy <kaber@trash.net>
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
#include <linux/siphash.h>
#include <linux/moduleparam.h>
#include <linux/export.h>
#include <net/net_namespace.h>
#include <net/netns/hash.h>

#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_conntrack_ecache.h>
#include <net/netfilter/nf_conntrack_expect.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <net/netfilter/nf_conntrack_l4proto.h>
#include <net/netfilter/nf_conntrack_tuple.h>
#include <net/netfilter/nf_conntrack_zones.h>

unsigned int nf_ct_expect_hsize __read_mostly;
EXPORT_SYMBOL_GPL(nf_ct_expect_hsize);

struct hlist_head *nf_ct_expect_hash __read_mostly;
EXPORT_SYMBOL_GPL(nf_ct_expect_hash);

unsigned int nf_ct_expect_max __read_mostly;

static struct kmem_cache *nf_ct_expect_cachep __read_mostly;
static siphash_aligned_key_t nf_ct_expect_hashrnd;

/* nf_conntrack_expect helper functions */
void nf_ct_unlink_expect_report(struct nf_conntrack_expect *exp,
				u32 portid, int report)
{
	struct nf_conn_help *master_help = nfct_help(exp->master);
	struct net *net = nf_ct_exp_net(exp);
	struct nf_conntrack_net *cnet;

	WARN_ON(!master_help);
	WARN_ON(timer_pending(&exp->timeout));

	hlist_del_rcu(&exp->hnode);

	cnet = nf_ct_pernet(net);
	cnet->expect_count--;

	hlist_del_rcu(&exp->lnode);
	master_help->expecting[exp->class]--;

	nf_ct_expect_event_report(IPEXP_DESTROY, exp, portid, report);
	nf_ct_expect_put(exp);

	NF_CT_STAT_INC(net, expect_delete);
}
EXPORT_SYMBOL_GPL(nf_ct_unlink_expect_report);

static void nf_ct_expectation_timed_out(struct timer_list *t)
{
	struct nf_conntrack_expect *exp = from_timer(exp, t, timeout);

	spin_lock_bh(&nf_conntrack_expect_lock);
	nf_ct_unlink_expect(exp);
	spin_unlock_bh(&nf_conntrack_expect_lock);
	nf_ct_expect_put(exp);
}

static unsigned int nf_ct_expect_dst_hash(const struct net *n, const struct nf_conntrack_tuple *tuple)
{
	struct {
		union nf_inet_addr dst_addr;
		u32 net_mix;
		u16 dport;
		u8 l3num;
		u8 protonum;
	} __aligned(SIPHASH_ALIGNMENT) combined;
	u32 hash;

	get_random_once(&nf_ct_expect_hashrnd, sizeof(nf_ct_expect_hashrnd));

	memset(&combined, 0, sizeof(combined));

	combined.dst_addr = tuple->dst.u3;
	combined.net_mix = net_hash_mix(n);
	combined.dport = (__force __u16)tuple->dst.u.all;
	combined.l3num = tuple->src.l3num;
	combined.protonum = tuple->dst.protonum;

	hash = siphash(&combined, sizeof(combined), &nf_ct_expect_hashrnd);

	return reciprocal_scale(hash, nf_ct_expect_hsize);
}

static bool
nf_ct_exp_equal(const struct nf_conntrack_tuple *tuple,
		const struct nf_conntrack_expect *i,
		const struct nf_conntrack_zone *zone,
		const struct net *net)
{
	return nf_ct_tuple_mask_cmp(tuple, &i->tuple, &i->mask) &&
	       net_eq(net, nf_ct_net(i->master)) &&
	       nf_ct_zone_equal_any(i->master, zone);
}

bool nf_ct_remove_expect(struct nf_conntrack_expect *exp)
{
	if (del_timer(&exp->timeout)) {
		nf_ct_unlink_expect(exp);
		nf_ct_expect_put(exp);
		return true;
	}
	return false;
}
EXPORT_SYMBOL_GPL(nf_ct_remove_expect);

struct nf_conntrack_expect *
__nf_ct_expect_find(struct net *net,
		    const struct nf_conntrack_zone *zone,
		    const struct nf_conntrack_tuple *tuple)
{
	struct nf_conntrack_net *cnet = nf_ct_pernet(net);
	struct nf_conntrack_expect *i;
	unsigned int h;

	if (!cnet->expect_count)
		return NULL;

	h = nf_ct_expect_dst_hash(net, tuple);
	hlist_for_each_entry_rcu(i, &nf_ct_expect_hash[h], hnode) {
		if (nf_ct_exp_equal(tuple, i, zone, net))
			return i;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(__nf_ct_expect_find);

/* Just find a expectation corresponding to a tuple. */
struct nf_conntrack_expect *
nf_ct_expect_find_get(struct net *net,
		      const struct nf_conntrack_zone *zone,
		      const struct nf_conntrack_tuple *tuple)
{
	struct nf_conntrack_expect *i;

	rcu_read_lock();
	i = __nf_ct_expect_find(net, zone, tuple);
	if (i && !refcount_inc_not_zero(&i->use))
		i = NULL;
	rcu_read_unlock();

	return i;
}
EXPORT_SYMBOL_GPL(nf_ct_expect_find_get);

/* If an expectation for this connection is found, it gets delete from
 * global list then returned. */
struct nf_conntrack_expect *
nf_ct_find_expectation(struct net *net,
		       const struct nf_conntrack_zone *zone,
		       const struct nf_conntrack_tuple *tuple)
{
	struct nf_conntrack_net *cnet = nf_ct_pernet(net);
	struct nf_conntrack_expect *i, *exp = NULL;
	unsigned int h;

	if (!cnet->expect_count)
		return NULL;

	h = nf_ct_expect_dst_hash(net, tuple);
	hlist_for_each_entry(i, &nf_ct_expect_hash[h], hnode) {
		if (!(i->flags & NF_CT_EXPECT_INACTIVE) &&
		    nf_ct_exp_equal(tuple, i, zone, net)) {
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

	/* Avoid race with other CPUs, that for exp->master ct, is
	 * about to invoke ->destroy(), or nf_ct_delete() via timeout
	 * or early_drop().
	 *
	 * The refcount_inc_not_zero() check tells:  If that fails, we
	 * know that the ct is being destroyed.  If it succeeds, we
	 * can be sure the ct cannot disappear underneath.
	 */
	if (unlikely(nf_ct_is_dying(exp->master) ||
		     !refcount_inc_not_zero(&exp->master->ct_general.use)))
		return NULL;

	if (exp->flags & NF_CT_EXPECT_PERMANENT) {
		refcount_inc(&exp->use);
		return exp;
	} else if (del_timer(&exp->timeout)) {
		nf_ct_unlink_expect(exp);
		return exp;
	}
	/* Undo exp->master refcnt increase, if del_timer() failed */
	nf_ct_put(exp->master);

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

	spin_lock_bh(&nf_conntrack_expect_lock);
	hlist_for_each_entry_safe(exp, next, &help->expectations, lnode) {
		nf_ct_remove_expect(exp);
	}
	spin_unlock_bh(&nf_conntrack_expect_lock);
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
	       net_eq(nf_ct_net(a->master), nf_ct_net(b->master)) &&
	       nf_ct_zone_equal_any(a->master, nf_ct_zone(b->master));
}

static inline int expect_matches(const struct nf_conntrack_expect *a,
				 const struct nf_conntrack_expect *b)
{
	return nf_ct_tuple_equal(&a->tuple, &b->tuple) &&
	       nf_ct_tuple_mask_equal(&a->mask, &b->mask) &&
	       net_eq(nf_ct_net(a->master), nf_ct_net(b->master)) &&
	       nf_ct_zone_equal_any(a->master, nf_ct_zone(b->master));
}

static bool master_matches(const struct nf_conntrack_expect *a,
			   const struct nf_conntrack_expect *b,
			   unsigned int flags)
{
	if (flags & NF_CT_EXP_F_SKIP_MASTER)
		return true;

	return a->master == b->master;
}

/* Generally a bad idea to call this: could have matched already. */
void nf_ct_unexpect_related(struct nf_conntrack_expect *exp)
{
	spin_lock_bh(&nf_conntrack_expect_lock);
	nf_ct_remove_expect(exp);
	spin_unlock_bh(&nf_conntrack_expect_lock);
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
	refcount_set(&new->use, 1);
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

#if IS_ENABLED(CONFIG_NF_NAT)
	memset(&exp->saved_addr, 0, sizeof(exp->saved_addr));
	memset(&exp->saved_proto, 0, sizeof(exp->saved_proto));
#endif
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
	if (refcount_dec_and_test(&exp->use))
		call_rcu(&exp->rcu, nf_ct_expect_free_rcu);
}
EXPORT_SYMBOL_GPL(nf_ct_expect_put);

static void nf_ct_expect_insert(struct nf_conntrack_expect *exp)
{
	struct nf_conntrack_net *cnet;
	struct nf_conn_help *master_help = nfct_help(exp->master);
	struct nf_conntrack_helper *helper;
	struct net *net = nf_ct_exp_net(exp);
	unsigned int h = nf_ct_expect_dst_hash(net, &exp->tuple);

	/* two references : one for hash insert, one for the timer */
	refcount_add(2, &exp->use);

	timer_setup(&exp->timeout, nf_ct_expectation_timed_out, 0);
	helper = rcu_dereference_protected(master_help->helper,
					   lockdep_is_held(&nf_conntrack_expect_lock));
	if (helper) {
		exp->timeout.expires = jiffies +
			helper->expect_policy[exp->class].timeout * HZ;
	}
	add_timer(&exp->timeout);

	hlist_add_head_rcu(&exp->lnode, &master_help->expectations);
	master_help->expecting[exp->class]++;

	hlist_add_head_rcu(&exp->hnode, &nf_ct_expect_hash[h]);
	cnet = nf_ct_pernet(net);
	cnet->expect_count++;

	NF_CT_STAT_INC(net, expect_create);
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

	if (last)
		nf_ct_remove_expect(last);
}

static inline int __nf_ct_expect_check(struct nf_conntrack_expect *expect,
				       unsigned int flags)
{
	const struct nf_conntrack_expect_policy *p;
	struct nf_conntrack_expect *i;
	struct nf_conntrack_net *cnet;
	struct nf_conn *master = expect->master;
	struct nf_conn_help *master_help = nfct_help(master);
	struct nf_conntrack_helper *helper;
	struct net *net = nf_ct_exp_net(expect);
	struct hlist_node *next;
	unsigned int h;
	int ret = 0;

	if (!master_help) {
		ret = -ESHUTDOWN;
		goto out;
	}
	h = nf_ct_expect_dst_hash(net, &expect->tuple);
	hlist_for_each_entry_safe(i, next, &nf_ct_expect_hash[h], hnode) {
		if (master_matches(i, expect, flags) &&
		    expect_matches(i, expect)) {
			if (i->class != expect->class ||
			    i->master != expect->master)
				return -EALREADY;

			if (nf_ct_remove_expect(i))
				break;
		} else if (expect_clash(i, expect)) {
			ret = -EBUSY;
			goto out;
		}
	}
	/* Will be over limit? */
	helper = rcu_dereference_protected(master_help->helper,
					   lockdep_is_held(&nf_conntrack_expect_lock));
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

	cnet = nf_ct_pernet(net);
	if (cnet->expect_count >= nf_ct_expect_max) {
		net_warn_ratelimited("nf_conntrack: expectation table full\n");
		ret = -EMFILE;
	}
out:
	return ret;
}

int nf_ct_expect_related_report(struct nf_conntrack_expect *expect,
				u32 portid, int report, unsigned int flags)
{
	int ret;

	spin_lock_bh(&nf_conntrack_expect_lock);
	ret = __nf_ct_expect_check(expect, flags);
	if (ret < 0)
		goto out;

	nf_ct_expect_insert(expect);

	spin_unlock_bh(&nf_conntrack_expect_lock);
	nf_ct_expect_event_report(IPEXP_NEW, expect, portid, report);
	return 0;
out:
	spin_unlock_bh(&nf_conntrack_expect_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(nf_ct_expect_related_report);

void nf_ct_expect_iterate_destroy(bool (*iter)(struct nf_conntrack_expect *e, void *data),
				  void *data)
{
	struct nf_conntrack_expect *exp;
	const struct hlist_node *next;
	unsigned int i;

	spin_lock_bh(&nf_conntrack_expect_lock);

	for (i = 0; i < nf_ct_expect_hsize; i++) {
		hlist_for_each_entry_safe(exp, next,
					  &nf_ct_expect_hash[i],
					  hnode) {
			if (iter(exp, data) && del_timer(&exp->timeout)) {
				nf_ct_unlink_expect(exp);
				nf_ct_expect_put(exp);
			}
		}
	}

	spin_unlock_bh(&nf_conntrack_expect_lock);
}
EXPORT_SYMBOL_GPL(nf_ct_expect_iterate_destroy);

void nf_ct_expect_iterate_net(struct net *net,
			      bool (*iter)(struct nf_conntrack_expect *e, void *data),
			      void *data,
			      u32 portid, int report)
{
	struct nf_conntrack_expect *exp;
	const struct hlist_node *next;
	unsigned int i;

	spin_lock_bh(&nf_conntrack_expect_lock);

	for (i = 0; i < nf_ct_expect_hsize; i++) {
		hlist_for_each_entry_safe(exp, next,
					  &nf_ct_expect_hash[i],
					  hnode) {

			if (!net_eq(nf_ct_exp_net(exp), net))
				continue;

			if (iter(exp, data) && del_timer(&exp->timeout)) {
				nf_ct_unlink_expect_report(exp, portid, report);
				nf_ct_expect_put(exp);
			}
		}
	}

	spin_unlock_bh(&nf_conntrack_expect_lock);
}
EXPORT_SYMBOL_GPL(nf_ct_expect_iterate_net);

#ifdef CONFIG_NF_CONNTRACK_PROCFS
struct ct_expect_iter_state {
	struct seq_net_private p;
	unsigned int bucket;
};

static struct hlist_node *ct_expect_get_first(struct seq_file *seq)
{
	struct ct_expect_iter_state *st = seq->private;
	struct hlist_node *n;

	for (st->bucket = 0; st->bucket < nf_ct_expect_hsize; st->bucket++) {
		n = rcu_dereference(hlist_first_rcu(&nf_ct_expect_hash[st->bucket]));
		if (n)
			return n;
	}
	return NULL;
}

static struct hlist_node *ct_expect_get_next(struct seq_file *seq,
					     struct hlist_node *head)
{
	struct ct_expect_iter_state *st = seq->private;

	head = rcu_dereference(hlist_next_rcu(head));
	while (head == NULL) {
		if (++st->bucket >= nf_ct_expect_hsize)
			return NULL;
		head = rcu_dereference(hlist_first_rcu(&nf_ct_expect_hash[st->bucket]));
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
		seq_puts(s, "- ");
	seq_printf(s, "l3proto = %u proto=%u ",
		   expect->tuple.src.l3num,
		   expect->tuple.dst.protonum);
	print_tuple(s, &expect->tuple,
		    nf_ct_l4proto_find(expect->tuple.dst.protonum));

	if (expect->flags & NF_CT_EXPECT_PERMANENT) {
		seq_puts(s, "PERMANENT");
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
		if (helper->expect_policy[expect->class].name[0])
			seq_printf(s, "/%s",
				   helper->expect_policy[expect->class].name);
	}

	seq_putc(s, '\n');

	return 0;
}

static const struct seq_operations exp_seq_ops = {
	.start = exp_seq_start,
	.next = exp_seq_next,
	.stop = exp_seq_stop,
	.show = exp_seq_show
};
#endif /* CONFIG_NF_CONNTRACK_PROCFS */

static int exp_proc_init(struct net *net)
{
#ifdef CONFIG_NF_CONNTRACK_PROCFS
	struct proc_dir_entry *proc;
	kuid_t root_uid;
	kgid_t root_gid;

	proc = proc_create_net("nf_conntrack_expect", 0440, net->proc_net,
			&exp_seq_ops, sizeof(struct ct_expect_iter_state));
	if (!proc)
		return -ENOMEM;

	root_uid = make_kuid(net->user_ns, 0);
	root_gid = make_kgid(net->user_ns, 0);
	if (uid_valid(root_uid) && gid_valid(root_gid))
		proc_set_user(proc, root_uid, root_gid);
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
	return exp_proc_init(net);
}

void nf_conntrack_expect_pernet_fini(struct net *net)
{
	exp_proc_remove(net);
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

	nf_ct_expect_hash = nf_ct_alloc_hashtable(&nf_ct_expect_hsize, 0);
	if (!nf_ct_expect_hash) {
		kmem_cache_destroy(nf_ct_expect_cachep);
		return -ENOMEM;
	}

	return 0;
}

void nf_conntrack_expect_fini(void)
{
	rcu_barrier(); /* Wait for call_rcu() before destroy */
	kmem_cache_destroy(nf_ct_expect_cachep);
	kvfree(nf_ct_expect_hash);
}
