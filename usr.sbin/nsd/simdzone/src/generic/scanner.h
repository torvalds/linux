/*
 * scanner.h -- fast lexical analyzer for (DNS) zone files
 *
 * Copyright (c) 2022, NLnet Labs. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */
#ifndef SCANNER_H
#define SCANNER_H

#include <assert.h>
#include <string.h>
#include <stdio.h>

// Copied from simdjson under the terms of The 3-Clause BSD License.
// Copyright (c) 2018-2023 The simdjson authors
static really_inline uint64_t find_escaped(
  uint64_t backslash, uint64_t *is_escaped)
{
  backslash &= ~ *is_escaped;

  uint64_t follows_escape = backslash << 1 | *is_escaped;

  // Get sequences starting on even bits by clearing out the odd series using +
  const uint64_t even_bits = 0x5555555555555555ULL;
  uint64_t odd_sequence_starts = backslash & ~even_bits & ~follows_escape;
  uint64_t sequences_starting_on_even_bits;
  *is_escaped = add_overflow(odd_sequence_starts, backslash, &sequences_starting_on_even_bits);
  uint64_t invert_mask = sequences_starting_on_even_bits << 1; // The mask we want to return is the *escaped* bits, not escapes.

  // Mask every other backslashed character as an escaped character
  // Flip the mask for sequences that start on even bits, to correct them
  return (even_bits ^ invert_mask) & follows_escape;
}

// special characters in zone files cannot be identified without branching
// (unlike json) due to comments (*). no algorithm was found (so far) that
// can correctly identify quoted and comment regions where a quoted region
// includes a semicolon (or newline for that matter) and/or a comment region
// includes one (or more) quote characters. also, for comments, only newlines
// directly following a non-escaped, non-quoted semicolon must be included
static really_inline void find_delimiters(
  uint64_t quotes,
  uint64_t semicolons,
  uint64_t newlines,
  uint64_t in_quoted,
  uint64_t in_comment,
  uint64_t *quoted_,
  uint64_t *comment)
{
  uint64_t delimiters, starts = quotes | semicolons;
  uint64_t end;

  assert(!(quotes & semicolons));

  // carry over state from previous block
  end = (newlines & in_comment) | (quotes & in_quoted);
  end &= -end;

  delimiters = end;
  starts &= ~((in_comment | in_quoted) ^ (-end - end));

  while (starts) {
    const uint64_t start = -starts & starts;
    assert(start);
    const uint64_t quote = quotes & start;
    const uint64_t semicolon = semicolons & start;

    // FIXME: technically, this introduces a data dependency
    end = (newlines & -semicolon) | (quotes & (-quote - quote));
    end &= -end;

    delimiters |= end | start;
    starts &= -end - end;
  }

  *quoted_ = delimiters & quotes;
  *comment = delimiters & ~quotes;
}

static inline uint64_t follows(const uint64_t match, uint64_t *overflow)
{
  const uint64_t result = match << 1 | (*overflow);
  *overflow = match >> 63;
  return result;
}

static const simd_table_t blank = SIMD_TABLE(
  0x20, // 0x00 :  " " : 0x20 -- space
  0x00, // 0x01
  0x00, // 0x02
  0x00, // 0x03
  0x00, // 0x04
  0x00, // 0x05
  0x00, // 0x06
  0x00, // 0x07
  0x00, // 0x08
  0x09, // 0x09 : "\t" : 0x09 -- tab
  0x00, // 0x0a
  0x00, // 0x0b
  0x00, // 0x0c
  0x0d, // 0x0d : "\r" : 0x0d -- carriage return
  0x00, // 0x0e
  0x00  // 0x0f
);

static const simd_table_t special = SIMD_TABLE(
  0x00, // 0x00 : "\0" : 0x00 -- end-of-file
  0x00, // 0x01
  0x00, // 0x02
  0x00, // 0x03
  0x00, // 0x04
  0x00, // 0x05
  0x00, // 0x06
  0x00, // 0x07
  0x28, // 0x08 :  "(" : 0x28 -- start grouped
  0x29, // 0x09 :  ")" : 0x29 -- end grouped
  0x0a, // 0x0a : "\n" : 0x0a -- end-of-line
  0x00, // 0x0b
  0x00, // 0x0c
  0x00, // 0x0d
  0x00, // 0x0e
  0x00  // 0x0f
);

typedef struct block block_t;
struct block {
  simd_8x64_t input;
  uint64_t newline;
  uint64_t backslash;
  uint64_t escaped;
  uint64_t comment;
  uint64_t quoted;
  uint64_t semicolon;
  uint64_t in_quoted;
  uint64_t in_comment;
  uint64_t contiguous;
  uint64_t follows_contiguous;
  uint64_t blank;
  uint64_t special;
};

static really_inline void scan(parser_t *parser, block_t *block)
{
  // escaped newlines are classified as contiguous. however, escape sequences
  // have no meaning in comments and newlines, escaped or not, have no
  // special meaning in quoted
  block->newline = simd_find_8x64(&block->input, '\n');
  block->backslash = simd_find_8x64(&block->input, '\\');
  block->escaped = find_escaped(
    block->backslash, &parser->file->state.is_escaped);

  block->comment = 0;
  block->quoted = simd_find_8x64(&block->input, '"') & ~block->escaped;
  block->semicolon = simd_find_8x64(&block->input, ';') & ~block->escaped;

  block->in_quoted = parser->file->state.in_quoted;
  block->in_comment = parser->file->state.in_comment;

  if (block->in_comment || block->semicolon) {
    find_delimiters(
      block->quoted,
      block->semicolon,
      block->newline,
      block->in_quoted,
      block->in_comment,
     &block->quoted,
     &block->comment);

    block->in_quoted ^= prefix_xor(block->quoted);
    parser->file->state.in_quoted = (uint64_t)((int64_t)block->in_quoted >> 63);
    block->in_comment ^= prefix_xor(block->comment);
    parser->file->state.in_comment = (uint64_t)((int64_t)block->in_comment >> 63);
  } else {
    block->in_quoted ^= prefix_xor(block->quoted);
    parser->file->state.in_quoted = (uint64_t)((int64_t)block->in_quoted >> 63);
  }

  block->blank =
    simd_find_any_8x64(&block->input, blank) & ~(block->escaped | block->in_quoted | block->in_comment);
  block->special =
    simd_find_any_8x64(&block->input, special) & ~(block->escaped | block->in_quoted | block->in_comment);

  block->contiguous =
    ~(block->blank | block->special | block->quoted) & ~(block->in_quoted | block->in_comment);
  block->follows_contiguous =
    follows(block->contiguous, &parser->file->state.follows_contiguous);
}

static really_inline void write_indexes(parser_t *parser, const block_t *block, uint64_t clear)
{
  uint64_t fields = (block->contiguous & ~block->follows_contiguous) |
                    (block->quoted & block->in_quoted) |
                    (block->special);

  // delimiters are only important for contigouos and quoted character strings
  // (all other tokens automatically have a length 1). write out both in
  // separate vectors and base logic solely on field vector, order is
  // automatically correct
  uint64_t delimiters = (~block->contiguous & block->follows_contiguous) |
                        (block->quoted & ~block->in_quoted);

  fields &= ~clear;
  delimiters &= ~clear;

  const char *base = parser->file->buffer.data + parser->file->buffer.index;
  uint64_t field_count = count_ones(fields);
  uint64_t delimiter_count = count_ones(delimiters);
  // bulk of the data are contiguous and quoted character strings. field and
  // delimiter counts are therefore (mostly) equal. select the greater number
  // and write out indexes in a single loop leveraging superscalar properties
  // of modern CPUs
  uint64_t count = field_count;
  if (delimiter_count > field_count)
    count = delimiter_count;

  // take slow path if (escaped) newlines appear in contiguous or quoted
  // character strings. edge case, but must be supported and handled in the
  // scanner for ease of use and to accommodate for parallel processing in the
  // parser. escaped newlines may have been present in the last block
  uint64_t newlines = block->newline & (block->contiguous | block->in_quoted);

  // non-delimiting tokens may contain (escaped) newlines. tracking newlines
  // within tokens by taping them makes the lex operation more complex, resulting
  // in a significantly larger binary and slower operation, and may introduce an
  // infinite loop if the tape may not be sufficiently large enough. tokens
  // containing newlines is very much an edge case, therefore the scanner
  // implements an unlikely slow path that tracks the number of escaped newlines
  // during tokenization and registers them with each consecutive newline token.
  // this mode of operation nicely isolates location tracking in the scanner and
  // accommodates parallel processing should that ever be desired
  if (unlikely(*parser->file->newlines.tail || newlines)) {
    for (uint64_t i=0; i < count; i++) {
      const uint64_t field = fields & -fields;
      const uint64_t delimiter = delimiters & -delimiters;
      if (field & block->newline) {
        *parser->file->newlines.tail += count_ones(newlines & (field - 1));
        if (*parser->file->newlines.tail) {
          parser->file->fields.tail[i] = line_feed;
          parser->file->newlines.tail++;
        } else {
          parser->file->fields.tail[i] = base + trailing_zeroes(field);
        }
        newlines &= -field;
      } else {
        parser->file->fields.tail[i] = base + trailing_zeroes(field);
      }
      parser->file->delimiters.tail[i] = base + trailing_zeroes(delimiter);
      fields &= ~field;
      delimiters &= ~delimiter;
    }

    *parser->file->newlines.tail += count_ones(newlines);
    parser->file->fields.tail += field_count;
    parser->file->delimiters.tail += delimiter_count;
  } else {
    for (uint64_t i=0; i < 6; i++) {
      parser->file->fields.tail[i] = base + trailing_zeroes(fields);
      parser->file->delimiters.tail[i] = base + trailing_zeroes(delimiters);
      fields = clear_lowest_bit(fields);
      delimiters = clear_lowest_bit(delimiters);
    }

    if (unlikely(count > 6)) {
      for (uint64_t i=6; i < 12; i++) {
        parser->file->fields.tail[i] = base + trailing_zeroes(fields);
        parser->file->delimiters.tail[i] = base + trailing_zeroes(delimiters);
        fields = clear_lowest_bit(fields);
        delimiters = clear_lowest_bit(delimiters);
      }

      if (unlikely(count > 12)) {
        for (uint64_t i=12; i < count; i++) {
          parser->file->fields.tail[i] = base + trailing_zeroes(fields);
          parser->file->delimiters.tail[i] = base + trailing_zeroes(delimiters);
          fields = clear_lowest_bit(fields);
          delimiters = clear_lowest_bit(delimiters);
        }
      }
    }

    parser->file->fields.tail += field_count;
    parser->file->delimiters.tail += delimiter_count;
  }
}

nonnull_all
warn_unused_result
static really_inline int32_t reindex(parser_t *parser)
{
  block_t block = { 0 };

  assert(parser->file->buffer.index <= parser->file->buffer.length);
  size_t left = parser->file->buffer.length - parser->file->buffer.index;
  const char *data = parser->file->buffer.data + parser->file->buffer.index;
  const char **tape = parser->file->fields.tail;
  const char **tape_limit = parser->file->fields.tape + ZONE_TAPE_SIZE;

  if (left >= ZONE_BLOCK_SIZE) {
    const char *data_limit = parser->file->buffer.data +
                            (parser->file->buffer.length - ZONE_BLOCK_SIZE);
    while (data <= data_limit && ((uintptr_t)tape_limit - (uintptr_t)tape) >= ZONE_BLOCK_SIZE) {
      simd_loadu_8x64(&block.input, (const uint8_t *)data);
      scan(parser, &block);
      write_indexes(parser, &block, 0);
      parser->file->buffer.index += ZONE_BLOCK_SIZE;
      data += ZONE_BLOCK_SIZE;
      tape = parser->file->fields.tail;
    }

    assert(parser->file->buffer.index <= parser->file->buffer.length);
    left = parser->file->buffer.length - parser->file->buffer.index;
  }

  // only scan partial blocks after reading all data
  if (parser->file->end_of_file) {
    assert(left < ZONE_BLOCK_SIZE);
    if (!left) {
      parser->file->end_of_file = NO_MORE_DATA;
    } else if (((uintptr_t)tape_limit - (uintptr_t)tape) >= left) {
      // input is required to be padded, but may contain garbage
      uint8_t buffer[ZONE_BLOCK_SIZE] = { 0 };
      memcpy(buffer, data, left);
      const uint64_t clear = ~((1llu << left) - 1);
      simd_loadu_8x64(&block.input, buffer);
      scan(parser, &block);
      block.contiguous &= ~clear;
      write_indexes(parser, &block, clear);
      parser->file->end_of_file = NO_MORE_DATA;
      parser->file->buffer.index += left;
    }
  }

  return (uint64_t)((int64_t)(block.contiguous | block.in_quoted) >> 63) != 0;
}

#endif // SCANNER_H
