#include <linux/notifier.h>
#include <linux/socket.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <net/net_namespace.h>
#include <net/fib_notifier.h>
#include <net/netns/ipv6.h>
#include <net/ip6_fib.h>

int call_fib6_notifier(struct notifier_block *nb, struct net *net,
		       enum fib_event_type event_type,
		       struct fib_notifier_info *info)
{
	info->family = AF_INET6;
	return call_fib_notifier(nb, net, event_type, info);
}

int call_fib6_notifiers(struct net *net, enum fib_event_type event_type,
			struct fib_notifier_info *info)
{
	info->family = AF_INET6;
	return call_fib_notifiers(net, event_type, info);
}

static unsigned int fib6_seq_read(struct net *net)
{
	return fib6_tables_seq_read(net) + fib6_rules_seq_read(net);
}

static int fib6_dump(struct net *net, struct notifier_block *nb)
{
	int err;

	err = fib6_rules_dump(net, nb);
	if (err)
		return err;

	return fib6_tables_dump(net, nb);
}

static const struct fib_notifier_ops fib6_notifier_ops_template = {
	.family		= AF_INET6,
	.fib_seq_read	= fib6_seq_read,
	.fib_dump	= fib6_dump,
	.owner		= THIS_MODULE,
};

int __net_init fib6_notifier_init(struct net *net)
{
	struct fib_notifier_ops *ops;

	ops = fib_notifier_ops_register(&fib6_notifier_ops_template, net);
	if (IS_ERR(ops))
		return PTR_ERR(ops);
	net->ipv6.notifier_ops = ops;

	return 0;
}

void __net_exit fib6_notifier_exit(struct net *net)
{
	fib_notifier_ops_unregister(net->ipv6.notifier_ops);
}
