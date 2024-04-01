/* SPDX-License-Identifier: GPL-2.0 */
#ifndef PERF_PRINT_INSN_H
#define PERF_PRINT_INSN_H

#include <stddef.h>
#include <stdio.h>

struct perf_sample;
struct thread;
struct machine;
struct perf_insn;

size_t sample__fprintf_insn_asm(struct perf_sample *sample, struct thread *thread,
				struct machine *machine, FILE *fp, struct addr_location *al);
size_t sample__fprintf_insn_raw(struct perf_sample *sample, FILE *fp);
const char *cs_dump_insn(struct perf_insn *x, uint64_t ip,
                         u8 *inbuf, int inlen, int *lenp);

#endif /* PERF_PRINT_INSN_H */
