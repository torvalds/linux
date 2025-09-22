/*	$OpenBSD: flowspec.c,v 1.5 2023/10/23 13:07:44 claudio Exp $ */

/*
 * Copyright (c) 2023 Claudio Jeker <claudio@openbsd.org>
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

#include <string.h>
#include <stdio.h>

#include "bgpd.h"
#include "rde.h"

/*
 * Extract the next component from a flowspec NLRI buffer.
 * Returns the length of the component including type field or -1 on failure.
 * Also checks that the prefix encoding is valid.
 */
static int
flowspec_next_component(const uint8_t *buf, int len, int is_v6, int *type)
{
	int vlen = 0;
	uint8_t plen, off, op;

	*type = 0;
	if (len < 1)
		return -1;
	*type = buf[vlen];
	vlen++;
	if (*type < FLOWSPEC_TYPE_MIN || *type >= FLOWSPEC_TYPE_MAX)
		return -1;

	switch (*type) {
	case FLOWSPEC_TYPE_DEST:
	case FLOWSPEC_TYPE_SOURCE:
		if (!is_v6) {
			/* regular RFC 4271 encoding of prefixes */
			if (len < vlen + 1)
				return -1;
			plen = buf[vlen];
			vlen += PREFIX_SIZE(plen);
			if (plen > 32 || len < vlen)
				return -1;
		} else {
			/* special RFC 8956 encoding with extra offset */
			if (len < vlen + 2)
				return -1;
			plen = buf[vlen];
			off = buf[vlen + 1];
			if (plen > 128 || off >= plen)
				return -1;
			vlen += PREFIX_SIZE(plen - off) + 1; /* off is extra */
			if (len < vlen)
				return -1;
		}
		break;
	case FLOWSPEC_TYPE_FLOW:
		if (!is_v6)
			return -1;
		/* FALLTHROUGH */
	default:
		do {
			if (len < vlen + 1)
				return -1;
			op = buf[vlen];
			/* first component cannot have and flag set */
			if (vlen == 1 && op & FLOWSPEC_OP_AND)
				return -1;
			vlen += FLOWSPEC_OP_LEN(op) + 1;

			if (len < vlen)
				return -1;
		} while ((op & FLOWSPEC_OP_EOL) == 0);
		break;
	}
	return vlen;
}

#define MINIMUM(a, b)	((a) < (b) ? (a) : (b))

/*
 * Compare two IPv4 flowspec prefix components.
 * Returns -1 if first prefix is preferred, 1 if second, 0 if equal.
 */
static int
flowspec_cmp_prefix4(const uint8_t *abuf, int ablen, const uint8_t *bbuf,
    int bblen)
{
	uint8_t a[4] = { 0 }, b[4] = { 0 };
	int alen, blen, clen, cmp;

	alen = abuf[1];
	blen = bbuf[1];
	clen = MINIMUM(alen, blen);

	/* only extract the common prefix */
	extract_prefix(abuf + 2, ablen - 2, &a, clen, sizeof(a));
	extract_prefix(bbuf + 2, bblen - 2, &b, clen, sizeof(b));

	/* lowest IP value has precedence */
	cmp = memcmp(a, b, sizeof(a));
	if (cmp < 0)
		return -1;
	if (cmp > 0)
		return 1;

	/* if common prefix, more specific route has precedence */
	if (alen > blen)
		return -1;
	if (alen < blen)
		return 1;
	return 0;
}

/*
 * Compare two IPv6 flowspec prefix components.
 * Returns 1 if first prefix is preferred, -1 if second, 0 if equal.
 * As usual the encoding of IPv6 addresses is extra complex.
 */
static int
flowspec_cmp_prefix6(const uint8_t *abuf, int ablen, const uint8_t *bbuf,
    int bblen)
{
	uint8_t a[16] = { 0 }, b[16] = { 0 };
	int alen, blen, clen, cmp;

	/* lowest offset has precedence */
	if (abuf[2] < bbuf[2])
		return -1;
	if (abuf[2] > bbuf[2])
		return 1;

	/* calculate actual pattern size (len - offset) */
	alen = abuf[1] - abuf[2];
	blen = bbuf[1] - bbuf[2];
	clen = MINIMUM(alen, blen);

	/* only extract the common prefix */
	extract_prefix(abuf + 3, ablen - 3, &a, clen, sizeof(a));
	extract_prefix(bbuf + 3, bblen - 3, &b, clen, sizeof(b));

	/* lowest IP value has precedence */
	cmp = memcmp(a, b, sizeof(a));
	if (cmp < 0)
		return -1;
	if (cmp > 0)
		return 1;

	/* if common prefix, more specific route has precedence */
	if (alen > blen)
		return -1;
	if (alen < blen)
		return 1;
	return 0;
}

/*
 * Check if the flowspec NLRI is syntactically valid.
 */
int
flowspec_valid(const uint8_t *buf, int len, int is_v6)
{
	int l, type, prev = 0;

	/* empty NLRI is invalid */
	if (len == 0)
		return -1;

	while (len > 0) {
		l = flowspec_next_component(buf, len, is_v6, &type);
		if (l == -1)
			return -1;
		/* ensure that types are ordered */
		if (prev >= type)
			return -1;
		prev = type;
		buf += l;
		len -= l;
	}
	if (len < 0)
		return -1;
	return 0;
}

/*
 * Compare two valid flowspec NLRI objects according to RFC 8955 & 8956.
 * Returns -1 if the first object has preference, 1 if not, and 0 if the
 * two objects are equal.
 */
int
flowspec_cmp(const uint8_t *a, int alen, const uint8_t *b, int blen, int is_v6)
{
	int atype, btype;
	int acomplen, bcomplen;
	int cmp;

	while (alen > 0 && blen > 0) {
		acomplen = flowspec_next_component(a, alen, is_v6, &atype);
		bcomplen = flowspec_next_component(b, blen, is_v6, &btype);
		/* should not happen */
		if (acomplen == -1)
			return 1;
		if (bcomplen == -1)
			return -1;

		/* If types differ, lowest type wins. */
		if (atype < btype)
			return -1;
		if (atype > btype)
			return 1;

		switch (atype) {
		case FLOWSPEC_TYPE_DEST:
		case FLOWSPEC_TYPE_SOURCE:
			if (!is_v6) {
				cmp = flowspec_cmp_prefix4(a, acomplen,
				    b, bcomplen);
			} else {
				cmp = flowspec_cmp_prefix6(a, acomplen,
				    b, bcomplen);
			}
			if (cmp != 0)
				return cmp;
			break;
		default:
			cmp = memcmp(a, b, MINIMUM(acomplen, bcomplen));
			/*
			 * Lowest common component prefix wins also
			 * if both commponents are same length also lowest
			 * string has precedence.
			 */
			if (cmp < 0)
				return -1;
			if (cmp > 0)
				return 1;
			/*
			 * Longest component wins when common prefix is equal.
			 * This is not really possible because EOL encoding will
			 * always tie break on the memcmp but the RFC mandates
			 * it (and it is cheap).
			 */
			if (acomplen > bcomplen)
				return -1;
			if (acomplen < bcomplen)
				return 1;
			break;
		}
		a += acomplen;
		alen -= acomplen;
		b += bcomplen;
		blen -= bcomplen;

		/* Rule with more components wins */
		if (alen > 0 && blen <= 0)
			return -1;
		if (alen <= 0 && blen > 0)
			return 1;
	}
	return 0;
}

static void
shift_right(uint8_t *dst, const uint8_t *src, int off, int len)
{
	uint8_t carry = 0;
	int i;

	dst += off / 8;		/* go to inital start point */
	off %= 8;
	len = (len + 7) / 8;	/* how much to copy in bytes */

	for (i = 0; i < len; i++) {
		dst[i] = carry | src[i] >> off;
		if (off != 0)
			carry = src[i] << (8 - off);
	}
	dst[i] = carry;
}

/*
 * Extract a flowspec component and return its buffer and size.
 * Returns 1 on success, 0 if component is not present and -1 on error.
 */
int
flowspec_get_component(const uint8_t *flow, int flowlen, int type, int is_v6,
    const uint8_t **buf, int *len)
{
	int complen, t;

	*buf = NULL;
	*len = 0;

	do {
		complen = flowspec_next_component(flow, flowlen, is_v6, &t);
		if (complen == -1)
			return -1;
		if (type == t)
			break;
		if (type < t)
			return 0;

		flow += complen;
		flowlen -= complen;
	} while (1);

	*buf = flow + 1;
	*len = complen - 1;

	return 1;
}

/*
 * Extract source or destination address into provided bgpd_addr.
 * Returns 1 on success, 0 if no address was present, -1 on error.
 * Sets plen to the prefix len and olen to the offset for IPv6 case.
 * If olen is set to NULL when an IPv6 prefix with offset is fetched
 * the function will return -1.
 */
int
flowspec_get_addr(const uint8_t *flow, int flowlen, int type, int is_v6,
    struct bgpd_addr *addr, uint8_t *plen, uint8_t *olen)
{
	const uint8_t *comp;
	int complen, rv;

	memset(addr, 0, sizeof(*addr));
	*plen = 0;
	if (olen != NULL)
		*olen = 0;

	rv = flowspec_get_component(flow, flowlen, type, is_v6,
	    &comp, &complen);
	if (rv != 1)
		return rv;

	/* flowspec_get_component only returns valid encodings */
	if (!is_v6) {
		addr->aid = AID_INET;
		if (extract_prefix(comp + 1, complen - 1, &addr->v4, comp[0],
		    sizeof(addr->v4)) == -1)
			return -1;
		*plen = comp[0];
	} else {
		uint8_t buf[16] = { 0 };
		int xlen, xoff = 0;

		addr->aid = AID_INET6;
		xlen = comp[0];
		if (comp[1] != 0) {
			if (olen == NULL)
				return -1;
			xoff = comp[1];
			xlen -= xoff;
		}
		if (extract_prefix(comp + 2, complen - 2, buf, xlen,
		    sizeof(buf)) == -1)
			return -1;
		shift_right(addr->v6.s6_addr, buf, xoff, xlen);
		*plen = comp[0];
		if (olen != NULL)
			*olen = comp[1];
	}

	return 1;
}

const char *
flowspec_fmt_label(int type)
{
	switch (type) {
	case FLOWSPEC_TYPE_DEST:
		return "to";
	case FLOWSPEC_TYPE_SOURCE:
		return "from";
	case FLOWSPEC_TYPE_PROTO:
		return "proto";
	case FLOWSPEC_TYPE_PORT:
	case FLOWSPEC_TYPE_DST_PORT:
	case FLOWSPEC_TYPE_SRC_PORT:
		return "port";
	case FLOWSPEC_TYPE_ICMP_TYPE:
		return "icmp-type";
	case FLOWSPEC_TYPE_ICMP_CODE:
		return "icmp-code";
	case FLOWSPEC_TYPE_TCP_FLAGS:
		return "flags";
	case FLOWSPEC_TYPE_PKT_LEN:
		return "length";
	case FLOWSPEC_TYPE_DSCP:
		return "tos";
	case FLOWSPEC_TYPE_FRAG:
		return "fragment";
	case FLOWSPEC_TYPE_FLOW:
		return "flow";
	default:
		return "???";
	}
}

static uint64_t
extract_val(const uint8_t *comp, int len)
{
	uint64_t val = 0;

	while (len-- > 0) {
		val <<= 8;
		val |= *comp++;
	}
	return val;
}

const char *
flowspec_fmt_num_op(const uint8_t *comp, int complen, int *off)
{
	static char buf[32];
	uint64_t val, val2 = 0;
	uint8_t op, op2 = 0;
	int len, len2 = 0;

	if (*off == -1)
		return "";
	if (complen < *off + 1)
		return "bad encoding";

	op = comp[*off];
	len = FLOWSPEC_OP_LEN(op) + 1;
	if (complen < *off + len)
		return "bad encoding";
	val = extract_val(comp + *off + 1, FLOWSPEC_OP_LEN(op));

	if ((op & FLOWSPEC_OP_EOL) == 0) {
		if (complen < *off + len + 1)
			return "bad encoding";
		op2 = comp[*off + len];
		/*
		 * Check if this is a range specification else fall back
		 * to basic rules.
		 */
		if (op2 & FLOWSPEC_OP_AND &&
		    (op & FLOWSPEC_OP_NUM_MASK) == FLOWSPEC_OP_NUM_GE &&
		    (op2 & FLOWSPEC_OP_NUM_MASK) == FLOWSPEC_OP_NUM_LE) {
			len2 =  FLOWSPEC_OP_LEN(op2) + 1;
			val2 = extract_val(comp + *off + len + 1,
			    FLOWSPEC_OP_LEN(op2));
		} else
			op2 = 0;
	}

	if (op2 & FLOWSPEC_OP_AND) {
		/* binary range operation */
		snprintf(buf, sizeof(buf), "%llu - %llu",
		    (unsigned long long)val, (unsigned long long)val2);
	} else {
		/* unary operation */
		switch (op & FLOWSPEC_OP_NUM_MASK) {
		case 0:
			snprintf(buf, sizeof(buf), "%sfalse",
			    op & FLOWSPEC_OP_AND ? "&& " : "");
			break;
		case FLOWSPEC_OP_NUM_EQ:
			snprintf(buf, sizeof(buf), "%s%llu",
			    op & FLOWSPEC_OP_AND ? "&& " : "",
			    (unsigned long long)val);
			break;
		case FLOWSPEC_OP_NUM_GT:
			snprintf(buf, sizeof(buf), "%s> %llu",
			    op & FLOWSPEC_OP_AND ? "&& " : "",
			    (unsigned long long)val);
			break;
		case FLOWSPEC_OP_NUM_GE:
			snprintf(buf, sizeof(buf), "%s>= %llu",
			    op & FLOWSPEC_OP_AND ? "&& " : "",
			    (unsigned long long)val);
			break;
		case FLOWSPEC_OP_NUM_LT:
			snprintf(buf, sizeof(buf), "%s< %llu",
			    op & FLOWSPEC_OP_AND ? "&& " : "",
			    (unsigned long long)val);
			break;
		case FLOWSPEC_OP_NUM_LE:
			snprintf(buf, sizeof(buf), "%s<= %llu",
			    op & FLOWSPEC_OP_AND ? "&& " : "",
			    (unsigned long long)val);
			break;
		case FLOWSPEC_OP_NUM_NOT:
			snprintf(buf, sizeof(buf), "%s!= %llu",
			    op & FLOWSPEC_OP_AND ? "&& " : "",
			    (unsigned long long)val);
			break;
		case 0x7:
			snprintf(buf, sizeof(buf), "%strue",
			    op & FLOWSPEC_OP_AND ? "&& " : "");
			break;
		}
	}

	if (op2 & FLOWSPEC_OP_EOL || op & FLOWSPEC_OP_EOL)
		*off = -1;
	else
		*off += len + len2;

	return buf;
}

static const char *
fmt_flags(uint64_t val, const char *bits, char *buf, size_t blen)
{
	int i, bi;

	for (i = 0, bi = 0; i < 64 && val != 0; i++) {
		if (val & 1) {
			if (bits[i] == '\0' || bits[i] == ' ')
				goto fail;
			buf[bi++] = bits[i];
		}
		val >>= 1;
	}
	buf[bi++] = '\0';
	return buf;

fail:
	snprintf(buf, blen, "%llx", (unsigned long long)val);
	return buf;
}

const char *
flowspec_fmt_bin_op(const uint8_t *comp, int complen, int *off,
    const char *bits)
{
	static char buf[36], bit[17], mask[17];
	uint64_t val, val2 = 0;
	uint8_t op, op2 = 0;
	int len, len2 = 0;

	if (*off == -1)
		return "";
	if (complen < *off + 1)
		return "bad encoding";

	op = comp[*off];
	len = FLOWSPEC_OP_LEN(op) + 1;
	if (complen < *off + len)
		return "bad encoding";
	val = extract_val(comp + *off + 1, FLOWSPEC_OP_LEN(op));

	if ((op & FLOWSPEC_OP_EOL) == 0) {
		if (complen < *off + len + 1)
			return "bad encoding";
		op2 = comp[*off + len];
		/*
		 * Check if this is a mask specification else fall back
		 * to basic rules.
		 */
		if (op2 & FLOWSPEC_OP_AND &&
		    (op & FLOWSPEC_OP_BIT_MASK) == FLOWSPEC_OP_BIT_MATCH &&
		    (op2 & FLOWSPEC_OP_BIT_MASK) == FLOWSPEC_OP_BIT_NOT) {
			len2 =  FLOWSPEC_OP_LEN(op2) + 1;
			val2 = extract_val(comp + *off + len + 1,
			    FLOWSPEC_OP_LEN(op2));
		} else
			op2 = 0;
	}

	if (op2 & FLOWSPEC_OP_AND) {
		val2 |= val;
		snprintf(buf, sizeof(buf), "%s / %s",
		    fmt_flags(val, bits, bit, sizeof(bit)),
		    fmt_flags(val2, bits, mask, sizeof(mask)));
	} else {
		switch (op & FLOWSPEC_OP_BIT_MASK) {
		case 0:
			snprintf(buf, sizeof(buf), "%s",
			    fmt_flags(val, bits, bit, sizeof(bit)));
			break;
		case FLOWSPEC_OP_BIT_NOT:
			snprintf(buf, sizeof(buf), "/ %s",
			    fmt_flags(val, bits, mask, sizeof(mask)));
			break;
		case FLOWSPEC_OP_BIT_MATCH:
			snprintf(buf, sizeof(buf), "%s / %s",
			    fmt_flags(val, bits, bit, sizeof(bit)),
			    fmt_flags(val, bits, mask, sizeof(mask)));
			break;
		case FLOWSPEC_OP_BIT_NOT | FLOWSPEC_OP_BIT_MATCH:
			snprintf(buf, sizeof(buf), "???");
			break;
		}
	}

	if (op2 & FLOWSPEC_OP_EOL || op & FLOWSPEC_OP_EOL)
		*off = -1;
	else
		*off += len + len2;

	return buf;
}
