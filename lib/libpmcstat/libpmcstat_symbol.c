/*-
 * Copyright (c) 2003-2008 Joseph Koshy
 * Copyright (c) 2017 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * This software was developed by BAE Systems, the University of Cambridge
 * Computer Laboratory, and Memorial University under DARPA/AFRL contract
 * FA8650-15-C-7558 ("CADETS"), as part of the DARPA Transparent Computing
 * (TC) research program.
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

#include <sys/types.h>
#include <sys/cpuset.h>
#include <sys/event.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/module.h>
#include <sys/pmc.h>

#include <assert.h>
#include <err.h>
#include <pmc.h>
#include <pmclog.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libpmcstat.h"

struct pmcstat_symbol *
pmcstat_symbol_search_by_name(struct pmcstat_process *pp,
    const char *pi_name, const char *name, uintptr_t *addr_start,
    uintptr_t *addr_end)
{
	struct pmcstat_symbol *sym;
	struct pmcstat_image *image;
	struct pmcstat_pcmap *pcm;
	const char *name1;
	const char *name2;
	bool found;
	size_t i;

	found = 0;

	if (pp == NULL)
		return (NULL);

	TAILQ_FOREACH(pcm, &pp->pp_map, ppm_next) {
		image = pcm->ppm_image;
		if (image->pi_name == NULL)
			continue;
		name1 = pmcstat_string_unintern(image->pi_name);
		if (strcmp(name1, pi_name) == 0) {
			found = 1;
			break;
		}
	}

	if (!found || image->pi_symbols == NULL)
		return (NULL);

	found = 0;

	for (i = 0; i < image->pi_symcount; i++) {
		sym = &image->pi_symbols[i];
		name2 = pmcstat_string_unintern(sym->ps_name);
		if (strcmp(name2, name) == 0) {
			found = 1;
			break;
		}
	}

	if (!found)
		return (NULL);

	*addr_start = (image->pi_vaddr - image->pi_start +
	    pcm->ppm_lowpc + sym->ps_start);
	*addr_end = (image->pi_vaddr - image->pi_start +
	    pcm->ppm_lowpc + sym->ps_end);

	return (sym);
}

/*
 * Helper function.
 */

int
pmcstat_symbol_compare(const void *a, const void *b)
{
	const struct pmcstat_symbol *sym1, *sym2;

	sym1 = (const struct pmcstat_symbol *) a;
	sym2 = (const struct pmcstat_symbol *) b;

	if (sym1->ps_end <= sym2->ps_start)
		return (-1);
	if (sym1->ps_start >= sym2->ps_end)
		return (1);
	return (0);
}

/*
 * Map an address to a symbol in an image.
 */

struct pmcstat_symbol *
pmcstat_symbol_search(struct pmcstat_image *image, uintfptr_t addr)
{
	struct pmcstat_symbol sym;

	if (image->pi_symbols == NULL)
		return (NULL);

	sym.ps_name  = NULL;
	sym.ps_start = addr;
	sym.ps_end   = addr + 1;

	return (bsearch((void *) &sym, image->pi_symbols,
	    image->pi_symcount, sizeof(struct pmcstat_symbol),
	    pmcstat_symbol_compare));
}
