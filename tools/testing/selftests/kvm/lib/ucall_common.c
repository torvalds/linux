// SPDX-License-Identifier: GPL-2.0-only
#include "kvm_util.h"

void ucall(uint64_t cmd, int nargs, ...)
{
	struct ucall uc = {};
	va_list va;
	int i;

	WRITE_ONCE(uc.cmd, cmd);

	nargs = min(nargs, UCALL_MAX_ARGS);

	va_start(va, nargs);
	for (i = 0; i < nargs; ++i)
		WRITE_ONCE(uc.args[i], va_arg(va, uint64_t));
	va_end(va);

	ucall_arch_do_ucall((vm_vaddr_t)&uc);
}
