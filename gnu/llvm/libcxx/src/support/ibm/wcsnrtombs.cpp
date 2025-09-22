//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <cwchar>   // mbstate_t
#include <limits.h> // MB_LEN_MAX
#include <stdlib.h> // MB_CUR_MAX, size_t
#include <string.h> // memcpy

// Converts `max_source_chars` from the wide character buffer pointer to by *`src`,
// into the multi byte character sequence buffer stored at `dst`, which must be
// `dst_size_bytes` bytes in size. Returns the number of bytes in the sequence
// converted from *src, excluding the null terminator.
// Returns (size_t) -1 if an error occurs and sets errno.
// If `dst` is NULL, `dst_size_bytes` is ignored and no bytes are copied to `dst`.
_LIBCPP_EXPORTED_FROM_ABI size_t wcsnrtombs(
    char* __restrict dst,
    const wchar_t** __restrict src,
    size_t max_source_chars,
    size_t dst_size_bytes,
    mbstate_t* __restrict ps) {
  const size_t invalid_wchar = static_cast<size_t>(-1);

  size_t source_converted;
  size_t dest_converted;
  size_t result = 0;

  // If `dst` is null then `dst_size_bytes` should be ignored according to the
  // standard. Setting dst_size_bytes to a large value has this effect.
  if (dst == nullptr)
    dst_size_bytes = static_cast<size_t>(-1);

  for (dest_converted = source_converted = 0;
       source_converted < max_source_chars && (!dst || dest_converted < dst_size_bytes);
       ++source_converted, dest_converted += result) {
    wchar_t c             = (*src)[source_converted];
    size_t dest_remaining = dst_size_bytes - dest_converted;

    if (dst == nullptr) {
      result = wcrtomb(NULL, c, ps);
    } else if (dest_remaining >= static_cast<size_t>(MB_CUR_MAX)) {
      // dst has enough space to translate in-place.
      result = wcrtomb(dst + dest_converted, c, ps);
    } else {
      /*
       * dst may not have enough space, so use a temporary buffer.
       *
       * We need to save a copy of the conversion state
       * here so we can restore it if the multibyte
       * character is too long for the buffer.
       */
      char buff[MB_LEN_MAX];
      mbstate_t mbstate_tmp;

      if (ps != nullptr)
        mbstate_tmp = *ps;
      result = wcrtomb(buff, c, ps);

      if (result > dest_remaining) {
        // Multi-byte sequence for character won't fit.
        if (ps != nullptr)
          *ps = mbstate_tmp;
        if (result != invalid_wchar)
          break;
      } else {
        // The buffer was used, so we need copy the translation to dst.
        memcpy(dst, buff, result);
      }
    }

    // result (char_size) contains the size of the multi-byte-sequence converted.
    // Otherwise, result (char_size) is (size_t) -1 and wcrtomb() sets the errno.
    if (result == invalid_wchar) {
      if (dst)
        *src = *src + source_converted;
      return invalid_wchar;
    }

    if (c == L'\0') {
      if (dst)
        *src = NULL;
      return dest_converted;
    }
  }

  if (dst)
    *src = *src + source_converted;

  return dest_converted;
}
