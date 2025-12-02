/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_LLVM_H
#define __PERF_LLVM_H

#include <stdbool.h>
#include <linux/types.h>

struct annotate_args;
struct dso;
struct inline_node;
struct symbol;

int llvm__addr2line(const char *dso_name, u64 addr,
		char **file, unsigned int *line, struct dso *dso,
		bool unwind_inlines, struct inline_node *node,
		struct symbol *sym);

int symbol__disassemble_llvm(const char *filename, struct symbol *sym,
			     struct annotate_args *args);

#endif /* __PERF_LLVM_H */
