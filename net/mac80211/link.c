// SPDX-License-Identifier: GPL-2.0-only
/*
 * MLO link handling
 *
 * Copyright (C) 2022-2023 Intel Corporation
 */
#include <linux/slab.h>
#include <linux/kernel.h>
#include <net/mac80211.h>
#include "ieee80211_i.h"
#include "driver-ops.h"
#include "key.h"
#include "debugfs_netdev.h"

void ieee80211_link_setup(struct ieee80211_link_data *link)
{
	if (link->sdata->vif.type == NL80211_IFTYPE_STATION)
		ieee80211_mgd_setup_link(link);
}

void ieee80211_link_init(struct ieee80211_sub_if_data *sdata,
			 int link_id,
			 struct ieee80211_link_data *link,
			 struct ieee80211_bss_conf *link_conf)
{
	bool deflink = link_id < 0;

	if (link_id < 0)
		link_id = 0;

	rcu_assign_pointer(sdata->vif.link_conf[link_id], link_conf);
	rcu_assign_pointer(sdata->link[link_id], link);

	link->sdata = sdata;
	link->link_id = link_id;
	link->conf = link_conf;
	link_conf->link_id = link_id;
	link_conf->vif = &sdata->vif;

	INIT_WORK(&link->csa_finalize_work,
		  ieee80211_csa_finalize_work);
	INIT_WORK(&link->color_change_finalize_work,
		  ieee80211_color_change_finalize_work);
	INIT_DELAYED_WORK(&link->color_collision_detect_work,
			  ieee80211_color_collision_detection_work);
	INIT_LIST_HEAD(&link->assigned_chanctx_list);
	INIT_LIST_HEAD(&link->reserved_chanctx_list);
	INIT_DELAYED_WORK(&link->dfs_cac_timer_work,
			  ieee80211_dfs_cac_timer_work);

	if (!deflink) {
		switch (sdata->vif.type) {
		case NL80211_IFTYPE_AP:
			ether_addr_copy(link_conf->addr,
					sdata->wdev.links[link_id].addr);
			link_conf->bssid = link_conf->addr;
			WARN_ON(!(sdata->wdev.valid_links & BIT(link_id)));
			break;
		case NL80211_IFTYPE_STATION:
			/* station sets the bssid in ieee80211_mgd_setup_link */
			break;
		default:
			WARN_ON(1);
		}

		ieee80211_link_debugfs_add(link);
	}
}

void ieee80211_link_stop(struct ieee80211_link_data *link)
{
	if (link->sdata->vif.type == NL80211_IFTYPE_STATION)
		ieee80211_mgd_stop_link(link);

	cancel_delayed_work_sync(&link->color_collision_detect_work);
	ieee80211_link_release_channel(link);
}

struct link_container {
	struct ieee80211_link_data data;
	struct ieee80211_bss_conf conf;
};

static void ieee80211_tear_down_links(struct ieee80211_sub_if_data *sdata,
				      struct link_container **links, u16 mask)
{
	struct ieee80211_link_data *link;
	LIST_HEAD(keys);
	unsigned int link_id;

	for (link_id = 0; link_id < IEEE80211_MLD_MAX_NUM_LINKS; link_id++) {
		if (!(mask & BIT(link_id)))
			continue;
		link = &links[link_id]->data;
		if (link_id == 0 && !link)
			link = &sdata->deflink;
		if (WARN_ON(!link))
			continue;
		ieee80211_remove_link_keys(link, &keys);
		ieee80211_link_debugfs_remove(link);
		ieee80211_link_stop(link);
	}

	synchronize_rcu();

	ieee80211_free_key_list(sdata->local, &keys);
}

static void ieee80211_free_links(struct ieee80211_sub_if_data *sdata,
				 struct link_container **links)
{
	unsigned int link_id;

	for (link_id = 0; link_id < IEEE80211_MLD_MAX_NUM_LINKS; link_id++)
		kfree(links[link_id]);
}

static int ieee80211_check_dup_link_addrs(struct ieee80211_sub_if_data *sdata)
{
	unsigned int i, j;

	for (i = 0; i < IEEE80211_MLD_MAX_NUM_LINKS; i++) {
		struct ieee80211_link_data *link1;

		link1 = sdata_dereference(sdata->link[i], sdata);
		if (!link1)
			continue;
		for (j = i + 1; j < IEEE80211_MLD_MAX_NUM_LINKS; j++) {
			struct ieee80211_link_data *link2;

			link2 = sdata_dereference(sdata->link[j], sdata);
			if (!link2)
				continue;

			if (ether_addr_equal(link1->conf->addr,
					     link2->conf->addr))
				return -EALREADY;
		}
	}

	return 0;
}

static void ieee80211_set_vif_links_bitmaps(struct ieee80211_sub_if_data *sdata,
					    u16 links)
{
	sdata->vif.valid_links = links;

	if (!links) {
		sdata->vif.active_links = 0;
		return;
	}

	switch (sdata->vif.type) {
	case NL80211_IFTYPE_AP:
		/* in an AP all links are always active */
		sdata->vif.active_links = links;
		break;
	case NL80211_IFTYPE_STATION:
		if (sdata->vif.active_links)
			break;
		WARN_ON(hweight16(links) > 1);
		sdata->vif.active_links = links;
		break;
	default:
		WARN_ON(1);
	}
}

static int ieee80211_vif_update_links(struct ieee80211_sub_if_data *sdata,
				      struct link_container **to_free,
				      u16 new_links)
{
	u16 old_links = sdata->vif.valid_links;
	u16 old_active = sdata->vif.active_links;
	unsigned long add = new_links & ~old_links;
	unsigned long rem = old_links & ~new_links;
	unsigned int link_id;
	int ret;
	struct link_container *links[IEEE80211_MLD_MAX_NUM_LINKS] = {}, *link;
	struct ieee80211_bss_conf *old[IEEE80211_MLD_MAX_NUM_LINKS];
	struct ieee80211_link_data *old_data[IEEE80211_MLD_MAX_NUM_LINKS];
	bool use_deflink = old_links == 0; /* set for error case */

	sdata_assert_lock(sdata);

	memset(to_free, 0, sizeof(links));

	if (old_links == new_links)
		return 0;

	/* if there were no old links, need to clear the pointers to deflink */
	if (!old_links)
		rem |= BIT(0);

	/* allocate new link structures first */
	for_each_set_bit(link_id, &add, IEEE80211_MLD_MAX_NUM_LINKS) {
		link = kzalloc(sizeof(*link), GFP_KERNEL);
		if (!link) {
			ret = -ENOMEM;
			goto free;
		}
		links[link_id] = link;
	}

	/* keep track of the old pointers for the driver */
	BUILD_BUG_ON(sizeof(old) != sizeof(sdata->vif.link_conf));
	memcpy(old, sdata->vif.link_conf, sizeof(old));
	/* and for us in error cases */
	BUILD_BUG_ON(sizeof(old_data) != sizeof(sdata->link));
	memcpy(old_data, sdata->link, sizeof(old_data));

	/* grab old links to free later */
	for_each_set_bit(link_id, &rem, IEEE80211_MLD_MAX_NUM_LINKS) {
		if (rcu_access_pointer(sdata->link[link_id]) != &sdata->deflink) {
			/*
			 * we must have allocated the data through this path so
			 * we know we can free both at the same time
			 */
			to_free[link_id] = container_of(rcu_access_pointer(sdata->link[link_id]),
							typeof(*links[link_id]),
							data);
		}

		RCU_INIT_POINTER(sdata->link[link_id], NULL);
		RCU_INIT_POINTER(sdata->vif.link_conf[link_id], NULL);
	}

	/* link them into data structures */
	for_each_set_bit(link_id, &add, IEEE80211_MLD_MAX_NUM_LINKS) {
		WARN_ON(!use_deflink &&
			rcu_access_pointer(sdata->link[link_id]) == &sdata->deflink);

		link = links[link_id];
		ieee80211_link_init(sdata, link_id, &link->data, &link->conf);
		ieee80211_link_setup(&link->data);
	}

	if (new_links == 0)
		ieee80211_link_init(sdata, -1, &sdata->deflink,
				    &sdata->vif.bss_conf);

	ret = ieee80211_check_dup_link_addrs(sdata);
	if (!ret) {
		/* for keys we will not be able to undo this */
		ieee80211_tear_down_links(sdata, to_free, rem);

		ieee80211_set_vif_links_bitmaps(sdata, new_links);

		/* tell the driver */
		ret = drv_change_vif_links(sdata->local, sdata,
					   old_links & old_active,
					   new_links & sdata->vif.active_links,
					   old);
	}

	if (ret) {
		/* restore config */
		memcpy(sdata->link, old_data, sizeof(old_data));
		memcpy(sdata->vif.link_conf, old, sizeof(old));
		ieee80211_set_vif_links_bitmaps(sdata, old_links);
		/* and free (only) the newly allocated links */
		memset(to_free, 0, sizeof(links));
		goto free;
	}

	/* use deflink/bss_conf again if and only if there are no more links */
	use_deflink = new_links == 0;

	goto deinit;
free:
	/* if we failed during allocation, only free all */
	for (link_id = 0; link_id < IEEE80211_MLD_MAX_NUM_LINKS; link_id++) {
		kfree(links[link_id]);
		links[link_id] = NULL;
	}
deinit:
	if (use_deflink)
		ieee80211_link_init(sdata, -1, &sdata->deflink,
				    &sdata->vif.bss_conf);
	return ret;
}

int ieee80211_vif_set_links(struct ieee80211_sub_if_data *sdata,
			    u16 new_links)
{
	struct link_container *links[IEEE80211_MLD_MAX_NUM_LINKS];
	int ret;

	ret = ieee80211_vif_update_links(sdata, links, new_links);
	ieee80211_free_links(sdata, links);

	return ret;
}

void ieee80211_vif_clear_links(struct ieee80211_sub_if_data *sdata)
{
	struct link_container *links[IEEE80211_MLD_MAX_NUM_LINKS];

	/*
	 * The locking here is different because when we free links
	 * in the station case we need to be able to cancel_work_sync()
	 * something that also takes the lock.
	 */

	sdata_lock(sdata);
	ieee80211_vif_update_links(sdata, links, 0);
	sdata_unlock(sdata);

	ieee80211_free_links(sdata, links);
}

static int _ieee80211_set_active_links(struct ieee80211_sub_if_data *sdata,
				       u16 active_links)
{
	struct ieee80211_bss_conf *link_confs[IEEE80211_MLD_MAX_NUM_LINKS];
	struct ieee80211_local *local = sdata->local;
	u16 old_active = sdata->vif.active_links;
	unsigned long rem = old_active & ~active_links;
	unsigned long add = active_links & ~old_active;
	struct sta_info *sta;
	unsigned int link_id;
	int ret, i;

	if (!ieee80211_sdata_running(sdata))
		return -ENETDOWN;

	if (sdata->vif.type != NL80211_IFTYPE_STATION)
		return -EINVAL;

	/* cannot activate links that don't exist */
	if (active_links & ~sdata->vif.valid_links)
		return -EINVAL;

	/* nothing to do */
	if (old_active == active_links)
		return 0;

	for (i = 0; i < IEEE80211_MLD_MAX_NUM_LINKS; i++)
		link_confs[i] = sdata_dereference(sdata->vif.link_conf[i],
						  sdata);

	if (add) {
		sdata->vif.active_links |= active_links;
		ret = drv_change_vif_links(local, sdata,
					   old_active,
					   sdata->vif.active_links,
					   link_confs);
		if (ret) {
			sdata->vif.active_links = old_active;
			return ret;
		}
	}

	for_each_set_bit(link_id, &rem, IEEE80211_MLD_MAX_NUM_LINKS) {
		struct ieee80211_link_data *link;

		link = sdata_dereference(sdata->link[link_id], sdata);

		/* FIXME: kill TDLS connections on the link */

		ieee80211_link_release_channel(link);
	}

	list_for_each_entry(sta, &local->sta_list, list) {
		if (sdata != sta->sdata)
			continue;

		/* this is very temporary, but do it anyway */
		__ieee80211_sta_recalc_aggregates(sta,
						  old_active | active_links);

		ret = drv_change_sta_links(local, sdata, &sta->sta,
					   old_active,
					   old_active | active_links);
		WARN_ON_ONCE(ret);
	}

	ret = ieee80211_key_switch_links(sdata, rem, add);
	WARN_ON_ONCE(ret);

	list_for_each_entry(sta, &local->sta_list, list) {
		if (sdata != sta->sdata)
			continue;

		__ieee80211_sta_recalc_aggregates(sta, active_links);

		ret = drv_change_sta_links(local, sdata, &sta->sta,
					   old_active | active_links,
					   active_links);
		WARN_ON_ONCE(ret);

		/*
		 * Do it again, just in case - the driver might very
		 * well have called ieee80211_sta_recalc_aggregates()
		 * from there when filling in the new links, which
		 * would set it wrong since the vif's active links are
		 * not switched yet...
		 */
		__ieee80211_sta_recalc_aggregates(sta, active_links);
	}

	for_each_set_bit(link_id, &add, IEEE80211_MLD_MAX_NUM_LINKS) {
		struct ieee80211_link_data *link;

		link = sdata_dereference(sdata->link[link_id], sdata);

		ret = ieee80211_link_use_channel(link, &link->conf->chandef,
						 IEEE80211_CHANCTX_SHARED);
		WARN_ON_ONCE(ret);

		ieee80211_mgd_set_link_qos_params(link);
		ieee80211_link_info_change_notify(sdata, link,
						  BSS_CHANGED_ERP_CTS_PROT |
						  BSS_CHANGED_ERP_PREAMBLE |
						  BSS_CHANGED_ERP_SLOT |
						  BSS_CHANGED_HT |
						  BSS_CHANGED_BASIC_RATES |
						  BSS_CHANGED_BSSID |
						  BSS_CHANGED_CQM |
						  BSS_CHANGED_QOS |
						  BSS_CHANGED_TXPOWER |
						  BSS_CHANGED_BANDWIDTH |
						  BSS_CHANGED_TWT |
						  BSS_CHANGED_HE_OBSS_PD |
						  BSS_CHANGED_HE_BSS_COLOR);
	}

	old_active = sdata->vif.active_links;
	sdata->vif.active_links = active_links;

	if (rem) {
		ret = drv_change_vif_links(local, sdata, old_active,
					   active_links, link_confs);
		WARN_ON_ONCE(ret);
	}

	return 0;
}

int ieee80211_set_active_links(struct ieee80211_vif *vif, u16 active_links)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);
	struct ieee80211_local *local = sdata->local;
	u16 old_active;
	int ret;

	sdata_lock(sdata);
	mutex_lock(&local->sta_mtx);
	mutex_lock(&local->mtx);
	mutex_lock(&local->key_mtx);
	old_active = sdata->vif.active_links;
	if (old_active & active_links) {
		/*
		 * if there's at least one link that stays active across
		 * the change then switch to it (to those) first, and
		 * then enable the additional links
		 */
		ret = _ieee80211_set_active_links(sdata,
						  old_active & active_links);
		if (!ret)
			ret = _ieee80211_set_active_links(sdata, active_links);
	} else {
		/* otherwise switch directly */
		ret = _ieee80211_set_active_links(sdata, active_links);
	}
	mutex_unlock(&local->key_mtx);
	mutex_unlock(&local->mtx);
	mutex_unlock(&local->sta_mtx);
	sdata_unlock(sdata);

	return ret;
}
EXPORT_SYMBOL_GPL(ieee80211_set_active_links);

void ieee80211_set_active_links_async(struct ieee80211_vif *vif,
				      u16 active_links)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);

	if (!ieee80211_sdata_running(sdata))
		return;

	if (sdata->vif.type != NL80211_IFTYPE_STATION)
		return;

	/* cannot activate links that don't exist */
	if (active_links & ~sdata->vif.valid_links)
		return;

	/* nothing to do */
	if (sdata->vif.active_links == active_links)
		return;

	sdata->desired_active_links = active_links;
	schedule_work(&sdata->activate_links_work);
}
EXPORT_SYMBOL_GPL(ieee80211_set_active_links_async);
