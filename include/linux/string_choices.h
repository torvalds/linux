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

static inline const char *str_read_write(bool v)
{
	return v ? "read" : "write";
}

static inline const char *str_on_off(bool v)
{
	return v ? "on" : "off";
}

static inline const char *str_yes_no(bool v)
{
	return v ? "yes" : "no";
}

#endif
