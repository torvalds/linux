#ifndef __NET_WIRELESS_REG_H
#define __NET_WIRELESS_REG_H

extern const struct ieee80211_regdomain *cfg80211_regdomain;

bool is_world_regdom(const char *alpha2);
bool reg_is_valid_request(const char *alpha2);

int regulatory_hint_user(const char *alpha2);

void reg_device_remove(struct wiphy *wiphy);

int regulatory_init(void);
void regulatory_exit(void);

int set_regdom(const struct ieee80211_regdomain *rd);

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
 * @country_ie: pointer to the country IE
 * @country_ie_len: length of the country IE
 *
 * We will intersect the rd with the what CRDA tells us should apply
 * for the alpha2 this country IE belongs to, this prevents APs from
 * sending us incorrect or outdated information against a country.
 */
void regulatory_hint_11d(struct wiphy *wiphy,
			 u8 *country_ie,
			 u8 country_ie_len);

#endif  /* __NET_WIRELESS_REG_H */
