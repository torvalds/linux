/* $OpenBSD: tsvar.h,v 1.8 2016/01/12 17:20:02 deraadt Exp $ */
/* $NetBSD: tsvar.h,v 1.1 1999/06/29 06:46:47 ross Exp $ */

/*-
 * Copyright (c) 1999 by Ross Harvey.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Ross Harvey.
 * 4. The name of Ross Harvey may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY ROSS HARVEY ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURP0SE
 * ARE DISCLAIMED.  IN NO EVENT SHALL ROSS HARVEY BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <dev/pci/pcivar.h>
#include <alpha/pci/pci_sgmap_pte64.h>

#define	tsvar() { Generate ctags(1) key. }

struct tsp_config {
	int	pc_pslot;		/* Pchip 0 or 1 */
	int	pc_initted;		/* Initialized */
	u_int64_t pc_iobase;		/* All Pchip space starts here */
	struct	ts_pchip *pc_csr;	/* Pchip CSR space starts here */
	volatile u_int64_t *pc_tlbia;	/* Pchip TLBIA register address */

	struct	alpha_bus_space pc_iot, pc_memt;
	struct	alpha_pci_chipset pc_pc;

	struct	alpha_bus_dma_tag pc_dmat_direct;
	struct	alpha_bus_dma_tag pc_dmat_sgmap;

	struct alpha_sgmap pc_sgmap;

	u_int32_t pc_hae_mem;
	u_int32_t pc_hae_io;

	struct	extent *pc_io_ex, *pc_mem_ex;
	char	pc_io_ex_name[16], pc_mem_ex_name[16];
	long	pc_io_ex_storage[EXTENT_FIXED_STORAGE_SIZE(8) / sizeof(long)];
	long	pc_mem_ex_storage[EXTENT_FIXED_STORAGE_SIZE(8) / sizeof(long)];
	int	pc_mallocsafe;
};

struct tsp_softc {
	struct	device sc_dev;
	struct	tsp_config *sc_ccp;
};

struct tsp_attach_args {
	char	*tsp_name;
	int	tsp_slot;
};

extern int tsp_console_hose;

struct	tsp_config *tsp_init(int, int);
void	tsp_pci_init(pci_chipset_tag_t, void *);
void	tsp_dma_init(struct device *, struct tsp_config *);

void	tsp_bus_io_init(bus_space_tag_t, void *);
void	tsp_bus_mem_init(bus_space_tag_t, void *);

void tsp_bus_mem_init2(void *);
