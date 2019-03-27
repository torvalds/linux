/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003 Ryuichiro Imura
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
 *
 * $FreeBSD$
 */

/*
 * kiconv(3) requires shared linked, and reduce module size
 * when statically linked.
 */

#ifdef PIC

/*
 * Why do we need quirks?
 * Since each vendors has their own Unicode mapping rules,
 * we need some quirks until iconv(3) supports them.
 * We can define Microsoft mappings here.
 * 
 * For example, the eucJP and Unocode mapping rule is based on
 * the JIS standard. Since Microsoft uses cp932 for Unicode mapping
 * which is not truly based on the JIS standard, reading a file
 * system created by Microsoft Windows family using eucJP/Unicode
 * mapping rule will cause a problem. That's why we define eucJP-ms here.
 * The eucJP-ms has been defined by The Open Group Japan Vendor Council.
 *
 * Well, Apple Mac OS also has their own Unicode mappings,
 * but we won't require these quirks here, because HFS doesn't have
 * Unicode and HFS+ has decomposed Unicode which can not be
 * handled by this xlat16 converter.
 */

#include <sys/types.h>
#include <sys/iconv.h>

#include <stdio.h>
#include <string.h>

#include "quirks.h"

/*
 * All lists of quirk character set
 */
static struct {
	int vendor; /* reserved for non MS mapping */
	const char *base_codeset, *quirk_codeset;
} quirk_list[] = {
	{ KICONV_VENDOR_MICSFT,	"eucJP", "eucJP-ms" },
	{ KICONV_VENDOR_MICSFT,	"EUC-JP", "eucJP-ms" },
	{ KICONV_VENDOR_MICSFT,	"SJIS", "SJIS-ms" },
	{ KICONV_VENDOR_MICSFT,	"Shift_JIS", "SJIS-ms" },
	{ KICONV_VENDOR_MICSFT,	"Big5", "Big5-ms" }
};

/*
 * The character list to replace for Japanese MS-Windows.
 */
static struct quirk_replace_list quirk_jis_cp932[] = {
	{ 0x00a2, 0xffe0 }, /* Cent Sign, Fullwidth Cent Sign */
	{ 0x00a3, 0xffe1 }, /* Pound Sign, Fullwidth Pound Sign */
	{ 0x00ac, 0xffe2 }, /* Not Sign, Fullwidth Not Sign */
	{ 0x2016, 0x2225 }, /* Double Vertical Line, Parallel To */
	{ 0x203e, 0x007e }, /* Overline, Tilde */
	{ 0x2212, 0xff0d }, /* Minus Sign, Fullwidth Hyphenminus */
	{ 0x301c, 0xff5e }  /* Wave Dash, Fullwidth Tilde */
};

/*
 * All entries of quirks
 */
#define	NumOf(n)	(sizeof((n)) / sizeof((n)[0]))
static struct {
	const char *quirk_codeset, *iconv_codeset, *pair_codeset;
	struct quirk_replace_list (*replace_list)[];
	size_t num_of_replaces;
} quirk_table[] = {
	{
		"eucJP-ms", "eucJP", ENCODING_UNICODE,
		(struct quirk_replace_list (*)[])&quirk_jis_cp932,
		NumOf(quirk_jis_cp932)
	},
	{
		"SJIS-ms", "CP932", ENCODING_UNICODE,
		/* XXX - quirk_replace_list should be NULL */
		(struct quirk_replace_list (*)[])&quirk_jis_cp932,
		NumOf(quirk_jis_cp932)
	},
	{
		"Big5-ms", "CP950", ENCODING_UNICODE,
		NULL, 0
	}
};


const char *
kiconv_quirkcs(const char* base, int vendor)
{
	size_t i;

	/*
	 * We should compare codeset names ignoring case here,
	 * so that quirk could be used for all of the user input
	 * patterns.
	 */
	for (i = 0; i < NumOf(quirk_list); i++)
		if (quirk_list[i].vendor == vendor &&
		    strcasecmp(quirk_list[i].base_codeset, base) == 0)
			return (quirk_list[i].quirk_codeset);

	return (base);
}

/*
 * Internal Functions
 */
const char *
search_quirk(const char *given_codeset,
	     const char *pair_codeset,
	     struct quirk_replace_list **replace_list,
	     size_t *num_of_replaces)
{
	size_t i;

	*replace_list = NULL;
	*num_of_replaces = 0;
	for (i = 0; i < NumOf(quirk_table); i++)
		if (strcmp(quirk_table[i].quirk_codeset, given_codeset) == 0) {
			if (strcmp(quirk_table[i].pair_codeset, pair_codeset) == 0) {
				*replace_list = *quirk_table[i].replace_list;
				*num_of_replaces = quirk_table[i].num_of_replaces;
			}
			return (quirk_table[i].iconv_codeset);
		}

	return (given_codeset);
}

uint16_t
quirk_vendor2unix(uint16_t c, struct quirk_replace_list *replace_list, size_t num)
{
	size_t i;

	for (i = 0; i < num; i++)
		if (replace_list[i].vendor_code == c)
			return (replace_list[i].standard_code);

	return (c);
}

uint16_t
quirk_unix2vendor(uint16_t c, struct quirk_replace_list *replace_list, size_t num)
{
	size_t i;

	for (i = 0; i < num; i++)
		if (replace_list[i].standard_code == c)
			return (replace_list[i].vendor_code);

	return (c);
}

#else /* statically linked */

#include <sys/types.h>
#include <sys/iconv.h>

const char *
kiconv_quirkcs(const char* base __unused, int vendor __unused)
{

	return (base);
}

#endif /* PIC */
