// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * The ASB.1/BER parsing code is derived from ip_nat_snmp_basic.c which was in
 * turn derived from the gxsnmp package by Gregory McLean & Jochen Friedrich
 *
 * Copyright (c) 2000 RP Internet (www.rpi.net.au).
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/slab.h>

#include "glob.h"

#include "asn1.h"
#include "connection.h"
#include "auth.h"

/*****************************************************************************
 *
 * Basic ASN.1 decoding routines (gxsnmp author Dirk Wisse)
 *
 *****************************************************************************/

/* Class */
#define ASN1_UNI	0	/* Universal */
#define ASN1_APL	1	/* Application */
#define ASN1_CTX	2	/* Context */
#define ASN1_PRV	3	/* Private */

/* Tag */
#define ASN1_EOC	0	/* End Of Contents or N/A */
#define ASN1_BOL	1	/* Boolean */
#define ASN1_INT	2	/* Integer */
#define ASN1_BTS	3	/* Bit String */
#define ASN1_OTS	4	/* Octet String */
#define ASN1_NUL	5	/* Null */
#define ASN1_OJI	6	/* Object Identifier  */
#define ASN1_OJD	7	/* Object Description */
#define ASN1_EXT	8	/* External */
#define ASN1_ENUM	10	/* Enumerated */
#define ASN1_SEQ	16	/* Sequence */
#define ASN1_SET	17	/* Set */
#define ASN1_NUMSTR	18	/* Numerical String */
#define ASN1_PRNSTR	19	/* Printable String */
#define ASN1_TEXSTR	20	/* Teletext String */
#define ASN1_VIDSTR	21	/* Video String */
#define ASN1_IA5STR	22	/* IA5 String */
#define ASN1_UNITIM	23	/* Universal Time */
#define ASN1_GENTIM	24	/* General Time */
#define ASN1_GRASTR	25	/* Graphical String */
#define ASN1_VISSTR	26	/* Visible String */
#define ASN1_GENSTR	27	/* General String */

/* Primitive / Constructed methods*/
#define ASN1_PRI	0	/* Primitive */
#define ASN1_CON	1	/* Constructed */

/*
 * Error codes.
 */
#define ASN1_ERR_NOERROR		0
#define ASN1_ERR_DEC_EMPTY		2
#define ASN1_ERR_DEC_EOC_MISMATCH	3
#define ASN1_ERR_DEC_LENGTH_MISMATCH	4
#define ASN1_ERR_DEC_BADVALUE		5

#define SPNEGO_OID_LEN 7
#define NTLMSSP_OID_LEN  10
#define KRB5_OID_LEN  7
#define KRB5U2U_OID_LEN  8
#define MSKRB5_OID_LEN  7
static unsigned long SPNEGO_OID[7] = { 1, 3, 6, 1, 5, 5, 2 };
static unsigned long NTLMSSP_OID[10] = { 1, 3, 6, 1, 4, 1, 311, 2, 2, 10 };
static unsigned long KRB5_OID[7] = { 1, 2, 840, 113554, 1, 2, 2 };
static unsigned long KRB5U2U_OID[8] = { 1, 2, 840, 113554, 1, 2, 2, 3 };
static unsigned long MSKRB5_OID[7] = { 1, 2, 840, 48018, 1, 2, 2 };

static char NTLMSSP_OID_STR[NTLMSSP_OID_LEN] = { 0x2b, 0x06, 0x01, 0x04, 0x01,
	0x82, 0x37, 0x02, 0x02, 0x0a };

/*
 * ASN.1 context.
 */
struct asn1_ctx {
	int error;		/* Error condition */
	unsigned char *pointer;	/* Octet just to be decoded */
	unsigned char *begin;	/* First octet */
	unsigned char *end;	/* Octet after last octet */
};

/*
 * Octet string (not null terminated)
 */
struct asn1_octstr {
	unsigned char *data;
	unsigned int len;
};

static void
asn1_open(struct asn1_ctx *ctx, unsigned char *buf, unsigned int len)
{
	ctx->begin = buf;
	ctx->end = buf + len;
	ctx->pointer = buf;
	ctx->error = ASN1_ERR_NOERROR;
}

static unsigned char
asn1_octet_decode(struct asn1_ctx *ctx, unsigned char *ch)
{
	if (ctx->pointer >= ctx->end) {
		ctx->error = ASN1_ERR_DEC_EMPTY;
		return 0;
	}
	*ch = *(ctx->pointer)++;
	return 1;
}

static unsigned char
asn1_tag_decode(struct asn1_ctx *ctx, unsigned int *tag)
{
	unsigned char ch;

	*tag = 0;

	do {
		if (!asn1_octet_decode(ctx, &ch))
			return 0;
		*tag <<= 7;
		*tag |= ch & 0x7F;
	} while ((ch & 0x80) == 0x80);
	return 1;
}

static unsigned char
asn1_id_decode(struct asn1_ctx *ctx,
	       unsigned int *cls, unsigned int *con, unsigned int *tag)
{
	unsigned char ch;

	if (!asn1_octet_decode(ctx, &ch))
		return 0;

	*cls = (ch & 0xC0) >> 6;
	*con = (ch & 0x20) >> 5;
	*tag = (ch & 0x1F);

	if (*tag == 0x1F) {
		if (!asn1_tag_decode(ctx, tag))
			return 0;
	}
	return 1;
}

static unsigned char
asn1_length_decode(struct asn1_ctx *ctx, unsigned int *def, unsigned int *len)
{
	unsigned char ch, cnt;

	if (!asn1_octet_decode(ctx, &ch))
		return 0;

	if (ch == 0x80)
		*def = 0;
	else {
		*def = 1;

		if (ch < 0x80)
			*len = ch;
		else {
			cnt = (unsigned char) (ch & 0x7F);
			*len = 0;

			while (cnt > 0) {
				if (!asn1_octet_decode(ctx, &ch))
					return 0;
				*len <<= 8;
				*len |= ch;
				cnt--;
			}
		}
	}

	/* don't trust len bigger than ctx buffer */
	if (*len > ctx->end - ctx->pointer)
		return 0;

	return 1;
}

static unsigned char
asn1_header_decode(struct asn1_ctx *ctx,
		   unsigned char **eoc,
		   unsigned int *cls, unsigned int *con, unsigned int *tag)
{
	unsigned int def = 0;
	unsigned int len = 0;

	if (!asn1_id_decode(ctx, cls, con, tag))
		return 0;

	if (!asn1_length_decode(ctx, &def, &len))
		return 0;

	/* primitive shall be definite, indefinite shall be constructed */
	if (*con == ASN1_PRI && !def)
		return 0;

	if (def)
		*eoc = ctx->pointer + len;
	else
		*eoc = NULL;
	return 1;
}

static unsigned char
asn1_eoc_decode(struct asn1_ctx *ctx, unsigned char *eoc)
{
	unsigned char ch;

	if (!eoc) {
		if (!asn1_octet_decode(ctx, &ch))
			return 0;

		if (ch != 0x00) {
			ctx->error = ASN1_ERR_DEC_EOC_MISMATCH;
			return 0;
		}

		if (!asn1_octet_decode(ctx, &ch))
			return 0;

		if (ch != 0x00) {
			ctx->error = ASN1_ERR_DEC_EOC_MISMATCH;
			return 0;
		}
	} else {
		if (ctx->pointer != eoc) {
			ctx->error = ASN1_ERR_DEC_LENGTH_MISMATCH;
			return 0;
		}
	}
	return 1;
}

static unsigned char
asn1_subid_decode(struct asn1_ctx *ctx, unsigned long *subid)
{
	unsigned char ch;

	*subid = 0;

	do {
		if (!asn1_octet_decode(ctx, &ch))
			return 0;

		*subid <<= 7;
		*subid |= ch & 0x7F;
	} while ((ch & 0x80) == 0x80);
	return 1;
}

static int
asn1_oid_decode(struct asn1_ctx *ctx,
		unsigned char *eoc, unsigned long **oid, unsigned int *len)
{
	unsigned long subid;
	unsigned int size;
	unsigned long *optr;

	size = eoc - ctx->pointer + 1;

	/* first subid actually encodes first two subids */
	if (size < 2 || size > UINT_MAX/sizeof(unsigned long))
		return 0;

	*oid = kmalloc(size * sizeof(unsigned long), GFP_KERNEL);
	if (!*oid)
		return 0;

	optr = *oid;

	if (!asn1_subid_decode(ctx, &subid)) {
		kfree(*oid);
		*oid = NULL;
		return 0;
	}

	if (subid < 40) {
		optr[0] = 0;
		optr[1] = subid;
	} else if (subid < 80) {
		optr[0] = 1;
		optr[1] = subid - 40;
	} else {
		optr[0] = 2;
		optr[1] = subid - 80;
	}

	*len = 2;
	optr += 2;

	while (ctx->pointer < eoc) {
		if (++(*len) > size) {
			ctx->error = ASN1_ERR_DEC_BADVALUE;
			kfree(*oid);
			*oid = NULL;
			return 0;
		}

		if (!asn1_subid_decode(ctx, optr++)) {
			kfree(*oid);
			*oid = NULL;
			return 0;
		}
	}
	return 1;
}

static int
compare_oid(unsigned long *oid1, unsigned int oid1len,
	    unsigned long *oid2, unsigned int oid2len)
{
	unsigned int i;

	if (oid1len != oid2len)
		return 0;

	for (i = 0; i < oid1len; i++) {
		if (oid1[i] != oid2[i])
			return 0;
	}
	return 1;
}

/* BB check for endian conversion issues here */

int
ksmbd_decode_negTokenInit(unsigned char *security_blob, int length,
		    struct ksmbd_conn *conn)
{
	struct asn1_ctx ctx;
	unsigned char *end;
	unsigned char *sequence_end;
	unsigned long *oid = NULL;
	unsigned int cls, con, tag, oidlen, rc, mechTokenlen;
	unsigned int mech_type;

	ksmbd_debug(AUTH, "Received SecBlob: length %d\n", length);

	asn1_open(&ctx, security_blob, length);

	/* GSSAPI header */
	if (asn1_header_decode(&ctx, &end, &cls, &con, &tag) == 0) {
		ksmbd_debug(AUTH, "Error decoding negTokenInit header\n");
		return 0;
	} else if ((cls != ASN1_APL) || (con != ASN1_CON)
		   || (tag != ASN1_EOC)) {
		ksmbd_debug(AUTH, "cls = %d con = %d tag = %d\n", cls, con,
			tag);
		return 0;
	}

	/* Check for SPNEGO OID -- remember to free obj->oid */
	rc = asn1_header_decode(&ctx, &end, &cls, &con, &tag);
	if (rc) {
		if ((tag == ASN1_OJI) && (con == ASN1_PRI) &&
		    (cls == ASN1_UNI)) {
			rc = asn1_oid_decode(&ctx, end, &oid, &oidlen);
			if (rc) {
				rc = compare_oid(oid, oidlen, SPNEGO_OID,
						 SPNEGO_OID_LEN);
				kfree(oid);
			}
		} else
			rc = 0;
	}

	/* SPNEGO OID not present or garbled -- bail out */
	if (!rc) {
		ksmbd_debug(AUTH, "Error decoding negTokenInit header\n");
		return 0;
	}

	/* SPNEGO */
	if (asn1_header_decode(&ctx, &end, &cls, &con, &tag) == 0) {
		ksmbd_debug(AUTH, "Error decoding negTokenInit\n");
		return 0;
	} else if ((cls != ASN1_CTX) || (con != ASN1_CON)
		   || (tag != ASN1_EOC)) {
		ksmbd_debug(AUTH,
			"cls = %d con = %d tag = %d end = %p (%d) exit 0\n",
			cls, con, tag, end, *end);
		return 0;
	}

	/* negTokenInit */
	if (asn1_header_decode(&ctx, &end, &cls, &con, &tag) == 0) {
		ksmbd_debug(AUTH, "Error decoding negTokenInit\n");
		return 0;
	} else if ((cls != ASN1_UNI) || (con != ASN1_CON)
		   || (tag != ASN1_SEQ)) {
		ksmbd_debug(AUTH,
			"cls = %d con = %d tag = %d end = %p (%d) exit 1\n",
			cls, con, tag, end, *end);
		return 0;
	}

	/* sequence */
	if (asn1_header_decode(&ctx, &end, &cls, &con, &tag) == 0) {
		ksmbd_debug(AUTH, "Error decoding 2nd part of negTokenInit\n");
		return 0;
	} else if ((cls != ASN1_CTX) || (con != ASN1_CON)
		   || (tag != ASN1_EOC)) {
		ksmbd_debug(AUTH,
			"cls = %d con = %d tag = %d end = %p (%d) exit 0\n",
			cls, con, tag, end, *end);
		return 0;
	}

	/* sequence of */
	if (asn1_header_decode
	    (&ctx, &sequence_end, &cls, &con, &tag) == 0) {
		ksmbd_debug(AUTH, "Error decoding 2nd part of negTokenInit\n");
		return 0;
	} else if ((cls != ASN1_UNI) || (con != ASN1_CON)
		   || (tag != ASN1_SEQ)) {
		ksmbd_debug(AUTH,
			"cls = %d con = %d tag = %d end = %p (%d) exit 1\n",
			cls, con, tag, end, *end);
		return 0;
	}

	/* list of security mechanisms */
	while (!asn1_eoc_decode(&ctx, sequence_end)) {
		rc = asn1_header_decode(&ctx, &end, &cls, &con, &tag);
		if (!rc) {
			ksmbd_debug(AUTH,
				"Error decoding negTokenInit hdr exit2\n");
			return 0;
		}
		if ((tag == ASN1_OJI) && (con == ASN1_PRI)) {
			if (asn1_oid_decode(&ctx, end, &oid, &oidlen)) {
				if (compare_oid(oid, oidlen, MSKRB5_OID,
						MSKRB5_OID_LEN))
					mech_type = KSMBD_AUTH_MSKRB5;
				else if (compare_oid(oid, oidlen, KRB5U2U_OID,
						     KRB5U2U_OID_LEN))
					mech_type = KSMBD_AUTH_KRB5U2U;
				else if (compare_oid(oid, oidlen, KRB5_OID,
						     KRB5_OID_LEN))
					mech_type = KSMBD_AUTH_KRB5;
				else if (compare_oid(oid, oidlen, NTLMSSP_OID,
						     NTLMSSP_OID_LEN))
					mech_type = KSMBD_AUTH_NTLMSSP;
				else {
					kfree(oid);
					continue;
				}

				conn->auth_mechs |= mech_type;
				if (conn->preferred_auth_mech == 0)
					conn->preferred_auth_mech = mech_type;
				kfree(oid);
			}
		} else {
			ksmbd_debug(AUTH,
				"Should be an oid what is going on?\n");
		}
	}

	/* sequence */
	if (asn1_header_decode(&ctx, &end, &cls, &con, &tag) == 0) {
		ksmbd_debug(AUTH, "Error decoding 2nd part of negTokenInit\n");
		return 0;
	} else if ((cls != ASN1_CTX) || (con != ASN1_CON)
		   || (tag != ASN1_INT)) {
		ksmbd_debug(AUTH,
			"cls = %d con = %d tag = %d end = %p (%d) exit 0\n",
			cls, con, tag, end, *end);
		return 0;
	}

	/* sequence of */
	if (asn1_header_decode(&ctx, &end, &cls, &con, &tag) == 0) {
		ksmbd_debug(AUTH, "Error decoding 2nd part of negTokenInit\n");
		return 0;
	} else if ((cls != ASN1_UNI) || (con != ASN1_PRI)
		   || (tag != ASN1_OTS)) {
		ksmbd_debug(AUTH,
			"cls = %d con = %d tag = %d end = %p (%d) exit 0\n",
			cls, con, tag, end, *end);
		return 0;
	}

	mechTokenlen = ctx.end - ctx.pointer;
	conn->mechToken = kmalloc(mechTokenlen + 1, GFP_KERNEL);
	if (!conn->mechToken) {
		ksmbd_err("memory allocation error\n");
		return 0;
	}

	memcpy(conn->mechToken, ctx.pointer, mechTokenlen);
	conn->mechToken[mechTokenlen] = '\0';

	return 1;
}

int
ksmbd_decode_negTokenTarg(unsigned char *security_blob, int length,
		    struct ksmbd_conn *conn)
{
	struct asn1_ctx ctx;
	unsigned char *end;
	unsigned int cls, con, tag, mechTokenlen;

	ksmbd_debug(AUTH, "Received Auth SecBlob: length %d\n", length);

	asn1_open(&ctx, security_blob, length);

	/* GSSAPI header */
	if (asn1_header_decode(&ctx, &end, &cls, &con, &tag) == 0) {
		ksmbd_debug(AUTH, "Error decoding negTokenInit header\n");
		return 0;
	} else if ((cls != ASN1_CTX) || (con != ASN1_CON)
		   || (tag != ASN1_BOL)) {
		ksmbd_debug(AUTH, "cls = %d con = %d tag = %d\n", cls, con,
			tag);
		return 0;
	}

	/* SPNEGO */
	if (asn1_header_decode(&ctx, &end, &cls, &con, &tag) == 0) {
		ksmbd_debug(AUTH, "Error decoding negTokenInit\n");
		return 0;
	} else if ((cls != ASN1_UNI) || (con != ASN1_CON)
		   || (tag != ASN1_SEQ)) {
		ksmbd_debug(AUTH,
			"cls = %d con = %d tag = %d end = %p (%d) exit 0\n",
			cls, con, tag, end, *end);
		return 0;
	}

	/* negTokenTarg */
	if (asn1_header_decode(&ctx, &end, &cls, &con, &tag) == 0) {
		ksmbd_debug(AUTH, "Error decoding negTokenInit\n");
		return 0;
	} else if ((cls != ASN1_CTX) || (con != ASN1_CON)
		   || (tag != ASN1_INT)) {
		ksmbd_debug(AUTH,
			"cls = %d con = %d tag = %d end = %p (%d) exit 1\n",
			cls, con, tag, end, *end);
		return 0;
	}

	/* negTokenTarg */
	if (asn1_header_decode(&ctx, &end, &cls, &con, &tag) == 0) {
		ksmbd_debug(AUTH, "Error decoding negTokenInit\n");
		return 0;
	} else if ((cls != ASN1_UNI) || (con != ASN1_PRI)
		   || (tag != ASN1_OTS)) {
		ksmbd_debug(AUTH,
			"cls = %d con = %d tag = %d end = %p (%d) exit 1\n",
			cls, con, tag, end, *end);
		return 0;
	}

	mechTokenlen = ctx.end - ctx.pointer;
	conn->mechToken = kmalloc(mechTokenlen + 1, GFP_KERNEL);
	if (!conn->mechToken) {
		ksmbd_err("memory allocation error\n");
		return 0;
	}

	memcpy(conn->mechToken, ctx.pointer, mechTokenlen);
	conn->mechToken[mechTokenlen] = '\0';

	return 1;
}

static int compute_asn_hdr_len_bytes(int len)
{
	if (len > 0xFFFFFF)
		return 4;
	else if (len > 0xFFFF)
		return 3;
	else if (len > 0xFF)
		return 2;
	else if (len > 0x7F)
		return 1;
	else
		return 0;
}

static void encode_asn_tag(char *buf,
			   unsigned int *ofs,
			   char tag,
			   char seq,
			   int length)
{
	int i;
	int index = *ofs;
	char hdr_len = compute_asn_hdr_len_bytes(length);
	int len = length + 2 + hdr_len;

	/* insert tag */
	buf[index++] = tag;

	if (!hdr_len)
		buf[index++] = len;
	else {
		buf[index++] = 0x80 | hdr_len;
		for (i = hdr_len - 1; i >= 0; i--)
			buf[index++] = (len >> (i * 8)) & 0xFF;
	}

	/* insert seq */
	len = len - (index - *ofs);
	buf[index++] = seq;

	if (!hdr_len)
		buf[index++] = len;
	else {
		buf[index++] = 0x80 | hdr_len;
		for (i = hdr_len - 1; i >= 0; i--)
			buf[index++] = (len >> (i * 8)) & 0xFF;
	}

	*ofs += (index - *ofs);
}

int build_spnego_ntlmssp_neg_blob(unsigned char **pbuffer, u16 *buflen,
		char *ntlm_blob, int ntlm_blob_len)
{
	char *buf;
	unsigned int ofs = 0;
	int neg_result_len = 4 + compute_asn_hdr_len_bytes(1) * 2 + 1;
	int oid_len = 4 + compute_asn_hdr_len_bytes(NTLMSSP_OID_LEN) * 2 +
		NTLMSSP_OID_LEN;
	int ntlmssp_len = 4 + compute_asn_hdr_len_bytes(ntlm_blob_len) * 2 +
		ntlm_blob_len;
	int total_len = 4 + compute_asn_hdr_len_bytes(neg_result_len +
			oid_len + ntlmssp_len) * 2 +
			neg_result_len + oid_len + ntlmssp_len;

	buf = kmalloc(total_len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	/* insert main gss header */
	encode_asn_tag(buf, &ofs, 0xa1, 0x30, neg_result_len + oid_len +
			ntlmssp_len);

	/* insert neg result */
	encode_asn_tag(buf, &ofs, 0xa0, 0x0a, 1);
	buf[ofs++] = 1;

	/* insert oid */
	encode_asn_tag(buf, &ofs, 0xa1, 0x06, NTLMSSP_OID_LEN);
	memcpy(buf + ofs, NTLMSSP_OID_STR, NTLMSSP_OID_LEN);
	ofs += NTLMSSP_OID_LEN;

	/* insert response token - ntlmssp blob */
	encode_asn_tag(buf, &ofs, 0xa2, 0x04, ntlm_blob_len);
	memcpy(buf + ofs, ntlm_blob, ntlm_blob_len);
	ofs += ntlm_blob_len;

	*pbuffer = buf;
	*buflen = total_len;
	return 0;
}

int build_spnego_ntlmssp_auth_blob(unsigned char **pbuffer, u16 *buflen,
		int neg_result)
{
	char *buf;
	unsigned int ofs = 0;
	int neg_result_len = 4 + compute_asn_hdr_len_bytes(1) * 2 + 1;
	int total_len = 4 + compute_asn_hdr_len_bytes(neg_result_len) * 2 +
		neg_result_len;

	buf = kmalloc(total_len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	/* insert main gss header */
	encode_asn_tag(buf, &ofs, 0xa1, 0x30, neg_result_len);

	/* insert neg result */
	encode_asn_tag(buf, &ofs, 0xa0, 0x0a, 1);
	if (neg_result)
		buf[ofs++] = 2;
	else
		buf[ofs++] = 0;

	*pbuffer = buf;
	*buflen = total_len;
	return 0;
}
