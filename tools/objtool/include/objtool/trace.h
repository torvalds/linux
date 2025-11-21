/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2025, Oracle and/or its affiliates.
 */

#ifndef _TRACE_H
#define _TRACE_H

#include <objtool/check.h>
#include <objtool/disas.h>

#ifdef DISAS

extern bool trace;
extern int trace_depth;

#define TRACE(fmt, ...)						\
({	if (trace)						\
		fprintf(stderr, fmt, ##__VA_ARGS__);		\
})

#define TRACE_INSN(insn, fmt, ...)				\
({								\
	if (trace) {						\
		disas_print_insn(stderr, objtool_disas_ctx,	\
				 insn, trace_depth - 1,	\
				 fmt, ##__VA_ARGS__);		\
		fprintf(stderr, "\n");				\
		insn->trace = 1;				\
	}							\
})

#define TRACE_INSN_STATE(insn, sprev, snext)			\
({								\
	if (trace)						\
		trace_insn_state(insn, sprev, snext);		\
})

static inline void trace_enable(void)
{
	trace = true;
	trace_depth = 0;
}

static inline void trace_disable(void)
{
	trace = false;
}

static inline void trace_depth_inc(void)
{
	if (trace)
		trace_depth++;
}

static inline void trace_depth_dec(void)
{
	if (trace)
		trace_depth--;
}

void trace_insn_state(struct instruction *insn, struct insn_state *sprev,
		      struct insn_state *snext);

#else /* DISAS */

#define TRACE(fmt, ...) ({})
#define TRACE_INSN(insn, fmt, ...) ({})
#define TRACE_INSN_STATE(insn, sprev, snext) ({})

static inline void trace_enable(void) {}
static inline void trace_disable(void) {}
static inline void trace_depth_inc(void) {}
static inline void trace_depth_dec(void) {}

#endif

#endif /* _TRACE_H */
