/*
 * name.h -- domain name parser
 *
 * Copyright (c) 2022-2023, NLnet Labs. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */
#ifndef NAME_H
#define NAME_H

typedef struct name_block name_block_t;
struct name_block {
  uint64_t backslashes;
  uint64_t dots;
};

nonnull_all
static really_inline void copy_name_block(
  name_block_t *block, const char *text, uint8_t *wire)
{
  simd_8x32_t input;
  simd_loadu_8x32(&input, text);
  simd_storeu_8x32(wire, &input);
  block->backslashes = simd_find_8x32(&input, '\\');
  block->dots = simd_find_8x32(&input, '.');
}

nonnull_all
static really_inline int32_t scan_name(
  const char *data,
  size_t tlength,
  uint8_t octets[255 + ZONE_BLOCK_SIZE],
  size_t *lengthp)
{
  uint64_t label = 0;
  const char *text = data;
  uint8_t *wire = octets + 1;
  name_block_t block;

  octets[0] = 0;

  // real world domain names quickly exceed 16 octets (www.example.com is
  // encoded as 3www7example3com0, or 18 octets), but rarely exceed 32
  // octets. encode in 32-byte blocks.
  copy_name_block(&block, text, wire);

  uint64_t count = 32, length = 0, base = 0, left = tlength;
  uint64_t carry = 0;
  if (tlength < 32)
    count = tlength;
  uint64_t mask = (1llu << count) - 1u;

  // check for escape sequences
  if (unlikely(block.backslashes & mask))
    goto escaped;

  // check for root, i.e. "."
  if (unlikely(block.dots & 1llu))
    return ((*lengthp = tlength) == 1 ? 0 : -1);

  length = count;
  block.dots &= mask;
  carry = (block.dots >> (length - 1));

  // check for null labels, i.e. ".."
  if (unlikely(block.dots & (block.dots >> 1)))
    return -1;

  if (likely(block.dots)) {
    count = trailing_zeroes(block.dots);
    block.dots = clear_lowest_bit(block.dots);
    octets[label] = (uint8_t)count;
    label = count + 1;
    while (block.dots) {
      count = trailing_zeroes(block.dots);
      block.dots = clear_lowest_bit(block.dots);
      octets[label] = (uint8_t)(count - label);
      label = count + 1;
    }
  }

  octets[label] = (uint8_t)(length - label);

  if (tlength <= 32)
    return (void)(*lengthp = length + 1), carry == 0;

  text += length;
  wire += length;
  left -= length;

  do {
    copy_name_block(&block, text, wire);
    count = 32;
    if (left < 32)
      count = left;
    mask = (1llu << count) - 1u;
    base = length;

    // check for escape sequences
    if (unlikely(block.backslashes & mask)) {
escaped:
      block.backslashes &= -block.backslashes;
      mask = block.backslashes - 1;
      block.dots &= mask;
      count = count_ones(mask);
      const uint32_t octet = unescape(text+count, wire+count);
      if (!octet)
        return -1;
      text += count + octet;
      wire += count + 1;
      length += count + 1;
      left -= count + octet;
      count += 1; // for correct carry
    } else {
      block.dots &= mask;
      text += count;
      wire += count;
      length += count;
      left -= count;
    }

    // check for null labels, i.e. ".."
    if (unlikely(block.dots & ((block.dots >> 1) | carry)))
      return -1;
    carry = block.dots >> (count - 1);

    if (likely(block.dots)) {
      count = trailing_zeroes(block.dots) + base;
      block.dots = clear_lowest_bit(block.dots);
      octets[label] = (uint8_t)(count - label);
      // check if label exceeds 63 octets
      if (unlikely(count - label > 63))
        return -1;
      label = count + 1;
      while (block.dots) {
        count = trailing_zeroes(block.dots) + base;
        block.dots = clear_lowest_bit(block.dots);
        octets[label] = (uint8_t)(count - label);
        label = count + 1;
      }
    } else {
      // check if label exceeds 63 octets
      if (length - label > 63)
        return -1;
    }

    octets[label] = (uint8_t)(length - label);
  } while (left && length < 255);

  if (length >= 255)
    return -1;

  *lengthp = length + 1;
  return carry == 0;
}

#endif // NAME_H
