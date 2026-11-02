// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) 2018 Samsung Electronics Co., Ltd.
 */

#include <linux/atomic.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/rwsem.h>
#include <linux/xarray.h>

#include "ksmbd_ida.h"
#include "user_session.h"
#include "user_config.h"
#include "tree_connect.h"
#include "share_config.h"
#include "../transport_ipc.h"
#include "../connection.h"
#include "../vfs_cache.h"
#include "../misc.h"
#include "../stats.h"
#include "../server.h"

static DEFINE_IDA(session_ida);

#define SESSION_HASH_BITS		12
#define KSMBD_MAX_PENDING_SESSIONS	1
static DEFINE_HASHTABLE(sessions_table, SESSION_HASH_BITS);
static DECLARE_RWSEM(sessions_table_lock);

int ksmbd_session_reserve_slot(struct ksmbd_conn *conn)
{
	if (atomic64_inc_return(&conn->session_count) >
	    READ_ONCE(server_conf.max_sessions_per_connection)) {
		atomic64_dec(&conn->session_count);
		return -ENOSPC;
	}
	return 0;
}

void ksmbd_session_release_slot(struct ksmbd_conn *conn)
{
	atomic64_dec(&conn->session_count);
}

#ifdef CONFIG_PROC_FS

static const struct ksmbd_const_name ksmbd_sess_cap_const_names[] = {
	{SMB2_GLOBAL_CAP_DFS, "dfs"},
	{SMB2_GLOBAL_CAP_LEASING, "lease"},
	{SMB2_GLOBAL_CAP_LARGE_MTU, "large-mtu"},
	{SMB2_GLOBAL_CAP_MULTI_CHANNEL, "multi-channel"},
	{SMB2_GLOBAL_CAP_PERSISTENT_HANDLES, "persistent-handles"},
	{SMB2_GLOBAL_CAP_DIRECTORY_LEASING, "dir-lease"},
	{SMB2_GLOBAL_CAP_ENCRYPTION, "encryption"}
};

static const struct ksmbd_const_name ksmbd_cipher_const_names[] = {
	{le16_to_cpu(SMB2_ENCRYPTION_AES128_CCM), "aes128-ccm"},
	{le16_to_cpu(SMB2_ENCRYPTION_AES128_GCM), "aes128-gcm"},
	{le16_to_cpu(SMB2_ENCRYPTION_AES256_CCM), "aes256-ccm"},
	{le16_to_cpu(SMB2_ENCRYPTION_AES256_GCM), "aes256-gcm"},
};

static const struct ksmbd_const_name ksmbd_signing_const_names[] = {
	{SIGNING_ALG_HMAC_SHA256, "hmac-sha256"},
	{SIGNING_ALG_AES_CMAC, "aes-cmac"},
	{SIGNING_ALG_AES_GMAC, "aes-gmac"},
};

static const char *session_state_string(struct ksmbd_session *session)
{
	switch (session->state) {
	case SMB2_SESSION_VALID:
		return "valid";
	case SMB2_SESSION_IN_PROGRESS:
		return "progress";
	case SMB2_SESSION_EXPIRED:
		return "expired";
	default:
		return "";
	}
}

static const char *session_user_name(struct ksmbd_session *session)
{
	if (user_guest(session->user))
		return "(Guest)";
	else if (ksmbd_anonymous_user(session->user))
		return "(Anonymous)";
	return session->user->name;
}

static const char *session_account_type(struct ksmbd_session *session)
{
	if (user_guest(session->user))
		return "guest";
	if (ksmbd_anonymous_user(session->user))
		return "anonymous";
	return "user";
}

static unsigned int session_open_file_count(struct ksmbd_session *session)
{
	struct ksmbd_file *fp;
	unsigned int count = 0;
	unsigned int id;

	read_lock(&session->file_table.lock);
	if (session->file_table.idr) {
		idr_for_each_entry(session->file_table.idr, fp, id)
			count++;
	}
	read_unlock(&session->file_table.lock);
	return count;
}

static int show_proc_session(struct seq_file *m, void *v)
{
	struct ksmbd_session *sess;
	struct ksmbd_tree_connect *tree_conn;
	struct ksmbd_share_config *share_conf;
	struct channel *chan;
	unsigned long id;
	int i = 0;

	sess = (struct ksmbd_session *)m->private;
	ksmbd_user_session_get(sess);

	seq_printf(m, "user:\t%s\n", session_user_name(sess));
	seq_printf(m, "account_type:\t%s\n",
		   session_account_type(sess));
	seq_printf(m, "id:\t%llu\n", sess->id);
	seq_printf(m, "state:\t%s\n", session_state_string(sess));
	seq_printf(m, "dialect:\t0x%04x\n", sess->dialect);
	seq_printf(m, "last_active_seconds:\t%lu\n",
		   jiffies_to_msecs(jiffies - sess->last_active) / MSEC_PER_SEC);
	seq_printf(m, "open_files:\t%u\n",
		   session_open_file_count(sess));

	i = 0;
	down_read(&sess->chann_lock);
	xa_for_each(&sess->ksmbd_chann_list, id, chan) {
		const char *name;

#if IS_ENABLED(CONFIG_IPV6)
		if (!chan->conn->is_ipv6)
			seq_printf(m, "client:\t%pI4\n",
					&chan->conn->inet_addr);
		else
			seq_printf(m, "client:\t%pI6c\n",
					&chan->conn->inet6_addr);
#else
		seq_printf(m, "client:\t%pI4\n",
				&chan->conn->inet_addr);
#endif
		seq_puts(m, "capabilities:\t");
		ksmbd_proc_show_flag_names(m,
				ksmbd_sess_cap_const_names,
				ARRAY_SIZE(ksmbd_sess_cap_const_names),
				chan->conn->vals->req_capabilities);
		seq_putc(m, '\n');
		seq_printf(m, "posix_extensions:\t%s\n",
			   chan->conn->posix_ext_supported ? "yes" : "no");

		if (sess->sign) {
			unsigned int algorithm =
				le16_to_cpu(chan->conn->signing_algorithm);

			name = ksmbd_proc_const_name(ksmbd_signing_const_names,
						     ARRAY_SIZE(ksmbd_signing_const_names),
						     algorithm);
			if (name)
				seq_printf(m, "signing:\t%s\n", name);
			else
				seq_printf(m, "signing:\t0x%04x\n",
					   algorithm);
		}
		if (sess->enc) {
			unsigned int cipher = le16_to_cpu(chan->conn->cipher_type);

			name = ksmbd_proc_const_name(ksmbd_cipher_const_names,
						     ARRAY_SIZE(ksmbd_cipher_const_names),
						     cipher);
			if (name)
				seq_printf(m, "encryption:\t%s\n", name);
			else
				seq_printf(m, "encryption:\t0x%04x\n",
					   cipher);
		}
		i++;
	}
	up_read(&sess->chann_lock);

	seq_printf(m, "channels:\t%d\n", i);

	i = 0;
	down_read(&sess->tree_conns_lock);
	xa_for_each(&sess->tree_conns, id, tree_conn) {
		share_conf = tree_conn->share_conf;
		seq_printf(m, "share:\t%s\n", share_conf->name);
		seq_printf(m, "tree_id:\t%d\n", tree_conn->id);
		seq_printf(m, "share_type:\t%s\n",
			   test_share_config_flag(share_conf, KSMBD_SHARE_FLAG_PIPE) ?
			   "pipe" : "disk");
		i++;
	}
	up_read(&sess->tree_conns_lock);
	seq_printf(m, "tree_connects:\t%d\n", i);

	ksmbd_user_session_put(sess);
	return 0;
}

static int create_proc_session(struct ksmbd_session *sess)
{
	char name[30];

	snprintf(name, sizeof(name), "sessions/%llu", sess->id);
	sess->proc_entry = ksmbd_proc_create(name,
					     show_proc_session, sess);
	if (!sess->proc_entry)
		return -ENOMEM;
	return 0;
}

static void delete_proc_session(struct ksmbd_session *sess)
{
	if (sess->proc_entry) {
		proc_remove(sess->proc_entry);
		sess->proc_entry = NULL;
	}
}

static int show_proc_sessions(struct seq_file *m, void *v)
{
	struct ksmbd_session *session;
	struct channel *chan;
	int i;
	unsigned long id;

	down_read(&sessions_table_lock);
	hash_for_each(sessions_table, i, session, hlist) {
		down_read(&session->chann_lock);
		xa_for_each(&session->ksmbd_chann_list, id, chan) {
			down_read(&chan->conn->session_lock);
			ksmbd_user_session_get(session);

#if IS_ENABLED(CONFIG_IPV6)
			if (chan->conn->is_ipv6)
				seq_printf(m, "client:\t%pI6c\n", &chan->conn->inet6_addr);
			else
#endif
				seq_printf(m, "client:\t%pI4\n", &chan->conn->inet_addr);
			seq_printf(m, "user:\t%s\n", session_user_name(session));
			seq_printf(m, "id:\t%llu\n", session->id);
			seq_printf(m, "state:\t%s\n\n",
				   session_state_string(session));

			ksmbd_user_session_put(session);
			up_read(&chan->conn->session_lock);
		}
		up_read(&session->chann_lock);
	}
	up_read(&sessions_table_lock);
	return 0;
}

int create_proc_sessions(void)
{
	if (!ksmbd_proc_create("sessions/sessions",
			       show_proc_sessions, NULL))
		return -ENOMEM;
	return 0;
}
#else
int create_proc_sessions(void) { return 0; }
static int create_proc_session(struct ksmbd_session *sess) { return 0; }
static void delete_proc_session(struct ksmbd_session *sess) {}
#endif

static void free_channel_list(struct ksmbd_session *sess)
{
	struct channel *chann;
	unsigned long index;

	down_write(&sess->chann_lock);
	xa_for_each(&sess->ksmbd_chann_list, index, chann) {
		xa_erase(&sess->ksmbd_chann_list, index);
		ksmbd_session_release_slot(chann->conn);
		ksmbd_conn_put(chann->conn);
		kfree_sensitive(chann);
	}

	xa_destroy(&sess->ksmbd_chann_list);
	up_write(&sess->chann_lock);
}

static void ksmbd_session_rpc_free(struct ksmbd_session_rpc *entry)
{
	if (refcount_dec_and_test(&entry->refcount)) {
		ksmbd_rpc_id_free(entry->id);
		kvfree(entry->pending);
		kfree(entry);
	}
}

static void __session_rpc_close(struct ksmbd_session_rpc *entry)
{
	struct ksmbd_rpc_command *resp;

	mutex_lock(&entry->lock);
	resp = ksmbd_rpc_close(entry);
	if (!resp)
		pr_err("Unable to close RPC pipe %d\n", entry->id);
	kvfree(resp);
	mutex_unlock(&entry->lock);
	ksmbd_session_rpc_free(entry);
}

void ksmbd_session_rpc_clear_list(struct ksmbd_session *sess)
{
	struct ksmbd_session_rpc *entry;
	unsigned long index;

	xa_for_each(&sess->rpc_handle_list, index, entry)
		ksmbd_session_rpc_close(sess, index);
	xa_destroy(&sess->rpc_handle_list);
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

	if (!strcmp(rpc_name, "\\mdssvc") || !strcmp(rpc_name, "mdssvc"))
		return -ENOENT;

	pr_err("Unsupported RPC: %s\n", rpc_name);
	return -ENOENT;
}

int ksmbd_session_rpc_open(struct ksmbd_session *sess, char *rpc_name)
{
	struct ksmbd_session_rpc *entry;
	struct ksmbd_rpc_command *resp;
	int method, id, ret;

	method = __rpc_method(rpc_name);
	if (method < 0)
		return method;

	entry = kzalloc_obj(struct ksmbd_session_rpc, KSMBD_DEFAULT_GFP);
	if (!entry)
		return -ENOMEM;

	entry->method = method;
	entry->id = id = ksmbd_ipc_id_alloc();
	if (id < 0) {
		kfree(entry);
		return id;
	}
	mutex_init(&entry->lock);
	refcount_set(&entry->refcount, 1);

	resp = ksmbd_rpc_open(entry);
	if (!resp) {
		ksmbd_session_rpc_free(entry);
		return -EINVAL;
	}
	kvfree(resp);

	down_write(&sess->rpc_lock);
	ret = xa_err(xa_store(&sess->rpc_handle_list, id, entry,
			      KSMBD_DEFAULT_GFP));
	up_write(&sess->rpc_lock);
	if (ret) {
		__session_rpc_close(entry);
		return ret;
	}
	return id;
}

void ksmbd_session_rpc_close(struct ksmbd_session *sess, int id)
{
	struct ksmbd_session_rpc *entry;

	down_write(&sess->rpc_lock);
	entry = xa_erase(&sess->rpc_handle_list, id);
	if (entry)
		WRITE_ONCE(entry->closed, true);
	up_write(&sess->rpc_lock);
	if (entry)
		__session_rpc_close(entry);
}

/* The reference pins the entry while waiting without holding rpc_lock. */
struct ksmbd_session_rpc *ksmbd_session_rpc_get(struct ksmbd_work *work,
					     int id)
{
	struct ksmbd_session_rpc *entry;
	int err;

	down_read(&work->sess->rpc_lock);
	entry = xa_load(&work->sess->rpc_handle_list, id);
	if (entry)
		refcount_inc(&entry->refcount);
	up_read(&work->sess->rpc_lock);
	if (!entry)
		return ERR_PTR(-ENOENT);

	mutex_lock(&entry->lock);
	if (READ_ONCE(work->state) == KSMBD_WORK_CANCELLED) {
		err = -ECANCELED;
		goto err_put;
	}
	if (ksmbd_conn_exiting(work->conn) ||
	    ksmbd_conn_releasing(work->conn) ||
	    READ_ONCE(work->sess->tearing_down)) {
		err = -ESHUTDOWN;
		goto err_put;
	}
	if (READ_ONCE(entry->closed)) {
		err = -EBADF;
		goto err_put;
	}
	return entry;

err_put:
	ksmbd_session_rpc_put(entry);
	return ERR_PTR(err);
}

void ksmbd_session_rpc_put(struct ksmbd_session_rpc *entry)
{
	mutex_unlock(&entry->lock);
	ksmbd_session_rpc_free(entry);
}

int ksmbd_session_rpc_method(struct ksmbd_session *sess, int id)
{
	struct ksmbd_session_rpc *entry;

	lockdep_assert_held(&sess->rpc_lock);
	entry = xa_load(&sess->rpc_handle_list, id);

	return entry ? entry->method : 0;
}

void ksmbd_session_destroy(struct ksmbd_session *sess)
{
	struct ksmbd_conn *setup_conn;

	if (!sess)
		return;

	setup_conn = sess->setup_conn;
	delete_proc_session(sess);
	ksmbd_tree_conn_session_logoff(sess);
	ksmbd_destroy_file_table(sess);
	if (sess->user)
		ksmbd_free_user(sess->user);
	ksmbd_launch_ksmbd_durable_scavenger();
	ksmbd_session_rpc_clear_list(sess);
	free_channel_list(sess);
	kfree_sensitive(sess->Preauth_HashValue);
	if (sess->id)
		ksmbd_release_id(&session_ida, sess->id);
	ida_destroy(&sess->tree_conn_ida);
	kfree_sensitive(sess);
	if (setup_conn) {
		ksmbd_session_release_slot(setup_conn);
		ksmbd_conn_put(setup_conn);
	}
}

static void ksmbd_session_remove_from_table(struct ksmbd_session *sess)
{
	hash_del(&sess->hlist);
	ksmbd_counter_dec(KSMBD_COUNTER_SESSIONS);
}

static void ksmbd_dispose_sessions(struct list_head *dispose)
{
	struct ksmbd_session *sess, *tmp;

	list_for_each_entry_safe(sess, tmp, dispose, dispose_entry) {
		list_del_init(&sess->dispose_entry);
		delete_proc_session(sess);
		ksmbd_user_session_put(sess);
	}
}

struct ksmbd_session *__session_lookup(unsigned long long id)
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

bool ksmbd_too_many_session_setups(struct ksmbd_conn *conn)
{
	LIST_HEAD(dispose);
	unsigned long id;
	struct ksmbd_session *sess;
	unsigned int pending = 0;

	down_write(&sessions_table_lock);
	down_write(&conn->session_lock);
	xa_for_each(&conn->sessions, id, sess) {
		if (READ_ONCE(sess->state) != SMB2_SESSION_IN_PROGRESS)
			continue;

		/* Keep response signing keys until every request has dropped its reference. */
		if (atomic_read(&sess->refcnt) == 1 &&
		    time_after(jiffies, sess->last_active +
			       KSMBD_UNAUTHENTICATED_CONN_TIMEOUT)) {
			xa_erase(&conn->sessions, sess->id);
			ksmbd_session_remove_from_table(sess);
			list_add_tail(&sess->dispose_entry, &dispose);
			continue;
		}
		pending++;
	}
	up_write(&conn->session_lock);
	up_write(&sessions_table_lock);
	ksmbd_dispose_sessions(&dispose);
	return pending >= KSMBD_MAX_PENDING_SESSIONS;
}

int ksmbd_session_register(struct ksmbd_conn *conn,
			   struct ksmbd_session *sess)
{
	int ret;

	sess->dialect = conn->dialect;
	memcpy(sess->ClientGUID, conn->ClientGUID, SMB2_CLIENT_GUID_SIZE);
	ret = xa_err(xa_store(&conn->sessions, sess->id, sess,
			      KSMBD_DEFAULT_GFP));
	if (ret) {
		down_write(&sessions_table_lock);
		ksmbd_session_remove_from_table(sess);
		up_write(&sessions_table_lock);
		delete_proc_session(sess);
		ksmbd_user_session_put(sess);
	}

	return ret;
}

/* The caller holds sessions_table_lock for writing. */
static bool __ksmbd_session_unregister(struct ksmbd_conn *conn,
				       struct ksmbd_session *sess)
{
	struct ksmbd_conn *session_conns[KSMBD_MAX_CHANNELS];
	struct channel *chann;
	unsigned long index;
	unsigned int nr_conns = 0, i;
	bool removed = false;

	lockdep_assert_held_write(&sessions_table_lock);
	if (!hlist_unhashed(&sess->hlist)) {
		/* Existing references stay valid under sessions_table_lock. */
		down_read(&sess->chann_lock);
		/* SMB2.0/2.1 sessions have no channel holding their connection. */
		if (sess->setup_conn)
			session_conns[nr_conns++] = sess->setup_conn;
		xa_for_each(&sess->ksmbd_chann_list, index, chann) {
			if (nr_conns == ARRAY_SIZE(session_conns))
				break;
			session_conns[nr_conns++] = chann->conn;
		}
		up_read(&sess->chann_lock);

		ksmbd_session_remove_from_table(sess);
		removed = true;
	}

	if (conn) {
		down_write(&conn->session_lock);
		if (xa_load(&conn->sessions, sess->id) == sess)
			xa_erase(&conn->sessions, sess->id);
		up_write(&conn->session_lock);
	}
	for (i = 0; i < nr_conns; i++) {
		if (session_conns[i] == conn)
			continue;
		down_write(&session_conns[i]->session_lock);
		if (xa_load(&session_conns[i]->sessions, sess->id) == sess)
			xa_erase(&session_conns[i]->sessions, sess->id);
		up_write(&session_conns[i]->session_lock);
	}
	return removed;
}

void ksmbd_session_unregister(struct ksmbd_conn *conn,
			      struct ksmbd_session *sess)
{
	bool removed;

	down_write(&sessions_table_lock);
	removed = __ksmbd_session_unregister(conn, sess);
	up_write(&sessions_table_lock);

	if (removed) {
		/*
		 * Keep the table reference until procfs readers have finished,
		 * so none can remove its own proc entry on the last put.
		 */
		delete_proc_session(sess);
		ksmbd_user_session_put(sess);
	}
}

void ksmbd_reclaim_ended_sessions(void)
{
	LIST_HEAD(dispose);
	struct ksmbd_session *sess;
	struct hlist_node *tmp;
	int bkt;

	down_write(&sessions_table_lock);
	hash_for_each_safe(sessions_table, bkt, tmp, sess, hlist) {
		if (READ_ONCE(sess->state) != SMB2_SESSION_EXPIRED ||
		    READ_ONCE(sess->kerberos_expiry) ||
		    atomic_read(&sess->refcnt) != 1)
			continue;

		__ksmbd_session_unregister(NULL, sess);
		list_add_tail(&sess->dispose_entry, &dispose);
	}
	up_write(&sessions_table_lock);
	ksmbd_dispose_sessions(&dispose);
}

bool ksmbd_conn_has_valid_or_expired_session(struct ksmbd_conn *conn)
{
	struct ksmbd_session *sess;
	unsigned long id;
	int state, bkt;
	bool found = false;

	/*
	 * The expiration worker holds conn_list_lock, which session teardown
	 * takes after the session locks. Defer scavenging if either is busy.
	 */
	if (!down_read_trylock(&conn->session_lock))
		return true;
	xa_for_each(&conn->sessions, id, sess) {
		state = READ_ONCE(sess->state);
		if (state == SMB2_SESSION_VALID ||
		    state == SMB2_SESSION_EXPIRED) {
			found = true;
			break;
		}
	}
	up_read(&conn->session_lock);
	if (found)
		return true;

	/* A session bound through SMB3 multichannel is not in conn->sessions. */
	if (!down_read_trylock(&sessions_table_lock))
		return true;
	hash_for_each(sessions_table, bkt, sess, hlist) {
		state = READ_ONCE(sess->state);
		if (state != SMB2_SESSION_VALID &&
		    state != SMB2_SESSION_EXPIRED)
			continue;

		down_read(&sess->chann_lock);
		found = xa_load(&sess->ksmbd_chann_list, (long)conn);
		up_read(&sess->chann_lock);
		if (found)
			break;
	}
	up_read(&sessions_table_lock);
	return found;
}

void ksmbd_expire_sessions(void)
{
	struct ksmbd_session *sess;
	u64 now = ktime_get_real_seconds();
	int bkt;

	down_read(&sessions_table_lock);
	hash_for_each(sessions_table, bkt, sess, hlist) {
		if (READ_ONCE(sess->state) != SMB2_SESSION_VALID ||
		    !sess->kerberos_expiry || now < sess->kerberos_expiry)
			continue;

		if (cmpxchg(&sess->state, SMB2_SESSION_VALID,
			    SMB2_SESSION_EXPIRED) == SMB2_SESSION_VALID)
			ksmbd_counter_inc(KSMBD_COUNTER_SESSION_TIMEOUTS);
	}
	up_read(&sessions_table_lock);
}

static int ksmbd_chann_del(struct ksmbd_conn *conn, struct ksmbd_session *sess)
{
	struct channel *chann;

	down_write(&sess->chann_lock);
	chann = xa_erase(&sess->ksmbd_chann_list, (long)conn);
	up_write(&sess->chann_lock);
	if (!chann)
		return -ENOENT;

	ksmbd_session_release_slot(chann->conn);
	ksmbd_conn_put(chann->conn);
	kfree_sensitive(chann);
	return 0;
}

void ksmbd_conn_sessions_cleanup(struct ksmbd_conn *conn)
{
	LIST_HEAD(dispose);
	struct ksmbd_session *sess;
	unsigned long id;
	struct hlist_node *tmp;
	int bkt;

	down_write(&sessions_table_lock);
	hash_for_each_safe(sessions_table, bkt, tmp, sess, hlist) {
		if (!ksmbd_chann_del(conn, sess) &&
		    xa_empty(&sess->ksmbd_chann_list)) {
			ksmbd_session_remove_from_table(sess);
			down_write(&conn->session_lock);
			xa_erase(&conn->sessions, sess->id);
			up_write(&conn->session_lock);
			list_add_tail(&sess->dispose_entry, &dispose);
		}
	}

	down_write(&conn->session_lock);
	xa_for_each(&conn->sessions, id, sess) {
		ksmbd_chann_del(conn, sess);
		if (xa_empty(&sess->ksmbd_chann_list)) {
			xa_erase(&conn->sessions, sess->id);
			ksmbd_session_remove_from_table(sess);
			list_add_tail(&sess->dispose_entry, &dispose);
		}
	}
	up_write(&conn->session_lock);
	up_write(&sessions_table_lock);
	ksmbd_dispose_sessions(&dispose);
}

bool is_ksmbd_session_in_connection(struct ksmbd_conn *conn,
				   unsigned long long id)
{
	struct ksmbd_session *sess;

	down_read(&conn->session_lock);
	sess = xa_load(&conn->sessions, id);
	if (sess) {
		up_read(&conn->session_lock);
		return true;
	}
	up_read(&conn->session_lock);

	return false;
}

struct ksmbd_session *ksmbd_session_lookup(struct ksmbd_conn *conn,
					   unsigned long long id)
{
	struct ksmbd_session *sess;

	down_read(&conn->session_lock);
	sess = xa_load(&conn->sessions, id);
	if (sess) {
		sess->last_active = jiffies;
		ksmbd_user_session_get(sess);
	}
	up_read(&conn->session_lock);
	return sess;
}

struct ksmbd_session *ksmbd_session_lookup_slowpath(unsigned long long id)
{
	struct ksmbd_session *sess;

	down_read(&sessions_table_lock);
	sess = __session_lookup(id);
	if (sess)
		ksmbd_user_session_get(sess);
	up_read(&sessions_table_lock);

	return sess;
}

/* The caller holds a session reference and must put each returned connection. */
unsigned int ksmbd_session_get_channels(struct ksmbd_session *sess,
					struct ksmbd_conn **conns,
					unsigned int max_conns)
{
	struct channel *chann;
	unsigned long index;
	unsigned int nr_conns = 0;

	/*
	 * Channel connections are borrowed. Unregister can leave the channel
	 * list in a pinned session after removing it from the registry, so
	 * chann_lock alone does not keep those connections alive. Only collect
	 * registered sessions, excluding both unregister and connection cleanup
	 * until the connection references have been taken.
	 */
	down_read(&sessions_table_lock);
	if (hlist_unhashed(&sess->hlist))
		goto out;
	down_read(&sess->chann_lock);
	xa_for_each(&sess->ksmbd_chann_list, index, chann) {
		if (nr_conns == max_conns)
			break;
		conns[nr_conns++] = ksmbd_conn_get(chann->conn);
	}
	up_read(&sess->chann_lock);
out:
	up_read(&sessions_table_lock);
	return nr_conns;
}

struct ksmbd_session *ksmbd_session_lookup_all_states(struct ksmbd_conn *conn,
						      unsigned long long id)
{
	struct ksmbd_session *sess;
	bool channel_found;

	sess = ksmbd_session_lookup(conn, id);
	if (!sess) {
		sess = ksmbd_session_lookup_slowpath(id);
		if (!sess)
			return NULL;

		down_read(&sess->chann_lock);
		channel_found = xa_load(&sess->ksmbd_chann_list, (long)conn);
		up_read(&sess->chann_lock);
		if (!channel_found) {
			ksmbd_user_session_put(sess);
			sess = NULL;
		}
	}
	return sess;
}

struct ksmbd_session *ksmbd_session_lookup_all(struct ksmbd_conn *conn,
					       unsigned long long id)
{
	struct ksmbd_session *sess;

	sess = ksmbd_session_lookup_all_states(conn, id);
	if (sess && sess->state != SMB2_SESSION_VALID) {
		ksmbd_user_session_put(sess);
		sess = NULL;
	}
	return sess;
}

void ksmbd_user_session_get(struct ksmbd_session *sess)
{
	atomic_inc(&sess->refcnt);
}

void ksmbd_user_session_put(struct ksmbd_session *sess)
{
	if (!sess)
		return;

	if (atomic_read(&sess->refcnt) <= 0)
		WARN_ON(1);
	else if (atomic_dec_and_test(&sess->refcnt))
		ksmbd_session_destroy(sess);
}

struct preauth_session *ksmbd_preauth_session_alloc(struct ksmbd_conn *conn,
						    u64 sess_id)
{
	struct preauth_session *sess;

	sess = kmalloc_obj(struct preauth_session, KSMBD_DEFAULT_GFP);
	if (!sess)
		return NULL;

	sess->id = sess_id;
	memcpy(sess->Preauth_HashValue, conn->preauth_info->Preauth_HashValue,
	       PREAUTH_HASHVALUE_SIZE);
	list_add(&sess->preauth_entry, &conn->preauth_sess_table);

	return sess;
}

void ksmbd_preauth_session_destroy(struct ksmbd_conn *conn)
{
	struct preauth_session *sess, *tmp;

	list_for_each_entry_safe(sess, tmp, &conn->preauth_sess_table,
				 preauth_entry) {
		list_del(&sess->preauth_entry);
		kfree(sess);
	}
}

void destroy_previous_session(struct ksmbd_conn *conn,
			      struct ksmbd_user *user, u64 id)
{
	struct ksmbd_session *prev_sess;
	struct ksmbd_user *prev_user;
	bool pinned = false;
	int err;

	down_write(&sessions_table_lock);
	down_write(&conn->session_lock);
	prev_sess = __session_lookup(id);
	if (!prev_sess || prev_sess->state == SMB2_SESSION_EXPIRED)
		goto out;

	prev_user = prev_sess->user;
	if (!prev_user ||
	    strcmp(user->name, prev_user->name) ||
	    user->passkey_sz != prev_user->passkey_sz ||
	    memcmp(user->passkey, prev_user->passkey, user->passkey_sz))
		goto out;

	down_write(&prev_sess->chann_lock);
	if (prev_sess->tearing_down) {
		up_write(&prev_sess->chann_lock);
		goto out;
	}
	WRITE_ONCE(prev_sess->tearing_down, true);
	up_write(&prev_sess->chann_lock);
	ksmbd_user_session_get(prev_sess);
	pinned = true;
	up_write(&conn->session_lock);
	up_write(&sessions_table_lock);

	ksmbd_all_conn_set_status(prev_sess, KSMBD_SESS_NEED_RECONNECT);
	/*
	 * The caller has finished using the connection's authentication state.
	 * Drop srv_mutex so drained requests can write their responses.
	 */
	ksmbd_conn_unlock(conn);
	err = ksmbd_conn_wait_idle_sess(conn, prev_sess);
	ksmbd_conn_lock(conn);
	down_write(&sessions_table_lock);
	down_write(&conn->session_lock);
	if (err) {
		down_write(&prev_sess->chann_lock);
		WRITE_ONCE(prev_sess->tearing_down, false);
		up_write(&prev_sess->chann_lock);
		ksmbd_all_conn_set_status(prev_sess, KSMBD_SESS_GOOD);
		goto out;
	}

	ksmbd_destroy_file_table(prev_sess);
	prev_sess->kerberos_expiry = 0;
	prev_sess->state = SMB2_SESSION_EXPIRED;
	ksmbd_all_conn_set_status(prev_sess, KSMBD_SESS_NEED_SETUP);
	ksmbd_launch_ksmbd_durable_scavenger();
out:
	up_write(&conn->session_lock);
	up_write(&sessions_table_lock);
	if (pinned)
		ksmbd_user_session_put(prev_sess);
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

static struct ksmbd_session *__session_create(struct ksmbd_conn *conn, int protocol)
{
	struct ksmbd_session *sess;
	int ret;

	if (protocol != CIFDS_SESSION_FLAG_SMB2)
		return NULL;

	if (ksmbd_session_reserve_slot(conn))
		return NULL;

	sess = kzalloc_obj(struct ksmbd_session, KSMBD_DEFAULT_GFP);
	if (!sess) {
		ksmbd_session_release_slot(conn);
		return NULL;
	}

	sess->setup_conn = ksmbd_conn_get(conn);
	ida_init(&sess->tree_conn_ida);
	sess->last_active = jiffies;
	sess->state = SMB2_SESSION_IN_PROGRESS;
	set_session_flag(sess, protocol);
	xa_init(&sess->tree_conns);
	xa_init(&sess->ksmbd_chann_list);
	xa_init(&sess->rpc_handle_list);
	INIT_LIST_HEAD(&sess->dispose_entry);
	sess->sequence_number = 1;
	atomic_set(&sess->refcnt, 2);
	init_rwsem(&sess->tree_conns_lock);
	init_rwsem(&sess->rpc_lock);
	init_rwsem(&sess->chann_lock);

	if (ksmbd_init_file_table(&sess->file_table))
		goto error;

	ret = __init_smb2_session(sess);
	if (ret)
		goto error;

	down_write(&sessions_table_lock);
	hash_add(sessions_table, &sess->hlist, sess->id);
	ksmbd_counter_inc(KSMBD_COUNTER_SESSIONS);
	up_write(&sessions_table_lock);

	if (create_proc_session(sess))
		pr_warn_ratelimited("Unable to create session %llu procfs entry\n", sess->id);
	return sess;

error:
	ksmbd_session_destroy(sess);
	return NULL;
}

struct ksmbd_session *ksmbd_smb2_session_create(struct ksmbd_conn *conn)
{
	return __session_create(conn, CIFDS_SESSION_FLAG_SMB2);
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
