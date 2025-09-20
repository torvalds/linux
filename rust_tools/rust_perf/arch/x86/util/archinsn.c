// SPDX-License-Identifier: GPL-2.0
#include "archinsn.h"
#include "event.h"
#include "machine.h"
#include "thread.h"
#include "symbol.h"
#include "../../../../arch/x86/include/asm/insn.h"

void arch_fetch_insn(struct perf_sample *sample,
		     struct thread *thread,
		     struct machine *machine)
{
	struct insn insn;
	int len, ret;
	bool is64bit = false;

	if (!sample->ip)
		return;
	len = thread__memcpy(thread, machine, sample->insn, sample->ip, sizeof(sample->insn), &is64bit);
	if (len <= 0)
		return;

	ret = insn_decode(&insn, sample->insn, len,
			  is64bit ? INSN_MODE_64 : INSN_MODE_32);
	if (ret >= 0 && insn.length <= len)
		sample->insn_len = insn.length;
}
