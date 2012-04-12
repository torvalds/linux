/*
 * Copyright 2011-2012, Pavel Zubarev <pavel.zubarev@gmail.com>
 * Copyright 2011-2012, Marco Porsch <marco.porsch@s2005.tu-chemnitz.de>
 * Copyright 2011-2012, cozybit Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "ieee80211_i.h"
#include "mesh.h"
#include "driver-ops.h"

#ifdef CONFIG_MAC80211_VERBOSE_MESH_SYNC_DEBUG
#define msync_dbg(fmt, args...) \
	printk(KERN_DEBUG "Mesh sync (%s): " fmt "\n", sdata->name, ##args)
#else
#define msync_dbg(fmt, args...)   do { (void)(0); } while (0)
#endif

/* This is not in the standard.  It represents a tolerable tbtt drift below
 * which we do no TSF adjustment.
 */
#define TBTT_MINIMUM_ADJUSTMENT 10

struct sync_method {
	u8 method;
	struct ieee80211_mesh_sync_ops ops;
};

/**
 * mesh_peer_tbtt_adjusting - check if an mp is currently adjusting its TBTT
 *
 * @ie: information elements of a management frame from the mesh peer
 */
static bool mesh_peer_tbtt_adjusting(struct ieee802_11_elems *ie)
{
	return (ie->mesh_config->meshconf_cap &
	    MESHCONF_CAPAB_TBTT_ADJUSTING) != 0;
}

void mesh_sync_adjust_tbtt(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_if_mesh *ifmsh = &sdata->u.mesh;
	/* sdata->vif.bss_conf.beacon_int in 1024us units, 0.04% */
	u64 beacon_int_fraction = sdata->vif.bss_conf.beacon_int * 1024 / 2500;
	u64 tsf;
	u64 tsfdelta;

	spin_lock_bh(&ifmsh->sync_offset_lock);

	if (ifmsh->sync_offset_clockdrift_max < beacon_int_fraction) {
		msync_dbg("TBTT : max clockdrift=%lld; adjusting",
			(long long) ifmsh->sync_offset_clockdrift_max);
		tsfdelta = -ifmsh->sync_offset_clockdrift_max;
		ifmsh->sync_offset_clockdrift_max = 0;
	} else {
		msync_dbg("TBTT : max clockdrift=%lld; adjusting by %llu",
			(long long) ifmsh->sync_offset_clockdrift_max,
			(unsigned long long) beacon_int_fraction);
		tsfdelta = -beacon_int_fraction;
		ifmsh->sync_offset_clockdrift_max -= beacon_int_fraction;
	}

	tsf = drv_get_tsf(local, sdata);
	if (tsf != -1ULL)
		drv_set_tsf(local, sdata, tsf + tsfdelta);
	spin_unlock_bh(&ifmsh->sync_offset_lock);
}

static void mesh_sync_offset_rx_bcn_presp(struct ieee80211_sub_if_data *sdata,
				   u16 stype,
				   struct ieee80211_mgmt *mgmt,
				   struct ieee802_11_elems *elems,
				   struct ieee80211_rx_status *rx_status)
{
	struct ieee80211_if_mesh *ifmsh = &sdata->u.mesh;
	struct ieee80211_local *local = sdata->local;
	struct sta_info *sta;
	u64 t_t, t_r;

	WARN_ON(ifmsh->mesh_sp_id != IEEE80211_SYNC_METHOD_NEIGHBOR_OFFSET);

	/* standard mentions only beacons */
	if (stype != IEEE80211_STYPE_BEACON)
		return;

	/* The current tsf is a first approximation for the timestamp
	 * for the received beacon.  Further down we try to get a
	 * better value from the rx_status->mactime field if
	 * available. Also we have to call drv_get_tsf() before
	 * entering the rcu-read section.*/
	t_r = drv_get_tsf(local, sdata);

	rcu_read_lock();
	sta = sta_info_get(sdata, mgmt->sa);
	if (!sta)
		goto no_sync;

	/* check offset sync conditions (13.13.2.2.1)
	 *
	 * TODO also sync to
	 * dot11MeshNbrOffsetMaxNeighbor non-peer non-MBSS neighbors
	 */

	if (elems->mesh_config && mesh_peer_tbtt_adjusting(elems)) {
		clear_sta_flag(sta, WLAN_STA_TOFFSET_KNOWN);
		msync_dbg("STA %pM : is adjusting TBTT", sta->sta.addr);
		goto no_sync;
	}

	if (rx_status->flag & RX_FLAG_MACTIME_MPDU && rx_status->mactime) {
		/*
		 * The mactime is defined as the time the first data symbol
		 * of the frame hits the PHY, and the timestamp of the beacon
		 * is defined as "the time that the data symbol containing the
		 * first bit of the timestamp is transmitted to the PHY plus
		 * the transmitting STA's delays through its local PHY from the
		 * MAC-PHY interface to its interface with the WM" (802.11
		 * 11.1.2)
		 *
		 * T_r, in 13.13.2.2.2, is just defined as "the frame reception
		 * time" but we unless we interpret that time to be the same
		 * time of the beacon timestamp, the offset calculation will be
		 * off.  Below we adjust t_r to be "the time at which the first
		 * symbol of the timestamp element in the beacon is received".
		 * This correction depends on the rate.
		 *
		 * Based on similar code in ibss.c
		 */
		int rate;

		if (rx_status->flag & RX_FLAG_HT) {
			/* TODO:
			 * In principle there could be HT-beacons (Dual Beacon
			 * HT Operation options), but for now ignore them and
			 * just use the primary (i.e. non-HT) beacons for
			 * synchronization.
			 * */
			goto no_sync;
		} else
			rate = local->hw.wiphy->bands[rx_status->band]->
				bitrates[rx_status->rate_idx].bitrate;

		/* 24 bytes of header * 8 bits/byte *
		 * 10*(100 Kbps)/Mbps / rate (100 Kbps)*/
		t_r = rx_status->mactime + (24 * 8 * 10 / rate);
	}

	/* Timing offset calculation (see 13.13.2.2.2) */
	t_t = le64_to_cpu(mgmt->u.beacon.timestamp);
	sta->t_offset = t_t - t_r;

	if (test_sta_flag(sta, WLAN_STA_TOFFSET_KNOWN)) {
		s64 t_clockdrift = sta->t_offset_setpoint
				   - sta->t_offset;

		msync_dbg("STA %pM : sta->t_offset=%lld,"
			  " sta->t_offset_setpoint=%lld,"
			  " t_clockdrift=%lld",
			  sta->sta.addr,
			  (long long) sta->t_offset,
			  (long long)
			  sta->t_offset_setpoint,
			  (long long) t_clockdrift);
		rcu_read_unlock();

		spin_lock_bh(&ifmsh->sync_offset_lock);
		if (t_clockdrift >
		    ifmsh->sync_offset_clockdrift_max)
			ifmsh->sync_offset_clockdrift_max
				= t_clockdrift;
		spin_unlock_bh(&ifmsh->sync_offset_lock);

	} else {
		sta->t_offset_setpoint = sta->t_offset;
		set_sta_flag(sta, WLAN_STA_TOFFSET_KNOWN);
		msync_dbg("STA %pM : offset was invalid, "
			  " sta->t_offset=%lld",
			  sta->sta.addr,
			  (long long) sta->t_offset);
		rcu_read_unlock();
	}
	return;

no_sync:
	rcu_read_unlock();
}

static void mesh_sync_offset_adjust_tbtt(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_mesh *ifmsh = &sdata->u.mesh;

	WARN_ON(ifmsh->mesh_sp_id
		!= IEEE80211_SYNC_METHOD_NEIGHBOR_OFFSET);
	BUG_ON(!rcu_read_lock_held());

	spin_lock_bh(&ifmsh->sync_offset_lock);

	if (ifmsh->sync_offset_clockdrift_max >
		TBTT_MINIMUM_ADJUSTMENT) {
		/* Since ajusting the tsf here would
		 * require a possibly blocking call
		 * to the driver tsf setter, we punt
		 * the tsf adjustment to the mesh tasklet
		 */
		msync_dbg("TBTT : kicking off TBTT "
			  "adjustment with "
			  "clockdrift_max=%lld",
		  ifmsh->sync_offset_clockdrift_max);
		set_bit(MESH_WORK_DRIFT_ADJUST,
			&ifmsh->wrkq_flags);
	} else {
		msync_dbg("TBTT : max clockdrift=%lld; "
			  "too small to adjust",
			  (long long)
		       ifmsh->sync_offset_clockdrift_max);
		ifmsh->sync_offset_clockdrift_max = 0;
	}
	spin_unlock_bh(&ifmsh->sync_offset_lock);
}

static const u8 *mesh_get_vendor_oui(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_mesh *ifmsh = &sdata->u.mesh;
	u8 offset;

	if (!ifmsh->ie || !ifmsh->ie_len)
		return NULL;

	offset = ieee80211_ie_split_vendor(ifmsh->ie,
					ifmsh->ie_len, 0);

	if (!offset)
		return NULL;

	return ifmsh->ie + offset + 2;
}

static void mesh_sync_vendor_rx_bcn_presp(struct ieee80211_sub_if_data *sdata,
				   u16 stype,
				   struct ieee80211_mgmt *mgmt,
				   struct ieee802_11_elems *elems,
				   struct ieee80211_rx_status *rx_status)
{
	const u8 *oui;

	WARN_ON(sdata->u.mesh.mesh_sp_id != IEEE80211_SYNC_METHOD_VENDOR);
	msync_dbg("called mesh_sync_vendor_rx_bcn_presp");
	oui = mesh_get_vendor_oui(sdata);
	/*  here you would implement the vendor offset tracking for this oui */
}

static void mesh_sync_vendor_adjust_tbtt(struct ieee80211_sub_if_data *sdata)
{
	const u8 *oui;

	WARN_ON(sdata->u.mesh.mesh_sp_id != IEEE80211_SYNC_METHOD_VENDOR);
	msync_dbg("called mesh_sync_vendor_adjust_tbtt");
	oui = mesh_get_vendor_oui(sdata);
	/*  here you would implement the vendor tsf adjustment for this oui */
}

/* global variable */
static struct sync_method sync_methods[] = {
	{
		.method = IEEE80211_SYNC_METHOD_NEIGHBOR_OFFSET,
		.ops = {
			.rx_bcn_presp = &mesh_sync_offset_rx_bcn_presp,
			.adjust_tbtt = &mesh_sync_offset_adjust_tbtt,
		}
	},
	{
		.method = IEEE80211_SYNC_METHOD_VENDOR,
		.ops = {
			.rx_bcn_presp = &mesh_sync_vendor_rx_bcn_presp,
			.adjust_tbtt = &mesh_sync_vendor_adjust_tbtt,
		}
	},
};

struct ieee80211_mesh_sync_ops *ieee80211_mesh_sync_ops_get(u8 method)
{
	struct ieee80211_mesh_sync_ops *ops = NULL;
	u8 i;

	for (i = 0 ; i < ARRAY_SIZE(sync_methods); ++i) {
		if (sync_methods[i].method == method) {
			ops = &sync_methods[i].ops;
			break;
		}
	}
	return ops;
}
