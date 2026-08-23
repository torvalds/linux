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
#include "mgmt/user_session.h"
#include "connection.h"
#include "compress.h"
#include "transport_tcp.h"
#include "transport_rdma.h"
#include "misc.h"

static DEFINE_MUTEX(init_lock);

static struct ksmbd_conn_ops default_conn_ops;

DEFINE_HASHTABLE(conn_list, CONN_HASH_BITS);
DECLARE_RWSEM(conn_list_lock);

#ifdef CONFIG_PROC_FS
static struct proc_dir_entry *proc_clients;

static const char *ksmbd_conn_state_string(struct ksmbd_conn *conn)
{
	switch (READ_ONCE(conn->status)) {
	case KSMBD_SESS_NEW:
		return "new";
	case KSMBD_SESS_GOOD:
		return "good";
	case KSMBD_SESS_EXITING:
		return "exiting";
	case KSMBD_SESS_NEED_RECONNECT:
		return "reconnect";
	case KSMBD_SESS_NEED_NEGOTIATE:
		return "negotiate";
	case KSMBD_SESS_NEED_SETUP:
		return "setup";
	case KSMBD_SESS_RELEASING:
		return "releasing";
	default:
		return "unknown";
	}
}

static const char *ksmbd_conn_transport_string(struct ksmbd_conn *conn)
{
	if (conn->transport->ops->rdma_read || conn->transport->ops->rdma_write)
		return "smbdirect";
	return "tcp";
}

static void proc_show_conn_feature(struct seq_file *m, bool *separator,
				   bool enabled, const char *name)
{
	if (!enabled)
		return;
	seq_printf(m, "%s%s", *separator ? "," : "", name);
	*separator = true;
}

static void proc_show_conn_features(struct seq_file *m,
				    struct ksmbd_conn *conn)
{
	bool separator = false;

	proc_show_conn_feature(m, &separator,
			       conn->sign || conn->signing_negotiated, "sign");
	proc_show_conn_feature(m, &separator, conn->cipher_type, "encrypt");
	proc_show_conn_feature(m, &separator,
			       conn->compress_algorithm != SMB3_COMPRESS_NONE,
			       "compress");
	proc_show_conn_feature(m, &separator, conn->rdma_transform_ids,
			       "rdma-transform");
	proc_show_conn_feature(m, &separator, conn->posix_ext_supported, "posix");
	if (!separator)
		seq_puts(m, "none");
}

static int proc_show_clients(struct seq_file *m, void *v)
{
	struct ksmbd_conn *conn;
	struct timespec64 now, t;
	int i;

	down_read(&conn_list_lock);
	hash_for_each(conn_list, i, conn, hlist) {
		unsigned int outstanding_credits, total_credits;
		unsigned long id;
		void *entry;
		unsigned int sessions = 0;

		jiffies_to_timespec64(jiffies - conn->last_active, &t);
		ktime_get_real_ts64(&now);
		t = timespec64_sub(now, t);

		spin_lock(&conn->credits_lock);
		outstanding_credits = conn->outstanding_credits;
		total_credits = conn->total_credits;
		spin_unlock(&conn->credits_lock);

		rcu_read_lock();
		xa_for_each(&conn->sessions, id, entry)
			sessions++;
		rcu_read_unlock();
#if IS_ENABLED(CONFIG_IPV6)
		if (!conn->inet_addr)
			seq_printf(m, "client:\t%pI6c\n", &conn->inet6_addr);
		else
#endif
			seq_printf(m, "client:\t%pI4\n", &conn->inet_addr);
		seq_printf(m, "transport:\t%s\n", ksmbd_conn_transport_string(conn));
		seq_printf(m, "state:\t%s\n", ksmbd_conn_state_string(conn));
		seq_printf(m, "dialect:\t0x%04x\n", conn->dialect);
		seq_printf(m, "credits:\t%u/%u\n", outstanding_credits,
			   total_credits);
		seq_printf(m, "sessions:\t%u\n", sessions);
		seq_printf(m, "open_files:\t%d\n",
			   atomic_read(&conn->stats.open_files_count));
		seq_printf(m, "requests:\t%lld\n",
			   atomic64_read(&conn->stats.request_served));
		seq_puts(m, "features:\t");
		proc_show_conn_features(m, conn);
		seq_printf(m, "\nlast_active:\t%ptT\n\n", &t);
	}
	up_read(&conn_list_lock);
	return 0;
}

static int create_proc_clients(void)
{
	proc_clients = ksmbd_proc_create("clients",
					 proc_show_clients, NULL);
	if (!proc_clients)
		return -ENOMEM;
	return 0;
}

static void delete_proc_clients(void)
{
	if (proc_clients) {
		proc_remove(proc_clients);
		proc_clients = NULL;
	}
}
#else
static int create_proc_clients(void) { return 0; }
static void delete_proc_clients(void) {}
#endif

static struct workqueue_struct *ksmbd_conn_wq;

int ksmbd_conn_wq_init(void)
{
	ksmbd_conn_wq = alloc_workqueue("ksmbd-conn-release",
					WQ_UNBOUND | WQ_MEM_RECLAIM, 0);
	if (!ksmbd_conn_wq)
		return -ENOMEM;
	return 0;
}

void ksmbd_conn_wq_destroy(void)
{
	if (ksmbd_conn_wq) {
		destroy_workqueue(ksmbd_conn_wq);
		ksmbd_conn_wq = NULL;
	}
}

/*
 * __ksmbd_conn_release_work() - perform the final, once-per-struct cleanup
 * of a ksmbd_conn whose refcount has just dropped to zero.
 *
 * This is the common release path used by ksmbd_conn_put() for the embedded
 * state that outlives the connection thread: async_ida and the attached
 * transport (which owns the socket and iov for TCP).  Called from a workqueue
 * so that sleep-allowed teardown (sock_release -> tcp_close ->
 * lock_sock_nested) never runs from an RCU softirq callback (free_opinfo_rcu)
 * or any other non-sleeping putter context.
 */
static void __ksmbd_conn_release_work(struct work_struct *work)
{
	struct ksmbd_conn *conn =
		container_of(work, struct ksmbd_conn, release_work);

	ida_destroy(&conn->async_ida);
	conn->transport->ops->free_transport(conn->transport);
	kfree_sensitive(conn);
}

/**
 * ksmbd_conn_get() - take a reference on @conn and return it.
 *
 * @conn: connection instance to get a reference to
 *
 * Returns @conn unchanged so callers can write
 * "fp->conn = ksmbd_conn_get(work->conn);" in one expression.  Returns NULL
 * if @conn is NULL.
 */
struct ksmbd_conn *ksmbd_conn_get(struct ksmbd_conn *conn)
{
	if (!conn)
		return NULL;

	atomic_inc(&conn->refcnt);
	return conn;
}

/**
 * ksmbd_conn_put() - drop a reference and, if it was the last, queue the
 * release onto ksmbd_conn_wq so it runs from process context.
 *
 * @conn: connection instance to put a reference to
 *
 * Callable from any context including RCU softirq callbacks and non-sleeping
 * locks; the actual release is deferred to the workqueue.  ksmbd_conn_wq is
 * created in ksmbd_server_init() before any conn can be allocated and is
 * destroyed in ksmbd_server_exit() after rcu_barrier(), so it is always
 * non-NULL while a conn reference is held.
 */
void ksmbd_conn_put(struct ksmbd_conn *conn)
{
	if (!conn)
		return;

	if (atomic_dec_and_test(&conn->refcnt))
		queue_work(ksmbd_conn_wq, &conn->release_work);
}

/**
 * ksmbd_conn_free() - free resources of the connection instance
 *
 * @conn:	connection instance to be cleaned up
 *
 * During the thread termination, the corresponding conn instance
 * resources(sock/memory) are released and finally the conn object is freed.
 */
void ksmbd_conn_free(struct ksmbd_conn *conn)
{
	down_write(&conn_list_lock);
	hash_del(&conn->hlist);
	up_write(&conn_list_lock);

	/*
	 * request_buf / preauth_info / mechToken are only ever accessed by the
	 * connection handler thread that owns @conn.  ksmbd_conn_free() is
	 * called from the transport free_transport() path when that thread is
	 * exiting, so it is safe to release them unconditionally even when
	 * ksmbd_conn_put() below is not the final putter (oplock / ksmbd_file
	 * holders only retain the conn pointer, not these per-thread buffers).
	 */
	xa_destroy(&conn->sessions);
	kvfree(conn->request_buf);
	kfree_sensitive(conn->preauth_info);
	kfree(conn->mechToken);
	ksmbd_preauth_session_destroy(conn);
	ksmbd_conn_put(conn);
}

/**
 * ksmbd_conn_alloc() - initialize a new connection instance
 *
 * Return:	ksmbd_conn struct on success, otherwise NULL
 */
struct ksmbd_conn *ksmbd_conn_alloc(void)
{
	struct ksmbd_conn *conn;

	conn = kzalloc_obj(struct ksmbd_conn, KSMBD_DEFAULT_GFP);
	if (!conn)
		return NULL;

	conn->need_neg = true;
	ksmbd_conn_set_new(conn);
	conn->local_nls = load_nls("utf8");
	if (!conn->local_nls)
		conn->local_nls = load_nls_default();
	if (IS_ENABLED(CONFIG_UNICODE))
		conn->um = utf8_load(UNICODE_AGE(12, 1, 0));
	else
		conn->um = ERR_PTR(-EOPNOTSUPP);
	if (IS_ERR(conn->um))
		conn->um = NULL;
	INIT_WORK(&conn->release_work, __ksmbd_conn_release_work);
	atomic_set(&conn->req_running, 0);
	atomic_set(&conn->r_count, 0);
	atomic_set(&conn->refcnt, 1);
	conn->total_credits = 1;
	conn->outstanding_credits = 0;

	/*
	 * The command sequence window starts as the set { 0 } when the
	 * connection is established.
	 */
	conn->seq_low = 0;
	conn->seq_high = 1;
	__set_bit(0, conn->seq_bitmap);

	init_waitqueue_head(&conn->req_running_q);
	init_waitqueue_head(&conn->r_count_q);
	INIT_LIST_HEAD(&conn->requests);
	INIT_LIST_HEAD(&conn->async_requests);
	INIT_LIST_HEAD(&conn->preauth_sess_table);
	spin_lock_init(&conn->request_lock);
	spin_lock_init(&conn->credits_lock);
	ida_init(&conn->async_ida);
	xa_init(&conn->sessions);

	spin_lock_init(&conn->llist_lock);
	INIT_LIST_HEAD(&conn->lock_list);

	init_rwsem(&conn->session_lock);

	return conn;
}

bool ksmbd_conn_lookup_dialect(struct ksmbd_conn *c)
{
	struct ksmbd_conn *t;
	int bkt;
	bool ret = false;

	down_read(&conn_list_lock);
	hash_for_each(conn_list, bkt, t, hlist) {
		if (memcmp(t->ClientGUID, c->ClientGUID, SMB2_CLIENT_GUID_SIZE))
			continue;

		ret = true;
		break;
	}
	up_read(&conn_list_lock);
	return ret;
}

void ksmbd_conn_enqueue_request(struct ksmbd_work *work)
{
	struct ksmbd_conn *conn = work->conn;
	struct list_head *requests_queue = NULL;

	if (conn->ops->get_cmd_val(work) != SMB2_CANCEL_HE)
		requests_queue = &conn->requests;

	atomic_inc(&conn->req_running);
	if (requests_queue) {
		spin_lock(&conn->request_lock);
		list_add_tail(&work->request_entry, requests_queue);
		spin_unlock(&conn->request_lock);
	}
}

void ksmbd_conn_try_dequeue_request(struct ksmbd_work *work)
{
	struct ksmbd_conn *conn = work->conn;

	atomic_dec(&conn->req_running);
	if (waitqueue_active(&conn->req_running_q))
		wake_up(&conn->req_running_q);

	if (list_empty(&work->request_entry) &&
	    list_empty(&work->async_request_entry))
		return;

	spin_lock(&conn->request_lock);
	list_del_init(&work->request_entry);
	spin_unlock(&conn->request_lock);
	if (work->asynchronous)
		release_async_work(work);

	wake_up_all(&conn->req_running_q);
}

static void ksmbd_conn_cancel_async_requests(struct ksmbd_conn *conn)
{
	struct ksmbd_work *work, *tmp;

	ksmbd_debug(CONN, "Cancel pending async requests on releasing connection\n");
	spin_lock(&conn->request_lock);
	list_for_each_entry_safe(work, tmp, &conn->async_requests,
				 async_request_entry) {
		if (work->state != KSMBD_WORK_ACTIVE)
			continue;

		ksmbd_debug(CONN, "Cancel async request id %d\n",
			    work->async_id);
		work->state = KSMBD_WORK_CANCELLED;
		if (work->cancel_fn)
			work->cancel_fn(work->cancel_argv);
	}
	spin_unlock(&conn->request_lock);
}

void ksmbd_conn_lock(struct ksmbd_conn *conn)
{
	mutex_lock(&conn->srv_mutex);
}

void ksmbd_conn_unlock(struct ksmbd_conn *conn)
{
	mutex_unlock(&conn->srv_mutex);
}

static bool ksmbd_session_is_bound_to_conn(struct ksmbd_session *sess,
					   struct ksmbd_conn *conn)
{
	bool found;

	rcu_read_lock();
	found = xa_load(&conn->sessions, sess->id) == sess;
	rcu_read_unlock();
	if (found)
		return true;

	down_read(&sess->chann_lock);
	found = xa_load(&sess->ksmbd_chann_list, (long)conn);
	up_read(&sess->chann_lock);
	return found;
}

void ksmbd_all_conn_set_status(struct ksmbd_session *sess, u32 status)
{
	struct ksmbd_conn *conn;
	int bkt;

	down_read(&conn_list_lock);
	hash_for_each(conn_list, bkt, conn, hlist) {
		if (ksmbd_session_is_bound_to_conn(sess, conn)) {
			spin_lock(&conn->request_lock);
			if (!ksmbd_conn_exiting(conn) &&
			    !ksmbd_conn_releasing(conn))
				WRITE_ONCE(conn->status, status);
			spin_unlock(&conn->request_lock);
		}
	}
	up_read(&conn_list_lock);
}

void ksmbd_conn_abort(struct ksmbd_conn *conn)
{
	bool shutdown = false;

	spin_lock(&conn->request_lock);
	if (!ksmbd_conn_exiting(conn) && !ksmbd_conn_releasing(conn)) {
		ksmbd_conn_set_exiting(conn);
		shutdown = true;
	}
	spin_unlock(&conn->request_lock);
	wake_up_all(&conn->req_running_q);

	if (shutdown && conn->transport->ops->shutdown)
		conn->transport->ops->shutdown(conn->transport);
}

void ksmbd_conn_wait_idle(struct ksmbd_conn *conn)
{
	wait_event(conn->req_running_q, atomic_read(&conn->req_running) < 2);
}

int ksmbd_conn_wait_idle_sess(struct ksmbd_conn *curr_conn,
			      struct ksmbd_session *sess)
{
	struct ksmbd_conn *conn;
	int rc, retry_count = 0, max_timeout = 120;
	int rcount, bkt;

retry_idle:
	if (retry_count >= max_timeout)
		return -EIO;

	down_read(&conn_list_lock);
	hash_for_each(conn_list, bkt, conn, hlist) {
		if (ksmbd_session_is_bound_to_conn(sess, conn)) {
			rcount = (conn == curr_conn) ? 2 : 1;
			if (atomic_read(&conn->req_running) >= rcount) {
				rc = wait_event_timeout(conn->req_running_q,
					atomic_read(&conn->req_running) < rcount,
					HZ);
				if (!rc) {
					up_read(&conn_list_lock);
					retry_count++;
					goto retry_idle;
				}
			}
		}
	}
	up_read(&conn_list_lock);

	return 0;
}

static int __ksmbd_conn_write(struct ksmbd_work *work,
			      struct ksmbd_transport_write *tx)
{
	struct ksmbd_conn *conn = work->conn;
	int sent;

	if (!work->response_buf) {
		pr_err("NULL response header\n");
		return -EINVAL;
	}

	if (work->send_no_response)
		return 0;

	if (!work->iov_idx)
		return -EINVAL;

	tx->iov = work->iov;
	tx->iov_cnt = work->iov_cnt;
	tx->size = get_rfc1002_len(work->iov[0].iov_base) + 4;
	tx->need_invalidate_rkey = work->need_invalidate_rkey;
	tx->remote_key = work->remote_key;

	ksmbd_conn_lock(conn);
	sent = conn->transport->ops->writev(conn->transport, tx);
	ksmbd_conn_unlock(conn);

	if (sent < 0) {
		pr_err("Failed to send message: %d\n", sent);
		return sent;
	}

	return 0;
}

int ksmbd_conn_write(struct ksmbd_work *work)
{
	struct ksmbd_transport_write tx = {};

	return __ksmbd_conn_write(work, &tx);
}

int ksmbd_conn_write_eor(struct ksmbd_work *work)
{
	struct ksmbd_transport_write tx = {
		.msg_flags = MSG_EOR,
	};

	return __ksmbd_conn_write(work, &tx);
}

int ksmbd_conn_rdma_read(struct ksmbd_conn *conn,
			 void *buf, unsigned int buflen,
			 struct smbdirect_buffer_descriptor_v1 *desc,
			 unsigned int desc_len)
{
	int ret = -EINVAL;

	if (conn->transport->ops->rdma_read)
		ret = conn->transport->ops->rdma_read(conn->transport,
						      buf, buflen,
						      desc, desc_len);
	return ret;
}

int ksmbd_conn_rdma_write(struct ksmbd_conn *conn,
			  void *buf, unsigned int buflen,
			  struct smbdirect_buffer_descriptor_v1 *desc,
			  unsigned int desc_len)
{
	int ret = -EINVAL;

	if (conn->transport->ops->rdma_write)
		ret = conn->transport->ops->rdma_write(conn->transport,
						       buf, buflen,
						       desc, desc_len);
	return ret;
}

bool ksmbd_conn_alive(struct ksmbd_conn *conn)
{
	if (!ksmbd_server_running())
		return false;

	if (ksmbd_conn_exiting(conn))
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

/* "+2" for BCC field (ByteCount, 2 bytes) */
#define SMB1_MIN_SUPPORTED_PDU_SIZE (sizeof(struct smb_hdr) + 2)
#define SMB2_MIN_SUPPORTED_PDU_SIZE (sizeof(struct smb2_pdu))
#define SMB2_TRANSFORM_MIN_SUPPORTED_PDU_SIZE	\
	(sizeof(struct smb2_transform_hdr) + sizeof(struct smb2_hdr))

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
	unsigned int pdu_size, max_allowed_pdu_size, max_req;
	__le32 proto;
	char hdr_buf[4] = {0,};
	int size;

	mutex_init(&conn->srv_mutex);
	__module_get(THIS_MODULE);

	max_req = server_conf.max_inflight_req;
	conn->last_active = jiffies;
	set_freezable();
	while (ksmbd_conn_alive(conn)) {
		if (try_to_freeze())
			continue;

		kvfree(conn->request_buf);
		conn->request_buf = NULL;

recheck:
		if (atomic_read(&conn->req_running) + 1 > max_req) {
			wait_event_interruptible(conn->req_running_q,
				atomic_read(&conn->req_running) < max_req);
			goto recheck;
		}

		size = t->ops->read(t, hdr_buf, sizeof(hdr_buf), -1);
		if (size != sizeof(hdr_buf))
			break;

		pdu_size = get_rfc1002_len(hdr_buf);
		ksmbd_debug(CONN, "RFC1002 header %u bytes\n", pdu_size);

		max_allowed_pdu_size = ksmbd_max_allowed_pdu_size(conn);

		if (pdu_size > max_allowed_pdu_size) {
			pr_err_ratelimited("PDU length(%u) exceeded maximum allowed pdu size(%u) on connection(%d)\n",
					pdu_size, max_allowed_pdu_size,
					READ_ONCE(conn->status));
			break;
		}

		/*
		 * Check maximum pdu size(0x00FFFFFF).
		 */
		if (pdu_size > MAX_STREAM_PROT_LEN)
			break;

		if (pdu_size < SMB1_MIN_SUPPORTED_PDU_SIZE)
			break;

		/* 4 for rfc1002 length field */
		/* 1 for implied bcc[0] */
		size = pdu_size + 4 + 1;
		conn->request_buf = kvmalloc(size, KSMBD_DEFAULT_GFP);
		if (!conn->request_buf)
			break;

		memcpy(conn->request_buf, hdr_buf, sizeof(hdr_buf));

		/*
		 * We already read 4 bytes to find out PDU size, now
		 * read in PDU
		 */
		size = t->ops->read(t, conn->request_buf + 4, pdu_size, 2);
		if (size < 0) {
			pr_err("sock_read failed: %d\n", size);
			break;
		}

		if (size != pdu_size) {
			pr_err("PDU error. Read: %d, Expected: %d\n",
			       size, pdu_size);
			continue;
		}

		if (((struct smb2_hdr *)smb_get_msg(conn->request_buf))->ProtocolId ==
		    SMB2_COMPRESSION_TRANSFORM_ID) {
			/*
			 * Convert the transform into a normal RFC1002-framed SMB2
			 * request before protocol validation and work allocation.
			 */
			if (ksmbd_decompress_request(conn))
				break;
			pdu_size = get_rfc1002_len(conn->request_buf);
		}

		if (!ksmbd_smb_request(conn))
			break;

		proto = *(__le32 *)smb_get_msg(conn->request_buf);
		if (proto == SMB2_PROTO_NUMBER &&
		    pdu_size < SMB2_MIN_SUPPORTED_PDU_SIZE)
			break;

		if (proto == SMB2_TRANSFORM_PROTO_NUM &&
		    pdu_size < SMB2_TRANSFORM_MIN_SUPPORTED_PDU_SIZE)
			break;

		if (!default_conn_ops.process_fn) {
			pr_err("No connection request callback\n");
			break;
		}

		if (default_conn_ops.process_fn(conn)) {
			pr_err("Cannot handle request\n");
			break;
		}
	}

	ksmbd_conn_set_releasing(conn);
	ksmbd_conn_cancel_async_requests(conn);
	/* Wait till all reference dropped to the Server object*/
	ksmbd_debug(CONN, "Wait for all pending requests(%d)\n", atomic_read(&conn->r_count));
	wait_event(conn->r_count_q, atomic_read(&conn->r_count) == 0);

	if (IS_ENABLED(CONFIG_UNICODE))
		utf8_unload(conn->um);
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

void ksmbd_conn_r_count_inc(struct ksmbd_conn *conn)
{
	atomic_inc(&conn->r_count);
}

void ksmbd_conn_r_count_dec(struct ksmbd_conn *conn)
{
	/*
	 * Checking waitqueue to dropping pending requests on
	 * disconnection. waitqueue_active is safe because it
	 * uses atomic operation for condition.
	 */
	atomic_inc(&conn->refcnt);
	if (!atomic_dec_return(&conn->r_count) && waitqueue_active(&conn->r_count_q))
		wake_up(&conn->r_count_q);

	ksmbd_conn_put(conn);
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
	if (create_proc_clients())
		pr_warn("Unable to create clients procfs entry\n");
	return ret;
}

static void stop_sessions(void)
{
	struct ksmbd_conn *conn, *target;
	struct ksmbd_transport *t;
	bool any;
	int bkt;

	/*
	 * Serialised via init_lock; no concurrent stop_sessions() can
	 * touch conn->stop_called, so writing it under the read lock is
	 * safe.
	 */
again:
	target = NULL;
	any = false;
	down_read(&conn_list_lock);
	hash_for_each(conn_list, bkt, conn, hlist) {
		any = true;
		if (conn->stop_called)
			continue;
		atomic_inc(&conn->refcnt);
		conn->stop_called = true;
		/*
		 * Mark the connection EXITING while still holding the
		 * read lock so the selection and the status transition
		 * happen together.  Do not regress a connection that has
		 * already advanced to RELEASING on its own (e.g. the
		 * handler exited its receive loop for an unrelated
		 * reason).
		 */
		spin_lock(&conn->request_lock);
		if (!ksmbd_conn_releasing(conn))
			ksmbd_conn_set_exiting(conn);
		spin_unlock(&conn->request_lock);
		target = conn;
		break;
	}
	up_read(&conn_list_lock);

	if (target) {
		t = target->transport;
		if (t->ops->shutdown)
			t->ops->shutdown(t);
		if (atomic_dec_and_test(&target->refcnt)) {
			ida_destroy(&target->async_ida);
			t->ops->free_transport(t);
			kfree_sensitive(target);
		}
		goto again;
	}

	if (any) {
		msleep(100);
		goto again;
	}
}

void ksmbd_conn_transport_destroy(void)
{
	delete_proc_clients();
	mutex_lock(&init_lock);
	ksmbd_tcp_destroy();
	ksmbd_rdma_stop_listening();
	stop_sessions();
	mutex_unlock(&init_lock);
}
