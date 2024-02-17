// SPDX-License-Identifier: GPL-2.0
/*
 * Instruction binary disassembler based on capstone.
 *
 * Author(s): Changbin Du <changbin.du@huawei.com>
 */
#include <string.h>
#include <stdbool.h>
#include "debug.h"
#include "sample.h"
#include "symbol.h"
#include "machine.h"
#include "thread.h"
#include "print_insn.h"

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

#ifdef HAVE_LIBCAPSTONE_SUPPORT
#include <capstone/capstone.h>

static int capstone_init(struct machine *machine, csh *cs_handle)
{
	cs_arch arch;
	cs_mode mode;

	if (machine__is(machine, "x86_64")) {
		arch = CS_ARCH_X86;
		mode = CS_MODE_64;
	} else if (machine__normalized_is(machine, "x86")) {
		arch = CS_ARCH_X86;
		mode = CS_MODE_32;
	} else if (machine__normalized_is(machine, "arm64")) {
		arch = CS_ARCH_ARM64;
		mode = CS_MODE_ARM;
	} else if (machine__normalized_is(machine, "arm")) {
		arch = CS_ARCH_ARM;
		mode = CS_MODE_ARM + CS_MODE_V8;
	} else if (machine__normalized_is(machine, "s390")) {
		arch = CS_ARCH_SYSZ;
		mode = CS_MODE_BIG_ENDIAN;
	} else {
		return -1;
	}

	if (cs_open(arch, mode, cs_handle) != CS_ERR_OK) {
		pr_warning_once("cs_open failed\n");
		return -1;
	}

	if (machine__normalized_is(machine, "x86")) {
		cs_option(*cs_handle, CS_OPT_SYNTAX, CS_OPT_SYNTAX_ATT);
		/*
		 * Resolving address operands to symbols is implemented
		 * on x86 by investigating instruction details.
		 */
		cs_option(*cs_handle, CS_OPT_DETAIL, CS_OPT_ON);
	}

	return 0;
}

static size_t print_insn_x86(struct perf_sample *sample, struct thread *thread,
			     cs_insn *insn, FILE *fp)
{
	struct addr_location al;
	size_t printed = 0;

	if (insn->detail && insn->detail->x86.op_count == 1) {
		cs_x86_op *op = &insn->detail->x86.operands[0];

		addr_location__init(&al);
		if (op->type == X86_OP_IMM &&
		    thread__find_symbol(thread, sample->cpumode, op->imm, &al)) {
			printed += fprintf(fp, "%s ", insn[0].mnemonic);
			printed += symbol__fprintf_symname_offs(al.sym, &al, fp);
			addr_location__exit(&al);
			return printed;
		}
		addr_location__exit(&al);
	}

	printed += fprintf(fp, "%s %s", insn[0].mnemonic, insn[0].op_str);
	return printed;
}

size_t sample__fprintf_insn_asm(struct perf_sample *sample, struct thread *thread,
				struct machine *machine, FILE *fp)
{
	csh cs_handle;
	cs_insn *insn;
	size_t count;
	size_t printed = 0;
	int ret;

	/* TODO: Try to initiate capstone only once but need a proper place. */
	ret = capstone_init(machine, &cs_handle);
	if (ret < 0) {
		/* fallback */
		return sample__fprintf_insn_raw(sample, fp);
	}

	count = cs_disasm(cs_handle, (uint8_t *)sample->insn, sample->insn_len,
			  sample->ip, 1, &insn);
	if (count > 0) {
		if (machine__normalized_is(machine, "x86"))
			printed += print_insn_x86(sample, thread, &insn[0], fp);
		else
			printed += fprintf(fp, "%s %s", insn[0].mnemonic, insn[0].op_str);
		cs_free(insn, count);
	} else {
		printed += fprintf(fp, "illegal instruction");
	}

	cs_close(&cs_handle);
	return printed;
}
#else
size_t sample__fprintf_insn_asm(struct perf_sample *sample __maybe_unused,
				struct thread *thread __maybe_unused,
				struct machine *machine __maybe_unused,
				FILE *fp __maybe_unused)
{
	return 0;
}
#endif
