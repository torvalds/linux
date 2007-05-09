/*********************************************************************
 *                
 * Filename:      irlan_eth.h
 * Version:       
 * Description:   
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Thu Oct 15 08:36:58 1998
 * Modified at:   Fri May 14 23:29:00 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1998-1999 Dag Brattli, All Rights Reserved.
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

#ifndef IRLAN_ETH_H
#define IRLAN_ETH_H

struct net_device *alloc_irlandev(const char *name);
int  irlan_eth_receive(void *instance, void *sap, struct sk_buff *skb);

void irlan_eth_flow_indication( void *instance, void *sap, LOCAL_FLOW flow);
void irlan_eth_send_gratuitous_arp(struct net_device *dev);
#endif
