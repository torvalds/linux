/* L3/L4 protocol support for nf_conntrack. */

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
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/skbuff.h>
#include <linux/vmalloc.h>
#include <linux/stddef.h>
#include <linux/err.h>
#include <linux/percpu.h>
#include <linux/moduleparam.h>
#include <linux/notifier.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>

#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_l3proto.h>
#include <net/netfilter/nf_conntrack_l4proto.h>
#include <net/netfilter/nf_conntrack_core.h>

struct nf_conntrack_l4proto **nf_ct_protos[PF_MAX] __read_mostly;
struct nf_conntrack_l3proto *nf_ct_l3protos[AF_MAX] __read_mostly;
EXPORT_SYMBOL_GPL(nf_ct_l3protos);

#ifdef CONFIG_SYSCTL
static DEFINE_MUTEX(nf_ct_proto_sysctl_mutex);

static int
nf_ct_register_sysctl(struct ctl_table_header **header, struct ctl_table *path,
		      struct ctl_table *table, unsigned int *users)
{
	if (*header == NULL) {
		*header = nf_register_sysctl_table(path, table);
		if (*header == NULL)
			return -ENOMEM;
	}
	if (users != NULL)
		(*users)++;
	return 0;
}

static void
nf_ct_unregister_sysctl(struct ctl_table_header **header,
			struct ctl_table *table, unsigned int *users)
{
	if (users != NULL && --*users > 0)
		return;
	nf_unregister_sysctl_table(*header, table);
	*header = NULL;
}
#endif

struct nf_conntrack_l4proto *
__nf_ct_l4proto_find(u_int16_t l3proto, u_int8_t l4proto)
{
	if (unlikely(l3proto >= AF_MAX || nf_ct_protos[l3proto] == NULL))
		return &nf_conntrack_l4proto_generic;

	return nf_ct_protos[l3proto][l4proto];
}
EXPORT_SYMBOL_GPL(__nf_ct_l4proto_find);

/* this is guaranteed to always return a valid protocol helper, since
 * it falls back to generic_protocol */
struct nf_conntrack_l4proto *
nf_ct_l4proto_find_get(u_int16_t l3proto, u_int8_t l4proto)
{
	struct nf_conntrack_l4proto *p;

	preempt_disable();
	p = __nf_ct_l4proto_find(l3proto, l4proto);
	if (!try_module_get(p->me))
		p = &nf_conntrack_l4proto_generic;
	preempt_enable();

	return p;
}
EXPORT_SYMBOL_GPL(nf_ct_l4proto_find_get);

void nf_ct_l4proto_put(struct nf_conntrack_l4proto *p)
{
	module_put(p->me);
}
EXPORT_SYMBOL_GPL(nf_ct_l4proto_put);

struct nf_conntrack_l3proto *
nf_ct_l3proto_find_get(u_int16_t l3proto)
{
	struct nf_conntrack_l3proto *p;

	preempt_disable();
	p = __nf_ct_l3proto_find(l3proto);
	if (!try_module_get(p->me))
		p = &nf_conntrack_l3proto_generic;
	preempt_enable();

	return p;
}
EXPORT_SYMBOL_GPL(nf_ct_l3proto_find_get);

void nf_ct_l3proto_put(struct nf_conntrack_l3proto *p)
{
	module_put(p->me);
}
EXPORT_SYMBOL_GPL(nf_ct_l3proto_put);

int
nf_ct_l3proto_try_module_get(unsigned short l3proto)
{
	int ret;
	struct nf_conntrack_l3proto *p;

retry:	p = nf_ct_l3proto_find_get(l3proto);
	if (p == &nf_conntrack_l3proto_generic) {
		ret = request_module("nf_conntrack-%d", l3proto);
		if (!ret)
			goto retry;

		return -EPROTOTYPE;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(nf_ct_l3proto_try_module_get);

void nf_ct_l3proto_module_put(unsigned short l3proto)
{
	struct nf_conntrack_l3proto *p;

	preempt_disable();
	p = __nf_ct_l3proto_find(l3proto);
	preempt_enable();

	module_put(p->me);
}
EXPORT_SYMBOL_GPL(nf_ct_l3proto_module_put);

static int kill_l3proto(struct nf_conn *i, void *data)
{
	return (i->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.l3num ==
			((struct nf_conntrack_l3proto *)data)->l3proto);
}

static int kill_l4proto(struct nf_conn *i, void *data)
{
	struct nf_conntrack_l4proto *l4proto;
	l4proto = (struct nf_conntrack_l4proto *)data;
	return (i->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.protonum ==
			l4proto->l4proto) &&
	       (i->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.l3num ==
			l4proto->l3proto);
}

static int nf_ct_l3proto_register_sysctl(struct nf_conntrack_l3proto *l3proto)
{
	int err = 0;

#ifdef CONFIG_SYSCTL
	mutex_lock(&nf_ct_proto_sysctl_mutex);
	if (l3proto->ctl_table != NULL) {
		err = nf_ct_register_sysctl(&l3proto->ctl_table_header,
					    l3proto->ctl_table_path,
					    l3proto->ctl_table, NULL);
	}
	mutex_unlock(&nf_ct_proto_sysctl_mutex);
#endif
	return err;
}

static void nf_ct_l3proto_unregister_sysctl(struct nf_conntrack_l3proto *l3proto)
{
#ifdef CONFIG_SYSCTL
	mutex_lock(&nf_ct_proto_sysctl_mutex);
	if (l3proto->ctl_table_header != NULL)
		nf_ct_unregister_sysctl(&l3proto->ctl_table_header,
					l3proto->ctl_table, NULL);
	mutex_unlock(&nf_ct_proto_sysctl_mutex);
#endif
}

int nf_conntrack_l3proto_register(struct nf_conntrack_l3proto *proto)
{
	int ret = 0;

	if (proto->l3proto >= AF_MAX) {
		ret = -EBUSY;
		goto out;
	}

	write_lock_bh(&nf_conntrack_lock);
	if (nf_ct_l3protos[proto->l3proto] != &nf_conntrack_l3proto_generic) {
		ret = -EBUSY;
		goto out_unlock;
	}
	nf_ct_l3protos[proto->l3proto] = proto;
	write_unlock_bh(&nf_conntrack_lock);

	ret = nf_ct_l3proto_register_sysctl(proto);
	if (ret < 0)
		nf_conntrack_l3proto_unregister(proto);
	return ret;

out_unlock:
	write_unlock_bh(&nf_conntrack_lock);
out:
	return ret;
}
EXPORT_SYMBOL_GPL(nf_conntrack_l3proto_register);

int nf_conntrack_l3proto_unregister(struct nf_conntrack_l3proto *proto)
{
	int ret = 0;

	if (proto->l3proto >= AF_MAX) {
		ret = -EBUSY;
		goto out;
	}

	write_lock_bh(&nf_conntrack_lock);
	if (nf_ct_l3protos[proto->l3proto] != proto) {
		write_unlock_bh(&nf_conntrack_lock);
		ret = -EBUSY;
		goto out;
	}

	nf_ct_l3protos[proto->l3proto] = &nf_conntrack_l3proto_generic;
	write_unlock_bh(&nf_conntrack_lock);

	nf_ct_l3proto_unregister_sysctl(proto);

	/* Somebody could be still looking at the proto in bh. */
	synchronize_net();

	/* Remove all contrack entries for this protocol */
	nf_ct_iterate_cleanup(kill_l3proto, proto);

out:
	return ret;
}
EXPORT_SYMBOL_GPL(nf_conntrack_l3proto_unregister);

static int nf_ct_l4proto_register_sysctl(struct nf_conntrack_l4proto *l4proto)
{
	int err = 0;

#ifdef CONFIG_SYSCTL
	mutex_lock(&nf_ct_proto_sysctl_mutex);
	if (l4proto->ctl_table != NULL) {
		err = nf_ct_register_sysctl(l4proto->ctl_table_header,
					    nf_net_netfilter_sysctl_path,
					    l4proto->ctl_table,
					    l4proto->ctl_table_users);
		if (err < 0)
			goto out;
	}
#ifdef CONFIG_NF_CONNTRACK_PROC_COMPAT
	if (l4proto->ctl_compat_table != NULL) {
		err = nf_ct_register_sysctl(&l4proto->ctl_compat_table_header,
					    nf_net_ipv4_netfilter_sysctl_path,
					    l4proto->ctl_compat_table, NULL);
		if (err == 0)
			goto out;
		nf_ct_unregister_sysctl(l4proto->ctl_table_header,
					l4proto->ctl_table,
					l4proto->ctl_table_users);
	}
#endif /* CONFIG_NF_CONNTRACK_PROC_COMPAT */
out:
	mutex_unlock(&nf_ct_proto_sysctl_mutex);
#endif /* CONFIG_SYSCTL */
	return err;
}

static void nf_ct_l4proto_unregister_sysctl(struct nf_conntrack_l4proto *l4proto)
{
#ifdef CONFIG_SYSCTL
	mutex_lock(&nf_ct_proto_sysctl_mutex);
	if (l4proto->ctl_table_header != NULL &&
	    *l4proto->ctl_table_header != NULL)
		nf_ct_unregister_sysctl(l4proto->ctl_table_header,
					l4proto->ctl_table,
					l4proto->ctl_table_users);
#ifdef CONFIG_NF_CONNTRACK_PROC_COMPAT
	if (l4proto->ctl_compat_table_header != NULL)
		nf_ct_unregister_sysctl(&l4proto->ctl_compat_table_header,
					l4proto->ctl_compat_table, NULL);
#endif /* CONFIG_NF_CONNTRACK_PROC_COMPAT */
	mutex_unlock(&nf_ct_proto_sysctl_mutex);
#endif /* CONFIG_SYSCTL */
}

/* FIXME: Allow NULL functions and sub in pointers to generic for
   them. --RR */
int nf_conntrack_l4proto_register(struct nf_conntrack_l4proto *l4proto)
{
	int ret = 0;

	if (l4proto->l3proto >= PF_MAX) {
		ret = -EBUSY;
		goto out;
	}

	if (l4proto == &nf_conntrack_l4proto_generic)
		return nf_ct_l4proto_register_sysctl(l4proto);

retry:
	write_lock_bh(&nf_conntrack_lock);
	if (nf_ct_protos[l4proto->l3proto]) {
		if (nf_ct_protos[l4proto->l3proto][l4proto->l4proto]
				!= &nf_conntrack_l4proto_generic) {
			ret = -EBUSY;
			goto out_unlock;
		}
	} else {
		/* l3proto may be loaded latter. */
		struct nf_conntrack_l4proto **proto_array;
		int i;

		write_unlock_bh(&nf_conntrack_lock);

		proto_array = (struct nf_conntrack_l4proto **)
				kmalloc(MAX_NF_CT_PROTO *
					 sizeof(struct nf_conntrack_l4proto *),
					GFP_KERNEL);
		if (proto_array == NULL) {
			ret = -ENOMEM;
			goto out;
		}
		for (i = 0; i < MAX_NF_CT_PROTO; i++)
			proto_array[i] = &nf_conntrack_l4proto_generic;

		write_lock_bh(&nf_conntrack_lock);
		if (nf_ct_protos[l4proto->l3proto]) {
			/* bad timing, but no problem */
			write_unlock_bh(&nf_conntrack_lock);
			kfree(proto_array);
		} else {
			nf_ct_protos[l4proto->l3proto] = proto_array;
			write_unlock_bh(&nf_conntrack_lock);
		}

		/*
		 * Just once because array is never freed until unloading
		 * nf_conntrack.ko
		 */
		goto retry;
	}

	nf_ct_protos[l4proto->l3proto][l4proto->l4proto] = l4proto;
	write_unlock_bh(&nf_conntrack_lock);

	ret = nf_ct_l4proto_register_sysctl(l4proto);
	if (ret < 0)
		nf_conntrack_l4proto_unregister(l4proto);
	return ret;

out_unlock:
	write_unlock_bh(&nf_conntrack_lock);
out:
	return ret;
}
EXPORT_SYMBOL_GPL(nf_conntrack_l4proto_register);

int nf_conntrack_l4proto_unregister(struct nf_conntrack_l4proto *l4proto)
{
	int ret = 0;

	if (l4proto->l3proto >= PF_MAX) {
		ret = -EBUSY;
		goto out;
	}

	if (l4proto == &nf_conntrack_l4proto_generic) {
		nf_ct_l4proto_unregister_sysctl(l4proto);
		goto out;
	}

	write_lock_bh(&nf_conntrack_lock);
	if (nf_ct_protos[l4proto->l3proto][l4proto->l4proto]
	    != l4proto) {
		write_unlock_bh(&nf_conntrack_lock);
		ret = -EBUSY;
		goto out;
	}
	nf_ct_protos[l4proto->l3proto][l4proto->l4proto]
		= &nf_conntrack_l4proto_generic;
	write_unlock_bh(&nf_conntrack_lock);

	nf_ct_l4proto_unregister_sysctl(l4proto);

	/* Somebody could be still looking at the proto in bh. */
	synchronize_net();

	/* Remove all contrack entries for this protocol */
	nf_ct_iterate_cleanup(kill_l4proto, l4proto);

out:
	return ret;
}
EXPORT_SYMBOL_GPL(nf_conntrack_l4proto_unregister);
