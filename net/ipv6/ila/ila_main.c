// SPDX-License-Identifier: GPL-2.0
#include <net/genetlink.h>
#include <net/netns/generic.h>
#include <uapi/linux/genetlink.h>
#include "ila.h"

static const struct nla_policy ila_nl_policy[ILA_ATTR_MAX + 1] = {
	[ILA_ATTR_LOCATOR] = { .type = NLA_U64, },
	[ILA_ATTR_LOCATOR_MATCH] = { .type = NLA_U64, },
	[ILA_ATTR_IFINDEX] = { .type = NLA_U32, },
	[ILA_ATTR_CSUM_MODE] = { .type = NLA_U8, },
	[ILA_ATTR_IDENT_TYPE] = { .type = NLA_U8, },
};

static const struct genl_ops ila_nl_ops[] = {
	{
		.cmd = ILA_CMD_ADD,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = ila_xlat_nl_cmd_add_mapping,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = ILA_CMD_DEL,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = ila_xlat_nl_cmd_del_mapping,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = ILA_CMD_FLUSH,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = ila_xlat_nl_cmd_flush,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = ILA_CMD_GET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = ila_xlat_nl_cmd_get_mapping,
		.start = ila_xlat_nl_dump_start,
		.dumpit = ila_xlat_nl_dump,
		.done = ila_xlat_nl_dump_done,
	},
};

unsigned int ila_net_id;

struct genl_family ila_nl_family __ro_after_init = {
	.hdrsize	= 0,
	.name		= ILA_GENL_NAME,
	.version	= ILA_GENL_VERSION,
	.maxattr	= ILA_ATTR_MAX,
	.policy = ila_nl_policy,
	.netnsok	= true,
	.parallel_ops	= true,
	.module		= THIS_MODULE,
	.ops		= ila_nl_ops,
	.n_ops		= ARRAY_SIZE(ila_nl_ops),
	.resv_start_op	= ILA_CMD_FLUSH + 1,
};

static __net_init int ila_init_net(struct net *net)
{
	int err;

	err = ila_xlat_init_net(net);
	if (err)
		goto ila_xlat_init_fail;

	return 0;

ila_xlat_init_fail:
	return err;
}

static __net_exit void ila_pre_exit_net(struct net *net)
{
	ila_xlat_pre_exit_net(net);
}

static __net_exit void ila_exit_net(struct net *net)
{
	ila_xlat_exit_net(net);
}

static struct pernet_operations ila_net_ops = {
	.init = ila_init_net,
	.pre_exit = ila_pre_exit_net,
	.exit = ila_exit_net,
	.id   = &ila_net_id,
	.size = sizeof(struct ila_net),
};

static int __init ila_init(void)
{
	int ret;

	ret = register_pernet_device(&ila_net_ops);
	if (ret)
		goto register_device_fail;

	ret = genl_register_family(&ila_nl_family);
	if (ret)
		goto register_family_fail;

	ret = ila_lwt_init();
	if (ret)
		goto fail_lwt;

	return 0;

fail_lwt:
	genl_unregister_family(&ila_nl_family);
register_family_fail:
	unregister_pernet_device(&ila_net_ops);
register_device_fail:
	return ret;
}

static void __exit ila_fini(void)
{
	ila_lwt_fini();
	genl_unregister_family(&ila_nl_family);
	unregister_pernet_device(&ila_net_ops);
}

module_init(ila_init);
module_exit(ila_fini);
MODULE_AUTHOR("Tom Herbert <tom@herbertland.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("IPv6: Identifier Locator Addressing (ILA)");
