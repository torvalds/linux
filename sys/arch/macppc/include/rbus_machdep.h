/*	$OpenBSD: rbus_machdep.h,v 1.6 2011/03/23 16:54:36 pirofti Exp $ */
/*	$NetBSD: rbus_machdep.h,v 1.2 1999/10/15 06:43:05 haya Exp $	*/

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


#if !defined _MACHINE_RBUS_MACHDEP_H_
#define _MACHINE_RBUS_MACHDEP_H_

struct pci_attach_args;		/* XXX */

void _bus_space_unmap(bus_space_tag_t, bus_space_handle_t,
			     bus_size_t, bus_addr_t *);

#define md_space_map(rbt, physaddr, size, flags, bshp) \
	bus_space_map((rbt)->rb_bt, (physaddr), (size), (flags), (bshp))

/* XXX */
bus_addr_t bus_space_unmap_p(bus_space_tag_t t, bus_space_handle_t bsh,
                          bus_size_t size);


#define md_space_unmap(rbt, bsh, size, adrp) \
	do { \
		*adrp = bus_space_unmap_p((rbt)->rb_bt, (bsh), (size)); \
		if ((rbt)->rb_bt->bus_io) { \
			*adrp = *adrp & 0xffff; \
		} \
	} while (0)

rbus_tag_t rbus_pccbb_parent_io(struct device *self,
    struct pci_attach_args *pa);
rbus_tag_t rbus_pccbb_parent_mem(struct device *self,
    struct pci_attach_args *pa);

void	pccbb_attach_hook(struct device *, struct device *,
	    struct pci_attach_args *);

#endif /* _MACHINE_RBUS_MACHDEP_H_ */
