/* SPDX-License-Identifier: GPL-2.0 */
#ifndef PERF_PRINT_INSN_H
#define PERF_PRINT_INSN_H

#include <stddef.h>
#include <stdio.h>

struct perf_sample;
struct thread;
struct machine;

size_t sample__fprintf_insn_asm(struct perf_sample *sample, struct thread *thread,
				struct machine *machine, FILE *fp);
size_t sample__fprintf_insn_raw(struct perf_sample *sample, FILE *fp);

#endif /* PERF_PRINT_INSN_H */
