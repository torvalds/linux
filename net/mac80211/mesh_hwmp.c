/*
 * Copyright (c) 2008, 2009 open80211s Ltd.
 * Author:     Luis Carlos Cobo <luisca@cozybit.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/slab.h>
#include <linux/etherdevice.h>
#include <asm/unaligned.h>
#include "wme.h"
#include "mesh.h"

#define TEST_FRAME_LEN	8192
#define MAX_METRIC	0xffffffff
#define ARITH_SHIFT	8

/* Number of frames buffered per destination for unresolved destinations */
#define MESH_FRAME_QUEUE_LEN	10
#define MAX_PREQ_QUEUE_LEN	64

/* Destination only */
#define MP_F_DO	0x1
/* Reply and forward */
#define MP_F_RF	0x2
/* Unknown Sequence Number */
#define MP_F_USN    0x01
/* Reason code Present */
#define MP_F_RCODE  0x02

static void mesh_queue_preq(struct mesh_path *, u8);

static inline u32 u32_field_get(u8 *preq_elem, int offset, bool ae)
{
	if (ae)
		offset += 6;
	return get_unaligned_le32(preq_elem + offset);
}

static inline u32 u16_field_get(u8 *preq_elem, int offset, bool ae)
{
	if (ae)
		offset += 6;
	return get_unaligned_le16(preq_elem + offset);
}

/* HWMP IE processing macros */
#define AE_F			(1<<6)
#define AE_F_SET(x)		(*x & AE_F)
#define PREQ_IE_FLAGS(x)	(*(x))
#define PREQ_IE_HOPCOUNT(x)	(*(x + 1))
#define PREQ_IE_TTL(x)		(*(x + 2))
#define PREQ_IE_PREQ_ID(x)	u32_field_get(x, 3, 0)
#define PREQ_IE_ORIG_ADDR(x)	(x + 7)
#define PREQ_IE_ORIG_SN(x)	u32_field_get(x, 13, 0)
#define PREQ_IE_LIFETIME(x)	u32_field_get(x, 17, AE_F_SET(x))
#define PREQ_IE_METRIC(x) 	u32_field_get(x, 21, AE_F_SET(x))
#define PREQ_IE_TARGET_F(x)	(*(AE_F_SET(x) ? x + 32 : x + 26))
#define PREQ_IE_TARGET_ADDR(x) 	(AE_F_SET(x) ? x + 33 : x + 27)
#define PREQ_IE_TARGET_SN(x) 	u32_field_get(x, 33, AE_F_SET(x))


#define PREP_IE_FLAGS(x)	PREQ_IE_FLAGS(x)
#define PREP_IE_HOPCOUNT(x)	PREQ_IE_HOPCOUNT(x)
#define PREP_IE_TTL(x)		PREQ_IE_TTL(x)
#define PREP_IE_ORIG_ADDR(x)	(AE_F_SET(x) ? x + 27 : x + 21)
#define PREP_IE_ORIG_SN(x)	u32_field_get(x, 27, AE_F_SET(x))
#define PREP_IE_LIFETIME(x)	u32_field_get(x, 13, AE_F_SET(x))
#define PREP_IE_METRIC(x)	u32_field_get(x, 17, AE_F_SET(x))
#define PREP_IE_TARGET_ADDR(x)	(x + 3)
#define PREP_IE_TARGET_SN(x)	u32_field_get(x, 9, 0)

#define PERR_IE_TTL(x)		(*(x))
#define PERR_IE_TARGET_FLAGS(x)	(*(x + 2))
#define PERR_IE_TARGET_ADDR(x)	(x + 3)
#define PERR_IE_TARGET_SN(x)	u32_field_get(x, 9, 0)
#define PERR_IE_TARGET_RCODE(x)	u16_field_get(x, 13, 0)

#define MSEC_TO_TU(x) (x*1000/1024)
#define SN_GT(x, y) ((s32)(y - x) < 0)
#define SN_LT(x, y) ((s32)(x - y) < 0)

#define net_traversal_jiffies(s) \
	msecs_to_jiffies(s->u.mesh.mshcfg.dot11MeshHWMPnetDiameterTraversalTime)
#define default_lifetime(s) \
	MSEC_TO_TU(s->u.mesh.mshcfg.dot11MeshHWMPactivePathTimeout)
#define min_preq_int_jiff(s) \
	(msecs_to_jiffies(s->u.mesh.mshcfg.dot11MeshHWMPpreqMinInterval))
#define max_preq_retries(s) (s->u.mesh.mshcfg.dot11MeshHWMPmaxPREQretries)
#define disc_timeout_jiff(s) \
	msecs_to_jiffies(sdata->u.mesh.mshcfg.min_discovery_timeout)
#define root_path_confirmation_jiffies(s) \
	msecs_to_jiffies(sdata->u.mesh.mshcfg.dot11MeshHWMPconfirmationInterval)

enum mpath_frame_type {
	MPATH_PREQ = 0,
	MPATH_PREP,
	MPATH_PERR,
	MPATH_RANN
};

static const u8 broadcast_addr[ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

static int mesh_path_sel_frame_tx(enum mpath_frame_type action, u8 flags,
		u8 *orig_addr, __le32 orig_sn, u8 target_flags, u8 *target,
		__le32 target_sn, const u8 *da, u8 hop_count, u8 ttl,
		__le32 lifetime, __le32 metric, __le32 preq_id,
		struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;
	struct sk_buff *skb;
	struct ieee80211_mgmt *mgmt;
	u8 *pos, ie_len;
	int hdr_len = offsetof(struct ieee80211_mgmt, u.action.u.mesh_action) +
		      sizeof(mgmt->u.action.u.mesh_action);

	skb = dev_alloc_skb(local->tx_headroom +
			    hdr_len +
			    2 + 37); /* max HWMP IE */
	if (!skb)
		return -1;
	skb_reserve(skb, local->tx_headroom);
	mgmt = (struct ieee80211_mgmt *) skb_put(skb, hdr_len);
	memset(mgmt, 0, hdr_len);
	mgmt->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
					  IEEE80211_STYPE_ACTION);

	memcpy(mgmt->da, da, ETH_ALEN);
	memcpy(mgmt->sa, sdata->vif.addr, ETH_ALEN);
	/* BSSID == SA */
	memcpy(mgmt->bssid, sdata->vif.addr, ETH_ALEN);
	mgmt->u.action.category = WLAN_CATEGORY_MESH_ACTION;
	mgmt->u.action.u.mesh_action.action_code =
					WLAN_MESH_ACTION_HWMP_PATH_SELECTION;

	switch (action) {
	case MPATH_PREQ:
		mhwmp_dbg(sdata, "sending PREQ to %pM\n", target);
		ie_len = 37;
		pos = skb_put(skb, 2 + ie_len);
		*pos++ = WLAN_EID_PREQ;
		break;
	case MPATH_PREP:
		mhwmp_dbg(sdata, "sending PREP to %pM\n", target);
		ie_len = 31;
		pos = skb_put(skb, 2 + ie_len);
		*pos++ = WLAN_EID_PREP;
		break;
	case MPATH_RANN:
		mhwmp_dbg(sdata, "sending RANN from %pM\n", orig_addr);
		ie_len = sizeof(struct ieee80211_rann_ie);
		pos = skb_put(skb, 2 + ie_len);
		*pos++ = WLAN_EID_RANN;
		break;
	default:
		kfree_skb(skb);
		return -ENOTSUPP;
		break;
	}
	*pos++ = ie_len;
	*pos++ = flags;
	*pos++ = hop_count;
	*pos++ = ttl;
	if (action == MPATH_PREP) {
		memcpy(pos, target, ETH_ALEN);
		pos += ETH_ALEN;
		memcpy(pos, &target_sn, 4);
		pos += 4;
	} else {
		if (action == MPATH_PREQ) {
			memcpy(pos, &preq_id, 4);
			pos += 4;
		}
		memcpy(pos, orig_addr, ETH_ALEN);
		pos += ETH_ALEN;
		memcpy(pos, &orig_sn, 4);
		pos += 4;
	}
	memcpy(pos, &lifetime, 4);	/* interval for RANN */
	pos += 4;
	memcpy(pos, &metric, 4);
	pos += 4;
	if (action == MPATH_PREQ) {
		*pos++ = 1; /* destination count */
		*pos++ = target_flags;
		memcpy(pos, target, ETH_ALEN);
		pos += ETH_ALEN;
		memcpy(pos, &target_sn, 4);
		pos += 4;
	} else if (action == MPATH_PREP) {
		memcpy(pos, orig_addr, ETH_ALEN);
		pos += ETH_ALEN;
		memcpy(pos, &orig_sn, 4);
		pos += 4;
	}

	ieee80211_tx_skb(sdata, skb);
	return 0;
}


/*  Headroom is not adjusted.  Caller should ensure that skb has sufficient
 *  headroom in case the frame is encrypted. */
static void prepare_frame_for_deferred_tx(struct ieee80211_sub_if_data *sdata,
		struct sk_buff *skb)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);

	skb_set_mac_header(skb, 0);
	skb_set_network_header(skb, 0);
	skb_set_transport_header(skb, 0);

	/* Send all internal mgmt frames on VO. Accordingly set TID to 7. */
	skb_set_queue_mapping(skb, IEEE80211_AC_VO);
	skb->priority = 7;

	info->control.vif = &sdata->vif;
	ieee80211_set_qos_hdr(sdata, skb);
}

/**
 * mesh_send_path error - Sends a PERR mesh management frame
 *
 * @target: broken destination
 * @target_sn: SN of the broken destination
 * @target_rcode: reason code for this PERR
 * @ra: node this frame is addressed to
 *
 * Note: This function may be called with driver locks taken that the driver
 * also acquires in the TX path.  To avoid a deadlock we don't transmit the
 * frame directly but add it to the pending queue instead.
 */
int mesh_path_error_tx(u8 ttl, u8 *target, __le32 target_sn,
		       __le16 target_rcode, const u8 *ra,
		       struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;
	struct sk_buff *skb;
	struct ieee80211_if_mesh *ifmsh = &sdata->u.mesh;
	struct ieee80211_mgmt *mgmt;
	u8 *pos, ie_len;
	int hdr_len = offsetof(struct ieee80211_mgmt, u.action.u.mesh_action) +
		      sizeof(mgmt->u.action.u.mesh_action);

	if (time_before(jiffies, ifmsh->next_perr))
		return -EAGAIN;

	skb = dev_alloc_skb(local->tx_headroom +
			    hdr_len +
			    2 + 15 /* PERR IE */);
	if (!skb)
		return -1;
	skb_reserve(skb, local->tx_headroom);
	mgmt = (struct ieee80211_mgmt *) skb_put(skb, hdr_len);
	memset(mgmt, 0, hdr_len);
	mgmt->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
					  IEEE80211_STYPE_ACTION);

	memcpy(mgmt->da, ra, ETH_ALEN);
	memcpy(mgmt->sa, sdata->vif.addr, ETH_ALEN);
	/* BSSID == SA */
	memcpy(mgmt->bssid, sdata->vif.addr, ETH_ALEN);
	mgmt->u.action.category = WLAN_CATEGORY_MESH_ACTION;
	mgmt->u.action.u.mesh_action.action_code =
					WLAN_MESH_ACTION_HWMP_PATH_SELECTION;
	ie_len = 15;
	pos = skb_put(skb, 2 + ie_len);
	*pos++ = WLAN_EID_PERR;
	*pos++ = ie_len;
	/* ttl */
	*pos++ = ttl;
	/* number of destinations */
	*pos++ = 1;
	/*
	 * flags bit, bit 1 is unset if we know the sequence number and
	 * bit 2 is set if we have a reason code
	 */
	*pos = 0;
	if (!target_sn)
		*pos |= MP_F_USN;
	if (target_rcode)
		*pos |= MP_F_RCODE;
	pos++;
	memcpy(pos, target, ETH_ALEN);
	pos += ETH_ALEN;
	memcpy(pos, &target_sn, 4);
	pos += 4;
	memcpy(pos, &target_rcode, 2);

	/* see note in function header */
	prepare_frame_for_deferred_tx(sdata, skb);
	ifmsh->next_perr = TU_TO_EXP_TIME(
				   ifmsh->mshcfg.dot11MeshHWMPperrMinInterval);
	ieee80211_add_pending_skb(local, skb);
	return 0;
}

void ieee80211s_update_metric(struct ieee80211_local *local,
		struct sta_info *sta, struct sk_buff *skb)
{
	struct ieee80211_tx_info *txinfo = IEEE80211_SKB_CB(skb);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	int failed;

	if (!ieee80211_is_data(hdr->frame_control))
		return;

	failed = !(txinfo->flags & IEEE80211_TX_STAT_ACK);

	/* moving average, scaled to 100 */
	sta->fail_avg = ((80 * sta->fail_avg + 5) / 100 + 20 * failed);
	if (sta->fail_avg > 95)
		mesh_plink_broken(sta);
}

static u32 airtime_link_metric_get(struct ieee80211_local *local,
				   struct sta_info *sta)
{
	struct rate_info rinfo;
	/* This should be adjusted for each device */
	int device_constant = 1 << ARITH_SHIFT;
	int test_frame_len = TEST_FRAME_LEN << ARITH_SHIFT;
	int s_unit = 1 << ARITH_SHIFT;
	int rate, err;
	u32 tx_time, estimated_retx;
	u64 result;

	if (sta->fail_avg >= 100)
		return MAX_METRIC;

	sta_set_rate_info_tx(sta, &sta->last_tx_rate, &rinfo);
	rate = cfg80211_calculate_bitrate(&rinfo);
	if (WARN_ON(!rate))
		return MAX_METRIC;

	err = (sta->fail_avg << ARITH_SHIFT) / 100;

	/* bitrate is in units of 100 Kbps, while we need rate in units of
	 * 1Mbps. This will be corrected on tx_time computation.
	 */
	tx_time = (device_constant + 10 * test_frame_len / rate);
	estimated_retx = ((1 << (2 * ARITH_SHIFT)) / (s_unit - err));
	result = (tx_time * estimated_retx) >> (2 * ARITH_SHIFT) ;
	return (u32)result;
}

/**
 * hwmp_route_info_get - Update routing info to originator and transmitter
 *
 * @sdata: local mesh subif
 * @mgmt: mesh management frame
 * @hwmp_ie: hwmp information element (PREP or PREQ)
 *
 * This function updates the path routing information to the originator and the
 * transmitter of a HWMP PREQ or PREP frame.
 *
 * Returns: metric to frame originator or 0 if the frame should not be further
 * processed
 *
 * Notes: this function is the only place (besides user-provided info) where
 * path routing information is updated.
 */
static u32 hwmp_route_info_get(struct ieee80211_sub_if_data *sdata,
			    struct ieee80211_mgmt *mgmt,
			    u8 *hwmp_ie, enum mpath_frame_type action)
{
	struct ieee80211_local *local = sdata->local;
	struct mesh_path *mpath;
	struct sta_info *sta;
	bool fresh_info;
	u8 *orig_addr, *ta;
	u32 orig_sn, orig_metric;
	unsigned long orig_lifetime, exp_time;
	u32 last_hop_metric, new_metric;
	bool process = true;

	rcu_read_lock();
	sta = sta_info_get(sdata, mgmt->sa);
	if (!sta) {
		rcu_read_unlock();
		return 0;
	}

	last_hop_metric = airtime_link_metric_get(local, sta);
	/* Update and check originator routing info */
	fresh_info = true;

	switch (action) {
	case MPATH_PREQ:
		orig_addr = PREQ_IE_ORIG_ADDR(hwmp_ie);
		orig_sn = PREQ_IE_ORIG_SN(hwmp_ie);
		orig_lifetime = PREQ_IE_LIFETIME(hwmp_ie);
		orig_metric = PREQ_IE_METRIC(hwmp_ie);
		break;
	case MPATH_PREP:
		/* Originator here refers to the MP that was the target in the
		 * Path Request. We divert from the nomenclature in the draft
		 * so that we can easily use a single function to gather path
		 * information from both PREQ and PREP frames.
		 */
		orig_addr = PREP_IE_TARGET_ADDR(hwmp_ie);
		orig_sn = PREP_IE_TARGET_SN(hwmp_ie);
		orig_lifetime = PREP_IE_LIFETIME(hwmp_ie);
		orig_metric = PREP_IE_METRIC(hwmp_ie);
		break;
	default:
		rcu_read_unlock();
		return 0;
	}
	new_metric = orig_metric + last_hop_metric;
	if (new_metric < orig_metric)
		new_metric = MAX_METRIC;
	exp_time = TU_TO_EXP_TIME(orig_lifetime);

	if (ether_addr_equal(orig_addr, sdata->vif.addr)) {
		/* This MP is the originator, we are not interested in this
		 * frame, except for updating transmitter's path info.
		 */
		process = false;
		fresh_info = false;
	} else {
		mpath = mesh_path_lookup(orig_addr, sdata);
		if (mpath) {
			spin_lock_bh(&mpath->state_lock);
			if (mpath->flags & MESH_PATH_FIXED)
				fresh_info = false;
			else if ((mpath->flags & MESH_PATH_ACTIVE) &&
			    (mpath->flags & MESH_PATH_SN_VALID)) {
				if (SN_GT(mpath->sn, orig_sn) ||
				    (mpath->sn == orig_sn &&
				     new_metric >= mpath->metric)) {
					process = false;
					fresh_info = false;
				}
			}
		} else {
			mesh_path_add(orig_addr, sdata);
			mpath = mesh_path_lookup(orig_addr, sdata);
			if (!mpath) {
				rcu_read_unlock();
				return 0;
			}
			spin_lock_bh(&mpath->state_lock);
		}

		if (fresh_info) {
			mesh_path_assign_nexthop(mpath, sta);
			mpath->flags |= MESH_PATH_SN_VALID;
			mpath->metric = new_metric;
			mpath->sn = orig_sn;
			mpath->exp_time = time_after(mpath->exp_time, exp_time)
					  ?  mpath->exp_time : exp_time;
			mesh_path_activate(mpath);
			spin_unlock_bh(&mpath->state_lock);
			mesh_path_tx_pending(mpath);
			/* draft says preq_id should be saved to, but there does
			 * not seem to be any use for it, skipping by now
			 */
		} else
			spin_unlock_bh(&mpath->state_lock);
	}

	/* Update and check transmitter routing info */
	ta = mgmt->sa;
	if (ether_addr_equal(orig_addr, ta))
		fresh_info = false;
	else {
		fresh_info = true;

		mpath = mesh_path_lookup(ta, sdata);
		if (mpath) {
			spin_lock_bh(&mpath->state_lock);
			if ((mpath->flags & MESH_PATH_FIXED) ||
				((mpath->flags & MESH_PATH_ACTIVE) &&
					(last_hop_metric > mpath->metric)))
				fresh_info = false;
		} else {
			mesh_path_add(ta, sdata);
			mpath = mesh_path_lookup(ta, sdata);
			if (!mpath) {
				rcu_read_unlock();
				return 0;
			}
			spin_lock_bh(&mpath->state_lock);
		}

		if (fresh_info) {
			mesh_path_assign_nexthop(mpath, sta);
			mpath->metric = last_hop_metric;
			mpath->exp_time = time_after(mpath->exp_time, exp_time)
					  ?  mpath->exp_time : exp_time;
			mesh_path_activate(mpath);
			spin_unlock_bh(&mpath->state_lock);
			mesh_path_tx_pending(mpath);
		} else
			spin_unlock_bh(&mpath->state_lock);
	}

	rcu_read_unlock();

	return process ? new_metric : 0;
}

static void hwmp_preq_frame_process(struct ieee80211_sub_if_data *sdata,
				    struct ieee80211_mgmt *mgmt,
				    u8 *preq_elem, u32 metric)
{
	struct ieee80211_if_mesh *ifmsh = &sdata->u.mesh;
	struct mesh_path *mpath = NULL;
	u8 *target_addr, *orig_addr;
	const u8 *da;
	u8 target_flags, ttl, flags;
	u32 orig_sn, target_sn, lifetime, orig_metric;
	bool reply = false;
	bool forward = true;
	bool root_is_gate;

	/* Update target SN, if present */
	target_addr = PREQ_IE_TARGET_ADDR(preq_elem);
	orig_addr = PREQ_IE_ORIG_ADDR(preq_elem);
	target_sn = PREQ_IE_TARGET_SN(preq_elem);
	orig_sn = PREQ_IE_ORIG_SN(preq_elem);
	target_flags = PREQ_IE_TARGET_F(preq_elem);
	orig_metric = metric;
	/* Proactive PREQ gate announcements */
	flags = PREQ_IE_FLAGS(preq_elem);
	root_is_gate = !!(flags & RANN_FLAG_IS_GATE);

	mhwmp_dbg(sdata, "received PREQ from %pM\n", orig_addr);

	if (ether_addr_equal(target_addr, sdata->vif.addr)) {
		mhwmp_dbg(sdata, "PREQ is for us\n");
		forward = false;
		reply = true;
		metric = 0;
		if (time_after(jiffies, ifmsh->last_sn_update +
					net_traversal_jiffies(sdata)) ||
		    time_before(jiffies, ifmsh->last_sn_update)) {
			target_sn = ++ifmsh->sn;
			ifmsh->last_sn_update = jiffies;
		}
	} else if (is_broadcast_ether_addr(target_addr) &&
		   (target_flags & IEEE80211_PREQ_TO_FLAG)) {
		rcu_read_lock();
		mpath = mesh_path_lookup(orig_addr, sdata);
		if (mpath) {
			if (flags & IEEE80211_PREQ_PROACTIVE_PREP_FLAG) {
				reply = true;
				target_addr = sdata->vif.addr;
				target_sn = ++ifmsh->sn;
				metric = 0;
				ifmsh->last_sn_update = jiffies;
			}
			if (root_is_gate)
				mesh_path_add_gate(mpath);
		}
		rcu_read_unlock();
	} else {
		rcu_read_lock();
		mpath = mesh_path_lookup(target_addr, sdata);
		if (mpath) {
			if ((!(mpath->flags & MESH_PATH_SN_VALID)) ||
					SN_LT(mpath->sn, target_sn)) {
				mpath->sn = target_sn;
				mpath->flags |= MESH_PATH_SN_VALID;
			} else if ((!(target_flags & MP_F_DO)) &&
					(mpath->flags & MESH_PATH_ACTIVE)) {
				reply = true;
				metric = mpath->metric;
				target_sn = mpath->sn;
				if (target_flags & MP_F_RF)
					target_flags |= MP_F_DO;
				else
					forward = false;
			}
		}
		rcu_read_unlock();
	}

	if (reply) {
		lifetime = PREQ_IE_LIFETIME(preq_elem);
		ttl = ifmsh->mshcfg.element_ttl;
		if (ttl != 0) {
			mhwmp_dbg(sdata, "replying to the PREQ\n");
			mesh_path_sel_frame_tx(MPATH_PREP, 0, orig_addr,
				cpu_to_le32(orig_sn), 0, target_addr,
				cpu_to_le32(target_sn), mgmt->sa, 0, ttl,
				cpu_to_le32(lifetime), cpu_to_le32(metric),
				0, sdata);
		} else {
			ifmsh->mshstats.dropped_frames_ttl++;
		}
	}

	if (forward && ifmsh->mshcfg.dot11MeshForwarding) {
		u32 preq_id;
		u8 hopcount;

		ttl = PREQ_IE_TTL(preq_elem);
		lifetime = PREQ_IE_LIFETIME(preq_elem);
		if (ttl <= 1) {
			ifmsh->mshstats.dropped_frames_ttl++;
			return;
		}
		mhwmp_dbg(sdata, "forwarding the PREQ from %pM\n", orig_addr);
		--ttl;
		preq_id = PREQ_IE_PREQ_ID(preq_elem);
		hopcount = PREQ_IE_HOPCOUNT(preq_elem) + 1;
		da = (mpath && mpath->is_root) ?
			mpath->rann_snd_addr : broadcast_addr;

		if (flags & IEEE80211_PREQ_PROACTIVE_PREP_FLAG) {
			target_addr = PREQ_IE_TARGET_ADDR(preq_elem);
			target_sn = PREQ_IE_TARGET_SN(preq_elem);
			metric = orig_metric;
		}

		mesh_path_sel_frame_tx(MPATH_PREQ, flags, orig_addr,
				cpu_to_le32(orig_sn), target_flags, target_addr,
				cpu_to_le32(target_sn), da,
				hopcount, ttl, cpu_to_le32(lifetime),
				cpu_to_le32(metric), cpu_to_le32(preq_id),
				sdata);
		if (!is_multicast_ether_addr(da))
			ifmsh->mshstats.fwded_unicast++;
		else
			ifmsh->mshstats.fwded_mcast++;
		ifmsh->mshstats.fwded_frames++;
	}
}


static inline struct sta_info *
next_hop_deref_protected(struct mesh_path *mpath)
{
	return rcu_dereference_protected(mpath->next_hop,
					 lockdep_is_held(&mpath->state_lock));
}


static void hwmp_prep_frame_process(struct ieee80211_sub_if_data *sdata,
				    struct ieee80211_mgmt *mgmt,
				    u8 *prep_elem, u32 metric)
{
	struct ieee80211_if_mesh *ifmsh = &sdata->u.mesh;
	struct mesh_path *mpath;
	u8 *target_addr, *orig_addr;
	u8 ttl, hopcount, flags;
	u8 next_hop[ETH_ALEN];
	u32 target_sn, orig_sn, lifetime;

	mhwmp_dbg(sdata, "received PREP from %pM\n",
		  PREP_IE_ORIG_ADDR(prep_elem));

	orig_addr = PREP_IE_ORIG_ADDR(prep_elem);
	if (ether_addr_equal(orig_addr, sdata->vif.addr))
		/* destination, no forwarding required */
		return;

	if (!ifmsh->mshcfg.dot11MeshForwarding)
		return;

	ttl = PREP_IE_TTL(prep_elem);
	if (ttl <= 1) {
		sdata->u.mesh.mshstats.dropped_frames_ttl++;
		return;
	}

	rcu_read_lock();
	mpath = mesh_path_lookup(orig_addr, sdata);
	if (mpath)
		spin_lock_bh(&mpath->state_lock);
	else
		goto fail;
	if (!(mpath->flags & MESH_PATH_ACTIVE)) {
		spin_unlock_bh(&mpath->state_lock);
		goto fail;
	}
	memcpy(next_hop, next_hop_deref_protected(mpath)->sta.addr, ETH_ALEN);
	spin_unlock_bh(&mpath->state_lock);
	--ttl;
	flags = PREP_IE_FLAGS(prep_elem);
	lifetime = PREP_IE_LIFETIME(prep_elem);
	hopcount = PREP_IE_HOPCOUNT(prep_elem) + 1;
	target_addr = PREP_IE_TARGET_ADDR(prep_elem);
	target_sn = PREP_IE_TARGET_SN(prep_elem);
	orig_sn = PREP_IE_ORIG_SN(prep_elem);

	mesh_path_sel_frame_tx(MPATH_PREP, flags, orig_addr,
		cpu_to_le32(orig_sn), 0, target_addr,
		cpu_to_le32(target_sn), next_hop, hopcount,
		ttl, cpu_to_le32(lifetime), cpu_to_le32(metric),
		0, sdata);
	rcu_read_unlock();

	sdata->u.mesh.mshstats.fwded_unicast++;
	sdata->u.mesh.mshstats.fwded_frames++;
	return;

fail:
	rcu_read_unlock();
	sdata->u.mesh.mshstats.dropped_frames_no_route++;
}

static void hwmp_perr_frame_process(struct ieee80211_sub_if_data *sdata,
			     struct ieee80211_mgmt *mgmt, u8 *perr_elem)
{
	struct ieee80211_if_mesh *ifmsh = &sdata->u.mesh;
	struct mesh_path *mpath;
	u8 ttl;
	u8 *ta, *target_addr;
	u32 target_sn;
	u16 target_rcode;

	ta = mgmt->sa;
	ttl = PERR_IE_TTL(perr_elem);
	if (ttl <= 1) {
		ifmsh->mshstats.dropped_frames_ttl++;
		return;
	}
	ttl--;
	target_addr = PERR_IE_TARGET_ADDR(perr_elem);
	target_sn = PERR_IE_TARGET_SN(perr_elem);
	target_rcode = PERR_IE_TARGET_RCODE(perr_elem);

	rcu_read_lock();
	mpath = mesh_path_lookup(target_addr, sdata);
	if (mpath) {
		struct sta_info *sta;

		spin_lock_bh(&mpath->state_lock);
		sta = next_hop_deref_protected(mpath);
		if (mpath->flags & MESH_PATH_ACTIVE &&
		    ether_addr_equal(ta, sta->sta.addr) &&
		    (!(mpath->flags & MESH_PATH_SN_VALID) ||
		    SN_GT(target_sn, mpath->sn))) {
			mpath->flags &= ~MESH_PATH_ACTIVE;
			mpath->sn = target_sn;
			spin_unlock_bh(&mpath->state_lock);
			if (!ifmsh->mshcfg.dot11MeshForwarding)
				goto endperr;
			mesh_path_error_tx(ttl, target_addr, cpu_to_le32(target_sn),
					   cpu_to_le16(target_rcode),
					   broadcast_addr, sdata);
		} else
			spin_unlock_bh(&mpath->state_lock);
	}
endperr:
	rcu_read_unlock();
}

static void hwmp_rann_frame_process(struct ieee80211_sub_if_data *sdata,
				struct ieee80211_mgmt *mgmt,
				struct ieee80211_rann_ie *rann)
{
	struct ieee80211_if_mesh *ifmsh = &sdata->u.mesh;
	struct ieee80211_local *local = sdata->local;
	struct sta_info *sta;
	struct mesh_path *mpath;
	u8 ttl, flags, hopcount;
	u8 *orig_addr;
	u32 orig_sn, metric, metric_txsta, interval;
	bool root_is_gate;

	ttl = rann->rann_ttl;
	flags = rann->rann_flags;
	root_is_gate = !!(flags & RANN_FLAG_IS_GATE);
	orig_addr = rann->rann_addr;
	orig_sn = le32_to_cpu(rann->rann_seq);
	interval = le32_to_cpu(rann->rann_interval);
	hopcount = rann->rann_hopcount;
	hopcount++;
	metric = le32_to_cpu(rann->rann_metric);

	/*  Ignore our own RANNs */
	if (ether_addr_equal(orig_addr, sdata->vif.addr))
		return;

	mhwmp_dbg(sdata,
		  "received RANN from %pM via neighbour %pM (is_gate=%d)\n",
		  orig_addr, mgmt->sa, root_is_gate);

	rcu_read_lock();
	sta = sta_info_get(sdata, mgmt->sa);
	if (!sta) {
		rcu_read_unlock();
		return;
	}

	metric_txsta = airtime_link_metric_get(local, sta);

	mpath = mesh_path_lookup(orig_addr, sdata);
	if (!mpath) {
		mesh_path_add(orig_addr, sdata);
		mpath = mesh_path_lookup(orig_addr, sdata);
		if (!mpath) {
			rcu_read_unlock();
			sdata->u.mesh.mshstats.dropped_frames_no_route++;
			return;
		}
	}

	if (!(SN_LT(mpath->sn, orig_sn)) &&
	    !(mpath->sn == orig_sn && metric < mpath->rann_metric)) {
		rcu_read_unlock();
		return;
	}

	if ((!(mpath->flags & (MESH_PATH_ACTIVE | MESH_PATH_RESOLVING)) ||
	     (time_after(jiffies, mpath->last_preq_to_root +
				  root_path_confirmation_jiffies(sdata)) ||
	     time_before(jiffies, mpath->last_preq_to_root))) &&
	     !(mpath->flags & MESH_PATH_FIXED) && (ttl != 0)) {
		mhwmp_dbg(sdata,
			  "time to refresh root mpath %pM\n",
			  orig_addr);
		mesh_queue_preq(mpath, PREQ_Q_F_START | PREQ_Q_F_REFRESH);
		mpath->last_preq_to_root = jiffies;
	}

	mpath->sn = orig_sn;
	mpath->rann_metric = metric + metric_txsta;
	mpath->is_root = true;
	/* Recording RANNs sender address to send individually
	 * addressed PREQs destined for root mesh STA */
	memcpy(mpath->rann_snd_addr, mgmt->sa, ETH_ALEN);

	if (root_is_gate)
		mesh_path_add_gate(mpath);

	if (ttl <= 1) {
		ifmsh->mshstats.dropped_frames_ttl++;
		rcu_read_unlock();
		return;
	}
	ttl--;

	if (ifmsh->mshcfg.dot11MeshForwarding) {
		mesh_path_sel_frame_tx(MPATH_RANN, flags, orig_addr,
				       cpu_to_le32(orig_sn),
				       0, NULL, 0, broadcast_addr,
				       hopcount, ttl, cpu_to_le32(interval),
				       cpu_to_le32(metric + metric_txsta),
				       0, sdata);
	}

	rcu_read_unlock();
}


void mesh_rx_path_sel_frame(struct ieee80211_sub_if_data *sdata,
			    struct ieee80211_mgmt *mgmt,
			    size_t len)
{
	struct ieee802_11_elems elems;
	size_t baselen;
	u32 last_hop_metric;
	struct sta_info *sta;

	/* need action_code */
	if (len < IEEE80211_MIN_ACTION_SIZE + 1)
		return;

	rcu_read_lock();
	sta = sta_info_get(sdata, mgmt->sa);
	if (!sta || sta->plink_state != NL80211_PLINK_ESTAB) {
		rcu_read_unlock();
		return;
	}
	rcu_read_unlock();

	baselen = (u8 *) mgmt->u.action.u.mesh_action.variable - (u8 *) mgmt;
	ieee802_11_parse_elems(mgmt->u.action.u.mesh_action.variable,
			len - baselen, &elems);

	if (elems.preq) {
		if (elems.preq_len != 37)
			/* Right now we support just 1 destination and no AE */
			return;
		last_hop_metric = hwmp_route_info_get(sdata, mgmt, elems.preq,
						      MPATH_PREQ);
		if (last_hop_metric)
			hwmp_preq_frame_process(sdata, mgmt, elems.preq,
						last_hop_metric);
	}
	if (elems.prep) {
		if (elems.prep_len != 31)
			/* Right now we support no AE */
			return;
		last_hop_metric = hwmp_route_info_get(sdata, mgmt, elems.prep,
						      MPATH_PREP);
		if (last_hop_metric)
			hwmp_prep_frame_process(sdata, mgmt, elems.prep,
						last_hop_metric);
	}
	if (elems.perr) {
		if (elems.perr_len != 15)
			/* Right now we support only one destination per PERR */
			return;
		hwmp_perr_frame_process(sdata, mgmt, elems.perr);
	}
	if (elems.rann)
		hwmp_rann_frame_process(sdata, mgmt, elems.rann);
}

/**
 * mesh_queue_preq - queue a PREQ to a given destination
 *
 * @mpath: mesh path to discover
 * @flags: special attributes of the PREQ to be sent
 *
 * Locking: the function must be called from within a rcu read lock block.
 *
 */
static void mesh_queue_preq(struct mesh_path *mpath, u8 flags)
{
	struct ieee80211_sub_if_data *sdata = mpath->sdata;
	struct ieee80211_if_mesh *ifmsh = &sdata->u.mesh;
	struct mesh_preq_queue *preq_node;

	preq_node = kmalloc(sizeof(struct mesh_preq_queue), GFP_ATOMIC);
	if (!preq_node) {
		mhwmp_dbg(sdata, "could not allocate PREQ node\n");
		return;
	}

	spin_lock_bh(&ifmsh->mesh_preq_queue_lock);
	if (ifmsh->preq_queue_len == MAX_PREQ_QUEUE_LEN) {
		spin_unlock_bh(&ifmsh->mesh_preq_queue_lock);
		kfree(preq_node);
		if (printk_ratelimit())
			mhwmp_dbg(sdata, "PREQ node queue full\n");
		return;
	}

	spin_lock(&mpath->state_lock);
	if (mpath->flags & MESH_PATH_REQ_QUEUED) {
		spin_unlock(&mpath->state_lock);
		spin_unlock_bh(&ifmsh->mesh_preq_queue_lock);
		kfree(preq_node);
		return;
	}

	memcpy(preq_node->dst, mpath->dst, ETH_ALEN);
	preq_node->flags = flags;

	mpath->flags |= MESH_PATH_REQ_QUEUED;
	spin_unlock(&mpath->state_lock);

	list_add_tail(&preq_node->list, &ifmsh->preq_queue.list);
	++ifmsh->preq_queue_len;
	spin_unlock_bh(&ifmsh->mesh_preq_queue_lock);

	if (time_after(jiffies, ifmsh->last_preq + min_preq_int_jiff(sdata)))
		ieee80211_queue_work(&sdata->local->hw, &sdata->work);

	else if (time_before(jiffies, ifmsh->last_preq)) {
		/* avoid long wait if did not send preqs for a long time
		 * and jiffies wrapped around
		 */
		ifmsh->last_preq = jiffies - min_preq_int_jiff(sdata) - 1;
		ieee80211_queue_work(&sdata->local->hw, &sdata->work);
	} else
		mod_timer(&ifmsh->mesh_path_timer, ifmsh->last_preq +
						min_preq_int_jiff(sdata));
}

/**
 * mesh_path_start_discovery - launch a path discovery from the PREQ queue
 *
 * @sdata: local mesh subif
 */
void mesh_path_start_discovery(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_mesh *ifmsh = &sdata->u.mesh;
	struct mesh_preq_queue *preq_node;
	struct mesh_path *mpath;
	u8 ttl, target_flags;
	const u8 *da;
	u32 lifetime;

	spin_lock_bh(&ifmsh->mesh_preq_queue_lock);
	if (!ifmsh->preq_queue_len ||
		time_before(jiffies, ifmsh->last_preq +
				min_preq_int_jiff(sdata))) {
		spin_unlock_bh(&ifmsh->mesh_preq_queue_lock);
		return;
	}

	preq_node = list_first_entry(&ifmsh->preq_queue.list,
			struct mesh_preq_queue, list);
	list_del(&preq_node->list);
	--ifmsh->preq_queue_len;
	spin_unlock_bh(&ifmsh->mesh_preq_queue_lock);

	rcu_read_lock();
	mpath = mesh_path_lookup(preq_node->dst, sdata);
	if (!mpath)
		goto enddiscovery;

	spin_lock_bh(&mpath->state_lock);
	mpath->flags &= ~MESH_PATH_REQ_QUEUED;
	if (preq_node->flags & PREQ_Q_F_START) {
		if (mpath->flags & MESH_PATH_RESOLVING) {
			spin_unlock_bh(&mpath->state_lock);
			goto enddiscovery;
		} else {
			mpath->flags &= ~MESH_PATH_RESOLVED;
			mpath->flags |= MESH_PATH_RESOLVING;
			mpath->discovery_retries = 0;
			mpath->discovery_timeout = disc_timeout_jiff(sdata);
		}
	} else if (!(mpath->flags & MESH_PATH_RESOLVING) ||
			mpath->flags & MESH_PATH_RESOLVED) {
		mpath->flags &= ~MESH_PATH_RESOLVING;
		spin_unlock_bh(&mpath->state_lock);
		goto enddiscovery;
	}

	ifmsh->last_preq = jiffies;

	if (time_after(jiffies, ifmsh->last_sn_update +
				net_traversal_jiffies(sdata)) ||
	    time_before(jiffies, ifmsh->last_sn_update)) {
		++ifmsh->sn;
		sdata->u.mesh.last_sn_update = jiffies;
	}
	lifetime = default_lifetime(sdata);
	ttl = sdata->u.mesh.mshcfg.element_ttl;
	if (ttl == 0) {
		sdata->u.mesh.mshstats.dropped_frames_ttl++;
		spin_unlock_bh(&mpath->state_lock);
		goto enddiscovery;
	}

	if (preq_node->flags & PREQ_Q_F_REFRESH)
		target_flags = MP_F_DO;
	else
		target_flags = MP_F_RF;

	spin_unlock_bh(&mpath->state_lock);
	da = (mpath->is_root) ? mpath->rann_snd_addr : broadcast_addr;
	mesh_path_sel_frame_tx(MPATH_PREQ, 0, sdata->vif.addr,
			cpu_to_le32(ifmsh->sn), target_flags, mpath->dst,
			cpu_to_le32(mpath->sn), da, 0,
			ttl, cpu_to_le32(lifetime), 0,
			cpu_to_le32(ifmsh->preq_id++), sdata);
	mod_timer(&mpath->timer, jiffies + mpath->discovery_timeout);

enddiscovery:
	rcu_read_unlock();
	kfree(preq_node);
}

/* mesh_nexthop_resolve - lookup next hop for given skb and start path
 * discovery if no forwarding information is found.
 *
 * @skb: 802.11 frame to be sent
 * @sdata: network subif the frame will be sent through
 *
 * Returns: 0 if the next hop was found and -ENOENT if the frame was queued.
 * skb is freeed here if no mpath could be allocated.
 */
int mesh_nexthop_resolve(struct sk_buff *skb,
			 struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct mesh_path *mpath;
	struct sk_buff *skb_to_free = NULL;
	u8 *target_addr = hdr->addr3;
	int err = 0;

	rcu_read_lock();
	err = mesh_nexthop_lookup(skb, sdata);
	if (!err)
		goto endlookup;

	/* no nexthop found, start resolving */
	mpath = mesh_path_lookup(target_addr, sdata);
	if (!mpath) {
		mesh_path_add(target_addr, sdata);
		mpath = mesh_path_lookup(target_addr, sdata);
		if (!mpath) {
			mesh_path_discard_frame(skb, sdata);
			err = -ENOSPC;
			goto endlookup;
		}
	}

	if (!(mpath->flags & MESH_PATH_RESOLVING))
		mesh_queue_preq(mpath, PREQ_Q_F_START);

	if (skb_queue_len(&mpath->frame_queue) >= MESH_FRAME_QUEUE_LEN)
		skb_to_free = skb_dequeue(&mpath->frame_queue);

	info->flags |= IEEE80211_TX_INTFL_NEED_TXPROCESSING;
	ieee80211_set_qos_hdr(sdata, skb);
	skb_queue_tail(&mpath->frame_queue, skb);
	err = -ENOENT;
	if (skb_to_free)
		mesh_path_discard_frame(skb_to_free, sdata);

endlookup:
	rcu_read_unlock();
	return err;
}
/**
 * mesh_nexthop_lookup - put the appropriate next hop on a mesh frame. Calling
 * this function is considered "using" the associated mpath, so preempt a path
 * refresh if this mpath expires soon.
 *
 * @skb: 802.11 frame to be sent
 * @sdata: network subif the frame will be sent through
 *
 * Returns: 0 if the next hop was found. Nonzero otherwise.
 */
int mesh_nexthop_lookup(struct sk_buff *skb,
			struct ieee80211_sub_if_data *sdata)
{
	struct mesh_path *mpath;
	struct sta_info *next_hop;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	u8 *target_addr = hdr->addr3;
	int err = -ENOENT;

	rcu_read_lock();
	mpath = mesh_path_lookup(target_addr, sdata);

	if (!mpath || !(mpath->flags & MESH_PATH_ACTIVE))
		goto endlookup;

	if (time_after(jiffies,
		       mpath->exp_time -
		       msecs_to_jiffies(sdata->u.mesh.mshcfg.path_refresh_time)) &&
	    ether_addr_equal(sdata->vif.addr, hdr->addr4) &&
	    !(mpath->flags & MESH_PATH_RESOLVING) &&
	    !(mpath->flags & MESH_PATH_FIXED))
		mesh_queue_preq(mpath, PREQ_Q_F_START | PREQ_Q_F_REFRESH);

	next_hop = rcu_dereference(mpath->next_hop);
	if (next_hop) {
		memcpy(hdr->addr1, next_hop->sta.addr, ETH_ALEN);
		memcpy(hdr->addr2, sdata->vif.addr, ETH_ALEN);
		err = 0;
	}

endlookup:
	rcu_read_unlock();
	return err;
}

void mesh_path_timer(unsigned long data)
{
	struct mesh_path *mpath = (void *) data;
	struct ieee80211_sub_if_data *sdata = mpath->sdata;
	int ret;

	if (sdata->local->quiescing)
		return;

	spin_lock_bh(&mpath->state_lock);
	if (mpath->flags & MESH_PATH_RESOLVED ||
			(!(mpath->flags & MESH_PATH_RESOLVING))) {
		mpath->flags &= ~(MESH_PATH_RESOLVING | MESH_PATH_RESOLVED);
		spin_unlock_bh(&mpath->state_lock);
	} else if (mpath->discovery_retries < max_preq_retries(sdata)) {
		++mpath->discovery_retries;
		mpath->discovery_timeout *= 2;
		mpath->flags &= ~MESH_PATH_REQ_QUEUED;
		spin_unlock_bh(&mpath->state_lock);
		mesh_queue_preq(mpath, 0);
	} else {
		mpath->flags = 0;
		mpath->exp_time = jiffies;
		spin_unlock_bh(&mpath->state_lock);
		if (!mpath->is_gate && mesh_gate_num(sdata) > 0) {
			ret = mesh_path_send_to_gates(mpath);
			if (ret)
				mhwmp_dbg(sdata, "no gate was reachable\n");
		} else
			mesh_path_flush_pending(mpath);
	}
}

void
mesh_path_tx_root_frame(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_mesh *ifmsh = &sdata->u.mesh;
	u32 interval = ifmsh->mshcfg.dot11MeshHWMPRannInterval;
	u8 flags, target_flags = 0;

	flags = (ifmsh->mshcfg.dot11MeshGateAnnouncementProtocol)
			? RANN_FLAG_IS_GATE : 0;

	switch (ifmsh->mshcfg.dot11MeshHWMPRootMode) {
	case IEEE80211_PROACTIVE_RANN:
		mesh_path_sel_frame_tx(MPATH_RANN, flags, sdata->vif.addr,
			       cpu_to_le32(++ifmsh->sn),
			       0, NULL, 0, broadcast_addr,
			       0, ifmsh->mshcfg.element_ttl,
			       cpu_to_le32(interval), 0, 0, sdata);
		break;
	case IEEE80211_PROACTIVE_PREQ_WITH_PREP:
		flags |= IEEE80211_PREQ_PROACTIVE_PREP_FLAG;
	case IEEE80211_PROACTIVE_PREQ_NO_PREP:
		interval = ifmsh->mshcfg.dot11MeshHWMPactivePathToRootTimeout;
		target_flags |= IEEE80211_PREQ_TO_FLAG |
				IEEE80211_PREQ_USN_FLAG;
		mesh_path_sel_frame_tx(MPATH_PREQ, flags, sdata->vif.addr,
				cpu_to_le32(++ifmsh->sn), target_flags,
				(u8 *) broadcast_addr, 0, broadcast_addr,
				0, ifmsh->mshcfg.element_ttl,
				cpu_to_le32(interval),
				0, cpu_to_le32(ifmsh->preq_id++), sdata);
		break;
	default:
		mhwmp_dbg(sdata, "Proactive mechanism not supported\n");
		return;
	}
}
