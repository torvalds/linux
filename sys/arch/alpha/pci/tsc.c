/* $OpenBSD: tsc.c,v 1.19 2025/06/29 15:55:21 miod Exp $ */
/* $NetBSD: tsc.c,v 1.3 2000/06/25 19:17:40 thorpej Exp $ */

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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/malloc.h>

#include <machine/autoconf.h>
#include <machine/rpb.h>

#include <dev/isa/isareg.h>
#include <dev/isa/isavar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <alpha/pci/tsreg.h>
#include <alpha/pci/tsvar.h>

#ifdef DEC_6600
#include <alpha/pci/pci_6600.h>
#endif

#define tsc() { Generate ctags(1) key. }

int	tscmatch(struct device *, void *, void *);
void	tscattach(struct device *, struct device *, void *);

const struct cfattach tsc_ca = {
	sizeof(struct device), tscmatch, tscattach,
};

struct cfdriver tsc_cd = {
	NULL, "tsc", DV_DULL,
};

struct tsp_config tsp_configuration[4];

static int tscprint(void *, const char *pnp);

int	tspmatch(struct device *, void *, void *);
void	tspattach(struct device *, struct device *, void *);

const struct cfattach tsp_ca = {
	sizeof(struct tsp_softc), tspmatch, tspattach,
};

struct cfdriver tsp_cd = {
	NULL, "tsp", DV_DULL,
};


static int tspprint(void *, const char *pnp);

/* There can be only one */
static int tscfound;

/* Which hose is the display console connected to? */
int tsp_console_hose;

int
tscmatch(struct device *parent, void *match, void *aux)
{
	struct mainbus_attach_args *ma = aux;

	switch (cputype) {
	case ST_DEC_6600:
	case ST_DEC_TITAN:
		return strcmp(ma->ma_name, tsc_cd.cd_name) == 0 && !tscfound;
	default:
		return 0;
	}
}

void
tscattach(struct device *parent, struct device *self, void *aux)
{
	int i;
	int nbus;
	u_int64_t csc, aar;
	struct tsp_attach_args tsp;
	struct mainbus_attach_args *ma = aux;
	int titan = cputype == ST_DEC_TITAN;

	tscfound = 1;

	csc = LDQP(TS_C_CSC);

	nbus = 1 + (CSC_BC(csc) >= 2);
	printf(": 2127%c Chipset, Cchip rev %d\n"
	       "%s%d: %c Dchips, %d memory bus%s of %d bytes\n",
	    titan ? '4' : '2', (int)MISC_REV(LDQP(TS_C_MISC)),
	    ma->ma_name, ma->ma_slot, "2448"[CSC_BC(csc)],
	    nbus, nbus > 1 ? "es" : "", 16 + 16 * ((csc & CSC_AW) != 0));
	printf("%s%d: arrays present: ", ma->ma_name, ma->ma_slot);
	for(i = 0; i < 4; ++i) {
		aar = LDQP(TS_C_AAR0 + i * TS_STEP);
		printf("%s%dMB%s", i ? ", " : "", (8 << AAR_ASIZ(aar)) & ~0xf,
		    aar & AAR_SPLIT ? " (split)" : "");
	}
	printf(", Dchip 0 rev %d\n", (int)LDQP(TS_D_DREV) & 0xf);

	tsp.tsp_name = "tsp";
	tsp.tsp_slot = 0;
	config_found(self, &tsp, tscprint);
	if (titan) {
		tsp.tsp_slot += 2;
		config_found(self, &tsp, tscprint);
	}

	if (csc & CSC_P1P) {
		tsp.tsp_slot = 1;
		config_found(self, &tsp, tscprint);
		if (titan) {
			tsp.tsp_slot += 2;
			config_found(self, &tsp, tscprint);
		}
	}

	tsp.tsp_name = "tsciic";
	tsp.tsp_slot = -1;
	config_found(self, &tsp, tscprint);
}

static int
tscprint(void *aux, const char *p)
{
	struct tsp_attach_args *tsp = aux;

	if (p)
		printf("%s at %s", tsp->tsp_name, p);
	if (tsp->tsp_slot >= 0)
		printf(" hose %d", tsp->tsp_slot);
	return UNCONF;
}

#define tsp() { Generate ctags(1) key. }

int
tspmatch(struct device *parent, void *match, void *aux)
{
	struct tsp_attach_args *t = aux;

	switch (cputype) {
	case ST_DEC_6600:
	case ST_DEC_TITAN:
		return strcmp(t->tsp_name, tsp_cd.cd_name) == 0;
	default:
		return 0;
	}
}

void
tspattach(struct device *parent, struct device *self, void *aux)
{
	struct pcibus_attach_args pba;
	struct tsp_attach_args *t = aux;
	struct tsp_config *pcp;

	printf("\n");
	pcp = tsp_init(1, t->tsp_slot);

	tsp_dma_init(self, pcp);
	
	/*
	 * Do PCI memory initialization that needs to be deferred until
	 * malloc is safe.  On the Tsunami, we need to do this after
	 * DMA is initialized, as well.
	 */
	tsp_bus_mem_init2(pcp);

	pci_6600_pickintr(pcp);

	bzero(&pba, sizeof(pba));
	pba.pba_busname = "pci";
	pba.pba_iot = &pcp->pc_iot;
	pba.pba_memt = &pcp->pc_memt;
	pba.pba_dmat =
	    alphabus_dma_get_tag(&pcp->pc_dmat_direct, ALPHA_BUS_PCI);
	pba.pba_pc = &pcp->pc_pc;
	pba.pba_domain = pci_ndomains++;
	pba.pba_bus = 0;
#ifdef	notyet
	pba.pba_flags = PCI_FLAGS_IO_ENABLED | PCI_FLAGS_MEM_ENABLED |
	    PCI_FLAGS_MRL_OKAY | PCI_FLAGS_MRM_OKAY | PCI_FLAGS_MWI_OKAY;
#endif
	config_found(self, &pba, tspprint);
}

struct tsp_config *
tsp_init(int mallocsafe, int n)	/* hose number */
{
	struct tsp_config *pcp;
	int titan = cputype == ST_DEC_TITAN;

	KASSERT(n >= 0 && n < nitems(tsp_configuration));
	pcp = &tsp_configuration[n];
	pcp->pc_pslot = n;
	pcp->pc_iobase = TS_Pn(n, 0);
	pcp->pc_csr = S_PAGE(TS_Pn(n & 1, P_CSRBASE));
	if (n & 2) {
		/* `A' port of PA Chip */
		pcp->pc_csr++;
	}
	if (titan) {
		/* same address on G and A ports */
		pcp->pc_tlbia = &pcp->pc_csr->port.g.tsp_tlbia.tsg_r;
	} else {
		pcp->pc_tlbia = &pcp->pc_csr->port.p.tsp_tlbia.tsg_r;
	}
	snprintf(pcp->pc_io_ex_name, sizeof pcp->pc_io_ex_name,
	    "tsp%d_bus_io", n);
	snprintf(pcp->pc_mem_ex_name, sizeof pcp->pc_mem_ex_name,
	    "tsp%d_bus_mem", n);

	if (!pcp->pc_initted) {
		tsp_bus_io_init(&pcp->pc_iot, pcp);
		tsp_bus_mem_init(&pcp->pc_memt, pcp);
	}
	pcp->pc_mallocsafe = mallocsafe;
	tsp_pci_init(&pcp->pc_pc, pcp);
	alpha_pci_chipset = &pcp->pc_pc;
	if (titan)
		alpha_pci_chipset->pc_name = "titan";
	else
		alpha_pci_chipset->pc_name = "tsunami";
	alpha_pci_chipset->pc_mem = P_PCI_MEM;
	alpha_pci_chipset->pc_ports = P_PCI_IO;
	alpha_pci_chipset->pc_hae_mask = 0;
	alpha_pci_chipset->pc_dense = TS_P0(0);	/* XXX */
	alpha_pci_chipset->pc_bwx = 1;
	pcp->pc_initted = 1;
	return pcp;
}

static int
tspprint(void *aux, const char *p)
{
	register struct pcibus_attach_args *pci = aux;

	if (p)
		printf("%s at %s", pci->pba_busname, p);
	printf(" bus %d", pci->pba_bus);
	return UNCONF;
}
