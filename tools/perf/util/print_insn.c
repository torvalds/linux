// SPDX-License-Identifier: GPL-2.0
/*
 * Instruction binary disassembler based on capstone.
 *
 * Author(s): Changbin Du <changbin.du@huawei.com>
 */
#include <inttypes.h>
#include <string.h>
#include <stdbool.h>
#include "capstone.h"
#include "debug.h"
#include "sample.h"
#include "symbol.h"
#include "machine.h"
#include "thread.h"
#include "print_insn.h"
#include "dump-insn.h"
#include "map.h"
#include "dso.h"

size_t sample__fprintf_insn_raw(struct perf_sample *sample, FILE *fp)
{
	int printed = 0;

	for (int i = 0; i < sample->insn_len; i++) {
		printed += fprintf(fp, "%02x", (unsigned char)sample->insn[i]);
		if (sample->insn_len - i > 1)
			printed += fprintf(fp, " ");
	}
	return printed;
}

static bool is64bitip(struct machine *machine, struct addr_location *al)
{
	const struct dso *dso = al->map ? map__dso(al->map) : NULL;

	if (dso)
		return dso__is_64_bit(dso);

	return machine__is(machine, "x86_64") ||
		machine__normalized_is(machine, "arm64") ||
		machine__normalized_is(machine, "s390");
}

ssize_t fprintf_insn_asm(struct machine *machine, struct thread *thread, u8 cpumode,
			 bool is64bit, const uint8_t *code, size_t code_size,
			 uint64_t ip, int *lenp, int print_opts, FILE *fp)
{
	return capstone__fprintf_insn_asm(machine, thread, cpumode, is64bit, code, code_size,
					  ip, lenp, print_opts, fp);
}

size_t sample__fprintf_insn_asm(struct perf_sample *sample, struct thread *thread,
				struct machine *machine, FILE *fp,
				struct addr_location *al)
{
	bool is64bit = is64bitip(machine, al);
	ssize_t printed;

	printed = fprintf_insn_asm(machine, thread, sample->cpumode, is64bit,
				   (uint8_t *)sample->insn, sample->insn_len,
				   sample->ip, NULL, 0, fp);
	if (printed < 0)
		return sample__fprintf_insn_raw(sample, fp);

	return printed;
}
