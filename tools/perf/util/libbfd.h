/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_LIBBFD_H
#define __PERF_LIBBFD_H

#include <linux/compiler.h>
#include <linux/types.h>
#include <stdbool.h>
#include <stddef.h>

struct annotate_args;
struct build_id;
struct dso;
struct inline_node;
struct symbol;

#ifdef HAVE_LIBBFD_SUPPORT
int libbfd__addr2line(const char *dso_name, u64 addr,
		char **file, unsigned int *line, struct dso *dso,
		bool unwind_inlines, struct inline_node *node,
		struct symbol *sym);


void dso__free_a2l_libbfd(struct dso *dso);

int symbol__disassemble_libbfd(const char *filename, struct symbol *sym,
			     struct annotate_args *args);

int libbfd__read_build_id(const char *filename, struct build_id *bid, bool block);

int libbfd_filename__read_debuglink(const char *filename, char *debuglink, size_t size);

int symbol__disassemble_bpf_libbfd(struct symbol *sym, struct annotate_args *args);

#else // !defined(HAVE_LIBBFD_SUPPORT)
#include "annotate.h"

static inline int libbfd__addr2line(const char *dso_name __always_unused,
				u64 addr __always_unused,
				char **file __always_unused,
				unsigned int *line __always_unused,
				struct dso *dso __always_unused,
				bool unwind_inlines __always_unused,
				struct inline_node *node __always_unused,
				struct symbol *sym __always_unused)
{
	return -1;
}


static inline void dso__free_a2l_libbfd(struct dso *dso __always_unused)
{
}

static inline int symbol__disassemble_libbfd(const char *filename __always_unused,
					struct symbol *sym __always_unused,
					struct annotate_args *args __always_unused)
{
	return -1;
}

static inline int libbfd__read_build_id(const char *filename __always_unused,
					struct build_id *bid __always_unused,
					bool block __always_unused)
{
	return -1;
}

static inline int libbfd_filename__read_debuglink(const char *filename __always_unused,
						char *debuglink __always_unused,
						size_t size __always_unused)
{
	return -1;
}

static inline int symbol__disassemble_bpf_libbfd(struct symbol *sym __always_unused,
						 struct annotate_args *args __always_unused)
{
	return SYMBOL_ANNOTATE_ERRNO__NO_LIBOPCODES_FOR_BPF;
}

#endif // defined(HAVE_LIBBFD_SUPPORT)

#endif /* __PERF_LIBBFD_H */
