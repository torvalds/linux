// SPDX-License-Identifier: GPL-2.0
#include <linux/rtnetlink.h>
#include <linux/notifier.h>
#include <linux/socket.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <net/net_namespace.h>
#include <net/fib_notifier.h>
#include <net/ip_fib.h>

int call_fib4_notifier(struct notifier_block *nb,
		       enum fib_event_type event_type,
		       struct fib_notifier_info *info)
{
	info->family = AF_INET;
	return call_fib_notifier(nb, event_type, info);
}

int call_fib4_notifiers(struct net *net, enum fib_event_type event_type,
			struct fib_notifier_info *info)
{
	ASSERT_RTNL();

	info->family = AF_INET;
	/* Paired with READ_ONCE() in fib4_seq_read() */
	WRITE_ONCE(net->ipv4.fib_seq, net->ipv4.fib_seq + 1);
	return call_fib_notifiers(net, event_type, info);
}

static unsigned int fib4_seq_read(const struct net *net)
{
	/* Paired with WRITE_ONCE() in call_fib4_notifiers() */
	return READ_ONCE(net->ipv4.fib_seq) + fib4_rules_seq_read(net);
}

static int fib4_dump(struct net *net, struct notifier_block *nb,
		     struct netlink_ext_ack *extack)
{
	int err;

	err = fib4_rules_dump(net, nb, extack);
	if (err)
		return err;

	return fib_notify(net, nb, extack);
}

static const struct fib_notifier_ops fib4_notifier_ops_template = {
	.family		= AF_INET,
	.fib_seq_read	= fib4_seq_read,
	.fib_dump	= fib4_dump,
	.owner		= THIS_MODULE,
};

int __net_init fib4_notifier_init(struct net *net)
{
	struct fib_notifier_ops *ops;

	net->ipv4.fib_seq = 0;

	ops = fib_notifier_ops_register(&fib4_notifier_ops_template, net);
	if (IS_ERR(ops))
		return PTR_ERR(ops);
	net->ipv4.notifier_ops = ops;

	return 0;
}

void __net_exit fib4_notifier_exit(struct net *net)
{
	fib_notifier_ops_unregister(net->ipv4.notifier_ops);
}
