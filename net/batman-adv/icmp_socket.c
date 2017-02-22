/* Copyright (C) 2007-2016  B.A.T.M.A.N. contributors:
 *
 * Marek Lindner
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "icmp_socket.h"
#include "main.h"

#include <linux/atomic.h>
#include <linux/compiler.h>
#include <linux/debugfs.h>
#include <linux/errno.h>
#include <linux/etherdevice.h>
#include <linux/export.h>
#include <linux/fcntl.h>
#include <linux/fs.h>
#include <linux/if_ether.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/pkt_sched.h>
#include <linux/poll.h>
#include <linux/printk.h>
#include <linux/sched.h> /* for linux/wait.h */
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/stddef.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

#include "hard-interface.h"
#include "log.h"
#include "originator.h"
#include "packet.h"
#include "send.h"

static struct batadv_socket_client *batadv_socket_client_hash[256];

static void batadv_socket_add_packet(struct batadv_socket_client *socket_client,
				     struct batadv_icmp_header *icmph,
				     size_t icmp_len);

void batadv_socket_init(void)
{
	memset(batadv_socket_client_hash, 0, sizeof(batadv_socket_client_hash));
}

static int batadv_socket_open(struct inode *inode, struct file *file)
{
	unsigned int i;
	struct batadv_socket_client *socket_client;

	if (!try_module_get(THIS_MODULE))
		return -EBUSY;

	nonseekable_open(inode, file);

	socket_client = kmalloc(sizeof(*socket_client), GFP_KERNEL);
	if (!socket_client) {
		module_put(THIS_MODULE);
		return -ENOMEM;
	}

	for (i = 0; i < ARRAY_SIZE(batadv_socket_client_hash); i++) {
		if (!batadv_socket_client_hash[i]) {
			batadv_socket_client_hash[i] = socket_client;
			break;
		}
	}

	if (i == ARRAY_SIZE(batadv_socket_client_hash)) {
		pr_err("Error - can't add another packet client: maximum number of clients reached\n");
		kfree(socket_client);
		module_put(THIS_MODULE);
		return -EXFULL;
	}

	INIT_LIST_HEAD(&socket_client->queue_list);
	socket_client->queue_len = 0;
	socket_client->index = i;
	socket_client->bat_priv = inode->i_private;
	spin_lock_init(&socket_client->lock);
	init_waitqueue_head(&socket_client->queue_wait);

	file->private_data = socket_client;

	return 0;
}

static int batadv_socket_release(struct inode *inode, struct file *file)
{
	struct batadv_socket_client *client = file->private_data;
	struct batadv_socket_packet *packet, *tmp;

	spin_lock_bh(&client->lock);

	/* for all packets in the queue ... */
	list_for_each_entry_safe(packet, tmp, &client->queue_list, list) {
		list_del(&packet->list);
		kfree(packet);
	}

	batadv_socket_client_hash[client->index] = NULL;
	spin_unlock_bh(&client->lock);

	kfree(client);
	module_put(THIS_MODULE);

	return 0;
}

static ssize_t batadv_socket_read(struct file *file, char __user *buf,
				  size_t count, loff_t *ppos)
{
	struct batadv_socket_client *socket_client = file->private_data;
	struct batadv_socket_packet *socket_packet;
	size_t packet_len;
	int error;

	if ((file->f_flags & O_NONBLOCK) && (socket_client->queue_len == 0))
		return -EAGAIN;

	if ((!buf) || (count < sizeof(struct batadv_icmp_packet)))
		return -EINVAL;

	if (!access_ok(VERIFY_WRITE, buf, count))
		return -EFAULT;

	error = wait_event_interruptible(socket_client->queue_wait,
					 socket_client->queue_len);

	if (error)
		return error;

	spin_lock_bh(&socket_client->lock);

	socket_packet = list_first_entry(&socket_client->queue_list,
					 struct batadv_socket_packet, list);
	list_del(&socket_packet->list);
	socket_client->queue_len--;

	spin_unlock_bh(&socket_client->lock);

	packet_len = min(count, socket_packet->icmp_len);
	error = copy_to_user(buf, &socket_packet->icmp_packet, packet_len);

	kfree(socket_packet);

	if (error)
		return -EFAULT;

	return packet_len;
}

static ssize_t batadv_socket_write(struct file *file, const char __user *buff,
				   size_t len, loff_t *off)
{
	struct batadv_socket_client *socket_client = file->private_data;
	struct batadv_priv *bat_priv = socket_client->bat_priv;
	struct batadv_hard_iface *primary_if = NULL;
	struct sk_buff *skb;
	struct batadv_icmp_packet_rr *icmp_packet_rr;
	struct batadv_icmp_header *icmp_header;
	struct batadv_orig_node *orig_node = NULL;
	struct batadv_neigh_node *neigh_node = NULL;
	size_t packet_len = sizeof(struct batadv_icmp_packet);
	u8 *addr;

	if (len < sizeof(struct batadv_icmp_header)) {
		batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
			   "Error - can't send packet from char device: invalid packet size\n");
		return -EINVAL;
	}

	primary_if = batadv_primary_if_get_selected(bat_priv);

	if (!primary_if) {
		len = -EFAULT;
		goto out;
	}

	if (len >= BATADV_ICMP_MAX_PACKET_SIZE)
		packet_len = BATADV_ICMP_MAX_PACKET_SIZE;
	else
		packet_len = len;

	skb = netdev_alloc_skb_ip_align(NULL, packet_len + ETH_HLEN);
	if (!skb) {
		len = -ENOMEM;
		goto out;
	}

	skb->priority = TC_PRIO_CONTROL;
	skb_reserve(skb, ETH_HLEN);
	icmp_header = (struct batadv_icmp_header *)skb_put(skb, packet_len);

	if (copy_from_user(icmp_header, buff, packet_len)) {
		len = -EFAULT;
		goto free_skb;
	}

	if (icmp_header->packet_type != BATADV_ICMP) {
		batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
			   "Error - can't send packet from char device: got bogus packet type (expected: BAT_ICMP)\n");
		len = -EINVAL;
		goto free_skb;
	}

	switch (icmp_header->msg_type) {
	case BATADV_ECHO_REQUEST:
		if (len < sizeof(struct batadv_icmp_packet)) {
			batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
				   "Error - can't send packet from char device: invalid packet size\n");
			len = -EINVAL;
			goto free_skb;
		}

		if (atomic_read(&bat_priv->mesh_state) != BATADV_MESH_ACTIVE)
			goto dst_unreach;

		orig_node = batadv_orig_hash_find(bat_priv, icmp_header->dst);
		if (!orig_node)
			goto dst_unreach;

		neigh_node = batadv_orig_router_get(orig_node,
						    BATADV_IF_DEFAULT);
		if (!neigh_node)
			goto dst_unreach;

		if (!neigh_node->if_incoming)
			goto dst_unreach;

		if (neigh_node->if_incoming->if_status != BATADV_IF_ACTIVE)
			goto dst_unreach;

		icmp_packet_rr = (struct batadv_icmp_packet_rr *)icmp_header;
		if (packet_len == sizeof(*icmp_packet_rr)) {
			addr = neigh_node->if_incoming->net_dev->dev_addr;
			ether_addr_copy(icmp_packet_rr->rr[0], addr);
		}

		break;
	default:
		batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
			   "Error - can't send packet from char device: got unknown message type\n");
		len = -EINVAL;
		goto free_skb;
	}

	icmp_header->uid = socket_client->index;

	if (icmp_header->version != BATADV_COMPAT_VERSION) {
		icmp_header->msg_type = BATADV_PARAMETER_PROBLEM;
		icmp_header->version = BATADV_COMPAT_VERSION;
		batadv_socket_add_packet(socket_client, icmp_header,
					 packet_len);
		goto free_skb;
	}

	ether_addr_copy(icmp_header->orig, primary_if->net_dev->dev_addr);

	batadv_send_unicast_skb(skb, neigh_node);
	goto out;

dst_unreach:
	icmp_header->msg_type = BATADV_DESTINATION_UNREACHABLE;
	batadv_socket_add_packet(socket_client, icmp_header, packet_len);
free_skb:
	kfree_skb(skb);
out:
	if (primary_if)
		batadv_hardif_put(primary_if);
	if (neigh_node)
		batadv_neigh_node_put(neigh_node);
	if (orig_node)
		batadv_orig_node_put(orig_node);
	return len;
}

static unsigned int batadv_socket_poll(struct file *file, poll_table *wait)
{
	struct batadv_socket_client *socket_client = file->private_data;

	poll_wait(file, &socket_client->queue_wait, wait);

	if (socket_client->queue_len > 0)
		return POLLIN | POLLRDNORM;

	return 0;
}

static const struct file_operations batadv_fops = {
	.owner = THIS_MODULE,
	.open = batadv_socket_open,
	.release = batadv_socket_release,
	.read = batadv_socket_read,
	.write = batadv_socket_write,
	.poll = batadv_socket_poll,
	.llseek = no_llseek,
};

int batadv_socket_setup(struct batadv_priv *bat_priv)
{
	struct dentry *d;

	if (!bat_priv->debug_dir)
		goto err;

	d = debugfs_create_file(BATADV_ICMP_SOCKET, 0600, bat_priv->debug_dir,
				bat_priv, &batadv_fops);
	if (!d)
		goto err;

	return 0;

err:
	return -ENOMEM;
}

/**
 * batadv_socket_add_packet - schedule an icmp packet to be sent to
 *  userspace on an icmp socket.
 * @socket_client: the socket this packet belongs to
 * @icmph: pointer to the header of the icmp packet
 * @icmp_len: total length of the icmp packet
 */
static void batadv_socket_add_packet(struct batadv_socket_client *socket_client,
				     struct batadv_icmp_header *icmph,
				     size_t icmp_len)
{
	struct batadv_socket_packet *socket_packet;
	size_t len;

	socket_packet = kmalloc(sizeof(*socket_packet), GFP_ATOMIC);

	if (!socket_packet)
		return;

	len = icmp_len;
	/* check the maximum length before filling the buffer */
	if (len > sizeof(socket_packet->icmp_packet))
		len = sizeof(socket_packet->icmp_packet);

	INIT_LIST_HEAD(&socket_packet->list);
	memcpy(&socket_packet->icmp_packet, icmph, len);
	socket_packet->icmp_len = len;

	spin_lock_bh(&socket_client->lock);

	/* while waiting for the lock the socket_client could have been
	 * deleted
	 */
	if (!batadv_socket_client_hash[icmph->uid]) {
		spin_unlock_bh(&socket_client->lock);
		kfree(socket_packet);
		return;
	}

	list_add_tail(&socket_packet->list, &socket_client->queue_list);
	socket_client->queue_len++;

	if (socket_client->queue_len > 100) {
		socket_packet = list_first_entry(&socket_client->queue_list,
						 struct batadv_socket_packet,
						 list);

		list_del(&socket_packet->list);
		kfree(socket_packet);
		socket_client->queue_len--;
	}

	spin_unlock_bh(&socket_client->lock);

	wake_up(&socket_client->queue_wait);
}

/**
 * batadv_socket_receive_packet - schedule an icmp packet to be received
 *  locally and sent to userspace.
 * @icmph: pointer to the header of the icmp packet
 * @icmp_len: total length of the icmp packet
 */
void batadv_socket_receive_packet(struct batadv_icmp_header *icmph,
				  size_t icmp_len)
{
	struct batadv_socket_client *hash;

	hash = batadv_socket_client_hash[icmph->uid];
	if (hash)
		batadv_socket_add_packet(hash, icmph, icmp_len);
}
