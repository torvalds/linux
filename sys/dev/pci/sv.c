/*      $OpenBSD: sv.c,v 1.45 2024/06/22 10:22:29 jsg Exp $ */

/*
 * Copyright (c) 1998 Constantine Paul Sapuntzakis
 * All rights reserved
 *
 * Author: Constantine Paul Sapuntzakis (csapuntz@cvs.openbsd.org)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The author's name or those of the contributors may be used to
 *    endorse or promote products derived from this software without 
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) AND CONTRIBUTORS
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
 * S3 SonicVibes driver
 *   Heavily based on the eap driver by Lennart Augustsson
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/device.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <sys/audioio.h>
#include <dev/audio_if.h>

#include <dev/ic/i8237reg.h>
#include <dev/ic/s3_617.h>


#include <machine/bus.h>

struct cfdriver sv_cd = {
	NULL, "sv", DV_DULL
};

#ifdef AUDIO_DEBUG
#define DPRINTF(x)	if (svdebug) printf x
#define DPRINTFN(n,x)	if (svdebug>(n)) printf x
static int	svdebug = 100;
#else
#define DPRINTF(x)
#define DPRINTFN(n,x)
#endif

int	sv_match(struct device *, void *, void *);
static void	sv_attach(struct device *, struct device *, void *);
int	sv_intr(void *);

struct sv_dma {
	bus_dmamap_t map;
        caddr_t addr;
        bus_dma_segment_t segs[1];
        int nsegs;
        size_t size;
        struct sv_dma *next;
};
#define DMAADDR(map) ((map)->segs[0].ds_addr)
#define KERNADDR(map) ((void *)((map)->addr))

enum {
  SV_DMAA_CONFIGURED = 1,
  SV_DMAC_CONFIGURED = 2,
  SV_DMAA_TRIED_CONFIGURE = 4,
  SV_DMAC_TRIED_CONFIGURE = 8
};

struct sv_softc {
	struct device sc_dev;		/* base device */
	void *sc_ih;			/* interrupt vectoring */

        pci_chipset_tag_t sc_pci_chipset_tag;
        pcitag_t  sc_pci_tag;

	bus_space_tag_t sc_iot;
	bus_space_handle_t sc_ioh;
	bus_space_handle_t sc_dmaa_ioh;
	bus_space_handle_t sc_dmac_ioh;
	bus_dma_tag_t sc_dmatag;	/* DMA tag */

        struct sv_dma *sc_dmas;

	void	(*sc_pintr)(void *);	/* dma completion intr handler */
	void	*sc_parg;		/* arg for sc_intr() */

	void	(*sc_rintr)(void *);	/* dma completion intr handler */
	void	*sc_rarg;		/* arg for sc_intr() */
	char	sc_enable;
        char    sc_trd;

        char    sc_dma_configured;
        u_int	sc_record_source;	/* recording source mask */
};


const struct cfattach sv_ca = {
	sizeof(struct sv_softc), sv_match, sv_attach
};

#define ARRAY_SIZE(foo)  ((sizeof(foo)) / sizeof(foo[0]))

int	sv_allocmem(struct sv_softc *, size_t, size_t, struct sv_dma *);
int	sv_freemem(struct sv_softc *, struct sv_dma *);

int	sv_open(void *, int);
void	sv_close(void *);
int	sv_set_params(void *, int, int, struct audio_params *, struct audio_params *);
int	sv_round_blocksize(void *, int);
int	sv_dma_init_output(void *, void *, int);
int	sv_dma_init_input(void *, void *, int);
int	sv_dma_output(void *, void *, int, void (*)(void *), void *);
int	sv_dma_input(void *, void *, int, void (*)(void *), void *);
int	sv_halt_in_dma(void *);
int	sv_halt_out_dma(void *);
int	sv_mixer_set_port(void *, mixer_ctrl_t *);
int	sv_mixer_get_port(void *, mixer_ctrl_t *);
int	sv_query_devinfo(void *, mixer_devinfo_t *);
void   *sv_malloc(void *, int, size_t, int, int);
void	sv_free(void *, void *, int);

void    sv_dumpregs(struct sv_softc *sc);

const struct audio_hw_if sv_hw_if = {
	.open = sv_open,
	.close = sv_close,
	.set_params = sv_set_params,
	.round_blocksize = sv_round_blocksize,
	.init_output = sv_dma_init_output,
	.init_input = sv_dma_init_input,
	.start_output = sv_dma_output,
	.start_input = sv_dma_input,
	.halt_output = sv_halt_out_dma,
	.halt_input = sv_halt_in_dma,
	.set_port = sv_mixer_set_port,
	.get_port = sv_mixer_get_port,
	.query_devinfo = sv_query_devinfo,
	.allocm = sv_malloc,
	.freem = sv_free,
};


static __inline__ u_int8_t sv_read(struct sv_softc *, u_int8_t);
static __inline__ u_int8_t sv_read_indirect(struct sv_softc *, u_int8_t);
static __inline__ void sv_write(struct sv_softc *, u_int8_t, u_int8_t );
static __inline__ void sv_write_indirect(struct sv_softc *, u_int8_t, u_int8_t );
static void sv_init_mixer(struct sv_softc *);

static __inline__ void
sv_write(struct sv_softc *sc, u_int8_t reg, u_int8_t val)
{
  bus_space_write_1(sc->sc_iot, sc->sc_ioh, reg, val);
}

static __inline__ u_int8_t
sv_read(struct sv_softc *sc, u_int8_t reg)
{
  return (bus_space_read_1(sc->sc_iot, sc->sc_ioh, reg));
}

static __inline__ u_int8_t
sv_read_indirect(struct sv_softc *sc, u_int8_t reg)
{
    u_int8_t iaddr = 0;

    if (sc->sc_trd > 0)
      iaddr |= SV_IADDR_TRD;

    iaddr |= (reg & SV_IADDR_MASK);
    sv_write (sc, SV_CODEC_IADDR, iaddr);

    return (sv_read(sc, SV_CODEC_IDATA));
}

static __inline__ void
sv_write_indirect(struct sv_softc *sc, u_int8_t reg, u_int8_t val)
{
    u_int8_t iaddr = 0;
#ifdef DIAGNOSTIC
    if (reg > 0x3f) {
      printf ("Invalid register\n");
      return;
    }
#endif

    if (reg == SV_DMA_DATA_FORMAT)
      iaddr |= SV_IADDR_MCE;

    if (sc->sc_trd > 0)
      iaddr |= SV_IADDR_TRD;

    iaddr |= (reg & SV_IADDR_MASK);
    sv_write (sc, SV_CODEC_IADDR, iaddr);
    sv_write (sc, SV_CODEC_IDATA, val);
}

int
sv_match(struct device *parent, void *match, void *aux)
{
	struct pci_attach_args *pa = aux;

	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_S3 &&
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_S3_SONICVIBES)
	  return (1);

	return (0);
}

static void
sv_attach(struct device *parent, struct device *self, void *aux)
{
  struct sv_softc *sc = (struct sv_softc *)self;
  struct pci_attach_args *pa = aux;
  pci_chipset_tag_t pc = pa->pa_pc;
  pci_intr_handle_t ih;
  bus_size_t iosize;
  char const *intrstr;
  u_int32_t  dmareg, dmaio; 
  u_int8_t   reg;

  sc->sc_pci_chipset_tag = pc;
  sc->sc_pci_tag = pa->pa_tag;

  /* Map the enhanced port only */
  if (pci_mapreg_map(pa, SV_ENHANCED_PORTBASE_SLOT, PCI_MAPREG_TYPE_IO, 0,
      &sc->sc_iot, &sc->sc_ioh, NULL, &iosize, 0)) {
    printf (": Couldn't map enhanced synth I/O range\n");
    return;
  }

  sc->sc_dmatag = pa->pa_dmat;

  dmareg = pci_conf_read(pa->pa_pc, pa->pa_tag, SV_DMAA_CONFIG_OFF);
  iosize = 0x10;
  dmaio =  dmareg & ~(iosize - 1);
  
  if (dmaio) {
    dmareg &= 0xF;
    
    if (bus_space_map(sc->sc_iot, dmaio, iosize, 0, &sc->sc_dmaa_ioh)) {
      /* The BIOS assigned us some bad I/O address! Make sure to clear
         and disable this DMA before we enable the device */
      pci_conf_write(pa->pa_pc, pa->pa_tag, SV_DMAA_CONFIG_OFF, 0);

      printf (": can't map DMA i/o space\n");
      goto enable;
    }

    pci_conf_write(pa->pa_pc, pa->pa_tag, SV_DMAA_CONFIG_OFF,
		   dmaio | dmareg | 
		   SV_DMA_CHANNEL_ENABLE | SV_DMAA_EXTENDED_ADDR);
    sc->sc_dma_configured |= SV_DMAA_CONFIGURED;
  }

  dmareg = pci_conf_read(pa->pa_pc, pa->pa_tag, SV_DMAC_CONFIG_OFF);
  dmaio = dmareg & ~(iosize - 1);
  if (dmaio) {
    dmareg &= 0xF;

    if (bus_space_map(sc->sc_iot, dmaio, iosize, 0, &sc->sc_dmac_ioh)) {
      /* The BIOS assigned us some bad I/O address! Make sure to clear
         and disable this DMA before we enable the device */
      pci_conf_write (pa->pa_pc, pa->pa_tag, SV_DMAC_CONFIG_OFF, 
		      dmareg & ~SV_DMA_CHANNEL_ENABLE); 
      printf (": can't map DMA i/o space\n");
      goto enable;
    }

    pci_conf_write(pa->pa_pc, pa->pa_tag, SV_DMAC_CONFIG_OFF, 
		   dmaio | dmareg | SV_DMA_CHANNEL_ENABLE);
    sc->sc_dma_configured |= SV_DMAC_CONFIGURED;
  }

  /* Enable the device. */
 enable:
  sv_write_indirect(sc, SV_ANALOG_POWER_DOWN_CONTROL, 0);
  sv_write_indirect(sc, SV_DIGITAL_POWER_DOWN_CONTROL, 0);

  /* initialize codec registers */
  reg = sv_read(sc, SV_CODEC_CONTROL);
  reg |= SV_CTL_RESET;
  sv_write(sc, SV_CODEC_CONTROL, reg);
  delay(50);

  reg = sv_read(sc, SV_CODEC_CONTROL);
  reg &= ~SV_CTL_RESET;
  reg |= SV_CTL_INTA | SV_CTL_ENHANCED;

  /* This write clears the reset */
  sv_write(sc, SV_CODEC_CONTROL, reg);
  delay(50);

  /* This write actually shoves the new values in */
  sv_write(sc, SV_CODEC_CONTROL, reg);

  DPRINTF (("reg: %x\n", sv_read(sc, SV_CODEC_CONTROL)));

  /* Enable DMA interrupts */
  reg = sv_read(sc, SV_CODEC_INTMASK);
  reg &= ~(SV_INTMASK_DMAA | SV_INTMASK_DMAC);
  reg |= SV_INTMASK_UD | SV_INTMASK_SINT | SV_INTMASK_MIDI;
  sv_write(sc, SV_CODEC_INTMASK, reg);

  sv_read(sc, SV_CODEC_STATUS);

  sc->sc_trd = 0;
  sc->sc_enable = 0;

  /* Map and establish the interrupt. */
  if (pci_intr_map(pa, &ih)) {
    printf(": couldn't map interrupt\n");
    return;
  }
  intrstr = pci_intr_string(pc, ih);
  sc->sc_ih = pci_intr_establish(pc, ih, IPL_AUDIO | IPL_MPSAFE,
      sv_intr, sc, sc->sc_dev.dv_xname);
  if (sc->sc_ih == NULL) {
    printf(": couldn't establish interrupt");
    if (intrstr != NULL)
      printf(" at %s", intrstr);
    printf("\n");
    return;
  }
  printf(": %s\n", intrstr);

  sv_init_mixer(sc);

  audio_attach_mi(&sv_hw_if, sc, NULL, &sc->sc_dev);
}

#ifdef AUDIO_DEBUG
void
sv_dumpregs(struct sv_softc *sc)
{
  int idx;

  { int idx;
  for (idx = 0; idx < 0x50; idx += 4) {
    printf ("%02x = %x\n", idx, pci_conf_read(sc->sc_pci_chipset_tag,
            sc->sc_pci_tag, idx));
  }
  }

  for (idx = 0; idx < 6; idx++) {
    printf ("REG %02x = %02x\n", idx, sv_read(sc, idx));
  }

  for (idx = 0; idx < 0x32; idx++) {
    printf ("IREG %02x = %02x\n", idx, sv_read_indirect(sc, idx));
  }

  for (idx = 0; idx < 0x10; idx++) {
    printf ("DMA %02x = %02x\n", idx, 
	    bus_space_read_1(sc->sc_iot, sc->sc_dmaa_ioh, idx));
  }

  return;
}
#endif

int
sv_intr(void *p)
{
  struct sv_softc *sc = p;
  u_int8_t intr;

  mtx_enter(&audio_lock);
  intr = sv_read(sc, SV_CODEC_STATUS);

  if (!(intr & (SV_INTSTATUS_DMAA | SV_INTSTATUS_DMAC))) {
    mtx_leave(&audio_lock);
    return (0);
  }

  if (intr & SV_INTSTATUS_DMAA) {
    if (sc->sc_pintr)
      sc->sc_pintr(sc->sc_parg);
  }

  if (intr & SV_INTSTATUS_DMAC) {
    if (sc->sc_rintr)
      sc->sc_rintr(sc->sc_rarg);
  }
  mtx_leave(&audio_lock);
  return (1);
}

int
sv_allocmem(struct sv_softc *sc, size_t size, size_t align, struct sv_dma *p)
{
	int error;

	p->size = size;
	error = bus_dmamem_alloc(sc->sc_dmatag, p->size, align, 0,
				 p->segs, ARRAY_SIZE(p->segs),
				 &p->nsegs, BUS_DMA_NOWAIT);
	if (error)
		return (error);

	error = bus_dmamem_map(sc->sc_dmatag, p->segs, p->nsegs, p->size, 
			       &p->addr, BUS_DMA_NOWAIT|BUS_DMA_COHERENT);
	if (error)
		goto free;

	error = bus_dmamap_create(sc->sc_dmatag, p->size, 1, p->size,
				  0, BUS_DMA_NOWAIT, &p->map);
	if (error)
		goto unmap;

	error = bus_dmamap_load(sc->sc_dmatag, p->map, p->addr, p->size, NULL, 
				BUS_DMA_NOWAIT);
	if (error)
		goto destroy;
	return (0);

destroy:
	bus_dmamap_destroy(sc->sc_dmatag, p->map);
unmap:
	bus_dmamem_unmap(sc->sc_dmatag, p->addr, p->size);
free:
	bus_dmamem_free(sc->sc_dmatag, p->segs, p->nsegs);
	return (error);
}

int
sv_freemem(struct sv_softc *sc, struct sv_dma *p)
{
	bus_dmamap_unload(sc->sc_dmatag, p->map);
	bus_dmamap_destroy(sc->sc_dmatag, p->map);
	bus_dmamem_unmap(sc->sc_dmatag, p->addr, p->size);
	bus_dmamem_free(sc->sc_dmatag, p->segs, p->nsegs);
	return (0);
}

int
sv_open(void *addr, int flags)
{

    struct sv_softc *sc = addr;
    int  intr_mask = 0;
    u_int8_t reg;

    /* Map the DMA channels, if necessary */
    if (!(sc->sc_dma_configured & SV_DMAA_CONFIGURED)) {
	/* XXX - there seems to be no general way to find an
	   I/O range */
	int dmaio;
	int iosize = 0x10;

	if (sc->sc_dma_configured & SV_DMAA_TRIED_CONFIGURE)
	    return (ENXIO);

	for (dmaio = 0xa000; dmaio < 0xb000; dmaio += iosize) {
	    if (!bus_space_map(sc->sc_iot, dmaio, iosize, 0, 
			      &sc->sc_dmaa_ioh)) {
		goto found_dmaa;
	    }
	}

	sc->sc_dma_configured |= SV_DMAA_TRIED_CONFIGURE;
	return (ENXIO);
    found_dmaa:
	  
	pci_conf_write(sc->sc_pci_chipset_tag, sc->sc_pci_tag,
		       SV_DMAA_CONFIG_OFF, 
		       dmaio | SV_DMA_CHANNEL_ENABLE 
		       | SV_DMAA_EXTENDED_ADDR);

	sc->sc_dma_configured |= SV_DMAA_CONFIGURED;
	intr_mask = 1;
    }

    if (!(sc->sc_dma_configured & SV_DMAC_CONFIGURED)) {
	/* XXX - there seems to be no general way to find an
	   I/O range */
	int dmaio;
	int iosize = 0x10;

	if (sc->sc_dma_configured & SV_DMAC_TRIED_CONFIGURE)
	    return (ENXIO);

	for (dmaio = 0xa000; dmaio < 0xb000; dmaio += iosize) {
	    if (!bus_space_map(sc->sc_iot, dmaio, iosize, 0, 
			      &sc->sc_dmac_ioh)) {
		goto found_dmac;
	    }
	}

	sc->sc_dma_configured |= SV_DMAC_TRIED_CONFIGURE;	    
	return (ENXIO);
    found_dmac:
	  
	pci_conf_write(sc->sc_pci_chipset_tag, sc->sc_pci_tag,
		       SV_DMAC_CONFIG_OFF, 
		       dmaio | SV_DMA_CHANNEL_ENABLE);

	sc->sc_dma_configured |= SV_DMAC_CONFIGURED;
	intr_mask = 1;
    }

    /* Make sure DMA interrupts are enabled */
    if (intr_mask) {
	reg = sv_read(sc, SV_CODEC_INTMASK);
	reg &= ~(SV_INTMASK_DMAA | SV_INTMASK_DMAC);
	reg |= SV_INTMASK_UD | SV_INTMASK_SINT | SV_INTMASK_MIDI;
	sv_write(sc, SV_CODEC_INTMASK, reg);
    }

    sc->sc_pintr = 0;
    sc->sc_rintr = 0;

    return (0);
}

/*
 * Close function is called at splaudio().
 */
void
sv_close(void *addr)
{
	struct sv_softc *sc = addr;
    
        sv_halt_in_dma(sc);
        sv_halt_out_dma(sc);

        sc->sc_pintr = 0;
        sc->sc_rintr = 0;
}

int
sv_set_params(void *addr, int setmode, int usemode,
    struct audio_params *p, struct audio_params *r)
{
	struct sv_softc *sc = addr;
        u_int32_t mode, val;
        u_int8_t reg;
	
        switch (p->encoding) {
        case AUDIO_ENCODING_SLINEAR_LE:
        	if (p->precision != 16)
			return EINVAL;
        	break;
        case AUDIO_ENCODING_ULINEAR_BE:
        case AUDIO_ENCODING_ULINEAR_LE:
        	if (p->precision != 8)
			return EINVAL;
        	break;
        default:
        	return (EINVAL);
        }

	if (p->precision == 16)
		mode = SV_DMAA_FORMAT16 | SV_DMAC_FORMAT16;
	else
		mode = 0;
	if (p->channels > 2)
		p->channels = 2;
        if (p->channels == 2)
        	mode |= SV_DMAA_STEREO | SV_DMAC_STEREO;
        if (p->sample_rate < 2000)
		p->sample_rate = 2000;
	if (p->sample_rate > 48000)
		p->sample_rate = 48000;

	p->bps = AUDIO_BPS(p->precision);
	r->bps = AUDIO_BPS(r->precision);
	p->msb = r->msb = 1;

        /* Set the encoding */
	reg = sv_read_indirect(sc, SV_DMA_DATA_FORMAT);
	reg &= ~(SV_DMAA_FORMAT16 | SV_DMAC_FORMAT16 | SV_DMAA_STEREO |
		 SV_DMAC_STEREO);
	reg |= (mode);
	sv_write_indirect(sc, SV_DMA_DATA_FORMAT, reg);

	val = p->sample_rate * 65536 / 48000;

	sv_write_indirect(sc, SV_PCM_SAMPLE_RATE_0, (val & 0xff));
	sv_write_indirect(sc, SV_PCM_SAMPLE_RATE_1, (val >> 8));

#define F_REF 24576000

	if (setmode & AUMODE_RECORD)
	{
	  /* The ADC reference frequency (f_out) is 512 * the sample rate */

	  /* f_out is derived from the 24.576MHZ crystal by three values:
	     M & N & R. The equation is as follows:

	     f_out = (m + 2) * f_ref / ((n + 2) * (2 ^ a))

	     with the constraint that:

	     80 MHz < (m + 2) / (n + 2) * f_ref <= 150MHz
	     and n, m >= 1
	  */

	  int  goal_f_out = 512 * r->sample_rate;
	  int  a, n, m, best_n, best_m, best_error = 10000000;
	  int  pll_sample;

	  for (a = 0; a < 8; a++) {
	    if ((goal_f_out * (1 << a)) >= 80000000)
	      break;
	  }
	  
	  /* a != 8 because sample_rate >= 2000 */

	  for (n = 33; n > 2; n--) {
	    int error;

	    m = (goal_f_out * n * (1 << a)) / F_REF;

	    if ((m > 257) || (m < 3)) continue;
 
	    pll_sample = (m * F_REF) / (n * (1 << a));
	    pll_sample /= 512;

	    /* Threshold might be good here */
	    error = pll_sample - r->sample_rate;
	    error = abs(error);
	    
	    if (error < best_error) {
	      best_error = error;
	      best_n = n;
	      best_m = m;
	      if (error == 0) break;
	    }
	  }
	

	  best_n -= 2;
	  best_m -= 2;
	  
	  sv_write_indirect(sc, SV_ADC_PLL_M, best_m);
	  sv_write_indirect(sc, SV_ADC_PLL_N, best_n | (a << SV_PLL_R_SHIFT));
	}
        return (0);
}

int
sv_round_blocksize(void *addr, int blk)
{
	return ((blk + 31) & -32);	/* keep good alignment */
}

int
sv_dma_init_input(void *addr, void *buf, int cc)
{
	struct sv_softc *sc = addr;
	struct sv_dma *p;
	int dma_count;

	DPRINTF(("sv_dma_init_input: dma start loop input addr=%p cc=%d\n", 
		 buf, cc));
        for (p = sc->sc_dmas; p && KERNADDR(p) != buf; p = p->next)
		;
	if (!p) {
		printf("sv_dma_init_input: bad addr %p\n", buf);
		return (EINVAL);
	}

	dma_count = (cc >> 1) - 1;

	bus_space_write_4(sc->sc_iot, sc->sc_dmac_ioh, SV_DMA_ADDR0,
			  DMAADDR(p));
	bus_space_write_4(sc->sc_iot, sc->sc_dmac_ioh, SV_DMA_COUNT0,
			  dma_count);
	bus_space_write_1(sc->sc_iot, sc->sc_dmac_ioh, SV_DMA_MODE,
			  DMA37MD_WRITE | DMA37MD_LOOP);

	return (0);
}

int
sv_dma_init_output(void *addr, void *buf, int cc)
{
	struct sv_softc *sc = addr;
	struct sv_dma *p;
	int dma_count;

	DPRINTF(("sv: dma start loop output buf=%p cc=%d\n", buf, cc));
        for (p = sc->sc_dmas; p && KERNADDR(p) != buf; p = p->next)
		;
	if (!p) {
		printf("sv_dma_init_output: bad addr %p\n", buf);
		return (EINVAL);
	}

	dma_count = cc - 1;

	bus_space_write_4(sc->sc_iot, sc->sc_dmaa_ioh, SV_DMA_ADDR0,
			  DMAADDR(p));
	bus_space_write_4(sc->sc_iot, sc->sc_dmaa_ioh, SV_DMA_COUNT0,
			  dma_count);
	bus_space_write_1(sc->sc_iot, sc->sc_dmaa_ioh, SV_DMA_MODE,
			  DMA37MD_READ | DMA37MD_LOOP);

	return (0);
}

int
sv_dma_output(void *addr, void *p, int cc, void (*intr)(void *), void *arg)
{
	struct sv_softc *sc = addr;
	u_int8_t mode;

	DPRINTFN(1, 
                 ("sv_dma_output: sc=%p buf=%p cc=%d intr=%p(%p)\n", 
                  addr, p, cc, intr, arg));
	sc->sc_pintr = intr;
	sc->sc_parg = arg;
	if (!(sc->sc_enable & SV_PLAY_ENABLE)) {
	        int dma_count = cc - 1;

		sv_write_indirect(sc, SV_DMAA_COUNT1, dma_count >> 8);
		sv_write_indirect(sc, SV_DMAA_COUNT0, (dma_count & 0xFF));

		mode = sv_read_indirect(sc, SV_PLAY_RECORD_ENABLE);
		mode |= SV_PLAY_ENABLE;
		sv_write_indirect(sc, SV_PLAY_RECORD_ENABLE, mode);
		sc->sc_enable |= SV_PLAY_ENABLE;
	}
        return (0);
}

int
sv_dma_input(void *addr, void *p, int cc, void (*intr)(void *), void *arg)
{
	struct sv_softc *sc = addr;
	u_int8_t mode;

	DPRINTFN(1, ("sv_dma_input: sc=%p buf=%p cc=%d intr=%p(%p)\n", 
		     addr, p, cc, intr, arg));
	sc->sc_rintr = intr;
	sc->sc_rarg = arg;
	if (!(sc->sc_enable & SV_RECORD_ENABLE)) {
	        int dma_count = (cc >> 1) - 1;

		sv_write_indirect(sc, SV_DMAC_COUNT1, dma_count >> 8);
		sv_write_indirect(sc, SV_DMAC_COUNT0, (dma_count & 0xFF));

		mode = sv_read_indirect(sc, SV_PLAY_RECORD_ENABLE);
		mode |= SV_RECORD_ENABLE;
		sv_write_indirect(sc, SV_PLAY_RECORD_ENABLE, mode);
		sc->sc_enable |= SV_RECORD_ENABLE;
	}
        return (0);
}

int
sv_halt_out_dma(void *addr)
{
	struct sv_softc *sc = addr;
	u_int8_t mode;
	
        DPRINTF(("sv: sv_halt_out_dma\n"));
	mtx_enter(&audio_lock);
	mode = sv_read_indirect(sc, SV_PLAY_RECORD_ENABLE);
	mode &= ~SV_PLAY_ENABLE;
	sc->sc_enable &= ~SV_PLAY_ENABLE;
	sv_write_indirect(sc, SV_PLAY_RECORD_ENABLE, mode);
	mtx_leave(&audio_lock);
        return (0);
}

int
sv_halt_in_dma(void *addr)
{
	struct sv_softc *sc = addr;
	u_int8_t mode;
    
        DPRINTF(("sv: sv_halt_in_dma\n"));
	mtx_enter(&audio_lock);
	mode = sv_read_indirect(sc, SV_PLAY_RECORD_ENABLE);
	mode &= ~SV_RECORD_ENABLE;
	sc->sc_enable &= ~SV_RECORD_ENABLE;
	sv_write_indirect(sc, SV_PLAY_RECORD_ENABLE, mode);
	mtx_leave(&audio_lock);
        return (0);
}

/*
 * Mixer related code is here
 *
 */

#define SV_INPUT_CLASS 0
#define SV_OUTPUT_CLASS 1
#define SV_RECORD_CLASS 2

#define SV_LAST_CLASS 2

static const char *mixer_classes[] = { AudioCinputs, AudioCoutputs, AudioCrecord };

static const struct {
  u_int8_t   l_port;
  u_int8_t   r_port;
  u_int8_t   mask;
  u_int8_t   class;
  const char *audio;
} ports[] = {
  { SV_LEFT_AUX1_INPUT_CONTROL, SV_RIGHT_AUX1_INPUT_CONTROL, SV_AUX1_MASK,
    SV_INPUT_CLASS, "aux1" },
  { SV_LEFT_CD_INPUT_CONTROL, SV_RIGHT_CD_INPUT_CONTROL, SV_CD_MASK, 
    SV_INPUT_CLASS, AudioNcd },
  { SV_LEFT_LINE_IN_INPUT_CONTROL, SV_RIGHT_LINE_IN_INPUT_CONTROL, SV_LINE_IN_MASK,
    SV_INPUT_CLASS, AudioNline },
  { SV_MIC_INPUT_CONTROL, 0, SV_MIC_MASK, SV_INPUT_CLASS, AudioNmicrophone },
  { SV_LEFT_SYNTH_INPUT_CONTROL, SV_RIGHT_SYNTH_INPUT_CONTROL, 
    SV_SYNTH_MASK, SV_INPUT_CLASS, AudioNfmsynth },
  { SV_LEFT_AUX2_INPUT_CONTROL, SV_RIGHT_AUX2_INPUT_CONTROL, SV_AUX2_MASK,
    SV_INPUT_CLASS, "aux2" },
  { SV_LEFT_PCM_INPUT_CONTROL, SV_RIGHT_PCM_INPUT_CONTROL, SV_PCM_MASK,
    SV_INPUT_CLASS, AudioNdac },
  { SV_LEFT_MIXER_OUTPUT_CONTROL, SV_RIGHT_MIXER_OUTPUT_CONTROL, 
    SV_MIXER_OUT_MASK, SV_OUTPUT_CLASS, AudioNmaster }
};


static const struct {
  int idx;
  const char *name;
} record_sources[] = {
  { SV_REC_CD, AudioNcd },
  { SV_REC_DAC, AudioNdac },
  { SV_REC_AUX2, "aux2" },
  { SV_REC_LINE, AudioNline },
  { SV_REC_AUX1, "aux1" },
  { SV_REC_MIC, AudioNmicrophone },
  { SV_REC_MIXER, AudioNmixerout }
};


#define SV_DEVICES_PER_PORT 2
#define SV_FIRST_MIXER (SV_LAST_CLASS + 1)
#define SV_LAST_MIXER (SV_DEVICES_PER_PORT * (ARRAY_SIZE(ports)) + SV_LAST_CLASS)
#define SV_RECORD_SOURCE (SV_LAST_MIXER + 1)
#define SV_MIC_BOOST (SV_LAST_MIXER + 2)
#define SV_RECORD_GAIN (SV_LAST_MIXER + 3)
#define SV_SRS_MODE (SV_LAST_MIXER + 4)

int 
sv_query_devinfo(void *addr, mixer_devinfo_t *dip)
{

  if (dip->index < 0)
    return (ENXIO);

  /* It's a class */
  if (dip->index <= SV_LAST_CLASS) {
    dip->type = AUDIO_MIXER_CLASS;
    dip->mixer_class = dip->index;
    dip->next = dip->prev = AUDIO_MIXER_LAST;
    strlcpy(dip->label.name, mixer_classes[dip->index],
	    sizeof dip->label.name);
    return (0);
  }

  if (dip->index >= SV_FIRST_MIXER &&
      dip->index <= SV_LAST_MIXER) {
    int off = dip->index - SV_FIRST_MIXER;
    int mute = (off % SV_DEVICES_PER_PORT);
    int idx = off / SV_DEVICES_PER_PORT;

    dip->mixer_class = ports[idx].class;
    strlcpy(dip->label.name, ports[idx].audio, sizeof dip->label.name);

    if (!mute) {
      dip->type = AUDIO_MIXER_VALUE;
      dip->prev = AUDIO_MIXER_LAST;
      dip->next = dip->index + 1;

      if (ports[idx].r_port != 0)
	dip->un.v.num_channels = 2;
      else
	dip->un.v.num_channels = 1;
      
      strlcpy(dip->un.v.units.name, AudioNvolume, sizeof dip->un.v.units.name);

    } else {
      dip->type = AUDIO_MIXER_ENUM;
      dip->prev = dip->index - 1;
      dip->next = AUDIO_MIXER_LAST;

      strlcpy(dip->label.name, AudioNmute, sizeof dip->label.name);
      dip->un.e.num_mem = 2;
      strlcpy(dip->un.e.member[0].label.name, AudioNoff,
	  sizeof dip->un.e.member[0].label.name);
      dip->un.e.member[0].ord = 0;
      strlcpy(dip->un.e.member[1].label.name, AudioNon,
	  sizeof dip->un.e.member[1].label.name);
      dip->un.e.member[1].ord = 1;

    }

    return (0);
  }

  switch (dip->index) {
  case SV_RECORD_SOURCE:
    dip->mixer_class = SV_RECORD_CLASS;
    dip->prev = AUDIO_MIXER_LAST;
    dip->next = SV_RECORD_GAIN;
    strlcpy(dip->label.name, AudioNsource, sizeof dip->label.name);
    dip->type = AUDIO_MIXER_ENUM;

    dip->un.e.num_mem = ARRAY_SIZE(record_sources);

    {
      int idx;
      for (idx = 0; idx < ARRAY_SIZE(record_sources); idx++) {
	strlcpy(dip->un.e.member[idx].label.name, record_sources[idx].name,
	    sizeof dip->un.e.member[idx].label.name);
	dip->un.e.member[idx].ord = record_sources[idx].idx;
      }
    }
    return (0);

  case SV_RECORD_GAIN:
    dip->mixer_class = SV_RECORD_CLASS;
    dip->prev = SV_RECORD_SOURCE;
    dip->next = AUDIO_MIXER_LAST;
    strlcpy(dip->label.name, "gain", sizeof dip->label.name);
    dip->type = AUDIO_MIXER_VALUE;
    dip->un.v.num_channels = 1;
    strlcpy(dip->un.v.units.name, AudioNvolume, sizeof dip->un.v.units.name);
    return (0);

  case SV_MIC_BOOST:
    dip->mixer_class = SV_RECORD_CLASS;
    dip->prev = AUDIO_MIXER_LAST;
    dip->next = AUDIO_MIXER_LAST;
    strlcpy(dip->label.name, "micboost", sizeof dip->label.name);
    goto on_off;

  case SV_SRS_MODE:
    dip->mixer_class = SV_OUTPUT_CLASS;
    dip->prev = dip->next = AUDIO_MIXER_LAST;
    strlcpy(dip->label.name, AudioNspatial, sizeof dip->label.name);

on_off:
    dip->type = AUDIO_MIXER_ENUM;
    dip->un.e.num_mem = 2;
    strlcpy(dip->un.e.member[0].label.name, AudioNoff,
	sizeof dip->un.e.member[0].label.name);
    dip->un.e.member[0].ord = 0;
    strlcpy(dip->un.e.member[1].label.name, AudioNon,
	sizeof dip->un.e.member[1].label.name);
    dip->un.e.member[1].ord = 1;
    return (0);
  }

  return (ENXIO);
}

int
sv_mixer_set_port(void *addr, mixer_ctrl_t *cp)
{
  struct sv_softc *sc = addr;
  u_int8_t reg;
  int idx;

  if (cp->dev >= SV_FIRST_MIXER &&
      cp->dev <= SV_LAST_MIXER) {
    int off = cp->dev - SV_FIRST_MIXER;
    int mute = (off % SV_DEVICES_PER_PORT);
    idx = off / SV_DEVICES_PER_PORT;

    if (mute) {
      if (cp->type != AUDIO_MIXER_ENUM) 
	return (EINVAL);

      reg = sv_read_indirect(sc, ports[idx].l_port);
      if (cp->un.ord) 
	reg |= SV_MUTE_BIT;
      else
	reg &= ~SV_MUTE_BIT;
      sv_write_indirect(sc, ports[idx].l_port, reg);

      if (ports[idx].r_port) {
	reg = sv_read_indirect(sc, ports[idx].r_port);
	if (cp->un.ord) 
	  reg |= SV_MUTE_BIT;
	else
	  reg &= ~SV_MUTE_BIT;
	sv_write_indirect(sc, ports[idx].r_port, reg);
      }
    } else {
      int  lval, rval;

      if (cp->type != AUDIO_MIXER_VALUE)
	return (EINVAL);

      if (cp->un.value.num_channels != 1 &&
	  cp->un.value.num_channels != 2)
	return (EINVAL);

      if (ports[idx].r_port == 0) {
	if (cp->un.value.num_channels != 1)
	  return (EINVAL);
	lval = cp->un.value.level[AUDIO_MIXER_LEVEL_MONO];
      } else {
	if (cp->un.value.num_channels != 2)
	  return (EINVAL);

	lval = cp->un.value.level[AUDIO_MIXER_LEVEL_LEFT];
	rval = cp->un.value.level[AUDIO_MIXER_LEVEL_RIGHT];
      }

      sc->sc_trd = 1;

      reg = sv_read_indirect(sc, ports[idx].l_port);
      reg &= ~(ports[idx].mask);
      lval = ((AUDIO_MAX_GAIN - lval) * ports[idx].mask) / AUDIO_MAX_GAIN;
      reg |= lval;
      sv_write_indirect(sc, ports[idx].l_port, reg);

      if (ports[idx].r_port != 0) {
	reg = sv_read_indirect(sc, ports[idx].r_port);
	reg &= ~(ports[idx].mask);

	rval = ((AUDIO_MAX_GAIN - rval) * ports[idx].mask) / AUDIO_MAX_GAIN;
	reg |= rval;

	sv_write_indirect(sc, ports[idx].r_port, reg);
      }

      sc->sc_trd = 0;
      sv_read_indirect(sc, ports[idx].l_port);
    }

    return (0);
  }


  switch (cp->dev) {
  case SV_RECORD_SOURCE:
    if (cp->type != AUDIO_MIXER_ENUM)
      return (EINVAL);

    for (idx = 0; idx < ARRAY_SIZE(record_sources); idx++) {
      if (record_sources[idx].idx == cp->un.ord)
	goto found;
    }
    
    return (EINVAL);

  found:
    reg = sv_read_indirect(sc, SV_LEFT_ADC_INPUT_CONTROL);
    reg &= ~SV_REC_SOURCE_MASK;
    reg |= (((cp->un.ord) << SV_REC_SOURCE_SHIFT) & SV_REC_SOURCE_MASK);
    sv_write_indirect(sc, SV_LEFT_ADC_INPUT_CONTROL, reg);

    reg = sv_read_indirect(sc, SV_RIGHT_ADC_INPUT_CONTROL);
    reg &= ~SV_REC_SOURCE_MASK;
    reg |= (((cp->un.ord) << SV_REC_SOURCE_SHIFT) & SV_REC_SOURCE_MASK);
    sv_write_indirect(sc, SV_RIGHT_ADC_INPUT_CONTROL, reg);
    return (0);

  case SV_RECORD_GAIN:
    {
      int val;

      if (cp->type != AUDIO_MIXER_VALUE)
	return (EINVAL);

      if (cp->un.value.num_channels != 1)
	return (EINVAL);

      val = (cp->un.value.level[AUDIO_MIXER_LEVEL_MONO] * SV_REC_GAIN_MASK) 
	/ AUDIO_MAX_GAIN;

      reg = sv_read_indirect(sc, SV_LEFT_ADC_INPUT_CONTROL);
      reg &= ~SV_REC_GAIN_MASK;
      reg |= val;
      sv_write_indirect(sc, SV_LEFT_ADC_INPUT_CONTROL, reg);
      
      reg = sv_read_indirect(sc, SV_RIGHT_ADC_INPUT_CONTROL);
      reg &= ~SV_REC_GAIN_MASK;
      reg |= val;
      sv_write_indirect(sc, SV_RIGHT_ADC_INPUT_CONTROL, reg);

    }

    return (0);

  case SV_MIC_BOOST:
    if (cp->type != AUDIO_MIXER_ENUM)
      return (EINVAL);

    reg = sv_read_indirect(sc, SV_LEFT_ADC_INPUT_CONTROL);
    if (cp->un.ord) {
      reg |= SV_MIC_BOOST_BIT;
    } else {
      reg &= ~SV_MIC_BOOST_BIT;
    }
    
    sv_write_indirect(sc, SV_LEFT_ADC_INPUT_CONTROL, reg);
    return (0);

  case SV_SRS_MODE:
    if (cp->type != AUDIO_MIXER_ENUM)
      return (EINVAL);

    reg = sv_read_indirect(sc, SV_SRS_SPACE_CONTROL);
    if (cp->un.ord) {
      reg &= ~SV_SRS_SPACE_ONOFF;
    } else {
      reg |= SV_SRS_SPACE_ONOFF;
    }
    
    sv_write_indirect(sc, SV_SRS_SPACE_CONTROL, reg);
    return (0);
  }

  return (EINVAL);
}

int
sv_mixer_get_port(void *addr, mixer_ctrl_t *cp)
{
  struct sv_softc *sc = addr;
  int val;
  u_int8_t reg;

  if (cp->dev >= SV_FIRST_MIXER &&
      cp->dev <= SV_LAST_MIXER) {
    int off = cp->dev - SV_FIRST_MIXER;
    int mute = (off % 2);
    int idx = off / 2;

    if (mute) {
      if (cp->type != AUDIO_MIXER_ENUM) 
	return (EINVAL);

      reg = sv_read_indirect(sc, ports[idx].l_port);
      cp->un.ord = ((reg & SV_MUTE_BIT) ? 1 : 0);
    } else {
      if (cp->type != AUDIO_MIXER_VALUE)
	return (EINVAL);

      if (cp->un.value.num_channels != 1 &&
	  cp->un.value.num_channels != 2)
	return (EINVAL);

      if ((ports[idx].r_port == 0 &&
	   cp->un.value.num_channels != 1) ||
	  (ports[idx].r_port != 0 &&
	   cp->un.value.num_channels != 2))
	return (EINVAL);

      reg = sv_read_indirect(sc, ports[idx].l_port);
      reg &= ports[idx].mask;

      val = AUDIO_MAX_GAIN - ((reg * AUDIO_MAX_GAIN) / ports[idx].mask);

      if (ports[idx].r_port != 0) {
	cp->un.value.level[AUDIO_MIXER_LEVEL_LEFT] = val;

	reg = sv_read_indirect(sc, ports[idx].r_port);
	reg &= ports[idx].mask;
      
	val = AUDIO_MAX_GAIN - ((reg * AUDIO_MAX_GAIN) / ports[idx].mask);
	cp->un.value.level[AUDIO_MIXER_LEVEL_RIGHT] = val;
      } else 
	cp->un.value.level[AUDIO_MIXER_LEVEL_MONO] = val;
    }

    return (0);
  }

  switch (cp->dev) {
  case SV_RECORD_SOURCE:
    if (cp->type != AUDIO_MIXER_ENUM)
      return (EINVAL);

    reg = sv_read_indirect(sc, SV_LEFT_ADC_INPUT_CONTROL);
    cp->un.ord = ((reg & SV_REC_SOURCE_MASK) >> SV_REC_SOURCE_SHIFT);

    return (0);

  case SV_RECORD_GAIN:
    if (cp->type != AUDIO_MIXER_VALUE)
      return (EINVAL);

    if (cp->un.value.num_channels != 1)
      return (EINVAL);

    reg = sv_read_indirect(sc, SV_LEFT_ADC_INPUT_CONTROL) & SV_REC_GAIN_MASK;
    cp->un.value.level[AUDIO_MIXER_LEVEL_MONO] = 
      (((unsigned int)reg) * AUDIO_MAX_GAIN) / SV_REC_GAIN_MASK;

    return (0);

  case SV_MIC_BOOST:
    if (cp->type != AUDIO_MIXER_ENUM)
      return (EINVAL);

    reg = sv_read_indirect(sc, SV_LEFT_ADC_INPUT_CONTROL);
    cp->un.ord = ((reg & SV_MIC_BOOST_BIT) ? 1 : 0);

    return (0);


  case SV_SRS_MODE:
    if (cp->type != AUDIO_MIXER_ENUM)
      return (EINVAL);

    reg = sv_read_indirect(sc, SV_SRS_SPACE_CONTROL);

    cp->un.ord = ((reg & SV_SRS_SPACE_ONOFF) ? 0 : 1);
    return (0);
  }

  return (EINVAL);
}


static void
sv_init_mixer(struct sv_softc *sc)
{
  mixer_ctrl_t cp;
  int idx;

  cp.type = AUDIO_MIXER_ENUM;
  cp.dev = SV_SRS_MODE;
  cp.un.ord = 0;

  sv_mixer_set_port(sc, &cp);

  for (idx = 0; idx < ARRAY_SIZE(ports); idx++) {
    if (strcmp(ports[idx].audio, AudioNdac) == 0) {
      cp.type = AUDIO_MIXER_ENUM;
      cp.dev = SV_FIRST_MIXER + idx * SV_DEVICES_PER_PORT + 1;
      cp.un.ord = 0;
      sv_mixer_set_port(sc, &cp);
      break;
    }
  }
}

void *
sv_malloc(void *addr, int direction, size_t size, int pool, int flags)
{
	struct sv_softc *sc = addr;
        struct sv_dma *p;
        int error;

        p = malloc(sizeof(*p), pool, flags);
        if (!p)
                return (0);
        error = sv_allocmem(sc, size, 16, p);
        if (error) {
                free(p, pool, sizeof(*p));
        	return (0);
        }
        p->next = sc->sc_dmas;
        sc->sc_dmas = p;
	return (KERNADDR(p));
}

void
sv_free(void *addr, void *ptr, int pool)
{
	struct sv_softc *sc = addr;
        struct sv_dma **p;

        for (p = &sc->sc_dmas; *p; p = &(*p)->next) {
                if (KERNADDR(*p) == ptr) {
                        sv_freemem(sc, *p);
                        *p = (*p)->next;
                        free(*p, pool, sizeof(**p));
                        return;
                }
        }
}
