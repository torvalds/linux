/*********************************************************************
 *
 * Filename:      irlan_common.c
 * Version:       0.9
 * Description:   IrDA LAN Access Protocol Implementation
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Sun Aug 31 20:14:37 1997
 * Modified at:   Sun Dec 26 21:53:10 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 *
 *     Copyright (c) 1997, 1999 Dag Brattli <dagb@cs.uit.no>,
 *     All Rights Reserved.
 *
 *     This program is free software; you can redistribute it and/or
 *     modify it under the terms of the GNU General Public License as
 *     published by the Free Software Foundation; either version 2 of
 *     the License, or (at your option) any later version.
 *
 *     Neither Dag Brattli nor University of Tromsø admit liability nor
 *     provide warranty for any of this software. This material is
 *     provided "AS-IS" and at no charge.
 *
 ********************************************************************/

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/random.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/rtnetlink.h>
#include <linux/moduleparam.h>
#include <linux/bitops.h>

#include <asm/system.h>
#include <asm/byteorder.h>

#include <net/irda/irda.h>
#include <net/irda/irttp.h>
#include <net/irda/irlmp.h>
#include <net/irda/iriap.h>
#include <net/irda/timer.h>

#include <net/irda/irlan_common.h>
#include <net/irda/irlan_client.h>
#include <net/irda/irlan_provider.h>
#include <net/irda/irlan_eth.h>
#include <net/irda/irlan_filter.h>


/* extern char sysctl_devname[]; */

/*
 *  Master structure
 */
static LIST_HEAD(irlans);

static void *ckey;
static void *skey;

/* Module parameters */
static int eth;   /* Use "eth" or "irlan" name for devices */
static int access = ACCESS_PEER; /* PEER, DIRECT or HOSTED */

#ifdef CONFIG_PROC_FS
static const char *const irlan_access[] = {
	"UNKNOWN",
	"DIRECT",
	"PEER",
	"HOSTED"
};

static const char *const irlan_media[] = {
	"UNKNOWN",
	"802.3",
	"802.5"
};

extern struct proc_dir_entry *proc_irda;

static int irlan_seq_open(struct inode *inode, struct file *file);

static const struct file_operations irlan_fops = {
	.owner	 = THIS_MODULE,
	.open    = irlan_seq_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release,
};

extern struct proc_dir_entry *proc_irda;
#endif /* CONFIG_PROC_FS */

static struct irlan_cb *irlan_open(__u32 saddr, __u32 daddr);
static void __irlan_close(struct irlan_cb *self);
static int __irlan_insert_param(struct sk_buff *skb, char *param, int type,
				__u8 value_byte, __u16 value_short,
				__u8 *value_array, __u16 value_len);
static void irlan_open_unicast_addr(struct irlan_cb *self);
static void irlan_get_unicast_addr(struct irlan_cb *self);
void irlan_close_tsaps(struct irlan_cb *self);

/*
 * Function irlan_init (void)
 *
 *    Initialize IrLAN layer
 *
 */
static int __init irlan_init(void)
{
	struct irlan_cb *new;
	__u16 hints;

	IRDA_DEBUG(2, "%s()\n", __func__ );

#ifdef CONFIG_PROC_FS
	{ struct proc_dir_entry *proc;
	proc = proc_create("irlan", 0, proc_irda, &irlan_fops);
	if (!proc) {
		printk(KERN_ERR "irlan_init: can't create /proc entry!\n");
		return -ENODEV;
	}
	}
#endif /* CONFIG_PROC_FS */

	IRDA_DEBUG(4, "%s()\n", __func__ );
	hints = irlmp_service_to_hint(S_LAN);

	/* Register with IrLMP as a client */
	ckey = irlmp_register_client(hints, &irlan_client_discovery_indication,
				     NULL, NULL);
	if (!ckey)
		goto err_ckey;

	/* Register with IrLMP as a service */
	skey = irlmp_register_service(hints);
	if (!skey)
		goto err_skey;

	/* Start the master IrLAN instance (the only one for now) */
	new = irlan_open(DEV_ADDR_ANY, DEV_ADDR_ANY);
	if (!new)
		goto err_open;

	/* The master will only open its (listen) control TSAP */
	irlan_provider_open_ctrl_tsap(new);

	/* Do some fast discovery! */
	irlmp_discovery_request(DISCOVERY_DEFAULT_SLOTS);

	return 0;

err_open:
	irlmp_unregister_service(skey);
err_skey:
	irlmp_unregister_client(ckey);
err_ckey:
#ifdef CONFIG_PROC_FS
	remove_proc_entry("irlan", proc_irda);
#endif /* CONFIG_PROC_FS */

	return -ENOMEM;
}

static void __exit irlan_cleanup(void)
{
	struct irlan_cb *self, *next;

	IRDA_DEBUG(4, "%s()\n", __func__ );

	irlmp_unregister_client(ckey);
	irlmp_unregister_service(skey);

#ifdef CONFIG_PROC_FS
	remove_proc_entry("irlan", proc_irda);
#endif /* CONFIG_PROC_FS */

	/* Cleanup any leftover network devices */
	rtnl_lock();
	list_for_each_entry_safe(self, next, &irlans, dev_list) {
		__irlan_close(self);
	}
	rtnl_unlock();
}

/*
 * Function irlan_open (void)
 *
 *    Open new instance of a client/provider, we should only register the
 *    network device if this instance is ment for a particular client/provider
 */
static struct irlan_cb *irlan_open(__u32 saddr, __u32 daddr)
{
	struct net_device *dev;
	struct irlan_cb *self;

	IRDA_DEBUG(2, "%s()\n", __func__ );

	/* Create network device with irlan */
	dev = alloc_irlandev(eth ? "eth%d" : "irlan%d");
	if (!dev)
		return NULL;

	self = netdev_priv(dev);
	self->dev = dev;

	/*
	 *  Initialize local device structure
	 */
	self->magic = IRLAN_MAGIC;
	self->saddr = saddr;
	self->daddr = daddr;

	/* Provider access can only be PEER, DIRECT, or HOSTED */
	self->provider.access_type = access;
	if (access == ACCESS_DIRECT) {
		/*
		 * Since we are emulating an IrLAN sever we will have to
		 * give ourself an ethernet address!
		 */
		dev->dev_addr[0] = 0x40;
		dev->dev_addr[1] = 0x00;
		dev->dev_addr[2] = 0x00;
		dev->dev_addr[3] = 0x00;
		get_random_bytes(dev->dev_addr+4, 1);
		get_random_bytes(dev->dev_addr+5, 1);
	}

	self->media = MEDIA_802_3;
	self->disconnect_reason = LM_USER_REQUEST;
	init_timer(&self->watchdog_timer);
	init_timer(&self->client.kick_timer);
	init_waitqueue_head(&self->open_wait);

	skb_queue_head_init(&self->client.txq);

	irlan_next_client_state(self, IRLAN_IDLE);
	irlan_next_provider_state(self, IRLAN_IDLE);

	if (register_netdev(dev)) {
		IRDA_DEBUG(2, "%s(), register_netdev() failed!\n",
			   __func__ );
		self = NULL;
		free_netdev(dev);
	} else {
		rtnl_lock();
		list_add_rcu(&self->dev_list, &irlans);
		rtnl_unlock();
	}

	return self;
}
/*
 * Function __irlan_close (self)
 *
 *    This function closes and deallocates the IrLAN client instances. Be
 *    aware that other functions which calls client_close() must
 *    remove self from irlans list first.
 */
static void __irlan_close(struct irlan_cb *self)
{
	IRDA_DEBUG(2, "%s()\n", __func__ );

	ASSERT_RTNL();
	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IRLAN_MAGIC, return;);

	del_timer_sync(&self->watchdog_timer);
	del_timer_sync(&self->client.kick_timer);

	/* Close all open connections and remove TSAPs */
	irlan_close_tsaps(self);

	if (self->client.iriap)
		iriap_close(self->client.iriap);

	/* Remove frames queued on the control channel */
	skb_queue_purge(&self->client.txq);

	/* Unregister and free self via destructor */
	unregister_netdevice(self->dev);
}

/* Find any instance of irlan, used for client discovery wakeup */
struct irlan_cb *irlan_get_any(void)
{
	struct irlan_cb *self;

	list_for_each_entry_rcu(self, &irlans, dev_list) {
		return self;
	}
	return NULL;
}

/*
 * Function irlan_connect_indication (instance, sap, qos, max_sdu_size, skb)
 *
 *    Here we receive the connect indication for the data channel
 *
 */
static void irlan_connect_indication(void *instance, void *sap,
				     struct qos_info *qos,
				     __u32 max_sdu_size,
				     __u8 max_header_size,
				     struct sk_buff *skb)
{
	struct irlan_cb *self;
	struct tsap_cb *tsap;

	IRDA_DEBUG(2, "%s()\n", __func__ );

	self = (struct irlan_cb *) instance;
	tsap = (struct tsap_cb *) sap;

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IRLAN_MAGIC, return;);
	IRDA_ASSERT(tsap == self->tsap_data,return;);

	self->max_sdu_size = max_sdu_size;
	self->max_header_size = max_header_size;

	IRDA_DEBUG(0, "%s: We are now connected!\n", __func__);

	del_timer(&self->watchdog_timer);

	/* If you want to pass the skb to *both* state machines, you will
	 * need to skb_clone() it, so that you don't free it twice.
	 * As the state machines don't need it, git rid of it here...
	 * Jean II */
	if (skb)
		dev_kfree_skb(skb);

	irlan_do_provider_event(self, IRLAN_DATA_CONNECT_INDICATION, NULL);
	irlan_do_client_event(self, IRLAN_DATA_CONNECT_INDICATION, NULL);

	if (self->provider.access_type == ACCESS_PEER) {
		/*
		 * Data channel is open, so we are now allowed to
		 * configure the remote filter
		 */
		irlan_get_unicast_addr(self);
		irlan_open_unicast_addr(self);
	}
	/* Ready to transfer Ethernet frames (at last) */
	netif_start_queue(self->dev); /* Clear reason */
}

static void irlan_connect_confirm(void *instance, void *sap,
				  struct qos_info *qos,
				  __u32 max_sdu_size,
				  __u8 max_header_size,
				  struct sk_buff *skb)
{
	struct irlan_cb *self;

	self = (struct irlan_cb *) instance;

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IRLAN_MAGIC, return;);

	self->max_sdu_size = max_sdu_size;
	self->max_header_size = max_header_size;

	/* TODO: we could set the MTU depending on the max_sdu_size */

	IRDA_DEBUG(0, "%s: We are now connected!\n", __func__);
	del_timer(&self->watchdog_timer);

	/*
	 * Data channel is open, so we are now allowed to configure the remote
	 * filter
	 */
	irlan_get_unicast_addr(self);
	irlan_open_unicast_addr(self);

	/* Open broadcast and multicast filter by default */
	irlan_set_broadcast_filter(self, TRUE);
	irlan_set_multicast_filter(self, TRUE);

	/* Ready to transfer Ethernet frames */
	netif_start_queue(self->dev);
	self->disconnect_reason = 0; /* Clear reason */
	wake_up_interruptible(&self->open_wait);
}

/*
 * Function irlan_client_disconnect_indication (handle)
 *
 *    Callback function for the IrTTP layer. Indicates a disconnection of
 *    the specified connection (handle)
 */
static void irlan_disconnect_indication(void *instance,
					void *sap, LM_REASON reason,
					struct sk_buff *userdata)
{
	struct irlan_cb *self;
	struct tsap_cb *tsap;

	IRDA_DEBUG(0, "%s(), reason=%d\n", __func__ , reason);

	self = (struct irlan_cb *) instance;
	tsap = (struct tsap_cb *) sap;

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IRLAN_MAGIC, return;);
	IRDA_ASSERT(tsap != NULL, return;);
	IRDA_ASSERT(tsap->magic == TTP_TSAP_MAGIC, return;);

	IRDA_ASSERT(tsap == self->tsap_data, return;);

	IRDA_DEBUG(2, "IrLAN, data channel disconnected by peer!\n");

	/* Save reason so we know if we should try to reconnect or not */
	self->disconnect_reason = reason;

	switch (reason) {
	case LM_USER_REQUEST: /* User request */
		IRDA_DEBUG(2, "%s(), User requested\n", __func__ );
		break;
	case LM_LAP_DISCONNECT: /* Unexpected IrLAP disconnect */
		IRDA_DEBUG(2, "%s(), Unexpected IrLAP disconnect\n", __func__ );
		break;
	case LM_CONNECT_FAILURE: /* Failed to establish IrLAP connection */
		IRDA_DEBUG(2, "%s(), IrLAP connect failed\n", __func__ );
		break;
	case LM_LAP_RESET:  /* IrLAP reset */
		IRDA_DEBUG(2, "%s(), IrLAP reset\n", __func__ );
		break;
	case LM_INIT_DISCONNECT:
		IRDA_DEBUG(2, "%s(), IrLMP connect failed\n", __func__ );
		break;
	default:
		IRDA_ERROR("%s(), Unknown disconnect reason\n", __func__);
		break;
	}

	/* If you want to pass the skb to *both* state machines, you will
	 * need to skb_clone() it, so that you don't free it twice.
	 * As the state machines don't need it, git rid of it here...
	 * Jean II */
	if (userdata)
		dev_kfree_skb(userdata);

	irlan_do_client_event(self, IRLAN_LMP_DISCONNECT, NULL);
	irlan_do_provider_event(self, IRLAN_LMP_DISCONNECT, NULL);

	wake_up_interruptible(&self->open_wait);
}

void irlan_open_data_tsap(struct irlan_cb *self)
{
	struct tsap_cb *tsap;
	notify_t notify;

	IRDA_DEBUG(2, "%s()\n", __func__ );

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IRLAN_MAGIC, return;);

	/* Check if already open */
	if (self->tsap_data)
		return;

	irda_notify_init(&notify);

	notify.data_indication       = irlan_eth_receive;
	notify.udata_indication      = irlan_eth_receive;
	notify.connect_indication    = irlan_connect_indication;
	notify.connect_confirm       = irlan_connect_confirm;
	notify.flow_indication       = irlan_eth_flow_indication;
	notify.disconnect_indication = irlan_disconnect_indication;
	notify.instance              = self;
	strlcpy(notify.name, "IrLAN data", sizeof(notify.name));

	tsap = irttp_open_tsap(LSAP_ANY, DEFAULT_INITIAL_CREDIT, &notify);
	if (!tsap) {
		IRDA_DEBUG(2, "%s(), Got no tsap!\n", __func__ );
		return;
	}
	self->tsap_data = tsap;

	/*
	 *  This is the data TSAP selector which we will pass to the client
	 *  when the client ask for it.
	 */
	self->stsap_sel_data = self->tsap_data->stsap_sel;
}

void irlan_close_tsaps(struct irlan_cb *self)
{
	IRDA_DEBUG(4, "%s()\n", __func__ );

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IRLAN_MAGIC, return;);

	/* Disconnect and close all open TSAP connections */
	if (self->tsap_data) {
		irttp_disconnect_request(self->tsap_data, NULL, P_NORMAL);
		irttp_close_tsap(self->tsap_data);
		self->tsap_data = NULL;
	}
	if (self->client.tsap_ctrl) {
		irttp_disconnect_request(self->client.tsap_ctrl, NULL,
					 P_NORMAL);
		irttp_close_tsap(self->client.tsap_ctrl);
		self->client.tsap_ctrl = NULL;
	}
	if (self->provider.tsap_ctrl) {
		irttp_disconnect_request(self->provider.tsap_ctrl, NULL,
					 P_NORMAL);
		irttp_close_tsap(self->provider.tsap_ctrl);
		self->provider.tsap_ctrl = NULL;
	}
	self->disconnect_reason = LM_USER_REQUEST;
}

/*
 * Function irlan_ias_register (self, tsap_sel)
 *
 *    Register with LM-IAS
 *
 */
void irlan_ias_register(struct irlan_cb *self, __u8 tsap_sel)
{
	struct ias_object *obj;
	struct ias_value *new_value;

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IRLAN_MAGIC, return;);

	/*
	 * Check if object has already been registered by a previous provider.
	 * If that is the case, we just change the value of the attribute
	 */
	if (!irias_find_object("IrLAN")) {
		obj = irias_new_object("IrLAN", IAS_IRLAN_ID);
		irias_add_integer_attrib(obj, "IrDA:TinyTP:LsapSel", tsap_sel,
					 IAS_KERNEL_ATTR);
		irias_insert_object(obj);
	} else {
		new_value = irias_new_integer_value(tsap_sel);
		irias_object_change_attribute("IrLAN", "IrDA:TinyTP:LsapSel",
					      new_value);
	}

	/* Register PnP object only if not registered before */
	if (!irias_find_object("PnP")) {
		obj = irias_new_object("PnP", IAS_PNP_ID);
#if 0
		irias_add_string_attrib(obj, "Name", sysctl_devname,
					IAS_KERNEL_ATTR);
#else
		irias_add_string_attrib(obj, "Name", "Linux", IAS_KERNEL_ATTR);
#endif
		irias_add_string_attrib(obj, "DeviceID", "HWP19F0",
					IAS_KERNEL_ATTR);
		irias_add_integer_attrib(obj, "CompCnt", 1, IAS_KERNEL_ATTR);
		if (self->provider.access_type == ACCESS_PEER)
			irias_add_string_attrib(obj, "Comp#01", "PNP8389",
						IAS_KERNEL_ATTR);
		else
			irias_add_string_attrib(obj, "Comp#01", "PNP8294",
						IAS_KERNEL_ATTR);

		irias_add_string_attrib(obj, "Manufacturer",
					"Linux-IrDA Project", IAS_KERNEL_ATTR);
		irias_insert_object(obj);
	}
}

/*
 * Function irlan_run_ctrl_tx_queue (self)
 *
 *    Try to send the next command in the control transmit queue
 *
 */
int irlan_run_ctrl_tx_queue(struct irlan_cb *self)
{
	struct sk_buff *skb;

	IRDA_DEBUG(2, "%s()\n", __func__ );

	if (irda_lock(&self->client.tx_busy) == FALSE)
		return -EBUSY;

	skb = skb_dequeue(&self->client.txq);
	if (!skb) {
		self->client.tx_busy = FALSE;
		return 0;
	}

	/* Check that it's really possible to send commands */
	if ((self->client.tsap_ctrl == NULL) ||
	    (self->client.state == IRLAN_IDLE))
	{
		self->client.tx_busy = FALSE;
		dev_kfree_skb(skb);
		return -1;
	}
	IRDA_DEBUG(2, "%s(), sending ...\n", __func__ );

	return irttp_data_request(self->client.tsap_ctrl, skb);
}

/*
 * Function irlan_ctrl_data_request (self, skb)
 *
 *    This function makes sure that commands on the control channel is being
 *    sent in a command/response fashion
 */
static void irlan_ctrl_data_request(struct irlan_cb *self, struct sk_buff *skb)
{
	IRDA_DEBUG(2, "%s()\n", __func__ );

	/* Queue command */
	skb_queue_tail(&self->client.txq, skb);

	/* Try to send command */
	irlan_run_ctrl_tx_queue(self);
}

/*
 * Function irlan_get_provider_info (self)
 *
 *    Send Get Provider Information command to peer IrLAN layer
 *
 */
void irlan_get_provider_info(struct irlan_cb *self)
{
	struct sk_buff *skb;
	__u8 *frame;

	IRDA_DEBUG(4, "%s()\n", __func__ );

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IRLAN_MAGIC, return;);

	skb = alloc_skb(IRLAN_MAX_HEADER + IRLAN_CMD_HEADER,
			GFP_ATOMIC);
	if (!skb)
		return;

	/* Reserve space for TTP, LMP, and LAP header */
	skb_reserve(skb, self->client.max_header_size);
	skb_put(skb, 2);

	frame = skb->data;

	frame[0] = CMD_GET_PROVIDER_INFO;
	frame[1] = 0x00;                 /* Zero parameters */

	irlan_ctrl_data_request(self, skb);
}

/*
 * Function irlan_open_data_channel (self)
 *
 *    Send an Open Data Command to provider
 *
 */
void irlan_open_data_channel(struct irlan_cb *self)
{
	struct sk_buff *skb;
	__u8 *frame;

	IRDA_DEBUG(4, "%s()\n", __func__ );

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IRLAN_MAGIC, return;);

	skb = alloc_skb(IRLAN_MAX_HEADER + IRLAN_CMD_HEADER +
			IRLAN_STRING_PARAMETER_LEN("MEDIA", "802.3") +
			IRLAN_STRING_PARAMETER_LEN("ACCESS_TYPE", "DIRECT"),
			GFP_ATOMIC);
	if (!skb)
		return;

	skb_reserve(skb, self->client.max_header_size);
	skb_put(skb, 2);

	frame = skb->data;

	/* Build frame */
	frame[0] = CMD_OPEN_DATA_CHANNEL;
	frame[1] = 0x02; /* Two parameters */

	irlan_insert_string_param(skb, "MEDIA", "802.3");
	irlan_insert_string_param(skb, "ACCESS_TYPE", "DIRECT");
	/* irlan_insert_string_param(skb, "MODE", "UNRELIABLE"); */

/* 	self->use_udata = TRUE; */

	irlan_ctrl_data_request(self, skb);
}

void irlan_close_data_channel(struct irlan_cb *self)
{
	struct sk_buff *skb;
	__u8 *frame;

	IRDA_DEBUG(4, "%s()\n", __func__ );

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IRLAN_MAGIC, return;);

	/* Check if the TSAP is still there */
	if (self->client.tsap_ctrl == NULL)
		return;

	skb = alloc_skb(IRLAN_MAX_HEADER + IRLAN_CMD_HEADER +
			IRLAN_BYTE_PARAMETER_LEN("DATA_CHAN"),
			GFP_ATOMIC);
	if (!skb)
		return;

	skb_reserve(skb, self->client.max_header_size);
	skb_put(skb, 2);

	frame = skb->data;

	/* Build frame */
	frame[0] = CMD_CLOSE_DATA_CHAN;
	frame[1] = 0x01; /* One parameter */

	irlan_insert_byte_param(skb, "DATA_CHAN", self->dtsap_sel_data);

	irlan_ctrl_data_request(self, skb);
}

/*
 * Function irlan_open_unicast_addr (self)
 *
 *    Make IrLAN provider accept ethernet frames addressed to the unicast
 *    address.
 *
 */
static void irlan_open_unicast_addr(struct irlan_cb *self)
{
	struct sk_buff *skb;
	__u8 *frame;

	IRDA_DEBUG(4, "%s()\n", __func__ );

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IRLAN_MAGIC, return;);

	skb = alloc_skb(IRLAN_MAX_HEADER + IRLAN_CMD_HEADER +
			IRLAN_BYTE_PARAMETER_LEN("DATA_CHAN") +
			IRLAN_STRING_PARAMETER_LEN("FILTER_TYPE", "DIRECTED") +
			IRLAN_STRING_PARAMETER_LEN("FILTER_MODE", "FILTER"),
			GFP_ATOMIC);
	if (!skb)
		return;

	/* Reserve space for TTP, LMP, and LAP header */
	skb_reserve(skb, self->max_header_size);
	skb_put(skb, 2);

	frame = skb->data;

	frame[0] = CMD_FILTER_OPERATION;
	frame[1] = 0x03;                 /* Three parameters */
	irlan_insert_byte_param(skb, "DATA_CHAN" , self->dtsap_sel_data);
	irlan_insert_string_param(skb, "FILTER_TYPE", "DIRECTED");
	irlan_insert_string_param(skb, "FILTER_MODE", "FILTER");

	irlan_ctrl_data_request(self, skb);
}

/*
 * Function irlan_set_broadcast_filter (self, status)
 *
 *    Make IrLAN provider accept ethernet frames addressed to the broadcast
 *    address. Be careful with the use of this one, since there may be a lot
 *    of broadcast traffic out there. We can still function without this
 *    one but then _we_ have to initiate all communication with other
 *    hosts, since ARP request for this host will not be answered.
 */
void irlan_set_broadcast_filter(struct irlan_cb *self, int status)
{
	struct sk_buff *skb;
	__u8 *frame;

	IRDA_DEBUG(2, "%s()\n", __func__ );

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IRLAN_MAGIC, return;);

	skb = alloc_skb(IRLAN_MAX_HEADER + IRLAN_CMD_HEADER +
			IRLAN_BYTE_PARAMETER_LEN("DATA_CHAN") +
			IRLAN_STRING_PARAMETER_LEN("FILTER_TYPE", "BROADCAST") +
			/* We may waste one byte here...*/
			IRLAN_STRING_PARAMETER_LEN("FILTER_MODE", "FILTER"),
			GFP_ATOMIC);
	if (!skb)
		return;

	/* Reserve space for TTP, LMP, and LAP header */
	skb_reserve(skb, self->client.max_header_size);
	skb_put(skb, 2);

	frame = skb->data;

	frame[0] = CMD_FILTER_OPERATION;
	frame[1] = 0x03;                 /* Three parameters */
	irlan_insert_byte_param(skb, "DATA_CHAN", self->dtsap_sel_data);
	irlan_insert_string_param(skb, "FILTER_TYPE", "BROADCAST");
	if (status)
		irlan_insert_string_param(skb, "FILTER_MODE", "FILTER");
	else
		irlan_insert_string_param(skb, "FILTER_MODE", "NONE");

	irlan_ctrl_data_request(self, skb);
}

/*
 * Function irlan_set_multicast_filter (self, status)
 *
 *    Make IrLAN provider accept ethernet frames addressed to the multicast
 *    address.
 *
 */
void irlan_set_multicast_filter(struct irlan_cb *self, int status)
{
	struct sk_buff *skb;
	__u8 *frame;

	IRDA_DEBUG(2, "%s()\n", __func__ );

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IRLAN_MAGIC, return;);

	skb = alloc_skb(IRLAN_MAX_HEADER + IRLAN_CMD_HEADER +
			IRLAN_BYTE_PARAMETER_LEN("DATA_CHAN") +
			IRLAN_STRING_PARAMETER_LEN("FILTER_TYPE", "MULTICAST") +
			/* We may waste one byte here...*/
			IRLAN_STRING_PARAMETER_LEN("FILTER_MODE", "NONE"),
			GFP_ATOMIC);
	if (!skb)
		return;

	/* Reserve space for TTP, LMP, and LAP header */
	skb_reserve(skb, self->client.max_header_size);
	skb_put(skb, 2);

	frame = skb->data;

	frame[0] = CMD_FILTER_OPERATION;
	frame[1] = 0x03;                 /* Three parameters */
	irlan_insert_byte_param(skb, "DATA_CHAN", self->dtsap_sel_data);
	irlan_insert_string_param(skb, "FILTER_TYPE", "MULTICAST");
	if (status)
		irlan_insert_string_param(skb, "FILTER_MODE", "ALL");
	else
		irlan_insert_string_param(skb, "FILTER_MODE", "NONE");

	irlan_ctrl_data_request(self, skb);
}

/*
 * Function irlan_get_unicast_addr (self)
 *
 *    Retrieves the unicast address from the IrLAN provider. This address
 *    will be inserted into the devices structure, so the ethernet layer
 *    can construct its packets.
 *
 */
static void irlan_get_unicast_addr(struct irlan_cb *self)
{
	struct sk_buff *skb;
	__u8 *frame;

	IRDA_DEBUG(2, "%s()\n", __func__ );

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IRLAN_MAGIC, return;);

	skb = alloc_skb(IRLAN_MAX_HEADER + IRLAN_CMD_HEADER +
			IRLAN_BYTE_PARAMETER_LEN("DATA_CHAN") +
			IRLAN_STRING_PARAMETER_LEN("FILTER_TYPE", "DIRECTED") +
			IRLAN_STRING_PARAMETER_LEN("FILTER_OPERATION",
						   "DYNAMIC"),
			GFP_ATOMIC);
	if (!skb)
		return;

	/* Reserve space for TTP, LMP, and LAP header */
	skb_reserve(skb, self->client.max_header_size);
	skb_put(skb, 2);

	frame = skb->data;

	frame[0] = CMD_FILTER_OPERATION;
	frame[1] = 0x03;                 /* Three parameters */
	irlan_insert_byte_param(skb, "DATA_CHAN", self->dtsap_sel_data);
	irlan_insert_string_param(skb, "FILTER_TYPE", "DIRECTED");
	irlan_insert_string_param(skb, "FILTER_OPERATION", "DYNAMIC");

	irlan_ctrl_data_request(self, skb);
}

/*
 * Function irlan_get_media_char (self)
 *
 *
 *
 */
void irlan_get_media_char(struct irlan_cb *self)
{
	struct sk_buff *skb;
	__u8 *frame;

	IRDA_DEBUG(4, "%s()\n", __func__ );

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IRLAN_MAGIC, return;);

	skb = alloc_skb(IRLAN_MAX_HEADER + IRLAN_CMD_HEADER +
			IRLAN_STRING_PARAMETER_LEN("MEDIA", "802.3"),
			GFP_ATOMIC);

	if (!skb)
		return;

	/* Reserve space for TTP, LMP, and LAP header */
	skb_reserve(skb, self->client.max_header_size);
	skb_put(skb, 2);

	frame = skb->data;

	/* Build frame */
	frame[0] = CMD_GET_MEDIA_CHAR;
	frame[1] = 0x01; /* One parameter */

	irlan_insert_string_param(skb, "MEDIA", "802.3");
	irlan_ctrl_data_request(self, skb);
}

/*
 * Function insert_byte_param (skb, param, value)
 *
 *    Insert byte parameter into frame
 *
 */
int irlan_insert_byte_param(struct sk_buff *skb, char *param, __u8 value)
{
	return __irlan_insert_param(skb, param, IRLAN_BYTE, value, 0, NULL, 0);
}

int irlan_insert_short_param(struct sk_buff *skb, char *param, __u16 value)
{
	return __irlan_insert_param(skb, param, IRLAN_SHORT, 0, value, NULL, 0);
}

/*
 * Function insert_string (skb, param, value)
 *
 *    Insert string parameter into frame
 *
 */
int irlan_insert_string_param(struct sk_buff *skb, char *param, char *string)
{
	int string_len = strlen(string);

	return __irlan_insert_param(skb, param, IRLAN_ARRAY, 0, 0, string,
				    string_len);
}

/*
 * Function insert_array_param(skb, param, value, len_value)
 *
 *    Insert array parameter into frame
 *
 */
int irlan_insert_array_param(struct sk_buff *skb, char *name, __u8 *array,
			     __u16 array_len)
{
	return __irlan_insert_param(skb, name, IRLAN_ARRAY, 0, 0, array,
				    array_len);
}

/*
 * Function insert_param (skb, param, value, byte)
 *
 *    Insert parameter at end of buffer, structure of a parameter is:
 *
 *    -----------------------------------------------------------------------
 *    | Name Length[1] | Param Name[1..255] | Val Length[2] | Value[0..1016]|
 *    -----------------------------------------------------------------------
 */
static int __irlan_insert_param(struct sk_buff *skb, char *param, int type,
				__u8 value_byte, __u16 value_short,
				__u8 *value_array, __u16 value_len)
{
	__u8 *frame;
	__u8 param_len;
	__le16 tmp_le; /* Temporary value in little endian format */
	int n=0;

	if (skb == NULL) {
		IRDA_DEBUG(2, "%s(), Got NULL skb\n", __func__ );
		return 0;
	}

	param_len = strlen(param);
	switch (type) {
	case IRLAN_BYTE:
		value_len = 1;
		break;
	case IRLAN_SHORT:
		value_len = 2;
		break;
	case IRLAN_ARRAY:
		IRDA_ASSERT(value_array != NULL, return 0;);
		IRDA_ASSERT(value_len > 0, return 0;);
		break;
	default:
		IRDA_DEBUG(2, "%s(), Unknown parameter type!\n", __func__ );
		return 0;
		break;
	}

	/* Insert at end of sk-buffer */
	frame = skb_tail_pointer(skb);

	/* Make space for data */
	if (skb_tailroom(skb) < (param_len+value_len+3)) {
		IRDA_DEBUG(2, "%s(), No more space at end of skb\n", __func__ );
		return 0;
	}
	skb_put(skb, param_len+value_len+3);

	/* Insert parameter length */
	frame[n++] = param_len;

	/* Insert parameter */
	memcpy(frame+n, param, param_len); n += param_len;

	/* Insert value length (2 byte little endian format, LSB first) */
	tmp_le = cpu_to_le16(value_len);
	memcpy(frame+n, &tmp_le, 2); n += 2; /* To avoid alignment problems */

	/* Insert value */
	switch (type) {
	case IRLAN_BYTE:
		frame[n++] = value_byte;
		break;
	case IRLAN_SHORT:
		tmp_le = cpu_to_le16(value_short);
		memcpy(frame+n, &tmp_le, 2); n += 2;
		break;
	case IRLAN_ARRAY:
		memcpy(frame+n, value_array, value_len); n+=value_len;
		break;
	default:
		break;
	}
	IRDA_ASSERT(n == (param_len+value_len+3), return 0;);

	return param_len+value_len+3;
}

/*
 * Function irlan_extract_param (buf, name, value, len)
 *
 *    Extracts a single parameter name/value pair from buffer and updates
 *    the buffer pointer to point to the next name/value pair.
 */
int irlan_extract_param(__u8 *buf, char *name, char *value, __u16 *len)
{
	__u8 name_len;
	__u16 val_len;
	int n=0;

	IRDA_DEBUG(4, "%s()\n", __func__ );

	/* get length of parameter name (1 byte) */
	name_len = buf[n++];

	if (name_len > 254) {
		IRDA_DEBUG(2, "%s(), name_len > 254\n", __func__ );
		return -RSP_INVALID_COMMAND_FORMAT;
	}

	/* get parameter name */
	memcpy(name, buf+n, name_len);
	name[name_len] = '\0';
	n+=name_len;

	/*
	 *  Get length of parameter value (2 bytes in little endian
	 *  format)
	 */
	memcpy(&val_len, buf+n, 2); /* To avoid alignment problems */
	le16_to_cpus(&val_len); n+=2;

	if (val_len >= 1016) {
		IRDA_DEBUG(2, "%s(), parameter length to long\n", __func__ );
		return -RSP_INVALID_COMMAND_FORMAT;
	}
	*len = val_len;

	/* get parameter value */
	memcpy(value, buf+n, val_len);
	value[val_len] = '\0';
	n+=val_len;

	IRDA_DEBUG(4, "Parameter: %s ", name);
	IRDA_DEBUG(4, "Value: %s\n", value);

	return n;
}

#ifdef CONFIG_PROC_FS

/*
 * Start of reading /proc entries.
 * Return entry at pos,
 *	or start_token to indicate print header line
 *	or NULL if end of file
 */
static void *irlan_seq_start(struct seq_file *seq, loff_t *pos)
{
	int i = 1;
	struct irlan_cb *self;

	rcu_read_lock();
	if (*pos == 0)
		return SEQ_START_TOKEN;

	list_for_each_entry(self, &irlans, dev_list) {
		if (*pos == i)
			return self;
		++i;
	}
	return NULL;
}

/* Return entry after v, and increment pos */
static void *irlan_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct list_head *nxt;

	++*pos;
	if (v == SEQ_START_TOKEN)
		nxt = irlans.next;
	else
		nxt = ((struct irlan_cb *)v)->dev_list.next;

	return (nxt == &irlans) ? NULL
		: list_entry(nxt, struct irlan_cb, dev_list);
}

/* End of reading /proc file */
static void irlan_seq_stop(struct seq_file *seq, void *v)
{
	rcu_read_unlock();
}


/*
 * Show one entry in /proc file.
 */
static int irlan_seq_show(struct seq_file *seq, void *v)
{
	if (v == SEQ_START_TOKEN)
		seq_puts(seq, "IrLAN instances:\n");
	else {
		struct irlan_cb *self = v;

		IRDA_ASSERT(self != NULL, return -1;);
		IRDA_ASSERT(self->magic == IRLAN_MAGIC, return -1;);

		seq_printf(seq,"ifname: %s,\n",
			       self->dev->name);
		seq_printf(seq,"client state: %s, ",
			       irlan_state[ self->client.state]);
		seq_printf(seq,"provider state: %s,\n",
			       irlan_state[ self->provider.state]);
		seq_printf(seq,"saddr: %#08x, ",
			       self->saddr);
		seq_printf(seq,"daddr: %#08x\n",
			       self->daddr);
		seq_printf(seq,"version: %d.%d,\n",
			       self->version[1], self->version[0]);
		seq_printf(seq,"access type: %s\n",
			       irlan_access[self->client.access_type]);
		seq_printf(seq,"media: %s\n",
			       irlan_media[self->media]);

		seq_printf(seq,"local filter:\n");
		seq_printf(seq,"remote filter: ");
		irlan_print_filter(seq, self->client.filter_type);
		seq_printf(seq,"tx busy: %s\n",
			       netif_queue_stopped(self->dev) ? "TRUE" : "FALSE");

		seq_putc(seq,'\n');
	}
	return 0;
}

static const struct seq_operations irlan_seq_ops = {
	.start = irlan_seq_start,
	.next  = irlan_seq_next,
	.stop  = irlan_seq_stop,
	.show  = irlan_seq_show,
};

static int irlan_seq_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &irlan_seq_ops);
}
#endif

MODULE_AUTHOR("Dag Brattli <dagb@cs.uit.no>");
MODULE_DESCRIPTION("The Linux IrDA LAN protocol");
MODULE_LICENSE("GPL");

module_param(eth, bool, 0);
MODULE_PARM_DESC(eth, "Name devices ethX (0) or irlanX (1)");
module_param(access, int, 0);
MODULE_PARM_DESC(access, "Access type DIRECT=1, PEER=2, HOSTED=3");

module_init(irlan_init);
module_exit(irlan_cleanup);

