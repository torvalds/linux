/*
 * zone.h -- (DNS) presentation format parser
 *
 * Copyright (c) 2022-2024, NLnet Labs. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */
#ifndef ZONE_H
#define ZONE_H

/**
 * @file
 * @brief simdzone main header
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stddef.h>

#include "zone/attributes.h"
#include "zone/export.h"

#if defined (__cplusplus)
extern "C" {
#endif

/**
 * @defgroup class_codes Class codes
 *
 * Supported CLASSes.
 *
 * See @iana{DNS CLASSes,dns-parameters,dns-parameters-2} for a list of
 * classes registered by IANA.
 *
 * @{
 */
/** Internet @rfc{1035} */
#define ZONE_CLASS_IN (1u)
/** CSNET @rfc{1035} @obsolete */
#define ZONE_CLASS_CS (2u)
/** CHAOS @rfc{1035} */
#define ZONE_CLASS_CH (3u)
/** Hesiod @rfc{1035} */
#define ZONE_CLASS_HS (4u)
/** Any (QCLASS) @rfc{1035} */
#define ZONE_CLASS_ANY (255u)
/** @} */

/**
 * @defgroup type_codes Type codes
 *
 * Supported resource record (RR) TYPEs.
 *
 * See @iana{RR TYPEs,dns-parameters,dns-parameters-4} for a list of
 * types registered by IANA.
 *
 * @{
 */
/** Host address @rfc{1035} */
#define ZONE_TYPE_A (1u)
/** Authoritative name server @rfc{1035} */
#define ZONE_TYPE_NS (2u)
/** Mail destination @rfc{1035} @obsolete */
#define ZONE_TYPE_MD (3u)
/** Mail forwarder @rfc{1035} @obsolete */
#define ZONE_TYPE_MF (4u)
/** Canonical name for an alias @rfc{1035} */
#define ZONE_TYPE_CNAME (5u)
/** Marks the start of authority @rfc{1035} */
#define ZONE_TYPE_SOA (6u)
/** Mailbox domain name @rfc{1035} @experimental */
#define ZONE_TYPE_MB (7u)
/** Mail group member @rfc{1035} @experimental */
#define ZONE_TYPE_MG (8u)
/** Mail rename domain name @rfc{1035} @experimental */
#define ZONE_TYPE_MR (9u)
/** Anything @rfc{883} @obsolete */
#define ZONE_TYPE_NULL (10u)
/** Well known service description @rfc{1035} */
#define ZONE_TYPE_WKS (11u)
/** Domain name pointer @rfc{1035} */
#define ZONE_TYPE_PTR (12u)
/** Host information @rfc{1035} */
#define ZONE_TYPE_HINFO (13u)
/** Mailbox or mail list information @rfc{1035} */
#define ZONE_TYPE_MINFO (14u)
/** Mail exchange @rfc{1035} */
#define ZONE_TYPE_MX (15u)
/** Text strings @rfc{1035} */
#define ZONE_TYPE_TXT (16u)
/** Responsible person @rfc{1035} */
#define ZONE_TYPE_RP (17u)
/** AFS Data Base location @rfc{1183} @rfc{5864} */
#define ZONE_TYPE_AFSDB (18u)
/** X.25 PSDN address @rfc{1183} */
#define ZONE_TYPE_X25 (19u)
/** ISDN address @rfc{1183} */
#define ZONE_TYPE_ISDN (20u)
/** Route Through @rfc{1183} */
#define ZONE_TYPE_RT (21u)
/** NSAP address, NSAP style A record @rfc{1706} */
#define ZONE_TYPE_NSAP (22u)
/** Domain name pointer, NSAP style @rfc{1348} @rfc{1637} */
#define ZONE_TYPE_NSAP_PTR (23u)
/** Signature @rfc{2535} */
#define ZONE_TYPE_SIG (24u)
/** Public key @rfc{2535} @rfc{2930} */
#define ZONE_TYPE_KEY (25u)
/** X.400 mail mapping information @rfc{2163} */
#define ZONE_TYPE_PX (26u)
/** Geographical Position @rfc{1712} */
#define ZONE_TYPE_GPOS (27u)
/** IPv6 Address @rfc{3596} */
#define ZONE_TYPE_AAAA (28u)
/** Location Information @rfc{1876} */
#define ZONE_TYPE_LOC (29u)
/** Next domain @rfc{3755} @rfc{2535} @obsolete */
#define ZONE_TYPE_NXT (30u)
/** Endpoint Identifier */
#define ZONE_TYPE_EID (31u)
/** Nimrod Locator */
#define ZONE_TYPE_NIMLOC (32u)
/** Server Selection @rfc{2782} */
#define ZONE_TYPE_SRV (33u)
/** ATM Address */
#define ZONE_TYPE_ATMA (34u)
/** Naming Authority Pointer @rfc{2915} @rfc{2168} @rfc{3403} */
#define ZONE_TYPE_NAPTR (35u)
/** Key Exchanger @rfc{2230} */
#define ZONE_TYPE_KX (36u)
/** CERT @rfc{4398}*/
#define ZONE_TYPE_CERT (37u)
/** IPv6 Address @rfc{3226} @rfc{2874} @rfc{6563} @obsolete */
#define ZONE_TYPE_A6 (38u)
/** DNAME @rfc{6672} */
#define ZONE_TYPE_DNAME (39u)
/** SINK @draft{eastlake, kitchen-sink} */
#define ZONE_TYPE_SINK (40u)
/** Address Prefix List @rfc{3123} */
#define ZONE_TYPE_APL (42u)
/** Delegation Signer @rfc{4034} @rfc{3658} */
#define ZONE_TYPE_DS (43u)
/** SSH Key Fingerprint @rfc{4255} */
#define ZONE_TYPE_SSHFP (44u)
/** IPsec public key @rfc{4025} */
#define ZONE_TYPE_IPSECKEY (45u)
/** Resource Record Signature @rfc{4034} @rfc{3755} */
#define ZONE_TYPE_RRSIG (46u)
/** Next Secure @rfc{4034} @rfc{3755} */
#define ZONE_TYPE_NSEC (47u)
/** DNS Public Key @rfc{4034} @rfc{3755} */
#define ZONE_TYPE_DNSKEY (48u)
/** DHCID @rfc{4701} */
#define ZONE_TYPE_DHCID (49u)
/** NSEC3 @rfc{5155} */
#define ZONE_TYPE_NSEC3 (50u)
/** NSEC3PARAM @rfc{5155} */
#define ZONE_TYPE_NSEC3PARAM (51u)
/** TLSA @rfc{6698} */
#define ZONE_TYPE_TLSA (52u)
/** S/MIME cert association @rfc{8162} */
#define ZONE_TYPE_SMIMEA (53u)
/** Host Identity Protocol @rfc{8005} */
#define ZONE_TYPE_HIP (55u)
/** NINFO */
#define ZONE_TYPE_NINFO (56u)
/** RKEY */
#define ZONE_TYPE_RKEY (57u)
/** Trust Anchor LINK @draft{ietf, dnsop-dnssec-trust-history} */
#define ZONE_TYPE_TALINK (58u)
/** Child DS @rfc{7344} */
#define ZONE_TYPE_CDS (59u)
/** DNSKEY(s) the Child wants reflected in DS @rfc{7344} */
#define ZONE_TYPE_CDNSKEY (60u)
/** OpenPGP Key @rfc{7929} */
#define ZONE_TYPE_OPENPGPKEY (61u)
/** Child-To-Parent Synchronization @rfc{7477} */
#define ZONE_TYPE_CSYNC (62u)
/** Zone message digest @rfc{8976} */
#define ZONE_TYPE_ZONEMD (63u)
/** Service binding @rfc{9460} */
#define ZONE_TYPE_SVCB (64u)
/** Service binding @rfc{9460} */
#define ZONE_TYPE_HTTPS (65u)
/** Endpoint discovery for delegation synchronization @draft{ietf, dnsop-generalized-notify} */
#define ZONE_TYPE_DSYNC (66u)
/** Sender Policy Framework @rfc{7208} */
#define ZONE_TYPE_SPF (99u)
/** Node Identifier @rfc{6742} */
#define ZONE_TYPE_NID (104u)
/** 32-bit Locator for ILNPv4-capable nodes @rfc{6742} */
#define ZONE_TYPE_L32 (105u)
/** 64-bit Locator for ILNPv6-capable nodes @rfc{6742} */
#define ZONE_TYPE_L64 (106u)
/** Name of an ILNP subnetwork @rfc{6742} */
#define ZONE_TYPE_LP (107u)
/** EUI-48 address @rfc{7043} */
#define ZONE_TYPE_EUI48 (108u)
/** EUI-64 address @rfc{7043} */
#define ZONE_TYPE_EUI64 (109u)
/** Uniform Resource Identifier @rfc{7553} */
#define ZONE_TYPE_URI (256u)
/** Certification Authority Restriction @rfc{6844} */
#define ZONE_TYPE_CAA (257u)
/** DNS Authoritative Source (DNS-AS) */
#define ZONE_TYPE_AVC (258u)
/** Digital Object Architecture @draft{durand, doa-over-dns} */
#define ZONE_TYPE_DOA (259u)
/** Automatic Multicast Tunneling Relay @rfc{8777} */
#define ZONE_TYPE_AMTRELAY (260u)
/** Resolver Information as Key/Value Pairs @rfc{9606} */
#define ZONE_TYPE_RESINFO (261u)
/** Public wallet address */
#define ZONE_TYPE_WALLET (262u)
/** BP Convergence Layer Adapter */
#define ZONE_TYPE_CLA (263u)
/** BP Node Number */
#define ZONE_TYPE_IPN (264u)
/** DNSSEC Trust Authorities */
#define ZONE_TYPE_TA (32768u)
/** DNSSEC Lookaside Validation @rfc{4431} @obsolete */
#define ZONE_TYPE_DLV (32769u)
/** @} */

/**
 * @defgroup svc_params Service Parameter Keys
 *
 * Supported service parameters.
 *
 * See @iana{Service Parameter Keys (SvcParamKeys),dns-svcb,dns-svcparamkeys}
 * for a list of service parameter keys registered by IANA.
 *
 * @{
 */
/** Parameters clients must not ignore @rfc{9460} */
#define ZONE_SVC_PARAM_KEY_MANDATORY (0u)
/** Application Layer Protocol Negotiation (ALPN) protocol identifiers @rfc{9460} */
#define ZONE_SVC_PARAM_KEY_ALPN (1u)
/** No support for default protocol (alpn must be specified) @rfc{9460} */
#define ZONE_SVC_PARAM_KEY_NO_DEFAULT_ALPN (2u)
/** TCP or UDP port for alternative endpoint @rfc{9460} */
#define ZONE_SVC_PARAM_KEY_PORT (3u)
/** IPv4 address hints @rfc{9460} */
#define ZONE_SVC_PARAM_KEY_IPV4HINT (4u)
/** Encrypted ClientHello (ECH) configuration @draft{ietf, tls-svcb-ech} */
#define ZONE_SVC_PARAM_KEY_ECH (5u)
/** IPv6 address hints @rfc{9460} */
#define ZONE_SVC_PARAM_KEY_IPV6HINT (6u)
/** URI template in relative form @rfc{9461} */
#define ZONE_SVC_PARAM_KEY_DOHPATH (7u)
/** Target is an Oblivious HTTP service @rfc{9540} */
#define ZONE_SVC_PARAM_KEY_OHTTP (8u)
/** Supported groups in TLS @draft{ietf, tls-key-share-prediction} */
#define ZONE_SVC_PARAM_KEY_TLS_SUPPORTED_GROUPS (9u)
/** Reserved ("invalid key") @rfc{9460} */
#define ZONE_SVC_PARAM_KEY_INVALID_KEY (65535u)
/** @} */

/**
 * Number of bytes per block.
 *
 * Higher throughput is achieved by block-based operation. The size of a
 * block is determined by the word size of the CPU. To avoid pipeline flushes
 * as much as possible buffers are required to be padded by the number of
 * bytes in a single block.
 *
 * @warning The input buffer to @zone_parse_string is required to be
 *          null-terminated and padded, which is somewhat counter intuitive. A
 *          future release may lift this requirement (@issue{174}).
 */
#define ZONE_BLOCK_SIZE (64)

/**
 * Number of blocks per window.
 *
 * Master files can become quite large and are read in multiples of blocks.
 * The input buffer is expanded as needed.
 */
#define ZONE_WINDOW_SIZE (256 * ZONE_BLOCK_SIZE) // 16KB

/** Maximum size of domain name. */
#define ZONE_NAME_SIZE (255)

typedef struct zone_name_buffer zone_name_buffer_t;
struct zone_name_buffer {
  /** Length of domain name stored in buffer. */
  size_t length;
  /** Maximum number of octets in a domain name plus padding. */
  uint8_t octets[ ZONE_NAME_SIZE + ZONE_BLOCK_SIZE ];
};

/** Maximum size of RDATA section. */
#define ZONE_RDATA_SIZE (65535)

typedef struct zone_rdata_buffer zone_rdata_buffer_t;
struct zone_rdata_buffer {
  /** Maximum number of octets in RDATA section plus padding. */
  uint8_t octets[ ZONE_RDATA_SIZE + ZONE_BLOCK_SIZE ];
};

/**
 * @brief Tape capacity per-file.
 *
 * Tape capacity must be sufficiently large to hold every token from a single
 * worst-case read (e.g. 64 consecutive line feeds). Not likely to occur in
 * practice, therefore, to optimize throughput, allocate at least twice the
 * size so consecutive index operations can be performed.
 */
#define ZONE_TAPE_SIZE ((100 * ZONE_BLOCK_SIZE) + ZONE_BLOCK_SIZE)

typedef struct zone_file zone_file_t;
struct zone_file {
  /** @private */
  zone_file_t *includer;
  /** @private */
  zone_name_buffer_t origin, owner;
  /** @private */
  uint16_t last_type;
  /** Last stated TTL. */
  uint32_t last_ttl;
  /** Last parsed TTL in $TTL entry. */
  uint32_t dollar_ttl;
  /** TTL passed to accept callback. */
  uint32_t *ttl;
  /** Default TTL passed to accept. */
  /** Last stated TTL is used as default unless $TTL entry was found. */
  uint32_t *default_ttl;
  /** @private */
  uint16_t last_class;
  /** Number of lines spanned by RR. */
  /** Non-terminating line feeds, i.e. escaped line feeds, line feeds in
      quoted sections or within parentheses, are counted, but deferred for
      consistency in error reports */
  size_t span;
  /** Starting line of RR. */
  size_t line;
  /** Filename in control directive. */
  char *name;
  /** Absolute path. */
  char *path;
  /** @private */
  FILE *handle;
  /** @private */
  bool grouped;
  /** @private */
  bool start_of_line;
  /** @private */
  uint8_t end_of_file;
  /** @private */
  struct {
    size_t index, length, size;
    char *data;
  } buffer;
  /** @private */
  /** scanner state is kept per-file */
  struct {
    uint64_t in_comment;
    uint64_t in_quoted;
    uint64_t is_escaped;
    uint64_t follows_contiguous;
  } state;
  /** @private */
  /** vector of tokens generated by the scanner guaranteed to be large
      enough to hold every token for a single read + terminators */
  struct { const char **head, **tail, *tape[ZONE_TAPE_SIZE + 2]; } fields;
  struct { const char **head, **tail, *tape[ZONE_TAPE_SIZE + 1]; } delimiters;
  struct { uint16_t *head, *tail, tape[ZONE_TAPE_SIZE + 1]; } newlines;
};

typedef struct zone_parser zone_parser_t;
struct zone_parser;

/**
 * @brief Signature of callback function that is invoked for log messages.
 *
 * By default messages are printed to stdout (info) and stderr (warnings,
 * errors). A custom log handler (callback) may be provided for better
 * integration of reporting.
 *
 * @note file maybe NULL if initial file does not exist.
 */
typedef void(*zone_log_t)(
  zone_parser_t *,
  uint32_t, // priority
  const char *, // file
  size_t, // line
  const char *, // message
  void *); // user data

/**
 * @brief Domain name and corresponding length in wire format.
 */
typedef struct zone_name zone_name_t;
struct zone_name {
  /** Length of domain name. */
  uint8_t length;
  /** Absolute, uncompressed, domain name in wire format. */
  const uint8_t *octets;
};

/**
 * @brief Signature of callback function invoked for each RR.
 *
 * Header is in host order, RDATA section is in network order.
 */
typedef int32_t(*zone_accept_t)(
  zone_parser_t *,
  const zone_name_t *, // owner (length + octets)
  uint16_t, // type
  uint16_t, // class
  uint32_t, // ttl
  uint16_t, // rdlength
  const uint8_t *, // rdata
  void *); // user data

/**
 * @brief Signature of callback function invoked on $INCLUDE.
 *
 * Signal file name in $INCLUDE directive to application. Useful for
 * dependency tracking, etc.
 */
typedef int32_t(*zone_include_t)(
  zone_parser_t *,
  const char *, // name in $INCLUDE entry
  const char *, // fully qualified path
  void *); // user data

/**
 * @brief Available configuration options.
 */
typedef struct {
  /** Non-strict mode of operation. */
  /** Authoritative servers may choose to be more lenient when operating as
      a secondary as data may have been transferred over AXFR/IXFR that
      would have triggered an error otherwise. */
  bool secondary;
  /** Disable $INCLUDE directive. */
  /** Useful in setups where untrusted input may be offered. */
  bool no_includes;
  /** Maximum $INCLUDE depth. 0 for default. */
  uint32_t include_limit;
  /** Enable 1h2m3s notations for TTLS. */
  bool pretty_ttls;
  /** Origin in wire format. */
  zone_name_t origin;
  /** Default TTL to use. */
  uint32_t default_ttl;
  /** Default CLASS to use. */
  uint16_t default_class;
  struct {
    /** Priorities NOT to write out. */
    uint32_t mask;
    /** Callback invoked to write out log messages. */
    zone_log_t callback;
  } log;
  struct {
    /** Callback invoked for each RR. */
    zone_accept_t callback;
  } accept;
  struct {
    /** Callback invoked for each $INCLUDE entry. */
    zone_include_t callback;
  } include;
} zone_options_t;

/**
 * @brief Scratch buffer space reserved for parser.
 *
 * @note Future versions may leverage multiple buffers to improve throughput
 *       as parsing and committing resource records are disjunct operations.
 */
typedef struct zone_buffers zone_buffers_t;
struct zone_buffers {
  /** Number of name and rdata buffers available. */
  size_t size;
  /** Vector of name buffers to use as scratch buffer. */
  zone_name_buffer_t *owner;
  /** Vector of rdata buffers to use as scratch buffer. */
  zone_rdata_buffer_t *rdata;
};

/**
 * @brief Parser state.
 * @warning Do not modify directly.
 */
struct zone_parser {
  /** @private */
  zone_options_t options;
  /** @private */
  void *user_data;
  struct {
    size_t size;
    struct {
      size_t active;
      zone_name_buffer_t *blocks;
    } owner;
    struct {
      size_t active;
      zone_rdata_buffer_t *blocks;
    } rdata;
  } buffers;
  /** @private */
  zone_name_buffer_t *owner;
  /** @private */
  zone_rdata_buffer_t *rdata;
  /** @private */
  zone_file_t *file, first;
};

/**
 * @defgroup return_codes Return codes
 *
 * @{
 */
/** Success. */
#define ZONE_SUCCESS (0)
/** A syntax error occurred. */
#define ZONE_SYNTAX_ERROR (-256)  // (-1 << 8)
/** A semantic error occurred. */
#define ZONE_SEMANTIC_ERROR (-512)  // (-2 << 8)
/** Operation failed due to lack of memory. */
#define ZONE_OUT_OF_MEMORY (-768)  // (-3 << 8)
/** Bad parameter value. */
#define ZONE_BAD_PARAMETER (-1024)  // (-4 << 8)
/** Error reading zone file. */
#define ZONE_READ_ERROR (-1280)  // (-5 << 8)
/** Control directive or support for record type is not implemented. */
#define ZONE_NOT_IMPLEMENTED (-1536)  // (-6 << 8)
/** Specified file does not exist. */
#define ZONE_NOT_A_FILE (-1792)  // (-7 << 8)
/** Access to specified file is not allowed. */
#define ZONE_NOT_PERMITTED (-2048)  // (-8 << 8)
/** @} */

/**
 * @brief Parse zone file
 *
 * Parse file containing resource records.
 *
 * @param[in]  parser     Zone parser
 * @param[in]  options    Settings used for parsing.
 * @param[in]  buffers    Scratch buffers used for parsing.
 * @param[in]  path       Path of master file to parse.
 * @param[in]  user_data  Pointer passed verbatim to callbacks.
 *
 * @returns @ref ZONE_SUCCESS on success or a negative number on error.
 */
ZONE_EXPORT int32_t
zone_parse(
  zone_parser_t *parser,
  const zone_options_t *options,
  zone_buffers_t *buffers,
  const char *path,
  void *user_data)
zone_nonnull((1,2,3,4));

/**
 * @brief Parse zone from string
 *
 * Parse string containing resource records in presentation format.
 *
 * @warning The input string must be null terminated and padded with at least
 *          @ref ZONE_BLOCK_SIZE bytes.
 *
 * @param[in]  parser     Zone parser
 * @param[in]  options    Settings used for parsing.
 * @param[in]  buffers    Scratch buffers used by parsing.
 * @param[in]  string     Input string.
 * @param[in]  length     Length of string (excluding null byte and padding).
 * @param[in]  user_data  Pointer passed verbatim to callbacks.
 *
 * @returns @ref ZONE_SUCCESS on success or a negative number on error.
 */
ZONE_EXPORT int32_t
zone_parse_string(
  zone_parser_t *parser,
  const zone_options_t *options,
  zone_buffers_t *buffers,
  const char *string,
  size_t length,
  void *user_data)
zone_nonnull((1,2,3,4));

/**
 * @defgroup log_priorities Log categories.
 *
 * @note No direct relation between log categories and error codes exists.
 *       Log categories communicate the importance of the log message, error
 *       codes communicate what went wrong to the caller.
 * @{
 */
/** Error condition. */
/** @hideinitializer */
#define ZONE_ERROR (1u<<1)
/** Warning condition. */
/** @hideinitializer */
#define ZONE_WARNING (1u<<2)
/** Informational message. */
/** @hideinitializer */
#define ZONE_INFO (1u<<3)
/** @} */

/**
 * @brief Write message to active log handler.
 *
 * The zone parser operates on a per-record base and therefore cannot detect
 * errors that span records. e.g. SOA records being specified more than once.
 * The user may print a message using the active log handler, keeping the
 * error message format consistent.
 *
 * @param[in]  parser    Zone parser
 * @param[in]  priority  Log priority
 * @param[in]  format    Format string compatible with printf
 * @param[in]  ...       Variadic arguments corresponding to #format
 */
ZONE_EXPORT void zone_log(
  zone_parser_t *parser,
  uint32_t priority,
  const char *format,
  ...)
zone_nonnull((1,3))
zone_format_printf(3,4);

/**
 * @brief Write error message to active log handler.
 * @hideinitializer
 *
 * Shorthand to write out error message via @ref zone_log if error messages are
 * not to be discarded.
 *
 * @param[in]  parser  Zone parser
 * @param[in]  format  Format string
 * @param[in]  ...     Variadic arguments corresponding to #format
 */
#define zone_error(parser, ...) \
  (((parser)->options.log.mask & ZONE_ERROR) ? \
     (void)0 : zone_log((parser), ZONE_ERROR, __VA_ARGS__))

/**
 * @brief Write warning message to active log handler.
 * @hideinitializer
 *
 * Shorthand to write out warning message via @ref zone_log if warning messages
 * are not to be discarded.
 *
 * @param[in]  parser  Zone parser
 * @param[in]  format  Format string compatible with printf.
 * @param[in]  ...     Variadic arguments corresponding to @format.
 */
#define zone_warning(parser, ...) \
  (((parser)->options.mask & ZONE_WARNING) ? \
     (void)0 : zone_log((parser), ZONE_WARNING, __VA_ARGS__))

/**
 * @brief Write informational message to active log handler.
 * @hideinitializer
 *
 * Shorthand to write out informational message via @ref zone_log if
 * informational messages are not be discarded.
 *
 * @param[in]  parser  Zone parser.
 * @param[in]  format  Format string compatible with printf.
 * @param[in]  ...     Variadic arguments corresponding to @format.
 */
#define zone_info(parser, ...) \
  (((parser)->options.mask & ZONE_INFO) ? \
     (void)0 : zone_log((parser), ZONE_INFO, __VA_ARGS__))

#if defined(__cplusplus)
}
#endif

#endif // ZONE_H
