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
#include <sys/sbuf.h>

#ifdef _KERNEL

#include <sys/ctype.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/systm.h>

#include <machine/_inttypes.h>

#else /* !_KERNEL */

#include <ctype.h>
#include <inttypes.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#endif /* _KERNEL */

#include "bhnd_nvram_private.h"
#include "bhnd_nvram_valuevar.h"

#ifdef _KERNEL
#define	bhnd_nv_hex2ascii(hex)	hex2ascii(hex)
#else /* !_KERNEL */
static char const bhnd_nv_hex2ascii[] = "0123456789abcdefghijklmnopqrstuvwxyz";
#define	bhnd_nv_hex2ascii(hex)		(bhnd_nv_hex2ascii[hex])
#endif /* _KERNEL */

/**
 * Maximum size, in bytes, of a string-encoded NVRAM integer value, not
 * including any prefix (0x, 0, etc).
 * 
 * We assume the largest possible encoding is the base-2 representation
 * of a 64-bit integer.
 */
#define NV_NUMSTR_MAX	((sizeof(uint64_t) * CHAR_BIT) + 1)

/**
 * Format a string representation of @p value using @p fmt, with, writing the
 * result to @p outp.
 *
 * @param		value	The value to be formatted.
 * @param		fmt	The format string.
 * @param[out]		outp	On success, the string will be written to this 
 *				buffer. This argment may be NULL if the value is
 *				not desired.
 * @param[in,out]	olen	The capacity of @p outp. On success, will be set
 *				to the actual number of bytes required for the
 *				requested string encoding (including a trailing
 *				NUL).
 * 
 * Refer to bhnd_nvram_val_vprintf() for full format string documentation.
 *
 * @retval 0		success
 * @retval EINVAL	If @p fmt contains unrecognized format string
 *			specifiers.
 * @retval ENOMEM	If the @p outp is non-NULL, and the provided @p olen
 *			is too small to hold the encoded value.
 * @retval EFTYPE	If value coercion from @p value to a single string
 *			value via @p fmt is unsupported.
 * @retval ERANGE	If value coercion of @p value would overflow (or
 *			underflow) the representation defined by @p fmt.
 */
int
bhnd_nvram_val_printf(bhnd_nvram_val *value, const char *fmt, char *outp,
    size_t *olen, ...)
{
	va_list	ap;
	int	error;

	va_start(ap, olen);
	error = bhnd_nvram_val_vprintf(value, fmt, outp, olen, ap);
	va_end(ap);

	return (error);
}


/**
 * Format a string representation of the elements of @p value using @p fmt,
 * writing the result to @p outp.
 *
 * @param		value	The value to be formatted.
 * @param		fmt	The format string.
 * @param[out]		outp	On success, the string will be written to this 
 *				buffer. This argment may be NULL if the value is
 *				not desired.
 * @param[in,out]	olen	The capacity of @p outp. On success, will be set
 *				to the actual number of bytes required for the
 *				requested string encoding (including a trailing
 *				NUL).
 * @param		ap	Argument list.
 *
 * @par Format Strings
 * 
 * Value format strings are similar, but not identical to, those used
 * by printf(3).
 * 
 * Format specifier format:
 *     %[repeat][flags][width][.precision][length modifier][specifier]
 *
 * The format specifier is interpreted as an encoding directive for an
 * individual value element; each format specifier will fetch the next element
 * from the value, encode the element as the appropriate type based on the
 * length modifiers and specifier, and then format the result as a string.
 * 
 * For example, given a string value of '0x000F', and a format specifier of
 * '%#hhx', the value will be asked to encode its first element as
 * BHND_NVRAM_TYPE_UINT8. String formatting will then be applied to the 8-bit
 * unsigned integer representation, producing a string value of "0xF".
 * 
 * Repeat:
 * - [digits]		Repeatedly apply the format specifier to the input
 *			value's elements up to `digits` times. The delimiter
 *			must be passed as a string in the next variadic
 *			argument.
 * - []			Repeatedly apply the format specifier to the input
 *			value's elements until all elements have been. The
 *			processed. The delimiter must be passed as a string in
 *			the next variadic argument.
 * - [*]		Repeatedly apply the format specifier to the input
 *			value's elements. The repeat count is read from the
 *			next variadic argument as a size_t value
 * 
 * Flags:
 * - '#'		use alternative form (e.g. 0x/0X prefixing of hex
 *			strings).
 * - '0'		zero padding
 * - '-'		left adjust padding
 * - '+'		include a sign character
 * - ' '		include a space in place of a sign character for
 *			positive numbers.
 * 
 * Width/Precision:
 * - digits		minimum field width.
 * - *			read the minimum field width from the next variadic
 *			argument as a ssize_t value. A negative value enables
 *			left adjustment.
 * - .digits		field precision.
 * - .*			read the field precision from the next variadic argument
 *			as a ssize_t value. A negative value enables left
 *			adjustment.
 *
 * Length Modifiers:
 * - 'hh', 'I8'		Convert the value to an 8-bit signed or unsigned
 *			integer.
 * - 'h', 'I16'		Convert the value to an 16-bit signed or unsigned
 *			integer.
 * - 'l', 'I32'		Convert the value to an 32-bit signed or unsigned
 *			integer.
 * - 'll', 'j', 'I64'	Convert the value to an 64-bit signed or unsigned
 *			integer.
 * 
 * Data Specifiers:
 * - 'd', 'i'		Convert and format as a signed decimal integer.
 * - 'u'		Convert and format as an unsigned decimal integer.
 * - 'o'		Convert and format as an unsigned octal integer.
 * - 'x'		Convert and format as an unsigned hexadecimal integer,
 *			using lowercase hex digits.
 * - 'X'		Convert and format as an unsigned hexadecimal integer,
 *			using uppercase hex digits.
 * - 's'		Convert and format as a string.
 * - '%'		Print a literal '%' character.
 *
 * @retval 0		success
 * @retval EINVAL	If @p fmt contains unrecognized format string
 *			specifiers.
 * @retval ENOMEM	If the @p outp is non-NULL, and the provided @p olen
 *			is too small to hold the encoded value.
 * @retval EFTYPE	If value coercion from @p value to a single string
 *			value via @p fmt is unsupported.
 * @retval ERANGE	If value coercion of @p value would overflow (or
 *			underflow) the representation defined by @p fmt.
 */
int
bhnd_nvram_val_vprintf(bhnd_nvram_val *value, const char *fmt, char *outp,
    size_t *olen, va_list ap)
{
	const void	*elem;
	size_t		 elen;
	size_t		 limit, nbytes;
	int		 error;

	elem = NULL;

	/* Determine output byte limit */
	nbytes = 0;
	if (outp != NULL)
		limit = *olen;
	else
		limit = 0;

#define	WRITE_CHAR(_c)	do {			\
	if (limit > nbytes)			\
		*(outp + nbytes) = _c;		\
						\
	if (nbytes == SIZE_MAX)			\
		return (EFTYPE);		\
	nbytes++;				\
} while (0)

	/* Encode string value as per the format string */
	for (const char *p = fmt; *p != '\0'; p++) {
		const char	*delim;
		size_t		 precision, width, delim_len;
		u_long		 repeat, bits;
		bool		 alt_form, ladjust, have_precision;
		char		 padc, signc, lenc;

		padc = ' ';
		signc = '\0';
		lenc = '\0';
		delim = "";
		delim_len = 0;

		ladjust = false;
		alt_form = false;

		have_precision = false;
		precision = 1;
		bits = 32;
		width = 0;
		repeat = 1;

		/* Copy all input to output until we hit a format specifier */
		if (*p != '%') {
			WRITE_CHAR(*p);
			continue;
		}

		/* Hit '%' -- is this followed by an escaped '%' literal? */
		p++;
		if (*p == '%') {
			WRITE_CHAR('%');
			p++;
			continue;
		}

		/* Parse repeat specifier */
		if (*p == '[') {
			p++;
			
			/* Determine repeat count */
			if (*p == ']') {
				/* Repeat consumes all input */
				repeat = bhnd_nvram_val_nelem(value);
			} else if (*p == '*') {
				/* Repeat is supplied as an argument */
				repeat = va_arg(ap, size_t);
				p++;
			} else {
				char *endp;

				/* Repeat specified as argument */
				repeat = strtoul(p, &endp, 10);
				if (p == endp) {
					BHND_NV_LOG("error parsing repeat "
						    "count at '%s'", p);
					return (EINVAL);
				}
				
				/* Advance past repeat count */
				p = endp;
			}

			/* Advance past terminating ']' */
			if (*p != ']') {
				BHND_NV_LOG("error parsing repeat count at "
				    "'%s'", p);
				return (EINVAL);
			}
			p++;

			delim = va_arg(ap, const char *);
			delim_len = strlen(delim);
		}

		/* Parse flags */
		while (*p != '\0') {
			const char	*np;
			bool		 stop;

			stop = false;
			np = p+1;
	
			switch (*p) {
			case '#':
				alt_form = true;
				break;
			case '0':
				padc = '0';
				break;
			case '-':
				ladjust = true;
				break;
			case ' ':
				/* Must not override '+' */
				if (signc != '+')
					signc = ' ';
				break;
			case '+':
				signc = '+';
				break;
			default:
				/* Non-flag character */
				stop = true;
				break;
			}

			if (stop)
				break;
			else
				p = np;
		}

		/* Parse minimum width */
		if (*p == '*') {
			ssize_t arg;

			/* Width is supplied as an argument */
			arg = va_arg(ap, int);

			/* Negative width argument is interpreted as
			 * '-' flag followed by positive width */
			if (arg < 0) {
				ladjust = true;
				arg = -arg;
			}

			width = arg;
			p++;
		} else if (bhnd_nv_isdigit(*p)) {
			uint32_t	v;
			size_t		len, parsed;

			/* Parse width value */
			len = sizeof(v);
			error = bhnd_nvram_parse_int(p, strlen(p), 10, &parsed,
			    &v, &len, BHND_NVRAM_TYPE_UINT32);
			if (error) {
				BHND_NV_LOG("error parsing width %s: %d\n", p,
				    error);
				return (EINVAL);
			}

			/* Save width and advance input */
			width = v;
			p += parsed;
		}

		/* Parse precision */
		if (*p == '.') {
			uint32_t	v;
			size_t		len, parsed;

			p++;
			have_precision = true;

			if (*p == '*') {
				ssize_t arg;

				/* Precision is specified as an argument */
				arg = va_arg(ap, int);

				/* Negative precision argument is interpreted
				 * as '-' flag followed by positive
				 * precision */
				if (arg < 0) {
					ladjust = true;
					arg = -arg;
				}

				precision = arg;
			} else if (!bhnd_nv_isdigit(*p)) {
				/* Implicit precision of 0 */
				precision = 0;
			} else {
				/* Parse precision value */
				len = sizeof(v);
				error = bhnd_nvram_parse_int(p, strlen(p), 10,
				    &parsed, &v, &len,
				    BHND_NVRAM_TYPE_UINT32);
				if (error) {
					BHND_NV_LOG("error parsing width %s: "
					    "%d\n", p, error);
					return (EINVAL);
				}

				/* Save precision and advance input */
				precision = v;
				p += parsed;
			}
		}

		/* Parse length modifiers */
		while (*p != '\0') {
			const char	*np;
			bool		 stop;
			
			stop = false;
			np = p+1;

			switch (*p) {
			case 'h':
				if (lenc == '\0') {
					/* Set initial length value */
					lenc = *p;
					bits = 16;
				} else if (lenc == *p && bits == 16) {
					/* Modify previous length value */
					bits = 8;
				} else {
					BHND_NV_LOG("invalid length modifier "
					    "%c\n", *p);
					return (EINVAL);
				}
				break;

			case 'l':
				if (lenc == '\0') {
					/* Set initial length value */
					lenc = *p;
					bits = 32;
				} else if (lenc == *p && bits == 32) {
					/* Modify previous length value */
					bits = 64;
				} else {
					BHND_NV_LOG("invalid length modifier "
					    "%c\n", *p);
					return (EINVAL);
				}
				break;

			case 'j':
				/* Conflicts with all other length
				 * specifications, and may only occur once */
				if (lenc != '\0') {
					BHND_NV_LOG("invalid length modifier "
					    "%c\n", *p);
					return (EINVAL);
				}

				lenc = *p;
				bits = 64;
				break;

			case 'I': {
				char	*endp;

				/* Conflicts with all other length
				 * specifications, and may only occur once */
				if (lenc != '\0') {
					BHND_NV_LOG("invalid length modifier "
					    "%c\n", *p);
					return (EINVAL);
				}

				lenc = *p;

				/* Parse the length specifier value */
				p++;
				bits = strtoul(p, &endp, 10);
				if (p == endp) {
					BHND_NV_LOG("invalid size specifier: "
					    "%s\n", p);
					return (EINVAL);
				}

				/* Advance input past the parsed integer */
				np = endp;
				break;
			}
			default:
				/* Non-length modifier character */
				stop = true;
				break;
			}

			if (stop)
				break;
			else
				p = np;
		}

		/* Parse conversion specifier and format the value(s) */
		for (u_long n = 0; n < repeat; n++) {
			bhnd_nvram_type	arg_type;
			size_t		arg_size;
			size_t		i;
			u_long		base;
			bool		is_signed, is_upper;

			is_signed = false;
			is_upper = false;
			base = 0;

			/* Fetch next element */
			elem = bhnd_nvram_val_next(value, elem, &elen);
			if (elem == NULL) {
				BHND_NV_LOG("format string references more "
				    "than %zu available value elements\n",
				    bhnd_nvram_val_nelem(value));
				return (EINVAL);
			}

			/*
			 * If this is not the first value, append the delimiter.
			 */
			if (n > 0) {
				size_t nremain = 0;
				if (limit > nbytes)
					nremain = limit - nbytes;
	
				if (nremain >= delim_len)
					memcpy(outp + nbytes, delim, delim_len);

				/* Add delimiter length to the total byte count */
				if (SIZE_MAX - nbytes < delim_len)
					return (EFTYPE); /* overflows size_t */

				nbytes += delim_len;
			}

			/* Parse integer conversion specifiers */
			switch (*p) {
			case 'd':
			case 'i':
				base = 10;
				is_signed = true;
				break;

			case 'u':
				base = 10;
				break;

			case 'o':
				base = 8;
				break;

			case 'x':
				base = 16;
				break;

			case 'X':
				base = 16;
				is_upper = true;
				break;
			}

			/* Format argument */
			switch (*p) {
#define	NV_ENCODE_INT(_width) do { 					\
	arg_type = (is_signed) ? BHND_NVRAM_TYPE_INT ## _width :	\
	    BHND_NVRAM_TYPE_UINT ## _width;				\
	arg_size = sizeof(v.u ## _width);				\
	error = bhnd_nvram_val_encode_elem(value, elem, elen,		\
	    &v.u ## _width, &arg_size, arg_type);			\
	if (error) {							\
		BHND_NV_LOG("error encoding argument as %s: %d\n",	\
		     bhnd_nvram_type_name(arg_type), error);		\
		return (error);						\
	}								\
									\
	if (is_signed) {						\
		if (v.i ## _width < 0) {				\
			add_neg = true;					\
			numval = (int64_t)-(v.i ## _width);		\
		} else {						\
			numval = (int64_t) (v.i ## _width);		\
		}							\
	} else {							\
		numval = v.u ## _width;					\
	}								\
} while(0)
			case 'd':
			case 'i':
			case 'u':
			case 'o':
			case 'x':
			case 'X': {
				char		 numbuf[NV_NUMSTR_MAX];
				char		*sptr;
				uint64_t	 numval;
				size_t		 slen;
				bool		 add_neg;
				union {
					uint8_t		u8;
					uint16_t	u16;
					uint32_t	u32;
					uint64_t	u64;
					int8_t		i8;
					int16_t		i16;
					int32_t		i32;
					int64_t		i64;
				} v;

				add_neg = false;

				/* If precision is specified, it overrides
				 * (and behaves identically) to a zero-prefixed
				 * minimum width */
				if (have_precision) {
					padc = '0';
					width = precision;
					ladjust = false;
				}

				/* If zero-padding is used, value must be right
				 * adjusted */
				if (padc == '0')
					ladjust = false;

				/* Request encode to the appropriate integer
				 * type, and then promote to common 64-bit
				 * representation */
				switch (bits) {
				case 8:
					NV_ENCODE_INT(8);
					break;
				case 16:
					NV_ENCODE_INT(16);
					break;
				case 32:
					NV_ENCODE_INT(32);
					break;
				case 64:
					NV_ENCODE_INT(64);
					break;
				default:
					BHND_NV_LOG("invalid length specifier: "
					    "%lu\n", bits);
					return (EINVAL);
				}
#undef	NV_ENCODE_INT

				/* If a precision of 0 is specified and the
				 * value is also zero, no characters should
				 * be produced */
				if (have_precision && precision == 0 &&
				    numval == 0)
				{
					break;
				}

				/* Emit string representation to local buffer */
				BHND_NV_ASSERT(base <= 16, ("invalid base"));
				sptr = numbuf + nitems(numbuf) - 1;
				for (slen = 0; slen < sizeof(numbuf); slen++) {
					char		c;
					uint64_t	n;

					n = numval % base;
					c = bhnd_nv_hex2ascii(n);
					if (is_upper)
						c = bhnd_nv_toupper(c);

					sptr--;
					*sptr = c;

					numval /= (uint64_t)base;
					if (numval == 0) {
						slen++;
						break;
					}
				}

				arg_size = slen;

				/* Reserve space for 0/0x prefix? */
				if (alt_form) {
					if (numval == 0) {
						/* If 0, no prefix */
						alt_form = false;
					} else if (base == 8) {
						arg_size += 1; /* 0 */
					} else if (base == 16) {
						arg_size += 2; /* 0x/0X */
					}
				}

				/* Reserve space for ' ', '+', or '-' prefix? */
				if (add_neg || signc != '\0') {
					if (add_neg)
						signc = '-';

					arg_size++;
				}

				/* Right adjust (if using spaces) */
				if (!ladjust && padc != '0') {
					for (i = arg_size;  i < width; i++)
						WRITE_CHAR(padc);
				}

				if (signc != '\0')
					WRITE_CHAR(signc);

				if (alt_form) {
					if (base == 8) {
						WRITE_CHAR('0');
					} else if (base == 16) {
						WRITE_CHAR('0');
						if (is_upper)
							WRITE_CHAR('X');
						else
							WRITE_CHAR('x');
					}
				}

				/* Right adjust (if using zeros) */
				if (!ladjust && padc == '0') {
					for (i = slen;  i < width; i++)
						WRITE_CHAR(padc);
				}

				/* Write the string to our output buffer */
				if (limit > nbytes && limit - nbytes >= slen)
					memcpy(outp + nbytes, sptr, slen);

				/* Update the total byte count */
				if (SIZE_MAX - nbytes < arg_size)
					return (EFTYPE); /* overflows size_t */

				nbytes += arg_size;

				/* Left adjust */
				for (i = arg_size; ladjust && i < width; i++)
					WRITE_CHAR(padc);

				break;
			}

			case 's': {
				char	*s;
				size_t	 slen;

				/* Query the total length of the element when
				 * converted to a string */
				arg_type = BHND_NVRAM_TYPE_STRING;
				error = bhnd_nvram_val_encode_elem(value, elem,
				    elen, NULL, &arg_size, arg_type);
				if (error) {
					BHND_NV_LOG("error encoding argument "
					    "as %s: %d\n",
					    bhnd_nvram_type_name(arg_type),
					    error);
					return (error);
				}

				/* Do not include trailing NUL in the string
				 * length */
				if (arg_size > 0)
					arg_size--;

				/* Right adjust */
				for (i = arg_size; !ladjust && i < width; i++)
					WRITE_CHAR(padc);

				/* Determine output positition and remaining
				 * buffer space */
				if (limit > nbytes) {
					s = outp + nbytes;
					slen = limit - nbytes;
				} else {
					s = NULL;
					slen = 0;
				}

				/* Encode the string to our output buffer */
				error = bhnd_nvram_val_encode_elem(value, elem,
				    elen, s, &slen, arg_type);
				if (error && error != ENOMEM) {
					BHND_NV_LOG("error encoding argument "
					    "as %s: %d\n",
					    bhnd_nvram_type_name(arg_type),
					    error);
					return (error);
				}

				/* Update the total byte count */
				if (SIZE_MAX - nbytes < arg_size)
					return (EFTYPE); /* overflows size_t */

				nbytes += arg_size;

				/* Left adjust */
				for (i = arg_size; ladjust && i < width; i++)
					WRITE_CHAR(padc);

				break;
			}

			case 'c': {
				char c;

				arg_type = BHND_NVRAM_TYPE_CHAR;
				arg_size = bhnd_nvram_type_width(arg_type);

				/* Encode as single character */
				error = bhnd_nvram_val_encode_elem(value, elem,
				    elen, &c, &arg_size, arg_type);
				if (error) {
					BHND_NV_LOG("error encoding argument "
					    "as %s: %d\n",
					    bhnd_nvram_type_name(arg_type),
					    error);
					return (error);
				}

				BHND_NV_ASSERT(arg_size == sizeof(c),
				    ("invalid encoded size"));

				/* Right adjust */
				for (i = arg_size; !ladjust && i < width; i++)
					WRITE_CHAR(padc);

				WRITE_CHAR(padc);

				/* Left adjust */
				for (i = arg_size; ladjust && i < width; i++)
					WRITE_CHAR(padc);

				break;
			}
			}
		}
	}

	/* Append terminating NUL */
	if (limit > nbytes)
		*(outp + nbytes) = '\0';

	if (nbytes < SIZE_MAX)
		nbytes++;
	else
		return (EFTYPE);

	/* Report required space */
	*olen = nbytes;
	if (limit < nbytes) {
		if (outp != NULL)
			return (ENOMEM);
	}

	return (0);
}
