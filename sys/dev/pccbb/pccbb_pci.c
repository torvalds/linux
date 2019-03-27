/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002-2004 M. Warner Losh.
 * Copyright (c) 2000-2001 Jonathan Chen.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

/*-
 * Copyright (c) 1998, 1999 and 2000
 *      HAYAKAWA Koichi.  All rights reserved.
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
 *	This product includes software developed by HAYAKAWA Koichi.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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

/*
 * Driver for PCI to CardBus Bridge chips
 *
 * References:
 *  TI Datasheets:
 *   http://www-s.ti.com/cgi-bin/sc/generic2.cgi?family=PCI+CARDBUS+CONTROLLERS
 *
 * Written by Jonathan Chen <jon@freebsd.org>
 * The author would like to acknowledge:
 *  * HAYAKAWA Koichi: Author of the NetBSD code for the same thing
 *  * Warner Losh: Newbus/newcard guru and author of the pccard side of things
 *  * YAMAMOTO Shigeru: Author of another FreeBSD cardbus driver
 *  * David Cross: Author of the initial ugly hack for a specific cardbus card
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/condvar.h>
#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>
#include <sys/kthread.h>
#include <sys/bus.h>
#include <machine/bus.h>
#include <sys/rman.h>
#include <machine/resource.h>
#include <sys/module.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcib_private.h>

#include <dev/pccard/pccardreg.h>
#include <dev/pccard/pccardvar.h>

#include <dev/exca/excareg.h>
#include <dev/exca/excavar.h>

#include <dev/pccbb/pccbbreg.h>
#include <dev/pccbb/pccbbvar.h>

#include "power_if.h"
#include "card_if.h"
#include "pcib_if.h"

#define	DPRINTF(x) do { if (cbb_debug) printf x; } while (0)
#define	DEVPRINTF(x) do { if (cbb_debug) device_printf x; } while (0)

#define	PCI_MASK_CONFIG(DEV,REG,MASK,SIZE)				\
	pci_write_config(DEV, REG, pci_read_config(DEV, REG, SIZE) MASK, SIZE)
#define	PCI_MASK2_CONFIG(DEV,REG,MASK1,MASK2,SIZE)			\
	pci_write_config(DEV, REG, (					\
		pci_read_config(DEV, REG, SIZE) MASK1) MASK2, SIZE)

static void cbb_chipinit(struct cbb_softc *sc);
static int cbb_pci_filt(void *arg);

static struct yenta_chipinfo {
	uint32_t yc_id;
	const	char *yc_name;
	int	yc_chiptype;
} yc_chipsets[] = {
	/* Texas Instruments chips */
	{PCIC_ID_TI1031, "TI1031 PCI-PC Card Bridge", CB_TI113X},
	{PCIC_ID_TI1130, "TI1130 PCI-CardBus Bridge", CB_TI113X},
	{PCIC_ID_TI1131, "TI1131 PCI-CardBus Bridge", CB_TI113X},

	{PCIC_ID_TI1210, "TI1210 PCI-CardBus Bridge", CB_TI12XX},
	{PCIC_ID_TI1211, "TI1211 PCI-CardBus Bridge", CB_TI12XX},
	{PCIC_ID_TI1220, "TI1220 PCI-CardBus Bridge", CB_TI12XX},
	{PCIC_ID_TI1221, "TI1221 PCI-CardBus Bridge", CB_TI12XX},
	{PCIC_ID_TI1225, "TI1225 PCI-CardBus Bridge", CB_TI12XX},
	{PCIC_ID_TI1250, "TI1250 PCI-CardBus Bridge", CB_TI125X},
	{PCIC_ID_TI1251, "TI1251 PCI-CardBus Bridge", CB_TI125X},
	{PCIC_ID_TI1251B,"TI1251B PCI-CardBus Bridge",CB_TI125X},
	{PCIC_ID_TI1260, "TI1260 PCI-CardBus Bridge", CB_TI12XX},
	{PCIC_ID_TI1260B,"TI1260B PCI-CardBus Bridge",CB_TI12XX},
	{PCIC_ID_TI1410, "TI1410 PCI-CardBus Bridge", CB_TI12XX},
	{PCIC_ID_TI1420, "TI1420 PCI-CardBus Bridge", CB_TI12XX},
	{PCIC_ID_TI1421, "TI1421 PCI-CardBus Bridge", CB_TI12XX},
	{PCIC_ID_TI1450, "TI1450 PCI-CardBus Bridge", CB_TI125X}, /*SIC!*/
	{PCIC_ID_TI1451, "TI1451 PCI-CardBus Bridge", CB_TI12XX},
	{PCIC_ID_TI1510, "TI1510 PCI-CardBus Bridge", CB_TI12XX},
	{PCIC_ID_TI1520, "TI1520 PCI-CardBus Bridge", CB_TI12XX},
	{PCIC_ID_TI4410, "TI4410 PCI-CardBus Bridge", CB_TI12XX},
	{PCIC_ID_TI4450, "TI4450 PCI-CardBus Bridge", CB_TI12XX},
	{PCIC_ID_TI4451, "TI4451 PCI-CardBus Bridge", CB_TI12XX},
	{PCIC_ID_TI4510, "TI4510 PCI-CardBus Bridge", CB_TI12XX},
	{PCIC_ID_TI6411, "TI6411 PCI-CardBus Bridge", CB_TI12XX},
	{PCIC_ID_TI6420, "TI6420 PCI-CardBus Bridge", CB_TI12XX},
	{PCIC_ID_TI6420SC, "TI6420 PCI-CardBus Bridge", CB_TI12XX},
	{PCIC_ID_TI7410, "TI7410 PCI-CardBus Bridge", CB_TI12XX},
	{PCIC_ID_TI7510, "TI7510 PCI-CardBus Bridge", CB_TI12XX},
	{PCIC_ID_TI7610, "TI7610 PCI-CardBus Bridge", CB_TI12XX},
	{PCIC_ID_TI7610M, "TI7610 PCI-CardBus Bridge", CB_TI12XX},
	{PCIC_ID_TI7610SD, "TI7610 PCI-CardBus Bridge", CB_TI12XX},
	{PCIC_ID_TI7610MS, "TI7610 PCI-CardBus Bridge", CB_TI12XX},

	/* ENE */
	{PCIC_ID_ENE_CB710, "ENE CB710 PCI-CardBus Bridge", CB_TI12XX},
	{PCIC_ID_ENE_CB720, "ENE CB720 PCI-CardBus Bridge", CB_TI12XX},
	{PCIC_ID_ENE_CB1211, "ENE CB1211 PCI-CardBus Bridge", CB_TI12XX},
	{PCIC_ID_ENE_CB1225, "ENE CB1225 PCI-CardBus Bridge", CB_TI12XX},
	{PCIC_ID_ENE_CB1410, "ENE CB1410 PCI-CardBus Bridge", CB_TI12XX},
	{PCIC_ID_ENE_CB1420, "ENE CB1420 PCI-CardBus Bridge", CB_TI12XX},

	/* Ricoh chips */
	{PCIC_ID_RICOH_RL5C465, "RF5C465 PCI-CardBus Bridge", CB_RF5C46X},
	{PCIC_ID_RICOH_RL5C466, "RF5C466 PCI-CardBus Bridge", CB_RF5C46X},
	{PCIC_ID_RICOH_RL5C475, "RF5C475 PCI-CardBus Bridge", CB_RF5C47X},
	{PCIC_ID_RICOH_RL5C476, "RF5C476 PCI-CardBus Bridge", CB_RF5C47X},
	{PCIC_ID_RICOH_RL5C477, "RF5C477 PCI-CardBus Bridge", CB_RF5C47X},
	{PCIC_ID_RICOH_RL5C478, "RF5C478 PCI-CardBus Bridge", CB_RF5C47X},

	/* Toshiba products */
	{PCIC_ID_TOPIC95, "ToPIC95 PCI-CardBus Bridge", CB_TOPIC95},
	{PCIC_ID_TOPIC95B, "ToPIC95B PCI-CardBus Bridge", CB_TOPIC95},
	{PCIC_ID_TOPIC97, "ToPIC97 PCI-CardBus Bridge", CB_TOPIC97},
	{PCIC_ID_TOPIC100, "ToPIC100 PCI-CardBus Bridge", CB_TOPIC97},

	/* Cirrus Logic */
	{PCIC_ID_CLPD6832, "CLPD6832 PCI-CardBus Bridge", CB_CIRRUS},
	{PCIC_ID_CLPD6833, "CLPD6833 PCI-CardBus Bridge", CB_CIRRUS},
	{PCIC_ID_CLPD6834, "CLPD6834 PCI-CardBus Bridge", CB_CIRRUS},

	/* 02Micro */
	{PCIC_ID_OZ6832, "O2Micro OZ6832/6833 PCI-CardBus Bridge", CB_O2MICRO},
	{PCIC_ID_OZ6860, "O2Micro OZ6836/6860 PCI-CardBus Bridge", CB_O2MICRO},
	{PCIC_ID_OZ6872, "O2Micro OZ6812/6872 PCI-CardBus Bridge", CB_O2MICRO},
	{PCIC_ID_OZ6912, "O2Micro OZ6912/6972 PCI-CardBus Bridge", CB_O2MICRO},
	{PCIC_ID_OZ6922, "O2Micro OZ6922 PCI-CardBus Bridge", CB_O2MICRO},
	{PCIC_ID_OZ6933, "O2Micro OZ6933 PCI-CardBus Bridge", CB_O2MICRO},
	{PCIC_ID_OZ711E1, "O2Micro OZ711E1 PCI-CardBus Bridge", CB_O2MICRO},
	{PCIC_ID_OZ711EC1, "O2Micro OZ711EC1/M1 PCI-CardBus Bridge", CB_O2MICRO},
	{PCIC_ID_OZ711E2, "O2Micro OZ711E2 PCI-CardBus Bridge", CB_O2MICRO},
	{PCIC_ID_OZ711M1, "O2Micro OZ711M1 PCI-CardBus Bridge", CB_O2MICRO},
	{PCIC_ID_OZ711M2, "O2Micro OZ711M2 PCI-CardBus Bridge", CB_O2MICRO},
	{PCIC_ID_OZ711M3, "O2Micro OZ711M3 PCI-CardBus Bridge", CB_O2MICRO},

	/* SMC */
	{PCIC_ID_SMC_34C90, "SMC 34C90 PCI-CardBus Bridge", CB_CIRRUS},

	/* sentinel */
	{0 /* null id */, "unknown", CB_UNKNOWN},
};

/************************************************************************/
/* Probe/Attach								*/
/************************************************************************/

static int
cbb_chipset(uint32_t pci_id, const char **namep)
{
	struct yenta_chipinfo *ycp;

	for (ycp = yc_chipsets; ycp->yc_id != 0 && pci_id != ycp->yc_id; ++ycp)
		continue;
	if (namep != NULL)
		*namep = ycp->yc_name;
	return (ycp->yc_chiptype);
}

static int
cbb_pci_probe(device_t brdev)
{
	const char *name;
	uint32_t progif;
	uint32_t baseclass;
	uint32_t subclass;

	/*
	 * Do we know that we support the chipset?  If so, then we
	 * accept the device.
	 */
	if (cbb_chipset(pci_get_devid(brdev), &name) != CB_UNKNOWN) {
		device_set_desc(brdev, name);
		return (BUS_PROBE_DEFAULT);
	}

	/*
	 * We do support generic CardBus bridges.  All that we've seen
	 * to date have progif 0 (the Yenta spec, and successors mandate
	 * this).
	 */
	baseclass = pci_get_class(brdev);
	subclass = pci_get_subclass(brdev);
	progif = pci_get_progif(brdev);
	if (baseclass == PCIC_BRIDGE &&
	    subclass == PCIS_BRIDGE_CARDBUS && progif == 0) {
		device_set_desc(brdev, "PCI-CardBus Bridge");
		return (BUS_PROBE_GENERIC);
	}
	return (ENXIO);
}

/*
 * Print out the config space
 */
static void
cbb_print_config(device_t dev)
{
	int i;

	device_printf(dev, "PCI Configuration space:");
	for (i = 0; i < 256; i += 4) {
		if (i % 16 == 0)
			printf("\n  0x%02x: ", i);
		printf("0x%08x ", pci_read_config(dev, i, 4));
	}
	printf("\n");
}

static int
cbb_pci_attach(device_t brdev)
{
#if !(defined(NEW_PCIB) && defined(PCI_RES_BUS))
	static int curr_bus_number = 2; /* XXX EVILE BAD (see below) */
	uint32_t pribus;
#endif
	struct cbb_softc *sc = (struct cbb_softc *)device_get_softc(brdev);
	struct sysctl_ctx_list *sctx;
	struct sysctl_oid *soid;
	int rid;
	device_t parent;

	parent = device_get_parent(brdev);
	mtx_init(&sc->mtx, device_get_nameunit(brdev), "cbb", MTX_DEF);
	sc->chipset = cbb_chipset(pci_get_devid(brdev), NULL);
	sc->dev = brdev;
	sc->cbdev = NULL;
	sc->exca[0].pccarddev = NULL;
	sc->domain = pci_get_domain(brdev);
	sc->pribus = pcib_get_bus(parent);
#if defined(NEW_PCIB) && defined(PCI_RES_BUS)
	pci_write_config(brdev, PCIR_PRIBUS_2, sc->pribus, 1);
	pcib_setup_secbus(brdev, &sc->bus, 1);
#else
	sc->bus.sec = pci_read_config(brdev, PCIR_SECBUS_2, 1);
	sc->bus.sub = pci_read_config(brdev, PCIR_SUBBUS_2, 1);
#endif
	SLIST_INIT(&sc->rl);

	rid = CBBR_SOCKBASE;
	sc->base_res = bus_alloc_resource_any(brdev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (!sc->base_res) {
		device_printf(brdev, "Could not map register memory\n");
		mtx_destroy(&sc->mtx);
		return (ENOMEM);
	} else {
		DEVPRINTF((brdev, "Found memory at %jx\n",
		    rman_get_start(sc->base_res)));
	}

	sc->bst = rman_get_bustag(sc->base_res);
	sc->bsh = rman_get_bushandle(sc->base_res);
	exca_init(&sc->exca[0], brdev, sc->bst, sc->bsh, CBB_EXCA_OFFSET);
	sc->exca[0].flags |= EXCA_HAS_MEMREG_WIN;
	sc->exca[0].chipset = EXCA_CARDBUS;
	sc->chipinit = cbb_chipinit;
	sc->chipinit(sc);

	/*Sysctls*/
	sctx = device_get_sysctl_ctx(brdev);
	soid = device_get_sysctl_tree(brdev);
	SYSCTL_ADD_UINT(sctx, SYSCTL_CHILDREN(soid), OID_AUTO, "domain",
	    CTLFLAG_RD, &sc->domain, 0, "Domain number");
	SYSCTL_ADD_UINT(sctx, SYSCTL_CHILDREN(soid), OID_AUTO, "pribus",
	    CTLFLAG_RD, &sc->pribus, 0, "Primary bus number");
	SYSCTL_ADD_UINT(sctx, SYSCTL_CHILDREN(soid), OID_AUTO, "secbus",
	    CTLFLAG_RD, &sc->bus.sec, 0, "Secondary bus number");
	SYSCTL_ADD_UINT(sctx, SYSCTL_CHILDREN(soid), OID_AUTO, "subbus",
	    CTLFLAG_RD, &sc->bus.sub, 0, "Subordinate bus number");
#if 0
	SYSCTL_ADD_UINT(sctx, SYSCTL_CHILDREN(soid), OID_AUTO, "memory",
	    CTLFLAG_RD, &sc->subbus, 0, "Memory window open");
	SYSCTL_ADD_UINT(sctx, SYSCTL_CHILDREN(soid), OID_AUTO, "premem",
	    CTLFLAG_RD, &sc->subbus, 0, "Prefetch memory window open");
	SYSCTL_ADD_UINT(sctx, SYSCTL_CHILDREN(soid), OID_AUTO, "io1",
	    CTLFLAG_RD, &sc->subbus, 0, "io range 1 open");
	SYSCTL_ADD_UINT(sctx, SYSCTL_CHILDREN(soid), OID_AUTO, "io2",
	    CTLFLAG_RD, &sc->subbus, 0, "io range 2 open");
#endif

#if !(defined(NEW_PCIB) && defined(PCI_RES_BUS))
	/*
	 * This is a gross hack.  We should be scanning the entire pci
	 * tree, assigning bus numbers in a way such that we (1) can
	 * reserve 1 extra bus just in case and (2) all sub buses
	 * are in an appropriate range.
	 */
	DEVPRINTF((brdev, "Secondary bus is %d\n", sc->bus.sec));
	pribus = pci_read_config(brdev, PCIR_PRIBUS_2, 1);
	if (sc->bus.sec == 0 || sc->pribus != pribus) {
		if (curr_bus_number <= sc->pribus)
			curr_bus_number = sc->pribus + 1;
		if (pribus != sc->pribus) {
			DEVPRINTF((brdev, "Setting primary bus to %d\n",
			    sc->pribus));
			pci_write_config(brdev, PCIR_PRIBUS_2, sc->pribus, 1);
		}
		sc->bus.sec = curr_bus_number++;
		sc->bus.sub = curr_bus_number++;
		DEVPRINTF((brdev, "Secondary bus set to %d subbus %d\n",
		    sc->bus.sec, sc->bus.sub));
		pci_write_config(brdev, PCIR_SECBUS_2, sc->bus.sec, 1);
		pci_write_config(brdev, PCIR_SUBBUS_2, sc->bus.sub, 1);
	}
#endif

	/* attach children */
	sc->cbdev = device_add_child(brdev, "cardbus", -1);
	if (sc->cbdev == NULL)
		DEVPRINTF((brdev, "WARNING: cannot add cardbus bus.\n"));
	else if (device_probe_and_attach(sc->cbdev) != 0)
		DEVPRINTF((brdev, "WARNING: cannot attach cardbus bus!\n"));

	sc->exca[0].pccarddev = device_add_child(brdev, "pccard", -1);
	if (sc->exca[0].pccarddev == NULL)
		DEVPRINTF((brdev, "WARNING: cannot add pccard bus.\n"));
	else if (device_probe_and_attach(sc->exca[0].pccarddev) != 0)
		DEVPRINTF((brdev, "WARNING: cannot attach pccard bus.\n"));

	/* Map and establish the interrupt. */
	rid = 0;
	sc->irq_res = bus_alloc_resource_any(brdev, SYS_RES_IRQ, &rid,
	    RF_SHAREABLE | RF_ACTIVE);
	if (sc->irq_res == NULL) {
		device_printf(brdev, "Unable to map IRQ...\n");
		goto err;
	}

	if (bus_setup_intr(brdev, sc->irq_res, INTR_TYPE_AV | INTR_MPSAFE,
	    cbb_pci_filt, NULL, sc, &sc->intrhand)) {
		device_printf(brdev, "couldn't establish interrupt\n");
		goto err;
	}

	/* reset 16-bit pcmcia bus */
	exca_clrb(&sc->exca[0], EXCA_INTR, EXCA_INTR_RESET);

	/* turn off power */
	cbb_power(brdev, CARD_OFF);

	/* CSC Interrupt: Card detect interrupt on */
	cbb_setb(sc, CBB_SOCKET_MASK, CBB_SOCKET_MASK_CD);

	/* reset interrupt */
	cbb_set(sc, CBB_SOCKET_EVENT, cbb_get(sc, CBB_SOCKET_EVENT));

	if (bootverbose)
		cbb_print_config(brdev);

	/* Start the thread */
	if (kproc_create(cbb_event_thread, sc, &sc->event_thread, 0, 0,
	    "%s event thread", device_get_nameunit(brdev))) {
		device_printf(brdev, "unable to create event thread.\n");
		panic("cbb_create_event_thread");
	}
	sc->sc_root_token = root_mount_hold(device_get_nameunit(sc->dev));
	return (0);
err:
	if (sc->irq_res)
		bus_release_resource(brdev, SYS_RES_IRQ, 0, sc->irq_res);
	if (sc->base_res) {
		bus_release_resource(brdev, SYS_RES_MEMORY, CBBR_SOCKBASE,
		    sc->base_res);
	}
	mtx_destroy(&sc->mtx);
	return (ENOMEM);
}

static int
cbb_pci_detach(device_t brdev)
{
#if defined(NEW_PCIB) && defined(PCI_RES_BUS)
	struct cbb_softc *sc = device_get_softc(brdev);
#endif
	int error;

	error = cbb_detach(brdev);
#if defined(NEW_PCIB) && defined(PCI_RES_BUS)
	if (error == 0)
		pcib_free_secbus(brdev, &sc->bus);
#endif
	return (error);
}

static void
cbb_chipinit(struct cbb_softc *sc)
{
	uint32_t mux, sysctrl, reg;

	/* Set CardBus latency timer */
	if (pci_read_config(sc->dev, PCIR_SECLAT_2, 1) < 0x20)
		pci_write_config(sc->dev, PCIR_SECLAT_2, 0x20, 1);

	/* Set PCI latency timer */
	if (pci_read_config(sc->dev, PCIR_LATTIMER, 1) < 0x20)
		pci_write_config(sc->dev, PCIR_LATTIMER, 0x20, 1);

	/* Enable DMA, memory access for this card and I/O access for children */
	pci_enable_busmaster(sc->dev);
	pci_enable_io(sc->dev, SYS_RES_IOPORT);
	pci_enable_io(sc->dev, SYS_RES_MEMORY);

	/* disable Legacy IO */
	switch (sc->chipset) {
	case CB_RF5C46X:
		PCI_MASK_CONFIG(sc->dev, CBBR_BRIDGECTRL,
		    & ~(CBBM_BRIDGECTRL_RL_3E0_EN |
		    CBBM_BRIDGECTRL_RL_3E2_EN), 2);
		break;
	default:
		pci_write_config(sc->dev, CBBR_LEGACY, 0x0, 4);
		break;
	}

	/* Use PCI interrupt for interrupt routing */
	PCI_MASK2_CONFIG(sc->dev, CBBR_BRIDGECTRL,
	    & ~(CBBM_BRIDGECTRL_MASTER_ABORT |
	    CBBM_BRIDGECTRL_INTR_IREQ_ISA_EN),
	    | CBBM_BRIDGECTRL_WRITE_POST_EN,
	    2);

	/*
	 * XXX this should be a function table, ala OLDCARD.  This means
	 * that we could more easily support ISA interrupts for pccard
	 * cards if we had to.
	 */
	switch (sc->chipset) {
	case CB_TI113X:
		/*
		 * The TI 1031, TI 1130 and TI 1131 all require another bit
		 * be set to enable PCI routing of interrupts, and then
		 * a bit for each of the CSC and Function interrupts we
		 * want routed.
		 */
		PCI_MASK_CONFIG(sc->dev, CBBR_CBCTRL,
		    | CBBM_CBCTRL_113X_PCI_INTR |
		    CBBM_CBCTRL_113X_PCI_CSC | CBBM_CBCTRL_113X_PCI_IRQ_EN,
		    1);
		PCI_MASK_CONFIG(sc->dev, CBBR_DEVCTRL,
		    & ~(CBBM_DEVCTRL_INT_SERIAL |
		    CBBM_DEVCTRL_INT_PCI), 1);
		break;
	case CB_TI12XX:
		/*
		 * Some TI 12xx (and [14][45]xx) based pci cards
		 * sometimes have issues with the MFUNC register not
		 * being initialized due to a bad EEPROM on board.
		 * Laptops that this matters on have this register
		 * properly initialized.
		 *
		 * The TI125X parts have a different register.
		 *
		 * Note: Only the lower two nibbles matter. When set
		 * to 0, the MFUNC{0,1} pins are GPIO, which isn't
		 * going to work out too well because we specifically
		 * program these parts to parallel interrupt signalling
		 * elsewhere. We preserve the upper bits of this
		 * register since changing them have subtle side effects
		 * for different variants of the card and are
		 * extremely difficult to exaustively test.
		 *
		 * Also, the TI 1510/1520 changed the default for the MFUNC
		 * register from 0x0 to 0x1000 to enable IRQSER by default.
		 * We want to be careful to avoid overriding that, and the
		 * below test will do that. Should this check prove to be
		 * too permissive, we should just check against 0 and 0x1000
		 * and not touch it otherwise.
		 */
		mux = pci_read_config(sc->dev, CBBR_MFUNC, 4);
		sysctrl = pci_read_config(sc->dev, CBBR_SYSCTRL, 4);
		if ((mux & (CBBM_MFUNC_PIN0 | CBBM_MFUNC_PIN1)) == 0) {
			mux = (mux & ~CBBM_MFUNC_PIN0) |
			    CBBM_MFUNC_PIN0_INTA;
			if ((sysctrl & CBBM_SYSCTRL_INTRTIE) == 0)
				mux = (mux & ~CBBM_MFUNC_PIN1) |
				    CBBM_MFUNC_PIN1_INTB;
			pci_write_config(sc->dev, CBBR_MFUNC, mux, 4);
		}
		/*FALLTHROUGH*/
	case CB_TI125X:
		/*
		 * Disable zoom video.  Some machines initialize this
		 * improperly and exerpience has shown that this helps
		 * prevent strange behavior. We don't support zoom
		 * video anyway, so no harm can come from this.
		 */
		pci_write_config(sc->dev, CBBR_MMCTRL, 0, 4);
		break;
	case CB_O2MICRO:
		/*
		 * Issue #1: INT# generated at the same time as
		 * selected ISA IRQ.  When IREQ# or STSCHG# is active,
		 * in addition to the ISA IRQ being generated, INT#
		 * will also be generated at the same time.
		 *
		 * Some of the older controllers have an issue in
		 * which the slot's PCI INT# will be asserted whenever
		 * IREQ# or STSCGH# is asserted even if ExCA registers
		 * 03h or 05h have an ISA IRQ selected.
		 *
		 * The fix for this issue, which will work for any
		 * controller (old or new), is to set ExCA registers
		 * 3Ah (slot 0) & 7Ah (slot 1) bits 7:4 = 1010b.
		 * These bits are undocumented.  By setting this
		 * register (of each slot) to '1010xxxxb' a routing of
		 * IREQ# to INTC# and STSCHG# to INTC# is selected.
		 * Since INTC# isn't connected there will be no
		 * unexpected PCI INT when IREQ# or STSCHG# is active.
		 * However, INTA# (slot 0) or INTB# (slot 1) will
		 * still be correctly generated if NO ISA IRQ is
		 * selected (ExCA regs 03h or 05h are cleared).
		 */
		reg = exca_getb(&sc->exca[0], EXCA_O2MICRO_CTRL_C);
		reg = (reg & 0x0f) |
		    EXCA_O2CC_IREQ_INTC | EXCA_O2CC_STSCHG_INTC;
		exca_putb(&sc->exca[0], EXCA_O2MICRO_CTRL_C, reg);
		break;
	case CB_TOPIC97:
		/*
		 * Disable Zoom Video, ToPIC 97, 100.
		 */
		pci_write_config(sc->dev, TOPIC97_ZV_CONTROL, 0, 1);
		/*
		 * ToPIC 97, 100
		 * At offset 0xa1: INTERRUPT CONTROL register
		 * 0x1: Turn on INT interrupts.
		 */
		PCI_MASK_CONFIG(sc->dev, TOPIC_INTCTRL,
		    | TOPIC97_INTCTRL_INTIRQSEL, 1);
		/*
		 * ToPIC97, 100
		 * Need to assert support for low voltage cards
		 */
		exca_setb(&sc->exca[0], EXCA_TOPIC97_CTRL,
		    EXCA_TOPIC97_CTRL_LV_MASK);
		goto topic_common;
	case CB_TOPIC95:
		/*
		 * SOCKETCTRL appears to be TOPIC 95/B specific
		 */
		PCI_MASK_CONFIG(sc->dev, TOPIC95_SOCKETCTRL,
		    | TOPIC95_SOCKETCTRL_SCR_IRQSEL, 4);

	topic_common:;
		/*
		 * At offset 0xa0: SLOT CONTROL
		 * 0x80 Enable CardBus Functionality
		 * 0x40 Enable CardBus and PC Card registers
		 * 0x20 Lock ID in exca regs
		 * 0x10 Write protect ID in config regs
		 * Clear the rest of the bits, which defaults the slot
		 * in legacy mode to 0x3e0 and offset 0. (legacy
		 * mode is determined elsewhere)
		 */
		pci_write_config(sc->dev, TOPIC_SLOTCTRL,
		    TOPIC_SLOTCTRL_SLOTON |
		    TOPIC_SLOTCTRL_SLOTEN |
		    TOPIC_SLOTCTRL_ID_LOCK |
		    TOPIC_SLOTCTRL_ID_WP, 1);

		/*
		 * At offset 0xa3 Card Detect Control Register
		 * 0x80 CARDBUS enbale
		 * 0x01 Cleared for hardware change detect
		 */
		PCI_MASK2_CONFIG(sc->dev, TOPIC_CDC,
		    | TOPIC_CDC_CARDBUS, & ~TOPIC_CDC_SWDETECT, 4);
		break;
	}

	/*
	 * Need to tell ExCA registers to CSC interrupts route via PCI
	 * interrupts.  There are two ways to do this.  One is to set
	 * INTR_ENABLE and the other is to set CSC to 0.  Since both
	 * methods are mutually compatible, we do both.
	 */
	exca_putb(&sc->exca[0], EXCA_INTR, EXCA_INTR_ENABLE);
	exca_putb(&sc->exca[0], EXCA_CSC_INTR, 0);

	cbb_disable_func_intr(sc);

	/* close all memory and io windows */
	pci_write_config(sc->dev, CBBR_MEMBASE0, 0xffffffff, 4);
	pci_write_config(sc->dev, CBBR_MEMLIMIT0, 0, 4);
	pci_write_config(sc->dev, CBBR_MEMBASE1, 0xffffffff, 4);
	pci_write_config(sc->dev, CBBR_MEMLIMIT1, 0, 4);
	pci_write_config(sc->dev, CBBR_IOBASE0, 0xffffffff, 4);
	pci_write_config(sc->dev, CBBR_IOLIMIT0, 0, 4);
	pci_write_config(sc->dev, CBBR_IOBASE1, 0xffffffff, 4);
	pci_write_config(sc->dev, CBBR_IOLIMIT1, 0, 4);
}

static int
cbb_route_interrupt(device_t pcib, device_t dev, int pin)
{
	struct cbb_softc *sc = (struct cbb_softc *)device_get_softc(pcib);

	return (rman_get_start(sc->irq_res));
}

static int
cbb_pci_shutdown(device_t brdev)
{
	struct cbb_softc *sc = (struct cbb_softc *)device_get_softc(brdev);

	/*
	 * We're about to pull the rug out from the card, so mark it as
	 * gone to prevent harm.
         */
        sc->cardok = 0;

	/*
	 * Place the cards in reset, turn off the interrupts and power
	 * down the socket.
	 */
	PCI_MASK_CONFIG(brdev, CBBR_BRIDGECTRL, |CBBM_BRIDGECTRL_RESET, 2);
	exca_clrb(&sc->exca[0], EXCA_INTR, EXCA_INTR_RESET);
	cbb_set(sc, CBB_SOCKET_MASK, 0);
	cbb_set(sc, CBB_SOCKET_EVENT, 0xffffffff);
	cbb_power(brdev, CARD_OFF);

	/* 
	 * For paranoia, turn off all address decoding.  Really not needed,
	 * it seems, but it can't hurt
	 */
	exca_putb(&sc->exca[0], EXCA_ADDRWIN_ENABLE, 0);
	pci_write_config(brdev, CBBR_MEMBASE0, 0, 4);
	pci_write_config(brdev, CBBR_MEMLIMIT0, 0, 4);
	pci_write_config(brdev, CBBR_MEMBASE1, 0, 4);
	pci_write_config(brdev, CBBR_MEMLIMIT1, 0, 4);
	pci_write_config(brdev, CBBR_IOBASE0, 0, 4);
	pci_write_config(brdev, CBBR_IOLIMIT0, 0, 4);
	pci_write_config(brdev, CBBR_IOBASE1, 0, 4);
	pci_write_config(brdev, CBBR_IOLIMIT1, 0, 4);
	return (0);
}

static int
cbb_pci_filt(void *arg)
{
	struct cbb_softc *sc = arg;
	uint32_t sockevent;
	uint8_t csc;
	int retval = FILTER_STRAY;

	/*
	 * Some chips also require us to read the old ExCA registe for card
	 * status change when we route CSC vis PCI.  This isn't supposed to be
	 * required, but it clears the interrupt state on some chipsets.
	 * Maybe there's a setting that would obviate its need.  Maybe we
	 * should test the status bits and deal with them, but so far we've
	 * not found any machines that don't also give us the socket status
	 * indication above.
	 *
	 * This call used to be unconditional.  However, further research
	 * suggests that we hit this condition when the card READY interrupt
	 * fired.  So now we only read it for 16-bit cards, and we only claim
	 * the interrupt if READY is set.  If this still causes problems, then
	 * the next step would be to read this if we have a 16-bit card *OR*
	 * we have no card.  We treat the READY signal as if it were the power
	 * completion signal.  Some bridges may double signal things here, bit
	 * signalling twice should be OK since we only sleep on the powerintr
	 * in one place and a double wakeup would be benign there.
	 */
	if (sc->flags & CBB_16BIT_CARD) {
		csc = exca_getb(&sc->exca[0], EXCA_CSC);
		if (csc & EXCA_CSC_READY) {
			atomic_add_int(&sc->powerintr, 1);
			wakeup((void *)&sc->powerintr);
			retval = FILTER_HANDLED;
		}
	}

	/*
	 * Read the socket event.  Sometimes, the theory goes, the PCI bus is
	 * so loaded that it cannot satisfy the read request, so we get
	 * garbage back from the following read.  We have to filter out the
	 * garbage so that we don't spontaneously reset the card under high
	 * load.  PCI isn't supposed to act like this.  No doubt this is a bug
	 * in the PCI bridge chipset (or cbb brige) that's being used in
	 * certain amd64 laptops today.  Work around the issue by assuming
	 * that any bits we don't know about being set means that we got
	 * garbage.
	 */
	sockevent = cbb_get(sc, CBB_SOCKET_EVENT);
	if (sockevent != 0 && (sockevent & ~CBB_SOCKET_EVENT_VALID_MASK) == 0) {
		/*
		 * If anything has happened to the socket, we assume that the
		 * card is no longer OK, and we shouldn't call its ISR.  We
		 * set cardok as soon as we've attached the card.  This helps
		 * in a noisy eject, which happens all too often when users
		 * are ejecting their PC Cards.
		 *
		 * We use this method in preference to checking to see if the
		 * card is still there because the check suffers from a race
		 * condition in the bouncing case.
		 */
#define DELTA (CBB_SOCKET_MASK_CD)
		if (sockevent & DELTA) {
			cbb_clrb(sc, CBB_SOCKET_MASK, DELTA);
			cbb_set(sc, CBB_SOCKET_EVENT, DELTA);
			sc->cardok = 0;
			cbb_disable_func_intr(sc);
			wakeup(&sc->intrhand);
		}
#undef DELTA

		/*
		 * Wakeup anybody waiting for a power interrupt.  We have to
		 * use atomic_add_int for wakups on other cores.
		 */
		if (sockevent & CBB_SOCKET_EVENT_POWER) {
			cbb_clrb(sc, CBB_SOCKET_MASK, CBB_SOCKET_EVENT_POWER);
			cbb_set(sc, CBB_SOCKET_EVENT, CBB_SOCKET_EVENT_POWER);
			atomic_add_int(&sc->powerintr, 1);
			wakeup((void *)&sc->powerintr);
		}

		/*
		 * Status change interrupts aren't presently used in the
		 * rest of the driver.  For now, just ACK them.
		 */
		if (sockevent & CBB_SOCKET_EVENT_CSTS)
			cbb_set(sc, CBB_SOCKET_EVENT, CBB_SOCKET_EVENT_CSTS);
		retval = FILTER_HANDLED;
	}
	return retval;
}

#if defined(NEW_PCIB) && defined(PCI_RES_BUS)
static struct resource *
cbb_pci_alloc_resource(device_t bus, device_t child, int type, int *rid,
    rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{
	struct cbb_softc *sc;

	sc = device_get_softc(bus);
	if (type == PCI_RES_BUS)
		return (pcib_alloc_subbus(&sc->bus, child, rid, start, end,
		    count, flags));
	return (cbb_alloc_resource(bus, child, type, rid, start, end, count,
	    flags));
}

static int
cbb_pci_adjust_resource(device_t bus, device_t child, int type,
    struct resource *r, rman_res_t start, rman_res_t end)
{
	struct cbb_softc *sc;

	sc = device_get_softc(bus);
	if (type == PCI_RES_BUS) {
		if (!rman_is_region_manager(r, &sc->bus.rman))
			return (EINVAL);
		return (rman_adjust_resource(r, start, end));
	}
	return (bus_generic_adjust_resource(bus, child, type, r, start, end));
}

static int
cbb_pci_release_resource(device_t bus, device_t child, int type, int rid,
    struct resource *r)
{
	struct cbb_softc *sc;
	int error;

	sc = device_get_softc(bus);
	if (type == PCI_RES_BUS) {
		if (!rman_is_region_manager(r, &sc->bus.rman))
			return (EINVAL);
		if (rman_get_flags(r) & RF_ACTIVE) {
			error = bus_deactivate_resource(child, type, rid, r);
			if (error)
				return (error);
		}
		return (rman_release_resource(r));
	}
	return (cbb_release_resource(bus, child, type, rid, r));
}
#endif

/************************************************************************/
/* PCI compat methods							*/
/************************************************************************/

static int
cbb_maxslots(device_t brdev)
{
	return (0);
}

static uint32_t
cbb_read_config(device_t brdev, u_int b, u_int s, u_int f, u_int reg, int width)
{
	/*
	 * Pass through to the next ppb up the chain (i.e. our grandparent).
	 */
	return (PCIB_READ_CONFIG(device_get_parent(device_get_parent(brdev)),
	    b, s, f, reg, width));
}

static void
cbb_write_config(device_t brdev, u_int b, u_int s, u_int f, u_int reg, uint32_t val,
    int width)
{
	/*
	 * Pass through to the next ppb up the chain (i.e. our grandparent).
	 */
	PCIB_WRITE_CONFIG(device_get_parent(device_get_parent(brdev)),
	    b, s, f, reg, val, width);
}

static int
cbb_pci_suspend(device_t brdev)
{
	int			error = 0;
	struct cbb_softc	*sc = device_get_softc(brdev);

	error = bus_generic_suspend(brdev);
	if (error != 0)
		return (error);
	cbb_set(sc, CBB_SOCKET_MASK, 0);	/* Quiet hardware */
	sc->cardok = 0;				/* Card is bogus now */
	return (0);
}

static int
cbb_pci_resume(device_t brdev)
{
	int	error = 0;
	struct cbb_softc *sc = (struct cbb_softc *)device_get_softc(brdev);
	uint32_t tmp;

	/*
	 * In the APM and early ACPI era, BIOSes saved the PCI config
	 * registers. As chips became more complicated, that functionality moved
	 * into the ACPI code / tables. We must therefore, restore the settings
	 * we made here to make sure the device come back. Transitions to Dx
	 * from D0 and back to D0 cause the bridge to lose its config space, so
	 * all the bus mappings and such are preserved.
	 *
	 * The PCI layer handles standard PCI registers like the
	 * command register and BARs, but cbb-specific registers are
	 * handled here.
	 */
	sc->chipinit(sc);

	/* reset interrupt -- Do we really need to do this? */
	tmp = cbb_get(sc, CBB_SOCKET_EVENT);
	cbb_set(sc, CBB_SOCKET_EVENT, tmp);

	/* CSC Interrupt: Card detect interrupt on */
	cbb_setb(sc, CBB_SOCKET_MASK, CBB_SOCKET_MASK_CD);

	/* Signal the thread to wakeup. */
	wakeup(&sc->intrhand);

	error = bus_generic_resume(brdev);

	return (error);
}

static device_method_t cbb_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,			cbb_pci_probe),
	DEVMETHOD(device_attach,		cbb_pci_attach),
	DEVMETHOD(device_detach,		cbb_pci_detach),
	DEVMETHOD(device_shutdown,		cbb_pci_shutdown),
	DEVMETHOD(device_suspend,		cbb_pci_suspend),
	DEVMETHOD(device_resume,		cbb_pci_resume),

	/* bus methods */
	DEVMETHOD(bus_read_ivar,		cbb_read_ivar),
	DEVMETHOD(bus_write_ivar,		cbb_write_ivar),
#if defined(NEW_PCIB) && defined(PCI_RES_BUS)
	DEVMETHOD(bus_alloc_resource,		cbb_pci_alloc_resource),
	DEVMETHOD(bus_adjust_resource,		cbb_pci_adjust_resource),
	DEVMETHOD(bus_release_resource,		cbb_pci_release_resource),
#else
	DEVMETHOD(bus_alloc_resource,		cbb_alloc_resource),
	DEVMETHOD(bus_release_resource,		cbb_release_resource),
#endif
	DEVMETHOD(bus_activate_resource,	cbb_activate_resource),
	DEVMETHOD(bus_deactivate_resource,	cbb_deactivate_resource),
	DEVMETHOD(bus_driver_added,		cbb_driver_added),
	DEVMETHOD(bus_child_detached,		cbb_child_detached),
	DEVMETHOD(bus_setup_intr,		cbb_setup_intr),
	DEVMETHOD(bus_teardown_intr,		cbb_teardown_intr),
	DEVMETHOD(bus_child_present,		cbb_child_present),

	/* 16-bit card interface */
	DEVMETHOD(card_set_res_flags,		cbb_pcic_set_res_flags),
	DEVMETHOD(card_set_memory_offset,	cbb_pcic_set_memory_offset),

	/* power interface */
	DEVMETHOD(power_enable_socket,		cbb_power_enable_socket),
	DEVMETHOD(power_disable_socket,		cbb_power_disable_socket),

	/* pcib compatibility interface */
	DEVMETHOD(pcib_maxslots,		cbb_maxslots),
	DEVMETHOD(pcib_read_config,		cbb_read_config),
	DEVMETHOD(pcib_write_config,		cbb_write_config),
	DEVMETHOD(pcib_route_interrupt,		cbb_route_interrupt),

	DEVMETHOD_END
};

static driver_t cbb_driver = {
	"cbb",
	cbb_methods,
	sizeof(struct cbb_softc)
};

DRIVER_MODULE(cbb, pci, cbb_driver, cbb_devclass, 0, 0);
MODULE_PNP_INFO("W32:vendor/device;D:#", pci, cbb, yc_chipsets,
    nitems(yc_chipsets) - 1);
MODULE_DEPEND(cbb, exca, 1, 1, 1);
