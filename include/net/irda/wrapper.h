/*********************************************************************
 *                
 * Filename:      wrapper.h
 * Version:       1.2
 * Description:   IrDA SIR async wrapper layer
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Mon Aug  4 20:40:53 1997
 * Modified at:   Tue Jan 11 12:37:29 2000
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1998-2000 Dag Brattli <dagb@cs.uit.no>, 
 *     All Rights Reserved.
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

#ifndef WRAPPER_H
#define WRAPPER_H

#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>

#include <net/irda/irda_device.h>	/* iobuff_t */

#define BOF  0xc0 /* Beginning of frame */
#define XBOF 0xff
#define EOF  0xc1 /* End of frame */
#define CE   0x7d /* Control escape */

#define STA BOF  /* Start flag */
#define STO EOF  /* End flag */

#define IRDA_TRANS 0x20    /* Asynchronous transparency modifier */       

/* States for receiving a frame in async mode */
enum {
	OUTSIDE_FRAME, 
	BEGIN_FRAME, 
	LINK_ESCAPE, 
	INSIDE_FRAME
};

/* Proto definitions */
int async_wrap_skb(struct sk_buff *skb, __u8 *tx_buff, int buffsize);
void async_unwrap_char(struct net_device *dev, struct net_device_stats *stats,
		       iobuff_t *buf, __u8 byte);

#endif
