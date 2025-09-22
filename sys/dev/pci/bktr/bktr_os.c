/*	$OpenBSD: bktr_os.c,v 1.37 2022/07/02 08:50:42 visa Exp $	*/
/* $FreeBSD: src/sys/dev/bktr/bktr_os.c,v 1.20 2000/10/20 08:16:53 roger Exp $ */

/*
 * This is part of the Driver for Video Capture Cards (Frame grabbers)
 * and TV Tuner cards using the Brooktree Bt848, Bt848A, Bt849A, Bt878, Bt879
 * chipset.
 * Copyright Roger Hardiman and Amancio Hasty.
 *
 * bktr_os : This has all the Operating System dependant code,
 *             probe/attach and open/close/ioctl/read/mmap
 *             memory allocation
 *             PCI bus interfacing
 *             
 *
 */

/*
 * 1. Redistributions of source code must retain the 
 * Copyright (c) 1997 Amancio Hasty, 1999 Roger Hardiman
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Amancio Hasty and
 *      Roger Hardiman
 * 4. The name of the author may not be used to endorse or promote products 
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.	IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#define FIFO_RISC_DISABLED      0
#define ALL_INTS_DISABLED       0

#include "radio.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/kernel.h>
#include <sys/signalvar.h>
#include <sys/mman.h>
#include <sys/vnode.h>
#if NRADIO > 0
#include <sys/radioio.h>
#include <dev/radio_if.h>
#endif

#include <uvm/uvm_extern.h>

#include <sys/device.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcidevs.h>

#ifdef BKTR_DEBUG
int bktr_debug = 1;
#define DPR(x)	(bktr_debug ? printf x : 0)
#else
#define DPR(x)
#endif

#include <dev/ic/bt8xx.h>	/* OpenBSD location for .h files */
#include <dev/pci/bktr/bktr_reg.h>
#include <dev/pci/bktr/bktr_tuner.h>
#include <dev/pci/bktr/bktr_audio.h>
#include <dev/pci/bktr/bktr_core.h>
#include <dev/pci/bktr/bktr_os.h>

#define IPL_VIDEO       IPL_BIO         /* XXX */

static	int		bktr_intr(void *arg) { return common_bktr_intr(arg); }

#define bktr_open       bktropen
#define bktr_close      bktrclose
#define bktr_read       bktrread
#define bktr_write      bktrwrite
#define bktr_ioctl      bktrioctl
#define bktr_mmap       bktrmmap

int	bktr_open(dev_t, int, int, struct proc *);
int	bktr_close(dev_t, int, int, struct proc *);
int	bktr_read(dev_t, struct uio *, int);
int	bktr_write(dev_t, struct uio *, int);
int	bktr_ioctl(dev_t, ioctl_cmd_t, caddr_t, int, struct proc *);
paddr_t	bktr_mmap(dev_t, off_t, int);

static int      bktr_probe(struct device *, void *, void *);
static void     bktr_attach(struct device *, struct device *, void *);

const struct cfattach bktr_ca = {
        sizeof(struct bktr_softc), bktr_probe, bktr_attach
};

struct cfdriver bktr_cd = {
        NULL, "bktr", DV_DULL
};

#if NRADIO > 0
/* for radio(4) */
int	bktr_get_info(void *, struct radio_info *);
int	bktr_set_info(void *, struct radio_info *);

const struct radio_hw_if bktr_hw_if = {
	NULL,	/* open */
	NULL,	/* close */
	bktr_get_info,
	bktr_set_info,
	NULL	/* search */
};
#endif

int
bktr_probe(struct device *parent, void *match, void *aux)
{
        struct pci_attach_args *pa = aux;

        if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_BROOKTREE &&
            (PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_BROOKTREE_BT848 ||
             PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_BROOKTREE_BT849 ||
             PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_BROOKTREE_BT878 ||
             PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_BROOKTREE_BT879))
                return 1;

        return 0;
}


/*
 * the attach routine.
 */
static void
bktr_attach(struct device *parent, struct device *self, void *aux)
{
	bktr_ptr_t	bktr;
	u_int		latency;
	u_int		fun;
	unsigned int	rev;
	struct pci_attach_args *pa = aux;
	pci_intr_handle_t ih;
	const char *intrstr;
	int retval;
	int unit;

	bktr = (bktr_ptr_t)self;
	unit = bktr->bktr_dev.dv_unit;
        bktr->dmat = pa->pa_dmat;

	/* Enable Back-to-Back
	   XXX: check if all old DMA is stopped first (e.g. after warm
	   boot) */
	fun = pci_conf_read(pa->pa_pc, pa->pa_tag, PCI_COMMAND_STATUS_REG);
	DPR((" fun=%b", fun, PCI_COMMAND_STATUS_BITS));
	pci_conf_write(pa->pa_pc, pa->pa_tag, PCI_COMMAND_STATUS_REG,
	    fun | PCI_COMMAND_BACKTOBACK_ENABLE);

	/*
	 * map memory
	 */
	retval = pci_mapreg_map(pa, PCI_MAPREG_START, PCI_MAPREG_TYPE_MEM |
	    PCI_MAPREG_MEM_TYPE_32BIT, 0, &bktr->memt, &bktr->memh, NULL,
	    &bktr->obmemsz, 0);
	DPR(("pci_mapreg_map: memt %lx, memh %lx, size %x\n",
	     bktr->memt, bktr->memh, bktr->obmemsz));
	if (retval) {
		printf("%s: can't map mem space\n", bktr_name(bktr));
		return;
	}

	/*
	 * Disable the brooktree device
	 */
	OUTL(bktr, BKTR_INT_MASK, ALL_INTS_DISABLED);
	OUTW(bktr, BKTR_GPIO_DMA_CTL, FIFO_RISC_DISABLED);
	
	/*
	 * map interrupt
	 */
	if (pci_intr_map(pa, &ih)) {
		printf("%s: can't map interrupt\n",
		       bktr_name(bktr));
		return;
	}
	intrstr = pci_intr_string(pa->pa_pc, ih);
	bktr->ih = pci_intr_establish(pa->pa_pc, ih, IPL_VIDEO,
	    bktr_intr, bktr, bktr->bktr_dev.dv_xname);
	if (bktr->ih == NULL) {
		printf("%s: can't establish interrupt",
		       bktr_name(bktr));
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		return;
	}
	if (intrstr != NULL)
		printf(": %s\n", intrstr);
	
/*
 * PCI latency timer.  32 is a good value for 4 bus mastering slots, if
 * you have more than four, then 16 would probably be a better value.
 */
#ifndef BROOKTREE_DEF_LATENCY_VALUE
#define BROOKTREE_DEF_LATENCY_VALUE	0x10
#endif
	latency = pci_conf_read(pa->pa_pc, pa->pa_tag, PCI_LATENCY_TIMER);
	latency = (latency >> 8) & 0xff;

	if (!latency) {
		if (bootverbose) {
			printf("%s: PCI bus latency was 0 changing to %d",
			       bktr_name(bktr), BROOKTREE_DEF_LATENCY_VALUE);
		}
		latency = BROOKTREE_DEF_LATENCY_VALUE;
		pci_conf_write(pa->pa_pc, pa->pa_tag, 
			       PCI_LATENCY_TIMER, latency<<8);
	}


	/* read the pci id and determine the card type */
	fun = pci_conf_read(pa->pa_pc, pa->pa_tag, PCI_ID_REG);
        rev = PCI_REVISION(pci_conf_read(pa->pa_pc, pa->pa_tag, PCI_CLASS_REG));

	common_bktr_attach(bktr, unit, fun, rev);

#if NRADIO > 0
	if (bktr->card.tuner->pllControl[3] != 0x00)
		radio_attach_mi(&bktr_hw_if, bktr, &bktr->bktr_dev);
#endif
}


/*
 * Special Memory Allocation
 */
vaddr_t
get_bktr_mem(bktr_ptr_t bktr, bus_dmamap_t *dmapp, unsigned int size)
{
        bus_dma_tag_t dmat = bktr->dmat;
        bus_dma_segment_t seg;
        bus_size_t align;
        int rseg;
        caddr_t kva;

        /*
         * Allocate a DMA area
         */
        align = 1 << 24;
        if (bus_dmamem_alloc(dmat, size, align, 0, &seg, 1,
                             &rseg, BUS_DMA_NOWAIT)) {
                align = PAGE_SIZE;
                if (bus_dmamem_alloc(dmat, size, align, 0, &seg, 1,
                                     &rseg, BUS_DMA_NOWAIT)) {
                        printf("%s: Unable to dmamem_alloc of %d bytes\n",
			       bktr_name(bktr), size);
                        return 0;
                }
        }
        if (bus_dmamem_map(dmat, &seg, rseg, size,
                           &kva, BUS_DMA_NOWAIT|BUS_DMA_COHERENT)) {
                printf("%s: Unable to dmamem_map of %d bytes\n",
                        bktr_name(bktr), size);
                bus_dmamem_free(dmat, &seg, rseg);
                return 0;
        }
        /*
         * Create and locd the DMA map for the DMA area
         */
        if (bus_dmamap_create(dmat, size, 1, size, 0, BUS_DMA_NOWAIT, dmapp)) {
                printf("%s: Unable to dmamap_create of %d bytes\n",
                        bktr_name(bktr), size);
                bus_dmamem_unmap(dmat, kva, size);
                bus_dmamem_free(dmat, &seg, rseg);
                return 0;
        }
        if (bus_dmamap_load(dmat, *dmapp, kva, size, NULL, BUS_DMA_NOWAIT)) {
                printf("%s: Unable to dmamap_load of %d bytes\n",
                        bktr_name(bktr), size);
                bus_dmamem_unmap(dmat, kva, size);
                bus_dmamem_free(dmat, &seg, rseg);
                bus_dmamap_destroy(dmat, *dmapp);
                return 0;
        }
        return (vaddr_t)kva;
}

void
free_bktr_mem(bktr_ptr_t bktr, bus_dmamap_t dmap, vaddr_t kva)
{
        bus_dma_tag_t dmat = bktr->dmat;

        bus_dmamem_unmap(dmat, (caddr_t)kva, dmap->dm_mapsize);
        bus_dmamem_free(dmat, dmap->dm_segs, 1);
        bus_dmamap_destroy(dmat, dmap);
}


/*---------------------------------------------------------
**
**	BrookTree 848 character device driver routines
**
**---------------------------------------------------------
*/


#define VIDEO_DEV	0x00
#define TUNER_DEV	0x01
#define VBI_DEV		0x02

#define	UNIT(x)		((minor((x)) < 16) ? minor((x)) : ((minor((x)) - 16) / 2))
#define	FUNCTION(x)	((minor((x)) < 16) ? VIDEO_DEV : ((minor((x)) & 0x1) ? \
    VBI_DEV : TUNER_DEV))

/*
 * 
 */
int
bktr_open(dev_t dev, int flags, int fmt, struct proc *p)
{
	bktr_ptr_t	bktr;
	int		unit;

	unit = UNIT(dev);

	/* unit out of range */
	if ((unit >= bktr_cd.cd_ndevs) || (bktr_cd.cd_devs[unit] == NULL))
		return(ENXIO);

	bktr = bktr_cd.cd_devs[unit];

	if (!(bktr->flags & METEOR_INITIALIZED)) /* device not found */
		return(ENXIO);	

	switch (FUNCTION(dev)) {
	case VIDEO_DEV:
		return(video_open(bktr));
	case TUNER_DEV:
		return(tuner_open(bktr));
	case VBI_DEV:
		return(vbi_open(bktr));
	}

	return(ENXIO);
}


/*
 * 
 */
int
bktr_close(dev_t dev, int flags, int fmt, struct proc *p)
{
	bktr_ptr_t	bktr;
	int		unit;

	unit = UNIT(dev);

	bktr = bktr_cd.cd_devs[unit];

	switch (FUNCTION(dev)) {
	case VIDEO_DEV:
		return(video_close(bktr));
	case TUNER_DEV:
		return(tuner_close(bktr));
	case VBI_DEV:
		return(vbi_close(bktr));
	}

	return(ENXIO);
}

/*
 * 
 */
int
bktr_read(dev_t dev, struct uio *uio, int ioflag)
{
	bktr_ptr_t	bktr;
	int		unit;
	
	unit = UNIT(dev);

	bktr = bktr_cd.cd_devs[unit];

	switch (FUNCTION(dev)) {
	case VIDEO_DEV:
		return(video_read(bktr, unit, dev, uio));
	case VBI_DEV:
		return(vbi_read(bktr, uio, ioflag));
	}

        return(ENXIO);
}


/*
 * 
 */
int
bktr_write(dev_t dev, struct uio *uio, int ioflag)
{
	/* operation not supported */
	return(EOPNOTSUPP);
}

/*
 * 
 */
int
bktr_ioctl(dev_t dev, ioctl_cmd_t cmd, caddr_t arg, int flag, struct proc* pr)
{
	bktr_ptr_t	bktr;
	int		unit;

	unit = UNIT(dev);

	bktr = bktr_cd.cd_devs[unit];

	if (bktr->bigbuf == 0)	/* no frame buffer allocated (ioctl failed) */
		return(ENOMEM);

	switch (FUNCTION(dev)) {
	case VIDEO_DEV:
		return(video_ioctl(bktr, unit, cmd, arg, pr));
	case TUNER_DEV:
		return(tuner_ioctl(bktr, unit, cmd, arg, pr));
	}

	return(ENXIO);
}

/*
 * 
 */
paddr_t
bktr_mmap(dev_t dev, off_t offset, int nprot)
{
	int		unit;
	bktr_ptr_t	bktr;

	unit = UNIT(dev);

	if (FUNCTION(dev) > 0)	/* only allow mmap on /dev/bktr[n] */
		return(-1);

	bktr = bktr_cd.cd_devs[unit];

	if (offset < 0)
		return(-1);

	if (offset >= bktr->alloc_pages * PAGE_SIZE)
		return(-1);

	return (bus_dmamem_mmap(bktr->dmat, bktr->dm_mem->dm_segs, 1,
				offset, nprot, BUS_DMA_WAITOK));
}

#if NRADIO > 0
int
bktr_set_info(void *v, struct radio_info *ri)
{
	struct bktr_softc *sc = v;
	struct TVTUNER *tv = &sc->tuner;
	u_int32_t freq;
	u_int32_t chan;

	if (ri->mute) {
		/* mute the audio stream by switching the mux */
		set_audio(sc, AUDIO_MUTE);
	} else {
		/* unmute the audio stream */
		set_audio(sc, AUDIO_UNMUTE);
		init_audio_devices(sc);
	}

	set_audio(sc, AUDIO_INTERN);	/* use internal audio */
	temp_mute(sc, TRUE);

	if (ri->tuner_mode == RADIO_TUNER_MODE_TV) {
		if (ri->chan) {
			if (ri->chan < MIN_TV_CHAN)
				ri->chan = MIN_TV_CHAN;
			if (ri->chan > MAX_TV_CHAN)
				ri->chan = MAX_TV_CHAN;

			chan = ri->chan;
			ri->chan = tv_channel(sc, chan);
			tv->tuner_mode = BT848_TUNER_MODE_TV;
		} else {
			ri->chan = tv->channel;
		}
	} else {
		if (ri->freq) {
			if (ri->freq < MIN_FM_FREQ)
				ri->freq = MIN_FM_FREQ;
			if (ri->freq > MAX_FM_FREQ)
				ri->freq = MAX_FM_FREQ;

			freq = ri->freq / 10;
			ri->freq = tv_freq(sc, freq, FM_RADIO_FREQUENCY) * 10;
			tv->tuner_mode = BT848_TUNER_MODE_RADIO;
		} else {
			ri->freq = tv->frequency;
		}
	}

	if (ri->chnlset >= CHNLSET_MIN && ri->chnlset <= CHNLSET_MAX)
		tv->chnlset = ri->chnlset;
	else
		tv->chnlset = DEFAULT_CHNLSET;
	
	temp_mute(sc, FALSE);

	return (0);
}

int
bktr_get_info(void *v, struct radio_info *ri)
{
	struct bktr_softc *sc = v;
	struct TVTUNER *tv = &sc->tuner;
	int status;

	status = get_tuner_status(sc);

#define	STATUSBIT_STEREO	0x10
	ri->mute = (int)sc->audio_mute_state ? 1 : 0;
	ri->caps = RADIO_CAPS_DETECT_STEREO | RADIO_CAPS_HW_AFC;
	ri->info = (status & STATUSBIT_STEREO) ? RADIO_INFO_STEREO : 0;

	/* not yet supported */
	ri->volume = ri->rfreq = ri->lock = 0;

	switch (tv->tuner_mode) {
	case BT848_TUNER_MODE_TV:
		ri->tuner_mode = RADIO_TUNER_MODE_TV;
		ri->freq = tv->frequency * 1000 / 16;
		break;
	case BT848_TUNER_MODE_RADIO:
		ri->tuner_mode = RADIO_TUNER_MODE_RADIO;
		ri->freq = tv->frequency * 10;
		break;
	}

	/*
	 * The field ri->stereo is used to forcible switch to
	 * mono/stereo, not as an indicator of received signal quality.
	 * The ri->info is for that purpose.
	 */
	ri->stereo = 1; /* Can't switch to mono, always stereo */
	
	ri->chan = tv->channel;
	ri->chnlset = tv->chnlset;

	return (0);
}
#endif /* NRADIO */
