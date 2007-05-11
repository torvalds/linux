/*********************************************************************
 *                
 * Filename:      irlan_event.h
 * Version:       
 * Description:   LAN access
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Sun Aug 31 20:14:37 1997
 * Modified at:   Tue Feb  2 09:45:17 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1997 Dag Brattli <dagb@cs.uit.no>, All Rights Reserved.
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

#ifndef IRLAN_EVENT_H
#define IRLAN_EVENT_H

#include <linux/kernel.h>
#include <linux/skbuff.h>

#include <net/irda/irlan_common.h>

typedef enum {
	IRLAN_IDLE,
	IRLAN_QUERY,
	IRLAN_CONN, 
	IRLAN_INFO,
	IRLAN_MEDIA,
	IRLAN_OPEN,
	IRLAN_WAIT,
	IRLAN_ARB, 
	IRLAN_DATA,
	IRLAN_CLOSE,
	IRLAN_SYNC
} IRLAN_STATE;

typedef enum {
	IRLAN_DISCOVERY_INDICATION,
	IRLAN_IAS_PROVIDER_AVAIL,
	IRLAN_IAS_PROVIDER_NOT_AVAIL,
	IRLAN_LAP_DISCONNECT,
	IRLAN_LMP_DISCONNECT,
	IRLAN_CONNECT_COMPLETE,
	IRLAN_DATA_INDICATION,
	IRLAN_DATA_CONNECT_INDICATION,
	IRLAN_RETRY_CONNECT,

	IRLAN_CONNECT_INDICATION,
	IRLAN_GET_INFO_CMD,
	IRLAN_GET_MEDIA_CMD,
	IRLAN_OPEN_DATA_CMD,
	IRLAN_FILTER_CONFIG_CMD,

	IRLAN_CHECK_CON_ARB,
	IRLAN_PROVIDER_SIGNAL,

	IRLAN_WATCHDOG_TIMEOUT,
} IRLAN_EVENT;

extern char *irlan_state[];

void irlan_do_client_event(struct irlan_cb *self, IRLAN_EVENT event, 
			   struct sk_buff *skb);

void irlan_do_provider_event(struct irlan_cb *self, IRLAN_EVENT event, 
			     struct sk_buff *skb);

void irlan_next_client_state(struct irlan_cb *self, IRLAN_STATE state);
void irlan_next_provider_state(struct irlan_cb *self, IRLAN_STATE state);

#endif
