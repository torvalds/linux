/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2025, Oracle and/or its affiliates.
 */

#ifndef _DISAS_H
#define _DISAS_H

struct alternative;
struct disas_context;
struct disassemble_info;

#ifdef DISAS

struct disas_context *disas_context_create(struct objtool_file *file);
void disas_context_destroy(struct disas_context *dctx);
void disas_warned_funcs(struct disas_context *dctx);
void disas_funcs(struct disas_context *dctx);
int disas_info_init(struct disassemble_info *dinfo,
		    int arch, int mach32, int mach64,
		    const char *options);
size_t disas_insn(struct disas_context *dctx, struct instruction *insn);
char *disas_result(struct disas_context *dctx);
void disas_print_info(FILE *stream, struct instruction *insn, int depth,
		      const char *format, ...);
void disas_print_insn(FILE *stream, struct disas_context *dctx,
		      struct instruction *insn, int depth,
		      const char *format, ...);
char *disas_alt_name(struct alternative *alt);
const char *disas_alt_type_name(struct instruction *insn);

#else /* DISAS */

#include <objtool/warn.h>

static inline struct disas_context *disas_context_create(struct objtool_file *file)
{
	WARN("Rebuild with libopcodes for disassembly support");
	return NULL;
}

static inline void disas_context_destroy(struct disas_context *dctx) {}
static inline void disas_warned_funcs(struct disas_context *dctx) {}
static inline void disas_funcs(struct disas_context *dctx) {}

static inline int disas_info_init(struct disassemble_info *dinfo,
				  int arch, int mach32, int mach64,
				  const char *options)
{
	return -1;
}

static inline size_t disas_insn(struct disas_context *dctx,
				struct instruction *insn)
{
	return -1;
}

static inline char *disas_result(struct disas_context *dctx)
{
	return NULL;
}

static inline void disas_print_info(FILE *stream, struct instruction *insn,
				    int depth, const char *format, ...) {}
static inline void disas_print_insn(FILE *stream, struct disas_context *dctx,
				    struct instruction *insn, int depth,
				    const char *format, ...) {}
static inline char *disas_alt_name(struct alternative *alt)
{
	return NULL;
}

static inline const char *disas_alt_type_name(struct instruction *insn)
{
	return NULL;
}

#endif /* DISAS */

#endif /* _DISAS_H */
