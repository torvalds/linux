/*-
 * Copyright (c) 2013-2014 Robert N. M. Watson
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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

#include <sys/param.h>
#include <sys/linker.h>

#include <machine/bootinfo.h>
#include <machine/elf.h>

#include <bootstrap.h>
#include <loader.h>
#include <mips.h>
#include <stand.h>

static int	beri_elf64_loadfile(char *, uint64_t,
		    struct preloaded_file **);
static int	beri_elf64_exec(struct preloaded_file *fp);

struct file_format beri_elf = {
	.l_load = beri_elf64_loadfile,
	.l_exec = beri_elf64_exec,
};

/*
 * bootinfo that we will pass onto the kernel; some fields derived from
 * *boot2_bootinfop, others filled in by loader.
 */
struct bootinfo	bootinfo;

static int
beri_elf64_loadfile(char *filename, uint64_t dest,
    struct preloaded_file **result)
{

	/*
	 * Some platforms require invalidation of instruction caches here; we
	 * don't need that currently.
	 */
	return (__elfN(loadfile)(filename, dest, result));
}

static int
beri_elf64_exec(struct preloaded_file *fp)
{
	void (*entry)(register_t, register_t, register_t, register_t);
	struct file_metadata *md;
	vm_offset_t mdp;
	Elf_Ehdr *ehdr;
	int error;

	md = file_findmetadata(fp, MODINFOMD_ELFHDR);
	if (md == NULL) {
		printf("%s: file_findmetadata failed\n", fp->f_name);
		return (EFTYPE);
	}
	ehdr = (Elf_Ehdr *)md->md_data;

	error = md_load64(fp->f_args, &mdp, NULL);
	if (error) {
		printf("%s: md_load64 failed\n", fp->f_name);
		return (error);
	}

	entry = (void *)ehdr->e_entry;
	printf("Kernel entry at %p\n", entry);

	dev_cleanup();		/* XXXRW: Required? */
	printf("Kernel args: %s\n", fp->f_args);

	/*
	 * Configure bootinfo for the loaded kernel.  Some values are
	 * inherited from the bootinfo passed to us by boot2 (e.g., DTB
	 * pointer); others are local to the loader (e.g., kernel boot flags).
	 */
	bzero(&bootinfo, sizeof(bootinfo));
	bootinfo.bi_version = BOOTINFO_VERSION;
	bootinfo.bi_size = sizeof(bootinfo);
	bootinfo.bi_boot2opts = boot2_bootinfo.bi_boot2opts;
	/* NB: bi_kernelname used only by boot2. */
	/* NB: bi_nfs_diskless not yet. */
	bootinfo.bi_dtb = boot2_bootinfo.bi_dtb;
	bootinfo.bi_memsize = boot2_bootinfo.bi_memsize;
	bootinfo.bi_modulep = mdp;

	/*
	 * XXXRW: For now, pass 'memsize' rather than dtb or bootinfo.  This
	 * is the 'old' ABI spoken by Miniboot and the kernel.  To pass in
	 * boot2opts, modules, etc, we will need to fix this to pass in at
	 * least bootinfop.
	 */
	(*entry)(boot2_argc, (register_t)boot2_argv, (register_t)boot2_envv,
	    (register_t)&bootinfo);

	panic("exec returned");
}
