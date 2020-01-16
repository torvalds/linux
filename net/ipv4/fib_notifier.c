// SPDX-License-Identifier: GPL-2.0
#include <linux/rtnetlink.h>
#include <linux/yestifier.h>
#include <linux/socket.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <net/net_namespace.h>
#include <net/fib_yestifier.h>
#include <net/netns/ipv4.h>
#include <net/ip_fib.h>

int call_fib4_yestifier(struct yestifier_block *nb,
		       enum fib_event_type event_type,
		       struct fib_yestifier_info *info)
{
	info->family = AF_INET;
	return call_fib_yestifier(nb, event_type, info);
}

int call_fib4_yestifiers(struct net *net, enum fib_event_type event_type,
			struct fib_yestifier_info *info)
{
	ASSERT_RTNL();

	info->family = AF_INET;
	net->ipv4.fib_seq++;
	return call_fib_yestifiers(net, event_type, info);
}

static unsigned int fib4_seq_read(struct net *net)
{
	ASSERT_RTNL();

	return net->ipv4.fib_seq + fib4_rules_seq_read(net);
}

static int fib4_dump(struct net *net, struct yestifier_block *nb,
		     struct netlink_ext_ack *extack)
{
	int err;

	err = fib4_rules_dump(net, nb, extack);
	if (err)
		return err;

	return fib_yestify(net, nb, extack);
}

static const struct fib_yestifier_ops fib4_yestifier_ops_template = {
	.family		= AF_INET,
	.fib_seq_read	= fib4_seq_read,
	.fib_dump	= fib4_dump,
	.owner		= THIS_MODULE,
};

int __net_init fib4_yestifier_init(struct net *net)
{
	struct fib_yestifier_ops *ops;

	net->ipv4.fib_seq = 0;

	ops = fib_yestifier_ops_register(&fib4_yestifier_ops_template, net);
	if (IS_ERR(ops))
		return PTR_ERR(ops);
	net->ipv4.yestifier_ops = ops;

	return 0;
}

void __net_exit fib4_yestifier_exit(struct net *net)
{
	fib_yestifier_ops_unregister(net->ipv4.yestifier_ops);
}
