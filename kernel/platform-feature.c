// SPDX-License-Identifier: GPL-2.0

#include <linux/bitops.h>
#include <linux/cache.h>
#include <linux/export.h>
#include <linux/platform-feature.h>

#define PLATFORM_FEAT_ARRAY_SZ  BITS_TO_LONGS(PLATFORM_FEAT_N)
static unsigned long __read_mostly platform_features[PLATFORM_FEAT_ARRAY_SZ];

void platform_set(unsigned int feature)
{
	set_bit(feature, platform_features);
}
EXPORT_SYMBOL_GPL(platform_set);

void platform_clear(unsigned int feature)
{
	clear_bit(feature, platform_features);
}
EXPORT_SYMBOL_GPL(platform_clear);

bool platform_has(unsigned int feature)
{
	return test_bit(feature, platform_features);
}
EXPORT_SYMBOL_GPL(platform_has);
