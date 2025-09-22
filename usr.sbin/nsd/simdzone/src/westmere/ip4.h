/*
 * ip4.h -- SSE 4.1 parser for IPv4 addresses
 *          https://lemire.me/blog/2023/06/08/parsing-ip-addresses-crazily-fast/
 *
 * Copyright (c) 2023. Daniel Lemire
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */
#ifndef IP4_H
#define IP4_H

static const uint8_t patterns_id[256] = {
  38,  65,  255, 56,  73,  255, 255, 255, 255, 255, 255, 3,   255, 255, 6,
  255, 255, 9,   255, 27,  255, 12,  30,  255, 255, 255, 255, 15,  255, 33,
  255, 255, 255, 255, 18,  36,  255, 255, 255, 54,  21,  255, 39,  255, 255,
  57,  255, 255, 255, 255, 255, 255, 255, 255, 24,  42,  255, 255, 255, 60,
  255, 255, 255, 255, 255, 255, 255, 255, 45,  255, 255, 63,  255, 255, 255,
  255, 255, 255, 255, 255, 255, 48,  53,  255, 255, 66,  71,  255, 255, 16,
  255, 34,  255, 255, 255, 255, 255, 255, 255, 52,  255, 255, 22,  70,  40,
  255, 255, 58,  51,  255, 255, 69,  255, 255, 255, 255, 255, 255, 255, 255,
  255, 5,   255, 255, 255, 255, 255, 255, 11,  29,  46,  255, 255, 64,  255,
  255, 72,  0,   77,  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
  255, 255, 255, 255, 255, 76,  255, 255, 255, 255, 255, 255, 255, 75,  255,
  80,  255, 255, 255, 26,  255, 44,  255, 7,   62,  255, 255, 25,  255, 43,
  13,  31,  61,  255, 255, 255, 255, 255, 255, 255, 255, 255, 2,   19,  37,
  255, 255, 50,  55,  79,  68,  255, 255, 255, 255, 49,  255, 255, 67,  255,
  255, 255, 255, 17,  255, 35,  78,  255, 4,   255, 255, 255, 255, 255, 255,
  10,  23,  28,  41,  255, 255, 59,  255, 255, 255, 8,   255, 255, 255, 255,
  255, 1,   14,  32,  255, 255, 255, 255, 255, 255, 255, 255, 74,  255, 47,
  20,
};

static const uint8_t patterns[81][16] = {
  {0, 128, 2, 128, 4, 128, 6, 128, 128, 128, 128, 128, 128, 128, 128, 128},
  {0, 128, 2, 128, 4, 128, 7, 6, 128, 128, 128, 128, 128, 128, 128, 6},
  {0, 128, 2, 128, 4, 128, 8, 7, 128, 128, 128, 128, 128, 128, 6, 6},
  {0, 128, 2, 128, 5, 4, 7, 128, 128, 128, 128, 128, 128, 4, 128, 128},
  {0, 128, 2, 128, 5, 4, 8, 7, 128, 128, 128, 128, 128, 4, 128, 7},
  {0, 128, 2, 128, 5, 4, 9, 8, 128, 128, 128, 128, 128, 4, 7, 7},
  {0, 128, 2, 128, 6, 5, 8, 128, 128, 128, 128, 128, 4, 4, 128, 128},
  {0, 128, 2, 128, 6, 5, 9, 8, 128, 128, 128, 128, 4, 4, 128, 8},
  {0, 128, 2, 128, 6, 5, 10, 9, 128, 128, 128, 128, 4, 4, 8, 8},
  {0, 128, 3, 2, 5, 128, 7, 128, 128, 128, 128, 2, 128, 128, 128, 128},
  {0, 128, 3, 2, 5, 128, 8, 7, 128, 128, 128, 2, 128, 128, 128, 7},
  {0, 128, 3, 2, 5, 128, 9, 8, 128, 128, 128, 2, 128, 128, 7, 7},
  {0, 128, 3, 2, 6, 5, 8, 128, 128, 128, 128, 2, 128, 5, 128, 128},
  {0, 128, 3, 2, 6, 5, 9, 8, 128, 128, 128, 2, 128, 5, 128, 8},
  {0, 128, 3, 2, 6, 5, 10, 9, 128, 128, 128, 2, 128, 5, 8, 8},
  {0, 128, 3, 2, 7, 6, 9, 128, 128, 128, 128, 2, 5, 5, 128, 128},
  {0, 128, 3, 2, 7, 6, 10, 9, 128, 128, 128, 2, 5, 5, 128, 9},
  {0, 128, 3, 2, 7, 6, 11, 10, 128, 128, 128, 2, 5, 5, 9, 9},
  {0, 128, 4, 3, 6, 128, 8, 128, 128, 128, 2, 2, 128, 128, 128, 128},
  {0, 128, 4, 3, 6, 128, 9, 8, 128, 128, 2, 2, 128, 128, 128, 8},
  {0, 128, 4, 3, 6, 128, 10, 9, 128, 128, 2, 2, 128, 128, 8, 8},
  {0, 128, 4, 3, 7, 6, 9, 128, 128, 128, 2, 2, 128, 6, 128, 128},
  {0, 128, 4, 3, 7, 6, 10, 9, 128, 128, 2, 2, 128, 6, 128, 9},
  {0, 128, 4, 3, 7, 6, 11, 10, 128, 128, 2, 2, 128, 6, 9, 9},
  {0, 128, 4, 3, 8, 7, 10, 128, 128, 128, 2, 2, 6, 6, 128, 128},
  {0, 128, 4, 3, 8, 7, 11, 10, 128, 128, 2, 2, 6, 6, 128, 10},
  {0, 128, 4, 3, 8, 7, 12, 11, 128, 128, 2, 2, 6, 6, 10, 10},
  {1, 0, 3, 128, 5, 128, 7, 128, 128, 0, 128, 128, 128, 128, 128, 128},
  {1, 0, 3, 128, 5, 128, 8, 7, 128, 0, 128, 128, 128, 128, 128, 7},
  {1, 0, 3, 128, 5, 128, 9, 8, 128, 0, 128, 128, 128, 128, 7, 7},
  {1, 0, 3, 128, 6, 5, 8, 128, 128, 0, 128, 128, 128, 5, 128, 128},
  {1, 0, 3, 128, 6, 5, 9, 8, 128, 0, 128, 128, 128, 5, 128, 8},
  {1, 0, 3, 128, 6, 5, 10, 9, 128, 0, 128, 128, 128, 5, 8, 8},
  {1, 0, 3, 128, 7, 6, 9, 128, 128, 0, 128, 128, 5, 5, 128, 128},
  {1, 0, 3, 128, 7, 6, 10, 9, 128, 0, 128, 128, 5, 5, 128, 9},
  {1, 0, 3, 128, 7, 6, 11, 10, 128, 0, 128, 128, 5, 5, 9, 9},
  {1, 0, 4, 3, 6, 128, 8, 128, 128, 0, 128, 3, 128, 128, 128, 128},
  {1, 0, 4, 3, 6, 128, 9, 8, 128, 0, 128, 3, 128, 128, 128, 8},
  {1, 0, 4, 3, 6, 128, 10, 9, 128, 0, 128, 3, 128, 128, 8, 8},
  {1, 0, 4, 3, 7, 6, 9, 128, 128, 0, 128, 3, 128, 6, 128, 128},
  {1, 0, 4, 3, 7, 6, 10, 9, 128, 0, 128, 3, 128, 6, 128, 9},
  {1, 0, 4, 3, 7, 6, 11, 10, 128, 0, 128, 3, 128, 6, 9, 9},
  {1, 0, 4, 3, 8, 7, 10, 128, 128, 0, 128, 3, 6, 6, 128, 128},
  {1, 0, 4, 3, 8, 7, 11, 10, 128, 0, 128, 3, 6, 6, 128, 10},
  {1, 0, 4, 3, 8, 7, 12, 11, 128, 0, 128, 3, 6, 6, 10, 10},
  {1, 0, 5, 4, 7, 128, 9, 128, 128, 0, 3, 3, 128, 128, 128, 128},
  {1, 0, 5, 4, 7, 128, 10, 9, 128, 0, 3, 3, 128, 128, 128, 9},
  {1, 0, 5, 4, 7, 128, 11, 10, 128, 0, 3, 3, 128, 128, 9, 9},
  {1, 0, 5, 4, 8, 7, 10, 128, 128, 0, 3, 3, 128, 7, 128, 128},
  {1, 0, 5, 4, 8, 7, 11, 10, 128, 0, 3, 3, 128, 7, 128, 10},
  {1, 0, 5, 4, 8, 7, 12, 11, 128, 0, 3, 3, 128, 7, 10, 10},
  {1, 0, 5, 4, 9, 8, 11, 128, 128, 0, 3, 3, 7, 7, 128, 128},
  {1, 0, 5, 4, 9, 8, 12, 11, 128, 0, 3, 3, 7, 7, 128, 11},
  {1, 0, 5, 4, 9, 8, 13, 12, 128, 0, 3, 3, 7, 7, 11, 11},
  {2, 1, 4, 128, 6, 128, 8, 128, 0, 0, 128, 128, 128, 128, 128, 128},
  {2, 1, 4, 128, 6, 128, 9, 8, 0, 0, 128, 128, 128, 128, 128, 8},
  {2, 1, 4, 128, 6, 128, 10, 9, 0, 0, 128, 128, 128, 128, 8, 8},
  {2, 1, 4, 128, 7, 6, 9, 128, 0, 0, 128, 128, 128, 6, 128, 128},
  {2, 1, 4, 128, 7, 6, 10, 9, 0, 0, 128, 128, 128, 6, 128, 9},
  {2, 1, 4, 128, 7, 6, 11, 10, 0, 0, 128, 128, 128, 6, 9, 9},
  {2, 1, 4, 128, 8, 7, 10, 128, 0, 0, 128, 128, 6, 6, 128, 128},
  {2, 1, 4, 128, 8, 7, 11, 10, 0, 0, 128, 128, 6, 6, 128, 10},
  {2, 1, 4, 128, 8, 7, 12, 11, 0, 0, 128, 128, 6, 6, 10, 10},
  {2, 1, 5, 4, 7, 128, 9, 128, 0, 0, 128, 4, 128, 128, 128, 128},
  {2, 1, 5, 4, 7, 128, 10, 9, 0, 0, 128, 4, 128, 128, 128, 9},
  {2, 1, 5, 4, 7, 128, 11, 10, 0, 0, 128, 4, 128, 128, 9, 9},
  {2, 1, 5, 4, 8, 7, 10, 128, 0, 0, 128, 4, 128, 7, 128, 128},
  {2, 1, 5, 4, 8, 7, 11, 10, 0, 0, 128, 4, 128, 7, 128, 10},
  {2, 1, 5, 4, 8, 7, 12, 11, 0, 0, 128, 4, 128, 7, 10, 10},
  {2, 1, 5, 4, 9, 8, 11, 128, 0, 0, 128, 4, 7, 7, 128, 128},
  {2, 1, 5, 4, 9, 8, 12, 11, 0, 0, 128, 4, 7, 7, 128, 11},
  {2, 1, 5, 4, 9, 8, 13, 12, 0, 0, 128, 4, 7, 7, 11, 11},
  {2, 1, 6, 5, 8, 128, 10, 128, 0, 0, 4, 4, 128, 128, 128, 128},
  {2, 1, 6, 5, 8, 128, 11, 10, 0, 0, 4, 4, 128, 128, 128, 10},
  {2, 1, 6, 5, 8, 128, 12, 11, 0, 0, 4, 4, 128, 128, 10, 10},
  {2, 1, 6, 5, 9, 8, 11, 128, 0, 0, 4, 4, 128, 8, 128, 128},
  {2, 1, 6, 5, 9, 8, 12, 11, 0, 0, 4, 4, 128, 8, 128, 11},
  {2, 1, 6, 5, 9, 8, 13, 12, 0, 0, 4, 4, 128, 8, 11, 11},
  {2, 1, 6, 5, 10, 9, 12, 128, 0, 0, 4, 4, 8, 8, 128, 128},
  {2, 1, 6, 5, 10, 9, 13, 12, 0, 0, 4, 4, 8, 8, 128, 12},
  {2, 1, 6, 5, 10, 9, 14, 13, 0, 0, 4, 4, 8, 8, 12, 12},
};

// convert IPv4 from text to binary form.
//
// ipv4_string points to a character string containing an IPv4 network address in dotted-decimal format
// "ddd.ddd.ddd.ddd" of length ipv4_string_length (the string does not have to be null terminated),
// where ddd is a decimal number of up to three digits in the range 0 to 255. 
// The address is converted to a 32-bit integer (destination) (in  network byte order).
//
// Important: the function will systematically read 16 bytes at the provided address (ipv4_string). We infer
// the network address size in bytes by looking for a sequence of dots and decimal digits.
//
// returns 1 on success (network address was successfully converted).
//
// This function assumes that the processor supports SSE 4.1 instructions or better. That's true of most
// processors in operation today (June 2023).
static inline int sse_inet_aton(const char* ipv4_string, uint8_t* destination, size_t* restrict ipv4_string_length) {

  __m128i v = _mm_loadu_si128((const __m128i *)ipv4_string);

  __m128i is_dot = _mm_cmpeq_epi8(v, _mm_set1_epi8(0x2E));
  uint32_t dot_mask = (uint32_t)_mm_movemask_epi8(is_dot);

  // set non-digits to 0x80..0x89, set digits to 0x00..0x09
  const __m128i saturation_distance = _mm_set1_epi8(0x76); // 0x7F - 9
  v = _mm_xor_si128(v, _mm_set1_epi8(0x30)); // ascii '0'
  v = _mm_adds_epu8(v, saturation_distance);
  uint32_t non_digit_mask = (uint32_t)_mm_movemask_epi8(v);
  v = _mm_subs_epi8(v, saturation_distance);

  uint32_t bad_mask = dot_mask ^ non_digit_mask;
  uint32_t clip_mask = bad_mask ^ (bad_mask - 1);
  uint32_t partition_mask = non_digit_mask & clip_mask;

  const uint32_t length = (uint32_t)count_ones(clip_mask) - 1;

  uint32_t hash_key = (partition_mask * 0x00CF7800) >> 24;
  uint8_t hash_id = patterns_id[hash_key];
  if (hash_id >= 81)
      return 0;
  const uint8_t* const pattern_ptr = &patterns[hash_id][0];

  __m128i shuf = _mm_loadu_si128((const __m128i *)pattern_ptr);
  v = _mm_shuffle_epi8(v, shuf);

  const __m128i mul_weights =
      _mm_set_epi8(0,100, 0,100, 0,100, 0,100, 10,1, 10,1, 10,1, 10,1);
  __m128i acc = _mm_maddubs_epi16(mul_weights, v);
  __m128i swapped = _mm_shuffle_epi32(acc, _MM_SHUFFLE(1,0,3,2));
  acc = _mm_adds_epu16(acc, swapped);

  // check `v` for leading zeros in each partition, ignore lanes if partition has only one digit
  // if hibyte of `acc` then bad_char or overflow
  __m128i check_lz = _mm_xor_si128(_mm_cmpeq_epi8(_mm_setzero_si128(), v), shuf);
  __m128i check_of = _mm_adds_epu16(_mm_set1_epi16(0x7F00), acc);
  __m128i checks = _mm_or_si128(check_lz, check_of);
  uint32_t check_mask = (uint32_t)_mm_movemask_epi8(checks);
  check_mask &= 0x0000AA00; // the only lanes wanted

  // pack and we are done!
  uint32_t address = (uint32_t)_mm_cvtsi128_si32(_mm_packus_epi16(acc, acc));
  *ipv4_string_length = length;
  memcpy(destination, &address, 4);
  return (int)(length + check_mask - pattern_ptr[6]);
}

nonnull_all
static really_inline int32_t scan_ip4(const char *text, uint8_t *wire)
{
  size_t len;
  if (sse_inet_aton(text, wire, &len) != 1)
    return 0;
  return (int32_t)len;
}

nonnull_all
static really_inline int32_t parse_ip4(
  parser_t *parser,
  const type_info_t *type,
  const rdata_info_t *field,
  rdata_t *rdata,
  const token_t *token)
{
  // Note that this assumes that reading up to token->data + 16 is safe (i.e., we do not cross a page).
  if ((size_t)scan_ip4(token->data, rdata->octets) != token->length)
    SYNTAX_ERROR(parser, "Invalid %s in %s", NAME(field), NAME(type));
  rdata->octets += 4;
  return 0;
}

#endif // IP4_H
