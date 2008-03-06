/*********************************************************************
 *
 * Filename:      ircomm_tty_attach.c
 * Version:
 * Description:   Code for attaching the serial driver to IrCOMM
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Sat Jun  5 17:42:00 1999
 * Modified at:   Tue Jan  4 14:20:49 2000
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 *
 *     Copyright (c) 1999-2000 Dag Brattli, All Rights Reserved.
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

#include <linux/init.h>

#include <net/irda/irda.h>
#include <net/irda/irlmp.h>
#include <net/irda/iriap.h>
#include <net/irda/irttp.h>
#include <net/irda/irias_object.h>
#include <net/irda/parameters.h>

#include <net/irda/ircomm_core.h>
#include <net/irda/ircomm_param.h>
#include <net/irda/ircomm_event.h>

#include <net/irda/ircomm_tty.h>
#include <net/irda/ircomm_tty_attach.h>

static void ircomm_tty_ias_register(struct ircomm_tty_cb *self);
static void ircomm_tty_discovery_indication(discinfo_t *discovery,
					    DISCOVERY_MODE mode,
					    void *priv);
static void ircomm_tty_getvalue_confirm(int result, __u16 obj_id,
					struct ias_value *value, void *priv);
static void ircomm_tty_start_watchdog_timer(struct ircomm_tty_cb *self,
					    int timeout);
static void ircomm_tty_watchdog_timer_expired(void *data);

static int ircomm_tty_state_idle(struct ircomm_tty_cb *self,
				 IRCOMM_TTY_EVENT event,
				 struct sk_buff *skb,
				 struct ircomm_tty_info *info);
static int ircomm_tty_state_search(struct ircomm_tty_cb *self,
				   IRCOMM_TTY_EVENT event,
				   struct sk_buff *skb,
				   struct ircomm_tty_info *info);
static int ircomm_tty_state_query_parameters(struct ircomm_tty_cb *self,
					     IRCOMM_TTY_EVENT event,
					     struct sk_buff *skb,
					     struct ircomm_tty_info *info);
static int ircomm_tty_state_query_lsap_sel(struct ircomm_tty_cb *self,
					   IRCOMM_TTY_EVENT event,
					   struct sk_buff *skb,
					   struct ircomm_tty_info *info);
static int ircomm_tty_state_setup(struct ircomm_tty_cb *self,
				  IRCOMM_TTY_EVENT event,
				  struct sk_buff *skb,
				  struct ircomm_tty_info *info);
static int ircomm_tty_state_ready(struct ircomm_tty_cb *self,
				  IRCOMM_TTY_EVENT event,
				  struct sk_buff *skb,
				  struct ircomm_tty_info *info);

char *ircomm_tty_state[] = {
	"IRCOMM_TTY_IDLE",
	"IRCOMM_TTY_SEARCH",
	"IRCOMM_TTY_QUERY_PARAMETERS",
	"IRCOMM_TTY_QUERY_LSAP_SEL",
	"IRCOMM_TTY_SETUP",
	"IRCOMM_TTY_READY",
	"*** ERROR *** ",
};

#ifdef CONFIG_IRDA_DEBUG
static char *ircomm_tty_event[] = {
	"IRCOMM_TTY_ATTACH_CABLE",
	"IRCOMM_TTY_DETACH_CABLE",
	"IRCOMM_TTY_DATA_REQUEST",
	"IRCOMM_TTY_DATA_INDICATION",
	"IRCOMM_TTY_DISCOVERY_REQUEST",
	"IRCOMM_TTY_DISCOVERY_INDICATION",
	"IRCOMM_TTY_CONNECT_CONFIRM",
	"IRCOMM_TTY_CONNECT_INDICATION",
	"IRCOMM_TTY_DISCONNECT_REQUEST",
	"IRCOMM_TTY_DISCONNECT_INDICATION",
	"IRCOMM_TTY_WD_TIMER_EXPIRED",
	"IRCOMM_TTY_GOT_PARAMETERS",
	"IRCOMM_TTY_GOT_LSAPSEL",
	"*** ERROR ****",
};
#endif /* CONFIG_IRDA_DEBUG */

static int (*state[])(struct ircomm_tty_cb *self, IRCOMM_TTY_EVENT event,
		      struct sk_buff *skb, struct ircomm_tty_info *info) =
{
	ircomm_tty_state_idle,
	ircomm_tty_state_search,
	ircomm_tty_state_query_parameters,
	ircomm_tty_state_query_lsap_sel,
	ircomm_tty_state_setup,
	ircomm_tty_state_ready,
};

/*
 * Function ircomm_tty_attach_cable (driver)
 *
 *    Try to attach cable (IrCOMM link). This function will only return
 *    when the link has been connected, or if an error condition occurs.
 *    If success, the return value is the resulting service type.
 */
int ircomm_tty_attach_cable(struct ircomm_tty_cb *self)
{
	IRDA_DEBUG(0, "%s()\n", __func__ );

	IRDA_ASSERT(self != NULL, return -1;);
	IRDA_ASSERT(self->magic == IRCOMM_TTY_MAGIC, return -1;);

	/* Check if somebody has already connected to us */
	if (ircomm_is_connected(self->ircomm)) {
		IRDA_DEBUG(0, "%s(), already connected!\n", __func__ );
		return 0;
	}

	/* Make sure nobody tries to write before the link is up */
	self->tty->hw_stopped = 1;

	ircomm_tty_ias_register(self);

	ircomm_tty_do_event(self, IRCOMM_TTY_ATTACH_CABLE, NULL, NULL);

	return 0;
}

/*
 * Function ircomm_detach_cable (driver)
 *
 *    Detach cable, or cable has been detached by peer
 *
 */
void ircomm_tty_detach_cable(struct ircomm_tty_cb *self)
{
	IRDA_DEBUG(0, "%s()\n", __func__ );

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IRCOMM_TTY_MAGIC, return;);

	del_timer(&self->watchdog_timer);

	/* Remove discovery handler */
	if (self->ckey) {
		irlmp_unregister_client(self->ckey);
		self->ckey = NULL;
	}
	/* Remove IrCOMM hint bits */
	if (self->skey) {
		irlmp_unregister_service(self->skey);
		self->skey = NULL;
	}

	if (self->iriap) {
		iriap_close(self->iriap);
		self->iriap = NULL;
	}

	/* Remove LM-IAS object */
	if (self->obj) {
		irias_delete_object(self->obj);
		self->obj = NULL;
	}

	ircomm_tty_do_event(self, IRCOMM_TTY_DETACH_CABLE, NULL, NULL);

	/* Reset some values */
	self->daddr = self->saddr = 0;
	self->dlsap_sel = self->slsap_sel = 0;

	memset(&self->settings, 0, sizeof(struct ircomm_params));
}

/*
 * Function ircomm_tty_ias_register (self)
 *
 *    Register with LM-IAS depending on which service type we are
 *
 */
static void ircomm_tty_ias_register(struct ircomm_tty_cb *self)
{
	__u8 oct_seq[6];
	__u16 hints;

	IRDA_DEBUG(0, "%s()\n", __func__ );

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IRCOMM_TTY_MAGIC, return;);

	/* Compute hint bits based on service */
	hints = irlmp_service_to_hint(S_COMM);
	if (self->service_type & IRCOMM_3_WIRE_RAW)
		hints |= irlmp_service_to_hint(S_PRINTER);

	/* Advertise IrCOMM hint bit in discovery */
	if (!self->skey)
		self->skey = irlmp_register_service(hints);
	/* Set up a discovery handler */
	if (!self->ckey)
		self->ckey = irlmp_register_client(hints,
						   ircomm_tty_discovery_indication,
						   NULL, (void *) self);

	/* If already done, no need to do it again */
	if (self->obj)
		return;

	if (self->service_type & IRCOMM_3_WIRE_RAW) {
		/* Register IrLPT with LM-IAS */
		self->obj = irias_new_object("IrLPT", IAS_IRLPT_ID);
		irias_add_integer_attrib(self->obj, "IrDA:IrLMP:LsapSel",
					 self->slsap_sel, IAS_KERNEL_ATTR);
	} else {
		/* Register IrCOMM with LM-IAS */
		self->obj = irias_new_object("IrDA:IrCOMM", IAS_IRCOMM_ID);
		irias_add_integer_attrib(self->obj, "IrDA:TinyTP:LsapSel",
					 self->slsap_sel, IAS_KERNEL_ATTR);

		/* Code the parameters into the buffer */
		irda_param_pack(oct_seq, "bbbbbb",
				IRCOMM_SERVICE_TYPE, 1, self->service_type,
				IRCOMM_PORT_TYPE,    1, IRCOMM_SERIAL);

		/* Register parameters with LM-IAS */
		irias_add_octseq_attrib(self->obj, "Parameters", oct_seq, 6,
					IAS_KERNEL_ATTR);
	}
	irias_insert_object(self->obj);
}

/*
 * Function ircomm_tty_ias_unregister (self)
 *
 *    Remove our IAS object and client hook while connected.
 *
 */
static void ircomm_tty_ias_unregister(struct ircomm_tty_cb *self)
{
	/* Remove LM-IAS object now so it is not reused.
	 * IrCOMM deals very poorly with multiple incoming connections.
	 * It should looks a lot more like IrNET, and "dup" a server TSAP
	 * to the application TSAP (based on various rules).
	 * This is a cheap workaround allowing multiple clients to
	 * connect to us. It will not always work.
	 * Each IrCOMM socket has an IAS entry. Incoming connection will
	 * pick the first one found. So, when we are fully connected,
	 * we remove our IAS entries so that the next IAS entry is used.
	 * We do that for *both* client and server, because a server
	 * can also create client instances.
	 * Jean II */
	if (self->obj) {
		irias_delete_object(self->obj);
		self->obj = NULL;
	}

#if 0
	/* Remove discovery handler.
	 * While we are connected, we no longer need to receive
	 * discovery events. This would be the case if there is
	 * multiple IrLAP interfaces. Jean II */
	if (self->ckey) {
		irlmp_unregister_client(self->ckey);
		self->ckey = NULL;
	}
#endif
}

/*
 * Function ircomm_send_initial_parameters (self)
 *
 *    Send initial parameters to the remote IrCOMM device. These parameters
 *    must be sent before any data.
 */
int ircomm_tty_send_initial_parameters(struct ircomm_tty_cb *self)
{
	IRDA_ASSERT(self != NULL, return -1;);
	IRDA_ASSERT(self->magic == IRCOMM_TTY_MAGIC, return -1;);

	if (self->service_type & IRCOMM_3_WIRE_RAW)
		return 0;

	/*
	 * Set default values, but only if the application for some reason
	 * haven't set them already
	 */
	IRDA_DEBUG(2, "%s(), data-rate = %d\n", __func__ ,
		   self->settings.data_rate);
	if (!self->settings.data_rate)
		self->settings.data_rate = 9600;
	IRDA_DEBUG(2, "%s(), data-format = %d\n", __func__ ,
		   self->settings.data_format);
	if (!self->settings.data_format)
		self->settings.data_format = IRCOMM_WSIZE_8;  /* 8N1 */

	IRDA_DEBUG(2, "%s(), flow-control = %d\n", __func__ ,
		   self->settings.flow_control);
	/*self->settings.flow_control = IRCOMM_RTS_CTS_IN|IRCOMM_RTS_CTS_OUT;*/

	/* Do not set delta values for the initial parameters */
	self->settings.dte = IRCOMM_DTR | IRCOMM_RTS;

	/* Only send service type parameter when we are the client */
	if (self->client)
		ircomm_param_request(self, IRCOMM_SERVICE_TYPE, FALSE);
	ircomm_param_request(self, IRCOMM_DATA_RATE, FALSE);
	ircomm_param_request(self, IRCOMM_DATA_FORMAT, FALSE);

	/* For a 3 wire service, we just flush the last parameter and return */
	if (self->settings.service_type == IRCOMM_3_WIRE) {
		ircomm_param_request(self, IRCOMM_FLOW_CONTROL, TRUE);
		return 0;
	}

	/* Only 9-wire service types continue here */
	ircomm_param_request(self, IRCOMM_FLOW_CONTROL, FALSE);
#if 0
	ircomm_param_request(self, IRCOMM_XON_XOFF, FALSE);
	ircomm_param_request(self, IRCOMM_ENQ_ACK, FALSE);
#endif
	/* Notify peer that we are ready to receive data */
	ircomm_param_request(self, IRCOMM_DTE, TRUE);

	return 0;
}

/*
 * Function ircomm_tty_discovery_indication (discovery)
 *
 *    Remote device is discovered, try query the remote IAS to see which
 *    device it is, and which services it has.
 *
 */
static void ircomm_tty_discovery_indication(discinfo_t *discovery,
					    DISCOVERY_MODE mode,
					    void *priv)
{
	struct ircomm_tty_cb *self;
	struct ircomm_tty_info info;

	IRDA_DEBUG(2, "%s()\n", __func__ );

	/* Important note :
	 * We need to drop all passive discoveries.
	 * The LSAP management of IrComm is deficient and doesn't deal
	 * with the case of two instance connecting to each other
	 * simultaneously (it will deadlock in LMP).
	 * The proper fix would be to use the same technique as in IrNET,
	 * to have one server socket and separate instances for the
	 * connecting/connected socket.
	 * The workaround is to drop passive discovery, which drastically
	 * reduce the probability of this happening.
	 * Jean II */
	if(mode == DISCOVERY_PASSIVE)
		return;

	info.daddr = discovery->daddr;
	info.saddr = discovery->saddr;

	self = (struct ircomm_tty_cb *) priv;
	ircomm_tty_do_event(self, IRCOMM_TTY_DISCOVERY_INDICATION,
			    NULL, &info);
}

/*
 * Function ircomm_tty_disconnect_indication (instance, sap, reason, skb)
 *
 *    Link disconnected
 *
 */
void ircomm_tty_disconnect_indication(void *instance, void *sap,
				      LM_REASON reason,
				      struct sk_buff *skb)
{
	struct ircomm_tty_cb *self = (struct ircomm_tty_cb *) instance;

	IRDA_DEBUG(2, "%s()\n", __func__ );

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IRCOMM_TTY_MAGIC, return;);

	if (!self->tty)
		return;

	/* This will stop control data transfers */
	self->flow = FLOW_STOP;

	/* Stop data transfers */
	self->tty->hw_stopped = 1;

	ircomm_tty_do_event(self, IRCOMM_TTY_DISCONNECT_INDICATION, NULL,
			    NULL);
}

/*
 * Function ircomm_tty_getvalue_confirm (result, obj_id, value, priv)
 *
 *    Got result from the IAS query we make
 *
 */
static void ircomm_tty_getvalue_confirm(int result, __u16 obj_id,
					struct ias_value *value,
					void *priv)
{
	struct ircomm_tty_cb *self = (struct ircomm_tty_cb *) priv;

	IRDA_DEBUG(2, "%s()\n", __func__ );

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IRCOMM_TTY_MAGIC, return;);

	/* We probably don't need to make any more queries */
	iriap_close(self->iriap);
	self->iriap = NULL;

	/* Check if request succeeded */
	if (result != IAS_SUCCESS) {
		IRDA_DEBUG(4, "%s(), got NULL value!\n", __func__ );
		return;
	}

	switch (value->type) {
	case IAS_OCT_SEQ:
		IRDA_DEBUG(2, "%s(), got octet sequence\n", __func__ );

		irda_param_extract_all(self, value->t.oct_seq, value->len,
				       &ircomm_param_info);

		ircomm_tty_do_event(self, IRCOMM_TTY_GOT_PARAMETERS, NULL,
				    NULL);
		break;
	case IAS_INTEGER:
		/* Got LSAP selector */
		IRDA_DEBUG(2, "%s(), got lsapsel = %d\n", __func__ ,
			   value->t.integer);

		if (value->t.integer == -1) {
			IRDA_DEBUG(0, "%s(), invalid value!\n", __func__ );
		} else
			self->dlsap_sel = value->t.integer;

		ircomm_tty_do_event(self, IRCOMM_TTY_GOT_LSAPSEL, NULL, NULL);
		break;
	case IAS_MISSING:
		IRDA_DEBUG(0, "%s(), got IAS_MISSING\n", __func__ );
		break;
	default:
		IRDA_DEBUG(0, "%s(), got unknown type!\n", __func__ );
		break;
	}
	irias_delete_value(value);
}

/*
 * Function ircomm_tty_connect_confirm (instance, sap, qos, max_sdu_size, skb)
 *
 *    Connection confirmed
 *
 */
void ircomm_tty_connect_confirm(void *instance, void *sap,
				struct qos_info *qos,
				__u32 max_data_size,
				__u8 max_header_size,
				struct sk_buff *skb)
{
	struct ircomm_tty_cb *self = (struct ircomm_tty_cb *) instance;

	IRDA_DEBUG(2, "%s()\n", __func__ );

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IRCOMM_TTY_MAGIC, return;);

	self->client = TRUE;
	self->max_data_size = max_data_size;
	self->max_header_size = max_header_size;
	self->flow = FLOW_START;

	ircomm_tty_do_event(self, IRCOMM_TTY_CONNECT_CONFIRM, NULL, NULL);

	/* No need to kfree_skb - see ircomm_ttp_connect_confirm() */
}

/*
 * Function ircomm_tty_connect_indication (instance, sap, qos, max_sdu_size,
 *                                         skb)
 *
 *    we are discovered and being requested to connect by remote device !
 *
 */
void ircomm_tty_connect_indication(void *instance, void *sap,
				   struct qos_info *qos,
				   __u32 max_data_size,
				   __u8 max_header_size,
				   struct sk_buff *skb)
{
	struct ircomm_tty_cb *self = (struct ircomm_tty_cb *) instance;
	int clen;

	IRDA_DEBUG(2, "%s()\n", __func__ );

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IRCOMM_TTY_MAGIC, return;);

	self->client = FALSE;
	self->max_data_size = max_data_size;
	self->max_header_size = max_header_size;
	self->flow = FLOW_START;

	clen = skb->data[0];
	if (clen)
		irda_param_extract_all(self, skb->data+1,
				       IRDA_MIN(skb->len, clen),
				       &ircomm_param_info);

	ircomm_tty_do_event(self, IRCOMM_TTY_CONNECT_INDICATION, NULL, NULL);

	/* No need to kfree_skb - see ircomm_ttp_connect_indication() */
}

/*
 * Function ircomm_tty_link_established (self)
 *
 *    Called when the IrCOMM link is established
 *
 */
void ircomm_tty_link_established(struct ircomm_tty_cb *self)
{
	IRDA_DEBUG(2, "%s()\n", __func__ );

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IRCOMM_TTY_MAGIC, return;);

	if (!self->tty)
		return;

	del_timer(&self->watchdog_timer);

	/*
	 * IrCOMM link is now up, and if we are not using hardware
	 * flow-control, then declare the hardware as running. Otherwise we
	 * will have to wait for the peer device (DCE) to raise the CTS
	 * line.
	 */
	if ((self->flags & ASYNC_CTS_FLOW) && ((self->settings.dce & IRCOMM_CTS) == 0)) {
		IRDA_DEBUG(0, "%s(), waiting for CTS ...\n", __func__ );
		return;
	} else {
		IRDA_DEBUG(1, "%s(), starting hardware!\n", __func__ );

		self->tty->hw_stopped = 0;

		/* Wake up processes blocked on open */
		wake_up_interruptible(&self->open_wait);
	}

	schedule_work(&self->tqueue);
}

/*
 * Function ircomm_tty_start_watchdog_timer (self, timeout)
 *
 *    Start the watchdog timer. This timer is used to make sure that any
 *    connection attempt is successful, and if not, we will retry after
 *    the timeout
 */
static void ircomm_tty_start_watchdog_timer(struct ircomm_tty_cb *self,
					    int timeout)
{
	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IRCOMM_TTY_MAGIC, return;);

	irda_start_timer(&self->watchdog_timer, timeout, (void *) self,
			 ircomm_tty_watchdog_timer_expired);
}

/*
 * Function ircomm_tty_watchdog_timer_expired (data)
 *
 *    Called when the connect procedure have taken to much time.
 *
 */
static void ircomm_tty_watchdog_timer_expired(void *data)
{
	struct ircomm_tty_cb *self = (struct ircomm_tty_cb *) data;

	IRDA_DEBUG(2, "%s()\n", __func__ );

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IRCOMM_TTY_MAGIC, return;);

	ircomm_tty_do_event(self, IRCOMM_TTY_WD_TIMER_EXPIRED, NULL, NULL);
}


/*
 * Function ircomm_tty_do_event (self, event, skb)
 *
 *    Process event
 *
 */
int ircomm_tty_do_event(struct ircomm_tty_cb *self, IRCOMM_TTY_EVENT event,
			struct sk_buff *skb, struct ircomm_tty_info *info)
{
	IRDA_ASSERT(self != NULL, return -1;);
	IRDA_ASSERT(self->magic == IRCOMM_TTY_MAGIC, return -1;);

	IRDA_DEBUG(2, "%s: state=%s, event=%s\n", __func__ ,
		   ircomm_tty_state[self->state], ircomm_tty_event[event]);

	return (*state[self->state])(self, event, skb, info);
}

/*
 * Function ircomm_tty_next_state (self, state)
 *
 *    Switch state
 *
 */
static inline void ircomm_tty_next_state(struct ircomm_tty_cb *self, IRCOMM_TTY_STATE state)
{
	/*
	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IRCOMM_TTY_MAGIC, return;);

	IRDA_DEBUG(2, "%s: next state=%s, service type=%d\n", __func__ ,
		   ircomm_tty_state[self->state], self->service_type);
	*/
	self->state = state;
}

/*
 * Function ircomm_tty_state_idle (self, event, skb, info)
 *
 *    Just hanging around
 *
 */
static int ircomm_tty_state_idle(struct ircomm_tty_cb *self,
				 IRCOMM_TTY_EVENT event,
				 struct sk_buff *skb,
				 struct ircomm_tty_info *info)
{
	int ret = 0;

	IRDA_DEBUG(2, "%s: state=%s, event=%s\n", __func__ ,
		   ircomm_tty_state[self->state], ircomm_tty_event[event]);
	switch (event) {
	case IRCOMM_TTY_ATTACH_CABLE:
		/* Try to discover any remote devices */
		ircomm_tty_start_watchdog_timer(self, 3*HZ);
		ircomm_tty_next_state(self, IRCOMM_TTY_SEARCH);

		irlmp_discovery_request(DISCOVERY_DEFAULT_SLOTS);
		break;
	case IRCOMM_TTY_DISCOVERY_INDICATION:
		self->daddr = info->daddr;
		self->saddr = info->saddr;

		if (self->iriap) {
			IRDA_WARNING("%s(), busy with a previous query\n",
				     __func__);
			return -EBUSY;
		}

		self->iriap = iriap_open(LSAP_ANY, IAS_CLIENT, self,
					 ircomm_tty_getvalue_confirm);

		iriap_getvaluebyclass_request(self->iriap,
					      self->saddr, self->daddr,
					      "IrDA:IrCOMM", "Parameters");

		ircomm_tty_start_watchdog_timer(self, 3*HZ);
		ircomm_tty_next_state(self, IRCOMM_TTY_QUERY_PARAMETERS);
		break;
	case IRCOMM_TTY_CONNECT_INDICATION:
		del_timer(&self->watchdog_timer);

		/* Accept connection */
		ircomm_connect_response(self->ircomm, NULL);
		ircomm_tty_next_state(self, IRCOMM_TTY_READY);
		break;
	case IRCOMM_TTY_WD_TIMER_EXPIRED:
		/* Just stay idle */
		break;
	case IRCOMM_TTY_DETACH_CABLE:
		ircomm_tty_next_state(self, IRCOMM_TTY_IDLE);
		break;
	default:
		IRDA_DEBUG(2, "%s(), unknown event: %s\n", __func__ ,
			   ircomm_tty_event[event]);
		ret = -EINVAL;
	}
	return ret;
}

/*
 * Function ircomm_tty_state_search (self, event, skb, info)
 *
 *    Trying to discover an IrCOMM device
 *
 */
static int ircomm_tty_state_search(struct ircomm_tty_cb *self,
				   IRCOMM_TTY_EVENT event,
				   struct sk_buff *skb,
				   struct ircomm_tty_info *info)
{
	int ret = 0;

	IRDA_DEBUG(2, "%s: state=%s, event=%s\n", __func__ ,
		   ircomm_tty_state[self->state], ircomm_tty_event[event]);

	switch (event) {
	case IRCOMM_TTY_DISCOVERY_INDICATION:
		self->daddr = info->daddr;
		self->saddr = info->saddr;

		if (self->iriap) {
			IRDA_WARNING("%s(), busy with a previous query\n",
				     __func__);
			return -EBUSY;
		}

		self->iriap = iriap_open(LSAP_ANY, IAS_CLIENT, self,
					 ircomm_tty_getvalue_confirm);

		if (self->service_type == IRCOMM_3_WIRE_RAW) {
			iriap_getvaluebyclass_request(self->iriap, self->saddr,
						      self->daddr, "IrLPT",
						      "IrDA:IrLMP:LsapSel");
			ircomm_tty_next_state(self, IRCOMM_TTY_QUERY_LSAP_SEL);
		} else {
			iriap_getvaluebyclass_request(self->iriap, self->saddr,
						      self->daddr,
						      "IrDA:IrCOMM",
						      "Parameters");

			ircomm_tty_next_state(self, IRCOMM_TTY_QUERY_PARAMETERS);
		}
		ircomm_tty_start_watchdog_timer(self, 3*HZ);
		break;
	case IRCOMM_TTY_CONNECT_INDICATION:
		del_timer(&self->watchdog_timer);
		ircomm_tty_ias_unregister(self);

		/* Accept connection */
		ircomm_connect_response(self->ircomm, NULL);
		ircomm_tty_next_state(self, IRCOMM_TTY_READY);
		break;
	case IRCOMM_TTY_WD_TIMER_EXPIRED:
#if 1
		/* Give up */
#else
		/* Try to discover any remote devices */
		ircomm_tty_start_watchdog_timer(self, 3*HZ);
		irlmp_discovery_request(DISCOVERY_DEFAULT_SLOTS);
#endif
		break;
	case IRCOMM_TTY_DETACH_CABLE:
		ircomm_tty_next_state(self, IRCOMM_TTY_IDLE);
		break;
	default:
		IRDA_DEBUG(2, "%s(), unknown event: %s\n", __func__ ,
			   ircomm_tty_event[event]);
		ret = -EINVAL;
	}
	return ret;
}

/*
 * Function ircomm_tty_state_query (self, event, skb, info)
 *
 *    Querying the remote LM-IAS for IrCOMM parameters
 *
 */
static int ircomm_tty_state_query_parameters(struct ircomm_tty_cb *self,
					     IRCOMM_TTY_EVENT event,
					     struct sk_buff *skb,
					     struct ircomm_tty_info *info)
{
	int ret = 0;

	IRDA_DEBUG(2, "%s: state=%s, event=%s\n", __func__ ,
		   ircomm_tty_state[self->state], ircomm_tty_event[event]);

	switch (event) {
	case IRCOMM_TTY_GOT_PARAMETERS:
		if (self->iriap) {
			IRDA_WARNING("%s(), busy with a previous query\n",
				     __func__);
			return -EBUSY;
		}

		self->iriap = iriap_open(LSAP_ANY, IAS_CLIENT, self,
					 ircomm_tty_getvalue_confirm);

		iriap_getvaluebyclass_request(self->iriap, self->saddr,
					      self->daddr, "IrDA:IrCOMM",
					      "IrDA:TinyTP:LsapSel");

		ircomm_tty_start_watchdog_timer(self, 3*HZ);
		ircomm_tty_next_state(self, IRCOMM_TTY_QUERY_LSAP_SEL);
		break;
	case IRCOMM_TTY_WD_TIMER_EXPIRED:
		/* Go back to search mode */
		ircomm_tty_next_state(self, IRCOMM_TTY_SEARCH);
		ircomm_tty_start_watchdog_timer(self, 3*HZ);
		break;
	case IRCOMM_TTY_CONNECT_INDICATION:
		del_timer(&self->watchdog_timer);
		ircomm_tty_ias_unregister(self);

		/* Accept connection */
		ircomm_connect_response(self->ircomm, NULL);
		ircomm_tty_next_state(self, IRCOMM_TTY_READY);
		break;
	case IRCOMM_TTY_DETACH_CABLE:
		ircomm_tty_next_state(self, IRCOMM_TTY_IDLE);
		break;
	default:
		IRDA_DEBUG(2, "%s(), unknown event: %s\n", __func__ ,
			   ircomm_tty_event[event]);
		ret = -EINVAL;
	}
	return ret;
}

/*
 * Function ircomm_tty_state_query_lsap_sel (self, event, skb, info)
 *
 *    Query remote LM-IAS for the LSAP selector which we can connect to
 *
 */
static int ircomm_tty_state_query_lsap_sel(struct ircomm_tty_cb *self,
					   IRCOMM_TTY_EVENT event,
					   struct sk_buff *skb,
					   struct ircomm_tty_info *info)
{
	int ret = 0;

	IRDA_DEBUG(2, "%s: state=%s, event=%s\n", __func__ ,
		   ircomm_tty_state[self->state], ircomm_tty_event[event]);

	switch (event) {
	case IRCOMM_TTY_GOT_LSAPSEL:
		/* Connect to remote device */
		ret = ircomm_connect_request(self->ircomm, self->dlsap_sel,
					     self->saddr, self->daddr,
					     NULL, self->service_type);
		ircomm_tty_start_watchdog_timer(self, 3*HZ);
		ircomm_tty_next_state(self, IRCOMM_TTY_SETUP);
		break;
	case IRCOMM_TTY_WD_TIMER_EXPIRED:
		/* Go back to search mode */
		ircomm_tty_next_state(self, IRCOMM_TTY_SEARCH);
		ircomm_tty_start_watchdog_timer(self, 3*HZ);
		break;
	case IRCOMM_TTY_CONNECT_INDICATION:
		del_timer(&self->watchdog_timer);
		ircomm_tty_ias_unregister(self);

		/* Accept connection */
		ircomm_connect_response(self->ircomm, NULL);
		ircomm_tty_next_state(self, IRCOMM_TTY_READY);
		break;
	case IRCOMM_TTY_DETACH_CABLE:
		ircomm_tty_next_state(self, IRCOMM_TTY_IDLE);
		break;
	default:
		IRDA_DEBUG(2, "%s(), unknown event: %s\n", __func__ ,
			   ircomm_tty_event[event]);
		ret = -EINVAL;
	}
	return ret;
}

/*
 * Function ircomm_tty_state_setup (self, event, skb, info)
 *
 *    Trying to connect
 *
 */
static int ircomm_tty_state_setup(struct ircomm_tty_cb *self,
				  IRCOMM_TTY_EVENT event,
				  struct sk_buff *skb,
				  struct ircomm_tty_info *info)
{
	int ret = 0;

	IRDA_DEBUG(2, "%s: state=%s, event=%s\n", __func__ ,
		   ircomm_tty_state[self->state], ircomm_tty_event[event]);

	switch (event) {
	case IRCOMM_TTY_CONNECT_CONFIRM:
		del_timer(&self->watchdog_timer);
		ircomm_tty_ias_unregister(self);

		/*
		 * Send initial parameters. This will also send out queued
		 * parameters waiting for the connection to come up
		 */
		ircomm_tty_send_initial_parameters(self);
		ircomm_tty_link_established(self);
		ircomm_tty_next_state(self, IRCOMM_TTY_READY);
		break;
	case IRCOMM_TTY_CONNECT_INDICATION:
		del_timer(&self->watchdog_timer);
		ircomm_tty_ias_unregister(self);

		/* Accept connection */
		ircomm_connect_response(self->ircomm, NULL);
		ircomm_tty_next_state(self, IRCOMM_TTY_READY);
		break;
	case IRCOMM_TTY_WD_TIMER_EXPIRED:
		/* Go back to search mode */
		ircomm_tty_next_state(self, IRCOMM_TTY_SEARCH);
		ircomm_tty_start_watchdog_timer(self, 3*HZ);
		break;
	case IRCOMM_TTY_DETACH_CABLE:
		/* ircomm_disconnect_request(self->ircomm, NULL); */
		ircomm_tty_next_state(self, IRCOMM_TTY_IDLE);
		break;
	default:
		IRDA_DEBUG(2, "%s(), unknown event: %s\n", __func__ ,
			   ircomm_tty_event[event]);
		ret = -EINVAL;
	}
	return ret;
}

/*
 * Function ircomm_tty_state_ready (self, event, skb, info)
 *
 *    IrCOMM is now connected
 *
 */
static int ircomm_tty_state_ready(struct ircomm_tty_cb *self,
				  IRCOMM_TTY_EVENT event,
				  struct sk_buff *skb,
				  struct ircomm_tty_info *info)
{
	int ret = 0;

	switch (event) {
	case IRCOMM_TTY_DATA_REQUEST:
		ret = ircomm_data_request(self->ircomm, skb);
		break;
	case IRCOMM_TTY_DETACH_CABLE:
		ircomm_disconnect_request(self->ircomm, NULL);
		ircomm_tty_next_state(self, IRCOMM_TTY_IDLE);
		break;
	case IRCOMM_TTY_DISCONNECT_INDICATION:
		ircomm_tty_ias_register(self);
		ircomm_tty_next_state(self, IRCOMM_TTY_SEARCH);
		ircomm_tty_start_watchdog_timer(self, 3*HZ);

		if (self->flags & ASYNC_CHECK_CD) {
			/* Drop carrier */
			self->settings.dce = IRCOMM_DELTA_CD;
			ircomm_tty_check_modem_status(self);
		} else {
			IRDA_DEBUG(0, "%s(), hanging up!\n", __func__ );
			if (self->tty)
				tty_hangup(self->tty);
		}
		break;
	default:
		IRDA_DEBUG(2, "%s(), unknown event: %s\n", __func__ ,
			   ircomm_tty_event[event]);
		ret = -EINVAL;
	}
	return ret;
}

