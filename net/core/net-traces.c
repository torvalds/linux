// SPDX-License-Identifier: GPL-2.0
/*
 * consolidates trace point definitions
 *
 * Copyright (C) 2009 Neil Horman <nhorman@tuxdriver.com>
 */

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/string.h>
#include <linux/if_arp.h>
#include <linux/inetdevice.h>
#include <linux/inet.h>
#include <linux/interrupt.h>
#include <linux/export.h>
#include <linux/netpoll.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/rcupdate.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <linux/netlink.h>
#include <linux/net_dropmon.h>
#include <linux/slab.h>

#include <asm/unaligned.h>
#include <asm/bitops.h>

#define CREATE_TRACE_POINTS
#include <trace/events/skb.h>
#include <trace/events/net.h>
#include <trace/events/napi.h>
#include <trace/events/sock.h>
#include <trace/events/udp.h>
#include <trace/events/tcp.h>
#include <trace/events/fib.h>
#include <trace/events/qdisc.h>
#include <trace/events/bridge.h>
EXPORT_TRACEPOINT_SYMBOL_GPL(br_fdb_add);
EXPORT_TRACEPOINT_SYMBOL_GPL(br_fdb_external_learn_add);
EXPORT_TRACEPOINT_SYMBOL_GPL(fdb_delete);
EXPORT_TRACEPOINT_SYMBOL_GPL(br_fdb_update);

#if IS_ENABLED(CONFIG_PAGE_POOL)
#include <trace/events/page_pool.h>
#endif

#include <trace/events/neigh.h>
EXPORT_TRACEPOINT_SYMBOL_GPL(neigh_update);
EXPORT_TRACEPOINT_SYMBOL_GPL(neigh_update_done);
EXPORT_TRACEPOINT_SYMBOL_GPL(neigh_timer_handler);
EXPORT_TRACEPOINT_SYMBOL_GPL(neigh_event_send_done);
EXPORT_TRACEPOINT_SYMBOL_GPL(neigh_event_send_dead);
EXPORT_TRACEPOINT_SYMBOL_GPL(neigh_cleanup_and_release);

EXPORT_TRACEPOINT_SYMBOL_GPL(kfree_skb);

EXPORT_TRACEPOINT_SYMBOL_GPL(napi_poll);

EXPORT_TRACEPOINT_SYMBOL_GPL(tcp_send_reset);
EXPORT_TRACEPOINT_SYMBOL_GPL(tcp_bad_csum);
