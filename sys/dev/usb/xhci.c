/* $OpenBSD: xhci.c,v 1.136 2025/03/01 14:43:03 kirill Exp $ */

/*
 * Copyright (c) 2014-2015 Martin Pieuchot
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/queue.h>
#include <sys/timeout.h>
#include <sys/pool.h>
#include <sys/endian.h>
#include <sys/rwlock.h>

#include <machine/bus.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdivar.h>
#include <dev/usb/usb_mem.h>

#include <dev/usb/xhcireg.h>
#include <dev/usb/xhcivar.h>

struct cfdriver xhci_cd = {
	NULL, "xhci", DV_DULL, CD_SKIPHIBERNATE
};

#ifdef XHCI_DEBUG
#define DPRINTF(x)	do { if (xhcidebug) printf x; } while(0)
#define DPRINTFN(n,x)	do { if (xhcidebug>(n)) printf x; } while (0)
int xhcidebug = 3;
#else
#define DPRINTF(x)
#define DPRINTFN(n,x)
#endif

#define DEVNAME(sc)	((sc)->sc_bus.bdev.dv_xname)

#define TRBOFF(r, trb)	((char *)(trb) - (char *)((r)->trbs))
#define DEQPTR(r)	((r).dma.paddr + (sizeof(struct xhci_trb) * (r).index))

struct pool *xhcixfer;

struct xhci_pipe {
	struct usbd_pipe	pipe;

	uint8_t			dci;
	uint8_t			slot;	/* Device slot ID */
	struct xhci_ring	ring;

	/*
	 * XXX used to pass the xfer pointer back to the
	 * interrupt routine, better way?
	 */
	struct usbd_xfer	*pending_xfers[XHCI_MAX_XFER];
	struct usbd_xfer	*aborted_xfer;
	int			 halted;
	size_t			 free_trbs;
	int			 skip;
#define TRB_PROCESSED_NO	0
#define TRB_PROCESSED_YES 	1
#define TRB_PROCESSED_SHORT	2
	uint8_t			 trb_processed[XHCI_MAX_XFER];
};

int	xhci_reset(struct xhci_softc *);
void	xhci_suspend(struct xhci_softc *);
int	xhci_intr1(struct xhci_softc *);
void	xhci_event_dequeue(struct xhci_softc *);
void	xhci_event_xfer(struct xhci_softc *, uint64_t, uint32_t, uint32_t);
int	xhci_event_xfer_generic(struct xhci_softc *, struct usbd_xfer *,
	    struct xhci_pipe *, uint32_t, int, uint8_t, uint8_t, uint8_t);
int	xhci_event_xfer_isoc(struct usbd_xfer *, struct xhci_pipe *,
	    uint32_t, int, uint8_t);
void	xhci_event_command(struct xhci_softc *, uint64_t);
void	xhci_event_port_change(struct xhci_softc *, uint64_t, uint32_t);
int	xhci_pipe_init(struct xhci_softc *, struct usbd_pipe *);
int	xhci_context_setup(struct xhci_softc *, struct usbd_pipe *);
int	xhci_scratchpad_alloc(struct xhci_softc *, int);
void	xhci_scratchpad_free(struct xhci_softc *);
int	xhci_softdev_alloc(struct xhci_softc *, uint8_t);
void	xhci_softdev_free(struct xhci_softc *, uint8_t);
int	xhci_ring_alloc(struct xhci_softc *, struct xhci_ring *, size_t,
	    size_t);
void	xhci_ring_free(struct xhci_softc *, struct xhci_ring *);
void	xhci_ring_reset(struct xhci_softc *, struct xhci_ring *);
struct	xhci_trb *xhci_ring_consume(struct xhci_softc *, struct xhci_ring *);
struct	xhci_trb *xhci_ring_produce(struct xhci_softc *, struct xhci_ring *);

struct	xhci_trb *xhci_xfer_get_trb(struct xhci_softc *, struct usbd_xfer*,
	    uint8_t *, int);
void	xhci_xfer_done(struct usbd_xfer *xfer);
/* xHCI command helpers. */
int	xhci_command_submit(struct xhci_softc *, struct xhci_trb *, int);
int	xhci_command_abort(struct xhci_softc *);

void	xhci_cmd_reset_ep_async(struct xhci_softc *, uint8_t, uint8_t);
void	xhci_cmd_set_tr_deq_async(struct xhci_softc *, uint8_t, uint8_t, uint64_t);
int	xhci_cmd_configure_ep(struct xhci_softc *, uint8_t, uint64_t);
int	xhci_cmd_stop_ep(struct xhci_softc *, uint8_t, uint8_t);
int	xhci_cmd_slot_control(struct xhci_softc *, uint8_t *, int);
int	xhci_cmd_set_address(struct xhci_softc *, uint8_t,  uint64_t, uint32_t);
#ifdef XHCI_DEBUG
int	xhci_cmd_noop(struct xhci_softc *);
#endif

/* XXX should be part of the Bus interface. */
void	xhci_abort_xfer(struct usbd_xfer *, usbd_status);
void	xhci_pipe_close(struct usbd_pipe *);
void	xhci_noop(struct usbd_xfer *);

void 	xhci_timeout(void *);
void	xhci_timeout_task(void *);

/* USBD Bus Interface. */
usbd_status	  xhci_pipe_open(struct usbd_pipe *);
int		  xhci_setaddr(struct usbd_device *, int);
void		  xhci_softintr(void *);
void		  xhci_poll(struct usbd_bus *);
struct usbd_xfer *xhci_allocx(struct usbd_bus *);
void		  xhci_freex(struct usbd_bus *, struct usbd_xfer *);

usbd_status	  xhci_root_ctrl_transfer(struct usbd_xfer *);
usbd_status	  xhci_root_ctrl_start(struct usbd_xfer *);

usbd_status	  xhci_root_intr_transfer(struct usbd_xfer *);
usbd_status	  xhci_root_intr_start(struct usbd_xfer *);
void		  xhci_root_intr_abort(struct usbd_xfer *);
void		  xhci_root_intr_done(struct usbd_xfer *);

usbd_status	  xhci_device_ctrl_transfer(struct usbd_xfer *);
usbd_status	  xhci_device_ctrl_start(struct usbd_xfer *);
void		  xhci_device_ctrl_abort(struct usbd_xfer *);

usbd_status	  xhci_device_generic_transfer(struct usbd_xfer *);
usbd_status	  xhci_device_generic_start(struct usbd_xfer *);
void		  xhci_device_generic_abort(struct usbd_xfer *);
void		  xhci_device_generic_done(struct usbd_xfer *);

usbd_status	  xhci_device_isoc_transfer(struct usbd_xfer *);
usbd_status	  xhci_device_isoc_start(struct usbd_xfer *);

#define XHCI_INTR_ENDPT 1

const struct usbd_bus_methods xhci_bus_methods = {
	.open_pipe = xhci_pipe_open,
	.dev_setaddr = xhci_setaddr,
	.soft_intr = xhci_softintr,
	.do_poll = xhci_poll,
	.allocx = xhci_allocx,
	.freex = xhci_freex,
};

const struct usbd_pipe_methods xhci_root_ctrl_methods = {
	.transfer = xhci_root_ctrl_transfer,
	.start = xhci_root_ctrl_start,
	.abort = xhci_noop,
	.close = xhci_pipe_close,
	.done = xhci_noop,
};

const struct usbd_pipe_methods xhci_root_intr_methods = {
	.transfer = xhci_root_intr_transfer,
	.start = xhci_root_intr_start,
	.abort = xhci_root_intr_abort,
	.close = xhci_pipe_close,
	.done = xhci_root_intr_done,
};

const struct usbd_pipe_methods xhci_device_ctrl_methods = {
	.transfer = xhci_device_ctrl_transfer,
	.start = xhci_device_ctrl_start,
	.abort = xhci_device_ctrl_abort,
	.close = xhci_pipe_close,
	.done = xhci_noop,
};

const struct usbd_pipe_methods xhci_device_intr_methods = {
	.transfer = xhci_device_generic_transfer,
	.start = xhci_device_generic_start,
	.abort = xhci_device_generic_abort,
	.close = xhci_pipe_close,
	.done = xhci_device_generic_done,
};

const struct usbd_pipe_methods xhci_device_bulk_methods = {
	.transfer = xhci_device_generic_transfer,
	.start = xhci_device_generic_start,
	.abort = xhci_device_generic_abort,
	.close = xhci_pipe_close,
	.done = xhci_device_generic_done,
};

const struct usbd_pipe_methods xhci_device_isoc_methods = {
	.transfer = xhci_device_isoc_transfer,
	.start = xhci_device_isoc_start,
	.abort = xhci_device_generic_abort,
	.close = xhci_pipe_close,
	.done = xhci_noop,
};

#ifdef XHCI_DEBUG
static void
xhci_dump_trb(struct xhci_trb *trb)
{
	printf("trb=%p (0x%016llx 0x%08x 0x%b)\n", trb,
	    (long long)letoh64(trb->trb_paddr), letoh32(trb->trb_status),
	    (int)letoh32(trb->trb_flags), XHCI_TRB_FLAGS_BITMASK);
}
#endif

int	usbd_dma_contig_alloc(struct usbd_bus *, struct usbd_dma_info *,
	    void **, bus_size_t, bus_size_t, bus_size_t);
void	usbd_dma_contig_free(struct usbd_bus *, struct usbd_dma_info *);

int
usbd_dma_contig_alloc(struct usbd_bus *bus, struct usbd_dma_info *dma,
    void **kvap, bus_size_t size, bus_size_t alignment, bus_size_t boundary)
{
	int error;

	dma->tag = bus->dmatag;
	dma->size = size;

	error = bus_dmamap_create(dma->tag, size, 1, size, boundary,
	    BUS_DMA_NOWAIT | bus->dmaflags, &dma->map);
	if (error != 0)
		return (error);

	error = bus_dmamem_alloc(dma->tag, size, alignment, boundary, &dma->seg,
	    1, &dma->nsegs, BUS_DMA_NOWAIT | BUS_DMA_ZERO | bus->dmaflags);
	if (error != 0)
		goto destroy;

	error = bus_dmamem_map(dma->tag, &dma->seg, 1, size, &dma->vaddr,
	    BUS_DMA_NOWAIT | BUS_DMA_COHERENT);
	if (error != 0)
		goto free;

	error = bus_dmamap_load_raw(dma->tag, dma->map, &dma->seg, 1, size,
	    BUS_DMA_NOWAIT);
	if (error != 0)
		goto unmap;

	bus_dmamap_sync(dma->tag, dma->map, 0, size, BUS_DMASYNC_PREREAD |
	    BUS_DMASYNC_PREWRITE);

	dma->paddr = dma->map->dm_segs[0].ds_addr;
	if (kvap != NULL)
		*kvap = dma->vaddr;

	return (0);

unmap:
	bus_dmamem_unmap(dma->tag, dma->vaddr, size);
free:
	bus_dmamem_free(dma->tag, &dma->seg, 1);
destroy:
	bus_dmamap_destroy(dma->tag, dma->map);
	return (error);
}

void
usbd_dma_contig_free(struct usbd_bus *bus, struct usbd_dma_info *dma)
{
	if (dma->map != NULL) {
		bus_dmamap_sync(bus->dmatag, dma->map, 0, dma->size,
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(bus->dmatag, dma->map);
		bus_dmamem_unmap(bus->dmatag, dma->vaddr, dma->size);
		bus_dmamem_free(bus->dmatag, &dma->seg, 1);
		bus_dmamap_destroy(bus->dmatag, dma->map);
		dma->map = NULL;
	}
}

int
xhci_init(struct xhci_softc *sc)
{
	uint32_t hcr;
	int npage, error;

	sc->sc_bus.usbrev = USBREV_3_0;
	sc->sc_bus.methods = &xhci_bus_methods;
	sc->sc_bus.pipe_size = sizeof(struct xhci_pipe);

	sc->sc_oper_off = XREAD1(sc, XHCI_CAPLENGTH);
	sc->sc_door_off = XREAD4(sc, XHCI_DBOFF);
	sc->sc_runt_off = XREAD4(sc, XHCI_RTSOFF);

	sc->sc_version = XREAD2(sc, XHCI_HCIVERSION);
	printf(", xHCI %x.%x\n", sc->sc_version >> 8, sc->sc_version & 0xff);

#ifdef XHCI_DEBUG
	printf("%s: CAPLENGTH=%#lx\n", DEVNAME(sc), sc->sc_oper_off);
	printf("%s: DOORBELL=%#lx\n", DEVNAME(sc), sc->sc_door_off);
	printf("%s: RUNTIME=%#lx\n", DEVNAME(sc), sc->sc_runt_off);
#endif

	error = xhci_reset(sc);
	if (error)
		return (error);

	if (xhcixfer == NULL) {
		xhcixfer = malloc(sizeof(struct pool), M_USBHC, M_NOWAIT);
		if (xhcixfer == NULL) {
			printf("%s: unable to allocate pool descriptor\n",
			    DEVNAME(sc));
			return (ENOMEM);
		}
		pool_init(xhcixfer, sizeof(struct xhci_xfer), 0, IPL_SOFTUSB,
		    0, "xhcixfer", NULL);
	}

	hcr = XREAD4(sc, XHCI_HCCPARAMS);
	sc->sc_ctxsize = XHCI_HCC_CSZ(hcr) ? 64 : 32;
	sc->sc_bus.dmaflags |= XHCI_HCC_AC64(hcr) ? BUS_DMA_64BIT : 0;
	DPRINTF(("%s: %d bytes context\n", DEVNAME(sc), sc->sc_ctxsize));

#ifdef XHCI_DEBUG
	hcr = XOREAD4(sc, XHCI_PAGESIZE);
	printf("%s: supported page size 0x%08x\n", DEVNAME(sc), hcr);
#endif
	/* Use 4K for the moment since it's easier. */
	sc->sc_pagesize = 4096;

	/* Get port and device slot numbers. */
	hcr = XREAD4(sc, XHCI_HCSPARAMS1);
	sc->sc_noport = XHCI_HCS1_N_PORTS(hcr);
	sc->sc_noslot = XHCI_HCS1_DEVSLOT_MAX(hcr);
	DPRINTF(("%s: %d ports and %d slots\n", DEVNAME(sc), sc->sc_noport,
	    sc->sc_noslot));

	/* Setup Device Context Base Address Array. */
	error = usbd_dma_contig_alloc(&sc->sc_bus, &sc->sc_dcbaa.dma,
	    (void **)&sc->sc_dcbaa.segs, (sc->sc_noslot + 1) * sizeof(uint64_t),
	    XHCI_DCBAA_ALIGN, sc->sc_pagesize);
	if (error)
		return (ENOMEM);

	/* Setup command ring. */
	rw_init(&sc->sc_cmd_lock, "xhcicmd");
	error = xhci_ring_alloc(sc, &sc->sc_cmd_ring, XHCI_MAX_CMDS,
	    XHCI_CMDS_RING_ALIGN);
	if (error) {
		printf("%s: could not allocate command ring.\n", DEVNAME(sc));
		usbd_dma_contig_free(&sc->sc_bus, &sc->sc_dcbaa.dma);
		return (error);
	}

	/* Setup one event ring and its segment table (ERST). */
	error = xhci_ring_alloc(sc, &sc->sc_evt_ring, XHCI_MAX_EVTS,
	    XHCI_EVTS_RING_ALIGN);
	if (error) {
		printf("%s: could not allocate event ring.\n", DEVNAME(sc));
		xhci_ring_free(sc, &sc->sc_cmd_ring);
		usbd_dma_contig_free(&sc->sc_bus, &sc->sc_dcbaa.dma);
		return (error);
	}

	/* Allocate the required entry for the segment table. */
	error = usbd_dma_contig_alloc(&sc->sc_bus, &sc->sc_erst.dma,
	    (void **)&sc->sc_erst.segs, sizeof(struct xhci_erseg),
	    XHCI_ERST_ALIGN, XHCI_ERST_BOUNDARY);
	if (error) {
		printf("%s: could not allocate segment table.\n", DEVNAME(sc));
		xhci_ring_free(sc, &sc->sc_evt_ring);
		xhci_ring_free(sc, &sc->sc_cmd_ring);
		usbd_dma_contig_free(&sc->sc_bus, &sc->sc_dcbaa.dma);
		return (ENOMEM);
	}

	/* Set our ring address and size in its corresponding segment. */
	sc->sc_erst.segs[0].er_addr = htole64(sc->sc_evt_ring.dma.paddr);
	sc->sc_erst.segs[0].er_size = htole32(XHCI_MAX_EVTS);
	sc->sc_erst.segs[0].er_rsvd = 0;
	bus_dmamap_sync(sc->sc_erst.dma.tag, sc->sc_erst.dma.map, 0,
	    sc->sc_erst.dma.size, BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	/* Get the number of scratch pages and configure them if necessary. */
	hcr = XREAD4(sc, XHCI_HCSPARAMS2);
	npage = XHCI_HCS2_SPB_MAX(hcr);
	DPRINTF(("%s: %u scratch pages, ETE=%u, IST=0x%x\n", DEVNAME(sc), npage,
	   XHCI_HCS2_ETE(hcr), XHCI_HCS2_IST(hcr)));

	if (npage > 0 && xhci_scratchpad_alloc(sc, npage)) {
		printf("%s: could not allocate scratchpad.\n", DEVNAME(sc));
		usbd_dma_contig_free(&sc->sc_bus, &sc->sc_erst.dma);
		xhci_ring_free(sc, &sc->sc_evt_ring);
		xhci_ring_free(sc, &sc->sc_cmd_ring);
		usbd_dma_contig_free(&sc->sc_bus, &sc->sc_dcbaa.dma);
		return (ENOMEM);
	}


	return (0);
}

void
xhci_config(struct xhci_softc *sc)
{
	uint64_t paddr;
	uint32_t hcr;
	int i;

	/* Make sure to program a number of device slots we can handle. */
	if (sc->sc_noslot > USB_MAX_DEVICES)
		sc->sc_noslot = USB_MAX_DEVICES;
	hcr = XOREAD4(sc, XHCI_CONFIG) & ~XHCI_CONFIG_SLOTS_MASK;
	XOWRITE4(sc, XHCI_CONFIG, hcr | sc->sc_noslot);

	/* Set the device context base array address. */
	paddr = (uint64_t)sc->sc_dcbaa.dma.paddr;
	XOWRITE4(sc, XHCI_DCBAAP_LO, (uint32_t)paddr);
	XOWRITE4(sc, XHCI_DCBAAP_HI, (uint32_t)(paddr >> 32));

	DPRINTF(("%s: DCBAAP=%#x%#x\n", DEVNAME(sc),
	    XOREAD4(sc, XHCI_DCBAAP_HI), XOREAD4(sc, XHCI_DCBAAP_LO)));

	/* Set the command ring address. */
	paddr = (uint64_t)sc->sc_cmd_ring.dma.paddr;
	XOWRITE4(sc, XHCI_CRCR_LO, ((uint32_t)paddr) | XHCI_CRCR_LO_RCS);
	XOWRITE4(sc, XHCI_CRCR_HI, (uint32_t)(paddr >> 32));

	DPRINTF(("%s: CRCR=%#x%#x (%016llx)\n", DEVNAME(sc),
	    XOREAD4(sc, XHCI_CRCR_HI), XOREAD4(sc, XHCI_CRCR_LO), paddr));

	/* Set the ERST count number to 1, since we use only one event ring. */
	XRWRITE4(sc, XHCI_ERSTSZ(0), XHCI_ERSTS_SET(1));

	/* Set the segment table address. */
	paddr = (uint64_t)sc->sc_erst.dma.paddr;
	XRWRITE4(sc, XHCI_ERSTBA_LO(0), (uint32_t)paddr);
	XRWRITE4(sc, XHCI_ERSTBA_HI(0), (uint32_t)(paddr >> 32));

	DPRINTF(("%s: ERSTBA=%#x%#x\n", DEVNAME(sc),
	    XRREAD4(sc, XHCI_ERSTBA_HI(0)), XRREAD4(sc, XHCI_ERSTBA_LO(0))));

	/* Set the ring dequeue address. */
	paddr = (uint64_t)sc->sc_evt_ring.dma.paddr;
	XRWRITE4(sc, XHCI_ERDP_LO(0), (uint32_t)paddr);
	XRWRITE4(sc, XHCI_ERDP_HI(0), (uint32_t)(paddr >> 32));

	DPRINTF(("%s: ERDP=%#x%#x\n", DEVNAME(sc),
	    XRREAD4(sc, XHCI_ERDP_HI(0)), XRREAD4(sc, XHCI_ERDP_LO(0))));

	/*
	 * If we successfully saved the state during suspend, restore
	 * it here.  Otherwise some Intel controllers don't function
	 * correctly after resume.
	 */
	if (sc->sc_saved_state) {
		XOWRITE4(sc, XHCI_USBCMD, XHCI_CMD_CRS); /* Restore state */
		hcr = XOREAD4(sc, XHCI_USBSTS);
		for (i = 0; i < 100; i++) {
			usb_delay_ms(&sc->sc_bus, 1);
			hcr = XOREAD4(sc, XHCI_USBSTS) & XHCI_STS_RSS;
			if (!hcr)
				break;
		}

		if (hcr)
			printf("%s: restore state timeout\n", DEVNAME(sc));

		sc->sc_saved_state = 0;
	}

	/* Enable interrupts. */
	hcr = XRREAD4(sc, XHCI_IMAN(0));
	XRWRITE4(sc, XHCI_IMAN(0), hcr | XHCI_IMAN_INTR_ENA);

	/* Set default interrupt moderation. */
	XRWRITE4(sc, XHCI_IMOD(0), XHCI_IMOD_DEFAULT);

	/* Allow event interrupt and start the controller. */
	XOWRITE4(sc, XHCI_USBCMD, XHCI_CMD_INTE|XHCI_CMD_RS);

	DPRINTF(("%s: USBCMD=%#x\n", DEVNAME(sc), XOREAD4(sc, XHCI_USBCMD)));
	DPRINTF(("%s: IMAN=%#x\n", DEVNAME(sc), XRREAD4(sc, XHCI_IMAN(0))));
}

int
xhci_detach(struct device *self, int flags)
{
	struct xhci_softc *sc = (struct xhci_softc *)self;
	int rv;

	rv = config_detach_children(self, flags);
	if (rv != 0) {
		printf("%s: error while detaching %d\n", DEVNAME(sc), rv);
		return (rv);
	}

	/* Since the hardware might already be gone, ignore the errors. */
	xhci_command_abort(sc);

	xhci_reset(sc);

	/* Disable interrupts. */
	XRWRITE4(sc, XHCI_IMOD(0), 0);
	XRWRITE4(sc, XHCI_IMAN(0), 0);

	/* Clear the event ring address. */
	XRWRITE4(sc, XHCI_ERDP_LO(0), 0);
	XRWRITE4(sc, XHCI_ERDP_HI(0), 0);

	XRWRITE4(sc, XHCI_ERSTBA_LO(0), 0);
	XRWRITE4(sc, XHCI_ERSTBA_HI(0), 0);

	XRWRITE4(sc, XHCI_ERSTSZ(0), 0);

	/* Clear the command ring address. */
	XOWRITE4(sc, XHCI_CRCR_LO, 0);
	XOWRITE4(sc, XHCI_CRCR_HI, 0);

	XOWRITE4(sc, XHCI_DCBAAP_LO, 0);
	XOWRITE4(sc, XHCI_DCBAAP_HI, 0);

	if (sc->sc_spad.npage > 0)
		xhci_scratchpad_free(sc);

	usbd_dma_contig_free(&sc->sc_bus, &sc->sc_erst.dma);
	xhci_ring_free(sc, &sc->sc_evt_ring);
	xhci_ring_free(sc, &sc->sc_cmd_ring);
	usbd_dma_contig_free(&sc->sc_bus, &sc->sc_dcbaa.dma);

	return (0);
}

int
xhci_activate(struct device *self, int act)
{
	struct xhci_softc *sc = (struct xhci_softc *)self;
	int rv = 0;

	switch (act) {
	case DVACT_RESUME:
		sc->sc_bus.use_polling++;
		xhci_reinit(sc);
		sc->sc_bus.use_polling--;
		rv = config_activate_children(self, act);
		break;
	case DVACT_POWERDOWN:
		rv = config_activate_children(self, act);
		xhci_suspend(sc);
		break;
	default:
		rv = config_activate_children(self, act);
		break;
	}

	return (rv);
}

int
xhci_reset(struct xhci_softc *sc)
{
	uint32_t hcr;
	int i;

	XOWRITE4(sc, XHCI_USBCMD, 0);	/* Halt controller */
	for (i = 0; i < 100; i++) {
		usb_delay_ms(&sc->sc_bus, 1);
		hcr = XOREAD4(sc, XHCI_USBSTS) & XHCI_STS_HCH;
		if (hcr)
			break;
	}

	if (!hcr)
		printf("%s: halt timeout\n", DEVNAME(sc));

	XOWRITE4(sc, XHCI_USBCMD, XHCI_CMD_HCRST);
	for (i = 0; i < 100; i++) {
		usb_delay_ms(&sc->sc_bus, 1);
		hcr = (XOREAD4(sc, XHCI_USBCMD) & XHCI_CMD_HCRST) |
		    (XOREAD4(sc, XHCI_USBSTS) & XHCI_STS_CNR);
		if (!hcr)
			break;
	}

	if (hcr) {
		printf("%s: reset timeout\n", DEVNAME(sc));
		return (EIO);
	}

	return (0);
}

void
xhci_suspend(struct xhci_softc *sc)
{
	uint32_t hcr;
	int i;

	XOWRITE4(sc, XHCI_USBCMD, 0);	/* Halt controller */
	for (i = 0; i < 100; i++) {
		usb_delay_ms(&sc->sc_bus, 1);
		hcr = XOREAD4(sc, XHCI_USBSTS) & XHCI_STS_HCH;
		if (hcr)
			break;
	}

	if (!hcr) {
		printf("%s: halt timeout\n", DEVNAME(sc));
		xhci_reset(sc);
		return;
	}

	/*
	 * Some Intel controllers will not power down completely
	 * unless they have seen a save state command.  This in turn
	 * will prevent the SoC from reaching its lowest idle state.
	 * So save the state here.
	 */
	if ((sc->sc_flags & XHCI_NOCSS) == 0) {
		XOWRITE4(sc, XHCI_USBCMD, XHCI_CMD_CSS); /* Save state */
		hcr = XOREAD4(sc, XHCI_USBSTS);
		for (i = 0; i < 100; i++) {
			usb_delay_ms(&sc->sc_bus, 1);
			hcr = XOREAD4(sc, XHCI_USBSTS) & XHCI_STS_SSS;
			if (!hcr)
				break;
		}

		if (hcr) {
			printf("%s: save state timeout\n", DEVNAME(sc));
			xhci_reset(sc);
			return;
		}

		sc->sc_saved_state = 1;
	}

	/* Disable interrupts. */
	XRWRITE4(sc, XHCI_IMOD(0), 0);
	XRWRITE4(sc, XHCI_IMAN(0), 0);

	/* Clear the event ring address. */
	XRWRITE4(sc, XHCI_ERDP_LO(0), 0);
	XRWRITE4(sc, XHCI_ERDP_HI(0), 0);

	XRWRITE4(sc, XHCI_ERSTBA_LO(0), 0);
	XRWRITE4(sc, XHCI_ERSTBA_HI(0), 0);

	XRWRITE4(sc, XHCI_ERSTSZ(0), 0);

	/* Clear the command ring address. */
	XOWRITE4(sc, XHCI_CRCR_LO, 0);
	XOWRITE4(sc, XHCI_CRCR_HI, 0);

	XOWRITE4(sc, XHCI_DCBAAP_LO, 0);
	XOWRITE4(sc, XHCI_DCBAAP_HI, 0);
}

void
xhci_reinit(struct xhci_softc *sc)
{
	xhci_reset(sc);
	xhci_ring_reset(sc, &sc->sc_cmd_ring);
	xhci_ring_reset(sc, &sc->sc_evt_ring);

	/* Renesas controllers, at least, need more time to resume. */
	usb_delay_ms(&sc->sc_bus, USB_RESUME_WAIT);

	xhci_config(sc);
}

int
xhci_intr(void *v)
{
	struct xhci_softc *sc = v;

	if (sc->sc_dead)
		return (0);

	/* If we get an interrupt while polling, then just ignore it. */
	if (sc->sc_bus.use_polling) {
		DPRINTFN(16, ("xhci_intr: ignored interrupt while polling\n"));
		return (0);
	}

	return (xhci_intr1(sc));
}

int
xhci_intr1(struct xhci_softc *sc)
{
	uint32_t intrs;

	intrs = XOREAD4(sc, XHCI_USBSTS);
	if (intrs == 0xffffffff) {
		sc->sc_bus.dying = 1;
		sc->sc_dead = 1;
		return (0);
	}

	if ((intrs & XHCI_STS_EINT) == 0)
		return (0);

	sc->sc_bus.no_intrs++;

	if (intrs & XHCI_STS_HSE) {
		printf("%s: host system error\n", DEVNAME(sc));
		sc->sc_bus.dying = 1;
		XOWRITE4(sc, XHCI_USBSTS, intrs);
		return (1);
	}

	/* Acknowledge interrupts */
	XOWRITE4(sc, XHCI_USBSTS, intrs);
	intrs = XRREAD4(sc, XHCI_IMAN(0));
	XRWRITE4(sc, XHCI_IMAN(0), intrs | XHCI_IMAN_INTR_PEND);

	usb_schedsoftintr(&sc->sc_bus);

	return (1);
}

void
xhci_poll(struct usbd_bus *bus)
{
	struct xhci_softc *sc = (struct xhci_softc *)bus;

	if (XOREAD4(sc, XHCI_USBSTS))
		xhci_intr1(sc);
}

void
xhci_softintr(void *v)
{
	struct xhci_softc *sc = v;

	if (sc->sc_bus.dying)
		return;

	sc->sc_bus.intr_context++;
	xhci_event_dequeue(sc);
	sc->sc_bus.intr_context--;
}

void
xhci_event_dequeue(struct xhci_softc *sc)
{
	struct xhci_trb *trb;
	uint64_t paddr;
	uint32_t status, flags;

	while ((trb = xhci_ring_consume(sc, &sc->sc_evt_ring)) != NULL) {
		paddr = letoh64(trb->trb_paddr);
		status = letoh32(trb->trb_status);
		flags = letoh32(trb->trb_flags);

		switch (flags & XHCI_TRB_TYPE_MASK) {
		case XHCI_EVT_XFER:
			xhci_event_xfer(sc, paddr, status, flags);
			break;
		case XHCI_EVT_CMD_COMPLETE:
			memcpy(&sc->sc_result_trb, trb, sizeof(*trb));
			xhci_event_command(sc, paddr);
			break;
		case XHCI_EVT_PORT_CHANGE:
			xhci_event_port_change(sc, paddr, status);
			break;
		case XHCI_EVT_HOST_CTRL:
			/* TODO */
			break;
		default:
#ifdef XHCI_DEBUG
			printf("event (%d): ", XHCI_TRB_TYPE(flags));
			xhci_dump_trb(trb);
#endif
			break;
		}

	}

	paddr = (uint64_t)DEQPTR(sc->sc_evt_ring);
	XRWRITE4(sc, XHCI_ERDP_LO(0), ((uint32_t)paddr) | XHCI_ERDP_LO_BUSY);
	XRWRITE4(sc, XHCI_ERDP_HI(0), (uint32_t)(paddr >> 32));
}

void
xhci_skip_all(struct xhci_pipe *xp)
{
	struct usbd_xfer *xfer, *last;

	if (xp->skip) {
		/*
		 * Find the last transfer to skip, this is necessary
		 * as xhci_xfer_done() posts new transfers which we
		 * don't want to skip
		 */
		last = SIMPLEQ_FIRST(&xp->pipe.queue);
		if (last == NULL)
			goto done;
		while ((xfer = SIMPLEQ_NEXT(last, next)) != NULL)
			last = xfer;

		do {
			xfer = SIMPLEQ_FIRST(&xp->pipe.queue);
			if (xfer == NULL)
				goto done;
			DPRINTF(("%s: skipping %p\n", __func__, xfer));
			xfer->status = USBD_NORMAL_COMPLETION;
			xhci_xfer_done(xfer);
		} while (xfer != last);
	done:
		xp->skip = 0;
	}
}

void
xhci_event_xfer(struct xhci_softc *sc, uint64_t paddr, uint32_t status,
    uint32_t flags)
{
	struct xhci_pipe *xp;
	struct usbd_xfer *xfer;
	uint8_t dci, slot, code, xfertype;
	uint32_t remain;
	int trb_idx;

	slot = XHCI_TRB_GET_SLOT(flags);
	dci = XHCI_TRB_GET_EP(flags);
	if (slot > sc->sc_noslot) {
		DPRINTF(("%s: incorrect slot (%u)\n", DEVNAME(sc), slot));
		return;
	}

	xp = sc->sc_sdevs[slot].pipes[dci - 1];
	if (xp == NULL) {
		DPRINTF(("%s: incorrect dci (%u)\n", DEVNAME(sc), dci));
		return;
	}

	code = XHCI_TRB_GET_CODE(status);
	remain = XHCI_TRB_REMAIN(status);

	switch (code) {
	case XHCI_CODE_RING_UNDERRUN:
		DPRINTF(("%s: slot %u underrun with %zu TRB\n", DEVNAME(sc),
		    slot, xp->ring.ntrb - xp->free_trbs));
		xhci_skip_all(xp);
		return;
	case XHCI_CODE_RING_OVERRUN:
		DPRINTF(("%s: slot %u overrun with %zu TRB\n", DEVNAME(sc),
		    slot, xp->ring.ntrb - xp->free_trbs));
		xhci_skip_all(xp);
		return;
	case XHCI_CODE_MISSED_SRV:
		DPRINTF(("%s: slot %u missed srv with %zu TRB\n", DEVNAME(sc),
		    slot, xp->ring.ntrb - xp->free_trbs));
		xp->skip = 1;
		return;
	default:
		break;
	}

	trb_idx = (paddr - xp->ring.dma.paddr) / sizeof(struct xhci_trb);
	if (trb_idx < 0 || trb_idx >= xp->ring.ntrb) {
		printf("%s: wrong trb index (%u) max is %zu\n", DEVNAME(sc),
		    trb_idx, xp->ring.ntrb - 1);
		return;
	}

	xfer = xp->pending_xfers[trb_idx];
	if (xfer == NULL) {
		DPRINTF(("%s: NULL xfer pointer\n", DEVNAME(sc)));
		return;
	}

	if (remain > xfer->length)
		remain = xfer->length;

	xfertype = UE_GET_XFERTYPE(xfer->pipe->endpoint->edesc->bmAttributes);

	switch (xfertype) {
	case UE_BULK:
	case UE_INTERRUPT:
	case UE_CONTROL:
		if (xhci_event_xfer_generic(sc, xfer, xp, remain, trb_idx,
		    code, slot, dci))
			return;
		break;
	case UE_ISOCHRONOUS:
		if (xhci_event_xfer_isoc(xfer, xp, remain, trb_idx, code))
			return;
		break;
	default:
		panic("xhci_event_xfer: unknown xfer type %u", xfertype);
	}

	xhci_xfer_done(xfer);
}

uint32_t
xhci_xfer_length_generic(struct xhci_xfer *xx, struct xhci_pipe *xp,
    int trb_idx)
{
	int	 trb0_idx;
	uint32_t len = 0, type;

	trb0_idx =
	    ((xx->index + xp->ring.ntrb) - xx->ntrb) % (xp->ring.ntrb - 1);

	while (1) {
		type = letoh32(xp->ring.trbs[trb0_idx].trb_flags) &
		    XHCI_TRB_TYPE_MASK;
		if (type == XHCI_TRB_TYPE_NORMAL || type == XHCI_TRB_TYPE_DATA)
			len += XHCI_TRB_LEN(letoh32(
			    xp->ring.trbs[trb0_idx].trb_status));
		if (trb0_idx == trb_idx)
			break;
		if (++trb0_idx == xp->ring.ntrb)
			trb0_idx = 0;
	}
	return len;
}

int
xhci_event_xfer_generic(struct xhci_softc *sc, struct usbd_xfer *xfer,
    struct xhci_pipe *xp, uint32_t remain, int trb_idx,
    uint8_t code, uint8_t slot, uint8_t dci)
{
	struct xhci_xfer *xx = (struct xhci_xfer *)xfer;

	switch (code) {
	case XHCI_CODE_SUCCESS:
		if (xfer->actlen == 0) {
			if (remain)
				xfer->actlen =
				    xhci_xfer_length_generic(xx, xp, trb_idx) -
				    remain;
			else
				xfer->actlen = xfer->length;
		}
		if (xfer->actlen)
			usb_syncmem(&xfer->dmabuf, 0, xfer->actlen,
			    usbd_xfer_isread(xfer) ?
			    BUS_DMASYNC_POSTREAD : BUS_DMASYNC_POSTWRITE);
		xfer->status = USBD_NORMAL_COMPLETION;
		break;
	case XHCI_CODE_SHORT_XFER:
		/*
		 * Use values from the transfer TRB instead of the status TRB.
		 */
		if (xfer->actlen == 0)
			xfer->actlen =
			    xhci_xfer_length_generic(xx, xp, trb_idx) - remain;
		/*
		 * If this is not the last TRB of a transfer, we should
		 * theoretically clear the IOC at the end of the chain
		 * but the HC might have already processed it before we
		 * had a chance to schedule the softinterrupt.
		 */
		if (xx->index != trb_idx) {
			DPRINTF(("%s: short xfer %p for %u\n",
			    DEVNAME(sc), xfer, xx->index));
			return (1);
		}
		if (xfer->actlen)
			usb_syncmem(&xfer->dmabuf, 0, xfer->actlen,
			    usbd_xfer_isread(xfer) ?
			    BUS_DMASYNC_POSTREAD : BUS_DMASYNC_POSTWRITE);
		xfer->status = USBD_NORMAL_COMPLETION;
		break;
	case XHCI_CODE_TXERR:
	case XHCI_CODE_SPLITERR:
		DPRINTF(("%s: txerr? code %d\n", DEVNAME(sc), code));
		xfer->status = USBD_IOERROR;
		break;
	case XHCI_CODE_STALL:
	case XHCI_CODE_BABBLE:
		DPRINTF(("%s: babble code %d\n", DEVNAME(sc), code));
		/* Prevent any timeout to kick in. */
		timeout_del(&xfer->timeout_handle);
		usb_rem_task(xfer->device, &xfer->abort_task);

		/* We need to report this condition for umass(4). */
		if (code == XHCI_CODE_STALL)
			xp->halted = USBD_STALLED;
		else
			xp->halted = USBD_IOERROR;
		/*
		 * Since the stack might try to start a new transfer as
		 * soon as a pending one finishes, make sure the endpoint
		 * is fully reset before calling usb_transfer_complete().
		 */
		xp->aborted_xfer = xfer;
		xhci_cmd_reset_ep_async(sc, slot, dci);
		return (1);
	case XHCI_CODE_XFER_STOPPED:
	case XHCI_CODE_XFER_STOPINV:
		/* Endpoint stopped while processing a TD. */
		if (xfer == xp->aborted_xfer) {
			DPRINTF(("%s: stopped xfer=%p\n", __func__, xfer));
		    	return (1);
		}

		/* FALLTHROUGH */
	default:
		DPRINTF(("%s: unhandled code %d\n", DEVNAME(sc), code));
		xfer->status = USBD_IOERROR;
		xp->halted = 1;
		break;
	}

	return (0);
}

int
xhci_event_xfer_isoc(struct usbd_xfer *xfer, struct xhci_pipe *xp,
    uint32_t remain, int trb_idx, uint8_t code)
{
	struct usbd_xfer *skipxfer;
	struct xhci_xfer *xx = (struct xhci_xfer *)xfer;
	int trb0_idx, frame_idx = 0, skip_trb = 0;

	KASSERT(xx->index >= 0);

	switch (code) {
	case XHCI_CODE_SHORT_XFER:
		xp->trb_processed[trb_idx] = TRB_PROCESSED_SHORT;
		break;
	default:
		xp->trb_processed[trb_idx] = TRB_PROCESSED_YES;
		break;
	}

	trb0_idx =
	    ((xx->index + xp->ring.ntrb) - xx->ntrb) % (xp->ring.ntrb - 1);

	/* Find the according frame index for this TRB. */
	while (trb0_idx != trb_idx) {
		if ((letoh32(xp->ring.trbs[trb0_idx].trb_flags) &
		    XHCI_TRB_TYPE_MASK) == XHCI_TRB_TYPE_ISOCH)
			frame_idx++;
		if (trb0_idx++ == (xp->ring.ntrb - 1))
			trb0_idx = 0;
	}

	/*
	 * If we queued two TRBs for a frame and this is the second TRB,
	 * check if the first TRB needs accounting since it might not have
	 * raised an interrupt in case of full data received.
	 */
	if ((letoh32(xp->ring.trbs[trb_idx].trb_flags) & XHCI_TRB_TYPE_MASK) ==
	    XHCI_TRB_TYPE_NORMAL) {
		frame_idx--;
		if (trb_idx == 0)
			trb0_idx = xp->ring.ntrb - 2;
		else
			trb0_idx = trb_idx - 1;
		if (xp->trb_processed[trb0_idx] == TRB_PROCESSED_NO) {
			xfer->frlengths[frame_idx] = XHCI_TRB_LEN(letoh32(
			    xp->ring.trbs[trb0_idx].trb_status));
		} else if (xp->trb_processed[trb0_idx] == TRB_PROCESSED_SHORT) {
			skip_trb = 1;
		}
	}

	if (!skip_trb) {
		xfer->frlengths[frame_idx] +=
		    XHCI_TRB_LEN(letoh32(xp->ring.trbs[trb_idx].trb_status)) -
		    remain;
		xfer->actlen += xfer->frlengths[frame_idx];
	}

	if (xx->index != trb_idx)
		return (1);

	if (xp->skip) {
		while (1) {
			skipxfer = SIMPLEQ_FIRST(&xp->pipe.queue);
			if (skipxfer == xfer || skipxfer == NULL)
				break;
			DPRINTF(("%s: skipping %p\n", __func__, skipxfer));
			skipxfer->status = USBD_NORMAL_COMPLETION;
			xhci_xfer_done(skipxfer);
		}
		xp->skip = 0;
	}

	usb_syncmem(&xfer->dmabuf, 0, xfer->length,
	    usbd_xfer_isread(xfer) ?
	    BUS_DMASYNC_POSTREAD : BUS_DMASYNC_POSTWRITE);
	xfer->status = USBD_NORMAL_COMPLETION;

	return (0);
}

void
xhci_event_command(struct xhci_softc *sc, uint64_t paddr)
{
	struct xhci_trb *trb;
	struct xhci_pipe *xp;
	uint32_t flags;
	uint8_t dci, slot;
	int trb_idx, status;

	trb_idx = (paddr - sc->sc_cmd_ring.dma.paddr) / sizeof(*trb);
	if (trb_idx < 0 || trb_idx >= sc->sc_cmd_ring.ntrb) {
		printf("%s: wrong trb index (%u) max is %zu\n", DEVNAME(sc),
		    trb_idx, sc->sc_cmd_ring.ntrb - 1);
		return;
	}

	trb = &sc->sc_cmd_ring.trbs[trb_idx];

	bus_dmamap_sync(sc->sc_cmd_ring.dma.tag, sc->sc_cmd_ring.dma.map,
	    TRBOFF(&sc->sc_cmd_ring, trb), sizeof(struct xhci_trb),
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	flags = letoh32(trb->trb_flags);

	slot = XHCI_TRB_GET_SLOT(flags);
	dci = XHCI_TRB_GET_EP(flags);

	switch (flags & XHCI_TRB_TYPE_MASK) {
	case XHCI_CMD_RESET_EP:
		xp = sc->sc_sdevs[slot].pipes[dci - 1];
		if (xp == NULL)
			break;

		/* Update the dequeue pointer past the last TRB. */
		xhci_cmd_set_tr_deq_async(sc, xp->slot, xp->dci,
		    DEQPTR(xp->ring) | xp->ring.toggle);
		break;
	case XHCI_CMD_SET_TR_DEQ:
		xp = sc->sc_sdevs[slot].pipes[dci - 1];
		if (xp == NULL)
			break;

		status = xp->halted;
		xp->halted = 0;
		if (xp->aborted_xfer != NULL) {
			xp->aborted_xfer->status = status;
			xhci_xfer_done(xp->aborted_xfer);
			wakeup(xp);
		}
		break;
	case XHCI_CMD_CONFIG_EP:
	case XHCI_CMD_STOP_EP:
	case XHCI_CMD_DISABLE_SLOT:
	case XHCI_CMD_ENABLE_SLOT:
	case XHCI_CMD_ADDRESS_DEVICE:
	case XHCI_CMD_EVAL_CTX:
	case XHCI_CMD_NOOP:
		/*
		 * All these commands are synchronous.
		 *
		 * If TRBs differ, this could be a delayed result after we
		 * gave up waiting for the expected TRB due to timeout.
		 */
		if (sc->sc_cmd_trb == trb) {
			sc->sc_cmd_trb = NULL;
			wakeup(&sc->sc_cmd_trb);
		}
		break;
	default:
		DPRINTF(("%s: unexpected command %x\n", DEVNAME(sc), flags));
	}
}

void
xhci_event_port_change(struct xhci_softc *sc, uint64_t paddr, uint32_t status)
{
	struct usbd_xfer *xfer = sc->sc_intrxfer;
	uint32_t port = XHCI_TRB_PORTID(paddr);
	uint8_t *p;

	if (XHCI_TRB_GET_CODE(status) != XHCI_CODE_SUCCESS) {
		DPRINTF(("%s: failed port status event\n", DEVNAME(sc)));
		return;
	}

	if (xfer == NULL)
		return;

	p = KERNADDR(&xfer->dmabuf, 0);
	memset(p, 0, xfer->length);

	p[port/8] |= 1 << (port%8);
	DPRINTF(("%s: port=%d change=0x%02x\n", DEVNAME(sc), port, *p));

	xfer->actlen = xfer->length;
	xfer->status = USBD_NORMAL_COMPLETION;

	usb_transfer_complete(xfer);
}

void
xhci_xfer_done(struct usbd_xfer *xfer)
{
	struct xhci_pipe *xp = (struct xhci_pipe *)xfer->pipe;
	struct xhci_xfer *xx = (struct xhci_xfer *)xfer;
	int ntrb, i;

	splsoftassert(IPL_SOFTUSB);

#ifdef XHCI_DEBUG
	if (xx->index < 0 || xp->pending_xfers[xx->index] == NULL) {
		printf("%s: xfer=%p done (idx=%d, ntrb=%zd)\n", __func__,
		    xfer, xx->index, xx->ntrb);
	}
#endif

	if (xp->aborted_xfer == xfer)
		xp->aborted_xfer = NULL;

	for (ntrb = 0, i = xx->index; ntrb < xx->ntrb; ntrb++, i--) {
		xp->pending_xfers[i] = NULL;
		if (i == 0)
			i = (xp->ring.ntrb - 1);
	}
	xp->free_trbs += xx->ntrb;
	xp->free_trbs += xx->zerotd;
	xx->index = -1;
	xx->ntrb = 0;
	xx->zerotd = 0;

	timeout_del(&xfer->timeout_handle);
	usb_rem_task(xfer->device, &xfer->abort_task);
	usb_transfer_complete(xfer);
}

/*
 * Calculate the Device Context Index (DCI) for endpoints as stated
 * in section 4.5.1 of xHCI specification r1.1.
 */
static inline uint8_t
xhci_ed2dci(usb_endpoint_descriptor_t *ed)
{
	uint8_t dir;

	if (UE_GET_XFERTYPE(ed->bmAttributes) == UE_CONTROL)
		return (UE_GET_ADDR(ed->bEndpointAddress) * 2 + 1);

	if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN)
		dir = 1;
	else
		dir = 0;

	return (UE_GET_ADDR(ed->bEndpointAddress) * 2 + dir);
}

usbd_status
xhci_pipe_open(struct usbd_pipe *pipe)
{
	struct xhci_softc *sc = (struct xhci_softc *)pipe->device->bus;
	struct xhci_pipe *xp = (struct xhci_pipe *)pipe;
	usb_endpoint_descriptor_t *ed = pipe->endpoint->edesc;
	uint8_t slot = 0, xfertype = UE_GET_XFERTYPE(ed->bmAttributes);
	int error;

	KASSERT(xp->slot == 0);

	if (sc->sc_bus.dying)
		return (USBD_IOERROR);

	/* Root Hub */
	if (pipe->device->depth == 0) {
		switch (ed->bEndpointAddress) {
		case USB_CONTROL_ENDPOINT:
			pipe->methods = &xhci_root_ctrl_methods;
			break;
		case UE_DIR_IN | XHCI_INTR_ENDPT:
			pipe->methods = &xhci_root_intr_methods;
			break;
		default:
			pipe->methods = NULL;
			return (USBD_INVAL);
		}
		return (USBD_NORMAL_COMPLETION);
	}

#if 0
	/* Issue a noop to check if the command ring is correctly configured. */
	xhci_cmd_noop(sc);
#endif

	switch (xfertype) {
	case UE_CONTROL:
		pipe->methods = &xhci_device_ctrl_methods;

		/*
		 * Get a slot and init the device's contexts.
		 *
		 * Since the control endpoint, represented as the default
		 * pipe, is always opened first we are dealing with a
		 * new device.  Put a new slot in the ENABLED state.
		 *
		 */
		error = xhci_cmd_slot_control(sc, &slot, 1);
		if (error || slot == 0 || slot > sc->sc_noslot)
			return (USBD_INVAL);

		if (xhci_softdev_alloc(sc, slot)) {
			xhci_cmd_slot_control(sc, &slot, 0);
			return (USBD_NOMEM);
		}

		break;
	case UE_ISOCHRONOUS:
		pipe->methods = &xhci_device_isoc_methods;
		break;
	case UE_BULK:
		pipe->methods = &xhci_device_bulk_methods;
		break;
	case UE_INTERRUPT:
		pipe->methods = &xhci_device_intr_methods;
		break;
	default:
		return (USBD_INVAL);
	}

	/*
	 * Our USBD Bus Interface is pipe-oriented but for most of the
	 * operations we need to access a device context, so keep track
	 * of the slot ID in every pipe.
	 */
	if (slot == 0)
		slot = ((struct xhci_pipe *)pipe->device->default_pipe)->slot;

	xp->slot = slot;
	xp->dci = xhci_ed2dci(ed);

	if (xhci_pipe_init(sc, pipe)) {
		xhci_cmd_slot_control(sc, &slot, 0);
		return (USBD_IOERROR);
	}

	return (USBD_NORMAL_COMPLETION);
}

/*
 * Set the maximum Endpoint Service Interface Time (ESIT) payload and
 * the average TRB buffer length for an endpoint.
 */
static inline uint32_t
xhci_get_txinfo(struct xhci_softc *sc, struct usbd_pipe *pipe)
{
	usb_endpoint_descriptor_t *ed = pipe->endpoint->edesc;
	usb_endpoint_ss_comp_descriptor_t *esscd = pipe->endpoint->esscd;
	uint32_t mep, atl, mps = UGETW(ed->wMaxPacketSize);

	switch (UE_GET_XFERTYPE(ed->bmAttributes)) {
	case UE_CONTROL:
		mep = 0;
		atl = 8;
		break;
	case UE_INTERRUPT:
	case UE_ISOCHRONOUS:
		if (esscd && pipe->device->speed >= USB_SPEED_SUPER) {
			mep = UGETW(esscd->wBytesPerInterval);
			atl = mep;
			break;
		}

		mep = (UE_GET_TRANS(mps) + 1) * UE_GET_SIZE(mps);
		atl = mep;
		break;
	case UE_BULK:
	default:
		mep = 0;
		atl = 0;
	}

	return (XHCI_EPCTX_MAX_ESIT_PAYLOAD(mep) | XHCI_EPCTX_AVG_TRB_LEN(atl));
}

static inline uint32_t
xhci_linear_interval(usb_endpoint_descriptor_t *ed)
{
	uint32_t ival = min(max(1, ed->bInterval), 255);

	return (fls(ival) - 1);
}

static inline uint32_t
xhci_exponential_interval(usb_endpoint_descriptor_t *ed)
{
	uint32_t ival = min(max(1, ed->bInterval), 16);

	return (ival - 1);
}
/*
 * Return interval for endpoint expressed in 2^(ival) * 125us.
 *
 * See section 6.2.3.6 of xHCI r1.1 Specification for more details.
 */
uint32_t
xhci_pipe_interval(struct usbd_pipe *pipe)
{
	usb_endpoint_descriptor_t *ed = pipe->endpoint->edesc;
	uint8_t speed = pipe->device->speed;
	uint8_t xfertype = UE_GET_XFERTYPE(ed->bmAttributes);
	uint32_t ival;

	if (xfertype == UE_CONTROL || xfertype == UE_BULK) {
		/* Control and Bulk endpoints never NAKs. */
		ival = 0;
	} else {
		switch (speed) {
		case USB_SPEED_FULL:
			if (xfertype == UE_ISOCHRONOUS) {
				/* Convert 1-2^(15)ms into 3-18 */
				ival = xhci_exponential_interval(ed) + 3;
				break;
			}
			/* FALLTHROUGH */
		case USB_SPEED_LOW:
			/* Convert 1-255ms into 3-10 */
			ival = xhci_linear_interval(ed) + 3;
			break;
		case USB_SPEED_HIGH:
		case USB_SPEED_SUPER:
		default:
			/* Convert 1-2^(15) * 125us into 0-15 */
			ival = xhci_exponential_interval(ed);
			break;
		}
	}

	KASSERT(ival <= 15);
	return (XHCI_EPCTX_SET_IVAL(ival));
}

uint32_t
xhci_pipe_maxburst(struct usbd_pipe *pipe)
{
	usb_endpoint_descriptor_t *ed = pipe->endpoint->edesc;
	usb_endpoint_ss_comp_descriptor_t *esscd = pipe->endpoint->esscd;
	uint32_t mps = UGETW(ed->wMaxPacketSize);
	uint8_t xfertype = UE_GET_XFERTYPE(ed->bmAttributes);
	uint32_t maxb = 0;

	switch (pipe->device->speed) {
	case USB_SPEED_HIGH:
		if (xfertype == UE_ISOCHRONOUS || xfertype == UE_INTERRUPT)
			maxb = UE_GET_TRANS(mps);
		break;
	case USB_SPEED_SUPER:
		if (esscd &&
		    (xfertype == UE_ISOCHRONOUS || xfertype == UE_INTERRUPT))
			maxb = esscd->bMaxBurst;
	default:
		break;
	}

	return (maxb);
}

static inline uint32_t
xhci_last_valid_dci(struct xhci_pipe **pipes, struct xhci_pipe *ignore)
{
	struct xhci_pipe *lxp;
	int i;

	/* Find the last valid Endpoint Context. */
	for (i = 30; i >= 0; i--) {
		lxp = pipes[i];
		if (lxp != NULL && lxp != ignore)
			return XHCI_SCTX_DCI(lxp->dci);
	}

	return 0;
}

int
xhci_context_setup(struct xhci_softc *sc, struct usbd_pipe *pipe)
{
	struct xhci_pipe *xp = (struct xhci_pipe *)pipe;
	struct xhci_soft_dev *sdev = &sc->sc_sdevs[xp->slot];
	usb_endpoint_descriptor_t *ed = pipe->endpoint->edesc;
	uint32_t mps = UGETW(ed->wMaxPacketSize);
	uint8_t xfertype = UE_GET_XFERTYPE(ed->bmAttributes);
	uint8_t speed, cerr = 0;
	uint32_t route = 0, rhport = 0;
	struct usbd_device *hub;

	/*
	 * Calculate the Route String.  Assume that there is no hub with
	 * more than 15 ports and that they all have a detph < 6.  See
	 * section 8.9 of USB 3.1 Specification for more details.
	 */
	for (hub = pipe->device; hub->myhub->depth; hub = hub->myhub) {
		uint32_t port = hub->powersrc->portno;
		uint32_t depth = hub->myhub->depth;

		route |= port << (4 * (depth - 1));
	}

	/* Get Root Hub port */
	rhport = hub->powersrc->portno;

	switch (pipe->device->speed) {
	case USB_SPEED_LOW:
		speed = XHCI_SPEED_LOW;
		break;
	case USB_SPEED_FULL:
		speed = XHCI_SPEED_FULL;
		break;
	case USB_SPEED_HIGH:
		speed = XHCI_SPEED_HIGH;
		break;
	case USB_SPEED_SUPER:
		speed = XHCI_SPEED_SUPER;
		break;
	default:
		return (USBD_INVAL);
	}

	/* Setup the endpoint context */
	if (xfertype != UE_ISOCHRONOUS)
		cerr = 3;

	if ((ed->bEndpointAddress & UE_DIR_IN) || (xfertype == UE_CONTROL))
		xfertype |= 0x4;

	sdev->ep_ctx[xp->dci-1]->info_lo = htole32(xhci_pipe_interval(pipe));
	sdev->ep_ctx[xp->dci-1]->info_hi = htole32(
	    XHCI_EPCTX_SET_MPS(UE_GET_SIZE(mps)) |
	    XHCI_EPCTX_SET_MAXB(xhci_pipe_maxburst(pipe)) |
	    XHCI_EPCTX_SET_EPTYPE(xfertype) | XHCI_EPCTX_SET_CERR(cerr)
	);
	sdev->ep_ctx[xp->dci-1]->txinfo = htole32(xhci_get_txinfo(sc, pipe));
	sdev->ep_ctx[xp->dci-1]->deqp = htole64(
	    DEQPTR(xp->ring) | xp->ring.toggle
	);

	/* Unmask the new endpoint */
	sdev->input_ctx->drop_flags = 0;
	sdev->input_ctx->add_flags = htole32(XHCI_INCTX_MASK_DCI(xp->dci));

	/* Setup the slot context */
	sdev->slot_ctx->info_lo = htole32(
	    xhci_last_valid_dci(sdev->pipes, NULL) | XHCI_SCTX_SPEED(speed) |
	    XHCI_SCTX_ROUTE(route)
	);
	sdev->slot_ctx->info_hi = htole32(XHCI_SCTX_RHPORT(rhport));
	sdev->slot_ctx->tt = 0;
	sdev->slot_ctx->state = 0;

/* XXX */
#define UHUB_IS_MTT(dev) (dev->ddesc.bDeviceProtocol == UDPROTO_HSHUBMTT)
	/*
	 * If we are opening the interrupt pipe of a hub, update its
	 * context before putting it in the CONFIGURED state.
	 */
	if (pipe->device->hub != NULL) {
		int nports = pipe->device->hub->nports;

		sdev->slot_ctx->info_lo |= htole32(XHCI_SCTX_HUB(1));
		sdev->slot_ctx->info_hi |= htole32(XHCI_SCTX_NPORTS(nports));

		if (UHUB_IS_MTT(pipe->device))
			sdev->slot_ctx->info_lo |= htole32(XHCI_SCTX_MTT(1));

		sdev->slot_ctx->tt |= htole32(
		    XHCI_SCTX_TT_THINK_TIME(pipe->device->hub->ttthink)
		);
	}

	/*
	 * If this is a Low or Full Speed device below an external High
	 * Speed hub, it needs some TT love.
	 */
	if (speed < XHCI_SPEED_HIGH && pipe->device->myhsport != NULL) {
		struct usbd_device *hshub = pipe->device->myhsport->parent;
		uint8_t slot = ((struct xhci_pipe *)hshub->default_pipe)->slot;

		if (UHUB_IS_MTT(hshub))
			sdev->slot_ctx->info_lo |= htole32(XHCI_SCTX_MTT(1));

		sdev->slot_ctx->tt |= htole32(
		    XHCI_SCTX_TT_HUB_SID(slot) |
		    XHCI_SCTX_TT_PORT_NUM(pipe->device->myhsport->portno)
		);
	}
#undef UHUB_IS_MTT

	/* Unmask the slot context */
	sdev->input_ctx->add_flags |= htole32(XHCI_INCTX_MASK_DCI(0));

	bus_dmamap_sync(sdev->ictx_dma.tag, sdev->ictx_dma.map, 0,
	    sc->sc_pagesize, BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	return (0);
}

int
xhci_pipe_init(struct xhci_softc *sc, struct usbd_pipe *pipe)
{
	struct xhci_pipe *xp = (struct xhci_pipe *)pipe;
	struct xhci_soft_dev *sdev = &sc->sc_sdevs[xp->slot];
	int error;

#ifdef XHCI_DEBUG
	struct usbd_device *dev = pipe->device;
	printf("%s: pipe=%p addr=%d depth=%d port=%d speed=%d dev %d dci %u"
	    " (epAddr=0x%x)\n", __func__, pipe, dev->address, dev->depth,
	    dev->powersrc->portno, dev->speed, xp->slot, xp->dci,
	    pipe->endpoint->edesc->bEndpointAddress);
#endif

	if (xhci_ring_alloc(sc, &xp->ring, XHCI_MAX_XFER, XHCI_XFER_RING_ALIGN))
		return (ENOMEM);

	xp->free_trbs = xp->ring.ntrb;
	xp->halted = 0;

	sdev->pipes[xp->dci - 1] = xp;

	error = xhci_context_setup(sc, pipe);
	if (error)
		return (error);

	if (xp->dci == 1) {
		/*
		 * If we are opening the default pipe, the Slot should
		 * be in the ENABLED state.  Issue an "Address Device"
		 * with BSR=1 to put the device in the DEFAULT state.
		 * We cannot jump directly to the ADDRESSED state with
		 * BSR=0 because some Low/Full speed devices won't accept
		 * a SET_ADDRESS command before we've read their device
		 * descriptor.
		 */
		error = xhci_cmd_set_address(sc, xp->slot,
		    sdev->ictx_dma.paddr, XHCI_TRB_BSR);
	} else {
		error = xhci_cmd_configure_ep(sc, xp->slot,
		    sdev->ictx_dma.paddr);
	}

	if (error) {
		xhci_ring_free(sc, &xp->ring);
		return (EIO);
	}

	return (0);
}

void
xhci_pipe_close(struct usbd_pipe *pipe)
{
	struct xhci_softc *sc = (struct xhci_softc *)pipe->device->bus;
	struct xhci_pipe *xp = (struct xhci_pipe *)pipe;
	struct xhci_soft_dev *sdev = &sc->sc_sdevs[xp->slot];

	/* Root Hub */
	if (pipe->device->depth == 0)
		return;

	/* Mask the endpoint */
	sdev->input_ctx->drop_flags = htole32(XHCI_INCTX_MASK_DCI(xp->dci));
	sdev->input_ctx->add_flags = 0;

	/* Update last valid Endpoint Context */
	sdev->slot_ctx->info_lo &= htole32(~XHCI_SCTX_DCI(31));
	sdev->slot_ctx->info_lo |= htole32(xhci_last_valid_dci(sdev->pipes, xp));

	/* Clear the Endpoint Context */
	memset(sdev->ep_ctx[xp->dci - 1], 0, sizeof(struct xhci_epctx));

	bus_dmamap_sync(sdev->ictx_dma.tag, sdev->ictx_dma.map, 0,
	    sc->sc_pagesize, BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	if (xhci_cmd_configure_ep(sc, xp->slot, sdev->ictx_dma.paddr))
		DPRINTF(("%s: error clearing ep (%d)\n", DEVNAME(sc), xp->dci));

	xhci_ring_free(sc, &xp->ring);
	sdev->pipes[xp->dci - 1] = NULL;

	/*
	 * If we are closing the default pipe, the device is probably
	 * gone, so put its slot in the DISABLED state.
	 */
	if (xp->dci == 1) {
		xhci_cmd_slot_control(sc, &xp->slot, 0);
		xhci_softdev_free(sc, xp->slot);
	}
}

/*
 * Transition a device from DEFAULT to ADDRESSED Slot state, this hook
 * is needed for Low/Full speed devices.
 *
 * See section 4.5.3 of USB 3.1 Specification for more details.
 */
int
xhci_setaddr(struct usbd_device *dev, int addr)
{
	struct xhci_softc *sc = (struct xhci_softc *)dev->bus;
	struct xhci_pipe *xp = (struct xhci_pipe *)dev->default_pipe;
	struct xhci_soft_dev *sdev = &sc->sc_sdevs[xp->slot];
	int error;

	/* Root Hub */
	if (dev->depth == 0)
		return (0);

	KASSERT(xp->dci == 1);

	error = xhci_context_setup(sc, dev->default_pipe);
	if (error)
		return (error);

	error = xhci_cmd_set_address(sc, xp->slot, sdev->ictx_dma.paddr, 0);

#ifdef XHCI_DEBUG
	if (error == 0) {
		struct xhci_sctx *sctx;
		uint8_t addr;

		bus_dmamap_sync(sdev->octx_dma.tag, sdev->octx_dma.map, 0,
		    sc->sc_pagesize, BUS_DMASYNC_POSTREAD);

		/* Get output slot context. */
		sctx = (struct xhci_sctx *)sdev->octx_dma.vaddr;
		addr = XHCI_SCTX_DEV_ADDR(letoh32(sctx->state));
		error = (addr == 0);

		printf("%s: dev %d addr %d\n", DEVNAME(sc), xp->slot, addr);
	}
#endif

	return (error);
}

struct usbd_xfer *
xhci_allocx(struct usbd_bus *bus)
{
	return (pool_get(xhcixfer, PR_NOWAIT | PR_ZERO));
}

void
xhci_freex(struct usbd_bus *bus, struct usbd_xfer *xfer)
{
	pool_put(xhcixfer, xfer);
}

int
xhci_scratchpad_alloc(struct xhci_softc *sc, int npage)
{
	uint64_t *pte;
	int error, i;

	/* Allocate the required entry for the table. */
	error = usbd_dma_contig_alloc(&sc->sc_bus, &sc->sc_spad.table_dma,
	    (void **)&pte, npage * sizeof(uint64_t), XHCI_SPAD_TABLE_ALIGN,
	    sc->sc_pagesize);
	if (error)
		return (ENOMEM);

	/* Allocate pages. XXX does not need to be contiguous. */
	error = usbd_dma_contig_alloc(&sc->sc_bus, &sc->sc_spad.pages_dma,
	    NULL, npage * sc->sc_pagesize, sc->sc_pagesize, 0);
	if (error) {
		usbd_dma_contig_free(&sc->sc_bus, &sc->sc_spad.table_dma);
		return (ENOMEM);
	}

	for (i = 0; i < npage; i++) {
		pte[i] = htole64(
		    sc->sc_spad.pages_dma.paddr + (i * sc->sc_pagesize)
		);
	}

	bus_dmamap_sync(sc->sc_spad.table_dma.tag, sc->sc_spad.table_dma.map, 0,
	    npage * sizeof(uint64_t), BUS_DMASYNC_PREREAD |
	    BUS_DMASYNC_PREWRITE);

	/*  Entry 0 points to the table of scratchpad pointers. */
	sc->sc_dcbaa.segs[0] = htole64(sc->sc_spad.table_dma.paddr);
	bus_dmamap_sync(sc->sc_dcbaa.dma.tag, sc->sc_dcbaa.dma.map, 0,
	    sizeof(uint64_t), BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	sc->sc_spad.npage = npage;

	return (0);
}

void
xhci_scratchpad_free(struct xhci_softc *sc)
{
	sc->sc_dcbaa.segs[0] = 0;
	bus_dmamap_sync(sc->sc_dcbaa.dma.tag, sc->sc_dcbaa.dma.map, 0,
	    sizeof(uint64_t), BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	usbd_dma_contig_free(&sc->sc_bus, &sc->sc_spad.pages_dma);
	usbd_dma_contig_free(&sc->sc_bus, &sc->sc_spad.table_dma);
}

int
xhci_ring_alloc(struct xhci_softc *sc, struct xhci_ring *ring, size_t ntrb,
    size_t alignment)
{
	size_t size;
	int error;

	size = ntrb * sizeof(struct xhci_trb);

	error = usbd_dma_contig_alloc(&sc->sc_bus, &ring->dma,
	    (void **)&ring->trbs, size, alignment, XHCI_RING_BOUNDARY);
	if (error)
		return (error);

	ring->ntrb = ntrb;

	xhci_ring_reset(sc, ring);

	return (0);
}

void
xhci_ring_free(struct xhci_softc *sc, struct xhci_ring *ring)
{
	usbd_dma_contig_free(&sc->sc_bus, &ring->dma);
}

void
xhci_ring_reset(struct xhci_softc *sc, struct xhci_ring *ring)
{
	size_t size;

	size = ring->ntrb * sizeof(struct xhci_trb);

	memset(ring->trbs, 0, size);

	ring->index = 0;
	ring->toggle = XHCI_TRB_CYCLE;

	/*
	 * Since all our rings use only one segment, at least for
	 * the moment, link their tail to their head.
	 */
	if (ring != &sc->sc_evt_ring) {
		struct xhci_trb *trb = &ring->trbs[ring->ntrb - 1];

		trb->trb_paddr = htole64(ring->dma.paddr);
		trb->trb_flags = htole32(XHCI_TRB_TYPE_LINK | XHCI_TRB_LINKSEG |
		    XHCI_TRB_CYCLE);
		bus_dmamap_sync(ring->dma.tag, ring->dma.map, 0, size,
		    BUS_DMASYNC_PREWRITE);
	} else
		bus_dmamap_sync(ring->dma.tag, ring->dma.map, 0, size,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
}

struct xhci_trb*
xhci_ring_consume(struct xhci_softc *sc, struct xhci_ring *ring)
{
	struct xhci_trb *trb = &ring->trbs[ring->index];

	KASSERT(ring->index < ring->ntrb);

	bus_dmamap_sync(ring->dma.tag, ring->dma.map, TRBOFF(ring, trb),
	    sizeof(struct xhci_trb), BUS_DMASYNC_POSTREAD);

	/* Make sure this TRB can be consumed. */
	if (ring->toggle != (letoh32(trb->trb_flags) & XHCI_TRB_CYCLE))
		return (NULL);

	ring->index++;

	if (ring->index == ring->ntrb) {
		ring->index = 0;
		ring->toggle ^= 1;
	}

	return (trb);
}

struct xhci_trb*
xhci_ring_produce(struct xhci_softc *sc, struct xhci_ring *ring)
{
	struct xhci_trb *lnk, *trb;

	KASSERT(ring->index < ring->ntrb);

	/* Setup the link TRB after the previous TRB is done. */
	if (ring->index == 0) {
		lnk = &ring->trbs[ring->ntrb - 1];
		trb = &ring->trbs[ring->ntrb - 2];

		bus_dmamap_sync(ring->dma.tag, ring->dma.map, TRBOFF(ring, lnk),
		    sizeof(struct xhci_trb), BUS_DMASYNC_POSTREAD |
		    BUS_DMASYNC_POSTWRITE);

		lnk->trb_flags &= htole32(~XHCI_TRB_CHAIN);
		if (letoh32(trb->trb_flags) & XHCI_TRB_CHAIN)
			lnk->trb_flags |= htole32(XHCI_TRB_CHAIN);

		bus_dmamap_sync(ring->dma.tag, ring->dma.map, TRBOFF(ring, lnk),
		    sizeof(struct xhci_trb), BUS_DMASYNC_PREWRITE);

		lnk->trb_flags ^= htole32(XHCI_TRB_CYCLE);

		bus_dmamap_sync(ring->dma.tag, ring->dma.map, TRBOFF(ring, lnk),
		    sizeof(struct xhci_trb), BUS_DMASYNC_PREWRITE);
	}

	trb = &ring->trbs[ring->index++];
	bus_dmamap_sync(ring->dma.tag, ring->dma.map, TRBOFF(ring, trb),
	    sizeof(struct xhci_trb), BUS_DMASYNC_POSTREAD |
	    BUS_DMASYNC_POSTWRITE);

	/* Toggle cycle state of the link TRB and skip it. */
	if (ring->index == (ring->ntrb - 1)) {
		ring->index = 0;
		ring->toggle ^= 1;
	}

	return (trb);
}

struct xhci_trb *
xhci_xfer_get_trb(struct xhci_softc *sc, struct usbd_xfer *xfer,
    uint8_t *togglep, int last)
{
	struct xhci_pipe *xp = (struct xhci_pipe *)xfer->pipe;
	struct xhci_xfer *xx = (struct xhci_xfer *)xfer;

	KASSERT(xp->free_trbs >= 1);
	xp->free_trbs--;
	*togglep = xp->ring.toggle;

	switch (last) {
	case -1:	/* This will be a zero-length TD. */
		xp->pending_xfers[xp->ring.index] = NULL;
		xx->zerotd += 1;
		break;
	case 0:		/* This will be in a chain. */
		xp->pending_xfers[xp->ring.index] = xfer;
		xx->index = -2;
		xx->ntrb += 1;
		break;
	case 1:		/* This will terminate a chain. */
		xp->pending_xfers[xp->ring.index] = xfer;
		xx->index = xp->ring.index;
		xx->ntrb += 1;
		break;
	}

	xp->trb_processed[xp->ring.index] = TRB_PROCESSED_NO;

	return (xhci_ring_produce(sc, &xp->ring));
}

int
xhci_command_submit(struct xhci_softc *sc, struct xhci_trb *trb0, int timeout)
{
	struct xhci_trb *trb;
	int s, error = 0;

	KASSERT(timeout == 0 || sc->sc_cmd_trb == NULL);

	trb0->trb_flags |= htole32(sc->sc_cmd_ring.toggle);

	trb = xhci_ring_produce(sc, &sc->sc_cmd_ring);
	if (trb == NULL)
		return (EAGAIN);
	trb->trb_paddr = trb0->trb_paddr;
	trb->trb_status = trb0->trb_status;
	bus_dmamap_sync(sc->sc_cmd_ring.dma.tag, sc->sc_cmd_ring.dma.map,
	    TRBOFF(&sc->sc_cmd_ring, trb), sizeof(struct xhci_trb),
	    BUS_DMASYNC_PREWRITE);

	trb->trb_flags = trb0->trb_flags;
	bus_dmamap_sync(sc->sc_cmd_ring.dma.tag, sc->sc_cmd_ring.dma.map,
	    TRBOFF(&sc->sc_cmd_ring, trb), sizeof(struct xhci_trb),
	    BUS_DMASYNC_PREWRITE);

	if (timeout == 0) {
		XDWRITE4(sc, XHCI_DOORBELL(0), 0);
		return (0);
	}

	rw_assert_wrlock(&sc->sc_cmd_lock);

	s = splusb();
	sc->sc_cmd_trb = trb;
	XDWRITE4(sc, XHCI_DOORBELL(0), 0);
	error = tsleep_nsec(&sc->sc_cmd_trb, PZERO, "xhcicmd", timeout);
	if (error) {
#ifdef XHCI_DEBUG
		printf("%s: tsleep() = %d\n", __func__, error);
		printf("cmd = %d ", XHCI_TRB_TYPE(letoh32(trb->trb_flags)));
		xhci_dump_trb(trb);
#endif
		KASSERT(sc->sc_cmd_trb == trb || sc->sc_cmd_trb == NULL);
		/*
		 * Just because the timeout expired this does not mean that the
		 * TRB isn't active anymore! We could get an interrupt from
		 * this TRB later on and then wonder what to do with it.
		 * We'd rather abort it.
		 */
		xhci_command_abort(sc);
		sc->sc_cmd_trb = NULL;
		splx(s);
		return (error);
	}
	splx(s);

	memcpy(trb0, &sc->sc_result_trb, sizeof(struct xhci_trb));

	if (XHCI_TRB_GET_CODE(letoh32(trb0->trb_status)) == XHCI_CODE_SUCCESS)
		return (0);

#ifdef XHCI_DEBUG
	printf("%s: event error code=%d, result=%d  \n", DEVNAME(sc),
	    XHCI_TRB_GET_CODE(letoh32(trb0->trb_status)),
	    XHCI_TRB_TYPE(letoh32(trb0->trb_flags)));
	xhci_dump_trb(trb0);
#endif
	return (EIO);
}

int
xhci_command_abort(struct xhci_softc *sc)
{
	uint32_t reg;
	int i;

	reg = XOREAD4(sc, XHCI_CRCR_LO);
	if ((reg & XHCI_CRCR_LO_CRR) == 0)
		return (0);

	XOWRITE4(sc, XHCI_CRCR_LO, reg | XHCI_CRCR_LO_CA);
	XOWRITE4(sc, XHCI_CRCR_HI, 0);

	for (i = 0; i < 2500; i++) {
		DELAY(100);
		reg = XOREAD4(sc, XHCI_CRCR_LO) & XHCI_CRCR_LO_CRR;
		if (!reg)
			break;
	}

	if (reg) {
		printf("%s: command ring abort timeout\n", DEVNAME(sc));
		return (1);
	}

	return (0);
}

int
xhci_cmd_configure_ep(struct xhci_softc *sc, uint8_t slot, uint64_t addr)
{
	struct xhci_trb trb;
	int error;

	DPRINTF(("%s: %s dev %u\n", DEVNAME(sc), __func__, slot));

	trb.trb_paddr = htole64(addr);
	trb.trb_status = 0;
	trb.trb_flags = htole32(
	    XHCI_TRB_SET_SLOT(slot) | XHCI_CMD_CONFIG_EP
	);

	rw_enter_write(&sc->sc_cmd_lock);
	error = xhci_command_submit(sc, &trb, XHCI_CMD_TIMEOUT);
	rw_exit_write(&sc->sc_cmd_lock);
	return (error);
}

int
xhci_cmd_stop_ep(struct xhci_softc *sc, uint8_t slot, uint8_t dci)
{
	struct xhci_trb trb;
	int error;

	DPRINTF(("%s: %s dev %u dci %u\n", DEVNAME(sc), __func__, slot, dci));

	trb.trb_paddr = 0;
	trb.trb_status = 0;
	trb.trb_flags = htole32(
	    XHCI_TRB_SET_SLOT(slot) | XHCI_TRB_SET_EP(dci) | XHCI_CMD_STOP_EP
	);

	rw_enter_write(&sc->sc_cmd_lock);
	error = xhci_command_submit(sc, &trb, XHCI_CMD_TIMEOUT);
	rw_exit_write(&sc->sc_cmd_lock);
	return (error);
}

void
xhci_cmd_reset_ep_async(struct xhci_softc *sc, uint8_t slot, uint8_t dci)
{
	struct xhci_trb trb;

	DPRINTF(("%s: %s dev %u dci %u\n", DEVNAME(sc), __func__, slot, dci));

	trb.trb_paddr = 0;
	trb.trb_status = 0;
	trb.trb_flags = htole32(
	    XHCI_TRB_SET_SLOT(slot) | XHCI_TRB_SET_EP(dci) | XHCI_CMD_RESET_EP
	);

	xhci_command_submit(sc, &trb, 0);
}

void
xhci_cmd_set_tr_deq_async(struct xhci_softc *sc, uint8_t slot, uint8_t dci,
   uint64_t addr)
{
	struct xhci_trb trb;

	DPRINTF(("%s: %s dev %u dci %u\n", DEVNAME(sc), __func__, slot, dci));

	trb.trb_paddr = htole64(addr);
	trb.trb_status = 0;
	trb.trb_flags = htole32(
	    XHCI_TRB_SET_SLOT(slot) | XHCI_TRB_SET_EP(dci) | XHCI_CMD_SET_TR_DEQ
	);

	xhci_command_submit(sc, &trb, 0);
}

int
xhci_cmd_slot_control(struct xhci_softc *sc, uint8_t *slotp, int enable)
{
	struct xhci_trb trb;
	int error;

	DPRINTF(("%s: %s\n", DEVNAME(sc), __func__));

	trb.trb_paddr = 0;
	trb.trb_status = 0;
	if (enable)
		trb.trb_flags = htole32(XHCI_CMD_ENABLE_SLOT);
	else
		trb.trb_flags = htole32(
			XHCI_TRB_SET_SLOT(*slotp) | XHCI_CMD_DISABLE_SLOT
		);

	rw_enter_write(&sc->sc_cmd_lock);
	error = xhci_command_submit(sc, &trb, XHCI_CMD_TIMEOUT);
	rw_exit_write(&sc->sc_cmd_lock);
	if (error != 0)
		return (EIO);

	if (enable)
		*slotp = XHCI_TRB_GET_SLOT(letoh32(trb.trb_flags));

	return (0);
}

int
xhci_cmd_set_address(struct xhci_softc *sc, uint8_t slot, uint64_t addr,
    uint32_t bsr)
{
	struct xhci_trb trb;
	int error;

	DPRINTF(("%s: %s BSR=%u\n", DEVNAME(sc), __func__, bsr ? 1 : 0));

	trb.trb_paddr = htole64(addr);
	trb.trb_status = 0;
	trb.trb_flags = htole32(
	    XHCI_TRB_SET_SLOT(slot) | XHCI_CMD_ADDRESS_DEVICE | bsr
	);

	rw_enter_write(&sc->sc_cmd_lock);
	error = xhci_command_submit(sc, &trb, XHCI_CMD_TIMEOUT);
	rw_exit_write(&sc->sc_cmd_lock);
	return (error);
}

#ifdef XHCI_DEBUG
int
xhci_cmd_noop(struct xhci_softc *sc)
{
	struct xhci_trb trb;
	int error;

	DPRINTF(("%s: %s\n", DEVNAME(sc), __func__));

	trb.trb_paddr = 0;
	trb.trb_status = 0;
	trb.trb_flags = htole32(XHCI_CMD_NOOP);

	rw_enter_write(&sc->sc_cmd_lock);
	error = xhci_command_submit(sc, &trb, XHCI_CMD_TIMEOUT);
	rw_exit_write(&sc->sc_cmd_lock);
	return (error);
}
#endif

int
xhci_softdev_alloc(struct xhci_softc *sc, uint8_t slot)
{
	struct xhci_soft_dev *sdev = &sc->sc_sdevs[slot];
	int i, error;
	uint8_t *kva;

	/*
	 * Setup input context.  Even with 64 byte context size, it
	 * fits into the smallest supported page size, so use that.
	 */
	error = usbd_dma_contig_alloc(&sc->sc_bus, &sdev->ictx_dma,
	    (void **)&kva, sc->sc_pagesize, XHCI_ICTX_ALIGN, sc->sc_pagesize);
	if (error)
		return (ENOMEM);

	sdev->input_ctx = (struct xhci_inctx *)kva;
	sdev->slot_ctx = (struct xhci_sctx *)(kva + sc->sc_ctxsize);
	for (i = 0; i < 31; i++)
		sdev->ep_ctx[i] =
		    (struct xhci_epctx *)(kva + (i + 2) * sc->sc_ctxsize);

	DPRINTF(("%s: dev %d, input=%p slot=%p ep0=%p\n", DEVNAME(sc),
	 slot, sdev->input_ctx, sdev->slot_ctx, sdev->ep_ctx[0]));

	/* Setup output context */
	error = usbd_dma_contig_alloc(&sc->sc_bus, &sdev->octx_dma, NULL,
	    sc->sc_pagesize, XHCI_OCTX_ALIGN, sc->sc_pagesize);
	if (error) {
		usbd_dma_contig_free(&sc->sc_bus, &sdev->ictx_dma);
		return (ENOMEM);
	}

	memset(&sdev->pipes, 0, sizeof(sdev->pipes));

	DPRINTF(("%s: dev %d, setting DCBAA to 0x%016llx\n", DEVNAME(sc),
	    slot, (long long)sdev->octx_dma.paddr));

	sc->sc_dcbaa.segs[slot] = htole64(sdev->octx_dma.paddr);
	bus_dmamap_sync(sc->sc_dcbaa.dma.tag, sc->sc_dcbaa.dma.map,
	    slot * sizeof(uint64_t), sizeof(uint64_t), BUS_DMASYNC_PREREAD |
	    BUS_DMASYNC_PREWRITE);

	return (0);
}

void
xhci_softdev_free(struct xhci_softc *sc, uint8_t slot)
{
	struct xhci_soft_dev *sdev = &sc->sc_sdevs[slot];

	sc->sc_dcbaa.segs[slot] = 0;
	bus_dmamap_sync(sc->sc_dcbaa.dma.tag, sc->sc_dcbaa.dma.map,
	    slot * sizeof(uint64_t), sizeof(uint64_t), BUS_DMASYNC_PREREAD |
	    BUS_DMASYNC_PREWRITE);

	usbd_dma_contig_free(&sc->sc_bus, &sdev->octx_dma);
	usbd_dma_contig_free(&sc->sc_bus, &sdev->ictx_dma);

	memset(sdev, 0, sizeof(struct xhci_soft_dev));
}

/* Root hub descriptors. */
const usb_device_descriptor_t xhci_devd = {
	USB_DEVICE_DESCRIPTOR_SIZE,
	UDESC_DEVICE,		/* type */
	{0x00, 0x03},		/* USB version */
	UDCLASS_HUB,		/* class */
	UDSUBCLASS_HUB,		/* subclass */
	UDPROTO_HSHUBSTT,	/* protocol */
	9,			/* max packet */
	{0},{0},{0x00,0x01},	/* device id */
	1,2,0,			/* string indexes */
	1			/* # of configurations */
};

const usb_config_descriptor_t xhci_confd = {
	USB_CONFIG_DESCRIPTOR_SIZE,
	UDESC_CONFIG,
	{USB_CONFIG_DESCRIPTOR_SIZE +
	 USB_INTERFACE_DESCRIPTOR_SIZE +
	 USB_ENDPOINT_DESCRIPTOR_SIZE},
	1,
	1,
	0,
	UC_BUS_POWERED | UC_SELF_POWERED,
	0                      /* max power */
};

const usb_interface_descriptor_t xhci_ifcd = {
	USB_INTERFACE_DESCRIPTOR_SIZE,
	UDESC_INTERFACE,
	0,
	0,
	1,
	UICLASS_HUB,
	UISUBCLASS_HUB,
	UIPROTO_HSHUBSTT,
	0
};

const usb_endpoint_descriptor_t xhci_endpd = {
	USB_ENDPOINT_DESCRIPTOR_SIZE,
	UDESC_ENDPOINT,
	UE_DIR_IN | XHCI_INTR_ENDPT,
	UE_INTERRUPT,
	{2, 0},                 /* max 15 ports */
	255
};

const usb_endpoint_ss_comp_descriptor_t xhci_endpcd = {
	USB_ENDPOINT_SS_COMP_DESCRIPTOR_SIZE,
	UDESC_ENDPOINT_SS_COMP,
	0,
	0,
	{0, 0}
};

const usb_hub_descriptor_t xhci_hubd = {
	USB_HUB_DESCRIPTOR_SIZE,
	UDESC_SS_HUB,
	0,
	{0,0},
	0,
	0,
	{0},
};

void
xhci_abort_xfer(struct usbd_xfer *xfer, usbd_status status)
{
	struct xhci_softc *sc = (struct xhci_softc *)xfer->device->bus;
	struct xhci_pipe *xp = (struct xhci_pipe *)xfer->pipe;
	int error;

	splsoftassert(IPL_SOFTUSB);

	DPRINTF(("%s: xfer=%p status=%s err=%s actlen=%d len=%d idx=%d\n",
	    __func__, xfer, usbd_errstr(xfer->status), usbd_errstr(status),
	    xfer->actlen, xfer->length, ((struct xhci_xfer *)xfer)->index));

	/* XXX The stack should not call abort() in this case. */
	if (sc->sc_bus.dying || xfer->status == USBD_NOT_STARTED) {
		xfer->status = status;
		timeout_del(&xfer->timeout_handle);
		usb_rem_task(xfer->device, &xfer->abort_task);
		usb_transfer_complete(xfer);
		return;
	}

	/* Transfer is already done. */
	if (xfer->status != USBD_IN_PROGRESS) {
		DPRINTF(("%s: already done \n", __func__));
		return;
	}

	/* Prevent any timeout to kick in. */
	timeout_del(&xfer->timeout_handle);
	usb_rem_task(xfer->device, &xfer->abort_task);

	/* Indicate that we are aborting this transfer. */
	xp->halted = status;
	xp->aborted_xfer = xfer;

	/* Stop the endpoint and wait until the hardware says so. */
	if (xhci_cmd_stop_ep(sc, xp->slot, xp->dci)) {
		DPRINTF(("%s: error stopping endpoint\n", DEVNAME(sc)));
		/* Assume the device is gone. */
		xp->halted = 0;
		xp->aborted_xfer = NULL;
		xfer->status = status;
		usb_transfer_complete(xfer);
		return;
	}

	/*
	 * The transfer was already completed when we stopped the
	 * endpoint, no need to move the dequeue pointer past its
	 * TRBs.
	 */
	if (xp->aborted_xfer == NULL) {
		DPRINTF(("%s: done before stopping the endpoint\n", __func__));
		xp->halted = 0;
		return;
	}

	/*
	 * At this stage the endpoint has been stopped, so update its
	 * dequeue pointer past the last TRB of the transfer.
	 *
	 * Note: This assumes that only one transfer per endpoint has
	 *	 pending TRBs on the ring.
	 */
	xhci_cmd_set_tr_deq_async(sc, xp->slot, xp->dci,
	    DEQPTR(xp->ring) | xp->ring.toggle);
	error = tsleep_nsec(xp, PZERO, "xhciab", XHCI_CMD_TIMEOUT);
	if (error)
		printf("%s: timeout aborting transfer\n", DEVNAME(sc));
}

void
xhci_timeout(void *addr)
{
	struct usbd_xfer *xfer = addr;
	struct xhci_softc *sc = (struct xhci_softc *)xfer->device->bus;

	if (sc->sc_bus.dying) {
		xhci_timeout_task(addr);
		return;
	}

	usb_init_task(&xfer->abort_task, xhci_timeout_task, addr,
	    USB_TASK_TYPE_ABORT);
	usb_add_task(xfer->device, &xfer->abort_task);
}

void
xhci_timeout_task(void *addr)
{
	struct usbd_xfer *xfer = addr;
	int s;

	s = splusb();
	xhci_abort_xfer(xfer, USBD_TIMEOUT);
	splx(s);
}

usbd_status
xhci_root_ctrl_transfer(struct usbd_xfer *xfer)
{
	usbd_status err;

	err = usb_insert_transfer(xfer);
	if (err)
		return (err);

	return (xhci_root_ctrl_start(SIMPLEQ_FIRST(&xfer->pipe->queue)));
}

usbd_status
xhci_root_ctrl_start(struct usbd_xfer *xfer)
{
	struct xhci_softc *sc = (struct xhci_softc *)xfer->device->bus;
	usb_port_status_t ps;
	usb_device_request_t *req;
	void *buf = NULL;
	usb_device_descriptor_t devd;
	usb_hub_descriptor_t hubd;
	usbd_status err;
	int s, len, value, index;
	int l, totlen = 0;
	int port, i;
	uint32_t v;

	KASSERT(xfer->rqflags & URQ_REQUEST);

	if (sc->sc_bus.dying)
		return (USBD_IOERROR);

	req = &xfer->request;

	DPRINTFN(4,("%s: type=0x%02x request=%02x\n", __func__,
	    req->bmRequestType, req->bRequest));

	len = UGETW(req->wLength);
	value = UGETW(req->wValue);
	index = UGETW(req->wIndex);

	if (len != 0)
		buf = KERNADDR(&xfer->dmabuf, 0);

#define C(x,y) ((x) | ((y) << 8))
	switch(C(req->bRequest, req->bmRequestType)) {
	case C(UR_CLEAR_FEATURE, UT_WRITE_DEVICE):
	case C(UR_CLEAR_FEATURE, UT_WRITE_INTERFACE):
	case C(UR_CLEAR_FEATURE, UT_WRITE_ENDPOINT):
		/*
		 * DEVICE_REMOTE_WAKEUP and ENDPOINT_HALT are no-ops
		 * for the integrated root hub.
		 */
		break;
	case C(UR_GET_CONFIG, UT_READ_DEVICE):
		if (len > 0) {
			*(uint8_t *)buf = sc->sc_conf;
			totlen = 1;
		}
		break;
	case C(UR_GET_DESCRIPTOR, UT_READ_DEVICE):
		DPRINTFN(8,("xhci_root_ctrl_start: wValue=0x%04x\n", value));
		switch(value >> 8) {
		case UDESC_DEVICE:
			if ((value & 0xff) != 0) {
				err = USBD_IOERROR;
				goto ret;
			}
			devd = xhci_devd;
			USETW(devd.idVendor, sc->sc_id_vendor);
			totlen = l = min(len, USB_DEVICE_DESCRIPTOR_SIZE);
			memcpy(buf, &devd, l);
			break;
		/*
		 * We can't really operate at another speed, but the spec says
		 * we need this descriptor.
		 */
		case UDESC_OTHER_SPEED_CONFIGURATION:
		case UDESC_CONFIG:
			if ((value & 0xff) != 0) {
				err = USBD_IOERROR;
				goto ret;
			}
			totlen = l = min(len, USB_CONFIG_DESCRIPTOR_SIZE);
			memcpy(buf, &xhci_confd, l);
			((usb_config_descriptor_t *)buf)->bDescriptorType =
			    value >> 8;
			buf = (char *)buf + l;
			len -= l;
			l = min(len, USB_INTERFACE_DESCRIPTOR_SIZE);
			totlen += l;
			memcpy(buf, &xhci_ifcd, l);
			buf = (char *)buf + l;
			len -= l;
			l = min(len, USB_ENDPOINT_DESCRIPTOR_SIZE);
			totlen += l;
			memcpy(buf, &xhci_endpd, l);
			break;
		case UDESC_STRING:
			if (len == 0)
				break;
			*(u_int8_t *)buf = 0;
			totlen = 1;
			switch (value & 0xff) {
			case 0: /* Language table */
				totlen = usbd_str(buf, len, "\001");
				break;
			case 1: /* Vendor */
				totlen = usbd_str(buf, len, sc->sc_vendor);
				break;
			case 2: /* Product */
				totlen = usbd_str(buf, len, "xHCI root hub");
				break;
			}
			break;
		default:
			err = USBD_IOERROR;
			goto ret;
		}
		break;
	case C(UR_GET_INTERFACE, UT_READ_INTERFACE):
		if (len > 0) {
			*(uint8_t *)buf = 0;
			totlen = 1;
		}
		break;
	case C(UR_GET_STATUS, UT_READ_DEVICE):
		if (len > 1) {
			USETW(((usb_status_t *)buf)->wStatus,UDS_SELF_POWERED);
			totlen = 2;
		}
		break;
	case C(UR_GET_STATUS, UT_READ_INTERFACE):
	case C(UR_GET_STATUS, UT_READ_ENDPOINT):
		if (len > 1) {
			USETW(((usb_status_t *)buf)->wStatus, 0);
			totlen = 2;
		}
		break;
	case C(UR_SET_ADDRESS, UT_WRITE_DEVICE):
		if (value >= USB_MAX_DEVICES) {
			err = USBD_IOERROR;
			goto ret;
		}
		break;
	case C(UR_SET_CONFIG, UT_WRITE_DEVICE):
		if (value != 0 && value != 1) {
			err = USBD_IOERROR;
			goto ret;
		}
		sc->sc_conf = value;
		break;
	case C(UR_SET_DESCRIPTOR, UT_WRITE_DEVICE):
		break;
	case C(UR_SET_FEATURE, UT_WRITE_DEVICE):
	case C(UR_SET_FEATURE, UT_WRITE_INTERFACE):
	case C(UR_SET_FEATURE, UT_WRITE_ENDPOINT):
		err = USBD_IOERROR;
		goto ret;
	case C(UR_SET_INTERFACE, UT_WRITE_INTERFACE):
		break;
	case C(UR_SYNCH_FRAME, UT_WRITE_ENDPOINT):
		break;
	/* Hub requests */
	case C(UR_CLEAR_FEATURE, UT_WRITE_CLASS_DEVICE):
		break;
	case C(UR_CLEAR_FEATURE, UT_WRITE_CLASS_OTHER):
		DPRINTFN(8, ("xhci_root_ctrl_start: UR_CLEAR_PORT_FEATURE "
		    "port=%d feature=%d\n", index, value));
		if (index < 1 || index > sc->sc_noport) {
			err = USBD_IOERROR;
			goto ret;
		}
		port = XHCI_PORTSC(index);
		v = XOREAD4(sc, port) & ~XHCI_PS_CLEAR;
		switch (value) {
		case UHF_PORT_ENABLE:
			XOWRITE4(sc, port, v | XHCI_PS_PED);
			break;
		case UHF_PORT_SUSPEND:
			/* TODO */
			break;
		case UHF_PORT_POWER:
			XOWRITE4(sc, port, v & ~XHCI_PS_PP);
			break;
		case UHF_PORT_INDICATOR:
			XOWRITE4(sc, port, v & ~XHCI_PS_SET_PIC(3));
			break;
		case UHF_C_PORT_CONNECTION:
			XOWRITE4(sc, port, v | XHCI_PS_CSC);
			break;
		case UHF_C_PORT_ENABLE:
			XOWRITE4(sc, port, v | XHCI_PS_PEC);
			break;
		case UHF_C_PORT_SUSPEND:
		case UHF_C_PORT_LINK_STATE:
			XOWRITE4(sc, port, v | XHCI_PS_PLC);
			break;
		case UHF_C_PORT_OVER_CURRENT:
			XOWRITE4(sc, port, v | XHCI_PS_OCC);
			break;
		case UHF_C_PORT_RESET:
			XOWRITE4(sc, port, v | XHCI_PS_PRC);
			break;
		case UHF_C_BH_PORT_RESET:
			XOWRITE4(sc, port, v | XHCI_PS_WRC);
			break;
		default:
			err = USBD_IOERROR;
			goto ret;
		}
		break;

	case C(UR_GET_DESCRIPTOR, UT_READ_CLASS_DEVICE):
		if (len == 0)
			break;
		if ((value & 0xff) != 0) {
			err = USBD_IOERROR;
			goto ret;
		}
		v = XREAD4(sc, XHCI_HCCPARAMS);
		hubd = xhci_hubd;
		hubd.bNbrPorts = sc->sc_noport;
		USETW(hubd.wHubCharacteristics,
		    (XHCI_HCC_PPC(v) ? UHD_PWR_INDIVIDUAL : UHD_PWR_GANGED) |
		    (XHCI_HCC_PIND(v) ? UHD_PORT_IND : 0));
		hubd.bPwrOn2PwrGood = 10; /* xHCI section 5.4.9 */
		for (i = 1; i <= sc->sc_noport; i++) {
			v = XOREAD4(sc, XHCI_PORTSC(i));
			if (v & XHCI_PS_DR)
				hubd.DeviceRemovable[i / 8] |= 1U << (i % 8);
		}
		hubd.bDescLength = USB_HUB_DESCRIPTOR_SIZE + i;
		l = min(len, hubd.bDescLength);
		totlen = l;
		memcpy(buf, &hubd, l);
		break;
	case C(UR_GET_STATUS, UT_READ_CLASS_DEVICE):
		if (len != 16) {
			err = USBD_IOERROR;
			goto ret;
		}
		memset(buf, 0, len);
		totlen = len;
		break;
	case C(UR_GET_STATUS, UT_READ_CLASS_OTHER):
		DPRINTFN(8,("xhci_root_ctrl_start: get port status i=%d\n",
		    index));
		if (index < 1 || index > sc->sc_noport) {
			err = USBD_IOERROR;
			goto ret;
		}
		if (len != 4) {
			err = USBD_IOERROR;
			goto ret;
		}
		v = XOREAD4(sc, XHCI_PORTSC(index));
		DPRINTFN(8,("xhci_root_ctrl_start: port status=0x%04x\n", v));
		i = UPS_PORT_LS_SET(XHCI_PS_GET_PLS(v));
		switch (XHCI_PS_SPEED(v)) {
		case XHCI_SPEED_FULL:
			i |= UPS_FULL_SPEED;
			break;
		case XHCI_SPEED_LOW:
			i |= UPS_LOW_SPEED;
			break;
		case XHCI_SPEED_HIGH:
			i |= UPS_HIGH_SPEED;
			break;
		case XHCI_SPEED_SUPER:
		default:
			break;
		}
		if (v & XHCI_PS_CCS)	i |= UPS_CURRENT_CONNECT_STATUS;
		if (v & XHCI_PS_PED)	i |= UPS_PORT_ENABLED;
		if (v & XHCI_PS_OCA)	i |= UPS_OVERCURRENT_INDICATOR;
		if (v & XHCI_PS_PR)	i |= UPS_RESET;
		if (v & XHCI_PS_PP)	{
			if (XHCI_PS_SPEED(v) >= XHCI_SPEED_FULL &&
			    XHCI_PS_SPEED(v) <= XHCI_SPEED_HIGH)
				i |= UPS_PORT_POWER;
			else
				i |= UPS_PORT_POWER_SS;
		}
		USETW(ps.wPortStatus, i);
		i = 0;
		if (v & XHCI_PS_CSC)    i |= UPS_C_CONNECT_STATUS;
		if (v & XHCI_PS_PEC)    i |= UPS_C_PORT_ENABLED;
		if (v & XHCI_PS_OCC)    i |= UPS_C_OVERCURRENT_INDICATOR;
		if (v & XHCI_PS_PRC)	i |= UPS_C_PORT_RESET;
		if (v & XHCI_PS_WRC)	i |= UPS_C_BH_PORT_RESET;
		if (v & XHCI_PS_PLC)	i |= UPS_C_PORT_LINK_STATE;
		if (v & XHCI_PS_CEC)	i |= UPS_C_PORT_CONFIG_ERROR;
		USETW(ps.wPortChange, i);
		l = min(len, sizeof ps);
		memcpy(buf, &ps, l);
		totlen = l;
		break;
	case C(UR_SET_DESCRIPTOR, UT_WRITE_CLASS_DEVICE):
		err = USBD_IOERROR;
		goto ret;
	case C(UR_SET_FEATURE, UT_WRITE_CLASS_DEVICE):
		break;
	case C(UR_SET_FEATURE, UT_WRITE_CLASS_OTHER):

		i = index >> 8;
		index &= 0x00ff;

		if (index < 1 || index > sc->sc_noport) {
			err = USBD_IOERROR;
			goto ret;
		}
		port = XHCI_PORTSC(index);
		v = XOREAD4(sc, port) & ~XHCI_PS_CLEAR;

		switch (value) {
		case UHF_PORT_ENABLE:
			XOWRITE4(sc, port, v | XHCI_PS_PED);
			break;
		case UHF_PORT_SUSPEND:
			DPRINTFN(6, ("suspend port %u (LPM=%u)\n", index, i));
			if (XHCI_PS_SPEED(v) == XHCI_SPEED_SUPER) {
				err = USBD_IOERROR;
				goto ret;
			}
			XOWRITE4(sc, port, v |
			    XHCI_PS_SET_PLS(i ? 2 /* LPM */ : 3) | XHCI_PS_LWS);
			break;
		case UHF_PORT_RESET:
			DPRINTFN(6, ("reset port %d\n", index));
			XOWRITE4(sc, port, v | XHCI_PS_PR);
			break;
		case UHF_PORT_POWER:
			DPRINTFN(3, ("set port power %d\n", index));
			XOWRITE4(sc, port, v | XHCI_PS_PP);
			break;
		case UHF_PORT_INDICATOR:
			DPRINTFN(3, ("set port indicator %d\n", index));

			v &= ~XHCI_PS_SET_PIC(3);
			v |= XHCI_PS_SET_PIC(1);

			XOWRITE4(sc, port, v);
			break;
		case UHF_C_PORT_RESET:
			XOWRITE4(sc, port, v | XHCI_PS_PRC);
			break;
		case UHF_C_BH_PORT_RESET:
			XOWRITE4(sc, port, v | XHCI_PS_WRC);
			break;
		default:
			err = USBD_IOERROR;
			goto ret;
		}
		break;
	case C(UR_CLEAR_TT_BUFFER, UT_WRITE_CLASS_OTHER):
	case C(UR_RESET_TT, UT_WRITE_CLASS_OTHER):
	case C(UR_GET_TT_STATE, UT_READ_CLASS_OTHER):
	case C(UR_STOP_TT, UT_WRITE_CLASS_OTHER):
		break;
	default:
		err = USBD_IOERROR;
		goto ret;
	}
	xfer->actlen = totlen;
	err = USBD_NORMAL_COMPLETION;
ret:
	xfer->status = err;
	s = splusb();
	usb_transfer_complete(xfer);
	splx(s);
	return (err);
}


void
xhci_noop(struct usbd_xfer *xfer)
{
}


usbd_status
xhci_root_intr_transfer(struct usbd_xfer *xfer)
{
	usbd_status err;

	err = usb_insert_transfer(xfer);
	if (err)
		return (err);

	return (xhci_root_intr_start(SIMPLEQ_FIRST(&xfer->pipe->queue)));
}

usbd_status
xhci_root_intr_start(struct usbd_xfer *xfer)
{
	struct xhci_softc *sc = (struct xhci_softc *)xfer->device->bus;

	if (sc->sc_bus.dying)
		return (USBD_IOERROR);

	sc->sc_intrxfer = xfer;

	return (USBD_IN_PROGRESS);
}

void
xhci_root_intr_abort(struct usbd_xfer *xfer)
{
	struct xhci_softc *sc = (struct xhci_softc *)xfer->device->bus;
	int s;

	sc->sc_intrxfer = NULL;

	xfer->status = USBD_CANCELLED;
	s = splusb();
	usb_transfer_complete(xfer);
	splx(s);
}

void
xhci_root_intr_done(struct usbd_xfer *xfer)
{
}

/*
 * Number of packets remaining in the TD after the corresponding TRB.
 *
 * Section 4.11.2.4 of xHCI specification r1.1.
 */
static inline uint32_t
xhci_xfer_tdsize(struct usbd_xfer *xfer, uint32_t remain, uint32_t len)
{
	uint32_t npkt, mps = UGETW(xfer->pipe->endpoint->edesc->wMaxPacketSize);

	if (len == 0)
		return XHCI_TRB_TDREM(0);

	npkt = howmany(remain - len, UE_GET_SIZE(mps));
	if (npkt > 31)
		npkt = 31;

	return XHCI_TRB_TDREM(npkt);
}

/*
 * Transfer Burst Count (TBC) and Transfer Last Burst Packet Count (TLBPC).
 *
 * Section 4.11.2.3  of xHCI specification r1.1.
 */
static inline uint32_t
xhci_xfer_tbc(struct usbd_xfer *xfer, uint32_t len, uint32_t *tlbpc)
{
	uint32_t mps = UGETW(xfer->pipe->endpoint->edesc->wMaxPacketSize);
	uint32_t maxb, tdpc, residue, tbc;

	/* Transfer Descriptor Packet Count, section 4.14.1. */
	tdpc = howmany(len, UE_GET_SIZE(mps));
	if (tdpc == 0)
		tdpc = 1;

	/* Transfer Burst Count */
	maxb = xhci_pipe_maxburst(xfer->pipe);
	tbc = howmany(tdpc, maxb + 1) - 1;

	/* Transfer Last Burst Packet Count */
	if (xfer->device->speed == USB_SPEED_SUPER) {
		residue = tdpc % (maxb + 1);
		if (residue == 0)
			*tlbpc = maxb;
		else
			*tlbpc = residue - 1;
	} else {
		*tlbpc = tdpc - 1;
	}

	return (tbc);
}

usbd_status
xhci_device_ctrl_transfer(struct usbd_xfer *xfer)
{
	usbd_status err;

	err = usb_insert_transfer(xfer);
	if (err)
		return (err);

	return (xhci_device_ctrl_start(SIMPLEQ_FIRST(&xfer->pipe->queue)));
}

usbd_status
xhci_device_ctrl_start(struct usbd_xfer *xfer)
{
	struct xhci_softc *sc = (struct xhci_softc *)xfer->device->bus;
	struct xhci_pipe *xp = (struct xhci_pipe *)xfer->pipe;
	struct xhci_trb *trb0, *trb;
	uint32_t flags, len = UGETW(xfer->request.wLength);
	uint8_t toggle;
	int s;

	KASSERT(xfer->rqflags & URQ_REQUEST);

	if (sc->sc_bus.dying || xp->halted)
		return (USBD_IOERROR);

	if (xp->free_trbs < 3)
		return (USBD_NOMEM);

	if (len != 0)
		usb_syncmem(&xfer->dmabuf, 0, len,
		    usbd_xfer_isread(xfer) ?
		    BUS_DMASYNC_PREREAD : BUS_DMASYNC_PREWRITE);

	/* We'll toggle the setup TRB once we're finished with the stages. */
	trb0 = xhci_xfer_get_trb(sc, xfer, &toggle, 0);

	flags = XHCI_TRB_TYPE_SETUP | XHCI_TRB_IDT | (toggle ^ 1);
	if (len != 0) {
		if (usbd_xfer_isread(xfer))
			flags |= XHCI_TRB_TRT_IN;
		else
			flags |= XHCI_TRB_TRT_OUT;
	}

	memcpy(&trb0->trb_paddr, &xfer->request, sizeof(trb0->trb_paddr));
	trb0->trb_status = htole32(XHCI_TRB_INTR(0) | XHCI_TRB_LEN(8));
	trb0->trb_flags = htole32(flags);
	bus_dmamap_sync(xp->ring.dma.tag, xp->ring.dma.map,
	    TRBOFF(&xp->ring, trb0), sizeof(struct xhci_trb),
	    BUS_DMASYNC_PREWRITE);

	/* Data TRB */
	if (len != 0) {
		trb = xhci_xfer_get_trb(sc, xfer, &toggle, 0);

		flags = XHCI_TRB_TYPE_DATA | toggle;
		if (usbd_xfer_isread(xfer))
			flags |= XHCI_TRB_DIR_IN | XHCI_TRB_ISP;

		trb->trb_paddr = htole64(DMAADDR(&xfer->dmabuf, 0));
		trb->trb_status = htole32(
		    XHCI_TRB_INTR(0) | XHCI_TRB_LEN(len) |
		    xhci_xfer_tdsize(xfer, len, len)
		);
		trb->trb_flags = htole32(flags);

		bus_dmamap_sync(xp->ring.dma.tag, xp->ring.dma.map,
		    TRBOFF(&xp->ring, trb), sizeof(struct xhci_trb),
		    BUS_DMASYNC_PREWRITE);
	}

	/* Status TRB */
	trb = xhci_xfer_get_trb(sc, xfer, &toggle, 1);

	flags = XHCI_TRB_TYPE_STATUS | XHCI_TRB_IOC | toggle;
	if (len == 0 || !usbd_xfer_isread(xfer))
		flags |= XHCI_TRB_DIR_IN;

	trb->trb_paddr = 0;
	trb->trb_status = htole32(XHCI_TRB_INTR(0));
	trb->trb_flags = htole32(flags);

	bus_dmamap_sync(xp->ring.dma.tag, xp->ring.dma.map,
	    TRBOFF(&xp->ring, trb), sizeof(struct xhci_trb),
	    BUS_DMASYNC_PREWRITE);

	/* Setup TRB */
	trb0->trb_flags ^= htole32(XHCI_TRB_CYCLE);
	bus_dmamap_sync(xp->ring.dma.tag, xp->ring.dma.map,
	    TRBOFF(&xp->ring, trb0), sizeof(struct xhci_trb),
	    BUS_DMASYNC_PREWRITE);

	s = splusb();
	XDWRITE4(sc, XHCI_DOORBELL(xp->slot), xp->dci);

	xfer->status = USBD_IN_PROGRESS;
	if (xfer->timeout && !sc->sc_bus.use_polling) {
		timeout_del(&xfer->timeout_handle);
		timeout_set(&xfer->timeout_handle, xhci_timeout, xfer);
		timeout_add_msec(&xfer->timeout_handle, xfer->timeout);
	}
	splx(s);

	return (USBD_IN_PROGRESS);
}

void
xhci_device_ctrl_abort(struct usbd_xfer *xfer)
{
	xhci_abort_xfer(xfer, USBD_CANCELLED);
}

usbd_status
xhci_device_generic_transfer(struct usbd_xfer *xfer)
{
	usbd_status err;

	err = usb_insert_transfer(xfer);
	if (err)
		return (err);

	return (xhci_device_generic_start(SIMPLEQ_FIRST(&xfer->pipe->queue)));
}

usbd_status
xhci_device_generic_start(struct usbd_xfer *xfer)
{
	struct xhci_softc *sc = (struct xhci_softc *)xfer->device->bus;
	struct xhci_pipe *xp = (struct xhci_pipe *)xfer->pipe;
	struct xhci_trb *trb0, *trb;
	uint32_t len, remain, flags;
	uint32_t mps = UGETW(xfer->pipe->endpoint->edesc->wMaxPacketSize);
	uint64_t paddr = DMAADDR(&xfer->dmabuf, 0);
	uint8_t toggle;
	int s, i, ntrb, zerotd = 0;

	KASSERT(!(xfer->rqflags & URQ_REQUEST));

	if (sc->sc_bus.dying || xp->halted)
		return (USBD_IOERROR);

	/* How many TRBs do we need for this transfer? */
	ntrb = howmany(xfer->length, XHCI_TRB_MAXSIZE);

	/* If the buffer crosses a 64k boundary, we need one more. */
	len = XHCI_TRB_MAXSIZE - (paddr & (XHCI_TRB_MAXSIZE - 1));
	if (len < xfer->length)
		ntrb = howmany(xfer->length - len, XHCI_TRB_MAXSIZE) + 1;
	else
		len = xfer->length;

	/* If we need to append a zero length packet, we need one more. */
	if ((xfer->flags & USBD_FORCE_SHORT_XFER || xfer->length == 0) &&
	    (xfer->length % UE_GET_SIZE(mps) == 0))
		zerotd = 1;

	if (xp->free_trbs < (ntrb + zerotd))
		return (USBD_NOMEM);

	usb_syncmem(&xfer->dmabuf, 0, xfer->length,
	    usbd_xfer_isread(xfer) ?
	    BUS_DMASYNC_PREREAD : BUS_DMASYNC_PREWRITE);

	/* We'll toggle the first TRB once we're finished with the chain. */
	trb0 = xhci_xfer_get_trb(sc, xfer, &toggle, (ntrb == 1));
	flags = XHCI_TRB_TYPE_NORMAL | (toggle ^ 1);
	if (usbd_xfer_isread(xfer))
		flags |= XHCI_TRB_ISP;
	flags |= (ntrb == 1) ? XHCI_TRB_IOC : XHCI_TRB_CHAIN;

	trb0->trb_paddr = htole64(DMAADDR(&xfer->dmabuf, 0));
	trb0->trb_status = htole32(
	    XHCI_TRB_INTR(0) | XHCI_TRB_LEN(len) |
	    xhci_xfer_tdsize(xfer, xfer->length, len)
	);
	trb0->trb_flags = htole32(flags);
	bus_dmamap_sync(xp->ring.dma.tag, xp->ring.dma.map,
	    TRBOFF(&xp->ring, trb0), sizeof(struct xhci_trb),
	    BUS_DMASYNC_PREWRITE);

	remain = xfer->length - len;
	paddr += len;

	/* Chain more TRBs if needed. */
	for (i = ntrb - 1; i > 0; i--) {
		len = min(remain, XHCI_TRB_MAXSIZE);

		/* Next (or Last) TRB. */
		trb = xhci_xfer_get_trb(sc, xfer, &toggle, (i == 1));
		flags = XHCI_TRB_TYPE_NORMAL | toggle;
		if (usbd_xfer_isread(xfer))
			flags |= XHCI_TRB_ISP;
		flags |= (i == 1) ? XHCI_TRB_IOC : XHCI_TRB_CHAIN;

		trb->trb_paddr = htole64(paddr);
		trb->trb_status = htole32(
		    XHCI_TRB_INTR(0) | XHCI_TRB_LEN(len) |
		    xhci_xfer_tdsize(xfer, remain, len)
		);
		trb->trb_flags = htole32(flags);

		bus_dmamap_sync(xp->ring.dma.tag, xp->ring.dma.map,
		    TRBOFF(&xp->ring, trb), sizeof(struct xhci_trb),
		    BUS_DMASYNC_PREWRITE);

		remain -= len;
		paddr += len;
	}

	/* Do we need to issue a zero length transfer? */
	if (zerotd == 1) {
		trb = xhci_xfer_get_trb(sc, xfer, &toggle, -1);
		trb->trb_paddr = 0;
		trb->trb_status = 0;
		trb->trb_flags = htole32(XHCI_TRB_TYPE_NORMAL | XHCI_TRB_IOC | toggle);
		bus_dmamap_sync(xp->ring.dma.tag, xp->ring.dma.map,
		    TRBOFF(&xp->ring, trb), sizeof(struct xhci_trb),
		    BUS_DMASYNC_PREWRITE);
	}

	/* First TRB. */
	trb0->trb_flags ^= htole32(XHCI_TRB_CYCLE);
	bus_dmamap_sync(xp->ring.dma.tag, xp->ring.dma.map,
	    TRBOFF(&xp->ring, trb0), sizeof(struct xhci_trb),
	    BUS_DMASYNC_PREWRITE);

	s = splusb();
	XDWRITE4(sc, XHCI_DOORBELL(xp->slot), xp->dci);

	xfer->status = USBD_IN_PROGRESS;
	if (xfer->timeout && !sc->sc_bus.use_polling) {
		timeout_del(&xfer->timeout_handle);
		timeout_set(&xfer->timeout_handle, xhci_timeout, xfer);
		timeout_add_msec(&xfer->timeout_handle, xfer->timeout);
	}
	splx(s);

	return (USBD_IN_PROGRESS);
}

void
xhci_device_generic_done(struct usbd_xfer *xfer)
{
	/* Only happens with interrupt transfers. */
	if (xfer->pipe->repeat) {
		xfer->actlen = 0;
		xhci_device_generic_start(xfer);
	}
}

void
xhci_device_generic_abort(struct usbd_xfer *xfer)
{
	KASSERT(!xfer->pipe->repeat || xfer->pipe->intrxfer == xfer);

	xhci_abort_xfer(xfer, USBD_CANCELLED);
}

usbd_status
xhci_device_isoc_transfer(struct usbd_xfer *xfer)
{
	usbd_status err;

	err = usb_insert_transfer(xfer);
	if (err && err != USBD_IN_PROGRESS)
		return (err);

	return (xhci_device_isoc_start(xfer));
}

usbd_status
xhci_device_isoc_start(struct usbd_xfer *xfer)
{
	struct xhci_softc *sc = (struct xhci_softc *)xfer->device->bus;
	struct xhci_pipe *xp = (struct xhci_pipe *)xfer->pipe;
	struct xhci_xfer *xx = (struct xhci_xfer *)xfer;
	struct xhci_trb *trb0, *trb;
	uint32_t len, remain, flags;
	uint64_t paddr;
	uint32_t tbc, tlbpc;
	int s, i, j, ntrb = xfer->nframes;
	uint8_t toggle;

	KASSERT(!(xfer->rqflags & URQ_REQUEST));

	/*
	 * To allow continuous transfers, above we start all transfers
	 * immediately. However, we're still going to get usbd_start_next call
	 * this when another xfer completes. So, check if this is already
	 * in progress or not
	 */
	if (xx->ntrb > 0)
		return (USBD_IN_PROGRESS);

	if (sc->sc_bus.dying || xp->halted)
		return (USBD_IOERROR);

	/* Why would you do that anyway? */
	if (sc->sc_bus.use_polling)
		return (USBD_INVAL);

	paddr = DMAADDR(&xfer->dmabuf, 0);

	/* How many TRBs do for all Transfers? */
	for (i = 0, ntrb = 0; i < xfer->nframes; i++) {
		/* How many TRBs do we need for this transfer? */
		ntrb += howmany(xfer->frlengths[i], XHCI_TRB_MAXSIZE);

		/* If the buffer crosses a 64k boundary, we need one more. */
		len = XHCI_TRB_MAXSIZE - (paddr & (XHCI_TRB_MAXSIZE - 1));
		if (len < xfer->frlengths[i])
			ntrb++;

		paddr += xfer->frlengths[i];
	}

	if (xp->free_trbs < ntrb)
		return (USBD_NOMEM);

	usb_syncmem(&xfer->dmabuf, 0, xfer->length,
	    usbd_xfer_isread(xfer) ?
	    BUS_DMASYNC_PREREAD : BUS_DMASYNC_PREWRITE);

	paddr = DMAADDR(&xfer->dmabuf, 0);

	for (i = 0, trb0 = NULL; i < xfer->nframes; i++) {
		/* How many TRBs do we need for this transfer? */
		ntrb = howmany(xfer->frlengths[i], XHCI_TRB_MAXSIZE);

		/* If the buffer crosses a 64k boundary, we need one more. */
		len = XHCI_TRB_MAXSIZE - (paddr & (XHCI_TRB_MAXSIZE - 1));
		if (len < xfer->frlengths[i])
			ntrb++;
		else
			len = xfer->frlengths[i];

		KASSERT(ntrb < 3);

		/*
		 * We'll commit the first TRB once we're finished with the
		 * chain.
		 */
		trb = xhci_xfer_get_trb(sc, xfer, &toggle, (ntrb == 1));

		DPRINTFN(4, ("%s:%d: ring %p trb0_idx %lu ntrb %d paddr %llx "
		    "len %u\n", __func__, __LINE__,
		    &xp->ring.trbs[0], (trb - &xp->ring.trbs[0]), ntrb, paddr,
		    len));

		/* Record the first TRB so we can toggle later. */
		if (trb0 == NULL) {
			trb0 = trb;
			toggle ^= 1;
		}

		flags = XHCI_TRB_TYPE_ISOCH | XHCI_TRB_SIA | toggle;
		if (usbd_xfer_isread(xfer))
			flags |= XHCI_TRB_ISP;
		flags |= (ntrb == 1) ? XHCI_TRB_IOC : XHCI_TRB_CHAIN;

		tbc = xhci_xfer_tbc(xfer, xfer->frlengths[i], &tlbpc);
		flags |= XHCI_TRB_ISOC_TBC(tbc) | XHCI_TRB_ISOC_TLBPC(tlbpc);

		trb->trb_paddr = htole64(paddr);
		trb->trb_status = htole32(
		    XHCI_TRB_INTR(0) | XHCI_TRB_LEN(len) |
		    xhci_xfer_tdsize(xfer, xfer->frlengths[i], len)
		);
		trb->trb_flags = htole32(flags);

		bus_dmamap_sync(xp->ring.dma.tag, xp->ring.dma.map,
		    TRBOFF(&xp->ring, trb), sizeof(struct xhci_trb),
		    BUS_DMASYNC_PREWRITE);

		remain = xfer->frlengths[i] - len;
		paddr += len;

		/* Chain more TRBs if needed. */
		for (j = ntrb - 1; j > 0; j--) {
			len = min(remain, XHCI_TRB_MAXSIZE);

			/* Next (or Last) TRB. */
			trb = xhci_xfer_get_trb(sc, xfer, &toggle, (j == 1));
			flags = XHCI_TRB_TYPE_NORMAL | toggle;
			if (usbd_xfer_isread(xfer))
				flags |= XHCI_TRB_ISP;
			flags |= (j == 1) ? XHCI_TRB_IOC : XHCI_TRB_CHAIN;
			DPRINTFN(3, ("%s:%d: ring %p trb0_idx %lu ntrb %d "
			    "paddr %llx len %u\n", __func__, __LINE__,
			    &xp->ring.trbs[0], (trb - &xp->ring.trbs[0]), ntrb,
			    paddr, len));

			trb->trb_paddr = htole64(paddr);
			trb->trb_status = htole32(
			    XHCI_TRB_INTR(0) | XHCI_TRB_LEN(len) |
			    xhci_xfer_tdsize(xfer, remain, len)
			);
			trb->trb_flags = htole32(flags);

			bus_dmamap_sync(xp->ring.dma.tag, xp->ring.dma.map,
			    TRBOFF(&xp->ring, trb), sizeof(struct xhci_trb),
			    BUS_DMASYNC_PREWRITE);

			remain -= len;
			paddr += len;
		}

		xfer->frlengths[i] = 0;
	}

	/* First TRB. */
	trb0->trb_flags ^= htole32(XHCI_TRB_CYCLE);
	bus_dmamap_sync(xp->ring.dma.tag, xp->ring.dma.map,
	    TRBOFF(&xp->ring, trb0), sizeof(struct xhci_trb),
	    BUS_DMASYNC_PREWRITE);

	s = splusb();
	XDWRITE4(sc, XHCI_DOORBELL(xp->slot), xp->dci);

	xfer->status = USBD_IN_PROGRESS;

	if (xfer->timeout) {
		timeout_del(&xfer->timeout_handle);
		timeout_set(&xfer->timeout_handle, xhci_timeout, xfer);
		timeout_add_msec(&xfer->timeout_handle, xfer->timeout);
	}
	splx(s);

	return (USBD_IN_PROGRESS);
}
