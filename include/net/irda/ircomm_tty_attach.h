/*********************************************************************
 *                
 * Filename:      ircomm_tty_attach.h
 * Version:       
 * Description:   
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Wed Jun  9 15:55:18 1999
 * Modified at:   Fri Dec 10 21:04:55 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1999 Dag Brattli, All Rights Reserved.
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

#ifndef IRCOMM_TTY_ATTACH_H
#define IRCOMM_TTY_ATTACH_H

#include <net/irda/ircomm_tty.h>

typedef enum {
        IRCOMM_TTY_IDLE,
	IRCOMM_TTY_SEARCH,
        IRCOMM_TTY_QUERY_PARAMETERS,
	IRCOMM_TTY_QUERY_LSAP_SEL,
	IRCOMM_TTY_SETUP,
        IRCOMM_TTY_READY,
} IRCOMM_TTY_STATE;

/* IrCOMM TTY Events */
typedef enum {
	IRCOMM_TTY_ATTACH_CABLE,
	IRCOMM_TTY_DETACH_CABLE,
	IRCOMM_TTY_DATA_REQUEST,
	IRCOMM_TTY_DATA_INDICATION,
	IRCOMM_TTY_DISCOVERY_REQUEST,
	IRCOMM_TTY_DISCOVERY_INDICATION,
	IRCOMM_TTY_CONNECT_CONFIRM,
	IRCOMM_TTY_CONNECT_INDICATION,
	IRCOMM_TTY_DISCONNECT_REQUEST,
	IRCOMM_TTY_DISCONNECT_INDICATION,
	IRCOMM_TTY_WD_TIMER_EXPIRED,
	IRCOMM_TTY_GOT_PARAMETERS,
	IRCOMM_TTY_GOT_LSAPSEL,
} IRCOMM_TTY_EVENT;

/* Used for passing information through the state-machine */
struct ircomm_tty_info {
        __u32     saddr;               /* Source device address */
        __u32     daddr;               /* Destination device address */
        __u8      dlsap_sel;
};

extern const char *const ircomm_state[];
extern const char *const ircomm_tty_state[];

int ircomm_tty_do_event(struct ircomm_tty_cb *self, IRCOMM_TTY_EVENT event,
			struct sk_buff *skb, struct ircomm_tty_info *info);


int  ircomm_tty_attach_cable(struct ircomm_tty_cb *self);
void ircomm_tty_detach_cable(struct ircomm_tty_cb *self);
void ircomm_tty_connect_confirm(void *instance, void *sap, 
				struct qos_info *qos, 
				__u32 max_sdu_size, 
				__u8 max_header_size, 
				struct sk_buff *skb);
void ircomm_tty_disconnect_indication(void *instance, void *sap, 
				      LM_REASON reason,
				      struct sk_buff *skb);
void ircomm_tty_connect_indication(void *instance, void *sap, 
				   struct qos_info *qos, 
				   __u32 max_sdu_size,
				   __u8 max_header_size, 
				   struct sk_buff *skb);
int ircomm_tty_send_initial_parameters(struct ircomm_tty_cb *self);
void ircomm_tty_link_established(struct ircomm_tty_cb *self);

#endif /* IRCOMM_TTY_ATTACH_H */
