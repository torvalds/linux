#include <net/mac80211.h>
#include <net/rtnetlink.h>

#include "ieee80211_i.h"
#include "mesh.h"
#include "driver-ops.h"
#include "led.h"

int __ieee80211_suspend(struct ieee80211_hw *hw)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_sub_if_data *sdata;
	struct sta_info *sta;

	ieee80211_scan_cancel(local);

	ieee80211_stop_queues_by_reason(hw,
			IEEE80211_QUEUE_STOP_REASON_SUSPEND);

	/* flush out all packets */
	synchronize_net();

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

	/* disable keys */
	list_for_each_entry(sdata, &local->interfaces, list)
		ieee80211_disable_keys(sdata);

	/* tear down aggregation sessions and remove STAs */
	mutex_lock(&local->sta_mtx);
	list_for_each_entry(sta, &local->sta_list, list) {
		if (hw->flags & IEEE80211_HW_AMPDU_AGGREGATION) {
			set_sta_flags(sta, WLAN_STA_BLOCK_BA);
			ieee80211_sta_tear_down_BA_sessions(sta, true);
		}

		if (sta->uploaded) {
			sdata = sta->sdata;
			if (sdata->vif.type == NL80211_IFTYPE_AP_VLAN)
				sdata = container_of(sdata->bss,
					     struct ieee80211_sub_if_data,
					     u.ap);

			drv_sta_remove(local, sdata, &sta->sta);
		}

		mesh_plink_quiesce(sta);
	}
	mutex_unlock(&local->sta_mtx);

	/* remove all interfaces */
	list_for_each_entry(sdata, &local->interfaces, list) {
		cancel_work_sync(&sdata->work);

		switch(sdata->vif.type) {
		case NL80211_IFTYPE_STATION:
			ieee80211_sta_quiesce(sdata);
			break;
		case NL80211_IFTYPE_ADHOC:
			ieee80211_ibss_quiesce(sdata);
			break;
		case NL80211_IFTYPE_MESH_POINT:
			ieee80211_mesh_quiesce(sdata);
			break;
		case NL80211_IFTYPE_AP_VLAN:
		case NL80211_IFTYPE_MONITOR:
			/* don't tell driver about this */
			continue;
		default:
			break;
		}

		if (!ieee80211_sdata_running(sdata))
			continue;

		/* disable beaconing */
		ieee80211_bss_info_change_notify(sdata,
			BSS_CHANGED_BEACON_ENABLED);

		drv_remove_interface(local, &sdata->vif);
	}

	/* stop hardware - this must stop RX */
	if (local->open_count)
		ieee80211_stop_device(local);

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
