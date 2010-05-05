/*
 * mac80211 - channel management
 */

#include "ieee80211_i.h"

enum ieee80211_chan_mode
__ieee80211_get_channel_mode(struct ieee80211_local *local,
			     struct ieee80211_sub_if_data *ignore)
{
	struct ieee80211_sub_if_data *sdata;

	WARN_ON(!mutex_is_locked(&local->iflist_mtx));

	list_for_each_entry(sdata, &local->interfaces, list) {
		if (sdata == ignore)
			continue;

		if (!ieee80211_sdata_running(sdata))
			continue;

		if (sdata->vif.type == NL80211_IFTYPE_MONITOR)
			continue;

		if (sdata->vif.type == NL80211_IFTYPE_STATION &&
		    !sdata->u.mgd.associated)
			continue;

		if (sdata->vif.type == NL80211_IFTYPE_ADHOC) {
			if (!sdata->u.ibss.ssid_len)
				continue;
			if (!sdata->u.ibss.fixed_channel)
				return CHAN_MODE_HOPPING;
		}

		if (sdata->vif.type == NL80211_IFTYPE_AP &&
		    !sdata->u.ap.beacon)
			continue;

		return CHAN_MODE_FIXED;
	}

	return CHAN_MODE_UNDEFINED;
}

enum ieee80211_chan_mode
ieee80211_get_channel_mode(struct ieee80211_local *local,
			   struct ieee80211_sub_if_data *ignore)
{
	enum ieee80211_chan_mode mode;

	mutex_lock(&local->iflist_mtx);
	mode = __ieee80211_get_channel_mode(local, ignore);
	mutex_unlock(&local->iflist_mtx);

	return mode;
}
