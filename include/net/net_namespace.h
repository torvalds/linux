/*
 * Operations on the network namespace
 */
#ifndef __NET_NET_NAMESPACE_H
#define __NET_NET_NAMESPACE_H

#include <asm/atomic.h>
#include <linux/workqueue.h>
#include <linux/list.h>

#include <net/netns/core.h>
#include <net/netns/unix.h>
#include <net/netns/packet.h>
#include <net/netns/ipv4.h>
#include <net/netns/ipv6.h>
#include <net/netns/dccp.h>
#include <net/netns/x_tables.h>

struct proc_dir_entry;
struct net_device;
struct sock;
struct ctl_table_header;
struct net_generic;

struct net {
	atomic_t		count;		/* To decided when the network
						 *  namespace should be freed.
						 */
#ifdef NETNS_REFCNT_DEBUG
	atomic_t		use_count;	/* To track references we
						 * destroy on demand
						 */
#endif
	struct list_head	list;		/* list of network namespaces */
	struct work_struct	work;		/* work struct for freeing */

	struct proc_dir_entry 	*proc_net;
	struct proc_dir_entry 	*proc_net_stat;

	struct list_head	sysctl_table_headers;

	struct net_device       *loopback_dev;          /* The loopback */

	struct list_head 	dev_base_head;
	struct hlist_head 	*dev_name_head;
	struct hlist_head	*dev_index_head;

	/* core fib_rules */
	struct list_head	rules_ops;
	spinlock_t		rules_mod_lock;

	struct sock 		*rtnl;			/* rtnetlink socket */

	struct netns_core	core;
	struct netns_packet	packet;
	struct netns_unix	unx;
	struct netns_ipv4	ipv4;
#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
	struct netns_ipv6	ipv6;
#endif
#if defined(CONFIG_IP_DCCP) || defined(CONFIG_IP_DCCP_MODULE)
	struct netns_dccp	dccp;
#endif
#ifdef CONFIG_NETFILTER
	struct netns_xt		xt;
#endif
	struct net_generic	*gen;
};


#include <linux/seq_file_net.h>

/* Init's network namespace */
extern struct net init_net;

#ifdef CONFIG_NET
#define INIT_NET_NS(net_ns) .net_ns = &init_net,

extern struct net *copy_net_ns(unsigned long flags, struct net *net_ns);

#else /* CONFIG_NET */

#define INIT_NET_NS(net_ns)

static inline struct net *copy_net_ns(unsigned long flags, struct net *net_ns)
{
	/* There is nothing to copy so this is a noop */
	return net_ns;
}
#endif /* CONFIG_NET */


extern struct list_head net_namespace_list;

#ifdef CONFIG_NET_NS
extern void __put_net(struct net *net);

static inline int net_alive(struct net *net)
{
	return net && atomic_read(&net->count);
}

static inline struct net *get_net(struct net *net)
{
	atomic_inc(&net->count);
	return net;
}

static inline struct net *maybe_get_net(struct net *net)
{
	/* Used when we know struct net exists but we
	 * aren't guaranteed a previous reference count
	 * exists.  If the reference count is zero this
	 * function fails and returns NULL.
	 */
	if (!atomic_inc_not_zero(&net->count))
		net = NULL;
	return net;
}

static inline void put_net(struct net *net)
{
	if (atomic_dec_and_test(&net->count))
		__put_net(net);
}

static inline
int net_eq(const struct net *net1, const struct net *net2)
{
	return net1 == net2;
}
#else

static inline int net_alive(struct net *net)
{
	return 1;
}

static inline struct net *get_net(struct net *net)
{
	return net;
}

static inline void put_net(struct net *net)
{
}

static inline struct net *maybe_get_net(struct net *net)
{
	return net;
}

static inline
int net_eq(const struct net *net1, const struct net *net2)
{
	return 1;
}
#endif


#ifdef NETNS_REFCNT_DEBUG
static inline struct net *hold_net(struct net *net)
{
	if (net)
		atomic_inc(&net->use_count);
	return net;
}

static inline void release_net(struct net *net)
{
	if (net)
		atomic_dec(&net->use_count);
}
#else
static inline struct net *hold_net(struct net *net)
{
	return net;
}

static inline void release_net(struct net *net)
{
}
#endif


#define for_each_net(VAR)				\
	list_for_each_entry(VAR, &net_namespace_list, list)

#ifdef CONFIG_NET_NS
#define __net_init
#define __net_exit
#define __net_initdata
#else
#define __net_init	__init
#define __net_exit	__exit_refok
#define __net_initdata	__initdata
#endif

struct pernet_operations {
	struct list_head list;
	int (*init)(struct net *net);
	void (*exit)(struct net *net);
};

extern int register_pernet_subsys(struct pernet_operations *);
extern void unregister_pernet_subsys(struct pernet_operations *);
extern int register_pernet_device(struct pernet_operations *);
extern void unregister_pernet_device(struct pernet_operations *);
extern int register_pernet_gen_device(int *id, struct pernet_operations *);
extern void unregister_pernet_gen_device(int id, struct pernet_operations *);

struct ctl_path;
struct ctl_table;
struct ctl_table_header;
extern struct ctl_table_header *register_net_sysctl_table(struct net *net,
	const struct ctl_path *path, struct ctl_table *table);
extern void unregister_net_sysctl_table(struct ctl_table_header *header);

#endif /* __NET_NET_NAMESPACE_H */
