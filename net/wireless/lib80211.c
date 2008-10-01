/*
 * lib80211 -- common bits for IEEE802.11 drivers
 *
 * Copyright(c) 2008 John W. Linville <linville@tuxdriver.com>
 *
 */

#include <linux/module.h>
#include <linux/ctype.h>
#include <linux/ieee80211.h>

#include <net/lib80211.h>

#define DRV_NAME        "lib80211"

#define DRV_DESCRIPTION	"common routines for IEEE802.11 drivers"

MODULE_DESCRIPTION(DRV_DESCRIPTION);
MODULE_AUTHOR("John W. Linville <linville@tuxdriver.com>");
MODULE_LICENSE("GPL");

const char *print_ssid(char *buf, const char *ssid, u8 ssid_len)
{
	const char *s = ssid;
	char *d = buf;

	ssid_len = min_t(u8, ssid_len, IEEE80211_MAX_SSID_LEN);
	while (ssid_len--) {
		if (isprint(*s)) {
			*d++ = *s++;
			continue;
		}

		*d++ = '\\';
		if (*s == '\0')
			*d++ = '0';
		else if (*s == '\n')
			*d++ = 'n';
		else if (*s == '\r')
			*d++ = 'r';
		else if (*s == '\t')
			*d++ = 't';
		else if (*s == '\\')
			*d++ = '\\';
		else
			d += snprintf(d, 3, "%03o", *s);
		s++;
	}
	*d = '\0';
	return buf;
}
EXPORT_SYMBOL(print_ssid);

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
