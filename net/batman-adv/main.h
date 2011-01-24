/*
 * Copyright (C) 2007-2010 B.A.T.M.A.N. contributors:
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
 *
 */

#ifndef _NET_BATMAN_ADV_MAIN_H_
#define _NET_BATMAN_ADV_MAIN_H_

/* Kernel Programming */
#define LINUX

#define DRIVER_AUTHOR "Marek Lindner <lindner_marek@yahoo.de>, " \
		      "Simon Wunderlich <siwu@hrz.tu-chemnitz.de>"
#define DRIVER_DESC   "B.A.T.M.A.N. advanced"
#define DRIVER_DEVICE "batman-adv"

#define SOURCE_VERSION "next"


/* B.A.T.M.A.N. parameters */

#define TQ_MAX_VALUE 255
#define JITTER 20
#define TTL 50			  /* Time To Live of broadcast messages */

#define PURGE_TIMEOUT 200	/* purge originators after time in seconds if no
				   * valid packet comes in -> TODO: check
				   * influence on TQ_LOCAL_WINDOW_SIZE */
#define LOCAL_HNA_TIMEOUT 3600 /* in seconds */

#define TQ_LOCAL_WINDOW_SIZE 64	  /* sliding packet range of received originator
				   * messages in squence numbers (should be a
				   * multiple of our word size) */
#define TQ_GLOBAL_WINDOW_SIZE 5
#define TQ_LOCAL_BIDRECT_SEND_MINIMUM 1
#define TQ_LOCAL_BIDRECT_RECV_MINIMUM 1
#define TQ_TOTAL_BIDRECT_LIMIT 1

#define NUM_WORDS (TQ_LOCAL_WINDOW_SIZE / WORD_BIT_SIZE)

#define PACKBUFF_SIZE 2000
#define LOG_BUF_LEN 8192	  /* has to be a power of 2 */

#define VIS_INTERVAL 5000	/* 5 seconds */

/* how much worse secondary interfaces may be to
 * to be considered as bonding candidates */

#define BONDING_TQ_THRESHOLD	50

#define MAX_AGGREGATION_BYTES 512 /* should not be bigger than 512 bytes or
				   * change the size of
				   * forw_packet->direct_link_flags */
#define MAX_AGGREGATION_MS 100

#define SOFTIF_NEIGH_TIMEOUT 180000 /* 3 minutes */

#define RESET_PROTECTION_MS 30000
#define EXPECTED_SEQNO_RANGE	65536
/* don't reset again within 30 seconds */

#define MESH_INACTIVE 0
#define MESH_ACTIVE 1
#define MESH_DEACTIVATING 2

#define BCAST_QUEUE_LEN		256
#define BATMAN_QUEUE_LEN	256

/*
 * Debug Messages
 */
#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt /* Append 'batman-adv: ' before
					     * kernel messages */

#define DBG_BATMAN 1	/* all messages related to routing / flooding /
			 * broadcasting / etc */
#define DBG_ROUTES 2	/* route or hna added / changed / deleted */
#define DBG_ALL 3

#define LOG_BUF_LEN 8192          /* has to be a power of 2 */


/*
 *  Vis
 */

/* #define VIS_SUBCLUSTERS_DISABLED */

/*
 * Kernel headers
 */

#include <linux/mutex.h>	/* mutex */
#include <linux/module.h>	/* needed by all modules */
#include <linux/netdevice.h>	/* netdevice */
#include <linux/etherdevice.h>  /* ethernet address classifaction */
#include <linux/if_ether.h>	/* ethernet header */
#include <linux/poll.h>		/* poll_table */
#include <linux/kthread.h>	/* kernel threads */
#include <linux/pkt_sched.h>	/* schedule types */
#include <linux/workqueue.h>	/* workqueue */
#include <linux/slab.h>
#include <net/sock.h>		/* struct sock */
#include <linux/jiffies.h>
#include <linux/seq_file.h>
#include "types.h"

#ifndef REVISION_VERSION
#define REVISION_VERSION_STR ""
#else
#define REVISION_VERSION_STR " "REVISION_VERSION
#endif

extern struct list_head if_list;

extern unsigned char broadcast_addr[];
extern struct workqueue_struct *bat_event_workqueue;

int mesh_init(struct net_device *soft_iface);
void mesh_free(struct net_device *soft_iface);
void inc_module_count(void);
void dec_module_count(void);
int is_my_mac(uint8_t *addr);

#ifdef CONFIG_BATMAN_ADV_DEBUG
int debug_log(struct bat_priv *bat_priv, char *fmt, ...);

#define bat_dbg(type, bat_priv, fmt, arg...)			\
	do {							\
		if (atomic_read(&bat_priv->log_level) & type)	\
			debug_log(bat_priv, fmt, ## arg);	\
	}							\
	while (0)
#else /* !CONFIG_BATMAN_ADV_DEBUG */
static inline void bat_dbg(char type __always_unused,
			   struct bat_priv *bat_priv __always_unused,
			   char *fmt __always_unused, ...)
{
}
#endif

#define bat_warning(net_dev, fmt, arg...)				\
	do {								\
		struct net_device *_netdev = (net_dev);                 \
		struct bat_priv *_batpriv = netdev_priv(_netdev);       \
		bat_dbg(DBG_ALL, _batpriv, fmt, ## arg);		\
		pr_warning("%s: " fmt, _netdev->name, ## arg);		\
	} while (0)
#define bat_info(net_dev, fmt, arg...)					\
	do {								\
		struct net_device *_netdev = (net_dev);                 \
		struct bat_priv *_batpriv = netdev_priv(_netdev);       \
		bat_dbg(DBG_ALL, _batpriv, fmt, ## arg);		\
		pr_info("%s: " fmt, _netdev->name, ## arg);		\
	} while (0)
#define bat_err(net_dev, fmt, arg...)					\
	do {								\
		struct net_device *_netdev = (net_dev);                 \
		struct bat_priv *_batpriv = netdev_priv(_netdev);       \
		bat_dbg(DBG_ALL, _batpriv, fmt, ## arg);		\
		pr_err("%s: " fmt, _netdev->name, ## arg);		\
	} while (0)

#endif /* _NET_BATMAN_ADV_MAIN_H_ */
