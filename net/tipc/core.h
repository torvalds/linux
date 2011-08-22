/*
 * net/tipc/core.h: Include file for TIPC global declarations
 *
 * Copyright (c) 2005-2006, Ericsson AB
 * Copyright (c) 2005-2007, 2010-2011, Wind River Systems
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

#include <linux/tipc.h>
#include <linux/tipc_config.h>
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
#include <linux/slab.h>
#include <linux/vmalloc.h>


#define TIPC_MOD_VER "2.0.0"

struct tipc_msg;	/* msg.h */
struct print_buf;	/* log.h */

/*
 * TIPC system monitoring code
 */

/*
 * TIPC's print buffer subsystem supports the following print buffers:
 *
 * TIPC_NULL : null buffer (i.e. print nowhere)
 * TIPC_CONS : system console
 * TIPC_LOG  : TIPC log buffer
 * &buf	     : user-defined buffer (struct print_buf *)
 *
 * Note: TIPC_LOG is configured to echo its output to the system console;
 *       user-defined buffers can be configured to do the same thing.
 */
extern struct print_buf *const TIPC_NULL;
extern struct print_buf *const TIPC_CONS;
extern struct print_buf *const TIPC_LOG;

void tipc_printf(struct print_buf *, const char *fmt, ...);

/*
 * TIPC_OUTPUT is the destination print buffer for system messages.
 */

#ifndef TIPC_OUTPUT
#define TIPC_OUTPUT TIPC_LOG
#endif

#define err(fmt, arg...)  tipc_printf(TIPC_OUTPUT, \
				      KERN_ERR "TIPC: " fmt, ## arg)
#define warn(fmt, arg...) tipc_printf(TIPC_OUTPUT, \
				      KERN_WARNING "TIPC: " fmt, ## arg)
#define info(fmt, arg...) tipc_printf(TIPC_OUTPUT, \
				      KERN_NOTICE "TIPC: " fmt, ## arg)

#ifdef CONFIG_TIPC_DEBUG

/*
 * DBG_OUTPUT is the destination print buffer for debug messages.
 */

#ifndef DBG_OUTPUT
#define DBG_OUTPUT TIPC_LOG
#endif

#define dbg(fmt, arg...)  tipc_printf(DBG_OUTPUT, KERN_DEBUG fmt, ## arg);

#define msg_dbg(msg, txt) tipc_msg_dbg(DBG_OUTPUT, msg, txt);

void tipc_msg_dbg(struct print_buf *, struct tipc_msg *, const char *);

#else

#define dbg(fmt, arg...)	do {} while (0)
#define msg_dbg(msg, txt)	do {} while (0)

#define tipc_msg_dbg(buf, msg, txt) do {} while (0)

#endif


/*
 * TIPC-specific error codes
 */

#define ELINKCONG EAGAIN	/* link congestion <=> resource unavailable */

/*
 * TIPC operating mode routines
 */
#define TIPC_NOT_RUNNING  0
#define TIPC_NODE_MODE    1
#define TIPC_NET_MODE     2

/*
 * Global configuration variables
 */

extern u32 tipc_own_addr;
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


/*
 * Routines available to privileged subsystems
 */

extern int tipc_core_start_net(unsigned long);
extern int  tipc_handler_start(void);
extern void tipc_handler_stop(void);
extern int  tipc_netlink_start(void);
extern void tipc_netlink_stop(void);
extern int  tipc_socket_init(void);
extern void tipc_socket_stop(void);

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

u32 tipc_k_signal(Handler routine, unsigned long argument);

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
	setup_timer(timer, routine, argument);
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
}


/*
 * TIPC message buffer code
 *
 * TIPC message buffer headroom reserves space for the worst-case
 * link-level device header (in case the message is sent off-node).
 *
 * Note: Headroom should be a multiple of 4 to ensure the TIPC header fields
 *       are word aligned for quicker access
 */

#define BUF_HEADROOM LL_MAX_HEADER

struct tipc_skb_cb {
	void *handle;
};

#define TIPC_SKB_CB(__skb) ((struct tipc_skb_cb *)&((__skb)->cb[0]))


static inline struct tipc_msg *buf_msg(struct sk_buff *skb)
{
	return (struct tipc_msg *)skb->data;
}

extern struct sk_buff *tipc_buf_acquire(u32 size);

/**
 * buf_discard - frees a TIPC message buffer
 * @skb: message buffer
 *
 * Frees a message buffer.  If passed NULL, just returns.
 */

static inline void buf_discard(struct sk_buff *skb)
{
	kfree_skb(skb);
}

/**
 * buf_linearize - convert a TIPC message buffer into a single contiguous piece
 * @skb: message buffer
 *
 * Returns 0 on success.
 */

static inline int buf_linearize(struct sk_buff *skb)
{
	return skb_linearize(skb);
}

#endif
