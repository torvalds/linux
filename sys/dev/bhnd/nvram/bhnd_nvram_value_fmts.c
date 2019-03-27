/*-
 * Copyright (c) 2015-2016 Landon Fuller <landonf@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>

#include <net/ethernet.h>

#ifdef _KERNEL

#include <sys/ctype.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/systm.h>

#include <machine/_inttypes.h>

#else /* !_KERNEL */

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#endif /* _KERNEL */

#include "bhnd_nvram_private.h"

#include "bhnd_nvram_valuevar.h"

static bool		 bhnd_nvram_ident_octet_string(const char *inp,
			     size_t ilen, char *delim, size_t *nelem);
static bool		 bhnd_nvram_ident_num_string(const char *inp,
			     size_t ilen, u_int base, u_int *obase);

static int		 bhnd_nvram_val_bcm_macaddr_filter(
			     const bhnd_nvram_val_fmt **fmt, const void *inp,
			     size_t ilen, bhnd_nvram_type itype);
static int		 bhnd_nvram_val_bcm_macaddr_encode(
			     bhnd_nvram_val *value, void *outp, size_t *olen,
			     bhnd_nvram_type otype);

static int		 bhnd_nvram_val_bcm_macaddr_string_filter(
			     const bhnd_nvram_val_fmt **fmt, const void *inp,
			     size_t ilen, bhnd_nvram_type itype);
static int		 bhnd_nvram_val_bcm_macaddr_string_encode_elem(
			     bhnd_nvram_val *value, const void *inp,
			     size_t ilen, void *outp, size_t *olen, 
			     bhnd_nvram_type otype);
static const void 	*bhnd_nvram_val_bcm_macaddr_string_next(
			     bhnd_nvram_val *value, const void *prev,
			     size_t *len);


static int		 bhnd_nvram_val_bcm_int_filter(
			     const bhnd_nvram_val_fmt **fmt, const void *inp,
			     size_t ilen, bhnd_nvram_type itype);
static int		 bhnd_nvram_val_bcm_int_encode(bhnd_nvram_val *value,
			     void *outp, size_t *olen, bhnd_nvram_type otype);

static int		 bhnd_nvram_val_bcm_decimal_encode_elem(
			     bhnd_nvram_val *value, const void *inp,
			     size_t ilen, void *outp, size_t *olen,
			     bhnd_nvram_type otype);
static int		 bhnd_nvram_val_bcm_hex_encode_elem(
			     bhnd_nvram_val *value, const void *inp,
			     size_t ilen, void *outp, size_t *olen,
			     bhnd_nvram_type otype);

static int		 bhnd_nvram_val_bcm_leddc_filter(
			     const bhnd_nvram_val_fmt **fmt, const void *inp,
			     size_t ilen, bhnd_nvram_type itype);
static int		 bhnd_nvram_val_bcm_leddc_encode_elem(
			     bhnd_nvram_val *value, const void *inp,
			     size_t ilen, void *outp, size_t *olen,
			     bhnd_nvram_type otype);


static int		 bhnd_nvram_val_bcmstr_encode(bhnd_nvram_val *value,
			     void *outp, size_t *olen, bhnd_nvram_type otype);

static int		 bhnd_nvram_val_bcmstr_csv_filter(
			     const bhnd_nvram_val_fmt **fmt, const void *inp,
			     size_t ilen, bhnd_nvram_type itype);
static const void	*bhnd_nvram_val_bcmstr_csv_next(bhnd_nvram_val *value,
			     const void *prev, size_t *len);

/**
 * Broadcom NVRAM MAC address format.
 */
const bhnd_nvram_val_fmt bhnd_nvram_val_bcm_macaddr_fmt = {
	.name		= "bcm-macaddr",
	.native_type	= BHND_NVRAM_TYPE_UINT8_ARRAY,
	.op_filter	= bhnd_nvram_val_bcm_macaddr_filter,
	.op_encode	= bhnd_nvram_val_bcm_macaddr_encode,
};

/** Broadcom NVRAM MAC address string format. */
static const bhnd_nvram_val_fmt bhnd_nvram_val_bcm_macaddr_string_fmt = {
	.name		= "bcm-macaddr-string",
	.native_type	= BHND_NVRAM_TYPE_STRING,
	.op_filter	= bhnd_nvram_val_bcm_macaddr_string_filter,
	.op_encode_elem	= bhnd_nvram_val_bcm_macaddr_string_encode_elem,
	.op_next	= bhnd_nvram_val_bcm_macaddr_string_next,
};

/**
 * Broadcom NVRAM LED duty-cycle format.
 */
const bhnd_nvram_val_fmt bhnd_nvram_val_bcm_leddc_fmt = {
	.name		= "bcm-leddc",
	.native_type	= BHND_NVRAM_TYPE_UINT32,
	.op_filter	= bhnd_nvram_val_bcm_leddc_filter,
	.op_encode_elem	= bhnd_nvram_val_bcm_leddc_encode_elem,
};

/**
 * Broadcom NVRAM decimal integer format.
 *
 * Extends standard integer handling, encoding the string representation of
 * the integer value as a decimal string:
 * - Positive values will be string-encoded without a prefix.
 * - Negative values will be string-encoded with a leading '-' sign.
 */
const bhnd_nvram_val_fmt bhnd_nvram_val_bcm_decimal_fmt = {
	.name		= "bcm-decimal",
	.native_type	= BHND_NVRAM_TYPE_UINT64,
	.op_filter	= bhnd_nvram_val_bcm_int_filter,
	.op_encode	= bhnd_nvram_val_bcm_int_encode,
	.op_encode_elem	= bhnd_nvram_val_bcm_decimal_encode_elem,
};

/**
 * Broadcom NVRAM decimal integer format.
 *
 * Extends standard integer handling, encoding the string representation of
 * unsigned and positive signed integer values as an 0x-prefixed hexadecimal
 * string.
 * 
 * For compatibility with standard Broadcom NVRAM parsing, if the integer is
 * both signed and negative, it will be string encoded as a negative decimal
 * value, not as a twos-complement hexadecimal value.
 */
const bhnd_nvram_val_fmt bhnd_nvram_val_bcm_hex_fmt = {
	.name		= "bcm-hex",
	.native_type	= BHND_NVRAM_TYPE_UINT64,
	.op_filter	= bhnd_nvram_val_bcm_int_filter,
	.op_encode	= bhnd_nvram_val_bcm_int_encode,
	.op_encode_elem	= bhnd_nvram_val_bcm_hex_encode_elem,
};

/**
 * Broadcom NVRAM string format.
 * 
 * Handles standard, comma-delimited, and octet-string values as used in
 * Broadcom NVRAM data.
 */
const bhnd_nvram_val_fmt bhnd_nvram_val_bcm_string_fmt = {
	.name		= "bcm-string",
	.native_type	= BHND_NVRAM_TYPE_STRING,
	.op_encode	= bhnd_nvram_val_bcmstr_encode,
};

/** Broadcom comma-delimited string. */
static const bhnd_nvram_val_fmt bhnd_nvram_val_bcm_string_csv_fmt = {
	.name		= "bcm-string[]",
	.native_type	= BHND_NVRAM_TYPE_STRING,
	.op_filter	= bhnd_nvram_val_bcmstr_csv_filter,
	.op_next	= bhnd_nvram_val_bcmstr_csv_next,
};


/* Built-in format definitions */
#define	BHND_NVRAM_VAL_FMT_NATIVE(_n, _type)				\
	const bhnd_nvram_val_fmt bhnd_nvram_val_ ## _n ## _fmt = {	\
		.name		= __STRING(_n),				\
		.native_type	= BHND_NVRAM_TYPE_ ## _type,		\
	}

BHND_NVRAM_VAL_FMT_NATIVE(uint8,	UINT8);
BHND_NVRAM_VAL_FMT_NATIVE(uint16,	UINT16);
BHND_NVRAM_VAL_FMT_NATIVE(uint32,	UINT32);
BHND_NVRAM_VAL_FMT_NATIVE(uint64,	UINT64);
BHND_NVRAM_VAL_FMT_NATIVE(int8,		INT8);
BHND_NVRAM_VAL_FMT_NATIVE(int16,	INT16);
BHND_NVRAM_VAL_FMT_NATIVE(int32,	INT32);
BHND_NVRAM_VAL_FMT_NATIVE(int64,	INT64);
BHND_NVRAM_VAL_FMT_NATIVE(char,		CHAR);
BHND_NVRAM_VAL_FMT_NATIVE(bool,		BOOL);
BHND_NVRAM_VAL_FMT_NATIVE(string,	STRING);
BHND_NVRAM_VAL_FMT_NATIVE(data,		DATA);
BHND_NVRAM_VAL_FMT_NATIVE(null,		NULL);

BHND_NVRAM_VAL_FMT_NATIVE(uint8_array,	UINT8_ARRAY);
BHND_NVRAM_VAL_FMT_NATIVE(uint16_array,	UINT16_ARRAY);
BHND_NVRAM_VAL_FMT_NATIVE(uint32_array,	UINT32_ARRAY);
BHND_NVRAM_VAL_FMT_NATIVE(uint64_array,	UINT64_ARRAY);
BHND_NVRAM_VAL_FMT_NATIVE(int8_array,	INT8_ARRAY);
BHND_NVRAM_VAL_FMT_NATIVE(int16_array,	INT16_ARRAY);
BHND_NVRAM_VAL_FMT_NATIVE(int32_array,	INT32_ARRAY);
BHND_NVRAM_VAL_FMT_NATIVE(int64_array,	INT64_ARRAY);
BHND_NVRAM_VAL_FMT_NATIVE(char_array,	CHAR_ARRAY);
BHND_NVRAM_VAL_FMT_NATIVE(bool_array,	BOOL_ARRAY);
BHND_NVRAM_VAL_FMT_NATIVE(string_array,	STRING_ARRAY);

/**
 * Common hex/decimal integer filter implementation.
 */
static int
bhnd_nvram_val_bcm_int_filter(const bhnd_nvram_val_fmt **fmt, const void *inp,
    size_t ilen, bhnd_nvram_type itype)
{
	bhnd_nvram_type	itype_base;

	itype_base = bhnd_nvram_base_type(itype);

	switch (itype_base) {
	case BHND_NVRAM_TYPE_STRING:
		/*
		 * If the input is a string, delegate to the Broadcom
		 * string format -- preserving the original string value
		 * takes priority over enforcing hexadecimal/integer string
		 * formatting.
		 */
		*fmt = &bhnd_nvram_val_bcm_string_fmt;
		return (0);

	default:
		if (bhnd_nvram_is_int_type(itype_base))
			return (0);

		return (EFTYPE);
	}
}

/**
 * Broadcom hex/decimal integer encode implementation.
 */
static int
bhnd_nvram_val_bcm_int_encode(bhnd_nvram_val *value, void *outp, size_t *olen,
    bhnd_nvram_type otype)
{
	/* If encoding to a string, format multiple elements (if any) with a
	 * comma delimiter. */
	if (otype == BHND_NVRAM_TYPE_STRING)
		return (bhnd_nvram_val_printf(value, "%[]s", outp, olen, ","));

	return (bhnd_nvram_val_generic_encode(value, outp, olen, otype));
}

/**
 * Broadcom hex integer encode_elem implementation.
 */
static int
bhnd_nvram_val_bcm_hex_encode_elem(bhnd_nvram_val *value, const void *inp,
    size_t ilen, void *outp, size_t *olen, bhnd_nvram_type otype)
{
	bhnd_nvram_type	itype;
	ssize_t		width;
	int		error;

	itype = bhnd_nvram_val_elem_type(value);
	BHND_NV_ASSERT(bhnd_nvram_is_int_type(itype), ("invalid type"));

	/* If not encoding as a string, perform generic value encoding */
	if (otype != BHND_NVRAM_TYPE_STRING)
		return (bhnd_nvram_val_generic_encode_elem(value, inp, ilen,
		    outp, olen, otype));

	/* If the value is a signed, negative value, encode as a decimal
	 * string */
	if (bhnd_nvram_is_signed_type(itype)) {
		int64_t		sval;
		size_t		slen;
		bhnd_nvram_type	stype;

		stype = BHND_NVRAM_TYPE_INT64;
		slen = sizeof(sval);

		/* Fetch 64-bit signed representation */
		error = bhnd_nvram_value_coerce(inp, ilen, itype, &sval, &slen,
		    stype);
		if (error)
			return (error);

		/* Decimal encoding required? */
		if (sval < 0)
			return (bhnd_nvram_value_printf("%I64d", &sval, slen,
			    stype, outp, olen, otype));
	}

	/*
	 * Encode the value as a hex string.
	 * 
	 * Most producers of Broadcom NVRAM values zero-pad hex values out to
	 * their native width (width * two hex characters), and we do the same
	 * for compatibility
	 */
	width = bhnd_nvram_type_width(itype) * 2;
	return (bhnd_nvram_value_printf("0x%0*I64X", inp, ilen, itype,
	    outp, olen, width));
}

/**
 * Broadcom decimal integer encode_elem implementation.
 */
static int
bhnd_nvram_val_bcm_decimal_encode_elem(bhnd_nvram_val *value, const void *inp,
    size_t ilen, void *outp, size_t *olen, bhnd_nvram_type otype)
{
	const char	*sfmt;
	bhnd_nvram_type	 itype;

	itype = bhnd_nvram_val_elem_type(value);
	BHND_NV_ASSERT(bhnd_nvram_is_int_type(itype), ("invalid type"));

	/* If not encoding as a string, perform generic value encoding */
	if (otype != BHND_NVRAM_TYPE_STRING)
		return (bhnd_nvram_val_generic_encode_elem(value, inp, ilen,
		    outp, olen, otype));

	sfmt = bhnd_nvram_is_signed_type(itype) ? "%I64d" : "%I64u";
	return (bhnd_nvram_value_printf(sfmt, inp, ilen, itype, outp, olen));
}

/**
 * Broadcom LED duty-cycle filter.
 */
static int
bhnd_nvram_val_bcm_leddc_filter(const bhnd_nvram_val_fmt **fmt,
    const void *inp, size_t ilen, bhnd_nvram_type itype)
{
	const char	*p;
	size_t		 plen;

	switch (itype) {
	case BHND_NVRAM_TYPE_UINT16:
	case BHND_NVRAM_TYPE_UINT32:
		return (0);

	case BHND_NVRAM_TYPE_STRING:
		/* Trim any whitespace */
		p = inp;
		plen = bhnd_nvram_trim_field(&p, ilen, '\0');

		/* If the value is not a valid integer string, delegate to the
		 * Broadcom string format */
		if (!bhnd_nvram_ident_num_string(p, plen, 0, NULL))
			*fmt = &bhnd_nvram_val_bcm_string_fmt;

		return (0);
	default:
		return (EFTYPE);
	}
}

/**
 * Broadcom LED duty-cycle encode.
 */
static int
bhnd_nvram_val_bcm_leddc_encode_elem(bhnd_nvram_val *value, const void *inp,
    size_t ilen, void *outp, size_t *olen, bhnd_nvram_type otype)
{
	bhnd_nvram_type		itype;
	size_t			limit, nbytes;
	int			error;
	uint16_t		led16;
	uint32_t		led32;
	bool			led16_lossy;
	union {
		uint16_t	u16;
		uint32_t	u32;
	} strval;

	/*
	 * LED duty-cycle values represent the on/off periods as a 32-bit
	 * integer, with the top 16 bits representing on cycles, and the
	 * bottom 16 representing off cycles.
	 * 
	 * LED duty cycle values have three different formats:
	 * 
	 * - SPROM:	A 16-bit unsigned integer, with on/off cycles encoded
	 *		as 8-bit values.
	 * - NVRAM:	A 16-bit decimal or hexadecimal string, with on/off
	 *		cycles encoded as 8-bit values as per the SPROM format.
	 * - NVRAM:	A 32-bit decimal or hexadecimal string, with on/off
	 *		cycles encoded as 16-bit values.
	 *
	 * To convert from a 16-bit representation to a 32-bit representation:
	 *     ((value & 0xFF00) << 16) | ((value & 0x00FF) << 8)
	 * 
	 * To convert from a 32-bit representation to a 16-bit representation,
	 * perform the same operation in reverse, discarding the lower 8-bits
	 * of each half of the 32-bit representation:
	 *     ((value >> 16) & 0xFF00) | ((value >> 8) & 0x00FF)
	 */

	itype = bhnd_nvram_val_elem_type(value);
	nbytes = 0;
	led16_lossy = false;

	/* Determine output byte limit */
	if (outp != NULL)
		limit = *olen;
	else
		limit = 0;

	/* If the input/output types match, just delegate to standard value
	 * encoding support */
	if (otype == itype) {
		return (bhnd_nvram_value_coerce(inp, ilen, itype, outp, olen,
		    otype));
	}

	/* If our value is a string, it may either be a 16-bit or a 32-bit
	 * representation of the duty cycle */
	if (itype == BHND_NVRAM_TYPE_STRING) {
		const char	*p;
		uint32_t	 ival;
		size_t		 nlen, parsed;

		/* Parse integer value */
		p = inp;
		nlen = sizeof(ival);
		error = bhnd_nvram_parse_int(p, ilen, 0, &parsed, &ival, &nlen,
		    BHND_NVRAM_TYPE_UINT32);
		if (error)
			return (error);

		/* Trailing garbage? */
		if (parsed < ilen && *(p+parsed) != '\0')
			return (EFTYPE);

		/* Point inp and itype to either our parsed 32-bit or 16-bit
		 * value */
		inp = &strval;
		if (ival & 0xFFFF0000) {
			strval.u32 = ival;
			itype = BHND_NVRAM_TYPE_UINT32;
		} else {
			strval.u16 = ival;
			itype = BHND_NVRAM_TYPE_UINT16;
		}
	}

	/* Populate both u32 and (possibly lossy) u16 LEDDC representations */
	switch (itype) {
	case BHND_NVRAM_TYPE_UINT16: {
		led16 = *(const uint16_t *)inp;
		led32 = ((led16 & 0xFF00) << 16) | ((led16 & 0x00FF) << 8);

		/* If all bits are set in the 16-bit value (indicating that
		 * the value is 'unset' in SPROM), we must update the 32-bit
		 * representation to match. */
		if (led16 == UINT16_MAX)
			led32 = UINT32_MAX;

		break;
	}

	case BHND_NVRAM_TYPE_UINT32:
		led32 = *(const uint32_t *)inp;
		led16 = ((led32 >> 16) & 0xFF00) | ((led32 >> 8) & 0x00FF);

		/*
		 * Determine whether the led16 conversion is lossy:
		 * 
		 * - If the lower 8 bits of each half of the 32-bit value
		 *   aren't set, we can safely use the 16-bit representation
		 *   without losing data.
		 * - If all bits in the 32-bit value are set, the variable is
		 *   treated as unset in  SPROM. We can safely use the 16-bit
		 *   representation without losing data.
		 */
		if ((led32 & 0x00FF00FF) != 0 && led32 != UINT32_MAX)
			led16_lossy = true;

		break;
	default:
		BHND_NV_PANIC("unsupported backing data type: %s",
		    bhnd_nvram_type_name(itype));
	}

	/*
	 * Encode as requested output type.
	 */
	switch (otype) {
	case BHND_NVRAM_TYPE_STRING:
		/*
		 * Prefer 16-bit format.
		 */
		if (!led16_lossy) {
			return (bhnd_nvram_value_printf("0x%04hX", &led16,
			    sizeof(led16), BHND_NVRAM_TYPE_UINT16, outp, olen));
		} else {
			return (bhnd_nvram_value_printf("0x%04X", &led32,
			    sizeof(led32), BHND_NVRAM_TYPE_UINT32, outp, olen));
		}

		break;

	case BHND_NVRAM_TYPE_UINT16: {
		/* Can we encode as uint16 without losing data? */
		if (led16_lossy)
			return (ERANGE);

		/* Write led16 format */
		nbytes += sizeof(uint16_t);
		if (limit >= nbytes)
			*(uint16_t *)outp = led16;

		break;
	}

	case BHND_NVRAM_TYPE_UINT32:
		/* Write led32 format */
		nbytes += sizeof(uint32_t);
		if (limit >= nbytes)
			*(uint32_t *)outp = led32;
		break;

	default:
		/* No other output formats are supported */
		return (EFTYPE);
	}

	/* Provide the actual length */
	*olen = nbytes;

	/* Report insufficient space (if output was requested) */
	if (limit < nbytes && outp != NULL)
		return (ENOMEM);

	return (0);
}

/**
 * Broadcom NVRAM string encoding.
 */
static int
bhnd_nvram_val_bcmstr_encode(bhnd_nvram_val *value, void *outp, size_t *olen,
    bhnd_nvram_type otype)
{
	bhnd_nvram_val			 array;
	const bhnd_nvram_val_fmt	*array_fmt;
	const void			*inp;
	bhnd_nvram_type			itype;
	size_t				ilen;
	int				error;

	inp = bhnd_nvram_val_bytes(value, &ilen, &itype);

	/* If the output is not an array type (or if it's a character array),
	 * we can fall back on standard string encoding */
	if (!bhnd_nvram_is_array_type(otype) ||
	    otype == BHND_NVRAM_TYPE_CHAR_ARRAY)
	{
		return (bhnd_nvram_value_coerce(inp, ilen, itype, outp, olen,
		    otype));
	}

	/* Otherwise, we need to interpret our value as either a macaddr
	 * string, or a comma-delimited string. */
	inp = bhnd_nvram_val_bytes(value, &ilen, &itype);
	if (bhnd_nvram_ident_octet_string(inp, ilen, NULL, NULL))
		array_fmt = &bhnd_nvram_val_bcm_macaddr_string_fmt;
	else
		array_fmt = &bhnd_nvram_val_bcm_string_csv_fmt;

	/* Wrap in array-typed representation */
	error = bhnd_nvram_val_init(&array, array_fmt, inp, ilen, itype,
	    BHND_NVRAM_VAL_BORROW_DATA);
	if (error) {
		BHND_NV_LOG("error initializing array representation: %d\n",
		    error);
		return (error);
	}

	/* Ask the array-typed value to perform the encode */
	error = bhnd_nvram_val_encode(&array, outp, olen, otype);
	if (error)
		BHND_NV_LOG("error encoding array representation: %d\n", error);

	bhnd_nvram_val_release(&array);

	return (error);
}

/**
 * Broadcom NVRAM comma-delimited string filter.
 */
static int
bhnd_nvram_val_bcmstr_csv_filter(const bhnd_nvram_val_fmt **fmt,
    const void *inp, size_t ilen, bhnd_nvram_type itype)
{
	switch (itype) {
	case BHND_NVRAM_TYPE_STRING:
	case BHND_NVRAM_TYPE_STRING_ARRAY:
		return (0);
	default:
		return (EFTYPE);
	}
}

/**
 * Broadcom NVRAM comma-delimited string iteration.
 */
static const void *
bhnd_nvram_val_bcmstr_csv_next(bhnd_nvram_val *value, const void *prev,
    size_t *len)
{
	const char	*next;
	const char	*inp;
	bhnd_nvram_type	 itype;
	size_t		 ilen, remain;
	char		 delim;

	/* Fetch backing representation */
	inp = bhnd_nvram_val_bytes(value, &ilen, &itype);

	/* Fetch next value */
	switch (itype) {
	case BHND_NVRAM_TYPE_STRING:
		/* Zero-length array? */
		if (ilen == 0)
			return (NULL);

		if (prev == NULL) {
			/* First element */
			next = inp;
			remain = ilen;
			delim = ',';
		} else {
			/* Advance to the previous element's delimiter */
			next = (const char *)prev + *len;

			/* Did we hit the end of the string? */
			if ((size_t)(next - inp) >= ilen)
				return (NULL);

			/* Fetch (and skip past) the delimiter */
			delim = *next;
			next++;
			remain = ilen - (size_t)(next - inp);

			/* Was the delimiter the final character? */
			if (remain == 0)
				return (NULL);
		}

		/* Parse the field value, up to the next delimiter */
		*len = bhnd_nvram_parse_field(&next, remain, delim);

		return (next);

	case BHND_NVRAM_TYPE_STRING_ARRAY:
		/* Delegate to default array iteration */
		return (bhnd_nvram_value_array_next(inp, ilen, itype, prev,
		    len));
	default:
		BHND_NV_PANIC("unsupported type: %d", itype);
	}
}

/**
 * MAC address filter.
 */
static int
bhnd_nvram_val_bcm_macaddr_filter(const bhnd_nvram_val_fmt **fmt,
    const void *inp, size_t ilen, bhnd_nvram_type itype)
{
	switch (itype) {
	case BHND_NVRAM_TYPE_UINT8_ARRAY:
		return (0);
	case BHND_NVRAM_TYPE_STRING:
		/* Let bcm_macaddr_string format handle it */
		*fmt = &bhnd_nvram_val_bcm_macaddr_string_fmt;
		return (0);
	default:
		return (EFTYPE);
	}
}

/**
 * MAC address encoding.
 */
static int
bhnd_nvram_val_bcm_macaddr_encode(bhnd_nvram_val *value, void *outp,
    size_t *olen, bhnd_nvram_type otype)
{
	const void	*inp;
	bhnd_nvram_type	 itype;
	size_t		 ilen;

	/*
	 * If converting to a string (or a single-element string array),
	 * produce an octet string (00:00:...).
	 */
	if (bhnd_nvram_base_type(otype) == BHND_NVRAM_TYPE_STRING) {
		return (bhnd_nvram_val_printf(value, "%[]02hhX", outp, olen,
		    ":"));
	}

	/* Otherwise, use standard encoding support */
	inp = bhnd_nvram_val_bytes(value, &ilen, &itype);
	return (bhnd_nvram_value_coerce(inp, ilen, itype, outp, olen, otype));}

/**
 * MAC address string filter.
 */
static int
bhnd_nvram_val_bcm_macaddr_string_filter(const bhnd_nvram_val_fmt **fmt,
    const void *inp, size_t ilen, bhnd_nvram_type itype)
{
	switch (itype) {
	case BHND_NVRAM_TYPE_STRING:
		/* Use the standard Broadcom string format implementation if
		 * the input is not an octet string. */
		if (!bhnd_nvram_ident_octet_string(inp, ilen, NULL, NULL))
			*fmt = &bhnd_nvram_val_bcm_string_fmt;

		return (0);
	default:
		return (EFTYPE);
	}
}


/**
 * MAC address string octet encoding.
 */
static int
bhnd_nvram_val_bcm_macaddr_string_encode_elem(bhnd_nvram_val *value,
    const void *inp, size_t ilen, void *outp, size_t *olen,
    bhnd_nvram_type otype)
{
	size_t	nparsed;
	int	error;

	/* If integer encoding is requested, explicitly parse our
	 * non-0x-prefixed as a base 16 integer value */
	if (bhnd_nvram_is_int_type(otype)) {
		error = bhnd_nvram_parse_int(inp, ilen, 16, &nparsed, outp,
		    olen, otype);
		if (error)
			return (error);

		if (nparsed != ilen)
			return (EFTYPE);

		return (0);
	}

	/* Otherwise, use standard encoding support */
	return (bhnd_nvram_value_coerce(inp, ilen,
	    bhnd_nvram_val_elem_type(value), outp, olen, otype));
}

/**
 * MAC address string octet iteration.
 */
static const void *
bhnd_nvram_val_bcm_macaddr_string_next(bhnd_nvram_val *value, const void *prev,
    size_t *len)
{
	const char	*next;
	const char	*str;
	bhnd_nvram_type	 stype;
	size_t		 slen, remain;
	char		 delim;

	/* Fetch backing string */
	str = bhnd_nvram_val_bytes(value, &slen, &stype);
	BHND_NV_ASSERT(stype == BHND_NVRAM_TYPE_STRING,
	    ("unsupported type: %d", stype));

	/* Zero-length array? */
	if (slen == 0)
		return (NULL);

	if (prev == NULL) {
		/* First element */

		/* Determine delimiter */
		if (!bhnd_nvram_ident_octet_string(str, slen, &delim, NULL)) {
			/* Default to comma-delimited parsing */
			delim = ',';
		}

		/* Parsing will start at the base string pointer */
		next = str;
		remain = slen;
	} else {
		/* Advance to the previous element's delimiter */
		next = (const char *)prev + *len;

		/* Did we hit the end of the string? */
		if ((size_t)(next - str) >= slen)
			return (NULL);

		/* Fetch (and skip past) the delimiter */
		delim = *next;
		next++;
		remain = slen - (size_t)(next - str);

		/* Was the delimiter the final character? */
		if (remain == 0)
			return (NULL);
	}

	/* Parse the field value, up to the next delimiter */
	*len = bhnd_nvram_parse_field(&next, remain, delim);

	return (next);
}


/**
 * Determine whether @p inp is in octet string format, consisting of a
 * fields of two hex characters, separated with ':' or '-' delimiters.
 * 
 * This may be used to identify MAC address octet strings
 * (BHND_NVRAM_SFMT_MACADDR).
 *
 * @param		inp	The string to be parsed.
 * @param		ilen	The length of @p inp, in bytes.
 * @param[out]		delim	On success, the delimiter used by this octet
 * 				string. May be set to NULL if the field
 *				delimiter is not desired.
 * @param[out]		nelem	On success, the number of fields in this
 *				octet string. May be set to NULL if the field
 *				count is not desired.
 *
 * 
 * @retval true		if @p inp is a valid octet string
 * @retval false	if @p inp is not a valid octet string.
 */
static bool
bhnd_nvram_ident_octet_string(const char *inp, size_t ilen, char *delim,
    size_t *nelem)
{
	size_t	elem_count;
	size_t	max_elem_count, min_elem_count;
	size_t	field_count;
	char	idelim;

	field_count = 0;

	/* Require exactly two digits. If we relax this, there is room
	 * for ambiguity with signed integers and the '-' delimiter */
	min_elem_count = 2;
	max_elem_count = 2;

	/* Identify the delimiter used. The standard delimiter for MAC
	 * addresses is ':', but some earlier NVRAM formats may use '-' */
	for (const char *d = ":-";; d++) {
		const char *loc;

		/* No delimiter found, not an octet string */
		if (*d == '\0')
			return (false);

		/* Look for the delimiter */
		if ((loc = memchr(inp, *d, ilen)) == NULL)
			continue;

		/* Delimiter found */
		idelim = *loc;
		break;
	}

	/* To disambiguate from signed integers, if the delimiter is "-",
	 * the octets must be exactly 2 chars each */
	if (idelim == '-')
		min_elem_count = 2;

	/* String must be composed of individual octets (zero or more hex
	 * digits) separated by our delimiter. */
	elem_count = 0;
	for (const char *p = inp; (size_t)(p - inp) < ilen; p++) {
		switch (*p) {
		case ':':
		case '-':
		case '\0':
			/* Hit a delim character; all delims must match
			 * the first delimiter used */
			if (*p != '\0' && *p != idelim)
				return (false);

			/* Must have parsed at least min_elem_count digits */
			if (elem_count < min_elem_count)
				return (false);

			/* Reset element count */
			elem_count = 0;

			/* Bump field count */
			field_count++;
			break;
		default:
			/* More than maximum number of hex digits? */
			if (elem_count >= max_elem_count)
				return (false);

			/* Octet values must be hex digits */
			if (!bhnd_nv_isxdigit(*p))
				return (false);

			elem_count++;
			break;
		}
	}

	if (delim != NULL)
		*delim = idelim;

	if (nelem != NULL)
		*nelem = field_count;

	return (true);
}


/**
 * Determine whether @p inp is in hexadecimal, octal, or decimal string
 * format.
 *
 * - A @p str may be prefixed with a single optional '+' or '-' sign denoting
 *   signedness.
 * - A hexadecimal @p str may include an '0x' or '0X' prefix, denoting that a
 *   base 16 integer follows.
 * - An octal @p str may include a '0' prefix, denoting that an octal integer
 *   follows.
 * 
 * @param	inp	The string to be parsed.
 * @param	ilen	The length of @p inp, in bytes.
 * @param	base	The input string's base (2-36), or 0.
 * @param[out]	obase	On success, will be set to the base of the parsed
 *			integer. May be set to NULL if the base is not
 *			desired.
 *
 * @retval true		if @p inp is a valid number string
 * @retval false	if @p inp is not a valid number string.
 * @retval false	if @p base is invalid.
 */
static bool
bhnd_nvram_ident_num_string(const char *inp, size_t ilen, u_int base,
    u_int *obase)
{
	size_t	nbytes, ndigits;

	nbytes = 0;
	ndigits = 0;

	/* Parse and skip sign */
	if (nbytes >= ilen)
		return (false);

	if (inp[nbytes] == '-' || inp[nbytes] == '+')
		nbytes++;

	/* Truncated after sign character? */
	if (nbytes == ilen)
		return (false);

	/* Identify (or validate) hex base, skipping 0x/0X prefix */
	if (base == 16 || base == 0) {
		/* Check for (and skip) 0x/0X prefix */
		if (ilen - nbytes >= 2 && inp[nbytes] == '0' &&
		    (inp[nbytes+1] == 'x' || inp[nbytes+1] == 'X'))
		{
			base = 16;
			nbytes += 2;
		}
	}

	/* Truncated after hex prefix? */
	if (nbytes == ilen)
		return (false);

	/* Differentiate decimal/octal by looking for a leading 0 */
	if (base == 0) {
		if (inp[nbytes] == '0') {
			base = 8;
		} else {
			base = 10;
		}
	}

	/* Consume and validate all remaining digit characters */
	for (; nbytes < ilen; nbytes++) {
		u_int	carry;
		char	c;

		/* Parse carry value */
		c = inp[nbytes];
		if (bhnd_nv_isdigit(c)) {
			carry = c - '0';
		} else if (bhnd_nv_isxdigit(c)) {
			if (bhnd_nv_isupper(c))
				carry = (c - 'A') + 10;
			else
				carry = (c - 'a') + 10;
		} else {
			/* Hit a non-digit character */
			return (false);
		}

		/* If carry is outside the base, it's not a valid digit
		 * in the current parse context; consider it a non-digit
		 * character */
		if (carry >= base)
			return (false);

		/* Increment parsed digit count */
		ndigits++;
	}

	/* Empty integer string? */
	if (ndigits == 0)
		return (false);

	/* Valid integer -- provide the base and return */
	if (obase != NULL)
		*obase = base;
	return (true);
}
