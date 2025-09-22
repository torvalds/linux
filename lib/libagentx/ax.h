/*	$OpenBSD: ax.h,v 1.5 2023/10/24 08:54:52 martijn Exp $ */
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

#include <stdint.h>

#define AX_PDU_FLAG_INSTANCE_REGISTRATION (1 << 0)
#define AX_PDU_FLAG_NEW_INDEX (1 << 1)
#define AX_PDU_FLAG_ANY_INDEX (1 << 2)
#define AX_PDU_FLAG_NON_DEFAULT_CONTEXT (1 << 3)
#define AX_PDU_FLAG_NETWORK_BYTE_ORDER (1 << 4)

#define AX_PRIORITY_DEFAULT 127

enum ax_byte_order {
	AX_BYTE_ORDER_BE,
	AX_BYTE_ORDER_LE
};

#if BYTE_ORDER == BIG_ENDIAN
#define AX_BYTE_ORDER_NATIVE AX_BYTE_ORDER_BE
#else
#define AX_BYTE_ORDER_NATIVE AX_BYTE_ORDER_LE
#endif

enum ax_pdu_type {
	AX_PDU_TYPE_OPEN		= 1,
	AX_PDU_TYPE_CLOSE		= 2,
	AX_PDU_TYPE_REGISTER		= 3,
	AX_PDU_TYPE_UNREGISTER		= 4,
	AX_PDU_TYPE_GET			= 5,
	AX_PDU_TYPE_GETNEXT		= 6,
	AX_PDU_TYPE_GETBULK		= 7,
	AX_PDU_TYPE_TESTSET		= 8,
	AX_PDU_TYPE_COMMITSET		= 9,
	AX_PDU_TYPE_UNDOSET		= 10,
	AX_PDU_TYPE_CLEANUPSET		= 11,
	AX_PDU_TYPE_NOTIFY		= 12,
	AX_PDU_TYPE_PING		= 13,
	AX_PDU_TYPE_INDEXALLOCATE	= 14,
	AX_PDU_TYPE_INDEXDEALLOCATE	= 15,
	AX_PDU_TYPE_ADDAGENTCAPS	= 16,
	AX_PDU_TYPE_REMOVEAGENTCAPS	= 17,
	AX_PDU_TYPE_RESPONSE		= 18
};

enum ax_pdu_error {
	AX_PDU_ERROR_NOERROR			= 0,
	AX_PDU_ERROR_GENERR			= 5,
	AX_PDU_ERROR_NOACCESS			= 6,
	AX_PDU_ERROR_WRONGTYPE			= 7,
	AX_PDU_ERROR_WRONGLENGTH		= 8,
	AX_PDU_ERROR_WRONGENCODING		= 9,
	AX_PDU_ERROR_WRONGVALUE			= 10,
	AX_PDU_ERROR_NOCREATION			= 11,
	AX_PDU_ERROR_INCONSISTENTVALUE		= 12,
	AX_PDU_ERROR_RESOURCEUNAVAILABLE	= 13,
	AX_PDU_ERROR_COMMITFAILED		= 14,
	AX_PDU_ERROR_UNDOFAILED			= 15,
	AX_PDU_ERROR_NOTWRITABLE		= 17,
	AX_PDU_ERROR_INCONSISTENTNAME		= 18,
	AX_PDU_ERROR_OPENFAILED			= 256,
	AX_PDU_ERROR_NOTOPEN			= 257,
	AX_PDU_ERROR_INDEXWRONGTYPE		= 258,
	AX_PDU_ERROR_INDEXALREADYALLOCATED	= 259,
	AX_PDU_ERROR_INDEXNONEAVAILABLE		= 260,
	AX_PDU_ERROR_INDEXNOTALLOCATED		= 261,
	AX_PDU_ERROR_UNSUPPORTEDCONETXT		= 262,
	AX_PDU_ERROR_DUPLICATEREGISTRATION	= 263,
	AX_PDU_ERROR_UNKNOWNREGISTRATION	= 264,
	AX_PDU_ERROR_UNKNOWNAGENTCAPS		= 265,
	AX_PDU_ERROR_PARSEERROR			= 266,
	AX_PDU_ERROR_REQUESTDENIED		= 267,
	AX_PDU_ERROR_PROCESSINGERROR		= 268
};

enum ax_data_type {
	AX_DATA_TYPE_INTEGER		= 2,
	AX_DATA_TYPE_OCTETSTRING	= 4,
	AX_DATA_TYPE_NULL		= 5,
	AX_DATA_TYPE_OID		= 6,
	AX_DATA_TYPE_IPADDRESS		= 64,
	AX_DATA_TYPE_COUNTER32		= 65,
	AX_DATA_TYPE_GAUGE32		= 66,
	AX_DATA_TYPE_TIMETICKS		= 67,
	AX_DATA_TYPE_OPAQUE		= 68,
	AX_DATA_TYPE_COUNTER64		= 70,
	AX_DATA_TYPE_NOSUCHOBJECT	= 128,
	AX_DATA_TYPE_NOSUCHINSTANCE	= 129,
	AX_DATA_TYPE_ENDOFMIBVIEW	= 130
};

enum ax_close_reason {
	AX_CLOSE_OTHER			= 1,
	AX_CLOSEN_PARSEERROR		= 2,
	AX_CLOSE_PROTOCOLERROR		= 3,
	AX_CLOSE_TIMEOUTS		= 4,
	AX_CLOSE_SHUTDOWN		= 5,
	AX_CLOSE_BYMANAGER		= 6
};

struct ax {
	int ax_fd;
	enum ax_byte_order ax_byteorder;
	uint8_t *ax_rbuf;
	size_t ax_rblen;
	size_t ax_rbsize;
	uint8_t *ax_wbuf;
	size_t ax_wblen;
	size_t ax_wbtlen;
	size_t ax_wbsize;
	uint32_t *ax_packetids;
	size_t ax_packetidsize;
};

#ifndef AX_PRIMITIVE
#define AX_PRIMITIVE

#define AX_OID_MAX_LEN 128

struct ax_oid {
	uint8_t aoi_include;
	uint32_t aoi_id[AX_OID_MAX_LEN];
	size_t aoi_idlen;
};

struct ax_ostring {
	unsigned char *aos_string;
	uint32_t aos_slen;
};
#endif

struct ax_searchrange {
	struct ax_oid asr_start;
	struct ax_oid asr_stop;
};

struct ax_pdu_header {
	uint8_t	aph_version;
	uint8_t	aph_type;
	uint8_t	aph_flags;
	uint8_t	aph_reserved;
	uint32_t aph_sessionid;
	uint32_t aph_transactionid;
	uint32_t aph_packetid;
	uint32_t aph_plength;
};

struct ax_varbind {
	enum ax_data_type avb_type;
	struct ax_oid avb_oid;
	union ax_data {
		int32_t avb_int32;
		uint32_t avb_uint32;
		uint64_t avb_uint64;
		struct ax_ostring avb_ostring;
		struct ax_oid avb_oid;
	} avb_data;
};

struct ax_pdu {
	struct ax_pdu_header ap_header;
	struct ax_ostring ap_context;
	union {
		struct ax_pdu_searchrangelist {
			size_t ap_nsr;
			struct ax_searchrange *ap_sr;
		} ap_srl;
		struct ax_pdu_getbulk {
			uint16_t ap_nonrep;
			uint16_t ap_maxrep;
			struct ax_pdu_searchrangelist ap_srl;
		} ap_getbulk;
		struct ax_pdu_varbindlist {
			struct ax_varbind *ap_varbind;
			size_t ap_nvarbind;
		} ap_vbl;
		struct ax_pdu_response {
			uint32_t ap_uptime;
			enum ax_pdu_error ap_error;
			uint16_t ap_index;
			struct ax_varbind *ap_varbindlist;
			size_t ap_nvarbind;
		} ap_response;
		void *ap_raw;
	} ap_payload;
};

struct ax *ax_new(int);
void ax_free(struct ax *);
struct ax_pdu *ax_recv(struct ax *);
ssize_t ax_send(struct ax *);
uint32_t ax_open(struct ax *, uint8_t, struct ax_oid *,
    struct ax_ostring *);
uint32_t ax_close(struct ax *, uint32_t, enum ax_close_reason);
uint32_t ax_indexallocate(struct ax *, uint8_t, uint32_t,
    struct ax_ostring *, struct ax_varbind *, size_t);
uint32_t ax_indexdeallocate(struct ax *, uint32_t,
    struct ax_ostring *, struct ax_varbind *, size_t);
uint32_t ax_addagentcaps(struct ax *, uint32_t, struct ax_ostring *,
    struct ax_oid *, struct ax_ostring *);
uint32_t ax_removeagentcaps(struct ax *, uint32_t,
    struct ax_ostring *, struct ax_oid *);
uint32_t ax_register(struct ax *, uint8_t, uint32_t,
    struct ax_ostring *, uint8_t, uint8_t, uint8_t, struct ax_oid *,
    uint32_t);
uint32_t ax_unregister(struct ax *, uint32_t, struct ax_ostring *,
    uint8_t, uint8_t, struct ax_oid *, uint32_t);
int ax_response(struct ax *, uint32_t, uint32_t, uint32_t,
    uint32_t, uint16_t, uint16_t, struct ax_varbind *, size_t);
void ax_pdu_free(struct ax_pdu *);
void ax_varbind_free(struct ax_varbind *);
const char *ax_error2string(enum ax_pdu_error);
const char *ax_pdutype2string(enum ax_pdu_type);
const char *ax_oid2string(struct ax_oid *);
const char *ax_oidrange2string(struct ax_oid *, uint8_t, uint32_t);
const char *ax_varbind2string(struct ax_varbind *);
const char *ax_closereason2string(enum ax_close_reason);
int ax_oid_cmp(struct ax_oid *, struct ax_oid *);
int ax_oid_add(struct ax_oid *, uint32_t);
