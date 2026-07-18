// SPDX-License-Identifier: GPL-2.0
/*
 * Instruction binary disassembler based on capstone.
 *
 * Author(s): Changbin Du <changbin.du@huawei.com>
 */
#include "print_insn.h"

#include <inttypes.h>
#include <stdbool.h>
#include <string.h>

#include <dwarf-regs.h>

#include "capstone.h"
#include "debug.h"
#include "dso.h"
#include "dump-insn.h"
#include "env.h"
#include "machine.h"
#include "map.h"
#include "sample.h"
#include "symbol.h"
#include "thread.h"

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
	uint16_t e_machine;

	if (dso)
		return dso__is_64_bit(dso);

	e_machine = perf_env__e_machine(machine->env, /*e_flags=*/NULL);
	return e_machine == EM_X86_64 || e_machine == EM_AARCH64 || e_machine == EM_S390;
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
