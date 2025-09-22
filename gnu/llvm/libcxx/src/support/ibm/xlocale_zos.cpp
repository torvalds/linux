//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <__assert>
#include <__support/ibm/xlocale.h>
#include <sstream>
#include <vector>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

locale_t newlocale(int category_mask, const char* locale, locale_t base) {
  // Maintain current locale name(s) to restore later.
  std::string current_loc_name(setlocale(LC_ALL, 0));

  // Check for errors.
  if (category_mask == LC_ALL_MASK && setlocale(LC_ALL, locale) == NULL) {
    errno = EINVAL;
    return (locale_t)0;
  } else {
    for (int _Cat = 0; _Cat <= _LC_MAX; ++_Cat) {
      if ((_CATMASK(_Cat) & category_mask) != 0 && setlocale(_Cat, locale) == NULL) {
        setlocale(LC_ALL, current_loc_name.c_str());
        errno = EINVAL;
        return (locale_t)0;
      }
    }
  }

  // Create new locale.
  locale_t newloc = new locale_struct();

  if (base) {
    if (category_mask != LC_ALL_MASK) {
      // Copy base when it will not be overwritten.
      memcpy(newloc, base, sizeof(locale_struct));
      newloc->category_mask = category_mask | base->category_mask;
    }
    delete base;
  } else {
    newloc->category_mask = category_mask;
  }

  if (category_mask & LC_COLLATE_MASK)
    newloc->lc_collate = locale;
  if (category_mask & LC_CTYPE_MASK)
    newloc->lc_ctype = locale;
  if (category_mask & LC_MONETARY_MASK)
    newloc->lc_monetary = locale;
  if (category_mask & LC_NUMERIC_MASK)
    newloc->lc_numeric = locale;
  if (category_mask & LC_TIME_MASK)
    newloc->lc_time = locale;
  if (category_mask & LC_MESSAGES_MASK)
    newloc->lc_messages = locale;

  // Restore current locale.
  setlocale(LC_ALL, current_loc_name.c_str());
  return (locale_t)newloc;
}

void freelocale(locale_t locobj) { delete locobj; }

locale_t uselocale(locale_t newloc) {
  // Maintain current locale name(s).
  std::string current_loc_name(setlocale(LC_ALL, 0));

  if (newloc) {
    // Set locales and check for errors.
    bool is_error =
        (newloc->category_mask & LC_COLLATE_MASK && setlocale(LC_COLLATE, newloc->lc_collate.c_str()) == NULL) ||
        (newloc->category_mask & LC_CTYPE_MASK && setlocale(LC_CTYPE, newloc->lc_ctype.c_str()) == NULL) ||
        (newloc->category_mask & LC_MONETARY_MASK && setlocale(LC_MONETARY, newloc->lc_monetary.c_str()) == NULL) ||
        (newloc->category_mask & LC_NUMERIC_MASK && setlocale(LC_NUMERIC, newloc->lc_numeric.c_str()) == NULL) ||
        (newloc->category_mask & LC_TIME_MASK && setlocale(LC_TIME, newloc->lc_time.c_str()) == NULL) ||
        (newloc->category_mask & LC_MESSAGES_MASK && setlocale(LC_MESSAGES, newloc->lc_messages.c_str()) == NULL);

    if (is_error) {
      setlocale(LC_ALL, current_loc_name.c_str());
      errno = EINVAL;
      return (locale_t)0;
    }
  }

  // Construct and return previous locale.
  locale_t previous_loc = new locale_struct();

  // current_loc_name might be a comma-separated locale name list.
  if (current_loc_name.find(',') != std::string::npos) {
    // Tokenize locale name list.
    const char delimiter = ',';
    std::vector<std::string> tokenized;
    std::stringstream ss(current_loc_name);
    std::string s;

    while (std::getline(ss, s, delimiter)) {
      tokenized.push_back(s);
    }

    _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(tokenized.size() >= _NCAT, "locale-name list is too short");

    previous_loc->lc_collate  = tokenized[LC_COLLATE];
    previous_loc->lc_ctype    = tokenized[LC_CTYPE];
    previous_loc->lc_monetary = tokenized[LC_MONETARY];
    previous_loc->lc_numeric  = tokenized[LC_NUMERIC];
    previous_loc->lc_time     = tokenized[LC_TIME];
    // Skip LC_TOD.
    previous_loc->lc_messages = tokenized[LC_MESSAGES];
  } else {
    previous_loc->lc_collate  = current_loc_name;
    previous_loc->lc_ctype    = current_loc_name;
    previous_loc->lc_monetary = current_loc_name;
    previous_loc->lc_numeric  = current_loc_name;
    previous_loc->lc_time     = current_loc_name;
    previous_loc->lc_messages = current_loc_name;
  }

  previous_loc->category_mask = LC_ALL_MASK;
  return previous_loc;
}

#ifdef __cplusplus
}
#endif // __cplusplus
