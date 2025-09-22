/*	$OpenBSD: application.h,v 1.13 2023/11/12 16:07:34 martijn Exp $	*/

/*
 * Copyright (c) 2021 Martijn van Duren <martijn@openbsd.org>
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

#include <sys/tree.h>

#include <ber.h>

#include <stdint.h>

#define APPL_OIDMAX 128 /* RFC 2578 Section 3.5 */
#define APPL_CONTEXTNAME_MAX 32 /* RFC 3415 vacmContextName */

/* Combination of RFC 3416 error-status and RFC 2741 res.error */
enum appl_error {
	APPL_ERROR_NOERROR		= 0,
	APPL_ERROR_TOOBIG		= 1,
	APPL_ERROR_NOSUCHNAME		= 2,
	APPL_ERROR_BADVALUE		= 3,
	APPL_ERROR_READONLY		= 4,
	APPL_ERROR_GENERR		= 5,
	APPL_ERROR_NOACCESS		= 6,
	APPL_ERROR_WRONGTYPE		= 7,
	APPL_ERROR_WRONGLENGTH		= 8,
	APPL_ERROR_WRONGENCODING	= 9,
	APPL_ERROR_WRONGVALUE		= 10,
	APPL_ERROR_NOCREATION		= 11,
	APPL_ERROR_INCONSISTENTVALUE	= 12,
	APPL_ERROR_RESOURCEUNAVAILABLE	= 13,
	APPL_ERROR_COMMITFAILED		= 14,
	APPL_ERROR_UNDOFAILED		= 15,
	APPL_ERROR_AUTHORIZATIONERROR	= 16,
	APPL_ERROR_NOTWRITABLE		= 17,
	APPL_ERROR_INCONSISTENTNAME	= 18,
	APPL_ERROR_OPENFAILED		= 256,
	APPL_ERROR_NOTOPEN		= 257,
	APPL_ERROR_INDEXWRONGTYPE	= 258,
	APPL_ERROR_INDEXALREADYALLOCATED= 259,
	APPL_ERROR_INDEXNONEAVAILABLE	= 260,
	APPL_ERROR_INDEXNOTALLOCATED	= 261,
	APPL_ERROR_UNSUPPORTEDCONTEXT	= 262,
	APPL_ERROR_DUPLICATEREGISTRATION= 263,
	APPL_ERROR_UNKNOWNREGISTRATION	= 264,
	APPL_ERROR_UNKNOWNAGENTCAPS	= 265,
	APPL_ERROR_PARSEERROR		= 266,
	APPL_ERROR_REQUESTDENIED	= 267,
	APPL_ERROR_PROCESSINGERROR	= 268
};

enum appl_exception {
	APPL_EXC_NOSUCHOBJECT		= 0,
	APPL_EXC_NOSUCHINSTANCE		= 1,
	APPL_EXC_ENDOFMIBVIEW		= 2
};

enum appl_close_reason {
	APPL_CLOSE_REASONOTHER		= 1,
	APPL_CLOSE_REASONPARSEERROR	= 2,
	APPL_CLOSE_REASONPROTOCOLERROR	= 3,
	APPL_CLOSE_REASONTIMEOUTS	= 4,
	APPL_CLOSE_REASONSHUTDOWN	= 5,
	APPL_CLOSE_REASONBYMANAGER	= 6
};

struct appl_varbind {
	int8_t av_include; /* RFC 2741 section 5.1 */
	struct ber_oid av_oid;
	struct ber_oid av_oid_end;
	struct ber_element *av_value;

	struct appl_varbind *av_next;
};

struct snmp_message;
enum snmp_version;
struct appl_backend;
struct appl_context;

struct appl_backend_functions {
	void (*ab_close)(struct appl_backend *, enum appl_close_reason);
	void (*ab_get)(struct appl_backend *, int32_t, int32_t, const char *,
	    struct appl_varbind *);
	void (*ab_getnext)(struct appl_backend *, int32_t, int32_t, const char *,
	    struct appl_varbind *);
	/*
	 * RFC 3416 section 3: non-repeaters/max-repetitions = 0..max-bindings
	 * max-bindings = (2^31)-1
	 * RFC 2741 section 6.2.7: non-repeaters/max-repetitions = 2 bytes
	 * Go for the lowest common denominator.
	 */
	void (*ab_getbulk)(struct appl_backend *, int32_t, int32_t, int16_t,
	    int16_t, const char *, struct appl_varbind *);
};

struct appl_backend {
	char *ab_name;
	void *ab_cookie;
	uint8_t ab_retries;
	int ab_range; /* Supports searchrange */
	struct appl_backend_functions *ab_fn;
	/*
	 * Only store downstream requests: they reference upstream and when
	 * downstream requests are done the upstream request is finalized.
	 */
	RB_HEAD(appl_requests, appl_request_downstream) ab_requests;
};

void appl(void);
void appl_init(void);
void appl_shutdown(void);
struct appl_context *appl_context(const char *, int);
enum appl_error appl_addagentcaps(const char *, struct ber_oid *, const char *,
    struct appl_backend *);
enum appl_error appl_removeagentcaps(const char *, struct ber_oid *,
    struct appl_backend *);
struct ber_element *appl_sysorlastchange(struct ber_oid *);
struct ber_element *appl_sysortable(struct ber_oid *);
struct ber_element *appl_sysortable_getnext(int8_t, struct ber_oid *);
struct ber_element *appl_targetmib(struct ber_oid *);
enum appl_error appl_register(const char *, uint32_t, uint8_t, struct ber_oid *,
    int, int, uint8_t, uint32_t, struct appl_backend *);
enum appl_error appl_unregister(const char *, uint8_t, struct ber_oid *,
    uint8_t, uint32_t, struct appl_backend *);
void appl_close(struct appl_backend *);
void appl_processpdu(struct snmp_message *, const char *,
    enum snmp_version , struct ber_element *);
void appl_response(struct appl_backend *, int32_t, enum appl_error, int16_t,
    struct appl_varbind *);
void appl_report(struct snmp_message *, int32_t, struct ber_oid *);
struct ber_element *appl_exception(enum appl_exception);

/* application_agentx.c */
void	 appl_agentx(void);
void	 appl_agentx_init(void);
void	 appl_agentx_shutdown(void);
void	 appl_agentx_backend(int);

/* application_blocklist.c */
void	 appl_blocklist_init(void);
void	 appl_blocklist_shutdown(void);

/* application_internal.c */
void	 appl_internal_init(void);
void	 appl_internal_shutdown(void);
const char *appl_internal_object_int(struct ber_oid *, int32_t);
const char *appl_internal_object_string(struct ber_oid *, char *);
