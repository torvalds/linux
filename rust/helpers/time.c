// SPDX-License-Identifier: GPL-2.0

#include <linux/delay.h>
#include <linux/ktime.h>
#include <linux/timekeeping.h>

__rust_helper void rust_helper_fsleep(unsigned long usecs)
{
	fsleep(usecs);
}

__rust_helper ktime_t rust_helper_ktime_get_real(void)
{
	return ktime_get_real();
}

__rust_helper ktime_t rust_helper_ktime_get_boottime(void)
{
	return ktime_get_boottime();
}

__rust_helper ktime_t rust_helper_ktime_get_clocktai(void)
{
	return ktime_get_clocktai();
}

__rust_helper s64 rust_helper_ktime_to_us(const ktime_t kt)
{
	return ktime_to_us(kt);
}

__rust_helper s64 rust_helper_ktime_to_ms(const ktime_t kt)
{
	return ktime_to_ms(kt);
}

__rust_helper void rust_helper_udelay(unsigned long usec)
{
	udelay(usec);
}
