/*
 * Copyright (c) 2014 Intel Corporation. All rights reserved.
 * Copyright (c) 2014 Chelsio, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer in the documentation and/or other materials
 *	  provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#ifndef _IW_PORTMAP_H
#define _IW_PORTMAP_H

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

/**
 * iwpm_init - Allocate resources for the iwarp port mapper
 *
 * Should be called when network interface goes up.
 */
int iwpm_init(u8);

/**
 * iwpm_exit - Deallocate resources for the iwarp port mapper
 *
 * Should be called when network interface goes down.
 */
int iwpm_exit(u8);

/**
 * iwpm_valid_pid - Check if the userspace iwarp port mapper pid is valid
 *
 * Returns true if the pid is greater than zero, otherwise returns false
 */
int iwpm_valid_pid(void);

/**
 * iwpm_register_pid - Send a netlink query to userspace
 *                     to get the iwarp port mapper pid
 * @pm_msg: Contains driver info to send to the userspace port mapper
 * @nl_client: The index of the netlink client
 */
int iwpm_register_pid(struct iwpm_dev_data *pm_msg, u8 nl_client);

/**
 * iwpm_add_mapping - Send a netlink add mapping request to
 *                    the userspace port mapper
 * @pm_msg: Contains the local ip/tcp address info to send
 * @nl_client: The index of the netlink client
 *
 * If the request is successful, the pm_msg stores
 * the port mapper response (mapped address info)
 */
int iwpm_add_mapping(struct iwpm_sa_data *pm_msg, u8 nl_client);

/**
 * iwpm_add_and_query_mapping - Send a netlink add and query mapping request
 *				 to the userspace port mapper
 * @pm_msg: Contains the local and remote ip/tcp address info to send
 * @nl_client: The index of the netlink client
 *
 * If the request is successful, the pm_msg stores the
 * port mapper response (mapped local and remote address info)
 */
int iwpm_add_and_query_mapping(struct iwpm_sa_data *pm_msg, u8 nl_client);

/**
 * iwpm_remove_mapping - Send a netlink remove mapping request
 *                       to the userspace port mapper
 *
 * @local_addr: Local ip/tcp address to remove
 * @nl_client: The index of the netlink client
 */
int iwpm_remove_mapping(struct sockaddr_storage *local_addr, u8 nl_client);

/**
 * iwpm_register_pid_cb - Process the port mapper response to
 *                        iwpm_register_pid query
 * @skb:
 * @cb: Contains the received message (payload and netlink header)
 *
 * If successful, the function receives the userspace port mapper pid
 * which is used in future communication with the port mapper
 */
int iwpm_register_pid_cb(struct sk_buff *, struct netlink_callback *);

/**
 * iwpm_add_mapping_cb - Process the port mapper response to
 *                       iwpm_add_mapping request
 * @skb:
 * @cb: Contains the received message (payload and netlink header)
 */
int iwpm_add_mapping_cb(struct sk_buff *, struct netlink_callback *);

/**
 * iwpm_add_and_query_mapping_cb - Process the port mapper response to
 *                                 iwpm_add_and_query_mapping request
 * @skb:
 * @cb: Contains the received message (payload and netlink header)
 */
int iwpm_add_and_query_mapping_cb(struct sk_buff *, struct netlink_callback *);

/**
 * iwpm_remote_info_cb - Process remote connecting peer address info, which
 *                       the port mapper has received from the connecting peer
 *
 * @cb: Contains the received message (payload and netlink header)
 *
 * Stores the IPv4/IPv6 address info in a hash table
 */
int iwpm_remote_info_cb(struct sk_buff *, struct netlink_callback *);

/**
 * iwpm_mapping_error_cb - Process port mapper notification for error
 *
 * @skb:
 * @cb: Contains the received message (payload and netlink header)
 */
int iwpm_mapping_error_cb(struct sk_buff *, struct netlink_callback *);

/**
 * iwpm_mapping_info_cb - Process a notification that the userspace
 *                        port mapper daemon is started
 * @skb:
 * @cb: Contains the received message (payload and netlink header)
 *
 * Using the received port mapper pid, send all the local mapping
 * info records to the userspace port mapper
 */
int iwpm_mapping_info_cb(struct sk_buff *, struct netlink_callback *);

/**
 * iwpm_ack_mapping_info_cb - Process the port mapper ack for
 *                            the provided local mapping info records
 * @skb:
 * @cb: Contains the received message (payload and netlink header)
 */
int iwpm_ack_mapping_info_cb(struct sk_buff *, struct netlink_callback *);

/**
 * iwpm_get_remote_info - Get the remote connecting peer address info
 *
 * @mapped_loc_addr: Mapped local address of the listening peer
 * @mapped_rem_addr: Mapped remote address of the connecting peer
 * @remote_addr: To store the remote address of the connecting peer
 * @nl_client: The index of the netlink client
 *
 * The remote address info is retrieved and provided to the client in
 * the remote_addr. After that it is removed from the hash table
 */
int iwpm_get_remote_info(struct sockaddr_storage *mapped_loc_addr,
			struct sockaddr_storage *mapped_rem_addr,
			struct sockaddr_storage *remote_addr, u8 nl_client);

/**
 * iwpm_create_mapinfo - Store local and mapped IPv4/IPv6 address
 *                       info in a hash table
 * @local_addr: Local ip/tcp address
 * @mapped_addr: Mapped local ip/tcp address
 * @nl_client: The index of the netlink client
 * @map_flags: IWPM mapping flags
 */
int iwpm_create_mapinfo(struct sockaddr_storage *local_addr,
			struct sockaddr_storage *mapped_addr, u8 nl_client,
			u32 map_flags);

/**
 * iwpm_remove_mapinfo - Remove local and mapped IPv4/IPv6 address
 *                       info from the hash table
 * @local_addr: Local ip/tcp address
 * @mapped_addr: Mapped local ip/tcp address
 *
 * Returns err code if mapping info is not found in the hash table,
 * otherwise returns 0
 */
int iwpm_remove_mapinfo(struct sockaddr_storage *local_addr,
			struct sockaddr_storage *mapped_addr);

/**
 * iwpm_hello_cb - Process a hello message from iwpmd
 *
 * @skb:
 * @cb: Contains the received message (payload and netlink header)
 *
 * Using the received port mapper pid, send the kernel's abi_version
 * after adjusting it to support the iwpmd version.
 */
int iwpm_hello_cb(struct sk_buff *skb, struct netlink_callback *cb);
#endif /* _IW_PORTMAP_H */
