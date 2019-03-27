/*-
 * Copyright (c) 2001 Benno Rice <benno@FreeBSD.org>
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#define __ELF_WORD_SIZE 64

#include <sys/param.h>
#include <sys/endian.h>
#include <sys/linker.h>

#include <machine/metadata.h>
#include <machine/elf.h>

#include <stand.h>

#include "bootstrap.h"
#include "host_syscall.h"

extern char		end[];
extern void		*kerneltramp;
extern size_t		szkerneltramp;

struct trampoline_data {
	uint32_t	kernel_entry;
	uint32_t	dtb;
	uint32_t	phys_mem_offset;
	uint32_t	of_entry;
	uint32_t	mdp;
	uint32_t	mdp_size;
};

vm_offset_t md_load64(char *args, vm_offset_t *modulep, vm_offset_t *dtb);

int
ppc64_elf_loadfile(char *filename, uint64_t dest,
    struct preloaded_file **result)
{
	int	r;

	r = __elfN(loadfile)(filename, dest, result);
	if (r != 0)
		return (r);

	return (0);
}

int
ppc64_elf_exec(struct preloaded_file *fp)
{
	struct file_metadata	*fmp;
	vm_offset_t		mdp, dtb;
	Elf_Ehdr		*e;
	int			error;
	uint32_t		*trampoline;
	uint64_t		entry;
	uint64_t		trampolinebase;
	struct trampoline_data	*trampoline_data;
	int			nseg;
	void			*kseg;

	if ((fmp = file_findmetadata(fp, MODINFOMD_ELFHDR)) == NULL) {
		return(EFTYPE);
	}
	e = (Elf_Ehdr *)&fmp->md_data;

	/*
	 * Figure out where to put it.
	 *
	 * Linux does not allow to do kexec_load into
	 * any part of memory. Ask arch_loadaddr to
	 * resolve the first available chunk of physical
	 * memory where loading is possible (load_addr).
	 *
	 * Memory organization is shown below.
	 * It is assumed, that text segment offset of
	 * kernel ELF (KERNPHYSADDR) is non-zero,
	 * which is true for PPC/PPC64 architectures,
	 * where default is 0x100000.
	 *
	 * load_addr:                 trampoline code
	 * load_addr + KERNPHYSADDR:  kernel text segment
	 */
	trampolinebase = archsw.arch_loadaddr(LOAD_RAW, NULL, 0);
	printf("Load address at %#jx\n", (uintmax_t)trampolinebase);
	printf("Relocation offset is %#jx\n", (uintmax_t)elf64_relocation_offset);

	/* Set up loader trampoline */
	trampoline = malloc(szkerneltramp);
	memcpy(trampoline, &kerneltramp, szkerneltramp);

	/* Parse function descriptor for ELFv1 kernels */
	if ((e->e_flags & 3) == 2)
		entry = e->e_entry;
	else {
		archsw.arch_copyout(e->e_entry + elf64_relocation_offset,
		    &entry, 8);
		entry = be64toh(entry);
	}

	/*
	 * Placeholder for trampoline data is at trampolinebase + 0x08
	 * CAUTION: all data must be Big Endian
	 */
	trampoline_data = (void*)&trampoline[2];
	trampoline_data->kernel_entry = htobe32(entry + elf64_relocation_offset);
	trampoline_data->phys_mem_offset = htobe32(0);
	trampoline_data->of_entry = htobe32(0);

	if ((error = md_load64(fp->f_args, &mdp, &dtb)) != 0)
		return (error);

	trampoline_data->dtb = htobe32(dtb);
	trampoline_data->mdp = htobe32(mdp);
	trampoline_data->mdp_size = htobe32(0xfb5d104d);

	printf("Kernel entry at %#jx (%#x) ...\n",
	    entry, be32toh(trampoline_data->kernel_entry));
	printf("DTB at %#x, mdp at %#x\n",
	    be32toh(trampoline_data->dtb), be32toh(trampoline_data->mdp));

	dev_cleanup();

	archsw.arch_copyin(trampoline, trampolinebase, szkerneltramp);
	free(trampoline);

	if (archsw.arch_kexec_kseg_get == NULL)
		panic("architecture did not provide kexec segment mapping");
	archsw.arch_kexec_kseg_get(&nseg, &kseg);

	error = kexec_load(trampolinebase, nseg, (uintptr_t)kseg);
	if (error != 0)
		panic("kexec_load returned error: %d", error);

	error = host_reboot(0xfee1dead, 672274793,
	    0x45584543 /* LINUX_REBOOT_CMD_KEXEC */, (uintptr_t)NULL);
	if (error != 0)
		panic("reboot returned error: %d", error);

	while (1) {}
}

struct file_format	ppc_elf64 =
{
	ppc64_elf_loadfile,
	ppc64_elf_exec
};
