#include <linux/workqueue.h>
#include <linux/rtnetlink.h>
#include <linux/cache.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/idr.h>
#include <net/net_namespace.h>
#include <net/netns/generic.h>

/*
 *	Our network namespace constructor/destructor lists
 */

static LIST_HEAD(pernet_list);
static struct list_head *first_device = &pernet_list;
static DEFINE_MUTEX(net_mutex);

LIST_HEAD(net_namespace_list);
EXPORT_SYMBOL_GPL(net_namespace_list);

struct net init_net;
EXPORT_SYMBOL(init_net);

#define INITIAL_NET_GEN_PTRS	13 /* +1 for len +2 for rcu_head */

/*
 * setup_net runs the initializers for the network namespace object.
 */
static __net_init int setup_net(struct net *net)
{
	/* Must be called with net_mutex held */
	struct pernet_operations *ops;
	int error = 0;

	atomic_set(&net->count, 1);

#ifdef NETNS_REFCNT_DEBUG
	atomic_set(&net->use_count, 0);
#endif

	list_for_each_entry(ops, &pernet_list, list) {
		if (ops->init) {
			error = ops->init(net);
			if (error < 0)
				goto out_undo;
		}
	}
out:
	return error;

out_undo:
	/* Walk through the list backwards calling the exit functions
	 * for the pernet modules whose init functions did not fail.
	 */
	list_for_each_entry_continue_reverse(ops, &pernet_list, list) {
		if (ops->exit)
			ops->exit(net);
	}

	rcu_barrier();
	goto out;
}

static struct net_generic *net_alloc_generic(void)
{
	struct net_generic *ng;
	size_t generic_size = sizeof(struct net_generic) +
		INITIAL_NET_GEN_PTRS * sizeof(void *);

	ng = kzalloc(generic_size, GFP_KERNEL);
	if (ng)
		ng->len = INITIAL_NET_GEN_PTRS;

	return ng;
}

#ifdef CONFIG_NET_NS
static struct kmem_cache *net_cachep;
static struct workqueue_struct *netns_wq;

static struct net *net_alloc(void)
{
	struct net *net = NULL;
	struct net_generic *ng;

	ng = net_alloc_generic();
	if (!ng)
		goto out;

	net = kmem_cache_zalloc(net_cachep, GFP_KERNEL);
	if (!net)
		goto out_free;

	rcu_assign_pointer(net->gen, ng);
out:
	return net;

out_free:
	kfree(ng);
	goto out;
}

static void net_free(struct net *net)
{
#ifdef NETNS_REFCNT_DEBUG
	if (unlikely(atomic_read(&net->use_count) != 0)) {
		printk(KERN_EMERG "network namespace not free! Usage: %d\n",
			atomic_read(&net->use_count));
		return;
	}
#endif
	kfree(net->gen);
	kmem_cache_free(net_cachep, net);
}

struct net *copy_net_ns(unsigned long flags, struct net *old_net)
{
	struct net *new_net = NULL;
	int err;

	get_net(old_net);

	if (!(flags & CLONE_NEWNET))
		return old_net;

	err = -ENOMEM;
	new_net = net_alloc();
	if (!new_net)
		goto out_err;

	mutex_lock(&net_mutex);
	err = setup_net(new_net);
	if (!err) {
		rtnl_lock();
		list_add_tail(&new_net->list, &net_namespace_list);
		rtnl_unlock();
	}
	mutex_unlock(&net_mutex);

	if (err)
		goto out_free;
out:
	put_net(old_net);
	return new_net;

out_free:
	net_free(new_net);
out_err:
	new_net = ERR_PTR(err);
	goto out;
}

static void cleanup_net(struct work_struct *work)
{
	struct pernet_operations *ops;
	struct net *net;

	net = container_of(work, struct net, work);

	mutex_lock(&net_mutex);

	/* Don't let anyone else find us. */
	rtnl_lock();
	list_del(&net->list);
	rtnl_unlock();

	/* Run all of the network namespace exit methods */
	list_for_each_entry_reverse(ops, &pernet_list, list) {
		if (ops->exit)
			ops->exit(net);
	}

	mutex_unlock(&net_mutex);

	/* Ensure there are no outstanding rcu callbacks using this
	 * network namespace.
	 */
	rcu_barrier();

	/* Finally it is safe to free my network namespace structure */
	net_free(net);
}

void __put_net(struct net *net)
{
	/* Cleanup the network namespace in process context */
	INIT_WORK(&net->work, cleanup_net);
	queue_work(netns_wq, &net->work);
}
EXPORT_SYMBOL_GPL(__put_net);

#else
struct net *copy_net_ns(unsigned long flags, struct net *old_net)
{
	if (flags & CLONE_NEWNET)
		return ERR_PTR(-EINVAL);
	return old_net;
}
#endif

static int __init net_ns_init(void)
{
	struct net_generic *ng;
	int err;

	printk(KERN_INFO "net_namespace: %zd bytes\n", sizeof(struct net));
#ifdef CONFIG_NET_NS
	net_cachep = kmem_cache_create("net_namespace", sizeof(struct net),
					SMP_CACHE_BYTES,
					SLAB_PANIC, NULL);

	/* Create workqueue for cleanup */
	netns_wq = create_singlethread_workqueue("netns");
	if (!netns_wq)
		panic("Could not create netns workq");
#endif

	ng = net_alloc_generic();
	if (!ng)
		panic("Could not allocate generic netns");

	rcu_assign_pointer(init_net.gen, ng);

	mutex_lock(&net_mutex);
	err = setup_net(&init_net);

	rtnl_lock();
	list_add_tail(&init_net.list, &net_namespace_list);
	rtnl_unlock();

	mutex_unlock(&net_mutex);
	if (err)
		panic("Could not setup the initial network namespace");

	return 0;
}

pure_initcall(net_ns_init);

#ifdef CONFIG_NET_NS
static int register_pernet_operations(struct list_head *list,
				      struct pernet_operations *ops)
{
	struct net *net, *undo_net;
	int error;

	list_add_tail(&ops->list, list);
	if (ops->init) {
		for_each_net(net) {
			error = ops->init(net);
			if (error)
				goto out_undo;
		}
	}
	return 0;

out_undo:
	/* If I have an error cleanup all namespaces I initialized */
	list_del(&ops->list);
	if (ops->exit) {
		for_each_net(undo_net) {
			if (undo_net == net)
				goto undone;
			ops->exit(undo_net);
		}
	}
undone:
	return error;
}

static void unregister_pernet_operations(struct pernet_operations *ops)
{
	struct net *net;

	list_del(&ops->list);
	if (ops->exit)
		for_each_net(net)
			ops->exit(net);
}

#else

static int register_pernet_operations(struct list_head *list,
				      struct pernet_operations *ops)
{
	if (ops->init == NULL)
		return 0;
	return ops->init(&init_net);
}

static void unregister_pernet_operations(struct pernet_operations *ops)
{
	if (ops->exit)
		ops->exit(&init_net);
}
#endif

static DEFINE_IDA(net_generic_ids);

/**
 *      register_pernet_subsys - register a network namespace subsystem
 *	@ops:  pernet operations structure for the subsystem
 *
 *	Register a subsystem which has init and exit functions
 *	that are called when network namespaces are created and
 *	destroyed respectively.
 *
 *	When registered all network namespace init functions are
 *	called for every existing network namespace.  Allowing kernel
 *	modules to have a race free view of the set of network namespaces.
 *
 *	When a new network namespace is created all of the init
 *	methods are called in the order in which they were registered.
 *
 *	When a network namespace is destroyed all of the exit methods
 *	are called in the reverse of the order with which they were
 *	registered.
 */
int register_pernet_subsys(struct pernet_operations *ops)
{
	int error;
	mutex_lock(&net_mutex);
	error =  register_pernet_operations(first_device, ops);
	mutex_unlock(&net_mutex);
	return error;
}
EXPORT_SYMBOL_GPL(register_pernet_subsys);

/**
 *      unregister_pernet_subsys - unregister a network namespace subsystem
 *	@ops: pernet operations structure to manipulate
 *
 *	Remove the pernet operations structure from the list to be
 *	used when network namespaces are created or destroyed.  In
 *	addition run the exit method for all existing network
 *	namespaces.
 */
void unregister_pernet_subsys(struct pernet_operations *module)
{
	mutex_lock(&net_mutex);
	unregister_pernet_operations(module);
	mutex_unlock(&net_mutex);
}
EXPORT_SYMBOL_GPL(unregister_pernet_subsys);

int register_pernet_gen_subsys(int *id, struct pernet_operations *ops)
{
	int rv;

	mutex_lock(&net_mutex);
again:
	rv = ida_get_new_above(&net_generic_ids, 1, id);
	if (rv < 0) {
		if (rv == -EAGAIN) {
			ida_pre_get(&net_generic_ids, GFP_KERNEL);
			goto again;
		}
		goto out;
	}
	rv = register_pernet_operations(first_device, ops);
	if (rv < 0)
		ida_remove(&net_generic_ids, *id);
out:
	mutex_unlock(&net_mutex);
	return rv;
}
EXPORT_SYMBOL_GPL(register_pernet_gen_subsys);

void unregister_pernet_gen_subsys(int id, struct pernet_operations *ops)
{
	mutex_lock(&net_mutex);
	unregister_pernet_operations(ops);
	ida_remove(&net_generic_ids, id);
	mutex_unlock(&net_mutex);
}
EXPORT_SYMBOL_GPL(unregister_pernet_gen_subsys);

/**
 *      register_pernet_device - register a network namespace device
 *	@ops:  pernet operations structure for the subsystem
 *
 *	Register a device which has init and exit functions
 *	that are called when network namespaces are created and
 *	destroyed respectively.
 *
 *	When registered all network namespace init functions are
 *	called for every existing network namespace.  Allowing kernel
 *	modules to have a race free view of the set of network namespaces.
 *
 *	When a new network namespace is created all of the init
 *	methods are called in the order in which they were registered.
 *
 *	When a network namespace is destroyed all of the exit methods
 *	are called in the reverse of the order with which they were
 *	registered.
 */
int register_pernet_device(struct pernet_operations *ops)
{
	int error;
	mutex_lock(&net_mutex);
	error = register_pernet_operations(&pernet_list, ops);
	if (!error && (first_device == &pernet_list))
		first_device = &ops->list;
	mutex_unlock(&net_mutex);
	return error;
}
EXPORT_SYMBOL_GPL(register_pernet_device);

int register_pernet_gen_device(int *id, struct pernet_operations *ops)
{
	int error;
	mutex_lock(&net_mutex);
again:
	error = ida_get_new_above(&net_generic_ids, 1, id);
	if (error) {
		if (error == -EAGAIN) {
			ida_pre_get(&net_generic_ids, GFP_KERNEL);
			goto again;
		}
		goto out;
	}
	error = register_pernet_operations(&pernet_list, ops);
	if (error)
		ida_remove(&net_generic_ids, *id);
	else if (first_device == &pernet_list)
		first_device = &ops->list;
out:
	mutex_unlock(&net_mutex);
	return error;
}
EXPORT_SYMBOL_GPL(register_pernet_gen_device);

/**
 *      unregister_pernet_device - unregister a network namespace netdevice
 *	@ops: pernet operations structure to manipulate
 *
 *	Remove the pernet operations structure from the list to be
 *	used when network namespaces are created or destroyed.  In
 *	addition run the exit method for all existing network
 *	namespaces.
 */
void unregister_pernet_device(struct pernet_operations *ops)
{
	mutex_lock(&net_mutex);
	if (&ops->list == first_device)
		first_device = first_device->next;
	unregister_pernet_operations(ops);
	mutex_unlock(&net_mutex);
}
EXPORT_SYMBOL_GPL(unregister_pernet_device);

void unregister_pernet_gen_device(int id, struct pernet_operations *ops)
{
	mutex_lock(&net_mutex);
	if (&ops->list == first_device)
		first_device = first_device->next;
	unregister_pernet_operations(ops);
	ida_remove(&net_generic_ids, id);
	mutex_unlock(&net_mutex);
}
EXPORT_SYMBOL_GPL(unregister_pernet_gen_device);

static void net_generic_release(struct rcu_head *rcu)
{
	struct net_generic *ng;

	ng = container_of(rcu, struct net_generic, rcu);
	kfree(ng);
}

int net_assign_generic(struct net *net, int id, void *data)
{
	struct net_generic *ng, *old_ng;

	BUG_ON(!mutex_is_locked(&net_mutex));
	BUG_ON(id == 0);

	ng = old_ng = net->gen;
	if (old_ng->len >= id)
		goto assign;

	ng = kzalloc(sizeof(struct net_generic) +
			id * sizeof(void *), GFP_KERNEL);
	if (ng == NULL)
		return -ENOMEM;

	/*
	 * Some synchronisation notes:
	 *
	 * The net_generic explores the net->gen array inside rcu
	 * read section. Besides once set the net->gen->ptr[x]
	 * pointer never changes (see rules in netns/generic.h).
	 *
	 * That said, we simply duplicate this array and schedule
	 * the old copy for kfree after a grace period.
	 */

	ng->len = id;
	memcpy(&ng->ptr, &old_ng->ptr, old_ng->len);

	rcu_assign_pointer(net->gen, ng);
	call_rcu(&old_ng->rcu, net_generic_release);
assign:
	ng->ptr[id - 1] = data;
	return 0;
}
EXPORT_SYMBOL_GPL(net_assign_generic);
