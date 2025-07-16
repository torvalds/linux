// SPDX-License-Identifier: GPL-2.0

#include <linux/delay.h>
#include <linux/ktime.h>
#include <linux/timekeeping.h>

void rust_helper_fsleep(unsigned long usecs)
{
	fsleep(usecs);
}

ktime_t rust_helper_ktime_get_real(void)
{
	return ktime_get_real();
}

ktime_t rust_helper_ktime_get_boottime(void)
{
	return ktime_get_boottime();
}

ktime_t rust_helper_ktime_get_clocktai(void)
{
	return ktime_get_clocktai();
}

s64 rust_helper_ktime_to_us(const ktime_t kt)
{
	return ktime_to_us(kt);
}

s64 rust_helper_ktime_to_ms(const ktime_t kt)
{
	return ktime_to_ms(kt);
}
