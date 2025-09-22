//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <cstddef>  // size_t
#include <cwchar>   // mbstate_t
#include <limits.h> // MB_LEN_MAX
#include <string.h> // wmemcpy

// Returns the number of wide characters found in the multi byte sequence `src`
// (of `src_size_bytes`), that fit in the buffer `dst` (of `max_dest_chars`
// elements size). The count returned excludes the null terminator.
// When `dst` is NULL, no characters are copied to `dst`.
// Returns (size_t) -1 when an invalid sequence is encountered.
// Leaves *`src` pointing to the next character to convert or NULL
// if a null character was converted from *`src`.
_LIBCPP_EXPORTED_FROM_ABI size_t mbsnrtowcs(
    wchar_t* __restrict dst,
    const char** __restrict src,
    size_t src_size_bytes,
    size_t max_dest_chars,
    mbstate_t* __restrict ps) {
  const size_t terminated_sequence = static_cast<size_t>(0);
  const size_t invalid_sequence    = static_cast<size_t>(-1);
  const size_t incomplete_sequence = static_cast<size_t>(-2);

  size_t source_converted;
  size_t dest_converted;
  size_t result = 0;

  // If `dst` is null then `max_dest_chars` should be ignored according to the
  // standard. Setting `max_dest_chars` to a large value has this effect.
  if (dst == nullptr)
    max_dest_chars = static_cast<size_t>(-1);

  for (dest_converted = source_converted = 0;
       source_converted < src_size_bytes && (!dst || dest_converted < max_dest_chars);
       ++dest_converted, source_converted += result) {
    // Converts one multi byte character.
    // If result (char_size) is greater than 0, it's the size in bytes of that character.
    // If result (char_size) is zero, it indicates that the null character has been found.
    // Otherwise, it's an error and errno may be set.
    size_t source_remaining = src_size_bytes - source_converted;
    size_t dest_remaining   = max_dest_chars - dest_converted;

    if (dst == nullptr) {
      result = mbrtowc(NULL, *src + source_converted, source_remaining, ps);
    } else if (dest_remaining >= source_remaining) {
      // dst has enough space to translate in-place.
      result = mbrtowc(dst + dest_converted, *src + source_converted, source_remaining, ps);
    } else {
      /*
       * dst may not have enough space, so use a temporary buffer.
       *
       * We need to save a copy of the conversion state
       * here so we can restore it if the multibyte
       * character is too long for the buffer.
       */
      wchar_t buff[MB_LEN_MAX];
      mbstate_t mbstate_tmp;

      if (ps != nullptr)
        mbstate_tmp = *ps;
      result = mbrtowc(buff, *src + source_converted, source_remaining, ps);

      if (result > dest_remaining) {
        // Multi-byte sequence for character won't fit.
        if (ps != nullptr)
          *ps = mbstate_tmp;
        break;
      } else {
        // The buffer was used, so we need copy the translation to dst.
        wmemcpy(dst, buff, result);
      }
    }

    // Don't do anything to change errno from here on.
    if (result == invalid_sequence || result == terminated_sequence || result == incomplete_sequence) {
      break;
    }
  }

  if (dst) {
    if (result == terminated_sequence)
      *src = NULL;
    else
      *src += source_converted;
  }
  if (result == invalid_sequence)
    return invalid_sequence;

  return dest_converted;
}
