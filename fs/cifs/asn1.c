/*
 * The ASB.1/BER parsing code is derived from ip_nat_snmp_basic.c which was in
 * turn derived from the gxsnmp package by Gregory McLean & Jochen Friedrich
 *
 * Copyright (c) 2000 RP Internet (www.rpi.net.au).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include "cifspdu.h"
#include "cifsglob.h"
#include "cifs_debug.h"
#include "cifsproto.h"

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

#if 0 /* will be needed later by spnego decoding/encoding of ntlmssp */
static unsigned char
asn1_enum_decode(struct asn1_ctx *ctx, __le32 *val)
{
	unsigned char ch;

	if (ctx->pointer >= ctx->end) {
		ctx->error = ASN1_ERR_DEC_EMPTY;
		return 0;
	}

	ch = *(ctx->pointer)++; /* ch has 0xa, ptr points to length octet */
	if ((ch) == ASN1_ENUM)  /* if ch value is ENUM, 0xa */
		*val = *(++(ctx->pointer)); /* value has enum value */
	else
		return 0;

	ctx->pointer++;
	return 1;
}
#endif

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

	if (eoc == NULL) {
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
		return 1;
	} else {
		if (ctx->pointer != eoc) {
			ctx->error = ASN1_ERR_DEC_LENGTH_MISMATCH;
			return 0;
		}
		return 1;
	}
}

/* static unsigned char asn1_null_decode(struct asn1_ctx *ctx,
				      unsigned char *eoc)
{
	ctx->pointer = eoc;
	return 1;
}

static unsigned char asn1_long_decode(struct asn1_ctx *ctx,
				      unsigned char *eoc, long *integer)
{
	unsigned char ch;
	unsigned int len;

	if (!asn1_octet_decode(ctx, &ch))
		return 0;

	*integer = (signed char) ch;
	len = 1;

	while (ctx->pointer < eoc) {
		if (++len > sizeof(long)) {
			ctx->error = ASN1_ERR_DEC_BADVALUE;
			return 0;
		}

		if (!asn1_octet_decode(ctx, &ch))
			return 0;

		*integer <<= 8;
		*integer |= ch;
	}
	return 1;
}

static unsigned char asn1_uint_decode(struct asn1_ctx *ctx,
				      unsigned char *eoc,
				      unsigned int *integer)
{
	unsigned char ch;
	unsigned int len;

	if (!asn1_octet_decode(ctx, &ch))
		return 0;

	*integer = ch;
	if (ch == 0)
		len = 0;
	else
		len = 1;

	while (ctx->pointer < eoc) {
		if (++len > sizeof(unsigned int)) {
			ctx->error = ASN1_ERR_DEC_BADVALUE;
			return 0;
		}

		if (!asn1_octet_decode(ctx, &ch))
			return 0;

		*integer <<= 8;
		*integer |= ch;
	}
	return 1;
}

static unsigned char asn1_ulong_decode(struct asn1_ctx *ctx,
				       unsigned char *eoc,
				       unsigned long *integer)
{
	unsigned char ch;
	unsigned int len;

	if (!asn1_octet_decode(ctx, &ch))
		return 0;

	*integer = ch;
	if (ch == 0)
		len = 0;
	else
		len = 1;

	while (ctx->pointer < eoc) {
		if (++len > sizeof(unsigned long)) {
			ctx->error = ASN1_ERR_DEC_BADVALUE;
			return 0;
		}

		if (!asn1_octet_decode(ctx, &ch))
			return 0;

		*integer <<= 8;
		*integer |= ch;
	}
	return 1;
}

static unsigned char
asn1_octets_decode(struct asn1_ctx *ctx,
		   unsigned char *eoc,
		   unsigned char **octets, unsigned int *len)
{
	unsigned char *ptr;

	*len = 0;

	*octets = kmalloc(eoc - ctx->pointer, GFP_ATOMIC);
	if (*octets == NULL) {
		return 0;
	}

	ptr = *octets;
	while (ctx->pointer < eoc) {
		if (!asn1_octet_decode(ctx, (unsigned char *) ptr++)) {
			kfree(*octets);
			*octets = NULL;
			return 0;
		}
		(*len)++;
	}
	return 1;
} */

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

	*oid = kmalloc(size * sizeof(unsigned long), GFP_ATOMIC);
	if (*oid == NULL)
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
	else {
		for (i = 0; i < oid1len; i++) {
			if (oid1[i] != oid2[i])
				return 0;
		}
		return 1;
	}
}

	/* BB check for endian conversion issues here */

int
decode_negTokenInit(unsigned char *security_blob, int length,
		    enum securityEnum *secType)
{
	struct asn1_ctx ctx;
	unsigned char *end;
	unsigned char *sequence_end;
	unsigned long *oid = NULL;
	unsigned int cls, con, tag, oidlen, rc;
	bool use_ntlmssp = false;
	bool use_kerberos = false;
	bool use_kerberosu2u = false;
	bool use_mskerberos = false;

	/* cifs_dump_mem(" Received SecBlob ", security_blob, length); */

	asn1_open(&ctx, security_blob, length);

	/* GSSAPI header */
	if (asn1_header_decode(&ctx, &end, &cls, &con, &tag) == 0) {
		cFYI(1, ("Error decoding negTokenInit header"));
		return 0;
	} else if ((cls != ASN1_APL) || (con != ASN1_CON)
		   || (tag != ASN1_EOC)) {
		cFYI(1, ("cls = %d con = %d tag = %d", cls, con, tag));
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
		cFYI(1, ("Error decoding negTokenInit header"));
		return 0;
	}

	/* SPNEGO */
	if (asn1_header_decode(&ctx, &end, &cls, &con, &tag) == 0) {
		cFYI(1, ("Error decoding negTokenInit"));
		return 0;
	} else if ((cls != ASN1_CTX) || (con != ASN1_CON)
		   || (tag != ASN1_EOC)) {
		cFYI(1,
		     ("cls = %d con = %d tag = %d end = %p (%d) exit 0",
		      cls, con, tag, end, *end));
		return 0;
	}

	/* negTokenInit */
	if (asn1_header_decode(&ctx, &end, &cls, &con, &tag) == 0) {
		cFYI(1, ("Error decoding negTokenInit"));
		return 0;
	} else if ((cls != ASN1_UNI) || (con != ASN1_CON)
		   || (tag != ASN1_SEQ)) {
		cFYI(1,
		     ("cls = %d con = %d tag = %d end = %p (%d) exit 1",
		      cls, con, tag, end, *end));
		return 0;
	}

	/* sequence */
	if (asn1_header_decode(&ctx, &end, &cls, &con, &tag) == 0) {
		cFYI(1, ("Error decoding 2nd part of negTokenInit"));
		return 0;
	} else if ((cls != ASN1_CTX) || (con != ASN1_CON)
		   || (tag != ASN1_EOC)) {
		cFYI(1,
		     ("cls = %d con = %d tag = %d end = %p (%d) exit 0",
		      cls, con, tag, end, *end));
		return 0;
	}

	/* sequence of */
	if (asn1_header_decode
	    (&ctx, &sequence_end, &cls, &con, &tag) == 0) {
		cFYI(1, ("Error decoding 2nd part of negTokenInit"));
		return 0;
	} else if ((cls != ASN1_UNI) || (con != ASN1_CON)
		   || (tag != ASN1_SEQ)) {
		cFYI(1,
		     ("cls = %d con = %d tag = %d end = %p (%d) exit 1",
		      cls, con, tag, end, *end));
		return 0;
	}

	/* list of security mechanisms */
	while (!asn1_eoc_decode(&ctx, sequence_end)) {
		rc = asn1_header_decode(&ctx, &end, &cls, &con, &tag);
		if (!rc) {
			cFYI(1,
			     ("Error decoding negTokenInit hdr exit2"));
			return 0;
		}
		if ((tag == ASN1_OJI) && (con == ASN1_PRI)) {
			if (asn1_oid_decode(&ctx, end, &oid, &oidlen)) {

				cFYI(1, ("OID len = %d oid = 0x%lx 0x%lx "
					 "0x%lx 0x%lx", oidlen, *oid,
					 *(oid + 1), *(oid + 2), *(oid + 3)));

				if (compare_oid(oid, oidlen, MSKRB5_OID,
						MSKRB5_OID_LEN) &&
						!use_mskerberos)
					use_mskerberos = true;
				else if (compare_oid(oid, oidlen, KRB5U2U_OID,
						     KRB5U2U_OID_LEN) &&
						     !use_kerberosu2u)
					use_kerberosu2u = true;
				else if (compare_oid(oid, oidlen, KRB5_OID,
						     KRB5_OID_LEN) &&
						     !use_kerberos)
					use_kerberos = true;
				else if (compare_oid(oid, oidlen, NTLMSSP_OID,
						     NTLMSSP_OID_LEN))
					use_ntlmssp = true;

				kfree(oid);
			}
		} else {
			cFYI(1, ("Should be an oid what is going on?"));
		}
	}

	/* mechlistMIC */
	if (asn1_header_decode(&ctx, &end, &cls, &con, &tag) == 0) {
		/* Check if we have reached the end of the blob, but with
		   no mechListMic (e.g. NTLMSSP instead of KRB5) */
		if (ctx.error == ASN1_ERR_DEC_EMPTY)
			goto decode_negtoken_exit;
		cFYI(1, ("Error decoding last part negTokenInit exit3"));
		return 0;
	} else if ((cls != ASN1_CTX) || (con != ASN1_CON)) {
		/* tag = 3 indicating mechListMIC */
		cFYI(1, ("Exit 4 cls = %d con = %d tag = %d end = %p (%d)",
			 cls, con, tag, end, *end));
		return 0;
	}

	/* sequence */
	if (asn1_header_decode(&ctx, &end, &cls, &con, &tag) == 0) {
		cFYI(1, ("Error decoding last part negTokenInit exit5"));
		return 0;
	} else if ((cls != ASN1_UNI) || (con != ASN1_CON)
		   || (tag != ASN1_SEQ)) {
		cFYI(1, ("cls = %d con = %d tag = %d end = %p (%d)",
			cls, con, tag, end, *end));
	}

	/* sequence of */
	if (asn1_header_decode(&ctx, &end, &cls, &con, &tag) == 0) {
		cFYI(1, ("Error decoding last part negTokenInit exit 7"));
		return 0;
	} else if ((cls != ASN1_CTX) || (con != ASN1_CON)) {
		cFYI(1, ("Exit 8 cls = %d con = %d tag = %d end = %p (%d)",
			 cls, con, tag, end, *end));
		return 0;
	}

	/* general string */
	if (asn1_header_decode(&ctx, &end, &cls, &con, &tag) == 0) {
		cFYI(1, ("Error decoding last part negTokenInit exit9"));
		return 0;
	} else if ((cls != ASN1_UNI) || (con != ASN1_PRI)
		   || (tag != ASN1_GENSTR)) {
		cFYI(1, ("Exit10 cls = %d con = %d tag = %d end = %p (%d)",
			 cls, con, tag, end, *end));
		return 0;
	}
	cFYI(1, ("Need to call asn1_octets_decode() function for %s",
		 ctx.pointer));	/* is this UTF-8 or ASCII? */
decode_negtoken_exit:
	if (use_kerberos)
		*secType = Kerberos;
	else if (use_mskerberos)
		*secType = MSKerberos;
	else if (use_ntlmssp)
		*secType = RawNTLMSSP;

	return 1;
}
