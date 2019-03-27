/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 Stephane E. Potvin <sepotvin@videotron.ca>
 * Copyright (c) 2006 Ariff Abdullah <ariff@FreeBSD.org>
 * Copyright (c) 2008-2012 Alexander Motin <mav@FreeBSD.org>
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
 */

/*
 * Intel High Definition Audio (Controller) driver for FreeBSD.
 */

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_snd.h"
#endif

#include <dev/sound/pcm/sound.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <sys/ctype.h>
#include <sys/endian.h>
#include <sys/taskqueue.h>

#include <dev/sound/pci/hda/hdac_private.h>
#include <dev/sound/pci/hda/hdac_reg.h>
#include <dev/sound/pci/hda/hda_reg.h>
#include <dev/sound/pci/hda/hdac.h>

#define HDA_DRV_TEST_REV	"20120126_0002"

SND_DECLARE_FILE("$FreeBSD$");

#define hdac_lock(sc)		snd_mtxlock((sc)->lock)
#define hdac_unlock(sc)		snd_mtxunlock((sc)->lock)
#define hdac_lockassert(sc)	snd_mtxassert((sc)->lock)
#define hdac_lockowned(sc)	mtx_owned((sc)->lock)

#define HDAC_QUIRK_64BIT	(1 << 0)
#define HDAC_QUIRK_DMAPOS	(1 << 1)
#define HDAC_QUIRK_MSI		(1 << 2)

static const struct {
	const char *key;
	uint32_t value;
} hdac_quirks_tab[] = {
	{ "64bit", HDAC_QUIRK_DMAPOS },
	{ "dmapos", HDAC_QUIRK_DMAPOS },
	{ "msi", HDAC_QUIRK_MSI },
};

MALLOC_DEFINE(M_HDAC, "hdac", "HDA Controller");

static const struct {
	uint32_t	model;
	const char	*desc;
	char		quirks_on;
	char		quirks_off;
} hdac_devices[] = {
	{ HDA_INTEL_OAK,     "Intel Oaktrail",	0, 0 },
	{ HDA_INTEL_BAY,     "Intel BayTrail",	0, 0 },
	{ HDA_INTEL_HSW1,    "Intel Haswell",	0, 0 },
	{ HDA_INTEL_HSW2,    "Intel Haswell",	0, 0 },
	{ HDA_INTEL_HSW3,    "Intel Haswell",	0, 0 },
	{ HDA_INTEL_BDW1,    "Intel Broadwell",	0, 0 },
	{ HDA_INTEL_BDW2,    "Intel Broadwell",	0, 0 },
	{ HDA_INTEL_CPT,     "Intel Cougar Point",	0, 0 },
	{ HDA_INTEL_PATSBURG,"Intel Patsburg",  0, 0 },
	{ HDA_INTEL_PPT1,    "Intel Panther Point",	0, 0 },
	{ HDA_INTEL_LPT1,    "Intel Lynx Point",	0, 0 },
	{ HDA_INTEL_LPT2,    "Intel Lynx Point",	0, 0 },
	{ HDA_INTEL_WCPT,    "Intel Wildcat Point",	0, 0 },
	{ HDA_INTEL_WELLS1,  "Intel Wellsburg",	0, 0 },
	{ HDA_INTEL_WELLS2,  "Intel Wellsburg",	0, 0 },
	{ HDA_INTEL_LPTLP1,  "Intel Lynx Point-LP",	0, 0 },
	{ HDA_INTEL_LPTLP2,  "Intel Lynx Point-LP",	0, 0 },
	{ HDA_INTEL_SRPTLP,  "Intel Sunrise Point-LP",	0, 0 },
	{ HDA_INTEL_KBLKLP,  "Intel Kaby Lake-LP",	0, 0 },
	{ HDA_INTEL_SRPT,    "Intel Sunrise Point",	0, 0 },
	{ HDA_INTEL_KBLK,    "Intel Kaby Lake",	0, 0 },
	{ HDA_INTEL_KBLKH,   "Intel Kaby Lake-H",	0, 0 },
	{ HDA_INTEL_CFLK,    "Intel Coffee Lake",	0, 0 },
	{ HDA_INTEL_82801F,  "Intel 82801F",	0, 0 },
	{ HDA_INTEL_63XXESB, "Intel 631x/632xESB",	0, 0 },
	{ HDA_INTEL_82801G,  "Intel 82801G",	0, 0 },
	{ HDA_INTEL_82801H,  "Intel 82801H",	0, 0 },
	{ HDA_INTEL_82801I,  "Intel 82801I",	0, 0 },
	{ HDA_INTEL_82801JI, "Intel 82801JI",	0, 0 },
	{ HDA_INTEL_82801JD, "Intel 82801JD",	0, 0 },
	{ HDA_INTEL_PCH,     "Intel Ibex Peak",	0, 0 },
	{ HDA_INTEL_PCH2,    "Intel Ibex Peak",	0, 0 },
	{ HDA_INTEL_SCH,     "Intel SCH",	0, 0 },
	{ HDA_NVIDIA_MCP51,  "NVIDIA MCP51",	0, HDAC_QUIRK_MSI },
	{ HDA_NVIDIA_MCP55,  "NVIDIA MCP55",	0, HDAC_QUIRK_MSI },
	{ HDA_NVIDIA_MCP61_1, "NVIDIA MCP61",	0, 0 },
	{ HDA_NVIDIA_MCP61_2, "NVIDIA MCP61",	0, 0 },
	{ HDA_NVIDIA_MCP65_1, "NVIDIA MCP65",	0, 0 },
	{ HDA_NVIDIA_MCP65_2, "NVIDIA MCP65",	0, 0 },
	{ HDA_NVIDIA_MCP67_1, "NVIDIA MCP67",	0, 0 },
	{ HDA_NVIDIA_MCP67_2, "NVIDIA MCP67",	0, 0 },
	{ HDA_NVIDIA_MCP73_1, "NVIDIA MCP73",	0, 0 },
	{ HDA_NVIDIA_MCP73_2, "NVIDIA MCP73",	0, 0 },
	{ HDA_NVIDIA_MCP78_1, "NVIDIA MCP78",	0, HDAC_QUIRK_64BIT },
	{ HDA_NVIDIA_MCP78_2, "NVIDIA MCP78",	0, HDAC_QUIRK_64BIT },
	{ HDA_NVIDIA_MCP78_3, "NVIDIA MCP78",	0, HDAC_QUIRK_64BIT },
	{ HDA_NVIDIA_MCP78_4, "NVIDIA MCP78",	0, HDAC_QUIRK_64BIT },
	{ HDA_NVIDIA_MCP79_1, "NVIDIA MCP79",	0, 0 },
	{ HDA_NVIDIA_MCP79_2, "NVIDIA MCP79",	0, 0 },
	{ HDA_NVIDIA_MCP79_3, "NVIDIA MCP79",	0, 0 },
	{ HDA_NVIDIA_MCP79_4, "NVIDIA MCP79",	0, 0 },
	{ HDA_NVIDIA_MCP89_1, "NVIDIA MCP89",	0, 0 },
	{ HDA_NVIDIA_MCP89_2, "NVIDIA MCP89",	0, 0 },
	{ HDA_NVIDIA_MCP89_3, "NVIDIA MCP89",	0, 0 },
	{ HDA_NVIDIA_MCP89_4, "NVIDIA MCP89",	0, 0 },
	{ HDA_NVIDIA_0BE2,   "NVIDIA (0x0be2)",	0, HDAC_QUIRK_MSI },
	{ HDA_NVIDIA_0BE3,   "NVIDIA (0x0be3)",	0, HDAC_QUIRK_MSI },
	{ HDA_NVIDIA_0BE4,   "NVIDIA (0x0be4)",	0, HDAC_QUIRK_MSI },
	{ HDA_NVIDIA_GT100,  "NVIDIA GT100",	0, HDAC_QUIRK_MSI },
	{ HDA_NVIDIA_GT104,  "NVIDIA GT104",	0, HDAC_QUIRK_MSI },
	{ HDA_NVIDIA_GT106,  "NVIDIA GT106",	0, HDAC_QUIRK_MSI },
	{ HDA_NVIDIA_GT108,  "NVIDIA GT108",	0, HDAC_QUIRK_MSI },
	{ HDA_NVIDIA_GT116,  "NVIDIA GT116",	0, HDAC_QUIRK_MSI },
	{ HDA_NVIDIA_GF119,  "NVIDIA GF119",	0, 0 },
	{ HDA_NVIDIA_GF110_1, "NVIDIA GF110",	0, HDAC_QUIRK_MSI },
	{ HDA_NVIDIA_GF110_2, "NVIDIA GF110",	0, HDAC_QUIRK_MSI },
	{ HDA_ATI_SB450,     "ATI SB450",	0, 0 },
	{ HDA_ATI_SB600,     "ATI SB600",	0, 0 },
	{ HDA_ATI_RS600,     "ATI RS600",	0, 0 },
	{ HDA_ATI_RS690,     "ATI RS690",	0, 0 },
	{ HDA_ATI_RS780,     "ATI RS780",	0, 0 },
	{ HDA_ATI_R600,      "ATI R600",	0, 0 },
	{ HDA_ATI_RV610,     "ATI RV610",	0, 0 },
	{ HDA_ATI_RV620,     "ATI RV620",	0, 0 },
	{ HDA_ATI_RV630,     "ATI RV630",	0, 0 },
	{ HDA_ATI_RV635,     "ATI RV635",	0, 0 },
	{ HDA_ATI_RV710,     "ATI RV710",	0, 0 },
	{ HDA_ATI_RV730,     "ATI RV730",	0, 0 },
	{ HDA_ATI_RV740,     "ATI RV740",	0, 0 },
	{ HDA_ATI_RV770,     "ATI RV770",	0, 0 },
	{ HDA_ATI_RV810,     "ATI RV810",	0, 0 },
	{ HDA_ATI_RV830,     "ATI RV830",	0, 0 },
	{ HDA_ATI_RV840,     "ATI RV840",	0, 0 },
	{ HDA_ATI_RV870,     "ATI RV870",	0, 0 },
	{ HDA_ATI_RV910,     "ATI RV910",	0, 0 },
	{ HDA_ATI_RV930,     "ATI RV930",	0, 0 },
	{ HDA_ATI_RV940,     "ATI RV940",	0, 0 },
	{ HDA_ATI_RV970,     "ATI RV970",	0, 0 },
	{ HDA_ATI_R1000,     "ATI R1000",	0, 0 },
	{ HDA_AMD_HUDSON2,   "AMD Hudson-2",	0, 0 },
	{ HDA_RDC_M3010,     "RDC M3010",	0, 0 },
	{ HDA_VIA_VT82XX,    "VIA VT8251/8237A",0, 0 },
	{ HDA_SIS_966,       "SiS 966/968",	0, 0 },
	{ HDA_ULI_M5461,     "ULI M5461",	0, 0 },
	/* Unknown */
	{ HDA_INTEL_ALL,  "Intel",		0, 0 },
	{ HDA_NVIDIA_ALL, "NVIDIA",		0, 0 },
	{ HDA_ATI_ALL,    "ATI",		0, 0 },
	{ HDA_AMD_ALL,    "AMD",		0, 0 },
	{ HDA_CREATIVE_ALL,    "Creative",	0, 0 },
	{ HDA_VIA_ALL,    "VIA",		0, 0 },
	{ HDA_SIS_ALL,    "SiS",		0, 0 },
	{ HDA_ULI_ALL,    "ULI",		0, 0 },
};

static const struct {
	uint16_t vendor;
	uint8_t reg;
	uint8_t mask;
	uint8_t enable;
} hdac_pcie_snoop[] = {
	{  INTEL_VENDORID, 0x00, 0x00, 0x00 },
	{    ATI_VENDORID, 0x42, 0xf8, 0x02 },
	{ NVIDIA_VENDORID, 0x4e, 0xf0, 0x0f },
};

/****************************************************************************
 * Function prototypes
 ****************************************************************************/
static void	hdac_intr_handler(void *);
static int	hdac_reset(struct hdac_softc *, int);
static int	hdac_get_capabilities(struct hdac_softc *);
static void	hdac_dma_cb(void *, bus_dma_segment_t *, int, int);
static int	hdac_dma_alloc(struct hdac_softc *,
					struct hdac_dma *, bus_size_t);
static void	hdac_dma_free(struct hdac_softc *, struct hdac_dma *);
static int	hdac_mem_alloc(struct hdac_softc *);
static void	hdac_mem_free(struct hdac_softc *);
static int	hdac_irq_alloc(struct hdac_softc *);
static void	hdac_irq_free(struct hdac_softc *);
static void	hdac_corb_init(struct hdac_softc *);
static void	hdac_rirb_init(struct hdac_softc *);
static void	hdac_corb_start(struct hdac_softc *);
static void	hdac_rirb_start(struct hdac_softc *);

static void	hdac_attach2(void *);

static uint32_t	hdac_send_command(struct hdac_softc *, nid_t, uint32_t);

static int	hdac_probe(device_t);
static int	hdac_attach(device_t);
static int	hdac_detach(device_t);
static int	hdac_suspend(device_t);
static int	hdac_resume(device_t);

static int	hdac_rirb_flush(struct hdac_softc *sc);
static int	hdac_unsolq_flush(struct hdac_softc *sc);

#define hdac_command(a1, a2, a3)	\
		hdac_send_command(a1, a3, a2)

/* This function surely going to make its way into upper level someday. */
static void
hdac_config_fetch(struct hdac_softc *sc, uint32_t *on, uint32_t *off)
{
	const char *res = NULL;
	int i = 0, j, k, len, inv;

	if (resource_string_value(device_get_name(sc->dev),
	    device_get_unit(sc->dev), "config", &res) != 0)
		return;
	if (!(res != NULL && strlen(res) > 0))
		return;
	HDA_BOOTVERBOSE(
		device_printf(sc->dev, "Config options:");
	);
	for (;;) {
		while (res[i] != '\0' &&
		    (res[i] == ',' || isspace(res[i]) != 0))
			i++;
		if (res[i] == '\0') {
			HDA_BOOTVERBOSE(
				printf("\n");
			);
			return;
		}
		j = i;
		while (res[j] != '\0' &&
		    !(res[j] == ',' || isspace(res[j]) != 0))
			j++;
		len = j - i;
		if (len > 2 && strncmp(res + i, "no", 2) == 0)
			inv = 2;
		else
			inv = 0;
		for (k = 0; len > inv && k < nitems(hdac_quirks_tab); k++) {
			if (strncmp(res + i + inv,
			    hdac_quirks_tab[k].key, len - inv) != 0)
				continue;
			if (len - inv != strlen(hdac_quirks_tab[k].key))
				continue;
			HDA_BOOTVERBOSE(
				printf(" %s%s", (inv != 0) ? "no" : "",
				    hdac_quirks_tab[k].key);
			);
			if (inv == 0) {
				*on |= hdac_quirks_tab[k].value;
				*on &= ~hdac_quirks_tab[k].value;
			} else if (inv != 0) {
				*off |= hdac_quirks_tab[k].value;
				*off &= ~hdac_quirks_tab[k].value;
			}
			break;
		}
		i = j;
	}
}

/****************************************************************************
 * void hdac_intr_handler(void *)
 *
 * Interrupt handler. Processes interrupts received from the hdac.
 ****************************************************************************/
static void
hdac_intr_handler(void *context)
{
	struct hdac_softc *sc;
	device_t dev;
	uint32_t intsts;
	uint8_t rirbsts;
	int i;

	sc = (struct hdac_softc *)context;
	hdac_lock(sc);

	/* Do we have anything to do? */
	intsts = HDAC_READ_4(&sc->mem, HDAC_INTSTS);
	if ((intsts & HDAC_INTSTS_GIS) == 0) {
		hdac_unlock(sc);
		return;
	}

	/* Was this a controller interrupt? */
	if (intsts & HDAC_INTSTS_CIS) {
		rirbsts = HDAC_READ_1(&sc->mem, HDAC_RIRBSTS);
		/* Get as many responses that we can */
		while (rirbsts & HDAC_RIRBSTS_RINTFL) {
			HDAC_WRITE_1(&sc->mem,
			    HDAC_RIRBSTS, HDAC_RIRBSTS_RINTFL);
			hdac_rirb_flush(sc);
			rirbsts = HDAC_READ_1(&sc->mem, HDAC_RIRBSTS);
		}
		if (sc->unsolq_rp != sc->unsolq_wp)
			taskqueue_enqueue(taskqueue_thread, &sc->unsolq_task);
	}

	if (intsts & HDAC_INTSTS_SIS_MASK) {
		for (i = 0; i < sc->num_ss; i++) {
			if ((intsts & (1 << i)) == 0)
				continue;
			HDAC_WRITE_1(&sc->mem, (i << 5) + HDAC_SDSTS,
			    HDAC_SDSTS_DESE | HDAC_SDSTS_FIFOE | HDAC_SDSTS_BCIS );
			if ((dev = sc->streams[i].dev) != NULL) {
				HDAC_STREAM_INTR(dev,
				    sc->streams[i].dir, sc->streams[i].stream);
			}
		}
	}

	HDAC_WRITE_4(&sc->mem, HDAC_INTSTS, intsts);
	hdac_unlock(sc);
}

static void
hdac_poll_callback(void *arg)
{
	struct hdac_softc *sc = arg;

	if (sc == NULL)
		return;

	hdac_lock(sc);
	if (sc->polling == 0) {
		hdac_unlock(sc);
		return;
	}
	callout_reset(&sc->poll_callout, sc->poll_ival,
	    hdac_poll_callback, sc);
	hdac_unlock(sc);

	hdac_intr_handler(sc);
}

/****************************************************************************
 * int hdac_reset(hdac_softc *, int)
 *
 * Reset the hdac to a quiescent and known state.
 ****************************************************************************/
static int
hdac_reset(struct hdac_softc *sc, int wakeup)
{
	uint32_t gctl;
	int count, i;

	/*
	 * Stop all Streams DMA engine
	 */
	for (i = 0; i < sc->num_iss; i++)
		HDAC_WRITE_4(&sc->mem, HDAC_ISDCTL(sc, i), 0x0);
	for (i = 0; i < sc->num_oss; i++)
		HDAC_WRITE_4(&sc->mem, HDAC_OSDCTL(sc, i), 0x0);
	for (i = 0; i < sc->num_bss; i++)
		HDAC_WRITE_4(&sc->mem, HDAC_BSDCTL(sc, i), 0x0);

	/*
	 * Stop Control DMA engines.
	 */
	HDAC_WRITE_1(&sc->mem, HDAC_CORBCTL, 0x0);
	HDAC_WRITE_1(&sc->mem, HDAC_RIRBCTL, 0x0);

	/*
	 * Reset DMA position buffer.
	 */
	HDAC_WRITE_4(&sc->mem, HDAC_DPIBLBASE, 0x0);
	HDAC_WRITE_4(&sc->mem, HDAC_DPIBUBASE, 0x0);

	/*
	 * Reset the controller. The reset must remain asserted for
	 * a minimum of 100us.
	 */
	gctl = HDAC_READ_4(&sc->mem, HDAC_GCTL);
	HDAC_WRITE_4(&sc->mem, HDAC_GCTL, gctl & ~HDAC_GCTL_CRST);
	count = 10000;
	do {
		gctl = HDAC_READ_4(&sc->mem, HDAC_GCTL);
		if (!(gctl & HDAC_GCTL_CRST))
			break;
		DELAY(10);
	} while	(--count);
	if (gctl & HDAC_GCTL_CRST) {
		device_printf(sc->dev, "Unable to put hdac in reset\n");
		return (ENXIO);
	}

	/* If wakeup is not requested - leave the controller in reset state. */
	if (!wakeup)
		return (0);

	DELAY(100);
	gctl = HDAC_READ_4(&sc->mem, HDAC_GCTL);
	HDAC_WRITE_4(&sc->mem, HDAC_GCTL, gctl | HDAC_GCTL_CRST);
	count = 10000;
	do {
		gctl = HDAC_READ_4(&sc->mem, HDAC_GCTL);
		if (gctl & HDAC_GCTL_CRST)
			break;
		DELAY(10);
	} while (--count);
	if (!(gctl & HDAC_GCTL_CRST)) {
		device_printf(sc->dev, "Device stuck in reset\n");
		return (ENXIO);
	}

	/*
	 * Wait for codecs to finish their own reset sequence. The delay here
	 * should be of 250us but for some reasons, it's not enough on my
	 * computer. Let's use twice as much as necessary to make sure that
	 * it's reset properly.
	 */
	DELAY(1000);

	return (0);
}


/****************************************************************************
 * int hdac_get_capabilities(struct hdac_softc *);
 *
 * Retreive the general capabilities of the hdac;
 *	Number of Input Streams
 *	Number of Output Streams
 *	Number of bidirectional Streams
 *	64bit ready
 *	CORB and RIRB sizes
 ****************************************************************************/
static int
hdac_get_capabilities(struct hdac_softc *sc)
{
	uint16_t gcap;
	uint8_t corbsize, rirbsize;

	gcap = HDAC_READ_2(&sc->mem, HDAC_GCAP);
	sc->num_iss = HDAC_GCAP_ISS(gcap);
	sc->num_oss = HDAC_GCAP_OSS(gcap);
	sc->num_bss = HDAC_GCAP_BSS(gcap);
	sc->num_ss = sc->num_iss + sc->num_oss + sc->num_bss;
	sc->num_sdo = HDAC_GCAP_NSDO(gcap);
	sc->support_64bit = (gcap & HDAC_GCAP_64OK) != 0;
	if (sc->quirks_on & HDAC_QUIRK_64BIT)
		sc->support_64bit = 1;
	else if (sc->quirks_off & HDAC_QUIRK_64BIT)
		sc->support_64bit = 0;

	corbsize = HDAC_READ_1(&sc->mem, HDAC_CORBSIZE);
	if ((corbsize & HDAC_CORBSIZE_CORBSZCAP_256) ==
	    HDAC_CORBSIZE_CORBSZCAP_256)
		sc->corb_size = 256;
	else if ((corbsize & HDAC_CORBSIZE_CORBSZCAP_16) ==
	    HDAC_CORBSIZE_CORBSZCAP_16)
		sc->corb_size = 16;
	else if ((corbsize & HDAC_CORBSIZE_CORBSZCAP_2) ==
	    HDAC_CORBSIZE_CORBSZCAP_2)
		sc->corb_size = 2;
	else {
		device_printf(sc->dev, "%s: Invalid corb size (%x)\n",
		    __func__, corbsize);
		return (ENXIO);
	}

	rirbsize = HDAC_READ_1(&sc->mem, HDAC_RIRBSIZE);
	if ((rirbsize & HDAC_RIRBSIZE_RIRBSZCAP_256) ==
	    HDAC_RIRBSIZE_RIRBSZCAP_256)
		sc->rirb_size = 256;
	else if ((rirbsize & HDAC_RIRBSIZE_RIRBSZCAP_16) ==
	    HDAC_RIRBSIZE_RIRBSZCAP_16)
		sc->rirb_size = 16;
	else if ((rirbsize & HDAC_RIRBSIZE_RIRBSZCAP_2) ==
	    HDAC_RIRBSIZE_RIRBSZCAP_2)
		sc->rirb_size = 2;
	else {
		device_printf(sc->dev, "%s: Invalid rirb size (%x)\n",
		    __func__, rirbsize);
		return (ENXIO);
	}

	HDA_BOOTVERBOSE(
		device_printf(sc->dev, "Caps: OSS %d, ISS %d, BSS %d, "
		    "NSDO %d%s, CORB %d, RIRB %d\n",
		    sc->num_oss, sc->num_iss, sc->num_bss, 1 << sc->num_sdo,
		    sc->support_64bit ? ", 64bit" : "",
		    sc->corb_size, sc->rirb_size);
	);

	return (0);
}


/****************************************************************************
 * void hdac_dma_cb
 *
 * This function is called by bus_dmamap_load when the mapping has been
 * established. We just record the physical address of the mapping into
 * the struct hdac_dma passed in.
 ****************************************************************************/
static void
hdac_dma_cb(void *callback_arg, bus_dma_segment_t *segs, int nseg, int error)
{
	struct hdac_dma *dma;

	if (error == 0) {
		dma = (struct hdac_dma *)callback_arg;
		dma->dma_paddr = segs[0].ds_addr;
	}
}


/****************************************************************************
 * int hdac_dma_alloc
 *
 * This function allocate and setup a dma region (struct hdac_dma).
 * It must be freed by a corresponding hdac_dma_free.
 ****************************************************************************/
static int
hdac_dma_alloc(struct hdac_softc *sc, struct hdac_dma *dma, bus_size_t size)
{
	bus_size_t roundsz;
	int result;

	roundsz = roundup2(size, HDA_DMA_ALIGNMENT);
	bzero(dma, sizeof(*dma));

	/*
	 * Create a DMA tag
	 */
	result = bus_dma_tag_create(
	    bus_get_dma_tag(sc->dev),		/* parent */
	    HDA_DMA_ALIGNMENT,			/* alignment */
	    0,					/* boundary */
	    (sc->support_64bit) ? BUS_SPACE_MAXADDR :
		BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
	    BUS_SPACE_MAXADDR,			/* highaddr */
	    NULL,				/* filtfunc */
	    NULL,				/* fistfuncarg */
	    roundsz, 				/* maxsize */
	    1,					/* nsegments */
	    roundsz, 				/* maxsegsz */
	    0,					/* flags */
	    NULL,				/* lockfunc */
	    NULL,				/* lockfuncarg */
	    &dma->dma_tag);			/* dmat */
	if (result != 0) {
		device_printf(sc->dev, "%s: bus_dma_tag_create failed (%d)\n",
		    __func__, result);
		goto hdac_dma_alloc_fail;
	}

	/*
	 * Allocate DMA memory
	 */
	result = bus_dmamem_alloc(dma->dma_tag, (void **)&dma->dma_vaddr,
	    BUS_DMA_NOWAIT | BUS_DMA_ZERO |
	    ((sc->flags & HDAC_F_DMA_NOCACHE) ? BUS_DMA_NOCACHE :
	     BUS_DMA_COHERENT),
	    &dma->dma_map);
	if (result != 0) {
		device_printf(sc->dev, "%s: bus_dmamem_alloc failed (%d)\n",
		    __func__, result);
		goto hdac_dma_alloc_fail;
	}

	dma->dma_size = roundsz;

	/*
	 * Map the memory
	 */
	result = bus_dmamap_load(dma->dma_tag, dma->dma_map,
	    (void *)dma->dma_vaddr, roundsz, hdac_dma_cb, (void *)dma, 0);
	if (result != 0 || dma->dma_paddr == 0) {
		if (result == 0)
			result = ENOMEM;
		device_printf(sc->dev, "%s: bus_dmamem_load failed (%d)\n",
		    __func__, result);
		goto hdac_dma_alloc_fail;
	}

	HDA_BOOTHVERBOSE(
		device_printf(sc->dev, "%s: size=%ju -> roundsz=%ju\n",
		    __func__, (uintmax_t)size, (uintmax_t)roundsz);
	);

	return (0);

hdac_dma_alloc_fail:
	hdac_dma_free(sc, dma);

	return (result);
}


/****************************************************************************
 * void hdac_dma_free(struct hdac_softc *, struct hdac_dma *)
 *
 * Free a struct dhac_dma that has been previously allocated via the
 * hdac_dma_alloc function.
 ****************************************************************************/
static void
hdac_dma_free(struct hdac_softc *sc, struct hdac_dma *dma)
{
	if (dma->dma_paddr != 0) {
		/* Flush caches */
		bus_dmamap_sync(dma->dma_tag, dma->dma_map,
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(dma->dma_tag, dma->dma_map);
		dma->dma_paddr = 0;
	}
	if (dma->dma_vaddr != NULL) {
		bus_dmamem_free(dma->dma_tag, dma->dma_vaddr, dma->dma_map);
		dma->dma_vaddr = NULL;
	}
	if (dma->dma_tag != NULL) {
		bus_dma_tag_destroy(dma->dma_tag);
		dma->dma_tag = NULL;
	}
	dma->dma_size = 0;
}

/****************************************************************************
 * int hdac_mem_alloc(struct hdac_softc *)
 *
 * Allocate all the bus resources necessary to speak with the physical
 * controller.
 ****************************************************************************/
static int
hdac_mem_alloc(struct hdac_softc *sc)
{
	struct hdac_mem *mem;

	mem = &sc->mem;
	mem->mem_rid = PCIR_BAR(0);
	mem->mem_res = bus_alloc_resource_any(sc->dev, SYS_RES_MEMORY,
	    &mem->mem_rid, RF_ACTIVE);
	if (mem->mem_res == NULL) {
		device_printf(sc->dev,
		    "%s: Unable to allocate memory resource\n", __func__);
		return (ENOMEM);
	}
	mem->mem_tag = rman_get_bustag(mem->mem_res);
	mem->mem_handle = rman_get_bushandle(mem->mem_res);

	return (0);
}

/****************************************************************************
 * void hdac_mem_free(struct hdac_softc *)
 *
 * Free up resources previously allocated by hdac_mem_alloc.
 ****************************************************************************/
static void
hdac_mem_free(struct hdac_softc *sc)
{
	struct hdac_mem *mem;

	mem = &sc->mem;
	if (mem->mem_res != NULL)
		bus_release_resource(sc->dev, SYS_RES_MEMORY, mem->mem_rid,
		    mem->mem_res);
	mem->mem_res = NULL;
}

/****************************************************************************
 * int hdac_irq_alloc(struct hdac_softc *)
 *
 * Allocate and setup the resources necessary for interrupt handling.
 ****************************************************************************/
static int
hdac_irq_alloc(struct hdac_softc *sc)
{
	struct hdac_irq *irq;
	int result;

	irq = &sc->irq;
	irq->irq_rid = 0x0;

	if ((sc->quirks_off & HDAC_QUIRK_MSI) == 0 &&
	    (result = pci_msi_count(sc->dev)) == 1 &&
	    pci_alloc_msi(sc->dev, &result) == 0)
		irq->irq_rid = 0x1;

	irq->irq_res = bus_alloc_resource_any(sc->dev, SYS_RES_IRQ,
	    &irq->irq_rid, RF_SHAREABLE | RF_ACTIVE);
	if (irq->irq_res == NULL) {
		device_printf(sc->dev, "%s: Unable to allocate irq\n",
		    __func__);
		goto hdac_irq_alloc_fail;
	}
	result = bus_setup_intr(sc->dev, irq->irq_res, INTR_MPSAFE | INTR_TYPE_AV,
	    NULL, hdac_intr_handler, sc, &irq->irq_handle);
	if (result != 0) {
		device_printf(sc->dev,
		    "%s: Unable to setup interrupt handler (%d)\n",
		    __func__, result);
		goto hdac_irq_alloc_fail;
	}

	return (0);

hdac_irq_alloc_fail:
	hdac_irq_free(sc);

	return (ENXIO);
}

/****************************************************************************
 * void hdac_irq_free(struct hdac_softc *)
 *
 * Free up resources previously allocated by hdac_irq_alloc.
 ****************************************************************************/
static void
hdac_irq_free(struct hdac_softc *sc)
{
	struct hdac_irq *irq;

	irq = &sc->irq;
	if (irq->irq_res != NULL && irq->irq_handle != NULL)
		bus_teardown_intr(sc->dev, irq->irq_res, irq->irq_handle);
	if (irq->irq_res != NULL)
		bus_release_resource(sc->dev, SYS_RES_IRQ, irq->irq_rid,
		    irq->irq_res);
	if (irq->irq_rid == 0x1)
		pci_release_msi(sc->dev);
	irq->irq_handle = NULL;
	irq->irq_res = NULL;
	irq->irq_rid = 0x0;
}

/****************************************************************************
 * void hdac_corb_init(struct hdac_softc *)
 *
 * Initialize the corb registers for operations but do not start it up yet.
 * The CORB engine must not be running when this function is called.
 ****************************************************************************/
static void
hdac_corb_init(struct hdac_softc *sc)
{
	uint8_t corbsize;
	uint64_t corbpaddr;

	/* Setup the CORB size. */
	switch (sc->corb_size) {
	case 256:
		corbsize = HDAC_CORBSIZE_CORBSIZE(HDAC_CORBSIZE_CORBSIZE_256);
		break;
	case 16:
		corbsize = HDAC_CORBSIZE_CORBSIZE(HDAC_CORBSIZE_CORBSIZE_16);
		break;
	case 2:
		corbsize = HDAC_CORBSIZE_CORBSIZE(HDAC_CORBSIZE_CORBSIZE_2);
		break;
	default:
		panic("%s: Invalid CORB size (%x)\n", __func__, sc->corb_size);
	}
	HDAC_WRITE_1(&sc->mem, HDAC_CORBSIZE, corbsize);

	/* Setup the CORB Address in the hdac */
	corbpaddr = (uint64_t)sc->corb_dma.dma_paddr;
	HDAC_WRITE_4(&sc->mem, HDAC_CORBLBASE, (uint32_t)corbpaddr);
	HDAC_WRITE_4(&sc->mem, HDAC_CORBUBASE, (uint32_t)(corbpaddr >> 32));

	/* Set the WP and RP */
	sc->corb_wp = 0;
	HDAC_WRITE_2(&sc->mem, HDAC_CORBWP, sc->corb_wp);
	HDAC_WRITE_2(&sc->mem, HDAC_CORBRP, HDAC_CORBRP_CORBRPRST);
	/*
	 * The HDA specification indicates that the CORBRPRST bit will always
	 * read as zero. Unfortunately, it seems that at least the 82801G
	 * doesn't reset the bit to zero, which stalls the corb engine.
	 * manually reset the bit to zero before continuing.
	 */
	HDAC_WRITE_2(&sc->mem, HDAC_CORBRP, 0x0);

	/* Enable CORB error reporting */
#if 0
	HDAC_WRITE_1(&sc->mem, HDAC_CORBCTL, HDAC_CORBCTL_CMEIE);
#endif
}

/****************************************************************************
 * void hdac_rirb_init(struct hdac_softc *)
 *
 * Initialize the rirb registers for operations but do not start it up yet.
 * The RIRB engine must not be running when this function is called.
 ****************************************************************************/
static void
hdac_rirb_init(struct hdac_softc *sc)
{
	uint8_t rirbsize;
	uint64_t rirbpaddr;

	/* Setup the RIRB size. */
	switch (sc->rirb_size) {
	case 256:
		rirbsize = HDAC_RIRBSIZE_RIRBSIZE(HDAC_RIRBSIZE_RIRBSIZE_256);
		break;
	case 16:
		rirbsize = HDAC_RIRBSIZE_RIRBSIZE(HDAC_RIRBSIZE_RIRBSIZE_16);
		break;
	case 2:
		rirbsize = HDAC_RIRBSIZE_RIRBSIZE(HDAC_RIRBSIZE_RIRBSIZE_2);
		break;
	default:
		panic("%s: Invalid RIRB size (%x)\n", __func__, sc->rirb_size);
	}
	HDAC_WRITE_1(&sc->mem, HDAC_RIRBSIZE, rirbsize);

	/* Setup the RIRB Address in the hdac */
	rirbpaddr = (uint64_t)sc->rirb_dma.dma_paddr;
	HDAC_WRITE_4(&sc->mem, HDAC_RIRBLBASE, (uint32_t)rirbpaddr);
	HDAC_WRITE_4(&sc->mem, HDAC_RIRBUBASE, (uint32_t)(rirbpaddr >> 32));

	/* Setup the WP and RP */
	sc->rirb_rp = 0;
	HDAC_WRITE_2(&sc->mem, HDAC_RIRBWP, HDAC_RIRBWP_RIRBWPRST);

	/* Setup the interrupt threshold */
	HDAC_WRITE_2(&sc->mem, HDAC_RINTCNT, sc->rirb_size / 2);

	/* Enable Overrun and response received reporting */
#if 0
	HDAC_WRITE_1(&sc->mem, HDAC_RIRBCTL,
	    HDAC_RIRBCTL_RIRBOIC | HDAC_RIRBCTL_RINTCTL);
#else
	HDAC_WRITE_1(&sc->mem, HDAC_RIRBCTL, HDAC_RIRBCTL_RINTCTL);
#endif

	/*
	 * Make sure that the Host CPU cache doesn't contain any dirty
	 * cache lines that falls in the rirb. If I understood correctly, it
	 * should be sufficient to do this only once as the rirb is purely
	 * read-only from now on.
	 */
	bus_dmamap_sync(sc->rirb_dma.dma_tag, sc->rirb_dma.dma_map,
	    BUS_DMASYNC_PREREAD);
}

/****************************************************************************
 * void hdac_corb_start(hdac_softc *)
 *
 * Startup the corb DMA engine
 ****************************************************************************/
static void
hdac_corb_start(struct hdac_softc *sc)
{
	uint32_t corbctl;

	corbctl = HDAC_READ_1(&sc->mem, HDAC_CORBCTL);
	corbctl |= HDAC_CORBCTL_CORBRUN;
	HDAC_WRITE_1(&sc->mem, HDAC_CORBCTL, corbctl);
}

/****************************************************************************
 * void hdac_rirb_start(hdac_softc *)
 *
 * Startup the rirb DMA engine
 ****************************************************************************/
static void
hdac_rirb_start(struct hdac_softc *sc)
{
	uint32_t rirbctl;

	rirbctl = HDAC_READ_1(&sc->mem, HDAC_RIRBCTL);
	rirbctl |= HDAC_RIRBCTL_RIRBDMAEN;
	HDAC_WRITE_1(&sc->mem, HDAC_RIRBCTL, rirbctl);
}

static int
hdac_rirb_flush(struct hdac_softc *sc)
{
	struct hdac_rirb *rirb_base, *rirb;
	nid_t cad;
	uint32_t resp, resp_ex;
	uint8_t rirbwp;
	int ret;

	rirb_base = (struct hdac_rirb *)sc->rirb_dma.dma_vaddr;
	rirbwp = HDAC_READ_1(&sc->mem, HDAC_RIRBWP);
	bus_dmamap_sync(sc->rirb_dma.dma_tag, sc->rirb_dma.dma_map,
	    BUS_DMASYNC_POSTREAD);

	ret = 0;
	while (sc->rirb_rp != rirbwp) {
		sc->rirb_rp++;
		sc->rirb_rp %= sc->rirb_size;
		rirb = &rirb_base[sc->rirb_rp];
		resp = le32toh(rirb->response);
		resp_ex = le32toh(rirb->response_ex);
		cad = HDAC_RIRB_RESPONSE_EX_SDATA_IN(resp_ex);
		if (resp_ex & HDAC_RIRB_RESPONSE_EX_UNSOLICITED) {
			sc->unsolq[sc->unsolq_wp++] = resp;
			sc->unsolq_wp %= HDAC_UNSOLQ_MAX;
			sc->unsolq[sc->unsolq_wp++] = cad;
			sc->unsolq_wp %= HDAC_UNSOLQ_MAX;
		} else if (sc->codecs[cad].pending <= 0) {
			device_printf(sc->dev, "Unexpected unsolicited "
			    "response from address %d: %08x\n", cad, resp);
		} else {
			sc->codecs[cad].response = resp;
			sc->codecs[cad].pending--;
		}
		ret++;
	}

	bus_dmamap_sync(sc->rirb_dma.dma_tag, sc->rirb_dma.dma_map,
	    BUS_DMASYNC_PREREAD);
	return (ret);
}

static int
hdac_unsolq_flush(struct hdac_softc *sc)
{
	device_t child;
	nid_t cad;
	uint32_t resp;
	int ret = 0;

	if (sc->unsolq_st == HDAC_UNSOLQ_READY) {
		sc->unsolq_st = HDAC_UNSOLQ_BUSY;
		while (sc->unsolq_rp != sc->unsolq_wp) {
			resp = sc->unsolq[sc->unsolq_rp++];
			sc->unsolq_rp %= HDAC_UNSOLQ_MAX;
			cad = sc->unsolq[sc->unsolq_rp++];
			sc->unsolq_rp %= HDAC_UNSOLQ_MAX;
			if ((child = sc->codecs[cad].dev) != NULL)
				HDAC_UNSOL_INTR(child, resp);
			ret++;
		}
		sc->unsolq_st = HDAC_UNSOLQ_READY;
	}

	return (ret);
}

/****************************************************************************
 * uint32_t hdac_command_sendone_internal
 *
 * Wrapper function that sends only one command to a given codec
 ****************************************************************************/
static uint32_t
hdac_send_command(struct hdac_softc *sc, nid_t cad, uint32_t verb)
{
	int timeout;
	uint32_t *corb;

	if (!hdac_lockowned(sc))
		device_printf(sc->dev, "WARNING!!!! mtx not owned!!!!\n");
	verb &= ~HDA_CMD_CAD_MASK;
	verb |= ((uint32_t)cad) << HDA_CMD_CAD_SHIFT;
	sc->codecs[cad].response = HDA_INVALID;

	sc->codecs[cad].pending++;
	sc->corb_wp++;
	sc->corb_wp %= sc->corb_size;
	corb = (uint32_t *)sc->corb_dma.dma_vaddr;
	bus_dmamap_sync(sc->corb_dma.dma_tag,
	    sc->corb_dma.dma_map, BUS_DMASYNC_PREWRITE);
	corb[sc->corb_wp] = htole32(verb);
	bus_dmamap_sync(sc->corb_dma.dma_tag,
	    sc->corb_dma.dma_map, BUS_DMASYNC_POSTWRITE);
	HDAC_WRITE_2(&sc->mem, HDAC_CORBWP, sc->corb_wp);

	timeout = 10000;
	do {
		if (hdac_rirb_flush(sc) == 0)
			DELAY(10);
	} while (sc->codecs[cad].pending != 0 && --timeout);

	if (sc->codecs[cad].pending != 0) {
		device_printf(sc->dev, "Command timeout on address %d\n", cad);
		sc->codecs[cad].pending = 0;
	}

	if (sc->unsolq_rp != sc->unsolq_wp)
		taskqueue_enqueue(taskqueue_thread, &sc->unsolq_task);
	return (sc->codecs[cad].response);
}

/****************************************************************************
 * Device Methods
 ****************************************************************************/

/****************************************************************************
 * int hdac_probe(device_t)
 *
 * Probe for the presence of an hdac. If none is found, check for a generic
 * match using the subclass of the device.
 ****************************************************************************/
static int
hdac_probe(device_t dev)
{
	int i, result;
	uint32_t model;
	uint16_t class, subclass;
	char desc[64];

	model = (uint32_t)pci_get_device(dev) << 16;
	model |= (uint32_t)pci_get_vendor(dev) & 0x0000ffff;
	class = pci_get_class(dev);
	subclass = pci_get_subclass(dev);

	bzero(desc, sizeof(desc));
	result = ENXIO;
	for (i = 0; i < nitems(hdac_devices); i++) {
		if (hdac_devices[i].model == model) {
			strlcpy(desc, hdac_devices[i].desc, sizeof(desc));
			result = BUS_PROBE_DEFAULT;
			break;
		}
		if (HDA_DEV_MATCH(hdac_devices[i].model, model) &&
		    class == PCIC_MULTIMEDIA &&
		    subclass == PCIS_MULTIMEDIA_HDA) {
			snprintf(desc, sizeof(desc),
			    "%s (0x%04x)",
			    hdac_devices[i].desc, pci_get_device(dev));
			result = BUS_PROBE_GENERIC;
			break;
		}
	}
	if (result == ENXIO && class == PCIC_MULTIMEDIA &&
	    subclass == PCIS_MULTIMEDIA_HDA) {
		snprintf(desc, sizeof(desc), "Generic (0x%08x)", model);
		result = BUS_PROBE_GENERIC;
	}
	if (result != ENXIO) {
		strlcat(desc, " HDA Controller", sizeof(desc));
		device_set_desc_copy(dev, desc);
	}

	return (result);
}

static void
hdac_unsolq_task(void *context, int pending)
{
	struct hdac_softc *sc;

	sc = (struct hdac_softc *)context;

	hdac_lock(sc);
	hdac_unsolq_flush(sc);
	hdac_unlock(sc);
}

/****************************************************************************
 * int hdac_attach(device_t)
 *
 * Attach the device into the kernel. Interrupts usually won't be enabled
 * when this function is called. Setup everything that doesn't require
 * interrupts and defer probing of codecs until interrupts are enabled.
 ****************************************************************************/
static int
hdac_attach(device_t dev)
{
	struct hdac_softc *sc;
	int result;
	int i, devid = -1;
	uint32_t model;
	uint16_t class, subclass;
	uint16_t vendor;
	uint8_t v;

	sc = device_get_softc(dev);
	HDA_BOOTVERBOSE(
		device_printf(dev, "PCI card vendor: 0x%04x, device: 0x%04x\n",
		    pci_get_subvendor(dev), pci_get_subdevice(dev));
		device_printf(dev, "HDA Driver Revision: %s\n",
		    HDA_DRV_TEST_REV);
	);

	model = (uint32_t)pci_get_device(dev) << 16;
	model |= (uint32_t)pci_get_vendor(dev) & 0x0000ffff;
	class = pci_get_class(dev);
	subclass = pci_get_subclass(dev);

	for (i = 0; i < nitems(hdac_devices); i++) {
		if (hdac_devices[i].model == model) {
			devid = i;
			break;
		}
		if (HDA_DEV_MATCH(hdac_devices[i].model, model) &&
		    class == PCIC_MULTIMEDIA &&
		    subclass == PCIS_MULTIMEDIA_HDA) {
			devid = i;
			break;
		}
	}

	sc->lock = snd_mtxcreate(device_get_nameunit(dev), "HDA driver mutex");
	sc->dev = dev;
	TASK_INIT(&sc->unsolq_task, 0, hdac_unsolq_task, sc);
	callout_init(&sc->poll_callout, 1);
	for (i = 0; i < HDAC_CODEC_MAX; i++)
		sc->codecs[i].dev = NULL;
	if (devid >= 0) {
		sc->quirks_on = hdac_devices[devid].quirks_on;
		sc->quirks_off = hdac_devices[devid].quirks_off;
	} else {
		sc->quirks_on = 0;
		sc->quirks_off = 0;
	}
	if (resource_int_value(device_get_name(dev),
	    device_get_unit(dev), "msi", &i) == 0) {
		if (i == 0)
			sc->quirks_off |= HDAC_QUIRK_MSI;
		else {
			sc->quirks_on |= HDAC_QUIRK_MSI;
			sc->quirks_off |= ~HDAC_QUIRK_MSI;
		}
	}
	hdac_config_fetch(sc, &sc->quirks_on, &sc->quirks_off);
	HDA_BOOTVERBOSE(
		device_printf(sc->dev,
		    "Config options: on=0x%08x off=0x%08x\n",
		    sc->quirks_on, sc->quirks_off);
	);
	sc->poll_ival = hz;
	if (resource_int_value(device_get_name(dev),
	    device_get_unit(dev), "polling", &i) == 0 && i != 0)
		sc->polling = 1;
	else
		sc->polling = 0;

	pci_enable_busmaster(dev);

	vendor = pci_get_vendor(dev);
	if (vendor == INTEL_VENDORID) {
		/* TCSEL -> TC0 */
		v = pci_read_config(dev, 0x44, 1);
		pci_write_config(dev, 0x44, v & 0xf8, 1);
		HDA_BOOTHVERBOSE(
			device_printf(dev, "TCSEL: 0x%02d -> 0x%02d\n", v,
			    pci_read_config(dev, 0x44, 1));
		);
	}

#if defined(__i386__) || defined(__amd64__)
	sc->flags |= HDAC_F_DMA_NOCACHE;

	if (resource_int_value(device_get_name(dev),
	    device_get_unit(dev), "snoop", &i) == 0 && i != 0) {
#else
	sc->flags &= ~HDAC_F_DMA_NOCACHE;
#endif
		/*
		 * Try to enable PCIe snoop to avoid messing around with
		 * uncacheable DMA attribute. Since PCIe snoop register
		 * config is pretty much vendor specific, there are no
		 * general solutions on how to enable it, forcing us (even
		 * Microsoft) to enable uncacheable or write combined DMA
		 * by default.
		 *
		 * http://msdn2.microsoft.com/en-us/library/ms790324.aspx
		 */
		for (i = 0; i < nitems(hdac_pcie_snoop); i++) {
			if (hdac_pcie_snoop[i].vendor != vendor)
				continue;
			sc->flags &= ~HDAC_F_DMA_NOCACHE;
			if (hdac_pcie_snoop[i].reg == 0x00)
				break;
			v = pci_read_config(dev, hdac_pcie_snoop[i].reg, 1);
			if ((v & hdac_pcie_snoop[i].enable) ==
			    hdac_pcie_snoop[i].enable)
				break;
			v &= hdac_pcie_snoop[i].mask;
			v |= hdac_pcie_snoop[i].enable;
			pci_write_config(dev, hdac_pcie_snoop[i].reg, v, 1);
			v = pci_read_config(dev, hdac_pcie_snoop[i].reg, 1);
			if ((v & hdac_pcie_snoop[i].enable) !=
			    hdac_pcie_snoop[i].enable) {
				HDA_BOOTVERBOSE(
					device_printf(dev,
					    "WARNING: Failed to enable PCIe "
					    "snoop!\n");
				);
#if defined(__i386__) || defined(__amd64__)
				sc->flags |= HDAC_F_DMA_NOCACHE;
#endif
			}
			break;
		}
#if defined(__i386__) || defined(__amd64__)
	}
#endif

	HDA_BOOTHVERBOSE(
		device_printf(dev, "DMA Coherency: %s / vendor=0x%04x\n",
		    (sc->flags & HDAC_F_DMA_NOCACHE) ?
		    "Uncacheable" : "PCIe snoop", vendor);
	);

	/* Allocate resources */
	result = hdac_mem_alloc(sc);
	if (result != 0)
		goto hdac_attach_fail;
	result = hdac_irq_alloc(sc);
	if (result != 0)
		goto hdac_attach_fail;

	/* Get Capabilities */
	result = hdac_get_capabilities(sc);
	if (result != 0)
		goto hdac_attach_fail;

	/* Allocate CORB, RIRB, POS and BDLs dma memory */
	result = hdac_dma_alloc(sc, &sc->corb_dma,
	    sc->corb_size * sizeof(uint32_t));
	if (result != 0)
		goto hdac_attach_fail;
	result = hdac_dma_alloc(sc, &sc->rirb_dma,
	    sc->rirb_size * sizeof(struct hdac_rirb));
	if (result != 0)
		goto hdac_attach_fail;
	sc->streams = malloc(sizeof(struct hdac_stream) * sc->num_ss,
	    M_HDAC, M_ZERO | M_WAITOK);
	for (i = 0; i < sc->num_ss; i++) {
		result = hdac_dma_alloc(sc, &sc->streams[i].bdl,
		    sizeof(struct hdac_bdle) * HDA_BDL_MAX);
		if (result != 0)
			goto hdac_attach_fail;
	}
	if (sc->quirks_on & HDAC_QUIRK_DMAPOS) {
		if (hdac_dma_alloc(sc, &sc->pos_dma, (sc->num_ss) * 8) != 0) {
			HDA_BOOTVERBOSE(
				device_printf(dev, "Failed to "
				    "allocate DMA pos buffer "
				    "(non-fatal)\n");
			);
		} else {
			uint64_t addr = sc->pos_dma.dma_paddr;

			HDAC_WRITE_4(&sc->mem, HDAC_DPIBUBASE, addr >> 32);
			HDAC_WRITE_4(&sc->mem, HDAC_DPIBLBASE,
			    (addr & HDAC_DPLBASE_DPLBASE_MASK) |
			    HDAC_DPLBASE_DPLBASE_DMAPBE);
		}
	}

	result = bus_dma_tag_create(
	    bus_get_dma_tag(sc->dev),		/* parent */
	    HDA_DMA_ALIGNMENT,			/* alignment */
	    0,					/* boundary */
	    (sc->support_64bit) ? BUS_SPACE_MAXADDR :
		BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
	    BUS_SPACE_MAXADDR,			/* highaddr */
	    NULL,				/* filtfunc */
	    NULL,				/* fistfuncarg */
	    HDA_BUFSZ_MAX, 			/* maxsize */
	    1,					/* nsegments */
	    HDA_BUFSZ_MAX, 			/* maxsegsz */
	    0,					/* flags */
	    NULL,				/* lockfunc */
	    NULL,				/* lockfuncarg */
	    &sc->chan_dmat);			/* dmat */
	if (result != 0) {
		device_printf(dev, "%s: bus_dma_tag_create failed (%d)\n",
		     __func__, result);
		goto hdac_attach_fail;
	}

	/* Quiesce everything */
	HDA_BOOTHVERBOSE(
		device_printf(dev, "Reset controller...\n");
	);
	hdac_reset(sc, 1);

	/* Initialize the CORB and RIRB */
	hdac_corb_init(sc);
	hdac_rirb_init(sc);

	/* Defer remaining of initialization until interrupts are enabled */
	sc->intrhook.ich_func = hdac_attach2;
	sc->intrhook.ich_arg = (void *)sc;
	if (cold == 0 || config_intrhook_establish(&sc->intrhook) != 0) {
		sc->intrhook.ich_func = NULL;
		hdac_attach2((void *)sc);
	}

	return (0);

hdac_attach_fail:
	hdac_irq_free(sc);
	if (sc->streams != NULL)
		for (i = 0; i < sc->num_ss; i++)
			hdac_dma_free(sc, &sc->streams[i].bdl);
	free(sc->streams, M_HDAC);
	hdac_dma_free(sc, &sc->rirb_dma);
	hdac_dma_free(sc, &sc->corb_dma);
	hdac_mem_free(sc);
	snd_mtxfree(sc->lock);

	return (ENXIO);
}

static int
sysctl_hdac_pindump(SYSCTL_HANDLER_ARGS)
{
	struct hdac_softc *sc;
	device_t *devlist;
	device_t dev;
	int devcount, i, err, val;

	dev = oidp->oid_arg1;
	sc = device_get_softc(dev);
	if (sc == NULL)
		return (EINVAL);
	val = 0;
	err = sysctl_handle_int(oidp, &val, 0, req);
	if (err != 0 || req->newptr == NULL || val == 0)
		return (err);

	/* XXX: Temporary. For debugging. */
	if (val == 100) {
		hdac_suspend(dev);
		return (0);
	} else if (val == 101) {
		hdac_resume(dev);
		return (0);
	}

	if ((err = device_get_children(dev, &devlist, &devcount)) != 0)
		return (err);
	hdac_lock(sc);
	for (i = 0; i < devcount; i++)
		HDAC_PINDUMP(devlist[i]);
	hdac_unlock(sc);
	free(devlist, M_TEMP);
	return (0);
}

static int
hdac_mdata_rate(uint16_t fmt)
{
	static const int mbits[8] = { 8, 16, 32, 32, 32, 32, 32, 32 };
	int rate, bits;

	if (fmt & (1 << 14))
		rate = 44100;
	else
		rate = 48000;
	rate *= ((fmt >> 11) & 0x07) + 1;
	rate /= ((fmt >> 8) & 0x07) + 1;
	bits = mbits[(fmt >> 4) & 0x03];
	bits *= (fmt & 0x0f) + 1;
	return (rate * bits);
}

static int
hdac_bdata_rate(uint16_t fmt, int output)
{
	static const int bbits[8] = { 8, 16, 20, 24, 32, 32, 32, 32 };
	int rate, bits;

	rate = 48000;
	rate *= ((fmt >> 11) & 0x07) + 1;
	bits = bbits[(fmt >> 4) & 0x03];
	bits *= (fmt & 0x0f) + 1;
	if (!output)
		bits = ((bits + 7) & ~0x07) + 10;
	return (rate * bits);
}

static void
hdac_poll_reinit(struct hdac_softc *sc)
{
	int i, pollticks, min = 1000000;
	struct hdac_stream *s;

	if (sc->polling == 0)
		return;
	if (sc->unsol_registered > 0)
		min = hz / 2;
	for (i = 0; i < sc->num_ss; i++) {
		s = &sc->streams[i];
		if (s->running == 0)
			continue;
		pollticks = ((uint64_t)hz * s->blksz) /
		    (hdac_mdata_rate(s->format) / 8);
		pollticks >>= 1;
		if (pollticks > hz)
			pollticks = hz;
		if (pollticks < 1) {
			HDA_BOOTVERBOSE(
				device_printf(sc->dev,
				    "poll interval < 1 tick !\n");
			);
			pollticks = 1;
		}
		if (min > pollticks)
			min = pollticks;
	}
	HDA_BOOTVERBOSE(
		device_printf(sc->dev,
		    "poll interval %d -> %d ticks\n",
		    sc->poll_ival, min);
	);
	sc->poll_ival = min;
	if (min == 1000000)
		callout_stop(&sc->poll_callout);
	else
		callout_reset(&sc->poll_callout, 1, hdac_poll_callback, sc);
}

static int
sysctl_hdac_polling(SYSCTL_HANDLER_ARGS)
{
	struct hdac_softc *sc;
	device_t dev;
	uint32_t ctl;
	int err, val;

	dev = oidp->oid_arg1;
	sc = device_get_softc(dev);
	if (sc == NULL)
		return (EINVAL);
	hdac_lock(sc);
	val = sc->polling;
	hdac_unlock(sc);
	err = sysctl_handle_int(oidp, &val, 0, req);

	if (err != 0 || req->newptr == NULL)
		return (err);
	if (val < 0 || val > 1)
		return (EINVAL);

	hdac_lock(sc);
	if (val != sc->polling) {
		if (val == 0) {
			callout_stop(&sc->poll_callout);
			hdac_unlock(sc);
			callout_drain(&sc->poll_callout);
			hdac_lock(sc);
			sc->polling = 0;
			ctl = HDAC_READ_4(&sc->mem, HDAC_INTCTL);
			ctl |= HDAC_INTCTL_GIE;
			HDAC_WRITE_4(&sc->mem, HDAC_INTCTL, ctl);
		} else {
			ctl = HDAC_READ_4(&sc->mem, HDAC_INTCTL);
			ctl &= ~HDAC_INTCTL_GIE;
			HDAC_WRITE_4(&sc->mem, HDAC_INTCTL, ctl);
			sc->polling = 1;
			hdac_poll_reinit(sc);
		}
	}
	hdac_unlock(sc);

	return (err);
}

static void
hdac_attach2(void *arg)
{
	struct hdac_softc *sc;
	device_t child;
	uint32_t vendorid, revisionid;
	int i;
	uint16_t statests;

	sc = (struct hdac_softc *)arg;

	hdac_lock(sc);

	/* Remove ourselves from the config hooks */
	if (sc->intrhook.ich_func != NULL) {
		config_intrhook_disestablish(&sc->intrhook);
		sc->intrhook.ich_func = NULL;
	}

	HDA_BOOTHVERBOSE(
		device_printf(sc->dev, "Starting CORB Engine...\n");
	);
	hdac_corb_start(sc);
	HDA_BOOTHVERBOSE(
		device_printf(sc->dev, "Starting RIRB Engine...\n");
	);
	hdac_rirb_start(sc);
	HDA_BOOTHVERBOSE(
		device_printf(sc->dev,
		    "Enabling controller interrupt...\n");
	);
	HDAC_WRITE_4(&sc->mem, HDAC_GCTL, HDAC_READ_4(&sc->mem, HDAC_GCTL) |
	    HDAC_GCTL_UNSOL);
	if (sc->polling == 0) {
		HDAC_WRITE_4(&sc->mem, HDAC_INTCTL,
		    HDAC_INTCTL_CIE | HDAC_INTCTL_GIE);
	}
	DELAY(1000);

	HDA_BOOTHVERBOSE(
		device_printf(sc->dev, "Scanning HDA codecs ...\n");
	);
	statests = HDAC_READ_2(&sc->mem, HDAC_STATESTS);
	hdac_unlock(sc);
	for (i = 0; i < HDAC_CODEC_MAX; i++) {
		if (HDAC_STATESTS_SDIWAKE(statests, i)) {
			HDA_BOOTHVERBOSE(
				device_printf(sc->dev,
				    "Found CODEC at address %d\n", i);
			);
			hdac_lock(sc);
			vendorid = hdac_send_command(sc, i,
			    HDA_CMD_GET_PARAMETER(0, 0x0, HDA_PARAM_VENDOR_ID));
			revisionid = hdac_send_command(sc, i,
			    HDA_CMD_GET_PARAMETER(0, 0x0, HDA_PARAM_REVISION_ID));
			hdac_unlock(sc);
			if (vendorid == HDA_INVALID &&
			    revisionid == HDA_INVALID) {
				device_printf(sc->dev,
				    "CODEC is not responding!\n");
				continue;
			}
			sc->codecs[i].vendor_id =
			    HDA_PARAM_VENDOR_ID_VENDOR_ID(vendorid);
			sc->codecs[i].device_id =
			    HDA_PARAM_VENDOR_ID_DEVICE_ID(vendorid);
			sc->codecs[i].revision_id =
			    HDA_PARAM_REVISION_ID_REVISION_ID(revisionid);
			sc->codecs[i].stepping_id =
			    HDA_PARAM_REVISION_ID_STEPPING_ID(revisionid);
			child = device_add_child(sc->dev, "hdacc", -1);
			if (child == NULL) {
				device_printf(sc->dev,
				    "Failed to add CODEC device\n");
				continue;
			}
			device_set_ivars(child, (void *)(intptr_t)i);
			sc->codecs[i].dev = child;
		}
	}
	bus_generic_attach(sc->dev);

	SYSCTL_ADD_PROC(device_get_sysctl_ctx(sc->dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(sc->dev)), OID_AUTO,
	    "pindump", CTLTYPE_INT | CTLFLAG_RW, sc->dev, sizeof(sc->dev),
	    sysctl_hdac_pindump, "I", "Dump pin states/data");
	SYSCTL_ADD_PROC(device_get_sysctl_ctx(sc->dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(sc->dev)), OID_AUTO,
	    "polling", CTLTYPE_INT | CTLFLAG_RW, sc->dev, sizeof(sc->dev),
	    sysctl_hdac_polling, "I", "Enable polling mode");
}

/****************************************************************************
 * int hdac_suspend(device_t)
 *
 * Suspend and power down HDA bus and codecs.
 ****************************************************************************/
static int
hdac_suspend(device_t dev)
{
	struct hdac_softc *sc = device_get_softc(dev);

	HDA_BOOTHVERBOSE(
		device_printf(dev, "Suspend...\n");
	);
	bus_generic_suspend(dev);

	hdac_lock(sc);
	HDA_BOOTHVERBOSE(
		device_printf(dev, "Reset controller...\n");
	);
	callout_stop(&sc->poll_callout);
	hdac_reset(sc, 0);
	hdac_unlock(sc);
	callout_drain(&sc->poll_callout);
	taskqueue_drain(taskqueue_thread, &sc->unsolq_task);
	HDA_BOOTHVERBOSE(
		device_printf(dev, "Suspend done\n");
	);
	return (0);
}

/****************************************************************************
 * int hdac_resume(device_t)
 *
 * Powerup and restore HDA bus and codecs state.
 ****************************************************************************/
static int
hdac_resume(device_t dev)
{
	struct hdac_softc *sc = device_get_softc(dev);
	int error;

	HDA_BOOTHVERBOSE(
		device_printf(dev, "Resume...\n");
	);
	hdac_lock(sc);

	/* Quiesce everything */
	HDA_BOOTHVERBOSE(
		device_printf(dev, "Reset controller...\n");
	);
	hdac_reset(sc, 1);

	/* Initialize the CORB and RIRB */
	hdac_corb_init(sc);
	hdac_rirb_init(sc);

	HDA_BOOTHVERBOSE(
		device_printf(dev, "Starting CORB Engine...\n");
	);
	hdac_corb_start(sc);
	HDA_BOOTHVERBOSE(
		device_printf(dev, "Starting RIRB Engine...\n");
	);
	hdac_rirb_start(sc);
	HDA_BOOTHVERBOSE(
		device_printf(dev, "Enabling controller interrupt...\n");
	);
	HDAC_WRITE_4(&sc->mem, HDAC_GCTL, HDAC_READ_4(&sc->mem, HDAC_GCTL) |
	    HDAC_GCTL_UNSOL);
	HDAC_WRITE_4(&sc->mem, HDAC_INTCTL, HDAC_INTCTL_CIE | HDAC_INTCTL_GIE);
	DELAY(1000);
	hdac_poll_reinit(sc);
	hdac_unlock(sc);

	error = bus_generic_resume(dev);
	HDA_BOOTHVERBOSE(
		device_printf(dev, "Resume done\n");
	);
	return (error);
}

/****************************************************************************
 * int hdac_detach(device_t)
 *
 * Detach and free up resources utilized by the hdac device.
 ****************************************************************************/
static int
hdac_detach(device_t dev)
{
	struct hdac_softc *sc = device_get_softc(dev);
	device_t *devlist;
	int cad, i, devcount, error;

	if ((error = device_get_children(dev, &devlist, &devcount)) != 0)
		return (error);
	for (i = 0; i < devcount; i++) {
		cad = (intptr_t)device_get_ivars(devlist[i]);
		if ((error = device_delete_child(dev, devlist[i])) != 0) {
			free(devlist, M_TEMP);
			return (error);
		}
		sc->codecs[cad].dev = NULL;
	}
	free(devlist, M_TEMP);

	hdac_lock(sc);
	hdac_reset(sc, 0);
	hdac_unlock(sc);
	taskqueue_drain(taskqueue_thread, &sc->unsolq_task);
	hdac_irq_free(sc);

	for (i = 0; i < sc->num_ss; i++)
		hdac_dma_free(sc, &sc->streams[i].bdl);
	free(sc->streams, M_HDAC);
	hdac_dma_free(sc, &sc->pos_dma);
	hdac_dma_free(sc, &sc->rirb_dma);
	hdac_dma_free(sc, &sc->corb_dma);
	if (sc->chan_dmat != NULL) {
		bus_dma_tag_destroy(sc->chan_dmat);
		sc->chan_dmat = NULL;
	}
	hdac_mem_free(sc);
	snd_mtxfree(sc->lock);
	return (0);
}

static bus_dma_tag_t
hdac_get_dma_tag(device_t dev, device_t child)
{
	struct hdac_softc *sc = device_get_softc(dev);

	return (sc->chan_dmat);
}

static int
hdac_print_child(device_t dev, device_t child)
{
	int retval;

	retval = bus_print_child_header(dev, child);
	retval += printf(" at cad %d",
	    (int)(intptr_t)device_get_ivars(child));
	retval += bus_print_child_footer(dev, child);

	return (retval);
}

static int
hdac_child_location_str(device_t dev, device_t child, char *buf,
    size_t buflen)
{

	snprintf(buf, buflen, "cad=%d",
	    (int)(intptr_t)device_get_ivars(child));
	return (0);
}

static int
hdac_child_pnpinfo_str_method(device_t dev, device_t child, char *buf,
    size_t buflen)
{
	struct hdac_softc *sc = device_get_softc(dev);
	nid_t cad = (uintptr_t)device_get_ivars(child);

	snprintf(buf, buflen, "vendor=0x%04x device=0x%04x revision=0x%02x "
	    "stepping=0x%02x",
	    sc->codecs[cad].vendor_id, sc->codecs[cad].device_id,
	    sc->codecs[cad].revision_id, sc->codecs[cad].stepping_id);
	return (0);
}

static int
hdac_read_ivar(device_t dev, device_t child, int which, uintptr_t *result)
{
	struct hdac_softc *sc = device_get_softc(dev);
	nid_t cad = (uintptr_t)device_get_ivars(child);

	switch (which) {
	case HDA_IVAR_CODEC_ID:
		*result = cad;
		break;
	case HDA_IVAR_VENDOR_ID:
		*result = sc->codecs[cad].vendor_id;
		break;
	case HDA_IVAR_DEVICE_ID:
		*result = sc->codecs[cad].device_id;
		break;
	case HDA_IVAR_REVISION_ID:
		*result = sc->codecs[cad].revision_id;
		break;
	case HDA_IVAR_STEPPING_ID:
		*result = sc->codecs[cad].stepping_id;
		break;
	case HDA_IVAR_SUBVENDOR_ID:
		*result = pci_get_subvendor(dev);
		break;
	case HDA_IVAR_SUBDEVICE_ID:
		*result = pci_get_subdevice(dev);
		break;
	case HDA_IVAR_DMA_NOCACHE:
		*result = (sc->flags & HDAC_F_DMA_NOCACHE) != 0;
		break;
	case HDA_IVAR_STRIPES_MASK:
		*result = (1 << (1 << sc->num_sdo)) - 1;
		break;
	default:
		return (ENOENT);
	}
	return (0);
}

static struct mtx *
hdac_get_mtx(device_t dev, device_t child)
{
	struct hdac_softc *sc = device_get_softc(dev);

	return (sc->lock);
}

static uint32_t
hdac_codec_command(device_t dev, device_t child, uint32_t verb)
{

	return (hdac_send_command(device_get_softc(dev),
	    (intptr_t)device_get_ivars(child), verb));
}

static int
hdac_find_stream(struct hdac_softc *sc, int dir, int stream)
{
	int i, ss;

	ss = -1;
	/* Allocate ISS/OSS first. */
	if (dir == 0) {
		for (i = 0; i < sc->num_iss; i++) {
			if (sc->streams[i].stream == stream) {
				ss = i;
				break;
			}
		}
	} else {
		for (i = 0; i < sc->num_oss; i++) {
			if (sc->streams[i + sc->num_iss].stream == stream) {
				ss = i + sc->num_iss;
				break;
			}
		}
	}
	/* Fallback to BSS. */
	if (ss == -1) {
		for (i = 0; i < sc->num_bss; i++) {
			if (sc->streams[i + sc->num_iss + sc->num_oss].stream
			    == stream) {
				ss = i + sc->num_iss + sc->num_oss;
				break;
			}
		}
	}
	return (ss);
}

static int
hdac_stream_alloc(device_t dev, device_t child, int dir, int format, int stripe,
    uint32_t **dmapos)
{
	struct hdac_softc *sc = device_get_softc(dev);
	nid_t cad = (uintptr_t)device_get_ivars(child);
	int stream, ss, bw, maxbw, prevbw;

	/* Look for empty stream. */
	ss = hdac_find_stream(sc, dir, 0);

	/* Return if found nothing. */
	if (ss < 0)
		return (0);

	/* Check bus bandwidth. */
	bw = hdac_bdata_rate(format, dir);
	if (dir == 1) {
		bw *= 1 << (sc->num_sdo - stripe);
		prevbw = sc->sdo_bw_used;
		maxbw = 48000 * 960 * (1 << sc->num_sdo);
	} else {
		prevbw = sc->codecs[cad].sdi_bw_used;
		maxbw = 48000 * 464;
	}
	HDA_BOOTHVERBOSE(
		device_printf(dev, "%dKbps of %dKbps bandwidth used%s\n",
		    (bw + prevbw) / 1000, maxbw / 1000,
		    bw + prevbw > maxbw ? " -- OVERFLOW!" : "");
	);
	if (bw + prevbw > maxbw)
		return (0);
	if (dir == 1)
		sc->sdo_bw_used += bw;
	else
		sc->codecs[cad].sdi_bw_used += bw;

	/* Allocate stream number */
	if (ss >= sc->num_iss + sc->num_oss)
		stream = 15 - (ss - sc->num_iss - sc->num_oss);
	else if (ss >= sc->num_iss)
		stream = ss - sc->num_iss + 1;
	else
		stream = ss + 1;

	sc->streams[ss].dev = child;
	sc->streams[ss].dir = dir;
	sc->streams[ss].stream = stream;
	sc->streams[ss].bw = bw;
	sc->streams[ss].format = format;
	sc->streams[ss].stripe = stripe;
	if (dmapos != NULL) {
		if (sc->pos_dma.dma_vaddr != NULL)
			*dmapos = (uint32_t *)(sc->pos_dma.dma_vaddr + ss * 8);
		else
			*dmapos = NULL;
	}
	return (stream);
}

static void
hdac_stream_free(device_t dev, device_t child, int dir, int stream)
{
	struct hdac_softc *sc = device_get_softc(dev);
	nid_t cad = (uintptr_t)device_get_ivars(child);
	int ss;

	ss = hdac_find_stream(sc, dir, stream);
	KASSERT(ss >= 0,
	    ("Free for not allocated stream (%d/%d)\n", dir, stream));
	if (dir == 1)
		sc->sdo_bw_used -= sc->streams[ss].bw;
	else
		sc->codecs[cad].sdi_bw_used -= sc->streams[ss].bw;
	sc->streams[ss].stream = 0;
	sc->streams[ss].dev = NULL;
}

static int
hdac_stream_start(device_t dev, device_t child,
    int dir, int stream, bus_addr_t buf, int blksz, int blkcnt)
{
	struct hdac_softc *sc = device_get_softc(dev);
	struct hdac_bdle *bdle;
	uint64_t addr;
	int i, ss, off;
	uint32_t ctl;

	ss = hdac_find_stream(sc, dir, stream);
	KASSERT(ss >= 0,
	    ("Start for not allocated stream (%d/%d)\n", dir, stream));

	addr = (uint64_t)buf;
	bdle = (struct hdac_bdle *)sc->streams[ss].bdl.dma_vaddr;
	for (i = 0; i < blkcnt; i++, bdle++) {
		bdle->addrl = htole32((uint32_t)addr);
		bdle->addrh = htole32((uint32_t)(addr >> 32));
		bdle->len = htole32(blksz);
		bdle->ioc = htole32(1);
		addr += blksz;
	}

	bus_dmamap_sync(sc->streams[ss].bdl.dma_tag,
	    sc->streams[ss].bdl.dma_map, BUS_DMASYNC_PREWRITE);

	off = ss << 5;
	HDAC_WRITE_4(&sc->mem, off + HDAC_SDCBL, blksz * blkcnt);
	HDAC_WRITE_2(&sc->mem, off + HDAC_SDLVI, blkcnt - 1);
	addr = sc->streams[ss].bdl.dma_paddr;
	HDAC_WRITE_4(&sc->mem, off + HDAC_SDBDPL, (uint32_t)addr);
	HDAC_WRITE_4(&sc->mem, off + HDAC_SDBDPU, (uint32_t)(addr >> 32));

	ctl = HDAC_READ_1(&sc->mem, off + HDAC_SDCTL2);
	if (dir)
		ctl |= HDAC_SDCTL2_DIR;
	else
		ctl &= ~HDAC_SDCTL2_DIR;
	ctl &= ~HDAC_SDCTL2_STRM_MASK;
	ctl |= stream << HDAC_SDCTL2_STRM_SHIFT;
	ctl &= ~HDAC_SDCTL2_STRIPE_MASK;
	ctl |= sc->streams[ss].stripe << HDAC_SDCTL2_STRIPE_SHIFT;
	HDAC_WRITE_1(&sc->mem, off + HDAC_SDCTL2, ctl);

	HDAC_WRITE_2(&sc->mem, off + HDAC_SDFMT, sc->streams[ss].format);

	ctl = HDAC_READ_4(&sc->mem, HDAC_INTCTL);
	ctl |= 1 << ss;
	HDAC_WRITE_4(&sc->mem, HDAC_INTCTL, ctl);

	HDAC_WRITE_1(&sc->mem, off + HDAC_SDSTS,
	    HDAC_SDSTS_DESE | HDAC_SDSTS_FIFOE | HDAC_SDSTS_BCIS);
	ctl = HDAC_READ_1(&sc->mem, off + HDAC_SDCTL0);
	ctl |= HDAC_SDCTL_IOCE | HDAC_SDCTL_FEIE | HDAC_SDCTL_DEIE |
	    HDAC_SDCTL_RUN;
	HDAC_WRITE_1(&sc->mem, off + HDAC_SDCTL0, ctl);

	sc->streams[ss].blksz = blksz;
	sc->streams[ss].running = 1;
	hdac_poll_reinit(sc);
	return (0);
}

static void
hdac_stream_stop(device_t dev, device_t child, int dir, int stream)
{
	struct hdac_softc *sc = device_get_softc(dev);
	int ss, off;
	uint32_t ctl;

	ss = hdac_find_stream(sc, dir, stream);
	KASSERT(ss >= 0,
	    ("Stop for not allocated stream (%d/%d)\n", dir, stream));

	bus_dmamap_sync(sc->streams[ss].bdl.dma_tag,
	    sc->streams[ss].bdl.dma_map, BUS_DMASYNC_POSTWRITE);

	off = ss << 5;
	ctl = HDAC_READ_1(&sc->mem, off + HDAC_SDCTL0);
	ctl &= ~(HDAC_SDCTL_IOCE | HDAC_SDCTL_FEIE | HDAC_SDCTL_DEIE |
	    HDAC_SDCTL_RUN);
	HDAC_WRITE_1(&sc->mem, off + HDAC_SDCTL0, ctl);

	ctl = HDAC_READ_4(&sc->mem, HDAC_INTCTL);
	ctl &= ~(1 << ss);
	HDAC_WRITE_4(&sc->mem, HDAC_INTCTL, ctl);

	sc->streams[ss].running = 0;
	hdac_poll_reinit(sc);
}

static void
hdac_stream_reset(device_t dev, device_t child, int dir, int stream)
{
	struct hdac_softc *sc = device_get_softc(dev);
	int timeout = 1000;
	int to = timeout;
	int ss, off;
	uint32_t ctl;

	ss = hdac_find_stream(sc, dir, stream);
	KASSERT(ss >= 0,
	    ("Reset for not allocated stream (%d/%d)\n", dir, stream));

	off = ss << 5;
	ctl = HDAC_READ_1(&sc->mem, off + HDAC_SDCTL0);
	ctl |= HDAC_SDCTL_SRST;
	HDAC_WRITE_1(&sc->mem, off + HDAC_SDCTL0, ctl);
	do {
		ctl = HDAC_READ_1(&sc->mem, off + HDAC_SDCTL0);
		if (ctl & HDAC_SDCTL_SRST)
			break;
		DELAY(10);
	} while (--to);
	if (!(ctl & HDAC_SDCTL_SRST))
		device_printf(dev, "Reset setting timeout\n");
	ctl &= ~HDAC_SDCTL_SRST;
	HDAC_WRITE_1(&sc->mem, off + HDAC_SDCTL0, ctl);
	to = timeout;
	do {
		ctl = HDAC_READ_1(&sc->mem, off + HDAC_SDCTL0);
		if (!(ctl & HDAC_SDCTL_SRST))
			break;
		DELAY(10);
	} while (--to);
	if (ctl & HDAC_SDCTL_SRST)
		device_printf(dev, "Reset timeout!\n");
}

static uint32_t
hdac_stream_getptr(device_t dev, device_t child, int dir, int stream)
{
	struct hdac_softc *sc = device_get_softc(dev);
	int ss, off;

	ss = hdac_find_stream(sc, dir, stream);
	KASSERT(ss >= 0,
	    ("Reset for not allocated stream (%d/%d)\n", dir, stream));

	off = ss << 5;
	return (HDAC_READ_4(&sc->mem, off + HDAC_SDLPIB));
}

static int
hdac_unsol_alloc(device_t dev, device_t child, int tag)
{
	struct hdac_softc *sc = device_get_softc(dev);

	sc->unsol_registered++;
	hdac_poll_reinit(sc);
	return (tag);
}

static void
hdac_unsol_free(device_t dev, device_t child, int tag)
{
	struct hdac_softc *sc = device_get_softc(dev);

	sc->unsol_registered--;
	hdac_poll_reinit(sc);
}

static device_method_t hdac_methods[] = {
	/* device interface */
	DEVMETHOD(device_probe,		hdac_probe),
	DEVMETHOD(device_attach,	hdac_attach),
	DEVMETHOD(device_detach,	hdac_detach),
	DEVMETHOD(device_suspend,	hdac_suspend),
	DEVMETHOD(device_resume,	hdac_resume),
	/* Bus interface */
	DEVMETHOD(bus_get_dma_tag,	hdac_get_dma_tag),
	DEVMETHOD(bus_print_child,	hdac_print_child),
	DEVMETHOD(bus_child_location_str, hdac_child_location_str),
	DEVMETHOD(bus_child_pnpinfo_str, hdac_child_pnpinfo_str_method),
	DEVMETHOD(bus_read_ivar,	hdac_read_ivar),
	DEVMETHOD(hdac_get_mtx,		hdac_get_mtx),
	DEVMETHOD(hdac_codec_command,	hdac_codec_command),
	DEVMETHOD(hdac_stream_alloc,	hdac_stream_alloc),
	DEVMETHOD(hdac_stream_free,	hdac_stream_free),
	DEVMETHOD(hdac_stream_start,	hdac_stream_start),
	DEVMETHOD(hdac_stream_stop,	hdac_stream_stop),
	DEVMETHOD(hdac_stream_reset,	hdac_stream_reset),
	DEVMETHOD(hdac_stream_getptr,	hdac_stream_getptr),
	DEVMETHOD(hdac_unsol_alloc,	hdac_unsol_alloc),
	DEVMETHOD(hdac_unsol_free,	hdac_unsol_free),
	DEVMETHOD_END
};

static driver_t hdac_driver = {
	"hdac",
	hdac_methods,
	sizeof(struct hdac_softc),
};

static devclass_t hdac_devclass;

DRIVER_MODULE(snd_hda, pci, hdac_driver, hdac_devclass, NULL, NULL);
