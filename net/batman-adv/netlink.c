/* Copyright (C) 2016 B.A.T.M.A.N. contributors:
 *
 * Matthias Schiffer
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "netlink.h"
#include "main.h"

#include <linux/genetlink.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <net/genetlink.h>
#include <uapi/linux/batman_adv.h>

static struct genl_family batadv_netlink_family = {
	.id = GENL_ID_GENERATE,
	.hdrsize = 0,
	.name = BATADV_NL_NAME,
	.version = 1,
	.maxattr = BATADV_ATTR_MAX,
};

static struct genl_ops batadv_netlink_ops[] = {
};

/**
 * batadv_netlink_register - register batadv genl netlink family
 */
void __init batadv_netlink_register(void)
{
	int ret;

	ret = genl_register_family_with_ops(&batadv_netlink_family,
					    batadv_netlink_ops);
	if (ret)
		pr_warn("unable to register netlink family");
}

/**
 * batadv_netlink_unregister - unregister batadv genl netlink family
 */
void batadv_netlink_unregister(void)
{
	genl_unregister_family(&batadv_netlink_family);
}
