/*
 * type.h -- RRTYPE parser
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

#if BYTE_ORDER == LITTLE_ENDIAN
# define TYPE (0x45505954llu)
# define TYPE_MASK (0xffffffffllu)
# define CLASS (0x5353414c43llu)
# define CLASS_MASK (0xffffffffffllu)
#else
# define TYPE (0x5459504500000000llu)
# define TYPE_MASK (0xffffffff00000000llu)
# define CLASS (0x434c415353000000llu)
# define CLASS_MASK (0xffffffffff000000llu)
#endif

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
  prefix = le64toh(prefix);
  uint32_t value = (uint32_t)((prefix >> 32) ^ prefix);
  // magic value is generated using hash.c, rerun when adding types
  return (uint8_t)((value * 3537259401ull) >> 32);
}

nonnull_all
static really_inline int32_t scan_type_or_class(
  const char *data, size_t length, uint16_t *code, const mnemonic_t **mnemonic)
{
  uint64_t input0, input1;
  static const uint64_t letter_mask = 0x4040404040404040llu;

  // safe, input is padded
  memcpy(&input0, data, 8);
  memcpy(&input1, data + 8, 8);

  // convert to upper case
  input0 = input0 & ~((input0 & letter_mask) >> 1);
  input1 = input1 & ~((input1 & letter_mask) >> 1);

  length &= 0x1f;
  const uint8_t *zero_mask = (const uint8_t *)&zero_masks[32 - (length & 0x1f)];
  uint64_t zero_mask0, zero_mask1;

  // sanitize input
  memcpy(&zero_mask0, zero_mask, 8);
  memcpy(&zero_mask1, zero_mask + 8, 8);

  input0 &= zero_mask0;
  input1 &= zero_mask1;

  const uint8_t index = hash(input0);
  *code = (uint16_t)types_and_classes[index].mnemonic->value;
  *mnemonic = types_and_classes[index].mnemonic;

  uint64_t name0, name1;
  memcpy(&name0, (*mnemonic)->key.data, 8);
  memcpy(&name1, (*mnemonic)->key.data + 8, 8);

  if (likely(((input0 ^ name0) | (input1 ^ name1)) == 0) && *code)
    return types_and_classes[index].code;
  else if ((input0 & TYPE_MASK) == TYPE)
    return scan_generic_type(data, length, code, mnemonic);
  else if ((input0 & CLASS_MASK) == CLASS)
    return scan_generic_class(data, length, code, mnemonic);
  return 0;
}

nonnull_all
static really_inline int32_t scan_type(
  const char *data, size_t length, uint16_t *code, const mnemonic_t **mnemonic)
{
  uint64_t input0, input1;
  static const uint64_t letter_mask = 0x4040404040404040llu;

  // safe, input is padded
  memcpy(&input0, data, 8);
  memcpy(&input1, data + 8, 8);

  // convert to upper case
  input0 = input0 & ~((input0 & letter_mask) >> 1);
  input1 = input1 & ~((input1 & letter_mask) >> 1);

  length &= 0x1f;
  const uint8_t *zero_mask = (const uint8_t *)&zero_masks[32 - (length & 0x1f)];
  uint64_t zero_mask0, zero_mask1;

  // sanitize input
  memcpy(&zero_mask0, zero_mask, 8);
  memcpy(&zero_mask1, zero_mask + 8, 8);

  input0 &= zero_mask0;
  input1 &= zero_mask1;

  const uint8_t index = hash(input0);
  *code = (uint16_t)types_and_classes[index].mnemonic->value;
  *mnemonic = types_and_classes[index].mnemonic;

  uint64_t name0, name1;
  memcpy(&name0, (*mnemonic)->key.data, 8);
  memcpy(&name1, (*mnemonic)->key.data + 8, 8);

  if (likely(((input0 ^ name0) | (input1 ^ name1)) == 0) && *code)
    return types_and_classes[index].code;
  else if ((input0 & TYPE_MASK) == TYPE)
    return scan_generic_type(data, length, code, mnemonic);
  return 0;
}

#undef TYPE
#undef TYPE_MASK
#undef CLASS
#undef CLASS_MASK

#endif // TYPE_H
