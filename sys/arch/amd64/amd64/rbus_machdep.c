/*	$OpenBSD: rbus_machdep.c,v 1.13 2015/03/14 03:38:46 jsg Exp $	*/
/*	$NetBSD: rbus_machdep.c,v 1.2 1999/10/15 06:43:06 haya Exp $	*/

/*
 * Copyright (c) 1999
 *     HAYAKAWA Koichi.  All rights reserved.
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/extent.h>

#include <uvm/uvm_extern.h>

#include <machine/bus.h>
#include <dev/cardbus/rbus.h>

#include <dev/pci/pcivar.h>

#ifndef RBUS_MEM_START
/* Avoid the ISA hole and everything below it. */
#define RBUS_MEM_START	0x00100000
#endif

#ifndef RBUS_IO_START
/* Try to avoid onboard legacy devices; just a guess. */
#define RBUS_IO_START	0xa000
#endif

struct rbustag rbus_null;

rbus_tag_t
rbus_pccbb_parent_mem(struct device *self, struct pci_attach_args *pa)
{
	struct extent *ex = pa->pa_memex;
	bus_addr_t start;
	bus_size_t size;

	if (ex == NULL)
		return &rbus_null;

	start = RBUS_MEM_START;
	size = ex->ex_end - start;

	return (rbus_new_root_share(pa->pa_memt, ex, start, size));
}

rbus_tag_t
rbus_pccbb_parent_io(struct device *self, struct pci_attach_args *pa)
{
	struct extent *ex = pa->pa_ioex;
	bus_addr_t start;
	bus_size_t size;

	if (ex == NULL)
		return &rbus_null;

	start = ex->ex_start;
	if (ex == pciio_ex) {
		/*
		 * We're on the root bus, or behind a subtractive
		 * decode PCI-PCI bridge.  To avoid conflicts with
		 * onboard legacy devices, we only make a subregion
		 * available.
		 */
		start = max(start, RBUS_IO_START);
	}
	size = ex->ex_end - start;

	return (rbus_new_root_share(pa->pa_iot, ex, start, size));
}

void
pccbb_attach_hook(struct device *parent, struct device *self,
    struct pci_attach_args *pa)
{
}
