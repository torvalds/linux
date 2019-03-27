/* $FreeBSD$ */
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 Hans Petter Selasky. All rights reserved.
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
 * USB eXtensible Host Controller Interface, a.k.a. USB 3.0 controller.
 *
 * The XHCI 1.0 spec can be found at
 * http://www.intel.com/technology/usb/download/xHCI_Specification_for_USB.pdf
 * and the USB 3.0 spec at
 * http://www.usb.org/developers/docs/usb_30_spec_060910.zip
 */

/*
 * A few words about the design implementation: This driver emulates
 * the concept about TDs which is found in EHCI specification. This
 * way we achieve that the USB controller drivers look similar to
 * eachother which makes it easier to understand the code.
 */

#ifdef USB_GLOBAL_INCLUDE_FILE
#include USB_GLOBAL_INCLUDE_FILE
#else
#include <sys/stdint.h>
#include <sys/stddef.h>
#include <sys/param.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/module.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/sysctl.h>
#include <sys/sx.h>
#include <sys/unistd.h>
#include <sys/callout.h>
#include <sys/malloc.h>
#include <sys/priv.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>

#define	USB_DEBUG_VAR xhcidebug

#include <dev/usb/usb_core.h>
#include <dev/usb/usb_debug.h>
#include <dev/usb/usb_busdma.h>
#include <dev/usb/usb_process.h>
#include <dev/usb/usb_transfer.h>
#include <dev/usb/usb_device.h>
#include <dev/usb/usb_hub.h>
#include <dev/usb/usb_util.h>

#include <dev/usb/usb_controller.h>
#include <dev/usb/usb_bus.h>
#endif			/* USB_GLOBAL_INCLUDE_FILE */

#include <dev/usb/controller/xhci.h>
#include <dev/usb/controller/xhcireg.h>

#define	XHCI_BUS2SC(bus) \
   ((struct xhci_softc *)(((uint8_t *)(bus)) - \
    ((uint8_t *)&(((struct xhci_softc *)0)->sc_bus))))

static SYSCTL_NODE(_hw_usb, OID_AUTO, xhci, CTLFLAG_RW, 0, "USB XHCI");

static int xhcistreams;
SYSCTL_INT(_hw_usb_xhci, OID_AUTO, streams, CTLFLAG_RWTUN,
    &xhcistreams, 0, "Set to enable streams mode support");

#ifdef USB_DEBUG
static int xhcidebug;
static int xhciroute;
static int xhcipolling;
static int xhcidma32;
static int xhcictlstep;

SYSCTL_INT(_hw_usb_xhci, OID_AUTO, debug, CTLFLAG_RWTUN,
    &xhcidebug, 0, "Debug level");
SYSCTL_INT(_hw_usb_xhci, OID_AUTO, xhci_port_route, CTLFLAG_RWTUN,
    &xhciroute, 0, "Routing bitmap for switching EHCI ports to the XHCI controller");
SYSCTL_INT(_hw_usb_xhci, OID_AUTO, use_polling, CTLFLAG_RWTUN,
    &xhcipolling, 0, "Set to enable software interrupt polling for the XHCI controller");
SYSCTL_INT(_hw_usb_xhci, OID_AUTO, dma32, CTLFLAG_RWTUN,
    &xhcidma32, 0, "Set to only use 32-bit DMA for the XHCI controller");
SYSCTL_INT(_hw_usb_xhci, OID_AUTO, ctlstep, CTLFLAG_RWTUN,
    &xhcictlstep, 0, "Set to enable control endpoint status stage stepping");
#else
#define	xhciroute 0
#define	xhcidma32 0
#define	xhcictlstep 0
#endif

#define	XHCI_INTR_ENDPT 1

struct xhci_std_temp {
	struct xhci_softc	*sc;
	struct usb_page_cache	*pc;
	struct xhci_td		*td;
	struct xhci_td		*td_next;
	uint32_t		len;
	uint32_t		offset;
	uint32_t		max_packet_size;
	uint32_t		average;
	uint16_t		isoc_delta;
	uint16_t		isoc_frame;
	uint8_t			shortpkt;
	uint8_t			multishort;
	uint8_t			last_frame;
	uint8_t			trb_type;
	uint8_t			direction;
	uint8_t			tbc;
	uint8_t			tlbpc;
	uint8_t			step_td;
	uint8_t			do_isoc_sync;
};

static void	xhci_do_poll(struct usb_bus *);
static void	xhci_device_done(struct usb_xfer *, usb_error_t);
static void	xhci_root_intr(struct xhci_softc *);
static void	xhci_free_device_ext(struct usb_device *);
static struct xhci_endpoint_ext *xhci_get_endpoint_ext(struct usb_device *,
		    struct usb_endpoint_descriptor *);
static usb_proc_callback_t xhci_configure_msg;
static usb_error_t xhci_configure_device(struct usb_device *);
static usb_error_t xhci_configure_endpoint(struct usb_device *,
		   struct usb_endpoint_descriptor *, struct xhci_endpoint_ext *,
		   uint16_t, uint8_t, uint8_t, uint8_t, uint16_t, uint16_t,
		   uint8_t);
static usb_error_t xhci_configure_mask(struct usb_device *,
		    uint32_t, uint8_t);
static usb_error_t xhci_cmd_evaluate_ctx(struct xhci_softc *,
		    uint64_t, uint8_t);
static void xhci_endpoint_doorbell(struct usb_xfer *);
static void xhci_ctx_set_le32(struct xhci_softc *sc, volatile uint32_t *ptr, uint32_t val);
static uint32_t xhci_ctx_get_le32(struct xhci_softc *sc, volatile uint32_t *ptr);
static void xhci_ctx_set_le64(struct xhci_softc *sc, volatile uint64_t *ptr, uint64_t val);
#ifdef USB_DEBUG
static uint64_t xhci_ctx_get_le64(struct xhci_softc *sc, volatile uint64_t *ptr);
#endif

static const struct usb_bus_methods xhci_bus_methods;

#ifdef USB_DEBUG
static void
xhci_dump_trb(struct xhci_trb *trb)
{
	DPRINTFN(5, "trb = %p\n", trb);
	DPRINTFN(5, "qwTrb0 = 0x%016llx\n", (long long)le64toh(trb->qwTrb0));
	DPRINTFN(5, "dwTrb2 = 0x%08x\n", le32toh(trb->dwTrb2));
	DPRINTFN(5, "dwTrb3 = 0x%08x\n", le32toh(trb->dwTrb3));
}

static void
xhci_dump_endpoint(struct xhci_softc *sc, struct xhci_endp_ctx *pep)
{
	DPRINTFN(5, "pep = %p\n", pep);
	DPRINTFN(5, "dwEpCtx0=0x%08x\n", xhci_ctx_get_le32(sc, &pep->dwEpCtx0));
	DPRINTFN(5, "dwEpCtx1=0x%08x\n", xhci_ctx_get_le32(sc, &pep->dwEpCtx1));
	DPRINTFN(5, "qwEpCtx2=0x%016llx\n", (long long)xhci_ctx_get_le64(sc, &pep->qwEpCtx2));
	DPRINTFN(5, "dwEpCtx4=0x%08x\n", xhci_ctx_get_le32(sc, &pep->dwEpCtx4));
	DPRINTFN(5, "dwEpCtx5=0x%08x\n", xhci_ctx_get_le32(sc, &pep->dwEpCtx5));
	DPRINTFN(5, "dwEpCtx6=0x%08x\n", xhci_ctx_get_le32(sc, &pep->dwEpCtx6));
	DPRINTFN(5, "dwEpCtx7=0x%08x\n", xhci_ctx_get_le32(sc, &pep->dwEpCtx7));
}

static void
xhci_dump_device(struct xhci_softc *sc, struct xhci_slot_ctx *psl)
{
	DPRINTFN(5, "psl = %p\n", psl);
	DPRINTFN(5, "dwSctx0=0x%08x\n", xhci_ctx_get_le32(sc, &psl->dwSctx0));
	DPRINTFN(5, "dwSctx1=0x%08x\n", xhci_ctx_get_le32(sc, &psl->dwSctx1));
	DPRINTFN(5, "dwSctx2=0x%08x\n", xhci_ctx_get_le32(sc, &psl->dwSctx2));
	DPRINTFN(5, "dwSctx3=0x%08x\n", xhci_ctx_get_le32(sc, &psl->dwSctx3));
}
#endif

uint8_t
xhci_use_polling(void)
{
#ifdef USB_DEBUG
	return (xhcipolling != 0);
#else
	return (0);
#endif
}

static void
xhci_iterate_hw_softc(struct usb_bus *bus, usb_bus_mem_sub_cb_t *cb)
{
	struct xhci_softc *sc = XHCI_BUS2SC(bus);
	uint16_t i;

	cb(bus, &sc->sc_hw.root_pc, &sc->sc_hw.root_pg,
	   sizeof(struct xhci_hw_root), XHCI_PAGE_SIZE);

	cb(bus, &sc->sc_hw.ctx_pc, &sc->sc_hw.ctx_pg,
	   sizeof(struct xhci_dev_ctx_addr), XHCI_PAGE_SIZE);

	for (i = 0; i != sc->sc_noscratch; i++) {
		cb(bus, &sc->sc_hw.scratch_pc[i], &sc->sc_hw.scratch_pg[i],
		    XHCI_PAGE_SIZE, XHCI_PAGE_SIZE);
	}
}

static void
xhci_ctx_set_le32(struct xhci_softc *sc, volatile uint32_t *ptr, uint32_t val)
{
	if (sc->sc_ctx_is_64_byte) {
		uint32_t offset;
		/* exploit the fact that our structures are XHCI_PAGE_SIZE aligned */
		/* all contexts are initially 32-bytes */
		offset = ((uintptr_t)ptr) & ((XHCI_PAGE_SIZE - 1) & ~(31U));
		ptr = (volatile uint32_t *)(((volatile uint8_t *)ptr) + offset);
	}
	*ptr = htole32(val);
}

static uint32_t
xhci_ctx_get_le32(struct xhci_softc *sc, volatile uint32_t *ptr)
{
	if (sc->sc_ctx_is_64_byte) {
		uint32_t offset;
		/* exploit the fact that our structures are XHCI_PAGE_SIZE aligned */
		/* all contexts are initially 32-bytes */
		offset = ((uintptr_t)ptr) & ((XHCI_PAGE_SIZE - 1) & ~(31U));
		ptr = (volatile uint32_t *)(((volatile uint8_t *)ptr) + offset);
	}
	return (le32toh(*ptr));
}

static void
xhci_ctx_set_le64(struct xhci_softc *sc, volatile uint64_t *ptr, uint64_t val)
{
	if (sc->sc_ctx_is_64_byte) {
		uint32_t offset;
		/* exploit the fact that our structures are XHCI_PAGE_SIZE aligned */
		/* all contexts are initially 32-bytes */
		offset = ((uintptr_t)ptr) & ((XHCI_PAGE_SIZE - 1) & ~(31U));
		ptr = (volatile uint64_t *)(((volatile uint8_t *)ptr) + offset);
	}
	*ptr = htole64(val);
}

#ifdef USB_DEBUG
static uint64_t
xhci_ctx_get_le64(struct xhci_softc *sc, volatile uint64_t *ptr)
{
	if (sc->sc_ctx_is_64_byte) {
		uint32_t offset;
		/* exploit the fact that our structures are XHCI_PAGE_SIZE aligned */
		/* all contexts are initially 32-bytes */
		offset = ((uintptr_t)ptr) & ((XHCI_PAGE_SIZE - 1) & ~(31U));
		ptr = (volatile uint64_t *)(((volatile uint8_t *)ptr) + offset);
	}
	return (le64toh(*ptr));
}
#endif

static int
xhci_reset_command_queue_locked(struct xhci_softc *sc)
{
	struct usb_page_search buf_res;
	struct xhci_hw_root *phwr;
	uint64_t addr;
	uint32_t temp;

	DPRINTF("\n");

	temp = XREAD4(sc, oper, XHCI_CRCR_LO);
	if (temp & XHCI_CRCR_LO_CRR) {
		DPRINTF("Command ring running\n");
		temp &= ~(XHCI_CRCR_LO_CS | XHCI_CRCR_LO_CA);

		/*
		 * Try to abort the last command as per section
		 * 4.6.1.2 "Aborting a Command" of the XHCI
		 * specification:
		 */

		/* stop and cancel */
		XWRITE4(sc, oper, XHCI_CRCR_LO, temp | XHCI_CRCR_LO_CS);
		XWRITE4(sc, oper, XHCI_CRCR_HI, 0);

		XWRITE4(sc, oper, XHCI_CRCR_LO, temp | XHCI_CRCR_LO_CA);
		XWRITE4(sc, oper, XHCI_CRCR_HI, 0);

 		/* wait 250ms */
 		usb_pause_mtx(&sc->sc_bus.bus_mtx, hz / 4);

		/* check if command ring is still running */
		temp = XREAD4(sc, oper, XHCI_CRCR_LO);
		if (temp & XHCI_CRCR_LO_CRR) {
			DPRINTF("Comand ring still running\n");
			return (USB_ERR_IOERROR);
		}
	}

	/* reset command ring */
	sc->sc_command_ccs = 1;
	sc->sc_command_idx = 0;

	usbd_get_page(&sc->sc_hw.root_pc, 0, &buf_res);

	/* set up command ring control base address */
	addr = buf_res.physaddr;
	phwr = buf_res.buffer;
	addr += (uintptr_t)&((struct xhci_hw_root *)0)->hwr_commands[0];

	DPRINTF("CRCR=0x%016llx\n", (unsigned long long)addr);

	memset(phwr->hwr_commands, 0, sizeof(phwr->hwr_commands));
	phwr->hwr_commands[XHCI_MAX_COMMANDS - 1].qwTrb0 = htole64(addr);

	usb_pc_cpu_flush(&sc->sc_hw.root_pc);

	XWRITE4(sc, oper, XHCI_CRCR_LO, ((uint32_t)addr) | XHCI_CRCR_LO_RCS);
	XWRITE4(sc, oper, XHCI_CRCR_HI, (uint32_t)(addr >> 32));

	return (0);
}

usb_error_t
xhci_start_controller(struct xhci_softc *sc)
{
	struct usb_page_search buf_res;
	struct xhci_hw_root *phwr;
	struct xhci_dev_ctx_addr *pdctxa;
	usb_error_t err;
	uint64_t addr;
	uint32_t temp;
	uint16_t i;

	DPRINTF("\n");

	sc->sc_event_ccs = 1;
	sc->sc_event_idx = 0;
	sc->sc_command_ccs = 1;
	sc->sc_command_idx = 0;

	err = xhci_reset_controller(sc);
	if (err)
		return (err);

	/* set up number of device slots */
	DPRINTF("CONFIG=0x%08x -> 0x%08x\n",
	    XREAD4(sc, oper, XHCI_CONFIG), sc->sc_noslot);

	XWRITE4(sc, oper, XHCI_CONFIG, sc->sc_noslot);

	temp = XREAD4(sc, oper, XHCI_USBSTS);

	/* clear interrupts */
	XWRITE4(sc, oper, XHCI_USBSTS, temp);
	/* disable all device notifications */
	XWRITE4(sc, oper, XHCI_DNCTRL, 0);

	/* set up device context base address */
	usbd_get_page(&sc->sc_hw.ctx_pc, 0, &buf_res);
	pdctxa = buf_res.buffer;
	memset(pdctxa, 0, sizeof(*pdctxa));

	addr = buf_res.physaddr;
	addr += (uintptr_t)&((struct xhci_dev_ctx_addr *)0)->qwSpBufPtr[0];

	/* slot 0 points to the table of scratchpad pointers */
	pdctxa->qwBaaDevCtxAddr[0] = htole64(addr);

	for (i = 0; i != sc->sc_noscratch; i++) {
		struct usb_page_search buf_scp;
		usbd_get_page(&sc->sc_hw.scratch_pc[i], 0, &buf_scp);
		pdctxa->qwSpBufPtr[i] = htole64((uint64_t)buf_scp.physaddr);
	}

	addr = buf_res.physaddr;

	XWRITE4(sc, oper, XHCI_DCBAAP_LO, (uint32_t)addr);
	XWRITE4(sc, oper, XHCI_DCBAAP_HI, (uint32_t)(addr >> 32));
	XWRITE4(sc, oper, XHCI_DCBAAP_LO, (uint32_t)addr);
	XWRITE4(sc, oper, XHCI_DCBAAP_HI, (uint32_t)(addr >> 32));

	/* set up event table size */
	DPRINTF("ERSTSZ=0x%08x -> 0x%08x\n",
	    XREAD4(sc, runt, XHCI_ERSTSZ(0)), sc->sc_erst_max);

	XWRITE4(sc, runt, XHCI_ERSTSZ(0), XHCI_ERSTS_SET(sc->sc_erst_max));

	/* set up interrupt rate */
	XWRITE4(sc, runt, XHCI_IMOD(0), sc->sc_imod_default);

	usbd_get_page(&sc->sc_hw.root_pc, 0, &buf_res);

	phwr = buf_res.buffer;
	addr = buf_res.physaddr;
	addr += (uintptr_t)&((struct xhci_hw_root *)0)->hwr_events[0];

	/* reset hardware root structure */
	memset(phwr, 0, sizeof(*phwr));

	phwr->hwr_ring_seg[0].qwEvrsTablePtr = htole64(addr);
	phwr->hwr_ring_seg[0].dwEvrsTableSize = htole32(XHCI_MAX_EVENTS);

	DPRINTF("ERDP(0)=0x%016llx\n", (unsigned long long)addr);

	XWRITE4(sc, runt, XHCI_ERDP_LO(0), (uint32_t)addr);
	XWRITE4(sc, runt, XHCI_ERDP_HI(0), (uint32_t)(addr >> 32));

	addr = buf_res.physaddr;

	DPRINTF("ERSTBA(0)=0x%016llx\n", (unsigned long long)addr);

	XWRITE4(sc, runt, XHCI_ERSTBA_LO(0), (uint32_t)addr);
	XWRITE4(sc, runt, XHCI_ERSTBA_HI(0), (uint32_t)(addr >> 32));

	/* set up interrupter registers */
	temp = XREAD4(sc, runt, XHCI_IMAN(0));
	temp |= XHCI_IMAN_INTR_ENA;
	XWRITE4(sc, runt, XHCI_IMAN(0), temp);

	/* set up command ring control base address */
	addr = buf_res.physaddr;
	addr += (uintptr_t)&((struct xhci_hw_root *)0)->hwr_commands[0];

	DPRINTF("CRCR=0x%016llx\n", (unsigned long long)addr);

	XWRITE4(sc, oper, XHCI_CRCR_LO, ((uint32_t)addr) | XHCI_CRCR_LO_RCS);
	XWRITE4(sc, oper, XHCI_CRCR_HI, (uint32_t)(addr >> 32));

	phwr->hwr_commands[XHCI_MAX_COMMANDS - 1].qwTrb0 = htole64(addr);

	usb_bus_mem_flush_all(&sc->sc_bus, &xhci_iterate_hw_softc);

	/* Go! */
	XWRITE4(sc, oper, XHCI_USBCMD, XHCI_CMD_RS |
	    XHCI_CMD_INTE | XHCI_CMD_HSEE);

	for (i = 0; i != 100; i++) {
		usb_pause_mtx(NULL, hz / 100);
		temp = XREAD4(sc, oper, XHCI_USBSTS) & XHCI_STS_HCH;
		if (!temp)
			break;
	}
	if (temp) {
		XWRITE4(sc, oper, XHCI_USBCMD, 0);
		device_printf(sc->sc_bus.parent, "Run timeout.\n");
		return (USB_ERR_IOERROR);
	}

	/* catch any lost interrupts */
	xhci_do_poll(&sc->sc_bus);

	if (sc->sc_port_route != NULL) {
		/* Route all ports to the XHCI by default */
		sc->sc_port_route(sc->sc_bus.parent,
		    ~xhciroute, xhciroute);
	}
	return (0);
}

usb_error_t
xhci_halt_controller(struct xhci_softc *sc)
{
	uint32_t temp;
	uint16_t i;

	DPRINTF("\n");

	sc->sc_capa_off = 0;
	sc->sc_oper_off = XREAD1(sc, capa, XHCI_CAPLENGTH);
	sc->sc_runt_off = XREAD4(sc, capa, XHCI_RTSOFF) & ~0xF;
	sc->sc_door_off = XREAD4(sc, capa, XHCI_DBOFF) & ~0x3;

	/* Halt controller */
	XWRITE4(sc, oper, XHCI_USBCMD, 0);

	for (i = 0; i != 100; i++) {
		usb_pause_mtx(NULL, hz / 100);
		temp = XREAD4(sc, oper, XHCI_USBSTS) & XHCI_STS_HCH;
		if (temp)
			break;
	}

	if (!temp) {
		device_printf(sc->sc_bus.parent, "Controller halt timeout.\n");
		return (USB_ERR_IOERROR);
	}
	return (0);
}

usb_error_t
xhci_reset_controller(struct xhci_softc *sc)
{
	uint32_t temp = 0;
	uint16_t i;

	DPRINTF("\n");

	/* Reset controller */
	XWRITE4(sc, oper, XHCI_USBCMD, XHCI_CMD_HCRST);

	for (i = 0; i != 100; i++) {
		usb_pause_mtx(NULL, hz / 100);
		temp = (XREAD4(sc, oper, XHCI_USBCMD) & XHCI_CMD_HCRST) |
		    (XREAD4(sc, oper, XHCI_USBSTS) & XHCI_STS_CNR);
		if (!temp)
			break;
	}

	if (temp) {
		device_printf(sc->sc_bus.parent, "Controller "
		    "reset timeout.\n");
		return (USB_ERR_IOERROR);
	}
	return (0);
}

usb_error_t
xhci_init(struct xhci_softc *sc, device_t self, uint8_t dma32)
{
	uint32_t temp;

	DPRINTF("\n");

	/* initialize some bus fields */
	sc->sc_bus.parent = self;

	/* set the bus revision */
	sc->sc_bus.usbrev = USB_REV_3_0;

	/* set up the bus struct */
	sc->sc_bus.methods = &xhci_bus_methods;

	/* set up devices array */
	sc->sc_bus.devices = sc->sc_devices;
	sc->sc_bus.devices_max = XHCI_MAX_DEVICES;

	/* set default cycle state in case of early interrupts */
	sc->sc_event_ccs = 1;
	sc->sc_command_ccs = 1;

	/* set up bus space offsets */
	sc->sc_capa_off = 0;
	sc->sc_oper_off = XREAD1(sc, capa, XHCI_CAPLENGTH);
	sc->sc_runt_off = XREAD4(sc, capa, XHCI_RTSOFF) & ~0x1F;
	sc->sc_door_off = XREAD4(sc, capa, XHCI_DBOFF) & ~0x3;

	DPRINTF("CAPLENGTH=0x%x\n", sc->sc_oper_off);
	DPRINTF("RUNTIMEOFFSET=0x%x\n", sc->sc_runt_off);
	DPRINTF("DOOROFFSET=0x%x\n", sc->sc_door_off);

	DPRINTF("xHCI version = 0x%04x\n", XREAD2(sc, capa, XHCI_HCIVERSION));

	if (!(XREAD4(sc, oper, XHCI_PAGESIZE) & XHCI_PAGESIZE_4K)) {
		device_printf(sc->sc_bus.parent, "Controller does "
		    "not support 4K page size.\n");
		return (ENXIO);
	}

	temp = XREAD4(sc, capa, XHCI_HCSPARAMS0);

	DPRINTF("HCS0 = 0x%08x\n", temp);

	/* set up context size */
	if (XHCI_HCS0_CSZ(temp)) {
		sc->sc_ctx_is_64_byte = 1;
	} else {
		sc->sc_ctx_is_64_byte = 0;
	}

	/* get DMA bits */
	sc->sc_bus.dma_bits = (XHCI_HCS0_AC64(temp) &&
	    xhcidma32 == 0 && dma32 == 0) ? 64 : 32;

	device_printf(self, "%d bytes context size, %d-bit DMA\n",
	    sc->sc_ctx_is_64_byte ? 64 : 32, (int)sc->sc_bus.dma_bits);

	temp = XREAD4(sc, capa, XHCI_HCSPARAMS1);

	/* get number of device slots */
	sc->sc_noport = XHCI_HCS1_N_PORTS(temp);

	if (sc->sc_noport == 0) {
		device_printf(sc->sc_bus.parent, "Invalid number "
		    "of ports: %u\n", sc->sc_noport);
		return (ENXIO);
	}

	sc->sc_noport = sc->sc_noport;
	sc->sc_noslot = XHCI_HCS1_DEVSLOT_MAX(temp);

	DPRINTF("Max slots: %u\n", sc->sc_noslot);

	if (sc->sc_noslot > XHCI_MAX_DEVICES)
		sc->sc_noslot = XHCI_MAX_DEVICES;

	temp = XREAD4(sc, capa, XHCI_HCSPARAMS2);

	DPRINTF("HCS2=0x%08x\n", temp);

	/* get number of scratchpads */
	sc->sc_noscratch = XHCI_HCS2_SPB_MAX(temp);

	if (sc->sc_noscratch > XHCI_MAX_SCRATCHPADS) {
		device_printf(sc->sc_bus.parent, "XHCI request "
		    "too many scratchpads\n");
		return (ENOMEM);
	}

	DPRINTF("Max scratch: %u\n", sc->sc_noscratch);

	/* get event table size */
	sc->sc_erst_max = 1U << XHCI_HCS2_ERST_MAX(temp);
	if (sc->sc_erst_max > XHCI_MAX_RSEG)
		sc->sc_erst_max = XHCI_MAX_RSEG;

	temp = XREAD4(sc, capa, XHCI_HCSPARAMS3);

	/* get maximum exit latency */
	sc->sc_exit_lat_max = XHCI_HCS3_U1_DEL(temp) +
	    XHCI_HCS3_U2_DEL(temp) + 250 /* us */;

	/* Check if we should use the default IMOD value. */
	if (sc->sc_imod_default == 0)
		sc->sc_imod_default = XHCI_IMOD_DEFAULT;

	/* get all DMA memory */
	if (usb_bus_mem_alloc_all(&sc->sc_bus,
	    USB_GET_DMA_TAG(self), &xhci_iterate_hw_softc)) {
		return (ENOMEM);
	}

	/* set up command queue mutex and condition varible */
	cv_init(&sc->sc_cmd_cv, "CMDQ");
	sx_init(&sc->sc_cmd_sx, "CMDQ lock");

	sc->sc_config_msg[0].hdr.pm_callback = &xhci_configure_msg;
	sc->sc_config_msg[0].bus = &sc->sc_bus;
	sc->sc_config_msg[1].hdr.pm_callback = &xhci_configure_msg;
	sc->sc_config_msg[1].bus = &sc->sc_bus;

	return (0);
}

void
xhci_uninit(struct xhci_softc *sc)
{
	/*
	 * NOTE: At this point the control transfer process is gone
	 * and "xhci_configure_msg" is no longer called. Consequently
	 * waiting for the configuration messages to complete is not
	 * needed.
	 */
	usb_bus_mem_free_all(&sc->sc_bus, &xhci_iterate_hw_softc);

	cv_destroy(&sc->sc_cmd_cv);
	sx_destroy(&sc->sc_cmd_sx);
}

static void
xhci_set_hw_power_sleep(struct usb_bus *bus, uint32_t state)
{
	struct xhci_softc *sc = XHCI_BUS2SC(bus);

	switch (state) {
	case USB_HW_POWER_SUSPEND:
		DPRINTF("Stopping the XHCI\n");
		xhci_halt_controller(sc);
		xhci_reset_controller(sc);
		break;
	case USB_HW_POWER_SHUTDOWN:
		DPRINTF("Stopping the XHCI\n");
		xhci_halt_controller(sc);
		xhci_reset_controller(sc);
		break;
	case USB_HW_POWER_RESUME:
		DPRINTF("Starting the XHCI\n");
		xhci_start_controller(sc);
		break;
	default:
		break;
	}
}

static usb_error_t
xhci_generic_done_sub(struct usb_xfer *xfer)
{
	struct xhci_td *td;
	struct xhci_td *td_alt_next;
	uint32_t len;
	uint8_t status;

	td = xfer->td_transfer_cache;
	td_alt_next = td->alt_next;

	if (xfer->aframes != xfer->nframes)
		usbd_xfer_set_frame_len(xfer, xfer->aframes, 0);

	while (1) {

		usb_pc_cpu_invalidate(td->page_cache);

		status = td->status;
		len = td->remainder;

		DPRINTFN(4, "xfer=%p[%u/%u] rem=%u/%u status=%u\n",
		    xfer, (unsigned int)xfer->aframes,
		    (unsigned int)xfer->nframes,
		    (unsigned int)len, (unsigned int)td->len,
		    (unsigned int)status);

		/*
	         * Verify the status length and
		 * add the length to "frlengths[]":
	         */
		if (len > td->len) {
			/* should not happen */
			DPRINTF("Invalid status length, "
			    "0x%04x/0x%04x bytes\n", len, td->len);
			status = XHCI_TRB_ERROR_LENGTH;
		} else if (xfer->aframes != xfer->nframes) {
			xfer->frlengths[xfer->aframes] += td->len - len;
		}
		/* Check for last transfer */
		if (((void *)td) == xfer->td_transfer_last) {
			td = NULL;
			break;
		}
		/* Check for transfer error */
		if (status != XHCI_TRB_ERROR_SHORT_PKT &&
		    status != XHCI_TRB_ERROR_SUCCESS) {
			/* the transfer is finished */
			td = NULL;
			break;
		}
		/* Check for short transfer */
		if (len > 0) {
			if (xfer->flags_int.short_frames_ok || 
			    xfer->flags_int.isochronous_xfr ||
			    xfer->flags_int.control_xfr) {
				/* follow alt next */
				td = td->alt_next;
			} else {
				/* the transfer is finished */
				td = NULL;
			}
			break;
		}
		td = td->obj_next;

		if (td->alt_next != td_alt_next) {
			/* this USB frame is complete */
			break;
		}
	}

	/* update transfer cache */

	xfer->td_transfer_cache = td;

	return ((status == XHCI_TRB_ERROR_STALL) ? USB_ERR_STALLED : 
	    (status != XHCI_TRB_ERROR_SHORT_PKT && 
	    status != XHCI_TRB_ERROR_SUCCESS) ? USB_ERR_IOERROR :
	    USB_ERR_NORMAL_COMPLETION);
}

static void
xhci_generic_done(struct usb_xfer *xfer)
{
	usb_error_t err = 0;

	DPRINTFN(13, "xfer=%p endpoint=%p transfer done\n",
	    xfer, xfer->endpoint);

	/* reset scanner */

	xfer->td_transfer_cache = xfer->td_transfer_first;

	if (xfer->flags_int.control_xfr) {

		if (xfer->flags_int.control_hdr)
			err = xhci_generic_done_sub(xfer);

		xfer->aframes = 1;

		if (xfer->td_transfer_cache == NULL)
			goto done;
	}

	while (xfer->aframes != xfer->nframes) {

		err = xhci_generic_done_sub(xfer);
		xfer->aframes++;

		if (xfer->td_transfer_cache == NULL)
			goto done;
	}

	if (xfer->flags_int.control_xfr &&
	    !xfer->flags_int.control_act)
		err = xhci_generic_done_sub(xfer);
done:
	/* transfer is complete */
	xhci_device_done(xfer, err);
}

static void
xhci_activate_transfer(struct usb_xfer *xfer)
{
	struct xhci_td *td;

	td = xfer->td_transfer_cache;

	usb_pc_cpu_invalidate(td->page_cache);

	if (!(td->td_trb[0].dwTrb3 & htole32(XHCI_TRB_3_CYCLE_BIT))) {

		/* activate the transfer */

		td->td_trb[0].dwTrb3 |= htole32(XHCI_TRB_3_CYCLE_BIT);
		usb_pc_cpu_flush(td->page_cache);

		xhci_endpoint_doorbell(xfer);
	}
}

static void
xhci_skip_transfer(struct usb_xfer *xfer)
{
	struct xhci_td *td;
	struct xhci_td *td_last;

	td = xfer->td_transfer_cache;
	td_last = xfer->td_transfer_last;

	td = td->alt_next;

	usb_pc_cpu_invalidate(td->page_cache);

	if (!(td->td_trb[0].dwTrb3 & htole32(XHCI_TRB_3_CYCLE_BIT))) {

		usb_pc_cpu_invalidate(td_last->page_cache);

		/* copy LINK TRB to current waiting location */

		td->td_trb[0].qwTrb0 = td_last->td_trb[td_last->ntrb].qwTrb0;
		td->td_trb[0].dwTrb2 = td_last->td_trb[td_last->ntrb].dwTrb2;
		usb_pc_cpu_flush(td->page_cache);

		td->td_trb[0].dwTrb3 = td_last->td_trb[td_last->ntrb].dwTrb3;
		usb_pc_cpu_flush(td->page_cache);

		xhci_endpoint_doorbell(xfer);
	}
}

/*------------------------------------------------------------------------*
 *	xhci_check_transfer
 *------------------------------------------------------------------------*/
static void
xhci_check_transfer(struct xhci_softc *sc, struct xhci_trb *trb)
{
	struct xhci_endpoint_ext *pepext;
	int64_t offset;
	uint64_t td_event;
	uint32_t temp;
	uint32_t remainder;
	uint16_t stream_id;
	uint16_t i;
	uint8_t status;
	uint8_t halted;
	uint8_t epno;
	uint8_t index;

	/* decode TRB */
	td_event = le64toh(trb->qwTrb0);
	temp = le32toh(trb->dwTrb2);

	remainder = XHCI_TRB_2_REM_GET(temp);
	status = XHCI_TRB_2_ERROR_GET(temp);
	stream_id = XHCI_TRB_2_STREAM_GET(temp);

	temp = le32toh(trb->dwTrb3);
	epno = XHCI_TRB_3_EP_GET(temp);
	index = XHCI_TRB_3_SLOT_GET(temp);

	/* check if error means halted */
	halted = (status != XHCI_TRB_ERROR_SHORT_PKT &&
	    status != XHCI_TRB_ERROR_SUCCESS);

	DPRINTF("slot=%u epno=%u stream=%u remainder=%u status=%u\n",
	    index, epno, stream_id, remainder, status);

	if (index > sc->sc_noslot) {
		DPRINTF("Invalid slot.\n");
		return;
	}

	if ((epno == 0) || (epno >= XHCI_MAX_ENDPOINTS)) {
		DPRINTF("Invalid endpoint.\n");
		return;
	}

	pepext = &sc->sc_hw.devs[index].endp[epno];

	if (pepext->trb_ep_mode != USB_EP_MODE_STREAMS) {
		stream_id = 0;
		DPRINTF("stream_id=0\n");
	} else if (stream_id >= XHCI_MAX_STREAMS) {
		DPRINTF("Invalid stream ID.\n");
		return;
	}

	/* try to find the USB transfer that generated the event */
	for (i = 0; i != (XHCI_MAX_TRANSFERS - 1); i++) {
		struct usb_xfer *xfer;
		struct xhci_td *td;

		xfer = pepext->xfer[i + (XHCI_MAX_TRANSFERS * stream_id)];
		if (xfer == NULL)
			continue;

		td = xfer->td_transfer_cache;

		DPRINTFN(5, "Checking if 0x%016llx == (0x%016llx .. 0x%016llx)\n",
			(long long)td_event,
			(long long)td->td_self,
			(long long)td->td_self + sizeof(td->td_trb));

		/*
		 * NOTE: Some XHCI implementations might not trigger
		 * an event on the last LINK TRB so we need to
		 * consider both the last and second last event
		 * address as conditions for a successful transfer.
		 *
		 * NOTE: We assume that the XHCI will only trigger one
		 * event per chain of TRBs.
		 */

		offset = td_event - td->td_self;

		if (offset >= 0 &&
		    offset < (int64_t)sizeof(td->td_trb)) {

			usb_pc_cpu_invalidate(td->page_cache);

			/* compute rest of remainder, if any */
			for (i = (offset / 16) + 1; i < td->ntrb; i++) {
				temp = le32toh(td->td_trb[i].dwTrb2);
				remainder += XHCI_TRB_2_BYTES_GET(temp);
			}

			DPRINTFN(5, "New remainder: %u\n", remainder);

			/* clear isochronous transfer errors */
			if (xfer->flags_int.isochronous_xfr) {
				if (halted) {
					halted = 0;
					status = XHCI_TRB_ERROR_SUCCESS;
					remainder = td->len;
				}
			}

			/* "td->remainder" is verified later */
			td->remainder = remainder;
			td->status = status;

			usb_pc_cpu_flush(td->page_cache);

			/*
			 * 1) Last transfer descriptor makes the
			 * transfer done
			 */
			if (((void *)td) == xfer->td_transfer_last) {
				DPRINTF("TD is last\n");
				xhci_generic_done(xfer);
				break;
			}

			/*
			 * 2) Any kind of error makes the transfer
			 * done
			 */
			if (halted) {
				DPRINTF("TD has I/O error\n");
				xhci_generic_done(xfer);
				break;
			}

			/*
			 * 3) If there is no alternate next transfer,
			 * a short packet also makes the transfer done
			 */
			if (td->remainder > 0) {
				if (td->alt_next == NULL) {
					DPRINTF(
					    "short TD has no alternate next\n");
					xhci_generic_done(xfer);
					break;
				}
				DPRINTF("TD has short pkt\n");
				if (xfer->flags_int.short_frames_ok ||
				    xfer->flags_int.isochronous_xfr ||
				    xfer->flags_int.control_xfr) {
					/* follow the alt next */
					xfer->td_transfer_cache = td->alt_next;
					xhci_activate_transfer(xfer);
					break;
				}
				xhci_skip_transfer(xfer);
				xhci_generic_done(xfer);
				break;
			}

			/*
			 * 4) Transfer complete - go to next TD
			 */
			DPRINTF("Following next TD\n");
			xfer->td_transfer_cache = td->obj_next;
			xhci_activate_transfer(xfer);
			break;		/* there should only be one match */
		}
	}
}

static int
xhci_check_command(struct xhci_softc *sc, struct xhci_trb *trb)
{
	if (sc->sc_cmd_addr == trb->qwTrb0) {
		DPRINTF("Received command event\n");
		sc->sc_cmd_result[0] = trb->dwTrb2;
		sc->sc_cmd_result[1] = trb->dwTrb3;
		cv_signal(&sc->sc_cmd_cv);
		return (1);	/* command match */
	}
	return (0);
}

static int
xhci_interrupt_poll(struct xhci_softc *sc)
{
	struct usb_page_search buf_res;
	struct xhci_hw_root *phwr;
	uint64_t addr;
	uint32_t temp;
	int retval = 0;
	uint16_t i;
	uint8_t event;
	uint8_t j;
	uint8_t k;
	uint8_t t;

	usbd_get_page(&sc->sc_hw.root_pc, 0, &buf_res);

	phwr = buf_res.buffer;

	/* Receive any events */

	usb_pc_cpu_invalidate(&sc->sc_hw.root_pc);

	i = sc->sc_event_idx;
	j = sc->sc_event_ccs;
	t = 2;

	while (1) {

		temp = le32toh(phwr->hwr_events[i].dwTrb3);

		k = (temp & XHCI_TRB_3_CYCLE_BIT) ? 1 : 0;

		if (j != k)
			break;

		event = XHCI_TRB_3_TYPE_GET(temp);

		DPRINTFN(10, "event[%u] = %u (0x%016llx 0x%08lx 0x%08lx)\n",
		    i, event, (long long)le64toh(phwr->hwr_events[i].qwTrb0),
		    (long)le32toh(phwr->hwr_events[i].dwTrb2),
		    (long)le32toh(phwr->hwr_events[i].dwTrb3));

		switch (event) {
		case XHCI_TRB_EVENT_TRANSFER:
			xhci_check_transfer(sc, &phwr->hwr_events[i]);
			break;
		case XHCI_TRB_EVENT_CMD_COMPLETE:
			retval |= xhci_check_command(sc, &phwr->hwr_events[i]);
			break;
		default:
			DPRINTF("Unhandled event = %u\n", event);
			break;
		}

		i++;

		if (i == XHCI_MAX_EVENTS) {
			i = 0;
			j ^= 1;

			/* check for timeout */
			if (!--t)
				break;
		}
	}

	sc->sc_event_idx = i;
	sc->sc_event_ccs = j;

	/*
	 * NOTE: The Event Ring Dequeue Pointer Register is 64-bit
	 * latched. That means to activate the register we need to
	 * write both the low and high double word of the 64-bit
	 * register.
	 */

	addr = buf_res.physaddr;
	addr += (uintptr_t)&((struct xhci_hw_root *)0)->hwr_events[i];

	/* try to clear busy bit */
	addr |= XHCI_ERDP_LO_BUSY;

	XWRITE4(sc, runt, XHCI_ERDP_LO(0), (uint32_t)addr);
	XWRITE4(sc, runt, XHCI_ERDP_HI(0), (uint32_t)(addr >> 32));

	return (retval);
}

static usb_error_t
xhci_do_command(struct xhci_softc *sc, struct xhci_trb *trb, 
    uint16_t timeout_ms)
{
	struct usb_page_search buf_res;
	struct xhci_hw_root *phwr;
	uint64_t addr;
	uint32_t temp;
	uint8_t i;
	uint8_t j;
	uint8_t timeout = 0;
	int err;

	XHCI_CMD_ASSERT_LOCKED(sc);

	/* get hardware root structure */

	usbd_get_page(&sc->sc_hw.root_pc, 0, &buf_res);

	phwr = buf_res.buffer;

	/* Queue command */

	USB_BUS_LOCK(&sc->sc_bus);
retry:
	i = sc->sc_command_idx;
	j = sc->sc_command_ccs;

	DPRINTFN(10, "command[%u] = %u (0x%016llx, 0x%08lx, 0x%08lx)\n",
	    i, XHCI_TRB_3_TYPE_GET(le32toh(trb->dwTrb3)),
	    (long long)le64toh(trb->qwTrb0),
	    (long)le32toh(trb->dwTrb2),
	    (long)le32toh(trb->dwTrb3));

	phwr->hwr_commands[i].qwTrb0 = trb->qwTrb0;
	phwr->hwr_commands[i].dwTrb2 = trb->dwTrb2;

	usb_pc_cpu_flush(&sc->sc_hw.root_pc);

	temp = trb->dwTrb3;

	if (j)
		temp |= htole32(XHCI_TRB_3_CYCLE_BIT);
	else
		temp &= ~htole32(XHCI_TRB_3_CYCLE_BIT);

	temp &= ~htole32(XHCI_TRB_3_TC_BIT);

	phwr->hwr_commands[i].dwTrb3 = temp;

	usb_pc_cpu_flush(&sc->sc_hw.root_pc);

	addr = buf_res.physaddr;
	addr += (uintptr_t)&((struct xhci_hw_root *)0)->hwr_commands[i];

	sc->sc_cmd_addr = htole64(addr);

	i++;

	if (i == (XHCI_MAX_COMMANDS - 1)) {

		if (j) {
			temp = htole32(XHCI_TRB_3_TC_BIT |
			    XHCI_TRB_3_TYPE_SET(XHCI_TRB_TYPE_LINK) |
			    XHCI_TRB_3_CYCLE_BIT);
		} else {
			temp = htole32(XHCI_TRB_3_TC_BIT |
			    XHCI_TRB_3_TYPE_SET(XHCI_TRB_TYPE_LINK));
		}

		phwr->hwr_commands[i].dwTrb3 = temp;

		usb_pc_cpu_flush(&sc->sc_hw.root_pc);

		i = 0;
		j ^= 1;
	}

	sc->sc_command_idx = i;
	sc->sc_command_ccs = j;

	XWRITE4(sc, door, XHCI_DOORBELL(0), 0);

	err = cv_timedwait(&sc->sc_cmd_cv, &sc->sc_bus.bus_mtx,
	    USB_MS_TO_TICKS(timeout_ms));

	/*
	 * In some error cases event interrupts are not generated.
	 * Poll one time to see if the command has completed.
	 */
	if (err != 0 && xhci_interrupt_poll(sc) != 0) {
		DPRINTF("Command was completed when polling\n");
		err = 0;
	}
	if (err != 0) {
		DPRINTF("Command timeout!\n");
		/*
		 * After some weeks of continuous operation, it has
		 * been observed that the ASMedia Technology, ASM1042
		 * SuperSpeed USB Host Controller can suddenly stop
		 * accepting commands via the command queue. Try to
		 * first reset the command queue. If that fails do a
		 * host controller reset.
		 */
		if (timeout == 0 &&
		    xhci_reset_command_queue_locked(sc) == 0) {
			temp = le32toh(trb->dwTrb3);

			/*
			 * Avoid infinite XHCI reset loops if the set
			 * address command fails to respond due to a
			 * non-enumerating device:
			 */
			if (XHCI_TRB_3_TYPE_GET(temp) == XHCI_TRB_TYPE_ADDRESS_DEVICE &&
			    (temp & XHCI_TRB_3_BSR_BIT) == 0) {
				DPRINTF("Set address timeout\n");
			} else {
				timeout = 1;
				goto retry;
			}
		} else {
			DPRINTF("Controller reset!\n");
			usb_bus_reset_async_locked(&sc->sc_bus);
		}
		err = USB_ERR_TIMEOUT;
		trb->dwTrb2 = 0;
		trb->dwTrb3 = 0;
	} else {
		temp = le32toh(sc->sc_cmd_result[0]);
		if (XHCI_TRB_2_ERROR_GET(temp) != XHCI_TRB_ERROR_SUCCESS)
			err = USB_ERR_IOERROR;

		trb->dwTrb2 = sc->sc_cmd_result[0];
		trb->dwTrb3 = sc->sc_cmd_result[1];
	}

	USB_BUS_UNLOCK(&sc->sc_bus);

	return (err);
}

#if 0
static usb_error_t
xhci_cmd_nop(struct xhci_softc *sc)
{
	struct xhci_trb trb;
	uint32_t temp;

	DPRINTF("\n");

	trb.qwTrb0 = 0;
	trb.dwTrb2 = 0;
	temp = XHCI_TRB_3_TYPE_SET(XHCI_TRB_TYPE_NOOP);

	trb.dwTrb3 = htole32(temp);

	return (xhci_do_command(sc, &trb, 100 /* ms */));
}
#endif

static usb_error_t
xhci_cmd_enable_slot(struct xhci_softc *sc, uint8_t *pslot)
{
	struct xhci_trb trb;
	uint32_t temp;
	usb_error_t err;

	DPRINTF("\n");

	trb.qwTrb0 = 0;
	trb.dwTrb2 = 0;
	trb.dwTrb3 = htole32(XHCI_TRB_3_TYPE_SET(XHCI_TRB_TYPE_ENABLE_SLOT));

	err = xhci_do_command(sc, &trb, 100 /* ms */);
	if (err)
		goto done;

	temp = le32toh(trb.dwTrb3);

	*pslot = XHCI_TRB_3_SLOT_GET(temp); 

done:
	return (err);
}

static usb_error_t
xhci_cmd_disable_slot(struct xhci_softc *sc, uint8_t slot_id)
{
	struct xhci_trb trb;
	uint32_t temp;

	DPRINTF("\n");

	trb.qwTrb0 = 0;
	trb.dwTrb2 = 0;
	temp = XHCI_TRB_3_TYPE_SET(XHCI_TRB_TYPE_DISABLE_SLOT) |
	    XHCI_TRB_3_SLOT_SET(slot_id);

	trb.dwTrb3 = htole32(temp);

	return (xhci_do_command(sc, &trb, 100 /* ms */));
}

static usb_error_t
xhci_cmd_set_address(struct xhci_softc *sc, uint64_t input_ctx,
    uint8_t bsr, uint8_t slot_id)
{
	struct xhci_trb trb;
	uint32_t temp;

	DPRINTF("\n");

	trb.qwTrb0 = htole64(input_ctx);
	trb.dwTrb2 = 0;
	temp = XHCI_TRB_3_TYPE_SET(XHCI_TRB_TYPE_ADDRESS_DEVICE) |
	    XHCI_TRB_3_SLOT_SET(slot_id);

	if (bsr)
		temp |= XHCI_TRB_3_BSR_BIT;

	trb.dwTrb3 = htole32(temp);

	return (xhci_do_command(sc, &trb, 500 /* ms */));
}

static usb_error_t
xhci_set_address(struct usb_device *udev, struct mtx *mtx, uint16_t address)
{
	struct usb_page_search buf_inp;
	struct usb_page_search buf_dev;
	struct xhci_softc *sc = XHCI_BUS2SC(udev->bus);
	struct xhci_hw_dev *hdev;
	struct xhci_dev_ctx *pdev;
	struct xhci_endpoint_ext *pepext;
	uint32_t temp;
	uint16_t mps;
	usb_error_t err;
	uint8_t index;

	/* the root HUB case is not handled here */
	if (udev->parent_hub == NULL)
		return (USB_ERR_INVAL);

	index = udev->controller_slot_id;

	hdev = 	&sc->sc_hw.devs[index];

	if (mtx != NULL)
		mtx_unlock(mtx);

	XHCI_CMD_LOCK(sc);

	switch (hdev->state) {
	case XHCI_ST_DEFAULT:
	case XHCI_ST_ENABLED:

		hdev->state = XHCI_ST_ENABLED;

		/* set configure mask to slot and EP0 */
		xhci_configure_mask(udev, 3, 0);

		/* configure input slot context structure */
		err = xhci_configure_device(udev);

		if (err != 0) {
			DPRINTF("Could not configure device\n");
			break;
		}

		/* configure input endpoint context structure */
		switch (udev->speed) {
		case USB_SPEED_LOW:
		case USB_SPEED_FULL:
			mps = 8;
			break;
		case USB_SPEED_HIGH:
			mps = 64;
			break;
		default:
			mps = 512;
			break;
		}

		pepext = xhci_get_endpoint_ext(udev,
		    &udev->ctrl_ep_desc);

		/* ensure the control endpoint is setup again */
		USB_BUS_LOCK(udev->bus);
		pepext->trb_halted = 1;
		pepext->trb_running = 0;
		USB_BUS_UNLOCK(udev->bus);

		err = xhci_configure_endpoint(udev,
		    &udev->ctrl_ep_desc, pepext,
		    0, 1, 1, 0, mps, mps, USB_EP_MODE_DEFAULT);

		if (err != 0) {
			DPRINTF("Could not configure default endpoint\n");
			break;
		}

		/* execute set address command */
		usbd_get_page(&hdev->input_pc, 0, &buf_inp);

		err = xhci_cmd_set_address(sc, buf_inp.physaddr,
		    (address == 0), index);

		if (err != 0) {
			temp = le32toh(sc->sc_cmd_result[0]);
			if (address == 0 && sc->sc_port_route != NULL &&
			    XHCI_TRB_2_ERROR_GET(temp) ==
			    XHCI_TRB_ERROR_PARAMETER) {
				/* LynxPoint XHCI - ports are not switchable */
				/* Un-route all ports from the XHCI */
				sc->sc_port_route(sc->sc_bus.parent, 0, ~0);
			}
			DPRINTF("Could not set address "
			    "for slot %u.\n", index);
			if (address != 0)
				break;
		}

		/* update device address to new value */

		usbd_get_page(&hdev->device_pc, 0, &buf_dev);
		pdev = buf_dev.buffer;
		usb_pc_cpu_invalidate(&hdev->device_pc);

		temp = xhci_ctx_get_le32(sc, &pdev->ctx_slot.dwSctx3);
		udev->address = XHCI_SCTX_3_DEV_ADDR_GET(temp);

		/* update device state to new value */

		if (address != 0)
			hdev->state = XHCI_ST_ADDRESSED;
		else
			hdev->state = XHCI_ST_DEFAULT;
		break;

	default:
		DPRINTF("Wrong state for set address.\n");
		err = USB_ERR_IOERROR;
		break;
	}
	XHCI_CMD_UNLOCK(sc);

	if (mtx != NULL)
		mtx_lock(mtx);

	return (err);
}

static usb_error_t
xhci_cmd_configure_ep(struct xhci_softc *sc, uint64_t input_ctx,
    uint8_t deconfigure, uint8_t slot_id)
{
	struct xhci_trb trb;
	uint32_t temp;

	DPRINTF("\n");

	trb.qwTrb0 = htole64(input_ctx);
	trb.dwTrb2 = 0;
	temp = XHCI_TRB_3_TYPE_SET(XHCI_TRB_TYPE_CONFIGURE_EP) |
	    XHCI_TRB_3_SLOT_SET(slot_id);

	if (deconfigure)
		temp |= XHCI_TRB_3_DCEP_BIT;

	trb.dwTrb3 = htole32(temp);

	return (xhci_do_command(sc, &trb, 100 /* ms */));
}

static usb_error_t
xhci_cmd_evaluate_ctx(struct xhci_softc *sc, uint64_t input_ctx,
    uint8_t slot_id)
{
	struct xhci_trb trb;
	uint32_t temp;

	DPRINTF("\n");

	trb.qwTrb0 = htole64(input_ctx);
	trb.dwTrb2 = 0;
	temp = XHCI_TRB_3_TYPE_SET(XHCI_TRB_TYPE_EVALUATE_CTX) |
	    XHCI_TRB_3_SLOT_SET(slot_id);
	trb.dwTrb3 = htole32(temp);

	return (xhci_do_command(sc, &trb, 100 /* ms */));
}

static usb_error_t
xhci_cmd_reset_ep(struct xhci_softc *sc, uint8_t preserve,
    uint8_t ep_id, uint8_t slot_id)
{
	struct xhci_trb trb;
	uint32_t temp;

	DPRINTF("\n");

	trb.qwTrb0 = 0;
	trb.dwTrb2 = 0;
	temp = XHCI_TRB_3_TYPE_SET(XHCI_TRB_TYPE_RESET_EP) |
	    XHCI_TRB_3_SLOT_SET(slot_id) |
	    XHCI_TRB_3_EP_SET(ep_id);

	if (preserve)
		temp |= XHCI_TRB_3_PRSV_BIT;

	trb.dwTrb3 = htole32(temp);

	return (xhci_do_command(sc, &trb, 100 /* ms */));
}

static usb_error_t
xhci_cmd_set_tr_dequeue_ptr(struct xhci_softc *sc, uint64_t dequeue_ptr,
    uint16_t stream_id, uint8_t ep_id, uint8_t slot_id)
{
	struct xhci_trb trb;
	uint32_t temp;

	DPRINTF("\n");

	trb.qwTrb0 = htole64(dequeue_ptr);

	temp = XHCI_TRB_2_STREAM_SET(stream_id);
	trb.dwTrb2 = htole32(temp);

	temp = XHCI_TRB_3_TYPE_SET(XHCI_TRB_TYPE_SET_TR_DEQUEUE) |
	    XHCI_TRB_3_SLOT_SET(slot_id) |
	    XHCI_TRB_3_EP_SET(ep_id);
	trb.dwTrb3 = htole32(temp);

	return (xhci_do_command(sc, &trb, 100 /* ms */));
}

static usb_error_t
xhci_cmd_stop_ep(struct xhci_softc *sc, uint8_t suspend,
    uint8_t ep_id, uint8_t slot_id)
{
	struct xhci_trb trb;
	uint32_t temp;

	DPRINTF("\n");

	trb.qwTrb0 = 0;
	trb.dwTrb2 = 0;
	temp = XHCI_TRB_3_TYPE_SET(XHCI_TRB_TYPE_STOP_EP) |
	    XHCI_TRB_3_SLOT_SET(slot_id) |
	    XHCI_TRB_3_EP_SET(ep_id);

	if (suspend)
		temp |= XHCI_TRB_3_SUSP_EP_BIT;

	trb.dwTrb3 = htole32(temp);

	return (xhci_do_command(sc, &trb, 100 /* ms */));
}

static usb_error_t
xhci_cmd_reset_dev(struct xhci_softc *sc, uint8_t slot_id)
{
	struct xhci_trb trb;
	uint32_t temp;

	DPRINTF("\n");

	trb.qwTrb0 = 0;
	trb.dwTrb2 = 0;
	temp = XHCI_TRB_3_TYPE_SET(XHCI_TRB_TYPE_RESET_DEVICE) |
	    XHCI_TRB_3_SLOT_SET(slot_id);

	trb.dwTrb3 = htole32(temp);

	return (xhci_do_command(sc, &trb, 100 /* ms */));
}

/*------------------------------------------------------------------------*
 *	xhci_interrupt - XHCI interrupt handler
 *------------------------------------------------------------------------*/
void
xhci_interrupt(struct xhci_softc *sc)
{
	uint32_t status;
	uint32_t temp;

	USB_BUS_LOCK(&sc->sc_bus);

	status = XREAD4(sc, oper, XHCI_USBSTS);

	/* acknowledge interrupts, if any */
	if (status != 0) {
		XWRITE4(sc, oper, XHCI_USBSTS, status);
		DPRINTFN(16, "real interrupt (status=0x%08x)\n", status);
	}

	temp = XREAD4(sc, runt, XHCI_IMAN(0));

	/* force clearing of pending interrupts */
	if (temp & XHCI_IMAN_INTR_PEND)
		XWRITE4(sc, runt, XHCI_IMAN(0), temp);
 
	/* check for event(s) */
	xhci_interrupt_poll(sc);

	if (status & (XHCI_STS_PCD | XHCI_STS_HCH |
	    XHCI_STS_HSE | XHCI_STS_HCE)) {

		if (status & XHCI_STS_PCD) {
			xhci_root_intr(sc);
		}

		if (status & XHCI_STS_HCH) {
			printf("%s: host controller halted\n",
			    __FUNCTION__);
		}

		if (status & XHCI_STS_HSE) {
			printf("%s: host system error\n",
			    __FUNCTION__);
		}

		if (status & XHCI_STS_HCE) {
			printf("%s: host controller error\n",
			   __FUNCTION__);
		}
	}
	USB_BUS_UNLOCK(&sc->sc_bus);
}

/*------------------------------------------------------------------------*
 *	xhci_timeout - XHCI timeout handler
 *------------------------------------------------------------------------*/
static void
xhci_timeout(void *arg)
{
	struct usb_xfer *xfer = arg;

	DPRINTF("xfer=%p\n", xfer);

	USB_BUS_LOCK_ASSERT(xfer->xroot->bus, MA_OWNED);

	/* transfer is transferred */
	xhci_device_done(xfer, USB_ERR_TIMEOUT);
}

static void
xhci_do_poll(struct usb_bus *bus)
{
	struct xhci_softc *sc = XHCI_BUS2SC(bus);

	USB_BUS_LOCK(&sc->sc_bus);
	xhci_interrupt_poll(sc);
	USB_BUS_UNLOCK(&sc->sc_bus);
}

static void
xhci_setup_generic_chain_sub(struct xhci_std_temp *temp)
{
	struct usb_page_search buf_res;
	struct xhci_td *td;
	struct xhci_td *td_next;
	struct xhci_td *td_alt_next;
	struct xhci_td *td_first;
	uint32_t buf_offset;
	uint32_t average;
	uint32_t len_old;
	uint32_t npkt_off;
	uint32_t dword;
	uint8_t shortpkt_old;
	uint8_t precompute;
	uint8_t x;

	td_alt_next = NULL;
	buf_offset = 0;
	shortpkt_old = temp->shortpkt;
	len_old = temp->len;
	npkt_off = 0;
	precompute = 1;

restart:

	td = temp->td;
	td_next = td_first = temp->td_next;

	while (1) {

		if (temp->len == 0) {

			if (temp->shortpkt)
				break;

			/* send a Zero Length Packet, ZLP, last */

			temp->shortpkt = 1;
			average = 0;

		} else {

			average = temp->average;

			if (temp->len < average) {
				if (temp->len % temp->max_packet_size) {
					temp->shortpkt = 1;
				}
				average = temp->len;
			}
		}

		if (td_next == NULL)
			panic("%s: out of XHCI transfer descriptors!", __FUNCTION__);

		/* get next TD */

		td = td_next;
		td_next = td->obj_next;

		/* check if we are pre-computing */

		if (precompute) {

			/* update remaining length */

			temp->len -= average;

			continue;
		}
		/* fill out current TD */

		td->len = average;
		td->remainder = 0;
		td->status = 0;

		/* update remaining length */

		temp->len -= average;

		/* reset TRB index */

		x = 0;

		if (temp->trb_type == XHCI_TRB_TYPE_SETUP_STAGE) {
			/* immediate data */

			if (average > 8)
				average = 8;

			td->td_trb[0].qwTrb0 = 0;

			usbd_copy_out(temp->pc, temp->offset + buf_offset, 
			   (uint8_t *)(uintptr_t)&td->td_trb[0].qwTrb0,
			   average);

			dword = XHCI_TRB_2_BYTES_SET(8) |
			    XHCI_TRB_2_TDSZ_SET(0) |
			    XHCI_TRB_2_IRQ_SET(0);

			td->td_trb[0].dwTrb2 = htole32(dword);

			dword = XHCI_TRB_3_TYPE_SET(XHCI_TRB_TYPE_SETUP_STAGE) |
			  XHCI_TRB_3_IDT_BIT | XHCI_TRB_3_CYCLE_BIT;

			/* check wLength */
			if (td->td_trb[0].qwTrb0 &
			   htole64(XHCI_TRB_0_WLENGTH_MASK)) {
				if (td->td_trb[0].qwTrb0 &
				    htole64(XHCI_TRB_0_DIR_IN_MASK))
					dword |= XHCI_TRB_3_TRT_IN;
				else
					dword |= XHCI_TRB_3_TRT_OUT;
			}

			td->td_trb[0].dwTrb3 = htole32(dword);
#ifdef USB_DEBUG
			xhci_dump_trb(&td->td_trb[x]);
#endif
			x++;

		} else do {

			uint32_t npkt;

			/* fill out buffer pointers */

			if (average == 0) {
				memset(&buf_res, 0, sizeof(buf_res));
			} else {
				usbd_get_page(temp->pc, temp->offset +
				    buf_offset, &buf_res);

				/* get length to end of page */
				if (buf_res.length > average)
					buf_res.length = average;

				/* check for maximum length */
				if (buf_res.length > XHCI_TD_PAGE_SIZE)
					buf_res.length = XHCI_TD_PAGE_SIZE;

				npkt_off += buf_res.length;
			}

			/* set up npkt */
			npkt = howmany(len_old - npkt_off,
				       temp->max_packet_size);

			if (npkt == 0)
				npkt = 1;
			else if (npkt > 31)
				npkt = 31;

			/* fill out TRB's */
			td->td_trb[x].qwTrb0 =
			    htole64((uint64_t)buf_res.physaddr);

			dword =
			  XHCI_TRB_2_BYTES_SET(buf_res.length) |
			  XHCI_TRB_2_TDSZ_SET(npkt) | 
			  XHCI_TRB_2_IRQ_SET(0);

			td->td_trb[x].dwTrb2 = htole32(dword);

			switch (temp->trb_type) {
			case XHCI_TRB_TYPE_ISOCH:
				dword = XHCI_TRB_3_CHAIN_BIT | XHCI_TRB_3_CYCLE_BIT |
				    XHCI_TRB_3_TBC_SET(temp->tbc) |
				    XHCI_TRB_3_TLBPC_SET(temp->tlbpc);
				if (td != td_first) {
					dword |= XHCI_TRB_3_TYPE_SET(XHCI_TRB_TYPE_NORMAL);
				} else if (temp->do_isoc_sync != 0) {
					temp->do_isoc_sync = 0;
					/* wait until "isoc_frame" */
					dword |= XHCI_TRB_3_TYPE_SET(XHCI_TRB_TYPE_ISOCH) |
					    XHCI_TRB_3_FRID_SET(temp->isoc_frame / 8);
				} else {
					/* start data transfer at next interval */
					dword |= XHCI_TRB_3_TYPE_SET(XHCI_TRB_TYPE_ISOCH) |
					    XHCI_TRB_3_ISO_SIA_BIT;
				}
				if (temp->direction == UE_DIR_IN)
					dword |= XHCI_TRB_3_ISP_BIT;
				break;
			case XHCI_TRB_TYPE_DATA_STAGE:
				dword = XHCI_TRB_3_CHAIN_BIT | XHCI_TRB_3_CYCLE_BIT |
				    XHCI_TRB_3_TYPE_SET(XHCI_TRB_TYPE_DATA_STAGE);
				if (temp->direction == UE_DIR_IN)
					dword |= XHCI_TRB_3_DIR_IN | XHCI_TRB_3_ISP_BIT;
				/*
				 * Section 3.2.9 in the XHCI
				 * specification about control
				 * transfers says that we should use a
				 * normal-TRB if there are more TRBs
				 * extending the data-stage
				 * TRB. Update the "trb_type".
				 */
				temp->trb_type = XHCI_TRB_TYPE_NORMAL;
				break;
			case XHCI_TRB_TYPE_STATUS_STAGE:
				dword = XHCI_TRB_3_CHAIN_BIT | XHCI_TRB_3_CYCLE_BIT |
				    XHCI_TRB_3_TYPE_SET(XHCI_TRB_TYPE_STATUS_STAGE);
				if (temp->direction == UE_DIR_IN)
					dword |= XHCI_TRB_3_DIR_IN;
				break;
			default:	/* XHCI_TRB_TYPE_NORMAL */
				dword = XHCI_TRB_3_CHAIN_BIT | XHCI_TRB_3_CYCLE_BIT |
				    XHCI_TRB_3_TYPE_SET(XHCI_TRB_TYPE_NORMAL);
				if (temp->direction == UE_DIR_IN)
					dword |= XHCI_TRB_3_ISP_BIT;
				break;
			}
			td->td_trb[x].dwTrb3 = htole32(dword);

			average -= buf_res.length;
			buf_offset += buf_res.length;
#ifdef USB_DEBUG
			xhci_dump_trb(&td->td_trb[x]);
#endif
			x++;

		} while (average != 0);

		td->td_trb[x-1].dwTrb3 |= htole32(XHCI_TRB_3_IOC_BIT);

		/* store number of data TRB's */

		td->ntrb = x;

		DPRINTF("NTRB=%u\n", x);

		/* fill out link TRB */

		if (td_next != NULL) {
			/* link the current TD with the next one */
			td->td_trb[x].qwTrb0 = htole64((uint64_t)td_next->td_self);
			DPRINTF("LINK=0x%08llx\n", (long long)td_next->td_self);
		} else {
			/* this field will get updated later */
			DPRINTF("NOLINK\n");
		}

		dword = XHCI_TRB_2_IRQ_SET(0);

		td->td_trb[x].dwTrb2 = htole32(dword);

		dword = XHCI_TRB_3_TYPE_SET(XHCI_TRB_TYPE_LINK) |
		    XHCI_TRB_3_CYCLE_BIT | XHCI_TRB_3_IOC_BIT |
		    /*
		     * CHAIN-BIT: Ensure that a multi-TRB IN-endpoint
		     * frame only receives a single short packet event
		     * by setting the CHAIN bit in the LINK field. In
		     * addition some XHCI controllers have problems
		     * sending a ZLP unless the CHAIN-BIT is set in
		     * the LINK TRB.
		     */
		    XHCI_TRB_3_CHAIN_BIT;

		td->td_trb[x].dwTrb3 = htole32(dword);

		td->alt_next = td_alt_next;
#ifdef USB_DEBUG
		xhci_dump_trb(&td->td_trb[x]);
#endif
		usb_pc_cpu_flush(td->page_cache);
	}

	if (precompute) {
		precompute = 0;

		/* set up alt next pointer, if any */
		if (temp->last_frame) {
			td_alt_next = NULL;
		} else {
			/* we use this field internally */
			td_alt_next = td_next;
		}

		/* restore */
		temp->shortpkt = shortpkt_old;
		temp->len = len_old;
		goto restart;
	}

	/*
	 * Remove cycle bit from the first TRB if we are
	 * stepping them:
	 */
	if (temp->step_td != 0) {
		td_first->td_trb[0].dwTrb3 &= ~htole32(XHCI_TRB_3_CYCLE_BIT);
		usb_pc_cpu_flush(td_first->page_cache);
	}

	/* clear TD SIZE to zero, hence this is the last TRB */
	/* remove chain bit because this is the last data TRB in the chain */
	td->td_trb[td->ntrb - 1].dwTrb2 &= ~htole32(XHCI_TRB_2_TDSZ_SET(15));
	td->td_trb[td->ntrb - 1].dwTrb3 &= ~htole32(XHCI_TRB_3_CHAIN_BIT);
	/* remove CHAIN-BIT from last LINK TRB */
	td->td_trb[td->ntrb].dwTrb3 &= ~htole32(XHCI_TRB_3_CHAIN_BIT);

	usb_pc_cpu_flush(td->page_cache);

	temp->td = td;
	temp->td_next = td_next;
}

static void
xhci_setup_generic_chain(struct usb_xfer *xfer)
{
	struct xhci_std_temp temp;
	struct xhci_td *td;
	uint32_t x;
	uint32_t y;
	uint8_t mult;

	temp.do_isoc_sync = 0;
	temp.step_td = 0;
	temp.tbc = 0;
	temp.tlbpc = 0;
	temp.average = xfer->max_hc_frame_size;
	temp.max_packet_size = xfer->max_packet_size;
	temp.sc = XHCI_BUS2SC(xfer->xroot->bus);
	temp.pc = NULL;
	temp.last_frame = 0;
	temp.offset = 0;
	temp.multishort = xfer->flags_int.isochronous_xfr ||
	    xfer->flags_int.control_xfr ||
	    xfer->flags_int.short_frames_ok;

	/* toggle the DMA set we are using */
	xfer->flags_int.curr_dma_set ^= 1;

	/* get next DMA set */
	td = xfer->td_start[xfer->flags_int.curr_dma_set];

	temp.td = NULL;
	temp.td_next = td;

	xfer->td_transfer_first = td;
	xfer->td_transfer_cache = td;

	if (xfer->flags_int.isochronous_xfr) {
		uint8_t shift;

		/* compute multiplier for ISOCHRONOUS transfers */
		mult = xfer->endpoint->ecomp ?
		    UE_GET_SS_ISO_MULT(xfer->endpoint->ecomp->bmAttributes)
		    : 0;
		/* check for USB 2.0 multiplier */
		if (mult == 0) {
			mult = (xfer->endpoint->edesc->
			    wMaxPacketSize[1] >> 3) & 3;
		}
		/* range check */
		if (mult > 2)
			mult = 3;
		else
			mult++;

		x = XREAD4(temp.sc, runt, XHCI_MFINDEX);

		DPRINTF("MFINDEX=0x%08x\n", x);

		switch (usbd_get_speed(xfer->xroot->udev)) {
		case USB_SPEED_FULL:
			shift = 3;
			temp.isoc_delta = 8;	/* 1ms */
			x += temp.isoc_delta - 1;
			x &= ~(temp.isoc_delta - 1);
			break;
		default:
			shift = usbd_xfer_get_fps_shift(xfer);
			temp.isoc_delta = 1U << shift;
			x += temp.isoc_delta - 1;
			x &= ~(temp.isoc_delta - 1);
			/* simple frame load balancing */
			x += xfer->endpoint->usb_uframe;
			break;
		}

		y = XHCI_MFINDEX_GET(x - xfer->endpoint->isoc_next);

		if ((xfer->endpoint->is_synced == 0) ||
		    (y < (xfer->nframes << shift)) ||
		    (XHCI_MFINDEX_GET(-y) >= (128 * 8))) {
			/*
			 * If there is data underflow or the pipe
			 * queue is empty we schedule the transfer a
			 * few frames ahead of the current frame
			 * position. Else two isochronous transfers
			 * might overlap.
			 */
			xfer->endpoint->isoc_next = XHCI_MFINDEX_GET(x + (3 * 8));
			xfer->endpoint->is_synced = 1;
			temp.do_isoc_sync = 1;

			DPRINTFN(3, "start next=%d\n", xfer->endpoint->isoc_next);
		}

		/* compute isochronous completion time */

		y = XHCI_MFINDEX_GET(xfer->endpoint->isoc_next - (x & ~7));

		xfer->isoc_time_complete =
		    usb_isoc_time_expand(&temp.sc->sc_bus, x / 8) +
		    (y / 8) + (((xfer->nframes << shift) + 7) / 8);

		x = 0;
		temp.isoc_frame = xfer->endpoint->isoc_next;
		temp.trb_type = XHCI_TRB_TYPE_ISOCH;

		xfer->endpoint->isoc_next += xfer->nframes << shift;

	} else if (xfer->flags_int.control_xfr) {

		/* check if we should prepend a setup message */

		if (xfer->flags_int.control_hdr) {

			temp.len = xfer->frlengths[0];
			temp.pc = xfer->frbuffers + 0;
			temp.shortpkt = temp.len ? 1 : 0;
			temp.trb_type = XHCI_TRB_TYPE_SETUP_STAGE;
			temp.direction = 0;

			/* check for last frame */
			if (xfer->nframes == 1) {
				/* no STATUS stage yet, SETUP is last */
				if (xfer->flags_int.control_act)
					temp.last_frame = 1;
			}

			xhci_setup_generic_chain_sub(&temp);
		}
		x = 1;
		mult = 1;
		temp.isoc_delta = 0;
		temp.isoc_frame = 0;
		temp.trb_type = xfer->flags_int.control_did_data ?
		    XHCI_TRB_TYPE_NORMAL : XHCI_TRB_TYPE_DATA_STAGE;
	} else {
		x = 0;
		mult = 1;
		temp.isoc_delta = 0;
		temp.isoc_frame = 0;
		temp.trb_type = XHCI_TRB_TYPE_NORMAL;
	}

	if (x != xfer->nframes) {
                /* set up page_cache pointer */
                temp.pc = xfer->frbuffers + x;
		/* set endpoint direction */
		temp.direction = UE_GET_DIR(xfer->endpointno);
	}

	while (x != xfer->nframes) {

		/* DATA0 / DATA1 message */

		temp.len = xfer->frlengths[x];
		temp.step_td = ((xfer->endpointno & UE_DIR_IN) &&
		    x != 0 && temp.multishort == 0);

		x++;

		if (x == xfer->nframes) {
			if (xfer->flags_int.control_xfr) {
				/* no STATUS stage yet, DATA is last */
				if (xfer->flags_int.control_act)
					temp.last_frame = 1;
			} else {
				temp.last_frame = 1;
			}
		}
		if (temp.len == 0) {

			/* make sure that we send an USB packet */

			temp.shortpkt = 0;

			temp.tbc = 0;
			temp.tlbpc = mult - 1;

		} else if (xfer->flags_int.isochronous_xfr) {

			uint8_t tdpc;

			/*
			 * Isochronous transfers don't have short
			 * packet termination:
			 */

			temp.shortpkt = 1;

			/* isochronous transfers have a transfer limit */

			if (temp.len > xfer->max_frame_size)
				temp.len = xfer->max_frame_size;

			/* compute TD packet count */
			tdpc = howmany(temp.len, xfer->max_packet_size);

			temp.tbc = howmany(tdpc, mult) - 1;
			temp.tlbpc = (tdpc % mult);

			if (temp.tlbpc == 0)
				temp.tlbpc = mult - 1;
			else
				temp.tlbpc--;
		} else {

			/* regular data transfer */

			temp.shortpkt = xfer->flags.force_short_xfer ? 0 : 1;
		}

		xhci_setup_generic_chain_sub(&temp);

		if (xfer->flags_int.isochronous_xfr) {
			temp.offset += xfer->frlengths[x - 1];
			temp.isoc_frame += temp.isoc_delta;
		} else {
			/* get next Page Cache pointer */
			temp.pc = xfer->frbuffers + x;
		}
	}

	/* check if we should append a status stage */

	if (xfer->flags_int.control_xfr &&
	    !xfer->flags_int.control_act) {

		/*
		 * Send a DATA1 message and invert the current
		 * endpoint direction.
		 */
		if (xhcictlstep || temp.sc->sc_ctlstep) {
			/*
			 * Some XHCI controllers will not delay the
			 * status stage until the next SOF. Force this
			 * behaviour to avoid failed control
			 * transfers.
			 */
			temp.step_td = (xfer->nframes != 0);
		} else {
			temp.step_td = 0;
		}
		temp.direction = UE_GET_DIR(xfer->endpointno) ^ UE_DIR_IN;
		temp.len = 0;
		temp.pc = NULL;
		temp.shortpkt = 0;
		temp.last_frame = 1;
		temp.trb_type = XHCI_TRB_TYPE_STATUS_STAGE;

		xhci_setup_generic_chain_sub(&temp);
	}

	td = temp.td;

	/* must have at least one frame! */

	xfer->td_transfer_last = td;

	DPRINTF("first=%p last=%p\n", xfer->td_transfer_first, td);
}

static void
xhci_set_slot_pointer(struct xhci_softc *sc, uint8_t index, uint64_t dev_addr)
{
	struct usb_page_search buf_res;
	struct xhci_dev_ctx_addr *pdctxa;

	usbd_get_page(&sc->sc_hw.ctx_pc, 0, &buf_res);

	pdctxa = buf_res.buffer;

	DPRINTF("addr[%u]=0x%016llx\n", index, (long long)dev_addr);

	pdctxa->qwBaaDevCtxAddr[index] = htole64(dev_addr);

	usb_pc_cpu_flush(&sc->sc_hw.ctx_pc);
}

static usb_error_t
xhci_configure_mask(struct usb_device *udev, uint32_t mask, uint8_t drop)
{
	struct xhci_softc *sc = XHCI_BUS2SC(udev->bus);
	struct usb_page_search buf_inp;
	struct xhci_input_dev_ctx *pinp;
	uint32_t temp;
	uint8_t index;
	uint8_t x;

	index = udev->controller_slot_id;

	usbd_get_page(&sc->sc_hw.devs[index].input_pc, 0, &buf_inp);

	pinp = buf_inp.buffer;

	if (drop) {
		mask &= XHCI_INCTX_NON_CTRL_MASK;
		xhci_ctx_set_le32(sc, &pinp->ctx_input.dwInCtx0, mask);
		xhci_ctx_set_le32(sc, &pinp->ctx_input.dwInCtx1, 0);
	} else {
		/*
		 * Some hardware requires that we drop the endpoint
		 * context before adding it again:
		 */
		xhci_ctx_set_le32(sc, &pinp->ctx_input.dwInCtx0,
		    mask & XHCI_INCTX_NON_CTRL_MASK);

		/* Add new endpoint context */
		xhci_ctx_set_le32(sc, &pinp->ctx_input.dwInCtx1, mask);

		/* find most significant set bit */
		for (x = 31; x != 1; x--) {
			if (mask & (1 << x))
				break;
		}

		/* adjust */
		x--;

		/* figure out the maximum number of contexts */
		if (x > sc->sc_hw.devs[index].context_num)
			sc->sc_hw.devs[index].context_num = x;
		else
			x = sc->sc_hw.devs[index].context_num;

		/* update number of contexts */
		temp = xhci_ctx_get_le32(sc, &pinp->ctx_slot.dwSctx0);
		temp &= ~XHCI_SCTX_0_CTX_NUM_SET(31);
		temp |= XHCI_SCTX_0_CTX_NUM_SET(x + 1);
		xhci_ctx_set_le32(sc, &pinp->ctx_slot.dwSctx0, temp);
	}
	usb_pc_cpu_flush(&sc->sc_hw.devs[index].input_pc);
	return (0);
}

static usb_error_t
xhci_configure_endpoint(struct usb_device *udev,
    struct usb_endpoint_descriptor *edesc, struct xhci_endpoint_ext *pepext,
    uint16_t interval, uint8_t max_packet_count,
    uint8_t mult, uint8_t fps_shift, uint16_t max_packet_size,
    uint16_t max_frame_size, uint8_t ep_mode)
{
	struct usb_page_search buf_inp;
	struct xhci_softc *sc = XHCI_BUS2SC(udev->bus);
	struct xhci_input_dev_ctx *pinp;
	uint64_t ring_addr = pepext->physaddr;
	uint32_t temp;
	uint8_t index;
	uint8_t epno;
	uint8_t type;

	index = udev->controller_slot_id;

	usbd_get_page(&sc->sc_hw.devs[index].input_pc, 0, &buf_inp);

	pinp = buf_inp.buffer;

	epno = edesc->bEndpointAddress;
	type = edesc->bmAttributes & UE_XFERTYPE;

	if (type == UE_CONTROL)
		epno |= UE_DIR_IN;

	epno = XHCI_EPNO2EPID(epno);

 	if (epno == 0)
		return (USB_ERR_NO_PIPE);		/* invalid */

	if (max_packet_count == 0)
		return (USB_ERR_BAD_BUFSIZE);

	max_packet_count--;

	if (mult == 0)
		return (USB_ERR_BAD_BUFSIZE);

	/* store endpoint mode */
	pepext->trb_ep_mode = ep_mode;
	/* store bMaxPacketSize for control endpoints */
	pepext->trb_ep_maxp = edesc->wMaxPacketSize[0];
	usb_pc_cpu_flush(pepext->page_cache);

	if (ep_mode == USB_EP_MODE_STREAMS) {
		temp = XHCI_EPCTX_0_EPSTATE_SET(0) |
		    XHCI_EPCTX_0_MAXP_STREAMS_SET(XHCI_MAX_STREAMS_LOG - 1) |
		    XHCI_EPCTX_0_LSA_SET(1);

		ring_addr += sizeof(struct xhci_trb) *
		    XHCI_MAX_TRANSFERS * XHCI_MAX_STREAMS;
	} else {
		temp = XHCI_EPCTX_0_EPSTATE_SET(0) |
		    XHCI_EPCTX_0_MAXP_STREAMS_SET(0) |
		    XHCI_EPCTX_0_LSA_SET(0);

		ring_addr |= XHCI_EPCTX_2_DCS_SET(1);
	}

	switch (udev->speed) {
	case USB_SPEED_FULL:
	case USB_SPEED_LOW:
		/* 1ms -> 125us */
		fps_shift += 3;
		break;
	default:
		break;
	}

	switch (type) {
	case UE_INTERRUPT:
		if (fps_shift > 3)
			fps_shift--;
		temp |= XHCI_EPCTX_0_IVAL_SET(fps_shift);
		break;
	case UE_ISOCHRONOUS:
		temp |= XHCI_EPCTX_0_IVAL_SET(fps_shift);

		switch (udev->speed) {
		case USB_SPEED_SUPER:
			if (mult > 3)
				mult = 3;
			temp |= XHCI_EPCTX_0_MULT_SET(mult - 1);
			max_packet_count /= mult;
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}

	xhci_ctx_set_le32(sc, &pinp->ctx_ep[epno - 1].dwEpCtx0, temp);

	temp =
	    XHCI_EPCTX_1_HID_SET(0) |
	    XHCI_EPCTX_1_MAXB_SET(max_packet_count) |
	    XHCI_EPCTX_1_MAXP_SIZE_SET(max_packet_size);

	/*
	 * Always enable the "three strikes and you are gone" feature
	 * except for ISOCHRONOUS endpoints. This is suggested by
	 * section 4.3.3 in the XHCI specification about device slot
	 * initialisation.
	 */
	if (type != UE_ISOCHRONOUS)
		temp |= XHCI_EPCTX_1_CERR_SET(3);

	switch (type) {
	case UE_CONTROL:
		temp |= XHCI_EPCTX_1_EPTYPE_SET(4);
		break;
	case UE_ISOCHRONOUS:
		temp |= XHCI_EPCTX_1_EPTYPE_SET(1);
		break;
	case UE_BULK:
		temp |= XHCI_EPCTX_1_EPTYPE_SET(2);
		break;
	default:
		temp |= XHCI_EPCTX_1_EPTYPE_SET(3);
		break;
	}

	/* check for IN direction */
	if (epno & 1)
		temp |= XHCI_EPCTX_1_EPTYPE_SET(4);

	xhci_ctx_set_le32(sc, &pinp->ctx_ep[epno - 1].dwEpCtx1, temp);
	xhci_ctx_set_le64(sc, &pinp->ctx_ep[epno - 1].qwEpCtx2, ring_addr);

	switch (edesc->bmAttributes & UE_XFERTYPE) {
	case UE_INTERRUPT:
	case UE_ISOCHRONOUS:
		temp = XHCI_EPCTX_4_MAX_ESIT_PAYLOAD_SET(max_frame_size) |
		    XHCI_EPCTX_4_AVG_TRB_LEN_SET(MIN(XHCI_PAGE_SIZE,
		    max_frame_size));
		break;
	case UE_CONTROL:
		temp = XHCI_EPCTX_4_AVG_TRB_LEN_SET(8);
		break;
	default:
		temp = XHCI_EPCTX_4_AVG_TRB_LEN_SET(XHCI_PAGE_SIZE);
		break;
	}

	xhci_ctx_set_le32(sc, &pinp->ctx_ep[epno - 1].dwEpCtx4, temp);

#ifdef USB_DEBUG
	xhci_dump_endpoint(sc, &pinp->ctx_ep[epno - 1]);
#endif
	usb_pc_cpu_flush(&sc->sc_hw.devs[index].input_pc);

	return (0);		/* success */
}

static usb_error_t
xhci_configure_endpoint_by_xfer(struct usb_xfer *xfer)
{
	struct xhci_endpoint_ext *pepext;
	struct usb_endpoint_ss_comp_descriptor *ecomp;
	usb_stream_t x;

	pepext = xhci_get_endpoint_ext(xfer->xroot->udev,
	    xfer->endpoint->edesc);

	ecomp = xfer->endpoint->ecomp;

	for (x = 0; x != XHCI_MAX_STREAMS; x++) {
		uint64_t temp;

		/* halt any transfers */
		pepext->trb[x * XHCI_MAX_TRANSFERS].dwTrb3 = 0;

		/* compute start of TRB ring for stream "x" */
		temp = pepext->physaddr +
		    (x * XHCI_MAX_TRANSFERS * sizeof(struct xhci_trb)) +
		    XHCI_SCTX_0_SCT_SEC_TR_RING;

		/* make tree structure */
		pepext->trb[(XHCI_MAX_TRANSFERS *
		    XHCI_MAX_STREAMS) + x].qwTrb0 = htole64(temp);

		/* reserved fields */
		pepext->trb[(XHCI_MAX_TRANSFERS *
                    XHCI_MAX_STREAMS) + x].dwTrb2 = 0;
		pepext->trb[(XHCI_MAX_TRANSFERS *
		    XHCI_MAX_STREAMS) + x].dwTrb3 = 0;
	}
	usb_pc_cpu_flush(pepext->page_cache);

	return (xhci_configure_endpoint(xfer->xroot->udev,
	    xfer->endpoint->edesc, pepext,
	    xfer->interval, xfer->max_packet_count,
	    (ecomp != NULL) ? UE_GET_SS_ISO_MULT(ecomp->bmAttributes) + 1 : 1,
	    usbd_xfer_get_fps_shift(xfer), xfer->max_packet_size,
	    xfer->max_frame_size, xfer->endpoint->ep_mode));
}

static usb_error_t
xhci_configure_device(struct usb_device *udev)
{
	struct xhci_softc *sc = XHCI_BUS2SC(udev->bus);
	struct usb_page_search buf_inp;
	struct usb_page_cache *pcinp;
	struct xhci_input_dev_ctx *pinp;
	struct usb_device *hubdev;
	uint32_t temp;
	uint32_t route;
	uint32_t rh_port;
	uint8_t is_hub;
	uint8_t index;
	uint8_t depth;

	index = udev->controller_slot_id;

	DPRINTF("index=%u\n", index);

	pcinp = &sc->sc_hw.devs[index].input_pc;

	usbd_get_page(pcinp, 0, &buf_inp);

	pinp = buf_inp.buffer;

	rh_port = 0;
	route = 0;

	/* figure out route string and root HUB port number */

	for (hubdev = udev; hubdev != NULL; hubdev = hubdev->parent_hub) {

		if (hubdev->parent_hub == NULL)
			break;

		depth = hubdev->parent_hub->depth;

		/*
		 * NOTE: HS/FS/LS devices and the SS root HUB can have
		 * more than 15 ports
		 */

		rh_port = hubdev->port_no;

		if (depth == 0)
			break;

		if (rh_port > 15)
			rh_port = 15;

		if (depth < 6)
			route |= rh_port << (4 * (depth - 1));
	}

	DPRINTF("Route=0x%08x\n", route);

	temp = XHCI_SCTX_0_ROUTE_SET(route) |
	    XHCI_SCTX_0_CTX_NUM_SET(
	    sc->sc_hw.devs[index].context_num + 1);

	switch (udev->speed) {
	case USB_SPEED_LOW:
		temp |= XHCI_SCTX_0_SPEED_SET(2);
		if (udev->parent_hs_hub != NULL &&
		    udev->parent_hs_hub->ddesc.bDeviceProtocol ==
		    UDPROTO_HSHUBMTT) {
			DPRINTF("Device inherits MTT\n");
			temp |= XHCI_SCTX_0_MTT_SET(1);
		}
		break;
	case USB_SPEED_HIGH:
		temp |= XHCI_SCTX_0_SPEED_SET(3);
		if (sc->sc_hw.devs[index].nports != 0 &&
		    udev->ddesc.bDeviceProtocol == UDPROTO_HSHUBMTT) {
			DPRINTF("HUB supports MTT\n");
			temp |= XHCI_SCTX_0_MTT_SET(1);
		}
		break;
	case USB_SPEED_FULL:
		temp |= XHCI_SCTX_0_SPEED_SET(1);
		if (udev->parent_hs_hub != NULL &&
		    udev->parent_hs_hub->ddesc.bDeviceProtocol ==
		    UDPROTO_HSHUBMTT) {
			DPRINTF("Device inherits MTT\n");
			temp |= XHCI_SCTX_0_MTT_SET(1);
		}
		break;
	default:
		temp |= XHCI_SCTX_0_SPEED_SET(4);
		break;
	}

	is_hub = sc->sc_hw.devs[index].nports != 0 &&
	    (udev->speed == USB_SPEED_SUPER ||
	    udev->speed == USB_SPEED_HIGH);

	if (is_hub)
		temp |= XHCI_SCTX_0_HUB_SET(1);

	xhci_ctx_set_le32(sc, &pinp->ctx_slot.dwSctx0, temp);

	temp = XHCI_SCTX_1_RH_PORT_SET(rh_port);

	if (is_hub) {
		temp |= XHCI_SCTX_1_NUM_PORTS_SET(
		    sc->sc_hw.devs[index].nports);
	}

	switch (udev->speed) {
	case USB_SPEED_SUPER:
		switch (sc->sc_hw.devs[index].state) {
		case XHCI_ST_ADDRESSED:
		case XHCI_ST_CONFIGURED:
			/* enable power save */
			temp |= XHCI_SCTX_1_MAX_EL_SET(sc->sc_exit_lat_max);
			break;
		default:
			/* disable power save */
			break;
		}
		break;
	default:
		break;
	}

	xhci_ctx_set_le32(sc, &pinp->ctx_slot.dwSctx1, temp);

	temp = XHCI_SCTX_2_IRQ_TARGET_SET(0);

	if (is_hub) {
		temp |= XHCI_SCTX_2_TT_THINK_TIME_SET(
		    sc->sc_hw.devs[index].tt);
	}

	hubdev = udev->parent_hs_hub;

	/* check if we should activate the transaction translator */
	switch (udev->speed) {
	case USB_SPEED_FULL:
	case USB_SPEED_LOW:
		if (hubdev != NULL) {
			temp |= XHCI_SCTX_2_TT_HUB_SID_SET(
			    hubdev->controller_slot_id);
			temp |= XHCI_SCTX_2_TT_PORT_NUM_SET(
			    udev->hs_port_no);
		}
		break;
	default:
		break;
	}

	xhci_ctx_set_le32(sc, &pinp->ctx_slot.dwSctx2, temp);

	/*
	 * These fields should be initialized to zero, according to
	 * XHCI section 6.2.2 - slot context:
	 */
	temp = XHCI_SCTX_3_DEV_ADDR_SET(0) |
	    XHCI_SCTX_3_SLOT_STATE_SET(0);

	xhci_ctx_set_le32(sc, &pinp->ctx_slot.dwSctx3, temp);

#ifdef USB_DEBUG
	xhci_dump_device(sc, &pinp->ctx_slot);
#endif
	usb_pc_cpu_flush(pcinp);

	return (0);		/* success */
}

static usb_error_t
xhci_alloc_device_ext(struct usb_device *udev)
{
	struct xhci_softc *sc = XHCI_BUS2SC(udev->bus);
	struct usb_page_search buf_dev;
	struct usb_page_search buf_ep;
	struct xhci_trb *trb;
	struct usb_page_cache *pc;
	struct usb_page *pg;
	uint64_t addr;
	uint8_t index;
	uint8_t i;

	index = udev->controller_slot_id;

	pc = &sc->sc_hw.devs[index].device_pc;
	pg = &sc->sc_hw.devs[index].device_pg;

	/* need to initialize the page cache */
	pc->tag_parent = sc->sc_bus.dma_parent_tag;

	if (usb_pc_alloc_mem(pc, pg, sc->sc_ctx_is_64_byte ?
	    (2 * sizeof(struct xhci_dev_ctx)) :
	    sizeof(struct xhci_dev_ctx), XHCI_PAGE_SIZE))
		goto error;

	usbd_get_page(pc, 0, &buf_dev);

	pc = &sc->sc_hw.devs[index].input_pc;
	pg = &sc->sc_hw.devs[index].input_pg;

	/* need to initialize the page cache */
	pc->tag_parent = sc->sc_bus.dma_parent_tag;

	if (usb_pc_alloc_mem(pc, pg, sc->sc_ctx_is_64_byte ?
	    (2 * sizeof(struct xhci_input_dev_ctx)) :
	    sizeof(struct xhci_input_dev_ctx), XHCI_PAGE_SIZE)) {
		goto error;
	}

	/* initialize all endpoint LINK TRBs */

	for (i = 0; i != XHCI_MAX_ENDPOINTS; i++) {

		pc = &sc->sc_hw.devs[index].endpoint_pc[i];
		pg = &sc->sc_hw.devs[index].endpoint_pg[i];

		/* need to initialize the page cache */
		pc->tag_parent = sc->sc_bus.dma_parent_tag;

		if (usb_pc_alloc_mem(pc, pg,
		    sizeof(struct xhci_dev_endpoint_trbs), XHCI_TRB_ALIGN)) {
			goto error;
		}

		/* lookup endpoint TRB ring */
		usbd_get_page(pc, 0, &buf_ep);

		/* get TRB pointer */
		trb = buf_ep.buffer;
		trb += XHCI_MAX_TRANSFERS - 1;

		/* get TRB start address */
		addr = buf_ep.physaddr;

		/* create LINK TRB */
		trb->qwTrb0 = htole64(addr);
		trb->dwTrb2 = htole32(XHCI_TRB_2_IRQ_SET(0));
		trb->dwTrb3 = htole32(XHCI_TRB_3_CYCLE_BIT |
		    XHCI_TRB_3_TYPE_SET(XHCI_TRB_TYPE_LINK));

		usb_pc_cpu_flush(pc);
	}

	xhci_set_slot_pointer(sc, index, buf_dev.physaddr);

	return (0);

error:
	xhci_free_device_ext(udev);

	return (USB_ERR_NOMEM);
}

static void
xhci_free_device_ext(struct usb_device *udev)
{
	struct xhci_softc *sc = XHCI_BUS2SC(udev->bus);
	uint8_t index;
	uint8_t i;

	index = udev->controller_slot_id;
	xhci_set_slot_pointer(sc, index, 0);

	usb_pc_free_mem(&sc->sc_hw.devs[index].device_pc);
	usb_pc_free_mem(&sc->sc_hw.devs[index].input_pc);
	for (i = 0; i != XHCI_MAX_ENDPOINTS; i++)
		usb_pc_free_mem(&sc->sc_hw.devs[index].endpoint_pc[i]);
}

static struct xhci_endpoint_ext *
xhci_get_endpoint_ext(struct usb_device *udev, struct usb_endpoint_descriptor *edesc)
{
	struct xhci_softc *sc = XHCI_BUS2SC(udev->bus);
	struct xhci_endpoint_ext *pepext;
	struct usb_page_cache *pc;
	struct usb_page_search buf_ep;
	uint8_t epno;
	uint8_t index;

	epno = edesc->bEndpointAddress;
	if ((edesc->bmAttributes & UE_XFERTYPE) == UE_CONTROL)
		epno |= UE_DIR_IN;

	epno = XHCI_EPNO2EPID(epno);

	index = udev->controller_slot_id;

	pc = &sc->sc_hw.devs[index].endpoint_pc[epno];

	usbd_get_page(pc, 0, &buf_ep);

	pepext = &sc->sc_hw.devs[index].endp[epno];
	pepext->page_cache = pc;
	pepext->trb = buf_ep.buffer;
	pepext->physaddr = buf_ep.physaddr;

	return (pepext);
}

static void
xhci_endpoint_doorbell(struct usb_xfer *xfer)
{
	struct xhci_softc *sc = XHCI_BUS2SC(xfer->xroot->bus);
	uint8_t epno;
	uint8_t index;

	epno = xfer->endpointno;
	if (xfer->flags_int.control_xfr)
		epno |= UE_DIR_IN;

	epno = XHCI_EPNO2EPID(epno);
	index = xfer->xroot->udev->controller_slot_id;

	if (xfer->xroot->udev->flags.self_suspended == 0) {
		XWRITE4(sc, door, XHCI_DOORBELL(index),
		    epno | XHCI_DB_SID_SET(xfer->stream_id));
	}
}

static void
xhci_transfer_remove(struct usb_xfer *xfer, usb_error_t error)
{
	struct xhci_endpoint_ext *pepext;

	if (xfer->flags_int.bandwidth_reclaimed) {
		xfer->flags_int.bandwidth_reclaimed = 0;

		pepext = xhci_get_endpoint_ext(xfer->xroot->udev,
		    xfer->endpoint->edesc);

		pepext->trb_used[xfer->stream_id]--;

		pepext->xfer[xfer->qh_pos] = NULL;

		if (error && pepext->trb_running != 0) {
			pepext->trb_halted = 1;
			pepext->trb_running = 0;
		}
	}
}

static usb_error_t
xhci_transfer_insert(struct usb_xfer *xfer)
{
	struct xhci_td *td_first;
	struct xhci_td *td_last;
	struct xhci_trb *trb_link;
	struct xhci_endpoint_ext *pepext;
	uint64_t addr;
	usb_stream_t id;
	uint8_t i;
	uint8_t inext;
	uint8_t trb_limit;

	DPRINTFN(8, "\n");

	id = xfer->stream_id;

	/* check if already inserted */
	if (xfer->flags_int.bandwidth_reclaimed) {
		DPRINTFN(8, "Already in schedule\n");
		return (0);
	}

	pepext = xhci_get_endpoint_ext(xfer->xroot->udev,
	    xfer->endpoint->edesc);

	td_first = xfer->td_transfer_first;
	td_last = xfer->td_transfer_last;
	addr = pepext->physaddr;

	switch (xfer->endpoint->edesc->bmAttributes & UE_XFERTYPE) {
	case UE_CONTROL:
	case UE_INTERRUPT:
		/* single buffered */
		trb_limit = 1;
		break;
	default:
		/* multi buffered */
		trb_limit = (XHCI_MAX_TRANSFERS - 2);
		break;
	}

	if (pepext->trb_used[id] >= trb_limit) {
		DPRINTFN(8, "Too many TDs queued.\n");
		return (USB_ERR_NOMEM);
	}

	/* check if bMaxPacketSize changed */
	if (xfer->flags_int.control_xfr != 0 &&
	    pepext->trb_ep_maxp != xfer->endpoint->edesc->wMaxPacketSize[0]) {

		DPRINTFN(8, "Reconfigure control endpoint\n");

		/* force driver to reconfigure endpoint */
		pepext->trb_halted = 1;
		pepext->trb_running = 0;
	}

	/* check for stopped condition, after putting transfer on interrupt queue */
	if (pepext->trb_running == 0) {
		struct xhci_softc *sc = XHCI_BUS2SC(xfer->xroot->bus);

		DPRINTFN(8, "Not running\n");

		/* start configuration */
		(void)usb_proc_msignal(USB_BUS_CONTROL_XFER_PROC(&sc->sc_bus),
		    &sc->sc_config_msg[0], &sc->sc_config_msg[1]);
		return (0);
	}

	pepext->trb_used[id]++;

	/* get current TRB index */
	i = pepext->trb_index[id];

	/* get next TRB index */
	inext = (i + 1);

	/* the last entry of the ring is a hardcoded link TRB */
	if (inext >= (XHCI_MAX_TRANSFERS - 1))
		inext = 0;

	/* store next TRB index, before stream ID offset is added */
	pepext->trb_index[id] = inext;

	/* offset for stream */
	i += id * XHCI_MAX_TRANSFERS;
	inext += id * XHCI_MAX_TRANSFERS;

	/* compute terminating return address */
	addr += (inext * sizeof(struct xhci_trb));

	/* compute link TRB pointer */
	trb_link = td_last->td_trb + td_last->ntrb;

	/* update next pointer of last link TRB */
	trb_link->qwTrb0 = htole64(addr);
	trb_link->dwTrb2 = htole32(XHCI_TRB_2_IRQ_SET(0));
	trb_link->dwTrb3 = htole32(XHCI_TRB_3_IOC_BIT |
	    XHCI_TRB_3_CYCLE_BIT |
	    XHCI_TRB_3_TYPE_SET(XHCI_TRB_TYPE_LINK));

#ifdef USB_DEBUG
	xhci_dump_trb(&td_last->td_trb[td_last->ntrb]);
#endif
	usb_pc_cpu_flush(td_last->page_cache);

	/* write ahead chain end marker */

	pepext->trb[inext].qwTrb0 = 0;
	pepext->trb[inext].dwTrb2 = 0;
	pepext->trb[inext].dwTrb3 = 0;

	/* update next pointer of link TRB */

	pepext->trb[i].qwTrb0 = htole64((uint64_t)td_first->td_self);
	pepext->trb[i].dwTrb2 = htole32(XHCI_TRB_2_IRQ_SET(0));

#ifdef USB_DEBUG
	xhci_dump_trb(&pepext->trb[i]);
#endif
	usb_pc_cpu_flush(pepext->page_cache);

	/* toggle cycle bit which activates the transfer chain */

	pepext->trb[i].dwTrb3 = htole32(XHCI_TRB_3_CYCLE_BIT |
	    XHCI_TRB_3_TYPE_SET(XHCI_TRB_TYPE_LINK));

	usb_pc_cpu_flush(pepext->page_cache);

	DPRINTF("qh_pos = %u\n", i);

	pepext->xfer[i] = xfer;

	xfer->qh_pos = i;

	xfer->flags_int.bandwidth_reclaimed = 1;

	xhci_endpoint_doorbell(xfer);

	return (0);
}

static void
xhci_root_intr(struct xhci_softc *sc)
{
	uint16_t i;

	USB_BUS_LOCK_ASSERT(&sc->sc_bus, MA_OWNED);

	/* clear any old interrupt data */
	memset(sc->sc_hub_idata, 0, sizeof(sc->sc_hub_idata));

	for (i = 1; i <= sc->sc_noport; i++) {
		/* pick out CHANGE bits from the status register */
		if (XREAD4(sc, oper, XHCI_PORTSC(i)) & (
		    XHCI_PS_CSC | XHCI_PS_PEC |
		    XHCI_PS_OCC | XHCI_PS_WRC |
		    XHCI_PS_PRC | XHCI_PS_PLC |
		    XHCI_PS_CEC)) {
			sc->sc_hub_idata[i / 8] |= 1 << (i % 8);
			DPRINTF("port %d changed\n", i);
		}
	}
	uhub_root_intr(&sc->sc_bus, sc->sc_hub_idata,
	    sizeof(sc->sc_hub_idata));
}

/*------------------------------------------------------------------------*
 *	xhci_device_done - XHCI done handler
 *
 * NOTE: This function can be called two times in a row on
 * the same USB transfer. From close and from interrupt.
 *------------------------------------------------------------------------*/
static void
xhci_device_done(struct usb_xfer *xfer, usb_error_t error)
{
	DPRINTFN(2, "xfer=%p, endpoint=%p, error=%d\n",
	    xfer, xfer->endpoint, error);

	/* remove transfer from HW queue */
	xhci_transfer_remove(xfer, error);

	/* dequeue transfer and start next transfer */
	usbd_transfer_done(xfer, error);
}

/*------------------------------------------------------------------------*
 * XHCI data transfer support (generic type)
 *------------------------------------------------------------------------*/
static void
xhci_device_generic_open(struct usb_xfer *xfer)
{
	if (xfer->flags_int.isochronous_xfr) {
		switch (xfer->xroot->udev->speed) {
		case USB_SPEED_FULL:
			break;
		default:
			usb_hs_bandwidth_alloc(xfer);
			break;
		}
	}
}

static void
xhci_device_generic_close(struct usb_xfer *xfer)
{
	DPRINTF("\n");

	xhci_device_done(xfer, USB_ERR_CANCELLED);

	if (xfer->flags_int.isochronous_xfr) {
		switch (xfer->xroot->udev->speed) {
		case USB_SPEED_FULL:
			break;
		default:
			usb_hs_bandwidth_free(xfer);
			break;
		}
	}
}

static void
xhci_device_generic_multi_enter(struct usb_endpoint *ep,
    usb_stream_t stream_id, struct usb_xfer *enter_xfer)
{
	struct usb_xfer *xfer;

	/* check if there is a current transfer */
	xfer = ep->endpoint_q[stream_id].curr;
	if (xfer == NULL)
		return;

	/*
	 * Check if the current transfer is started and then pickup
	 * the next one, if any. Else wait for next start event due to
	 * block on failure feature.
	 */
	if (!xfer->flags_int.bandwidth_reclaimed)
		return;

	xfer = TAILQ_FIRST(&ep->endpoint_q[stream_id].head);
	if (xfer == NULL) {
		/*
		 * In case of enter we have to consider that the
		 * transfer is queued by the USB core after the enter
		 * method is called.
		 */
		xfer = enter_xfer;

		if (xfer == NULL)
			return;
	}

	/* try to multi buffer */
	xhci_transfer_insert(xfer);
}

static void
xhci_device_generic_enter(struct usb_xfer *xfer)
{
	DPRINTF("\n");

	/* set up TD's and QH */
	xhci_setup_generic_chain(xfer);

	xhci_device_generic_multi_enter(xfer->endpoint,
	    xfer->stream_id, xfer);
}

static void
xhci_device_generic_start(struct usb_xfer *xfer)
{
	DPRINTF("\n");

	/* try to insert xfer on HW queue */
	xhci_transfer_insert(xfer);

	/* try to multi buffer */
	xhci_device_generic_multi_enter(xfer->endpoint,
	    xfer->stream_id, NULL);

	/* add transfer last on interrupt queue */
	usbd_transfer_enqueue(&xfer->xroot->bus->intr_q, xfer);

	/* start timeout, if any */
	if (xfer->timeout != 0)
		usbd_transfer_timeout_ms(xfer, &xhci_timeout, xfer->timeout);
}

static const struct usb_pipe_methods xhci_device_generic_methods =
{
	.open = xhci_device_generic_open,
	.close = xhci_device_generic_close,
	.enter = xhci_device_generic_enter,
	.start = xhci_device_generic_start,
};

/*------------------------------------------------------------------------*
 * xhci root HUB support
 *------------------------------------------------------------------------*
 * Simulate a hardware HUB by handling all the necessary requests.
 *------------------------------------------------------------------------*/

#define	HSETW(ptr, val) ptr = { (uint8_t)(val), (uint8_t)((val) >> 8) }

static const
struct usb_device_descriptor xhci_devd =
{
	.bLength = sizeof(xhci_devd),
	.bDescriptorType = UDESC_DEVICE,	/* type */
	HSETW(.bcdUSB, 0x0300),			/* USB version */
	.bDeviceClass = UDCLASS_HUB,		/* class */
	.bDeviceSubClass = UDSUBCLASS_HUB,	/* subclass */
	.bDeviceProtocol = UDPROTO_SSHUB,	/* protocol */
	.bMaxPacketSize = 9,			/* max packet size */
	HSETW(.idVendor, 0x0000),		/* vendor */
	HSETW(.idProduct, 0x0000),		/* product */
	HSETW(.bcdDevice, 0x0100),		/* device version */
	.iManufacturer = 1,
	.iProduct = 2,
	.iSerialNumber = 0,
	.bNumConfigurations = 1,		/* # of configurations */
};

static const
struct xhci_bos_desc xhci_bosd = {
	.bosd = {
		.bLength = sizeof(xhci_bosd.bosd),
		.bDescriptorType = UDESC_BOS,
		HSETW(.wTotalLength, sizeof(xhci_bosd)),
		.bNumDeviceCaps = 3,
	},
	.usb2extd = {
		.bLength = sizeof(xhci_bosd.usb2extd),
		.bDescriptorType = 1,
		.bDevCapabilityType = 2,
		.bmAttributes[0] = 2,
	},
	.usbdcd = {
		.bLength = sizeof(xhci_bosd.usbdcd),
		.bDescriptorType = UDESC_DEVICE_CAPABILITY,
		.bDevCapabilityType = 3,
		.bmAttributes = 0, /* XXX */
		HSETW(.wSpeedsSupported, 0x000C),
		.bFunctionalitySupport = 8,
		.bU1DevExitLat = 255,	/* dummy - not used */
		.wU2DevExitLat = { 0x00, 0x08 },
	},
	.cidd = {
		.bLength = sizeof(xhci_bosd.cidd),
		.bDescriptorType = 1,
		.bDevCapabilityType = 4,
		.bReserved = 0,
		.bContainerID = 0, /* XXX */
	},
};

static const
struct xhci_config_desc xhci_confd = {
	.confd = {
		.bLength = sizeof(xhci_confd.confd),
		.bDescriptorType = UDESC_CONFIG,
		.wTotalLength[0] = sizeof(xhci_confd),
		.bNumInterface = 1,
		.bConfigurationValue = 1,
		.iConfiguration = 0,
		.bmAttributes = UC_SELF_POWERED,
		.bMaxPower = 0		/* max power */
	},
	.ifcd = {
		.bLength = sizeof(xhci_confd.ifcd),
		.bDescriptorType = UDESC_INTERFACE,
		.bNumEndpoints = 1,
		.bInterfaceClass = UICLASS_HUB,
		.bInterfaceSubClass = UISUBCLASS_HUB,
		.bInterfaceProtocol = 0,
	},
	.endpd = {
		.bLength = sizeof(xhci_confd.endpd),
		.bDescriptorType = UDESC_ENDPOINT,
		.bEndpointAddress = UE_DIR_IN | XHCI_INTR_ENDPT,
		.bmAttributes = UE_INTERRUPT,
		.wMaxPacketSize[0] = 2,		/* max 15 ports */
		.bInterval = 255,
	},
	.endpcd = {
		.bLength = sizeof(xhci_confd.endpcd),
		.bDescriptorType = UDESC_ENDPOINT_SS_COMP,
		.bMaxBurst = 0,
		.bmAttributes = 0,
	},
};

static const
struct usb_hub_ss_descriptor xhci_hubd = {
	.bLength = sizeof(xhci_hubd),
	.bDescriptorType = UDESC_SS_HUB,
};

static usb_error_t
xhci_roothub_exec(struct usb_device *udev,
    struct usb_device_request *req, const void **pptr, uint16_t *plength)
{
	struct xhci_softc *sc = XHCI_BUS2SC(udev->bus);
	const char *str_ptr;
	const void *ptr;
	uint32_t port;
	uint32_t v;
	uint16_t len;
	uint16_t i;
	uint16_t value;
	uint16_t index;
	uint8_t j;
	usb_error_t err;

	USB_BUS_LOCK_ASSERT(&sc->sc_bus, MA_OWNED);

	/* buffer reset */
	ptr = (const void *)&sc->sc_hub_desc;
	len = 0;
	err = 0;

	value = UGETW(req->wValue);
	index = UGETW(req->wIndex);

	DPRINTFN(3, "type=0x%02x request=0x%02x wLen=0x%04x "
	    "wValue=0x%04x wIndex=0x%04x\n",
	    req->bmRequestType, req->bRequest,
	    UGETW(req->wLength), value, index);

#define	C(x,y) ((x) | ((y) << 8))
	switch (C(req->bRequest, req->bmRequestType)) {
	case C(UR_CLEAR_FEATURE, UT_WRITE_DEVICE):
	case C(UR_CLEAR_FEATURE, UT_WRITE_INTERFACE):
	case C(UR_CLEAR_FEATURE, UT_WRITE_ENDPOINT):
		/*
		 * DEVICE_REMOTE_WAKEUP and ENDPOINT_HALT are no-ops
		 * for the integrated root hub.
		 */
		break;
	case C(UR_GET_CONFIG, UT_READ_DEVICE):
		len = 1;
		sc->sc_hub_desc.temp[0] = sc->sc_conf;
		break;
	case C(UR_GET_DESCRIPTOR, UT_READ_DEVICE):
		switch (value >> 8) {
		case UDESC_DEVICE:
			if ((value & 0xff) != 0) {
				err = USB_ERR_IOERROR;
				goto done;
			}
			len = sizeof(xhci_devd);
			ptr = (const void *)&xhci_devd;
			break;

		case UDESC_BOS:
			if ((value & 0xff) != 0) {
				err = USB_ERR_IOERROR;
				goto done;
			}
			len = sizeof(xhci_bosd);
			ptr = (const void *)&xhci_bosd;
			break;

		case UDESC_CONFIG:
			if ((value & 0xff) != 0) {
				err = USB_ERR_IOERROR;
				goto done;
			}
			len = sizeof(xhci_confd);
			ptr = (const void *)&xhci_confd;
			break;

		case UDESC_STRING:
			switch (value & 0xff) {
			case 0:	/* Language table */
				str_ptr = "\001";
				break;

			case 1:	/* Vendor */
				str_ptr = sc->sc_vendor;
				break;

			case 2:	/* Product */
				str_ptr = "XHCI root HUB";
				break;

			default:
				str_ptr = "";
				break;
			}

			len = usb_make_str_desc(
			    sc->sc_hub_desc.temp,
			    sizeof(sc->sc_hub_desc.temp),
			    str_ptr);
			break;

		default:
			err = USB_ERR_IOERROR;
			goto done;
		}
		break;
	case C(UR_GET_INTERFACE, UT_READ_INTERFACE):
		len = 1;
		sc->sc_hub_desc.temp[0] = 0;
		break;
	case C(UR_GET_STATUS, UT_READ_DEVICE):
		len = 2;
		USETW(sc->sc_hub_desc.stat.wStatus, UDS_SELF_POWERED);
		break;
	case C(UR_GET_STATUS, UT_READ_INTERFACE):
	case C(UR_GET_STATUS, UT_READ_ENDPOINT):
		len = 2;
		USETW(sc->sc_hub_desc.stat.wStatus, 0);
		break;
	case C(UR_SET_ADDRESS, UT_WRITE_DEVICE):
		if (value >= XHCI_MAX_DEVICES) {
			err = USB_ERR_IOERROR;
			goto done;
		}
		break;
	case C(UR_SET_CONFIG, UT_WRITE_DEVICE):
		if (value != 0 && value != 1) {
			err = USB_ERR_IOERROR;
			goto done;
		}
		sc->sc_conf = value;
		break;
	case C(UR_SET_DESCRIPTOR, UT_WRITE_DEVICE):
		break;
	case C(UR_SET_FEATURE, UT_WRITE_DEVICE):
	case C(UR_SET_FEATURE, UT_WRITE_INTERFACE):
	case C(UR_SET_FEATURE, UT_WRITE_ENDPOINT):
		err = USB_ERR_IOERROR;
		goto done;
	case C(UR_SET_INTERFACE, UT_WRITE_INTERFACE):
		break;
	case C(UR_SYNCH_FRAME, UT_WRITE_ENDPOINT):
		break;
		/* Hub requests */
	case C(UR_CLEAR_FEATURE, UT_WRITE_CLASS_DEVICE):
		break;
	case C(UR_CLEAR_FEATURE, UT_WRITE_CLASS_OTHER):
		DPRINTFN(9, "UR_CLEAR_PORT_FEATURE\n");

		if ((index < 1) ||
		    (index > sc->sc_noport)) {
			err = USB_ERR_IOERROR;
			goto done;
		}
		port = XHCI_PORTSC(index);

		v = XREAD4(sc, oper, port);
		i = XHCI_PS_PLS_GET(v);
		v &= ~XHCI_PS_CLEAR;

		switch (value) {
		case UHF_C_BH_PORT_RESET:
			XWRITE4(sc, oper, port, v | XHCI_PS_WRC);
			break;
		case UHF_C_PORT_CONFIG_ERROR:
			XWRITE4(sc, oper, port, v | XHCI_PS_CEC);
			break;
		case UHF_C_PORT_SUSPEND:
		case UHF_C_PORT_LINK_STATE:
			XWRITE4(sc, oper, port, v | XHCI_PS_PLC);
			break;
		case UHF_C_PORT_CONNECTION:
			XWRITE4(sc, oper, port, v | XHCI_PS_CSC);
			break;
		case UHF_C_PORT_ENABLE:
			XWRITE4(sc, oper, port, v | XHCI_PS_PEC);
			break;
		case UHF_C_PORT_OVER_CURRENT:
			XWRITE4(sc, oper, port, v | XHCI_PS_OCC);
			break;
		case UHF_C_PORT_RESET:
			XWRITE4(sc, oper, port, v | XHCI_PS_PRC);
			break;
		case UHF_PORT_ENABLE:
			XWRITE4(sc, oper, port, v | XHCI_PS_PED);
			break;
		case UHF_PORT_POWER:
			XWRITE4(sc, oper, port, v & ~XHCI_PS_PP);
			break;
		case UHF_PORT_INDICATOR:
			XWRITE4(sc, oper, port, v & ~XHCI_PS_PIC_SET(3));
			break;
		case UHF_PORT_SUSPEND:

			/* U3 -> U15 */
			if (i == 3) {
				XWRITE4(sc, oper, port, v |
				    XHCI_PS_PLS_SET(0xF) | XHCI_PS_LWS);
			}

			/* wait 20ms for resume sequence to complete */
			usb_pause_mtx(&sc->sc_bus.bus_mtx, hz / 50);

			/* U0 */
			XWRITE4(sc, oper, port, v |
			    XHCI_PS_PLS_SET(0) | XHCI_PS_LWS);
			break;
		default:
			err = USB_ERR_IOERROR;
			goto done;
		}
		break;

	case C(UR_GET_DESCRIPTOR, UT_READ_CLASS_DEVICE):
		if ((value & 0xff) != 0) {
			err = USB_ERR_IOERROR;
			goto done;
		}

		v = XREAD4(sc, capa, XHCI_HCSPARAMS0);

		sc->sc_hub_desc.hubd = xhci_hubd;

		sc->sc_hub_desc.hubd.bNbrPorts = sc->sc_noport;

		if (XHCI_HCS0_PPC(v))
			i = UHD_PWR_INDIVIDUAL;
		else
			i = UHD_PWR_GANGED;

		if (XHCI_HCS0_PIND(v))
			i |= UHD_PORT_IND;

		i |= UHD_OC_INDIVIDUAL;

		USETW(sc->sc_hub_desc.hubd.wHubCharacteristics, i);

		/* see XHCI section 5.4.9: */
		sc->sc_hub_desc.hubd.bPwrOn2PwrGood = 10;

		for (j = 1; j <= sc->sc_noport; j++) {

			v = XREAD4(sc, oper, XHCI_PORTSC(j));
			if (v & XHCI_PS_DR) {
				sc->sc_hub_desc.hubd.
				    DeviceRemovable[j / 8] |= 1U << (j % 8);
			}
		}
		len = sc->sc_hub_desc.hubd.bLength;
		break;

	case C(UR_GET_STATUS, UT_READ_CLASS_DEVICE):
		len = 16;
		memset(sc->sc_hub_desc.temp, 0, 16);
		break;

	case C(UR_GET_STATUS, UT_READ_CLASS_OTHER):
		DPRINTFN(9, "UR_GET_STATUS i=%d\n", index);

		if ((index < 1) ||
		    (index > sc->sc_noport)) {
			err = USB_ERR_IOERROR;
			goto done;
		}

		v = XREAD4(sc, oper, XHCI_PORTSC(index));

		DPRINTFN(9, "port status=0x%08x\n", v);

		i = UPS_PORT_LINK_STATE_SET(XHCI_PS_PLS_GET(v));

		switch (XHCI_PS_SPEED_GET(v)) {
		case 3:
			i |= UPS_HIGH_SPEED;
			break;
		case 2:
			i |= UPS_LOW_SPEED;
			break;
		case 1:
			/* FULL speed */
			break;
		default:
			i |= UPS_OTHER_SPEED;
			break;
		}

		if (v & XHCI_PS_CCS)
			i |= UPS_CURRENT_CONNECT_STATUS;
		if (v & XHCI_PS_PED)
			i |= UPS_PORT_ENABLED;
		if (v & XHCI_PS_OCA)
			i |= UPS_OVERCURRENT_INDICATOR;
		if (v & XHCI_PS_PR)
			i |= UPS_RESET;
		if (v & XHCI_PS_PP) {
			/*
			 * The USB 3.0 RH is using the
			 * USB 2.0's power bit
			 */
			i |= UPS_PORT_POWER;
		}
		USETW(sc->sc_hub_desc.ps.wPortStatus, i);

		i = 0;
		if (v & XHCI_PS_CSC)
			i |= UPS_C_CONNECT_STATUS;
		if (v & XHCI_PS_PEC)
			i |= UPS_C_PORT_ENABLED;
		if (v & XHCI_PS_OCC)
			i |= UPS_C_OVERCURRENT_INDICATOR;
		if (v & XHCI_PS_WRC)
			i |= UPS_C_BH_PORT_RESET;
		if (v & XHCI_PS_PRC)
			i |= UPS_C_PORT_RESET;
		if (v & XHCI_PS_PLC)
			i |= UPS_C_PORT_LINK_STATE;
		if (v & XHCI_PS_CEC)
			i |= UPS_C_PORT_CONFIG_ERROR;

		USETW(sc->sc_hub_desc.ps.wPortChange, i);
		len = sizeof(sc->sc_hub_desc.ps);
		break;

	case C(UR_SET_DESCRIPTOR, UT_WRITE_CLASS_DEVICE):
		err = USB_ERR_IOERROR;
		goto done;

	case C(UR_SET_FEATURE, UT_WRITE_CLASS_DEVICE):
		break;

	case C(UR_SET_FEATURE, UT_WRITE_CLASS_OTHER):

		i = index >> 8;
		index &= 0x00FF;

		if ((index < 1) ||
		    (index > sc->sc_noport)) {
			err = USB_ERR_IOERROR;
			goto done;
		}

		port = XHCI_PORTSC(index);
		v = XREAD4(sc, oper, port) & ~XHCI_PS_CLEAR;

		switch (value) {
		case UHF_PORT_U1_TIMEOUT:
			if (XHCI_PS_SPEED_GET(v) != 4) {
				err = USB_ERR_IOERROR;
				goto done;
			}
			port = XHCI_PORTPMSC(index);
			v = XREAD4(sc, oper, port);
			v &= ~XHCI_PM3_U1TO_SET(0xFF);
			v |= XHCI_PM3_U1TO_SET(i);
			XWRITE4(sc, oper, port, v);
			break;
		case UHF_PORT_U2_TIMEOUT:
			if (XHCI_PS_SPEED_GET(v) != 4) {
				err = USB_ERR_IOERROR;
				goto done;
			}
			port = XHCI_PORTPMSC(index);
			v = XREAD4(sc, oper, port);
			v &= ~XHCI_PM3_U2TO_SET(0xFF);
			v |= XHCI_PM3_U2TO_SET(i);
			XWRITE4(sc, oper, port, v);
			break;
		case UHF_BH_PORT_RESET:
			XWRITE4(sc, oper, port, v | XHCI_PS_WPR);
			break;
		case UHF_PORT_LINK_STATE:
			XWRITE4(sc, oper, port, v |
			    XHCI_PS_PLS_SET(i) | XHCI_PS_LWS);
			/* 4ms settle time */
			usb_pause_mtx(&sc->sc_bus.bus_mtx, hz / 250);
			break;
		case UHF_PORT_ENABLE:
			DPRINTFN(3, "set port enable %d\n", index);
			break;
		case UHF_PORT_SUSPEND:
			DPRINTFN(6, "suspend port %u (LPM=%u)\n", index, i);
			j = XHCI_PS_SPEED_GET(v);
			if ((j < 1) || (j > 3)) {
				/* non-supported speed */
				err = USB_ERR_IOERROR;
				goto done;
			}
			XWRITE4(sc, oper, port, v |
			    XHCI_PS_PLS_SET(i ? 2 /* LPM */ : 3) | XHCI_PS_LWS);
			break;
		case UHF_PORT_RESET:
			DPRINTFN(6, "reset port %d\n", index);
			XWRITE4(sc, oper, port, v | XHCI_PS_PR);
			break;
		case UHF_PORT_POWER:
			DPRINTFN(3, "set port power %d\n", index);
			XWRITE4(sc, oper, port, v | XHCI_PS_PP);
			break;
		case UHF_PORT_TEST:
			DPRINTFN(3, "set port test %d\n", index);
			break;
		case UHF_PORT_INDICATOR:
			DPRINTFN(3, "set port indicator %d\n", index);

			v &= ~XHCI_PS_PIC_SET(3);
			v |= XHCI_PS_PIC_SET(1);

			XWRITE4(sc, oper, port, v);
			break;
		default:
			err = USB_ERR_IOERROR;
			goto done;
		}
		break;

	case C(UR_CLEAR_TT_BUFFER, UT_WRITE_CLASS_OTHER):
	case C(UR_RESET_TT, UT_WRITE_CLASS_OTHER):
	case C(UR_GET_TT_STATE, UT_READ_CLASS_OTHER):
	case C(UR_STOP_TT, UT_WRITE_CLASS_OTHER):
		break;
	default:
		err = USB_ERR_IOERROR;
		goto done;
	}
done:
	*plength = len;
	*pptr = ptr;
	return (err);
}

static void
xhci_xfer_setup(struct usb_setup_params *parm)
{
	struct usb_page_search page_info;
	struct usb_page_cache *pc;
	struct usb_xfer *xfer;
	void *last_obj;
	uint32_t ntd;
	uint32_t n;

	xfer = parm->curr_xfer;

	/*
	 * The proof for the "ntd" formula is illustrated like this:
	 *
	 * +------------------------------------+
	 * |                                    |
	 * |         |remainder ->              |
	 * |   +-----+---+                      |
	 * |   | xxx | x | frm 0                |
	 * |   +-----+---++                     |
	 * |   | xxx | xx | frm 1               |
	 * |   +-----+----+                     |
	 * |            ...                     |
	 * +------------------------------------+
	 *
	 * "xxx" means a completely full USB transfer descriptor
	 *
	 * "x" and "xx" means a short USB packet
	 *
	 * For the remainder of an USB transfer modulo
	 * "max_data_length" we need two USB transfer descriptors.
	 * One to transfer the remaining data and one to finalise with
	 * a zero length packet in case the "force_short_xfer" flag is
	 * set. We only need two USB transfer descriptors in the case
	 * where the transfer length of the first one is a factor of
	 * "max_frame_size". The rest of the needed USB transfer
	 * descriptors is given by the buffer size divided by the
	 * maximum data payload.
	 */
	parm->hc_max_packet_size = 0x400;
	parm->hc_max_packet_count = 16 * 3;
	parm->hc_max_frame_size = XHCI_TD_PAYLOAD_MAX;

	xfer->flags_int.bdma_enable = 1;

	usbd_transfer_setup_sub(parm);

	if (xfer->flags_int.isochronous_xfr) {
		ntd = ((1 * xfer->nframes)
		    + (xfer->max_data_length / xfer->max_hc_frame_size));
	} else if (xfer->flags_int.control_xfr) {
		ntd = ((2 * xfer->nframes) + 1	/* STATUS */
		    + (xfer->max_data_length / xfer->max_hc_frame_size));
	} else {
		ntd = ((2 * xfer->nframes)
		    + (xfer->max_data_length / xfer->max_hc_frame_size));
	}

alloc_dma_set:

	if (parm->err)
		return;

	/*
	 * Allocate queue heads and transfer descriptors
	 */
	last_obj = NULL;

	if (usbd_transfer_setup_sub_malloc(
	    parm, &pc, sizeof(struct xhci_td),
	    XHCI_TD_ALIGN, ntd)) {
		parm->err = USB_ERR_NOMEM;
		return;
	}
	if (parm->buf) {
		for (n = 0; n != ntd; n++) {
			struct xhci_td *td;

			usbd_get_page(pc + n, 0, &page_info);

			td = page_info.buffer;

			/* init TD */
			td->td_self = page_info.physaddr;
			td->obj_next = last_obj;
			td->page_cache = pc + n;

			last_obj = td;

			usb_pc_cpu_flush(pc + n);
		}
	}
	xfer->td_start[xfer->flags_int.curr_dma_set] = last_obj;

	if (!xfer->flags_int.curr_dma_set) {
		xfer->flags_int.curr_dma_set = 1;
		goto alloc_dma_set;
	}
}

static usb_error_t
xhci_configure_reset_endpoint(struct usb_xfer *xfer)
{
	struct xhci_softc *sc = XHCI_BUS2SC(xfer->xroot->bus);
	struct usb_page_search buf_inp;
	struct usb_device *udev;
	struct xhci_endpoint_ext *pepext;
	struct usb_endpoint_descriptor *edesc;
	struct usb_page_cache *pcinp;
	usb_error_t err;
	usb_stream_t stream_id;
	uint8_t index;
	uint8_t epno;

	pepext = xhci_get_endpoint_ext(xfer->xroot->udev,
	    xfer->endpoint->edesc);

	udev = xfer->xroot->udev;
	index = udev->controller_slot_id;

	pcinp = &sc->sc_hw.devs[index].input_pc;

	usbd_get_page(pcinp, 0, &buf_inp);

	edesc = xfer->endpoint->edesc;

	epno = edesc->bEndpointAddress;
	stream_id = xfer->stream_id;

	if ((edesc->bmAttributes & UE_XFERTYPE) == UE_CONTROL)
		epno |= UE_DIR_IN;

	epno = XHCI_EPNO2EPID(epno);

 	if (epno == 0)
		return (USB_ERR_NO_PIPE);		/* invalid */

	XHCI_CMD_LOCK(sc);

	/* configure endpoint */

	err = xhci_configure_endpoint_by_xfer(xfer);

	if (err != 0) {
		XHCI_CMD_UNLOCK(sc);
		return (err);
	}

	/*
	 * Get the endpoint into the stopped state according to the
	 * endpoint context state diagram in the XHCI specification:
	 */

	err = xhci_cmd_stop_ep(sc, 0, epno, index);

	if (err != 0)
		DPRINTF("Could not stop endpoint %u\n", epno);

	err = xhci_cmd_reset_ep(sc, 0, epno, index);

	if (err != 0)
		DPRINTF("Could not reset endpoint %u\n", epno);

	err = xhci_cmd_set_tr_dequeue_ptr(sc,
	    (pepext->physaddr + (stream_id * sizeof(struct xhci_trb) *
	    XHCI_MAX_TRANSFERS)) | XHCI_EPCTX_2_DCS_SET(1),
	    stream_id, epno, index);

	if (err != 0)
		DPRINTF("Could not set dequeue ptr for endpoint %u\n", epno);

	/*
	 * Get the endpoint into the running state according to the
	 * endpoint context state diagram in the XHCI specification:
	 */

	xhci_configure_mask(udev, (1U << epno) | 1U, 0);

	if (epno > 1)
		err = xhci_cmd_configure_ep(sc, buf_inp.physaddr, 0, index);
	else
		err = xhci_cmd_evaluate_ctx(sc, buf_inp.physaddr, index);

	if (err != 0)
		DPRINTF("Could not configure endpoint %u\n", epno);

	XHCI_CMD_UNLOCK(sc);

	return (0);
}

static void
xhci_xfer_unsetup(struct usb_xfer *xfer)
{
	return;
}

static void
xhci_start_dma_delay(struct usb_xfer *xfer)
{
	struct xhci_softc *sc = XHCI_BUS2SC(xfer->xroot->bus);

	/* put transfer on interrupt queue (again) */
	usbd_transfer_enqueue(&sc->sc_bus.intr_q, xfer);

	(void)usb_proc_msignal(USB_BUS_CONTROL_XFER_PROC(&sc->sc_bus),
	    &sc->sc_config_msg[0], &sc->sc_config_msg[1]);
}

static void
xhci_configure_msg(struct usb_proc_msg *pm)
{
	struct xhci_softc *sc;
	struct xhci_endpoint_ext *pepext;
	struct usb_xfer *xfer;

	sc = XHCI_BUS2SC(((struct usb_bus_msg *)pm)->bus);

restart:
	TAILQ_FOREACH(xfer, &sc->sc_bus.intr_q.head, wait_entry) {

		pepext = xhci_get_endpoint_ext(xfer->xroot->udev,
		    xfer->endpoint->edesc);

		if ((pepext->trb_halted != 0) ||
		    (pepext->trb_running == 0)) {

			uint16_t i;

			/* clear halted and running */
			pepext->trb_halted = 0;
			pepext->trb_running = 0;

			/* nuke remaining buffered transfers */

			for (i = 0; i != (XHCI_MAX_TRANSFERS *
			    XHCI_MAX_STREAMS); i++) {
				/*
				 * NOTE: We need to use the timeout
				 * error code here else existing
				 * isochronous clients can get
				 * confused:
				 */
				if (pepext->xfer[i] != NULL) {
					xhci_device_done(pepext->xfer[i],
					    USB_ERR_TIMEOUT);
				}
			}

			/*
			 * NOTE: The USB transfer cannot vanish in
			 * this state!
			 */

			USB_BUS_UNLOCK(&sc->sc_bus);

			xhci_configure_reset_endpoint(xfer);

			USB_BUS_LOCK(&sc->sc_bus);

			/* check if halted is still cleared */
			if (pepext->trb_halted == 0) {
				pepext->trb_running = 1;
				memset(pepext->trb_index, 0,
				    sizeof(pepext->trb_index));
			}
			goto restart;
		}

		if (xfer->flags_int.did_dma_delay) {

			/* remove transfer from interrupt queue (again) */
			usbd_transfer_dequeue(xfer);

			/* we are finally done */
			usb_dma_delay_done_cb(xfer);

			/* queue changed - restart */
			goto restart;
		}
	}

	TAILQ_FOREACH(xfer, &sc->sc_bus.intr_q.head, wait_entry) {

		/* try to insert xfer on HW queue */
		xhci_transfer_insert(xfer);

		/* try to multi buffer */
		xhci_device_generic_multi_enter(xfer->endpoint,
		    xfer->stream_id, NULL);
	}
}

static void
xhci_ep_init(struct usb_device *udev, struct usb_endpoint_descriptor *edesc,
    struct usb_endpoint *ep)
{
	struct xhci_endpoint_ext *pepext;

	DPRINTFN(2, "endpoint=%p, addr=%d, endpt=%d, mode=%d\n",
	    ep, udev->address, edesc->bEndpointAddress, udev->flags.usb_mode);

	if (udev->parent_hub == NULL) {
		/* root HUB has special endpoint handling */
		return;
	}

	ep->methods = &xhci_device_generic_methods;

	pepext = xhci_get_endpoint_ext(udev, edesc);

	USB_BUS_LOCK(udev->bus);
	pepext->trb_halted = 1;
	pepext->trb_running = 0;
	USB_BUS_UNLOCK(udev->bus);
}

static void
xhci_ep_uninit(struct usb_device *udev, struct usb_endpoint *ep)
{

}

static void
xhci_ep_clear_stall(struct usb_device *udev, struct usb_endpoint *ep)
{
	struct xhci_endpoint_ext *pepext;

	DPRINTF("\n");

	if (udev->flags.usb_mode != USB_MODE_HOST) {
		/* not supported */
		return;
	}
	if (udev->parent_hub == NULL) {
		/* root HUB has special endpoint handling */
		return;
	}

	pepext = xhci_get_endpoint_ext(udev, ep->edesc);

	USB_BUS_LOCK(udev->bus);
	pepext->trb_halted = 1;
	pepext->trb_running = 0;
	USB_BUS_UNLOCK(udev->bus);
}

static usb_error_t
xhci_device_init(struct usb_device *udev)
{
	struct xhci_softc *sc = XHCI_BUS2SC(udev->bus);
	usb_error_t err;
	uint8_t temp;

	/* no init for root HUB */
	if (udev->parent_hub == NULL)
		return (0);

	XHCI_CMD_LOCK(sc);

	/* set invalid default */

	udev->controller_slot_id = sc->sc_noslot + 1;

	/* try to get a new slot ID from the XHCI */

	err = xhci_cmd_enable_slot(sc, &temp);

	if (err) {
		XHCI_CMD_UNLOCK(sc);
		return (err);
	}

	if (temp > sc->sc_noslot) {
		XHCI_CMD_UNLOCK(sc);
		return (USB_ERR_BAD_ADDRESS);
	}

	if (sc->sc_hw.devs[temp].state != XHCI_ST_DISABLED) {
		DPRINTF("slot %u already allocated.\n", temp);
		XHCI_CMD_UNLOCK(sc);
		return (USB_ERR_BAD_ADDRESS);
	}

	/* store slot ID for later reference */

	udev->controller_slot_id = temp;

	/* reset data structure */

	memset(&sc->sc_hw.devs[temp], 0, sizeof(sc->sc_hw.devs[0]));

	/* set mark slot allocated */

	sc->sc_hw.devs[temp].state = XHCI_ST_ENABLED;

	err = xhci_alloc_device_ext(udev);

	XHCI_CMD_UNLOCK(sc);

	/* get device into default state */

	if (err == 0)
		err = xhci_set_address(udev, NULL, 0);

	return (err);
}

static void
xhci_device_uninit(struct usb_device *udev)
{
	struct xhci_softc *sc = XHCI_BUS2SC(udev->bus);
	uint8_t index;

	/* no init for root HUB */
	if (udev->parent_hub == NULL)
		return;

	XHCI_CMD_LOCK(sc);

	index = udev->controller_slot_id;

	if (index <= sc->sc_noslot) {
		xhci_cmd_disable_slot(sc, index);
		sc->sc_hw.devs[index].state = XHCI_ST_DISABLED;

		/* free device extension */
		xhci_free_device_ext(udev);
	}

	XHCI_CMD_UNLOCK(sc);
}

static void
xhci_get_dma_delay(struct usb_device *udev, uint32_t *pus)
{
	/*
	 * Wait until the hardware has finished any possible use of
	 * the transfer descriptor(s)
	 */
	*pus = 2048;			/* microseconds */
}

static void
xhci_device_resume(struct usb_device *udev)
{
	struct xhci_softc *sc = XHCI_BUS2SC(udev->bus);
	uint8_t index;
	uint8_t n;
	uint8_t p;

	DPRINTF("\n");

	/* check for root HUB */
	if (udev->parent_hub == NULL)
		return;

	index = udev->controller_slot_id;

	XHCI_CMD_LOCK(sc);

	/* blindly resume all endpoints */

	USB_BUS_LOCK(udev->bus);

	for (n = 1; n != XHCI_MAX_ENDPOINTS; n++) {
		for (p = 0; p != XHCI_MAX_STREAMS; p++) {
			XWRITE4(sc, door, XHCI_DOORBELL(index),
			    n | XHCI_DB_SID_SET(p));
		}
	}

	USB_BUS_UNLOCK(udev->bus);

	XHCI_CMD_UNLOCK(sc);
}

static void
xhci_device_suspend(struct usb_device *udev)
{
	struct xhci_softc *sc = XHCI_BUS2SC(udev->bus);
	uint8_t index;
	uint8_t n;
	usb_error_t err;

	DPRINTF("\n");

	/* check for root HUB */
	if (udev->parent_hub == NULL)
		return;

	index = udev->controller_slot_id;

	XHCI_CMD_LOCK(sc);

	/* blindly suspend all endpoints */

	for (n = 1; n != XHCI_MAX_ENDPOINTS; n++) {
		err = xhci_cmd_stop_ep(sc, 1, n, index);
		if (err != 0) {
			DPRINTF("Failed to suspend endpoint "
			    "%u on slot %u (ignored).\n", n, index);
		}
	}

	XHCI_CMD_UNLOCK(sc);
}

static void
xhci_set_hw_power(struct usb_bus *bus)
{
	DPRINTF("\n");
}

static void
xhci_device_state_change(struct usb_device *udev)
{
	struct xhci_softc *sc = XHCI_BUS2SC(udev->bus);
	struct usb_page_search buf_inp;
	usb_error_t err;
	uint8_t index;

	/* check for root HUB */
	if (udev->parent_hub == NULL)
		return;

	index = udev->controller_slot_id;

	DPRINTF("\n");

	if (usb_get_device_state(udev) == USB_STATE_CONFIGURED) {
		err = uhub_query_info(udev, &sc->sc_hw.devs[index].nports, 
		    &sc->sc_hw.devs[index].tt);
		if (err != 0)
			sc->sc_hw.devs[index].nports = 0;
	}

	XHCI_CMD_LOCK(sc);

	switch (usb_get_device_state(udev)) {
	case USB_STATE_POWERED:
		if (sc->sc_hw.devs[index].state == XHCI_ST_DEFAULT)
			break;

		/* set default state */
		sc->sc_hw.devs[index].state = XHCI_ST_DEFAULT;

		/* reset number of contexts */
		sc->sc_hw.devs[index].context_num = 0;

		err = xhci_cmd_reset_dev(sc, index);

		if (err != 0) {
			DPRINTF("Device reset failed "
			    "for slot %u.\n", index);
		}
		break;

	case USB_STATE_ADDRESSED:
		if (sc->sc_hw.devs[index].state == XHCI_ST_ADDRESSED)
			break;

		sc->sc_hw.devs[index].state = XHCI_ST_ADDRESSED;

		/* set configure mask to slot only */
		xhci_configure_mask(udev, 1, 0);

		/* deconfigure all endpoints, except EP0 */
		err = xhci_cmd_configure_ep(sc, 0, 1, index);

		if (err) {
			DPRINTF("Failed to deconfigure "
			    "slot %u.\n", index);
		}
		break;

	case USB_STATE_CONFIGURED:
		if (sc->sc_hw.devs[index].state == XHCI_ST_CONFIGURED)
			break;

		/* set configured state */
		sc->sc_hw.devs[index].state = XHCI_ST_CONFIGURED;

		/* reset number of contexts */
		sc->sc_hw.devs[index].context_num = 0;

		usbd_get_page(&sc->sc_hw.devs[index].input_pc, 0, &buf_inp);

		xhci_configure_mask(udev, 3, 0);

		err = xhci_configure_device(udev);
		if (err != 0) {
			DPRINTF("Could not configure device "
			    "at slot %u.\n", index);
		}

		err = xhci_cmd_evaluate_ctx(sc, buf_inp.physaddr, index);
		if (err != 0) {
			DPRINTF("Could not evaluate device "
			    "context at slot %u.\n", index);
		}
		break;

	default:
		break;
	}
	XHCI_CMD_UNLOCK(sc);
}

static usb_error_t
xhci_set_endpoint_mode(struct usb_device *udev, struct usb_endpoint *ep,
    uint8_t ep_mode)
{
	switch (ep_mode) {
	case USB_EP_MODE_DEFAULT:
		return (0);
	case USB_EP_MODE_STREAMS:
		if (xhcistreams == 0 || 
		    (ep->edesc->bmAttributes & UE_XFERTYPE) != UE_BULK ||
		    udev->speed != USB_SPEED_SUPER)
			return (USB_ERR_INVAL);
		return (0);
	default:
		return (USB_ERR_INVAL);
	}
}

static const struct usb_bus_methods xhci_bus_methods = {
	.endpoint_init = xhci_ep_init,
	.endpoint_uninit = xhci_ep_uninit,
	.xfer_setup = xhci_xfer_setup,
	.xfer_unsetup = xhci_xfer_unsetup,
	.get_dma_delay = xhci_get_dma_delay,
	.device_init = xhci_device_init,
	.device_uninit = xhci_device_uninit,
	.device_resume = xhci_device_resume,
	.device_suspend = xhci_device_suspend,
	.set_hw_power = xhci_set_hw_power,
	.roothub_exec = xhci_roothub_exec,
	.xfer_poll = xhci_do_poll,
	.start_dma_delay = xhci_start_dma_delay,
	.set_address = xhci_set_address,
	.clear_stall = xhci_ep_clear_stall,
	.device_state_change = xhci_device_state_change,
	.set_hw_power_sleep = xhci_set_hw_power_sleep,
	.set_endpoint_mode = xhci_set_endpoint_mode,
};
