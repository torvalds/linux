/*
 * Netlink inteface for IEEE 802.15.4 stack
 *
 * Copyright 2007, 2008 Siemens AG
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Written by:
 * Sergey Lapin <slapin@ossfans.org>
 * Dmitry Eremin-Solenikov <dbaryshkov@gmail.com>
 * Maxim Osipov <maxim.osipov@siemens.com>
 */

#include <linux/kernel.h>
#include <linux/gfp.h>
#include <net/genetlink.h>
#include <linux/nl802154.h>

#include "ieee802154.h"

static unsigned int ieee802154_seq_num;
static DEFINE_SPINLOCK(ieee802154_seq_lock);

struct genl_family nl802154_family = {
	.id		= GENL_ID_GENERATE,
	.hdrsize	= 0,
	.name		= IEEE802154_NL_NAME,
	.version	= 1,
	.maxattr	= IEEE802154_ATTR_MAX,
};

/* Requests to userspace */
struct sk_buff *ieee802154_nl_create(int flags, u8 req)
{
	void *hdr;
	struct sk_buff *msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_ATOMIC);
	unsigned long f;

	if (!msg)
		return NULL;

	spin_lock_irqsave(&ieee802154_seq_lock, f);
	hdr = genlmsg_put(msg, 0, ieee802154_seq_num++,
			&nl802154_family, flags, req);
	spin_unlock_irqrestore(&ieee802154_seq_lock, f);
	if (!hdr) {
		nlmsg_free(msg);
		return NULL;
	}

	return msg;
}

int ieee802154_nl_mcast(struct sk_buff *msg, unsigned int group)
{
	struct nlmsghdr *nlh = nlmsg_hdr(msg);
	void *hdr = genlmsg_data(nlmsg_data(nlh));

	if (genlmsg_end(msg, hdr) < 0)
		goto out;

	return genlmsg_multicast(msg, 0, group, GFP_ATOMIC);
out:
	nlmsg_free(msg);
	return -ENOBUFS;
}

struct sk_buff *ieee802154_nl_new_reply(struct genl_info *info,
		int flags, u8 req)
{
	void *hdr;
	struct sk_buff *msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_ATOMIC);

	if (!msg)
		return NULL;

	hdr = genlmsg_put_reply(msg, info,
			&nl802154_family, flags, req);
	if (!hdr) {
		nlmsg_free(msg);
		return NULL;
	}

	return msg;
}

int ieee802154_nl_reply(struct sk_buff *msg, struct genl_info *info)
{
	struct nlmsghdr *nlh = nlmsg_hdr(msg);
	void *hdr = genlmsg_data(nlmsg_data(nlh));

	if (genlmsg_end(msg, hdr) < 0)
		goto out;

	return genlmsg_reply(msg, info);
out:
	nlmsg_free(msg);
	return -ENOBUFS;
}

int __init ieee802154_nl_init(void)
{
	int rc;

	rc = genl_register_family(&nl802154_family);
	if (rc)
		goto fail;

	rc = nl802154_mac_register();
	if (rc)
		goto fail;

	rc = nl802154_phy_register();
	if (rc)
		goto fail;

	return 0;

fail:
	genl_unregister_family(&nl802154_family);
	return rc;
}

void __exit ieee802154_nl_exit(void)
{
	genl_unregister_family(&nl802154_family);
}

