// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) 2018 Samsung Electronics Co., Ltd.
 */

#include <linux/list.h>
#include <linux/slab.h>
#include <linux/xarray.h>

#include "../transport_ipc.h"
#include "../connection.h"

#include "tree_connect.h"
#include "user_config.h"
#include "share_config.h"
#include "user_session.h"

struct ksmbd_tree_conn_status
ksmbd_tree_conn_connect(struct ksmbd_work *work, const char *share_name)
{
	struct ksmbd_tree_conn_status status = {-ENOENT, NULL};
	struct ksmbd_tree_connect_response *resp = NULL;
	struct ksmbd_share_config *sc;
	struct ksmbd_tree_connect *tree_conn = NULL;
	struct sockaddr *peer_addr;
	struct ksmbd_conn *conn = work->conn;
	struct ksmbd_session *sess = work->sess;
	int ret;

	sc = ksmbd_share_config_get(work, share_name);
	if (!sc)
		return status;

	tree_conn = kzalloc(sizeof(struct ksmbd_tree_connect), GFP_KERNEL);
	if (!tree_conn) {
		status.ret = -ENOMEM;
		goto out_error;
	}

	tree_conn->id = ksmbd_acquire_tree_conn_id(sess);
	if (tree_conn->id < 0) {
		status.ret = -EINVAL;
		goto out_error;
	}

	peer_addr = KSMBD_TCP_PEER_SOCKADDR(conn);
	resp = ksmbd_ipc_tree_connect_request(sess,
					      sc,
					      tree_conn,
					      peer_addr);
	if (!resp) {
		status.ret = -EINVAL;
		goto out_error;
	}

	status.ret = resp->status;
	if (status.ret != KSMBD_TREE_CONN_STATUS_OK)
		goto out_error;

	tree_conn->flags = resp->connection_flags;
	if (test_tree_conn_flag(tree_conn, KSMBD_TREE_CONN_FLAG_UPDATE)) {
		struct ksmbd_share_config *new_sc;

		ksmbd_share_config_del(sc);
		new_sc = ksmbd_share_config_get(work, share_name);
		if (!new_sc) {
			pr_err("Failed to update stale share config\n");
			status.ret = -ESTALE;
			goto out_error;
		}
		ksmbd_share_config_put(sc);
		sc = new_sc;
	}

	tree_conn->user = sess->user;
	tree_conn->share_conf = sc;
	tree_conn->t_state = TREE_NEW;
	status.tree_conn = tree_conn;
	atomic_set(&tree_conn->refcount, 1);
	init_waitqueue_head(&tree_conn->refcount_q);

	ret = xa_err(xa_store(&sess->tree_conns, tree_conn->id, tree_conn,
			      GFP_KERNEL));
	if (ret) {
		status.ret = -ENOMEM;
		goto out_error;
	}
	kvfree(resp);
	return status;

out_error:
	if (tree_conn)
		ksmbd_release_tree_conn_id(sess, tree_conn->id);
	ksmbd_share_config_put(sc);
	kfree(tree_conn);
	kvfree(resp);
	return status;
}

void ksmbd_tree_connect_put(struct ksmbd_tree_connect *tcon)
{
	/*
	 * Checking waitqueue to releasing tree connect on
	 * tree disconnect. waitqueue_active is safe because it
	 * uses atomic operation for condition.
	 */
	if (!atomic_dec_return(&tcon->refcount) &&
	    waitqueue_active(&tcon->refcount_q))
		wake_up(&tcon->refcount_q);
}

int ksmbd_tree_conn_disconnect(struct ksmbd_session *sess,
			       struct ksmbd_tree_connect *tree_conn)
{
	int ret;

	write_lock(&sess->tree_conns_lock);
	xa_erase(&sess->tree_conns, tree_conn->id);
	write_unlock(&sess->tree_conns_lock);

	if (!atomic_dec_and_test(&tree_conn->refcount))
		wait_event(tree_conn->refcount_q,
			   atomic_read(&tree_conn->refcount) == 0);

	ret = ksmbd_ipc_tree_disconnect_request(sess->id, tree_conn->id);
	ksmbd_release_tree_conn_id(sess, tree_conn->id);
	ksmbd_share_config_put(tree_conn->share_conf);
	kfree(tree_conn);
	return ret;
}

struct ksmbd_tree_connect *ksmbd_tree_conn_lookup(struct ksmbd_session *sess,
						  unsigned int id)
{
	struct ksmbd_tree_connect *tcon;

	read_lock(&sess->tree_conns_lock);
	tcon = xa_load(&sess->tree_conns, id);
	if (tcon) {
		if (tcon->t_state != TREE_CONNECTED)
			tcon = NULL;
		else if (!atomic_inc_not_zero(&tcon->refcount))
			tcon = NULL;
	}
	read_unlock(&sess->tree_conns_lock);

	return tcon;
}

int ksmbd_tree_conn_session_logoff(struct ksmbd_session *sess)
{
	int ret = 0;
	struct ksmbd_tree_connect *tc;
	unsigned long id;

	if (!sess)
		return -EINVAL;

	xa_for_each(&sess->tree_conns, id, tc) {
		write_lock(&sess->tree_conns_lock);
		if (tc->t_state == TREE_DISCONNECTED) {
			write_unlock(&sess->tree_conns_lock);
			ret = -ENOENT;
			continue;
		}
		tc->t_state = TREE_DISCONNECTED;
		write_unlock(&sess->tree_conns_lock);

		ret |= ksmbd_tree_conn_disconnect(sess, tc);
	}
	xa_destroy(&sess->tree_conns);
	return ret;
}
