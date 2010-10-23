/*********************************************************************
 *
 * Filename:      discovery.c
 * Version:       0.1
 * Description:   Routines for handling discoveries at the IrLMP layer
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Tue Apr  6 15:33:50 1999
 * Modified at:   Sat Oct  9 17:11:31 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * Modified at:   Fri May 28  3:11 CST 1999
 * Modified by:   Horst von Brand <vonbrand@sleipnir.valparaiso.cl>
 *
 *     Copyright (c) 1999 Dag Brattli, All Rights Reserved.
 *
 *     This program is free software; you can redistribute it and/or
 *     modify it under the terms of the GNU General Public License as
 *     published by the Free Software Foundation; either version 2 of
 *     the License, or (at your option) any later version.
 *
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *     GNU General Public License for more details.
 *
 *     You should have received a copy of the GNU General Public License
 *     along with this program; if not, write to the Free Software
 *     Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 *     MA 02111-1307 USA
 *
 ********************************************************************/

#include <linux/string.h>
#include <linux/socket.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>

#include <net/irda/irda.h>
#include <net/irda/irlmp.h>

#include <net/irda/discovery.h>

#include <asm/unaligned.h>

/*
 * Function irlmp_add_discovery (cachelog, discovery)
 *
 *    Add a new discovery to the cachelog, and remove any old discoveries
 *    from the same device
 *
 * Note : we try to preserve the time this device was *first* discovered
 * (as opposed to the time of last discovery used for cleanup). This is
 * used by clients waiting for discovery events to tell if the device
 * discovered is "new" or just the same old one. They can't rely there
 * on a binary flag (new/old), because not all discovery events are
 * propagated to them, and they might not always listen, so they would
 * miss some new devices popping up...
 * Jean II
 */
void irlmp_add_discovery(hashbin_t *cachelog, discovery_t *new)
{
	discovery_t *discovery, *node;
	unsigned long flags;

	/* Set time of first discovery if node is new (see below) */
	new->firststamp = new->timestamp;

	spin_lock_irqsave(&cachelog->hb_spinlock, flags);

	/*
	 * Remove all discoveries of devices that has previously been
	 * discovered on the same link with the same name (info), or the
	 * same daddr. We do this since some devices (mostly PDAs) change
	 * their device address between every discovery.
	 */
	discovery = (discovery_t *) hashbin_get_first(cachelog);
	while (discovery != NULL ) {
		node = discovery;

		/* Be sure to stay one item ahead */
		discovery = (discovery_t *) hashbin_get_next(cachelog);

		if ((node->data.saddr == new->data.saddr) &&
		    ((node->data.daddr == new->data.daddr) ||
		     (strcmp(node->data.info, new->data.info) == 0)))
		{
			/* This discovery is a previous discovery
			 * from the same device, so just remove it
			 */
			hashbin_remove_this(cachelog, (irda_queue_t *) node);
			/* Check if hints bits are unchanged */
			if (get_unaligned((__u16 *)node->data.hints) == get_unaligned((__u16 *)new->data.hints))
				/* Set time of first discovery for this node */
				new->firststamp = node->firststamp;
			kfree(node);
		}
	}

	/* Insert the new and updated version */
	hashbin_insert(cachelog, (irda_queue_t *) new, new->data.daddr, NULL);

	spin_unlock_irqrestore(&cachelog->hb_spinlock, flags);
}

/*
 * Function irlmp_add_discovery_log (cachelog, log)
 *
 *    Merge a disovery log into the cachelog.
 *
 */
void irlmp_add_discovery_log(hashbin_t *cachelog, hashbin_t *log)
{
	discovery_t *discovery;

	IRDA_DEBUG(4, "%s()\n", __func__);

	/*
	 *  If log is missing this means that IrLAP was unable to perform the
	 *  discovery, so restart discovery again with just the half timeout
	 *  of the normal one.
	 */
	/* Well... It means that there was nobody out there - Jean II */
	if (log == NULL) {
		/* irlmp_start_discovery_timer(irlmp, 150); */
		return;
	}

	/*
	 * Locking : we are the only owner of this discovery log, so
	 * no need to lock it.
	 * We just need to lock the global log in irlmp_add_discovery().
	 */
	discovery = (discovery_t *) hashbin_remove_first(log);
	while (discovery != NULL) {
		irlmp_add_discovery(cachelog, discovery);

		discovery = (discovery_t *) hashbin_remove_first(log);
	}

	/* Delete the now empty log */
	hashbin_delete(log, (FREE_FUNC) kfree);
}

/*
 * Function irlmp_expire_discoveries (log, saddr, force)
 *
 *    Go through all discoveries and expire all that has stayed too long
 *
 * Note : this assume that IrLAP won't change its saddr, which
 * currently is a valid assumption...
 */
void irlmp_expire_discoveries(hashbin_t *log, __u32 saddr, int force)
{
	discovery_t *		discovery;
	discovery_t *		curr;
	unsigned long		flags;
	discinfo_t *		buffer = NULL;
	int			n;		/* Size of the full log */
	int			i = 0;		/* How many we expired */

	IRDA_ASSERT(log != NULL, return;);
	IRDA_DEBUG(4, "%s()\n", __func__);

	spin_lock_irqsave(&log->hb_spinlock, flags);

	discovery = (discovery_t *) hashbin_get_first(log);
	while (discovery != NULL) {
		/* Be sure to be one item ahead */
		curr = discovery;
		discovery = (discovery_t *) hashbin_get_next(log);

		/* Test if it's time to expire this discovery */
		if ((curr->data.saddr == saddr) &&
		    (force ||
		     ((jiffies - curr->timestamp) > DISCOVERY_EXPIRE_TIMEOUT)))
		{
			/* Create buffer as needed.
			 * As this function get called a lot and most time
			 * we don't have anything to put in the log (we are
			 * quite picky), we can save a lot of overhead
			 * by not calling kmalloc. Jean II */
			if(buffer == NULL) {
				/* Create the client specific buffer */
				n = HASHBIN_GET_SIZE(log);
				buffer = kmalloc(n * sizeof(struct irda_device_info), GFP_ATOMIC);
				if (buffer == NULL) {
					spin_unlock_irqrestore(&log->hb_spinlock, flags);
					return;
				}

			}

			/* Copy discovery information */
			memcpy(&(buffer[i]), &(curr->data),
			       sizeof(discinfo_t));
			i++;

			/* Remove it from the log */
			curr = hashbin_remove_this(log, (irda_queue_t *) curr);
			kfree(curr);
		}
	}

	/* Drop the spinlock before calling the higher layers, as
	 * we can't guarantee they won't call us back and create a
	 * deadlock. We will work on our own private data, so we
	 * don't care to be interrupted. - Jean II */
	spin_unlock_irqrestore(&log->hb_spinlock, flags);

	if(buffer == NULL)
		return;

	/* Tell IrLMP and registered clients about it */
	irlmp_discovery_expiry(buffer, i);

	/* Free up our buffer */
	kfree(buffer);
}

#if 0
/*
 * Function irlmp_dump_discoveries (log)
 *
 *    Print out all discoveries in log
 *
 */
void irlmp_dump_discoveries(hashbin_t *log)
{
	discovery_t *discovery;

	IRDA_ASSERT(log != NULL, return;);

	discovery = (discovery_t *) hashbin_get_first(log);
	while (discovery != NULL) {
		IRDA_DEBUG(0, "Discovery:\n");
		IRDA_DEBUG(0, "  daddr=%08x\n", discovery->data.daddr);
		IRDA_DEBUG(0, "  saddr=%08x\n", discovery->data.saddr);
		IRDA_DEBUG(0, "  nickname=%s\n", discovery->data.info);

		discovery = (discovery_t *) hashbin_get_next(log);
	}
}
#endif

/*
 * Function irlmp_copy_discoveries (log, pn, mask)
 *
 *    Copy all discoveries in a buffer
 *
 * This function implement a safe way for lmp clients to access the
 * discovery log. The basic problem is that we don't want the log
 * to change (add/remove) while the client is reading it. If the
 * lmp client manipulate directly the hashbin, he is sure to get
 * into troubles...
 * The idea is that we copy all the current discovery log in a buffer
 * which is specific to the client and pass this copy to him. As we
 * do this operation with the spinlock grabbed, we are safe...
 * Note : we don't want those clients to grab the spinlock, because
 * we have no control on how long they will hold it...
 * Note : we choose to copy the log in "struct irda_device_info" to
 * save space...
 * Note : the client must kfree himself() the log...
 * Jean II
 */
struct irda_device_info *irlmp_copy_discoveries(hashbin_t *log, int *pn,
						__u16 mask, int old_entries)
{
	discovery_t *		discovery;
	unsigned long		flags;
	discinfo_t *		buffer = NULL;
	int			j_timeout = (sysctl_discovery_timeout * HZ);
	int			n;		/* Size of the full log */
	int			i = 0;		/* How many we picked */

	IRDA_ASSERT(pn != NULL, return NULL;);
	IRDA_ASSERT(log != NULL, return NULL;);

	/* Save spin lock */
	spin_lock_irqsave(&log->hb_spinlock, flags);

	discovery = (discovery_t *) hashbin_get_first(log);
	while (discovery != NULL) {
		/* Mask out the ones we don't want :
		 * We want to match the discovery mask, and to get only
		 * the most recent one (unless we want old ones) */
		if ((get_unaligned((__u16 *)discovery->data.hints) & mask) &&
		    ((old_entries) ||
		     ((jiffies - discovery->firststamp) < j_timeout))) {
			/* Create buffer as needed.
			 * As this function get called a lot and most time
			 * we don't have anything to put in the log (we are
			 * quite picky), we can save a lot of overhead
			 * by not calling kmalloc. Jean II */
			if(buffer == NULL) {
				/* Create the client specific buffer */
				n = HASHBIN_GET_SIZE(log);
				buffer = kmalloc(n * sizeof(struct irda_device_info), GFP_ATOMIC);
				if (buffer == NULL) {
					spin_unlock_irqrestore(&log->hb_spinlock, flags);
					return NULL;
				}

			}

			/* Copy discovery information */
			memcpy(&(buffer[i]), &(discovery->data),
			       sizeof(discinfo_t));
			i++;
		}
		discovery = (discovery_t *) hashbin_get_next(log);
	}

	spin_unlock_irqrestore(&log->hb_spinlock, flags);

	/* Get the actual number of device in the buffer and return */
	*pn = i;
	return buffer;
}

#ifdef CONFIG_PROC_FS
static inline discovery_t *discovery_seq_idx(loff_t pos)

{
	discovery_t *discovery;

	for (discovery = (discovery_t *) hashbin_get_first(irlmp->cachelog);
	     discovery != NULL;
	     discovery = (discovery_t *) hashbin_get_next(irlmp->cachelog)) {
		if (pos-- == 0)
			break;
	}

	return discovery;
}

static void *discovery_seq_start(struct seq_file *seq, loff_t *pos)
{
	spin_lock_irq(&irlmp->cachelog->hb_spinlock);
	return *pos ? discovery_seq_idx(*pos - 1) : SEQ_START_TOKEN;
}

static void *discovery_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	++*pos;
	return (v == SEQ_START_TOKEN)
		? (void *) hashbin_get_first(irlmp->cachelog)
		: (void *) hashbin_get_next(irlmp->cachelog);
}

static void discovery_seq_stop(struct seq_file *seq, void *v)
{
	spin_unlock_irq(&irlmp->cachelog->hb_spinlock);
}

static int discovery_seq_show(struct seq_file *seq, void *v)
{
	if (v == SEQ_START_TOKEN)
		seq_puts(seq, "IrLMP: Discovery log:\n\n");
	else {
		const discovery_t *discovery = v;

		seq_printf(seq, "nickname: %s, hint: 0x%02x%02x",
			   discovery->data.info,
			   discovery->data.hints[0],
			   discovery->data.hints[1]);
#if 0
		if ( discovery->data.hints[0] & HINT_PNP)
			seq_puts(seq, "PnP Compatible ");
		if ( discovery->data.hints[0] & HINT_PDA)
			seq_puts(seq, "PDA/Palmtop ");
		if ( discovery->data.hints[0] & HINT_COMPUTER)
			seq_puts(seq, "Computer ");
		if ( discovery->data.hints[0] & HINT_PRINTER)
			seq_puts(seq, "Printer ");
		if ( discovery->data.hints[0] & HINT_MODEM)
			seq_puts(seq, "Modem ");
		if ( discovery->data.hints[0] & HINT_FAX)
			seq_puts(seq, "Fax ");
		if ( discovery->data.hints[0] & HINT_LAN)
			seq_puts(seq, "LAN Access ");

		if ( discovery->data.hints[1] & HINT_TELEPHONY)
			seq_puts(seq, "Telephony ");
		if ( discovery->data.hints[1] & HINT_FILE_SERVER)
			seq_puts(seq, "File Server ");
		if ( discovery->data.hints[1] & HINT_COMM)
			seq_puts(seq, "IrCOMM ");
		if ( discovery->data.hints[1] & HINT_OBEX)
			seq_puts(seq, "IrOBEX ");
#endif
		seq_printf(seq,", saddr: 0x%08x, daddr: 0x%08x\n\n",
			       discovery->data.saddr,
			       discovery->data.daddr);

		seq_putc(seq, '\n');
	}
	return 0;
}

static const struct seq_operations discovery_seq_ops = {
	.start  = discovery_seq_start,
	.next   = discovery_seq_next,
	.stop   = discovery_seq_stop,
	.show   = discovery_seq_show,
};

static int discovery_seq_open(struct inode *inode, struct file *file)
{
	IRDA_ASSERT(irlmp != NULL, return -EINVAL;);

	return seq_open(file, &discovery_seq_ops);
}

const struct file_operations discovery_seq_fops = {
	.owner		= THIS_MODULE,
	.open           = discovery_seq_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release	= seq_release,
};
#endif
