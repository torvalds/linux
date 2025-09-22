/*	$OpenBSD: exidx.c,v 1.2 2016/09/18 00:19:37 jsg Exp $ */
/*
 * Copyright (c) 2016 Mark Kettenis <kettenis@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <link.h>
#include <stddef.h>

void *dl_unwind_find_exidx(const void *pc, int *pcount) __attribute__((weak));

struct exidx_data {
	u_long pc;
	void *exidx;
	int *pcount;
};

struct exidx_entry {
	uint32_t data[2];
};

static int
find_exidx(struct dl_phdr_info *info, size_t size, void *p)
{
	struct exidx_data *data = p;
	const Elf_Phdr *phdr;
	void *exidx;
	int count = 0;
	int found = 0;
	int i;

	for (i = 0; i < info->dlpi_phnum; i++) {
		phdr = &info->dlpi_phdr[i];
		if (data->pc >= info->dlpi_addr + phdr->p_vaddr &&
		    data->pc < info->dlpi_addr + phdr->p_vaddr + phdr->p_memsz)
			found = 1;
		if (phdr->p_type == PT_ARM_EXIDX) {
			exidx = (void *)(info->dlpi_addr + phdr->p_vaddr);
			count = phdr->p_memsz / sizeof(struct exidx_entry);
		}
	}

	if (found && count > 0) {
		data->exidx = exidx;
		*data->pcount = count;
		return 1;
	}

	return 0;
}

void *
dl_unwind_find_exidx(const void *pc, int *pcount)
{
	struct exidx_data data;

	data.pc = (u_long)pc;
	data.pcount = pcount;
	if (dl_iterate_phdr(find_exidx, &data))
		return data.exidx;
	return NULL;
}

__strong_alias(__gnu_Unwind_Find_exidx, dl_unwind_find_exidx);
