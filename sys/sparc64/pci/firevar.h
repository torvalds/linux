/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 by Marius Strobl <marius@FreeBSD.org>.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
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
 *
 * $FreeBSD$
 */

#ifndef _SPARC64_PCI_FIREVAR_H_
#define	_SPARC64_PCI_FIREVAR_H_

struct fire_softc {
	/*
	 * This is here so that we can hook up the common bus interface
	 * methods in ofw_pci.c directly.
	 */
	struct ofw_pci_softc		sc_ops;

	struct iommu_state		sc_is;
	struct bus_dma_methods		sc_dma_methods;

	struct mtx			sc_msi_mtx;
	struct mtx			sc_pcib_mtx;

	struct resource			*sc_mem_res[FIRE_NREG];
	struct resource			*sc_irq_res[FIRE_NINTR];
	void				*sc_ihand[FIRE_NINTR];

	device_t			sc_dev;

	uint64_t			*sc_msiq;
	u_char				*sc_msi_bitmap;
	uint32_t			*sc_msi_msiq_table;
	u_char				*sc_msiq_bitmap;
	uint64_t			sc_msi_addr32;
	uint64_t			sc_msi_addr64;
	uint32_t			sc_msi_count;
	uint32_t			sc_msi_first;
	uint32_t			sc_msi_data_mask;
	uint32_t			sc_msix_data_width;
	uint32_t			sc_msiq_count;
	uint32_t			sc_msiq_size;
	uint32_t			sc_msiq_first;
	uint32_t			sc_msiq_ino_first;

	u_int				sc_mode;
#define	FIRE_MODE_FIRE			0
#define	FIRE_MODE_OBERON		1

	u_int				sc_flags;
#define	FIRE_MSIX			(1 << 0)

	uint32_t			sc_ign;

	uint32_t			sc_stats_ilu_err;
	uint32_t			sc_stats_jbc_ce_async;
	uint32_t			sc_stats_jbc_unsol_int;
	uint32_t			sc_stats_jbc_unsol_rd;
	uint32_t			sc_stats_mmu_err;
	uint32_t			sc_stats_tlu_ce;
	uint32_t			sc_stats_tlu_oe_non_fatal;
	uint32_t			sc_stats_tlu_oe_rx_err;
	uint32_t			sc_stats_tlu_oe_tx_err;
	uint32_t			sc_stats_ubc_dmardue;
};

#endif /* !_SPARC64_PCI_FIREVAR_H_ */
