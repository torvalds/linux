/*
 * net/tipc/core.h: Include file for TIPC global declarations
 * 
 * Copyright (c) 2005-2006, Ericsson AB
 * Copyright (c) 2005, Wind River Systems
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _TIPC_CORE_H
#define _TIPC_CORE_H

#include <net/tipc/tipc.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <asm/uaccess.h>
#include <linux/interrupt.h>
#include <asm/atomic.h>
#include <asm/hardirq.h>
#include <linux/netdevice.h>
#include <linux/in.h>	
#include <linux/list.h>
#include <linux/vmalloc.h>

/*
 * TIPC debugging code
 */

#define assert(i)  BUG_ON(!(i))

struct tipc_msg;
extern struct print_buf *CONS, *LOG;
extern struct print_buf *TEE(struct print_buf *, struct print_buf *);
void msg_print(struct print_buf*,struct tipc_msg *,const char*);
void tipc_printf(struct print_buf *, const char *fmt, ...);
void tipc_dump(struct print_buf*,const char *fmt, ...);

#ifdef CONFIG_TIPC_DEBUG

/*
 * TIPC debug support included:
 * - system messages are printed to TIPC_OUTPUT print buffer
 * - debug messages are printed to DBG_OUTPUT print buffer
 */

#define err(fmt, arg...)  tipc_printf(TIPC_OUTPUT, KERN_ERR "TIPC: " fmt, ## arg)
#define warn(fmt, arg...) tipc_printf(TIPC_OUTPUT, KERN_WARNING "TIPC: " fmt, ## arg)
#define info(fmt, arg...) tipc_printf(TIPC_OUTPUT, KERN_NOTICE "TIPC: " fmt, ## arg)

#define dbg(fmt, arg...)  do {if (DBG_OUTPUT) tipc_printf(DBG_OUTPUT, fmt, ## arg);} while(0)
#define msg_dbg(msg, txt) do {if (DBG_OUTPUT) msg_print(DBG_OUTPUT, msg, txt);} while(0)
#define dump(fmt, arg...) do {if (DBG_OUTPUT) tipc_dump(DBG_OUTPUT, fmt, ##arg);} while(0)


/*	
 * By default, TIPC_OUTPUT is defined to be system console and TIPC log buffer,
 * while DBG_OUTPUT is the null print buffer.  These defaults can be changed
 * here, or on a per .c file basis, by redefining these symbols.  The following
 * print buffer options are available:
 *
 * NULL			: Output to null print buffer (i.e. print nowhere)
 * CONS			: Output to system console
 * LOG			: Output to TIPC log buffer 
 * &buf 		: Output to user-defined buffer (struct print_buf *)
 * TEE(&buf_a,&buf_b)	: Output to two print buffers (eg. TEE(CONS,LOG) )
 */

#ifndef TIPC_OUTPUT
#define TIPC_OUTPUT TEE(CONS,LOG)
#endif

#ifndef DBG_OUTPUT
#define DBG_OUTPUT NULL
#endif

#else

#ifndef DBG_OUTPUT
#define DBG_OUTPUT NULL
#endif

/*
 * TIPC debug support not included:
 * - system messages are printed to system console
 * - debug messages are not printed
 */

#define err(fmt, arg...)  printk(KERN_ERR "TIPC: " fmt , ## arg)
#define info(fmt, arg...) printk(KERN_INFO "TIPC: " fmt , ## arg)
#define warn(fmt, arg...) printk(KERN_WARNING "TIPC: " fmt , ## arg)

#define dbg(fmt, arg...) do {} while (0)
#define msg_dbg(msg,txt) do {} while (0)
#define dump(fmt,arg...) do {} while (0)

#endif			  


/* 
 * TIPC-specific error codes
 */

#define ELINKCONG EAGAIN	/* link congestion <=> resource unavailable */

/*
 * Global configuration variables
 */

extern u32 tipc_own_addr;
extern int tipc_max_zones;
extern int tipc_max_clusters;
extern int tipc_max_nodes;
extern int tipc_max_slaves;
extern int tipc_max_ports;
extern int tipc_max_subscriptions;
extern int tipc_max_publications;
extern int tipc_net_id;
extern int tipc_remote_management;

/*
 * Other global variables
 */

extern int tipc_mode;
extern int tipc_random;
extern const char tipc_alphabet[];
extern atomic_t tipc_user_count;


/*
 * Routines available to privileged subsystems
 */

extern int  start_core(void);
extern void stop_core(void);
extern int  start_net(void);
extern void stop_net(void);

static inline int delimit(int val, int min, int max)
{
	if (val > max)
		return max;
	if (val < min)
		return min;
	return val;
}


/*
 * TIPC timer and signal code
 */

typedef void (*Handler) (unsigned long);

u32 k_signal(Handler routine, unsigned long argument);

/**
 * k_init_timer - initialize a timer
 * @timer: pointer to timer structure
 * @routine: pointer to routine to invoke when timer expires
 * @argument: value to pass to routine when timer expires
 * 
 * Timer must be initialized before use (and terminated when no longer needed).
 */

static inline void k_init_timer(struct timer_list *timer, Handler routine, 
				unsigned long argument)
{
	dbg("initializing timer %p\n", timer);
	init_timer(timer);
	timer->function = routine;
	timer->data = argument;
}

/**
 * k_start_timer - start a timer
 * @timer: pointer to timer structure
 * @msec: time to delay (in ms)
 * 
 * Schedules a previously initialized timer for later execution.
 * If timer is already running, the new timeout overrides the previous request.
 * 
 * To ensure the timer doesn't expire before the specified delay elapses,
 * the amount of delay is rounded up when converting to the jiffies
 * then an additional jiffy is added to account for the fact that 
 * the starting time may be in the middle of the current jiffy.
 */

static inline void k_start_timer(struct timer_list *timer, unsigned long msec)
{
	dbg("starting timer %p for %u\n", timer, msec);
	mod_timer(timer, jiffies + msecs_to_jiffies(msec) + 1);
}

/**
 * k_cancel_timer - cancel a timer
 * @timer: pointer to timer structure
 * 
 * Cancels a previously initialized timer.  
 * Can be called safely even if the timer is already inactive.
 * 
 * WARNING: Must not be called when holding locks required by the timer's
 *          timeout routine, otherwise deadlock can occur on SMP systems!
 */

static inline void k_cancel_timer(struct timer_list *timer)
{
	dbg("cancelling timer %p\n", timer);
	del_timer_sync(timer);
}

/**
 * k_term_timer - terminate a timer
 * @timer: pointer to timer structure
 * 
 * Prevents further use of a previously initialized timer.
 * 
 * WARNING: Caller must ensure timer isn't currently running.
 * 
 * (Do not "enhance" this routine to automatically cancel an active timer,
 * otherwise deadlock can arise when a timeout routine calls k_term_timer.)
 */

static inline void k_term_timer(struct timer_list *timer)
{
	dbg("terminating timer %p\n", timer);
}


/*
 * TIPC message buffer code
 *
 * TIPC message buffer headroom leaves room for 14 byte Ethernet header, 
 * while ensuring TIPC header is word aligned for quicker access
 */

#define BUF_HEADROOM 16u 

struct tipc_skb_cb {
	void *handle;
};

#define TIPC_SKB_CB(__skb) ((struct tipc_skb_cb *)&((__skb)->cb[0]))


static inline struct tipc_msg *buf_msg(struct sk_buff *skb)
{
	return (struct tipc_msg *)skb->data;
}

/**
 * buf_acquire - creates a TIPC message buffer
 * @size: message size (including TIPC header)
 *
 * Returns a new buffer.  Space is reserved for a data link header.
 */

static inline struct sk_buff *buf_acquire(u32 size)
{
	struct sk_buff *skb;
	unsigned int buf_size = (BUF_HEADROOM + size + 3) & ~3u;

	skb = alloc_skb(buf_size, GFP_ATOMIC);
	if (skb) {
		skb_reserve(skb, BUF_HEADROOM);
		skb_put(skb, size);
		skb->next = NULL;
	}
	return skb;
}

/**
 * buf_discard - frees a TIPC message buffer
 * @skb: message buffer
 *
 * Frees a new buffer.  If passed NULL, just returns.
 */

static inline void buf_discard(struct sk_buff *skb)
{
	if (likely(skb != NULL))
		kfree_skb(skb);
}

#endif			
