// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) 2016 Namjae Jeon <linkinjeon@kernel.org>
 *   Copyright (C) 2018 Samsung Electronics Co., Ltd.
 */

#include "glob.h"
#include "oplock.h"
#include "misc.h"
#include <linux/sched/signal.h>
#include <linux/workqueue.h>
#include <linux/sysfs.h>
#include <linux/module.h>
#include <linux/moduleparam.h>

#include "server.h"
#include "smb_common.h"
#include "smbstatus.h"
#include "connection.h"
#include "transport_ipc.h"
#include "mgmt/user_session.h"
#include "crypto_ctx.h"
#include "auth.h"

int ksmbd_debug_types;

struct ksmbd_server_config server_conf;

enum SERVER_CTRL_TYPE {
	SERVER_CTRL_TYPE_INIT,
	SERVER_CTRL_TYPE_RESET,
};

struct server_ctrl_struct {
	int			type;
	struct work_struct	ctrl_work;
};

static DEFINE_MUTEX(ctrl_lock);

static int ___server_conf_set(int idx, char *val)
{
	if (idx >= ARRAY_SIZE(server_conf.conf))
		return -EINVAL;

	if (!val || val[0] == 0x00)
		return -EINVAL;

	kfree(server_conf.conf[idx]);
	server_conf.conf[idx] = kstrdup(val, GFP_KERNEL);
	if (!server_conf.conf[idx])
		return -ENOMEM;
	return 0;
}

int ksmbd_set_netbios_name(char *v)
{
	return ___server_conf_set(SERVER_CONF_NETBIOS_NAME, v);
}

int ksmbd_set_server_string(char *v)
{
	return ___server_conf_set(SERVER_CONF_SERVER_STRING, v);
}

int ksmbd_set_work_group(char *v)
{
	return ___server_conf_set(SERVER_CONF_WORK_GROUP, v);
}

char *ksmbd_netbios_name(void)
{
	return server_conf.conf[SERVER_CONF_NETBIOS_NAME];
}

char *ksmbd_server_string(void)
{
	return server_conf.conf[SERVER_CONF_SERVER_STRING];
}

char *ksmbd_work_group(void)
{
	return server_conf.conf[SERVER_CONF_WORK_GROUP];
}

/**
 * check_conn_state() - check state of server thread connection
 * @work:     smb work containing server thread information
 *
 * Return:	0 on valid connection, otherwise 1 to reconnect
 */
static inline int check_conn_state(struct ksmbd_work *work)
{
	struct smb_hdr *rsp_hdr;

	if (ksmbd_conn_exiting(work->conn) ||
	    ksmbd_conn_need_reconnect(work->conn)) {
		rsp_hdr = work->response_buf;
		rsp_hdr->Status.CifsError = STATUS_CONNECTION_DISCONNECTED;
		return 1;
	}
	return 0;
}

#define SERVER_HANDLER_CONTINUE		0
#define SERVER_HANDLER_ABORT		1

static int __process_request(struct ksmbd_work *work, struct ksmbd_conn *conn,
			     u16 *cmd)
{
	struct smb_version_cmds *cmds;
	u16 command;
	int ret;

	if (check_conn_state(work))
		return SERVER_HANDLER_CONTINUE;

	if (ksmbd_verify_smb_message(work)) {
		conn->ops->set_rsp_status(work, STATUS_INVALID_PARAMETER);
		return SERVER_HANDLER_ABORT;
	}

	command = conn->ops->get_cmd_val(work);
	*cmd = command;

andx_again:
	if (command >= conn->max_cmds) {
		conn->ops->set_rsp_status(work, STATUS_INVALID_PARAMETER);
		return SERVER_HANDLER_CONTINUE;
	}

	cmds = &conn->cmds[command];
	if (!cmds->proc) {
		ksmbd_debug(SMB, "*** not implemented yet cmd = %x\n", command);
		conn->ops->set_rsp_status(work, STATUS_NOT_IMPLEMENTED);
		return SERVER_HANDLER_CONTINUE;
	}

	if (work->sess && conn->ops->is_sign_req(work, command)) {
		ret = conn->ops->check_sign_req(work);
		if (!ret) {
			conn->ops->set_rsp_status(work, STATUS_ACCESS_DENIED);
			return SERVER_HANDLER_CONTINUE;
		}
	}

	ret = cmds->proc(work);

	if (ret < 0)
		ksmbd_debug(CONN, "Failed to process %u [%d]\n", command, ret);
	/* AndX commands - chained request can return positive values */
	else if (ret > 0) {
		command = ret;
		*cmd = command;
		goto andx_again;
	}

	if (work->send_no_response)
		return SERVER_HANDLER_ABORT;
	return SERVER_HANDLER_CONTINUE;
}

static void __handle_ksmbd_work(struct ksmbd_work *work,
				struct ksmbd_conn *conn)
{
	u16 command = 0;
	int rc;
	bool is_chained = false;

	if (conn->ops->allocate_rsp_buf(work))
		return;

	if (conn->ops->is_transform_hdr &&
	    conn->ops->is_transform_hdr(work->request_buf)) {
		rc = conn->ops->decrypt_req(work);
		if (rc < 0) {
			conn->ops->set_rsp_status(work, STATUS_DATA_ERROR);
			goto send;
		}

		work->encrypted = true;
	}

	rc = conn->ops->init_rsp_hdr(work);
	if (rc) {
		/* either uid or tid is not correct */
		conn->ops->set_rsp_status(work, STATUS_INVALID_HANDLE);
		goto send;
	}

	do {
		if (conn->ops->check_user_session) {
			rc = conn->ops->check_user_session(work);
			if (rc < 0) {
				if (rc == -EINVAL)
					conn->ops->set_rsp_status(work,
						STATUS_INVALID_PARAMETER);
				else
					conn->ops->set_rsp_status(work,
						STATUS_USER_SESSION_DELETED);
				goto send;
			} else if (rc > 0) {
				rc = conn->ops->get_ksmbd_tcon(work);
				if (rc < 0) {
					if (rc == -EINVAL)
						conn->ops->set_rsp_status(work,
							STATUS_INVALID_PARAMETER);
					else
						conn->ops->set_rsp_status(work,
							STATUS_NETWORK_NAME_DELETED);
					goto send;
				}
			}
		}

		rc = __process_request(work, conn, &command);
		if (rc == SERVER_HANDLER_ABORT)
			break;

		/*
		 * Call smb2_set_rsp_credits() function to set number of credits
		 * granted in hdr of smb2 response.
		 */
		if (conn->ops->set_rsp_credits) {
			spin_lock(&conn->credits_lock);
			rc = conn->ops->set_rsp_credits(work);
			spin_unlock(&conn->credits_lock);
			if (rc < 0) {
				conn->ops->set_rsp_status(work,
					STATUS_INVALID_PARAMETER);
				goto send;
			}
		}

		is_chained = is_chained_smb2_message(work);

		if (work->sess &&
		    (work->sess->sign || smb3_11_final_sess_setup_resp(work) ||
		     conn->ops->is_sign_req(work, command)))
			conn->ops->set_sign_rsp(work);
	} while (is_chained == true);

send:
	if (work->tcon)
		ksmbd_tree_connect_put(work->tcon);
	smb3_preauth_hash_rsp(work);
	if (work->sess && work->sess->enc && work->encrypted &&
	    conn->ops->encrypt_resp) {
		rc = conn->ops->encrypt_resp(work);
		if (rc < 0)
			conn->ops->set_rsp_status(work, STATUS_DATA_ERROR);
	}

	ksmbd_conn_write(work);
}

/**
 * handle_ksmbd_work() - process pending smb work requests
 * @wk:	smb work containing request command buffer
 *
 * called by kworker threads to processing remaining smb work requests
 */
static void handle_ksmbd_work(struct work_struct *wk)
{
	struct ksmbd_work *work = container_of(wk, struct ksmbd_work, work);
	struct ksmbd_conn *conn = work->conn;

	atomic64_inc(&conn->stats.request_served);

	__handle_ksmbd_work(work, conn);

	ksmbd_conn_try_dequeue_request(work);
	ksmbd_free_work_struct(work);
	/*
	 * Checking waitqueue to dropping pending requests on
	 * disconnection. waitqueue_active is safe because it
	 * uses atomic operation for condition.
	 */
	if (!atomic_dec_return(&conn->r_count) && waitqueue_active(&conn->r_count_q))
		wake_up(&conn->r_count_q);
}

/**
 * queue_ksmbd_work() - queue a smb request to worker thread queue
 *		for proccessing smb command and sending response
 * @conn:	connection instance
 *
 * read remaining data from socket create and submit work.
 */
static int queue_ksmbd_work(struct ksmbd_conn *conn)
{
	struct ksmbd_work *work;
	int err;

	work = ksmbd_alloc_work_struct();
	if (!work) {
		pr_err("allocation for work failed\n");
		return -ENOMEM;
	}

	work->conn = conn;
	work->request_buf = conn->request_buf;
	conn->request_buf = NULL;

	err = ksmbd_init_smb_server(work);
	if (err) {
		ksmbd_free_work_struct(work);
		return 0;
	}

	ksmbd_conn_enqueue_request(work);
	atomic_inc(&conn->r_count);
	/* update activity on connection */
	conn->last_active = jiffies;
	INIT_WORK(&work->work, handle_ksmbd_work);
	ksmbd_queue_work(work);
	return 0;
}

static int ksmbd_server_process_request(struct ksmbd_conn *conn)
{
	return queue_ksmbd_work(conn);
}

static int ksmbd_server_terminate_conn(struct ksmbd_conn *conn)
{
	ksmbd_sessions_deregister(conn);
	destroy_lease_table(conn);
	return 0;
}

static void ksmbd_server_tcp_callbacks_init(void)
{
	struct ksmbd_conn_ops ops;

	ops.process_fn = ksmbd_server_process_request;
	ops.terminate_fn = ksmbd_server_terminate_conn;

	ksmbd_conn_init_server_callbacks(&ops);
}

static void server_conf_free(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(server_conf.conf); i++) {
		kfree(server_conf.conf[i]);
		server_conf.conf[i] = NULL;
	}
}

static int server_conf_init(void)
{
	WRITE_ONCE(server_conf.state, SERVER_STATE_STARTING_UP);
	server_conf.enforced_signing = 0;
	server_conf.min_protocol = ksmbd_min_protocol();
	server_conf.max_protocol = ksmbd_max_protocol();
	server_conf.auth_mechs = KSMBD_AUTH_NTLMSSP;
#ifdef CONFIG_SMB_SERVER_KERBEROS5
	server_conf.auth_mechs |= KSMBD_AUTH_KRB5 |
				KSMBD_AUTH_MSKRB5;
#endif
	return 0;
}

static void server_ctrl_handle_init(struct server_ctrl_struct *ctrl)
{
	int ret;

	ret = ksmbd_conn_transport_init();
	if (ret) {
		server_queue_ctrl_reset_work();
		return;
	}

	WRITE_ONCE(server_conf.state, SERVER_STATE_RUNNING);
}

static void server_ctrl_handle_reset(struct server_ctrl_struct *ctrl)
{
	ksmbd_ipc_soft_reset();
	ksmbd_conn_transport_destroy();
	server_conf_free();
	server_conf_init();
	WRITE_ONCE(server_conf.state, SERVER_STATE_STARTING_UP);
}

static void server_ctrl_handle_work(struct work_struct *work)
{
	struct server_ctrl_struct *ctrl;

	ctrl = container_of(work, struct server_ctrl_struct, ctrl_work);

	mutex_lock(&ctrl_lock);
	switch (ctrl->type) {
	case SERVER_CTRL_TYPE_INIT:
		server_ctrl_handle_init(ctrl);
		break;
	case SERVER_CTRL_TYPE_RESET:
		server_ctrl_handle_reset(ctrl);
		break;
	default:
		pr_err("Unknown server work type: %d\n", ctrl->type);
	}
	mutex_unlock(&ctrl_lock);
	kfree(ctrl);
	module_put(THIS_MODULE);
}

static int __queue_ctrl_work(int type)
{
	struct server_ctrl_struct *ctrl;

	ctrl = kmalloc(sizeof(struct server_ctrl_struct), GFP_KERNEL);
	if (!ctrl)
		return -ENOMEM;

	__module_get(THIS_MODULE);
	ctrl->type = type;
	INIT_WORK(&ctrl->ctrl_work, server_ctrl_handle_work);
	queue_work(system_long_wq, &ctrl->ctrl_work);
	return 0;
}

int server_queue_ctrl_init_work(void)
{
	return __queue_ctrl_work(SERVER_CTRL_TYPE_INIT);
}

int server_queue_ctrl_reset_work(void)
{
	return __queue_ctrl_work(SERVER_CTRL_TYPE_RESET);
}

static ssize_t stats_show(struct class *class, struct class_attribute *attr,
			  char *buf)
{
	/*
	 * Inc this each time you change stats output format,
	 * so user space will know what to do.
	 */
	static int stats_version = 2;
	static const char * const state[] = {
		"startup",
		"running",
		"reset",
		"shutdown"
	};
	return sysfs_emit(buf, "%d %s %d %lu\n", stats_version,
			  state[server_conf.state], server_conf.tcp_port,
			  server_conf.ipc_last_active / HZ);
}

static ssize_t kill_server_store(struct class *class,
				 struct class_attribute *attr, const char *buf,
				 size_t len)
{
	if (!sysfs_streq(buf, "hard"))
		return len;

	pr_info("kill command received\n");
	mutex_lock(&ctrl_lock);
	WRITE_ONCE(server_conf.state, SERVER_STATE_RESETTING);
	__module_get(THIS_MODULE);
	server_ctrl_handle_reset(NULL);
	module_put(THIS_MODULE);
	mutex_unlock(&ctrl_lock);
	return len;
}

static const char * const debug_type_strings[] = {"smb", "auth", "vfs",
						  "oplock", "ipc", "conn",
						  "rdma"};

static ssize_t debug_show(struct class *class, struct class_attribute *attr,
			  char *buf)
{
	ssize_t sz = 0;
	int i, pos = 0;

	for (i = 0; i < ARRAY_SIZE(debug_type_strings); i++) {
		if ((ksmbd_debug_types >> i) & 1) {
			pos = sysfs_emit_at(buf, sz, "[%s] ", debug_type_strings[i]);
		} else {
			pos = sysfs_emit_at(buf, sz, "%s ", debug_type_strings[i]);
		}
		sz += pos;
	}
	sz += sysfs_emit_at(buf, sz, "\n");
	return sz;
}

static ssize_t debug_store(struct class *class, struct class_attribute *attr,
			   const char *buf, size_t len)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(debug_type_strings); i++) {
		if (sysfs_streq(buf, "all")) {
			if (ksmbd_debug_types == KSMBD_DEBUG_ALL)
				ksmbd_debug_types = 0;
			else
				ksmbd_debug_types = KSMBD_DEBUG_ALL;
			break;
		}

		if (sysfs_streq(buf, debug_type_strings[i])) {
			if (ksmbd_debug_types & (1 << i))
				ksmbd_debug_types &= ~(1 << i);
			else
				ksmbd_debug_types |= (1 << i);
			break;
		}
	}

	return len;
}

static CLASS_ATTR_RO(stats);
static CLASS_ATTR_WO(kill_server);
static CLASS_ATTR_RW(debug);

static struct attribute *ksmbd_control_class_attrs[] = {
	&class_attr_stats.attr,
	&class_attr_kill_server.attr,
	&class_attr_debug.attr,
	NULL,
};
ATTRIBUTE_GROUPS(ksmbd_control_class);

static struct class ksmbd_control_class = {
	.name		= "ksmbd-control",
	.owner		= THIS_MODULE,
	.class_groups	= ksmbd_control_class_groups,
};

static int ksmbd_server_shutdown(void)
{
	WRITE_ONCE(server_conf.state, SERVER_STATE_SHUTTING_DOWN);

	class_unregister(&ksmbd_control_class);
	ksmbd_workqueue_destroy();
	ksmbd_ipc_release();
	ksmbd_conn_transport_destroy();
	ksmbd_crypto_destroy();
	ksmbd_free_global_file_table();
	destroy_lease_table(NULL);
	ksmbd_work_pool_destroy();
	ksmbd_exit_file_cache();
	server_conf_free();
	return 0;
}

static int __init ksmbd_server_init(void)
{
	int ret;

	ret = class_register(&ksmbd_control_class);
	if (ret) {
		pr_err("Unable to register ksmbd-control class\n");
		return ret;
	}

	ksmbd_server_tcp_callbacks_init();

	ret = server_conf_init();
	if (ret)
		goto err_unregister;

	ret = ksmbd_work_pool_init();
	if (ret)
		goto err_unregister;

	ret = ksmbd_init_file_cache();
	if (ret)
		goto err_destroy_work_pools;

	ret = ksmbd_ipc_init();
	if (ret)
		goto err_exit_file_cache;

	ret = ksmbd_init_global_file_table();
	if (ret)
		goto err_ipc_release;

	ret = ksmbd_inode_hash_init();
	if (ret)
		goto err_destroy_file_table;

	ret = ksmbd_crypto_create();
	if (ret)
		goto err_release_inode_hash;

	ret = ksmbd_workqueue_init();
	if (ret)
		goto err_crypto_destroy;

	return 0;

err_crypto_destroy:
	ksmbd_crypto_destroy();
err_release_inode_hash:
	ksmbd_release_inode_hash();
err_destroy_file_table:
	ksmbd_free_global_file_table();
err_ipc_release:
	ksmbd_ipc_release();
err_exit_file_cache:
	ksmbd_exit_file_cache();
err_destroy_work_pools:
	ksmbd_work_pool_destroy();
err_unregister:
	class_unregister(&ksmbd_control_class);

	return ret;
}

/**
 * ksmbd_server_exit() - shutdown forker thread and free memory at module exit
 */
static void __exit ksmbd_server_exit(void)
{
	ksmbd_server_shutdown();
	rcu_barrier();
	ksmbd_release_inode_hash();
}

MODULE_AUTHOR("Namjae Jeon <linkinjeon@kernel.org>");
MODULE_VERSION(KSMBD_VERSION);
MODULE_DESCRIPTION("Linux kernel CIFS/SMB SERVER");
MODULE_LICENSE("GPL");
MODULE_SOFTDEP("pre: ecb");
MODULE_SOFTDEP("pre: hmac");
MODULE_SOFTDEP("pre: md5");
MODULE_SOFTDEP("pre: nls");
MODULE_SOFTDEP("pre: aes");
MODULE_SOFTDEP("pre: cmac");
MODULE_SOFTDEP("pre: sha256");
MODULE_SOFTDEP("pre: sha512");
MODULE_SOFTDEP("pre: aead2");
MODULE_SOFTDEP("pre: ccm");
MODULE_SOFTDEP("pre: gcm");
MODULE_SOFTDEP("pre: crc32");
module_init(ksmbd_server_init)
module_exit(ksmbd_server_exit)
