/*
 * lib80211 -- common bits for IEEE802.11 drivers
 *
 * Copyright(c) 2008 John W. Linville <linville@tuxdriver.com>
 *
 */

#include <linux/module.h>
#include <linux/ieee80211.h>

#include <net/lib80211.h>

#define DRV_NAME        "lib80211"

#define DRV_DESCRIPTION	"common routines for IEEE802.11 drivers"

MODULE_DESCRIPTION(DRV_DESCRIPTION);
MODULE_AUTHOR("John W. Linville <linville@tuxdriver.com>");
MODULE_LICENSE("GPL");

const char *escape_ssid(const char *ssid, u8 ssid_len)
{
	static char escaped[IEEE80211_MAX_SSID_LEN * 2 + 1];
	const char *s = ssid;
	char *d = escaped;

	if (is_empty_ssid(ssid, ssid_len)) {
		memcpy(escaped, "<hidden>", sizeof("<hidden>"));
		return escaped;
	}

	ssid_len = min_t(u8, ssid_len, IEEE80211_MAX_SSID_LEN);
	while (ssid_len--) {
		if (*s == '\0') {
			*d++ = '\\';
			*d++ = '0';
			s++;
		} else {
			*d++ = *s++;
		}
	}
	*d = '\0';
	return escaped;
}
EXPORT_SYMBOL(escape_ssid);

static int __init ieee80211_init(void)
{
	printk(KERN_INFO DRV_NAME ": " DRV_DESCRIPTION "\n");
	return 0;
}

static void __exit ieee80211_exit(void)
{
}

module_init(ieee80211_init);
module_exit(ieee80211_exit);
