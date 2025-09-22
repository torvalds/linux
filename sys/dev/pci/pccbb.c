/*	$OpenBSD: pccbb.c,v 1.105 2024/05/24 06:02:58 jsg Exp $	*/
/*	$NetBSD: pccbb.c,v 1.96 2004/03/28 09:49:31 nakayama Exp $	*/

/*
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
#define CBB_DEBUG
#define SHOW_REGS
#define PCCBB_PCMCIA_POLL
*/

/*
#define CB_PCMCIA_POLL
#define CB_PCMCIA_POLL_ONLY
#define LEVEL2
*/

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/evcount.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/task.h>
#include <sys/time.h>
#include <sys/timeout.h>

#include <machine/intr.h>
#include <machine/bus.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcidevs.h>

#include <dev/pci/pccbbreg.h>

#include <dev/cardbus/cardslotvar.h>

#include <dev/cardbus/cardbusvar.h>

#include <dev/pcmcia/pcmciareg.h>
#include <dev/pcmcia/pcmciavar.h>

#include <dev/ic/i82365reg.h>
#include <dev/ic/i82365var.h>
#include <dev/pci/pccbbvar.h>

struct cfdriver cbb_cd = {
	NULL, "cbb", DV_DULL
};

#if defined CBB_DEBUG
#define DPRINTF(x) printf x
#else
#define DPRINTF(x)
#endif

int	pcicbbmatch(struct device *, void *, void *);
void	pccbbattach(struct device *, struct device *, void *);
int	pccbbactivate(struct device *, int);
int	pccbbintr(void *);
void	pccbb_shutdown(void *);
void	pci113x_insert(void *);
int	pccbbintr_function(struct pccbb_softc *);

int	pccbb_checksockstat(struct pccbb_softc *);
int	pccbb_detect_card(struct pccbb_softc *);

void	pccbb_pcmcia_write(struct pcic_handle *, int, int);
u_int8_t pccbb_pcmcia_read(struct pcic_handle *, int);
#define Pcic_read(ph, reg) ((ph)->ph_read((ph), (reg)))
#define Pcic_write(ph, reg, val) ((ph)->ph_write((ph), (reg), (val)))

int	cb_reset(struct pccbb_softc *);
int	cb_detect_voltage(struct pccbb_softc *);
int	cbbprint(void *, const char *);

int	cb_chipset(u_int32_t, int *);
void	pccbb_pcmcia_attach_setup(struct pccbb_softc *,
    struct pcmciabus_attach_args *);
#if 0
void	pccbb_pcmcia_attach_card(struct pcic_handle *);
void	pccbb_pcmcia_detach_card(struct pcic_handle *, int);
#endif

int	pccbb_ctrl(cardbus_chipset_tag_t, int);
int	pccbb_power(cardbus_chipset_tag_t, int);
int	pccbb_cardenable(struct pccbb_softc * sc, int function);
void   *pccbb_intr_establish(struct pccbb_softc *, int irq, int level,
    int (*ih) (void *), void *sc, const char *);
void	pccbb_intr_disestablish(struct pccbb_softc *, void *ih);

void   *pccbb_cb_intr_establish(cardbus_chipset_tag_t, int irq, int level,
    int (*ih) (void *), void *sc, const char *);
void	pccbb_cb_intr_disestablish(cardbus_chipset_tag_t ct, void *ih);

void	pccbb_legacy_disable(struct pccbb_softc *sc);
void	pccbb_chipinit(struct pccbb_softc *);

int	pccbb_pcmcia_mem_alloc(pcmcia_chipset_handle_t, bus_size_t,
    struct pcmcia_mem_handle *);
void	pccbb_pcmcia_mem_free(pcmcia_chipset_handle_t,
    struct pcmcia_mem_handle *);
int	pccbb_pcmcia_mem_map(pcmcia_chipset_handle_t, int, bus_addr_t,
    bus_size_t, struct pcmcia_mem_handle *, bus_size_t *, int *);
void	pccbb_pcmcia_mem_unmap(pcmcia_chipset_handle_t, int);
int	pccbb_pcmcia_io_alloc(pcmcia_chipset_handle_t, bus_addr_t,
    bus_size_t, bus_size_t, struct pcmcia_io_handle *);
void	pccbb_pcmcia_io_free(pcmcia_chipset_handle_t,
    struct pcmcia_io_handle *);
int	pccbb_pcmcia_io_map(pcmcia_chipset_handle_t, int, bus_addr_t,
    bus_size_t, struct pcmcia_io_handle *, int *);
void	pccbb_pcmcia_io_unmap(pcmcia_chipset_handle_t, int);
void   *pccbb_pcmcia_intr_establish(pcmcia_chipset_handle_t,
    struct pcmcia_function *, int, int (*)(void *), void *, char *);
void	pccbb_pcmcia_intr_disestablish(pcmcia_chipset_handle_t, void *);
const char *pccbb_pcmcia_intr_string(pcmcia_chipset_handle_t, void *);
void	pccbb_pcmcia_socket_enable(pcmcia_chipset_handle_t);
void	pccbb_pcmcia_socket_disable(pcmcia_chipset_handle_t);
int	pccbb_pcmcia_card_detect(pcmcia_chipset_handle_t pch);

void	pccbb_pcmcia_do_io_map(struct pcic_handle *, int);
void	pccbb_pcmcia_wait_ready(struct pcic_handle *);
void	pccbb_pcmcia_do_mem_map(struct pcic_handle *, int);

/* bus-space allocation and deallocation functions */
int	pccbb_rbus_cb_space_alloc(cardbus_chipset_tag_t, rbus_tag_t,
    bus_addr_t addr, bus_size_t size, bus_addr_t mask, bus_size_t align,
    int flags, bus_addr_t * addrp, bus_space_handle_t * bshp);
int	pccbb_rbus_cb_space_free(cardbus_chipset_tag_t, rbus_tag_t,
    bus_space_handle_t, bus_size_t);

int	pccbb_open_win(struct pccbb_softc *, bus_space_tag_t,
    bus_addr_t, bus_size_t, bus_space_handle_t, int flags);
int	pccbb_close_win(struct pccbb_softc *, bus_space_tag_t,
    bus_space_handle_t, bus_size_t);
int	pccbb_winlist_insert(struct pccbb_win_chain_head *, bus_addr_t,
    bus_size_t, bus_space_handle_t, int);
int	pccbb_winlist_delete(struct pccbb_win_chain_head *,
    bus_space_handle_t, bus_size_t);
void	pccbb_winset(bus_addr_t align, struct pccbb_softc *,
    bus_space_tag_t);

/* for config_defer */
void	pccbb_pci_callback(struct device *);

#if defined SHOW_REGS
void	cb_show_regs(pci_chipset_tag_t, pcitag_t, bus_space_tag_t,
    bus_space_handle_t memh);
#endif

const struct cfattach cbb_pci_ca = {
	sizeof(struct pccbb_softc), pcicbbmatch, pccbbattach, NULL,
	pccbbactivate
};

static struct pcmcia_chip_functions pccbb_pcmcia_funcs = {
	pccbb_pcmcia_mem_alloc,
	pccbb_pcmcia_mem_free,
	pccbb_pcmcia_mem_map,
	pccbb_pcmcia_mem_unmap,
	pccbb_pcmcia_io_alloc,
	pccbb_pcmcia_io_free,
	pccbb_pcmcia_io_map,
	pccbb_pcmcia_io_unmap,
	pccbb_pcmcia_intr_establish,
	pccbb_pcmcia_intr_disestablish,
	pccbb_pcmcia_intr_string,
	pccbb_pcmcia_socket_enable,
	pccbb_pcmcia_socket_disable,
	pccbb_pcmcia_card_detect
};

static struct cardbus_functions pccbb_funcs = {
	pccbb_rbus_cb_space_alloc,
	pccbb_rbus_cb_space_free,
	pccbb_cb_intr_establish,
	pccbb_cb_intr_disestablish,
	pccbb_ctrl,
	pccbb_power,
};

/*
 * delay_ms() is wait in milliseconds.  It should be used instead
 * of delay() if you want to wait more than 1 ms.
 */
static inline void
delay_ms(int millis, void *param)
{
	if (cold)
		delay(millis * 1000);
	else
		tsleep_nsec(param, PWAIT, "pccbb", MSEC_TO_NSEC(millis));
}

int
pcicbbmatch(struct device *parent, void *match, void *aux)
{
	struct pci_attach_args *pa = (struct pci_attach_args *)aux;

	if (PCI_CLASS(pa->pa_class) == PCI_CLASS_BRIDGE &&
	    PCI_SUBCLASS(pa->pa_class) == PCI_SUBCLASS_BRIDGE_CARDBUS &&
	    PCI_INTERFACE(pa->pa_class) == 0) {
		return 1;
	}

	return 0;
}

#define MAKEID(vendor, prod) (((vendor) << PCI_VENDOR_SHIFT) \
				| ((prod) << PCI_PRODUCT_SHIFT))

struct yenta_chipinfo {
	pcireg_t yc_id;		       /* vendor tag | product tag */
	int yc_chiptype;
	int yc_flags;
} yc_chipsets[] = {
	/* Texas Instruments chips */
	{ MAKEID(PCI_VENDOR_TI, PCI_PRODUCT_TI_PCI1130), CB_TI113X,
	    PCCBB_PCMCIA_IO_RELOC | PCCBB_PCMCIA_MEM_32},
	{ MAKEID(PCI_VENDOR_TI, PCI_PRODUCT_TI_PCI1131), CB_TI113X,
	    PCCBB_PCMCIA_IO_RELOC | PCCBB_PCMCIA_MEM_32},
	{ MAKEID(PCI_VENDOR_TI, PCI_PRODUCT_TI_PCI1250), CB_TI125X,
	    PCCBB_PCMCIA_IO_RELOC | PCCBB_PCMCIA_MEM_32},
	{ MAKEID(PCI_VENDOR_TI, PCI_PRODUCT_TI_PCI1220), CB_TI12XX,
	    PCCBB_PCMCIA_IO_RELOC | PCCBB_PCMCIA_MEM_32},
	{ MAKEID(PCI_VENDOR_TI, PCI_PRODUCT_TI_PCI1221), CB_TI12XX,
	    PCCBB_PCMCIA_IO_RELOC | PCCBB_PCMCIA_MEM_32},
	{ MAKEID(PCI_VENDOR_TI, PCI_PRODUCT_TI_PCI1225), CB_TI12XX,
	    PCCBB_PCMCIA_IO_RELOC | PCCBB_PCMCIA_MEM_32},
	{ MAKEID(PCI_VENDOR_TI, PCI_PRODUCT_TI_PCI1251), CB_TI125X,
	    PCCBB_PCMCIA_IO_RELOC | PCCBB_PCMCIA_MEM_32},
	{ MAKEID(PCI_VENDOR_TI, PCI_PRODUCT_TI_PCI1251B), CB_TI125X,
	    PCCBB_PCMCIA_IO_RELOC | PCCBB_PCMCIA_MEM_32},
	{ MAKEID(PCI_VENDOR_TI, PCI_PRODUCT_TI_PCI1211), CB_TI12XX,
	    PCCBB_PCMCIA_IO_RELOC | PCCBB_PCMCIA_MEM_32},
	{ MAKEID(PCI_VENDOR_TI, PCI_PRODUCT_TI_PCI1410), CB_TI12XX,
	    PCCBB_PCMCIA_IO_RELOC | PCCBB_PCMCIA_MEM_32},
	{ MAKEID(PCI_VENDOR_TI, PCI_PRODUCT_TI_PCI1420), CB_TI12XX,
	    PCCBB_PCMCIA_IO_RELOC | PCCBB_PCMCIA_MEM_32},
	{ MAKEID(PCI_VENDOR_TI, PCI_PRODUCT_TI_PCI1450), CB_TI125X,
	    PCCBB_PCMCIA_IO_RELOC | PCCBB_PCMCIA_MEM_32},
	{ MAKEID(PCI_VENDOR_TI, PCI_PRODUCT_TI_PCI1451), CB_TI12XX,
	    PCCBB_PCMCIA_IO_RELOC | PCCBB_PCMCIA_MEM_32},
	{ MAKEID(PCI_VENDOR_TI, PCI_PRODUCT_TI_PCI1510), CB_TI12XX,
	    PCCBB_PCMCIA_IO_RELOC | PCCBB_PCMCIA_MEM_32},
	{ MAKEID(PCI_VENDOR_TI, PCI_PRODUCT_TI_PCI7XX1), CB_TI12XX,
	    PCCBB_PCMCIA_IO_RELOC | PCCBB_PCMCIA_MEM_32},

	/* Ricoh chips */
	{ MAKEID(PCI_VENDOR_RICOH, PCI_PRODUCT_RICOH_RF5C475), CB_RX5C47X,
	    PCCBB_PCMCIA_MEM_32},
	{ MAKEID(PCI_VENDOR_RICOH, PCI_PRODUCT_RICOH_RF5C476), CB_RX5C47X,
	    PCCBB_PCMCIA_MEM_32},
	{ MAKEID(PCI_VENDOR_RICOH, PCI_PRODUCT_RICOH_RF5C477), CB_RX5C47X,
	    PCCBB_PCMCIA_MEM_32},
	{ MAKEID(PCI_VENDOR_RICOH, PCI_PRODUCT_RICOH_RF5C478), CB_RX5C47X,
	    PCCBB_PCMCIA_MEM_32},
	{ MAKEID(PCI_VENDOR_RICOH, PCI_PRODUCT_RICOH_RF5C465), CB_RX5C46X,
	    PCCBB_PCMCIA_MEM_32},
	{ MAKEID(PCI_VENDOR_RICOH, PCI_PRODUCT_RICOH_RF5C466), CB_RX5C46X,
	    PCCBB_PCMCIA_MEM_32},

	/* Toshiba products */
	{ MAKEID(PCI_VENDOR_TOSHIBA2, PCI_PRODUCT_TOSHIBA2_TOPIC95),
	    CB_TOPIC95, PCCBB_PCMCIA_MEM_32},
	{ MAKEID(PCI_VENDOR_TOSHIBA2, PCI_PRODUCT_TOSHIBA2_TOPIC95B),
	    CB_TOPIC95B, PCCBB_PCMCIA_MEM_32},
	{ MAKEID(PCI_VENDOR_TOSHIBA2, PCI_PRODUCT_TOSHIBA2_TOPIC97),
	    CB_TOPIC97, PCCBB_PCMCIA_MEM_32},
	{ MAKEID(PCI_VENDOR_TOSHIBA2, PCI_PRODUCT_TOSHIBA2_TOPIC100),
	    CB_TOPIC97, PCCBB_PCMCIA_MEM_32},

	/* Cirrus Logic products */
	{ MAKEID(PCI_VENDOR_CIRRUS, PCI_PRODUCT_CIRRUS_CL_PD6832),
	    CB_CIRRUS, PCCBB_PCMCIA_MEM_32},
	{ MAKEID(PCI_VENDOR_CIRRUS, PCI_PRODUCT_CIRRUS_CL_PD6833),
	    CB_CIRRUS, PCCBB_PCMCIA_MEM_32},

	/* older O2Micro bridges */
	{ MAKEID(PCI_VENDOR_O2MICRO, PCI_PRODUCT_O2MICRO_OZ6729),
	    CB_OLDO2MICRO, PCCBB_PCMCIA_MEM_32},
	{ MAKEID(PCI_VENDOR_O2MICRO, PCI_PRODUCT_O2MICRO_OZ6730),
	    CB_OLDO2MICRO, PCCBB_PCMCIA_MEM_32},
	{ MAKEID(PCI_VENDOR_O2MICRO, PCI_PRODUCT_O2MICRO_OZ6872), /* 68[71]2 */
	    CB_OLDO2MICRO, PCCBB_PCMCIA_MEM_32},
	{ MAKEID(PCI_VENDOR_O2MICRO, PCI_PRODUCT_O2MICRO_OZ6832),
	    CB_OLDO2MICRO, PCCBB_PCMCIA_MEM_32},
	{ MAKEID(PCI_VENDOR_O2MICRO, PCI_PRODUCT_O2MICRO_OZ6836),
	    CB_OLDO2MICRO, PCCBB_PCMCIA_MEM_32},

	/* sentinel, or Generic chip */
	{ 0 /* null id */ , CB_UNKNOWN, PCCBB_PCMCIA_MEM_32},
};

int
cb_chipset(u_int32_t pci_id, int *flagp)
{
	struct yenta_chipinfo *yc;

	/* Loop over except the last default entry. */
	for (yc = yc_chipsets; yc < yc_chipsets +
	    sizeof(yc_chipsets) / sizeof(yc_chipsets[0]) - 1; yc++)
		if (pci_id == yc->yc_id)
			break;

	if (flagp != NULL)
		*flagp = yc->yc_flags;

	return (yc->yc_chiptype);
}

void
pccbb_shutdown(void *arg)
{
	struct pccbb_softc *sc = arg;
	pcireg_t command;

	DPRINTF(("%s: shutdown\n", sc->sc_dev.dv_xname));

	/* turn off power */
	pccbb_power((cardbus_chipset_tag_t)sc, CARDBUS_VCC_0V | CARDBUS_VPP_0V);

	bus_space_write_4(sc->sc_base_memt, sc->sc_base_memh, CB_SOCKET_MASK,
	    0);

	command = pci_conf_read(sc->sc_pc, sc->sc_tag, PCI_COMMAND_STATUS_REG);

	command &= ~(PCI_COMMAND_IO_ENABLE | PCI_COMMAND_MEM_ENABLE |
	    PCI_COMMAND_MASTER_ENABLE);
	pci_conf_write(sc->sc_pc, sc->sc_tag, PCI_COMMAND_STATUS_REG, command);
}

void
pccbbattach(struct device *parent, struct device *self, void *aux)
{
	struct pccbb_softc *sc = (void *)self;
	struct pci_attach_args *pa = aux;
	pci_chipset_tag_t pc = pa->pa_pc;
	pci_intr_handle_t ih;
	const char *intrstr = NULL;
	u_long busnum;
	int flags;

	pccbb_attach_hook(parent, self, pa);

	sc->sc_chipset = cb_chipset(pa->pa_id, &flags);
	sc->sc_id = pa->pa_id;

#ifdef CBB_DEBUG
	printf(" (chipflags %x)", flags);
#endif

	TAILQ_INIT(&sc->sc_memwindow);
	TAILQ_INIT(&sc->sc_iowindow);

	sc->sc_rbus_iot = rbus_pccbb_parent_io(self, pa);
	sc->sc_rbus_memt = rbus_pccbb_parent_mem(self, pa);

	/*
	 * MAP socket registers and ExCA registers on memory-space
	 */
	if (pci_mapreg_map(pa, PCI_SOCKBASE, PCI_MAPREG_TYPE_MEM, 0,
	    &sc->sc_base_memt, &sc->sc_base_memh, NULL, NULL, 0)) {
		printf("can't map registers\n");
		return;
	}

	sc->sc_busnum = pci_conf_read(pc, pa->pa_tag, PCI_BUSNUM);

#if defined CBB_DEBUG
	{
		static char *intrname[5] = { "NON", "A", "B", "C", "D" };
		printf(": intrpin %s, line %d\n",
		    intrname[pa->pa_intrpin], pa->pa_intrline);
	}
#endif

	/* setup softc */
	sc->sc_pc = pc;
	sc->sc_iot = pa->pa_iot;
	sc->sc_memt = pa->pa_memt;
	sc->sc_dmat = pa->pa_dmat;
	sc->sc_tag = pa->pa_tag;
	sc->sc_function = pa->pa_function;
	sc->sc_intrtag = pa->pa_intrtag;
	sc->sc_intrpin = pa->pa_intrpin;

	sc->sc_pcmcia_flags = flags;   /* set PCMCIA facility */

	/* Map and establish the interrupt. */
	if (pci_intr_map(pa, &ih)) {
		printf(": couldn't map interrupt\n");
		return;
	}
	intrstr = pci_intr_string(pc, ih);
	/* must do this after intr is mapped and established */
	sc->sc_intrline = pci_intr_line(pc, ih);

	/*
	 * XXX pccbbintr should be called under the priority lower
	 * than any other hard interrupts.
	 */
	sc->sc_ih = pci_intr_establish(pc, ih, IPL_BIO, pccbbintr, sc,
	    sc->sc_dev.dv_xname);

	if (sc->sc_ih == NULL) {
		printf(": couldn't establish interrupt");
		if (intrstr != NULL) {
			printf(" at %s", intrstr);
		}
		printf("\n");
		return;
	}
	printf(": %s", intrstr);

	/*
	 * When the bus number isn't configured, try to allocate one
	 * ourselves.
	 */
	if ((sc->sc_busnum & 0x00ffff00) == 0 && pa->pa_busex &&
	    extent_alloc(pa->pa_busex, 1, 1, 0, 0, EX_NOWAIT, &busnum) == 0) {
		sc->sc_busnum |= (busnum << 8);
		sc->sc_busnum |= (busnum << 16);
		pci_conf_write(pc, pa->pa_tag, PCI_BUSNUM, sc->sc_busnum);
	}

	/*
	 * When the bus number still isn't set correctly, give up
	 * using 32-bit CardBus mode.
	 */
	if (((sc->sc_busnum >> 8) & 0xff) == 0) {
		printf(", CardBus support disabled");
		sc->sc_pcmcia_flags |= PCCBB_PCMCIA_16BITONLY;
	}

	printf("\n");

	/* Disable legacy register mapping. */
	pccbb_legacy_disable(sc);

	timeout_set(&sc->sc_ins_tmo, pci113x_insert, sc);
	config_defer(self, pccbb_pci_callback);
}

/*
 * void pccbb_pci_callback(struct device *self)
 *
 *   The actual attach routine: get memory space for YENTA register
 *   space, setup YENTA register and route interrupt.
 *
 *   This function should be deferred because this device may obtain
 *   memory space dynamically.  This function must avoid obtaining
 *   memory area which has already kept for another device.  Also,
 *   this function MUST be done before ISA attach process because this
 *   function kills pcic compatible port used by ISA pcic.
 */
void
pccbb_pci_callback(struct device *self)
{
	struct pccbb_softc *sc = (void *)self;
	pci_chipset_tag_t pc = sc->sc_pc;
	bus_space_tag_t base_memt;
	bus_space_handle_t base_memh;
	u_int32_t maskreg;
	struct cbslot_attach_args cba;
	struct pcmciabus_attach_args paa;
	struct cardslot_attach_args caa;
	struct cardslot_softc *csc;
	u_int32_t sockstat;

	base_memt = sc->sc_base_memt;  /* socket regs memory tag */
	base_memh = sc->sc_base_memh;  /* socket regs memory handle */

	/* bus bridge initialization */
	pccbb_chipinit(sc);

	/* clear data structure for child device interrupt handlers */
	sc->sc_pil = NULL;
	sc->sc_pil_intr_enable = 1;

	sockstat = bus_space_read_4(base_memt, base_memh, CB_SOCKET_STAT);
	if ((sockstat & CB_SOCKET_STAT_CD) == 0)
		sc->sc_flags |= CBB_CARDEXIST;

	/*
	 * attach cardbus
	 */
	if (!(sc->sc_pcmcia_flags & PCCBB_PCMCIA_16BITONLY)) {
		pcireg_t busreg = pci_conf_read(pc, sc->sc_tag, PCI_BUSNUM);
		pcireg_t bhlc = pci_conf_read(pc, sc->sc_tag, PCI_BHLC_REG);

		/* initialize cbslot_attach */
		cba.cba_busname = "cardbus";
		cba.cba_iot = sc->sc_iot;
		cba.cba_memt = sc->sc_memt;
		cba.cba_dmat = sc->sc_dmat;
		cba.cba_bus = (busreg >> 8) & 0x0ff;
		cba.cba_cc = (void *)sc;
		cba.cba_pc = sc->sc_pc;
		cba.cba_cf = &pccbb_funcs;
		cba.cba_intrline = sc->sc_intrline;

		cba.cba_rbus_iot = sc->sc_rbus_iot;
		cba.cba_rbus_memt = sc->sc_rbus_memt;

		cba.cba_cacheline = PCI_CACHELINE(bhlc);
		cba.cba_lattimer = PCI_CB_LATENCY(busreg);

#if defined CBB_DEBUG
		printf("%s: cacheline 0x%x lattimer 0x%x\n",
		    sc->sc_dev.dv_xname, cba.cba_cacheline, cba.cba_lattimer);
		printf("%s: bhlc 0x%x lscp 0x%x\n", sc->sc_dev.dv_xname, bhlc,
		    busreg);
#endif
#if defined SHOW_REGS
		cb_show_regs(sc->sc_pc, sc->sc_tag, sc->sc_base_memt,
		    sc->sc_base_memh);
#endif
	}

	pccbb_pcmcia_attach_setup(sc, &paa);
	caa.caa_cb_attach = NULL;
	if (!(sc->sc_pcmcia_flags & PCCBB_PCMCIA_16BITONLY)) {
		caa.caa_cb_attach = &cba;
	}
	caa.caa_16_attach = &paa;
	caa.caa_ph = &sc->sc_pcmcia_h;

	if (NULL != (csc = (void *)config_found(self, &caa, cbbprint))) {
		DPRINTF(("pccbbattach: found cardslot\n"));
		sc->sc_csc = csc;
	}

	sc->sc_ints_on = 1;

	/* CSC Interrupt: Card detect interrupt on */
	maskreg = bus_space_read_4(base_memt, base_memh, CB_SOCKET_MASK);
	maskreg |= CB_SOCKET_MASK_CD;  /* Card detect intr is turned on. */
	bus_space_write_4(base_memt, base_memh, CB_SOCKET_MASK, maskreg);
	/* reset interrupt */
	bus_space_write_4(base_memt, base_memh, CB_SOCKET_EVENT,
	    bus_space_read_4(base_memt, base_memh, CB_SOCKET_EVENT));

	return;
}

void
pccbb_legacy_disable(struct pccbb_softc *sc)
{
	pcireg_t reg;

	switch (sc->sc_chipset) {
	case CB_RX5C46X:
		/*
		 * The legacy pcic io-port on Ricoh RX5C46X CardBus bridges
		 * cannot be disabled by substituting 0 into PCI_LEGACY
		 * register.  Ricoh CardBus bridges have special bits on Bridge
		 * control reg (addr 0x3e on PCI config space).
		 */
		reg = pci_conf_read(sc->sc_pc, sc->sc_tag, PCI_BCR_INTR);
		reg &= ~(CB_BCRI_RL_3E0_ENA | CB_BCRI_RL_3E2_ENA);
		pci_conf_write(sc->sc_pc, sc->sc_tag, PCI_BCR_INTR, reg);
		break;

	default:
		pci_conf_write(sc->sc_pc, sc->sc_tag, PCI_LEGACY, 0x0);
		break;
	}
}

/*
 * void pccbb_chipinit(struct pccbb_softc *sc)
 *
 *   This function initialize YENTA chip registers listed below:
 *     1) PCI command reg,
 *     2) PCI and CardBus latency timer,
 *     3) route PCI interrupt,
 *     4) close all memory and io windows.
 */
void
pccbb_chipinit(struct pccbb_softc *sc)
{
	pci_chipset_tag_t pc = sc->sc_pc;
	pcitag_t tag = sc->sc_tag;
	pcireg_t reg;

	/* Power on the controller if the BIOS didn't */
	pci_set_powerstate(pc, tag, PCI_PMCSR_STATE_D0);

	/*
	 * Set PCI command reg.
	 * Some laptop's BIOSes (i.e. TICO) do not enable CardBus chip.
	 */
	reg = pci_conf_read(pc, tag, PCI_COMMAND_STATUS_REG);
	/* I believe it is harmless. */
	reg |= (PCI_COMMAND_IO_ENABLE | PCI_COMMAND_MEM_ENABLE |
	    PCI_COMMAND_MASTER_ENABLE);
	pci_conf_write(pc, tag, PCI_COMMAND_STATUS_REG, reg);

	/*
	 * Set CardBus latency timer.
	 */
	reg = pci_conf_read(pc, tag, PCI_CB_LSCP_REG);
	if (PCI_CB_LATENCY(reg) < 0x20) {
		reg &= ~(PCI_CB_LATENCY_MASK << PCI_CB_LATENCY_SHIFT);
		reg |= (0x20 << PCI_CB_LATENCY_SHIFT);
		pci_conf_write(pc, tag, PCI_CB_LSCP_REG, reg);
	}
	DPRINTF(("CardBus latency timer 0x%x (%x)\n",
	    PCI_CB_LATENCY(reg), pci_conf_read(pc, tag, PCI_CB_LSCP_REG)));

	/*
	 * Set PCI latency timer.
	 */
	reg = pci_conf_read(pc, tag, PCI_BHLC_REG);
	if (PCI_LATTIMER(reg) < 0x10) {
		reg &= ~(PCI_LATTIMER_MASK << PCI_LATTIMER_SHIFT);
		reg |= (0x10 << PCI_LATTIMER_SHIFT);
		pci_conf_write(pc, tag, PCI_BHLC_REG, reg);
	}
	DPRINTF(("PCI latency timer 0x%x (%x)\n",
	    PCI_LATTIMER(reg), pci_conf_read(pc, tag, PCI_BHLC_REG)));

	/* Route functional interrupts to PCI. */
	reg = pci_conf_read(pc, tag, PCI_BCR_INTR);
	reg |= CB_BCR_INTR_IREQ_ENABLE;		/* disable PCI Intr */
	reg |= CB_BCR_WRITE_POST_ENABLE;	/* enable write post */
	reg |= CB_BCR_RESET_ENABLE;		/* assert reset */
	pci_conf_write(pc, tag, PCI_BCR_INTR, reg);

	switch (sc->sc_chipset) {
	case CB_TI113X:
		reg = pci_conf_read(pc, tag, PCI_CBCTRL);
		/* This bit is shared, but may read as 0 on some chips, so set
		   it explicitly on both functions. */
		reg |= PCI113X_CBCTRL_PCI_IRQ_ENA;
		/* CSC intr enable */
		reg |= PCI113X_CBCTRL_PCI_CSC;
		/* functional intr prohibit | prohibit ISA routing */
		reg &= ~(PCI113X_CBCTRL_PCI_INTR | PCI113X_CBCTRL_INT_MASK);
		pci_conf_write(pc, tag, PCI_CBCTRL, reg);
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
		 */
		reg = pci_conf_read(pc, tag, PCI12XX_MFUNC);
		if (reg == PCI12XX_MFUNC_DEFAULT) {
			reg &= ~PCI12XX_MFUNC_PIN0;
			reg |= PCI12XX_MFUNC_PIN0_INTA;
			if ((pci_conf_read(pc, tag, PCI_SYSCTRL) &
			     PCI12XX_SYSCTRL_INTRTIE) == 0) {
				reg &= ~PCI12XX_MFUNC_PIN1;
				reg |= PCI12XX_MFUNC_PIN1_INTB;
			}
			pci_conf_write(pc, tag, PCI12XX_MFUNC, reg);
		}
		/* FALLTHROUGH */

	case CB_TI125X:
		/*
		 * Disable zoom video.  Some machines initialize this
		 * improperly and experience has shown that this helps
		 * prevent strange behavior.
		 */
		pci_conf_write(pc, tag, PCI12XX_MMCTRL, 0);

		reg = pci_conf_read(pc, tag, PCI_SYSCTRL);
		reg |= PCI12XX_SYSCTRL_VCCPROT;
		pci_conf_write(pc, tag, PCI_SYSCTRL, reg);
		reg = pci_conf_read(pc, tag, PCI_CBCTRL);
		reg |= PCI12XX_CBCTRL_CSC;
		pci_conf_write(pc, tag, PCI_CBCTRL, reg);
		break;

	case CB_TOPIC95B:
		reg = pci_conf_read(pc, tag, TOPIC_SOCKET_CTRL);
		reg |= TOPIC_SOCKET_CTRL_SCR_IRQSEL;
		pci_conf_write(pc, tag, TOPIC_SOCKET_CTRL, reg);

		reg = pci_conf_read(pc, tag, TOPIC_SLOT_CTRL);
		DPRINTF(("%s: topic slot ctrl reg 0x%x -> ",
		    sc->sc_dev.dv_xname, reg));
		reg |= (TOPIC_SLOT_CTRL_SLOTON | TOPIC_SLOT_CTRL_SLOTEN |
		    TOPIC_SLOT_CTRL_ID_LOCK | TOPIC_SLOT_CTRL_CARDBUS);
		reg &= ~TOPIC_SLOT_CTRL_SWDETECT;
		DPRINTF(("0x%x\n", reg));
		pci_conf_write(pc, tag, TOPIC_SLOT_CTRL, reg);
		break;

	case CB_TOPIC97:
		reg = pci_conf_read(pc, tag, TOPIC_SLOT_CTRL);
		DPRINTF(("%s: topic slot ctrl reg 0x%x -> ",
		    sc->sc_dev.dv_xname, reg));
		reg |= (TOPIC_SLOT_CTRL_SLOTON | TOPIC_SLOT_CTRL_SLOTEN |
		    TOPIC_SLOT_CTRL_ID_LOCK | TOPIC_SLOT_CTRL_CARDBUS);
		reg &= ~TOPIC_SLOT_CTRL_SWDETECT;
		reg |= TOPIC97_SLOT_CTRL_PCIINT;
		reg &= ~(TOPIC97_SLOT_CTRL_STSIRQP | TOPIC97_SLOT_CTRL_IRQP);
		DPRINTF(("0x%x\n", reg));
		pci_conf_write(pc, tag, TOPIC_SLOT_CTRL, reg);

		/* make sure to assert LV card support bits */
		bus_space_write_1(sc->sc_base_memt, sc->sc_base_memh,
		    0x800 + 0x3e, bus_space_read_1(sc->sc_base_memt,
		    sc->sc_base_memh, 0x800 + 0x3e) | 0x03);
		break;

	case CB_OLDO2MICRO:
		/*
		 * older bridges have problems with both read prefetch and
		 * write bursting depending on the combination of the chipset,
		 * bridge and the cardbus card. so disable them to be on the
		 * safe side. One example is O2Micro 6812 with Atheros AR5012
		 * chipsets
		 */
		DPRINTF(("%s: old O2Micro bridge found\n",
		    sc->sc_dev.dv_xname, reg));
		reg = pci_conf_read(pc, tag, O2MICRO_RESERVED1);
		pci_conf_write(pc, tag, O2MICRO_RESERVED1, reg &
		    ~(O2MICRO_RES_READ_PREFETCH | O2MICRO_RES_WRITE_BURST));
		reg = pci_conf_read(pc, tag, O2MICRO_RESERVED2);
		pci_conf_write(pc, tag, O2MICRO_RESERVED2, reg &
		    ~(O2MICRO_RES_READ_PREFETCH | O2MICRO_RES_WRITE_BURST));
		break;
	}

	/* Close all memory and I/O windows. */
	pci_conf_write(pc, tag, PCI_CB_MEMBASE0, 0xffffffff);
	pci_conf_write(pc, tag, PCI_CB_MEMLIMIT0, 0);
	pci_conf_write(pc, tag, PCI_CB_MEMBASE1, 0xffffffff);
	pci_conf_write(pc, tag, PCI_CB_MEMLIMIT1, 0);
	pci_conf_write(pc, tag, PCI_CB_IOBASE0, 0xffffffff);
	pci_conf_write(pc, tag, PCI_CB_IOLIMIT0, 0);
	pci_conf_write(pc, tag, PCI_CB_IOBASE1, 0xffffffff);
	pci_conf_write(pc, tag, PCI_CB_IOLIMIT1, 0);

	/* reset 16-bit pcmcia bus */
	bus_space_write_1(sc->sc_base_memt, sc->sc_base_memh,
	    0x800 + PCIC_INTR,
	    bus_space_read_1(sc->sc_base_memt, sc->sc_base_memh,
		0x800 + PCIC_INTR) & ~PCIC_INTR_RESET);

	/* turn off power */
	pccbb_power((cardbus_chipset_tag_t)sc, CARDBUS_VCC_0V | CARDBUS_VPP_0V);
}




/*
 * void pccbb_pcmcia_attach_setup(struct pccbb_softc *sc,
 *					 struct pcmciabus_attach_args *paa)
 *
 *   This function attaches 16-bit PCcard bus.
 */
void
pccbb_pcmcia_attach_setup(struct pccbb_softc *sc,
    struct pcmciabus_attach_args *paa)
{
	struct pcic_handle *ph = &sc->sc_pcmcia_h;
	rbus_tag_t rb;

	/* initialize pcmcia part in pccbb_softc */
	ph->ph_parent = (struct device *)sc;
	ph->sock = sc->sc_function;
	ph->flags = 0;
	ph->shutdown = 0;
	ph->ih_irq = sc->sc_intrline;
	ph->ph_bus_t = sc->sc_base_memt;
	ph->ph_bus_h = sc->sc_base_memh;
	ph->ph_read = pccbb_pcmcia_read;
	ph->ph_write = pccbb_pcmcia_write;
	sc->sc_pct = &pccbb_pcmcia_funcs;

	/*
	 * We need to do a few things here:
	 * 1) Disable routing of CSC and functional interrupts to ISA IRQs by
	 *    setting the IRQ numbers to 0.
	 * 2) Set bit 4 of PCIC_INTR, which is needed on some chips to enable
	 *    routing of CSC interrupts (e.g. card removal) to PCI while in
	 *    PCMCIA mode.  We just leave this set all the time.
	 * 3) Enable card insertion/removal interrupts in case the chip also
	 *    needs that while in PCMCIA mode.
	 * 4) Clear any pending CSC interrupt.
	 */
	Pcic_write(ph, PCIC_INTR, PCIC_INTR_ENABLE | PCIC_INTR_RESET);
	if (sc->sc_chipset == CB_TI113X) {
		Pcic_write(ph, PCIC_CSC_INTR, 0);
	} else {
		Pcic_write(ph, PCIC_CSC_INTR, PCIC_CSC_INTR_CD_ENABLE);
		Pcic_read(ph, PCIC_CSC);
	}

	/* initialize pcmcia bus attachment */
	paa->paa_busname = "pcmcia";
	paa->pct = sc->sc_pct;
	paa->pch = ph;
	paa->iobase = 0;	       /* I don't use them */
	paa->iosize = 0;
	rb = ((struct pccbb_softc *)(ph->ph_parent))->sc_rbus_iot;
	paa->iobase = rb->rb_start + rb->rb_offset;
	paa->iosize = rb->rb_end - rb->rb_start;

	return;
}

#if 0
void
pccbb_pcmcia_attach_card(struct pcic_handle *ph)
{
	if (ph->flags & PCIC_FLAG_CARDP) {
		panic("pccbb_pcmcia_attach_card: already attached");
	}

	/* call the MI attach function */
	pcmcia_card_attach(ph->pcmcia);

	ph->flags |= PCIC_FLAG_CARDP;
}

void
pccbb_pcmcia_detach_card(struct pcic_handle *ph, int flags)
{
	if (!(ph->flags & PCIC_FLAG_CARDP)) {
		panic("pccbb_pcmcia_detach_card: already detached");
	}

	ph->flags &= ~PCIC_FLAG_CARDP;

	/* call the MI detach function */
	pcmcia_card_detach(ph->pcmcia, flags);
}
#endif

int
pccbb_checksockstat(struct pccbb_softc *sc)
{
	u_int32_t sockstate;

	sockstate = bus_space_read_4(sc->sc_base_memt, sc->sc_base_memh,
	    CB_SOCKET_STAT);

	if ((sockstate & CB_SOCKET_STAT_CD) != 0) {
		/* A card should be removed. */
		if (sc->sc_flags & CBB_CARDEXIST) {
			DPRINTF(("%s: card removed, 0x%08x\n",
			    sc->sc_dev.dv_xname, sockstate));
			sc->sc_flags &= ~CBB_CARDEXIST;
			if (sc->sc_csc->sc_status & CARDSLOT_STATUS_CARD_16) {
#if 0
				struct pcic_handle *ph =
				    &sc->sc_pcmcia_h;

				pcmcia_card_deactivate(ph->pcmcia);
				pccbb_pcmcia_socket_disable(ph);
				pccbb_pcmcia_detach_card(ph,
				    DETACH_FORCE);
#endif
				cardslot_event_throw(sc->sc_csc,
				    CARDSLOT_EVENT_REMOVAL_16);
			} else if (sc->sc_csc->sc_status &
			    CARDSLOT_STATUS_CARD_CB) {
				/* Cardbus intr removed */
				cardslot_event_throw(sc->sc_csc,
				    CARDSLOT_EVENT_REMOVAL_CB);
			}
		}
		return (1);
	} else if ((sockstate & CB_SOCKET_STAT_CD) == 0 &&
	    (sc->sc_flags & CBB_CARDEXIST) == 0) {
		timeout_add_msec(&sc->sc_ins_tmo, 100);
		sc->sc_flags |= CBB_INSERTING;
		return (1);
	}
	return (0);
}

/*
 * int pccbbintr(arg)
 *    void *arg;
 *   This routine handles the interrupt from Yenta PCI-CardBus bridge
 *   itself.
 */
int
pccbbintr(void *arg)
{
	struct pccbb_softc *sc = (struct pccbb_softc *)arg;
	u_int32_t sockevent;
	struct pcic_handle *ph = &sc->sc_pcmcia_h;

	if (!sc->sc_ints_on)
		return 0;

	sockevent = bus_space_read_4(sc->sc_base_memt, sc->sc_base_memh,
	    CB_SOCKET_EVENT);
	bus_space_write_4(sc->sc_base_memt, sc->sc_base_memh,
	    CB_SOCKET_EVENT, sockevent);
	Pcic_read(ph, PCIC_CSC);

	if (sockevent & CB_SOCKET_EVENT_CD) {
		if (pccbb_checksockstat(sc))
			return (1);
	}

	if (sc->sc_pil_intr_enable)
		return pccbbintr_function(sc);
	return (0);
}

/*
 * int pccbbintr_function(struct pccbb_softc *sc)
 *
 *    This function calls each interrupt handler registered at the
 *    bridge.  The interrupt handlers are called in registered order.
 */
int
pccbbintr_function(struct pccbb_softc *sc)
{
	int retval = 0, val;
	struct pccbb_intrhand_list *pil;
	int s;

	for (pil = sc->sc_pil; pil != NULL; pil = pil->pil_next) {
		s = splraise(pil->pil_level);

		val = (*pil->pil_func)(pil->pil_arg);
		if (val != 0)
			pil->pil_count.ec_count++;

		splx(s);

		if (retval == 0 || val != 0)
			retval = val;
	}

	return retval;
}

void
pci113x_insert(void *arg)
{
	struct pccbb_softc *sc = (struct pccbb_softc *)arg;
	u_int32_t sockevent, sockstate;

	sockevent = bus_space_read_4(sc->sc_base_memt, sc->sc_base_memh,
	    CB_SOCKET_EVENT);
	sockstate = bus_space_read_4(sc->sc_base_memt, sc->sc_base_memh,
	    CB_SOCKET_STAT);

	if (0 == (sockstate & CB_SOCKET_STAT_CD)) {	/* card exist */
		DPRINTF(("%s: 0x%08x", sc->sc_dev.dv_xname, sockevent));
		DPRINTF((" card inserted, 0x%08x\n", sockstate));
		sc->sc_flags |= CBB_CARDEXIST;
		/* call pccard interrupt handler here */
		if (sockstate & CB_SOCKET_STAT_16BIT) {
			/* 16-bit card found */
/*      pccbb_pcmcia_attach_card(&sc->sc_pcmcia_h); */
			cardslot_event_throw(sc->sc_csc,
			    CARDSLOT_EVENT_INSERTION_16);
		} else if (sockstate & CB_SOCKET_STAT_CB) {
			/* cardbus card found */
/*      cardbus_attach_card(sc->sc_csc); */
			cardslot_event_throw(sc->sc_csc,
			    CARDSLOT_EVENT_INSERTION_CB);
		} else {
			/* who are you? */
		}
	} else {
		timeout_add_msec(&sc->sc_ins_tmo, 100);
	}
}

#define PCCBB_PCMCIA_OFFSET 0x800
u_int8_t
pccbb_pcmcia_read(struct pcic_handle *ph, int reg)
{
	bus_space_barrier(ph->ph_bus_t, ph->ph_bus_h,
	    PCCBB_PCMCIA_OFFSET + reg, 1, BUS_SPACE_BARRIER_READ);

	return bus_space_read_1(ph->ph_bus_t, ph->ph_bus_h,
	    PCCBB_PCMCIA_OFFSET + reg);
}

void
pccbb_pcmcia_write(struct pcic_handle *ph, int reg, int val)
{
	bus_space_barrier(ph->ph_bus_t, ph->ph_bus_h,
	    PCCBB_PCMCIA_OFFSET + reg, 1, BUS_SPACE_BARRIER_WRITE);

	bus_space_write_1(ph->ph_bus_t, ph->ph_bus_h, PCCBB_PCMCIA_OFFSET + reg,
	    val);
}

/*
 * int pccbb_ctrl(cardbus_chipset_tag_t, int)
 */
int
pccbb_ctrl(cardbus_chipset_tag_t ct, int command)
{
	struct pccbb_softc *sc = (struct pccbb_softc *)ct;

	switch (command) {
	case CARDBUS_CD:
		if (2 == pccbb_detect_card(sc)) {
			int retval = 0;
			int status = cb_detect_voltage(sc);
			if (PCCARD_VCC_5V & status) {
				retval |= CARDBUS_5V_CARD;
			}
			if (PCCARD_VCC_3V & status) {
				retval |= CARDBUS_3V_CARD;
			}
			if (PCCARD_VCC_XV & status) {
				retval |= CARDBUS_XV_CARD;
			}
			if (PCCARD_VCC_YV & status) {
				retval |= CARDBUS_YV_CARD;
			}
			return retval;
		} else {
			return 0;
		}
		break;
	case CARDBUS_RESET:
		return cb_reset(sc);
		break;
	case CARDBUS_IO_ENABLE:       /* fallthrough */
	case CARDBUS_IO_DISABLE:      /* fallthrough */
	case CARDBUS_MEM_ENABLE:      /* fallthrough */
	case CARDBUS_MEM_DISABLE:     /* fallthrough */
	case CARDBUS_BM_ENABLE:       /* fallthrough */
	case CARDBUS_BM_DISABLE:      /* fallthrough */
		return pccbb_cardenable(sc, command);
		break;
	}

	return 0;
}

/*
 * int pccbb_power(cardbus_chipset_tag_t, int)
 *   This function returns true when it succeeds and returns false when
 *   it fails.
 */
int
pccbb_power(cardbus_chipset_tag_t ct, int command)
{
	struct pccbb_softc *sc = (struct pccbb_softc *)ct;

	u_int32_t status, sock_ctrl;
	bus_space_tag_t memt = sc->sc_base_memt;
	bus_space_handle_t memh = sc->sc_base_memh;

	DPRINTF(("pccbb_power: %s and %s [%x]\n",
	    (command & CARDBUS_VCCMASK) == CARDBUS_VCC_UC ? "CARDBUS_VCC_UC" :
	    (command & CARDBUS_VCCMASK) == CARDBUS_VCC_5V ? "CARDBUS_VCC_5V" :
	    (command & CARDBUS_VCCMASK) == CARDBUS_VCC_3V ? "CARDBUS_VCC_3V" :
	    (command & CARDBUS_VCCMASK) == CARDBUS_VCC_XV ? "CARDBUS_VCC_XV" :
	    (command & CARDBUS_VCCMASK) == CARDBUS_VCC_YV ? "CARDBUS_VCC_YV" :
	    (command & CARDBUS_VCCMASK) == CARDBUS_VCC_0V ? "CARDBUS_VCC_0V" :
	    "UNKNOWN",
	    (command & CARDBUS_VPPMASK) == CARDBUS_VPP_UC ? "CARDBUS_VPP_UC" :
	    (command & CARDBUS_VPPMASK) == CARDBUS_VPP_12V ? "CARDBUS_VPP_12V" :
	    (command & CARDBUS_VPPMASK) == CARDBUS_VPP_VCC ? "CARDBUS_VPP_VCC" :
	    (command & CARDBUS_VPPMASK) == CARDBUS_VPP_0V ? "CARDBUS_VPP_0V" :
	    "UNKNOWN", command));

	status = bus_space_read_4(memt, memh, CB_SOCKET_STAT);
	sock_ctrl = bus_space_read_4(memt, memh, CB_SOCKET_CTRL);

	switch (command & CARDBUS_VCCMASK) {
	case CARDBUS_VCC_UC:
		break;
	case CARDBUS_VCC_5V:
		if (CB_SOCKET_STAT_5VCARD & status) {	/* check 5 V card */
			sock_ctrl &= ~CB_SOCKET_CTRL_VCCMASK;
			sock_ctrl |= CB_SOCKET_CTRL_VCC_5V;
		} else {
			printf("%s: BAD voltage request: no 5 V card\n",
			    sc->sc_dev.dv_xname);
		}
		break;
	case CARDBUS_VCC_3V:
		if (CB_SOCKET_STAT_3VCARD & status) {
			sock_ctrl &= ~CB_SOCKET_CTRL_VCCMASK;
			sock_ctrl |= CB_SOCKET_CTRL_VCC_3V;
		} else {
			printf("%s: BAD voltage request: no 3.3 V card\n",
			    sc->sc_dev.dv_xname);
		}
		break;
	case CARDBUS_VCC_0V:
		sock_ctrl &= ~CB_SOCKET_CTRL_VCCMASK;
		break;
	default:
		return 0;	       /* power NEVER changed */
		break;
	}

	switch (command & CARDBUS_VPPMASK) {
	case CARDBUS_VPP_UC:
		break;
	case CARDBUS_VPP_0V:
		sock_ctrl &= ~CB_SOCKET_CTRL_VPPMASK;
		break;
	case CARDBUS_VPP_VCC:
		sock_ctrl &= ~CB_SOCKET_CTRL_VPPMASK;
		sock_ctrl |= ((sock_ctrl >> 4) & 0x07);
		break;
	case CARDBUS_VPP_12V:
		sock_ctrl &= ~CB_SOCKET_CTRL_VPPMASK;
		sock_ctrl |= CB_SOCKET_CTRL_VPP_12V;
		break;
	}

#if 0
	DPRINTF(("sock_ctrl: %x\n", sock_ctrl));
#endif
	bus_space_write_4(memt, memh, CB_SOCKET_CTRL, sock_ctrl);
	status = bus_space_read_4(memt, memh, CB_SOCKET_STAT);

	if (status & CB_SOCKET_STAT_BADVCC) {	/* bad Vcc request */
		printf
		    ("%s: bad Vcc request. sock_ctrl 0x%x, sock_status 0x%x\n",
		    sc->sc_dev.dv_xname, sock_ctrl, status);
		DPRINTF(("pccbb_power: %s and %s [%x]\n",
		    (command & CARDBUS_VCCMASK) ==
		    CARDBUS_VCC_UC ? "CARDBUS_VCC_UC" : (command &
		    CARDBUS_VCCMASK) ==
		    CARDBUS_VCC_5V ? "CARDBUS_VCC_5V" : (command &
		    CARDBUS_VCCMASK) ==
		    CARDBUS_VCC_3V ? "CARDBUS_VCC_3V" : (command &
		    CARDBUS_VCCMASK) ==
		    CARDBUS_VCC_XV ? "CARDBUS_VCC_XV" : (command &
		    CARDBUS_VCCMASK) ==
		    CARDBUS_VCC_YV ? "CARDBUS_VCC_YV" : (command &
		    CARDBUS_VCCMASK) ==
		    CARDBUS_VCC_0V ? "CARDBUS_VCC_0V" : "UNKNOWN",
		    (command & CARDBUS_VPPMASK) ==
		    CARDBUS_VPP_UC ? "CARDBUS_VPP_UC" : (command &
		    CARDBUS_VPPMASK) ==
		    CARDBUS_VPP_12V ? "CARDBUS_VPP_12V" : (command &
		    CARDBUS_VPPMASK) ==
		    CARDBUS_VPP_VCC ? "CARDBUS_VPP_VCC" : (command &
		    CARDBUS_VPPMASK) ==
		    CARDBUS_VPP_0V ? "CARDBUS_VPP_0V" : "UNKNOWN", command));
#if 0
		if (command == (CARDBUS_VCC_0V | CARDBUS_VPP_0V)) {
			u_int32_t force =
			    bus_space_read_4(memt, memh, CB_SOCKET_FORCE);
			/* Reset Bad Vcc request */
			force &= ~CB_SOCKET_FORCE_BADVCC;
			bus_space_write_4(memt, memh, CB_SOCKET_FORCE, force);
			printf("new status 0x%x\n", bus_space_read_4(memt, memh,
			    CB_SOCKET_STAT));
			return 1;
		}
#endif
		return 0;
	}

	/*
	 * XXX delay 300 ms: though the standard defines that the Vcc set-up
	 * time is 20 ms, some PC-Card bridge requires longer duration.
	 */
	delay(300 * 1000);

	return 1;		       /* power changed correctly */
}

#if defined CB_PCMCIA_POLL
struct cb_poll_str {
	void *arg;
	int (*func)(void *);
	int level;
	pccard_chipset_tag_t ct;
	int count;
};

static struct cb_poll_str cb_poll[10];
static int cb_poll_n = 0;
static struct timeout cb_poll_timeout;

void cb_pcmcia_poll(void *arg);

void
cb_pcmcia_poll(void *arg)
{
	struct cb_poll_str *poll = arg;
	struct cbb_pcmcia_softc *psc = (void *)poll->ct->v;
	struct pccbb_softc *sc = psc->cpc_parent;
	int s;
	u_int32_t spsr;		       /* socket present-state reg */

	timeout_set(&cb_poll_timeout, cb_pcmcia_poll, arg);
	timeout_add_msec(&cb_poll_timeout, 1000 / 10);
	switch (poll->level) {
	case IPL_NET:
		s = splnet();
		break;
	case IPL_BIO:
		s = splbio();
		break;
	case IPL_TTY:		       /* fallthrough */
	default:
		s = spltty();
		break;
	}

	spsr =
	    bus_space_read_4(sc->sc_base_memt, sc->sc_base_memh,
	    CB_SOCKET_STAT);

#if defined CB_PCMCIA_POLL_ONLY && defined LEVEL2
	if (!(spsr & 0x40)) {	       /* CINT low */
#else
	if (1) {
#endif
		if ((*poll->func) (poll->arg) == 1) {
			++poll->count;
			printf("intr: reported from poller, 0x%x\n", spsr);
#if defined LEVEL2
		} else {
			printf("intr: miss! 0x%x\n", spsr);
#endif
		}
	}
	splx(s);
}
#endif /* defined CB_PCMCIA_POLL */

/*
 * int pccbb_detect_card(struct pccbb_softc *sc)
 *   return value:  0 if no card exists.
 *                  1 if 16-bit card exists.
 *                  2 if cardbus card exists.
 */
int
pccbb_detect_card(struct pccbb_softc *sc)
{
	bus_space_handle_t base_memh = sc->sc_base_memh;
	bus_space_tag_t base_memt = sc->sc_base_memt;
	u_int32_t sockstat =
	    bus_space_read_4(base_memt, base_memh, CB_SOCKET_STAT);
	int retval = 0;

	/*
	 * The SCM Microsystems TI1225-based PCI-CardBus dock card that
	 * ships with some Lucent WaveLAN cards has only one physical slot
	 * but OpenBSD probes two. The phantom card in the second slot can
	 * be ignored by punting on unsupported voltages.
	 */
	if (sockstat & CB_SOCKET_STAT_XVCARD)
		return 0;

	/* CD1 and CD2 asserted */
	if (0x00 == (sockstat & CB_SOCKET_STAT_CD)) {
		/* card must be present */
		if (!(CB_SOCKET_STAT_NOTCARD & sockstat)) {
			/* NOTACARD DEASSERTED */
			if (CB_SOCKET_STAT_CB & sockstat) {
				/* CardBus mode */
				retval = 2;
			} else if (CB_SOCKET_STAT_16BIT & sockstat) {
				/* 16-bit mode */
				retval = 1;
			}
		}
	}
	return retval;
}

/*
 * int cb_reset(struct pccbb_softc *sc)
 *   This function resets CardBus card.
 */
int
cb_reset(struct pccbb_softc *sc)
{
	/*
	 * Reset Assert at least 20 ms
	 * Some machines request longer duration.
	 */
	int reset_duration = (sc->sc_chipset == CB_RX5C47X ? 400 : 50);
	u_int32_t bcr = pci_conf_read(sc->sc_pc, sc->sc_tag, PCI_BCR_INTR);

	/* Reset bit Assert (bit 6 at 0x3E) */
	bcr |= CB_BCR_RESET_ENABLE;
	pci_conf_write(sc->sc_pc, sc->sc_tag, PCI_BCR_INTR, bcr);
	delay_ms(reset_duration, sc);

	if (CBB_CARDEXIST & sc->sc_flags) {	/* A card exists.  Reset it! */
		/* Reset bit Deassert (bit 6 at 0x3E) */
		bcr &= ~CB_BCR_RESET_ENABLE;
		pci_conf_write(sc->sc_pc, sc->sc_tag, PCI_BCR_INTR, bcr);
		delay_ms(reset_duration, sc);
	}
	/* No card found on the slot. Keep Reset. */
	return 1;
}

/*
 * int cb_detect_voltage(struct pccbb_softc *sc)
 *  This function detect card Voltage.
 */
int
cb_detect_voltage(struct pccbb_softc *sc)
{
	u_int32_t psr;		       /* socket present-state reg */
	bus_space_tag_t iot = sc->sc_base_memt;
	bus_space_handle_t ioh = sc->sc_base_memh;
	int vol = PCCARD_VCC_UKN;      /* set 0 */

	psr = bus_space_read_4(iot, ioh, CB_SOCKET_STAT);

	if (0x400u & psr) {
		vol |= PCCARD_VCC_5V;
	}
	if (0x800u & psr) {
		vol |= PCCARD_VCC_3V;
	}

	return vol;
}

int
cbbprint(void *aux, const char *pcic)
{
/*
  struct cbslot_attach_args *cba = aux;

  if (cba->cba_slot >= 0) {
    printf(" slot %d", cba->cba_slot);
  }
*/
	return UNCONF;
}

/*
 * int pccbb_cardenable(struct pccbb_softc *sc, int function)
 *   This function enables and disables the card
 */
int
pccbb_cardenable(struct pccbb_softc *sc, int function)
{
	u_int32_t command =
	    pci_conf_read(sc->sc_pc, sc->sc_tag, PCI_COMMAND_STATUS_REG);

	DPRINTF(("pccbb_cardenable:"));
	switch (function) {
	case CARDBUS_IO_ENABLE:
		command |= PCI_COMMAND_IO_ENABLE;
		break;
	case CARDBUS_IO_DISABLE:
		command &= ~PCI_COMMAND_IO_ENABLE;
		break;
	case CARDBUS_MEM_ENABLE:
		command |= PCI_COMMAND_MEM_ENABLE;
		break;
	case CARDBUS_MEM_DISABLE:
		command &= ~PCI_COMMAND_MEM_ENABLE;
		break;
	case CARDBUS_BM_ENABLE:
		command |= PCI_COMMAND_MASTER_ENABLE;
		break;
	case CARDBUS_BM_DISABLE:
		command &= ~PCI_COMMAND_MASTER_ENABLE;
		break;
	default:
		return 0;
	}

	pci_conf_write(sc->sc_pc, sc->sc_tag, PCI_COMMAND_STATUS_REG, command);
	DPRINTF((" command reg 0x%x\n", command));
	return 1;
}

/*
 * void *pccbb_cb_intr_establish(cardbus_chipset_tag_t ct,
 *					int irq,
 *					int level,
 *					int (* func)(void *),
 *					void *arg,
 *					const char *name)
 *
 *   This function registers an interrupt handler at the bridge, in
 *   order not to call the interrupt handlers of child devices when
 *   a card-deletion interrupt occurs.
 *
 *   The arguments irq is not used because pccbb selects intr vector.
 */
void *
pccbb_cb_intr_establish(cardbus_chipset_tag_t ct, int irq, int level,
    int (*func)(void *), void *arg, const char *name)
{
	struct pccbb_softc *sc = (struct pccbb_softc *)ct;

	return pccbb_intr_establish(sc, irq, level, func, arg, name);
}


/*
 * void *pccbb_cb_intr_disestablish(cardbus_chipset_tag_t ct,
 *					   void *ih)
 *
 *   This function removes an interrupt handler pointed by ih.
 */
void
pccbb_cb_intr_disestablish(cardbus_chipset_tag_t ct, void *ih)
{
	struct pccbb_softc *sc = (struct pccbb_softc *)ct;

	pccbb_intr_disestablish(sc, ih);
}


/*
 * void *pccbb_intr_establish(struct pccbb_softc *sc,
 *				     int irq,
 *				     int level,
 *				     int (* func)(void *),
 *				     void *arg,
 *				     const char *name)
 *
 *   This function registers an interrupt handler at the bridge, in
 *   order not to call the interrupt handlers of child devices when
 *   a card-deletion interrupt occurs.
 *
 *   The arguments irq and level are not used.
 */
void *
pccbb_intr_establish(struct pccbb_softc *sc, int irq, int level,
    int (*func)(void *), void *arg, const char *name)
{
	struct pccbb_intrhand_list *pil, *newpil;
	pcireg_t reg;

	DPRINTF(("pccbb_intr_establish start. %p\n", sc->sc_pil));

	if (sc->sc_pil == NULL) {
		/* initialize bridge intr routing */
		reg = pci_conf_read(sc->sc_pc, sc->sc_tag, PCI_BCR_INTR);
		reg &= ~CB_BCR_INTR_IREQ_ENABLE;
		pci_conf_write(sc->sc_pc, sc->sc_tag, PCI_BCR_INTR, reg);

		switch (sc->sc_chipset) {
		case CB_TI113X:
			reg = pci_conf_read(sc->sc_pc, sc->sc_tag, PCI_CBCTRL);
			/* functional intr enabled */
			reg |= PCI113X_CBCTRL_PCI_INTR;
			pci_conf_write(sc->sc_pc, sc->sc_tag, PCI_CBCTRL, reg);
			break;
		default:
			break;
		}
	}

	/*
	 * Allocate a room for interrupt handler structure.
	 */
	newpil = (struct pccbb_intrhand_list *)
		malloc(sizeof(struct pccbb_intrhand_list), M_DEVBUF, M_WAITOK);

	newpil->pil_func = func;
	newpil->pil_arg = arg;
	newpil->pil_level = level;
	evcount_attach(&newpil->pil_count, name, &sc->sc_intrline);
	newpil->pil_next = NULL;

	if (sc->sc_pil == NULL) {
		sc->sc_pil = newpil;
	} else {
		for (pil = sc->sc_pil; pil->pil_next != NULL;
		    pil = pil->pil_next);
		pil->pil_next = newpil;
	}

	DPRINTF(("pccbb_intr_establish add pil. %p\n", sc->sc_pil));

	return newpil;
}

/*
 * void *pccbb_intr_disestablish(struct pccbb_softc *sc,
 *					void *ih)
 *
 *   This function removes an interrupt handler pointed by ih.
 */
void
pccbb_intr_disestablish(struct pccbb_softc *sc, void *ih)
{
	struct pccbb_intrhand_list *pil, **pil_prev;
	pcireg_t reg;

	DPRINTF(("pccbb_intr_disestablish start. %p\n", sc->sc_pil));

	pil_prev = &sc->sc_pil;

	for (pil = sc->sc_pil; pil != NULL; pil = pil->pil_next) {
		if (pil == ih) {
			evcount_detach(&pil->pil_count);
			*pil_prev = pil->pil_next;
			free(pil, M_DEVBUF, sizeof *pil);
			DPRINTF(("pccbb_intr_disestablish frees one pil\n"));
			break;
		}
		pil_prev = &pil->pil_next;
	}

	if (sc->sc_pil == NULL) {
		/* No interrupt handlers */

		DPRINTF(("pccbb_intr_disestablish: no interrupt handler\n"));

		/* stop routing PCI intr */
		reg = pci_conf_read(sc->sc_pc, sc->sc_tag, PCI_BCR_INTR);
		reg |= CB_BCR_INTR_IREQ_ENABLE;
		pci_conf_write(sc->sc_pc, sc->sc_tag, PCI_BCR_INTR, reg);

		switch (sc->sc_chipset) {
		case CB_TI113X:
			reg = pci_conf_read(sc->sc_pc, sc->sc_tag, PCI_CBCTRL);
			/* functional intr disabled */
			reg &= ~PCI113X_CBCTRL_PCI_INTR;
			pci_conf_write(sc->sc_pc, sc->sc_tag, PCI_CBCTRL, reg);
			break;
		default:
			break;
		}
	}
}

#if defined SHOW_REGS
void
cb_show_regs(pci_chipset_tag_t pc, pcitag_t tag, bus_space_tag_t memt,
    bus_space_handle_t memh)
{
	int i;
	printf("PCI config regs:");
	for (i = 0; i < 0x50; i += 4) {
		if (i % 16 == 0) {
			printf("\n 0x%02x:", i);
		}
		printf(" %08x", pci_conf_read(pc, tag, i));
	}
	for (i = 0x80; i < 0xb0; i += 4) {
		if (i % 16 == 0) {
			printf("\n 0x%02x:", i);
		}
		printf(" %08x", pci_conf_read(pc, tag, i));
	}

	if (memh == 0) {
		printf("\n");
		return;
	}

	printf("\nsocket regs:");
	for (i = 0; i <= 0x10; i += 0x04) {
		printf(" %08x", bus_space_read_4(memt, memh, i));
	}
	printf("\nExCA regs:");
	for (i = 0; i < 0x08; ++i) {
		printf(" %02x", bus_space_read_1(memt, memh, 0x800 + i));
	}
	printf("\n");
	return;
}
#endif

/*
 * int pccbb_pcmcia_io_alloc(pcmcia_chipset_handle_t pch,
 *                                  bus_addr_t start, bus_size_t size,
 *                                  bus_size_t align,
 *                                  struct pcmcia_io_handle *pcihp
 *
 * This function only allocates I/O region for pccard. This function
 * never maps the allocated region to pccard I/O area.
 *
 * XXX: The interface of this function is not very good, I believe.
 */
int
pccbb_pcmcia_io_alloc(pcmcia_chipset_handle_t pch, bus_addr_t start,
    bus_size_t size, bus_size_t align, struct pcmcia_io_handle *pcihp)
{
	struct pcic_handle *ph = (struct pcic_handle *)pch;
	bus_addr_t ioaddr;
	int flags = 0;
	bus_space_tag_t iot;
	bus_space_handle_t ioh;
	bus_addr_t mask;
	rbus_tag_t rb;

	if (align == 0) {
		align = size;	       /* XXX: funny??? */
	}

	if (start != 0) {
		/* XXX: assume all card decode lower 10 bits by its hardware */
		mask = 0x3ff;
		/* enforce to use only masked address */
		start &= mask;
	} else {
		/*
		 * calculate mask:
		 *  1. get the most significant bit of size (call it msb).
		 *  2. compare msb with the value of size.
		 *  3. if size is larger, shift msb left once.
		 *  4. obtain mask value to decrement msb.
		 */
		bus_size_t size_tmp = size;
		int shifts = 0;

		while (size_tmp) {
			++shifts;
			size_tmp >>= 1;
		}
		mask = (1 << shifts);
		if (mask < size) {
			mask <<= 1;
		}
		mask--;
	}

	/*
	 * Allocate some arbitrary I/O space.
	 */

	iot = ((struct pccbb_softc *)(ph->ph_parent))->sc_iot;

	rb = ((struct pccbb_softc *)(ph->ph_parent))->sc_rbus_iot;
	if (rbus_space_alloc(rb, start, size, mask, align, 0, &ioaddr, &ioh)) {
		return 1;
	}

	pcihp->iot = iot;
	pcihp->ioh = ioh;
	pcihp->addr = ioaddr;
	pcihp->size = size;
	pcihp->flags = flags;

	return 0;
}

/*
 * int pccbb_pcmcia_io_free(pcmcia_chipset_handle_t pch,
 *                                 struct pcmcia_io_handle *pcihp)
 *
 * This function only frees I/O region for pccard.
 *
 * XXX: The interface of this function is not very good, I believe.
 */
void
pccbb_pcmcia_io_free(pcmcia_chipset_handle_t pch,
    struct pcmcia_io_handle *pcihp)
{
	bus_space_handle_t ioh = pcihp->ioh;
	bus_size_t size = pcihp->size;

	struct pccbb_softc *sc =
	    (struct pccbb_softc *)((struct pcic_handle *)pch)->ph_parent;
	rbus_tag_t rb = sc->sc_rbus_iot;

	rbus_space_free(rb, ioh, size, NULL);
}

/*
 * int pccbb_pcmcia_io_map(pcmcia_chipset_handle_t pch, int width,
 *                                bus_addr_t offset, bus_size_t size,
 *                                struct pcmcia_io_handle *pcihp,
 *                                int *windowp)
 *
 * This function maps the allocated I/O region to pccard. This function
 * never allocates any I/O region for pccard I/O area.  I don't
 * understand why the original authors of pcmciabus separated alloc and
 * map.  I believe the two must be unite.
 *
 * XXX: no wait timing control?
 */
int
pccbb_pcmcia_io_map(pcmcia_chipset_handle_t pch, int width, bus_addr_t offset,
    bus_size_t size, struct pcmcia_io_handle *pcihp, int *windowp)
{
	struct pcic_handle *ph = (struct pcic_handle *)pch;
	bus_addr_t ioaddr = pcihp->addr + offset;
	int i, win;
#if defined CBB_DEBUG
	static char *width_names[] = { "dynamic", "io8", "io16" };
#endif

	/* Sanity check I/O handle. */

	if (((struct pccbb_softc *)ph->ph_parent)->sc_iot != pcihp->iot) {
		panic("pccbb_pcmcia_io_map iot is bogus");
	}

	/* XXX Sanity check offset/size. */

	win = -1;
	for (i = 0; i < PCIC_IO_WINS; i++) {
		if ((ph->ioalloc & (1 << i)) == 0) {
			win = i;
			ph->ioalloc |= (1 << i);
			break;
		}
	}

	if (win == -1) {
		return 1;
	}

	*windowp = win;

	/* XXX this is pretty gross */

	DPRINTF(("pccbb_pcmcia_io_map window %d %s port %lx+%lx\n",
	    win, width_names[width], (u_long) ioaddr, (u_long) size));

	/* XXX wtf is this doing here? */

#if 0
	printf(" port 0x%lx", (u_long) ioaddr);
	if (size > 1) {
		printf("-0x%lx", (u_long) ioaddr + (u_long) size - 1);
	}
#endif

	ph->io[win].addr = ioaddr;
	ph->io[win].size = size;
	ph->io[win].width = width;

	/* actual dirty register-value changing in the function below. */
	pccbb_pcmcia_do_io_map(ph, win);

	return 0;
}

/*
 * void pccbb_pcmcia_do_io_map(struct pcic_handle *h, int win)
 *
 * This function changes register-value to map I/O region for pccard.
 */
void
pccbb_pcmcia_do_io_map(struct pcic_handle *ph, int win)
{
	static u_int8_t pcic_iowidth[3] = {
		PCIC_IOCTL_IO0_IOCS16SRC_CARD,
		PCIC_IOCTL_IO0_IOCS16SRC_DATASIZE |
		    PCIC_IOCTL_IO0_DATASIZE_8BIT,
		PCIC_IOCTL_IO0_IOCS16SRC_DATASIZE |
		    PCIC_IOCTL_IO0_DATASIZE_16BIT,
	};

#define PCIC_SIA_START_LOW 0
#define PCIC_SIA_START_HIGH 1
#define PCIC_SIA_STOP_LOW 2
#define PCIC_SIA_STOP_HIGH 3

	int regbase_win = 0x8 + win * 0x04;
	u_int8_t ioctl, enable;

	DPRINTF(
	    ("pccbb_pcmcia_do_io_map win %d addr 0x%lx size 0x%lx width %d\n",
	    win, (long)ph->io[win].addr, (long)ph->io[win].size,
	    ph->io[win].width * 8));

	Pcic_write(ph, regbase_win + PCIC_SIA_START_LOW,
	    ph->io[win].addr & 0xff);
	Pcic_write(ph, regbase_win + PCIC_SIA_START_HIGH,
	    (ph->io[win].addr >> 8) & 0xff);

	Pcic_write(ph, regbase_win + PCIC_SIA_STOP_LOW,
	    (ph->io[win].addr + ph->io[win].size - 1) & 0xff);
	Pcic_write(ph, regbase_win + PCIC_SIA_STOP_HIGH,
	    ((ph->io[win].addr + ph->io[win].size - 1) >> 8) & 0xff);

	ioctl = Pcic_read(ph, PCIC_IOCTL);
	enable = Pcic_read(ph, PCIC_ADDRWIN_ENABLE);
	switch (win) {
	case 0:
		ioctl &= ~(PCIC_IOCTL_IO0_WAITSTATE | PCIC_IOCTL_IO0_ZEROWAIT |
		    PCIC_IOCTL_IO0_IOCS16SRC_MASK |
		    PCIC_IOCTL_IO0_DATASIZE_MASK);
		ioctl |= pcic_iowidth[ph->io[win].width];
		enable |= PCIC_ADDRWIN_ENABLE_IO0;
		break;
	case 1:
		ioctl &= ~(PCIC_IOCTL_IO1_WAITSTATE | PCIC_IOCTL_IO1_ZEROWAIT |
		    PCIC_IOCTL_IO1_IOCS16SRC_MASK |
		    PCIC_IOCTL_IO1_DATASIZE_MASK);
		ioctl |= (pcic_iowidth[ph->io[win].width] << 4);
		enable |= PCIC_ADDRWIN_ENABLE_IO1;
		break;
	}
	Pcic_write(ph, PCIC_IOCTL, ioctl);
	Pcic_write(ph, PCIC_ADDRWIN_ENABLE, enable);
#if defined CBB_DEBUG
	{
		u_int8_t start_low =
		    Pcic_read(ph, regbase_win + PCIC_SIA_START_LOW);
		u_int8_t start_high =
		    Pcic_read(ph, regbase_win + PCIC_SIA_START_HIGH);
		u_int8_t stop_low =
		    Pcic_read(ph, regbase_win + PCIC_SIA_STOP_LOW);
		u_int8_t stop_high =
		    Pcic_read(ph, regbase_win + PCIC_SIA_STOP_HIGH);
		printf
		    (" start %02x %02x, stop %02x %02x, ioctl %02x enable %02x\n",
		    start_low, start_high, stop_low, stop_high, ioctl, enable);
	}
#endif
}

/*
 * void pccbb_pcmcia_io_unmap(pcmcia_chipset_handle_t *h, int win)
 *
 * This function unmaps I/O region.  No return value.
 */
void
pccbb_pcmcia_io_unmap(pcmcia_chipset_handle_t pch, int win)
{
	struct pcic_handle *ph = (struct pcic_handle *)pch;
	int reg;

	if (win >= PCIC_IO_WINS || win < 0) {
		panic("pccbb_pcmcia_io_unmap: window out of range");
	}

	reg = Pcic_read(ph, PCIC_ADDRWIN_ENABLE);
	switch (win) {
	case 0:
		reg &= ~PCIC_ADDRWIN_ENABLE_IO0;
		break;
	case 1:
		reg &= ~PCIC_ADDRWIN_ENABLE_IO1;
		break;
	}
	Pcic_write(ph, PCIC_ADDRWIN_ENABLE, reg);

	ph->ioalloc &= ~(1 << win);
}

/*
 * void pccbb_pcmcia_wait_ready(struct pcic_handle *ph)
 *
 * This function enables the card.  All information is stored in
 * the first argument, pcmcia_chipset_handle_t.
 */
void
pccbb_pcmcia_wait_ready(struct pcic_handle *ph)
{
	int i;

	DPRINTF(("pccbb_pcmcia_wait_ready: status 0x%02x\n",
	    Pcic_read(ph, PCIC_IF_STATUS)));

	for (i = 0; i < 10000; i++) {
		if (Pcic_read(ph, PCIC_IF_STATUS) & PCIC_IF_STATUS_READY) {
			return;
		}
		delay(500);
#ifdef CBB_DEBUG
		if ((i > 5000) && (i % 100 == 99))
			printf(".");
#endif
	}

#ifdef DIAGNOSTIC
	printf("pcic_wait_ready: ready never happened, status = %02x\n",
	    Pcic_read(ph, PCIC_IF_STATUS));
#endif
}

/*
 * void pccbb_pcmcia_socket_enable(pcmcia_chipset_handle_t pch)
 *
 * This function enables the card.  All information is stored in
 * the first argument, pcmcia_chipset_handle_t.
 */
void
pccbb_pcmcia_socket_enable(pcmcia_chipset_handle_t pch)
{
	struct pcic_handle *ph = (struct pcic_handle *)pch;
	struct pccbb_softc *sc = (struct pccbb_softc *)ph->ph_parent;
	int cardtype, win;
	u_int8_t power, intr;
	pcireg_t spsr;
	int voltage;

	/* this bit is mostly stolen from pcic_attach_card */

	DPRINTF(("pccbb_pcmcia_socket_enable: "));

	/* get card Vcc info */

	spsr =
	    bus_space_read_4(sc->sc_base_memt, sc->sc_base_memh,
	    CB_SOCKET_STAT);
	if (spsr & CB_SOCKET_STAT_5VCARD) {
		DPRINTF(("5V card\n"));
		voltage = CARDBUS_VCC_5V | CARDBUS_VPP_VCC;
	} else if (spsr & CB_SOCKET_STAT_3VCARD) {
		DPRINTF(("3V card\n"));
		voltage = CARDBUS_VCC_3V | CARDBUS_VPP_VCC;
	} else {
		DPRINTF(("?V card, 0x%x\n", spsr));	/* XXX */
		return;
	}

	/* disable socket i/o: negate output enable bit */

	power = 0;
	Pcic_write(ph, PCIC_PWRCTL, power);

	/* power down the socket to reset it, clear the card reset pin */

	pccbb_power((cardbus_chipset_tag_t)sc, CARDBUS_VCC_0V | CARDBUS_VPP_0V);

	/*
	 * wait 200ms until power fails (Tpf).  Then, wait 100ms since
	 * we are changing Vcc (Toff).
	 */
	/* delay(300*1000); too much */

	/* assert reset bit */
	intr = Pcic_read(ph, PCIC_INTR);
	intr &= ~(PCIC_INTR_RESET | PCIC_INTR_CARDTYPE_MASK);
	Pcic_write(ph, PCIC_INTR, intr);

	/* Power up the socket. */
	power = Pcic_read(ph, PCIC_PWRCTL);
	Pcic_write(ph, PCIC_PWRCTL, (power & ~PCIC_PWRCTL_OE));
	pccbb_power((cardbus_chipset_tag_t)sc, voltage);

	/* Now output enable */
	power = Pcic_read(ph, PCIC_PWRCTL);
	Pcic_write(ph, PCIC_PWRCTL, power | PCIC_PWRCTL_OE);

	/*
	 * hold RESET at least 10us.
	 */
	delay(10);
	delay(2 * 1000);	       /* XXX: TI1130 requires it. */
	delay(20 * 1000);	       /* XXX: TI1130 requires it. */

	/* clear the reset flag */

	intr |= PCIC_INTR_RESET;
	Pcic_write(ph, PCIC_INTR, intr);

	/* wait 20ms as per pc card standard (r2.01) section 4.3.6 */

	delay(20000);

	/* wait for the chip to finish initializing */

	pccbb_pcmcia_wait_ready(ph);

	/* zero out the address windows */

	Pcic_write(ph, PCIC_ADDRWIN_ENABLE, 0);

	/* set the card type */

	cardtype = pcmcia_card_gettype(ph->pcmcia);

	intr |= ((cardtype == PCMCIA_IFTYPE_IO) ?
	    PCIC_INTR_CARDTYPE_IO : PCIC_INTR_CARDTYPE_MEM);
	Pcic_write(ph, PCIC_INTR, intr);

	DPRINTF(("%s: pccbb_pcmcia_socket_enable %02x cardtype %s %02x\n",
	    ph->ph_parent->dv_xname, ph->sock,
	    ((cardtype == PCMCIA_IFTYPE_IO) ? "io" : "mem"), intr));

	/* reinstall all the memory and io mappings */

	for (win = 0; win < PCIC_MEM_WINS; ++win) {
		if (ph->memalloc & (1 << win)) {
			pccbb_pcmcia_do_mem_map(ph, win);
		}
	}

	for (win = 0; win < PCIC_IO_WINS; ++win) {
		if (ph->ioalloc & (1 << win)) {
			pccbb_pcmcia_do_io_map(ph, win);
		}
	}
}

/*
 * void pccbb_pcmcia_socket_disable(pcmcia_chipset_handle_t *ph)
 *
 * This function disables the card.  All information is stored in
 * the first argument, pcmcia_chipset_handle_t.
 */
void
pccbb_pcmcia_socket_disable(pcmcia_chipset_handle_t pch)
{
	struct pcic_handle *ph = (struct pcic_handle *)pch;
	struct pccbb_softc *sc = (struct pccbb_softc *)ph->ph_parent;
	u_int8_t power, intr;

	DPRINTF(("pccbb_pcmcia_socket_disable\n"));

	/* reset signal asserting... */

	intr = Pcic_read(ph, PCIC_INTR);
	intr &= ~(PCIC_INTR_CARDTYPE_MASK);
	Pcic_write(ph, PCIC_INTR, intr);
	delay(2 * 1000);

	/* power down the socket */
	power = Pcic_read(ph, PCIC_PWRCTL);
	power &= ~PCIC_PWRCTL_OE;
	Pcic_write(ph, PCIC_PWRCTL, power);
	pccbb_power((cardbus_chipset_tag_t)sc, CARDBUS_VCC_0V | CARDBUS_VPP_0V);
	/*
	 * wait 300ms until power fails (Tpf).
	 */
	delay(300 * 1000);
}

/*
 * int pccbb_pcmcia_card_detect(pcmcia_chipset_handle_t *ph)
 *
 * This function detects whether a card is in the slot or not.
 * If a card is inserted, return 1.  Otherwise, return 0.
 */
int
pccbb_pcmcia_card_detect(pcmcia_chipset_handle_t pch)
{
	struct pcic_handle *ph = (struct pcic_handle *)pch;
	struct pccbb_softc *sc = (struct pccbb_softc *)ph->ph_parent;

	DPRINTF(("pccbb_pcmcia_card_detect\n"));
	return pccbb_detect_card(sc) == 1 ? 1 : 0;
}

/*
 * int pccbb_pcmcia_mem_alloc(pcmcia_chipset_handle_t pch,
 *                                   bus_size_t size,
 *                                   struct pcmcia_mem_handle *pcmhp)
 *
 * This function only allocates memory region for pccard. This
 * function never maps the allocated region to pccard memory area.
 *
 * XXX: Why the argument of start address is not in?
 */
int
pccbb_pcmcia_mem_alloc(pcmcia_chipset_handle_t pch, bus_size_t size,
    struct pcmcia_mem_handle *pcmhp)
{
	struct pcic_handle *ph = (struct pcic_handle *)pch;
	bus_space_handle_t memh;
	bus_addr_t addr;
	bus_size_t sizepg;
	struct pccbb_softc *sc = (struct pccbb_softc *)ph->ph_parent;
	rbus_tag_t rb;

	/* out of sc->memh, allocate as many pages as necessary */

	/* convert size to PCIC pages */
	/*
	 * This is not enough; when the requested region is on the page
	 * boundaries, this may calculate wrong result.
	 */
	sizepg = (size + (PCIC_MEM_PAGESIZE - 1)) / PCIC_MEM_PAGESIZE;
#if 0
	if (sizepg > PCIC_MAX_MEM_PAGES) {
		return 1;
	}
#endif

	if (!(sc->sc_pcmcia_flags & PCCBB_PCMCIA_MEM_32)) {
		return 1;
	}

	addr = 0;		       /* XXX gcc -Wuninitialized */

	rb = sc->sc_rbus_memt;
	if (rbus_space_alloc(rb, 0, sizepg * PCIC_MEM_PAGESIZE,
	    sizepg * PCIC_MEM_PAGESIZE - 1, PCIC_MEM_PAGESIZE, 0,
	    &addr, &memh)) {
		return 1;
	}

	DPRINTF(
	    ("pccbb_pcmcia_alloc_mem: addr 0x%lx size 0x%lx, realsize 0x%lx\n",
	    addr, size, sizepg * PCIC_MEM_PAGESIZE));

	pcmhp->memt = sc->sc_memt;
	pcmhp->memh = memh;
	pcmhp->addr = addr;
	pcmhp->size = size;
	pcmhp->realsize = sizepg * PCIC_MEM_PAGESIZE;
	/* What is mhandle?  I feel it is very dirty and it must go trush. */
	pcmhp->mhandle = 0;
	/* No offset???  Funny. */

	return 0;
}

/*
 * void pccbb_pcmcia_mem_free(pcmcia_chipset_handle_t pch,
 *                                   struct pcmcia_mem_handle *pcmhp)
 *
 * This function release the memory space allocated by the function
 * pccbb_pcmcia_mem_alloc().
 */
void
pccbb_pcmcia_mem_free(pcmcia_chipset_handle_t pch,
    struct pcmcia_mem_handle *pcmhp)
{
	struct pcic_handle *ph = (struct pcic_handle *)pch;
	struct pccbb_softc *sc = (struct pccbb_softc *)ph->ph_parent;

	rbus_space_free(sc->sc_rbus_memt, pcmhp->memh, pcmhp->realsize, NULL);
}

/*
 * void pccbb_pcmcia_do_mem_map(struct pcic_handle *ph, int win)
 *
 * This function release the memory space allocated by the function
 * pccbb_pcmcia_mem_alloc().
 */
void
pccbb_pcmcia_do_mem_map(struct pcic_handle *ph, int win)
{
	int regbase_win;
	bus_addr_t phys_addr;
	bus_addr_t phys_end;

#define PCIC_SMM_START_LOW 0
#define PCIC_SMM_START_HIGH 1
#define PCIC_SMM_STOP_LOW 2
#define PCIC_SMM_STOP_HIGH 3
#define PCIC_CMA_LOW 4
#define PCIC_CMA_HIGH 5

	u_int8_t start_low, start_high = 0;
	u_int8_t stop_low, stop_high;
	u_int8_t off_low, off_high;
	u_int8_t mem_window;
	int reg;

	regbase_win = 0x10 + win * 0x08;

	phys_addr = ph->mem[win].addr;
	phys_end = phys_addr + ph->mem[win].size;

	DPRINTF(("pccbb_pcmcia_do_mem_map: start 0x%lx end 0x%lx off 0x%lx\n",
	    phys_addr, phys_end, ph->mem[win].offset));

#define PCIC_MEMREG_LSB_SHIFT PCIC_SYSMEM_ADDRX_SHIFT
#define PCIC_MEMREG_MSB_SHIFT (PCIC_SYSMEM_ADDRX_SHIFT + 8)
#define PCIC_MEMREG_WIN_SHIFT (PCIC_SYSMEM_ADDRX_SHIFT + 12)

	/* bit 19:12 */
	start_low = (phys_addr >> PCIC_MEMREG_LSB_SHIFT) & 0xff;
	/* bit 23:20 and bit 7 on */
	start_high = ((phys_addr >> PCIC_MEMREG_MSB_SHIFT) & 0x0f)
	    | PCIC_SYSMEM_ADDRX_START_MSB_DATASIZE_16BIT; /* bit 7 on */
	/* bit 31:24, for 32-bit address */
	mem_window = (phys_addr >> PCIC_MEMREG_WIN_SHIFT) & 0xff;

	Pcic_write(ph, regbase_win + PCIC_SMM_START_LOW, start_low);
	Pcic_write(ph, regbase_win + PCIC_SMM_START_HIGH, start_high);

	if (((struct pccbb_softc *)ph->
	    ph_parent)->sc_pcmcia_flags & PCCBB_PCMCIA_MEM_32) {
		Pcic_write(ph, 0x40 + win, mem_window);
	}

	stop_low = (phys_end >> PCIC_MEMREG_LSB_SHIFT) & 0xff;
	stop_high = ((phys_end >> PCIC_MEMREG_MSB_SHIFT) & 0x0f)
	    | PCIC_SYSMEM_ADDRX_STOP_MSB_WAIT2;	/* wait 2 cycles */
	/* XXX Geee, WAIT2!! Crazy!!  I must rewrite this routine. */

	Pcic_write(ph, regbase_win + PCIC_SMM_STOP_LOW, stop_low);
	Pcic_write(ph, regbase_win + PCIC_SMM_STOP_HIGH, stop_high);

	off_low = (ph->mem[win].offset >> PCIC_CARDMEM_ADDRX_SHIFT) & 0xff;
	off_high = ((ph->mem[win].offset >> (PCIC_CARDMEM_ADDRX_SHIFT + 8))
	    & PCIC_CARDMEM_ADDRX_MSB_ADDR_MASK)
	    | ((ph->mem[win].kind == PCMCIA_MEM_ATTR) ?
	    PCIC_CARDMEM_ADDRX_MSB_REGACTIVE_ATTR : 0);

	Pcic_write(ph, regbase_win + PCIC_CMA_LOW, off_low);
	Pcic_write(ph, regbase_win + PCIC_CMA_HIGH, off_high);

	reg = Pcic_read(ph, PCIC_ADDRWIN_ENABLE);
	reg |= ((1 << win) | PCIC_ADDRWIN_ENABLE_MEMCS16);
	Pcic_write(ph, PCIC_ADDRWIN_ENABLE, reg);

#if defined CBB_DEBUG
	{
		int r1, r2, r3, r4, r5, r6, r7 = 0;

		r1 = Pcic_read(ph, regbase_win + PCIC_SMM_START_LOW);
		r2 = Pcic_read(ph, regbase_win + PCIC_SMM_START_HIGH);
		r3 = Pcic_read(ph, regbase_win + PCIC_SMM_STOP_LOW);
		r4 = Pcic_read(ph, regbase_win + PCIC_SMM_STOP_HIGH);
		r5 = Pcic_read(ph, regbase_win + PCIC_CMA_LOW);
		r6 = Pcic_read(ph, regbase_win + PCIC_CMA_HIGH);
		if (((struct pccbb_softc *)(ph->
		    ph_parent))->sc_pcmcia_flags & PCCBB_PCMCIA_MEM_32) {
			r7 = Pcic_read(ph, 0x40 + win);
		}

		DPRINTF(("pccbb_pcmcia_do_mem_map window %d: %02x%02x %02x%02x "
		    "%02x%02x", win, r1, r2, r3, r4, r5, r6));
		if (((struct pccbb_softc *)(ph->
		    ph_parent))->sc_pcmcia_flags & PCCBB_PCMCIA_MEM_32) {
			DPRINTF((" %02x", r7));
		}
		DPRINTF(("\n"));
	}
#endif
}

/*
 * int pccbb_pcmcia_mem_map(pcmcia_chipset_handle_t pch, int kind,
 *                                 bus_addr_t card_addr, bus_size_t size,
 *                                 struct pcmcia_mem_handle *pcmhp,
 *                                 bus_size_t *offsetp, int *windowp)
 *
 * This function maps memory space allocated by the function
 * pccbb_pcmcia_mem_alloc().
 */
int
pccbb_pcmcia_mem_map(pcmcia_chipset_handle_t pch, int kind,
    bus_addr_t card_addr, bus_size_t size, struct pcmcia_mem_handle *pcmhp,
    bus_size_t *offsetp, int *windowp)
{
	struct pcic_handle *ph = (struct pcic_handle *)pch;
	bus_addr_t busaddr;
	long card_offset;
	int win;

	for (win = 0; win < PCIC_MEM_WINS; ++win) {
		if ((ph->memalloc & (1 << win)) == 0) {
			ph->memalloc |= (1 << win);
			break;
		}
	}

	if (win == PCIC_MEM_WINS) {
		return 1;
	}

	*windowp = win;

	/* XXX this is pretty gross */

	if (((struct pccbb_softc *)ph->ph_parent)->sc_memt != pcmhp->memt) {
		panic("pccbb_pcmcia_mem_map memt is bogus");
	}

	busaddr = pcmhp->addr;

	/*
	 * compute the address offset to the pcmcia address space for the
	 * pcic.  this is intentionally signed.  The masks and shifts below
	 * will cause TRT to happen in the pcic registers.  Deal with making
	 * sure the address is aligned, and return the alignment offset.
	 */

	*offsetp = card_addr % PCIC_MEM_PAGESIZE;
	card_addr -= *offsetp;

	DPRINTF(("pccbb_pcmcia_mem_map window %d bus %lx+%lx+%lx at card addr "
	    "%lx\n", win, (u_long) busaddr, (u_long) * offsetp, (u_long) size,
	    (u_long) card_addr));

	/*
	 * include the offset in the size, and decrement size by one, since
	 * the hw wants start/stop
	 */
	size += *offsetp - 1;

	card_offset = (((long)card_addr) - ((long)busaddr));

	ph->mem[win].addr = busaddr;
	ph->mem[win].size = size;
	ph->mem[win].offset = card_offset;
	ph->mem[win].kind = kind;

	pccbb_pcmcia_do_mem_map(ph, win);

	return 0;
}

/*
 * int pccbb_pcmcia_mem_unmap(pcmcia_chipset_handle_t pch,
 *                                   int window)
 *
 * This function unmaps memory space which mapped by the function
 * pccbb_pcmcia_mem_map().
 */
void
pccbb_pcmcia_mem_unmap(pcmcia_chipset_handle_t pch, int window)
{
	struct pcic_handle *ph = (struct pcic_handle *)pch;
	int reg;

	if (window >= PCIC_MEM_WINS) {
		panic("pccbb_pcmcia_mem_unmap: window out of range");
	}

	reg = Pcic_read(ph, PCIC_ADDRWIN_ENABLE);
	reg &= ~(1 << window);
	Pcic_write(ph, PCIC_ADDRWIN_ENABLE, reg);

	ph->memalloc &= ~(1 << window);
}

#if defined PCCBB_PCMCIA_POLL
struct pccbb_poll_str {
	void *arg;
	int (*func)(void *);
	int level;
	struct pcic_handle *ph;
	int count;
	int num;
};

static struct pccbb_poll_str pccbb_poll[10];
static int pccbb_poll_n = 0;
static struct timeout pccbb_poll_timeout;

void pccbb_pcmcia_poll(void *arg);

void
pccbb_pcmcia_poll(void *arg)
{
	struct pccbb_poll_str *poll = arg;
	struct pcic_handle *ph = poll->ph;
	struct pccbb_softc *sc = ph->sc;
	int s;
	u_int32_t spsr;		       /* socket present-state reg */

	timeout_set(&pccbb_poll_timeout, pccbb_pcmcia_poll, arg);
	timeout_add_sec(&pccbb_poll_timeout, 2);
	switch (poll->level) {
	case IPL_NET:
		s = splnet();
		break;
	case IPL_BIO:
		s = splbio();
		break;
	case IPL_TTY:		       /* fallthrough */
	default:
		s = spltty();
		break;
	}

	spsr =
	    bus_space_read_4(sc->sc_base_memt, sc->sc_base_memh,
	    CB_SOCKET_STAT);

#if defined PCCBB_PCMCIA_POLL_ONLY && defined LEVEL2
	if (!(spsr & 0x40))	       /* CINT low */
#else
	if (1)
#endif
	{
		if ((*poll->func) (poll->arg) > 0) {
			++poll->count;
	/* printf("intr: reported from poller, 0x%x\n", spsr); */
#if defined LEVEL2
		} else {
			printf("intr: miss! 0x%x\n", spsr);
#endif
		}
	}
	splx(s);
}
#endif /* defined CB_PCMCIA_POLL */

/*
 * void *pccbb_pcmcia_intr_establish(pcmcia_chipset_handle_t pch,
 *                                          struct pcmcia_function *pf,
 *                                          int ipl,
 *                                          int (*func)(void *),
 *                                          void *arg);
 *
 * This function enables PC-Card interrupt.  PCCBB uses PCI interrupt line.
 */
void *
pccbb_pcmcia_intr_establish(pcmcia_chipset_handle_t pch,
    struct pcmcia_function *pf, int ipl, int (*func)(void *), void *arg,
    char *xname)
{
	struct pcic_handle *ph = (struct pcic_handle *)pch;
	struct pccbb_softc *sc = (struct pccbb_softc *)ph->ph_parent;

	if (!(pf->cfe->flags & PCMCIA_CFE_IRQLEVEL)) {
		/* what should I do? */
		if ((pf->cfe->flags & PCMCIA_CFE_IRQLEVEL)) {
			DPRINTF(
			    ("%s does not provide edge nor pulse interrupt\n",
			    sc->sc_dev.dv_xname));
			return NULL;
		}
		/*
		 * XXX Noooooo!  The interrupt flag must set properly!!
		 * dumb pcmcia driver!!
		 */
	}

	return pccbb_intr_establish(sc, -1, ipl, func, arg, xname);
}

/*
 * void pccbb_pcmcia_intr_disestablish(pcmcia_chipset_handle_t pch,
 *                                            void *ih)
 *
 * This function disables PC-Card interrupt.
 */
void
pccbb_pcmcia_intr_disestablish(pcmcia_chipset_handle_t pch, void *ih)
{
	struct pcic_handle *ph = (struct pcic_handle *)pch;
	struct pccbb_softc *sc = (struct pccbb_softc *)ph->ph_parent;

	pccbb_intr_disestablish(sc, ih);
}

const char *
pccbb_pcmcia_intr_string(pcmcia_chipset_handle_t pch, void *ih)
{
	if (ih == NULL)
		return "couldn't establish interrupt";
	else
		return "";	/* card shares interrupt of the bridge */
}

/*
 * int
 * pccbb_rbus_cb_space_alloc(cardbus_chipset_tag_t ct, rbus_tag_t rb,
 *			    bus_addr_t addr, bus_size_t size,
 *			    bus_addr_t mask, bus_size_t align,
 *			    int flags, bus_addr_t *addrp;
 *			    bus_space_handle_t *bshp)
 *
 *   This function allocates a portion of memory or io space for
 *   clients.  This function is called from CardBus card drivers.
 */
int
pccbb_rbus_cb_space_alloc(cardbus_chipset_tag_t ct, rbus_tag_t rb,
    bus_addr_t addr, bus_size_t size, bus_addr_t mask, bus_size_t align,
    int flags, bus_addr_t *addrp, bus_space_handle_t *bshp)
{
	struct pccbb_softc *sc = (struct pccbb_softc *)ct;

	DPRINTF(
	    ("pccbb_rbus_cb_space_alloc: adr %lx, size %lx, mask %lx, align %lx\n",
	    addr, size, mask, align));

	align = max(align, 4);
	mask = max(mask, (4 - 1));
	if (rb->rb_bt == sc->sc_memt) {
		align = max(align, 0x1000);
		mask = max(mask, (0x1000 - 1));
	}

	if (rb->rb_bt == sc->sc_iot) {
		/* XXX: hack for avoiding ISA image */
		if (mask < 0x0100) {
			mask = 0x3ff;
			addr = 0x300;
		}
	}

	if (rbus_space_alloc(rb, addr, size, mask, align, flags, addrp, bshp)) {
		printf("%s: <rbus> no bus space\n", sc->sc_dev.dv_xname);
		return 1;
	}

	pccbb_open_win(sc, rb->rb_bt, *addrp, size, *bshp, 0);

	return 0;
}

/*
 * int
 * pccbb_rbus_cb_space_free(cardbus_chipset_tag_t *ct, rbus_tag_t rb,
 *			   bus_space_handle_t *bshp, bus_size_t size);
 *
 *   This function is called from CardBus card drivers.
 */
int
pccbb_rbus_cb_space_free(cardbus_chipset_tag_t ct, rbus_tag_t rb,
    bus_space_handle_t bsh, bus_size_t size)
{
	struct pccbb_softc *sc = (struct pccbb_softc *)ct;
	bus_space_tag_t bt = rb->rb_bt;

	pccbb_close_win(sc, bt, bsh, size);

	if (bt == sc->sc_memt) {
	} else if (bt == sc->sc_iot) {
	} else {
		return 1;
		/* XXX: panic here? */
	}

	return rbus_space_free(rb, bsh, size, NULL);
}

int
pccbb_open_win(struct pccbb_softc *sc, bus_space_tag_t bst, bus_addr_t addr,
    bus_size_t size, bus_space_handle_t bsh, int flags)
{
	struct pccbb_win_chain_head *head;
	bus_addr_t align;

	head = &sc->sc_iowindow;
	align = 0x04;
	if (sc->sc_memt == bst) {
		head = &sc->sc_memwindow;
		align = 0x1000;
		DPRINTF(("using memory window, %x %x %x\n\n",
		    sc->sc_iot, sc->sc_memt, bst));
	}

	if (pccbb_winlist_insert(head, addr, size, bsh, flags)) {
		printf("%s: pccbb_open_win: %s winlist insert failed\n",
		    sc->sc_dev.dv_xname,
		    (head == &sc->sc_memwindow) ? "mem" : "io");
	}
	pccbb_winset(align, sc, bst);

	return 0;
}

int
pccbb_close_win(struct pccbb_softc *sc, bus_space_tag_t bst,
    bus_space_handle_t bsh, bus_size_t size)
{
	struct pccbb_win_chain_head *head;
	bus_addr_t align;

	head = &sc->sc_iowindow;
	align = 0x04;
	if (sc->sc_memt == bst) {
		head = &sc->sc_memwindow;
		align = 0x1000;
	}

	if (pccbb_winlist_delete(head, bsh, size)) {
		printf("%s: pccbb_close_win: %s winlist delete failed\n",
		    sc->sc_dev.dv_xname,
		    (head == &sc->sc_memwindow) ? "mem" : "io");
	}
	pccbb_winset(align, sc, bst);

	return 0;
}

int
pccbb_winlist_insert(struct pccbb_win_chain_head *head, bus_addr_t start,
    bus_size_t size, bus_space_handle_t bsh, int flags)
{
	struct pccbb_win_chain *chainp, *elem;

	if ((elem = malloc(sizeof(struct pccbb_win_chain), M_DEVBUF,
	    M_NOWAIT)) == NULL)
		return (1);		/* fail */

	elem->wc_start = start;
	elem->wc_end = start + (size - 1);
	elem->wc_handle = bsh;
	elem->wc_flags = flags;

	for (chainp = TAILQ_FIRST(head); chainp != NULL;
	    chainp = TAILQ_NEXT(chainp, wc_list)) {
		if (chainp->wc_end < start)
			continue;
		TAILQ_INSERT_AFTER(head, chainp, elem, wc_list);
		return (0);
	}

	TAILQ_INSERT_TAIL(head, elem, wc_list);
	return (0);
}

int
pccbb_winlist_delete(struct pccbb_win_chain_head *head, bus_space_handle_t bsh,
    bus_size_t size)
{
	struct pccbb_win_chain *chainp;

	for (chainp = TAILQ_FIRST(head); chainp != NULL;
	     chainp = TAILQ_NEXT(chainp, wc_list)) {
		if (memcmp(&chainp->wc_handle, &bsh, sizeof(bsh)))
			continue;
		if ((chainp->wc_end - chainp->wc_start) != (size - 1)) {
			printf("pccbb_winlist_delete: window 0x%lx size "
			    "inconsistent: 0x%lx, 0x%lx\n",
			    chainp->wc_start,
			    chainp->wc_end - chainp->wc_start,
			    size - 1);
			return 1;
		}

		TAILQ_REMOVE(head, chainp, wc_list);
		free(chainp, M_DEVBUF, sizeof *chainp);

		return 0;
	}

	return 1;	       /* fail: no candidate to remove */
}

void
pccbb_winset(bus_addr_t align, struct pccbb_softc *sc, bus_space_tag_t bst)
{
	pci_chipset_tag_t pc;
	pcitag_t tag;
	bus_addr_t mask = ~(align - 1);
	struct {
		pcireg_t win_start;
		pcireg_t win_limit;
		int win_flags;
	} win[2];
	struct pccbb_win_chain *chainp;
	int offs;

	win[0].win_start = win[1].win_start = 0xffffffff;
	win[0].win_limit = win[1].win_limit = 0;
	win[0].win_flags = win[1].win_flags = 0;

	chainp = TAILQ_FIRST(&sc->sc_iowindow);
	offs = PCI_CB_IOBASE0;
	if (sc->sc_memt == bst) {
		chainp = TAILQ_FIRST(&sc->sc_memwindow);
		offs = PCI_CB_MEMBASE0;
	}

	if (chainp != NULL) {
		win[0].win_start = chainp->wc_start & mask;
		win[0].win_limit = chainp->wc_end & mask;
		win[0].win_flags = chainp->wc_flags;
		chainp = TAILQ_NEXT(chainp, wc_list);
	}

	for (; chainp != NULL; chainp = TAILQ_NEXT(chainp, wc_list)) {
		if (win[1].win_start == 0xffffffff) {
			/* window 1 is not used */
			if ((win[0].win_flags == chainp->wc_flags) &&
			    (win[0].win_limit + align >=
			    (chainp->wc_start & mask))) {
				/* concatenate */
				win[0].win_limit = chainp->wc_end & mask;
			} else {
				/* make new window */
				win[1].win_start = chainp->wc_start & mask;
				win[1].win_limit = chainp->wc_end & mask;
				win[1].win_flags = chainp->wc_flags;
			}
			continue;
		}

		/* Both windows are engaged. */
		if (win[0].win_flags == win[1].win_flags) {
			/* same flags */
			if (win[0].win_flags == chainp->wc_flags) {
				if (win[1].win_start - (win[0].win_limit +
				    align) <
				    (chainp->wc_start & mask) -
				    ((chainp->wc_end & mask) + align)) {
					/*
					 * merge window 0 and 1, and set win1
					 * to chainp
					 */
					win[0].win_limit = win[1].win_limit;
					win[1].win_start =
					    chainp->wc_start & mask;
					win[1].win_limit =
					    chainp->wc_end & mask;
				} else {
					win[1].win_limit =
					    chainp->wc_end & mask;
				}
			} else {
				/* different flags */

				/* concatenate win0 and win1 */
				win[0].win_limit = win[1].win_limit;
				/* allocate win[1] to new space */
				win[1].win_start = chainp->wc_start & mask;
				win[1].win_limit = chainp->wc_end & mask;
				win[1].win_flags = chainp->wc_flags;
			}
		} else {
			/* the flags of win[0] and win[1] are different */
			if (win[0].win_flags == chainp->wc_flags) {
				win[0].win_limit = chainp->wc_end & mask;
				/*
				 * XXX this creates overlapping windows, so
				 * what should the poor bridge do if one is
				 * cachable, and the other is not?
				 */
				printf("%s: overlapping windows\n",
				    sc->sc_dev.dv_xname);
			} else {
				win[1].win_limit = chainp->wc_end & mask;
			}
		}
	}

	pc = sc->sc_pc;
	tag = sc->sc_tag;
	pci_conf_write(pc, tag, offs, win[0].win_start);
	pci_conf_write(pc, tag, offs + 4, win[0].win_limit);
	pci_conf_write(pc, tag, offs + 8, win[1].win_start);
	pci_conf_write(pc, tag, offs + 12, win[1].win_limit);
	DPRINTF(("--pccbb_winset: win0 [%x, %lx), win1 [%x, %lx)\n",
	    pci_conf_read(pc, tag, offs),
	    pci_conf_read(pc, tag, offs + 4) + align,
	    pci_conf_read(pc, tag, offs + 8),
	    pci_conf_read(pc, tag, offs + 12) + align));

	if (bst == sc->sc_memt) {
		pcireg_t bcr = pci_conf_read(pc, tag, PCI_BCR_INTR);

		bcr &= ~(CB_BCR_PREFETCH_MEMWIN0 | CB_BCR_PREFETCH_MEMWIN1);
		if (win[0].win_flags & PCCBB_MEM_CACHABLE)
			bcr |= CB_BCR_PREFETCH_MEMWIN0;
		if (win[1].win_flags & PCCBB_MEM_CACHABLE)
			bcr |= CB_BCR_PREFETCH_MEMWIN1;
		pci_conf_write(pc, tag, PCI_BCR_INTR, bcr);
	}
}

int
pccbbactivate(struct device *self, int act)
{
	struct pccbb_softc *sc = (struct pccbb_softc *)self;
	pci_chipset_tag_t pc = sc->sc_pc;
	pcitag_t tag = sc->sc_tag;
	pcireg_t csr;
	u_int32_t reg;
	bus_space_tag_t base_memt = sc->sc_base_memt;	/* socket regs memory */
	bus_space_handle_t base_memh = sc->sc_base_memh;
	int rv = 0;

	switch (act) {
	case DVACT_SUSPEND:
		rv = config_activate_children(self, act);

		sc->sc_pil_intr_enable = 0;

		/* Save registers that may get lost. */
		sc->sc_csr = pci_conf_read(pc, tag, PCI_COMMAND_STATUS_REG);
		sc->sc_bhlcr = pci_conf_read(pc, tag, PCI_BHLC_REG);
		sc->sc_int = pci_conf_read(pc, tag, PCI_INTERRUPT_REG);

		sc->sc_sockbase = pci_conf_read(pc, tag, PCI_SOCKBASE);
		sc->sc_busnum = pci_conf_read(pc, tag, PCI_BUSNUM);

		sc->sc_membase[0] = pci_conf_read(pc, tag, PCI_CB_MEMBASE0);
		sc->sc_memlimit[0] = pci_conf_read(pc, tag, PCI_CB_MEMLIMIT0);
		sc->sc_membase[1] = pci_conf_read(pc, tag, PCI_CB_MEMBASE1);
		sc->sc_memlimit[1] = pci_conf_read(pc, tag, PCI_CB_MEMLIMIT1);
		sc->sc_iobase[0] = pci_conf_read(pc, tag, PCI_CB_IOBASE0);
		sc->sc_iolimit[0] = pci_conf_read(pc, tag, PCI_CB_IOLIMIT0);
		sc->sc_iobase[1] = pci_conf_read(pc, tag, PCI_CB_IOBASE1);
		sc->sc_iolimit[1] = pci_conf_read(pc, tag, PCI_CB_IOLIMIT1);
		break;
	case DVACT_RESUME:
		/* Restore the registers saved above. */
		pci_conf_write(pc, tag, PCI_BHLC_REG, sc->sc_bhlcr);
		pci_conf_write(pc, tag, PCI_INTERRUPT_REG, sc->sc_int);

		pci_conf_write(pc, tag, PCI_SOCKBASE, sc->sc_sockbase);
		pci_conf_write(pc, tag, PCI_BUSNUM, sc->sc_busnum);

		pci_conf_write(pc, tag, PCI_CB_MEMBASE0, sc->sc_membase[0]);
		pci_conf_write(pc, tag, PCI_CB_MEMLIMIT0, sc->sc_memlimit[0]);
		pci_conf_write(pc, tag, PCI_CB_MEMBASE1, sc->sc_membase[1]);
		pci_conf_write(pc, tag, PCI_CB_MEMLIMIT1, sc->sc_memlimit[1]);
		pci_conf_write(pc, tag, PCI_CB_IOBASE0, sc->sc_iobase[0]);
		pci_conf_write(pc, tag, PCI_CB_IOLIMIT0, sc->sc_iolimit[0]);
		pci_conf_write(pc, tag, PCI_CB_IOBASE1, sc->sc_iobase[1]);
		pci_conf_write(pc, tag, PCI_CB_IOLIMIT1, sc->sc_iolimit[1]);

		/* Disable legacy register mapping. */
		pccbb_legacy_disable(sc);

		/*
		 * Restore command register last to avoid exposing
		 * uninitialised windows.
		 */
		csr = pci_conf_read(pc, tag, PCI_COMMAND_STATUS_REG);
		pci_conf_write(pc, tag, PCI_COMMAND_STATUS_REG,
		    (csr & 0xffff0000) | (sc->sc_csr & 0x0000ffff));

		/* CSC Interrupt: Card detect interrupt on */
		reg = bus_space_read_4(base_memt, base_memh, CB_SOCKET_MASK);
		/* Card detect intr is turned on. */
		reg |= CB_SOCKET_MASK_CD;
		bus_space_write_4(base_memt, base_memh, CB_SOCKET_MASK, reg);

		/* reset interrupt */
		reg = bus_space_read_4(base_memt, base_memh, CB_SOCKET_EVENT);
		bus_space_write_4(base_memt, base_memh, CB_SOCKET_EVENT, reg);

		/* re-check all cards */
		pccbb_checksockstat(sc);

		/*
		 * XXX Because the cardslot stuff is so obfuscated with threads,
		 * here we are activating children which may have been
		 * ejected while we were asleep.  This needs to be solved.
		 */
		rv = config_activate_children(self, act);

		sc->sc_pil_intr_enable = 1;
		break;
	case DVACT_POWERDOWN:
		rv = config_activate_children(self, act);
		pccbb_shutdown(self);
		break;
	default:
		rv = config_activate_children(self, act);
		break;
	}
	return (rv);
}
