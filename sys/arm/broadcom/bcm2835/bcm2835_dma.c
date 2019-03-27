/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Daisuke Aoyama <aoyama@peach.ne.jp>
 * Copyright (c) 2013 Oleksandr Tymoshenko <gonzo@bluezbox.com>
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/queue.h>
#include <sys/resource.h>
#include <sys/rman.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <machine/bus.h>

#include "bcm2835_dma.h"
#include "bcm2835_vcbus.h"

#define	MAX_REG			9

/* private flags */
#define	BCM_DMA_CH_USED		0x00000001
#define	BCM_DMA_CH_FREE		0x40000000
#define	BCM_DMA_CH_UNMAP	0x80000000

/* Register Map (4.2.1.2) */
#define	BCM_DMA_CS(n)		(0x100*(n) + 0x00)
#define		CS_ACTIVE		(1 <<  0)
#define		CS_END			(1 <<  1)
#define		CS_INT			(1 <<  2)
#define		CS_DREQ			(1 <<  3)
#define		CS_ISPAUSED		(1 <<  4)
#define		CS_ISHELD		(1 <<  5)
#define		CS_ISWAIT		(1 <<  6)
#define		CS_ERR			(1 <<  8)
#define		CS_WAITWRT		(1 << 28)
#define		CS_DISDBG		(1 << 29)
#define		CS_ABORT		(1 << 30)
#define		CS_RESET		(1U << 31)
#define	BCM_DMA_CBADDR(n)	(0x100*(n) + 0x04)
#define	BCM_DMA_INFO(n)		(0x100*(n) + 0x08)
#define		INFO_INT_EN		(1 << 0)
#define		INFO_TDMODE		(1 << 1)
#define		INFO_WAIT_RESP		(1 << 3)
#define		INFO_D_INC		(1 << 4)
#define		INFO_D_WIDTH		(1 << 5)
#define		INFO_D_DREQ		(1 << 6)
#define		INFO_S_INC		(1 << 8)
#define		INFO_S_WIDTH		(1 << 9)
#define		INFO_S_DREQ		(1 << 10)
#define		INFO_WAITS_SHIFT	(21)
#define		INFO_PERMAP_SHIFT	(16)
#define		INFO_PERMAP_MASK	(0x1f << INFO_PERMAP_SHIFT)

#define	BCM_DMA_SRC(n)		(0x100*(n) + 0x0C)
#define	BCM_DMA_DST(n)		(0x100*(n) + 0x10)
#define	BCM_DMA_LEN(n)		(0x100*(n) + 0x14)
#define	BCM_DMA_STRIDE(n)	(0x100*(n) + 0x18)
#define	BCM_DMA_CBNEXT(n)	(0x100*(n) + 0x1C)
#define	BCM_DMA_DEBUG(n)	(0x100*(n) + 0x20)
#define		DEBUG_ERROR_MASK	(7)

#define	BCM_DMA_INT_STATUS	0xfe0
#define	BCM_DMA_ENABLE		0xff0

/* relative offset from BCM_VC_DMA0_BASE (p.39) */
#define	BCM_DMA_CH(n)		(0x100*(n))

/* channels used by GPU */
#define	BCM_DMA_CH_BULK		0
#define	BCM_DMA_CH_FAST1	2
#define	BCM_DMA_CH_FAST2	3

#define	BCM_DMA_CH_GPU_MASK	((1 << BCM_DMA_CH_BULK) |	\
				 (1 << BCM_DMA_CH_FAST1) |	\
				 (1 << BCM_DMA_CH_FAST2))

/* DMA Control Block - 256bit aligned (p.40) */
struct bcm_dma_cb {
	uint32_t info;		/* Transfer Information */
	uint32_t src;		/* Source Address */
	uint32_t dst;		/* Destination Address */
	uint32_t len;		/* Transfer Length */
	uint32_t stride;	/* 2D Mode Stride */
	uint32_t next;		/* Next Control Block Address */
	uint32_t rsvd1;		/* Reserved */
	uint32_t rsvd2;		/* Reserved */
};

#ifdef DEBUG
static void bcm_dma_cb_dump(struct bcm_dma_cb *cb);
static void bcm_dma_reg_dump(int ch);
#endif

/* DMA channel private info */
struct bcm_dma_ch {
	int			ch;
	uint32_t		flags;
	struct bcm_dma_cb *	cb;
	uint32_t		vc_cb;
	bus_dmamap_t		dma_map;
	void 			(*intr_func)(int, void *);
	void *			intr_arg;
};

struct bcm_dma_softc {
	device_t		sc_dev;
	struct mtx		sc_mtx;
	struct resource *	sc_mem;
	struct resource *	sc_irq[BCM_DMA_CH_MAX];
	void *			sc_intrhand[BCM_DMA_CH_MAX];
	struct bcm_dma_ch	sc_dma_ch[BCM_DMA_CH_MAX];
	bus_dma_tag_t		sc_dma_tag;
};

static struct bcm_dma_softc *bcm_dma_sc = NULL;
static uint32_t bcm_dma_channel_mask;

static struct ofw_compat_data compat_data[] = {
	{"broadcom,bcm2835-dma",	1},
	{"brcm,bcm2835-dma",		1},
	{NULL,				0}
};

static void
bcm_dmamap_cb(void *arg, bus_dma_segment_t *segs,
	int nseg, int err)
{
        bus_addr_t *addr;

        if (err)
                return;

        addr = (bus_addr_t*)arg;
        *addr = PHYS_TO_VCBUS(segs[0].ds_addr);
}

static void
bcm_dma_reset(device_t dev, int ch)
{
	struct bcm_dma_softc *sc = device_get_softc(dev);
	struct bcm_dma_cb *cb;
	uint32_t cs;
	int count;

	if (ch < 0 || ch >= BCM_DMA_CH_MAX)
		return;

	cs = bus_read_4(sc->sc_mem, BCM_DMA_CS(ch));

	if (cs & CS_ACTIVE) {
		/* pause current task */
		bus_write_4(sc->sc_mem, BCM_DMA_CS(ch), 0);

		count = 1000;
		do {
			cs = bus_read_4(sc->sc_mem, BCM_DMA_CS(ch));
		} while (!(cs & CS_ISPAUSED) && (count-- > 0));

		if (!(cs & CS_ISPAUSED)) {
			device_printf(dev,
			    "Can't abort DMA transfer at channel %d\n", ch);
		}

		bus_write_4(sc->sc_mem, BCM_DMA_CBNEXT(ch), 0);

		/* Complete everything, clear interrupt */
		bus_write_4(sc->sc_mem, BCM_DMA_CS(ch),
		    CS_ABORT | CS_INT | CS_END| CS_ACTIVE);
	}

	/* clear control blocks */
	bus_write_4(sc->sc_mem, BCM_DMA_CBADDR(ch), 0);
	bus_write_4(sc->sc_mem, BCM_DMA_CBNEXT(ch), 0);

	/* Reset control block */
	cb = sc->sc_dma_ch[ch].cb;
	bzero(cb, sizeof(*cb));
	cb->info = INFO_WAIT_RESP;
}

static int
bcm_dma_init(device_t dev)
{
	struct bcm_dma_softc *sc = device_get_softc(dev);
	uint32_t reg;
	struct bcm_dma_ch *ch;
	void *cb_virt;
	vm_paddr_t cb_phys;
	int err;
	int i;

	/*
	 * Only channels set in bcm_dma_channel_mask can be controlled by us.
	 * The others are out of our control as well as the corresponding bits
	 * in both BCM_DMA_ENABLE and BCM_DMA_INT_STATUS global registers. As
	 * these registers are RW ones, there is no safe way how to write only
	 * the bits which can be controlled by us.
	 *
	 * Fortunately, after reset, all channels are enabled in BCM_DMA_ENABLE
	 * register and all statuses are cleared in BCM_DMA_INT_STATUS one.
	 * Not touching these registers is a trade off between correct
	 * initialization which does not count on anything and not messing up
	 * something we have no control over.
	 */
	reg = bus_read_4(sc->sc_mem, BCM_DMA_ENABLE);
	if ((reg & bcm_dma_channel_mask) != bcm_dma_channel_mask)
		device_printf(dev, "channels are not enabled\n");
	reg = bus_read_4(sc->sc_mem, BCM_DMA_INT_STATUS);
	if ((reg & bcm_dma_channel_mask) != 0)
		device_printf(dev, "statuses are not cleared\n");

	/* Allocate DMA chunks control blocks */
	/* p.40 of spec - control block should be 32-bit aligned */
	err = bus_dma_tag_create(bus_get_dma_tag(dev),
	    1, 0, BUS_SPACE_MAXADDR_32BIT,
	    BUS_SPACE_MAXADDR, NULL, NULL,
	    sizeof(struct bcm_dma_cb), 1,
	    sizeof(struct bcm_dma_cb),
	    BUS_DMA_ALLOCNOW, NULL, NULL,
	    &sc->sc_dma_tag);

	if (err) {
		device_printf(dev, "failed allocate DMA tag\n");
		return (err);
	}

	/* setup initial settings */
	for (i = 0; i < BCM_DMA_CH_MAX; i++) {
		ch = &sc->sc_dma_ch[i];

		bzero(ch, sizeof(struct bcm_dma_ch));
		ch->ch = i;
		ch->flags = BCM_DMA_CH_UNMAP;

		if ((bcm_dma_channel_mask & (1 << i)) == 0)
			continue;

		err = bus_dmamem_alloc(sc->sc_dma_tag, &cb_virt,
		    BUS_DMA_WAITOK | BUS_DMA_COHERENT | BUS_DMA_ZERO,
		    &ch->dma_map);
		if (err) {
			device_printf(dev, "cannot allocate DMA memory\n");
			break;
		}

		/* 
		 * Least alignment for busdma-allocated stuff is cache 
		 * line size, so just make sure nothing stupid happened
		 * and we got properly aligned address
		 */
		if ((uintptr_t)cb_virt & 0x1f) {
			device_printf(dev,
			    "DMA address is not 32-bytes aligned: %p\n",
			    (void*)cb_virt);
			break;
		}

		err = bus_dmamap_load(sc->sc_dma_tag, ch->dma_map, cb_virt,
		    sizeof(struct bcm_dma_cb), bcm_dmamap_cb, &cb_phys,
		    BUS_DMA_WAITOK);
		if (err) {
			device_printf(dev, "cannot load DMA memory\n");
			break;
		}

		ch->cb = cb_virt;
		ch->vc_cb = cb_phys;
		ch->flags = BCM_DMA_CH_FREE;
		ch->cb->info = INFO_WAIT_RESP;

		/* reset DMA engine */
		bus_write_4(sc->sc_mem, BCM_DMA_CS(i), CS_RESET);
	}

	return (0);
}

/*
 * Allocate DMA channel for further use, returns channel # or
 *     BCM_DMA_CH_INVALID
 */
int
bcm_dma_allocate(int req_ch)
{
	struct bcm_dma_softc *sc = bcm_dma_sc;
	int ch = BCM_DMA_CH_INVALID;
	int i;

	if (req_ch >= BCM_DMA_CH_MAX)
		return (BCM_DMA_CH_INVALID);

	/* Auto(req_ch < 0) or CH specified */
	mtx_lock(&sc->sc_mtx);

	if (req_ch < 0) {
		for (i = 0; i < BCM_DMA_CH_MAX; i++) {
			if (sc->sc_dma_ch[i].flags & BCM_DMA_CH_FREE) {
				ch = i;
				sc->sc_dma_ch[ch].flags &= ~BCM_DMA_CH_FREE;
				sc->sc_dma_ch[ch].flags |= BCM_DMA_CH_USED;
				break;
			}
		}
	}
	else {
		if (sc->sc_dma_ch[req_ch].flags & BCM_DMA_CH_FREE) {
			ch = req_ch;
			sc->sc_dma_ch[ch].flags &= ~BCM_DMA_CH_FREE;
			sc->sc_dma_ch[ch].flags |= BCM_DMA_CH_USED;
		}
	}

	mtx_unlock(&sc->sc_mtx);
	return (ch);
}

/*
 * Frees allocated channel. Returns 0 on success, -1 otherwise
 */
int
bcm_dma_free(int ch)
{
	struct bcm_dma_softc *sc = bcm_dma_sc;

	if (ch < 0 || ch >= BCM_DMA_CH_MAX)
		return (-1);

	mtx_lock(&sc->sc_mtx);
	if (sc->sc_dma_ch[ch].flags & BCM_DMA_CH_USED) {
		sc->sc_dma_ch[ch].flags |= BCM_DMA_CH_FREE;
		sc->sc_dma_ch[ch].flags &= ~BCM_DMA_CH_USED;
		sc->sc_dma_ch[ch].intr_func = NULL;
		sc->sc_dma_ch[ch].intr_arg = NULL;

		/* reset DMA engine */
		bcm_dma_reset(sc->sc_dev, ch);
	}

	mtx_unlock(&sc->sc_mtx);
	return (0);
}

/*
 * Assign handler function for channel interrupt
 * Returns 0 on success, -1 otherwise
 */
int
bcm_dma_setup_intr(int ch, void (*func)(int, void *), void *arg)
{
	struct bcm_dma_softc *sc = bcm_dma_sc;
	struct bcm_dma_cb *cb;

	if (ch < 0 || ch >= BCM_DMA_CH_MAX)
		return (-1);

	if (!(sc->sc_dma_ch[ch].flags & BCM_DMA_CH_USED))
		return (-1);

	sc->sc_dma_ch[ch].intr_func = func;
	sc->sc_dma_ch[ch].intr_arg = arg;
	cb = sc->sc_dma_ch[ch].cb;
	cb->info |= INFO_INT_EN;

	return (0);
}

/*
 * Setup DMA source parameters
 *     ch - channel number
 *     dreq - hardware DREQ # or BCM_DMA_DREQ_NONE if
 *         source is physical memory
 *     inc_addr - BCM_DMA_INC_ADDR if source address
 *         should be increased after each access or 
 *         BCM_DMA_SAME_ADDR if address should remain 
 *         the same
 *     width - size of read operation, BCM_DMA_32BIT
 *         for 32bit bursts, BCM_DMA_128BIT for 128 bits
 *	  
 * Returns 0 on success, -1 otherwise
 */
int
bcm_dma_setup_src(int ch, int dreq, int inc_addr, int width)
{
	struct bcm_dma_softc *sc = bcm_dma_sc;
	uint32_t info;

	if (ch < 0 || ch >= BCM_DMA_CH_MAX)
		return (-1);

	if (!(sc->sc_dma_ch[ch].flags & BCM_DMA_CH_USED))
		return (-1);

	info = sc->sc_dma_ch[ch].cb->info;
	info &= ~INFO_PERMAP_MASK;
	info |= (dreq << INFO_PERMAP_SHIFT) & INFO_PERMAP_MASK;

	if (dreq)
		info |= INFO_S_DREQ;
	else
		info &= ~INFO_S_DREQ;

	if (width == BCM_DMA_128BIT)
		info |= INFO_S_WIDTH;
	else
		info &= ~INFO_S_WIDTH;

	if (inc_addr == BCM_DMA_INC_ADDR)
		info |= INFO_S_INC;
	else
		info &= ~INFO_S_INC;

	sc->sc_dma_ch[ch].cb->info = info;

	return (0);
}

/*
 * Setup DMA destination parameters
 *     ch - channel number
 *     dreq - hardware DREQ # or BCM_DMA_DREQ_NONE if
 *         destination is physical memory
 *     inc_addr - BCM_DMA_INC_ADDR if source address
 *         should be increased after each access or 
 *         BCM_DMA_SAME_ADDR if address should remain 
 *         the same
 *     width - size of write operation, BCM_DMA_32BIT
 *         for 32bit bursts, BCM_DMA_128BIT for 128 bits
 *	  
 * Returns 0 on success, -1 otherwise
 */
int
bcm_dma_setup_dst(int ch, int dreq, int inc_addr, int width)
{
	struct bcm_dma_softc *sc = bcm_dma_sc;
	uint32_t info;

	if (ch < 0 || ch >= BCM_DMA_CH_MAX)
		return (-1);

	if (!(sc->sc_dma_ch[ch].flags & BCM_DMA_CH_USED))
		return (-1);

	info = sc->sc_dma_ch[ch].cb->info;
	info &= ~INFO_PERMAP_MASK;
	info |= (dreq << INFO_PERMAP_SHIFT) & INFO_PERMAP_MASK;

	if (dreq)
		info |= INFO_D_DREQ;
	else
		info &= ~INFO_D_DREQ;

	if (width == BCM_DMA_128BIT)
		info |= INFO_D_WIDTH;
	else
		info &= ~INFO_D_WIDTH;

	if (inc_addr == BCM_DMA_INC_ADDR)
		info |= INFO_D_INC;
	else
		info &= ~INFO_D_INC;

	sc->sc_dma_ch[ch].cb->info = info;

	return (0);
}

#ifdef DEBUG
void
bcm_dma_cb_dump(struct bcm_dma_cb *cb)
{

	printf("DMA CB ");
	printf("INFO: %8.8x ", cb->info);
	printf("SRC: %8.8x ", cb->src);
	printf("DST: %8.8x ", cb->dst);
	printf("LEN: %8.8x ", cb->len);
	printf("\n");
	printf("STRIDE: %8.8x ", cb->stride);
	printf("NEXT: %8.8x ", cb->next);
	printf("RSVD1: %8.8x ", cb->rsvd1);
	printf("RSVD2: %8.8x ", cb->rsvd2);
	printf("\n");
}

void
bcm_dma_reg_dump(int ch)
{
	struct bcm_dma_softc *sc = bcm_dma_sc;
	int i;
	uint32_t reg;

	if (ch < 0 || ch >= BCM_DMA_CH_MAX)
		return;

	printf("DMA%d: ", ch);
	for (i = 0; i < MAX_REG; i++) {
		reg = bus_read_4(sc->sc_mem, BCM_DMA_CH(ch) + i*4);
		printf("%8.8x ", reg);
	}
	printf("\n");
}
#endif

/*
 * Start DMA transaction
 *     ch - channel number
 *     src, dst - source and destination address in
 *         ARM physical memory address space. 
 *     len - amount of bytes to be transferred
 *	  
 * Returns 0 on success, -1 otherwise
 */
int
bcm_dma_start(int ch, vm_paddr_t src, vm_paddr_t dst, int len)
{
	struct bcm_dma_softc *sc = bcm_dma_sc;
	struct bcm_dma_cb *cb;

	if (ch < 0 || ch >= BCM_DMA_CH_MAX)
		return (-1);

	if (!(sc->sc_dma_ch[ch].flags & BCM_DMA_CH_USED))
		return (-1);

	cb = sc->sc_dma_ch[ch].cb;
	if (BCM2835_ARM_IS_IO(src))
		cb->src = IO_TO_VCBUS(src);
	else
		cb->src = PHYS_TO_VCBUS(src);
	if (BCM2835_ARM_IS_IO(dst))
		cb->dst = IO_TO_VCBUS(dst);
	else
		cb->dst = PHYS_TO_VCBUS(dst);
	cb->len = len;

	bus_dmamap_sync(sc->sc_dma_tag,
	    sc->sc_dma_ch[ch].dma_map, BUS_DMASYNC_PREWRITE);

	bus_write_4(sc->sc_mem, BCM_DMA_CBADDR(ch),
	    sc->sc_dma_ch[ch].vc_cb);
	bus_write_4(sc->sc_mem, BCM_DMA_CS(ch), CS_ACTIVE);

#ifdef DEBUG
	bcm_dma_cb_dump(sc->sc_dma_ch[ch].cb);
	bcm_dma_reg_dump(ch);
#endif

	return (0);
}

/*
 * Get length requested for DMA transaction
 *     ch - channel number
 *	  
 * Returns size of transaction, 0 if channel is invalid
 */
uint32_t
bcm_dma_length(int ch)
{
	struct bcm_dma_softc *sc = bcm_dma_sc;
	struct bcm_dma_cb *cb;

	if (ch < 0 || ch >= BCM_DMA_CH_MAX)
		return (0);

	if (!(sc->sc_dma_ch[ch].flags & BCM_DMA_CH_USED))
		return (0);

	cb = sc->sc_dma_ch[ch].cb;

	return (cb->len);
}

static void
bcm_dma_intr(void *arg)
{
	struct bcm_dma_softc *sc = bcm_dma_sc;
	struct bcm_dma_ch *ch = (struct bcm_dma_ch *)arg;
	uint32_t cs, debug;

	/* my interrupt? */
	cs = bus_read_4(sc->sc_mem, BCM_DMA_CS(ch->ch));

	if (!(cs & (CS_INT | CS_ERR))) {
		device_printf(sc->sc_dev,
		    "unexpected DMA intr CH=%d, CS=%x\n", ch->ch, cs);
		return;
	}

	/* running? */
	if (!(ch->flags & BCM_DMA_CH_USED)) {
		device_printf(sc->sc_dev,
		    "unused DMA intr CH=%d, CS=%x\n", ch->ch, cs);
		return;
	}

	if (cs & CS_ERR) {
		debug = bus_read_4(sc->sc_mem, BCM_DMA_DEBUG(ch->ch));
		device_printf(sc->sc_dev, "DMA error %d on CH%d\n",
			debug & DEBUG_ERROR_MASK, ch->ch);
		bus_write_4(sc->sc_mem, BCM_DMA_DEBUG(ch->ch), 
		    debug & DEBUG_ERROR_MASK);
		bcm_dma_reset(sc->sc_dev, ch->ch);
	}

	if (cs & CS_INT) {
		/* acknowledge interrupt */
		bus_write_4(sc->sc_mem, BCM_DMA_CS(ch->ch), 
		    CS_INT | CS_END);

		/* Prepare for possible access to len field */
		bus_dmamap_sync(sc->sc_dma_tag, ch->dma_map,
		    BUS_DMASYNC_POSTWRITE);

		/* save callback function and argument */
		if (ch->intr_func)
			ch->intr_func(ch->ch, ch->intr_arg);
	}
}

static int
bcm_dma_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "BCM2835 DMA Controller");
	return (BUS_PROBE_DEFAULT);
}

static int
bcm_dma_attach(device_t dev)
{
	struct bcm_dma_softc *sc = device_get_softc(dev);
	phandle_t node;
	int rid, err = 0;
	int i;

	sc->sc_dev = dev;

	if (bcm_dma_sc)
		return (ENXIO);

	for (i = 0; i < BCM_DMA_CH_MAX; i++) {
		sc->sc_irq[i] = NULL;
		sc->sc_intrhand[i] = NULL;
	}

	/* Get DMA channel mask. */
	node = ofw_bus_get_node(sc->sc_dev);
	if (OF_getencprop(node, "brcm,dma-channel-mask", &bcm_dma_channel_mask,
	    sizeof(bcm_dma_channel_mask)) == -1 &&
	    OF_getencprop(node, "broadcom,channels", &bcm_dma_channel_mask,
	    sizeof(bcm_dma_channel_mask)) == -1) {
		device_printf(dev, "could not get channel mask property\n");
		return (ENXIO);
	}

	/* Mask out channels used by GPU. */
	bcm_dma_channel_mask &= ~BCM_DMA_CH_GPU_MASK;

	/* DMA0 - DMA14 */
	rid = 0;
	sc->sc_mem = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE);
	if (sc->sc_mem == NULL) {
		device_printf(dev, "could not allocate memory resource\n");
		return (ENXIO);
	}

	/* IRQ DMA0 - DMA11 XXX NOT USE DMA12(spurious?) */
	for (rid = 0; rid < BCM_DMA_CH_MAX; rid++) {
		if ((bcm_dma_channel_mask & (1 << rid)) == 0)
			continue;

		sc->sc_irq[rid] = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
						       RF_ACTIVE);
		if (sc->sc_irq[rid] == NULL) {
			device_printf(dev, "cannot allocate interrupt\n");
			err = ENXIO;
			goto fail;
		}
		if (bus_setup_intr(dev, sc->sc_irq[rid], INTR_TYPE_MISC | INTR_MPSAFE,
				   NULL, bcm_dma_intr, &sc->sc_dma_ch[rid],
				   &sc->sc_intrhand[rid])) {
			device_printf(dev, "cannot setup interrupt handler\n");
			err = ENXIO;
			goto fail;
		}
	}

	mtx_init(&sc->sc_mtx, "bcmdma", "bcmdma", MTX_DEF);
	bcm_dma_sc = sc;

	err = bcm_dma_init(dev);
	if (err)
		goto fail;

	return (err);

fail:
	if (sc->sc_mem)
		bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->sc_mem);

	for (i = 0; i < BCM_DMA_CH_MAX; i++) {
		if (sc->sc_intrhand[i])
			bus_teardown_intr(dev, sc->sc_irq[i], sc->sc_intrhand[i]);
		if (sc->sc_irq[i])
			bus_release_resource(dev, SYS_RES_IRQ, 0, sc->sc_irq[i]);
	}

	return (err);
}

static device_method_t bcm_dma_methods[] = {
	DEVMETHOD(device_probe,		bcm_dma_probe),
	DEVMETHOD(device_attach,	bcm_dma_attach),
	{ 0, 0 }
};

static driver_t bcm_dma_driver = {
	"bcm_dma",
	bcm_dma_methods,
	sizeof(struct bcm_dma_softc),
};

static devclass_t bcm_dma_devclass;

DRIVER_MODULE(bcm_dma, simplebus, bcm_dma_driver, bcm_dma_devclass, 0, 0);
MODULE_VERSION(bcm_dma, 1);
