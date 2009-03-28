#include <net/mac80211.h>
#include <net/rtnetlink.h>

#include "ieee80211_i.h"
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

			local->ops->sta_notify(hw, &sdata->vif,
				STA_NOTIFY_REMOVE, &sta->sta);
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
			local->ops->remove_interface(hw, &conf);
		}
	}

	/* flush again, in case driver queued work */
	flush_workqueue(local->hw.workqueue);

	/* stop hardware */
	if (local->open_count) {
		ieee80211_led_radio(local, false);
		local->ops->stop(hw);
	}
	return 0;
}

int __ieee80211_resume(struct ieee80211_hw *hw)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_if_init_conf conf;
	struct sta_info *sta;
	unsigned long flags;
	int res;

	/* restart hardware */
	if (local->open_count) {
		res = local->ops->start(hw);

		ieee80211_led_radio(local, hw->conf.radio_enabled);
	}

	/* add interfaces */
	list_for_each_entry(sdata, &local->interfaces, list) {
		if (sdata->vif.type != NL80211_IFTYPE_AP_VLAN &&
		    sdata->vif.type != NL80211_IFTYPE_MONITOR &&
		    netif_running(sdata->dev)) {
			conf.vif = &sdata->vif;
			conf.type = sdata->vif.type;
			conf.mac_addr = sdata->dev->dev_addr;
			res = local->ops->add_interface(hw, &conf);
		}
	}

	/* add STAs back */
	if (local->ops->sta_notify) {
		spin_lock_irqsave(&local->sta_lock, flags);
		list_for_each_entry(sta, &local->sta_list, list) {
			if (sdata->vif.type == NL80211_IFTYPE_AP_VLAN)
				sdata = container_of(sdata->bss,
					     struct ieee80211_sub_if_data,
					     u.ap);

			local->ops->sta_notify(hw, &sdata->vif,
				STA_NOTIFY_ADD, &sta->sta);
		}
		spin_unlock_irqrestore(&local->sta_lock, flags);
	}

	/* Clear Suspend state so that ADDBA requests can be processed */

	rcu_read_lock();

	if (hw->flags & IEEE80211_HW_AMPDU_AGGREGATION) {
		list_for_each_entry_rcu(sta, &local->sta_list, list) {
			clear_sta_flags(sta, WLAN_STA_SUSPEND);
		}
	}

	rcu_read_unlock();

	/* add back keys */
	list_for_each_entry(sdata, &local->interfaces, list)
		if (netif_running(sdata->dev))
			ieee80211_enable_keys(sdata);

	/* setup RTS threshold */
	if (local->ops->set_rts_threshold)
		local->ops->set_rts_threshold(hw, local->rts_threshold);

	/* reconfigure hardware */
	ieee80211_hw_config(local, ~0);

	netif_addr_lock_bh(local->mdev);
	ieee80211_configure_filter(local);
	netif_addr_unlock_bh(local->mdev);

	/* Finally also reconfigure all the BSS information */
	list_for_each_entry(sdata, &local->interfaces, list) {
		u32 changed = ~0;
		if (!netif_running(sdata->dev))
			continue;
		switch (sdata->vif.type) {
		case NL80211_IFTYPE_STATION:
			/* disable beacon change bits */
			changed &= ~IEEE80211_IFCC_BEACON;
			/* fall through */
		case NL80211_IFTYPE_ADHOC:
		case NL80211_IFTYPE_AP:
		case NL80211_IFTYPE_MESH_POINT:
			WARN_ON(ieee80211_if_config(sdata, changed));
			ieee80211_bss_info_change_notify(sdata, ~0);
			break;
		case NL80211_IFTYPE_WDS:
			break;
		case NL80211_IFTYPE_AP_VLAN:
		case NL80211_IFTYPE_MONITOR:
			/* ignore virtual */
			break;
		case NL80211_IFTYPE_UNSPECIFIED:
		case __NL80211_IFTYPE_AFTER_LAST:
			WARN_ON(1);
			break;
		}
	}

	ieee80211_wake_queues_by_reason(hw,
			IEEE80211_QUEUE_STOP_REASON_SUSPEND);

	return 0;
}
