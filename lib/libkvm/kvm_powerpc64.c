/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2008, Juniper Networks, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/endian.h>
#include <sys/kerneldump.h>
#include <sys/mman.h>

#include <elf.h>
#include <kvm.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "kvm_private.h"

struct vmstate {
	void		*map;
	size_t		mapsz;
	size_t		dmphdrsz;
	Elf64_Ehdr	*eh;
	Elf64_Phdr	*ph;
};

static int
valid_elf_header(Elf64_Ehdr *eh)
{

	if (!IS_ELF(*eh))
		return (0);
	if (eh->e_ident[EI_CLASS] != ELFCLASS64)
		return (0);
	if (eh->e_ident[EI_DATA] != ELFDATA2MSB)
		return (0);
	if (eh->e_ident[EI_VERSION] != EV_CURRENT)
		return (0);
	if (eh->e_ident[EI_OSABI] != ELFOSABI_STANDALONE)
		return (0);
	if (be16toh(eh->e_type) != ET_CORE)
		return (0);
	if (be16toh(eh->e_machine) != EM_PPC64)
		return (0);
	/* Can't think of anything else to check... */
	return (1);
}

static size_t
dump_header_size(struct kerneldumpheader *dh)
{

	if (strcmp(dh->magic, KERNELDUMPMAGIC) != 0)
		return (0);
	if (strcmp(dh->architecture, "powerpc64") != 0)
		return (0);
	/* That should do it... */
	return (sizeof(*dh));
}

/*
 * Map the ELF headers into the process' address space. We do this in two
 * steps: first the ELF header itself and using that information the whole
 * set of headers.
 */
static int
powerpc_maphdrs(kvm_t *kd)
{
	struct vmstate *vm;
	size_t mapsz;

	vm = kd->vmst;

	vm->mapsz = sizeof(*vm->eh) + sizeof(struct kerneldumpheader);
	vm->map = mmap(NULL, vm->mapsz, PROT_READ, MAP_PRIVATE, kd->pmfd, 0);
	if (vm->map == MAP_FAILED) {
		_kvm_err(kd, kd->program, "cannot map corefile");
		return (-1);
	}
	vm->dmphdrsz = 0;
	vm->eh = vm->map;
	if (!valid_elf_header(vm->eh)) {
		/*
		 * Hmmm, no ELF header. Maybe we still have a dump header.
		 * This is normal when the core file wasn't created by
		 * savecore(8), but instead was dumped over TFTP. We can
		 * easily skip the dump header...
		 */
		vm->dmphdrsz = dump_header_size(vm->map);
		if (vm->dmphdrsz == 0)
			goto inval;
		vm->eh = (void *)((uintptr_t)vm->map + vm->dmphdrsz);
		if (!valid_elf_header(vm->eh))
			goto inval;
	}
	mapsz = be16toh(vm->eh->e_phentsize) * be16toh(vm->eh->e_phnum) +
	    be64toh(vm->eh->e_phoff);
	munmap(vm->map, vm->mapsz);

	/* Map all headers. */
	vm->mapsz = vm->dmphdrsz + mapsz;
	vm->map = mmap(NULL, vm->mapsz, PROT_READ, MAP_PRIVATE, kd->pmfd, 0);
	if (vm->map == MAP_FAILED) {
		_kvm_err(kd, kd->program, "cannot map corefile headers");
		return (-1);
	}
	vm->eh = (void *)((uintptr_t)vm->map + vm->dmphdrsz);
	vm->ph = (void *)((uintptr_t)vm->eh +
	    (uintptr_t)be64toh(vm->eh->e_phoff));
	return (0);

 inval:
	_kvm_err(kd, kd->program, "invalid corefile");
	return (-1);
}

/*
 * Determine the offset within the corefile corresponding the virtual
 * address. Return the number of contiguous bytes in the corefile or
 * 0 when the virtual address is invalid.
 */
static size_t
powerpc64_va2off(kvm_t *kd, kvaddr_t va, off_t *ofs)
{
	struct vmstate *vm = kd->vmst;
	Elf64_Phdr *ph;
	int nph;

	ph = vm->ph;
	nph = be16toh(vm->eh->e_phnum);
	while (nph && (va < be64toh(ph->p_vaddr) ||
	    va >= be64toh(ph->p_vaddr) + be64toh(ph->p_memsz))) {
		nph--;
		ph = (void *)((uintptr_t)ph + be16toh(vm->eh->e_phentsize));
	}
	if (nph == 0)
		return (0);

	/* Segment found. Return file offset and range. */
	*ofs = vm->dmphdrsz + be64toh(ph->p_offset) +
	    (va - be64toh(ph->p_vaddr));
	return (be64toh(ph->p_memsz) - (va - be64toh(ph->p_vaddr)));
}

static void
_powerpc64_freevtop(kvm_t *kd)
{
	struct vmstate *vm = kd->vmst;

	if (vm->eh != MAP_FAILED)
		munmap(vm->eh, vm->mapsz);
	free(vm);
	kd->vmst = NULL;
}

static int
_powerpc64_probe(kvm_t *kd)
{

	return (_kvm_probe_elf_kernel(kd, ELFCLASS64, EM_PPC64) &&
	    kd->nlehdr.e_ident[EI_DATA] == ELFDATA2MSB);
}

static int
_powerpc64_initvtop(kvm_t *kd)
{

	kd->vmst = (struct vmstate *)_kvm_malloc(kd, sizeof(*kd->vmst));
	if (kd->vmst == NULL)
		return (-1);

	if (powerpc_maphdrs(kd) == -1)
		return (-1);

	return (0);
}

static int
_powerpc64_kvatop(kvm_t *kd, kvaddr_t va, off_t *ofs)
{
	struct vmstate *vm;

	vm = kd->vmst;
	if (be64toh(vm->ph->p_paddr) == 0xffffffffffffffff)
		return ((int)powerpc64_va2off(kd, va, ofs));

	_kvm_err(kd, kd->program, "Raw corefile not supported");
	return (0);
}

static int
_powerpc64_native(kvm_t *kd __unused)
{

#ifdef __powerpc64__
	return (1);
#else
	return (0);
#endif
}

static struct kvm_arch kvm_powerpc64 = {
	.ka_probe = _powerpc64_probe,
	.ka_initvtop = _powerpc64_initvtop,
	.ka_freevtop = _powerpc64_freevtop,
	.ka_kvatop = _powerpc64_kvatop,
	.ka_native = _powerpc64_native,
};

KVM_ARCH(kvm_powerpc64);
