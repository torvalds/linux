// SPDX-License-Identifier: GPL-2.0
#include <linux/string.h>
#include <linux/if_ether.h>
#include <linux/ctype.h>
#include <linux/export.h>
#include <linux/hex.h>

bool mac_pton(const char *s, u8 *mac)
{
	size_t maxlen = 3 * ETH_ALEN - 1;
	int i;

	/* XX:XX:XX:XX:XX:XX */
	if (strnlen(s, maxlen) < maxlen)
		return false;

	/* Don't dirty result unless string is valid MAC. */
	for (i = 0; i < ETH_ALEN; i++) {
		if (!isxdigit(s[i * 3]) || !isxdigit(s[i * 3 + 1]))
			return false;
		if (i != ETH_ALEN - 1 && s[i * 3 + 2] != ':')
			return false;
	}
	for (i = 0; i < ETH_ALEN; i++) {
		mac[i] = (hex_to_bin(s[i * 3]) << 4) | hex_to_bin(s[i * 3 + 1]);
	}
	return true;
}
EXPORT_SYMBOL(mac_pton);
