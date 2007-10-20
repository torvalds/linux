/*********************************************************************
 *
 * Filename:      irlan_provider_event.c
 * Version:       0.9
 * Description:   IrLAN provider state machine)
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Sun Aug 31 20:14:37 1997
 * Modified at:   Sat Oct 30 12:52:41 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 *
 *     Copyright (c) 1998-1999 Dag Brattli <dagb@cs.uit.no>, All Rights Reserved.
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
#include <net/irda/iriap.h>
#include <net/irda/irlmp.h>
#include <net/irda/irttp.h>

#include <net/irda/irlan_provider.h>
#include <net/irda/irlan_event.h>

static int irlan_provider_state_idle(struct irlan_cb *self, IRLAN_EVENT event,
				     struct sk_buff *skb);
static int irlan_provider_state_info(struct irlan_cb *self, IRLAN_EVENT event,
				     struct sk_buff *skb);
static int irlan_provider_state_open(struct irlan_cb *self, IRLAN_EVENT event,
				     struct sk_buff *skb);
static int irlan_provider_state_data(struct irlan_cb *self, IRLAN_EVENT event,
				     struct sk_buff *skb);

static int (*state[])(struct irlan_cb *self, IRLAN_EVENT event,
		      struct sk_buff *skb) =
{
	irlan_provider_state_idle,
	NULL, /* Query */
	NULL, /* Info */
	irlan_provider_state_info,
	NULL, /* Media */
	irlan_provider_state_open,
	NULL, /* Wait */
	NULL, /* Arb */
	irlan_provider_state_data,
	NULL, /* Close */
	NULL, /* Sync */
};

void irlan_do_provider_event(struct irlan_cb *self, IRLAN_EVENT event,
			     struct sk_buff *skb)
{
	IRDA_ASSERT(*state[ self->provider.state] != NULL, return;);

	(*state[self->provider.state]) (self, event, skb);
}

/*
 * Function irlan_provider_state_idle (event, skb, info)
 *
 *    IDLE, We are waiting for an indication that there is a provider
 *    available.
 */
static int irlan_provider_state_idle(struct irlan_cb *self, IRLAN_EVENT event,
				     struct sk_buff *skb)
{
	IRDA_DEBUG(4, "%s()\n", __FUNCTION__ );

	IRDA_ASSERT(self != NULL, return -1;);

	switch(event) {
	case IRLAN_CONNECT_INDICATION:
	     irlan_provider_connect_response( self, self->provider.tsap_ctrl);
	     irlan_next_provider_state( self, IRLAN_INFO);
	     break;
	default:
		IRDA_DEBUG(4, "%s(), Unknown event %d\n", __FUNCTION__ , event);
		break;
	}
	if (skb)
		dev_kfree_skb(skb);

	return 0;
}

/*
 * Function irlan_provider_state_info (self, event, skb, info)
 *
 *    INFO, We have issued a GetInfo command and is awaiting a reply.
 */
static int irlan_provider_state_info(struct irlan_cb *self, IRLAN_EVENT event,
				     struct sk_buff *skb)
{
	int ret;

	IRDA_DEBUG(4, "%s()\n", __FUNCTION__ );

	IRDA_ASSERT(self != NULL, return -1;);

	switch(event) {
	case IRLAN_GET_INFO_CMD:
		/* Be sure to use 802.3 in case of peer mode */
		if (self->provider.access_type == ACCESS_PEER) {
			self->media = MEDIA_802_3;

			/* Check if client has started yet */
			if (self->client.state == IRLAN_IDLE) {
				/* This should get the client going */
				irlmp_discovery_request(8);
			}
		}

		irlan_provider_send_reply(self, CMD_GET_PROVIDER_INFO,
					  RSP_SUCCESS);
		/* Keep state */
		break;
	case IRLAN_GET_MEDIA_CMD:
		irlan_provider_send_reply(self, CMD_GET_MEDIA_CHAR,
					  RSP_SUCCESS);
		/* Keep state */
		break;
	case IRLAN_OPEN_DATA_CMD:
		ret = irlan_parse_open_data_cmd(self, skb);
		if (self->provider.access_type == ACCESS_PEER) {
			/* FIXME: make use of random functions! */
			self->provider.send_arb_val = (jiffies & 0xffff);
		}
		irlan_provider_send_reply(self, CMD_OPEN_DATA_CHANNEL, ret);

		if (ret == RSP_SUCCESS) {
			irlan_next_provider_state(self, IRLAN_OPEN);

			/* Signal client that we are now open */
			irlan_do_client_event(self, IRLAN_PROVIDER_SIGNAL, NULL);
		}
		break;
	case IRLAN_LMP_DISCONNECT:  /* FALLTHROUGH */
	case IRLAN_LAP_DISCONNECT:
		irlan_next_provider_state(self, IRLAN_IDLE);
		break;
	default:
		IRDA_DEBUG( 0, "%s(), Unknown event %d\n", __FUNCTION__ , event);
		break;
	}
	if (skb)
		dev_kfree_skb(skb);

	return 0;
}

/*
 * Function irlan_provider_state_open (self, event, skb, info)
 *
 *    OPEN, The client has issued a OpenData command and is awaiting a
 *    reply
 *
 */
static int irlan_provider_state_open(struct irlan_cb *self, IRLAN_EVENT event,
				     struct sk_buff *skb)
{
	IRDA_DEBUG(4, "%s()\n", __FUNCTION__ );

	IRDA_ASSERT(self != NULL, return -1;);

	switch(event) {
	case IRLAN_FILTER_CONFIG_CMD:
		irlan_provider_parse_command(self, CMD_FILTER_OPERATION, skb);
		irlan_provider_send_reply(self, CMD_FILTER_OPERATION,
					  RSP_SUCCESS);
		/* Keep state */
		break;
	case IRLAN_DATA_CONNECT_INDICATION:
		irlan_next_provider_state(self, IRLAN_DATA);
		irlan_provider_connect_response(self, self->tsap_data);
		break;
	case IRLAN_LMP_DISCONNECT:  /* FALLTHROUGH */
	case IRLAN_LAP_DISCONNECT:
		irlan_next_provider_state(self, IRLAN_IDLE);
		break;
	default:
		IRDA_DEBUG(2, "%s(), Unknown event %d\n", __FUNCTION__ , event);
		break;
	}
	if (skb)
		dev_kfree_skb(skb);

	return 0;
}

/*
 * Function irlan_provider_state_data (self, event, skb, info)
 *
 *    DATA, The data channel is connected, allowing data transfers between
 *    the local and remote machines.
 *
 */
static int irlan_provider_state_data(struct irlan_cb *self, IRLAN_EVENT event,
				     struct sk_buff *skb)
{
	IRDA_DEBUG(4, "%s()\n", __FUNCTION__ );

	IRDA_ASSERT(self != NULL, return -1;);
	IRDA_ASSERT(self->magic == IRLAN_MAGIC, return -1;);

	switch(event) {
	case IRLAN_FILTER_CONFIG_CMD:
		irlan_provider_parse_command(self, CMD_FILTER_OPERATION, skb);
		irlan_provider_send_reply(self, CMD_FILTER_OPERATION,
					  RSP_SUCCESS);
		break;
	case IRLAN_LMP_DISCONNECT: /* FALLTHROUGH */
	case IRLAN_LAP_DISCONNECT:
		irlan_next_provider_state(self, IRLAN_IDLE);
		break;
	default:
		IRDA_DEBUG( 0, "%s(), Unknown event %d\n", __FUNCTION__ , event);
		break;
	}
	if (skb)
		dev_kfree_skb(skb);

	return 0;
}










