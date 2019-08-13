/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2007, 2008, 2009 Siemens AG
 */
#ifndef IEEE_802154_LOCAL_H
#define IEEE_802154_LOCAL_H

int __init ieee802154_nl_init(void);
void ieee802154_nl_exit(void);

#define IEEE802154_OP(_cmd, _func)			\
	{						\
		.cmd	= _cmd,				\
		.doit	= _func,			\
		.dumpit	= NULL,				\
		.flags	= GENL_ADMIN_PERM,		\
	}

#define IEEE802154_DUMP(_cmd, _func, _dump)		\
	{						\
		.cmd	= _cmd,				\
		.doit	= _func,			\
		.dumpit	= _dump,			\
	}

struct genl_info;

struct sk_buff *ieee802154_nl_create(int flags, u8 req);
int ieee802154_nl_mcast(struct sk_buff *msg, unsigned int group);
struct sk_buff *ieee802154_nl_new_reply(struct genl_info *info,
					int flags, u8 req);
int ieee802154_nl_reply(struct sk_buff *msg, struct genl_info *info);

extern struct genl_family nl802154_family;

/* genetlink ops/groups */
int ieee802154_list_phy(struct sk_buff *skb, struct genl_info *info);
int ieee802154_dump_phy(struct sk_buff *skb, struct netlink_callback *cb);
int ieee802154_add_iface(struct sk_buff *skb, struct genl_info *info);
int ieee802154_del_iface(struct sk_buff *skb, struct genl_info *info);

enum ieee802154_mcgrp_ids {
	IEEE802154_COORD_MCGRP,
	IEEE802154_BEACON_MCGRP,
};

int ieee802154_associate_req(struct sk_buff *skb, struct genl_info *info);
int ieee802154_associate_resp(struct sk_buff *skb, struct genl_info *info);
int ieee802154_disassociate_req(struct sk_buff *skb, struct genl_info *info);
int ieee802154_scan_req(struct sk_buff *skb, struct genl_info *info);
int ieee802154_start_req(struct sk_buff *skb, struct genl_info *info);
int ieee802154_list_iface(struct sk_buff *skb, struct genl_info *info);
int ieee802154_dump_iface(struct sk_buff *skb, struct netlink_callback *cb);
int ieee802154_set_macparams(struct sk_buff *skb, struct genl_info *info);

int ieee802154_llsec_getparams(struct sk_buff *skb, struct genl_info *info);
int ieee802154_llsec_setparams(struct sk_buff *skb, struct genl_info *info);
int ieee802154_llsec_add_key(struct sk_buff *skb, struct genl_info *info);
int ieee802154_llsec_del_key(struct sk_buff *skb, struct genl_info *info);
int ieee802154_llsec_dump_keys(struct sk_buff *skb,
			       struct netlink_callback *cb);
int ieee802154_llsec_add_dev(struct sk_buff *skb, struct genl_info *info);
int ieee802154_llsec_del_dev(struct sk_buff *skb, struct genl_info *info);
int ieee802154_llsec_dump_devs(struct sk_buff *skb,
			       struct netlink_callback *cb);
int ieee802154_llsec_add_devkey(struct sk_buff *skb, struct genl_info *info);
int ieee802154_llsec_del_devkey(struct sk_buff *skb, struct genl_info *info);
int ieee802154_llsec_dump_devkeys(struct sk_buff *skb,
				  struct netlink_callback *cb);
int ieee802154_llsec_add_seclevel(struct sk_buff *skb, struct genl_info *info);
int ieee802154_llsec_del_seclevel(struct sk_buff *skb, struct genl_info *info);
int ieee802154_llsec_dump_seclevels(struct sk_buff *skb,
				    struct netlink_callback *cb);

#endif
