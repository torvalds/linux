// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/hfsplus/unicode.c
 *
 * Copyright (C) 2001
 * Brad Boyer (flar@allandria.com)
 * (C) 2003 Ardis Technologies <roman@ardistech.com>
 *
 * Handler routines for unicode strings
 */

#include <linux/types.h>
#include <linux/nls.h>
#include "hfsplus_fs.h"
#include "hfsplus_raw.h"

/* Fold the case of a unicode char, given the 16 bit value */
/* Returns folded char, or 0 if ignorable */
static inline u16 case_fold(u16 c)
{
	u16 tmp;

	tmp = hfsplus_case_fold_table[c >> 8];
	if (tmp)
		tmp = hfsplus_case_fold_table[tmp + (c & 0xff)];
	else
		tmp = c;
	return tmp;
}

/* Compare unicode strings, return values like normal strcmp */
int hfsplus_strcasecmp(const struct hfsplus_unistr *s1,
		       const struct hfsplus_unistr *s2)
{
	u16 len1, len2, c1, c2;
	const hfsplus_unichr *p1, *p2;

	len1 = be16_to_cpu(s1->length);
	len2 = be16_to_cpu(s2->length);
	p1 = s1->unicode;
	p2 = s2->unicode;

	while (1) {
		c1 = c2 = 0;

		while (len1 && !c1) {
			c1 = case_fold(be16_to_cpu(*p1));
			p1++;
			len1--;
		}
		while (len2 && !c2) {
			c2 = case_fold(be16_to_cpu(*p2));
			p2++;
			len2--;
		}

		if (c1 != c2)
			return (c1 < c2) ? -1 : 1;
		if (!c1 && !c2)
			return 0;
	}
}

/* Compare names as a sequence of 16-bit unsigned integers */
int hfsplus_strcmp(const struct hfsplus_unistr *s1,
		   const struct hfsplus_unistr *s2)
{
	u16 len1, len2, c1, c2;
	const hfsplus_unichr *p1, *p2;
	int len;

	len1 = be16_to_cpu(s1->length);
	len2 = be16_to_cpu(s2->length);
	p1 = s1->unicode;
	p2 = s2->unicode;

	for (len = min(len1, len2); len > 0; len--) {
		c1 = be16_to_cpu(*p1);
		c2 = be16_to_cpu(*p2);
		if (c1 != c2)
			return c1 < c2 ? -1 : 1;
		p1++;
		p2++;
	}

	return len1 < len2 ? -1 :
	       len1 > len2 ? 1 : 0;
}


#define Hangul_SBase	0xac00
#define Hangul_LBase	0x1100
#define Hangul_VBase	0x1161
#define Hangul_TBase	0x11a7
#define Hangul_SCount	11172
#define Hangul_LCount	19
#define Hangul_VCount	21
#define Hangul_TCount	28
#define Hangul_NCount	(Hangul_VCount * Hangul_TCount)


static u16 *hfsplus_compose_lookup(u16 *p, u16 cc)
{
	int i, s, e;

	s = 1;
	e = p[1];
	if (!e || cc < p[s * 2] || cc > p[e * 2])
		return NULL;
	do {
		i = (s + e) / 2;
		if (cc > p[i * 2])
			s = i + 1;
		else if (cc < p[i * 2])
			e = i - 1;
		else
			return hfsplus_compose_table + p[i * 2 + 1];
	} while (s <= e);
	return NULL;
}

int hfsplus_uni2asc(struct super_block *sb,
		const struct hfsplus_unistr *ustr,
		char *astr, int *len_p)
{
	const hfsplus_unichr *ip;
	struct nls_table *nls = HFSPLUS_SB(sb)->nls;
	u8 *op;
	u16 cc, c0, c1;
	u16 *ce1, *ce2;
	int i, len, ustrlen, res, compose;

	op = astr;
	ip = ustr->unicode;
	ustrlen = be16_to_cpu(ustr->length);
	len = *len_p;
	ce1 = NULL;
	compose = !test_bit(HFSPLUS_SB_NODECOMPOSE, &HFSPLUS_SB(sb)->flags);

	while (ustrlen > 0) {
		c0 = be16_to_cpu(*ip++);
		ustrlen--;
		/* search for single decomposed char */
		if (likely(compose))
			ce1 = hfsplus_compose_lookup(hfsplus_compose_table, c0);
		if (ce1)
			cc = ce1[0];
		else
			cc = 0;
		if (cc) {
			/* start of a possibly decomposed Hangul char */
			if (cc != 0xffff)
				goto done;
			if (!ustrlen)
				goto same;
			c1 = be16_to_cpu(*ip) - Hangul_VBase;
			if (c1 < Hangul_VCount) {
				/* compose the Hangul char */
				cc = (c0 - Hangul_LBase) * Hangul_VCount;
				cc = (cc + c1) * Hangul_TCount;
				cc += Hangul_SBase;
				ip++;
				ustrlen--;
				if (!ustrlen)
					goto done;
				c1 = be16_to_cpu(*ip) - Hangul_TBase;
				if (c1 > 0 && c1 < Hangul_TCount) {
					cc += c1;
					ip++;
					ustrlen--;
				}
				goto done;
			}
		}
		while (1) {
			/* main loop for common case of not composed chars */
			if (!ustrlen)
				goto same;
			c1 = be16_to_cpu(*ip);
			if (likely(compose))
				ce1 = hfsplus_compose_lookup(
					hfsplus_compose_table, c1);
			if (ce1)
				break;
			switch (c0) {
			case 0:
				c0 = 0x2400;
				break;
			case '/':
				c0 = ':';
				break;
			}
			res = nls->uni2char(c0, op, len);
			if (res < 0) {
				if (res == -ENAMETOOLONG)
					goto out;
				*op = '?';
				res = 1;
			}
			op += res;
			len -= res;
			c0 = c1;
			ip++;
			ustrlen--;
		}
		ce2 = hfsplus_compose_lookup(ce1, c0);
		if (ce2) {
			i = 1;
			while (i < ustrlen) {
				ce1 = hfsplus_compose_lookup(ce2,
					be16_to_cpu(ip[i]));
				if (!ce1)
					break;
				i++;
				ce2 = ce1;
			}
			cc = ce2[0];
			if (cc) {
				ip += i;
				ustrlen -= i;
				goto done;
			}
		}
same:
		switch (c0) {
		case 0:
			cc = 0x2400;
			break;
		case '/':
			cc = ':';
			break;
		default:
			cc = c0;
		}
done:
		res = nls->uni2char(cc, op, len);
		if (res < 0) {
			if (res == -ENAMETOOLONG)
				goto out;
			*op = '?';
			res = 1;
		}
		op += res;
		len -= res;
	}
	res = 0;
out:
	*len_p = (char *)op - astr;
	return res;
}

/*
 * Convert one or more ASCII characters into a single unicode character.
 * Returns the number of ASCII characters corresponding to the unicode char.
 */
static inline int asc2unichar(struct super_block *sb, const char *astr, int len,
			      wchar_t *uc)
{
	int size = HFSPLUS_SB(sb)->nls->char2uni(astr, len, uc);
	if (size <= 0) {
		*uc = '?';
		size = 1;
	}
	switch (*uc) {
	case 0x2400:
		*uc = 0;
		break;
	case ':':
		*uc = '/';
		break;
	}
	return size;
}

/* Decomposes a non-Hangul unicode character. */
static u16 *hfsplus_decompose_nonhangul(wchar_t uc, int *size)
{
	int off;

	off = hfsplus_decompose_table[(uc >> 12) & 0xf];
	if (off == 0 || off == 0xffff)
		return NULL;

	off = hfsplus_decompose_table[off + ((uc >> 8) & 0xf)];
	if (!off)
		return NULL;

	off = hfsplus_decompose_table[off + ((uc >> 4) & 0xf)];
	if (!off)
		return NULL;

	off = hfsplus_decompose_table[off + (uc & 0xf)];
	*size = off & 3;
	if (*size == 0)
		return NULL;
	return hfsplus_decompose_table + (off / 4);
}

/*
 * Try to decompose a unicode character as Hangul. Return 0 if @uc is not
 * precomposed Hangul, otherwise return the length of the decomposition.
 *
 * This function was adapted from sample code from the Unicode Standard
 * Annex #15: Unicode Normalization Forms, version 3.2.0.
 *
 * Copyright (C) 1991-2018 Unicode, Inc.  All rights reserved.  Distributed
 * under the Terms of Use in http://www.unicode.org/copyright.html.
 */
static int hfsplus_try_decompose_hangul(wchar_t uc, u16 *result)
{
	int index;
	int l, v, t;

	index = uc - Hangul_SBase;
	if (index < 0 || index >= Hangul_SCount)
		return 0;

	l = Hangul_LBase + index / Hangul_NCount;
	v = Hangul_VBase + (index % Hangul_NCount) / Hangul_TCount;
	t = Hangul_TBase + index % Hangul_TCount;

	result[0] = l;
	result[1] = v;
	if (t != Hangul_TBase) {
		result[2] = t;
		return 3;
	}
	return 2;
}

/* Decomposes a single unicode character. */
static u16 *decompose_unichar(wchar_t uc, int *size, u16 *hangul_buffer)
{
	u16 *result;

	/* Hangul is handled separately */
	result = hangul_buffer;
	*size = hfsplus_try_decompose_hangul(uc, result);
	if (*size == 0)
		result = hfsplus_decompose_nonhangul(uc, size);
	return result;
}

int hfsplus_asc2uni(struct super_block *sb,
		    struct hfsplus_unistr *ustr, int max_unistr_len,
		    const char *astr, int len)
{
	int size, dsize, decompose;
	u16 *dstr, outlen = 0;
	wchar_t c;
	u16 dhangul[3];

	decompose = !test_bit(HFSPLUS_SB_NODECOMPOSE, &HFSPLUS_SB(sb)->flags);
	while (outlen < max_unistr_len && len > 0) {
		size = asc2unichar(sb, astr, len, &c);

		if (decompose)
			dstr = decompose_unichar(c, &dsize, dhangul);
		else
			dstr = NULL;
		if (dstr) {
			if (outlen + dsize > max_unistr_len)
				break;
			do {
				ustr->unicode[outlen++] = cpu_to_be16(*dstr++);
			} while (--dsize > 0);
		} else
			ustr->unicode[outlen++] = cpu_to_be16(c);

		astr += size;
		len -= size;
	}
	ustr->length = cpu_to_be16(outlen);
	if (len > 0)
		return -ENAMETOOLONG;
	return 0;
}

/*
 * Hash a string to an integer as appropriate for the HFS+ filesystem.
 * Composed unicode characters are decomposed and case-folding is performed
 * if the appropriate bits are (un)set on the superblock.
 */
int hfsplus_hash_dentry(const struct dentry *dentry, struct qstr *str)
{
	struct super_block *sb = dentry->d_sb;
	const char *astr;
	const u16 *dstr;
	int casefold, decompose, size, len;
	unsigned long hash;
	wchar_t c;
	u16 c2;
	u16 dhangul[3];

	casefold = test_bit(HFSPLUS_SB_CASEFOLD, &HFSPLUS_SB(sb)->flags);
	decompose = !test_bit(HFSPLUS_SB_NODECOMPOSE, &HFSPLUS_SB(sb)->flags);
	hash = init_name_hash(dentry);
	astr = str->name;
	len = str->len;
	while (len > 0) {
		int uninitialized_var(dsize);
		size = asc2unichar(sb, astr, len, &c);
		astr += size;
		len -= size;

		if (decompose)
			dstr = decompose_unichar(c, &dsize, dhangul);
		else
			dstr = NULL;
		if (dstr) {
			do {
				c2 = *dstr++;
				if (casefold)
					c2 = case_fold(c2);
				if (!casefold || c2)
					hash = partial_name_hash(c2, hash);
			} while (--dsize > 0);
		} else {
			c2 = c;
			if (casefold)
				c2 = case_fold(c2);
			if (!casefold || c2)
				hash = partial_name_hash(c2, hash);
		}
	}
	str->hash = end_name_hash(hash);

	return 0;
}

/*
 * Compare strings with HFS+ filename ordering.
 * Composed unicode characters are decomposed and case-folding is performed
 * if the appropriate bits are (un)set on the superblock.
 */
int hfsplus_compare_dentry(const struct dentry *dentry,
		unsigned int len, const char *str, const struct qstr *name)
{
	struct super_block *sb = dentry->d_sb;
	int casefold, decompose, size;
	int dsize1, dsize2, len1, len2;
	const u16 *dstr1, *dstr2;
	const char *astr1, *astr2;
	u16 c1, c2;
	wchar_t c;
	u16 dhangul_1[3], dhangul_2[3];

	casefold = test_bit(HFSPLUS_SB_CASEFOLD, &HFSPLUS_SB(sb)->flags);
	decompose = !test_bit(HFSPLUS_SB_NODECOMPOSE, &HFSPLUS_SB(sb)->flags);
	astr1 = str;
	len1 = len;
	astr2 = name->name;
	len2 = name->len;
	dsize1 = dsize2 = 0;
	dstr1 = dstr2 = NULL;

	while (len1 > 0 && len2 > 0) {
		if (!dsize1) {
			size = asc2unichar(sb, astr1, len1, &c);
			astr1 += size;
			len1 -= size;

			if (decompose)
				dstr1 = decompose_unichar(c, &dsize1,
							  dhangul_1);
			if (!decompose || !dstr1) {
				c1 = c;
				dstr1 = &c1;
				dsize1 = 1;
			}
		}

		if (!dsize2) {
			size = asc2unichar(sb, astr2, len2, &c);
			astr2 += size;
			len2 -= size;

			if (decompose)
				dstr2 = decompose_unichar(c, &dsize2,
							  dhangul_2);
			if (!decompose || !dstr2) {
				c2 = c;
				dstr2 = &c2;
				dsize2 = 1;
			}
		}

		c1 = *dstr1;
		c2 = *dstr2;
		if (casefold) {
			c1 = case_fold(c1);
			if (!c1) {
				dstr1++;
				dsize1--;
				continue;
			}
			c2 = case_fold(c2);
			if (!c2) {
				dstr2++;
				dsize2--;
				continue;
			}
		}
		if (c1 < c2)
			return -1;
		else if (c1 > c2)
			return 1;

		dstr1++;
		dsize1--;
		dstr2++;
		dsize2--;
	}

	if (len1 < len2)
		return -1;
	if (len1 > len2)
		return 1;
	return 0;
}
