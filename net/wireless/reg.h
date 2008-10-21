#ifndef __NET_WIRELESS_REG_H
#define __NET_WIRELESS_REG_H

bool is_world_regdom(const char *alpha2);
bool reg_is_valid_request(const char *alpha2);

int regulatory_init(void);
void regulatory_exit(void);

int set_regdom(const struct ieee80211_regdomain *rd);

/**
 * __regulatory_hint - hint to the wireless core a regulatory domain
 * @wiphy: if a driver is providing the hint this is the driver's very
 * 	own &struct wiphy
 * @alpha2: the ISO/IEC 3166 alpha2 being claimed the regulatory domain
 * 	should be in. If @rd is set this should be NULL
 * @rd: a complete regulatory domain, if passed the caller need not worry
 * 	about freeing it
 *
 * The Wireless subsystem can use this function to hint to the wireless core
 * what it believes should be the current regulatory domain by
 * giving it an ISO/IEC 3166 alpha2 country code it knows its regulatory
 * domain should be in or by providing a completely build regulatory domain.
 *
 * Returns -EALREADY if *a regulatory domain* has already been set. Note that
 * this could be by another driver. It is safe for drivers to continue if
 * -EALREADY is returned, if drivers are not capable of world roaming they
 * should not register more channels than they support. Right now we only
 * support listening to the first driver hint. If the driver is capable
 * of world roaming but wants to respect its own EEPROM mappings for
 * specific regulatory domains it should register the @reg_notifier callback
 * on the &struct wiphy. Returns 0 if the hint went through fine or through an
 * intersection operation. Otherwise a standard error code is returned.
 *
 */
extern int __regulatory_hint(struct wiphy *wiphy, enum reg_set_by set_by,
		const char *alpha2, struct ieee80211_regdomain *rd);

#endif  /* __NET_WIRELESS_REG_H */
