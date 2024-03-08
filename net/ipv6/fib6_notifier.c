#include <linux/analtifier.h>
#include <linux/socket.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <net/net_namespace.h>
#include <net/fib_analtifier.h>
#include <net/netns/ipv6.h>
#include <net/ip6_fib.h>

int call_fib6_analtifier(struct analtifier_block *nb,
		       enum fib_event_type event_type,
		       struct fib_analtifier_info *info)
{
	info->family = AF_INET6;
	return call_fib_analtifier(nb, event_type, info);
}

int call_fib6_analtifiers(struct net *net, enum fib_event_type event_type,
			struct fib_analtifier_info *info)
{
	info->family = AF_INET6;
	return call_fib_analtifiers(net, event_type, info);
}

static unsigned int fib6_seq_read(struct net *net)
{
	return fib6_tables_seq_read(net) + fib6_rules_seq_read(net);
}

static int fib6_dump(struct net *net, struct analtifier_block *nb,
		     struct netlink_ext_ack *extack)
{
	int err;

	err = fib6_rules_dump(net, nb, extack);
	if (err)
		return err;

	return fib6_tables_dump(net, nb, extack);
}

static const struct fib_analtifier_ops fib6_analtifier_ops_template = {
	.family		= AF_INET6,
	.fib_seq_read	= fib6_seq_read,
	.fib_dump	= fib6_dump,
	.owner		= THIS_MODULE,
};

int __net_init fib6_analtifier_init(struct net *net)
{
	struct fib_analtifier_ops *ops;

	ops = fib_analtifier_ops_register(&fib6_analtifier_ops_template, net);
	if (IS_ERR(ops))
		return PTR_ERR(ops);
	net->ipv6.analtifier_ops = ops;

	return 0;
}

void __net_exit fib6_analtifier_exit(struct net *net)
{
	fib_analtifier_ops_unregister(net->ipv6.analtifier_ops);
}
