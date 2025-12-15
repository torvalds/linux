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

/*
 * Print the instruction address and a message. The instruction
 * itself is not printed.
 */
#define TRACE_ADDR(insn, fmt, ...)				\
({								\
	if (trace) {						\
		disas_print_info(stderr, insn, trace_depth - 1, \
				 fmt "\n", ##__VA_ARGS__);	\
	}							\
})

/*
 * Print the instruction address, the instruction and a message.
 */
#define TRACE_INSN(insn, fmt, ...)				\
({								\
	if (trace) {						\
		disas_print_insn(stderr, objtool_disas_ctx,	\
				 insn, trace_depth - 1,		\
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

#define TRACE_ALT_FMT(pfx, fmt) pfx "<%s.%lx> " fmt
#define TRACE_ALT_ARG(insn) disas_alt_type_name(insn), (insn)->offset

#define TRACE_ALT(insn, fmt, ...)				\
	TRACE_INSN(insn, TRACE_ALT_FMT("", fmt),		\
		   TRACE_ALT_ARG(insn), ##__VA_ARGS__)

#define TRACE_ALT_INFO(insn, pfx, fmt, ...)			\
	TRACE_ADDR(insn, TRACE_ALT_FMT(pfx, fmt),		\
		   TRACE_ALT_ARG(insn), ##__VA_ARGS__)

#define TRACE_ALT_INFO_NOADDR(insn, pfx, fmt, ...)		\
	TRACE_ADDR(NULL, TRACE_ALT_FMT(pfx, fmt),		\
		   TRACE_ALT_ARG(insn), ##__VA_ARGS__)

#define TRACE_ALT_BEGIN(insn, alt, alt_name)			\
({								\
	if (trace) {						\
		alt_name = disas_alt_name(alt);			\
		trace_alt_begin(insn, alt, alt_name);		\
	}							\
})

#define TRACE_ALT_END(insn, alt, alt_name)			\
({								\
	if (trace) {						\
		trace_alt_end(insn, alt, alt_name);		\
		free(alt_name);					\
	}							\
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
void trace_alt_begin(struct instruction *orig_insn, struct alternative *alt,
		     char *alt_name);
void trace_alt_end(struct instruction *orig_insn, struct alternative *alt,
		   char *alt_name);

#else /* DISAS */

#define TRACE(fmt, ...) ({})
#define TRACE_ADDR(insn, fmt, ...) ({})
#define TRACE_INSN(insn, fmt, ...) ({})
#define TRACE_INSN_STATE(insn, sprev, snext) ({})
#define TRACE_ALT(insn, fmt, ...) ({})
#define TRACE_ALT_INFO(insn, fmt, ...) ({})
#define TRACE_ALT_INFO_NOADDR(insn, fmt, ...) ({})
#define TRACE_ALT_BEGIN(insn, alt, alt_name) ({})
#define TRACE_ALT_END(insn, alt, alt_name) ({})


static inline void trace_enable(void) {}
static inline void trace_disable(void) {}
static inline void trace_depth_inc(void) {}
static inline void trace_depth_dec(void) {}
static inline void trace_alt_begin(struct instruction *orig_insn,
				   struct alternative *alt,
				   char *alt_name) {};
static inline void trace_alt_end(struct instruction *orig_insn,
				 struct alternative *alt,
				 char *alt_name) {};

#endif

#endif /* _TRACE_H */
