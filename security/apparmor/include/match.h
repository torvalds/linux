/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * AppArmor security module
 *
 * This file contains AppArmor policy dfa matching engine definitions.
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009-2012 Canonical Ltd.
 */

#ifndef __AA_MATCH_H
#define __AA_MATCH_H

#include <linux/kref.h>

#define DFA_NOMATCH			0
#define DFA_START			1


/*
 * The format used for transition tables is based on the GNU flex table
 * file format (--tables-file option; see Table File Format in the flex
 * info pages and the flex sources for documentation). The magic number
 * used in the header is 0x1B5E783D instead of 0xF13C57B1 though, because
 * new tables have been defined and others YY_ID_CHK (check) and YY_ID_DEF
 * (default) tables are used slightly differently (see the apparmor-parser
 * package).
 *
 *
 * The data in the packed dfa is stored in network byte order, and the tables
 * are arranged for flexibility.  We convert the table data to host native
 * byte order.
 *
 * The dfa begins with a table set header, and is followed by the actual
 * tables.
 */

#define YYTH_MAGIC	0x1B5E783D
#define YYTH_FLAG_DIFF_ENCODE	1
#define YYTH_FLAG_OOB_TRANS	2
#define YYTH_FLAGS (YYTH_FLAG_DIFF_ENCODE | YYTH_FLAG_OOB_TRANS)

#define MAX_OOB_SUPPORTED	1

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
#define YYTD_ID_MAX	8

#define YYTD_DATA8	1
#define YYTD_DATA16	2
#define YYTD_DATA32	4
#define YYTD_DATA64	8

/* ACCEPT & ACCEPT2 tables gets 6 dedicated flags, YYTD_DATAX define the
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

#define TABLE_DATAU16(TABLE) ((u16 *)((TABLE)->td_data))
#define TABLE_DATAU32(TABLE) ((u32 *)((TABLE)->td_data))
#define DEFAULT_TABLE(DFA) ((u32 *)((DFA)->tables[YYTD_ID_DEF]->td_data))
#define BASE_TABLE(DFA) ((u32 *)((DFA)->tables[YYTD_ID_BASE]->td_data))
#define NEXT_TABLE(DFA) ((u32 *)((DFA)->tables[YYTD_ID_NXT]->td_data))
#define CHECK_TABLE(DFA) ((u32 *)((DFA)->tables[YYTD_ID_CHK]->td_data))
#define EQUIV_TABLE(DFA) ((u8 *)((DFA)->tables[YYTD_ID_EC]->td_data))
#define ACCEPT_TABLE(DFA) ((u32 *)((DFA)->tables[YYTD_ID_ACCEPT]->td_data))
#define ACCEPT_TABLE2(DFA) ((u32 *)((DFA)->tables[YYTD_ID_ACCEPT2]->td_data))

struct aa_dfa {
	struct kref count;
	u16 flags;
	u32 max_oob;
	struct table_header *tables[YYTD_ID_TSIZE];
};

#define byte_to_byte(X) (X)

#define UNPACK_ARRAY(TABLE, BLOB, LEN, TTYPE, BTYPE, NTOHX)	\
	do { \
		typeof(LEN) __i; \
		TTYPE *__t = (TTYPE *) TABLE; \
		BTYPE *__b = (BTYPE *) BLOB; \
		for (__i = 0; __i < LEN; __i++) { \
			__t[__i] = NTOHX(__b[__i]); \
		} \
	} while (0)

static inline size_t table_size(size_t len, size_t el_size)
{
	return ALIGN(sizeof(struct table_header) + len * el_size, 8);
}

#define aa_state_t unsigned int

struct aa_dfa *aa_dfa_unpack(void *blob, size_t size, int flags);
aa_state_t aa_dfa_match_len(struct aa_dfa *dfa, aa_state_t start,
			    const char *str, int len);
aa_state_t aa_dfa_match(struct aa_dfa *dfa, aa_state_t start,
			const char *str);
aa_state_t aa_dfa_next(struct aa_dfa *dfa, aa_state_t state, const char c);
aa_state_t aa_dfa_outofband_transition(struct aa_dfa *dfa, aa_state_t state);
aa_state_t aa_dfa_match_until(struct aa_dfa *dfa, aa_state_t start,
			      const char *str, const char **retpos);
aa_state_t aa_dfa_matchn_until(struct aa_dfa *dfa, aa_state_t start,
			       const char *str, int n, const char **retpos);

void aa_dfa_free_kref(struct kref *kref);

/* This needs to be a power of 2 */
#define WB_HISTORY_SIZE 32
struct match_workbuf {
	unsigned int pos;
	unsigned int len;
	aa_state_t history[WB_HISTORY_SIZE];
};
#define DEFINE_MATCH_WB(N)		\
struct match_workbuf N = {		\
	.pos = 0,			\
	.len = 0,			\
}

aa_state_t aa_dfa_leftmatch(struct aa_dfa *dfa, aa_state_t start,
			    const char *str, unsigned int *count);

/**
 * aa_get_dfa - increment refcount on dfa @p
 * @dfa: dfa  (MAYBE NULL)
 *
 * Returns: pointer to @dfa if @dfa is NULL will return NULL
 * Requires: @dfa must be held with valid refcount when called
 */
static inline struct aa_dfa *aa_get_dfa(struct aa_dfa *dfa)
{
	if (dfa)
		kref_get(&(dfa->count));

	return dfa;
}

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

#define MATCH_FLAG_DIFF_ENCODE 0x80000000
#define MARK_DIFF_ENCODE 0x40000000
#define MATCH_FLAG_OOB_TRANSITION 0x20000000
#define MATCH_FLAGS_MASK 0xff000000
#define MATCH_FLAGS_VALID (MATCH_FLAG_DIFF_ENCODE | MATCH_FLAG_OOB_TRANSITION)
#define MATCH_FLAGS_INVALID (MATCH_FLAGS_MASK & ~MATCH_FLAGS_VALID)

#endif /* __AA_MATCH_H */
