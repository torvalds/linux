/*
 * nf_nat_snmp_basic.c
 *
 * Basic SNMP Application Layer Gateway
 *
 * This IP NAT module is intended for use with SNMP network
 * discovery and monitoring applications where target networks use
 * conflicting private address realms.
 *
 * Static NAT is used to remap the networks from the view of the network
 * management system at the IP layer, and this module remaps some application
 * layer addresses to match.
 *
 * The simplest form of ALG is performed, where only tagged IP addresses
 * are modified.  The module does not need to be MIB aware and only scans
 * messages at the ASN.1/BER level.
 *
 * Currently, only SNMPv1 and SNMPv2 are supported.
 *
 * More information on ALG and associated issues can be found in
 * RFC 2962
 *
 * The ASB.1/BER parsing code is derived from the gxsnmp package by Gregory
 * McLean & Jochen Friedrich, stripped down for use in the kernel.
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
 *
 * Author: James Morris <jmorris@intercode.com.au>
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <net/checksum.h>
#include <net/udp.h>

#include <net/netfilter/nf_nat.h>
#include <net/netfilter/nf_conntrack_expect.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <net/netfilter/nf_nat_helper.h>
#include <linux/netfilter/nf_conntrack_snmp.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("James Morris <jmorris@intercode.com.au>");
MODULE_DESCRIPTION("Basic SNMP Application Layer Gateway");
MODULE_ALIAS("ip_nat_snmp_basic");

#define SNMP_PORT 161
#define SNMP_TRAP_PORT 162
#define NOCT1(n) (*(u8 *)(n))

static int debug;
static DEFINE_SPINLOCK(snmp_lock);

/*
 * Application layer address mapping mimics the NAT mapping, but
 * only for the first octet in this case (a more flexible system
 * can be implemented if needed).
 */
struct oct1_map
{
	u_int8_t from;
	u_int8_t to;
};


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
#define ASN1_EOC	0	/* End Of Contents */
#define ASN1_BOL	1	/* Boolean */
#define ASN1_INT	2	/* Integer */
#define ASN1_BTS	3	/* Bit String */
#define ASN1_OTS	4	/* Octet String */
#define ASN1_NUL	5	/* Null */
#define ASN1_OJI	6	/* Object Identifier  */
#define ASN1_OJD	7	/* Object Description */
#define ASN1_EXT	8	/* External */
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

/*
 * ASN.1 context.
 */
struct asn1_ctx
{
	int error;			/* Error condition */
	unsigned char *pointer;		/* Octet just to be decoded */
	unsigned char *begin;		/* First octet */
	unsigned char *end;		/* Octet after last octet */
};

/*
 * Octet string (not null terminated)
 */
struct asn1_octstr
{
	unsigned char *data;
	unsigned int len;
};

static void asn1_open(struct asn1_ctx *ctx,
		      unsigned char *buf,
		      unsigned int len)
{
	ctx->begin = buf;
	ctx->end = buf + len;
	ctx->pointer = buf;
	ctx->error = ASN1_ERR_NOERROR;
}

static unsigned char asn1_octet_decode(struct asn1_ctx *ctx, unsigned char *ch)
{
	if (ctx->pointer >= ctx->end) {
		ctx->error = ASN1_ERR_DEC_EMPTY;
		return 0;
	}
	*ch = *(ctx->pointer)++;
	return 1;
}

static unsigned char asn1_tag_decode(struct asn1_ctx *ctx, unsigned int *tag)
{
	unsigned char ch;

	*tag = 0;

	do
	{
		if (!asn1_octet_decode(ctx, &ch))
			return 0;
		*tag <<= 7;
		*tag |= ch & 0x7F;
	} while ((ch & 0x80) == 0x80);
	return 1;
}

static unsigned char asn1_id_decode(struct asn1_ctx *ctx,
				    unsigned int *cls,
				    unsigned int *con,
				    unsigned int *tag)
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

static unsigned char asn1_length_decode(struct asn1_ctx *ctx,
					unsigned int *def,
					unsigned int *len)
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
			cnt = ch & 0x7F;
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

static unsigned char asn1_header_decode(struct asn1_ctx *ctx,
					unsigned char **eoc,
					unsigned int *cls,
					unsigned int *con,
					unsigned int *tag)
{
	unsigned int def, len;

	if (!asn1_id_decode(ctx, cls, con, tag))
		return 0;

	def = len = 0;
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

static unsigned char asn1_eoc_decode(struct asn1_ctx *ctx, unsigned char *eoc)
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

static unsigned char asn1_null_decode(struct asn1_ctx *ctx, unsigned char *eoc)
{
	ctx->pointer = eoc;
	return 1;
}

static unsigned char asn1_long_decode(struct asn1_ctx *ctx,
				      unsigned char *eoc,
				      long *integer)
{
	unsigned char ch;
	unsigned int  len;

	if (!asn1_octet_decode(ctx, &ch))
		return 0;

	*integer = (signed char) ch;
	len = 1;

	while (ctx->pointer < eoc) {
		if (++len > sizeof (long)) {
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
	unsigned int  len;

	if (!asn1_octet_decode(ctx, &ch))
		return 0;

	*integer = ch;
	if (ch == 0) len = 0;
	else len = 1;

	while (ctx->pointer < eoc) {
		if (++len > sizeof (unsigned int)) {
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
	unsigned int  len;

	if (!asn1_octet_decode(ctx, &ch))
		return 0;

	*integer = ch;
	if (ch == 0) len = 0;
	else len = 1;

	while (ctx->pointer < eoc) {
		if (++len > sizeof (unsigned long)) {
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

static unsigned char asn1_octets_decode(struct asn1_ctx *ctx,
					unsigned char *eoc,
					unsigned char **octets,
					unsigned int *len)
{
	unsigned char *ptr;

	*len = 0;

	*octets = kmalloc(eoc - ctx->pointer, GFP_ATOMIC);
	if (*octets == NULL)
		return 0;

	ptr = *octets;
	while (ctx->pointer < eoc) {
		if (!asn1_octet_decode(ctx, (unsigned char *)ptr++)) {
			kfree(*octets);
			*octets = NULL;
			return 0;
		}
		(*len)++;
	}
	return 1;
}

static unsigned char asn1_subid_decode(struct asn1_ctx *ctx,
				       unsigned long *subid)
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

static unsigned char asn1_oid_decode(struct asn1_ctx *ctx,
				     unsigned char *eoc,
				     unsigned long **oid,
				     unsigned int *len)
{
	unsigned long subid;
	unsigned long *optr;
	size_t size;

	size = eoc - ctx->pointer + 1;

	/* first subid actually encodes first two subids */
	if (size < 2 || size > ULONG_MAX/sizeof(unsigned long))
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
		optr [0] = 0;
		optr [1] = subid;
	} else if (subid < 80) {
		optr [0] = 1;
		optr [1] = subid - 40;
	} else {
		optr [0] = 2;
		optr [1] = subid - 80;
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

/*****************************************************************************
 *
 * SNMP decoding routines (gxsnmp author Dirk Wisse)
 *
 *****************************************************************************/

/* SNMP Versions */
#define SNMP_V1				0
#define SNMP_V2C			1
#define SNMP_V2				2
#define SNMP_V3				3

/* Default Sizes */
#define SNMP_SIZE_COMM			256
#define SNMP_SIZE_OBJECTID		128
#define SNMP_SIZE_BUFCHR		256
#define SNMP_SIZE_BUFINT		128
#define SNMP_SIZE_SMALLOBJECTID		16

/* Requests */
#define SNMP_PDU_GET			0
#define SNMP_PDU_NEXT			1
#define SNMP_PDU_RESPONSE		2
#define SNMP_PDU_SET			3
#define SNMP_PDU_TRAP1			4
#define SNMP_PDU_BULK			5
#define SNMP_PDU_INFORM			6
#define SNMP_PDU_TRAP2			7

/* Errors */
#define SNMP_NOERROR			0
#define SNMP_TOOBIG			1
#define SNMP_NOSUCHNAME			2
#define SNMP_BADVALUE			3
#define SNMP_READONLY			4
#define SNMP_GENERROR			5
#define SNMP_NOACCESS			6
#define SNMP_WRONGTYPE			7
#define SNMP_WRONGLENGTH		8
#define SNMP_WRONGENCODING		9
#define SNMP_WRONGVALUE			10
#define SNMP_NOCREATION			11
#define SNMP_INCONSISTENTVALUE		12
#define SNMP_RESOURCEUNAVAILABLE	13
#define SNMP_COMMITFAILED		14
#define SNMP_UNDOFAILED			15
#define SNMP_AUTHORIZATIONERROR		16
#define SNMP_NOTWRITABLE		17
#define SNMP_INCONSISTENTNAME		18

/* General SNMP V1 Traps */
#define SNMP_TRAP_COLDSTART		0
#define SNMP_TRAP_WARMSTART		1
#define SNMP_TRAP_LINKDOWN		2
#define SNMP_TRAP_LINKUP		3
#define SNMP_TRAP_AUTFAILURE		4
#define SNMP_TRAP_EQPNEIGHBORLOSS	5
#define SNMP_TRAP_ENTSPECIFIC		6

/* SNMPv1 Types */
#define SNMP_NULL                0
#define SNMP_INTEGER             1    /* l  */
#define SNMP_OCTETSTR            2    /* c  */
#define SNMP_DISPLAYSTR          2    /* c  */
#define SNMP_OBJECTID            3    /* ul */
#define SNMP_IPADDR              4    /* uc */
#define SNMP_COUNTER             5    /* ul */
#define SNMP_GAUGE               6    /* ul */
#define SNMP_TIMETICKS           7    /* ul */
#define SNMP_OPAQUE              8    /* c  */

/* Additional SNMPv2 Types */
#define SNMP_UINTEGER            5    /* ul */
#define SNMP_BITSTR              9    /* uc */
#define SNMP_NSAP               10    /* uc */
#define SNMP_COUNTER64          11    /* ul */
#define SNMP_NOSUCHOBJECT       12
#define SNMP_NOSUCHINSTANCE     13
#define SNMP_ENDOFMIBVIEW       14

union snmp_syntax
{
	unsigned char uc[0];	/* 8 bit unsigned */
	char c[0];		/* 8 bit signed */
	unsigned long ul[0];	/* 32 bit unsigned */
	long l[0];		/* 32 bit signed */
};

struct snmp_object
{
	unsigned long *id;
	unsigned int id_len;
	unsigned short type;
	unsigned int syntax_len;
	union snmp_syntax syntax;
};

struct snmp_request
{
	unsigned long id;
	unsigned int error_status;
	unsigned int error_index;
};

struct snmp_v1_trap
{
	unsigned long *id;
	unsigned int id_len;
	unsigned long ip_address;	/* pointer  */
	unsigned int general;
	unsigned int specific;
	unsigned long time;
};

/* SNMP types */
#define SNMP_IPA    0
#define SNMP_CNT    1
#define SNMP_GGE    2
#define SNMP_TIT    3
#define SNMP_OPQ    4
#define SNMP_C64    6

/* SNMP errors */
#define SERR_NSO    0
#define SERR_NSI    1
#define SERR_EOM    2

static inline void mangle_address(unsigned char *begin,
				  unsigned char *addr,
				  const struct oct1_map *map,
				  __sum16 *check);
struct snmp_cnv
{
	unsigned int class;
	unsigned int tag;
	int syntax;
};

static const struct snmp_cnv snmp_conv[] = {
	{ASN1_UNI, ASN1_NUL, SNMP_NULL},
	{ASN1_UNI, ASN1_INT, SNMP_INTEGER},
	{ASN1_UNI, ASN1_OTS, SNMP_OCTETSTR},
	{ASN1_UNI, ASN1_OTS, SNMP_DISPLAYSTR},
	{ASN1_UNI, ASN1_OJI, SNMP_OBJECTID},
	{ASN1_APL, SNMP_IPA, SNMP_IPADDR},
	{ASN1_APL, SNMP_CNT, SNMP_COUNTER},	/* Counter32 */
	{ASN1_APL, SNMP_GGE, SNMP_GAUGE},	/* Gauge32 == Unsigned32  */
	{ASN1_APL, SNMP_TIT, SNMP_TIMETICKS},
	{ASN1_APL, SNMP_OPQ, SNMP_OPAQUE},

	/* SNMPv2 data types and errors */
	{ASN1_UNI, ASN1_BTS, SNMP_BITSTR},
	{ASN1_APL, SNMP_C64, SNMP_COUNTER64},
	{ASN1_CTX, SERR_NSO, SNMP_NOSUCHOBJECT},
	{ASN1_CTX, SERR_NSI, SNMP_NOSUCHINSTANCE},
	{ASN1_CTX, SERR_EOM, SNMP_ENDOFMIBVIEW},
	{0,       0,       -1}
};

static unsigned char snmp_tag_cls2syntax(unsigned int tag,
					 unsigned int cls,
					 unsigned short *syntax)
{
	const struct snmp_cnv *cnv;

	cnv = snmp_conv;

	while (cnv->syntax != -1) {
		if (cnv->tag == tag && cnv->class == cls) {
			*syntax = cnv->syntax;
			return 1;
		}
		cnv++;
	}
	return 0;
}

static unsigned char snmp_object_decode(struct asn1_ctx *ctx,
					struct snmp_object **obj)
{
	unsigned int cls, con, tag, len, idlen;
	unsigned short type;
	unsigned char *eoc, *end, *p;
	unsigned long *lp, *id;
	unsigned long ul;
	long l;

	*obj = NULL;
	id = NULL;

	if (!asn1_header_decode(ctx, &eoc, &cls, &con, &tag))
		return 0;

	if (cls != ASN1_UNI || con != ASN1_CON || tag != ASN1_SEQ)
		return 0;

	if (!asn1_header_decode(ctx, &end, &cls, &con, &tag))
		return 0;

	if (cls != ASN1_UNI || con != ASN1_PRI || tag != ASN1_OJI)
		return 0;

	if (!asn1_oid_decode(ctx, end, &id, &idlen))
		return 0;

	if (!asn1_header_decode(ctx, &end, &cls, &con, &tag)) {
		kfree(id);
		return 0;
	}

	if (con != ASN1_PRI) {
		kfree(id);
		return 0;
	}

	type = 0;
	if (!snmp_tag_cls2syntax(tag, cls, &type)) {
		kfree(id);
		return 0;
	}

	l = 0;
	switch (type) {
	case SNMP_INTEGER:
		len = sizeof(long);
		if (!asn1_long_decode(ctx, end, &l)) {
			kfree(id);
			return 0;
		}
		*obj = kmalloc(sizeof(struct snmp_object) + len, GFP_ATOMIC);
		if (*obj == NULL) {
			kfree(id);
			return 0;
		}
		(*obj)->syntax.l[0] = l;
		break;
	case SNMP_OCTETSTR:
	case SNMP_OPAQUE:
		if (!asn1_octets_decode(ctx, end, &p, &len)) {
			kfree(id);
			return 0;
		}
		*obj = kmalloc(sizeof(struct snmp_object) + len, GFP_ATOMIC);
		if (*obj == NULL) {
			kfree(p);
			kfree(id);
			return 0;
		}
		memcpy((*obj)->syntax.c, p, len);
		kfree(p);
		break;
	case SNMP_NULL:
	case SNMP_NOSUCHOBJECT:
	case SNMP_NOSUCHINSTANCE:
	case SNMP_ENDOFMIBVIEW:
		len = 0;
		*obj = kmalloc(sizeof(struct snmp_object), GFP_ATOMIC);
		if (*obj == NULL) {
			kfree(id);
			return 0;
		}
		if (!asn1_null_decode(ctx, end)) {
			kfree(id);
			kfree(*obj);
			*obj = NULL;
			return 0;
		}
		break;
	case SNMP_OBJECTID:
		if (!asn1_oid_decode(ctx, end, (unsigned long **)&lp, &len)) {
			kfree(id);
			return 0;
		}
		len *= sizeof(unsigned long);
		*obj = kmalloc(sizeof(struct snmp_object) + len, GFP_ATOMIC);
		if (*obj == NULL) {
			kfree(lp);
			kfree(id);
			return 0;
		}
		memcpy((*obj)->syntax.ul, lp, len);
		kfree(lp);
		break;
	case SNMP_IPADDR:
		if (!asn1_octets_decode(ctx, end, &p, &len)) {
			kfree(id);
			return 0;
		}
		if (len != 4) {
			kfree(p);
			kfree(id);
			return 0;
		}
		*obj = kmalloc(sizeof(struct snmp_object) + len, GFP_ATOMIC);
		if (*obj == NULL) {
			kfree(p);
			kfree(id);
			return 0;
		}
		memcpy((*obj)->syntax.uc, p, len);
		kfree(p);
		break;
	case SNMP_COUNTER:
	case SNMP_GAUGE:
	case SNMP_TIMETICKS:
		len = sizeof(unsigned long);
		if (!asn1_ulong_decode(ctx, end, &ul)) {
			kfree(id);
			return 0;
		}
		*obj = kmalloc(sizeof(struct snmp_object) + len, GFP_ATOMIC);
		if (*obj == NULL) {
			kfree(id);
			return 0;
		}
		(*obj)->syntax.ul[0] = ul;
		break;
	default:
		kfree(id);
		return 0;
	}

	(*obj)->syntax_len = len;
	(*obj)->type = type;
	(*obj)->id = id;
	(*obj)->id_len = idlen;

	if (!asn1_eoc_decode(ctx, eoc)) {
		kfree(id);
		kfree(*obj);
		*obj = NULL;
		return 0;
	}
	return 1;
}

static unsigned char snmp_request_decode(struct asn1_ctx *ctx,
					 struct snmp_request *request)
{
	unsigned int cls, con, tag;
	unsigned char *end;

	if (!asn1_header_decode(ctx, &end, &cls, &con, &tag))
		return 0;

	if (cls != ASN1_UNI || con != ASN1_PRI || tag != ASN1_INT)
		return 0;

	if (!asn1_ulong_decode(ctx, end, &request->id))
		return 0;

	if (!asn1_header_decode(ctx, &end, &cls, &con, &tag))
		return 0;

	if (cls != ASN1_UNI || con != ASN1_PRI || tag != ASN1_INT)
		return 0;

	if (!asn1_uint_decode(ctx, end, &request->error_status))
		return 0;

	if (!asn1_header_decode(ctx, &end, &cls, &con, &tag))
		return 0;

	if (cls != ASN1_UNI || con != ASN1_PRI || tag != ASN1_INT)
		return 0;

	if (!asn1_uint_decode(ctx, end, &request->error_index))
		return 0;

	return 1;
}

/*
 * Fast checksum update for possibly oddly-aligned UDP byte, from the
 * code example in the draft.
 */
static void fast_csum(__sum16 *csum,
		      const unsigned char *optr,
		      const unsigned char *nptr,
		      int offset)
{
	unsigned char s[4];

	if (offset & 1) {
		s[0] = ~0;
		s[1] = ~*optr;
		s[2] = 0;
		s[3] = *nptr;
	} else {
		s[0] = ~*optr;
		s[1] = ~0;
		s[2] = *nptr;
		s[3] = 0;
	}

	*csum = csum_fold(csum_partial(s, 4, ~csum_unfold(*csum)));
}

/*
 * Mangle IP address.
 * 	- begin points to the start of the snmp messgae
 *      - addr points to the start of the address
 */
static inline void mangle_address(unsigned char *begin,
				  unsigned char *addr,
				  const struct oct1_map *map,
				  __sum16 *check)
{
	if (map->from == NOCT1(addr)) {
		u_int32_t old;

		if (debug)
			memcpy(&old, addr, sizeof(old));

		*addr = map->to;

		/* Update UDP checksum if being used */
		if (*check) {
			fast_csum(check,
				  &map->from, &map->to, addr - begin);

		}

		if (debug)
			printk(KERN_DEBUG "bsalg: mapped %pI4 to %pI4\n",
			       &old, addr);
	}
}

static unsigned char snmp_trap_decode(struct asn1_ctx *ctx,
				      struct snmp_v1_trap *trap,
				      const struct oct1_map *map,
				      __sum16 *check)
{
	unsigned int cls, con, tag, len;
	unsigned char *end;

	if (!asn1_header_decode(ctx, &end, &cls, &con, &tag))
		return 0;

	if (cls != ASN1_UNI || con != ASN1_PRI || tag != ASN1_OJI)
		return 0;

	if (!asn1_oid_decode(ctx, end, &trap->id, &trap->id_len))
		return 0;

	if (!asn1_header_decode(ctx, &end, &cls, &con, &tag))
		goto err_id_free;

	if (!((cls == ASN1_APL && con == ASN1_PRI && tag == SNMP_IPA) ||
	      (cls == ASN1_UNI && con == ASN1_PRI && tag == ASN1_OTS)))
		goto err_id_free;

	if (!asn1_octets_decode(ctx, end, (unsigned char **)&trap->ip_address, &len))
		goto err_id_free;

	/* IPv4 only */
	if (len != 4)
		goto err_addr_free;

	mangle_address(ctx->begin, ctx->pointer - 4, map, check);

	if (!asn1_header_decode(ctx, &end, &cls, &con, &tag))
		goto err_addr_free;

	if (cls != ASN1_UNI || con != ASN1_PRI || tag != ASN1_INT)
		goto err_addr_free;

	if (!asn1_uint_decode(ctx, end, &trap->general))
		goto err_addr_free;

	if (!asn1_header_decode(ctx, &end, &cls, &con, &tag))
		goto err_addr_free;

	if (cls != ASN1_UNI || con != ASN1_PRI || tag != ASN1_INT)
		goto err_addr_free;

	if (!asn1_uint_decode(ctx, end, &trap->specific))
		goto err_addr_free;

	if (!asn1_header_decode(ctx, &end, &cls, &con, &tag))
		goto err_addr_free;

	if (!((cls == ASN1_APL && con == ASN1_PRI && tag == SNMP_TIT) ||
	      (cls == ASN1_UNI && con == ASN1_PRI && tag == ASN1_INT)))
		goto err_addr_free;

	if (!asn1_ulong_decode(ctx, end, &trap->time))
		goto err_addr_free;

	return 1;

err_addr_free:
	kfree((unsigned long *)trap->ip_address);

err_id_free:
	kfree(trap->id);

	return 0;
}

/*****************************************************************************
 *
 * Misc. routines
 *
 *****************************************************************************/

static void hex_dump(const unsigned char *buf, size_t len)
{
	size_t i;

	for (i = 0; i < len; i++) {
		if (i && !(i % 16))
			printk("\n");
		printk("%02x ", *(buf + i));
	}
	printk("\n");
}

/*
 * Parse and mangle SNMP message according to mapping.
 * (And this is the fucking 'basic' method).
 */
static int snmp_parse_mangle(unsigned char *msg,
			     u_int16_t len,
			     const struct oct1_map *map,
			     __sum16 *check)
{
	unsigned char *eoc, *end;
	unsigned int cls, con, tag, vers, pdutype;
	struct asn1_ctx ctx;
	struct asn1_octstr comm;
	struct snmp_object *obj;

	if (debug > 1)
		hex_dump(msg, len);

	asn1_open(&ctx, msg, len);

	/*
	 * Start of SNMP message.
	 */
	if (!asn1_header_decode(&ctx, &eoc, &cls, &con, &tag))
		return 0;
	if (cls != ASN1_UNI || con != ASN1_CON || tag != ASN1_SEQ)
		return 0;

	/*
	 * Version 1 or 2 handled.
	 */
	if (!asn1_header_decode(&ctx, &end, &cls, &con, &tag))
		return 0;
	if (cls != ASN1_UNI || con != ASN1_PRI || tag != ASN1_INT)
		return 0;
	if (!asn1_uint_decode (&ctx, end, &vers))
		return 0;
	if (debug > 1)
		printk(KERN_DEBUG "bsalg: snmp version: %u\n", vers + 1);
	if (vers > 1)
		return 1;

	/*
	 * Community.
	 */
	if (!asn1_header_decode (&ctx, &end, &cls, &con, &tag))
		return 0;
	if (cls != ASN1_UNI || con != ASN1_PRI || tag != ASN1_OTS)
		return 0;
	if (!asn1_octets_decode(&ctx, end, &comm.data, &comm.len))
		return 0;
	if (debug > 1) {
		unsigned int i;

		printk(KERN_DEBUG "bsalg: community: ");
		for (i = 0; i < comm.len; i++)
			printk("%c", comm.data[i]);
		printk("\n");
	}
	kfree(comm.data);

	/*
	 * PDU type
	 */
	if (!asn1_header_decode(&ctx, &eoc, &cls, &con, &pdutype))
		return 0;
	if (cls != ASN1_CTX || con != ASN1_CON)
		return 0;
	if (debug > 1) {
		static const unsigned char *const pdus[] = {
			[SNMP_PDU_GET] = "get",
			[SNMP_PDU_NEXT] = "get-next",
			[SNMP_PDU_RESPONSE] = "response",
			[SNMP_PDU_SET] = "set",
			[SNMP_PDU_TRAP1] = "trapv1",
			[SNMP_PDU_BULK] = "bulk",
			[SNMP_PDU_INFORM] = "inform",
			[SNMP_PDU_TRAP2] = "trapv2"
		};

		if (pdutype > SNMP_PDU_TRAP2)
			printk(KERN_DEBUG "bsalg: bad pdu type %u\n", pdutype);
		else
			printk(KERN_DEBUG "bsalg: pdu: %s\n", pdus[pdutype]);
	}
	if (pdutype != SNMP_PDU_RESPONSE &&
	    pdutype != SNMP_PDU_TRAP1 && pdutype != SNMP_PDU_TRAP2)
		return 1;

	/*
	 * Request header or v1 trap
	 */
	if (pdutype == SNMP_PDU_TRAP1) {
		struct snmp_v1_trap trap;
		unsigned char ret = snmp_trap_decode(&ctx, &trap, map, check);

		if (ret) {
			kfree(trap.id);
			kfree((unsigned long *)trap.ip_address);
		} else
			return ret;

	} else {
		struct snmp_request req;

		if (!snmp_request_decode(&ctx, &req))
			return 0;

		if (debug > 1)
			printk(KERN_DEBUG "bsalg: request: id=0x%lx error_status=%u "
			"error_index=%u\n", req.id, req.error_status,
			req.error_index);
	}

	/*
	 * Loop through objects, look for IP addresses to mangle.
	 */
	if (!asn1_header_decode(&ctx, &eoc, &cls, &con, &tag))
		return 0;

	if (cls != ASN1_UNI || con != ASN1_CON || tag != ASN1_SEQ)
		return 0;

	while (!asn1_eoc_decode(&ctx, eoc)) {
		unsigned int i;

		if (!snmp_object_decode(&ctx, &obj)) {
			if (obj) {
				kfree(obj->id);
				kfree(obj);
			}
			return 0;
		}

		if (debug > 1) {
			printk(KERN_DEBUG "bsalg: object: ");
			for (i = 0; i < obj->id_len; i++) {
				if (i > 0)
					printk(".");
				printk("%lu", obj->id[i]);
			}
			printk(": type=%u\n", obj->type);

		}

		if (obj->type == SNMP_IPADDR)
			mangle_address(ctx.begin, ctx.pointer - 4 , map, check);

		kfree(obj->id);
		kfree(obj);
	}

	if (!asn1_eoc_decode(&ctx, eoc))
		return 0;

	return 1;
}

/*****************************************************************************
 *
 * NAT routines.
 *
 *****************************************************************************/

/*
 * SNMP translation routine.
 */
static int snmp_translate(struct nf_conn *ct,
			  enum ip_conntrack_info ctinfo,
			  struct sk_buff *skb)
{
	struct iphdr *iph = ip_hdr(skb);
	struct udphdr *udph = (struct udphdr *)((__be32 *)iph + iph->ihl);
	u_int16_t udplen = ntohs(udph->len);
	u_int16_t paylen = udplen - sizeof(struct udphdr);
	int dir = CTINFO2DIR(ctinfo);
	struct oct1_map map;

	/*
	 * Determine mappping for application layer addresses based
	 * on NAT manipulations for the packet.
	 */
	if (dir == IP_CT_DIR_ORIGINAL) {
		/* SNAT traps */
		map.from = NOCT1(&ct->tuplehash[dir].tuple.src.u3.ip);
		map.to = NOCT1(&ct->tuplehash[!dir].tuple.dst.u3.ip);
	} else {
		/* DNAT replies */
		map.from = NOCT1(&ct->tuplehash[dir].tuple.src.u3.ip);
		map.to = NOCT1(&ct->tuplehash[!dir].tuple.dst.u3.ip);
	}

	if (map.from == map.to)
		return NF_ACCEPT;

	if (!snmp_parse_mangle((unsigned char *)udph + sizeof(struct udphdr),
			       paylen, &map, &udph->check)) {
		if (net_ratelimit())
			printk(KERN_WARNING "bsalg: parser failed\n");
		return NF_DROP;
	}
	return NF_ACCEPT;
}

/* We don't actually set up expectations, just adjust internal IP
 * addresses if this is being NATted */
static int help(struct sk_buff *skb, unsigned int protoff,
		struct nf_conn *ct,
		enum ip_conntrack_info ctinfo)
{
	int dir = CTINFO2DIR(ctinfo);
	unsigned int ret;
	const struct iphdr *iph = ip_hdr(skb);
	const struct udphdr *udph = (struct udphdr *)((__be32 *)iph + iph->ihl);

	/* SNMP replies and originating SNMP traps get mangled */
	if (udph->source == htons(SNMP_PORT) && dir != IP_CT_DIR_REPLY)
		return NF_ACCEPT;
	if (udph->dest == htons(SNMP_TRAP_PORT) && dir != IP_CT_DIR_ORIGINAL)
		return NF_ACCEPT;

	/* No NAT? */
	if (!(ct->status & IPS_NAT_MASK))
		return NF_ACCEPT;

	/*
	 * Make sure the packet length is ok.  So far, we were only guaranteed
	 * to have a valid length IP header plus 8 bytes, which means we have
	 * enough room for a UDP header.  Just verify the UDP length field so we
	 * can mess around with the payload.
	 */
	if (ntohs(udph->len) != skb->len - (iph->ihl << 2)) {
		 if (net_ratelimit())
			 printk(KERN_WARNING "SNMP: dropping malformed packet src=%pI4 dst=%pI4\n",
				&iph->saddr, &iph->daddr);
		 return NF_DROP;
	}

	if (!skb_make_writable(skb, skb->len))
		return NF_DROP;

	spin_lock_bh(&snmp_lock);
	ret = snmp_translate(ct, ctinfo, skb);
	spin_unlock_bh(&snmp_lock);
	return ret;
}

static const struct nf_conntrack_expect_policy snmp_exp_policy = {
	.max_expected	= 0,
	.timeout	= 180,
};

static struct nf_conntrack_helper snmp_helper __read_mostly = {
	.me			= THIS_MODULE,
	.help			= help,
	.expect_policy		= &snmp_exp_policy,
	.name			= "snmp",
	.tuple.src.l3num	= AF_INET,
	.tuple.src.u.udp.port	= cpu_to_be16(SNMP_PORT),
	.tuple.dst.protonum	= IPPROTO_UDP,
};

static struct nf_conntrack_helper snmp_trap_helper __read_mostly = {
	.me			= THIS_MODULE,
	.help			= help,
	.expect_policy		= &snmp_exp_policy,
	.name			= "snmp_trap",
	.tuple.src.l3num	= AF_INET,
	.tuple.src.u.udp.port	= cpu_to_be16(SNMP_TRAP_PORT),
	.tuple.dst.protonum	= IPPROTO_UDP,
};

/*****************************************************************************
 *
 * Module stuff.
 *
 *****************************************************************************/

static int __init nf_nat_snmp_basic_init(void)
{
	int ret = 0;

	BUG_ON(nf_nat_snmp_hook != NULL);
	RCU_INIT_POINTER(nf_nat_snmp_hook, help);

	ret = nf_conntrack_helper_register(&snmp_trap_helper);
	if (ret < 0) {
		nf_conntrack_helper_unregister(&snmp_helper);
		return ret;
	}
	return ret;
}

static void __exit nf_nat_snmp_basic_fini(void)
{
	RCU_INIT_POINTER(nf_nat_snmp_hook, NULL);
	nf_conntrack_helper_unregister(&snmp_trap_helper);
}

module_init(nf_nat_snmp_basic_init);
module_exit(nf_nat_snmp_basic_fini);

module_param(debug, int, 0600);
