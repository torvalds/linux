/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */




/*
 * UTF-8 text preparation functions (PSARC/2007/149, PSARC/2007/458).
 *
 * Man pages: u8_textprep_open(9F), u8_textprep_buf(9F), u8_textprep_close(9F),
 * u8_textprep_str(9F), u8_strcmp(9F), and u8_validate(9F). See also
 * the section 3C man pages.
 * Interface stability: Committed.
 */

#include <sys/types.h>
#ifdef	_KERNEL
#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/debug.h>
#include <sys/kmem.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#else
#include <sys/u8_textprep.h>
#include <strings.h>
#endif	/* _KERNEL */
#include <sys/byteorder.h>
#include <sys/errno.h>
#include <sys/u8_textprep_data.h>


/* The maximum possible number of bytes in a UTF-8 character. */
#define	U8_MB_CUR_MAX			(4)

/*
 * The maximum number of bytes needed for a UTF-8 character to cover
 * U+0000 - U+FFFF, i.e., the coding space of now deprecated UCS-2.
 */
#define	U8_MAX_BYTES_UCS2		(3)

/* The maximum possible number of bytes in a Stream-Safe Text. */
#define	U8_STREAM_SAFE_TEXT_MAX		(128)

/*
 * The maximum number of characters in a combining/conjoining sequence and
 * the actual upperbound limit of a combining/conjoining sequence.
 */
#define	U8_MAX_CHARS_A_SEQ		(32)
#define	U8_UPPER_LIMIT_IN_A_SEQ		(31)

/* The combining class value for Starter. */
#define	U8_COMBINING_CLASS_STARTER	(0)

/*
 * Some Hangul related macros at below.
 *
 * The first and the last of Hangul syllables, Hangul Jamo Leading consonants,
 * Vowels, and optional Trailing consonants in Unicode scalar values.
 *
 * Please be noted that the U8_HANGUL_JAMO_T_FIRST is 0x11A7 at below not
 * the actual U+11A8. This is due to that the trailing consonant is optional
 * and thus we are doing a pre-calculation of subtracting one.
 *
 * Each of 19 modern leading consonants has total 588 possible syllables since
 * Hangul has 21 modern vowels and 27 modern trailing consonants plus 1 for
 * no trailing consonant case, i.e., 21 x 28 = 588.
 *
 * We also have bunch of Hangul related macros at below. Please bear in mind
 * that the U8_HANGUL_JAMO_1ST_BYTE can be used to check whether it is
 * a Hangul Jamo or not but the value does not guarantee that it is a Hangul
 * Jamo; it just guarantee that it will be most likely.
 */
#define	U8_HANGUL_SYL_FIRST		(0xAC00U)
#define	U8_HANGUL_SYL_LAST		(0xD7A3U)

#define	U8_HANGUL_JAMO_L_FIRST		(0x1100U)
#define	U8_HANGUL_JAMO_L_LAST		(0x1112U)
#define	U8_HANGUL_JAMO_V_FIRST		(0x1161U)
#define	U8_HANGUL_JAMO_V_LAST		(0x1175U)
#define	U8_HANGUL_JAMO_T_FIRST		(0x11A7U)
#define	U8_HANGUL_JAMO_T_LAST		(0x11C2U)

#define	U8_HANGUL_V_COUNT		(21)
#define	U8_HANGUL_VT_COUNT		(588)
#define	U8_HANGUL_T_COUNT		(28)

#define	U8_HANGUL_JAMO_1ST_BYTE		(0xE1U)

#define	U8_SAVE_HANGUL_AS_UTF8(s, i, j, k, b) \
	(s)[(i)] = (uchar_t)(0xE0U | ((uint32_t)(b) & 0xF000U) >> 12); \
	(s)[(j)] = (uchar_t)(0x80U | ((uint32_t)(b) & 0x0FC0U) >> 6); \
	(s)[(k)] = (uchar_t)(0x80U | ((uint32_t)(b) & 0x003FU));

#define	U8_HANGUL_JAMO_L(u) \
	((u) >= U8_HANGUL_JAMO_L_FIRST && (u) <= U8_HANGUL_JAMO_L_LAST)

#define	U8_HANGUL_JAMO_V(u) \
	((u) >= U8_HANGUL_JAMO_V_FIRST && (u) <= U8_HANGUL_JAMO_V_LAST)

#define	U8_HANGUL_JAMO_T(u) \
	((u) > U8_HANGUL_JAMO_T_FIRST && (u) <= U8_HANGUL_JAMO_T_LAST)

#define	U8_HANGUL_JAMO(u) \
	((u) >= U8_HANGUL_JAMO_L_FIRST && (u) <= U8_HANGUL_JAMO_T_LAST)

#define	U8_HANGUL_SYLLABLE(u) \
	((u) >= U8_HANGUL_SYL_FIRST && (u) <= U8_HANGUL_SYL_LAST)

#define	U8_HANGUL_COMPOSABLE_L_V(s, u) \
	((s) == U8_STATE_HANGUL_L && U8_HANGUL_JAMO_V((u)))

#define	U8_HANGUL_COMPOSABLE_LV_T(s, u) \
	((s) == U8_STATE_HANGUL_LV && U8_HANGUL_JAMO_T((u)))

/* The types of decomposition mappings. */
#define	U8_DECOMP_BOTH			(0xF5U)
#define	U8_DECOMP_CANONICAL		(0xF6U)

/* The indicator for 16-bit table. */
#define	U8_16BIT_TABLE_INDICATOR	(0x8000U)

/* The following are some convenience macros. */
#define	U8_PUT_3BYTES_INTO_UTF32(u, b1, b2, b3)  \
	(u) = ((((uint32_t)(b1) & 0x0F) << 12) | \
		(((uint32_t)(b2) & 0x3F) << 6)  | \
		((uint32_t)(b3) & 0x3F));

#define	U8_SIMPLE_SWAP(a, b, t) \
	(t) = (a); \
	(a) = (b); \
	(b) = (t);

#define	U8_ASCII_TOUPPER(c) \
	(((c) >= 'a' && (c) <= 'z') ? (c) - 'a' + 'A' : (c))

#define	U8_ASCII_TOLOWER(c) \
	(((c) >= 'A' && (c) <= 'Z') ? (c) - 'A' + 'a' : (c))

#define	U8_ISASCII(c)			(((uchar_t)(c)) < 0x80U)
/*
 * The following macro assumes that the two characters that are to be
 * swapped are adjacent to each other and 'a' comes before 'b'.
 *
 * If the assumptions are not met, then, the macro will fail.
 */
#define	U8_SWAP_COMB_MARKS(a, b) \
	for (k = 0; k < disp[(a)]; k++) \
		u8t[k] = u8s[start[(a)] + k]; \
	for (k = 0; k < disp[(b)]; k++) \
		u8s[start[(a)] + k] = u8s[start[(b)] + k]; \
	start[(b)] = start[(a)] + disp[(b)]; \
	for (k = 0; k < disp[(a)]; k++) \
		u8s[start[(b)] + k] = u8t[k]; \
	U8_SIMPLE_SWAP(comb_class[(a)], comb_class[(b)], tc); \
	U8_SIMPLE_SWAP(disp[(a)], disp[(b)], tc);

/* The possible states during normalization. */
typedef enum {
	U8_STATE_START = 0,
	U8_STATE_HANGUL_L = 1,
	U8_STATE_HANGUL_LV = 2,
	U8_STATE_HANGUL_LVT = 3,
	U8_STATE_HANGUL_V = 4,
	U8_STATE_HANGUL_T = 5,
	U8_STATE_COMBINING_MARK = 6
} u8_normalization_states_t;

/*
 * The three vectors at below are used to check bytes of a given UTF-8
 * character are valid and not containing any malformed byte values.
 *
 * We used to have a quite relaxed UTF-8 binary representation but then there
 * was some security related issues and so the Unicode Consortium defined
 * and announced the UTF-8 Corrigendum at Unicode 3.1 and then refined it
 * one more time at the Unicode 3.2. The following three tables are based on
 * that.
 */

#define	U8_ILLEGAL_NEXT_BYTE_COMMON(c)	((c) < 0x80 || (c) > 0xBF)

#define	I_				U8_ILLEGAL_CHAR
#define	O_				U8_OUT_OF_RANGE_CHAR

const int8_t u8_number_of_bytes[0x100] = {
	1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
	1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
	1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
	1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
	1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
	1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
	1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
	1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,

/*	80  81  82  83  84  85  86  87  88  89  8A  8B  8C  8D  8E  8F  */
	I_, I_, I_, I_, I_, I_, I_, I_, I_, I_, I_, I_, I_, I_, I_, I_,

/*  	90  91  92  93  94  95  96  97  98  99  9A  9B  9C  9D  9E  9F  */
	I_, I_, I_, I_, I_, I_, I_, I_, I_, I_, I_, I_, I_, I_, I_, I_,

/*  	A0  A1  A2  A3  A4  A5  A6  A7  A8  A9  AA  AB  AC  AD  AE  AF  */
	I_, I_, I_, I_, I_, I_, I_, I_, I_, I_, I_, I_, I_, I_, I_, I_,

/*	B0  B1  B2  B3  B4  B5  B6  B7  B8  B9  BA  BB  BC  BD  BE  BF  */
	I_, I_, I_, I_, I_, I_, I_, I_, I_, I_, I_, I_, I_, I_, I_, I_,

/*	C0  C1  C2  C3  C4  C5  C6  C7  C8  C9  CA  CB  CC  CD  CE  CF  */
	I_, I_, 2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,

/*	D0  D1  D2  D3  D4  D5  D6  D7  D8  D9  DA  DB  DC  DD  DE  DF  */
	2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,

/*	E0  E1  E2  E3  E4  E5  E6  E7  E8  E9  EA  EB  EC  ED  EE  EF  */
	3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,

/*	F0  F1  F2  F3  F4  F5  F6  F7  F8  F9  FA  FB  FC  FD  FE  FF  */
	4,  4,  4,  4,  4,  O_, O_, O_, O_, O_, O_, O_, O_, O_, O_, O_,
};

#undef	I_
#undef	O_

const uint8_t u8_valid_min_2nd_byte[0x100] = {
	0,    0,    0,    0,    0,    0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0,
/*	C0    C1    C2    C3    C4    C5    C6    C7    */
	0,    0,    0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
/*	C8    C9    CA    CB    CC    CD    CE    CF    */
	0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
/*	D0    D1    D2    D3    D4    D5    D6    D7    */
	0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
/*	D8    D9    DA    DB    DC    DD    DE    DF    */
	0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
/*	E0    E1    E2    E3    E4    E5    E6    E7    */
	0xa0, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
/*	E8    E9    EA    EB    EC    ED    EE    EF    */
	0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
/*	F0    F1    F2    F3    F4    F5    F6    F7    */
	0x90, 0x80, 0x80, 0x80, 0x80, 0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0,
};

const uint8_t u8_valid_max_2nd_byte[0x100] = {
	0,    0,    0,    0,    0,    0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0,
/*	C0    C1    C2    C3    C4    C5    C6    C7    */
	0,    0,    0xbf, 0xbf, 0xbf, 0xbf, 0xbf, 0xbf,
/*	C8    C9    CA    CB    CC    CD    CE    CF    */
	0xbf, 0xbf, 0xbf, 0xbf, 0xbf, 0xbf, 0xbf, 0xbf,
/*	D0    D1    D2    D3    D4    D5    D6    D7    */
	0xbf, 0xbf, 0xbf, 0xbf, 0xbf, 0xbf, 0xbf, 0xbf,
/*	D8    D9    DA    DB    DC    DD    DE    DF    */
	0xbf, 0xbf, 0xbf, 0xbf, 0xbf, 0xbf, 0xbf, 0xbf,
/*	E0    E1    E2    E3    E4    E5    E6    E7    */
	0xbf, 0xbf, 0xbf, 0xbf, 0xbf, 0xbf, 0xbf, 0xbf,
/*	E8    E9    EA    EB    EC    ED    EE    EF    */
	0xbf, 0xbf, 0xbf, 0xbf, 0xbf, 0x9f, 0xbf, 0xbf,
/*	F0    F1    F2    F3    F4    F5    F6    F7    */
	0xbf, 0xbf, 0xbf, 0xbf, 0x8f, 0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0,
};


/*
 * The u8_validate() validates on the given UTF-8 character string and
 * calculate the byte length. It is quite similar to mblen(3C) except that
 * this will validate against the list of characters if required and
 * specific to UTF-8 and Unicode.
 */
int
u8_validate(char *u8str, size_t n, char **list, int flag, int *errnum)
{
	uchar_t *ib;
	uchar_t *ibtail;
	uchar_t **p;
	uchar_t *s1;
	uchar_t *s2;
	uchar_t f;
	int sz;
	size_t i;
	int ret_val;
	boolean_t second;
	boolean_t no_need_to_validate_entire;
	boolean_t check_additional;
	boolean_t validate_ucs2_range_only;

	if (! u8str)
		return (0);

	ib = (uchar_t *)u8str;
	ibtail = ib + n;

	ret_val = 0;

	no_need_to_validate_entire = ! (flag & U8_VALIDATE_ENTIRE);
	check_additional = flag & U8_VALIDATE_CHECK_ADDITIONAL;
	validate_ucs2_range_only = flag & U8_VALIDATE_UCS2_RANGE;

	while (ib < ibtail) {
		/*
		 * The first byte of a UTF-8 character tells how many
		 * bytes will follow for the character. If the first byte
		 * is an illegal byte value or out of range value, we just
		 * return -1 with an appropriate error number.
		 */
		sz = u8_number_of_bytes[*ib];
		if (sz == U8_ILLEGAL_CHAR) {
			*errnum = EILSEQ;
			return (-1);
		}

		if (sz == U8_OUT_OF_RANGE_CHAR ||
		    (validate_ucs2_range_only && sz > U8_MAX_BYTES_UCS2)) {
			*errnum = ERANGE;
			return (-1);
		}

		/*
		 * If we don't have enough bytes to check on, that's also
		 * an error. As you can see, we give illegal byte sequence
		 * checking higher priority then EINVAL cases.
		 */
		if ((ibtail - ib) < sz) {
			*errnum = EINVAL;
			return (-1);
		}

		if (sz == 1) {
			ib++;
			ret_val++;
		} else {
			/*
			 * Check on the multi-byte UTF-8 character. For more
			 * details on this, see comment added for the used
			 * data structures at the beginning of the file.
			 */
			f = *ib++;
			ret_val++;
			second = B_TRUE;
			for (i = 1; i < sz; i++) {
				if (second) {
					if (*ib < u8_valid_min_2nd_byte[f] ||
					    *ib > u8_valid_max_2nd_byte[f]) {
						*errnum = EILSEQ;
						return (-1);
					}
					second = B_FALSE;
				} else if (U8_ILLEGAL_NEXT_BYTE_COMMON(*ib)) {
					*errnum = EILSEQ;
					return (-1);
				}
				ib++;
				ret_val++;
			}
		}

		if (check_additional) {
			for (p = (uchar_t **)list, i = 0; p[i]; i++) {
				s1 = ib - sz;
				s2 = p[i];
				while (s1 < ib) {
					if (*s1 != *s2 || *s2 == '\0')
						break;
					s1++;
					s2++;
				}

				if (s1 >= ib && *s2 == '\0') {
					*errnum = EBADF;
					return (-1);
				}
			}
		}

		if (no_need_to_validate_entire)
			break;
	}

	return (ret_val);
}

/*
 * The do_case_conv() looks at the mapping tables and returns found
 * bytes if any. If not found, the input bytes are returned. The function
 * always terminate the return bytes with a null character assuming that
 * there are plenty of room to do so.
 *
 * The case conversions are simple case conversions mapping a character to
 * another character as specified in the Unicode data. The byte size of
 * the mapped character could be different from that of the input character.
 *
 * The return value is the byte length of the returned character excluding
 * the terminating null byte.
 */
static size_t
do_case_conv(int uv, uchar_t *u8s, uchar_t *s, int sz, boolean_t is_it_toupper)
{
	size_t i;
	uint16_t b1 = 0;
	uint16_t b2 = 0;
	uint16_t b3 = 0;
	uint16_t b3_tbl;
	uint16_t b3_base;
	uint16_t b4 = 0;
	size_t start_id;
	size_t end_id;

	/*
	 * At this point, the only possible values for sz are 2, 3, and 4.
	 * The u8s should point to a vector that is well beyond the size of
	 * 5 bytes.
	 */
	if (sz == 2) {
		b3 = u8s[0] = s[0];
		b4 = u8s[1] = s[1];
	} else if (sz == 3) {
		b2 = u8s[0] = s[0];
		b3 = u8s[1] = s[1];
		b4 = u8s[2] = s[2];
	} else if (sz == 4) {
		b1 = u8s[0] = s[0];
		b2 = u8s[1] = s[1];
		b3 = u8s[2] = s[2];
		b4 = u8s[3] = s[3];
	} else {
		/* This is not possible but just in case as a fallback. */
		if (is_it_toupper)
			*u8s = U8_ASCII_TOUPPER(*s);
		else
			*u8s = U8_ASCII_TOLOWER(*s);
		u8s[1] = '\0';

		return (1);
	}
	u8s[sz] = '\0';

	/*
	 * Let's find out if we have a corresponding character.
	 */
	b1 = u8_common_b1_tbl[uv][b1];
	if (b1 == U8_TBL_ELEMENT_NOT_DEF)
		return ((size_t)sz);

	b2 = u8_case_common_b2_tbl[uv][b1][b2];
	if (b2 == U8_TBL_ELEMENT_NOT_DEF)
		return ((size_t)sz);

	if (is_it_toupper) {
		b3_tbl = u8_toupper_b3_tbl[uv][b2][b3].tbl_id;
		if (b3_tbl == U8_TBL_ELEMENT_NOT_DEF)
			return ((size_t)sz);

		start_id = u8_toupper_b4_tbl[uv][b3_tbl][b4];
		end_id = u8_toupper_b4_tbl[uv][b3_tbl][b4 + 1];

		/* Either there is no match or an error at the table. */
		if (start_id >= end_id || (end_id - start_id) > U8_MB_CUR_MAX)
			return ((size_t)sz);

		b3_base = u8_toupper_b3_tbl[uv][b2][b3].base;

		for (i = 0; start_id < end_id; start_id++)
			u8s[i++] = u8_toupper_final_tbl[uv][b3_base + start_id];
	} else {
		b3_tbl = u8_tolower_b3_tbl[uv][b2][b3].tbl_id;
		if (b3_tbl == U8_TBL_ELEMENT_NOT_DEF)
			return ((size_t)sz);

		start_id = u8_tolower_b4_tbl[uv][b3_tbl][b4];
		end_id = u8_tolower_b4_tbl[uv][b3_tbl][b4 + 1];

		if (start_id >= end_id || (end_id - start_id) > U8_MB_CUR_MAX)
			return ((size_t)sz);

		b3_base = u8_tolower_b3_tbl[uv][b2][b3].base;

		for (i = 0; start_id < end_id; start_id++)
			u8s[i++] = u8_tolower_final_tbl[uv][b3_base + start_id];
	}

	/*
	 * If i is still zero, that means there is no corresponding character.
	 */
	if (i == 0)
		return ((size_t)sz);

	u8s[i] = '\0';

	return (i);
}

/*
 * The do_case_compare() function compares the two input strings, s1 and s2,
 * one character at a time doing case conversions if applicable and return
 * the comparison result as like strcmp().
 *
 * Since, in empirical sense, most of text data are 7-bit ASCII characters,
 * we treat the 7-bit ASCII characters as a special case trying to yield
 * faster processing time.
 */
static int
do_case_compare(size_t uv, uchar_t *s1, uchar_t *s2, size_t n1,
    size_t n2, boolean_t is_it_toupper, int *errnum)
{
	int f;
	int sz1;
	int sz2;
	size_t j;
	size_t i1;
	size_t i2;
	uchar_t u8s1[U8_MB_CUR_MAX + 1];
	uchar_t u8s2[U8_MB_CUR_MAX + 1];

	i1 = i2 = 0;
	while (i1 < n1 && i2 < n2) {
		/*
		 * Find out what would be the byte length for this UTF-8
		 * character at string s1 and also find out if this is
		 * an illegal start byte or not and if so, issue a proper
		 * error number and yet treat this byte as a character.
		 */
		sz1 = u8_number_of_bytes[*s1];
		if (sz1 < 0) {
			*errnum = EILSEQ;
			sz1 = 1;
		}

		/*
		 * For 7-bit ASCII characters mainly, we do a quick case
		 * conversion right at here.
		 *
		 * If we don't have enough bytes for this character, issue
		 * an EINVAL error and use what are available.
		 *
		 * If we have enough bytes, find out if there is
		 * a corresponding uppercase character and if so, copy over
		 * the bytes for a comparison later. If there is no
		 * corresponding uppercase character, then, use what we have
		 * for the comparison.
		 */
		if (sz1 == 1) {
			if (is_it_toupper)
				u8s1[0] = U8_ASCII_TOUPPER(*s1);
			else
				u8s1[0] = U8_ASCII_TOLOWER(*s1);
			s1++;
			u8s1[1] = '\0';
		} else if ((i1 + sz1) > n1) {
			*errnum = EINVAL;
			for (j = 0; (i1 + j) < n1; )
				u8s1[j++] = *s1++;
			u8s1[j] = '\0';
		} else {
			(void) do_case_conv(uv, u8s1, s1, sz1, is_it_toupper);
			s1 += sz1;
		}

		/* Do the same for the string s2. */
		sz2 = u8_number_of_bytes[*s2];
		if (sz2 < 0) {
			*errnum = EILSEQ;
			sz2 = 1;
		}

		if (sz2 == 1) {
			if (is_it_toupper)
				u8s2[0] = U8_ASCII_TOUPPER(*s2);
			else
				u8s2[0] = U8_ASCII_TOLOWER(*s2);
			s2++;
			u8s2[1] = '\0';
		} else if ((i2 + sz2) > n2) {
			*errnum = EINVAL;
			for (j = 0; (i2 + j) < n2; )
				u8s2[j++] = *s2++;
			u8s2[j] = '\0';
		} else {
			(void) do_case_conv(uv, u8s2, s2, sz2, is_it_toupper);
			s2 += sz2;
		}

		/* Now compare the two characters. */
		if (sz1 == 1 && sz2 == 1) {
			if (*u8s1 > *u8s2)
				return (1);
			if (*u8s1 < *u8s2)
				return (-1);
		} else {
			f = strcmp((const char *)u8s1, (const char *)u8s2);
			if (f != 0)
				return (f);
		}

		/*
		 * They were the same. Let's move on to the next
		 * characters then.
		 */
		i1 += sz1;
		i2 += sz2;
	}

	/*
	 * We compared until the end of either or both strings.
	 *
	 * If we reached to or went over the ends for the both, that means
	 * they are the same.
	 *
	 * If we reached only one of the two ends, that means the other string
	 * has something which then the fact can be used to determine
	 * the return value.
	 */
	if (i1 >= n1) {
		if (i2 >= n2)
			return (0);
		return (-1);
	}
	return (1);
}

/*
 * The combining_class() function checks on the given bytes and find out
 * the corresponding Unicode combining class value. The return value 0 means
 * it is a Starter. Any illegal UTF-8 character will also be treated as
 * a Starter.
 */
static uchar_t
combining_class(size_t uv, uchar_t *s, size_t sz)
{
	uint16_t b1 = 0;
	uint16_t b2 = 0;
	uint16_t b3 = 0;
	uint16_t b4 = 0;

	if (sz == 1 || sz > 4)
		return (0);

	if (sz == 2) {
		b3 = s[0];
		b4 = s[1];
	} else if (sz == 3) {
		b2 = s[0];
		b3 = s[1];
		b4 = s[2];
	} else if (sz == 4) {
		b1 = s[0];
		b2 = s[1];
		b3 = s[2];
		b4 = s[3];
	}

	b1 = u8_common_b1_tbl[uv][b1];
	if (b1 == U8_TBL_ELEMENT_NOT_DEF)
		return (0);

	b2 = u8_combining_class_b2_tbl[uv][b1][b2];
	if (b2 == U8_TBL_ELEMENT_NOT_DEF)
		return (0);

	b3 = u8_combining_class_b3_tbl[uv][b2][b3];
	if (b3 == U8_TBL_ELEMENT_NOT_DEF)
		return (0);

	return (u8_combining_class_b4_tbl[uv][b3][b4]);
}

/*
 * The do_decomp() function finds out a matching decomposition if any
 * and return. If there is no match, the input bytes are copied and returned.
 * The function also checks if there is a Hangul, decomposes it if necessary
 * and returns.
 *
 * To save time, a single byte 7-bit ASCII character should be handled by
 * the caller.
 *
 * The function returns the number of bytes returned sans always terminating
 * the null byte. It will also return a state that will tell if there was
 * a Hangul character decomposed which then will be used by the caller.
 */
static size_t
do_decomp(size_t uv, uchar_t *u8s, uchar_t *s, int sz,
    boolean_t canonical_decomposition, u8_normalization_states_t *state)
{
	uint16_t b1 = 0;
	uint16_t b2 = 0;
	uint16_t b3 = 0;
	uint16_t b3_tbl;
	uint16_t b3_base;
	uint16_t b4 = 0;
	size_t start_id;
	size_t end_id;
	size_t i;
	uint32_t u1;

	if (sz == 2) {
		b3 = u8s[0] = s[0];
		b4 = u8s[1] = s[1];
		u8s[2] = '\0';
	} else if (sz == 3) {
		/* Convert it to a Unicode scalar value. */
		U8_PUT_3BYTES_INTO_UTF32(u1, s[0], s[1], s[2]);

		/*
		 * If this is a Hangul syllable, we decompose it into
		 * a leading consonant, a vowel, and an optional trailing
		 * consonant and then return.
		 */
		if (U8_HANGUL_SYLLABLE(u1)) {
			u1 -= U8_HANGUL_SYL_FIRST;

			b1 = U8_HANGUL_JAMO_L_FIRST + u1 / U8_HANGUL_VT_COUNT;
			b2 = U8_HANGUL_JAMO_V_FIRST + (u1 % U8_HANGUL_VT_COUNT)
			    / U8_HANGUL_T_COUNT;
			b3 = u1 % U8_HANGUL_T_COUNT;

			U8_SAVE_HANGUL_AS_UTF8(u8s, 0, 1, 2, b1);
			U8_SAVE_HANGUL_AS_UTF8(u8s, 3, 4, 5, b2);
			if (b3) {
				b3 += U8_HANGUL_JAMO_T_FIRST;
				U8_SAVE_HANGUL_AS_UTF8(u8s, 6, 7, 8, b3);

				u8s[9] = '\0';
				*state = U8_STATE_HANGUL_LVT;
				return (9);
			}

			u8s[6] = '\0';
			*state = U8_STATE_HANGUL_LV;
			return (6);
		}

		b2 = u8s[0] = s[0];
		b3 = u8s[1] = s[1];
		b4 = u8s[2] = s[2];
		u8s[3] = '\0';

		/*
		 * If this is a Hangul Jamo, we know there is nothing
		 * further that we can decompose.
		 */
		if (U8_HANGUL_JAMO_L(u1)) {
			*state = U8_STATE_HANGUL_L;
			return (3);
		}

		if (U8_HANGUL_JAMO_V(u1)) {
			if (*state == U8_STATE_HANGUL_L)
				*state = U8_STATE_HANGUL_LV;
			else
				*state = U8_STATE_HANGUL_V;
			return (3);
		}

		if (U8_HANGUL_JAMO_T(u1)) {
			if (*state == U8_STATE_HANGUL_LV)
				*state = U8_STATE_HANGUL_LVT;
			else
				*state = U8_STATE_HANGUL_T;
			return (3);
		}
	} else if (sz == 4) {
		b1 = u8s[0] = s[0];
		b2 = u8s[1] = s[1];
		b3 = u8s[2] = s[2];
		b4 = u8s[3] = s[3];
		u8s[4] = '\0';
	} else {
		/*
		 * This is a fallback and should not happen if the function
		 * was called properly.
		 */
		u8s[0] = s[0];
		u8s[1] = '\0';
		*state = U8_STATE_START;
		return (1);
	}

	/*
	 * At this point, this routine does not know what it would get.
	 * The caller should sort it out if the state isn't a Hangul one.
	 */
	*state = U8_STATE_START;

	/* Try to find matching decomposition mapping byte sequence. */
	b1 = u8_common_b1_tbl[uv][b1];
	if (b1 == U8_TBL_ELEMENT_NOT_DEF)
		return ((size_t)sz);

	b2 = u8_decomp_b2_tbl[uv][b1][b2];
	if (b2 == U8_TBL_ELEMENT_NOT_DEF)
		return ((size_t)sz);

	b3_tbl = u8_decomp_b3_tbl[uv][b2][b3].tbl_id;
	if (b3_tbl == U8_TBL_ELEMENT_NOT_DEF)
		return ((size_t)sz);

	/*
	 * If b3_tbl is bigger than or equal to U8_16BIT_TABLE_INDICATOR
	 * which is 0x8000, this means we couldn't fit the mappings into
	 * the cardinality of a unsigned byte.
	 */
	if (b3_tbl >= U8_16BIT_TABLE_INDICATOR) {
		b3_tbl -= U8_16BIT_TABLE_INDICATOR;
		start_id = u8_decomp_b4_16bit_tbl[uv][b3_tbl][b4];
		end_id = u8_decomp_b4_16bit_tbl[uv][b3_tbl][b4 + 1];
	} else {
		start_id = u8_decomp_b4_tbl[uv][b3_tbl][b4];
		end_id = u8_decomp_b4_tbl[uv][b3_tbl][b4 + 1];
	}

	/* This also means there wasn't any matching decomposition. */
	if (start_id >= end_id)
		return ((size_t)sz);

	/*
	 * The final table for decomposition mappings has three types of
	 * byte sequences depending on whether a mapping is for compatibility
	 * decomposition, canonical decomposition, or both like the following:
	 *
	 * (1) Compatibility decomposition mappings:
	 *
	 *	+---+---+-...-+---+
	 *	| B0| B1| ... | Bm|
	 *	+---+---+-...-+---+
	 *
	 *	The first byte, B0, is always less then 0xF5 (U8_DECOMP_BOTH).
	 *
	 * (2) Canonical decomposition mappings:
	 *
	 *	+---+---+---+-...-+---+
	 *	| T | b0| b1| ... | bn|
	 *	+---+---+---+-...-+---+
	 *
	 *	where the first byte, T, is 0xF6 (U8_DECOMP_CANONICAL).
	 *
	 * (3) Both mappings:
	 *
	 *	+---+---+---+---+-...-+---+---+---+-...-+---+
	 *	| T | D | b0| b1| ... | bn| B0| B1| ... | Bm|
	 *	+---+---+---+---+-...-+---+---+---+-...-+---+
	 *
	 *	where T is 0xF5 (U8_DECOMP_BOTH) and D is a displacement
	 *	byte, b0 to bn are canonical mapping bytes and B0 to Bm are
	 *	compatibility mapping bytes.
	 *
	 * Note that compatibility decomposition means doing recursive
	 * decompositions using both compatibility decomposition mappings and
	 * canonical decomposition mappings. On the other hand, canonical
	 * decomposition means doing recursive decompositions using only
	 * canonical decomposition mappings. Since the table we have has gone
	 * through the recursions already, we do not need to do so during
	 * runtime, i.e., the table has been completely flattened out
	 * already.
	 */

	b3_base = u8_decomp_b3_tbl[uv][b2][b3].base;

	/* Get the type, T, of the byte sequence. */
	b1 = u8_decomp_final_tbl[uv][b3_base + start_id];

	/*
	 * If necessary, adjust start_id, end_id, or both. Note that if
	 * this is compatibility decomposition mapping, there is no
	 * adjustment.
	 */
	if (canonical_decomposition) {
		/* Is the mapping only for compatibility decomposition? */
		if (b1 < U8_DECOMP_BOTH)
			return ((size_t)sz);

		start_id++;

		if (b1 == U8_DECOMP_BOTH) {
			end_id = start_id +
			    u8_decomp_final_tbl[uv][b3_base + start_id];
			start_id++;
		}
	} else {
		/*
		 * Unless this is a compatibility decomposition mapping,
		 * we adjust the start_id.
		 */
		if (b1 == U8_DECOMP_BOTH) {
			start_id++;
			start_id += u8_decomp_final_tbl[uv][b3_base + start_id];
		} else if (b1 == U8_DECOMP_CANONICAL) {
			start_id++;
		}
	}

	for (i = 0; start_id < end_id; start_id++)
		u8s[i++] = u8_decomp_final_tbl[uv][b3_base + start_id];
	u8s[i] = '\0';

	return (i);
}

/*
 * The find_composition_start() function uses the character bytes given and
 * find out the matching composition mappings if any and return the address
 * to the composition mappings as explained in the do_composition().
 */
static uchar_t *
find_composition_start(size_t uv, uchar_t *s, size_t sz)
{
	uint16_t b1 = 0;
	uint16_t b2 = 0;
	uint16_t b3 = 0;
	uint16_t b3_tbl;
	uint16_t b3_base;
	uint16_t b4 = 0;
	size_t start_id;
	size_t end_id;

	if (sz == 1) {
		b4 = s[0];
	} else if (sz == 2) {
		b3 = s[0];
		b4 = s[1];
	} else if (sz == 3) {
		b2 = s[0];
		b3 = s[1];
		b4 = s[2];
	} else if (sz == 4) {
		b1 = s[0];
		b2 = s[1];
		b3 = s[2];
		b4 = s[3];
	} else {
		/*
		 * This is a fallback and should not happen if the function
		 * was called properly.
		 */
		return (NULL);
	}

	b1 = u8_composition_b1_tbl[uv][b1];
	if (b1 == U8_TBL_ELEMENT_NOT_DEF)
		return (NULL);

	b2 = u8_composition_b2_tbl[uv][b1][b2];
	if (b2 == U8_TBL_ELEMENT_NOT_DEF)
		return (NULL);

	b3_tbl = u8_composition_b3_tbl[uv][b2][b3].tbl_id;
	if (b3_tbl == U8_TBL_ELEMENT_NOT_DEF)
		return (NULL);

	if (b3_tbl >= U8_16BIT_TABLE_INDICATOR) {
		b3_tbl -= U8_16BIT_TABLE_INDICATOR;
		start_id = u8_composition_b4_16bit_tbl[uv][b3_tbl][b4];
		end_id = u8_composition_b4_16bit_tbl[uv][b3_tbl][b4 + 1];
	} else {
		start_id = u8_composition_b4_tbl[uv][b3_tbl][b4];
		end_id = u8_composition_b4_tbl[uv][b3_tbl][b4 + 1];
	}

	if (start_id >= end_id)
		return (NULL);

	b3_base = u8_composition_b3_tbl[uv][b2][b3].base;

	return ((uchar_t *)&(u8_composition_final_tbl[uv][b3_base + start_id]));
}

/*
 * The blocked() function checks on the combining class values of previous
 * characters in this sequence and return whether it is blocked or not.
 */
static boolean_t
blocked(uchar_t *comb_class, size_t last)
{
	uchar_t my_comb_class;
	size_t i;

	my_comb_class = comb_class[last];
	for (i = 1; i < last; i++)
		if (comb_class[i] >= my_comb_class ||
		    comb_class[i] == U8_COMBINING_CLASS_STARTER)
			return (B_TRUE);

	return (B_FALSE);
}

/*
 * The do_composition() reads the character string pointed by 's' and
 * do necessary canonical composition and then copy over the result back to
 * the 's'.
 *
 * The input argument 's' cannot contain more than 32 characters.
 */
static size_t
do_composition(size_t uv, uchar_t *s, uchar_t *comb_class, uchar_t *start,
    uchar_t *disp, size_t last, uchar_t **os, uchar_t *oslast)
{
	uchar_t t[U8_STREAM_SAFE_TEXT_MAX + 1];
	uchar_t tc[U8_MB_CUR_MAX] = { '\0' };
	uint8_t saved_marks[U8_MAX_CHARS_A_SEQ];
	size_t saved_marks_count;
	uchar_t *p;
	uchar_t *saved_p;
	uchar_t *q;
	size_t i;
	size_t saved_i;
	size_t j;
	size_t k;
	size_t l;
	size_t C;
	size_t saved_l;
	size_t size;
	uint32_t u1;
	uint32_t u2;
	boolean_t match_not_found = B_TRUE;

	/*
	 * This should never happen unless the callers are doing some strange
	 * and unexpected things.
	 *
	 * The "last" is the index pointing to the last character not last + 1.
	 */
	if (last >= U8_MAX_CHARS_A_SEQ)
		last = U8_UPPER_LIMIT_IN_A_SEQ;

	for (i = l = 0; i <= last; i++) {
		/*
		 * The last or any non-Starters at the beginning, we don't
		 * have any chance to do composition and so we just copy them
		 * to the temporary buffer.
		 */
		if (i >= last || comb_class[i] != U8_COMBINING_CLASS_STARTER) {
SAVE_THE_CHAR:
			p = s + start[i];
			size = disp[i];
			for (k = 0; k < size; k++)
				t[l++] = *p++;
			continue;
		}

		/*
		 * If this could be a start of Hangul Jamos, then, we try to
		 * conjoin them.
		 */
		if (s[start[i]] == U8_HANGUL_JAMO_1ST_BYTE) {
			U8_PUT_3BYTES_INTO_UTF32(u1, s[start[i]],
			    s[start[i] + 1], s[start[i] + 2]);
			U8_PUT_3BYTES_INTO_UTF32(u2, s[start[i] + 3],
			    s[start[i] + 4], s[start[i] + 5]);

			if (U8_HANGUL_JAMO_L(u1) && U8_HANGUL_JAMO_V(u2)) {
				u1 -= U8_HANGUL_JAMO_L_FIRST;
				u2 -= U8_HANGUL_JAMO_V_FIRST;
				u1 = U8_HANGUL_SYL_FIRST +
				    (u1 * U8_HANGUL_V_COUNT + u2) *
				    U8_HANGUL_T_COUNT;

				i += 2;
				if (i <= last) {
					U8_PUT_3BYTES_INTO_UTF32(u2,
					    s[start[i]], s[start[i] + 1],
					    s[start[i] + 2]);

					if (U8_HANGUL_JAMO_T(u2)) {
						u1 += u2 -
						    U8_HANGUL_JAMO_T_FIRST;
						i++;
					}
				}

				U8_SAVE_HANGUL_AS_UTF8(t + l, 0, 1, 2, u1);
				i--;
				l += 3;
				continue;
			}
		}

		/*
		 * Let's then find out if this Starter has composition
		 * mapping.
		 */
		p = find_composition_start(uv, s + start[i], disp[i]);
		if (p == NULL)
			goto SAVE_THE_CHAR;

		/*
		 * We have a Starter with composition mapping and the next
		 * character is a non-Starter. Let's try to find out if
		 * we can do composition.
		 */

		saved_p = p;
		saved_i = i;
		saved_l = l;
		saved_marks_count = 0;

TRY_THE_NEXT_MARK:
		q = s + start[++i];
		size = disp[i];

		/*
		 * The next for() loop compares the non-Starter pointed by
		 * 'q' with the possible (joinable) characters pointed by 'p'.
		 *
		 * The composition final table entry pointed by the 'p'
		 * looks like the following:
		 *
		 * +---+---+---+-...-+---+---+---+---+-...-+---+---+
		 * | C | b0| b2| ... | bn| F | B0| B1| ... | Bm| F |
		 * +---+---+---+-...-+---+---+---+---+-...-+---+---+
		 *
		 * where C is the count byte indicating the number of
		 * mapping pairs where each pair would be look like
		 * (b0-bn F, B0-Bm F). The b0-bn are the bytes of the second
		 * character of a canonical decomposition and the B0-Bm are
		 * the bytes of a matching composite character. The F is
		 * a filler byte after each character as the separator.
		 */

		match_not_found = B_TRUE;

		for (C = *p++; C > 0; C--) {
			for (k = 0; k < size; p++, k++)
				if (*p != q[k])
					break;

			/* Have we found it? */
			if (k >= size && *p == U8_TBL_ELEMENT_FILLER) {
				match_not_found = B_FALSE;

				l = saved_l;

				while (*++p != U8_TBL_ELEMENT_FILLER)
					t[l++] = *p;

				break;
			}

			/* We didn't find; skip to the next pair. */
			if (*p != U8_TBL_ELEMENT_FILLER)
				while (*++p != U8_TBL_ELEMENT_FILLER)
					;
			while (*++p != U8_TBL_ELEMENT_FILLER)
				;
			p++;
		}

		/*
		 * If there was no match, we will need to save the combining
		 * mark for later appending. After that, if the next one
		 * is a non-Starter and not blocked, then, we try once
		 * again to do composition with the next non-Starter.
		 *
		 * If there was no match and this was a Starter, then,
		 * this is a new start.
		 *
		 * If there was a match and a composition done and we have
		 * more to check on, then, we retrieve a new composition final
		 * table entry for the composite and then try to do the
		 * composition again.
		 */

		if (match_not_found) {
			if (comb_class[i] == U8_COMBINING_CLASS_STARTER) {
				i--;
				goto SAVE_THE_CHAR;
			}

			saved_marks[saved_marks_count++] = i;
		}

		if (saved_l == l) {
			while (i < last) {
				if (blocked(comb_class, i + 1))
					saved_marks[saved_marks_count++] = ++i;
				else
					break;
			}
			if (i < last) {
				p = saved_p;
				goto TRY_THE_NEXT_MARK;
			}
		} else if (i < last) {
			p = find_composition_start(uv, t + saved_l,
			    l - saved_l);
			if (p != NULL) {
				saved_p = p;
				goto TRY_THE_NEXT_MARK;
			}
		}

		/*
		 * There is no more composition possible.
		 *
		 * If there was no composition what so ever then we copy
		 * over the original Starter and then append any non-Starters
		 * remaining at the target string sequentially after that.
		 */

		if (saved_l == l) {
			p = s + start[saved_i];
			size = disp[saved_i];
			for (j = 0; j < size; j++)
				t[l++] = *p++;
		}

		for (k = 0; k < saved_marks_count; k++) {
			p = s + start[saved_marks[k]];
			size = disp[saved_marks[k]];
			for (j = 0; j < size; j++)
				t[l++] = *p++;
		}
	}

	/*
	 * If the last character is a Starter and if we have a character
	 * (possibly another Starter) that can be turned into a composite,
	 * we do so and we do so until there is no more of composition
	 * possible.
	 */
	if (comb_class[last] == U8_COMBINING_CLASS_STARTER) {
		p = *os;
		saved_l = l - disp[last];

		while (p < oslast) {
			size = u8_number_of_bytes[*p];
			if (size <= 1 || (p + size) > oslast)
				break;

			saved_p = p;

			for (i = 0; i < size; i++)
				tc[i] = *p++;

			q = find_composition_start(uv, t + saved_l,
			    l - saved_l);
			if (q == NULL) {
				p = saved_p;
				break;
			}

			match_not_found = B_TRUE;

			for (C = *q++; C > 0; C--) {
				for (k = 0; k < size; q++, k++)
					if (*q != tc[k])
						break;

				if (k >= size && *q == U8_TBL_ELEMENT_FILLER) {
					match_not_found = B_FALSE;

					l = saved_l;

					while (*++q != U8_TBL_ELEMENT_FILLER) {
						/*
						 * This is practically
						 * impossible but we don't
						 * want to take any chances.
						 */
						if (l >=
						    U8_STREAM_SAFE_TEXT_MAX) {
							p = saved_p;
							goto SAFE_RETURN;
						}
						t[l++] = *q;
					}

					break;
				}

				if (*q != U8_TBL_ELEMENT_FILLER)
					while (*++q != U8_TBL_ELEMENT_FILLER)
						;
				while (*++q != U8_TBL_ELEMENT_FILLER)
					;
				q++;
			}

			if (match_not_found) {
				p = saved_p;
				break;
			}
		}
SAFE_RETURN:
		*os = p;
	}

	/*
	 * Now we copy over the temporary string to the target string.
	 * Since composition always reduces the number of characters or
	 * the number of characters stay, we don't need to worry about
	 * the buffer overflow here.
	 */
	for (i = 0; i < l; i++)
		s[i] = t[i];
	s[l] = '\0';

	return (l);
}

/*
 * The collect_a_seq() function checks on the given string s, collect
 * a sequence of characters at u8s, and return the sequence. While it collects
 * a sequence, it also applies case conversion, canonical or compatibility
 * decomposition, canonical decomposition, or some or all of them and
 * in that order.
 *
 * The collected sequence cannot be bigger than 32 characters since if
 * it is having more than 31 characters, the sequence will be terminated
 * with a U+034F COMBINING GRAPHEME JOINER (CGJ) character and turned into
 * a Stream-Safe Text. The collected sequence is always terminated with
 * a null byte and the return value is the byte length of the sequence
 * including 0. The return value does not include the terminating
 * null byte.
 */
static size_t
collect_a_seq(size_t uv, uchar_t *u8s, uchar_t **source, uchar_t *slast,
    boolean_t is_it_toupper, boolean_t is_it_tolower,
    boolean_t canonical_decomposition, boolean_t compatibility_decomposition,
    boolean_t canonical_composition,
    int *errnum, u8_normalization_states_t *state)
{
	uchar_t *s;
	int sz;
	int saved_sz;
	size_t i;
	size_t j;
	size_t k;
	size_t l;
	uchar_t comb_class[U8_MAX_CHARS_A_SEQ];
	uchar_t disp[U8_MAX_CHARS_A_SEQ];
	uchar_t start[U8_MAX_CHARS_A_SEQ];
	uchar_t u8t[U8_MB_CUR_MAX] = { '\0' };
	uchar_t uts[U8_STREAM_SAFE_TEXT_MAX + 1];
	uchar_t tc;
	size_t last;
	size_t saved_last;
	uint32_t u1;

	/*
	 * Save the source string pointer which we will return a changed
	 * pointer if we do processing.
	 */
	s = *source;

	/*
	 * The following is a fallback for just in case callers are not
	 * checking the string boundaries before the calling.
	 */
	if (s >= slast) {
		u8s[0] = '\0';

		return (0);
	}

	/*
	 * As the first thing, let's collect a character and do case
	 * conversion if necessary.
	 */

	sz = u8_number_of_bytes[*s];

	if (sz < 0) {
		*errnum = EILSEQ;

		u8s[0] = *s++;
		u8s[1] = '\0';

		*source = s;

		return (1);
	}

	if (sz == 1) {
		if (is_it_toupper)
			u8s[0] = U8_ASCII_TOUPPER(*s);
		else if (is_it_tolower)
			u8s[0] = U8_ASCII_TOLOWER(*s);
		else
			u8s[0] = *s;
		s++;
		u8s[1] = '\0';
	} else if ((s + sz) > slast) {
		*errnum = EINVAL;

		for (i = 0; s < slast; )
			u8s[i++] = *s++;
		u8s[i] = '\0';

		*source = s;

		return (i);
	} else {
		if (is_it_toupper || is_it_tolower) {
			i = do_case_conv(uv, u8s, s, sz, is_it_toupper);
			s += sz;
			sz = i;
		} else {
			for (i = 0; i < sz; )
				u8s[i++] = *s++;
			u8s[i] = '\0';
		}
	}

	/*
	 * And then canonical/compatibility decomposition followed by
	 * an optional canonical composition. Please be noted that
	 * canonical composition is done only when a decomposition is
	 * done.
	 */
	if (canonical_decomposition || compatibility_decomposition) {
		if (sz == 1) {
			*state = U8_STATE_START;

			saved_sz = 1;

			comb_class[0] = 0;
			start[0] = 0;
			disp[0] = 1;

			last = 1;
		} else {
			saved_sz = do_decomp(uv, u8s, u8s, sz,
			    canonical_decomposition, state);

			last = 0;

			for (i = 0; i < saved_sz; ) {
				sz = u8_number_of_bytes[u8s[i]];

				comb_class[last] = combining_class(uv,
				    u8s + i, sz);
				start[last] = i;
				disp[last] = sz;

				last++;
				i += sz;
			}

			/*
			 * Decomposition yields various Hangul related
			 * states but not on combining marks. We need to
			 * find out at here by checking on the last
			 * character.
			 */
			if (*state == U8_STATE_START) {
				if (comb_class[last - 1])
					*state = U8_STATE_COMBINING_MARK;
			}
		}

		saved_last = last;

		while (s < slast) {
			sz = u8_number_of_bytes[*s];

			/*
			 * If this is an illegal character, an incomplete
			 * character, or an 7-bit ASCII Starter character,
			 * then we have collected a sequence; break and let
			 * the next call deal with the two cases.
			 *
			 * Note that this is okay only if you are using this
			 * function with a fixed length string, not on
			 * a buffer with multiple calls of one chunk at a time.
			 */
			if (sz <= 1) {
				break;
			} else if ((s + sz) > slast) {
				break;
			} else {
				/*
				 * If the previous character was a Hangul Jamo
				 * and this character is a Hangul Jamo that
				 * can be conjoined, we collect the Jamo.
				 */
				if (*s == U8_HANGUL_JAMO_1ST_BYTE) {
					U8_PUT_3BYTES_INTO_UTF32(u1,
					    *s, *(s + 1), *(s + 2));

					if (U8_HANGUL_COMPOSABLE_L_V(*state,
					    u1)) {
						i = 0;
						*state = U8_STATE_HANGUL_LV;
						goto COLLECT_A_HANGUL;
					}

					if (U8_HANGUL_COMPOSABLE_LV_T(*state,
					    u1)) {
						i = 0;
						*state = U8_STATE_HANGUL_LVT;
						goto COLLECT_A_HANGUL;
					}
				}

				/*
				 * Regardless of whatever it was, if this is
				 * a Starter, we don't collect the character
				 * since that's a new start and we will deal
				 * with it at the next time.
				 */
				i = combining_class(uv, s, sz);
				if (i == U8_COMBINING_CLASS_STARTER)
					break;

				/*
				 * We know the current character is a combining
				 * mark. If the previous character wasn't
				 * a Starter (not Hangul) or a combining mark,
				 * then, we don't collect this combining mark.
				 */
				if (*state != U8_STATE_START &&
				    *state != U8_STATE_COMBINING_MARK)
					break;

				*state = U8_STATE_COMBINING_MARK;
COLLECT_A_HANGUL:
				/*
				 * If we collected a Starter and combining
				 * marks up to 30, i.e., total 31 characters,
				 * then, we terminate this degenerately long
				 * combining sequence with a U+034F COMBINING
				 * GRAPHEME JOINER (CGJ) which is 0xCD 0x8F in
				 * UTF-8 and turn this into a Stream-Safe
				 * Text. This will be extremely rare but
				 * possible.
				 *
				 * The following will also guarantee that
				 * we are not writing more than 32 characters
				 * plus a NULL at u8s[].
				 */
				if (last >= U8_UPPER_LIMIT_IN_A_SEQ) {
TURN_STREAM_SAFE:
					*state = U8_STATE_START;
					comb_class[last] = 0;
					start[last] = saved_sz;
					disp[last] = 2;
					last++;

					u8s[saved_sz++] = 0xCD;
					u8s[saved_sz++] = 0x8F;

					break;
				}

				/*
				 * Some combining marks also do decompose into
				 * another combining mark or marks.
				 */
				if (*state == U8_STATE_COMBINING_MARK) {
					k = last;
					l = sz;
					i = do_decomp(uv, uts, s, sz,
					    canonical_decomposition, state);
					for (j = 0; j < i; ) {
						sz = u8_number_of_bytes[uts[j]];

						comb_class[last] =
						    combining_class(uv,
						    uts + j, sz);
						start[last] = saved_sz + j;
						disp[last] = sz;

						last++;
						if (last >=
						    U8_UPPER_LIMIT_IN_A_SEQ) {
							last = k;
							goto TURN_STREAM_SAFE;
						}
						j += sz;
					}

					*state = U8_STATE_COMBINING_MARK;
					sz = i;
					s += l;

					for (i = 0; i < sz; i++)
						u8s[saved_sz++] = uts[i];
				} else {
					comb_class[last] = i;
					start[last] = saved_sz;
					disp[last] = sz;
					last++;

					for (i = 0; i < sz; i++)
						u8s[saved_sz++] = *s++;
				}

				/*
				 * If this is U+0345 COMBINING GREEK
				 * YPOGEGRAMMENI (0xCD 0x85 in UTF-8), a.k.a.,
				 * iota subscript, and need to be converted to
				 * uppercase letter, convert it to U+0399 GREEK
				 * CAPITAL LETTER IOTA (0xCE 0x99 in UTF-8),
				 * i.e., convert to capital adscript form as
				 * specified in the Unicode standard.
				 *
				 * This is the only special case of (ambiguous)
				 * case conversion at combining marks and
				 * probably the standard will never have
				 * anything similar like this in future.
				 */
				if (is_it_toupper && sz >= 2 &&
				    u8s[saved_sz - 2] == 0xCD &&
				    u8s[saved_sz - 1] == 0x85) {
					u8s[saved_sz - 2] = 0xCE;
					u8s[saved_sz - 1] = 0x99;
				}
			}
		}

		/*
		 * Let's try to ensure a canonical ordering for the collected
		 * combining marks. We do this only if we have collected
		 * at least one more non-Starter. (The decomposition mapping
		 * data tables have fully (and recursively) expanded and
		 * canonically ordered decompositions.)
		 *
		 * The U8_SWAP_COMB_MARKS() convenience macro has some
		 * assumptions and we are meeting the assumptions.
		 */
		last--;
		if (last >= saved_last) {
			for (i = 0; i < last; i++)
				for (j = last; j > i; j--)
					if (comb_class[j] &&
					    comb_class[j - 1] > comb_class[j]) {
						U8_SWAP_COMB_MARKS(j - 1, j);
					}
		}

		*source = s;

		if (! canonical_composition) {
			u8s[saved_sz] = '\0';
			return (saved_sz);
		}

		/*
		 * Now do the canonical composition. Note that we do this
		 * only after a canonical or compatibility decomposition to
		 * finish up NFC or NFKC.
		 */
		sz = do_composition(uv, u8s, comb_class, start, disp, last,
		    &s, slast);
	}

	*source = s;

	return ((size_t)sz);
}

/*
 * The do_norm_compare() function does string comparion based on Unicode
 * simple case mappings and Unicode Normalization definitions.
 *
 * It does so by collecting a sequence of character at a time and comparing
 * the collected sequences from the strings.
 *
 * The meanings on the return values are the same as the usual strcmp().
 */
static int
do_norm_compare(size_t uv, uchar_t *s1, uchar_t *s2, size_t n1, size_t n2,
    int flag, int *errnum)
{
	int result;
	size_t sz1;
	size_t sz2;
	uchar_t u8s1[U8_STREAM_SAFE_TEXT_MAX + 1];
	uchar_t u8s2[U8_STREAM_SAFE_TEXT_MAX + 1];
	uchar_t *s1last;
	uchar_t *s2last;
	boolean_t is_it_toupper;
	boolean_t is_it_tolower;
	boolean_t canonical_decomposition;
	boolean_t compatibility_decomposition;
	boolean_t canonical_composition;
	u8_normalization_states_t state;

	s1last = s1 + n1;
	s2last = s2 + n2;

	is_it_toupper = flag & U8_TEXTPREP_TOUPPER;
	is_it_tolower = flag & U8_TEXTPREP_TOLOWER;
	canonical_decomposition = flag & U8_CANON_DECOMP;
	compatibility_decomposition = flag & U8_COMPAT_DECOMP;
	canonical_composition = flag & U8_CANON_COMP;

	while (s1 < s1last && s2 < s2last) {
		/*
		 * If the current character is a 7-bit ASCII and the last
		 * character, or, if the current character and the next
		 * character are both some 7-bit ASCII characters then
		 * we treat the current character as a sequence.
		 *
		 * In any other cases, we need to call collect_a_seq().
		 */

		if (U8_ISASCII(*s1) && ((s1 + 1) >= s1last ||
		    ((s1 + 1) < s1last && U8_ISASCII(*(s1 + 1))))) {
			if (is_it_toupper)
				u8s1[0] = U8_ASCII_TOUPPER(*s1);
			else if (is_it_tolower)
				u8s1[0] = U8_ASCII_TOLOWER(*s1);
			else
				u8s1[0] = *s1;
			u8s1[1] = '\0';
			sz1 = 1;
			s1++;
		} else {
			state = U8_STATE_START;
			sz1 = collect_a_seq(uv, u8s1, &s1, s1last,
			    is_it_toupper, is_it_tolower,
			    canonical_decomposition,
			    compatibility_decomposition,
			    canonical_composition, errnum, &state);
		}

		if (U8_ISASCII(*s2) && ((s2 + 1) >= s2last ||
		    ((s2 + 1) < s2last && U8_ISASCII(*(s2 + 1))))) {
			if (is_it_toupper)
				u8s2[0] = U8_ASCII_TOUPPER(*s2);
			else if (is_it_tolower)
				u8s2[0] = U8_ASCII_TOLOWER(*s2);
			else
				u8s2[0] = *s2;
			u8s2[1] = '\0';
			sz2 = 1;
			s2++;
		} else {
			state = U8_STATE_START;
			sz2 = collect_a_seq(uv, u8s2, &s2, s2last,
			    is_it_toupper, is_it_tolower,
			    canonical_decomposition,
			    compatibility_decomposition,
			    canonical_composition, errnum, &state);
		}

		/*
		 * Now compare the two characters. If they are the same,
		 * we move on to the next character sequences.
		 */
		if (sz1 == 1 && sz2 == 1) {
			if (*u8s1 > *u8s2)
				return (1);
			if (*u8s1 < *u8s2)
				return (-1);
		} else {
			result = strcmp((const char *)u8s1, (const char *)u8s2);
			if (result != 0)
				return (result);
		}
	}

	/*
	 * We compared until the end of either or both strings.
	 *
	 * If we reached to or went over the ends for the both, that means
	 * they are the same.
	 *
	 * If we reached only one end, that means the other string has
	 * something which then can be used to determine the return value.
	 */
	if (s1 >= s1last) {
		if (s2 >= s2last)
			return (0);
		return (-1);
	}
	return (1);
}

/*
 * The u8_strcmp() function compares two UTF-8 strings quite similar to
 * the strcmp(). For the comparison, however, Unicode Normalization specific
 * equivalency and Unicode simple case conversion mappings based equivalency
 * can be requested and checked against.
 */
int
u8_strcmp(const char *s1, const char *s2, size_t n, int flag, size_t uv,
    int *errnum)
{
	int f;
	size_t n1;
	size_t n2;

	*errnum = 0;

	/*
	 * Check on the requested Unicode version, case conversion, and
	 * normalization flag values.
	 */

	if (uv > U8_UNICODE_LATEST) {
		*errnum = ERANGE;
		uv = U8_UNICODE_LATEST;
	}

	if (flag == 0) {
		flag = U8_STRCMP_CS;
	} else {
		f = flag & (U8_STRCMP_CS | U8_STRCMP_CI_UPPER |
		    U8_STRCMP_CI_LOWER);
		if (f == 0) {
			flag |= U8_STRCMP_CS;
		} else if (f != U8_STRCMP_CS && f != U8_STRCMP_CI_UPPER &&
		    f != U8_STRCMP_CI_LOWER) {
			*errnum = EBADF;
			flag = U8_STRCMP_CS;
		}

		f = flag & (U8_CANON_DECOMP | U8_COMPAT_DECOMP | U8_CANON_COMP);
		if (f && f != U8_STRCMP_NFD && f != U8_STRCMP_NFC &&
		    f != U8_STRCMP_NFKD && f != U8_STRCMP_NFKC) {
			*errnum = EBADF;
			flag = U8_STRCMP_CS;
		}
	}

	if (flag == U8_STRCMP_CS) {
		return (n == 0 ? strcmp(s1, s2) : strncmp(s1, s2, n));
	}

	n1 = strlen(s1);
	n2 = strlen(s2);
	if (n != 0) {
		if (n < n1)
			n1 = n;
		if (n < n2)
			n2 = n;
	}

	/*
	 * Simple case conversion can be done much faster and so we do
	 * them separately here.
	 */
	if (flag == U8_STRCMP_CI_UPPER) {
		return (do_case_compare(uv, (uchar_t *)s1, (uchar_t *)s2,
		    n1, n2, B_TRUE, errnum));
	} else if (flag == U8_STRCMP_CI_LOWER) {
		return (do_case_compare(uv, (uchar_t *)s1, (uchar_t *)s2,
		    n1, n2, B_FALSE, errnum));
	}

	return (do_norm_compare(uv, (uchar_t *)s1, (uchar_t *)s2, n1, n2,
	    flag, errnum));
}

size_t
u8_textprep_str(char *inarray, size_t *inlen, char *outarray, size_t *outlen,
    int flag, size_t unicode_version, int *errnum)
{
	int f;
	int sz;
	uchar_t *ib;
	uchar_t *ibtail;
	uchar_t *ob;
	uchar_t *obtail;
	boolean_t do_not_ignore_null;
	boolean_t do_not_ignore_invalid;
	boolean_t is_it_toupper;
	boolean_t is_it_tolower;
	boolean_t canonical_decomposition;
	boolean_t compatibility_decomposition;
	boolean_t canonical_composition;
	size_t ret_val;
	size_t i;
	size_t j;
	uchar_t u8s[U8_STREAM_SAFE_TEXT_MAX + 1];
	u8_normalization_states_t state;

	if (unicode_version > U8_UNICODE_LATEST) {
		*errnum = ERANGE;
		return ((size_t)-1);
	}

	f = flag & (U8_TEXTPREP_TOUPPER | U8_TEXTPREP_TOLOWER);
	if (f == (U8_TEXTPREP_TOUPPER | U8_TEXTPREP_TOLOWER)) {
		*errnum = EBADF;
		return ((size_t)-1);
	}

	f = flag & (U8_CANON_DECOMP | U8_COMPAT_DECOMP | U8_CANON_COMP);
	if (f && f != U8_TEXTPREP_NFD && f != U8_TEXTPREP_NFC &&
	    f != U8_TEXTPREP_NFKD && f != U8_TEXTPREP_NFKC) {
		*errnum = EBADF;
		return ((size_t)-1);
	}

	if (inarray == NULL || *inlen == 0)
		return (0);

	if (outarray == NULL) {
		*errnum = E2BIG;
		return ((size_t)-1);
	}

	ib = (uchar_t *)inarray;
	ob = (uchar_t *)outarray;
	ibtail = ib + *inlen;
	obtail = ob + *outlen;

	do_not_ignore_null = !(flag & U8_TEXTPREP_IGNORE_NULL);
	do_not_ignore_invalid = !(flag & U8_TEXTPREP_IGNORE_INVALID);
	is_it_toupper = flag & U8_TEXTPREP_TOUPPER;
	is_it_tolower = flag & U8_TEXTPREP_TOLOWER;

	ret_val = 0;

	/*
	 * If we don't have a normalization flag set, we do the simple case
	 * conversion based text preparation separately below. Text
	 * preparation involving Normalization will be done in the false task
	 * block, again, separately since it will take much more time and
	 * resource than doing simple case conversions.
	 */
	if (f == 0) {
		while (ib < ibtail) {
			if (*ib == '\0' && do_not_ignore_null)
				break;

			sz = u8_number_of_bytes[*ib];

			if (sz < 0) {
				if (do_not_ignore_invalid) {
					*errnum = EILSEQ;
					ret_val = (size_t)-1;
					break;
				}

				sz = 1;
				ret_val++;
			}

			if (sz == 1) {
				if (ob >= obtail) {
					*errnum = E2BIG;
					ret_val = (size_t)-1;
					break;
				}

				if (is_it_toupper)
					*ob = U8_ASCII_TOUPPER(*ib);
				else if (is_it_tolower)
					*ob = U8_ASCII_TOLOWER(*ib);
				else
					*ob = *ib;
				ib++;
				ob++;
			} else if ((ib + sz) > ibtail) {
				if (do_not_ignore_invalid) {
					*errnum = EINVAL;
					ret_val = (size_t)-1;
					break;
				}

				if ((obtail - ob) < (ibtail - ib)) {
					*errnum = E2BIG;
					ret_val = (size_t)-1;
					break;
				}

				/*
				 * We treat the remaining incomplete character
				 * bytes as a character.
				 */
				ret_val++;

				while (ib < ibtail)
					*ob++ = *ib++;
			} else {
				if (is_it_toupper || is_it_tolower) {
					i = do_case_conv(unicode_version, u8s,
					    ib, sz, is_it_toupper);

					if ((obtail - ob) < i) {
						*errnum = E2BIG;
						ret_val = (size_t)-1;
						break;
					}

					ib += sz;

					for (sz = 0; sz < i; sz++)
						*ob++ = u8s[sz];
				} else {
					if ((obtail - ob) < sz) {
						*errnum = E2BIG;
						ret_val = (size_t)-1;
						break;
					}

					for (i = 0; i < sz; i++)
						*ob++ = *ib++;
				}
			}
		}
	} else {
		canonical_decomposition = flag & U8_CANON_DECOMP;
		compatibility_decomposition = flag & U8_COMPAT_DECOMP;
		canonical_composition = flag & U8_CANON_COMP;

		while (ib < ibtail) {
			if (*ib == '\0' && do_not_ignore_null)
				break;

			/*
			 * If the current character is a 7-bit ASCII
			 * character and it is the last character, or,
			 * if the current character is a 7-bit ASCII
			 * character and the next character is also a 7-bit
			 * ASCII character, then, we copy over this
			 * character without going through collect_a_seq().
			 *
			 * In any other cases, we need to look further with
			 * the collect_a_seq() function.
			 */
			if (U8_ISASCII(*ib) && ((ib + 1) >= ibtail ||
			    ((ib + 1) < ibtail && U8_ISASCII(*(ib + 1))))) {
				if (ob >= obtail) {
					*errnum = E2BIG;
					ret_val = (size_t)-1;
					break;
				}

				if (is_it_toupper)
					*ob = U8_ASCII_TOUPPER(*ib);
				else if (is_it_tolower)
					*ob = U8_ASCII_TOLOWER(*ib);
				else
					*ob = *ib;
				ib++;
				ob++;
			} else {
				*errnum = 0;
				state = U8_STATE_START;

				j = collect_a_seq(unicode_version, u8s,
				    &ib, ibtail,
				    is_it_toupper,
				    is_it_tolower,
				    canonical_decomposition,
				    compatibility_decomposition,
				    canonical_composition,
				    errnum, &state);

				if (*errnum && do_not_ignore_invalid) {
					ret_val = (size_t)-1;
					break;
				}

				if ((obtail - ob) < j) {
					*errnum = E2BIG;
					ret_val = (size_t)-1;
					break;
				}

				for (i = 0; i < j; i++)
					*ob++ = u8s[i];
			}
		}
	}

	*inlen = ibtail - ib;
	*outlen = obtail - ob;

	return (ret_val);
}

#if defined(_KERNEL) && defined(HAVE_SPL)
static int __init
unicode_init(void)
{
	return (0);
}

static void __exit
unicode_fini(void)
{
}

module_init(unicode_init);
module_exit(unicode_fini);

MODULE_DESCRIPTION("Unicode implementation");
MODULE_AUTHOR(ZFS_META_AUTHOR);
MODULE_LICENSE(ZFS_META_LICENSE);
MODULE_VERSION(ZFS_META_VERSION "-" ZFS_META_RELEASE);

EXPORT_SYMBOL(u8_validate);
EXPORT_SYMBOL(u8_strcmp);
EXPORT_SYMBOL(u8_textprep_str);
#endif
