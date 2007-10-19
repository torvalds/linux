/*********************************************************************
 *
 * Filename:      iriap_event.c
 * Version:       0.1
 * Description:   IAP Finite State Machine
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Thu Aug 21 00:02:07 1997
 * Modified at:   Wed Mar  1 11:28:34 2000
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 *
 *     Copyright (c) 1997, 1999-2000 Dag Brattli <dagb@cs.uit.no>,
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

#include <net/irda/irda.h>
#include <net/irda/irlmp.h>
#include <net/irda/iriap.h>
#include <net/irda/iriap_event.h>

static void state_s_disconnect   (struct iriap_cb *self, IRIAP_EVENT event,
				  struct sk_buff *skb);
static void state_s_connecting   (struct iriap_cb *self, IRIAP_EVENT event,
				  struct sk_buff *skb);
static void state_s_call         (struct iriap_cb *self, IRIAP_EVENT event,
				  struct sk_buff *skb);

static void state_s_make_call    (struct iriap_cb *self, IRIAP_EVENT event,
				  struct sk_buff *skb);
static void state_s_calling      (struct iriap_cb *self, IRIAP_EVENT event,
				  struct sk_buff *skb);
static void state_s_outstanding  (struct iriap_cb *self, IRIAP_EVENT event,
				  struct sk_buff *skb);
static void state_s_replying     (struct iriap_cb *self, IRIAP_EVENT event,
				  struct sk_buff *skb);
static void state_s_wait_for_call(struct iriap_cb *self, IRIAP_EVENT event,
				  struct sk_buff *skb);
static void state_s_wait_active  (struct iriap_cb *self, IRIAP_EVENT event,
				  struct sk_buff *skb);

static void state_r_disconnect   (struct iriap_cb *self, IRIAP_EVENT event,
				  struct sk_buff *skb);
static void state_r_call         (struct iriap_cb *self, IRIAP_EVENT event,
				  struct sk_buff *skb);
static void state_r_waiting      (struct iriap_cb *self, IRIAP_EVENT event,
				  struct sk_buff *skb);
static void state_r_wait_active  (struct iriap_cb *self, IRIAP_EVENT event,
				  struct sk_buff *skb);
static void state_r_receiving    (struct iriap_cb *self, IRIAP_EVENT event,
				  struct sk_buff *skb);
static void state_r_execute      (struct iriap_cb *self, IRIAP_EVENT event,
				  struct sk_buff *skb);
static void state_r_returning    (struct iriap_cb *self, IRIAP_EVENT event,
				  struct sk_buff *skb);

static void (*iriap_state[])(struct iriap_cb *self, IRIAP_EVENT event,
			     struct sk_buff *skb) = {
	/* Client FSM */
	state_s_disconnect,
	state_s_connecting,
	state_s_call,

	/* S-Call FSM */
	state_s_make_call,
	state_s_calling,
	state_s_outstanding,
	state_s_replying,
	state_s_wait_for_call,
	state_s_wait_active,

	/* Server FSM */
	state_r_disconnect,
	state_r_call,

	/* R-Connect FSM */
	state_r_waiting,
	state_r_wait_active,
	state_r_receiving,
	state_r_execute,
	state_r_returning,
};

void iriap_next_client_state(struct iriap_cb *self, IRIAP_STATE state)
{
	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IAS_MAGIC, return;);

	self->client_state = state;
}

void iriap_next_call_state(struct iriap_cb *self, IRIAP_STATE state)
{
	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IAS_MAGIC, return;);

	self->call_state = state;
}

void iriap_next_server_state(struct iriap_cb *self, IRIAP_STATE state)
{
	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IAS_MAGIC, return;);

	self->server_state = state;
}

void iriap_next_r_connect_state(struct iriap_cb *self, IRIAP_STATE state)
{
	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IAS_MAGIC, return;);

	self->r_connect_state = state;
}

void iriap_do_client_event(struct iriap_cb *self, IRIAP_EVENT event,
			   struct sk_buff *skb)
{
	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IAS_MAGIC, return;);

	(*iriap_state[ self->client_state]) (self, event, skb);
}

void iriap_do_call_event(struct iriap_cb *self, IRIAP_EVENT event,
			 struct sk_buff *skb)
{
	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IAS_MAGIC, return;);

	(*iriap_state[ self->call_state]) (self, event, skb);
}

void iriap_do_server_event(struct iriap_cb *self, IRIAP_EVENT event,
			   struct sk_buff *skb)
{
	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IAS_MAGIC, return;);

	(*iriap_state[ self->server_state]) (self, event, skb);
}

void iriap_do_r_connect_event(struct iriap_cb *self, IRIAP_EVENT event,
			      struct sk_buff *skb)
{
	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IAS_MAGIC, return;);

	(*iriap_state[ self->r_connect_state]) (self, event, skb);
}


/*
 * Function state_s_disconnect (event, skb)
 *
 *    S-Disconnect, The device has no LSAP connection to a particular
 *    remote device.
 */
static void state_s_disconnect(struct iriap_cb *self, IRIAP_EVENT event,
			       struct sk_buff *skb)
{
	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IAS_MAGIC, return;);

	switch (event) {
	case IAP_CALL_REQUEST_GVBC:
		iriap_next_client_state(self, S_CONNECTING);
		IRDA_ASSERT(self->request_skb == NULL, return;);
		/* Don't forget to refcount it -
		 * see iriap_getvaluebyclass_request(). */
		skb_get(skb);
		self->request_skb = skb;
		iriap_connect_request(self);
		break;
	case IAP_LM_DISCONNECT_INDICATION:
		break;
	default:
		IRDA_DEBUG(0, "%s(), Unknown event %d\n", __FUNCTION__, event);
		break;
	}
}

/*
 * Function state_s_connecting (self, event, skb)
 *
 *    S-Connecting
 *
 */
static void state_s_connecting(struct iriap_cb *self, IRIAP_EVENT event,
			       struct sk_buff *skb)
{
	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IAS_MAGIC, return;);

	switch (event) {
	case IAP_LM_CONNECT_CONFIRM:
		/*
		 *  Jump to S-Call FSM
		 */
		iriap_do_call_event(self, IAP_CALL_REQUEST, skb);
		/* iriap_call_request(self, 0,0,0); */
		iriap_next_client_state(self, S_CALL);
		break;
	case IAP_LM_DISCONNECT_INDICATION:
		/* Abort calls */
		iriap_next_call_state(self, S_MAKE_CALL);
		iriap_next_client_state(self, S_DISCONNECT);
		break;
	default:
		IRDA_DEBUG(0, "%s(), Unknown event %d\n", __FUNCTION__, event);
		break;
	}
}

/*
 * Function state_s_call (self, event, skb)
 *
 *    S-Call, The device can process calls to a specific remote
 *    device. Whenever the LSAP connection is disconnected, this state
 *    catches that event and clears up
 */
static void state_s_call(struct iriap_cb *self, IRIAP_EVENT event,
			 struct sk_buff *skb)
{
	IRDA_ASSERT(self != NULL, return;);

	switch (event) {
	case IAP_LM_DISCONNECT_INDICATION:
		/* Abort calls */
		iriap_next_call_state(self, S_MAKE_CALL);
		iriap_next_client_state(self, S_DISCONNECT);
		break;
	default:
		IRDA_DEBUG(0, "state_s_call: Unknown event %d\n", event);
		break;
	}
}

/*
 * Function state_s_make_call (event, skb)
 *
 *    S-Make-Call
 *
 */
static void state_s_make_call(struct iriap_cb *self, IRIAP_EVENT event,
			      struct sk_buff *skb)
{
	struct sk_buff *tx_skb;

	IRDA_ASSERT(self != NULL, return;);

	switch (event) {
	case IAP_CALL_REQUEST:
		/* Already refcounted - see state_s_disconnect() */
		tx_skb = self->request_skb;
		self->request_skb = NULL;

		irlmp_data_request(self->lsap, tx_skb);
		iriap_next_call_state(self, S_OUTSTANDING);
		break;
	default:
		IRDA_DEBUG(0, "%s(), Unknown event %d\n", __FUNCTION__, event);
		break;
	}
}

/*
 * Function state_s_calling (event, skb)
 *
 *    S-Calling
 *
 */
static void state_s_calling(struct iriap_cb *self, IRIAP_EVENT event,
			    struct sk_buff *skb)
{
	IRDA_DEBUG(0, "%s(), Not implemented\n", __FUNCTION__);
}

/*
 * Function state_s_outstanding (event, skb)
 *
 *    S-Outstanding, The device is waiting for a response to a command
 *
 */
static void state_s_outstanding(struct iriap_cb *self, IRIAP_EVENT event,
				struct sk_buff *skb)
{
	IRDA_ASSERT(self != NULL, return;);

	switch (event) {
	case IAP_RECV_F_LST:
		/*iriap_send_ack(self);*/
		/*LM_Idle_request(idle); */

		iriap_next_call_state(self, S_WAIT_FOR_CALL);
		break;
	default:
		IRDA_DEBUG(0, "%s(), Unknown event %d\n", __FUNCTION__, event);
		break;
	}
}

/*
 * Function state_s_replying (event, skb)
 *
 *    S-Replying, The device is collecting a multiple part response
 */
static void state_s_replying(struct iriap_cb *self, IRIAP_EVENT event,
			     struct sk_buff *skb)
{
	IRDA_DEBUG(0, "%s(), Not implemented\n", __FUNCTION__);
}

/*
 * Function state_s_wait_for_call (event, skb)
 *
 *    S-Wait-for-Call
 *
 */
static void state_s_wait_for_call(struct iriap_cb *self, IRIAP_EVENT event,
				  struct sk_buff *skb)
{
	IRDA_DEBUG(0, "%s(), Not implemented\n", __FUNCTION__);
}


/*
 * Function state_s_wait_active (event, skb)
 *
 *    S-Wait-Active
 *
 */
static void state_s_wait_active(struct iriap_cb *self, IRIAP_EVENT event,
				struct sk_buff *skb)
{
	IRDA_DEBUG(0, "%s(), Not implemented\n", __FUNCTION__);
}

/**************************************************************************
 *
 *  Server FSM
 *
 **************************************************************************/

/*
 * Function state_r_disconnect (self, event, skb)
 *
 *    LM-IAS server is disconnected (not processing any requests!)
 *
 */
static void state_r_disconnect(struct iriap_cb *self, IRIAP_EVENT event,
			       struct sk_buff *skb)
{
	struct sk_buff *tx_skb;

	switch (event) {
	case IAP_LM_CONNECT_INDICATION:
		tx_skb = alloc_skb(LMP_MAX_HEADER, GFP_ATOMIC);
		if (tx_skb == NULL) {
			IRDA_WARNING("%s: unable to malloc!\n", __FUNCTION__);
			return;
		}

		/* Reserve space for MUX_CONTROL and LAP header */
		skb_reserve(tx_skb, LMP_MAX_HEADER);

		irlmp_connect_response(self->lsap, tx_skb);
		/*LM_Idle_request(idle); */

		iriap_next_server_state(self, R_CALL);

		/*
		 *  Jump to R-Connect FSM, we skip R-Waiting since we do not
		 *  care about LM_Idle_request()!
		 */
		iriap_next_r_connect_state(self, R_RECEIVING);
		break;
	default:
		IRDA_DEBUG(0, "%s(), unknown event %d\n", __FUNCTION__, event);
		break;
	}
}

/*
 * Function state_r_call (self, event, skb)
 */
static void state_r_call(struct iriap_cb *self, IRIAP_EVENT event,
			 struct sk_buff *skb)
{
	IRDA_DEBUG(4, "%s()\n", __FUNCTION__);

	switch (event) {
	case IAP_LM_DISCONNECT_INDICATION:
		/* Abort call */
		iriap_next_server_state(self, R_DISCONNECT);
		iriap_next_r_connect_state(self, R_WAITING);
		break;
	default:
		IRDA_DEBUG(0, "%s(), unknown event!\n", __FUNCTION__);
		break;
	}
}

/*
 *  R-Connect FSM
 */

/*
 * Function state_r_waiting (self, event, skb)
 */
static void state_r_waiting(struct iriap_cb *self, IRIAP_EVENT event,
			    struct sk_buff *skb)
{
	IRDA_DEBUG(0, "%s(), Not implemented\n", __FUNCTION__);
}

static void state_r_wait_active(struct iriap_cb *self, IRIAP_EVENT event,
				struct sk_buff *skb)
{
	IRDA_DEBUG(0, "%s(), Not implemented\n", __FUNCTION__);
}

/*
 * Function state_r_receiving (self, event, skb)
 *
 *    We are receiving a command
 *
 */
static void state_r_receiving(struct iriap_cb *self, IRIAP_EVENT event,
			      struct sk_buff *skb)
{
	IRDA_DEBUG(4, "%s()\n", __FUNCTION__);

	switch (event) {
	case IAP_RECV_F_LST:
		iriap_next_r_connect_state(self, R_EXECUTE);

		iriap_call_indication(self, skb);
		break;
	default:
		IRDA_DEBUG(0, "%s(), unknown event!\n", __FUNCTION__);
		break;
	}
}

/*
 * Function state_r_execute (self, event, skb)
 *
 *    The server is processing the request
 *
 */
static void state_r_execute(struct iriap_cb *self, IRIAP_EVENT event,
			    struct sk_buff *skb)
{
	IRDA_DEBUG(4, "%s()\n", __FUNCTION__);

	IRDA_ASSERT(skb != NULL, return;);
	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IAS_MAGIC, return;);

	switch (event) {
	case IAP_CALL_RESPONSE:
		/*
		 *  Since we don't implement the Waiting state, we return
		 *  to state Receiving instead, DB.
		 */
		iriap_next_r_connect_state(self, R_RECEIVING);

		/* Don't forget to refcount it - see
		 * iriap_getvaluebyclass_response(). */
		skb_get(skb);

		irlmp_data_request(self->lsap, skb);
		break;
	default:
		IRDA_DEBUG(0, "%s(), unknown event!\n", __FUNCTION__);
		break;
	}
}

static void state_r_returning(struct iriap_cb *self, IRIAP_EVENT event,
			      struct sk_buff *skb)
{
	IRDA_DEBUG(0, "%s(), event=%d\n", __FUNCTION__, event);

	switch (event) {
	case IAP_RECV_F_LST:
		break;
	default:
		break;
	}
}
