/*	$OpenBSD: auich.c,v 1.120 2024/09/04 07:54:52 mglocker Exp $	*/

/*
 * Copyright (c) 2000,2001 Michael Shalayeff
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * AC'97 audio found on Intel 810/815/820/440MX chipsets.
 *	http://developer.intel.com/design/chipsets/datashts/290655.htm
 *	http://developer.intel.com/design/chipsets/manuals/298028.htm
 *	http://www.intel.com/design/chipsets/datashts/290714.htm
 *	http://www.intel.com/design/chipsets/datashts/290744.htm
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/device.h>

#include <dev/pci/pcidevs.h>
#include <dev/pci/pcivar.h>

#include <sys/audioio.h>
#include <dev/audio_if.h>

#include <machine/bus.h>

#include <dev/ic/ac97.h>

/* 12.1.10 NAMBAR - native audio mixer base address register */
#define	AUICH_NAMBAR	0x10
/* 12.1.11 NABMBAR - native audio bus mastering base address register */
#define	AUICH_NABMBAR	0x14
#define	AUICH_CFG	0x41
#define	AUICH_CFG_IOSE	0x01
/* ICH4/ICH5/ICH6/ICH7 native audio mixer BAR */
#define	AUICH_MMBAR	0x18
/* ICH4/ICH5/ICH6/ICH7 native bus mastering BAR */
#define	AUICH_MBBAR	0x1c
#define	AUICH_S2CR	0x10000000	/* tertiary codec ready */

/* table 12-3. native audio bus master control registers */
#define	AUICH_BDBAR	0x00	/* 8-byte aligned address */
#define	AUICH_CIV		0x04	/* 5 bits current index value */
#define	AUICH_LVI		0x05	/* 5 bits last valid index value */
#define		AUICH_LVI_MASK	0x1f
#define	AUICH_STS		0x06	/* 16 bits status */
#define		AUICH_FIFOE	0x10	/* fifo error */
#define		AUICH_BCIS	0x08	/* r- buf cmplt int sts; wr ack */
#define		AUICH_LVBCI	0x04	/* r- last valid bci, wr ack */
#define		AUICH_CELV	0x02	/* current equals last valid */
#define		AUICH_DCH	0x01	/* dma halted */
#define		AUICH_ISTS_BITS	"\020\01dch\02celv\03lvbci\04bcis\05fifoe"
#define	AUICH_PICB	0x08	/* 16 bits */
#define	AUICH_PIV		0x0a	/* 5 bits prefetched index value */
#define	AUICH_CTRL	0x0b	/* control */
#define		AUICH_IOCE	0x10	/* int on completion enable */
#define		AUICH_FEIE	0x08	/* fifo error int enable */
#define		AUICH_LVBIE	0x04	/* last valid buf int enable */
#define		AUICH_RR		0x02	/* 1 - reset regs */
#define		AUICH_RPBM	0x01	/* 1 - run, 0 - pause */

#define	AUICH_PCMI	0x00
#define	AUICH_PCMO	0x10
#define	AUICH_MICI	0x20

#define	AUICH_GCTRL	0x2c
#define		AUICH_SSM_78	0x40000000	/* S/PDIF slots 7 and 8 */
#define		AUICH_SSM_69	0x80000000	/* S/PDIF slots 6 and 9 */
#define		AUICH_SSM_1011	0xc0000000	/* S/PDIF slots 10 and 11 */
#define		AUICH_POM16	0x000000	/* PCM out precision 16bit */
#define		AUICH_POM20	0x400000	/* PCM out precision 20bit */
#define		AUICH_PCM246_MASK 0x300000
#define		AUICH_PCM2	0x000000	/* 2ch output */
#define		AUICH_PCM4	0x100000	/* 4ch output */
#define		AUICH_PCM6	0x200000	/* 6ch output */
#define		AUICH_SIS_PCM246_MASK 0x0000c0	/* SiS 7012 */
#define		AUICH_SIS_PCM2	0x000000	/* SiS 7012 2ch output */
#define		AUICH_SIS_PCM4	0x000040	/* SiS 7012 4ch output */
#define		AUICH_SIS_PCM6	0x000080	/* SiS 7012 6ch output */
#define		AUICH_S2RIE	0x40	/* int when tertiary codec resume */
#define		AUICH_SRIE	0x20	/* int when 2ndary codec resume */
#define		AUICH_PRIE	0x10	/* int when primary codec resume */
#define		AUICH_ACLSO	0x08	/* aclink shut off */
#define		AUICH_WRESET	0x04	/* warm reset */
#define		AUICH_CRESET	0x02	/* cold reset */
#define		AUICH_GIE		0x01	/* gpi int enable */
#define	AUICH_GSTS	0x30
#define		AUICH_MD3		0x20000	/* pwr-dn semaphore for modem */
#define		AUICH_AD3		0x10000	/* pwr-dn semaphore for audio */
#define		AUICH_RCS		0x08000	/* read completion status */
#define		AUICH_B3S12	0x04000	/* bit 3 of slot 12 */
#define		AUICH_B2S12	0x02000	/* bit 2 of slot 12 */
#define		AUICH_B1S12	0x01000	/* bit 1 of slot 12 */
#define		AUICH_SRI		0x00800	/* secondary resume int */
#define		AUICH_PRI		0x00400	/* primary resume int */
#define		AUICH_SCR		0x00200	/* secondary codec ready */
#define		AUICH_PCR		0x00100	/* primary codec ready */
#define		AUICH_MINT	0x00080	/* mic in int */
#define		AUICH_POINT	0x00040	/* pcm out int */
#define		AUICH_PIINT	0x00020	/* pcm in int */
#define		AUICH_MOINT	0x00004	/* modem out int */
#define		AUICH_MIINT	0x00002	/* modem in int */
#define		AUICH_GSCI	0x00001	/* gpi status change */
#define		AUICH_GSTS_BITS	"\020\01gsci\02miict\03moint\06piint\07point\010mint\011pcr\012scr\013pri\014sri\015b1s12\016b2s12\017b3s12\020rcs\021ad3\022md3"
#define	AUICH_CAS		0x34	/* 1/8 bit */
#define	AUICH_SEMATIMO		1000	/* us */
#define	AUICH_RESETIMO		500000	/* us */

#define	ICH_SIS_NV_CTL	0x4c	/* some SiS/NVIDIA register.  From Linux */
#define		ICH_SIS_CTL_UNMUTE	0x01	/* un-mute the output */

/*
 * There are 32 buffer descriptors.  Each can reference up to 2^16 16-bit
 * samples.
 */
#define	AUICH_DMALIST_MAX	32
#define	AUICH_DMASEG_MAX	(65536*2)
struct auich_dmalist {
	u_int32_t	base;
	u_int32_t	len;
#define	AUICH_DMAF_IOC	0x80000000	/* 1-int on complete */
#define	AUICH_DMAF_BUP	0x40000000	/* 0-retrans last, 1-transmit 0 */
};

#define	AUICH_FIXED_RATE 48000

struct auich_dma {
	bus_dmamap_t map;
	caddr_t addr;
	bus_dma_segment_t segs[1];
	int nsegs;
	size_t size;
};

struct auich_cdata {
	struct auich_dmalist ic_dmalist_pcmo[AUICH_DMALIST_MAX];
	struct auich_dmalist ic_dmalist_pcmi[AUICH_DMALIST_MAX];
	struct auich_dmalist ic_dmalist_mici[AUICH_DMALIST_MAX];
};

#define	AUICH_CDOFF(x)		offsetof(struct auich_cdata, x)
#define	AUICH_PCMO_OFF(x)	AUICH_CDOFF(ic_dmalist_pcmo[(x)])
#define	AUICH_PCMI_OFF(x)	AUICH_CDOFF(ic_dmalist_pcmi[(x)])
#define	AUICH_MICI_OFF(x)	AUICH_CDOFF(ic_dmalist_mici[(x)])

struct auich_softc {
	struct device sc_dev;
	void *sc_ih;

	pcireg_t pci_id;
	bus_space_tag_t iot;
	bus_space_tag_t iot_mix;
	bus_space_handle_t mix_ioh;
	bus_space_handle_t aud_ioh;
	bus_dma_tag_t dmat;

	struct ac97_codec_if *codec_if;
	struct ac97_host_if host_if;
	int sc_spdif;

	/* dma scatter-gather buffer lists */

	bus_dmamap_t sc_cddmamap;
#define	sc_cddma	sc_cddmamap->dm_segs[0].ds_addr

	struct auich_cdata *sc_cdata;

	struct auich_ring {
		int qptr;
		struct auich_dmalist *dmalist;

		uint32_t start, p, end;
		int blksize;

		void (*intr)(void *);
		void *arg;
		int running;
		size_t size;
		uint32_t ap;
	} pcmo, pcmi, mici;

	struct auich_dma *sc_pdma;	/* play */
	struct auich_dma *sc_rdma;	/* record */
	struct auich_dma *sc_cdma;	/* calibrate */

#ifdef AUICH_DEBUG
	int pcmi_fifoe;
	int pcmo_fifoe;
#endif

	int suspend;
	u_int16_t ext_ctrl;
	int sc_sample_size;
	int sc_sts_reg;
	int sc_dmamap_flags;
	int sc_ignore_codecready;
	int flags;
	int sc_ac97rate;

	/* multi-channel control bits */
	int sc_pcm246_mask;
	int sc_pcm2;
	int sc_pcm4;
	int sc_pcm6;

	u_int last_rrate;
	u_int last_prate;
	u_int last_pchan;
};

#ifdef AUICH_DEBUG
#define	DPRINTF(l,x)	do { if (auich_debug & (l)) printf x; } while(0)
int auich_debug = 0x0002;
#define	AUICH_DEBUG_CODECIO	0x0001
#define	AUICH_DEBUG_DMA		0x0002
#define	AUICH_DEBUG_INTR	0x0004
#else
#define	DPRINTF(x,y)	/* nothing */
#endif

struct cfdriver	auich_cd = {
	NULL, "auich", DV_DULL
};

int  auich_match(struct device *, void *, void *);
void auich_attach(struct device *, struct device *, void *);
int  auich_intr(void *);

int auich_activate(struct device *, int);

const struct cfattach auich_ca = {
	sizeof(struct auich_softc), auich_match, auich_attach,
	NULL, auich_activate
};

static const struct auich_devtype {
	int	vendor;
	int	product;
	int	options;
	char	name[8];
} auich_devices[] = {
	{ PCI_VENDOR_INTEL,	PCI_PRODUCT_INTEL_6300ESB_ACA,	0, "ESB" },
	{ PCI_VENDOR_INTEL,	PCI_PRODUCT_INTEL_6321ESB_ACA,	0, "ESB2" },
	{ PCI_VENDOR_INTEL,	PCI_PRODUCT_INTEL_82801AA_ACA,	0, "ICH" },
	{ PCI_VENDOR_INTEL,	PCI_PRODUCT_INTEL_82801AB_ACA,	0, "ICH0" },
	{ PCI_VENDOR_INTEL,	PCI_PRODUCT_INTEL_82801BA_ACA,	0, "ICH2" },
	{ PCI_VENDOR_INTEL,	PCI_PRODUCT_INTEL_82801CA_ACA,	0, "ICH3" },
	{ PCI_VENDOR_INTEL,	PCI_PRODUCT_INTEL_82801DB_ACA,	0, "ICH4" },
	{ PCI_VENDOR_INTEL,	PCI_PRODUCT_INTEL_82801EB_ACA,	0, "ICH5" },
	{ PCI_VENDOR_INTEL,	PCI_PRODUCT_INTEL_82801FB_ACA,	0, "ICH6" },
	{ PCI_VENDOR_INTEL,	PCI_PRODUCT_INTEL_82801GB_ACA,	0, "ICH7" },
	{ PCI_VENDOR_INTEL,	PCI_PRODUCT_INTEL_82440MX_ACA,	0, "440MX" },
	{ PCI_VENDOR_SIS,	PCI_PRODUCT_SIS_7012_ACA,	0, "SiS7012" },
	{ PCI_VENDOR_NVIDIA,	PCI_PRODUCT_NVIDIA_NFORCE_ACA,	0, "nForce" },
	{ PCI_VENDOR_NVIDIA,	PCI_PRODUCT_NVIDIA_NFORCE2_ACA,	0, "nForce2" },
	{ PCI_VENDOR_NVIDIA,	PCI_PRODUCT_NVIDIA_NFORCE2_400_ACA,
	    0, "nForce2" },
	{ PCI_VENDOR_NVIDIA,	PCI_PRODUCT_NVIDIA_NFORCE3_ACA,	0, "nForce3" },
	{ PCI_VENDOR_NVIDIA,	PCI_PRODUCT_NVIDIA_NFORCE3_250_ACA,
	    0, "nForce3" },
	{ PCI_VENDOR_NVIDIA,	PCI_PRODUCT_NVIDIA_NFORCE4_AC,	0, "nForce4" },
	{ PCI_VENDOR_NVIDIA,	PCI_PRODUCT_NVIDIA_MCP04_AC97,	0, "MCP04" },
	{ PCI_VENDOR_NVIDIA,	PCI_PRODUCT_NVIDIA_MCP51_ACA,	0, "MCP51" },
	{ PCI_VENDOR_AMD,	PCI_PRODUCT_AMD_PBC768_ACA,	0, "AMD768" },
	{ PCI_VENDOR_AMD,	PCI_PRODUCT_AMD_8111_ACA,	0, "AMD8111" },
};

int auich_open(void *, int);
void auich_close(void *);
int auich_set_params(void *, int, int, struct audio_params *,
    struct audio_params *);
int auich_round_blocksize(void *, int);
void auich_halt_pipe(struct auich_softc *, int, struct auich_ring *);
int auich_halt_output(void *);
int auich_halt_input(void *);
int auich_set_port(void *, mixer_ctrl_t *);
int auich_get_port(void *, mixer_ctrl_t *);
int auich_query_devinfo(void *, mixer_devinfo_t *);
void *auich_allocm(void *, int, size_t, int, int);
void auich_freem(void *, void *, int);
size_t auich_round_buffersize(void *, int, size_t);
void auich_trigger_pipe(struct auich_softc *, int, struct auich_ring *);
void auich_intr_pipe(struct auich_softc *, int, struct auich_ring *);
int auich_trigger_output(void *, void *, void *, int, void (*)(void *),
    void *, struct audio_params *);
int auich_trigger_input(void *, void *, void *, int, void (*)(void *),
    void *, struct audio_params *);
int auich_alloc_cdata(struct auich_softc *);
int auich_allocmem(struct auich_softc *, size_t, size_t, struct auich_dma *);
void auich_freemem(struct auich_softc *, struct auich_dma *);

void auich_resume(struct auich_softc *);

const struct audio_hw_if auich_hw_if = {
	.open = auich_open,
	.close = auich_close,
	.set_params = auich_set_params,
	.round_blocksize = auich_round_blocksize,
	.halt_output = auich_halt_output,
	.halt_input = auich_halt_input,
	.set_port = auich_set_port,
	.get_port = auich_get_port,
	.query_devinfo = auich_query_devinfo,
	.allocm = auich_allocm,
	.freem = auich_freem,
	.round_buffersize = auich_round_buffersize,
	.trigger_output = auich_trigger_output,
	.trigger_input = auich_trigger_input,
};

int  auich_attach_codec(void *, struct ac97_codec_if *);
int  auich_read_codec(void *, u_int8_t, u_int16_t *);
int  auich_write_codec(void *, u_int8_t, u_int16_t);
void auich_reset_codec(void *);
enum ac97_host_flags auich_flags_codec(void *);
unsigned int auich_calibrate(struct auich_softc *);
void auich_spdif_event(void *, int);

int
auich_match(struct device *parent, void *match, void *aux)
{
	struct pci_attach_args *pa = aux;
	int i;

	for (i = nitems(auich_devices); i--;)
		if (PCI_VENDOR(pa->pa_id) == auich_devices[i].vendor &&
		    PCI_PRODUCT(pa->pa_id) == auich_devices[i].product)
			return 1;

	return 0;
}

void
auich_attach(struct device *parent, struct device *self, void *aux)
{
	struct auich_softc *sc = (struct auich_softc *)self;
	struct pci_attach_args *pa = aux;
	pci_intr_handle_t ih;
	bus_size_t mix_size, aud_size;
	pcireg_t csr;
	const char *intrstr;
	u_int32_t status;
	int i;

	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_INTEL &&
	    (PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_INTEL_82801DB_ACA ||
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_INTEL_82801EB_ACA ||
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_INTEL_82801FB_ACA ||
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_INTEL_82801GB_ACA)) {
		/*
		 * Use native mode for ICH4/ICH5/ICH6/ICH7
		 */
		if (pci_mapreg_map(pa, AUICH_MMBAR, PCI_MAPREG_TYPE_MEM, 0,
		    &sc->iot_mix, &sc->mix_ioh, NULL, &mix_size, 0)) {
			csr = pci_conf_read(pa->pa_pc, pa->pa_tag, AUICH_CFG);
			pci_conf_write(pa->pa_pc, pa->pa_tag, AUICH_CFG,
			    csr | AUICH_CFG_IOSE);
			if (pci_mapreg_map(pa, AUICH_NAMBAR, PCI_MAPREG_TYPE_IO,
			    0, &sc->iot_mix, &sc->mix_ioh, NULL, &mix_size, 0)) {
				printf(": can't map codec mem/io space\n");
				return;
			}
		}

		if (pci_mapreg_map(pa, AUICH_MBBAR, PCI_MAPREG_TYPE_MEM, 0,
		    &sc->iot, &sc->aud_ioh, NULL, &aud_size, 0)) {
			csr = pci_conf_read(pa->pa_pc, pa->pa_tag, AUICH_CFG);
			pci_conf_write(pa->pa_pc, pa->pa_tag, AUICH_CFG,
			    csr | AUICH_CFG_IOSE);
			if (pci_mapreg_map(pa, AUICH_NABMBAR,
			    PCI_MAPREG_TYPE_IO, 0, &sc->iot,
			    &sc->aud_ioh, NULL, &aud_size, 0)) {
				printf(": can't map device mem/io space\n");
				goto fail_unmap_mix;
			}
		}
	} else {
		if (pci_mapreg_map(pa, AUICH_NAMBAR, PCI_MAPREG_TYPE_IO,
		    0, &sc->iot_mix, &sc->mix_ioh, NULL, &mix_size, 0)) {
			printf(": can't map codec i/o space\n");
			return;
		}

		if (pci_mapreg_map(pa, AUICH_NABMBAR, PCI_MAPREG_TYPE_IO,
		    0, &sc->iot, &sc->aud_ioh, NULL, &aud_size, 0)) {
			printf(": can't map device i/o space\n");
			goto fail_unmap_mix;
		}
	}
	sc->dmat = pa->pa_dmat;
	sc->pci_id = pa->pa_id;

	if (pci_intr_map(pa, &ih)) {
		printf(": can't map interrupt\n");
		goto fail_unmap;
	}
	intrstr = pci_intr_string(pa->pa_pc, ih);
	sc->sc_ih = pci_intr_establish(pa->pa_pc, ih, IPL_AUDIO | IPL_MPSAFE,
	    auich_intr, sc, sc->sc_dev.dv_xname);
	if (!sc->sc_ih) {
		printf(": can't establish interrupt");
		if (intrstr)
			printf(" at %s", intrstr);
		printf("\n");
		goto fail_unmap;
	}

	for (i = nitems(auich_devices); i--;)
		if (PCI_PRODUCT(pa->pa_id) == auich_devices[i].product)
			break;

	printf(": %s, %s\n", intrstr, auich_devices[i].name);

	/* SiS 7012 needs special handling */
	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_SIS &&
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_SIS_7012_ACA) {
		sc->sc_sts_reg = AUICH_PICB;
		sc->sc_sample_size = 1;
		sc->sc_pcm246_mask = AUICH_SIS_PCM246_MASK;
		sc->sc_pcm2 = AUICH_SIS_PCM2;
		sc->sc_pcm4 = AUICH_SIS_PCM4;
		sc->sc_pcm6 = AUICH_SIS_PCM6;
		/* un-mute output */
		bus_space_write_4(sc->iot, sc->aud_ioh, ICH_SIS_NV_CTL,
		    bus_space_read_4(sc->iot, sc->aud_ioh, ICH_SIS_NV_CTL) |
		    ICH_SIS_CTL_UNMUTE);
	} else {
		sc->sc_sts_reg = AUICH_STS;
		sc->sc_sample_size = 2;
		sc->sc_pcm246_mask = AUICH_PCM246_MASK;
		sc->sc_pcm2 = AUICH_PCM2;
		sc->sc_pcm4 = AUICH_PCM4;
		sc->sc_pcm6 = AUICH_PCM6;
	}

	/* Workaround for a 440MX B-stepping erratum */
	sc->sc_dmamap_flags = BUS_DMA_COHERENT;
	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_INTEL &&
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_INTEL_82440MX_ACA) {
		sc->sc_dmamap_flags |= BUS_DMA_NOCACHE;
		printf("%s: DMA bug workaround enabled\n", sc->sc_dev.dv_xname);
	}

	/* Set up DMA lists. */
	sc->pcmo.qptr = sc->pcmi.qptr = sc->mici.qptr = 0;
	if (auich_alloc_cdata(sc) != 0)
		goto fail_disestablish_intr;

	DPRINTF(AUICH_DEBUG_DMA, ("auich_attach: lists %p %p %p\n",
	    sc->pcmo.dmalist, sc->pcmi.dmalist, sc->mici.dmalist));

	/* Reset codec and AC'97 */
	auich_reset_codec(sc);
	status = bus_space_read_4(sc->iot, sc->aud_ioh, AUICH_GSTS);
	if (!(status & AUICH_PCR)) {	/* reset failure */
		if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_INTEL &&
		    (PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_INTEL_82801DB_ACA ||
		     PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_INTEL_82801EB_ACA ||
		     PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_INTEL_82801FB_ACA ||
		     PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_INTEL_82801GB_ACA)) {
			/* MSI 845G Max never return AUICH_PCR */
			sc->sc_ignore_codecready = 1;
		} else {
			printf("%s: reset failed!\n", sc->sc_dev.dv_xname);
			return;
		}
	}

	sc->host_if.arg = sc;
	sc->host_if.attach = auich_attach_codec;
	sc->host_if.read = auich_read_codec;
	sc->host_if.write = auich_write_codec;
	sc->host_if.reset = auich_reset_codec;
	sc->host_if.flags = auich_flags_codec;
	sc->host_if.spdif_event = auich_spdif_event;
	if (sc->sc_dev.dv_cfdata->cf_flags & 0x0001)
		sc->flags = AC97_HOST_SWAPPED_CHANNELS;

	if (ac97_attach(&sc->host_if) != 0)
		goto fail_disestablish_intr;
	sc->codec_if->vtbl->unlock(sc->codec_if);

	audio_attach_mi(&auich_hw_if, sc, NULL, &sc->sc_dev);

	/* Watch for power changes */
	sc->suspend = DVACT_RESUME;

	sc->sc_ac97rate = -1;
	return;

fail_disestablish_intr:
	pci_intr_disestablish(pa->pa_pc, sc->sc_ih);
fail_unmap:
	bus_space_unmap(sc->iot, sc->aud_ioh, aud_size);
fail_unmap_mix:
	bus_space_unmap(sc->iot_mix, sc->mix_ioh, mix_size);
}

int
auich_activate(struct device *self, int act)
{
	struct auich_softc *sc = (struct auich_softc *)self;

	switch (act) {
	case DVACT_RESUME:
		auich_resume(sc);
		break;
	default:
		break;
	}
	return (config_activate_children(self, act));
}

int
auich_read_codec(void *v, u_int8_t reg, u_int16_t *val)
{
	struct auich_softc *sc = v;
	int i;

	/* wait for an access semaphore */
	for (i = AUICH_SEMATIMO; i-- &&
	    bus_space_read_1(sc->iot, sc->aud_ioh, AUICH_CAS) & 1; DELAY(1));

	if (!sc->sc_ignore_codecready && i < 0) {
		DPRINTF(AUICH_DEBUG_CODECIO,
		    ("%s: read_codec timeout\n", sc->sc_dev.dv_xname));
		return (-1);
	}

	*val = bus_space_read_2(sc->iot_mix, sc->mix_ioh, reg);
	DPRINTF(AUICH_DEBUG_CODECIO, ("%s: read_codec(%x, %x)\n",
	    sc->sc_dev.dv_xname, reg, *val));
	return (0);
}

int
auich_write_codec(void *v, u_int8_t reg, u_int16_t val)
{
	struct auich_softc *sc = v;
	int i;

	/* wait for an access semaphore */
	for (i = AUICH_SEMATIMO; i-- &&
	    bus_space_read_1(sc->iot, sc->aud_ioh, AUICH_CAS) & 1; DELAY(1));

	if (sc->sc_ignore_codecready || i >= 0) {
		DPRINTF(AUICH_DEBUG_CODECIO, ("%s: write_codec(%x, %x)\n",
		    sc->sc_dev.dv_xname, reg, val));
		bus_space_write_2(sc->iot_mix, sc->mix_ioh, reg, val);
		return (0);
	} else {
		DPRINTF(AUICH_DEBUG_CODECIO,
		    ("%s: write_codec timeout\n", sc->sc_dev.dv_xname));
		return (-1);
	}
}

int
auich_attach_codec(void *v, struct ac97_codec_if *cif)
{
	struct auich_softc *sc = v;

	sc->codec_if = cif;
	return 0;
}

void
auich_reset_codec(void *v)
{
	struct auich_softc *sc = v;
	u_int32_t control;
	int i;

	control = bus_space_read_4(sc->iot, sc->aud_ioh, AUICH_GCTRL);
	control &= ~(AUICH_ACLSO | sc->sc_pcm246_mask);
	control |= (control & AUICH_CRESET) ? AUICH_WRESET : AUICH_CRESET;
	bus_space_write_4(sc->iot, sc->aud_ioh, AUICH_GCTRL, control);

	for (i = AUICH_RESETIMO; i-- &&
	    !(bus_space_read_4(sc->iot, sc->aud_ioh, AUICH_GSTS) & AUICH_PCR);
	    DELAY(1));

	if (i < 0)
		DPRINTF(AUICH_DEBUG_CODECIO,
		    ("%s: reset_codec timeout\n", sc->sc_dev.dv_xname));
}

enum ac97_host_flags
auich_flags_codec(void *v)
{
	struct auich_softc *sc = v;

	return (sc->flags);
}

void
auich_spdif_event(void *v, int flag)
{
	struct auich_softc *sc = v;
	sc->sc_spdif = flag;
}

int
auich_open(void *v, int flags)
{
	struct auich_softc *sc = v;

	if (sc->sc_ac97rate == -1)
		sc->sc_ac97rate = auich_calibrate(sc);

	sc->codec_if->vtbl->lock(sc->codec_if);

	return 0;
}

void
auich_close(void *v)
{
	struct auich_softc *sc = v;

	sc->codec_if->vtbl->unlock(sc->codec_if);
}

int
auich_set_params(void *v, int setmode, int usemode,
    struct audio_params *play, struct audio_params *rec)
{
	struct auich_softc *sc = v;
	struct ac97_codec_if *codec = sc->codec_if;
	int error;
	u_int orate;
	u_int adj_rate;
	u_int32_t control;
	u_int16_t ext_id;

	if (setmode & AUMODE_PLAY) {
		/* only 16-bit 48kHz slinear_le if s/pdif enabled */
		if (sc->sc_spdif) {
			play->sample_rate = 48000;
			play->precision = 16;
			play->encoding = AUDIO_ENCODING_SLINEAR_LE;
		}
	}
	if (setmode & AUMODE_PLAY) {
		play->precision = 16;
		switch(play->encoding) {
		case AUDIO_ENCODING_SLINEAR_LE:
			if (play->channels > 6)
				play->channels = 6;
			if (play->channels > 1)
				play->channels &= ~1;
			switch (play->channels) {
			case 1:
				play->channels = 2;
				break;
			case 2:
				break;
			case 4:
				ext_id = codec->vtbl->get_caps(codec);
				if (!(ext_id & AC97_EXT_AUDIO_SDAC))
					play->channels = 2;
				break;
			case 6:
				ext_id = codec->vtbl->get_caps(codec);
				if ((ext_id & AC97_BITS_6CH) !=
				    AC97_BITS_6CH)
					play->channels = 2;
				break;
			default:
				return (EINVAL);
			}
			break;
		default:
			return (EINVAL);
		}
		play->bps = AUDIO_BPS(play->precision);
		play->msb = 1;

		orate = adj_rate = play->sample_rate;
		if (sc->sc_ac97rate != 0)
			adj_rate = orate * AUICH_FIXED_RATE / sc->sc_ac97rate;

		play->sample_rate = adj_rate;
		sc->last_prate = play->sample_rate;

		error = ac97_set_rate(sc->codec_if,
		    AC97_REG_PCM_LFE_DAC_RATE, &play->sample_rate);
		if (error)
			return (error);

		play->sample_rate = adj_rate;
		error = ac97_set_rate(sc->codec_if,
		    AC97_REG_PCM_SURR_DAC_RATE, &play->sample_rate);
		if (error)
			return (error);

		play->sample_rate = adj_rate;
		error = ac97_set_rate(sc->codec_if,
		    AC97_REG_PCM_FRONT_DAC_RATE, &play->sample_rate);
		if (error)
			return (error);

		if (play->sample_rate == adj_rate)
			play->sample_rate = orate;

		control = bus_space_read_4(sc->iot, sc->aud_ioh, AUICH_GCTRL);
		control &= ~(sc->sc_pcm246_mask);
		if (play->channels == 4)
			control |= sc->sc_pcm4;
		else if (play->channels == 6)
			control |= sc->sc_pcm6;
		bus_space_write_4(sc->iot, sc->aud_ioh, AUICH_GCTRL, control);

		sc->last_pchan = play->channels;
	}

	if (setmode & AUMODE_RECORD) {
		rec->channels = 2;
		rec->precision = 16;
		rec->encoding = AUDIO_ENCODING_SLINEAR_LE;
		rec->bps = AUDIO_BPS(rec->precision);
		rec->msb = 1;

		orate = rec->sample_rate;
		if (sc->sc_ac97rate != 0)
			rec->sample_rate = orate * AUICH_FIXED_RATE /
			    sc->sc_ac97rate;
		sc->last_rrate = rec->sample_rate;
		error = ac97_set_rate(sc->codec_if, AC97_REG_PCM_LR_ADC_RATE,
		    &rec->sample_rate);
		if (error)
			return (error);
		rec->sample_rate = orate;
	}

	return (0);
}

int
auich_round_blocksize(void *v, int blk)
{
	return (blk + 0x3f) & ~0x3f;
}


void
auich_halt_pipe(struct auich_softc *sc, int pipe, struct auich_ring *ring)
{
	int i;
	uint32_t sts;

	bus_space_write_1(sc->iot, sc->aud_ioh, pipe + AUICH_CTRL, 0);

	/* wait for DMA halted and clear interrupt / event bits if needed */
	for (i = 0; i < 1000; i++) {
		sts = bus_space_read_2(sc->iot, sc->aud_ioh,
		    pipe + sc->sc_sts_reg);
		if (sts & (AUICH_CELV | AUICH_LVBCI | AUICH_BCIS | AUICH_FIFOE))
			bus_space_write_2(sc->iot, sc->aud_ioh,
			    pipe + sc->sc_sts_reg,
			    AUICH_CELV | AUICH_LVBCI |
			    AUICH_BCIS | AUICH_FIFOE);
		if (sts & AUICH_DCH)
			break;
		DELAY(100);
	}
	bus_space_write_1(sc->iot, sc->aud_ioh, pipe + AUICH_CTRL, AUICH_RR);

	if (i > 0)
		DPRINTF(AUICH_DEBUG_DMA,
		    ("auich_halt_pipe: halt took %d cycles\n", i));

	ring->running = 0;
}


int
auich_halt_output(void *v)
{
	struct auich_softc *sc = v;

	DPRINTF(AUICH_DEBUG_DMA, ("%s: halt_output\n", sc->sc_dev.dv_xname));

	mtx_enter(&audio_lock);
	auich_halt_pipe(sc, AUICH_PCMO, &sc->pcmo);

	sc->pcmo.intr = NULL;
	mtx_leave(&audio_lock);
	return 0;
}

int
auich_halt_input(void *v)
{
	struct auich_softc *sc = v;

	DPRINTF(AUICH_DEBUG_DMA,
	    ("%s: halt_input\n", sc->sc_dev.dv_xname));

	/* XXX halt both unless known otherwise */
	mtx_enter(&audio_lock);
	auich_halt_pipe(sc, AUICH_PCMI, &sc->pcmi);
	auich_halt_pipe(sc, AUICH_MICI, &sc->mici);

	sc->pcmi.intr = NULL;
	mtx_leave(&audio_lock);
	return 0;
}

int
auich_set_port(void *v, mixer_ctrl_t *cp)
{
	struct auich_softc *sc = v;
	return sc->codec_if->vtbl->mixer_set_port(sc->codec_if, cp);
}

int
auich_get_port(void *v, mixer_ctrl_t *cp)
{
	struct auich_softc *sc = v;
	return sc->codec_if->vtbl->mixer_get_port(sc->codec_if, cp);
}

int
auich_query_devinfo(void *v, mixer_devinfo_t *dp)
{
	struct auich_softc *sc = v;
	return sc->codec_if->vtbl->query_devinfo(sc->codec_if, dp);
}

void *
auich_allocm(void *v, int direction, size_t size, int pool, int flags)
{
	struct auich_softc *sc = v;
	struct auich_dma *p;
	int error;

	/* can only use 1 segment */
	if (size > AUICH_DMASEG_MAX) {
		DPRINTF(AUICH_DEBUG_DMA,
		    ("%s: requested buffer size too large: %zd", \
		    sc->sc_dev.dv_xname, size));
		return NULL;
	}

	p = malloc(sizeof(*p), pool, flags | M_ZERO);
	if (!p)
		return NULL;

	error = auich_allocmem(sc, size, PAGE_SIZE, p);
	if (error) {
		free(p, pool, sizeof(*p));
		return NULL;
	}

	if (direction == AUMODE_PLAY)
		sc->sc_pdma = p;
	else if (direction == AUMODE_RECORD)
		sc->sc_rdma = p;
	else
		sc->sc_cdma = p;

	return p->addr;
}

void
auich_freem(void *v, void *ptr, int pool)
{
	struct auich_softc *sc;
	struct auich_dma *p;

	sc = v;
	if (sc->sc_pdma != NULL && sc->sc_pdma->addr == ptr)
		p = sc->sc_pdma;
	else if (sc->sc_rdma != NULL && sc->sc_rdma->addr == ptr)
		p = sc->sc_rdma;
	else if (sc->sc_cdma != NULL && sc->sc_cdma->addr == ptr)
		p = sc->sc_cdma;
	else
		return;

	auich_freemem(sc, p);
	free(p, pool, sizeof(*p));
}

size_t
auich_round_buffersize(void *v, int direction, size_t size)
{
	if (size > AUICH_DMALIST_MAX * AUICH_DMASEG_MAX)
		size = AUICH_DMALIST_MAX * AUICH_DMASEG_MAX;

	return size;
}

int
auich_intr(void *v)
{
	struct auich_softc *sc = v;
	int ret = 0, sts, gsts;

	mtx_enter(&audio_lock);
	gsts = bus_space_read_4(sc->iot, sc->aud_ioh, AUICH_GSTS);
	DPRINTF(AUICH_DEBUG_INTR, ("auich_intr: gsts=%b\n", gsts, AUICH_GSTS_BITS));

	if (gsts & AUICH_POINT) {
		sts = bus_space_read_2(sc->iot, sc->aud_ioh,
		    AUICH_PCMO + sc->sc_sts_reg);
		DPRINTF(AUICH_DEBUG_INTR,
		    ("auich_intr: osts=%b\n", sts, AUICH_ISTS_BITS));

#ifdef AUICH_DEBUG
		if (sts & AUICH_FIFOE) {
			printf("%s: in fifo underrun # %u civ=%u ctrl=0x%x sts=%b\n",
			    sc->sc_dev.dv_xname, sc->pcmo_fifoe++,
			    bus_space_read_1(sc->iot, sc->aud_ioh,
				AUICH_PCMO + AUICH_CIV),
			    bus_space_read_1(sc->iot, sc->aud_ioh,
				AUICH_PCMO + AUICH_CTRL),
			    bus_space_read_2(sc->iot, sc->aud_ioh,
				AUICH_PCMO + sc->sc_sts_reg),
			    AUICH_ISTS_BITS);
		}
#endif

		if (sts & AUICH_BCIS)
			auich_intr_pipe(sc, AUICH_PCMO, &sc->pcmo);

		/* int ack */
		bus_space_write_2(sc->iot, sc->aud_ioh,
		    AUICH_PCMO + sc->sc_sts_reg, sts &
		    (AUICH_BCIS | AUICH_FIFOE));
		bus_space_write_4(sc->iot, sc->aud_ioh, AUICH_GSTS, AUICH_POINT);
		ret++;
	}

	if (gsts & AUICH_PIINT) {
		sts = bus_space_read_2(sc->iot, sc->aud_ioh,
		    AUICH_PCMI + sc->sc_sts_reg);
		DPRINTF(AUICH_DEBUG_INTR,
		    ("auich_intr: ists=%b\n", sts, AUICH_ISTS_BITS));

#ifdef AUICH_DEBUG
		if (sts & AUICH_FIFOE) {
			printf("%s: in fifo overrun # %u civ=%u ctrl=0x%x sts=%b\n",
			    sc->sc_dev.dv_xname, sc->pcmi_fifoe++,
			    bus_space_read_1(sc->iot, sc->aud_ioh,
				AUICH_PCMI + AUICH_CIV),
			    bus_space_read_1(sc->iot, sc->aud_ioh,
				AUICH_PCMI + AUICH_CTRL),
			    bus_space_read_2(sc->iot, sc->aud_ioh,
				AUICH_PCMI + sc->sc_sts_reg),
			    AUICH_ISTS_BITS);
		}
#endif

		if (sts & AUICH_BCIS)
			auich_intr_pipe(sc, AUICH_PCMI, &sc->pcmi);

		/* int ack */
		bus_space_write_2(sc->iot, sc->aud_ioh,
		    AUICH_PCMI + sc->sc_sts_reg, sts &
		    (AUICH_BCIS | AUICH_FIFOE));
		bus_space_write_4(sc->iot, sc->aud_ioh, AUICH_GSTS, AUICH_PIINT);
		ret++;
	}

	if (gsts & AUICH_MINT) {
		sts = bus_space_read_2(sc->iot, sc->aud_ioh,
		    AUICH_MICI + sc->sc_sts_reg);
		DPRINTF(AUICH_DEBUG_INTR,
		    ("auich_intr: ists=%b\n", sts, AUICH_ISTS_BITS));
#ifdef AUICH_DEBUG
		if (sts & AUICH_FIFOE) {
			printf("%s: in fifo overrun civ=%u ctrl=0x%x sts=%b\n",
			    sc->sc_dev.dv_xname,
			    bus_space_read_1(sc->iot, sc->aud_ioh,
				AUICH_MICI + AUICH_CIV),
			    bus_space_read_1(sc->iot, sc->aud_ioh,
				AUICH_MICI + AUICH_CTRL),
			    bus_space_read_2(sc->iot, sc->aud_ioh,
				AUICH_MICI + sc->sc_sts_reg),
			    AUICH_ISTS_BITS);
		}
#endif
		if (sts & AUICH_BCIS)
			auich_intr_pipe(sc, AUICH_MICI, &sc->mici);

		/* int ack */
		bus_space_write_2(sc->iot, sc->aud_ioh,
		    AUICH_MICI + sc->sc_sts_reg,
		    sts + (AUICH_BCIS | AUICH_FIFOE));

		bus_space_write_4(sc->iot, sc->aud_ioh, AUICH_GSTS, AUICH_MINT);
		ret++;
	}
	mtx_leave(&audio_lock);
	return ret;
}


void
auich_trigger_pipe(struct auich_softc *sc, int pipe, struct auich_ring *ring)
{
	int blksize, qptr, oqptr;
	struct auich_dmalist *q;

	blksize = ring->blksize;
	qptr = oqptr = bus_space_read_1(sc->iot, sc->aud_ioh, pipe + AUICH_CIV);

	/* XXX remove this when no one reports problems */
	if(oqptr >= AUICH_DMALIST_MAX) {
		printf("%s: Unexpected CIV: %d\n", sc->sc_dev.dv_xname, oqptr);
		qptr = oqptr = 0;
	}

	do {
		q = &ring->dmalist[qptr];
		q->base = ring->p;
		q->len = (blksize / sc->sc_sample_size) | AUICH_DMAF_IOC;

		DPRINTF(AUICH_DEBUG_INTR,
		    ("auich_trigger_pipe: %p, %p = %x @ 0x%x qptr=%d\n",
			&ring->dmalist[qptr], q, q->len, q->base, qptr));

		ring->p += blksize;
		if (ring->p >= ring->end)
			ring->p = ring->start;

		qptr = (qptr + 1) & AUICH_LVI_MASK;
	} while (qptr != oqptr);

	ring->qptr = qptr;

	DPRINTF(AUICH_DEBUG_DMA,
	    ("auich_trigger_pipe: qptr=%d\n", qptr));

	bus_space_write_1(sc->iot, sc->aud_ioh, pipe + AUICH_LVI,
	    (qptr - 1) & AUICH_LVI_MASK);
	bus_space_write_1(sc->iot, sc->aud_ioh, pipe + AUICH_CTRL,
	    AUICH_IOCE | AUICH_FEIE | AUICH_RPBM);

	ring->running = 1;
}

void
auich_intr_pipe(struct auich_softc *sc, int pipe, struct auich_ring *ring)
{
	int blksize, qptr, nqptr;
	struct auich_dmalist *q;

	blksize = ring->blksize;
	qptr = ring->qptr;
	nqptr = bus_space_read_1(sc->iot, sc->aud_ioh, pipe + AUICH_CIV);

	while (qptr != nqptr) {
		q = &ring->dmalist[qptr];
		q->base = ring->p;
		q->len = (blksize / sc->sc_sample_size) | AUICH_DMAF_IOC;

		DPRINTF(AUICH_DEBUG_INTR,
		    ("auich_intr: %p, %p = %x @ 0x%x qptr=%d\n",
		    &ring->dmalist[qptr], q, q->len, q->base, qptr));

		ring->p += blksize;
		if (ring->p >= ring->end)
			ring->p = ring->start;

		qptr = (qptr + 1) & AUICH_LVI_MASK;
		if (ring->intr)
			ring->intr(ring->arg);
		else
			printf("auich_intr: got progress with intr==NULL\n");

		ring->ap += blksize;
		if (ring->ap >= ring->size)
			ring->ap = 0;
	}
	ring->qptr = qptr;

	bus_space_write_1(sc->iot, sc->aud_ioh, pipe + AUICH_LVI,
	    (qptr - 1) & AUICH_LVI_MASK);
}


int
auich_trigger_output(void *v, void *start, void *end, int blksize,
    void (*intr)(void *), void *arg, struct audio_params *param)
{
	struct auich_softc *sc = v;
	struct auich_dma *p;
	size_t size;
#ifdef AUICH_DEBUG
	uint16_t sts;
	sts = bus_space_read_2(sc->iot, sc->aud_ioh,
	    AUICH_PCMO + sc->sc_sts_reg);
	DPRINTF(AUICH_DEBUG_DMA,
	    ("auich_trigger_output(%p, %p, %d, %p, %p, %p) sts=%b\n",
		start, end, blksize, intr, arg, param, sts, AUICH_ISTS_BITS));
#endif

	if (sc->sc_pdma->addr == start)
		p = sc->sc_pdma;
	else
		return -1;

	size = (size_t)((caddr_t)end - (caddr_t)start);
	sc->pcmo.size = size;
	sc->pcmo.intr = intr;
	sc->pcmo.arg = arg;

	/*
	 * The logic behind this is:
	 * setup one buffer to play, then LVI dump out the rest
	 * to the scatter-gather chain.
	 */
	sc->pcmo.start = p->segs->ds_addr;
	sc->pcmo.p = sc->pcmo.start;
	sc->pcmo.end = sc->pcmo.start + size;
	sc->pcmo.blksize = blksize;

	mtx_enter(&audio_lock);
	bus_space_write_4(sc->iot, sc->aud_ioh, AUICH_PCMO + AUICH_BDBAR,
	    sc->sc_cddma + AUICH_PCMO_OFF(0));
	auich_trigger_pipe(sc, AUICH_PCMO, &sc->pcmo);
	mtx_leave(&audio_lock);
	return 0;
}

int
auich_trigger_input(void *v, void *start, void *end, int blksize,
    void (*intr)(void *), void *arg, struct audio_params *param)
{
	struct auich_softc *sc = v;
	struct auich_dma *p;
	size_t size;

	DPRINTF(AUICH_DEBUG_DMA,
	    ("auich_trigger_input(%p, %p, %d, %p, %p, %p) sts=%b\n",
		start, end, blksize, intr, arg, param,
		bus_space_read_2(sc->iot, sc->aud_ioh,
		    AUICH_PCMI + sc->sc_sts_reg),
		AUICH_ISTS_BITS));

	if (sc->sc_rdma->addr == start)
		p = sc->sc_rdma;
	else
		return -1;

	size = (size_t)((caddr_t)end - (caddr_t)start);
	sc->pcmi.size = size;
	sc->pcmi.intr = intr;
	sc->pcmi.arg = arg;

	/*
	 * The logic behind this is:
	 * setup one buffer to play, then LVI dump out the rest
	 * to the scatter-gather chain.
	 */
	sc->pcmi.start = p->segs->ds_addr;
	sc->pcmi.p = sc->pcmi.start;
	sc->pcmi.end = sc->pcmi.start + size;
	sc->pcmi.blksize = blksize;
	mtx_enter(&audio_lock);
	bus_space_write_4(sc->iot, sc->aud_ioh, AUICH_PCMI + AUICH_BDBAR,
	    sc->sc_cddma + AUICH_PCMI_OFF(0));
	auich_trigger_pipe(sc, AUICH_PCMI, &sc->pcmi);
	mtx_leave(&audio_lock);
	return 0;
}


int
auich_allocmem(struct auich_softc *sc, size_t size, size_t align,
    struct auich_dma *p)
{
	int error;

	p->size = size;
	error = bus_dmamem_alloc(sc->dmat, p->size, align, 0, p->segs, 1,
	    &p->nsegs, BUS_DMA_NOWAIT);
	if (error) {
		DPRINTF(AUICH_DEBUG_DMA, 
		    ("%s: bus_dmamem_alloc failed: error %d\n",
		    sc->sc_dev.dv_xname, error));
		return error;
	}

	error = bus_dmamem_map(sc->dmat, p->segs, 1, p->size, &p->addr,
	    BUS_DMA_NOWAIT | sc->sc_dmamap_flags);
	if (error) {
		DPRINTF(AUICH_DEBUG_DMA, 
		    ("%s: bus_dmamem_map failed: error %d\n",
		    sc->sc_dev.dv_xname, error));
		goto free;
	}

	error = bus_dmamap_create(sc->dmat, p->size, 1, p->size, 0,
	    BUS_DMA_NOWAIT, &p->map);
	if (error) {
		DPRINTF(AUICH_DEBUG_DMA, 
		    ("%s: bus_dmamap_create failed: error %d\n",
		    sc->sc_dev.dv_xname, error));
		goto unmap;
	}

	error = bus_dmamap_load(sc->dmat, p->map, p->addr, p->size, NULL,
	    BUS_DMA_NOWAIT);
	if (error) {
		DPRINTF(AUICH_DEBUG_DMA,
		    ("%s: bus_dmamap_load failed: error %d\n",
		    sc->sc_dev.dv_xname, error));
		goto destroy;
	}
	return 0;

 destroy:
	bus_dmamap_destroy(sc->dmat, p->map);
 unmap:
	bus_dmamem_unmap(sc->dmat, p->addr, p->size);
 free:
	bus_dmamem_free(sc->dmat, p->segs, p->nsegs);
	return error;
}


void
auich_freemem(struct auich_softc *sc, struct auich_dma *p)
{
	bus_dmamap_unload(sc->dmat, p->map);
	bus_dmamap_destroy(sc->dmat, p->map);
	bus_dmamem_unmap(sc->dmat, p->addr, p->size);
	bus_dmamem_free(sc->dmat, p->segs, p->nsegs);
}



int
auich_alloc_cdata(struct auich_softc *sc)
{
	bus_dma_segment_t seg;
	int error, rseg;

	/*
	 * Allocate the control data structure, and create and load the
	 * DMA map for it.
	 */
	if ((error = bus_dmamem_alloc(sc->dmat, sizeof(struct auich_cdata),
	    PAGE_SIZE, 0, &seg, 1, &rseg, 0)) != 0) {
		printf("%s: unable to allocate control data, error = %d\n",
		    sc->sc_dev.dv_xname, error);
		goto fail_0;
	}

	if ((error = bus_dmamem_map(sc->dmat, &seg, 1,
	    sizeof(struct auich_cdata), (caddr_t *) &sc->sc_cdata,
	    sc->sc_dmamap_flags)) != 0) {
		printf("%s: unable to map control data, error = %d\n",
		    sc->sc_dev.dv_xname, error);
		goto fail_1;
	}

	if ((error = bus_dmamap_create(sc->dmat, sizeof(struct auich_cdata), 1,
	    sizeof(struct auich_cdata), 0, 0, &sc->sc_cddmamap)) != 0) {
		printf("%s: unable to create control data DMA map, "
		    "error = %d\n", sc->sc_dev.dv_xname, error);
		goto fail_2;
	}

	if ((error = bus_dmamap_load(sc->dmat, sc->sc_cddmamap, sc->sc_cdata,
	    sizeof(struct auich_cdata), NULL, 0)) != 0) {
		printf("%s: unable to load control data DMA map, "
		    "error = %d\n", sc->sc_dev.dv_xname, error);
		goto fail_3;
	}

	sc->pcmo.dmalist = sc->sc_cdata->ic_dmalist_pcmo;
	sc->pcmi.dmalist = sc->sc_cdata->ic_dmalist_pcmi;
	sc->mici.dmalist = sc->sc_cdata->ic_dmalist_mici;

	return 0;

 fail_3:
	bus_dmamap_destroy(sc->dmat, sc->sc_cddmamap);
 fail_2:
	bus_dmamem_unmap(sc->dmat, (caddr_t) sc->sc_cdata,
	    sizeof(struct auich_cdata));
 fail_1:
	bus_dmamem_free(sc->dmat, &seg, rseg);
 fail_0:
	return error;
}

void
auich_resume(struct auich_softc *sc)
{
	/* SiS 7012 needs special handling */
	if (PCI_VENDOR(sc->pci_id) == PCI_VENDOR_SIS &&
	    PCI_PRODUCT(sc->pci_id) == PCI_PRODUCT_SIS_7012_ACA) {
		/* un-mute output */
		bus_space_write_4(sc->iot, sc->aud_ioh, ICH_SIS_NV_CTL,
		    bus_space_read_4(sc->iot, sc->aud_ioh, ICH_SIS_NV_CTL) |
		    ICH_SIS_CTL_UNMUTE);
	}

	ac97_resume(&sc->host_if, sc->codec_if);
}

/* -------------------------------------------------------------------- */
/* Calibrate card (some boards are overclocked and need scaling) */

unsigned int
auich_calibrate(struct auich_softc *sc)
{
	struct timeval t1, t2;
	u_int8_t civ, ociv;
	uint16_t sts, osts;
	u_int32_t wait_us, actual_48k_rate, bytes, ac97rate;
	void *temp_buffer;
	struct auich_dma *p;

	ac97rate = AUICH_FIXED_RATE;
	/*
	 * Grab audio from input for fixed interval and compare how
	 * much we actually get with what we expect.  Interval needs
	 * to be sufficiently short that no interrupts are
	 * generated.
	 * XXX: Is this true? We don't request any interrupts,
	 * so why should the chip issue any?
	 */

	/* Setup a buffer */
	bytes = 16000;
	temp_buffer = auich_allocm(sc, 0, bytes, M_DEVBUF, M_NOWAIT);
	if (temp_buffer == NULL)
		return (ac97rate);
	if (sc->sc_cdma->addr == temp_buffer) {
		p = sc->sc_cdma;
	} else {
		printf("auich_calibrate: bad address %p\n", temp_buffer);
		return (ac97rate);
	}

	/* get current CIV (usually 0 after reboot) */
	ociv = civ = bus_space_read_1(sc->iot, sc->aud_ioh, AUICH_PCMI + AUICH_CIV);
	sc->pcmi.dmalist[civ].base = p->map->dm_segs[0].ds_addr;
	sc->pcmi.dmalist[civ].len = bytes / sc->sc_sample_size;


	/*
	 * our data format is stereo, 16 bit so each sample is 4 bytes.
	 * assuming we get 48000 samples per second, we get 192000 bytes/sec.
	 * we're going to start recording with interrupts disabled and measure
	 * the time taken for one block to complete.  we know the block size,
	 * we know the time in microseconds, we calculate the sample rate:
	 *
	 * actual_rate [bps] = bytes / (time [s] * 4)
	 * actual_rate [bps] = (bytes * 1000000) / (time [us] * 4)
	 * actual_rate [Hz] = (bytes * 250000) / time [us]
	 */

	/* prepare */
	bus_space_write_4(sc->iot, sc->aud_ioh, AUICH_PCMI + AUICH_BDBAR,
	    sc->sc_cddma + AUICH_PCMI_OFF(0));
	/* we got only one valid sample, so set LVI to CIV
	 * otherwise we provoke a AUICH_FIFOE FIFO error
	 * which will confuse the chip later on. */
	bus_space_write_1(sc->iot, sc->aud_ioh, AUICH_PCMI + AUICH_LVI,
	    civ & AUICH_LVI_MASK);

	/* start, but don't request any interrupts */
	microuptime(&t1);
	bus_space_write_1(sc->iot, sc->aud_ioh, AUICH_PCMI + AUICH_CTRL,
	    AUICH_RPBM);

	/* XXX remove this sometime */
	osts = bus_space_read_2(sc->iot, sc->aud_ioh,
	    AUICH_PCMI + sc->sc_sts_reg);
	/* wait */
	while(1) {
		microuptime(&t2);
		sts = bus_space_read_2(sc->iot, sc->aud_ioh,
		    AUICH_PCMI + sc->sc_sts_reg);
		civ = bus_space_read_1(sc->iot, sc->aud_ioh,
		    AUICH_PCMI + AUICH_CIV);
	  
		/* turn time delta into us */
		wait_us = ((t2.tv_sec - t1.tv_sec) * 1000000) +
		    t2.tv_usec - t1.tv_usec;

		/* this should actually never happen because civ==lvi */
		if ((civ & AUICH_LVI_MASK) != (ociv & AUICH_LVI_MASK)) {
			printf("%s: ac97 CIV progressed after %d us sts=%b civ=%u\n",
			    sc->sc_dev.dv_xname, wait_us, sts,
			    AUICH_ISTS_BITS, civ);
			ociv = civ;
		}
		/* normal completion */
		if (sts & (AUICH_DCH | AUICH_CELV | AUICH_LVBCI))
			break;
		/*
		 * check for strange changes in STS -
		 * XXX remove it when everything is fine
		 */
		if (sts != osts) {
			printf("%s: ac97 sts changed after %d us sts=%b civ=%u\n",
			    sc->sc_dev.dv_xname, wait_us, sts,
			    AUICH_ISTS_BITS, civ);
			osts = sts;
		}
		/*
		 * timeout: we expect 83333 us for 48k sampling rate,
		 * 600000 us will be enough even for 8k sampling rate
		 */
		if (wait_us > 600000) {
			printf("%s: ac97 link rate timed out %d us sts=%b civ=%u\n",
			    sc->sc_dev.dv_xname, wait_us, sts,
			    AUICH_ISTS_BITS, civ);
			/* reset and clean up*/
			auich_halt_pipe(sc, AUICH_PCMI, &sc->pcmi);
			auich_halt_pipe(sc, AUICH_MICI, &sc->mici);
			auich_freem(sc, temp_buffer, M_DEVBUF);
			/* return default sample rate */
			return (ac97rate);
		}
	}

	DPRINTF(AUICH_DEBUG_CODECIO,
	    ("%s: ac97 link rate calibration took %d us sts=%b civ=%u\n",
		sc->sc_dev.dv_xname, wait_us, sts, AUICH_ISTS_BITS, civ));

	/* reset and clean up */
	auich_halt_pipe(sc, AUICH_PCMI, &sc->pcmi);
	auich_halt_pipe(sc, AUICH_MICI, &sc->mici);
	auich_freem(sc, temp_buffer, M_DEVBUF);

#ifdef AUICH_DEBUG
	sts = bus_space_read_2(sc->iot, sc->aud_ioh,
	    AUICH_PCMI + sc->sc_sts_reg);
	civ = bus_space_read_4(sc->iot, sc->aud_ioh,
	    AUICH_PCMI + AUICH_CIV);
	printf("%s: after calibration and reset sts=%b civ=%u\n",
	    sc->sc_dev.dv_xname, sts, AUICH_ISTS_BITS, civ);
#endif

	/* now finally calculate measured samplerate */
	actual_48k_rate = (bytes * 250000) / wait_us;

	if (actual_48k_rate <= 48500)
		ac97rate = AUICH_FIXED_RATE;
	else
		ac97rate = actual_48k_rate;

	DPRINTF(AUICH_DEBUG_CODECIO, ("%s: measured ac97 link rate at %d Hz",
		sc->sc_dev.dv_xname, actual_48k_rate));
	if (ac97rate != actual_48k_rate)
		DPRINTF(AUICH_DEBUG_CODECIO, (", will use %d Hz", ac97rate));
	DPRINTF(AUICH_DEBUG_CODECIO, ("\n"));

	return (ac97rate);
}
