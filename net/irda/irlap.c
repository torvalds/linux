/*********************************************************************
 *
 * Filename:      irlap.c
 * Version:       1.0
 * Description:   IrLAP implementation for Linux
 * Status:        Stable
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Mon Aug  4 20:40:53 1997
 * Modified at:   Tue Dec 14 09:26:44 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 *
 *     Copyright (c) 1998-1999 Dag Brattli, All Rights Reserved.
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

#include <linux/slab.h>
#include <linux/string.h>
#include <linux/skbuff.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <linux/random.h>
#include <linux/module.h>
#include <linux/seq_file.h>

#include <net/irda/irda.h>
#include <net/irda/irda_device.h>
#include <net/irda/irqueue.h>
#include <net/irda/irlmp.h>
#include <net/irda/irlmp_frame.h>
#include <net/irda/irlap_frame.h>
#include <net/irda/irlap.h>
#include <net/irda/timer.h>
#include <net/irda/qos.h>

static hashbin_t *irlap = NULL;
int sysctl_slot_timeout = SLOT_TIMEOUT * 1000 / HZ;

/* This is the delay of missed pf period before generating an event
 * to the application. The spec mandate 3 seconds, but in some cases
 * it's way too long. - Jean II */
int sysctl_warn_noreply_time = 3;

extern void irlap_queue_xmit(struct irlap_cb *self, struct sk_buff *skb);
static void __irlap_close(struct irlap_cb *self);
static void irlap_init_qos_capabilities(struct irlap_cb *self,
					struct qos_info *qos_user);

#ifdef CONFIG_IRDA_DEBUG
static const char *const lap_reasons[] = {
	"ERROR, NOT USED",
	"LAP_DISC_INDICATION",
	"LAP_NO_RESPONSE",
	"LAP_RESET_INDICATION",
	"LAP_FOUND_NONE",
	"LAP_MEDIA_BUSY",
	"LAP_PRIMARY_CONFLICT",
	"ERROR, NOT USED",
};
#endif	/* CONFIG_IRDA_DEBUG */

int __init irlap_init(void)
{
	/* Check if the compiler did its job properly.
	 * May happen on some ARM configuration, check with Russell King. */
	IRDA_ASSERT(sizeof(struct xid_frame) == 14, ;);
	IRDA_ASSERT(sizeof(struct test_frame) == 10, ;);
	IRDA_ASSERT(sizeof(struct ua_frame) == 10, ;);
	IRDA_ASSERT(sizeof(struct snrm_frame) == 11, ;);

	/* Allocate master array */
	irlap = hashbin_new(HB_LOCK);
	if (irlap == NULL) {
		IRDA_ERROR("%s: can't allocate irlap hashbin!\n",
			   __func__);
		return -ENOMEM;
	}

	return 0;
}

void irlap_cleanup(void)
{
	IRDA_ASSERT(irlap != NULL, return;);

	hashbin_delete(irlap, (FREE_FUNC) __irlap_close);
}

/*
 * Function irlap_open (driver)
 *
 *    Initialize IrLAP layer
 *
 */
struct irlap_cb *irlap_open(struct net_device *dev, struct qos_info *qos,
			    const char *hw_name)
{
	struct irlap_cb *self;

	IRDA_DEBUG(4, "%s()\n", __func__);

	/* Initialize the irlap structure. */
	self = kzalloc(sizeof(struct irlap_cb), GFP_KERNEL);
	if (self == NULL)
		return NULL;

	self->magic = LAP_MAGIC;

	/* Make a binding between the layers */
	self->netdev = dev;
	self->qos_dev = qos;
	/* Copy hardware name */
	if(hw_name != NULL) {
		strlcpy(self->hw_name, hw_name, sizeof(self->hw_name));
	} else {
		self->hw_name[0] = '\0';
	}

	/* FIXME: should we get our own field? */
	dev->atalk_ptr = self;

	self->state = LAP_OFFLINE;

	/* Initialize transmit queue */
	skb_queue_head_init(&self->txq);
	skb_queue_head_init(&self->txq_ultra);
	skb_queue_head_init(&self->wx_list);

	/* My unique IrLAP device address! */
	/* We don't want the broadcast address, neither the NULL address
	 * (most often used to signify "invalid"), and we don't want an
	 * address already in use (otherwise connect won't be able
	 * to select the proper link). - Jean II */
	do {
		get_random_bytes(&self->saddr, sizeof(self->saddr));
	} while ((self->saddr == 0x0) || (self->saddr == BROADCAST) ||
		 (hashbin_lock_find(irlap, self->saddr, NULL)) );
	/* Copy to the driver */
	memcpy(dev->dev_addr, &self->saddr, 4);

	init_timer(&self->slot_timer);
	init_timer(&self->query_timer);
	init_timer(&self->discovery_timer);
	init_timer(&self->final_timer);
	init_timer(&self->poll_timer);
	init_timer(&self->wd_timer);
	init_timer(&self->backoff_timer);
	init_timer(&self->media_busy_timer);

	irlap_apply_default_connection_parameters(self);

	self->N3 = 3; /* # connections attemts to try before giving up */

	self->state = LAP_NDM;

	hashbin_insert(irlap, (irda_queue_t *) self, self->saddr, NULL);

	irlmp_register_link(self, self->saddr, &self->notify);

	return self;
}
EXPORT_SYMBOL(irlap_open);

/*
 * Function __irlap_close (self)
 *
 *    Remove IrLAP and all allocated memory. Stop any pending timers.
 *
 */
static void __irlap_close(struct irlap_cb *self)
{
	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == LAP_MAGIC, return;);

	/* Stop timers */
	del_timer(&self->slot_timer);
	del_timer(&self->query_timer);
	del_timer(&self->discovery_timer);
	del_timer(&self->final_timer);
	del_timer(&self->poll_timer);
	del_timer(&self->wd_timer);
	del_timer(&self->backoff_timer);
	del_timer(&self->media_busy_timer);

	irlap_flush_all_queues(self);

	self->magic = 0;

	kfree(self);
}

/*
 * Function irlap_close (self)
 *
 *    Remove IrLAP instance
 *
 */
void irlap_close(struct irlap_cb *self)
{
	struct irlap_cb *lap;

	IRDA_DEBUG(4, "%s()\n", __func__);

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == LAP_MAGIC, return;);

	/* We used to send a LAP_DISC_INDICATION here, but this was
	 * racy. This has been move within irlmp_unregister_link()
	 * itself. Jean II */

	/* Kill the LAP and all LSAPs on top of it */
	irlmp_unregister_link(self->saddr);
	self->notify.instance = NULL;

	/* Be sure that we manage to remove ourself from the hash */
	lap = hashbin_remove(irlap, self->saddr, NULL);
	if (!lap) {
		IRDA_DEBUG(1, "%s(), Didn't find myself!\n", __func__);
		return;
	}
	__irlap_close(lap);
}
EXPORT_SYMBOL(irlap_close);

/*
 * Function irlap_connect_indication (self, skb)
 *
 *    Another device is attempting to make a connection
 *
 */
void irlap_connect_indication(struct irlap_cb *self, struct sk_buff *skb)
{
	IRDA_DEBUG(4, "%s()\n", __func__);

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == LAP_MAGIC, return;);

	irlap_init_qos_capabilities(self, NULL); /* No user QoS! */

	irlmp_link_connect_indication(self->notify.instance, self->saddr,
				      self->daddr, &self->qos_tx, skb);
}

/*
 * Function irlap_connect_response (self, skb)
 *
 *    Service user has accepted incoming connection
 *
 */
void irlap_connect_response(struct irlap_cb *self, struct sk_buff *userdata)
{
	IRDA_DEBUG(4, "%s()\n", __func__);

	irlap_do_event(self, CONNECT_RESPONSE, userdata, NULL);
}

/*
 * Function irlap_connect_request (self, daddr, qos_user, sniff)
 *
 *    Request connection with another device, sniffing is not implemented
 *    yet.
 *
 */
void irlap_connect_request(struct irlap_cb *self, __u32 daddr,
			   struct qos_info *qos_user, int sniff)
{
	IRDA_DEBUG(3, "%s(), daddr=0x%08x\n", __func__, daddr);

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == LAP_MAGIC, return;);

	self->daddr = daddr;

	/*
	 *  If the service user specifies QoS values for this connection,
	 *  then use them
	 */
	irlap_init_qos_capabilities(self, qos_user);

	if ((self->state == LAP_NDM) && !self->media_busy)
		irlap_do_event(self, CONNECT_REQUEST, NULL, NULL);
	else
		self->connect_pending = TRUE;
}

/*
 * Function irlap_connect_confirm (self, skb)
 *
 *    Connection request has been accepted
 *
 */
void irlap_connect_confirm(struct irlap_cb *self, struct sk_buff *skb)
{
	IRDA_DEBUG(4, "%s()\n", __func__);

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == LAP_MAGIC, return;);

	irlmp_link_connect_confirm(self->notify.instance, &self->qos_tx, skb);
}

/*
 * Function irlap_data_indication (self, skb)
 *
 *    Received data frames from IR-port, so we just pass them up to
 *    IrLMP for further processing
 *
 */
void irlap_data_indication(struct irlap_cb *self, struct sk_buff *skb,
			   int unreliable)
{
	/* Hide LAP header from IrLMP layer */
	skb_pull(skb, LAP_ADDR_HEADER+LAP_CTRL_HEADER);

	irlmp_link_data_indication(self->notify.instance, skb, unreliable);
}


/*
 * Function irlap_data_request (self, skb)
 *
 *    Queue data for transmission, must wait until XMIT state
 *
 */
void irlap_data_request(struct irlap_cb *self, struct sk_buff *skb,
			int unreliable)
{
	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == LAP_MAGIC, return;);

	IRDA_DEBUG(3, "%s()\n", __func__);

	IRDA_ASSERT(skb_headroom(skb) >= (LAP_ADDR_HEADER+LAP_CTRL_HEADER),
		    return;);
	skb_push(skb, LAP_ADDR_HEADER+LAP_CTRL_HEADER);

	/*
	 *  Must set frame format now so that the rest of the code knows
	 *  if its dealing with an I or an UI frame
	 */
	if (unreliable)
		skb->data[1] = UI_FRAME;
	else
		skb->data[1] = I_FRAME;

	/* Don't forget to refcount it - see irlmp_connect_request(). */
	skb_get(skb);

	/* Add at the end of the queue (keep ordering) - Jean II */
	skb_queue_tail(&self->txq, skb);

	/*
	 *  Send event if this frame only if we are in the right state
	 *  FIXME: udata should be sent first! (skb_queue_head?)
	 */
	if ((self->state == LAP_XMIT_P) || (self->state == LAP_XMIT_S)) {
		/* If we are not already processing the Tx queue, trigger
		 * transmission immediately - Jean II */
		if((skb_queue_len(&self->txq) <= 1) && (!self->local_busy))
			irlap_do_event(self, DATA_REQUEST, skb, NULL);
		/* Otherwise, the packets will be sent normally at the
		 * next pf-poll - Jean II */
	}
}

/*
 * Function irlap_unitdata_request (self, skb)
 *
 *    Send Ultra data. This is data that must be sent outside any connection
 *
 */
#ifdef CONFIG_IRDA_ULTRA
void irlap_unitdata_request(struct irlap_cb *self, struct sk_buff *skb)
{
	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == LAP_MAGIC, return;);

	IRDA_DEBUG(3, "%s()\n", __func__);

	IRDA_ASSERT(skb_headroom(skb) >= (LAP_ADDR_HEADER+LAP_CTRL_HEADER),
	       return;);
	skb_push(skb, LAP_ADDR_HEADER+LAP_CTRL_HEADER);

	skb->data[0] = CBROADCAST;
	skb->data[1] = UI_FRAME;

	/* Don't need to refcount, see irlmp_connless_data_request() */

	skb_queue_tail(&self->txq_ultra, skb);

	irlap_do_event(self, SEND_UI_FRAME, NULL, NULL);
}
#endif /*CONFIG_IRDA_ULTRA */

/*
 * Function irlap_udata_indication (self, skb)
 *
 *    Receive Ultra data. This is data that is received outside any connection
 *
 */
#ifdef CONFIG_IRDA_ULTRA
void irlap_unitdata_indication(struct irlap_cb *self, struct sk_buff *skb)
{
	IRDA_DEBUG(1, "%s()\n", __func__);

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == LAP_MAGIC, return;);
	IRDA_ASSERT(skb != NULL, return;);

	/* Hide LAP header from IrLMP layer */
	skb_pull(skb, LAP_ADDR_HEADER+LAP_CTRL_HEADER);

	irlmp_link_unitdata_indication(self->notify.instance, skb);
}
#endif /* CONFIG_IRDA_ULTRA */

/*
 * Function irlap_disconnect_request (void)
 *
 *    Request to disconnect connection by service user
 */
void irlap_disconnect_request(struct irlap_cb *self)
{
	IRDA_DEBUG(3, "%s()\n", __func__);

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == LAP_MAGIC, return;);

	/* Don't disconnect until all data frames are successfully sent */
	if (!skb_queue_empty(&self->txq)) {
		self->disconnect_pending = TRUE;
		return;
	}

	/* Check if we are in the right state for disconnecting */
	switch (self->state) {
	case LAP_XMIT_P:        /* FALLTHROUGH */
	case LAP_XMIT_S:        /* FALLTHROUGH */
	case LAP_CONN:          /* FALLTHROUGH */
	case LAP_RESET_WAIT:    /* FALLTHROUGH */
	case LAP_RESET_CHECK:
		irlap_do_event(self, DISCONNECT_REQUEST, NULL, NULL);
		break;
	default:
		IRDA_DEBUG(2, "%s(), disconnect pending!\n", __func__);
		self->disconnect_pending = TRUE;
		break;
	}
}

/*
 * Function irlap_disconnect_indication (void)
 *
 *    Disconnect request from other device
 *
 */
void irlap_disconnect_indication(struct irlap_cb *self, LAP_REASON reason)
{
	IRDA_DEBUG(1, "%s(), reason=%s\n", __func__, lap_reasons[reason]);

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == LAP_MAGIC, return;);

	/* Flush queues */
	irlap_flush_all_queues(self);

	switch (reason) {
	case LAP_RESET_INDICATION:
		IRDA_DEBUG(1, "%s(), Sending reset request!\n", __func__);
		irlap_do_event(self, RESET_REQUEST, NULL, NULL);
		break;
	case LAP_NO_RESPONSE:	   /* FALLTHROUGH */
	case LAP_DISC_INDICATION:  /* FALLTHROUGH */
	case LAP_FOUND_NONE:       /* FALLTHROUGH */
	case LAP_MEDIA_BUSY:
		irlmp_link_disconnect_indication(self->notify.instance, self,
						 reason, NULL);
		break;
	default:
		IRDA_ERROR("%s: Unknown reason %d\n", __func__, reason);
	}
}

/*
 * Function irlap_discovery_request (gen_addr_bit)
 *
 *    Start one single discovery operation.
 *
 */
void irlap_discovery_request(struct irlap_cb *self, discovery_t *discovery)
{
	struct irlap_info info;

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == LAP_MAGIC, return;);
	IRDA_ASSERT(discovery != NULL, return;);

	IRDA_DEBUG(4, "%s(), nslots = %d\n", __func__, discovery->nslots);

	IRDA_ASSERT((discovery->nslots == 1) || (discovery->nslots == 6) ||
		    (discovery->nslots == 8) || (discovery->nslots == 16),
		    return;);

	/* Discovery is only possible in NDM mode */
	if (self->state != LAP_NDM) {
		IRDA_DEBUG(4, "%s(), discovery only possible in NDM mode\n",
			   __func__);
		irlap_discovery_confirm(self, NULL);
		/* Note : in theory, if we are not in NDM, we could postpone
		 * the discovery like we do for connection request.
		 * In practice, it's not worth it. If the media was busy,
		 * it's likely next time around it won't be busy. If we are
		 * in REPLY state, we will get passive discovery info & event.
		 * Jean II */
		return;
	}

	/* Check if last discovery request finished in time, or if
	 * it was aborted due to the media busy flag. */
	if (self->discovery_log != NULL) {
		hashbin_delete(self->discovery_log, (FREE_FUNC) kfree);
		self->discovery_log = NULL;
	}

	/* All operations will occur at predictable time, no need to lock */
	self->discovery_log = hashbin_new(HB_NOLOCK);

	if (self->discovery_log == NULL) {
		IRDA_WARNING("%s(), Unable to allocate discovery log!\n",
			     __func__);
		return;
	}

	info.S = discovery->nslots; /* Number of slots */
	info.s = 0; /* Current slot */

	self->discovery_cmd = discovery;
	info.discovery = discovery;

	/* sysctl_slot_timeout bounds are checked in irsysctl.c - Jean II */
	self->slot_timeout = sysctl_slot_timeout * HZ / 1000;

	irlap_do_event(self, DISCOVERY_REQUEST, NULL, &info);
}

/*
 * Function irlap_discovery_confirm (log)
 *
 *    A device has been discovered in front of this station, we
 *    report directly to LMP.
 */
void irlap_discovery_confirm(struct irlap_cb *self, hashbin_t *discovery_log)
{
	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == LAP_MAGIC, return;);

	IRDA_ASSERT(self->notify.instance != NULL, return;);

	/*
	 * Check for successful discovery, since we are then allowed to clear
	 * the media busy condition (IrLAP 6.13.4 - p.94). This should allow
	 * us to make connection attempts much faster and easier (i.e. no
	 * collisions).
	 * Setting media busy to false will also generate an event allowing
	 * to process pending events in NDM state machine.
	 * Note : the spec doesn't define what's a successful discovery is.
	 * If we want Ultra to work, it's successful even if there is
	 * nobody discovered - Jean II
	 */
	if (discovery_log)
		irda_device_set_media_busy(self->netdev, FALSE);

	/* Inform IrLMP */
	irlmp_link_discovery_confirm(self->notify.instance, discovery_log);
}

/*
 * Function irlap_discovery_indication (log)
 *
 *    Somebody is trying to discover us!
 *
 */
void irlap_discovery_indication(struct irlap_cb *self, discovery_t *discovery)
{
	IRDA_DEBUG(4, "%s()\n", __func__);

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == LAP_MAGIC, return;);
	IRDA_ASSERT(discovery != NULL, return;);

	IRDA_ASSERT(self->notify.instance != NULL, return;);

	/* A device is very likely to connect immediately after it performs
	 * a successful discovery. This means that in our case, we are much
	 * more likely to receive a connection request over the medium.
	 * So, we backoff to avoid collisions.
	 * IrLAP spec 6.13.4 suggest 100ms...
	 * Note : this little trick actually make a *BIG* difference. If I set
	 * my Linux box with discovery enabled and one Ultra frame sent every
	 * second, my Palm has no trouble connecting to it every time !
	 * Jean II */
	irda_device_set_media_busy(self->netdev, SMALL);

	irlmp_link_discovery_indication(self->notify.instance, discovery);
}

/*
 * Function irlap_status_indication (quality_of_link)
 */
void irlap_status_indication(struct irlap_cb *self, int quality_of_link)
{
	switch (quality_of_link) {
	case STATUS_NO_ACTIVITY:
		IRDA_MESSAGE("IrLAP, no activity on link!\n");
		break;
	case STATUS_NOISY:
		IRDA_MESSAGE("IrLAP, noisy link!\n");
		break;
	default:
		break;
	}
	irlmp_status_indication(self->notify.instance,
				quality_of_link, LOCK_NO_CHANGE);
}

/*
 * Function irlap_reset_indication (void)
 */
void irlap_reset_indication(struct irlap_cb *self)
{
	IRDA_DEBUG(1, "%s()\n", __func__);

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == LAP_MAGIC, return;);

	if (self->state == LAP_RESET_WAIT)
		irlap_do_event(self, RESET_REQUEST, NULL, NULL);
	else
		irlap_do_event(self, RESET_RESPONSE, NULL, NULL);
}

/*
 * Function irlap_reset_confirm (void)
 */
void irlap_reset_confirm(void)
{
	IRDA_DEBUG(1, "%s()\n", __func__);
}

/*
 * Function irlap_generate_rand_time_slot (S, s)
 *
 *    Generate a random time slot between s and S-1 where
 *    S = Number of slots (0 -> S-1)
 *    s = Current slot
 */
int irlap_generate_rand_time_slot(int S, int s)
{
	static int rand;
	int slot;

	IRDA_ASSERT((S - s) > 0, return 0;);

	rand += jiffies;
	rand ^= (rand << 12);
	rand ^= (rand >> 20);

	slot = s + rand % (S-s);

	IRDA_ASSERT((slot >= s) || (slot < S), return 0;);

	return slot;
}

/*
 * Function irlap_update_nr_received (nr)
 *
 *    Remove all acknowledged frames in current window queue. This code is
 *    not intuitive and you should not try to change it. If you think it
 *    contains bugs, please mail a patch to the author instead.
 */
void irlap_update_nr_received(struct irlap_cb *self, int nr)
{
	struct sk_buff *skb = NULL;
	int count = 0;

	/*
	 * Remove all the ack-ed frames from the window queue.
	 */

	/*
	 *  Optimize for the common case. It is most likely that the receiver
	 *  will acknowledge all the frames we have sent! So in that case we
	 *  delete all frames stored in window.
	 */
	if (nr == self->vs) {
		while ((skb = skb_dequeue(&self->wx_list)) != NULL) {
			dev_kfree_skb(skb);
		}
		/* The last acked frame is the next to send minus one */
		self->va = nr - 1;
	} else {
		/* Remove all acknowledged frames in current window */
		while ((skb_peek(&self->wx_list) != NULL) &&
		       (((self->va+1) % 8) != nr))
		{
			skb = skb_dequeue(&self->wx_list);
			dev_kfree_skb(skb);

			self->va = (self->va + 1) % 8;
			count++;
		}
	}

	/* Advance window */
	self->window = self->window_size - skb_queue_len(&self->wx_list);
}

/*
 * Function irlap_validate_ns_received (ns)
 *
 *    Validate the next to send (ns) field from received frame.
 */
int irlap_validate_ns_received(struct irlap_cb *self, int ns)
{
	/*  ns as expected?  */
	if (ns == self->vr)
		return NS_EXPECTED;
	/*
	 *  Stations are allowed to treat invalid NS as unexpected NS
	 *  IrLAP, Recv ... with-invalid-Ns. p. 84
	 */
	return NS_UNEXPECTED;

	/* return NR_INVALID; */
}
/*
 * Function irlap_validate_nr_received (nr)
 *
 *    Validate the next to receive (nr) field from received frame.
 *
 */
int irlap_validate_nr_received(struct irlap_cb *self, int nr)
{
	/*  nr as expected?  */
	if (nr == self->vs) {
		IRDA_DEBUG(4, "%s(), expected!\n", __func__);
		return NR_EXPECTED;
	}

	/*
	 *  unexpected nr? (but within current window), first we check if the
	 *  ns numbers of the frames in the current window wrap.
	 */
	if (self->va < self->vs) {
		if ((nr >= self->va) && (nr <= self->vs))
			return NR_UNEXPECTED;
	} else {
		if ((nr >= self->va) || (nr <= self->vs))
			return NR_UNEXPECTED;
	}

	/* Invalid nr!  */
	return NR_INVALID;
}

/*
 * Function irlap_initiate_connection_state ()
 *
 *    Initialize the connection state parameters
 *
 */
void irlap_initiate_connection_state(struct irlap_cb *self)
{
	IRDA_DEBUG(4, "%s()\n", __func__);

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == LAP_MAGIC, return;);

	/* Next to send and next to receive */
	self->vs = self->vr = 0;

	/* Last frame which got acked (0 - 1) % 8 */
	self->va = 7;

	self->window = 1;

	self->remote_busy = FALSE;
	self->retry_count = 0;
}

/*
 * Function irlap_wait_min_turn_around (self, qos)
 *
 *    Wait negotiated minimum turn around time, this function actually sets
 *    the number of BOS's that must be sent before the next transmitted
 *    frame in order to delay for the specified amount of time. This is
 *    done to avoid using timers, and the forbidden udelay!
 */
void irlap_wait_min_turn_around(struct irlap_cb *self, struct qos_info *qos)
{
	__u32 min_turn_time;
	__u32 speed;

	/* Get QoS values.  */
	speed = qos->baud_rate.value;
	min_turn_time = qos->min_turn_time.value;

	/* No need to calculate XBOFs for speeds over 115200 bps */
	if (speed > 115200) {
		self->mtt_required = min_turn_time;
		return;
	}

	/*
	 *  Send additional BOF's for the next frame for the requested
	 *  min turn time, so now we must calculate how many chars (XBOF's) we
	 *  must send for the requested time period (min turn time)
	 */
	self->xbofs_delay = irlap_min_turn_time_in_bytes(speed, min_turn_time);
}

/*
 * Function irlap_flush_all_queues (void)
 *
 *    Flush all queues
 *
 */
void irlap_flush_all_queues(struct irlap_cb *self)
{
	struct sk_buff* skb;

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == LAP_MAGIC, return;);

	/* Free transmission queue */
	while ((skb = skb_dequeue(&self->txq)) != NULL)
		dev_kfree_skb(skb);

	while ((skb = skb_dequeue(&self->txq_ultra)) != NULL)
		dev_kfree_skb(skb);

	/* Free sliding window buffered packets */
	while ((skb = skb_dequeue(&self->wx_list)) != NULL)
		dev_kfree_skb(skb);
}

/*
 * Function irlap_setspeed (self, speed)
 *
 *    Change the speed of the IrDA port
 *
 */
static void irlap_change_speed(struct irlap_cb *self, __u32 speed, int now)
{
	struct sk_buff *skb;

	IRDA_DEBUG(0, "%s(), setting speed to %d\n", __func__, speed);

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == LAP_MAGIC, return;);

	self->speed = speed;

	/* Change speed now, or just piggyback speed on frames */
	if (now) {
		/* Send down empty frame to trigger speed change */
		skb = alloc_skb(0, GFP_ATOMIC);
		if (skb)
			irlap_queue_xmit(self, skb);
	}
}

/*
 * Function irlap_init_qos_capabilities (self, qos)
 *
 *    Initialize QoS for this IrLAP session, What we do is to compute the
 *    intersection of the QoS capabilities for the user, driver and for
 *    IrLAP itself. Normally, IrLAP will not specify any values, but it can
 *    be used to restrict certain values.
 */
static void irlap_init_qos_capabilities(struct irlap_cb *self,
					struct qos_info *qos_user)
{
	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == LAP_MAGIC, return;);
	IRDA_ASSERT(self->netdev != NULL, return;);

	/* Start out with the maximum QoS support possible */
	irda_init_max_qos_capabilies(&self->qos_rx);

	/* Apply drivers QoS capabilities */
	irda_qos_compute_intersection(&self->qos_rx, self->qos_dev);

	/*
	 *  Check for user supplied QoS parameters. The service user is only
	 *  allowed to supply these values. We check each parameter since the
	 *  user may not have set all of them.
	 */
	if (qos_user) {
		IRDA_DEBUG(1, "%s(), Found user specified QoS!\n", __func__);

		if (qos_user->baud_rate.bits)
			self->qos_rx.baud_rate.bits &= qos_user->baud_rate.bits;

		if (qos_user->max_turn_time.bits)
			self->qos_rx.max_turn_time.bits &= qos_user->max_turn_time.bits;
		if (qos_user->data_size.bits)
			self->qos_rx.data_size.bits &= qos_user->data_size.bits;

		if (qos_user->link_disc_time.bits)
			self->qos_rx.link_disc_time.bits &= qos_user->link_disc_time.bits;
	}

	/* Use 500ms in IrLAP for now */
	self->qos_rx.max_turn_time.bits &= 0x01;

	/* Set data size */
	/*self->qos_rx.data_size.bits &= 0x03;*/

	irda_qos_bits_to_value(&self->qos_rx);
}

/*
 * Function irlap_apply_default_connection_parameters (void, now)
 *
 *    Use the default connection and transmission parameters
 */
void irlap_apply_default_connection_parameters(struct irlap_cb *self)
{
	IRDA_DEBUG(4, "%s()\n", __func__);

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == LAP_MAGIC, return;);

	/* xbofs : Default value in NDM */
	self->next_bofs   = 12;
	self->bofs_count  = 12;

	/* NDM Speed is 9600 */
	irlap_change_speed(self, 9600, TRUE);

	/* Set mbusy when going to NDM state */
	irda_device_set_media_busy(self->netdev, TRUE);

	/*
	 * Generate random connection address for this session, which must
	 * be 7 bits wide and different from 0x00 and 0xfe
	 */
	while ((self->caddr == 0x00) || (self->caddr == 0xfe)) {
		get_random_bytes(&self->caddr, sizeof(self->caddr));
		self->caddr &= 0xfe;
	}

	/* Use default values until connection has been negitiated */
	self->slot_timeout = sysctl_slot_timeout;
	self->final_timeout = FINAL_TIMEOUT;
	self->poll_timeout = POLL_TIMEOUT;
	self->wd_timeout = WD_TIMEOUT;

	/* Set some default values */
	self->qos_tx.baud_rate.value = 9600;
	self->qos_rx.baud_rate.value = 9600;
	self->qos_tx.max_turn_time.value = 0;
	self->qos_rx.max_turn_time.value = 0;
	self->qos_tx.min_turn_time.value = 0;
	self->qos_rx.min_turn_time.value = 0;
	self->qos_tx.data_size.value = 64;
	self->qos_rx.data_size.value = 64;
	self->qos_tx.window_size.value = 1;
	self->qos_rx.window_size.value = 1;
	self->qos_tx.additional_bofs.value = 12;
	self->qos_rx.additional_bofs.value = 12;
	self->qos_tx.link_disc_time.value = 0;
	self->qos_rx.link_disc_time.value = 0;

	irlap_flush_all_queues(self);

	self->disconnect_pending = FALSE;
	self->connect_pending = FALSE;
}

/*
 * Function irlap_apply_connection_parameters (qos, now)
 *
 *    Initialize IrLAP with the negotiated QoS values
 *
 * If 'now' is false, the speed and xbofs will be changed after the next
 * frame is sent.
 * If 'now' is true, the speed and xbofs is changed immediately
 */
void irlap_apply_connection_parameters(struct irlap_cb *self, int now)
{
	IRDA_DEBUG(4, "%s()\n", __func__);

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == LAP_MAGIC, return;);

	/* Set the negotiated xbofs value */
	self->next_bofs   = self->qos_tx.additional_bofs.value;
	if (now)
		self->bofs_count = self->next_bofs;

	/* Set the negotiated link speed (may need the new xbofs value) */
	irlap_change_speed(self, self->qos_tx.baud_rate.value, now);

	self->window_size = self->qos_tx.window_size.value;
	self->window      = self->qos_tx.window_size.value;

#ifdef CONFIG_IRDA_DYNAMIC_WINDOW
	/*
	 *  Calculate how many bytes it is possible to transmit before the
	 *  link must be turned around
	 */
	self->line_capacity =
		irlap_max_line_capacity(self->qos_tx.baud_rate.value,
					self->qos_tx.max_turn_time.value);
	self->bytes_left = self->line_capacity;
#endif /* CONFIG_IRDA_DYNAMIC_WINDOW */


	/*
	 *  Initialize timeout values, some of the rules are listed on
	 *  page 92 in IrLAP.
	 */
	IRDA_ASSERT(self->qos_tx.max_turn_time.value != 0, return;);
	IRDA_ASSERT(self->qos_rx.max_turn_time.value != 0, return;);
	/* The poll timeout applies only to the primary station.
	 * It defines the maximum time the primary stay in XMIT mode
	 * before timeout and turning the link around (sending a RR).
	 * Or, this is how much we can keep the pf bit in primary mode.
	 * Therefore, it must be lower or equal than our *OWN* max turn around.
	 * Jean II */
	self->poll_timeout = self->qos_tx.max_turn_time.value * HZ / 1000;
	/* The Final timeout applies only to the primary station.
	 * It defines the maximum time the primary wait (mostly in RECV mode)
	 * for an answer from the secondary station before polling it again.
	 * Therefore, it must be greater or equal than our *PARTNER*
	 * max turn around time - Jean II */
	self->final_timeout = self->qos_rx.max_turn_time.value * HZ / 1000;
	/* The Watchdog Bit timeout applies only to the secondary station.
	 * It defines the maximum time the secondary wait (mostly in RECV mode)
	 * for poll from the primary station before getting annoyed.
	 * Therefore, it must be greater or equal than our *PARTNER*
	 * max turn around time - Jean II */
	self->wd_timeout = self->final_timeout * 2;

	/*
	 * N1 and N2 are maximum retry count for *both* the final timer
	 * and the wd timer (with a factor 2) as defined above.
	 * After N1 retry of a timer, we give a warning to the user.
	 * After N2 retry, we consider the link dead and disconnect it.
	 * Jean II
	 */

	/*
	 *  Set N1 to 0 if Link Disconnect/Threshold Time = 3 and set it to
	 *  3 seconds otherwise. See page 71 in IrLAP for more details.
	 *  Actually, it's not always 3 seconds, as we allow to set
	 *  it via sysctl... Max maxtt is 500ms, and N1 need to be multiple
	 *  of 2, so 1 second is minimum we can allow. - Jean II
	 */
	if (self->qos_tx.link_disc_time.value == sysctl_warn_noreply_time)
		/*
		 * If we set N1 to 0, it will trigger immediately, which is
		 * not what we want. What we really want is to disable it,
		 * Jean II
		 */
		self->N1 = -2; /* Disable - Need to be multiple of 2*/
	else
		self->N1 = sysctl_warn_noreply_time * 1000 /
		  self->qos_rx.max_turn_time.value;

	IRDA_DEBUG(4, "Setting N1 = %d\n", self->N1);

	/* Set N2 to match our own disconnect time */
	self->N2 = self->qos_tx.link_disc_time.value * 1000 /
		self->qos_rx.max_turn_time.value;
	IRDA_DEBUG(4, "Setting N2 = %d\n", self->N2);
}

#ifdef CONFIG_PROC_FS
struct irlap_iter_state {
	int id;
};

static void *irlap_seq_start(struct seq_file *seq, loff_t *pos)
{
	struct irlap_iter_state *iter = seq->private;
	struct irlap_cb *self;

	/* Protect our access to the tsap list */
	spin_lock_irq(&irlap->hb_spinlock);
	iter->id = 0;

	for (self = (struct irlap_cb *) hashbin_get_first(irlap);
	     self; self = (struct irlap_cb *) hashbin_get_next(irlap)) {
		if (iter->id == *pos)
			break;
		++iter->id;
	}

	return self;
}

static void *irlap_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct irlap_iter_state *iter = seq->private;

	++*pos;
	++iter->id;
	return (void *) hashbin_get_next(irlap);
}

static void irlap_seq_stop(struct seq_file *seq, void *v)
{
	spin_unlock_irq(&irlap->hb_spinlock);
}

static int irlap_seq_show(struct seq_file *seq, void *v)
{
	const struct irlap_iter_state *iter = seq->private;
	const struct irlap_cb *self = v;

	IRDA_ASSERT(self->magic == LAP_MAGIC, return -EINVAL;);

	seq_printf(seq, "irlap%d ", iter->id);
	seq_printf(seq, "state: %s\n",
		   irlap_state[self->state]);

	seq_printf(seq, "  device name: %s, ",
		   (self->netdev) ? self->netdev->name : "bug");
	seq_printf(seq, "hardware name: %s\n", self->hw_name);

	seq_printf(seq, "  caddr: %#02x, ", self->caddr);
	seq_printf(seq, "saddr: %#08x, ", self->saddr);
	seq_printf(seq, "daddr: %#08x\n", self->daddr);

	seq_printf(seq, "  win size: %d, ",
		   self->window_size);
	seq_printf(seq, "win: %d, ", self->window);
#ifdef CONFIG_IRDA_DYNAMIC_WINDOW
	seq_printf(seq, "line capacity: %d, ",
		   self->line_capacity);
	seq_printf(seq, "bytes left: %d\n", self->bytes_left);
#endif /* CONFIG_IRDA_DYNAMIC_WINDOW */
	seq_printf(seq, "  tx queue len: %d ",
		   skb_queue_len(&self->txq));
	seq_printf(seq, "win queue len: %d ",
		   skb_queue_len(&self->wx_list));
	seq_printf(seq, "rbusy: %s", self->remote_busy ?
		   "TRUE" : "FALSE");
	seq_printf(seq, " mbusy: %s\n", self->media_busy ?
		   "TRUE" : "FALSE");

	seq_printf(seq, "  retrans: %d ", self->retry_count);
	seq_printf(seq, "vs: %d ", self->vs);
	seq_printf(seq, "vr: %d ", self->vr);
	seq_printf(seq, "va: %d\n", self->va);

	seq_printf(seq, "  qos\tbps\tmaxtt\tdsize\twinsize\taddbofs\tmintt\tldisc\tcomp\n");

	seq_printf(seq, "  tx\t%d\t",
		   self->qos_tx.baud_rate.value);
	seq_printf(seq, "%d\t",
		   self->qos_tx.max_turn_time.value);
	seq_printf(seq, "%d\t",
		   self->qos_tx.data_size.value);
	seq_printf(seq, "%d\t",
		   self->qos_tx.window_size.value);
	seq_printf(seq, "%d\t",
		   self->qos_tx.additional_bofs.value);
	seq_printf(seq, "%d\t",
		   self->qos_tx.min_turn_time.value);
	seq_printf(seq, "%d\t",
		   self->qos_tx.link_disc_time.value);
	seq_printf(seq, "\n");

	seq_printf(seq, "  rx\t%d\t",
		   self->qos_rx.baud_rate.value);
	seq_printf(seq, "%d\t",
		   self->qos_rx.max_turn_time.value);
	seq_printf(seq, "%d\t",
		   self->qos_rx.data_size.value);
	seq_printf(seq, "%d\t",
		   self->qos_rx.window_size.value);
	seq_printf(seq, "%d\t",
		   self->qos_rx.additional_bofs.value);
	seq_printf(seq, "%d\t",
		   self->qos_rx.min_turn_time.value);
	seq_printf(seq, "%d\n",
		   self->qos_rx.link_disc_time.value);

	return 0;
}

static const struct seq_operations irlap_seq_ops = {
	.start  = irlap_seq_start,
	.next   = irlap_seq_next,
	.stop   = irlap_seq_stop,
	.show   = irlap_seq_show,
};

static int irlap_seq_open(struct inode *inode, struct file *file)
{
	if (irlap == NULL)
		return -EINVAL;

	return seq_open_private(file, &irlap_seq_ops,
			sizeof(struct irlap_iter_state));
}

const struct file_operations irlap_seq_fops = {
	.owner		= THIS_MODULE,
	.open           = irlap_seq_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release	= seq_release_private,
};

#endif /* CONFIG_PROC_FS */
