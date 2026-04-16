/* SPDX-License-Identifier: GPL-2.0 */
#include "sample.h"
#include "debug.h"
#include "thread.h"
#include <elf.h>
#ifndef EM_CSKY
#define EM_CSKY		252
#endif
#ifndef EM_LOONGARCH
#define EM_LOONGARCH	258
#endif
#include <linux/zalloc.h>
#include <stdlib.h>
#include <string.h>
#include "../../arch/x86/include/asm/insn.h"

void perf_sample__init(struct perf_sample *sample, bool all)
{
	if (all) {
		memset(sample, 0, sizeof(*sample));
	} else {
		sample->user_regs = NULL;
		sample->intr_regs = NULL;
	}
}

void perf_sample__exit(struct perf_sample *sample)
{
	free(sample->user_regs);
	free(sample->intr_regs);
}

struct regs_dump *perf_sample__user_regs(struct perf_sample *sample)
{
	if (!sample->user_regs) {
		sample->user_regs = zalloc(sizeof(*sample->user_regs));
		if (!sample->user_regs)
			pr_err("Failure to allocate sample user_regs");
	}
	return sample->user_regs;
}


struct regs_dump *perf_sample__intr_regs(struct perf_sample *sample)
{
	if (!sample->intr_regs) {
		sample->intr_regs = zalloc(sizeof(*sample->intr_regs));
		if (!sample->intr_regs)
			pr_err("Failure to allocate sample intr_regs");
	}
	return sample->intr_regs;
}

static int elf_machine_max_instruction_length(uint16_t e_machine)
{
	switch (e_machine) {
	/* Fixed 4-byte (32-bit) architectures */
	case EM_AARCH64:
	case EM_PPC:
	case EM_PPC64:
	case EM_MIPS:
	case EM_SPARC:
	case EM_SPARCV9:
	case EM_ALPHA:
	case EM_LOONGARCH:
	case EM_PARISC:
	case EM_SH:
		return 4;

	/* Variable length or mixed-mode architectures */
	case EM_ARM:    /* Variable due to Thumb/Thumb-2 */
	case EM_RISCV:  /* Variable due to Compressed (C) extension */
	case EM_CSKY:   /* Variable (16 or 32 bit) */
	case EM_ARC:    /* Variable (ARCompact) */
		return 4;
	case EM_S390:   /* Variable (2, 4, or 6 bytes) */
		return 6;
	case EM_68K:
		return 10;
	case EM_386:
	case EM_X86_64:
		return 15;
	case EM_XTENSA: /* Variable (FLIX) */
		return 16;
	default:
		return MAX_INSN;
	}
}

void perf_sample__fetch_insn(struct perf_sample *sample,
			     struct thread *thread,
			     struct machine *machine)
{
	int ret, len;
	bool is64bit = false;
	uint16_t e_machine;

	if (!sample->ip || sample->insn_len != 0)
		return;

	e_machine = thread__e_machine(thread, machine, /*e_flags=*/NULL);
	len = elf_machine_max_instruction_length(e_machine);
	len = thread__memcpy(thread, machine, sample->insn,
			     sample->ip, len,
			     &is64bit);
	if (len <= 0)
		return;

	sample->insn_len = len;

	if (e_machine == EM_386 || e_machine == EM_X86_64) {
		/* Refine the x86 instruction length with the decoder. */
		struct insn insn;

		ret = insn_decode(&insn, sample->insn, len,
				  is64bit ? INSN_MODE_64 : INSN_MODE_32);
		if (ret >= 0 && insn.length <= len)
			sample->insn_len = insn.length;
	}
}
