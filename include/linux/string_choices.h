/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_STRING_CHOICES_H_
#define _LINUX_STRING_CHOICES_H_

#include <linux/types.h>

static inline const char *str_enable_disable(bool v)
{
	return v ? "enable" : "disable";
}

static inline const char *str_enabled_disabled(bool v)
{
	return v ? "enabled" : "disabled";
}

static inline const char *str_hi_lo(bool v)
{
	return v ? "hi" : "lo";
}
#define str_lo_hi(v)		str_hi_lo(!(v))

static inline const char *str_high_low(bool v)
{
	return v ? "high" : "low";
}
#define str_low_high(v)		str_high_low(!(v))

static inline const char *str_read_write(bool v)
{
	return v ? "read" : "write";
}
#define str_write_read(v)		str_read_write(!(v))

static inline const char *str_on_off(bool v)
{
	return v ? "on" : "off";
}

static inline const char *str_yes_no(bool v)
{
	return v ? "yes" : "no";
}

#endif
