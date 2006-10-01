/*
 * lib/ts_fsm.c	   A naive finite state machine text search approach
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Authors:	Thomas Graf <tgraf@suug.ch>
 *
 * ==========================================================================
 *
 *   A finite state machine consists of n states (struct ts_fsm_token)
 *   representing the pattern as a finite automation. The data is read
 *   sequentially on an octet basis. Every state token specifies the number
 *   of recurrences and the type of value accepted which can be either a
 *   specific character or ctype based set of characters. The available
 *   type of recurrences include 1, (0|1), [0 n], and [1 n].
 *
 *   The algorithm differs between strict/non-strict mode specifying
 *   whether the pattern has to start at the first octet. Strict mode
 *   is enabled by default and can be disabled by inserting
 *   TS_FSM_HEAD_IGNORE as the first token in the chain.
 *
 *   The runtime performance of the algorithm should be around O(n),
 *   however while in strict mode the average runtime can be better.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/textsearch.h>
#include <linux/textsearch_fsm.h>

struct ts_fsm
{
	unsigned int		ntokens;
	struct ts_fsm_token	tokens[0];
};

/* other values derived from ctype.h */
#define _A		0x100 /* ascii */
#define _W		0x200 /* wildcard */

/* Map to _ctype flags and some magic numbers */
static const u16 token_map[TS_FSM_TYPE_MAX+1] = {
	[TS_FSM_SPECIFIC] = 0,
	[TS_FSM_WILDCARD] = _W,
	[TS_FSM_CNTRL]	  = _C,
	[TS_FSM_LOWER]	  = _L,
	[TS_FSM_UPPER]	  = _U,
	[TS_FSM_PUNCT]	  = _P,
	[TS_FSM_SPACE]	  = _S,
	[TS_FSM_DIGIT]	  = _D,
	[TS_FSM_XDIGIT]	  = _D | _X,
	[TS_FSM_ALPHA]	  = _U | _L,
	[TS_FSM_ALNUM]	  = _U | _L | _D,
	[TS_FSM_PRINT]	  = _P | _U | _L | _D | _SP,
	[TS_FSM_GRAPH]	  = _P | _U | _L | _D,
	[TS_FSM_ASCII]	  = _A,
};

static const u16 token_lookup_tbl[256] = {
_W|_A|_C,      _W|_A|_C,     _W|_A|_C,     _W|_A|_C,		/*   0-  3 */
_W|_A|_C,      _W|_A|_C,     _W|_A|_C,     _W|_A|_C,		/*   4-  7 */
_W|_A|_C,      _W|_A|_C|_S,  _W|_A|_C|_S,  _W|_A|_C|_S,		/*   8- 11 */
_W|_A|_C|_S,   _W|_A|_C|_S,  _W|_A|_C,     _W|_A|_C,		/*  12- 15 */
_W|_A|_C,      _W|_A|_C,     _W|_A|_C,     _W|_A|_C,		/*  16- 19 */
_W|_A|_C,      _W|_A|_C,     _W|_A|_C,     _W|_A|_C,		/*  20- 23 */
_W|_A|_C,      _W|_A|_C,     _W|_A|_C,     _W|_A|_C,		/*  24- 27 */
_W|_A|_C,      _W|_A|_C,     _W|_A|_C,     _W|_A|_C,		/*  28- 31 */
_W|_A|_S|_SP,  _W|_A|_P,     _W|_A|_P,     _W|_A|_P,		/*  32- 35 */
_W|_A|_P,      _W|_A|_P,     _W|_A|_P,     _W|_A|_P,		/*  36- 39 */
_W|_A|_P,      _W|_A|_P,     _W|_A|_P,     _W|_A|_P,		/*  40- 43 */
_W|_A|_P,      _W|_A|_P,     _W|_A|_P,     _W|_A|_P,		/*  44- 47 */
_W|_A|_D,      _W|_A|_D,     _W|_A|_D,     _W|_A|_D,		/*  48- 51 */
_W|_A|_D,      _W|_A|_D,     _W|_A|_D,     _W|_A|_D,		/*  52- 55 */
_W|_A|_D,      _W|_A|_D,     _W|_A|_P,     _W|_A|_P,		/*  56- 59 */
_W|_A|_P,      _W|_A|_P,     _W|_A|_P,     _W|_A|_P,		/*  60- 63 */
_W|_A|_P,      _W|_A|_U|_X,  _W|_A|_U|_X,  _W|_A|_U|_X,		/*  64- 67 */
_W|_A|_U|_X,   _W|_A|_U|_X,  _W|_A|_U|_X,  _W|_A|_U,		/*  68- 71 */
_W|_A|_U,      _W|_A|_U,     _W|_A|_U,     _W|_A|_U,		/*  72- 75 */
_W|_A|_U,      _W|_A|_U,     _W|_A|_U,     _W|_A|_U,		/*  76- 79 */
_W|_A|_U,      _W|_A|_U,     _W|_A|_U,     _W|_A|_U,		/*  80- 83 */
_W|_A|_U,      _W|_A|_U,     _W|_A|_U,     _W|_A|_U,		/*  84- 87 */
_W|_A|_U,      _W|_A|_U,     _W|_A|_U,     _W|_A|_P,		/*  88- 91 */
_W|_A|_P,      _W|_A|_P,     _W|_A|_P,     _W|_A|_P,		/*  92- 95 */
_W|_A|_P,      _W|_A|_L|_X,  _W|_A|_L|_X,  _W|_A|_L|_X,		/*  96- 99 */
_W|_A|_L|_X,   _W|_A|_L|_X,  _W|_A|_L|_X,  _W|_A|_L,		/* 100-103 */
_W|_A|_L,      _W|_A|_L,     _W|_A|_L,     _W|_A|_L,		/* 104-107 */
_W|_A|_L,      _W|_A|_L,     _W|_A|_L,     _W|_A|_L,		/* 108-111 */
_W|_A|_L,      _W|_A|_L,     _W|_A|_L,     _W|_A|_L,		/* 112-115 */
_W|_A|_L,      _W|_A|_L,     _W|_A|_L,     _W|_A|_L,		/* 116-119 */
_W|_A|_L,      _W|_A|_L,     _W|_A|_L,     _W|_A|_P,		/* 120-123 */
_W|_A|_P,      _W|_A|_P,     _W|_A|_P,     _W|_A|_C,		/* 124-127 */
_W,            _W,           _W,           _W,			/* 128-131 */
_W,            _W,           _W,           _W,			/* 132-135 */
_W,            _W,           _W,           _W,			/* 136-139 */
_W,            _W,           _W,           _W,			/* 140-143 */
_W,            _W,           _W,           _W,			/* 144-147 */
_W,            _W,           _W,           _W,			/* 148-151 */
_W,            _W,           _W,           _W,			/* 152-155 */
_W,            _W,           _W,           _W,			/* 156-159 */
_W|_S|_SP,     _W|_P,        _W|_P,        _W|_P,		/* 160-163 */
_W|_P,         _W|_P,        _W|_P,        _W|_P,		/* 164-167 */
_W|_P,         _W|_P,        _W|_P,        _W|_P,		/* 168-171 */
_W|_P,         _W|_P,        _W|_P,        _W|_P,		/* 172-175 */
_W|_P,         _W|_P,        _W|_P,        _W|_P,		/* 176-179 */
_W|_P,         _W|_P,        _W|_P,        _W|_P,		/* 180-183 */
_W|_P,         _W|_P,        _W|_P,        _W|_P,		/* 184-187 */
_W|_P,         _W|_P,        _W|_P,        _W|_P,		/* 188-191 */
_W|_U,         _W|_U,        _W|_U,        _W|_U,		/* 192-195 */
_W|_U,         _W|_U,        _W|_U,        _W|_U,		/* 196-199 */
_W|_U,         _W|_U,        _W|_U,        _W|_U,		/* 200-203 */
_W|_U,         _W|_U,        _W|_U,        _W|_U,		/* 204-207 */
_W|_U,         _W|_U,        _W|_U,        _W|_U,		/* 208-211 */
_W|_U,         _W|_U,        _W|_U,        _W|_P,		/* 212-215 */
_W|_U,         _W|_U,        _W|_U,        _W|_U,		/* 216-219 */
_W|_U,         _W|_U,        _W|_U,        _W|_L,		/* 220-223 */
_W|_L,         _W|_L,        _W|_L,        _W|_L,		/* 224-227 */
_W|_L,         _W|_L,        _W|_L,        _W|_L,		/* 228-231 */
_W|_L,         _W|_L,        _W|_L,        _W|_L,		/* 232-235 */
_W|_L,         _W|_L,        _W|_L,        _W|_L,		/* 236-239 */
_W|_L,         _W|_L,        _W|_L,        _W|_L,		/* 240-243 */
_W|_L,         _W|_L,        _W|_L,        _W|_P,		/* 244-247 */
_W|_L,         _W|_L,        _W|_L,        _W|_L,		/* 248-251 */
_W|_L,         _W|_L,        _W|_L,        _W|_L};		/* 252-255 */

static inline int match_token(struct ts_fsm_token *t, u8 d)
{
	if (t->type)
		return (token_lookup_tbl[d] & t->type) != 0;
	else
		return t->value == d;
}

static unsigned int fsm_find(struct ts_config *conf, struct ts_state *state)
{
	struct ts_fsm *fsm = ts_config_priv(conf);
	struct ts_fsm_token *cur = NULL, *next;
	unsigned int match_start, block_idx = 0, tok_idx;
	unsigned block_len = 0, strict, consumed = state->offset;
	const u8 *data;

#define GET_NEXT_BLOCK()		\
({	consumed += block_idx;		\
	block_idx = 0;			\
	block_len = conf->get_next_block(consumed, &data, conf, state); })

#define TOKEN_MISMATCH()		\
	do {				\
		if (strict)		\
			goto no_match;	\
		block_idx++;		\
		goto startover;		\
	} while(0)

#define end_of_data() unlikely(block_idx >= block_len && !GET_NEXT_BLOCK())

	if (end_of_data())
		goto no_match;

	strict = fsm->tokens[0].recur != TS_FSM_HEAD_IGNORE;

startover:
	match_start = consumed + block_idx;

	for (tok_idx = 0; tok_idx < fsm->ntokens; tok_idx++) {
		cur = &fsm->tokens[tok_idx];

		if (likely(tok_idx < (fsm->ntokens - 1)))
			next = &fsm->tokens[tok_idx + 1];
		else
			next = NULL;

		switch (cur->recur) {
		case TS_FSM_SINGLE:
			if (end_of_data())
				goto no_match;

			if (!match_token(cur, data[block_idx]))
				TOKEN_MISMATCH();
			break;

		case TS_FSM_PERHAPS:
			if (end_of_data() ||
			    !match_token(cur, data[block_idx]))
				continue;
			break;

		case TS_FSM_MULTI:
			if (end_of_data())
				goto no_match;

			if (!match_token(cur, data[block_idx]))
				TOKEN_MISMATCH();

			block_idx++;
			/* fall through */

		case TS_FSM_ANY:
			if (next == NULL)
				goto found_match;

			if (end_of_data())
				continue;

			while (!match_token(next, data[block_idx])) {
				if (!match_token(cur, data[block_idx]))
					TOKEN_MISMATCH();
				block_idx++;
				if (end_of_data())
					goto no_match;
			}
			continue;

		/*
		 * Optimization: Prefer small local loop over jumping
		 * back and forth until garbage at head is munched.
		 */
		case TS_FSM_HEAD_IGNORE:
			if (end_of_data())
				continue;

			while (!match_token(next, data[block_idx])) {
				/*
				 * Special case, don't start over upon
				 * a mismatch, give the user the
				 * chance to specify the type of data
				 * allowed to be ignored.
				 */
				if (!match_token(cur, data[block_idx]))
					goto no_match;

				block_idx++;
				if (end_of_data())
					goto no_match;
			}

			match_start = consumed + block_idx;
			continue;
		}

		block_idx++;
	}

	if (end_of_data())
		goto found_match;

no_match:
	return UINT_MAX;

found_match:
	state->offset = consumed + block_idx;
	return match_start;
}

static struct ts_config *fsm_init(const void *pattern, unsigned int len,
				     gfp_t gfp_mask)
{
	int i, err = -EINVAL;
	struct ts_config *conf;
	struct ts_fsm *fsm;
	struct ts_fsm_token *tokens = (struct ts_fsm_token *) pattern;
	unsigned int ntokens = len / sizeof(*tokens);
	size_t priv_size = sizeof(*fsm) + len;

	if (len  % sizeof(struct ts_fsm_token) || ntokens < 1)
		goto errout;

	for (i = 0; i < ntokens; i++) {
		struct ts_fsm_token *t = &tokens[i];

		if (t->type > TS_FSM_TYPE_MAX || t->recur > TS_FSM_RECUR_MAX)
			goto errout;

		if (t->recur == TS_FSM_HEAD_IGNORE &&
		    (i != 0 || i == (ntokens - 1)))
			goto errout;
	}

	conf = alloc_ts_config(priv_size, gfp_mask);
	if (IS_ERR(conf))
		return conf;

	fsm = ts_config_priv(conf);
	fsm->ntokens = ntokens;
	memcpy(fsm->tokens, pattern, len);

	for (i = 0; i < fsm->ntokens; i++) {
		struct ts_fsm_token *t = &fsm->tokens[i];
		t->type = token_map[t->type];
	}

	return conf;

errout:
	return ERR_PTR(err);
}

static void *fsm_get_pattern(struct ts_config *conf)
{
	struct ts_fsm *fsm = ts_config_priv(conf);
	return fsm->tokens;
}

static unsigned int fsm_get_pattern_len(struct ts_config *conf)
{
	struct ts_fsm *fsm = ts_config_priv(conf);
	return fsm->ntokens * sizeof(struct ts_fsm_token);
}

static struct ts_ops fsm_ops = {
	.name		  = "fsm",
	.find		  = fsm_find,
	.init		  = fsm_init,
	.get_pattern	  = fsm_get_pattern,
	.get_pattern_len  = fsm_get_pattern_len,
	.owner		  = THIS_MODULE,
	.list		  = LIST_HEAD_INIT(fsm_ops.list)
};

static int __init init_fsm(void)
{
	return textsearch_register(&fsm_ops);
}

static void __exit exit_fsm(void)
{
	textsearch_unregister(&fsm_ops);
}

MODULE_LICENSE("GPL");

module_init(init_fsm);
module_exit(exit_fsm);
