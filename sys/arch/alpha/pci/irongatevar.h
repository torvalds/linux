/*	$OpenBSD: irongatevar.h,v 1.5 2008/06/26 05:42:08 ray Exp $	*/
/* $NetBSD: irongatevar.h,v 1.3 2000/11/29 06:29:10 thorpej Exp $ */

/*-
 * Copyright (c) 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <dev/isa/isavar.h>
#include <dev/pci/pcivar.h>

/*
 * AMD 751 chipset's configuration.
 *
 * All of the information that the chipset-specific functions need to
 * do their dirty work (and more!).
 */
struct irongate_config {
	int	ic_initted;

	struct alpha_bus_space ic_iot, ic_memt;
	struct alpha_pci_chipset ic_pc;

	struct alpha_bus_dma_tag ic_dmat_pci;
	struct alpha_bus_dma_tag ic_dmat_isa;

	u_int32_t ic_rev;

	struct extent *ic_io_ex, *ic_mem_ex;
	int	ic_mallocsafe;
};

void	irongate_init(struct irongate_config *, int);
void	irongate_pci_init(pci_chipset_tag_t, void *);
void	irongate_dma_init(struct irongate_config *);

void	irongate_bus_io_init(bus_space_tag_t, void *);
void	irongate_bus_mem_init(bus_space_tag_t, void *);

void	irongate_bus_mem_init2(bus_space_tag_t, void *);

pcireg_t irongate_conf_read0(void *, pcitag_t, int);
