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

/* This is not in the standard.  It represents a tolerable tbtt drift below
 * which we do no TSF adjustment.
 */
#define TOFFSET_MINIMUM_ADJUSTMENT 10

/* This is not in the standard. It is a margin added to the
 * Toffset setpoint to mitigate TSF overcorrection
 * introduced by TSF adjustment latency.
 */
#define TOFFSET_SET_MARGIN 20

/* This is not in the standard.  It represents the maximum Toffset jump above
 * which we'll invalidate the Toffset setpoint and choose a new setpoint.  This
 * could be, for instance, in case a neighbor is restarted and its TSF counter
 * reset.
 */
#define TOFFSET_MAXIMUM_ADJUSTMENT 30000		/* 30 ms */

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
			IEEE80211_MESHCONF_CAPAB_TBTT_ADJUSTING) != 0;
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
		msync_dbg(sdata, "TBTT : max clockdrift=%lld; adjusting\n",
			  (long long) ifmsh->sync_offset_clockdrift_max);
		tsfdelta = -ifmsh->sync_offset_clockdrift_max;
		ifmsh->sync_offset_clockdrift_max = 0;
	} else {
		msync_dbg(sdata, "TBTT : max clockdrift=%lld; adjusting by %llu\n",
			  (long long) ifmsh->sync_offset_clockdrift_max,
			  (unsigned long long) beacon_int_fraction);
		tsfdelta = -beacon_int_fraction;
		ifmsh->sync_offset_clockdrift_max -= beacon_int_fraction;
	}
	spin_unlock_bh(&ifmsh->sync_offset_lock);

	tsf = drv_get_tsf(local, sdata);
	if (tsf != -1ULL)
		drv_set_tsf(local, sdata, tsf + tsfdelta);
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

	/*
	 * Get time when timestamp field was received.  If we don't
	 * have rx timestamps, then use current tsf as an approximation.
	 * drv_get_tsf() must be called before entering the rcu-read
	 * section.
	 */
	if (ieee80211_have_rx_timestamp(rx_status))
		t_r = ieee80211_calculate_rx_timestamp(local, rx_status,
						       24 + 12 +
						       elems->total_len +
						       FCS_LEN,
						       24);
	else
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
		msync_dbg(sdata, "STA %pM : is adjusting TBTT\n",
			  sta->sta.addr);
		goto no_sync;
	}

	/* Timing offset calculation (see 13.13.2.2.2) */
	t_t = le64_to_cpu(mgmt->u.beacon.timestamp);
	sta->t_offset = t_t - t_r;

	if (test_sta_flag(sta, WLAN_STA_TOFFSET_KNOWN)) {
		s64 t_clockdrift = sta->t_offset_setpoint - sta->t_offset;
		msync_dbg(sdata,
			  "STA %pM : sta->t_offset=%lld, sta->t_offset_setpoint=%lld, t_clockdrift=%lld\n",
			  sta->sta.addr, (long long) sta->t_offset,
			  (long long) sta->t_offset_setpoint,
			  (long long) t_clockdrift);

		if (t_clockdrift > TOFFSET_MAXIMUM_ADJUSTMENT ||
		    t_clockdrift < -TOFFSET_MAXIMUM_ADJUSTMENT) {
			msync_dbg(sdata,
				  "STA %pM : t_clockdrift=%lld too large, setpoint reset\n",
				  sta->sta.addr,
				  (long long) t_clockdrift);
			clear_sta_flag(sta, WLAN_STA_TOFFSET_KNOWN);
			goto no_sync;
		}

		spin_lock_bh(&ifmsh->sync_offset_lock);
		if (t_clockdrift > ifmsh->sync_offset_clockdrift_max)
			ifmsh->sync_offset_clockdrift_max = t_clockdrift;
		spin_unlock_bh(&ifmsh->sync_offset_lock);
	} else {
		sta->t_offset_setpoint = sta->t_offset - TOFFSET_SET_MARGIN;
		set_sta_flag(sta, WLAN_STA_TOFFSET_KNOWN);
		msync_dbg(sdata,
			  "STA %pM : offset was invalid, sta->t_offset=%lld\n",
			  sta->sta.addr,
			  (long long) sta->t_offset);
	}

no_sync:
	rcu_read_unlock();
}

static void mesh_sync_offset_adjust_tbtt(struct ieee80211_sub_if_data *sdata,
					 struct beacon_data *beacon)
{
	struct ieee80211_if_mesh *ifmsh = &sdata->u.mesh;
	u8 cap;

	WARN_ON(ifmsh->mesh_sp_id != IEEE80211_SYNC_METHOD_NEIGHBOR_OFFSET);
	BUG_ON(!rcu_read_lock_held());
	cap = beacon->meshconf->meshconf_cap;

	spin_lock_bh(&ifmsh->sync_offset_lock);

	if (ifmsh->sync_offset_clockdrift_max > TOFFSET_MINIMUM_ADJUSTMENT) {
		/* Since ajusting the tsf here would
		 * require a possibly blocking call
		 * to the driver tsf setter, we punt
		 * the tsf adjustment to the mesh tasklet
		 */
		msync_dbg(sdata,
			  "TBTT : kicking off TBTT adjustment with clockdrift_max=%lld\n",
			  ifmsh->sync_offset_clockdrift_max);
		set_bit(MESH_WORK_DRIFT_ADJUST, &ifmsh->wrkq_flags);

		ifmsh->adjusting_tbtt = true;
	} else {
		msync_dbg(sdata,
			  "TBTT : max clockdrift=%lld; too small to adjust\n",
			  (long long)ifmsh->sync_offset_clockdrift_max);
		ifmsh->sync_offset_clockdrift_max = 0;

		ifmsh->adjusting_tbtt = false;
	}
	spin_unlock_bh(&ifmsh->sync_offset_lock);

	beacon->meshconf->meshconf_cap = ifmsh->adjusting_tbtt ?
			IEEE80211_MESHCONF_CAPAB_TBTT_ADJUSTING | cap :
			~IEEE80211_MESHCONF_CAPAB_TBTT_ADJUSTING & cap;
}

static const struct sync_method sync_methods[] = {
	{
		.method = IEEE80211_SYNC_METHOD_NEIGHBOR_OFFSET,
		.ops = {
			.rx_bcn_presp = &mesh_sync_offset_rx_bcn_presp,
			.adjust_tbtt = &mesh_sync_offset_adjust_tbtt,
		}
	},
};

const struct ieee80211_mesh_sync_ops *ieee80211_mesh_sync_ops_get(u8 method)
{
	int i;

	for (i = 0 ; i < ARRAY_SIZE(sync_methods); ++i) {
		if (sync_methods[i].method == method)
			return &sync_methods[i].ops;
	}
	return NULL;
}
