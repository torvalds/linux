/*
 * attributes.h -- compiler attribute abstractions
 *
 * Copyright (c) 2022, NLnet Labs. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */
#ifndef ZONE_ATTRIBUTES_H
#define ZONE_ATTRIBUTES_H

#if defined __GNUC__
# define zone_has_gnuc(major, minor) \
    ((__GNUC__ > major) || (__GNUC__ == major && __GNUC_MINOR__ >= minor))
#else
# define zone_has_gnuc(major, minor) (0)
#endif

#if defined __has_attribute
# define zone_has_attribute(params) __has_attribute(params)
#else
# define zone_has_attribute(params) (0)
#endif

#if zone_has_attribute(nonnull)
# define zone_nonnull(params) __attribute__((__nonnull__ params))
# define zone_nonnull_all __attribute__((__nonnull__))
#else
# define zone_nonnull(params)
# define zone_nonnull_all
#endif

#if zone_has_attribute(format) || zone_has_gnuc(2, 4)
# define zone_format(params) __attribute__((__format__ params))
# if __MINGW32__
#   if __MINGW_PRINTF_FORMAT
#     define zone_format_printf(string_index, first_to_check) \
        zone_format((__MINGW_PRINTF_FORMAT, string_index, first_to_check))
#   else
#     define zone_format_printf(string_index, first_to_check) \
        zone_format((gnu_printf, string_index, first_to_check))
#   endif
# else
#   define zone_format_printf(string_index, first_to_check) \
      zone_format((printf, string_index, first_to_check))
# endif
#else
# define zone_format(params)
# define zone_format_printf(string_index, first_to_check)
#endif

#endif // ZONE_ATTRIBUTES_H
