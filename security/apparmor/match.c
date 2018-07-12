/*
 * AppArmor security module
 *
 * This file contains AppArmor dfa based regular expression matching engine
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009-2012 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/err.h>
#include <linux/kref.h>

#include "include/lib.h"
#include "include/match.h"

#define base_idx(X) ((X) & 0xffffff)

static char nulldfa_src[] = {
	#include "nulldfa.in"
};
struct aa_dfa *nulldfa;

static char stacksplitdfa_src[] = {
	#include "stacksplitdfa.in"
};
struct aa_dfa *stacksplitdfa;

int aa_setup_dfa_engine(void)
{
	int error;

	nulldfa = aa_dfa_unpack(nulldfa_src, sizeof(nulldfa_src),
				TO_ACCEPT1_FLAG(YYTD_DATA32) |
				TO_ACCEPT2_FLAG(YYTD_DATA32));
	if (IS_ERR(nulldfa)) {
		error = PTR_ERR(nulldfa);
		nulldfa = NULL;
		return error;
	}

	stacksplitdfa = aa_dfa_unpack(stacksplitdfa_src,
				      sizeof(stacksplitdfa_src),
				      TO_ACCEPT1_FLAG(YYTD_DATA32) |
				      TO_ACCEPT2_FLAG(YYTD_DATA32));
	if (IS_ERR(stacksplitdfa)) {
		aa_put_dfa(nulldfa);
		nulldfa = NULL;
		error = PTR_ERR(stacksplitdfa);
		stacksplitdfa = NULL;
		return error;
	}

	return 0;
}

void aa_teardown_dfa_engine(void)
{
	aa_put_dfa(stacksplitdfa);
	aa_put_dfa(nulldfa);
}

/**
 * unpack_table - unpack a dfa table (one of accept, default, base, next check)
 * @blob: data to unpack (NOT NULL)
 * @bsize: size of blob
 *
 * Returns: pointer to table else NULL on failure
 *
 * NOTE: must be freed by kvfree (not kfree)
 */
static struct table_header *unpack_table(char *blob, size_t bsize)
{
	struct table_header *table = NULL;
	struct table_header th;
	size_t tsize;

	if (bsize < sizeof(struct table_header))
		goto out;

	/* loaded td_id's start at 1, subtract 1 now to avoid doing
	 * it every time we use td_id as an index
	 */
	th.td_id = be16_to_cpu(*(__be16 *) (blob)) - 1;
	if (th.td_id > YYTD_ID_MAX)
		goto out;
	th.td_flags = be16_to_cpu(*(__be16 *) (blob + 2));
	th.td_lolen = be32_to_cpu(*(__be32 *) (blob + 8));
	blob += sizeof(struct table_header);

	if (!(th.td_flags == YYTD_DATA16 || th.td_flags == YYTD_DATA32 ||
	      th.td_flags == YYTD_DATA8))
		goto out;

	tsize = table_size(th.td_lolen, th.td_flags);
	if (bsize < tsize)
		goto out;

	table = kvzalloc(tsize, GFP_KERNEL);
	if (table) {
		table->td_id = th.td_id;
		table->td_flags = th.td_flags;
		table->td_lolen = th.td_lolen;
		if (th.td_flags == YYTD_DATA8)
			UNPACK_ARRAY(table->td_data, blob, th.td_lolen,
				     u8, u8, byte_to_byte);
		else if (th.td_flags == YYTD_DATA16)
			UNPACK_ARRAY(table->td_data, blob, th.td_lolen,
				     u16, __be16, be16_to_cpu);
		else if (th.td_flags == YYTD_DATA32)
			UNPACK_ARRAY(table->td_data, blob, th.td_lolen,
				     u32, __be32, be32_to_cpu);
		else
			goto fail;
		/* if table was vmalloced make sure the page tables are synced
		 * before it is used, as it goes live to all cpus.
		 */
		if (is_vmalloc_addr(table))
			vm_unmap_aliases();
	}

out:
	return table;
fail:
	kvfree(table);
	return NULL;
}

/**
 * verify_table_headers - verify that the tables headers are as expected
 * @tables - array of dfa tables to check (NOT NULL)
 * @flags: flags controlling what type of accept table are acceptable
 *
 * Assumes dfa has gone through the first pass verification done by unpacking
 * NOTE: this does not valid accept table values
 *
 * Returns: %0 else error code on failure to verify
 */
static int verify_table_headers(struct table_header **tables, int flags)
{
	size_t state_count, trans_count;
	int error = -EPROTO;

	/* check that required tables exist */
	if (!(tables[YYTD_ID_DEF] && tables[YYTD_ID_BASE] &&
	      tables[YYTD_ID_NXT] && tables[YYTD_ID_CHK]))
		goto out;

	/* accept.size == default.size == base.size */
	state_count = tables[YYTD_ID_BASE]->td_lolen;
	if (ACCEPT1_FLAGS(flags)) {
		if (!tables[YYTD_ID_ACCEPT])
			goto out;
		if (state_count != tables[YYTD_ID_ACCEPT]->td_lolen)
			goto out;
	}
	if (ACCEPT2_FLAGS(flags)) {
		if (!tables[YYTD_ID_ACCEPT2])
			goto out;
		if (state_count != tables[YYTD_ID_ACCEPT2]->td_lolen)
			goto out;
	}
	if (state_count != tables[YYTD_ID_DEF]->td_lolen)
		goto out;

	/* next.size == chk.size */
	trans_count = tables[YYTD_ID_NXT]->td_lolen;
	if (trans_count != tables[YYTD_ID_CHK]->td_lolen)
		goto out;

	/* if equivalence classes then its table size must be 256 */
	if (tables[YYTD_ID_EC] && tables[YYTD_ID_EC]->td_lolen != 256)
		goto out;

	error = 0;
out:
	return error;
}

/**
 * verify_dfa - verify that transitions and states in the tables are in bounds.
 * @dfa: dfa to test  (NOT NULL)
 *
 * Assumes dfa has gone through the first pass verification done by unpacking
 * NOTE: this does not valid accept table values
 *
 * Returns: %0 else error code on failure to verify
 */
static int verify_dfa(struct aa_dfa *dfa)
{
	size_t i, state_count, trans_count;
	int error = -EPROTO;

	state_count = dfa->tables[YYTD_ID_BASE]->td_lolen;
	trans_count = dfa->tables[YYTD_ID_NXT]->td_lolen;
	for (i = 0; i < state_count; i++) {
		if (!(BASE_TABLE(dfa)[i] & MATCH_FLAG_DIFF_ENCODE) &&
		    (DEFAULT_TABLE(dfa)[i] >= state_count))
			goto out;
		if (base_idx(BASE_TABLE(dfa)[i]) + 255 >= trans_count) {
			pr_err("AppArmor DFA next/check upper bounds error\n");
			goto out;
		}
	}

	for (i = 0; i < trans_count; i++) {
		if (NEXT_TABLE(dfa)[i] >= state_count)
			goto out;
		if (CHECK_TABLE(dfa)[i] >= state_count)
			goto out;
	}

	/* Now that all the other tables are verified, verify diffencoding */
	for (i = 0; i < state_count; i++) {
		size_t j, k;

		for (j = i;
		     (BASE_TABLE(dfa)[j] & MATCH_FLAG_DIFF_ENCODE) &&
		     !(BASE_TABLE(dfa)[j] & MARK_DIFF_ENCODE);
		     j = k) {
			k = DEFAULT_TABLE(dfa)[j];
			if (j == k)
				goto out;
			if (k < j)
				break;		/* already verified */
			BASE_TABLE(dfa)[j] |= MARK_DIFF_ENCODE;
		}
	}
	error = 0;

out:
	return error;
}

/**
 * dfa_free - free a dfa allocated by aa_dfa_unpack
 * @dfa: the dfa to free  (MAYBE NULL)
 *
 * Requires: reference count to dfa == 0
 */
static void dfa_free(struct aa_dfa *dfa)
{
	if (dfa) {
		int i;

		for (i = 0; i < ARRAY_SIZE(dfa->tables); i++) {
			kvfree(dfa->tables[i]);
			dfa->tables[i] = NULL;
		}
		kfree(dfa);
	}
}

/**
 * aa_dfa_free_kref - free aa_dfa by kref (called by aa_put_dfa)
 * @kr: kref callback for freeing of a dfa  (NOT NULL)
 */
void aa_dfa_free_kref(struct kref *kref)
{
	struct aa_dfa *dfa = container_of(kref, struct aa_dfa, count);
	dfa_free(dfa);
}

/**
 * aa_dfa_unpack - unpack the binary tables of a serialized dfa
 * @blob: aligned serialized stream of data to unpack  (NOT NULL)
 * @size: size of data to unpack
 * @flags: flags controlling what type of accept tables are acceptable
 *
 * Unpack a dfa that has been serialized.  To find information on the dfa
 * format look in Documentation/admin-guide/LSM/apparmor.rst
 * Assumes the dfa @blob stream has been aligned on a 8 byte boundary
 *
 * Returns: an unpacked dfa ready for matching or ERR_PTR on failure
 */
struct aa_dfa *aa_dfa_unpack(void *blob, size_t size, int flags)
{
	int hsize;
	int error = -ENOMEM;
	char *data = blob;
	struct table_header *table = NULL;
	struct aa_dfa *dfa = kzalloc(sizeof(struct aa_dfa), GFP_KERNEL);
	if (!dfa)
		goto fail;

	kref_init(&dfa->count);

	error = -EPROTO;

	/* get dfa table set header */
	if (size < sizeof(struct table_set_header))
		goto fail;

	if (ntohl(*(__be32 *) data) != YYTH_MAGIC)
		goto fail;

	hsize = ntohl(*(__be32 *) (data + 4));
	if (size < hsize)
		goto fail;

	dfa->flags = ntohs(*(__be16 *) (data + 12));
	if (dfa->flags != 0 && dfa->flags != YYTH_FLAG_DIFF_ENCODE)
		goto fail;

	data += hsize;
	size -= hsize;

	while (size > 0) {
		table = unpack_table(data, size);
		if (!table)
			goto fail;

		switch (table->td_id) {
		case YYTD_ID_ACCEPT:
			if (!(table->td_flags & ACCEPT1_FLAGS(flags)))
				goto fail;
			break;
		case YYTD_ID_ACCEPT2:
			if (!(table->td_flags & ACCEPT2_FLAGS(flags)))
				goto fail;
			break;
		case YYTD_ID_BASE:
			if (table->td_flags != YYTD_DATA32)
				goto fail;
			break;
		case YYTD_ID_DEF:
		case YYTD_ID_NXT:
		case YYTD_ID_CHK:
			if (table->td_flags != YYTD_DATA16)
				goto fail;
			break;
		case YYTD_ID_EC:
			if (table->td_flags != YYTD_DATA8)
				goto fail;
			break;
		default:
			goto fail;
		}
		/* check for duplicate table entry */
		if (dfa->tables[table->td_id])
			goto fail;
		dfa->tables[table->td_id] = table;
		data += table_size(table->td_lolen, table->td_flags);
		size -= table_size(table->td_lolen, table->td_flags);
		table = NULL;
	}
	error = verify_table_headers(dfa->tables, flags);
	if (error)
		goto fail;

	if (flags & DFA_FLAG_VERIFY_STATES) {
		error = verify_dfa(dfa);
		if (error)
			goto fail;
	}

	return dfa;

fail:
	kvfree(table);
	dfa_free(dfa);
	return ERR_PTR(error);
}

#define match_char(state, def, base, next, check, C)	\
do {							\
	u32 b = (base)[(state)];			\
	unsigned int pos = base_idx(b) + (C);		\
	if ((check)[pos] != (state)) {			\
		(state) = (def)[(state)];		\
		if (b & MATCH_FLAG_DIFF_ENCODE)		\
			continue;			\
		break;					\
	}						\
	(state) = (next)[pos];				\
	break;						\
} while (1)

/**
 * aa_dfa_match_len - traverse @dfa to find state @str stops at
 * @dfa: the dfa to match @str against  (NOT NULL)
 * @start: the state of the dfa to start matching in
 * @str: the string of bytes to match against the dfa  (NOT NULL)
 * @len: length of the string of bytes to match
 *
 * aa_dfa_match_len will match @str against the dfa and return the state it
 * finished matching in. The final state can be used to look up the accepting
 * label, or as the start state of a continuing match.
 *
 * This function will happily match again the 0 byte and only finishes
 * when @len input is consumed.
 *
 * Returns: final state reached after input is consumed
 */
unsigned int aa_dfa_match_len(struct aa_dfa *dfa, unsigned int start,
			      const char *str, int len)
{
	u16 *def = DEFAULT_TABLE(dfa);
	u32 *base = BASE_TABLE(dfa);
	u16 *next = NEXT_TABLE(dfa);
	u16 *check = CHECK_TABLE(dfa);
	unsigned int state = start;

	if (state == 0)
		return 0;

	/* current state is <state>, matching character *str */
	if (dfa->tables[YYTD_ID_EC]) {
		/* Equivalence class table defined */
		u8 *equiv = EQUIV_TABLE(dfa);
		for (; len; len--)
			match_char(state, def, base, next, check,
				   equiv[(u8) *str++]);
	} else {
		/* default is direct to next state */
		for (; len; len--)
			match_char(state, def, base, next, check, (u8) *str++);
	}

	return state;
}

/**
 * aa_dfa_match - traverse @dfa to find state @str stops at
 * @dfa: the dfa to match @str against  (NOT NULL)
 * @start: the state of the dfa to start matching in
 * @str: the null terminated string of bytes to match against the dfa (NOT NULL)
 *
 * aa_dfa_match will match @str against the dfa and return the state it
 * finished matching in. The final state can be used to look up the accepting
 * label, or as the start state of a continuing match.
 *
 * Returns: final state reached after input is consumed
 */
unsigned int aa_dfa_match(struct aa_dfa *dfa, unsigned int start,
			  const char *str)
{
	u16 *def = DEFAULT_TABLE(dfa);
	u32 *base = BASE_TABLE(dfa);
	u16 *next = NEXT_TABLE(dfa);
	u16 *check = CHECK_TABLE(dfa);
	unsigned int state = start;

	if (state == 0)
		return 0;

	/* current state is <state>, matching character *str */
	if (dfa->tables[YYTD_ID_EC]) {
		/* Equivalence class table defined */
		u8 *equiv = EQUIV_TABLE(dfa);
		/* default is direct to next state */
		while (*str)
			match_char(state, def, base, next, check,
				   equiv[(u8) *str++]);
	} else {
		/* default is direct to next state */
		while (*str)
			match_char(state, def, base, next, check, (u8) *str++);
	}

	return state;
}

/**
 * aa_dfa_next - step one character to the next state in the dfa
 * @dfa: the dfa to tranverse (NOT NULL)
 * @state: the state to start in
 * @c: the input character to transition on
 *
 * aa_dfa_match will step through the dfa by one input character @c
 *
 * Returns: state reach after input @c
 */
unsigned int aa_dfa_next(struct aa_dfa *dfa, unsigned int state,
			  const char c)
{
	u16 *def = DEFAULT_TABLE(dfa);
	u32 *base = BASE_TABLE(dfa);
	u16 *next = NEXT_TABLE(dfa);
	u16 *check = CHECK_TABLE(dfa);

	/* current state is <state>, matching character *str */
	if (dfa->tables[YYTD_ID_EC]) {
		/* Equivalence class table defined */
		u8 *equiv = EQUIV_TABLE(dfa);
		match_char(state, def, base, next, check, equiv[(u8) c]);
	} else
		match_char(state, def, base, next, check, (u8) c);

	return state;
}

/**
 * aa_dfa_match_until - traverse @dfa until accept state or end of input
 * @dfa: the dfa to match @str against  (NOT NULL)
 * @start: the state of the dfa to start matching in
 * @str: the null terminated string of bytes to match against the dfa (NOT NULL)
 * @retpos: first character in str after match OR end of string
 *
 * aa_dfa_match will match @str against the dfa and return the state it
 * finished matching in. The final state can be used to look up the accepting
 * label, or as the start state of a continuing match.
 *
 * Returns: final state reached after input is consumed
 */
unsigned int aa_dfa_match_until(struct aa_dfa *dfa, unsigned int start,
				const char *str, const char **retpos)
{
	u16 *def = DEFAULT_TABLE(dfa);
	u32 *base = BASE_TABLE(dfa);
	u16 *next = NEXT_TABLE(dfa);
	u16 *check = CHECK_TABLE(dfa);
	u32 *accept = ACCEPT_TABLE(dfa);
	unsigned int state = start, pos;

	if (state == 0)
		return 0;

	/* current state is <state>, matching character *str */
	if (dfa->tables[YYTD_ID_EC]) {
		/* Equivalence class table defined */
		u8 *equiv = EQUIV_TABLE(dfa);
		/* default is direct to next state */
		while (*str) {
			pos = base_idx(base[state]) + equiv[(u8) *str++];
			if (check[pos] == state)
				state = next[pos];
			else
				state = def[state];
			if (accept[state])
				break;
		}
	} else {
		/* default is direct to next state */
		while (*str) {
			pos = base_idx(base[state]) + (u8) *str++;
			if (check[pos] == state)
				state = next[pos];
			else
				state = def[state];
			if (accept[state])
				break;
		}
	}

	*retpos = str;
	return state;
}

/**
 * aa_dfa_matchn_until - traverse @dfa until accept or @n bytes consumed
 * @dfa: the dfa to match @str against  (NOT NULL)
 * @start: the state of the dfa to start matching in
 * @str: the string of bytes to match against the dfa  (NOT NULL)
 * @n: length of the string of bytes to match
 * @retpos: first character in str after match OR str + n
 *
 * aa_dfa_match_len will match @str against the dfa and return the state it
 * finished matching in. The final state can be used to look up the accepting
 * label, or as the start state of a continuing match.
 *
 * This function will happily match again the 0 byte and only finishes
 * when @n input is consumed.
 *
 * Returns: final state reached after input is consumed
 */
unsigned int aa_dfa_matchn_until(struct aa_dfa *dfa, unsigned int start,
				 const char *str, int n, const char **retpos)
{
	u16 *def = DEFAULT_TABLE(dfa);
	u32 *base = BASE_TABLE(dfa);
	u16 *next = NEXT_TABLE(dfa);
	u16 *check = CHECK_TABLE(dfa);
	u32 *accept = ACCEPT_TABLE(dfa);
	unsigned int state = start, pos;

	*retpos = NULL;
	if (state == 0)
		return 0;

	/* current state is <state>, matching character *str */
	if (dfa->tables[YYTD_ID_EC]) {
		/* Equivalence class table defined */
		u8 *equiv = EQUIV_TABLE(dfa);
		/* default is direct to next state */
		for (; n; n--) {
			pos = base_idx(base[state]) + equiv[(u8) *str++];
			if (check[pos] == state)
				state = next[pos];
			else
				state = def[state];
			if (accept[state])
				break;
		}
	} else {
		/* default is direct to next state */
		for (; n; n--) {
			pos = base_idx(base[state]) + (u8) *str++;
			if (check[pos] == state)
				state = next[pos];
			else
				state = def[state];
			if (accept[state])
				break;
		}
	}

	*retpos = str;
	return state;
}

#define inc_wb_pos(wb)						\
do {								\
	wb->pos = (wb->pos + 1) & (wb->size - 1);		\
	wb->len = (wb->len + 1) & (wb->size - 1);		\
} while (0)

/* For DFAs that don't support extended tagging of states */
static bool is_loop(struct match_workbuf *wb, unsigned int state,
		    unsigned int *adjust)
{
	unsigned int pos = wb->pos;
	unsigned int i;

	if (wb->history[pos] < state)
		return false;

	for (i = 0; i <= wb->len; i++) {
		if (wb->history[pos] == state) {
			*adjust = i;
			return true;
		}
		if (pos == 0)
			pos = wb->size;
		pos--;
	}

	*adjust = i;
	return true;
}

static unsigned int leftmatch_fb(struct aa_dfa *dfa, unsigned int start,
				 const char *str, struct match_workbuf *wb,
				 unsigned int *count)
{
	u16 *def = DEFAULT_TABLE(dfa);
	u32 *base = BASE_TABLE(dfa);
	u16 *next = NEXT_TABLE(dfa);
	u16 *check = CHECK_TABLE(dfa);
	unsigned int state = start, pos;

	AA_BUG(!dfa);
	AA_BUG(!str);
	AA_BUG(!wb);
	AA_BUG(!count);

	*count = 0;
	if (state == 0)
		return 0;

	/* current state is <state>, matching character *str */
	if (dfa->tables[YYTD_ID_EC]) {
		/* Equivalence class table defined */
		u8 *equiv = EQUIV_TABLE(dfa);
		/* default is direct to next state */
		while (*str) {
			unsigned int adjust;

			wb->history[wb->pos] = state;
			pos = base_idx(base[state]) + equiv[(u8) *str++];
			if (check[pos] == state)
				state = next[pos];
			else
				state = def[state];
			if (is_loop(wb, state, &adjust)) {
				state = aa_dfa_match(dfa, state, str);
				*count -= adjust;
				goto out;
			}
			inc_wb_pos(wb);
			(*count)++;
		}
	} else {
		/* default is direct to next state */
		while (*str) {
			unsigned int adjust;

			wb->history[wb->pos] = state;
			pos = base_idx(base[state]) + (u8) *str++;
			if (check[pos] == state)
				state = next[pos];
			else
				state = def[state];
			if (is_loop(wb, state, &adjust)) {
				state = aa_dfa_match(dfa, state, str);
				*count -= adjust;
				goto out;
			}
			inc_wb_pos(wb);
			(*count)++;
		}
	}

out:
	if (!state)
		*count = 0;
	return state;
}

/**
 * aa_dfa_leftmatch - traverse @dfa to find state @str stops at
 * @dfa: the dfa to match @str against  (NOT NULL)
 * @start: the state of the dfa to start matching in
 * @str: the null terminated string of bytes to match against the dfa (NOT NULL)
 * @count: current count of longest left.
 *
 * aa_dfa_match will match @str against the dfa and return the state it
 * finished matching in. The final state can be used to look up the accepting
 * label, or as the start state of a continuing match.
 *
 * Returns: final state reached after input is consumed
 */
unsigned int aa_dfa_leftmatch(struct aa_dfa *dfa, unsigned int start,
			      const char *str, unsigned int *count)
{
	DEFINE_MATCH_WB(wb);

	/* TODO: match for extended state dfas */

	return leftmatch_fb(dfa, start, str, &wb, count);
}
