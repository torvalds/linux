/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2009 Gabor Kovesdan <gabor@FreeBSD.org>
 * Copyright (C) 2012 Oleg Moskalenko <mom040267@gmail.com>
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>

#include <errno.h>
#include <err.h>
#include <langinfo.h>
#include <limits.h>
#include <math.h>
#include <md5.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

#include "coll.h"
#include "vsort.h"

struct key_specs *keys;
size_t keys_num = 0;

wint_t symbol_decimal_point = L'.';
/* there is no default thousands separator in collate rules: */
wint_t symbol_thousands_sep = 0;
wint_t symbol_negative_sign = L'-';
wint_t symbol_positive_sign = L'+';

static int wstrcoll(struct key_value *kv1, struct key_value *kv2, size_t offset);
static int gnumcoll(struct key_value*, struct key_value *, size_t offset);
static int monthcoll(struct key_value*, struct key_value *, size_t offset);
static int numcoll(struct key_value*, struct key_value *, size_t offset);
static int hnumcoll(struct key_value*, struct key_value *, size_t offset);
static int randomcoll(struct key_value*, struct key_value *, size_t offset);
static int versioncoll(struct key_value*, struct key_value *, size_t offset);

/*
 * Allocate keys array
 */
struct keys_array *
keys_array_alloc(void)
{
	struct keys_array *ka;
	size_t sz;

	sz = keys_array_size();
	ka = sort_malloc(sz);
	memset(ka, 0, sz);

	return (ka);
}

/*
 * Calculate whether we need key hint space
 */
static size_t
key_hint_size(void)
{

	return (need_hint ? sizeof(struct key_hint) : 0);
}

/*
 * Calculate keys array size
 */
size_t
keys_array_size(void)
{

	return (keys_num * (sizeof(struct key_value) + key_hint_size()));
}

/*
 * Clean data of keys array
 */
void
clean_keys_array(const struct bwstring *s, struct keys_array *ka)
{

	if (ka) {
		for (size_t i = 0; i < keys_num; ++i) {
			const struct key_value *kv;

			kv = get_key_from_keys_array(ka, i);
			if (kv->k && kv->k != s)
				bwsfree(kv->k);
		}
		memset(ka, 0, keys_array_size());
	}
}

/*
 * Get pointer to a key value in the keys set
 */
struct key_value *
get_key_from_keys_array(struct keys_array *ka, size_t ind)
{

	return ((struct key_value *)((caddr_t)ka->key +
	    ind * (sizeof(struct key_value) + key_hint_size())));
}

/*
 * Set value of a key in the keys set
 */
void
set_key_on_keys_array(struct keys_array *ka, struct bwstring *s, size_t ind)
{

	if (ka && keys_num > ind) {
		struct key_value *kv;

		kv = get_key_from_keys_array(ka, ind);

		if (kv->k && kv->k != s)
			bwsfree(kv->k);
		kv->k = s;
	}
}

/*
 * Initialize a sort list item
 */
struct sort_list_item *
sort_list_item_alloc(void)
{
	struct sort_list_item *si;
	size_t sz;

	sz = sizeof(struct sort_list_item) + keys_array_size();
	si = sort_malloc(sz);
	memset(si, 0, sz);

	return (si);
}

size_t
sort_list_item_size(struct sort_list_item *si)
{
	size_t ret = 0;

	if (si) {
		ret = sizeof(struct sort_list_item) + keys_array_size();
		if (si->str)
			ret += bws_memsize(si->str);
		for (size_t i = 0; i < keys_num; ++i) {
			const struct key_value *kv;

			kv = get_key_from_keys_array(&si->ka, i);

			if (kv->k != si->str)
				ret += bws_memsize(kv->k);
		}
	}
	return (ret);
}

/*
 * Calculate key for a sort list item
 */
static void
sort_list_item_make_key(struct sort_list_item *si)
{

	preproc(si->str, &(si->ka));
}

/*
 * Set value of a sort list item.
 * Return combined string and keys memory size.
 */
void
sort_list_item_set(struct sort_list_item *si, struct bwstring *str)
{

	if (si) {
		clean_keys_array(si->str, &(si->ka));
		if (si->str) {
			if (si->str == str) {
				/* we are trying to reset the same string */
				return;
			} else {
				bwsfree(si->str);
				si->str = NULL;
			}
		}
		si->str = str;
		sort_list_item_make_key(si);
	}
}

/*
 * De-allocate a sort list item object memory
 */
void
sort_list_item_clean(struct sort_list_item *si)
{

	if (si) {
		clean_keys_array(si->str, &(si->ka));
		if (si->str) {
			bwsfree(si->str);
			si->str = NULL;
		}
	}
}

/*
 * Skip columns according to specs
 */
static size_t
skip_cols_to_start(const struct bwstring *s, size_t cols, size_t start,
    bool skip_blanks, bool *empty_key)
{
	if (cols < 1)
		return (BWSLEN(s) + 1);

	if (skip_blanks)
		while (start < BWSLEN(s) && iswblank(BWS_GET(s,start)))
			++start;

	while (start < BWSLEN(s) && cols > 1) {
		--cols;
		++start;
	}

	if (start >= BWSLEN(s))
		*empty_key = true;

	return (start);
}

/*
 * Skip fields according to specs
 */
static size_t
skip_fields_to_start(const struct bwstring *s, size_t fields, bool *empty_field)
{

	if (fields < 2) {
		if (BWSLEN(s) == 0)
			*empty_field = true;
		return (0);
	} else if (!(sort_opts_vals.tflag)) {
		size_t cpos = 0;
		bool pb = true;

		while (cpos < BWSLEN(s)) {
			bool isblank;

			isblank = iswblank(BWS_GET(s, cpos));

			if (isblank && !pb) {
				--fields;
				if (fields <= 1)
					return (cpos);
			}
			pb = isblank;
			++cpos;
		}
		if (fields > 1)
			*empty_field = true;
		return (cpos);
	} else {
		size_t cpos = 0;

		while (cpos < BWSLEN(s)) {
			if (BWS_GET(s,cpos) == (wchar_t)sort_opts_vals.field_sep) {
				--fields;
				if (fields <= 1)
					return (cpos + 1);
			}
			++cpos;
		}
		if (fields > 1)
			*empty_field = true;
		return (cpos);
	}
}

/*
 * Find fields start
 */
static void
find_field_start(const struct bwstring *s, struct key_specs *ks,
    size_t *field_start, size_t *key_start, bool *empty_field, bool *empty_key)
{

	*field_start = skip_fields_to_start(s, ks->f1, empty_field);
	if (!*empty_field)
		*key_start = skip_cols_to_start(s, ks->c1, *field_start,
		    ks->pos1b, empty_key);
	else
		*empty_key = true;
}

/*
 * Find end key position
 */
static size_t
find_field_end(const struct bwstring *s, struct key_specs *ks)
{
	size_t f2, next_field_start, pos_end;
	bool empty_field, empty_key;

	empty_field = false;
	empty_key = false;
	f2 = ks->f2;

	if (f2 == 0)
		return (BWSLEN(s) + 1);
	else {
		if (ks->c2 == 0) {
			next_field_start = skip_fields_to_start(s, f2 + 1,
			    &empty_field);
			if ((next_field_start > 0) && sort_opts_vals.tflag &&
			    ((wchar_t)sort_opts_vals.field_sep == BWS_GET(s,
			    next_field_start - 1)))
				--next_field_start;
		} else
			next_field_start = skip_fields_to_start(s, f2,
			    &empty_field);
	}

	if (empty_field || (next_field_start >= BWSLEN(s)))
		return (BWSLEN(s) + 1);

	if (ks->c2) {
		pos_end = skip_cols_to_start(s, ks->c2, next_field_start,
		    ks->pos2b, &empty_key);
		if (pos_end < BWSLEN(s))
			++pos_end;
	} else
		pos_end = next_field_start;

	return (pos_end);
}

/*
 * Cut a field according to the key specs
 */
static struct bwstring *
cut_field(const struct bwstring *s, struct key_specs *ks)
{
	struct bwstring *ret = NULL;

	if (s && ks) {
		size_t field_start, key_end, key_start, sz;
		bool empty_field, empty_key;

		field_start = 0;
		key_start = 0;
		empty_field = false;
		empty_key = false;

		find_field_start(s, ks, &field_start, &key_start,
		    &empty_field, &empty_key);

		if (empty_key)
			sz = 0;
		else {
			key_end = find_field_end(s, ks);
			sz = (key_end < key_start) ? 0 : (key_end - key_start);
		}

		ret = bwsalloc(sz);
		if (sz)
			bwsnocpy(ret, s, key_start, sz);
	} else
		ret = bwsalloc(0);

	return (ret);
}

/*
 * Preprocesses a line applying the necessary transformations
 * specified by command line options and returns the preprocessed
 * string, which can be used to compare.
 */
int
preproc(struct bwstring *s, struct keys_array *ka)
{

	if (sort_opts_vals.kflag)
		for (size_t i = 0; i < keys_num; i++) {
			struct bwstring *key;
			struct key_specs *kspecs;
			struct sort_mods *sm;

			kspecs = &(keys[i]);
			key = cut_field(s, kspecs);

			sm = &(kspecs->sm);
			if (sm->dflag)
				key = dictionary_order(key);
			else if (sm->iflag)
				key = ignore_nonprinting(key);
			if (sm->fflag || sm->Mflag)
				key = ignore_case(key);

			set_key_on_keys_array(ka, key, i);
		}
	else {
		struct bwstring *ret = NULL;
		struct sort_mods *sm = default_sort_mods;

		if (sm->bflag) {
			if (ret == NULL)
				ret = bwsdup(s);
			ret = ignore_leading_blanks(ret);
		}
		if (sm->dflag) {
			if (ret == NULL)
				ret = bwsdup(s);
			ret = dictionary_order(ret);
		} else if (sm->iflag) {
			if (ret == NULL)
				ret = bwsdup(s);
			ret = ignore_nonprinting(ret);
		}
		if (sm->fflag || sm->Mflag) {
			if (ret == NULL)
				ret = bwsdup(s);
			ret = ignore_case(ret);
		}
		if (ret == NULL)
			set_key_on_keys_array(ka, s, 0);
		else
			set_key_on_keys_array(ka, ret, 0);
	}

	return 0;
}

cmpcoll_t
get_sort_func(struct sort_mods *sm)
{

	if (sm->nflag)
		return (numcoll);
	else if (sm->hflag)
		return (hnumcoll);
	else if (sm->gflag)
		return (gnumcoll);
	else if (sm->Mflag)
		return (monthcoll);
	else if (sm->Rflag)
		return (randomcoll);
	else if (sm->Vflag)
		return (versioncoll);
	else
		return (wstrcoll);
}

/*
 * Compares the given strings.  Returns a positive number if
 * the first precedes the second, a negative number if the second is
 * the preceding one, and zero if they are equal.  This function calls
 * the underlying collate functions, which done the actual comparison.
 */
int
key_coll(struct keys_array *ps1, struct keys_array *ps2, size_t offset)
{
	struct key_value *kv1, *kv2;
	struct sort_mods *sm;
	int res = 0;

	for (size_t i = 0; i < keys_num; ++i) {
		kv1 = get_key_from_keys_array(ps1, i);
		kv2 = get_key_from_keys_array(ps2, i);
		sm = &(keys[i].sm);

		if (sm->rflag)
			res = sm->func(kv2, kv1, offset);
		else
			res = sm->func(kv1, kv2, offset);

		if (res)
			break;

		/* offset applies to only the first key */
		offset = 0;
	}
	return (res);
}

/*
 * Compare two strings.
 * Plain symbol-by-symbol comparison.
 */
int
top_level_str_coll(const struct bwstring *s1, const struct bwstring *s2)
{

	if (default_sort_mods->rflag) {
		const struct bwstring *tmp;

		tmp = s1;
		s1 = s2;
		s2 = tmp;
	}

	return (bwscoll(s1, s2, 0));
}

/*
 * Compare a string and a sort list item, according to the sort specs.
 */
int
str_list_coll(struct bwstring *str1, struct sort_list_item **ss2)
{
	struct keys_array *ka1;
	int ret = 0;

	ka1 = keys_array_alloc();

	preproc(str1, ka1);

	sort_list_item_make_key(*ss2);

	if (debug_sort) {
		bwsprintf(stdout, str1, "; s1=<", ">");
		bwsprintf(stdout, (*ss2)->str, ", s2=<", ">");
	}

	ret = key_coll(ka1, &((*ss2)->ka), 0);

	if (debug_sort)
		printf("; cmp1=%d", ret);

	clean_keys_array(str1, ka1);
	sort_free(ka1);

	if ((ret == 0) && !(sort_opts_vals.sflag) && sort_opts_vals.complex_sort) {
		ret = top_level_str_coll(str1, ((*ss2)->str));
		if (debug_sort)
			printf("; cmp2=%d", ret);
	}

	if (debug_sort)
		printf("\n");

	return (ret);
}

/*
 * Compare two sort list items, according to the sort specs.
 */
int
list_coll_offset(struct sort_list_item **ss1, struct sort_list_item **ss2,
    size_t offset)
{
	int ret;

	ret = key_coll(&((*ss1)->ka), &((*ss2)->ka), offset);

	if (debug_sort) {
		if (offset)
			printf("; offset=%d", (int) offset);
		bwsprintf(stdout, ((*ss1)->str), "; s1=<", ">");
		bwsprintf(stdout, ((*ss2)->str), ", s2=<", ">");
		printf("; cmp1=%d\n", ret);
	}

	if (ret)
		return (ret);

	if (!(sort_opts_vals.sflag) && sort_opts_vals.complex_sort) {
		ret = top_level_str_coll(((*ss1)->str), ((*ss2)->str));
		if (debug_sort)
			printf("; cmp2=%d\n", ret);
	}

	return (ret);
}

/*
 * Compare two sort list items, according to the sort specs.
 */
int
list_coll(struct sort_list_item **ss1, struct sort_list_item **ss2)
{

	return (list_coll_offset(ss1, ss2, 0));
}

#define	LSCDEF(N)							\
static int 								\
list_coll_##N(struct sort_list_item **ss1, struct sort_list_item **ss2)	\
{									\
									\
	return (list_coll_offset(ss1, ss2, N));				\
}

LSCDEF(1)
LSCDEF(2)
LSCDEF(3)
LSCDEF(4)
LSCDEF(5)
LSCDEF(6)
LSCDEF(7)
LSCDEF(8)
LSCDEF(9)
LSCDEF(10)
LSCDEF(11)
LSCDEF(12)
LSCDEF(13)
LSCDEF(14)
LSCDEF(15)
LSCDEF(16)
LSCDEF(17)
LSCDEF(18)
LSCDEF(19)
LSCDEF(20)

listcoll_t
get_list_call_func(size_t offset)
{
	static const listcoll_t lsarray[] = { list_coll, list_coll_1,
	    list_coll_2, list_coll_3, list_coll_4, list_coll_5,
	    list_coll_6, list_coll_7, list_coll_8, list_coll_9,
	    list_coll_10, list_coll_11, list_coll_12, list_coll_13,
	    list_coll_14, list_coll_15, list_coll_16, list_coll_17,
	    list_coll_18, list_coll_19, list_coll_20 };

	if (offset <= 20)
		return (lsarray[offset]);

	return (list_coll);
}

/*
 * Compare two sort list items, only by their original string.
 */
int
list_coll_by_str_only(struct sort_list_item **ss1, struct sort_list_item **ss2)
{

	return (top_level_str_coll(((*ss1)->str), ((*ss2)->str)));
}

/*
 * Maximum size of a number in the string (before or after decimal point)
 */
#define MAX_NUM_SIZE (128)

/*
 * Set suffix value
 */
static void setsuffix(wchar_t c, unsigned char *si)
{
	switch (c){
	case L'k':
	case L'K':
		*si = 1;
		break;
	case L'M':
		*si = 2;
		break;
	case L'G':
		*si = 3;
		break;
	case L'T':
		*si = 4;
		break;
	case L'P':
		*si = 5;
		break;
	case L'E':
		*si = 6;
		break;
	case L'Z':
		*si = 7;
		break;
	case L'Y':
		*si = 8;
		break;
	default:
		*si = 0;
	}
}

/*
 * Read string s and parse the string into a fixed-decimal-point number.
 * sign equals -1 if the number is negative (explicit plus is not allowed,
 * according to GNU sort's "info sort".
 * The number part before decimal point is in the smain, after the decimal
 * point is in sfrac, tail is the pointer to the remainder of the string.
 */
static int
read_number(struct bwstring *s0, int *sign, wchar_t *smain, size_t *main_len, wchar_t *sfrac, size_t *frac_len, unsigned char *si)
{
	bwstring_iterator s;

	s = bws_begin(s0);

	/* always end the fraction with zero, even if we have no fraction */
	sfrac[0] = 0;

	while (iswblank(bws_get_iter_value(s)))
		s = bws_iterator_inc(s, 1);

	if (bws_get_iter_value(s) == (wchar_t)symbol_negative_sign) {
		*sign = -1;
		s = bws_iterator_inc(s, 1);
	}

	// This is '0', not '\0', do not change this
	while (iswdigit(bws_get_iter_value(s)) &&
	    (bws_get_iter_value(s) == L'0'))
		s = bws_iterator_inc(s, 1);

	while (bws_get_iter_value(s) && *main_len < MAX_NUM_SIZE) {
		if (iswdigit(bws_get_iter_value(s))) {
			smain[*main_len] = bws_get_iter_value(s);
			s = bws_iterator_inc(s, 1);
			*main_len += 1;
		} else if (symbol_thousands_sep &&
		    (bws_get_iter_value(s) == (wchar_t)symbol_thousands_sep))
			s = bws_iterator_inc(s, 1);
		else
			break;
	}

	smain[*main_len] = 0;

	if (bws_get_iter_value(s) == (wchar_t)symbol_decimal_point) {
		s = bws_iterator_inc(s, 1);
		while (iswdigit(bws_get_iter_value(s)) &&
		    *frac_len < MAX_NUM_SIZE) {
			sfrac[*frac_len] = bws_get_iter_value(s);
			s = bws_iterator_inc(s, 1);
			*frac_len += 1;
		}
		sfrac[*frac_len] = 0;

		while (*frac_len > 0 && sfrac[*frac_len - 1] == L'0') {
			--(*frac_len);
			sfrac[*frac_len] = L'\0';
		}
	}

	setsuffix(bws_get_iter_value(s),si);

	if ((*main_len + *frac_len) == 0)
		*sign = 0;

	return (0);
}

/*
 * Implements string sort.
 */
static int
wstrcoll(struct key_value *kv1, struct key_value *kv2, size_t offset)
{

	if (debug_sort) {
		if (offset)
			printf("; offset=%d\n", (int) offset);
		bwsprintf(stdout, kv1->k, "; k1=<", ">");
		printf("(%zu)", BWSLEN(kv1->k));
		bwsprintf(stdout, kv2->k, ", k2=<", ">");
		printf("(%zu)", BWSLEN(kv2->k));
	}

	return (bwscoll(kv1->k, kv2->k, offset));
}

/*
 * Compare two suffixes
 */
static inline int
cmpsuffix(unsigned char si1, unsigned char si2)
{

	return ((char)si1 - (char)si2);
}

/*
 * Implements numeric sort for -n and -h.
 */
static int
numcoll_impl(struct key_value *kv1, struct key_value *kv2,
    size_t offset __unused, bool use_suffix)
{
	struct bwstring *s1, *s2;
	wchar_t sfrac1[MAX_NUM_SIZE + 1], sfrac2[MAX_NUM_SIZE + 1];
	wchar_t smain1[MAX_NUM_SIZE + 1], smain2[MAX_NUM_SIZE + 1];
	int cmp_res, sign1, sign2;
	size_t frac1, frac2, main1, main2;
	unsigned char SI1, SI2;
	bool e1, e2, key1_read, key2_read;

	s1 = kv1->k;
	s2 = kv2->k;
	sign1 = sign2 = 0;
	main1 = main2 = 0;
	frac1 = frac2 = 0;

	key1_read = key2_read = false;

	if (debug_sort) {
		bwsprintf(stdout, s1, "; k1=<", ">");
		bwsprintf(stdout, s2, ", k2=<", ">");
	}

	if (s1 == s2)
		return (0);

	if (kv1->hint->status == HS_UNINITIALIZED) {
		/* read the number from the string */
		read_number(s1, &sign1, smain1, &main1, sfrac1, &frac1, &SI1);
		key1_read = true;
		kv1->hint->v.nh.n1 = wcstoull(smain1, NULL, 10);
		if(main1 < 1 && frac1 < 1)
			kv1->hint->v.nh.empty=true;
		kv1->hint->v.nh.si = SI1;
		kv1->hint->status = (kv1->hint->v.nh.n1 != ULLONG_MAX) ?
		    HS_INITIALIZED : HS_ERROR;
		kv1->hint->v.nh.neg = (sign1 < 0) ? true : false;
	}

	if (kv2->hint->status == HS_UNINITIALIZED) {
		/* read the number from the string */
		read_number(s2, &sign2, smain2, &main2, sfrac2, &frac2,&SI2);
		key2_read = true;
		kv2->hint->v.nh.n1 = wcstoull(smain2, NULL, 10);
		if(main2 < 1 && frac2 < 1)
			kv2->hint->v.nh.empty=true;
		kv2->hint->v.nh.si = SI2;
		kv2->hint->status = (kv2->hint->v.nh.n1 != ULLONG_MAX) ?
		    HS_INITIALIZED : HS_ERROR;
		kv2->hint->v.nh.neg = (sign2 < 0) ? true : false;
	}

	if (kv1->hint->status == HS_INITIALIZED && kv2->hint->status ==
	    HS_INITIALIZED) {
		unsigned long long n1, n2;
		bool neg1, neg2;

		e1 = kv1->hint->v.nh.empty;
		e2 = kv2->hint->v.nh.empty;

		if (e1 && e2)
			return (0);

		neg1 = kv1->hint->v.nh.neg;
		neg2 = kv2->hint->v.nh.neg;

		if (neg1 && !neg2)
			return (-1);
		if (neg2 && !neg1)
			return (+1);

		if (e1)
			return (neg2 ? +1 : -1);
		else if (e2)
			return (neg1 ? -1 : +1);


		if (use_suffix) {
			cmp_res = cmpsuffix(kv1->hint->v.nh.si, kv2->hint->v.nh.si);
			if (cmp_res)
				return (neg1 ? -cmp_res : cmp_res);
		}

		n1 = kv1->hint->v.nh.n1;
		n2 = kv2->hint->v.nh.n1;
		if (n1 < n2)
			return (neg1 ? +1 : -1);
		else if (n1 > n2)
			return (neg1 ? -1 : +1);
	}

	/* read the numbers from the strings */
	if (!key1_read)
		read_number(s1, &sign1, smain1, &main1, sfrac1, &frac1, &SI1);
	if (!key2_read)
		read_number(s2, &sign2, smain2, &main2, sfrac2, &frac2, &SI2);

	e1 = ((main1 + frac1) == 0);
	e2 = ((main2 + frac2) == 0);

	if (e1 && e2)
		return (0);

	/* we know the result if the signs are different */
	if (sign1 < 0 && sign2 >= 0)
		return (-1);
	if (sign1 >= 0 && sign2 < 0)
		return (+1);

	if (e1)
		return ((sign2 < 0) ? +1 : -1);
	else if (e2)
		return ((sign1 < 0) ? -1 : +1);

	if (use_suffix) {
		cmp_res = cmpsuffix(SI1, SI2);
		if (cmp_res)
			return ((sign1 < 0) ? -cmp_res : cmp_res);
	}

	/* if both numbers are empty assume that the strings are equal */
	if (main1 < 1 && main2 < 1 && frac1 < 1 && frac2 < 1)
		return (0);

	/*
	 * if the main part is of different size, we know the result
	 * (because the leading zeros are removed)
	 */
	if (main1 < main2)
		cmp_res = -1;
	else if (main1 > main2)
		cmp_res = +1;
	/* if the sizes are equal then simple non-collate string compare gives the correct result */
	else
		cmp_res = wcscmp(smain1, smain2);

	/* check fraction */
	if (!cmp_res)
		cmp_res = wcscmp(sfrac1, sfrac2);

	if (!cmp_res)
		return (0);

	/* reverse result if the signs are negative */
	if (sign1 < 0 && sign2 < 0)
		cmp_res = -cmp_res;

	return (cmp_res);
}

/*
 * Implements numeric sort (-n).
 */
static int
numcoll(struct key_value *kv1, struct key_value *kv2, size_t offset)
{

	return (numcoll_impl(kv1, kv2, offset, false));
}

/*
 * Implements 'human' numeric sort (-h).
 */
static int
hnumcoll(struct key_value *kv1, struct key_value *kv2, size_t offset)
{

	return (numcoll_impl(kv1, kv2, offset, true));
}

/*
 * Implements random sort (-R).
 */
static int
randomcoll(struct key_value *kv1, struct key_value *kv2,
    size_t offset __unused)
{
	struct bwstring *s1, *s2;
	MD5_CTX ctx1, ctx2;
	char *b1, *b2;

	s1 = kv1->k;
	s2 = kv2->k;

	if (debug_sort) {
		bwsprintf(stdout, s1, "; k1=<", ">");
		bwsprintf(stdout, s2, ", k2=<", ">");
	}

	if (s1 == s2)
		return (0);

	memcpy(&ctx1,&md5_ctx,sizeof(MD5_CTX));
	memcpy(&ctx2,&md5_ctx,sizeof(MD5_CTX));

	MD5Update(&ctx1, bwsrawdata(s1), bwsrawlen(s1));
	MD5Update(&ctx2, bwsrawdata(s2), bwsrawlen(s2));
	b1 = MD5End(&ctx1, NULL);
	b2 = MD5End(&ctx2, NULL);
	if (b1 == NULL) {
		if (b2 == NULL)
			return (0);
		else {
			sort_free(b2);
			return (-1);
		}
	} else if (b2 == NULL) {
		sort_free(b1);
		return (+1);
	} else {
		int cmp_res;

		cmp_res = strcmp(b1,b2);
		sort_free(b1);
		sort_free(b2);

		if (!cmp_res)
			cmp_res = bwscoll(s1, s2, 0);

		return (cmp_res);
	}
}

/*
 * Implements version sort (-V).
 */
static int
versioncoll(struct key_value *kv1, struct key_value *kv2,
    size_t offset __unused)
{
	struct bwstring *s1, *s2;

	s1 = kv1->k;
	s2 = kv2->k;

	if (debug_sort) {
		bwsprintf(stdout, s1, "; k1=<", ">");
		bwsprintf(stdout, s2, ", k2=<", ">");
	}

	if (s1 == s2)
		return (0);

	return (vcmp(s1, s2));
}

/*
 * Check for minus infinity
 */
static inline bool
huge_minus(double d, int err1)
{

	if (err1 == ERANGE)
		if (d == -HUGE_VAL || d == -HUGE_VALF || d == -HUGE_VALL)
			return (+1);

	return (0);
}

/*
 * Check for plus infinity
 */
static inline bool
huge_plus(double d, int err1)
{

	if (err1 == ERANGE)
		if (d == HUGE_VAL || d == HUGE_VALF || d == HUGE_VALL)
			return (+1);

	return (0);
}

/*
 * Check whether a function is a NAN
 */
static bool
is_nan(double d)
{

	return ((d == NAN) || (isnan(d)));
}

/*
 * Compare two NANs
 */
static int
cmp_nans(double d1, double d2)
{

	if (d1 < d2)
		return (-1);
	if (d1 > d2)
		return (+1);
	return (0);
}

/*
 * Implements general numeric sort (-g).
 */
static int
gnumcoll(struct key_value *kv1, struct key_value *kv2,
    size_t offset __unused)
{
	double d1, d2;
	int err1, err2;
	bool empty1, empty2, key1_read, key2_read;

	d1 = d2 = 0;
	err1 = err2 = 0;
	key1_read = key2_read = false;

	if (debug_sort) {
		bwsprintf(stdout, kv1->k, "; k1=<", ">");
		bwsprintf(stdout, kv2->k, "; k2=<", ">");
	}

	if (kv1->hint->status == HS_UNINITIALIZED) {
		errno = 0;
		d1 = bwstod(kv1->k, &empty1);
		err1 = errno;

		if (empty1)
			kv1->hint->v.gh.notnum = true;
		else if (err1 == 0) {
			kv1->hint->v.gh.d = d1;
			kv1->hint->v.gh.nan = is_nan(d1);
			kv1->hint->status = HS_INITIALIZED;
		} else
			kv1->hint->status = HS_ERROR;

		key1_read = true;
	}

	if (kv2->hint->status == HS_UNINITIALIZED) {
		errno = 0;
		d2 = bwstod(kv2->k, &empty2);
		err2 = errno;

		if (empty2)
			kv2->hint->v.gh.notnum = true;
		else if (err2 == 0) {
			kv2->hint->v.gh.d = d2;
			kv2->hint->v.gh.nan = is_nan(d2);
			kv2->hint->status = HS_INITIALIZED;
		} else
			kv2->hint->status = HS_ERROR;

		key2_read = true;
	}

	if (kv1->hint->status == HS_INITIALIZED &&
	    kv2->hint->status == HS_INITIALIZED) {
		if (kv1->hint->v.gh.notnum)
			return ((kv2->hint->v.gh.notnum) ? 0 : -1);
		else if (kv2->hint->v.gh.notnum)
			return (+1);

		if (kv1->hint->v.gh.nan)
			return ((kv2->hint->v.gh.nan) ?
			    cmp_nans(kv1->hint->v.gh.d, kv2->hint->v.gh.d) :
			    -1);
		else if (kv2->hint->v.gh.nan)
			return (+1);

		d1 = kv1->hint->v.gh.d;
		d2 = kv2->hint->v.gh.d;

		if (d1 < d2)
			return (-1);
		else if (d1 > d2)
			return (+1);
		else
			return (0);
	}

	if (!key1_read) {
		errno = 0;
		d1 = bwstod(kv1->k, &empty1);
		err1 = errno;
	}

	if (!key2_read) {
		errno = 0;
		d2 = bwstod(kv2->k, &empty2);
		err2 = errno;
	}

	/* Non-value case: */
	if (empty1)
		return (empty2 ? 0 : -1);
	else if (empty2)
		return (+1);

	/* NAN case */
	if (is_nan(d1))
		return (is_nan(d2) ? cmp_nans(d1, d2) : -1);
	else if (is_nan(d2))
		return (+1);

	/* Infinities */
	if (err1 == ERANGE || err2 == ERANGE) {
		/* Minus infinity case */
		if (huge_minus(d1, err1)) {
			if (huge_minus(d2, err2)) {
				if (d1 < d2)
					return (-1);
				if (d1 > d2)
					return (+1);
				return (0);
			} else
				return (-1);

		} else if (huge_minus(d2, err2)) {
			if (huge_minus(d1, err1)) {
				if (d1 < d2)
					return (-1);
				if (d1 > d2)
					return (+1);
				return (0);
			} else
				return (+1);
		}

		/* Plus infinity case */
		if (huge_plus(d1, err1)) {
			if (huge_plus(d2, err2)) {
				if (d1 < d2)
					return (-1);
				if (d1 > d2)
					return (+1);
				return (0);
			} else
				return (+1);
		} else if (huge_plus(d2, err2)) {
			if (huge_plus(d1, err1)) {
				if (d1 < d2)
					return (-1);
				if (d1 > d2)
					return (+1);
				return (0);
			} else
				return (-1);
		}
	}

	if (d1 < d2)
		return (-1);
	if (d1 > d2)
		return (+1);

	return (0);
}

/*
 * Implements month sort (-M).
 */
static int
monthcoll(struct key_value *kv1, struct key_value *kv2, size_t offset __unused)
{
	int val1, val2;
	bool key1_read, key2_read;

	val1 = val2 = 0;
	key1_read = key2_read = false;

	if (debug_sort) {
		bwsprintf(stdout, kv1->k, "; k1=<", ">");
		bwsprintf(stdout, kv2->k, "; k2=<", ">");
	}

	if (kv1->hint->status == HS_UNINITIALIZED) {
		kv1->hint->v.Mh.m = bws_month_score(kv1->k);
		key1_read = true;
		kv1->hint->status = HS_INITIALIZED;
	}

	if (kv2->hint->status == HS_UNINITIALIZED) {
		kv2->hint->v.Mh.m = bws_month_score(kv2->k);
		key2_read = true;
		kv2->hint->status = HS_INITIALIZED;
	}

	if (kv1->hint->status == HS_INITIALIZED) {
		val1 = kv1->hint->v.Mh.m;
		key1_read = true;
	}

	if (kv2->hint->status == HS_INITIALIZED) {
		val2 = kv2->hint->v.Mh.m;
		key2_read = true;
	}

	if (!key1_read)
		val1 = bws_month_score(kv1->k);
	if (!key2_read)
		val2 = bws_month_score(kv2->k);

	if (val1 == val2) {
		return (0);
	}
	if (val1 < val2)
		return (-1);
	return (+1);
}
