/*
 * svcb.h -- svcb (RFC9460) parser
 *
 * Copyright (c) 2022-2023, NLnet Labs. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */
#ifndef SVCB_H
#define SVCB_H

#include <inttypes.h>

// RFC9460 section 7.1:
//   The "alpn" and "no-default-alpn" SvcParamKeys together indicate the set
//   of Application-Layer Protocol Negotiation (ALPN) protocol identifiers
//   [ALPN] and associated transport protocols supported by this service
//   endpoint (the "SVCB ALPN set").
//
// RFC9460 section 7.1.1:
//   ALPNs are identified by their registered "Identification Sequence"
//   (alpn-id), which is a sequence of 1-255 octets. For "alpn", the
//   presentation value SHALL be a comma-separated list (Appendix A.1) of
//   one or more alpn-ids. Zone-file implementations MAY disallow the ","
//   and "\" characters in ALPN IDs instead of implementing the value-list
//   escaping procedure, relying on the opaque key format (e.g., key=\002h2)
//   in the event that these characters are needed.
//
// Application-Layer Protocol Negotiation (ALPN) protocol identifiers are
// maintained by IANA:
//
// https://www.iana.org/assignments/tls-extensiontype-values#alpn-protocol-ids
//
// RFC9460 section 7.1.1:
//   For "alpn", the presentation value SHALL be a comma-separated list
//   (Appendix A.1) of one or more alpn-ids. Zone-file implementations MAY
//   disallow the "," and "\" characters in ALPN IDs instead of implementing
//   the value-list escaping procedure, relying on the opaque key format
//   (e.g., key1=\002h2) in the event that these characters are needed.
//
// RFC9460 appendix A.1:
//   A value-list parser that splits on "," and prohibits items containing
//   "\\" is sufficient to comply with all requirements in this document.
//
// RFC9460 appendix A.1:
//   Decoding of value-lists happens after character-string decoding.
//
//
//
// RFC9460 (somewhat incorrectly) states that an SvcParamValue is a
// character-string. An SvcParamValue is just that, an SvcParamValue. The
// presentation format is not a context-free format like JSON. Tokens can be
// identified, not classified, by syntax.
//
// Context-free languages (e.g. C, JSON) classify a token as a string if it is
// quoted, an identifier or keyword if it is a contiguous set of characters,
// etc. Unescaping is done by the scanner because the token is classified
// during that stage. The presentation format defines basic syntax to identify
// tokens, but as the format is NOT context-free and intentionally extensible,
// the token is classified by the parser. Conversion of domain names from text
// format to wire format is a prime example.
//
// Example:
// "foo. NS bar\." defines "bar\." as a relative domain. The "\" (backslash)
// is important because it signals that the trailing dot does not serve as a
// label separator.
//
// Note that RFC9460 is contradicts itself by stating that the value-list
// escaping procedure may rely on the opaque key format (e.g., key1=\002h2)
// in the event that these characters are needed. Escaping using \DDD, if
// SvcParamValue is indeed to be interpreted as a string, would produce
// 0x03 0x02 0x68 0x32 in wire format.
//
// IETF mailing list discussion on this topic:
// https://mailarchive.ietf.org/arch/msg/dnsop/SXnlsE1B8gmlDjn4HtOo1lwtqAI/
//
//
// BIND disallows any escape sequences in port, ipv4hint, etc. Regular
// (single stage) escaping rules are applied to dohpath. Special (two stage)
// escaping rules apply for alpn.
//
// Knot disallows escape sequences in port, ipv4hint, etc. kzonecheck 3.3.4
// does not to accept dohpath. Special (two stage) escaping rules apply for
// alpn.
nonnull_all
static int32_t parse_alpn(
  parser_t *parser,
  const type_info_t *type,
  const rdata_info_t *field,
  uint16_t key,
  const svc_param_info_t *param,
  rdata_t *rdata,
  const token_t *token)
{
  assert(rdata->octets < rdata->limit);

  (void)field;
  (void)key;
  (void)param;

  uint8_t *comma = rdata->octets++;
  const char *data = token->data, *limit = token->data + token->length;

  while (data < limit && rdata->octets < rdata->limit) {
    *rdata->octets = (uint8_t)*data;
    if (unlikely(*rdata->octets == '\\')) {
      uint32_t length;
      if (!(length = unescape(data, rdata->octets)))
        SYNTAX_ERROR(parser, "Invalid alpn in %s", NAME(type));
      data += length;
      // second level escape processing
      if (*rdata->octets == '\\') {
        assert(length);
        if (*data == '\\') {
          if (!(length = unescape(data, rdata->octets)))
            SYNTAX_ERROR(parser, "Invalid alpn in %s", NAME(type));
          data += length;
        } else {
          *rdata->octets = (uint8_t)*data;
          data++;
        }
        rdata->octets++;
        continue;
      }
    } else {
      data++;
    }

    if (*rdata->octets == ',') {
      assert(comma < rdata->octets);
      const size_t length = ((uintptr_t)rdata->octets - (uintptr_t)comma) - 1;
      if (!length || length > 255)
        SYNTAX_ERROR(parser, "Invalid alpn in %s", NAME(type));
      *comma = (uint8_t)length;
      comma = rdata->octets;
    }

    rdata->octets++;
  }

  if (data != limit || rdata->octets > rdata->limit)
    SYNTAX_ERROR(parser, "Invalid alpn in %s", NAME(type));
  const size_t length = ((uintptr_t)rdata->octets - (uintptr_t)comma) - 1;
  if (!length || length > 255)
    SYNTAX_ERROR(parser, "Invalid alpn in %s", NAME(type));
  *comma = (uint8_t)length;
  return 0;
}

nonnull_all
static int32_t parse_port(
  parser_t *parser,
  const type_info_t *type,
  const rdata_info_t *field,
  uint16_t key,
  const svc_param_info_t *param,
  rdata_t *rdata,
  const token_t *token)
{
  const char *data = token->data;

  (void)field;
  (void)key;
  (void)param;

  if (!token->length || token->length > 5)
    SYNTAX_ERROR(parser, "Invalid port in %s", NAME(type));

  uint64_t number = 0;
  for (;; data++) {
    const uint64_t digit = (uint8_t)*data - '0';
    if (digit > 9)
      break;
    number = number * 10 + digit;
  }

  uint16_t port = (uint16_t)number;
  port = htobe16(port);
  memcpy(rdata->octets, &port, 2);
  rdata->octets += 2;

  if (rdata->octets > rdata->limit)
    SYNTAX_ERROR(parser, "Invalid %s", NAME(type));
  if (data != token->data + token->length || number > 65535)
    SYNTAX_ERROR(parser, "Invalid port in %s", NAME(type));
  return 0;
}

nonnull_all
static int32_t parse_ipv4hint(
  parser_t *parser,
  const type_info_t *type,
  const rdata_info_t *field,
  uint16_t key,
  const svc_param_info_t *param,
  rdata_t *rdata,
  const token_t *token)
{
  const char *t = token->data, *te = token->data + token->length;
  size_t n = 0;

  (void)field;
  (void)key;
  (void)param;

  if ((n = (size_t)scan_ip4(t, rdata->octets)) == 0)
    SYNTAX_ERROR(parser, "Invalid ipv4hint in %s", NAME(type));
  rdata->octets += 4;
  t += n;

  while (*t == ',') {
    if (rdata->octets > rdata->limit)
      SYNTAX_ERROR(parser, "Invalid ipv4hint in %s", NAME(type));
    if ((n = (size_t)scan_ip4(t + 1, rdata->octets)) == 0)
      SYNTAX_ERROR(parser, "Invalid ipv4hint in %s", NAME(type));
    rdata->octets += 4;
    t += n + 1;
  }

  if (t != te || rdata->octets > rdata->limit)
    SYNTAX_ERROR(parser, "Invalid ipv4hint in %s", NAME(type));
  return 0;
}

nonnull_all
static int32_t parse_ech(
  parser_t *parser,
  const type_info_t *type,
  const rdata_info_t *field,
  uint16_t key,
  const svc_param_info_t *param,
  rdata_t *rdata,
  const token_t *token)
{
  size_t size = (uintptr_t)rdata->limit - (uintptr_t)rdata->octets;
  size_t length;

  (void)field;
  (void)key;
  (void)param;

  if (token->length / 4 > size / 3)
    SYNTAX_ERROR(parser, "maximum size exceeded");

  struct base64_state state = { .eof = 0, .bytes = 0, .carry = 0 };
  if (!base64_stream_decode(
    &state, token->data, token->length, rdata->octets, &length))
    SYNTAX_ERROR(parser, "Invalid ech in %s", NAME(type));

  rdata->octets += length;
  if (state.bytes)
    SYNTAX_ERROR(parser, "Invalid ech in %s", NAME(type));

  return 0;
}

nonnull_all
static int32_t parse_ipv6hint(
  parser_t *parser,
  const type_info_t *type,
  const rdata_info_t *field,
  uint16_t key,
  const svc_param_info_t *param,
  rdata_t *rdata,
  const token_t *token)
{
  const char *t = token->data, *te = token->data + token->length;
  size_t n = 0;

  (void)field;
  (void)key;
  (void)param;

  if ((n = (size_t)scan_ip6(t, rdata->octets)) == 0)
    SYNTAX_ERROR(parser, "Invalid ipv6hint in %s", NAME(type));
  rdata->octets += 16;
  t += n;

  while (*t == ',') {
    if (rdata->octets >= rdata->limit)
      SYNTAX_ERROR(parser, "Invalid ipv6hint in %s", NAME(type));
    if ((n = (size_t)scan_ip6(t + 1, rdata->octets)) == 0)
      SYNTAX_ERROR(parser, "Invalid ipv6hint in %s", NAME(type));
    rdata->octets += 16;
    t += n + 1;
  }

  if (t != te || rdata->octets > rdata->limit)
    SYNTAX_ERROR(parser, "Invalid ipv6hint in %s", NAME(type));
  return 0;
}

// RFC9461 section 5:
//   "dohpath" is a single-valued SvcParamKey whose value (in both
//   presentation format and wire format) MUST be a URI Template in
//   relative form ([RFC6570], Section 1.1) encoded in UTF-8 [RFC3629].
nonnull_all
static int32_t parse_dohpath(
  parser_t *parser,
  const type_info_t *type,
  const rdata_info_t *field,
  uint16_t key,
  const svc_param_info_t *param,
  rdata_t *rdata,
  const token_t *token)
{
  const char *t = token->data, *te = t + token->length;

  (void)field;
  (void)key;
  (void)param;

  // FIXME: easily optimized using SIMD (and possibly SWAR)
  while ((t < te) & (rdata->octets < rdata->limit)) {
    *rdata->octets = (uint8_t)*t;
    if (*t == '\\') {
      uint32_t o;
      if (!(o = unescape(t, rdata->octets)))
        SYNTAX_ERROR(parser, "Invalid dohpath in %s", NAME(type));
      rdata->octets += 1; t += o;
    } else {
      rdata->octets += 1; t += 1;
    }
  }

  // RFC9461 section 5:
  //   The URI Template MUST contain a "dns" variable, and MUST be chosen such
  //   that the result after DoH URI Template expansion (RFC8484 section 6)
  //   is always a valid and function ":path" value (RFC9113 section 8.3.1)
  // FIXME: implement

  if (t != te || rdata->octets >= rdata->limit)
    SYNTAX_ERROR(parser, "Invalid dohpath in %s", NAME(type));
  return 0;
}

nonnull_all
static int32_t parse_unknown(
  parser_t *parser,
  const type_info_t *type,
  const rdata_info_t *field,
  uint16_t key,
  const svc_param_info_t *param,
  rdata_t *rdata,
  const token_t *token)
{
  const char *t = token->data, *te = t + token->length;

  (void)key;
  (void)param;

  while ((t < te) & (rdata->octets < rdata->limit)) {
    *rdata->octets = (uint8_t)*t;
    if (*t == '\\') {
      uint32_t o;
      if (!(o = unescape(t, rdata->octets)))
        SYNTAX_ERROR(parser, "Invalid %s in %s", NAME(field), NAME(type));
      rdata->octets += 1; t += o;
    } else {
      rdata->octets += 1; t += 1;
    }
  }

  if (t != te || rdata->octets >= rdata->limit)
    SYNTAX_ERROR(parser, "Invalid %s in %s", NAME(field), NAME(type));
  return 0;
}

nonnull_all
static int32_t parse_tls_supported_groups(
  parser_t *parser,
  const type_info_t *type,
  const rdata_info_t *field,
  uint16_t key,
  const svc_param_info_t *param,
  rdata_t *rdata,
  const token_t *token)
{
  const char *t = token->data, *te = token->data + token->length;
  const uint8_t *rdata_start = rdata->octets;

  (void)field;
  (void)key;
  (void)param;

  while (t < te && rdata->octets+2 <= rdata->limit) {
    uint64_t number = 0;
    for (;; t++) {
      const uint64_t digit = (uint8_t)*t - '0';
      if (digit > 9)
        break;
      number = number * 10 + digit;
    }

    uint16_t group = (uint16_t)number;
    group = htobe16(group);
    memcpy(rdata->octets, &group, 2);
    rdata->octets += 2;
    if (number > 65535)
      SYNTAX_ERROR(parser, "Invalid tls-supported-group in %s", NAME(type));

    const uint8_t *g;
    for (g = rdata_start; g < rdata->octets - 2; g += 2) {
      if (memcmp(g, &group, 2) == 0)
        SEMANTIC_ERROR(parser, "Duplicate group in tls-supported-groups in %s", NAME(type));
    }
    if (*t != ',')
      break;
    else
      t++;
  }

  if (t != te || rdata->octets > rdata->limit)
    SYNTAX_ERROR(parser, "Invalid tls-supported-groups in %s", NAME(type));
  return 0;
}

nonnull_all
static int32_t parse_mandatory_lax(
  parser_t *parser,
  const type_info_t *type,
  const rdata_info_t *field,
  uint16_t key,
  const svc_param_info_t *svc_param,
  rdata_t *rdata,
  const token_t *token);

nonnull_all
static int32_t parse_mandatory(
  parser_t *parser,
  const type_info_t *type,
  const rdata_info_t *field,
  uint16_t key,
  const svc_param_info_t *svc_param,
  rdata_t *rdata,
  const token_t *token);

#define SVC_PARAM(name, key, value, parse, parse_lax) \
  { { { name, sizeof(name) - 1 }, key }, value, parse, parse_lax }

#define NO_VALUE (0u)
#define OPTIONAL_VALUE (1u << 1)
#define MANDATORY_VALUE (1u << 2)

static const svc_param_info_t svc_params[] = {
  // RFC9460 section 8:
  //   The presentation value SHALL be a comma-separated list (Appendix A.1)
  //   of one or more valid SvcParamKeys ...
  SVC_PARAM("mandatory", 0u, MANDATORY_VALUE,
            parse_mandatory, parse_mandatory_lax),
  SVC_PARAM("alpn", 1u, MANDATORY_VALUE, parse_alpn, parse_alpn),
  // RFC9460 section 7.1.1:
  //   For "no-default-alpn", the presentation and wire format values MUST be
  //   empty. When "no-default-alpn" is specified in an RR, "alpn" must also be
  //   specified in order for the RR to be "self-consistent" (Section 2.4.3).
  SVC_PARAM("no-default-alpn", 2u, NO_VALUE, parse_unknown, parse_unknown),
  // RFC9460 section 7.2:
  //   The presentation value of the SvcParamValue is a single decimal integer
  //   between 0 and 65535 in ASCII. ...
  SVC_PARAM("port", 3u, MANDATORY_VALUE, parse_port, parse_port),
  // RFC9460 section 7.3:
  //   The presentation value SHALL be a comma-separated list (Appendix A.1)
  //   of one or more IP addresses ...
  SVC_PARAM("ipv4hint", 4u, MANDATORY_VALUE, parse_ipv4hint, parse_ipv4hint),
  SVC_PARAM("ech", 5u, OPTIONAL_VALUE, parse_ech, parse_ech),
  // RFC9460 section 7.3:
  //   See "ipv4hint".
  SVC_PARAM("ipv6hint", 6u, MANDATORY_VALUE, parse_ipv6hint, parse_ipv6hint),
  // RFC9461 section 5:
  //   If the "alpn" SvcParam indicates support for HTTP, "dohpath" MUST be
  //   present.
  SVC_PARAM("dohpath", 7u, MANDATORY_VALUE, parse_dohpath, parse_dohpath),
  // RFC9540 section 4.
  //   Both the presentation and wire-format values for the "ohttp" parameter
  //   MUST be empty.
  SVC_PARAM("ohttp", 8u, NO_VALUE, parse_unknown, parse_unknown),
  // draft-ietf-tls-key-share-prediction-01 section 3.1
  SVC_PARAM("tls-supported-groups", 9u, MANDATORY_VALUE,
            parse_tls_supported_groups, parse_tls_supported_groups),
};

static const svc_param_info_t unknown_svc_param =
  SVC_PARAM("unknown", 0u, OPTIONAL_VALUE, parse_unknown, parse_unknown);

#undef SVC_PARAM

nonnull_all
static really_inline size_t scan_unknown_svc_param_key(
  const char *data, uint16_t *key, const svc_param_info_t **param)
{
  size_t length = 4;
  uint32_t number = (uint8_t)data[3] - '0';

  if (number > 9)
    return 0;

  uint32_t leading_zero = number == 0;

  for (;; length++) {
    const uint32_t digit = (uint8_t)data[length] - '0';
    if (digit > 9)
      break;
    number = number * 10 + digit;
  }

  leading_zero &= length > 4;
  if (leading_zero || length > 3 + 5)
    return 0;
  if (number < (sizeof(svc_params) / sizeof(svc_params[0])))
    return (void)(*param = &svc_params[(*key = (uint16_t)number)]), length;
  if (number < 65535)
    return (void)(*key = (uint16_t)number), (void)(*param = &unknown_svc_param), length;
  return 0;
}

nonnull_all
static really_inline size_t scan_svc_param(
  const char *data, uint16_t *key, const svc_param_info_t **param)
{
  // draft-ietf-dnsop-svcb-https-12 section 2.1:
  // alpha-lc    = %x61-7A   ;  a-z
  // SvcParamKey = 1*63(alpha-lc / DIGIT / "-")
  //
  // FIXME: naive implementation
  if (memcmp(data, "mandatory", 9) == 0)
    return (void)(*param = &svc_params[(*key = ZONE_SVC_PARAM_KEY_MANDATORY)]), 9;
  else if (memcmp(data, "alpn", 4) == 0)
    return (void)(*param = &svc_params[(*key = ZONE_SVC_PARAM_KEY_ALPN)]), 4;
  else if (memcmp(data, "no-default-alpn", 15) == 0)
    return (void)(*param = &svc_params[(*key = ZONE_SVC_PARAM_KEY_NO_DEFAULT_ALPN)]), 15;
  else if (memcmp(data, "port", 4) == 0)
    return (void)(*param = &svc_params[(*key = ZONE_SVC_PARAM_KEY_PORT)]), 4;
  else if (memcmp(data, "ipv4hint", 8) == 0)
    return (void)(*param = &svc_params[(*key = ZONE_SVC_PARAM_KEY_IPV4HINT)]), 8;
  else if (memcmp(data, "ech", 3) == 0)
    return (void)(*param = &svc_params[(*key = ZONE_SVC_PARAM_KEY_ECH)]), 3;
  else if (memcmp(data, "ipv6hint", 8) == 0)
    return (void)(*param = &svc_params[(*key = ZONE_SVC_PARAM_KEY_IPV6HINT)]), 8;
  else if (memcmp(data, "dohpath", 7) == 0)
    return (void)(*param = &svc_params[(*key = ZONE_SVC_PARAM_KEY_DOHPATH)]), 7;
  else if (memcmp(data, "ohttp", 5) == 0)
    return (void)(*param = &svc_params[(*key = ZONE_SVC_PARAM_KEY_OHTTP)]), 5;
  else if (memcmp(data, "tls-supported-groups", 20) == 0)
    return (void)(*param = &svc_params[(*key = ZONE_SVC_PARAM_KEY_TLS_SUPPORTED_GROUPS)]), 20;
  else if (memcmp(data, "key", 3) == 0)
    return scan_unknown_svc_param_key(data, key, param);
  else
    return 0;
}

nonnull_all
static really_inline size_t scan_svc_param_key(
  const char *data, uint16_t *key)
{
  // FIXME: improve implementation
  const svc_param_info_t *param;
  return scan_svc_param(data, key, &param);
}

nonnull_all
static int32_t parse_mandatory(
  parser_t *parser,
  const type_info_t *type,
  const rdata_info_t *field,
  uint16_t key,
  const svc_param_info_t *param,
  rdata_t *rdata,
  const token_t *token)
{
  (void)field;
  (void)param;

  // RFC9460 section 8:
  //   This SvcParamKey is always automatically mandatory and MUST NOT appear
  //   in its own value-list. Other automatically mandatory keys SHOULD NOT
  //   appear in the list either.
  uint64_t mandatory = (1u << ZONE_SVC_PARAM_KEY_MANDATORY);
  uint64_t keys = 0u;
  int32_t highest_key = -1;
  const char *data = token->data;
  uint8_t *whence = rdata->octets;
  size_t skip;

  // RFC9460 section 9:
  //   The "automatically mandatory" keys (Section 8) are "port" and
  //   "no-default-alpn".
  if (type->name.value == ZONE_TYPE_HTTPS)
    mandatory = (1u << ZONE_SVC_PARAM_KEY_MANDATORY) |
                (1u << ZONE_SVC_PARAM_KEY_NO_DEFAULT_ALPN) |
                (1u << ZONE_SVC_PARAM_KEY_PORT);

  // RFC9460 section 8:
  //   The presentation value SHALL be a comma-seperatred list of one or more
  //   valid SvcParamKeys, ...
  if (!(skip = scan_svc_param_key(data, &key)))
    SYNTAX_ERROR(parser, "Invalid mandatory in %s", NAME(type));
  if (key < 64)
    keys = 1llu << key;

  highest_key = key;
  key = htobe16(key);
  memcpy(rdata->octets, &key, sizeof(key));
  rdata->octets += sizeof(key);
  data += skip;

  while (*data == ',' && rdata->octets < rdata->limit) {
    if (!(skip = scan_svc_param_key(data + 1, &key)))
      SYNTAX_ERROR(parser, "Invalid mandatory of %s", NAME(type));

    // check if key appears in automatically mandatory key list
    if (key < 64)
      keys |= 1llu << key;

    data += skip + 1;
    if (key > highest_key) {
      highest_key = key;
      key = htobe16(key);
      memcpy(rdata->octets, &key, 2);
      rdata->octets += 2;
    } else {
      // RFC9460 section 8:
      //   In wire format, the keys are represented by their numeric values in
      //   network byte order, concatenated in ascending order.
      uint8_t *octets = whence;
      uint16_t smaller_key = 0;
      while (octets < rdata->octets) {
        memcpy(&smaller_key, octets, sizeof(smaller_key));
        smaller_key = be16toh(smaller_key);
        if (key <= smaller_key)
          break;
        octets += 2;
      }
      assert(octets <= rdata->octets);
      // RFC9460 section 8:
      //   Keys MAY appear in any order, but MUST NOT appear more than once.
      if (key == smaller_key)
        SEMANTIC_ERROR(parser, "Duplicate key in mandatory of %s", NAME(type));
      assert(key < smaller_key);
      uint16_t length = (uint16_t)(rdata->octets - octets);
      memmove(octets + 2, octets, length);
      key = htobe16(key);
      memcpy(octets, &key, 2);
      rdata->octets += 2;
    }
  }

  if (keys & mandatory)
    SEMANTIC_ERROR(parser, "Automatically mandatory key(s) in mandatory of %s", NAME(type));
  if (rdata->octets >= rdata->limit)
    SYNTAX_ERROR(parser, "Invalid mandatory in %s", NAME(type));
  if (data != token->data + token->length)
    SYNTAX_ERROR(parser, "Invalid mandatory in %s", NAME(type));
  return 0;
}

nonnull_all
static int32_t parse_mandatory_lax(
  parser_t *parser,
  const type_info_t *type,
  const rdata_info_t *field,
  uint16_t key,
  const svc_param_info_t *param,
  rdata_t *rdata,
  const token_t *token)
{
  (void)field;

  // RFC9460 section 8:
  //   This SvcParamKey is always automatically mandatory and MUST NOT appear
  //   in its own value-list. Other automatically mandatory keys SHOULD NOT
  //   appear in the list either.
  uint64_t mandatory = (1u << ZONE_SVC_PARAM_KEY_MANDATORY);
  uint64_t keys = 0;
  // RFC9460 section 8:
  //   In wire format, the keys are represented by their numeric values in
  //   network byte order, concatenated in strictly increasing numeric order.
  //
  // cannot reorder in secondary mode, print an error
  bool out_of_order = false;
  int32_t highest_key = -1;
  const uint8_t *whence = rdata->octets;
  const char *data = token->data;
  size_t skip;

  // RFC9460 section 8:
  //   The presentation value SHALL be a comma-seperatred list of one or more
  //   valid SvcParamKeys, ...
  if (!(skip = scan_svc_param_key(data, &key)))
    SYNTAX_ERROR(parser, "Invalid key in %s of %s", NAME(param), NAME(type));
  if (key < 64)
    keys |= 1llu << key;

  // RFC9460 section 9:
  //   The "automatically mandatory" keys (Section 8) are "port" and
  //   "no-default-alpn".
  if (type->name.value == ZONE_TYPE_HTTPS)
    mandatory = (1u << ZONE_SVC_PARAM_KEY_MANDATORY) |
                (1u << ZONE_SVC_PARAM_KEY_NO_DEFAULT_ALPN) |
                (1u << ZONE_SVC_PARAM_KEY_PORT);

  key = htobe16(key);
  memcpy(rdata->octets, &key, 2);
  rdata->octets += 2;
  data += skip;

  while (*data == ',' && rdata->octets < rdata->limit) {
    if (!(skip = scan_svc_param_key(data + 1, &key)))
      SYNTAX_ERROR(parser, "Invalid key in %s of %s", NAME(param), NAME(type));

    // check if key appears in automatically mandatory key list
    if (key < 64)
      keys |= (1llu << key);

    if ((int32_t)key <= highest_key) {
      // RFC9460 section 8:
      //   In wire format, the keys are represented by their numeric values in
      //   network byte order, concatenated in ascending order.
      const uint8_t *octets = whence;
      uint16_t smaller_key = 0;
      while (octets < rdata->octets) {
        memcpy(&smaller_key, octets, sizeof(smaller_key));
        smaller_key = be16toh(smaller_key);
        if (key <= smaller_key)
          break;
        octets += 2;
      }
      assert(octets <= rdata->octets);
      // RFC9460 section 8:
      //   Keys MAY appear in any order, but MUST NOT appear more than once.
      if (key == smaller_key)
        SEMANTIC_ERROR(parser, "Duplicate key in mandatory of %s", NAME(type));
      assert(key < smaller_key);
      out_of_order = true;
    }

    data += skip + 1;
    key = htobe16(key);
    memcpy(rdata->octets, &key, 2);
    rdata->octets += 2;
  }

  if (keys & mandatory)
    SEMANTIC_ERROR(parser, "Automatically mandatory key(s) in mandatory of %s", NAME(type));
  if (out_of_order)
    SEMANTIC_ERROR(parser, "Out of order keys in mandatory of %s", NAME(type));
  if (rdata->octets >= rdata->limit - 2)
    SYNTAX_ERROR(parser, "Invalid %s", NAME(type));
  if (data != token->data + token->length)
    SYNTAX_ERROR(parser, "Invalid %s", NAME(type));
  return 0;
}

nonnull_all
static really_inline int32_t check_mandatory(
  zone_parser_t *parser,
  const type_info_t *type,
  const rdata_info_t *field,
  const rdata_t *rdata,
  const uint8_t *parameters)
{
  // parameters are guaranteed to be in order (automatic in strict mode)
  if (parameters[0] || parameters[1])
    return 0; // no mandatory parameter

  uint16_t length;
  memcpy(&length, parameters + 2, sizeof(length));
  length = be16toh(length);
  assert(rdata->octets - parameters >= 4 + length);

  bool missing_keys = false;
  const uint8_t *limit = parameters + 4 + length;
  const uint8_t *keys = parameters + 4;
  parameters += 4 + length;

  assert(parameters <= rdata->octets);

  for (; keys < limit; keys += 2) {
    uint16_t key;
    memcpy(&key, keys, sizeof(key));
    // no byteswap, compare big endian

    // mandatory is guaranteed to exist
    if (key == 0)
      continue;
    if ((missing_keys = (parameters == rdata->octets)))
      break;
    assert(rdata->octets - parameters >= 4);
    memcpy(&length, parameters + 2, 2);
    length = be16toh(length);
    assert(rdata->octets - parameters >= 4 + length);
    // parameters are guaranteed to be sorted
    if (memcmp(parameters, &key, 2) == 0) {
      parameters += 4 + length;
    } else {
      const uint8_t *parameter = parameters + 4 + length;
      assert(rdata->octets - parameters >= 4);
      while (parameter < rdata->octets) {
        if (memcmp(parameter, &key, 2) == 0)
          break;
        memcpy(&length, parameter + 2, 2);
        length = be16toh(length);
        assert(rdata->octets - parameters >= 4 + length);
        parameter += 4 + length;
      }

      if ((missing_keys = (parameter == rdata->octets)))
        break;
    }
  }

  if (missing_keys)
    SEMANTIC_ERROR(parser, "Mandatory %s missing in %s", NAME(field), NAME(type));
  return 0;
}

nonnull((1, 2, 3, 5, 7))
static really_inline int32_t parse_svc_param(
  parser_t *parser,
  const type_info_t *type,
  const rdata_info_t *field,
  uint16_t key,
  const svc_param_info_t *param,
  const parse_svc_param_t parse,
  rdata_t *rdata,
  const token_t *token)
{
  switch ((token != NULL) | param->has_value) {
    case 0: // void parameter without value
      return 0;
    case 1: // void parameter with value
      assert(token);
      SEMANTIC_ERROR(parser, "%s with value in %s", NAME(field), NAME(type));
      if (unlikely(!token->length))
        return 0;
      break;
    case 2: // parameter without optional value
      return 0;
    case 3: // parameter with optional value
      assert(token);
      if (unlikely(!token->length))
        return 0;
      break;
    case 4: // parameter without value
      SEMANTIC_ERROR(parser, "%s without value in %s", NAME(field), NAME(type));
      return 0;
    case 5: // parameter with value
      assert(token);
      if (unlikely(!token->length))
        SEMANTIC_ERROR(parser, "%s without value in %s", NAME(field), NAME(type));
      break;
  }

  return parse(parser, type, field, key, param, rdata, token);
}

nonnull_all
static int32_t parse_svc_params_lax(
  parser_t *parser,
  const type_info_t *type,
  const rdata_info_t *field,
  rdata_t *rdata,
  token_t *token)
{
  bool out_of_order = false;
  int32_t code, highest_key = -1;
  uint32_t errors = 0;
  const uint8_t *whence = rdata->octets;
  uint64_t keys = 0;

  // FIXME: check if parameter even fits
  while (is_contiguous(token)) {
    size_t count;
    uint16_t key;
    const svc_param_info_t *param;
    const token_t *value = token;

    if (!(count = scan_svc_param(token->data, &key, &param)))
      SYNTAX_ERROR(parser, "Invalid %s in %s", NAME(field), NAME(type));
    assert(param);

    if (likely(key > highest_key))
      highest_key = key;
    else
      out_of_order = true;

    if (key < 64)
      keys |= (1llu << key);

    if (token->data[count] != '=')
      value = NULL;
    else if (token->data[count+1] != '"')
      (void)(token->data += count + 1), token->length -= count + 1;
    else if ((code = take_quoted(parser, type, field, token)) < 0)
      return code;

    uint8_t *octets = rdata->octets;
    uint16_t length;
    rdata->octets += 4;
    if ((code = parse_svc_param(
         parser, type, field, key, param, param->parse_lax, rdata, value)) < 0)
      return code;

    errors += (code != 0);
    key = htobe16(key);
    memcpy(octets, &key, sizeof(key));
    assert(rdata->octets >= octets && (rdata->octets - octets) <= 65535 + 4);
    length = (uint16_t)((rdata->octets - octets) - 4);
    length = htobe16(length);
    memcpy(octets + 2, &length, sizeof(length));
    take(parser, token);
  }

  if (likely(errors == 0 && whence != rdata->octets)) {
    assert(whence <= rdata->octets + 4);

    if (unlikely(out_of_order)) {
      SEMANTIC_ERROR(parser, "Out of order %s in %s", NAME(field), NAME(type));
    } else { // warn about missing or out-of-order parameters
      if (keys & 0x01)
        check_mandatory(parser, type, field, rdata, whence);
      // RFC9460 section 7.2:
      //   For "no-default-alpn", the presentation and wire-format values MUST
      //   be empty. When "no-default-alpn" is specified in an RR, "alpn" must
      //   also be specified in order for the RR to be "self-consistent"
      //   (Section 2.4.3).
      if ((keys & 0x04) && !(keys & 0x02))
        SEMANTIC_ERROR(parser, "%s with no-default-alpn but without alpn in %s",
                       NAME(field), NAME(type));
    }
  }

  return have_delimiter(parser, type, token);
}

// https://www.iana.org/assignments/dns-svcb/dns-svcb.xhtml
nonnull_all
static really_inline int32_t parse_svc_params(
  parser_t *parser,
  const type_info_t *type,
  const rdata_info_t *field,
  rdata_t *rdata,
  token_t *token)
{
  // propagate data as-is if secondary
  if (parser->options.secondary)
    return parse_svc_params_lax(parser, type, field, rdata, token);

  int32_t code;
  int32_t highest_key = -1;
  uint8_t *whence = rdata->octets;
  uint64_t keys = 0;

  while (is_contiguous(token)) {
    size_t count;
    uint16_t key;
    const token_t *value = token;
    const svc_param_info_t *param;

    if (!(count = scan_svc_param(token->data, &key, &param)))
      SYNTAX_ERROR(parser, "Invalid %s in %s", NAME(field), NAME(type));
    assert(param);

    if (key < 64)
      keys |= (1llu << key);

    if (token->data[count] != '=')
      value = NULL;
    else if (token->data[count+1] != '"')
      (void)(token->data += count + 1), token->length -= count + 1;
    else if ((code = take_quoted(parser, type, field, token)) < 0)
      return code;

    uint8_t *octets;
    uint16_t length;

    if (likely(key > highest_key)) {
      highest_key = key;
      octets = rdata->octets;
      rdata->octets += 4;
      if ((code = parse_svc_param(
           parser, type, field, key, param, param->parse, rdata, value)))
        return code;
      assert(rdata->octets >= octets && (rdata->octets - octets) <= 65535 + 4);
      length = (uint16_t)((rdata->octets - octets) - 4);
    } else {
      octets = whence;
      uint16_t smaller_key = 65535;

      // this can probably be done in a function or something
      while (octets < rdata->octets) {
        memcpy(&smaller_key, octets, sizeof(smaller_key));
        smaller_key = be16toh(smaller_key);
        if (key <= smaller_key)
          break;
        memcpy(&length, octets + 2, sizeof(length));
        length = be16toh(length);
        octets += length + 4;
      }

      assert(octets < rdata->octets);
      if (key == smaller_key)
        SEMANTIC_ERROR(parser, "Duplicate key in %s", NAME(type));

      rdata_t rdata_view;
      // RFC9460 section 2.2:
      //   SvcParamKeys SHALL appear in increasing numeric order.
      //
      // move existing data to end of the buffer and reset limit to
      // avoid allocating memory
      assert(rdata->octets - octets < ZONE_RDATA_SIZE);
      length = (uint16_t)(rdata->octets - octets);
      rdata_view.octets = octets + 4u;
      rdata_view.limit = parser->rdata->octets + (ZONE_RDATA_SIZE - length);
      // move data PADDING_SIZE past limit to ensure SIMD operatations
      // do not overwrite existing data
      memmove(rdata_view.limit + ZONE_BLOCK_SIZE, octets, length);
      if ((code = parse_svc_param(
           parser, type, field, key, param, param->parse, &rdata_view, token)))
        return code;
      assert(rdata_view.octets < rdata_view.limit);
      memmove(rdata_view.octets, rdata_view.limit + ZONE_BLOCK_SIZE, length);
      rdata->octets = rdata_view.octets + length;
      length = (uint16_t)(rdata_view.octets - octets) - 4u;
    }

    key = htobe16(key);
    memcpy(octets, &key, sizeof(key));
    length = htobe16(length);
    memcpy(octets + 2, &length, sizeof(length));
    take(parser, token);
  }

  if ((code = have_delimiter(parser, type, token)))
    return code;
  assert(whence);
  if ((keys & (1u << ZONE_SVC_PARAM_KEY_MANDATORY)) &&
      (code = check_mandatory(parser, type, field, rdata, whence)) < 0)
    return code;
  // RFC9460 section 7.2:
  //   For "no-default-alpn", the presentation and wire-format values MUST be
  //   empty. When "no-default-alpn" is specified in an RR, "alpn" must also
  //   be specified in order for the RR to be "self-consistent"
  //   (Section 2.4.3).
  if ((keys & 0x04) && !(keys & 0x02))
    SEMANTIC_ERROR(parser, "%s with no-default-alpn but without alpn in %s",
                   NAME(field), NAME(type));
  return 0;
}

#endif // SVCB_H
