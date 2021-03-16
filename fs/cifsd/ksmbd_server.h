/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *   Copyright (C) 2018 Samsung Electronics Co., Ltd.
 *
 *   linux-ksmbd-devel@lists.sourceforge.net
 */

#ifndef _LINUX_KSMBD_SERVER_H
#define _LINUX_KSMBD_SERVER_H

#include <linux/types.h>

#define KSMBD_GENL_NAME		"SMBD_GENL"
#define KSMBD_GENL_VERSION		0x01

#ifndef ____ksmbd_align
#define ____ksmbd_align		__aligned(4)
#endif

#define KSMBD_REQ_MAX_ACCOUNT_NAME_SZ	48
#define KSMBD_REQ_MAX_HASH_SZ		18
#define KSMBD_REQ_MAX_SHARE_NAME	64

struct ksmbd_heartbeat {
	__u32	handle;
};

/*
 * Global config flags.
 */
#define KSMBD_GLOBAL_FLAG_INVALID		(0)
#define KSMBD_GLOBAL_FLAG_SMB2_LEASES		(1 << 0)
#define KSMBD_GLOBAL_FLAG_CACHE_TBUF		(1 << 1)
#define KSMBD_GLOBAL_FLAG_CACHE_RBUF		(1 << 2)
#define KSMBD_GLOBAL_FLAG_SMB2_ENCRYPTION	(1 << 3)
#define KSMBD_GLOBAL_FLAG_DURABLE_HANDLE	(1 << 4)

struct ksmbd_startup_request {
	__u32	flags;
	__s32	signing;
	__s8	min_prot[16];
	__s8	max_prot[16];
	__s8	netbios_name[16];
	__s8	work_group[64];
	__s8	server_string[64];
	__u16	tcp_port;
	__u16	ipc_timeout;
	__u32	deadtime;
	__u32	file_max;
	__u32	smb2_max_write;
	__u32	smb2_max_read;
	__u32	smb2_max_trans;
	__u32	share_fake_fscaps;
	__u32	sub_auth[3];
	__u32	ifc_list_sz;
	__s8	____payload[0];
} ____ksmbd_align;

#define KSMBD_STARTUP_CONFIG_INTERFACES(s)	((s)->____payload)

struct ksmbd_shutdown_request {
	__s32	reserved;
} ____ksmbd_align;

struct ksmbd_login_request {
	__u32	handle;
	__s8	account[KSMBD_REQ_MAX_ACCOUNT_NAME_SZ];
} ____ksmbd_align;

struct ksmbd_login_response {
	__u32	handle;
	__u32	gid;
	__u32	uid;
	__s8	account[KSMBD_REQ_MAX_ACCOUNT_NAME_SZ];
	__u16	status;
	__u16	hash_sz;
	__s8	hash[KSMBD_REQ_MAX_HASH_SZ];
} ____ksmbd_align;

struct ksmbd_share_config_request {
	__u32	handle;
	__s8	share_name[KSMBD_REQ_MAX_SHARE_NAME];
} ____ksmbd_align;

struct ksmbd_share_config_response {
	__u32	handle;
	__u32	flags;
	__u16	create_mask;
	__u16	directory_mask;
	__u16	force_create_mode;
	__u16	force_directory_mode;
	__u16	force_uid;
	__u16	force_gid;
	__u32	veto_list_sz;
	__s8	____payload[0];
} ____ksmbd_align;

#define KSMBD_SHARE_CONFIG_VETO_LIST(s)	((s)->____payload)
#define KSMBD_SHARE_CONFIG_PATH(s)				\
	({							\
		char *p = (s)->____payload;			\
		if ((s)->veto_list_sz)				\
			p += (s)->veto_list_sz + 1;		\
		p;						\
	 })

struct ksmbd_tree_connect_request {
	__u32	handle;
	__u16	account_flags;
	__u16	flags;
	__u64	session_id;
	__u64	connect_id;
	__s8	account[KSMBD_REQ_MAX_ACCOUNT_NAME_SZ];
	__s8	share[KSMBD_REQ_MAX_SHARE_NAME];
	__s8	peer_addr[64];
} ____ksmbd_align;

struct ksmbd_tree_connect_response {
	__u32	handle;
	__u16	status;
	__u16	connection_flags;
} ____ksmbd_align;

struct ksmbd_tree_disconnect_request {
	__u64	session_id;
	__u64	connect_id;
} ____ksmbd_align;

struct ksmbd_logout_request {
	__s8	account[KSMBD_REQ_MAX_ACCOUNT_NAME_SZ];
} ____ksmbd_align;

struct ksmbd_rpc_command {
	__u32	handle;
	__u32	flags;
	__u32	payload_sz;
	__u8	payload[0];
} ____ksmbd_align;

struct ksmbd_spnego_authen_request {
	__u32	handle;
	__u16	spnego_blob_len;
	__u8	spnego_blob[0];
} ____ksmbd_align;

struct ksmbd_spnego_authen_response {
	__u32	handle;
	struct ksmbd_login_response	login_response;
	__u16	session_key_len;
	__u16	spnego_blob_len;
	__u8	payload[0];		/* session key + AP_REP */
} ____ksmbd_align;

/*
 * This also used as NETLINK attribute type value.
 *
 * NOTE:
 * Response message type value should be equal to
 * request message type value + 1.
 */
enum ksmbd_event {
	KSMBD_EVENT_UNSPEC			= 0,
	KSMBD_EVENT_HEARTBEAT_REQUEST,

	KSMBD_EVENT_STARTING_UP,
	KSMBD_EVENT_SHUTTING_DOWN,

	KSMBD_EVENT_LOGIN_REQUEST,
	KSMBD_EVENT_LOGIN_RESPONSE		= 5,

	KSMBD_EVENT_SHARE_CONFIG_REQUEST,
	KSMBD_EVENT_SHARE_CONFIG_RESPONSE,

	KSMBD_EVENT_TREE_CONNECT_REQUEST,
	KSMBD_EVENT_TREE_CONNECT_RESPONSE,

	KSMBD_EVENT_TREE_DISCONNECT_REQUEST	= 10,

	KSMBD_EVENT_LOGOUT_REQUEST,

	KSMBD_EVENT_RPC_REQUEST,
	KSMBD_EVENT_RPC_RESPONSE,

	KSMBD_EVENT_SPNEGO_AUTHEN_REQUEST,
	KSMBD_EVENT_SPNEGO_AUTHEN_RESPONSE	= 15,

	KSMBD_EVENT_MAX
};

enum KSMBD_TREE_CONN_STATUS {
	KSMBD_TREE_CONN_STATUS_OK		= 0,
	KSMBD_TREE_CONN_STATUS_NOMEM,
	KSMBD_TREE_CONN_STATUS_NO_SHARE,
	KSMBD_TREE_CONN_STATUS_NO_USER,
	KSMBD_TREE_CONN_STATUS_INVALID_USER,
	KSMBD_TREE_CONN_STATUS_HOST_DENIED	= 5,
	KSMBD_TREE_CONN_STATUS_CONN_EXIST,
	KSMBD_TREE_CONN_STATUS_TOO_MANY_CONNS,
	KSMBD_TREE_CONN_STATUS_TOO_MANY_SESSIONS,
	KSMBD_TREE_CONN_STATUS_ERROR,
};

/*
 * User config flags.
 */
#define KSMBD_USER_FLAG_INVALID		(0)
#define KSMBD_USER_FLAG_OK		(1 << 0)
#define KSMBD_USER_FLAG_BAD_PASSWORD	(1 << 1)
#define KSMBD_USER_FLAG_BAD_UID		(1 << 2)
#define KSMBD_USER_FLAG_BAD_USER	(1 << 3)
#define KSMBD_USER_FLAG_GUEST_ACCOUNT	(1 << 4)

/*
 * Share config flags.
 */
#define KSMBD_SHARE_FLAG_INVALID		(0)
#define KSMBD_SHARE_FLAG_AVAILABLE		(1 << 0)
#define KSMBD_SHARE_FLAG_BROWSEABLE		(1 << 1)
#define KSMBD_SHARE_FLAG_WRITEABLE		(1 << 2)
#define KSMBD_SHARE_FLAG_READONLY		(1 << 3)
#define KSMBD_SHARE_FLAG_GUEST_OK		(1 << 4)
#define KSMBD_SHARE_FLAG_GUEST_ONLY		(1 << 5)
#define KSMBD_SHARE_FLAG_STORE_DOS_ATTRS	(1 << 6)
#define KSMBD_SHARE_FLAG_OPLOCKS		(1 << 7)
#define KSMBD_SHARE_FLAG_PIPE			(1 << 8)
#define KSMBD_SHARE_FLAG_HIDE_DOT_FILES		(1 << 9)
#define KSMBD_SHARE_FLAG_INHERIT_SMACK		(1 << 10)
#define KSMBD_SHARE_FLAG_INHERIT_OWNER		(1 << 11)
#define KSMBD_SHARE_FLAG_STREAMS		(1 << 12)
#define KSMBD_SHARE_FLAG_FOLLOW_SYMLINKS	(1 << 13)
#define KSMBD_SHARE_FLAG_ACL_XATTR		(1 << 14)

/*
 * Tree connect request flags.
 */
#define KSMBD_TREE_CONN_FLAG_REQUEST_SMB1	(0)
#define KSMBD_TREE_CONN_FLAG_REQUEST_IPV6	(1 << 0)
#define KSMBD_TREE_CONN_FLAG_REQUEST_SMB2	(1 << 1)

/*
 * Tree connect flags.
 */
#define KSMBD_TREE_CONN_FLAG_GUEST_ACCOUNT	(1 << 0)
#define KSMBD_TREE_CONN_FLAG_READ_ONLY		(1 << 1)
#define KSMBD_TREE_CONN_FLAG_WRITABLE		(1 << 2)
#define KSMBD_TREE_CONN_FLAG_ADMIN_ACCOUNT	(1 << 3)

/*
 * RPC over IPC.
 */
#define KSMBD_RPC_METHOD_RETURN		(1 << 0)
#define KSMBD_RPC_SRVSVC_METHOD_INVOKE	(1 << 1)
#define KSMBD_RPC_SRVSVC_METHOD_RETURN	((1 << 1) | KSMBD_RPC_METHOD_RETURN)
#define KSMBD_RPC_WKSSVC_METHOD_INVOKE	(1 << 2)
#define KSMBD_RPC_WKSSVC_METHOD_RETURN	((1 << 2) | KSMBD_RPC_METHOD_RETURN)
#define KSMBD_RPC_IOCTL_METHOD		((1 << 3) | KSMBD_RPC_METHOD_RETURN)
#define KSMBD_RPC_OPEN_METHOD		(1 << 4)
#define KSMBD_RPC_WRITE_METHOD		(1 << 5)
#define KSMBD_RPC_READ_METHOD		((1 << 6) | KSMBD_RPC_METHOD_RETURN)
#define KSMBD_RPC_CLOSE_METHOD		(1 << 7)
#define KSMBD_RPC_RAP_METHOD		((1 << 8) | KSMBD_RPC_METHOD_RETURN)
#define KSMBD_RPC_RESTRICTED_CONTEXT	(1 << 9)
#define KSMBD_RPC_SAMR_METHOD_INVOKE	(1 << 10)
#define KSMBD_RPC_SAMR_METHOD_RETURN	((1 << 10) | KSMBD_RPC_METHOD_RETURN)
#define KSMBD_RPC_LSARPC_METHOD_INVOKE	(1 << 11)
#define KSMBD_RPC_LSARPC_METHOD_RETURN	((1 << 11) | KSMBD_RPC_METHOD_RETURN)

#define KSMBD_RPC_OK			0
#define KSMBD_RPC_EBAD_FUNC		0x00000001
#define KSMBD_RPC_EACCESS_DENIED	0x00000005
#define KSMBD_RPC_EBAD_FID		0x00000006
#define KSMBD_RPC_ENOMEM		0x00000008
#define KSMBD_RPC_EBAD_DATA		0x0000000D
#define KSMBD_RPC_ENOTIMPLEMENTED	0x00000040
#define KSMBD_RPC_EINVALID_PARAMETER	0x00000057
#define KSMBD_RPC_EMORE_DATA		0x000000EA
#define KSMBD_RPC_EINVALID_LEVEL	0x0000007C
#define KSMBD_RPC_SOME_NOT_MAPPED	0x00000107

#define KSMBD_CONFIG_OPT_DISABLED	0
#define KSMBD_CONFIG_OPT_ENABLED	1
#define KSMBD_CONFIG_OPT_AUTO		2
#define KSMBD_CONFIG_OPT_MANDATORY	3

#endif /* _LINUX_KSMBD_SERVER_H */
