/*********************************************************************
 *
 * Filename:      ircomm_core.c
 * Version:       1.0
 * Description:   IrCOMM service interface
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Sun Jun  6 20:37:34 1999
 * Modified at:   Tue Dec 21 13:26:41 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 *
 *     Copyright (c) 1999 Dag Brattli, All Rights Reserved.
 *     Copyright (c) 2000-2003 Jean Tourrilhes <jt@hpl.hp.com>
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

#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/init.h>

#include <net/irda/irda.h>
#include <net/irda/irmod.h>
#include <net/irda/irlmp.h>
#include <net/irda/iriap.h>
#include <net/irda/irttp.h>
#include <net/irda/irias_object.h>

#include <net/irda/ircomm_event.h>
#include <net/irda/ircomm_lmp.h>
#include <net/irda/ircomm_ttp.h>
#include <net/irda/ircomm_param.h>
#include <net/irda/ircomm_core.h>

static int __ircomm_close(struct ircomm_cb *self);
static void ircomm_control_indication(struct ircomm_cb *self,
				      struct sk_buff *skb, int clen);

#ifdef CONFIG_PROC_FS
extern struct proc_dir_entry *proc_irda;
static int ircomm_seq_open(struct inode *, struct file *);

static const struct file_operations ircomm_proc_fops = {
	.owner		= THIS_MODULE,
	.open           = ircomm_seq_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release	= seq_release,
};
#endif /* CONFIG_PROC_FS */

hashbin_t *ircomm = NULL;

static int __init ircomm_init(void)
{
	ircomm = hashbin_new(HB_LOCK);
	if (ircomm == NULL) {
		IRDA_ERROR("%s(), can't allocate hashbin!\n", __func__);
		return -ENOMEM;
	}

#ifdef CONFIG_PROC_FS
	{ struct proc_dir_entry *ent;
	ent = proc_create("ircomm", 0, proc_irda, &ircomm_proc_fops);
	if (!ent) {
		printk(KERN_ERR "ircomm_init: can't create /proc entry!\n");
		return -ENODEV;
	}
	}
#endif /* CONFIG_PROC_FS */

	IRDA_MESSAGE("IrCOMM protocol (Dag Brattli)\n");

	return 0;
}

static void __exit ircomm_cleanup(void)
{
	IRDA_DEBUG(2, "%s()\n", __func__ );

	hashbin_delete(ircomm, (FREE_FUNC) __ircomm_close);

#ifdef CONFIG_PROC_FS
	remove_proc_entry("ircomm", proc_irda);
#endif /* CONFIG_PROC_FS */
}

/*
 * Function ircomm_open (client_notify)
 *
 *    Start a new IrCOMM instance
 *
 */
struct ircomm_cb *ircomm_open(notify_t *notify, __u8 service_type, int line)
{
	struct ircomm_cb *self = NULL;
	int ret;

	IRDA_DEBUG(2, "%s(), service_type=0x%02x\n", __func__ ,
		   service_type);

	IRDA_ASSERT(ircomm != NULL, return NULL;);

	self = kzalloc(sizeof(struct ircomm_cb), GFP_ATOMIC);
	if (self == NULL)
		return NULL;

	self->notify = *notify;
	self->magic = IRCOMM_MAGIC;

	/* Check if we should use IrLMP or IrTTP */
	if (service_type & IRCOMM_3_WIRE_RAW) {
		self->flow_status = FLOW_START;
		ret = ircomm_open_lsap(self);
	} else
		ret = ircomm_open_tsap(self);

	if (ret < 0) {
		kfree(self);
		return NULL;
	}

	self->service_type = service_type;
	self->line = line;

	hashbin_insert(ircomm, (irda_queue_t *) self, line, NULL);

	ircomm_next_state(self, IRCOMM_IDLE);

	return self;
}

EXPORT_SYMBOL(ircomm_open);

/*
 * Function ircomm_close_instance (self)
 *
 *    Remove IrCOMM instance
 *
 */
static int __ircomm_close(struct ircomm_cb *self)
{
	IRDA_DEBUG(2, "%s()\n", __func__ );

	/* Disconnect link if any */
	ircomm_do_event(self, IRCOMM_DISCONNECT_REQUEST, NULL, NULL);

	/* Remove TSAP */
	if (self->tsap) {
		irttp_close_tsap(self->tsap);
		self->tsap = NULL;
	}

	/* Remove LSAP */
	if (self->lsap) {
		irlmp_close_lsap(self->lsap);
		self->lsap = NULL;
	}
	self->magic = 0;

	kfree(self);

	return 0;
}

/*
 * Function ircomm_close (self)
 *
 *    Closes and removes the specified IrCOMM instance
 *
 */
int ircomm_close(struct ircomm_cb *self)
{
	struct ircomm_cb *entry;

	IRDA_ASSERT(self != NULL, return -EIO;);
	IRDA_ASSERT(self->magic == IRCOMM_MAGIC, return -EIO;);

	IRDA_DEBUG(0, "%s()\n", __func__ );

	entry = hashbin_remove(ircomm, self->line, NULL);

	IRDA_ASSERT(entry == self, return -1;);

	return __ircomm_close(self);
}

EXPORT_SYMBOL(ircomm_close);

/*
 * Function ircomm_connect_request (self, service_type)
 *
 *    Impl. of this function is differ from one of the reference. This
 *    function does discovery as well as sending connect request
 *
 */
int ircomm_connect_request(struct ircomm_cb *self, __u8 dlsap_sel,
			   __u32 saddr, __u32 daddr, struct sk_buff *skb,
			   __u8 service_type)
{
	struct ircomm_info info;
	int ret;

	IRDA_DEBUG(2 , "%s()\n", __func__ );

	IRDA_ASSERT(self != NULL, return -1;);
	IRDA_ASSERT(self->magic == IRCOMM_MAGIC, return -1;);

	self->service_type= service_type;

	info.dlsap_sel = dlsap_sel;
	info.saddr = saddr;
	info.daddr = daddr;

	ret = ircomm_do_event(self, IRCOMM_CONNECT_REQUEST, skb, &info);

	return ret;
}

EXPORT_SYMBOL(ircomm_connect_request);

/*
 * Function ircomm_connect_indication (self, qos, skb)
 *
 *    Notify user layer about the incoming connection
 *
 */
void ircomm_connect_indication(struct ircomm_cb *self, struct sk_buff *skb,
			       struct ircomm_info *info)
{
	int clen = 0;

	IRDA_DEBUG(2, "%s()\n", __func__ );

	/* Check if the packet contains data on the control channel */
	if (skb->len > 0)
		clen = skb->data[0];

	/*
	 * If there are any data hiding in the control channel, we must
	 * deliver it first. The side effect is that the control channel
	 * will be removed from the skb
	 */
	if (self->notify.connect_indication)
		self->notify.connect_indication(self->notify.instance, self,
						info->qos, info->max_data_size,
						info->max_header_size, skb);
	else {
		IRDA_DEBUG(0, "%s(), missing handler\n", __func__ );
	}
}

/*
 * Function ircomm_connect_response (self, userdata, max_sdu_size)
 *
 *    User accepts connection
 *
 */
int ircomm_connect_response(struct ircomm_cb *self, struct sk_buff *userdata)
{
	int ret;

	IRDA_ASSERT(self != NULL, return -1;);
	IRDA_ASSERT(self->magic == IRCOMM_MAGIC, return -1;);

	IRDA_DEBUG(4, "%s()\n", __func__ );

	ret = ircomm_do_event(self, IRCOMM_CONNECT_RESPONSE, userdata, NULL);

	return ret;
}

EXPORT_SYMBOL(ircomm_connect_response);

/*
 * Function connect_confirm (self, skb)
 *
 *    Notify user layer that the link is now connected
 *
 */
void ircomm_connect_confirm(struct ircomm_cb *self, struct sk_buff *skb,
			    struct ircomm_info *info)
{
	IRDA_DEBUG(4, "%s()\n", __func__ );

	if (self->notify.connect_confirm )
		self->notify.connect_confirm(self->notify.instance,
					     self, info->qos,
					     info->max_data_size,
					     info->max_header_size, skb);
	else {
		IRDA_DEBUG(0, "%s(), missing handler\n", __func__ );
	}
}

/*
 * Function ircomm_data_request (self, userdata)
 *
 *    Send IrCOMM data to peer device
 *
 */
int ircomm_data_request(struct ircomm_cb *self, struct sk_buff *skb)
{
	int ret;

	IRDA_DEBUG(4, "%s()\n", __func__ );

	IRDA_ASSERT(self != NULL, return -EFAULT;);
	IRDA_ASSERT(self->magic == IRCOMM_MAGIC, return -EFAULT;);
	IRDA_ASSERT(skb != NULL, return -EFAULT;);

	ret = ircomm_do_event(self, IRCOMM_DATA_REQUEST, skb, NULL);

	return ret;
}

EXPORT_SYMBOL(ircomm_data_request);

/*
 * Function ircomm_data_indication (self, skb)
 *
 *    Data arrived, so deliver it to user
 *
 */
void ircomm_data_indication(struct ircomm_cb *self, struct sk_buff *skb)
{
	IRDA_DEBUG(4, "%s()\n", __func__ );

	IRDA_ASSERT(skb->len > 0, return;);

	if (self->notify.data_indication)
		self->notify.data_indication(self->notify.instance, self, skb);
	else {
		IRDA_DEBUG(0, "%s(), missing handler\n", __func__ );
	}
}

/*
 * Function ircomm_process_data (self, skb)
 *
 *    Data arrived which may contain control channel data
 *
 */
void ircomm_process_data(struct ircomm_cb *self, struct sk_buff *skb)
{
	int clen;

	IRDA_ASSERT(skb->len > 0, return;);

	clen = skb->data[0];

	/*
	 * Input validation check: a stir4200/mcp2150 combinations sometimes
	 * results in frames with clen > remaining packet size. These are
	 * illegal; if we throw away just this frame then it seems to carry on
	 * fine
	 */
	if (unlikely(skb->len < (clen + 1))) {
		IRDA_DEBUG(2, "%s() throwing away illegal frame\n",
			   __func__ );
		return;
	}

	/*
	 * If there are any data hiding in the control channel, we must
	 * deliver it first. The side effect is that the control channel
	 * will be removed from the skb
	 */
	if (clen > 0)
		ircomm_control_indication(self, skb, clen);

	/* Remove control channel from data channel */
	skb_pull(skb, clen+1);

	if (skb->len)
		ircomm_data_indication(self, skb);
	else {
		IRDA_DEBUG(4, "%s(), data was control info only!\n",
			   __func__ );
	}
}

/*
 * Function ircomm_control_request (self, params)
 *
 *    Send control data to peer device
 *
 */
int ircomm_control_request(struct ircomm_cb *self, struct sk_buff *skb)
{
	int ret;

	IRDA_DEBUG(2, "%s()\n", __func__ );

	IRDA_ASSERT(self != NULL, return -EFAULT;);
	IRDA_ASSERT(self->magic == IRCOMM_MAGIC, return -EFAULT;);
	IRDA_ASSERT(skb != NULL, return -EFAULT;);

	ret = ircomm_do_event(self, IRCOMM_CONTROL_REQUEST, skb, NULL);

	return ret;
}

EXPORT_SYMBOL(ircomm_control_request);

/*
 * Function ircomm_control_indication (self, skb)
 *
 *    Data has arrived on the control channel
 *
 */
static void ircomm_control_indication(struct ircomm_cb *self,
				      struct sk_buff *skb, int clen)
{
	IRDA_DEBUG(2, "%s()\n", __func__ );

	/* Use udata for delivering data on the control channel */
	if (self->notify.udata_indication) {
		struct sk_buff *ctrl_skb;

		/* We don't own the skb, so clone it */
		ctrl_skb = skb_clone(skb, GFP_ATOMIC);
		if (!ctrl_skb)
			return;

		/* Remove data channel from control channel */
		skb_trim(ctrl_skb, clen+1);

		self->notify.udata_indication(self->notify.instance, self,
					      ctrl_skb);

		/* Drop reference count -
		 * see ircomm_tty_control_indication(). */
		dev_kfree_skb(ctrl_skb);
	} else {
		IRDA_DEBUG(0, "%s(), missing handler\n", __func__ );
	}
}

/*
 * Function ircomm_disconnect_request (self, userdata, priority)
 *
 *    User layer wants to disconnect the IrCOMM connection
 *
 */
int ircomm_disconnect_request(struct ircomm_cb *self, struct sk_buff *userdata)
{
	struct ircomm_info info;
	int ret;

	IRDA_DEBUG(2, "%s()\n", __func__ );

	IRDA_ASSERT(self != NULL, return -1;);
	IRDA_ASSERT(self->magic == IRCOMM_MAGIC, return -1;);

	ret = ircomm_do_event(self, IRCOMM_DISCONNECT_REQUEST, userdata,
			      &info);
	return ret;
}

EXPORT_SYMBOL(ircomm_disconnect_request);

/*
 * Function disconnect_indication (self, skb)
 *
 *    Tell user that the link has been disconnected
 *
 */
void ircomm_disconnect_indication(struct ircomm_cb *self, struct sk_buff *skb,
				  struct ircomm_info *info)
{
	IRDA_DEBUG(2, "%s()\n", __func__ );

	IRDA_ASSERT(info != NULL, return;);

	if (self->notify.disconnect_indication) {
		self->notify.disconnect_indication(self->notify.instance, self,
						   info->reason, skb);
	} else {
		IRDA_DEBUG(0, "%s(), missing handler\n", __func__ );
	}
}

/*
 * Function ircomm_flow_request (self, flow)
 *
 *
 *
 */
void ircomm_flow_request(struct ircomm_cb *self, LOCAL_FLOW flow)
{
	IRDA_DEBUG(2, "%s()\n", __func__ );

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IRCOMM_MAGIC, return;);

	if (self->service_type == IRCOMM_3_WIRE_RAW)
		return;

	irttp_flow_request(self->tsap, flow);
}

EXPORT_SYMBOL(ircomm_flow_request);

#ifdef CONFIG_PROC_FS
static void *ircomm_seq_start(struct seq_file *seq, loff_t *pos)
{
	struct ircomm_cb *self;
	loff_t off = 0;

	spin_lock_irq(&ircomm->hb_spinlock);

	for (self = (struct ircomm_cb *) hashbin_get_first(ircomm);
	     self != NULL;
	     self = (struct ircomm_cb *) hashbin_get_next(ircomm)) {
		if (off++ == *pos)
			break;

	}
	return self;
}

static void *ircomm_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	++*pos;

	return (void *) hashbin_get_next(ircomm);
}

static void ircomm_seq_stop(struct seq_file *seq, void *v)
{
	spin_unlock_irq(&ircomm->hb_spinlock);
}

static int ircomm_seq_show(struct seq_file *seq, void *v)
{
	const struct ircomm_cb *self = v;

	IRDA_ASSERT(self->magic == IRCOMM_MAGIC, return -EINVAL; );

	if(self->line < 0x10)
		seq_printf(seq, "ircomm%d", self->line);
	else
		seq_printf(seq, "irlpt%d", self->line - 0x10);

	seq_printf(seq,
		   " state: %s, slsap_sel: %#02x, dlsap_sel: %#02x, mode:",
		   ircomm_state[ self->state],
		   self->slsap_sel, self->dlsap_sel);

	if(self->service_type & IRCOMM_3_WIRE_RAW)
		seq_printf(seq, " 3-wire-raw");
	if(self->service_type & IRCOMM_3_WIRE)
		seq_printf(seq, " 3-wire");
	if(self->service_type & IRCOMM_9_WIRE)
		seq_printf(seq, " 9-wire");
	if(self->service_type & IRCOMM_CENTRONICS)
		seq_printf(seq, " Centronics");
	seq_putc(seq, '\n');

	return 0;
}

static const struct seq_operations ircomm_seq_ops = {
	.start  = ircomm_seq_start,
	.next   = ircomm_seq_next,
	.stop   = ircomm_seq_stop,
	.show   = ircomm_seq_show,
};

static int ircomm_seq_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &ircomm_seq_ops);
}
#endif /* CONFIG_PROC_FS */

MODULE_AUTHOR("Dag Brattli <dag@brattli.net>");
MODULE_DESCRIPTION("IrCOMM protocol");
MODULE_LICENSE("GPL");

module_init(ircomm_init);
module_exit(ircomm_cleanup);
