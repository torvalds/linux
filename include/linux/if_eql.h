/*
 * Equalizer Load-balancer for serial network interfaces.
 *
 * (c) Copyright 1995 Simon "Guru Aleph-Null" Janes
 * NCM: Network and Communications Management, Inc.
 *
 *
 *	This software may be used and distributed according to the terms
 *	of the GNU General Public License, incorporated herein by reference.
 * 
 * The author may be reached as simon@ncm.com, or C/O
 *    NCM
 *    Attn: Simon Janes
 *    6803 Whittier Ave
 *    McLean VA 22101
 *    Phone: 1-703-847-0040 ext 103
 */
#ifndef _LINUX_IF_EQL_H
#define _LINUX_IF_EQL_H


#include <linux/timer.h>
#include <linux/spinlock.h>
#include <uapi/linux/if_eql.h>

typedef struct slave {
	struct list_head	list;
	struct net_device	*dev;
	netdevice_tracker	dev_tracker;
	long			priority;
	long			priority_bps;
	long			priority_Bps;
	long			bytes_queued;
} slave_t;

typedef struct slave_queue {
	spinlock_t		lock;
	struct list_head	all_slaves;
	int			num_slaves;
	struct net_device	*master_dev;
} slave_queue_t;

typedef struct equalizer {
	slave_queue_t		queue;
	int			min_slaves;
	int			max_slaves;
	struct timer_list	timer;
} equalizer_t;  

#endif /* _LINUX_EQL_H */
