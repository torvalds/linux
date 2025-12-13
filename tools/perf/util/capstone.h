/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_CAPSTONE_H
#define __PERF_CAPSTONE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <linux/types.h>

struct annotate_args;
struct machine;
struct symbol;
struct thread;

ssize_t capstone__fprintf_insn_asm(struct machine *machine, struct thread *thread, u8 cpumode,
				   bool is64bit, const uint8_t *code, size_t code_size,
				   uint64_t ip, int *lenp, int print_opts, FILE *fp);
int symbol__disassemble_capstone(const char *filename, struct symbol *sym,
				 struct annotate_args *args);
int symbol__disassemble_capstone_powerpc(const char *filename, struct symbol *sym,
					 struct annotate_args *args);

#endif /* __PERF_CAPSTONE_H */
