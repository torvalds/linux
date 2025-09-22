/*	$OpenBSD: eso.c,v 1.56 2024/08/18 14:42:56 deraadt Exp $	*/
/*	$NetBSD: eso.c,v 1.48 2006/12/18 23:13:39 kleink Exp $	*/

/*
 * Copyright (c) 1999, 2000, 2004 Klaus J. Klein
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
 */

/*
 * ESS Technology Inc. Solo-1 PCI AudioDrive (ES1938/1946) device driver.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/device.h>

#include <dev/pci/pcidevs.h>
#include <dev/pci/pcivar.h>

#include <sys/audioio.h>
#include <dev/audio_if.h>
#include <dev/midi_if.h>

#include <dev/ic/i8237reg.h>
#include <dev/pci/esoreg.h>
#include <dev/pci/esovar.h>

#include <machine/bus.h>
#include <machine/intr.h>

/*
 * XXX Work around the 24-bit implementation limit of the Audio 1 DMA
 * XXX engine by allocating through the ISA DMA tag.
 */
#if defined(__amd64__) || defined(__i386__)
#include "isa.h"
#if NISA > 0
#include <dev/isa/isavar.h>
#endif
#endif

#if defined(AUDIO_DEBUG) || defined(DEBUG)
#define	DPRINTF(x)	if (esodebug) printf x
int	esodebug = 0;
#else
#define	DPRINTF(x)
#endif

struct eso_dma {
	bus_dma_tag_t		ed_dmat;
	bus_dmamap_t		ed_map;
	caddr_t			ed_addr;
	bus_dma_segment_t	ed_segs[1];
	int			ed_nsegs;
	size_t			ed_size;
	struct eso_dma *	ed_next;
};

#define KVADDR(dma)	((void *)(dma)->ed_addr)
#define DMAADDR(dma)	((dma)->ed_map->dm_segs[0].ds_addr)

int eso_match(struct device *, void *, void *);
void eso_attach(struct device *, struct device *, void *);
int eso_activate(struct device *, int);
void eso_defer(struct device *);

const struct cfattach eso_ca = {
	sizeof (struct eso_softc), eso_match, eso_attach, NULL,
	eso_activate
};

struct cfdriver eso_cd = {
	NULL, "eso", DV_DULL
};

/* PCI interface */
int eso_intr(void *);

/* MI audio layer interface */
int	eso_open(void *, int);
void	eso_close(void *);
int	eso_set_params(void *, int, int, struct audio_params *,
		    struct audio_params *);
int	eso_round_blocksize(void *, int);
int	eso_halt_output(void *);
int	eso_halt_input(void *);
int	eso_set_port(void *, mixer_ctrl_t *);
int	eso_get_port(void *, mixer_ctrl_t *);
int	eso_query_devinfo(void *, mixer_devinfo_t *);
void *	eso_allocm(void *, int, size_t, int, int);
void	eso_freem(void *, void *, int);
size_t	eso_round_buffersize(void *, int, size_t);
int	eso_trigger_output(void *, void *, void *, int,
		    void (*)(void *), void *, struct audio_params *);
int	eso_trigger_input(void *, void *, void *, int,
		    void (*)(void *), void *, struct audio_params *);
void	eso_setup(struct eso_softc *, int, int);

const struct audio_hw_if eso_hw_if = {
	.open = eso_open,
	.close = eso_close,
	.set_params = eso_set_params,
	.round_blocksize = eso_round_blocksize,
	.halt_output = eso_halt_output,
	.halt_input = eso_halt_input,
	.set_port = eso_set_port,
	.get_port = eso_get_port,
	.query_devinfo = eso_query_devinfo,
	.allocm = eso_allocm,
	.freem = eso_freem,
	.round_buffersize = eso_round_buffersize,
	.trigger_output = eso_trigger_output,
	.trigger_input = eso_trigger_input,
};

const char * const eso_rev2model[] = {
	"ES1938",
	"ES1946",
	"ES1946 rev E"
};


/*
 * Utility routines
 */

/* Register access etc. */
uint8_t	eso_read_ctlreg(struct eso_softc *, uint8_t);
uint8_t	eso_read_mixreg(struct eso_softc *, uint8_t);
uint8_t	eso_read_rdr(struct eso_softc *);
void	eso_reload_master_vol(struct eso_softc *);
int	eso_reset(struct eso_softc *);
void	eso_set_gain(struct eso_softc *, uint);
int	eso_set_recsrc(struct eso_softc *, uint);
int	eso_set_monooutsrc(struct eso_softc *, uint);
int	eso_set_monoinbypass(struct eso_softc *, uint);
int	eso_set_preamp(struct eso_softc *, uint);
void	eso_write_cmd(struct eso_softc *, uint8_t);
void	eso_write_ctlreg(struct eso_softc *, uint8_t, uint8_t);
void	eso_write_mixreg(struct eso_softc *, uint8_t, uint8_t);

/* DMA memory allocation */
int	eso_allocmem(struct eso_softc *, size_t, size_t, size_t,
		    int, int, struct eso_dma *);
void	eso_freemem(struct eso_dma *);


int
eso_match(struct device *parent, void *match, void *aux)
{
	struct pci_attach_args *pa = aux;

	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_ESSTECH &&
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_ESSTECH_SOLO1)
		return (1);

	return (0);
}

void
eso_attach(struct device *parent, struct device *self, void *aux)
{
	struct eso_softc *sc = (struct eso_softc *)self;
	struct pci_attach_args *pa = aux;
	struct audio_attach_args aa;
	pci_intr_handle_t ih;
	bus_addr_t vcbase;
	const char *intrstring;
	uint8_t mvctl;

	sc->sc_revision = PCI_REVISION(pa->pa_class);

	if (sc->sc_revision <
	    sizeof (eso_rev2model) / sizeof (eso_rev2model[0]))
		printf(": %s", eso_rev2model[sc->sc_revision]);
	else
		printf(": (unknown rev. 0x%02x)", sc->sc_revision);

	/* Map I/O registers. */
	if (pci_mapreg_map(pa, ESO_PCI_BAR_IO, PCI_MAPREG_TYPE_IO, 0,
	    &sc->sc_iot, &sc->sc_ioh, NULL, NULL, 0)) {
		printf(": can't map i/o space\n");
		return;
	}
	if (pci_mapreg_map(pa, ESO_PCI_BAR_SB, PCI_MAPREG_TYPE_IO, 0,
	    &sc->sc_sb_iot, &sc->sc_sb_ioh, NULL, NULL, 0)) {
		printf(": can't map SB I/O space\n");
		return;
	}
	if (pci_mapreg_map(pa, ESO_PCI_BAR_VC, PCI_MAPREG_TYPE_IO, 0,
	    &sc->sc_dmac_iot, &sc->sc_dmac_ioh, &vcbase, &sc->sc_vcsize, 0)) {
		vcbase = 0;
		sc->sc_vcsize = 0x10; /* From the data sheet. */
	}
	if (pci_mapreg_map(pa, ESO_PCI_BAR_MPU, PCI_MAPREG_TYPE_IO, 0,
	    &sc->sc_mpu_iot, &sc->sc_mpu_ioh, NULL, NULL, 0)) {
		printf(": can't map MPU I/O space\n");
		return;
	}

	sc->sc_dmat = pa->pa_dmat;
	sc->sc_dmas = NULL;
	sc->sc_dmac_configured = 0;

	sc->sc_pa = *pa;

	eso_setup(sc, 1, 0);

	/* map and establish the interrupt. */
	if (pci_intr_map(pa, &ih)) {
		printf(", couldn't map interrupt\n");
		return;
	}
	intrstring = pci_intr_string(pa->pa_pc, ih);
	sc->sc_ih  = pci_intr_establish(pa->pa_pc, ih, IPL_AUDIO | IPL_MPSAFE,
	    eso_intr, sc, sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(", couldn't establish interrupt");
		if (intrstring != NULL)
			printf(" at %s", intrstring);
		printf("\n");
		return;
	}
	printf(", %s\n", intrstring);

	/*
	 * Set up the DDMA Control register; a suitable I/O region has been
	 * supposedly mapped in the VC base address register.
	 *
	 * The Solo-1 has an ... interesting silicon bug that causes it to
	 * not respond to I/O space accesses to the Audio 1 DMA controller
	 * if the latter's mapping base address is aligned on a 1K boundary.
	 * As a consequence, it is quite possible for the mapping provided
	 * in the VC BAR to be useless.  To work around this, we defer this
	 * part until all autoconfiguration on our parent bus is completed
	 * and then try to map it ourselves in fulfillment of the constraint.
	 * 
	 * According to the register map we may write to the low 16 bits
	 * only, but experimenting has shown we're safe.
	 * -kjk
	 */

	if (ESO_VALID_DDMAC_BASE(vcbase)) {
		pci_conf_write(pa->pa_pc, pa->pa_tag, ESO_PCI_DDMAC,
			       vcbase | ESO_PCI_DDMAC_DE);
		sc->sc_dmac_configured = 1;
		sc->sc_dmac_addr = vcbase;
		
		printf("%s: mapping Audio 1 DMA using VC I/O space at 0x%lx\n",
		       sc->sc_dev.dv_xname, (unsigned long)vcbase);
	} else {
		DPRINTF(("%s: VC I/O space at 0x%lx not suitable, deferring\n",
			 sc->sc_dev.dv_xname, (unsigned long)vcbase));
		config_defer((struct device *)sc, eso_defer);
	}
	
	audio_attach_mi(&eso_hw_if, sc, NULL, &sc->sc_dev);

	aa.type = AUDIODEV_TYPE_OPL;
	aa.hwif = NULL;
	aa.hdl = NULL;
	(void)config_found(&sc->sc_dev, &aa, audioprint);

	aa.type = AUDIODEV_TYPE_MPU;
	aa.hwif = NULL;
	aa.hdl = NULL;
	sc->sc_mpudev = config_found(&sc->sc_dev, &aa, audioprint);
	if (sc->sc_mpudev != NULL) {
		/* Unmask the MPU irq. */
		mvctl = eso_read_mixreg(sc, ESO_MIXREG_MVCTL);
		mvctl |= ESO_MIXREG_MVCTL_MPUIRQM;
		eso_write_mixreg(sc, ESO_MIXREG_MVCTL, mvctl);
	}
}

void
eso_setup(struct eso_softc *sc, int verbose, int resuming)
{
	struct pci_attach_args *pa = &sc->sc_pa;	
	uint8_t a2mode, tmp;
	int idx; 

	/* Reset the device; bail out upon failure. */
	if (eso_reset(sc) != 0) {
		if (verbose) printf(", can't reset\n");
		return;
	}
	
	/* Select the DMA/IRQ policy: DDMA, ISA IRQ emulation disabled. */
	pci_conf_write(pa->pa_pc, pa->pa_tag, ESO_PCI_S1C,
		       pci_conf_read(pa->pa_pc, pa->pa_tag, ESO_PCI_S1C) &
		       ~(ESO_PCI_S1C_IRQP_MASK | ESO_PCI_S1C_DMAP_MASK));

	/* Enable the relevant DMA interrupts. */
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, ESO_IO_IRQCTL,
	    ESO_IO_IRQCTL_A1IRQ | ESO_IO_IRQCTL_A2IRQ | ESO_IO_IRQCTL_HVIRQ |
	    ESO_IO_IRQCTL_MPUIRQ);

	/* Set up A1's sample rate generator for new-style parameters. */
	a2mode = eso_read_mixreg(sc, ESO_MIXREG_A2MODE);
	a2mode |= ESO_MIXREG_A2MODE_NEWA1 | ESO_MIXREG_A2MODE_ASYNC;
	eso_write_mixreg(sc, ESO_MIXREG_A2MODE, a2mode);

	/* Slave Master Volume to Hardware Volume Control Counter, unmask IRQ. */
	tmp = eso_read_mixreg(sc, ESO_MIXREG_MVCTL);
	tmp &= ~ESO_MIXREG_MVCTL_SPLIT;
	tmp |= ESO_MIXREG_MVCTL_HVIRQM;
	eso_write_mixreg(sc, ESO_MIXREG_MVCTL, tmp);

	if (!resuming) {
		/* Set mixer regs to something reasonable, needs work. */
		sc->sc_recmon = sc->sc_spatializer = sc->sc_mvmute = 0;
		eso_set_monooutsrc(sc, ESO_MIXREG_MPM_MOMUTE);
		eso_set_monoinbypass(sc, 0);
		eso_set_preamp(sc, 1);
		for (idx = 0; idx < ESO_NGAINDEVS; idx++) {
			int v;
		
			switch (idx) {
 			case ESO_MIC_PLAY_VOL:
			case ESO_LINE_PLAY_VOL:
			case ESO_CD_PLAY_VOL:
			case ESO_MONO_PLAY_VOL:
			case ESO_AUXB_PLAY_VOL:
			case ESO_DAC_REC_VOL:
			case ESO_LINE_REC_VOL:
			case ESO_SYNTH_REC_VOL:
			case ESO_CD_REC_VOL:
			case ESO_MONO_REC_VOL:
			case ESO_AUXB_REC_VOL:
			case ESO_SPATIALIZER:
				v = 0;
				break;
			case ESO_MASTER_VOL:
				v = ESO_GAIN_TO_6BIT(AUDIO_MAX_GAIN / 2);
				break;
			default:
				v = ESO_GAIN_TO_4BIT(AUDIO_MAX_GAIN / 2);
				break;
			}
			sc->sc_gain[idx][ESO_LEFT] =
			    sc->sc_gain[idx][ESO_RIGHT] = v;
			eso_set_gain(sc, idx);
		}
		eso_set_recsrc(sc, ESO_MIXREG_ERS_MIC);
	} else {
		eso_set_monooutsrc(sc, sc->sc_monooutsrc);
		eso_set_monoinbypass(sc, sc->sc_monoinbypass);
		eso_set_preamp(sc, sc->sc_preamp);
		eso_set_recsrc(sc, sc->sc_recsrc);

		/* recmon */
		tmp = eso_read_ctlreg(sc, ESO_CTLREG_ACTL);
		if (sc->sc_recmon)
			tmp |= ESO_CTLREG_ACTL_RECMON;
		else
			tmp &= ~ESO_CTLREG_ACTL_RECMON;
		eso_write_ctlreg(sc, ESO_CTLREG_ACTL, tmp);

		/* spatializer enable */
		tmp = eso_read_mixreg(sc, ESO_MIXREG_SPAT);
		if (sc->sc_spatializer)
			tmp |= ESO_MIXREG_SPAT_ENB;
		else
			tmp &= ~ESO_MIXREG_SPAT_ENB;
		eso_write_mixreg(sc, ESO_MIXREG_SPAT,
		    tmp | ESO_MIXREG_SPAT_RSTREL);

		/* master volume mute */
		if (sc->sc_mvmute) {
			eso_write_mixreg(sc, ESO_MIXREG_LMVM,
			    eso_read_mixreg(sc, ESO_MIXREG_LMVM) |
			    ESO_MIXREG_LMVM_MUTE);
			eso_write_mixreg(sc, ESO_MIXREG_RMVM,
			    eso_read_mixreg(sc, ESO_MIXREG_RMVM) |
			    ESO_MIXREG_RMVM_MUTE);
		} else { 
			eso_write_mixreg(sc, ESO_MIXREG_LMVM,
			    eso_read_mixreg(sc, ESO_MIXREG_LMVM) &
			    ~ESO_MIXREG_LMVM_MUTE);
			eso_write_mixreg(sc, ESO_MIXREG_RMVM,
			    eso_read_mixreg(sc, ESO_MIXREG_RMVM) &
			    ~ESO_MIXREG_RMVM_MUTE);
		}

		for (idx = 0; idx < ESO_NGAINDEVS; idx++)
			eso_set_gain(sc, idx);
	}
}

void
eso_defer(struct device *self)
{
	struct eso_softc *sc = (struct eso_softc *)self;
	struct pci_attach_args *pa = &sc->sc_pa;
	bus_addr_t addr, start;

	printf("%s: ", sc->sc_dev.dv_xname);

	/*
	 * This is outright ugly, but since we must not make assumptions
	 * on the underlying allocator's behaviour it's the most straight-
	 * forward way to implement it.  Note that we skip over the first
	 * 1K region, which is typically occupied by an attached ISA bus.
	 */
	for (start = 0x0400; start < 0xffff; start += 0x0400) {
		if (bus_space_alloc(sc->sc_iot,
		    start + sc->sc_vcsize, start + 0x0400 - 1,
		    sc->sc_vcsize, sc->sc_vcsize, 0, 0, &addr,
		    &sc->sc_dmac_ioh) != 0)
			continue;

		pci_conf_write(pa->pa_pc, pa->pa_tag, ESO_PCI_DDMAC,
		    addr | ESO_PCI_DDMAC_DE);
		sc->sc_dmac_iot = sc->sc_iot;
		sc->sc_dmac_configured = 1;
		sc->sc_dmac_addr = addr;
		printf("mapping Audio 1 DMA using I/O space at 0x%lx\n",
		    (unsigned long)addr);

		return;
	}
	
	printf("can't map Audio 1 DMA into I/O space\n");
}

void
eso_write_cmd(struct eso_softc *sc, uint8_t cmd)
{
	int i;

	/* Poll for busy indicator to become clear. */
	for (i = 0; i < ESO_WDR_TIMEOUT; i++) {
		if ((bus_space_read_1(sc->sc_sb_iot, sc->sc_sb_ioh, ESO_SB_RSR)
		    & ESO_SB_RSR_BUSY) == 0) {
			bus_space_write_1(sc->sc_sb_iot, sc->sc_sb_ioh,
			    ESO_SB_WDR, cmd);
			return;
		} else {
			delay(10);
		}
	}

	printf("%s: WDR timeout\n", sc->sc_dev.dv_xname);
}

/* Write to a controller register */
void
eso_write_ctlreg(struct eso_softc *sc, uint8_t reg, uint8_t val)
{

	/* DPRINTF(("ctlreg 0x%02x = 0x%02x\n", reg, val)); */
	
	eso_write_cmd(sc, reg);
	eso_write_cmd(sc, val);
}

/* Read out the Read Data Register */
uint8_t
eso_read_rdr(struct eso_softc *sc)
{
	int i;

	for (i = 0; i < ESO_RDR_TIMEOUT; i++) {
		if (bus_space_read_1(sc->sc_sb_iot, sc->sc_sb_ioh,
		    ESO_SB_RBSR) & ESO_SB_RBSR_RDAV) {
			return (bus_space_read_1(sc->sc_sb_iot,
			    sc->sc_sb_ioh, ESO_SB_RDR));
		} else {
			delay(10);
		}
	}

	printf("%s: RDR timeout\n", sc->sc_dev.dv_xname);
	return (-1);
}


uint8_t
eso_read_ctlreg(struct eso_softc *sc, uint8_t reg)
{
	eso_write_cmd(sc, ESO_CMD_RCR);
	eso_write_cmd(sc, reg);
	return (eso_read_rdr(sc));
}

void
eso_write_mixreg(struct eso_softc *sc, uint8_t reg, uint8_t val)
{
	/* DPRINTF(("mixreg 0x%02x = 0x%02x\n", reg, val)); */
	
	bus_space_write_1(sc->sc_sb_iot, sc->sc_sb_ioh, ESO_SB_MIXERADDR, reg);
	bus_space_write_1(sc->sc_sb_iot, sc->sc_sb_ioh, ESO_SB_MIXERDATA, val);
}

uint8_t
eso_read_mixreg(struct eso_softc *sc, uint8_t reg)
{
	uint8_t val;

	bus_space_write_1(sc->sc_sb_iot, sc->sc_sb_ioh, ESO_SB_MIXERADDR, reg);
	val = bus_space_read_1(sc->sc_sb_iot, sc->sc_sb_ioh, ESO_SB_MIXERDATA);
	return (val);
}

int
eso_intr(void *hdl)
{
	struct eso_softc *sc = hdl;
	uint8_t irqctl;

	mtx_enter(&audio_lock);
	irqctl = bus_space_read_1(sc->sc_iot, sc->sc_ioh, ESO_IO_IRQCTL);

	/* If it wasn't ours, that's all she wrote. */
	if ((irqctl & (ESO_IO_IRQCTL_A1IRQ | ESO_IO_IRQCTL_A2IRQ |
	    ESO_IO_IRQCTL_HVIRQ | ESO_IO_IRQCTL_MPUIRQ)) == 0) {
		mtx_leave(&audio_lock);
		return (0);
	}
	
	if (irqctl & ESO_IO_IRQCTL_A1IRQ) {
		/* Clear interrupt. */
		(void)bus_space_read_1(sc->sc_sb_iot, sc->sc_sb_ioh,
		    ESO_SB_RBSR);
	
		if (sc->sc_rintr)
			sc->sc_rintr(sc->sc_rarg);
		else
			wakeup(&sc->sc_rintr);
	}

	if (irqctl & ESO_IO_IRQCTL_A2IRQ) {
		/*
		 * Clear the A2 IRQ latch: the cached value reflects the
		 * current DAC settings with the IRQ latch bit not set.
		 */
		eso_write_mixreg(sc, ESO_MIXREG_A2C2, sc->sc_a2c2);

		if (sc->sc_pintr)
			sc->sc_pintr(sc->sc_parg);
		else
			wakeup(&sc->sc_pintr);
	}

	if (irqctl & ESO_IO_IRQCTL_HVIRQ) {
		/* Clear interrupt. */
		eso_write_mixreg(sc, ESO_MIXREG_CHVIR, ESO_MIXREG_CHVIR_CHVIR);

		/*
		 * Raise a flag to cause a lazy update of the in-softc gain
		 * values the next time the software mixer is read to keep
		 * interrupt service cost low.  ~0 cannot occur otherwise
		 * as the master volume has a precision of 6 bits only.
		 */
		sc->sc_gain[ESO_MASTER_VOL][ESO_LEFT] = (uint8_t)~0;
	}

#if NMIDI > 0
	if ((irqctl & ESO_IO_IRQCTL_MPUIRQ) && sc->sc_mpudev != NULL)
		mpu_intr(sc->sc_mpudev);
#endif
 
	mtx_leave(&audio_lock);
	return (1);
}

/* Perform a software reset, including DMA FIFOs. */
int
eso_reset(struct eso_softc *sc)
{
	int i;

	bus_space_write_1(sc->sc_sb_iot, sc->sc_sb_ioh, ESO_SB_RESET,
	    ESO_SB_RESET_SW | ESO_SB_RESET_FIFO);
	/* `Delay' suggested in the data sheet. */
	(void)bus_space_read_1(sc->sc_sb_iot, sc->sc_sb_ioh, ESO_SB_STATUS);
	bus_space_write_1(sc->sc_sb_iot, sc->sc_sb_ioh, ESO_SB_RESET, 0);

	/* Wait for reset to take effect. */
	for (i = 0; i < ESO_RESET_TIMEOUT; i++) {
		/* Poll for data to become available. */
		if ((bus_space_read_1(sc->sc_sb_iot, sc->sc_sb_ioh,
		    ESO_SB_RBSR) & ESO_SB_RBSR_RDAV) != 0 &&
		    bus_space_read_1(sc->sc_sb_iot, sc->sc_sb_ioh,
			ESO_SB_RDR) == ESO_SB_RDR_RESETMAGIC) {

			/* Activate Solo-1 extension commands. */
			eso_write_cmd(sc, ESO_CMD_EXTENB);
			/* Reset mixer registers. */
			eso_write_mixreg(sc, ESO_MIXREG_RESET,
			    ESO_MIXREG_RESET_RESET);

			return (0);
		} else {
			delay(1000);
		}
	}
	
	printf("%s: reset timeout\n", sc->sc_dev.dv_xname);
	return (-1);
}


int
eso_open(void *hdl, int flags)
{
	return (0);
}

void
eso_close(void *hdl)
{
}

int
eso_set_params(void *hdl, int setmode, int usemode,
    struct audio_params *play, struct audio_params *rec)
{
	struct eso_softc *sc = hdl;
	struct audio_params *p;
	int mode, r[2], rd[2], ar[2], clk;
	uint srg, fltdiv;

	for (mode = AUMODE_RECORD; mode != -1; 
	     mode = mode == AUMODE_RECORD ? AUMODE_PLAY : -1) {
		if ((setmode & mode) == 0)
			continue;

		p = (mode == AUMODE_PLAY) ? play : rec;

		if (p->sample_rate < ESO_MINRATE)
			p->sample_rate = ESO_MINRATE;
		if (p->sample_rate > ESO_MAXRATE)
			p->sample_rate = ESO_MAXRATE;
		if (p->precision > 16)
			p->precision = 16;
		if (p->channels > 2)
			p->channels = 2;

		switch (p->encoding) {
		case AUDIO_ENCODING_SLINEAR_BE:
		case AUDIO_ENCODING_ULINEAR_BE:
			if (p->precision != 8)
				return EINVAL;
			break;
		case AUDIO_ENCODING_SLINEAR_LE:
		case AUDIO_ENCODING_ULINEAR_LE:
			break;
		default:
			return (EINVAL);
		}
		p->bps = AUDIO_BPS(p->precision);
		p->msb = 1;

		/*
		 * We'll compute both possible sample rate dividers and pick
		 * the one with the least error.
		 */
#define ABS(x) ((x) < 0 ? -(x) : (x))
		r[0] = ESO_CLK0 /
		    (128 - (rd[0] = 128 - ESO_CLK0 / p->sample_rate));
		r[1] = ESO_CLK1 /
		    (128 - (rd[1] = 128 - ESO_CLK1 / p->sample_rate));

		ar[0] = p->sample_rate - r[0];
		ar[1] = p->sample_rate - r[1];
		clk = ABS(ar[0]) > ABS(ar[1]) ? 1 : 0;
		srg = rd[clk] | (clk == 1 ? ESO_CLK1_SELECT : 0x00);

		/* Roll-off frequency of 87%, as in the ES1888 driver. */
		fltdiv = 256 - 200279L / r[clk];

		/* Update to reflect the possibly inexact rate. */
		p->sample_rate = r[clk];
	
		if (mode == AUMODE_RECORD) {
			/* Audio 1 */
			DPRINTF(("A1 srg 0x%02x fdiv 0x%02x\n", srg, fltdiv));
			eso_write_ctlreg(sc, ESO_CTLREG_SRG, srg);
			eso_write_ctlreg(sc, ESO_CTLREG_FLTDIV, fltdiv);
		} else {
			/* Audio 2 */
			DPRINTF(("A2 srg 0x%02x fdiv 0x%02x\n", srg, fltdiv));
			eso_write_mixreg(sc, ESO_MIXREG_A2SRG, srg);
			eso_write_mixreg(sc, ESO_MIXREG_A2FLTDIV, fltdiv);
		}
#undef ABS

	}

	return (0);
}

int
eso_round_blocksize(void *hdl, int blk)
{
	return ((blk + 31) & -32); /* keep good alignment; at least 16 req'd */
}

int
eso_halt_output(void *hdl)
{
	struct eso_softc *sc = hdl;
	int error;
	
	DPRINTF(("%s: halt_output\n", sc->sc_dev.dv_xname));

	/*
	 * Disable auto-initialize DMA, allowing the FIFO to drain and then
	 * stop.  The interrupt callback pointer is cleared at this
	 * point so that an outstanding FIFO interrupt for the remaining data
	 * will be acknowledged without further processing.
	 *
	 * This does not immediately `abort' an operation in progress (c.f.
	 * audio(9)) but is the method to leave the FIFO behind in a clean
	 * state with the least hair.  (Besides, that item needs to be
	 * rephrased for trigger_*()-based DMA environments.)
	 */
	mtx_enter(&audio_lock);
	eso_write_mixreg(sc, ESO_MIXREG_A2C1,
	    ESO_MIXREG_A2C1_FIFOENB | ESO_MIXREG_A2C1_DMAENB);
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, ESO_IO_A2DMAM,
	    ESO_IO_A2DMAM_DMAENB);

	sc->sc_pintr = NULL;
	error = msleep_nsec(&sc->sc_pintr, &audio_lock, PWAIT | PNORELOCK,
	    "esoho", MSEC_TO_NSEC(sc->sc_pdrain));
	
	/* Shut down DMA completely. */
	eso_write_mixreg(sc, ESO_MIXREG_A2C1, 0);
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, ESO_IO_A2DMAM, 0);
	
	return (error == EWOULDBLOCK ? 0 : error);
}

int
eso_halt_input(void *hdl)
{
	struct eso_softc *sc = hdl;
	int error;
	
	DPRINTF(("%s: halt_input\n", sc->sc_dev.dv_xname));

	/* Just like eso_halt_output(), but for Audio 1. */
	mtx_enter(&audio_lock);
	eso_write_ctlreg(sc, ESO_CTLREG_A1C2,
	    ESO_CTLREG_A1C2_READ | ESO_CTLREG_A1C2_ADC |
	    ESO_CTLREG_A1C2_DMAENB);
	bus_space_write_1(sc->sc_dmac_iot, sc->sc_dmac_ioh, ESO_DMAC_MODE,
	    DMA37MD_WRITE | DMA37MD_DEMAND);

	sc->sc_rintr = NULL;
	error = msleep_nsec(&sc->sc_rintr, &audio_lock, PWAIT | PNORELOCK,
	    "esohi", MSEC_TO_NSEC(sc->sc_rdrain));

	/* Shut down DMA completely. */
	eso_write_ctlreg(sc, ESO_CTLREG_A1C2,
	    ESO_CTLREG_A1C2_READ | ESO_CTLREG_A1C2_ADC);
	bus_space_write_1(sc->sc_dmac_iot, sc->sc_dmac_ioh, ESO_DMAC_MASK,
	    ESO_DMAC_MASK_MASK);

	return (error == EWOULDBLOCK ? 0 : error);
}

int
eso_set_port(void *hdl, mixer_ctrl_t *cp)
{
	struct eso_softc *sc = hdl;
	uint lgain, rgain;
	uint8_t tmp;
	int rc = 0;

	mtx_enter(&audio_lock);
	switch (cp->dev) {
	case ESO_DAC_PLAY_VOL:
	case ESO_MIC_PLAY_VOL:
	case ESO_LINE_PLAY_VOL:
	case ESO_SYNTH_PLAY_VOL:
	case ESO_CD_PLAY_VOL:
	case ESO_AUXB_PLAY_VOL:
	case ESO_RECORD_VOL:
	case ESO_DAC_REC_VOL:
	case ESO_MIC_REC_VOL:
	case ESO_LINE_REC_VOL:
	case ESO_SYNTH_REC_VOL:
	case ESO_CD_REC_VOL:
	case ESO_AUXB_REC_VOL:
		if (cp->type != AUDIO_MIXER_VALUE)
			goto error;

		/*
		 * Stereo-capable mixer ports: if we get a single-channel
		 * gain value passed in, then we duplicate it to both left
		 * and right channels.
		 */
		switch (cp->un.value.num_channels) {
		case 1:
			lgain = rgain = ESO_GAIN_TO_4BIT(
			    cp->un.value.level[AUDIO_MIXER_LEVEL_MONO]);
			break;
		case 2:
			lgain = ESO_GAIN_TO_4BIT(
			    cp->un.value.level[AUDIO_MIXER_LEVEL_LEFT]);
			rgain = ESO_GAIN_TO_4BIT(
			    cp->un.value.level[AUDIO_MIXER_LEVEL_RIGHT]);
			break;
		default:
			goto error;
		}

		sc->sc_gain[cp->dev][ESO_LEFT] = lgain;
		sc->sc_gain[cp->dev][ESO_RIGHT] = rgain;
		eso_set_gain(sc, cp->dev);
		break;

	case ESO_MASTER_VOL:
		if (cp->type != AUDIO_MIXER_VALUE)
			goto error;

		/* Like above, but a precision of 6 bits. */
		switch (cp->un.value.num_channels) {
		case 1:
			lgain = rgain = ESO_GAIN_TO_6BIT(
			    cp->un.value.level[AUDIO_MIXER_LEVEL_MONO]);
			break;
		case 2:
			lgain = ESO_GAIN_TO_6BIT(
			    cp->un.value.level[AUDIO_MIXER_LEVEL_LEFT]);
			rgain = ESO_GAIN_TO_6BIT(
			    cp->un.value.level[AUDIO_MIXER_LEVEL_RIGHT]);
			break;
		default:
			goto error;
		}

		sc->sc_gain[cp->dev][ESO_LEFT] = lgain;
		sc->sc_gain[cp->dev][ESO_RIGHT] = rgain;
		eso_set_gain(sc, cp->dev);
		break;

	case ESO_SPATIALIZER:
		if (cp->type != AUDIO_MIXER_VALUE ||
		    cp->un.value.num_channels != 1)
			goto error;

		sc->sc_gain[cp->dev][ESO_LEFT] =
		    sc->sc_gain[cp->dev][ESO_RIGHT] =
		    ESO_GAIN_TO_6BIT(
			cp->un.value.level[AUDIO_MIXER_LEVEL_MONO]);
		eso_set_gain(sc, cp->dev);
		break;

	case ESO_MONO_PLAY_VOL:
	case ESO_MONO_REC_VOL:
		if (cp->type != AUDIO_MIXER_VALUE ||
		    cp->un.value.num_channels != 1)
			goto error;

		sc->sc_gain[cp->dev][ESO_LEFT] =
		    sc->sc_gain[cp->dev][ESO_RIGHT] =
		    ESO_GAIN_TO_4BIT(
			cp->un.value.level[AUDIO_MIXER_LEVEL_MONO]);
		eso_set_gain(sc, cp->dev);
		break;

	case ESO_PCSPEAKER_VOL:
		if (cp->type != AUDIO_MIXER_VALUE ||
		    cp->un.value.num_channels != 1)
			goto error;

		sc->sc_gain[cp->dev][ESO_LEFT] =
		    sc->sc_gain[cp->dev][ESO_RIGHT] =
		    ESO_GAIN_TO_3BIT(
			cp->un.value.level[AUDIO_MIXER_LEVEL_MONO]);
		eso_set_gain(sc, cp->dev);
		break;

	case ESO_SPATIALIZER_ENABLE:
		if (cp->type != AUDIO_MIXER_ENUM)
			goto error;

		sc->sc_spatializer = (cp->un.ord != 0);

		tmp = eso_read_mixreg(sc, ESO_MIXREG_SPAT);
		if (sc->sc_spatializer)
			tmp |= ESO_MIXREG_SPAT_ENB;
		else
			tmp &= ~ESO_MIXREG_SPAT_ENB;
		eso_write_mixreg(sc, ESO_MIXREG_SPAT,
		    tmp | ESO_MIXREG_SPAT_RSTREL);
		break;

	case ESO_MASTER_MUTE:
		if (cp->type != AUDIO_MIXER_ENUM)
			goto error;

		sc->sc_mvmute = (cp->un.ord != 0);

		if (sc->sc_mvmute) {
			eso_write_mixreg(sc, ESO_MIXREG_LMVM,
			    eso_read_mixreg(sc, ESO_MIXREG_LMVM) |
			    ESO_MIXREG_LMVM_MUTE);
			eso_write_mixreg(sc, ESO_MIXREG_RMVM,
			    eso_read_mixreg(sc, ESO_MIXREG_RMVM) |
			    ESO_MIXREG_RMVM_MUTE);
		} else { 
			eso_write_mixreg(sc, ESO_MIXREG_LMVM,
			    eso_read_mixreg(sc, ESO_MIXREG_LMVM) &
			    ~ESO_MIXREG_LMVM_MUTE);
			eso_write_mixreg(sc, ESO_MIXREG_RMVM,
			    eso_read_mixreg(sc, ESO_MIXREG_RMVM) &
			    ~ESO_MIXREG_RMVM_MUTE);
		}
		break;

	case ESO_MONOOUT_SOURCE:
		if (cp->type != AUDIO_MIXER_ENUM)
			goto error;

		rc = eso_set_monooutsrc(sc, cp->un.ord);
		break;

	case ESO_MONOIN_BYPASS:
		if (cp->type != AUDIO_MIXER_ENUM)
			goto error;

		rc = eso_set_monoinbypass(sc, cp->un.ord);
		break;

	case ESO_RECORD_MONITOR:
		if (cp->type != AUDIO_MIXER_ENUM)
			goto error;

		sc->sc_recmon = (cp->un.ord != 0);

		tmp = eso_read_ctlreg(sc, ESO_CTLREG_ACTL);
		if (sc->sc_recmon)
			tmp |= ESO_CTLREG_ACTL_RECMON;
		else
			tmp &= ~ESO_CTLREG_ACTL_RECMON;
		eso_write_ctlreg(sc, ESO_CTLREG_ACTL, tmp);
		break;

	case ESO_RECORD_SOURCE:
		if (cp->type != AUDIO_MIXER_ENUM)
			goto error;

		rc = eso_set_recsrc(sc, cp->un.ord);
		break;

	case ESO_MIC_PREAMP:
		if (cp->type != AUDIO_MIXER_ENUM)
			goto error;

		rc = eso_set_preamp(sc, cp->un.ord);
		break;

	default:
		goto error;
	}

	mtx_leave(&audio_lock);
	return rc;
error:
	mtx_leave(&audio_lock);
	return EINVAL;
}

int
eso_get_port(void *hdl, mixer_ctrl_t *cp)
{
	struct eso_softc *sc = hdl;

	mtx_enter(&audio_lock);
	switch (cp->dev) {
	case ESO_MASTER_VOL:
		/* Reload from mixer after hardware volume control use. */
		if (sc->sc_gain[cp->dev][ESO_LEFT] == (uint8_t)~0)
			eso_reload_master_vol(sc);
		/* FALLTHROUGH */
	case ESO_DAC_PLAY_VOL:
	case ESO_MIC_PLAY_VOL:
	case ESO_LINE_PLAY_VOL:
	case ESO_SYNTH_PLAY_VOL:
	case ESO_CD_PLAY_VOL:
	case ESO_AUXB_PLAY_VOL:
	case ESO_RECORD_VOL:
	case ESO_DAC_REC_VOL:
	case ESO_MIC_REC_VOL:
	case ESO_LINE_REC_VOL:
	case ESO_SYNTH_REC_VOL:
	case ESO_CD_REC_VOL:
	case ESO_AUXB_REC_VOL:
		/*
		 * Stereo-capable ports: if a single-channel query is made,
		 * just return the left channel's value (since single-channel
		 * settings themselves are applied to both channels).
		 */
		switch (cp->un.value.num_channels) {
		case 1:
			cp->un.value.level[AUDIO_MIXER_LEVEL_MONO] =
			    sc->sc_gain[cp->dev][ESO_LEFT];
			break;
		case 2:
			cp->un.value.level[AUDIO_MIXER_LEVEL_LEFT] =
			    sc->sc_gain[cp->dev][ESO_LEFT];
			cp->un.value.level[AUDIO_MIXER_LEVEL_RIGHT] =
			    sc->sc_gain[cp->dev][ESO_RIGHT];
			break;
		default:
			goto error;
		}
		break;

	case ESO_MONO_PLAY_VOL:
	case ESO_PCSPEAKER_VOL:
	case ESO_MONO_REC_VOL:
	case ESO_SPATIALIZER:
		if (cp->un.value.num_channels != 1)
			goto error;
		cp->un.value.level[AUDIO_MIXER_LEVEL_MONO] =
		    sc->sc_gain[cp->dev][ESO_LEFT];
		break;

	case ESO_RECORD_MONITOR:
		cp->un.ord = sc->sc_recmon;
		break;

	case ESO_RECORD_SOURCE:
		cp->un.ord = sc->sc_recsrc;
		break;

	case ESO_MONOOUT_SOURCE:
		cp->un.ord = sc->sc_monooutsrc;
		break;

	case ESO_MONOIN_BYPASS:
		cp->un.ord = sc->sc_monoinbypass;
		break;

	case ESO_SPATIALIZER_ENABLE:
		cp->un.ord = sc->sc_spatializer;
		break;

	case ESO_MIC_PREAMP:
		cp->un.ord = sc->sc_preamp;
		break;

	case ESO_MASTER_MUTE:
		/* Reload from mixer after hardware volume control use. */
		if (sc->sc_gain[ESO_MASTER_VOL][ESO_LEFT] == (uint8_t)~0)
			eso_reload_master_vol(sc);
		cp->un.ord = sc->sc_mvmute;
		break;

	default:
		goto error;
	}

	mtx_leave(&audio_lock);
	return 0;
error:
	mtx_leave(&audio_lock);
	return EINVAL;
}

int
eso_query_devinfo(void *hdl, mixer_devinfo_t *dip)
{
	switch (dip->index) {
	case ESO_DAC_PLAY_VOL:
		dip->mixer_class = ESO_INPUT_CLASS;
		dip->next = dip->prev = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioNdac, sizeof dip->label.name);
		dip->type = AUDIO_MIXER_VALUE;
		dip->un.v.num_channels = 2;
		strlcpy(dip->un.v.units.name, AudioNvolume,
		    sizeof dip->un.v.units.name);
		break;
	case ESO_MIC_PLAY_VOL:
		dip->mixer_class = ESO_INPUT_CLASS;
		dip->next = dip->prev = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioNmicrophone,
		    sizeof dip->label.name);
		dip->type = AUDIO_MIXER_VALUE;
		dip->un.v.num_channels = 2;
		strlcpy(dip->un.v.units.name, AudioNvolume,
		    sizeof dip->un.v.units.name);
		break;
	case ESO_LINE_PLAY_VOL:
		dip->mixer_class = ESO_INPUT_CLASS;
		dip->next = dip->prev = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioNline, sizeof dip->label.name);
		dip->type = AUDIO_MIXER_VALUE;
		dip->un.v.num_channels = 2;
		strlcpy(dip->un.v.units.name, AudioNvolume,
		    sizeof dip->un.v.units.name);
		break;
	case ESO_SYNTH_PLAY_VOL:
		dip->mixer_class = ESO_INPUT_CLASS;
		dip->next = dip->prev = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioNfmsynth,
		    sizeof dip->label.name);
		dip->type = AUDIO_MIXER_VALUE;
		dip->un.v.num_channels = 2;
		strlcpy(dip->un.v.units.name, AudioNvolume,
		    sizeof dip->un.v.units.name);
		break;
	case ESO_MONO_PLAY_VOL:
		dip->mixer_class = ESO_INPUT_CLASS;
		dip->next = dip->prev = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, "mono_in", sizeof dip->label.name);
		dip->type = AUDIO_MIXER_VALUE;
		dip->un.v.num_channels = 1;
		strlcpy(dip->un.v.units.name, AudioNvolume,
		    sizeof dip->un.v.units.name);
		break;
	case ESO_CD_PLAY_VOL:
		dip->mixer_class = ESO_INPUT_CLASS;
		dip->next = dip->prev = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioNcd, sizeof dip->label.name);
		dip->type = AUDIO_MIXER_VALUE;
		dip->un.v.num_channels = 2;
		strlcpy(dip->un.v.units.name, AudioNvolume,
		    sizeof dip->un.v.units.name);
		break;
	case ESO_AUXB_PLAY_VOL:
		dip->mixer_class = ESO_INPUT_CLASS;
		dip->next = dip->prev = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, "auxb", sizeof dip->label.name);
		dip->type = AUDIO_MIXER_VALUE;
		dip->un.v.num_channels = 2;
		strlcpy(dip->un.v.units.name, AudioNvolume,
		    sizeof dip->un.v.units.name);
		break;
	case ESO_MIC_PREAMP:
		dip->mixer_class = ESO_MICROPHONE_CLASS;
		dip->next = dip->prev = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioNpreamp, sizeof dip->label.name);
		dip->type = AUDIO_MIXER_ENUM;
		dip->un.e.num_mem = 2;
		strlcpy(dip->un.e.member[0].label.name, AudioNoff,
		    sizeof dip->un.e.member[0].label.name);
		dip->un.e.member[0].ord = 0;
		strlcpy(dip->un.e.member[1].label.name, AudioNon,
		    sizeof dip->un.e.member[1].label.name);
		dip->un.e.member[1].ord = 1;
		break;
	case ESO_MICROPHONE_CLASS:
		dip->mixer_class = ESO_MICROPHONE_CLASS;
		dip->next = dip->prev = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioNmicrophone,
		    sizeof dip->label.name);
		dip->type = AUDIO_MIXER_CLASS;
		break;
	case ESO_INPUT_CLASS:
		dip->mixer_class = ESO_INPUT_CLASS;
		dip->next = dip->prev = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioCinputs, sizeof dip->label.name);
		dip->type = AUDIO_MIXER_CLASS;
		break;
	case ESO_MASTER_VOL:
		dip->mixer_class = ESO_OUTPUT_CLASS;
		dip->prev = AUDIO_MIXER_LAST;
		dip->next = ESO_MASTER_MUTE;
		strlcpy(dip->label.name, AudioNmaster, sizeof dip->label.name);
		dip->type = AUDIO_MIXER_VALUE;
		dip->un.v.num_channels = 2;
		strlcpy(dip->un.v.units.name, AudioNvolume,
		    sizeof dip->un.v.units.name);
		break;
	case ESO_MASTER_MUTE:
		dip->mixer_class = ESO_OUTPUT_CLASS;
		dip->prev = ESO_MASTER_VOL;
		dip->next = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioNmute, sizeof dip->label.name);
		dip->type = AUDIO_MIXER_ENUM;
		dip->un.e.num_mem = 2;
		strlcpy(dip->un.e.member[0].label.name, AudioNoff,
		    sizeof dip->un.e.member[0].label.name);
		dip->un.e.member[0].ord = 0;
		strlcpy(dip->un.e.member[1].label.name, AudioNon,
		    sizeof dip->un.e.member[1].label.name);
		dip->un.e.member[1].ord = 1;
		break;
	case ESO_PCSPEAKER_VOL:
		dip->mixer_class = ESO_OUTPUT_CLASS;
		dip->next = dip->prev = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, "pc_speaker", sizeof dip->label.name);
		dip->type = AUDIO_MIXER_VALUE;
		dip->un.v.num_channels = 1;
		strlcpy(dip->un.v.units.name, AudioNvolume,
		    sizeof dip->un.v.units.name);
		break;
	case ESO_MONOOUT_SOURCE:
		dip->mixer_class = ESO_OUTPUT_CLASS;
		dip->next = dip->prev = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, "mono_out", sizeof dip->label.name);
		dip->type = AUDIO_MIXER_ENUM;
		dip->un.e.num_mem = 3;
		strlcpy(dip->un.e.member[0].label.name, AudioNmute,
		    sizeof dip->un.e.member[0].label.name);
		dip->un.e.member[0].ord = ESO_MIXREG_MPM_MOMUTE;
		strlcpy(dip->un.e.member[1].label.name, AudioNdac,
		    sizeof dip->un.e.member[1].label.name);
		dip->un.e.member[1].ord = ESO_MIXREG_MPM_MOA2R;
		strlcpy(dip->un.e.member[2].label.name, AudioNmixerout,
		    sizeof dip->un.e.member[2].label.name);
		dip->un.e.member[2].ord = ESO_MIXREG_MPM_MOREC;
		break;
	case ESO_MONOIN_BYPASS:
		dip->mixer_class = ESO_MONOIN_CLASS;
		dip->next = dip->prev = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, "bypass", sizeof dip->label.name);
		dip->type = AUDIO_MIXER_ENUM;
		dip->un.e.num_mem = 2;
		strlcpy(dip->un.e.member[0].label.name, AudioNoff,
		    sizeof dip->un.e.member[0].label.name);
		dip->un.e.member[0].ord = 0;
		strlcpy(dip->un.e.member[1].label.name, AudioNon,
		    sizeof dip->un.e.member[1].label.name);
		dip->un.e.member[1].ord = 1;
		break;
	case ESO_MONOIN_CLASS:
		dip->mixer_class = ESO_MONOIN_CLASS;
		dip->next = dip->prev = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, "mono_in", sizeof dip->label.name);
		dip->type = AUDIO_MIXER_CLASS;
		break;
	case ESO_SPATIALIZER:
		dip->mixer_class = ESO_OUTPUT_CLASS;
		dip->prev = AUDIO_MIXER_LAST;
		dip->next = ESO_SPATIALIZER_ENABLE;
		strlcpy(dip->label.name, AudioNspatial,
		    sizeof dip->label.name);
		dip->type = AUDIO_MIXER_VALUE;
		dip->un.v.num_channels = 1;
		strlcpy(dip->un.v.units.name, "level",
		    sizeof dip->un.v.units.name);
		break;
	case ESO_SPATIALIZER_ENABLE:
		dip->mixer_class = ESO_OUTPUT_CLASS;
		dip->prev = ESO_SPATIALIZER;
		dip->next = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, "enable", sizeof dip->label.name);
		dip->type = AUDIO_MIXER_ENUM;
		dip->un.e.num_mem = 2;
		strlcpy(dip->un.e.member[0].label.name, AudioNoff,
		    sizeof dip->un.e.member[0].label.name);
		dip->un.e.member[0].ord = 0;
		strlcpy(dip->un.e.member[1].label.name, AudioNon,
		    sizeof dip->un.e.member[1].label.name);
		dip->un.e.member[1].ord = 1;
		break;
	case ESO_OUTPUT_CLASS:
		dip->mixer_class = ESO_OUTPUT_CLASS;
		dip->next = dip->prev = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioCoutputs,
		    sizeof dip->label.name);
		dip->type = AUDIO_MIXER_CLASS;
		break;
	case ESO_RECORD_MONITOR:
		dip->mixer_class = ESO_MONITOR_CLASS;
		dip->next = dip->prev = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioNmute, sizeof dip->label.name);
		dip->type = AUDIO_MIXER_ENUM;
		dip->un.e.num_mem = 2;
		strlcpy(dip->un.e.member[0].label.name, AudioNoff,
		    sizeof dip->un.e.member[0].label.name);
		dip->un.e.member[0].ord = 0;
		strlcpy(dip->un.e.member[1].label.name, AudioNon,
		    sizeof dip->un.e.member[1].label.name);
		dip->un.e.member[1].ord = 1;
		break;
	case ESO_MONITOR_CLASS:
		dip->mixer_class = ESO_MONITOR_CLASS;
		dip->next = dip->prev = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioCmonitor,
		    sizeof dip->label.name);
		dip->type = AUDIO_MIXER_CLASS;
		break;
	case ESO_RECORD_VOL:
		dip->mixer_class = ESO_RECORD_CLASS;
		dip->next = dip->prev = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioNrecord, sizeof dip->label.name);
		dip->type = AUDIO_MIXER_VALUE;
		strlcpy(dip->un.v.units.name, AudioNvolume,
		    sizeof dip->un.v.units.name);
		break;
	case ESO_RECORD_SOURCE:
		dip->mixer_class = ESO_RECORD_CLASS;
		dip->next = dip->prev = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioNsource, sizeof dip->label.name);
		dip->type = AUDIO_MIXER_ENUM;
		dip->un.e.num_mem = 4;
		strlcpy(dip->un.e.member[0].label.name, AudioNmicrophone,
		    sizeof dip->un.e.member[0].label.name);
		dip->un.e.member[0].ord = ESO_MIXREG_ERS_MIC;
		strlcpy(dip->un.e.member[1].label.name, AudioNline,
		    sizeof dip->un.e.member[1].label.name);
		dip->un.e.member[1].ord = ESO_MIXREG_ERS_LINE;
		strlcpy(dip->un.e.member[2].label.name, AudioNcd,
		    sizeof dip->un.e.member[2].label.name);
		dip->un.e.member[2].ord = ESO_MIXREG_ERS_CD;
		strlcpy(dip->un.e.member[3].label.name, AudioNmixerout,
		    sizeof dip->un.e.member[3].label.name);
		dip->un.e.member[3].ord = ESO_MIXREG_ERS_MIXER;
		break;
	case ESO_DAC_REC_VOL:
		dip->mixer_class = ESO_RECORD_CLASS;
		dip->next = dip->prev = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioNdac, sizeof dip->label.name);
		dip->type = AUDIO_MIXER_VALUE;
		dip->un.v.num_channels = 2;
		strlcpy(dip->un.v.units.name, AudioNvolume,
		    sizeof dip->un.v.units.name);
		break;
	case ESO_MIC_REC_VOL:
		dip->mixer_class = ESO_RECORD_CLASS;
		dip->next = dip->prev = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioNmicrophone,
		    sizeof dip->label.name);
		dip->type = AUDIO_MIXER_VALUE;
		dip->un.v.num_channels = 2;
		strlcpy(dip->un.v.units.name, AudioNvolume,
		    sizeof dip->un.v.units.name);
		break;
	case ESO_LINE_REC_VOL:
		dip->mixer_class = ESO_RECORD_CLASS;
		dip->next = dip->prev = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioNline, sizeof dip->label.name);
		dip->type = AUDIO_MIXER_VALUE;
		dip->un.v.num_channels = 2;
		strlcpy(dip->un.v.units.name, AudioNvolume,
		    sizeof dip->un.v.units.name);
		break;
	case ESO_SYNTH_REC_VOL:
		dip->mixer_class = ESO_RECORD_CLASS;
		dip->next = dip->prev = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioNfmsynth,
		    sizeof dip->label.name);
		dip->type = AUDIO_MIXER_VALUE;
		dip->un.v.num_channels = 2;
		strlcpy(dip->un.v.units.name, AudioNvolume,
		    sizeof dip->un.v.units.name);
		break;
	case ESO_MONO_REC_VOL:
		dip->mixer_class = ESO_RECORD_CLASS;
		dip->next = dip->prev = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, "mono_in", sizeof dip->label.name);
		dip->type = AUDIO_MIXER_VALUE;
		dip->un.v.num_channels = 1; /* No lies */
		strlcpy(dip->un.v.units.name, AudioNvolume,
		    sizeof dip->un.v.units.name);
		break;
	case ESO_CD_REC_VOL:
		dip->mixer_class = ESO_RECORD_CLASS;
		dip->next = dip->prev = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioNcd, sizeof dip->label.name);
		dip->type = AUDIO_MIXER_VALUE;
		dip->un.v.num_channels = 2;
		strlcpy(dip->un.v.units.name, AudioNvolume,
		    sizeof dip->un.v.units.name);
		break;
	case ESO_AUXB_REC_VOL:
		dip->mixer_class = ESO_RECORD_CLASS;
		dip->next = dip->prev = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, "auxb", sizeof dip->label.name);
		dip->type = AUDIO_MIXER_VALUE;
		dip->un.v.num_channels = 2;
		strlcpy(dip->un.v.units.name, AudioNvolume,
		    sizeof dip->un.v.units.name);
		break;
	case ESO_RECORD_CLASS:
		dip->mixer_class = ESO_RECORD_CLASS;
		dip->next = dip->prev = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioCrecord, sizeof dip->label.name);
		dip->type = AUDIO_MIXER_CLASS;
		break;
	default:
		return (ENXIO);
	}

	return (0);
}

int
eso_allocmem(struct eso_softc *sc, size_t size, size_t align,
    size_t boundary, int flags, int direction, struct eso_dma *ed)
{
	int error, wait;

	wait = (flags & M_NOWAIT) ? BUS_DMA_NOWAIT : BUS_DMA_WAITOK;
	ed->ed_size = size;

	error = bus_dmamem_alloc(ed->ed_dmat, ed->ed_size, align, boundary,
	    ed->ed_segs, sizeof (ed->ed_segs) / sizeof (ed->ed_segs[0]),
	    &ed->ed_nsegs, wait);
	if (error)
		goto out;

	error = bus_dmamem_map(ed->ed_dmat, ed->ed_segs, ed->ed_nsegs,
	    ed->ed_size, &ed->ed_addr, wait | BUS_DMA_COHERENT);
	if (error)
		goto free;

	error = bus_dmamap_create(ed->ed_dmat, ed->ed_size, 1, ed->ed_size,
	    boundary,  wait, &ed->ed_map);
	if (error)
		goto unmap;

	error = bus_dmamap_load(ed->ed_dmat, ed->ed_map, ed->ed_addr,
	    ed->ed_size, NULL, wait |
	    ((direction == AUMODE_RECORD) ? BUS_DMA_READ : BUS_DMA_WRITE));
	if (error)
		goto destroy;

	return (0);

 destroy:
	bus_dmamap_destroy(ed->ed_dmat, ed->ed_map);
 unmap:
	bus_dmamem_unmap(ed->ed_dmat, ed->ed_addr, ed->ed_size);
 free:
	bus_dmamem_free(ed->ed_dmat, ed->ed_segs, ed->ed_nsegs);
 out:
	return (error);
}

void
eso_freemem(struct eso_dma *ed)
{
	bus_dmamap_unload(ed->ed_dmat, ed->ed_map);
	bus_dmamap_destroy(ed->ed_dmat, ed->ed_map);
	bus_dmamem_unmap(ed->ed_dmat, ed->ed_addr, ed->ed_size);
	bus_dmamem_free(ed->ed_dmat, ed->ed_segs, ed->ed_nsegs);
}
	
void *
eso_allocm(void *hdl, int direction, size_t size, int type, int flags)
{
	struct eso_softc *sc = hdl;
	struct eso_dma *ed;
	size_t boundary;
	int error;

	if ((ed = malloc(sizeof (*ed), type, flags)) == NULL)
		return (NULL);

	/*
	 * Apparently the Audio 1 DMA controller's current address
	 * register can't roll over a 64K address boundary, so we have to
	 * take care of that ourselves.  Similarly, the Audio 2 DMA
	 * controller needs a 1M address boundary.
	 */
	if (direction == AUMODE_RECORD)
		boundary = 0x10000;
	else
		boundary = 0x100000;

	/*
	 * XXX Work around allocation problems for Audio 1, which
	 * XXX implements the 24 low address bits only, with
	 * XXX machine-specific DMA tag use.
	 */
#if defined(__alpha__)
	/*
	 * XXX Force allocation through the (ISA) SGMAP.
	 */
	if (direction == AUMODE_RECORD)
		ed->ed_dmat = alphabus_dma_get_tag(sc->sc_dmat, ALPHA_BUS_ISA);
	else
#elif defined(__amd64__) || defined(__i386__)
	/*
	 * XXX Force allocation through the ISA DMA tag.
	 */
	if (direction == AUMODE_RECORD)
		ed->ed_dmat = &isa_bus_dma_tag;
	else
#endif
		ed->ed_dmat = sc->sc_dmat;

	error = eso_allocmem(sc, size, 32, boundary, flags, direction, ed);
	if (error) {
		free(ed, type, sizeof(*ed));
		return (NULL);
	}
	ed->ed_next = sc->sc_dmas;
	sc->sc_dmas = ed;

	return (KVADDR(ed));
}

void
eso_freem(void *hdl, void *addr, int type)
{
	struct eso_softc *sc = hdl;
	struct eso_dma *p, **pp;

	for (pp = &sc->sc_dmas; (p = *pp) != NULL; pp = &p->ed_next) {
		if (KVADDR(p) == addr) {
			eso_freemem(p);
			*pp = p->ed_next;
			free(p, type, sizeof(*p));
			return;
		}
	}
}

size_t
eso_round_buffersize(void *hdl, int direction, size_t bufsize)
{
	size_t maxsize;

	/*
	 * The playback DMA buffer size on the Solo-1 is limited to 0xfff0
	 * bytes.  This is because IO_A2DMAC is a two byte value
	 * indicating the literal byte count, and the 4 least significant
	 * bits are read-only.  Zero is not used as a special case for
	 * 0x10000.
	 *
	 * For recording, DMAC_DMAC is the byte count - 1, so 0x10000 can
	 * be represented.
	 */
	maxsize = (direction == AUMODE_PLAY) ? 0xfff0 : 0x10000;

	if (bufsize > maxsize)
		bufsize = maxsize;

	return (bufsize);
}

int
eso_trigger_output(void *hdl, void *start, void *end, int blksize,
    void (*intr)(void *), void *arg, struct audio_params *param)
{
	struct eso_softc *sc = hdl;
	struct eso_dma *ed;
	uint8_t a2c1;

	DPRINTF((
	    "%s: trigger_output: start %p, end %p, blksize %d, intr %p(%p)\n",
	    sc->sc_dev.dv_xname, start, end, blksize, intr, arg));
	DPRINTF(("%s: param: rate %lu, encoding %u, precision %u, channels %u\n",
	    sc->sc_dev.dv_xname, param->sample_rate, param->encoding,
	    param->precision, param->channels));

	/* Find DMA buffer. */
	for (ed = sc->sc_dmas; ed != NULL && KVADDR(ed) != start;
	     ed = ed->ed_next)
		;
	if (ed == NULL) {
		printf("%s: trigger_output: bad addr %p\n",
		    sc->sc_dev.dv_xname, start);
		return (EINVAL);
	}
	DPRINTF(("%s: output dmaaddr %lx\n",
	    sc->sc_dev.dv_xname, (unsigned long)DMAADDR(ed)));

	sc->sc_pintr = intr;
	sc->sc_parg = arg;

	/* Compute drain timeout (milliseconds). */
	sc->sc_pdrain = 1000 * (blksize * 3 / 2) / 
	    (param->sample_rate * param->channels * param->bps);

	/* DMA transfer count (in `words'!) reload using 2's complement. */
	blksize = -(blksize >> 1);
	eso_write_mixreg(sc, ESO_MIXREG_A2TCRLO, blksize & 0xff);
	eso_write_mixreg(sc, ESO_MIXREG_A2TCRHI, blksize >> 8);

	/* Update DAC to reflect DMA count and audio parameters. */
	/* Note: we cache A2C2 in order to avoid r/m/w at interrupt time. */
	if (param->precision == 16)
		sc->sc_a2c2 |= ESO_MIXREG_A2C2_16BIT;
	else
		sc->sc_a2c2 &= ~ESO_MIXREG_A2C2_16BIT;
	if (param->channels == 2)
		sc->sc_a2c2 |= ESO_MIXREG_A2C2_STEREO;
	else
		sc->sc_a2c2 &= ~ESO_MIXREG_A2C2_STEREO;
	if (param->encoding == AUDIO_ENCODING_SLINEAR_BE ||
	    param->encoding == AUDIO_ENCODING_SLINEAR_LE)
		sc->sc_a2c2 |= ESO_MIXREG_A2C2_SIGNED;
	else
		sc->sc_a2c2 &= ~ESO_MIXREG_A2C2_SIGNED;
	/* Unmask IRQ. */
	sc->sc_a2c2 |= ESO_MIXREG_A2C2_IRQM;
	eso_write_mixreg(sc, ESO_MIXREG_A2C2, sc->sc_a2c2);

	/* Set up DMA controller. */
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, ESO_IO_A2DMAA, DMAADDR(ed));
	bus_space_write_2(sc->sc_iot, sc->sc_ioh, ESO_IO_A2DMAC,
	    (uint8_t *)end - (uint8_t *)start);
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, ESO_IO_A2DMAM,
	    ESO_IO_A2DMAM_DMAENB | ESO_IO_A2DMAM_AUTO);

	/* Start DMA. */
	mtx_enter(&audio_lock);
	a2c1 = eso_read_mixreg(sc, ESO_MIXREG_A2C1);
	a2c1 &= ~ESO_MIXREG_A2C1_RESV0; /* Paranoia? XXX bit 5 */
	a2c1 |= ESO_MIXREG_A2C1_FIFOENB | ESO_MIXREG_A2C1_DMAENB |
	    ESO_MIXREG_A2C1_AUTO;
	eso_write_mixreg(sc, ESO_MIXREG_A2C1, a2c1);
	mtx_leave(&audio_lock);
	return (0);
}

int
eso_trigger_input(void *hdl, void *start, void *end, int blksize,
    void (*intr)(void *), void *arg, struct audio_params *param)
{
	struct eso_softc *sc = hdl;
	struct eso_dma *ed;
	uint8_t actl, a1c1;

	DPRINTF((
	    "%s: trigger_input: start %p, end %p, blksize %d, intr %p(%p)\n",
	    sc->sc_dev.dv_xname, start, end, blksize, intr, arg));
	DPRINTF(("%s: param: rate %lu, encoding %u, precision %u, channels %u\n",
	    sc->sc_dev.dv_xname, param->sample_rate, param->encoding,
	    param->precision, param->channels));

	/*
	 * If we failed to configure the Audio 1 DMA controller, bail here
	 * while retaining availability of the DAC direction (in Audio 2).
	 */
	if (!sc->sc_dmac_configured)
		return (EIO);

	/* Find DMA buffer. */
	for (ed = sc->sc_dmas; ed != NULL && KVADDR(ed) != start;
	     ed = ed->ed_next)
		;
	if (ed == NULL) {
		printf("%s: trigger_input: bad addr %p\n",
		    sc->sc_dev.dv_xname, start);
		return (EINVAL);
	}
	DPRINTF(("%s: input dmaaddr %lx\n",
	    sc->sc_dev.dv_xname, (unsigned long)DMAADDR(ed)));

	sc->sc_rintr = intr;
	sc->sc_rarg = arg;

	/* Compute drain timeout (milliseconds). */
	sc->sc_rdrain = 1000 * (blksize * 3 / 2) / 
	    (param->sample_rate * param->channels * param->bps);

	/* Set up ADC DMA converter parameters. */
	actl = eso_read_ctlreg(sc, ESO_CTLREG_ACTL);
	if (param->channels == 2) {
		actl &= ~ESO_CTLREG_ACTL_MONO;
		actl |= ESO_CTLREG_ACTL_STEREO;
	} else {
		actl &= ~ESO_CTLREG_ACTL_STEREO;
		actl |= ESO_CTLREG_ACTL_MONO;
	}
	eso_write_ctlreg(sc, ESO_CTLREG_ACTL, actl);

	/* Set up Transfer Type: maybe move to attach time? */
	eso_write_ctlreg(sc, ESO_CTLREG_A1TT, ESO_CTLREG_A1TT_DEMAND4);

	/* DMA transfer count reload using 2's complement. */
	blksize = -blksize;
	eso_write_ctlreg(sc, ESO_CTLREG_A1TCRLO, blksize & 0xff);
	eso_write_ctlreg(sc, ESO_CTLREG_A1TCRHI, blksize >> 8);

	/* Set up and enable Audio 1 DMA FIFO. */
	a1c1 = ESO_CTLREG_A1C1_RESV1 | ESO_CTLREG_A1C1_FIFOENB;
	if (param->precision == 16)
		a1c1 |= ESO_CTLREG_A1C1_16BIT;
	if (param->channels == 2)
		a1c1 |= ESO_CTLREG_A1C1_STEREO;
	else
		a1c1 |= ESO_CTLREG_A1C1_MONO;
	if (param->encoding == AUDIO_ENCODING_SLINEAR_BE ||
	    param->encoding == AUDIO_ENCODING_SLINEAR_LE)
		a1c1 |= ESO_CTLREG_A1C1_SIGNED;
	eso_write_ctlreg(sc, ESO_CTLREG_A1C1, a1c1);

	/* Set up ADC IRQ/DRQ parameters. */
	eso_write_ctlreg(sc, ESO_CTLREG_LAIC,
	    ESO_CTLREG_LAIC_PINENB | ESO_CTLREG_LAIC_EXTENB);
	eso_write_ctlreg(sc, ESO_CTLREG_DRQCTL,
	    ESO_CTLREG_DRQCTL_ENB1 | ESO_CTLREG_DRQCTL_EXTENB);

	/* Set up and enable DMA controller. */
	bus_space_write_1(sc->sc_dmac_iot, sc->sc_dmac_ioh, ESO_DMAC_CLEAR, 0);
	bus_space_write_1(sc->sc_dmac_iot, sc->sc_dmac_ioh, ESO_DMAC_MASK,
	    ESO_DMAC_MASK_MASK);
	bus_space_write_1(sc->sc_dmac_iot, sc->sc_dmac_ioh, ESO_DMAC_MODE,
	    DMA37MD_WRITE | DMA37MD_LOOP | DMA37MD_DEMAND);
	bus_space_write_4(sc->sc_dmac_iot, sc->sc_dmac_ioh, ESO_DMAC_DMAA,
	    DMAADDR(ed));
	bus_space_write_2(sc->sc_dmac_iot, sc->sc_dmac_ioh, ESO_DMAC_DMAC,
	    (uint8_t *)end - (uint8_t *)start - 1);
	bus_space_write_1(sc->sc_dmac_iot, sc->sc_dmac_ioh, ESO_DMAC_MASK, 0);

	/* Start DMA. */
	mtx_enter(&audio_lock);
	eso_write_ctlreg(sc, ESO_CTLREG_A1C2,
	    ESO_CTLREG_A1C2_DMAENB | ESO_CTLREG_A1C2_READ |
	    ESO_CTLREG_A1C2_AUTO | ESO_CTLREG_A1C2_ADC);
	mtx_leave(&audio_lock);
	return (0);
}

/*
 * Mixer utility functions.
 */
int
eso_set_recsrc(struct eso_softc *sc, u_int recsrc)
{
	mixer_devinfo_t di;
	int i, error;

	di.index = ESO_RECORD_SOURCE;
	error = eso_query_devinfo(sc, &di);
	if (error != 0) {
		printf("eso_set_recsrc: eso_query_devinfo failed");
		return (error);
	}

	for (i = 0; i < di.un.e.num_mem; i++) {
		if (recsrc == di.un.e.member[i].ord) {
			eso_write_mixreg(sc, ESO_MIXREG_ERS, recsrc);
			sc->sc_recsrc = recsrc;
			return (0);
		}
	}

	return (EINVAL);
}

int
eso_set_monooutsrc(struct eso_softc *sc, uint monooutsrc)
{
	mixer_devinfo_t di;
	int i, error;
	uint8_t mpm;

	di.index = ESO_MONOOUT_SOURCE;
	error = eso_query_devinfo(sc, &di);
	if (error != 0) {
		printf("eso_set_monooutsrc: eso_query_devinfo failed");
		return (error);
	}

	for (i = 0; i < di.un.e.num_mem; i++) {
		if (monooutsrc == di.un.e.member[i].ord) {
			mpm = eso_read_mixreg(sc, ESO_MIXREG_MPM);
			mpm &= ~ESO_MIXREG_MPM_MOMASK;
			mpm |= monooutsrc;
			eso_write_mixreg(sc, ESO_MIXREG_MPM, mpm);
			sc->sc_monooutsrc = monooutsrc;
			return (0);
		}
	}

	return (EINVAL);
}

int
eso_set_monoinbypass(struct eso_softc *sc, uint monoinbypass)
{
	mixer_devinfo_t di;
	int i, error;
	uint8_t mpm;

	di.index = ESO_MONOIN_BYPASS;
	error = eso_query_devinfo(sc, &di);
	if (error != 0) {
		printf("eso_set_monoinbypass: eso_query_devinfo failed");
		return (error);
	}

	for (i = 0; i < di.un.e.num_mem; i++) {
		if (monoinbypass == di.un.e.member[i].ord) {
			mpm = eso_read_mixreg(sc, ESO_MIXREG_MPM);
			mpm &= ~(ESO_MIXREG_MPM_MOMASK | ESO_MIXREG_MPM_RESV0);
			mpm |= (monoinbypass ? ESO_MIXREG_MPM_MIBYPASS : 0);
			eso_write_mixreg(sc, ESO_MIXREG_MPM, mpm);
			sc->sc_monoinbypass = monoinbypass;
			return (0);
		}
	}

	return (EINVAL);
}

int
eso_set_preamp(struct eso_softc *sc, uint preamp)
{
	mixer_devinfo_t di;
	int i, error;
	uint8_t mpm;

	di.index = ESO_MIC_PREAMP;
	error = eso_query_devinfo(sc, &di);
	if (error != 0) {
		printf("eso_set_preamp: eso_query_devinfo failed");
		return (error);
	}

	for (i = 0; i < di.un.e.num_mem; i++) {
		if (preamp == di.un.e.member[i].ord) {
			mpm = eso_read_mixreg(sc, ESO_MIXREG_MPM);
			mpm &= ~(ESO_MIXREG_MPM_PREAMP | ESO_MIXREG_MPM_RESV0);
			mpm |= (preamp ? ESO_MIXREG_MPM_PREAMP : 0);
			eso_write_mixreg(sc, ESO_MIXREG_MPM, mpm);
			sc->sc_preamp = preamp;
			return (0);
		}
	}

	return (EINVAL);
}

/*
 * Reload Master Volume and Mute values in softc from mixer; used when
 * those have previously been invalidated by use of hardware volume controls.
 */
void
eso_reload_master_vol(struct eso_softc *sc)
{
	uint8_t mv;

	mv = eso_read_mixreg(sc, ESO_MIXREG_LMVM);
	sc->sc_gain[ESO_MASTER_VOL][ESO_LEFT] =
	    (mv & ~ESO_MIXREG_LMVM_MUTE) << 2;
	mv = eso_read_mixreg(sc, ESO_MIXREG_LMVM);
	sc->sc_gain[ESO_MASTER_VOL][ESO_RIGHT] =
	    (mv & ~ESO_MIXREG_RMVM_MUTE) << 2;
	/* Currently both channels are muted simultaneously; either is OK. */
	sc->sc_mvmute = (mv & ESO_MIXREG_RMVM_MUTE) != 0;
}

void
eso_set_gain(struct eso_softc *sc, uint port)
{
	uint8_t mixreg, tmp;

	switch (port) {
	case ESO_DAC_PLAY_VOL:
		mixreg = ESO_MIXREG_PVR_A2;
		break;
	case ESO_MIC_PLAY_VOL:
		mixreg = ESO_MIXREG_PVR_MIC;
		break;
	case ESO_LINE_PLAY_VOL:
		mixreg = ESO_MIXREG_PVR_LINE;
		break;
	case ESO_SYNTH_PLAY_VOL:
		mixreg = ESO_MIXREG_PVR_SYNTH;
		break;
	case ESO_CD_PLAY_VOL:
		mixreg = ESO_MIXREG_PVR_CD;
		break;
	case ESO_AUXB_PLAY_VOL:
		mixreg = ESO_MIXREG_PVR_AUXB;
		break;
	case ESO_DAC_REC_VOL:
		mixreg = ESO_MIXREG_RVR_A2;
		break;
	case ESO_MIC_REC_VOL:
		mixreg = ESO_MIXREG_RVR_MIC;
		break;
	case ESO_LINE_REC_VOL:
		mixreg = ESO_MIXREG_RVR_LINE;
		break;
	case ESO_SYNTH_REC_VOL:
		mixreg = ESO_MIXREG_RVR_SYNTH;
		break;
	case ESO_CD_REC_VOL:
		mixreg = ESO_MIXREG_RVR_CD;
		break;
	case ESO_AUXB_REC_VOL:
		mixreg = ESO_MIXREG_RVR_AUXB;
		break;
	case ESO_MONO_PLAY_VOL:
		mixreg = ESO_MIXREG_PVR_MONO;
		break;
	case ESO_MONO_REC_VOL:
		mixreg = ESO_MIXREG_RVR_MONO;
		break;
	case ESO_PCSPEAKER_VOL:
		/* Special case - only 3-bit, mono, and reserved bits. */
		tmp = eso_read_mixreg(sc, ESO_MIXREG_PCSVR);
		tmp &= ESO_MIXREG_PCSVR_RESV;
		/* Map bits 7:5 -> 2:0. */
		tmp |= (sc->sc_gain[port][ESO_LEFT] >> 5);
		eso_write_mixreg(sc, ESO_MIXREG_PCSVR, tmp);
		return;
	case ESO_MASTER_VOL:
		/* Special case - separate regs, and 6-bit precision. */
		/* Map bits 7:2 -> 5:0, reflect mute settings. */
		eso_write_mixreg(sc, ESO_MIXREG_LMVM,
		    (sc->sc_gain[port][ESO_LEFT] >> 2) |
		    (sc->sc_mvmute ? ESO_MIXREG_LMVM_MUTE : 0x00));
		eso_write_mixreg(sc, ESO_MIXREG_RMVM,
		    (sc->sc_gain[port][ESO_RIGHT] >> 2) |
		    (sc->sc_mvmute ? ESO_MIXREG_RMVM_MUTE : 0x00));
		return;
	case ESO_SPATIALIZER:
		/* Special case - only `mono', and higher precision. */
		eso_write_mixreg(sc, ESO_MIXREG_SPATLVL,
		    sc->sc_gain[port][ESO_LEFT]);
		return;
	case ESO_RECORD_VOL:
		/* Very Special case, controller register. */
		eso_write_ctlreg(sc, ESO_CTLREG_RECLVL,ESO_4BIT_GAIN_TO_STEREO(
		   sc->sc_gain[port][ESO_LEFT], sc->sc_gain[port][ESO_RIGHT]));
		return;
	default:
#ifdef DIAGNOSTIC		
		printf("eso_set_gain: bad port %u", port);
		return;
		/* NOTREACHED */
#else
		return;
#endif		
		}

	eso_write_mixreg(sc, mixreg, ESO_4BIT_GAIN_TO_STEREO(
	    sc->sc_gain[port][ESO_LEFT], sc->sc_gain[port][ESO_RIGHT]));
}

int
eso_activate(struct device *self, int act)
{
	struct eso_softc *sc = (struct eso_softc *)self;
	uint8_t tmp;
	int rv = 0;

	switch (act) {
	case DVACT_QUIESCE:
		rv = config_activate_children(self, act);
		tmp = bus_space_read_1(sc->sc_iot, sc->sc_ioh, ESO_IO_IRQCTL);
		tmp &= ~(ESO_IO_IRQCTL_MASK);
		bus_space_write_1(sc->sc_iot, sc->sc_ioh, ESO_IO_IRQCTL, tmp);
		break;
	case DVACT_SUSPEND:
		rv = config_activate_children(self, act);
		bus_space_write_1(sc->sc_iot, sc->sc_ioh, ESO_IO_A2DMAM, 0);
		bus_space_write_1(sc->sc_dmac_iot, sc->sc_dmac_ioh,
		    ESO_DMAC_CLEAR, 0);
		bus_space_write_1(sc->sc_sb_iot, sc->sc_sb_ioh,
		    ESO_SB_STATUSFLAGS, 3);
		/* shut down dma */
		pci_conf_write(sc->sc_pa.pa_pc, sc->sc_pa.pa_tag,
		    ESO_PCI_DDMAC, 0);
		break;
	case DVACT_RESUME:
		eso_setup(sc, 1, 1);
		pci_conf_write(sc->sc_pa.pa_pc, sc->sc_pa.pa_tag,
		    ESO_PCI_DDMAC, sc->sc_dmac_addr | ESO_PCI_DDMAC_DE);
		rv = config_activate_children(self, act);
		break;
	default:
		rv = config_activate_children(self, act);
		break;
	}
	return (rv);
}
