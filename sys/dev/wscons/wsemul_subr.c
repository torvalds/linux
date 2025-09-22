/*	$OpenBSD: wsemul_subr.c,v 1.2 2023/03/06 17:14:44 miod Exp $	*/

/*
 * Copyright (c) 2007, 2013 Miodrag Vallat.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice, this permission notice, and the disclaimer below
 * appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Part of the UTF-8 state machine logic borrowed from citrus_utf8.c
 * under the following licence:
 */
/*-
 * Copyright (c) 2002-2004 Tim J. Robbins
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>

#include <dev/wscons/wscons_features.h>
#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>
#include <dev/wscons/wsemulvar.h>
#include <dev/wscons/wsksymdef.h>

int	wsemul_local_translate(u_int32_t, kbd_t, u_char *);

/*
 * Get characters from an input stream and update the input state.
 * Processing stops when the stream is empty, or a complete character
 * sequence has been recognized, in which case it returns zero.
 */
int
wsemul_getchar(const u_char **inbuf, u_int *inlen,
    struct wsemul_inputstate *state, int allow_utf8)
{
	u_int len = *inlen;
	const u_char *buf = *inbuf;
#ifdef HAVE_UTF8_SUPPORT
	int rc;
	u_int32_t tmpchar, lbound;
	u_int mbleft;
#endif

	if (len == 0)
		return EAGAIN;

#ifndef HAVE_UTF8_SUPPORT
	state->inchar = *buf++;
	state->mbleft = 0;
	len--;
	*inlen = len;
	*inbuf = buf;
	return 0;
#else
	/*
	 * If we do not allow multibyte sequences, process as quickly
	 * as possible.
	 */
	if (!allow_utf8) {
		state->inchar = *buf++;
		state->mbleft = 0;
		len--;
		*inlen = len;
		*inbuf = buf;
		return 0;
	}

	rc = EAGAIN;
	tmpchar = state->inchar;
	lbound = state->lbound;
	mbleft = state->mbleft;

	while (len != 0) {
		u_int32_t frag = (u_int32_t)*buf++;
		len--;

		/*
		 * If we are in the middle of a multibyte sequence, try
		 * to complete it.
		 */

		if (mbleft != 0) {
			if ((frag & 0xc0) != 0x80)
				goto invalid;

			tmpchar = (tmpchar << 6) | (frag & 0x3f);
			mbleft--;
			if (mbleft == 0) {
				if (tmpchar < lbound)
					goto invalid;
				if (tmpchar >= 0xd800 && tmpchar < 0xe000)
					goto invalid;
				if (tmpchar >= 0x110000)
					goto invalid;
				rc = 0;
				break;
			}
			continue;
		}

		/*
		 * Otherwise let's decide if this is the start of a new
		 * multibyte sequence, or a 7-bit character.
		 */

		if ((frag & 0x80) == 0) {
			tmpchar = frag;
			rc = 0;
			break;
		}

		if ((frag & 0xe0) == 0xc0) {
			frag &= 0x1f;
			mbleft = 1;
			lbound = 0x80;
		} else if ((frag & 0xf0) == 0xe0) {
			frag &= 0x0f;
			mbleft = 2;
			lbound = 0x800;
		} else if ((frag & 0xf8) == 0xf0) {
			frag &= 0x07;
			mbleft = 3;
			lbound = 0x10000;
		} else {
			goto invalid;
		}

		tmpchar = frag;
		state->lbound = lbound;
		continue;

invalid:
		/* Abort the ill-formed sequence and continue */
		mbleft = 0;
		tmpchar = 0;
		rc = EILSEQ;
	}

	state->inchar = tmpchar;
	state->mbleft = mbleft;
	*inlen = len;
	*inbuf = buf;
	return rc;
#endif
}

/*
 * Unicode Cyrillic to KOI8 translation table (starts at U+0400),
 * from RFC 2319.
 */
const u_int8_t cyrillic_to_koi8[] = {
	0x00,	/* IE grave */		/* 0400 */
	0xb3,	/* IO */
	0x00,	/* DJE */
	0x00,	/* GJE */
	0xb4,	/* UKR IE */
	0x00,	/* DZE */
	0xb6,	/* BYE/UKR I */
	0xb7,	/* YI */
	0x00,	/* JE */
	0x00,	/* LJE */
	0x00,	/* NJE */
	0x00,	/* TSHE */
	0x00,	/* KJE */
	0x00,	/* I grave */
	0x00,	/* short U */
	0x00,	/* DZHE */
	0xe1,	/* A */			/* 0410 */
	0xe2,	/* BE */
	0xf7,	/* VE */
	0xe7,	/* GHE */
	0xe4,	/* DE */
	0xe5,	/* IE */
	0xf6,	/* ZHE */
	0xfa,	/* ZE */
	0xe9,	/* I */
	0xea,	/* short I */
	0xeb,	/* KA */
	0xec,	/* EL */
	0xed,	/* EM */
	0xee,	/* EN */
	0xef,	/* O */
	0xf0,	/* PE */
	0xf2,	/* ER */		/* 0420 */
	0xf3,	/* ES */
	0xf4,	/* TE */
	0xf5,	/* U */
	0xe6,	/* EF */
	0xe8,	/* HA */
	0xe3,	/* TSE */
	0xfe,	/* CHE */
	0xfb,	/* SHA */
	0xfd,	/* SHCHA */
	0xff,	/* HARD SIGN */
	0xf9,	/* YERU */
	0xf8,	/* SOFT SIGN */
	0xfc,	/* E */
	0xe0,	/* YU */
	0xf1,	/* YA */
	0xc1,	/* a */			/* 0430 */
	0xc2,	/* be */
	0xd7,	/* ve */
	0xc7,	/* ghe */
	0xc4,	/* de */
	0xc5,	/* ie */
	0xd6,	/* zhe */
	0xda,	/* ze */
	0xc9,	/* i */
	0xca,	/* short i */
	0xcb,	/* ka */
	0xcc,	/* el */
	0xcd,	/* em */
	0xce,	/* en */
	0xcf,	/* o */
	0xd0,	/* pe */
	0xd2,	/* er */		/* 0440 */
	0xd3,	/* es */
	0xd4,	/* te */
	0xd5,	/* u */
	0xc6,	/* ef */
	0xc8,	/* ha */
	0xc3,	/* tse */
	0xde,	/* che */
	0xdb,	/* sha */
	0xdd,	/* shcha */
	0xdf,	/* hard sign */
	0xd9,	/* yeru */
	0xd8,	/* soft sign */
	0xdc,	/* e */
	0xc0,	/* yu */
	0xd1,	/* ya */
	0x00,	/* ie grave */		/* 0450 */
	0xa3,	/* io */
	0x00,	/* dje */
	0x00,	/* GJE */
	0xa4,	/* UKR ie */
	0x00,	/* DZE */
	0xa6,	/* BYE/UKR I */
	0xa7,	/* YI */
	0x00,	/* JE */
	0x00,	/* LJE */
	0x00,	/* NJE */
	0x00,	/* TSHE */
	0x00,	/* KJE */
	0x00,	/* I grave */
	0x00,	/* short U */
	0x00	/* DZHE */
};

/*
 * Europe to Latin-2 translation table (starts at U+0100).
 */
const u_int8_t unicode_to_latin2[] = {
	0x00,	/* A macron */		/* 0100 */
	0x00,	/* a macron */
	0xc3,	/* A breve */
	0xe3,	/* a breve */
	0xa1,	/* A ogonek */
	0xb1,	/* a ogonek */
	0xc6,	/* C acute */
	0xe6,	/* c acute */
	0x00,	/* C circumflex */
	0x00,	/* c circumflex */
	0x00,	/* C abovering */
	0x00,	/* c abovering */
	0xc8,	/* C caron */
	0xe8,	/* c caron */
	0xcf,	/* D caron */
	0xef,	/* d caron */
	0xd0,	/* D stroke */		/* 0110 */
	0xf0,	/* d stroke */
	0x00,	/* E macron */
	0x00,	/* e macron */
	0x00,	/* E breve */
	0x00,	/* e breve */
	0x00,	/* E abovering */
	0x00,	/* e abovering */
	0xca,	/* E ogonek */
	0xea,	/* e ogonek */
	0xcc,	/* E caron */
	0xec,	/* e caron */
	0x00,	/* G circumflex */
	0x00,	/* g circumflex */
	0x00,	/* G breve */
	0x00,	/* g breve */
	0x00,	/* G abovering */	/* 0120 */
	0x00,	/* g abovering */
	0x00,	/* G cedilla */
	0x00,	/* g cedilla */
	0x00,	/* H circumflex */
	0x00,	/* h circumflex */
	0x00,	/* H stroke */
	0x00,	/* h stroke */
	0x00,	/* I tilde */
	0x00,	/* i tilde */
	0x00,	/* I macron */
	0x00,	/* i macron */
	0x00,	/* I breve */
	0x00,	/* i breve */
	0x00,	/* I ogonek */
	0x00,	/* i ogonek */
	0x00,	/* dotted I */		/* 0130 */
	0x00,	/* non-dotted i */
	0x00,	/* ligature IJ */
	0x00,	/* ligature ij */
	0x00,	/* J circumflex */
	0x00,	/* j circumflex */
	0x00,	/* K cedilla */
	0x00,	/* k cedilla */
	0x00,	/* kra */
	0xc5,	/* L acute */
	0xe5,	/* l acute */
	0x00,	/* L cedilla */
	0x00,	/* l cedilla */
	0xa5,	/* L caron */
	0xb5,	/* l caron */
	0x00,	/* L middle dot */
	0x00,	/* l middle dot */	/* 0140 */
	0xa3,	/* L stroke */
	0xb3,	/* l stroke */
	0xd1,	/* N acute */
	0xf1,	/* n acute */
	0x00,	/* N cedilla */
	0x00,	/* n cedilla */
	0xd2,	/* N caron */
	0xf2,	/* n caron */
	0x00,	/* N preceded by apostrophe */
	0x00,	/* ENG */
	0x00,	/* eng */
	0x00,	/* O macron */
	0x00,	/* o macron */
	0x00,	/* O breve */
	0x00,	/* o breve */
	0xd5,	/* O double acute */	/* 0150 */
	0xf5,	/* o double acute */
	0x00,	/* ligature OE */
	0x00,	/* ligature oe */
	0xc0,	/* R acute */
	0xe0,	/* r acute */
	0x00,	/* R cedilla */
	0x00,	/* r cedilla */
	0xd8,	/* R caron */
	0xf8,	/* r caron */
	0xa6,	/* S acute */
	0xb6,	/* s acute */
	0x00,	/* S circumflex */
	0x00,	/* s circumflex */
	0xaa,	/* S cedilla */
	0xba,	/* s cedilla */
	0xa9,	/* S caron */		/* 0160 */
	0xb9,	/* s caron */
	0xde,	/* T cedilla */
	0xfe,	/* t cedilla */
	0xab,	/* T caron */
	0xbb,	/* t caron */
	0x00,	/* T stroke */
	0x00,	/* t stroke */
	0x00,	/* U tilde */
	0x00,	/* u tilde */
	0x00,	/* U macron */
	0x00,	/* u macron */
	0x00,	/* U breve */
	0x00,	/* u breve */
	0xd9,	/* U abovering */
	0xf9,	/* u abovering */
	0xdb,	/* U double acute */	/* 0170 */
	0xfb,	/* u double acute */
	0x00,	/* U ogonek */
	0x00,	/* u ogonek */
	0x00,	/* W circumflex */
	0x00,	/* w circumflex */
	0x00,	/* Y circumflex */
	0x00,	/* y circumflex */
	0x00,	/* Y diaeresis */
	0xac,	/* Z acute */
	0xbc,	/* z acute */
	0xaf,	/* Z abovering */
	0xbf,	/* z abovering */
	0xae,	/* Z caron */
	0xbe,	/* z caron */
	0x00	/* long s */
};

/*
 * Baltic to Latin-7 translation table.
 */
const u_int8_t unicode_to_latin7[] = {
	0xc2,	/* A macron */		/* 0100 */
	0xe2,	/* a macron */
	0x00,	/* A breve */
	0x00,	/* a breve */
	0xc0,	/* A ogonek */
	0xe0,	/* a ogonek */
	0xc3,	/* C acute */
	0xe3,	/* c acute */
	0x00,	/* C circumflex */
	0x00,	/* c circumflex */
	0x00,	/* C abovering */
	0x00,	/* c abovering */
	0xc8,	/* C caron */
	0xe8,	/* c caron */
	0x00,	/* D caron */
	0x00,	/* d caron */
	0x00,	/* D stroke */		/* 0110 */
	0x00,	/* d stroke */
	0xc7,	/* E macron */
	0xe7,	/* e macron */
	0x00,	/* E breve */
	0x00,	/* e breve */
	0xcb,	/* E abovering */
	0xeb,	/* e abovering */
	0xc6,	/* E ogonek */
	0xe6,	/* e ogonek */
	0x00,	/* E caron */
	0x00,	/* e caron */
	0x00,	/* G circumflex */
	0x00,	/* g circumflex */
	0x00,	/* G breve */
	0x00,	/* g breve */
	0x00,	/* G abovering */	/* 0120 */
	0x00,	/* g abovering */
	0xcc,	/* G cedilla */
	0xec,	/* g cedilla */
	0x00,	/* H circumflex */
	0x00,	/* h circumflex */
	0x00,	/* H stroke */
	0x00,	/* h stroke */
	0x00,	/* I tilde */
	0x00,	/* i tilde */
	0xce,	/* I macron */
	0xee,	/* i macron */
	0x00,	/* I breve */
	0x00,	/* i breve */
	0xc1,	/* I ogonek */
	0xe1,	/* i ogonek */
	0x00,	/* dotted I */		/* 0130 */
	0x00,	/* non-dotted I */
	0x00,	/* ligature IJ */
	0x00,	/* ligature ij */
	0x00,	/* J circumflex */
	0x00,	/* j circumflex */
	0xcd,	/* K cedilla */
	0xed,	/* k cedilla */
	0x00,	/* kra */
	0x00,	/* L acute */
	0x00,	/* l acute */
	0xcf,	/* L cedilla */
	0xef,	/* l cedilla */
	0x00,	/* L caron */
	0x00,	/* l caron */
	0x00,	/* L middle dot */
	0x00,	/* l middle dot */	/* 0140 */
	0xd9,	/* L stroke */
	0xf9,	/* l stroke */
	0xd1,	/* N acute */
	0xf1,	/* n acute */
	0xd2,	/* N cedilla */
	0xf2,	/* n cedilla */
	0x00,	/* N caron */
	0x00,	/* n caron */
	0x00,	/* N preceded by apostrophe */
	0x00,	/* ENG */
	0x00,	/* eng */
	0xd4,	/* O macron */
	0xf4,	/* o macron */
	0x00,	/* O breve */
	0x00,	/* o breve */
	0x00,	/* O double acute */	/* 0150 */
	0x00,	/* o double acute */
	0x00,	/* ligature OE */
	0x00,	/* ligature oe */
	0x00,	/* R acute */
	0x00,	/* r acute */
	0xaa,	/* R cedilla */
	0xba,	/* r cedilla */
	0x00,	/* R caron */
	0x00,	/* r caron */
	0xda,	/* S acute */
	0xfa,	/* s acute */
	0x00,	/* S circumflex */
	0x00,	/* s circumflex */
	0x00,	/* S cedilla */
	0x00,	/* s cedilla */
	0xd0,	/* S caron */		/* 0160 */
	0xf0,	/* s caron */
	0x00,	/* T cedilla */
	0x00,	/* t cedilla */
	0x00,	/* T caron */
	0x00,	/* t caron */
	0x00,	/* T stroke */
	0x00,	/* t stroke */
	0x00,	/* U tilde */
	0x00,	/* u tilde */
	0xdb,	/* U macron */
	0xfb,	/* u macron */
	0x00,	/* U breve */
	0x00,	/* u breve */
	0x00,	/* U abovering */
	0x00,	/* u abovering */
	0x00,	/* U double acute */	/* 0170 */
	0x00,	/* u double acute */
	0xd8,	/* U ogonek */
	0xf8,	/* u ogonek */
	0x00,	/* W circumflex */
	0x00,	/* w circumflex */
	0x00,	/* Y circumflex */
	0x00,	/* y circumflex */
	0x00,	/* Y diaeresis */
	0xca,	/* Z acute */
	0xea,	/* z acute */
	0xdd,	/* Z abovering */
	0xfd,	/* z abovering */
	0xde,	/* Z caron */
	0xfe,	/* z caron */
	0x00	/* long s */
};

/*
 * Keysym to local 8-bit charset sequence translation function.
 * The out buffer is at least one character long.
 * The keyboard layout is used as a hint to decide which latin charset to
 * assume.
 */
int
wsemul_local_translate(u_int32_t unisym, kbd_t layout, u_char *out)
{
	switch (unisym >> 7) {
	case 0x0080 >> 7:
		switch (KB_ENCODING(layout)) {
		case KB_LT:
		case KB_LV:
			switch (unisym) {
			case KS_L7_AE:
				unisym = 0xaf;
				break;
			case KS_L7_Ostroke:
				unisym = 0xa8;
				break;
			case KS_L7_ae:
				unisym = 0xbf;
				break;
			case KS_L7_ostroke:
				unisym = 0xb8;
				break;
			}
		}
		break;

	case 0x0100 >> 7:
		switch (KB_ENCODING(layout)) {
		case KB_LT:
		case KB_LV:
			if (unisym < 0x100 + nitems(unicode_to_latin7) &&
			    unicode_to_latin7[unisym - 0x100] != 0)
				unisym = unicode_to_latin7[unisym - 0x100];
			break;
		case KB_TR:
			switch (unisym) {
			case KS_L5_Gbreve:
				unisym = 0xd0;
				break;
			case KS_L5_gbreve:
				unisym = 0xf0;
				break;
			case KS_L5_Idotabove:
				unisym = 0xdd;
				break;
			case KS_L5_idotless:
				unisym = 0xfd;
				break;
			case KS_L5_Scedilla:
				unisym = 0xde;
				break;
			case KS_L5_scedilla:
				unisym = 0xfe;
				break;
			}
			break;
		case KB_PL:
		case KB_SI:
			if (unisym < 0x100 + nitems(unicode_to_latin2) &&
			    unicode_to_latin2[unisym - 0x100] != 0)
				unisym = unicode_to_latin2[unisym - 0x100];
			break;
		}
		break;

	case 0x0280 >> 7:
		switch (KB_ENCODING(layout)) {
		case KB_PL:
		case KB_SI:
			switch (unisym) {
			case KS_L2_caron:
				unisym = 0xb7;
				break;
			case KS_L2_breve:
				unisym = 0xa2;
				break;
			case KS_L2_dotabove:
				unisym = 0xff;
				break;
			case KS_L2_ogonek:
				unisym = 0xb2;
				break;
			case KS_L2_dblacute:
				unisym = 0xbd;
				break;
			}
			break;
		}
		break;

	case 0x0400 >> 7:
		if (unisym < 0x400 +
		    sizeof(cyrillic_to_koi8) / sizeof(cyrillic_to_koi8[0]) &&
		    cyrillic_to_koi8[unisym - 0x400] != 0)
			unisym = cyrillic_to_koi8[unisym - 0x400];
		break;
	case 0x0480 >> 7:
		if (unisym == KS_Cyrillic_GHEUKR)
			unisym = 0xbd;	/* ukrainian GHE */
		else if (unisym == KS_Cyrillic_gheukr)
			unisym = 0xad;	/* ukrainian ghe */
		break;

	case 0x2000 >> 7:
		switch (KB_ENCODING(layout)) {
		case KB_LT:
		case KB_LV:
			switch (unisym) {
			case KS_L7_rightsnglquot:
				unisym = 0xff;
				break;
			case KS_L7_leftdblquot:
				unisym = 0xb4;
				break;
			case KS_L7_rightdblquot:
				unisym = 0xa1;
				break;
			case KS_L7_dbllow9quot:
				unisym = 0xa5;
				break;
			}
		}
		break;

	}

	out[0] = unisym & 0xff;
	return (1);
}

/*
 * Keysym to UTF-8 sequence translation function.
 * The out buffer is at least 4 characters long.
 */
int
wsemul_utf8_translate(u_int32_t unisym, kbd_t layout, u_char *out,
    int allow_utf8)
{
#ifndef HAVE_UTF8_SUPPORT
	return (wsemul_local_translate(unisym, layout, out));
#else
	u_int pos, length, headpat;

	if (!allow_utf8)
		return wsemul_local_translate(unisym, layout, out);

	if (unisym < 0x80) {
		/* Fast path for plain ASCII characters. */
		*out = (u_char)unisym;
		return 1;
	}

	if (unisym < 0x800) {
		headpat = 0xc0;
		length = 2;
	} else if (unisym < 0x10000) {
		if (unisym >= 0xd800 && unisym < 0xe000)
			return 0;
		headpat = 0xe0;
		length = 3;
	} else {
		if (unisym >= 0x110000)
			return 0;
		headpat = 0xf0;
		length = 4;
	}

	for (pos = length - 1; pos > 0; pos--) {
		out[pos] = 0x80 | (unisym & 0x3f);
		unisym >>= 6;
	}
	out[0] = headpat | unisym;

	return length;
#endif
}
