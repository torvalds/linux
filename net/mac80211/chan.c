/*
 * mac80211 - channel management
 */

#include <linux/nl80211.h>
#include "ieee80211_i.h"

static enum ieee80211_chan_mode
__ieee80211_get_channel_mode(struct ieee80211_local *local,
			     struct ieee80211_sub_if_data *ignore)
{
	struct ieee80211_sub_if_data *sdata;

	lockdep_assert_held(&local->iflist_mtx);

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

bool ieee80211_set_channel_type(struct ieee80211_local *local,
				struct ieee80211_sub_if_data *sdata,
				enum nl80211_channel_type chantype)
{
	struct ieee80211_sub_if_data *tmp;
	enum nl80211_channel_type superchan = NL80211_CHAN_NO_HT;
	bool result;

	mutex_lock(&local->iflist_mtx);

	list_for_each_entry(tmp, &local->interfaces, list) {
		if (tmp == sdata)
			continue;

		if (!ieee80211_sdata_running(tmp))
			continue;

		switch (tmp->vif.bss_conf.channel_type) {
		case NL80211_CHAN_NO_HT:
		case NL80211_CHAN_HT20:
			superchan = tmp->vif.bss_conf.channel_type;
			break;
		case NL80211_CHAN_HT40PLUS:
			WARN_ON(superchan == NL80211_CHAN_HT40MINUS);
			superchan = NL80211_CHAN_HT40PLUS;
			break;
		case NL80211_CHAN_HT40MINUS:
			WARN_ON(superchan == NL80211_CHAN_HT40PLUS);
			superchan = NL80211_CHAN_HT40MINUS;
			break;
		}
	}

	switch (superchan) {
	case NL80211_CHAN_NO_HT:
	case NL80211_CHAN_HT20:
		/*
		 * allow any change that doesn't go to no-HT
		 * (if it already is no-HT no change is needed)
		 */
		if (chantype == NL80211_CHAN_NO_HT)
			break;
		superchan = chantype;
		break;
	case NL80211_CHAN_HT40PLUS:
	case NL80211_CHAN_HT40MINUS:
		/* allow smaller bandwidth and same */
		if (chantype == NL80211_CHAN_NO_HT)
			break;
		if (chantype == NL80211_CHAN_HT20)
			break;
		if (superchan == chantype)
			break;
		result = false;
		goto out;
	}

	local->_oper_channel_type = superchan;

	if (sdata)
		sdata->vif.bss_conf.channel_type = chantype;

	result = true;
 out:
	mutex_unlock(&local->iflist_mtx);

	return result;
}
