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

#include <kunit/visibility.h>

#include "hfsplus_fs.h"
#include "hfsplus_raw.h"
#include "unicode.h"

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

	if (len1 > HFSPLUS_MAX_STRLEN) {
		len1 = HFSPLUS_MAX_STRLEN;
		pr_err("invalid length %u has been corrected to %d\n",
			be16_to_cpu(s1->length), len1);
	}

	if (len2 > HFSPLUS_MAX_STRLEN) {
		len2 = HFSPLUS_MAX_STRLEN;
		pr_err("invalid length %u has been corrected to %d\n",
			be16_to_cpu(s2->length), len2);
	}

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
EXPORT_SYMBOL_IF_KUNIT(hfsplus_strcasecmp);

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

	if (len1 > HFSPLUS_MAX_STRLEN) {
		len1 = HFSPLUS_MAX_STRLEN;
		pr_err("invalid length %u has been corrected to %d\n",
			be16_to_cpu(s1->length), len1);
	}

	if (len2 > HFSPLUS_MAX_STRLEN) {
		len2 = HFSPLUS_MAX_STRLEN;
		pr_err("invalid length %u has been corrected to %d\n",
			be16_to_cpu(s2->length), len2);
	}

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
EXPORT_SYMBOL_IF_KUNIT(hfsplus_strcmp);

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

/*
 * In HFS+, a filename can contain / because : is the separator.
 * The slash is a valid filename character on macOS.
 * But on Linux, / is the path separator and
 * it cannot appear in a filename component.
 * There's a parallel mapping for the NUL character (0 -> U+2400).
 * NUL terminates strings in C/POSIX but is valid in HFS+ filenames.
 */
static inline
void hfsplus_mac2linux_compatibility_check(u16 symbol, u16 *conversion,
					   int name_type)
{
	*conversion = symbol;

	switch (name_type) {
	case HFS_XATTR_NAME:
		/* ignore conversion */
		return;

	default:
		/* continue logic */
		break;
	}

	switch (symbol) {
	case 0:
		*conversion = 0x2400;
		break;
	case '/':
		*conversion = ':';
		break;
	}
}

static int hfsplus_uni2asc(struct super_block *sb,
			   const struct hfsplus_unistr *ustr,
			   int max_len, char *astr, int *len_p,
			   int name_type)
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
	if (ustrlen > max_len) {
		ustrlen = max_len;
		pr_err("invalid length %u has been corrected to %d\n",
			be16_to_cpu(ustr->length), ustrlen);
	}

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
			hfsplus_mac2linux_compatibility_check(c0, &c0,
							      name_type);
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
		hfsplus_mac2linux_compatibility_check(c0, &cc,
						      name_type);
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

inline int hfsplus_uni2asc_str(struct super_block *sb,
			       const struct hfsplus_unistr *ustr, char *astr,
			       int *len_p)
{
	return hfsplus_uni2asc(sb,
				ustr, HFSPLUS_MAX_STRLEN,
				astr, len_p,
				HFS_REGULAR_NAME);
}
EXPORT_SYMBOL_IF_KUNIT(hfsplus_uni2asc_str);

inline int hfsplus_uni2asc_xattr_str(struct super_block *sb,
				     const struct hfsplus_attr_unistr *ustr,
				     char *astr, int *len_p)
{
	return hfsplus_uni2asc(sb, (const struct hfsplus_unistr *)ustr,
				HFSPLUS_ATTR_MAX_STRLEN, astr, len_p,
				HFS_XATTR_NAME);
}
EXPORT_SYMBOL_IF_KUNIT(hfsplus_uni2asc_xattr_str);

/*
 * In HFS+, a filename can contain / because : is the separator.
 * The slash is a valid filename character on macOS.
 * But on Linux, / is the path separator and
 * it cannot appear in a filename component.
 * There's a parallel mapping for the NUL character (0 -> U+2400).
 * NUL terminates strings in C/POSIX but is valid in HFS+ filenames.
 */
static inline
void hfsplus_linux2mac_compatibility_check(wchar_t *uc, int name_type)
{
	switch (name_type) {
	case HFS_XATTR_NAME:
		/* ignore conversion */
		return;

	default:
		/* continue logic */
		break;
	}

	switch (*uc) {
	case 0x2400:
		*uc = 0;
		break;
	case ':':
		*uc = '/';
		break;
	}
}

/*
 * Convert one or more ASCII characters into a single unicode character.
 * Returns the number of ASCII characters corresponding to the unicode char.
 */
static inline int asc2unichar(struct super_block *sb, const char *astr, int len,
			      wchar_t *uc, int name_type)
{
	int size = HFSPLUS_SB(sb)->nls->char2uni(astr, len, uc);

	if (size <= 0) {
		*uc = '?';
		size = 1;
	}

	hfsplus_linux2mac_compatibility_check(uc, name_type);
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
	if (!result) {
		/*
		 * Not every character with a canonical decomposition is in
		 * Apple Technote #1150's own table above; a small, fixed
		 * set was only added to the decomposition standard (or had
		 * its decomposition corrected) after that table was
		 * generated. hfsplus_legacy_decompose() covers those.
		 */
		const u16 *legacy = hfsplus_legacy_decompose(uc, size);

		if (legacy)
			result = memcpy(hangul_buffer, legacy,
					*size * sizeof(*legacy));
	}
	return result;
}

/*
 * Apply the Unicode Canonical Ordering Algorithm to a decomposed name held
 * as plain host-order code units: within each maximal run of characters
 * that have a nonzero combining class, stable-sort the run into ascending
 * combining-class order. A character with combining class 0 always starts
 * a new run and is never itself reordered.
 *
 * hfsplus_decompose_table decomposes each source character on its own; it
 * says nothing about how the decompositions of two different source
 * characters should be ordered relative to each other when both produce
 * combining marks that end up adjacent. Without this pass, such a
 * sequence can be stored in an order that macOS's own fsck_hfs
 * (FixDecomps() in CatalogCheck.c) considers illegal, even though every
 * individual character was decomposed correctly.
 */
static void hfsplus_canonical_reorder(u16 *ustr, int len)
{
	int i;

	for (i = 1; i < len; i++) {
		u8 cls = hfsplus_combining_class(ustr[i]);
		int j = i;

		if (!cls)
			continue;

		while (j > 0) {
			u8 prev_cls = hfsplus_combining_class(ustr[j - 1]);
			u16 tmp;

			if (!prev_cls || prev_cls <= cls)
				break;

			tmp = ustr[j];
			ustr[j] = ustr[j - 1];
			ustr[j - 1] = tmp;
			j--;
		}
	}
}

#define HFSPLUS_HANGUL_MAX_JAMO		(3)       /* L + V + optional T */

static_assert(HFSPLUS_HANGUL_MAX_JAMO >= HFSPLUS_LEGACY_DECOMP_MAX_LEN,
		"hangul_buffer must fit the longest legacy decomposition too");

/*
 * Decompose and canonically reorder an entire Linux name, as plain
 * host-order code units. hfsplus_asc2uni(), hfsplus_hash_dentry() and
 * hfsplus_compare_dentry() all go through this so that storage, hashing
 * and comparison always agree on what a given name canonicalizes to.
 *
 * @out must hold at least @max_len entries, which must not exceed
 * HFSPLUS_MAX_STRLEN. Returns the number of code units written. If
 * @consumed is non-NULL, it is set to the number of input bytes actually
 * consumed, which is less than @len when @out fills up first.
 */
static int hfsplus_decompose_str(struct super_block *sb, const char *astr,
				  int len, int max_len, int name_type,
				  u16 *out, int *consumed)
{
	int decompose = !test_bit(HFSPLUS_SB_NODECOMPOSE, &HFSPLUS_SB(sb)->flags);
	const char *start = astr;
	int outlen = 0;

	while (outlen < max_len && len > 0) {
		u16 *dstr;
		u16 dhangul[HFSPLUS_HANGUL_MAX_JAMO];
		int dsize, size;
		wchar_t c;

		size = asc2unichar(sb, astr, len, &c, name_type);

		dstr = decompose ? decompose_unichar(c, &dsize, dhangul) : NULL;
		if (dstr) {
			if (outlen + dsize > max_len)
				break;
			do {
				out[outlen++] = *dstr++;
			} while (--dsize > 0);
		} else {
			out[outlen++] = c;
		}

		astr += size;
		len -= size;
	}

	hfsplus_canonical_reorder(out, outlen);
	hfsplus_fixup_legacy_sequences(out, &outlen);
	if (consumed)
		*consumed = astr - start;
	return outlen;
}

int hfsplus_asc2uni(struct super_block *sb,
		    struct hfsplus_unistr *ustr, int max_unistr_len,
		    const char *astr, int len, int name_type)
{
	u16 buf[HFSPLUS_MAX_STRLEN];
	int outlen, i, consumed;

	if (max_unistr_len > HFSPLUS_MAX_STRLEN)
		max_unistr_len = HFSPLUS_MAX_STRLEN;

	outlen = hfsplus_decompose_str(sb, astr, len, max_unistr_len,
				       name_type, buf, &consumed);
	for (i = 0; i < outlen; i++)
		ustr->unicode[i] = cpu_to_be16(buf[i]);
	ustr->length = cpu_to_be16(outlen);

	if (consumed < len)
		return -ENAMETOOLONG;
	return 0;
}
EXPORT_SYMBOL_IF_KUNIT(hfsplus_asc2uni);

/*
 * Maximum length of a single maximal run of nonzero-combining-class code
 * units that hfsplus_decompose_iter_next() below will canonically
 * reorder. Every code unit with combining class 0 starts a new run, so
 * this only bounds how many *consecutive* combining marks between two
 * base characters get sorted - real text, and even deliberately
 * adversarial "Zalgo" text, essentially never approaches this. Keeping
 * it small means hfsplus_hash_dentry() and hfsplus_compare_dentry() only
 * ever need a tiny amount of lookahead state, rather than buffering an
 * entire (up to 255-unit) name.
 */
#define HFSPLUS_CCC_RUN_MAX 32

/*
 * Iterator that produces the canonically-ordered, decomposed code units
 * of a Linux name one at a time, without materializing the whole name.
 * hfsplus_hash_dentry() and hfsplus_compare_dentry() use this so that
 * hashing and comparison always agree with what hfsplus_asc2uni() would
 * actually store, while still comparing lazily (stopping at the first
 * difference) the way this code did before canonical reordering existed.
 */
struct hfsplus_decompose_iter {
	struct super_block *sb;
	const char *astr;
	int len;
	int name_type;
	int decompose;

	u16 run[HFSPLUS_CCC_RUN_MAX];
	int run_len;
	int run_pos;
};

static void hfsplus_decompose_iter_init(struct hfsplus_decompose_iter *it,
					struct super_block *sb,
					const char *astr, int len,
					int name_type)
{
	it->sb = sb;
	it->astr = astr;
	it->len = len;
	it->name_type = name_type;
	it->decompose = !test_bit(HFSPLUS_SB_NODECOMPOSE, &HFSPLUS_SB(sb)->flags);
	it->run_len = 0;
	it->run_pos = 0;
}

/*
 * Gather the next maximal run of nonzero-combining-class code units
 * (starting with whatever character comes next, base or not) and
 * canonically reorder just that run. A character is only included once
 * we know the class of the first unit it produces, so decoding it is
 * speculative until that's decided.
 */
static bool hfsplus_decompose_iter_refill(struct hfsplus_decompose_iter *it)
{
	it->run_pos = 0;
	it->run_len = 0;

	while (it->len > 0) {
		u16 *dstr;
		u16 dhangul[HFSPLUS_HANGUL_MAX_JAMO];
		int dsize, size;
		wchar_t c;

		size = asc2unichar(it->sb, it->astr, it->len, &c,
				   it->name_type);

		dstr = it->decompose ?
			decompose_unichar(c, &dsize, dhangul) : NULL;
		if (!dstr) {
			dhangul[0] = c;
			dstr = dhangul;
			dsize = 1;
		}

		if (it->run_len > 0 && !hfsplus_combining_class(dstr[0]))
			break;

		if (it->run_len + dsize > HFSPLUS_CCC_RUN_MAX)
			break;

		it->astr += size;
		it->len -= size;

		do {
			it->run[it->run_len++] = *dstr++;
		} while (--dsize > 0);
	}

	hfsplus_canonical_reorder(it->run, it->run_len);
	hfsplus_fixup_legacy_sequences(it->run, &it->run_len);
	return it->run_len > 0;
}

/* Returns the next code unit, or a negative value once @it is exhausted. */
static int hfsplus_decompose_iter_next(struct hfsplus_decompose_iter *it)
{
	if (it->run_pos >= it->run_len && !hfsplus_decompose_iter_refill(it))
		return -1;
	return it->run[it->run_pos++];
}

/*
 * Returns the next code unit that matters for comparison/hashing: folded
 * if @casefold, with any character folding to 0 ("ignorable") skipped
 * entirely. Returns a negative value once @it is exhausted.
 */
static int hfsplus_decompose_iter_next_folded(struct hfsplus_decompose_iter *it,
					      int casefold)
{
	int c;

	do {
		c = hfsplus_decompose_iter_next(it);
		if (c < 0)
			return -1;
		if (casefold)
			c = case_fold(c);
	} while (casefold && !c);

	return c;
}

/*
 * Hash a string to an integer as appropriate for the HFS+ filesystem.
 * Composed unicode characters are decomposed and case-folding is performed
 * if the appropriate bits are (un)set on the superblock.
 */
int hfsplus_hash_dentry(const struct dentry *dentry, struct qstr *str)
{
	struct super_block *sb = dentry->d_sb;
	int casefold = test_bit(HFSPLUS_SB_CASEFOLD, &HFSPLUS_SB(sb)->flags);
	struct hfsplus_decompose_iter it;
	unsigned long hash;
	int c;

	hfsplus_decompose_iter_init(&it, sb, str->name, str->len,
				    HFS_REGULAR_NAME);

	hash = init_name_hash(dentry);
	while ((c = hfsplus_decompose_iter_next_folded(&it, casefold)) >= 0)
		hash = partial_name_hash(c, hash);
	str->hash = end_name_hash(hash);

	return 0;
}
EXPORT_SYMBOL_IF_KUNIT(hfsplus_hash_dentry);

/*
 * Compare strings with HFS+ filename ordering.
 * Composed unicode characters are decomposed and case-folding is performed
 * if the appropriate bits are (un)set on the superblock.
 */
int hfsplus_compare_dentry(const struct dentry *dentry,
		unsigned int len, const char *str, const struct qstr *name)
{
	struct super_block *sb = dentry->d_sb;
	int casefold = test_bit(HFSPLUS_SB_CASEFOLD, &HFSPLUS_SB(sb)->flags);
	struct hfsplus_decompose_iter it1, it2;

	hfsplus_decompose_iter_init(&it1, sb, str, len, HFS_REGULAR_NAME);
	hfsplus_decompose_iter_init(&it2, sb, name->name, name->len,
				    HFS_REGULAR_NAME);

	while (1) {
		int c1 = hfsplus_decompose_iter_next_folded(&it1, casefold);
		int c2 = hfsplus_decompose_iter_next_folded(&it2, casefold);

		if (c1 < 0 || c2 < 0)
			return c1 == c2 ? 0 : (c1 < 0 ? -1 : 1);
		if (c1 != c2)
			return c1 < c2 ? -1 : 1;
	}
}
EXPORT_SYMBOL_IF_KUNIT(hfsplus_compare_dentry);
