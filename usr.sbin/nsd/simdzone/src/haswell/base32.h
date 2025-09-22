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

//////////////////////////
/// Source: Wojciech Muła, Daniel Lemire, Faster Base64 Encoding and Decoding Using AVX2 Instructions,
///         ACM Transactions on the Web 12 (3), 2018
///         https://arxiv.org/abs/1704.00605
//////////////////////////

static size_t base32hex_avx(uint8_t *dst, const uint8_t *src) {
  static int8_t zero_masks256[64] = {
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
  bool valid = true;
  const __m256i delta_check = _mm256_setr_epi8(
      -16, -32, -48, 70, -65, 41, -97, 9, 0, 0, 0, 0, 0, 0, 0, 0, -16, -32, -48,
      70, -65, 41, -97, 9, 0, 0, 0, 0, 0, 0, 0, 0);
  const __m256i delta_rebase = _mm256_setr_epi8(
      0, 0, 0, -48, -55, -55, -87, -87, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -48,
      -55, -55, -87, -87, 0, 0, 0, 0, 0, 0, 0, 0);
  const uint8_t *srcinit = src;
  do {
    __m256i v = _mm256_loadu_si256((__m256i *)src);

    __m256i hash_key =
        _mm256_and_si256(_mm256_srli_epi32(v, 4), _mm256_set1_epi8(0x0F));
    __m256i check =
        _mm256_add_epi8(_mm256_shuffle_epi8(delta_check, hash_key), v);
    v = _mm256_add_epi8(v, _mm256_shuffle_epi8(delta_rebase, hash_key));
    unsigned int m = (unsigned)_mm256_movemask_epi8(check);

    if (m) {
      int length = (int)trailing_zeroes(m);
      if (length == 0) {
        break;
      }
      src += length;
      __m256i zero_mask =
          _mm256_loadu_si256((__m256i *)(zero_masks256 + 32 - length));
      v = _mm256_andnot_si256(zero_mask, v);
      valid = false;
    } else { // common case
      src += 32;
    }
    v = _mm256_maddubs_epi16(v, _mm256_set1_epi32(0x01200120));
    v = _mm256_madd_epi16(
        v, _mm256_set_epi32(0x00010400, 0x00104000, 0x00010400, 0x00104000,
                            0x00010400, 0x00104000, 0x00010400, 0x00104000));
    // ...00000000`0000eeee`efffffgg`ggghhhhh`00000000`aaaaabbb`bbcccccd`dddd0000
    v = _mm256_or_si256(v, _mm256_srli_epi64(v, 48));
    v = _mm256_shuffle_epi8(
        v, _mm256_set_epi8(0, 0, 0, 0, 0, 0, 12, 13, 8, 9, 10, 4, 5, 0, 1, 2, 0,
                           0, 0, 0, 0, 0, 12, 13, 8, 9, 10, 4, 5, 0, 1, 2));
    // 5. store bytes
    _mm_storeu_si128((__m128i *)dst, _mm256_castsi256_si128(v));
    dst += 10;
    _mm_storeu_si128((__m128i *)dst, _mm256_extractf128_si256(v, 1));
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

  size_t decoded = base32hex_avx(rdata->octets+1, (const uint8_t*)token->data);
  if (decoded != token->length)
    SYNTAX_ERROR(parser, "Invalid %s in %s", NAME(field), NAME(type));
  *rdata->octets = (uint8_t)length;
  rdata->octets += 1 + length;
  return 0;
}

#endif // BASE32_H
