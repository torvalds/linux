/*********************************************************************
 *                
 * Filename:      ircomm_event.h
 * Version:       
 * Description:   
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Sun Jun  6 23:51:13 1999
 * Modified at:   Thu Jun 10 08:36:25 1999
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

#ifndef IRCOMM_EVENT_H
#define IRCOMM_EVENT_H

#include <net/irda/irmod.h>

typedef enum {
        IRCOMM_IDLE,
        IRCOMM_WAITI,
        IRCOMM_WAITR,
        IRCOMM_CONN,
} IRCOMM_STATE;

/* IrCOMM Events */
typedef enum {
        IRCOMM_CONNECT_REQUEST,
        IRCOMM_CONNECT_RESPONSE,
        IRCOMM_TTP_CONNECT_INDICATION,
	IRCOMM_LMP_CONNECT_INDICATION,
        IRCOMM_TTP_CONNECT_CONFIRM,
	IRCOMM_LMP_CONNECT_CONFIRM,

        IRCOMM_LMP_DISCONNECT_INDICATION,
	IRCOMM_TTP_DISCONNECT_INDICATION,
        IRCOMM_DISCONNECT_REQUEST,

        IRCOMM_TTP_DATA_INDICATION,
	IRCOMM_LMP_DATA_INDICATION,
        IRCOMM_DATA_REQUEST,
        IRCOMM_CONTROL_REQUEST,
        IRCOMM_CONTROL_INDICATION,
} IRCOMM_EVENT;

/*
 * Used for passing information through the state-machine
 */
struct ircomm_info {
        __u32     saddr;               /* Source device address */
        __u32     daddr;               /* Destination device address */
        __u8      dlsap_sel;
        LM_REASON reason;              /* Reason for disconnect */
	__u32     max_data_size;
	__u32     max_header_size;

	struct qos_info *qos;
};

extern char *ircomm_state[];

struct ircomm_cb;   /* Forward decl. */

int ircomm_do_event(struct ircomm_cb *self, IRCOMM_EVENT event,
		    struct sk_buff *skb, struct ircomm_info *info);
void ircomm_next_state(struct ircomm_cb *self, IRCOMM_STATE state);

#endif
