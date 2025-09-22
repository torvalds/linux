/*
 * types.h
 *
 * Copyright (c) 2023, NLnet Labs. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */
#ifndef TYPES_H
#define TYPES_H

nonnull_all
static really_inline int32_t scan_type_or_class(
  const char *data,
  size_t length,
  uint16_t *code,
  const mnemonic_t **mnemonic);

nonnull_all
static really_inline int32_t parse_type(
  parser_t *parser,
  const type_info_t *type,
  const rdata_info_t *field,
  rdata_t *rdata,
  const token_t *token);

nonnull_all
static really_inline int32_t parse_name(
  parser_t *parser,
  const type_info_t *type,
  const rdata_info_t *field,
  rdata_t *rdata,
  const token_t *token);

nonnull_all
static really_inline int32_t parse_string(
  parser_t *parser,
  const type_info_t *type,
  const rdata_info_t *field,
  rdata_t *rdata,
  const token_t *token);

nonnull_all
static really_inline int32_t parse_text(
  parser_t *parser,
  const type_info_t *type,
  const rdata_info_t *field,
  rdata_t *rdata,
  const token_t *token);

#define FIELDS(fields) \
  { (sizeof(fields)/sizeof(fields[0])), fields }

#define FIELD(name) \
  { { { name, sizeof(name) - 1 } } }

#define CLASS(name, code) \
  { { { name, sizeof(name) - 1 }, code } }

#define UNKNOWN_CLASS(code) \
  { { { "", 0 }, code } }

#define TYPE(name, code, _class, fields, check, parse) \
  { { { name, sizeof(name) - 1 }, code }, _class, false, false, fields, check, parse }

#define UNKNOWN_TYPE(code) \
  { { { "", 0 }, code }, 0, false, false, { 0, NULL }, check_generic_rr, parse_unknown_rdata }

nonnull((1,2,3,4))
static really_inline int32_t check_bytes(
  parser_t *parser,
  const type_info_t *type,
  const rdata_info_t *field,
  const uint8_t *data,
  const size_t length,
  const size_t size)
{
  (void)data;
  if (length < size)
    SYNTAX_ERROR(parser, "Missing %s in %s", NAME(field), NAME(type));
  return (int32_t)size;
}

#define check_int8(...) check_bytes(__VA_ARGS__, sizeof(uint8_t))

#define check_int16(...) check_bytes(__VA_ARGS__, sizeof(uint16_t))

#define check_int32(...) check_bytes(__VA_ARGS__, sizeof(uint32_t))

#define check_int64(...) check_bytes(__VA_ARGS__, sizeof(uint64_t))

#define check_ip4(...) check_bytes(__VA_ARGS__, 4)

#define check_ip6(...) check_bytes(__VA_ARGS__, 16)

#define check_ilnp64(...) check_bytes(__VA_ARGS__, sizeof(uint64_t))

nonnull((1,2,3,4))
static really_inline int32_t check_ttl(
  parser_t *parser,
  const type_info_t *type,
  const rdata_info_t *field,
  const uint8_t *data,
  const size_t length)
{
  uint32_t number;

  if (length < sizeof(number))
    SYNTAX_ERROR(parser, "Missing %s in %s", NAME(field), NAME(type));

  memcpy(&number, data, sizeof(number));
  number = be32toh(number);

  if (number > INT32_MAX)
    SEMANTIC_ERROR(parser, "Invalid %s in %s", NAME(field), NAME(type));

  return 4;
}

zone_nonnull((1,2,3,4))
static really_inline int32_t check_name(
  parser_t *parser,
  const type_info_t *type,
  const rdata_info_t *field,
  const uint8_t *data,
  const size_t length)
{
  int32_t label = 0, count = 0;
  while (count < (int32_t)length) {
    label = data[count];
    count += 1 + label;
    if (!label)
      break;
  }

  if (!count || count > (int32_t)length)
    SYNTAX_ERROR(parser, "Invalid %s in %s", NAME(field), NAME(type));

  return count;
}

zone_nonnull((1,2,3,4))
static really_inline int32_t check_string(
  parser_t *parser,
  const type_info_t *type,
  const rdata_info_t *field,
  const uint8_t *data,
  const size_t length)
{
  int32_t count;

  if (!length || (count = 1 + (int32_t)data[0]) > (int32_t)length)
    SYNTAX_ERROR(parser, "Invalid %s in %s", NAME(field), NAME(type));

  return count;
}

zone_nonnull((1,2,3,4))
static really_inline int32_t check_nsec(
  parser_t *parser,
  const type_info_t *type,
  const rdata_info_t *field,
  const uint8_t *data,
  const size_t length)
{
  int32_t count = 0;
  int32_t last_window = -1;

  while ((count + 2) < (int32_t)length) {
    const int32_t window = (int32_t)data[0];
    const int32_t blocks = (int32_t)data[1];
    if (window <= last_window)
      SYNTAX_ERROR(parser, "Invalid %s in %s, windows are out-of-order",
                   NAME(field), NAME(type));
    if (!blocks || blocks > 32)
      SYNTAX_ERROR(parser, "Invalid %s in %s, blocks are out-of-bounds",
                   NAME(field), NAME(type));
    count += 2 + blocks;
    last_window = window;
  }

  if (count != (int32_t)length)
    SYNTAX_ERROR(parser, "Invalid %s in %s", NAME(field), NAME(type));

  return count;
}

zone_nonnull((1))
static really_inline int32_t check(size_t *length, int32_t count)
{
  if (count < 0)
    return count;
  *length += (size_t)count;
  return 0;
}

nonnull_all
static really_inline void adjust_line_count(file_t *file)
{
  file->line += file->span;
  file->span = 0;
}

nonnull_all
static really_inline int32_t accept_rr(
  parser_t *parser, const type_info_t *type, const rdata_t *rdata)
{
  (void)type;

  assert(rdata->octets <= rdata->limit);
  assert(rdata->octets >= parser->rdata->octets);
  size_t length = (uintptr_t)rdata->octets - (uintptr_t)parser->rdata->octets;

  assert(length <= UINT16_MAX);
  assert(parser->owner->length <= UINT8_MAX);
  int32_t code = parser->options.accept.callback(
    parser,
    &(zone_name_t){ (uint8_t)parser->owner->length, parser->owner->octets },
    parser->file->last_type,
    parser->file->last_class,
    *parser->file->ttl,
    (uint16_t)length,
    parser->rdata->octets,
    parser->user_data);

  adjust_line_count(parser->file);
  return code;
}

nonnull_all
static int32_t check_a_rr(
  parser_t *parser, const type_info_t *type, const rdata_t *rdata)
{
  assert(rdata->octets >= parser->rdata->octets);
  if ((uintptr_t)rdata->octets - (uintptr_t)parser->rdata->octets == 4)
    return accept_rr(parser, type, rdata);
  SYNTAX_ERROR(parser, "Invalid %s", NAME(type));
}

nonnull_all
static int32_t parse_a_rdata(
  parser_t *parser, const type_info_t *type, rdata_t *rdata, token_t *token)
{
  int32_t code;
  const rdata_info_t *fields = type->rdata.fields;

  if ((code = have_contiguous(parser, type, &fields[0], token)) < 0)
    return code;
  if ((code = parse_ip4(parser, type, &fields[0], rdata, token)) < 0)
    return code;
  if ((code = take_delimiter(parser, type, token)) < 0)
    return code;
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t check_ns_rr(
  parser_t *parser, const type_info_t *type, const rdata_t *rdata)
{
  int32_t r;
  size_t c = 0;
  const size_t n = (uintptr_t)rdata->octets - (uintptr_t)parser->rdata->octets;
  const uint8_t *o = parser->rdata->octets;
  const rdata_info_t *f = type->rdata.fields;

  if ((r = check(&c, check_name(parser, type, &f[0], o, n))) < 0)
    return r;

  if (c != n)
    SYNTAX_ERROR(parser, "Invalid %s", NAME(type));
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t parse_ns_rdata(
  parser_t *parser, const type_info_t *type, rdata_t *rdata, token_t *token)
{
  int32_t code;
  const rdata_info_t *fields = type->rdata.fields;

  if ((code = have_contiguous(parser, type, &fields[0], token)) < 0)
    return code;
  if ((code = parse_name(parser, type, &fields[0], rdata, token)) < 0)
    return code;
  if ((code = take_delimiter(parser, type, token)) < 0)
    return code;
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t check_soa_rr(
  parser_t *parser, const type_info_t *type, const rdata_t *rdata)
{
  int32_t r;
  size_t c = 0;
  const size_t n = (uintptr_t)rdata->octets - (uintptr_t)parser->rdata->octets;
  const uint8_t *o = parser->rdata->octets;
  const rdata_info_t *f = type->rdata.fields;

  if ((r = check(&c, check_name(parser, type, &f[0], o, n))) ||
      (r = check(&c, check_name(parser, type, &f[1], o+c, n-c))) ||
      (r = check(&c, check_int32(parser, type, &f[2], o+c, n-c))) ||
      (r = check(&c, check_ttl(parser, type, &f[3], o+c, n-c))) ||
      (r = check(&c, check_ttl(parser, type, &f[4], o+c, n-c))) ||
      (r = check(&c, check_ttl(parser, type, &f[5], o+c, n-c))) ||
      (r = check(&c, check_ttl(parser, type, &f[6], o+c, n-c))))
    return r;

  if (c != n)
    SYNTAX_ERROR(parser, "Invalid %s", NAME(type));
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t parse_soa_rdata(
  parser_t *parser, const type_info_t *type, rdata_t *rdata, token_t *token)
{
  int32_t code;
  const rdata_info_t *fields = type->rdata.fields;

  if ((code = have_contiguous(parser, type, &fields[0], token)) < 0)
    return code;
  if ((code = parse_name(parser, type, &fields[0], rdata, token)) < 0)
    return code;
  if ((code = take_contiguous(parser, type, &fields[1], token)) < 0)
    return code;
  if ((code = parse_name(parser, type, &fields[1], rdata, token)) < 0)
    return code;
  if ((code = take_contiguous(parser, type, &fields[2], token)) < 0)
    return code;
  if ((code = parse_int32(parser, type, &fields[2], rdata, token)) < 0)
    return code;
  if ((code = take_contiguous(parser, type, &fields[3], token)) < 0)
    return code;
  if ((code = parse_ttl(parser, type, &fields[3], rdata, token)) < 0)
    return code;
  if ((code = take_contiguous(parser, type, &fields[4], token)) < 0)
    return code;
  if ((code = parse_ttl(parser, type, &fields[4], rdata, token)) < 0)
    return code;
  if ((code = take_contiguous(parser, type, &fields[5], token)) < 0)
    return code;
  if ((code = parse_ttl(parser, type, &fields[5], rdata, token)) < 0)
    return code;
  if ((code = take_contiguous(parser, type, &fields[6], token)) < 0)
    return code;
  if ((code = parse_ttl(parser, type, &fields[6], rdata, token)) < 0)
    return code;
  if ((code = take_delimiter(parser, type, token)) < 0)
    return code;
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t check_wks_rr(
  parser_t *parser, const type_info_t *type, const rdata_t *rdata)
{
  int32_t r;
  size_t c = 0;
  const size_t n = (uintptr_t)rdata->octets - (uintptr_t)parser->rdata->octets;
  const uint8_t *o = parser->rdata->octets;
  const rdata_info_t *f = type->rdata.fields;

  if ((r = check(&c, check_ip4(parser, type, &f[0], o, n))) ||
      (r = check(&c, check_int8(parser, type, &f[0], o+c, n-c))))
    return r;

  // any bit may, or may not, be set. confirm the bitmap does not exceed the
  // maximum number of ports
  if (n > 8192 + 5)
    SYNTAX_ERROR(parser, "Invalid %s", NAME(type));

  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t parse_wks_rdata(
  parser_t *parser, const type_info_t *type, rdata_t *rdata, token_t *token)
{
  int32_t code;
  const rdata_info_t *fields = type->rdata.fields;

  if ((code = have_contiguous(parser, type, &fields[0], token)) < 0)
    return code;
  if ((code = parse_ip4(parser, type, &fields[0], rdata, token) < 0))
    return code;

  if ((code = take_contiguous(parser, type, &fields[1], token)) < 0)
    return code;

  uint8_t protocol;
  if (!scan_protocol(token->data, token->length, &protocol))
    SYNTAX_ERROR(parser, "Invalid %s in %s", NAME(&fields[1]), NAME(type));

  *rdata->octets++ = protocol;
  uint8_t *bitmap = rdata->octets;
  int32_t highest_port = -1;

  take(parser, token);
  while (is_contiguous(token)) {
    uint16_t port;
    if (!scan_service(token->data, token->length, protocol, &port))
      SYNTAX_ERROR(parser, "Invalid %s in %s", NAME(&type->rdata.fields[2]), NAME(type));

    if (port > highest_port) {
      // ensure newly used octets are zeroed out before use
      size_t offset = highest_port < 0 ? 0 : (size_t)highest_port / 8 + 1;
      size_t length = (size_t)port / 8 + 1;
      memset(bitmap + offset, 0, length - offset);
      highest_port = port;
    }

    // bits are counted from left to right, so bit 0 is the left most bit
    bitmap[port / 8] |= (1 << (7 - port % 8));
    take(parser, token);
  }

  rdata->octets += (size_t)highest_port / 8 + 1;

  if (have_delimiter(parser, type, token) < 0)
    return token->code;
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t check_hinfo_rr(
  parser_t *parser, const type_info_t *type, const rdata_t *rdata)
{
  int32_t r;
  size_t c = 0;
  const size_t n = (uintptr_t)rdata->octets - (uintptr_t)parser->rdata->octets;
  const uint8_t *o = parser->rdata->octets;
  const rdata_info_t *f = type->rdata.fields;

  if ((r = check(&c, check_string(parser, type, &f[0], o, n))) ||
      (r = check(&c, check_string(parser, type, &f[1], o+c, n-c))))
    return r;

  if (c != n)
    SYNTAX_ERROR(parser, "Invalid %s", NAME(type));
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t parse_hinfo_rdata(
  parser_t *parser, const type_info_t *type, rdata_t *rdata, token_t *token)
{
  int32_t code;
  const rdata_info_t *fields = type->rdata.fields;

  if ((code = have_contiguous_or_quoted(parser, type, &fields[0], token)) < 0)
    return code;
  if ((code = parse_string(parser, type, &fields[0], rdata, token)) < 0)
    return code;
  if ((code = take_quoted_or_contiguous(parser, type, &fields[1], token)) < 0)
    return code;
  if ((code = parse_string(parser, type, &fields[1], rdata, token)) < 0)
    return code;
  if ((code = take_delimiter(parser, type, token)) < 0)
    return code;
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t check_minfo_rr(
  parser_t *parser, const type_info_t *type, const rdata_t *rdata)
{
  int32_t r;
  size_t c = 0;
  const size_t n = (uintptr_t)rdata->octets - (uintptr_t)parser->rdata->octets;
  const uint8_t *o = parser->rdata->octets;
  const rdata_info_t *f = type->rdata.fields;

  if ((r = check(&c, check_name(parser, type, &f[0], o, n))) ||
      (r = check(&c, check_name(parser, type, &f[1], o+c, n-c))))
    return r;

  if (c != n)
    SYNTAX_ERROR(parser, "Invalid %s", NAME(type));
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t parse_minfo_rdata(
  parser_t *parser, const type_info_t *type, rdata_t *rdata, token_t *token)
{
  int32_t code;
  const rdata_info_t *fields = type->rdata.fields;

  if ((code = have_contiguous(parser, type, &fields[0], token)) < 0)
    return code;
  if ((code = parse_name(parser, type, &fields[0], rdata, token)) < 0)
    return code;
  if ((code = take_contiguous(parser, type, &fields[1], token)) < 0)
    return code;
  if ((code = parse_name(parser, type, &fields[1], rdata, token)) < 0)
    return code;
  if ((code = take_delimiter(parser, type, token)) < 0)
    return code;
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t check_mx_rr(
  parser_t *parser, const type_info_t *type, const rdata_t *rdata)
{
  int32_t r;
  size_t c = 0;
  const size_t n = (uintptr_t)rdata->octets - (uintptr_t)parser->rdata->octets;
  const uint8_t *o = parser->rdata->octets;
  const rdata_info_t *f = type->rdata.fields;

  if ((r = check(&c, check_int16(parser, type, &f[0], o, n))) ||
      (r = check(&c, check_name(parser, type, &f[1], o+c, n-c))))
    return r;

  if (c != n)
    SYNTAX_ERROR(parser, "Invalid %s", NAME(type));
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t parse_mx_rdata(
  parser_t *parser, const type_info_t *type, rdata_t *rdata, token_t *token)
{
  int32_t code;
  const rdata_info_t *fields = type->rdata.fields;

  if ((code = have_contiguous(parser, type, &fields[0], token)) < 0)
    return code;
  if ((code = parse_int16(parser, type, &fields[0], rdata, token)) < 0)
    return code;
  if ((code = take_contiguous(parser, type, &fields[1], token)) < 0)
    return code;
  if ((code = parse_name(parser, type, &fields[1], rdata, token)) < 0)
    return code;
  if ((code = take_delimiter(parser, type, token)) < 0)
    return code;
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t check_txt_rr(
  parser_t *parser, const type_info_t *type, const rdata_t *rdata)
{
  int32_t r;
  size_t c = 0;
  const size_t n = (uintptr_t)rdata->octets - (uintptr_t)parser->rdata->octets;
  const uint8_t *o = parser->rdata->octets;
  const rdata_info_t *f = type->rdata.fields;

  if ((r = check(&c, check_string(parser, type, &f[0], o, n))))
    return r;

  while (c < n)
    if ((r = check(&c, check_string(parser, type, &f[0], o+c, n-c))))
      return r;

  if (c != n)
    SYNTAX_ERROR(parser, "Invalid %s", NAME(type));
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t parse_txt_rdata(
  parser_t *parser, const type_info_t *type, rdata_t *rdata, token_t *token)
{
  int32_t code;
  const rdata_info_t *fields = type->rdata.fields;

  while (is_contiguous_or_quoted(token)) {
    if ((code = parse_string(parser, type, &fields[0], rdata, token)) < 0)
      return code;
    take(parser, token);
  }

  if ((code = have_delimiter(parser, type, token)) < 0)
    return code;
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t check_x25_rr(
  parser_t *parser, const type_info_t *type, const rdata_t *rdata)
{
  int32_t r;
  size_t c = 0;
  const size_t n = (uintptr_t)rdata->octets - (uintptr_t)parser->rdata->octets;
  const uint8_t *o = parser->rdata->octets;
  const rdata_info_t *f = type->rdata.fields;

  if ((r = check(&c, check_string(parser, type, &f[0], o, n))))
    return r;

  if (c != n)
    SYNTAX_ERROR(parser, "Invalid %s", NAME(type));
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t parse_x25_rdata(
  parser_t *parser, const type_info_t *type, rdata_t *rdata, token_t *token)
{
  int32_t code;
  const rdata_info_t *fields = type->rdata.fields;

  if ((code = have_contiguous_or_quoted(parser, type, &fields[0], token)) < 0)
    return code;
  if ((code = parse_string(parser, type, &fields[0], rdata, token)) < 0)
    return code;
  if ((code = take_delimiter(parser, type, token)) < 0)
    return code;
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t check_isdn_rr(
  parser_t *parser, const type_info_t *type, const rdata_t *rdata)
{
  int32_t r;
  size_t c = 0;
  const size_t n = (uintptr_t)rdata->octets - (uintptr_t)parser->rdata->octets;
  const uint8_t *o = parser->rdata->octets;
  const rdata_info_t *f = type->rdata.fields;

  if ((r = check(&c, check_string(parser, type, &f[0], o, n))))
    return r;
  // subaddress is optional
  if (c < n && (r = check(&c, check_string(parser, type, &f[1], o+c, n-c))))
    return r;

  if (c != n)
    SYNTAX_ERROR(parser, "Invalid %s", NAME(type));
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t parse_isdn_rdata(
  parser_t *parser, const type_info_t *type, rdata_t *rdata, token_t *token)
{
  int32_t code;
  const rdata_info_t *fields = type->rdata.fields;

  if ((code = have_contiguous_or_quoted(parser, type, &fields[0], token)) < 0)
    return code;
  if ((code = parse_string(parser, type, &fields[0], rdata, token)) < 0)
    return code;

  // subaddress is optional
  take(parser, token);
  if (is_contiguous_or_quoted(token)) {
    if ((code = parse_string(parser, type, &fields[1], rdata, token)) < 0)
      return code;
    take(parser, token);
  }

  if ((code = have_delimiter(parser, type, token)) < 0)
    return code;
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t check_rt_rr(
  parser_t *parser, const type_info_t *type, const rdata_t *rdata)
{
  int32_t r;
  size_t c = 0;
  const size_t n = (uintptr_t)rdata->octets - (uintptr_t)parser->rdata->octets;
  const uint8_t *o = parser->rdata->octets;
  const rdata_info_t *f = type->rdata.fields;

  if ((r = check(&c, check_int16(parser, type, &f[0], o, n))) ||
      (r = check(&c, check_name(parser, type, &f[1], o+c, n-c))))
    return r;

  if (c != n)
    SYNTAX_ERROR(parser, "Invalid %s", NAME(type));
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t parse_rt_rdata(
  parser_t *parser, const type_info_t *type, rdata_t *rdata, token_t *token)
{
  int32_t code;
  const rdata_info_t *fields = type->rdata.fields;

  if ((code = have_contiguous(parser, type, &fields[0], token)) < 0)
    return code;
  if ((code = parse_int16(parser, type, &fields[0], rdata, token)) < 0)
    return code;
  if ((code = take_contiguous(parser, type, &fields[1], token)) < 0)
    return code;
  if ((code = parse_name(parser, type, &fields[1], rdata, token)) < 0)
    return code;
  if ((code = take_delimiter(parser, type, token)) < 0)
    return code;
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t check_nsap_rr(
  parser_t *parser, const type_info_t *type, const rdata_t *rdata)
{
  if (rdata->octets == parser->rdata->octets)
    SYNTAX_ERROR(parser, "Invalid %s", NAME(type));
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t parse_nsap_rdata(
  parser_t *parser, const type_info_t *type, rdata_t *rdata, token_t *token)
{
  int32_t code;
  const rdata_info_t *fields = type->rdata.fields;

  if ((code = have_contiguous(parser, type, &fields[0], token)) < 0)
    return code;
  if ((code = parse_nsap(parser, type, &fields[0], rdata, token)) < 0)
    return code;
  if ((code = take_delimiter(parser, type, token)) < 0)
    return code;
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t check_nsap_ptr_rr(
  parser_t *parser, const type_info_t *type, const rdata_t *rdata)
{
  {
    int32_t r;
    size_t c = 0;
    const size_t n = (uintptr_t)rdata->octets - (uintptr_t)parser->rdata->octets;
    const uint8_t *o = parser->rdata->octets;
    const rdata_info_t *f = type->rdata.fields;

    if ((r = check(&c, check_string(parser, type, &f[0], o, n))))
      return r;

    if (c != n)
      SYNTAX_ERROR(parser, "Invalid %s", NAME(type));
  }

  {
    // RFC1706 section 6
    // A domain name is generated from an NSAP by reversing the hex nibbles of
    // the NSAP, treating each nibble as a separate subdomain, and appending
    // the top-level subdomain name "NSAP.INT" to it. For example, the domain
    // name used in the reverse lookup for the NSAP
    //
    //    47.0005.80.005a00.0000.0001.e133.ffffff000162.00
    //
    // would appear as
    //
    //    0.0.2.6.1.0.0.0.f.f.f.f.f.f.3.3.1.e.1.0.0.0.0.0.0.0.0.0.a.5.0.0.
    //                        0.8.5.0.0.0.7.4.NSAP.INT.
    size_t i = 0;
    const size_t n = parser->file->owner.length;
    const uint8_t *o = parser->file->owner.octets;
    for (; i < n; i += 2)
      if (o[i] != 1 || (base16_table_dec_32bit_d1[o[i+1]] > 0xff))
        break;

    const uint8_t nsap_int[] = { 4, 'n', 's', 'a', 'p', 3, 'i', 'n', 't', 0 };
    if (strncasecmp((const char *)o + i, (const char *)nsap_int, 9) != 0 || !i || i + 10 != n)
      SYNTAX_ERROR(parser, "Invalid %s", NAME(type));
  }

  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t parse_nsap_ptr_rdata(
  parser_t *parser, const type_info_t *type, rdata_t *rdata, token_t *token)
{
  int32_t code;
  const rdata_info_t *fields = type->rdata.fields;

  if ((code = have_contiguous_or_quoted(parser, type, &fields[0], token)) < 0)
    return code;
  if ((code = parse_string(parser, type, &fields[0], rdata, token)) < 0)
    return code;
  if ((code = take_delimiter(parser, type, token)) < 0)
    return code;

  // RFC1706 section 6 states each nibble is treated as a separate subdomain
  return check_nsap_ptr_rr(parser, type, rdata);
}

nonnull_all
static int32_t check_key_rr(
  parser_t *parser, const type_info_t *type, const rdata_t *rdata)
{
#if 0
  int32_t r;
  size_t c = 0;
  const size_t n = parser->rdata->length;
  const uint8_t *o = parser->rdata->octets;
  const rdata_info_t *f = type->rdata.fields;

  //
  // FIXME: implement (RFC2065)
  //
  // FIXME: verify the flag, algorithm and protocol combination is valid
  // FIXME: verify the key is valid for type(3)+algorithm(1)
  //
  // The combination is of course subject to secondary checks!
  //
#endif
  (void)type;
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t parse_key_rdata(
  parser_t *parser, const type_info_t *type, rdata_t *rdata, token_t *token)
{
  int32_t code;
  const rdata_info_t *fields = type->rdata.fields;

  if ((code = have_contiguous(parser, type, &fields[0], token)) < 0)
    return code;
  if ((code = parse_int16(parser, type, &fields[0], rdata, token)) < 0)
    return code;
  if ((code = take_contiguous(parser, type, &fields[1], token)) < 0)
    return code;
  if ((code = parse_int8(parser, type, &fields[1], rdata, token)) < 0)
    return code;
  if ((code = take_contiguous(parser, type, &fields[2], token)) < 0)
    return code;
  if ((code = parse_int8(parser, type, &fields[2], rdata, token)) < 0)
    return code;
  take(parser, token);
  if ((code = parse_base64_sequence(parser, type, &fields[3], rdata, token)) < 0)
    return code;

  return check_key_rr(parser, type, rdata);
}

nonnull_all
static int32_t check_px_rr(
  parser_t *parser, const type_info_t *type, const rdata_t *rdata)
{
  int32_t r;
  size_t c = 0;
  const size_t n = (uintptr_t)rdata->octets - (uintptr_t)parser->rdata->octets;
  const uint8_t *o = parser->rdata->octets;
  const rdata_info_t *f = type->rdata.fields;

  if ((r = check(&c, check_int16(parser, type, &f[0], o, n))) ||
      (r = check(&c, check_name(parser, type, &f[1], o+c, n-c))) ||
      (r = check(&c, check_name(parser, type, &f[2], o+c, n-c))))
    return r;

  if (c != n)
    SYNTAX_ERROR(parser, "Invalid %s record", NAME(type));
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t parse_px_rdata(
  parser_t *parser, const type_info_t *type, rdata_t *rdata, token_t *token)
{
  int32_t code;
  const rdata_info_t *fields = type->rdata.fields;

  if ((code = have_contiguous(parser, type, &fields[0], token)) < 0)
    return code;
  if ((code = parse_int16(parser, type, &fields[0], rdata, token)) < 0)
    return code;
  if ((code = take_contiguous(parser, type, &fields[1], token)) < 0)
    return code;
  if ((code = parse_name(parser, type, &fields[1], rdata, token)) < 0)
    return code;
  if ((code = take_contiguous(parser, type, &fields[2], token)) < 0)
    return code;
  if ((code = parse_name(parser, type, &fields[2], rdata, token)) < 0)
    return code;
  if ((code = take_delimiter(parser, type, token)) < 0)
    return code;
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t check_gpos_rr(
  parser_t *parser, const type_info_t *type, const rdata_t *rdata)
{
  int32_t r;
  size_t c = 0;
  const size_t n = (uintptr_t)rdata->octets - (uintptr_t)parser->rdata->octets;
  const uint8_t *o = parser->rdata->octets;
  const rdata_info_t *f = type->rdata.fields;

  if ((r = check(&c, check_string(parser, type, &f[0], o, n))) ||
      (r = check(&c, check_string(parser, type, &f[1], o+c, n-c))) ||
      (r = check(&c, check_string(parser, type, &f[2], o+c, n-c))))
    return r;

  if (c != n)
    SYNTAX_ERROR(parser, "Invalid %s record", NAME(type));
  return accept_rr(parser, type, rdata);
}

static int32_t parse_gpos_rdata(
  parser_t *parser, const type_info_t *type, rdata_t *rdata, token_t *token)
{
  int32_t code;
  const rdata_info_t *fields = type->rdata.fields;

  if ((code = have_contiguous(parser, type, &fields[0], token)) < 0)
    return code;
  if ((code = parse_latitude(parser, type, &fields[0], rdata, token)) < 0)
    return code;
  if ((code = take_contiguous(parser, type, &fields[1], token)) < 0)
    return code;
  if ((code = parse_longitude(parser, type, &fields[1], rdata, token)) < 0)
    return code;
  if ((code = take_contiguous(parser, type, &fields[2], token)) < 0)
    return code;
  if ((code = parse_altitude(parser, type, &fields[2], rdata, token)) < 0)
    return code;
  if ((code = take_delimiter(parser, type, token)) < 0)
    return code;
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t check_aaaa_rr(
  parser_t *parser, const type_info_t *type, const rdata_t *rdata)
{
  int32_t r;
  size_t c = 0;
  const size_t n = (uintptr_t)rdata->octets - (uintptr_t)parser->rdata->octets;
  const uint8_t *o = parser->rdata->octets;
  const rdata_info_t *f = type->rdata.fields;

  if ((r = check(&c, check_ip6(parser, type, &f[0], o, n))))
    return r;

  if (c != n)
    SYNTAX_ERROR(parser, "Invalid %s record", NAME(type));
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t parse_aaaa_rdata(
  parser_t *parser, const type_info_t *type, rdata_t *rdata, token_t *token)
{
  int32_t code;
  const rdata_info_t *fields = type->rdata.fields;

  if ((code = have_contiguous(parser, type, &fields[0], token)) < 0)
    return code;
  if ((code = parse_ip6(parser, type, &fields[0], rdata, token)) < 0)
    return code;
  if ((code = take_delimiter(parser, type, token)) < 0)
    return code;
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t check_loc_rr(
  parser_t *parser, const type_info_t *type, const rdata_t *rdata)
{
  if ((uintptr_t)rdata->octets - (uintptr_t)parser->rdata->octets != 16)
    SYNTAX_ERROR(parser, "Invalid %s record", NAME(type));
  return accept_rr(parser, type, rdata);

  // FIXME: check validity of latitude, longitude and latitude?
}

nonnull_all
static int32_t parse_loc_rdata(
  parser_t *parser, const type_info_t *type, rdata_t *rdata, token_t *token)
{
  int32_t code;
  uint32_t degrees, minutes, seconds;
  uint32_t latitude, longitude, altitude;
  const rdata_info_t *fields = type->rdata.fields;
  static const uint8_t defaults[4] = { 0x00, 0x12, 0x16, 0x13 };

  // RFC1876 section 3:
  // If omitted, minutes and seconds default to zero, size defaults to 1m,
  // horizontal precision defaults to 10000m, and vertical precision defaults
  // to 10m.
  memcpy(rdata->octets, &defaults, sizeof(defaults));

  // latitude
  if ((code = have_contiguous(parser, type, &fields[4], token)) < 0)
    return code;
  if (scan_degrees(token->data, token->length, &degrees) == -1)
    SYNTAX_ERROR(parser, "Invalid %s in %s", NAME(&fields[4]), NAME(type));
  if ((code = take_contiguous(parser, type, &fields[4], token)) < 0)
    return code;
  if (scan_minutes(token->data, token->length, &minutes) == -1)
    goto north_south; // minutes default to zero
  degrees += minutes;
  if ((code = take_contiguous(parser, type, &fields[4], token)) < 0)
    return code;
  if (scan_seconds(token->data, token->length, &seconds) == -1)
    goto north_south; // seconds default to zero
  degrees += seconds;

  if ((code = take_contiguous(parser, type, &fields[4], token)) < 0)
    return code;
north_south:
  if (token->data[0] == 'N')
    latitude = htobe32((1u<<31) + degrees);
  else if (token->data[0] == 'S')
    latitude = htobe32((1u<<31) - degrees);
  else
    SYNTAX_ERROR(parser, "Invalid %s in %s", NAME(&fields[4]), NAME(type));

  memcpy(&rdata->octets[4], &latitude, sizeof(latitude));

  // longitude
  if ((code = take_contiguous(parser, type, &fields[5], token)) < 0)
    return code;
  if (scan_degrees(token->data, token->length, &degrees) == -1)
    SYNTAX_ERROR(parser, "Invalid %s in %s", NAME(&fields[5]), NAME(type));
  if ((code = take_contiguous(parser, type, &fields[5], token)) < 0)
    return code;
  if (scan_minutes(token->data, token->length, &minutes) == -1)
    goto east_west; // minutes default to zero
  degrees += minutes;
  if ((code = take_contiguous(parser, type, &fields[5], token)) < 0)
    return code;
  if (scan_seconds(token->data, token->length, &seconds) == -1)
    goto east_west; // seconds default to zero
  degrees += seconds;

  if ((code = take_contiguous(parser, type, &fields[5], token)) < 0)
    return code;
east_west:
  if (token->data[0] == 'E')
    longitude = htobe32((1u<<31) + degrees);
  else if (token->data[0] == 'W')
    longitude = htobe32((1u<<31) - degrees);
  else
    SYNTAX_ERROR(parser, "Invalid %s in %s", NAME(&fields[5]), NAME(type));

  memcpy(&rdata->octets[8], &longitude, sizeof(longitude));

  // altitude
  if ((code = take_contiguous(parser, type, &fields[6], token)) < 0)
    return code;
  if (scan_altitude(token->data, token->length, &altitude) == -1)
    SYNTAX_ERROR(parser, "Invalid %s in %s", NAME(&fields[6]), NAME(type));

  altitude = htobe32(altitude);
  memcpy(&rdata->octets[12], &altitude, sizeof(altitude));

  // size
  take(parser, token);
  if (!is_contiguous(token))
    goto skip_optional;
  if (scan_precision(token->data, token->length, &rdata->octets[1]))
    SYNTAX_ERROR(parser, "Invalid %s in %s", NAME(&fields[1]), NAME(type));

  // horizontal precision
  take(parser, token);
  if (!is_contiguous(token))
    goto skip_optional;
  if (scan_precision(token->data, token->length, &rdata->octets[2]))
    SYNTAX_ERROR(parser, "Invalid %s in %s", NAME(&fields[2]), NAME(type));

  // vertical precision
  take(parser, token);
  if (!is_contiguous(token))
    goto skip_optional;
  if (scan_precision(token->data, token->length, &rdata->octets[3]))
    SYNTAX_ERROR(parser, "Invalid %s in %s", NAME(&fields[3]), NAME(type));

  take(parser, token);
skip_optional:
  if ((code = have_delimiter(parser, type, token)) < 0)
    return code;

  rdata->octets += 16;
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t check_nxt_rr(
  parser_t *parser, const type_info_t *type, const rdata_t *rdata)
{
  int32_t r;
  size_t c = 0;
  const size_t n = (uintptr_t)rdata->octets - (uintptr_t)parser->rdata->octets;
  const uint8_t *o = parser->rdata->octets;
  const rdata_info_t *f = type->rdata.fields;

  if ((r = check(&c, check_name(parser, type, &f[0], o+c, n-c))))
    return r;

  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t parse_nxt_rdata(
  parser_t *parser, const type_info_t *type, rdata_t *rdata, token_t *token)
{
  int32_t code;
  const rdata_info_t *fields = type->rdata.fields;

  if ((code = have_contiguous(parser, type, &fields[0], token)) < 0)
    return code;
  if ((code = parse_name(parser, type, &fields[0], rdata, token)) < 0)
    return code;
  take(parser, token);
  if ((code = parse_nxt(parser, type, &fields[1], rdata, token)) < 0)
    return code;

  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t check_eid_rr(
  parser_t *parser, const type_info_t *type, const rdata_t *rdata)
{
  if ((uintptr_t)rdata->octets - (uintptr_t)parser->rdata->octets <= 0)
    SYNTAX_ERROR(parser, "Invalid %s record", NAME(type));
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t parse_eid_rdata(
  parser_t *parser, const type_info_t *type, rdata_t *rdata, token_t *token)
{
  int32_t code;
  const rdata_info_t *fields = type->rdata.fields;

  if ((code = parse_base16_sequence(parser, type, &fields[1], rdata, token)) < 0)
    return code;
  return check_eid_rr(parser, type, rdata);
}

nonnull_all
static int32_t check_srv_rr(
  parser_t *parser, const type_info_t *type, const rdata_t *rdata)
{
  int32_t r;
  size_t c = 0;
  const size_t n = (uintptr_t)rdata->octets - (uintptr_t)parser->rdata->octets;
  const uint8_t *o = parser->rdata->octets;
  const rdata_info_t *f = type->rdata.fields;

  if ((r = check(&c, check_int16(parser, type, &f[0], o, n))) ||
      (r = check(&c, check_int16(parser, type, &f[1], o+c, n-c))) ||
      (r = check(&c, check_int16(parser, type, &f[2], o+c, n-c))) ||
      (r = check(&c, check_name(parser, type, &f[3], o+c, n-c))))
    return r;

  if (c != n)
    SYNTAX_ERROR(parser, "Invalid %s", NAME(type));
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t parse_srv_rdata(
  parser_t *parser, const type_info_t *type, rdata_t *rdata, token_t *token)
{
  int32_t code;
  const rdata_info_t *fields = type->rdata.fields;

  if ((code = have_contiguous(parser, type, &fields[0], token)) < 0)
    return code;
  if ((code = parse_int16(parser, type, &fields[0], rdata, token)) < 0)
    return code;
  if ((code = take_contiguous(parser, type, &fields[1], token)) < 0)
    return code;
  if ((code = parse_int16(parser, type, &fields[1], rdata, token)) < 0)
    return code;
  if ((code = take_contiguous(parser, type, &fields[2], token)) < 0)
    return code;
  if ((code = parse_int16(parser, type, &fields[2], rdata, token)) < 0)
    return code;
  if ((code = take_contiguous(parser, type, &fields[3], token)) < 0)
    return code;
  if ((code = parse_name(parser, type, &fields[3], rdata, token)) < 0)
    return code;
  if ((code = take_delimiter(parser, type, token)) < 0)
    return code;
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t check_atma_rr(
  parser_t *parser, const type_info_t *type, const rdata_t *rdata)
{
  assert(rdata->octets >= parser->rdata->octets);
  if ((uintptr_t)rdata->octets - (uintptr_t)parser->rdata->octets > 2)
    return accept_rr(parser, type, rdata);
  SYNTAX_ERROR(parser, "Invalid %s", NAME(type));
}

nonnull_all
static int32_t parse_atma_rdata(
  parser_t *parser, const type_info_t *type, rdata_t *rdata, token_t *token)
{
  int32_t code;
  const rdata_info_t *fields = type->rdata.fields;

  if ((code = have_contiguous(parser, type, &fields[0], token)) < 0)
    return code;
  if ((code = parse_atma(parser, type, &fields[0], rdata, token)) < 0)
    return code;
  if ((code = take_delimiter(parser, type, token)) < 0)
    return code;
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t check_naptr_rr(
  parser_t *parser, const type_info_t *type, const rdata_t *rdata)
{
  // FIXME: implement actual checks
  (void)type;
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t parse_naptr_rdata(
  parser_t *parser, const type_info_t *type, rdata_t *rdata, token_t *token)
{
  int32_t code;
  const rdata_info_t *fields = type->rdata.fields;

  if ((code = have_contiguous(parser, type, &fields[0], token)) < 0)
    return code;
  if ((code = parse_int16(parser, type, &fields[0], rdata, token)) < 0)
    return code;
  if ((code = take_contiguous(parser, type, &fields[1], token)) < 0)
    return code;
  if ((code = parse_int16(parser, type, &fields[1], rdata, token)) < 0)
    return code;
  if ((code = take_quoted_or_contiguous(parser, type, &fields[2], token)) < 0)
    return code;
  if ((code = parse_string(parser, type, &fields[2], rdata, token)) < 0)
    return code;
  if ((code = take_quoted_or_contiguous(parser, type, &fields[3], token)) < 0)
    return code;
  if ((code = parse_string(parser, type, &fields[3], rdata, token)) < 0)
    return code;
  if ((code = take_quoted_or_contiguous(parser, type, &fields[4], token)) < 0)
    return code;
  if ((code = parse_string(parser, type, &fields[4], rdata, token)) < 0)
    return code;
  if ((code = take_contiguous(parser, type, &fields[5], token)) < 0)
    return code;
  if ((code = parse_name(parser, type, &fields[5], rdata, token)) < 0)
    return code;
  if ((code = take_delimiter(parser, type, token)) < 0)
    return code;
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t check_cert_rr(
  parser_t *parser, const type_info_t *type, const rdata_t *rdata)
{
  // FIXME: implement actual checks
  (void)type;

  assert(rdata->octets >= parser->rdata->octets);
  if ((uintptr_t)rdata->octets - (uintptr_t)parser->rdata->octets < 6)
    SYNTAX_ERROR(parser, "Invalid %s", NAME(type));
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t parse_cert_rdata(
  parser_t *parser, const type_info_t *type, rdata_t *rdata, token_t *token)
{
  int32_t code;
  const rdata_info_t *fields = type->rdata.fields;

  if ((code = have_contiguous(parser, type, &fields[0], token)) < 0)
    return code;
  if ((code = parse_certificate_type(parser, type, &fields[0], rdata, token)) < 0)
    return code;
  if ((code = take_contiguous(parser, type, &fields[1], token)) < 0)
    return code;
  if ((code = parse_int16(parser, type, &fields[1], rdata, token)) < 0)
    return code;
  if ((code = take_contiguous(parser, type, &fields[2], token)) < 0)
    return code;
  if ((code = parse_algorithm(parser, type, &fields[2], rdata, token)) < 0)
    return code;
  take(parser, token);
  if ((code = parse_base64_sequence(parser, type, &fields[3], rdata, token)) < 0)
    return code;

  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t check_sink_rr(
  parser_t *parser, const type_info_t *type, const rdata_t *rdata)
{
  // FIXME: implement actual checks
  (void)type;

  assert(rdata->octets >= parser->rdata->octets);
  if ((uintptr_t)rdata->octets - (uintptr_t)parser->rdata->octets < 3)
    SYNTAX_ERROR(parser, "Invalid %s", NAME(type));
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t parse_sink_rdata(
  parser_t *parser, const type_info_t *type, rdata_t *rdata, token_t *token)
{
  int32_t code;
  const rdata_info_t *fields = type->rdata.fields;

  if ((code = have_contiguous(parser, type, &fields[0], token)) < 0)
    return code;
  if ((code = parse_int8(parser, type, &fields[0], rdata, token)) < 0)
    return code;
  if ((code = take_contiguous(parser, type, &fields[1], token)) < 0)
    return code;
  if ((code = parse_int8(parser, type, &fields[1], rdata, token)) < 0)
    return code;
  take(parser, token);
  if ((code = parse_base64_sequence(parser, type, &fields[3], rdata, token)) < 0)
    return code;

  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t check_apl_rr(
  parser_t *parser, const type_info_t *type, const rdata_t *rdata)
{
  // FIXME: check correctness of fields and total length
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t parse_apl_rdata(
  parser_t *parser, const type_info_t *type, rdata_t *rdata, token_t *token)
{
  int32_t code;
  const rdata_info_t *fields = type->rdata.fields;

  // RDATA section for APL consists of zero or more fields
  while (is_contiguous(token)) {
    int32_t length;
    const size_t size = (uintptr_t)rdata->limit - (uintptr_t)rdata->octets;
    if ((length = scan_apl(token->data, token->length, rdata->octets, size)) < 0)
      SYNTAX_ERROR(parser, "Invalid %s in %s", NAME(&fields[0]), NAME(type));
    assert(length == 8 /* ipv4 */ || length == 20 /* ipv6 */);
    rdata->octets += (size_t)length;
    take(parser, token);
  }

  if ((code = have_delimiter(parser, type, token)) < 0)
    return code;
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t check_ds_rr(
  parser_t *parser, const type_info_t *type, const rdata_t *rdata)
{
  int32_t r;
  size_t c = 0;
  const size_t n = (uintptr_t)rdata->octets - (uintptr_t)parser->rdata->octets;
  const uint8_t *o = parser->rdata->octets;
  const rdata_info_t *f = type->rdata.fields;

  if ((r = check(&c, check_int16(parser, type, &f[0], o, n))) ||
      (r = check(&c, check_int8(parser, type, &f[1], o+c, n-c))) ||
      (r = check(&c, check_int8(parser, type, &f[2], o+c, n-c))))
    return r;

  const uint8_t digest_algorithm = parser->rdata->octets[3];

  if ((digest_algorithm & 0x7) == digest_algorithm) {
    // https://www.iana.org/assignments/ds-rr-types
    static const uint8_t digest_sizes[8] = {
      0,  // 0: Reserved
      20, // 1: SHA-1
      32, // 2: SHA-256
      32, // 3: GOST R 34.11-94
      48, // 4: SHA-384
      48, // 5: GOST R 34.10-2012
      48, // 6: SM3
      0   // 7: Unassigned
    };

    const uint8_t digest_size = digest_sizes[ digest_algorithm ];

    if (digest_size && n - 4 != digest_size)
      SEMANTIC_ERROR(parser, "Invalid digest in %s", NAME(type));
  }

  if (c > n)
    SYNTAX_ERROR(parser, "Invalid %s", NAME(type));
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t parse_ds_rdata(
  parser_t *parser, const type_info_t *type, rdata_t *rdata, token_t *token)
{
  int32_t code;
  const rdata_info_t *fields = type->rdata.fields;

  if ((code = have_contiguous(parser, type, &fields[0], token)) < 0)
    return code;
  if ((code = parse_int16(parser, type, &fields[0], rdata, token)) < 0)
    return code;
  if ((code = take_contiguous(parser, type, &fields[1], token)) < 0)
    return code;
  if ((code = parse_algorithm(parser, type, &fields[1], rdata, token)) < 0)
    return code;
  if ((code = take_contiguous(parser, type, &fields[2], token)) < 0)
    return code;
  if ((code = parse_int8(parser, type, &fields[2], rdata, token)) < 0)
    return code;
  take(parser, token);
  if (!(token->length == 1 && (char)*token->data == '0')
  &&  (code = parse_base16_sequence(parser, type, &fields[3], rdata, token)) < 0)
    return code;

  const uint8_t digest_algorithm = parser->rdata->octets[3];

  if ((digest_algorithm & 0x7) == digest_algorithm) {
    // https://www.iana.org/assignments/ds-rr-types
    static const uint8_t digest_sizes[8] = {
      0,  // 0: Reserved
      20, // 1: SHA-1
      32, // 2: SHA-256
      32, // 3: GOST R 34.11-94
      48, // 4: SHA-384
      48, // 5: GOST R 34.10-2012
      48, // 6: SM3
      0   // 7: Unassigned
    };

    const uint8_t digest_size = digest_sizes[ digest_algorithm ];
    size_t length = (uintptr_t)rdata->octets - (uintptr_t)parser->rdata->octets;

    if (digest_size && length - 4 != digest_size)
      SEMANTIC_ERROR(parser, "Invalid digest in %s", NAME(type));
  }

  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t check_sshfp_rr(
  parser_t *parser, const type_info_t *type, const rdata_t *rdata)
{
  int32_t r;
  size_t c = 0;
  const size_t n = (uintptr_t)rdata->octets - (uintptr_t)parser->rdata->octets;
  const uint8_t *o = parser->rdata->octets;
  const rdata_info_t *f = type->rdata.fields;

  if ((r = check(&c, check_int8(parser, type, &f[0], o, n))) ||
      (r = check(&c, check_int8(parser, type, &f[1], o+c, n-c))))
    return r;

  // https://www.iana.org/assignments/dns-sshfp-rr-parameters

  if (c == n)
    SYNTAX_ERROR(parser, "Missing %s in %s", NAME((&f[n!=0])), NAME(type));
  else if (o[1] == 1 && (n - c) != 20)
    SEMANTIC_ERROR(parser, "Wrong fingerprint size for type %s in %s",
                           "SHA1", NAME(type));
  else if (o[1] == 2 && (n - c) != 32)
    SEMANTIC_ERROR(parser, "Wrong fingerprint size for type %s in %s",
                           "SHA256", NAME(type));

  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t parse_sshfp_rdata(
  parser_t *parser, const type_info_t *type, rdata_t *rdata, token_t *token)
{
  int32_t code;
  const rdata_info_t *fields = type->rdata.fields;

  if ((code = have_contiguous(parser, type, &fields[0], token)) < 0)
    return code;
  if ((code = parse_int8(parser, type, &fields[0], rdata, token)) < 0)
    return code;

  const uint8_t *fingerprint_type = rdata->octets;
  if ((code = take_contiguous(parser, type, &fields[1], token)) < 0)
    return code;
  if ((code = parse_int8(parser, type, &fields[1], rdata, token)) < 0)
    return code;

  const uint8_t *fingerprint = rdata->octets;
  take(parser, token);
  if ((code = parse_base16_sequence(parser, type, &fields[2], rdata, token)) < 0)
    return code;

  // https://www.iana.org/assignments/dns-sshfp-rr-parameters
  size_t fingerprint_size = (uintptr_t)rdata->octets - (uintptr_t)fingerprint;
  if (unlikely(*fingerprint_type == 1 && fingerprint_size != 20))
    SEMANTIC_ERROR(parser, "Wrong fingerprint size for type %s in %s",
                           "SHA1", NAME(type));
  if (unlikely(*fingerprint_type == 2 && fingerprint_size != 32))
    SEMANTIC_ERROR(parser, "Wrong fingerprint size for type %s in %s",
                           "SHA256", NAME(type));

  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t check_ipseckey_rr(
  parser_t *parser, const type_info_t *type, const rdata_t *rdata);

nonnull_all
static int32_t parse_ipseckey_rdata(
  parser_t *parser, const type_info_t *type, rdata_t *rdata, token_t *token);

diagnostic_push()
gcc_diagnostic_ignored(missing-field-initializers)
clang_diagnostic_ignored(missing-field-initializers)

static const rdata_info_t ipseckey_ipv4_rdata_fields[] = {
  FIELD("precedence"),
  FIELD("gateway type"),
  FIELD("algorithm"),
  FIELD("gateway"),
  FIELD("public key")
};

static const type_info_t ipseckey_ipv4[] = {
  TYPE("IPSECKEY", ZONE_TYPE_IPSECKEY, ZONE_CLASS_IN, FIELDS(ipseckey_ipv4_rdata_fields),
                   check_ipseckey_rr, parse_ipseckey_rdata),
};

static const rdata_info_t ipseckey_ipv6_rdata_fields[] = {
  FIELD("precedence"),
  FIELD("gateway type"),
  FIELD("algorithm"),
  FIELD("gateway"),
  FIELD("public key")
};

static const type_info_t ipseckey_ipv6[] = {
  TYPE("IPSECKEY", ZONE_TYPE_IPSECKEY, ZONE_CLASS_IN, FIELDS(ipseckey_ipv6_rdata_fields),
                   check_ipseckey_rr, parse_ipseckey_rdata),
};

diagnostic_pop()

nonnull_all
static int32_t check_ipseckey_rr(
  parser_t *parser, const type_info_t *type, const rdata_t *rdata)
{
  int32_t r;
  size_t c = 0;
  const size_t n = (uintptr_t)rdata->octets - (uintptr_t)parser->rdata->octets;
  const uint8_t *o = parser->rdata->octets;
  const type_info_t *t = type;
  const rdata_info_t *f = type->rdata.fields;

  if ((r = check(&c, check_int8(parser, type, &f[0], o, n))) ||
      (r = check(&c, check_int8(parser, type, &f[1], o+c, n-c))) ||
      (r = check(&c, check_int8(parser, type, &f[2], o+c, n-c))))
    return r;

  switch (parser->rdata->octets[1]) {
    case 1: /* IPv4 address */
      t = (const type_info_t *)ipseckey_ipv4;
      f = ipseckey_ipv4_rdata_fields;
      if ((r = check(&c, check_ip4(parser, t, &f[3], o+c, n-c))) < 0)
        return r;
      break;
    case 2: /* IPv6 address */
      t = (const type_info_t *)ipseckey_ipv6;
      f = ipseckey_ipv6_rdata_fields;
      if ((r = check(&c, check_ip6(parser, t, &f[3], o+c, n-c))) < 0)
        return r;
      break;
    case 0: /* no gateway */
      break;
    case 3: /* domain name */
      if ((r = check(&c, check_name(parser, t, &f[3], o+c, n-c))) < 0)
        return r;
      break;
    default:
      SYNTAX_ERROR(parser, "Invalid %s", NAME(type));
  }

  switch (parser->rdata->octets[2]) {
    case 0:
      if (c < n)
        SYNTAX_ERROR(parser, "Trailing data in %s", NAME(t));
      break;
    default:
      if (c >= n)
        SYNTAX_ERROR(parser, "Missing %s in %s", NAME(&f[4]), NAME(t));
      break;
  }

  return accept_rr(parser, t, rdata);
}

nonnull_all
static int32_t parse_ipseckey_rdata(
  parser_t *parser, const type_info_t *type, rdata_t *rdata, token_t *token)
{
  int32_t code;
  const rdata_info_t *fields = type->rdata.fields;
  uint8_t *octets = rdata->octets;

  if ((code = have_contiguous(parser, type, &fields[0], token)) < 0)
    return code;
  if ((code = parse_int8(parser, type, &fields[0], rdata, token)) < 0)
    return code;
  if ((code = take_contiguous(parser, type, &fields[1], token)) < 0)
    return code;
  if ((code = parse_int8(parser, type, &fields[1], rdata, token)) < 0)
    return code;
  if ((code = take_contiguous(parser, type, &fields[2], token)) < 0)
    return code;
  if ((code = parse_int8(parser, type, &fields[2], rdata, token)) < 0)
    return code;
  if ((code = take_contiguous(parser, type, &fields[3], token)) < 0)
    return code;

  switch (octets[1]) {
    case 0: /* no gateway */
      if (token->length != 1 || token->data[0] != '.')
        SYNTAX_ERROR(parser, "Invalid %s in %s", NAME(&fields[3]), NAME(type));
      break;
    case 1: /* IPv4 address */
      type = (const type_info_t *)ipseckey_ipv4;
      fields = type->rdata.fields;
      if ((code = parse_ip4(parser, type, &fields[3], rdata, token)) < 0)
        return code;
      break;
    case 2: /* IPv6 address */
      type = (const type_info_t *)ipseckey_ipv6;
      fields = type->rdata.fields;
      if ((code = parse_ip6(parser, type, &fields[3], rdata, token)) < 0)
        return code;
      break;
    case 3: /* domain name */
      if ((code = parse_name(parser, type, &fields[3], rdata, token)) < 0)
        return code;
      break;
    default:
      SYNTAX_ERROR(parser, "Invalid %s in %s", NAME(&fields[3]), NAME(type));
  }

  take(parser, token);
  switch (octets[2]) {
    case 0:
      if ((code = have_delimiter(parser, type, token)) < 0)
        return code;
      break;
    default:
      if ((code = parse_base64_sequence(parser, type, &fields[4], rdata, token)) < 0)
        return code;
      break;
  }

  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t check_rrsig_rr(
  parser_t *parser, const type_info_t *type, const rdata_t *rdata)
{
  int32_t r;
  size_t c = 0;
  const size_t n = (uintptr_t)rdata->octets - (uintptr_t)parser->rdata->octets;
  const uint8_t *o = parser->rdata->octets;
  const rdata_info_t *f = type->rdata.fields;

  if ((r = check(&c, check_int16(parser, type, &f[0], o, n))) ||
      (r = check(&c, check_int8(parser, type, &f[1], o+c, n-c))) ||
      (r = check(&c, check_int8(parser, type, &f[2], o+c, n-c))) ||
      (r = check(&c, check_ttl(parser, type, &f[3], o+c, n-c))) ||
      (r = check(&c, check_int32(parser, type, &f[4], o+c, n-c))) ||
      (r = check(&c, check_int32(parser, type, &f[5], o+c, n-c))) ||
      (r = check(&c, check_int16(parser, type, &f[6], o+c, n-c))) ||
      (r = check(&c, check_name(parser, type, &f[7], o+c, n-c))))
    return r;

  if (c > n)
    SYNTAX_ERROR(parser, "Invalid %s", NAME(type));
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t parse_rrsig_rdata(
  parser_t *parser, const type_info_t *type, rdata_t *rdata, token_t *token)
{
  int32_t code;
  const rdata_info_t *fields = type->rdata.fields;

  if ((code = have_contiguous(parser, type, &fields[0], token)) < 0)
    return code;
  if ((code = parse_type(parser, type, &fields[0], rdata, token)) < 0)
    return code;
  if ((code = take_contiguous(parser, type, &fields[1], token)) < 0)
    return code;
  if ((code = parse_algorithm(parser, type, &fields[1], rdata, token)) < 0)
    return code;
  if ((code = take_contiguous(parser, type, &fields[2], token)) < 0)
    return code;
  if ((code = parse_int8(parser, type, &fields[2], rdata, token)) < 0)
    return code;
  if ((code = take_contiguous(parser, type, &fields[3], token)) < 0)
    return code;
  if ((code = parse_ttl(parser, type, &fields[3], rdata, token)) < 0)
    return code;
  if ((code = take_contiguous(parser, type, &fields[4], token)) < 0)
    return code;
  if ((code = parse_time(parser, type, &fields[4], rdata, token)) < 0)
    return code;
  if ((code = take_contiguous(parser, type, &fields[5], token)) < 0)
    return code;
  if ((code = parse_time(parser, type, &fields[5], rdata, token)) < 0)
    return code;
  if ((code = take_contiguous(parser, type, &fields[6], token)) < 0)
    return code;
  if ((code = parse_int16(parser, type, &fields[6], rdata, token)) < 0)
    return code;
  if ((code = take_contiguous(parser, type, &fields[7], token)) < 0)
    return code;
  if ((code = parse_name(parser, type, &fields[7], rdata, token)) < 0)
    return code;
  take(parser, token);
  if ((code = parse_base64_sequence(parser, type, &fields[8], rdata, token)) < 0)
    return code;

  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t check_nsec_rr(
  parser_t *parser, const type_info_t *type, const rdata_t *rdata)
{
  int32_t r;
  size_t c = 0;
  const size_t n = (uintptr_t)rdata->octets - (uintptr_t)parser->rdata->octets;
  const uint8_t *o = parser->rdata->octets;
  const rdata_info_t *f = type->rdata.fields;

  if ((r = check(&c, check_name(parser, type, &f[0], o, n))) ||
      (r = check(&c, check_nsec(parser, type, &f[1], o+c, n-c))))
    return r;

  if (c != n)
    SYNTAX_ERROR(parser, "Invalid %s", NAME(type));
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t parse_nsec_rdata(
  parser_t *parser, const type_info_t *type, rdata_t *rdata, token_t *token)
{
  int32_t code;
  const rdata_info_t *fields = type->rdata.fields;

  if ((code = have_contiguous(parser, type, &fields[0], token)) < 0)
    return code;
  if ((code = parse_name(parser, type, &fields[0], rdata, token)) < 0)
    return code;
  take(parser, token);
  if ((code = parse_nsec(parser, type, &fields[1], rdata, token)) < 0)
    return code;

  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t check_dnskey_rr(
  parser_t *parser, const type_info_t *type, const rdata_t *rdata)
{
  int32_t r;
  size_t c = 0;
  const size_t n = (uintptr_t)rdata->octets - (uintptr_t)parser->rdata->octets;
  const uint8_t *o = parser->rdata->octets;
  const rdata_info_t *f = type->rdata.fields;

  if ((r = check(&c, check_int16(parser, type, &f[0], o, n))) ||
      (r = check(&c, check_int8(parser, type, &f[1], o+c, n-c))) ||
      (r = check(&c, check_int8(parser, type, &f[2], o+c, n-c))))
    return r;

  if (c > n)
    SYNTAX_ERROR(parser, "Invalid %s", NAME(type));
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t parse_dnskey_rdata(
  parser_t *parser, const type_info_t *type, rdata_t *rdata, token_t *token)
{
  int32_t code;
  const rdata_info_t *fields = type->rdata.fields;

  if ((code = have_contiguous(parser, type, &fields[0], token)) < 0)
    return code;
  if ((code = parse_int16(parser, type, &fields[0], rdata, token)) < 0)
    return code;
  if ((code = take_contiguous(parser, type, &fields[1], token)) < 0)
    return code;
  if ((code = parse_int8(parser, type, &fields[1], rdata, token)) < 0)
    return code;
  if ((code = take_contiguous(parser, type, &fields[2], token)) < 0)
    return code;
  if ((code = parse_algorithm(parser, type, &fields[2], rdata, token)) < 0)
    return code;
  take(parser, token);
  if (!(token->length == 1 && (char)*token->data == '0')
  &&  (code = parse_base64_sequence(parser, type, &fields[3], rdata, token)) < 0)
    return code;

  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t check_dhcid_rr(
  parser_t *parser, const type_info_t *type, const rdata_t *rdata)
{
  // RFC4701 section 3.1:
  // 2-octet identifier type, 1-octet digest type, followed by one or more
  // octets representing the actual identifier
  if ((uintptr_t)rdata->octets - (uintptr_t)parser->rdata->octets < 4)
    SEMANTIC_ERROR(parser, "Invalid %s", NAME(type));
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t parse_dhcid_rdata(
  parser_t *parser, const type_info_t *type, rdata_t *rdata, token_t *token)
{
  int32_t code;
  const rdata_info_t *fields = type->rdata.fields;

  if ((code = parse_base64_sequence(parser, type, &fields[0], rdata, token)) < 0)
    return code;

  return check_dhcid_rr(parser, type, rdata);
}

nonnull_all
static int32_t check_nsec3_rr(
  parser_t *parser, const type_info_t *type, const rdata_t *rdata)
{
  int32_t r;
  size_t c = 0;
  const size_t n = (uintptr_t)rdata->octets - (uintptr_t)parser->rdata->octets;
  const uint8_t *o = parser->rdata->octets;
  const rdata_info_t *f = type->rdata.fields;

  if ((r = check(&c, check_int8(parser, type, &f[0], o, n))) ||
      (r = check(&c, check_int8(parser, type, &f[1], o+c, n-c))) ||
      (r = check(&c, check_int16(parser, type, &f[2], o+c, n-c))) ||
      (r = check(&c, check_string(parser, type, &f[3], o+c, n-c))) ||
      (r = check(&c, check_string(parser, type, &f[4], o+c, n-c))) ||
      (r = check(&c, check_nsec(parser, type, &f[5], o+c, n-c))))
    return r;

  if (c != n)
    SYNTAX_ERROR(parser, "Invalid %s", NAME(type));
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t parse_nsec3_rdata(
  parser_t *parser, const type_info_t *type, rdata_t *rdata, token_t *token)
{
  int32_t code;
  const rdata_info_t *fields = type->rdata.fields;

  if ((code = have_contiguous(parser, type, &fields[0], token)) < 0)
    return code;
  if ((code = parse_int8(parser, type, &fields[0], rdata, token)) < 0)
    return code;
  if ((code = take_contiguous(parser, type, &fields[1], token)) < 0)
    return code;
  if ((code = parse_int8(parser, type, &fields[1], rdata, token)) < 0)
    return code;
  if ((code = take_contiguous(parser, type, &fields[2], token)) < 0)
    return code;
  if ((code = parse_int16(parser, type, &fields[2], rdata, token)) < 0)
    return code;
  if ((code = take_contiguous(parser, type, &fields[3], token)) < 0)
    return code;
  if ((code = parse_salt(parser, type, &fields[3], rdata, token)) < 0)
    return code;
  if ((code = take_contiguous(parser, type, &fields[4], token)) < 0)
    return code;
  if ((code = parse_base32(parser, type, &fields[4], rdata, token)) < 0)
    return code;
  take(parser, token);
  if ((code = parse_nsec(parser, type, &fields[5], rdata, token)) < 0)
    return code;

  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t check_nsec3param_rr(
  parser_t *parser, const type_info_t *type, const rdata_t *rdata)
{
  int32_t r;
  size_t c = 0;
  const size_t n = (uintptr_t)rdata->octets - (uintptr_t)parser->rdata->octets;
  const uint8_t *o = parser->rdata->octets;
  const rdata_info_t *f = type->rdata.fields;

  if ((r = check(&c, check_int8(parser, type, &f[0], o, n))) ||
      (r = check(&c, check_int8(parser, type, &f[1], o+c, n-c))) ||
      (r = check(&c, check_int16(parser, type, &f[2], o+c, n-c))) ||
      (r = check(&c, check_string(parser, type, &f[3], o+c, n-c))))
    return r;

  if (c != n)
    SYNTAX_ERROR(parser, "Invalid %s", NAME(type));
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t parse_nsec3param_rdata(
  parser_t *parser, const type_info_t *type, rdata_t *rdata, token_t *token)
{
  int32_t code;
  const rdata_info_t *fields = type->rdata.fields;

  if ((code = have_contiguous(parser, type, &fields[0], token)) < 0)
    return code;
  if ((code = parse_int8(parser, type, &fields[0], rdata, token)) < 0)
    return code;
  if ((code = take_contiguous(parser, type, &fields[1], token)) < 0)
    return code;
  if ((code = parse_int8(parser, type, &fields[1], rdata, token)) < 0)
    return code;
  if ((code = take_contiguous(parser, type, &fields[2], token)) < 0)
    return code;
  if ((code = parse_int16(parser, type, &fields[2], rdata, token)) < 0)
    return code;
  if ((code = take_contiguous(parser, type, &fields[3], token)) < 0)
    return code;
  if ((code = parse_salt(parser, type, &fields[3], rdata, token)) < 0)
    return code;
  if ((code = take_delimiter(parser, type, token)) < 0)
    return code;
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t check_tlsa_rr(
  parser_t *parser, const type_info_t *type, const rdata_t *rdata)
{
  int32_t r;
  size_t c = 0;
  const size_t n = (uintptr_t)rdata->octets - (uintptr_t)parser->rdata->octets;
  const uint8_t *o = parser->rdata->octets;
  const rdata_info_t *f = type->rdata.fields;

  if ((r = check(&c, check_int8(parser, type, &f[0], o, n))) ||
      (r = check(&c, check_int8(parser, type, &f[1], o+c, n-c))) ||
      (r = check(&c, check_int8(parser, type, &f[2], o+c, n-c))))
    return r;

  if (c >= n)
    SYNTAX_ERROR(parser, "Invalid %s", NAME(type));
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t parse_tlsa_rdata(
  parser_t *parser, const type_info_t *type, rdata_t *rdata, token_t *token)
{
  int32_t code;
  const rdata_info_t *fields = type->rdata.fields;

  if ((code = have_contiguous(parser, type, &fields[0], token)) < 0)
    return code;
  if ((code = parse_int8(parser, type, &fields[0], rdata, token)) < 0)
    return code;
  if ((code = take_contiguous(parser, type, &fields[1], token)) < 0)
    return code;
  if ((code = parse_int8(parser, type, &fields[1], rdata, token)) < 0)
    return code;
  if ((code = take_contiguous(parser, type, &fields[2], token)) < 0)
    return code;
  if ((code = parse_int8(parser, type, &fields[2], rdata, token)) < 0)
    return code;
  take(parser, token);
  if ((code = parse_base16_sequence(parser, type, &fields[3], rdata, token)) < 0)
    return code;

  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t check_hip_rr(
  parser_t *parser, const type_info_t *type, const rdata_t *rdata)
{
  // FIXME: verify field lengths etc
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t parse_hip_rdata(
  parser_t *parser, const type_info_t *type, rdata_t *rdata, token_t *token)
{
  int32_t code;
  const rdata_info_t *fields = type->rdata.fields;
  uint8_t *octets = rdata->octets;

  // reserve octet for HIT length
  rdata->octets += 1;

  // PK algorithm
  if ((code = have_contiguous(parser, type, &fields[1], token)) < 0)
    return code;
  if ((code = parse_int8(parser, type, &fields[1], rdata, token)) < 0)
    return code;

  // reserve octets for PK length
  rdata->octets += 2;

  // HIT
  if ((code = take_contiguous(parser, type, &fields[3], token)) < 0)
    return code;
  if ((code = parse_base16(parser, type, &fields[3], rdata, token)) < 0)
    return code;

  if ((rdata->octets - octets) > 255 + 4)
    SYNTAX_ERROR(parser, "Invalid %s in %s", NAME(&fields[3]), NAME(type));
  uint8_t hit_length = (uint8_t)((rdata->octets - octets) - 4);
  octets[0] = hit_length;

  // Public Key
  if ((code = take_contiguous(parser, type, &fields[4], token)) < 0)
    return code;
  if ((code = parse_base64(parser, type, &fields[4], rdata, token)) < 0)
    return code;

  uint16_t pk_length = htobe16((uint16_t)(((rdata->octets - octets) - hit_length) - 4));
  memcpy(&octets[2], &pk_length, sizeof(pk_length));

  take(parser, token);
  while (is_contiguous(token)) {
    if ((code = parse_name(parser, type, &fields[5], rdata, token)) < 0)
      return code;
    take(parser, token);
  }

  if ((code = have_delimiter(parser, type, token)) < 0)
    return code;
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t check_openpgpkey_rr(
  parser_t *parser, const type_info_t *type, const rdata_t *rdata)
{
  // FIXME: as the RDATA contains a digest, it is likely we can make this
  //        check stricter, at least, for known algorithms
  if ((uintptr_t)rdata->octets - (uintptr_t)parser->rdata->octets < 4)
    SYNTAX_ERROR(parser, "Invalid %s", NAME(type));
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t parse_openpgpkey_rdata(
  parser_t *parser, const type_info_t *type, rdata_t *rdata, token_t *token)
{
  int32_t code;
  const rdata_info_t *fields = type->rdata.fields;

  if ((code = parse_base64_sequence(parser, type, &fields[0], rdata, token)) < 0)
    return code;

  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t check_csync_rr(
  parser_t *parser, const type_info_t *type, const rdata_t *rdata)
{
  int32_t r;
  size_t c = 0;
  const size_t n = (uintptr_t)rdata->octets - (uintptr_t)parser->rdata->octets;
  const uint8_t *o = parser->rdata->octets;
  const rdata_info_t *f = type->rdata.fields;

  if ((r = check(&c, check_int32(parser, type, &f[0], o, n))) ||
      (r = check(&c, check_int16(parser, type, &f[1], o+c, n-c))) ||
      (r = check(&c, check_nsec(parser, type, &f[2], o+c, n-c))))
    return r;

  if (c != n)
    SYNTAX_ERROR(parser, "Invalid %s", NAME(type));
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t parse_csync_rdata(
  parser_t *parser, const type_info_t *type, rdata_t *rdata, token_t *token)
{
  int32_t code;
  const rdata_info_t *fields = type->rdata.fields;

  if ((code = have_contiguous(parser, type, &fields[0], token)) < 0)
    return code;
  if ((code = parse_int32(parser, type, &fields[0], rdata, token)) < 0)
    return code;
  if ((code = take_contiguous(parser, type, &fields[1], token)) < 0)
    return code;
  if ((code = parse_int16(parser, type, &fields[1], rdata, token)) < 0)
    return code;
  take(parser, token);
  if ((code = parse_nsec(parser, type, &fields[2], rdata, token)) < 0)
    return code;

  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t check_zonemd_rr(
  parser_t *parser, const type_info_t *type, const rdata_t *rdata)
{
  int32_t r;
  size_t c = 0;
  const size_t n = (uintptr_t)rdata->octets - (uintptr_t)parser->rdata->octets;
  const uint8_t *o = parser->rdata->octets;
  const rdata_info_t *f = type->rdata.fields;

  if ((r = check(&c, check_int32(parser, type, &f[0], o, n))) ||
      (r = check(&c, check_int8(parser, type, &f[1], o+c, n-c))) ||
      (r = check(&c, check_int8(parser, type, &f[2], o+c, n-c))))
    return r;

  const uint8_t digest_algorithm = parser->rdata->octets[5];
  if ((digest_algorithm & 0x3) == digest_algorithm) {
    // https://www.iana.org/assignments/dns-parameters#zonemd-hash-algorithms
    static const uint8_t digest_sizes[4] = {
      0,  // 0: Reserved
      48, // 1: SHA-384
      64, // 2: SHA-512
      0   // 3: Unassigned
    };

    const uint8_t digest_size = digest_sizes[ digest_algorithm ];
    if (digest_size && n - 6 != digest_size)
      SEMANTIC_ERROR(parser, "Invalid digest in %s", NAME(type));
  }

  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t parse_zonemd_rdata(
  parser_t *parser, const type_info_t *type, rdata_t *rdata, token_t *token)
{
  int32_t code;
  const rdata_info_t *fields = type->rdata.fields;

  if ((code = have_contiguous(parser, type, &fields[0], token)) < 0)
    return code;
  if ((code = parse_int32(parser, type, &fields[0], rdata, token)) < 0)
    return code;
  if ((code = take_contiguous(parser, type, &fields[1], token)) < 0)
    return code;
  if ((code = parse_int8(parser, type, &fields[1], rdata, token)) < 0)
    return code;
  if ((code = take_contiguous(parser, type, &fields[2], token)) < 0)
    return code;
  if ((code = parse_int8(parser, type, &fields[2], rdata, token)) < 0)
    return code;
  take(parser, token);
  if ((code = parse_base16_sequence(parser, type, &fields[3], rdata, token)) < 0)
    return code;

  const uint8_t digest_algorithm = parser->rdata->octets[5];
  if ((digest_algorithm & 0x3) == digest_algorithm) {
    // https://www.iana.org/assignments/dns-parameters#zonemd-hash-algorithms
    static const uint8_t digest_sizes[4] = {
      0,  // 0: Reserved
      48, // 1: SHA-384
      64, // 2: SHA-512
      0   // 3: Unassigned
    };

    const uint8_t digest_size = digest_sizes[ digest_algorithm ];
    size_t length = (uintptr_t)rdata->octets - (uintptr_t)parser->rdata->octets;
    if (digest_size && length - 6 != digest_size)
      SEMANTIC_ERROR(parser, "Invalid digest in %s", NAME(type));
  }

  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t check_svcb_rr(
  parser_t *parser, const type_info_t *type, const rdata_t *rdata)
{
  //
  // FIXME: implement checking parameters etc
  //
  // - check if all keys in mandatory exist
  // - check if at least keys and key lengths are valid
  //
  // FIXME: implement reordering parameters in strict (primary) mode
  // FIXME: note that when reordering or checking, rdata may not actually
  //        contain valid parameters
  //

  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t parse_svcb_rdata(
  parser_t *parser, const type_info_t *type, rdata_t *rdata, token_t *token)
{
  int32_t code;
  const rdata_info_t *fields = type->rdata.fields;

  if ((code = have_contiguous(parser, type, &fields[0], token)) < 0)
    return code;
  if ((code = parse_int16(parser, type, &fields[0], rdata, token)) < 0)
    return code;
  if ((code = take_contiguous(parser, type, &fields[1], token)) < 0)
    return code;
  if ((code = parse_name(parser, type, &fields[1], rdata, token)) < 0)
    return code;
  take(parser, token);
  if ((code = parse_svc_params(parser, type, &fields[2], rdata, token)) < 0)
    return code;

  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t check_https_rr(
  parser_t *parser, const type_info_t *type, const rdata_t *rdata)
{
  //
  // FIXME: incorporate fixes mentioned in check_svcb_rr
  //

  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t parse_https_rdata(
  parser_t *parser, const type_info_t *type, rdata_t *rdata, token_t *token)
{
  int32_t code;
  const rdata_info_t *fields = type->rdata.fields;

  if ((code = have_contiguous(parser, type, &fields[0], token)) < 0)
    return code;
  if ((code = parse_int16(parser, type, &fields[0], rdata, token)) < 0)
    return code;
  if ((code = take_contiguous(parser, type, &fields[1], token)) < 0)
    return code;
  if ((code = parse_name(parser, type, &fields[1], rdata, token)) < 0)
    return code;
  take(parser, token);
  if ((code = parse_svc_params(parser, type, &fields[2], rdata, token)) < 0)
    return code;

  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t check_dsync_rr(
  parser_t *parser, const type_info_t *type, const rdata_t *rdata)
{
  int32_t r;
  size_t c = 0;
  const size_t n = (uintptr_t)rdata->octets - (uintptr_t)parser->rdata->octets;
  const uint8_t *o = parser->rdata->octets;
  const rdata_info_t *f = type->rdata.fields;

  if ((r = check(&c, check_int16(parser, type, &f[0], o, n))) ||
      (r = check(&c, check_int8(parser, type, &f[1], o+c, n-c))) ||
      (r = check(&c, check_int16(parser, type, &f[2], o+c, n-c))) ||
      (r = check(&c, check_name(parser, type, &f[3], o+c, n-c))))
    return r;

  const uint8_t dsync_scheme = o[2];
  uint16_t dsync_type;
  memcpy(&dsync_type, o, sizeof(dsync_type));
  dsync_type = be16toh(dsync_type);
  if (dsync_scheme == 1 && dsync_type != ZONE_TYPE_CDS
                        && dsync_type != ZONE_TYPE_CSYNC)
    SEMANTIC_ERROR(parser, "Wrong type for scheme 1 in %s", NAME(type));

  if (c > n)
    SYNTAX_ERROR(parser, "Invalid %s", NAME(type));
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t parse_dsync_rdata(
  parser_t *parser, const type_info_t *type, rdata_t *rdata, token_t *token)
{
  int32_t code;
  const rdata_info_t *fields = type->rdata.fields;

  if ((code = have_contiguous(parser, type, &fields[0], token)) < 0)
    return code;
  if ((code = parse_type(parser, type, &fields[0], rdata, token)) < 0)
    return code;
  if ((code = take_contiguous(parser, type, &fields[1], token)) < 0)
    return code;
  if ((code = parse_int8(parser, type, &fields[1], rdata, token)) < 0)
    return code;
  if ((code = take_contiguous(parser, type, &fields[2], token)) < 0)
    return code;
  if ((code = parse_int16(parser, type, &fields[2], rdata, token)) < 0)
    return code;
  if ((code = take_contiguous(parser, type, &fields[3], token)) < 0)
    return code;
  if ((code = parse_name(parser, type, &fields[3], rdata, token)) < 0)
    return code;

  const uint8_t dsync_scheme = parser->rdata->octets[2];
  uint16_t dsync_type;
  memcpy(&dsync_type, parser->rdata->octets, sizeof(dsync_type));
  dsync_type = be16toh(dsync_type);
  if (dsync_scheme == 1 && dsync_type != ZONE_TYPE_CDS
                        && dsync_type != ZONE_TYPE_CSYNC)
    SEMANTIC_ERROR(parser, "Wrong type for scheme 1 in %s", NAME(type));

  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t check_nid_rr(
  parser_t *parser, const type_info_t *type, const rdata_t *rdata)
{
  int32_t r;
  size_t c = 0;
  const size_t n = (uintptr_t)rdata->octets - (uintptr_t)parser->rdata->octets;
  const uint8_t *o = parser->rdata->octets;
  const rdata_info_t *f = type->rdata.fields;

  if ((r = check(&c, check_int16(parser, type, &f[0], o, n))) ||
      (r = check(&c, check_ilnp64(parser, type, &f[1], o+c, n-c))))
    return r;
  if (c != n)
    SYNTAX_ERROR(parser, "Invalid %s", NAME(type));
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t parse_nid_rdata(
  parser_t *parser, const type_info_t *type, rdata_t *rdata, token_t *token)
{
  int32_t code;
  const rdata_info_t *fields = type->rdata.fields;

  if ((code = have_contiguous(parser, type, &fields[0], token)) < 0)
    return code;
  if ((code = parse_int16(parser, type, &fields[0], rdata, token)) < 0)
    return code;
  if ((code = take_contiguous(parser, type, &fields[1], token)) < 0)
    return code;
  if ((code = parse_ilnp64(parser, type, &fields[1], rdata, token)) < 0)
    return code;
  if ((code = take_delimiter(parser, type, token)) < 0)
    return code;
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t check_l32_rr(
  parser_t *parser, const type_info_t *type, const rdata_t *rdata)
{
  int32_t r;
  size_t c = 0;
  const size_t n = (uintptr_t)rdata->octets - (uintptr_t)parser->rdata->octets;
  const uint8_t *o = parser->rdata->octets;
  const rdata_info_t *f = type->rdata.fields;

  if ((r = check(&c, check_int16(parser, type, &f[0], o, n))) ||
      (r = check(&c, check_ip4(parser, type, &f[1], o+c, n-c))))
    return r;

  if (c != n)
    SYNTAX_ERROR(parser, "Invalid %s", NAME(type));
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t parse_l32_rdata(
  parser_t *parser, const type_info_t *type, rdata_t *rdata, token_t *token)
{
  int32_t code;
  const rdata_info_t *fields = type->rdata.fields;

  if ((code = have_contiguous(parser, type, &fields[0], token)) < 0)
    return code;
  if ((code = parse_int16(parser, type, &fields[0], rdata, token)) < 0)
    return code;
  if ((code = take_contiguous(parser, type, &fields[1], token)) < 0)
    return code;
  if ((code = parse_ip4(parser, type, &fields[1], rdata, token)) < 0)
    return code;
  if ((code = take_delimiter(parser, type, token)) < 0)
    return code;
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t check_l64_rr(
  parser_t *parser, const type_info_t *type, const rdata_t *rdata)
{
  int32_t r;
  size_t c = 0;
  const size_t n = (uintptr_t)rdata->octets - (uintptr_t)parser->rdata->octets;
  const uint8_t *o = parser->rdata->octets;
  const rdata_info_t *f = type->rdata.fields;

  if ((r = check(&c, check_int16(parser, type, &f[0], o, n))) ||
      (r = check(&c, check_ilnp64(parser, type, &f[1], o+c, n-c))))
    return r;
  if (c != n)
    SYNTAX_ERROR(parser, "Invalid %s", NAME(type));
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t parse_l64_rdata(
  parser_t *parser, const type_info_t *type, rdata_t *rdata, token_t *token)
{
  int32_t code;
  const rdata_info_t *fields = type->rdata.fields;

  if ((code = have_contiguous(parser, type, &fields[0], token)) < 0)
    return code;
  if ((code = parse_int16(parser, type, &fields[0], rdata, token)) < 0)
    return code;
  if ((code = take_contiguous(parser, type, &fields[1], token)) < 0)
    return code;
  if ((code = parse_ilnp64(parser, type, &fields[1], rdata, token)) < 0)
    return code;
  if ((code = take_delimiter(parser, type, token)) < 0)
    return code;
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t check_eui48_rr(
  parser_t *parser, const type_info_t *type, const rdata_t *rdata)
{
  if ((uintptr_t)rdata->octets - (uintptr_t)parser->rdata->octets != 6)
    SYNTAX_ERROR(parser, "Invalid %s", NAME(type));
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t parse_eui48_rdata(
  parser_t *parser, const type_info_t *type, rdata_t *rdata, token_t *token)
{
  int32_t code;
  const rdata_info_t *fields = type->rdata.fields;

  if ((code = have_contiguous(parser, type, &fields[0], token)) < 0)
    return code;
  if ((code = parse_eui48(parser, type, &fields[0], rdata, token)) < 0)
    return code;
  if ((code = take_delimiter(parser, type, token)) < 0)
    return code;
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t check_eui64_rr(
  parser_t *parser, const type_info_t *type, const rdata_t *rdata)
{
  if ((uintptr_t)rdata->octets - (uintptr_t)parser->rdata->octets != 8)
    SYNTAX_ERROR(parser, "Invalid %s", NAME(type));
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t parse_eui64_rdata(
  parser_t *parser, const type_info_t *type, rdata_t *rdata, token_t *token)
{
  int32_t code;
  const rdata_info_t *fields = type->rdata.fields;

  if ((code = have_contiguous(parser, type, &fields[0], token)) < 0)
    return code;
  if ((code = parse_eui64(parser, type, &fields[0], rdata, token)) < 0)
    return code;
  if ((code = take_delimiter(parser, type, token)) < 0)
    return code;
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t check_uri_rr(
  parser_t *parser, const type_info_t *type, const rdata_t *rdata)
{
  int32_t r;
  size_t c = 0;
  const size_t n = (uintptr_t)rdata->octets - (uintptr_t)parser->rdata->octets;
  const uint8_t *o = parser->rdata->octets;
  const rdata_info_t *f = type->rdata.fields;

  if ((r = check(&c, check_int16(parser, type, &f[0], o, n))) ||
      (r = check(&c, check_int16(parser, type, &f[1], o+c, n-c))))
    return r;
  if (c >= n)
    SYNTAX_ERROR(parser, "Invalid %s", NAME(type));
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t parse_uri_rdata(
  parser_t *parser, const type_info_t *type, rdata_t *rdata, token_t *token)
{
  int32_t code;
  const rdata_info_t *fields = type->rdata.fields;

  if ((code = have_contiguous(parser, type, &fields[0], token)) < 0)
    return code;
  if ((code = parse_int16(parser, type, &fields[0], rdata, token)) < 0)
    return code;
  if ((code = take_contiguous(parser, type, &fields[1], token)) < 0)
    return code;
  if ((code = parse_int16(parser, type, &fields[1], rdata, token)) < 0)
    return code;
  if ((code = take_quoted(parser, type, &fields[2], token)) < 0)
    return code;
  if ((code = parse_text(parser, type, &fields[2], rdata, token)) < 0)
    return code;
  if ((code = take_delimiter(parser, type, token)) < 0)
    return code;
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t check_caa_rr(
  parser_t *parser, const type_info_t *type, const rdata_t *rdata)
{
  int32_t r;
  size_t c = 0;
  const size_t n = (uintptr_t)rdata->octets - (uintptr_t)parser->rdata->octets;
  const uint8_t *o = parser->rdata->octets;
  const rdata_info_t *f = type->rdata.fields;

  if ((r = check(&c, check_int8(parser, type, &f[0], o, n))) ||
      (r = check(&c, check_int8(parser, type, &f[1], o+c, n-c))))
    return r;
  if (c >= n)
    SYNTAX_ERROR(parser, "Invalid %s", NAME(type));
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t parse_caa_rdata(
  parser_t *parser, const type_info_t *type, rdata_t *rdata, token_t *token)
{
  int32_t code;
  const rdata_info_t *fields = type->rdata.fields;

  if ((code = have_contiguous(parser, type, &fields[0], token)) < 0)
    return code;
  if ((code = parse_int8(parser, type, &fields[0], rdata, token)) < 0)
    return code;
  if ((code = take_contiguous(parser, type, &fields[1], token)) < 0)
    return code;
  if ((code = parse_caa_tag(parser, type, &fields[1], rdata, token)) < 0)
    return code;
  if ((code = take_quoted_or_contiguous(parser, type, &fields[2], token)) < 0)
    return code;
  if ((code = parse_text(parser, type, &fields[2], rdata, token)) < 0)
    return code;
  if ((code = take_delimiter(parser, type, token)) < 0)
    return code;
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t check_doa_rr(
  parser_t *parser, const type_info_t *type, const rdata_t *rdata)
{
  int32_t r;
  size_t c = 0;
  const size_t n = (uintptr_t)rdata->octets - (uintptr_t)parser->rdata->octets;
  const uint8_t *o = parser->rdata->octets;
  const rdata_info_t *f = type->rdata.fields;

  if ((r = check(&c, check_int32(parser, type, &f[0], o, n))) ||
      (r = check(&c, check_int32(parser, type, &f[1], o+c, n-c))) ||
      (r = check(&c, check_int8(parser, type, &f[2], o+c, n-c))) ||
      (r = check(&c, check_string(parser, type, &f[3], o+c, n-c))))
    return r;
  if (c > n)
    SYNTAX_ERROR(parser, "Invalid %s", NAME(type));
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t parse_doa_rdata(
  parser_t *parser, const type_info_t *type, rdata_t *rdata, token_t *token)
{
  int32_t code;
  const rdata_info_t *fields = type->rdata.fields;

  if ((code = have_contiguous(parser, type, &fields[0], token)) < 0)
    return code;
  if ((code = parse_int32(parser, type, &fields[0], rdata, token)) < 0)
    return code;
  if ((code = take_contiguous(parser, type, &fields[1], token)) < 0)
    return code;
  if ((code = parse_int32(parser, type, &fields[1], rdata, token)) < 0)
    return code;
  if ((code = take_contiguous(parser, type, &fields[2], token)) < 0)
    return code;
  if ((code = parse_int8(parser, type, &fields[2], rdata, token)) < 0)
    return code;
  if ((code = take_quoted_or_contiguous(parser, type, &fields[3], token)) < 0)
    return code;
  if ((code = parse_string(parser, type, &fields[3], rdata, token)) < 0)
    return code;
  take(parser, token);
  if (!(token->length == 1 && ((char)*token->data == '0' || (char)*token->data == '-'))
  &&  (code = parse_base64_sequence(parser, type, &fields[4], rdata, token)) < 0)
    return code;
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t check_amtrelay_rr(
  parser_t *parser, const type_info_t *type, const rdata_t *rdata);

nonnull_all
static int32_t parse_amtrelay_rdata(
  parser_t *parser, const type_info_t *type, rdata_t *rdata, token_t *token);

diagnostic_push()
gcc_diagnostic_ignored(missing-field-initializers)
clang_diagnostic_ignored(missing-field-initializers)

static const rdata_info_t amtrelay_ipv4_rdata_fields[] = {
  FIELD("precedence"),
  FIELD("discovery optional"),
  FIELD("type"),
  FIELD("relay")
};

static const type_info_t amtrelay_ipv4[] = {
  TYPE("AMTRELAY", ZONE_TYPE_AMTRELAY, ZONE_CLASS_IN, FIELDS(amtrelay_ipv4_rdata_fields),
                   check_amtrelay_rr, parse_amtrelay_rdata),
};

static const rdata_info_t amtrelay_ipv6_rdata_fields[] = {
  FIELD("precedence"),
  FIELD("discovery optional"),
  FIELD("type"),
  FIELD("relay")
};

static const type_info_t amtrelay_ipv6[] = {
  TYPE("AMTRELAY", ZONE_TYPE_AMTRELAY, ZONE_CLASS_IN, FIELDS(amtrelay_ipv6_rdata_fields),
                   check_amtrelay_rr, parse_amtrelay_rdata),
};

diagnostic_pop()

nonnull_all
static int32_t check_amtrelay_rr(
  parser_t *parser, const type_info_t *type, const rdata_t *rdata)
{
  int32_t r;
  size_t c = 0;
  const size_t n = (uintptr_t)rdata->octets - (uintptr_t)parser->rdata->octets;
  const uint8_t *o = parser->rdata->octets;
  const type_info_t *t = type;
  const rdata_info_t *f = type->rdata.fields;

  if ((r = check(&c, check_int8(parser, type, &f[0], o, n))) ||
      (r = check(&c, check_int8(parser, type, &f[2], o+c, n-c))))
    return r;

  switch (parser->rdata->octets[1] & 0x7f) {
    case 1: /* IPv4 address */
      t = (const type_info_t *)amtrelay_ipv4;
      f = amtrelay_ipv4_rdata_fields;
      if ((r = check(&c, check_ip4(parser, t, &f[3], o+c, n-c))) < 0)
        return r;
      break;
    case 2: /* IPv6 address */
      t = (const type_info_t *)amtrelay_ipv6;
      f = amtrelay_ipv6_rdata_fields;
      if ((r = check(&c, check_ip6(parser, t, &f[3], o+c, n-c))) < 0)
        return r;
      break;
    case 0: /* no gateway */
      break;
    case 3: /* domain name */
      if ((r = check(&c, check_name(parser, t, &f[3], o+c, n-c))) < 0)
        return r;
      break;
    default:
      SYNTAX_ERROR(parser, "Invalid %s", NAME(type));
  }
  if (c < n)
    SYNTAX_ERROR(parser, "Trailing data in %s", NAME(t));
  return accept_rr(parser, t, rdata);
}

nonnull_all
static int32_t parse_amtrelay_rdata(
  parser_t *parser, const type_info_t *type, rdata_t *rdata, token_t *token)
{
  int32_t code;
  const rdata_info_t *fields = type->rdata.fields;
  uint8_t *octets = rdata->octets;
  uint8_t D;

  if ((code = have_contiguous(parser, type, &fields[0], token)) < 0)
    return code;
  if ((code = parse_int8(parser, type, &fields[0], rdata, token)) < 0)
    return code;

  if ((code = take_contiguous(parser, type, &fields[1], token)) < 0)
    return code;
  if (token->length != 1)
    SYNTAX_ERROR(parser, "Invalid %s in %s", NAME(&fields[1]), NAME(type));
  switch((char)*token->data) {
    case '0':
      D = 0x00;
      break;
    case '1':
      D = 0x80;
      break;
    default :
      SYNTAX_ERROR(parser, "Invalid %s in %s", NAME(&fields[1]), NAME(type));
  }

  if ((code = take_contiguous(parser, type, &fields[2], token)) < 0)
    return code;
  if ((code = parse_int8(parser, type, &fields[2], rdata, token)) < 0)
    return code;

  if (octets[1]) {
    if ((code = take_contiguous(parser, type, &fields[3], token)) < 0)
      return code;
    switch (octets[1]) {
      case 1: /* IPv4 address */
        type = (const type_info_t *)amtrelay_ipv4;
        fields = type->rdata.fields;
        if ((code = parse_ip4(parser, type, &fields[3], rdata, token)) < 0)
          return code;
        break;
      case 2: /* IPv6 address */
        type = (const type_info_t *)amtrelay_ipv6;
        fields = type->rdata.fields;
        if ((code = parse_ip6(parser, type, &fields[3], rdata, token)) < 0)
          return code;
        break;
      case 3: /* domain name */
        if ((code = parse_name(parser, type, &fields[3], rdata, token)) < 0)
          return code;
        break;
      default:
        SYNTAX_ERROR(parser, "Invalid %s in %s", NAME(&fields[3]), NAME(type));
    }
  }
  octets[1] |= D;
  if ((code = take_delimiter(parser, type, token)) < 0)
    return code;
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t check_ipn_rr(
  parser_t *parser, const type_info_t *type, const rdata_t *rdata)
{
  int32_t r;
  size_t c = 0;
  const size_t n = (uintptr_t)rdata->octets - (uintptr_t)parser->rdata->octets;
  const uint8_t *o = parser->rdata->octets;
  const rdata_info_t *f = type->rdata.fields;

  if ((r = check(&c, check_int64(parser, type, &f[0], o, n))))
    return r;
  if (c > n)
    SYNTAX_ERROR(parser, "Invalid %s", NAME(type));
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t parse_ipn_rdata(
  parser_t *parser, const type_info_t *type, rdata_t *rdata, token_t *token)
{
  int32_t code;
  const rdata_info_t *fields = type->rdata.fields;
  token_t left, right;

  /* draft-johnson-dns-ipn-cla-07 Section 3.1. IPN:
   *   Presentation format for these resource records are either a 64 bit
   *   unsigned decimal integer, or two 32 bit unsigned decimal integers
   *   delimited by a period with the most significant 32 bits first and least
   *   significant 32 bits last.
   */
  if ((code = have_contiguous(parser, type, &fields[0], token)) < 0)
    return code;
  if (!(right.data = memchr(token->data, '.', token->length))) {
    if ((code = parse_int64(parser, type, &fields[0], rdata, token)) < 0)
      return code;
    if ((code = take_delimiter(parser, type, token)) < 0)
      return code;
    return accept_rr(parser, type, rdata);
  }
  left.code = token->code;
  left.data = token->data;
  left.length = (size_t)(right.data - token->data);
  right.code = token->code;
  right.data += 1;
  right.length = token->length - left.length - 1;
  if ((code = parse_int32(parser, type, &fields[0], rdata, &left)) < 0)
	  return code;
  if ((code = parse_int32(parser, type, &fields[0], rdata, &right)) < 0)
	  return code;
  if ((code = take_delimiter(parser, type, token)) < 0)
    return code;
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t check_generic_rr(
  parser_t *parser, const type_info_t *type, const rdata_t *rdata)
{
  return accept_rr(parser, type, rdata);
}

nonnull_all
static int32_t parse_generic_rdata(
  parser_t *parser, const type_info_t *type, rdata_t *rdata, token_t *token)
{
  int32_t code;
  uint16_t rdlength;
  static const rdata_info_t fields[] = {
    FIELD("rdlength"),
    FIELD("rdata")
  };

  // discard '\#'
  if ((code = take_contiguous(parser, type, &fields[0], token)) < 0)
    return code;
  if (!scan_int16(token->data, token->length, &rdlength))
    SYNTAX_ERROR(parser, "Invalid RDLENGTH in %s", NAME(type));

  take(parser, token);
  if (is_contiguous(token)) {
    struct base16_state state = { .eof = 0, .bytes = 0, .carry = 0 };

    do {
      size_t length = token->length + 1 / 2;
      if (length > (uintptr_t)rdata->limit - (uintptr_t)rdata->octets)
        SYNTAX_ERROR(parser, "Invalid RDATA in %s", NAME(type));
      if (!base16_stream_decode(&state, token->data, token->length, rdata->octets, &length))
        SYNTAX_ERROR(parser, "Invalid RDATA in %s", NAME(type));
      rdata->octets += length;
      take(parser, token);
    } while (is_contiguous(token));

    if (state.bytes)
      *rdata->octets++ = state.carry;
  }

  if ((code = have_delimiter(parser, type, token)) < 0)
    return code;
  if (rdata->octets - parser->rdata->octets != rdlength)
    SYNTAX_ERROR(parser, "Invalid RDATA in %s", NAME(type));
  return type->check(parser, type, rdata);
}

nonnull_all
static int32_t parse_unknown_rdata(
  parser_t *parser, const type_info_t *type, rdata_t *rdata, token_t *token)
{
  (void)type;
  (void)rdata;
  (void)token;
  SYNTAX_ERROR(parser, "Unknown record type");
}

diagnostic_push()
gcc_diagnostic_ignored(missing-field-initializers)
clang_diagnostic_ignored(missing-field-initializers)

static const class_info_t classes[] = {
  UNKNOWN_CLASS(0),
  CLASS("IN", ZONE_CLASS_IN),
  CLASS("CS", ZONE_CLASS_CS),
  CLASS("CH", ZONE_CLASS_CH),
  CLASS("HS", ZONE_CLASS_HS)
};

static const rdata_info_t a_rdata_fields[] = {
  FIELD("address")
};

static const rdata_info_t ns_rdata_fields[] = {
  FIELD("host")
};

static const rdata_info_t md_rdata_fields[] = {
  FIELD("madname")
};

static const rdata_info_t mf_rdata_fields[] = {
  FIELD("madname")
};

static const rdata_info_t cname_rdata_fields[] = {
  FIELD("host")
};

static const rdata_info_t soa_rdata_fields[] = {
  FIELD("primary"),
  FIELD("mailbox"),
  FIELD("serial"),
  FIELD("refresh"),
  FIELD("retry"),
  FIELD("expire"),
  FIELD("minimum")
};

static const rdata_info_t mb_rdata_fields[] = {
  FIELD("madname")
};

static const rdata_info_t mg_rdata_fields[] = {
  FIELD("mgmname")
};

static const rdata_info_t mr_rdata_fields[] = {
  FIELD("newname")
};

static const rdata_info_t ptr_rdata_fields[] = {
  FIELD("ptrdname")
};

static const rdata_info_t hinfo_rdata_fields[] = {
  FIELD("cpu"),
  FIELD("os")
};

static const rdata_info_t minfo_rdata_fields[] = {
  FIELD("rmailbx"),
  FIELD("emailbx")
};

static const rdata_info_t null_rdata_fields[] = {
  FIELD("anything")
};

static const rdata_info_t wks_rdata_fields[] = {
  FIELD("address"),
  FIELD("protocol"),
  FIELD("bitmap")
};

static const rdata_info_t mx_rdata_fields[] = {
  FIELD("priority"),
  FIELD("hostname")
};

static const rdata_info_t txt_rdata_fields[] = {
  FIELD("text")
};

static const rdata_info_t rp_rdata_fields[] = {
  FIELD("mailbox"),
  FIELD("text")
};

static const rdata_info_t afsdb_rdata_fields[] = {
  FIELD("subtype"),
  FIELD("hostname")
};

static const rdata_info_t x25_rdata_fields[] = {
  FIELD("address")
};

static const rdata_info_t isdn_rdata_fields[] = {
  FIELD("address"),
  FIELD("subaddress")
};

static const rdata_info_t rt_rdata_fields[] = {
  FIELD("preference"),
  FIELD("hostname")
};

static const rdata_info_t nsap_rdata_fields[] = {
  FIELD("address")
};

static const rdata_info_t nsap_ptr_rdata_fields[] = {
  FIELD("hostname")
};

static const rdata_info_t key_rdata_fields[] = {
  FIELD("flags"),
  FIELD("protocol"),
  FIELD("algorithm"),
  FIELD("publickey")
};

static const rdata_info_t px_rdata_fields[] = {
  FIELD("preference"),
  FIELD("map822"),
  FIELD("mapx400")
};

static const rdata_info_t gpos_rdata_fields[] = {
  FIELD("latitude"),
  FIELD("longitude"),
  FIELD("altitude")
};

static const rdata_info_t aaaa_rdata_fields[] = {
  FIELD("address")
};

static const rdata_info_t loc_rdata_fields[] = {
  FIELD("version"),
  FIELD("size"),
  FIELD("horizontal precision"),
  FIELD("vertical precision"),
  FIELD("latitude"),
  FIELD("longitude"),
  FIELD("altitude")
};

static const rdata_info_t nxt_rdata_fields[] = {
  FIELD("next domain name"),
  FIELD("type bit map")
};

static const rdata_info_t eid_rdata_fields[] = {
  FIELD("end point identifier")
};

static const rdata_info_t nimloc_rdata_fields[] = {
  FIELD("nimrod locator")
};

static const rdata_info_t srv_rdata_fields[] = {
  FIELD("priority"),
  FIELD("weight"),
  FIELD("port"),
  FIELD("target")
};

static const rdata_info_t atma_rdata_fields[] = {
  FIELD("address")
};

static const rdata_info_t naptr_rdata_fields[] = {
  FIELD("order"),
  FIELD("preference"),
  FIELD("flags"),
  FIELD("services"),
  FIELD("regex"),
  FIELD("replacement"),
};

static const rdata_info_t kx_rdata_fields[] = {
  FIELD("preference"),
  FIELD("exchanger")
};

static const rdata_info_t sig_rdata_fields[] = {
  FIELD("sigtype"),
  FIELD("algorithm"),
  FIELD("labels"),
  FIELD("origttl"),
  FIELD("expire"),
  FIELD("inception"),
  FIELD("keytag"),
  FIELD("signer"),
  FIELD("signature")
};

static const rdata_info_t cert_rdata_fields[] = {
  FIELD("type"),
  FIELD("key tag"),
  FIELD("algorithm"),
  FIELD("certificate")
};

static const rdata_info_t dname_rdata_fields[] = {
  FIELD("source")
};

static const rdata_info_t sink_rdata_fields[] = {
  FIELD("coding"),
  FIELD("subcoding"),
  FIELD("data")
};

static const rdata_info_t apl_rdata_fields[] = {
  FIELD("prefix")
};

static const rdata_info_t ds_rdata_fields[] = {
  FIELD("keytag"),
  FIELD("algorithm"),
  FIELD("digtype"),
  FIELD("digest")
};

static const rdata_info_t sshfp_rdata_fields[] = {
  FIELD("algorithm"),
  FIELD("ftype"),
  FIELD("fingerprint")
};

// IPSECKEY is different because the rdata depends on the algorithm
static const rdata_info_t ipseckey_rdata_fields[] = {
  FIELD("precedence"),
  FIELD("gateway type"),
  FIELD("algorithm"),
  FIELD("gateway"),
  FIELD("public key")
};

static const rdata_info_t rrsig_rdata_fields[] = {
  FIELD("rrtype"),
  FIELD("algorithm"),
  FIELD("labels"),
  FIELD("origttl"),
  FIELD("expire"),
  FIELD("inception"),
  FIELD("keytag"),
  FIELD("signer"),
  FIELD("signature")
};

static const rdata_info_t nsec_rdata_fields[] = {
  FIELD("next"),
  FIELD("types")
};

static const rdata_info_t dnskey_rdata_fields[] = {
  FIELD("flags"),
  FIELD("protocol"),
  FIELD("algorithm"),
  FIELD("publickey")
};

static const rdata_info_t dhcid_rdata_fields[] = {
  FIELD("dhcpinfo")
};

static const rdata_info_t nsec3_rdata_fields[] = {
  FIELD("algorithm"),
  FIELD("flags"),
  FIELD("iterations"),
  FIELD("salt"),
  FIELD("next"),
  FIELD("types")
};

static const rdata_info_t nsec3param_rdata_fields[] = {
  FIELD("algorithm"),
  FIELD("flags"),
  FIELD("iterations"),
  FIELD("salt")
};

static const rdata_info_t tlsa_rdata_fields[] = {
  FIELD("usage"),
  FIELD("selector"),
  FIELD("matching type"),
  FIELD("certificate association data")
};

static const rdata_info_t smimea_rdata_fields[] = {
  FIELD("usage"),
  FIELD("selector"),
  FIELD("matching type"),
  FIELD("certificate association data")
};

static const rdata_info_t cds_rdata_fields[] = {
  FIELD("keytag"),
  FIELD("algorithm"),
  FIELD("digtype"),
  FIELD("digest")
};

static const rdata_info_t cdnskey_rdata_fields[] = {
  FIELD("flags"),
  FIELD("protocol"),
  FIELD("algorithm"),
  FIELD("publickey")
};

static const rdata_info_t hip_rdata_fields[] = {
  FIELD("HIT length"),
  FIELD("PK algorithm"),
  FIELD("PK length"),
  FIELD("HIT"),
  FIELD("Public Key"),
  FIELD("Rendezvous Servers")
};

// https://www.iana.org/assignments/dns-parameters/NINFO/ninfo-completed-template
static const rdata_info_t ninfo_rdata_fields[] = {
  FIELD("text")
};

// https://www.iana.org/assignments/dns-parameters/RKEY/rkey-completed-template
static const rdata_info_t rkey_rdata_fields[] = {
  FIELD("flags"),
  FIELD("protocol"),
  FIELD("algorithm"),
  FIELD("publickey")
};

// https://www.iana.org/assignments/dns-parameters/TALINK/talink-completed-template
static const rdata_info_t talink_rdata_fields[] = {
  FIELD("start or previous"),
  FIELD("end or next")
};

static const rdata_info_t openpgpkey_rdata_fields[] = {
  FIELD("key")
};

static const rdata_info_t csync_rdata_fields[] = {
  FIELD("serial"),
  FIELD("flags"),
  FIELD("types")
};

static const rdata_info_t zonemd_rdata_fields[] = {
  FIELD("serial"),
  FIELD("scheme"),
  FIELD("algorithm"),
  FIELD("digest"),
};

static const rdata_info_t svcb_rdata_fields[] = {
  FIELD("priority"),
  FIELD("target"),
  FIELD("params")
};

static const rdata_info_t https_rdata_fields[] = {
  FIELD("priority"),
  FIELD("target"),
  FIELD("params")
};

static const rdata_info_t dsync_rdata_fields[] = {
  FIELD("rrtype"),
  FIELD("scheme"),
  FIELD("port"),
  FIELD("target")
};

static const rdata_info_t spf_rdata_fields[] = {
  FIELD("text")
};

static const rdata_info_t nid_rdata_fields[] = {
  FIELD("preference"),
  FIELD("nodeid")
};

// RFC6742 specifies the syntax for the locator is compatible with the syntax
// for IPv4 addresses, but then proceeds to provide an example with leading
// zeroes. The example is corrected in the errata.
static const rdata_info_t l32_rdata_fields[] = {
  FIELD("preference"),
  FIELD("locator")
};

static const rdata_info_t l64_rdata_fields[] = {
  FIELD("preference"),
  FIELD("locator")
};

static const rdata_info_t lp_rdata_fields[] = {
  FIELD("preference"),
  FIELD("pointer")
};

static const rdata_info_t eui48_rdata_fields[] = {
  FIELD("address")
};

static const rdata_info_t eui64_rdata_fields[] = {
  FIELD("address")
};

static const rdata_info_t uri_rdata_fields[] = {
  FIELD("priority"),
  FIELD("weight"),
  FIELD("target")
};

static const rdata_info_t caa_rdata_fields[] = {
  FIELD("flags"),
  FIELD("tag"),
  FIELD("value")
};

// https://www.iana.org/assignments/dns-parameters/AVC/avc-completed-template
static const rdata_info_t avc_rdata_fields[] = {
  FIELD("text")
};

// draft-durand-doa-over-dns-02
static const rdata_info_t doa_rdata_fields[] = {
  FIELD("enterprise"),
  FIELD("type"),
  FIELD("location"),
  FIELD("media type"),
  FIELD("data")
};

// RFC 8777
// AMTRELAY is different because the rdata depends on the type
static const rdata_info_t amtrelay_rdata_fields[] = {
  FIELD("precedence"),
  FIELD("discovery optional"),
  FIELD("type"),
  FIELD("relay"),
};


// RFC 9606
static const rdata_info_t resinfo_rdata_fields[] = {
  FIELD("text")
};

// https://www.iana.org/assignments/dns-parameters/WALLET/wallet-completed-template
static const rdata_info_t wallet_rdata_fields[] = {
  FIELD("text")
};

// https://www.iana.org/assignments/dns-parameters/CLA/cla-completed-template
static const rdata_info_t cla_rdata_fields[] = {
  FIELD("text")
};

// https://www.iana.org/assignments/dns-parameters/IPN/ipn-completed-template
// and https://datatracker.ietf.org/doc/draft-johnson-dns-ipn-cla/07/
static const rdata_info_t ipn_rdata_fields[] = {
  FIELD("CBHE Node Number")
};

static const rdata_info_t ta_rdata_fields[] = {
  FIELD("key"),
  FIELD("algorithm"),
  FIELD("type"),
  FIELD("digest")
};

static const rdata_info_t dlv_rdata_fields[] = {
  FIELD("key"),
  FIELD("algorithm"),
  FIELD("type"),
  FIELD("digest")
};

// https://www.iana.org/assignments/dns-parameters/dns-parameters.xhtml
static const type_info_t types[] = {
  UNKNOWN_TYPE(0),

  TYPE("A", ZONE_TYPE_A, ZONE_CLASS_ANY, FIELDS(a_rdata_fields),
            check_a_rr, parse_a_rdata),
  TYPE("NS", ZONE_TYPE_NS, ZONE_CLASS_ANY, FIELDS(ns_rdata_fields),
             check_ns_rr, parse_ns_rdata),
  TYPE("MD", ZONE_TYPE_MD, ZONE_CLASS_ANY, FIELDS(md_rdata_fields), // obsolete
             check_ns_rr, parse_ns_rdata),
  TYPE("MF", ZONE_TYPE_MF, ZONE_CLASS_ANY, FIELDS(mf_rdata_fields), // obsolete
             check_ns_rr, parse_ns_rdata),
  TYPE("CNAME", ZONE_TYPE_CNAME, ZONE_CLASS_ANY, FIELDS(cname_rdata_fields),
                check_ns_rr, parse_ns_rdata),
  TYPE("SOA", ZONE_TYPE_SOA, ZONE_CLASS_ANY, FIELDS(soa_rdata_fields),
              check_soa_rr, parse_soa_rdata),
  TYPE("MB", ZONE_TYPE_MB, ZONE_CLASS_ANY, FIELDS(mb_rdata_fields), // experimental
             check_ns_rr, parse_ns_rdata),
  TYPE("MG", ZONE_TYPE_MG, ZONE_CLASS_ANY, FIELDS(mg_rdata_fields), // experimental
             check_ns_rr, parse_ns_rdata),
  TYPE("MR", ZONE_TYPE_MR, ZONE_CLASS_ANY, FIELDS(mr_rdata_fields), // experimental
             check_ns_rr, parse_ns_rdata),
  TYPE("NULL", ZONE_TYPE_NULL, ZONE_CLASS_ANY, FIELDS(null_rdata_fields), // experimetal
               check_generic_rr, parse_unknown_rdata),
  TYPE("WKS", ZONE_TYPE_WKS, ZONE_CLASS_IN, FIELDS(wks_rdata_fields),
              check_wks_rr, parse_wks_rdata),
  TYPE("PTR", ZONE_TYPE_PTR, ZONE_CLASS_ANY, FIELDS(ptr_rdata_fields),
              check_ns_rr, parse_ns_rdata),
  TYPE("HINFO", ZONE_TYPE_HINFO, ZONE_CLASS_ANY, FIELDS(hinfo_rdata_fields),
                check_hinfo_rr, parse_hinfo_rdata),
  TYPE("MINFO", ZONE_TYPE_MINFO, ZONE_CLASS_ANY, FIELDS(minfo_rdata_fields),
                check_minfo_rr, parse_minfo_rdata),
  TYPE("MX", ZONE_TYPE_MX, ZONE_CLASS_ANY, FIELDS(mx_rdata_fields),
             check_mx_rr, parse_mx_rdata),
  TYPE("TXT", ZONE_TYPE_TXT, ZONE_CLASS_ANY, FIELDS(txt_rdata_fields),
              check_txt_rr, parse_txt_rdata),
  TYPE("RP", ZONE_TYPE_RP, ZONE_CLASS_ANY, FIELDS(rp_rdata_fields),
             check_minfo_rr, parse_minfo_rdata),
  TYPE("AFSDB", ZONE_TYPE_AFSDB, ZONE_CLASS_ANY, FIELDS(afsdb_rdata_fields),
                check_mx_rr, parse_mx_rdata),
  TYPE("X25", ZONE_TYPE_X25, ZONE_CLASS_ANY, FIELDS(x25_rdata_fields),
              check_x25_rr, parse_x25_rdata),
  TYPE("ISDN", ZONE_TYPE_ISDN, ZONE_CLASS_ANY, FIELDS(isdn_rdata_fields),
               check_isdn_rr, parse_isdn_rdata),
  TYPE("RT", ZONE_TYPE_RT, ZONE_CLASS_ANY, FIELDS(rt_rdata_fields),
             check_rt_rr, parse_rt_rdata),
  TYPE("NSAP", ZONE_TYPE_NSAP, ZONE_CLASS_IN, FIELDS(nsap_rdata_fields),
               check_nsap_rr, parse_nsap_rdata),
  TYPE("NSAP-PTR", ZONE_TYPE_NSAP_PTR, ZONE_CLASS_IN, FIELDS(nsap_ptr_rdata_fields),
                   check_nsap_ptr_rr, parse_nsap_ptr_rdata),
  TYPE("SIG", ZONE_TYPE_SIG, ZONE_CLASS_ANY, FIELDS(sig_rdata_fields),
              check_rrsig_rr, parse_rrsig_rdata),
  TYPE("KEY", ZONE_TYPE_KEY, ZONE_CLASS_ANY, FIELDS(key_rdata_fields),
              check_key_rr, parse_key_rdata),
  TYPE("PX", ZONE_TYPE_PX, ZONE_CLASS_IN, FIELDS(px_rdata_fields),
             check_px_rr, parse_px_rdata),
  TYPE("GPOS", ZONE_TYPE_GPOS, ZONE_CLASS_ANY, FIELDS(gpos_rdata_fields),
               check_gpos_rr, parse_gpos_rdata),
  TYPE("AAAA", ZONE_TYPE_AAAA, ZONE_CLASS_IN, FIELDS(aaaa_rdata_fields),
               check_aaaa_rr, parse_aaaa_rdata),
  TYPE("LOC", ZONE_TYPE_LOC, ZONE_CLASS_ANY, FIELDS(loc_rdata_fields),
              check_loc_rr, parse_loc_rdata),
  TYPE("NXT", ZONE_TYPE_NXT, ZONE_CLASS_ANY, FIELDS(nxt_rdata_fields), // obsolete
              check_nxt_rr, parse_nxt_rdata),
  TYPE("EID", ZONE_TYPE_EID, ZONE_CLASS_IN, FIELDS(eid_rdata_fields),
              check_eid_rr, parse_eid_rdata),
  TYPE("NIMLOC", ZONE_TYPE_NIMLOC, ZONE_CLASS_IN, FIELDS(nimloc_rdata_fields),
              check_eid_rr, parse_eid_rdata),
  TYPE("SRV", ZONE_TYPE_SRV, ZONE_CLASS_IN, FIELDS(srv_rdata_fields),
              check_srv_rr, parse_srv_rdata),
  TYPE("ATMA", ZONE_TYPE_ATMA, ZONE_CLASS_IN, FIELDS(atma_rdata_fields),
               check_atma_rr, parse_atma_rdata),
  TYPE("NAPTR", ZONE_TYPE_NAPTR, ZONE_CLASS_IN, FIELDS(naptr_rdata_fields),
                check_naptr_rr, parse_naptr_rdata),
  TYPE("KX", ZONE_TYPE_KX, ZONE_CLASS_IN, FIELDS(kx_rdata_fields),
             check_mx_rr, parse_mx_rdata),
  TYPE("CERT", ZONE_TYPE_CERT, ZONE_CLASS_ANY, FIELDS(cert_rdata_fields),
               check_cert_rr, parse_cert_rdata),

  UNKNOWN_TYPE(38),

  TYPE("DNAME", ZONE_TYPE_DNAME, ZONE_CLASS_ANY, FIELDS(dname_rdata_fields),
                check_ns_rr, parse_ns_rdata),

  TYPE("SINK", ZONE_TYPE_SINK, ZONE_CLASS_ANY, FIELDS(sink_rdata_fields),
               check_sink_rr, parse_sink_rdata),
  UNKNOWN_TYPE(41),

  TYPE("APL", ZONE_TYPE_APL, ZONE_CLASS_IN, FIELDS(apl_rdata_fields),
             check_apl_rr, parse_apl_rdata),
  TYPE("DS", ZONE_TYPE_DS, ZONE_CLASS_ANY, FIELDS(ds_rdata_fields),
             check_ds_rr, parse_ds_rdata),
  TYPE("SSHFP", ZONE_TYPE_SSHFP, ZONE_CLASS_ANY, FIELDS(sshfp_rdata_fields),
                check_sshfp_rr, parse_sshfp_rdata),
  TYPE("IPSECKEY", ZONE_TYPE_IPSECKEY, ZONE_CLASS_IN, FIELDS(ipseckey_rdata_fields),
                   check_ipseckey_rr, parse_ipseckey_rdata),
  TYPE("RRSIG", ZONE_TYPE_RRSIG, ZONE_CLASS_ANY, FIELDS(rrsig_rdata_fields),
                check_rrsig_rr, parse_rrsig_rdata),
  TYPE("NSEC", ZONE_TYPE_NSEC, ZONE_CLASS_ANY, FIELDS(nsec_rdata_fields),
               check_nsec_rr, parse_nsec_rdata),
  TYPE("DNSKEY", ZONE_TYPE_DNSKEY, ZONE_CLASS_ANY, FIELDS(dnskey_rdata_fields),
                 check_dnskey_rr, parse_dnskey_rdata),
  TYPE("DHCID", ZONE_TYPE_DHCID, ZONE_CLASS_IN, FIELDS(dhcid_rdata_fields),
                check_dhcid_rr, parse_dhcid_rdata),
  TYPE("NSEC3", ZONE_TYPE_NSEC3, ZONE_CLASS_ANY, FIELDS(nsec3_rdata_fields),
                check_nsec3_rr, parse_nsec3_rdata),
  TYPE("NSEC3PARAM", ZONE_TYPE_NSEC3PARAM, ZONE_CLASS_ANY, FIELDS(nsec3param_rdata_fields),
                     check_nsec3param_rr, parse_nsec3param_rdata),
  TYPE("TLSA", ZONE_TYPE_TLSA, ZONE_CLASS_ANY, FIELDS(tlsa_rdata_fields),
               check_tlsa_rr, parse_tlsa_rdata),
  TYPE("SMIMEA", ZONE_TYPE_SMIMEA, ZONE_CLASS_ANY, FIELDS(smimea_rdata_fields),
                 check_tlsa_rr, parse_tlsa_rdata),

  UNKNOWN_TYPE(54),

  TYPE("HIP", ZONE_TYPE_HIP, ZONE_CLASS_ANY, FIELDS(hip_rdata_fields),
              check_hip_rr, parse_hip_rdata),
  TYPE("NINFO", ZONE_TYPE_NINFO, ZONE_CLASS_ANY, FIELDS(ninfo_rdata_fields),
              check_txt_rr, parse_txt_rdata),
  TYPE("RKEY", ZONE_TYPE_RKEY, ZONE_CLASS_ANY, FIELDS(rkey_rdata_fields),
               check_dnskey_rr, parse_dnskey_rdata),
  TYPE("TALINK", ZONE_TYPE_TALINK, ZONE_CLASS_ANY, FIELDS(talink_rdata_fields),
                 check_minfo_rr, parse_minfo_rdata),
  TYPE("CDS", ZONE_TYPE_CDS, ZONE_CLASS_ANY, FIELDS(cds_rdata_fields),
              check_ds_rr, parse_ds_rdata),
  TYPE("CDNSKEY", ZONE_TYPE_CDNSKEY, ZONE_CLASS_ANY, FIELDS(cdnskey_rdata_fields),
                  check_dnskey_rr, parse_dnskey_rdata),
  TYPE("OPENPGPKEY", ZONE_TYPE_OPENPGPKEY, ZONE_CLASS_ANY, FIELDS(openpgpkey_rdata_fields),
                     check_openpgpkey_rr, parse_openpgpkey_rdata),
  TYPE("CSYNC", ZONE_TYPE_CSYNC, ZONE_CLASS_ANY, FIELDS(csync_rdata_fields),
                check_csync_rr, parse_csync_rdata),
  TYPE("ZONEMD", ZONE_TYPE_ZONEMD, ZONE_CLASS_ANY, FIELDS(zonemd_rdata_fields),
                 check_zonemd_rr, parse_zonemd_rdata),
  TYPE("SVCB", ZONE_TYPE_SVCB, ZONE_CLASS_IN, FIELDS(svcb_rdata_fields),
               check_svcb_rr, parse_svcb_rdata),
  TYPE("HTTPS", ZONE_TYPE_HTTPS, ZONE_CLASS_IN, FIELDS(https_rdata_fields),
                check_https_rr, parse_https_rdata),
  TYPE("DSYNC", ZONE_TYPE_DSYNC, ZONE_CLASS_ANY, FIELDS(dsync_rdata_fields),
                check_dsync_rr, parse_dsync_rdata),
  UNKNOWN_TYPE(67),
  UNKNOWN_TYPE(68),
  UNKNOWN_TYPE(69),
  UNKNOWN_TYPE(70),
  UNKNOWN_TYPE(71),
  UNKNOWN_TYPE(72),
  UNKNOWN_TYPE(73),
  UNKNOWN_TYPE(74),
  UNKNOWN_TYPE(75),
  UNKNOWN_TYPE(76),
  UNKNOWN_TYPE(77),
  UNKNOWN_TYPE(78),
  UNKNOWN_TYPE(79),
  UNKNOWN_TYPE(80),
  UNKNOWN_TYPE(81),
  UNKNOWN_TYPE(82),
  UNKNOWN_TYPE(83),
  UNKNOWN_TYPE(84),
  UNKNOWN_TYPE(85),
  UNKNOWN_TYPE(86),
  UNKNOWN_TYPE(87),
  UNKNOWN_TYPE(88),
  UNKNOWN_TYPE(89),
  UNKNOWN_TYPE(90),
  UNKNOWN_TYPE(91),
  UNKNOWN_TYPE(92),
  UNKNOWN_TYPE(93),
  UNKNOWN_TYPE(94),
  UNKNOWN_TYPE(95),
  UNKNOWN_TYPE(96),
  UNKNOWN_TYPE(97),
  UNKNOWN_TYPE(98),

  TYPE("SPF", ZONE_TYPE_SPF, ZONE_CLASS_ANY, FIELDS(spf_rdata_fields), // obsolete
              check_txt_rr, parse_txt_rdata),

  UNKNOWN_TYPE(100),
  UNKNOWN_TYPE(101),
  UNKNOWN_TYPE(102),
  UNKNOWN_TYPE(103),

  TYPE("NID", ZONE_TYPE_NID, ZONE_CLASS_ANY, FIELDS(nid_rdata_fields),
              check_nid_rr, parse_nid_rdata),
  TYPE("L32", ZONE_TYPE_L32, ZONE_CLASS_ANY, FIELDS(l32_rdata_fields),
              check_l32_rr, parse_l32_rdata),
  TYPE("L64", ZONE_TYPE_L64, ZONE_CLASS_ANY, FIELDS(l64_rdata_fields),
              check_l64_rr, parse_l64_rdata),
  TYPE("LP", ZONE_TYPE_LP, ZONE_CLASS_ANY, FIELDS(lp_rdata_fields),
             check_mx_rr, parse_mx_rdata),
  TYPE("EUI48", ZONE_TYPE_EUI48, ZONE_CLASS_ANY, FIELDS(eui48_rdata_fields),
                check_eui48_rr, parse_eui48_rdata),
  TYPE("EUI64", ZONE_TYPE_EUI64, ZONE_CLASS_ANY, FIELDS(eui64_rdata_fields),
                check_eui64_rr, parse_eui64_rdata),

  UNKNOWN_TYPE(110),
  UNKNOWN_TYPE(111),
  UNKNOWN_TYPE(112),
  UNKNOWN_TYPE(113),
  UNKNOWN_TYPE(114),
  UNKNOWN_TYPE(115),
  UNKNOWN_TYPE(116),
  UNKNOWN_TYPE(117),
  UNKNOWN_TYPE(118),
  UNKNOWN_TYPE(119),
  UNKNOWN_TYPE(120),
  UNKNOWN_TYPE(121),
  UNKNOWN_TYPE(122),
  UNKNOWN_TYPE(123),
  UNKNOWN_TYPE(124),
  UNKNOWN_TYPE(125),
  UNKNOWN_TYPE(126),
  UNKNOWN_TYPE(127),
  UNKNOWN_TYPE(128),
  UNKNOWN_TYPE(129),
  UNKNOWN_TYPE(130),
  UNKNOWN_TYPE(131),
  UNKNOWN_TYPE(132),
  UNKNOWN_TYPE(133),
  UNKNOWN_TYPE(134),
  UNKNOWN_TYPE(135),
  UNKNOWN_TYPE(136),
  UNKNOWN_TYPE(137),
  UNKNOWN_TYPE(138),
  UNKNOWN_TYPE(139),
  UNKNOWN_TYPE(140),
  UNKNOWN_TYPE(141),
  UNKNOWN_TYPE(142),
  UNKNOWN_TYPE(143),
  UNKNOWN_TYPE(144),
  UNKNOWN_TYPE(145),
  UNKNOWN_TYPE(146),
  UNKNOWN_TYPE(147),
  UNKNOWN_TYPE(148),
  UNKNOWN_TYPE(149),
  UNKNOWN_TYPE(150),
  UNKNOWN_TYPE(151),
  UNKNOWN_TYPE(152),
  UNKNOWN_TYPE(153),
  UNKNOWN_TYPE(154),
  UNKNOWN_TYPE(155),
  UNKNOWN_TYPE(156),
  UNKNOWN_TYPE(157),
  UNKNOWN_TYPE(158),
  UNKNOWN_TYPE(159),
  UNKNOWN_TYPE(160),
  UNKNOWN_TYPE(161),
  UNKNOWN_TYPE(162),
  UNKNOWN_TYPE(163),
  UNKNOWN_TYPE(164),
  UNKNOWN_TYPE(165),
  UNKNOWN_TYPE(166),
  UNKNOWN_TYPE(167),
  UNKNOWN_TYPE(168),
  UNKNOWN_TYPE(169),
  UNKNOWN_TYPE(170),
  UNKNOWN_TYPE(171),
  UNKNOWN_TYPE(172),
  UNKNOWN_TYPE(173),
  UNKNOWN_TYPE(174),
  UNKNOWN_TYPE(175),
  UNKNOWN_TYPE(176),
  UNKNOWN_TYPE(177),
  UNKNOWN_TYPE(178),
  UNKNOWN_TYPE(179),
  UNKNOWN_TYPE(180),
  UNKNOWN_TYPE(181),
  UNKNOWN_TYPE(182),
  UNKNOWN_TYPE(183),
  UNKNOWN_TYPE(184),
  UNKNOWN_TYPE(185),
  UNKNOWN_TYPE(186),
  UNKNOWN_TYPE(187),
  UNKNOWN_TYPE(188),
  UNKNOWN_TYPE(189),
  UNKNOWN_TYPE(190),
  UNKNOWN_TYPE(191),
  UNKNOWN_TYPE(192),
  UNKNOWN_TYPE(193),
  UNKNOWN_TYPE(194),
  UNKNOWN_TYPE(195),
  UNKNOWN_TYPE(196),
  UNKNOWN_TYPE(197),
  UNKNOWN_TYPE(198),
  UNKNOWN_TYPE(199),
  UNKNOWN_TYPE(200),
  UNKNOWN_TYPE(201),
  UNKNOWN_TYPE(202),
  UNKNOWN_TYPE(203),
  UNKNOWN_TYPE(204),
  UNKNOWN_TYPE(205),
  UNKNOWN_TYPE(206),
  UNKNOWN_TYPE(207),
  UNKNOWN_TYPE(208),
  UNKNOWN_TYPE(209),
  UNKNOWN_TYPE(210),
  UNKNOWN_TYPE(211),
  UNKNOWN_TYPE(212),
  UNKNOWN_TYPE(213),
  UNKNOWN_TYPE(214),
  UNKNOWN_TYPE(215),
  UNKNOWN_TYPE(216),
  UNKNOWN_TYPE(217),
  UNKNOWN_TYPE(218),
  UNKNOWN_TYPE(219),
  UNKNOWN_TYPE(220),
  UNKNOWN_TYPE(221),
  UNKNOWN_TYPE(222),
  UNKNOWN_TYPE(223),
  UNKNOWN_TYPE(224),
  UNKNOWN_TYPE(225),
  UNKNOWN_TYPE(226),
  UNKNOWN_TYPE(227),
  UNKNOWN_TYPE(228),
  UNKNOWN_TYPE(229),
  UNKNOWN_TYPE(230),
  UNKNOWN_TYPE(231),
  UNKNOWN_TYPE(232),
  UNKNOWN_TYPE(233),
  UNKNOWN_TYPE(234),
  UNKNOWN_TYPE(235),
  UNKNOWN_TYPE(236),
  UNKNOWN_TYPE(237),
  UNKNOWN_TYPE(238),
  UNKNOWN_TYPE(239),
  UNKNOWN_TYPE(240),
  UNKNOWN_TYPE(241),
  UNKNOWN_TYPE(242),
  UNKNOWN_TYPE(243),
  UNKNOWN_TYPE(244),
  UNKNOWN_TYPE(245),
  UNKNOWN_TYPE(246),
  UNKNOWN_TYPE(247),
  UNKNOWN_TYPE(248),
  UNKNOWN_TYPE(249),
  UNKNOWN_TYPE(250),
  UNKNOWN_TYPE(251),
  UNKNOWN_TYPE(252),
  UNKNOWN_TYPE(253),
  UNKNOWN_TYPE(254),
  UNKNOWN_TYPE(255),

  TYPE("URI", ZONE_TYPE_URI, ZONE_CLASS_ANY, FIELDS(uri_rdata_fields),
              check_uri_rr, parse_uri_rdata),
  TYPE("CAA", ZONE_TYPE_CAA, ZONE_CLASS_ANY, FIELDS(caa_rdata_fields),
              check_caa_rr, parse_caa_rdata),
  TYPE("AVC", ZONE_TYPE_AVC, ZONE_CLASS_ANY, FIELDS(avc_rdata_fields),
              check_txt_rr, parse_txt_rdata),
  TYPE("DOA", ZONE_TYPE_DOA, ZONE_CLASS_ANY, FIELDS(doa_rdata_fields),
              check_doa_rr, parse_doa_rdata),
  TYPE("AMTRELAY", ZONE_TYPE_AMTRELAY, ZONE_CLASS_ANY, FIELDS(amtrelay_rdata_fields),
              check_amtrelay_rr, parse_amtrelay_rdata),
  TYPE("RESINFO", ZONE_TYPE_RESINFO, ZONE_CLASS_ANY, FIELDS(resinfo_rdata_fields),
              check_txt_rr, parse_txt_rdata),
  TYPE("WALLET", ZONE_TYPE_WALLET, ZONE_CLASS_ANY, FIELDS(wallet_rdata_fields),
              check_txt_rr, parse_txt_rdata),
  TYPE("CLA", ZONE_TYPE_CLA, ZONE_CLASS_ANY, FIELDS(cla_rdata_fields),
              check_txt_rr, parse_txt_rdata),
  TYPE("IPN", ZONE_TYPE_IPN, ZONE_CLASS_ANY, FIELDS(ipn_rdata_fields),
              check_ipn_rr, parse_ipn_rdata),

  UNKNOWN_TYPE(265),
  UNKNOWN_TYPE(266),
  UNKNOWN_TYPE(267),
  UNKNOWN_TYPE(268),
  UNKNOWN_TYPE(269),

  /* Map 32768 in hash.c to 270 */
  TYPE("TA", ZONE_TYPE_TA, ZONE_CLASS_ANY, FIELDS(ta_rdata_fields), // obsolete
              check_ds_rr, parse_ds_rdata),
  /* Map 32769 in hash.c to 271 */
  TYPE("DLV", ZONE_TYPE_DLV, ZONE_CLASS_ANY, FIELDS(dlv_rdata_fields), // obsolete
              check_ds_rr, parse_ds_rdata)
};

#undef UNKNOWN_CLASS
#undef CLASS
#undef UNKNOWN_TYPE
#undef TYPE

diagnostic_pop()

#endif // TYPES_H
