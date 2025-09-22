/*
 * base32.h -- Fast Base32 decoder
 *
 * Copyright (c) 2023, Daniel Lemire and @aqrit.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */
#ifndef BASE32_H
#define BASE32_H

#include <stdint.h>
#include <immintrin.h>


//////////////////////////
/// Source: Wojciech Muła, Daniel Lemire, Faster Base64 Encoding and Decoding Using AVX2 Instructions,
///         ACM Transactions on the Web 12 (3), 2018
///         https://arxiv.org/abs/1704.00605
//////////////////////////
static size_t base32hex_sse(uint8_t *dst, const uint8_t *src) {
  static int8_t zero_masks[32] = {0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
                                  0,  0,  0,  0,  0,  -1, -1, -1, -1, -1, -1,
                                 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
  bool valid = true;
  const __m128i delta_check =
      _mm_setr_epi8(-16, -32, -48, 70, -65, 41, -97, 9, 0, 0, 0, 0, 0, 0, 0, 0);
  const __m128i delta_rebase =
      _mm_setr_epi8(0, 0, 0, -48, -55, -55, -87, -87, 0, 0, 0, 0, 0, 0, 0, 0);
  const uint8_t *srcinit = src;
  do {
    __m128i v = _mm_loadu_si128((__m128i *)src);

    __m128i hash_key = _mm_and_si128(_mm_srli_epi32(v, 4), _mm_set1_epi8(0x0F));
    __m128i check = _mm_add_epi8(_mm_shuffle_epi8(delta_check, hash_key), v);
    v = _mm_add_epi8(v, _mm_shuffle_epi8(delta_rebase, hash_key));
    unsigned int m = (unsigned)_mm_movemask_epi8(check);

    if (m) {
      int length = (int)trailing_zeroes(m);
      if (length == 0) {
        break;
      }
      src += length;
      __m128i zero_mask =
          _mm_loadu_si128((__m128i *)(zero_masks + 16 - length));
      v = _mm_andnot_si128(zero_mask, v);
      valid = false;
    } else { // common case
      src += 16;
    }
    v = _mm_maddubs_epi16(v, _mm_set1_epi32(0x01200120));
    v = _mm_madd_epi16(
        v, _mm_set_epi32(0x00010400, 0x00104000, 0x00010400, 0x00104000));
    // ...00000000`0000eeee`efffffgg`ggghhhhh`00000000`aaaaabbb`bbcccccd`dddd0000
    v = _mm_or_si128(v, _mm_srli_epi64(v, 48));
    v = _mm_shuffle_epi8(
        v, _mm_set_epi8(0, 0, 0, 0, 0, 0, 12, 13, 8, 9, 10, 4, 5, 0, 1, 2));

    /* decoded 10 bytes... but write 16 cause why not? */
    _mm_storeu_si128((__m128i *)dst, v);
    dst += 10;
  } while (valid);

  return (size_t)(src - srcinit);
}

nonnull_all
static really_inline int32_t parse_base32(
  parser_t *parser,
  const type_info_t *type,
  const rdata_info_t *field,
  rdata_t *rdata,
  const token_t *token)
{
  size_t length = (token->length * 5) / 8;
  if (length > 255 || (uintptr_t)rdata->limit - (uintptr_t)rdata->octets < (length + 1))
    SYNTAX_ERROR(parser, "Invalid %s in %s", NAME(field), NAME(type));

  size_t decoded = base32hex_sse(rdata->octets+1, (const uint8_t*)token->data);
  if (decoded != token->length)
    SYNTAX_ERROR(parser, "Invalid %s in %s", NAME(field), NAME(type));
  *rdata->octets = (uint8_t)length;
  rdata->octets += 1 + length;
  return 0;
}

#endif // BASE32_H
