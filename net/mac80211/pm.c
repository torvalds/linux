#include <net/mac80211.h>
#include <net/rtnetlink.h>

#include "ieee80211_i.h"
#include "driver-ops.h"
#include "led.h"

int __ieee80211_suspend(struct ieee80211_hw *hw)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_if_init_conf conf;
	struct sta_info *sta;
	unsigned long flags;

	ieee80211_stop_queues_by_reason(hw,
			IEEE80211_QUEUE_STOP_REASON_SUSPEND);

	flush_workqueue(local->hw.workqueue);

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

	/* remove STAs */
	if (local->ops->sta_notify) {
		spin_lock_irqsave(&local->sta_lock, flags);
		list_for_each_entry(sta, &local->sta_list, list) {
			if (sdata->vif.type == NL80211_IFTYPE_AP_VLAN)
				sdata = container_of(sdata->bss,
					     struct ieee80211_sub_if_data,
					     u.ap);

			drv_sta_notify(local, &sdata->vif, STA_NOTIFY_REMOVE,
				       &sta->sta);
		}
		spin_unlock_irqrestore(&local->sta_lock, flags);
	}

	/* remove all interfaces */
	list_for_each_entry(sdata, &local->interfaces, list) {
		if (sdata->vif.type != NL80211_IFTYPE_AP_VLAN &&
		    sdata->vif.type != NL80211_IFTYPE_MONITOR &&
		    netif_running(sdata->dev)) {
			conf.vif = &sdata->vif;
			conf.type = sdata->vif.type;
			conf.mac_addr = sdata->dev->dev_addr;
			drv_remove_interface(local, &conf);
		}
	}

	/* flush again, in case driver queued work */
	flush_workqueue(local->hw.workqueue);

	/* stop hardware */
	if (local->open_count) {
		ieee80211_led_radio(local, false);
		drv_stop(local);
	}
	return 0;
}

/*
 * __ieee80211_resume() is a static inline which just calls
 * ieee80211_reconfig(), which is also needed for hardware
 * hang/firmware failure/etc. recovery.
 */
