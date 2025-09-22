/**
 * algorithm.h -- Algorithm RDATA parser
 *
 * Copyright (c) 2023, NLnet Labs. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */
#ifndef ALGORITHM_H
#define ALGORITHM_H

// https://www.iana.org/assignments/dns-sec-alg-numbers/dns-sec-alg-numbers.xhtml

typedef struct algorithm algorithm_t;
struct algorithm {
  struct {
    char name[24];
    size_t length;
  } key;
  uint8_t value;
};

#define BAD_ALGORITHM(value) \
  { { "",  0 }, 0 }
#define ALGORITHM(name, value) \
  { { name, sizeof(name) - 1 }, value }

static const algorithm_t algorithms[32] = {
  BAD_ALGORITHM(0),
  ALGORITHM("RSAMD5", 1),
  ALGORITHM("DH", 2),
  ALGORITHM("DSA", 3),
  ALGORITHM("ECC", 4),
  ALGORITHM("RSASHA1", 5),
  ALGORITHM("DSA-NSEC-SHA1", 6),
  ALGORITHM("RSASHA1-NSEC3-SHA1", 7),
  ALGORITHM("RSASHA256", 8),
  BAD_ALGORITHM(9),
  ALGORITHM("RSASHA512", 10),
  BAD_ALGORITHM(11),
  ALGORITHM("ECC-GOST", 12),
  ALGORITHM("ECDSAP256SHA256", 13),
  ALGORITHM("ECDSAP384SHA384", 14),
  BAD_ALGORITHM(15),
  ALGORITHM("INDIRECT", 252),
  ALGORITHM("PRIVATEDNS", 253),
  ALGORITHM("PRIVATEOID", 254),
};

static const struct {
  const algorithm_t *algorithm;
  uint8_t mask[24];
} algorithm_hash_map[16] = {
  { &algorithms[2],  // DH (0)
    { 0xdf, 0xdf, 0 } },
  { &algorithms[10], // RSASHA512 (1)
    { 0xdf, 0xdf, 0xdf, 0xdf, 0xdf, 0xdf, 0xff, 0xff,
      0xff, 0 } },
  { &algorithms[7],  // RSASHA1-NSEC3-SHA1 (2)
    { 0xdf, 0xdf, 0xdf, 0xdf, 0xdf, 0xdf, 0xff, 0xff, 
      0xdf, 0xdf, 0xdf, 0xdf, 0xff, 0xff, 0xdf, 0xdf,
      0xdf, 0xff, 0 } },
  { &algorithms[8],  // RSASHA256 (3)
    { 0xdf, 0xdf, 0xdf, 0xdf, 0xdf, 0xdf, 0xff, 0xff,
      0xff, 0 } },
  { &algorithms[13], // ECDSAP256SHA256 (4)
    { 0xdf, 0xdf, 0xdf, 0xdf, 0xdf, 0xdf, 0xff, 0xff,
      0xff, 0xdf, 0xdf, 0xdf, 0xff, 0xff, 0xff, 0 } },
  { &algorithms[0],  // unknown
    { 0 } },
  { &algorithms[6],  // DSA-NSEC-SHA1 (6)
    { 0xdf, 0xdf, 0xdf, 0xff, 0xdf, 0xdf, 0xdf, 0xdf,
      0xff, 0xdf, 0xdf, 0xdf, 0xff, 0 } },
  { &algorithms[1],  // RSAMD5 (7)
    { 0xdf, 0xdf, 0xdf, 0xdf, 0xdf, 0xff, 0 } },
  { &algorithms[5],  // RSASHA1 (8)
    { 0xdf, 0xdf, 0xdf, 0xdf, 0xdf, 0xdf, 0xff, 0 } },
  { &algorithms[17], // PRIVATEDNS (9)
    { 0xdf, 0xdf, 0xdf, 0xdf, 0xdf, 0xdf, 0xdf, 0xdf,
      0xdf, 0xdf, 0 } },
  { &algorithms[18], // PRIVATEOID (10)
    { 0xdf, 0xdf, 0xdf, 0xdf, 0xdf, 0xdf, 0xdf, 0xdf,
      0xdf, 0xdf, 0 } },
  { &algorithms[16], // INDIRECT (11)
    { 0xdf, 0xdf, 0xdf, 0xdf, 0xdf, 0xdf, 0xdf, 0xdf,
      0 } },
  { &algorithms[14], // ECDSAP384SHA384 (12)
    { 0xdf, 0xdf, 0xdf, 0xdf, 0xdf, 0xdf, 0xff, 0xff,
      0xff, 0xdf, 0xdf, 0xdf, 0xff, 0xff, 0xff, 0 } },
  { &algorithms[3],  // DSA (13)
    { 0xdf, 0xdf, 0xdf, 0 } },
  { &algorithms[4],  // ECC (14)
    { 0xdf, 0xdf, 0xdf, 0 } },
  { &algorithms[12], // ECC-GHOST (15)
    { 0xdf, 0xdf, 0xdf, 0xff, 0xdf, 0xdf, 0xdf, 0xdf,
      0 } }
};

#undef UNKNOWN_ALGORITHM
#undef ALGORITHM

// magic value generated using algorithm-hash.c
static uint8_t algorithm_hash(uint64_t value)
{
  value = le64toh(value);
  uint32_t value32 = (uint32_t)((value >> 32) ^ value);
  return (uint8_t)((value32 * 29874llu) >> 32) & 0xf;
}

nonnull_all
warn_unused_result
static really_inline int32_t scan_algorithm(
  const char *data, size_t length, uint8_t *number)
{
  static const int8_t zero_masks[48] = {
    -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1,
     0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0
  };

  if ((uint8_t)*data - '0' > 9) {
    uint64_t input;
    memcpy(&input, data, 8);
    const uint64_t letter_mask = 0x4040404040404040llu;
    // convert to upper case
    input &= ~((input & letter_mask) >> 1);
    // zero out non-relevant bytes
    uint64_t zero_mask;
    memcpy(&zero_mask, &zero_masks[32 - (length & 0x1f)], 8);
    input &= zero_mask;
    const uint8_t index = algorithm_hash(input);
    assert(index < 16);
    const algorithm_t *algorithm = algorithm_hash_map[index].algorithm;
    uint64_t matches, mask, name;
    // compare bytes 0-7
    memcpy(&name, algorithm->key.name, 8);
    matches = input == name;
    // compare bytes 8-15
    memcpy(&input, data + 8, 8);
    memcpy(&mask, algorithm_hash_map[index].mask + 8, 8);
    memcpy(&name, algorithm->key.name + 8, 8);
    matches &= (input & mask) == name;
    // compare bytes 16-23
    memcpy(&input, data + 16, 8);
    memcpy(&mask, algorithm_hash_map[index].mask + 16, 8);
    memcpy(&name, algorithm->key.name + 16, 8);
    matches &= (input & mask) == name;
    *number = algorithm->value;
    return matches & (length == algorithm->key.length) & (*number > 0);
  }

  return scan_int8(data, length, number);
}

nonnull_all
warn_unused_result
static really_inline int32_t parse_algorithm(
  parser_t *parser,
  const type_info_t *type,
  const rdata_info_t *field,
  rdata_t *rdata,
  const token_t *token)
{
  if (!scan_algorithm(token->data, token->length, rdata->octets))
    SYNTAX_ERROR(parser, "Invalid %s in %s", NAME(field), NAME(type));
  rdata->octets++;
  return 0;
}

#endif // ALGORITHM_H
