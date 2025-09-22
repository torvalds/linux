/* $OpenBSD: xhcivar.h,v 1.17 2025/02/01 22:46:34 patrick Exp $ */

/*
 * Copyright (c) 2014 Martin Pieuchot
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

#ifndef	_XHCIVAR_H_
#define	_XHCIVAR_H_

/* Default command execution time (implementation defined). */
#define	XHCI_CMD_TIMEOUT	MSEC_TO_NSEC(500)

#define	XHCI_MAX_CMDS		(16 * 1)
#define	XHCI_MAX_EVTS		(16 * 13)
#define	XHCI_MAX_XFER		(16 * 16)

struct usbd_dma_info {
	bus_dma_tag_t		 tag;
	bus_dmamap_t		 map;
	bus_dma_segment_t	 seg;
	int			 nsegs;
	bus_addr_t		 paddr;
	caddr_t			 vaddr;
	bus_size_t		 size;
};

struct xhci_xfer {
	struct usbd_xfer	 xfer;
	int			 index;		/* Index of the last TRB */
	size_t			 ntrb;		/* Number of associated TRBs */
	size_t			 zerotd;	/* Is zero len TD required? */
};

struct xhci_ring {
	struct xhci_trb		*trbs;
	size_t			 ntrb;
	struct usbd_dma_info	 dma;

	uint32_t		 index;
	uint32_t		 toggle;	/* Producer/Consumer bit */
};

struct xhci_soft_dev {
	struct xhci_inctx	*input_ctx;	/* Input context */
	struct xhci_sctx	*slot_ctx;
	struct xhci_epctx	*ep_ctx[31];
	struct usbd_dma_info	 ictx_dma;

	struct usbd_dma_info	 octx_dma;	/* Output context */

	struct xhci_pipe	*pipes[31];
};

/* Device context segment table. */
struct xhci_devctx {
	uint64_t		*segs;		/* at most USB_MAX_DEVICES+1 */
	struct usbd_dma_info	 dma;
};

/* Event ring segment table. */
struct xhci_erst {
	struct xhci_erseg	*segs;		/* One segment per event ring */
	struct usbd_dma_info	 dma;
};

struct xhci_scratchpad {
	struct usbd_dma_info	 table_dma;
	struct usbd_dma_info	 pages_dma;
	int			 npage;
};

struct xhci_softc {
	struct usbd_bus		 sc_bus;

	bus_space_tag_t		 iot;
	bus_space_handle_t	 ioh;
	bus_size_t		 sc_size;

	int			 sc_dead;
	int			 sc_saved_state;

	bus_size_t		 sc_oper_off;	/* Operational Register space */
	bus_size_t		 sc_runt_off;	/* Runtime */
	bus_size_t		 sc_door_off;	/* Doorbell  */

	uint16_t		 sc_version;	/* xHCI version */
	uint32_t		 sc_pagesize;	/* xHCI page size, minimum 4k */
	uint32_t		 sc_ctxsize;	/* 32/64 byte context structs */

	int			 sc_noport;	/* Maximum number of ports */

	u_int8_t		 sc_conf;	/* Device configuration */
	struct usbd_xfer	*sc_intrxfer;	/* Root HUB interrupt xfer */

	struct xhci_devctx	 sc_dcbaa;	/* Device context base addr. */
	struct xhci_ring	 sc_cmd_ring;	/* Command ring */
	struct rwlock		 sc_cmd_lock;	/* Serialize commands */

	struct xhci_erst	 sc_erst;	/* Event ring segment table */
	struct xhci_ring	 sc_evt_ring;	/* Event ring */

	struct xhci_scratchpad	 sc_spad;	/* Optional scratchpad */

	int 			 sc_noslot;	/* Maximum number of slots */
	struct xhci_soft_dev	 sc_sdevs[USB_MAX_DEVICES];

	struct xhci_trb		*sc_cmd_trb;
	struct xhci_trb		 sc_result_trb;

	char			 sc_vendor[16];	/* Vendor string for root hub */
	int			 sc_id_vendor;	/* Vendor ID for root hub */
	
	int			 sc_flags;
#define XHCI_NOCSS		 0x01
};

int	xhci_init(struct xhci_softc *);
void	xhci_config(struct xhci_softc *);
void	xhci_reinit(struct xhci_softc *);
int	xhci_intr(void *);
int	xhci_detach(struct device *, int);
int	xhci_activate(struct device *, int);

static inline uint8_t
xhci_read_1(bus_space_tag_t iot, bus_space_handle_t ioh, bus_size_t offset)
{
	uint32_t reg;
	reg = bus_space_read_4(iot, ioh, offset & ~3);
	return (reg >> ((offset & 3) * 8)) & 0xff;
}

static inline uint16_t
xhci_read_2(bus_space_tag_t iot, bus_space_handle_t ioh, bus_size_t offset)
{
	uint32_t reg;
	reg = bus_space_read_4(iot, ioh, offset & ~2);
	return (reg >> ((offset & 2) * 8)) & 0xffff;
}

#define	XREAD1(sc, a) xhci_read_1((sc)->iot, (sc)->ioh, (a))
#define	XREAD2(sc, a) xhci_read_2((sc)->iot, (sc)->ioh, (a))
#define	XREAD4(sc, a) bus_space_read_4((sc)->iot, (sc)->ioh, (a))
#define	XWRITE1(sc, a, x) bus_space_write_1((sc)->iot, (sc)->ioh, (a), (x))
#define	XWRITE2(sc, a, x) bus_space_write_2((sc)->iot, (sc)->ioh, (a), (x))
#define	XWRITE4(sc, a, x) bus_space_write_4((sc)->iot, (sc)->ioh, (a), (x))

#define	XOREAD4(sc, a)							\
	bus_space_read_4((sc)->iot, (sc)->ioh, (sc)->sc_oper_off + (a))
#define	XOWRITE4(sc, a, x)						\
	bus_space_write_4((sc)->iot, (sc)->ioh, (sc)->sc_oper_off + (a), (x))

#define	XRREAD4(sc, a) \
	bus_space_read_4((sc)->iot, (sc)->ioh, (sc)->sc_runt_off + (a))
#define	XRWRITE4(sc, a, x) \
	bus_space_write_4((sc)->iot, (sc)->ioh, (sc)->sc_runt_off + (a), (x))

#define	XDREAD4(sc, a) \
	bus_space_read_4((sc)->iot, (sc)->ioh, (sc)->sc_door_off + (a))
#define	XDWRITE4(sc, a, x) \
	bus_space_write_4((sc)->iot, (sc)->ioh, (sc)->sc_door_off + (a), (x))

#endif /* _XHCIVAR_H_ */
