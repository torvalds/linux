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
#include <net/netfilter/nf_conntrack_protocol.h>
#include <net/netfilter/nf_conntrack_core.h>

struct nf_conntrack_protocol **nf_ct_protos[PF_MAX] __read_mostly;
struct nf_conntrack_l3proto *nf_ct_l3protos[PF_MAX] __read_mostly;

struct nf_conntrack_protocol *
__nf_ct_proto_find(u_int16_t l3proto, u_int8_t protocol)
{
	if (unlikely(l3proto >= AF_MAX || nf_ct_protos[l3proto] == NULL))
		return &nf_conntrack_generic_protocol;

	return nf_ct_protos[l3proto][protocol];
}

/* this is guaranteed to always return a valid protocol helper, since
 * it falls back to generic_protocol */
struct nf_conntrack_protocol *
nf_ct_proto_find_get(u_int16_t l3proto, u_int8_t protocol)
{
	struct nf_conntrack_protocol *p;

	preempt_disable();
	p = __nf_ct_proto_find(l3proto, protocol);
	if (!try_module_get(p->me))
		p = &nf_conntrack_generic_protocol;
	preempt_enable();

	return p;
}

void nf_ct_proto_put(struct nf_conntrack_protocol *p)
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
		p = &nf_conntrack_generic_l3proto;
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
	if (p == &nf_conntrack_generic_l3proto) {
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

static int kill_proto(struct nf_conn *i, void *data)
{
	struct nf_conntrack_protocol *proto;
	proto = (struct nf_conntrack_protocol *)data;
	return (i->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.protonum ==
			proto->proto) &&
	       (i->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.l3num ==
			proto->l3proto);
}

int nf_conntrack_l3proto_register(struct nf_conntrack_l3proto *proto)
{
	int ret = 0;

	write_lock_bh(&nf_conntrack_lock);
	if (nf_ct_l3protos[proto->l3proto] != &nf_conntrack_generic_l3proto) {
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
	nf_ct_l3protos[proto->l3proto] = &nf_conntrack_generic_l3proto;
	write_unlock_bh(&nf_conntrack_lock);

	/* Somebody could be still looking at the proto in bh. */
	synchronize_net();

	/* Remove all contrack entries for this protocol */
	nf_ct_iterate_cleanup(kill_l3proto, proto);
}

/* FIXME: Allow NULL functions and sub in pointers to generic for
   them. --RR */
int nf_conntrack_protocol_register(struct nf_conntrack_protocol *proto)
{
	int ret = 0;

retry:
	write_lock_bh(&nf_conntrack_lock);
	if (nf_ct_protos[proto->l3proto]) {
		if (nf_ct_protos[proto->l3proto][proto->proto]
				!= &nf_conntrack_generic_protocol) {
			ret = -EBUSY;
			goto out_unlock;
		}
	} else {
		/* l3proto may be loaded latter. */
		struct nf_conntrack_protocol **proto_array;
		int i;

		write_unlock_bh(&nf_conntrack_lock);

		proto_array = (struct nf_conntrack_protocol **)
				kmalloc(MAX_NF_CT_PROTO *
					 sizeof(struct nf_conntrack_protocol *),
					GFP_KERNEL);
		if (proto_array == NULL) {
			ret = -ENOMEM;
			goto out;
		}
		for (i = 0; i < MAX_NF_CT_PROTO; i++)
			proto_array[i] = &nf_conntrack_generic_protocol;

		write_lock_bh(&nf_conntrack_lock);
		if (nf_ct_protos[proto->l3proto]) {
			/* bad timing, but no problem */
			write_unlock_bh(&nf_conntrack_lock);
			kfree(proto_array);
		} else {
			nf_ct_protos[proto->l3proto] = proto_array;
			write_unlock_bh(&nf_conntrack_lock);
		}

		/*
		 * Just once because array is never freed until unloading
		 * nf_conntrack.ko
		 */
		goto retry;
	}

	nf_ct_protos[proto->l3proto][proto->proto] = proto;

out_unlock:
	write_unlock_bh(&nf_conntrack_lock);
out:
	return ret;
}

void nf_conntrack_protocol_unregister(struct nf_conntrack_protocol *proto)
{
	write_lock_bh(&nf_conntrack_lock);
	nf_ct_protos[proto->l3proto][proto->proto]
		= &nf_conntrack_generic_protocol;
	write_unlock_bh(&nf_conntrack_lock);

	/* Somebody could be still looking at the proto in bh. */
	synchronize_net();

	/* Remove all contrack entries for this protocol */
	nf_ct_iterate_cleanup(kill_proto, proto);
}
