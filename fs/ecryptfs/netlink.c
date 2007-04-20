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

#include <net/sock.h>
#include <linux/hash.h>
#include <linux/random.h>
#include "ecryptfs_kernel.h"

static struct sock *ecryptfs_nl_sock;

/**
 * ecryptfs_send_netlink
 * @data: The data to include as the payload
 * @data_len: The byte count of the data
 * @msg_ctx: The netlink context that will be used to handle the
 *          response message
 * @msg_type: The type of netlink message to send
 * @msg_flags: The flags to include in the netlink header
 * @daemon_pid: The process id of the daemon to send the message to
 *
 * Sends the data to the specified daemon pid and uses the netlink
 * context element to store the data needed for validation upon
 * receiving the response.  The data and the netlink context can be
 * null if just sending a netlink header is sufficient.  Returns zero
 * upon sending the message; non-zero upon error.
 */
int ecryptfs_send_netlink(char *data, int data_len,
			  struct ecryptfs_msg_ctx *msg_ctx, u16 msg_type,
			  u16 msg_flags, pid_t daemon_pid)
{
	struct sk_buff *skb;
	struct nlmsghdr *nlh;
	struct ecryptfs_message *msg;
	size_t payload_len;
	int rc;

	payload_len = ((data && data_len) ? (sizeof(*msg) + data_len) : 0);
	skb = alloc_skb(NLMSG_SPACE(payload_len), GFP_KERNEL);
	if (!skb) {
		rc = -ENOMEM;
		ecryptfs_printk(KERN_ERR, "Failed to allocate socket buffer\n");
		goto out;
	}
	nlh = NLMSG_PUT(skb, daemon_pid, msg_ctx ? msg_ctx->counter : 0,
			msg_type, payload_len);
	nlh->nlmsg_flags = msg_flags;
	if (msg_ctx && payload_len) {
		msg = (struct ecryptfs_message *)NLMSG_DATA(nlh);
		msg->index = msg_ctx->index;
		msg->data_len = data_len;
		memcpy(msg->data, data, data_len);
	}
	rc = netlink_unicast(ecryptfs_nl_sock, skb, daemon_pid, 0);
	if (rc < 0) {
		ecryptfs_printk(KERN_ERR, "Failed to send eCryptfs netlink "
				"message; rc = [%d]\n", rc);
		goto out;
	}
	rc = 0;
	goto out;
nlmsg_failure:
	rc = -EMSGSIZE;
	kfree_skb(skb);
out:
	return rc;
}

/**
 * ecryptfs_process_nl_reponse
 * @skb: The socket buffer containing the netlink message of state
 *       RESPONSE
 *
 * Processes a response message after sending a operation request to
 * userspace.  Attempts to assign the msg to a netlink context element
 * at the index specified in the msg.  The sk_buff and nlmsghdr must
 * be validated before this function. Returns zero upon delivery to
 * desired context element; non-zero upon delivery failure or error.
 */
static int ecryptfs_process_nl_response(struct sk_buff *skb)
{
	struct nlmsghdr *nlh = nlmsg_hdr(skb);
	struct ecryptfs_message *msg = NLMSG_DATA(nlh);
	int rc;

	if (skb->len - NLMSG_HDRLEN - sizeof(*msg) != msg->data_len) {
		rc = -EINVAL;
		ecryptfs_printk(KERN_ERR, "Received netlink message with "
				"incorrectly specified data length\n");
		goto out;
	}
	rc = ecryptfs_process_response(msg, NETLINK_CREDS(skb)->uid,
				       NETLINK_CREDS(skb)->pid, nlh->nlmsg_seq);
	if (rc)
		printk(KERN_ERR
		       "Error processing response message; rc = [%d]\n", rc);
out:
	return rc;
}

/**
 * ecryptfs_process_nl_helo
 * @skb: The socket buffer containing the nlmsghdr in HELO state
 *
 * Gets uid and pid of the skb and adds the values to the daemon id
 * hash. Returns zero after adding a new daemon id to the hash list;
 * non-zero otherwise.
 */
static int ecryptfs_process_nl_helo(struct sk_buff *skb)
{
	int rc;

	rc = ecryptfs_process_helo(ECRYPTFS_TRANSPORT_NETLINK,
				   NETLINK_CREDS(skb)->uid,
				   NETLINK_CREDS(skb)->pid);
	if (rc)
		printk(KERN_WARNING "Error processing HELO; rc = [%d]\n", rc);
	return rc;
}

/**
 * ecryptfs_process_nl_quit
 * @skb: The socket buffer containing the nlmsghdr in QUIT state
 *
 * Gets uid and pid of the skb and deletes the corresponding daemon
 * id, if it is the registered that is requesting the
 * deletion. Returns zero after deleting the desired daemon id;
 * non-zero otherwise.
 */
static int ecryptfs_process_nl_quit(struct sk_buff *skb)
{
	int rc;

	rc = ecryptfs_process_quit(NETLINK_CREDS(skb)->uid,
				   NETLINK_CREDS(skb)->pid);
	if (rc)
		printk(KERN_WARNING
		       "Error processing QUIT message; rc = [%d]\n", rc);
	return rc;
}

/**
 * ecryptfs_receive_nl_message
 *
 * Callback function called by netlink system when a message arrives.
 * If the message looks to be valid, then an attempt is made to assign
 * it to its desired netlink context element and wake up the process
 * that is waiting for a response.
 */
static void ecryptfs_receive_nl_message(struct sock *sk, int len)
{
	struct sk_buff *skb;
	struct nlmsghdr *nlh;
	int rc = 0;	/* skb_recv_datagram requires this */

receive:
	skb = skb_recv_datagram(sk, 0, 0, &rc);
	if (rc == -EINTR)
		goto receive;
	else if (rc < 0) {
		ecryptfs_printk(KERN_ERR, "Error occurred while "
				"receiving eCryptfs netlink message; "
				"rc = [%d]\n", rc);
		return;
	}
	nlh = nlmsg_hdr(skb);
	if (!NLMSG_OK(nlh, skb->len)) {
		ecryptfs_printk(KERN_ERR, "Received corrupt netlink "
				"message\n");
		goto free;
	}
	switch (nlh->nlmsg_type) {
		case ECRYPTFS_NLMSG_RESPONSE:
			if (ecryptfs_process_nl_response(skb)) {
				ecryptfs_printk(KERN_WARNING, "Failed to "
						"deliver netlink response to "
						"requesting operation\n");
			}
			break;
		case ECRYPTFS_NLMSG_HELO:
			if (ecryptfs_process_nl_helo(skb)) {
				ecryptfs_printk(KERN_WARNING, "Failed to "
						"fulfill HELO request\n");
			}
			break;
		case ECRYPTFS_NLMSG_QUIT:
			if (ecryptfs_process_nl_quit(skb)) {
				ecryptfs_printk(KERN_WARNING, "Failed to "
						"fulfill QUIT request\n");
			}
			break;
		default:
			ecryptfs_printk(KERN_WARNING, "Dropping netlink "
					"message of unrecognized type [%d]\n",
					nlh->nlmsg_type);
			break;
	}
free:
	kfree_skb(skb);
}

/**
 * ecryptfs_init_netlink
 *
 * Initializes the daemon id hash list, netlink context array, and
 * necessary locks.  Returns zero upon success; non-zero upon error.
 */
int ecryptfs_init_netlink(void)
{
	int rc;

	ecryptfs_nl_sock = netlink_kernel_create(NETLINK_ECRYPTFS, 0,
						 ecryptfs_receive_nl_message,
						 NULL, THIS_MODULE);
	if (!ecryptfs_nl_sock) {
		rc = -EIO;
		ecryptfs_printk(KERN_ERR, "Failed to create netlink socket\n");
		goto out;
	}
	ecryptfs_nl_sock->sk_sndtimeo = ECRYPTFS_DEFAULT_SEND_TIMEOUT;
	rc = 0;
out:
	return rc;
}

/**
 * ecryptfs_release_netlink
 *
 * Frees all memory used by the netlink context array and releases the
 * netlink socket.
 */
void ecryptfs_release_netlink(void)
{
	if (ecryptfs_nl_sock && ecryptfs_nl_sock->sk_socket)
		sock_release(ecryptfs_nl_sock->sk_socket);
	ecryptfs_nl_sock = NULL;
}
