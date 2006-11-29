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
struct nf_conntrack_l3proto *nf_ct_l3protos[PF_MAX] __read_mostly;

struct nf_conntrack_l4proto *
__nf_ct_l4proto_find(u_int16_t l3proto, u_int8_t l4proto)
{
	if (unlikely(l3proto >= AF_MAX || nf_ct_protos[l3proto] == NULL))
		return &nf_conntrack_l4proto_generic;

	return nf_ct_protos[l3proto][l4proto];
}

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

void nf_ct_l4proto_put(struct nf_conntrack_l4proto *p)
{
	module_put(p->me);
}

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

void nf_ct_l3proto_put(struct nf_conntrack_l3proto *p)
{
	module_put(p->me);
}

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

void nf_ct_l3proto_module_put(unsigned short l3proto)
{
	struct nf_conntrack_l3proto *p;

	preempt_disable();
	p = __nf_ct_l3proto_find(l3proto);
	preempt_enable();

	module_put(p->me);
}

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

int nf_conntrack_l3proto_register(struct nf_conntrack_l3proto *proto)
{
	int ret = 0;

	write_lock_bh(&nf_conntrack_lock);
	if (nf_ct_l3protos[proto->l3proto] != &nf_conntrack_l3proto_generic) {
		ret = -EBUSY;
		goto out;
	}
	nf_ct_l3protos[proto->l3proto] = proto;
out:
	write_unlock_bh(&nf_conntrack_lock);

	return ret;
}

void nf_conntrack_l3proto_unregister(struct nf_conntrack_l3proto *proto)
{
	write_lock_bh(&nf_conntrack_lock);
	nf_ct_l3protos[proto->l3proto] = &nf_conntrack_l3proto_generic;
	write_unlock_bh(&nf_conntrack_lock);

	/* Somebody could be still looking at the proto in bh. */
	synchronize_net();

	/* Remove all contrack entries for this protocol */
	nf_ct_iterate_cleanup(kill_l3proto, proto);
}

/* FIXME: Allow NULL functions and sub in pointers to generic for
   them. --RR */
int nf_conntrack_l4proto_register(struct nf_conntrack_l4proto *l4proto)
{
	int ret = 0;

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

out_unlock:
	write_unlock_bh(&nf_conntrack_lock);
out:
	return ret;
}

void nf_conntrack_l4proto_unregister(struct nf_conntrack_l4proto *l4proto)
{
	write_lock_bh(&nf_conntrack_lock);
	nf_ct_protos[l4proto->l3proto][l4proto->l4proto]
		= &nf_conntrack_l4proto_generic;
	write_unlock_bh(&nf_conntrack_lock);

	/* Somebody could be still looking at the proto in bh. */
	synchronize_net();

	/* Remove all contrack entries for this protocol */
	nf_ct_iterate_cleanup(kill_l4proto, l4proto);
}
