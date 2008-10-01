/*
 * lib80211.h -- common bits for IEEE802.11 wireless drivers
 *
 * Copyright (c) 2008, John W. Linville <linville@tuxdriver.com>
 *
 */

#ifndef LIB80211_H
#define LIB80211_H

#include <linux/ieee80211.h>

/* print_ssid() is intended to be used in debug (and possibly error)
 * messages. It should never be used for passing ssid to user space. */
const char *print_ssid(char *buf, const char *ssid, u8 ssid_len);
#define DECLARE_SSID_BUF(var) char var[IEEE80211_MAX_SSID_LEN * 4 + 1] __maybe_unused

#endif /* LIB80211_H */
