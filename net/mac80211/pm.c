#include <net/mac80211.h>
#include <net/rtnetlink.h>

#include "ieee80211_i.h"
#include "mesh.h"
#include "driver-ops.h"
#include "led.h"

/* return value indicates whether the driver should be further notified */
static void ieee80211_quiesce(struct ieee80211_sub_if_data *sdata)
{
	switch (sdata->vif.type) {
	case NL80211_IFTYPE_STATION:
		ieee80211_sta_quiesce(sdata);
		break;
	case NL80211_IFTYPE_ADHOC:
		ieee80211_ibss_quiesce(sdata);
		break;
	case NL80211_IFTYPE_MESH_POINT:
		ieee80211_mesh_quiesce(sdata);
		break;
	default:
		break;
	}

	cancel_work_sync(&sdata->work);
}

int __ieee80211_suspend(struct ieee80211_hw *hw, struct cfg80211_wowlan *wowlan)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_sub_if_data *sdata;
	struct sta_info *sta;
	struct ieee80211_chanctx *ctx;

	if (!local->open_count)
		goto suspend;

	ieee80211_scan_cancel(local);

	if (hw->flags & IEEE80211_HW_AMPDU_AGGREGATION) {
		mutex_lock(&local->sta_mtx);
		list_for_each_entry(sta, &local->sta_list, list) {
			set_sta_flag(sta, WLAN_STA_BLOCK_BA);
			ieee80211_sta_tear_down_BA_sessions(
					sta, AGG_STOP_LOCAL_REQUEST);
		}
		mutex_unlock(&local->sta_mtx);
	}

	ieee80211_stop_queues_by_reason(hw,
			IEEE80211_QUEUE_STOP_REASON_SUSPEND);

	/* flush out all packets */
	synchronize_net();

	drv_flush(local, false);

	local->quiescing = true;
	/* make quiescing visible to timers everywhere */
	mb();

	flush_workqueue(local->workqueue);

	/* Don't try to run timers while suspended. */
	del_timer_sync(&local->sta_cleanup);

	 /*
	 * Note that this particular timer doesn't need to be
	 * restarted at resume.
	 */
	cancel_work_sync(&local->dynamic_ps_enable_work);
	del_timer_sync(&local->dynamic_ps_timer);

	local->wowlan = wowlan && local->open_count;
	if (local->wowlan) {
		int err = drv_suspend(local, wowlan);
		if (err < 0) {
			local->quiescing = false;
			local->wowlan = false;
			if (hw->flags & IEEE80211_HW_AMPDU_AGGREGATION) {
				mutex_lock(&local->sta_mtx);
				list_for_each_entry(sta,
						    &local->sta_list, list) {
					clear_sta_flag(sta, WLAN_STA_BLOCK_BA);
				}
				mutex_unlock(&local->sta_mtx);
			}
			ieee80211_wake_queues_by_reason(hw,
					IEEE80211_QUEUE_STOP_REASON_SUSPEND);
			return err;
		} else if (err > 0) {
			WARN_ON(err != 1);
			local->wowlan = false;
		} else {
			list_for_each_entry(sdata, &local->interfaces, list)
				if (ieee80211_sdata_running(sdata))
					ieee80211_quiesce(sdata);
			goto suspend;
		}
	}

	/* disable keys */
	list_for_each_entry(sdata, &local->interfaces, list)
		ieee80211_disable_keys(sdata);

	/* tear down aggregation sessions and remove STAs */
	mutex_lock(&local->sta_mtx);
	list_for_each_entry(sta, &local->sta_list, list) {
		if (sta->uploaded) {
			enum ieee80211_sta_state state;

			state = sta->sta_state;
			for (; state > IEEE80211_STA_NOTEXIST; state--)
				WARN_ON(drv_sta_state(local, sta->sdata, sta,
						      state, state - 1));
		}

		mesh_plink_quiesce(sta);
	}
	mutex_unlock(&local->sta_mtx);

	/* remove all interfaces */
	list_for_each_entry(sdata, &local->interfaces, list) {
		static u8 zero_addr[ETH_ALEN] = {};
		u32 changed = 0;

		if (!ieee80211_sdata_running(sdata))
			continue;

		switch (sdata->vif.type) {
		case NL80211_IFTYPE_AP_VLAN:
		case NL80211_IFTYPE_MONITOR:
			/* skip these */
			continue;
		case NL80211_IFTYPE_STATION:
			if (sdata->vif.bss_conf.assoc)
				changed = BSS_CHANGED_ASSOC |
					  BSS_CHANGED_BSSID |
					  BSS_CHANGED_IDLE;
			break;
		case NL80211_IFTYPE_AP:
		case NL80211_IFTYPE_ADHOC:
		case NL80211_IFTYPE_MESH_POINT:
			if (sdata->vif.bss_conf.enable_beacon)
				changed = BSS_CHANGED_BEACON_ENABLED;
			break;
		default:
			break;
		}

		ieee80211_quiesce(sdata);

		sdata->suspend_bss_conf = sdata->vif.bss_conf;
		memset(&sdata->vif.bss_conf, 0, sizeof(sdata->vif.bss_conf));
		sdata->vif.bss_conf.idle = true;
		if (sdata->suspend_bss_conf.bssid)
			sdata->vif.bss_conf.bssid = zero_addr;

		/* disable beaconing or remove association */
		ieee80211_bss_info_change_notify(sdata, changed);

		if (sdata->vif.type == NL80211_IFTYPE_AP &&
		    rcu_access_pointer(sdata->u.ap.beacon))
			drv_stop_ap(local, sdata);

		if (local->use_chanctx) {
			struct ieee80211_chanctx_conf *conf;

			mutex_lock(&local->chanctx_mtx);
			conf = rcu_dereference_protected(
					sdata->vif.chanctx_conf,
					lockdep_is_held(&local->chanctx_mtx));
			if (conf) {
				ctx = container_of(conf,
						   struct ieee80211_chanctx,
						   conf);
				drv_unassign_vif_chanctx(local, sdata, ctx);
			}

			mutex_unlock(&local->chanctx_mtx);
		}
		drv_remove_interface(local, sdata);
	}

	sdata = rtnl_dereference(local->monitor_sdata);
	if (sdata) {
		if (local->use_chanctx) {
			struct ieee80211_chanctx_conf *conf;

			mutex_lock(&local->chanctx_mtx);
			conf = rcu_dereference_protected(
					sdata->vif.chanctx_conf,
					lockdep_is_held(&local->chanctx_mtx));
			if (conf) {
				ctx = container_of(conf,
						   struct ieee80211_chanctx,
						   conf);
				drv_unassign_vif_chanctx(local, sdata, ctx);
			}

			mutex_unlock(&local->chanctx_mtx);
		}

		drv_remove_interface(local, sdata);
	}

	mutex_lock(&local->chanctx_mtx);
	list_for_each_entry(ctx, &local->chanctx_list, list)
		drv_remove_chanctx(local, ctx);
	mutex_unlock(&local->chanctx_mtx);

	/* stop hardware - this must stop RX */
	if (local->open_count)
		ieee80211_stop_device(local);

 suspend:
	local->suspended = true;
	/* need suspended to be visible before quiescing is false */
	barrier();
	local->quiescing = false;

	return 0;
}

/*
 * __ieee80211_resume() is a static inline which just calls
 * ieee80211_reconfig(), which is also needed for hardware
 * hang/firmware failure/etc. recovery.
 */

void ieee80211_report_wowlan_wakeup(struct ieee80211_vif *vif,
				    struct cfg80211_wowlan_wakeup *wakeup,
				    gfp_t gfp)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);

	cfg80211_report_wowlan_wakeup(&sdata->wdev, wakeup, gfp);
}
EXPORT_SYMBOL(ieee80211_report_wowlan_wakeup);
