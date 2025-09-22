/*	$OpenBSD: rbus_machdep.h,v 1.10 2024/05/22 05:51:49 jsg Exp $	*/
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

#ifndef _MACHINE_RBUS_MACHDEP_H_
#define _MACHINE_RBUS_MACHDEP_H_

struct pci_attach_args;		/* XXX */

#define md_space_map(rbt, physaddr, size, flags, bshp) \
	_bus_space_map((rbt)->rb_bt, (physaddr), (size), (flags), (bshp))

#define md_space_unmap(rbt, bsh, size, adrp) \
	_bus_space_unmap((rbt)->rb_bt, (bsh), (size), (adrp))

rbus_tag_t	rbus_pccbb_parent_io(struct device *,
		    struct pci_attach_args *);
rbus_tag_t	rbus_pccbb_parent_mem(struct device *,
		    struct pci_attach_args *);

void		pccbb_attach_hook(struct device *, struct device *,
		    struct pci_attach_args *);

#endif /* _MACHINE_RBUS_MACHDEP_H_ */
