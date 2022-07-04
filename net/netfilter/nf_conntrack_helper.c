// SPDX-License-Identifier: GPL-2.0-only
/* Helper handling for netfilter. */

/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2006 Netfilter Core Team <coreteam@netfilter.org>
 * (C) 2003,2004 USAGI/WIDE Project <http://www.linux-ipv6.org>
 * (C) 2006-2012 Patrick McHardy <kaber@trash.net>
 */

#include <linux/types.h>
#include <linux/netfilter.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/vmalloc.h>
#include <linux/stddef.h>
#include <linux/random.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/rculist.h>
#include <linux/rtnetlink.h>

#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_conntrack_ecache.h>
#include <net/netfilter/nf_conntrack_extend.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <net/netfilter/nf_conntrack_l4proto.h>
#include <net/netfilter/nf_log.h>

static DEFINE_MUTEX(nf_ct_helper_mutex);
struct hlist_head *nf_ct_helper_hash __read_mostly;
EXPORT_SYMBOL_GPL(nf_ct_helper_hash);
unsigned int nf_ct_helper_hsize __read_mostly;
EXPORT_SYMBOL_GPL(nf_ct_helper_hsize);
static unsigned int nf_ct_helper_count __read_mostly;

static bool nf_ct_auto_assign_helper __read_mostly = false;
module_param_named(nf_conntrack_helper, nf_ct_auto_assign_helper, bool, 0644);
MODULE_PARM_DESC(nf_conntrack_helper,
		 "Enable automatic conntrack helper assignment (default 0)");

static DEFINE_MUTEX(nf_ct_nat_helpers_mutex);
static struct list_head nf_ct_nat_helpers __read_mostly;

/* Stupid hash, but collision free for the default registrations of the
 * helpers currently in the kernel. */
static unsigned int helper_hash(const struct nf_conntrack_tuple *tuple)
{
	return (((tuple->src.l3num << 8) | tuple->dst.protonum) ^
		(__force __u16)tuple->src.u.all) % nf_ct_helper_hsize;
}

static struct nf_conntrack_helper *
__nf_ct_helper_find(const struct nf_conntrack_tuple *tuple)
{
	struct nf_conntrack_helper *helper;
	struct nf_conntrack_tuple_mask mask = { .src.u.all = htons(0xFFFF) };
	unsigned int h;

	if (!nf_ct_helper_count)
		return NULL;

	h = helper_hash(tuple);
	hlist_for_each_entry_rcu(helper, &nf_ct_helper_hash[h], hnode) {
		if (nf_ct_tuple_src_mask_cmp(tuple, &helper->tuple, &mask))
			return helper;
	}
	return NULL;
}

struct nf_conntrack_helper *
__nf_conntrack_helper_find(const char *name, u16 l3num, u8 protonum)
{
	struct nf_conntrack_helper *h;
	unsigned int i;

	for (i = 0; i < nf_ct_helper_hsize; i++) {
		hlist_for_each_entry_rcu(h, &nf_ct_helper_hash[i], hnode) {
			if (strcmp(h->name, name))
				continue;

			if (h->tuple.src.l3num != NFPROTO_UNSPEC &&
			    h->tuple.src.l3num != l3num)
				continue;

			if (h->tuple.dst.protonum == protonum)
				return h;
		}
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(__nf_conntrack_helper_find);

struct nf_conntrack_helper *
nf_conntrack_helper_try_module_get(const char *name, u16 l3num, u8 protonum)
{
	struct nf_conntrack_helper *h;

	rcu_read_lock();

	h = __nf_conntrack_helper_find(name, l3num, protonum);
#ifdef CONFIG_MODULES
	if (h == NULL) {
		rcu_read_unlock();
		if (request_module("nfct-helper-%s", name) == 0) {
			rcu_read_lock();
			h = __nf_conntrack_helper_find(name, l3num, protonum);
		} else {
			return h;
		}
	}
#endif
	if (h != NULL && !try_module_get(h->me))
		h = NULL;
	if (h != NULL && !refcount_inc_not_zero(&h->refcnt)) {
		module_put(h->me);
		h = NULL;
	}

	rcu_read_unlock();

	return h;
}
EXPORT_SYMBOL_GPL(nf_conntrack_helper_try_module_get);

void nf_conntrack_helper_put(struct nf_conntrack_helper *helper)
{
	refcount_dec(&helper->refcnt);
	module_put(helper->me);
}
EXPORT_SYMBOL_GPL(nf_conntrack_helper_put);

static struct nf_conntrack_nat_helper *
nf_conntrack_nat_helper_find(const char *mod_name)
{
	struct nf_conntrack_nat_helper *cur;
	bool found = false;

	list_for_each_entry_rcu(cur, &nf_ct_nat_helpers, list) {
		if (!strcmp(cur->mod_name, mod_name)) {
			found = true;
			break;
		}
	}
	return found ? cur : NULL;
}

int
nf_nat_helper_try_module_get(const char *name, u16 l3num, u8 protonum)
{
	struct nf_conntrack_helper *h;
	struct nf_conntrack_nat_helper *nat;
	char mod_name[NF_CT_HELPER_NAME_LEN];
	int ret = 0;

	rcu_read_lock();
	h = __nf_conntrack_helper_find(name, l3num, protonum);
	if (!h) {
		rcu_read_unlock();
		return -ENOENT;
	}

	nat = nf_conntrack_nat_helper_find(h->nat_mod_name);
	if (!nat) {
		snprintf(mod_name, sizeof(mod_name), "%s", h->nat_mod_name);
		rcu_read_unlock();
		request_module(mod_name);

		rcu_read_lock();
		nat = nf_conntrack_nat_helper_find(mod_name);
		if (!nat) {
			rcu_read_unlock();
			return -ENOENT;
		}
	}

	if (!try_module_get(nat->module))
		ret = -ENOENT;

	rcu_read_unlock();
	return ret;
}
EXPORT_SYMBOL_GPL(nf_nat_helper_try_module_get);

void nf_nat_helper_put(struct nf_conntrack_helper *helper)
{
	struct nf_conntrack_nat_helper *nat;

	nat = nf_conntrack_nat_helper_find(helper->nat_mod_name);
	if (WARN_ON_ONCE(!nat))
		return;

	module_put(nat->module);
}
EXPORT_SYMBOL_GPL(nf_nat_helper_put);

struct nf_conn_help *
nf_ct_helper_ext_add(struct nf_conn *ct, gfp_t gfp)
{
	struct nf_conn_help *help;

	help = nf_ct_ext_add(ct, NF_CT_EXT_HELPER, gfp);
	if (help)
		INIT_HLIST_HEAD(&help->expectations);
	else
		pr_debug("failed to add helper extension area");
	return help;
}
EXPORT_SYMBOL_GPL(nf_ct_helper_ext_add);

static struct nf_conntrack_helper *
nf_ct_lookup_helper(struct nf_conn *ct, struct net *net)
{
	struct nf_conntrack_net *cnet = nf_ct_pernet(net);

	if (!cnet->sysctl_auto_assign_helper) {
		if (cnet->auto_assign_helper_warned)
			return NULL;
		if (!__nf_ct_helper_find(&ct->tuplehash[IP_CT_DIR_REPLY].tuple))
			return NULL;
		pr_info("nf_conntrack: default automatic helper assignment "
			"has been turned off for security reasons and CT-based "
			"firewall rule not found. Use the iptables CT target "
			"to attach helpers instead.\n");
		cnet->auto_assign_helper_warned = true;
		return NULL;
	}

	return __nf_ct_helper_find(&ct->tuplehash[IP_CT_DIR_REPLY].tuple);
}

int __nf_ct_try_assign_helper(struct nf_conn *ct, struct nf_conn *tmpl,
			      gfp_t flags)
{
	struct nf_conntrack_helper *helper = NULL;
	struct nf_conn_help *help;
	struct net *net = nf_ct_net(ct);

	/* We already got a helper explicitly attached. The function
	 * nf_conntrack_alter_reply - in case NAT is in use - asks for looking
	 * the helper up again. Since now the user is in full control of
	 * making consistent helper configurations, skip this automatic
	 * re-lookup, otherwise we'll lose the helper.
	 */
	if (test_bit(IPS_HELPER_BIT, &ct->status))
		return 0;

	if (tmpl != NULL) {
		help = nfct_help(tmpl);
		if (help != NULL) {
			helper = help->helper;
			set_bit(IPS_HELPER_BIT, &ct->status);
		}
	}

	help = nfct_help(ct);

	if (helper == NULL) {
		helper = nf_ct_lookup_helper(ct, net);
		if (helper == NULL) {
			if (help)
				RCU_INIT_POINTER(help->helper, NULL);
			return 0;
		}
	}

	if (help == NULL) {
		help = nf_ct_helper_ext_add(ct, flags);
		if (help == NULL)
			return -ENOMEM;
	} else {
		/* We only allow helper re-assignment of the same sort since
		 * we cannot reallocate the helper extension area.
		 */
		struct nf_conntrack_helper *tmp = rcu_dereference(help->helper);

		if (tmp && tmp->help != helper->help) {
			RCU_INIT_POINTER(help->helper, NULL);
			return 0;
		}
	}

	rcu_assign_pointer(help->helper, helper);

	return 0;
}
EXPORT_SYMBOL_GPL(__nf_ct_try_assign_helper);

/* appropriate ct lock protecting must be taken by caller */
static int unhelp(struct nf_conn *ct, void *me)
{
	struct nf_conn_help *help = nfct_help(ct);

	if (help && rcu_dereference_raw(help->helper) == me) {
		nf_conntrack_event(IPCT_HELPER, ct);
		RCU_INIT_POINTER(help->helper, NULL);
	}

	/* We are not intended to delete this conntrack. */
	return 0;
}

void nf_ct_helper_destroy(struct nf_conn *ct)
{
	struct nf_conn_help *help = nfct_help(ct);
	struct nf_conntrack_helper *helper;

	if (help) {
		rcu_read_lock();
		helper = rcu_dereference(help->helper);
		if (helper && helper->destroy)
			helper->destroy(ct);
		rcu_read_unlock();
	}
}

static LIST_HEAD(nf_ct_helper_expectfn_list);

void nf_ct_helper_expectfn_register(struct nf_ct_helper_expectfn *n)
{
	spin_lock_bh(&nf_conntrack_expect_lock);
	list_add_rcu(&n->head, &nf_ct_helper_expectfn_list);
	spin_unlock_bh(&nf_conntrack_expect_lock);
}
EXPORT_SYMBOL_GPL(nf_ct_helper_expectfn_register);

void nf_ct_helper_expectfn_unregister(struct nf_ct_helper_expectfn *n)
{
	spin_lock_bh(&nf_conntrack_expect_lock);
	list_del_rcu(&n->head);
	spin_unlock_bh(&nf_conntrack_expect_lock);
}
EXPORT_SYMBOL_GPL(nf_ct_helper_expectfn_unregister);

/* Caller should hold the rcu lock */
struct nf_ct_helper_expectfn *
nf_ct_helper_expectfn_find_by_name(const char *name)
{
	struct nf_ct_helper_expectfn *cur;
	bool found = false;

	list_for_each_entry_rcu(cur, &nf_ct_helper_expectfn_list, head) {
		if (!strcmp(cur->name, name)) {
			found = true;
			break;
		}
	}
	return found ? cur : NULL;
}
EXPORT_SYMBOL_GPL(nf_ct_helper_expectfn_find_by_name);

/* Caller should hold the rcu lock */
struct nf_ct_helper_expectfn *
nf_ct_helper_expectfn_find_by_symbol(const void *symbol)
{
	struct nf_ct_helper_expectfn *cur;
	bool found = false;

	list_for_each_entry_rcu(cur, &nf_ct_helper_expectfn_list, head) {
		if (cur->expectfn == symbol) {
			found = true;
			break;
		}
	}
	return found ? cur : NULL;
}
EXPORT_SYMBOL_GPL(nf_ct_helper_expectfn_find_by_symbol);

__printf(3, 4)
void nf_ct_helper_log(struct sk_buff *skb, const struct nf_conn *ct,
		      const char *fmt, ...)
{
	const struct nf_conn_help *help;
	const struct nf_conntrack_helper *helper;
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);

	vaf.fmt = fmt;
	vaf.va = &args;

	/* Called from the helper function, this call never fails */
	help = nfct_help(ct);

	/* rcu_read_lock()ed by nf_hook_thresh */
	helper = rcu_dereference(help->helper);

	nf_log_packet(nf_ct_net(ct), nf_ct_l3num(ct), 0, skb, NULL, NULL, NULL,
		      "nf_ct_%s: dropping packet: %pV ", helper->name, &vaf);

	va_end(args);
}
EXPORT_SYMBOL_GPL(nf_ct_helper_log);

int nf_conntrack_helper_register(struct nf_conntrack_helper *me)
{
	struct nf_conntrack_tuple_mask mask = { .src.u.all = htons(0xFFFF) };
	unsigned int h = helper_hash(&me->tuple);
	struct nf_conntrack_helper *cur;
	int ret = 0, i;

	BUG_ON(me->expect_policy == NULL);
	BUG_ON(me->expect_class_max >= NF_CT_MAX_EXPECT_CLASSES);
	BUG_ON(strlen(me->name) > NF_CT_HELPER_NAME_LEN - 1);

	if (me->expect_policy->max_expected > NF_CT_EXPECT_MAX_CNT)
		return -EINVAL;

	mutex_lock(&nf_ct_helper_mutex);
	for (i = 0; i < nf_ct_helper_hsize; i++) {
		hlist_for_each_entry(cur, &nf_ct_helper_hash[i], hnode) {
			if (!strcmp(cur->name, me->name) &&
			    (cur->tuple.src.l3num == NFPROTO_UNSPEC ||
			     cur->tuple.src.l3num == me->tuple.src.l3num) &&
			    cur->tuple.dst.protonum == me->tuple.dst.protonum) {
				ret = -EEXIST;
				goto out;
			}
		}
	}

	/* avoid unpredictable behaviour for auto_assign_helper */
	if (!(me->flags & NF_CT_HELPER_F_USERSPACE)) {
		hlist_for_each_entry(cur, &nf_ct_helper_hash[h], hnode) {
			if (nf_ct_tuple_src_mask_cmp(&cur->tuple, &me->tuple,
						     &mask)) {
				ret = -EEXIST;
				goto out;
			}
		}
	}
	refcount_set(&me->refcnt, 1);
	hlist_add_head_rcu(&me->hnode, &nf_ct_helper_hash[h]);
	nf_ct_helper_count++;
out:
	mutex_unlock(&nf_ct_helper_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(nf_conntrack_helper_register);

static bool expect_iter_me(struct nf_conntrack_expect *exp, void *data)
{
	struct nf_conn_help *help = nfct_help(exp->master);
	const struct nf_conntrack_helper *me = data;
	const struct nf_conntrack_helper *this;

	if (exp->helper == me)
		return true;

	this = rcu_dereference_protected(help->helper,
					 lockdep_is_held(&nf_conntrack_expect_lock));
	return this == me;
}

void nf_conntrack_helper_unregister(struct nf_conntrack_helper *me)
{
	mutex_lock(&nf_ct_helper_mutex);
	hlist_del_rcu(&me->hnode);
	nf_ct_helper_count--;
	mutex_unlock(&nf_ct_helper_mutex);

	/* Make sure every nothing is still using the helper unless its a
	 * connection in the hash.
	 */
	synchronize_rcu();

	nf_ct_expect_iterate_destroy(expect_iter_me, NULL);
	nf_ct_iterate_destroy(unhelp, me);
}
EXPORT_SYMBOL_GPL(nf_conntrack_helper_unregister);

void nf_ct_helper_init(struct nf_conntrack_helper *helper,
		       u16 l3num, u16 protonum, const char *name,
		       u16 default_port, u16 spec_port, u32 id,
		       const struct nf_conntrack_expect_policy *exp_pol,
		       u32 expect_class_max,
		       int (*help)(struct sk_buff *skb, unsigned int protoff,
				   struct nf_conn *ct,
				   enum ip_conntrack_info ctinfo),
		       int (*from_nlattr)(struct nlattr *attr,
					  struct nf_conn *ct),
		       struct module *module)
{
	helper->tuple.src.l3num = l3num;
	helper->tuple.dst.protonum = protonum;
	helper->tuple.src.u.all = htons(spec_port);
	helper->expect_policy = exp_pol;
	helper->expect_class_max = expect_class_max;
	helper->help = help;
	helper->from_nlattr = from_nlattr;
	helper->me = module;
	snprintf(helper->nat_mod_name, sizeof(helper->nat_mod_name),
		 NF_NAT_HELPER_PREFIX "%s", name);

	if (spec_port == default_port)
		snprintf(helper->name, sizeof(helper->name), "%s", name);
	else
		snprintf(helper->name, sizeof(helper->name), "%s-%u", name, id);
}
EXPORT_SYMBOL_GPL(nf_ct_helper_init);

int nf_conntrack_helpers_register(struct nf_conntrack_helper *helper,
				  unsigned int n)
{
	unsigned int i;
	int err = 0;

	for (i = 0; i < n; i++) {
		err = nf_conntrack_helper_register(&helper[i]);
		if (err < 0)
			goto err;
	}

	return err;
err:
	if (i > 0)
		nf_conntrack_helpers_unregister(helper, i);
	return err;
}
EXPORT_SYMBOL_GPL(nf_conntrack_helpers_register);

void nf_conntrack_helpers_unregister(struct nf_conntrack_helper *helper,
				unsigned int n)
{
	while (n-- > 0)
		nf_conntrack_helper_unregister(&helper[n]);
}
EXPORT_SYMBOL_GPL(nf_conntrack_helpers_unregister);

void nf_nat_helper_register(struct nf_conntrack_nat_helper *nat)
{
	mutex_lock(&nf_ct_nat_helpers_mutex);
	list_add_rcu(&nat->list, &nf_ct_nat_helpers);
	mutex_unlock(&nf_ct_nat_helpers_mutex);
}
EXPORT_SYMBOL_GPL(nf_nat_helper_register);

void nf_nat_helper_unregister(struct nf_conntrack_nat_helper *nat)
{
	mutex_lock(&nf_ct_nat_helpers_mutex);
	list_del_rcu(&nat->list);
	mutex_unlock(&nf_ct_nat_helpers_mutex);
}
EXPORT_SYMBOL_GPL(nf_nat_helper_unregister);

void nf_ct_set_auto_assign_helper_warned(struct net *net)
{
	nf_ct_pernet(net)->auto_assign_helper_warned = true;
}
EXPORT_SYMBOL_GPL(nf_ct_set_auto_assign_helper_warned);

void nf_conntrack_helper_pernet_init(struct net *net)
{
	struct nf_conntrack_net *cnet = nf_ct_pernet(net);

	cnet->sysctl_auto_assign_helper = nf_ct_auto_assign_helper;
}

int nf_conntrack_helper_init(void)
{
	nf_ct_helper_hsize = 1; /* gets rounded up to use one page */
	nf_ct_helper_hash =
		nf_ct_alloc_hashtable(&nf_ct_helper_hsize, 0);
	if (!nf_ct_helper_hash)
		return -ENOMEM;

	INIT_LIST_HEAD(&nf_ct_nat_helpers);
	return 0;
}

void nf_conntrack_helper_fini(void)
{
	kvfree(nf_ct_helper_hash);
}
