/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *   Copyright (C) 2018 Samsung Electronics Co., Ltd.
 *
 *   linux-ksmbd-devel@lists.sourceforge.net
 */

#ifndef _LINUX_KSMBD_SERVER_H
#define _LINUX_KSMBD_SERVER_H

#include <linux/types.h>

/*
 * This is a userspace ABI to communicate data between ksmbd and user IPC
 * daemon using netlink. This is added to track and cache user account DB
 * and share configuration info from userspace.
 *
 *  - KSMBD_EVENT_HEARTBEAT_REQUEST(ksmbd_heartbeat)
 *    This event is to check whether user IPC daemon is alive. If user IPC
 *    daemon is dead, ksmbd keep existing connection till disconnecting and
 *    new connection will be denied.
 *
 *  - KSMBD_EVENT_STARTING_UP(ksmbd_startup_request)
 *    This event is to receive the information that initializes the ksmbd
 *    server from the user IPC daemon and to start the server. The global
 *    section parameters are given from smb.conf as initialization
 *    information.
 *
 *  - KSMBD_EVENT_SHUTTING_DOWN(ksmbd_shutdown_request)
 *    This event is to shutdown ksmbd server.
 *
 *  - KSMBD_EVENT_LOGIN_REQUEST/RESPONSE(ksmbd_login_request/response)
 *    This event is to get user account info to user IPC daemon.
 *
 *  - KSMBD_EVENT_SHARE_CONFIG_REQUEST/RESPONSE(ksmbd_share_config_request/response)
 *    This event is to get net share configuration info.
 *
 *  - KSMBD_EVENT_TREE_CONNECT_REQUEST/RESPONSE(ksmbd_tree_connect_request/response)
 *    This event is to get session and tree connect info.
 *
 *  - KSMBD_EVENT_TREE_DISCONNECT_REQUEST(ksmbd_tree_disconnect_request)
 *    This event is to send tree disconnect info to user IPC daemon.
 *
 *  - KSMBD_EVENT_LOGOUT_REQUEST(ksmbd_logout_request)
 *    This event is to send logout request to user IPC daemon.
 *
 *  - KSMBD_EVENT_RPC_REQUEST/RESPONSE(ksmbd_rpc_command)
 *    This event is to make DCE/RPC request like srvsvc, wkssvc, lsarpc,
 *    samr to be processed in userspace.
 *
 *  - KSMBD_EVENT_SPNEGO_AUTHEN_REQUEST/RESPONSE(ksmbd_spnego_authen_request/response)
 *    This event is to make kerberos authentication to be processed in
 *    userspace.
 */

#define KSMBD_GENL_NAME		"SMBD_GENL"
#define KSMBD_GENL_VERSION		0x01

#define KSMBD_REQ_MAX_ACCOUNT_NAME_SZ	48
#define KSMBD_REQ_MAX_HASH_SZ		18
#define KSMBD_REQ_MAX_SHARE_NAME	64

/*
 * IPC heartbeat frame to check whether user IPC daemon is alive.
 */
struct ksmbd_heartbeat {
	__u32	handle;
};

/*
 * Global config flags.
 */
#define KSMBD_GLOBAL_FLAG_INVALID		(0)
#define KSMBD_GLOBAL_FLAG_SMB2_LEASES		BIT(0)
#define KSMBD_GLOBAL_FLAG_SMB2_ENCRYPTION	BIT(1)
#define KSMBD_GLOBAL_FLAG_SMB3_MULTICHANNEL	BIT(2)
#define KSMBD_GLOBAL_FLAG_SMB2_ENCRYPTION_OFF	BIT(3)
#define KSMBD_GLOBAL_FLAG_DURABLE_HANDLE	BIT(4)

/*
 * IPC request for ksmbd server startup
 */
struct ksmbd_startup_request {
	__u32	flags;			/* Flags for global config */
	__s32	signing;		/* Signing enabled */
	__s8	min_prot[16];		/* The minimum SMB protocol version */
	__s8	max_prot[16];		/* The maximum SMB protocol version */
	__s8	netbios_name[16];
	__s8	work_group[64];		/* Workgroup */
	__s8	server_string[64];	/* Server string */
	__u16	tcp_port;		/* tcp port */
	__u16	ipc_timeout;		/*
					 * specifies the number of seconds
					 * server will wait for the userspace to
					 * reply to heartbeat frames.
					 */
	__u32	deadtime;		/* Number of minutes of inactivity */
	__u32	file_max;		/* Limits the maximum number of open files */
	__u32	smb2_max_write;		/* MAX write size */
	__u32	smb2_max_read;		/* MAX read size */
	__u32	smb2_max_trans;		/* MAX trans size */
	__u32	share_fake_fscaps;	/*
					 * Support some special application that
					 * makes QFSINFO calls to check whether
					 * we set the SPARSE_FILES bit (0x40).
					 */
	__u32	sub_auth[3];		/* Subauth value for Security ID */
	__u32	smb2_max_credits;	/* MAX credits */
	__u32	smbd_max_io_size;	/* smbd read write size */
	__u32	max_connections;	/* Number of maximum simultaneous connections */
	__u32	reserved[126];		/* Reserved room */
	__u32	ifc_list_sz;		/* interfaces list size */
	__s8	____payload[];
};

#define KSMBD_STARTUP_CONFIG_INTERFACES(s)	((s)->____payload)

/*
 * IPC request to shutdown ksmbd server.
 */
struct ksmbd_shutdown_request {
	__s32	reserved[16];
};

/*
 * IPC user login request.
 */
struct ksmbd_login_request {
	__u32	handle;
	__s8	account[KSMBD_REQ_MAX_ACCOUNT_NAME_SZ]; /* user account name */
	__u32	reserved[16];				/* Reserved room */
};

/*
 * IPC user login response.
 */
struct ksmbd_login_response {
	__u32	handle;
	__u32	gid;					/* group id */
	__u32	uid;					/* user id */
	__s8	account[KSMBD_REQ_MAX_ACCOUNT_NAME_SZ]; /* user account name */
	__u16	status;
	__u16	hash_sz;			/* hash size */
	__s8	hash[KSMBD_REQ_MAX_HASH_SZ];	/* password hash */
	__u32	reserved[16];			/* Reserved room */
};

/*
 * IPC request to fetch net share config.
 */
struct ksmbd_share_config_request {
	__u32	handle;
	__s8	share_name[KSMBD_REQ_MAX_SHARE_NAME]; /* share name */
	__u32	reserved[16];		/* Reserved room */
};

/*
 * IPC response to the net share config request.
 */
struct ksmbd_share_config_response {
	__u32	handle;
	__u32	flags;
	__u16	create_mask;
	__u16	directory_mask;
	__u16	force_create_mode;
	__u16	force_directory_mode;
	__u16	force_uid;
	__u16	force_gid;
	__s8	share_name[KSMBD_REQ_MAX_SHARE_NAME];
	__u32	reserved[111];		/* Reserved room */
	__u32	payload_sz;
	__u32	veto_list_sz;
	__s8	____payload[];
};

#define KSMBD_SHARE_CONFIG_VETO_LIST(s)	((s)->____payload)

static inline char *
ksmbd_share_config_path(struct ksmbd_share_config_response *sc)
{
	char *p = sc->____payload;

	if (sc->veto_list_sz)
		p += sc->veto_list_sz + 1;

	return p;
}

/*
 * IPC request for tree connection. This request include session and tree
 * connect info from client.
 */
struct ksmbd_tree_connect_request {
	__u32	handle;
	__u16	account_flags;
	__u16	flags;
	__u64	session_id;
	__u64	connect_id;
	__s8	account[KSMBD_REQ_MAX_ACCOUNT_NAME_SZ];
	__s8	share[KSMBD_REQ_MAX_SHARE_NAME];
	__s8	peer_addr[64];
	__u32	reserved[16];		/* Reserved room */
};

/*
 * IPC Response structure for tree connection.
 */
struct ksmbd_tree_connect_response {
	__u32	handle;
	__u16	status;
	__u16	connection_flags;
	__u32	reserved[16];		/* Reserved room */
};

/*
 * IPC Request struture to disconnect tree connection.
 */
struct ksmbd_tree_disconnect_request {
	__u64	session_id;	/* session id */
	__u64	connect_id;	/* tree connection id */
	__u32	reserved[16];	/* Reserved room */
};

/*
 * IPC Response structure to logout user account.
 */
struct ksmbd_logout_request {
	__s8	account[KSMBD_REQ_MAX_ACCOUNT_NAME_SZ]; /* user account name */
	__u32	account_flags;
	__u32	reserved[16];				/* Reserved room */
};

/*
 * RPC command structure to send rpc request like srvsvc or wkssvc to
 * IPC user daemon.
 */
struct ksmbd_rpc_command {
	__u32	handle;
	__u32	flags;
	__u32	payload_sz;
	__u8	payload[];
};

/*
 * IPC Request Kerberos authentication
 */
struct ksmbd_spnego_authen_request {
	__u32	handle;
	__u16	spnego_blob_len;	/* the length of spnego_blob */
	__u8	spnego_blob[];		/*
					 * the GSS token from SecurityBuffer of
					 * SMB2 SESSION SETUP request
					 */
};

/*
 * Response data which includes the GSS token and the session key generated by
 * user daemon.
 */
struct ksmbd_spnego_authen_response {
	__u32	handle;
	struct ksmbd_login_response login_response; /*
						     * the login response with
						     * a user identified by the
						     * GSS token from a client
						     */
	__u16	session_key_len; /* the length of the session key */
	__u16	spnego_blob_len; /*
				  * the length of  the GSS token which will be
				  * stored in SecurityBuffer of SMB2 SESSION
				  * SETUP response
				  */
	__u8	payload[]; /* session key + AP_REP */
};

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

	__KSMBD_EVENT_MAX,
	KSMBD_EVENT_MAX = __KSMBD_EVENT_MAX - 1
};

/*
 * Enumeration for IPC tree connect status.
 */
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
#define KSMBD_USER_FLAG_OK		BIT(0)
#define KSMBD_USER_FLAG_BAD_PASSWORD	BIT(1)
#define KSMBD_USER_FLAG_BAD_UID		BIT(2)
#define KSMBD_USER_FLAG_BAD_USER	BIT(3)
#define KSMBD_USER_FLAG_GUEST_ACCOUNT	BIT(4)
#define KSMBD_USER_FLAG_DELAY_SESSION	BIT(5)

/*
 * Share config flags.
 */
#define KSMBD_SHARE_FLAG_INVALID			(0)
#define KSMBD_SHARE_FLAG_AVAILABLE			BIT(0)
#define KSMBD_SHARE_FLAG_BROWSEABLE			BIT(1)
#define KSMBD_SHARE_FLAG_WRITEABLE			BIT(2)
#define KSMBD_SHARE_FLAG_READONLY			BIT(3)
#define KSMBD_SHARE_FLAG_GUEST_OK			BIT(4)
#define KSMBD_SHARE_FLAG_GUEST_ONLY			BIT(5)
#define KSMBD_SHARE_FLAG_STORE_DOS_ATTRS		BIT(6)
#define KSMBD_SHARE_FLAG_OPLOCKS			BIT(7)
#define KSMBD_SHARE_FLAG_PIPE				BIT(8)
#define KSMBD_SHARE_FLAG_HIDE_DOT_FILES			BIT(9)
#define KSMBD_SHARE_FLAG_INHERIT_OWNER			BIT(10)
#define KSMBD_SHARE_FLAG_STREAMS			BIT(11)
#define KSMBD_SHARE_FLAG_FOLLOW_SYMLINKS		BIT(12)
#define KSMBD_SHARE_FLAG_ACL_XATTR			BIT(13)
#define KSMBD_SHARE_FLAG_UPDATE				BIT(14)
#define KSMBD_SHARE_FLAG_CROSSMNT			BIT(15)
#define KSMBD_SHARE_FLAG_CONTINUOUS_AVAILABILITY	BIT(16)

/*
 * Tree connect request flags.
 */
#define KSMBD_TREE_CONN_FLAG_REQUEST_SMB1	(0)
#define KSMBD_TREE_CONN_FLAG_REQUEST_IPV6	BIT(0)
#define KSMBD_TREE_CONN_FLAG_REQUEST_SMB2	BIT(1)

/*
 * Tree connect flags.
 */
#define KSMBD_TREE_CONN_FLAG_GUEST_ACCOUNT	BIT(0)
#define KSMBD_TREE_CONN_FLAG_READ_ONLY		BIT(1)
#define KSMBD_TREE_CONN_FLAG_WRITABLE		BIT(2)
#define KSMBD_TREE_CONN_FLAG_ADMIN_ACCOUNT	BIT(3)
#define KSMBD_TREE_CONN_FLAG_UPDATE		BIT(4)

/*
 * RPC over IPC.
 */
#define KSMBD_RPC_METHOD_RETURN		BIT(0)
#define KSMBD_RPC_SRVSVC_METHOD_INVOKE	BIT(1)
#define KSMBD_RPC_SRVSVC_METHOD_RETURN	(KSMBD_RPC_SRVSVC_METHOD_INVOKE | KSMBD_RPC_METHOD_RETURN)
#define KSMBD_RPC_WKSSVC_METHOD_INVOKE	BIT(2)
#define KSMBD_RPC_WKSSVC_METHOD_RETURN	(KSMBD_RPC_WKSSVC_METHOD_INVOKE | KSMBD_RPC_METHOD_RETURN)
#define KSMBD_RPC_IOCTL_METHOD		(BIT(3) | KSMBD_RPC_METHOD_RETURN)
#define KSMBD_RPC_OPEN_METHOD		BIT(4)
#define KSMBD_RPC_WRITE_METHOD		BIT(5)
#define KSMBD_RPC_READ_METHOD		(BIT(6) | KSMBD_RPC_METHOD_RETURN)
#define KSMBD_RPC_CLOSE_METHOD		BIT(7)
#define KSMBD_RPC_RAP_METHOD		(BIT(8) | KSMBD_RPC_METHOD_RETURN)
#define KSMBD_RPC_RESTRICTED_CONTEXT	BIT(9)
#define KSMBD_RPC_SAMR_METHOD_INVOKE	BIT(10)
#define KSMBD_RPC_SAMR_METHOD_RETURN	(KSMBD_RPC_SAMR_METHOD_INVOKE | KSMBD_RPC_METHOD_RETURN)
#define KSMBD_RPC_LSARPC_METHOD_INVOKE	BIT(11)
#define KSMBD_RPC_LSARPC_METHOD_RETURN	(KSMBD_RPC_LSARPC_METHOD_INVOKE | KSMBD_RPC_METHOD_RETURN)

/*
 * RPC status definitions.
 */
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
