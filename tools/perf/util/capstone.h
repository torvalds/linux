/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_CAPSTONE_H
#define __PERF_CAPSTONE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <linux/compiler.h>
#include <linux/types.h>

struct annotate_args;
struct machine;
struct symbol;
struct thread;

#ifdef HAVE_LIBCAPSTONE_SUPPORT
ssize_t capstone__fprintf_insn_asm(struct machine *machine, struct thread *thread, u8 cpumode,
				   bool is64bit, const uint8_t *code, size_t code_size,
				   uint64_t ip, int *lenp, int print_opts, FILE *fp);
int symbol__disassemble_capstone(const char *filename, struct symbol *sym,
				 struct annotate_args *args);
int symbol__disassemble_capstone_powerpc(const char *filename, struct symbol *sym,
					 struct annotate_args *args);

#else /* !HAVE_LIBCAPSTONE_SUPPORT */
static inline ssize_t capstone__fprintf_insn_asm(struct machine *machine __maybe_unused,
						 struct thread *thread __maybe_unused,
						 u8 cpumode __maybe_unused,
						 bool is64bit __maybe_unused,
						 const uint8_t *code __maybe_unused,
						 size_t code_size __maybe_unused,
						 uint64_t ip __maybe_unused,
						 int *lenp __maybe_unused,
						 int print_opts __maybe_unused,
						 FILE *fp __maybe_unused)
{
	return -1;
}

static inline int symbol__disassemble_capstone(const char *filename __maybe_unused,
					       struct symbol *sym __maybe_unused,
					       struct annotate_args *args __maybe_unused)
{
	return -1;
}

static inline int symbol__disassemble_capstone_powerpc(const char *filename __maybe_unused,
						       struct symbol *sym __maybe_unused,
						       struct annotate_args *args __maybe_unused)
{
	return -1;
}

#endif /* HAVE_LIBCAPSTONE_SUPPORT */

#endif /* __PERF_CAPSTONE_H */
