// SPDX-License-Identifier: GPL-2.0
#include "../../../../arch/x86/include/asm/insn.h"
#include "archinsn.h"
#include "event.h"
#include "machine.h"
#include "thread.h"
#include "symbol.h"

void arch_fetch_insn(struct perf_sample *sample,
		     struct thread *thread,
		     struct machine *machine)
{
	struct insn insn;
	int len;
	bool is64bit = false;

	if (!sample->ip)
		return;
	len = thread__memcpy(thread, machine, sample->insn, sample->ip, sizeof(sample->insn), &is64bit);
	if (len <= 0)
		return;
	insn_init(&insn, sample->insn, len, is64bit);
	insn_get_length(&insn);
	if (insn_complete(&insn) && insn.length <= len)
		sample->insn_len = insn.length;
}
