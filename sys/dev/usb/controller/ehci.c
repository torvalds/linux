/* $FreeBSD$ */
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Hans Petter Selasky. All rights reserved.
 * Copyright (c) 2004 The NetBSD Foundation, Inc. All rights reserved.
 * Copyright (c) 2004 Lennart Augustsson. All rights reserved.
 * Copyright (c) 2004 Charles M. Hannum. All rights reserved.
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
 * USB Enhanced Host Controller Driver, a.k.a. USB 2.0 controller.
 *
 * The EHCI 0.96 spec can be found at
 * http://developer.intel.com/technology/usb/download/ehci-r096.pdf
 * The EHCI 1.0 spec can be found at
 * http://developer.intel.com/technology/usb/download/ehci-r10.pdf
 * and the USB 2.0 spec at
 * http://www.usb.org/developers/docs/usb_20.zip
 *
 */

/*
 * TODO: 
 * 1) command failures are not recovered correctly
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

#define	USB_DEBUG_VAR ehcidebug

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

#include <dev/usb/controller/ehci.h>
#include <dev/usb/controller/ehcireg.h>

#define	EHCI_BUS2SC(bus) \
   ((ehci_softc_t *)(((uint8_t *)(bus)) - \
    ((uint8_t *)&(((ehci_softc_t *)0)->sc_bus))))

#ifdef USB_DEBUG
static int ehcidebug = 0;
static int ehcinohighspeed = 0;
static int ehciiaadbug = 0;
static int ehcilostintrbug = 0;

static SYSCTL_NODE(_hw_usb, OID_AUTO, ehci, CTLFLAG_RW, 0, "USB ehci");
SYSCTL_INT(_hw_usb_ehci, OID_AUTO, debug, CTLFLAG_RWTUN,
    &ehcidebug, 0, "Debug level");
SYSCTL_INT(_hw_usb_ehci, OID_AUTO, no_hs, CTLFLAG_RWTUN,
    &ehcinohighspeed, 0, "Disable High Speed USB");
SYSCTL_INT(_hw_usb_ehci, OID_AUTO, iaadbug, CTLFLAG_RWTUN,
    &ehciiaadbug, 0, "Enable doorbell bug workaround");
SYSCTL_INT(_hw_usb_ehci, OID_AUTO, lostintrbug, CTLFLAG_RWTUN,
    &ehcilostintrbug, 0, "Enable lost interrupt bug workaround");

static void ehci_dump_regs(ehci_softc_t *sc);
static void ehci_dump_sqh(ehci_softc_t *sc, ehci_qh_t *sqh);

#endif

#define	EHCI_INTR_ENDPT 1

static const struct usb_bus_methods ehci_bus_methods;
static const struct usb_pipe_methods ehci_device_bulk_methods;
static const struct usb_pipe_methods ehci_device_ctrl_methods;
static const struct usb_pipe_methods ehci_device_intr_methods;
static const struct usb_pipe_methods ehci_device_isoc_fs_methods;
static const struct usb_pipe_methods ehci_device_isoc_hs_methods;

static void ehci_do_poll(struct usb_bus *);
static void ehci_device_done(struct usb_xfer *, usb_error_t);
static uint8_t ehci_check_transfer(struct usb_xfer *);
static void ehci_timeout(void *);
static void ehci_poll_timeout(void *);

static void ehci_root_intr(ehci_softc_t *sc);

struct ehci_std_temp {
	ehci_softc_t *sc;
	struct usb_page_cache *pc;
	ehci_qtd_t *td;
	ehci_qtd_t *td_next;
	uint32_t average;
	uint32_t qtd_status;
	uint32_t len;
	uint16_t max_frame_size;
	uint8_t	shortpkt;
	uint8_t	auto_data_toggle;
	uint8_t	setup_alt_next;
	uint8_t	last_frame;
};

void
ehci_iterate_hw_softc(struct usb_bus *bus, usb_bus_mem_sub_cb_t *cb)
{
	ehci_softc_t *sc = EHCI_BUS2SC(bus);
	uint32_t i;

	cb(bus, &sc->sc_hw.pframes_pc, &sc->sc_hw.pframes_pg,
	    sizeof(uint32_t) * EHCI_FRAMELIST_COUNT, EHCI_FRAMELIST_ALIGN);

	cb(bus, &sc->sc_hw.terminate_pc, &sc->sc_hw.terminate_pg,
	    sizeof(struct ehci_qh_sub), EHCI_QH_ALIGN);

	cb(bus, &sc->sc_hw.async_start_pc, &sc->sc_hw.async_start_pg,
	    sizeof(ehci_qh_t), EHCI_QH_ALIGN);

	for (i = 0; i != EHCI_VIRTUAL_FRAMELIST_COUNT; i++) {
		cb(bus, sc->sc_hw.intr_start_pc + i,
		    sc->sc_hw.intr_start_pg + i,
		    sizeof(ehci_qh_t), EHCI_QH_ALIGN);
	}

	for (i = 0; i != EHCI_VIRTUAL_FRAMELIST_COUNT; i++) {
		cb(bus, sc->sc_hw.isoc_hs_start_pc + i,
		    sc->sc_hw.isoc_hs_start_pg + i,
		    sizeof(ehci_itd_t), EHCI_ITD_ALIGN);
	}

	for (i = 0; i != EHCI_VIRTUAL_FRAMELIST_COUNT; i++) {
		cb(bus, sc->sc_hw.isoc_fs_start_pc + i,
		    sc->sc_hw.isoc_fs_start_pg + i,
		    sizeof(ehci_sitd_t), EHCI_SITD_ALIGN);
	}
}

usb_error_t
ehci_reset(ehci_softc_t *sc)
{
	uint32_t hcr;
	int i;

	EOWRITE4(sc, EHCI_USBCMD, EHCI_CMD_HCRESET);
	for (i = 0; i < 100; i++) {
		usb_pause_mtx(NULL, hz / 128);
		hcr = EOREAD4(sc, EHCI_USBCMD) & EHCI_CMD_HCRESET;
		if (!hcr) {
			if (sc->sc_vendor_post_reset != NULL)
				sc->sc_vendor_post_reset(sc);
			return (0);
		}
	}
	device_printf(sc->sc_bus.bdev, "reset timeout\n");
	return (USB_ERR_IOERROR);
}

static usb_error_t
ehci_hcreset(ehci_softc_t *sc)
{
	uint32_t hcr;
	int i;

	EOWRITE4(sc, EHCI_USBCMD, 0);	/* Halt controller */
	for (i = 0; i < 100; i++) {
		usb_pause_mtx(NULL, hz / 128);
		hcr = EOREAD4(sc, EHCI_USBSTS) & EHCI_STS_HCH;
		if (hcr)
			break;
	}
	if (!hcr)
		/*
                 * Fall through and try reset anyway even though
                 * Table 2-9 in the EHCI spec says this will result
                 * in undefined behavior.
                 */
		device_printf(sc->sc_bus.bdev, "stop timeout\n");

	return (ehci_reset(sc));
}

static int
ehci_init_sub(struct ehci_softc *sc)
{
	struct usb_page_search buf_res;
	uint32_t cparams;
  	uint32_t hcr;
	uint8_t i;

	cparams = EREAD4(sc, EHCI_HCCPARAMS);

	DPRINTF("cparams=0x%x\n", cparams);

	if (EHCI_HCC_64BIT(cparams)) {
		DPRINTF("HCC uses 64-bit structures\n");

		/* MUST clear segment register if 64 bit capable */
		EOWRITE4(sc, EHCI_CTRLDSSEGMENT, 0);
	}

	usbd_get_page(&sc->sc_hw.pframes_pc, 0, &buf_res);
	EOWRITE4(sc, EHCI_PERIODICLISTBASE, buf_res.physaddr);

	usbd_get_page(&sc->sc_hw.async_start_pc, 0, &buf_res);
	EOWRITE4(sc, EHCI_ASYNCLISTADDR, buf_res.physaddr | EHCI_LINK_QH);

	/* enable interrupts */
	EOWRITE4(sc, EHCI_USBINTR, sc->sc_eintrs);

	/* turn on controller */
	EOWRITE4(sc, EHCI_USBCMD,
	    EHCI_CMD_ITC_1 |		/* 1 microframes interrupt delay */
	    (EOREAD4(sc, EHCI_USBCMD) & EHCI_CMD_FLS_M) |
	    EHCI_CMD_ASE |
	    EHCI_CMD_PSE |
	    EHCI_CMD_RS);

	/* Take over port ownership */
	EOWRITE4(sc, EHCI_CONFIGFLAG, EHCI_CONF_CF);

	for (i = 0; i < 100; i++) {
		usb_pause_mtx(NULL, hz / 128);
		hcr = EOREAD4(sc, EHCI_USBSTS) & EHCI_STS_HCH;
		if (!hcr) {
			break;
		}
	}
	if (hcr) {
		device_printf(sc->sc_bus.bdev, "run timeout\n");
		return (USB_ERR_IOERROR);
	}
	return (USB_ERR_NORMAL_COMPLETION);
}

usb_error_t
ehci_init(ehci_softc_t *sc)
{
	struct usb_page_search buf_res;
	uint32_t version;
	uint32_t sparams;
	uint16_t i;
	uint16_t x;
	uint16_t y;
	uint16_t bit;
	usb_error_t err = 0;

	DPRINTF("start\n");

	usb_callout_init_mtx(&sc->sc_tmo_pcd, &sc->sc_bus.bus_mtx, 0);
	usb_callout_init_mtx(&sc->sc_tmo_poll, &sc->sc_bus.bus_mtx, 0);

	sc->sc_offs = EHCI_CAPLENGTH(EREAD4(sc, EHCI_CAPLEN_HCIVERSION));

#ifdef USB_DEBUG
	if (ehciiaadbug)
		sc->sc_flags |= EHCI_SCFLG_IAADBUG;
	if (ehcilostintrbug)
		sc->sc_flags |= EHCI_SCFLG_LOSTINTRBUG;
	if (ehcidebug > 2) {
		ehci_dump_regs(sc);
	}
#endif

	version = EHCI_HCIVERSION(EREAD4(sc, EHCI_CAPLEN_HCIVERSION));
	device_printf(sc->sc_bus.bdev, "EHCI version %x.%x\n",
	    version >> 8, version & 0xff);

	sparams = EREAD4(sc, EHCI_HCSPARAMS);
	DPRINTF("sparams=0x%x\n", sparams);

	sc->sc_noport = EHCI_HCS_N_PORTS(sparams);
	sc->sc_bus.usbrev = USB_REV_2_0;

	if (!(sc->sc_flags & EHCI_SCFLG_DONTRESET)) {
		/* Reset the controller */
		DPRINTF("%s: resetting\n",
		    device_get_nameunit(sc->sc_bus.bdev));

		err = ehci_hcreset(sc);
		if (err) {
			device_printf(sc->sc_bus.bdev, "reset timeout\n");
			return (err);
		}
	}

	/*
	 * use current frame-list-size selection 0: 1024*4 bytes 1:  512*4
	 * bytes 2:  256*4 bytes 3:      unknown
	 */
	if (EHCI_CMD_FLS(EOREAD4(sc, EHCI_USBCMD)) == 3) {
		device_printf(sc->sc_bus.bdev, "invalid frame-list-size\n");
		return (USB_ERR_IOERROR);
	}
	/* set up the bus struct */
	sc->sc_bus.methods = &ehci_bus_methods;

	sc->sc_eintrs = EHCI_NORMAL_INTRS;

	if (1) {
		struct ehci_qh_sub *qh;

		usbd_get_page(&sc->sc_hw.terminate_pc, 0, &buf_res);

		qh = buf_res.buffer;

		sc->sc_terminate_self = htohc32(sc, buf_res.physaddr);

		/* init terminate TD */
		qh->qtd_next =
		    htohc32(sc, EHCI_LINK_TERMINATE);
		qh->qtd_altnext =
		    htohc32(sc, EHCI_LINK_TERMINATE);
		qh->qtd_status =
		    htohc32(sc, EHCI_QTD_HALTED);
	}

	for (i = 0; i < EHCI_VIRTUAL_FRAMELIST_COUNT; i++) {
		ehci_qh_t *qh;

		usbd_get_page(sc->sc_hw.intr_start_pc + i, 0, &buf_res);

		qh = buf_res.buffer;

		/* initialize page cache pointer */

		qh->page_cache = sc->sc_hw.intr_start_pc + i;

		/* store a pointer to queue head */

		sc->sc_intr_p_last[i] = qh;

		qh->qh_self =
		    htohc32(sc, buf_res.physaddr) |
		    htohc32(sc, EHCI_LINK_QH);

		qh->qh_endp =
		    htohc32(sc, EHCI_QH_SET_EPS(EHCI_QH_SPEED_HIGH));
		qh->qh_endphub =
		    htohc32(sc, EHCI_QH_SET_MULT(1));
		qh->qh_curqtd = 0;

		qh->qh_qtd.qtd_next =
		    htohc32(sc, EHCI_LINK_TERMINATE);
		qh->qh_qtd.qtd_altnext =
		    htohc32(sc, EHCI_LINK_TERMINATE);
		qh->qh_qtd.qtd_status =
		    htohc32(sc, EHCI_QTD_HALTED);
	}

	/*
	 * the QHs are arranged to give poll intervals that are
	 * powers of 2 times 1ms
	 */
	bit = EHCI_VIRTUAL_FRAMELIST_COUNT / 2;
	while (bit) {
		x = bit;
		while (x & bit) {
			ehci_qh_t *qh_x;
			ehci_qh_t *qh_y;

			y = (x ^ bit) | (bit / 2);

			qh_x = sc->sc_intr_p_last[x];
			qh_y = sc->sc_intr_p_last[y];

			/*
			 * the next QH has half the poll interval
			 */
			qh_x->qh_link = qh_y->qh_self;

			x++;
		}
		bit >>= 1;
	}

	if (1) {
		ehci_qh_t *qh;

		qh = sc->sc_intr_p_last[0];

		/* the last (1ms) QH terminates */
		qh->qh_link = htohc32(sc, EHCI_LINK_TERMINATE);
	}
	for (i = 0; i < EHCI_VIRTUAL_FRAMELIST_COUNT; i++) {
		ehci_sitd_t *sitd;
		ehci_itd_t *itd;

		usbd_get_page(sc->sc_hw.isoc_fs_start_pc + i, 0, &buf_res);

		sitd = buf_res.buffer;

		/* initialize page cache pointer */

		sitd->page_cache = sc->sc_hw.isoc_fs_start_pc + i;

		/* store a pointer to the transfer descriptor */

		sc->sc_isoc_fs_p_last[i] = sitd;

		/* initialize full speed isochronous */

		sitd->sitd_self =
		    htohc32(sc, buf_res.physaddr) |
		    htohc32(sc, EHCI_LINK_SITD);

		sitd->sitd_back =
		    htohc32(sc, EHCI_LINK_TERMINATE);

		sitd->sitd_next =
		    sc->sc_intr_p_last[i | (EHCI_VIRTUAL_FRAMELIST_COUNT / 2)]->qh_self;


		usbd_get_page(sc->sc_hw.isoc_hs_start_pc + i, 0, &buf_res);

		itd = buf_res.buffer;

		/* initialize page cache pointer */

		itd->page_cache = sc->sc_hw.isoc_hs_start_pc + i;

		/* store a pointer to the transfer descriptor */

		sc->sc_isoc_hs_p_last[i] = itd;

		/* initialize high speed isochronous */

		itd->itd_self =
		    htohc32(sc, buf_res.physaddr) |
		    htohc32(sc, EHCI_LINK_ITD);

		itd->itd_next =
		    sitd->sitd_self;
	}

	usbd_get_page(&sc->sc_hw.pframes_pc, 0, &buf_res);

	if (1) {
		uint32_t *pframes;

		pframes = buf_res.buffer;

		/*
		 * execution order:
		 * pframes -> high speed isochronous ->
		 *    full speed isochronous -> interrupt QH's
		 */
		for (i = 0; i < EHCI_FRAMELIST_COUNT; i++) {
			pframes[i] = sc->sc_isoc_hs_p_last
			    [i & (EHCI_VIRTUAL_FRAMELIST_COUNT - 1)]->itd_self;
		}
	}
	usbd_get_page(&sc->sc_hw.async_start_pc, 0, &buf_res);

	if (1) {

		ehci_qh_t *qh;

		qh = buf_res.buffer;

		/* initialize page cache pointer */

		qh->page_cache = &sc->sc_hw.async_start_pc;

		/* store a pointer to the queue head */

		sc->sc_async_p_last = qh;

		/* init dummy QH that starts the async list */

		qh->qh_self =
		    htohc32(sc, buf_res.physaddr) |
		    htohc32(sc, EHCI_LINK_QH);

		/* fill the QH */
		qh->qh_endp =
		    htohc32(sc, EHCI_QH_SET_EPS(EHCI_QH_SPEED_HIGH) | EHCI_QH_HRECL);
		qh->qh_endphub = htohc32(sc, EHCI_QH_SET_MULT(1));
		qh->qh_link = qh->qh_self;
		qh->qh_curqtd = 0;

		/* fill the overlay qTD */
		qh->qh_qtd.qtd_next = htohc32(sc, EHCI_LINK_TERMINATE);
		qh->qh_qtd.qtd_altnext = htohc32(sc, EHCI_LINK_TERMINATE);
		qh->qh_qtd.qtd_status = htohc32(sc, EHCI_QTD_HALTED);
	}
	/* flush all cache into memory */

	usb_bus_mem_flush_all(&sc->sc_bus, &ehci_iterate_hw_softc);

#ifdef USB_DEBUG
	if (ehcidebug) {
		ehci_dump_sqh(sc, sc->sc_async_p_last);
	}
#endif

	/* finial setup */
 	err = ehci_init_sub(sc);

	if (!err) {
		/* catch any lost interrupts */
		ehci_do_poll(&sc->sc_bus);
	}
	return (err);
}

/*
 * shut down the controller when the system is going down
 */
void
ehci_detach(ehci_softc_t *sc)
{
	USB_BUS_LOCK(&sc->sc_bus);

	usb_callout_stop(&sc->sc_tmo_pcd);
	usb_callout_stop(&sc->sc_tmo_poll);

	EOWRITE4(sc, EHCI_USBINTR, 0);
	USB_BUS_UNLOCK(&sc->sc_bus);

	if (ehci_hcreset(sc)) {
		DPRINTF("reset failed!\n");
	}

	/* XXX let stray task complete */
	usb_pause_mtx(NULL, hz / 20);

	usb_callout_drain(&sc->sc_tmo_pcd);
	usb_callout_drain(&sc->sc_tmo_poll);
}

static void
ehci_suspend(ehci_softc_t *sc)
{
	DPRINTF("stopping the HC\n");

	/* reset HC */
	ehci_hcreset(sc);
}

static void
ehci_resume(ehci_softc_t *sc)
{
	/* reset HC */
	ehci_hcreset(sc);

	/* setup HC */
	ehci_init_sub(sc);

	/* catch any lost interrupts */
	ehci_do_poll(&sc->sc_bus);
}

#ifdef USB_DEBUG
static void
ehci_dump_regs(ehci_softc_t *sc)
{
	uint32_t i;

	i = EOREAD4(sc, EHCI_USBCMD);
	printf("cmd=0x%08x\n", i);

	if (i & EHCI_CMD_ITC_1)
		printf(" EHCI_CMD_ITC_1\n");
	if (i & EHCI_CMD_ITC_2)
		printf(" EHCI_CMD_ITC_2\n");
	if (i & EHCI_CMD_ITC_4)
		printf(" EHCI_CMD_ITC_4\n");
	if (i & EHCI_CMD_ITC_8)
		printf(" EHCI_CMD_ITC_8\n");
	if (i & EHCI_CMD_ITC_16)
		printf(" EHCI_CMD_ITC_16\n");
	if (i & EHCI_CMD_ITC_32)
		printf(" EHCI_CMD_ITC_32\n");
	if (i & EHCI_CMD_ITC_64)
		printf(" EHCI_CMD_ITC_64\n");
	if (i & EHCI_CMD_ASPME)
		printf(" EHCI_CMD_ASPME\n");
	if (i & EHCI_CMD_ASPMC)
		printf(" EHCI_CMD_ASPMC\n");
	if (i & EHCI_CMD_LHCR)
		printf(" EHCI_CMD_LHCR\n");
	if (i & EHCI_CMD_IAAD)
		printf(" EHCI_CMD_IAAD\n");
	if (i & EHCI_CMD_ASE)
		printf(" EHCI_CMD_ASE\n");
	if (i & EHCI_CMD_PSE)
		printf(" EHCI_CMD_PSE\n");
	if (i & EHCI_CMD_FLS_M)
		printf(" EHCI_CMD_FLS_M\n");
	if (i & EHCI_CMD_HCRESET)
		printf(" EHCI_CMD_HCRESET\n");
	if (i & EHCI_CMD_RS)
		printf(" EHCI_CMD_RS\n");

	i = EOREAD4(sc, EHCI_USBSTS);

	printf("sts=0x%08x\n", i);

	if (i & EHCI_STS_ASS)
		printf(" EHCI_STS_ASS\n");
	if (i & EHCI_STS_PSS)
		printf(" EHCI_STS_PSS\n");
	if (i & EHCI_STS_REC)
		printf(" EHCI_STS_REC\n");
	if (i & EHCI_STS_HCH)
		printf(" EHCI_STS_HCH\n");
	if (i & EHCI_STS_IAA)
		printf(" EHCI_STS_IAA\n");
	if (i & EHCI_STS_HSE)
		printf(" EHCI_STS_HSE\n");
	if (i & EHCI_STS_FLR)
		printf(" EHCI_STS_FLR\n");
	if (i & EHCI_STS_PCD)
		printf(" EHCI_STS_PCD\n");
	if (i & EHCI_STS_ERRINT)
		printf(" EHCI_STS_ERRINT\n");
	if (i & EHCI_STS_INT)
		printf(" EHCI_STS_INT\n");

	printf("ien=0x%08x\n",
	    EOREAD4(sc, EHCI_USBINTR));
	printf("frindex=0x%08x ctrdsegm=0x%08x periodic=0x%08x async=0x%08x\n",
	    EOREAD4(sc, EHCI_FRINDEX),
	    EOREAD4(sc, EHCI_CTRLDSSEGMENT),
	    EOREAD4(sc, EHCI_PERIODICLISTBASE),
	    EOREAD4(sc, EHCI_ASYNCLISTADDR));
	for (i = 1; i <= sc->sc_noport; i++) {
		printf("port %d status=0x%08x\n", i,
		    EOREAD4(sc, EHCI_PORTSC(i)));
	}
}

static void
ehci_dump_link(ehci_softc_t *sc, uint32_t link, int type)
{
	link = hc32toh(sc, link);
	printf("0x%08x", link);
	if (link & EHCI_LINK_TERMINATE)
		printf("<T>");
	else {
		printf("<");
		if (type) {
			switch (EHCI_LINK_TYPE(link)) {
			case EHCI_LINK_ITD:
				printf("ITD");
				break;
			case EHCI_LINK_QH:
				printf("QH");
				break;
			case EHCI_LINK_SITD:
				printf("SITD");
				break;
			case EHCI_LINK_FSTN:
				printf("FSTN");
				break;
			}
		}
		printf(">");
	}
}

static void
ehci_dump_qtd(ehci_softc_t *sc, ehci_qtd_t *qtd)
{
	uint32_t s;

	printf("  next=");
	ehci_dump_link(sc, qtd->qtd_next, 0);
	printf(" altnext=");
	ehci_dump_link(sc, qtd->qtd_altnext, 0);
	printf("\n");
	s = hc32toh(sc, qtd->qtd_status);
	printf("  status=0x%08x: toggle=%d bytes=0x%x ioc=%d c_page=0x%x\n",
	    s, EHCI_QTD_GET_TOGGLE(s), EHCI_QTD_GET_BYTES(s),
	    EHCI_QTD_GET_IOC(s), EHCI_QTD_GET_C_PAGE(s));
	printf("    cerr=%d pid=%d stat=%s%s%s%s%s%s%s%s\n",
	    EHCI_QTD_GET_CERR(s), EHCI_QTD_GET_PID(s),
	    (s & EHCI_QTD_ACTIVE) ? "ACTIVE" : "NOT_ACTIVE",
	    (s & EHCI_QTD_HALTED) ? "-HALTED" : "",
	    (s & EHCI_QTD_BUFERR) ? "-BUFERR" : "",
	    (s & EHCI_QTD_BABBLE) ? "-BABBLE" : "",
	    (s & EHCI_QTD_XACTERR) ? "-XACTERR" : "",
	    (s & EHCI_QTD_MISSEDMICRO) ? "-MISSED" : "",
	    (s & EHCI_QTD_SPLITXSTATE) ? "-SPLIT" : "",
	    (s & EHCI_QTD_PINGSTATE) ? "-PING" : "");

	for (s = 0; s < 5; s++) {
		printf("  buffer[%d]=0x%08x\n", s,
		    hc32toh(sc, qtd->qtd_buffer[s]));
	}
	for (s = 0; s < 5; s++) {
		printf("  buffer_hi[%d]=0x%08x\n", s,
		    hc32toh(sc, qtd->qtd_buffer_hi[s]));
	}
}

static uint8_t
ehci_dump_sqtd(ehci_softc_t *sc, ehci_qtd_t *sqtd)
{
	uint8_t temp;

	usb_pc_cpu_invalidate(sqtd->page_cache);
	printf("QTD(%p) at 0x%08x:\n", sqtd, hc32toh(sc, sqtd->qtd_self));
	ehci_dump_qtd(sc, sqtd);
	temp = (sqtd->qtd_next & htohc32(sc, EHCI_LINK_TERMINATE)) ? 1 : 0;
	return (temp);
}

static void
ehci_dump_sqtds(ehci_softc_t *sc, ehci_qtd_t *sqtd)
{
	uint16_t i;
	uint8_t stop;

	stop = 0;
	for (i = 0; sqtd && (i < 20) && !stop; sqtd = sqtd->obj_next, i++) {
		stop = ehci_dump_sqtd(sc, sqtd);
	}
	if (sqtd) {
		printf("dump aborted, too many TDs\n");
	}
}

static void
ehci_dump_sqh(ehci_softc_t *sc, ehci_qh_t *qh)
{
	uint32_t endp;
	uint32_t endphub;

	usb_pc_cpu_invalidate(qh->page_cache);
	printf("QH(%p) at 0x%08x:\n", qh, hc32toh(sc, qh->qh_self) & ~0x1F);
	printf("  link=");
	ehci_dump_link(sc, qh->qh_link, 1);
	printf("\n");
	endp = hc32toh(sc, qh->qh_endp);
	printf("  endp=0x%08x\n", endp);
	printf("    addr=0x%02x inact=%d endpt=%d eps=%d dtc=%d hrecl=%d\n",
	    EHCI_QH_GET_ADDR(endp), EHCI_QH_GET_INACT(endp),
	    EHCI_QH_GET_ENDPT(endp), EHCI_QH_GET_EPS(endp),
	    EHCI_QH_GET_DTC(endp), EHCI_QH_GET_HRECL(endp));
	printf("    mpl=0x%x ctl=%d nrl=%d\n",
	    EHCI_QH_GET_MPL(endp), EHCI_QH_GET_CTL(endp),
	    EHCI_QH_GET_NRL(endp));
	endphub = hc32toh(sc, qh->qh_endphub);
	printf("  endphub=0x%08x\n", endphub);
	printf("    smask=0x%02x cmask=0x%02x huba=0x%02x port=%d mult=%d\n",
	    EHCI_QH_GET_SMASK(endphub), EHCI_QH_GET_CMASK(endphub),
	    EHCI_QH_GET_HUBA(endphub), EHCI_QH_GET_PORT(endphub),
	    EHCI_QH_GET_MULT(endphub));
	printf("  curqtd=");
	ehci_dump_link(sc, qh->qh_curqtd, 0);
	printf("\n");
	printf("Overlay qTD:\n");
	ehci_dump_qtd(sc, (void *)&qh->qh_qtd);
}

static void
ehci_dump_sitd(ehci_softc_t *sc, ehci_sitd_t *sitd)
{
	usb_pc_cpu_invalidate(sitd->page_cache);
	printf("SITD(%p) at 0x%08x\n", sitd, hc32toh(sc, sitd->sitd_self) & ~0x1F);
	printf(" next=0x%08x\n", hc32toh(sc, sitd->sitd_next));
	printf(" portaddr=0x%08x dir=%s addr=%d endpt=0x%x port=0x%x huba=0x%x\n",
	    hc32toh(sc, sitd->sitd_portaddr),
	    (sitd->sitd_portaddr & htohc32(sc, EHCI_SITD_SET_DIR_IN))
	    ? "in" : "out",
	    EHCI_SITD_GET_ADDR(hc32toh(sc, sitd->sitd_portaddr)),
	    EHCI_SITD_GET_ENDPT(hc32toh(sc, sitd->sitd_portaddr)),
	    EHCI_SITD_GET_PORT(hc32toh(sc, sitd->sitd_portaddr)),
	    EHCI_SITD_GET_HUBA(hc32toh(sc, sitd->sitd_portaddr)));
	printf(" mask=0x%08x\n", hc32toh(sc, sitd->sitd_mask));
	printf(" status=0x%08x <%s> len=0x%x\n", hc32toh(sc, sitd->sitd_status),
	    (sitd->sitd_status & htohc32(sc, EHCI_SITD_ACTIVE)) ? "ACTIVE" : "",
	    EHCI_SITD_GET_LEN(hc32toh(sc, sitd->sitd_status)));
	printf(" back=0x%08x, bp=0x%08x,0x%08x,0x%08x,0x%08x\n",
	    hc32toh(sc, sitd->sitd_back),
	    hc32toh(sc, sitd->sitd_bp[0]),
	    hc32toh(sc, sitd->sitd_bp[1]),
	    hc32toh(sc, sitd->sitd_bp_hi[0]),
	    hc32toh(sc, sitd->sitd_bp_hi[1]));
}

static void
ehci_dump_itd(ehci_softc_t *sc, ehci_itd_t *itd)
{
	usb_pc_cpu_invalidate(itd->page_cache);
	printf("ITD(%p) at 0x%08x\n", itd, hc32toh(sc, itd->itd_self) & ~0x1F);
	printf(" next=0x%08x\n", hc32toh(sc, itd->itd_next));
	printf(" status[0]=0x%08x; <%s>\n", hc32toh(sc, itd->itd_status[0]),
	    (itd->itd_status[0] & htohc32(sc, EHCI_ITD_ACTIVE)) ? "ACTIVE" : "");
	printf(" status[1]=0x%08x; <%s>\n", hc32toh(sc, itd->itd_status[1]),
	    (itd->itd_status[1] & htohc32(sc, EHCI_ITD_ACTIVE)) ? "ACTIVE" : "");
	printf(" status[2]=0x%08x; <%s>\n", hc32toh(sc, itd->itd_status[2]),
	    (itd->itd_status[2] & htohc32(sc, EHCI_ITD_ACTIVE)) ? "ACTIVE" : "");
	printf(" status[3]=0x%08x; <%s>\n", hc32toh(sc, itd->itd_status[3]),
	    (itd->itd_status[3] & htohc32(sc, EHCI_ITD_ACTIVE)) ? "ACTIVE" : "");
	printf(" status[4]=0x%08x; <%s>\n", hc32toh(sc, itd->itd_status[4]),
	    (itd->itd_status[4] & htohc32(sc, EHCI_ITD_ACTIVE)) ? "ACTIVE" : "");
	printf(" status[5]=0x%08x; <%s>\n", hc32toh(sc, itd->itd_status[5]),
	    (itd->itd_status[5] & htohc32(sc, EHCI_ITD_ACTIVE)) ? "ACTIVE" : "");
	printf(" status[6]=0x%08x; <%s>\n", hc32toh(sc, itd->itd_status[6]),
	    (itd->itd_status[6] & htohc32(sc, EHCI_ITD_ACTIVE)) ? "ACTIVE" : "");
	printf(" status[7]=0x%08x; <%s>\n", hc32toh(sc, itd->itd_status[7]),
	    (itd->itd_status[7] & htohc32(sc, EHCI_ITD_ACTIVE)) ? "ACTIVE" : "");
	printf(" bp[0]=0x%08x\n", hc32toh(sc, itd->itd_bp[0]));
	printf("  addr=0x%02x; endpt=0x%01x\n",
	    EHCI_ITD_GET_ADDR(hc32toh(sc, itd->itd_bp[0])),
	    EHCI_ITD_GET_ENDPT(hc32toh(sc, itd->itd_bp[0])));
	printf(" bp[1]=0x%08x\n", hc32toh(sc, itd->itd_bp[1]));
	printf(" dir=%s; mpl=0x%02x\n",
	    (hc32toh(sc, itd->itd_bp[1]) & EHCI_ITD_SET_DIR_IN) ? "in" : "out",
	    EHCI_ITD_GET_MPL(hc32toh(sc, itd->itd_bp[1])));
	printf(" bp[2..6]=0x%08x,0x%08x,0x%08x,0x%08x,0x%08x\n",
	    hc32toh(sc, itd->itd_bp[2]),
	    hc32toh(sc, itd->itd_bp[3]),
	    hc32toh(sc, itd->itd_bp[4]),
	    hc32toh(sc, itd->itd_bp[5]),
	    hc32toh(sc, itd->itd_bp[6]));
	printf(" bp_hi=0x%08x,0x%08x,0x%08x,0x%08x,\n"
	    "       0x%08x,0x%08x,0x%08x\n",
	    hc32toh(sc, itd->itd_bp_hi[0]),
	    hc32toh(sc, itd->itd_bp_hi[1]),
	    hc32toh(sc, itd->itd_bp_hi[2]),
	    hc32toh(sc, itd->itd_bp_hi[3]),
	    hc32toh(sc, itd->itd_bp_hi[4]),
	    hc32toh(sc, itd->itd_bp_hi[5]),
	    hc32toh(sc, itd->itd_bp_hi[6]));
}

static void
ehci_dump_isoc(ehci_softc_t *sc)
{
	ehci_itd_t *itd;
	ehci_sitd_t *sitd;
	uint16_t max = 1000;
	uint16_t pos;

	pos = (EOREAD4(sc, EHCI_FRINDEX) / 8) &
	    (EHCI_VIRTUAL_FRAMELIST_COUNT - 1);

	printf("%s: isochronous dump from frame 0x%03x:\n",
	    __FUNCTION__, pos);

	itd = sc->sc_isoc_hs_p_last[pos];
	sitd = sc->sc_isoc_fs_p_last[pos];

	while (itd && max && max--) {
		ehci_dump_itd(sc, itd);
		itd = itd->prev;
	}

	while (sitd && max && max--) {
		ehci_dump_sitd(sc, sitd);
		sitd = sitd->prev;
	}
}

#endif

static void
ehci_transfer_intr_enqueue(struct usb_xfer *xfer)
{
	/* check for early completion */
	if (ehci_check_transfer(xfer)) {
		return;
	}
	/* put transfer on interrupt queue */
	usbd_transfer_enqueue(&xfer->xroot->bus->intr_q, xfer);

	/* start timeout, if any */
	if (xfer->timeout != 0) {
		usbd_transfer_timeout_ms(xfer, &ehci_timeout, xfer->timeout);
	}
}

#define	EHCI_APPEND_FS_TD(std,last) (last) = _ehci_append_fs_td(std,last)
static ehci_sitd_t *
_ehci_append_fs_td(ehci_sitd_t *std, ehci_sitd_t *last)
{
	DPRINTFN(11, "%p to %p\n", std, last);

	/* (sc->sc_bus.mtx) must be locked */

	std->next = last->next;
	std->sitd_next = last->sitd_next;

	std->prev = last;

	usb_pc_cpu_flush(std->page_cache);

	/*
	 * the last->next->prev is never followed: std->next->prev = std;
	 */
	last->next = std;
	last->sitd_next = std->sitd_self;

	usb_pc_cpu_flush(last->page_cache);

	return (std);
}

#define	EHCI_APPEND_HS_TD(std,last) (last) = _ehci_append_hs_td(std,last)
static ehci_itd_t *
_ehci_append_hs_td(ehci_itd_t *std, ehci_itd_t *last)
{
	DPRINTFN(11, "%p to %p\n", std, last);

	/* (sc->sc_bus.mtx) must be locked */

	std->next = last->next;
	std->itd_next = last->itd_next;

	std->prev = last;

	usb_pc_cpu_flush(std->page_cache);

	/*
	 * the last->next->prev is never followed: std->next->prev = std;
	 */
	last->next = std;
	last->itd_next = std->itd_self;

	usb_pc_cpu_flush(last->page_cache);

	return (std);
}

#define	EHCI_APPEND_QH(sqh,last) (last) = _ehci_append_qh(sqh,last)
static ehci_qh_t *
_ehci_append_qh(ehci_qh_t *sqh, ehci_qh_t *last)
{
	DPRINTFN(11, "%p to %p\n", sqh, last);

	if (sqh->prev != NULL) {
		/* should not happen */
		DPRINTFN(0, "QH already linked!\n");
		return (last);
	}
	/* (sc->sc_bus.mtx) must be locked */

	sqh->next = last->next;
	sqh->qh_link = last->qh_link;

	sqh->prev = last;

	usb_pc_cpu_flush(sqh->page_cache);

	/*
	 * the last->next->prev is never followed: sqh->next->prev = sqh;
	 */

	last->next = sqh;
	last->qh_link = sqh->qh_self;

	usb_pc_cpu_flush(last->page_cache);

	return (sqh);
}

#define	EHCI_REMOVE_FS_TD(std,last) (last) = _ehci_remove_fs_td(std,last)
static ehci_sitd_t *
_ehci_remove_fs_td(ehci_sitd_t *std, ehci_sitd_t *last)
{
	DPRINTFN(11, "%p from %p\n", std, last);

	/* (sc->sc_bus.mtx) must be locked */

	std->prev->next = std->next;
	std->prev->sitd_next = std->sitd_next;

	usb_pc_cpu_flush(std->prev->page_cache);

	if (std->next) {
		std->next->prev = std->prev;
		usb_pc_cpu_flush(std->next->page_cache);
	}
	return ((last == std) ? std->prev : last);
}

#define	EHCI_REMOVE_HS_TD(std,last) (last) = _ehci_remove_hs_td(std,last)
static ehci_itd_t *
_ehci_remove_hs_td(ehci_itd_t *std, ehci_itd_t *last)
{
	DPRINTFN(11, "%p from %p\n", std, last);

	/* (sc->sc_bus.mtx) must be locked */

	std->prev->next = std->next;
	std->prev->itd_next = std->itd_next;

	usb_pc_cpu_flush(std->prev->page_cache);

	if (std->next) {
		std->next->prev = std->prev;
		usb_pc_cpu_flush(std->next->page_cache);
	}
	return ((last == std) ? std->prev : last);
}

#define	EHCI_REMOVE_QH(sqh,last) (last) = _ehci_remove_qh(sqh,last)
static ehci_qh_t *
_ehci_remove_qh(ehci_qh_t *sqh, ehci_qh_t *last)
{
	DPRINTFN(11, "%p from %p\n", sqh, last);

	/* (sc->sc_bus.mtx) must be locked */

	/* only remove if not removed from a queue */
	if (sqh->prev) {

		sqh->prev->next = sqh->next;
		sqh->prev->qh_link = sqh->qh_link;

		usb_pc_cpu_flush(sqh->prev->page_cache);

		if (sqh->next) {
			sqh->next->prev = sqh->prev;
			usb_pc_cpu_flush(sqh->next->page_cache);
		}
		last = ((last == sqh) ? sqh->prev : last);

		sqh->prev = 0;

		usb_pc_cpu_flush(sqh->page_cache);
	}
	return (last);
}

static void
ehci_data_toggle_update(struct usb_xfer *xfer, uint16_t actlen, uint16_t xlen)
{
	uint16_t rem;
	uint8_t dt;

	/* count number of full packets */
	dt = (actlen / xfer->max_packet_size) & 1;

	/* compute remainder */
	rem = actlen % xfer->max_packet_size;

	if (rem > 0)
		dt ^= 1;	/* short packet at the end */
	else if (actlen != xlen)
		dt ^= 1;	/* zero length packet at the end */
	else if (xlen == 0)
		dt ^= 1;	/* zero length transfer */

	xfer->endpoint->toggle_next ^= dt;
}

static usb_error_t
ehci_non_isoc_done_sub(struct usb_xfer *xfer)
{
	ehci_softc_t *sc = EHCI_BUS2SC(xfer->xroot->bus);
	ehci_qtd_t *td;
	ehci_qtd_t *td_alt_next;
	uint32_t status;
	uint16_t len;

	td = xfer->td_transfer_cache;
	td_alt_next = td->alt_next;

	if (xfer->aframes != xfer->nframes) {
		usbd_xfer_set_frame_len(xfer, xfer->aframes, 0);
	}
	while (1) {

		usb_pc_cpu_invalidate(td->page_cache);
		status = hc32toh(sc, td->qtd_status);

		len = EHCI_QTD_GET_BYTES(status);

		/*
	         * Verify the status length and
		 * add the length to "frlengths[]":
	         */
		if (len > td->len) {
			/* should not happen */
			DPRINTF("Invalid status length, "
			    "0x%04x/0x%04x bytes\n", len, td->len);
			status |= EHCI_QTD_HALTED;
		} else if (xfer->aframes != xfer->nframes) {
			xfer->frlengths[xfer->aframes] += td->len - len;
			/* manually update data toggle */
			ehci_data_toggle_update(xfer, td->len - len, td->len);
		}

		/* Check for last transfer */
		if (((void *)td) == xfer->td_transfer_last) {
			td = NULL;
			break;
		}
		/* Check for transfer error */
		if (status & EHCI_QTD_HALTED) {
			/* the transfer is finished */
			td = NULL;
			break;
		}
		/* Check for short transfer */
		if (len > 0) {
			if (xfer->flags_int.short_frames_ok) {
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

#ifdef USB_DEBUG
	if (status & EHCI_QTD_STATERRS) {
		DPRINTFN(11, "error, addr=%d, endpt=0x%02x, frame=0x%02x"
		    "status=%s%s%s%s%s%s%s%s\n",
		    xfer->address, xfer->endpointno, xfer->aframes,
		    (status & EHCI_QTD_ACTIVE) ? "[ACTIVE]" : "[NOT_ACTIVE]",
		    (status & EHCI_QTD_HALTED) ? "[HALTED]" : "",
		    (status & EHCI_QTD_BUFERR) ? "[BUFERR]" : "",
		    (status & EHCI_QTD_BABBLE) ? "[BABBLE]" : "",
		    (status & EHCI_QTD_XACTERR) ? "[XACTERR]" : "",
		    (status & EHCI_QTD_MISSEDMICRO) ? "[MISSED]" : "",
		    (status & EHCI_QTD_SPLITXSTATE) ? "[SPLIT]" : "",
		    (status & EHCI_QTD_PINGSTATE) ? "[PING]" : "");
	}
#endif
	if (status & EHCI_QTD_HALTED) {
		if ((xfer->xroot->udev->parent_hs_hub != NULL) ||
		    (xfer->xroot->udev->address != 0)) {
			/* try to separate I/O errors from STALL */
			if (EHCI_QTD_GET_CERR(status) == 0)
				return (USB_ERR_IOERROR);
		}
		return (USB_ERR_STALLED);
	}
	return (USB_ERR_NORMAL_COMPLETION);
}

static void
ehci_non_isoc_done(struct usb_xfer *xfer)
{
	ehci_qh_t *qh;
	usb_error_t err = 0;

	DPRINTFN(13, "xfer=%p endpoint=%p transfer done\n",
	    xfer, xfer->endpoint);

#ifdef USB_DEBUG
	if (ehcidebug > 10) {
		ehci_softc_t *sc = EHCI_BUS2SC(xfer->xroot->bus);

		ehci_dump_sqtds(sc, xfer->td_transfer_first);
	}
#endif

	/* extract data toggle directly from the QH's overlay area */

	qh = xfer->qh_start[xfer->flags_int.curr_dma_set];

	usb_pc_cpu_invalidate(qh->page_cache);

	/* reset scanner */

	xfer->td_transfer_cache = xfer->td_transfer_first;

	if (xfer->flags_int.control_xfr) {

		if (xfer->flags_int.control_hdr) {

			err = ehci_non_isoc_done_sub(xfer);
		}
		xfer->aframes = 1;

		if (xfer->td_transfer_cache == NULL) {
			goto done;
		}
	}
	while (xfer->aframes != xfer->nframes) {

		err = ehci_non_isoc_done_sub(xfer);
		xfer->aframes++;

		if (xfer->td_transfer_cache == NULL) {
			goto done;
		}
	}

	if (xfer->flags_int.control_xfr &&
	    !xfer->flags_int.control_act) {

		err = ehci_non_isoc_done_sub(xfer);
	}
done:
	ehci_device_done(xfer, err);
}

/*------------------------------------------------------------------------*
 *	ehci_check_transfer
 *
 * Return values:
 *    0: USB transfer is not finished
 * Else: USB transfer is finished
 *------------------------------------------------------------------------*/
static uint8_t
ehci_check_transfer(struct usb_xfer *xfer)
{
	const struct usb_pipe_methods *methods = xfer->endpoint->methods;
	ehci_softc_t *sc = EHCI_BUS2SC(xfer->xroot->bus);

	uint32_t status;

	DPRINTFN(13, "xfer=%p checking transfer\n", xfer);

	if (methods == &ehci_device_isoc_fs_methods) {
		ehci_sitd_t *td;

		/* isochronous full speed transfer */

		td = xfer->td_transfer_last;
		usb_pc_cpu_invalidate(td->page_cache);
		status = hc32toh(sc, td->sitd_status);

		/* also check if first is complete */

		td = xfer->td_transfer_first;
		usb_pc_cpu_invalidate(td->page_cache);
		status |= hc32toh(sc, td->sitd_status);

		if (!(status & EHCI_SITD_ACTIVE)) {
			ehci_device_done(xfer, USB_ERR_NORMAL_COMPLETION);
			goto transferred;
		}
	} else if (methods == &ehci_device_isoc_hs_methods) {
		ehci_itd_t *td;

		/* isochronous high speed transfer */

		/* check last transfer */
		td = xfer->td_transfer_last;
		usb_pc_cpu_invalidate(td->page_cache);
		status = td->itd_status[0];
		status |= td->itd_status[1];
		status |= td->itd_status[2];
		status |= td->itd_status[3];
		status |= td->itd_status[4];
		status |= td->itd_status[5];
		status |= td->itd_status[6];
		status |= td->itd_status[7];

		/* also check first transfer */
		td = xfer->td_transfer_first;
		usb_pc_cpu_invalidate(td->page_cache);
		status |= td->itd_status[0];
		status |= td->itd_status[1];
		status |= td->itd_status[2];
		status |= td->itd_status[3];
		status |= td->itd_status[4];
		status |= td->itd_status[5];
		status |= td->itd_status[6];
		status |= td->itd_status[7];

		/* if no transactions are active we continue */
		if (!(status & htohc32(sc, EHCI_ITD_ACTIVE))) {
			ehci_device_done(xfer, USB_ERR_NORMAL_COMPLETION);
			goto transferred;
		}
	} else {
		ehci_qtd_t *td;
		ehci_qh_t *qh;

		/* non-isochronous transfer */

		/*
		 * check whether there is an error somewhere in the middle,
		 * or whether there was a short packet (SPD and not ACTIVE)
		 */
		td = xfer->td_transfer_cache;

		qh = xfer->qh_start[xfer->flags_int.curr_dma_set];

		usb_pc_cpu_invalidate(qh->page_cache);

		status = hc32toh(sc, qh->qh_qtd.qtd_status);
		if (status & EHCI_QTD_ACTIVE) {
			/* transfer is pending */
			goto done;
		}

		while (1) {
			usb_pc_cpu_invalidate(td->page_cache);
			status = hc32toh(sc, td->qtd_status);

			/*
			 * Check if there is an active TD which
			 * indicates that the transfer isn't done.
			 */
			if (status & EHCI_QTD_ACTIVE) {
				/* update cache */
				xfer->td_transfer_cache = td;
				goto done;
			}
			/*
			 * last transfer descriptor makes the transfer done
			 */
			if (((void *)td) == xfer->td_transfer_last) {
				break;
			}
			/*
			 * any kind of error makes the transfer done
			 */
			if (status & EHCI_QTD_HALTED) {
				break;
			}
			/*
			 * if there is no alternate next transfer, a short
			 * packet also makes the transfer done
			 */
			if (EHCI_QTD_GET_BYTES(status)) {
				if (xfer->flags_int.short_frames_ok) {
					/* follow alt next */
					if (td->alt_next) {
						td = td->alt_next;
						continue;
					}
				}
				/* transfer is done */
				break;
			}
			td = td->obj_next;
		}
		ehci_non_isoc_done(xfer);
		goto transferred;
	}

done:
	DPRINTFN(13, "xfer=%p is still active\n", xfer);
	return (0);

transferred:
	return (1);
}

static void
ehci_pcd_enable(ehci_softc_t *sc)
{
	USB_BUS_LOCK_ASSERT(&sc->sc_bus, MA_OWNED);

	sc->sc_eintrs |= EHCI_STS_PCD;
	EOWRITE4(sc, EHCI_USBINTR, sc->sc_eintrs);

	/* acknowledge any PCD interrupt */
	EOWRITE4(sc, EHCI_USBSTS, EHCI_STS_PCD);

	ehci_root_intr(sc);
}

static void
ehci_interrupt_poll(ehci_softc_t *sc)
{
	struct usb_xfer *xfer;

repeat:
	TAILQ_FOREACH(xfer, &sc->sc_bus.intr_q.head, wait_entry) {
		/*
		 * check if transfer is transferred
		 */
		if (ehci_check_transfer(xfer)) {
			/* queue has been modified */
			goto repeat;
		}
	}
}

/*
 * Some EHCI chips from VIA / ATI seem to trigger interrupts before
 * writing back the qTD status, or miss signalling occasionally under
 * heavy load.  If the host machine is too fast, we can miss
 * transaction completion - when we scan the active list the
 * transaction still seems to be active. This generally exhibits
 * itself as a umass stall that never recovers.
 *
 * We work around this behaviour by setting up this callback after any
 * softintr that completes with transactions still pending, giving us
 * another chance to check for completion after the writeback has
 * taken place.
 */
static void
ehci_poll_timeout(void *arg)
{
	ehci_softc_t *sc = arg;

	DPRINTFN(3, "\n");
	ehci_interrupt_poll(sc);
}

/*------------------------------------------------------------------------*
 *	ehci_interrupt - EHCI interrupt handler
 *
 * NOTE: Do not access "sc->sc_bus.bdev" inside the interrupt handler,
 * hence the interrupt handler will be setup before "sc->sc_bus.bdev"
 * is present !
 *------------------------------------------------------------------------*/
void
ehci_interrupt(ehci_softc_t *sc)
{
	uint32_t status;

	USB_BUS_LOCK(&sc->sc_bus);

	DPRINTFN(16, "real interrupt\n");

#ifdef USB_DEBUG
	if (ehcidebug > 15) {
		ehci_dump_regs(sc);
	}
#endif

	status = EHCI_STS_INTRS(EOREAD4(sc, EHCI_USBSTS));
	if (status == 0) {
		/* the interrupt was not for us */
		goto done;
	}
	if (!(status & sc->sc_eintrs)) {
		goto done;
	}
	EOWRITE4(sc, EHCI_USBSTS, status);	/* acknowledge */

	status &= sc->sc_eintrs;

	if (status & EHCI_STS_HSE) {
		printf("%s: unrecoverable error, "
		    "controller halted\n", __FUNCTION__);
#ifdef USB_DEBUG
		ehci_dump_regs(sc);
		ehci_dump_isoc(sc);
#endif
	}
	if (status & EHCI_STS_PCD) {
		/*
		 * Disable PCD interrupt for now, because it will be
		 * on until the port has been reset.
		 */
		sc->sc_eintrs &= ~EHCI_STS_PCD;
		EOWRITE4(sc, EHCI_USBINTR, sc->sc_eintrs);

		ehci_root_intr(sc);

		/* do not allow RHSC interrupts > 1 per second */
		usb_callout_reset(&sc->sc_tmo_pcd, hz,
		    (void *)&ehci_pcd_enable, sc);
	}
	status &= ~(EHCI_STS_INT | EHCI_STS_ERRINT | EHCI_STS_PCD | EHCI_STS_IAA);

	if (status != 0) {
		/* block unprocessed interrupts */
		sc->sc_eintrs &= ~status;
		EOWRITE4(sc, EHCI_USBINTR, sc->sc_eintrs);
		printf("%s: blocking interrupts 0x%x\n", __FUNCTION__, status);
	}
	/* poll all the USB transfers */
	ehci_interrupt_poll(sc);

	if (sc->sc_flags & EHCI_SCFLG_LOSTINTRBUG) {
		usb_callout_reset(&sc->sc_tmo_poll, hz / 128,
		    (void *)&ehci_poll_timeout, sc);
	}

done:
	USB_BUS_UNLOCK(&sc->sc_bus);
}

/*
 * called when a request does not complete
 */
static void
ehci_timeout(void *arg)
{
	struct usb_xfer *xfer = arg;

	DPRINTF("xfer=%p\n", xfer);

	USB_BUS_LOCK_ASSERT(xfer->xroot->bus, MA_OWNED);

	/* transfer is transferred */
	ehci_device_done(xfer, USB_ERR_TIMEOUT);
}

static void
ehci_do_poll(struct usb_bus *bus)
{
	ehci_softc_t *sc = EHCI_BUS2SC(bus);

	USB_BUS_LOCK(&sc->sc_bus);
	ehci_interrupt_poll(sc);
	USB_BUS_UNLOCK(&sc->sc_bus);
}

static void
ehci_setup_standard_chain_sub(struct ehci_std_temp *temp)
{
	struct usb_page_search buf_res;
	ehci_qtd_t *td;
	ehci_qtd_t *td_next;
	ehci_qtd_t *td_alt_next;
	uint32_t buf_offset;
	uint32_t average;
	uint32_t len_old;
	uint32_t terminate;
	uint32_t qtd_altnext;
	uint8_t shortpkt_old;
	uint8_t precompute;

	terminate = temp->sc->sc_terminate_self;
	qtd_altnext = temp->sc->sc_terminate_self;
	td_alt_next = NULL;
	buf_offset = 0;
	shortpkt_old = temp->shortpkt;
	len_old = temp->len;
	precompute = 1;

restart:

	td = temp->td;
	td_next = temp->td_next;

	while (1) {

		if (temp->len == 0) {

			if (temp->shortpkt) {
				break;
			}
			/* send a Zero Length Packet, ZLP, last */

			temp->shortpkt = 1;
			average = 0;

		} else {

			average = temp->average;

			if (temp->len < average) {
				if (temp->len % temp->max_frame_size) {
					temp->shortpkt = 1;
				}
				average = temp->len;
			}
		}

		if (td_next == NULL) {
			panic("%s: out of EHCI transfer descriptors!", __FUNCTION__);
		}
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

		td->qtd_status =
		    temp->qtd_status |
		    htohc32(temp->sc, EHCI_QTD_IOC |
			EHCI_QTD_SET_BYTES(average));

		if (average == 0) {

			if (temp->auto_data_toggle == 0) {

				/* update data toggle, ZLP case */

				temp->qtd_status ^=
				    htohc32(temp->sc, EHCI_QTD_TOGGLE_MASK);
			}
			td->len = 0;

			/* properly reset reserved fields */
			td->qtd_buffer[0] = 0;
			td->qtd_buffer[1] = 0;
			td->qtd_buffer[2] = 0;
			td->qtd_buffer[3] = 0;
			td->qtd_buffer[4] = 0;
			td->qtd_buffer_hi[0] = 0;
			td->qtd_buffer_hi[1] = 0;
			td->qtd_buffer_hi[2] = 0;
			td->qtd_buffer_hi[3] = 0;
			td->qtd_buffer_hi[4] = 0;
		} else {

			uint8_t x;

			if (temp->auto_data_toggle == 0) {

				/* update data toggle */

				if (howmany(average, temp->max_frame_size) & 1) {
					temp->qtd_status ^=
					    htohc32(temp->sc, EHCI_QTD_TOGGLE_MASK);
				}
			}
			td->len = average;

			/* update remaining length */

			temp->len -= average;

			/* fill out buffer pointers */

			usbd_get_page(temp->pc, buf_offset, &buf_res);
			td->qtd_buffer[0] =
			    htohc32(temp->sc, buf_res.physaddr);
			td->qtd_buffer_hi[0] = 0;

			x = 1;

			while (average > EHCI_PAGE_SIZE) {
				average -= EHCI_PAGE_SIZE;
				buf_offset += EHCI_PAGE_SIZE;
				usbd_get_page(temp->pc, buf_offset, &buf_res);
				td->qtd_buffer[x] =
				    htohc32(temp->sc,
				    buf_res.physaddr & (~0xFFF));
				td->qtd_buffer_hi[x] = 0;
				x++;
			}

			/*
			 * NOTE: The "average" variable is never zero after
			 * exiting the loop above !
			 *
			 * NOTE: We have to subtract one from the offset to
			 * ensure that we are computing the physical address
			 * of a valid page !
			 */
			buf_offset += average;
			usbd_get_page(temp->pc, buf_offset - 1, &buf_res);
			td->qtd_buffer[x] =
			    htohc32(temp->sc,
			    buf_res.physaddr & (~0xFFF));
			td->qtd_buffer_hi[x] = 0;

			/* properly reset reserved fields */
			while (++x < EHCI_QTD_NBUFFERS) {
				td->qtd_buffer[x] = 0;
				td->qtd_buffer_hi[x] = 0;
			}
		}

		if (td_next) {
			/* link the current TD with the next one */
			td->qtd_next = td_next->qtd_self;
		}
		td->qtd_altnext = qtd_altnext;
		td->alt_next = td_alt_next;

		usb_pc_cpu_flush(td->page_cache);
	}

	if (precompute) {
		precompute = 0;

		/* setup alt next pointer, if any */
		if (temp->last_frame) {
			td_alt_next = NULL;
			qtd_altnext = terminate;
		} else {
			/* we use this field internally */
			td_alt_next = td_next;
			if (temp->setup_alt_next) {
				qtd_altnext = td_next->qtd_self;
			} else {
				qtd_altnext = terminate;
			}
		}

		/* restore */
		temp->shortpkt = shortpkt_old;
		temp->len = len_old;
		goto restart;
	}
	temp->td = td;
	temp->td_next = td_next;
}

static void
ehci_setup_standard_chain(struct usb_xfer *xfer, ehci_qh_t **qh_last)
{
	struct ehci_std_temp temp;
	const struct usb_pipe_methods *methods;
	ehci_qh_t *qh;
	ehci_qtd_t *td;
	uint32_t qh_endp;
	uint32_t qh_endphub;
	uint32_t x;

	DPRINTFN(9, "addr=%d endpt=%d sumlen=%d speed=%d\n",
	    xfer->address, UE_GET_ADDR(xfer->endpointno),
	    xfer->sumlen, usbd_get_speed(xfer->xroot->udev));

	temp.average = xfer->max_hc_frame_size;
	temp.max_frame_size = xfer->max_frame_size;
	temp.sc = EHCI_BUS2SC(xfer->xroot->bus);

	/* toggle the DMA set we are using */
	xfer->flags_int.curr_dma_set ^= 1;

	/* get next DMA set */
	td = xfer->td_start[xfer->flags_int.curr_dma_set];

	xfer->td_transfer_first = td;
	xfer->td_transfer_cache = td;

	temp.td = NULL;
	temp.td_next = td;
	temp.qtd_status = 0;
	temp.last_frame = 0;
	temp.setup_alt_next = xfer->flags_int.short_frames_ok;

	if (xfer->flags_int.control_xfr) {
		if (xfer->endpoint->toggle_next) {
			/* DATA1 is next */
			temp.qtd_status |=
			    htohc32(temp.sc, EHCI_QTD_SET_TOGGLE(1));
		}
		temp.auto_data_toggle = 0;
	} else {
		temp.auto_data_toggle = 1;
	}

	if ((xfer->xroot->udev->parent_hs_hub != NULL) ||
	    (xfer->xroot->udev->address != 0)) {
		/* max 3 retries */
		temp.qtd_status |=
		    htohc32(temp.sc, EHCI_QTD_SET_CERR(3));
	}
	/* check if we should prepend a setup message */

	if (xfer->flags_int.control_xfr) {
		if (xfer->flags_int.control_hdr) {

			xfer->endpoint->toggle_next = 0;

			temp.qtd_status &=
			    htohc32(temp.sc, EHCI_QTD_SET_CERR(3));
			temp.qtd_status |= htohc32(temp.sc,
			    EHCI_QTD_ACTIVE |
			    EHCI_QTD_SET_PID(EHCI_QTD_PID_SETUP) |
			    EHCI_QTD_SET_TOGGLE(0));

			temp.len = xfer->frlengths[0];
			temp.pc = xfer->frbuffers + 0;
			temp.shortpkt = temp.len ? 1 : 0;
			/* check for last frame */
			if (xfer->nframes == 1) {
				/* no STATUS stage yet, SETUP is last */
				if (xfer->flags_int.control_act) {
					temp.last_frame = 1;
					temp.setup_alt_next = 0;
				}
			}
			ehci_setup_standard_chain_sub(&temp);
		}
		x = 1;
	} else {
		x = 0;
	}

	while (x != xfer->nframes) {

		/* DATA0 / DATA1 message */

		temp.len = xfer->frlengths[x];
		temp.pc = xfer->frbuffers + x;

		x++;

		if (x == xfer->nframes) {
			if (xfer->flags_int.control_xfr) {
				/* no STATUS stage yet, DATA is last */
				if (xfer->flags_int.control_act) {
					temp.last_frame = 1;
					temp.setup_alt_next = 0;
				}
			} else {
				temp.last_frame = 1;
				temp.setup_alt_next = 0;
			}
		}
		/* keep previous data toggle and error count */

		temp.qtd_status &=
		    htohc32(temp.sc, EHCI_QTD_SET_CERR(3) |
		    EHCI_QTD_SET_TOGGLE(1));

		if (temp.len == 0) {

			/* make sure that we send an USB packet */

			temp.shortpkt = 0;

		} else {

			/* regular data transfer */

			temp.shortpkt = (xfer->flags.force_short_xfer) ? 0 : 1;
		}

		/* set endpoint direction */

		temp.qtd_status |=
		    (UE_GET_DIR(xfer->endpointno) == UE_DIR_IN) ?
		    htohc32(temp.sc, EHCI_QTD_ACTIVE |
		    EHCI_QTD_SET_PID(EHCI_QTD_PID_IN)) :
		    htohc32(temp.sc, EHCI_QTD_ACTIVE |
		    EHCI_QTD_SET_PID(EHCI_QTD_PID_OUT));

		ehci_setup_standard_chain_sub(&temp);
	}

	/* check if we should append a status stage */

	if (xfer->flags_int.control_xfr &&
	    !xfer->flags_int.control_act) {

		/*
		 * Send a DATA1 message and invert the current endpoint
		 * direction.
		 */

		temp.qtd_status &= htohc32(temp.sc, EHCI_QTD_SET_CERR(3) |
		    EHCI_QTD_SET_TOGGLE(1));
		temp.qtd_status |=
		    (UE_GET_DIR(xfer->endpointno) == UE_DIR_OUT) ?
		    htohc32(temp.sc, EHCI_QTD_ACTIVE |
		    EHCI_QTD_SET_PID(EHCI_QTD_PID_IN) |
		    EHCI_QTD_SET_TOGGLE(1)) :
		    htohc32(temp.sc, EHCI_QTD_ACTIVE |
		    EHCI_QTD_SET_PID(EHCI_QTD_PID_OUT) |
		    EHCI_QTD_SET_TOGGLE(1));

		temp.len = 0;
		temp.pc = NULL;
		temp.shortpkt = 0;
		temp.last_frame = 1;
		temp.setup_alt_next = 0;

		ehci_setup_standard_chain_sub(&temp);
	}
	td = temp.td;

	/* the last TD terminates the transfer: */
	td->qtd_next = htohc32(temp.sc, EHCI_LINK_TERMINATE);
	td->qtd_altnext = htohc32(temp.sc, EHCI_LINK_TERMINATE);

	usb_pc_cpu_flush(td->page_cache);

	/* must have at least one frame! */

	xfer->td_transfer_last = td;

#ifdef USB_DEBUG
	if (ehcidebug > 8) {
		DPRINTF("nexttog=%d; data before transfer:\n",
		    xfer->endpoint->toggle_next);
		ehci_dump_sqtds(temp.sc,
		    xfer->td_transfer_first);
	}
#endif

	methods = xfer->endpoint->methods;

	qh = xfer->qh_start[xfer->flags_int.curr_dma_set];

	/* the "qh_link" field is filled when the QH is added */

	qh_endp =
	    (EHCI_QH_SET_ADDR(xfer->address) |
	    EHCI_QH_SET_ENDPT(UE_GET_ADDR(xfer->endpointno)) |
	    EHCI_QH_SET_MPL(xfer->max_packet_size));

	if (usbd_get_speed(xfer->xroot->udev) == USB_SPEED_HIGH) {
		qh_endp |= EHCI_QH_SET_EPS(EHCI_QH_SPEED_HIGH);
		if (methods != &ehci_device_intr_methods)
			qh_endp |= EHCI_QH_SET_NRL(8);
	} else {

		if (usbd_get_speed(xfer->xroot->udev) == USB_SPEED_FULL) {
			qh_endp |= EHCI_QH_SET_EPS(EHCI_QH_SPEED_FULL);
		} else {
			qh_endp |= EHCI_QH_SET_EPS(EHCI_QH_SPEED_LOW);
		}

		if (methods == &ehci_device_ctrl_methods) {
			qh_endp |= EHCI_QH_CTL;
		}
		if (methods != &ehci_device_intr_methods) {
			/* Only try one time per microframe! */
			qh_endp |= EHCI_QH_SET_NRL(1);
		}
	}

	if (temp.auto_data_toggle == 0) {
		/* software computes the data toggle */
		qh_endp |= EHCI_QH_DTC;
	}

	qh->qh_endp = htohc32(temp.sc, qh_endp);

	qh_endphub =
	    (EHCI_QH_SET_MULT(xfer->max_packet_count & 3) |
	    EHCI_QH_SET_CMASK(xfer->endpoint->usb_cmask) |
	    EHCI_QH_SET_SMASK(xfer->endpoint->usb_smask) |
	    EHCI_QH_SET_HUBA(xfer->xroot->udev->hs_hub_addr) |
	    EHCI_QH_SET_PORT(xfer->xroot->udev->hs_port_no));

	qh->qh_endphub = htohc32(temp.sc, qh_endphub);
	qh->qh_curqtd = 0;

	/* fill the overlay qTD */

	if (temp.auto_data_toggle && xfer->endpoint->toggle_next) {
		/* DATA1 is next */
		qh->qh_qtd.qtd_status = htohc32(temp.sc, EHCI_QTD_SET_TOGGLE(1));
	} else {
		qh->qh_qtd.qtd_status = 0;
	}

	td = xfer->td_transfer_first;

	qh->qh_qtd.qtd_next = td->qtd_self;
	qh->qh_qtd.qtd_altnext =
	    htohc32(temp.sc, EHCI_LINK_TERMINATE);

	/* properly reset reserved fields */
	qh->qh_qtd.qtd_buffer[0] = 0;
	qh->qh_qtd.qtd_buffer[1] = 0;
	qh->qh_qtd.qtd_buffer[2] = 0;
	qh->qh_qtd.qtd_buffer[3] = 0;
	qh->qh_qtd.qtd_buffer[4] = 0;
	qh->qh_qtd.qtd_buffer_hi[0] = 0;
	qh->qh_qtd.qtd_buffer_hi[1] = 0;
	qh->qh_qtd.qtd_buffer_hi[2] = 0;
	qh->qh_qtd.qtd_buffer_hi[3] = 0;
	qh->qh_qtd.qtd_buffer_hi[4] = 0;

	usb_pc_cpu_flush(qh->page_cache);

	if (xfer->xroot->udev->flags.self_suspended == 0) {
		EHCI_APPEND_QH(qh, *qh_last);
	}
}

static void
ehci_root_intr(ehci_softc_t *sc)
{
	uint16_t i;
	uint16_t m;

	USB_BUS_LOCK_ASSERT(&sc->sc_bus, MA_OWNED);

	/* clear any old interrupt data */
	memset(sc->sc_hub_idata, 0, sizeof(sc->sc_hub_idata));

	/* set bits */
	m = (sc->sc_noport + 1);
	if (m > (8 * sizeof(sc->sc_hub_idata))) {
		m = (8 * sizeof(sc->sc_hub_idata));
	}
	for (i = 1; i < m; i++) {
		/* pick out CHANGE bits from the status register */
		if (EOREAD4(sc, EHCI_PORTSC(i)) & EHCI_PS_CLEAR) {
			sc->sc_hub_idata[i / 8] |= 1 << (i % 8);
			DPRINTF("port %d changed\n", i);
		}
	}
	uhub_root_intr(&sc->sc_bus, sc->sc_hub_idata,
	    sizeof(sc->sc_hub_idata));
}

static void
ehci_isoc_fs_done(ehci_softc_t *sc, struct usb_xfer *xfer)
{
	uint32_t nframes = xfer->nframes;
	uint32_t status;
	uint32_t *plen = xfer->frlengths;
	uint16_t len = 0;
	ehci_sitd_t *td = xfer->td_transfer_first;
	ehci_sitd_t **pp_last = &sc->sc_isoc_fs_p_last[xfer->qh_pos];

	DPRINTFN(13, "xfer=%p endpoint=%p transfer done\n",
	    xfer, xfer->endpoint);

	while (nframes--) {
		if (td == NULL) {
			panic("%s:%d: out of TD's\n",
			    __FUNCTION__, __LINE__);
		}
		if (pp_last >= &sc->sc_isoc_fs_p_last[EHCI_VIRTUAL_FRAMELIST_COUNT]) {
			pp_last = &sc->sc_isoc_fs_p_last[0];
		}
#ifdef USB_DEBUG
		if (ehcidebug > 15) {
			DPRINTF("isoc FS-TD\n");
			ehci_dump_sitd(sc, td);
		}
#endif
		usb_pc_cpu_invalidate(td->page_cache);
		status = hc32toh(sc, td->sitd_status);

		len = EHCI_SITD_GET_LEN(status);

		DPRINTFN(2, "status=0x%08x, rem=%u\n", status, len);

		if (*plen >= len) {
			len = *plen - len;
		} else {
			len = 0;
		}

		*plen = len;

		/* remove FS-TD from schedule */
		EHCI_REMOVE_FS_TD(td, *pp_last);

		pp_last++;
		plen++;
		td = td->obj_next;
	}

	xfer->aframes = xfer->nframes;
}

static void
ehci_isoc_hs_done(ehci_softc_t *sc, struct usb_xfer *xfer)
{
	uint32_t nframes = xfer->nframes;
	uint32_t status;
	uint32_t *plen = xfer->frlengths;
	uint16_t len = 0;
	uint8_t td_no = 0;
	ehci_itd_t *td = xfer->td_transfer_first;
	ehci_itd_t **pp_last = &sc->sc_isoc_hs_p_last[xfer->qh_pos];

	DPRINTFN(13, "xfer=%p endpoint=%p transfer done\n",
	    xfer, xfer->endpoint);

	while (nframes) {
		if (td == NULL) {
			panic("%s:%d: out of TD's\n",
			    __FUNCTION__, __LINE__);
		}
		if (pp_last >= &sc->sc_isoc_hs_p_last[EHCI_VIRTUAL_FRAMELIST_COUNT]) {
			pp_last = &sc->sc_isoc_hs_p_last[0];
		}
#ifdef USB_DEBUG
		if (ehcidebug > 15) {
			DPRINTF("isoc HS-TD\n");
			ehci_dump_itd(sc, td);
		}
#endif

		usb_pc_cpu_invalidate(td->page_cache);
		status = hc32toh(sc, td->itd_status[td_no]);

		len = EHCI_ITD_GET_LEN(status);

		DPRINTFN(2, "status=0x%08x, len=%u\n", status, len);

		if (xfer->endpoint->usb_smask & (1 << td_no)) {

			if (*plen >= len) {
				/*
				 * The length is valid. NOTE: The
				 * complete length is written back
				 * into the status field, and not the
				 * remainder like with other transfer
				 * descriptor types.
				 */
			} else {
				/* Invalid length - truncate */
				len = 0;
			}

			*plen = len;
			plen++;
			nframes--;
		}

		td_no++;

		if ((td_no == 8) || (nframes == 0)) {
			/* remove HS-TD from schedule */
			EHCI_REMOVE_HS_TD(td, *pp_last);
			pp_last++;

			td_no = 0;
			td = td->obj_next;
		}
	}
	xfer->aframes = xfer->nframes;
}

/* NOTE: "done" can be run two times in a row,
 * from close and from interrupt
 */
static void
ehci_device_done(struct usb_xfer *xfer, usb_error_t error)
{
	const struct usb_pipe_methods *methods = xfer->endpoint->methods;
	ehci_softc_t *sc = EHCI_BUS2SC(xfer->xroot->bus);

	USB_BUS_LOCK_ASSERT(&sc->sc_bus, MA_OWNED);

	DPRINTFN(2, "xfer=%p, endpoint=%p, error=%d\n",
	    xfer, xfer->endpoint, error);

	if ((methods == &ehci_device_bulk_methods) ||
	    (methods == &ehci_device_ctrl_methods)) {
#ifdef USB_DEBUG
		if (ehcidebug > 8) {
			DPRINTF("nexttog=%d; data after transfer:\n",
			    xfer->endpoint->toggle_next);
			ehci_dump_sqtds(sc,
			    xfer->td_transfer_first);
		}
#endif

		EHCI_REMOVE_QH(xfer->qh_start[xfer->flags_int.curr_dma_set],
		    sc->sc_async_p_last);
	}
	if (methods == &ehci_device_intr_methods) {
		EHCI_REMOVE_QH(xfer->qh_start[xfer->flags_int.curr_dma_set],
		    sc->sc_intr_p_last[xfer->qh_pos]);
	}
	/*
	 * Only finish isochronous transfers once which will update
	 * "xfer->frlengths".
	 */
	if (xfer->td_transfer_first &&
	    xfer->td_transfer_last) {
		if (methods == &ehci_device_isoc_fs_methods) {
			ehci_isoc_fs_done(sc, xfer);
		}
		if (methods == &ehci_device_isoc_hs_methods) {
			ehci_isoc_hs_done(sc, xfer);
		}
		xfer->td_transfer_first = NULL;
		xfer->td_transfer_last = NULL;
	}
	/* dequeue transfer and start next transfer */
	usbd_transfer_done(xfer, error);
}

/*------------------------------------------------------------------------*
 * ehci bulk support
 *------------------------------------------------------------------------*/
static void
ehci_device_bulk_open(struct usb_xfer *xfer)
{
	return;
}

static void
ehci_device_bulk_close(struct usb_xfer *xfer)
{
	ehci_device_done(xfer, USB_ERR_CANCELLED);
}

static void
ehci_device_bulk_enter(struct usb_xfer *xfer)
{
	return;
}

static void
ehci_doorbell_async(struct ehci_softc *sc)
{
	uint32_t temp;

	/*
	 * XXX Performance quirk: Some Host Controllers have a too low
	 * interrupt rate. Issue an IAAD to stimulate the Host
	 * Controller after queueing the BULK transfer.
	 *
	 * XXX Force the host controller to refresh any QH caches.
	 */
	temp = EOREAD4(sc, EHCI_USBCMD);
	if (!(temp & EHCI_CMD_IAAD))
		EOWRITE4(sc, EHCI_USBCMD, temp | EHCI_CMD_IAAD);
}

static void
ehci_device_bulk_start(struct usb_xfer *xfer)
{
	ehci_softc_t *sc = EHCI_BUS2SC(xfer->xroot->bus);

	/* setup TD's and QH */
	ehci_setup_standard_chain(xfer, &sc->sc_async_p_last);

	/* put transfer on interrupt queue */
	ehci_transfer_intr_enqueue(xfer);

	/* 
	 * XXX Certain nVidia chipsets choke when using the IAAD
	 * feature too frequently.
	 */
	if (sc->sc_flags & EHCI_SCFLG_IAADBUG)
		return;

	ehci_doorbell_async(sc);
}

static const struct usb_pipe_methods ehci_device_bulk_methods =
{
	.open = ehci_device_bulk_open,
	.close = ehci_device_bulk_close,
	.enter = ehci_device_bulk_enter,
	.start = ehci_device_bulk_start,
};

/*------------------------------------------------------------------------*
 * ehci control support
 *------------------------------------------------------------------------*/
static void
ehci_device_ctrl_open(struct usb_xfer *xfer)
{
	return;
}

static void
ehci_device_ctrl_close(struct usb_xfer *xfer)
{
	ehci_device_done(xfer, USB_ERR_CANCELLED);
}

static void
ehci_device_ctrl_enter(struct usb_xfer *xfer)
{
	return;
}

static void
ehci_device_ctrl_start(struct usb_xfer *xfer)
{
	ehci_softc_t *sc = EHCI_BUS2SC(xfer->xroot->bus);

	/* setup TD's and QH */
	ehci_setup_standard_chain(xfer, &sc->sc_async_p_last);

	/* put transfer on interrupt queue */
	ehci_transfer_intr_enqueue(xfer);
}

static const struct usb_pipe_methods ehci_device_ctrl_methods =
{
	.open = ehci_device_ctrl_open,
	.close = ehci_device_ctrl_close,
	.enter = ehci_device_ctrl_enter,
	.start = ehci_device_ctrl_start,
};

/*------------------------------------------------------------------------*
 * ehci interrupt support
 *------------------------------------------------------------------------*/
static void
ehci_device_intr_open(struct usb_xfer *xfer)
{
	ehci_softc_t *sc = EHCI_BUS2SC(xfer->xroot->bus);
	uint16_t best;
	uint16_t bit;
	uint16_t x;

	usb_hs_bandwidth_alloc(xfer);

	/*
	 * Find the best QH position corresponding to the given interval:
	 */

	best = 0;
	bit = EHCI_VIRTUAL_FRAMELIST_COUNT / 2;
	while (bit) {
		if (xfer->interval >= bit) {
			x = bit;
			best = bit;
			while (x & bit) {
				if (sc->sc_intr_stat[x] <
				    sc->sc_intr_stat[best]) {
					best = x;
				}
				x++;
			}
			break;
		}
		bit >>= 1;
	}

	sc->sc_intr_stat[best]++;
	xfer->qh_pos = best;

	DPRINTFN(3, "best=%d interval=%d\n",
	    best, xfer->interval);
}

static void
ehci_device_intr_close(struct usb_xfer *xfer)
{
	ehci_softc_t *sc = EHCI_BUS2SC(xfer->xroot->bus);

	sc->sc_intr_stat[xfer->qh_pos]--;

	ehci_device_done(xfer, USB_ERR_CANCELLED);

	/* bandwidth must be freed after device done */
	usb_hs_bandwidth_free(xfer);
}

static void
ehci_device_intr_enter(struct usb_xfer *xfer)
{
	return;
}

static void
ehci_device_intr_start(struct usb_xfer *xfer)
{
	ehci_softc_t *sc = EHCI_BUS2SC(xfer->xroot->bus);

	/* setup TD's and QH */
	ehci_setup_standard_chain(xfer, &sc->sc_intr_p_last[xfer->qh_pos]);

	/* put transfer on interrupt queue */
	ehci_transfer_intr_enqueue(xfer);
}

static const struct usb_pipe_methods ehci_device_intr_methods =
{
	.open = ehci_device_intr_open,
	.close = ehci_device_intr_close,
	.enter = ehci_device_intr_enter,
	.start = ehci_device_intr_start,
};

/*------------------------------------------------------------------------*
 * ehci full speed isochronous support
 *------------------------------------------------------------------------*/
static void
ehci_device_isoc_fs_open(struct usb_xfer *xfer)
{
	ehci_softc_t *sc = EHCI_BUS2SC(xfer->xroot->bus);
	ehci_sitd_t *td;
	uint32_t sitd_portaddr;
	uint8_t ds;

	sitd_portaddr =
	    EHCI_SITD_SET_ADDR(xfer->address) |
	    EHCI_SITD_SET_ENDPT(UE_GET_ADDR(xfer->endpointno)) |
	    EHCI_SITD_SET_HUBA(xfer->xroot->udev->hs_hub_addr) |
	    EHCI_SITD_SET_PORT(xfer->xroot->udev->hs_port_no);

	if (UE_GET_DIR(xfer->endpointno) == UE_DIR_IN)
		sitd_portaddr |= EHCI_SITD_SET_DIR_IN;

	sitd_portaddr = htohc32(sc, sitd_portaddr);

	/* initialize all TD's */

	for (ds = 0; ds != 2; ds++) {

		for (td = xfer->td_start[ds]; td; td = td->obj_next) {

			td->sitd_portaddr = sitd_portaddr;

			/*
			 * TODO: make some kind of automatic
			 * SMASK/CMASK selection based on micro-frame
			 * usage
			 *
			 * micro-frame usage (8 microframes per 1ms)
			 */
			td->sitd_back = htohc32(sc, EHCI_LINK_TERMINATE);

			usb_pc_cpu_flush(td->page_cache);
		}
	}
}

static void
ehci_device_isoc_fs_close(struct usb_xfer *xfer)
{
	ehci_device_done(xfer, USB_ERR_CANCELLED);
}

static void
ehci_device_isoc_fs_enter(struct usb_xfer *xfer)
{
	struct usb_page_search buf_res;
	ehci_softc_t *sc = EHCI_BUS2SC(xfer->xroot->bus);
	ehci_sitd_t *td;
	ehci_sitd_t *td_last = NULL;
	ehci_sitd_t **pp_last;
	uint32_t *plen;
	uint32_t buf_offset;
	uint32_t nframes;
	uint32_t temp;
	uint32_t sitd_mask;
	uint16_t tlen;
	uint8_t sa;
	uint8_t sb;

#ifdef USB_DEBUG
	uint8_t once = 1;

#endif

	DPRINTFN(6, "xfer=%p next=%d nframes=%d\n",
	    xfer, xfer->endpoint->isoc_next, xfer->nframes);

	/* get the current frame index */

	nframes = EOREAD4(sc, EHCI_FRINDEX) / 8;

	/*
	 * check if the frame index is within the window where the frames
	 * will be inserted
	 */
	buf_offset = (nframes - xfer->endpoint->isoc_next) &
	    (EHCI_VIRTUAL_FRAMELIST_COUNT - 1);

	if ((xfer->endpoint->is_synced == 0) ||
	    (buf_offset < xfer->nframes)) {
		/*
		 * If there is data underflow or the pipe queue is empty we
		 * schedule the transfer a few frames ahead of the current
		 * frame position. Else two isochronous transfers might
		 * overlap.
		 */
		xfer->endpoint->isoc_next = (nframes + 3) &
		    (EHCI_VIRTUAL_FRAMELIST_COUNT - 1);
		xfer->endpoint->is_synced = 1;
		DPRINTFN(3, "start next=%d\n", xfer->endpoint->isoc_next);
	}
	/*
	 * compute how many milliseconds the insertion is ahead of the
	 * current frame position:
	 */
	buf_offset = (xfer->endpoint->isoc_next - nframes) &
	    (EHCI_VIRTUAL_FRAMELIST_COUNT - 1);

	/*
	 * pre-compute when the isochronous transfer will be finished:
	 */
	xfer->isoc_time_complete =
	    usb_isoc_time_expand(&sc->sc_bus, nframes) +
	    buf_offset + xfer->nframes;

	/* get the real number of frames */

	nframes = xfer->nframes;

	buf_offset = 0;

	plen = xfer->frlengths;

	/* toggle the DMA set we are using */
	xfer->flags_int.curr_dma_set ^= 1;

	/* get next DMA set */
	td = xfer->td_start[xfer->flags_int.curr_dma_set];
	xfer->td_transfer_first = td;

	pp_last = &sc->sc_isoc_fs_p_last[xfer->endpoint->isoc_next];

	/* store starting position */

	xfer->qh_pos = xfer->endpoint->isoc_next;

	while (nframes--) {
		if (td == NULL) {
			panic("%s:%d: out of TD's\n",
			    __FUNCTION__, __LINE__);
		}
		if (pp_last >= &sc->sc_isoc_fs_p_last[EHCI_VIRTUAL_FRAMELIST_COUNT])
			pp_last = &sc->sc_isoc_fs_p_last[0];

		/* reuse sitd_portaddr and sitd_back from last transfer */

		if (*plen > xfer->max_frame_size) {
#ifdef USB_DEBUG
			if (once) {
				once = 0;
				printf("%s: frame length(%d) exceeds %d "
				    "bytes (frame truncated)\n",
				    __FUNCTION__, *plen,
				    xfer->max_frame_size);
			}
#endif
			*plen = xfer->max_frame_size;
		}

		/* allocate a slot */

		sa = usbd_fs_isoc_schedule_alloc_slot(xfer,
		    xfer->isoc_time_complete - nframes - 1);

		if (sa == 255) {
			/*
			 * Schedule is FULL, set length to zero:
			 */

			*plen = 0;
			sa = USB_FS_ISOC_UFRAME_MAX - 1;
		}
		if (*plen) {
			/*
			 * only call "usbd_get_page()" when we have a
			 * non-zero length
			 */
			usbd_get_page(xfer->frbuffers, buf_offset, &buf_res);
			td->sitd_bp[0] = htohc32(sc, buf_res.physaddr);
			buf_offset += *plen;
			/*
			 * NOTE: We need to subtract one from the offset so
			 * that we are on a valid page!
			 */
			usbd_get_page(xfer->frbuffers, buf_offset - 1,
			    &buf_res);
			temp = buf_res.physaddr & ~0xFFF;
		} else {
			td->sitd_bp[0] = 0;
			temp = 0;
		}

		if (UE_GET_DIR(xfer->endpointno) == UE_DIR_OUT) {
			tlen = *plen;
			if (tlen <= 188) {
				temp |= 1;	/* T-count = 1, TP = ALL */
				tlen = 1;
			} else {
				tlen += 187;
				tlen /= 188;
				temp |= tlen;	/* T-count = [1..6] */
				temp |= 8;	/* TP = Begin */
			}

			tlen += sa;

			if (tlen >= 8) {
				sb = 0;
			} else {
				sb = (1 << tlen);
			}

			sa = (1 << sa);
			sa = (sb - sa) & 0x3F;
			sb = 0;
		} else {
			sb = (-(4 << sa)) & 0xFE;
			sa = (1 << sa) & 0x3F;
		}

		sitd_mask = (EHCI_SITD_SET_SMASK(sa) |
		    EHCI_SITD_SET_CMASK(sb));

		td->sitd_bp[1] = htohc32(sc, temp);

		td->sitd_mask = htohc32(sc, sitd_mask);

		if (nframes == 0) {
			td->sitd_status = htohc32(sc,
			    EHCI_SITD_IOC |
			    EHCI_SITD_ACTIVE |
			    EHCI_SITD_SET_LEN(*plen));
		} else {
			td->sitd_status = htohc32(sc,
			    EHCI_SITD_ACTIVE |
			    EHCI_SITD_SET_LEN(*plen));
		}
		usb_pc_cpu_flush(td->page_cache);

#ifdef USB_DEBUG
		if (ehcidebug > 15) {
			DPRINTF("FS-TD %d\n", nframes);
			ehci_dump_sitd(sc, td);
		}
#endif
		/* insert TD into schedule */
		EHCI_APPEND_FS_TD(td, *pp_last);
		pp_last++;

		plen++;
		td_last = td;
		td = td->obj_next;
	}

	xfer->td_transfer_last = td_last;

	/* update isoc_next */
	xfer->endpoint->isoc_next = (pp_last - &sc->sc_isoc_fs_p_last[0]) &
	    (EHCI_VIRTUAL_FRAMELIST_COUNT - 1);

	/*
	 * We don't allow cancelling of the SPLIT transaction USB FULL
	 * speed transfer, because it disturbs the bandwidth
	 * computation algorithm.
	 */
	xfer->flags_int.can_cancel_immed = 0;
}

static void
ehci_device_isoc_fs_start(struct usb_xfer *xfer)
{
	/*
	 * We don't allow cancelling of the SPLIT transaction USB FULL
	 * speed transfer, because it disturbs the bandwidth
	 * computation algorithm.
	 */
	xfer->flags_int.can_cancel_immed = 0;

	/* set a default timeout */
	if (xfer->timeout == 0)
		xfer->timeout = 500; /* ms */

	/* put transfer on interrupt queue */
	ehci_transfer_intr_enqueue(xfer);
}

static const struct usb_pipe_methods ehci_device_isoc_fs_methods =
{
	.open = ehci_device_isoc_fs_open,
	.close = ehci_device_isoc_fs_close,
	.enter = ehci_device_isoc_fs_enter,
	.start = ehci_device_isoc_fs_start,
};

/*------------------------------------------------------------------------*
 * ehci high speed isochronous support
 *------------------------------------------------------------------------*/
static void
ehci_device_isoc_hs_open(struct usb_xfer *xfer)
{
	ehci_softc_t *sc = EHCI_BUS2SC(xfer->xroot->bus);
	ehci_itd_t *td;
	uint32_t temp;
	uint8_t ds;

	usb_hs_bandwidth_alloc(xfer);

	/* initialize all TD's */

	for (ds = 0; ds != 2; ds++) {

		for (td = xfer->td_start[ds]; td; td = td->obj_next) {

			/* set TD inactive */
			td->itd_status[0] = 0;
			td->itd_status[1] = 0;
			td->itd_status[2] = 0;
			td->itd_status[3] = 0;
			td->itd_status[4] = 0;
			td->itd_status[5] = 0;
			td->itd_status[6] = 0;
			td->itd_status[7] = 0;

			/* set endpoint and address */
			td->itd_bp[0] = htohc32(sc,
			    EHCI_ITD_SET_ADDR(xfer->address) |
			    EHCI_ITD_SET_ENDPT(UE_GET_ADDR(xfer->endpointno)));

			temp =
			    EHCI_ITD_SET_MPL(xfer->max_packet_size & 0x7FF);

			/* set direction */
			if (UE_GET_DIR(xfer->endpointno) == UE_DIR_IN) {
				temp |= EHCI_ITD_SET_DIR_IN;
			}
			/* set maximum packet size */
			td->itd_bp[1] = htohc32(sc, temp);

			/* set transfer multiplier */
			td->itd_bp[2] = htohc32(sc, xfer->max_packet_count & 3);

			usb_pc_cpu_flush(td->page_cache);
		}
	}
}

static void
ehci_device_isoc_hs_close(struct usb_xfer *xfer)
{
	ehci_device_done(xfer, USB_ERR_CANCELLED);

	/* bandwidth must be freed after device done */
	usb_hs_bandwidth_free(xfer);
}

static void
ehci_device_isoc_hs_enter(struct usb_xfer *xfer)
{
	struct usb_page_search buf_res;
	ehci_softc_t *sc = EHCI_BUS2SC(xfer->xroot->bus);
	ehci_itd_t *td;
	ehci_itd_t *td_last = NULL;
	ehci_itd_t **pp_last;
	bus_size_t page_addr;
	uint32_t *plen;
	uint32_t status;
	uint32_t buf_offset;
	uint32_t nframes;
	uint32_t itd_offset[8 + 1];
	uint8_t x;
	uint8_t td_no;
	uint8_t page_no;
	uint8_t shift = usbd_xfer_get_fps_shift(xfer);

#ifdef USB_DEBUG
	uint8_t once = 1;

#endif

	DPRINTFN(6, "xfer=%p next=%d nframes=%d shift=%d\n",
	    xfer, xfer->endpoint->isoc_next, xfer->nframes, (int)shift);

	/* get the current frame index */

	nframes = EOREAD4(sc, EHCI_FRINDEX) / 8;

	/*
	 * check if the frame index is within the window where the frames
	 * will be inserted
	 */
	buf_offset = (nframes - xfer->endpoint->isoc_next) &
	    (EHCI_VIRTUAL_FRAMELIST_COUNT - 1);

	if ((xfer->endpoint->is_synced == 0) ||
	    (buf_offset < (((xfer->nframes << shift) + 7) / 8))) {
		/*
		 * If there is data underflow or the pipe queue is empty we
		 * schedule the transfer a few frames ahead of the current
		 * frame position. Else two isochronous transfers might
		 * overlap.
		 */
		xfer->endpoint->isoc_next = (nframes + 3) &
		    (EHCI_VIRTUAL_FRAMELIST_COUNT - 1);
		xfer->endpoint->is_synced = 1;
		DPRINTFN(3, "start next=%d\n", xfer->endpoint->isoc_next);
	}
	/*
	 * compute how many milliseconds the insertion is ahead of the
	 * current frame position:
	 */
	buf_offset = (xfer->endpoint->isoc_next - nframes) &
	    (EHCI_VIRTUAL_FRAMELIST_COUNT - 1);

	/*
	 * pre-compute when the isochronous transfer will be finished:
	 */
	xfer->isoc_time_complete =
	    usb_isoc_time_expand(&sc->sc_bus, nframes) + buf_offset +
	    (((xfer->nframes << shift) + 7) / 8);

	/* get the real number of frames */

	nframes = xfer->nframes;

	buf_offset = 0;
	td_no = 0;

	plen = xfer->frlengths;

	/* toggle the DMA set we are using */
	xfer->flags_int.curr_dma_set ^= 1;

	/* get next DMA set */
	td = xfer->td_start[xfer->flags_int.curr_dma_set];
	xfer->td_transfer_first = td;

	pp_last = &sc->sc_isoc_hs_p_last[xfer->endpoint->isoc_next];

	/* store starting position */

	xfer->qh_pos = xfer->endpoint->isoc_next;

	while (nframes) {
		if (td == NULL) {
			panic("%s:%d: out of TD's\n",
			    __FUNCTION__, __LINE__);
		}
		if (pp_last >= &sc->sc_isoc_hs_p_last[EHCI_VIRTUAL_FRAMELIST_COUNT]) {
			pp_last = &sc->sc_isoc_hs_p_last[0];
		}
		/* range check */
		if (*plen > xfer->max_frame_size) {
#ifdef USB_DEBUG
			if (once) {
				once = 0;
				printf("%s: frame length(%d) exceeds %d bytes "
				    "(frame truncated)\n",
				    __FUNCTION__, *plen, xfer->max_frame_size);
			}
#endif
			*plen = xfer->max_frame_size;
		}

		if (xfer->endpoint->usb_smask & (1 << td_no)) {
			status = (EHCI_ITD_SET_LEN(*plen) |
			    EHCI_ITD_ACTIVE |
			    EHCI_ITD_SET_PG(0));
			td->itd_status[td_no] = htohc32(sc, status);
			itd_offset[td_no] = buf_offset;
			buf_offset += *plen;
			plen++;
			nframes --;
		} else {
			td->itd_status[td_no] = 0;	/* not active */
			itd_offset[td_no] = buf_offset;
		}

		td_no++;

		if ((td_no == 8) || (nframes == 0)) {

			/* the rest of the transfers are not active, if any */
			for (x = td_no; x != 8; x++) {
				td->itd_status[x] = 0;	/* not active */
			}

			/* check if there is any data to be transferred */
			if (itd_offset[0] != buf_offset) {
				page_no = 0;
				itd_offset[td_no] = buf_offset;

				/* get first page offset */
				usbd_get_page(xfer->frbuffers, itd_offset[0], &buf_res);
				/* get page address */
				page_addr = buf_res.physaddr & ~0xFFF;
				/* update page address */
				td->itd_bp[0] &= htohc32(sc, 0xFFF);
				td->itd_bp[0] |= htohc32(sc, page_addr);

				for (x = 0; x != td_no; x++) {
					/* set page number and page offset */
					status = (EHCI_ITD_SET_PG(page_no) |
					    (buf_res.physaddr & 0xFFF));
					td->itd_status[x] |= htohc32(sc, status);

					/* get next page offset */
					if (itd_offset[x + 1] == buf_offset) {
						/*
						 * We subtract one so that
						 * we don't go off the last
						 * page!
						 */
						usbd_get_page(xfer->frbuffers, buf_offset - 1, &buf_res);
					} else {
						usbd_get_page(xfer->frbuffers, itd_offset[x + 1], &buf_res);
					}

					/* check if we need a new page */
					if ((buf_res.physaddr ^ page_addr) & ~0xFFF) {
						/* new page needed */
						page_addr = buf_res.physaddr & ~0xFFF;
						if (page_no == 6) {
							panic("%s: too many pages\n", __FUNCTION__);
						}
						page_no++;
						/* update page address */
						td->itd_bp[page_no] &= htohc32(sc, 0xFFF);
						td->itd_bp[page_no] |= htohc32(sc, page_addr);
					}
				}
			}
			/* set IOC bit if we are complete */
			if (nframes == 0) {
				td->itd_status[td_no - 1] |= htohc32(sc, EHCI_ITD_IOC);
			}
			usb_pc_cpu_flush(td->page_cache);
#ifdef USB_DEBUG
			if (ehcidebug > 15) {
				DPRINTF("HS-TD %d\n", nframes);
				ehci_dump_itd(sc, td);
			}
#endif
			/* insert TD into schedule */
			EHCI_APPEND_HS_TD(td, *pp_last);
			pp_last++;

			td_no = 0;
			td_last = td;
			td = td->obj_next;
		}
	}

	xfer->td_transfer_last = td_last;

	/* update isoc_next */
	xfer->endpoint->isoc_next = (pp_last - &sc->sc_isoc_hs_p_last[0]) &
	    (EHCI_VIRTUAL_FRAMELIST_COUNT - 1);
}

static void
ehci_device_isoc_hs_start(struct usb_xfer *xfer)
{
	/* put transfer on interrupt queue */
	ehci_transfer_intr_enqueue(xfer);
}

static const struct usb_pipe_methods ehci_device_isoc_hs_methods =
{
	.open = ehci_device_isoc_hs_open,
	.close = ehci_device_isoc_hs_close,
	.enter = ehci_device_isoc_hs_enter,
	.start = ehci_device_isoc_hs_start,
};

/*------------------------------------------------------------------------*
 * ehci root control support
 *------------------------------------------------------------------------*
 * Simulate a hardware hub by handling all the necessary requests.
 *------------------------------------------------------------------------*/

static const
struct usb_device_descriptor ehci_devd =
{
	sizeof(struct usb_device_descriptor),
	UDESC_DEVICE,			/* type */
	{0x00, 0x02},			/* USB version */
	UDCLASS_HUB,			/* class */
	UDSUBCLASS_HUB,			/* subclass */
	UDPROTO_HSHUBSTT,		/* protocol */
	64,				/* max packet */
	{0}, {0}, {0x00, 0x01},		/* device id */
	1, 2, 0,			/* string indexes */
	1				/* # of configurations */
};

static const
struct usb_device_qualifier ehci_odevd =
{
	sizeof(struct usb_device_qualifier),
	UDESC_DEVICE_QUALIFIER,		/* type */
	{0x00, 0x02},			/* USB version */
	UDCLASS_HUB,			/* class */
	UDSUBCLASS_HUB,			/* subclass */
	UDPROTO_FSHUB,			/* protocol */
	0,				/* max packet */
	0,				/* # of configurations */
	0
};

static const struct ehci_config_desc ehci_confd = {
	.confd = {
		.bLength = sizeof(struct usb_config_descriptor),
		.bDescriptorType = UDESC_CONFIG,
		.wTotalLength[0] = sizeof(ehci_confd),
		.bNumInterface = 1,
		.bConfigurationValue = 1,
		.iConfiguration = 0,
		.bmAttributes = UC_SELF_POWERED,
		.bMaxPower = 0		/* max power */
	},
	.ifcd = {
		.bLength = sizeof(struct usb_interface_descriptor),
		.bDescriptorType = UDESC_INTERFACE,
		.bNumEndpoints = 1,
		.bInterfaceClass = UICLASS_HUB,
		.bInterfaceSubClass = UISUBCLASS_HUB,
		.bInterfaceProtocol = 0,
	},
	.endpd = {
		.bLength = sizeof(struct usb_endpoint_descriptor),
		.bDescriptorType = UDESC_ENDPOINT,
		.bEndpointAddress = UE_DIR_IN | EHCI_INTR_ENDPT,
		.bmAttributes = UE_INTERRUPT,
		.wMaxPacketSize[0] = 8,	/* max packet (63 ports) */
		.bInterval = 255,
	},
};

static const
struct usb_hub_descriptor ehci_hubd =
{
	.bDescLength = 0,		/* dynamic length */
	.bDescriptorType = UDESC_HUB,
};

uint16_t
ehci_get_port_speed_portsc(struct ehci_softc *sc, uint16_t index)
{
	uint32_t v;

	v = EOREAD4(sc, EHCI_PORTSC(index));
	v = (v >> EHCI_PORTSC_PSPD_SHIFT) & EHCI_PORTSC_PSPD_MASK;

	if (v == EHCI_PORT_SPEED_HIGH)
		return (UPS_HIGH_SPEED);
	if (v == EHCI_PORT_SPEED_LOW)
		return (UPS_LOW_SPEED);
	return (0);
}

uint16_t
ehci_get_port_speed_hostc(struct ehci_softc *sc, uint16_t index)
{
	uint32_t v;

	v = EOREAD4(sc, EHCI_HOSTC(index));
	v = (v >> EHCI_HOSTC_PSPD_SHIFT) & EHCI_HOSTC_PSPD_MASK;

	if (v == EHCI_PORT_SPEED_HIGH)
		return (UPS_HIGH_SPEED);
	if (v == EHCI_PORT_SPEED_LOW)
		return (UPS_LOW_SPEED);
	return (0);
}

static void
ehci_disown(ehci_softc_t *sc, uint16_t index, uint8_t lowspeed)
{
	uint32_t port;
	uint32_t v;

	DPRINTF("index=%d lowspeed=%d\n", index, lowspeed);

	port = EHCI_PORTSC(index);
	v = EOREAD4(sc, port) & ~EHCI_PS_CLEAR;
	EOWRITE4(sc, port, v | EHCI_PS_PO);
}

static usb_error_t
ehci_roothub_exec(struct usb_device *udev,
    struct usb_device_request *req, const void **pptr, uint16_t *plength)
{
	ehci_softc_t *sc = EHCI_BUS2SC(udev->bus);
	const char *str_ptr;
	const void *ptr;
	uint32_t port;
	uint32_t v;
	uint16_t len;
	uint16_t i;
	uint16_t value;
	uint16_t index;
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
			len = sizeof(ehci_devd);
			ptr = (const void *)&ehci_devd;
			break;
			/*
			 * We can't really operate at another speed,
			 * but the specification says we need this
			 * descriptor:
			 */
		case UDESC_DEVICE_QUALIFIER:
			if ((value & 0xff) != 0) {
				err = USB_ERR_IOERROR;
				goto done;
			}
			len = sizeof(ehci_odevd);
			ptr = (const void *)&ehci_odevd;
			break;

		case UDESC_CONFIG:
			if ((value & 0xff) != 0) {
				err = USB_ERR_IOERROR;
				goto done;
			}
			len = sizeof(ehci_confd);
			ptr = (const void *)&ehci_confd;
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
				str_ptr = "EHCI root HUB";
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
		if (value >= EHCI_MAX_DEVICES) {
			err = USB_ERR_IOERROR;
			goto done;
		}
		sc->sc_addr = value;
		break;
	case C(UR_SET_CONFIG, UT_WRITE_DEVICE):
		if ((value != 0) && (value != 1)) {
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
		port = EHCI_PORTSC(index);
		v = EOREAD4(sc, port) & ~EHCI_PS_CLEAR;
		switch (value) {
		case UHF_PORT_ENABLE:
			EOWRITE4(sc, port, v & ~EHCI_PS_PE);
			break;
		case UHF_PORT_SUSPEND:
			if ((v & EHCI_PS_SUSP) && (!(v & EHCI_PS_FPR))) {

				/*
				 * waking up a High Speed device is rather
				 * complicated if
				 */
				EOWRITE4(sc, port, v | EHCI_PS_FPR);
			}
			/* wait 20ms for resume sequence to complete */
			usb_pause_mtx(&sc->sc_bus.bus_mtx, hz / 50);

			EOWRITE4(sc, port, v & ~(EHCI_PS_SUSP |
			    EHCI_PS_FPR | (3 << 10) /* High Speed */ ));

			/* 4ms settle time */
			usb_pause_mtx(&sc->sc_bus.bus_mtx, hz / 250);
			break;
		case UHF_PORT_POWER:
			EOWRITE4(sc, port, v & ~EHCI_PS_PP);
			break;
		case UHF_PORT_TEST:
			DPRINTFN(3, "clear port test "
			    "%d\n", index);
			break;
		case UHF_PORT_INDICATOR:
			DPRINTFN(3, "clear port ind "
			    "%d\n", index);
			EOWRITE4(sc, port, v & ~EHCI_PS_PIC);
			break;
		case UHF_C_PORT_CONNECTION:
			EOWRITE4(sc, port, v | EHCI_PS_CSC);
			break;
		case UHF_C_PORT_ENABLE:
			EOWRITE4(sc, port, v | EHCI_PS_PEC);
			break;
		case UHF_C_PORT_SUSPEND:
			EOWRITE4(sc, port, v | EHCI_PS_SUSP);
			break;
		case UHF_C_PORT_OVER_CURRENT:
			EOWRITE4(sc, port, v | EHCI_PS_OCC);
			break;
		case UHF_C_PORT_RESET:
			sc->sc_isreset = 0;
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
		v = EREAD4(sc, EHCI_HCSPARAMS);

		sc->sc_hub_desc.hubd = ehci_hubd;
		sc->sc_hub_desc.hubd.bNbrPorts = sc->sc_noport;

		if (EHCI_HCS_PPC(v))
			i = UHD_PWR_INDIVIDUAL;
		else
			i = UHD_PWR_NO_SWITCH;

		if (EHCI_HCS_P_INDICATOR(v))
			i |= UHD_PORT_IND;

		USETW(sc->sc_hub_desc.hubd.wHubCharacteristics, i);
		/* XXX can't find out? */
		sc->sc_hub_desc.hubd.bPwrOn2PwrGood = 200;
		/* XXX don't know if ports are removable or not */
		sc->sc_hub_desc.hubd.bDescLength =
		    8 + ((sc->sc_noport + 7) / 8);
		len = sc->sc_hub_desc.hubd.bDescLength;
		break;
	case C(UR_GET_STATUS, UT_READ_CLASS_DEVICE):
		len = 16;
		memset(sc->sc_hub_desc.temp, 0, 16);
		break;
	case C(UR_GET_STATUS, UT_READ_CLASS_OTHER):
		DPRINTFN(9, "get port status i=%d\n",
		    index);
		if ((index < 1) ||
		    (index > sc->sc_noport)) {
			err = USB_ERR_IOERROR;
			goto done;
		}
		v = EOREAD4(sc, EHCI_PORTSC(index));
		DPRINTFN(9, "port status=0x%04x\n", v);
		if (sc->sc_flags & EHCI_SCFLG_TT) {
			if (sc->sc_vendor_get_port_speed != NULL) {
				i = sc->sc_vendor_get_port_speed(sc, index);
			} else {
				device_printf(sc->sc_bus.bdev,
				    "EHCI_SCFLG_TT quirk is set but "
				    "sc_vendor_get_hub_speed() is NULL\n");
				i = UPS_HIGH_SPEED;
			}
		} else {
			i = UPS_HIGH_SPEED;
		}
		if (v & EHCI_PS_CS)
			i |= UPS_CURRENT_CONNECT_STATUS;
		if (v & EHCI_PS_PE)
			i |= UPS_PORT_ENABLED;
		if ((v & EHCI_PS_SUSP) && !(v & EHCI_PS_FPR))
			i |= UPS_SUSPEND;
		if (v & EHCI_PS_OCA)
			i |= UPS_OVERCURRENT_INDICATOR;
		if (v & EHCI_PS_PR)
			i |= UPS_RESET;
		if (v & EHCI_PS_PP)
			i |= UPS_PORT_POWER;
		USETW(sc->sc_hub_desc.ps.wPortStatus, i);
		i = 0;
		if (v & EHCI_PS_CSC)
			i |= UPS_C_CONNECT_STATUS;
		if (v & EHCI_PS_PEC)
			i |= UPS_C_PORT_ENABLED;
		if (v & EHCI_PS_OCC)
			i |= UPS_C_OVERCURRENT_INDICATOR;
		if (v & EHCI_PS_FPR)
			i |= UPS_C_SUSPEND;
		if (sc->sc_isreset)
			i |= UPS_C_PORT_RESET;
		USETW(sc->sc_hub_desc.ps.wPortChange, i);
		len = sizeof(sc->sc_hub_desc.ps);
		break;
	case C(UR_SET_DESCRIPTOR, UT_WRITE_CLASS_DEVICE):
		err = USB_ERR_IOERROR;
		goto done;
	case C(UR_SET_FEATURE, UT_WRITE_CLASS_DEVICE):
		break;
	case C(UR_SET_FEATURE, UT_WRITE_CLASS_OTHER):
		if ((index < 1) ||
		    (index > sc->sc_noport)) {
			err = USB_ERR_IOERROR;
			goto done;
		}
		port = EHCI_PORTSC(index);
		v = EOREAD4(sc, port) & ~EHCI_PS_CLEAR;
		switch (value) {
		case UHF_PORT_ENABLE:
			EOWRITE4(sc, port, v | EHCI_PS_PE);
			break;
		case UHF_PORT_SUSPEND:
			EOWRITE4(sc, port, v | EHCI_PS_SUSP);
			break;
		case UHF_PORT_RESET:
			DPRINTFN(6, "reset port %d\n", index);
#ifdef USB_DEBUG
			if (ehcinohighspeed) {
				/*
				 * Connect USB device to companion
				 * controller.
				 */
				ehci_disown(sc, index, 1);
				break;
			}
#endif
			if (EHCI_PS_IS_LOWSPEED(v) &&
			    (sc->sc_flags & EHCI_SCFLG_TT) == 0) {
				/* Low speed device, give up ownership. */
				ehci_disown(sc, index, 1);
				break;
			}
			/* Start reset sequence. */
			v &= ~(EHCI_PS_PE | EHCI_PS_PR);
			EOWRITE4(sc, port, v | EHCI_PS_PR);

			/* Wait for reset to complete. */
			usb_pause_mtx(&sc->sc_bus.bus_mtx,
			    USB_MS_TO_TICKS(usb_port_root_reset_delay));

			/* Terminate reset sequence. */
			if (!(sc->sc_flags & EHCI_SCFLG_NORESTERM))
				EOWRITE4(sc, port, v);

			/* Wait for HC to complete reset. */
			usb_pause_mtx(&sc->sc_bus.bus_mtx,
			    USB_MS_TO_TICKS(EHCI_PORT_RESET_COMPLETE));

			v = EOREAD4(sc, port);
			DPRINTF("ehci after reset, status=0x%08x\n", v);
			if (v & EHCI_PS_PR) {
				device_printf(sc->sc_bus.bdev,
				    "port reset timeout\n");
				err = USB_ERR_TIMEOUT;
				goto done;
			}
			if (!(v & EHCI_PS_PE) &&
			    (sc->sc_flags & EHCI_SCFLG_TT) == 0) {
				/* Not a high speed device, give up ownership.*/
				ehci_disown(sc, index, 0);
				break;
			}
			sc->sc_isreset = 1;
			DPRINTF("ehci port %d reset, status = 0x%08x\n",
			    index, v);
			break;

		case UHF_PORT_POWER:
			DPRINTFN(3, "set port power %d\n", index);
			EOWRITE4(sc, port, v | EHCI_PS_PP);
			break;

		case UHF_PORT_TEST:
			DPRINTFN(3, "set port test %d\n", index);
			break;

		case UHF_PORT_INDICATOR:
			DPRINTFN(3, "set port ind %d\n", index);
			EOWRITE4(sc, port, v | EHCI_PS_PIC);
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
ehci_xfer_setup(struct usb_setup_params *parm)
{
	struct usb_page_search page_info;
	struct usb_page_cache *pc;
	ehci_softc_t *sc;
	struct usb_xfer *xfer;
	void *last_obj;
	uint32_t nqtd;
	uint32_t nqh;
	uint32_t nsitd;
	uint32_t nitd;
	uint32_t n;

	sc = EHCI_BUS2SC(parm->udev->bus);
	xfer = parm->curr_xfer;

	nqtd = 0;
	nqh = 0;
	nsitd = 0;
	nitd = 0;

	/*
	 * compute maximum number of some structures
	 */
	if (parm->methods == &ehci_device_ctrl_methods) {

		/*
		 * The proof for the "nqtd" formula is illustrated like
		 * this:
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
		 * One to transfer the remaining data and one to finalise
		 * with a zero length packet in case the "force_short_xfer"
		 * flag is set. We only need two USB transfer descriptors in
		 * the case where the transfer length of the first one is a
		 * factor of "max_frame_size". The rest of the needed USB
		 * transfer descriptors is given by the buffer size divided
		 * by the maximum data payload.
		 */
		parm->hc_max_packet_size = 0x400;
		parm->hc_max_packet_count = 1;
		parm->hc_max_frame_size = EHCI_QTD_PAYLOAD_MAX;
		xfer->flags_int.bdma_enable = 1;

		usbd_transfer_setup_sub(parm);

		nqh = 1;
		nqtd = ((2 * xfer->nframes) + 1	/* STATUS */
		    + (xfer->max_data_length / xfer->max_hc_frame_size));

	} else if (parm->methods == &ehci_device_bulk_methods) {

		parm->hc_max_packet_size = 0x400;
		parm->hc_max_packet_count = 1;
		parm->hc_max_frame_size = EHCI_QTD_PAYLOAD_MAX;
		xfer->flags_int.bdma_enable = 1;

		usbd_transfer_setup_sub(parm);

		nqh = 1;
		nqtd = ((2 * xfer->nframes)
		    + (xfer->max_data_length / xfer->max_hc_frame_size));

	} else if (parm->methods == &ehci_device_intr_methods) {

		if (parm->speed == USB_SPEED_HIGH) {
			parm->hc_max_packet_size = 0x400;
			parm->hc_max_packet_count = 3;
		} else if (parm->speed == USB_SPEED_FULL) {
			parm->hc_max_packet_size = USB_FS_BYTES_PER_HS_UFRAME;
			parm->hc_max_packet_count = 1;
		} else {
			parm->hc_max_packet_size = USB_FS_BYTES_PER_HS_UFRAME / 8;
			parm->hc_max_packet_count = 1;
		}

		parm->hc_max_frame_size = EHCI_QTD_PAYLOAD_MAX;
		xfer->flags_int.bdma_enable = 1;

		usbd_transfer_setup_sub(parm);

		nqh = 1;
		nqtd = ((2 * xfer->nframes)
		    + (xfer->max_data_length / xfer->max_hc_frame_size));

	} else if (parm->methods == &ehci_device_isoc_fs_methods) {

		parm->hc_max_packet_size = 0x3FF;
		parm->hc_max_packet_count = 1;
		parm->hc_max_frame_size = 0x3FF;
		xfer->flags_int.bdma_enable = 1;

		usbd_transfer_setup_sub(parm);

		nsitd = xfer->nframes;

	} else if (parm->methods == &ehci_device_isoc_hs_methods) {

		parm->hc_max_packet_size = 0x400;
		parm->hc_max_packet_count = 3;
		parm->hc_max_frame_size = 0xC00;
		xfer->flags_int.bdma_enable = 1;

		usbd_transfer_setup_sub(parm);

		nitd = ((xfer->nframes + 7) / 8) <<
		    usbd_xfer_get_fps_shift(xfer);

	} else {

		parm->hc_max_packet_size = 0x400;
		parm->hc_max_packet_count = 1;
		parm->hc_max_frame_size = 0x400;

		usbd_transfer_setup_sub(parm);
	}

alloc_dma_set:

	if (parm->err) {
		return;
	}
	/*
	 * Allocate queue heads and transfer descriptors
	 */
	last_obj = NULL;

	if (usbd_transfer_setup_sub_malloc(
	    parm, &pc, sizeof(ehci_itd_t),
	    EHCI_ITD_ALIGN, nitd)) {
		parm->err = USB_ERR_NOMEM;
		return;
	}
	if (parm->buf) {
		for (n = 0; n != nitd; n++) {
			ehci_itd_t *td;

			usbd_get_page(pc + n, 0, &page_info);

			td = page_info.buffer;

			/* init TD */
			td->itd_self = htohc32(sc, page_info.physaddr | EHCI_LINK_ITD);
			td->obj_next = last_obj;
			td->page_cache = pc + n;

			last_obj = td;

			usb_pc_cpu_flush(pc + n);
		}
	}
	if (usbd_transfer_setup_sub_malloc(
	    parm, &pc, sizeof(ehci_sitd_t),
	    EHCI_SITD_ALIGN, nsitd)) {
		parm->err = USB_ERR_NOMEM;
		return;
	}
	if (parm->buf) {
		for (n = 0; n != nsitd; n++) {
			ehci_sitd_t *td;

			usbd_get_page(pc + n, 0, &page_info);

			td = page_info.buffer;

			/* init TD */
			td->sitd_self = htohc32(sc, page_info.physaddr | EHCI_LINK_SITD);
			td->obj_next = last_obj;
			td->page_cache = pc + n;

			last_obj = td;

			usb_pc_cpu_flush(pc + n);
		}
	}
	if (usbd_transfer_setup_sub_malloc(
	    parm, &pc, sizeof(ehci_qtd_t),
	    EHCI_QTD_ALIGN, nqtd)) {
		parm->err = USB_ERR_NOMEM;
		return;
	}
	if (parm->buf) {
		for (n = 0; n != nqtd; n++) {
			ehci_qtd_t *qtd;

			usbd_get_page(pc + n, 0, &page_info);

			qtd = page_info.buffer;

			/* init TD */
			qtd->qtd_self = htohc32(sc, page_info.physaddr);
			qtd->obj_next = last_obj;
			qtd->page_cache = pc + n;

			last_obj = qtd;

			usb_pc_cpu_flush(pc + n);
		}
	}
	xfer->td_start[xfer->flags_int.curr_dma_set] = last_obj;

	last_obj = NULL;

	if (usbd_transfer_setup_sub_malloc(
	    parm, &pc, sizeof(ehci_qh_t),
	    EHCI_QH_ALIGN, nqh)) {
		parm->err = USB_ERR_NOMEM;
		return;
	}
	if (parm->buf) {
		for (n = 0; n != nqh; n++) {
			ehci_qh_t *qh;

			usbd_get_page(pc + n, 0, &page_info);

			qh = page_info.buffer;

			/* init QH */
			qh->qh_self = htohc32(sc, page_info.physaddr | EHCI_LINK_QH);
			qh->obj_next = last_obj;
			qh->page_cache = pc + n;

			last_obj = qh;

			usb_pc_cpu_flush(pc + n);
		}
	}
	xfer->qh_start[xfer->flags_int.curr_dma_set] = last_obj;

	if (!xfer->flags_int.curr_dma_set) {
		xfer->flags_int.curr_dma_set = 1;
		goto alloc_dma_set;
	}
}

static void
ehci_xfer_unsetup(struct usb_xfer *xfer)
{
	return;
}

static void
ehci_ep_init(struct usb_device *udev, struct usb_endpoint_descriptor *edesc,
    struct usb_endpoint *ep)
{
	ehci_softc_t *sc = EHCI_BUS2SC(udev->bus);

	DPRINTFN(2, "endpoint=%p, addr=%d, endpt=%d, mode=%d (%d)\n",
	    ep, udev->address,
	    edesc->bEndpointAddress, udev->flags.usb_mode,
	    sc->sc_addr);

	if (udev->device_index != sc->sc_addr) {

		if ((udev->speed != USB_SPEED_HIGH) &&
		    ((udev->hs_hub_addr == 0) ||
		    (udev->hs_port_no == 0) ||
		    (udev->parent_hs_hub == NULL) ||
		    (udev->parent_hs_hub->hub == NULL))) {
			/* We need a transaction translator */
			goto done;
		}
		switch (edesc->bmAttributes & UE_XFERTYPE) {
		case UE_CONTROL:
			ep->methods = &ehci_device_ctrl_methods;
			break;
		case UE_INTERRUPT:
			ep->methods = &ehci_device_intr_methods;
			break;
		case UE_ISOCHRONOUS:
			if (udev->speed == USB_SPEED_HIGH) {
				ep->methods = &ehci_device_isoc_hs_methods;
			} else if (udev->speed == USB_SPEED_FULL) {
				ep->methods = &ehci_device_isoc_fs_methods;
			}
			break;
		case UE_BULK:
			ep->methods = &ehci_device_bulk_methods;
			break;
		default:
			/* do nothing */
			break;
		}
	}
done:
	return;
}

static void
ehci_get_dma_delay(struct usb_device *udev, uint32_t *pus)
{
	/*
	 * Wait until the hardware has finished any possible use of
	 * the transfer descriptor(s) and QH
	 */
	*pus = (1125);			/* microseconds */
}

static void
ehci_device_resume(struct usb_device *udev)
{
	ehci_softc_t *sc = EHCI_BUS2SC(udev->bus);
	struct usb_xfer *xfer;
	const struct usb_pipe_methods *methods;

	DPRINTF("\n");

	USB_BUS_LOCK(udev->bus);

	TAILQ_FOREACH(xfer, &sc->sc_bus.intr_q.head, wait_entry) {

		if (xfer->xroot->udev == udev) {

			methods = xfer->endpoint->methods;

			if ((methods == &ehci_device_bulk_methods) ||
			    (methods == &ehci_device_ctrl_methods)) {
				EHCI_APPEND_QH(xfer->qh_start[xfer->flags_int.curr_dma_set],
				    sc->sc_async_p_last);
			}
			if (methods == &ehci_device_intr_methods) {
				EHCI_APPEND_QH(xfer->qh_start[xfer->flags_int.curr_dma_set],
				    sc->sc_intr_p_last[xfer->qh_pos]);
			}
		}
	}

	USB_BUS_UNLOCK(udev->bus);

	return;
}

static void
ehci_device_suspend(struct usb_device *udev)
{
	ehci_softc_t *sc = EHCI_BUS2SC(udev->bus);
	struct usb_xfer *xfer;
	const struct usb_pipe_methods *methods;

	DPRINTF("\n");

	USB_BUS_LOCK(udev->bus);

	TAILQ_FOREACH(xfer, &sc->sc_bus.intr_q.head, wait_entry) {

		if (xfer->xroot->udev == udev) {

			methods = xfer->endpoint->methods;

			if ((methods == &ehci_device_bulk_methods) ||
			    (methods == &ehci_device_ctrl_methods)) {
				EHCI_REMOVE_QH(xfer->qh_start[xfer->flags_int.curr_dma_set],
				    sc->sc_async_p_last);
			}
			if (methods == &ehci_device_intr_methods) {
				EHCI_REMOVE_QH(xfer->qh_start[xfer->flags_int.curr_dma_set],
				    sc->sc_intr_p_last[xfer->qh_pos]);
			}
		}
	}

	USB_BUS_UNLOCK(udev->bus);
}

static void
ehci_set_hw_power_sleep(struct usb_bus *bus, uint32_t state)
{
	struct ehci_softc *sc = EHCI_BUS2SC(bus);

	switch (state) {
	case USB_HW_POWER_SUSPEND:
	case USB_HW_POWER_SHUTDOWN:
		ehci_suspend(sc);
		break;
	case USB_HW_POWER_RESUME:
		ehci_resume(sc);
		break;
	default:
		break;
	}
}

static void
ehci_set_hw_power(struct usb_bus *bus)
{
	ehci_softc_t *sc = EHCI_BUS2SC(bus);
	uint32_t temp;
	uint32_t flags;

	DPRINTF("\n");

	USB_BUS_LOCK(bus);

	flags = bus->hw_power_state;

	temp = EOREAD4(sc, EHCI_USBCMD);

	temp &= ~(EHCI_CMD_ASE | EHCI_CMD_PSE);

	if (flags & (USB_HW_POWER_CONTROL |
	    USB_HW_POWER_BULK)) {
		DPRINTF("Async is active\n");
		temp |= EHCI_CMD_ASE;
	}
	if (flags & (USB_HW_POWER_INTERRUPT |
	    USB_HW_POWER_ISOC)) {
		DPRINTF("Periodic is active\n");
		temp |= EHCI_CMD_PSE;
	}
	EOWRITE4(sc, EHCI_USBCMD, temp);

	USB_BUS_UNLOCK(bus);

	return;
}

static void
ehci_start_dma_delay_second(struct usb_xfer *xfer)
{
	struct ehci_softc *sc = EHCI_BUS2SC(xfer->xroot->bus);

	DPRINTF("\n");

	/* trigger doorbell */
	ehci_doorbell_async(sc);

	/* give the doorbell 4ms */
	usbd_transfer_timeout_ms(xfer,
	    (void (*)(void *))&usb_dma_delay_done_cb, 4);
}

/*
 * Ring the doorbell twice before freeing any DMA descriptors. Some host
 * controllers apparently cache the QH descriptors and need a message
 * that the cache needs to be discarded.
 */
static void
ehci_start_dma_delay(struct usb_xfer *xfer)
{
	struct ehci_softc *sc = EHCI_BUS2SC(xfer->xroot->bus);

	DPRINTF("\n");

	/* trigger doorbell */
	ehci_doorbell_async(sc);

	/* give the doorbell 4ms */
	usbd_transfer_timeout_ms(xfer,
	    (void (*)(void *))&ehci_start_dma_delay_second, 4);
}

static const struct usb_bus_methods ehci_bus_methods =
{
	.endpoint_init = ehci_ep_init,
	.xfer_setup = ehci_xfer_setup,
	.xfer_unsetup = ehci_xfer_unsetup,
	.get_dma_delay = ehci_get_dma_delay,
	.device_resume = ehci_device_resume,
	.device_suspend = ehci_device_suspend,
	.set_hw_power = ehci_set_hw_power,
	.set_hw_power_sleep = ehci_set_hw_power_sleep,
	.roothub_exec = ehci_roothub_exec,
	.xfer_poll = ehci_do_poll,
	.start_dma_delay = ehci_start_dma_delay,
};
