/*
 * scanner.h -- fallback (non-simd) lexical analyzer for (DNS) zone data
 *
 * Copyright (c) 2022-2023, NLnet Labs. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */
#ifndef SCANNER_H
#define SCANNER_H

#include <assert.h>
#include <stdint.h>
#include <string.h>

nonnull_all
static really_inline const char *scan_comment(
  parser_t *parser, const char *start, const char *end)
{
  assert(!parser->file->state.is_escaped);

  while (start < end) {
    if (unlikely(*start == '\n')) {
      parser->file->state.in_comment = 0;
      return start;
    }
    start++;
  }

  parser->file->state.in_comment = 1;
  return start;
}

nonnull_all
static really_inline const char *scan_quoted(
  parser_t *parser, const char *start, const char *end)
{
  if (unlikely(parser->file->state.is_escaped && start < end))
    goto escaped;

  while (start < end) {
    if (*start == '\\') {
      start++;
escaped:
      if ((parser->file->state.is_escaped = (start == end)))
        break;
      assert(start < end);
      *parser->file->newlines.tail += (*start == '\n');
      start++;
    } else if (*start == '\"') {
      parser->file->state.in_quoted = 0;
      *parser->file->delimiters.tail++ = start;
      return ++start;
    } else {
      *parser->file->newlines.tail += (*start == '\n');
      start++;
    }
  }

  parser->file->state.in_quoted = 1;
  return start;
}

nonnull_all
static really_inline const char *scan_contiguous(
  parser_t *parser, const char *start, const char *end)
{
  if (parser->file->state.is_escaped && start < end)
    goto escaped;

  while (start < end) {
    // null-byte is considered contiguous by the indexer (for now)
    if (likely((classify[ (uint8_t)*start ] & ~CONTIGUOUS) == 0)) {
      if (unlikely(*start == '\\')) {
        start++;
escaped:
        if ((parser->file->state.is_escaped = (start == end)))
          break;
        assert(start < end);
        parser->file->newlines.tail[0] += (*start == '\n');
      }
      start++;
    } else {
      parser->file->state.follows_contiguous = 0;
      *parser->file->delimiters.tail++ = start;
      return start;
    }
  }

  parser->file->state.follows_contiguous = 1;
  return start;
}

nonnull_all
static really_inline void scan(
  parser_t *parser, const char *start, const char *end)
{
  if (parser->file->state.follows_contiguous)
    start = scan_contiguous(parser, start, end);
  else if (parser->file->state.in_comment)
    start = scan_comment(parser, start, end);
  else if (parser->file->state.in_quoted)
    start = scan_quoted(parser, start, end);

  while (start < end) {
    const int32_t code = classify[(uint8_t)*start];
    if (code == BLANK) {
      start++;
    } else if ((code & ~CONTIGUOUS) == 0) {
      // null-byte is considered contiguous by the indexer (for now)
      *parser->file->fields.tail++ = start;
      start = scan_contiguous(parser, start, end);
    } else if (code == LINE_FEED) {
      if (*parser->file->newlines.tail) {
        *parser->file->fields.tail++ = line_feed;
        parser->file->newlines.tail++;
      } else {
        *parser->file->fields.tail++ = start;
      }
      start++;
    } else if (code == QUOTED) {
      *parser->file->fields.tail++ = start;
      start = scan_quoted(parser, start + 1, end);
    } else if (code == LEFT_PAREN || code == RIGHT_PAREN) {
      *parser->file->fields.tail++ = start;
      start++;
    } else {
      assert(code == COMMENT);
      start = scan_comment(parser, start, end);
    }
  }
}

nonnull_all
warn_unused_result
static really_inline int32_t reindex(parser_t *parser)
{
  assert(parser->file->buffer.index <= parser->file->buffer.length);
  size_t left = parser->file->buffer.length - parser->file->buffer.index;
  const char *data = parser->file->buffer.data + parser->file->buffer.index;
  const char **tape = parser->file->fields.tail;
  const char **tape_limit = parser->file->fields.tape + ZONE_TAPE_SIZE;

  if (left >= ZONE_BLOCK_SIZE) {
    const char *data_limit = parser->file->buffer.data +
                            (parser->file->buffer.length - ZONE_BLOCK_SIZE);
    while (data <= data_limit && ((uintptr_t)tape_limit - (uintptr_t)tape) >= ZONE_BLOCK_SIZE) {
      scan(parser, data, data + ZONE_BLOCK_SIZE);
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
      scan(parser, data, data + left);
      parser->file->end_of_file = NO_MORE_DATA;
      parser->file->buffer.index += left;
      parser->file->state.follows_contiguous = 0;
    }
  }

  return (parser->file->state.follows_contiguous | parser->file->state.in_quoted) != 0;
}

#endif // SCANNER_H
