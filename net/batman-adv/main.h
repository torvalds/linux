/* Copyright (C) 2007-2012 B.A.T.M.A.N. contributors:
 *
 * Marek Lindner, Simon Wunderlich
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 */

#ifndef _NET_BATMAN_ADV_MAIN_H_
#define _NET_BATMAN_ADV_MAIN_H_

#define BATADV_DRIVER_AUTHOR "Marek Lindner <lindner_marek@yahoo.de>, " \
			     "Simon Wunderlich <siwu@hrz.tu-chemnitz.de>"
#define BATADV_DRIVER_DESC   "B.A.T.M.A.N. advanced"
#define BATADV_DRIVER_DEVICE "batman-adv"

#ifndef BATADV_SOURCE_VERSION
#define BATADV_SOURCE_VERSION "2012.4.0"
#endif

/* B.A.T.M.A.N. parameters */

#define BATADV_TQ_MAX_VALUE 255
#define BATADV_JITTER 20

/* Time To Live of broadcast messages */
#define BATADV_TTL 50

/* purge originators after time in seconds if no valid packet comes in
 * -> TODO: check influence on BATADV_TQ_LOCAL_WINDOW_SIZE
 */
#define BATADV_PURGE_TIMEOUT 200000 /* 200 seconds */
#define BATADV_TT_LOCAL_TIMEOUT 3600000 /* in milliseconds */
#define BATADV_TT_CLIENT_ROAM_TIMEOUT 600000 /* in milliseconds */
#define BATADV_TT_CLIENT_TEMP_TIMEOUT 600000 /* in milliseconds */
/* sliding packet range of received originator messages in sequence numbers
 * (should be a multiple of our word size)
 */
#define BATADV_TQ_LOCAL_WINDOW_SIZE 64
/* milliseconds we have to keep pending tt_req */
#define BATADV_TT_REQUEST_TIMEOUT 3000

#define BATADV_TQ_GLOBAL_WINDOW_SIZE 5
#define BATADV_TQ_LOCAL_BIDRECT_SEND_MINIMUM 1
#define BATADV_TQ_LOCAL_BIDRECT_RECV_MINIMUM 1
#define BATADV_TQ_TOTAL_BIDRECT_LIMIT 1

/* number of OGMs sent with the last tt diff */
#define BATADV_TT_OGM_APPEND_MAX 3

/* Time in which a client can roam at most ROAMING_MAX_COUNT times in
 * milliseconds
 */
#define BATADV_ROAMING_MAX_TIME 20000
#define BATADV_ROAMING_MAX_COUNT 5

#define BATADV_NO_FLAGS 0

#define BATADV_NULL_IFINDEX 0 /* dummy ifindex used to avoid iface checks */

#define BATADV_NUM_WORDS BITS_TO_LONGS(BATADV_TQ_LOCAL_WINDOW_SIZE)

#define BATADV_LOG_BUF_LEN 8192	  /* has to be a power of 2 */

#define BATADV_VIS_INTERVAL 5000	/* 5 seconds */

/* how much worse secondary interfaces may be to be considered as bonding
 * candidates
 */
#define BATADV_BONDING_TQ_THRESHOLD	50

/* should not be bigger than 512 bytes or change the size of
 * forw_packet->direct_link_flags
 */
#define BATADV_MAX_AGGREGATION_BYTES 512
#define BATADV_MAX_AGGREGATION_MS 100

#define BATADV_BLA_PERIOD_LENGTH	10000	/* 10 seconds */
#define BATADV_BLA_BACKBONE_TIMEOUT	(BATADV_BLA_PERIOD_LENGTH * 3)
#define BATADV_BLA_CLAIM_TIMEOUT	(BATADV_BLA_PERIOD_LENGTH * 10)

#define BATADV_DUPLIST_SIZE		16
#define BATADV_DUPLIST_TIMEOUT		500	/* 500 ms */
/* don't reset again within 30 seconds */
#define BATADV_RESET_PROTECTION_MS 30000
#define BATADV_EXPECTED_SEQNO_RANGE	65536

enum batadv_mesh_state {
	BATADV_MESH_INACTIVE,
	BATADV_MESH_ACTIVE,
	BATADV_MESH_DEACTIVATING,
};

#define BATADV_BCAST_QUEUE_LEN		256
#define BATADV_BATMAN_QUEUE_LEN	256

enum batadv_uev_action {
	BATADV_UEV_ADD = 0,
	BATADV_UEV_DEL,
	BATADV_UEV_CHANGE,
};

enum batadv_uev_type {
	BATADV_UEV_GW = 0,
};

#define BATADV_GW_THRESHOLD	50

/* Debug Messages */
#ifdef pr_fmt
#undef pr_fmt
#endif
/* Append 'batman-adv: ' before kernel messages */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

/* Kernel headers */

#include <linux/mutex.h>	/* mutex */
#include <linux/module.h>	/* needed by all modules */
#include <linux/netdevice.h>	/* netdevice */
#include <linux/etherdevice.h>  /* ethernet address classification */
#include <linux/if_ether.h>	/* ethernet header */
#include <linux/poll.h>		/* poll_table */
#include <linux/kthread.h>	/* kernel threads */
#include <linux/pkt_sched.h>	/* schedule types */
#include <linux/workqueue.h>	/* workqueue */
#include <linux/percpu.h>
#include <linux/slab.h>
#include <net/sock.h>		/* struct sock */
#include <linux/jiffies.h>
#include <linux/seq_file.h>
#include "types.h"

extern char batadv_routing_algo[];
extern struct list_head batadv_hardif_list;

extern unsigned char batadv_broadcast_addr[];
extern struct workqueue_struct *batadv_event_workqueue;

int batadv_mesh_init(struct net_device *soft_iface);
void batadv_mesh_free(struct net_device *soft_iface);
void batadv_inc_module_count(void);
void batadv_dec_module_count(void);
int batadv_is_my_mac(const uint8_t *addr);
int batadv_batman_skb_recv(struct sk_buff *skb, struct net_device *dev,
			   struct packet_type *ptype,
			   struct net_device *orig_dev);
int
batadv_recv_handler_register(uint8_t packet_type,
			     int (*recv_handler)(struct sk_buff *,
						 struct batadv_hard_iface *));
void batadv_recv_handler_unregister(uint8_t packet_type);
int batadv_algo_register(struct batadv_algo_ops *bat_algo_ops);
int batadv_algo_select(struct batadv_priv *bat_priv, char *name);
int batadv_algo_seq_print_text(struct seq_file *seq, void *offset);

/* all messages related to routing / flooding / broadcasting / etc */
enum batadv_dbg_level {
	BATADV_DBG_BATMAN = BIT(0),
	BATADV_DBG_ROUTES = BIT(1), /* route added / changed / deleted */
	BATADV_DBG_TT	  = BIT(2), /* translation table operations */
	BATADV_DBG_BLA    = BIT(3), /* bridge loop avoidance */
	BATADV_DBG_ALL    = 15,
};

#ifdef CONFIG_BATMAN_ADV_DEBUG
int batadv_debug_log(struct batadv_priv *bat_priv, const char *fmt, ...)
__printf(2, 3);

#define batadv_dbg(type, bat_priv, fmt, arg...)			\
	do {							\
		if (atomic_read(&bat_priv->log_level) & type)	\
			batadv_debug_log(bat_priv, fmt, ## arg);\
	}							\
	while (0)
#else /* !CONFIG_BATMAN_ADV_DEBUG */
__printf(3, 4)
static inline void batadv_dbg(int type __always_unused,
			      struct batadv_priv *bat_priv __always_unused,
			      const char *fmt __always_unused, ...)
{
}
#endif

#define batadv_info(net_dev, fmt, arg...)				\
	do {								\
		struct net_device *_netdev = (net_dev);                 \
		struct batadv_priv *_batpriv = netdev_priv(_netdev);    \
		batadv_dbg(BATADV_DBG_ALL, _batpriv, fmt, ## arg);	\
		pr_info("%s: " fmt, _netdev->name, ## arg);		\
	} while (0)
#define batadv_err(net_dev, fmt, arg...)				\
	do {								\
		struct net_device *_netdev = (net_dev);                 \
		struct batadv_priv *_batpriv = netdev_priv(_netdev);    \
		batadv_dbg(BATADV_DBG_ALL, _batpriv, fmt, ## arg);	\
		pr_err("%s: " fmt, _netdev->name, ## arg);		\
	} while (0)

/* returns 1 if they are the same ethernet addr
 *
 * note: can't use compare_ether_addr() as it requires aligned memory
 */
static inline int batadv_compare_eth(const void *data1, const void *data2)
{
	return (memcmp(data1, data2, ETH_ALEN) == 0 ? 1 : 0);
}

/**
 * has_timed_out - compares current time (jiffies) and timestamp + timeout
 * @timestamp:		base value to compare with (in jiffies)
 * @timeout:		added to base value before comparing (in milliseconds)
 *
 * Returns true if current time is after timestamp + timeout
 */
static inline bool batadv_has_timed_out(unsigned long timestamp,
					unsigned int timeout)
{
	return time_is_before_jiffies(timestamp + msecs_to_jiffies(timeout));
}

#define batadv_atomic_dec_not_zero(v)	atomic_add_unless((v), -1, 0)

/* Returns the smallest signed integer in two's complement with the sizeof x */
#define batadv_smallest_signed_int(x) (1u << (7u + 8u * (sizeof(x) - 1u)))

/* Checks if a sequence number x is a predecessor/successor of y.
 * they handle overflows/underflows and can correctly check for a
 * predecessor/successor unless the variable sequence number has grown by
 * more then 2**(bitwidth(x)-1)-1.
 * This means that for a uint8_t with the maximum value 255, it would think:
 *  - when adding nothing - it is neither a predecessor nor a successor
 *  - before adding more than 127 to the starting value - it is a predecessor,
 *  - when adding 128 - it is neither a predecessor nor a successor,
 *  - after adding more than 127 to the starting value - it is a successor
 */
#define batadv_seq_before(x, y) ({typeof(x) _d1 = (x); \
				 typeof(y) _d2 = (y); \
				 typeof(x) _dummy = (_d1 - _d2); \
				 (void) (&_d1 == &_d2); \
				 _dummy > batadv_smallest_signed_int(_dummy); })
#define batadv_seq_after(x, y) batadv_seq_before(y, x)

/* Stop preemption on local cpu while incrementing the counter */
static inline void batadv_add_counter(struct batadv_priv *bat_priv, size_t idx,
				      size_t count)
{
	int cpu = get_cpu();
	per_cpu_ptr(bat_priv->bat_counters, cpu)[idx] += count;
	put_cpu();
}

#define batadv_inc_counter(b, i) batadv_add_counter(b, i, 1)

/* Sum and return the cpu-local counters for index 'idx' */
static inline uint64_t batadv_sum_counter(struct batadv_priv *bat_priv,
					  size_t idx)
{
	uint64_t *counters, sum = 0;
	int cpu;

	for_each_possible_cpu(cpu) {
		counters = per_cpu_ptr(bat_priv->bat_counters, cpu);
		sum += counters[idx];
	}

	return sum;
}

#endif /* _NET_BATMAN_ADV_MAIN_H_ */
