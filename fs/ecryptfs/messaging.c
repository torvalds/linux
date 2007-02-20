/**
 * eCryptfs: Linux filesystem encryption layer
 *
 * Copyright (C) 2004-2006 International Business Machines Corp.
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

#include "ecryptfs_kernel.h"

static LIST_HEAD(ecryptfs_msg_ctx_free_list);
static LIST_HEAD(ecryptfs_msg_ctx_alloc_list);
static struct mutex ecryptfs_msg_ctx_lists_mux;

static struct hlist_head *ecryptfs_daemon_id_hash;
static struct mutex ecryptfs_daemon_id_hash_mux;
static int ecryptfs_hash_buckets;
#define ecryptfs_uid_hash(uid) \
        hash_long((unsigned long)uid, ecryptfs_hash_buckets)

static unsigned int ecryptfs_msg_counter;
static struct ecryptfs_msg_ctx *ecryptfs_msg_ctx_arr;

/**
 * ecryptfs_acquire_free_msg_ctx
 * @msg_ctx: The context that was acquired from the free list
 *
 * Acquires a context element from the free list and locks the mutex
 * on the context.  Returns zero on success; non-zero on error or upon
 * failure to acquire a free context element.  Be sure to lock the
 * list mutex before calling.
 */
static int ecryptfs_acquire_free_msg_ctx(struct ecryptfs_msg_ctx **msg_ctx)
{
	struct list_head *p;
	int rc;

	if (list_empty(&ecryptfs_msg_ctx_free_list)) {
		ecryptfs_printk(KERN_WARNING, "The eCryptfs free "
				"context list is empty.  It may be helpful to "
				"specify the ecryptfs_message_buf_len "
				"parameter to be greater than the current "
				"value of [%d]\n", ecryptfs_message_buf_len);
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
 * Be sure to lock the list mutex and the context mutex before
 * calling.
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
 * Be sure to lock the list mutex and the context mutex before
 * calling.
 */
static void ecryptfs_msg_ctx_alloc_to_free(struct ecryptfs_msg_ctx *msg_ctx)
{
	list_move(&(msg_ctx->node), &ecryptfs_msg_ctx_free_list);
	if (msg_ctx->msg)
		kfree(msg_ctx->msg);
	msg_ctx->state = ECRYPTFS_MSG_CTX_STATE_FREE;
}

/**
 * ecryptfs_find_daemon_id
 * @uid: The user id which maps to the desired daemon id
 * @id: If return value is zero, points to the desired daemon id
 *      pointer
 *
 * Search the hash list for the given user id.  Returns zero if the
 * user id exists in the list; non-zero otherwise.  The daemon id hash
 * mutex should be held before calling this function.
 */
static int ecryptfs_find_daemon_id(uid_t uid, struct ecryptfs_daemon_id **id)
{
	struct hlist_node *elem;
	int rc;

	hlist_for_each_entry(*id, elem,
			     &ecryptfs_daemon_id_hash[ecryptfs_uid_hash(uid)],
			     id_chain) {
		if ((*id)->uid == uid) {
			rc = 0;
			goto out;
		}
	}
	rc = -EINVAL;
out:
	return rc;
}

static int ecryptfs_send_raw_message(unsigned int transport, u16 msg_type,
				     pid_t pid)
{
	int rc;

	switch(transport) {
	case ECRYPTFS_TRANSPORT_NETLINK:
		rc = ecryptfs_send_netlink(NULL, 0, NULL, msg_type, 0, pid);
		break;
	case ECRYPTFS_TRANSPORT_CONNECTOR:
	case ECRYPTFS_TRANSPORT_RELAYFS:
	default:
		rc = -ENOSYS;
	}
	return rc;
}

/**
 * ecryptfs_process_helo
 * @transport: The underlying transport (netlink, etc.)
 * @uid: The user ID owner of the message
 * @pid: The process ID for the userspace program that sent the
 *       message
 *
 * Adds the uid and pid values to the daemon id hash.  If a uid
 * already has a daemon pid registered, the daemon will be
 * unregistered before the new daemon id is put into the hash list.
 * Returns zero after adding a new daemon id to the hash list;
 * non-zero otherwise.
 */
int ecryptfs_process_helo(unsigned int transport, uid_t uid, pid_t pid)
{
	struct ecryptfs_daemon_id *new_id;
	struct ecryptfs_daemon_id *old_id;
	int rc;

	mutex_lock(&ecryptfs_daemon_id_hash_mux);
	new_id = kmalloc(sizeof(*new_id), GFP_KERNEL);
	if (!new_id) {
		rc = -ENOMEM;
		ecryptfs_printk(KERN_ERR, "Failed to allocate memory; unable "
				"to register daemon [%d] for user [%d]\n",
				pid, uid);
		goto unlock;
	}
	if (!ecryptfs_find_daemon_id(uid, &old_id)) {
		printk(KERN_WARNING "Received request from user [%d] "
		       "to register daemon [%d]; unregistering daemon "
		       "[%d]\n", uid, pid, old_id->pid);
		hlist_del(&old_id->id_chain);
		rc = ecryptfs_send_raw_message(transport, ECRYPTFS_NLMSG_QUIT,
					       old_id->pid);
		if (rc)
			printk(KERN_WARNING "Failed to send QUIT "
			       "message to daemon [%d]; rc = [%d]\n",
			       old_id->pid, rc);
		kfree(old_id);
	}
	new_id->uid = uid;
	new_id->pid = pid;
	hlist_add_head(&new_id->id_chain,
		       &ecryptfs_daemon_id_hash[ecryptfs_uid_hash(uid)]);
	rc = 0;
unlock:
	mutex_unlock(&ecryptfs_daemon_id_hash_mux);
	return rc;
}

/**
 * ecryptfs_process_quit
 * @uid: The user ID owner of the message
 * @pid: The process ID for the userspace program that sent the
 *       message
 *
 * Deletes the corresponding daemon id for the given uid and pid, if
 * it is the registered that is requesting the deletion. Returns zero
 * after deleting the desired daemon id; non-zero otherwise.
 */
int ecryptfs_process_quit(uid_t uid, pid_t pid)
{
	struct ecryptfs_daemon_id *id;
	int rc;

	mutex_lock(&ecryptfs_daemon_id_hash_mux);
	if (ecryptfs_find_daemon_id(uid, &id)) {
		rc = -EINVAL;
		ecryptfs_printk(KERN_ERR, "Received request from user [%d] to "
				"unregister unrecognized daemon [%d]\n", uid,
				pid);
		goto unlock;
	}
	if (id->pid != pid) {
		rc = -EINVAL;
		ecryptfs_printk(KERN_WARNING, "Received request from user [%d] "
				"with pid [%d] to unregister daemon [%d]\n",
				uid, pid, id->pid);
		goto unlock;
	}
	hlist_del(&id->id_chain);
	kfree(id);
	rc = 0;
unlock:
	mutex_unlock(&ecryptfs_daemon_id_hash_mux);
	return rc;
}

/**
 * ecryptfs_process_reponse
 * @msg: The ecryptfs message received; the caller should sanity check
 *       msg->data_len
 * @pid: The process ID of the userspace application that sent the
 *       message
 * @seq: The sequence number of the message
 *
 * Processes a response message after sending a operation request to
 * userspace. Returns zero upon delivery to desired context element;
 * non-zero upon delivery failure or error.
 */
int ecryptfs_process_response(struct ecryptfs_message *msg, uid_t uid,
			      pid_t pid, u32 seq)
{
	struct ecryptfs_daemon_id *id;
	struct ecryptfs_msg_ctx *msg_ctx;
	int msg_size;
	int rc;

	if (msg->index >= ecryptfs_message_buf_len) {
		rc = -EINVAL;
		ecryptfs_printk(KERN_ERR, "Attempt to reference "
				"context buffer at index [%d]; maximum "
				"allowable is [%d]\n", msg->index,
				(ecryptfs_message_buf_len - 1));
		goto out;
	}
	msg_ctx = &ecryptfs_msg_ctx_arr[msg->index];
	mutex_lock(&msg_ctx->mux);
	if (ecryptfs_find_daemon_id(msg_ctx->task->euid, &id)) {
		rc = -EBADMSG;
		ecryptfs_printk(KERN_WARNING, "User [%d] received a "
				"message response from process [%d] but does "
				"not have a registered daemon\n",
				msg_ctx->task->euid, pid);
		goto wake_up;
	}
	if (msg_ctx->task->euid != uid) {
		rc = -EBADMSG;
		ecryptfs_printk(KERN_WARNING, "Received message from user "
				"[%d]; expected message from user [%d]\n",
				uid, msg_ctx->task->euid);
		goto unlock;
	}
	if (id->pid != pid) {
		rc = -EBADMSG;
		ecryptfs_printk(KERN_ERR, "User [%d] received a "
				"message response from an unrecognized "
				"process [%d]\n", msg_ctx->task->euid, pid);
		goto unlock;
	}
	if (msg_ctx->state != ECRYPTFS_MSG_CTX_STATE_PENDING) {
		rc = -EINVAL;
		ecryptfs_printk(KERN_WARNING, "Desired context element is not "
				"pending a response\n");
		goto unlock;
	} else if (msg_ctx->counter != seq) {
		rc = -EINVAL;
		ecryptfs_printk(KERN_WARNING, "Invalid message sequence; "
				"expected [%d]; received [%d]\n",
				msg_ctx->counter, seq);
		goto unlock;
	}
	msg_size = sizeof(*msg) + msg->data_len;
	msg_ctx->msg = kmalloc(msg_size, GFP_KERNEL);
	if (!msg_ctx->msg) {
		rc = -ENOMEM;
		ecryptfs_printk(KERN_ERR, "Failed to allocate memory\n");
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
 * ecryptfs_send_message
 * @transport: The transport over which to send the message (i.e.,
 *             netlink)
 * @data: The data to send
 * @data_len: The length of data
 * @msg_ctx: The message context allocated for the send
 */
int ecryptfs_send_message(unsigned int transport, char *data, int data_len,
			  struct ecryptfs_msg_ctx **msg_ctx)
{
	struct ecryptfs_daemon_id *id;
	int rc;

	mutex_lock(&ecryptfs_daemon_id_hash_mux);
	if (ecryptfs_find_daemon_id(current->euid, &id)) {
		mutex_unlock(&ecryptfs_daemon_id_hash_mux);
		rc = -ENOTCONN;
		ecryptfs_printk(KERN_ERR, "User [%d] does not have a daemon "
				"registered\n", current->euid);
		goto out;
	}
	mutex_unlock(&ecryptfs_daemon_id_hash_mux);
	mutex_lock(&ecryptfs_msg_ctx_lists_mux);
	rc = ecryptfs_acquire_free_msg_ctx(msg_ctx);
	if (rc) {
		mutex_unlock(&ecryptfs_msg_ctx_lists_mux);
		ecryptfs_printk(KERN_WARNING, "Could not claim a free "
				"context element\n");
		goto out;
	}
	ecryptfs_msg_ctx_free_to_alloc(*msg_ctx);
	mutex_unlock(&(*msg_ctx)->mux);
	mutex_unlock(&ecryptfs_msg_ctx_lists_mux);
	switch (transport) {
	case ECRYPTFS_TRANSPORT_NETLINK:
		rc = ecryptfs_send_netlink(data, data_len, *msg_ctx,
					   ECRYPTFS_NLMSG_REQUEST, 0, id->pid);
		break;
	case ECRYPTFS_TRANSPORT_CONNECTOR:
	case ECRYPTFS_TRANSPORT_RELAYFS:
	default:
		rc = -ENOSYS;
	}
	if (rc) {
		printk(KERN_ERR "Error attempting to send message to userspace "
		       "daemon; rc = [%d]\n", rc);
	}
out:
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
 * error occurs.
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

int ecryptfs_init_messaging(unsigned int transport)
{
	int i;
	int rc = 0;

	if (ecryptfs_number_of_users > ECRYPTFS_MAX_NUM_USERS) {
		ecryptfs_number_of_users = ECRYPTFS_MAX_NUM_USERS;
		ecryptfs_printk(KERN_WARNING, "Specified number of users is "
				"too large, defaulting to [%d] users\n",
				ecryptfs_number_of_users);
	}
	mutex_init(&ecryptfs_daemon_id_hash_mux);
	mutex_lock(&ecryptfs_daemon_id_hash_mux);
	ecryptfs_hash_buckets = 0;
	while (ecryptfs_number_of_users >> ++ecryptfs_hash_buckets);
	ecryptfs_daemon_id_hash = kmalloc(sizeof(struct hlist_head)
					  * ecryptfs_hash_buckets, GFP_KERNEL);
	if (!ecryptfs_daemon_id_hash) {
		rc = -ENOMEM;
		ecryptfs_printk(KERN_ERR, "Failed to allocate memory\n");
		goto out;
	}
	for (i = 0; i < ecryptfs_hash_buckets; i++)
		INIT_HLIST_HEAD(&ecryptfs_daemon_id_hash[i]);
	mutex_unlock(&ecryptfs_daemon_id_hash_mux);

	ecryptfs_msg_ctx_arr = kmalloc((sizeof(struct ecryptfs_msg_ctx)
				      * ecryptfs_message_buf_len), GFP_KERNEL);
	if (!ecryptfs_msg_ctx_arr) {
		rc = -ENOMEM;
		ecryptfs_printk(KERN_ERR, "Failed to allocate memory\n");
		goto out;
	}
	mutex_init(&ecryptfs_msg_ctx_lists_mux);
	mutex_lock(&ecryptfs_msg_ctx_lists_mux);
	ecryptfs_msg_counter = 0;
	for (i = 0; i < ecryptfs_message_buf_len; i++) {
		INIT_LIST_HEAD(&ecryptfs_msg_ctx_arr[i].node);
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
	switch(transport) {
	case ECRYPTFS_TRANSPORT_NETLINK:
		rc = ecryptfs_init_netlink();
		if (rc)
			ecryptfs_release_messaging(transport);
		break;
	case ECRYPTFS_TRANSPORT_CONNECTOR:
	case ECRYPTFS_TRANSPORT_RELAYFS:
	default:
		rc = -ENOSYS;
	}
out:
	return rc;
}

void ecryptfs_release_messaging(unsigned int transport)
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
	if (ecryptfs_daemon_id_hash) {
		struct hlist_node *elem;
		struct ecryptfs_daemon_id *id;
		int i;

		mutex_lock(&ecryptfs_daemon_id_hash_mux);
		for (i = 0; i < ecryptfs_hash_buckets; i++) {
			hlist_for_each_entry(id, elem,
					     &ecryptfs_daemon_id_hash[i],
					     id_chain) {
				hlist_del(elem);
				kfree(id);
			}
		}
		kfree(ecryptfs_daemon_id_hash);
		mutex_unlock(&ecryptfs_daemon_id_hash_mux);
	}
	switch(transport) {
	case ECRYPTFS_TRANSPORT_NETLINK:
		ecryptfs_release_netlink();
		break;
	case ECRYPTFS_TRANSPORT_CONNECTOR:
	case ECRYPTFS_TRANSPORT_RELAYFS:
	default:
		break;
	}
	return;
}
