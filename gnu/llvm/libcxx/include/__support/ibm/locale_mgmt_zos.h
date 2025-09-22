// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___SUPPORT_IBM_LOCALE_MGMT_ZOS_H
#define _LIBCPP___SUPPORT_IBM_LOCALE_MGMT_ZOS_H

#if defined(__MVS__)
#  include <locale.h>
#  include <string>

#  ifdef __cplusplus
extern "C" {
#  endif

#  define _LC_MAX LC_MESSAGES /* highest real category */
#  define _NCAT (_LC_MAX + 1) /* maximum + 1 */

#  define _CATMASK(n) (1 << (n))
#  define LC_COLLATE_MASK _CATMASK(LC_COLLATE)
#  define LC_CTYPE_MASK _CATMASK(LC_CTYPE)
#  define LC_MONETARY_MASK _CATMASK(LC_MONETARY)
#  define LC_NUMERIC_MASK _CATMASK(LC_NUMERIC)
#  define LC_TIME_MASK _CATMASK(LC_TIME)
#  define LC_MESSAGES_MASK _CATMASK(LC_MESSAGES)
#  define LC_ALL_MASK (_CATMASK(_NCAT) - 1)

typedef struct locale_struct {
  int category_mask;
  std::string lc_collate;
  std::string lc_ctype;
  std::string lc_monetary;
  std::string lc_numeric;
  std::string lc_time;
  std::string lc_messages;
}* locale_t;

// z/OS does not have newlocale, freelocale and uselocale.
// The functions below are workarounds in single thread mode.
locale_t newlocale(int category_mask, const char* locale, locale_t base);
void freelocale(locale_t locobj);
locale_t uselocale(locale_t newloc);

#  ifdef __cplusplus
}
#  endif
#endif // defined(__MVS__)
#endif // _LIBCPP___SUPPORT_IBM_LOCALE_MGMT_ZOS_H
