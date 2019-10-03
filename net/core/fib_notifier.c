#include <linux/rtnetlink.h>
#include <linux/notifier.h>
#include <linux/rcupdate.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <net/net_namespace.h>
#include <net/netns/generic.h>
#include <net/fib_notifier.h>

static unsigned int fib_notifier_net_id;

struct fib_notifier_net {
	struct list_head fib_notifier_ops;
	struct atomic_notifier_head fib_chain;
};

int call_fib_notifier(struct notifier_block *nb,
		      enum fib_event_type event_type,
		      struct fib_notifier_info *info)
{
	int err;

	err = nb->notifier_call(nb, event_type, info);
	return notifier_to_errno(err);
}
EXPORT_SYMBOL(call_fib_notifier);

int call_fib_notifiers(struct net *net, enum fib_event_type event_type,
		       struct fib_notifier_info *info)
{
	struct fib_notifier_net *fn_net = net_generic(net, fib_notifier_net_id);
	int err;

	err = atomic_notifier_call_chain(&fn_net->fib_chain, event_type, info);
	return notifier_to_errno(err);
}
EXPORT_SYMBOL(call_fib_notifiers);

static unsigned int fib_seq_sum(struct net *net)
{
	struct fib_notifier_net *fn_net = net_generic(net, fib_notifier_net_id);
	struct fib_notifier_ops *ops;
	unsigned int fib_seq = 0;

	rtnl_lock();
	rcu_read_lock();
	list_for_each_entry_rcu(ops, &fn_net->fib_notifier_ops, list) {
		if (!try_module_get(ops->owner))
			continue;
		fib_seq += ops->fib_seq_read(net);
		module_put(ops->owner);
	}
	rcu_read_unlock();
	rtnl_unlock();

	return fib_seq;
}

static int fib_net_dump(struct net *net, struct notifier_block *nb,
			struct netlink_ext_ack *extack)
{
	struct fib_notifier_net *fn_net = net_generic(net, fib_notifier_net_id);
	struct fib_notifier_ops *ops;
	int err = 0;

	rcu_read_lock();
	list_for_each_entry_rcu(ops, &fn_net->fib_notifier_ops, list) {
		if (!try_module_get(ops->owner))
			continue;
		err = ops->fib_dump(net, nb, extack);
		module_put(ops->owner);
		if (err)
			goto unlock;
	}

unlock:
	rcu_read_unlock();

	return err;
}

static bool fib_dump_is_consistent(struct net *net, struct notifier_block *nb,
				   void (*cb)(struct notifier_block *nb),
				   unsigned int fib_seq)
{
	struct fib_notifier_net *fn_net = net_generic(net, fib_notifier_net_id);

	atomic_notifier_chain_register(&fn_net->fib_chain, nb);
	if (fib_seq == fib_seq_sum(net))
		return true;
	atomic_notifier_chain_unregister(&fn_net->fib_chain, nb);
	if (cb)
		cb(nb);
	return false;
}

#define FIB_DUMP_MAX_RETRIES 5
int register_fib_notifier(struct net *net, struct notifier_block *nb,
			  void (*cb)(struct notifier_block *nb),
			  struct netlink_ext_ack *extack)
{
	int retries = 0;
	int err;

	do {
		unsigned int fib_seq = fib_seq_sum(net);

		err = fib_net_dump(net, nb, extack);
		if (err)
			return err;

		if (fib_dump_is_consistent(net, nb, cb, fib_seq))
			return 0;
	} while (++retries < FIB_DUMP_MAX_RETRIES);

	return -EBUSY;
}
EXPORT_SYMBOL(register_fib_notifier);

int unregister_fib_notifier(struct net *net, struct notifier_block *nb)
{
	struct fib_notifier_net *fn_net = net_generic(net, fib_notifier_net_id);

	return atomic_notifier_chain_unregister(&fn_net->fib_chain, nb);
}
EXPORT_SYMBOL(unregister_fib_notifier);

static int __fib_notifier_ops_register(struct fib_notifier_ops *ops,
				       struct net *net)
{
	struct fib_notifier_net *fn_net = net_generic(net, fib_notifier_net_id);
	struct fib_notifier_ops *o;

	list_for_each_entry(o, &fn_net->fib_notifier_ops, list)
		if (ops->family == o->family)
			return -EEXIST;
	list_add_tail_rcu(&ops->list, &fn_net->fib_notifier_ops);
	return 0;
}

struct fib_notifier_ops *
fib_notifier_ops_register(const struct fib_notifier_ops *tmpl, struct net *net)
{
	struct fib_notifier_ops *ops;
	int err;

	ops = kmemdup(tmpl, sizeof(*ops), GFP_KERNEL);
	if (!ops)
		return ERR_PTR(-ENOMEM);

	err = __fib_notifier_ops_register(ops, net);
	if (err)
		goto err_register;

	return ops;

err_register:
	kfree(ops);
	return ERR_PTR(err);
}
EXPORT_SYMBOL(fib_notifier_ops_register);

void fib_notifier_ops_unregister(struct fib_notifier_ops *ops)
{
	list_del_rcu(&ops->list);
	kfree_rcu(ops, rcu);
}
EXPORT_SYMBOL(fib_notifier_ops_unregister);

static int __net_init fib_notifier_net_init(struct net *net)
{
	struct fib_notifier_net *fn_net = net_generic(net, fib_notifier_net_id);

	INIT_LIST_HEAD(&fn_net->fib_notifier_ops);
	ATOMIC_INIT_NOTIFIER_HEAD(&fn_net->fib_chain);
	return 0;
}

static void __net_exit fib_notifier_net_exit(struct net *net)
{
	struct fib_notifier_net *fn_net = net_generic(net, fib_notifier_net_id);

	WARN_ON_ONCE(!list_empty(&fn_net->fib_notifier_ops));
}

static struct pernet_operations fib_notifier_net_ops = {
	.init = fib_notifier_net_init,
	.exit = fib_notifier_net_exit,
	.id = &fib_notifier_net_id,
	.size = sizeof(struct fib_notifier_net),
};

static int __init fib_notifier_init(void)
{
	return register_pernet_subsys(&fib_notifier_net_ops);
}

subsys_initcall(fib_notifier_init);
