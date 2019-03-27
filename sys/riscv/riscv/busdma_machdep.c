/*-
 * Copyright (c) 1997, 1998 Justin T. Gibbs.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
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
#include <sys/malloc.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/lock.h>
#include <sys/memdesc.h>
#include <sys/mutex.h>
#include <sys/uio.h>
#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/pmap.h>

#include <machine/bus.h>

int
_bus_dmamap_load_phys(bus_dma_tag_t dmat, bus_dmamap_t map, vm_paddr_t buf,
    bus_size_t buflen, int flags, bus_dma_segment_t *segs, int *segp)
{

	panic("busdma");
}

int
_bus_dmamap_load_ma(bus_dma_tag_t dmat, bus_dmamap_t map, struct vm_page **ma,
    bus_size_t tlen, int ma_offs, int flags, bus_dma_segment_t *segs,
    int *segp)
{

	panic("busdma");
}

int
_bus_dmamap_load_buffer(bus_dma_tag_t dmat, bus_dmamap_t map, void *buf,
    bus_size_t buflen, pmap_t pmap, int flags, bus_dma_segment_t *segs,
    int *segp)
{

	panic("busdma");
}

void
_bus_dmamap_waitok(bus_dma_tag_t dmat, bus_dmamap_t map,
    struct memdesc *mem, bus_dmamap_callback_t *callback, void *callback_arg)
{

	panic("busdma");
}

bus_dma_segment_t *
_bus_dmamap_complete(bus_dma_tag_t dmat, bus_dmamap_t map,
    bus_dma_segment_t *segs, int nsegs, int error)
{

	panic("busdma");
}

/*
 * Release the mapping held by map.
 */
void
bus_dmamap_unload(bus_dma_tag_t dmat, bus_dmamap_t map)
{

	panic("busdma");
}

void
bus_dmamap_sync(bus_dma_tag_t dmat, bus_dmamap_t map, bus_dmasync_op_t op)
{

	panic("busdma");
}
