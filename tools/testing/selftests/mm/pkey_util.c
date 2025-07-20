// SPDX-License-Identifier: GPL-2.0-only
#define __SANE_USERSPACE_TYPES__
#include <sys/syscall.h>
#include <unistd.h>

#include "pkey-helpers.h"

int sys_pkey_alloc(unsigned long flags, unsigned long init_val)
{
	int ret = syscall(SYS_pkey_alloc, flags, init_val);
	dprintf1("%s(flags=%lx, init_val=%lx) syscall ret: %d errno: %d\n",
			__func__, flags, init_val, ret, errno);
	return ret;
}

int sys_pkey_free(unsigned long pkey)
{
	int ret = syscall(SYS_pkey_free, pkey);
	dprintf1("%s(pkey=%ld) syscall ret: %d\n", __func__, pkey, ret);
	return ret;
}

int sys_mprotect_pkey(void *ptr, size_t size, unsigned long orig_prot,
		unsigned long pkey)
{
	int sret;

	dprintf2("%s(0x%p, %zx, prot=%lx, pkey=%lx)\n", __func__,
			ptr, size, orig_prot, pkey);

	errno = 0;
	sret = syscall(__NR_pkey_mprotect, ptr, size, orig_prot, pkey);
	if (errno) {
		dprintf2("SYS_mprotect_key sret: %d\n", sret);
		dprintf2("SYS_mprotect_key prot: 0x%lx\n", orig_prot);
		dprintf2("SYS_mprotect_key failed, errno: %d\n", errno);
		if (DEBUG_LEVEL >= 2)
			perror("SYS_mprotect_pkey");
	}
	return sret;
}
