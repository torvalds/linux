/*	$OpenBSD: cardbus.c,v 1.54 2024/05/24 06:26:47 jsg Exp $	*/
/*	$NetBSD: cardbus.c,v 1.24 2000/04/02 19:11:37 mycroft Exp $	*/

/*
 * Copyright (c) 1997, 1998, 1999 and 2000
 *     HAYAKAWA Koichi.  All rights reserved.
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/malloc.h>

#include <machine/bus.h>

#include <dev/cardbus/cardbusvar.h>
#include <dev/pci/pcidevs.h>

#include <dev/cardbus/cardbus_exrom.h>

#include <dev/pci/pcivar.h>	/* XXX */
#include <dev/pci/pcireg.h>	/* XXX */

#include <dev/pcmcia/pcmciareg.h>

#ifdef CARDBUS_DEBUG
#define STATIC
#define DPRINTF(a) printf a
#else
#ifdef DDB
#define STATIC
#else
#define STATIC static
#endif
#define DPRINTF(a)
#endif

STATIC void cardbusattach(struct device *, struct device *, void *);
/* STATIC int cardbusprint(void *, const char *); */

STATIC int cardbusmatch(struct device *, void *, void *);
STATIC int cardbussubmatch(struct device *, void *, void *);
STATIC int cardbusprint(void *, const char *);

typedef void (*tuple_decode_func)(u_int8_t *, int, void *);

STATIC int decode_tuples(u_int8_t *, int, tuple_decode_func, void *);
STATIC void parse_tuple(u_int8_t *, int, void *);
#ifdef CARDBUS_DEBUG
static void print_tuple(u_int8_t *, int, void *);
#endif

STATIC int cardbus_read_tuples(struct cardbus_attach_args *,
    pcireg_t, u_int8_t *, size_t);

STATIC void enable_function(struct cardbus_softc *, int, int);
STATIC void disable_function(struct cardbus_softc *, int);


const struct cfattach cardbus_ca = {
	sizeof(struct cardbus_softc), cardbusmatch, cardbusattach
};

struct cfdriver cardbus_cd = {
	NULL, "cardbus", DV_DULL
};

STATIC int
cardbusmatch(struct device *parent, void *match, void *aux)
{
	struct cfdata *cf = match;
	struct cbslot_attach_args *cba = aux;

	if (strcmp(cba->cba_busname, cf->cf_driver->cd_name)) {
		DPRINTF(("cardbusmatch: busname differs %s <=> %s\n",
		    cba->cba_busname, cf->cf_driver->cd_name));
		return (0);
	}

	return (1);
}

STATIC void
cardbusattach(struct device *parent, struct device *self, void *aux)
{
	struct cardbus_softc *sc = (void *)self;
	struct cbslot_attach_args *cba = aux;
	int cdstatus;

	sc->sc_bus = cba->cba_bus;
	sc->sc_device = 0;
	sc->sc_intrline = cba->cba_intrline;
	sc->sc_cacheline = cba->cba_cacheline;
	sc->sc_lattimer = cba->cba_lattimer;

	printf(": bus %d device %d", sc->sc_bus, sc->sc_device);
	printf(" cacheline 0x%x, lattimer 0x%x\n",
	    sc->sc_cacheline,sc->sc_lattimer);

	sc->sc_iot = cba->cba_iot;	/* CardBus I/O space tag */
	sc->sc_memt = cba->cba_memt;	/* CardBus MEM space tag */
	sc->sc_dmat = cba->cba_dmat;	/* DMA tag */
	sc->sc_cc = cba->cba_cc;
	sc->sc_pc = cba->cba_pc;
	sc->sc_cf = cba->cba_cf;
	sc->sc_rbus_iot = cba->cba_rbus_iot;
	sc->sc_rbus_memt = cba->cba_rbus_memt;

	cdstatus = 0;
}

STATIC int
cardbus_read_tuples(struct cardbus_attach_args *ca, pcireg_t cis_ptr,
    u_int8_t *tuples, size_t len)
{
	struct cardbus_softc *sc = ca->ca_ct->ct_sc;
	pci_chipset_tag_t pc = ca->ca_pc;
	pcitag_t tag = ca->ca_tag;
	pcireg_t command;
	int found = 0;

	int i, j;
	int cardbus_space = cis_ptr & CARDBUS_CIS_ASIMASK;
	bus_space_tag_t bar_tag;
	bus_space_handle_t bar_memh;
	bus_size_t bar_size;
	bus_addr_t bar_addr;

	int reg;

	memset(tuples, 0, len);

	cis_ptr = cis_ptr & CARDBUS_CIS_ADDRMASK;

	switch (cardbus_space) {
	case CARDBUS_CIS_ASI_TUPLE:
		DPRINTF(("%s: reading CIS data from configuration space\n",
		    sc->sc_dev.dv_xname));
		for (i = cis_ptr, j = 0; i < 0xff; i += 4) {
			u_int32_t e = pci_conf_read(pc, tag, i);
			tuples[j] = 0xff & e;
			e >>= 8;
			tuples[j + 1] = 0xff & e;
			e >>= 8;
			tuples[j + 2] = 0xff & e;
			e >>= 8;
			tuples[j + 3] = 0xff & e;
			j += 4;
		}
		found++;
		break;

	case CARDBUS_CIS_ASI_BAR0:
	case CARDBUS_CIS_ASI_BAR1:
	case CARDBUS_CIS_ASI_BAR2:
	case CARDBUS_CIS_ASI_BAR3:
	case CARDBUS_CIS_ASI_BAR4:
	case CARDBUS_CIS_ASI_BAR5:
	case CARDBUS_CIS_ASI_ROM:
		if (cardbus_space == CARDBUS_CIS_ASI_ROM) {
			reg = CARDBUS_ROM_REG;
			DPRINTF(("%s: reading CIS data from ROM\n",
			    sc->sc_dev.dv_xname));
		} else {
			reg = CARDBUS_BASE0_REG + (cardbus_space - 1) * 4;
			DPRINTF(("%s: reading CIS data from BAR%d\n",
			    sc->sc_dev.dv_xname, cardbus_space - 1));
		}

		/* XXX zero register so mapreg_map doesn't get confused by old
		   contents */
		pci_conf_write(pc, tag, reg, 0);
		if (Cardbus_mapreg_map(ca->ca_ct, reg,
		    PCI_MAPREG_TYPE_MEM | PCI_MAPREG_MEM_TYPE_32BIT, 0,
		    &bar_tag, &bar_memh, &bar_addr, &bar_size)) {
			printf("%s: can't map memory\n",
			    sc->sc_dev.dv_xname);
			return (1);
		}

		if (cardbus_space == CARDBUS_CIS_ASI_ROM) {
			pcireg_t exrom;
			int save;
			struct cardbus_rom_image_head rom_image;
			struct cardbus_rom_image *p;

			save = splhigh();
			/* enable rom address decoder */
			exrom = pci_conf_read(pc, tag, reg);
			pci_conf_write(pc, tag, reg, exrom | 1);

			command = pci_conf_read(pc, tag,
			    PCI_COMMAND_STATUS_REG);
			pci_conf_write(pc, tag,
			    PCI_COMMAND_STATUS_REG,
			    command | PCI_COMMAND_MEM_ENABLE);

			if (cardbus_read_exrom(ca->ca_memt, bar_memh,
			    &rom_image))
				goto out;

			for (p = SIMPLEQ_FIRST(&rom_image); p;
			    p = SIMPLEQ_NEXT(p, next)) {
				if (p->rom_image ==
				    CARDBUS_CIS_ASI_ROM_IMAGE(cis_ptr)) {
					bus_space_read_region_1(p->romt,
					    p->romh, CARDBUS_CIS_ADDR(cis_ptr),
					    tuples, MIN(p->image_size, len));
					found++;
					break;
				}
			}

		out:
			while ((p = SIMPLEQ_FIRST(&rom_image)) != NULL) {
				SIMPLEQ_REMOVE_HEAD(&rom_image, next);
				free(p, M_DEVBUF, sizeof(*p));
			}
			exrom = pci_conf_read(pc, tag, reg);
			pci_conf_write(pc, tag, reg, exrom & ~1);
			splx(save);
		} else {
			command = pci_conf_read(pc, tag,
			    PCI_COMMAND_STATUS_REG);
			pci_conf_write(pc, tag,
			    PCI_COMMAND_STATUS_REG,
    			    command | PCI_COMMAND_MEM_ENABLE);
			/* XXX byte order? */
			bus_space_read_region_1(ca->ca_memt, bar_memh,
			    cis_ptr, tuples, 256);
			found++;
		}
		command = pci_conf_read(pc, tag,
		    PCI_COMMAND_STATUS_REG);
		pci_conf_write(pc, tag, PCI_COMMAND_STATUS_REG,
		    command & ~PCI_COMMAND_MEM_ENABLE);
		pci_conf_write(pc, tag, reg, 0);

		Cardbus_mapreg_unmap(ca->ca_ct, reg, bar_tag, bar_memh,
		    bar_size);
		break;

#ifdef DIAGNOSTIC
		default:
			panic("%s: bad CIS space (%d)", sc->sc_dev.dv_xname,
			    cardbus_space);
#endif
	}
	return (!found);
}

STATIC void
parse_tuple(u_int8_t *tuple, int len, void *data)
{
	struct cardbus_cis_info *cis = data;
	int bar_index;
	int i;
	char *p;

	switch (tuple[0]) {
	case PCMCIA_CISTPL_MANFID:
		if (tuple[1] < 4) {
			DPRINTF(("%s: wrong length manufacturer id (%d)\n",
			    __func__, tuple[1]));
			break;
		}
		cis->manufacturer = tuple[2] | (tuple[3] << 8);
		cis->product = tuple[4] | (tuple[5] << 8);
		break;
	case PCMCIA_CISTPL_VERS_1:
		bcopy(tuple + 2, cis->cis1_info_buf, tuple[1]);
		i = 0;
		p = cis->cis1_info_buf + 2;
		while (i <
		    sizeof(cis->cis1_info) / sizeof(cis->cis1_info[0])) {
			if (p >= cis->cis1_info_buf + tuple[1] || *p == '\xff')
				break;
			cis->cis1_info[i++] = p;
			while (*p != '\0' && *p != '\xff')
				p++;
			if (*p == '\0')
				p++;
		}
		break;
	case PCMCIA_CISTPL_BAR:
		if (tuple[1] != 6) {
			DPRINTF(("%s: BAR with short length (%d)\n",
			    __func__, tuple[1]));
			break;
		}
		bar_index = tuple[2] & 7;
		if (bar_index == 0) {
			DPRINTF(("%s: invalid ASI in BAR tuple\n",
			    __func__));
			break;
		}
		bar_index--;
		cis->bar[bar_index].flags = tuple[2];
		cis->bar[bar_index].size = (tuple[4] << 0) |
		    (tuple[5] << 8) | (tuple[6] << 16) | (tuple[7] << 24);
		break;
    case PCMCIA_CISTPL_FUNCID:
		cis->funcid = tuple[2];
		break;

    case PCMCIA_CISTPL_FUNCE:
		switch (cis->funcid) {
		case PCMCIA_FUNCTION_SERIAL:
			if (tuple[1] >= 2 &&
			    tuple[2] == 0
			    /* XXX PCMCIA_TPLFE_TYPE_SERIAL_??? */) {
				cis->funce.serial.uart_type = tuple[3] & 0x1f;
				cis->funce.serial.uart_present = 1;
			}
			break;
		case PCMCIA_FUNCTION_NETWORK:
			if (tuple[1] >= 8 && tuple[2] ==
			    PCMCIA_TPLFE_TYPE_LAN_NID) {
				if (tuple[3] >
				    sizeof(cis->funce.network.netid)) {
					DPRINTF(("%s: unknown network id type"
					    " (len = %d)\n", __func__,
					    tuple[3]));
				} else {
					cis->funce.network.netid_present = 1;
					bcopy(tuple + 4,
					    cis->funce.network.netid, tuple[3]);
				}
			}
		}
		break;
	}
}

/*
 * int cardbus_attach_card(struct cardbus_softc *sc)
 *
 *    This function attaches the card on the slot: turns on power,
 *    reads and analyses tuple, sets configuration index.
 *
 *    This function returns the number of recognised device functions.
 *    If no functions are recognised, return 0.
 */
int
cardbus_attach_card(struct cardbus_softc *sc)
{
	cardbus_chipset_tag_t cc;
	cardbus_function_tag_t cf;
	int cdstatus;
	pcitag_t tag;
	pcireg_t id, class, cis_ptr;
	pcireg_t bhlc;
	u_int8_t *tuple;
	int function, nfunction;
	struct device *csc;
	int no_work_funcs = 0;
	cardbus_devfunc_t ct;
	pci_chipset_tag_t pc = sc->sc_pc;
	int i;

	cc = sc->sc_cc;
	cf = sc->sc_cf;

	DPRINTF(("cardbus_attach_card: cb%d start\n", sc->sc_dev.dv_unit));

	/* inspect initial voltage */
	if (0 == (cdstatus = (cf->cardbus_ctrl)(cc, CARDBUS_CD))) {
		DPRINTF(("cardbusattach: no CardBus card on cb%d\n",
		    sc->sc_dev.dv_unit));
		return (0);
	}

	/* XXX use fake function 8 to keep power on during whole configuration */
	enable_function(sc, cdstatus, 8);

	function = 0;

	tag = pci_make_tag(pc, sc->sc_bus, sc->sc_device, function);

	/* Wait until power comes up.  Maximum 500 ms. */
	for (i = 0; i < 5; ++i) {
		id = pci_conf_read(pc, tag, PCI_ID_REG);
		if (id != 0xffffffff && id != 0)
			break;
		if (cold) {	/* before kernel thread invoked */
			delay(100*1000);
		} else {	/* thread context */
			if (tsleep_nsec(sc, PCATCH, "cardbus",
			    MSEC_TO_NSEC(100)) != EWOULDBLOCK) {
				break;
			}
		}
	}
	if (i == 5)
		return (0);

	bhlc = pci_conf_read(pc, tag, PCI_BHLC_REG);
	DPRINTF(("%s bhlc 0x%08x -> ", sc->sc_dev.dv_xname, bhlc));
	nfunction = PCI_HDRTYPE_MULTIFN(bhlc) ? 8 : 1;

	tuple = malloc(2048, M_TEMP, M_NOWAIT);
	if (tuple == NULL)
		panic("no room for cardbus tuples");

	for (function = 0; function < nfunction; function++) {
		struct cardbus_attach_args ca;

		tag = pci_make_tag(pc, sc->sc_bus, sc->sc_device,
		    function);

		id = pci_conf_read(pc, tag, PCI_ID_REG);
		class = pci_conf_read(pc, tag, PCI_CLASS_REG);
		cis_ptr = pci_conf_read(pc, tag, CARDBUS_CIS_REG);

		/* Invalid vendor ID value? */
		if (PCI_VENDOR(id) == PCI_VENDOR_INVALID)
			continue;

		DPRINTF(("cardbus_attach_card: Vendor 0x%x, Product 0x%x, "
		    "CIS 0x%x\n", PCI_VENDOR(id), PCI_PRODUCT(id),
		    cis_ptr));

		enable_function(sc, cdstatus, function);

		/* clean up every BAR */
		pci_conf_write(pc, tag, CARDBUS_BASE0_REG, 0);
		pci_conf_write(pc, tag, CARDBUS_BASE1_REG, 0);
		pci_conf_write(pc, tag, CARDBUS_BASE2_REG, 0);
		pci_conf_write(pc, tag, CARDBUS_BASE3_REG, 0);
		pci_conf_write(pc, tag, CARDBUS_BASE4_REG, 0);
		pci_conf_write(pc, tag, CARDBUS_BASE5_REG, 0);
		pci_conf_write(pc, tag, CARDBUS_ROM_REG, 0);

		/* set initial latency and cacheline size */
		bhlc = pci_conf_read(pc, tag, PCI_BHLC_REG);
		DPRINTF(("%s func%d bhlc 0x%08x -> ", sc->sc_dev.dv_xname,
		    function, bhlc));
		bhlc &= ~((PCI_LATTIMER_MASK << PCI_LATTIMER_SHIFT) |
		    (PCI_CACHELINE_MASK << PCI_CACHELINE_SHIFT));
		bhlc |= ((sc->sc_cacheline & PCI_CACHELINE_MASK) <<
		    PCI_CACHELINE_SHIFT);
		bhlc |= ((sc->sc_lattimer & PCI_LATTIMER_MASK) <<
		    PCI_LATTIMER_SHIFT);

		pci_conf_write(pc, tag, PCI_BHLC_REG, bhlc);
		bhlc = pci_conf_read(pc, tag, PCI_BHLC_REG);
		DPRINTF(("0x%08x\n", bhlc));

		if (PCI_LATTIMER(bhlc) < 0x10) {
			bhlc &= ~(PCI_LATTIMER_MASK <<
			    PCI_LATTIMER_SHIFT);
			bhlc |= (0x10 << PCI_LATTIMER_SHIFT);
			pci_conf_write(pc, tag, PCI_BHLC_REG,
			    bhlc);
		}

		/*
		 * We need to allocate the ct here, since we might
		 * need it when reading the CIS
		 */
		if ((ct =
		    (cardbus_devfunc_t)malloc(sizeof(struct cardbus_devfunc),
		    M_DEVBUF, M_NOWAIT)) == NULL)
			panic("no room for cardbus_tag");

		ct->ct_cc = sc->sc_cc;
		ct->ct_cf = sc->sc_cf;
		ct->ct_bus = sc->sc_bus;
		ct->ct_dev = sc->sc_device;
		ct->ct_func = function;
		ct->ct_sc = sc;
		sc->sc_funcs[function] = ct;

		memset(&ca, 0, sizeof(ca));

		ca.ca_unit = sc->sc_dev.dv_unit;
		ca.ca_ct = ct;

		ca.ca_iot = sc->sc_iot;
		ca.ca_memt = sc->sc_memt;
		ca.ca_dmat = sc->sc_dmat;
		ca.ca_rbus_iot = sc->sc_rbus_iot;
		ca.ca_rbus_memt = sc->sc_rbus_memt;
		ca.ca_tag = tag;
		ca.ca_bus = sc->sc_bus;
		ca.ca_device = sc->sc_device;
		ca.ca_function = function;
		ca.ca_id = id;
		ca.ca_class = class;
		ca.ca_pc = sc->sc_pc;

		ca.ca_intrline = sc->sc_intrline;

		if (cis_ptr != 0) {
			if (cardbus_read_tuples(&ca, cis_ptr, tuple, 2048)) {
				printf("cardbus_attach_card: failed to "
				    "read CIS\n");
			} else {
#ifdef CARDBUS_DEBUG
				decode_tuples(tuple, 2048, print_tuple, NULL);
#endif
				decode_tuples(tuple, 2048, parse_tuple,
				    &ca.ca_cis);
			}
		}

		if ((csc = config_found_sm((void *)sc, &ca, cardbusprint,
		    cardbussubmatch)) == NULL) {
			/* do not match */
			disable_function(sc, function);
			sc->sc_funcs[function] = NULL;
			free(ct, M_DEVBUF, sizeof(struct cardbus_devfunc));
		} else {
			/* found */
			ct->ct_device = csc;
			++no_work_funcs;
		}
	}
	/*
	 * XXX power down pseudo function 8 (this will power down the card
	 * if no functions were attached).
	 */
	disable_function(sc, 8);
	free(tuple, M_TEMP, 2048);

	return (no_work_funcs);
}

STATIC int
cardbussubmatch(struct device *parent, void *match, void *aux)
{
	struct cfdata *cf = match;
	struct cardbus_attach_args *ca = aux;

	if (cf->cardbuscf_dev != CARDBUS_UNK_DEV &&
		cf->cardbuscf_dev != ca->ca_unit) {
		return (0);
	}
	if (cf->cardbuscf_function != CARDBUS_UNK_FUNCTION &&
	    cf->cardbuscf_function != ca->ca_function) {
		return (0);
	}

	return ((*cf->cf_attach->ca_match)(parent, cf, aux));
}

STATIC int
cardbusprint(void *aux, const char *pnp)
{
	struct cardbus_attach_args *ca = aux;
	char devinfo[256];

	if (pnp) {
		pci_devinfo(ca->ca_id, ca->ca_class, 1, devinfo,
		    sizeof(devinfo));
		printf("%s at %s", devinfo, pnp);
	}
	printf(" dev %d function %d", ca->ca_device, ca->ca_function);
	if (!pnp) {
		pci_devinfo(ca->ca_id, ca->ca_class, 0, devinfo,
		    sizeof(devinfo));
		printf(" %s", devinfo);
	}

	return (UNCONF);
}

/*
 * void cardbus_detach_card(struct cardbus_softc *sc)
 *
 *    This function detaches the card on the slot: detach device data
 *    structure and turns off the power.
 *
 *    This function must not be called under interrupt context.
 */
void
cardbus_detach_card(struct cardbus_softc *sc)
{
	struct cardbus_devfunc *ct;
	int f;

	for (f = 0; f < 8; f++) {
		ct = sc->sc_funcs[f];
		if (ct == NULL)
			continue;

		DPRINTF(("%s: detaching %s\n", sc->sc_dev.dv_xname,
		    ct->ct_device->dv_xname));

		if (config_detach(ct->ct_device, 0) != 0) {
			printf("%s: cannot detach dev %s, function %d\n",
			    sc->sc_dev.dv_xname, ct->ct_device->dv_xname,
			    ct->ct_func);
		} else {
			sc->sc_poweron_func &= ~(1 << ct->ct_func);
			sc->sc_funcs[ct->ct_func] = NULL;
			free(ct, M_DEVBUF, sizeof(struct cardbus_devfunc));
		}
	}

	sc->sc_poweron_func = 0;
	sc->sc_cf->cardbus_power(sc->sc_cc, CARDBUS_VCC_0V | CARDBUS_VPP_0V);
}

/*
 * void *cardbus_intr_establish(cc, cf, irq, level, func, arg, name)
 *   Interrupt handler of pccard.
 *  args:
 *   cardbus_chipset_tag_t *cc
 *   int irq:
 */
void *
cardbus_intr_establish(cardbus_chipset_tag_t cc, cardbus_function_tag_t cf,
    cardbus_intr_handle_t irq, int level, int (*func)(void *), void *arg,
    const char *name)
{
	DPRINTF(("- cardbus_intr_establish: irq %d\n", irq));

	return (*cf->cardbus_intr_establish)(cc, irq, level, func, arg, name);
}

/*
 * void cardbus_intr_disestablish(cc, cf, handler)
 *   Interrupt handler of pccard.
 *  args:
 *   cardbus_chipset_tag_t *cc
 */
void
cardbus_intr_disestablish(cardbus_chipset_tag_t cc, cardbus_function_tag_t cf,
    void *handler)
{
	DPRINTF(("- pccard_intr_disestablish\n"));

	(*cf->cardbus_intr_disestablish)(cc, handler);
}

/* XXX this should be merged with cardbus_function_{enable,disable},
   but we don't have a ct when these functions are called */

STATIC void
enable_function(struct cardbus_softc *sc, int cdstatus, int function)
{
	if (sc->sc_poweron_func == 0) {
		/* switch to 3V and/or wait for power to stabilize */
		if (cdstatus & CARDBUS_3V_CARD) {
			sc->sc_cf->cardbus_power(sc->sc_cc, CARDBUS_VCC_3V);
		} else {
			/* No cards other than 3.3V cards. */
			return;
		}
		(sc->sc_cf->cardbus_ctrl)(sc->sc_cc, CARDBUS_RESET);
	}
	sc->sc_poweron_func |= (1 << function);
}

STATIC void
disable_function(struct cardbus_softc *sc, int function)
{
	sc->sc_poweron_func &= ~(1 << function);
	if (sc->sc_poweron_func == 0) {
		/* power-off because no functions are enabled */
		sc->sc_cf->cardbus_power(sc->sc_cc, CARDBUS_VCC_0V);
	}
}

/*
 * int cardbus_function_enable(struct cardbus_softc *sc, int func)
 *
 *   This function enables a function on a card.  When no power is
 *  applied on the card, power will be applied on it.
 */
int
cardbus_function_enable(struct cardbus_softc *sc, int func)
{
	pci_chipset_tag_t pc = sc->sc_pc;
	pcireg_t command;
	pcitag_t tag;

	DPRINTF(("entering cardbus_function_enable...  "));

	/* entering critical area */

	/* XXX: sc_vold should be used */
	enable_function(sc, CARDBUS_3V_CARD, func);

	/* exiting critical area */

	tag = pci_make_tag(pc, sc->sc_bus, sc->sc_device, func);

	command = pci_conf_read(pc, tag, PCI_COMMAND_STATUS_REG);
	command |= (PCI_COMMAND_MEM_ENABLE | PCI_COMMAND_IO_ENABLE |
	    PCI_COMMAND_MASTER_ENABLE); /* XXX: good guess needed */

	pci_conf_write(pc, tag, PCI_COMMAND_STATUS_REG, command);

	DPRINTF(("%x\n", sc->sc_poweron_func));

	return (0);
}

/*
 * int cardbus_function_disable(struct cardbus_softc *, int func)
 *
 *   This function disable a function on a card.  When no functions are
 *  enabled, it turns off the power.
 */
int
cardbus_function_disable(struct cardbus_softc *sc, int func)
{
	DPRINTF(("entering cardbus_function_disable...  "));

	disable_function(sc, func);

	return (0);
}

int
cardbus_matchbyid(struct cardbus_attach_args *ca,
    const struct pci_matchid *ids, int nent)
{
	const struct pci_matchid *pm;
	int i;

	for (i = 0, pm = ids; i < nent; i++, pm++)
		if (PCI_VENDOR(ca->ca_id) == pm->pm_vid &&
		    PCI_PRODUCT(ca->ca_id) == pm->pm_pid)
			return (1);
	return (0);
}

/*
 * below this line, there are some functions for decoding tuples.
 * They should go out from this file.
 */

STATIC u_int8_t *
decode_tuple(u_int8_t *, u_int8_t *, tuple_decode_func, void *);

STATIC int
decode_tuples(u_int8_t *tuple, int buflen, tuple_decode_func func, void *data)
{
	u_int8_t *tp = tuple;

	if (PCMCIA_CISTPL_LINKTARGET != *tuple) {
		DPRINTF(("WRONG TUPLE: 0x%x\n", *tuple));
		return (0);
	}

	while ((tp = decode_tuple(tp, tuple + buflen, func, data)) != NULL)
		;

	return (1);
}

STATIC u_int8_t *
decode_tuple(u_int8_t *tuple, u_int8_t *end, tuple_decode_func func,
    void *data)
{
	u_int8_t type;
	u_int8_t len;

	type = tuple[0];
	switch (type) {
	case PCMCIA_CISTPL_NULL:
	case PCMCIA_CISTPL_END:
		len = 1;
		break;
	default:
		if (tuple + 2 > end)
			return (NULL);
		len = tuple[1] + 2;
		break;
	}

	if (tuple + len > end)
		return (NULL);

	(*func)(tuple, len, data);

	if (PCMCIA_CISTPL_END == type || tuple + len == end)
		return (NULL);

	return (tuple + len);
}

#ifdef CARDBUS_DEBUG
static char *tuple_name(int type);

static char *
tuple_name(int type)
{
	static char *tuple_name_s [] = {
	    "TPL_NULL", "TPL_DEVICE", "Reserved", "Reserved",	  /* 0-3 */
	    "CONFIG_CB", "CFTABLE_ENTRY_CB", "Reserved", "BAR",	  /* 4-7 */
	    "Reserved", "Reserved", "Reserved", "Reserved",	  /* 8-B */
	    "Reserved", "Reserved", "Reserved", "Reserved",	  /* C-F */
	    "CHECKSUM", "LONGLINK_A", "LONGLINK_C", "LINKTARGET", /* 10-13 */
	    "NO_LINK", "VERS_1", "ALTSTR", "DEVICE_A",		  /* 14-17 */
	    "JEDEC_C", "JEDEC_A", "CONFIG", "CFTABLE_ENTRY",	  /* 18-1B */
	    "DEVICE_OC", "DEVICE_OA", "DEVICE_GEO",		  /* 1C-1E */
	    "DEVICE_GEO_A", "MANFID", "FUNCID", "FUNCE", "SWIL",  /* 1F-23 */
	    "Reserved", "Reserved", "Reserved", "Reserved",	  /* 24-27 */
	    "Reserved", "Reserved", "Reserved", "Reserved",	  /* 28-2B */
	    "Reserved", "Reserved", "Reserved", "Reserved",	  /* 2C-2F */
	    "Reserved", "Reserved", "Reserved", "Reserved",	  /* 30-33 */
	    "Reserved", "Reserved", "Reserved", "Reserved",	  /* 34-37 */
	    "Reserved", "Reserved", "Reserved", "Reserved",	  /* 38-3B */
	    "Reserved", "Reserved", "Reserved", "Reserved",	  /* 3C-3F */
	    "VERS_2", "FORMAT", "GEOMETRY", "BYTEORDER",	  /* 40-43 */
	    "DATE", "BATTERY", "ORG", "FORMAT_A"		  /* 44-47 */
	};

	if (type > 0 && type < nitems(tuple_name_s))
		return (tuple_name_s[type]);
	else if (0xff == type)
		return ("END");
	else
		return ("Reserved");
}

static void
print_tuple(u_int8_t *tuple, int len, void *data)
{
	int i;

	printf("tuple: %s len %d\n", tuple_name(tuple[0]), len);

	for (i = 0; i < len; ++i) {
		if (i % 16 == 0)
			printf("  0x%02x:", i);
		printf(" %x",tuple[i]);
		if (i % 16 == 15)
			printf("\n");
	}
	if (i % 16 != 0)
		printf("\n");
}
#endif
