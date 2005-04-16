/*********************************************************************
 *                
 * Filename:      ircomm_core.h
 * Version:       
 * Description:   
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Wed Jun  9 08:58:43 1999
 * Modified at:   Mon Dec 13 11:52:29 1999
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

#ifndef IRCOMM_CORE_H
#define IRCOMM_CORE_H

#include <net/irda/irda.h>
#include <net/irda/irqueue.h>
#include <net/irda/ircomm_event.h>

#define IRCOMM_MAGIC 0x98347298
#define IRCOMM_HEADER_SIZE 1

struct ircomm_cb;   /* Forward decl. */

/*
 * A small call-table, so we don't have to check the service-type whenever
 * we want to do something
 */
typedef struct {
	int (*data_request)(struct ircomm_cb *, struct sk_buff *, int clen);
	int (*connect_request)(struct ircomm_cb *, struct sk_buff *, 
			       struct ircomm_info *);
	int (*connect_response)(struct ircomm_cb *, struct sk_buff *);
	int (*disconnect_request)(struct ircomm_cb *, struct sk_buff *, 
				  struct ircomm_info *);	
} call_t;

struct ircomm_cb {
	irda_queue_t queue;
	magic_t magic;

	notify_t notify;
	call_t   issue;

	int state;
	int line;            /* Which TTY line we are using */

	struct tsap_cb *tsap;
	struct lsap_cb *lsap;
	
	__u8 dlsap_sel;      /* Destination LSAP/TSAP selector */
	__u8 slsap_sel;      /* Source LSAP/TSAP selector */

	__u32 saddr;         /* Source device address (link we are using) */
	__u32 daddr;         /* Destination device address */

	int max_header_size; /* Header space we must reserve for each frame */
	int max_data_size;   /* The amount of data we can fill in each frame */

	LOCAL_FLOW flow_status; /* Used by ircomm_lmp */
	int pkt_count;          /* Number of frames we have sent to IrLAP */

	__u8 service_type;
};

extern hashbin_t *ircomm;

struct ircomm_cb *ircomm_open(notify_t *notify, __u8 service_type, int line);
int ircomm_close(struct ircomm_cb *self);

int ircomm_data_request(struct ircomm_cb *self, struct sk_buff *skb);
void ircomm_data_indication(struct ircomm_cb *self, struct sk_buff *skb);
void ircomm_process_data(struct ircomm_cb *self, struct sk_buff *skb);
int ircomm_control_request(struct ircomm_cb *self, struct sk_buff *skb);
int ircomm_connect_request(struct ircomm_cb *self, __u8 dlsap_sel, 
			   __u32 saddr, __u32 daddr, struct sk_buff *skb,
			   __u8 service_type);
void ircomm_connect_indication(struct ircomm_cb *self, struct sk_buff *skb,
			       struct ircomm_info *info);
void ircomm_connect_confirm(struct ircomm_cb *self, struct sk_buff *skb,
			    struct ircomm_info *info);
int ircomm_connect_response(struct ircomm_cb *self, struct sk_buff *userdata);
int ircomm_disconnect_request(struct ircomm_cb *self, struct sk_buff *userdata);
void ircomm_disconnect_indication(struct ircomm_cb *self, struct sk_buff *skb,
				  struct ircomm_info *info);
void ircomm_flow_request(struct ircomm_cb *self, LOCAL_FLOW flow);

#define ircomm_is_connected(self) (self->state == IRCOMM_CONN)

#endif
