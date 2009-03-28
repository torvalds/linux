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

#define IEEE80211_MESH_PEER_INACTIVITY_LIMIT (1800 * HZ)
#define IEEE80211_MESH_HOUSEKEEPING_INTERVAL (60 * HZ)

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

static void ieee80211_mesh_housekeeping_timer(unsigned long data)
{
	struct ieee80211_sub_if_data *sdata = (void *) data;
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_if_mesh *ifmsh = &sdata->u.mesh;

	ifmsh->housekeeping = true;
	queue_work(local->hw.workqueue, &ifmsh->work);
}

/**
 * mesh_matches_local - check if the config of a mesh point matches ours
 *
 * @ie: information elements of a management frame from the mesh peer
 * @sdata: local mesh subif
 *
 * This function checks if the mesh configuration of a mesh point matches the
 * local mesh configuration, i.e. if both nodes belong to the same mesh network.
 */
bool mesh_matches_local(struct ieee802_11_elems *ie, struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_mesh *ifmsh = &sdata->u.mesh;

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
	if (ifmsh->mesh_id_len == ie->mesh_id_len &&
		memcmp(ifmsh->mesh_id, ie->mesh_id, ie->mesh_id_len) == 0 &&
		memcmp(ifmsh->mesh_pp_id, ie->mesh_config + PP_OFFSET, 4) == 0 &&
		memcmp(ifmsh->mesh_pm_id, ie->mesh_config + PM_OFFSET, 4) == 0 &&
		memcmp(ifmsh->mesh_cc_id, ie->mesh_config + CC_OFFSET, 4) == 0)
		return true;

	return false;
}

/**
 * mesh_peer_accepts_plinks - check if an mp is willing to establish peer links
 *
 * @ie: information elements of a management frame from the mesh peer
 */
bool mesh_peer_accepts_plinks(struct ieee802_11_elems *ie)
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

	if (free_plinks != sdata->u.mesh.accepting_plinks)
		ieee80211_mesh_housekeeping_timer((unsigned long) sdata);
}

void mesh_ids_set_default(struct ieee80211_if_mesh *sta)
{
	u8 def_id[4] = {0x00, 0x0F, 0xAC, 0xff};

	memcpy(sta->mesh_pp_id, def_id, 4);
	memcpy(sta->mesh_pm_id, def_id, 4);
	memcpy(sta->mesh_cc_id, def_id, 4);
}

int mesh_rmc_init(struct ieee80211_sub_if_data *sdata)
{
	int i;

	sdata->u.mesh.rmc = kmalloc(sizeof(struct mesh_rmc), GFP_KERNEL);
	if (!sdata->u.mesh.rmc)
		return -ENOMEM;
	sdata->u.mesh.rmc->idx_mask = RMC_BUCKETS - 1;
	for (i = 0; i < RMC_BUCKETS; i++)
		INIT_LIST_HEAD(&sdata->u.mesh.rmc->bucket[i].list);
	return 0;
}

void mesh_rmc_free(struct ieee80211_sub_if_data *sdata)
{
	struct mesh_rmc *rmc = sdata->u.mesh.rmc;
	struct rmc_entry *p, *n;
	int i;

	if (!sdata->u.mesh.rmc)
		return;

	for (i = 0; i < RMC_BUCKETS; i++)
		list_for_each_entry_safe(p, n, &rmc->bucket[i].list, list) {
			list_del(&p->list);
			kmem_cache_free(rm_cache, p);
		}

	kfree(rmc);
	sdata->u.mesh.rmc = NULL;
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
		   struct ieee80211_sub_if_data *sdata)
{
	struct mesh_rmc *rmc = sdata->u.mesh.rmc;
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

void mesh_mgmt_ies_add(struct sk_buff *skb, struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;
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

	pos = skb_put(skb, 2 + sdata->u.mesh.mesh_id_len);
	*pos++ = WLAN_EID_MESH_ID;
	*pos++ = sdata->u.mesh.mesh_id_len;
	if (sdata->u.mesh.mesh_id_len)
		memcpy(pos, sdata->u.mesh.mesh_id, sdata->u.mesh.mesh_id_len);

	pos = skb_put(skb, 21);
	*pos++ = WLAN_EID_MESH_CONFIG;
	*pos++ = IEEE80211_MESH_CONFIG_LEN;
	/* Version */
	*pos++ = 1;

	/* Active path selection protocol ID */
	memcpy(pos, sdata->u.mesh.mesh_pp_id, 4);
	pos += 4;

	/* Active path selection metric ID   */
	memcpy(pos, sdata->u.mesh.mesh_pm_id, 4);
	pos += 4;

	/* Congestion control mode identifier */
	memcpy(pos, sdata->u.mesh.mesh_cc_id, 4);
	pos += 4;

	/* Channel precedence:
	 * Not running simple channel unification protocol
	 */
	memset(pos, 0x00, 4);
	pos += 4;

	/* Mesh capability */
	sdata->u.mesh.accepting_plinks = mesh_plink_availables(sdata);
	*pos++ = sdata->u.mesh.accepting_plinks ? ACCEPT_PLINKS : 0x00;
	*pos++ = 0x00;

	return;
}

u32 mesh_table_hash(u8 *addr, struct ieee80211_sub_if_data *sdata, struct mesh_table *tbl)
{
	/* Use last four bytes of hw addr and interface index as hash index */
	return jhash_2words(*(u32 *)(addr+2), sdata->dev->ifindex, tbl->hash_rnd)
		& tbl->hash_mask;
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
	struct ieee80211_if_mesh *ifmsh = &sdata->u.mesh;
	struct ieee80211_local *local = sdata->local;

	queue_work(local->hw.workqueue, &ifmsh->work);
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
	meshhdr->ttl = sdata->u.mesh.mshcfg.dot11MeshTTL;
	put_unaligned(cpu_to_le32(sdata->u.mesh.mesh_seqnum), &meshhdr->seqnum);
	sdata->u.mesh.mesh_seqnum++;

	return 6;
}

static void ieee80211_mesh_housekeeping(struct ieee80211_sub_if_data *sdata,
			   struct ieee80211_if_mesh *ifmsh)
{
	bool free_plinks;

#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
	printk(KERN_DEBUG "%s: running mesh housekeeping\n",
	       sdata->dev->name);
#endif

	ieee80211_sta_expire(sdata, IEEE80211_MESH_PEER_INACTIVITY_LIMIT);
	mesh_path_expire(sdata);

	free_plinks = mesh_plink_availables(sdata);
	if (free_plinks != sdata->u.mesh.accepting_plinks)
		ieee80211_if_config(sdata, IEEE80211_IFCC_BEACON);

	ifmsh->housekeeping = false;
	mod_timer(&ifmsh->housekeeping_timer,
		  round_jiffies(jiffies + IEEE80211_MESH_HOUSEKEEPING_INTERVAL));
}


void ieee80211_start_mesh(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_mesh *ifmsh = &sdata->u.mesh;
	struct ieee80211_local *local = sdata->local;

	ifmsh->housekeeping = true;
	queue_work(local->hw.workqueue, &ifmsh->work);
	ieee80211_if_config(sdata, IEEE80211_IFCC_BEACON |
				   IEEE80211_IFCC_BEACON_ENABLED);
}

void ieee80211_stop_mesh(struct ieee80211_sub_if_data *sdata)
{
	del_timer_sync(&sdata->u.mesh.housekeeping_timer);
	/*
	 * If the timer fired while we waited for it, it will have
	 * requeued the work. Now the work will be running again
	 * but will not rearm the timer again because it checks
	 * whether the interface is running, which, at this point,
	 * it no longer is.
	 */
	cancel_work_sync(&sdata->u.mesh.work);

	/*
	 * When we get here, the interface is marked down.
	 * Call synchronize_rcu() to wait for the RX path
	 * should it be using the interface and enqueuing
	 * frames at this very time on another CPU.
	 */
	synchronize_rcu();
	skb_queue_purge(&sdata->u.mesh.skb_queue);
}

static void ieee80211_mesh_rx_bcn_presp(struct ieee80211_sub_if_data *sdata,
					u16 stype,
					struct ieee80211_mgmt *mgmt,
					size_t len,
					struct ieee80211_rx_status *rx_status)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee802_11_elems elems;
	struct ieee80211_channel *channel;
	u32 supp_rates = 0;
	size_t baselen;
	int freq;
	enum ieee80211_band band = rx_status->band;

	/* ignore ProbeResp to foreign address */
	if (stype == IEEE80211_STYPE_PROBE_RESP &&
	    compare_ether_addr(mgmt->da, sdata->dev->dev_addr))
		return;

	baselen = (u8 *) mgmt->u.probe_resp.variable - (u8 *) mgmt;
	if (baselen > len)
		return;

	ieee802_11_parse_elems(mgmt->u.probe_resp.variable, len - baselen,
			       &elems);

	if (elems.ds_params && elems.ds_params_len == 1)
		freq = ieee80211_channel_to_frequency(elems.ds_params[0]);
	else
		freq = rx_status->freq;

	channel = ieee80211_get_channel(local->hw.wiphy, freq);

	if (!channel || channel->flags & IEEE80211_CHAN_DISABLED)
		return;

	if (elems.mesh_id && elems.mesh_config &&
	    mesh_matches_local(&elems, sdata)) {
		supp_rates = ieee80211_sta_get_rates(local, &elems, band);

		mesh_neighbour_update(mgmt->sa, supp_rates, sdata,
				      mesh_peer_accepts_plinks(&elems));
	}
}

static void ieee80211_mesh_rx_mgmt_action(struct ieee80211_sub_if_data *sdata,
					  struct ieee80211_mgmt *mgmt,
					  size_t len,
					  struct ieee80211_rx_status *rx_status)
{
	switch (mgmt->u.action.category) {
	case PLINK_CATEGORY:
		mesh_rx_plink_frame(sdata, mgmt, len, rx_status);
		break;
	case MESH_PATH_SEL_CATEGORY:
		mesh_rx_path_sel_frame(sdata, mgmt, len);
		break;
	}
}

static void ieee80211_mesh_rx_queued_mgmt(struct ieee80211_sub_if_data *sdata,
					  struct sk_buff *skb)
{
	struct ieee80211_rx_status *rx_status;
	struct ieee80211_if_mesh *ifmsh;
	struct ieee80211_mgmt *mgmt;
	u16 stype;

	ifmsh = &sdata->u.mesh;

	rx_status = (struct ieee80211_rx_status *) skb->cb;
	mgmt = (struct ieee80211_mgmt *) skb->data;
	stype = le16_to_cpu(mgmt->frame_control) & IEEE80211_FCTL_STYPE;

	switch (stype) {
	case IEEE80211_STYPE_PROBE_RESP:
	case IEEE80211_STYPE_BEACON:
		ieee80211_mesh_rx_bcn_presp(sdata, stype, mgmt, skb->len,
					    rx_status);
		break;
	case IEEE80211_STYPE_ACTION:
		ieee80211_mesh_rx_mgmt_action(sdata, mgmt, skb->len, rx_status);
		break;
	}

	kfree_skb(skb);
}

static void ieee80211_mesh_work(struct work_struct *work)
{
	struct ieee80211_sub_if_data *sdata =
		container_of(work, struct ieee80211_sub_if_data, u.mesh.work);
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_if_mesh *ifmsh = &sdata->u.mesh;
	struct sk_buff *skb;

	if (!netif_running(sdata->dev))
		return;

	if (local->sw_scanning || local->hw_scanning)
		return;

	while ((skb = skb_dequeue(&ifmsh->skb_queue)))
		ieee80211_mesh_rx_queued_mgmt(sdata, skb);

	if (ifmsh->preq_queue_len &&
	    time_after(jiffies,
		       ifmsh->last_preq + msecs_to_jiffies(ifmsh->mshcfg.dot11MeshHWMPpreqMinInterval)))
		mesh_path_start_discovery(sdata);

	if (ifmsh->housekeeping)
		ieee80211_mesh_housekeeping(sdata, ifmsh);
}

void ieee80211_mesh_notify_scan_completed(struct ieee80211_local *local)
{
	struct ieee80211_sub_if_data *sdata;

	rcu_read_lock();
	list_for_each_entry_rcu(sdata, &local->interfaces, list)
		if (ieee80211_vif_is_mesh(&sdata->vif))
			queue_work(local->hw.workqueue, &sdata->u.mesh.work);
	rcu_read_unlock();
}

void ieee80211_mesh_init_sdata(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_mesh *ifmsh = &sdata->u.mesh;

	INIT_WORK(&ifmsh->work, ieee80211_mesh_work);
	setup_timer(&ifmsh->housekeeping_timer,
		    ieee80211_mesh_housekeeping_timer,
		    (unsigned long) sdata);
	skb_queue_head_init(&sdata->u.mesh.skb_queue);

	ifmsh->mshcfg.dot11MeshRetryTimeout = MESH_RET_T;
	ifmsh->mshcfg.dot11MeshConfirmTimeout = MESH_CONF_T;
	ifmsh->mshcfg.dot11MeshHoldingTimeout = MESH_HOLD_T;
	ifmsh->mshcfg.dot11MeshMaxRetries = MESH_MAX_RETR;
	ifmsh->mshcfg.dot11MeshTTL = MESH_TTL;
	ifmsh->mshcfg.auto_open_plinks = true;
	ifmsh->mshcfg.dot11MeshMaxPeerLinks =
		MESH_MAX_ESTAB_PLINKS;
	ifmsh->mshcfg.dot11MeshHWMPactivePathTimeout =
		MESH_PATH_TIMEOUT;
	ifmsh->mshcfg.dot11MeshHWMPpreqMinInterval =
		MESH_PREQ_MIN_INT;
	ifmsh->mshcfg.dot11MeshHWMPnetDiameterTraversalTime =
		MESH_DIAM_TRAVERSAL_TIME;
	ifmsh->mshcfg.dot11MeshHWMPmaxPREQretries =
		MESH_MAX_PREQ_RETRIES;
	ifmsh->mshcfg.path_refresh_time =
		MESH_PATH_REFRESH_TIME;
	ifmsh->mshcfg.min_discovery_timeout =
		MESH_MIN_DISCOVERY_TIMEOUT;
	ifmsh->accepting_plinks = true;
	ifmsh->preq_id = 0;
	ifmsh->dsn = 0;
	atomic_set(&ifmsh->mpaths, 0);
	mesh_rmc_init(sdata);
	ifmsh->last_preq = jiffies;
	/* Allocate all mesh structures when creating the first mesh interface. */
	if (!mesh_allocated)
		ieee80211s_init();
	mesh_ids_set_default(ifmsh);
	setup_timer(&ifmsh->mesh_path_timer,
		    ieee80211_mesh_path_timer,
		    (unsigned long) sdata);
	INIT_LIST_HEAD(&ifmsh->preq_queue.list);
	spin_lock_init(&ifmsh->mesh_preq_queue_lock);
}

ieee80211_rx_result
ieee80211_mesh_rx_mgmt(struct ieee80211_sub_if_data *sdata, struct sk_buff *skb,
		       struct ieee80211_rx_status *rx_status)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_if_mesh *ifmsh = &sdata->u.mesh;
	struct ieee80211_mgmt *mgmt;
	u16 fc;

	if (skb->len < 24)
		return RX_DROP_MONITOR;

	mgmt = (struct ieee80211_mgmt *) skb->data;
	fc = le16_to_cpu(mgmt->frame_control);

	switch (fc & IEEE80211_FCTL_STYPE) {
	case IEEE80211_STYPE_PROBE_RESP:
	case IEEE80211_STYPE_BEACON:
	case IEEE80211_STYPE_ACTION:
		memcpy(skb->cb, rx_status, sizeof(*rx_status));
		skb_queue_tail(&ifmsh->skb_queue, skb);
		queue_work(local->hw.workqueue, &ifmsh->work);
		return RX_QUEUED;
	}

	return RX_CONTINUE;
}
