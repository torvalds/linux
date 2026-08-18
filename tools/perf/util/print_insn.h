/* SPDX-License-Identifier: GPL-2.0 */
#ifndef PERF_PRINT_INSN_H
#define PERF_PRINT_INSN_H

#include <stddef.h>
#include <stdio.h>

#include <linux/types.h>

struct addr_location;
struct machine;
struct perf_insn;
struct perf_sample;
struct thread;

#define PRINT_INSN_IMM_HEX		(1<<0)

size_t sample__fprintf_insn_asm(struct perf_sample *sample, struct thread *thread,
				struct machine *machine, FILE *fp, struct addr_location *al);
size_t sample__fprintf_insn_raw(struct perf_sample *sample, FILE *fp);
ssize_t fprintf_insn_asm(struct machine *machine, struct thread *thread, u8 cpumode,
			 bool is64bit, const uint8_t *code, size_t code_size,
			 uint64_t ip, int *lenp, int print_opts, FILE *fp);

#endif /* PERF_PRINT_INSN_H */
