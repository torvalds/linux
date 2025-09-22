/*	$OpenBSD: agentx.h,v 1.7 2022/10/14 15:26:58 martijn Exp $ */
/*
 * Copyright (c) 2019 Martijn van Duren <martijn@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <netinet/in.h>

#include <stdint.h>
#include <stddef.h>

struct agentx;
struct agentx_session;
struct agentx_context;
struct agentx_agentcaps;
struct agentx_region;
struct agentx_index;
struct agentx_object;
struct agentx_varbind;

enum agentx_request_type {
	AGENTX_REQUEST_TYPE_GET,
	AGENTX_REQUEST_TYPE_GETNEXT,
	AGENTX_REQUEST_TYPE_GETNEXTINCLUSIVE
};

#define AGENTX_MASTER_PATH "/var/agentx/master"
#define AGENTX_OID_MIN_LEN 2
#define AGENTX_OID_MAX_LEN 128
#define AGENTX_OID_INDEX_MAX_LEN 10
#define AGENTX_MIB2 1, 3, 6, 1, 2, 1
#define AGENTX_ENTERPRISES 1, 3, 6, 1, 4, 1
#define AGENTX_OID(...) (uint32_t []) { __VA_ARGS__ }, \
    (sizeof((uint32_t []) { __VA_ARGS__ }) / sizeof(uint32_t))

extern void (*agentx_log_fatal)(const char *, ...)
    __attribute__((__format__ (printf, 1, 2)));
extern void (*agentx_log_warn)(const char *, ...)
    __attribute__((__format__ (printf, 1, 2)));
extern void (*agentx_log_info)(const char *, ...)
    __attribute__((__format__ (printf, 1, 2)));
extern void (*agentx_log_debug)(const char *, ...)
    __attribute__((__format__ (printf, 1, 2)));

struct agentx *agentx(void (*)(struct agentx *, void *, int), void *);
void agentx_connect(struct agentx *, int);
void agentx_retry(struct agentx *);
void agentx_read(struct agentx *);
void agentx_write(struct agentx *);
extern void (*agentx_wantwrite)(struct agentx *, int);
void agentx_free(struct agentx *);
struct agentx_session *agentx_session(struct agentx *,
    uint32_t[], size_t, const char *, uint8_t);
void agentx_session_free(struct agentx_session *);
struct agentx_context *agentx_context(struct agentx_session *,
    const char *);
struct agentx_object *agentx_context_object_find(
    struct agentx_context *, const uint32_t[], size_t, int, int);
struct agentx_object *agentx_context_object_nfind(
    struct agentx_context *, const uint32_t[], size_t, int, int);
uint32_t agentx_context_uptime(struct agentx_context *);
void agentx_context_free(struct agentx_context *);
struct agentx_agentcaps *agentx_agentcaps(struct agentx_context *,
    uint32_t[], size_t, const char *);
void agentx_agentcaps_free(struct agentx_agentcaps *);
struct agentx_region *agentx_region(struct agentx_context *,
    uint32_t[], size_t, uint8_t);
void agentx_region_free(struct agentx_region *);
struct agentx_index *agentx_index_integer_new(struct agentx_region *,
    uint32_t[], size_t);
struct agentx_index *agentx_index_integer_any(struct agentx_region *,
    uint32_t[], size_t);
struct agentx_index *agentx_index_integer_value(struct agentx_region *,
    uint32_t[], size_t, int32_t);
struct agentx_index *agentx_index_integer_dynamic(
    struct agentx_region *, uint32_t[], size_t);
struct agentx_index *agentx_index_string_dynamic(
    struct agentx_region *, uint32_t[], size_t);
struct agentx_index *agentx_index_nstring_dynamic(
    struct agentx_region *, uint32_t[], size_t, size_t);
struct agentx_index *agentx_index_oid_dynamic(struct agentx_region *,
    uint32_t[], size_t);
struct agentx_index *agentx_index_noid_dynamic(struct agentx_region *,
    uint32_t[], size_t, size_t);
struct agentx_index *agentx_index_ipaddress_dynamic(
    struct agentx_region *, uint32_t[], size_t);
void agentx_index_free(struct agentx_index *);
struct agentx_object *agentx_object(struct agentx_region *, uint32_t[],
    size_t, struct agentx_index *[], size_t, int,
    void (*)(struct agentx_varbind *));
void agentx_object_free(struct agentx_object *);

void agentx_varbind_integer(struct agentx_varbind *, int32_t);
void agentx_varbind_string(struct agentx_varbind *, const char *);
void agentx_varbind_nstring(struct agentx_varbind *,
    const unsigned char *, size_t);
void agentx_varbind_printf(struct agentx_varbind *, const char *, ...)
    __attribute__((__format__ (printf, 2, 3)));
void agentx_varbind_null(struct agentx_varbind *);
void agentx_varbind_oid(struct agentx_varbind *, const uint32_t[],
    size_t);
void agentx_varbind_object(struct agentx_varbind *,
    struct agentx_object *);
void agentx_varbind_index(struct agentx_varbind *,
    struct agentx_index *);
void agentx_varbind_ipaddress(struct agentx_varbind *,
    const struct in_addr *);
void agentx_varbind_counter32(struct agentx_varbind *, uint32_t);
void agentx_varbind_gauge32(struct agentx_varbind *, uint32_t);
void agentx_varbind_unsigned32(struct agentx_varbind *, uint32_t);
void agentx_varbind_timeticks(struct agentx_varbind *, uint32_t);
void agentx_varbind_opaque(struct agentx_varbind *, const char *, size_t);
void agentx_varbind_counter64(struct agentx_varbind *, uint64_t);
void agentx_varbind_notfound(struct agentx_varbind *);
void agentx_varbind_error(struct agentx_varbind *);

enum agentx_request_type agentx_varbind_request(
    struct agentx_varbind *);
struct agentx_object *
    agentx_varbind_get_object(struct agentx_varbind *);
int32_t agentx_varbind_get_index_integer(struct agentx_varbind *,
    struct agentx_index *);
const unsigned char *agentx_varbind_get_index_string(
    struct agentx_varbind *, struct agentx_index *, size_t *, int *);
const uint32_t *agentx_varbind_get_index_oid(struct agentx_varbind *,
    struct agentx_index *, size_t *, int *);
const struct in_addr *agentx_varbind_get_index_ipaddress(
    struct agentx_varbind *, struct agentx_index *);
void agentx_varbind_set_index_integer(struct agentx_varbind *,
    struct agentx_index *, int32_t);
void agentx_varbind_set_index_string(struct agentx_varbind *,
    struct agentx_index *, const char *);
void agentx_varbind_set_index_nstring(struct agentx_varbind *,
    struct agentx_index *, const unsigned char *, size_t);
void agentx_varbind_set_index_oid(struct agentx_varbind *,
    struct agentx_index *, const uint32_t *, size_t);
void agentx_varbind_set_index_object(struct agentx_varbind *,
    struct agentx_index *, struct agentx_object *);
void agentx_varbind_set_index_ipaddress(struct agentx_varbind *,
    struct agentx_index *, const struct in_addr *);
