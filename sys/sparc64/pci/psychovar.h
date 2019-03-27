/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 1999, 2000 Matthew R. Green
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: NetBSD: psychovar.h,v 1.15 2008/05/29 14:51:26 mrg Exp
 *
 * $FreeBSD$
 */

#ifndef _SPARC64_PCI_PSYCHOVAR_H_
#define _SPARC64_PCI_PSYCHOVAR_H_

/*
 * Per-PCI bus on mainbus softc structure; one for sabre, or two
 * per pair of psychos.
 */
struct psycho_softc {
	/*
	 * This is here so that we can hook up the common bus interface
	 * methods in ofw_pci.c directly.
	 */
	struct ofw_pci_softc		sc_ops;

	struct iommu_state		*sc_is;
	struct bus_dma_methods		*sc_dma_methods;

	struct mtx			*sc_mtx;

	struct resource			*sc_mem_res;
	struct resource			*sc_irq_res[PSYCHO_NINTR];
	void				*sc_ihand[PSYCHO_NINTR];

	uint8_t				sc_pci_hpbcfg[16];

	SLIST_ENTRY(psycho_softc)	sc_link;

	device_t			sc_dev;

	bus_addr_t			sc_pcictl;

	u_int				sc_mode;
#define	PSYCHO_MODE_SABRE		0
#define	PSYCHO_MODE_PSYCHO		1

	/* Bus A or B of a psycho pair? */
	u_int				sc_half;

	/* Interrupt Group Number for this device */
	uint32_t			sc_ign;
};

#endif /* !_SPARC64_PCI_PSYCHOVAR_H_ */
