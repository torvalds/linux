/* Public domain. */

#ifndef _LINUX_STRING_HELPERS_H
#define _LINUX_STRING_HELPERS_H

#include <linux/types.h>

static inline const char *
str_yes_no(bool x)
{
	if (x)
		return "yes";
	return "no";
}

static inline const char *
str_on_off(bool x)
{
	if (x)
		return "on";
	return "off";
}

static inline const char *
str_enabled_disabled(bool x)
{
	if (x)
		return "enabled";
	return "disabled";
}

static inline const char *
str_enable_disable(bool x)
{
	if (x)
		return "enable";
	return "disable";
}

#endif
