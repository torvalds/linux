// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) 2018 Samsung Electronics Co., Ltd.
 */

#include <linux/list.h>
#include <linux/slab.h>

#include "../buffer_pool.h"
#include "../transport_ipc.h"
#include "../connection.h"

#include "tree_connect.h"
#include "user_config.h"
#include "share_config.h"
#include "user_session.h"

struct ksmbd_tree_conn_status
ksmbd_tree_conn_connect(struct ksmbd_session *sess, char *share_name)
{
	struct ksmbd_tree_conn_status status = {-EINVAL, NULL};
	struct ksmbd_tree_connect_response *resp = NULL;
	struct ksmbd_share_config *sc;
	struct ksmbd_tree_connect *tree_conn = NULL;
	struct sockaddr *peer_addr;

	sc = ksmbd_share_config_get(share_name);
	if (!sc)
		return status;

	tree_conn = ksmbd_alloc(sizeof(struct ksmbd_tree_connect));
	if (!tree_conn) {
		status.ret = -ENOMEM;
		goto out_error;
	}

	tree_conn->id = ksmbd_acquire_tree_conn_id(sess);
	if (tree_conn->id < 0) {
		status.ret = -EINVAL;
		goto out_error;
	}

	peer_addr = KSMBD_TCP_PEER_SOCKADDR(sess->conn);
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
	tree_conn->user = sess->user;
	tree_conn->share_conf = sc;
	status.tree_conn = tree_conn;

	list_add(&tree_conn->list, &sess->tree_conn_list);

	ksmbd_free(resp);
	return status;

out_error:
	if (tree_conn)
		ksmbd_release_tree_conn_id(sess, tree_conn->id);
	ksmbd_share_config_put(sc);
	ksmbd_free(tree_conn);
	ksmbd_free(resp);
	return status;
}

int ksmbd_tree_conn_disconnect(struct ksmbd_session *sess,
			       struct ksmbd_tree_connect *tree_conn)
{
	int ret;

	ret = ksmbd_ipc_tree_disconnect_request(sess->id, tree_conn->id);
	ksmbd_release_tree_conn_id(sess, tree_conn->id);
	list_del(&tree_conn->list);
	ksmbd_share_config_put(tree_conn->share_conf);
	ksmbd_free(tree_conn);
	return ret;
}

struct ksmbd_tree_connect *ksmbd_tree_conn_lookup(struct ksmbd_session *sess,
						  unsigned int id)
{
	struct ksmbd_tree_connect *tree_conn;
	struct list_head *tmp;

	list_for_each(tmp, &sess->tree_conn_list) {
		tree_conn = list_entry(tmp, struct ksmbd_tree_connect, list);
		if (tree_conn->id == id)
			return tree_conn;
	}
	return NULL;
}

struct ksmbd_share_config *ksmbd_tree_conn_share(struct ksmbd_session *sess,
						 unsigned int id)
{
	struct ksmbd_tree_connect *tc;

	tc = ksmbd_tree_conn_lookup(sess, id);
	if (tc)
		return tc->share_conf;
	return NULL;
}

int ksmbd_tree_conn_session_logoff(struct ksmbd_session *sess)
{
	int ret = 0;

	while (!list_empty(&sess->tree_conn_list)) {
		struct ksmbd_tree_connect *tc;

		tc = list_entry(sess->tree_conn_list.next,
				struct ksmbd_tree_connect,
				list);
		ret |= ksmbd_tree_conn_disconnect(sess, tc);
	}

	return ret;
}
