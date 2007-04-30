/*
 *	Linux NET3:	Multicast List maintenance.
 *
 *	Authors:
 *		Tim Kordas <tjk@nostromo.eeap.cwru.edu>
 *		Richard Underwood <richard@wuzz.demon.co.uk>
 *
 *	Stir fried together from the IP multicast and CAP patches above
 *		Alan Cox <Alan.Cox@linux.org>
 *
 *	Fixes:
 *		Alan Cox	:	Update the device on a real delete
 *					rather than any time but...
 *		Alan Cox	:	IFF_ALLMULTI support.
 *		Alan Cox	: 	New format set_multicast_list() calls.
 *		Gleb Natapov    :       Remove dev_mc_lock.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <linux/bitops.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/in.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/if_ether.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/init.h>
#include <net/ip.h>
#include <net/route.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/arp.h>


/*
 *	Device multicast list maintenance.
 *
 *	This is used both by IP and by the user level maintenance functions.
 *	Unlike BSD we maintain a usage count on a given multicast address so
 *	that a casual user application can add/delete multicasts used by
 *	protocols without doing damage to the protocols when it deletes the
 *	entries. It also helps IP as it tracks overlapping maps.
 *
 *	Device mc lists are changed by bh at least if IPv6 is enabled,
 *	so that it must be bh protected.
 *
 *	We block accesses to device mc filters with netif_tx_lock.
 */

/*
 *	Update the multicast list into the physical NIC controller.
 */

static void __dev_mc_upload(struct net_device *dev)
{
	/* Don't do anything till we up the interface
	 * [dev_open will call this function so the list will
	 * stay sane]
	 */

	if (!(dev->flags&IFF_UP))
		return;

	/*
	 *	Devices with no set multicast or which have been
	 *	detached don't get set.
	 */

	if (dev->set_multicast_list == NULL ||
	    !netif_device_present(dev))
		return;

	dev->set_multicast_list(dev);
}

void dev_mc_upload(struct net_device *dev)
{
	netif_tx_lock_bh(dev);
	__dev_mc_upload(dev);
	netif_tx_unlock_bh(dev);
}

/*
 *	Delete a device level multicast
 */

int dev_mc_delete(struct net_device *dev, void *addr, int alen, int glbl)
{
	int err = 0;
	struct dev_mc_list *dmi, **dmip;

	netif_tx_lock_bh(dev);

	for (dmip = &dev->mc_list; (dmi = *dmip) != NULL; dmip = &dmi->next) {
		/*
		 *	Find the entry we want to delete. The device could
		 *	have variable length entries so check these too.
		 */
		if (memcmp(dmi->dmi_addr, addr, dmi->dmi_addrlen) == 0 &&
		    alen == dmi->dmi_addrlen) {
			if (glbl) {
				int old_glbl = dmi->dmi_gusers;
				dmi->dmi_gusers = 0;
				if (old_glbl == 0)
					break;
			}
			if (--dmi->dmi_users)
				goto done;

			/*
			 *	Last user. So delete the entry.
			 */
			*dmip = dmi->next;
			dev->mc_count--;

			kfree(dmi);

			/*
			 *	We have altered the list, so the card
			 *	loaded filter is now wrong. Fix it
			 */
			__dev_mc_upload(dev);

			netif_tx_unlock_bh(dev);
			return 0;
		}
	}
	err = -ENOENT;
done:
	netif_tx_unlock_bh(dev);
	return err;
}

/*
 *	Add a device level multicast
 */

int dev_mc_add(struct net_device *dev, void *addr, int alen, int glbl)
{
	int err = 0;
	struct dev_mc_list *dmi, *dmi1;

	dmi1 = kmalloc(sizeof(*dmi), GFP_ATOMIC);

	netif_tx_lock_bh(dev);
	for (dmi = dev->mc_list; dmi != NULL; dmi = dmi->next) {
		if (memcmp(dmi->dmi_addr, addr, dmi->dmi_addrlen) == 0 &&
		    dmi->dmi_addrlen == alen) {
			if (glbl) {
				int old_glbl = dmi->dmi_gusers;
				dmi->dmi_gusers = 1;
				if (old_glbl)
					goto done;
			}
			dmi->dmi_users++;
			goto done;
		}
	}

	if ((dmi = dmi1) == NULL) {
		netif_tx_unlock_bh(dev);
		return -ENOMEM;
	}
	memcpy(dmi->dmi_addr, addr, alen);
	dmi->dmi_addrlen = alen;
	dmi->next = dev->mc_list;
	dmi->dmi_users = 1;
	dmi->dmi_gusers = glbl ? 1 : 0;
	dev->mc_list = dmi;
	dev->mc_count++;

	__dev_mc_upload(dev);

	netif_tx_unlock_bh(dev);
	return 0;

done:
	netif_tx_unlock_bh(dev);
	kfree(dmi1);
	return err;
}

/*
 *	Discard multicast list when a device is downed
 */

void dev_mc_discard(struct net_device *dev)
{
	netif_tx_lock_bh(dev);

	while (dev->mc_list != NULL) {
		struct dev_mc_list *tmp = dev->mc_list;
		dev->mc_list = tmp->next;
		if (tmp->dmi_users > tmp->dmi_gusers)
			printk("dev_mc_discard: multicast leakage! dmi_users=%d\n", tmp->dmi_users);
		kfree(tmp);
	}
	dev->mc_count = 0;

	netif_tx_unlock_bh(dev);
}

#ifdef CONFIG_PROC_FS
static void *dev_mc_seq_start(struct seq_file *seq, loff_t *pos)
{
	struct net_device *dev;
	loff_t off = 0;

	read_lock(&dev_base_lock);
	for (dev = dev_base; dev; dev = dev->next) {
		if (off++ == *pos)
			return dev;
	}
	return NULL;
}

static void *dev_mc_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct net_device *dev = v;
	++*pos;
	return dev->next;
}

static void dev_mc_seq_stop(struct seq_file *seq, void *v)
{
	read_unlock(&dev_base_lock);
}


static int dev_mc_seq_show(struct seq_file *seq, void *v)
{
	struct dev_mc_list *m;
	struct net_device *dev = v;

	netif_tx_lock_bh(dev);
	for (m = dev->mc_list; m; m = m->next) {
		int i;

		seq_printf(seq, "%-4d %-15s %-5d %-5d ", dev->ifindex,
			   dev->name, m->dmi_users, m->dmi_gusers);

		for (i = 0; i < m->dmi_addrlen; i++)
			seq_printf(seq, "%02x", m->dmi_addr[i]);

		seq_putc(seq, '\n');
	}
	netif_tx_unlock_bh(dev);
	return 0;
}

static const struct seq_operations dev_mc_seq_ops = {
	.start = dev_mc_seq_start,
	.next  = dev_mc_seq_next,
	.stop  = dev_mc_seq_stop,
	.show  = dev_mc_seq_show,
};

static int dev_mc_seq_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &dev_mc_seq_ops);
}

static const struct file_operations dev_mc_seq_fops = {
	.owner	 = THIS_MODULE,
	.open    = dev_mc_seq_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release,
};

#endif

void __init dev_mcast_init(void)
{
	proc_net_fops_create("dev_mcast", 0, &dev_mc_seq_fops);
}

EXPORT_SYMBOL(dev_mc_add);
EXPORT_SYMBOL(dev_mc_delete);
EXPORT_SYMBOL(dev_mc_upload);
