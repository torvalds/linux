#include <linux/rtnetlink.h>
#include <linux/notifier.h>
#include <linux/rcupdate.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <net/net_namespace.h>
#include <net/fib_notifier.h>

static ATOMIC_NOTIFIER_HEAD(fib_chain);

int call_fib_notifier(struct notifier_block *nb, struct net *net,
		      enum fib_event_type event_type,
		      struct fib_notifier_info *info)
{
	info->net = net;
	return nb->notifier_call(nb, event_type, info);
}
EXPORT_SYMBOL(call_fib_notifier);

int call_fib_notifiers(struct net *net, enum fib_event_type event_type,
		       struct fib_notifier_info *info)
{
	info->net = net;
	return atomic_notifier_call_chain(&fib_chain, event_type, info);
}
EXPORT_SYMBOL(call_fib_notifiers);

static unsigned int fib_seq_sum(void)
{
	struct fib_notifier_ops *ops;
	unsigned int fib_seq = 0;
	struct net *net;

	rtnl_lock();
	for_each_net(net) {
		rcu_read_lock();
		list_for_each_entry_rcu(ops, &net->fib_notifier_ops, list) {
			if (!try_module_get(ops->owner))
				continue;
			fib_seq += ops->fib_seq_read(net);
			module_put(ops->owner);
		}
		rcu_read_unlock();
	}
	rtnl_unlock();

	return fib_seq;
}

static int fib_net_dump(struct net *net, struct notifier_block *nb)
{
	struct fib_notifier_ops *ops;

	list_for_each_entry_rcu(ops, &net->fib_notifier_ops, list) {
		int err;

		if (!try_module_get(ops->owner))
			continue;
		err = ops->fib_dump(net, nb);
		module_put(ops->owner);
		if (err)
			return err;
	}

	return 0;
}

static bool fib_dump_is_consistent(struct notifier_block *nb,
				   void (*cb)(struct notifier_block *nb),
				   unsigned int fib_seq)
{
	atomic_notifier_chain_register(&fib_chain, nb);
	if (fib_seq == fib_seq_sum())
		return true;
	atomic_notifier_chain_unregister(&fib_chain, nb);
	if (cb)
		cb(nb);
	return false;
}

#define FIB_DUMP_MAX_RETRIES 5
int register_fib_notifier(struct notifier_block *nb,
			  void (*cb)(struct notifier_block *nb))
{
	int retries = 0;
	int err;

	do {
		unsigned int fib_seq = fib_seq_sum();
		struct net *net;

		rcu_read_lock();
		for_each_net_rcu(net) {
			err = fib_net_dump(net, nb);
			if (err)
				goto err_fib_net_dump;
		}
		rcu_read_unlock();

		if (fib_dump_is_consistent(nb, cb, fib_seq))
			return 0;
	} while (++retries < FIB_DUMP_MAX_RETRIES);

	return -EBUSY;

err_fib_net_dump:
	rcu_read_unlock();
	return err;
}
EXPORT_SYMBOL(register_fib_notifier);

int unregister_fib_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_unregister(&fib_chain, nb);
}
EXPORT_SYMBOL(unregister_fib_notifier);

static int __fib_notifier_ops_register(struct fib_notifier_ops *ops,
				       struct net *net)
{
	struct fib_notifier_ops *o;

	list_for_each_entry(o, &net->fib_notifier_ops, list)
		if (ops->family == o->family)
			return -EEXIST;
	list_add_tail_rcu(&ops->list, &net->fib_notifier_ops);
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
	INIT_LIST_HEAD(&net->fib_notifier_ops);
	return 0;
}

static void __net_exit fib_notifier_net_exit(struct net *net)
{
	WARN_ON_ONCE(!list_empty(&net->fib_notifier_ops));
}

static struct pernet_operations fib_notifier_net_ops = {
	.init = fib_notifier_net_init,
	.exit = fib_notifier_net_exit,
	.async = true,
};

static int __init fib_notifier_init(void)
{
	return register_pernet_subsys(&fib_notifier_net_ops);
}

subsys_initcall(fib_notifier_init);
