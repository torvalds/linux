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
	unsigned long index;

	xa_for_each(&sess->ksmbd_chann_list, index, chann) {
		xa_erase(&sess->ksmbd_chann_list, index);
		kfree(chann);
	}

	xa_destroy(&sess->ksmbd_chann_list);
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

	pr_err("Unsupported RPC: %s\n", rpc_name);
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
		goto free_entry;

	resp = ksmbd_rpc_open(sess, entry->id);
	if (!resp)
		goto free_id;

	kvfree(resp);
	return entry->id;
free_id:
	ksmbd_rpc_id_free(entry->id);
free_entry:
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
		if (id == sess->id) {
			sess->last_active = jiffies;
			return sess;
		}
	}
	return NULL;
}

static void ksmbd_expire_session(struct ksmbd_conn *conn)
{
	unsigned long id;
	struct ksmbd_session *sess;

	down_write(&sessions_table_lock);
	xa_for_each(&conn->sessions, id, sess) {
		if (sess->state != SMB2_SESSION_VALID ||
		    time_after(jiffies,
			       sess->last_active + SMB2_SESSION_TIMEOUT)) {
			xa_erase(&conn->sessions, sess->id);
			hash_del(&sess->hlist);
			ksmbd_session_destroy(sess);
			continue;
		}
	}
	up_write(&sessions_table_lock);
}

int ksmbd_session_register(struct ksmbd_conn *conn,
			   struct ksmbd_session *sess)
{
	sess->dialect = conn->dialect;
	memcpy(sess->ClientGUID, conn->ClientGUID, SMB2_CLIENT_GUID_SIZE);
	ksmbd_expire_session(conn);
	return xa_err(xa_store(&conn->sessions, sess->id, sess, GFP_KERNEL));
}

static int ksmbd_chann_del(struct ksmbd_conn *conn, struct ksmbd_session *sess)
{
	struct channel *chann;

	chann = xa_erase(&sess->ksmbd_chann_list, (long)conn);
	if (!chann)
		return -ENOENT;

	kfree(chann);
	return 0;
}

void ksmbd_sessions_deregister(struct ksmbd_conn *conn)
{
	struct ksmbd_session *sess;
	unsigned long id;

	down_write(&sessions_table_lock);
	if (conn->binding) {
		int bkt;
		struct hlist_node *tmp;

		hash_for_each_safe(sessions_table, bkt, tmp, sess, hlist) {
			if (!ksmbd_chann_del(conn, sess) &&
			    xa_empty(&sess->ksmbd_chann_list)) {
				hash_del(&sess->hlist);
				ksmbd_session_destroy(sess);
			}
		}
	}

	xa_for_each(&conn->sessions, id, sess) {
		unsigned long chann_id;
		struct channel *chann;

		xa_for_each(&sess->ksmbd_chann_list, chann_id, chann) {
			if (chann->conn != conn)
				ksmbd_conn_set_exiting(chann->conn);
		}

		ksmbd_chann_del(conn, sess);
		if (xa_empty(&sess->ksmbd_chann_list)) {
			xa_erase(&conn->sessions, sess->id);
			hash_del(&sess->hlist);
			ksmbd_session_destroy(sess);
		}
	}
	up_write(&sessions_table_lock);
}

struct ksmbd_session *ksmbd_session_lookup(struct ksmbd_conn *conn,
					   unsigned long long id)
{
	struct ksmbd_session *sess;

	sess = xa_load(&conn->sessions, id);
	if (sess)
		sess->last_active = jiffies;
	return sess;
}

struct ksmbd_session *ksmbd_session_lookup_slowpath(unsigned long long id)
{
	struct ksmbd_session *sess;

	down_read(&sessions_table_lock);
	sess = __session_lookup(id);
	if (sess)
		sess->last_active = jiffies;
	up_read(&sessions_table_lock);

	return sess;
}

struct ksmbd_session *ksmbd_session_lookup_all(struct ksmbd_conn *conn,
					       unsigned long long id)
{
	struct ksmbd_session *sess;

	sess = ksmbd_session_lookup(conn, id);
	if (!sess && conn->binding)
		sess = ksmbd_session_lookup_slowpath(id);
	if (sess && sess->state != SMB2_SESSION_VALID)
		sess = NULL;
	return sess;
}

struct preauth_session *ksmbd_preauth_session_alloc(struct ksmbd_conn *conn,
						    u64 sess_id)
{
	struct preauth_session *sess;

	sess = kmalloc(sizeof(struct preauth_session), GFP_KERNEL);
	if (!sess)
		return NULL;

	sess->id = sess_id;
	memcpy(sess->Preauth_HashValue, conn->preauth_info->Preauth_HashValue,
	       PREAUTH_HASHVALUE_SIZE);
	list_add(&sess->preauth_entry, &conn->preauth_sess_table);

	return sess;
}

static bool ksmbd_preauth_session_id_match(struct preauth_session *sess,
					   unsigned long long id)
{
	return sess->id == id;
}

struct preauth_session *ksmbd_preauth_session_lookup(struct ksmbd_conn *conn,
						     unsigned long long id)
{
	struct preauth_session *sess = NULL;

	list_for_each_entry(sess, &conn->preauth_sess_table, preauth_entry) {
		if (ksmbd_preauth_session_id_match(sess, id))
			return sess;
	}
	return NULL;
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

	if (protocol != CIFDS_SESSION_FLAG_SMB2)
		return NULL;

	sess = kzalloc(sizeof(struct ksmbd_session), GFP_KERNEL);
	if (!sess)
		return NULL;

	if (ksmbd_init_file_table(&sess->file_table))
		goto error;

	sess->last_active = jiffies;
	sess->state = SMB2_SESSION_IN_PROGRESS;
	set_session_flag(sess, protocol);
	xa_init(&sess->tree_conns);
	xa_init(&sess->ksmbd_chann_list);
	INIT_LIST_HEAD(&sess->rpc_handle_list);
	sess->sequence_number = 1;

	ret = __init_smb2_session(sess);
	if (ret)
		goto error;

	ida_init(&sess->tree_conn_ida);

	down_write(&sessions_table_lock);
	hash_add(sessions_table, &sess->hlist, sess->id);
	up_write(&sessions_table_lock);

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
