/*
 * Copyright (c) 2005-2006 Intel Corporation.  All rights reserved.
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
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
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

#ifndef RDMA_USER_CM_H
#define RDMA_USER_CM_H

#include <linux/types.h>
#include <linux/socket.h>
#include <linux/in6.h>
#include <rdma/ib_user_verbs.h>
#include <rdma/ib_user_sa.h>

#define RDMA_USER_CM_ABI_VERSION	4

#define RDMA_MAX_PRIVATE_DATA		256

enum {
	RDMA_USER_CM_CMD_CREATE_ID,
	RDMA_USER_CM_CMD_DESTROY_ID,
	RDMA_USER_CM_CMD_BIND_IP,
	RDMA_USER_CM_CMD_RESOLVE_IP,
	RDMA_USER_CM_CMD_RESOLVE_ROUTE,
	RDMA_USER_CM_CMD_QUERY_ROUTE,
	RDMA_USER_CM_CMD_CONNECT,
	RDMA_USER_CM_CMD_LISTEN,
	RDMA_USER_CM_CMD_ACCEPT,
	RDMA_USER_CM_CMD_REJECT,
	RDMA_USER_CM_CMD_DISCONNECT,
	RDMA_USER_CM_CMD_INIT_QP_ATTR,
	RDMA_USER_CM_CMD_GET_EVENT,
	RDMA_USER_CM_CMD_GET_OPTION,
	RDMA_USER_CM_CMD_SET_OPTION,
	RDMA_USER_CM_CMD_NOTIFY,
	RDMA_USER_CM_CMD_JOIN_IP_MCAST,
	RDMA_USER_CM_CMD_LEAVE_MCAST,
	RDMA_USER_CM_CMD_MIGRATE_ID,
	RDMA_USER_CM_CMD_QUERY,
	RDMA_USER_CM_CMD_BIND,
	RDMA_USER_CM_CMD_RESOLVE_ADDR,
	RDMA_USER_CM_CMD_JOIN_MCAST
};

/*
 * command ABI structures.
 */
struct rdma_ucm_cmd_hdr {
	__u32 cmd;
	__u16 in;
	__u16 out;
};

struct rdma_ucm_create_id {
	__u64 uid;
	__u64 response;
	__u16 ps;
	__u8  qp_type;
	__u8  reserved[5];
};

struct rdma_ucm_create_id_resp {
	__u32 id;
};

struct rdma_ucm_destroy_id {
	__u64 response;
	__u32 id;
	__u32 reserved;
};

struct rdma_ucm_destroy_id_resp {
	__u32 events_reported;
};

struct rdma_ucm_bind_ip {
	__u64 response;
	struct sockaddr_in6 addr;
	__u32 id;
};

struct rdma_ucm_bind {
	__u32 id;
	__u16 addr_size;
	__u16 reserved;
	struct __kernel_sockaddr_storage addr;
};

struct rdma_ucm_resolve_ip {
	struct sockaddr_in6 src_addr;
	struct sockaddr_in6 dst_addr;
	__u32 id;
	__u32 timeout_ms;
};

struct rdma_ucm_resolve_addr {
	__u32 id;
	__u32 timeout_ms;
	__u16 src_size;
	__u16 dst_size;
	__u32 reserved;
	struct __kernel_sockaddr_storage src_addr;
	struct __kernel_sockaddr_storage dst_addr;
};

struct rdma_ucm_resolve_route {
	__u32 id;
	__u32 timeout_ms;
};

enum {
	RDMA_USER_CM_QUERY_ADDR,
	RDMA_USER_CM_QUERY_PATH,
	RDMA_USER_CM_QUERY_GID
};

struct rdma_ucm_query {
	__u64 response;
	__u32 id;
	__u32 option;
};

struct rdma_ucm_query_route_resp {
	__u64 node_guid;
	struct ib_user_path_rec ib_route[2];
	struct sockaddr_in6 src_addr;
	struct sockaddr_in6 dst_addr;
	__u32 num_paths;
	__u8 port_num;
	__u8 reserved[3];
};

struct rdma_ucm_query_addr_resp {
	__u64 node_guid;
	__u8  port_num;
	__u8  reserved;
	__u16 pkey;
	__u16 src_size;
	__u16 dst_size;
	struct __kernel_sockaddr_storage src_addr;
	struct __kernel_sockaddr_storage dst_addr;
};

struct rdma_ucm_query_path_resp {
	__u32 num_paths;
	__u32 reserved;
	struct ib_path_rec_data path_data[0];
};

struct rdma_ucm_conn_param {
	__u32 qp_num;
	__u32 qkey;
	__u8  private_data[RDMA_MAX_PRIVATE_DATA];
	__u8  private_data_len;
	__u8  srq;
	__u8  responder_resources;
	__u8  initiator_depth;
	__u8  flow_control;
	__u8  retry_count;
	__u8  rnr_retry_count;
	__u8  valid;
};

struct rdma_ucm_ud_param {
	__u32 qp_num;
	__u32 qkey;
	struct ib_uverbs_ah_attr ah_attr;
	__u8  private_data[RDMA_MAX_PRIVATE_DATA];
	__u8  private_data_len;
	__u8  reserved[7];
};

struct rdma_ucm_connect {
	struct rdma_ucm_conn_param conn_param;
	__u32 id;
	__u32 reserved;
};

struct rdma_ucm_listen {
	__u32 id;
	__u32 backlog;
};

struct rdma_ucm_accept {
	__u64 uid;
	struct rdma_ucm_conn_param conn_param;
	__u32 id;
	__u32 reserved;
};

struct rdma_ucm_reject {
	__u32 id;
	__u8  private_data_len;
	__u8  reserved[3];
	__u8  private_data[RDMA_MAX_PRIVATE_DATA];
};

struct rdma_ucm_disconnect {
	__u32 id;
};

struct rdma_ucm_init_qp_attr {
	__u64 response;
	__u32 id;
	__u32 qp_state;
};

struct rdma_ucm_notify {
	__u32 id;
	__u32 event;
};

struct rdma_ucm_join_ip_mcast {
	__u64 response;		/* rdma_ucm_create_id_resp */
	__u64 uid;
	struct sockaddr_in6 addr;
	__u32 id;
};

/* Multicast join flags */
enum {
	RDMA_MC_JOIN_FLAG_FULLMEMBER,
	RDMA_MC_JOIN_FLAG_SENDONLY_FULLMEMBER,
	RDMA_MC_JOIN_FLAG_RESERVED,
};

struct rdma_ucm_join_mcast {
	__u64 response;		/* rdma_ucma_create_id_resp */
	__u64 uid;
	__u32 id;
	__u16 addr_size;
	__u16 join_flags;
	struct __kernel_sockaddr_storage addr;
};

struct rdma_ucm_get_event {
	__u64 response;
};

struct rdma_ucm_event_resp {
	__u64 uid;
	__u32 id;
	__u32 event;
	__u32 status;
	union {
		struct rdma_ucm_conn_param conn;
		struct rdma_ucm_ud_param   ud;
	} param;
};

/* Option levels */
enum {
	RDMA_OPTION_ID		= 0,
	RDMA_OPTION_IB		= 1
};

/* Option details */
enum {
	RDMA_OPTION_ID_TOS	 = 0,
	RDMA_OPTION_ID_REUSEADDR = 1,
	RDMA_OPTION_ID_AFONLY	 = 2,
	RDMA_OPTION_IB_PATH	 = 1
};

struct rdma_ucm_set_option {
	__u64 optval;
	__u32 id;
	__u32 level;
	__u32 optname;
	__u32 optlen;
};

struct rdma_ucm_migrate_id {
	__u64 response;
	__u32 id;
	__u32 fd;
};

struct rdma_ucm_migrate_resp {
	__u32 events_reported;
};

#endif /* RDMA_USER_CM_H */
