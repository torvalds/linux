/*********************************************************************
 *                
 * Filename:      iriap_event.h
 * Version:       
 * Description:   
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Mon Aug  4 20:40:53 1997
 * Modified at:   Sun Oct 31 22:02:54 1999
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

#ifndef IRIAP_FSM_H
#define IRIAP_FSM_H

/* Forward because of circular include dependecies */
struct iriap_cb;

/* IrIAP states */
typedef enum {
	/* Client */
	S_DISCONNECT,
	S_CONNECTING,
	S_CALL,

	/* S-Call */
	S_MAKE_CALL,
	S_CALLING,
	S_OUTSTANDING,
	S_REPLYING,
	S_WAIT_FOR_CALL,
	S_WAIT_ACTIVE,

	/* Server */
	R_DISCONNECT,
	R_CALL,
	
	/* R-Connect */
	R_WAITING,
	R_WAIT_ACTIVE,
	R_RECEIVING,
	R_EXECUTE,
	R_RETURNING,
} IRIAP_STATE;

typedef enum {
	IAP_CALL_REQUEST,
	IAP_CALL_REQUEST_GVBC,
	IAP_CALL_RESPONSE,
	IAP_RECV_F_LST,
	IAP_LM_DISCONNECT_INDICATION,
	IAP_LM_CONNECT_INDICATION,
	IAP_LM_CONNECT_CONFIRM,
} IRIAP_EVENT;

void iriap_next_client_state   (struct iriap_cb *self, IRIAP_STATE state);
void iriap_next_call_state     (struct iriap_cb *self, IRIAP_STATE state);
void iriap_next_server_state   (struct iriap_cb *self, IRIAP_STATE state);
void iriap_next_r_connect_state(struct iriap_cb *self, IRIAP_STATE state);


void iriap_do_client_event(struct iriap_cb *self, IRIAP_EVENT event, 
			   struct sk_buff *skb);
void iriap_do_call_event  (struct iriap_cb *self, IRIAP_EVENT event, 
			   struct sk_buff *skb);

void iriap_do_server_event   (struct iriap_cb *self, IRIAP_EVENT event, 
			      struct sk_buff *skb);
void iriap_do_r_connect_event(struct iriap_cb *self, IRIAP_EVENT event, 
			      struct sk_buff *skb);

#endif /* IRIAP_FSM_H */

