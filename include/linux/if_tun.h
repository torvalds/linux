/*
 *  Universal TUN/TAP device driver.
 *  Copyright (C) 1999-2000 Maxim Krasnyansky <max_mk@yahoo.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  $Id: if_tun.h,v 1.2 2001/06/01 18:39:47 davem Exp $
 */

#ifndef __IF_TUN_H
#define __IF_TUN_H

/* Uncomment to enable debugging */
/* #define TUN_DEBUG 1 */

#ifdef __KERNEL__

#ifdef TUN_DEBUG
#define DBG  if(tun->debug)printk
#define DBG1 if(debug==2)printk
#else
#define DBG( a... )
#define DBG1( a... )
#endif

struct tun_struct {
	struct list_head        list;
	unsigned long 		flags;
	int			attached;
	uid_t			owner;

	wait_queue_head_t	read_wait;
	struct sk_buff_head	readq;

	struct net_device	*dev;
	struct net_device_stats	stats;

	struct fasync_struct    *fasync;

	unsigned long if_flags;
	u8 dev_addr[ETH_ALEN];
	u32 chr_filter[2];
	u32 net_filter[2];

#ifdef TUN_DEBUG	
	int debug;
#endif  
};

#endif /* __KERNEL__ */

/* Read queue size */
#define TUN_READQ_SIZE	500

/* TUN device flags */
#define TUN_TUN_DEV 	0x0001	
#define TUN_TAP_DEV	0x0002
#define TUN_TYPE_MASK   0x000f

#define TUN_FASYNC	0x0010
#define TUN_NOCHECKSUM	0x0020
#define TUN_NO_PI	0x0040
#define TUN_ONE_QUEUE	0x0080
#define TUN_PERSIST 	0x0100	

/* Ioctl defines */
#define TUNSETNOCSUM  _IOW('T', 200, int) 
#define TUNSETDEBUG   _IOW('T', 201, int) 
#define TUNSETIFF     _IOW('T', 202, int) 
#define TUNSETPERSIST _IOW('T', 203, int) 
#define TUNSETOWNER   _IOW('T', 204, int)
#define TUNSETLINK    _IOW('T', 205, int)

/* TUNSETIFF ifr flags */
#define IFF_TUN		0x0001
#define IFF_TAP		0x0002
#define IFF_NO_PI	0x1000
#define IFF_ONE_QUEUE	0x2000

struct tun_pi {
	unsigned short flags;
	unsigned short proto;
};
#define TUN_PKT_STRIP	0x0001

#endif /* __IF_TUN_H */
