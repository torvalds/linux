/* $OpenBSD: mcpcia.c,v 1.9 2025/06/29 15:55:21 miod Exp $ */
/* $NetBSD: mcpcia.c,v 1.20 2007/03/04 05:59:11 christos Exp $ */

/*-
 * Copyright (c) 1999 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center.
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

/*
 * Copyright (c) 1998 by Matthew Jacob
 * NASA AMES Research Center.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice immediately at the beginning of the file, without modification,
 *    this list of conditions, and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
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

/*
 * MCPCIA mcbus to PCI bus adapter
 * found on AlphaServer 4100 systems.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/malloc.h>

#include <machine/autoconf.h>
#include <machine/rpb.h>
#include <machine/sysarch.h>

#include <alpha/mcbus/mcbusreg.h>
#include <alpha/mcbus/mcbusvar.h>
#include <alpha/pci/mcpciareg.h>
#include <alpha/pci/mcpciavar.h>
#include <alpha/pci/pci_kn300.h>

#define KV(_addr)	((caddr_t)ALPHA_PHYS_TO_K0SEG((_addr)))
#define	MCPCIA_SYSBASE(mc)	\
	((((unsigned long) (mc)->cc_gid) << MCBUS_GID_SHIFT) | \
	 (((unsigned long) (mc)->cc_mid) << MCBUS_MID_SHIFT) | \
	 (MCBUS_IOSPACE))

#define	MCPCIA_PROBE(mid, gid)	\
	badaddr((void *)KV(((((unsigned long) gid) << MCBUS_GID_SHIFT) | \
	 (((unsigned long) mid) << MCBUS_MID_SHIFT) | \
	 (MCBUS_IOSPACE) | MCPCIA_PCI_BRIDGE | _MCPCIA_PCI_REV)), \
	sizeof(u_int32_t))

int	mcpciamatch (struct device *, void *, void *);
void	mcpciaattach (struct device *, struct device *, void *);
void	mcpcia_config_cleanup (void);

int	mcpciaprint (void *, const char *);

const struct cfattach mcpcia_ca = {
	sizeof(struct mcpcia_softc), mcpciamatch, mcpciaattach
};

struct cfdriver mcpcia_cd = {
	NULL, "mcpcia", DV_DULL,
};

/*
 * We have one statically-allocated mcpcia_config structure; this is
 * the one used for the console (which, coincidentally, is the only
 * MCPCIA with an EISA adapter attached to it).
 */
struct mcpcia_config mcpcia_console_configuration;

int
mcpciaprint(void *aux, const char *pnp)
{
       register struct pcibus_attach_args *pba = aux;
       /* only PCIs can attach to MCPCIA for now */
       if (pnp)
		printf("%s at %s", pba->pba_busname, pnp);
       printf(" bus %d", pba->pba_bus);
       return (UNCONF);
}

int
mcpciamatch(struct device *parent, void *cf, void *aux)
{
	struct mcbus_dev_attach_args *ma = aux;

	if (ma->ma_type == MCBUS_TYPE_PCI)
		return (1);

	return (0);
}

void
mcpciaattach(struct device *parent, struct device *self, void *aux)
{
	static int first = 1;
	struct mcbus_dev_attach_args *ma = aux;
	struct mcpcia_softc *mcp = (struct mcpcia_softc *)self;
	struct mcpcia_config *ccp;
	struct pcibus_attach_args pba;
	u_int32_t ctl;

	/*
	 * Make sure this MCPCIA exists...
	 */
	if (MCPCIA_PROBE(ma->ma_mid, ma->ma_gid)) {
		mcp->mcpcia_cc = NULL;
		printf(" (not present)\n");
		return;
	}
	printf("\n");

	/*
	 * Determine if we're the console's MCPCIA.
	 */
	if (ma->ma_mid == mcpcia_console_configuration.cc_mid &&
	    ma->ma_gid == mcpcia_console_configuration.cc_gid)
		ccp = &mcpcia_console_configuration;
	else {
		ccp = malloc(sizeof(*ccp), M_DEVBUF, M_WAITOK | M_ZERO);

		ccp->cc_mid = ma->ma_mid;
		ccp->cc_gid = ma->ma_gid;
	}
	mcp->mcpcia_cc = ccp;
	ccp->cc_sc = mcp;

	/* This initializes cc_sysbase so we can do register access. */
	mcpcia_init0(ccp, 1);

	ctl = REGVAL(MCPCIA_PCI_REV(ccp));
	printf("%s: Horse rev %d, %s handed Saddle rev %d, CAP rev %d\n",
	    mcp->mcpcia_dev.dv_xname, HORSE_REV(ctl),
	    (SADDLE_TYPE(ctl) & 1) ? "right" : "left", SADDLE_REV(ctl),
	    CAP_REV(ctl));

	mcpcia_dma_init(ccp);

	/*
	 * Set up interrupts
	 */
	pci_kn300_pickintr(ccp, first);
	first = 0;

	/*
	 * Attach PCI bus
	 */
	bzero(&pba, sizeof(pba));
	pba.pba_busname = "pci";
	pba.pba_iot = &ccp->cc_iot;
	pba.pba_memt = &ccp->cc_memt;
	pba.pba_dmat =
	    alphabus_dma_get_tag(&ccp->cc_dmat_direct, ALPHA_BUS_PCI);
	pba.pba_pc = &ccp->cc_pc;
	pba.pba_domain = pci_ndomains++;
	pba.pba_bus = 0;

	config_found(self, &pba, mcpciaprint);

	/*
	 * Clear any errors that may have occurred during the probe
	 * sequence.
	 */
	REGVAL(MCPCIA_CAP_ERR(ccp)) = 0xFFFFFFFF;
	alpha_mb();
}

void
mcpcia_init(void)
{
	struct mcpcia_config *ccp = &mcpcia_console_configuration;
	int i;

	/*
	 * Look for all of the MCPCIAs on the system.  One of them
	 * will have an EISA attached to it.  This MCPCIA is the
	 * only one that can be used for the console.  Once we find
	 * that one, initialize it.
	 */
	for (i = 0; i < MCPCIA_PER_MCBUS; i++) {
		ccp->cc_mid = mcbus_mcpcia_probe_order[i];
		/*
		 * XXX If we ever support more than one MCBUS, we'll
		 * XXX have to probe for them, and map them to unit
		 * XXX numbers.
		 */
		ccp->cc_gid = MCBUS_GID_FROM_INSTANCE(0);
		ccp->cc_sysbase = MCPCIA_SYSBASE(ccp);

		if (badaddr((void *)ALPHA_PHYS_TO_K0SEG(MCPCIA_PCI_REV(ccp)),
		    sizeof(u_int32_t)))
			continue;

		if (EISA_PRESENT(REGVAL(MCPCIA_PCI_REV(ccp)))) {
			mcpcia_init0(ccp, 0);
			return;
		}
	}

	panic("mcpcia_init: unable to find EISA bus");
}

void
mcpcia_init0(struct mcpcia_config *ccp, int mallocsafe)
{
	u_int32_t ctl;
	
	snprintf(ccp->pc_io_ex_name, sizeof ccp->pc_io_ex_name,
	    "mcpcia%d_bus_io", ccp->cc_mid);
	snprintf(ccp->pc_mem_dex_name, sizeof ccp->pc_mem_dex_name,
	    "mcpciad%d_bus_mem", ccp->cc_mid);
	snprintf(ccp->pc_mem_dex_name, sizeof ccp->pc_mem_sex_name,
	    "mcpcias%d_bus_mem", ccp->cc_mid);

	if (!ccp->cc_initted) {
		/* don't do these twice since they set up extents */
		mcpcia_bus_io_init(&ccp->cc_iot, ccp);
		mcpcia_bus_mem_init(&ccp->cc_memt, ccp);
	}
	ccp->cc_mallocsafe = mallocsafe;

	mcpcia_pci_init(&ccp->cc_pc, ccp);

	/*
	 * Establish a precalculated base for convenience's sake.
	 */
	ccp->cc_sysbase = MCPCIA_SYSBASE(ccp);

	/*
	 * Disable interrupts and clear errors prior to probing
	 */
	REGVAL(MCPCIA_INT_MASK0(ccp)) = 0;
	REGVAL(MCPCIA_INT_MASK1(ccp)) = 0;
	REGVAL(MCPCIA_CAP_ERR(ccp)) = 0xFFFFFFFF;
	alpha_mb();

	if (ccp == &mcpcia_console_configuration) {
		/*
		 * Use this opportunity to also find out the MID and CPU
		 * type of the currently running CPU (that's us, billybob....)
		 */
		ctl = REGVAL(MCPCIA_WHOAMI(ccp));
		mcbus_primary.mcbus_cpu_mid = MCBUS_CPU_MID(ctl);
		if ((MCBUS_CPU_INFO(ctl) & CPU_Fill_Err) == 0 &&
		    mcbus_primary.mcbus_valid == 0) {
			mcbus_primary.mcbus_bcache =
			    MCBUS_CPU_INFO(ctl) & CPU_BCacheMask;
			mcbus_primary.mcbus_valid = 1;
		}
		alpha_mb();
	}

	alpha_pci_chipset = &ccp->cc_pc;
	alpha_pci_chipset->pc_name = "mcpcia";
	alpha_pci_chipset->pc_hae_mask = 0;
	alpha_pci_chipset->pc_dense = MCPCIA_PCI_DENSE;
	
	ccp->cc_initted = 1;
}

void
mcpcia_config_cleanup(void)
{
	volatile u_int32_t ctl;
	struct mcpcia_softc *mcp;
	struct mcpcia_config *ccp;
	int i;
	extern struct cfdriver mcpcia_cd;

	/*
	 * Turn on Hard, Soft error interrupts. Maybe i2c too.
	 */
	for (i = 0; i < mcpcia_cd.cd_ndevs; i++) {
		if ((mcp = mcpcia_cd.cd_devs[i]) == NULL)
			continue;
		
		ccp = mcp->mcpcia_cc;
		if (ccp == NULL)
			continue;

		ctl = REGVAL(MCPCIA_INT_MASK0(ccp));
		ctl |= MCPCIA_GEN_IENABL;
		REGVAL(MCPCIA_INT_MASK0(ccp)) = ctl;
		alpha_mb();

		/* force stall while write completes */
		ctl = REGVAL(MCPCIA_INT_MASK0(ccp));
	}
}
