/*********************************************************************
 *
 * Filename:      irlan_client.c
 * Version:       0.9
 * Description:   IrDA LAN Access Protocol (IrLAN) Client
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Sun Aug 31 20:14:37 1997
 * Modified at:   Tue Dec 14 15:47:02 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * Sources:       skeleton.c by Donald Becker <becker@CESDIS.gsfc.nasa.gov>
 *                slip.c by Laurence Culhane, <loz@holmes.demon.co.uk>
 *                          Fred N. van Kempen, <waltje@uwalt.nl.mugnet.org>
 *
 *     Copyright (c) 1998-1999 Dag Brattli <dagb@cs.uit.no>,
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

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/if_arp.h>
#include <linux/bitops.h>
#include <net/arp.h>

#include <asm/system.h>
#include <asm/byteorder.h>

#include <net/irda/irda.h>
#include <net/irda/irttp.h>
#include <net/irda/irlmp.h>
#include <net/irda/irias_object.h>
#include <net/irda/iriap.h>
#include <net/irda/timer.h>

#include <net/irda/irlan_common.h>
#include <net/irda/irlan_event.h>
#include <net/irda/irlan_eth.h>
#include <net/irda/irlan_provider.h>
#include <net/irda/irlan_client.h>

#undef CONFIG_IRLAN_GRATUITOUS_ARP

static void irlan_client_ctrl_disconnect_indication(void *instance, void *sap,
						    LM_REASON reason,
						    struct sk_buff *);
static int irlan_client_ctrl_data_indication(void *instance, void *sap,
					     struct sk_buff *skb);
static void irlan_client_ctrl_connect_confirm(void *instance, void *sap,
					      struct qos_info *qos,
					      __u32 max_sdu_size,
					      __u8 max_header_size,
					      struct sk_buff *);
static void irlan_check_response_param(struct irlan_cb *self, char *param,
				       char *value, int val_len);
static void irlan_client_open_ctrl_tsap(struct irlan_cb *self);

static void irlan_client_kick_timer_expired(void *data)
{
	struct irlan_cb *self = (struct irlan_cb *) data;

	IRDA_DEBUG(2, "%s()\n", __func__ );

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IRLAN_MAGIC, return;);

	/*
	 * If we are in peer mode, the client may not have got the discovery
	 * indication it needs to make progress. If the client is still in
	 * IDLE state, we must kick it to, but only if the provider is not IDLE
	 */
	if ((self->provider.access_type == ACCESS_PEER) &&
	    (self->client.state == IRLAN_IDLE) &&
	    (self->provider.state != IRLAN_IDLE)) {
		irlan_client_wakeup(self, self->saddr, self->daddr);
	}
}

static void irlan_client_start_kick_timer(struct irlan_cb *self, int timeout)
{
	IRDA_DEBUG(4, "%s()\n", __func__ );

	irda_start_timer(&self->client.kick_timer, timeout, (void *) self,
			 irlan_client_kick_timer_expired);
}

/*
 * Function irlan_client_wakeup (self, saddr, daddr)
 *
 *    Wake up client
 *
 */
void irlan_client_wakeup(struct irlan_cb *self, __u32 saddr, __u32 daddr)
{
	IRDA_DEBUG(1, "%s()\n", __func__ );

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IRLAN_MAGIC, return;);

	/*
	 * Check if we are already awake, or if we are a provider in direct
	 * mode (in that case we must leave the client idle
	 */
	if ((self->client.state != IRLAN_IDLE) ||
	    (self->provider.access_type == ACCESS_DIRECT))
	{
			IRDA_DEBUG(0, "%s(), already awake!\n", __func__ );
			return;
	}

	/* Addresses may have changed! */
	self->saddr = saddr;
	self->daddr = daddr;

	if (self->disconnect_reason == LM_USER_REQUEST) {
			IRDA_DEBUG(0, "%s(), still stopped by user\n", __func__ );
			return;
	}

	/* Open TSAPs */
	irlan_client_open_ctrl_tsap(self);
	irlan_open_data_tsap(self);

	irlan_do_client_event(self, IRLAN_DISCOVERY_INDICATION, NULL);

	/* Start kick timer */
	irlan_client_start_kick_timer(self, 2*HZ);
}

/*
 * Function irlan_discovery_indication (daddr)
 *
 *    Remote device with IrLAN server support discovered
 *
 */
void irlan_client_discovery_indication(discinfo_t *discovery,
				       DISCOVERY_MODE mode,
				       void *priv)
{
	struct irlan_cb *self;
	__u32 saddr, daddr;

	IRDA_DEBUG(1, "%s()\n", __func__ );

	IRDA_ASSERT(discovery != NULL, return;);

	/*
	 * I didn't check it, but I bet that IrLAN suffer from the same
	 * deficiency as IrComm and doesn't handle two instances
	 * simultaneously connecting to each other.
	 * Same workaround, drop passive discoveries.
	 * Jean II */
	if(mode == DISCOVERY_PASSIVE)
		return;

	saddr = discovery->saddr;
	daddr = discovery->daddr;

	/* Find instance */
	rcu_read_lock();
	self = irlan_get_any();
	if (self) {
		IRDA_ASSERT(self->magic == IRLAN_MAGIC, goto out;);

		IRDA_DEBUG(1, "%s(), Found instance (%08x)!\n", __func__ ,
		      daddr);

		irlan_client_wakeup(self, saddr, daddr);
	}
IRDA_ASSERT_LABEL(out:)
	rcu_read_unlock();
}

/*
 * Function irlan_client_data_indication (handle, skb)
 *
 *    This function gets the data that is received on the control channel
 *
 */
static int irlan_client_ctrl_data_indication(void *instance, void *sap,
					     struct sk_buff *skb)
{
	struct irlan_cb *self;

	IRDA_DEBUG(2, "%s()\n", __func__ );

	self = (struct irlan_cb *) instance;

	IRDA_ASSERT(self != NULL, return -1;);
	IRDA_ASSERT(self->magic == IRLAN_MAGIC, return -1;);
	IRDA_ASSERT(skb != NULL, return -1;);

	irlan_do_client_event(self, IRLAN_DATA_INDICATION, skb);

	/* Ready for a new command */
	IRDA_DEBUG(2, "%s(), clearing tx_busy\n", __func__ );
	self->client.tx_busy = FALSE;

	/* Check if we have some queued commands waiting to be sent */
	irlan_run_ctrl_tx_queue(self);

	return 0;
}

static void irlan_client_ctrl_disconnect_indication(void *instance, void *sap,
						    LM_REASON reason,
						    struct sk_buff *userdata)
{
	struct irlan_cb *self;
	struct tsap_cb *tsap;
	struct sk_buff *skb;

	IRDA_DEBUG(4, "%s(), reason=%d\n", __func__ , reason);

	self = (struct irlan_cb *) instance;
	tsap = (struct tsap_cb *) sap;

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IRLAN_MAGIC, return;);
	IRDA_ASSERT(tsap != NULL, return;);
	IRDA_ASSERT(tsap->magic == TTP_TSAP_MAGIC, return;);

	IRDA_ASSERT(tsap == self->client.tsap_ctrl, return;);

	/* Remove frames queued on the control channel */
	while ((skb = skb_dequeue(&self->client.txq)) != NULL) {
		dev_kfree_skb(skb);
	}
	self->client.tx_busy = FALSE;

	irlan_do_client_event(self, IRLAN_LMP_DISCONNECT, NULL);
}

/*
 * Function irlan_client_open_tsaps (self)
 *
 *    Initialize callbacks and open IrTTP TSAPs
 *
 */
static void irlan_client_open_ctrl_tsap(struct irlan_cb *self)
{
	struct tsap_cb *tsap;
	notify_t notify;

	IRDA_DEBUG(4, "%s()\n", __func__ );

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IRLAN_MAGIC, return;);

	/* Check if already open */
	if (self->client.tsap_ctrl)
		return;

	irda_notify_init(&notify);

	/* Set up callbacks */
	notify.data_indication       = irlan_client_ctrl_data_indication;
	notify.connect_confirm       = irlan_client_ctrl_connect_confirm;
	notify.disconnect_indication = irlan_client_ctrl_disconnect_indication;
	notify.instance = self;
	strlcpy(notify.name, "IrLAN ctrl (c)", sizeof(notify.name));

	tsap = irttp_open_tsap(LSAP_ANY, DEFAULT_INITIAL_CREDIT, &notify);
	if (!tsap) {
		IRDA_DEBUG(2, "%s(), Got no tsap!\n", __func__ );
		return;
	}
	self->client.tsap_ctrl = tsap;
}

/*
 * Function irlan_client_connect_confirm (handle, skb)
 *
 *    Connection to peer IrLAN laye confirmed
 *
 */
static void irlan_client_ctrl_connect_confirm(void *instance, void *sap,
					      struct qos_info *qos,
					      __u32 max_sdu_size,
					      __u8 max_header_size,
					      struct sk_buff *skb)
{
	struct irlan_cb *self;

	IRDA_DEBUG(4, "%s()\n", __func__ );

	self = (struct irlan_cb *) instance;

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IRLAN_MAGIC, return;);

	self->client.max_sdu_size = max_sdu_size;
	self->client.max_header_size = max_header_size;

	/* TODO: we could set the MTU depending on the max_sdu_size */

	irlan_do_client_event(self, IRLAN_CONNECT_COMPLETE, NULL);
}

/*
 * Function print_ret_code (code)
 *
 *    Print return code of request to peer IrLAN layer.
 *
 */
static void print_ret_code(__u8 code)
{
	switch(code) {
	case 0:
		printk(KERN_INFO "Success\n");
		break;
	case 1:
		IRDA_WARNING("IrLAN: Insufficient resources\n");
		break;
	case 2:
		IRDA_WARNING("IrLAN: Invalid command format\n");
		break;
	case 3:
		IRDA_WARNING("IrLAN: Command not supported\n");
		break;
	case 4:
		IRDA_WARNING("IrLAN: Parameter not supported\n");
		break;
	case 5:
		IRDA_WARNING("IrLAN: Value not supported\n");
		break;
	case 6:
		IRDA_WARNING("IrLAN: Not open\n");
		break;
	case 7:
		IRDA_WARNING("IrLAN: Authentication required\n");
		break;
	case 8:
		IRDA_WARNING("IrLAN: Invalid password\n");
		break;
	case 9:
		IRDA_WARNING("IrLAN: Protocol error\n");
		break;
	case 255:
		IRDA_WARNING("IrLAN: Asynchronous status\n");
		break;
	}
}

/*
 * Function irlan_client_parse_response (self, skb)
 *
 *    Extract all parameters from received buffer, then feed them to
 *    check_params for parsing
 */
void irlan_client_parse_response(struct irlan_cb *self, struct sk_buff *skb)
{
	__u8 *frame;
	__u8 *ptr;
	int count;
	int ret;
	__u16 val_len;
	int i;
	char *name;
	char *value;

	IRDA_ASSERT(skb != NULL, return;);

	IRDA_DEBUG(4, "%s() skb->len=%d\n", __func__ , (int) skb->len);

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IRLAN_MAGIC, return;);

	if (!skb) {
		IRDA_ERROR("%s(), Got NULL skb!\n", __func__);
		return;
	}
	frame = skb->data;

	/*
	 *  Check return code and print it if not success
	 */
	if (frame[0]) {
		print_ret_code(frame[0]);
		return;
	}

	name = kmalloc(255, GFP_ATOMIC);
	if (!name)
		return;
	value = kmalloc(1016, GFP_ATOMIC);
	if (!value) {
		kfree(name);
		return;
	}

	/* How many parameters? */
	count = frame[1];

	IRDA_DEBUG(4, "%s(), got %d parameters\n", __func__ , count);

	ptr = frame+2;

	/* For all parameters */
	for (i=0; i<count;i++) {
		ret = irlan_extract_param(ptr, name, value, &val_len);
		if (ret < 0) {
			IRDA_DEBUG(2, "%s(), IrLAN, Error!\n", __func__ );
			break;
		}
		ptr += ret;
		irlan_check_response_param(self, name, value, val_len);
	}
	/* Cleanup */
	kfree(name);
	kfree(value);
}

/*
 * Function irlan_check_response_param (self, param, value, val_len)
 *
 *     Check which parameter is received and update local variables
 *
 */
static void irlan_check_response_param(struct irlan_cb *self, char *param,
				       char *value, int val_len)
{
	__u16 tmp_cpu; /* Temporary value in host order */
	__u8 *bytes;
	int i;

	IRDA_DEBUG(4, "%s(), parm=%s\n", __func__ , param);

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IRLAN_MAGIC, return;);

	/* Media type */
	if (strcmp(param, "MEDIA") == 0) {
		if (strcmp(value, "802.3") == 0)
			self->media = MEDIA_802_3;
		else
			self->media = MEDIA_802_5;
		return;
	}
	if (strcmp(param, "FILTER_TYPE") == 0) {
		if (strcmp(value, "DIRECTED") == 0)
			self->client.filter_type |= IRLAN_DIRECTED;
		else if (strcmp(value, "FUNCTIONAL") == 0)
			self->client.filter_type |= IRLAN_FUNCTIONAL;
		else if (strcmp(value, "GROUP") == 0)
			self->client.filter_type |= IRLAN_GROUP;
		else if (strcmp(value, "MAC_FRAME") == 0)
			self->client.filter_type |= IRLAN_MAC_FRAME;
		else if (strcmp(value, "MULTICAST") == 0)
			self->client.filter_type |= IRLAN_MULTICAST;
		else if (strcmp(value, "BROADCAST") == 0)
			self->client.filter_type |= IRLAN_BROADCAST;
		else if (strcmp(value, "IPX_SOCKET") == 0)
			self->client.filter_type |= IRLAN_IPX_SOCKET;

	}
	if (strcmp(param, "ACCESS_TYPE") == 0) {
		if (strcmp(value, "DIRECT") == 0)
			self->client.access_type = ACCESS_DIRECT;
		else if (strcmp(value, "PEER") == 0)
			self->client.access_type = ACCESS_PEER;
		else if (strcmp(value, "HOSTED") == 0)
			self->client.access_type = ACCESS_HOSTED;
		else {
			IRDA_DEBUG(2, "%s(), unknown access type!\n", __func__ );
		}
	}
	/* IRLAN version */
	if (strcmp(param, "IRLAN_VER") == 0) {
		IRDA_DEBUG(4, "IrLAN version %d.%d\n", (__u8) value[0],
		      (__u8) value[1]);

		self->version[0] = value[0];
		self->version[1] = value[1];
		return;
	}
	/* Which remote TSAP to use for data channel */
	if (strcmp(param, "DATA_CHAN") == 0) {
		self->dtsap_sel_data = value[0];
		IRDA_DEBUG(4, "Data TSAP = %02x\n", self->dtsap_sel_data);
		return;
	}
	if (strcmp(param, "CON_ARB") == 0) {
		memcpy(&tmp_cpu, value, 2); /* Align value */
		le16_to_cpus(&tmp_cpu);     /* Convert to host order */
		self->client.recv_arb_val = tmp_cpu;
		IRDA_DEBUG(2, "%s(), receive arb val=%d\n", __func__ ,
			   self->client.recv_arb_val);
	}
	if (strcmp(param, "MAX_FRAME") == 0) {
		memcpy(&tmp_cpu, value, 2); /* Align value */
		le16_to_cpus(&tmp_cpu);     /* Convert to host order */
		self->client.max_frame = tmp_cpu;
		IRDA_DEBUG(4, "%s(), max frame=%d\n", __func__ ,
			   self->client.max_frame);
	}

	/* RECONNECT_KEY, in case the link goes down! */
	if (strcmp(param, "RECONNECT_KEY") == 0) {
		IRDA_DEBUG(4, "Got reconnect key: ");
		/* for (i = 0; i < val_len; i++) */
/* 			printk("%02x", value[i]); */
		memcpy(self->client.reconnect_key, value, val_len);
		self->client.key_len = val_len;
		IRDA_DEBUG(4, "\n");
	}
	/* FILTER_ENTRY, have we got an ethernet address? */
	if (strcmp(param, "FILTER_ENTRY") == 0) {
		bytes = value;
		IRDA_DEBUG(4, "Ethernet address = %pM\n", bytes);
		for (i = 0; i < 6; i++)
			self->dev->dev_addr[i] = bytes[i];
	}
}

/*
 * Function irlan_client_get_value_confirm (obj_id, value)
 *
 *    Got results from remote LM-IAS
 *
 */
void irlan_client_get_value_confirm(int result, __u16 obj_id,
				    struct ias_value *value, void *priv)
{
	struct irlan_cb *self;

	IRDA_DEBUG(4, "%s()\n", __func__ );

	IRDA_ASSERT(priv != NULL, return;);

	self = (struct irlan_cb *) priv;
	IRDA_ASSERT(self->magic == IRLAN_MAGIC, return;);

	/* We probably don't need to make any more queries */
	iriap_close(self->client.iriap);
	self->client.iriap = NULL;

	/* Check if request succeeded */
	if (result != IAS_SUCCESS) {
		IRDA_DEBUG(2, "%s(), got NULL value!\n", __func__ );
		irlan_do_client_event(self, IRLAN_IAS_PROVIDER_NOT_AVAIL,
				      NULL);
		return;
	}

	switch (value->type) {
	case IAS_INTEGER:
		self->dtsap_sel_ctrl = value->t.integer;

		if (value->t.integer != -1) {
			irlan_do_client_event(self, IRLAN_IAS_PROVIDER_AVAIL,
					      NULL);
			return;
		}
		irias_delete_value(value);
		break;
	default:
		IRDA_DEBUG(2, "%s(), unknown type!\n", __func__ );
		break;
	}
	irlan_do_client_event(self, IRLAN_IAS_PROVIDER_NOT_AVAIL, NULL);
}
