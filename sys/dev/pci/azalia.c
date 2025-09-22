/*	$OpenBSD: azalia.c,v 1.290 2024/08/18 14:42:56 deraadt Exp $	*/
/*	$NetBSD: azalia.c,v 1.20 2006/05/07 08:31:44 kent Exp $	*/

/*-
 * Copyright (c) 2005 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by TAMURA Kent
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
 * High Definition Audio Specification
 *
 * http://www.intel.com/content/dam/www/public/us/en/documents/product-specifications/high-definition-audio-specification.pdf
 *
 *
 * TO DO:
 *  - multiple codecs (needed?)
 *  - multiple streams (needed?)
 */

#include <sys/param.h>
#include <sys/fcntl.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/systm.h>
#include <sys/timeout.h>
#include <dev/audio_if.h>
#include <dev/pci/pcidevs.h>
#include <dev/pci/pcivar.h>

#include <dev/pci/azalia.h>

typedef struct audio_params audio_params_t;

struct audio_format {
	void *driver_data;
	int32_t mode;
	u_int encoding;
	u_int precision;
	u_int channels;

	/**
	 * 0: frequency[0] is lower limit, and frequency[1] is higher limit.
	 * 1-16: frequency[0] to frequency[frequency_type-1] are valid.
	 */
	u_int frequency_type;

#define	AUFMT_MAX_FREQUENCIES	16
	/**
	 * sampling rates
	 */
	u_int frequency[AUFMT_MAX_FREQUENCIES];
};


#ifdef AZALIA_DEBUG
# define DPRINTFN(n,x)	do { if (az_debug > (n)) printf x; } while (0/*CONSTCOND*/)
int az_debug = 0;
#else
# define DPRINTFN(n,x)	do {} while (0/*CONSTCOND*/)
#endif


/* ----------------------------------------------------------------
 * ICH6/ICH7 constant values
 * ---------------------------------------------------------------- */

/* PCI registers */
#define ICH_PCI_HDBARL	0x10
#define ICH_PCI_HDBARU	0x14
#define ICH_PCI_HDCTL	0x40
#define		ICH_PCI_HDCTL_CLKDETCLR		0x08
#define		ICH_PCI_HDCTL_CLKDETEN		0x04
#define		ICH_PCI_HDCTL_CLKDETINV		0x02
#define		ICH_PCI_HDCTL_SIGNALMODE	0x01
#define ICH_PCI_HDTCSEL	0x44
#define		ICH_PCI_HDTCSEL_MASK	0x7
#define ICH_PCI_MMC	0x62
#define		ICH_PCI_MMC_ME		0x1

/* internal types */

typedef struct {
	bus_dmamap_t map;
	caddr_t addr;		/* kernel virtual address */
	bus_dma_segment_t segments[1];
	size_t size;
} azalia_dma_t;
#define AZALIA_DMA_DMAADDR(p)	((p)->map->dm_segs[0].ds_addr)

typedef struct {
	struct azalia_t *az;
	int regbase;
	int number;
	int dir;		/* AUMODE_PLAY or AUMODE_RECORD */
	uint32_t intr_bit;
	azalia_dma_t bdlist;
	azalia_dma_t buffer;
	void (*intr)(void*);
	void *intr_arg;
	int bufsize;
	uint16_t fmt;
	int blk;
	unsigned int swpos;		/* position in the audio(4) layer */
} stream_t;
#define STR_READ_1(s, r)	\
	bus_space_read_1((s)->az->iot, (s)->az->ioh, (s)->regbase + HDA_SD_##r)
#define STR_READ_2(s, r)	\
	bus_space_read_2((s)->az->iot, (s)->az->ioh, (s)->regbase + HDA_SD_##r)
#define STR_READ_4(s, r)	\
	bus_space_read_4((s)->az->iot, (s)->az->ioh, (s)->regbase + HDA_SD_##r)
#define STR_WRITE_1(s, r, v)	\
	bus_space_write_1((s)->az->iot, (s)->az->ioh, (s)->regbase + HDA_SD_##r, v)
#define STR_WRITE_2(s, r, v)	\
	bus_space_write_2((s)->az->iot, (s)->az->ioh, (s)->regbase + HDA_SD_##r, v)
#define STR_WRITE_4(s, r, v)	\
	bus_space_write_4((s)->az->iot, (s)->az->ioh, (s)->regbase + HDA_SD_##r, v)

typedef struct azalia_t {
	struct device dev;

	pci_chipset_tag_t pc;
	pcitag_t tag;
	void *ih;
	bus_space_tag_t iot;
	bus_space_handle_t ioh;
	bus_size_t map_size;
	bus_dma_tag_t dmat;
	pcireg_t pciid;
	uint32_t subid;

	codec_t *codecs;
	int ncodecs;		/* number of codecs */
	int codecno;		/* index of the using codec */
	int detached;		/* 1 if failed to initialize, 2 if
				 * azalia_pci_detach has run
				 */
	azalia_dma_t corb_dma;
	int corb_entries;
	uint8_t corbsize;
	azalia_dma_t rirb_dma;
	int rirb_entries;
	uint8_t rirbsize;
	int rirb_rp;
#define UNSOLQ_SIZE	256
	rirb_entry_t *unsolq;
	int unsolq_wp;
	int unsolq_rp;
	int unsolq_kick;
	struct timeout unsol_to;

	int ok64;
	int nistreams, nostreams, nbstreams;
	stream_t pstream;
	stream_t rstream;
	uint32_t intctl;
} azalia_t;
#define XNAME(sc)		((sc)->dev.dv_xname)
#define AZ_READ_1(z, r)		bus_space_read_1((z)->iot, (z)->ioh, HDA_##r)
#define AZ_READ_2(z, r)		bus_space_read_2((z)->iot, (z)->ioh, HDA_##r)
#define AZ_READ_4(z, r)		bus_space_read_4((z)->iot, (z)->ioh, HDA_##r)
#define AZ_WRITE_1(z, r, v)	bus_space_write_1((z)->iot, (z)->ioh, HDA_##r, v)
#define AZ_WRITE_2(z, r, v)	bus_space_write_2((z)->iot, (z)->ioh, HDA_##r, v)
#define AZ_WRITE_4(z, r, v)	bus_space_write_4((z)->iot, (z)->ioh, HDA_##r, v)


/* prototypes */
uint8_t azalia_pci_read(pci_chipset_tag_t, pcitag_t, int);
void	azalia_pci_write(pci_chipset_tag_t, pcitag_t, int, uint8_t);
int	azalia_pci_match(struct device *, void *, void *);
void	azalia_pci_attach(struct device *, struct device *, void *);
int	azalia_pci_activate(struct device *, int);
int	azalia_pci_detach(struct device *, int);
void	azalia_configure_pci(azalia_t *);
int	azalia_intr(void *);
void	azalia_print_codec(codec_t *);
int	azalia_reset(azalia_t *);
int	azalia_get_ctrlr_caps(azalia_t *);
int	azalia_init(azalia_t *, int);
int	azalia_init_codecs(azalia_t *);
int	azalia_init_streams(azalia_t *);
void	azalia_shutdown(void *);
int	azalia_halt_corb(azalia_t *);
int	azalia_init_corb(azalia_t *, int);
int	azalia_halt_rirb(azalia_t *);
int	azalia_init_rirb(azalia_t *, int);
int	azalia_set_command(azalia_t *, nid_t, int, uint32_t, uint32_t);
int	azalia_get_response(azalia_t *, uint32_t *);
void	azalia_rirb_kick_unsol_events(void *);
void	azalia_rirb_intr(azalia_t *);
int	azalia_alloc_dmamem(azalia_t *, size_t, size_t, azalia_dma_t *);
void	azalia_free_dmamem(const azalia_t *, azalia_dma_t*);

int	azalia_codec_init(codec_t *);
int	azalia_codec_delete(codec_t *);
void	azalia_codec_add_bits(codec_t *, int, uint32_t, int);
void	azalia_codec_add_format(codec_t *, int, int, uint32_t, int32_t);
int	azalia_codec_connect_stream(stream_t *);
int	azalia_codec_disconnect_stream(stream_t *);
void	azalia_codec_print_audiofunc(const codec_t *);
void	azalia_codec_print_groups(const codec_t *);
int	azalia_codec_find_defdac(codec_t *, int, int);
int	azalia_codec_find_defadc(codec_t *, int, int);
int	azalia_codec_find_defadc_sub(codec_t *, nid_t, int, int);
int	azalia_codec_init_volgroups(codec_t *);
int	azalia_codec_sort_pins(codec_t *);
int	azalia_codec_select_micadc(codec_t *);
int	azalia_codec_select_dacs(codec_t *);
int	azalia_codec_select_spkrdac(codec_t *);
int	azalia_codec_find_inputmixer(codec_t *);

int	azalia_widget_init(widget_t *, const codec_t *, int);
int	azalia_widget_label_widgets(codec_t *);
int	azalia_widget_init_audio(widget_t *, const codec_t *);
int	azalia_widget_init_pin(widget_t *, const codec_t *);
int	azalia_widget_init_connection(widget_t *, const codec_t *);
int	azalia_widget_check_conn(codec_t *, int, int);
int	azalia_widget_sole_conn(codec_t *, nid_t);
void	azalia_widget_print_widget(const widget_t *, const codec_t *);
void	azalia_widget_print_audio(const widget_t *, const char *);
void	azalia_widget_print_pin(const widget_t *);

int	azalia_stream_init(stream_t *, azalia_t *, int, int, int);
int	azalia_stream_reset(stream_t *);
int	azalia_stream_start(stream_t *);
int	azalia_stream_halt(stream_t *);
int	azalia_stream_intr(stream_t *);

int	azalia_open(void *, int);
void	azalia_close(void *);
int	azalia_set_params(void *, int, int, audio_params_t *,
	audio_params_t *);
unsigned int azalia_set_blksz(void *, int,
	struct audio_params *, struct audio_params *, unsigned int);
unsigned int azalia_set_nblks(void *, int,
	struct audio_params *, unsigned int, unsigned int);
int	azalia_halt_output(void *);
int	azalia_halt_input(void *);
int	azalia_set_port(void *, mixer_ctrl_t *);
int	azalia_get_port(void *, mixer_ctrl_t *);
int	azalia_query_devinfo(void *, mixer_devinfo_t *);
void	*azalia_allocm(void *, int, size_t, int, int);
void	azalia_freem(void *, void *, int);
size_t	azalia_round_buffersize(void *, int, size_t);
int	azalia_trigger_output(void *, void *, void *, int,
	void (*)(void *), void *, audio_params_t *);
int	azalia_trigger_input(void *, void *, void *, int,
	void (*)(void *), void *, audio_params_t *);

int	azalia_params2fmt(const audio_params_t *, uint16_t *);

int	azalia_match_format(codec_t *, int, audio_params_t *);
int	azalia_set_params_sub(codec_t *, int, audio_params_t *);

int	azalia_suspend(azalia_t *);
int	azalia_resume(azalia_t *);
int	azalia_resume_codec(codec_t *);

/* variables */
const struct cfattach azalia_ca = {
	sizeof(azalia_t), azalia_pci_match, azalia_pci_attach,
	azalia_pci_detach, azalia_pci_activate
};

struct cfdriver azalia_cd = {
	NULL, "azalia", DV_DULL, CD_SKIPHIBERNATE
};

const struct audio_hw_if azalia_hw_if = {
	.open = azalia_open,
	.close = azalia_close,
	.set_params = azalia_set_params,
	.halt_output = azalia_halt_output,
	.halt_input = azalia_halt_input,
	.set_port = azalia_set_port,
	.get_port = azalia_get_port,
	.query_devinfo = azalia_query_devinfo,
	.allocm = azalia_allocm,
	.freem = azalia_freem,
	.round_buffersize = azalia_round_buffersize,
	.trigger_output = azalia_trigger_output,
	.trigger_input = azalia_trigger_input,
	.set_blksz = azalia_set_blksz,
	.set_nblks = azalia_set_nblks,
};

static const char *pin_devices[16] = {
	AudioNline, AudioNspeaker, AudioNheadphone, AudioNcd,
	"SPDIF", "digital-out", "modem-line", "modem-handset",
	"line-in", AudioNaux, AudioNmicrophone, "telephony",
	"SPDIF-in", "digital-in", "beep", "other"};
static const char *wtypes[16] = {
	"dac", "adc", "mix", "sel", "pin", "pow", "volume",
	"beep", "wid08", "wid09", "wid0a", "wid0b", "wid0c",
	"wid0d", "wid0e", "vendor"};
static const char *line_colors[16] = {
	"unk", "blk", "gry", "blu", "grn", "red", "org", "yel",
	"pur", "pnk", "0xa", "0xb", "0xc", "0xd", "wht", "oth"};

/* ================================================================
 * PCI functions
 * ================================================================ */

#define ATI_PCIE_SNOOP_REG		0x42
#define ATI_PCIE_SNOOP_MASK		0xf8
#define ATI_PCIE_SNOOP_ENABLE		0x02
#define NVIDIA_PCIE_SNOOP_REG		0x4e
#define NVIDIA_PCIE_SNOOP_MASK		0xf0
#define NVIDIA_PCIE_SNOOP_ENABLE	0x0f
#define NVIDIA_HDA_ISTR_COH_REG		0x4d
#define NVIDIA_HDA_OSTR_COH_REG		0x4c
#define NVIDIA_HDA_STR_COH_ENABLE	0x01
#define INTEL_PCIE_NOSNOOP_REG		0x79
#define INTEL_PCIE_NOSNOOP_MASK		0xf7
#define INTEL_PCIE_NOSNOOP_ENABLE	0x08

uint8_t
azalia_pci_read(pci_chipset_tag_t pc, pcitag_t pa, int reg)
{
	return (pci_conf_read(pc, pa, (reg & ~0x03)) >>
	    ((reg & 0x03) * 8) & 0xff);
}

void
azalia_pci_write(pci_chipset_tag_t pc, pcitag_t pa, int reg, uint8_t val)
{
	pcireg_t pcival;

	pcival = pci_conf_read(pc, pa, (reg & ~0x03));
	pcival &= ~(0xff << ((reg & 0x03) * 8));
	pcival |= (val << ((reg & 0x03) * 8));
	pci_conf_write(pc, pa, (reg & ~0x03), pcival);
}

void
azalia_configure_pci(azalia_t *az)
{
	pcireg_t v;
	uint8_t reg;

	/* enable back-to-back */
	v = pci_conf_read(az->pc, az->tag, PCI_COMMAND_STATUS_REG);
	pci_conf_write(az->pc, az->tag, PCI_COMMAND_STATUS_REG,
	    v | PCI_COMMAND_BACKTOBACK_ENABLE);

	/* traffic class select */
	v = pci_conf_read(az->pc, az->tag, ICH_PCI_HDTCSEL);
	pci_conf_write(az->pc, az->tag, ICH_PCI_HDTCSEL,
	    v & ~(ICH_PCI_HDTCSEL_MASK));

	/* enable PCIe snoop */
	switch (PCI_PRODUCT(az->pciid)) {
	case PCI_PRODUCT_ATI_SB450_HDA:
	case PCI_PRODUCT_ATI_SBX00_HDA:
	case PCI_PRODUCT_AMD_15_6X_AUDIO:
	case PCI_PRODUCT_AMD_17_HDA:
	case PCI_PRODUCT_AMD_17_1X_HDA:
	case PCI_PRODUCT_AMD_17_3X_HDA:
	case PCI_PRODUCT_AMD_HUDSON2_HDA:
		reg = azalia_pci_read(az->pc, az->tag, ATI_PCIE_SNOOP_REG);
		reg &= ATI_PCIE_SNOOP_MASK;
		reg |= ATI_PCIE_SNOOP_ENABLE;
		azalia_pci_write(az->pc, az->tag, ATI_PCIE_SNOOP_REG, reg);
		break;
	case PCI_PRODUCT_NVIDIA_MCP51_HDA:
	case PCI_PRODUCT_NVIDIA_MCP55_HDA:
	case PCI_PRODUCT_NVIDIA_MCP61_HDA_1:
	case PCI_PRODUCT_NVIDIA_MCP61_HDA_2:
	case PCI_PRODUCT_NVIDIA_MCP65_HDA_1:
	case PCI_PRODUCT_NVIDIA_MCP65_HDA_2:
	case PCI_PRODUCT_NVIDIA_MCP67_HDA_1:
	case PCI_PRODUCT_NVIDIA_MCP67_HDA_2:
	case PCI_PRODUCT_NVIDIA_MCP73_HDA_1:
	case PCI_PRODUCT_NVIDIA_MCP73_HDA_2:
	case PCI_PRODUCT_NVIDIA_MCP77_HDA_1:
	case PCI_PRODUCT_NVIDIA_MCP77_HDA_2:
	case PCI_PRODUCT_NVIDIA_MCP77_HDA_3:
	case PCI_PRODUCT_NVIDIA_MCP77_HDA_4:
	case PCI_PRODUCT_NVIDIA_MCP79_HDA_1:
	case PCI_PRODUCT_NVIDIA_MCP79_HDA_2:
	case PCI_PRODUCT_NVIDIA_MCP79_HDA_3:
	case PCI_PRODUCT_NVIDIA_MCP79_HDA_4:
	case PCI_PRODUCT_NVIDIA_MCP89_HDA_1:
	case PCI_PRODUCT_NVIDIA_MCP89_HDA_2:
	case PCI_PRODUCT_NVIDIA_MCP89_HDA_3:
	case PCI_PRODUCT_NVIDIA_MCP89_HDA_4:
		reg = azalia_pci_read(az->pc, az->tag,
		    NVIDIA_HDA_OSTR_COH_REG);
		reg |= NVIDIA_HDA_STR_COH_ENABLE;
		azalia_pci_write(az->pc, az->tag,
		    NVIDIA_HDA_OSTR_COH_REG, reg);

		reg = azalia_pci_read(az->pc, az->tag,
		    NVIDIA_HDA_ISTR_COH_REG);
		reg |= NVIDIA_HDA_STR_COH_ENABLE;
		azalia_pci_write(az->pc, az->tag,
		    NVIDIA_HDA_ISTR_COH_REG, reg);

		reg = azalia_pci_read(az->pc, az->tag,
		    NVIDIA_PCIE_SNOOP_REG);
		reg &= NVIDIA_PCIE_SNOOP_MASK;
		reg |= NVIDIA_PCIE_SNOOP_ENABLE;
		azalia_pci_write(az->pc, az->tag,
		    NVIDIA_PCIE_SNOOP_REG, reg);

		reg = azalia_pci_read(az->pc, az->tag,
		    NVIDIA_PCIE_SNOOP_REG);
		if ((reg & NVIDIA_PCIE_SNOOP_ENABLE) !=
		    NVIDIA_PCIE_SNOOP_ENABLE) {
			printf(": could not enable PCIe cache snooping!\n");
		}
		break;
	case PCI_PRODUCT_INTEL_82801FB_HDA:
	case PCI_PRODUCT_INTEL_82801GB_HDA:
	case PCI_PRODUCT_INTEL_82801H_HDA:
	case PCI_PRODUCT_INTEL_82801I_HDA:
	case PCI_PRODUCT_INTEL_82801JI_HDA:
	case PCI_PRODUCT_INTEL_82801JD_HDA:
	case PCI_PRODUCT_INTEL_6321ESB_HDA:
	case PCI_PRODUCT_INTEL_3400_HDA:
	case PCI_PRODUCT_INTEL_QS57_HDA:
	case PCI_PRODUCT_INTEL_6SERIES_HDA:
	case PCI_PRODUCT_INTEL_7SERIES_HDA:
	case PCI_PRODUCT_INTEL_8SERIES_HDA:
	case PCI_PRODUCT_INTEL_8SERIES_LP_HDA:
	case PCI_PRODUCT_INTEL_9SERIES_HDA:
	case PCI_PRODUCT_INTEL_9SERIES_LP_HDA:
	case PCI_PRODUCT_INTEL_100SERIES_HDA:
	case PCI_PRODUCT_INTEL_100SERIES_H_HDA:
	case PCI_PRODUCT_INTEL_100SERIES_LP_HDA:
	case PCI_PRODUCT_INTEL_200SERIES_HDA:
	case PCI_PRODUCT_INTEL_200SERIES_U_HDA:
	case PCI_PRODUCT_INTEL_300SERIES_CAVS:
	case PCI_PRODUCT_INTEL_300SERIES_U_HDA:
	case PCI_PRODUCT_INTEL_400SERIES_CAVS:
	case PCI_PRODUCT_INTEL_400SERIES_LP_HDA:
	case PCI_PRODUCT_INTEL_495SERIES_LP_HDA:
	case PCI_PRODUCT_INTEL_500SERIES_HDA:
	case PCI_PRODUCT_INTEL_500SERIES_HDA_2:
	case PCI_PRODUCT_INTEL_500SERIES_LP_HDA:
	case PCI_PRODUCT_INTEL_600SERIES_HDA:
	case PCI_PRODUCT_INTEL_600SERIES_LP_HDA:
	case PCI_PRODUCT_INTEL_700SERIES_HDA:
	case PCI_PRODUCT_INTEL_700SERIES_LP_HDA:
	case PCI_PRODUCT_INTEL_C600_HDA:
	case PCI_PRODUCT_INTEL_C610_HDA_1:
	case PCI_PRODUCT_INTEL_C610_HDA_2:
	case PCI_PRODUCT_INTEL_C620_HDA_1:
	case PCI_PRODUCT_INTEL_C620_HDA_2:
	case PCI_PRODUCT_INTEL_APOLLOLAKE_HDA:
	case PCI_PRODUCT_INTEL_BAYTRAIL_HDA:
	case PCI_PRODUCT_INTEL_BSW_HDA:
	case PCI_PRODUCT_INTEL_GLK_HDA:
	case PCI_PRODUCT_INTEL_JSL_HDA:
	case PCI_PRODUCT_INTEL_EHL_HDA:
	case PCI_PRODUCT_INTEL_ADL_N_HDA:
	case PCI_PRODUCT_INTEL_MTL_HDA:
		reg = azalia_pci_read(az->pc, az->tag,
		    INTEL_PCIE_NOSNOOP_REG);
		reg &= INTEL_PCIE_NOSNOOP_MASK;
		azalia_pci_write(az->pc, az->tag,
		    INTEL_PCIE_NOSNOOP_REG, reg);
		break;
	}
}

const struct pci_matchid azalia_pci_devices[] = {
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_200SERIES_U_HDA },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_300SERIES_CAVS },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_300SERIES_U_HDA },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_400SERIES_CAVS },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_400SERIES_LP_HDA },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_500SERIES_HDA },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_500SERIES_LP_HDA },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_600SERIES_LP_HDA },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_700SERIES_LP_HDA },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_APOLLOLAKE_HDA },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_GLK_HDA },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_JSL_HDA },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_EHL_HDA },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_ADL_N_HDA },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_MTL_HDA },
};

int
azalia_pci_match(struct device *parent, void *match, void *aux)
{
	struct pci_attach_args *pa;

	pa = aux;
	if (PCI_CLASS(pa->pa_class) == PCI_CLASS_MULTIMEDIA
	    && PCI_SUBCLASS(pa->pa_class) == PCI_SUBCLASS_MULTIMEDIA_HDAUDIO)
		return 1;
	return pci_matchbyid((struct pci_attach_args *)aux, azalia_pci_devices,
	    nitems(azalia_pci_devices));
}

void
azalia_pci_attach(struct device *parent, struct device *self, void *aux)
{
	azalia_t *sc;
	struct pci_attach_args *pa;
	pcireg_t v;
	uint8_t reg;
	pci_intr_handle_t ih;
	const char *interrupt_str;

	sc = (azalia_t*)self;
	pa = aux;

	sc->dmat = pa->pa_dmat;

	pci_set_powerstate(pa->pa_pc, pa->pa_tag, PCI_PMCSR_STATE_D0);

	v = pci_conf_read(pa->pa_pc, pa->pa_tag, ICH_PCI_HDBARL);
	v &= PCI_MAPREG_TYPE_MASK | PCI_MAPREG_MEM_TYPE_MASK;
	if (pci_mapreg_map(pa, ICH_PCI_HDBARL, v, 0,
			   &sc->iot, &sc->ioh, NULL, &sc->map_size, 0)) {
		printf(": can't map device i/o space\n");
		return;
	}

	sc->pc = pa->pa_pc;
	sc->tag = pa->pa_tag;
	sc->pciid = pa->pa_id;
	sc->subid = pci_conf_read(pa->pa_pc, pa->pa_tag, PCI_SUBSYS_ID_REG);

	azalia_configure_pci(sc);

	/* disable MSI, use INTx instead */
	if (PCI_VENDOR(sc->pciid) == PCI_VENDOR_INTEL) {
		reg = azalia_pci_read(sc->pc, sc->tag, ICH_PCI_MMC);
		reg &= ~(ICH_PCI_MMC_ME);
		azalia_pci_write(sc->pc, sc->tag, ICH_PCI_MMC, reg);
	}

	/* interrupt */
	if (pci_intr_map_msi(pa, &ih) && pci_intr_map(pa, &ih)) {
		printf(": can't map interrupt\n");
		return;
	}
	interrupt_str = pci_intr_string(pa->pa_pc, ih);
	sc->ih = pci_intr_establish(pa->pa_pc, ih, IPL_AUDIO | IPL_MPSAFE,
	    azalia_intr, sc, sc->dev.dv_xname);
	if (sc->ih == NULL) {
		printf(": can't establish interrupt");
		if (interrupt_str != NULL)
			printf(" at %s", interrupt_str);
		printf("\n");
		return;
	}
	printf(": %s\n", interrupt_str);

	if (azalia_init(sc, 0))
		goto err_exit;

	if (azalia_init_codecs(sc))
		goto err_exit;

	if (azalia_init_streams(sc))
		goto err_exit;

	audio_attach_mi(&azalia_hw_if, sc, NULL, &sc->dev);

	return;

err_exit:
	sc->detached = 1;
	azalia_pci_detach(self, 0);
}

int
azalia_pci_activate(struct device *self, int act)
{
	azalia_t *sc = (azalia_t*)self;
	int rv = 0; 

	if (sc->detached)
		return (0);

	switch (act) {
	case DVACT_QUIESCE:
		rv = config_activate_children(self, act);
		/* stop interrupts and clear status registers */
		AZ_WRITE_4(sc, INTCTL, 0);
		AZ_WRITE_2(sc, STATESTS, HDA_STATESTS_SDIWAKE);
		AZ_WRITE_1(sc, RIRBSTS, HDA_RIRBSTS_RINTFL | HDA_RIRBSTS_RIRBOIS);
		(void) AZ_READ_4(sc, INTSTS);
		break;
	case DVACT_SUSPEND:
		rv = config_activate_children(self, act);
		azalia_suspend(sc);
		break;
	case DVACT_RESUME:
		azalia_resume(sc);
		rv = config_activate_children(self, act);
		break;
	case DVACT_POWERDOWN:
		rv = config_activate_children(self, act);
		azalia_shutdown(sc);
		break;
	default:
		rv = config_activate_children(self, act);
		break;
	}
	return (rv);
}

int
azalia_pci_detach(struct device *self, int flags)
{
	azalia_t *az = (azalia_t*)self;
	uint32_t gctl;
	int i;

	DPRINTF(("%s\n", __func__));

	/*
	 * this function is called if the device could not be supported,
	 * in which case az->detached == 1.  check if this function has
	 * already cleaned up.
	 */
	if (az->detached > 1)
		return 0;

	config_detach_children(self, flags);

	/* disable unsolicited responses if soft detaching */
	if (az->detached == 1) {
		gctl = AZ_READ_4(az, GCTL);
		AZ_WRITE_4(az, GCTL, gctl &~(HDA_GCTL_UNSOL));
	}

	timeout_del(&az->unsol_to);

	DPRINTF(("%s: delete streams\n", __func__));
	if (az->rstream.bdlist.addr != NULL)
		azalia_free_dmamem(az, &az->rstream.bdlist);
	if (az->pstream.bdlist.addr != NULL)
		azalia_free_dmamem(az, &az->pstream.bdlist);

	DPRINTF(("%s: delete codecs\n", __func__));
	for (i = 0; i < az->ncodecs; i++) {
		azalia_codec_delete(&az->codecs[i]);
	}
	az->ncodecs = 0;
	if (az->codecs != NULL) {
		free(az->codecs, M_DEVBUF, 0);
		az->codecs = NULL;
	}

	DPRINTF(("%s: delete CORB and RIRB\n", __func__));
	if (az->corb_dma.addr != NULL)
		azalia_free_dmamem(az, &az->corb_dma);
	if (az->rirb_dma.addr != NULL)
		azalia_free_dmamem(az, &az->rirb_dma);
	if (az->unsolq != NULL) {
		free(az->unsolq, M_DEVBUF, 0);
		az->unsolq = NULL;
	}

	/* disable interrupts if soft detaching */
	if (az->detached == 1) {
		DPRINTF(("%s: disable interrupts\n", __func__));
		AZ_WRITE_4(az, INTCTL, 0);

		DPRINTF(("%s: clear interrupts\n", __func__));
		AZ_WRITE_2(az, STATESTS, HDA_STATESTS_SDIWAKE);
		AZ_WRITE_1(az, RIRBSTS, HDA_RIRBSTS_RINTFL | HDA_RIRBSTS_RIRBOIS);
	}

	DPRINTF(("%s: delete PCI resources\n", __func__));
	if (az->ih != NULL) {
		pci_intr_disestablish(az->pc, az->ih);
		az->ih = NULL;
	}
	if (az->map_size != 0) {
		bus_space_unmap(az->iot, az->ioh, az->map_size);
		az->map_size = 0;
	}

	az->detached = 2;
	return 0;
}

int
azalia_intr(void *v)
{
	azalia_t *az = v;
	uint32_t intsts;
	int ret = 0;

	mtx_enter(&audio_lock);
	for (;;) {
		intsts = AZ_READ_4(az, INTSTS);
		if ((intsts & az->intctl) == 0 || intsts == 0xffffffff)
			break;

		if (intsts & az->pstream.intr_bit) {
			azalia_stream_intr(&az->pstream);
			ret = 1;
		}

		if (intsts & az->rstream.intr_bit) {
			azalia_stream_intr(&az->rstream);
			ret = 1;
		}

		if ((intsts & HDA_INTSTS_CIS) &&
		    (AZ_READ_1(az, RIRBCTL) & HDA_RIRBCTL_RINTCTL) &&
		    (AZ_READ_1(az, RIRBSTS) & HDA_RIRBSTS_RINTFL)) {
			azalia_rirb_intr(az);
			ret = 1;
		}
	}
	mtx_leave(&audio_lock);
	return (ret);
}

void
azalia_shutdown(void *v)
{
	azalia_t *az = (azalia_t *)v;
	uint32_t gctl;

	if (az->detached)
		return;

	/* disable unsolicited response */
	gctl = AZ_READ_4(az, GCTL);
	AZ_WRITE_4(az, GCTL, gctl & ~(HDA_GCTL_UNSOL));

	timeout_del(&az->unsol_to);

	/* halt CORB/RIRB */
	azalia_halt_corb(az);
	azalia_halt_rirb(az);
}

/* ================================================================
 * HDA controller functions
 * ================================================================ */

void
azalia_print_codec(codec_t *codec)
{
	const char *vendor;

	if (codec->name == NULL) {
		vendor = pci_findvendor(codec->vid >> 16);
		if (vendor == NULL)
			printf("0x%04x/0x%04x",
			    codec->vid >> 16, codec->vid & 0xffff);
		else
			printf("%s/0x%04x", vendor, codec->vid & 0xffff);
	} else
		printf("%s", codec->name);
}

int
azalia_reset(azalia_t *az)
{
	uint32_t gctl;
	int i;

	/* 4.2.2 Starting the High Definition Audio Controller */
	DPRINTF(("%s: resetting\n", __func__));
	gctl = AZ_READ_4(az, GCTL);
	AZ_WRITE_4(az, GCTL, gctl & ~HDA_GCTL_CRST);
	for (i = 5000; i > 0; i--) {
		DELAY(10);
		if ((AZ_READ_4(az, GCTL) & HDA_GCTL_CRST) == 0)
			break;
	}
	DPRINTF(("%s: reset counter = %d\n", __func__, i));
	if (i == 0) {
		DPRINTF(("%s: reset failure\n", XNAME(az)));
		return(ETIMEDOUT);
	}
	DELAY(1000);
	gctl = AZ_READ_4(az, GCTL);
	AZ_WRITE_4(az, GCTL, gctl | HDA_GCTL_CRST);
	for (i = 5000; i > 0; i--) {
		DELAY(10);
		if (AZ_READ_4(az, GCTL) & HDA_GCTL_CRST)
			break;
	}
	DPRINTF(("%s: reset counter = %d\n", __func__, i));
	if (i == 0) {
		DPRINTF(("%s: reset-exit failure\n", XNAME(az)));
		return(ETIMEDOUT);
	}
	DELAY(1000);

	return(0);
}

int
azalia_get_ctrlr_caps(azalia_t *az)
{
	int i, n;
	uint16_t gcap;
	uint16_t statests;
	uint8_t cap;

	DPRINTF(("%s: host: High Definition Audio rev. %d.%d\n",
	    XNAME(az), AZ_READ_1(az, VMAJ), AZ_READ_1(az, VMIN)));
	gcap = AZ_READ_2(az, GCAP);
	az->nistreams = HDA_GCAP_ISS(gcap);
	az->nostreams = HDA_GCAP_OSS(gcap);
	az->nbstreams = HDA_GCAP_BSS(gcap);
	az->ok64 = (gcap & HDA_GCAP_64OK) != 0;
	DPRINTF(("%s: host: %d output, %d input, and %d bidi streams\n",
	    XNAME(az), az->nostreams, az->nistreams, az->nbstreams));

	/* 4.3 Codec discovery */
	statests = AZ_READ_2(az, STATESTS);
	for (i = 0, n = 0; i < HDA_MAX_CODECS; i++) {
		if ((statests >> i) & 1) {
			DPRINTF(("%s: found a codec at #%d\n", XNAME(az), i));
			n++;
		}
	}
	az->ncodecs = n;
	if (az->ncodecs < 1) {
		printf("%s: no HD-Audio codecs\n", XNAME(az));
		return -1;
	}
	az->codecs = mallocarray(az->ncodecs, sizeof(codec_t), M_DEVBUF,
	    M_NOWAIT | M_ZERO);
	if (az->codecs == NULL) {
		printf("%s: can't allocate memory for codecs\n", XNAME(az));
		return ENOMEM;
	}
	for (i = 0, n = 0; n < az->ncodecs; i++) {
		if ((statests >> i) & 1) {
			az->codecs[n].address = i;
			az->codecs[n++].az = az;
		}
	}

	/* determine CORB size */
	az->corbsize = AZ_READ_1(az, CORBSIZE);
	cap = az->corbsize & HDA_CORBSIZE_CORBSZCAP_MASK;
	az->corbsize  &= ~HDA_CORBSIZE_CORBSIZE_MASK;
	if (cap & HDA_CORBSIZE_CORBSZCAP_256) {
		az->corb_entries = 256;
		az->corbsize |= HDA_CORBSIZE_CORBSIZE_256;
	} else if (cap & HDA_CORBSIZE_CORBSZCAP_16) {
		az->corb_entries = 16;
		az->corbsize |= HDA_CORBSIZE_CORBSIZE_16;
	} else if (cap & HDA_CORBSIZE_CORBSZCAP_2) {
		az->corb_entries = 2;
		az->corbsize |= HDA_CORBSIZE_CORBSIZE_2;
	} else {
		printf("%s: invalid CORBSZCAP: 0x%2x\n", XNAME(az), cap);
		return(-1);
	}

	/* determine RIRB size */
	az->rirbsize = AZ_READ_1(az, RIRBSIZE);
	cap = az->rirbsize & HDA_RIRBSIZE_RIRBSZCAP_MASK;
	az->rirbsize &= ~HDA_RIRBSIZE_RIRBSIZE_MASK;
	if (cap & HDA_RIRBSIZE_RIRBSZCAP_256) {
		az->rirb_entries = 256;
		az->rirbsize |= HDA_RIRBSIZE_RIRBSIZE_256;
	} else if (cap & HDA_RIRBSIZE_RIRBSZCAP_16) {
		az->rirb_entries = 16;
		az->rirbsize |= HDA_RIRBSIZE_RIRBSIZE_16;
	} else if (cap & HDA_RIRBSIZE_RIRBSZCAP_2) {
		az->rirb_entries = 2;
		az->rirbsize |= HDA_RIRBSIZE_RIRBSIZE_2;
	} else {
		printf("%s: invalid RIRBSZCAP: 0x%2x\n", XNAME(az), cap);
		return(-1);
	}

	return(0);
}

int
azalia_init(azalia_t *az, int resuming)
{
	int err;

	err = azalia_reset(az);
	if (err)
		return(err);

	if (!resuming) {
		err = azalia_get_ctrlr_caps(az);
		if (err)
			return(err);
	}

	/* clear interrupt status */
	AZ_WRITE_2(az, STATESTS, HDA_STATESTS_SDIWAKE);
	AZ_WRITE_1(az, RIRBSTS, HDA_RIRBSTS_RINTFL | HDA_RIRBSTS_RIRBOIS);
	AZ_WRITE_4(az, DPLBASE, 0);
	AZ_WRITE_4(az, DPUBASE, 0);

	/* 4.4.1 Command Outbound Ring Buffer */
	err = azalia_init_corb(az, resuming);
	if (err)
		return(err);

	/* 4.4.2 Response Inbound Ring Buffer */
	err = azalia_init_rirb(az, resuming);
	if (err)
		return(err);

	az->intctl = HDA_INTCTL_CIE | HDA_INTCTL_GIE;
	AZ_WRITE_4(az, INTCTL, az->intctl);

	return(0);
}

int
azalia_init_codecs(azalia_t *az)
{
	codec_t *codec;
	int c, i;

	c = 0;
	for (i = 0; i < az->ncodecs; i++) {
		if (!azalia_codec_init(&az->codecs[i]))
			c++;
	}
	if (c == 0) {
		printf("%s: No codecs found\n", XNAME(az));
		return(1);
	}

	/* Use the first codec capable of analog I/O.  If there are none,
	 * use the first codec capable of digital I/O.  Skip HDMI codecs.
	 */
	c = -1;
	for (i = 0; i < az->ncodecs; i++) {
		codec = &az->codecs[i];
		if ((codec->audiofunc < 0) ||
		    (codec->codec_type == AZ_CODEC_TYPE_HDMI))
			continue;
		if (codec->codec_type == AZ_CODEC_TYPE_DIGITAL) {
			if (c < 0)
				c = i;
		} else {
			c = i;
			break;
		}
	}
	az->codecno = c;
	if (az->codecno < 0) {
		printf("%s: no supported codecs\n", XNAME(az));
		return(1);
	}

	printf("%s: codecs: ", XNAME(az));
	for (i = 0; i < az->ncodecs; i++) {
		azalia_print_codec(&az->codecs[i]);
		if (i < az->ncodecs - 1)
			printf(", ");
	}
	if (az->ncodecs > 1) {
		printf(", using ");
		azalia_print_codec(&az->codecs[az->codecno]);
	}
	printf("\n");

	/* All codecs with audio are enabled, but only one will be used. */
	for (i = 0; i < az->ncodecs; i++) {
		codec = &az->codecs[i];
		if (i != az->codecno) {
			if (codec->audiofunc < 0)
				continue;
			azalia_comresp(codec, codec->audiofunc,
			    CORB_SET_POWER_STATE, CORB_PS_D3, NULL);
			DELAY(100);
			azalia_codec_delete(codec);
		}
	}

	/* Enable unsolicited responses now that az->codecno is set. */
	AZ_WRITE_4(az, GCTL, AZ_READ_4(az, GCTL) | HDA_GCTL_UNSOL);

	return(0);
}

int
azalia_init_streams(azalia_t *az)
{
	int err;

	/* Use stream#1 and #2.  Don't use stream#0. */
	err = azalia_stream_init(&az->pstream, az, az->nistreams + 0,
	    1, AUMODE_PLAY);
	if (err)
		return(err);
	err = azalia_stream_init(&az->rstream, az, 0, 2, AUMODE_RECORD);
	if (err)
		return(err);

	return(0);
}

int
azalia_halt_corb(azalia_t *az)
{
	uint8_t corbctl;
	codec_t *codec;
	int i;

	corbctl = AZ_READ_1(az, CORBCTL);
	if (corbctl & HDA_CORBCTL_CORBRUN) { /* running? */
		/* power off all codecs */
		for (i = 0; i < az->ncodecs; i++) {
			codec = &az->codecs[i];
			if (codec->audiofunc < 0)
				continue;
			azalia_comresp(codec, codec->audiofunc,
			    CORB_SET_POWER_STATE, CORB_PS_D3, NULL);
		}

		AZ_WRITE_1(az, CORBCTL, corbctl & ~HDA_CORBCTL_CORBRUN);
		for (i = 5000; i > 0; i--) {
			DELAY(10);
			corbctl = AZ_READ_1(az, CORBCTL);
			if ((corbctl & HDA_CORBCTL_CORBRUN) == 0)
				break;
		}
		if (i == 0) {
			DPRINTF(("%s: CORB is running\n", XNAME(az)));
			return EBUSY;
		}
	}
	return(0);
}

int
azalia_init_corb(azalia_t *az, int resuming)
{
	int err, i;
	uint16_t corbrp, corbwp;
	uint8_t corbctl;

	err = azalia_halt_corb(az);
	if (err)
		return(err);

	if (!resuming) {
		err = azalia_alloc_dmamem(az,
		    az->corb_entries * sizeof(corb_entry_t), 128,
		    &az->corb_dma);
		if (err) {
			printf("%s: can't allocate CORB buffer\n", XNAME(az));
			return(err);
		}
		DPRINTF(("%s: CORB allocation succeeded.\n", __func__));
	}
	timeout_set(&az->unsol_to, azalia_rirb_kick_unsol_events, az);

	AZ_WRITE_4(az, CORBLBASE, (uint32_t)AZALIA_DMA_DMAADDR(&az->corb_dma));
	AZ_WRITE_4(az, CORBUBASE, PTR_UPPER32(AZALIA_DMA_DMAADDR(&az->corb_dma)));
	AZ_WRITE_1(az, CORBSIZE, az->corbsize);
 
	/* reset CORBRP */
	corbrp = AZ_READ_2(az, CORBRP);
	AZ_WRITE_2(az, CORBRP, corbrp | HDA_CORBRP_CORBRPRST);
	AZ_WRITE_2(az, CORBRP, corbrp & ~HDA_CORBRP_CORBRPRST);
	for (i = 5000; i > 0; i--) {
		DELAY(10);
		corbrp = AZ_READ_2(az, CORBRP);
		if ((corbrp & HDA_CORBRP_CORBRPRST) == 0)
			break;
	}
	if (i == 0) {
		DPRINTF(("%s: CORBRP reset failure\n", XNAME(az)));
		return -1;
	}
	DPRINTF(("%s: CORBWP=%d; size=%d\n", __func__,
		 AZ_READ_2(az, CORBRP) & HDA_CORBRP_CORBRP, az->corb_entries));

	/* clear CORBWP */
	corbwp = AZ_READ_2(az, CORBWP);
	AZ_WRITE_2(az, CORBWP, corbwp & ~HDA_CORBWP_CORBWP);

	/* Run! */
	corbctl = AZ_READ_1(az, CORBCTL);
	AZ_WRITE_1(az, CORBCTL, corbctl | HDA_CORBCTL_CORBRUN);
	return 0;
}

int
azalia_halt_rirb(azalia_t *az)
{
	int i;
	uint8_t rirbctl;

	rirbctl = AZ_READ_1(az, RIRBCTL);
	if (rirbctl & HDA_RIRBCTL_RIRBDMAEN) { /* running? */
		AZ_WRITE_1(az, RIRBCTL, rirbctl & ~HDA_RIRBCTL_RIRBDMAEN);
		for (i = 5000; i > 0; i--) {
			DELAY(10);
			rirbctl = AZ_READ_1(az, RIRBCTL);
			if ((rirbctl & HDA_RIRBCTL_RIRBDMAEN) == 0)
				break;
		}
		if (i == 0) {
			DPRINTF(("%s: RIRB is running\n", XNAME(az)));
			return(EBUSY);
		}
	}
	return(0);
}

int
azalia_init_rirb(azalia_t *az, int resuming)
{
	int err, i;
	uint16_t rirbwp;
	uint8_t rirbctl;

	err = azalia_halt_rirb(az);
	if (err)
		return(err);

	if (!resuming) {
		err = azalia_alloc_dmamem(az,
		    az->rirb_entries * sizeof(rirb_entry_t), 128,
		    &az->rirb_dma);
		if (err) {
			printf("%s: can't allocate RIRB buffer\n", XNAME(az));
			return err;
		}
		DPRINTF(("%s: RIRB allocation succeeded.\n", __func__));

		/* setup the unsolicited response queue */
		az->unsolq = malloc(sizeof(rirb_entry_t) * UNSOLQ_SIZE,
		    M_DEVBUF, M_NOWAIT | M_ZERO);
		if (az->unsolq == NULL) {
			DPRINTF(("%s: can't allocate unsolicited response queue.\n",
			    XNAME(az)));
			azalia_free_dmamem(az, &az->rirb_dma);
			return ENOMEM;
		}
	}
	AZ_WRITE_4(az, RIRBLBASE, (uint32_t)AZALIA_DMA_DMAADDR(&az->rirb_dma));
	AZ_WRITE_4(az, RIRBUBASE, PTR_UPPER32(AZALIA_DMA_DMAADDR(&az->rirb_dma)));
	AZ_WRITE_1(az, RIRBSIZE, az->rirbsize);

	/* reset the write pointer */
	rirbwp = AZ_READ_2(az, RIRBWP);
	AZ_WRITE_2(az, RIRBWP, rirbwp | HDA_RIRBWP_RIRBWPRST);

	/* clear the read pointer */
	az->rirb_rp = AZ_READ_2(az, RIRBWP) & HDA_RIRBWP_RIRBWP;
	DPRINTF(("%s: RIRBRP=%d, size=%d\n", __func__, az->rirb_rp,
	    az->rirb_entries));

	az->unsolq_rp = 0;
	az->unsolq_wp = 0;
	az->unsolq_kick = 0;

	AZ_WRITE_2(az, RINTCNT, 1);

	/* Run! */
	rirbctl = AZ_READ_1(az, RIRBCTL);
	AZ_WRITE_1(az, RIRBCTL, rirbctl |
	    HDA_RIRBCTL_RIRBDMAEN | HDA_RIRBCTL_RINTCTL);
	for (i = 5000; i > 0; i--) {
		DELAY(10);
		rirbctl = AZ_READ_1(az, RIRBCTL);
		if (rirbctl & HDA_RIRBCTL_RIRBDMAEN)
			break;
	}
	if (i == 0) {
		DPRINTF(("%s: RIRB is not running\n", XNAME(az)));
		return(EBUSY);
	}

	return (0);
}

int
azalia_comresp(const codec_t *codec, nid_t nid, uint32_t control,
    uint32_t param, uint32_t* result)
{
	int err;

	mtx_enter(&audio_lock);
	err = azalia_set_command(codec->az, codec->address, nid, control,
	    param);
	if (err)
		goto exit;
	err = azalia_get_response(codec->az, result);
exit:
	mtx_leave(&audio_lock);
	return(err);
}

int
azalia_set_command(azalia_t *az, int caddr, nid_t nid, uint32_t control,
    uint32_t param)
{
	corb_entry_t *corb;
	int  wp;
	uint32_t verb;
	uint16_t corbwp;

	if ((AZ_READ_1(az, CORBCTL) & HDA_CORBCTL_CORBRUN) == 0) {
		printf("%s: CORB is not running.\n", XNAME(az));
		return(-1);
	}
	verb = (caddr << 28) | (nid << 20) | (control << 8) | param;
	corbwp = AZ_READ_2(az, CORBWP);
	wp = corbwp & HDA_CORBWP_CORBWP;
	corb = (corb_entry_t*)az->corb_dma.addr;
	if (++wp >= az->corb_entries)
		wp = 0;
	corb[wp] = verb;

	AZ_WRITE_2(az, CORBWP, (corbwp & ~HDA_CORBWP_CORBWP) | wp);

	return(0);
}

int
azalia_get_response(azalia_t *az, uint32_t *result)
{
	const rirb_entry_t *rirb;
	int i;
	uint16_t wp;

	if ((AZ_READ_1(az, RIRBCTL) & HDA_RIRBCTL_RIRBDMAEN) == 0) {
		printf("%s: RIRB is not running.\n", XNAME(az));
		return(-1);
	}

	rirb = (rirb_entry_t*)az->rirb_dma.addr;
	i = 5000;
	for (;;) {
		while (i > 0) {
			wp = AZ_READ_2(az, RIRBWP) & HDA_RIRBWP_RIRBWP;
			if (az->rirb_rp != wp)
				break;
			DELAY(10);
			i--;
		}
		if (i == 0) {
			DPRINTF(("%s: RIRB time out\n", XNAME(az)));
			return(ETIMEDOUT);
		}
		if (++az->rirb_rp >= az->rirb_entries)
			az->rirb_rp = 0;
		if (rirb[az->rirb_rp].resp_ex & RIRB_RESP_UNSOL) {
			az->unsolq[az->unsolq_wp].resp = rirb[az->rirb_rp].resp;
			az->unsolq[az->unsolq_wp++].resp_ex = rirb[az->rirb_rp].resp_ex;
			az->unsolq_wp %= UNSOLQ_SIZE;
		} else
			break;
	}
	if (result != NULL)
		*result = rirb[az->rirb_rp].resp;

	return(0);
}

void
azalia_rirb_kick_unsol_events(void *v)
{
	azalia_t *az = v;
	int addr, tag;

	if (az->unsolq_kick)
		return;
	az->unsolq_kick = 1;
	while (az->unsolq_rp != az->unsolq_wp) {
		addr = RIRB_RESP_CODEC(az->unsolq[az->unsolq_rp].resp_ex);
		tag = RIRB_UNSOL_TAG(az->unsolq[az->unsolq_rp].resp);
		DPRINTF(("%s: codec address=%d tag=%d\n", __func__, addr, tag));

		az->unsolq_rp++;
		az->unsolq_rp %= UNSOLQ_SIZE;

		/* We only care about events on the using codec. */
		if (az->codecs[az->codecno].address == addr)
			azalia_unsol_event(&az->codecs[az->codecno], tag);
	}
	az->unsolq_kick = 0;
}

void
azalia_rirb_intr(azalia_t *az)
{
	const rirb_entry_t *rirb;
	uint16_t wp;
	uint8_t rirbsts;

	rirbsts = AZ_READ_1(az, RIRBSTS);

	wp = AZ_READ_2(az, RIRBWP) & HDA_RIRBWP_RIRBWP;
	rirb = (rirb_entry_t*)az->rirb_dma.addr;
	while (az->rirb_rp != wp) {
		if (++az->rirb_rp >= az->rirb_entries)
			az->rirb_rp = 0;
		if (rirb[az->rirb_rp].resp_ex & RIRB_RESP_UNSOL) {
			az->unsolq[az->unsolq_wp].resp = rirb[az->rirb_rp].resp;
			az->unsolq[az->unsolq_wp++].resp_ex = rirb[az->rirb_rp].resp_ex;
			az->unsolq_wp %= UNSOLQ_SIZE;
		} else {
			DPRINTF(("%s: dropped solicited response\n", __func__));
		}
	}
	timeout_add_msec(&az->unsol_to, 1);

	AZ_WRITE_1(az, RIRBSTS,
	    rirbsts | HDA_RIRBSTS_RIRBOIS | HDA_RIRBSTS_RINTFL);
}

int
azalia_alloc_dmamem(azalia_t *az, size_t size, size_t align, azalia_dma_t *d)
{
	int err;
	int nsegs;

	d->size = size;
	err = bus_dmamem_alloc(az->dmat, size, align, 0, d->segments, 1,
	    &nsegs, BUS_DMA_NOWAIT);
	if (err)
		return err;
	if (nsegs != 1)
		goto free;
	err = bus_dmamem_map(az->dmat, d->segments, 1, size,
	    &d->addr, BUS_DMA_NOWAIT | BUS_DMA_COHERENT);
	if (err)
		goto free;
	err = bus_dmamap_create(az->dmat, size, 1, size, 0,
	    BUS_DMA_NOWAIT, &d->map);
	if (err)
		goto unmap;
	err = bus_dmamap_load(az->dmat, d->map, d->addr, size,
	    NULL, BUS_DMA_NOWAIT);
	if (err)
		goto destroy;

	if (!az->ok64 && PTR_UPPER32(AZALIA_DMA_DMAADDR(d)) != 0) {
		azalia_free_dmamem(az, d);
		return -1;
	}
	return 0;

destroy:
	bus_dmamap_destroy(az->dmat, d->map);
unmap:
	bus_dmamem_unmap(az->dmat, d->addr, size);
free:
	bus_dmamem_free(az->dmat, d->segments, 1);
	d->addr = NULL;
	return err;
}

void
azalia_free_dmamem(const azalia_t *az, azalia_dma_t* d)
{
	if (d->addr == NULL)
		return;
	bus_dmamap_unload(az->dmat, d->map);
	bus_dmamap_destroy(az->dmat, d->map);
	bus_dmamem_unmap(az->dmat, d->addr, d->size);
	bus_dmamem_free(az->dmat, d->segments, 1);
	d->addr = NULL;
}

int
azalia_suspend(azalia_t *az)
{
	int err;

	if (az->detached)
		return 0;

	/* stop interrupts and clear status registers */
	AZ_WRITE_4(az, INTCTL, 0);
	AZ_WRITE_2(az, STATESTS, HDA_STATESTS_SDIWAKE);
	AZ_WRITE_1(az, RIRBSTS, HDA_RIRBSTS_RINTFL | HDA_RIRBSTS_RIRBOIS);
	(void) AZ_READ_4(az, INTSTS);

	/* disable unsolicited responses */
	AZ_WRITE_4(az, GCTL, AZ_READ_4(az, GCTL) & ~HDA_GCTL_UNSOL);

	timeout_del(&az->unsol_to);

	/* azalia_halt_{corb,rirb}() only fail if the {CORB,RIRB} can't
	 * be stopped and azalia_init_{corb,rirb}(), which starts the
	 * {CORB,RIRB}, first calls azalia_halt_{corb,rirb}().  If halt
	 * fails, don't try to restart.
	 */
	err = azalia_halt_corb(az);
	if (err)
		goto corb_fail;

	err = azalia_halt_rirb(az);
	if (err)
		goto rirb_fail;

	/* stop interrupts and clear status registers */
	AZ_WRITE_4(az, INTCTL, 0);
	AZ_WRITE_2(az, STATESTS, HDA_STATESTS_SDIWAKE);
	AZ_WRITE_1(az, RIRBSTS, HDA_RIRBSTS_RINTFL | HDA_RIRBSTS_RIRBOIS);

	return 0;

rirb_fail:
	azalia_init_corb(az, 1);
corb_fail:
	AZ_WRITE_4(az, GCTL, AZ_READ_4(az, GCTL) | HDA_GCTL_UNSOL);

	return err;
}

int
azalia_resume_codec(codec_t *this)
{
	widget_t *w;
	uint32_t result;
	int i, err;

	err = azalia_comresp(this, this->audiofunc, CORB_SET_POWER_STATE,
 	    CORB_PS_D0, &result);
	if (err) {
		DPRINTF(("%s: power audio func error: result=0x%8.8x\n",
		    __func__, result));
	}
	DELAY(100);

	if (this->qrks & AZ_QRK_DOLBY_ATMOS)
		azalia_codec_init_dolby_atmos(this);

	FOR_EACH_WIDGET(this, i) {
		w = &this->w[i];
		if (w->widgetcap & COP_AWCAP_POWER) {
			azalia_comresp(this, w->nid, CORB_SET_POWER_STATE,
			    CORB_PS_D0, &result);
			DELAY(100);
		}
		if (w->type == COP_AWTYPE_PIN_COMPLEX)
			azalia_widget_init_pin(w, this);
		if (this->qrks & AZ_QRK_WID_MASK)
			azalia_codec_widget_quirks(this, w->nid);
	}

	if (this->qrks & AZ_QRK_GPIO_MASK) {
		err = azalia_codec_gpio_quirks(this);
		if (err)
			return err;
	}

	return(0);
}

int
azalia_resume(azalia_t *az)
{
	int err;

	if (az->detached)
		return 0;

	azalia_configure_pci(az);

	/* is this necessary? */
	pci_conf_write(az->pc, az->tag, PCI_SUBSYS_ID_REG, az->subid);

	err = azalia_init(az, 1);
	if (err)
		return err;

	/* enable unsolicited responses on the controller */
	AZ_WRITE_4(az, GCTL, AZ_READ_4(az, GCTL) | HDA_GCTL_UNSOL);

	err = azalia_resume_codec(&az->codecs[az->codecno]);
	if (err)
		return err;

	err = azalia_codec_enable_unsol(&az->codecs[az->codecno]);
	if (err)
		return err;

	return 0;
}

/* ================================================================
 * HDA codec functions
 * ================================================================ */

int
azalia_codec_init(codec_t *this)
{
	widget_t *w;
	uint32_t rev, id, result;
	int err, addr, n, i, nspdif, nhdmi;

	addr = this->address;
	/* codec vendor/device/revision */
	err = azalia_comresp(this, CORB_NID_ROOT, CORB_GET_PARAMETER,
	    COP_REVISION_ID, &rev);
	if (err)
		return err;
	err = azalia_comresp(this, CORB_NID_ROOT, CORB_GET_PARAMETER,
	    COP_VENDOR_ID, &id);
	if (err)
		return err;
	this->vid = id;
	this->subid = this->az->subid;
	azalia_codec_init_vtbl(this);
	DPRINTF(("%s: codec[%d] vid 0x%8.8x, subid 0x%8.8x, rev. %u.%u,",
	    XNAME(this->az), addr, this->vid, this->subid,
	    COP_RID_REVISION(rev), COP_RID_STEPPING(rev)));
	DPRINTF((" HDA version %u.%u\n",
	    COP_RID_MAJ(rev), COP_RID_MIN(rev)));

	/* identify function nodes */
	err = azalia_comresp(this, CORB_NID_ROOT, CORB_GET_PARAMETER,
	    COP_SUBORDINATE_NODE_COUNT, &result);
	if (err)
		return err;
	this->nfunctions = COP_NSUBNODES(result);
	if (COP_NSUBNODES(result) <= 0) {
		DPRINTF(("%s: codec[%d]: No function groups\n",
		    XNAME(this->az), addr));
		return -1;
	}
	/* iterate function nodes and find an audio function */
	n = COP_START_NID(result);
	DPRINTF(("%s: nidstart=%d #functions=%d\n",
	    XNAME(this->az), n, this->nfunctions));
	this->audiofunc = -1;
	for (i = 0; i < this->nfunctions; i++) {
		err = azalia_comresp(this, n + i, CORB_GET_PARAMETER,
		    COP_FUNCTION_GROUP_TYPE, &result);
		if (err)
			continue;
		DPRINTF(("%s: FTYPE result = 0x%8.8x\n", __func__, result));
		if (COP_FTYPE(result) == COP_FTYPE_AUDIO) {
			this->audiofunc = n + i;
			break;	/* XXX multiple audio functions? */
		}
	}
	if (this->audiofunc < 0) {
		DPRINTF(("%s: codec[%d]: No audio function groups\n",
		    XNAME(this->az), addr));
		return -1;
	}

	/* power the audio function */
	azalia_comresp(this, this->audiofunc, CORB_SET_POWER_STATE,
	    CORB_PS_D0, &result);
	DELAY(100);

	/* check widgets in the audio function */
	err = azalia_comresp(this, this->audiofunc, CORB_GET_PARAMETER,
	    COP_SUBORDINATE_NODE_COUNT, &result);
	if (err)
		return err;
	DPRINTF(("%s: There are %d widgets in the audio function.\n",
	   __func__, COP_NSUBNODES(result)));
	this->wstart = COP_START_NID(result);
	if (this->wstart < 2) {
		printf("%s: invalid node structure\n", XNAME(this->az));
		return -1;
	}
	this->wend = this->wstart + COP_NSUBNODES(result);
	this->w = mallocarray(this->wend, sizeof(widget_t), M_DEVBUF,
	    M_NOWAIT | M_ZERO);
	if (this->w == NULL) {
		printf("%s: out of memory\n", XNAME(this->az));
		return ENOMEM;
	}

	if (this->qrks & AZ_QRK_DOLBY_ATMOS)
		azalia_codec_init_dolby_atmos(this);

	/* query the base parameters */
	azalia_comresp(this, this->audiofunc, CORB_GET_PARAMETER,
	    COP_STREAM_FORMATS, &result);
	this->w[this->audiofunc].d.audio.encodings = result;
	azalia_comresp(this, this->audiofunc, CORB_GET_PARAMETER,
	    COP_PCM, &result);
	this->w[this->audiofunc].d.audio.bits_rates = result;
	azalia_comresp(this, this->audiofunc, CORB_GET_PARAMETER,
	    COP_INPUT_AMPCAP, &result);
	this->w[this->audiofunc].inamp_cap = result;
	azalia_comresp(this, this->audiofunc, CORB_GET_PARAMETER,
	    COP_OUTPUT_AMPCAP, &result);
	this->w[this->audiofunc].outamp_cap = result;

	azalia_codec_print_audiofunc(this);

	strlcpy(this->w[CORB_NID_ROOT].name, "root",
	    sizeof(this->w[CORB_NID_ROOT].name));
	strlcpy(this->w[this->audiofunc].name, "hdaudio",
	    sizeof(this->w[this->audiofunc].name));
	this->w[this->audiofunc].enable = 1;

	FOR_EACH_WIDGET(this, i) {
		w = &this->w[i];
		err = azalia_widget_init(w, this, i);
		if (err)
			return err;
		err = azalia_widget_init_connection(w, this);
		if (err)
			return err;

		azalia_widget_print_widget(w, this);

		if (this->qrks & AZ_QRK_WID_MASK) {
			azalia_codec_widget_quirks(this, i);
		}
	}

	this->na_dacs = this->na_dacs_d = 0;
	this->na_adcs = this->na_adcs_d = 0;
	this->speaker = this->speaker2 = this->spkr_dac =
	    this->fhp = this->fhp_dac =
	    this->mic = this->mic_adc = -1;
	this->nsense_pins = 0;
	this->nout_jacks = 0;
	nspdif = nhdmi = 0;
	FOR_EACH_WIDGET(this, i) {
		w = &this->w[i];

		if (!w->enable)
			continue;

		switch (w->type) {

		case COP_AWTYPE_AUDIO_MIXER:
		case COP_AWTYPE_AUDIO_SELECTOR:
			if (!azalia_widget_check_conn(this, i, 0))
				w->enable = 0;
			break;

		case COP_AWTYPE_AUDIO_OUTPUT:
			if ((w->widgetcap & COP_AWCAP_DIGITAL) == 0) {
				if (this->na_dacs < HDA_MAX_CHANNELS)
					this->a_dacs[this->na_dacs++] = i;
			} else {
				if (this->na_dacs_d < HDA_MAX_CHANNELS)
					this->a_dacs_d[this->na_dacs_d++] = i;
			}
			break;

		case COP_AWTYPE_AUDIO_INPUT:
			if ((w->widgetcap & COP_AWCAP_DIGITAL) == 0) {
				if (this->na_adcs < HDA_MAX_CHANNELS)
					this->a_adcs[this->na_adcs++] = i;
			} else {
				if (this->na_adcs_d < HDA_MAX_CHANNELS)
					this->a_adcs_d[this->na_adcs_d++] = i;
			}
			break;

		case COP_AWTYPE_PIN_COMPLEX:
			switch (CORB_CD_PORT(w->d.pin.config)) {
			case CORB_CD_FIXED:
				switch (w->d.pin.device) {
				case CORB_CD_SPEAKER:
					if (this->speaker == -1) {
						this->speaker = i;
					} else if (w->d.pin.association <
					    this->w[this->speaker].d.pin.association ||
					    (w->d.pin.association ==
					    this->w[this->speaker].d.pin.association &&
					    w->d.pin.sequence <
					    this->w[this->speaker].d.pin.sequence)) {
						this->speaker2 = this->speaker;
						this->speaker = i;
					} else {
						this->speaker2 = i;
					}
					if (this->speaker == i)
						this->spkr_dac =
						    azalia_codec_find_defdac(this, i, 0);
					break;
				case CORB_CD_MICIN:
					this->mic = i;
					this->mic_adc =
					    azalia_codec_find_defadc(this, i, 0);
					break;
				}
				break;
			case CORB_CD_JACK:
				if (w->d.pin.device == CORB_CD_LINEOUT)
					this->nout_jacks++;
				else if (w->d.pin.device == CORB_CD_HEADPHONE &&
				    CORB_CD_LOC_GEO(w->d.pin.config) ==
				    CORB_CD_FRONT) {
					this->fhp = i;
					this->fhp_dac =
					    azalia_codec_find_defdac(this, i, 0);
				}
				if (this->nsense_pins >= HDA_MAX_SENSE_PINS ||
				    !(w->d.pin.cap & COP_PINCAP_PRESENCE))
					break;
				/* check override bit */
				err = azalia_comresp(this, i,
				    CORB_GET_CONFIGURATION_DEFAULT, 0, &result);
				if (err)
					break;
				if (!(CORB_CD_MISC(result) & CORB_CD_PRESENCEOV)) {
					this->sense_pins[this->nsense_pins++] = i;
				}
				break;
			}
			if ((w->d.pin.device == CORB_CD_DIGITALOUT) &&
			    (w->d.pin.cap & COP_PINCAP_HDMI))
				nhdmi++;
			else if (w->d.pin.device == CORB_CD_SPDIFOUT ||
			    w->d.pin.device == CORB_CD_SPDIFIN)
				nspdif++;
			break;
		}
	}
	this->codec_type = AZ_CODEC_TYPE_ANALOG;
	if ((this->na_dacs == 0) && (this->na_adcs == 0)) {
		this->codec_type = AZ_CODEC_TYPE_DIGITAL;
		if (nspdif == 0 && nhdmi > 0)
			this->codec_type = AZ_CODEC_TYPE_HDMI;
	}

	/* make sure built-in mic is connected to an adc */
	if (this->mic != -1 && this->mic_adc == -1) {
		if (azalia_codec_select_micadc(this)) {
			DPRINTF(("%s: could not select mic adc\n", __func__));
		}
	}

	err = azalia_codec_sort_pins(this);
	if (err)
		return err;

	err = azalia_codec_find_inputmixer(this);
	if (err)
		return err;

	/* If the codec can do multichannel, select different DACs for
	 * the multichannel jack group.  Also be sure to keep track of
	 * which DAC the front headphone is connected to.
	 */
	if (this->na_dacs >= 3 && this->nopins >= 3) {
		err = azalia_codec_select_dacs(this);
		if (err)
			return err;
	}

	err = azalia_codec_select_spkrdac(this);
	if (err)
		return err;

	err = azalia_init_dacgroup(this);
	if (err)
		return err;

	azalia_codec_print_groups(this);

	err = azalia_widget_label_widgets(this);
	if (err)
		return err;

	err = azalia_codec_construct_format(this, 0, 0);
	if (err)
		return err;

	err = azalia_codec_init_volgroups(this);
	if (err)
		return err;

	if (this->qrks & AZ_QRK_GPIO_MASK) {
		err = azalia_codec_gpio_quirks(this);
		if (err)
			return err;
	}

	err = azalia_mixer_init(this);
	if (err)
		return err;

	return 0;
}

int
azalia_codec_find_inputmixer(codec_t *this)
{
	widget_t *w;
	int i, j;

	this->input_mixer = -1;

	FOR_EACH_WIDGET(this, i) {
		w = &this->w[i];
		if (w->type != COP_AWTYPE_AUDIO_MIXER)
			continue;

		/* can input from a pin */
		for (j = 0; j < this->nipins; j++) {
			if (azalia_codec_fnode(this, this->ipins[j].nid,
			    w->nid, 0) != -1)
				break;
		}
		if (j == this->nipins)
			continue;

		/* can output to a pin */
		for (j = 0; j < this->nopins; j++) {
			if (azalia_codec_fnode(this, w->nid,
			    this->opins[j].nid, 0) != -1)
				break;
		}
		if (j == this->nopins)
			continue;

		/* can output to an ADC */
		for (j = 0; j < this->na_adcs; j++) {
			if (azalia_codec_fnode(this, w->nid,
			    this->a_adcs[j], 0) != -1)
				break;
		}
		if (j == this->na_adcs)
			continue;

		this->input_mixer = i;
		break;
	}
	return(0);
}

int
azalia_codec_select_micadc(codec_t *this)
{
	widget_t *w;
	int i, j, conv, err;

	for (i = 0; i < this->na_adcs; i++) {
		if (azalia_codec_fnode(this, this->mic,
		    this->a_adcs[i], 0) >= 0)
			break;
	}
	if (i >= this->na_adcs)
		return(-1);
	conv = this->a_adcs[i];

	w = &this->w[conv];
	for (j = 0; j < 10; j++) {
		for (i = 0; i < w->nconnections; i++) {
			if (!azalia_widget_enabled(this, w->connections[i]))
				continue;
			if (azalia_codec_fnode(this, this->mic,
			    w->connections[i], j + 1) >= 0) {
				break;
			}
		}
		if (i >= w->nconnections)
			return(-1);
		err = azalia_comresp(this, w->nid,
		    CORB_SET_CONNECTION_SELECT_CONTROL, i, 0);
		if (err)
			return(err);
		w->selected = i;
		if (w->connections[i] == this->mic) {
			this->mic_adc = conv;
			return(0);
		}
		w = &this->w[w->connections[i]];
	}
	return(-1);
}

int
azalia_codec_sort_pins(codec_t *this)
{
#define MAX_PINS	16
	const widget_t *w;
	struct io_pin opins[MAX_PINS], opins_d[MAX_PINS];
	struct io_pin ipins[MAX_PINS], ipins_d[MAX_PINS];
	int nopins, nopins_d, nipins, nipins_d;
	int prio, loc, add, nd, conv;
	int i, j, k;

	nopins = nopins_d = nipins = nipins_d = 0;

	FOR_EACH_WIDGET(this, i) {
		w = &this->w[i];
		if (!w->enable || w->type != COP_AWTYPE_PIN_COMPLEX)
			continue;

		loc = 0;
		if (this->na_dacs >= 3 && this->nout_jacks < 3)
			loc = CORB_CD_LOC_GEO(w->d.pin.config);

		prio = w->d.pin.association << 4 | w->d.pin.sequence;
		conv = -1;

		/* analog out */
		if ((w->d.pin.cap & COP_PINCAP_OUTPUT) && 
		    !(w->widgetcap & COP_AWCAP_DIGITAL)) {
			add = nd = 0;
			conv = azalia_codec_find_defdac(this, w->nid, 0);
			switch(w->d.pin.device) {
			/* primary - output by default */
			case CORB_CD_SPEAKER:
				if (w->nid == this->speaker ||
				    w->nid == this->speaker2)
					break;
				/* FALLTHROUGH */
			case CORB_CD_HEADPHONE:
			case CORB_CD_LINEOUT:
				add = 1;
				break;
			/* secondary - input by default */
			case CORB_CD_MICIN:
				if (w->nid == this->mic)
					break;
				/* FALLTHROUGH */
			case CORB_CD_LINEIN:
				add = nd = 1;
				break;
			}
			if (add && nopins < MAX_PINS) {
				opins[nopins].nid = w->nid;
				opins[nopins].conv = conv;
				prio |= (nd << 8) | (loc << 9);
				opins[nopins].prio = prio;
				nopins++;
			}
		}
		/* digital out */
		if ((w->d.pin.cap & COP_PINCAP_OUTPUT) && 
		    (w->widgetcap & COP_AWCAP_DIGITAL)) {
			conv = azalia_codec_find_defdac(this, w->nid, 0);
			switch(w->d.pin.device) {
			case CORB_CD_SPDIFOUT:
			case CORB_CD_DIGITALOUT:
				if (nopins_d < MAX_PINS) {
					opins_d[nopins_d].nid = w->nid;
					opins_d[nopins_d].conv = conv;
					opins_d[nopins_d].prio = prio;
					nopins_d++;
				}
				break;
			}
		}
		/* analog in */
		if ((w->d.pin.cap & COP_PINCAP_INPUT) &&
		    !(w->widgetcap & COP_AWCAP_DIGITAL)) {
			add = nd = 0;
			conv = azalia_codec_find_defadc(this, w->nid, 0);
			switch(w->d.pin.device) {
			/* primary - input by default */
			case CORB_CD_MICIN:
			case CORB_CD_LINEIN:
				add = 1;
				break;
			/* secondary - output by default */
			case CORB_CD_SPEAKER:
				if (w->nid == this->speaker ||
				    w->nid == this->speaker2)
					break;
				/* FALLTHROUGH */
			case CORB_CD_HEADPHONE:
			case CORB_CD_LINEOUT:
				add = nd = 1;
				break;
			}
			if (add && nipins < MAX_PINS) {
				ipins[nipins].nid = w->nid;
				ipins[nipins].prio = prio | (nd << 8);
				ipins[nipins].conv = conv;
				nipins++;
			}
		}
		/* digital in */
		if ((w->d.pin.cap & COP_PINCAP_INPUT) && 
		    (w->widgetcap & COP_AWCAP_DIGITAL)) {
			conv = azalia_codec_find_defadc(this, w->nid, 0);
			switch(w->d.pin.device) {
			case CORB_CD_SPDIFIN:
			case CORB_CD_DIGITALIN:
			case CORB_CD_MICIN:
				if (nipins_d < MAX_PINS) {
					ipins_d[nipins_d].nid = w->nid;
					ipins_d[nipins_d].prio = prio;
					ipins_d[nipins_d].conv = conv;
					nipins_d++;
				}
				break;
			}
		}
	}

	this->opins = mallocarray(nopins, sizeof(struct io_pin), M_DEVBUF,
	    M_NOWAIT | M_ZERO);
	if (this->opins == NULL)
		return(ENOMEM);
	this->nopins = 0;
	for (i = 0; i < nopins; i++) {
		for (j = 0; j < this->nopins; j++)
			if (this->opins[j].prio > opins[i].prio)
				break;
		for (k = this->nopins; k > j; k--)
			this->opins[k] = this->opins[k - 1];
		if (j < nopins)
			this->opins[j] = opins[i];
		this->nopins++;
		if (this->nopins == nopins)
			break;
	}

	this->opins_d = mallocarray(nopins_d, sizeof(struct io_pin), M_DEVBUF,
	    M_NOWAIT | M_ZERO);
	if (this->opins_d == NULL)
		return(ENOMEM);
	this->nopins_d = 0;
	for (i = 0; i < nopins_d; i++) {
		for (j = 0; j < this->nopins_d; j++)
			if (this->opins_d[j].prio > opins_d[i].prio)
				break;
		for (k = this->nopins_d; k > j; k--)
			this->opins_d[k] = this->opins_d[k - 1];
		if (j < nopins_d)
			this->opins_d[j] = opins_d[i];
		this->nopins_d++;
		if (this->nopins_d == nopins_d)
			break;
	}

	this->ipins = mallocarray(nipins, sizeof(struct io_pin), M_DEVBUF,
	    M_NOWAIT | M_ZERO);
	if (this->ipins == NULL)
		return(ENOMEM);
	this->nipins = 0;
	for (i = 0; i < nipins; i++) {
		for (j = 0; j < this->nipins; j++)
			if (this->ipins[j].prio > ipins[i].prio)
				break;
		for (k = this->nipins; k > j; k--)
			this->ipins[k] = this->ipins[k - 1];
		if (j < nipins)
			this->ipins[j] = ipins[i];
		this->nipins++;
		if (this->nipins == nipins)
			break;
	}

	this->ipins_d = mallocarray(nipins_d, sizeof(struct io_pin), M_DEVBUF,
	    M_NOWAIT | M_ZERO);
	if (this->ipins_d == NULL)
		return(ENOMEM);
	this->nipins_d = 0;
	for (i = 0; i < nipins_d; i++) {
		for (j = 0; j < this->nipins_d; j++)
			if (this->ipins_d[j].prio > ipins_d[i].prio)
				break;
		for (k = this->nipins_d; k > j; k--)
			this->ipins_d[k] = this->ipins_d[k - 1];
		if (j < nipins_d)
			this->ipins_d[j] = ipins_d[i];
		this->nipins_d++;
		if (this->nipins_d == nipins_d)
			break;
	}

#ifdef AZALIA_DEBUG
	printf("%s: analog out pins:", __func__);
	for (i = 0; i < this->nopins; i++)
		printf(" 0x%2.2x->0x%2.2x", this->opins[i].nid,
		    this->opins[i].conv);
	printf("\n");
	printf("%s: digital out pins:", __func__);
	for (i = 0; i < this->nopins_d; i++)
		printf(" 0x%2.2x->0x%2.2x", this->opins_d[i].nid,
		    this->opins_d[i].conv);
	printf("\n");
	printf("%s: analog in pins:", __func__);
	for (i = 0; i < this->nipins; i++)
		printf(" 0x%2.2x->0x%2.2x", this->ipins[i].nid,
		    this->ipins[i].conv);
	printf("\n");
	printf("%s: digital in pins:", __func__);
	for (i = 0; i < this->nipins_d; i++)
		printf(" 0x%2.2x->0x%2.2x", this->ipins_d[i].nid,
		    this->ipins_d[i].conv);
	printf("\n");
#endif

	return 0;
#undef MAX_PINS
}

int
azalia_codec_select_dacs(codec_t *this)
{
	widget_t *w;
	nid_t *convs;
	int nconv, conv;
	int i, j, k, err;

	convs = mallocarray(this->na_dacs, sizeof(nid_t), M_DEVBUF,
	    M_NOWAIT | M_ZERO);
	if (convs == NULL)
		return(ENOMEM);

	err = 0;
	nconv = 0;
	for (i = 0; i < this->nopins; i++) {
		w = &this->w[this->opins[i].nid];

		conv = this->opins[i].conv;
		for (j = 0; j < nconv; j++) {
			if (conv == convs[j])
				break;
		}
		if (j == nconv) {
			convs[nconv++] = conv;
			if (w->nid == this->fhp)
				this->fhp_dac = conv;
			if (nconv >= this->na_dacs) {
				break;
			}
		} else {
			/* find a different dac */
			conv = -1;
			for (j = 0; j < w->nconnections; j++) {
				if (!azalia_widget_enabled(this,
				    w->connections[j]))
					continue;
				conv = azalia_codec_find_defdac(this,
				    w->connections[j], 1);
				if (conv == -1)
					continue;
				for (k = 0; k < nconv; k++) {
					if (conv == convs[k])
						break;
				}
				if (k == nconv)
					break;
			}
			if (j < w->nconnections && conv != -1) {
				err = azalia_comresp(this, w->nid,
				    CORB_SET_CONNECTION_SELECT_CONTROL, j, 0);
				if (err)
					break;
				w->selected = j;
				this->opins[i].conv = conv;
				if (w->nid == this->fhp)
					this->fhp_dac = conv;
				convs[nconv++] = conv;
				if (nconv >= this->na_dacs)
					break;
			}
		}
	}

	free(convs, M_DEVBUF, this->na_dacs * sizeof(nid_t));
	return(err);
}

/* Connect the speaker to a DAC that no other output pin is connected
 * to by default.  If that is not possible, connect to a DAC other
 * than the one the first output pin is connected to. 
 */
int
azalia_codec_select_spkrdac(codec_t *this)
{
	widget_t *w;
	nid_t convs[HDA_MAX_CHANNELS];
	int nconv, conv;
	int i, j, err, fspkr, conn;

	nconv = fspkr = 0;
	for (i = 0; i < this->nopins; i++) {
		conv = this->opins[i].conv;
		for (j = 0; j < nconv; j++) {
			if (conv == convs[j])
				break;
		}
		if (j == nconv) {
			if (conv == this->spkr_dac)
				fspkr = 1;
			convs[nconv++] = conv;
			if (nconv == this->na_dacs)
				break;
		}
	}

	if (fspkr) {
		conn = conv = -1;
		w = &this->w[this->speaker];
		for (i = 0; i < w->nconnections; i++) {
			conv = azalia_codec_find_defdac(this,
			    w->connections[i], 1);
			for (j = 0; j < nconv; j++)
				if (conv == convs[j])
					break;
			if (j == nconv)
				break;
		}
		if (i < w->nconnections) {
			conn = i;
		} else {
			/* Couldn't get a unique DAC.  Try to get a different
			 * DAC than the first pin's DAC.
			 */
			if (this->spkr_dac == this->opins[0].conv) {
				/* If the speaker connection can't be changed,
				 * change the first pin's connection.
				 */
				if (w->nconnections == 1)
					w = &this->w[this->opins[0].nid];
				for (j = 0; j < w->nconnections; j++) {
					conv = azalia_codec_find_defdac(this,
					    w->connections[j], 1);
					if (conv != this->opins[0].conv) {
						conn = j;
						break;
					}
				}
			}
		}
		if (conn != -1 && conv != -1) {
			err = azalia_comresp(this, w->nid,
			    CORB_SET_CONNECTION_SELECT_CONTROL, conn, 0);
			if (err)
				return(err);
			w->selected = conn;
			if (w->nid == this->speaker)
				this->spkr_dac = conv;
			else
				this->opins[0].conv = conv;
		}
	}

	/* If there is a speaker2, try to connect it to spkr_dac. */
	if (this->speaker2 != -1) {
		conn = conv = -1;
		w = &this->w[this->speaker2];
		for (i = 0; i < w->nconnections; i++) {
			conv = azalia_codec_find_defdac(this,
			    w->connections[i], 1);
			if (this->qrks & AZ_QRK_ROUTE_SPKR2_DAC) {
				if (conv != this->spkr_dac) {
					conn = i;
					break;
				}
			} else if (conv == this->spkr_dac) {
				conn = i;
				break;
			}
		}
		if (conn != -1) {
			err = azalia_comresp(this, w->nid,
			    CORB_SET_CONNECTION_SELECT_CONTROL, conn, 0);
			if (err)
				return(err);
			w->selected = conn;
		}
	}

	return(0);
}

int
azalia_codec_find_defdac(codec_t *this, int index, int depth)
{
	const widget_t *w;
	int i, ret;

	w = &this->w[index];
	if (w->enable == 0)
		return -1;

	if (w->type == COP_AWTYPE_AUDIO_OUTPUT)
		return index;

	if (depth > 0 &&
	    (w->type == COP_AWTYPE_PIN_COMPLEX ||
	    w->type == COP_AWTYPE_BEEP_GENERATOR ||
	    w->type == COP_AWTYPE_AUDIO_INPUT))
		return -1;
	if (++depth >= 10)
		return -1;

	if (w->nconnections > 0) {
		/* by default, all mixer connections are active */
		if (w->type == COP_AWTYPE_AUDIO_MIXER) {
			for (i = 0; i < w->nconnections; i++) {
				index = w->connections[i];
				if (!azalia_widget_enabled(this, index))
					continue;
				ret = azalia_codec_find_defdac(this, index,
				    depth);
				if (ret >= 0)
					return ret;
			}
		/* 7.3.3.2 Connection Select Control
		 * If an attempt is made to Set an index value greater than
		 * the number of list entries (index is equal to or greater
		 * than the Connection List Length property for the widget)
		 * the behavior is not predictable.
		 */

		/* negative index values are wrong too */
		} else if (w->selected >= 0 &&
			w->selected < sizeof(w->connections)) {
				index = w->connections[w->selected];
				if (VALID_WIDGET_NID(index, this)) {
					ret = azalia_codec_find_defdac(this,
						index, depth);
					if (ret >= 0)
						return ret;
				}
		}
	}

	return -1;
}

int
azalia_codec_find_defadc_sub(codec_t *this, nid_t node, int index, int depth)
{
	const widget_t *w;
	int i, ret;

	w = &this->w[index];
	if (w->nid == node) {
		return index;
	}
	/* back at the beginning or a bad end */
	if (depth > 0 &&
	    (w->type == COP_AWTYPE_PIN_COMPLEX ||
	    w->type == COP_AWTYPE_BEEP_GENERATOR ||
	    w->type == COP_AWTYPE_AUDIO_OUTPUT ||
	    w->type == COP_AWTYPE_AUDIO_INPUT))
		return -1;
	if (++depth >= 10)
		return -1;

	if (w->nconnections > 0) {
		/* by default, all mixer connections are active */
		if (w->type == COP_AWTYPE_AUDIO_MIXER) {
			for (i = 0; i < w->nconnections; i++) {
				if (!azalia_widget_enabled(this, w->connections[i]))
					continue;
				ret = azalia_codec_find_defadc_sub(this, node,
				    w->connections[i], depth);
				if (ret >= 0)
					return ret;
			}
		/* 7.3.3.2 Connection Select Control
		 * If an attempt is made to Set an index value greater than
		 * the number of list entries (index is equal to or greater
		 * than the Connection List Length property for the widget)
		 * the behavior is not predictable.
		 */

		/* negative index values are wrong too */
		} else if (w->selected >= 0 &&
			w->selected < sizeof(w->connections)) {
				index = w->connections[w->selected];
				if (VALID_WIDGET_NID(index, this)) {
					ret = azalia_codec_find_defadc_sub(this,
						node, index, depth);
					if (ret >= 0)
						return ret;
				}
		}
	}
	return -1;
}

int
azalia_codec_find_defadc(codec_t *this, int index, int depth)
{
	int i, j, conv;

	conv = -1;
	for (i = 0; i < this->na_adcs; i++) {
		j = azalia_codec_find_defadc_sub(this, index,
		    this->a_adcs[i], 0);
		if (j >= 0) {
			conv = this->a_adcs[i];
			break;
		}
	}
	return(conv);
}

int
azalia_codec_init_volgroups(codec_t *this)
{
	const widget_t *w;
	uint32_t cap, result;
	int i, j, dac, err;

	j = 0;
	this->playvols.mask = 0;
	FOR_EACH_WIDGET(this, i) {
		w = &this->w[i];
		if (w->enable == 0)
			continue;
		if (w->mixer_class == AZ_CLASS_RECORD)
			continue;
		if (!(w->widgetcap & COP_AWCAP_OUTAMP))
			continue;
		if ((COP_AMPCAP_NUMSTEPS(w->outamp_cap) == 0) &&
		    !(w->outamp_cap & COP_AMPCAP_MUTE))
			continue;
		this->playvols.mask |= (1 << j);
		this->playvols.slaves[j++] = w->nid;
		if (j >= AZ_MAX_VOL_SLAVES)
			break;
	}
	this->playvols.nslaves = j;

	this->playvols.cur = 0;
	for (i = 0; i < this->playvols.nslaves; i++) {
		w = &this->w[this->playvols.slaves[i]];
		if (w->nid == this->input_mixer ||
		    w->parent == this->input_mixer ||
		    WIDGET_CHANNELS(w) < 2)
			continue;
		j = 0;
		/* azalia_codec_find_defdac only goes 10 connections deep.
		 * Start the connection depth at 7 so it doesn't go more
		 * than 3 connections deep.
		 */
		if (w->type == COP_AWTYPE_AUDIO_MIXER ||
		    w->type == COP_AWTYPE_AUDIO_SELECTOR)
			j = 7;
		dac = azalia_codec_find_defdac(this, w->nid, j);
		if (dac == -1)
			continue;
		if (dac != this->dacs.groups[this->dacs.cur].conv[0] &&
		    dac != this->spkr_dac && dac != this->fhp_dac)
			continue;
		cap = w->outamp_cap;
		if ((cap & COP_AMPCAP_MUTE) && COP_AMPCAP_NUMSTEPS(cap)) {
			if (w->type == COP_AWTYPE_BEEP_GENERATOR) {
				continue;
			} else if (w->type == COP_AWTYPE_PIN_COMPLEX) {
				err = azalia_comresp(this, w->nid,
				    CORB_GET_PIN_WIDGET_CONTROL, 0, &result);
				if (!err && (result & CORB_PWC_OUTPUT))
					this->playvols.cur |= (1 << i);
			} else
				this->playvols.cur |= (1 << i);
		}
	}
	if (this->playvols.cur == 0) {
		for (i = 0; i < this->playvols.nslaves; i++) {
			w = &this->w[this->playvols.slaves[i]];
			j = 0;
			if (w->type == COP_AWTYPE_AUDIO_MIXER ||
			    w->type == COP_AWTYPE_AUDIO_SELECTOR)
				j = 7;
			dac = azalia_codec_find_defdac(this, w->nid, j);
			if (dac == -1)
				continue;
			if (dac != this->dacs.groups[this->dacs.cur].conv[0] &&
			    dac != this->spkr_dac && dac != this->fhp_dac)
				continue;
			if (w->type == COP_AWTYPE_BEEP_GENERATOR)
				continue;
			if (w->type == COP_AWTYPE_PIN_COMPLEX) {
				err = azalia_comresp(this, w->nid,
				    CORB_GET_PIN_WIDGET_CONTROL, 0, &result);
				if (!err && (result & CORB_PWC_OUTPUT))
					this->playvols.cur |= (1 << i);
			} else {
				this->playvols.cur |= (1 << i);
			}
		}
	}

	this->playvols.master = this->audiofunc;
	if (this->playvols.nslaves > 0) {
		FOR_EACH_WIDGET(this, i) {
			w = &this->w[i];
			if (w->type != COP_AWTYPE_VOLUME_KNOB)
				continue;
			if (!COP_VKCAP_NUMSTEPS(w->d.volume.cap))
				continue;
			this->playvols.master = w->nid;
			break;
		}
	}

	j = 0;
	this->recvols.mask = 0;
	FOR_EACH_WIDGET(this, i) {
		w = &this->w[i];
		if (w->enable == 0)
			continue;
		if (w->type == COP_AWTYPE_AUDIO_INPUT ||
		    w->type == COP_AWTYPE_PIN_COMPLEX) {
			if (!(w->widgetcap & COP_AWCAP_INAMP))
				continue;
			if ((COP_AMPCAP_NUMSTEPS(w->inamp_cap) == 0) &&
			    !(w->inamp_cap & COP_AMPCAP_MUTE))
				continue;
		} else if (w->type == COP_AWTYPE_AUDIO_MIXER ||
		    w->type == COP_AWTYPE_AUDIO_SELECTOR) {
			if (w->mixer_class != AZ_CLASS_RECORD)
				continue;
			if (!(w->widgetcap & COP_AWCAP_OUTAMP))
				continue;
			if ((COP_AMPCAP_NUMSTEPS(w->outamp_cap) == 0) &&
			    !(w->outamp_cap & COP_AMPCAP_MUTE))
				continue;
		} else {
			continue;
		}
		this->recvols.mask |= (1 << j);
		this->recvols.slaves[j++] = w->nid;
		if (j >= AZ_MAX_VOL_SLAVES)
			break;
	}
	this->recvols.nslaves = j;

	this->recvols.cur = 0;
	for (i = 0; i < this->recvols.nslaves; i++) {
		w = &this->w[this->recvols.slaves[i]];
		cap = w->outamp_cap;
		if (w->type == COP_AWTYPE_AUDIO_INPUT ||
		    w->type != COP_AWTYPE_PIN_COMPLEX)
			cap = w->inamp_cap;
		 else
			if (w->mixer_class != AZ_CLASS_RECORD)
				continue;
		if ((cap & COP_AMPCAP_MUTE) && COP_AMPCAP_NUMSTEPS(cap)) {
			if (w->type == COP_AWTYPE_PIN_COMPLEX) {
				err = azalia_comresp(this, w->nid,
				    CORB_GET_PIN_WIDGET_CONTROL, 0, &result);
				if (!err && !(result & CORB_PWC_OUTPUT))
					this->recvols.cur |= (1 << i);
			} else
				this->recvols.cur |= (1 << i);
		}
	}
	if (this->recvols.cur == 0) {
		for (i = 0; i < this->recvols.nslaves; i++) {
			w = &this->w[this->recvols.slaves[i]];
			cap = w->outamp_cap;
			if (w->type == COP_AWTYPE_AUDIO_INPUT ||
			    w->type != COP_AWTYPE_PIN_COMPLEX)
				cap = w->inamp_cap;
			 else
				if (w->mixer_class != AZ_CLASS_RECORD)
					continue;
			if (w->type == COP_AWTYPE_PIN_COMPLEX) {
				err = azalia_comresp(this, w->nid,
				    CORB_GET_PIN_WIDGET_CONTROL, 0, &result);
				if (!err && !(result & CORB_PWC_OUTPUT))
					this->recvols.cur |= (1 << i);
			} else {
				this->recvols.cur |= (1 << i);
			}
		}
	}

	this->recvols.master = this->audiofunc;

	return 0;
}

int
azalia_codec_delete(codec_t *this)
{
	azalia_mixer_delete(this);

	if (this->formats != NULL) {
		free(this->formats, M_DEVBUF,
		    this->nformats * sizeof(struct audio_format));
		this->formats = NULL;
	}
	this->nformats = 0;

	if (this->opins != NULL) {
		free(this->opins, M_DEVBUF,
		    this->nopins * sizeof(struct io_pin));
		this->opins = NULL;
	}
	this->nopins = 0;

	if (this->opins_d != NULL) {
		free(this->opins_d, M_DEVBUF,
		    this->nopins_d * sizeof(struct io_pin));
		this->opins_d = NULL;
	}
	this->nopins_d = 0;

	if (this->ipins != NULL) {
		free(this->ipins, M_DEVBUF,
		    this->nipins * sizeof(struct io_pin));
		this->ipins = NULL;
	}
	this->nipins = 0;

	if (this->ipins_d != NULL) {
		free(this->ipins_d, M_DEVBUF,
		    this->nipins_d * sizeof(struct io_pin));
		this->ipins_d = NULL;
	}
	this->nipins_d = 0;

	if (this->w != NULL) {
		free(this->w, M_DEVBUF,
		    this->wend * sizeof(widget_t));
		this->w = NULL;
	}

	return 0;
}

int
azalia_codec_construct_format(codec_t *this, int newdac, int newadc)
{
	const convgroup_t *group;
	uint32_t bits_rates;
	int variation;
	int nbits, c, chan, i;
	nid_t nid;

	variation = 0;

	if (this->dacs.ngroups > 0 && newdac < this->dacs.ngroups &&
	    newdac >= 0) {
		this->dacs.cur = newdac;
		group = &this->dacs.groups[this->dacs.cur];
		bits_rates = this->w[group->conv[0]].d.audio.bits_rates;
		nbits = 0;
		if (bits_rates & COP_PCM_B8)
			nbits++;
		if (bits_rates & COP_PCM_B16)
			nbits++;
		if (bits_rates & COP_PCM_B20)
			nbits++;
		if (bits_rates & COP_PCM_B24)
			nbits++;
		if ((bits_rates & COP_PCM_B32) &&
		    !(this->w[group->conv[0]].widgetcap & COP_AWCAP_DIGITAL))
			nbits++;
		if (nbits == 0) {
			printf("%s: invalid DAC PCM format: 0x%8.8x\n",
			    XNAME(this->az), bits_rates);
			return -1;
		}
		variation += group->nconv * nbits;
	}

	if (this->adcs.ngroups > 0 && newadc < this->adcs.ngroups &&
	    newadc >= 0) {
		this->adcs.cur = newadc;
		group = &this->adcs.groups[this->adcs.cur];
		bits_rates = this->w[group->conv[0]].d.audio.bits_rates;
		nbits = 0;
		if (bits_rates & COP_PCM_B8)
			nbits++;
		if (bits_rates & COP_PCM_B16)
			nbits++;
		if (bits_rates & COP_PCM_B20)
			nbits++;
		if (bits_rates & COP_PCM_B24)
			nbits++;
		if ((bits_rates & COP_PCM_B32) &&
		    !(this->w[group->conv[0]].widgetcap & COP_AWCAP_DIGITAL))
			nbits++;
		if (nbits == 0) {
			printf("%s: invalid ADC PCM format: 0x%8.8x\n",
			    XNAME(this->az), bits_rates);
			return -1;
		}
		variation += group->nconv * nbits;
	}

	if (variation == 0) {
		DPRINTF(("%s: no converter groups\n", XNAME(this->az)));
		return -1;
	}

	if (this->formats != NULL)
		free(this->formats, M_DEVBUF, 0);
	this->nformats = 0;
	this->formats = mallocarray(variation, sizeof(struct audio_format),
	    M_DEVBUF, M_NOWAIT | M_ZERO);
	if (this->formats == NULL) {
		printf("%s: out of memory in %s\n",
		    XNAME(this->az), __func__);
		return ENOMEM;
	}

	/* register formats for playback */
	if (this->dacs.ngroups > 0) {
		group = &this->dacs.groups[this->dacs.cur];
		for (c = 0; c < group->nconv; c++) {
			chan = 0;
			bits_rates = ~0;
			if (this->w[group->conv[0]].widgetcap &
			    COP_AWCAP_DIGITAL)
				bits_rates &= ~(COP_PCM_B32);
			for (i = 0; i <= c; i++) {
				nid = group->conv[i];
				chan += WIDGET_CHANNELS(&this->w[nid]);
				bits_rates &= this->w[nid].d.audio.bits_rates;
			}
			azalia_codec_add_bits(this, chan, bits_rates,
			    AUMODE_PLAY);
		}
	}

	/* register formats for recording */
	if (this->adcs.ngroups > 0) {
		group = &this->adcs.groups[this->adcs.cur];
		for (c = 0; c < group->nconv; c++) {
			chan = 0;
			bits_rates = ~0;
			if (this->w[group->conv[0]].widgetcap &
			    COP_AWCAP_DIGITAL)
				bits_rates &= ~(COP_PCM_B32);
			for (i = 0; i <= c; i++) {
				nid = group->conv[i];
				chan += WIDGET_CHANNELS(&this->w[nid]);
				bits_rates &= this->w[nid].d.audio.bits_rates;
			}
			azalia_codec_add_bits(this, chan, bits_rates,
			    AUMODE_RECORD);
		}
	}

	return 0;
}

void
azalia_codec_add_bits(codec_t *this, int chan, uint32_t bits_rates, int mode)
{
	if (bits_rates & COP_PCM_B8)
		azalia_codec_add_format(this, chan, 8, bits_rates, mode);
	if (bits_rates & COP_PCM_B16)
		azalia_codec_add_format(this, chan, 16, bits_rates, mode);
	if (bits_rates & COP_PCM_B20)
		azalia_codec_add_format(this, chan, 20, bits_rates, mode);
	if (bits_rates & COP_PCM_B24)
		azalia_codec_add_format(this, chan, 24, bits_rates, mode);
	if (bits_rates & COP_PCM_B32)
		azalia_codec_add_format(this, chan, 32, bits_rates, mode);
}

void
azalia_codec_add_format(codec_t *this, int chan, int prec, uint32_t rates,
    int32_t mode)
{
	struct audio_format *f;

	f = &this->formats[this->nformats++];
	f->mode = mode;
	f->encoding = AUDIO_ENCODING_SLINEAR_LE;
	if (prec == 8)
		f->encoding = AUDIO_ENCODING_ULINEAR_LE;
	f->precision = prec;
	f->channels = chan;
	f->frequency_type = 0;
	if (rates & COP_PCM_R80)
		f->frequency[f->frequency_type++] = 8000;
	if (rates & COP_PCM_R110)
		f->frequency[f->frequency_type++] = 11025;
	if (rates & COP_PCM_R160)
		f->frequency[f->frequency_type++] = 16000;
	if (rates & COP_PCM_R220)
		f->frequency[f->frequency_type++] = 22050;
	if (rates & COP_PCM_R320)
		f->frequency[f->frequency_type++] = 32000;
	if (rates & COP_PCM_R441)
		f->frequency[f->frequency_type++] = 44100;
	if (rates & COP_PCM_R480)
		f->frequency[f->frequency_type++] = 48000;
	if (rates & COP_PCM_R882)
		f->frequency[f->frequency_type++] = 88200;
	if (rates & COP_PCM_R960)
		f->frequency[f->frequency_type++] = 96000;
	if (rates & COP_PCM_R1764)
		f->frequency[f->frequency_type++] = 176400;
	if (rates & COP_PCM_R1920)
		f->frequency[f->frequency_type++] = 192000;
	if (rates & COP_PCM_R3840)
		f->frequency[f->frequency_type++] = 384000;
}

int
azalia_codec_connect_stream(stream_t *this)
{
	const codec_t *codec = &this->az->codecs[this->az->codecno];
	const convgroup_t *group;
	widget_t *w;
	uint32_t digital, stream_chan;
	int i, err, curchan, nchan, widchan;

	err = 0;
	nchan = (this->fmt & HDA_SD_FMT_CHAN) + 1;

	if (this->dir == AUMODE_RECORD)
		group = &codec->adcs.groups[codec->adcs.cur];
	else
		group = &codec->dacs.groups[codec->dacs.cur];

	curchan = 0;
	for (i = 0; i < group->nconv; i++) {
		w = &codec->w[group->conv[i]];
		widchan = WIDGET_CHANNELS(w);

		stream_chan = (this->number << 4);
		if (curchan < nchan) {
			stream_chan |= curchan;
		} else if (w->nid == codec->spkr_dac ||
		    w->nid == codec->fhp_dac) {
			stream_chan |= 0;	/* first channel(s) */
		} else
			stream_chan = 0;	/* idle stream */

		if (stream_chan == 0) {
			DPRINTFN(0, ("%s: %2.2x is idle\n", __func__, w->nid));
		} else {
			DPRINTFN(0, ("%s: %2.2x on stream chan %d\n", __func__,
			    w->nid, stream_chan & ~(this->number << 4)));
		}

		err = azalia_comresp(codec, w->nid, CORB_SET_CONVERTER_FORMAT,
		    this->fmt, NULL);
		if (err) {
			DPRINTF(("%s: nid %2.2x fmt %2.2x: %d\n",
			    __func__, w->nid, this->fmt, err));
			break;
		}
		err = azalia_comresp(codec, w->nid,
		    CORB_SET_CONVERTER_STREAM_CHANNEL, stream_chan, NULL);
		if (err) {
			DPRINTF(("%s: nid %2.2x chan %d: %d\n",
			    __func__, w->nid, stream_chan, err));
			break;
		}

		if (w->widgetcap & COP_AWCAP_DIGITAL) {
			err = azalia_comresp(codec, w->nid,
			    CORB_GET_DIGITAL_CONTROL, 0, &digital);
			if (err) {
				DPRINTF(("%s: nid %2.2x get digital: %d\n",
				    __func__, w->nid, err));
				break;
			}
			digital = (digital & 0xff) | CORB_DCC_DIGEN;
			err = azalia_comresp(codec, w->nid,
			    CORB_SET_DIGITAL_CONTROL_L, digital, NULL);
			if (err) {
				DPRINTF(("%s: nid %2.2x set digital: %d\n",
				    __func__, w->nid, err));
				break;
			}
		}
		curchan += widchan;
	}

	return err;
}

int
azalia_codec_disconnect_stream(stream_t *this)
{
	const codec_t *codec = &this->az->codecs[this->az->codecno];
	const convgroup_t *group;
	uint32_t v;
	int i;
	nid_t nid;

	if (this->dir == AUMODE_RECORD)
		group = &codec->adcs.groups[codec->adcs.cur];
	else
		group = &codec->dacs.groups[codec->dacs.cur];
	for (i = 0; i < group->nconv; i++) {
		nid = group->conv[i];
		azalia_comresp(codec, nid, CORB_SET_CONVERTER_STREAM_CHANNEL,
		    0, NULL);	/* stream#0 */
		if (codec->w[nid].widgetcap & COP_AWCAP_DIGITAL) {
			/* disable S/PDIF */
			azalia_comresp(codec, nid, CORB_GET_DIGITAL_CONTROL,
			    0, &v);
			v = (v & ~CORB_DCC_DIGEN) & 0xff;
			azalia_comresp(codec, nid, CORB_SET_DIGITAL_CONTROL_L,
			    v, NULL);
		}
	}
	return 0;
}

/* ================================================================
 * HDA widget functions
 * ================================================================ */

int
azalia_widget_init(widget_t *this, const codec_t *codec, nid_t nid)
{
	uint32_t result;
	int err;

	err = azalia_comresp(codec, nid, CORB_GET_PARAMETER,
	    COP_AUDIO_WIDGET_CAP, &result);
	if (err)
		return err;
	this->nid = nid;
	this->widgetcap = result;
	this->type = COP_AWCAP_TYPE(result);
	if (this->widgetcap & COP_AWCAP_POWER) {
		azalia_comresp(codec, nid, CORB_SET_POWER_STATE, CORB_PS_D0,
		    &result);
		DELAY(100);
	}

	this->enable = 1;
	this->mixer_class = -1;
	this->parent = codec->audiofunc;

	switch (this->type) {
	case COP_AWTYPE_AUDIO_OUTPUT:
		/* FALLTHROUGH */
	case COP_AWTYPE_AUDIO_INPUT:
		azalia_widget_init_audio(this, codec);
		break;
	case COP_AWTYPE_PIN_COMPLEX:
		azalia_widget_init_pin(this, codec);
		break;
	case COP_AWTYPE_VOLUME_KNOB:
		err = azalia_comresp(codec, this->nid, CORB_GET_PARAMETER,
		    COP_VOLUME_KNOB_CAPABILITIES, &result);
		if (err)
			return err;
		this->d.volume.cap = result;
		break;
	case COP_AWTYPE_POWER:
		/* FALLTHROUGH */
	case COP_AWTYPE_VENDOR_DEFINED:
		this->enable = 0;
		break;
	}

	/* amplifier information */
	/* XXX (ab)use bits 24-30 to store the "control offset", which is
	 * the number of steps, starting at 0, that have no effect.  these
	 * bits are reserved in HDA 1.0.
	 */
	if (this->widgetcap & COP_AWCAP_INAMP) {
		if (this->widgetcap & COP_AWCAP_AMPOV)
			azalia_comresp(codec, nid, CORB_GET_PARAMETER,
			    COP_INPUT_AMPCAP, &this->inamp_cap);
		else
			this->inamp_cap = codec->w[codec->audiofunc].inamp_cap;
		this->inamp_cap &= ~(0x7f << 24);
	}
	if (this->widgetcap & COP_AWCAP_OUTAMP) {
		if (this->widgetcap & COP_AWCAP_AMPOV)
			azalia_comresp(codec, nid, CORB_GET_PARAMETER,
			    COP_OUTPUT_AMPCAP, &this->outamp_cap);
		else
			this->outamp_cap = codec->w[codec->audiofunc].outamp_cap;
		this->outamp_cap &= ~(0x7f << 24);
	}
	return 0;
}

int
azalia_widget_sole_conn(codec_t *this, nid_t nid)
{
	int i, j, target, nconn, has_target;

	/* connected to ADC */
	for (i = 0; i < this->adcs.ngroups; i++) {
		for (j = 0; j < this->adcs.groups[i].nconv; j++) {
			target = this->adcs.groups[i].conv[j];
			if (this->w[target].nconnections == 1 &&
			    this->w[target].connections[0] == nid) {
				return target;
			}
		}
	}
	/* connected to DAC */
	for (i = 0; i < this->dacs.ngroups; i++) {
		for (j = 0; j < this->dacs.groups[i].nconv; j++) {
			target = this->dacs.groups[i].conv[j];
			if (this->w[target].nconnections == 1 &&
			    this->w[target].connections[0] == nid) {
				return target;
			}
		}
	}
	/* connected to pin complex */
	target = -1;
	FOR_EACH_WIDGET(this, i) {
		if (this->w[i].type != COP_AWTYPE_PIN_COMPLEX)
			continue;
		if (this->w[i].nconnections == 1 &&
		    this->w[i].connections[0] == nid) {
			if (target != -1)
				return -1;
			target = i;
		} else {
			nconn = 0;
			has_target = 0;
			for (j = 0; j < this->w[i].nconnections; j++) {
				if (!this->w[this->w[i].connections[j]].enable)
					continue;
				nconn++;
				if (this->w[i].connections[j] == nid)
					has_target = 1;
			}
			if (has_target == 1) {
				if (nconn == 1) {
					if (target != -1)
						return -1;
					target = i;
				} else {
					/* not sole connection at least once */
					return -1;
				}
			}
		}
	}
	if (target != -1)
		return target;

	return -1;
}

int
azalia_widget_label_widgets(codec_t *codec)
{
	widget_t *w;
	convgroup_t *group;
	int types[16];
	int pins[16];
	int colors_used, use_colors, schan;
	int i, j;

	bzero(&pins, sizeof(pins));
	bzero(&types, sizeof(types));

	/* If codec has more than one line-out jack, check if the jacks
	 * have unique colors.  If so, use the colors in the mixer names.
	 */
	use_colors = 1;
	colors_used = 0;
	if (codec->nout_jacks < 2)
		use_colors = 0;
	for (i = 0; use_colors && i < codec->nopins; i++) {
		w = &codec->w[codec->opins[i].nid];
		if (w->d.pin.device != CORB_CD_LINEOUT)
			continue;
		if (colors_used & (1 << w->d.pin.color))
			use_colors = 0;
		else
			colors_used |= (1 << w->d.pin.color);
	}

	FOR_EACH_WIDGET(codec, i) {
		w = &codec->w[i];
		/* default for disabled/unused widgets */
		snprintf(w->name, sizeof(w->name), "u-wid%2.2x", w->nid);
		if (w->enable == 0)
			continue;
		switch (w->type) {
		case COP_AWTYPE_PIN_COMPLEX:
			pins[w->d.pin.device]++;
			if (use_colors && w->d.pin.device == CORB_CD_LINEOUT) {
				snprintf(w->name, sizeof(w->name), "%s-%s",
				    pin_devices[w->d.pin.device],
				    line_colors[w->d.pin.color]);
			} else if (pins[w->d.pin.device] > 1) {
				snprintf(w->name, sizeof(w->name), "%s%d",
				    pin_devices[w->d.pin.device],
				    pins[w->d.pin.device]);
			} else {
				snprintf(w->name, sizeof(w->name), "%s",
				    pin_devices[w->d.pin.device]);
			}
			break;
		case COP_AWTYPE_AUDIO_OUTPUT:
			if (codec->dacs.ngroups < 1)
				break;
			group = &codec->dacs.groups[0];
			schan = 0;
			for (j = 0; j < group->nconv; j++) {
				if (w->nid == group->conv[j]) {
					snprintf(w->name, sizeof(w->name),
					    "%s-%d:%d", wtypes[w->type], schan,
					    schan + WIDGET_CHANNELS(w) - 1);
				}
				schan += WIDGET_CHANNELS(w);
			}
			if (codec->dacs.ngroups < 2)
				break;
			group = &codec->dacs.groups[1];
			schan = 0;
			for (j = 0; j < group->nconv; j++) {
				if (w->nid == group->conv[j]) {
					snprintf(w->name, sizeof(w->name),
					    "dig-%s-%d:%d", wtypes[w->type],
					    schan,
					    schan + WIDGET_CHANNELS(w) - 1);
				}
				schan += WIDGET_CHANNELS(w);
			}
			break;
		case COP_AWTYPE_AUDIO_INPUT:
			w->mixer_class = AZ_CLASS_RECORD;
			if (codec->adcs.ngroups < 1)
				break;
			group = &codec->adcs.groups[0];
			schan = 0;
			for (j = 0; j < group->nconv; j++) {
				if (w->nid == group->conv[j]) {
					snprintf(w->name, sizeof(w->name),
					    "%s-%d:%d", wtypes[w->type], schan,
					    schan + WIDGET_CHANNELS(w) - 1);
				}
				schan += WIDGET_CHANNELS(w);
			}
			if (codec->adcs.ngroups < 2)
				break;
			group = &codec->adcs.groups[1];
			schan = 0;
			for (j = 0; j < group->nconv; j++) {
				if (w->nid == group->conv[j]) {
					snprintf(w->name, sizeof(w->name),
					    "dig-%s-%d:%d", wtypes[w->type],
					    schan,
					    schan + WIDGET_CHANNELS(w) - 1);
				}
				schan += WIDGET_CHANNELS(w);
			}
			break;
		default:
			types[w->type]++;
			if (types[w->type] > 1)
				snprintf(w->name, sizeof(w->name), "%s%d",
				    wtypes[w->type], types[w->type]);
			else
				snprintf(w->name, sizeof(w->name), "%s",
				    wtypes[w->type]);
			break;
		}
	}

	/* Mixers and selectors that connect to only one other widget are
	 * functionally part of the widget they are connected to.  Show that
	 * relationship in the name.
	 */
	FOR_EACH_WIDGET(codec, i) {
		if (codec->w[i].type != COP_AWTYPE_AUDIO_MIXER &&
		    codec->w[i].type != COP_AWTYPE_AUDIO_SELECTOR)
			continue;
		if (codec->w[i].enable == 0)
			continue;
		j = azalia_widget_sole_conn(codec, i);
		if (j == -1) {
			/* Special case.  A selector with outamp capabilities
			 * and is connected to a single widget that has either
			 * no input or no output capabilities.  This widget
			 * serves as the input or output amp for the widget
			 * it is connected to.
			 */
			if (codec->w[i].type == COP_AWTYPE_AUDIO_SELECTOR &&
			    (codec->w[i].widgetcap & COP_AWCAP_OUTAMP) &&
			    codec->w[i].nconnections == 1) {
				j = codec->w[i].connections[0];
				if (!azalia_widget_enabled(codec, j))
					continue;
				if (!(codec->w[j].widgetcap & COP_AWCAP_INAMP))
					codec->w[i].mixer_class =
					    AZ_CLASS_INPUT;
				else if (!(codec->w[j].widgetcap & COP_AWCAP_OUTAMP))
					codec->w[i].mixer_class =
					    AZ_CLASS_OUTPUT;
				else
					continue;
			}
		}
		if (j >= 0) {
			/* As part of a disabled widget, this widget
			 * should be disabled as well.
			 */
			if (codec->w[j].enable == 0) {
				codec->w[i].enable = 0;
				snprintf(codec->w[i].name,
				    sizeof(codec->w[i].name),
				    "u-wid%2.2x", i);
				continue;
			}
			snprintf(codec->w[i].name, sizeof(codec->w[i].name),
			    "%s", codec->w[j].name);
			if (codec->w[j].mixer_class == AZ_CLASS_RECORD)
				codec->w[i].mixer_class = AZ_CLASS_RECORD;
			codec->w[i].parent = j;
		}
	}

	return 0;
}

int
azalia_widget_init_audio(widget_t *this, const codec_t *codec)
{
	uint32_t result;
	int err;

	/* check audio format */
	if (this->widgetcap & COP_AWCAP_FORMATOV) {
		err = azalia_comresp(codec, this->nid, CORB_GET_PARAMETER,
		    COP_STREAM_FORMATS, &result);
		if (err)
			return err;
		this->d.audio.encodings = result;
		if (result == 0) { /* quirk for CMI9880.
				    * This must not occur usually... */
			this->d.audio.encodings =
			    codec->w[codec->audiofunc].d.audio.encodings;
			this->d.audio.bits_rates =
			    codec->w[codec->audiofunc].d.audio.bits_rates;
		} else {
			if ((result & COP_STREAM_FORMAT_PCM) == 0) {
				printf("%s: %s: No PCM support: %x\n",
				    XNAME(codec->az), this->name, result);
				return -1;
			}
			err = azalia_comresp(codec, this->nid,
			    CORB_GET_PARAMETER, COP_PCM, &result);
			if (err)
				return err;
			this->d.audio.bits_rates = result;
		}
	} else {
		this->d.audio.encodings =
		    codec->w[codec->audiofunc].d.audio.encodings;
		this->d.audio.bits_rates =
		    codec->w[codec->audiofunc].d.audio.bits_rates;
	}
	return 0;
}

int
azalia_widget_init_pin(widget_t *this, const codec_t *codec)
{
	uint32_t result, dir;
	int err;

	err = azalia_comresp(codec, this->nid, CORB_GET_CONFIGURATION_DEFAULT,
	    0, &result);
	if (err)
		return err;
	this->d.pin.config = result;
	this->d.pin.sequence = CORB_CD_SEQUENCE(result);
	this->d.pin.association = CORB_CD_ASSOCIATION(result);
	this->d.pin.color = CORB_CD_COLOR(result);
	this->d.pin.device = CORB_CD_DEVICE(result);

	err = azalia_comresp(codec, this->nid, CORB_GET_PARAMETER,
	    COP_PINCAP, &result);
	if (err)
		return err;
	this->d.pin.cap = result;

	dir = CORB_PWC_INPUT;
	switch (this->d.pin.device) {
	case CORB_CD_LINEOUT:
	case CORB_CD_SPEAKER:
	case CORB_CD_HEADPHONE:
	case CORB_CD_SPDIFOUT:
	case CORB_CD_DIGITALOUT:
		dir = CORB_PWC_OUTPUT;
		break;
	}

	if (dir == CORB_PWC_INPUT && !(this->d.pin.cap & COP_PINCAP_INPUT))
		dir = CORB_PWC_OUTPUT;
	if (dir == CORB_PWC_OUTPUT && !(this->d.pin.cap & COP_PINCAP_OUTPUT))
		dir = CORB_PWC_INPUT;

	if (dir == CORB_PWC_INPUT && this->d.pin.device == CORB_CD_MICIN) {
		if (COP_PINCAP_VREF(this->d.pin.cap) & (1 << CORB_PWC_VREF_80))
			dir |= CORB_PWC_VREF_80;
		else if (COP_PINCAP_VREF(this->d.pin.cap) &
		    (1 << CORB_PWC_VREF_50))
			dir |= CORB_PWC_VREF_50;
	}

	if ((codec->qrks & AZ_QRK_WID_OVREF50) && (dir == CORB_PWC_OUTPUT))
		dir |= CORB_PWC_VREF_50;

	azalia_comresp(codec, this->nid, CORB_SET_PIN_WIDGET_CONTROL,
	    dir, NULL);

	if (this->d.pin.cap & COP_PINCAP_EAPD) {
		err = azalia_comresp(codec, this->nid,
		    CORB_GET_EAPD_BTL_ENABLE, 0, &result);
		if (err)
			return err;
		result &= 0xff;
		result |= CORB_EAPD_EAPD;
		err = azalia_comresp(codec, this->nid,
		    CORB_SET_EAPD_BTL_ENABLE, result, &result);
		if (err)
			return err;
	}

	/* Disable unconnected pins */
	if (CORB_CD_PORT(this->d.pin.config) == CORB_CD_NONE)
		this->enable = 0;

	return 0;
}

int
azalia_widget_init_connection(widget_t *this, const codec_t *codec)
{
	uint32_t result;
	int err;
	int i, j, k;
	int length, nconn, bits, conn, last;

	this->selected = -1;
	if ((this->widgetcap & COP_AWCAP_CONNLIST) == 0)
		return 0;

	err = azalia_comresp(codec, this->nid, CORB_GET_PARAMETER,
	    COP_CONNECTION_LIST_LENGTH, &result);
	if (err)
		return err;

	bits = 8;
	if (result & COP_CLL_LONG)
		bits = 16;

	length = COP_CLL_LENGTH(result);
	if (length == 0)
		return 0;

	/*
	 * 'length' is the number of entries, not the number of
	 * connections.  Find the number of connections, 'nconn', so
	 * enough space can be allocated for the list of connected
	 * nids.
	 */
	nconn = last = 0;
	for (i = 0; i < length;) {
		err = azalia_comresp(codec, this->nid,
		    CORB_GET_CONNECTION_LIST_ENTRY, i, &result);
		if (err)
			return err;
		for (k = 0; i < length && (k < 32 / bits); k++) {
			conn = (result >> (k * bits)) & ((1 << bits) - 1);
			/* If high bit is set, this is the end of a continuous
			 * list that started with the last connection.
			 */
			if ((nconn > 0) && (conn & (1 << (bits - 1))))
				nconn += (conn & ~(1 << (bits - 1))) - last;
			else
				nconn++;
			last = conn;
			i++;
		}
	}

	this->connections = mallocarray(nconn, sizeof(nid_t), M_DEVBUF, M_NOWAIT);
	if (this->connections == NULL) {
		printf("%s: out of memory\n", XNAME(codec->az));
		return ENOMEM;
	}
	for (i = 0; i < nconn;) {
		err = azalia_comresp(codec, this->nid,
		    CORB_GET_CONNECTION_LIST_ENTRY, i, &result);
		if (err)
			return err;
		for (k = 0; i < nconn && (k < 32 / bits); k++) {
			conn = (result >> (k * bits)) & ((1 << bits) - 1);
			/* If high bit is set, this is the end of a continuous
			 * list that started with the last connection.
			 */
			if ((i > 0) && (conn & (1 << (bits - 1)))) {
				for (j = 1; i < nconn && j <= conn - last; j++)
					this->connections[i++] = last + j;
			} else {
				this->connections[i++] = conn;
			}
			last = conn;
		}
	}
	this->nconnections = nconn;

	if (nconn > 0) {
		err = azalia_comresp(codec, this->nid,
		    CORB_GET_CONNECTION_SELECT_CONTROL, 0, &result);
		if (err)
			return err;
		this->selected = CORB_CSC_INDEX(result);
	}
	return 0;
}

int
azalia_widget_check_conn(codec_t *codec, int index, int depth)
{
	const widget_t *w;
	int i;

	w = &codec->w[index];

	if (w->type == COP_AWTYPE_BEEP_GENERATOR)
		return 0;

	if (depth > 0 &&
	    (w->type == COP_AWTYPE_PIN_COMPLEX ||
	    w->type == COP_AWTYPE_AUDIO_OUTPUT ||
	    w->type == COP_AWTYPE_AUDIO_INPUT)) {
		if (w->enable)
			return 1;
		else
			return 0;
	}
	if (++depth >= 10)
		return 0;
	for (i = 0; i < w->nconnections; i++) {
		if (!azalia_widget_enabled(codec, w->connections[i]))
			continue;
		if (azalia_widget_check_conn(codec, w->connections[i], depth))
			return 1;
	}
	return 0;
}

#ifdef AZALIA_DEBUG

#define	WIDGETCAP_BITS							\
    "\20\014LRSWAP\013POWER\012DIGITAL"					\
    "\011CONNLIST\010UNSOL\07PROC\06STRIPE\05FORMATOV\04AMPOV\03OUTAMP"	\
    "\02INAMP\01STEREO"

#define	PINCAP_BITS	"\20\021EAPD\16VREF100\15VREF80" \
    "\13VREFGND\12VREF50\11VREFHIZ\07BALANCE\06INPUT" \
    "\05OUTPUT\04HEADPHONE\03PRESENCE\02TRIGGER\01IMPEDANCE"

#define	ENCODING_BITS	"\20\3AC3\2FLOAT32\1PCM"

#define	BITSRATES_BITS	"\20\x15""32bit\x14""24bit\x13""20bit"		\
    "\x12""16bit\x11""8bit""\x0c""384kHz\x0b""192kHz\x0a""176.4kHz"	\
    "\x09""96kHz\x08""88.2kHz\x07""48kHz\x06""44.1kHz\x05""32kHz\x04"	\
    "22.05kHz\x03""16kHz\x02""11.025kHz\x01""8kHz"

static const char *pin_colors[16] = {
	"unknown", "black", "gray", "blue",
	"green", "red", "orange", "yellow",
	"purple", "pink", "col0a", "col0b",
	"col0c", "col0d", "white", "other"};
static const char *pin_conn[4] = {
	"jack", "none", "fixed", "combined"};
static const char *pin_conntype[16] = {
	"unknown", "1/8", "1/4", "atapi", "rca", "optical",
	"digital", "analog", "din", "xlr", "rj-11", "combination",
	"con0c", "con0d", "con0e", "other"};
static const char *pin_geo[15] = {
	"n/a", "rear", "front", "left",
	"right", "top", "bottom", "spec0", "spec1", "spec2",
	"loc0a", "loc0b", "loc0c", "loc0d", "loc0f"};
static const char *pin_chass[4] = {
	"external", "internal", "separate", "other"};

void
azalia_codec_print_audiofunc(const codec_t *this)
{
	uint32_t result;

	azalia_widget_print_audio(&this->w[this->audiofunc], "\t");

	result = this->w[this->audiofunc].inamp_cap;
	DPRINTF(("\tinamp: mute=%u size=%u steps=%u offset=%u\n",
	    (result & COP_AMPCAP_MUTE) != 0, COP_AMPCAP_STEPSIZE(result),
	    COP_AMPCAP_NUMSTEPS(result), COP_AMPCAP_OFFSET(result)));
	result = this->w[this->audiofunc].outamp_cap;
	DPRINTF(("\toutamp: mute=%u size=%u steps=%u offset=%u\n",
	    (result & COP_AMPCAP_MUTE) != 0, COP_AMPCAP_STEPSIZE(result),
	    COP_AMPCAP_NUMSTEPS(result), COP_AMPCAP_OFFSET(result)));
	azalia_comresp(this, this->audiofunc, CORB_GET_PARAMETER,
	    COP_GPIO_COUNT, &result);
	DPRINTF(("\tgpio: wake=%u unsol=%u gpis=%u gpos=%u gpios=%u\n",
	    (result & COP_GPIO_WAKE) != 0, (result & COP_GPIO_UNSOL) != 0,
	    COP_GPIO_GPIS(result), COP_GPIO_GPOS(result),
	    COP_GPIO_GPIOS(result)));
}

void
azalia_codec_print_groups(const codec_t *this)
{
	int i, n;

	for (i = 0; i < this->dacs.ngroups; i++) {
		printf("%s: dacgroup[%d]:", XNAME(this->az), i);
		for (n = 0; n < this->dacs.groups[i].nconv; n++) {
			printf(" %2.2x", this->dacs.groups[i].conv[n]);
		}
		printf("\n");
	}
	for (i = 0; i < this->adcs.ngroups; i++) {
		printf("%s: adcgroup[%d]:", XNAME(this->az), i);
		for (n = 0; n < this->adcs.groups[i].nconv; n++) {
			printf(" %2.2x", this->adcs.groups[i].conv[n]);
		}
		printf("\n");
	}
}

void
azalia_widget_print_audio(const widget_t *this, const char *lead)
{
	printf("%sencodings=%b\n", lead, this->d.audio.encodings,
	    ENCODING_BITS);
	printf("%sPCM formats=%b\n", lead, this->d.audio.bits_rates,
	    BITSRATES_BITS);
}

void
azalia_widget_print_widget(const widget_t *w, const codec_t *codec)
{
	int i;

	printf("%s: ", XNAME(codec->az));
	printf("%s%2.2x wcap=%b\n", w->type == COP_AWTYPE_PIN_COMPLEX ?
	    pin_colors[w->d.pin.color] : wtypes[w->type],
	    w->nid, w->widgetcap, WIDGETCAP_BITS);
	if (w->widgetcap & COP_AWCAP_FORMATOV)
		azalia_widget_print_audio(w, "\t");
	if (w->type == COP_AWTYPE_PIN_COMPLEX)
		azalia_widget_print_pin(w);

	if (w->type == COP_AWTYPE_VOLUME_KNOB)
		printf("\tdelta=%d steps=%d\n",
		    !!(w->d.volume.cap & COP_VKCAP_DELTA),
		    COP_VKCAP_NUMSTEPS(w->d.volume.cap));

	if ((w->widgetcap & COP_AWCAP_INAMP) &&
	    (w->widgetcap & COP_AWCAP_AMPOV))
		printf("\tinamp: mute=%u size=%u steps=%u offset=%u\n",
		    (w->inamp_cap & COP_AMPCAP_MUTE) != 0,
		    COP_AMPCAP_STEPSIZE(w->inamp_cap),
		    COP_AMPCAP_NUMSTEPS(w->inamp_cap),
		    COP_AMPCAP_OFFSET(w->inamp_cap));

	if ((w->widgetcap & COP_AWCAP_OUTAMP) &&
	    (w->widgetcap & COP_AWCAP_AMPOV))
		printf("\toutamp: mute=%u size=%u steps=%u offset=%u\n",
		    (w->outamp_cap & COP_AMPCAP_MUTE) != 0,
		    COP_AMPCAP_STEPSIZE(w->outamp_cap),
		    COP_AMPCAP_NUMSTEPS(w->outamp_cap),
		    COP_AMPCAP_OFFSET(w->outamp_cap));

	if (w->nconnections > 0) {
		printf("\tconnections=0x%x", w->connections[0]);
		for (i = 1; i < w->nconnections; i++)
			printf(",0x%x", w->connections[i]);
		printf("; selected=0x%x\n", w->connections[w->selected]);
	}
}

void
azalia_widget_print_pin(const widget_t *this)
{
	printf("\tcap=%b\n", this->d.pin.cap, PINCAP_BITS);
	printf("\t[%2.2d/%2.2d] ", CORB_CD_ASSOCIATION(this->d.pin.config),
	    CORB_CD_SEQUENCE(this->d.pin.config));
	printf("color=%s ", pin_colors[CORB_CD_COLOR(this->d.pin.config)]);
	printf("device=%s ", pin_devices[CORB_CD_DEVICE(this->d.pin.config)]);
	printf("conn=%s ", pin_conn[CORB_CD_PORT(this->d.pin.config)]);
	printf("conntype=%s\n", pin_conntype[CORB_CD_CONNECTION(this->d.pin.config)]);
	printf("\tlocation=%s ", pin_geo[CORB_CD_LOC_GEO(this->d.pin.config)]);
	printf("chassis=%s ", pin_chass[CORB_CD_LOC_CHASS(this->d.pin.config)]);
	printf("special=");
	if (CORB_CD_LOC_GEO(this->d.pin.config) == CORB_CD_LOC_SPEC0) {
		if (CORB_CD_LOC_CHASS(this->d.pin.config) == CORB_CD_EXTERNAL)
			printf("rear-panel");
		else if (CORB_CD_LOC_CHASS(this->d.pin.config) == CORB_CD_INTERNAL)
			printf("riser");
		else if (CORB_CD_LOC_CHASS(this->d.pin.config) == CORB_CD_LOC_OTHER)
			printf("mobile-lid-internal");
	} else if (CORB_CD_LOC_GEO(this->d.pin.config) == CORB_CD_LOC_SPEC1) {
		if (CORB_CD_LOC_CHASS(this->d.pin.config) == CORB_CD_EXTERNAL)
			printf("drive-bay");
		else if (CORB_CD_LOC_CHASS(this->d.pin.config) == CORB_CD_INTERNAL)
			printf("hdmi");
		else if (CORB_CD_LOC_CHASS(this->d.pin.config) == CORB_CD_LOC_OTHER)
			printf("mobile-lid-external");
	} else if (CORB_CD_LOC_GEO(this->d.pin.config) == CORB_CD_LOC_SPEC2) {
		if (CORB_CD_LOC_CHASS(this->d.pin.config) == CORB_CD_INTERNAL)
			printf("atapi");
	} else
		printf("none");
	printf("\n");
}

#else	/* AZALIA_DEBUG */

void
azalia_codec_print_audiofunc(const codec_t *this) {}

void
azalia_codec_print_groups(const codec_t *this) {}

void
azalia_widget_print_audio(const widget_t *this, const char *lead) {}

void
azalia_widget_print_widget(const widget_t *w, const codec_t *codec) {}

void
azalia_widget_print_pin(const widget_t *this) {}

#endif	/* AZALIA_DEBUG */

/* ================================================================
 * Stream functions
 * ================================================================ */

int
azalia_stream_init(stream_t *this, azalia_t *az, int regindex, int strnum,
    int dir)
{
	int err;

	this->az = az;
	this->regbase = HDA_SD_BASE + regindex * HDA_SD_SIZE;
	this->intr_bit = 1 << regindex;
	this->number = strnum;
	this->dir = dir;

	/* setup BDL buffers */
	err = azalia_alloc_dmamem(az, sizeof(bdlist_entry_t) * HDA_BDL_MAX,
				  128, &this->bdlist);
	if (err) {
		printf("%s: can't allocate a BDL buffer\n", XNAME(az));
		return err;
	}
	return 0;
}

int
azalia_stream_reset(stream_t *this)
{
	int i;
	uint16_t ctl;
	uint8_t sts;

	/* Make sure RUN bit is zero before resetting */
	ctl = STR_READ_2(this, CTL);
	ctl &= ~HDA_SD_CTL_RUN;
	STR_WRITE_2(this, CTL, ctl);
	DELAY(40);

	/* Start reset and wait for chip to enter. */
	ctl = STR_READ_2(this, CTL);
	STR_WRITE_2(this, CTL, ctl | HDA_SD_CTL_SRST);
	for (i = 5000; i > 0; i--) {
		DELAY(10);
		ctl = STR_READ_2(this, CTL);
		if (ctl & HDA_SD_CTL_SRST)
			break;
	}
	if (i == 0) {
		DPRINTF(("%s: stream reset failure 1\n", XNAME(this->az)));
		return -1;
	}

	/* Clear reset and wait for chip to finish */
	STR_WRITE_2(this, CTL, ctl & ~HDA_SD_CTL_SRST);
	for (i = 5000; i > 0; i--) {
		DELAY(10);
		ctl = STR_READ_2(this, CTL);
		if ((ctl & HDA_SD_CTL_SRST) == 0)
			break;
	}
	if (i == 0) {
		DPRINTF(("%s: stream reset failure 2\n", XNAME(this->az)));
		return -1;
	}

	sts = STR_READ_1(this, STS);
	sts |= HDA_SD_STS_DESE | HDA_SD_STS_FIFOE | HDA_SD_STS_BCIS;
	STR_WRITE_1(this, STS, sts);

	return (0);
}

int
azalia_stream_start(stream_t *this)
{
	bdlist_entry_t *bdlist;
	bus_addr_t dmaaddr, dmaend;
	int err, index;
	uint8_t ctl2;

	err = azalia_stream_reset(this);
	if (err) {
		DPRINTF(("%s: stream reset failed\n", "azalia"));
		return err;
	}

	STR_WRITE_4(this, BDPL, 0);
	STR_WRITE_4(this, BDPU, 0);

	/* setup BDL */
	dmaaddr = AZALIA_DMA_DMAADDR(&this->buffer);
	dmaend = dmaaddr + this->bufsize;
	bdlist = (bdlist_entry_t*)this->bdlist.addr;
	for (index = 0; index < HDA_BDL_MAX; index++) {
		bdlist[index].low = htole32(dmaaddr);
		bdlist[index].high = htole32(PTR_UPPER32(dmaaddr));
		bdlist[index].length = htole32(this->blk);
		bdlist[index].flags = htole32(BDLIST_ENTRY_IOC);
		dmaaddr += this->blk;
		if (dmaaddr >= dmaend) {
			index++;
			break;
		}
	}

	DPRINTFN(1, ("%s: size=%d fmt=0x%4.4x index=%d\n",
	    __func__, this->bufsize, this->fmt, index));

	dmaaddr = AZALIA_DMA_DMAADDR(&this->bdlist);
	STR_WRITE_4(this, BDPL, dmaaddr);
	STR_WRITE_4(this, BDPU, PTR_UPPER32(dmaaddr));
	STR_WRITE_2(this, LVI, (index - 1) & HDA_SD_LVI_LVI);
	ctl2 = STR_READ_1(this, CTL2);
	STR_WRITE_1(this, CTL2,
	    (ctl2 & ~HDA_SD_CTL2_STRM) | (this->number << HDA_SD_CTL2_STRM_SHIFT));
	STR_WRITE_4(this, CBL, this->bufsize);
	STR_WRITE_2(this, FMT, this->fmt);

	err = azalia_codec_connect_stream(this);
	if (err)
		return EINVAL;

	this->az->intctl |= this->intr_bit;
	AZ_WRITE_4(this->az, INTCTL, this->az->intctl);

	STR_WRITE_1(this, CTL, STR_READ_1(this, CTL) |
	    HDA_SD_CTL_DEIE | HDA_SD_CTL_FEIE | HDA_SD_CTL_IOCE |
	    HDA_SD_CTL_RUN);
	return (0);
}

int
azalia_stream_halt(stream_t *this)
{
	uint16_t ctl;

	ctl = STR_READ_2(this, CTL);
	ctl &= ~(HDA_SD_CTL_DEIE | HDA_SD_CTL_FEIE | HDA_SD_CTL_IOCE | HDA_SD_CTL_RUN);
	STR_WRITE_2(this, CTL, ctl);
	this->az->intctl &= ~this->intr_bit;
	AZ_WRITE_4(this->az, INTCTL, this->az->intctl);
	azalia_codec_disconnect_stream(this);

	return (0);
}

#define	HDA_SD_STS_BITS	"\20\3BCIS\4FIFOE\5DESE\6FIFORDY"

int
azalia_stream_intr(stream_t *this)
{
	unsigned int lpib, fifos, hwpos, cnt;
	u_int8_t sts;

	sts = STR_READ_1(this, STS);
	STR_WRITE_1(this, STS, sts |
	    HDA_SD_STS_DESE | HDA_SD_STS_FIFOE | HDA_SD_STS_BCIS);

	if (sts & (HDA_SD_STS_DESE | HDA_SD_STS_FIFOE))
		DPRINTF(("%s: stream %d: sts=%b\n", XNAME(this->az),
		    this->number, sts, HDA_SD_STS_BITS));

	if (sts & HDA_SD_STS_BCIS) {
		lpib = STR_READ_4(this, LPIB);
		fifos = STR_READ_2(this, FIFOS);
		if (fifos & 1)
			fifos++;
		hwpos = lpib;
		if (this->dir == AUMODE_PLAY)
			hwpos += fifos + 1;
		if (hwpos >= this->bufsize)
			hwpos -= this->bufsize;
		DPRINTFN(2, ("%s: stream %d, pos = %d -> %d, "
		    "lpib = %u, fifos = %u\n", __func__,
		    this->number, this->swpos, hwpos, lpib, fifos));
		cnt = 0;
		while (hwpos - this->swpos >= this->blk) {
			this->intr(this->intr_arg);
			this->swpos += this->blk;
			if (this->swpos == this->bufsize)
				this->swpos = 0;
			cnt++;
		}
		if (cnt != 1) {
			DPRINTF(("%s: stream %d: hwpos %u, %u intrs\n",
			    __func__, this->number, this->swpos, cnt));
		}
	}
	return (1);
}

/* ================================================================
 * MI audio entries
 * ================================================================ */

int
azalia_open(void *v, int flags)
{
	azalia_t *az;
	codec_t *codec;

	DPRINTFN(1, ("%s: flags=0x%x\n", __func__, flags));
	az = v;
	codec = &az->codecs[az->codecno];
	if ((flags & FWRITE) && codec->dacs.ngroups == 0)
		return ENODEV;
	if ((flags & FREAD) && codec->adcs.ngroups == 0)
		return ENODEV;
	codec->running++;
	return 0;
}

void
azalia_close(void *v)
{
	azalia_t *az;
	codec_t *codec;

	DPRINTFN(1, ("%s\n", __func__));
	az = v;
	codec = &az->codecs[az->codecno];
	codec->running--;
}

int
azalia_match_format(codec_t *codec, int mode, audio_params_t *par)
{
	int i;

	DPRINTFN(1, ("%s: mode=%d, want: enc=%d, prec=%d, chans=%d\n", __func__,
	    mode, par->encoding, par->precision, par->channels));

	for (i = 0; i < codec->nformats; i++) {
		if (mode != codec->formats[i].mode)
			continue;
		if (par->encoding != codec->formats[i].encoding)
			continue;
		if (par->precision != codec->formats[i].precision)
			continue;
		if (par->channels != codec->formats[i].channels)
			continue;
		break;
	}

	DPRINTFN(1, ("%s: return: enc=%d, prec=%d, chans=%d\n", __func__,
	    codec->formats[i].encoding, codec->formats[i].precision,
	    codec->formats[i].channels));

	return (i);
}

int
azalia_set_params_sub(codec_t *codec, int mode, audio_params_t *par)
{
	int i, j;
	uint ochan, oenc, opre;
#ifdef AZALIA_DEBUG
	char *cmode = (mode == AUMODE_PLAY) ? "play" : "record";
#endif

	ochan = par->channels;
	oenc = par->encoding;
	opre = par->precision;

	if ((mode == AUMODE_PLAY && codec->dacs.ngroups == 0) ||
	    (mode == AUMODE_RECORD && codec->adcs.ngroups == 0))
		return 0;

	i = azalia_match_format(codec, mode, par);
	if (i == codec->nformats && (par->precision != 16 || par->encoding !=
	    AUDIO_ENCODING_SLINEAR_LE)) {
		/* try with default encoding/precision */
		par->encoding = AUDIO_ENCODING_SLINEAR_LE;
		par->precision = 16;
		i = azalia_match_format(codec, mode, par);
	}
	if (i == codec->nformats && par->channels != 2) {
		/* try with default channels */
		par->encoding = oenc;
		par->precision = opre;
		par->channels = 2;
		i = azalia_match_format(codec, mode, par);
	}
	/* try with default everything */
	if (i == codec->nformats) {
		par->encoding = AUDIO_ENCODING_SLINEAR_LE;
		par->precision = 16;
		par->channels = 2;
		i = azalia_match_format(codec, mode, par);
		if (i == codec->nformats) {
			DPRINTF(("%s: can't find %s format %u/%u/%u\n",
			    __func__, cmode, par->encoding,
			    par->precision, par->channels));
			return EINVAL;
		}
	}
	if (codec->formats[i].frequency_type == 0) {
		DPRINTF(("%s: matched %s format %d has 0 frequencies\n",
		    __func__, cmode, i));
		return EINVAL;
	}

	for (j = 0; j < codec->formats[i].frequency_type; j++) {
		if (par->sample_rate != codec->formats[i].frequency[j])
			continue;
		break;
	}
	if (j == codec->formats[i].frequency_type) {
		/* try again with default */
		par->sample_rate = 48000;
		for (j = 0; j < codec->formats[i].frequency_type; j++) {
			if (par->sample_rate != codec->formats[i].frequency[j])
				continue;
			break;
		}
		if (j == codec->formats[i].frequency_type) {
			DPRINTF(("%s: can't find %s rate %lu\n",
			    __func__, cmode, par->sample_rate));
			return EINVAL;
		}
	}
	par->bps = AUDIO_BPS(par->precision);
	par->msb = 1;

	return (0);
}

int
azalia_set_params(void *v, int smode, int umode, audio_params_t *p,
    audio_params_t *r)
{
	azalia_t *az;
	codec_t *codec;
	int ret;

	az = v;
	codec = &az->codecs[az->codecno];
	if (codec->nformats == 0) {
		DPRINTF(("%s: codec has no formats\n", __func__));
		return EINVAL;
	}

	if (smode & AUMODE_RECORD && r != NULL) {
		ret = azalia_set_params_sub(codec, AUMODE_RECORD, r);
		if (ret)
			return (ret);
	}

	if (smode & AUMODE_PLAY && p != NULL) {
		ret = azalia_set_params_sub(codec, AUMODE_PLAY, p);
		if (ret)
			return (ret);
	}

	return (0);
}

unsigned int
azalia_set_blksz(void *v, int mode,
	struct audio_params *p, struct audio_params *r, unsigned int blksz)
{
	int mult;

	/* must be multiple of 128 bytes */
	mult = audio_blksz_bytes(mode, p, r, 128);

	blksz -= blksz % mult;
	if (blksz == 0)
		blksz = mult;

	return blksz;
}

unsigned int
azalia_set_nblks(void *v, int mode,
	struct audio_params *params, unsigned int blksz, unsigned int nblks)
{
	/* number of blocks must be <= HDA_BDL_MAX */
	if (nblks > HDA_BDL_MAX)
		nblks = HDA_BDL_MAX;

	return nblks;
}

int
azalia_halt_output(void *v)
{
	azalia_t *az;

	DPRINTFN(1, ("%s\n", __func__));
	az = v;
	return azalia_stream_halt(&az->pstream);
}

int
azalia_halt_input(void *v)
{
	azalia_t *az;

	DPRINTFN(1, ("%s\n", __func__));
	az = v;
	return azalia_stream_halt(&az->rstream);
}

int
azalia_set_port(void *v, mixer_ctrl_t *mc)
{
	azalia_t *az;
	codec_t *co;
	const mixer_item_t *m;

	az = v;
	co = &az->codecs[az->codecno];
	if (mc->dev < 0 || mc->dev >= co->nmixers)
		return EINVAL;

	m = &co->mixers[mc->dev];
	if (mc->type != m->devinfo.type)
		return EINVAL;

	return azalia_mixer_set(co, m->nid, m->target, mc);
}

int
azalia_get_port(void *v, mixer_ctrl_t *mc)
{
	azalia_t *az;
	codec_t *co;
	const mixer_item_t *m;

	az = v;
	co = &az->codecs[az->codecno];
	if (mc->dev < 0 || mc->dev >= co->nmixers)
		return EINVAL;

	m = &co->mixers[mc->dev];
	mc->type = m->devinfo.type;

	return azalia_mixer_get(co, m->nid, m->target, mc);
}

int
azalia_query_devinfo(void *v, mixer_devinfo_t *mdev)
{
	azalia_t *az;
	const codec_t *co;

	az = v;
	co = &az->codecs[az->codecno];
	if (mdev->index < 0 || mdev->index >= co->nmixers)
		return ENXIO;
	*mdev = co->mixers[mdev->index].devinfo;
	return 0;
}

void *
azalia_allocm(void *v, int dir, size_t size, int pool, int flags)
{
	azalia_t *az;
	stream_t *stream;
	int err;

	az = v;
	stream = dir == AUMODE_PLAY ? &az->pstream : &az->rstream;
	err = azalia_alloc_dmamem(az, size, 128, &stream->buffer);
	if (err) {
		printf("%s: allocm failed\n", az->dev.dv_xname);
		return NULL;
	}
	return stream->buffer.addr;
}

void
azalia_freem(void *v, void *addr, int pool)
{
	azalia_t *az;
	stream_t *stream;

	az = v;
	if (addr == az->pstream.buffer.addr) {
		stream = &az->pstream;
	} else if (addr == az->rstream.buffer.addr) {
		stream = &az->rstream;
	} else {
		return;
	}
	azalia_free_dmamem(az, &stream->buffer);
}

size_t
azalia_round_buffersize(void *v, int dir, size_t size)
{
	size &= ~0x7f;		/* must be multiple of 128 */
	if (size <= 0)
		size = 128;
	return size;
}

int
azalia_trigger_output(void *v, void *start, void *end, int blk,
    void (*intr)(void *), void *arg, audio_params_t *param)
{
	azalia_t *az;
	int err;
	uint16_t fmt;

	az = v;

	if (az->codecs[az->codecno].dacs.ngroups == 0) {
		DPRINTF(("%s: can't play without a DAC\n", __func__));
		return ENXIO;
	}

	err = azalia_params2fmt(param, &fmt);
	if (err)
		return(EINVAL);

	az->pstream.bufsize = (caddr_t)end - (caddr_t)start;
	az->pstream.blk = blk;
	az->pstream.fmt = fmt;
	az->pstream.intr = intr;
	az->pstream.intr_arg = arg;
	az->pstream.swpos = 0;

	return azalia_stream_start(&az->pstream);
}

int
azalia_trigger_input(void *v, void *start, void *end, int blk,
    void (*intr)(void *), void *arg, audio_params_t *param)
{
	azalia_t *az;
	int err;
	uint16_t fmt;

	DPRINTFN(1, ("%s: this=%p start=%p end=%p blk=%d {enc=%u %uch %ubit %luHz}\n",
	    __func__, v, start, end, blk, param->encoding, param->channels,
	    param->precision, param->sample_rate));

	az = v;

	if (az->codecs[az->codecno].adcs.ngroups == 0) {
		DPRINTF(("%s: can't record without an ADC\n", __func__));
		return ENXIO;
	}

	err = azalia_params2fmt(param, &fmt);
	if (err)
		return(EINVAL);

	az->rstream.bufsize = (caddr_t)end - (caddr_t)start;
	az->rstream.blk = blk;
	az->rstream.fmt = fmt;
	az->rstream.intr = intr;
	az->rstream.intr_arg = arg;
	az->rstream.swpos = 0;

	return azalia_stream_start(&az->rstream);
}

/* --------------------------------
 * helpers for MI audio functions
 * -------------------------------- */
int
azalia_params2fmt(const audio_params_t *param, uint16_t *fmt)
{
	uint16_t ret;

	ret = 0;
	if (param->channels > HDA_MAX_CHANNELS) {
		printf("%s: too many channels: %u\n", __func__,
		    param->channels);
		return EINVAL;
	}

	DPRINTFN(1, ("%s: prec=%d, chan=%d, rate=%ld\n", __func__,
	    param->precision, param->channels, param->sample_rate));

	/* XXX: can channels be >2 ? */
	ret |= param->channels - 1;

	switch (param->precision) {
	case 8:
		ret |= HDA_SD_FMT_BITS_8_16;
		break;
	case 16:
		ret |= HDA_SD_FMT_BITS_16_16;
		break;
	case 20:
		ret |= HDA_SD_FMT_BITS_20_32;
		break;
	case 24:
		ret |= HDA_SD_FMT_BITS_24_32;
		break;
	case 32:
		ret |= HDA_SD_FMT_BITS_32_32;
		break;
	}

	switch (param->sample_rate) {
	default:
	case 384000:
		printf("%s: invalid sample_rate: %lu\n", __func__,
		    param->sample_rate);
		return EINVAL;
	case 192000:
		ret |= HDA_SD_FMT_BASE_48 | HDA_SD_FMT_MULT_X4 | HDA_SD_FMT_DIV_BY1;
		break;
	case 176400:
		ret |= HDA_SD_FMT_BASE_44 | HDA_SD_FMT_MULT_X4 | HDA_SD_FMT_DIV_BY1;
		break;
	case 96000:
		ret |= HDA_SD_FMT_BASE_48 | HDA_SD_FMT_MULT_X2 | HDA_SD_FMT_DIV_BY1;
		break;
	case 88200:
		ret |= HDA_SD_FMT_BASE_44 | HDA_SD_FMT_MULT_X2 | HDA_SD_FMT_DIV_BY1;
		break;
	case 48000:
		ret |= HDA_SD_FMT_BASE_48 | HDA_SD_FMT_MULT_X1 | HDA_SD_FMT_DIV_BY1;
		break;
	case 44100:
		ret |= HDA_SD_FMT_BASE_44 | HDA_SD_FMT_MULT_X1 | HDA_SD_FMT_DIV_BY1;
		break;
	case 32000:
		ret |= HDA_SD_FMT_BASE_48 | HDA_SD_FMT_MULT_X2 | HDA_SD_FMT_DIV_BY3;
		break;
	case 22050:
		ret |= HDA_SD_FMT_BASE_44 | HDA_SD_FMT_MULT_X1 | HDA_SD_FMT_DIV_BY2;
		break;
	case 16000:
		ret |= HDA_SD_FMT_BASE_48 | HDA_SD_FMT_MULT_X1 | HDA_SD_FMT_DIV_BY3;
		break;
	case 11025:
		ret |= HDA_SD_FMT_BASE_44 | HDA_SD_FMT_MULT_X1 | HDA_SD_FMT_DIV_BY4;
		break;
	case 8000:
		ret |= HDA_SD_FMT_BASE_48 | HDA_SD_FMT_MULT_X1 | HDA_SD_FMT_DIV_BY6;
		break;
	}
	*fmt = ret;
	return 0;
}
