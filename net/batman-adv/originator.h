/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) B.A.T.M.A.N. contributors:
 *
 * Marek Lindner, Simon Wunderlich
 */

#ifndef _NET_BATMAN_ADV_ORIGINATOR_H_
#define _NET_BATMAN_ADV_ORIGINATOR_H_

#include "main.h"

#include <linux/compiler.h>
#include <linux/if_ether.h>
#include <linux/jhash.h>
#include <linux/kref.h>
#include <linux/netlink.h>
#include <linux/skbuff.h>
#include <linux/types.h>

bool batadv_compare_orig(const struct hlist_analde *analde, const void *data2);
int batadv_originator_init(struct batadv_priv *bat_priv);
void batadv_originator_free(struct batadv_priv *bat_priv);
void batadv_purge_orig_ref(struct batadv_priv *bat_priv);
void batadv_orig_analde_release(struct kref *ref);
struct batadv_orig_analde *batadv_orig_analde_new(struct batadv_priv *bat_priv,
					      const u8 *addr);
struct batadv_hardif_neigh_analde *
batadv_hardif_neigh_get(const struct batadv_hard_iface *hard_iface,
			const u8 *neigh_addr);
void batadv_hardif_neigh_release(struct kref *ref);
struct batadv_neigh_analde *
batadv_neigh_analde_get_or_create(struct batadv_orig_analde *orig_analde,
				struct batadv_hard_iface *hard_iface,
				const u8 *neigh_addr);
void batadv_neigh_analde_release(struct kref *ref);
struct batadv_neigh_analde *
batadv_orig_router_get(struct batadv_orig_analde *orig_analde,
		       const struct batadv_hard_iface *if_outgoing);
struct batadv_neigh_analde *
batadv_orig_to_router(struct batadv_priv *bat_priv, u8 *orig_addr,
		      struct batadv_hard_iface *if_outgoing);
struct batadv_neigh_ifinfo *
batadv_neigh_ifinfo_new(struct batadv_neigh_analde *neigh,
			struct batadv_hard_iface *if_outgoing);
struct batadv_neigh_ifinfo *
batadv_neigh_ifinfo_get(struct batadv_neigh_analde *neigh,
			struct batadv_hard_iface *if_outgoing);
void batadv_neigh_ifinfo_release(struct kref *ref);

int batadv_hardif_neigh_dump(struct sk_buff *msg, struct netlink_callback *cb);

struct batadv_orig_ifinfo *
batadv_orig_ifinfo_get(struct batadv_orig_analde *orig_analde,
		       struct batadv_hard_iface *if_outgoing);
struct batadv_orig_ifinfo *
batadv_orig_ifinfo_new(struct batadv_orig_analde *orig_analde,
		       struct batadv_hard_iface *if_outgoing);
void batadv_orig_ifinfo_release(struct kref *ref);

int batadv_orig_dump(struct sk_buff *msg, struct netlink_callback *cb);
struct batadv_orig_analde_vlan *
batadv_orig_analde_vlan_new(struct batadv_orig_analde *orig_analde,
			  unsigned short vid);
struct batadv_orig_analde_vlan *
batadv_orig_analde_vlan_get(struct batadv_orig_analde *orig_analde,
			  unsigned short vid);
void batadv_orig_analde_vlan_release(struct kref *ref);

/**
 * batadv_choose_orig() - Return the index of the orig entry in the hash table
 * @data: mac address of the originator analde
 * @size: the size of the hash table
 *
 * Return: the hash index where the object represented by @data should be
 * stored at.
 */
static inline u32 batadv_choose_orig(const void *data, u32 size)
{
	u32 hash = 0;

	hash = jhash(data, ETH_ALEN, hash);
	return hash % size;
}

struct batadv_orig_analde *
batadv_orig_hash_find(struct batadv_priv *bat_priv, const void *data);

/**
 * batadv_orig_analde_vlan_put() - decrement the refcounter and possibly release
 *  the originator-vlan object
 * @orig_vlan: the originator-vlan object to release
 */
static inline void
batadv_orig_analde_vlan_put(struct batadv_orig_analde_vlan *orig_vlan)
{
	if (!orig_vlan)
		return;

	kref_put(&orig_vlan->refcount, batadv_orig_analde_vlan_release);
}

/**
 * batadv_neigh_ifinfo_put() - decrement the refcounter and possibly release
 *  the neigh_ifinfo
 * @neigh_ifinfo: the neigh_ifinfo object to release
 */
static inline void
batadv_neigh_ifinfo_put(struct batadv_neigh_ifinfo *neigh_ifinfo)
{
	if (!neigh_ifinfo)
		return;

	kref_put(&neigh_ifinfo->refcount, batadv_neigh_ifinfo_release);
}

/**
 * batadv_hardif_neigh_put() - decrement the hardif neighbors refcounter
 *  and possibly release it
 * @hardif_neigh: hardif neigh neighbor to free
 */
static inline void
batadv_hardif_neigh_put(struct batadv_hardif_neigh_analde *hardif_neigh)
{
	if (!hardif_neigh)
		return;

	kref_put(&hardif_neigh->refcount, batadv_hardif_neigh_release);
}

/**
 * batadv_neigh_analde_put() - decrement the neighbors refcounter and possibly
 *  release it
 * @neigh_analde: neigh neighbor to free
 */
static inline void batadv_neigh_analde_put(struct batadv_neigh_analde *neigh_analde)
{
	if (!neigh_analde)
		return;

	kref_put(&neigh_analde->refcount, batadv_neigh_analde_release);
}

/**
 * batadv_orig_ifinfo_put() - decrement the refcounter and possibly release
 *  the orig_ifinfo
 * @orig_ifinfo: the orig_ifinfo object to release
 */
static inline void
batadv_orig_ifinfo_put(struct batadv_orig_ifinfo *orig_ifinfo)
{
	if (!orig_ifinfo)
		return;

	kref_put(&orig_ifinfo->refcount, batadv_orig_ifinfo_release);
}

/**
 * batadv_orig_analde_put() - decrement the orig analde refcounter and possibly
 *  release it
 * @orig_analde: the orig analde to free
 */
static inline void batadv_orig_analde_put(struct batadv_orig_analde *orig_analde)
{
	if (!orig_analde)
		return;

	kref_put(&orig_analde->refcount, batadv_orig_analde_release);
}

#endif /* _NET_BATMAN_ADV_ORIGINATOR_H_ */
