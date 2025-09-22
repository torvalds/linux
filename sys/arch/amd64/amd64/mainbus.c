/*	$OpenBSD: mainbus.c,v 1.54 2025/09/16 12:18:10 hshoexer Exp $	*/
/*	$NetBSD: mainbus.c,v 1.1 2003/04/26 18:39:29 fvdl Exp $	*/

/*
 * Copyright (c) 1996 Christopher G. Demetriou.  All rights reserved.
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
 *      This product includes software developed by Christopher G. Demetriou
 *	for the NetBSD Project.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
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
#include <sys/device.h>

#include <machine/bus.h>
#include <machine/specialreg.h>
#include <machine/codepatch.h>

#include <dev/isa/isavar.h>
#include <dev/pci/pcivar.h>

#include "pci.h"
#include "isa.h"
#include "acpi.h"
#include "ipmi.h"
#include "bios.h"
#include "mpbios.h"
#include "vmm.h"
#include "pvbus.h"
#include "efifb.h"

#include <machine/cpuvar.h>
#include <machine/i82093var.h>

#include <dev/acpi/acpivar.h>

#if NIPMI > 0
#include <dev/ipmivar.h>
#endif

#if NPVBUS > 0
#include <dev/pv/pvvar.h>
#endif

#if NBIOS > 0
#include <machine/biosvar.h>
#endif

#if NEFIFB > 0
#include <machine/efifbvar.h>
#endif

void	replacemds(void);

int	mainbus_match(struct device *, void *, void *);
void	mainbus_attach(struct device *, struct device *, void *);

const struct cfattach mainbus_ca = {
	sizeof(struct device), mainbus_match, mainbus_attach
};

struct cfdriver mainbus_cd = {
	NULL, "mainbus", DV_DULL, CD_COCOVM
};

int	mainbus_print(void *, const char *);

union mainbus_attach_args {
	const char *mba_busname;		/* first elem of all */
	struct pcibus_attach_args mba_pba;
	struct isabus_attach_args mba_iba;
	struct cpu_attach_args mba_caa;
	struct apic_attach_args aaa_caa;
#if NIPMI > 0
	struct ipmi_attach_args mba_iaa;
#endif
#if NBIOS > 0
	struct bios_attach_args mba_bios;
#endif
#if NPVBUS > 0
	struct pvbus_attach_args mba_pvba;
#endif
#if NEFIFB > 0
	struct efifb_attach_args mba_eaa;
#endif
};

/*
 * This is set when the ISA bus is attached.  If it's not set by the
 * time it's checked below, then mainbus attempts to attach an ISA.
 */
int	isa_has_been_seen;

#if NMPBIOS > 0 || NACPI > 0
struct mp_bus *mp_busses;
int mp_nbusses;
struct mp_intr_map *mp_intrs;
int mp_nintrs;

struct mp_bus *mp_isa_bus;
struct mp_bus *mp_eisa_bus;

#ifdef MPVERBOSE
int mp_verbose = 1;
#else
int mp_verbose = 0;
#endif
#endif


/*
 * Probe for the mainbus; always succeeds.
 */
int
mainbus_match(struct device *parent, void *match, void *aux)
{
	return (1);
}

/*
 * Attach the mainbus.
 */
void
mainbus_attach(struct device *parent, struct device *self, void *aux)
{
#if NPCI > 0
	union mainbus_attach_args	mba;
#endif
#if NVMM > 0
	extern int vmm_enabled(void);
#endif
	extern void			(*setperf_setup)(struct cpu_info *);

	printf("\n");

#if NEFIFB > 0
	efifb_cnremap();
#endif

#if NBIOS > 0
	{
		mba.mba_bios.ba_name = "bios";
		mba.mba_bios.ba_iot = X86_BUS_SPACE_IO;
		mba.mba_bios.ba_memt = X86_BUS_SPACE_MEM;
		config_found(self, &mba.mba_bios, mainbus_print);
	}
#endif

#if NIPMI > 0
	{
		memset(&mba.mba_iaa, 0, sizeof(mba.mba_iaa));
		mba.mba_iaa.iaa_name = "ipmi";
		mba.mba_iaa.iaa_iot  = X86_BUS_SPACE_IO;
		mba.mba_iaa.iaa_memt = X86_BUS_SPACE_MEM;
		if (ipmi_probe(&mba.mba_iaa))
			config_found(self, &mba.mba_iaa, mainbus_print);
	}
#endif

	if ((cpu_info_primary.ci_flags & CPUF_PRESENT) == 0) {
		struct cpu_attach_args caa;

		memset(&caa, 0, sizeof(caa));
		caa.caa_name = "cpu";
		caa.cpu_role = CPU_ROLE_SP;

		config_found(self, &caa, mainbus_print);
	}

	/* All CPUs are attached, handle MDS */
	replacemds();

#if NACPI > 0
	if (!acpi_hasprocfvs)
#endif
	{
		if (setperf_setup != NULL)
			setperf_setup(&cpu_info_primary);
	}

#ifdef MULTIPROCESSOR
	mp_setperf_init();
#endif

#if NPVBUS > 0
	/* Probe first to hide the "not configured" message */
	if (pvbus_probe()) {
		mba.mba_pvba.pvba_busname = "pvbus";
		config_found(self, &mba.mba_pvba.pvba_busname, mainbus_print);
	}
#endif

#if NPCI > 0
#if NACPI > 0
	if (acpi_haspci) {
		extern void acpipci_attach_busses(struct device *);

		acpipci_attach_busses(self);
	} else
#endif
	{
		pci_init_extents();

		bzero(&mba.mba_pba, sizeof(mba.mba_pba));
		mba.mba_pba.pba_busname = "pci";
		mba.mba_pba.pba_iot = X86_BUS_SPACE_IO;
		mba.mba_pba.pba_memt = X86_BUS_SPACE_MEM;
		mba.mba_pba.pba_dmat = &pci_bus_dma_tag;
		mba.mba_pba.pba_ioex = pciio_ex;
		mba.mba_pba.pba_memex = pcimem_ex;
		mba.mba_pba.pba_busex = pcibus_ex;
		mba.mba_pba.pba_domain = pci_ndomains++;
		mba.mba_pba.pba_bus = 0;
		config_found(self, &mba.mba_pba, mainbus_print);
	}
#endif

#if NISA > 0
	if (isa_has_been_seen == 0) {
		mba.mba_busname = "isa";
		mba.mba_iba.iba_iot = X86_BUS_SPACE_IO;
		mba.mba_iba.iba_memt = X86_BUS_SPACE_MEM;
#if NISADMA > 0
		mba.mba_iba.iba_dmat = &isa_bus_dma_tag;
#endif
		mba.mba_iba.iba_ic = NULL;

		config_found(self, &mba, mainbus_print);
	}
#endif

#if NVMM > 0
	if (vmm_enabled()) {
		mba.mba_busname = "vmm";
		config_found(self, &mba.mba_busname, mainbus_print);
	}
#endif /* NVMM > 0 */

#if NEFIFB > 0
	if (bios_efiinfo != NULL || efifb_cb_found()) {
		mba.mba_eaa.eaa_name = "efifb";
		config_found(self, &mba, mainbus_print);
	}
#endif
	codepatch_disable();
}

#if NEFIFB > 0
void
mainbus_efifb_reattach(void)
{
	union mainbus_attach_args mba;
	struct device *self = device_mainbus();

	if (bios_efiinfo != NULL || efifb_cb_found()) {
		mba.mba_eaa.eaa_name = "efifb";
		config_found(self, &mba, mainbus_print);
	}
}
#endif

int
mainbus_print(void *aux, const char *pnp)
{
	union mainbus_attach_args	*mba = aux;

	if (pnp)
		printf("%s at %s", mba->mba_busname, pnp);
	if (strcmp(mba->mba_busname, "pci") == 0)
		printf(" bus %d", mba->mba_pba.pba_bus);

	return (UNCONF);
}
