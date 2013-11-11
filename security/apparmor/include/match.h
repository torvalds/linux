/*
 * AppArmor security module
 *
 * This file contains AppArmor policy dfa matching engine definitions.
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009-2010 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */

#ifndef __AA_MATCH_H
#define __AA_MATCH_H

#include <linux/kref.h>
#include <linux/workqueue.h>

#define DFA_NOMATCH			0
#define DFA_START			1

#define DFA_VALID_PERM_MASK		0xffffffff
#define DFA_VALID_PERM2_MASK		0xffffffff

/**
 * The format used for transition tables is based on the GNU flex table
 * file format (--tables-file option; see Table File Format in the flex
 * info pages and the flex sources for documentation). The magic number
 * used in the header is 0x1B5E783D instead of 0xF13C57B1 though, because
 * the YY_ID_CHK (check) and YY_ID_DEF (default) tables are used
 * slightly differently (see the apparmor-parser package).
 */

#define YYTH_MAGIC	0x1B5E783D
#define YYTH_DEF_RECURSE 0x1			/* DEF Table is recursive */

struct table_set_header {
	u32 th_magic;		/* YYTH_MAGIC */
	u32 th_hsize;
	u32 th_ssize;
	u16 th_flags;
	char th_version[];
};

/* The YYTD_ID are one less than flex table mappings.  The flex id
 * has 1 subtracted at table load time, this allows us to directly use the
 * ID's as indexes.
 */
#define	YYTD_ID_ACCEPT	0
#define YYTD_ID_BASE	1
#define YYTD_ID_CHK	2
#define YYTD_ID_DEF	3
#define YYTD_ID_EC	4
#define YYTD_ID_META	5
#define YYTD_ID_ACCEPT2 6
#define YYTD_ID_NXT	7
#define YYTD_ID_TSIZE	8

#define YYTD_DATA8	1
#define YYTD_DATA16	2
#define YYTD_DATA32	4
#define YYTD_DATA64	8

/* Each ACCEPT2 table gets 6 dedicated flags, YYTD_DATAX define the
 * first flags
 */
#define ACCEPT1_FLAGS(X) ((X) & 0x3f)
#define ACCEPT2_FLAGS(X) ACCEPT1_FLAGS((X) >> YYTD_ID_ACCEPT2)
#define TO_ACCEPT1_FLAG(X) ACCEPT1_FLAGS(X)
#define TO_ACCEPT2_FLAG(X) (ACCEPT1_FLAGS(X) << YYTD_ID_ACCEPT2)
#define DFA_FLAG_VERIFY_STATES 0x1000

struct table_header {
	u16 td_id;
	u16 td_flags;
	u32 td_hilen;
	u32 td_lolen;
	char td_data[];
};

#define DEFAULT_TABLE(DFA) ((u16 *)((DFA)->tables[YYTD_ID_DEF]->td_data))
#define BASE_TABLE(DFA) ((u32 *)((DFA)->tables[YYTD_ID_BASE]->td_data))
#define NEXT_TABLE(DFA) ((u16 *)((DFA)->tables[YYTD_ID_NXT]->td_data))
#define CHECK_TABLE(DFA) ((u16 *)((DFA)->tables[YYTD_ID_CHK]->td_data))
#define EQUIV_TABLE(DFA) ((u8 *)((DFA)->tables[YYTD_ID_EC]->td_data))
#define ACCEPT_TABLE(DFA) ((u32 *)((DFA)->tables[YYTD_ID_ACCEPT]->td_data))
#define ACCEPT_TABLE2(DFA) ((u32 *)((DFA)->tables[YYTD_ID_ACCEPT2]->td_data))

struct aa_dfa {
	struct kref count;
	u16 flags;
	struct table_header *tables[YYTD_ID_TSIZE];
};

#define byte_to_byte(X) (X)

#define UNPACK_ARRAY(TABLE, BLOB, LEN, TYPE, NTOHX) \
	do { \
		typeof(LEN) __i; \
		TYPE *__t = (TYPE *) TABLE; \
		TYPE *__b = (TYPE *) BLOB; \
		for (__i = 0; __i < LEN; __i++) { \
			__t[__i] = NTOHX(__b[__i]); \
		} \
	} while (0)

static inline size_t table_size(size_t len, size_t el_size)
{
	return ALIGN(sizeof(struct table_header) + len * el_size, 8);
}

struct aa_dfa *aa_dfa_unpack(void *blob, size_t size, int flags);
unsigned int aa_dfa_match_len(struct aa_dfa *dfa, unsigned int start,
			      const char *str, int len);
unsigned int aa_dfa_match(struct aa_dfa *dfa, unsigned int start,
			  const char *str);
unsigned int aa_dfa_next(struct aa_dfa *dfa, unsigned int state,
			 const char c);

void aa_dfa_free_kref(struct kref *kref);

/**
 * aa_put_dfa - put a dfa refcount
 * @dfa: dfa to put refcount   (MAYBE NULL)
 *
 * Requires: if @dfa != NULL that a valid refcount be held
 */
static inline void aa_put_dfa(struct aa_dfa *dfa)
{
	if (dfa)
		kref_put(&dfa->count, aa_dfa_free_kref);
}

#endif /* __AA_MATCH_H */
