// SPDX-License-Identifier: GPL-2.0
/*
 * cfg80211 scan result handling
 *
 * Copyright 2008 Johannes Berg <johannes@sipsolutions.net>
 * Copyright 2013-2014  Intel Mobile Communications GmbH
 * Copyright 2016	Intel Deutschland GmbH
 * Copyright (C) 2018-2021 Intel Corporation
 */
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/wireless.h>
#include <linux/nl80211.h>
#include <linux/etherdevice.h>
#include <linux/crc32.h>
#include <linux/bitfield.h>
#include <net/arp.h>
#include <net/cfg80211.h>
#include <net/cfg80211-wext.h>
#include <net/iw_handler.h>
#include "core.h"
#include "nl80211.h"
#include "wext-compat.h"
#include "rdev-ops.h"

/**
 * DOC: BSS tree/list structure
 *
 * At the top level, the BSS list is kept in both a list in each
 * registered device (@bss_list) as well as an RB-tree for faster
 * lookup. In the RB-tree, entries can be looked up using their
 * channel, MESHID, MESHCONF (for MBSSes) or channel, BSSID, SSID
 * for other BSSes.
 *
 * Due to the possibility of hidden SSIDs, there's a second level
 * structure, the "hidden_list" and "hidden_beacon_bss" pointer.
 * The hidden_list connects all BSSes belonging to a single AP
 * that has a hidden SSID, and connects beacon and probe response
 * entries. For a probe response entry for a hidden SSID, the
 * hidden_beacon_bss pointer points to the BSS struct holding the
 * beacon's information.
 *
 * Reference counting is done for all these references except for
 * the hidden_list, so that a beacon BSS struct that is otherwise
 * not referenced has one reference for being on the bss_list and
 * one for each probe response entry that points to it using the
 * hidden_beacon_bss pointer. When a BSS struct that has such a
 * pointer is get/put, the refcount update is also propagated to
 * the referenced struct, this ensure that it cannot get removed
 * while somebody is using the probe response version.
 *
 * Note that the hidden_beacon_bss pointer never changes, due to
 * the reference counting. Therefore, no locking is needed for
 * it.
 *
 * Also note that the hidden_beacon_bss pointer is only relevant
 * if the driver uses something other than the IEs, e.g. private
 * data stored in the BSS struct, since the beacon IEs are
 * also linked into the probe response struct.
 */

/*
 * Limit the number of BSS entries stored in mac80211. Each one is
 * a bit over 4k at most, so this limits to roughly 4-5M of memory.
 * If somebody wants to really attack this though, they'd likely
 * use small beacons, and only one type of frame, limiting each of
 * the entries to a much smaller size (in order to generate more
 * entries in total, so overhead is bigger.)
 */
static int bss_entries_limit = 1000;
module_param(bss_entries_limit, int, 0644);
MODULE_PARM_DESC(bss_entries_limit,
                 "limit to number of scan BSS entries (per wiphy, default 1000)");

#define IEEE80211_SCAN_RESULT_EXPIRE	(30 * HZ)

/**
 * struct cfg80211_colocated_ap - colocated AP information
 *
 * @list: linked list to all colocated aPS
 * @bssid: BSSID of the reported AP
 * @ssid: SSID of the reported AP
 * @ssid_len: length of the ssid
 * @center_freq: frequency the reported AP is on
 * @unsolicited_probe: the reported AP is part of an ESS, where all the APs
 *	that operate in the same channel as the reported AP and that might be
 *	detected by a STA receiving this frame, are transmitting unsolicited
 *	Probe Response frames every 20 TUs
 * @oct_recommended: OCT is recommended to exchange MMPDUs with the reported AP
 * @same_ssid: the reported AP has the same SSID as the reporting AP
 * @multi_bss: the reported AP is part of a multiple BSSID set
 * @transmitted_bssid: the reported AP is the transmitting BSSID
 * @colocated_ess: all the APs that share the same ESS as the reported AP are
 *	colocated and can be discovered via legacy bands.
 * @short_ssid_valid: short_ssid is valid and can be used
 * @short_ssid: the short SSID for this SSID
 */
struct cfg80211_colocated_ap {
	struct list_head list;
	u8 bssid[ETH_ALEN];
	u8 ssid[IEEE80211_MAX_SSID_LEN];
	size_t ssid_len;
	u32 short_ssid;
	u32 center_freq;
	u8 unsolicited_probe:1,
	   oct_recommended:1,
	   same_ssid:1,
	   multi_bss:1,
	   transmitted_bssid:1,
	   colocated_ess:1,
	   short_ssid_valid:1;
};

static void bss_free(struct cfg80211_internal_bss *bss)
{
	struct cfg80211_bss_ies *ies;

	if (WARN_ON(atomic_read(&bss->hold)))
		return;

	ies = (void *)rcu_access_pointer(bss->pub.beacon_ies);
	if (ies && !bss->pub.hidden_beacon_bss)
		kfree_rcu(ies, rcu_head);
	ies = (void *)rcu_access_pointer(bss->pub.proberesp_ies);
	if (ies)
		kfree_rcu(ies, rcu_head);

	/*
	 * This happens when the module is removed, it doesn't
	 * really matter any more save for completeness
	 */
	if (!list_empty(&bss->hidden_list))
		list_del(&bss->hidden_list);

	kfree(bss);
}

static inline void bss_ref_get(struct cfg80211_registered_device *rdev,
			       struct cfg80211_internal_bss *bss)
{
	lockdep_assert_held(&rdev->bss_lock);

	bss->refcount++;
	if (bss->pub.hidden_beacon_bss) {
		bss = container_of(bss->pub.hidden_beacon_bss,
				   struct cfg80211_internal_bss,
				   pub);
		bss->refcount++;
	}
	if (bss->pub.transmitted_bss) {
		bss = container_of(bss->pub.transmitted_bss,
				   struct cfg80211_internal_bss,
				   pub);
		bss->refcount++;
	}
}

static inline void bss_ref_put(struct cfg80211_registered_device *rdev,
			       struct cfg80211_internal_bss *bss)
{
	lockdep_assert_held(&rdev->bss_lock);

	if (bss->pub.hidden_beacon_bss) {
		struct cfg80211_internal_bss *hbss;
		hbss = container_of(bss->pub.hidden_beacon_bss,
				    struct cfg80211_internal_bss,
				    pub);
		hbss->refcount--;
		if (hbss->refcount == 0)
			bss_free(hbss);
	}

	if (bss->pub.transmitted_bss) {
		struct cfg80211_internal_bss *tbss;

		tbss = container_of(bss->pub.transmitted_bss,
				    struct cfg80211_internal_bss,
				    pub);
		tbss->refcount--;
		if (tbss->refcount == 0)
			bss_free(tbss);
	}

	bss->refcount--;
	if (bss->refcount == 0)
		bss_free(bss);
}

static bool __cfg80211_unlink_bss(struct cfg80211_registered_device *rdev,
				  struct cfg80211_internal_bss *bss)
{
	lockdep_assert_held(&rdev->bss_lock);

	if (!list_empty(&bss->hidden_list)) {
		/*
		 * don't remove the beacon entry if it has
		 * probe responses associated with it
		 */
		if (!bss->pub.hidden_beacon_bss)
			return false;
		/*
		 * if it's a probe response entry break its
		 * link to the other entries in the group
		 */
		list_del_init(&bss->hidden_list);
	}

	list_del_init(&bss->list);
	list_del_init(&bss->pub.nontrans_list);
	rb_erase(&bss->rbn, &rdev->bss_tree);
	rdev->bss_entries--;
	WARN_ONCE((rdev->bss_entries == 0) ^ list_empty(&rdev->bss_list),
		  "rdev bss entries[%d]/list[empty:%d] corruption\n",
		  rdev->bss_entries, list_empty(&rdev->bss_list));
	bss_ref_put(rdev, bss);
	return true;
}

bool cfg80211_is_element_inherited(const struct element *elem,
				   const struct element *non_inherit_elem)
{
	u8 id_len, ext_id_len, i, loop_len, id;
	const u8 *list;

	if (elem->id == WLAN_EID_MULTIPLE_BSSID)
		return false;

	if (!non_inherit_elem || non_inherit_elem->datalen < 2)
		return true;

	/*
	 * non inheritance element format is:
	 * ext ID (56) | IDs list len | list | extension IDs list len | list
	 * Both lists are optional. Both lengths are mandatory.
	 * This means valid length is:
	 * elem_len = 1 (extension ID) + 2 (list len fields) + list lengths
	 */
	id_len = non_inherit_elem->data[1];
	if (non_inherit_elem->datalen < 3 + id_len)
		return true;

	ext_id_len = non_inherit_elem->data[2 + id_len];
	if (non_inherit_elem->datalen < 3 + id_len + ext_id_len)
		return true;

	if (elem->id == WLAN_EID_EXTENSION) {
		if (!ext_id_len)
			return true;
		loop_len = ext_id_len;
		list = &non_inherit_elem->data[3 + id_len];
		id = elem->data[0];
	} else {
		if (!id_len)
			return true;
		loop_len = id_len;
		list = &non_inherit_elem->data[2];
		id = elem->id;
	}

	for (i = 0; i < loop_len; i++) {
		if (list[i] == id)
			return false;
	}

	return true;
}
EXPORT_SYMBOL(cfg80211_is_element_inherited);

static size_t cfg80211_gen_new_ie(const u8 *ie, size_t ielen,
				  const u8 *subelement, size_t subie_len,
				  u8 *new_ie, gfp_t gfp)
{
	u8 *pos, *tmp;
	const u8 *tmp_old, *tmp_new;
	const struct element *non_inherit_elem;
	u8 *sub_copy;

	/* copy subelement as we need to change its content to
	 * mark an ie after it is processed.
	 */
	sub_copy = kmemdup(subelement, subie_len, gfp);
	if (!sub_copy)
		return 0;

	pos = &new_ie[0];

	/* set new ssid */
	tmp_new = cfg80211_find_ie(WLAN_EID_SSID, sub_copy, subie_len);
	if (tmp_new) {
		memcpy(pos, tmp_new, tmp_new[1] + 2);
		pos += (tmp_new[1] + 2);
	}

	/* get non inheritance list if exists */
	non_inherit_elem =
		cfg80211_find_ext_elem(WLAN_EID_EXT_NON_INHERITANCE,
				       sub_copy, subie_len);

	/* go through IEs in ie (skip SSID) and subelement,
	 * merge them into new_ie
	 */
	tmp_old = cfg80211_find_ie(WLAN_EID_SSID, ie, ielen);
	tmp_old = (tmp_old) ? tmp_old + tmp_old[1] + 2 : ie;

	while (tmp_old + tmp_old[1] + 2 - ie <= ielen) {
		if (tmp_old[0] == 0) {
			tmp_old++;
			continue;
		}

		if (tmp_old[0] == WLAN_EID_EXTENSION)
			tmp = (u8 *)cfg80211_find_ext_ie(tmp_old[2], sub_copy,
							 subie_len);
		else
			tmp = (u8 *)cfg80211_find_ie(tmp_old[0], sub_copy,
						     subie_len);

		if (!tmp) {
			const struct element *old_elem = (void *)tmp_old;

			/* ie in old ie but not in subelement */
			if (cfg80211_is_element_inherited(old_elem,
							  non_inherit_elem)) {
				memcpy(pos, tmp_old, tmp_old[1] + 2);
				pos += tmp_old[1] + 2;
			}
		} else {
			/* ie in transmitting ie also in subelement,
			 * copy from subelement and flag the ie in subelement
			 * as copied (by setting eid field to WLAN_EID_SSID,
			 * which is skipped anyway).
			 * For vendor ie, compare OUI + type + subType to
			 * determine if they are the same ie.
			 */
			if (tmp_old[0] == WLAN_EID_VENDOR_SPECIFIC) {
				if (!memcmp(tmp_old + 2, tmp + 2, 5)) {
					/* same vendor ie, copy from
					 * subelement
					 */
					memcpy(pos, tmp, tmp[1] + 2);
					pos += tmp[1] + 2;
					tmp[0] = WLAN_EID_SSID;
				} else {
					memcpy(pos, tmp_old, tmp_old[1] + 2);
					pos += tmp_old[1] + 2;
				}
			} else {
				/* copy ie from subelement into new ie */
				memcpy(pos, tmp, tmp[1] + 2);
				pos += tmp[1] + 2;
				tmp[0] = WLAN_EID_SSID;
			}
		}

		if (tmp_old + tmp_old[1] + 2 - ie == ielen)
			break;

		tmp_old += tmp_old[1] + 2;
	}

	/* go through subelement again to check if there is any ie not
	 * copied to new ie, skip ssid, capability, bssid-index ie
	 */
	tmp_new = sub_copy;
	while (tmp_new + tmp_new[1] + 2 - sub_copy <= subie_len) {
		if (!(tmp_new[0] == WLAN_EID_NON_TX_BSSID_CAP ||
		      tmp_new[0] == WLAN_EID_SSID)) {
			memcpy(pos, tmp_new, tmp_new[1] + 2);
			pos += tmp_new[1] + 2;
		}
		if (tmp_new + tmp_new[1] + 2 - sub_copy == subie_len)
			break;
		tmp_new += tmp_new[1] + 2;
	}

	kfree(sub_copy);
	return pos - new_ie;
}

static bool is_bss(struct cfg80211_bss *a, const u8 *bssid,
		   const u8 *ssid, size_t ssid_len)
{
	const struct cfg80211_bss_ies *ies;
	const u8 *ssidie;

	if (bssid && !ether_addr_equal(a->bssid, bssid))
		return false;

	if (!ssid)
		return true;

	ies = rcu_access_pointer(a->ies);
	if (!ies)
		return false;
	ssidie = cfg80211_find_ie(WLAN_EID_SSID, ies->data, ies->len);
	if (!ssidie)
		return false;
	if (ssidie[1] != ssid_len)
		return false;
	return memcmp(ssidie + 2, ssid, ssid_len) == 0;
}

static int
cfg80211_add_nontrans_list(struct cfg80211_bss *trans_bss,
			   struct cfg80211_bss *nontrans_bss)
{
	const u8 *ssid;
	size_t ssid_len;
	struct cfg80211_bss *bss = NULL;

	rcu_read_lock();
	ssid = ieee80211_bss_get_ie(nontrans_bss, WLAN_EID_SSID);
	if (!ssid) {
		rcu_read_unlock();
		return -EINVAL;
	}
	ssid_len = ssid[1];
	ssid = ssid + 2;

	/* check if nontrans_bss is in the list */
	list_for_each_entry(bss, &trans_bss->nontrans_list, nontrans_list) {
		if (is_bss(bss, nontrans_bss->bssid, ssid, ssid_len)) {
			rcu_read_unlock();
			return 0;
		}
	}

	rcu_read_unlock();

	/* add to the list */
	list_add_tail(&nontrans_bss->nontrans_list, &trans_bss->nontrans_list);
	return 0;
}

static void __cfg80211_bss_expire(struct cfg80211_registered_device *rdev,
				  unsigned long expire_time)
{
	struct cfg80211_internal_bss *bss, *tmp;
	bool expired = false;

	lockdep_assert_held(&rdev->bss_lock);

	list_for_each_entry_safe(bss, tmp, &rdev->bss_list, list) {
		if (atomic_read(&bss->hold))
			continue;
		if (!time_after(expire_time, bss->ts))
			continue;

		if (__cfg80211_unlink_bss(rdev, bss))
			expired = true;
	}

	if (expired)
		rdev->bss_generation++;
}

static bool cfg80211_bss_expire_oldest(struct cfg80211_registered_device *rdev)
{
	struct cfg80211_internal_bss *bss, *oldest = NULL;
	bool ret;

	lockdep_assert_held(&rdev->bss_lock);

	list_for_each_entry(bss, &rdev->bss_list, list) {
		if (atomic_read(&bss->hold))
			continue;

		if (!list_empty(&bss->hidden_list) &&
		    !bss->pub.hidden_beacon_bss)
			continue;

		if (oldest && time_before(oldest->ts, bss->ts))
			continue;
		oldest = bss;
	}

	if (WARN_ON(!oldest))
		return false;

	/*
	 * The callers make sure to increase rdev->bss_generation if anything
	 * gets removed (and a new entry added), so there's no need to also do
	 * it here.
	 */

	ret = __cfg80211_unlink_bss(rdev, oldest);
	WARN_ON(!ret);
	return ret;
}

static u8 cfg80211_parse_bss_param(u8 data,
				   struct cfg80211_colocated_ap *coloc_ap)
{
	coloc_ap->oct_recommended =
		u8_get_bits(data, IEEE80211_RNR_TBTT_PARAMS_OCT_RECOMMENDED);
	coloc_ap->same_ssid =
		u8_get_bits(data, IEEE80211_RNR_TBTT_PARAMS_SAME_SSID);
	coloc_ap->multi_bss =
		u8_get_bits(data, IEEE80211_RNR_TBTT_PARAMS_MULTI_BSSID);
	coloc_ap->transmitted_bssid =
		u8_get_bits(data, IEEE80211_RNR_TBTT_PARAMS_TRANSMITTED_BSSID);
	coloc_ap->unsolicited_probe =
		u8_get_bits(data, IEEE80211_RNR_TBTT_PARAMS_PROBE_ACTIVE);
	coloc_ap->colocated_ess =
		u8_get_bits(data, IEEE80211_RNR_TBTT_PARAMS_COLOC_ESS);

	return u8_get_bits(data, IEEE80211_RNR_TBTT_PARAMS_COLOC_AP);
}

static int cfg80211_calc_short_ssid(const struct cfg80211_bss_ies *ies,
				    const struct element **elem, u32 *s_ssid)
{

	*elem = cfg80211_find_elem(WLAN_EID_SSID, ies->data, ies->len);
	if (!*elem || (*elem)->datalen > IEEE80211_MAX_SSID_LEN)
		return -EINVAL;

	*s_ssid = ~crc32_le(~0, (*elem)->data, (*elem)->datalen);
	return 0;
}

static void cfg80211_free_coloc_ap_list(struct list_head *coloc_ap_list)
{
	struct cfg80211_colocated_ap *ap, *tmp_ap;

	list_for_each_entry_safe(ap, tmp_ap, coloc_ap_list, list) {
		list_del(&ap->list);
		kfree(ap);
	}
}

static int cfg80211_parse_ap_info(struct cfg80211_colocated_ap *entry,
				  const u8 *pos, u8 length,
				  const struct element *ssid_elem,
				  int s_ssid_tmp)
{
	/* skip the TBTT offset */
	pos++;

	memcpy(entry->bssid, pos, ETH_ALEN);
	pos += ETH_ALEN;

	if (length == IEEE80211_TBTT_INFO_OFFSET_BSSID_SSSID_BSS_PARAM) {
		memcpy(&entry->short_ssid, pos,
		       sizeof(entry->short_ssid));
		entry->short_ssid_valid = true;
		pos += 4;
	}

	/* skip non colocated APs */
	if (!cfg80211_parse_bss_param(*pos, entry))
		return -EINVAL;
	pos++;

	if (length == IEEE80211_TBTT_INFO_OFFSET_BSSID_BSS_PARAM) {
		/*
		 * no information about the short ssid. Consider the entry valid
		 * for now. It would later be dropped in case there are explicit
		 * SSIDs that need to be matched
		 */
		if (!entry->same_ssid)
			return 0;
	}

	if (entry->same_ssid) {
		entry->short_ssid = s_ssid_tmp;
		entry->short_ssid_valid = true;

		/*
		 * This is safe because we validate datalen in
		 * cfg80211_parse_colocated_ap(), before calling this
		 * function.
		 */
		memcpy(&entry->ssid, &ssid_elem->data,
		       ssid_elem->datalen);
		entry->ssid_len = ssid_elem->datalen;
	}
	return 0;
}

static int cfg80211_parse_colocated_ap(const struct cfg80211_bss_ies *ies,
				       struct list_head *list)
{
	struct ieee80211_neighbor_ap_info *ap_info;
	const struct element *elem, *ssid_elem;
	const u8 *pos, *end;
	u32 s_ssid_tmp;
	int n_coloc = 0, ret;
	LIST_HEAD(ap_list);

	elem = cfg80211_find_elem(WLAN_EID_REDUCED_NEIGHBOR_REPORT, ies->data,
				  ies->len);
	if (!elem)
		return 0;

	pos = elem->data;
	end = pos + elem->datalen;

	ret = cfg80211_calc_short_ssid(ies, &ssid_elem, &s_ssid_tmp);
	if (ret)
		return ret;

	/* RNR IE may contain more than one NEIGHBOR_AP_INFO */
	while (pos + sizeof(*ap_info) <= end) {
		enum nl80211_band band;
		int freq;
		u8 length, i, count;

		ap_info = (void *)pos;
		count = u8_get_bits(ap_info->tbtt_info_hdr,
				    IEEE80211_AP_INFO_TBTT_HDR_COUNT) + 1;
		length = ap_info->tbtt_info_len;

		pos += sizeof(*ap_info);

		if (!ieee80211_operating_class_to_band(ap_info->op_class,
						       &band))
			break;

		freq = ieee80211_channel_to_frequency(ap_info->channel, band);

		if (end - pos < count * length)
			break;

		/*
		 * TBTT info must include bss param + BSSID +
		 * (short SSID or same_ssid bit to be set).
		 * ignore other options, and move to the
		 * next AP info
		 */
		if (band != NL80211_BAND_6GHZ ||
		    (length != IEEE80211_TBTT_INFO_OFFSET_BSSID_BSS_PARAM &&
		     length < IEEE80211_TBTT_INFO_OFFSET_BSSID_SSSID_BSS_PARAM)) {
			pos += count * length;
			continue;
		}

		for (i = 0; i < count; i++) {
			struct cfg80211_colocated_ap *entry;

			entry = kzalloc(sizeof(*entry) + IEEE80211_MAX_SSID_LEN,
					GFP_ATOMIC);

			if (!entry)
				break;

			entry->center_freq = freq;

			if (!cfg80211_parse_ap_info(entry, pos, length,
						    ssid_elem, s_ssid_tmp)) {
				n_coloc++;
				list_add_tail(&entry->list, &ap_list);
			} else {
				kfree(entry);
			}

			pos += length;
		}
	}

	if (pos != end) {
		cfg80211_free_coloc_ap_list(&ap_list);
		return 0;
	}

	list_splice_tail(&ap_list, list);
	return n_coloc;
}

static  void cfg80211_scan_req_add_chan(struct cfg80211_scan_request *request,
					struct ieee80211_channel *chan,
					bool add_to_6ghz)
{
	int i;
	u32 n_channels = request->n_channels;
	struct cfg80211_scan_6ghz_params *params =
		&request->scan_6ghz_params[request->n_6ghz_params];

	for (i = 0; i < n_channels; i++) {
		if (request->channels[i] == chan) {
			if (add_to_6ghz)
				params->channel_idx = i;
			return;
		}
	}

	request->channels[n_channels] = chan;
	if (add_to_6ghz)
		request->scan_6ghz_params[request->n_6ghz_params].channel_idx =
			n_channels;

	request->n_channels++;
}

static bool cfg80211_find_ssid_match(struct cfg80211_colocated_ap *ap,
				     struct cfg80211_scan_request *request)
{
	int i;
	u32 s_ssid;

	for (i = 0; i < request->n_ssids; i++) {
		/* wildcard ssid in the scan request */
		if (!request->ssids[i].ssid_len) {
			if (ap->multi_bss && !ap->transmitted_bssid)
				continue;

			return true;
		}

		if (ap->ssid_len &&
		    ap->ssid_len == request->ssids[i].ssid_len) {
			if (!memcmp(request->ssids[i].ssid, ap->ssid,
				    ap->ssid_len))
				return true;
		} else if (ap->short_ssid_valid) {
			s_ssid = ~crc32_le(~0, request->ssids[i].ssid,
					   request->ssids[i].ssid_len);

			if (ap->short_ssid == s_ssid)
				return true;
		}
	}

	return false;
}

static int cfg80211_scan_6ghz(struct cfg80211_registered_device *rdev)
{
	u8 i;
	struct cfg80211_colocated_ap *ap;
	int n_channels, count = 0, err;
	struct cfg80211_scan_request *request, *rdev_req = rdev->scan_req;
	LIST_HEAD(coloc_ap_list);
	bool need_scan_psc = true;
	const struct ieee80211_sband_iftype_data *iftd;

	rdev_req->scan_6ghz = true;

	if (!rdev->wiphy.bands[NL80211_BAND_6GHZ])
		return -EOPNOTSUPP;

	iftd = ieee80211_get_sband_iftype_data(rdev->wiphy.bands[NL80211_BAND_6GHZ],
					       rdev_req->wdev->iftype);
	if (!iftd || !iftd->he_cap.has_he)
		return -EOPNOTSUPP;

	n_channels = rdev->wiphy.bands[NL80211_BAND_6GHZ]->n_channels;

	if (rdev_req->flags & NL80211_SCAN_FLAG_COLOCATED_6GHZ) {
		struct cfg80211_internal_bss *intbss;

		spin_lock_bh(&rdev->bss_lock);
		list_for_each_entry(intbss, &rdev->bss_list, list) {
			struct cfg80211_bss *res = &intbss->pub;
			const struct cfg80211_bss_ies *ies;

			ies = rcu_access_pointer(res->ies);
			count += cfg80211_parse_colocated_ap(ies,
							     &coloc_ap_list);
		}
		spin_unlock_bh(&rdev->bss_lock);
	}

	request = kzalloc(struct_size(request, channels, n_channels) +
			  sizeof(*request->scan_6ghz_params) * count +
			  sizeof(*request->ssids) * rdev_req->n_ssids,
			  GFP_KERNEL);
	if (!request) {
		cfg80211_free_coloc_ap_list(&coloc_ap_list);
		return -ENOMEM;
	}

	*request = *rdev_req;
	request->n_channels = 0;
	request->scan_6ghz_params =
		(void *)&request->channels[n_channels];

	/*
	 * PSC channels should not be scanned in case of direct scan with 1 SSID
	 * and at least one of the reported co-located APs with same SSID
	 * indicating that all APs in the same ESS are co-located
	 */
	if (count && request->n_ssids == 1 && request->ssids[0].ssid_len) {
		list_for_each_entry(ap, &coloc_ap_list, list) {
			if (ap->colocated_ess &&
			    cfg80211_find_ssid_match(ap, request)) {
				need_scan_psc = false;
				break;
			}
		}
	}

	/*
	 * add to the scan request the channels that need to be scanned
	 * regardless of the collocated APs (PSC channels or all channels
	 * in case that NL80211_SCAN_FLAG_COLOCATED_6GHZ is not set)
	 */
	for (i = 0; i < rdev_req->n_channels; i++) {
		if (rdev_req->channels[i]->band == NL80211_BAND_6GHZ &&
		    ((need_scan_psc &&
		      cfg80211_channel_is_psc(rdev_req->channels[i])) ||
		     !(rdev_req->flags & NL80211_SCAN_FLAG_COLOCATED_6GHZ))) {
			cfg80211_scan_req_add_chan(request,
						   rdev_req->channels[i],
						   false);
		}
	}

	if (!(rdev_req->flags & NL80211_SCAN_FLAG_COLOCATED_6GHZ))
		goto skip;

	list_for_each_entry(ap, &coloc_ap_list, list) {
		bool found = false;
		struct cfg80211_scan_6ghz_params *scan_6ghz_params =
			&request->scan_6ghz_params[request->n_6ghz_params];
		struct ieee80211_channel *chan =
			ieee80211_get_channel(&rdev->wiphy, ap->center_freq);

		if (!chan || chan->flags & IEEE80211_CHAN_DISABLED)
			continue;

		for (i = 0; i < rdev_req->n_channels; i++) {
			if (rdev_req->channels[i] == chan)
				found = true;
		}

		if (!found)
			continue;

		if (request->n_ssids > 0 &&
		    !cfg80211_find_ssid_match(ap, request))
			continue;

		if (!request->n_ssids && ap->multi_bss && !ap->transmitted_bssid)
			continue;

		cfg80211_scan_req_add_chan(request, chan, true);
		memcpy(scan_6ghz_params->bssid, ap->bssid, ETH_ALEN);
		scan_6ghz_params->short_ssid = ap->short_ssid;
		scan_6ghz_params->short_ssid_valid = ap->short_ssid_valid;
		scan_6ghz_params->unsolicited_probe = ap->unsolicited_probe;

		/*
		 * If a PSC channel is added to the scan and 'need_scan_psc' is
		 * set to false, then all the APs that the scan logic is
		 * interested with on the channel are collocated and thus there
		 * is no need to perform the initial PSC channel listen.
		 */
		if (cfg80211_channel_is_psc(chan) && !need_scan_psc)
			scan_6ghz_params->psc_no_listen = true;

		request->n_6ghz_params++;
	}

skip:
	cfg80211_free_coloc_ap_list(&coloc_ap_list);

	if (request->n_channels) {
		struct cfg80211_scan_request *old = rdev->int_scan_req;
		rdev->int_scan_req = request;

		/*
		 * Add the ssids from the parent scan request to the new scan
		 * request, so the driver would be able to use them in its
		 * probe requests to discover hidden APs on PSC channels.
		 */
		request->ssids = (void *)&request->channels[request->n_channels];
		request->n_ssids = rdev_req->n_ssids;
		memcpy(request->ssids, rdev_req->ssids, sizeof(*request->ssids) *
		       request->n_ssids);

		/*
		 * If this scan follows a previous scan, save the scan start
		 * info from the first part of the scan
		 */
		if (old)
			rdev->int_scan_req->info = old->info;

		err = rdev_scan(rdev, request);
		if (err) {
			rdev->int_scan_req = old;
			kfree(request);
		} else {
			kfree(old);
		}

		return err;
	}

	kfree(request);
	return -EINVAL;
}

int cfg80211_scan(struct cfg80211_registered_device *rdev)
{
	struct cfg80211_scan_request *request;
	struct cfg80211_scan_request *rdev_req = rdev->scan_req;
	u32 n_channels = 0, idx, i;

	if (!(rdev->wiphy.flags & WIPHY_FLAG_SPLIT_SCAN_6GHZ))
		return rdev_scan(rdev, rdev_req);

	for (i = 0; i < rdev_req->n_channels; i++) {
		if (rdev_req->channels[i]->band != NL80211_BAND_6GHZ)
			n_channels++;
	}

	if (!n_channels)
		return cfg80211_scan_6ghz(rdev);

	request = kzalloc(struct_size(request, channels, n_channels),
			  GFP_KERNEL);
	if (!request)
		return -ENOMEM;

	*request = *rdev_req;
	request->n_channels = n_channels;

	for (i = idx = 0; i < rdev_req->n_channels; i++) {
		if (rdev_req->channels[i]->band != NL80211_BAND_6GHZ)
			request->channels[idx++] = rdev_req->channels[i];
	}

	rdev_req->scan_6ghz = false;
	rdev->int_scan_req = request;
	return rdev_scan(rdev, request);
}

void ___cfg80211_scan_done(struct cfg80211_registered_device *rdev,
			   bool send_message)
{
	struct cfg80211_scan_request *request, *rdev_req;
	struct wireless_dev *wdev;
	struct sk_buff *msg;
#ifdef CONFIG_CFG80211_WEXT
	union iwreq_data wrqu;
#endif

	lockdep_assert_held(&rdev->wiphy.mtx);

	if (rdev->scan_msg) {
		nl80211_send_scan_msg(rdev, rdev->scan_msg);
		rdev->scan_msg = NULL;
		return;
	}

	rdev_req = rdev->scan_req;
	if (!rdev_req)
		return;

	wdev = rdev_req->wdev;
	request = rdev->int_scan_req ? rdev->int_scan_req : rdev_req;

	if (wdev_running(wdev) &&
	    (rdev->wiphy.flags & WIPHY_FLAG_SPLIT_SCAN_6GHZ) &&
	    !rdev_req->scan_6ghz && !request->info.aborted &&
	    !cfg80211_scan_6ghz(rdev))
		return;

	/*
	 * This must be before sending the other events!
	 * Otherwise, wpa_supplicant gets completely confused with
	 * wext events.
	 */
	if (wdev->netdev)
		cfg80211_sme_scan_done(wdev->netdev);

	if (!request->info.aborted &&
	    request->flags & NL80211_SCAN_FLAG_FLUSH) {
		/* flush entries from previous scans */
		spin_lock_bh(&rdev->bss_lock);
		__cfg80211_bss_expire(rdev, request->scan_start);
		spin_unlock_bh(&rdev->bss_lock);
	}

	msg = nl80211_build_scan_msg(rdev, wdev, request->info.aborted);

#ifdef CONFIG_CFG80211_WEXT
	if (wdev->netdev && !request->info.aborted) {
		memset(&wrqu, 0, sizeof(wrqu));

		wireless_send_event(wdev->netdev, SIOCGIWSCAN, &wrqu, NULL);
	}
#endif

	dev_put(wdev->netdev);

	kfree(rdev->int_scan_req);
	rdev->int_scan_req = NULL;

	kfree(rdev->scan_req);
	rdev->scan_req = NULL;

	if (!send_message)
		rdev->scan_msg = msg;
	else
		nl80211_send_scan_msg(rdev, msg);
}

void __cfg80211_scan_done(struct work_struct *wk)
{
	struct cfg80211_registered_device *rdev;

	rdev = container_of(wk, struct cfg80211_registered_device,
			    scan_done_wk);

	wiphy_lock(&rdev->wiphy);
	___cfg80211_scan_done(rdev, true);
	wiphy_unlock(&rdev->wiphy);
}

void cfg80211_scan_done(struct cfg80211_scan_request *request,
			struct cfg80211_scan_info *info)
{
	struct cfg80211_scan_info old_info = request->info;

	trace_cfg80211_scan_done(request, info);
	WARN_ON(request != wiphy_to_rdev(request->wiphy)->scan_req &&
		request != wiphy_to_rdev(request->wiphy)->int_scan_req);

	request->info = *info;

	/*
	 * In case the scan is split, the scan_start_tsf and tsf_bssid should
	 * be of the first part. In such a case old_info.scan_start_tsf should
	 * be non zero.
	 */
	if (request->scan_6ghz && old_info.scan_start_tsf) {
		request->info.scan_start_tsf = old_info.scan_start_tsf;
		memcpy(request->info.tsf_bssid, old_info.tsf_bssid,
		       sizeof(request->info.tsf_bssid));
	}

	request->notified = true;
	queue_work(cfg80211_wq, &wiphy_to_rdev(request->wiphy)->scan_done_wk);
}
EXPORT_SYMBOL(cfg80211_scan_done);

void cfg80211_add_sched_scan_req(struct cfg80211_registered_device *rdev,
				 struct cfg80211_sched_scan_request *req)
{
	lockdep_assert_held(&rdev->wiphy.mtx);

	list_add_rcu(&req->list, &rdev->sched_scan_req_list);
}

static void cfg80211_del_sched_scan_req(struct cfg80211_registered_device *rdev,
					struct cfg80211_sched_scan_request *req)
{
	lockdep_assert_held(&rdev->wiphy.mtx);

	list_del_rcu(&req->list);
	kfree_rcu(req, rcu_head);
}

static struct cfg80211_sched_scan_request *
cfg80211_find_sched_scan_req(struct cfg80211_registered_device *rdev, u64 reqid)
{
	struct cfg80211_sched_scan_request *pos;

	list_for_each_entry_rcu(pos, &rdev->sched_scan_req_list, list,
				lockdep_is_held(&rdev->wiphy.mtx)) {
		if (pos->reqid == reqid)
			return pos;
	}
	return NULL;
}

/*
 * Determines if a scheduled scan request can be handled. When a legacy
 * scheduled scan is running no other scheduled scan is allowed regardless
 * whether the request is for legacy or multi-support scan. When a multi-support
 * scheduled scan is running a request for legacy scan is not allowed. In this
 * case a request for multi-support scan can be handled if resources are
 * available, ie. struct wiphy::max_sched_scan_reqs limit is not yet reached.
 */
int cfg80211_sched_scan_req_possible(struct cfg80211_registered_device *rdev,
				     bool want_multi)
{
	struct cfg80211_sched_scan_request *pos;
	int i = 0;

	list_for_each_entry(pos, &rdev->sched_scan_req_list, list) {
		/* request id zero means legacy in progress */
		if (!i && !pos->reqid)
			return -EINPROGRESS;
		i++;
	}

	if (i) {
		/* no legacy allowed when multi request(s) are active */
		if (!want_multi)
			return -EINPROGRESS;

		/* resource limit reached */
		if (i == rdev->wiphy.max_sched_scan_reqs)
			return -ENOSPC;
	}
	return 0;
}

void cfg80211_sched_scan_results_wk(struct work_struct *work)
{
	struct cfg80211_registered_device *rdev;
	struct cfg80211_sched_scan_request *req, *tmp;

	rdev = container_of(work, struct cfg80211_registered_device,
			   sched_scan_res_wk);

	wiphy_lock(&rdev->wiphy);
	list_for_each_entry_safe(req, tmp, &rdev->sched_scan_req_list, list) {
		if (req->report_results) {
			req->report_results = false;
			if (req->flags & NL80211_SCAN_FLAG_FLUSH) {
				/* flush entries from previous scans */
				spin_lock_bh(&rdev->bss_lock);
				__cfg80211_bss_expire(rdev, req->scan_start);
				spin_unlock_bh(&rdev->bss_lock);
				req->scan_start = jiffies;
			}
			nl80211_send_sched_scan(req,
						NL80211_CMD_SCHED_SCAN_RESULTS);
		}
	}
	wiphy_unlock(&rdev->wiphy);
}

void cfg80211_sched_scan_results(struct wiphy *wiphy, u64 reqid)
{
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wiphy);
	struct cfg80211_sched_scan_request *request;

	trace_cfg80211_sched_scan_results(wiphy, reqid);
	/* ignore if we're not scanning */

	rcu_read_lock();
	request = cfg80211_find_sched_scan_req(rdev, reqid);
	if (request) {
		request->report_results = true;
		queue_work(cfg80211_wq, &rdev->sched_scan_res_wk);
	}
	rcu_read_unlock();
}
EXPORT_SYMBOL(cfg80211_sched_scan_results);

void cfg80211_sched_scan_stopped_locked(struct wiphy *wiphy, u64 reqid)
{
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wiphy);

	lockdep_assert_held(&wiphy->mtx);

	trace_cfg80211_sched_scan_stopped(wiphy, reqid);

	__cfg80211_stop_sched_scan(rdev, reqid, true);
}
EXPORT_SYMBOL(cfg80211_sched_scan_stopped_locked);

void cfg80211_sched_scan_stopped(struct wiphy *wiphy, u64 reqid)
{
	wiphy_lock(wiphy);
	cfg80211_sched_scan_stopped_locked(wiphy, reqid);
	wiphy_unlock(wiphy);
}
EXPORT_SYMBOL(cfg80211_sched_scan_stopped);

int cfg80211_stop_sched_scan_req(struct cfg80211_registered_device *rdev,
				 struct cfg80211_sched_scan_request *req,
				 bool driver_initiated)
{
	lockdep_assert_held(&rdev->wiphy.mtx);

	if (!driver_initiated) {
		int err = rdev_sched_scan_stop(rdev, req->dev, req->reqid);
		if (err)
			return err;
	}

	nl80211_send_sched_scan(req, NL80211_CMD_SCHED_SCAN_STOPPED);

	cfg80211_del_sched_scan_req(rdev, req);

	return 0;
}

int __cfg80211_stop_sched_scan(struct cfg80211_registered_device *rdev,
			       u64 reqid, bool driver_initiated)
{
	struct cfg80211_sched_scan_request *sched_scan_req;

	lockdep_assert_held(&rdev->wiphy.mtx);

	sched_scan_req = cfg80211_find_sched_scan_req(rdev, reqid);
	if (!sched_scan_req)
		return -ENOENT;

	return cfg80211_stop_sched_scan_req(rdev, sched_scan_req,
					    driver_initiated);
}

void cfg80211_bss_age(struct cfg80211_registered_device *rdev,
                      unsigned long age_secs)
{
	struct cfg80211_internal_bss *bss;
	unsigned long age_jiffies = msecs_to_jiffies(age_secs * MSEC_PER_SEC);

	spin_lock_bh(&rdev->bss_lock);
	list_for_each_entry(bss, &rdev->bss_list, list)
		bss->ts -= age_jiffies;
	spin_unlock_bh(&rdev->bss_lock);
}

void cfg80211_bss_expire(struct cfg80211_registered_device *rdev)
{
	__cfg80211_bss_expire(rdev, jiffies - IEEE80211_SCAN_RESULT_EXPIRE);
}

void cfg80211_bss_flush(struct wiphy *wiphy)
{
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wiphy);

	spin_lock_bh(&rdev->bss_lock);
	__cfg80211_bss_expire(rdev, jiffies);
	spin_unlock_bh(&rdev->bss_lock);
}
EXPORT_SYMBOL(cfg80211_bss_flush);

const struct element *
cfg80211_find_elem_match(u8 eid, const u8 *ies, unsigned int len,
			 const u8 *match, unsigned int match_len,
			 unsigned int match_offset)
{
	const struct element *elem;

	for_each_element_id(elem, eid, ies, len) {
		if (elem->datalen >= match_offset + match_len &&
		    !memcmp(elem->data + match_offset, match, match_len))
			return elem;
	}

	return NULL;
}
EXPORT_SYMBOL(cfg80211_find_elem_match);

const struct element *cfg80211_find_vendor_elem(unsigned int oui, int oui_type,
						const u8 *ies,
						unsigned int len)
{
	const struct element *elem;
	u8 match[] = { oui >> 16, oui >> 8, oui, oui_type };
	int match_len = (oui_type < 0) ? 3 : sizeof(match);

	if (WARN_ON(oui_type > 0xff))
		return NULL;

	elem = cfg80211_find_elem_match(WLAN_EID_VENDOR_SPECIFIC, ies, len,
					match, match_len, 0);

	if (!elem || elem->datalen < 4)
		return NULL;

	return elem;
}
EXPORT_SYMBOL(cfg80211_find_vendor_elem);

/**
 * enum bss_compare_mode - BSS compare mode
 * @BSS_CMP_REGULAR: regular compare mode (for insertion and normal find)
 * @BSS_CMP_HIDE_ZLEN: find hidden SSID with zero-length mode
 * @BSS_CMP_HIDE_NUL: find hidden SSID with NUL-ed out mode
 */
enum bss_compare_mode {
	BSS_CMP_REGULAR,
	BSS_CMP_HIDE_ZLEN,
	BSS_CMP_HIDE_NUL,
};

static int cmp_bss(struct cfg80211_bss *a,
		   struct cfg80211_bss *b,
		   enum bss_compare_mode mode)
{
	const struct cfg80211_bss_ies *a_ies, *b_ies;
	const u8 *ie1 = NULL;
	const u8 *ie2 = NULL;
	int i, r;

	if (a->channel != b->channel)
		return b->channel->center_freq - a->channel->center_freq;

	a_ies = rcu_access_pointer(a->ies);
	if (!a_ies)
		return -1;
	b_ies = rcu_access_pointer(b->ies);
	if (!b_ies)
		return 1;

	if (WLAN_CAPABILITY_IS_STA_BSS(a->capability))
		ie1 = cfg80211_find_ie(WLAN_EID_MESH_ID,
				       a_ies->data, a_ies->len);
	if (WLAN_CAPABILITY_IS_STA_BSS(b->capability))
		ie2 = cfg80211_find_ie(WLAN_EID_MESH_ID,
				       b_ies->data, b_ies->len);
	if (ie1 && ie2) {
		int mesh_id_cmp;

		if (ie1[1] == ie2[1])
			mesh_id_cmp = memcmp(ie1 + 2, ie2 + 2, ie1[1]);
		else
			mesh_id_cmp = ie2[1] - ie1[1];

		ie1 = cfg80211_find_ie(WLAN_EID_MESH_CONFIG,
				       a_ies->data, a_ies->len);
		ie2 = cfg80211_find_ie(WLAN_EID_MESH_CONFIG,
				       b_ies->data, b_ies->len);
		if (ie1 && ie2) {
			if (mesh_id_cmp)
				return mesh_id_cmp;
			if (ie1[1] != ie2[1])
				return ie2[1] - ie1[1];
			return memcmp(ie1 + 2, ie2 + 2, ie1[1]);
		}
	}

	r = memcmp(a->bssid, b->bssid, sizeof(a->bssid));
	if (r)
		return r;

	ie1 = cfg80211_find_ie(WLAN_EID_SSID, a_ies->data, a_ies->len);
	ie2 = cfg80211_find_ie(WLAN_EID_SSID, b_ies->data, b_ies->len);

	if (!ie1 && !ie2)
		return 0;

	/*
	 * Note that with "hide_ssid", the function returns a match if
	 * the already-present BSS ("b") is a hidden SSID beacon for
	 * the new BSS ("a").
	 */

	/* sort missing IE before (left of) present IE */
	if (!ie1)
		return -1;
	if (!ie2)
		return 1;

	switch (mode) {
	case BSS_CMP_HIDE_ZLEN:
		/*
		 * In ZLEN mode we assume the BSS entry we're
		 * looking for has a zero-length SSID. So if
		 * the one we're looking at right now has that,
		 * return 0. Otherwise, return the difference
		 * in length, but since we're looking for the
		 * 0-length it's really equivalent to returning
		 * the length of the one we're looking at.
		 *
		 * No content comparison is needed as we assume
		 * the content length is zero.
		 */
		return ie2[1];
	case BSS_CMP_REGULAR:
	default:
		/* sort by length first, then by contents */
		if (ie1[1] != ie2[1])
			return ie2[1] - ie1[1];
		return memcmp(ie1 + 2, ie2 + 2, ie1[1]);
	case BSS_CMP_HIDE_NUL:
		if (ie1[1] != ie2[1])
			return ie2[1] - ie1[1];
		/* this is equivalent to memcmp(zeroes, ie2 + 2, len) */
		for (i = 0; i < ie2[1]; i++)
			if (ie2[i + 2])
				return -1;
		return 0;
	}
}

static bool cfg80211_bss_type_match(u16 capability,
				    enum nl80211_band band,
				    enum ieee80211_bss_type bss_type)
{
	bool ret = true;
	u16 mask, val;

	if (bss_type == IEEE80211_BSS_TYPE_ANY)
		return ret;

	if (band == NL80211_BAND_60GHZ) {
		mask = WLAN_CAPABILITY_DMG_TYPE_MASK;
		switch (bss_type) {
		case IEEE80211_BSS_TYPE_ESS:
			val = WLAN_CAPABILITY_DMG_TYPE_AP;
			break;
		case IEEE80211_BSS_TYPE_PBSS:
			val = WLAN_CAPABILITY_DMG_TYPE_PBSS;
			break;
		case IEEE80211_BSS_TYPE_IBSS:
			val = WLAN_CAPABILITY_DMG_TYPE_IBSS;
			break;
		default:
			return false;
		}
	} else {
		mask = WLAN_CAPABILITY_ESS | WLAN_CAPABILITY_IBSS;
		switch (bss_type) {
		case IEEE80211_BSS_TYPE_ESS:
			val = WLAN_CAPABILITY_ESS;
			break;
		case IEEE80211_BSS_TYPE_IBSS:
			val = WLAN_CAPABILITY_IBSS;
			break;
		case IEEE80211_BSS_TYPE_MBSS:
			val = 0;
			break;
		default:
			return false;
		}
	}

	ret = ((capability & mask) == val);
	return ret;
}

/* Returned bss is reference counted and must be cleaned up appropriately. */
struct cfg80211_bss *cfg80211_get_bss(struct wiphy *wiphy,
				      struct ieee80211_channel *channel,
				      const u8 *bssid,
				      const u8 *ssid, size_t ssid_len,
				      enum ieee80211_bss_type bss_type,
				      enum ieee80211_privacy privacy)
{
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wiphy);
	struct cfg80211_internal_bss *bss, *res = NULL;
	unsigned long now = jiffies;
	int bss_privacy;

	trace_cfg80211_get_bss(wiphy, channel, bssid, ssid, ssid_len, bss_type,
			       privacy);

	spin_lock_bh(&rdev->bss_lock);

	list_for_each_entry(bss, &rdev->bss_list, list) {
		if (!cfg80211_bss_type_match(bss->pub.capability,
					     bss->pub.channel->band, bss_type))
			continue;

		bss_privacy = (bss->pub.capability & WLAN_CAPABILITY_PRIVACY);
		if ((privacy == IEEE80211_PRIVACY_ON && !bss_privacy) ||
		    (privacy == IEEE80211_PRIVACY_OFF && bss_privacy))
			continue;
		if (channel && bss->pub.channel != channel)
			continue;
		if (!is_valid_ether_addr(bss->pub.bssid))
			continue;
		/* Don't get expired BSS structs */
		if (time_after(now, bss->ts + IEEE80211_SCAN_RESULT_EXPIRE) &&
		    !atomic_read(&bss->hold))
			continue;
		if (is_bss(&bss->pub, bssid, ssid, ssid_len)) {
			res = bss;
			bss_ref_get(rdev, res);
			break;
		}
	}

	spin_unlock_bh(&rdev->bss_lock);
	if (!res)
		return NULL;
	trace_cfg80211_return_bss(&res->pub);
	return &res->pub;
}
EXPORT_SYMBOL(cfg80211_get_bss);

static void rb_insert_bss(struct cfg80211_registered_device *rdev,
			  struct cfg80211_internal_bss *bss)
{
	struct rb_node **p = &rdev->bss_tree.rb_node;
	struct rb_node *parent = NULL;
	struct cfg80211_internal_bss *tbss;
	int cmp;

	while (*p) {
		parent = *p;
		tbss = rb_entry(parent, struct cfg80211_internal_bss, rbn);

		cmp = cmp_bss(&bss->pub, &tbss->pub, BSS_CMP_REGULAR);

		if (WARN_ON(!cmp)) {
			/* will sort of leak this BSS */
			return;
		}

		if (cmp < 0)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}

	rb_link_node(&bss->rbn, parent, p);
	rb_insert_color(&bss->rbn, &rdev->bss_tree);
}

static struct cfg80211_internal_bss *
rb_find_bss(struct cfg80211_registered_device *rdev,
	    struct cfg80211_internal_bss *res,
	    enum bss_compare_mode mode)
{
	struct rb_node *n = rdev->bss_tree.rb_node;
	struct cfg80211_internal_bss *bss;
	int r;

	while (n) {
		bss = rb_entry(n, struct cfg80211_internal_bss, rbn);
		r = cmp_bss(&res->pub, &bss->pub, mode);

		if (r == 0)
			return bss;
		else if (r < 0)
			n = n->rb_left;
		else
			n = n->rb_right;
	}

	return NULL;
}

static bool cfg80211_combine_bsses(struct cfg80211_registered_device *rdev,
				   struct cfg80211_internal_bss *new)
{
	const struct cfg80211_bss_ies *ies;
	struct cfg80211_internal_bss *bss;
	const u8 *ie;
	int i, ssidlen;
	u8 fold = 0;
	u32 n_entries = 0;

	ies = rcu_access_pointer(new->pub.beacon_ies);
	if (WARN_ON(!ies))
		return false;

	ie = cfg80211_find_ie(WLAN_EID_SSID, ies->data, ies->len);
	if (!ie) {
		/* nothing to do */
		return true;
	}

	ssidlen = ie[1];
	for (i = 0; i < ssidlen; i++)
		fold |= ie[2 + i];

	if (fold) {
		/* not a hidden SSID */
		return true;
	}

	/* This is the bad part ... */

	list_for_each_entry(bss, &rdev->bss_list, list) {
		/*
		 * we're iterating all the entries anyway, so take the
		 * opportunity to validate the list length accounting
		 */
		n_entries++;

		if (!ether_addr_equal(bss->pub.bssid, new->pub.bssid))
			continue;
		if (bss->pub.channel != new->pub.channel)
			continue;
		if (bss->pub.scan_width != new->pub.scan_width)
			continue;
		if (rcu_access_pointer(bss->pub.beacon_ies))
			continue;
		ies = rcu_access_pointer(bss->pub.ies);
		if (!ies)
			continue;
		ie = cfg80211_find_ie(WLAN_EID_SSID, ies->data, ies->len);
		if (!ie)
			continue;
		if (ssidlen && ie[1] != ssidlen)
			continue;
		if (WARN_ON_ONCE(bss->pub.hidden_beacon_bss))
			continue;
		if (WARN_ON_ONCE(!list_empty(&bss->hidden_list)))
			list_del(&bss->hidden_list);
		/* combine them */
		list_add(&bss->hidden_list, &new->hidden_list);
		bss->pub.hidden_beacon_bss = &new->pub;
		new->refcount += bss->refcount;
		rcu_assign_pointer(bss->pub.beacon_ies,
				   new->pub.beacon_ies);
	}

	WARN_ONCE(n_entries != rdev->bss_entries,
		  "rdev bss entries[%d]/list[len:%d] corruption\n",
		  rdev->bss_entries, n_entries);

	return true;
}

struct cfg80211_non_tx_bss {
	struct cfg80211_bss *tx_bss;
	u8 max_bssid_indicator;
	u8 bssid_index;
};

static bool
cfg80211_update_known_bss(struct cfg80211_registered_device *rdev,
			  struct cfg80211_internal_bss *known,
			  struct cfg80211_internal_bss *new,
			  bool signal_valid)
{
	lockdep_assert_held(&rdev->bss_lock);

	/* Update IEs */
	if (rcu_access_pointer(new->pub.proberesp_ies)) {
		const struct cfg80211_bss_ies *old;

		old = rcu_access_pointer(known->pub.proberesp_ies);

		rcu_assign_pointer(known->pub.proberesp_ies,
				   new->pub.proberesp_ies);
		/* Override possible earlier Beacon frame IEs */
		rcu_assign_pointer(known->pub.ies,
				   new->pub.proberesp_ies);
		if (old)
			kfree_rcu((struct cfg80211_bss_ies *)old, rcu_head);
	} else if (rcu_access_pointer(new->pub.beacon_ies)) {
		const struct cfg80211_bss_ies *old;
		struct cfg80211_internal_bss *bss;

		if (known->pub.hidden_beacon_bss &&
		    !list_empty(&known->hidden_list)) {
			const struct cfg80211_bss_ies *f;

			/* The known BSS struct is one of the probe
			 * response members of a group, but we're
			 * receiving a beacon (beacon_ies in the new
			 * bss is used). This can only mean that the
			 * AP changed its beacon from not having an
			 * SSID to showing it, which is confusing so
			 * drop this information.
			 */

			f = rcu_access_pointer(new->pub.beacon_ies);
			kfree_rcu((struct cfg80211_bss_ies *)f, rcu_head);
			return false;
		}

		old = rcu_access_pointer(known->pub.beacon_ies);

		rcu_assign_pointer(known->pub.beacon_ies, new->pub.beacon_ies);

		/* Override IEs if they were from a beacon before */
		if (old == rcu_access_pointer(known->pub.ies))
			rcu_assign_pointer(known->pub.ies, new->pub.beacon_ies);

		/* Assign beacon IEs to all sub entries */
		list_for_each_entry(bss, &known->hidden_list, hidden_list) {
			const struct cfg80211_bss_ies *ies;

			ies = rcu_access_pointer(bss->pub.beacon_ies);
			WARN_ON(ies != old);

			rcu_assign_pointer(bss->pub.beacon_ies,
					   new->pub.beacon_ies);
		}

		if (old)
			kfree_rcu((struct cfg80211_bss_ies *)old, rcu_head);
	}

	known->pub.beacon_interval = new->pub.beacon_interval;

	/* don't update the signal if beacon was heard on
	 * adjacent channel.
	 */
	if (signal_valid)
		known->pub.signal = new->pub.signal;
	known->pub.capability = new->pub.capability;
	known->ts = new->ts;
	known->ts_boottime = new->ts_boottime;
	known->parent_tsf = new->parent_tsf;
	known->pub.chains = new->pub.chains;
	memcpy(known->pub.chain_signal, new->pub.chain_signal,
	       IEEE80211_MAX_CHAINS);
	ether_addr_copy(known->parent_bssid, new->parent_bssid);
	known->pub.max_bssid_indicator = new->pub.max_bssid_indicator;
	known->pub.bssid_index = new->pub.bssid_index;

	return true;
}

/* Returned bss is reference counted and must be cleaned up appropriately. */
struct cfg80211_internal_bss *
cfg80211_bss_update(struct cfg80211_registered_device *rdev,
		    struct cfg80211_internal_bss *tmp,
		    bool signal_valid, unsigned long ts)
{
	struct cfg80211_internal_bss *found = NULL;

	if (WARN_ON(!tmp->pub.channel))
		return NULL;

	tmp->ts = ts;

	spin_lock_bh(&rdev->bss_lock);

	if (WARN_ON(!rcu_access_pointer(tmp->pub.ies))) {
		spin_unlock_bh(&rdev->bss_lock);
		return NULL;
	}

	found = rb_find_bss(rdev, tmp, BSS_CMP_REGULAR);

	if (found) {
		if (!cfg80211_update_known_bss(rdev, found, tmp, signal_valid))
			goto drop;
	} else {
		struct cfg80211_internal_bss *new;
		struct cfg80211_internal_bss *hidden;
		struct cfg80211_bss_ies *ies;

		/*
		 * create a copy -- the "res" variable that is passed in
		 * is allocated on the stack since it's not needed in the
		 * more common case of an update
		 */
		new = kzalloc(sizeof(*new) + rdev->wiphy.bss_priv_size,
			      GFP_ATOMIC);
		if (!new) {
			ies = (void *)rcu_dereference(tmp->pub.beacon_ies);
			if (ies)
				kfree_rcu(ies, rcu_head);
			ies = (void *)rcu_dereference(tmp->pub.proberesp_ies);
			if (ies)
				kfree_rcu(ies, rcu_head);
			goto drop;
		}
		memcpy(new, tmp, sizeof(*new));
		new->refcount = 1;
		INIT_LIST_HEAD(&new->hidden_list);
		INIT_LIST_HEAD(&new->pub.nontrans_list);

		if (rcu_access_pointer(tmp->pub.proberesp_ies)) {
			hidden = rb_find_bss(rdev, tmp, BSS_CMP_HIDE_ZLEN);
			if (!hidden)
				hidden = rb_find_bss(rdev, tmp,
						     BSS_CMP_HIDE_NUL);
			if (hidden) {
				new->pub.hidden_beacon_bss = &hidden->pub;
				list_add(&new->hidden_list,
					 &hidden->hidden_list);
				hidden->refcount++;
				rcu_assign_pointer(new->pub.beacon_ies,
						   hidden->pub.beacon_ies);
			}
		} else {
			/*
			 * Ok so we found a beacon, and don't have an entry. If
			 * it's a beacon with hidden SSID, we might be in for an
			 * expensive search for any probe responses that should
			 * be grouped with this beacon for updates ...
			 */
			if (!cfg80211_combine_bsses(rdev, new)) {
				bss_ref_put(rdev, new);
				goto drop;
			}
		}

		if (rdev->bss_entries >= bss_entries_limit &&
		    !cfg80211_bss_expire_oldest(rdev)) {
			bss_ref_put(rdev, new);
			goto drop;
		}

		/* This must be before the call to bss_ref_get */
		if (tmp->pub.transmitted_bss) {
			struct cfg80211_internal_bss *pbss =
				container_of(tmp->pub.transmitted_bss,
					     struct cfg80211_internal_bss,
					     pub);

			new->pub.transmitted_bss = tmp->pub.transmitted_bss;
			bss_ref_get(rdev, pbss);
		}

		list_add_tail(&new->list, &rdev->bss_list);
		rdev->bss_entries++;
		rb_insert_bss(rdev, new);
		found = new;
	}

	rdev->bss_generation++;
	bss_ref_get(rdev, found);
	spin_unlock_bh(&rdev->bss_lock);

	return found;
 drop:
	spin_unlock_bh(&rdev->bss_lock);
	return NULL;
}

/*
 * Update RX channel information based on the available frame payload
 * information. This is mainly for the 2.4 GHz band where frames can be received
 * from neighboring channels and the Beacon frames use the DSSS Parameter Set
 * element to indicate the current (transmitting) channel, but this might also
 * be needed on other bands if RX frequency does not match with the actual
 * operating channel of a BSS.
 */
static struct ieee80211_channel *
cfg80211_get_bss_channel(struct wiphy *wiphy, const u8 *ie, size_t ielen,
			 struct ieee80211_channel *channel,
			 enum nl80211_bss_scan_width scan_width)
{
	const u8 *tmp;
	u32 freq;
	int channel_number = -1;
	struct ieee80211_channel *alt_channel;

	if (channel->band == NL80211_BAND_S1GHZ) {
		tmp = cfg80211_find_ie(WLAN_EID_S1G_OPERATION, ie, ielen);
		if (tmp && tmp[1] >= sizeof(struct ieee80211_s1g_oper_ie)) {
			struct ieee80211_s1g_oper_ie *s1gop = (void *)(tmp + 2);

			channel_number = s1gop->primary_ch;
		}
	} else {
		tmp = cfg80211_find_ie(WLAN_EID_DS_PARAMS, ie, ielen);
		if (tmp && tmp[1] == 1) {
			channel_number = tmp[2];
		} else {
			tmp = cfg80211_find_ie(WLAN_EID_HT_OPERATION, ie, ielen);
			if (tmp && tmp[1] >= sizeof(struct ieee80211_ht_operation)) {
				struct ieee80211_ht_operation *htop = (void *)(tmp + 2);

				channel_number = htop->primary_chan;
			}
		}
	}

	if (channel_number < 0) {
		/* No channel information in frame payload */
		return channel;
	}

	freq = ieee80211_channel_to_freq_khz(channel_number, channel->band);
	alt_channel = ieee80211_get_channel_khz(wiphy, freq);
	if (!alt_channel) {
		if (channel->band == NL80211_BAND_2GHZ) {
			/*
			 * Better not allow unexpected channels when that could
			 * be going beyond the 1-11 range (e.g., discovering
			 * BSS on channel 12 when radio is configured for
			 * channel 11.
			 */
			return NULL;
		}

		/* No match for the payload channel number - ignore it */
		return channel;
	}

	if (scan_width == NL80211_BSS_CHAN_WIDTH_10 ||
	    scan_width == NL80211_BSS_CHAN_WIDTH_5) {
		/*
		 * Ignore channel number in 5 and 10 MHz channels where there
		 * may not be an n:1 or 1:n mapping between frequencies and
		 * channel numbers.
		 */
		return channel;
	}

	/*
	 * Use the channel determined through the payload channel number
	 * instead of the RX channel reported by the driver.
	 */
	if (alt_channel->flags & IEEE80211_CHAN_DISABLED)
		return NULL;
	return alt_channel;
}

/* Returned bss is reference counted and must be cleaned up appropriately. */
static struct cfg80211_bss *
cfg80211_inform_single_bss_data(struct wiphy *wiphy,
				struct cfg80211_inform_bss *data,
				enum cfg80211_bss_frame_type ftype,
				const u8 *bssid, u64 tsf, u16 capability,
				u16 beacon_interval, const u8 *ie, size_t ielen,
				struct cfg80211_non_tx_bss *non_tx_data,
				gfp_t gfp)
{
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wiphy);
	struct cfg80211_bss_ies *ies;
	struct ieee80211_channel *channel;
	struct cfg80211_internal_bss tmp = {}, *res;
	int bss_type;
	bool signal_valid;
	unsigned long ts;

	if (WARN_ON(!wiphy))
		return NULL;

	if (WARN_ON(wiphy->signal_type == CFG80211_SIGNAL_TYPE_UNSPEC &&
		    (data->signal < 0 || data->signal > 100)))
		return NULL;

	channel = cfg80211_get_bss_channel(wiphy, ie, ielen, data->chan,
					   data->scan_width);
	if (!channel)
		return NULL;

	memcpy(tmp.pub.bssid, bssid, ETH_ALEN);
	tmp.pub.channel = channel;
	tmp.pub.scan_width = data->scan_width;
	tmp.pub.signal = data->signal;
	tmp.pub.beacon_interval = beacon_interval;
	tmp.pub.capability = capability;
	tmp.ts_boottime = data->boottime_ns;
	tmp.parent_tsf = data->parent_tsf;
	ether_addr_copy(tmp.parent_bssid, data->parent_bssid);

	if (non_tx_data) {
		tmp.pub.transmitted_bss = non_tx_data->tx_bss;
		ts = bss_from_pub(non_tx_data->tx_bss)->ts;
		tmp.pub.bssid_index = non_tx_data->bssid_index;
		tmp.pub.max_bssid_indicator = non_tx_data->max_bssid_indicator;
	} else {
		ts = jiffies;
	}

	/*
	 * If we do not know here whether the IEs are from a Beacon or Probe
	 * Response frame, we need to pick one of the options and only use it
	 * with the driver that does not provide the full Beacon/Probe Response
	 * frame. Use Beacon frame pointer to avoid indicating that this should
	 * override the IEs pointer should we have received an earlier
	 * indication of Probe Response data.
	 */
	ies = kzalloc(sizeof(*ies) + ielen, gfp);
	if (!ies)
		return NULL;
	ies->len = ielen;
	ies->tsf = tsf;
	ies->from_beacon = false;
	memcpy(ies->data, ie, ielen);

	switch (ftype) {
	case CFG80211_BSS_FTYPE_BEACON:
		ies->from_beacon = true;
		fallthrough;
	case CFG80211_BSS_FTYPE_UNKNOWN:
		rcu_assign_pointer(tmp.pub.beacon_ies, ies);
		break;
	case CFG80211_BSS_FTYPE_PRESP:
		rcu_assign_pointer(tmp.pub.proberesp_ies, ies);
		break;
	}
	rcu_assign_pointer(tmp.pub.ies, ies);

	signal_valid = data->chan == channel;
	res = cfg80211_bss_update(wiphy_to_rdev(wiphy), &tmp, signal_valid, ts);
	if (!res)
		return NULL;

	if (channel->band == NL80211_BAND_60GHZ) {
		bss_type = res->pub.capability & WLAN_CAPABILITY_DMG_TYPE_MASK;
		if (bss_type == WLAN_CAPABILITY_DMG_TYPE_AP ||
		    bss_type == WLAN_CAPABILITY_DMG_TYPE_PBSS)
			regulatory_hint_found_beacon(wiphy, channel, gfp);
	} else {
		if (res->pub.capability & WLAN_CAPABILITY_ESS)
			regulatory_hint_found_beacon(wiphy, channel, gfp);
	}

	if (non_tx_data) {
		/* this is a nontransmitting bss, we need to add it to
		 * transmitting bss' list if it is not there
		 */
		spin_lock_bh(&rdev->bss_lock);
		if (cfg80211_add_nontrans_list(non_tx_data->tx_bss,
					       &res->pub)) {
			if (__cfg80211_unlink_bss(rdev, res))
				rdev->bss_generation++;
		}
		spin_unlock_bh(&rdev->bss_lock);
	}

	trace_cfg80211_return_bss(&res->pub);
	/* cfg80211_bss_update gives us a referenced result */
	return &res->pub;
}

static const struct element
*cfg80211_get_profile_continuation(const u8 *ie, size_t ielen,
				   const struct element *mbssid_elem,
				   const struct element *sub_elem)
{
	const u8 *mbssid_end = mbssid_elem->data + mbssid_elem->datalen;
	const struct element *next_mbssid;
	const struct element *next_sub;

	next_mbssid = cfg80211_find_elem(WLAN_EID_MULTIPLE_BSSID,
					 mbssid_end,
					 ielen - (mbssid_end - ie));

	/*
	 * If it is not the last subelement in current MBSSID IE or there isn't
	 * a next MBSSID IE - profile is complete.
	*/
	if ((sub_elem->data + sub_elem->datalen < mbssid_end - 1) ||
	    !next_mbssid)
		return NULL;

	/* For any length error, just return NULL */

	if (next_mbssid->datalen < 4)
		return NULL;

	next_sub = (void *)&next_mbssid->data[1];

	if (next_mbssid->data + next_mbssid->datalen <
	    next_sub->data + next_sub->datalen)
		return NULL;

	if (next_sub->id != 0 || next_sub->datalen < 2)
		return NULL;

	/*
	 * Check if the first element in the next sub element is a start
	 * of a new profile
	 */
	return next_sub->data[0] == WLAN_EID_NON_TX_BSSID_CAP ?
	       NULL : next_mbssid;
}

size_t cfg80211_merge_profile(const u8 *ie, size_t ielen,
			      const struct element *mbssid_elem,
			      const struct element *sub_elem,
			      u8 *merged_ie, size_t max_copy_len)
{
	size_t copied_len = sub_elem->datalen;
	const struct element *next_mbssid;

	if (sub_elem->datalen > max_copy_len)
		return 0;

	memcpy(merged_ie, sub_elem->data, sub_elem->datalen);

	while ((next_mbssid = cfg80211_get_profile_continuation(ie, ielen,
								mbssid_elem,
								sub_elem))) {
		const struct element *next_sub = (void *)&next_mbssid->data[1];

		if (copied_len + next_sub->datalen > max_copy_len)
			break;
		memcpy(merged_ie + copied_len, next_sub->data,
		       next_sub->datalen);
		copied_len += next_sub->datalen;
	}

	return copied_len;
}
EXPORT_SYMBOL(cfg80211_merge_profile);

static void cfg80211_parse_mbssid_data(struct wiphy *wiphy,
				       struct cfg80211_inform_bss *data,
				       enum cfg80211_bss_frame_type ftype,
				       const u8 *bssid, u64 tsf,
				       u16 beacon_interval, const u8 *ie,
				       size_t ielen,
				       struct cfg80211_non_tx_bss *non_tx_data,
				       gfp_t gfp)
{
	const u8 *mbssid_index_ie;
	const struct element *elem, *sub;
	size_t new_ie_len;
	u8 new_bssid[ETH_ALEN];
	u8 *new_ie, *profile;
	u64 seen_indices = 0;
	u16 capability;
	struct cfg80211_bss *bss;

	if (!non_tx_data)
		return;
	if (!cfg80211_find_ie(WLAN_EID_MULTIPLE_BSSID, ie, ielen))
		return;
	if (!wiphy->support_mbssid)
		return;
	if (wiphy->support_only_he_mbssid &&
	    !cfg80211_find_ext_ie(WLAN_EID_EXT_HE_CAPABILITY, ie, ielen))
		return;

	new_ie = kmalloc(IEEE80211_MAX_DATA_LEN, gfp);
	if (!new_ie)
		return;

	profile = kmalloc(ielen, gfp);
	if (!profile)
		goto out;

	for_each_element_id(elem, WLAN_EID_MULTIPLE_BSSID, ie, ielen) {
		if (elem->datalen < 4)
			continue;
		for_each_element(sub, elem->data + 1, elem->datalen - 1) {
			u8 profile_len;

			if (sub->id != 0 || sub->datalen < 4) {
				/* not a valid BSS profile */
				continue;
			}

			if (sub->data[0] != WLAN_EID_NON_TX_BSSID_CAP ||
			    sub->data[1] != 2) {
				/* The first element within the Nontransmitted
				 * BSSID Profile is not the Nontransmitted
				 * BSSID Capability element.
				 */
				continue;
			}

			memset(profile, 0, ielen);
			profile_len = cfg80211_merge_profile(ie, ielen,
							     elem,
							     sub,
							     profile,
							     ielen);

			/* found a Nontransmitted BSSID Profile */
			mbssid_index_ie = cfg80211_find_ie
				(WLAN_EID_MULTI_BSSID_IDX,
				 profile, profile_len);
			if (!mbssid_index_ie || mbssid_index_ie[1] < 1 ||
			    mbssid_index_ie[2] == 0 ||
			    mbssid_index_ie[2] > 46) {
				/* No valid Multiple BSSID-Index element */
				continue;
			}

			if (seen_indices & BIT_ULL(mbssid_index_ie[2]))
				/* We don't support legacy split of a profile */
				net_dbg_ratelimited("Partial info for BSSID index %d\n",
						    mbssid_index_ie[2]);

			seen_indices |= BIT_ULL(mbssid_index_ie[2]);

			non_tx_data->bssid_index = mbssid_index_ie[2];
			non_tx_data->max_bssid_indicator = elem->data[0];

			cfg80211_gen_new_bssid(bssid,
					       non_tx_data->max_bssid_indicator,
					       non_tx_data->bssid_index,
					       new_bssid);
			memset(new_ie, 0, IEEE80211_MAX_DATA_LEN);
			new_ie_len = cfg80211_gen_new_ie(ie, ielen,
							 profile,
							 profile_len, new_ie,
							 gfp);
			if (!new_ie_len)
				continue;

			capability = get_unaligned_le16(profile + 2);
			bss = cfg80211_inform_single_bss_data(wiphy, data,
							      ftype,
							      new_bssid, tsf,
							      capability,
							      beacon_interval,
							      new_ie,
							      new_ie_len,
							      non_tx_data,
							      gfp);
			if (!bss)
				break;
			cfg80211_put_bss(wiphy, bss);
		}
	}

out:
	kfree(new_ie);
	kfree(profile);
}

struct cfg80211_bss *
cfg80211_inform_bss_data(struct wiphy *wiphy,
			 struct cfg80211_inform_bss *data,
			 enum cfg80211_bss_frame_type ftype,
			 const u8 *bssid, u64 tsf, u16 capability,
			 u16 beacon_interval, const u8 *ie, size_t ielen,
			 gfp_t gfp)
{
	struct cfg80211_bss *res;
	struct cfg80211_non_tx_bss non_tx_data;

	res = cfg80211_inform_single_bss_data(wiphy, data, ftype, bssid, tsf,
					      capability, beacon_interval, ie,
					      ielen, NULL, gfp);
	if (!res)
		return NULL;
	non_tx_data.tx_bss = res;
	cfg80211_parse_mbssid_data(wiphy, data, ftype, bssid, tsf,
				   beacon_interval, ie, ielen, &non_tx_data,
				   gfp);
	return res;
}
EXPORT_SYMBOL(cfg80211_inform_bss_data);

static void
cfg80211_parse_mbssid_frame_data(struct wiphy *wiphy,
				 struct cfg80211_inform_bss *data,
				 struct ieee80211_mgmt *mgmt, size_t len,
				 struct cfg80211_non_tx_bss *non_tx_data,
				 gfp_t gfp)
{
	enum cfg80211_bss_frame_type ftype;
	const u8 *ie = mgmt->u.probe_resp.variable;
	size_t ielen = len - offsetof(struct ieee80211_mgmt,
				      u.probe_resp.variable);

	ftype = ieee80211_is_beacon(mgmt->frame_control) ?
		CFG80211_BSS_FTYPE_BEACON : CFG80211_BSS_FTYPE_PRESP;

	cfg80211_parse_mbssid_data(wiphy, data, ftype, mgmt->bssid,
				   le64_to_cpu(mgmt->u.probe_resp.timestamp),
				   le16_to_cpu(mgmt->u.probe_resp.beacon_int),
				   ie, ielen, non_tx_data, gfp);
}

static void
cfg80211_update_notlisted_nontrans(struct wiphy *wiphy,
				   struct cfg80211_bss *nontrans_bss,
				   struct ieee80211_mgmt *mgmt, size_t len)
{
	u8 *ie, *new_ie, *pos;
	const u8 *nontrans_ssid, *trans_ssid, *mbssid;
	size_t ielen = len - offsetof(struct ieee80211_mgmt,
				      u.probe_resp.variable);
	size_t new_ie_len;
	struct cfg80211_bss_ies *new_ies;
	const struct cfg80211_bss_ies *old;
	u8 cpy_len;

	lockdep_assert_held(&wiphy_to_rdev(wiphy)->bss_lock);

	ie = mgmt->u.probe_resp.variable;

	new_ie_len = ielen;
	trans_ssid = cfg80211_find_ie(WLAN_EID_SSID, ie, ielen);
	if (!trans_ssid)
		return;
	new_ie_len -= trans_ssid[1];
	mbssid = cfg80211_find_ie(WLAN_EID_MULTIPLE_BSSID, ie, ielen);
	/*
	 * It's not valid to have the MBSSID element before SSID
	 * ignore if that happens - the code below assumes it is
	 * after (while copying things inbetween).
	 */
	if (!mbssid || mbssid < trans_ssid)
		return;
	new_ie_len -= mbssid[1];

	nontrans_ssid = ieee80211_bss_get_ie(nontrans_bss, WLAN_EID_SSID);
	if (!nontrans_ssid)
		return;

	new_ie_len += nontrans_ssid[1];

	/* generate new ie for nontrans BSS
	 * 1. replace SSID with nontrans BSS' SSID
	 * 2. skip MBSSID IE
	 */
	new_ie = kzalloc(new_ie_len, GFP_ATOMIC);
	if (!new_ie)
		return;

	new_ies = kzalloc(sizeof(*new_ies) + new_ie_len, GFP_ATOMIC);
	if (!new_ies)
		goto out_free;

	pos = new_ie;

	/* copy the nontransmitted SSID */
	cpy_len = nontrans_ssid[1] + 2;
	memcpy(pos, nontrans_ssid, cpy_len);
	pos += cpy_len;
	/* copy the IEs between SSID and MBSSID */
	cpy_len = trans_ssid[1] + 2;
	memcpy(pos, (trans_ssid + cpy_len), (mbssid - (trans_ssid + cpy_len)));
	pos += (mbssid - (trans_ssid + cpy_len));
	/* copy the IEs after MBSSID */
	cpy_len = mbssid[1] + 2;
	memcpy(pos, mbssid + cpy_len, ((ie + ielen) - (mbssid + cpy_len)));

	/* update ie */
	new_ies->len = new_ie_len;
	new_ies->tsf = le64_to_cpu(mgmt->u.probe_resp.timestamp);
	new_ies->from_beacon = ieee80211_is_beacon(mgmt->frame_control);
	memcpy(new_ies->data, new_ie, new_ie_len);
	if (ieee80211_is_probe_resp(mgmt->frame_control)) {
		old = rcu_access_pointer(nontrans_bss->proberesp_ies);
		rcu_assign_pointer(nontrans_bss->proberesp_ies, new_ies);
		rcu_assign_pointer(nontrans_bss->ies, new_ies);
		if (old)
			kfree_rcu((struct cfg80211_bss_ies *)old, rcu_head);
	} else {
		old = rcu_access_pointer(nontrans_bss->beacon_ies);
		rcu_assign_pointer(nontrans_bss->beacon_ies, new_ies);
		rcu_assign_pointer(nontrans_bss->ies, new_ies);
		if (old)
			kfree_rcu((struct cfg80211_bss_ies *)old, rcu_head);
	}

out_free:
	kfree(new_ie);
}

/* cfg80211_inform_bss_width_frame helper */
static struct cfg80211_bss *
cfg80211_inform_single_bss_frame_data(struct wiphy *wiphy,
				      struct cfg80211_inform_bss *data,
				      struct ieee80211_mgmt *mgmt, size_t len,
				      gfp_t gfp)
{
	struct cfg80211_internal_bss tmp = {}, *res;
	struct cfg80211_bss_ies *ies;
	struct ieee80211_channel *channel;
	bool signal_valid;
	struct ieee80211_ext *ext = NULL;
	u8 *bssid, *variable;
	u16 capability, beacon_int;
	size_t ielen, min_hdr_len = offsetof(struct ieee80211_mgmt,
					     u.probe_resp.variable);
	int bss_type;

	BUILD_BUG_ON(offsetof(struct ieee80211_mgmt, u.probe_resp.variable) !=
			offsetof(struct ieee80211_mgmt, u.beacon.variable));

	trace_cfg80211_inform_bss_frame(wiphy, data, mgmt, len);

	if (WARN_ON(!mgmt))
		return NULL;

	if (WARN_ON(!wiphy))
		return NULL;

	if (WARN_ON(wiphy->signal_type == CFG80211_SIGNAL_TYPE_UNSPEC &&
		    (data->signal < 0 || data->signal > 100)))
		return NULL;

	if (ieee80211_is_s1g_beacon(mgmt->frame_control)) {
		ext = (void *) mgmt;
		min_hdr_len = offsetof(struct ieee80211_ext, u.s1g_beacon);
		if (ieee80211_is_s1g_short_beacon(mgmt->frame_control))
			min_hdr_len = offsetof(struct ieee80211_ext,
					       u.s1g_short_beacon.variable);
	}

	if (WARN_ON(len < min_hdr_len))
		return NULL;

	ielen = len - min_hdr_len;
	variable = mgmt->u.probe_resp.variable;
	if (ext) {
		if (ieee80211_is_s1g_short_beacon(mgmt->frame_control))
			variable = ext->u.s1g_short_beacon.variable;
		else
			variable = ext->u.s1g_beacon.variable;
	}

	channel = cfg80211_get_bss_channel(wiphy, variable,
					   ielen, data->chan, data->scan_width);
	if (!channel)
		return NULL;

	if (ext) {
		const struct ieee80211_s1g_bcn_compat_ie *compat;
		const struct element *elem;

		elem = cfg80211_find_elem(WLAN_EID_S1G_BCN_COMPAT,
					  variable, ielen);
		if (!elem)
			return NULL;
		if (elem->datalen < sizeof(*compat))
			return NULL;
		compat = (void *)elem->data;
		bssid = ext->u.s1g_beacon.sa;
		capability = le16_to_cpu(compat->compat_info);
		beacon_int = le16_to_cpu(compat->beacon_int);
	} else {
		bssid = mgmt->bssid;
		beacon_int = le16_to_cpu(mgmt->u.probe_resp.beacon_int);
		capability = le16_to_cpu(mgmt->u.probe_resp.capab_info);
	}

	ies = kzalloc(sizeof(*ies) + ielen, gfp);
	if (!ies)
		return NULL;
	ies->len = ielen;
	ies->tsf = le64_to_cpu(mgmt->u.probe_resp.timestamp);
	ies->from_beacon = ieee80211_is_beacon(mgmt->frame_control) ||
			   ieee80211_is_s1g_beacon(mgmt->frame_control);
	memcpy(ies->data, variable, ielen);

	if (ieee80211_is_probe_resp(mgmt->frame_control))
		rcu_assign_pointer(tmp.pub.proberesp_ies, ies);
	else
		rcu_assign_pointer(tmp.pub.beacon_ies, ies);
	rcu_assign_pointer(tmp.pub.ies, ies);

	memcpy(tmp.pub.bssid, bssid, ETH_ALEN);
	tmp.pub.beacon_interval = beacon_int;
	tmp.pub.capability = capability;
	tmp.pub.channel = channel;
	tmp.pub.scan_width = data->scan_width;
	tmp.pub.signal = data->signal;
	tmp.ts_boottime = data->boottime_ns;
	tmp.parent_tsf = data->parent_tsf;
	tmp.pub.chains = data->chains;
	memcpy(tmp.pub.chain_signal, data->chain_signal, IEEE80211_MAX_CHAINS);
	ether_addr_copy(tmp.parent_bssid, data->parent_bssid);

	signal_valid = data->chan == channel;
	res = cfg80211_bss_update(wiphy_to_rdev(wiphy), &tmp, signal_valid,
				  jiffies);
	if (!res)
		return NULL;

	if (channel->band == NL80211_BAND_60GHZ) {
		bss_type = res->pub.capability & WLAN_CAPABILITY_DMG_TYPE_MASK;
		if (bss_type == WLAN_CAPABILITY_DMG_TYPE_AP ||
		    bss_type == WLAN_CAPABILITY_DMG_TYPE_PBSS)
			regulatory_hint_found_beacon(wiphy, channel, gfp);
	} else {
		if (res->pub.capability & WLAN_CAPABILITY_ESS)
			regulatory_hint_found_beacon(wiphy, channel, gfp);
	}

	trace_cfg80211_return_bss(&res->pub);
	/* cfg80211_bss_update gives us a referenced result */
	return &res->pub;
}

struct cfg80211_bss *
cfg80211_inform_bss_frame_data(struct wiphy *wiphy,
			       struct cfg80211_inform_bss *data,
			       struct ieee80211_mgmt *mgmt, size_t len,
			       gfp_t gfp)
{
	struct cfg80211_bss *res, *tmp_bss;
	const u8 *ie = mgmt->u.probe_resp.variable;
	const struct cfg80211_bss_ies *ies1, *ies2;
	size_t ielen = len - offsetof(struct ieee80211_mgmt,
				      u.probe_resp.variable);
	struct cfg80211_non_tx_bss non_tx_data;

	res = cfg80211_inform_single_bss_frame_data(wiphy, data, mgmt,
						    len, gfp);
	if (!res || !wiphy->support_mbssid ||
	    !cfg80211_find_ie(WLAN_EID_MULTIPLE_BSSID, ie, ielen))
		return res;
	if (wiphy->support_only_he_mbssid &&
	    !cfg80211_find_ext_ie(WLAN_EID_EXT_HE_CAPABILITY, ie, ielen))
		return res;

	non_tx_data.tx_bss = res;
	/* process each non-transmitting bss */
	cfg80211_parse_mbssid_frame_data(wiphy, data, mgmt, len,
					 &non_tx_data, gfp);

	spin_lock_bh(&wiphy_to_rdev(wiphy)->bss_lock);

	/* check if the res has other nontransmitting bss which is not
	 * in MBSSID IE
	 */
	ies1 = rcu_access_pointer(res->ies);

	/* go through nontrans_list, if the timestamp of the BSS is
	 * earlier than the timestamp of the transmitting BSS then
	 * update it
	 */
	list_for_each_entry(tmp_bss, &res->nontrans_list,
			    nontrans_list) {
		ies2 = rcu_access_pointer(tmp_bss->ies);
		if (ies2->tsf < ies1->tsf)
			cfg80211_update_notlisted_nontrans(wiphy, tmp_bss,
							   mgmt, len);
	}
	spin_unlock_bh(&wiphy_to_rdev(wiphy)->bss_lock);

	return res;
}
EXPORT_SYMBOL(cfg80211_inform_bss_frame_data);

void cfg80211_ref_bss(struct wiphy *wiphy, struct cfg80211_bss *pub)
{
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wiphy);
	struct cfg80211_internal_bss *bss;

	if (!pub)
		return;

	bss = container_of(pub, struct cfg80211_internal_bss, pub);

	spin_lock_bh(&rdev->bss_lock);
	bss_ref_get(rdev, bss);
	spin_unlock_bh(&rdev->bss_lock);
}
EXPORT_SYMBOL(cfg80211_ref_bss);

void cfg80211_put_bss(struct wiphy *wiphy, struct cfg80211_bss *pub)
{
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wiphy);
	struct cfg80211_internal_bss *bss;

	if (!pub)
		return;

	bss = container_of(pub, struct cfg80211_internal_bss, pub);

	spin_lock_bh(&rdev->bss_lock);
	bss_ref_put(rdev, bss);
	spin_unlock_bh(&rdev->bss_lock);
}
EXPORT_SYMBOL(cfg80211_put_bss);

void cfg80211_unlink_bss(struct wiphy *wiphy, struct cfg80211_bss *pub)
{
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wiphy);
	struct cfg80211_internal_bss *bss, *tmp1;
	struct cfg80211_bss *nontrans_bss, *tmp;

	if (WARN_ON(!pub))
		return;

	bss = container_of(pub, struct cfg80211_internal_bss, pub);

	spin_lock_bh(&rdev->bss_lock);
	if (list_empty(&bss->list))
		goto out;

	list_for_each_entry_safe(nontrans_bss, tmp,
				 &pub->nontrans_list,
				 nontrans_list) {
		tmp1 = container_of(nontrans_bss,
				    struct cfg80211_internal_bss, pub);
		if (__cfg80211_unlink_bss(rdev, tmp1))
			rdev->bss_generation++;
	}

	if (__cfg80211_unlink_bss(rdev, bss))
		rdev->bss_generation++;
out:
	spin_unlock_bh(&rdev->bss_lock);
}
EXPORT_SYMBOL(cfg80211_unlink_bss);

void cfg80211_bss_iter(struct wiphy *wiphy,
		       struct cfg80211_chan_def *chandef,
		       void (*iter)(struct wiphy *wiphy,
				    struct cfg80211_bss *bss,
				    void *data),
		       void *iter_data)
{
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wiphy);
	struct cfg80211_internal_bss *bss;

	spin_lock_bh(&rdev->bss_lock);

	list_for_each_entry(bss, &rdev->bss_list, list) {
		if (!chandef || cfg80211_is_sub_chan(chandef, bss->pub.channel))
			iter(wiphy, &bss->pub, iter_data);
	}

	spin_unlock_bh(&rdev->bss_lock);
}
EXPORT_SYMBOL(cfg80211_bss_iter);

void cfg80211_update_assoc_bss_entry(struct wireless_dev *wdev,
				     struct ieee80211_channel *chan)
{
	struct wiphy *wiphy = wdev->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wiphy);
	struct cfg80211_internal_bss *cbss = wdev->current_bss;
	struct cfg80211_internal_bss *new = NULL;
	struct cfg80211_internal_bss *bss;
	struct cfg80211_bss *nontrans_bss;
	struct cfg80211_bss *tmp;

	spin_lock_bh(&rdev->bss_lock);

	/*
	 * Some APs use CSA also for bandwidth changes, i.e., without actually
	 * changing the control channel, so no need to update in such a case.
	 */
	if (cbss->pub.channel == chan)
		goto done;

	/* use transmitting bss */
	if (cbss->pub.transmitted_bss)
		cbss = container_of(cbss->pub.transmitted_bss,
				    struct cfg80211_internal_bss,
				    pub);

	cbss->pub.channel = chan;

	list_for_each_entry(bss, &rdev->bss_list, list) {
		if (!cfg80211_bss_type_match(bss->pub.capability,
					     bss->pub.channel->band,
					     wdev->conn_bss_type))
			continue;

		if (bss == cbss)
			continue;

		if (!cmp_bss(&bss->pub, &cbss->pub, BSS_CMP_REGULAR)) {
			new = bss;
			break;
		}
	}

	if (new) {
		/* to save time, update IEs for transmitting bss only */
		if (cfg80211_update_known_bss(rdev, cbss, new, false)) {
			new->pub.proberesp_ies = NULL;
			new->pub.beacon_ies = NULL;
		}

		list_for_each_entry_safe(nontrans_bss, tmp,
					 &new->pub.nontrans_list,
					 nontrans_list) {
			bss = container_of(nontrans_bss,
					   struct cfg80211_internal_bss, pub);
			if (__cfg80211_unlink_bss(rdev, bss))
				rdev->bss_generation++;
		}

		WARN_ON(atomic_read(&new->hold));
		if (!WARN_ON(!__cfg80211_unlink_bss(rdev, new)))
			rdev->bss_generation++;
	}

	rb_erase(&cbss->rbn, &rdev->bss_tree);
	rb_insert_bss(rdev, cbss);
	rdev->bss_generation++;

	list_for_each_entry_safe(nontrans_bss, tmp,
				 &cbss->pub.nontrans_list,
				 nontrans_list) {
		bss = container_of(nontrans_bss,
				   struct cfg80211_internal_bss, pub);
		bss->pub.channel = chan;
		rb_erase(&bss->rbn, &rdev->bss_tree);
		rb_insert_bss(rdev, bss);
		rdev->bss_generation++;
	}

done:
	spin_unlock_bh(&rdev->bss_lock);
}

#ifdef CONFIG_CFG80211_WEXT
static struct cfg80211_registered_device *
cfg80211_get_dev_from_ifindex(struct net *net, int ifindex)
{
	struct cfg80211_registered_device *rdev;
	struct net_device *dev;

	ASSERT_RTNL();

	dev = dev_get_by_index(net, ifindex);
	if (!dev)
		return ERR_PTR(-ENODEV);
	if (dev->ieee80211_ptr)
		rdev = wiphy_to_rdev(dev->ieee80211_ptr->wiphy);
	else
		rdev = ERR_PTR(-ENODEV);
	dev_put(dev);
	return rdev;
}

int cfg80211_wext_siwscan(struct net_device *dev,
			  struct iw_request_info *info,
			  union iwreq_data *wrqu, char *extra)
{
	struct cfg80211_registered_device *rdev;
	struct wiphy *wiphy;
	struct iw_scan_req *wreq = NULL;
	struct cfg80211_scan_request *creq = NULL;
	int i, err, n_channels = 0;
	enum nl80211_band band;

	if (!netif_running(dev))
		return -ENETDOWN;

	if (wrqu->data.length == sizeof(struct iw_scan_req))
		wreq = (struct iw_scan_req *)extra;

	rdev = cfg80211_get_dev_from_ifindex(dev_net(dev), dev->ifindex);

	if (IS_ERR(rdev))
		return PTR_ERR(rdev);

	if (rdev->scan_req || rdev->scan_msg) {
		err = -EBUSY;
		goto out;
	}

	wiphy = &rdev->wiphy;

	/* Determine number of channels, needed to allocate creq */
	if (wreq && wreq->num_channels)
		n_channels = wreq->num_channels;
	else
		n_channels = ieee80211_get_num_supported_channels(wiphy);

	creq = kzalloc(sizeof(*creq) + sizeof(struct cfg80211_ssid) +
		       n_channels * sizeof(void *),
		       GFP_ATOMIC);
	if (!creq) {
		err = -ENOMEM;
		goto out;
	}

	creq->wiphy = wiphy;
	creq->wdev = dev->ieee80211_ptr;
	/* SSIDs come after channels */
	creq->ssids = (void *)&creq->channels[n_channels];
	creq->n_channels = n_channels;
	creq->n_ssids = 1;
	creq->scan_start = jiffies;

	/* translate "Scan on frequencies" request */
	i = 0;
	for (band = 0; band < NUM_NL80211_BANDS; band++) {
		int j;

		if (!wiphy->bands[band])
			continue;

		for (j = 0; j < wiphy->bands[band]->n_channels; j++) {
			/* ignore disabled channels */
			if (wiphy->bands[band]->channels[j].flags &
						IEEE80211_CHAN_DISABLED)
				continue;

			/* If we have a wireless request structure and the
			 * wireless request specifies frequencies, then search
			 * for the matching hardware channel.
			 */
			if (wreq && wreq->num_channels) {
				int k;
				int wiphy_freq = wiphy->bands[band]->channels[j].center_freq;
				for (k = 0; k < wreq->num_channels; k++) {
					struct iw_freq *freq =
						&wreq->channel_list[k];
					int wext_freq =
						cfg80211_wext_freq(freq);

					if (wext_freq == wiphy_freq)
						goto wext_freq_found;
				}
				goto wext_freq_not_found;
			}

		wext_freq_found:
			creq->channels[i] = &wiphy->bands[band]->channels[j];
			i++;
		wext_freq_not_found: ;
		}
	}
	/* No channels found? */
	if (!i) {
		err = -EINVAL;
		goto out;
	}

	/* Set real number of channels specified in creq->channels[] */
	creq->n_channels = i;

	/* translate "Scan for SSID" request */
	if (wreq) {
		if (wrqu->data.flags & IW_SCAN_THIS_ESSID) {
			if (wreq->essid_len > IEEE80211_MAX_SSID_LEN) {
				err = -EINVAL;
				goto out;
			}
			memcpy(creq->ssids[0].ssid, wreq->essid, wreq->essid_len);
			creq->ssids[0].ssid_len = wreq->essid_len;
		}
		if (wreq->scan_type == IW_SCAN_TYPE_PASSIVE)
			creq->n_ssids = 0;
	}

	for (i = 0; i < NUM_NL80211_BANDS; i++)
		if (wiphy->bands[i])
			creq->rates[i] = (1 << wiphy->bands[i]->n_bitrates) - 1;

	eth_broadcast_addr(creq->bssid);

	wiphy_lock(&rdev->wiphy);

	rdev->scan_req = creq;
	err = rdev_scan(rdev, creq);
	if (err) {
		rdev->scan_req = NULL;
		/* creq will be freed below */
	} else {
		nl80211_send_scan_start(rdev, dev->ieee80211_ptr);
		/* creq now owned by driver */
		creq = NULL;
		dev_hold(dev);
	}
	wiphy_unlock(&rdev->wiphy);
 out:
	kfree(creq);
	return err;
}
EXPORT_WEXT_HANDLER(cfg80211_wext_siwscan);

static char *ieee80211_scan_add_ies(struct iw_request_info *info,
				    const struct cfg80211_bss_ies *ies,
				    char *current_ev, char *end_buf)
{
	const u8 *pos, *end, *next;
	struct iw_event iwe;

	if (!ies)
		return current_ev;

	/*
	 * If needed, fragment the IEs buffer (at IE boundaries) into short
	 * enough fragments to fit into IW_GENERIC_IE_MAX octet messages.
	 */
	pos = ies->data;
	end = pos + ies->len;

	while (end - pos > IW_GENERIC_IE_MAX) {
		next = pos + 2 + pos[1];
		while (next + 2 + next[1] - pos < IW_GENERIC_IE_MAX)
			next = next + 2 + next[1];

		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = IWEVGENIE;
		iwe.u.data.length = next - pos;
		current_ev = iwe_stream_add_point_check(info, current_ev,
							end_buf, &iwe,
							(void *)pos);
		if (IS_ERR(current_ev))
			return current_ev;
		pos = next;
	}

	if (end > pos) {
		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = IWEVGENIE;
		iwe.u.data.length = end - pos;
		current_ev = iwe_stream_add_point_check(info, current_ev,
							end_buf, &iwe,
							(void *)pos);
		if (IS_ERR(current_ev))
			return current_ev;
	}

	return current_ev;
}

static char *
ieee80211_bss(struct wiphy *wiphy, struct iw_request_info *info,
	      struct cfg80211_internal_bss *bss, char *current_ev,
	      char *end_buf)
{
	const struct cfg80211_bss_ies *ies;
	struct iw_event iwe;
	const u8 *ie;
	u8 buf[50];
	u8 *cfg, *p, *tmp;
	int rem, i, sig;
	bool ismesh = false;

	memset(&iwe, 0, sizeof(iwe));
	iwe.cmd = SIOCGIWAP;
	iwe.u.ap_addr.sa_family = ARPHRD_ETHER;
	memcpy(iwe.u.ap_addr.sa_data, bss->pub.bssid, ETH_ALEN);
	current_ev = iwe_stream_add_event_check(info, current_ev, end_buf, &iwe,
						IW_EV_ADDR_LEN);
	if (IS_ERR(current_ev))
		return current_ev;

	memset(&iwe, 0, sizeof(iwe));
	iwe.cmd = SIOCGIWFREQ;
	iwe.u.freq.m = ieee80211_frequency_to_channel(bss->pub.channel->center_freq);
	iwe.u.freq.e = 0;
	current_ev = iwe_stream_add_event_check(info, current_ev, end_buf, &iwe,
						IW_EV_FREQ_LEN);
	if (IS_ERR(current_ev))
		return current_ev;

	memset(&iwe, 0, sizeof(iwe));
	iwe.cmd = SIOCGIWFREQ;
	iwe.u.freq.m = bss->pub.channel->center_freq;
	iwe.u.freq.e = 6;
	current_ev = iwe_stream_add_event_check(info, current_ev, end_buf, &iwe,
						IW_EV_FREQ_LEN);
	if (IS_ERR(current_ev))
		return current_ev;

	if (wiphy->signal_type != CFG80211_SIGNAL_TYPE_NONE) {
		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = IWEVQUAL;
		iwe.u.qual.updated = IW_QUAL_LEVEL_UPDATED |
				     IW_QUAL_NOISE_INVALID |
				     IW_QUAL_QUAL_UPDATED;
		switch (wiphy->signal_type) {
		case CFG80211_SIGNAL_TYPE_MBM:
			sig = bss->pub.signal / 100;
			iwe.u.qual.level = sig;
			iwe.u.qual.updated |= IW_QUAL_DBM;
			if (sig < -110)		/* rather bad */
				sig = -110;
			else if (sig > -40)	/* perfect */
				sig = -40;
			/* will give a range of 0 .. 70 */
			iwe.u.qual.qual = sig + 110;
			break;
		case CFG80211_SIGNAL_TYPE_UNSPEC:
			iwe.u.qual.level = bss->pub.signal;
			/* will give range 0 .. 100 */
			iwe.u.qual.qual = bss->pub.signal;
			break;
		default:
			/* not reached */
			break;
		}
		current_ev = iwe_stream_add_event_check(info, current_ev,
							end_buf, &iwe,
							IW_EV_QUAL_LEN);
		if (IS_ERR(current_ev))
			return current_ev;
	}

	memset(&iwe, 0, sizeof(iwe));
	iwe.cmd = SIOCGIWENCODE;
	if (bss->pub.capability & WLAN_CAPABILITY_PRIVACY)
		iwe.u.data.flags = IW_ENCODE_ENABLED | IW_ENCODE_NOKEY;
	else
		iwe.u.data.flags = IW_ENCODE_DISABLED;
	iwe.u.data.length = 0;
	current_ev = iwe_stream_add_point_check(info, current_ev, end_buf,
						&iwe, "");
	if (IS_ERR(current_ev))
		return current_ev;

	rcu_read_lock();
	ies = rcu_dereference(bss->pub.ies);
	rem = ies->len;
	ie = ies->data;

	while (rem >= 2) {
		/* invalid data */
		if (ie[1] > rem - 2)
			break;

		switch (ie[0]) {
		case WLAN_EID_SSID:
			memset(&iwe, 0, sizeof(iwe));
			iwe.cmd = SIOCGIWESSID;
			iwe.u.data.length = ie[1];
			iwe.u.data.flags = 1;
			current_ev = iwe_stream_add_point_check(info,
								current_ev,
								end_buf, &iwe,
								(u8 *)ie + 2);
			if (IS_ERR(current_ev))
				goto unlock;
			break;
		case WLAN_EID_MESH_ID:
			memset(&iwe, 0, sizeof(iwe));
			iwe.cmd = SIOCGIWESSID;
			iwe.u.data.length = ie[1];
			iwe.u.data.flags = 1;
			current_ev = iwe_stream_add_point_check(info,
								current_ev,
								end_buf, &iwe,
								(u8 *)ie + 2);
			if (IS_ERR(current_ev))
				goto unlock;
			break;
		case WLAN_EID_MESH_CONFIG:
			ismesh = true;
			if (ie[1] != sizeof(struct ieee80211_meshconf_ie))
				break;
			cfg = (u8 *)ie + 2;
			memset(&iwe, 0, sizeof(iwe));
			iwe.cmd = IWEVCUSTOM;
			sprintf(buf, "Mesh Network Path Selection Protocol ID: "
				"0x%02X", cfg[0]);
			iwe.u.data.length = strlen(buf);
			current_ev = iwe_stream_add_point_check(info,
								current_ev,
								end_buf,
								&iwe, buf);
			if (IS_ERR(current_ev))
				goto unlock;
			sprintf(buf, "Path Selection Metric ID: 0x%02X",
				cfg[1]);
			iwe.u.data.length = strlen(buf);
			current_ev = iwe_stream_add_point_check(info,
								current_ev,
								end_buf,
								&iwe, buf);
			if (IS_ERR(current_ev))
				goto unlock;
			sprintf(buf, "Congestion Control Mode ID: 0x%02X",
				cfg[2]);
			iwe.u.data.length = strlen(buf);
			current_ev = iwe_stream_add_point_check(info,
								current_ev,
								end_buf,
								&iwe, buf);
			if (IS_ERR(current_ev))
				goto unlock;
			sprintf(buf, "Synchronization ID: 0x%02X", cfg[3]);
			iwe.u.data.length = strlen(buf);
			current_ev = iwe_stream_add_point_check(info,
								current_ev,
								end_buf,
								&iwe, buf);
			if (IS_ERR(current_ev))
				goto unlock;
			sprintf(buf, "Authentication ID: 0x%02X", cfg[4]);
			iwe.u.data.length = strlen(buf);
			current_ev = iwe_stream_add_point_check(info,
								current_ev,
								end_buf,
								&iwe, buf);
			if (IS_ERR(current_ev))
				goto unlock;
			sprintf(buf, "Formation Info: 0x%02X", cfg[5]);
			iwe.u.data.length = strlen(buf);
			current_ev = iwe_stream_add_point_check(info,
								current_ev,
								end_buf,
								&iwe, buf);
			if (IS_ERR(current_ev))
				goto unlock;
			sprintf(buf, "Capabilities: 0x%02X", cfg[6]);
			iwe.u.data.length = strlen(buf);
			current_ev = iwe_stream_add_point_check(info,
								current_ev,
								end_buf,
								&iwe, buf);
			if (IS_ERR(current_ev))
				goto unlock;
			break;
		case WLAN_EID_SUPP_RATES:
		case WLAN_EID_EXT_SUPP_RATES:
			/* display all supported rates in readable format */
			p = current_ev + iwe_stream_lcp_len(info);

			memset(&iwe, 0, sizeof(iwe));
			iwe.cmd = SIOCGIWRATE;
			/* Those two flags are ignored... */
			iwe.u.bitrate.fixed = iwe.u.bitrate.disabled = 0;

			for (i = 0; i < ie[1]; i++) {
				iwe.u.bitrate.value =
					((ie[i + 2] & 0x7f) * 500000);
				tmp = p;
				p = iwe_stream_add_value(info, current_ev, p,
							 end_buf, &iwe,
							 IW_EV_PARAM_LEN);
				if (p == tmp) {
					current_ev = ERR_PTR(-E2BIG);
					goto unlock;
				}
			}
			current_ev = p;
			break;
		}
		rem -= ie[1] + 2;
		ie += ie[1] + 2;
	}

	if (bss->pub.capability & (WLAN_CAPABILITY_ESS | WLAN_CAPABILITY_IBSS) ||
	    ismesh) {
		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = SIOCGIWMODE;
		if (ismesh)
			iwe.u.mode = IW_MODE_MESH;
		else if (bss->pub.capability & WLAN_CAPABILITY_ESS)
			iwe.u.mode = IW_MODE_MASTER;
		else
			iwe.u.mode = IW_MODE_ADHOC;
		current_ev = iwe_stream_add_event_check(info, current_ev,
							end_buf, &iwe,
							IW_EV_UINT_LEN);
		if (IS_ERR(current_ev))
			goto unlock;
	}

	memset(&iwe, 0, sizeof(iwe));
	iwe.cmd = IWEVCUSTOM;
	sprintf(buf, "tsf=%016llx", (unsigned long long)(ies->tsf));
	iwe.u.data.length = strlen(buf);
	current_ev = iwe_stream_add_point_check(info, current_ev, end_buf,
						&iwe, buf);
	if (IS_ERR(current_ev))
		goto unlock;
	memset(&iwe, 0, sizeof(iwe));
	iwe.cmd = IWEVCUSTOM;
	sprintf(buf, " Last beacon: %ums ago",
		elapsed_jiffies_msecs(bss->ts));
	iwe.u.data.length = strlen(buf);
	current_ev = iwe_stream_add_point_check(info, current_ev,
						end_buf, &iwe, buf);
	if (IS_ERR(current_ev))
		goto unlock;

	current_ev = ieee80211_scan_add_ies(info, ies, current_ev, end_buf);

 unlock:
	rcu_read_unlock();
	return current_ev;
}


static int ieee80211_scan_results(struct cfg80211_registered_device *rdev,
				  struct iw_request_info *info,
				  char *buf, size_t len)
{
	char *current_ev = buf;
	char *end_buf = buf + len;
	struct cfg80211_internal_bss *bss;
	int err = 0;

	spin_lock_bh(&rdev->bss_lock);
	cfg80211_bss_expire(rdev);

	list_for_each_entry(bss, &rdev->bss_list, list) {
		if (buf + len - current_ev <= IW_EV_ADDR_LEN) {
			err = -E2BIG;
			break;
		}
		current_ev = ieee80211_bss(&rdev->wiphy, info, bss,
					   current_ev, end_buf);
		if (IS_ERR(current_ev)) {
			err = PTR_ERR(current_ev);
			break;
		}
	}
	spin_unlock_bh(&rdev->bss_lock);

	if (err)
		return err;
	return current_ev - buf;
}


int cfg80211_wext_giwscan(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_point *data, char *extra)
{
	struct cfg80211_registered_device *rdev;
	int res;

	if (!netif_running(dev))
		return -ENETDOWN;

	rdev = cfg80211_get_dev_from_ifindex(dev_net(dev), dev->ifindex);

	if (IS_ERR(rdev))
		return PTR_ERR(rdev);

	if (rdev->scan_req || rdev->scan_msg)
		return -EAGAIN;

	res = ieee80211_scan_results(rdev, info, extra, data->length);
	data->length = 0;
	if (res >= 0) {
		data->length = res;
		res = 0;
	}

	return res;
}
EXPORT_WEXT_HANDLER(cfg80211_wext_giwscan);
#endif
