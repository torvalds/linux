/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Rui Paulo under sponsorship from the
 * FreeBSD Foundation.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <rtld_db.h>

#include "_libproc.h"

static void	rdl2prmap(const rd_loadobj_t *, prmap_t *);

static int
map_iter(const rd_loadobj_t *lop, void *arg)
{
	struct file_info *file;
	struct map_info *mapping, *tmp;
	struct proc_handle *phdl;
	size_t i;

	phdl = arg;
	if (phdl->nmappings >= phdl->maparrsz) {
		phdl->maparrsz *= 2;
		tmp = reallocarray(phdl->mappings, phdl->maparrsz,
		    sizeof(*phdl->mappings));
		if (tmp == NULL)
			return (-1);
		phdl->mappings = tmp;
	}

	mapping = &phdl->mappings[phdl->nmappings];
	rdl2prmap(lop, &mapping->map);
	if (strcmp(lop->rdl_path, phdl->execpath) == 0 &&
	    (lop->rdl_prot & RD_RDL_X) != 0)
		phdl->exec_map = phdl->nmappings;

	file = NULL;
	if (lop->rdl_path[0] != '\0') {
		/* Look for an existing mapping of the same file. */
		for (i = 0; i < phdl->nmappings; i++)
			if (strcmp(mapping->map.pr_mapname,
			    phdl->mappings[i].map.pr_mapname) == 0) {
				file = phdl->mappings[i].file;
				break;
			}

		if (file == NULL) {
			file = malloc(sizeof(*file));
			if (file == NULL)
				return (-1);
			file->elf = NULL;
			file->fd = -1;
			file->refs = 1;
		} else
			file->refs++;
	}
	mapping->file = file;
	phdl->nmappings++;
	return (0);
}

static void
rdl2prmap(const rd_loadobj_t *rdl, prmap_t *map)
{

	map->pr_vaddr = rdl->rdl_saddr;
	map->pr_size = rdl->rdl_eaddr - rdl->rdl_saddr;
	map->pr_offset = rdl->rdl_offset;
	map->pr_mflags = 0;
	if (rdl->rdl_prot & RD_RDL_R)
		map->pr_mflags |= MA_READ;
	if (rdl->rdl_prot & RD_RDL_W)
		map->pr_mflags |= MA_WRITE;
	if (rdl->rdl_prot & RD_RDL_X)
		map->pr_mflags |= MA_EXEC;
	(void)strlcpy(map->pr_mapname, rdl->rdl_path,
	    sizeof(map->pr_mapname));
}

rd_agent_t *
proc_rdagent(struct proc_handle *phdl)
{

	if (phdl->rdap == NULL && phdl->status != PS_UNDEAD &&
	    phdl->status != PS_IDLE) {
		if ((phdl->rdap = rd_new(phdl)) == NULL)
			return (NULL);

		phdl->maparrsz = 64;
		phdl->mappings = calloc(phdl->maparrsz,
		    sizeof(*phdl->mappings));
		if (phdl->mappings == NULL)
			return (phdl->rdap);
		if (rd_loadobj_iter(phdl->rdap, map_iter, phdl) != RD_OK)
			return (NULL);
	}
	return (phdl->rdap);
}

void
proc_updatesyms(struct proc_handle *phdl)
{

	memset(phdl->mappings, 0, sizeof(*phdl->mappings) * phdl->maparrsz);
	rd_loadobj_iter(phdl->rdap, map_iter, phdl);
}
