/*
 * type.h -- SSE4.1 RRTYPE parser
 *
 * Copyright (c) 2023, NLnet Labs. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */
#ifndef TYPE_H
#define TYPE_H

#define V(code) { &(types[0].name),      0 }
#define T(code) { &(types[code].name),   1 }
#define C(code) { &(classes[code].name), 2 }

// map hash to type or class descriptor (generated using hash.c)
static const struct {
  const mnemonic_t *mnemonic;
  int32_t code;
} types_and_classes[256] = {
    V(0),   V(0),   V(0),   V(0),   V(0),   V(0),  T(34),   V(0),
    V(0),   V(0),  T(30),   V(0),   V(0),  T(57),   V(0),  T(16),
    V(0),   V(0),  T(56),  T(14),  T(12),   V(0),   V(0),  T(13),
   T(61),   V(0), T(105),   V(0),   V(0),   V(0),  T(32), T(258),
    V(0), T(107),  T(47),   V(0),   V(0),   V(0),  T(17),   V(0),
  T(257),   V(0),   V(0),   V(0),   V(0),   V(0),   V(0),   V(0),
   T(65),   V(0),   V(0),  T(18),   V(0),   T(1),   V(0), T(263),
    V(0),   V(0),   V(0),   V(0),  T(51),   V(0),   V(0), T(106),
    T(3),   V(0),   V(0),  T(31),   V(0),   V(0),   V(0),   V(0),
    V(0),  T(50),  T(44), T(104),  T(10),   V(0),   V(0),   V(0),
    V(0),   V(0),  T(55),   V(0),  T(28),   V(0),   V(0),   V(0),
    V(0),   V(0),   V(0),   V(0),   V(0),   V(0),   V(0),  T(39),
   T(35),   V(0),   V(0),   T(5),  T(29), T(262),   V(0),   V(0),
  T(109),   V(0), T(264),   V(0),   V(0),   V(0),   V(0),   V(0),
    V(0),  T(21),   V(0),   V(0),   V(0),   V(0),   V(0),   V(0),
   T(37),   C(1),  T(58),   V(0),   V(0),   V(0),   V(0),   V(0),
    V(0),   V(0),   V(0),   C(3),   V(0),  T(52),  T(11),  T(20),
    V(0), T(261),   V(0),   V(0),   V(0),  T(48),   V(0),   V(0),
    V(0),  T(25),   C(2),  T(43),   V(0),   V(0),   C(4),  T(60),
    V(0),   V(0),   T(7),   T(2),   V(0),   V(0),  T(46),  T(22),
    V(0),   V(0),   V(0),   V(0),   V(0),   V(0),   V(0),  T(64),
    V(0), T(260),   V(0),   V(0),   V(0),   V(0),  T(38),   V(0),
    V(0), T(259),  T(59),   V(0),   V(0),   V(0),  T(42),  T(36),
    T(8),  T(15),   V(0),  T(26),  T(27),   T(6),   V(0),  T(99),
    V(0),   V(0),   V(0),   V(0),   V(0),   V(0),   V(0),  T(53),
    T(9),  T(63),  T(33),   V(0), T(271), T(270),   V(0),  T(40),
    V(0),   V(0),  T(24),  T(19),   V(0),   V(0),   V(0),   V(0),
    V(0),   V(0),   V(0), T(108),   V(0),   V(0),   V(0),  T(62),
    V(0),   V(0),   V(0),   V(0),   V(0),  T(66),   T(4),   V(0),
    V(0),   V(0), T(256),   V(0),  T(49),   V(0),   V(0),   V(0),
    V(0),   V(0),  T(45),   V(0),   V(0),  T(23),   V(0),   V(0),
    V(0),   V(0),   V(0),   V(0),   V(0),   V(0),   V(0),   V(0)
};

#undef V
#undef T
#undef C

nonnull_all
static really_inline int32_t scan_generic_type(
  const char *data, size_t length, uint16_t *code, const mnemonic_t **mnemonic)
{
  if (scan_int16(data + 4, length - 4, code) == 0)
    return -1;
  if (*code <= 258)
    *mnemonic = &types[*code].name;
  else if (*code == 32769)
    *mnemonic = &types[259].name;
  else
    *mnemonic = &types[0].name;
  return 1;
}

nonnull_all
static really_inline int32_t scan_generic_class(
  const char *data, size_t length, uint16_t *code, const mnemonic_t **mnemonic)
{
  if (scan_int16(data + 5, length - 5, code) == 0)
    return -1;
  if (*code <= 4)
    *mnemonic = &classes[*code].name;
  else
    *mnemonic = &classes[0].name;
  return 2;
}

#define TYPE (0x45505954llu)
#define TYPE_MASK (0xffffffffllu)
#define CLASS (0x5353414c43llu)
#define CLASS_MASK (0xffffffffffllu)

static const int8_t zero_masks[48] = {
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
   0,   0,   0,   0,   0,   0,   0,   0,
   0,   0,   0,   0,   0,   0,   0,   0
};

static really_inline uint8_t hash(uint64_t prefix)
{
  uint32_t value = (uint32_t)((prefix >> 32) ^ prefix);
  // magic value is generated using hash.c, rerun when adding types
  return (uint8_t)((value * 3537259401ull) >> 32);
}

nonnull_all
static really_inline int32_t scan_type_or_class(
  const char *data, size_t length, uint16_t *code, const mnemonic_t **mnemonic)
{
  __m128i input = _mm_loadu_si128((const __m128i *)data);

  const __m128i letter_mask =
    _mm_srli_epi32(_mm_and_si128(input, _mm_set1_epi8(0x40)), 1);

  // convert to upper case
  input = _mm_andnot_si128(letter_mask, input);

  // sanitize input
  const __m128i zero_mask =
    _mm_loadu_si128((const __m128i *)&zero_masks[32 - (length & 0x1f)]);
  input = _mm_and_si128(input, zero_mask);

  // input is now sanitized and upper case

  const uint64_t prefix = (uint64_t)_mm_cvtsi128_si64(input);
  const uint8_t index = hash(prefix);
  *code = (uint16_t)types_and_classes[index].mnemonic->value;
  *mnemonic = types_and_classes[index].mnemonic;

  const __m128i compar = _mm_loadu_si128((const __m128i *)(*mnemonic)->key.data);
  const __m128i xorthem = _mm_xor_si128(compar, input);

  if (likely(_mm_test_all_zeros(xorthem, xorthem)))
    return types_and_classes[index].code;
  else if ((prefix & TYPE_MASK) == TYPE)
    return scan_generic_type(data, length, code, mnemonic);
  else if ((prefix & CLASS_MASK) == CLASS)
    return scan_generic_class(data, length, code, mnemonic);
  return 0;
}

nonnull_all
static really_inline int32_t scan_type(
  const char *data, size_t length, uint16_t *code, const mnemonic_t **mnemonic)
{
  __m128i input = _mm_loadu_si128((const __m128i *)data);

  const __m128i letter_mask =
    _mm_srli_epi32(_mm_and_si128(input, _mm_set1_epi8(0x40)), 1);

  // convert to upper case
  input = _mm_andnot_si128(letter_mask, input);

  // sanitize input
  const __m128i zero_mask =
    _mm_loadu_si128((const __m128i *)&zero_masks[32 - (length & 0x1f)]);
  input = _mm_and_si128(input, zero_mask);

  // input is now sanitized and upper case

  const uint64_t prefix = (uint64_t)_mm_cvtsi128_si64(input);
  const uint8_t index = hash(prefix);
  *code = (uint16_t)types_and_classes[index].mnemonic->value;
  *mnemonic = types_and_classes[index].mnemonic;

  const __m128i compar = _mm_loadu_si128((const __m128i *)(*mnemonic)->key.data);
  const __m128i xorthem = _mm_xor_si128(compar, input);

  // FIXME: make sure length matches too!
  if (likely(_mm_test_all_zeros(xorthem, xorthem)))
    return types_and_classes[index].code == 1;
  else if ((prefix & TYPE_MASK) == TYPE)
    return scan_generic_type(data, length, code, mnemonic);
  return 0;
}

#undef TYPE
#undef TYPE_MASK
#undef CLASS
#undef CLASS_MASK

#endif // TYPE_H
