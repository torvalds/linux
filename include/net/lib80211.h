/*
 * lib80211.h -- common bits for IEEE802.11 wireless drivers
 *
 * Copyright (c) 2008, John W. Linville <linville@tuxdriver.com>
 *
 */

#ifndef LIB80211_H
#define LIB80211_H

/* escape_ssid() is intended to be used in debug (and possibly error)
 * messages. It should never be used for passing ssid to user space. */
const char *escape_ssid(const char *ssid, u8 ssid_len);

static inline int is_empty_ssid(const char *ssid, int ssid_len)
{
	/* Single white space is for Linksys APs */
	if (ssid_len == 1 && ssid[0] == ' ')
		return 1;

	/* Otherwise, if the entire ssid is 0, we assume it is hidden */
	while (ssid_len) {
		ssid_len--;
		if (ssid[ssid_len] != '\0')
			return 0;
	}

	return 1;
}

#endif /* LIB80211_H */
