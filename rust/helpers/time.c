// SPDX-License-Identifier: GPL-2.0

#include <linux/ktime.h>

s64 rust_helper_ktime_to_us(const ktime_t kt)
{
	return ktime_to_us(kt);
}

s64 rust_helper_ktime_to_ms(const ktime_t kt)
{
	return ktime_to_ms(kt);
}
