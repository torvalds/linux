/*-
 * SPDX-License-Identifier: BSD-4-Clause AND BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002-2005 M. Warner Losh.
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
 *
 * This software may be derived from NetBSD i82365.c and other files with
 * the following copyright:
 *
 * Copyright (c) 1997 Marc Horowitz.  All rights reserved.
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
 *	This product includes software developed by Marc Horowitz.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/condvar.h>
#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/module.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/conf.h>

#include <sys/bus.h>
#include <machine/bus.h>
#include <sys/rman.h>
#include <machine/resource.h>

#include <dev/pccard/pccardreg.h>
#include <dev/pccard/pccardvar.h>

#include <dev/exca/excareg.h>
#include <dev/exca/excavar.h>

#ifdef EXCA_DEBUG
#define DEVPRINTF(dev, fmt, args...)	device_printf((dev), (fmt), ## args)
#define DPRINTF(fmt, args...)		printf(fmt, ## args)
#else
#define DEVPRINTF(dev, fmt, args...)
#define DPRINTF(fmt, args...)
#endif

static const char *chip_names[] = 
{
	"CardBus socket",
	"Intel i82365SL-A/B or clone",
	"Intel i82365sl-DF step",
	"VLSI chip",
	"Cirrus Logic PD6710",
	"Cirrus logic PD6722",
	"Cirrus Logic PD6729",
	"Vadem 365",
	"Vadem 465",
	"Vadem 468",
	"Vadem 469",
	"Ricoh RF5C296",
	"Ricoh RF5C396",
	"IBM clone",
	"IBM KING PCMCIA Controller"
};

static exca_getb_fn exca_mem_getb;
static exca_putb_fn exca_mem_putb;
static exca_getb_fn exca_io_getb;
static exca_putb_fn exca_io_putb;

/* memory */

#define	EXCA_MEMINFO(NUM) {						\
	EXCA_SYSMEM_ADDR ## NUM ## _START_LSB,				\
	EXCA_SYSMEM_ADDR ## NUM ## _START_MSB,				\
	EXCA_SYSMEM_ADDR ## NUM ## _STOP_LSB,				\
	EXCA_SYSMEM_ADDR ## NUM ## _STOP_MSB,				\
	EXCA_SYSMEM_ADDR ## NUM ## _WIN,				\
	EXCA_CARDMEM_ADDR ## NUM ## _LSB,				\
	EXCA_CARDMEM_ADDR ## NUM ## _MSB,				\
	EXCA_ADDRWIN_ENABLE_MEM ## NUM,					\
}

static struct mem_map_index_st {
	int	sysmem_start_lsb;
	int	sysmem_start_msb;
	int	sysmem_stop_lsb;
	int	sysmem_stop_msb;
	int	sysmem_win;
	int	cardmem_lsb;
	int	cardmem_msb;
	int	memenable;
} mem_map_index[] = {
	EXCA_MEMINFO(0),
	EXCA_MEMINFO(1),
	EXCA_MEMINFO(2),
	EXCA_MEMINFO(3),
	EXCA_MEMINFO(4)
};
#undef	EXCA_MEMINFO

static uint8_t
exca_mem_getb(struct exca_softc *sc, int reg)
{
	return (bus_space_read_1(sc->bst, sc->bsh, sc->offset + reg));
}

static void
exca_mem_putb(struct exca_softc *sc, int reg, uint8_t val)
{
	bus_space_write_1(sc->bst, sc->bsh, sc->offset + reg, val);
}

static uint8_t
exca_io_getb(struct exca_softc *sc, int reg)
{
	bus_space_write_1(sc->bst, sc->bsh, EXCA_REG_INDEX, reg + sc->offset);
	return (bus_space_read_1(sc->bst, sc->bsh, EXCA_REG_DATA));
}

static void
exca_io_putb(struct exca_softc *sc, int reg, uint8_t val)
{
	bus_space_write_1(sc->bst, sc->bsh, EXCA_REG_INDEX, reg + sc->offset);
	bus_space_write_1(sc->bst, sc->bsh, EXCA_REG_DATA, val);
}

/*
 * Helper function.  This will map the requested memory slot.  We setup the
 * map before we call this function.  This is used to initially force the
 * mapping, as well as later restore the mapping after it has been destroyed
 * in some fashion (due to a power event typically).
 */
static void
exca_do_mem_map(struct exca_softc *sc, int win)
{
	struct mem_map_index_st *map;
	struct pccard_mem_handle *mem;
	uint32_t offset;
	uint32_t mem16;
	uint32_t attrmem;
	
	map = &mem_map_index[win];
	mem = &sc->mem[win];
	mem16 = (mem->kind & PCCARD_MEM_16BIT) ? 
	    EXCA_SYSMEM_ADDRX_START_MSB_DATASIZE_16BIT : 0;
	attrmem = (mem->kind & PCCARD_MEM_ATTR) ?
	    EXCA_CARDMEM_ADDRX_MSB_REGACTIVE_ATTR : 0;
	offset = ((mem->cardaddr >> EXCA_CARDMEM_ADDRX_SHIFT) -
	  (mem->addr >> EXCA_SYSMEM_ADDRX_SHIFT)) & 0x3fff;
	exca_putb(sc, map->sysmem_start_lsb,
	    mem->addr >> EXCA_SYSMEM_ADDRX_SHIFT);
	exca_putb(sc, map->sysmem_start_msb,
	    ((mem->addr >> (EXCA_SYSMEM_ADDRX_SHIFT + 8)) &
	    EXCA_SYSMEM_ADDRX_START_MSB_ADDR_MASK) | mem16);

	exca_putb(sc, map->sysmem_stop_lsb,
	    (mem->addr + mem->realsize - 1) >> EXCA_SYSMEM_ADDRX_SHIFT);
	exca_putb(sc, map->sysmem_stop_msb,
	    (((mem->addr + mem->realsize - 1) >>
	    (EXCA_SYSMEM_ADDRX_SHIFT + 8)) &
	    EXCA_SYSMEM_ADDRX_STOP_MSB_ADDR_MASK) |
	    EXCA_SYSMEM_ADDRX_STOP_MSB_WAIT2);
	exca_putb(sc, map->sysmem_win, mem->addr >> EXCA_MEMREG_WIN_SHIFT);

	exca_putb(sc, map->cardmem_lsb, offset & 0xff);
	exca_putb(sc, map->cardmem_msb, ((offset >> 8) &
	    EXCA_CARDMEM_ADDRX_MSB_ADDR_MASK) | attrmem);

	DPRINTF("%s %d-bit memory",
	    mem->kind & PCCARD_MEM_ATTR ? "attribute" : "common",
	    mem->kind & PCCARD_MEM_16BIT ? 16 : 8);
	exca_setb(sc, EXCA_ADDRWIN_ENABLE, map->memenable |
	    EXCA_ADDRWIN_ENABLE_MEMCS16);

	DELAY(100);
#ifdef EXCA_DEBUG
	{
		int r1, r2, r3, r4, r5, r6, r7;
		r1 = exca_getb(sc, map->sysmem_start_msb);
		r2 = exca_getb(sc, map->sysmem_start_lsb);
		r3 = exca_getb(sc, map->sysmem_stop_msb);
		r4 = exca_getb(sc, map->sysmem_stop_lsb);
		r5 = exca_getb(sc, map->cardmem_msb);
		r6 = exca_getb(sc, map->cardmem_lsb);
		r7 = exca_getb(sc, map->sysmem_win);
		printf("exca_do_mem_map win %d: %#02x%#02x %#02x%#02x "
		    "%#02x%#02x %#02x (%#08x+%#06x.%#06x*%#06x) flags %#x\n",
		    win, r1, r2, r3, r4, r5, r6, r7,
		    mem->addr, mem->size, mem->realsize,
		    mem->cardaddr, mem->kind);
	}
#endif
}

/*
 * public interface to map a resource.  kind is the type of memory to
 * map (either common or attribute).  Memory created via this interface
 * starts out at card address 0.  Since the only way to set this is
 * to set it on a struct resource after it has been mapped, we're safe
 * in maping this assumption.  Note that resources can be remapped using
 * exca_do_mem_map so that's how the card address can be set later.
 */
int
exca_mem_map(struct exca_softc *sc, int kind, struct resource *res)
{
	int win;

	for (win = 0; win < EXCA_MEM_WINS; win++) {
		if ((sc->memalloc & (1 << win)) == 0) {
			sc->memalloc |= (1 << win);
			break;
		}
	}
	if (win >= EXCA_MEM_WINS)
		return (ENOSPC);
	if (sc->flags & EXCA_HAS_MEMREG_WIN) {
#ifdef __LP64__
		if (rman_get_start(res) >> (EXCA_MEMREG_WIN_SHIFT + 8) != 0) {
			device_printf(sc->dev,
			    "Does not support mapping above 4GB.");
			return (EINVAL);
		}
#endif
	} else {
		if (rman_get_start(res) >> EXCA_MEMREG_WIN_SHIFT != 0) {
			device_printf(sc->dev,
			    "Does not support mapping above 16M.");
			return (EINVAL);
		}
	}

	sc->mem[win].cardaddr = 0;
	sc->mem[win].memt = rman_get_bustag(res);
	sc->mem[win].memh = rman_get_bushandle(res);
	sc->mem[win].addr = rman_get_start(res);
	sc->mem[win].size = rman_get_end(res) - sc->mem[win].addr + 1;
	sc->mem[win].realsize = sc->mem[win].size + EXCA_MEM_PAGESIZE - 1;
	sc->mem[win].realsize = sc->mem[win].realsize -
	    (sc->mem[win].realsize % EXCA_MEM_PAGESIZE);
	sc->mem[win].kind = kind;
	DPRINTF("exca_mem_map window %d bus %x+%x card addr %x\n",
	    win, sc->mem[win].addr, sc->mem[win].size, sc->mem[win].cardaddr);
	exca_do_mem_map(sc, win);

	return (0);
}

/*
 * Private helper function.  This turns off a given memory map that is in
 * use.  We do this by just clearing the enable bit in the pcic.  If we needed
 * to make memory unmapping/mapping pairs faster, we would have to store
 * more state information about the pcic and then use that to intelligently
 * to the map/unmap.  However, since we don't do that sort of thing often
 * (generally just at configure time), it isn't a case worth optimizing.
 */
static void
exca_mem_unmap(struct exca_softc *sc, int window)
{
	if (window < 0 || window >= EXCA_MEM_WINS)
		panic("exca_mem_unmap: window out of range");

	exca_clrb(sc, EXCA_ADDRWIN_ENABLE, mem_map_index[window].memenable);
	sc->memalloc &= ~(1 << window);
}

/*
 * Find the map that we're using to hold the resource.  This works well
 * so long as the client drivers don't do silly things like map the same
 * area mutliple times, or map both common and attribute memory at the
 * same time.  This latter restriction is a bug.  We likely should just
 * store a pointer to the res in the mem[x] data structure.
 */
static int
exca_mem_findmap(struct exca_softc *sc, struct resource *res)
{
	int win;

	for (win = 0; win < EXCA_MEM_WINS; win++) {
		if (sc->mem[win].memt == rman_get_bustag(res) &&
		    sc->mem[win].addr == rman_get_start(res) &&
		    sc->mem[win].size == rman_get_size(res))
			return (win);
	}
	return (-1);
}

/*
 * Set the memory flag.  This means that we are setting if the memory
 * is coming from attribute memory or from common memory on the card.
 * CIS entries are generally in attribute memory (although they can
 * reside in common memory).  Generally, this is the only use for attribute
 * memory.  However, some cards require their drivers to dance in both
 * common and/or attribute memory and this interface (and setting the
 * offset interface) exist for such cards.
 */
int
exca_mem_set_flags(struct exca_softc *sc, struct resource *res, uint32_t flags)
{
	int win;

	win = exca_mem_findmap(sc, res);
	if (win < 0) {
		device_printf(sc->dev,
		    "set_res_flags: specified resource not active\n");
		return (ENOENT);
	}

	switch (flags)
	{
	case PCCARD_A_MEM_ATTR:
		sc->mem[win].kind |= PCCARD_MEM_ATTR;
		break;
	case PCCARD_A_MEM_COM:
		sc->mem[win].kind &= ~PCCARD_MEM_ATTR;
		break;
	case PCCARD_A_MEM_16BIT:
		sc->mem[win].kind |= PCCARD_MEM_16BIT;
		break;
	case PCCARD_A_MEM_8BIT:
		sc->mem[win].kind &= ~PCCARD_MEM_16BIT;
		break;
	}
	exca_do_mem_map(sc, win);
	return (0);
}

/*
 * Given a resource, go ahead and unmap it if we can find it in the
 * resrouce list that's used.
 */
int
exca_mem_unmap_res(struct exca_softc *sc, struct resource *res)
{
	int win;

	win = exca_mem_findmap(sc, res);
	if (win < 0)
		return (ENOENT);
	exca_mem_unmap(sc, win);
	return (0);
}
	
/*
 * Set the offset of the memory.  We use this for reading the CIS and
 * frobbing the pccard's pccard registers (CCR, etc).  Some drivers
 * need to access arbitrary attribute and common memory during their
 * initialization and operation.
 */
int
exca_mem_set_offset(struct exca_softc *sc, struct resource *res,
    uint32_t cardaddr, uint32_t *deltap)
{
	int win;
	uint32_t delta;

	win = exca_mem_findmap(sc, res);
	if (win < 0) {
		device_printf(sc->dev,
		    "set_memory_offset: specified resource not active\n");
		return (ENOENT);
	}
	sc->mem[win].cardaddr = rounddown2(cardaddr, EXCA_MEM_PAGESIZE);
	delta = cardaddr % EXCA_MEM_PAGESIZE;
	if (deltap)
		*deltap = delta;
	sc->mem[win].realsize = sc->mem[win].size + delta +
	    EXCA_MEM_PAGESIZE - 1;
	sc->mem[win].realsize = sc->mem[win].realsize -
	    (sc->mem[win].realsize % EXCA_MEM_PAGESIZE);
	exca_do_mem_map(sc, win);
	return (0);
}
			

/* I/O */

#define	EXCA_IOINFO(NUM) {						\
	EXCA_IOADDR ## NUM ## _START_LSB,				\
	EXCA_IOADDR ## NUM ## _START_MSB,				\
	EXCA_IOADDR ## NUM ## _STOP_LSB,				\
	EXCA_IOADDR ## NUM ## _STOP_MSB,				\
	EXCA_ADDRWIN_ENABLE_IO ## NUM,					\
	EXCA_IOCTL_IO ## NUM ## _WAITSTATE				\
	| EXCA_IOCTL_IO ## NUM ## _ZEROWAIT				\
	| EXCA_IOCTL_IO ## NUM ## _IOCS16SRC_MASK			\
	| EXCA_IOCTL_IO ## NUM ## _DATASIZE_MASK,			\
	{								\
		EXCA_IOCTL_IO ## NUM ## _IOCS16SRC_CARD,		\
		EXCA_IOCTL_IO ## NUM ## _IOCS16SRC_DATASIZE		\
		| EXCA_IOCTL_IO ## NUM ## _DATASIZE_8BIT,		\
		EXCA_IOCTL_IO ## NUM ## _IOCS16SRC_DATASIZE		\
		| EXCA_IOCTL_IO ## NUM ## _DATASIZE_16BIT,		\
	}								\
}

static struct io_map_index_st {
	int	start_lsb;
	int	start_msb;
	int	stop_lsb;
	int	stop_msb;
	int	ioenable;
	int	ioctlmask;
	int	ioctlbits[3]; /* indexed by PCCARD_WIDTH_* */
} io_map_index[] = {
	EXCA_IOINFO(0),
	EXCA_IOINFO(1),
};
#undef	EXCA_IOINFO

static void
exca_do_io_map(struct exca_softc *sc, int win)
{
	struct io_map_index_st *map;

	struct pccard_io_handle *io;

	map = &io_map_index[win];
	io = &sc->io[win];
	exca_putb(sc, map->start_lsb, io->addr & 0xff);
	exca_putb(sc, map->start_msb, (io->addr >> 8) & 0xff);

	exca_putb(sc, map->stop_lsb, (io->addr + io->size - 1) & 0xff);
	exca_putb(sc, map->stop_msb, ((io->addr + io->size - 1) >> 8) & 0xff);

	exca_clrb(sc, EXCA_IOCTL, map->ioctlmask);
	exca_setb(sc, EXCA_IOCTL, map->ioctlbits[io->width]);

	exca_setb(sc, EXCA_ADDRWIN_ENABLE, map->ioenable);
#ifdef EXCA_DEBUG
	{
		int r1, r2, r3, r4;
		r1 = exca_getb(sc, map->start_msb);
		r2 = exca_getb(sc, map->start_lsb);
		r3 = exca_getb(sc, map->stop_msb);
		r4 = exca_getb(sc, map->stop_lsb);
		DPRINTF("exca_do_io_map window %d: %02x%02x %02x%02x "
		    "(%08x+%08x)\n", win, r1, r2, r3, r4,
		    io->addr, io->size);
	}
#endif
}

int
exca_io_map(struct exca_softc *sc, int width, struct resource *r)
{
	int win;
#ifdef EXCA_DEBUG
	static char *width_names[] = { "auto", "io8", "io16"};
#endif
	for (win=0; win < EXCA_IO_WINS; win++) {
		if ((sc->ioalloc & (1 << win)) == 0) {
			sc->ioalloc |= (1 << win);
			break;
		}
	}
	if (win >= EXCA_IO_WINS)
		return (ENOSPC);

	sc->io[win].iot = rman_get_bustag(r);
	sc->io[win].ioh = rman_get_bushandle(r);
	sc->io[win].addr = rman_get_start(r);
	sc->io[win].size = rman_get_end(r) - sc->io[win].addr + 1;
	sc->io[win].flags = 0;
	sc->io[win].width = width;
	DPRINTF("exca_io_map window %d %s port %x+%x\n",
	    win, width_names[width], sc->io[win].addr,
	    sc->io[win].size);
	exca_do_io_map(sc, win);

	return (0);
}

static void
exca_io_unmap(struct exca_softc *sc, int window)
{
	if (window >= EXCA_IO_WINS)
		panic("exca_io_unmap: window out of range");

	exca_clrb(sc, EXCA_ADDRWIN_ENABLE, io_map_index[window].ioenable);

	sc->ioalloc &= ~(1 << window);

	sc->io[window].iot = 0;
	sc->io[window].ioh = 0;
	sc->io[window].addr = 0;
	sc->io[window].size = 0;
	sc->io[window].flags = 0;
	sc->io[window].width = 0;
}

static int
exca_io_findmap(struct exca_softc *sc, struct resource *res)
{
	int win;

	for (win = 0; win < EXCA_IO_WINS; win++) {
		if (sc->io[win].iot == rman_get_bustag(res) &&
		    sc->io[win].addr == rman_get_start(res) &&
		    sc->io[win].size == rman_get_size(res))
			return (win);
	}
	return (-1);
}


int
exca_io_unmap_res(struct exca_softc *sc, struct resource *res)
{
	int win;

	win = exca_io_findmap(sc, res);
	if (win < 0)
		return (ENOENT);
	exca_io_unmap(sc, win);
	return (0);
}

/* Misc */

/*
 * If interrupts are enabled, then we should be able to just wait for
 * an interrupt routine to wake us up.  Busy waiting shouldn't be
 * necessary.  Sadly, not all legacy ISA cards support an interrupt
 * for the busy state transitions, at least according to their datasheets, 
 * so we busy wait a while here..
 */
static void
exca_wait_ready(struct exca_softc *sc)
{
	int i;
	DEVPRINTF(sc->dev, "exca_wait_ready: status 0x%02x\n",
	    exca_getb(sc, EXCA_IF_STATUS));
	for (i = 0; i < 10000; i++) {
		if (exca_getb(sc, EXCA_IF_STATUS) & EXCA_IF_STATUS_READY)
			return;
		DELAY(500);
	}
	device_printf(sc->dev, "ready never happened, status = %02x\n",
	    exca_getb(sc, EXCA_IF_STATUS));
}

/*
 * Reset the card.  Ideally, we'd do a lot of this via interrupts.
 * However, many PC Cards will deassert the ready signal.  This means
 * that they are asserting an interrupt.  This makes it hard to 
 * do anything but a busy wait here.  One could argue that these
 * such cards are broken, or that the bridge that allows this sort
 * of interrupt through isn't quite what you'd want (and may be a standards
 * violation).  However, such arguing would leave a huge class of PC Cards
 * and bridges out of reach for use in the system.
 *
 * Maybe I should reevaluate the above based on the power bug I fixed
 * in OLDCARD.
 */
void
exca_reset(struct exca_softc *sc, device_t child)
{
	int win;

	/* enable socket i/o */
	exca_setb(sc, EXCA_PWRCTL, EXCA_PWRCTL_OE);

	exca_putb(sc, EXCA_INTR, EXCA_INTR_ENABLE);
	/* hold reset for 30ms */
	DELAY(30*1000);
	/* clear the reset flag */
	exca_setb(sc, EXCA_INTR, EXCA_INTR_RESET);
	/* wait 20ms as per PC Card standard (r2.01) section 4.3.6 */
	DELAY(20*1000);

	exca_wait_ready(sc);

	/* disable all address windows */
	exca_putb(sc, EXCA_ADDRWIN_ENABLE, 0);

	exca_setb(sc, EXCA_INTR, EXCA_INTR_CARDTYPE_IO);
	DEVPRINTF(sc->dev, "card type is io\n");

	/* reinstall all the memory and io mappings */
	for (win = 0; win < EXCA_MEM_WINS; ++win)
		if (sc->memalloc & (1 << win))
			exca_do_mem_map(sc, win);
	for (win = 0; win < EXCA_IO_WINS; ++win)
		if (sc->ioalloc & (1 << win))
			exca_do_io_map(sc, win);
}

/*
 * Initialize the exca_softc data structure for the first time.
 */
void
exca_init(struct exca_softc *sc, device_t dev, 
    bus_space_tag_t bst, bus_space_handle_t bsh, uint32_t offset)
{
	sc->dev = dev;
	sc->memalloc = 0;
	sc->ioalloc = 0;
	sc->bst = bst;
	sc->bsh = bsh;
	sc->offset = offset;
	sc->flags = 0;
	sc->getb = exca_mem_getb;
	sc->putb = exca_mem_putb;
}

/*
 * Is this socket valid?
 */
static int
exca_valid_slot(struct exca_softc *exca)
{
	uint8_t c;

	/* Assume the worst */
	exca->chipset = EXCA_BOGUS;

	/*
	 * see if there's a PCMCIA controller here
	 * Intel PCMCIA controllers use 0x82 and 0x83
	 * IBM clone chips use 0x88 and 0x89, apparently
	 */
	c = exca_getb(exca, EXCA_IDENT);
	DEVPRINTF(exca->dev, "Ident is %x\n", c);
	if ((c & EXCA_IDENT_IFTYPE_MASK) != EXCA_IDENT_IFTYPE_MEM_AND_IO)
		return (0);
	if ((c & EXCA_IDENT_ZERO) != 0)
		return (0);
	switch (c & EXCA_IDENT_REV_MASK) {
	/*
	 *	82365 or clones.
	 */
	case EXCA_IDENT_REV_I82365SLR0:
	case EXCA_IDENT_REV_I82365SLR1:
		exca->chipset = EXCA_I82365;
		/*
		 * Check for Vadem chips by unlocking their extra
		 * registers and looking for valid ID.  Bit 3 in
		 * the ID register is normally 0, except when
		 * EXCA_VADEMREV is set.  Other bridges appear
		 * to ignore this frobbing.
		 */
		bus_space_write_1(exca->bst, exca->bsh, EXCA_REG_INDEX,
		    EXCA_VADEM_COOKIE1);
		bus_space_write_1(exca->bst, exca->bsh, EXCA_REG_INDEX,
		    EXCA_VADEM_COOKIE2);
		exca_setb(exca, EXCA_VADEM_VMISC, EXCA_VADEM_REV);
		c = exca_getb(exca, EXCA_IDENT);
		if (c & 0x08) {
			switch (c & 7) {
			case 1:
				exca->chipset = EXCA_VG365;
				break;
			case 2:
				exca->chipset = EXCA_VG465;
				break;
			case 3:
				exca->chipset = EXCA_VG468;
				break;
			default:
				exca->chipset = EXCA_VG469;
				break;
			}
			exca_clrb(exca, EXCA_VADEM_VMISC, EXCA_VADEM_REV);
			break;
		}
		/*
		 * Check for RICOH RF5C[23]96 PCMCIA Controller
		 */
		c = exca_getb(exca, EXCA_RICOH_ID);
		if (c == EXCA_RID_396) {
			exca->chipset = EXCA_RF5C396;
			break;
		} else if (c == EXCA_RID_296) {
			exca->chipset = EXCA_RF5C296;
			break;
		}
		/*
		 *	Check for Cirrus logic chips.
		 */
		exca_putb(exca, EXCA_CIRRUS_CHIP_INFO, 0);
		c = exca_getb(exca, EXCA_CIRRUS_CHIP_INFO);
		if ((c & EXCA_CIRRUS_CHIP_INFO_CHIP_ID) ==
		    EXCA_CIRRUS_CHIP_INFO_CHIP_ID) {
			c = exca_getb(exca, EXCA_CIRRUS_CHIP_INFO);
			if ((c & EXCA_CIRRUS_CHIP_INFO_CHIP_ID) == 0) {
				if (c & EXCA_CIRRUS_CHIP_INFO_SLOTS)
					exca->chipset = EXCA_PD6722;
				else
					exca->chipset = EXCA_PD6710;
				break;
			}
		}
		break;

	case EXCA_IDENT_REV_I82365SLDF:
		/*
		 *	Intel i82365sl-DF step or maybe a vlsi 82c146
		 * we detected the vlsi case earlier, so if the controller
		 * isn't set, we know it is a i82365sl step D.
		 */
		exca->chipset = EXCA_I82365SL_DF;
		break;
	case EXCA_IDENT_REV_IBM1:
	case EXCA_IDENT_REV_IBM2:
		exca->chipset = EXCA_IBM;
		break;
	case EXCA_IDENT_REV_IBM_KING:
		exca->chipset = EXCA_IBM_KING;
		break;
	default:
		return (0);
	}
	return (1);
}

/*
 * Probe the expected slots.  We maybe should set the ID for each of these
 * slots too while we're at it.  But maybe that belongs to a separate
 * function.
 *
 * The caller must guarantee that at least EXCA_NSLOTS are present in exca.
 */
int
exca_probe_slots(device_t dev, struct exca_softc *exca, bus_space_tag_t iot,
    bus_space_handle_t ioh)
{
	int err;
	int i;

	err = ENXIO;
	for (i = 0; i < EXCA_NSLOTS; i++)  {
		exca_init(&exca[i], dev, iot, ioh, i * EXCA_SOCKET_SIZE);
		exca->getb = exca_io_getb;
		exca->putb = exca_io_putb;
		if (exca_valid_slot(&exca[i])) {
			device_set_desc(dev, chip_names[exca[i].chipset]);
			err = 0;
		}
	}
	return (err);
}

void
exca_insert(struct exca_softc *exca)
{
	if (device_is_attached(exca->pccarddev)) {
		if (CARD_ATTACH_CARD(exca->pccarddev) != 0)
			device_printf(exca->dev,
			    "PC Card card activation failed\n");
	} else {
		device_printf(exca->dev,
		    "PC Card inserted, but no pccard bus.\n");
	}
}
  

void
exca_removal(struct exca_softc *exca)
{
	if (device_is_attached(exca->pccarddev))
		CARD_DETACH_CARD(exca->pccarddev);
}

int
exca_activate_resource(struct exca_softc *exca, device_t child, int type,
    int rid, struct resource *res)
{
	int err;

	if (rman_get_flags(res) & RF_ACTIVE)
		return (0);
	err = BUS_ACTIVATE_RESOURCE(device_get_parent(exca->dev), child,
	    type, rid, res);
	if (err)
		return (err);
	switch (type) {
	case SYS_RES_IOPORT:
		err = exca_io_map(exca, PCCARD_WIDTH_AUTO, res);
		break;
	case SYS_RES_MEMORY:
		err = exca_mem_map(exca, 0, res);
		break;
	}
	if (err)
		BUS_DEACTIVATE_RESOURCE(device_get_parent(exca->dev), child,
		    type, rid, res);
	return (err);
}

int
exca_deactivate_resource(struct exca_softc *exca, device_t child, int type,
    int rid, struct resource *res)
{
	if (rman_get_flags(res) & RF_ACTIVE) { /* if activated */
		switch (type) {
		case SYS_RES_IOPORT:
			if (exca_io_unmap_res(exca, res))
				return (ENOENT);
			break;
		case SYS_RES_MEMORY:
			if (exca_mem_unmap_res(exca, res))
				return (ENOENT);
			break;
		}
	}
	return (BUS_DEACTIVATE_RESOURCE(device_get_parent(exca->dev), child,
	    type, rid, res));
}

#if 0
static struct resource *
exca_alloc_resource(struct exca_softc *sc, device_t child, int type, int *rid,
    u_long start, u_long end, u_long count, uint flags)
{
	struct resource *res = NULL;
	int tmp;

	switch (type) {
	case SYS_RES_MEMORY:
		if (start < cbb_start_mem)
			start = cbb_start_mem;
		if (end < start)
			end = start;
		flags = (flags & ~RF_ALIGNMENT_MASK) |
		    rman_make_alignment_flags(CBB_MEMALIGN);
		break;
	case SYS_RES_IOPORT:
		if (start < cbb_start_16_io)
			start = cbb_start_16_io;
		if (end < start)
			end = start;
		break;
	case SYS_RES_IRQ:
		tmp = rman_get_start(sc->irq_res);
		if (start > tmp || end < tmp || count != 1) {
			device_printf(child, "requested interrupt %ld-%ld,"
			    "count = %ld not supported by cbb\n",
			    start, end, count);
			return (NULL);
		}
		flags |= RF_SHAREABLE;
		start = end = rman_get_start(sc->irq_res);
		break;
	}
	res = BUS_ALLOC_RESOURCE(up, child, type, rid,
	    start, end, count, flags & ~RF_ACTIVE);
	if (res == NULL)
		return (NULL);
	cbb_insert_res(sc, res, type, *rid);
	if (flags & RF_ACTIVE) {
		if (bus_activate_resource(child, type, *rid, res) != 0) {
			bus_release_resource(child, type, *rid, res);
			return (NULL);
		}
	}

	return (res);
}

static int
exca_release_resource(struct exca_softc *sc, device_t child, int type,
    int rid, struct resource *res)
{
	int error;

	if (rman_get_flags(res) & RF_ACTIVE) {
		error = bus_deactivate_resource(child, type, rid, res);
		if (error != 0)
			return (error);
	}
	cbb_remove_res(sc, res);
	return (BUS_RELEASE_RESOURCE(device_get_parent(brdev), child,
	    type, rid, res));
}
#endif

static int
exca_modevent(module_t mod, int cmd, void *arg)
{
	return 0;
}

DEV_MODULE(exca, exca_modevent, NULL);
MODULE_VERSION(exca, 1);
