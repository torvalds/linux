// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) 2016 Namjae Jeon <linkinjeon@kernel.org>
 *   Copyright (C) 2018 Samsung Electronics Co., Ltd.
 */

#include <linux/slab.h>
#include "glob.h"

#include "auth.h"
#include "connection.h"
#include "smb_common.h"
#include "server.h"

static struct smb_version_values smb21_server_values = {
	.version_string = SMB21_VERSION_STRING,
	.protocol_id = SMB21_PROT_ID,
	.capabilities = SMB2_GLOBAL_CAP_LARGE_MTU,
	.max_read_size = SMB21_DEFAULT_IOSIZE,
	.max_write_size = SMB21_DEFAULT_IOSIZE,
	.max_trans_size = SMB21_DEFAULT_IOSIZE,
	.max_credits = SMB2_MAX_CREDITS,
	.large_lock_type = 0,
	.exclusive_lock_type = SMB2_LOCKFLAG_EXCLUSIVE,
	.shared_lock_type = SMB2_LOCKFLAG_SHARED,
	.unlock_lock_type = SMB2_LOCKFLAG_UNLOCK,
	.header_size = sizeof(struct smb2_hdr),
	.max_header_size = MAX_SMB2_HDR_SIZE,
	.read_rsp_size = sizeof(struct smb2_read_rsp),
	.lock_cmd = SMB2_LOCK,
	.cap_unix = 0,
	.cap_nt_find = SMB2_NT_FIND,
	.cap_large_files = SMB2_LARGE_FILES,
	.create_lease_size = sizeof(struct create_lease),
	.create_durable_size = sizeof(struct create_durable_rsp),
	.create_mxac_size = sizeof(struct create_mxac_rsp),
	.create_disk_id_size = sizeof(struct create_disk_id_rsp),
	.create_posix_size = sizeof(struct create_posix_rsp),
};

static struct smb_version_values smb30_server_values = {
	.version_string = SMB30_VERSION_STRING,
	.protocol_id = SMB30_PROT_ID,
	.capabilities = SMB2_GLOBAL_CAP_LARGE_MTU,
	.max_read_size = SMB3_DEFAULT_IOSIZE,
	.max_write_size = SMB3_DEFAULT_IOSIZE,
	.max_trans_size = SMB3_DEFAULT_TRANS_SIZE,
	.max_credits = SMB2_MAX_CREDITS,
	.large_lock_type = 0,
	.exclusive_lock_type = SMB2_LOCKFLAG_EXCLUSIVE,
	.shared_lock_type = SMB2_LOCKFLAG_SHARED,
	.unlock_lock_type = SMB2_LOCKFLAG_UNLOCK,
	.header_size = sizeof(struct smb2_hdr),
	.max_header_size = MAX_SMB2_HDR_SIZE,
	.read_rsp_size = sizeof(struct smb2_read_rsp),
	.lock_cmd = SMB2_LOCK,
	.cap_unix = 0,
	.cap_nt_find = SMB2_NT_FIND,
	.cap_large_files = SMB2_LARGE_FILES,
	.create_lease_size = sizeof(struct create_lease_v2),
	.create_durable_size = sizeof(struct create_durable_rsp),
	.create_durable_v2_size = sizeof(struct create_durable_v2_rsp),
	.create_mxac_size = sizeof(struct create_mxac_rsp),
	.create_disk_id_size = sizeof(struct create_disk_id_rsp),
	.create_posix_size = sizeof(struct create_posix_rsp),
};

static struct smb_version_values smb302_server_values = {
	.version_string = SMB302_VERSION_STRING,
	.protocol_id = SMB302_PROT_ID,
	.capabilities = SMB2_GLOBAL_CAP_LARGE_MTU,
	.max_read_size = SMB3_DEFAULT_IOSIZE,
	.max_write_size = SMB3_DEFAULT_IOSIZE,
	.max_trans_size = SMB3_DEFAULT_TRANS_SIZE,
	.max_credits = SMB2_MAX_CREDITS,
	.large_lock_type = 0,
	.exclusive_lock_type = SMB2_LOCKFLAG_EXCLUSIVE,
	.shared_lock_type = SMB2_LOCKFLAG_SHARED,
	.unlock_lock_type = SMB2_LOCKFLAG_UNLOCK,
	.header_size = sizeof(struct smb2_hdr),
	.max_header_size = MAX_SMB2_HDR_SIZE,
	.read_rsp_size = sizeof(struct smb2_read_rsp),
	.lock_cmd = SMB2_LOCK,
	.cap_unix = 0,
	.cap_nt_find = SMB2_NT_FIND,
	.cap_large_files = SMB2_LARGE_FILES,
	.create_lease_size = sizeof(struct create_lease_v2),
	.create_durable_size = sizeof(struct create_durable_rsp),
	.create_durable_v2_size = sizeof(struct create_durable_v2_rsp),
	.create_mxac_size = sizeof(struct create_mxac_rsp),
	.create_disk_id_size = sizeof(struct create_disk_id_rsp),
	.create_posix_size = sizeof(struct create_posix_rsp),
};

static struct smb_version_values smb311_server_values = {
	.version_string = SMB311_VERSION_STRING,
	.protocol_id = SMB311_PROT_ID,
	.capabilities = SMB2_GLOBAL_CAP_LARGE_MTU,
	.max_read_size = SMB3_DEFAULT_IOSIZE,
	.max_write_size = SMB3_DEFAULT_IOSIZE,
	.max_trans_size = SMB3_DEFAULT_TRANS_SIZE,
	.max_credits = SMB2_MAX_CREDITS,
	.large_lock_type = 0,
	.exclusive_lock_type = SMB2_LOCKFLAG_EXCLUSIVE,
	.shared_lock_type = SMB2_LOCKFLAG_SHARED,
	.unlock_lock_type = SMB2_LOCKFLAG_UNLOCK,
	.header_size = sizeof(struct smb2_hdr),
	.max_header_size = MAX_SMB2_HDR_SIZE,
	.read_rsp_size = sizeof(struct smb2_read_rsp),
	.lock_cmd = SMB2_LOCK,
	.cap_unix = 0,
	.cap_nt_find = SMB2_NT_FIND,
	.cap_large_files = SMB2_LARGE_FILES,
	.create_lease_size = sizeof(struct create_lease_v2),
	.create_durable_size = sizeof(struct create_durable_rsp),
	.create_durable_v2_size = sizeof(struct create_durable_v2_rsp),
	.create_mxac_size = sizeof(struct create_mxac_rsp),
	.create_disk_id_size = sizeof(struct create_disk_id_rsp),
	.create_posix_size = sizeof(struct create_posix_rsp),
};

static struct smb_version_ops smb2_0_server_ops = {
	.get_cmd_val		=	get_smb2_cmd_val,
	.init_rsp_hdr		=	init_smb2_rsp_hdr,
	.set_rsp_status		=	set_smb2_rsp_status,
	.allocate_rsp_buf       =       smb2_allocate_rsp_buf,
	.set_rsp_credits	=	smb2_set_rsp_credits,
	.check_user_session	=	smb2_check_user_session,
	.get_ksmbd_tcon		=	smb2_get_ksmbd_tcon,
	.is_sign_req		=	smb2_is_sign_req,
	.check_sign_req		=	smb2_check_sign_req,
	.set_sign_rsp		=	smb2_set_sign_rsp
};

static struct smb_version_ops smb3_0_server_ops = {
	.get_cmd_val		=	get_smb2_cmd_val,
	.init_rsp_hdr		=	init_smb2_rsp_hdr,
	.set_rsp_status		=	set_smb2_rsp_status,
	.allocate_rsp_buf       =       smb2_allocate_rsp_buf,
	.set_rsp_credits	=	smb2_set_rsp_credits,
	.check_user_session	=	smb2_check_user_session,
	.get_ksmbd_tcon		=	smb2_get_ksmbd_tcon,
	.is_sign_req		=	smb2_is_sign_req,
	.check_sign_req		=	smb3_check_sign_req,
	.set_sign_rsp		=	smb3_set_sign_rsp,
	.generate_signingkey	=	ksmbd_gen_smb30_signingkey,
	.generate_encryptionkey	=	ksmbd_gen_smb30_encryptionkey,
	.is_transform_hdr	=	smb3_is_transform_hdr,
	.decrypt_req		=	smb3_decrypt_req,
	.encrypt_resp		=	smb3_encrypt_resp
};

static struct smb_version_ops smb3_11_server_ops = {
	.get_cmd_val		=	get_smb2_cmd_val,
	.init_rsp_hdr		=	init_smb2_rsp_hdr,
	.set_rsp_status		=	set_smb2_rsp_status,
	.allocate_rsp_buf       =       smb2_allocate_rsp_buf,
	.set_rsp_credits	=	smb2_set_rsp_credits,
	.check_user_session	=	smb2_check_user_session,
	.get_ksmbd_tcon		=	smb2_get_ksmbd_tcon,
	.is_sign_req		=	smb2_is_sign_req,
	.check_sign_req		=	smb3_check_sign_req,
	.set_sign_rsp		=	smb3_set_sign_rsp,
	.generate_signingkey	=	ksmbd_gen_smb311_signingkey,
	.generate_encryptionkey	=	ksmbd_gen_smb311_encryptionkey,
	.is_transform_hdr	=	smb3_is_transform_hdr,
	.decrypt_req		=	smb3_decrypt_req,
	.encrypt_resp		=	smb3_encrypt_resp
};

static struct smb_version_cmds smb2_0_server_cmds[NUMBER_OF_SMB2_COMMANDS] = {
	[SMB2_NEGOTIATE_HE]	=	{ .proc = smb2_negotiate_request, },
	[SMB2_SESSION_SETUP_HE] =	{ .proc = smb2_sess_setup, },
	[SMB2_TREE_CONNECT_HE]  =	{ .proc = smb2_tree_connect,},
	[SMB2_TREE_DISCONNECT_HE]  =	{ .proc = smb2_tree_disconnect,},
	[SMB2_LOGOFF_HE]	=	{ .proc = smb2_session_logoff,},
	[SMB2_CREATE_HE]	=	{ .proc = smb2_open},
	[SMB2_QUERY_INFO_HE]	=	{ .proc = smb2_query_info},
	[SMB2_QUERY_DIRECTORY_HE] =	{ .proc = smb2_query_dir},
	[SMB2_CLOSE_HE]		=	{ .proc = smb2_close},
	[SMB2_ECHO_HE]		=	{ .proc = smb2_echo},
	[SMB2_SET_INFO_HE]      =       { .proc = smb2_set_info},
	[SMB2_READ_HE]		=	{ .proc = smb2_read},
	[SMB2_WRITE_HE]		=	{ .proc = smb2_write},
	[SMB2_FLUSH_HE]		=	{ .proc = smb2_flush},
	[SMB2_CANCEL_HE]	=	{ .proc = smb2_cancel},
	[SMB2_LOCK_HE]		=	{ .proc = smb2_lock},
	[SMB2_IOCTL_HE]		=	{ .proc = smb2_ioctl},
	[SMB2_OPLOCK_BREAK_HE]	=	{ .proc = smb2_oplock_break},
	[SMB2_CHANGE_NOTIFY_HE]	=	{ .proc = smb2_notify},
};

/**
 * init_smb2_1_server() - initialize a smb server connection with smb2.1
 *			command dispatcher
 * @conn:	connection instance
 */
void init_smb2_1_server(struct ksmbd_conn *conn)
{
	conn->vals = &smb21_server_values;
	conn->ops = &smb2_0_server_ops;
	conn->cmds = smb2_0_server_cmds;
	conn->max_cmds = ARRAY_SIZE(smb2_0_server_cmds);
	conn->signing_algorithm = SIGNING_ALG_HMAC_SHA256_LE;

	if (server_conf.flags & KSMBD_GLOBAL_FLAG_SMB2_LEASES)
		conn->vals->capabilities |= SMB2_GLOBAL_CAP_LEASING;
}

/**
 * init_smb3_0_server() - initialize a smb server connection with smb3.0
 *			command dispatcher
 * @conn:	connection instance
 */
void init_smb3_0_server(struct ksmbd_conn *conn)
{
	conn->vals = &smb30_server_values;
	conn->ops = &smb3_0_server_ops;
	conn->cmds = smb2_0_server_cmds;
	conn->max_cmds = ARRAY_SIZE(smb2_0_server_cmds);
	conn->signing_algorithm = SIGNING_ALG_AES_CMAC_LE;

	if (server_conf.flags & KSMBD_GLOBAL_FLAG_SMB2_LEASES)
		conn->vals->capabilities |= SMB2_GLOBAL_CAP_LEASING |
			SMB2_GLOBAL_CAP_DIRECTORY_LEASING;

	if (server_conf.flags & KSMBD_GLOBAL_FLAG_SMB2_ENCRYPTION &&
	    conn->cli_cap & SMB2_GLOBAL_CAP_ENCRYPTION)
		conn->vals->capabilities |= SMB2_GLOBAL_CAP_ENCRYPTION;

	if (server_conf.flags & KSMBD_GLOBAL_FLAG_SMB2_ENCRYPTION ||
	    (!(server_conf.flags & KSMBD_GLOBAL_FLAG_SMB2_ENCRYPTION_OFF) &&
	     conn->cli_cap & SMB2_GLOBAL_CAP_ENCRYPTION))
		conn->vals->capabilities |= SMB2_GLOBAL_CAP_ENCRYPTION;

	if (server_conf.flags & KSMBD_GLOBAL_FLAG_SMB3_MULTICHANNEL)
		conn->vals->capabilities |= SMB2_GLOBAL_CAP_MULTI_CHANNEL;
}

/**
 * init_smb3_02_server() - initialize a smb server connection with smb3.02
 *			command dispatcher
 * @conn:	connection instance
 */
void init_smb3_02_server(struct ksmbd_conn *conn)
{
	conn->vals = &smb302_server_values;
	conn->ops = &smb3_0_server_ops;
	conn->cmds = smb2_0_server_cmds;
	conn->max_cmds = ARRAY_SIZE(smb2_0_server_cmds);
	conn->signing_algorithm = SIGNING_ALG_AES_CMAC_LE;

	if (server_conf.flags & KSMBD_GLOBAL_FLAG_SMB2_LEASES)
		conn->vals->capabilities |= SMB2_GLOBAL_CAP_LEASING |
			SMB2_GLOBAL_CAP_DIRECTORY_LEASING;

	if (server_conf.flags & KSMBD_GLOBAL_FLAG_SMB2_ENCRYPTION ||
	    (!(server_conf.flags & KSMBD_GLOBAL_FLAG_SMB2_ENCRYPTION_OFF) &&
	     conn->cli_cap & SMB2_GLOBAL_CAP_ENCRYPTION))
		conn->vals->capabilities |= SMB2_GLOBAL_CAP_ENCRYPTION;

	if (server_conf.flags & KSMBD_GLOBAL_FLAG_SMB3_MULTICHANNEL)
		conn->vals->capabilities |= SMB2_GLOBAL_CAP_MULTI_CHANNEL;

	if (server_conf.flags & KSMBD_GLOBAL_FLAG_DURABLE_HANDLE)
		conn->vals->capabilities |= SMB2_GLOBAL_CAP_PERSISTENT_HANDLES;
}

/**
 * init_smb3_11_server() - initialize a smb server connection with smb3.11
 *			command dispatcher
 * @conn:	connection instance
 */
int init_smb3_11_server(struct ksmbd_conn *conn)
{
	conn->vals = &smb311_server_values;
	conn->ops = &smb3_11_server_ops;
	conn->cmds = smb2_0_server_cmds;
	conn->max_cmds = ARRAY_SIZE(smb2_0_server_cmds);
	conn->signing_algorithm = SIGNING_ALG_AES_CMAC_LE;

	if (server_conf.flags & KSMBD_GLOBAL_FLAG_SMB2_LEASES)
		conn->vals->capabilities |= SMB2_GLOBAL_CAP_LEASING |
			SMB2_GLOBAL_CAP_DIRECTORY_LEASING;

	if (server_conf.flags & KSMBD_GLOBAL_FLAG_SMB3_MULTICHANNEL)
		conn->vals->capabilities |= SMB2_GLOBAL_CAP_MULTI_CHANNEL;

	if (server_conf.flags & KSMBD_GLOBAL_FLAG_DURABLE_HANDLE)
		conn->vals->capabilities |= SMB2_GLOBAL_CAP_PERSISTENT_HANDLES;

	INIT_LIST_HEAD(&conn->preauth_sess_table);
	return 0;
}

void init_smb2_max_read_size(unsigned int sz)
{
	sz = clamp_val(sz, SMB3_MIN_IOSIZE, SMB3_MAX_IOSIZE);
	smb21_server_values.max_read_size = sz;
	smb30_server_values.max_read_size = sz;
	smb302_server_values.max_read_size = sz;
	smb311_server_values.max_read_size = sz;
}

void init_smb2_max_write_size(unsigned int sz)
{
	sz = clamp_val(sz, SMB3_MIN_IOSIZE, SMB3_MAX_IOSIZE);
	smb21_server_values.max_write_size = sz;
	smb30_server_values.max_write_size = sz;
	smb302_server_values.max_write_size = sz;
	smb311_server_values.max_write_size = sz;
}

void init_smb2_max_trans_size(unsigned int sz)
{
	sz = clamp_val(sz, SMB3_MIN_IOSIZE, SMB3_MAX_IOSIZE);
	smb21_server_values.max_trans_size = sz;
	smb30_server_values.max_trans_size = sz;
	smb302_server_values.max_trans_size = sz;
	smb311_server_values.max_trans_size = sz;
}

void init_smb2_max_credits(unsigned int sz)
{
	smb21_server_values.max_credits = sz;
	smb30_server_values.max_credits = sz;
	smb302_server_values.max_credits = sz;
	smb311_server_values.max_credits = sz;
}
