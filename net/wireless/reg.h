#ifndef __NET_WIRELESS_REG_H
#define __NET_WIRELESS_REG_H

#include <net/cfg80211.h>

/*
 * Copyright 2008-2011	Luis R. Rodriguez <mcgrof@qca.qualcomm.com>
 * Copyright (C) 2019, 2023 Intel Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

enum ieee80211_regd_source {
	REGD_SOURCE_INTERNAL_DB,
	REGD_SOURCE_CRDA,
	REGD_SOURCE_CACHED,
};

extern const struct ieee80211_regdomain __rcu *cfg80211_regdomain;

bool reg_is_valid_request(const char *alpha2);
bool is_world_regdom(const char *alpha2);
bool reg_supported_dfs_region(enum nl80211_dfs_regions dfs_region);
enum nl80211_dfs_regions reg_get_dfs_region(struct wiphy *wiphy);

int regulatory_hint_user(const char *alpha2,
			 enum nl80211_user_reg_hint_type user_reg_hint_type);

/**
 * regulatory_hint_indoor - hint operation in indoor env. or not
 * @is_indoor: if true indicates that user space thinks that the
 * device is operating in an indoor environment.
 * @portid: the netlink port ID on which the hint was given.
 */
void regulatory_hint_indoor(bool is_indoor, u32 portid);

/**
 * regulatory_netlink_notify - notify on released netlink socket
 * @portid: the netlink socket port ID
 */
void regulatory_netlink_notify(u32 portid);

void wiphy_regulatory_register(struct wiphy *wiphy);
void wiphy_regulatory_deregister(struct wiphy *wiphy);

int __init regulatory_init(void);
void regulatory_exit(void);

int set_regdom(const struct ieee80211_regdomain *rd,
	       enum ieee80211_regd_source regd_src);

unsigned int reg_get_max_bandwidth(const struct ieee80211_regdomain *rd,
				   const struct ieee80211_reg_rule *rule);

bool reg_last_request_cell_base(void);

/**
 * regulatory_hint_found_beacon - hints a beacon was found on a channel
 * @wiphy: the wireless device where the beacon was found on
 * @beacon_chan: the channel on which the beacon was found on
 * @gfp: context flags
 *
 * This informs the wireless core that a beacon from an AP was found on
 * the channel provided. This allows the wireless core to make educated
 * guesses on regulatory to help with world roaming. This is only used for
 * world roaming -- when we do not know our current location. This is
 * only useful on channels 12, 13 and 14 on the 2 GHz band as channels
 * 1-11 are already enabled by the world regulatory domain; and on
 * non-radar 5 GHz channels.
 *
 * Drivers do not need to call this, cfg80211 will do it for after a scan
 * on a newly found BSS. If you cannot make use of this feature you can
 * set the wiphy->disable_beacon_hints to true.
 */
void regulatory_hint_found_beacon(struct wiphy *wiphy,
				  struct ieee80211_channel *beacon_chan,
				  gfp_t gfp);

/**
 * regulatory_hint_country_ie - hints a country IE as a regulatory domain
 * @wiphy: the wireless device giving the hint (used only for reporting
 *	conflicts)
 * @band: the band on which the country IE was received on. This determines
 *	the band we'll process the country IE channel triplets for.
 * @country_ie: pointer to the country IE
 * @country_ie_len: length of the country IE
 *
 * We will intersect the rd with the what CRDA tells us should apply
 * for the alpha2 this country IE belongs to, this prevents APs from
 * sending us incorrect or outdated information against a country.
 *
 * The AP is expected to provide Country IE channel triplets for the
 * band it is on. It is technically possible for APs to send channel
 * country IE triplets even for channels outside of the band they are
 * in but for that they would have to use the regulatory extension
 * in combination with a triplet but this behaviour is currently
 * not observed. For this reason if a triplet is seen with channel
 * information for a band the BSS is not present in it will be ignored.
 */
void regulatory_hint_country_ie(struct wiphy *wiphy,
			 enum nl80211_band band,
			 const u8 *country_ie,
			 u8 country_ie_len);

/**
 * regulatory_hint_disconnect - informs all devices have been disconnected
 *
 * Regulotory rules can be enhanced further upon scanning and upon
 * connection to an AP. These rules become stale if we disconnect
 * and go to another country, whether or not we suspend and resume.
 * If we suspend, go to another country and resume we'll automatically
 * get disconnected shortly after resuming and things will be reset as well.
 * This routine is a helper to restore regulatory settings to how they were
 * prior to our first connect attempt. This includes ignoring country IE and
 * beacon regulatory hints. The ieee80211_regdom module parameter will always
 * be respected but if a user had set the regulatory domain that will take
 * precedence.
 *
 * Must be called from process context.
 */
void regulatory_hint_disconnect(void);

/**
 * cfg80211_get_unii - get the U-NII band for the frequency
 * @freq: the frequency for which we want to get the UNII band.
 *
 * Get a value specifying the U-NII band frequency belongs to.
 * U-NII bands are defined by the FCC in C.F.R 47 part 15.
 *
 * Return: -EINVAL if freq is invalid, 0 for UNII-1, 1 for UNII-2A,
 * 2 for UNII-2B, 3 for UNII-2C and 4 for UNII-3.
 */
int cfg80211_get_unii(int freq);

/**
 * regulatory_indoor_allowed - is indoor operation allowed
 * Return: %true if indoor operation is allowed, %false otherwise
 */
bool regulatory_indoor_allowed(void);

/*
 * Grace period to timeout pre-CAC results on the dfs channels. This timeout
 * value is used for Non-ETSI domain.
 * TODO: May be make this timeout available through regdb?
 */
#define REG_PRE_CAC_EXPIRY_GRACE_MS 2000

/**
 * regulatory_propagate_dfs_state - Propagate DFS channel state to other wiphys
 * @wiphy: wiphy on which radar is detected and the event will be propagated
 *	to other available wiphys having the same DFS domain
 * @chandef: Channel definition of radar detected channel
 * @dfs_state: DFS channel state to be set
 * @event: Type of radar event which triggered this DFS state change
 *
 * This function should be called with rtnl lock held.
 */
void regulatory_propagate_dfs_state(struct wiphy *wiphy,
				    struct cfg80211_chan_def *chandef,
				    enum nl80211_dfs_state dfs_state,
				    enum nl80211_radar_event event);

/**
 * reg_dfs_domain_same - Checks if both wiphy have same DFS domain configured
 * @wiphy1: wiphy it's dfs_region to be checked against that of wiphy2
 * @wiphy2: wiphy it's dfs_region to be checked against that of wiphy1
 * Return: %true if both wiphys have the same DFS domain, %false otherwise
 */
bool reg_dfs_domain_same(struct wiphy *wiphy1, struct wiphy *wiphy2);

/**
 * reg_reload_regdb - reload the regulatory.db firmware file
 * Return: 0 for success, an error code otherwise
 */
int reg_reload_regdb(void);

/**
 * reg_check_channels - schedule regulatory enforcement
 */
void reg_check_channels(void);

extern const u8 shipped_regdb_certs[];
extern unsigned int shipped_regdb_certs_len;
extern const u8 extra_regdb_certs[];
extern unsigned int extra_regdb_certs_len;

#endif  /* __NET_WIRELESS_REG_H */
