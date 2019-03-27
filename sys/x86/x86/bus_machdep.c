/*-
 * Copyright (c) 2015 Marcel Moolenaar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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
#include <sys/systm.h>
#include <x86/bus.h>

#include <vm/vm.h>
#include <vm/pmap.h>

/*
 * Implementation of bus_space_map(), which effectively is a thin
 * wrapper around pmap_mapdev() for memory mapped I/O space. It's
 * implemented here and not in <x86/bus.h> to avoid pollution.
 */
int
bus_space_map(bus_space_tag_t tag, bus_addr_t addr, bus_size_t size,
    int flags __unused, bus_space_handle_t *bshp)
{

	*bshp = (tag == X86_BUS_SPACE_MEM)
	    ? (uintptr_t)pmap_mapdev(addr, size)
	    : addr;
	return (0);
}

void
bus_space_unmap(bus_space_tag_t tag, bus_space_handle_t bsh, bus_size_t size)
{

	if (tag == X86_BUS_SPACE_MEM)
		pmap_unmapdev(bsh, size);
}
