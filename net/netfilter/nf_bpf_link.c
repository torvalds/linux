// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <linux/filter.h>
#include <linux/kmod.h>
#include <linux/module.h>
#include <linux/netfilter.h>

#include <net/netfilter/nf_bpf_link.h>
#include <uapi/linux/netfilter_ipv4.h>

static unsigned int nf_hook_run_bpf(void *bpf_prog, struct sk_buff *skb,
				    const struct nf_hook_state *s)
{
	const struct bpf_prog *prog = bpf_prog;
	struct bpf_nf_ctx ctx = {
		.state = s,
		.skb = skb,
	};

	return bpf_prog_run_pin_on_cpu(prog, &ctx);
}

struct bpf_nf_link {
	struct bpf_link link;
	struct nf_hook_ops hook_ops;
	netns_tracker ns_tracker;
	struct net *net;
	u32 dead;
	const struct nf_defrag_hook *defrag_hook;
};

#if IS_ENABLED(CONFIG_NF_DEFRAG_IPV4) || IS_ENABLED(CONFIG_NF_DEFRAG_IPV6)
static const struct nf_defrag_hook *
get_proto_defrag_hook(struct bpf_nf_link *link,
		      const struct nf_defrag_hook __rcu **ptr_global_hook,
		      const char *mod)
{
	const struct nf_defrag_hook *hook;
	int err;

	/* RCU protects us from races against module unloading */
	rcu_read_lock();
	hook = rcu_dereference(*ptr_global_hook);
	if (!hook) {
		rcu_read_unlock();
		err = request_module("%s", mod);
		if (err)
			return ERR_PTR(err < 0 ? err : -EINVAL);

		rcu_read_lock();
		hook = rcu_dereference(*ptr_global_hook);
	}

	if (hook && try_module_get(hook->owner)) {
		/* Once we have a refcnt on the module, we no longer need RCU */
		hook = rcu_pointer_handoff(hook);
	} else {
		WARN_ONCE(!hook, "%s has bad registration", mod);
		hook = ERR_PTR(-ENOENT);
	}
	rcu_read_unlock();

	if (!IS_ERR(hook)) {
		err = hook->enable(link->net);
		if (err) {
			module_put(hook->owner);
			hook = ERR_PTR(err);
		}
	}

	return hook;
}
#endif

static int bpf_nf_enable_defrag(struct bpf_nf_link *link)
{
	const struct nf_defrag_hook __maybe_unused *hook;

	switch (link->hook_ops.pf) {
#if IS_ENABLED(CONFIG_NF_DEFRAG_IPV4)
	case NFPROTO_IPV4:
		hook = get_proto_defrag_hook(link, &nf_defrag_v4_hook, "nf_defrag_ipv4");
		if (IS_ERR(hook))
			return PTR_ERR(hook);

		link->defrag_hook = hook;
		return 0;
#endif
#if IS_ENABLED(CONFIG_NF_DEFRAG_IPV6)
	case NFPROTO_IPV6:
		hook = get_proto_defrag_hook(link, &nf_defrag_v6_hook, "nf_defrag_ipv6");
		if (IS_ERR(hook))
			return PTR_ERR(hook);

		link->defrag_hook = hook;
		return 0;
#endif
	default:
		return -EAFNOSUPPORT;
	}
}

static void bpf_nf_disable_defrag(struct bpf_nf_link *link)
{
	const struct nf_defrag_hook *hook = link->defrag_hook;

	if (!hook)
		return;
	hook->disable(link->net);
	module_put(hook->owner);
}

static void bpf_nf_link_release(struct bpf_link *link)
{
	struct bpf_nf_link *nf_link = container_of(link, struct bpf_nf_link, link);

	if (nf_link->dead)
		return;

	/* do not double release in case .detach was already called */
	if (!cmpxchg(&nf_link->dead, 0, 1)) {
		nf_unregister_net_hook(nf_link->net, &nf_link->hook_ops);
		bpf_nf_disable_defrag(nf_link);
		put_net_track(nf_link->net, &nf_link->ns_tracker);
	}
}

static void bpf_nf_link_dealloc(struct bpf_link *link)
{
	struct bpf_nf_link *nf_link = container_of(link, struct bpf_nf_link, link);

	kfree(nf_link);
}

static int bpf_nf_link_detach(struct bpf_link *link)
{
	bpf_nf_link_release(link);
	return 0;
}

static void bpf_nf_link_show_info(const struct bpf_link *link,
				  struct seq_file *seq)
{
	struct bpf_nf_link *nf_link = container_of(link, struct bpf_nf_link, link);

	seq_printf(seq, "pf:\t%u\thooknum:\t%u\tprio:\t%d\n",
		   nf_link->hook_ops.pf, nf_link->hook_ops.hooknum,
		   nf_link->hook_ops.priority);
}

static int bpf_nf_link_fill_link_info(const struct bpf_link *link,
				      struct bpf_link_info *info)
{
	struct bpf_nf_link *nf_link = container_of(link, struct bpf_nf_link, link);
	const struct nf_defrag_hook *hook = nf_link->defrag_hook;

	info->netfilter.pf = nf_link->hook_ops.pf;
	info->netfilter.hooknum = nf_link->hook_ops.hooknum;
	info->netfilter.priority = nf_link->hook_ops.priority;
	info->netfilter.flags = hook ? BPF_F_NETFILTER_IP_DEFRAG : 0;

	return 0;
}

static int bpf_nf_link_update(struct bpf_link *link, struct bpf_prog *new_prog,
			      struct bpf_prog *old_prog)
{
	return -EOPNOTSUPP;
}

static const struct bpf_link_ops bpf_nf_link_lops = {
	.release = bpf_nf_link_release,
	.dealloc = bpf_nf_link_dealloc,
	.detach = bpf_nf_link_detach,
	.show_fdinfo = bpf_nf_link_show_info,
	.fill_link_info = bpf_nf_link_fill_link_info,
	.update_prog = bpf_nf_link_update,
};

static int bpf_nf_check_pf_and_hooks(const union bpf_attr *attr)
{
	int prio;

	switch (attr->link_create.netfilter.pf) {
	case NFPROTO_IPV4:
	case NFPROTO_IPV6:
		if (attr->link_create.netfilter.hooknum >= NF_INET_NUMHOOKS)
			return -EPROTO;
		break;
	default:
		return -EAFNOSUPPORT;
	}

	if (attr->link_create.netfilter.flags & ~BPF_F_NETFILTER_IP_DEFRAG)
		return -EOPNOTSUPP;

	/* make sure conntrack confirm is always last */
	prio = attr->link_create.netfilter.priority;
	if (prio == NF_IP_PRI_FIRST)
		return -ERANGE;  /* sabotage_in and other warts */
	else if (prio == NF_IP_PRI_LAST)
		return -ERANGE;  /* e.g. conntrack confirm */
	else if ((attr->link_create.netfilter.flags & BPF_F_NETFILTER_IP_DEFRAG) &&
		 prio <= NF_IP_PRI_CONNTRACK_DEFRAG)
		return -ERANGE;  /* cannot use defrag if prog runs before nf_defrag */

	return 0;
}

int bpf_nf_link_attach(const union bpf_attr *attr, struct bpf_prog *prog)
{
	struct net *net = current->nsproxy->net_ns;
	struct bpf_link_primer link_primer;
	struct bpf_nf_link *link;
	int err;

	if (attr->link_create.flags)
		return -EINVAL;

	err = bpf_nf_check_pf_and_hooks(attr);
	if (err)
		return err;

	link = kzalloc(sizeof(*link), GFP_USER);
	if (!link)
		return -ENOMEM;

	bpf_link_init(&link->link, BPF_LINK_TYPE_NETFILTER, &bpf_nf_link_lops, prog,
		      attr->link_create.attach_type);

	link->hook_ops.hook = nf_hook_run_bpf;
	link->hook_ops.hook_ops_type = NF_HOOK_OP_BPF;
	link->hook_ops.priv = prog;

	link->hook_ops.pf = attr->link_create.netfilter.pf;
	link->hook_ops.priority = attr->link_create.netfilter.priority;
	link->hook_ops.hooknum = attr->link_create.netfilter.hooknum;

	link->net = net;
	link->dead = false;
	link->defrag_hook = NULL;

	err = bpf_link_prime(&link->link, &link_primer);
	if (err) {
		kfree(link);
		return err;
	}

	if (attr->link_create.netfilter.flags & BPF_F_NETFILTER_IP_DEFRAG) {
		err = bpf_nf_enable_defrag(link);
		if (err) {
			bpf_link_cleanup(&link_primer);
			return err;
		}
	}

	err = nf_register_net_hook(net, &link->hook_ops);
	if (err) {
		bpf_nf_disable_defrag(link);
		bpf_link_cleanup(&link_primer);
		return err;
	}

	get_net_track(net, &link->ns_tracker, GFP_KERNEL);

	return bpf_link_settle(&link_primer);
}

const struct bpf_prog_ops netfilter_prog_ops = {
	.test_run = bpf_prog_test_run_nf,
};

static bool nf_ptr_to_btf_id(struct bpf_insn_access_aux *info, const char *name)
{
	struct btf *btf;
	s32 type_id;

	btf = bpf_get_btf_vmlinux();
	if (IS_ERR_OR_NULL(btf))
		return false;

	type_id = btf_find_by_name_kind(btf, name, BTF_KIND_STRUCT);
	if (WARN_ON_ONCE(type_id < 0))
		return false;

	info->btf = btf;
	info->btf_id = type_id;
	info->reg_type = PTR_TO_BTF_ID | PTR_TRUSTED;
	return true;
}

static bool nf_is_valid_access(int off, int size, enum bpf_access_type type,
			       const struct bpf_prog *prog,
			       struct bpf_insn_access_aux *info)
{
	if (off < 0 || off >= sizeof(struct bpf_nf_ctx))
		return false;

	if (type == BPF_WRITE)
		return false;

	switch (off) {
	case bpf_ctx_range(struct bpf_nf_ctx, skb):
		if (size != sizeof_field(struct bpf_nf_ctx, skb))
			return false;

		return nf_ptr_to_btf_id(info, "sk_buff");
	case bpf_ctx_range(struct bpf_nf_ctx, state):
		if (size != sizeof_field(struct bpf_nf_ctx, state))
			return false;

		return nf_ptr_to_btf_id(info, "nf_hook_state");
	default:
		return false;
	}

	return false;
}

static const struct bpf_func_proto *
bpf_nf_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	return bpf_base_func_proto(func_id, prog);
}

const struct bpf_verifier_ops netfilter_verifier_ops = {
	.is_valid_access	= nf_is_valid_access,
	.get_func_proto		= bpf_nf_func_proto,
};
