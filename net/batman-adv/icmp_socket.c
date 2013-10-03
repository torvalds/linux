/* Copyright (C) 2007-2013 B.A.T.M.A.N. contributors:
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 */

#include "main.h"
#include <linux/debugfs.h>
#include <linux/slab.h>
#include "icmp_socket.h"
#include "send.h"
#include "hash.h"
#include "originator.h"
#include "hard-interface.h"

static struct batadv_socket_client *batadv_socket_client_hash[256];

static void batadv_socket_add_packet(struct batadv_socket_client *socket_client,
				     struct batadv_icmp_packet_rr *icmp_packet,
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
	struct batadv_socket_client *socket_client = file->private_data;
	struct batadv_socket_packet *socket_packet;
	struct list_head *list_pos, *list_pos_tmp;

	spin_lock_bh(&socket_client->lock);

	/* for all packets in the queue ... */
	list_for_each_safe(list_pos, list_pos_tmp, &socket_client->queue_list) {
		socket_packet = list_entry(list_pos,
					   struct batadv_socket_packet, list);

		list_del(list_pos);
		kfree(socket_packet);
	}

	batadv_socket_client_hash[socket_client->index] = NULL;
	spin_unlock_bh(&socket_client->lock);

	kfree(socket_client);
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
	struct batadv_icmp_packet_rr *icmp_packet;

	struct batadv_orig_node *orig_node = NULL;
	struct batadv_neigh_node *neigh_node = NULL;
	size_t packet_len = sizeof(struct batadv_icmp_packet);

	if (len < sizeof(struct batadv_icmp_packet)) {
		batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
			   "Error - can't send packet from char device: invalid packet size\n");
		return -EINVAL;
	}

	primary_if = batadv_primary_if_get_selected(bat_priv);

	if (!primary_if) {
		len = -EFAULT;
		goto out;
	}

	if (len >= sizeof(struct batadv_icmp_packet_rr))
		packet_len = sizeof(struct batadv_icmp_packet_rr);

	skb = netdev_alloc_skb_ip_align(NULL, packet_len + ETH_HLEN);
	if (!skb) {
		len = -ENOMEM;
		goto out;
	}

	skb->priority = TC_PRIO_CONTROL;
	skb_reserve(skb, ETH_HLEN);
	icmp_packet = (struct batadv_icmp_packet_rr *)skb_put(skb, packet_len);

	if (copy_from_user(icmp_packet, buff, packet_len)) {
		len = -EFAULT;
		goto free_skb;
	}

	if (icmp_packet->header.packet_type != BATADV_ICMP) {
		batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
			   "Error - can't send packet from char device: got bogus packet type (expected: BAT_ICMP)\n");
		len = -EINVAL;
		goto free_skb;
	}

	if (icmp_packet->msg_type != BATADV_ECHO_REQUEST) {
		batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
			   "Error - can't send packet from char device: got bogus message type (expected: ECHO_REQUEST)\n");
		len = -EINVAL;
		goto free_skb;
	}

	icmp_packet->uid = socket_client->index;

	if (icmp_packet->header.version != BATADV_COMPAT_VERSION) {
		icmp_packet->msg_type = BATADV_PARAMETER_PROBLEM;
		icmp_packet->header.version = BATADV_COMPAT_VERSION;
		batadv_socket_add_packet(socket_client, icmp_packet,
					 packet_len);
		goto free_skb;
	}

	if (atomic_read(&bat_priv->mesh_state) != BATADV_MESH_ACTIVE)
		goto dst_unreach;

	orig_node = batadv_orig_hash_find(bat_priv, icmp_packet->dst);
	if (!orig_node)
		goto dst_unreach;

	neigh_node = batadv_orig_node_get_router(orig_node);
	if (!neigh_node)
		goto dst_unreach;

	if (!neigh_node->if_incoming)
		goto dst_unreach;

	if (neigh_node->if_incoming->if_status != BATADV_IF_ACTIVE)
		goto dst_unreach;

	memcpy(icmp_packet->orig,
	       primary_if->net_dev->dev_addr, ETH_ALEN);

	if (packet_len == sizeof(struct batadv_icmp_packet_rr))
		memcpy(icmp_packet->rr,
		       neigh_node->if_incoming->net_dev->dev_addr, ETH_ALEN);

	batadv_send_skb_packet(skb, neigh_node->if_incoming, neigh_node->addr);
	goto out;

dst_unreach:
	icmp_packet->msg_type = BATADV_DESTINATION_UNREACHABLE;
	batadv_socket_add_packet(socket_client, icmp_packet, packet_len);
free_skb:
	kfree_skb(skb);
out:
	if (primary_if)
		batadv_hardif_free_ref(primary_if);
	if (neigh_node)
		batadv_neigh_node_free_ref(neigh_node);
	if (orig_node)
		batadv_orig_node_free_ref(orig_node);
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

	d = debugfs_create_file(BATADV_ICMP_SOCKET, S_IFREG | S_IWUSR | S_IRUSR,
				bat_priv->debug_dir, bat_priv, &batadv_fops);
	if (!d)
		goto err;

	return 0;

err:
	return -ENOMEM;
}

static void batadv_socket_add_packet(struct batadv_socket_client *socket_client,
				     struct batadv_icmp_packet_rr *icmp_packet,
				     size_t icmp_len)
{
	struct batadv_socket_packet *socket_packet;

	socket_packet = kmalloc(sizeof(*socket_packet), GFP_ATOMIC);

	if (!socket_packet)
		return;

	INIT_LIST_HEAD(&socket_packet->list);
	memcpy(&socket_packet->icmp_packet, icmp_packet, icmp_len);
	socket_packet->icmp_len = icmp_len;

	spin_lock_bh(&socket_client->lock);

	/* while waiting for the lock the socket_client could have been
	 * deleted
	 */
	if (!batadv_socket_client_hash[icmp_packet->uid]) {
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

void batadv_socket_receive_packet(struct batadv_icmp_packet_rr *icmp_packet,
				  size_t icmp_len)
{
	struct batadv_socket_client *hash;

	hash = batadv_socket_client_hash[icmp_packet->uid];
	if (hash)
		batadv_socket_add_packet(hash, icmp_packet, icmp_len);
}
