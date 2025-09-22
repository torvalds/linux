/*
 * text.h -- string parser
 *
 * Copyright (c) 2022-2023, NLnet Labs. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */
#ifndef TEXT_H
#define TEXT_H

nonnull_all
static really_inline uint32_t unescape(const char *text, uint8_t *wire)
{
  uint8_t d[3];
  uint32_t o;

  if ((d[0] = (uint8_t)text[1] - '0') > 9) {
    o = (uint8_t)text[1];
    *wire = (uint8_t)o;
    return 2u;
  } else {
    d[1] = (uint8_t)text[2] - '0';
    d[2] = (uint8_t)text[3] - '0';
    o = d[0] * 100 + d[1] * 10 + d[2];
    *wire = (uint8_t)o;
    return (o > 255 || d[1] > 9 || d[2] > 9) ? 0 : 4u;
  }
}

typedef struct string_block string_block_t;
struct string_block {
  uint64_t backslashes;
};

nonnull_all
static really_inline void copy_string_block(
  string_block_t *block, const char *text, uint8_t *wire)
{
  simd_8x32_t input;
  simd_loadu_8x32(&input, text);
  simd_storeu_8x32(wire, &input);
  block->backslashes = simd_find_8x32(&input, '\\');
}

nonnull_all
static really_inline int32_t scan_string(
  const char *data,
  size_t length,
  uint8_t *octets,
  const uint8_t *limit)
{
  const char *text = data;
  uint8_t *wire = octets;
  string_block_t block;

  copy_string_block(&block, text, octets);

  uint64_t count = 32;
  if (length < 32)
    count = length;
  uint64_t mask = (1llu << count) - 1u;

  // check for escape sequences
  if (unlikely(block.backslashes & mask))
    goto escaped;

  if (length < 32)
    return (int32_t)count;

  text += count;
  wire += count;
  length -= count;

  do {
    copy_string_block(&block, text, wire);
    count = 32;
    if (length < 32)
      count = length;
    mask = (1llu << count) - 1u;

    // check for escape sequences
    if (unlikely(block.backslashes & mask)) {
escaped:
      block.backslashes &= -block.backslashes;
      mask = block.backslashes - 1;
      count = count_ones(mask);
      const uint32_t octet = unescape(text+count, wire+count);
      if (!octet)
        return -1;
      text += count + octet;
      wire += count + 1;
      length -= count + octet;
    } else {
      text += count;
      wire += count;
      length -= count;
    }
  } while (length && wire < limit);

  if (length || (wire > limit))
    return -1;
  assert(!length);
  return (int32_t)(wire - octets);
}

#endif // TEXT_H
