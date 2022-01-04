/* SPDX-License-Identifier: GPL-2.0+ */
/*
 *  IPv6 IOAM implementation
 *
 *  Author:
 *  Justin Iurman <justin.iurman@uliege.be>
 */

#ifndef _NET_IOAM6_H
#define _NET_IOAM6_H

#include <linux/net.h>
#include <linux/ipv6.h>
#include <linux/ioam6.h>
#include <linux/rhashtable-types.h>

struct ioam6_namespace {
	struct rhash_head head;
	struct rcu_head rcu;

	struct ioam6_schema __rcu *schema;

	__be16 id;
	__be32 data;
	__be64 data_wide;
};

struct ioam6_schema {
	struct rhash_head head;
	struct rcu_head rcu;

	struct ioam6_namespace __rcu *ns;

	u32 id;
	int len;
	__be32 hdr;

	u8 data[0];
};

struct ioam6_pernet_data {
	struct mutex lock;
	struct rhashtable namespaces;
	struct rhashtable schemas;
};

static inline struct ioam6_pernet_data *ioam6_pernet(struct net *net)
{
#if IS_ENABLED(CONFIG_IPV6)
	return net->ipv6.ioam6_data;
#else
	return NULL;
#endif
}

struct ioam6_namespace *ioam6_namespace(struct net *net, __be16 id);
void ioam6_fill_trace_data(struct sk_buff *skb,
			   struct ioam6_namespace *ns,
			   struct ioam6_trace_hdr *trace);

int ioam6_init(void);
void ioam6_exit(void);

int ioam6_iptunnel_init(void);
void ioam6_iptunnel_exit(void);

#endif /* _NET_IOAM6_H */
