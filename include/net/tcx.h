/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2023 Isovalent */
#ifndef __NET_TCX_H
#define __NET_TCX_H

#include <linux/bpf.h>
#include <linux/bpf_mprog.h>

#include <net/sch_generic.h>

struct mini_Qdisc;

struct tcx_entry {
	struct mini_Qdisc __rcu *miniq;
	struct bpf_mprog_bundle bundle;
	u32 miniq_active;
	struct rcu_head rcu;
};

struct tcx_link {
	struct bpf_link link;
	struct net_device *dev;
};

static inline void tcx_set_ingress(struct sk_buff *skb, bool ingress)
{
#ifdef CONFIG_NET_XGRESS
	skb->tc_at_ingress = ingress;
#endif
}

#ifdef CONFIG_NET_XGRESS
static inline struct tcx_entry *tcx_entry(struct bpf_mprog_entry *entry)
{
	struct bpf_mprog_bundle *bundle = entry->parent;

	return container_of(bundle, struct tcx_entry, bundle);
}

static inline struct tcx_link *tcx_link(const struct bpf_link *link)
{
	return container_of(link, struct tcx_link, link);
}

void tcx_inc(void);
void tcx_dec(void);

static inline void tcx_entry_sync(void)
{
	/* bpf_mprog_entry got a/b swapped, therefore ensure that
	 * there are no inflight users on the old one anymore.
	 */
	synchronize_rcu();
}

static inline void
tcx_entry_update(struct net_device *dev, struct bpf_mprog_entry *entry,
		 bool ingress)
{
	ASSERT_RTNL();
	if (ingress)
		rcu_assign_pointer(dev->tcx_ingress, entry);
	else
		rcu_assign_pointer(dev->tcx_egress, entry);
}

static inline struct bpf_mprog_entry *
tcx_entry_fetch(struct net_device *dev, bool ingress)
{
	ASSERT_RTNL();
	if (ingress)
		return rcu_dereference_rtnl(dev->tcx_ingress);
	else
		return rcu_dereference_rtnl(dev->tcx_egress);
}

static inline struct bpf_mprog_entry *tcx_entry_create_noprof(void)
{
	struct tcx_entry *tcx = kzalloc_noprof(sizeof(*tcx), GFP_KERNEL);

	if (tcx) {
		bpf_mprog_bundle_init(&tcx->bundle);
		return &tcx->bundle.a;
	}
	return NULL;
}
#define tcx_entry_create(...)	alloc_hooks(tcx_entry_create_noprof(__VA_ARGS__))

static inline void tcx_entry_free(struct bpf_mprog_entry *entry)
{
	kfree_rcu(tcx_entry(entry), rcu);
}

static inline struct bpf_mprog_entry *
tcx_entry_fetch_or_create(struct net_device *dev, bool ingress, bool *created)
{
	struct bpf_mprog_entry *entry = tcx_entry_fetch(dev, ingress);

	*created = false;
	if (!entry) {
		entry = tcx_entry_create();
		if (!entry)
			return NULL;
		*created = true;
	}
	return entry;
}

static inline void tcx_skeys_inc(bool ingress)
{
	tcx_inc();
	if (ingress)
		net_inc_ingress_queue();
	else
		net_inc_egress_queue();
}

static inline void tcx_skeys_dec(bool ingress)
{
	if (ingress)
		net_dec_ingress_queue();
	else
		net_dec_egress_queue();
	tcx_dec();
}

static inline void tcx_miniq_inc(struct bpf_mprog_entry *entry)
{
	ASSERT_RTNL();
	tcx_entry(entry)->miniq_active++;
}

static inline void tcx_miniq_dec(struct bpf_mprog_entry *entry)
{
	ASSERT_RTNL();
	tcx_entry(entry)->miniq_active--;
}

static inline bool tcx_entry_is_active(struct bpf_mprog_entry *entry)
{
	ASSERT_RTNL();
	return bpf_mprog_total(entry) || tcx_entry(entry)->miniq_active;
}

static inline enum tcx_action_base tcx_action_code(struct sk_buff *skb,
						   int code)
{
	switch (code) {
	case TCX_PASS:
		skb->tc_index = qdisc_skb_cb(skb)->tc_classid;
		fallthrough;
	case TCX_DROP:
	case TCX_REDIRECT:
		return code;
	case TCX_NEXT:
	default:
		return TCX_NEXT;
	}
}
#endif /* CONFIG_NET_XGRESS */

#if defined(CONFIG_NET_XGRESS) && defined(CONFIG_BPF_SYSCALL)
int tcx_prog_attach(const union bpf_attr *attr, struct bpf_prog *prog);
int tcx_link_attach(const union bpf_attr *attr, struct bpf_prog *prog);
int tcx_prog_detach(const union bpf_attr *attr, struct bpf_prog *prog);
void tcx_uninstall(struct net_device *dev, bool ingress);

int tcx_prog_query(const union bpf_attr *attr,
		   union bpf_attr __user *uattr);

static inline void dev_tcx_uninstall(struct net_device *dev)
{
	ASSERT_RTNL();
	tcx_uninstall(dev, true);
	tcx_uninstall(dev, false);
}
#else
static inline int tcx_prog_attach(const union bpf_attr *attr,
				  struct bpf_prog *prog)
{
	return -EINVAL;
}

static inline int tcx_link_attach(const union bpf_attr *attr,
				  struct bpf_prog *prog)
{
	return -EINVAL;
}

static inline int tcx_prog_detach(const union bpf_attr *attr,
				  struct bpf_prog *prog)
{
	return -EINVAL;
}

static inline int tcx_prog_query(const union bpf_attr *attr,
				 union bpf_attr __user *uattr)
{
	return -EINVAL;
}

static inline void dev_tcx_uninstall(struct net_device *dev)
{
}
#endif /* CONFIG_NET_XGRESS && CONFIG_BPF_SYSCALL */
#endif /* __NET_TCX_H */
