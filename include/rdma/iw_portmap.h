/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/*
 * Copyright (c) 2014 Intel Corporation. All rights reserved.
 * Copyright (c) 2014 Chelsio, Inc. All rights reserved.
 */

#ifndef _IW_PORTMAP_H
#define _IW_PORTMAP_H

#include <linux/socket.h>
#include <linux/netlink.h>

#define IWPM_ULIBNAME_SIZE	32
#define IWPM_DEVNAME_SIZE	32
#define IWPM_IFNAME_SIZE	16
#define IWPM_IPADDR_SIZE	16

enum {
	IWPM_INVALID_NLMSG_ERR = 10,
	IWPM_CREATE_MAPPING_ERR,
	IWPM_DUPLICATE_MAPPING_ERR,
	IWPM_UNKNOWN_MAPPING_ERR,
	IWPM_CLIENT_DEV_INFO_ERR,
	IWPM_USER_LIB_INFO_ERR,
	IWPM_REMOTE_QUERY_REJECT
};

struct iwpm_dev_data {
	char dev_name[IWPM_DEVNAME_SIZE];
	char if_name[IWPM_IFNAME_SIZE];
};

struct iwpm_sa_data {
	struct sockaddr_storage loc_addr;
	struct sockaddr_storage mapped_loc_addr;
	struct sockaddr_storage rem_addr;
	struct sockaddr_storage mapped_rem_addr;
	u32 flags;
};

int iwpm_init(u8);
int iwpm_exit(u8);
int iwpm_valid_pid(void);
int iwpm_register_pid(struct iwpm_dev_data *pm_msg, u8 nl_client);
int iwpm_add_mapping(struct iwpm_sa_data *pm_msg, u8 nl_client);
int iwpm_add_and_query_mapping(struct iwpm_sa_data *pm_msg, u8 nl_client);
int iwpm_remove_mapping(struct sockaddr_storage *local_addr, u8 nl_client);
int iwpm_register_pid_cb(struct sk_buff *, struct netlink_callback *);
int iwpm_add_mapping_cb(struct sk_buff *, struct netlink_callback *);
int iwpm_add_and_query_mapping_cb(struct sk_buff *, struct netlink_callback *);
int iwpm_remote_info_cb(struct sk_buff *, struct netlink_callback *);
int iwpm_mapping_error_cb(struct sk_buff *, struct netlink_callback *);
int iwpm_mapping_info_cb(struct sk_buff *, struct netlink_callback *);
int iwpm_ack_mapping_info_cb(struct sk_buff *, struct netlink_callback *);
int iwpm_get_remote_info(struct sockaddr_storage *mapped_loc_addr,
			struct sockaddr_storage *mapped_rem_addr,
			struct sockaddr_storage *remote_addr, u8 nl_client);
int iwpm_create_mapinfo(struct sockaddr_storage *local_addr,
			struct sockaddr_storage *mapped_addr, u8 nl_client,
			u32 map_flags);
int iwpm_remove_mapinfo(struct sockaddr_storage *local_addr,
			struct sockaddr_storage *mapped_addr);

int iwpm_hello_cb(struct sk_buff *skb, struct netlink_callback *cb);
#endif /* _IW_PORTMAP_H */
