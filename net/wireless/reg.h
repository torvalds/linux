#ifndef __NET_WIRELESS_REG_H
#define __NET_WIRELESS_REG_H
/*
 * Copyright 2008-2011	Luis R. Rodriguez <mcgrof@qca.qualcomm.com>
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

extern const struct ieee80211_regdomain *cfg80211_regdomain;

bool is_world_regdom(const char *alpha2);
bool reg_is_valid_request(const char *alpha2);
bool reg_supported_dfs_region(u8 dfs_region);

int regulatory_hint_user(const char *alpha2);

int reg_device_uevent(struct device *dev, struct kobj_uevent_env *env);
void reg_device_remove(struct wiphy *wiphy);

int __init regulatory_init(void);
void regulatory_exit(void);

int set_regdom(const struct ieee80211_regdomain *rd);

void regulatory_update(struct wiphy *wiphy, enum nl80211_reg_initiator setby);

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
int regulatory_hint_found_beacon(struct wiphy *wiphy,
					struct ieee80211_channel *beacon_chan,
					gfp_t gfp);

/**
 * regulatory_hint_11d - hints a country IE as a regulatory domain
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
void regulatory_hint_11d(struct wiphy *wiphy,
			 enum ieee80211_band band,
			 u8 *country_ie,
			 u8 country_ie_len);

/**
 * regulatory_hint_disconnect - informs all devices have been disconneted
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

#endif  /* __NET_WIRELESS_REG_H */
