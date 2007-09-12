/*
 * Operations on the network namespace
 */
#ifndef __NET_NET_NAMESPACE_H
#define __NET_NET_NAMESPACE_H

#include <asm/atomic.h>
#include <linux/workqueue.h>
#include <linux/list.h>

struct proc_dir_entry;
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
};

extern struct net init_net;
extern struct list_head net_namespace_list;

extern void __put_net(struct net *net);

static inline struct net *get_net(struct net *net)
{
	atomic_inc(&net->count);
	return net;
}

static inline void put_net(struct net *net)
{
	if (atomic_dec_and_test(&net->count))
		__put_net(net);
}

static inline struct net *hold_net(struct net *net)
{
	atomic_inc(&net->use_count);
	return net;
}

static inline void release_net(struct net *net)
{
	atomic_dec(&net->use_count);
}

extern void net_lock(void);
extern void net_unlock(void);

#define for_each_net(VAR)				\
	list_for_each_entry(VAR, &net_namespace_list, list)


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
