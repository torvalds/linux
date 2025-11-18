/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_STRING_CHOICES_H_
#define _LINUX_STRING_CHOICES_H_

/*
 * Here provide a series of helpers in the str_$TRUE_$FALSE format (you can
 * also expand some helpers as needed), where $TRUE and $FALSE are their
 * corresponding literal strings. These helpers can be used in the printing
 * and also in other places where constant strings are required. Using these
 * helpers offers the following benefits:
 *  1) Reducing the hardcoding of strings, which makes the code more elegant
 *     through these simple literal-meaning helpers.
 *  2) Unifying the output, which prevents the same string from being printed
 *     in various forms, such as enable/disable, enabled/disabled, en/dis.
 *  3) Deduping by the linker, which results in a smaller binary file.
 */

#include <linux/types.h>

static inline const char *str_assert_deassert(bool v)
{
	return v ? "assert" : "deassert";
}
#define str_deassert_assert(v)		str_assert_deassert(!(v))

static inline const char *str_enable_disable(bool v)
{
	return v ? "enable" : "disable";
}
#define str_disable_enable(v)		str_enable_disable(!(v))

static inline const char *str_enabled_disabled(bool v)
{
	return v ? "enabled" : "disabled";
}
#define str_disabled_enabled(v)		str_enabled_disabled(!(v))

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

static inline const char *str_input_output(bool v)
{
	return v ? "input" : "output";
}
#define str_output_input(v)	str_input_output(!(v))

static inline const char *str_on_off(bool v)
{
	return v ? "on" : "off";
}
#define str_off_on(v)		str_on_off(!(v))

static inline const char *str_read_write(bool v)
{
	return v ? "read" : "write";
}
#define str_write_read(v)		str_read_write(!(v))

static inline const char *str_true_false(bool v)
{
	return v ? "true" : "false";
}
#define str_false_true(v)		str_true_false(!(v))

static inline const char *str_up_down(bool v)
{
	return v ? "up" : "down";
}
#define str_down_up(v)		str_up_down(!(v))

static inline const char *str_yes_no(bool v)
{
	return v ? "yes" : "no";
}
#define str_no_yes(v)		str_yes_no(!(v))

/**
 * str_plural - Return the simple pluralization based on English counts
 * @num: Number used for deciding pluralization
 *
 * If @num is 1, returns empty string, otherwise returns "s".
 */
static inline const char *str_plural(size_t num)
{
	return num == 1 ? "" : "s";
}

#endif
