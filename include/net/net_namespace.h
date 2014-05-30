/*
 * Operations on the network namespace
 */
#ifndef __NET_NET_NAMESPACE_H
#define __NET_NET_NAMESPACE_H

#include <linux/atomic.h>
#include <linux/workqueue.h>
#include <linux/list.h>
#include <linux/sysctl.h>

#include <net/flow.h>
#include <net/netns/core.h>
#include <net/netns/mib.h>
#include <net/netns/unix.h>
#include <net/netns/packet.h>
#include <net/netns/ipv4.h>
#include <net/netns/ipv6.h>
#include <net/netns/ieee802154_6lowpan.h>
#include <net/netns/sctp.h>
#include <net/netns/dccp.h>
#include <net/netns/netfilter.h>
#include <net/netns/x_tables.h>
#if defined(CONFIG_NF_CONNTRACK) || defined(CONFIG_NF_CONNTRACK_MODULE)
#include <net/netns/conntrack.h>
#endif
#include <net/netns/nftables.h>
#include <net/netns/xfrm.h>

struct user_namespace;
struct proc_dir_entry;
struct net_device;
struct sock;
struct ctl_table_header;
struct net_generic;
struct sock;
struct netns_ipvs;


#define NETDEV_HASHBITS    8
#define NETDEV_HASHENTRIES (1 << NETDEV_HASHBITS)

struct net {
	atomic_t		passive;	/* To decided when the network
						 * namespace should be freed.
						 */
	atomic_t		count;		/* To decided when the network
						 *  namespace should be shut down.
						 */
#ifdef NETNS_REFCNT_DEBUG
	atomic_t		use_count;	/* To track references we
						 * destroy on demand
						 */
#endif
	spinlock_t		rules_mod_lock;

	struct list_head	list;		/* list of network namespaces */
	struct list_head	cleanup_list;	/* namespaces on death row */
	struct list_head	exit_list;	/* Use only net_mutex */

	struct user_namespace   *user_ns;	/* Owning user namespace */

	unsigned int		proc_inum;

	struct proc_dir_entry 	*proc_net;
	struct proc_dir_entry 	*proc_net_stat;

#ifdef CONFIG_SYSCTL
	struct ctl_table_set	sysctls;
#endif

	struct sock 		*rtnl;			/* rtnetlink socket */
	struct sock		*genl_sock;

	struct list_head 	dev_base_head;
	struct hlist_head 	*dev_name_head;
	struct hlist_head	*dev_index_head;
	unsigned int		dev_base_seq;	/* protected by rtnl_mutex */
	int			ifindex;
	unsigned int		dev_unreg_count;

	/* core fib_rules */
	struct list_head	rules_ops;


	struct net_device       *loopback_dev;          /* The loopback */
	struct netns_core	core;
	struct netns_mib	mib;
	struct netns_packet	packet;
	struct netns_unix	unx;
	struct netns_ipv4	ipv4;
#if IS_ENABLED(CONFIG_IPV6)
	struct netns_ipv6	ipv6;
#endif
#if IS_ENABLED(CONFIG_IEEE802154_6LOWPAN)
	struct netns_ieee802154_lowpan	ieee802154_lowpan;
#endif
#if defined(CONFIG_IP_SCTP) || defined(CONFIG_IP_SCTP_MODULE)
	struct netns_sctp	sctp;
#endif
#if defined(CONFIG_IP_DCCP) || defined(CONFIG_IP_DCCP_MODULE)
	struct netns_dccp	dccp;
#endif
#ifdef CONFIG_NETFILTER
	struct netns_nf		nf;
	struct netns_xt		xt;
#if defined(CONFIG_NF_CONNTRACK) || defined(CONFIG_NF_CONNTRACK_MODULE)
	struct netns_ct		ct;
#endif
#if defined(CONFIG_NF_TABLES) || defined(CONFIG_NF_TABLES_MODULE)
	struct netns_nftables	nft;
#endif
#if IS_ENABLED(CONFIG_NF_DEFRAG_IPV6)
	struct netns_nf_frag	nf_frag;
#endif
	struct sock		*nfnl;
	struct sock		*nfnl_stash;
#endif
#ifdef CONFIG_WEXT_CORE
	struct sk_buff_head	wext_nlevents;
#endif
	struct net_generic __rcu	*gen;

	/* Note : following structs are cache line aligned */
#ifdef CONFIG_XFRM
	struct netns_xfrm	xfrm;
#endif
#if IS_ENABLED(CONFIG_IP_VS)
	struct netns_ipvs	*ipvs;
#endif
	struct sock		*diag_nlsk;
	atomic_t		fnhe_genid;
};

#include <linux/seq_file_net.h>

/* Init's network namespace */
extern struct net init_net;

#ifdef CONFIG_NET_NS
struct net *copy_net_ns(unsigned long flags, struct user_namespace *user_ns,
			struct net *old_net);

#else /* CONFIG_NET_NS */
#include <linux/sched.h>
#include <linux/nsproxy.h>
static inline struct net *copy_net_ns(unsigned long flags,
	struct user_namespace *user_ns, struct net *old_net)
{
	if (flags & CLONE_NEWNET)
		return ERR_PTR(-EINVAL);
	return old_net;
}
#endif /* CONFIG_NET_NS */


extern struct list_head net_namespace_list;

struct net *get_net_ns_by_pid(pid_t pid);
struct net *get_net_ns_by_fd(int pid);

#ifdef CONFIG_SYSCTL
void ipx_register_sysctl(void);
void ipx_unregister_sysctl(void);
#else
#define ipx_register_sysctl()
#define ipx_unregister_sysctl()
#endif

#ifdef CONFIG_NET_NS
void __put_net(struct net *net);

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

void net_drop_ns(void *);

#else

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

#define net_drop_ns NULL
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

#ifdef CONFIG_NET_NS

static inline void write_pnet(struct net **pnet, struct net *net)
{
	*pnet = net;
}

static inline struct net *read_pnet(struct net * const *pnet)
{
	return *pnet;
}

#else

#define write_pnet(pnet, net)	do { (void)(net);} while (0)
#define read_pnet(pnet)		(&init_net)

#endif

#define for_each_net(VAR)				\
	list_for_each_entry(VAR, &net_namespace_list, list)

#define for_each_net_rcu(VAR)				\
	list_for_each_entry_rcu(VAR, &net_namespace_list, list)

#ifdef CONFIG_NET_NS
#define __net_init
#define __net_exit
#define __net_initdata
#define __net_initconst
#else
#define __net_init	__init
#define __net_exit	__exit_refok
#define __net_initdata	__initdata
#define __net_initconst	__initconst
#endif

struct pernet_operations {
	struct list_head list;
	int (*init)(struct net *net);
	void (*exit)(struct net *net);
	void (*exit_batch)(struct list_head *net_exit_list);
	int *id;
	size_t size;
};

/*
 * Use these carefully.  If you implement a network device and it
 * needs per network namespace operations use device pernet operations,
 * otherwise use pernet subsys operations.
 *
 * Network interfaces need to be removed from a dying netns _before_
 * subsys notifiers can be called, as most of the network code cleanup
 * (which is done from subsys notifiers) runs with the assumption that
 * dev_remove_pack has been called so no new packets will arrive during
 * and after the cleanup functions have been called.  dev_remove_pack
 * is not per namespace so instead the guarantee of no more packets
 * arriving in a network namespace is provided by ensuring that all
 * network devices and all sockets have left the network namespace
 * before the cleanup methods are called.
 *
 * For the longest time the ipv4 icmp code was registered as a pernet
 * device which caused kernel oops, and panics during network
 * namespace cleanup.   So please don't get this wrong.
 */
int register_pernet_subsys(struct pernet_operations *);
void unregister_pernet_subsys(struct pernet_operations *);
int register_pernet_device(struct pernet_operations *);
void unregister_pernet_device(struct pernet_operations *);

struct ctl_table;
struct ctl_table_header;

#ifdef CONFIG_SYSCTL
int net_sysctl_init(void);
struct ctl_table_header *register_net_sysctl(struct net *net, const char *path,
					     struct ctl_table *table);
void unregister_net_sysctl_table(struct ctl_table_header *header);
#else
static inline int net_sysctl_init(void) { return 0; }
static inline struct ctl_table_header *register_net_sysctl(struct net *net,
	const char *path, struct ctl_table *table)
{
	return NULL;
}
static inline void unregister_net_sysctl_table(struct ctl_table_header *header)
{
}
#endif

static inline int rt_genid_ipv4(struct net *net)
{
	return atomic_read(&net->ipv4.rt_genid);
}

static inline void rt_genid_bump_ipv4(struct net *net)
{
	atomic_inc(&net->ipv4.rt_genid);
}

#if IS_ENABLED(CONFIG_IPV6)
static inline int rt_genid_ipv6(struct net *net)
{
	return atomic_read(&net->ipv6.rt_genid);
}

static inline void rt_genid_bump_ipv6(struct net *net)
{
	atomic_inc(&net->ipv6.rt_genid);
}
#else
static inline int rt_genid_ipv6(struct net *net)
{
	return 0;
}

static inline void rt_genid_bump_ipv6(struct net *net)
{
}
#endif

/* For callers who don't really care about whether it's IPv4 or IPv6 */
static inline void rt_genid_bump_all(struct net *net)
{
	rt_genid_bump_ipv4(net);
	rt_genid_bump_ipv6(net);
}

static inline int fnhe_genid(struct net *net)
{
	return atomic_read(&net->fnhe_genid);
}

static inline void fnhe_genid_bump(struct net *net)
{
	atomic_inc(&net->fnhe_genid);
}

#endif /* __NET_NET_NAMESPACE_H */
