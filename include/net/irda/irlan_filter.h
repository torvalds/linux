/*********************************************************************
 *                
 * Filename:      irlan_filter.h
 * Version:       
 * Description:   
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Fri Jan 29 15:24:08 1999
 * Modified at:   Sun Feb  7 23:35:31 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1998 Dag Brattli, All Rights Reserved.
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

#ifndef IRLAN_FILTER_H
#define IRLAN_FILTER_H

void irlan_check_command_param(struct irlan_cb *self, char *param, 
			       char *value);
void irlan_filter_request(struct irlan_cb *self, struct sk_buff *skb);
#ifdef CONFIG_PROC_FS
void irlan_print_filter(struct seq_file *seq, int filter_type);
#endif

#endif /* IRLAN_FILTER_H */
