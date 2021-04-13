// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) 2018 Samsung Electronics Co., Ltd.
 */

#include <linux/list.h>
#include <linux/slab.h>
#include <linux/rwsem.h>
#include <linux/xarray.h>

#include "ksmbd_ida.h"
#include "user_session.h"
#include "user_config.h"
#include "tree_connect.h"
#include "../transport_ipc.h"
#include "../connection.h"
#include "../buffer_pool.h"
#include "../vfs_cache.h"

static DEFINE_IDA(session_ida);

#define SESSION_HASH_BITS		3
static DEFINE_HASHTABLE(sessions_table, SESSION_HASH_BITS);
static DECLARE_RWSEM(sessions_table_lock);

struct ksmbd_session_rpc {
	int			id;
	unsigned int		method;
	struct list_head	list;
};

static void free_channel_list(struct ksmbd_session *sess)
{
	struct channel *chann;
	struct list_head *tmp, *t;

	list_for_each_safe(tmp, t, &sess->ksmbd_chann_list) {
		chann = list_entry(tmp, struct channel, chann_list);
		if (chann) {
			list_del(&chann->chann_list);
			kfree(chann);
		}
	}
}

static void __session_rpc_close(struct ksmbd_session *sess,
				struct ksmbd_session_rpc *entry)
{
	struct ksmbd_rpc_command *resp;

	resp = ksmbd_rpc_close(sess, entry->id);
	if (!resp)
		pr_err("Unable to close RPC pipe %d\n", entry->id);

	kvfree(resp);
	ksmbd_rpc_id_free(entry->id);
	kfree(entry);
}

static void ksmbd_session_rpc_clear_list(struct ksmbd_session *sess)
{
	struct ksmbd_session_rpc *entry;

	while (!list_empty(&sess->rpc_handle_list)) {
		entry = list_entry(sess->rpc_handle_list.next,
				   struct ksmbd_session_rpc,
				   list);

		list_del(&entry->list);
		__session_rpc_close(sess, entry);
	}
}

static int __rpc_method(char *rpc_name)
{
	if (!strcmp(rpc_name, "\\srvsvc") || !strcmp(rpc_name, "srvsvc"))
		return KSMBD_RPC_SRVSVC_METHOD_INVOKE;

	if (!strcmp(rpc_name, "\\wkssvc") || !strcmp(rpc_name, "wkssvc"))
		return KSMBD_RPC_WKSSVC_METHOD_INVOKE;

	if (!strcmp(rpc_name, "LANMAN") || !strcmp(rpc_name, "lanman"))
		return KSMBD_RPC_RAP_METHOD;

	if (!strcmp(rpc_name, "\\samr") || !strcmp(rpc_name, "samr"))
		return KSMBD_RPC_SAMR_METHOD_INVOKE;

	if (!strcmp(rpc_name, "\\lsarpc") || !strcmp(rpc_name, "lsarpc"))
		return KSMBD_RPC_LSARPC_METHOD_INVOKE;

	ksmbd_err("Unsupported RPC: %s\n", rpc_name);
	return 0;
}

int ksmbd_session_rpc_open(struct ksmbd_session *sess, char *rpc_name)
{
	struct ksmbd_session_rpc *entry;
	struct ksmbd_rpc_command *resp;
	int method;

	method = __rpc_method(rpc_name);
	if (!method)
		return -EINVAL;

	entry = kzalloc(sizeof(struct ksmbd_session_rpc), GFP_KERNEL);
	if (!entry)
		return -EINVAL;

	list_add(&entry->list, &sess->rpc_handle_list);
	entry->method = method;
	entry->id = ksmbd_ipc_id_alloc();
	if (entry->id < 0)
		goto error;

	resp = ksmbd_rpc_open(sess, entry->id);
	if (!resp)
		goto error;

	kvfree(resp);
	return entry->id;
error:
	list_del(&entry->list);
	kfree(entry);
	return -EINVAL;
}

void ksmbd_session_rpc_close(struct ksmbd_session *sess, int id)
{
	struct ksmbd_session_rpc *entry;

	list_for_each_entry(entry, &sess->rpc_handle_list, list) {
		if (entry->id == id) {
			list_del(&entry->list);
			__session_rpc_close(sess, entry);
			break;
		}
	}
}

int ksmbd_session_rpc_method(struct ksmbd_session *sess, int id)
{
	struct ksmbd_session_rpc *entry;

	list_for_each_entry(entry, &sess->rpc_handle_list, list) {
		if (entry->id == id)
			return entry->method;
	}
	return 0;
}

void ksmbd_session_destroy(struct ksmbd_session *sess)
{
	if (!sess)
		return;

	if (!atomic_dec_and_test(&sess->refcnt))
		return;

	list_del(&sess->sessions_entry);

	if (IS_SMB2(sess->conn)) {
		down_write(&sessions_table_lock);
		hash_del(&sess->hlist);
		up_write(&sessions_table_lock);
	}

	if (sess->user)
		ksmbd_free_user(sess->user);

	ksmbd_tree_conn_session_logoff(sess);
	ksmbd_destroy_file_table(&sess->file_table);
	ksmbd_session_rpc_clear_list(sess);
	free_channel_list(sess);
	kfree(sess->Preauth_HashValue);
	ksmbd_release_id(&session_ida, sess->id);
	kfree(sess);
}

static struct ksmbd_session *__session_lookup(unsigned long long id)
{
	struct ksmbd_session *sess;

	hash_for_each_possible(sessions_table, sess, hlist, id) {
		if (id == sess->id)
			return sess;
	}
	return NULL;
}

void ksmbd_session_register(struct ksmbd_conn *conn,
			    struct ksmbd_session *sess)
{
	sess->conn = conn;
	list_add(&sess->sessions_entry, &conn->sessions);
}

void ksmbd_sessions_deregister(struct ksmbd_conn *conn)
{
	struct ksmbd_session *sess;

	while (!list_empty(&conn->sessions)) {
		sess = list_entry(conn->sessions.next,
				  struct ksmbd_session,
				  sessions_entry);

		ksmbd_session_destroy(sess);
	}
}

bool ksmbd_session_id_match(struct ksmbd_session *sess, unsigned long long id)
{
	return sess->id == id;
}

struct ksmbd_session *ksmbd_session_lookup(struct ksmbd_conn *conn,
					   unsigned long long id)
{
	struct ksmbd_session *sess = NULL;

	list_for_each_entry(sess, &conn->sessions, sessions_entry) {
		if (ksmbd_session_id_match(sess, id))
			return sess;
	}
	return NULL;
}

int get_session(struct ksmbd_session *sess)
{
	return atomic_inc_not_zero(&sess->refcnt);
}

void put_session(struct ksmbd_session *sess)
{
	if (atomic_dec_and_test(&sess->refcnt))
		ksmbd_err("get/%s seems to be mismatched.", __func__);
}

struct ksmbd_session *ksmbd_session_lookup_slowpath(unsigned long long id)
{
	struct ksmbd_session *sess;

	down_read(&sessions_table_lock);
	sess = __session_lookup(id);
	if (sess) {
		if (!get_session(sess))
			sess = NULL;
	}
	up_read(&sessions_table_lock);

	return sess;
}

static int __init_smb2_session(struct ksmbd_session *sess)
{
	int id = ksmbd_acquire_smb2_uid(&session_ida);

	if (id < 0)
		return -EINVAL;
	sess->id = id;
	return 0;
}

static struct ksmbd_session *__session_create(int protocol)
{
	struct ksmbd_session *sess;
	int ret;

	sess = kzalloc(sizeof(struct ksmbd_session), GFP_KERNEL);
	if (!sess)
		return NULL;

	if (ksmbd_init_file_table(&sess->file_table))
		goto error;

	set_session_flag(sess, protocol);
	INIT_LIST_HEAD(&sess->sessions_entry);
	xa_init(&sess->tree_conns);
	INIT_LIST_HEAD(&sess->ksmbd_chann_list);
	INIT_LIST_HEAD(&sess->rpc_handle_list);
	sess->sequence_number = 1;
	atomic_set(&sess->refcnt, 1);

	switch (protocol) {
	case CIFDS_SESSION_FLAG_SMB2:
		ret = __init_smb2_session(sess);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (ret)
		goto error;

	ida_init(&sess->tree_conn_ida);

	if (protocol == CIFDS_SESSION_FLAG_SMB2) {
		down_write(&sessions_table_lock);
		hash_add(sessions_table, &sess->hlist, sess->id);
		up_write(&sessions_table_lock);
	}
	return sess;

error:
	ksmbd_session_destroy(sess);
	return NULL;
}

struct ksmbd_session *ksmbd_smb2_session_create(void)
{
	return __session_create(CIFDS_SESSION_FLAG_SMB2);
}

int ksmbd_acquire_tree_conn_id(struct ksmbd_session *sess)
{
	int id = -EINVAL;

	if (test_session_flag(sess, CIFDS_SESSION_FLAG_SMB2))
		id = ksmbd_acquire_smb2_tid(&sess->tree_conn_ida);

	return id;
}

void ksmbd_release_tree_conn_id(struct ksmbd_session *sess, int id)
{
	if (id >= 0)
		ksmbd_release_id(&sess->tree_conn_ida, id);
}
