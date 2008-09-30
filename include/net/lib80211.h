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

#endif /* LIB80211_H */
