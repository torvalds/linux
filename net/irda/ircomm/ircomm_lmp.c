/*********************************************************************
 *                
 * Filename:      ircomm_lmp.c
 * Version:       1.0
 * Description:   Interface between IrCOMM and IrLMP
 * Status:        Stable
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Sun Jun  6 20:48:27 1999
 * Modified at:   Sun Dec 12 13:44:17 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * Sources:       Previous IrLPT work by Thomas Davis
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

#include <linux/sched.h>
#include <linux/init.h>

#include <net/irda/irda.h>
#include <net/irda/irlmp.h>
#include <net/irda/iriap.h>
#include <net/irda/irda_device.h>	/* struct irda_skb_cb */

#include <net/irda/ircomm_event.h>
#include <net/irda/ircomm_lmp.h>


/*
 * Function ircomm_lmp_connect_request (self, userdata)
 *
 *    
 *
 */
static int ircomm_lmp_connect_request(struct ircomm_cb *self, 
				      struct sk_buff *userdata, 
				      struct ircomm_info *info)
{
	int ret = 0;

	IRDA_DEBUG(0, "%s()\n", __FUNCTION__ );

	/* Don't forget to refcount it - should be NULL anyway */
	if(userdata)
		skb_get(userdata);

	ret = irlmp_connect_request(self->lsap, info->dlsap_sel,
				    info->saddr, info->daddr, NULL, userdata); 
	return ret;
}	

/*
 * Function ircomm_lmp_connect_response (self, skb)
 *
 *    
 *
 */
static int ircomm_lmp_connect_response(struct ircomm_cb *self,
				       struct sk_buff *userdata)
{
	struct sk_buff *tx_skb;
	int ret;

	IRDA_DEBUG(0, "%s()\n", __FUNCTION__ );
	
	/* Any userdata supplied? */
	if (userdata == NULL) {
		tx_skb = alloc_skb(LMP_MAX_HEADER, GFP_ATOMIC);
		if (!tx_skb)
			return -ENOMEM;

		/* Reserve space for MUX and LAP header */
		skb_reserve(tx_skb, LMP_MAX_HEADER);
	} else {
		/*  
		 *  Check that the client has reserved enough space for 
		 *  headers
		 */
		IRDA_ASSERT(skb_headroom(userdata) >= LMP_MAX_HEADER,
			    return -1;);

		/* Don't forget to refcount it - should be NULL anyway */
		skb_get(userdata);
		tx_skb = userdata;
	}

	ret = irlmp_connect_response(self->lsap, tx_skb);

	return 0;
}

static int ircomm_lmp_disconnect_request(struct ircomm_cb *self, 
					 struct sk_buff *userdata, 
					 struct ircomm_info *info)
{
        struct sk_buff *tx_skb;
	int ret;

	IRDA_DEBUG(0, "%s()\n", __FUNCTION__ );

        if (!userdata) {
		tx_skb = alloc_skb(LMP_MAX_HEADER, GFP_ATOMIC);
		if (!tx_skb)
			return -ENOMEM;
		
		/*  Reserve space for MUX and LAP header */
		skb_reserve(tx_skb, LMP_MAX_HEADER);		
		userdata = tx_skb;
	} else {
		/* Don't forget to refcount it - should be NULL anyway */
		skb_get(userdata);
	}

	ret = irlmp_disconnect_request(self->lsap, userdata);

	return ret;
}

/*
 * Function ircomm_lmp_flow_control (skb)
 *
 *    This function is called when a data frame we have sent to IrLAP has
 *    been deallocated. We do this to make sure we don't flood IrLAP with 
 *    frames, since we are not using the IrTTP flow control mechanism
 */
static void ircomm_lmp_flow_control(struct sk_buff *skb)
{
	struct irda_skb_cb *cb;
	struct ircomm_cb *self;
	int line;

	IRDA_ASSERT(skb != NULL, return;);

	cb = (struct irda_skb_cb *) skb->cb;

	IRDA_DEBUG(2, "%s()\n", __FUNCTION__ );
 
        line = cb->line;

	self = (struct ircomm_cb *) hashbin_lock_find(ircomm, line, NULL);
        if (!self) {
		IRDA_DEBUG(2, "%s(), didn't find myself\n", __FUNCTION__ );
                return;
	}

        IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IRCOMM_MAGIC, return;);

	self->pkt_count--;

        if ((self->pkt_count < 2) && (self->flow_status == FLOW_STOP)) {
                IRDA_DEBUG(2, "%s(), asking TTY to start again!\n", __FUNCTION__ );
                self->flow_status = FLOW_START;
                if (self->notify.flow_indication)
                        self->notify.flow_indication(self->notify.instance, 
						     self, FLOW_START);
        }
}
    
/*
 * Function ircomm_lmp_data_request (self, userdata)
 *
 *    Send data frame to peer device
 *
 */
static int ircomm_lmp_data_request(struct ircomm_cb *self,
				   struct sk_buff *skb, 
				   int not_used)
{
	struct irda_skb_cb *cb;
	int ret;

	IRDA_ASSERT(skb != NULL, return -1;);

	cb = (struct irda_skb_cb *) skb->cb;
	
        cb->line = self->line;

	IRDA_DEBUG(4, "%s(), sending frame\n", __FUNCTION__ );

	/* Don't forget to refcount it - see ircomm_tty_do_softint() */
	skb_get(skb);

	skb->destructor = ircomm_lmp_flow_control;

        if ((self->pkt_count++ > 7) && (self->flow_status == FLOW_START)) {
		IRDA_DEBUG(2, "%s(), asking TTY to slow down!\n", __FUNCTION__ );
	        self->flow_status = FLOW_STOP;
                if (self->notify.flow_indication)
             	        self->notify.flow_indication(self->notify.instance, 
				                     self, FLOW_STOP);
        }
	ret = irlmp_data_request(self->lsap, skb);
	if (ret) {
		IRDA_ERROR("%s(), failed\n", __FUNCTION__);
		/* irlmp_data_request already free the packet */
	}

	return ret;
}

/*
 * Function ircomm_lmp_data_indication (instance, sap, skb)
 *
 *    Incoming data which we must deliver to the state machine, to check
 *    we are still connected.
 */
static int ircomm_lmp_data_indication(void *instance, void *sap,
				      struct sk_buff *skb)
{
	struct ircomm_cb *self = (struct ircomm_cb *) instance;

	IRDA_DEBUG(4, "%s()\n", __FUNCTION__ );
	
	IRDA_ASSERT(self != NULL, return -1;);
	IRDA_ASSERT(self->magic == IRCOMM_MAGIC, return -1;);
	IRDA_ASSERT(skb != NULL, return -1;);
	
	ircomm_do_event(self, IRCOMM_LMP_DATA_INDICATION, skb, NULL);

	/* Drop reference count - see ircomm_tty_data_indication(). */
	dev_kfree_skb(skb);

	return 0;
}

/*
 * Function ircomm_lmp_connect_confirm (instance, sap, qos, max_sdu_size, 
 *                                       max_header_size, skb)
 *
 *    Connection has been confirmed by peer device
 *
 */
static void ircomm_lmp_connect_confirm(void *instance, void *sap,
				       struct qos_info *qos, 
				       __u32 max_seg_size, 
				       __u8 max_header_size,
				       struct sk_buff *skb)
{
	struct ircomm_cb *self = (struct ircomm_cb *) instance;
	struct ircomm_info info;

	IRDA_DEBUG(0, "%s()\n", __FUNCTION__ );

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IRCOMM_MAGIC, return;);
	IRDA_ASSERT(skb != NULL, return;);
	IRDA_ASSERT(qos != NULL, return;);

	info.max_data_size = max_seg_size;
	info.max_header_size = max_header_size;
	info.qos = qos;

	ircomm_do_event(self, IRCOMM_LMP_CONNECT_CONFIRM, skb, &info);

	/* Drop reference count - see ircomm_tty_connect_confirm(). */
	dev_kfree_skb(skb);
}

/*
 * Function ircomm_lmp_connect_indication (instance, sap, qos, max_sdu_size,
 *                                         max_header_size, skb)
 *
 *    Peer device wants to make a connection with us
 *
 */
static void ircomm_lmp_connect_indication(void *instance, void *sap,
					  struct qos_info *qos,
					  __u32 max_seg_size,
					  __u8 max_header_size,
					  struct sk_buff *skb)
{
	struct ircomm_cb *self = (struct ircomm_cb *)instance;
	struct ircomm_info info;

	IRDA_DEBUG(0, "%s()\n", __FUNCTION__ );

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IRCOMM_MAGIC, return;);
	IRDA_ASSERT(skb != NULL, return;);
	IRDA_ASSERT(qos != NULL, return;);

	info.max_data_size = max_seg_size;
	info.max_header_size = max_header_size;
	info.qos = qos;

	ircomm_do_event(self, IRCOMM_LMP_CONNECT_INDICATION, skb, &info);

	/* Drop reference count - see ircomm_tty_connect_indication(). */
	dev_kfree_skb(skb);
}

/*
 * Function ircomm_lmp_disconnect_indication (instance, sap, reason, skb)
 *
 *    Peer device has closed the connection, or the link went down for some
 *    other reason
 */
static void ircomm_lmp_disconnect_indication(void *instance, void *sap, 
					     LM_REASON reason,
					     struct sk_buff *skb)
{
	struct ircomm_cb *self = (struct ircomm_cb *) instance;
	struct ircomm_info info;

	IRDA_DEBUG(0, "%s()\n", __FUNCTION__ );

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IRCOMM_MAGIC, return;);

	info.reason = reason;

	ircomm_do_event(self, IRCOMM_LMP_DISCONNECT_INDICATION, skb, &info);

	/* Drop reference count - see ircomm_tty_disconnect_indication(). */
	if(skb)
		dev_kfree_skb(skb);
}
/*
 * Function ircomm_open_lsap (self)
 *
 *    Open LSAP. This function will only be used when using "raw" services
 *
 */
int ircomm_open_lsap(struct ircomm_cb *self)
{
	notify_t notify;

	IRDA_DEBUG(0, "%s()\n", __FUNCTION__ );

	/* Register callbacks */
	irda_notify_init(&notify);
	notify.data_indication       = ircomm_lmp_data_indication;
	notify.connect_confirm       = ircomm_lmp_connect_confirm;
	notify.connect_indication    = ircomm_lmp_connect_indication;
	notify.disconnect_indication = ircomm_lmp_disconnect_indication;
	notify.instance = self;
	strlcpy(notify.name, "IrCOMM", sizeof(notify.name));

	self->lsap = irlmp_open_lsap(LSAP_ANY, &notify, 0);
	if (!self->lsap) {
		IRDA_DEBUG(0,"%sfailed to allocate tsap\n", __FUNCTION__ );
		return -1;
	}
	self->slsap_sel = self->lsap->slsap_sel;

	/*
	 *  Initialize the call-table for issuing commands
	 */
	self->issue.data_request       = ircomm_lmp_data_request;
	self->issue.connect_request    = ircomm_lmp_connect_request;
	self->issue.connect_response   = ircomm_lmp_connect_response;
	self->issue.disconnect_request = ircomm_lmp_disconnect_request;

	return 0;
}
