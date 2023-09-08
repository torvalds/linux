// SPDX-License-Identifier: GPL-2.0
/*
 * Netlink routines for CIFS
 *
 * Copyright (c) 2020 Samuel Cabrero <scabrero@suse.de>
 */

#include <net/genetlink.h>
#include <uapi/linux/cifs/cifs_netlink.h>

#include "netlink.h"
#include "cifsglob.h"
#include "cifs_debug.h"
#include "cifs_swn.h"

static const struct nla_policy cifs_genl_policy[CIFS_GENL_ATTR_MAX + 1] = {
	[CIFS_GENL_ATTR_SWN_REGISTRATION_ID]	= { .type = NLA_U32 },
	[CIFS_GENL_ATTR_SWN_NET_NAME]		= { .type = NLA_STRING },
	[CIFS_GENL_ATTR_SWN_SHARE_NAME]		= { .type = NLA_STRING },
	[CIFS_GENL_ATTR_SWN_IP]			= { .len = sizeof(struct sockaddr_storage) },
	[CIFS_GENL_ATTR_SWN_NET_NAME_NOTIFY]	= { .type = NLA_FLAG },
	[CIFS_GENL_ATTR_SWN_SHARE_NAME_NOTIFY]	= { .type = NLA_FLAG },
	[CIFS_GENL_ATTR_SWN_IP_NOTIFY]		= { .type = NLA_FLAG },
	[CIFS_GENL_ATTR_SWN_KRB_AUTH]		= { .type = NLA_FLAG },
	[CIFS_GENL_ATTR_SWN_USER_NAME]		= { .type = NLA_STRING },
	[CIFS_GENL_ATTR_SWN_PASSWORD]		= { .type = NLA_STRING },
	[CIFS_GENL_ATTR_SWN_DOMAIN_NAME]	= { .type = NLA_STRING },
	[CIFS_GENL_ATTR_SWN_NOTIFICATION_TYPE]	= { .type = NLA_U32 },
	[CIFS_GENL_ATTR_SWN_RESOURCE_STATE]	= { .type = NLA_U32 },
	[CIFS_GENL_ATTR_SWN_RESOURCE_NAME]	= { .type = NLA_STRING},
};

static const struct genl_ops cifs_genl_ops[] = {
	{
		.cmd = CIFS_GENL_CMD_SWN_NOTIFY,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = cifs_swn_notify,
	},
};

static const struct genl_multicast_group cifs_genl_mcgrps[] = {
	[CIFS_GENL_MCGRP_SWN] = { .name = CIFS_GENL_MCGRP_SWN_NAME },
};

struct genl_family cifs_genl_family = {
	.name		= CIFS_GENL_NAME,
	.version	= CIFS_GENL_VERSION,
	.hdrsize	= 0,
	.maxattr	= CIFS_GENL_ATTR_MAX,
	.module		= THIS_MODULE,
	.policy		= cifs_genl_policy,
	.ops		= cifs_genl_ops,
	.n_ops		= ARRAY_SIZE(cifs_genl_ops),
	.resv_start_op	= CIFS_GENL_CMD_SWN_NOTIFY + 1,
	.mcgrps		= cifs_genl_mcgrps,
	.n_mcgrps	= ARRAY_SIZE(cifs_genl_mcgrps),
};

/**
 * cifs_genl_init - Register generic netlink family
 *
 * Return zero if initialized successfully, otherwise non-zero.
 */
int cifs_genl_init(void)
{
	int ret;

	ret = genl_register_family(&cifs_genl_family);
	if (ret < 0) {
		cifs_dbg(VFS, "%s: failed to register netlink family\n",
				__func__);
		return ret;
	}

	return 0;
}

/**
 * cifs_genl_exit - Unregister generic netlink family
 */
void cifs_genl_exit(void)
{
	int ret;

	ret = genl_unregister_family(&cifs_genl_family);
	if (ret < 0) {
		cifs_dbg(VFS, "%s: failed to unregister netlink family\n",
				__func__);
	}
}
