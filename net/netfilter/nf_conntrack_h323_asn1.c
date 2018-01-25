/****************************************************************************
 * ip_conntrack_helper_h323_asn1.c - BER and PER decoding library for H.323
 * 			      	     conntrack/NAT module.
 *
 * Copyright (c) 2006 by Jing Min Zhao <zhaojingmin@users.sourceforge.net>
 *
 * This source code is licensed under General Public License version 2.
 *
 * See ip_conntrack_helper_h323_asn1.h for details.
 *
 ****************************************************************************/

#ifdef __KERNEL__
#include <linux/kernel.h>
#else
#include <stdio.h>
#endif
#include <linux/netfilter/nf_conntrack_h323_asn1.h>

/* Trace Flag */
#ifndef H323_TRACE
#define H323_TRACE 0
#endif

#if H323_TRACE
#define TAB_SIZE 4
#define IFTHEN(cond, act) if(cond){act;}
#ifdef __KERNEL__
#define PRINT printk
#else
#define PRINT printf
#endif
#define FNAME(name) name,
#else
#define IFTHEN(cond, act)
#define PRINT(fmt, args...)
#define FNAME(name)
#endif

/* ASN.1 Types */
#define NUL 0
#define BOOL 1
#define OID 2
#define INT 3
#define ENUM 4
#define BITSTR 5
#define NUMSTR 6
#define NUMDGT 6
#define TBCDSTR 6
#define OCTSTR 7
#define PRTSTR 7
#define IA5STR 7
#define GENSTR 7
#define BMPSTR 8
#define SEQ 9
#define SET 9
#define SEQOF 10
#define SETOF 10
#define CHOICE 11

/* Constraint Types */
#define FIXD 0
/* #define BITS 1-8 */
#define BYTE 9
#define WORD 10
#define CONS 11
#define SEMI 12
#define UNCO 13

/* ASN.1 Type Attributes */
#define SKIP 0
#define STOP 1
#define DECODE 2
#define EXT 4
#define OPEN 8
#define OPT 16


/* ASN.1 Field Structure */
typedef struct field_t {
#if H323_TRACE
	char *name;
#endif
	unsigned char type;
	unsigned char sz;
	unsigned char lb;
	unsigned char ub;
	unsigned short attr;
	unsigned short offset;
	const struct field_t *fields;
} field_t;

/* Bit Stream */
struct bitstr {
	unsigned char *buf;
	unsigned char *beg;
	unsigned char *end;
	unsigned char *cur;
	unsigned int bit;
};

/* Tool Functions */
#define INC_BIT(bs) if((++(bs)->bit)>7){(bs)->cur++;(bs)->bit=0;}
#define INC_BITS(bs,b) if(((bs)->bit+=(b))>7){(bs)->cur+=(bs)->bit>>3;(bs)->bit&=7;}
#define BYTE_ALIGN(bs) if((bs)->bit){(bs)->cur++;(bs)->bit=0;}
#define CHECK_BOUND(bs,n) if((bs)->cur+(n)>(bs)->end)return(H323_ERROR_BOUND)
static unsigned int get_len(struct bitstr *bs);
static unsigned int get_bit(struct bitstr *bs);
static unsigned int get_bits(struct bitstr *bs, unsigned int b);
static unsigned int get_bitmap(struct bitstr *bs, unsigned int b);
static unsigned int get_uint(struct bitstr *bs, int b);

/* Decoder Functions */
static int decode_nul(struct bitstr *bs, const struct field_t *f, char *base, int level);
static int decode_bool(struct bitstr *bs, const struct field_t *f, char *base, int level);
static int decode_oid(struct bitstr *bs, const struct field_t *f, char *base, int level);
static int decode_int(struct bitstr *bs, const struct field_t *f, char *base, int level);
static int decode_enum(struct bitstr *bs, const struct field_t *f, char *base, int level);
static int decode_bitstr(struct bitstr *bs, const struct field_t *f, char *base, int level);
static int decode_numstr(struct bitstr *bs, const struct field_t *f, char *base, int level);
static int decode_octstr(struct bitstr *bs, const struct field_t *f, char *base, int level);
static int decode_bmpstr(struct bitstr *bs, const struct field_t *f, char *base, int level);
static int decode_seq(struct bitstr *bs, const struct field_t *f, char *base, int level);
static int decode_seqof(struct bitstr *bs, const struct field_t *f, char *base, int level);
static int decode_choice(struct bitstr *bs, const struct field_t *f, char *base, int level);

/* Decoder Functions Vector */
typedef int (*decoder_t)(struct bitstr *, const struct field_t *, char *, int);
static const decoder_t Decoders[] = {
	decode_nul,
	decode_bool,
	decode_oid,
	decode_int,
	decode_enum,
	decode_bitstr,
	decode_numstr,
	decode_octstr,
	decode_bmpstr,
	decode_seq,
	decode_seqof,
	decode_choice,
};

/****************************************************************************
 * H.323 Types
 ****************************************************************************/
#include "nf_conntrack_h323_types.c"

/****************************************************************************
 * Functions
 ****************************************************************************/
/* Assume bs is aligned && v < 16384 */
static unsigned int get_len(struct bitstr *bs)
{
	unsigned int v;

	v = *bs->cur++;

	if (v & 0x80) {
		v &= 0x3f;
		v <<= 8;
		v += *bs->cur++;
	}

	return v;
}

/****************************************************************************/
static unsigned int get_bit(struct bitstr *bs)
{
	unsigned int b = (*bs->cur) & (0x80 >> bs->bit);

	INC_BIT(bs);

	return b;
}

/****************************************************************************/
/* Assume b <= 8 */
static unsigned int get_bits(struct bitstr *bs, unsigned int b)
{
	unsigned int v, l;

	v = (*bs->cur) & (0xffU >> bs->bit);
	l = b + bs->bit;

	if (l < 8) {
		v >>= 8 - l;
		bs->bit = l;
	} else if (l == 8) {
		bs->cur++;
		bs->bit = 0;
	} else {		/* l > 8 */

		v <<= 8;
		v += *(++bs->cur);
		v >>= 16 - l;
		bs->bit = l - 8;
	}

	return v;
}

/****************************************************************************/
/* Assume b <= 32 */
static unsigned int get_bitmap(struct bitstr *bs, unsigned int b)
{
	unsigned int v, l, shift, bytes;

	if (!b)
		return 0;

	l = bs->bit + b;

	if (l < 8) {
		v = (unsigned int)(*bs->cur) << (bs->bit + 24);
		bs->bit = l;
	} else if (l == 8) {
		v = (unsigned int)(*bs->cur++) << (bs->bit + 24);
		bs->bit = 0;
	} else {
		for (bytes = l >> 3, shift = 24, v = 0; bytes;
		     bytes--, shift -= 8)
			v |= (unsigned int)(*bs->cur++) << shift;

		if (l < 32) {
			v |= (unsigned int)(*bs->cur) << shift;
			v <<= bs->bit;
		} else if (l > 32) {
			v <<= bs->bit;
			v |= (*bs->cur) >> (8 - bs->bit);
		}

		bs->bit = l & 0x7;
	}

	v &= 0xffffffff << (32 - b);

	return v;
}

/****************************************************************************
 * Assume bs is aligned and sizeof(unsigned int) == 4
 ****************************************************************************/
static unsigned int get_uint(struct bitstr *bs, int b)
{
	unsigned int v = 0;

	switch (b) {
	case 4:
		v |= *bs->cur++;
		v <<= 8;
	case 3:
		v |= *bs->cur++;
		v <<= 8;
	case 2:
		v |= *bs->cur++;
		v <<= 8;
	case 1:
		v |= *bs->cur++;
		break;
	}
	return v;
}

/****************************************************************************/
static int decode_nul(struct bitstr *bs, const struct field_t *f,
                      char *base, int level)
{
	PRINT("%*.s%s\n", level * TAB_SIZE, " ", f->name);

	return H323_ERROR_NONE;
}

/****************************************************************************/
static int decode_bool(struct bitstr *bs, const struct field_t *f,
                       char *base, int level)
{
	PRINT("%*.s%s\n", level * TAB_SIZE, " ", f->name);

	INC_BIT(bs);

	CHECK_BOUND(bs, 0);
	return H323_ERROR_NONE;
}

/****************************************************************************/
static int decode_oid(struct bitstr *bs, const struct field_t *f,
                      char *base, int level)
{
	int len;

	PRINT("%*.s%s\n", level * TAB_SIZE, " ", f->name);

	BYTE_ALIGN(bs);
	CHECK_BOUND(bs, 1);
	len = *bs->cur++;
	bs->cur += len;

	CHECK_BOUND(bs, 0);
	return H323_ERROR_NONE;
}

/****************************************************************************/
static int decode_int(struct bitstr *bs, const struct field_t *f,
                      char *base, int level)
{
	unsigned int len;

	PRINT("%*.s%s", level * TAB_SIZE, " ", f->name);

	switch (f->sz) {
	case BYTE:		/* Range == 256 */
		BYTE_ALIGN(bs);
		bs->cur++;
		break;
	case WORD:		/* 257 <= Range <= 64K */
		BYTE_ALIGN(bs);
		bs->cur += 2;
		break;
	case CONS:		/* 64K < Range < 4G */
		len = get_bits(bs, 2) + 1;
		BYTE_ALIGN(bs);
		if (base && (f->attr & DECODE)) {	/* timeToLive */
			unsigned int v = get_uint(bs, len) + f->lb;
			PRINT(" = %u", v);
			*((unsigned int *)(base + f->offset)) = v;
		}
		bs->cur += len;
		break;
	case UNCO:
		BYTE_ALIGN(bs);
		CHECK_BOUND(bs, 2);
		len = get_len(bs);
		bs->cur += len;
		break;
	default:		/* 2 <= Range <= 255 */
		INC_BITS(bs, f->sz);
		break;
	}

	PRINT("\n");

	CHECK_BOUND(bs, 0);
	return H323_ERROR_NONE;
}

/****************************************************************************/
static int decode_enum(struct bitstr *bs, const struct field_t *f,
                       char *base, int level)
{
	PRINT("%*.s%s\n", level * TAB_SIZE, " ", f->name);

	if ((f->attr & EXT) && get_bit(bs)) {
		INC_BITS(bs, 7);
	} else {
		INC_BITS(bs, f->sz);
	}

	CHECK_BOUND(bs, 0);
	return H323_ERROR_NONE;
}

/****************************************************************************/
static int decode_bitstr(struct bitstr *bs, const struct field_t *f,
                         char *base, int level)
{
	unsigned int len;

	PRINT("%*.s%s\n", level * TAB_SIZE, " ", f->name);

	BYTE_ALIGN(bs);
	switch (f->sz) {
	case FIXD:		/* fixed length > 16 */
		len = f->lb;
		break;
	case WORD:		/* 2-byte length */
		CHECK_BOUND(bs, 2);
		len = (*bs->cur++) << 8;
		len += (*bs->cur++) + f->lb;
		break;
	case SEMI:
		CHECK_BOUND(bs, 2);
		len = get_len(bs);
		break;
	default:
		len = 0;
		break;
	}

	bs->cur += len >> 3;
	bs->bit = len & 7;

	CHECK_BOUND(bs, 0);
	return H323_ERROR_NONE;
}

/****************************************************************************/
static int decode_numstr(struct bitstr *bs, const struct field_t *f,
                         char *base, int level)
{
	unsigned int len;

	PRINT("%*.s%s\n", level * TAB_SIZE, " ", f->name);

	/* 2 <= Range <= 255 */
	len = get_bits(bs, f->sz) + f->lb;

	BYTE_ALIGN(bs);
	INC_BITS(bs, (len << 2));

	CHECK_BOUND(bs, 0);
	return H323_ERROR_NONE;
}

/****************************************************************************/
static int decode_octstr(struct bitstr *bs, const struct field_t *f,
                         char *base, int level)
{
	unsigned int len;

	PRINT("%*.s%s", level * TAB_SIZE, " ", f->name);

	switch (f->sz) {
	case FIXD:		/* Range == 1 */
		if (f->lb > 2) {
			BYTE_ALIGN(bs);
			if (base && (f->attr & DECODE)) {
				/* The IP Address */
				IFTHEN(f->lb == 4,
				       PRINT(" = %d.%d.%d.%d:%d",
					     bs->cur[0], bs->cur[1],
					     bs->cur[2], bs->cur[3],
					     bs->cur[4] * 256 + bs->cur[5]));
				*((unsigned int *)(base + f->offset)) =
				    bs->cur - bs->buf;
			}
		}
		len = f->lb;
		break;
	case BYTE:		/* Range == 256 */
		BYTE_ALIGN(bs);
		CHECK_BOUND(bs, 1);
		len = (*bs->cur++) + f->lb;
		break;
	case SEMI:
		BYTE_ALIGN(bs);
		CHECK_BOUND(bs, 2);
		len = get_len(bs) + f->lb;
		break;
	default:		/* 2 <= Range <= 255 */
		len = get_bits(bs, f->sz) + f->lb;
		BYTE_ALIGN(bs);
		break;
	}

	bs->cur += len;

	PRINT("\n");

	CHECK_BOUND(bs, 0);
	return H323_ERROR_NONE;
}

/****************************************************************************/
static int decode_bmpstr(struct bitstr *bs, const struct field_t *f,
                         char *base, int level)
{
	unsigned int len;

	PRINT("%*.s%s\n", level * TAB_SIZE, " ", f->name);

	switch (f->sz) {
	case BYTE:		/* Range == 256 */
		BYTE_ALIGN(bs);
		CHECK_BOUND(bs, 1);
		len = (*bs->cur++) + f->lb;
		break;
	default:		/* 2 <= Range <= 255 */
		len = get_bits(bs, f->sz) + f->lb;
		BYTE_ALIGN(bs);
		break;
	}

	bs->cur += len << 1;

	CHECK_BOUND(bs, 0);
	return H323_ERROR_NONE;
}

/****************************************************************************/
static int decode_seq(struct bitstr *bs, const struct field_t *f,
                      char *base, int level)
{
	unsigned int ext, bmp, i, opt, len = 0, bmp2, bmp2_len;
	int err;
	const struct field_t *son;
	unsigned char *beg = NULL;

	PRINT("%*.s%s\n", level * TAB_SIZE, " ", f->name);

	/* Decode? */
	base = (base && (f->attr & DECODE)) ? base + f->offset : NULL;

	/* Extensible? */
	ext = (f->attr & EXT) ? get_bit(bs) : 0;

	/* Get fields bitmap */
	bmp = get_bitmap(bs, f->sz);
	if (base)
		*(unsigned int *)base = bmp;

	/* Decode the root components */
	for (i = opt = 0, son = f->fields; i < f->lb; i++, son++) {
		if (son->attr & STOP) {
			PRINT("%*.s%s\n", (level + 1) * TAB_SIZE, " ",
			      son->name);
			return H323_ERROR_STOP;
		}

		if (son->attr & OPT) {	/* Optional component */
			if (!((0x80000000U >> (opt++)) & bmp))	/* Not exist */
				continue;
		}

		/* Decode */
		if (son->attr & OPEN) {	/* Open field */
			CHECK_BOUND(bs, 2);
			len = get_len(bs);
			CHECK_BOUND(bs, len);
			if (!base || !(son->attr & DECODE)) {
				PRINT("%*.s%s\n", (level + 1) * TAB_SIZE,
				      " ", son->name);
				bs->cur += len;
				continue;
			}
			beg = bs->cur;

			/* Decode */
			if ((err = (Decoders[son->type]) (bs, son, base,
							  level + 1)) <
			    H323_ERROR_NONE)
				return err;

			bs->cur = beg + len;
			bs->bit = 0;
		} else if ((err = (Decoders[son->type]) (bs, son, base,
							 level + 1)) <
			   H323_ERROR_NONE)
			return err;
	}

	/* No extension? */
	if (!ext)
		return H323_ERROR_NONE;

	/* Get the extension bitmap */
	bmp2_len = get_bits(bs, 7) + 1;
	CHECK_BOUND(bs, (bmp2_len + 7) >> 3);
	bmp2 = get_bitmap(bs, bmp2_len);
	bmp |= bmp2 >> f->sz;
	if (base)
		*(unsigned int *)base = bmp;
	BYTE_ALIGN(bs);

	/* Decode the extension components */
	for (opt = 0; opt < bmp2_len; opt++, i++, son++) {
		/* Check Range */
		if (i >= f->ub) {	/* Newer Version? */
			CHECK_BOUND(bs, 2);
			len = get_len(bs);
			CHECK_BOUND(bs, len);
			bs->cur += len;
			continue;
		}

		if (son->attr & STOP) {
			PRINT("%*.s%s\n", (level + 1) * TAB_SIZE, " ",
			      son->name);
			return H323_ERROR_STOP;
		}

		if (!((0x80000000 >> opt) & bmp2))	/* Not present */
			continue;

		CHECK_BOUND(bs, 2);
		len = get_len(bs);
		CHECK_BOUND(bs, len);
		if (!base || !(son->attr & DECODE)) {
			PRINT("%*.s%s\n", (level + 1) * TAB_SIZE, " ",
			      son->name);
			bs->cur += len;
			continue;
		}
		beg = bs->cur;

		if ((err = (Decoders[son->type]) (bs, son, base,
						  level + 1)) <
		    H323_ERROR_NONE)
			return err;

		bs->cur = beg + len;
		bs->bit = 0;
	}
	return H323_ERROR_NONE;
}

/****************************************************************************/
static int decode_seqof(struct bitstr *bs, const struct field_t *f,
                        char *base, int level)
{
	unsigned int count, effective_count = 0, i, len = 0;
	int err;
	const struct field_t *son;
	unsigned char *beg = NULL;

	PRINT("%*.s%s\n", level * TAB_SIZE, " ", f->name);

	/* Decode? */
	base = (base && (f->attr & DECODE)) ? base + f->offset : NULL;

	/* Decode item count */
	switch (f->sz) {
	case BYTE:
		BYTE_ALIGN(bs);
		CHECK_BOUND(bs, 1);
		count = *bs->cur++;
		break;
	case WORD:
		BYTE_ALIGN(bs);
		CHECK_BOUND(bs, 2);
		count = *bs->cur++;
		count <<= 8;
		count += *bs->cur++;
		break;
	case SEMI:
		BYTE_ALIGN(bs);
		CHECK_BOUND(bs, 2);
		count = get_len(bs);
		break;
	default:
		count = get_bits(bs, f->sz);
		break;
	}
	count += f->lb;

	/* Write Count */
	if (base) {
		effective_count = count > f->ub ? f->ub : count;
		*(unsigned int *)base = effective_count;
		base += sizeof(unsigned int);
	}

	/* Decode nested field */
	son = f->fields;
	if (base)
		base -= son->offset;
	for (i = 0; i < count; i++) {
		if (son->attr & OPEN) {
			BYTE_ALIGN(bs);
			len = get_len(bs);
			CHECK_BOUND(bs, len);
			if (!base || !(son->attr & DECODE)) {
				PRINT("%*.s%s\n", (level + 1) * TAB_SIZE,
				      " ", son->name);
				bs->cur += len;
				continue;
			}
			beg = bs->cur;

			if ((err = (Decoders[son->type]) (bs, son,
							  i <
							  effective_count ?
							  base : NULL,
							  level + 1)) <
			    H323_ERROR_NONE)
				return err;

			bs->cur = beg + len;
			bs->bit = 0;
		} else
			if ((err = (Decoders[son->type]) (bs, son,
							  i <
							  effective_count ?
							  base : NULL,
							  level + 1)) <
			    H323_ERROR_NONE)
				return err;

		if (base)
			base += son->offset;
	}

	return H323_ERROR_NONE;
}


/****************************************************************************/
static int decode_choice(struct bitstr *bs, const struct field_t *f,
                         char *base, int level)
{
	unsigned int type, ext, len = 0;
	int err;
	const struct field_t *son;
	unsigned char *beg = NULL;

	PRINT("%*.s%s\n", level * TAB_SIZE, " ", f->name);

	/* Decode? */
	base = (base && (f->attr & DECODE)) ? base + f->offset : NULL;

	/* Decode the choice index number */
	if ((f->attr & EXT) && get_bit(bs)) {
		ext = 1;
		type = get_bits(bs, 7) + f->lb;
	} else {
		ext = 0;
		type = get_bits(bs, f->sz);
		if (type >= f->lb)
			return H323_ERROR_RANGE;
	}

	/* Write Type */
	if (base)
		*(unsigned int *)base = type;

	/* Check Range */
	if (type >= f->ub) {	/* Newer version? */
		BYTE_ALIGN(bs);
		len = get_len(bs);
		CHECK_BOUND(bs, len);
		bs->cur += len;
		return H323_ERROR_NONE;
	}

	/* Transfer to son level */
	son = &f->fields[type];
	if (son->attr & STOP) {
		PRINT("%*.s%s\n", (level + 1) * TAB_SIZE, " ", son->name);
		return H323_ERROR_STOP;
	}

	if (ext || (son->attr & OPEN)) {
		BYTE_ALIGN(bs);
		len = get_len(bs);
		CHECK_BOUND(bs, len);
		if (!base || !(son->attr & DECODE)) {
			PRINT("%*.s%s\n", (level + 1) * TAB_SIZE, " ",
			      son->name);
			bs->cur += len;
			return H323_ERROR_NONE;
		}
		beg = bs->cur;

		if ((err = (Decoders[son->type]) (bs, son, base, level + 1)) <
		    H323_ERROR_NONE)
			return err;

		bs->cur = beg + len;
		bs->bit = 0;
	} else if ((err = (Decoders[son->type]) (bs, son, base, level + 1)) <
		   H323_ERROR_NONE)
		return err;

	return H323_ERROR_NONE;
}

/****************************************************************************/
int DecodeRasMessage(unsigned char *buf, size_t sz, RasMessage *ras)
{
	static const struct field_t ras_message = {
		FNAME("RasMessage") CHOICE, 5, 24, 32, DECODE | EXT,
		0, _RasMessage
	};
	struct bitstr bs;

	bs.buf = bs.beg = bs.cur = buf;
	bs.end = buf + sz;
	bs.bit = 0;

	return decode_choice(&bs, &ras_message, (char *) ras, 0);
}

/****************************************************************************/
static int DecodeH323_UserInformation(unsigned char *buf, unsigned char *beg,
				      size_t sz, H323_UserInformation *uuie)
{
	static const struct field_t h323_userinformation = {
		FNAME("H323-UserInformation") SEQ, 1, 2, 2, DECODE | EXT,
		0, _H323_UserInformation
	};
	struct bitstr bs;

	bs.buf = buf;
	bs.beg = bs.cur = beg;
	bs.end = beg + sz;
	bs.bit = 0;

	return decode_seq(&bs, &h323_userinformation, (char *) uuie, 0);
}

/****************************************************************************/
int DecodeMultimediaSystemControlMessage(unsigned char *buf, size_t sz,
					 MultimediaSystemControlMessage *
					 mscm)
{
	static const struct field_t multimediasystemcontrolmessage = {
		FNAME("MultimediaSystemControlMessage") CHOICE, 2, 4, 4,
		DECODE | EXT, 0, _MultimediaSystemControlMessage
	};
	struct bitstr bs;

	bs.buf = bs.beg = bs.cur = buf;
	bs.end = buf + sz;
	bs.bit = 0;

	return decode_choice(&bs, &multimediasystemcontrolmessage,
			     (char *) mscm, 0);
}

/****************************************************************************/
int DecodeQ931(unsigned char *buf, size_t sz, Q931 *q931)
{
	unsigned char *p = buf;
	int len;

	if (!p || sz < 1)
		return H323_ERROR_BOUND;

	/* Protocol Discriminator */
	if (*p != 0x08) {
		PRINT("Unknown Protocol Discriminator\n");
		return H323_ERROR_RANGE;
	}
	p++;
	sz--;

	/* CallReferenceValue */
	if (sz < 1)
		return H323_ERROR_BOUND;
	len = *p++;
	sz--;
	if (sz < len)
		return H323_ERROR_BOUND;
	p += len;
	sz -= len;

	/* Message Type */
	if (sz < 2)
		return H323_ERROR_BOUND;
	q931->MessageType = *p++;
	sz--;
	PRINT("MessageType = %02X\n", q931->MessageType);
	if (*p & 0x80) {
		p++;
		sz--;
	}

	/* Decode Information Elements */
	while (sz > 0) {
		if (*p == 0x7e) {	/* UserUserIE */
			if (sz < 3)
				break;
			p++;
			len = *p++ << 8;
			len |= *p++;
			sz -= 3;
			if (sz < len)
				break;
			p++;
			len--;
			return DecodeH323_UserInformation(buf, p, len,
							  &q931->UUIE);
		}
		p++;
		sz--;
		if (sz < 1)
			break;
		len = *p++;
		sz--;
		if (sz < len)
			break;
		p += len;
		sz -= len;
	}

	PRINT("Q.931 UUIE not found\n");

	return H323_ERROR_BOUND;
}
