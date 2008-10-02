/*
 * Copyright (c) 2008 open80211s Ltd.
 * Authors:    Luis Carlos Cobo <luisca@cozybit.com>
 * 	       Javier Cardona <javier@cozybit.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <asm/unaligned.h>
#include "ieee80211_i.h"
#include "mesh.h"

#define PP_OFFSET 	1		/* Path Selection Protocol */
#define PM_OFFSET	5		/* Path Selection Metric   */
#define CC_OFFSET	9		/* Congestion Control Mode */
#define CAPAB_OFFSET 17
#define ACCEPT_PLINKS 0x80

int mesh_allocated;
static struct kmem_cache *rm_cache;

void ieee80211s_init(void)
{
	mesh_pathtbl_init();
	mesh_allocated = 1;
	rm_cache = kmem_cache_create("mesh_rmc", sizeof(struct rmc_entry),
				     0, 0, NULL);
}

void ieee80211s_stop(void)
{
	mesh_pathtbl_unregister();
	kmem_cache_destroy(rm_cache);
}

/**
 * mesh_matches_local - check if the config of a mesh point matches ours
 *
 * @ie: information elements of a management frame from the mesh peer
 * @dev: local mesh interface
 *
 * This function checks if the mesh configuration of a mesh point matches the
 * local mesh configuration, i.e. if both nodes belong to the same mesh network.
 */
bool mesh_matches_local(struct ieee802_11_elems *ie, struct net_device *dev)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_if_sta *sta = &sdata->u.sta;

	/*
	 * As support for each feature is added, check for matching
	 * - On mesh config capabilities
	 *   - Power Save Support En
	 *   - Sync support enabled
	 *   - Sync support active
	 *   - Sync support required from peer
	 *   - MDA enabled
	 * - Power management control on fc
	 */
	if (sta->mesh_id_len == ie->mesh_id_len &&
		memcmp(sta->mesh_id, ie->mesh_id, ie->mesh_id_len) == 0 &&
		memcmp(sta->mesh_pp_id, ie->mesh_config + PP_OFFSET, 4) == 0 &&
		memcmp(sta->mesh_pm_id, ie->mesh_config + PM_OFFSET, 4) == 0 &&
		memcmp(sta->mesh_cc_id, ie->mesh_config + CC_OFFSET, 4) == 0)
		return true;

	return false;
}

/**
 * mesh_peer_accepts_plinks - check if an mp is willing to establish peer links
 *
 * @ie: information elements of a management frame from the mesh peer
 * @dev: local mesh interface
 */
bool mesh_peer_accepts_plinks(struct ieee802_11_elems *ie,
			      struct net_device *dev)
{
	return (*(ie->mesh_config + CAPAB_OFFSET) & ACCEPT_PLINKS) != 0;
}

/**
 * mesh_accept_plinks_update: update accepting_plink in local mesh beacons
 *
 * @sdata: mesh interface in which mesh beacons are going to be updated
 */
void mesh_accept_plinks_update(struct ieee80211_sub_if_data *sdata)
{
	bool free_plinks;

	/* In case mesh_plink_free_count > 0 and mesh_plinktbl_capacity == 0,
	 * the mesh interface might be able to establish plinks with peers that
	 * are already on the table but are not on PLINK_ESTAB state. However,
	 * in general the mesh interface is not accepting peer link requests
	 * from new peers, and that must be reflected in the beacon
	 */
	free_plinks = mesh_plink_availables(sdata);

	if (free_plinks != sdata->u.sta.accepting_plinks)
		ieee80211_sta_timer((unsigned long) sdata);
}

void mesh_ids_set_default(struct ieee80211_if_sta *sta)
{
	u8 def_id[4] = {0x00, 0x0F, 0xAC, 0xff};

	memcpy(sta->mesh_pp_id, def_id, 4);
	memcpy(sta->mesh_pm_id, def_id, 4);
	memcpy(sta->mesh_cc_id, def_id, 4);
}

int mesh_rmc_init(struct net_device *dev)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	int i;

	sdata->u.sta.rmc = kmalloc(sizeof(struct mesh_rmc), GFP_KERNEL);
	if (!sdata->u.sta.rmc)
		return -ENOMEM;
	sdata->u.sta.rmc->idx_mask = RMC_BUCKETS - 1;
	for (i = 0; i < RMC_BUCKETS; i++)
		INIT_LIST_HEAD(&sdata->u.sta.rmc->bucket[i].list);
	return 0;
}

void mesh_rmc_free(struct net_device *dev)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct mesh_rmc *rmc = sdata->u.sta.rmc;
	struct rmc_entry *p, *n;
	int i;

	if (!sdata->u.sta.rmc)
		return;

	for (i = 0; i < RMC_BUCKETS; i++)
		list_for_each_entry_safe(p, n, &rmc->bucket[i].list, list) {
			list_del(&p->list);
			kmem_cache_free(rm_cache, p);
		}

	kfree(rmc);
	sdata->u.sta.rmc = NULL;
}

/**
 * mesh_rmc_check - Check frame in recent multicast cache and add if absent.
 *
 * @sa:		source address
 * @mesh_hdr:	mesh_header
 *
 * Returns: 0 if the frame is not in the cache, nonzero otherwise.
 *
 * Checks using the source address and the mesh sequence number if we have
 * received this frame lately. If the frame is not in the cache, it is added to
 * it.
 */
int mesh_rmc_check(u8 *sa, struct ieee80211s_hdr *mesh_hdr,
		   struct net_device *dev)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct mesh_rmc *rmc = sdata->u.sta.rmc;
	u32 seqnum = 0;
	int entries = 0;
	u8 idx;
	struct rmc_entry *p, *n;

	/* Don't care about endianness since only match matters */
	memcpy(&seqnum, &mesh_hdr->seqnum, sizeof(mesh_hdr->seqnum));
	idx = le32_to_cpu(mesh_hdr->seqnum) & rmc->idx_mask;
	list_for_each_entry_safe(p, n, &rmc->bucket[idx].list, list) {
		++entries;
		if (time_after(jiffies, p->exp_time) ||
				(entries == RMC_QUEUE_MAX_LEN)) {
			list_del(&p->list);
			kmem_cache_free(rm_cache, p);
			--entries;
		} else if ((seqnum == p->seqnum)
				&& (memcmp(sa, p->sa, ETH_ALEN) == 0))
			return -1;
	}

	p = kmem_cache_alloc(rm_cache, GFP_ATOMIC);
	if (!p) {
		printk(KERN_DEBUG "o11s: could not allocate RMC entry\n");
		return 0;
	}
	p->seqnum = seqnum;
	p->exp_time = jiffies + RMC_TIMEOUT;
	memcpy(p->sa, sa, ETH_ALEN);
	list_add(&p->list, &rmc->bucket[idx].list);
	return 0;
}

void mesh_mgmt_ies_add(struct sk_buff *skb, struct net_device *dev)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_supported_band *sband;
	u8 *pos;
	int len, i, rate;

	sband = local->hw.wiphy->bands[local->hw.conf.channel->band];
	len = sband->n_bitrates;
	if (len > 8)
		len = 8;
	pos = skb_put(skb, len + 2);
	*pos++ = WLAN_EID_SUPP_RATES;
	*pos++ = len;
	for (i = 0; i < len; i++) {
		rate = sband->bitrates[i].bitrate;
		*pos++ = (u8) (rate / 5);
	}

	if (sband->n_bitrates > len) {
		pos = skb_put(skb, sband->n_bitrates - len + 2);
		*pos++ = WLAN_EID_EXT_SUPP_RATES;
		*pos++ = sband->n_bitrates - len;
		for (i = len; i < sband->n_bitrates; i++) {
			rate = sband->bitrates[i].bitrate;
			*pos++ = (u8) (rate / 5);
		}
	}

	pos = skb_put(skb, 2 + sdata->u.sta.mesh_id_len);
	*pos++ = WLAN_EID_MESH_ID;
	*pos++ = sdata->u.sta.mesh_id_len;
	if (sdata->u.sta.mesh_id_len)
		memcpy(pos, sdata->u.sta.mesh_id, sdata->u.sta.mesh_id_len);

	pos = skb_put(skb, 21);
	*pos++ = WLAN_EID_MESH_CONFIG;
	*pos++ = MESH_CFG_LEN;
	/* Version */
	*pos++ = 1;

	/* Active path selection protocol ID */
	memcpy(pos, sdata->u.sta.mesh_pp_id, 4);
	pos += 4;

	/* Active path selection metric ID   */
	memcpy(pos, sdata->u.sta.mesh_pm_id, 4);
	pos += 4;

	/* Congestion control mode identifier */
	memcpy(pos, sdata->u.sta.mesh_cc_id, 4);
	pos += 4;

	/* Channel precedence:
	 * Not running simple channel unification protocol
	 */
	memset(pos, 0x00, 4);
	pos += 4;

	/* Mesh capability */
	sdata->u.sta.accepting_plinks = mesh_plink_availables(sdata);
	*pos++ = sdata->u.sta.accepting_plinks ? ACCEPT_PLINKS : 0x00;
	*pos++ = 0x00;

	return;
}

u32 mesh_table_hash(u8 *addr, struct net_device *dev, struct mesh_table *tbl)
{
	/* Use last four bytes of hw addr and interface index as hash index */
	return jhash_2words(*(u32 *)(addr+2), dev->ifindex, tbl->hash_rnd)
		& tbl->hash_mask;
}

u8 mesh_id_hash(u8 *mesh_id, int mesh_id_len)
{
	if (!mesh_id_len)
		return 1;
	else if (mesh_id_len == 1)
		return (u8) mesh_id[0];
	else
		return (u8) (mesh_id[0] + 2 * mesh_id[1]);
}

struct mesh_table *mesh_table_alloc(int size_order)
{
	int i;
	struct mesh_table *newtbl;

	newtbl = kmalloc(sizeof(struct mesh_table), GFP_KERNEL);
	if (!newtbl)
		return NULL;

	newtbl->hash_buckets = kzalloc(sizeof(struct hlist_head) *
			(1 << size_order), GFP_KERNEL);

	if (!newtbl->hash_buckets) {
		kfree(newtbl);
		return NULL;
	}

	newtbl->hashwlock = kmalloc(sizeof(spinlock_t) *
			(1 << size_order), GFP_KERNEL);
	if (!newtbl->hashwlock) {
		kfree(newtbl->hash_buckets);
		kfree(newtbl);
		return NULL;
	}

	newtbl->size_order = size_order;
	newtbl->hash_mask = (1 << size_order) - 1;
	atomic_set(&newtbl->entries,  0);
	get_random_bytes(&newtbl->hash_rnd,
			sizeof(newtbl->hash_rnd));
	for (i = 0; i <= newtbl->hash_mask; i++)
		spin_lock_init(&newtbl->hashwlock[i]);

	return newtbl;
}

static void __mesh_table_free(struct mesh_table *tbl)
{
	kfree(tbl->hash_buckets);
	kfree(tbl->hashwlock);
	kfree(tbl);
}

void mesh_table_free(struct mesh_table *tbl, bool free_leafs)
{
	struct hlist_head *mesh_hash;
	struct hlist_node *p, *q;
	int i;

	mesh_hash = tbl->hash_buckets;
	for (i = 0; i <= tbl->hash_mask; i++) {
		spin_lock(&tbl->hashwlock[i]);
		hlist_for_each_safe(p, q, &mesh_hash[i]) {
			tbl->free_node(p, free_leafs);
			atomic_dec(&tbl->entries);
		}
		spin_unlock(&tbl->hashwlock[i]);
	}
	__mesh_table_free(tbl);
}

static void ieee80211_mesh_path_timer(unsigned long data)
{
	struct ieee80211_sub_if_data *sdata =
		(struct ieee80211_sub_if_data *) data;
	struct ieee80211_if_sta *ifsta = &sdata->u.sta;
	struct ieee80211_local *local = wdev_priv(&sdata->wdev);

	queue_work(local->hw.workqueue, &ifsta->work);
}

struct mesh_table *mesh_table_grow(struct mesh_table *tbl)
{
	struct mesh_table *newtbl;
	struct hlist_head *oldhash;
	struct hlist_node *p, *q;
	int i;

	if (atomic_read(&tbl->entries)
			< tbl->mean_chain_len * (tbl->hash_mask + 1))
		goto endgrow;

	newtbl = mesh_table_alloc(tbl->size_order + 1);
	if (!newtbl)
		goto endgrow;

	newtbl->free_node = tbl->free_node;
	newtbl->mean_chain_len = tbl->mean_chain_len;
	newtbl->copy_node = tbl->copy_node;
	atomic_set(&newtbl->entries, atomic_read(&tbl->entries));

	oldhash = tbl->hash_buckets;
	for (i = 0; i <= tbl->hash_mask; i++)
		hlist_for_each(p, &oldhash[i])
			if (tbl->copy_node(p, newtbl) < 0)
				goto errcopy;

	return newtbl;

errcopy:
	for (i = 0; i <= newtbl->hash_mask; i++) {
		hlist_for_each_safe(p, q, &newtbl->hash_buckets[i])
			tbl->free_node(p, 0);
	}
	__mesh_table_free(newtbl);
endgrow:
	return NULL;
}

/**
 * ieee80211_new_mesh_header - create a new mesh header
 * @meshhdr:    uninitialized mesh header
 * @sdata:	mesh interface to be used
 *
 * Return the header length.
 */
int ieee80211_new_mesh_header(struct ieee80211s_hdr *meshhdr,
		struct ieee80211_sub_if_data *sdata)
{
	meshhdr->flags = 0;
	meshhdr->ttl = sdata->u.sta.mshcfg.dot11MeshTTL;
	put_unaligned(cpu_to_le32(sdata->u.sta.mesh_seqnum), &meshhdr->seqnum);
	sdata->u.sta.mesh_seqnum++;

	return 6;
}

void ieee80211_mesh_init_sdata(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_sta *ifsta = &sdata->u.sta;

	ifsta->mshcfg.dot11MeshRetryTimeout = MESH_RET_T;
	ifsta->mshcfg.dot11MeshConfirmTimeout = MESH_CONF_T;
	ifsta->mshcfg.dot11MeshHoldingTimeout = MESH_HOLD_T;
	ifsta->mshcfg.dot11MeshMaxRetries = MESH_MAX_RETR;
	ifsta->mshcfg.dot11MeshTTL = MESH_TTL;
	ifsta->mshcfg.auto_open_plinks = true;
	ifsta->mshcfg.dot11MeshMaxPeerLinks =
		MESH_MAX_ESTAB_PLINKS;
	ifsta->mshcfg.dot11MeshHWMPactivePathTimeout =
		MESH_PATH_TIMEOUT;
	ifsta->mshcfg.dot11MeshHWMPpreqMinInterval =
		MESH_PREQ_MIN_INT;
	ifsta->mshcfg.dot11MeshHWMPnetDiameterTraversalTime =
		MESH_DIAM_TRAVERSAL_TIME;
	ifsta->mshcfg.dot11MeshHWMPmaxPREQretries =
		MESH_MAX_PREQ_RETRIES;
	ifsta->mshcfg.path_refresh_time =
		MESH_PATH_REFRESH_TIME;
	ifsta->mshcfg.min_discovery_timeout =
		MESH_MIN_DISCOVERY_TIMEOUT;
	ifsta->accepting_plinks = true;
	ifsta->preq_id = 0;
	ifsta->dsn = 0;
	atomic_set(&ifsta->mpaths, 0);
	mesh_rmc_init(sdata->dev);
	ifsta->last_preq = jiffies;
	/* Allocate all mesh structures when creating the first mesh interface. */
	if (!mesh_allocated)
		ieee80211s_init();
	mesh_ids_set_default(ifsta);
	setup_timer(&ifsta->mesh_path_timer,
		    ieee80211_mesh_path_timer,
		    (unsigned long) sdata);
	INIT_LIST_HEAD(&ifsta->preq_queue.list);
	spin_lock_init(&ifsta->mesh_preq_queue_lock);
}
