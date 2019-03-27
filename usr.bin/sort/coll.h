/*	$FreeBSD$	*/

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

#if !defined(__COLL_H__)
#define	__COLL_H__

#include "bwstring.h"
#include "sort.h"

/*
 * Sort hint data for -n
 */
struct n_hint
{
	unsigned long long	 n1;
	unsigned char		 si;
	bool			 empty;
	bool			 neg;
};

/*
 * Sort hint data for -g
 */
struct g_hint
{
	double			 d;
	bool			 nan;
	bool			 notnum;
};

/*
 * Sort hint data for -M
 */
struct M_hint
{
	int			 m;
};

/*
 * Status of a sort hint object
 */
typedef enum
{
	HS_ERROR = -1, HS_UNINITIALIZED = 0, HS_INITIALIZED = 1
} hint_status;

/*
 * Sort hint object
 */
struct key_hint
{
	hint_status		status;
	union
	{
		struct n_hint		nh;
		struct g_hint		gh;
		struct M_hint		Mh;
	}			v;
};

/*
 * Key value
 */
struct key_value
{
	struct bwstring		*k; /* key string */
	struct key_hint		 hint[0]; /* key sort hint */
} __packed;

/*
 * Set of keys container object.
 */
struct keys_array
{
	struct key_value	 key[0];
};

/*
 * Parsed -k option data
 */
struct key_specs
{
	struct sort_mods	 sm;
	size_t			 c1;
	size_t			 c2;
	size_t			 f1;
	size_t			 f2;
	bool			 pos1b;
	bool			 pos2b;
};

/*
 * Single entry in sort list.
 */
struct sort_list_item
{
	struct bwstring		*str;
	struct keys_array	 ka;
};

/*
 * Function type, used to compare two list objects
 */
typedef int (*listcoll_t)(struct sort_list_item **ss1, struct sort_list_item **ss2);

extern struct key_specs *keys;
extern size_t keys_num;

/*
 * Main localised symbols. These must be wint_t as they may hold WEOF.
 */
extern wint_t symbol_decimal_point;
extern wint_t symbol_thousands_sep;
extern wint_t symbol_negative_sign;
extern wint_t symbol_positive_sign;

/* funcs */

cmpcoll_t get_sort_func(struct sort_mods *sm);

struct keys_array *keys_array_alloc(void);
size_t keys_array_size(void);
struct key_value *get_key_from_keys_array(struct keys_array *ka, size_t ind);
void set_key_on_keys_array(struct keys_array *ka, struct bwstring *s, size_t ind);
void clean_keys_array(const struct bwstring *s, struct keys_array *ka);

struct sort_list_item *sort_list_item_alloc(void);
void sort_list_item_set(struct sort_list_item *si, struct bwstring *str);
void sort_list_item_clean(struct sort_list_item *si);
size_t sort_list_item_size(struct sort_list_item *si);

int preproc(struct bwstring *s, struct keys_array *ka);
int top_level_str_coll(const struct bwstring *, const struct bwstring *);
int key_coll(struct keys_array *ks1, struct keys_array *ks2, size_t offset);
int str_list_coll(struct bwstring *str1, struct sort_list_item **ss2);
int list_coll_by_str_only(struct sort_list_item **ss1, struct sort_list_item **ss2);
int list_coll(struct sort_list_item **ss1, struct sort_list_item **ss2);
int list_coll_offset(struct sort_list_item **ss1, struct sort_list_item **ss2, size_t offset);

listcoll_t get_list_call_func(size_t offset);

#endif /* __COLL_H__ */
