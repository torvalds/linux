/*
 *	"TEE" target extension for Xtables
 *	Copyright © Sebastian Claßen, 2007
 *	Jan Engelhardt, 2007-2010
 *
 *	based on ipt_ROUTE.c from Cédric de Launois
 *	<delaunois@info.ucl.be>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	version 2 or later, as published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/route.h>
#include <linux/netfilter/x_tables.h>
#include <net/route.h>
#include <net/netfilter/ipv4/nf_dup_ipv4.h>
#include <net/netfilter/ipv6/nf_dup_ipv6.h>
#include <linux/netfilter/xt_TEE.h>

struct xt_tee_priv {
	struct notifier_block	notifier;
	struct xt_tee_tginfo	*tginfo;
	int			oif;
};

static const union nf_inet_addr tee_zero_address;

static unsigned int
tee_tg4(struct sk_buff *skb, const struct xt_action_param *par)
{
	const struct xt_tee_tginfo *info = par->targinfo;
	int oif = info->priv ? info->priv->oif : 0;

	nf_dup_ipv4(par->net, skb, par->hooknum, &info->gw.in, oif);

	return XT_CONTINUE;
}

#if IS_ENABLED(CONFIG_IPV6)
static unsigned int
tee_tg6(struct sk_buff *skb, const struct xt_action_param *par)
{
	const struct xt_tee_tginfo *info = par->targinfo;
	int oif = info->priv ? info->priv->oif : 0;

	nf_dup_ipv6(par->net, skb, par->hooknum, &info->gw.in6, oif);

	return XT_CONTINUE;
}
#endif

static int tee_netdev_event(struct notifier_block *this, unsigned long event,
			    void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct xt_tee_priv *priv;

	priv = container_of(this, struct xt_tee_priv, notifier);
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

	return NOTIFY_DONE;
}

static int tee_tg_check(const struct xt_tgchk_param *par)
{
	struct xt_tee_tginfo *info = par->targinfo;
	struct xt_tee_priv *priv;

	/* 0.0.0.0 and :: not allowed */
	if (memcmp(&info->gw, &tee_zero_address,
		   sizeof(tee_zero_address)) == 0)
		return -EINVAL;

	if (info->oif[0]) {
		int ret;

		if (info->oif[sizeof(info->oif)-1] != '\0')
			return -EINVAL;

		priv = kzalloc(sizeof(*priv), GFP_KERNEL);
		if (priv == NULL)
			return -ENOMEM;

		priv->tginfo  = info;
		priv->oif     = -1;
		priv->notifier.notifier_call = tee_netdev_event;
		info->priv    = priv;

		ret = register_netdevice_notifier(&priv->notifier);
		if (ret) {
			kfree(priv);
			return ret;
		}
	} else
		info->priv = NULL;

	static_key_slow_inc(&xt_tee_enabled);
	return 0;
}

static void tee_tg_destroy(const struct xt_tgdtor_param *par)
{
	struct xt_tee_tginfo *info = par->targinfo;

	if (info->priv) {
		unregister_netdevice_notifier(&info->priv->notifier);
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
		.checkentry = tee_tg_check,
		.destroy    = tee_tg_destroy,
		.me         = THIS_MODULE,
	},
#if IS_ENABLED(CONFIG_IPV6)
	{
		.name       = "TEE",
		.revision   = 1,
		.family     = NFPROTO_IPV6,
		.target     = tee_tg6,
		.targetsize = sizeof(struct xt_tee_tginfo),
		.checkentry = tee_tg_check,
		.destroy    = tee_tg_destroy,
		.me         = THIS_MODULE,
	},
#endif
};

static int __init tee_tg_init(void)
{
	return xt_register_targets(tee_tg_reg, ARRAY_SIZE(tee_tg_reg));
}

static void __exit tee_tg_exit(void)
{
	xt_unregister_targets(tee_tg_reg, ARRAY_SIZE(tee_tg_reg));
}

module_init(tee_tg_init);
module_exit(tee_tg_exit);
MODULE_AUTHOR("Sebastian Claßen <sebastian.classen@freenet.ag>");
MODULE_AUTHOR("Jan Engelhardt <jengelh@medozas.de>");
MODULE_DESCRIPTION("Xtables: Reroute packet copy");
MODULE_LICENSE("GPL");
MODULE_ALIAS("ipt_TEE");
MODULE_ALIAS("ip6t_TEE");
