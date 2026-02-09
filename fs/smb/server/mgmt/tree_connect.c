// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) 2018 Samsung Electronics Co., Ltd.
 */

#include <linux/list.h>
#include <linux/slab.h>
#include <linux/xarray.h>

#include "../transport_ipc.h"
#include "../connection.h"
#include "../stats.h"

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

	tree_conn = kzalloc(sizeof(struct ksmbd_tree_connect),
			    KSMBD_DEFAULT_GFP);
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

	down_write(&sess->tree_conns_lock);
	ret = xa_err(xa_store(&sess->tree_conns, tree_conn->id, tree_conn,
			      KSMBD_DEFAULT_GFP));
	up_write(&sess->tree_conns_lock);
	if (ret) {
		status.ret = -ENOMEM;
		goto out_error;
	}
	ksmbd_counter_inc(KSMBD_COUNTER_TREE_CONNS);
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
	if (atomic_dec_and_test(&tcon->refcount))
		kfree(tcon);
}

static int __ksmbd_tree_conn_disconnect(struct ksmbd_session *sess,
					struct ksmbd_tree_connect *tree_conn)
{
	int ret;

	ret = ksmbd_ipc_tree_disconnect_request(sess->id, tree_conn->id);
	ksmbd_release_tree_conn_id(sess, tree_conn->id);
	ksmbd_share_config_put(tree_conn->share_conf);
	ksmbd_counter_dec(KSMBD_COUNTER_TREE_CONNS);
	if (atomic_dec_and_test(&tree_conn->refcount))
		kfree(tree_conn);
	return ret;
}

int ksmbd_tree_conn_disconnect(struct ksmbd_session *sess,
			       struct ksmbd_tree_connect *tree_conn)
{
	down_write(&sess->tree_conns_lock);
	xa_erase(&sess->tree_conns, tree_conn->id);
	up_write(&sess->tree_conns_lock);

	return __ksmbd_tree_conn_disconnect(sess, tree_conn);
}

struct ksmbd_tree_connect *ksmbd_tree_conn_lookup(struct ksmbd_session *sess,
						  unsigned int id)
{
	struct ksmbd_tree_connect *tcon;

	down_read(&sess->tree_conns_lock);
	tcon = xa_load(&sess->tree_conns, id);
	if (tcon) {
		if (tcon->t_state != TREE_CONNECTED)
			tcon = NULL;
		else if (!atomic_inc_not_zero(&tcon->refcount))
			tcon = NULL;
	}
	up_read(&sess->tree_conns_lock);

	return tcon;
}

int ksmbd_tree_conn_session_logoff(struct ksmbd_session *sess)
{
	int ret = 0;
	struct ksmbd_tree_connect *tc;
	unsigned long id;

	if (!sess)
		return -EINVAL;

	down_write(&sess->tree_conns_lock);
	xa_for_each(&sess->tree_conns, id, tc) {
		if (tc->t_state == TREE_DISCONNECTED) {
			ret = -ENOENT;
			continue;
		}
		tc->t_state = TREE_DISCONNECTED;

		xa_erase(&sess->tree_conns, tc->id);
		ret |= __ksmbd_tree_conn_disconnect(sess, tc);
	}
	xa_destroy(&sess->tree_conns);
	up_write(&sess->tree_conns_lock);

	return ret;
}
