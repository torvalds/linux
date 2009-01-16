#ifndef __NET_WIRELESS_REG_H
#define __NET_WIRELESS_REG_H

bool is_world_regdom(const char *alpha2);
bool reg_is_valid_request(const char *alpha2);

void reg_device_remove(struct wiphy *wiphy);

int regulatory_init(void);
void regulatory_exit(void);

int set_regdom(const struct ieee80211_regdomain *rd);

enum environment_cap {
	ENVIRON_ANY,
	ENVIRON_INDOOR,
	ENVIRON_OUTDOOR,
};


/**
 * __regulatory_hint - hint to the wireless core a regulatory domain
 * @wiphy: if the hint comes from country information from an AP, this
 *	is required to be set to the wiphy that received the information
 * @alpha2: the ISO/IEC 3166 alpha2 being claimed the regulatory domain
 *	should be in.
 * @country_ie_checksum: checksum of processed country IE, set this to 0
 * 	if the hint did not come from a country IE
 * @country_ie_env: the environment the IE told us we are in, %ENVIRON_*
 *
 * The Wireless subsystem can use this function to hint to the wireless core
 * what it believes should be the current regulatory domain by giving it an
 * ISO/IEC 3166 alpha2 country code it knows its regulatory domain should be
 * in.
 *
 * Returns zero if all went fine, %-EALREADY if a regulatory domain had
 * already been set or other standard error codes.
 *
 */
extern int __regulatory_hint(struct wiphy *wiphy, enum reg_set_by set_by,
			     const char *alpha2, u32 country_ie_checksum,
			     enum environment_cap country_ie_env);

#endif  /* __NET_WIRELESS_REG_H */
