/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MPOA_CACHES_H
#define MPOA_CACHES_H

#include <linux/netdevice.h>
#include <linux/types.h>
#include <linux/atm.h>
#include <linux/atmdev.h>
#include <linux/atmmpc.h>
#include <linux/refcount.h>

struct mpoa_client;

void atm_mpoa_init_cache(struct mpoa_client *mpc);

typedef struct in_cache_entry {
	struct in_cache_entry *next;
	struct in_cache_entry *prev;
	struct timeval  tv;
	struct timeval  reply_wait;
	struct timeval  hold_down;
	uint32_t  packets_fwded;
	uint16_t  entry_state;
	uint32_t retry_time;
	uint32_t refresh_time;
	uint32_t count;
	struct   atm_vcc *shortcut;
	uint8_t  MPS_ctrl_ATM_addr[ATM_ESA_LEN];
	struct   in_ctrl_info ctrl_info;
	refcount_t use;
} in_cache_entry;

struct in_cache_ops{
	in_cache_entry *(*add_entry)(__be32 dst_ip,
				      struct mpoa_client *client);
	in_cache_entry *(*get)(__be32 dst_ip, struct mpoa_client *client);
	in_cache_entry *(*get_with_mask)(__be32 dst_ip,
					 struct mpoa_client *client,
					 __be32 mask);
	in_cache_entry *(*get_by_vcc)(struct atm_vcc *vcc,
				      struct mpoa_client *client);
	void            (*put)(in_cache_entry *entry);
	void            (*remove_entry)(in_cache_entry *delEntry,
					struct mpoa_client *client );
	int             (*cache_hit)(in_cache_entry *entry,
				     struct mpoa_client *client);
	void            (*clear_count)(struct mpoa_client *client);
	void            (*check_resolving)(struct mpoa_client *client);
	void            (*refresh)(struct mpoa_client *client);
	void            (*destroy_cache)(struct mpoa_client *mpc);
};

typedef struct eg_cache_entry{
	struct               eg_cache_entry *next;
	struct               eg_cache_entry *prev;
	struct               timeval  tv;
	uint8_t              MPS_ctrl_ATM_addr[ATM_ESA_LEN];
	struct atm_vcc       *shortcut;
	uint32_t             packets_rcvd;
	uint16_t             entry_state;
	__be32             latest_ip_addr;    /* The src IP address of the last packet */
	struct eg_ctrl_info  ctrl_info;
	refcount_t             use;
} eg_cache_entry;

struct eg_cache_ops{
	eg_cache_entry *(*add_entry)(struct k_message *msg, struct mpoa_client *client);
	eg_cache_entry *(*get_by_cache_id)(__be32 cache_id, struct mpoa_client *client);
	eg_cache_entry *(*get_by_tag)(__be32 cache_id, struct mpoa_client *client);
	eg_cache_entry *(*get_by_vcc)(struct atm_vcc *vcc, struct mpoa_client *client);
	eg_cache_entry *(*get_by_src_ip)(__be32 ipaddr, struct mpoa_client *client);
	void            (*put)(eg_cache_entry *entry);
	void            (*remove_entry)(eg_cache_entry *entry, struct mpoa_client *client);
	void            (*update)(eg_cache_entry *entry, uint16_t holding_time);
	void            (*clear_expired)(struct mpoa_client *client);
	void            (*destroy_cache)(struct mpoa_client *mpc);
};


/* Ingress cache entry states */

#define INGRESS_REFRESHING 3
#define INGRESS_RESOLVED   2
#define INGRESS_RESOLVING  1
#define INGRESS_INVALID    0

/* VCC states */

#define OPEN   1
#define CLOSED 0

/* Egress cache entry states */

#define EGRESS_RESOLVED 2
#define EGRESS_PURGE    1
#define EGRESS_INVALID  0

#endif
