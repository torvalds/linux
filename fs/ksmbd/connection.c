// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) 2016 Namjae Jeon <namjae.jeon@protocolfreedom.org>
 *   Copyright (C) 2018 Samsung Electronics Co., Ltd.
 */

#include <linux/mutex.h>
#include <linux/freezer.h>
#include <linux/module.h>

#include "server.h"
#include "smb_common.h"
#include "mgmt/ksmbd_ida.h"
#include "connection.h"
#include "transport_tcp.h"
#include "transport_rdma.h"

static DEFINE_MUTEX(init_lock);

static struct ksmbd_conn_ops default_conn_ops;

LIST_HEAD(conn_list);
DEFINE_RWLOCK(conn_list_lock);

/**
 * ksmbd_conn_free() - free resources of the connection instance
 *
 * @conn:	connection instance to be cleand up
 *
 * During the thread termination, the corresponding conn instance
 * resources(sock/memory) are released and finally the conn object is freed.
 */
void ksmbd_conn_free(struct ksmbd_conn *conn)
{
	write_lock(&conn_list_lock);
	list_del(&conn->conns_list);
	write_unlock(&conn_list_lock);

	kvfree(conn->request_buf);
	kfree(conn->preauth_info);
	kfree(conn);
}

/**
 * ksmbd_conn_alloc() - initialize a new connection instance
 *
 * Return:	ksmbd_conn struct on success, otherwise NULL
 */
struct ksmbd_conn *ksmbd_conn_alloc(void)
{
	struct ksmbd_conn *conn;

	conn = kzalloc(sizeof(struct ksmbd_conn), GFP_KERNEL);
	if (!conn)
		return NULL;

	conn->need_neg = true;
	conn->status = KSMBD_SESS_NEW;
	conn->local_nls = load_nls("utf8");
	if (!conn->local_nls)
		conn->local_nls = load_nls_default();
	atomic_set(&conn->req_running, 0);
	atomic_set(&conn->r_count, 0);
	conn->total_credits = 1;

	init_waitqueue_head(&conn->req_running_q);
	INIT_LIST_HEAD(&conn->conns_list);
	INIT_LIST_HEAD(&conn->sessions);
	INIT_LIST_HEAD(&conn->requests);
	INIT_LIST_HEAD(&conn->async_requests);
	spin_lock_init(&conn->request_lock);
	spin_lock_init(&conn->credits_lock);
	ida_init(&conn->async_ida);

	spin_lock_init(&conn->llist_lock);
	INIT_LIST_HEAD(&conn->lock_list);

	write_lock(&conn_list_lock);
	list_add(&conn->conns_list, &conn_list);
	write_unlock(&conn_list_lock);
	return conn;
}

bool ksmbd_conn_lookup_dialect(struct ksmbd_conn *c)
{
	struct ksmbd_conn *t;
	bool ret = false;

	read_lock(&conn_list_lock);
	list_for_each_entry(t, &conn_list, conns_list) {
		if (memcmp(t->ClientGUID, c->ClientGUID, SMB2_CLIENT_GUID_SIZE))
			continue;

		ret = true;
		break;
	}
	read_unlock(&conn_list_lock);
	return ret;
}

void ksmbd_conn_enqueue_request(struct ksmbd_work *work)
{
	struct ksmbd_conn *conn = work->conn;
	struct list_head *requests_queue = NULL;

	if (conn->ops->get_cmd_val(work) != SMB2_CANCEL_HE) {
		requests_queue = &conn->requests;
		work->syncronous = true;
	}

	if (requests_queue) {
		atomic_inc(&conn->req_running);
		spin_lock(&conn->request_lock);
		list_add_tail(&work->request_entry, requests_queue);
		spin_unlock(&conn->request_lock);
	}
}

int ksmbd_conn_try_dequeue_request(struct ksmbd_work *work)
{
	struct ksmbd_conn *conn = work->conn;
	int ret = 1;

	if (list_empty(&work->request_entry) &&
	    list_empty(&work->async_request_entry))
		return 0;

	if (!work->multiRsp)
		atomic_dec(&conn->req_running);
	spin_lock(&conn->request_lock);
	if (!work->multiRsp) {
		list_del_init(&work->request_entry);
		if (work->syncronous == false)
			list_del_init(&work->async_request_entry);
		ret = 0;
	}
	spin_unlock(&conn->request_lock);

	wake_up_all(&conn->req_running_q);
	return ret;
}

static void ksmbd_conn_lock(struct ksmbd_conn *conn)
{
	mutex_lock(&conn->srv_mutex);
}

static void ksmbd_conn_unlock(struct ksmbd_conn *conn)
{
	mutex_unlock(&conn->srv_mutex);
}

void ksmbd_conn_wait_idle(struct ksmbd_conn *conn)
{
	wait_event(conn->req_running_q, atomic_read(&conn->req_running) < 2);
}

int ksmbd_conn_write(struct ksmbd_work *work)
{
	struct ksmbd_conn *conn = work->conn;
	size_t len = 0;
	int sent;
	struct kvec iov[3];
	int iov_idx = 0;

	ksmbd_conn_try_dequeue_request(work);
	if (!work->response_buf) {
		pr_err("NULL response header\n");
		return -EINVAL;
	}

	if (work->tr_buf) {
		iov[iov_idx] = (struct kvec) { work->tr_buf,
				sizeof(struct smb2_transform_hdr) + 4 };
		len += iov[iov_idx++].iov_len;
	}

	if (work->aux_payload_sz) {
		iov[iov_idx] = (struct kvec) { work->response_buf, work->resp_hdr_sz };
		len += iov[iov_idx++].iov_len;
		iov[iov_idx] = (struct kvec) { work->aux_payload_buf, work->aux_payload_sz };
		len += iov[iov_idx++].iov_len;
	} else {
		if (work->tr_buf)
			iov[iov_idx].iov_len = work->resp_hdr_sz;
		else
			iov[iov_idx].iov_len = get_rfc1002_len(work->response_buf) + 4;
		iov[iov_idx].iov_base = work->response_buf;
		len += iov[iov_idx++].iov_len;
	}

	ksmbd_conn_lock(conn);
	sent = conn->transport->ops->writev(conn->transport, &iov[0],
					iov_idx, len,
					work->need_invalidate_rkey,
					work->remote_key);
	ksmbd_conn_unlock(conn);

	if (sent < 0) {
		pr_err("Failed to send message: %d\n", sent);
		return sent;
	}

	return 0;
}

int ksmbd_conn_rdma_read(struct ksmbd_conn *conn, void *buf,
			 unsigned int buflen, u32 remote_key, u64 remote_offset,
			 u32 remote_len)
{
	int ret = -EINVAL;

	if (conn->transport->ops->rdma_read)
		ret = conn->transport->ops->rdma_read(conn->transport,
						      buf, buflen,
						      remote_key, remote_offset,
						      remote_len);
	return ret;
}

int ksmbd_conn_rdma_write(struct ksmbd_conn *conn, void *buf,
			  unsigned int buflen, u32 remote_key,
			  u64 remote_offset, u32 remote_len)
{
	int ret = -EINVAL;

	if (conn->transport->ops->rdma_write)
		ret = conn->transport->ops->rdma_write(conn->transport,
						       buf, buflen,
						       remote_key, remote_offset,
						       remote_len);
	return ret;
}

bool ksmbd_conn_alive(struct ksmbd_conn *conn)
{
	if (!ksmbd_server_running())
		return false;

	if (conn->status == KSMBD_SESS_EXITING)
		return false;

	if (kthread_should_stop())
		return false;

	if (atomic_read(&conn->stats.open_files_count) > 0)
		return true;

	/*
	 * Stop current session if the time that get last request from client
	 * is bigger than deadtime user configured and opening file count is
	 * zero.
	 */
	if (server_conf.deadtime > 0 &&
	    time_after(jiffies, conn->last_active + server_conf.deadtime)) {
		ksmbd_debug(CONN, "No response from client in %lu minutes\n",
			    server_conf.deadtime / SMB_ECHO_INTERVAL);
		return false;
	}
	return true;
}

/**
 * ksmbd_conn_handler_loop() - session thread to listen on new smb requests
 * @p:		connection instance
 *
 * One thread each per connection
 *
 * Return:	0 on success
 */
int ksmbd_conn_handler_loop(void *p)
{
	struct ksmbd_conn *conn = (struct ksmbd_conn *)p;
	struct ksmbd_transport *t = conn->transport;
	unsigned int pdu_size;
	char hdr_buf[4] = {0,};
	int size;

	mutex_init(&conn->srv_mutex);
	__module_get(THIS_MODULE);

	if (t->ops->prepare && t->ops->prepare(t))
		goto out;

	conn->last_active = jiffies;
	while (ksmbd_conn_alive(conn)) {
		if (try_to_freeze())
			continue;

		kvfree(conn->request_buf);
		conn->request_buf = NULL;

		size = t->ops->read(t, hdr_buf, sizeof(hdr_buf));
		if (size != sizeof(hdr_buf))
			break;

		pdu_size = get_rfc1002_len(hdr_buf);
		ksmbd_debug(CONN, "RFC1002 header %u bytes\n", pdu_size);

		/*
		 * Check if pdu size is valid (min : smb header size,
		 * max : 0x00FFFFFF).
		 */
		if (pdu_size < __SMB2_HEADER_STRUCTURE_SIZE ||
		    pdu_size > MAX_STREAM_PROT_LEN) {
			continue;
		}

		/* 4 for rfc1002 length field */
		size = pdu_size + 4;
		conn->request_buf = kvmalloc(size, GFP_KERNEL);
		if (!conn->request_buf)
			continue;

		memcpy(conn->request_buf, hdr_buf, sizeof(hdr_buf));
		if (!ksmbd_smb_request(conn))
			break;

		/*
		 * We already read 4 bytes to find out PDU size, now
		 * read in PDU
		 */
		size = t->ops->read(t, conn->request_buf + 4, pdu_size);
		if (size < 0) {
			pr_err("sock_read failed: %d\n", size);
			break;
		}

		if (size != pdu_size) {
			pr_err("PDU error. Read: %d, Expected: %d\n",
			       size, pdu_size);
			continue;
		}

		if (!default_conn_ops.process_fn) {
			pr_err("No connection request callback\n");
			break;
		}

		if (default_conn_ops.process_fn(conn)) {
			pr_err("Cannot handle request\n");
			break;
		}
	}

out:
	/* Wait till all reference dropped to the Server object*/
	while (atomic_read(&conn->r_count) > 0)
		schedule_timeout(HZ);

	unload_nls(conn->local_nls);
	if (default_conn_ops.terminate_fn)
		default_conn_ops.terminate_fn(conn);
	t->ops->disconnect(t);
	module_put(THIS_MODULE);
	return 0;
}

void ksmbd_conn_init_server_callbacks(struct ksmbd_conn_ops *ops)
{
	default_conn_ops.process_fn = ops->process_fn;
	default_conn_ops.terminate_fn = ops->terminate_fn;
}

int ksmbd_conn_transport_init(void)
{
	int ret;

	mutex_lock(&init_lock);
	ret = ksmbd_tcp_init();
	if (ret) {
		pr_err("Failed to init TCP subsystem: %d\n", ret);
		goto out;
	}

	ret = ksmbd_rdma_init();
	if (ret) {
		pr_err("Failed to init RDMA subsystem: %d\n", ret);
		goto out;
	}
out:
	mutex_unlock(&init_lock);
	return ret;
}

static void stop_sessions(void)
{
	struct ksmbd_conn *conn;

again:
	read_lock(&conn_list_lock);
	list_for_each_entry(conn, &conn_list, conns_list) {
		struct task_struct *task;

		task = conn->transport->handler;
		if (task)
			ksmbd_debug(CONN, "Stop session handler %s/%d\n",
				    task->comm, task_pid_nr(task));
		conn->status = KSMBD_SESS_EXITING;
	}
	read_unlock(&conn_list_lock);

	if (!list_empty(&conn_list)) {
		schedule_timeout_interruptible(HZ / 10); /* 100ms */
		goto again;
	}
}

void ksmbd_conn_transport_destroy(void)
{
	mutex_lock(&init_lock);
	ksmbd_tcp_destroy();
	ksmbd_rdma_destroy();
	stop_sessions();
	mutex_unlock(&init_lock);
}
