// SPDX-License-Identifier: GPL-2.0-only
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/workqueue.h>
#include <linux/rtnetlink.h>
#include <linux/cache.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/idr.h>
#include <linux/rculist.h>
#include <linux/nsproxy.h>
#include <linux/fs.h>
#include <linux/proc_ns.h>
#include <linux/file.h>
#include <linux/export.h>
#include <linux/user_namespace.h>
#include <linux/net_namespace.h>
#include <linux/sched/task.h>
#include <linux/uidgid.h>

#include <net/sock.h>
#include <net/netlink.h>
#include <net/net_namespace.h>
#include <net/netns/generic.h>

/*
 *	Our network namespace constructor/destructor lists
 */

static LIST_HEAD(pernet_list);
static struct list_head *first_device = &pernet_list;

LIST_HEAD(net_namespace_list);
EXPORT_SYMBOL_GPL(net_namespace_list);

/* Protects net_namespace_list. Nests iside rtnl_lock() */
DECLARE_RWSEM(net_rwsem);
EXPORT_SYMBOL_GPL(net_rwsem);

#ifdef CONFIG_KEYS
static struct key_tag init_net_key_domain = { .usage = REFCOUNT_INIT(1) };
#endif

struct net init_net = {
	.count		= REFCOUNT_INIT(1),
	.dev_base_head	= LIST_HEAD_INIT(init_net.dev_base_head),
#ifdef CONFIG_KEYS
	.key_domain	= &init_net_key_domain,
#endif
};
EXPORT_SYMBOL(init_net);

static bool init_net_initialized;
/*
 * pernet_ops_rwsem: protects: pernet_list, net_generic_ids,
 * init_net_initialized and first_device pointer.
 * This is internal net namespace object. Please, don't use it
 * outside.
 */
DECLARE_RWSEM(pernet_ops_rwsem);
EXPORT_SYMBOL_GPL(pernet_ops_rwsem);

#define MIN_PERNET_OPS_ID	\
	((sizeof(struct net_generic) + sizeof(void *) - 1) / sizeof(void *))

#define INITIAL_NET_GEN_PTRS	13 /* +1 for len +2 for rcu_head */

static unsigned int max_gen_ptrs = INITIAL_NET_GEN_PTRS;

static struct net_generic *net_alloc_generic(void)
{
	struct net_generic *ng;
	unsigned int generic_size = offsetof(struct net_generic, ptr[max_gen_ptrs]);

	ng = kzalloc(generic_size, GFP_KERNEL);
	if (ng)
		ng->s.len = max_gen_ptrs;

	return ng;
}

static int net_assign_generic(struct net *net, unsigned int id, void *data)
{
	struct net_generic *ng, *old_ng;

	BUG_ON(id < MIN_PERNET_OPS_ID);

	old_ng = rcu_dereference_protected(net->gen,
					   lockdep_is_held(&pernet_ops_rwsem));
	if (old_ng->s.len > id) {
		old_ng->ptr[id] = data;
		return 0;
	}

	ng = net_alloc_generic();
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

	memcpy(&ng->ptr[MIN_PERNET_OPS_ID], &old_ng->ptr[MIN_PERNET_OPS_ID],
	       (old_ng->s.len - MIN_PERNET_OPS_ID) * sizeof(void *));
	ng->ptr[id] = data;

	rcu_assign_pointer(net->gen, ng);
	kfree_rcu(old_ng, s.rcu);
	return 0;
}

static int ops_init(const struct pernet_operations *ops, struct net *net)
{
	int err = -ENOMEM;
	void *data = NULL;

	if (ops->id && ops->size) {
		data = kzalloc(ops->size, GFP_KERNEL);
		if (!data)
			goto out;

		err = net_assign_generic(net, *ops->id, data);
		if (err)
			goto cleanup;
	}
	err = 0;
	if (ops->init)
		err = ops->init(net);
	if (!err)
		return 0;

cleanup:
	kfree(data);

out:
	return err;
}

static void ops_free(const struct pernet_operations *ops, struct net *net)
{
	if (ops->id && ops->size) {
		kfree(net_generic(net, *ops->id));
	}
}

static void ops_pre_exit_list(const struct pernet_operations *ops,
			      struct list_head *net_exit_list)
{
	struct net *net;

	if (ops->pre_exit) {
		list_for_each_entry(net, net_exit_list, exit_list)
			ops->pre_exit(net);
	}
}

static void ops_exit_list(const struct pernet_operations *ops,
			  struct list_head *net_exit_list)
{
	struct net *net;
	if (ops->exit) {
		list_for_each_entry(net, net_exit_list, exit_list)
			ops->exit(net);
	}
	if (ops->exit_batch)
		ops->exit_batch(net_exit_list);
}

static void ops_free_list(const struct pernet_operations *ops,
			  struct list_head *net_exit_list)
{
	struct net *net;
	if (ops->size && ops->id) {
		list_for_each_entry(net, net_exit_list, exit_list)
			ops_free(ops, net);
	}
}

/* should be called with nsid_lock held */
static int alloc_netid(struct net *net, struct net *peer, int reqid)
{
	int min = 0, max = 0;

	if (reqid >= 0) {
		min = reqid;
		max = reqid + 1;
	}

	return idr_alloc(&net->netns_ids, peer, min, max, GFP_ATOMIC);
}

/* This function is used by idr_for_each(). If net is equal to peer, the
 * function returns the id so that idr_for_each() stops. Because we cannot
 * returns the id 0 (idr_for_each() will not stop), we return the magic value
 * NET_ID_ZERO (-1) for it.
 */
#define NET_ID_ZERO -1
static int net_eq_idr(int id, void *net, void *peer)
{
	if (net_eq(net, peer))
		return id ? : NET_ID_ZERO;
	return 0;
}

/* Should be called with nsid_lock held. If a new id is assigned, the bool alloc
 * is set to true, thus the caller knows that the new id must be notified via
 * rtnl.
 */
static int __peernet2id_alloc(struct net *net, struct net *peer, bool *alloc)
{
	int id = idr_for_each(&net->netns_ids, net_eq_idr, peer);
	bool alloc_it = *alloc;

	*alloc = false;

	/* Magic value for id 0. */
	if (id == NET_ID_ZERO)
		return 0;
	if (id > 0)
		return id;

	if (alloc_it) {
		id = alloc_netid(net, peer, -1);
		*alloc = true;
		return id >= 0 ? id : NETNSA_NSID_NOT_ASSIGNED;
	}

	return NETNSA_NSID_NOT_ASSIGNED;
}

/* should be called with nsid_lock held */
static int __peernet2id(struct net *net, struct net *peer)
{
	bool no = false;

	return __peernet2id_alloc(net, peer, &no);
}

static void rtnl_net_notifyid(struct net *net, int cmd, int id, u32 portid,
			      struct nlmsghdr *nlh);
/* This function returns the id of a peer netns. If no id is assigned, one will
 * be allocated and returned.
 */
int peernet2id_alloc(struct net *net, struct net *peer)
{
	bool alloc = false, alive = false;
	int id;

	if (refcount_read(&net->count) == 0)
		return NETNSA_NSID_NOT_ASSIGNED;
	spin_lock_bh(&net->nsid_lock);
	/*
	 * When peer is obtained from RCU lists, we may race with
	 * its cleanup. Check whether it's alive, and this guarantees
	 * we never hash a peer back to net->netns_ids, after it has
	 * just been idr_remove()'d from there in cleanup_net().
	 */
	if (maybe_get_net(peer))
		alive = alloc = true;
	id = __peernet2id_alloc(net, peer, &alloc);
	spin_unlock_bh(&net->nsid_lock);
	if (alloc && id >= 0)
		rtnl_net_notifyid(net, RTM_NEWNSID, id, 0, NULL);
	if (alive)
		put_net(peer);
	return id;
}
EXPORT_SYMBOL_GPL(peernet2id_alloc);

/* This function returns, if assigned, the id of a peer netns. */
int peernet2id(struct net *net, struct net *peer)
{
	int id;

	spin_lock_bh(&net->nsid_lock);
	id = __peernet2id(net, peer);
	spin_unlock_bh(&net->nsid_lock);
	return id;
}
EXPORT_SYMBOL(peernet2id);

/* This function returns true is the peer netns has an id assigned into the
 * current netns.
 */
bool peernet_has_id(struct net *net, struct net *peer)
{
	return peernet2id(net, peer) >= 0;
}

struct net *get_net_ns_by_id(struct net *net, int id)
{
	struct net *peer;

	if (id < 0)
		return NULL;

	rcu_read_lock();
	peer = idr_find(&net->netns_ids, id);
	if (peer)
		peer = maybe_get_net(peer);
	rcu_read_unlock();

	return peer;
}

/*
 * setup_net runs the initializers for the network namespace object.
 */
static __net_init int setup_net(struct net *net, struct user_namespace *user_ns)
{
	/* Must be called with pernet_ops_rwsem held */
	const struct pernet_operations *ops, *saved_ops;
	int error = 0;
	LIST_HEAD(net_exit_list);

	refcount_set(&net->count, 1);
	refcount_set(&net->passive, 1);
	get_random_bytes(&net->hash_mix, sizeof(u32));
	net->dev_base_seq = 1;
	net->user_ns = user_ns;
	idr_init(&net->netns_ids);
	spin_lock_init(&net->nsid_lock);
	mutex_init(&net->ipv4.ra_mutex);

	list_for_each_entry(ops, &pernet_list, list) {
		error = ops_init(ops, net);
		if (error < 0)
			goto out_undo;
	}
	down_write(&net_rwsem);
	list_add_tail_rcu(&net->list, &net_namespace_list);
	up_write(&net_rwsem);
out:
	return error;

out_undo:
	/* Walk through the list backwards calling the exit functions
	 * for the pernet modules whose init functions did not fail.
	 */
	list_add(&net->exit_list, &net_exit_list);
	saved_ops = ops;
	list_for_each_entry_continue_reverse(ops, &pernet_list, list)
		ops_pre_exit_list(ops, &net_exit_list);

	synchronize_rcu();

	ops = saved_ops;
	list_for_each_entry_continue_reverse(ops, &pernet_list, list)
		ops_exit_list(ops, &net_exit_list);

	ops = saved_ops;
	list_for_each_entry_continue_reverse(ops, &pernet_list, list)
		ops_free_list(ops, &net_exit_list);

	rcu_barrier();
	goto out;
}

static int __net_init net_defaults_init_net(struct net *net)
{
	net->core.sysctl_somaxconn = SOMAXCONN;
	return 0;
}

static struct pernet_operations net_defaults_ops = {
	.init = net_defaults_init_net,
};

static __init int net_defaults_init(void)
{
	if (register_pernet_subsys(&net_defaults_ops))
		panic("Cannot initialize net default settings");

	return 0;
}

core_initcall(net_defaults_init);

#ifdef CONFIG_NET_NS
static struct ucounts *inc_net_namespaces(struct user_namespace *ns)
{
	return inc_ucount(ns, current_euid(), UCOUNT_NET_NAMESPACES);
}

static void dec_net_namespaces(struct ucounts *ucounts)
{
	dec_ucount(ucounts, UCOUNT_NET_NAMESPACES);
}

static struct kmem_cache *net_cachep __ro_after_init;
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

#ifdef CONFIG_KEYS
	net->key_domain = kzalloc(sizeof(struct key_tag), GFP_KERNEL);
	if (!net->key_domain)
		goto out_free_2;
	refcount_set(&net->key_domain->usage, 1);
#endif

	rcu_assign_pointer(net->gen, ng);
out:
	return net;

#ifdef CONFIG_KEYS
out_free_2:
	kmem_cache_free(net_cachep, net);
	net = NULL;
#endif
out_free:
	kfree(ng);
	goto out;
}

static void net_free(struct net *net)
{
	kfree(rcu_access_pointer(net->gen));
	kmem_cache_free(net_cachep, net);
}

void net_drop_ns(void *p)
{
	struct net *ns = p;
	if (ns && refcount_dec_and_test(&ns->passive))
		net_free(ns);
}

struct net *copy_net_ns(unsigned long flags,
			struct user_namespace *user_ns, struct net *old_net)
{
	struct ucounts *ucounts;
	struct net *net;
	int rv;

	if (!(flags & CLONE_NEWNET))
		return get_net(old_net);

	ucounts = inc_net_namespaces(user_ns);
	if (!ucounts)
		return ERR_PTR(-ENOSPC);

	net = net_alloc();
	if (!net) {
		rv = -ENOMEM;
		goto dec_ucounts;
	}
	refcount_set(&net->passive, 1);
	net->ucounts = ucounts;
	get_user_ns(user_ns);

	rv = down_read_killable(&pernet_ops_rwsem);
	if (rv < 0)
		goto put_userns;

	rv = setup_net(net, user_ns);

	up_read(&pernet_ops_rwsem);

	if (rv < 0) {
put_userns:
		put_user_ns(user_ns);
		net_drop_ns(net);
dec_ucounts:
		dec_net_namespaces(ucounts);
		return ERR_PTR(rv);
	}
	return net;
}

/**
 * net_ns_get_ownership - get sysfs ownership data for @net
 * @net: network namespace in question (can be NULL)
 * @uid: kernel user ID for sysfs objects
 * @gid: kernel group ID for sysfs objects
 *
 * Returns the uid/gid pair of root in the user namespace associated with the
 * given network namespace.
 */
void net_ns_get_ownership(const struct net *net, kuid_t *uid, kgid_t *gid)
{
	if (net) {
		kuid_t ns_root_uid = make_kuid(net->user_ns, 0);
		kgid_t ns_root_gid = make_kgid(net->user_ns, 0);

		if (uid_valid(ns_root_uid))
			*uid = ns_root_uid;

		if (gid_valid(ns_root_gid))
			*gid = ns_root_gid;
	} else {
		*uid = GLOBAL_ROOT_UID;
		*gid = GLOBAL_ROOT_GID;
	}
}
EXPORT_SYMBOL_GPL(net_ns_get_ownership);

static void unhash_nsid(struct net *net, struct net *last)
{
	struct net *tmp;
	/* This function is only called from cleanup_net() work,
	 * and this work is the only process, that may delete
	 * a net from net_namespace_list. So, when the below
	 * is executing, the list may only grow. Thus, we do not
	 * use for_each_net_rcu() or net_rwsem.
	 */
	for_each_net(tmp) {
		int id;

		spin_lock_bh(&tmp->nsid_lock);
		id = __peernet2id(tmp, net);
		if (id >= 0)
			idr_remove(&tmp->netns_ids, id);
		spin_unlock_bh(&tmp->nsid_lock);
		if (id >= 0)
			rtnl_net_notifyid(tmp, RTM_DELNSID, id, 0, NULL);
		if (tmp == last)
			break;
	}
	spin_lock_bh(&net->nsid_lock);
	idr_destroy(&net->netns_ids);
	spin_unlock_bh(&net->nsid_lock);
}

static LLIST_HEAD(cleanup_list);

static void cleanup_net(struct work_struct *work)
{
	const struct pernet_operations *ops;
	struct net *net, *tmp, *last;
	struct llist_node *net_kill_list;
	LIST_HEAD(net_exit_list);

	/* Atomically snapshot the list of namespaces to cleanup */
	net_kill_list = llist_del_all(&cleanup_list);

	down_read(&pernet_ops_rwsem);

	/* Don't let anyone else find us. */
	down_write(&net_rwsem);
	llist_for_each_entry(net, net_kill_list, cleanup_list)
		list_del_rcu(&net->list);
	/* Cache last net. After we unlock rtnl, no one new net
	 * added to net_namespace_list can assign nsid pointer
	 * to a net from net_kill_list (see peernet2id_alloc()).
	 * So, we skip them in unhash_nsid().
	 *
	 * Note, that unhash_nsid() does not delete nsid links
	 * between net_kill_list's nets, as they've already
	 * deleted from net_namespace_list. But, this would be
	 * useless anyway, as netns_ids are destroyed there.
	 */
	last = list_last_entry(&net_namespace_list, struct net, list);
	up_write(&net_rwsem);

	llist_for_each_entry(net, net_kill_list, cleanup_list) {
		unhash_nsid(net, last);
		list_add_tail(&net->exit_list, &net_exit_list);
	}

	/* Run all of the network namespace pre_exit methods */
	list_for_each_entry_reverse(ops, &pernet_list, list)
		ops_pre_exit_list(ops, &net_exit_list);

	/*
	 * Another CPU might be rcu-iterating the list, wait for it.
	 * This needs to be before calling the exit() notifiers, so
	 * the rcu_barrier() below isn't sufficient alone.
	 * Also the pre_exit() and exit() methods need this barrier.
	 */
	synchronize_rcu();

	/* Run all of the network namespace exit methods */
	list_for_each_entry_reverse(ops, &pernet_list, list)
		ops_exit_list(ops, &net_exit_list);

	/* Free the net generic variables */
	list_for_each_entry_reverse(ops, &pernet_list, list)
		ops_free_list(ops, &net_exit_list);

	up_read(&pernet_ops_rwsem);

	/* Ensure there are no outstanding rcu callbacks using this
	 * network namespace.
	 */
	rcu_barrier();

	/* Finally it is safe to free my network namespace structure */
	list_for_each_entry_safe(net, tmp, &net_exit_list, exit_list) {
		list_del_init(&net->exit_list);
		dec_net_namespaces(net->ucounts);
		key_remove_domain(net->key_domain);
		put_user_ns(net->user_ns);
		net_drop_ns(net);
	}
}

/**
 * net_ns_barrier - wait until concurrent net_cleanup_work is done
 *
 * cleanup_net runs from work queue and will first remove namespaces
 * from the global list, then run net exit functions.
 *
 * Call this in module exit path to make sure that all netns
 * ->exit ops have been invoked before the function is removed.
 */
void net_ns_barrier(void)
{
	down_write(&pernet_ops_rwsem);
	up_write(&pernet_ops_rwsem);
}
EXPORT_SYMBOL(net_ns_barrier);

static DECLARE_WORK(net_cleanup_work, cleanup_net);

void __put_net(struct net *net)
{
	/* Cleanup the network namespace in process context */
	if (llist_add(&net->cleanup_list, &cleanup_list))
		queue_work(netns_wq, &net_cleanup_work);
}
EXPORT_SYMBOL_GPL(__put_net);

struct net *get_net_ns_by_fd(int fd)
{
	struct file *file;
	struct ns_common *ns;
	struct net *net;

	file = proc_ns_fget(fd);
	if (IS_ERR(file))
		return ERR_CAST(file);

	ns = get_proc_ns(file_inode(file));
	if (ns->ops == &netns_operations)
		net = get_net(container_of(ns, struct net, ns));
	else
		net = ERR_PTR(-EINVAL);

	fput(file);
	return net;
}

#else
struct net *get_net_ns_by_fd(int fd)
{
	return ERR_PTR(-EINVAL);
}
#endif
EXPORT_SYMBOL_GPL(get_net_ns_by_fd);

struct net *get_net_ns_by_pid(pid_t pid)
{
	struct task_struct *tsk;
	struct net *net;

	/* Lookup the network namespace */
	net = ERR_PTR(-ESRCH);
	rcu_read_lock();
	tsk = find_task_by_vpid(pid);
	if (tsk) {
		struct nsproxy *nsproxy;
		task_lock(tsk);
		nsproxy = tsk->nsproxy;
		if (nsproxy)
			net = get_net(nsproxy->net_ns);
		task_unlock(tsk);
	}
	rcu_read_unlock();
	return net;
}
EXPORT_SYMBOL_GPL(get_net_ns_by_pid);

static __net_init int net_ns_net_init(struct net *net)
{
#ifdef CONFIG_NET_NS
	net->ns.ops = &netns_operations;
#endif
	return ns_alloc_inum(&net->ns);
}

static __net_exit void net_ns_net_exit(struct net *net)
{
	ns_free_inum(&net->ns);
}

static struct pernet_operations __net_initdata net_ns_ops = {
	.init = net_ns_net_init,
	.exit = net_ns_net_exit,
};

static const struct nla_policy rtnl_net_policy[NETNSA_MAX + 1] = {
	[NETNSA_NONE]		= { .type = NLA_UNSPEC },
	[NETNSA_NSID]		= { .type = NLA_S32 },
	[NETNSA_PID]		= { .type = NLA_U32 },
	[NETNSA_FD]		= { .type = NLA_U32 },
	[NETNSA_TARGET_NSID]	= { .type = NLA_S32 },
};

static int rtnl_net_newid(struct sk_buff *skb, struct nlmsghdr *nlh,
			  struct netlink_ext_ack *extack)
{
	struct net *net = sock_net(skb->sk);
	struct nlattr *tb[NETNSA_MAX + 1];
	struct nlattr *nla;
	struct net *peer;
	int nsid, err;

	err = nlmsg_parse_deprecated(nlh, sizeof(struct rtgenmsg), tb,
				     NETNSA_MAX, rtnl_net_policy, extack);
	if (err < 0)
		return err;
	if (!tb[NETNSA_NSID]) {
		NL_SET_ERR_MSG(extack, "nsid is missing");
		return -EINVAL;
	}
	nsid = nla_get_s32(tb[NETNSA_NSID]);

	if (tb[NETNSA_PID]) {
		peer = get_net_ns_by_pid(nla_get_u32(tb[NETNSA_PID]));
		nla = tb[NETNSA_PID];
	} else if (tb[NETNSA_FD]) {
		peer = get_net_ns_by_fd(nla_get_u32(tb[NETNSA_FD]));
		nla = tb[NETNSA_FD];
	} else {
		NL_SET_ERR_MSG(extack, "Peer netns reference is missing");
		return -EINVAL;
	}
	if (IS_ERR(peer)) {
		NL_SET_BAD_ATTR(extack, nla);
		NL_SET_ERR_MSG(extack, "Peer netns reference is invalid");
		return PTR_ERR(peer);
	}

	spin_lock_bh(&net->nsid_lock);
	if (__peernet2id(net, peer) >= 0) {
		spin_unlock_bh(&net->nsid_lock);
		err = -EEXIST;
		NL_SET_BAD_ATTR(extack, nla);
		NL_SET_ERR_MSG(extack,
			       "Peer netns already has a nsid assigned");
		goto out;
	}

	err = alloc_netid(net, peer, nsid);
	spin_unlock_bh(&net->nsid_lock);
	if (err >= 0) {
		rtnl_net_notifyid(net, RTM_NEWNSID, err, NETLINK_CB(skb).portid,
				  nlh);
		err = 0;
	} else if (err == -ENOSPC && nsid >= 0) {
		err = -EEXIST;
		NL_SET_BAD_ATTR(extack, tb[NETNSA_NSID]);
		NL_SET_ERR_MSG(extack, "The specified nsid is already used");
	}
out:
	put_net(peer);
	return err;
}

static int rtnl_net_get_size(void)
{
	return NLMSG_ALIGN(sizeof(struct rtgenmsg))
	       + nla_total_size(sizeof(s32)) /* NETNSA_NSID */
	       + nla_total_size(sizeof(s32)) /* NETNSA_CURRENT_NSID */
	       ;
}

struct net_fill_args {
	u32 portid;
	u32 seq;
	int flags;
	int cmd;
	int nsid;
	bool add_ref;
	int ref_nsid;
};

static int rtnl_net_fill(struct sk_buff *skb, struct net_fill_args *args)
{
	struct nlmsghdr *nlh;
	struct rtgenmsg *rth;

	nlh = nlmsg_put(skb, args->portid, args->seq, args->cmd, sizeof(*rth),
			args->flags);
	if (!nlh)
		return -EMSGSIZE;

	rth = nlmsg_data(nlh);
	rth->rtgen_family = AF_UNSPEC;

	if (nla_put_s32(skb, NETNSA_NSID, args->nsid))
		goto nla_put_failure;

	if (args->add_ref &&
	    nla_put_s32(skb, NETNSA_CURRENT_NSID, args->ref_nsid))
		goto nla_put_failure;

	nlmsg_end(skb, nlh);
	return 0;

nla_put_failure:
	nlmsg_cancel(skb, nlh);
	return -EMSGSIZE;
}

static int rtnl_net_valid_getid_req(struct sk_buff *skb,
				    const struct nlmsghdr *nlh,
				    struct nlattr **tb,
				    struct netlink_ext_ack *extack)
{
	int i, err;

	if (!netlink_strict_get_check(skb))
		return nlmsg_parse_deprecated(nlh, sizeof(struct rtgenmsg),
					      tb, NETNSA_MAX, rtnl_net_policy,
					      extack);

	err = nlmsg_parse_deprecated_strict(nlh, sizeof(struct rtgenmsg), tb,
					    NETNSA_MAX, rtnl_net_policy,
					    extack);
	if (err)
		return err;

	for (i = 0; i <= NETNSA_MAX; i++) {
		if (!tb[i])
			continue;

		switch (i) {
		case NETNSA_PID:
		case NETNSA_FD:
		case NETNSA_NSID:
		case NETNSA_TARGET_NSID:
			break;
		default:
			NL_SET_ERR_MSG(extack, "Unsupported attribute in peer netns getid request");
			return -EINVAL;
		}
	}

	return 0;
}

static int rtnl_net_getid(struct sk_buff *skb, struct nlmsghdr *nlh,
			  struct netlink_ext_ack *extack)
{
	struct net *net = sock_net(skb->sk);
	struct nlattr *tb[NETNSA_MAX + 1];
	struct net_fill_args fillargs = {
		.portid = NETLINK_CB(skb).portid,
		.seq = nlh->nlmsg_seq,
		.cmd = RTM_NEWNSID,
	};
	struct net *peer, *target = net;
	struct nlattr *nla;
	struct sk_buff *msg;
	int err;

	err = rtnl_net_valid_getid_req(skb, nlh, tb, extack);
	if (err < 0)
		return err;
	if (tb[NETNSA_PID]) {
		peer = get_net_ns_by_pid(nla_get_u32(tb[NETNSA_PID]));
		nla = tb[NETNSA_PID];
	} else if (tb[NETNSA_FD]) {
		peer = get_net_ns_by_fd(nla_get_u32(tb[NETNSA_FD]));
		nla = tb[NETNSA_FD];
	} else if (tb[NETNSA_NSID]) {
		peer = get_net_ns_by_id(net, nla_get_s32(tb[NETNSA_NSID]));
		if (!peer)
			peer = ERR_PTR(-ENOENT);
		nla = tb[NETNSA_NSID];
	} else {
		NL_SET_ERR_MSG(extack, "Peer netns reference is missing");
		return -EINVAL;
	}

	if (IS_ERR(peer)) {
		NL_SET_BAD_ATTR(extack, nla);
		NL_SET_ERR_MSG(extack, "Peer netns reference is invalid");
		return PTR_ERR(peer);
	}

	if (tb[NETNSA_TARGET_NSID]) {
		int id = nla_get_s32(tb[NETNSA_TARGET_NSID]);

		target = rtnl_get_net_ns_capable(NETLINK_CB(skb).sk, id);
		if (IS_ERR(target)) {
			NL_SET_BAD_ATTR(extack, tb[NETNSA_TARGET_NSID]);
			NL_SET_ERR_MSG(extack,
				       "Target netns reference is invalid");
			err = PTR_ERR(target);
			goto out;
		}
		fillargs.add_ref = true;
		fillargs.ref_nsid = peernet2id(net, peer);
	}

	msg = nlmsg_new(rtnl_net_get_size(), GFP_KERNEL);
	if (!msg) {
		err = -ENOMEM;
		goto out;
	}

	fillargs.nsid = peernet2id(target, peer);
	err = rtnl_net_fill(msg, &fillargs);
	if (err < 0)
		goto err_out;

	err = rtnl_unicast(msg, net, NETLINK_CB(skb).portid);
	goto out;

err_out:
	nlmsg_free(msg);
out:
	if (fillargs.add_ref)
		put_net(target);
	put_net(peer);
	return err;
}

struct rtnl_net_dump_cb {
	struct net *tgt_net;
	struct net *ref_net;
	struct sk_buff *skb;
	struct net_fill_args fillargs;
	int idx;
	int s_idx;
};

static int rtnl_net_dumpid_one(int id, void *peer, void *data)
{
	struct rtnl_net_dump_cb *net_cb = (struct rtnl_net_dump_cb *)data;
	int ret;

	if (net_cb->idx < net_cb->s_idx)
		goto cont;

	net_cb->fillargs.nsid = id;
	if (net_cb->fillargs.add_ref)
		net_cb->fillargs.ref_nsid = __peernet2id(net_cb->ref_net, peer);
	ret = rtnl_net_fill(net_cb->skb, &net_cb->fillargs);
	if (ret < 0)
		return ret;

cont:
	net_cb->idx++;
	return 0;
}

static int rtnl_valid_dump_net_req(const struct nlmsghdr *nlh, struct sock *sk,
				   struct rtnl_net_dump_cb *net_cb,
				   struct netlink_callback *cb)
{
	struct netlink_ext_ack *extack = cb->extack;
	struct nlattr *tb[NETNSA_MAX + 1];
	int err, i;

	err = nlmsg_parse_deprecated_strict(nlh, sizeof(struct rtgenmsg), tb,
					    NETNSA_MAX, rtnl_net_policy,
					    extack);
	if (err < 0)
		return err;

	for (i = 0; i <= NETNSA_MAX; i++) {
		if (!tb[i])
			continue;

		if (i == NETNSA_TARGET_NSID) {
			struct net *net;

			net = rtnl_get_net_ns_capable(sk, nla_get_s32(tb[i]));
			if (IS_ERR(net)) {
				NL_SET_BAD_ATTR(extack, tb[i]);
				NL_SET_ERR_MSG(extack,
					       "Invalid target network namespace id");
				return PTR_ERR(net);
			}
			net_cb->fillargs.add_ref = true;
			net_cb->ref_net = net_cb->tgt_net;
			net_cb->tgt_net = net;
		} else {
			NL_SET_BAD_ATTR(extack, tb[i]);
			NL_SET_ERR_MSG(extack,
				       "Unsupported attribute in dump request");
			return -EINVAL;
		}
	}

	return 0;
}

static int rtnl_net_dumpid(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct rtnl_net_dump_cb net_cb = {
		.tgt_net = sock_net(skb->sk),
		.skb = skb,
		.fillargs = {
			.portid = NETLINK_CB(cb->skb).portid,
			.seq = cb->nlh->nlmsg_seq,
			.flags = NLM_F_MULTI,
			.cmd = RTM_NEWNSID,
		},
		.idx = 0,
		.s_idx = cb->args[0],
	};
	int err = 0;

	if (cb->strict_check) {
		err = rtnl_valid_dump_net_req(cb->nlh, skb->sk, &net_cb, cb);
		if (err < 0)
			goto end;
	}

	spin_lock_bh(&net_cb.tgt_net->nsid_lock);
	if (net_cb.fillargs.add_ref &&
	    !net_eq(net_cb.ref_net, net_cb.tgt_net) &&
	    !spin_trylock_bh(&net_cb.ref_net->nsid_lock)) {
		spin_unlock_bh(&net_cb.tgt_net->nsid_lock);
		err = -EAGAIN;
		goto end;
	}
	idr_for_each(&net_cb.tgt_net->netns_ids, rtnl_net_dumpid_one, &net_cb);
	if (net_cb.fillargs.add_ref &&
	    !net_eq(net_cb.ref_net, net_cb.tgt_net))
		spin_unlock_bh(&net_cb.ref_net->nsid_lock);
	spin_unlock_bh(&net_cb.tgt_net->nsid_lock);

	cb->args[0] = net_cb.idx;
end:
	if (net_cb.fillargs.add_ref)
		put_net(net_cb.tgt_net);
	return err < 0 ? err : skb->len;
}

static void rtnl_net_notifyid(struct net *net, int cmd, int id, u32 portid,
			      struct nlmsghdr *nlh)
{
	struct net_fill_args fillargs = {
		.portid = portid,
		.seq = nlh ? nlh->nlmsg_seq : 0,
		.cmd = cmd,
		.nsid = id,
	};
	struct sk_buff *msg;
	int err = -ENOMEM;

	msg = nlmsg_new(rtnl_net_get_size(), GFP_KERNEL);
	if (!msg)
		goto out;

	err = rtnl_net_fill(msg, &fillargs);
	if (err < 0)
		goto err_out;

	rtnl_notify(msg, net, portid, RTNLGRP_NSID, nlh, 0);
	return;

err_out:
	nlmsg_free(msg);
out:
	rtnl_set_sk_err(net, RTNLGRP_NSID, err);
}

static int __init net_ns_init(void)
{
	struct net_generic *ng;

#ifdef CONFIG_NET_NS
	net_cachep = kmem_cache_create("net_namespace", sizeof(struct net),
					SMP_CACHE_BYTES,
					SLAB_PANIC|SLAB_ACCOUNT, NULL);

	/* Create workqueue for cleanup */
	netns_wq = create_singlethread_workqueue("netns");
	if (!netns_wq)
		panic("Could not create netns workq");
#endif

	ng = net_alloc_generic();
	if (!ng)
		panic("Could not allocate generic netns");

	rcu_assign_pointer(init_net.gen, ng);

	down_write(&pernet_ops_rwsem);
	if (setup_net(&init_net, &init_user_ns))
		panic("Could not setup the initial network namespace");

	init_net_initialized = true;
	up_write(&pernet_ops_rwsem);

	if (register_pernet_subsys(&net_ns_ops))
		panic("Could not register network namespace subsystems");

	rtnl_register(PF_UNSPEC, RTM_NEWNSID, rtnl_net_newid, NULL,
		      RTNL_FLAG_DOIT_UNLOCKED);
	rtnl_register(PF_UNSPEC, RTM_GETNSID, rtnl_net_getid, rtnl_net_dumpid,
		      RTNL_FLAG_DOIT_UNLOCKED);

	return 0;
}

pure_initcall(net_ns_init);

#ifdef CONFIG_NET_NS
static int __register_pernet_operations(struct list_head *list,
					struct pernet_operations *ops)
{
	struct net *net;
	int error;
	LIST_HEAD(net_exit_list);

	list_add_tail(&ops->list, list);
	if (ops->init || (ops->id && ops->size)) {
		/* We held write locked pernet_ops_rwsem, and parallel
		 * setup_net() and cleanup_net() are not possible.
		 */
		for_each_net(net) {
			error = ops_init(ops, net);
			if (error)
				goto out_undo;
			list_add_tail(&net->exit_list, &net_exit_list);
		}
	}
	return 0;

out_undo:
	/* If I have an error cleanup all namespaces I initialized */
	list_del(&ops->list);
	ops_pre_exit_list(ops, &net_exit_list);
	synchronize_rcu();
	ops_exit_list(ops, &net_exit_list);
	ops_free_list(ops, &net_exit_list);
	return error;
}

static void __unregister_pernet_operations(struct pernet_operations *ops)
{
	struct net *net;
	LIST_HEAD(net_exit_list);

	list_del(&ops->list);
	/* See comment in __register_pernet_operations() */
	for_each_net(net)
		list_add_tail(&net->exit_list, &net_exit_list);
	ops_pre_exit_list(ops, &net_exit_list);
	synchronize_rcu();
	ops_exit_list(ops, &net_exit_list);
	ops_free_list(ops, &net_exit_list);
}

#else

static int __register_pernet_operations(struct list_head *list,
					struct pernet_operations *ops)
{
	if (!init_net_initialized) {
		list_add_tail(&ops->list, list);
		return 0;
	}

	return ops_init(ops, &init_net);
}

static void __unregister_pernet_operations(struct pernet_operations *ops)
{
	if (!init_net_initialized) {
		list_del(&ops->list);
	} else {
		LIST_HEAD(net_exit_list);
		list_add(&init_net.exit_list, &net_exit_list);
		ops_pre_exit_list(ops, &net_exit_list);
		synchronize_rcu();
		ops_exit_list(ops, &net_exit_list);
		ops_free_list(ops, &net_exit_list);
	}
}

#endif /* CONFIG_NET_NS */

static DEFINE_IDA(net_generic_ids);

static int register_pernet_operations(struct list_head *list,
				      struct pernet_operations *ops)
{
	int error;

	if (ops->id) {
		error = ida_alloc_min(&net_generic_ids, MIN_PERNET_OPS_ID,
				GFP_KERNEL);
		if (error < 0)
			return error;
		*ops->id = error;
		max_gen_ptrs = max(max_gen_ptrs, *ops->id + 1);
	}
	error = __register_pernet_operations(list, ops);
	if (error) {
		rcu_barrier();
		if (ops->id)
			ida_free(&net_generic_ids, *ops->id);
	}

	return error;
}

static void unregister_pernet_operations(struct pernet_operations *ops)
{
	__unregister_pernet_operations(ops);
	rcu_barrier();
	if (ops->id)
		ida_free(&net_generic_ids, *ops->id);
}

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
	down_write(&pernet_ops_rwsem);
	error =  register_pernet_operations(first_device, ops);
	up_write(&pernet_ops_rwsem);
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
void unregister_pernet_subsys(struct pernet_operations *ops)
{
	down_write(&pernet_ops_rwsem);
	unregister_pernet_operations(ops);
	up_write(&pernet_ops_rwsem);
}
EXPORT_SYMBOL_GPL(unregister_pernet_subsys);

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
	down_write(&pernet_ops_rwsem);
	error = register_pernet_operations(&pernet_list, ops);
	if (!error && (first_device == &pernet_list))
		first_device = &ops->list;
	up_write(&pernet_ops_rwsem);
	return error;
}
EXPORT_SYMBOL_GPL(register_pernet_device);

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
	down_write(&pernet_ops_rwsem);
	if (&ops->list == first_device)
		first_device = first_device->next;
	unregister_pernet_operations(ops);
	up_write(&pernet_ops_rwsem);
}
EXPORT_SYMBOL_GPL(unregister_pernet_device);

#ifdef CONFIG_NET_NS
static struct ns_common *netns_get(struct task_struct *task)
{
	struct net *net = NULL;
	struct nsproxy *nsproxy;

	task_lock(task);
	nsproxy = task->nsproxy;
	if (nsproxy)
		net = get_net(nsproxy->net_ns);
	task_unlock(task);

	return net ? &net->ns : NULL;
}

static inline struct net *to_net_ns(struct ns_common *ns)
{
	return container_of(ns, struct net, ns);
}

static void netns_put(struct ns_common *ns)
{
	put_net(to_net_ns(ns));
}

static int netns_install(struct nsproxy *nsproxy, struct ns_common *ns)
{
	struct net *net = to_net_ns(ns);

	if (!ns_capable(net->user_ns, CAP_SYS_ADMIN) ||
	    !ns_capable(current_user_ns(), CAP_SYS_ADMIN))
		return -EPERM;

	put_net(nsproxy->net_ns);
	nsproxy->net_ns = get_net(net);
	return 0;
}

static struct user_namespace *netns_owner(struct ns_common *ns)
{
	return to_net_ns(ns)->user_ns;
}

const struct proc_ns_operations netns_operations = {
	.name		= "net",
	.type		= CLONE_NEWNET,
	.get		= netns_get,
	.put		= netns_put,
	.install	= netns_install,
	.owner		= netns_owner,
};
#endif
