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
	struct ieee80211_if_init_conf conf;
	struct sta_info *sta;
	unsigned long flags;

	ieee80211_scan_cancel(local);

	ieee80211_stop_queues_by_reason(hw,
			IEEE80211_QUEUE_STOP_REASON_SUSPEND);

	/* flush out all packets */
	synchronize_net();

	local->quiescing = true;
	/* make quiescing visible to timers everywhere */
	mb();

	flush_workqueue(local->hw.workqueue);

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

	/* Tear down aggregation sessions */

	rcu_read_lock();

	if (hw->flags & IEEE80211_HW_AMPDU_AGGREGATION) {
		list_for_each_entry_rcu(sta, &local->sta_list, list) {
			set_sta_flags(sta, WLAN_STA_SUSPEND);
			ieee80211_sta_tear_down_BA_sessions(sta);
		}
	}

	rcu_read_unlock();

	/* flush again, in case driver queued work */
	flush_workqueue(local->hw.workqueue);

	/* stop hardware - this must stop RX */
	if (local->open_count) {
		ieee80211_led_radio(local, false);
		drv_stop(local);
	}

	/* remove STAs */
	spin_lock_irqsave(&local->sta_lock, flags);
	list_for_each_entry(sta, &local->sta_list, list) {
		if (local->ops->sta_notify) {
			sdata = sta->sdata;
			if (sdata->vif.type == NL80211_IFTYPE_AP_VLAN)
				sdata = container_of(sdata->bss,
					     struct ieee80211_sub_if_data,
					     u.ap);

			drv_sta_notify(local, &sdata->vif, STA_NOTIFY_REMOVE,
				       &sta->sta);
		}

		mesh_plink_quiesce(sta);
	}
	spin_unlock_irqrestore(&local->sta_lock, flags);

	/* remove all interfaces */
	list_for_each_entry(sdata, &local->interfaces, list) {
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

		if (!netif_running(sdata->dev))
			continue;

		conf.vif = &sdata->vif;
		conf.type = sdata->vif.type;
		conf.mac_addr = sdata->dev->dev_addr;
		drv_remove_interface(local, &conf);
	}

	local->suspended = true;
	local->quiescing = false;

	return 0;
}

/*
 * __ieee80211_resume() is a static inline which just calls
 * ieee80211_reconfig(), which is also needed for hardware
 * hang/firmware failure/etc. recovery.
 */
