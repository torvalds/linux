/**
 * eCryptfs: Linux filesystem encryption layer
 *
 * Copyright (C) 2004-2008 International Business Machines Corp.
 *   Author(s): Michael A. Halcrow <mhalcrow@us.ibm.com>
 *		Tyler Hicks <tyhicks@ou.edu>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/user_namespace.h>
#include <linux/nsproxy.h>
#include "ecryptfs_kernel.h"

static LIST_HEAD(ecryptfs_msg_ctx_free_list);
static LIST_HEAD(ecryptfs_msg_ctx_alloc_list);
static struct mutex ecryptfs_msg_ctx_lists_mux;

static struct hlist_head *ecryptfs_daemon_hash;
struct mutex ecryptfs_daemon_hash_mux;
static int ecryptfs_hash_bits;
#define ecryptfs_uid_hash(uid) \
        hash_long((unsigned long)uid, ecryptfs_hash_bits)

static u32 ecryptfs_msg_counter;
static struct ecryptfs_msg_ctx *ecryptfs_msg_ctx_arr;

/**
 * ecryptfs_acquire_free_msg_ctx
 * @msg_ctx: The context that was acquired from the free list
 *
 * Acquires a context element from the free list and locks the mutex
 * on the context.  Sets the msg_ctx task to current.  Returns zero on
 * success; non-zero on error or upon failure to acquire a free
 * context element.  Must be called with ecryptfs_msg_ctx_lists_mux
 * held.
 */
static int ecryptfs_acquire_free_msg_ctx(struct ecryptfs_msg_ctx **msg_ctx)
{
	struct list_head *p;
	int rc;

	if (list_empty(&ecryptfs_msg_ctx_free_list)) {
		printk(KERN_WARNING "%s: The eCryptfs free "
		       "context list is empty.  It may be helpful to "
		       "specify the ecryptfs_message_buf_len "
		       "parameter to be greater than the current "
		       "value of [%d]\n", __func__, ecryptfs_message_buf_len);
		rc = -ENOMEM;
		goto out;
	}
	list_for_each(p, &ecryptfs_msg_ctx_free_list) {
		*msg_ctx = list_entry(p, struct ecryptfs_msg_ctx, node);
		if (mutex_trylock(&(*msg_ctx)->mux)) {
			(*msg_ctx)->task = current;
			rc = 0;
			goto out;
		}
	}
	rc = -ENOMEM;
out:
	return rc;
}

/**
 * ecryptfs_msg_ctx_free_to_alloc
 * @msg_ctx: The context to move from the free list to the alloc list
 *
 * Must be called with ecryptfs_msg_ctx_lists_mux held.
 */
static void ecryptfs_msg_ctx_free_to_alloc(struct ecryptfs_msg_ctx *msg_ctx)
{
	list_move(&msg_ctx->node, &ecryptfs_msg_ctx_alloc_list);
	msg_ctx->state = ECRYPTFS_MSG_CTX_STATE_PENDING;
	msg_ctx->counter = ++ecryptfs_msg_counter;
}

/**
 * ecryptfs_msg_ctx_alloc_to_free
 * @msg_ctx: The context to move from the alloc list to the free list
 *
 * Must be called with ecryptfs_msg_ctx_lists_mux held.
 */
void ecryptfs_msg_ctx_alloc_to_free(struct ecryptfs_msg_ctx *msg_ctx)
{
	list_move(&(msg_ctx->node), &ecryptfs_msg_ctx_free_list);
	if (msg_ctx->msg)
		kfree(msg_ctx->msg);
	msg_ctx->msg = NULL;
	msg_ctx->state = ECRYPTFS_MSG_CTX_STATE_FREE;
}

/**
 * ecryptfs_find_daemon_by_euid
 * @euid: The effective user id which maps to the desired daemon id
 * @user_ns: The namespace in which @euid applies
 * @daemon: If return value is zero, points to the desired daemon pointer
 *
 * Must be called with ecryptfs_daemon_hash_mux held.
 *
 * Search the hash list for the given user id.
 *
 * Returns zero if the user id exists in the list; non-zero otherwise.
 */
int ecryptfs_find_daemon_by_euid(struct ecryptfs_daemon **daemon, uid_t euid,
				 struct user_namespace *user_ns)
{
	struct hlist_node *elem;
	int rc;

	hlist_for_each_entry(*daemon, elem,
			     &ecryptfs_daemon_hash[ecryptfs_uid_hash(euid)],
			     euid_chain) {
		if ((*daemon)->euid == euid && (*daemon)->user_ns == user_ns) {
			rc = 0;
			goto out;
		}
	}
	rc = -EINVAL;
out:
	return rc;
}

/**
 * ecryptfs_spawn_daemon - Create and initialize a new daemon struct
 * @daemon: Pointer to set to newly allocated daemon struct
 * @euid: Effective user id for the daemon
 * @user_ns: The namespace in which @euid applies
 * @pid: Process id for the daemon
 *
 * Must be called ceremoniously while in possession of
 * ecryptfs_sacred_daemon_hash_mux
 *
 * Returns zero on success; non-zero otherwise
 */
int
ecryptfs_spawn_daemon(struct ecryptfs_daemon **daemon, uid_t euid,
		      struct user_namespace *user_ns, struct pid *pid)
{
	int rc = 0;

	(*daemon) = kzalloc(sizeof(**daemon), GFP_KERNEL);
	if (!(*daemon)) {
		rc = -ENOMEM;
		printk(KERN_ERR "%s: Failed to allocate [%zd] bytes of "
		       "GFP_KERNEL memory\n", __func__, sizeof(**daemon));
		goto out;
	}
	(*daemon)->euid = euid;
	(*daemon)->user_ns = get_user_ns(user_ns);
	(*daemon)->pid = get_pid(pid);
	(*daemon)->task = current;
	mutex_init(&(*daemon)->mux);
	INIT_LIST_HEAD(&(*daemon)->msg_ctx_out_queue);
	init_waitqueue_head(&(*daemon)->wait);
	(*daemon)->num_queued_msg_ctx = 0;
	hlist_add_head(&(*daemon)->euid_chain,
		       &ecryptfs_daemon_hash[ecryptfs_uid_hash(euid)]);
out:
	return rc;
}

/**
 * ecryptfs_exorcise_daemon - Destroy the daemon struct
 *
 * Must be called ceremoniously while in possession of
 * ecryptfs_daemon_hash_mux and the daemon's own mux.
 */
int ecryptfs_exorcise_daemon(struct ecryptfs_daemon *daemon)
{
	struct ecryptfs_msg_ctx *msg_ctx, *msg_ctx_tmp;
	int rc = 0;

	mutex_lock(&daemon->mux);
	if ((daemon->flags & ECRYPTFS_DAEMON_IN_READ)
	    || (daemon->flags & ECRYPTFS_DAEMON_IN_POLL)) {
		rc = -EBUSY;
		printk(KERN_WARNING "%s: Attempt to destroy daemon with pid "
		       "[0x%p], but it is in the midst of a read or a poll\n",
		       __func__, daemon->pid);
		mutex_unlock(&daemon->mux);
		goto out;
	}
	list_for_each_entry_safe(msg_ctx, msg_ctx_tmp,
				 &daemon->msg_ctx_out_queue, daemon_out_list) {
		list_del(&msg_ctx->daemon_out_list);
		daemon->num_queued_msg_ctx--;
		printk(KERN_WARNING "%s: Warning: dropping message that is in "
		       "the out queue of a dying daemon\n", __func__);
		ecryptfs_msg_ctx_alloc_to_free(msg_ctx);
	}
	hlist_del(&daemon->euid_chain);
	if (daemon->task)
		wake_up_process(daemon->task);
	if (daemon->pid)
		put_pid(daemon->pid);
	if (daemon->user_ns)
		put_user_ns(daemon->user_ns);
	mutex_unlock(&daemon->mux);
	kzfree(daemon);
out:
	return rc;
}

/**
 * ecryptfs_process_quit
 * @euid: The user ID owner of the message
 * @user_ns: The namespace in which @euid applies
 * @pid: The process ID for the userspace program that sent the
 *       message
 *
 * Deletes the corresponding daemon for the given euid and pid, if
 * it is the registered that is requesting the deletion. Returns zero
 * after deleting the desired daemon; non-zero otherwise.
 */
int ecryptfs_process_quit(uid_t euid, struct user_namespace *user_ns,
			  struct pid *pid)
{
	struct ecryptfs_daemon *daemon;
	int rc;

	mutex_lock(&ecryptfs_daemon_hash_mux);
	rc = ecryptfs_find_daemon_by_euid(&daemon, euid, user_ns);
	if (rc || !daemon) {
		rc = -EINVAL;
		printk(KERN_ERR "Received request from user [%d] to "
		       "unregister unrecognized daemon [0x%p]\n", euid, pid);
		goto out_unlock;
	}
	rc = ecryptfs_exorcise_daemon(daemon);
out_unlock:
	mutex_unlock(&ecryptfs_daemon_hash_mux);
	return rc;
}

/**
 * ecryptfs_process_reponse
 * @msg: The ecryptfs message received; the caller should sanity check
 *       msg->data_len and free the memory
 * @pid: The process ID of the userspace application that sent the
 *       message
 * @seq: The sequence number of the message; must match the sequence
 *       number for the existing message context waiting for this
 *       response
 *
 * Processes a response message after sending an operation request to
 * userspace. Some other process is awaiting this response. Before
 * sending out its first communications, the other process allocated a
 * msg_ctx from the ecryptfs_msg_ctx_arr at a particular index. The
 * response message contains this index so that we can copy over the
 * response message into the msg_ctx that the process holds a
 * reference to. The other process is going to wake up, check to see
 * that msg_ctx->state == ECRYPTFS_MSG_CTX_STATE_DONE, and then
 * proceed to read off and process the response message. Returns zero
 * upon delivery to desired context element; non-zero upon delivery
 * failure or error.
 *
 * Returns zero on success; non-zero otherwise
 */
int ecryptfs_process_response(struct ecryptfs_message *msg, uid_t euid,
			      struct user_namespace *user_ns, struct pid *pid,
			      u32 seq)
{
	struct ecryptfs_daemon *uninitialized_var(daemon);
	struct ecryptfs_msg_ctx *msg_ctx;
	size_t msg_size;
	struct nsproxy *nsproxy;
	struct user_namespace *tsk_user_ns;
	uid_t ctx_euid;
	int rc;

	if (msg->index >= ecryptfs_message_buf_len) {
		rc = -EINVAL;
		printk(KERN_ERR "%s: Attempt to reference "
		       "context buffer at index [%d]; maximum "
		       "allowable is [%d]\n", __func__, msg->index,
		       (ecryptfs_message_buf_len - 1));
		goto out;
	}
	msg_ctx = &ecryptfs_msg_ctx_arr[msg->index];
	mutex_lock(&msg_ctx->mux);
	mutex_lock(&ecryptfs_daemon_hash_mux);
	rcu_read_lock();
	nsproxy = task_nsproxy(msg_ctx->task);
	if (nsproxy == NULL) {
		rc = -EBADMSG;
		printk(KERN_ERR "%s: Receiving process is a zombie. Dropping "
		       "message.\n", __func__);
		rcu_read_unlock();
		mutex_unlock(&ecryptfs_daemon_hash_mux);
		goto wake_up;
	}
	tsk_user_ns = __task_cred(msg_ctx->task)->user->user_ns;
	ctx_euid = task_euid(msg_ctx->task);
	rc = ecryptfs_find_daemon_by_euid(&daemon, ctx_euid, tsk_user_ns);
	rcu_read_unlock();
	mutex_unlock(&ecryptfs_daemon_hash_mux);
	if (rc) {
		rc = -EBADMSG;
		printk(KERN_WARNING "%s: User [%d] received a "
		       "message response from process [0x%p] but does "
		       "not have a registered daemon\n", __func__,
		       ctx_euid, pid);
		goto wake_up;
	}
	if (ctx_euid != euid) {
		rc = -EBADMSG;
		printk(KERN_WARNING "%s: Received message from user "
		       "[%d]; expected message from user [%d]\n", __func__,
		       euid, ctx_euid);
		goto unlock;
	}
	if (tsk_user_ns != user_ns) {
		rc = -EBADMSG;
		printk(KERN_WARNING "%s: Received message from user_ns "
		       "[0x%p]; expected message from user_ns [0x%p]\n",
		       __func__, user_ns, tsk_user_ns);
		goto unlock;
	}
	if (daemon->pid != pid) {
		rc = -EBADMSG;
		printk(KERN_ERR "%s: User [%d] sent a message response "
		       "from an unrecognized process [0x%p]\n",
		       __func__, ctx_euid, pid);
		goto unlock;
	}
	if (msg_ctx->state != ECRYPTFS_MSG_CTX_STATE_PENDING) {
		rc = -EINVAL;
		printk(KERN_WARNING "%s: Desired context element is not "
		       "pending a response\n", __func__);
		goto unlock;
	} else if (msg_ctx->counter != seq) {
		rc = -EINVAL;
		printk(KERN_WARNING "%s: Invalid message sequence; "
		       "expected [%d]; received [%d]\n", __func__,
		       msg_ctx->counter, seq);
		goto unlock;
	}
	msg_size = (sizeof(*msg) + msg->data_len);
	msg_ctx->msg = kmalloc(msg_size, GFP_KERNEL);
	if (!msg_ctx->msg) {
		rc = -ENOMEM;
		printk(KERN_ERR "%s: Failed to allocate [%zd] bytes of "
		       "GFP_KERNEL memory\n", __func__, msg_size);
		goto unlock;
	}
	memcpy(msg_ctx->msg, msg, msg_size);
	msg_ctx->state = ECRYPTFS_MSG_CTX_STATE_DONE;
	rc = 0;
wake_up:
	wake_up_process(msg_ctx->task);
unlock:
	mutex_unlock(&msg_ctx->mux);
out:
	return rc;
}

/**
 * ecryptfs_send_message_locked
 * @data: The data to send
 * @data_len: The length of data
 * @msg_ctx: The message context allocated for the send
 *
 * Must be called with ecryptfs_daemon_hash_mux held.
 *
 * Returns zero on success; non-zero otherwise
 */
static int
ecryptfs_send_message_locked(char *data, int data_len, u8 msg_type,
			     struct ecryptfs_msg_ctx **msg_ctx)
{
	struct ecryptfs_daemon *daemon;
	uid_t euid = current_euid();
	int rc;

	rc = ecryptfs_find_daemon_by_euid(&daemon, euid, current_user_ns());
	if (rc || !daemon) {
		rc = -ENOTCONN;
		printk(KERN_ERR "%s: User [%d] does not have a daemon "
		       "registered\n", __func__, euid);
		goto out;
	}
	mutex_lock(&ecryptfs_msg_ctx_lists_mux);
	rc = ecryptfs_acquire_free_msg_ctx(msg_ctx);
	if (rc) {
		mutex_unlock(&ecryptfs_msg_ctx_lists_mux);
		printk(KERN_WARNING "%s: Could not claim a free "
		       "context element\n", __func__);
		goto out;
	}
	ecryptfs_msg_ctx_free_to_alloc(*msg_ctx);
	mutex_unlock(&(*msg_ctx)->mux);
	mutex_unlock(&ecryptfs_msg_ctx_lists_mux);
	rc = ecryptfs_send_miscdev(data, data_len, *msg_ctx, msg_type, 0,
				   daemon);
	if (rc)
		printk(KERN_ERR "%s: Error attempting to send message to "
		       "userspace daemon; rc = [%d]\n", __func__, rc);
out:
	return rc;
}

/**
 * ecryptfs_send_message
 * @data: The data to send
 * @data_len: The length of data
 * @msg_ctx: The message context allocated for the send
 *
 * Grabs ecryptfs_daemon_hash_mux.
 *
 * Returns zero on success; non-zero otherwise
 */
int ecryptfs_send_message(char *data, int data_len,
			  struct ecryptfs_msg_ctx **msg_ctx)
{
	int rc;

	mutex_lock(&ecryptfs_daemon_hash_mux);
	rc = ecryptfs_send_message_locked(data, data_len, ECRYPTFS_MSG_REQUEST,
					  msg_ctx);
	mutex_unlock(&ecryptfs_daemon_hash_mux);
	return rc;
}

/**
 * ecryptfs_wait_for_response
 * @msg_ctx: The context that was assigned when sending a message
 * @msg: The incoming message from userspace; not set if rc != 0
 *
 * Sleeps until awaken by ecryptfs_receive_message or until the amount
 * of time exceeds ecryptfs_message_wait_timeout.  If zero is
 * returned, msg will point to a valid message from userspace; a
 * non-zero value is returned upon failure to receive a message or an
 * error occurs. Callee must free @msg on success.
 */
int ecryptfs_wait_for_response(struct ecryptfs_msg_ctx *msg_ctx,
			       struct ecryptfs_message **msg)
{
	signed long timeout = ecryptfs_message_wait_timeout * HZ;
	int rc = 0;

sleep:
	timeout = schedule_timeout_interruptible(timeout);
	mutex_lock(&ecryptfs_msg_ctx_lists_mux);
	mutex_lock(&msg_ctx->mux);
	if (msg_ctx->state != ECRYPTFS_MSG_CTX_STATE_DONE) {
		if (timeout) {
			mutex_unlock(&msg_ctx->mux);
			mutex_unlock(&ecryptfs_msg_ctx_lists_mux);
			goto sleep;
		}
		rc = -ENOMSG;
	} else {
		*msg = msg_ctx->msg;
		msg_ctx->msg = NULL;
	}
	ecryptfs_msg_ctx_alloc_to_free(msg_ctx);
	mutex_unlock(&msg_ctx->mux);
	mutex_unlock(&ecryptfs_msg_ctx_lists_mux);
	return rc;
}

int __init ecryptfs_init_messaging(void)
{
	int i;
	int rc = 0;

	if (ecryptfs_number_of_users > ECRYPTFS_MAX_NUM_USERS) {
		ecryptfs_number_of_users = ECRYPTFS_MAX_NUM_USERS;
		printk(KERN_WARNING "%s: Specified number of users is "
		       "too large, defaulting to [%d] users\n", __func__,
		       ecryptfs_number_of_users);
	}
	mutex_init(&ecryptfs_daemon_hash_mux);
	mutex_lock(&ecryptfs_daemon_hash_mux);
	ecryptfs_hash_bits = 1;
	while (ecryptfs_number_of_users >> ecryptfs_hash_bits)
		ecryptfs_hash_bits++;
	ecryptfs_daemon_hash = kmalloc((sizeof(struct hlist_head)
					* (1 << ecryptfs_hash_bits)),
				       GFP_KERNEL);
	if (!ecryptfs_daemon_hash) {
		rc = -ENOMEM;
		printk(KERN_ERR "%s: Failed to allocate memory\n", __func__);
		mutex_unlock(&ecryptfs_daemon_hash_mux);
		goto out;
	}
	for (i = 0; i < (1 << ecryptfs_hash_bits); i++)
		INIT_HLIST_HEAD(&ecryptfs_daemon_hash[i]);
	mutex_unlock(&ecryptfs_daemon_hash_mux);
	ecryptfs_msg_ctx_arr = kmalloc((sizeof(struct ecryptfs_msg_ctx)
					* ecryptfs_message_buf_len),
				       GFP_KERNEL);
	if (!ecryptfs_msg_ctx_arr) {
		rc = -ENOMEM;
		printk(KERN_ERR "%s: Failed to allocate memory\n", __func__);
		goto out;
	}
	mutex_init(&ecryptfs_msg_ctx_lists_mux);
	mutex_lock(&ecryptfs_msg_ctx_lists_mux);
	ecryptfs_msg_counter = 0;
	for (i = 0; i < ecryptfs_message_buf_len; i++) {
		INIT_LIST_HEAD(&ecryptfs_msg_ctx_arr[i].node);
		INIT_LIST_HEAD(&ecryptfs_msg_ctx_arr[i].daemon_out_list);
		mutex_init(&ecryptfs_msg_ctx_arr[i].mux);
		mutex_lock(&ecryptfs_msg_ctx_arr[i].mux);
		ecryptfs_msg_ctx_arr[i].index = i;
		ecryptfs_msg_ctx_arr[i].state = ECRYPTFS_MSG_CTX_STATE_FREE;
		ecryptfs_msg_ctx_arr[i].counter = 0;
		ecryptfs_msg_ctx_arr[i].task = NULL;
		ecryptfs_msg_ctx_arr[i].msg = NULL;
		list_add_tail(&ecryptfs_msg_ctx_arr[i].node,
			      &ecryptfs_msg_ctx_free_list);
		mutex_unlock(&ecryptfs_msg_ctx_arr[i].mux);
	}
	mutex_unlock(&ecryptfs_msg_ctx_lists_mux);
	rc = ecryptfs_init_ecryptfs_miscdev();
	if (rc)
		ecryptfs_release_messaging();
out:
	return rc;
}

void ecryptfs_release_messaging(void)
{
	if (ecryptfs_msg_ctx_arr) {
		int i;

		mutex_lock(&ecryptfs_msg_ctx_lists_mux);
		for (i = 0; i < ecryptfs_message_buf_len; i++) {
			mutex_lock(&ecryptfs_msg_ctx_arr[i].mux);
			if (ecryptfs_msg_ctx_arr[i].msg)
				kfree(ecryptfs_msg_ctx_arr[i].msg);
			mutex_unlock(&ecryptfs_msg_ctx_arr[i].mux);
		}
		kfree(ecryptfs_msg_ctx_arr);
		mutex_unlock(&ecryptfs_msg_ctx_lists_mux);
	}
	if (ecryptfs_daemon_hash) {
		struct hlist_node *elem;
		struct ecryptfs_daemon *daemon;
		int i;

		mutex_lock(&ecryptfs_daemon_hash_mux);
		for (i = 0; i < (1 << ecryptfs_hash_bits); i++) {
			int rc;

			hlist_for_each_entry(daemon, elem,
					     &ecryptfs_daemon_hash[i],
					     euid_chain) {
				rc = ecryptfs_exorcise_daemon(daemon);
				if (rc)
					printk(KERN_ERR "%s: Error whilst "
					       "attempting to destroy daemon; "
					       "rc = [%d]. Dazed and confused, "
					       "but trying to continue.\n",
					       __func__, rc);
			}
		}
		kfree(ecryptfs_daemon_hash);
		mutex_unlock(&ecryptfs_daemon_hash_mux);
	}
	ecryptfs_destroy_ecryptfs_miscdev();
	return;
}
