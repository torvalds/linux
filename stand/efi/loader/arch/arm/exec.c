/*-
 * Copyright (c) 2001 Benno Rice <benno@FreeBSD.org>
 * Copyright (c) 2007 Semihalf, Rafal Jaworowski <raj@semihalf.com>
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

#include <sys/param.h>
#include <sys/linker.h>

#include <machine/md_var.h>
#include <machine/metadata.h>
#include <machine/elf.h>

#include <stand.h>

#include <efi.h>
#include <efilib.h>

#include "bootstrap.h"
#include "loader_efi.h"

extern vm_offset_t md_load(char *, vm_offset_t *);
extern int bi_load(char *, vm_offset_t *, vm_offset_t *);

static int
__elfN(arm_load)(char *filename, uint64_t dest,
    struct preloaded_file **result)
{
	int r;

	r = __elfN(loadfile)(filename, dest, result);
	if (r != 0)
		return (r);

	return (0);
}

static int
__elfN(arm_exec)(struct preloaded_file *fp)
{
	struct file_metadata *fmp;
	vm_offset_t modulep, kernend;
	Elf_Ehdr *e;
	int error;
	void (*entry)(void *);

	if ((fmp = file_findmetadata(fp, MODINFOMD_ELFHDR)) == NULL)
		return (EFTYPE);

	e = (Elf_Ehdr *)&fmp->md_data;

	efi_time_fini();

	entry = efi_translate(e->e_entry);

	printf("Kernel entry at 0x%x...\n", (unsigned)entry);
	printf("Kernel args: %s\n", fp->f_args);

	if ((error = bi_load(fp->f_args, &modulep, &kernend)) != 0) {
		efi_time_init();
		return (error);
	}

	/* At this point we've called ExitBootServices, so we can't call
	 * printf or any other function that uses Boot Services */

	dev_cleanup();

	(*entry)((void *)modulep);
	panic("exec returned");
}

static struct file_format arm_elf = {
	__elfN(arm_load),
	__elfN(arm_exec)
};

struct file_format *file_formats[] = {
	&arm_elf,
	NULL
};

