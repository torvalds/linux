/*********************************************************************
 *
 * Filename:      irlmp_event.c
 * Version:       0.8
 * Description:   An IrDA LMP event driver for Linux
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Mon Aug  4 20:40:53 1997
 * Modified at:   Tue Dec 14 23:04:16 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 *
 *     Copyright (c) 1998-1999 Dag Brattli <dagb@cs.uit.no>,
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

#include <linux/config.h>
#include <linux/kernel.h>

#include <net/irda/irda.h>
#include <net/irda/timer.h>
#include <net/irda/irlap.h>
#include <net/irda/irlmp.h>
#include <net/irda/irlmp_frame.h>
#include <net/irda/irlmp_event.h>

const char *irlmp_state[] = {
	"LAP_STANDBY",
	"LAP_U_CONNECT",
	"LAP_ACTIVE",
};

const char *irlsap_state[] = {
	"LSAP_DISCONNECTED",
	"LSAP_CONNECT",
	"LSAP_CONNECT_PEND",
	"LSAP_DATA_TRANSFER_READY",
	"LSAP_SETUP",
	"LSAP_SETUP_PEND",
};

#ifdef CONFIG_IRDA_DEBUG
static const char *irlmp_event[] = {
	"LM_CONNECT_REQUEST",
	"LM_CONNECT_CONFIRM",
	"LM_CONNECT_RESPONSE",
	"LM_CONNECT_INDICATION",

	"LM_DISCONNECT_INDICATION",
	"LM_DISCONNECT_REQUEST",

	"LM_DATA_REQUEST",
	"LM_UDATA_REQUEST",
	"LM_DATA_INDICATION",
	"LM_UDATA_INDICATION",

	"LM_WATCHDOG_TIMEOUT",

	/* IrLAP events */
	"LM_LAP_CONNECT_REQUEST",
	"LM_LAP_CONNECT_INDICATION",
	"LM_LAP_CONNECT_CONFIRM",
	"LM_LAP_DISCONNECT_INDICATION",
	"LM_LAP_DISCONNECT_REQUEST",
	"LM_LAP_DISCOVERY_REQUEST",
	"LM_LAP_DISCOVERY_CONFIRM",
	"LM_LAP_IDLE_TIMEOUT",
};
#endif	/* CONFIG_IRDA_DEBUG */

/* LAP Connection control proto declarations */
static void irlmp_state_standby  (struct lap_cb *, IRLMP_EVENT,
				  struct sk_buff *);
static void irlmp_state_u_connect(struct lap_cb *, IRLMP_EVENT,
				  struct sk_buff *);
static void irlmp_state_active   (struct lap_cb *, IRLMP_EVENT,
				  struct sk_buff *);

/* LSAP Connection control proto declarations */
static int irlmp_state_disconnected(struct lsap_cb *, IRLMP_EVENT,
				    struct sk_buff *);
static int irlmp_state_connect     (struct lsap_cb *, IRLMP_EVENT,
				    struct sk_buff *);
static int irlmp_state_connect_pend(struct lsap_cb *, IRLMP_EVENT,
				    struct sk_buff *);
static int irlmp_state_dtr         (struct lsap_cb *, IRLMP_EVENT,
				    struct sk_buff *);
static int irlmp_state_setup       (struct lsap_cb *, IRLMP_EVENT,
				    struct sk_buff *);
static int irlmp_state_setup_pend  (struct lsap_cb *, IRLMP_EVENT,
				    struct sk_buff *);

static void (*lap_state[]) (struct lap_cb *, IRLMP_EVENT, struct sk_buff *) =
{
	irlmp_state_standby,
	irlmp_state_u_connect,
	irlmp_state_active,
};

static int (*lsap_state[])( struct lsap_cb *, IRLMP_EVENT, struct sk_buff *) =
{
	irlmp_state_disconnected,
	irlmp_state_connect,
	irlmp_state_connect_pend,
	irlmp_state_dtr,
	irlmp_state_setup,
	irlmp_state_setup_pend
};

static inline void irlmp_next_lap_state(struct lap_cb *self,
					IRLMP_STATE state)
{
	/*
	IRDA_DEBUG(4, "%s(), LMP LAP = %s\n", __FUNCTION__, irlmp_state[state]);
	*/
	self->lap_state = state;
}

static inline void irlmp_next_lsap_state(struct lsap_cb *self,
					 LSAP_STATE state)
{
	/*
	IRDA_ASSERT(self != NULL, return;);
	IRDA_DEBUG(4, "%s(), LMP LSAP = %s\n", __FUNCTION__, irlsap_state[state]);
	*/
	self->lsap_state = state;
}

/* Do connection control events */
int irlmp_do_lsap_event(struct lsap_cb *self, IRLMP_EVENT event,
			struct sk_buff *skb)
{
	IRDA_ASSERT(self != NULL, return -1;);
	IRDA_ASSERT(self->magic == LMP_LSAP_MAGIC, return -1;);

	IRDA_DEBUG(4, "%s(), EVENT = %s, STATE = %s\n",
		__FUNCTION__, irlmp_event[event], irlsap_state[ self->lsap_state]);

	return (*lsap_state[self->lsap_state]) (self, event, skb);
}

/*
 * Function do_lap_event (event, skb, info)
 *
 *    Do IrLAP control events
 *
 */
void irlmp_do_lap_event(struct lap_cb *self, IRLMP_EVENT event,
			struct sk_buff *skb)
{
	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == LMP_LAP_MAGIC, return;);

	IRDA_DEBUG(4, "%s(), EVENT = %s, STATE = %s\n", __FUNCTION__,
		   irlmp_event[event],
		   irlmp_state[self->lap_state]);

	(*lap_state[self->lap_state]) (self, event, skb);
}

void irlmp_discovery_timer_expired(void *data)
{
	IRDA_DEBUG(4, "%s()\n", __FUNCTION__);

	/* We always cleanup the log (active & passive discovery) */
	irlmp_do_expiry();

	/* Active discovery is conditional */
	if (sysctl_discovery)
		irlmp_do_discovery(sysctl_discovery_slots);

	/* Restart timer */
	irlmp_start_discovery_timer(irlmp, sysctl_discovery_timeout * HZ);
}

void irlmp_watchdog_timer_expired(void *data)
{
	struct lsap_cb *self = (struct lsap_cb *) data;

	IRDA_DEBUG(2, "%s()\n", __FUNCTION__);

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == LMP_LSAP_MAGIC, return;);

	irlmp_do_lsap_event(self, LM_WATCHDOG_TIMEOUT, NULL);
}

void irlmp_idle_timer_expired(void *data)
{
	struct lap_cb *self = (struct lap_cb *) data;

	IRDA_DEBUG(2, "%s()\n", __FUNCTION__);

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == LMP_LAP_MAGIC, return;);

	irlmp_do_lap_event(self, LM_LAP_IDLE_TIMEOUT, NULL);
}

/*
 * Send an event on all LSAPs attached to this LAP.
 */
static inline void
irlmp_do_all_lsap_event(hashbin_t *	lsap_hashbin,
			IRLMP_EVENT	event)
{
	struct lsap_cb *lsap;
	struct lsap_cb *lsap_next;

	/* Note : this function use the new hashbin_find_next()
	 * function, instead of the old hashbin_get_next().
	 * This make sure that we are always pointing one lsap
	 * ahead, so that if the current lsap is removed as the
	 * result of sending the event, we don't care.
	 * Also, as we store the context ourselves, if an enumeration
	 * of the same lsap hashbin happens as the result of sending the
	 * event, we don't care.
	 * The only problem is if the next lsap is removed. In that case,
	 * hashbin_find_next() will return NULL and we will abort the
	 * enumeration. - Jean II */

	/* Also : we don't accept any skb in input. We can *NOT* pass
	 * the same skb to multiple clients safely, we would need to
	 * skb_clone() it. - Jean II */

	lsap = (struct lsap_cb *) hashbin_get_first(lsap_hashbin);

	while (NULL != hashbin_find_next(lsap_hashbin,
					 (long) lsap,
					 NULL,
					 (void *) &lsap_next) ) {
		irlmp_do_lsap_event(lsap, event, NULL);
		lsap = lsap_next;
	}
}

/*********************************************************************
 *
 *    LAP connection control states
 *
 ********************************************************************/

/*
 * Function irlmp_state_standby (event, skb, info)
 *
 *    STANDBY, The IrLAP connection does not exist.
 *
 */
static void irlmp_state_standby(struct lap_cb *self, IRLMP_EVENT event,
				struct sk_buff *skb)
{
	IRDA_DEBUG(4, "%s()\n", __FUNCTION__);
	IRDA_ASSERT(self->irlap != NULL, return;);

	switch (event) {
	case LM_LAP_DISCOVERY_REQUEST:
		/* irlmp_next_station_state( LMP_DISCOVER); */

		irlap_discovery_request(self->irlap, &irlmp->discovery_cmd);
		break;
	case LM_LAP_CONNECT_INDICATION:
		/*  It's important to switch state first, to avoid IrLMP to
		 *  think that the link is free since IrLMP may then start
		 *  discovery before the connection is properly set up. DB.
		 */
		irlmp_next_lap_state(self, LAP_ACTIVE);

		/* Just accept connection TODO, this should be fixed */
		irlap_connect_response(self->irlap, skb);
		break;
	case LM_LAP_CONNECT_REQUEST:
		IRDA_DEBUG(4, "%s() LS_CONNECT_REQUEST\n", __FUNCTION__);

		irlmp_next_lap_state(self, LAP_U_CONNECT);

		/* FIXME: need to set users requested QoS */
		irlap_connect_request(self->irlap, self->daddr, NULL, 0);
		break;
	case LM_LAP_DISCONNECT_INDICATION:
		IRDA_DEBUG(4, "%s(), Error LM_LAP_DISCONNECT_INDICATION\n",
			   __FUNCTION__);

		irlmp_next_lap_state(self, LAP_STANDBY);
		break;
	default:
		IRDA_DEBUG(0, "%s(), Unknown event %s\n",
			   __FUNCTION__, irlmp_event[event]);
		break;
	}
}

/*
 * Function irlmp_state_u_connect (event, skb, info)
 *
 *    U_CONNECT, The layer above has tried to open an LSAP connection but
 *    since the IrLAP connection does not exist, we must first start an
 *    IrLAP connection. We are now waiting response from IrLAP.
 * */
static void irlmp_state_u_connect(struct lap_cb *self, IRLMP_EVENT event,
				  struct sk_buff *skb)
{
	IRDA_DEBUG(2, "%s(), event=%s\n", __FUNCTION__, irlmp_event[event]);

	switch (event) {
	case LM_LAP_CONNECT_INDICATION:
		/*  It's important to switch state first, to avoid IrLMP to
		 *  think that the link is free since IrLMP may then start
		 *  discovery before the connection is properly set up. DB.
		 */
		irlmp_next_lap_state(self, LAP_ACTIVE);

		/* Just accept connection TODO, this should be fixed */
		irlap_connect_response(self->irlap, skb);

		/* Tell LSAPs that they can start sending data */
		irlmp_do_all_lsap_event(self->lsaps, LM_LAP_CONNECT_CONFIRM);

		/* Note : by the time we get there (LAP retries and co),
		 * the lsaps may already have gone. This avoid getting stuck
		 * forever in LAP_ACTIVE state - Jean II */
		if (HASHBIN_GET_SIZE(self->lsaps) == 0) {
			IRDA_DEBUG(0, "%s() NO LSAPs !\n",  __FUNCTION__);
			irlmp_start_idle_timer(self, LM_IDLE_TIMEOUT);
		}
		break;
	case LM_LAP_CONNECT_REQUEST:
		/* Already trying to connect */
		break;
	case LM_LAP_CONNECT_CONFIRM:
		/* For all lsap_ce E Associated do LS_Connect_confirm */
		irlmp_next_lap_state(self, LAP_ACTIVE);

		/* Tell LSAPs that they can start sending data */
		irlmp_do_all_lsap_event(self->lsaps, LM_LAP_CONNECT_CONFIRM);

		/* Note : by the time we get there (LAP retries and co),
		 * the lsaps may already have gone. This avoid getting stuck
		 * forever in LAP_ACTIVE state - Jean II */
		if (HASHBIN_GET_SIZE(self->lsaps) == 0) {
			IRDA_DEBUG(0, "%s() NO LSAPs !\n",  __FUNCTION__);
			irlmp_start_idle_timer(self, LM_IDLE_TIMEOUT);
		}
		break;
	case LM_LAP_DISCONNECT_INDICATION:
		IRDA_DEBUG(4, "%s(), LM_LAP_DISCONNECT_INDICATION\n",  __FUNCTION__);
		irlmp_next_lap_state(self, LAP_STANDBY);

		/* Send disconnect event to all LSAPs using this link */
		irlmp_do_all_lsap_event(self->lsaps,
					LM_LAP_DISCONNECT_INDICATION);
		break;
	case LM_LAP_DISCONNECT_REQUEST:
		IRDA_DEBUG(4, "%s(), LM_LAP_DISCONNECT_REQUEST\n",  __FUNCTION__);

		/* One of the LSAP did timeout or was closed, if it was
		 * the last one, try to get out of here - Jean II */
		if (HASHBIN_GET_SIZE(self->lsaps) <= 1) {
			irlap_disconnect_request(self->irlap);
		}
		break;
	default:
		IRDA_DEBUG(0, "%s(), Unknown event %s\n",
			 __FUNCTION__, irlmp_event[event]);
		break;
	}
}

/*
 * Function irlmp_state_active (event, skb, info)
 *
 *    ACTIVE, IrLAP connection is active
 *
 */
static void irlmp_state_active(struct lap_cb *self, IRLMP_EVENT event,
			       struct sk_buff *skb)
{
	IRDA_DEBUG(4, "%s()\n", __FUNCTION__);

	switch (event) {
	case LM_LAP_CONNECT_REQUEST:
		IRDA_DEBUG(4, "%s(), LS_CONNECT_REQUEST\n", __FUNCTION__);

		/*
		 * IrLAP may have a pending disconnect. We tried to close
		 * IrLAP, but it was postponed because the link was
		 * busy or we were still sending packets. As we now
		 * need it, make sure it stays on. Jean II
		 */
		irlap_clear_disconnect(self->irlap);

		/*
		 *  LAP connection already active, just bounce back! Since we
		 *  don't know which LSAP that tried to do this, we have to
		 *  notify all LSAPs using this LAP, but that should be safe to
		 *  do anyway.
		 */
		irlmp_do_all_lsap_event(self->lsaps, LM_LAP_CONNECT_CONFIRM);

		/* Needed by connect indication */
		irlmp_do_all_lsap_event(irlmp->unconnected_lsaps,
					LM_LAP_CONNECT_CONFIRM);
		/* Keep state */
		break;
	case LM_LAP_DISCONNECT_REQUEST:
		/*
		 *  Need to find out if we should close IrLAP or not. If there
		 *  is only one LSAP connection left on this link, that LSAP
		 *  must be the one that tries to close IrLAP. It will be
		 *  removed later and moved to the list of unconnected LSAPs
		 */
		if (HASHBIN_GET_SIZE(self->lsaps) > 0) {
			/* Timer value is checked in irsysctl - Jean II */
			irlmp_start_idle_timer(self, sysctl_lap_keepalive_time * HZ / 1000);
		} else {
			/* No more connections, so close IrLAP */

			/* We don't want to change state just yet, because
			 * we want to reflect accurately the real state of
			 * the LAP, not the state we wish it was in,
			 * so that we don't lose LM_LAP_CONNECT_REQUEST.
			 * In some cases, IrLAP won't close the LAP
			 * immediately. For example, it might still be
			 * retrying packets or waiting for the pf bit.
			 * As the LAP always send a DISCONNECT_INDICATION
			 * in PCLOSE or SCLOSE, just change state on that.
			 * Jean II */
			irlap_disconnect_request(self->irlap);
		}
		break;
	case LM_LAP_IDLE_TIMEOUT:
		if (HASHBIN_GET_SIZE(self->lsaps) == 0) {
			/* Same reasoning as above - keep state */
			irlap_disconnect_request(self->irlap);
		}
		break;
	case LM_LAP_DISCONNECT_INDICATION:
		irlmp_next_lap_state(self, LAP_STANDBY);

		/* In some case, at this point our side has already closed
		 * all lsaps, and we are waiting for the idle_timer to
		 * expire. If another device reconnect immediately, the
		 * idle timer will expire in the midle of the connection
		 * initialisation, screwing up things a lot...
		 * Therefore, we must stop the timer... */
		irlmp_stop_idle_timer(self);

		/*
		 *  Inform all connected LSAP's using this link
		 */
		irlmp_do_all_lsap_event(self->lsaps,
					LM_LAP_DISCONNECT_INDICATION);

		/* Force an expiry of the discovery log.
		 * Now that the LAP is free, the system may attempt to
		 * connect to another device. Unfortunately, our entries
		 * are stale. There is a small window (<3s) before the
		 * normal discovery will run and where irlmp_connect_request()
		 * can get the wrong info, so make sure things get
		 * cleaned *NOW* ;-) - Jean II */
		irlmp_do_expiry();
		break;
	default:
		IRDA_DEBUG(0, "%s(), Unknown event %s\n",
			 __FUNCTION__, irlmp_event[event]);
		break;
	}
}

/*********************************************************************
 *
 *    LSAP connection control states
 *
 ********************************************************************/

/*
 * Function irlmp_state_disconnected (event, skb, info)
 *
 *    DISCONNECTED
 *
 */
static int irlmp_state_disconnected(struct lsap_cb *self, IRLMP_EVENT event,
				    struct sk_buff *skb)
{
	int ret = 0;

	IRDA_DEBUG(4, "%s()\n", __FUNCTION__);

	IRDA_ASSERT(self != NULL, return -1;);
	IRDA_ASSERT(self->magic == LMP_LSAP_MAGIC, return -1;);

	switch (event) {
#ifdef CONFIG_IRDA_ULTRA
	case LM_UDATA_INDICATION:
		/* This is most bizzare. Those packets are  aka unreliable
		 * connected, aka IrLPT or SOCK_DGRAM/IRDAPROTO_UNITDATA.
		 * Why do we pass them as Ultra ??? Jean II */
		irlmp_connless_data_indication(self, skb);
		break;
#endif /* CONFIG_IRDA_ULTRA */
	case LM_CONNECT_REQUEST:
		IRDA_DEBUG(4, "%s(), LM_CONNECT_REQUEST\n", __FUNCTION__);

		if (self->conn_skb) {
			IRDA_WARNING("%s: busy with another request!\n",
				     __FUNCTION__);
			return -EBUSY;
		}
		/* Don't forget to refcount it (see irlmp_connect_request()) */
		skb_get(skb);
		self->conn_skb = skb;

		irlmp_next_lsap_state(self, LSAP_SETUP_PEND);

		/* Start watchdog timer (5 secs for now) */
		irlmp_start_watchdog_timer(self, 5*HZ);

		irlmp_do_lap_event(self->lap, LM_LAP_CONNECT_REQUEST, NULL);
		break;
	case LM_CONNECT_INDICATION:
		if (self->conn_skb) {
			IRDA_WARNING("%s: busy with another request!\n",
				     __FUNCTION__);
			return -EBUSY;
		}
		/* Don't forget to refcount it (see irlap_driver_rcv()) */
		skb_get(skb);
		self->conn_skb = skb;

		irlmp_next_lsap_state(self, LSAP_CONNECT_PEND);

		/* Start watchdog timer
		 * This is not mentionned in the spec, but there is a rare
		 * race condition that can get the socket stuck.
		 * If we receive this event while our LAP is closing down,
		 * the LM_LAP_CONNECT_REQUEST get lost and we get stuck in
		 * CONNECT_PEND state forever.
		 * The other cause of getting stuck down there is if the
		 * higher layer never reply to the CONNECT_INDICATION.
		 * Anyway, it make sense to make sure that we always have
		 * a backup plan. 1 second is plenty (should be immediate).
		 * Jean II */
		irlmp_start_watchdog_timer(self, 1*HZ);

		irlmp_do_lap_event(self->lap, LM_LAP_CONNECT_REQUEST, NULL);
		break;
	default:
		IRDA_DEBUG(1, "%s(), Unknown event %s on LSAP %#02x\n",
			   __FUNCTION__, irlmp_event[event], self->slsap_sel);
		break;
	}
	return ret;
}

/*
 * Function irlmp_state_connect (self, event, skb)
 *
 *    CONNECT
 *
 */
static int irlmp_state_connect(struct lsap_cb *self, IRLMP_EVENT event,
				struct sk_buff *skb)
{
	struct lsap_cb *lsap;
	int ret = 0;

	IRDA_DEBUG(4, "%s()\n", __FUNCTION__);

	IRDA_ASSERT(self != NULL, return -1;);
	IRDA_ASSERT(self->magic == LMP_LSAP_MAGIC, return -1;);

	switch (event) {
	case LM_CONNECT_RESPONSE:
		/*
		 *  Bind this LSAP to the IrLAP link where the connect was
		 *  received
		 */
		lsap = hashbin_remove(irlmp->unconnected_lsaps, (long) self,
				      NULL);

		IRDA_ASSERT(lsap == self, return -1;);
		IRDA_ASSERT(self->lap != NULL, return -1;);
		IRDA_ASSERT(self->lap->lsaps != NULL, return -1;);

		hashbin_insert(self->lap->lsaps, (irda_queue_t *) self,
			       (long) self, NULL);

		set_bit(0, &self->connected);	/* TRUE */

		irlmp_send_lcf_pdu(self->lap, self->dlsap_sel,
				   self->slsap_sel, CONNECT_CNF, skb);

		del_timer(&self->watchdog_timer);

		irlmp_next_lsap_state(self, LSAP_DATA_TRANSFER_READY);
		break;
	case LM_WATCHDOG_TIMEOUT:
		/* May happen, who knows...
		 * Jean II */
		IRDA_DEBUG(0, "%s() WATCHDOG_TIMEOUT!\n",  __FUNCTION__);

		/* Disconnect, get out... - Jean II */
		self->lap = NULL;
		self->dlsap_sel = LSAP_ANY;
		irlmp_next_lsap_state(self, LSAP_DISCONNECTED);
		break;
	default:
		/* LM_LAP_DISCONNECT_INDICATION : Should never happen, we
		 * are *not* yet bound to the IrLAP link. Jean II */
		IRDA_DEBUG(0, "%s(), Unknown event %s on LSAP %#02x\n", 
			   __FUNCTION__, irlmp_event[event], self->slsap_sel);
		break;
	}
	return ret;
}

/*
 * Function irlmp_state_connect_pend (event, skb, info)
 *
 *    CONNECT_PEND
 *
 */
static int irlmp_state_connect_pend(struct lsap_cb *self, IRLMP_EVENT event,
				    struct sk_buff *skb)
{
	struct sk_buff *tx_skb;
	int ret = 0;

	IRDA_DEBUG(4, "%s()\n", __FUNCTION__);

	IRDA_ASSERT(self != NULL, return -1;);
	IRDA_ASSERT(self->magic == LMP_LSAP_MAGIC, return -1;);

	switch (event) {
	case LM_CONNECT_REQUEST:
		/* Keep state */
		break;
	case LM_CONNECT_RESPONSE:
		IRDA_DEBUG(0, "%s(), LM_CONNECT_RESPONSE, "
			   "no indication issued yet\n",  __FUNCTION__);
		/* Keep state */
		break;
	case LM_DISCONNECT_REQUEST:
		IRDA_DEBUG(0, "%s(), LM_DISCONNECT_REQUEST, "
			   "not yet bound to IrLAP connection\n",  __FUNCTION__);
		/* Keep state */
		break;
	case LM_LAP_CONNECT_CONFIRM:
		IRDA_DEBUG(4, "%s(), LS_CONNECT_CONFIRM\n",  __FUNCTION__);
		irlmp_next_lsap_state(self, LSAP_CONNECT);

		tx_skb = self->conn_skb;
		self->conn_skb = NULL;

		irlmp_connect_indication(self, tx_skb);
		/* Drop reference count - see irlmp_connect_indication(). */
		dev_kfree_skb(tx_skb);
		break;
	case LM_WATCHDOG_TIMEOUT:
		/* Will happen in some rare cases because of a race condition.
		 * Just make sure we don't stay there forever...
		 * Jean II */
		IRDA_DEBUG(0, "%s() WATCHDOG_TIMEOUT!\n",  __FUNCTION__);

		/* Go back to disconnected mode, keep the socket waiting */
		self->lap = NULL;
		self->dlsap_sel = LSAP_ANY;
		if(self->conn_skb)
			dev_kfree_skb(self->conn_skb);
		self->conn_skb = NULL;
		irlmp_next_lsap_state(self, LSAP_DISCONNECTED);
		break;
	default:
		/* LM_LAP_DISCONNECT_INDICATION : Should never happen, we
		 * are *not* yet bound to the IrLAP link. Jean II */
		IRDA_DEBUG(0, "%s(), Unknown event %s on LSAP %#02x\n",
			   __FUNCTION__, irlmp_event[event], self->slsap_sel);
		break;
	}
	return ret;
}

/*
 * Function irlmp_state_dtr (self, event, skb)
 *
 *    DATA_TRANSFER_READY
 *
 */
static int irlmp_state_dtr(struct lsap_cb *self, IRLMP_EVENT event,
			   struct sk_buff *skb)
{
	LM_REASON reason;
	int ret = 0;

	IRDA_DEBUG(4, "%s()\n", __FUNCTION__);

	IRDA_ASSERT(self != NULL, return -1;);
	IRDA_ASSERT(self->magic == LMP_LSAP_MAGIC, return -1;);
	IRDA_ASSERT(self->lap != NULL, return -1;);

	switch (event) {
	case LM_DATA_REQUEST: /* Optimize for the common case */
		irlmp_send_data_pdu(self->lap, self->dlsap_sel,
				    self->slsap_sel, FALSE, skb);
		break;
	case LM_DATA_INDICATION: /* Optimize for the common case */
		irlmp_data_indication(self, skb);
		break;
	case LM_UDATA_REQUEST:
		IRDA_ASSERT(skb != NULL, return -1;);
		irlmp_send_data_pdu(self->lap, self->dlsap_sel,
				    self->slsap_sel, TRUE, skb);
		break;
	case LM_UDATA_INDICATION:
		irlmp_udata_indication(self, skb);
		break;
	case LM_CONNECT_REQUEST:
		IRDA_DEBUG(0, "%s(), LM_CONNECT_REQUEST, "
			   "error, LSAP already connected\n", __FUNCTION__);
		/* Keep state */
		break;
	case LM_CONNECT_RESPONSE:
		IRDA_DEBUG(0, "%s(), LM_CONNECT_RESPONSE, "
			   "error, LSAP already connected\n", __FUNCTION__);
		/* Keep state */
		break;
	case LM_DISCONNECT_REQUEST:
		irlmp_send_lcf_pdu(self->lap, self->dlsap_sel, self->slsap_sel,
				   DISCONNECT, skb);
		irlmp_next_lsap_state(self, LSAP_DISCONNECTED);
		/* Called only from irlmp_disconnect_request(), will
		 * unbind from LAP over there. Jean II */

		/* Try to close the LAP connection if its still there */
		if (self->lap) {
			IRDA_DEBUG(4, "%s(), trying to close IrLAP\n",
				   __FUNCTION__);
			irlmp_do_lap_event(self->lap,
					   LM_LAP_DISCONNECT_REQUEST,
					   NULL);
		}
		break;
	case LM_LAP_DISCONNECT_INDICATION:
		irlmp_next_lsap_state(self, LSAP_DISCONNECTED);

		reason = irlmp_convert_lap_reason(self->lap->reason);

		irlmp_disconnect_indication(self, reason, NULL);
		break;
	case LM_DISCONNECT_INDICATION:
		irlmp_next_lsap_state(self, LSAP_DISCONNECTED);

		IRDA_ASSERT(self->lap != NULL, return -1;);
		IRDA_ASSERT(self->lap->magic == LMP_LAP_MAGIC, return -1;);

		IRDA_ASSERT(skb != NULL, return -1;);
		IRDA_ASSERT(skb->len > 3, return -1;);
		reason = skb->data[3];

		 /* Try to close the LAP connection */
		IRDA_DEBUG(4, "%s(), trying to close IrLAP\n", __FUNCTION__);
		irlmp_do_lap_event(self->lap, LM_LAP_DISCONNECT_REQUEST, NULL);

		irlmp_disconnect_indication(self, reason, skb);
		break;
	default:
		IRDA_DEBUG(0, "%s(), Unknown event %s on LSAP %#02x\n",
			   __FUNCTION__, irlmp_event[event], self->slsap_sel);
		break;
	}
	return ret;
}

/*
 * Function irlmp_state_setup (event, skb, info)
 *
 *    SETUP, Station Control has set up the underlying IrLAP connection.
 *    An LSAP connection request has been transmitted to the peer
 *    LSAP-Connection Control FSM and we are awaiting reply.
 */
static int irlmp_state_setup(struct lsap_cb *self, IRLMP_EVENT event,
			     struct sk_buff *skb)
{
	LM_REASON reason;
	int ret = 0;

	IRDA_ASSERT(self != NULL, return -1;);
	IRDA_ASSERT(self->magic == LMP_LSAP_MAGIC, return -1;);

	IRDA_DEBUG(4, "%s()\n", __FUNCTION__);

	switch (event) {
	case LM_CONNECT_CONFIRM:
		irlmp_next_lsap_state(self, LSAP_DATA_TRANSFER_READY);

		del_timer(&self->watchdog_timer);

		irlmp_connect_confirm(self, skb);
		break;
	case LM_DISCONNECT_INDICATION:
		irlmp_next_lsap_state(self, LSAP_DISCONNECTED);

		IRDA_ASSERT(self->lap != NULL, return -1;);
		IRDA_ASSERT(self->lap->magic == LMP_LAP_MAGIC, return -1;);

		IRDA_ASSERT(skb != NULL, return -1;);
		IRDA_ASSERT(skb->len > 3, return -1;);
		reason = skb->data[3];

		 /* Try to close the LAP connection */
		IRDA_DEBUG(4, "%s(), trying to close IrLAP\n",  __FUNCTION__);
		irlmp_do_lap_event(self->lap, LM_LAP_DISCONNECT_REQUEST, NULL);

		irlmp_disconnect_indication(self, reason, skb);
		break;
	case LM_LAP_DISCONNECT_INDICATION:
		irlmp_next_lsap_state(self, LSAP_DISCONNECTED);

		del_timer(&self->watchdog_timer);

		IRDA_ASSERT(self->lap != NULL, return -1;);
		IRDA_ASSERT(self->lap->magic == LMP_LAP_MAGIC, return -1;);

		reason = irlmp_convert_lap_reason(self->lap->reason);

		irlmp_disconnect_indication(self, reason, skb);
		break;
	case LM_WATCHDOG_TIMEOUT:
		IRDA_DEBUG(0, "%s() WATCHDOG_TIMEOUT!\n", __FUNCTION__);

		IRDA_ASSERT(self->lap != NULL, return -1;);
		irlmp_do_lap_event(self->lap, LM_LAP_DISCONNECT_REQUEST, NULL);
		irlmp_next_lsap_state(self, LSAP_DISCONNECTED);

		irlmp_disconnect_indication(self, LM_CONNECT_FAILURE, NULL);
		break;
	default:
		IRDA_DEBUG(0, "%s(), Unknown event %s on LSAP %#02x\n",
			   __FUNCTION__, irlmp_event[event], self->slsap_sel);
		break;
	}
	return ret;
}

/*
 * Function irlmp_state_setup_pend (event, skb, info)
 *
 *    SETUP_PEND, An LM_CONNECT_REQUEST has been received from the service
 *    user to set up an LSAP connection. A request has been sent to the
 *    LAP FSM to set up the underlying IrLAP connection, and we
 *    are awaiting confirm.
 */
static int irlmp_state_setup_pend(struct lsap_cb *self, IRLMP_EVENT event,
				  struct sk_buff *skb)
{
	struct sk_buff *tx_skb;
	LM_REASON reason;
	int ret = 0;

	IRDA_DEBUG(4, "%s()\n", __FUNCTION__);

	IRDA_ASSERT(self != NULL, return -1;);
	IRDA_ASSERT(irlmp != NULL, return -1;);

	switch (event) {
	case LM_LAP_CONNECT_CONFIRM:
		IRDA_ASSERT(self->conn_skb != NULL, return -1;);

		tx_skb = self->conn_skb;
		self->conn_skb = NULL;

		irlmp_send_lcf_pdu(self->lap, self->dlsap_sel,
				   self->slsap_sel, CONNECT_CMD, tx_skb);
		/* Drop reference count - see irlap_data_request(). */
		dev_kfree_skb(tx_skb);

		irlmp_next_lsap_state(self, LSAP_SETUP);
		break;
	case LM_WATCHDOG_TIMEOUT:
		IRDA_DEBUG(0, "%s() : WATCHDOG_TIMEOUT !\n",  __FUNCTION__);

		IRDA_ASSERT(self->lap != NULL, return -1;);
		irlmp_do_lap_event(self->lap, LM_LAP_DISCONNECT_REQUEST, NULL);
		irlmp_next_lsap_state(self, LSAP_DISCONNECTED);

		irlmp_disconnect_indication(self, LM_CONNECT_FAILURE, NULL);
		break;
	case LM_LAP_DISCONNECT_INDICATION: /* LS_Disconnect.indication */
		del_timer( &self->watchdog_timer);

		irlmp_next_lsap_state(self, LSAP_DISCONNECTED);

		reason = irlmp_convert_lap_reason(self->lap->reason);

		irlmp_disconnect_indication(self, reason, NULL);
		break;
	default:
		IRDA_DEBUG(0, "%s(), Unknown event %s on LSAP %#02x\n",
			   __FUNCTION__, irlmp_event[event], self->slsap_sel);
		break;
	}
	return ret;
}
