/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_TEXTSEARCH_FSM_H
#define __LINUX_TEXTSEARCH_FSM_H

#include <linux/types.h>

enum {
	TS_FSM_SPECIFIC,	/* specific character */
	TS_FSM_WILDCARD,	/* any character */
	TS_FSM_DIGIT,		/* isdigit() */
	TS_FSM_XDIGIT,		/* isxdigit() */
	TS_FSM_PRINT,		/* isprint() */
	TS_FSM_ALPHA,		/* isalpha() */
	TS_FSM_ALNUM,		/* isalnum() */
	TS_FSM_ASCII,		/* isascii() */
	TS_FSM_CNTRL,		/* iscntrl() */
	TS_FSM_GRAPH,		/* isgraph() */
	TS_FSM_LOWER,		/* islower() */
	TS_FSM_UPPER,		/* isupper() */
	TS_FSM_PUNCT,		/* ispunct() */
	TS_FSM_SPACE,		/* isspace() */
	__TS_FSM_TYPE_MAX,
};
#define TS_FSM_TYPE_MAX (__TS_FSM_TYPE_MAX - 1)

enum {
	TS_FSM_SINGLE,		/* 1 occurrence */
	TS_FSM_PERHAPS,		/* 1 or 0 occurrence */
	TS_FSM_ANY,		/* 0..n occurrences */
	TS_FSM_MULTI,		/* 1..n occurrences */
	TS_FSM_HEAD_IGNORE,	/* 0..n ignored occurrences at head */
	__TS_FSM_RECUR_MAX,
};
#define TS_FSM_RECUR_MAX (__TS_FSM_RECUR_MAX - 1)

/**
 * struct ts_fsm_token - state machine token (state)
 * @type: type of token
 * @recur: number of recurrences
 * @value: character value for TS_FSM_SPECIFIC
 */
struct ts_fsm_token
{
	__u16		type;
	__u8		recur;
	__u8		value;
};

#endif
