/*********************************************************************
 *                
 * Filename:      irlan_client.h
 * Version:       0.3
 * Description:   IrDA LAN access layer
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Sun Aug 31 20:14:37 1997
 * Modified at:   Thu Apr 22 14:13:34 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1998 Dag Brattli <dagb@cs.uit.no>, All Rights Reserved.
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

#ifndef IRLAN_CLIENT_H
#define IRLAN_CLIENT_H

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>

#include <net/irda/irias_object.h>
#include <net/irda/irlan_event.h>

void irlan_client_discovery_indication(discinfo_t *, DISCOVERY_MODE, void *);
void irlan_client_wakeup(struct irlan_cb *self, __u32 saddr, __u32 daddr);

void irlan_client_parse_response(struct irlan_cb *self, struct sk_buff *skb);
void irlan_client_get_value_confirm(int result, __u16 obj_id, 
				    struct ias_value *value, void *priv);
#endif
