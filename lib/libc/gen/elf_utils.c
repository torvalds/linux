/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 Konstantin Belousov <kib@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/sysctl.h>
#include <link.h>
#include <stddef.h>
#include "libc_private.h"

int __elf_phdr_match_addr(struct dl_phdr_info *, void *);
void __pthread_map_stacks_exec(void);

int
__elf_phdr_match_addr(struct dl_phdr_info *phdr_info, void *addr)
{
	const Elf_Phdr *ph;
	int i;

	for (i = 0; i < phdr_info->dlpi_phnum; i++) {
		ph = &phdr_info->dlpi_phdr[i];
		if (ph->p_type != PT_LOAD)
			continue;

		/* ELFv1 ABI for powerpc64 passes function descriptor
		 * pointers around, not function pointers.  The function
		 * descriptors live in .opd, which is a non-executable segment.
		 * The PF_X check would therefore make all address checks fail,
		 * causing a crash in some instances.  Don't skip over
		 * non-executable segments in the ELFv1 powerpc64 case.
		 */
#if !defined(__powerpc64__) || (defined(_CALL_ELF) && _CALL_ELF == 2)
		if ((ph->p_flags & PF_X) == 0)
			continue;
#endif

		if (phdr_info->dlpi_addr + ph->p_vaddr <= (uintptr_t)addr &&
		    (uintptr_t)addr + sizeof(addr) < phdr_info->dlpi_addr +
		    ph->p_vaddr + ph->p_memsz)
			break;
	}
	return (i != phdr_info->dlpi_phnum);
}

void
__libc_map_stacks_exec(void)
{
	int mib[2];
	struct rlimit rlim;
	u_long usrstack;
	size_t len;
	
	mib[0] = CTL_KERN;
	mib[1] = KERN_USRSTACK;
	len = sizeof(usrstack);
	if (sysctl(mib, sizeof(mib) / sizeof(mib[0]), &usrstack, &len, NULL, 0)
	    == -1)
		return;
	if (getrlimit(RLIMIT_STACK, &rlim) == -1)
		return;
	mprotect((void *)(uintptr_t)(usrstack - rlim.rlim_cur),
	    rlim.rlim_cur, _rtld_get_stack_prot());
}

#pragma weak __pthread_map_stacks_exec
void
__pthread_map_stacks_exec(void)
{

	((void (*)(void))__libc_interposing[INTERPOS_map_stacks_exec])();
}
