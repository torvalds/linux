/*********************************************************************
 *
 * Filename:      irlap_event.c
 * Version:       0.9
 * Description:   IrLAP state machine implementation
 * Status:        Experimental.
 * Author:        Dag Brattli <dag@brattli.net>
 * Created at:    Sat Aug 16 00:59:29 1997
 * Modified at:   Sat Dec 25 21:07:57 1999
 * Modified by:   Dag Brattli <dag@brattli.net>
 *
 *     Copyright (c) 1998-2000 Dag Brattli <dag@brattli.net>,
 *     Copyright (c) 1998      Thomas Davis <ratbert@radiks.net>
 *     All Rights Reserved.
 *     Copyright (c) 2000-2003 Jean Tourrilhes <jt@hpl.hp.com>
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

#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/skbuff.h>

#include <net/irda/irda.h>
#include <net/irda/irlap_event.h>

#include <net/irda/timer.h>
#include <net/irda/irlap.h>
#include <net/irda/irlap_frame.h>
#include <net/irda/qos.h>
#include <net/irda/parameters.h>
#include <net/irda/irlmp.h>		/* irlmp_flow_indication(), ... */

#include <net/irda/irda_device.h>

#ifdef CONFIG_IRDA_FAST_RR
int sysctl_fast_poll_increase = 50;
#endif

static int irlap_state_ndm    (struct irlap_cb *self, IRLAP_EVENT event,
			       struct sk_buff *skb, struct irlap_info *info);
static int irlap_state_query  (struct irlap_cb *self, IRLAP_EVENT event,
			       struct sk_buff *skb, struct irlap_info *info);
static int irlap_state_reply  (struct irlap_cb *self, IRLAP_EVENT event,
			       struct sk_buff *skb, struct irlap_info *info);
static int irlap_state_conn   (struct irlap_cb *self, IRLAP_EVENT event,
			       struct sk_buff *skb, struct irlap_info *info);
static int irlap_state_setup  (struct irlap_cb *self, IRLAP_EVENT event,
			       struct sk_buff *skb, struct irlap_info *info);
static int irlap_state_offline(struct irlap_cb *self, IRLAP_EVENT event,
			       struct sk_buff *skb, struct irlap_info *info);
static int irlap_state_xmit_p (struct irlap_cb *self, IRLAP_EVENT event,
			       struct sk_buff *skb, struct irlap_info *info);
static int irlap_state_pclose (struct irlap_cb *self, IRLAP_EVENT event,
			       struct sk_buff *skb, struct irlap_info *info);
static int irlap_state_nrm_p  (struct irlap_cb *self, IRLAP_EVENT event,
			       struct sk_buff *skb, struct irlap_info *info);
static int irlap_state_reset_wait(struct irlap_cb *self, IRLAP_EVENT event,
				  struct sk_buff *skb, struct irlap_info *info);
static int irlap_state_reset  (struct irlap_cb *self, IRLAP_EVENT event,
			       struct sk_buff *skb, struct irlap_info *info);
static int irlap_state_nrm_s  (struct irlap_cb *self, IRLAP_EVENT event,
			       struct sk_buff *skb, struct irlap_info *info);
static int irlap_state_xmit_s (struct irlap_cb *self, IRLAP_EVENT event,
			       struct sk_buff *skb, struct irlap_info *info);
static int irlap_state_sclose (struct irlap_cb *self, IRLAP_EVENT event,
			       struct sk_buff *skb, struct irlap_info *info);
static int irlap_state_reset_check(struct irlap_cb *, IRLAP_EVENT event,
				   struct sk_buff *, struct irlap_info *);

#ifdef CONFIG_IRDA_DEBUG
static const char *irlap_event[] = {
	"DISCOVERY_REQUEST",
	"CONNECT_REQUEST",
	"CONNECT_RESPONSE",
	"DISCONNECT_REQUEST",
	"DATA_REQUEST",
	"RESET_REQUEST",
	"RESET_RESPONSE",
	"SEND_I_CMD",
	"SEND_UI_FRAME",
	"RECV_DISCOVERY_XID_CMD",
	"RECV_DISCOVERY_XID_RSP",
	"RECV_SNRM_CMD",
	"RECV_TEST_CMD",
	"RECV_TEST_RSP",
	"RECV_UA_RSP",
	"RECV_DM_RSP",
	"RECV_RD_RSP",
	"RECV_I_CMD",
	"RECV_I_RSP",
	"RECV_UI_FRAME",
	"RECV_FRMR_RSP",
	"RECV_RR_CMD",
	"RECV_RR_RSP",
	"RECV_RNR_CMD",
	"RECV_RNR_RSP",
	"RECV_REJ_CMD",
	"RECV_REJ_RSP",
	"RECV_SREJ_CMD",
	"RECV_SREJ_RSP",
	"RECV_DISC_CMD",
	"SLOT_TIMER_EXPIRED",
	"QUERY_TIMER_EXPIRED",
	"FINAL_TIMER_EXPIRED",
	"POLL_TIMER_EXPIRED",
	"DISCOVERY_TIMER_EXPIRED",
	"WD_TIMER_EXPIRED",
	"BACKOFF_TIMER_EXPIRED",
	"MEDIA_BUSY_TIMER_EXPIRED",
};
#endif	/* CONFIG_IRDA_DEBUG */

const char *irlap_state[] = {
	"LAP_NDM",
	"LAP_QUERY",
	"LAP_REPLY",
	"LAP_CONN",
	"LAP_SETUP",
	"LAP_OFFLINE",
	"LAP_XMIT_P",
	"LAP_PCLOSE",
	"LAP_NRM_P",
	"LAP_RESET_WAIT",
	"LAP_RESET",
	"LAP_NRM_S",
	"LAP_XMIT_S",
	"LAP_SCLOSE",
	"LAP_RESET_CHECK",
};

static int (*state[])(struct irlap_cb *self, IRLAP_EVENT event,
		      struct sk_buff *skb, struct irlap_info *info) =
{
	irlap_state_ndm,
	irlap_state_query,
	irlap_state_reply,
	irlap_state_conn,
	irlap_state_setup,
	irlap_state_offline,
	irlap_state_xmit_p,
	irlap_state_pclose,
	irlap_state_nrm_p,
	irlap_state_reset_wait,
	irlap_state_reset,
	irlap_state_nrm_s,
	irlap_state_xmit_s,
	irlap_state_sclose,
	irlap_state_reset_check,
};

/*
 * Function irda_poll_timer_expired (data)
 *
 *    Poll timer has expired. Normally we must now send a RR frame to the
 *    remote device
 */
static void irlap_poll_timer_expired(void *data)
{
	struct irlap_cb *self = (struct irlap_cb *) data;

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == LAP_MAGIC, return;);

	irlap_do_event(self, POLL_TIMER_EXPIRED, NULL, NULL);
}

/*
 * Calculate and set time before we will have to send back the pf bit
 * to the peer. Use in primary.
 * Make sure that state is XMIT_P/XMIT_S when calling this function
 * (and that nobody messed up with the state). - Jean II
 */
static void irlap_start_poll_timer(struct irlap_cb *self, int timeout)
{
	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == LAP_MAGIC, return;);

#ifdef CONFIG_IRDA_FAST_RR
	/*
	 * Send out the RR frames faster if our own transmit queue is empty, or
	 * if the peer is busy. The effect is a much faster conversation
	 */
	if (skb_queue_empty(&self->txq) || self->remote_busy) {
		if (self->fast_RR == TRUE) {
			/*
			 *  Assert that the fast poll timer has not reached the
			 *  normal poll timer yet
			 */
			if (self->fast_RR_timeout < timeout) {
				/*
				 *  FIXME: this should be a more configurable
				 *         function
				 */
				self->fast_RR_timeout +=
					(sysctl_fast_poll_increase * HZ/1000);

				/* Use this fast(er) timeout instead */
				timeout = self->fast_RR_timeout;
			}
		} else {
			self->fast_RR = TRUE;

			/* Start with just 0 ms */
			self->fast_RR_timeout = 0;
			timeout = 0;
		}
	} else
		self->fast_RR = FALSE;

	IRDA_DEBUG(3, "%s(), timeout=%d (%ld)\n", __func__, timeout, jiffies);
#endif /* CONFIG_IRDA_FAST_RR */

	if (timeout == 0)
		irlap_do_event(self, POLL_TIMER_EXPIRED, NULL, NULL);
	else
		irda_start_timer(&self->poll_timer, timeout, self,
				 irlap_poll_timer_expired);
}

/*
 * Function irlap_do_event (event, skb, info)
 *
 *    Rushes through the state machine without any delay. If state == XMIT
 *    then send queued data frames.
 */
void irlap_do_event(struct irlap_cb *self, IRLAP_EVENT event,
		    struct sk_buff *skb, struct irlap_info *info)
{
	int ret;

	if (!self || self->magic != LAP_MAGIC)
		return;

	IRDA_DEBUG(3, "%s(), event = %s, state = %s\n", __func__,
		   irlap_event[event], irlap_state[self->state]);

	ret = (*state[self->state])(self, event, skb, info);

	/*
	 *  Check if there are any pending events that needs to be executed
	 */
	switch (self->state) {
	case LAP_XMIT_P: /* FALLTHROUGH */
	case LAP_XMIT_S:
		/*
		 * We just received the pf bit and are at the beginning
		 * of a new LAP transmit window.
		 * Check if there are any queued data frames, and do not
		 * try to disconnect link if we send any data frames, since
		 * that will change the state away form XMIT
		 */
		IRDA_DEBUG(2, "%s() : queue len = %d\n", __func__,
			   skb_queue_len(&self->txq));

		if (!skb_queue_empty(&self->txq)) {
			/* Prevent race conditions with irlap_data_request() */
			self->local_busy = TRUE;

			/* Theory of operation.
			 * We send frames up to when we fill the window or
			 * reach line capacity. Those frames will queue up
			 * in the device queue, and the driver will slowly
			 * send them.
			 * After each frame that we send, we poll the higher
			 * layer for more data. It's the right time to do
			 * that because the link layer need to perform the mtt
			 * and then send the first frame, so we can afford
			 * to send a bit of time in kernel space.
			 * The explicit flow indication allow to minimise
			 * buffers (== lower latency), to avoid higher layer
			 * polling via timers (== less context switches) and
			 * to implement a crude scheduler - Jean II */

			/* Try to send away all queued data frames */
			while ((skb = skb_dequeue(&self->txq)) != NULL) {
				/* Send one frame */
				ret = (*state[self->state])(self, SEND_I_CMD,
							    skb, NULL);
				/* Drop reference count.
				 * It will be increase as needed in
				 * irlap_send_data_xxx() */
				kfree_skb(skb);

				/* Poll the higher layers for one more frame */
				irlmp_flow_indication(self->notify.instance,
						      FLOW_START);

				if (ret == -EPROTO)
					break; /* Try again later! */
			}
			/* Finished transmitting */
			self->local_busy = FALSE;
		} else if (self->disconnect_pending) {
			self->disconnect_pending = FALSE;

			ret = (*state[self->state])(self, DISCONNECT_REQUEST,
						    NULL, NULL);
		}
		break;
/*	case LAP_NDM: */
/*	case LAP_CONN: */
/*	case LAP_RESET_WAIT: */
/*	case LAP_RESET_CHECK: */
	default:
		break;
	}
}

/*
 * Function irlap_state_ndm (event, skb, frame)
 *
 *    NDM (Normal Disconnected Mode) state
 *
 */
static int irlap_state_ndm(struct irlap_cb *self, IRLAP_EVENT event,
			   struct sk_buff *skb, struct irlap_info *info)
{
	discovery_t *discovery_rsp;
	int ret = 0;

	IRDA_ASSERT(self != NULL, return -1;);
	IRDA_ASSERT(self->magic == LAP_MAGIC, return -1;);

	switch (event) {
	case CONNECT_REQUEST:
		IRDA_ASSERT(self->netdev != NULL, return -1;);

		if (self->media_busy) {
			/* Note : this will never happen, because we test
			 * media busy in irlap_connect_request() and
			 * postpone the event... - Jean II */
			IRDA_DEBUG(0, "%s(), CONNECT_REQUEST: media busy!\n",
				   __func__);

			/* Always switch state before calling upper layers */
			irlap_next_state(self, LAP_NDM);

			irlap_disconnect_indication(self, LAP_MEDIA_BUSY);
		} else {
			irlap_send_snrm_frame(self, &self->qos_rx);

			/* Start Final-bit timer */
			irlap_start_final_timer(self, self->final_timeout);

			self->retry_count = 0;
			irlap_next_state(self, LAP_SETUP);
		}
		break;
	case RECV_SNRM_CMD:
		/* Check if the frame contains and I field */
		if (info) {
			self->daddr = info->daddr;
			self->caddr = info->caddr;

			irlap_next_state(self, LAP_CONN);

			irlap_connect_indication(self, skb);
		} else {
			IRDA_DEBUG(0, "%s(), SNRM frame does not "
				   "contain an I field!\n", __func__);
		}
		break;
	case DISCOVERY_REQUEST:
		IRDA_ASSERT(info != NULL, return -1;);

		if (self->media_busy) {
			IRDA_DEBUG(1, "%s(), DISCOVERY_REQUEST: media busy!\n",
				   __func__);
			/* irlap->log.condition = MEDIA_BUSY; */

			/* This will make IrLMP try again */
			irlap_discovery_confirm(self, NULL);
			/* Note : the discovery log is not cleaned up here,
			 * it will be done in irlap_discovery_request()
			 * Jean II */
			return 0;
		}

		self->S = info->S;
		self->s = info->s;
		irlap_send_discovery_xid_frame(self, info->S, info->s, TRUE,
					       info->discovery);
		self->frame_sent = FALSE;
		self->s++;

		irlap_start_slot_timer(self, self->slot_timeout);
		irlap_next_state(self, LAP_QUERY);
		break;
	case RECV_DISCOVERY_XID_CMD:
		IRDA_ASSERT(info != NULL, return -1;);

		/* Assert that this is not the final slot */
		if (info->s <= info->S) {
			self->slot = irlap_generate_rand_time_slot(info->S,
								   info->s);
			if (self->slot == info->s) {
				discovery_rsp = irlmp_get_discovery_response();
				discovery_rsp->data.daddr = info->daddr;

				irlap_send_discovery_xid_frame(self, info->S,
							       self->slot,
							       FALSE,
							       discovery_rsp);
				self->frame_sent = TRUE;
			} else
				self->frame_sent = FALSE;

			/*
			 * Go to reply state until end of discovery to
			 * inhibit our own transmissions. Set the timer
			 * to not stay forever there... Jean II
			 */
			irlap_start_query_timer(self, info->S, info->s);
			irlap_next_state(self, LAP_REPLY);
		} else {
		/* This is the final slot. How is it possible ?
		 * This would happen is both discoveries are just slightly
		 * offset (if they are in sync, all packets are lost).
		 * Most often, all the discovery requests will be received
		 * in QUERY state (see my comment there), except for the
		 * last frame that will come here.
		 * The big trouble when it happen is that active discovery
		 * doesn't happen, because nobody answer the discoveries
		 * frame of the other guy, so the log shows up empty.
		 * What should we do ?
		 * Not much. It's too late to answer those discovery frames,
		 * so we just pass the info to IrLMP who will put it in the
		 * log (and post an event).
		 * Another cause would be devices that do discovery much
		 * slower than us, however the latest fixes should minimise
		 * those cases...
		 * Jean II
		 */
			IRDA_DEBUG(1, "%s(), Receiving final discovery request, missed the discovery slots :-(\n", __func__);

			/* Last discovery request -> in the log */
			irlap_discovery_indication(self, info->discovery);
		}
		break;
	case MEDIA_BUSY_TIMER_EXPIRED:
		/* A bunch of events may be postponed because the media is
		 * busy (usually immediately after we close a connection),
		 * or while we are doing discovery (state query/reply).
		 * In all those cases, the media busy flag will be cleared
		 * when it's OK for us to process those postponed events.
		 * This event is not mentioned in the state machines in the
		 * IrLAP spec. It's because they didn't consider Ultra and
		 * postponing connection request is optional.
		 * Jean II */
#ifdef CONFIG_IRDA_ULTRA
		/* Send any pending Ultra frames if any */
		if (!skb_queue_empty(&self->txq_ultra)) {
			/* We don't send the frame, just post an event.
			 * Also, previously this code was in timer.c...
			 * Jean II */
			ret = (*state[self->state])(self, SEND_UI_FRAME,
						    NULL, NULL);
		}
#endif /* CONFIG_IRDA_ULTRA */
		/* Check if we should try to connect.
		 * This code was previously in irlap_do_event() */
		if (self->connect_pending) {
			self->connect_pending = FALSE;

			/* This one *should* not pend in this state, except
			 * if a socket try to connect and immediately
			 * disconnect. - clear - Jean II */
			if (self->disconnect_pending)
				irlap_disconnect_indication(self, LAP_DISC_INDICATION);
			else
				ret = (*state[self->state])(self,
							    CONNECT_REQUEST,
							    NULL, NULL);
			self->disconnect_pending = FALSE;
		}
		/* Note : one way to test if this code works well (including
		 * media busy and small busy) is to create a user space
		 * application generating an Ultra packet every 3.05 sec (or
		 * 2.95 sec) and to see how it interact with discovery.
		 * It's fairly easy to check that no packet is lost, that the
		 * packets are postponed during discovery and that after
		 * discovery indication you have a 100ms "gap".
		 * As connection request and Ultra are now processed the same
		 * way, this avoid the tedious job of trying IrLAP connection
		 * in all those cases...
		 * Jean II */
		break;
#ifdef CONFIG_IRDA_ULTRA
	case SEND_UI_FRAME:
	{
		int i;
		/* Only allowed to repeat an operation twice */
		for (i=0; ((i<2) && (self->media_busy == FALSE)); i++) {
			skb = skb_dequeue(&self->txq_ultra);
			if (skb)
				irlap_send_ui_frame(self, skb, CBROADCAST,
						    CMD_FRAME);
			else
				break;
			/* irlap_send_ui_frame() won't increase skb reference
			 * count, so no dev_kfree_skb() - Jean II */
		}
		if (i == 2) {
			/* Force us to listen 500 ms again */
			irda_device_set_media_busy(self->netdev, TRUE);
		}
		break;
	}
	case RECV_UI_FRAME:
		/* Only accept broadcast frames in NDM mode */
		if (info->caddr != CBROADCAST) {
			IRDA_DEBUG(0, "%s(), not a broadcast frame!\n",
				   __func__);
		} else
			irlap_unitdata_indication(self, skb);
		break;
#endif /* CONFIG_IRDA_ULTRA */
	case RECV_TEST_CMD:
		/* Remove test frame header */
		skb_pull(skb, sizeof(struct test_frame));

		/*
		 * Send response. This skb will not be sent out again, and
		 * will only be used to send out the same info as the cmd
		 */
		irlap_send_test_frame(self, CBROADCAST, info->daddr, skb);
		break;
	case RECV_TEST_RSP:
		IRDA_DEBUG(0, "%s() not implemented!\n", __func__);
		break;
	default:
		IRDA_DEBUG(2, "%s(), Unknown event %s\n", __func__,
			   irlap_event[event]);

		ret = -1;
		break;
	}
	return ret;
}

/*
 * Function irlap_state_query (event, skb, info)
 *
 *    QUERY state
 *
 */
static int irlap_state_query(struct irlap_cb *self, IRLAP_EVENT event,
			     struct sk_buff *skb, struct irlap_info *info)
{
	int ret = 0;

	IRDA_ASSERT(self != NULL, return -1;);
	IRDA_ASSERT(self->magic == LAP_MAGIC, return -1;);

	switch (event) {
	case RECV_DISCOVERY_XID_RSP:
		IRDA_ASSERT(info != NULL, return -1;);
		IRDA_ASSERT(info->discovery != NULL, return -1;);

		IRDA_DEBUG(4, "%s(), daddr=%08x\n", __func__,
			   info->discovery->data.daddr);

		if (!self->discovery_log) {
			IRDA_WARNING("%s: discovery log is gone! "
				     "maybe the discovery timeout has been set"
				     " too short?\n", __func__);
			break;
		}
		hashbin_insert(self->discovery_log,
			       (irda_queue_t *) info->discovery,
			       info->discovery->data.daddr, NULL);

		/* Keep state */
		/* irlap_next_state(self, LAP_QUERY);  */

		break;
	case RECV_DISCOVERY_XID_CMD:
		/* Yes, it is possible to receive those frames in this mode.
		 * Note that most often the last discovery request won't
		 * occur here but in NDM state (see my comment there).
		 * What should we do ?
		 * Not much. We are currently performing our own discovery,
		 * therefore we can't answer those frames. We don't want
		 * to change state either. We just pass the info to
		 * IrLMP who will put it in the log (and post an event).
		 * Jean II
		 */

		IRDA_ASSERT(info != NULL, return -1;);

		IRDA_DEBUG(1, "%s(), Receiving discovery request (s = %d) while performing discovery :-(\n", __func__, info->s);

		/* Last discovery request ? */
		if (info->s == 0xff)
			irlap_discovery_indication(self, info->discovery);
		break;
	case SLOT_TIMER_EXPIRED:
		/*
		 * Wait a little longer if we detect an incoming frame. This
		 * is not mentioned in the spec, but is a good thing to do,
		 * since we want to work even with devices that violate the
		 * timing requirements.
		 */
		if (irda_device_is_receiving(self->netdev) && !self->add_wait) {
			IRDA_DEBUG(2, "%s(), device is slow to answer, "
				   "waiting some more!\n", __func__);
			irlap_start_slot_timer(self, msecs_to_jiffies(10));
			self->add_wait = TRUE;
			return ret;
		}
		self->add_wait = FALSE;

		if (self->s < self->S) {
			irlap_send_discovery_xid_frame(self, self->S,
						       self->s, TRUE,
						       self->discovery_cmd);
			self->s++;
			irlap_start_slot_timer(self, self->slot_timeout);

			/* Keep state */
			irlap_next_state(self, LAP_QUERY);
		} else {
			/* This is the final slot! */
			irlap_send_discovery_xid_frame(self, self->S, 0xff,
						       TRUE,
						       self->discovery_cmd);

			/* Always switch state before calling upper layers */
			irlap_next_state(self, LAP_NDM);

			/*
			 *  We are now finished with the discovery procedure,
			 *  so now we must return the results
			 */
			irlap_discovery_confirm(self, self->discovery_log);

			/* IrLMP should now have taken care of the log */
			self->discovery_log = NULL;
		}
		break;
	default:
		IRDA_DEBUG(2, "%s(), Unknown event %s\n", __func__,
			   irlap_event[event]);

		ret = -1;
		break;
	}
	return ret;
}

/*
 * Function irlap_state_reply (self, event, skb, info)
 *
 *    REPLY, we have received a XID discovery frame from a device and we
 *    are waiting for the right time slot to send a response XID frame
 *
 */
static int irlap_state_reply(struct irlap_cb *self, IRLAP_EVENT event,
			     struct sk_buff *skb, struct irlap_info *info)
{
	discovery_t *discovery_rsp;
	int ret=0;

	IRDA_DEBUG(4, "%s()\n", __func__);

	IRDA_ASSERT(self != NULL, return -1;);
	IRDA_ASSERT(self->magic == LAP_MAGIC, return -1;);

	switch (event) {
	case QUERY_TIMER_EXPIRED:
		IRDA_DEBUG(0, "%s(), QUERY_TIMER_EXPIRED <%ld>\n",
			   __func__, jiffies);
		irlap_next_state(self, LAP_NDM);
		break;
	case RECV_DISCOVERY_XID_CMD:
		IRDA_ASSERT(info != NULL, return -1;);
		/* Last frame? */
		if (info->s == 0xff) {
			del_timer(&self->query_timer);

			/* info->log.condition = REMOTE; */

			/* Always switch state before calling upper layers */
			irlap_next_state(self, LAP_NDM);

			irlap_discovery_indication(self, info->discovery);
		} else {
			/* If it's our slot, send our reply */
			if ((info->s >= self->slot) && (!self->frame_sent)) {
				discovery_rsp = irlmp_get_discovery_response();
				discovery_rsp->data.daddr = info->daddr;

				irlap_send_discovery_xid_frame(self, info->S,
							       self->slot,
							       FALSE,
							       discovery_rsp);

				self->frame_sent = TRUE;
			}
			/* Readjust our timer to accomodate devices
			 * doing faster or slower discovery than us...
			 * Jean II */
			irlap_start_query_timer(self, info->S, info->s);

			/* Keep state */
			//irlap_next_state(self, LAP_REPLY);
		}
		break;
	default:
		IRDA_DEBUG(1, "%s(), Unknown event %d, %s\n", __func__,
			   event, irlap_event[event]);

		ret = -1;
		break;
	}
	return ret;
}

/*
 * Function irlap_state_conn (event, skb, info)
 *
 *    CONN, we have received a SNRM command and is waiting for the upper
 *    layer to accept or refuse connection
 *
 */
static int irlap_state_conn(struct irlap_cb *self, IRLAP_EVENT event,
			    struct sk_buff *skb, struct irlap_info *info)
{
	int ret = 0;

	IRDA_DEBUG(4, "%s(), event=%s\n", __func__, irlap_event[ event]);

	IRDA_ASSERT(self != NULL, return -1;);
	IRDA_ASSERT(self->magic == LAP_MAGIC, return -1;);

	switch (event) {
	case CONNECT_RESPONSE:
		skb_pull(skb, sizeof(struct snrm_frame));

		IRDA_ASSERT(self->netdev != NULL, return -1;);

		irlap_qos_negotiate(self, skb);

		irlap_initiate_connection_state(self);

		/*
		 * Applying the parameters now will make sure we change speed
		 * *after* we have sent the next frame
		 */
		irlap_apply_connection_parameters(self, FALSE);

		/*
		 * Sending this frame will force a speed change after it has
		 * been sent (i.e. the frame will be sent at 9600).
		 */
		irlap_send_ua_response_frame(self, &self->qos_rx);

#if 0
		/*
		 * We are allowed to send two frames, but this may increase
		 * the connect latency, so lets not do it for now.
		 */
		/* This is full of good intentions, but doesn't work in
		 * practice.
		 * After sending the first UA response, we switch the
		 * dongle to the negotiated speed, which is usually
		 * different than 9600 kb/s.
		 * From there, there is two solutions :
		 * 1) The other end has received the first UA response :
		 * it will set up the connection, move to state LAP_NRM_P,
		 * and will ignore and drop the second UA response.
		 * Actually, it's even worse : the other side will almost
		 * immediately send a RR that will likely collide with the
		 * UA response (depending on negotiated turnaround).
		 * 2) The other end has not received the first UA response,
		 * will stay at 9600 and will never see the second UA response.
		 * Jean II */
		irlap_send_ua_response_frame(self, &self->qos_rx);
#endif

		/*
		 *  The WD-timer could be set to the duration of the P-timer
		 *  for this case, but it is recommended to use twice the
		 *  value (note 3 IrLAP p. 60).
		 */
		irlap_start_wd_timer(self, self->wd_timeout);
		irlap_next_state(self, LAP_NRM_S);

		break;
	case RECV_DISCOVERY_XID_CMD:
		IRDA_DEBUG(3, "%s(), event RECV_DISCOVER_XID_CMD!\n",
			   __func__);
		irlap_next_state(self, LAP_NDM);

		break;
	case DISCONNECT_REQUEST:
		IRDA_DEBUG(0, "%s(), Disconnect request!\n", __func__);
		irlap_send_dm_frame(self);
		irlap_next_state( self, LAP_NDM);
		irlap_disconnect_indication(self, LAP_DISC_INDICATION);
		break;
	default:
		IRDA_DEBUG(1, "%s(), Unknown event %d, %s\n", __func__,
			   event, irlap_event[event]);

		ret = -1;
		break;
	}

	return ret;
}

/*
 * Function irlap_state_setup (event, skb, frame)
 *
 *    SETUP state, The local layer has transmitted a SNRM command frame to
 *    a remote peer layer and is awaiting a reply .
 *
 */
static int irlap_state_setup(struct irlap_cb *self, IRLAP_EVENT event,
			     struct sk_buff *skb, struct irlap_info *info)
{
	int ret = 0;

	IRDA_DEBUG(4, "%s()\n", __func__);

	IRDA_ASSERT(self != NULL, return -1;);
	IRDA_ASSERT(self->magic == LAP_MAGIC, return -1;);

	switch (event) {
	case FINAL_TIMER_EXPIRED:
		if (self->retry_count < self->N3) {
/*
 *  Perform random backoff, Wait a random number of time units, minimum
 *  duration half the time taken to transmitt a SNRM frame, maximum duration
 *  1.5 times the time taken to transmit a SNRM frame. So this time should
 *  between 15 msecs and 45 msecs.
 */
			irlap_start_backoff_timer(self, msecs_to_jiffies(20 +
							(jiffies % 30)));
		} else {
			/* Always switch state before calling upper layers */
			irlap_next_state(self, LAP_NDM);

			irlap_disconnect_indication(self, LAP_FOUND_NONE);
		}
		break;
	case BACKOFF_TIMER_EXPIRED:
		irlap_send_snrm_frame(self, &self->qos_rx);
		irlap_start_final_timer(self, self->final_timeout);
		self->retry_count++;
		break;
	case RECV_SNRM_CMD:
		IRDA_DEBUG(4, "%s(), SNRM battle!\n", __func__);

		IRDA_ASSERT(skb != NULL, return 0;);
		IRDA_ASSERT(info != NULL, return 0;);

		/*
		 *  The device with the largest device address wins the battle
		 *  (both have sent a SNRM command!)
		 */
		if (info &&(info->daddr > self->saddr)) {
			del_timer(&self->final_timer);
			irlap_initiate_connection_state(self);

			IRDA_ASSERT(self->netdev != NULL, return -1;);

			skb_pull(skb, sizeof(struct snrm_frame));

			irlap_qos_negotiate(self, skb);

			/* Send UA frame and then change link settings */
			irlap_apply_connection_parameters(self, FALSE);
			irlap_send_ua_response_frame(self, &self->qos_rx);

			irlap_next_state(self, LAP_NRM_S);
			irlap_connect_confirm(self, skb);

			/*
			 *  The WD-timer could be set to the duration of the
			 *  P-timer for this case, but it is recommended
			 *  to use twice the value (note 3 IrLAP p. 60).
			 */
			irlap_start_wd_timer(self, self->wd_timeout);
		} else {
			/* We just ignore the other device! */
			irlap_next_state(self, LAP_SETUP);
		}
		break;
	case RECV_UA_RSP:
		/* Stop F-timer */
		del_timer(&self->final_timer);

		/* Initiate connection state */
		irlap_initiate_connection_state(self);

		/* Negotiate connection parameters */
		IRDA_ASSERT(skb->len > 10, return -1;);

		skb_pull(skb, sizeof(struct ua_frame));

		IRDA_ASSERT(self->netdev != NULL, return -1;);

		irlap_qos_negotiate(self, skb);

		/* Set the new link setting *now* (before the rr frame) */
		irlap_apply_connection_parameters(self, TRUE);
		self->retry_count = 0;

		/* Wait for turnaround time to give a chance to the other
		 * device to be ready to receive us.
		 * Note : the time to switch speed is typically larger
		 * than the turnaround time, but as we don't have the other
		 * side speed switch time, that's our best guess...
		 * Jean II */
		irlap_wait_min_turn_around(self, &self->qos_tx);

		/* This frame will actually be sent at the new speed */
		irlap_send_rr_frame(self, CMD_FRAME);

		/* The timer is set to half the normal timer to quickly
		 * detect a failure to negociate the new connection
		 * parameters. IrLAP 6.11.3.2, note 3.
		 * Note that currently we don't process this failure
		 * properly, as we should do a quick disconnect.
		 * Jean II */
		irlap_start_final_timer(self, self->final_timeout/2);
		irlap_next_state(self, LAP_NRM_P);

		irlap_connect_confirm(self, skb);
		break;
	case RECV_DM_RSP:     /* FALLTHROUGH */
	case RECV_DISC_CMD:
		del_timer(&self->final_timer);
		irlap_next_state(self, LAP_NDM);

		irlap_disconnect_indication(self, LAP_DISC_INDICATION);
		break;
	default:
		IRDA_DEBUG(1, "%s(), Unknown event %d, %s\n", __func__,
			   event, irlap_event[event]);

		ret = -1;
		break;
	}
	return ret;
}

/*
 * Function irlap_state_offline (self, event, skb, info)
 *
 *    OFFLINE state, not used for now!
 *
 */
static int irlap_state_offline(struct irlap_cb *self, IRLAP_EVENT event,
			       struct sk_buff *skb, struct irlap_info *info)
{
	IRDA_DEBUG( 0, "%s(), Unknown event\n", __func__);

	return -1;
}

/*
 * Function irlap_state_xmit_p (self, event, skb, info)
 *
 *    XMIT, Only the primary station has right to transmit, and we
 *    therefore do not expect to receive any transmissions from other
 *    stations.
 *
 */
static int irlap_state_xmit_p(struct irlap_cb *self, IRLAP_EVENT event,
			      struct sk_buff *skb, struct irlap_info *info)
{
	int ret = 0;

	switch (event) {
	case SEND_I_CMD:
		/*
		 *  Only send frame if send-window > 0.
		 */
		if ((self->window > 0) && (!self->remote_busy)) {
			int nextfit;
#ifdef CONFIG_IRDA_DYNAMIC_WINDOW
			struct sk_buff *skb_next;

			/* With DYNAMIC_WINDOW, we keep the window size
			 * maximum, and adapt on the packets we are sending.
			 * At 115k, we can send only 2 packets of 2048 bytes
			 * in a 500 ms turnaround. Without this option, we
			 * would always limit the window to 2. With this
			 * option, if we send smaller packets, we can send
			 * up to 7 of them (always depending on QoS).
			 * Jean II */

			/* Look at the next skb. This is safe, as we are
			 * the only consumer of the Tx queue (if we are not,
			 * we have other problems) - Jean II */
			skb_next = skb_peek(&self->txq);

			/* Check if a subsequent skb exist and would fit in
			 * the current window (with respect to turnaround
			 * time).
			 * This allow us to properly mark the current packet
			 * with the pf bit, to avoid falling back on the
			 * second test below, and avoid waiting the
			 * end of the window and sending a extra RR.
			 * Note : (skb_next != NULL) <=> (skb_queue_len() > 0)
			 * Jean II */
			nextfit = ((skb_next != NULL) &&
				   ((skb_next->len + skb->len) <=
				    self->bytes_left));

			/*
			 * The current packet may not fit ! Because of test
			 * above, this should not happen any more !!!
			 *  Test if we have transmitted more bytes over the
			 *  link than its possible to do with the current
			 *  speed and turn-around-time.
			 */
			if((!nextfit) && (skb->len > self->bytes_left)) {
				IRDA_DEBUG(0, "%s(), Not allowed to transmit"
					   " more bytes!\n", __func__);
				/* Requeue the skb */
				skb_queue_head(&self->txq, skb_get(skb));
				/*
				 *  We should switch state to LAP_NRM_P, but
				 *  that is not possible since we must be sure
				 *  that we poll the other side. Since we have
				 *  used up our time, the poll timer should
				 *  trigger anyway now, so we just wait for it
				 *  DB
				 */
				/*
				 * Sorry, but that's not totally true. If
				 * we send 2000B packets, we may wait another
				 * 1000B until our turnaround expire. That's
				 * why we need to be proactive in avoiding
				 * coming here. - Jean II
				 */
				return -EPROTO;
			}

			/* Substract space used by this skb */
			self->bytes_left -= skb->len;
#else	/* CONFIG_IRDA_DYNAMIC_WINDOW */
			/* Window has been adjusted for the max packet
			 * size, so much simpler... - Jean II */
			nextfit = !skb_queue_empty(&self->txq);
#endif	/* CONFIG_IRDA_DYNAMIC_WINDOW */
			/*
			 *  Send data with poll bit cleared only if window > 1
			 *  and there is more frames after this one to be sent
			 */
			if ((self->window > 1) && (nextfit)) {
				/* More packet to send in current window */
				irlap_send_data_primary(self, skb);
				irlap_next_state(self, LAP_XMIT_P);
			} else {
				/* Final packet of window */
				irlap_send_data_primary_poll(self, skb);

				/*
				 * Make sure state machine does not try to send
				 * any more frames
				 */
				ret = -EPROTO;
			}
#ifdef CONFIG_IRDA_FAST_RR
			/* Peer may want to reply immediately */
			self->fast_RR = FALSE;
#endif /* CONFIG_IRDA_FAST_RR */
		} else {
			IRDA_DEBUG(4, "%s(), Unable to send! remote busy?\n",
				   __func__);
			skb_queue_head(&self->txq, skb_get(skb));

			/*
			 *  The next ret is important, because it tells
			 *  irlap_next_state _not_ to deliver more frames
			 */
			ret = -EPROTO;
		}
		break;
	case POLL_TIMER_EXPIRED:
		IRDA_DEBUG(3, "%s(), POLL_TIMER_EXPIRED <%ld>\n",
			    __func__, jiffies);
		irlap_send_rr_frame(self, CMD_FRAME);
		/* Return to NRM properly - Jean II  */
		self->window = self->window_size;
#ifdef CONFIG_IRDA_DYNAMIC_WINDOW
		/* Allowed to transmit a maximum number of bytes again. */
		self->bytes_left = self->line_capacity;
#endif /* CONFIG_IRDA_DYNAMIC_WINDOW */
		irlap_start_final_timer(self, self->final_timeout);
		irlap_next_state(self, LAP_NRM_P);
		break;
	case DISCONNECT_REQUEST:
		del_timer(&self->poll_timer);
		irlap_wait_min_turn_around(self, &self->qos_tx);
		irlap_send_disc_frame(self);
		irlap_flush_all_queues(self);
		irlap_start_final_timer(self, self->final_timeout);
		self->retry_count = 0;
		irlap_next_state(self, LAP_PCLOSE);
		break;
	case DATA_REQUEST:
		/* Nothing to do, irlap_do_event() will send the packet
		 * when we return... - Jean II */
		break;
	default:
		IRDA_DEBUG(0, "%s(), Unknown event %s\n",
			   __func__, irlap_event[event]);

		ret = -EINVAL;
		break;
	}
	return ret;
}

/*
 * Function irlap_state_pclose (event, skb, info)
 *
 *    PCLOSE state
 */
static int irlap_state_pclose(struct irlap_cb *self, IRLAP_EVENT event,
			      struct sk_buff *skb, struct irlap_info *info)
{
	int ret = 0;

	IRDA_DEBUG(1, "%s()\n", __func__);

	IRDA_ASSERT(self != NULL, return -1;);
	IRDA_ASSERT(self->magic == LAP_MAGIC, return -1;);

	switch (event) {
	case RECV_UA_RSP: /* FALLTHROUGH */
	case RECV_DM_RSP:
		del_timer(&self->final_timer);

		/* Set new link parameters */
		irlap_apply_default_connection_parameters(self);

		/* Always switch state before calling upper layers */
		irlap_next_state(self, LAP_NDM);

		irlap_disconnect_indication(self, LAP_DISC_INDICATION);
		break;
	case FINAL_TIMER_EXPIRED:
		if (self->retry_count < self->N3) {
			irlap_wait_min_turn_around(self, &self->qos_tx);
			irlap_send_disc_frame(self);
			irlap_start_final_timer(self, self->final_timeout);
			self->retry_count++;
			/* Keep state */
		} else {
			irlap_apply_default_connection_parameters(self);

			/*  Always switch state before calling upper layers */
			irlap_next_state(self, LAP_NDM);

			irlap_disconnect_indication(self, LAP_NO_RESPONSE);
		}
		break;
	default:
		IRDA_DEBUG(1, "%s(), Unknown event %d\n", __func__, event);

		ret = -1;
		break;
	}
	return ret;
}

/*
 * Function irlap_state_nrm_p (self, event, skb, info)
 *
 *   NRM_P (Normal Response Mode as Primary), The primary station has given
 *   permissions to a secondary station to transmit IrLAP resonse frames
 *   (by sending a frame with the P bit set). The primary station will not
 *   transmit any frames and is expecting to receive frames only from the
 *   secondary to which transmission permissions has been given.
 */
static int irlap_state_nrm_p(struct irlap_cb *self, IRLAP_EVENT event,
			     struct sk_buff *skb, struct irlap_info *info)
{
	int ret = 0;
	int ns_status;
	int nr_status;

	switch (event) {
	case RECV_I_RSP: /* Optimize for the common case */
		if (unlikely(skb->len <= LAP_ADDR_HEADER + LAP_CTRL_HEADER)) {
			/*
			 * Input validation check: a stir4200/mcp2150
			 * combination sometimes results in an empty i:rsp.
			 * This makes no sense; we can just ignore the frame
			 * and send an rr:cmd immediately. This happens before
			 * changing nr or ns so triggers a retransmit
			 */
			irlap_wait_min_turn_around(self, &self->qos_tx);
			irlap_send_rr_frame(self, CMD_FRAME);
			/* Keep state */
			break;
		}
		/* FIXME: must check for remote_busy below */
#ifdef CONFIG_IRDA_FAST_RR
		/*
		 *  Reset the fast_RR so we can use the fast RR code with
		 *  full speed the next time since peer may have more frames
		 *  to transmitt
		 */
		self->fast_RR = FALSE;
#endif /* CONFIG_IRDA_FAST_RR */
		IRDA_ASSERT( info != NULL, return -1;);

		ns_status = irlap_validate_ns_received(self, info->ns);
		nr_status = irlap_validate_nr_received(self, info->nr);

		/*
		 *  Check for expected I(nformation) frame
		 */
		if ((ns_status == NS_EXPECTED) && (nr_status == NR_EXPECTED)) {

			/* Update Vr (next frame for us to receive) */
			self->vr = (self->vr + 1) % 8;

			/* Update Nr received, cleanup our retry queue */
			irlap_update_nr_received(self, info->nr);

			/*
			 *  Got expected NR, so reset the
			 *  retry_count. This is not done by IrLAP spec,
			 *  which is strange!
			 */
			self->retry_count = 0;
			self->ack_required = TRUE;

			/*  poll bit cleared?  */
			if (!info->pf) {
				/* Keep state, do not move this line */
				irlap_next_state(self, LAP_NRM_P);

				irlap_data_indication(self, skb, FALSE);
			} else {
				/* No longer waiting for pf */
				del_timer(&self->final_timer);

				irlap_wait_min_turn_around(self, &self->qos_tx);

				/* Call higher layer *before* changing state
				 * to give them a chance to send data in the
				 * next LAP frame.
				 * Jean II */
				irlap_data_indication(self, skb, FALSE);

				/* XMIT states are the most dangerous state
				 * to be in, because user requests are
				 * processed directly and may change state.
				 * On the other hand, in NDM_P, those
				 * requests are queued and we will process
				 * them when we return to irlap_do_event().
				 * Jean II
				 */
				irlap_next_state(self, LAP_XMIT_P);

				/* This is the last frame.
				 * Make sure it's always called in XMIT state.
				 * - Jean II */
				irlap_start_poll_timer(self, self->poll_timeout);
			}
			break;

		}
		/* Unexpected next to send (Ns) */
		if ((ns_status == NS_UNEXPECTED) && (nr_status == NR_EXPECTED))
		{
			if (!info->pf) {
				irlap_update_nr_received(self, info->nr);

				/*
				 *  Wait until the last frame before doing
				 *  anything
				 */

				/* Keep state */
				irlap_next_state(self, LAP_NRM_P);
			} else {
				IRDA_DEBUG(4,
				       "%s(), missing or duplicate frame!\n",
					   __func__);

				/* Update Nr received */
				irlap_update_nr_received(self, info->nr);

				irlap_wait_min_turn_around(self, &self->qos_tx);
				irlap_send_rr_frame(self, CMD_FRAME);

				self->ack_required = FALSE;

				irlap_start_final_timer(self, self->final_timeout);
				irlap_next_state(self, LAP_NRM_P);
			}
			break;
		}
		/*
		 *  Unexpected next to receive (Nr)
		 */
		if ((ns_status == NS_EXPECTED) && (nr_status == NR_UNEXPECTED))
		{
			if (info->pf) {
				self->vr = (self->vr + 1) % 8;

				/* Update Nr received */
				irlap_update_nr_received(self, info->nr);

				/* Resend rejected frames */
				irlap_resend_rejected_frames(self, CMD_FRAME);

				self->ack_required = FALSE;

				/* Make sure we account for the time
				 * to transmit our frames. See comemnts
				 * in irlap_send_data_primary_poll().
				 * Jean II */
				irlap_start_final_timer(self, 2 * self->final_timeout);

				/* Keep state, do not move this line */
				irlap_next_state(self, LAP_NRM_P);

				irlap_data_indication(self, skb, FALSE);
			} else {
				/*
				 *  Do not resend frames until the last
				 *  frame has arrived from the other
				 *  device. This is not documented in
				 *  IrLAP!!
				 */
				self->vr = (self->vr + 1) % 8;

				/* Update Nr received */
				irlap_update_nr_received(self, info->nr);

				self->ack_required = FALSE;

				/* Keep state, do not move this line!*/
				irlap_next_state(self, LAP_NRM_P);

				irlap_data_indication(self, skb, FALSE);
			}
			break;
		}
		/*
		 *  Unexpected next to send (Ns) and next to receive (Nr)
		 *  Not documented by IrLAP!
		 */
		if ((ns_status == NS_UNEXPECTED) &&
		    (nr_status == NR_UNEXPECTED))
		{
			IRDA_DEBUG(4, "%s(), unexpected nr and ns!\n",
				   __func__);
			if (info->pf) {
				/* Resend rejected frames */
				irlap_resend_rejected_frames(self, CMD_FRAME);

				/* Give peer some time to retransmit!
				 * But account for our own Tx. */
				irlap_start_final_timer(self, 2 * self->final_timeout);

				/* Keep state, do not move this line */
				irlap_next_state(self, LAP_NRM_P);
			} else {
				/* Update Nr received */
				/* irlap_update_nr_received( info->nr); */

				self->ack_required = FALSE;
			}
			break;
		}

		/*
		 *  Invalid NR or NS
		 */
		if ((nr_status == NR_INVALID) || (ns_status == NS_INVALID)) {
			if (info->pf) {
				del_timer(&self->final_timer);

				irlap_next_state(self, LAP_RESET_WAIT);

				irlap_disconnect_indication(self, LAP_RESET_INDICATION);
				self->xmitflag = TRUE;
			} else {
				del_timer(&self->final_timer);

				irlap_disconnect_indication(self, LAP_RESET_INDICATION);

				self->xmitflag = FALSE;
			}
			break;
		}
		IRDA_DEBUG(1, "%s(), Not implemented!\n", __func__);
		IRDA_DEBUG(1, "%s(), event=%s, ns_status=%d, nr_status=%d\n",
		       __func__, irlap_event[event], ns_status, nr_status);
		break;
	case RECV_UI_FRAME:
		/* Poll bit cleared? */
		if (!info->pf) {
			irlap_data_indication(self, skb, TRUE);
			irlap_next_state(self, LAP_NRM_P);
		} else {
			del_timer(&self->final_timer);
			irlap_data_indication(self, skb, TRUE);
			irlap_next_state(self, LAP_XMIT_P);
			IRDA_DEBUG(1, "%s: RECV_UI_FRAME: next state %s\n", __func__, irlap_state[self->state]);
			irlap_start_poll_timer(self, self->poll_timeout);
		}
		break;
	case RECV_RR_RSP:
		/*
		 *  If you get a RR, the remote isn't busy anymore,
		 *  no matter what the NR
		 */
		self->remote_busy = FALSE;

		/* Stop final timer */
		del_timer(&self->final_timer);

		/*
		 *  Nr as expected?
		 */
		ret = irlap_validate_nr_received(self, info->nr);
		if (ret == NR_EXPECTED) {
			/* Update Nr received */
			irlap_update_nr_received(self, info->nr);

			/*
			 *  Got expected NR, so reset the retry_count. This
			 *  is not done by the IrLAP standard , which is
			 *  strange! DB.
			 */
			self->retry_count = 0;
			irlap_wait_min_turn_around(self, &self->qos_tx);

			irlap_next_state(self, LAP_XMIT_P);

			/* Start poll timer */
			irlap_start_poll_timer(self, self->poll_timeout);
		} else if (ret == NR_UNEXPECTED) {
			IRDA_ASSERT(info != NULL, return -1;);
			/*
			 *  Unexpected nr!
			 */

			/* Update Nr received */
			irlap_update_nr_received(self, info->nr);

			IRDA_DEBUG(4, "RECV_RR_FRAME: Retrans:%d, nr=%d, va=%d, "
			      "vs=%d, vr=%d\n",
			      self->retry_count, info->nr, self->va,
			      self->vs, self->vr);

			/* Resend rejected frames */
			irlap_resend_rejected_frames(self, CMD_FRAME);
			irlap_start_final_timer(self, self->final_timeout * 2);

			irlap_next_state(self, LAP_NRM_P);
		} else if (ret == NR_INVALID) {
			IRDA_DEBUG(1, "%s(), Received RR with "
				   "invalid nr !\n", __func__);

			irlap_next_state(self, LAP_RESET_WAIT);

			irlap_disconnect_indication(self, LAP_RESET_INDICATION);
			self->xmitflag = TRUE;
		}
		break;
	case RECV_RNR_RSP:
		IRDA_ASSERT(info != NULL, return -1;);

		/* Stop final timer */
		del_timer(&self->final_timer);
		self->remote_busy = TRUE;

		/* Update Nr received */
		irlap_update_nr_received(self, info->nr);
		irlap_next_state(self, LAP_XMIT_P);

		/* Start poll timer */
		irlap_start_poll_timer(self, self->poll_timeout);
		break;
	case RECV_FRMR_RSP:
		del_timer(&self->final_timer);
		self->xmitflag = TRUE;
		irlap_next_state(self, LAP_RESET_WAIT);
		irlap_reset_indication(self);
		break;
	case FINAL_TIMER_EXPIRED:
		/*
		 *  We are allowed to wait for additional 300 ms if
		 *  final timer expires when we are in the middle
		 *  of receiving a frame (page 45, IrLAP). Check that
		 *  we only do this once for each frame.
		 */
		if (irda_device_is_receiving(self->netdev) && !self->add_wait) {
			IRDA_DEBUG(1, "FINAL_TIMER_EXPIRED when receiving a "
			      "frame! Waiting a little bit more!\n");
			irlap_start_final_timer(self, msecs_to_jiffies(300));

			/*
			 *  Don't allow this to happen one more time in a row,
			 *  or else we can get a pretty tight loop here if
			 *  if we only receive half a frame. DB.
			 */
			self->add_wait = TRUE;
			break;
		}
		self->add_wait = FALSE;

		/* N2 is the disconnect timer. Until we reach it, we retry */
		if (self->retry_count < self->N2) {
			if (skb_peek(&self->wx_list) == NULL) {
				/* Retry sending the pf bit to the secondary */
				IRDA_DEBUG(4, "nrm_p: resending rr");
				irlap_wait_min_turn_around(self, &self->qos_tx);
				irlap_send_rr_frame(self, CMD_FRAME);
			} else {
				IRDA_DEBUG(4, "nrm_p: resend frames");
				irlap_resend_rejected_frames(self, CMD_FRAME);
			}

			irlap_start_final_timer(self, self->final_timeout);
			self->retry_count++;
			IRDA_DEBUG(4, "irlap_state_nrm_p: FINAL_TIMER_EXPIRED:"
				   " retry_count=%d\n", self->retry_count);

			/* Early warning event. I'm using a pretty liberal
			 * interpretation of the spec and generate an event
			 * every time the timer is multiple of N1 (and not
			 * only the first time). This allow application
			 * to know precisely if connectivity restart...
			 * Jean II */
			if((self->retry_count % self->N1) == 0)
				irlap_status_indication(self,
							STATUS_NO_ACTIVITY);

			/* Keep state */
		} else {
			irlap_apply_default_connection_parameters(self);

			/* Always switch state before calling upper layers */
			irlap_next_state(self, LAP_NDM);
			irlap_disconnect_indication(self, LAP_NO_RESPONSE);
		}
		break;
	case RECV_REJ_RSP:
		irlap_update_nr_received(self, info->nr);
		if (self->remote_busy) {
			irlap_wait_min_turn_around(self, &self->qos_tx);
			irlap_send_rr_frame(self, CMD_FRAME);
		} else
			irlap_resend_rejected_frames(self, CMD_FRAME);
		irlap_start_final_timer(self, 2 * self->final_timeout);
		break;
	case RECV_SREJ_RSP:
		irlap_update_nr_received(self, info->nr);
		if (self->remote_busy) {
			irlap_wait_min_turn_around(self, &self->qos_tx);
			irlap_send_rr_frame(self, CMD_FRAME);
		} else
			irlap_resend_rejected_frame(self, CMD_FRAME);
		irlap_start_final_timer(self, 2 * self->final_timeout);
		break;
	case RECV_RD_RSP:
		IRDA_DEBUG(1, "%s(), RECV_RD_RSP\n", __func__);

		irlap_flush_all_queues(self);
		irlap_next_state(self, LAP_XMIT_P);
		/* Call back the LAP state machine to do a proper disconnect */
		irlap_disconnect_request(self);
		break;
	default:
		IRDA_DEBUG(1, "%s(), Unknown event %s\n",
			    __func__, irlap_event[event]);

		ret = -1;
		break;
	}
	return ret;
}

/*
 * Function irlap_state_reset_wait (event, skb, info)
 *
 *    We have informed the service user of a reset condition, and is
 *    awaiting reset of disconnect request.
 *
 */
static int irlap_state_reset_wait(struct irlap_cb *self, IRLAP_EVENT event,
				  struct sk_buff *skb, struct irlap_info *info)
{
	int ret = 0;

	IRDA_DEBUG(3, "%s(), event = %s\n", __func__, irlap_event[event]);

	IRDA_ASSERT(self != NULL, return -1;);
	IRDA_ASSERT(self->magic == LAP_MAGIC, return -1;);

	switch (event) {
	case RESET_REQUEST:
		if (self->xmitflag) {
			irlap_wait_min_turn_around(self, &self->qos_tx);
			irlap_send_snrm_frame(self, NULL);
			irlap_start_final_timer(self, self->final_timeout);
			irlap_next_state(self, LAP_RESET);
		} else {
			irlap_start_final_timer(self, self->final_timeout);
			irlap_next_state(self, LAP_RESET);
		}
		break;
	case DISCONNECT_REQUEST:
		irlap_wait_min_turn_around( self, &self->qos_tx);
		irlap_send_disc_frame( self);
		irlap_flush_all_queues( self);
		irlap_start_final_timer( self, self->final_timeout);
		self->retry_count = 0;
		irlap_next_state( self, LAP_PCLOSE);
		break;
	default:
		IRDA_DEBUG(2, "%s(), Unknown event %s\n", __func__,
			   irlap_event[event]);

		ret = -1;
		break;
	}
	return ret;
}

/*
 * Function irlap_state_reset (self, event, skb, info)
 *
 *    We have sent a SNRM reset command to the peer layer, and is awaiting
 *    reply.
 *
 */
static int irlap_state_reset(struct irlap_cb *self, IRLAP_EVENT event,
			     struct sk_buff *skb, struct irlap_info *info)
{
	int ret = 0;

	IRDA_DEBUG(3, "%s(), event = %s\n", __func__, irlap_event[event]);

	IRDA_ASSERT(self != NULL, return -1;);
	IRDA_ASSERT(self->magic == LAP_MAGIC, return -1;);

	switch (event) {
	case RECV_DISC_CMD:
		del_timer(&self->final_timer);

		irlap_apply_default_connection_parameters(self);

		/* Always switch state before calling upper layers */
		irlap_next_state(self, LAP_NDM);

		irlap_disconnect_indication(self, LAP_NO_RESPONSE);

		break;
	case RECV_UA_RSP:
		del_timer(&self->final_timer);

		/* Initiate connection state */
		irlap_initiate_connection_state(self);

		irlap_reset_confirm();

		self->remote_busy = FALSE;

		irlap_next_state(self, LAP_XMIT_P);

		irlap_start_poll_timer(self, self->poll_timeout);

		break;
	case FINAL_TIMER_EXPIRED:
		if (self->retry_count < 3) {
			irlap_wait_min_turn_around(self, &self->qos_tx);

			IRDA_ASSERT(self->netdev != NULL, return -1;);
			irlap_send_snrm_frame(self, self->qos_dev);

			self->retry_count++; /* Experimental!! */

			irlap_start_final_timer(self, self->final_timeout);
			irlap_next_state(self, LAP_RESET);
		} else if (self->retry_count >= self->N3) {
			irlap_apply_default_connection_parameters(self);

			/* Always switch state before calling upper layers */
			irlap_next_state(self, LAP_NDM);

			irlap_disconnect_indication(self, LAP_NO_RESPONSE);
		}
		break;
	case RECV_SNRM_CMD:
		/*
		 * SNRM frame is not allowed to contain an I-field in this
		 * state
		 */
		if (!info) {
			IRDA_DEBUG(3, "%s(), RECV_SNRM_CMD\n", __func__);
			irlap_initiate_connection_state(self);
			irlap_wait_min_turn_around(self, &self->qos_tx);
			irlap_send_ua_response_frame(self, &self->qos_rx);
			irlap_reset_confirm();
			irlap_start_wd_timer(self, self->wd_timeout);
			irlap_next_state(self, LAP_NDM);
		} else {
			IRDA_DEBUG(0,
				   "%s(), SNRM frame contained an I field!\n",
				   __func__);
		}
		break;
	default:
		IRDA_DEBUG(1, "%s(), Unknown event %s\n",
			   __func__, irlap_event[event]);

		ret = -1;
		break;
	}
	return ret;
}

/*
 * Function irlap_state_xmit_s (event, skb, info)
 *
 *   XMIT_S, The secondary station has been given the right to transmit,
 *   and we therefor do not expect to receive any transmissions from other
 *   stations.
 */
static int irlap_state_xmit_s(struct irlap_cb *self, IRLAP_EVENT event,
			      struct sk_buff *skb, struct irlap_info *info)
{
	int ret = 0;

	IRDA_DEBUG(4, "%s(), event=%s\n", __func__, irlap_event[event]);

	IRDA_ASSERT(self != NULL, return -ENODEV;);
	IRDA_ASSERT(self->magic == LAP_MAGIC, return -EBADR;);

	switch (event) {
	case SEND_I_CMD:
		/*
		 *  Send frame only if send window > 0
		 */
		if ((self->window > 0) && (!self->remote_busy)) {
			int nextfit;
#ifdef CONFIG_IRDA_DYNAMIC_WINDOW
			struct sk_buff *skb_next;

			/*
			 * Same deal as in irlap_state_xmit_p(), so see
			 * the comments at that point.
			 * We are the secondary, so there are only subtle
			 * differences. - Jean II
			 */

			/* Check if a subsequent skb exist and would fit in
			 * the current window (with respect to turnaround
			 * time). - Jean II */
			skb_next = skb_peek(&self->txq);
			nextfit = ((skb_next != NULL) &&
				   ((skb_next->len + skb->len) <=
				    self->bytes_left));

			/*
			 *  Test if we have transmitted more bytes over the
			 *  link than its possible to do with the current
			 *  speed and turn-around-time.
			 */
			if((!nextfit) && (skb->len > self->bytes_left)) {
				IRDA_DEBUG(0, "%s(), Not allowed to transmit"
					   " more bytes!\n", __func__);
				/* Requeue the skb */
				skb_queue_head(&self->txq, skb_get(skb));

				/*
				 *  Switch to NRM_S, this is only possible
				 *  when we are in secondary mode, since we
				 *  must be sure that we don't miss any RR
				 *  frames
				 */
				self->window = self->window_size;
				self->bytes_left = self->line_capacity;
				irlap_start_wd_timer(self, self->wd_timeout);

				irlap_next_state(self, LAP_NRM_S);
				/* Slight difference with primary :
				 * here we would wait for the other side to
				 * expire the turnaround. - Jean II */

				return -EPROTO; /* Try again later */
			}
			/* Substract space used by this skb */
			self->bytes_left -= skb->len;
#else	/* CONFIG_IRDA_DYNAMIC_WINDOW */
			/* Window has been adjusted for the max packet
			 * size, so much simpler... - Jean II */
			nextfit = !skb_queue_empty(&self->txq);
#endif /* CONFIG_IRDA_DYNAMIC_WINDOW */
			/*
			 *  Send data with final bit cleared only if window > 1
			 *  and there is more frames to be sent
			 */
			if ((self->window > 1) && (nextfit)) {
				irlap_send_data_secondary(self, skb);
				irlap_next_state(self, LAP_XMIT_S);
			} else {
				irlap_send_data_secondary_final(self, skb);
				irlap_next_state(self, LAP_NRM_S);

				/*
				 * Make sure state machine does not try to send
				 * any more frames
				 */
				ret = -EPROTO;
			}
		} else {
			IRDA_DEBUG(2, "%s(), Unable to send!\n", __func__);
			skb_queue_head(&self->txq, skb_get(skb));
			ret = -EPROTO;
		}
		break;
	case DISCONNECT_REQUEST:
		irlap_send_rd_frame(self);
		irlap_flush_all_queues(self);
		irlap_start_wd_timer(self, self->wd_timeout);
		irlap_next_state(self, LAP_SCLOSE);
		break;
	case DATA_REQUEST:
		/* Nothing to do, irlap_do_event() will send the packet
		 * when we return... - Jean II */
		break;
	default:
		IRDA_DEBUG(2, "%s(), Unknown event %s\n", __func__,
			   irlap_event[event]);

		ret = -EINVAL;
		break;
	}
	return ret;
}

/*
 * Function irlap_state_nrm_s (event, skb, info)
 *
 *    NRM_S (Normal Response Mode as Secondary) state, in this state we are
 *    expecting to receive frames from the primary station
 *
 */
static int irlap_state_nrm_s(struct irlap_cb *self, IRLAP_EVENT event,
			     struct sk_buff *skb, struct irlap_info *info)
{
	int ns_status;
	int nr_status;
	int ret = 0;

	IRDA_DEBUG(4, "%s(), event=%s\n", __func__, irlap_event[ event]);

	IRDA_ASSERT(self != NULL, return -1;);
	IRDA_ASSERT(self->magic == LAP_MAGIC, return -1;);

	switch (event) {
	case RECV_I_CMD: /* Optimize for the common case */
		/* FIXME: must check for remote_busy below */
		IRDA_DEBUG(4, "%s(), event=%s nr=%d, vs=%d, ns=%d, "
			   "vr=%d, pf=%d\n", __func__,
			   irlap_event[event], info->nr,
			   self->vs, info->ns, self->vr, info->pf);

		self->retry_count = 0;

		ns_status = irlap_validate_ns_received(self, info->ns);
		nr_status = irlap_validate_nr_received(self, info->nr);
		/*
		 *  Check for expected I(nformation) frame
		 */
		if ((ns_status == NS_EXPECTED) && (nr_status == NR_EXPECTED)) {

			/* Update Vr (next frame for us to receive) */
			self->vr = (self->vr + 1) % 8;

			/* Update Nr received */
			irlap_update_nr_received(self, info->nr);

			/*
			 *  poll bit cleared?
			 */
			if (!info->pf) {

				self->ack_required = TRUE;

				/*
				 *  Starting WD-timer here is optional, but
				 *  not recommended. Note 6 IrLAP p. 83
				 */
#if 0
				irda_start_timer(WD_TIMER, self->wd_timeout);
#endif
				/* Keep state, do not move this line */
				irlap_next_state(self, LAP_NRM_S);

				irlap_data_indication(self, skb, FALSE);
				break;
			} else {
				/*
				 *  We should wait before sending RR, and
				 *  also before changing to XMIT_S
				 *  state. (note 1, IrLAP p. 82)
				 */
				irlap_wait_min_turn_around(self, &self->qos_tx);

				/*
				 * Give higher layers a chance to
				 * immediately reply with some data before
				 * we decide if we should send a RR frame
				 * or not
				 */
				irlap_data_indication(self, skb, FALSE);

				/* Any pending data requests?  */
				if (!skb_queue_empty(&self->txq) &&
				    (self->window > 0))
				{
					self->ack_required = TRUE;

					del_timer(&self->wd_timer);

					irlap_next_state(self, LAP_XMIT_S);
				} else {
					irlap_send_rr_frame(self, RSP_FRAME);
					irlap_start_wd_timer(self,
							     self->wd_timeout);

					/* Keep the state */
					irlap_next_state(self, LAP_NRM_S);
				}
				break;
			}
		}
		/*
		 *  Check for Unexpected next to send (Ns)
		 */
		if ((ns_status == NS_UNEXPECTED) && (nr_status == NR_EXPECTED))
		{
			/* Unexpected next to send, with final bit cleared */
			if (!info->pf) {
				irlap_update_nr_received(self, info->nr);

				irlap_start_wd_timer(self, self->wd_timeout);
			} else {
				/* Update Nr received */
				irlap_update_nr_received(self, info->nr);

				irlap_wait_min_turn_around(self, &self->qos_tx);
				irlap_send_rr_frame(self, RSP_FRAME);

				irlap_start_wd_timer(self, self->wd_timeout);
			}
			break;
		}

		/*
		 *  Unexpected Next to Receive(NR) ?
		 */
		if ((ns_status == NS_EXPECTED) && (nr_status == NR_UNEXPECTED))
		{
			if (info->pf) {
				IRDA_DEBUG(4, "RECV_I_RSP: frame(s) lost\n");

				self->vr = (self->vr + 1) % 8;

				/* Update Nr received */
				irlap_update_nr_received(self, info->nr);

				/* Resend rejected frames */
				irlap_resend_rejected_frames(self, RSP_FRAME);

				/* Keep state, do not move this line */
				irlap_next_state(self, LAP_NRM_S);

				irlap_data_indication(self, skb, FALSE);
				irlap_start_wd_timer(self, self->wd_timeout);
				break;
			}
			/*
			 *  This is not documented in IrLAP!! Unexpected NR
			 *  with poll bit cleared
			 */
			if (!info->pf) {
				self->vr = (self->vr + 1) % 8;

				/* Update Nr received */
				irlap_update_nr_received(self, info->nr);

				/* Keep state, do not move this line */
				irlap_next_state(self, LAP_NRM_S);

				irlap_data_indication(self, skb, FALSE);
				irlap_start_wd_timer(self, self->wd_timeout);
			}
			break;
		}

		if (ret == NR_INVALID) {
			IRDA_DEBUG(0, "NRM_S, NR_INVALID not implemented!\n");
		}
		if (ret == NS_INVALID) {
			IRDA_DEBUG(0, "NRM_S, NS_INVALID not implemented!\n");
		}
		break;
	case RECV_UI_FRAME:
		/*
		 *  poll bit cleared?
		 */
		if (!info->pf) {
			irlap_data_indication(self, skb, TRUE);
			irlap_next_state(self, LAP_NRM_S); /* Keep state */
		} else {
			/*
			 *  Any pending data requests?
			 */
			if (!skb_queue_empty(&self->txq) &&
			    (self->window > 0) && !self->remote_busy)
			{
				irlap_data_indication(self, skb, TRUE);

				del_timer(&self->wd_timer);

				irlap_next_state(self, LAP_XMIT_S);
			} else {
				irlap_data_indication(self, skb, TRUE);

				irlap_wait_min_turn_around(self, &self->qos_tx);

				irlap_send_rr_frame(self, RSP_FRAME);
				self->ack_required = FALSE;

				irlap_start_wd_timer(self, self->wd_timeout);

				/* Keep the state */
				irlap_next_state(self, LAP_NRM_S);
			}
		}
		break;
	case RECV_RR_CMD:
		self->retry_count = 0;

		/*
		 *  Nr as expected?
		 */
		nr_status = irlap_validate_nr_received(self, info->nr);
		if (nr_status == NR_EXPECTED) {
			if (!skb_queue_empty(&self->txq) &&
			    (self->window > 0)) {
				self->remote_busy = FALSE;

				/* Update Nr received */
				irlap_update_nr_received(self, info->nr);
				del_timer(&self->wd_timer);

				irlap_wait_min_turn_around(self, &self->qos_tx);
				irlap_next_state(self, LAP_XMIT_S);
			} else {
				self->remote_busy = FALSE;
				/* Update Nr received */
				irlap_update_nr_received(self, info->nr);
				irlap_wait_min_turn_around(self, &self->qos_tx);
				irlap_start_wd_timer(self, self->wd_timeout);

				/* Note : if the link is idle (this case),
				 * we never go in XMIT_S, so we never get a
				 * chance to process any DISCONNECT_REQUEST.
				 * Do it now ! - Jean II */
				if (self->disconnect_pending) {
					/* Disconnect */
					irlap_send_rd_frame(self);
					irlap_flush_all_queues(self);

					irlap_next_state(self, LAP_SCLOSE);
				} else {
					/* Just send back pf bit */
					irlap_send_rr_frame(self, RSP_FRAME);

					irlap_next_state(self, LAP_NRM_S);
				}
			}
		} else if (nr_status == NR_UNEXPECTED) {
			self->remote_busy = FALSE;
			irlap_update_nr_received(self, info->nr);
			irlap_resend_rejected_frames(self, RSP_FRAME);

			irlap_start_wd_timer(self, self->wd_timeout);

			/* Keep state */
			irlap_next_state(self, LAP_NRM_S);
		} else {
			IRDA_DEBUG(1, "%s(), invalid nr not implemented!\n",
				   __func__);
		}
		break;
	case RECV_SNRM_CMD:
		/* SNRM frame is not allowed to contain an I-field */
		if (!info) {
			del_timer(&self->wd_timer);
			IRDA_DEBUG(1, "%s(), received SNRM cmd\n", __func__);
			irlap_next_state(self, LAP_RESET_CHECK);

			irlap_reset_indication(self);
		} else {
			IRDA_DEBUG(0,
				   "%s(), SNRM frame contained an I-field!\n",
				   __func__);

		}
		break;
	case RECV_REJ_CMD:
		irlap_update_nr_received(self, info->nr);
		if (self->remote_busy) {
			irlap_wait_min_turn_around(self, &self->qos_tx);
			irlap_send_rr_frame(self, RSP_FRAME);
		} else
			irlap_resend_rejected_frames(self, RSP_FRAME);
		irlap_start_wd_timer(self, self->wd_timeout);
		break;
	case RECV_SREJ_CMD:
		irlap_update_nr_received(self, info->nr);
		if (self->remote_busy) {
			irlap_wait_min_turn_around(self, &self->qos_tx);
			irlap_send_rr_frame(self, RSP_FRAME);
		} else
			irlap_resend_rejected_frame(self, RSP_FRAME);
		irlap_start_wd_timer(self, self->wd_timeout);
		break;
	case WD_TIMER_EXPIRED:
		/*
		 *  Wait until retry_count * n matches negotiated threshold/
		 *  disconnect time (note 2 in IrLAP p. 82)
		 *
		 * Similar to irlap_state_nrm_p() -> FINAL_TIMER_EXPIRED
		 * Note : self->wd_timeout = (self->final_timeout * 2),
		 *   which explain why we use (self->N2 / 2) here !!!
		 * Jean II
		 */
		IRDA_DEBUG(1, "%s(), retry_count = %d\n", __func__,
			   self->retry_count);

		if (self->retry_count < (self->N2 / 2)) {
			/* No retry, just wait for primary */
			irlap_start_wd_timer(self, self->wd_timeout);
			self->retry_count++;

			if((self->retry_count % (self->N1 / 2)) == 0)
				irlap_status_indication(self,
							STATUS_NO_ACTIVITY);
		} else {
			irlap_apply_default_connection_parameters(self);

			/* Always switch state before calling upper layers */
			irlap_next_state(self, LAP_NDM);
			irlap_disconnect_indication(self, LAP_NO_RESPONSE);
		}
		break;
	case RECV_DISC_CMD:
		/* Always switch state before calling upper layers */
		irlap_next_state(self, LAP_NDM);

		/* Send disconnect response */
		irlap_wait_min_turn_around(self, &self->qos_tx);
		irlap_send_ua_response_frame(self, NULL);

		del_timer(&self->wd_timer);
		irlap_flush_all_queues(self);
		/* Set default link parameters */
		irlap_apply_default_connection_parameters(self);

		irlap_disconnect_indication(self, LAP_DISC_INDICATION);
		break;
	case RECV_DISCOVERY_XID_CMD:
		irlap_wait_min_turn_around(self, &self->qos_tx);
		irlap_send_rr_frame(self, RSP_FRAME);
		self->ack_required = TRUE;
		irlap_start_wd_timer(self, self->wd_timeout);
		irlap_next_state(self, LAP_NRM_S);

		break;
	case RECV_TEST_CMD:
		/* Remove test frame header (only LAP header in NRM) */
		skb_pull(skb, LAP_ADDR_HEADER + LAP_CTRL_HEADER);

		irlap_wait_min_turn_around(self, &self->qos_tx);
		irlap_start_wd_timer(self, self->wd_timeout);

		/* Send response (info will be copied) */
		irlap_send_test_frame(self, self->caddr, info->daddr, skb);
		break;
	default:
		IRDA_DEBUG(1, "%s(), Unknown event %d, (%s)\n", __func__,
			   event, irlap_event[event]);

		ret = -EINVAL;
		break;
	}
	return ret;
}

/*
 * Function irlap_state_sclose (self, event, skb, info)
 */
static int irlap_state_sclose(struct irlap_cb *self, IRLAP_EVENT event,
			      struct sk_buff *skb, struct irlap_info *info)
{
	int ret = 0;

	IRDA_DEBUG(1, "%s()\n", __func__);

	IRDA_ASSERT(self != NULL, return -ENODEV;);
	IRDA_ASSERT(self->magic == LAP_MAGIC, return -EBADR;);

	switch (event) {
	case RECV_DISC_CMD:
		/* Always switch state before calling upper layers */
		irlap_next_state(self, LAP_NDM);

		/* Send disconnect response */
		irlap_wait_min_turn_around(self, &self->qos_tx);
		irlap_send_ua_response_frame(self, NULL);

		del_timer(&self->wd_timer);
		/* Set default link parameters */
		irlap_apply_default_connection_parameters(self);

		irlap_disconnect_indication(self, LAP_DISC_INDICATION);
		break;
	case RECV_DM_RSP:
		/* IrLAP-1.1 p.82: in SCLOSE, S and I type RSP frames
		 * shall take us down into default NDM state, like DM_RSP
		 */
	case RECV_RR_RSP:
	case RECV_RNR_RSP:
	case RECV_REJ_RSP:
	case RECV_SREJ_RSP:
	case RECV_I_RSP:
		/* Always switch state before calling upper layers */
		irlap_next_state(self, LAP_NDM);

		del_timer(&self->wd_timer);
		irlap_apply_default_connection_parameters(self);

		irlap_disconnect_indication(self, LAP_DISC_INDICATION);
		break;
	case WD_TIMER_EXPIRED:
		/* Always switch state before calling upper layers */
		irlap_next_state(self, LAP_NDM);

		irlap_apply_default_connection_parameters(self);

		irlap_disconnect_indication(self, LAP_DISC_INDICATION);
		break;
	default:
		/* IrLAP-1.1 p.82: in SCLOSE, basically any received frame
		 * with pf=1 shall restart the wd-timer and resend the rd:rsp
		 */
		if (info != NULL  &&  info->pf) {
			del_timer(&self->wd_timer);
			irlap_wait_min_turn_around(self, &self->qos_tx);
			irlap_send_rd_frame(self);
			irlap_start_wd_timer(self, self->wd_timeout);
			break;		/* stay in SCLOSE */
		}

		IRDA_DEBUG(1, "%s(), Unknown event %d, (%s)\n", __func__,
			   event, irlap_event[event]);

		ret = -EINVAL;
		break;
	}

	return -1;
}

static int irlap_state_reset_check( struct irlap_cb *self, IRLAP_EVENT event,
				   struct sk_buff *skb,
				   struct irlap_info *info)
{
	int ret = 0;

	IRDA_DEBUG(1, "%s(), event=%s\n", __func__, irlap_event[event]);

	IRDA_ASSERT(self != NULL, return -ENODEV;);
	IRDA_ASSERT(self->magic == LAP_MAGIC, return -EBADR;);

	switch (event) {
	case RESET_RESPONSE:
		irlap_send_ua_response_frame(self, &self->qos_rx);
		irlap_initiate_connection_state(self);
		irlap_start_wd_timer(self, WD_TIMEOUT);
		irlap_flush_all_queues(self);

		irlap_next_state(self, LAP_NRM_S);
		break;
	case DISCONNECT_REQUEST:
		irlap_wait_min_turn_around(self, &self->qos_tx);
		irlap_send_rd_frame(self);
		irlap_start_wd_timer(self, WD_TIMEOUT);
		irlap_next_state(self, LAP_SCLOSE);
		break;
	default:
		IRDA_DEBUG(1, "%s(), Unknown event %d, (%s)\n", __func__,
			   event, irlap_event[event]);

		ret = -EINVAL;
		break;
	}
	return ret;
}
