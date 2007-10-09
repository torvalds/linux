/*
 * Operations on the network namespace
 */
#ifndef __NET_NET_NAMESPACE_H
#define __NET_NET_NAMESPACE_H

#include <asm/atomic.h>
#include <linux/workqueue.h>
#include <linux/list.h>

struct proc_dir_entry;
struct net_device;
struct net {
	atomic_t		count;		/* To decided when the network
						 *  namespace should be freed.
						 */
	atomic_t		use_count;	/* To track references we
						 * destroy on demand
						 */
	struct list_head	list;		/* list of network namespaces */
	struct work_struct	work;		/* work struct for freeing */

	struct proc_dir_entry 	*proc_net;
	struct proc_dir_entry 	*proc_net_stat;
	struct proc_dir_entry 	*proc_net_root;

	struct net_device       *loopback_dev;          /* The loopback */

	struct list_head 	dev_base_head;
	struct hlist_head 	*dev_name_head;
	struct hlist_head	*dev_index_head;
};

#ifdef CONFIG_NET
/* Init's network namespace */
extern struct net init_net;
#define INIT_NET_NS(net_ns) .net_ns = &init_net,
#else
#define INIT_NET_NS(net_ns)
#endif

extern struct list_head net_namespace_list;

#ifdef CONFIG_NET
extern struct net *copy_net_ns(unsigned long flags, struct net *net_ns);
#else
static inline struct net *copy_net_ns(unsigned long flags, struct net *net_ns)
{
	/* There is nothing to copy so this is a noop */
	return net_ns;
}
#endif

extern void __put_net(struct net *net);

static inline struct net *get_net(struct net *net)
{
#ifdef CONFIG_NET
	atomic_inc(&net->count);
#endif
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
#ifdef CONFIG_NET
	if (atomic_dec_and_test(&net->count))
		__put_net(net);
#endif
}

static inline struct net *hold_net(struct net *net)
{
#ifdef CONFIG_NET
	atomic_inc(&net->use_count);
#endif
	return net;
}

static inline void release_net(struct net *net)
{
#ifdef CONFIG_NET
	atomic_dec(&net->use_count);
#endif
}

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

#endif /* __NET_NET_NAMESPACE_H */
