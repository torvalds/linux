/*	$OpenBSD: auviavar.h,v 1.12 2010/09/12 02:03:35 jakemsr Exp $ */
/*	$NetBSD: auviavar.h,v 1.1 2000/03/31 04:45:29 tsarna Exp $	*/

/*-
 * Copyright (c) 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Tyler C. Sarna.
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


#ifndef _DEV_PCI_AUVIAVAR_H_
#define _DEV_PCI_AUVIAVAR_H_

struct auvia_softc_chan {
	void (*sc_intr)(void *);
	void *sc_arg;

	struct auvia_dma_op *sc_dma_ops;
	struct auvia_dma *sc_dma_ops_dma;
	u_int16_t sc_dma_op_count;
	int sc_base;
	u_int16_t sc_reg;
};

struct auvia_softc {
	struct device sc_dev;
	void *sc_ih;			/* interrupt handle */

	char sc_revision[8];
	u_int sc_flags;
#define	AUVIA_FLAGS_VT8233	0x0001

	pci_chipset_tag_t sc_pc;
	pcitag_t sc_pt;

	bus_space_tag_t sc_iot;
	bus_space_handle_t sc_ioh;
	bus_dma_tag_t sc_dmat;

	struct ac97_host_if host_if;
	struct ac97_codec_if *codec_if;
	int bufsize;
	int sc_spdif;

	pcireg_t sc_pci_junk;

	struct auvia_dma *sc_dmas;

	struct auvia_softc_chan sc_play, sc_record;
};

#define AUVIA_DMALIST_MAX	255

#endif
