// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	"TEE" target extension for Xtables
 *	Copyright © Sebastian Claßen, 2007
 *	Jan Engelhardt, 2007-2010
 *
 *	based on ipt_ROUTE.c from Cédric de Launois
 *	<delaunois@info.ucl.be>
 */
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/route.h>
#include <linux/netfilter/x_tables.h>
#include <net/net_namespace.h>
#include <net/netns/generic.h>
#include <net/route.h>
#include <net/netfilter/ipv4/nf_dup_ipv4.h>
#include <net/netfilter/ipv6/nf_dup_ipv6.h>
#include <linux/netfilter/xt_TEE.h>

struct xt_tee_priv {
	struct list_head	list;
	struct xt_tee_tginfo	*tginfo;
	int			oif;
};

static unsigned int tee_net_id __read_mostly;
static const union nf_inet_addr tee_zero_address;

struct tee_net {
	struct list_head priv_list;
	/* lock protects the priv_list */
	struct mutex lock;
};

static unsigned int
tee_tg4(struct sk_buff *skb, const struct xt_action_param *par)
{
	const struct xt_tee_tginfo *info = par->targinfo;
	int oif = info->priv ? info->priv->oif : 0;

	nf_dup_ipv4(xt_net(par), skb, xt_hooknum(par), &info->gw.in, oif);

	return XT_CONTINUE;
}

#if IS_ENABLED(CONFIG_IP6_NF_IPTABLES)
static unsigned int
tee_tg6(struct sk_buff *skb, const struct xt_action_param *par)
{
	const struct xt_tee_tginfo *info = par->targinfo;
	int oif = info->priv ? info->priv->oif : 0;

	nf_dup_ipv6(xt_net(par), skb, xt_hooknum(par), &info->gw.in6, oif);

	return XT_CONTINUE;
}
#endif

static int tee_netdev_event(struct notifier_block *this, unsigned long event,
			    void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct net *net = dev_net(dev);
	struct tee_net *tn = net_generic(net, tee_net_id);
	struct xt_tee_priv *priv;

	mutex_lock(&tn->lock);
	list_for_each_entry(priv, &tn->priv_list, list) {
		switch (event) {
		case NETDEV_REGISTER:
			if (!strcmp(dev->name, priv->tginfo->oif))
				priv->oif = dev->ifindex;
			break;
		case NETDEV_UNREGISTER:
			if (dev->ifindex == priv->oif)
				priv->oif = -1;
			break;
		case NETDEV_CHANGENAME:
			if (!strcmp(dev->name, priv->tginfo->oif))
				priv->oif = dev->ifindex;
			else if (dev->ifindex == priv->oif)
				priv->oif = -1;
			break;
		}
	}
	mutex_unlock(&tn->lock);

	return NOTIFY_DONE;
}

static int tee_tg_check(const struct xt_tgchk_param *par)
{
	struct tee_net *tn = net_generic(par->net, tee_net_id);
	struct xt_tee_tginfo *info = par->targinfo;
	struct xt_tee_priv *priv;

	/* 0.0.0.0 and :: not allowed */
	if (memcmp(&info->gw, &tee_zero_address,
		   sizeof(tee_zero_address)) == 0)
		return -EINVAL;

	if (info->oif[0]) {
		struct net_device *dev;

		if (info->oif[sizeof(info->oif)-1] != '\0')
			return -EINVAL;

		priv = kzalloc(sizeof(*priv), GFP_KERNEL);
		if (priv == NULL)
			return -ENOMEM;

		priv->tginfo  = info;
		priv->oif     = -1;
		info->priv    = priv;

		dev = dev_get_by_name(par->net, info->oif);
		if (dev) {
			priv->oif = dev->ifindex;
			dev_put(dev);
		}
		mutex_lock(&tn->lock);
		list_add(&priv->list, &tn->priv_list);
		mutex_unlock(&tn->lock);
	} else
		info->priv = NULL;

	static_key_slow_inc(&xt_tee_enabled);
	return 0;
}

static void tee_tg_destroy(const struct xt_tgdtor_param *par)
{
	struct tee_net *tn = net_generic(par->net, tee_net_id);
	struct xt_tee_tginfo *info = par->targinfo;

	if (info->priv) {
		mutex_lock(&tn->lock);
		list_del(&info->priv->list);
		mutex_unlock(&tn->lock);
		kfree(info->priv);
	}
	static_key_slow_dec(&xt_tee_enabled);
}

static struct xt_target tee_tg_reg[] __read_mostly = {
	{
		.name       = "TEE",
		.revision   = 1,
		.family     = NFPROTO_IPV4,
		.target     = tee_tg4,
		.targetsize = sizeof(struct xt_tee_tginfo),
		.usersize   = offsetof(struct xt_tee_tginfo, priv),
		.checkentry = tee_tg_check,
		.destroy    = tee_tg_destroy,
		.me         = THIS_MODULE,
	},
#if IS_ENABLED(CONFIG_IP6_NF_IPTABLES)
	{
		.name       = "TEE",
		.revision   = 1,
		.family     = NFPROTO_IPV6,
		.target     = tee_tg6,
		.targetsize = sizeof(struct xt_tee_tginfo),
		.usersize   = offsetof(struct xt_tee_tginfo, priv),
		.checkentry = tee_tg_check,
		.destroy    = tee_tg_destroy,
		.me         = THIS_MODULE,
	},
#endif
};

static int __net_init tee_net_init(struct net *net)
{
	struct tee_net *tn = net_generic(net, tee_net_id);

	INIT_LIST_HEAD(&tn->priv_list);
	mutex_init(&tn->lock);
	return 0;
}

static struct pernet_operations tee_net_ops = {
	.init = tee_net_init,
	.id   = &tee_net_id,
	.size = sizeof(struct tee_net),
};

static struct notifier_block tee_netdev_notifier = {
	.notifier_call = tee_netdev_event,
};

static int __init tee_tg_init(void)
{
	int ret;

	ret = register_pernet_subsys(&tee_net_ops);
	if (ret < 0)
		return ret;

	ret = xt_register_targets(tee_tg_reg, ARRAY_SIZE(tee_tg_reg));
	if (ret < 0)
		goto cleanup_subsys;

	ret = register_netdevice_notifier(&tee_netdev_notifier);
	if (ret < 0)
		goto unregister_targets;

	return 0;

unregister_targets:
	xt_unregister_targets(tee_tg_reg, ARRAY_SIZE(tee_tg_reg));
cleanup_subsys:
	unregister_pernet_subsys(&tee_net_ops);
	return ret;
}

static void __exit tee_tg_exit(void)
{
	unregister_netdevice_notifier(&tee_netdev_notifier);
	xt_unregister_targets(tee_tg_reg, ARRAY_SIZE(tee_tg_reg));
	unregister_pernet_subsys(&tee_net_ops);
}

module_init(tee_tg_init);
module_exit(tee_tg_exit);
MODULE_AUTHOR("Sebastian Claßen <sebastian.classen@freenet.ag>");
MODULE_AUTHOR("Jan Engelhardt <jengelh@medozas.de>");
MODULE_DESCRIPTION("Xtables: Reroute packet copy");
MODULE_LICENSE("GPL");
MODULE_ALIAS("ipt_TEE");
MODULE_ALIAS("ip6t_TEE");
