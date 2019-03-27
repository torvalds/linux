/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1998 Michael Smith
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
#include <sys/systm.h>
#include <sys/linker.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>

/*
 * Preloaded module support
 */

vm_offset_t preload_addr_relocate = 0;
caddr_t preload_metadata;

/*
 * Search for the preloaded module (name)
 */
caddr_t
preload_search_by_name(const char *name)
{
    caddr_t	curp;
    uint32_t	*hdr;
    int		next;
    
    if (preload_metadata != NULL) {
	
	curp = preload_metadata;
	for (;;) {
	    hdr = (uint32_t *)curp;
	    if (hdr[0] == 0 && hdr[1] == 0)
		break;

	    /* Search for a MODINFO_NAME field */
	    if ((hdr[0] == MODINFO_NAME) &&
		!strcmp(name, curp + sizeof(uint32_t) * 2))
		return(curp);

	    /* skip to next field */
	    next = sizeof(uint32_t) * 2 + hdr[1];
	    next = roundup(next, sizeof(u_long));
	    curp += next;
	}
    }
    return(NULL);
}

/*
 * Search for the first preloaded module of (type)
 */
caddr_t
preload_search_by_type(const char *type)
{
    caddr_t	curp, lname;
    uint32_t	*hdr;
    int		next;

    if (preload_metadata != NULL) {

	curp = preload_metadata;
	lname = NULL;
	for (;;) {
	    hdr = (uint32_t *)curp;
	    if (hdr[0] == 0 && hdr[1] == 0)
		break;

	    /* remember the start of each record */
	    if (hdr[0] == MODINFO_NAME)
		lname = curp;

	    /* Search for a MODINFO_TYPE field */
	    if ((hdr[0] == MODINFO_TYPE) &&
		!strcmp(type, curp + sizeof(uint32_t) * 2))
		return(lname);

	    /* skip to next field */
	    next = sizeof(uint32_t) * 2 + hdr[1];
	    next = roundup(next, sizeof(u_long));
	    curp += next;
	}
    }
    return(NULL);
}

/*
 * Walk through the preloaded module list
 */
caddr_t
preload_search_next_name(caddr_t base)
{
    caddr_t	curp;
    uint32_t	*hdr;
    int		next;
    
    if (preload_metadata != NULL) {
	
	/* Pick up where we left off last time */
	if (base) {
	    /* skip to next field */
	    curp = base;
	    hdr = (uint32_t *)curp;
	    next = sizeof(uint32_t) * 2 + hdr[1];
	    next = roundup(next, sizeof(u_long));
	    curp += next;
	} else
	    curp = preload_metadata;

	for (;;) {
	    hdr = (uint32_t *)curp;
	    if (hdr[0] == 0 && hdr[1] == 0)
		break;

	    /* Found a new record? */
	    if (hdr[0] == MODINFO_NAME)
		return curp;

	    /* skip to next field */
	    next = sizeof(uint32_t) * 2 + hdr[1];
	    next = roundup(next, sizeof(u_long));
	    curp += next;
	}
    }
    return(NULL);
}

/*
 * Given a preloaded module handle (mod), return a pointer
 * to the data for the attribute (inf).
 */
caddr_t
preload_search_info(caddr_t mod, int inf)
{
    caddr_t	curp;
    uint32_t	*hdr;
    uint32_t	type = 0;
    int		next;

    if (mod == NULL)
    	return (NULL);

    curp = mod;
    for (;;) {
	hdr = (uint32_t *)curp;
	/* end of module data? */
	if (hdr[0] == 0 && hdr[1] == 0)
	    break;
	/* 
	 * We give up once we've looped back to what we were looking at 
	 * first - this should normally be a MODINFO_NAME field.
	 */
	if (type == 0) {
	    type = hdr[0];
	} else {
	    if (hdr[0] == type)
		break;
	}
	
	/* 
	 * Attribute match? Return pointer to data.
	 * Consumer may safely assume that size value precedes	
	 * data.
	 */
	if (hdr[0] == inf)
	    return(curp + (sizeof(uint32_t) * 2));

	/* skip to next field */
	next = sizeof(uint32_t) * 2 + hdr[1];
	next = roundup(next, sizeof(u_long));
	curp += next;
    }
    return(NULL);
}

/*
 * Delete a preload record by name.
 */
void
preload_delete_name(const char *name)
{
    caddr_t	addr, curp;
    uint32_t	*hdr, sz;
    int		next;
    int		clearing;

    addr = 0;
    sz = 0;
    
    if (preload_metadata != NULL) {

	clearing = 0;
	curp = preload_metadata;
	for (;;) {
	    hdr = (uint32_t *)curp;
	    if (hdr[0] == MODINFO_NAME || (hdr[0] == 0 && hdr[1] == 0)) {
		/* Free memory used to store the file. */
		if (addr != 0 && sz != 0)
		    kmem_bootstrap_free((vm_offset_t)addr, sz);
		addr = 0;
		sz = 0;

		if (hdr[0] == 0)
		    break;
		if (!strcmp(name, curp + sizeof(uint32_t) * 2))
		    clearing = 1;	/* got it, start clearing */
		else if (clearing) {
		    clearing = 0;	/* at next one now.. better stop */
		}
	    }
	    if (clearing) {
		if (hdr[0] == MODINFO_ADDR)
		    addr = *(caddr_t *)(curp + sizeof(uint32_t) * 2);
		else if (hdr[0] == MODINFO_SIZE)
		    sz = *(uint32_t *)(curp + sizeof(uint32_t) * 2);
		hdr[0] = MODINFO_EMPTY;
	    }

	    /* skip to next field */
	    next = sizeof(uint32_t) * 2 + hdr[1];
	    next = roundup(next, sizeof(u_long));
	    curp += next;
	}
    }
}

void *
preload_fetch_addr(caddr_t mod)
{
	caddr_t *mdp;

	mdp = (caddr_t *)preload_search_info(mod, MODINFO_ADDR);
	if (mdp == NULL)
		return (NULL);
	return (*mdp + preload_addr_relocate);
}

size_t
preload_fetch_size(caddr_t mod)
{
	size_t *mdp;

	mdp = (size_t *)preload_search_info(mod, MODINFO_SIZE);
	if (mdp == NULL)
		return (0);
	return (*mdp);
}

/* Called from locore.  Convert physical pointers to kvm. Sigh. */
void
preload_bootstrap_relocate(vm_offset_t offset)
{
    caddr_t	curp;
    uint32_t	*hdr;
    vm_offset_t	*ptr;
    int		next;
    
    if (preload_metadata != NULL) {
	
	curp = preload_metadata;
	for (;;) {
	    hdr = (uint32_t *)curp;
	    if (hdr[0] == 0 && hdr[1] == 0)
		break;

	    /* Deal with the ones that we know we have to fix */
	    switch (hdr[0]) {
	    case MODINFO_ADDR:
	    case MODINFO_METADATA|MODINFOMD_SSYM:
	    case MODINFO_METADATA|MODINFOMD_ESYM:
		ptr = (vm_offset_t *)(curp + (sizeof(uint32_t) * 2));
		*ptr += offset;
		break;
	    }
	    /* The rest is beyond us for now */

	    /* skip to next field */
	    next = sizeof(uint32_t) * 2 + hdr[1];
	    next = roundup(next, sizeof(u_long));
	    curp += next;
	}
    }
}
