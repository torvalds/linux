/*
 * parser.h -- base parser definitions
 *
 * Copyright (c) 2023, NLnet Labs. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */
#ifndef PARSER_H
#define PARSER_H

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#if _MSC_VER
# define strncasecmp(s1, s2, n) _strnicmp((s1), (s2), (n))
#else
# include <strings.h>
#endif

typedef zone_parser_t parser_t; // convenience
typedef zone_file_t file_t;
typedef zone_name_buffer_t name_buffer_t;
typedef zone_rdata_buffer_t rdata_buffer_t;

typedef struct token token_t;
struct token {
  int32_t code;
  const char *data;
  size_t length;
};

// view of current RDATA buffer
typedef struct rdata rdata_t;
struct rdata {
  uint8_t *octets;
  uint8_t *limit;
};

typedef struct string string_t;
struct string {
  const char *data;
  size_t length;
};

typedef struct mnemonic mnemonic_t;
struct mnemonic {
  struct {
    char data[16]; /* MUST be 16 because of usage with SIMD cmpeq */
    size_t length;
  } key;
  uint32_t value;
};

#define NAME(info) ((info)->name.key.data)


typedef struct svc_param_info svc_param_info_t;
struct svc_param_info;

typedef struct rdata_info rdata_info_t;
struct rdata_info;

typedef struct type_info type_info_t;
struct type_info;


typedef int32_t (*parse_svc_param_t)(
  parser_t *,
  const type_info_t *,
  const rdata_info_t *,
  uint16_t,
  const svc_param_info_t *,
  rdata_t *,
  const token_t *);

struct svc_param_info {
  struct {
    struct {
      char data[24];
      size_t length;
    } key;
    uint32_t value;
  } name;
  uint32_t has_value;
  parse_svc_param_t parse, parse_lax;
};

struct rdata_info {
  struct { string_t key; } name; // convenience
};

typedef struct class_info class_info_t;
struct class_info {
  mnemonic_t name;
};

typedef int32_t (*check_rr_t)(
  parser_t *,
  const type_info_t *,
  const rdata_t *);

typedef int32_t (*parse_rdata_t)(
  parser_t *,
  const type_info_t *,
  rdata_t *,
  token_t *);

struct type_info {
  mnemonic_t name;
  uint16_t defined_in;
  bool is_obsolete;
  bool is_experimental;
  struct {
    size_t length;
    const rdata_info_t *fields;
  } rdata;
  check_rr_t check;
  parse_rdata_t parse;
};


#define END_OF_FILE (0)
#define CONTIGUOUS (1<<0)
#define QUOTED (1<<1)
#define LINE_FEED (1<<2)
#define LEFT_PAREN (1<<3)
#define RIGHT_PAREN (1<<4)
#define BLANK (1<<6)
#define COMMENT (1<<7)

static const uint8_t classify[256] = {
  // 0x00 = "\0"
  0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,  // 0x00 - 0x07
  // 0x09 = "\t", 0x0a = "\n", 0x0d = "\r"
  0x01, 0x40, 0x04, 0x01, 0x01, 0x40, 0x01, 0x01,  // 0x08 - 0x0f
  0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,  // 0x10 - 0x17
  0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,  // 0x18 - 0x1f
  // 0x20 = " ", 0x22 = "\""
  0x40, 0x01, 0x02, 0x01, 0x01, 0x01, 0x01, 0x01,  // 0x20 - 0x27
  // 0x28 = "(", 0x29 = ")"
  0x08, 0x10, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,  // 0x28 - 0x2f
  0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,  // 0x30 - 0x37
  // 0x3b = ";"
  0x01, 0x01, 0x01, 0x80, 0x01, 0x01, 0x01, 0x01,  // 0x38 - 0x3f
  0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,  // 0x40 - 0x47
  0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,  // 0x48 - 0x4f
  0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,  // 0x50 - 0x57
  0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,  // 0x58 - 0x5f
  0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,  // 0x60 - 0x67
  0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,  // 0x68 - 0x6f
  0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,  // 0x70 - 0x77
  0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,  // 0x78 - 0x7f
  0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,  // 0x80 - 0x87
  0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,  // 0x88 - 0x8f
  0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,  // 0x90 - 0x97
  0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,  // 0x98 - 0x9f
  0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,  // 0xa0 - 0xa7
  0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,  // 0xa8 - 0xaf
  0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,  // 0xb0 - 0xb7
  0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,  // 0xb8 - 0xbf
  0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,  // 0xc0 - 0xc7
  0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,  // 0xc8 - 0xcf
  0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,  // 0xd0 - 0xd7
  0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,  // 0xd8 - 0xdf
  0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,  // 0xe0 - 0xe7
  0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,  // 0xe8 - 0xef
  0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,  // 0xf8 - 0xf7
  0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01   // 0xf8 - 0xff
};



// special constant to mark line feeds with additional line count. i.e. CRLF
// within text. line feeds have no special meaning other than terminating the
// record and require no further processing
static const char line_feed[ZONE_BLOCK_SIZE] = { '\n', '\0' };

// special constant used as data on errors
static const char end_of_file[ZONE_BLOCK_SIZE] = { '\0' };

#define READ_ALL_DATA (1)
#define NO_MORE_DATA (2)
#define MISSING_QUOTE (3)

extern int32_t zone_open_file(
  parser_t *, const char *path, size_t length, zone_file_t **);

extern void zone_close_file(
  parser_t *, zone_file_t *);

extern void zone_vlog(parser_t *, uint32_t, const char *, va_list);

nonnull((1))
static really_inline void defer_error(token_t *token, int32_t code)
{
  token->code = code;
  token->data = end_of_file;
  token->length = 0;
}

nonnull((1,3))
warn_unused_result
static never_inline int32_t raise_error(
  parser_t *parser, int32_t code, const char *format, ...)
{
  va_list arguments;
  uint32_t category = ZONE_ERROR;
  if (code == ZONE_SEMANTIC_ERROR && parser->options.secondary)
    category = ZONE_WARNING;
  va_start(arguments, format);
  zone_vlog(parser, category, format, arguments);
  va_end(arguments);
  if (category == ZONE_WARNING)
    return 0;
  return code;
}

#define RAISE_ERROR(parser, code, ...) \
  do { \
    return raise_error((parser), (code), __VA_ARGS__); \
  } while (0)

#define SYNTAX_ERROR(parser, ...) \
  RAISE_ERROR((parser), ZONE_SYNTAX_ERROR, __VA_ARGS__)
#define OUT_OF_MEMORY(parser, ...) \
  RAISE_ERROR((parser), ZONE_OUT_OF_MEMORY, __VA_ARGS__)
#define READ_ERROR(parser, ...) \
  RAISE_ERROR((parser), ZONE_READ_ERROR, __VA_ARGS__)
#define NOT_IMPLEMENTED(parser, ...) \
  RAISE_ERROR((parser), ZONE_NOT_IMPLEMENTED, __VA_ARGS__)
#define NOT_PERMITTED(parser, ...) \
  RAISE_ERROR((parser), ZONE_NOT_PERMITTED, __VA_ARGS__)
#define NOT_A_FILE(parser, ...) \
  RAISE_ERROR((parser), ZONE_NOT_A_FILE, __VA_ARGS__)

// semantic errors in zone files are special as a secondary may choose
// to report, but otherwise ignore them. e.g. a TTL with the MSB set. cases
// where the data can be presented in wire format but is otherwise considered
// invalid. e.g. a TTL is limited to 32-bits, values that require more bits
// are invalid without exception, but secondaries may choose to accept values
// with the MSB set in order to update the zone
#define SEMANTIC_ERROR(parser, ...) \
  do { \
    if (raise_error((parser), ZONE_SEMANTIC_ERROR, __VA_ARGS__)) \
      return ZONE_SEMANTIC_ERROR; \
  } while (0)


nonnull_all
warn_unused_result
static really_inline int32_t reindex(parser_t *parser);

// limit maximum size of buffer to avoid malicious inputs claiming all memory.
// the maximum size of the buffer is the worst-case size of rdata, or 65535
// bytes, in presentation format. comma-separated value lists as introduced
// by RFC 9460 allow for double escaping. a reasonable limit is therefore
// 65535 (rdata) * 4 (\DDD) * 4 (\DDD) + 64 (sufficiently large enough to
// cover longest key and ancillary characters) bytes.
#define MAXIMUM_WINDOW_SIZE (65535u * 4u * 4u + 64u)

nonnull_all
warn_unused_result
static int32_t refill(parser_t *parser)
{
  // refill if possible (i.e. not if string or if file is empty)
  if (parser->file->end_of_file)
    return 0;

  assert(parser->file->handle);

  // move unread data to start of buffer
  char *data = parser->file->buffer.data + parser->file->buffer.index;
  // account for non-terminated character-strings
  if (*parser->file->fields.head[0] != '\0')
    data = (char *)parser->file->fields.head[0];

  *parser->file->fields.head = parser->file->buffer.data;
  // account for unread data left in buffer
  size_t length = (size_t)
    ((parser->file->buffer.data + parser->file->buffer.length) - data);
  // account for non-terminated character-string left in buffer
  assert((parser->file->buffer.data + parser->file->buffer.index) >= data);
  size_t index = (size_t)
    ((parser->file->buffer.data + parser->file->buffer.index) - data);
  memmove(parser->file->buffer.data, data, length);
  parser->file->buffer.length = length;
  parser->file->buffer.index = index;
  parser->file->buffer.data[length] = '\0';

  // allocate extra space if required
  if (parser->file->buffer.length == parser->file->buffer.size) {
    size_t size = parser->file->buffer.size;
    if (parser->file->buffer.size >= MAXIMUM_WINDOW_SIZE)
      SYNTAX_ERROR(parser, "Impossibly large input, exceeds %zu bytes", size);
    size += ZONE_WINDOW_SIZE;
    if (!(data = realloc(parser->file->buffer.data, size + 1 + ZONE_BLOCK_SIZE)))
      OUT_OF_MEMORY(parser, "Not enough memory to allocate buffer of %zu", size);
    parser->file->buffer.size = size;
    parser->file->buffer.data = data;
    // update reference to partial token
    parser->file->fields.head[0] = data;
  }

  size_t count = fread(
    parser->file->buffer.data + parser->file->buffer.length,
    sizeof(parser->file->buffer.data[0]),
    parser->file->buffer.size - parser->file->buffer.length,
    parser->file->handle);

  if (!count && ferror(parser->file->handle))
    READ_ERROR(parser, "Cannot refill buffer");

  // always null-terminate for terminating token
  parser->file->buffer.length += (size_t)count;
  parser->file->buffer.data[parser->file->buffer.length] = '\0';
  parser->file->end_of_file = feof(parser->file->handle) != 0;

  /* After the file, there is padding, that is used by vector instructions,
   * initialise those bytes. */
  memset(parser->file->buffer.data+parser->file->buffer.length+1, 0,
    ZONE_BLOCK_SIZE);
  return 0;
}

// do not invoke directly
nonnull_all
warn_unused_result
static really_inline int32_t advance(parser_t *parser)
{
  int32_t code;

  // save embedded line count (quoted or escaped newlines)
  parser->file->newlines.tape[0] = parser->file->newlines.tail[0];
  parser->file->newlines.head = parser->file->newlines.tape;
  parser->file->newlines.tail = parser->file->newlines.tape;
  // restore non-terminated token (partial quoted or contiguous)
  parser->file->fields.tape[0] = parser->file->fields.tail[1];
  parser->file->fields.head = parser->file->fields.tape;
  parser->file->fields.tail =
    parser->file->fields.tape + (*parser->file->fields.tape[0] != '\0');
  // reset delimiters
  parser->file->delimiters.head = parser->file->delimiters.tape;
  parser->file->delimiters.tail = parser->file->delimiters.tape;

  // delayed syntax error
  if (parser->file->end_of_file == MISSING_QUOTE)
    SYNTAX_ERROR(parser, "Missing closing quote");
  if ((code = refill(parser)) < 0)
    return code;

  if (reindex(parser)) {
    // save non-terminated token
    parser->file->fields.tail[0] = parser->file->fields.tail[-1];
    parser->file->fields.tail--;
    // delay syntax error so correct line number is available
    if (parser->file->end_of_file == NO_MORE_DATA &&
       *parser->file->fields.tail[1] == '"')
      parser->file->end_of_file = MISSING_QUOTE;
  } else {
    parser->file->fields.tail[1] = end_of_file;
  }

  // FIXME: if tail is still equal to tape, refill immediately?!

  // terminate (end of buffer is null-terminated)
  parser->file->fields.tail[0] =
    parser->file->buffer.data + parser->file->buffer.length;
  parser->file->delimiters.tail[0] =
    parser->file->buffer.data + parser->file->buffer.length;
  // start-of-line must be false if start of tape is not start of buffer
  if (*parser->file->fields.head != parser->file->buffer.data)
    parser->file->start_of_line = false;
  return 0;
}

nonnull_all
warn_unused_result
static really_inline bool is_contiguous(const token_t *token)
{
  return token->code == CONTIGUOUS;
}

nonnull_all
warn_unused_result
static really_inline bool is_quoted(const token_t *token)
{
  return token->code == QUOTED;
}

nonnull_all
warn_unused_result
static really_inline bool is_contiguous_or_quoted(const token_t *token)
{
  return (token->code == CONTIGUOUS || token->code == QUOTED);
}

nonnull_all
warn_unused_result
static really_inline bool is_delimiter(const token_t *token)
{
  return (token->code == LINE_FEED || token->code == END_OF_FILE);
}

nonnull_all
warn_unused_result
static really_inline bool is_line_feed(const token_t *token)
{
  return token->code == LINE_FEED;
}

nonnull_all
warn_unused_result
static really_inline bool is_end_of_file(const token_t *token)
{
  return token->code == 0;
}


#undef SYNTAX_ERROR
#define SYNTAX_ERROR(parser, token, ...) \
  do { \
    zone_log((parser), ZONE_ERROR, __VA_ARGS__); \
    defer_error((token), ZONE_SYNTAX_ERROR); \
    return; \
  } while (0)

#define ERROR(parser, token, code) \
  do { \
    defer_error(token, code); \
    return; \
  } while (0)


nonnull_all
static never_inline void maybe_take(parser_t *parser, token_t *token)
{
  for (;;) {
    token->data = *parser->file->fields.head;
    token->code = (int32_t)classify[ (uint8_t)**parser->file->fields.head ];
    if (likely(token->code == CONTIGUOUS)) {
      assert(*parser->file->delimiters.head > *parser->file->fields.head);
      token->length = (uintptr_t)*parser->file->delimiters.head -
                      (uintptr_t)*parser->file->fields.head;
      parser->file->fields.head++;
      parser->file->delimiters.head++;
      return;
    } else if (token->code == LINE_FEED) {
      if (unlikely(token->data == line_feed))
        parser->file->span += *parser->file->newlines.head++;
      parser->file->span++;
      parser->file->fields.head++;
      if (unlikely(parser->file->grouped))
        continue;
      parser->file->start_of_line = classify[ (uint8_t)*(token->data+1) ] != BLANK;
      token->length = 1;
      return;
    } else if (token->code == QUOTED) {
      assert(*parser->file->delimiters.head > *parser->file->fields.head);
      token->data++;
      token->length = ((uintptr_t)*parser->file->delimiters.head -
                       (uintptr_t)*parser->file->fields.head) - 1;
      parser->file->fields.head++;
      parser->file->delimiters.head++;
      return;
    } else if (token->code == END_OF_FILE) {
      int32_t code;
      if (parser->file->end_of_file == NO_MORE_DATA) {
        if (parser->file->grouped)
          SYNTAX_ERROR(parser, token, "Missing closing brace");
        token->data = end_of_file;
        token->length = 1;
        return;
      } else if (unlikely((code = advance(parser)) < 0)) {
        ERROR(parser, token, code);
      }
    } else if (token->code == LEFT_PAREN) {
      if (unlikely(parser->file->grouped))
        SYNTAX_ERROR(parser, token, "Nested opening brace");
      parser->file->grouped = true;
      parser->file->fields.head++;
    } else {
      assert(token->code == RIGHT_PAREN);
      if (unlikely(!parser->file->grouped))
        SYNTAX_ERROR(parser, token, "Missing opening brace");
      parser->file->grouped = false;
      parser->file->fields.head++;
    }
  }
}

nonnull_all
static really_inline void take(parser_t *parser, token_t *token)
{
  for (;;) {
    token->data = *parser->file->fields.head;
    token->code = (int32_t)classify[ (uint8_t)**parser->file->fields.head ];
    if (likely(token->code == CONTIGUOUS)) {
      assert(*parser->file->delimiters.head > *parser->file->fields.head);
      token->length = (uintptr_t)*parser->file->delimiters.head -
                      (uintptr_t)*parser->file->fields.head;
      parser->file->fields.head++;
      parser->file->delimiters.head++;
      return;
    } else if (token->code == LINE_FEED) {
      if (unlikely(token->data == line_feed))
        parser->file->span += *parser->file->newlines.head++;
      parser->file->span++;
      parser->file->fields.head++;
      if (unlikely(parser->file->grouped))
        continue;
      parser->file->start_of_line = classify[ (uint8_t)*(token->data+1) ] != BLANK;
      token->length = 1;
      return;
    } else if (token->code == QUOTED) {
      assert(*parser->file->delimiters.head > *parser->file->fields.head);
      token->data++;
      token->length = ((uintptr_t)*parser->file->delimiters.head -
                       (uintptr_t)*parser->file->fields.head) - 1;
      parser->file->fields.head++;
      parser->file->delimiters.head++;
      return;
    } else {
      maybe_take(parser, token);
      return;
    }
  }
}

#undef SYNTAX_ERROR
#undef ERROR

// token sequence is predictable. fields typically require a specific type,
// except names, strings and SvcParams. even then, names are typically not
// quoted and strings (or text) are typically quoted. implement specialized
// tape accessors for performance and reduction in binary size.

#define SYNTAX_ERROR(parser, token, ...) \
  do { \
    zone_log((parser), ZONE_ERROR, __VA_ARGS__); \
    defer_error((token), ZONE_SYNTAX_ERROR); \
    return ZONE_SYNTAX_ERROR; \
  } while (0)

#define ERROR(parser, token, code) \
  do { \
    defer_error((token), (code)); \
    return (code); \
  } while (0)

nonnull_all
warn_unused_result
static never_inline int32_t dont_have_contiguous(
  parser_t *parser,
  const type_info_t *type,
  const rdata_info_t *field,
  token_t *token)
{
  if (token->code < 0)
    return token->code;
  assert(token->code != CONTIGUOUS);
  if (token->code == QUOTED)
    SYNTAX_ERROR(parser, token, "Invalid %s in %s", NAME(field), NAME(type));
  assert(token->code == END_OF_FILE || token->code == LINE_FEED);
  SYNTAX_ERROR(parser, token, "Missing %s in %s", NAME(field), NAME(type));
}

nonnull_all
warn_unused_result
static really_inline int32_t have_contiguous(
  parser_t *parser,
  const type_info_t *type,
  const rdata_info_t *field,
  token_t *token)
{
  if (unlikely(token->code != CONTIGUOUS))
    return dont_have_contiguous(parser, type, field, token);
  return 0;
}

nonnull_all
warn_unused_result
static never_inline int32_t maybe_take_contiguous(
  parser_t *parser,
  const type_info_t *type,
  const rdata_info_t *field,
  token_t *token)
{
  int32_t code;

  assert(token->code != CONTIGUOUS);

  for (;;) {
    if (likely(token->code == CONTIGUOUS)) {
      assert(*parser->file->delimiters.head > *parser->file->fields.head);
      token->length = (uintptr_t)*parser->file->delimiters.head -
                      (uintptr_t)*parser->file->fields.head;
      parser->file->fields.head++;
      parser->file->delimiters.head++;
      return 0;
    } else if (token->code == END_OF_FILE) {
      if (parser->file->end_of_file == NO_MORE_DATA)
        SYNTAX_ERROR(parser, token, "Missing %s in %s", NAME(field), NAME(type));
      if ((code = advance(parser)) < 0)
        ERROR(parser, token, code);
    } else if (token->code == QUOTED) {
      SYNTAX_ERROR(parser, token, "Invalid %s in %s", NAME(field), NAME(type));
    } else if (token->code == LEFT_PAREN) {
      if (parser->file->grouped)
        SYNTAX_ERROR(parser, token, "Nested opening brace");
      parser->file->grouped = true;
      parser->file->fields.head++;
    } else if (token->code == RIGHT_PAREN) {
      if (!parser->file->grouped)
        SYNTAX_ERROR(parser, token, "Missing opening brace");
      parser->file->grouped = false;
      parser->file->fields.head++;
    } else if (token->code == LINE_FEED) {
      if (token->data == line_feed)
        parser->file->span += *parser->file->newlines.head++;
      parser->file->span++;
      if (!parser->file->grouped)
        SYNTAX_ERROR(parser, token, "Missing %s in %s", NAME(field), NAME(type));
      parser->file->fields.head++;
    }
    token->data = *parser->file->fields.head;
    token->code = (int32_t)classify[ (uint8_t)**parser->file->fields.head ];
  }
}

nonnull_all
warn_unused_result
static really_inline int32_t take_contiguous(
  parser_t *parser,
  const type_info_t *type,
  const rdata_info_t *field,
  token_t *token)
{
  token->data = *parser->file->fields.head;
  token->code = (int32_t)classify[ (uint8_t)**parser->file->fields.head ];
  if (unlikely(token->code != CONTIGUOUS))
    return maybe_take_contiguous(parser, type, field, token);
  assert(*parser->file->delimiters.head > *parser->file->fields.head);
  token->length = (uintptr_t)*parser->file->delimiters.head -
                  (uintptr_t)*parser->file->fields.head;
  parser->file->fields.head++;
  parser->file->delimiters.head++;
  return 0;
}

nonnull_all
warn_unused_result
static never_inline int32_t dont_have_quoted(
  parser_t *parser,
  const type_info_t *type,
  const rdata_info_t *field,
  token_t *token)
{
  if (token->code < 0)
    return token->code;
  assert(token->code != QUOTED);
  if (token->code == CONTIGUOUS)
    SYNTAX_ERROR(parser, token, "Invalid %s in %s", NAME(field), NAME(type));
  assert(token->code == END_OF_FILE || token->code == LINE_FEED);
  SYNTAX_ERROR(parser, token, "Missing %s in %s", NAME(field), NAME(type));
}

nonnull_all
warn_unused_result
static really_inline int32_t have_quoted(
  parser_t *parser,
  const type_info_t *type,
  const rdata_info_t *field,
  token_t *token)
{
  if (unlikely(token->code != QUOTED))
    return dont_have_quoted(parser, type, field, token);
  return 0;
}

nonnull_all
warn_unused_result
static never_inline int32_t maybe_take_quoted(
  parser_t *parser,
  const type_info_t *type,
  const rdata_info_t *field,
  token_t *token)
{
  int32_t code;

  assert(token->code != QUOTED);

  for (;;) {
    if (likely(token->code == QUOTED)) {
      assert(*parser->file->delimiters.head > *parser->file->fields.head);
      token->data++;
      token->length = ((uintptr_t)*parser->file->delimiters.head -
                       (uintptr_t)*parser->file->fields.head) - 1;
      parser->file->fields.head++;
      parser->file->delimiters.head++;
      return 0;
    } else if (token->code == END_OF_FILE) {
      if (parser->file->end_of_file == NO_MORE_DATA)
        SYNTAX_ERROR(parser, token, "Missing %s in %s", NAME(field), NAME(type));
      if ((code = advance(parser)) < 0)
        ERROR(parser, token, code);
    } else if (token->code == CONTIGUOUS) {
      SYNTAX_ERROR(parser, token, "Invalid %s in %s", NAME(field), NAME(type));
    } else if (token->code == LEFT_PAREN) {
      if (parser->file->grouped)
        SYNTAX_ERROR(parser, token, "Nested opening brace");
      parser->file->grouped = true;
      parser->file->fields.head++;
    } else if (token->code == RIGHT_PAREN) {
      if (!parser->file->grouped)
        SYNTAX_ERROR(parser, token, "Missing opening brace");
      parser->file->grouped = false;
      parser->file->fields.head++;
    } else if (token->code == LINE_FEED) {
      if (token->data == line_feed)
        parser->file->span += *parser->file->newlines.head++;
      parser->file->span++;
      if (!parser->file->grouped)
        SYNTAX_ERROR(parser, token, "Missing %s in %s", NAME(field), NAME(type));
      parser->file->fields.head++;
    } else {
      assert(token->code < 0);
      return token->code;
    }
    token->data = *parser->file->fields.head;
    token->code = (int32_t)classify[ (uint8_t)**parser->file->fields.head ];
  }
}

nonnull_all
warn_unused_result
static really_inline int32_t take_quoted(
  parser_t *parser,
  const type_info_t *type,
  const rdata_info_t *field,
  token_t *token)
{
  token->data = *parser->file->fields.head;
  token->code = (int32_t)classify[ (uint8_t)**parser->file->fields.head ];
  if (unlikely((token->code != QUOTED)))
    return maybe_take_quoted(parser, type, field, token);
  assert(*parser->file->delimiters.head > *parser->file->fields.head);
  token->data++;
  token->length = ((uintptr_t)*parser->file->delimiters.head -
                   (uintptr_t)*parser->file->fields.head) - 1;
  parser->file->fields.head++;
  parser->file->delimiters.head++;
  return 0;
}

nonnull_all
warn_unused_result
static never_inline int32_t dont_have_contiguous_or_quoted(
  parser_t *parser,
  const type_info_t *type,
  const rdata_info_t *field,
  token_t *token)
{
  if (token->code == QUOTED || token->code < 0)
    return token->code;
  assert(token->code == END_OF_FILE || token->code == LINE_FEED);
  SYNTAX_ERROR(parser, token, "Missing %s in %s", NAME(field), NAME(type));
}

nonnull_all
warn_unused_result
static really_inline int32_t have_contiguous_or_quoted(
  parser_t *parser,
  const type_info_t *type,
  const rdata_info_t *field,
  token_t *token)
{
  if (unlikely(token->code != CONTIGUOUS))
    return dont_have_contiguous_or_quoted(parser, type, field, token);
  return 0;
}

nonnull_all
warn_unused_result
static never_inline int32_t maybe_take_contiguous_or_quoted(
  parser_t *parser,
  const type_info_t *type,
  const rdata_info_t *field,
  token_t *token)
{
  int32_t code;

  for (;;) {
    if (likely(token->code == CONTIGUOUS)) {
      assert(*parser->file->delimiters.head > *parser->file->fields.head);
      token->length = (uintptr_t)*parser->file->delimiters.head -
                      (uintptr_t)*parser->file->fields.head;
      parser->file->fields.head++;
      parser->file->delimiters.head++;
      return 0;
    } else if (token->code == QUOTED) {
      assert(*parser->file->delimiters.head > *parser->file->fields.head);
      token->data++;
      token->length = ((uintptr_t)*parser->file->delimiters.head -
                       (uintptr_t)*parser->file->fields.head) - 1;
      parser->file->fields.head++;
      parser->file->delimiters.head++;
      return 0;
    } else if (token->code == END_OF_FILE) {
      if (parser->file->end_of_file == NO_MORE_DATA)
        SYNTAX_ERROR(parser, token, "Missing %s in %s", NAME(field), NAME(type));
      if ((code = advance(parser)) < 0)
        ERROR(parser, token, code);
    } else if (token->code == LEFT_PAREN) {
      if (parser->file->grouped)
        SYNTAX_ERROR(parser, token, "Nested opening brace");
      parser->file->grouped = true;
      parser->file->fields.head++;
    } else if (token->code == RIGHT_PAREN) {
      if (!parser->file->grouped)
        SYNTAX_ERROR(parser, token, "Missing opening brace");
      parser->file->grouped = false;
      parser->file->fields.head++;
    } else if (token->code == LINE_FEED) {
      if (token->data == line_feed)
        parser->file->span += *parser->file->newlines.head++;
      parser->file->span++;
      if (!parser->file->grouped)
        SYNTAX_ERROR(parser, token, "Missing %s in %s", NAME(field), NAME(type));
      parser->file->fields.head++;
    }
    token->data = *parser->file->fields.head;
    token->code = (int32_t)classify[ (uint8_t)**parser->file->fields.head ];
  }
}

nonnull_all
warn_unused_result
static really_inline int32_t take_contiguous_or_quoted(
  parser_t *parser,
  const type_info_t *type,
  const rdata_info_t *field,
  token_t *token)
{
  token->data = *parser->file->fields.head;
  token->code = (int32_t)classify[ (uint8_t)**parser->file->fields.head ];
  if (likely(token->code == CONTIGUOUS)) {
    assert(*parser->file->delimiters.head > *parser->file->fields.head);
    token->length = (uintptr_t)*parser->file->delimiters.head -
                    (uintptr_t)*parser->file->fields.head;
    parser->file->fields.head++;
    parser->file->delimiters.head++;
    return 0;
  } else {
    return maybe_take_contiguous_or_quoted(parser, type, field, token);
  }
}

nonnull_all
warn_unused_result
static really_inline int32_t take_quoted_or_contiguous(
  parser_t *parser,
  const type_info_t *type,
  const rdata_info_t *field,
  token_t *token)
{
  token->data = *parser->file->fields.head;
  token->code = (int32_t)classify[ (uint8_t)**parser->file->fields.head ];
  if (likely(token->code == QUOTED)) {
    assert(*parser->file->delimiters.head > *parser->file->fields.head);
    token->data++;
    token->length = ((uintptr_t)*parser->file->delimiters.head -
                     (uintptr_t)*parser->file->fields.head) - 1;
    parser->file->fields.head++;
    parser->file->delimiters.head++;
    return 0;
  } else {
    return maybe_take_contiguous_or_quoted(parser, type, field, token);
  }
}

diagnostic_push()
clang_diagnostic_ignored(unused-function)
gcc_diagnostic_ignored(unused-function)

nonnull_all
warn_unused_result
static never_inline int32_t dont_have_delimiter(
  parser_t *parser, const type_info_t *type, token_t *token)
{
  if (token->code == END_OF_FILE || token->code < 0)
    return token->code;
  assert(token->code == CONTIGUOUS || token->code == QUOTED);
  SYNTAX_ERROR(parser, token, "Trailing data in %s", NAME(type));
}

nonnull_all
warn_unused_result
static never_inline int32_t have_delimiter(
  parser_t *parser, const type_info_t *type, token_t *token)
{
  if (unlikely(token->code != LINE_FEED))
    return dont_have_delimiter(parser, type, token);
  return 0;
}

diagnostic_pop()

nonnull_all
warn_unused_result
static never_inline int32_t maybe_take_delimiter(
  parser_t *parser, const type_info_t *type, token_t *token)
{
  int32_t code;

  for (;;) {
    if (likely(token->code == LINE_FEED)) {
      if (unlikely(token->data == line_feed))
        parser->file->span += *parser->file->newlines.head++;
      if (unlikely(parser->file->grouped)) {
        parser->file->span++;
        parser->file->fields.head++;
      } else {
        token->length = 1;
        parser->file->span++;
        parser->file->start_of_line = classify[ (uint8_t)*(token->data+1) ] != BLANK;
        parser->file->fields.head++;
        return 0;
      }
    } else if (token->code == END_OF_FILE) {
      if (parser->file->end_of_file == NO_MORE_DATA) {
        if (parser->file->grouped)
          SYNTAX_ERROR(parser, token, "Missing closing brace");
        token->data = end_of_file;
        token->length = 1;
        return 0;
      }

      if ((code = advance(parser)) < 0)
        ERROR(parser, token, code);
    } else if (token->code == LEFT_PAREN) {
      if (parser->file->grouped)
        SYNTAX_ERROR(parser, token, "Nested opening brace");
      parser->file->grouped = true;
      parser->file->fields.head++;
    } else if (token->code == RIGHT_PAREN) {
      if (!parser->file->grouped)
        SYNTAX_ERROR(parser, token, "Missing opening brace");
      parser->file->grouped = false;
      parser->file->fields.head++;
    } else {
      assert(token->code == CONTIGUOUS || token->code == QUOTED);
      SYNTAX_ERROR(parser, token, "Trailing data in %s", NAME(type));
    }
    token->data = *parser->file->fields.head;
    token->code = (int32_t)classify[ (uint8_t)**parser->file->fields.head ];
  }
}

nonnull_all
warn_unused_result
static really_inline int32_t take_delimiter(
  parser_t *parser, const type_info_t *type, token_t *token)
{
  token->data = *parser->file->fields.head;
  token->code = (int32_t)classify[ (uint8_t)**parser->file->fields.head ];
  if (likely(token->code == LINE_FEED)) {
    if (unlikely(parser->file->grouped || token->data == line_feed))
      return maybe_take_delimiter(parser, type, token);
    token->length = 1;
    parser->file->span++;
    parser->file->start_of_line = classify[ (uint8_t)*(*parser->file->fields.head+1) ] != BLANK;
    parser->file->fields.head++;
    return 0;
  } else {
    return maybe_take_delimiter(parser, type, token);
  }
}

#undef SYNTAX_ERROR
#undef ERROR

// define SYNTAX_ERROR for the rest of the code base
#define SYNTAX_ERROR(parser, ...) \
  RAISE_ERROR((parser), ZONE_SYNTAX_ERROR, __VA_ARGS__)

#endif // PARSER_H
