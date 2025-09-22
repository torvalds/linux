/*	$OpenBSD: ufshci.c,v 1.46 2025/01/18 19:42:39 mglocker Exp $ */

/*
 * Copyright (c) 2022 Marcus Glocker <mglocker@openbsd.org>
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

/*
 * Universal Flash Storage Host Controller Interface (UFSHCI) 2.1 driver based
 * on the JEDEC JESD223C.pdf and JESD220C-2_1.pdf specifications.
 */

#include "kstat.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/queue.h>
#include <sys/mutex.h>
#include <sys/pool.h>
#include <sys/kstat.h>

#include <sys/atomic.h>

#include <machine/bus.h>

#include <scsi/scsi_all.h>
#include <scsi/scsi_disk.h>
#include <scsi/scsiconf.h>

#include <dev/ic/ufshcivar.h>
#include <dev/ic/ufshcireg.h>

#ifdef HIBERNATE
#include <uvm/uvm_extern.h>
#include <sys/hibernate.h>
#include <sys/disklabel.h>
#include <sys/disk.h>
#endif

#ifdef UFSHCI_DEBUG
int ufshci_debug = 1;
#endif

struct cfdriver ufshci_cd = {
	NULL, "ufshci", DV_DULL
};

int			 ufshci_reset(struct ufshci_softc *);
int			 ufshci_is_poll(struct ufshci_softc *, uint32_t);
struct ufshci_dmamem	*ufshci_dmamem_alloc(struct ufshci_softc *, size_t);
void			 ufshci_dmamem_free(struct ufshci_softc *,
			     struct ufshci_dmamem *);
int			 ufshci_alloc(struct ufshci_softc *);
int			 ufshci_init(struct ufshci_softc *);
void			 ufshci_disable(struct ufshci_softc *);
int			 ufshci_doorbell_read(struct ufshci_softc *);
void			 ufshci_doorbell_write(struct ufshci_softc *, int);
int			 ufshci_doorbell_poll(struct ufshci_softc *, int,
			     uint32_t);
int			 ufshci_utr_cmd_nop(struct ufshci_softc *,
			     struct ufshci_ccb *, struct scsi_xfer *);
int			 ufshci_utr_cmd_lun(struct ufshci_softc *,
			     struct ufshci_ccb *, struct scsi_xfer *);
int			 ufshci_utr_cmd_inquiry(struct ufshci_softc *,
			     struct ufshci_ccb *, struct scsi_xfer *);
int			 ufshci_utr_cmd_capacity16(struct ufshci_softc *,
			     struct ufshci_ccb *, struct scsi_xfer *);
int			 ufshci_utr_cmd_capacity(struct ufshci_softc *,
			     struct ufshci_ccb *, struct scsi_xfer *);
int			 ufshci_utr_cmd_io(struct ufshci_softc *,
			     struct ufshci_ccb *, struct scsi_xfer *, int);
int			 ufshci_utr_cmd_sync(struct ufshci_softc *,
			     struct ufshci_ccb *, struct scsi_xfer *,
			     uint32_t, uint16_t);
void			 ufshci_xfer_complete(struct ufshci_softc *);

/* SCSI */
int			 ufshci_ccb_alloc(struct ufshci_softc *, int);
void			*ufshci_ccb_get(void *);
void			 ufshci_ccb_put(void *, void *);
void			 ufshci_ccb_free(struct ufshci_softc*, int);

void			 ufshci_scsi_cmd(struct scsi_xfer *);

void			 ufshci_scsi_inquiry(struct scsi_xfer *);
void			 ufshci_scsi_capacity16(struct scsi_xfer *);
void			 ufshci_scsi_capacity(struct scsi_xfer *);
void			 ufshci_scsi_sync(struct scsi_xfer *);
void			 ufshci_scsi_io(struct scsi_xfer *, int);
void			 ufshci_scsi_io_done(struct ufshci_softc *,
			     struct ufshci_ccb *);
void			 ufshci_scsi_done(struct ufshci_softc *,
			     struct ufshci_ccb *);

#ifdef HIBERNATE
int			 ufshci_hibernate_io(dev_t, daddr_t, vaddr_t, size_t,
			     int, void *);
#endif

#if NKSTAT > 0
void			 ufshci_kstat_attach(struct ufshci_softc *);
int			 ufshci_kstat_read_ccb(struct kstat *);
int			 ufshci_kstat_read_slot(struct kstat *);
#endif

const struct scsi_adapter ufshci_switch = {
	ufshci_scsi_cmd, NULL, NULL, NULL, NULL
};

int
ufshci_intr(void *arg)
{
	struct ufshci_softc *sc = arg;
	uint32_t status, hcs;
	int handled = 0;

	status = UFSHCI_READ_4(sc, UFSHCI_REG_IS);

	if (status == 0)
		return handled;

	/* ACK interrupt */
	UFSHCI_WRITE_4(sc, UFSHCI_REG_IS, status);

	if (status & UFSHCI_REG_IS_UCCS) {
		handled = 1;
	}
	if (status & UFSHCI_REG_IS_UTRCS) {
		ufshci_xfer_complete(sc);

		handled = 1;
	}
	/* If Auto-Hibernate raises an interrupt, it's to yield an error. */
	if (status & UFSHCI_REG_IS_UHES) {
		hcs = UFSHCI_READ_4(sc, UFSHCI_REG_HCS);
		printf("%s: Auto-Hibernate enter error UPMCRS=0x%x\n",
		    __func__, UFSHCI_REG_HCS_UPMCRS(hcs));
		handled = 1;
	}
	if (status & UFSHCI_REG_IS_UHXS) {
		hcs = UFSHCI_READ_4(sc, UFSHCI_REG_HCS);
		printf("%s: Auto-Hibernate exit error UPMCRS=0x%x\n",
		    __func__, UFSHCI_REG_HCS_UPMCRS(hcs));
		handled = 1;
	}

	if (handled == 0) {
		printf("%s: UNKNOWN interrupt, status=0x%08x\n",
		    sc->sc_dev.dv_xname, status);
	}

	return handled;
}

int
ufshci_attach(struct ufshci_softc *sc)
{
	struct scsibus_attach_args saa;

	mtx_init(&sc->sc_cmd_mtx, IPL_BIO);
	mtx_init(&sc->sc_ccb_mtx, IPL_BIO);
	SIMPLEQ_INIT(&sc->sc_ccb_list);
	scsi_iopool_init(&sc->sc_iopool, sc, ufshci_ccb_get, ufshci_ccb_put);

	if (ufshci_reset(sc))
		return 1;

	sc->sc_ver = UFSHCI_READ_4(sc, UFSHCI_REG_VER);
	printf(", UFSHCI %d.%d%d\n",
	    UFSHCI_REG_VER_MAJOR(sc->sc_ver),
	    UFSHCI_REG_VER_MINOR(sc->sc_ver),
	    UFSHCI_REG_VER_SUFFIX(sc->sc_ver));

	sc->sc_cap = UFSHCI_READ_4(sc, UFSHCI_REG_CAP);
	sc->sc_hcpid = UFSHCI_READ_4(sc, UFSHCI_REG_HCPID);
	sc->sc_hcmid = UFSHCI_READ_4(sc, UFSHCI_REG_HCMID);
	sc->sc_nutmrs = UFSHCI_REG_CAP_NUTMRS(sc->sc_cap) + 1;
	sc->sc_rtt = UFSHCI_REG_CAP_RTT(sc->sc_cap) + 1;
	sc->sc_nutrs = UFSHCI_REG_CAP_NUTRS(sc->sc_cap) + 1;

#ifdef UFSHCI_DEBUG
	printf("Capabilities (0x%08x):\n", sc->sc_cap);
	printf(" CS=%d\n", sc->sc_cap & UFSHCI_REG_CAP_CS ? 1 : 0);
	printf(" UICDMETMS=%d\n", sc->sc_cap & UFSHCI_REG_CAP_UICDMETMS ? 1 :0);
	printf(" OODDS=%d\n", sc->sc_cap & UFSHCI_REG_CAP_OODDS ? 1 : 0);
	printf(" 64AS=%d\n", sc->sc_cap & UFSHCI_REG_CAP_64AS ? 1 : 0);
	printf(" AUTOH8=%d\n", sc->sc_cap & UFSHCI_REG_AUTOH8 ? 1 : 0);
	printf(" NUTMRS=%d\n", sc->sc_nutmrs);
	printf(" RTT=%d\n", sc->sc_rtt);
	printf(" NUTRS=%d\n", sc->sc_nutrs);
	printf(" HCPID=0x%08x\n", sc->sc_hcpid);
	printf("HCMID (0x%08x):\n", sc->sc_hcmid);
	printf(" BI=0x%04x\n", UFSHCI_REG_HCMID_BI(sc->sc_hcmid));
	printf(" MIC=0x%04x\n", UFSHCI_REG_HCMID_MIC(sc->sc_hcmid));
#endif
	if (sc->sc_nutrs < UFSHCI_SLOTS_MIN ||
	    sc->sc_nutrs > UFSHCI_SLOTS_MAX) {
		printf("%s: Invalid NUTRS value %d (must be %d-%d)!\n",
		    sc->sc_dev.dv_xname, sc->sc_nutrs,
		    UFSHCI_SLOTS_MIN, UFSHCI_SLOTS_MAX);
		return 1;
	}
	if (sc->sc_nutrs == UFSHCI_SLOTS_MAX)
		sc->sc_iacth = UFSHCI_INTR_AGGR_COUNT_MAX;
	else
		sc->sc_iacth = sc->sc_nutrs;
	DPRINTF(1, "Intr. aggr. counter threshold:\nIACTH=%d\n", sc->sc_iacth);

	/*
	 * XXX:
	 * At the moment normal interrupts work better for us than interrupt
	 * aggregation, because:
	 *
	 * 	1. With interrupt aggregation enabled, the I/O performance
	 *	   isn't better, but even slightly worse depending on the
	 *	   UFS controller and architecture.
	 *	2. With interrupt aggregation enabled we currently see
	 *	   intermittent SCSI command stalling.  Probably there is a
	 *	   race condition where new SCSI commands are getting
	 *	   scheduled, while we miss to reset the interrupt aggregation
	 *	   counter/timer, which leaves us with no more interrupts
	 *	   triggered.  This needs to be fixed, but I couldn't figure
	 *	   out yet how.
	 */
#if 0
	sc->sc_flags |= UFSHCI_FLAGS_AGGR_INTR;	/* Enable intr. aggregation */
#endif
	/* Allocate the DMA buffers and initialize the controller. */
	if (ufshci_alloc(sc))
		return 1;
	if (ufshci_init(sc))
		return 1;

	if (ufshci_ccb_alloc(sc, sc->sc_nutrs) != 0) {
		printf("%s: %s: Can't allocate CCBs\n",
		    sc->sc_dev.dv_xname, __func__);
		return 1;
	}

	/* Enable Auto-Hibernate Idle Timer (AHIT) and set it to 150ms. */
	if (sc->sc_cap & UFSHCI_REG_AUTOH8) {
		UFSHCI_WRITE_4(sc, UFSHCI_REG_AHIT,
		    UFSHCI_REG_AHIT_TS(UFSHCI_REG_AHIT_TS_1MS) | 150);
	}

	/* Attach to SCSI layer */
	saa.saa_adapter = &ufshci_switch;
	saa.saa_adapter_softc = sc;
	saa.saa_adapter_buswidth = UFSHCI_TARGETS_MAX + 1;
	saa.saa_luns = 1;
	saa.saa_adapter_target = 0;
	saa.saa_openings = sc->sc_nutrs;
	saa.saa_pool = &sc->sc_iopool;
	saa.saa_quirks = saa.saa_flags = 0;
	saa.saa_wwpn = saa.saa_wwnn = 0;
#if NKSTAT > 0
	ufshci_kstat_attach(sc);
#endif
	config_found(&sc->sc_dev, &saa, scsiprint);

	return 0;
}

int
ufshci_reset(struct ufshci_softc *sc)
{
	int i;
	int retry = 10;
	uint32_t hce;

	/*
	 * 7.1.1 Host Controller Initialization: 2)
	 * Reset and enable host controller
	 */
	UFSHCI_WRITE_4(sc, UFSHCI_REG_HCE, UFSHCI_REG_HCE_HCE);

	/* 7.1.1 Host Controller Initialization: 3) */
	for (i = 0; i < retry; i++) {
		hce = UFSHCI_READ_4(sc, UFSHCI_REG_HCE);
		if (hce == 1)
			break;
		delay(1);
	}
	if (i == retry) {
		printf("%s: Enabling Host Controller failed!\n",
		    sc->sc_dev.dv_xname);
		return 1;
	}

	DPRINTF(2, "\n%s: Host Controller enabled (i=%d)\n", __func__, i);

	return 0;
}

int
ufshci_is_poll(struct ufshci_softc *sc, uint32_t type)
{
	uint32_t status;
	int i, retry = 25;

	for (i = 0; i < retry; i++) {
		status = UFSHCI_READ_4(sc, UFSHCI_REG_IS);
		if (status & type)
			break;
		delay(10);
	}
	if (i == retry) {
		printf("%s: %s: timeout\n", sc->sc_dev.dv_xname, __func__);
		return 1;
	}
	DPRINTF(3, "%s: completed after %d retries\n", __func__, i);

	/* ACK interrupt */
	UFSHCI_WRITE_4(sc, UFSHCI_REG_IS, status);

	return 0;
}

struct ufshci_dmamem *
ufshci_dmamem_alloc(struct ufshci_softc *sc, size_t size)
{
	struct ufshci_dmamem *udm;
	int nsegs;

	udm = malloc(sizeof(*udm), M_DEVBUF, M_WAITOK | M_ZERO);
	if (udm == NULL)
		return NULL;

	udm->udm_size = size;

	if (bus_dmamap_create(sc->sc_dmat, size, 1, size, 0,
	    BUS_DMA_WAITOK | BUS_DMA_ALLOCNOW |
	    ((sc->sc_cap & UFSHCI_REG_CAP_64AS) ? BUS_DMA_64BIT : 0),
	    &udm->udm_map) != 0)
		goto udmfree;

	if (bus_dmamem_alloc(sc->sc_dmat, size, PAGE_SIZE, 0, &udm->udm_seg,
	    1, &nsegs, BUS_DMA_WAITOK | BUS_DMA_ZERO) != 0)
		goto destroy;

	if (bus_dmamem_map(sc->sc_dmat, &udm->udm_seg, nsegs, size,
	    &udm->udm_kva, BUS_DMA_WAITOK) != 0)
		goto free;

	if (bus_dmamap_load(sc->sc_dmat, udm->udm_map, udm->udm_kva, size,
	    NULL, BUS_DMA_WAITOK) != 0)
		goto unmap;

	DPRINTF(2, "%s: size=%lu, page_size=%d, nsegs=%d\n",
	    __func__, size, PAGE_SIZE, nsegs);

	return udm;

unmap:
	bus_dmamem_unmap(sc->sc_dmat, udm->udm_kva, size);
free:
	bus_dmamem_free(sc->sc_dmat, &udm->udm_seg, 1);
destroy:
	bus_dmamap_destroy(sc->sc_dmat, udm->udm_map);
udmfree:
	free(udm, M_DEVBUF, sizeof(*udm));

	return NULL;
}

void
ufshci_dmamem_free(struct ufshci_softc *sc, struct ufshci_dmamem *udm)
{
	bus_dmamap_unload(sc->sc_dmat, udm->udm_map);
	bus_dmamem_unmap(sc->sc_dmat, udm->udm_kva, udm->udm_size);
	bus_dmamem_free(sc->sc_dmat, &udm->udm_seg, 1);
	bus_dmamap_destroy(sc->sc_dmat, udm->udm_map);
	free(udm, M_DEVBUF, sizeof(*udm));
}

int
ufshci_alloc(struct ufshci_softc *sc)
{
	/* 7.1.1 Host Controller Initialization: 13) */
	sc->sc_dmamem_utmrd = ufshci_dmamem_alloc(sc,
	    sizeof(struct ufshci_utmrd) * sc->sc_nutmrs);
	if (sc->sc_dmamem_utmrd == NULL) {
		printf("%s: Can't allocate DMA memory for UTMRD\n",
		    sc->sc_dev.dv_xname);
		return 1;
	}

	/* 7.1.1 Host Controller Initialization: 15) */
	sc->sc_dmamem_utrd = ufshci_dmamem_alloc(sc,
	    sizeof(struct ufshci_utrd) * sc->sc_nutrs);
	if (sc->sc_dmamem_utrd == NULL) {
		printf("%s: Can't allocate DMA memory for UTRD\n",
		    sc->sc_dev.dv_xname);
		return 1;
	}

	/* Allocate UCDs. */
	sc->sc_dmamem_ucd = ufshci_dmamem_alloc(sc,
	    sizeof(struct ufshci_ucd) * sc->sc_nutrs);
	if (sc->sc_dmamem_ucd == NULL) {
		printf("%s: Can't allocate DMA memory for UCD\n",
		    sc->sc_dev.dv_xname);
		return 1;
	}

	return 0;
}

int
ufshci_init(struct ufshci_softc *sc)
{
	uint32_t reg;
	uint64_t dva;

	/*
	 * 7.1.1 Host Controller Initialization: 4)
	 * TODO: Do we need to set DME_SET?
	 */

	/* 7.1.1 Host Controller Initialization: 5) */
	if (sc->sc_cap & UFSHCI_REG_AUTOH8) {
		UFSHCI_WRITE_4(sc, UFSHCI_REG_IE,
		    UFSHCI_REG_IE_UTRCE | UFSHCI_REG_IE_UTMRCE |
		    UFSHCI_REG_IS_UHES | UFSHCI_REG_IS_UHXS);
	} else {
		UFSHCI_WRITE_4(sc, UFSHCI_REG_IE,
		    UFSHCI_REG_IE_UTRCE | UFSHCI_REG_IE_UTMRCE);
	}

	/* 7.1.1 Host Controller Initialization: 6) */
	UFSHCI_WRITE_4(sc, UFSHCI_REG_UICCMD,
	    UFSHCI_REG_UICCMD_CMDOP_DME_LINKSTARTUP);
	if (ufshci_is_poll(sc, UFSHCI_REG_IS_UCCS))
		return 1;

	/*
	 * 7.1.1 Host Controller Initialization: 7), 8), 9)
	 * TODO: Implement retry in case UFSHCI_REG_HCS returns 0
	 */
	reg = UFSHCI_READ_4(sc, UFSHCI_REG_HCS);
	if (reg & UFSHCI_REG_HCS_DP)
		DPRINTF(2, "%s: Device Presence SET\n", __func__);
	else
		DPRINTF(2, "%s: Device Presence NOT SET\n", __func__);

	/*
	 * 7.1.1 Host Controller Initialization: 10)
	 * TODO: Enable additional interrupt on the IE register
	 */

	/* 7.1.1 Host Controller Initialization: 11) */
	if (sc->sc_flags & UFSHCI_FLAGS_AGGR_INTR) {
		UFSHCI_WRITE_4(sc, UFSHCI_REG_UTRIACR,
		    UFSHCI_REG_UTRIACR_IAEN |
		    UFSHCI_REG_UTRIACR_IAPWEN |
		    UFSHCI_REG_UTRIACR_CTR |
		    UFSHCI_REG_UTRIACR_IACTH(sc->sc_iacth) |
		    UFSHCI_REG_UTRIACR_IATOVAL(UFSHCI_INTR_AGGR_TIMEOUT));
	} else {
		UFSHCI_WRITE_4(sc, UFSHCI_REG_UTRIACR, 0);
	}

	/*
	 * 7.1.1 Host Controller Initialization: 12)
	 * TODO: More UIC commands to issue?
	 */

	/* 7.1.1 Host Controller Initialization: 14) */
	dva = UFSHCI_DMA_DVA(sc->sc_dmamem_utmrd);
	DPRINTF(2, "%s: utmrd dva=%llu\n", __func__, dva);
	UFSHCI_WRITE_4(sc, UFSHCI_REG_UTMRLBA, (uint32_t)dva);
	UFSHCI_WRITE_4(sc, UFSHCI_REG_UTMRLBAU, (uint32_t)(dva >> 32));

	/* 7.1.1 Host Controller Initialization: 16) */
	dva = UFSHCI_DMA_DVA(sc->sc_dmamem_utrd);
	DPRINTF(2, "%s: utrd dva=%llu\n", __func__, dva);
	UFSHCI_WRITE_4(sc, UFSHCI_REG_UTRLBA, (uint32_t)dva);
	UFSHCI_WRITE_4(sc, UFSHCI_REG_UTRLBAU, (uint32_t)(dva >> 32));

	/* 7.1.1 Host Controller Initialization: 17) */
	UFSHCI_WRITE_4(sc, UFSHCI_REG_UTMRLRSR, UFSHCI_REG_UTMRLRSR_START);

	/* 7.1.1 Host Controller Initialization: 18) */
	UFSHCI_WRITE_4(sc, UFSHCI_REG_UTRLRSR, UFSHCI_REG_UTRLRSR_START);

	/* 7.1.1 Host Controller Initialization: 19) */
	/* TODO: bMaxNumOfRTT will be set as the minimum value of
	 * bDeviceRTTCap and NORTT. ???
	 */

	return 0;
}

void
ufshci_disable(struct ufshci_softc *sc)
{
	/* Stop run queues. */
	UFSHCI_WRITE_4(sc, UFSHCI_REG_UTMRLRSR, UFSHCI_REG_UTMRLRSR_STOP);
	UFSHCI_WRITE_4(sc, UFSHCI_REG_UTRLRSR, UFSHCI_REG_UTRLRSR_STOP);

	/* Disable interrupts. */
	UFSHCI_WRITE_4(sc, UFSHCI_REG_IE, 0);
}

int
ufshci_doorbell_read(struct ufshci_softc *sc)
{
	uint32_t reg;

	reg = UFSHCI_READ_4(sc, UFSHCI_REG_UTRLDBR);

	return reg;
}

void
ufshci_doorbell_write(struct ufshci_softc *sc, int slot)
{
	uint32_t reg;

	reg = (1U << slot);

	UFSHCI_WRITE_4(sc, UFSHCI_REG_UTRLDBR, reg);
}

int
ufshci_doorbell_poll(struct ufshci_softc *sc, int slot, uint32_t timeout_ms)
{
	uint32_t reg;
	uint64_t timeout_us;

	for (timeout_us = timeout_ms * 1000; timeout_us != 0;
	    timeout_us -= 1000) {
		reg = UFSHCI_READ_4(sc, UFSHCI_REG_UTRLDBR);
		if ((reg & (1U << slot)) == 0)
			break;
		delay(1000);
	}
	if (timeout_us == 0) {
		printf("%s: %s: timeout\n", sc->sc_dev.dv_xname, __func__);
		return 1;
	}

	return 0;
}

int
ufshci_utr_cmd_nop(struct ufshci_softc *sc, struct ufshci_ccb *ccb,
    struct scsi_xfer *xs)
{
	int slot, off, len;
	uint64_t dva;
	struct ufshci_utrd *utrd;
	struct ufshci_ucd *ucd;

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 1) */
	slot = ccb->ccb_slot;
	utrd = UFSHCI_DMA_KVA(sc->sc_dmamem_utrd);
	utrd += slot;
	memset(utrd, 0, sizeof(*utrd));

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 2a) */
	utrd->dw0 = UFSHCI_UTRD_DW0_CT_UFS;

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 2b) */
	utrd->dw0 |= UFSHCI_UTRD_DW0_DD_NO;

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 2c) */
	if (sc->sc_flags & UFSHCI_FLAGS_AGGR_INTR)
		utrd->dw0 |= UFSHCI_UTRD_DW0_I_REG;
	else
		utrd->dw0 |= UFSHCI_UTRD_DW0_I_INT;

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 2d) */
	utrd->dw2 = UFSHCI_UTRD_DW2_OCS_IOV;

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 2e) */
	ucd = UFSHCI_DMA_KVA(sc->sc_dmamem_ucd);
	ucd += slot;
	memset(ucd, 0, sizeof(*ucd));

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 2f) */
	ucd->cmd.hdr.tc = UPIU_TC_I2T_NOP_OUT;
	ucd->cmd.hdr.flags = 0;
	ucd->cmd.hdr.lun = 0;
	ucd->cmd.hdr.task_tag = slot;
	ucd->cmd.hdr.cmd_set_type = 0; /* SCSI command */
	ucd->cmd.hdr.query = 0;
	ucd->cmd.hdr.response = 0;
	ucd->cmd.hdr.status = 0;
	ucd->cmd.hdr.ehs_len = 0;
	ucd->cmd.hdr.device_info = 0;
	ucd->cmd.hdr.ds_len = 0;

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 2g) */
	/* Already done with above memset */

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 3) */
	dva = UFSHCI_DMA_DVA(sc->sc_dmamem_ucd);
	utrd->dw4 = (uint32_t)dva;
	utrd->dw5 = (uint32_t)(dva >> 32);

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 4) */
	off = sizeof(struct upiu_command) / 4; /* DWORD offset */
	utrd->dw6 = UFSHCI_UTRD_DW6_RUO(off);

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 5) */
	len = sizeof(struct upiu_response) / 4; /* DWORD length */
	utrd->dw6 |= UFSHCI_UTRD_DW6_RUL(len);

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 6) */
	off = (sizeof(struct upiu_command) + sizeof(struct upiu_response)) / 4;
	utrd->dw7 = UFSHCI_UTRD_DW7_PRDTO(off);

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 7) */
	utrd->dw7 |= UFSHCI_UTRD_DW7_PRDTL(0); /* No data xfer */

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 9) */
	if (UFSHCI_READ_4(sc, UFSHCI_REG_UTRLRSR) != 1) {
		printf("%s: %s: UTRLRSR not set\n",
		    sc->sc_dev.dv_xname, __func__);
		return 1;
	}

	bus_dmamap_sync(sc->sc_dmat, UFSHCI_DMA_MAP(sc->sc_dmamem_utrd),
	    sizeof(*utrd) * slot, sizeof(*utrd), BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->sc_dmat, UFSHCI_DMA_MAP(sc->sc_dmamem_ucd),
	    sizeof(*ucd) * slot, sizeof(*ucd), BUS_DMASYNC_PREWRITE);

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 14) */
	ccb->ccb_status = CCB_STATUS_INPROGRESS;
	ufshci_doorbell_write(sc, slot);

	return 0;
}

int
ufshci_utr_cmd_lun(struct ufshci_softc *sc, struct ufshci_ccb *ccb,
    struct scsi_xfer *xs)
{
	int slot, off, len, i;
	uint64_t dva;
	struct ufshci_utrd *utrd;
	struct ufshci_ucd *ucd;
	bus_dmamap_t dmap = ccb->ccb_dmamap;

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 1) */
	slot = ccb->ccb_slot;
	utrd = UFSHCI_DMA_KVA(sc->sc_dmamem_utrd);
	utrd += slot;
	memset(utrd, 0, sizeof(*utrd));

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 2a) */
	utrd->dw0 = UFSHCI_UTRD_DW0_CT_UFS;

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 2b) */
	utrd->dw0 |= UFSHCI_UTRD_DW0_DD_T2I;

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 2c) */
        if (sc->sc_flags & UFSHCI_FLAGS_AGGR_INTR)
		utrd->dw0 |= UFSHCI_UTRD_DW0_I_REG;
        else
		utrd->dw0 |= UFSHCI_UTRD_DW0_I_INT;

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 2d) */
	utrd->dw2 = UFSHCI_UTRD_DW2_OCS_IOV;

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 2e) */
	ucd = UFSHCI_DMA_KVA(sc->sc_dmamem_ucd);
	ucd += slot;
	memset(ucd, 0, sizeof(*ucd));

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 2f) */
	ucd->cmd.hdr.tc = UPIU_TC_I2T_COMMAND;
	ucd->cmd.hdr.flags = (1 << 6); /* Bit-5 = Write, Bit-6 = Read */
	ucd->cmd.hdr.lun = 0;
	ucd->cmd.hdr.task_tag = slot;
	ucd->cmd.hdr.cmd_set_type = 0; /* SCSI command */
	ucd->cmd.hdr.query = 0;
	ucd->cmd.hdr.response = 0;
	ucd->cmd.hdr.status = 0;
	ucd->cmd.hdr.ehs_len = 0;
	ucd->cmd.hdr.device_info = 0;
	ucd->cmd.hdr.ds_len = 0;

	ucd->cmd.expected_xfer_len = htobe32(xs->datalen);

	ucd->cmd.cdb[0] = REPORT_LUNS;
	ucd->cmd.cdb[6] = 0;
	ucd->cmd.cdb[7] = 0;
	ucd->cmd.cdb[8] = 0;
	ucd->cmd.cdb[9] = xs->datalen;

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 2g) */
	/* Already done with above memset */

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 3) */
	dva = UFSHCI_DMA_DVA(sc->sc_dmamem_ucd);
	utrd->dw4 = (uint32_t)dva;
	utrd->dw5 = (uint32_t)(dva >> 32);

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 4) */
	off = sizeof(struct upiu_command) / 4; /* DWORD offset */
	utrd->dw6 = UFSHCI_UTRD_DW6_RUO(off);

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 5) */
	len = sizeof(struct upiu_response) / 4; /* DWORD length */
	utrd->dw6 |= UFSHCI_UTRD_DW6_RUL(len);

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 6) */
	off = (sizeof(struct upiu_command) + sizeof(struct upiu_response)) / 4;
	utrd->dw7 = UFSHCI_UTRD_DW7_PRDTO(off);

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 7) */
	utrd->dw7 |= UFSHCI_UTRD_DW7_PRDTL(dmap->dm_nsegs);

	/* Build PRDT data segment. */
	for (i = 0; i < dmap->dm_nsegs; i++) {
		dva = dmap->dm_segs[i].ds_addr;
		ucd->prdt[i].dw0 = (uint32_t)dva;
		ucd->prdt[i].dw1 = (uint32_t)(dva >> 32);
		ucd->prdt[i].dw2 = 0;
		ucd->prdt[i].dw3 = dmap->dm_segs[i].ds_len - 1;
	}

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 9) */
	if (UFSHCI_READ_4(sc, UFSHCI_REG_UTRLRSR) != 1) {
		printf("%s: %s: UTRLRSR not set\n",
		    sc->sc_dev.dv_xname, __func__);
		return 1;
	}

	bus_dmamap_sync(sc->sc_dmat, UFSHCI_DMA_MAP(sc->sc_dmamem_utrd),
	    sizeof(*utrd) * slot, sizeof(*utrd), BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->sc_dmat, UFSHCI_DMA_MAP(sc->sc_dmamem_ucd),
	    sizeof(*ucd) * slot, sizeof(*ucd), BUS_DMASYNC_PREWRITE);

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 14) */
	ccb->ccb_status = CCB_STATUS_INPROGRESS;
	ufshci_doorbell_write(sc, slot);

	return 0;
}

int
ufshci_utr_cmd_inquiry(struct ufshci_softc *sc, struct ufshci_ccb *ccb,
    struct scsi_xfer *xs)
{
	int slot, off, len, i;
	uint64_t dva;
	struct ufshci_utrd *utrd;
	struct ufshci_ucd *ucd;
	bus_dmamap_t dmap = ccb->ccb_dmamap;

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 1) */
	slot = ccb->ccb_slot;
	utrd = UFSHCI_DMA_KVA(sc->sc_dmamem_utrd);
	utrd += slot;
	memset(utrd, 0, sizeof(*utrd));

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 2a) */
	utrd->dw0 = UFSHCI_UTRD_DW0_CT_UFS;

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 2b) */
	utrd->dw0 |= UFSHCI_UTRD_DW0_DD_T2I;

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 2c) */
        if (sc->sc_flags & UFSHCI_FLAGS_AGGR_INTR)
		utrd->dw0 |= UFSHCI_UTRD_DW0_I_REG;
        else
		utrd->dw0 |= UFSHCI_UTRD_DW0_I_INT;

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 2d) */
	utrd->dw2 = UFSHCI_UTRD_DW2_OCS_IOV;

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 2e) */
	ucd = UFSHCI_DMA_KVA(sc->sc_dmamem_ucd);
	ucd += slot;
	memset(ucd, 0, sizeof(*ucd));

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 2f) */
	ucd->cmd.hdr.tc = UPIU_TC_I2T_COMMAND;
	ucd->cmd.hdr.flags = (1 << 6); /* Bit-5 = Write, Bit-6 = Read */
	ucd->cmd.hdr.lun = 0;
	ucd->cmd.hdr.task_tag = slot;
	ucd->cmd.hdr.cmd_set_type = 0; /* SCSI command */
	ucd->cmd.hdr.query = 0;
	ucd->cmd.hdr.response = 0;
	ucd->cmd.hdr.status = 0;
	ucd->cmd.hdr.ehs_len = 0;
	ucd->cmd.hdr.device_info = 0;
	ucd->cmd.hdr.ds_len = 0;

	ucd->cmd.expected_xfer_len = htobe32(xs->datalen);

	ucd->cmd.cdb[0] = INQUIRY; /* 0x12 */
	ucd->cmd.cdb[3] = 0;
	ucd->cmd.cdb[4] = xs->datalen;

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 2g) */
	/* Already done with above memset */

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 3) */
	dva = UFSHCI_DMA_DVA(sc->sc_dmamem_ucd) + (sizeof(*ucd) * slot);
	utrd->dw4 = (uint32_t)dva;
	utrd->dw5 = (uint32_t)(dva >> 32);

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 4) */
	off = sizeof(struct upiu_command) / 4; /* DWORD offset */
	utrd->dw6 = UFSHCI_UTRD_DW6_RUO(off);

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 5) */
	len = sizeof(struct upiu_response) / 4; /* DWORD length */
	utrd->dw6 |= UFSHCI_UTRD_DW6_RUL(len);

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 6) */
	off = (sizeof(struct upiu_command) + sizeof(struct upiu_response)) / 4;
	utrd->dw7 = UFSHCI_UTRD_DW7_PRDTO(off);

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 7) */
	utrd->dw7 |= UFSHCI_UTRD_DW7_PRDTL(dmap->dm_nsegs);

	/* Build PRDT data segment. */
	for (i = 0; i < dmap->dm_nsegs; i++) {
		dva = dmap->dm_segs[i].ds_addr;
		ucd->prdt[i].dw0 = (uint32_t)dva;
		ucd->prdt[i].dw1 = (uint32_t)(dva >> 32);
		ucd->prdt[i].dw2 = 0;
		ucd->prdt[i].dw3 = dmap->dm_segs[i].ds_len - 1;
	}

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 9) */
	if (UFSHCI_READ_4(sc, UFSHCI_REG_UTRLRSR) != 1) {
		printf("%s: %s: UTRLRSR not set\n",
		    sc->sc_dev.dv_xname, __func__);
		return 1;
	}

	bus_dmamap_sync(sc->sc_dmat, UFSHCI_DMA_MAP(sc->sc_dmamem_utrd),
	    sizeof(*utrd) * slot, sizeof(*utrd), BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->sc_dmat, UFSHCI_DMA_MAP(sc->sc_dmamem_ucd),
	    sizeof(*ucd) * slot, sizeof(*ucd), BUS_DMASYNC_PREWRITE);

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 14) */
	ccb->ccb_status = CCB_STATUS_INPROGRESS;
	ufshci_doorbell_write(sc, slot);

	return 0;
}

int
ufshci_utr_cmd_capacity16(struct ufshci_softc *sc, struct ufshci_ccb *ccb,
    struct scsi_xfer *xs)
{
	int slot, off, len, i;
	uint64_t dva;
	struct ufshci_utrd *utrd;
	struct ufshci_ucd *ucd;
	bus_dmamap_t dmap = ccb->ccb_dmamap;

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 1) */
	slot = ccb->ccb_slot;
	utrd = UFSHCI_DMA_KVA(sc->sc_dmamem_utrd);
	utrd += slot;
	memset(utrd, 0, sizeof(*utrd));

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 2a) */
	utrd->dw0 = UFSHCI_UTRD_DW0_CT_UFS;

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 2b) */
	utrd->dw0 |= UFSHCI_UTRD_DW0_DD_T2I;

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 2c) */
        if (sc->sc_flags & UFSHCI_FLAGS_AGGR_INTR)
		utrd->dw0 |= UFSHCI_UTRD_DW0_I_REG;
        else
		utrd->dw0 |= UFSHCI_UTRD_DW0_I_INT;

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 2d) */
	utrd->dw2 = UFSHCI_UTRD_DW2_OCS_IOV;

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 2e) */
	ucd = UFSHCI_DMA_KVA(sc->sc_dmamem_ucd);
	ucd += slot;
	memset(ucd, 0, sizeof(*ucd));

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 2f) */
	ucd->cmd.hdr.tc = UPIU_TC_I2T_COMMAND;
	ucd->cmd.hdr.flags = (1 << 6); /* Bit-5 = Write, Bit-6 = Read */
	ucd->cmd.hdr.lun = 0;
	ucd->cmd.hdr.task_tag = slot;
	ucd->cmd.hdr.cmd_set_type = 0; /* SCSI command */
	ucd->cmd.hdr.query = 0;
	ucd->cmd.hdr.response = 0;
	ucd->cmd.hdr.status = 0;
	ucd->cmd.hdr.ehs_len = 0;
	ucd->cmd.hdr.device_info = 0;
	ucd->cmd.hdr.ds_len = 0;

	ucd->cmd.expected_xfer_len = htobe32(xs->datalen);

	ucd->cmd.cdb[0] = READ_CAPACITY_16; /* 0x9e */
	ucd->cmd.cdb[1] = 0x10; /* Service Action */
	/* Logical Block Address = 0 for UFS */
	ucd->cmd.cdb[10] = 0;
	ucd->cmd.cdb[11] = 0;
	ucd->cmd.cdb[12] = 0;
	ucd->cmd.cdb[13] = xs->datalen;

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 2g) */
	/* Already done with above memset */

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 3) */
	dva = UFSHCI_DMA_DVA(sc->sc_dmamem_ucd) + (sizeof(*ucd) * slot);
	utrd->dw4 = (uint32_t)dva;
	utrd->dw5 = (uint32_t)(dva >> 32);

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 4) */
	off = sizeof(struct upiu_command) / 4; /* DWORD offset */
	utrd->dw6 = UFSHCI_UTRD_DW6_RUO(off);

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 5) */
	len = sizeof(struct upiu_response) / 4; /* DWORD length */
	utrd->dw6 |= UFSHCI_UTRD_DW6_RUL(len);

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 6) */
	off = (sizeof(struct upiu_command) + sizeof(struct upiu_response)) / 4;
	utrd->dw7 = UFSHCI_UTRD_DW7_PRDTO(off);

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 7) */
	utrd->dw7 |= UFSHCI_UTRD_DW7_PRDTL(dmap->dm_nsegs);

	/* Build PRDT data segment. */
	for (i = 0; i < dmap->dm_nsegs; i++) {
		dva = dmap->dm_segs[i].ds_addr;
		ucd->prdt[i].dw0 = (uint32_t)dva;
		ucd->prdt[i].dw1 = (uint32_t)(dva >> 32);
		ucd->prdt[i].dw2 = 0;
		ucd->prdt[i].dw3 = dmap->dm_segs[i].ds_len - 1;
	}

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 9) */
	if (UFSHCI_READ_4(sc, UFSHCI_REG_UTRLRSR) != 1) {
		printf("%s: %s: UTRLRSR not set\n",
		    sc->sc_dev.dv_xname, __func__);
		return 1;
	}

	bus_dmamap_sync(sc->sc_dmat, UFSHCI_DMA_MAP(sc->sc_dmamem_utrd),
	    sizeof(*utrd) * slot, sizeof(*utrd), BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->sc_dmat, UFSHCI_DMA_MAP(sc->sc_dmamem_ucd),
	    sizeof(*ucd) * slot, sizeof(*ucd), BUS_DMASYNC_PREWRITE);

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 14) */
	ccb->ccb_status = CCB_STATUS_INPROGRESS;
	ufshci_doorbell_write(sc, slot);

	return 0;
}

int
ufshci_utr_cmd_capacity(struct ufshci_softc *sc, struct ufshci_ccb *ccb,
    struct scsi_xfer *xs)
{
	int slot, off, len, i;
	uint64_t dva;
	struct ufshci_utrd *utrd;
	struct ufshci_ucd *ucd;
	bus_dmamap_t dmap = ccb->ccb_dmamap;

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 1) */
	slot = ccb->ccb_slot;
	utrd = UFSHCI_DMA_KVA(sc->sc_dmamem_utrd);
	utrd += slot;
	memset(utrd, 0, sizeof(*utrd));

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 2a) */
	utrd->dw0 = UFSHCI_UTRD_DW0_CT_UFS;

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 2b) */
	utrd->dw0 |= UFSHCI_UTRD_DW0_DD_T2I;

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 2c) */
        if (sc->sc_flags & UFSHCI_FLAGS_AGGR_INTR)
		utrd->dw0 |= UFSHCI_UTRD_DW0_I_REG;
        else
		utrd->dw0 |= UFSHCI_UTRD_DW0_I_INT;

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 2d) */
	utrd->dw2 = UFSHCI_UTRD_DW2_OCS_IOV;

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 2e) */
	ucd = UFSHCI_DMA_KVA(sc->sc_dmamem_ucd);
	ucd += slot;
	memset(ucd, 0, sizeof(*ucd));

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 2f) */
	ucd->cmd.hdr.tc = UPIU_TC_I2T_COMMAND;
	ucd->cmd.hdr.flags = (1 << 6); /* Bit-5 = Write, Bit-6 = Read */
	ucd->cmd.hdr.lun = 0;
	ucd->cmd.hdr.task_tag = slot;
	ucd->cmd.hdr.cmd_set_type = 0; /* SCSI command */
	ucd->cmd.hdr.query = 0;
	ucd->cmd.hdr.response = 0;
	ucd->cmd.hdr.status = 0;
	ucd->cmd.hdr.ehs_len = 0;
	ucd->cmd.hdr.device_info = 0;
	ucd->cmd.hdr.ds_len = 0;

	ucd->cmd.expected_xfer_len = htobe32(xs->datalen);

	ucd->cmd.cdb[0] = READ_CAPACITY; /* 0x25 */
	/* Logical Block Address = 0 for UFS */
	ucd->cmd.cdb[2] = 0;
	ucd->cmd.cdb[3] = 0;
	ucd->cmd.cdb[4] = 0;
	ucd->cmd.cdb[5] = 0;

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 2g) */
	/* Already done with above memset */

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 3) */
	dva = UFSHCI_DMA_DVA(sc->sc_dmamem_ucd) + (sizeof(*ucd) * slot);
	utrd->dw4 = (uint32_t)dva;
	utrd->dw5 = (uint32_t)(dva >> 32);

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 4) */
	off = sizeof(struct upiu_command) / 4; /* DWORD offset */
	utrd->dw6 = UFSHCI_UTRD_DW6_RUO(off);

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 5) */
	len = sizeof(struct upiu_response) / 4; /* DWORD length */
	utrd->dw6 |= UFSHCI_UTRD_DW6_RUL(len);

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 6) */
	off = (sizeof(struct upiu_command) + sizeof(struct upiu_response)) / 4;
	utrd->dw7 = UFSHCI_UTRD_DW7_PRDTO(off);

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 7) */
	utrd->dw7 |= UFSHCI_UTRD_DW7_PRDTL(dmap->dm_nsegs);

	/* Build PRDT data segment. */
	for (i = 0; i < dmap->dm_nsegs; i++) {
		dva = dmap->dm_segs[i].ds_addr;
		ucd->prdt[i].dw0 = (uint32_t)dva;
		ucd->prdt[i].dw1 = (uint32_t)(dva >> 32);
		ucd->prdt[i].dw2 = 0;
		ucd->prdt[i].dw3 = dmap->dm_segs[i].ds_len - 1;
	}

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 9) */
	if (UFSHCI_READ_4(sc, UFSHCI_REG_UTRLRSR) != 1) {
		printf("%s: %s: UTRLRSR not set\n",
		    sc->sc_dev.dv_xname, __func__);
		return 1;
	}

	bus_dmamap_sync(sc->sc_dmat, UFSHCI_DMA_MAP(sc->sc_dmamem_utrd),
	    sizeof(*utrd) * slot, sizeof(*utrd), BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->sc_dmat, UFSHCI_DMA_MAP(sc->sc_dmamem_ucd),
	    sizeof(*ucd) * slot, sizeof(*ucd), BUS_DMASYNC_PREWRITE);

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 14) */
	ccb->ccb_status = CCB_STATUS_INPROGRESS;
	ufshci_doorbell_write(sc, slot);

	return 0;
}

int
ufshci_utr_cmd_io(struct ufshci_softc *sc, struct ufshci_ccb *ccb,
    struct scsi_xfer *xs, int dir)
{
	int slot, off, len, i;
	uint64_t dva;
	struct ufshci_utrd *utrd;
	struct ufshci_ucd *ucd;
	bus_dmamap_t dmap = ccb->ccb_dmamap;
	uint32_t blocks;
	uint64_t lba;

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 1) */
	slot = ccb->ccb_slot;
	utrd = UFSHCI_DMA_KVA(sc->sc_dmamem_utrd);
	utrd += slot;
	memset(utrd, 0, sizeof(*utrd));

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 2a) */
	utrd->dw0 = UFSHCI_UTRD_DW0_CT_UFS;

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 2b) */
	if (dir == SCSI_DATA_IN)
		utrd->dw0 |= UFSHCI_UTRD_DW0_DD_T2I;
	else
		utrd->dw0 |= UFSHCI_UTRD_DW0_DD_I2T;

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 2c) */
        if (sc->sc_flags & UFSHCI_FLAGS_AGGR_INTR)
		utrd->dw0 |= UFSHCI_UTRD_DW0_I_REG;
        else
		utrd->dw0 |= UFSHCI_UTRD_DW0_I_INT;

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 2d) */
	utrd->dw2 = UFSHCI_UTRD_DW2_OCS_IOV;

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 2e) */
	ucd = UFSHCI_DMA_KVA(sc->sc_dmamem_ucd);
	ucd += slot;
	memset(ucd, 0, sizeof(*ucd));

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 2f) */
	ucd->cmd.hdr.tc = UPIU_TC_I2T_COMMAND;
	if (dir == SCSI_DATA_IN)
		ucd->cmd.hdr.flags = (1 << 6); /* Bit-6 = Read */
	else
		ucd->cmd.hdr.flags = (1 << 5); /* Bit-5 = Write */
	ucd->cmd.hdr.lun = 0;
	ucd->cmd.hdr.task_tag = slot;
	ucd->cmd.hdr.cmd_set_type = 0; /* SCSI command */
	ucd->cmd.hdr.query = 0;
	ucd->cmd.hdr.response = 0;
	ucd->cmd.hdr.status = 0;
	ucd->cmd.hdr.ehs_len = 0;
	ucd->cmd.hdr.device_info = 0;
	ucd->cmd.hdr.ds_len = 0;

	/*
	 * JESD220C-2_1.pdf, page 88, d) Expected Data Transfer Length:
	 * "When the COMMAND UPIU encodes a SCSI WRITE or SCSI READ command
	 * (specifically WRITE (6), READ (6), WRITE (10), READ (10),
	 * WRITE (16), or READ (16)), the value of this field shall be the
	 * product of the Logical Block Size (bLogicalBlockSize) and the
	 * TRANSFER LENGTH field of the CDB."
	 */
	scsi_cmd_rw_decode(&xs->cmd, &lba, &blocks);
	ucd->cmd.expected_xfer_len = htobe32(UFSHCI_LBS * blocks);

	memcpy(ucd->cmd.cdb, &xs->cmd, sizeof(ucd->cmd.cdb));

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 2g) */
	/* Already done with above memset */

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 3) */
	dva = UFSHCI_DMA_DVA(sc->sc_dmamem_ucd) + (sizeof(*ucd) * slot);
	utrd->dw4 = (uint32_t)dva;
	utrd->dw5 = (uint32_t)(dva >> 32);

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 4) */
	off = sizeof(struct upiu_command) / 4; /* DWORD offset */
	utrd->dw6 = UFSHCI_UTRD_DW6_RUO(off);

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 5) */
	len = sizeof(struct upiu_response) / 4; /* DWORD length */
	utrd->dw6 |= UFSHCI_UTRD_DW6_RUL(len);

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 6) */
	off = (sizeof(struct upiu_command) + sizeof(struct upiu_response)) / 4;
	utrd->dw7 = UFSHCI_UTRD_DW7_PRDTO(off);

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 7) */
	utrd->dw7 |= UFSHCI_UTRD_DW7_PRDTL(dmap->dm_nsegs);

	/* Build PRDT data segment. */
	for (i = 0; i < dmap->dm_nsegs; i++) {
		dva = dmap->dm_segs[i].ds_addr;
		ucd->prdt[i].dw0 = (uint32_t)dva;
		ucd->prdt[i].dw1 = (uint32_t)(dva >> 32);
		ucd->prdt[i].dw2 = 0;
		ucd->prdt[i].dw3 = dmap->dm_segs[i].ds_len - 1;
	}

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 9) */
	if (UFSHCI_READ_4(sc, UFSHCI_REG_UTRLRSR) != 1) {
		printf("%s: %s: UTRLRSR not set\n",
		    sc->sc_dev.dv_xname, __func__);
		return 1;
	}

	bus_dmamap_sync(sc->sc_dmat, UFSHCI_DMA_MAP(sc->sc_dmamem_utrd),
	    sizeof(*utrd) * slot, sizeof(*utrd), BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->sc_dmat, UFSHCI_DMA_MAP(sc->sc_dmamem_ucd),
	    sizeof(*ucd) * slot, sizeof(*ucd), BUS_DMASYNC_PREWRITE);
#if NKSTAT > 0
	if (sc->sc_stats_slots)
		sc->sc_stats_slots[slot]++;
#endif
	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 14) */
	ccb->ccb_status = CCB_STATUS_INPROGRESS;
	ufshci_doorbell_write(sc, slot);

	return 0;
}

int
ufshci_utr_cmd_sync(struct ufshci_softc *sc, struct ufshci_ccb *ccb,
    struct scsi_xfer *xs, uint32_t lba, uint16_t blocks)
{
	int slot, off, len;
	uint64_t dva;
	struct ufshci_utrd *utrd;
	struct ufshci_ucd *ucd;

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 1) */
	slot = ccb->ccb_slot;
	utrd = UFSHCI_DMA_KVA(sc->sc_dmamem_utrd);
	utrd += slot;
	memset(utrd, 0, sizeof(*utrd));

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 2a) */
	utrd->dw0 = UFSHCI_UTRD_DW0_CT_UFS;

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 2b) */
	utrd->dw0 |= UFSHCI_UTRD_DW0_DD_I2T;

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 2c) */
        if (sc->sc_flags & UFSHCI_FLAGS_AGGR_INTR)
		utrd->dw0 |= UFSHCI_UTRD_DW0_I_REG;
        else
		utrd->dw0 |= UFSHCI_UTRD_DW0_I_INT;

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 2d) */
	utrd->dw2 = UFSHCI_UTRD_DW2_OCS_IOV;

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 2e) */
	ucd = UFSHCI_DMA_KVA(sc->sc_dmamem_ucd);
	ucd += slot;
	memset(ucd, 0, sizeof(*ucd));

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 2f) */
	ucd->cmd.hdr.tc = UPIU_TC_I2T_COMMAND;
	ucd->cmd.hdr.flags = 0; /* No data transfer */
	ucd->cmd.hdr.lun = 0;
	ucd->cmd.hdr.task_tag = slot;
	ucd->cmd.hdr.cmd_set_type = 0; /* SCSI command */
	ucd->cmd.hdr.query = 0;
	ucd->cmd.hdr.response = 0;
	ucd->cmd.hdr.status = 0;
	ucd->cmd.hdr.ehs_len = 0;
	ucd->cmd.hdr.device_info = 0;
	ucd->cmd.hdr.ds_len = 0;

	ucd->cmd.expected_xfer_len = htobe32(0); /* No data transfer */

	ucd->cmd.cdb[0] = SYNCHRONIZE_CACHE; /* 0x35 */
	ucd->cmd.cdb[2] = (lba >> 24) & 0xff;
	ucd->cmd.cdb[3] = (lba >> 16) & 0xff;
	ucd->cmd.cdb[4] = (lba >>  8) & 0xff;
	ucd->cmd.cdb[5] = (lba >>  0) & 0xff;
	ucd->cmd.cdb[7] = (blocks >> 8) & 0xff;
	ucd->cmd.cdb[8] = (blocks >> 0) & 0xff;

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 2g) */
	/* Already done with above memset */

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 3) */
	dva = UFSHCI_DMA_DVA(sc->sc_dmamem_ucd) + (sizeof(*ucd) * slot);
	utrd->dw4 = (uint32_t)dva;
	utrd->dw5 = (uint32_t)(dva >> 32);

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 4) */
	off = sizeof(struct upiu_command) / 4; /* DWORD offset */
	utrd->dw6 = UFSHCI_UTRD_DW6_RUO(off);

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 5) */
	len = sizeof(struct upiu_response) / 4; /* DWORD length */
	utrd->dw6 |= UFSHCI_UTRD_DW6_RUL(len);

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 6) */
	off = (sizeof(struct upiu_command) + sizeof(struct upiu_response)) / 4;
	utrd->dw7 = UFSHCI_UTRD_DW7_PRDTO(off);

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 7) */
	utrd->dw7 |= UFSHCI_UTRD_DW7_PRDTL(0); /* No data xfer */

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 9) */
	if (UFSHCI_READ_4(sc, UFSHCI_REG_UTRLRSR) != 1) {
		printf("%s: %s: UTRLRSR not set\n",
		    sc->sc_dev.dv_xname, __func__);
		return 1;
	}

	bus_dmamap_sync(sc->sc_dmat, UFSHCI_DMA_MAP(sc->sc_dmamem_utrd),
	    sizeof(*utrd) * slot, sizeof(*utrd), BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->sc_dmat, UFSHCI_DMA_MAP(sc->sc_dmamem_ucd),
	    sizeof(*ucd) * slot, sizeof(*ucd), BUS_DMASYNC_PREWRITE);

	/* 7.2.1 Basic Steps when Building a UTP Transfer Request: 14) */
	ccb->ccb_status = CCB_STATUS_INPROGRESS;
	ufshci_doorbell_write(sc, slot);

	return 0;
}

void
ufshci_xfer_complete(struct ufshci_softc *sc)
{
	struct ufshci_ccb *ccb;
	uint32_t reg;
	int i, timeout;

	mtx_enter(&sc->sc_cmd_mtx);

	/* Wait for all commands to complete. */
	for (timeout = 5000; timeout != 0; timeout--) {
		reg = ufshci_doorbell_read(sc);
		if (reg == 0)
			break;
		delay(10);
	}
	if (timeout == 0)
		printf("%s: timeout (reg=0x%x)\n", __func__, reg);

	for (i = 0; i < sc->sc_nutrs; i++) {
		ccb = &sc->sc_ccbs[i];

		/* Skip unused CCBs. */
		if (ccb->ccb_status != CCB_STATUS_INPROGRESS)
			continue;

		if (ccb->ccb_done == NULL)
			panic("ccb done wasn't defined");

		/* 7.2.3: Clear completion notification 3b) */
		UFSHCI_WRITE_4(sc, UFSHCI_REG_UTRLCNR, (1U << i));

		/* 7.2.3: Mark software slot for reuse 3c) */
		ccb->ccb_status = CCB_STATUS_READY2FREE;
	}

	/* 7.2.3: Reset Interrupt Aggregation Counter and Timer 4) */
	if (sc->sc_flags & UFSHCI_FLAGS_AGGR_INTR) {
		UFSHCI_WRITE_4(sc, UFSHCI_REG_UTRIACR,
		    UFSHCI_REG_UTRIACR_IAEN | UFSHCI_REG_UTRIACR_CTR);
	}

	mtx_leave(&sc->sc_cmd_mtx);

	/*
	 * Complete the CCB, which will re-schedule new transfers if any are
	 * pending.
	 */
	for (i = 0; i < sc->sc_nutrs; i++) {
		ccb = &sc->sc_ccbs[i];

		/* 7.2.3: Process the transfer by higher OS layer 3a) */
		if (ccb->ccb_status == CCB_STATUS_READY2FREE)
			ccb->ccb_done(sc, ccb);
	}
}

int
ufshci_activate(struct device *self, int act)
{
	struct ufshci_softc *sc = (struct ufshci_softc *)self;
	int rv = 0;

	switch (act) {
	case DVACT_POWERDOWN:
		DPRINTF(1, "%s: POWERDOWN\n", __func__);
		rv = config_activate_children(&sc->sc_dev, act);
		ufshci_disable(sc);
		break;
	case DVACT_RESUME:
		DPRINTF(1, "%s: RESUME\n", __func__);
		rv = ufshci_init(sc);
		if (rv == 0)
			rv = config_activate_children(&sc->sc_dev, act);
		break;
	default:
		rv = config_activate_children(&sc->sc_dev, act);
		break;
	}

	return rv;
}

/* SCSI */

int
ufshci_ccb_alloc(struct ufshci_softc *sc, int nccbs)
{
	struct ufshci_ccb *ccb;
	int i;

	DPRINTF(2, "%s: nccbs=%d, dma_size=%d, dma_nsegs=%d, "
	    "dma_segmaxsize=%d\n",
	    __func__, nccbs, UFSHCI_UCD_PRDT_MAX_XFER, UFSHCI_UCD_PRDT_MAX_SEGS,
	    UFSHCI_UCD_PRDT_MAX_XFER);

	sc->sc_ccbs = mallocarray(nccbs, sizeof(*ccb), M_DEVBUF,
	    M_WAITOK | M_CANFAIL);
	if (sc->sc_ccbs == NULL)
		return 1;

	for (i = 0; i < nccbs; i++) {
		ccb = &sc->sc_ccbs[i];

		if (bus_dmamap_create(sc->sc_dmat, UFSHCI_UCD_PRDT_MAX_XFER,
		    UFSHCI_UCD_PRDT_MAX_SEGS, UFSHCI_UCD_PRDT_MAX_XFER, 0,
		    BUS_DMA_WAITOK | BUS_DMA_ALLOCNOW |
	    	    ((sc->sc_cap & UFSHCI_REG_CAP_64AS) ? BUS_DMA_64BIT : 0),
		    &ccb->ccb_dmamap) != 0)
			goto free_maps;

		ccb->ccb_cookie = NULL;
		ccb->ccb_status = CCB_STATUS_FREE;
		ccb->ccb_slot = i;

		SIMPLEQ_INSERT_TAIL(&sc->sc_ccb_list, ccb, ccb_entry);
	}

	return 0;

free_maps:
	ufshci_ccb_free(sc, nccbs);
	return 1;
}

void *
ufshci_ccb_get(void *cookie)
{
	struct ufshci_softc *sc = cookie;
	struct ufshci_ccb *ccb;

	mtx_enter(&sc->sc_ccb_mtx);
	ccb = SIMPLEQ_FIRST(&sc->sc_ccb_list);
	if (ccb != NULL)
		SIMPLEQ_REMOVE_HEAD(&sc->sc_ccb_list, ccb_entry);
	mtx_leave(&sc->sc_ccb_mtx);

	return ccb;
}

void
ufshci_ccb_put(void *cookie, void *io)
{
	struct ufshci_softc *sc = cookie;
	struct ufshci_ccb *ccb = io;

	mtx_enter(&sc->sc_ccb_mtx);
	SIMPLEQ_INSERT_HEAD(&sc->sc_ccb_list, ccb, ccb_entry);
	mtx_leave(&sc->sc_ccb_mtx);
}

void
ufshci_ccb_free(struct ufshci_softc *sc, int nccbs)
{
	struct ufshci_ccb *ccb;

	while ((ccb = SIMPLEQ_FIRST(&sc->sc_ccb_list)) != NULL) {
		SIMPLEQ_REMOVE_HEAD(&sc->sc_ccb_list, ccb_entry);
		bus_dmamap_destroy(sc->sc_dmat, ccb->ccb_dmamap);
	}

	ufshci_dmamem_free(sc, sc->sc_dmamem_utrd);
	free(sc->sc_ccbs, M_DEVBUF, nccbs * sizeof(*ccb));
}

void
ufshci_scsi_cmd(struct scsi_xfer *xs)
{
	struct scsi_link *link = xs->sc_link;
	struct ufshci_softc *sc = link->bus->sb_adapter_softc;

	mtx_enter(&sc->sc_cmd_mtx);

	switch (xs->cmd.opcode) {

	case READ_COMMAND:
	case READ_10:
	case READ_12:
	case READ_16:
		ufshci_scsi_io(xs, SCSI_DATA_IN);
		break;
	case WRITE_COMMAND:
	case WRITE_10:
	case WRITE_12:
	case WRITE_16:
		ufshci_scsi_io(xs, SCSI_DATA_OUT);
		break;
	case SYNCHRONIZE_CACHE:
		ufshci_scsi_sync(xs);
		break;
	case INQUIRY:
		ufshci_scsi_inquiry(xs);
		break;
	case READ_CAPACITY_16:
		ufshci_scsi_capacity16(xs);
		break;
	case READ_CAPACITY:
		ufshci_scsi_capacity(xs);
		break;
	case TEST_UNIT_READY:
	case PREVENT_ALLOW:
	case START_STOP:
		xs->error = XS_NOERROR;
		scsi_done(xs);
		break;
	default:
		DPRINTF(3, "%s: unhandled scsi command 0x%02x\n",
		    __func__, xs->cmd.opcode);
		xs->error = XS_DRIVER_STUFFUP;
		scsi_done(xs);
		break;
	}

	mtx_leave(&sc->sc_cmd_mtx);
}

void
ufshci_scsi_inquiry(struct scsi_xfer *xs)
{
	struct scsi_link *link = xs->sc_link;
	struct ufshci_softc *sc = link->bus->sb_adapter_softc;
	struct ufshci_ccb *ccb = xs->io;
	bus_dmamap_t dmap = ccb->ccb_dmamap;
	int error;

	DPRINTF(3, "%s: INQUIRY (%s)\n",
	    __func__, ISSET(xs->flags, SCSI_POLL) ? "poll"  : "no poll");

	if (xs->datalen > UPIU_SCSI_RSP_INQUIRY_SIZE) {
		DPRINTF(2, "%s: request len too large\n", __func__);
		goto error1;
	}

	error = bus_dmamap_load(sc->sc_dmat, dmap, xs->data, xs->datalen, NULL,
	    ISSET(xs->flags, SCSI_NOSLEEP) ? BUS_DMA_NOWAIT : BUS_DMA_WAITOK);
	if (error) {
		printf("%s: bus_dmamap_load error=%d\n", __func__, error);
		goto error1;
	}

	bus_dmamap_sync(sc->sc_dmat, dmap, 0, dmap->dm_mapsize,
	    BUS_DMASYNC_PREREAD);

	ccb->ccb_cookie = xs;
	ccb->ccb_done = ufshci_scsi_io_done;

	/* Response length should be UPIU_SCSI_RSP_INQUIRY_SIZE. */
	error = ufshci_utr_cmd_inquiry(sc, ccb, xs);
	if (error)
		goto error2;

	if (ISSET(xs->flags, SCSI_POLL)) {
		if (ufshci_doorbell_poll(sc, ccb->ccb_slot, xs->timeout) == 0) {
			ccb->ccb_done(sc, ccb);
			return;
		}
		goto error2;
        }

	return;

error2:
	bus_dmamap_unload(sc->sc_dmat, dmap);
	ccb->ccb_cookie = NULL;
	ccb->ccb_status = CCB_STATUS_FREE;
	ccb->ccb_done = NULL;
error1:
	xs->error = XS_DRIVER_STUFFUP;
	scsi_done(xs);
}

void
ufshci_scsi_capacity16(struct scsi_xfer *xs)
{
	struct scsi_link *link = xs->sc_link;
	struct ufshci_softc *sc = link->bus->sb_adapter_softc;
	struct ufshci_ccb *ccb = xs->io;
	bus_dmamap_t dmap = ccb->ccb_dmamap;
	int error;

	DPRINTF(3, "%s: CAPACITY16 (%s)\n",
	    __func__, ISSET(xs->flags, SCSI_POLL) ? "poll"  : "no poll");

	if (xs->datalen > UPIU_SCSI_RSP_CAPACITY16_SIZE) {
		DPRINTF(2, "%s: request len too large\n", __func__);
		goto error1;
	}

	error = bus_dmamap_load(sc->sc_dmat, dmap, xs->data, xs->datalen, NULL,
	    ISSET(xs->flags, SCSI_NOSLEEP) ? BUS_DMA_NOWAIT : BUS_DMA_WAITOK);
	if (error) {
		printf("%s: bus_dmamap_load error=%d\n", __func__, error);
		goto error1;
	}

	bus_dmamap_sync(sc->sc_dmat, dmap, 0, dmap->dm_mapsize,
	    BUS_DMASYNC_PREREAD);

	ccb->ccb_cookie = xs;
	ccb->ccb_done = ufshci_scsi_io_done;

	/* Response length should be UPIU_SCSI_RSP_CAPACITY16_SIZE. */
	error = ufshci_utr_cmd_capacity16(sc, ccb, xs);
	if (error)
		goto error2;

	if (ISSET(xs->flags, SCSI_POLL)) {
		if (ufshci_doorbell_poll(sc, ccb->ccb_slot, xs->timeout) == 0) {
			ccb->ccb_done(sc, ccb);
			return;
		}
		goto error2;
	}

	return;

error2:
	bus_dmamap_unload(sc->sc_dmat, dmap);
	ccb->ccb_cookie = NULL;
	ccb->ccb_status = CCB_STATUS_FREE;
	ccb->ccb_done = NULL;
error1:
	xs->error = XS_DRIVER_STUFFUP;
	scsi_done(xs);
}

void
ufshci_scsi_capacity(struct scsi_xfer *xs)
{
	struct scsi_link *link = xs->sc_link;
	struct ufshci_softc *sc = link->bus->sb_adapter_softc;
	struct ufshci_ccb *ccb = xs->io;
	bus_dmamap_t dmap = ccb->ccb_dmamap;
	int error;

	DPRINTF(3, "%s: CAPACITY (%s)\n",
	    __func__, ISSET(xs->flags, SCSI_POLL) ? "poll"  : "no poll");

	if (xs->datalen > UPIU_SCSI_RSP_CAPACITY_SIZE) {
		DPRINTF(2, "%s: request len too large\n", __func__);
		goto error1;
	}

	error = bus_dmamap_load(sc->sc_dmat, dmap, xs->data, xs->datalen, NULL,
	    ISSET(xs->flags, SCSI_NOSLEEP) ? BUS_DMA_NOWAIT : BUS_DMA_WAITOK);
	if (error) {
		printf("%s: bus_dmamap_load error=%d\n", __func__, error);
		goto error1;
        }

	bus_dmamap_sync(sc->sc_dmat, dmap, 0, dmap->dm_mapsize,
	    BUS_DMASYNC_PREREAD);

	ccb->ccb_cookie = xs;
	ccb->ccb_done = ufshci_scsi_io_done;

	/* Response length should be UPIU_SCSI_RSP_CAPACITY_SIZE */
	error = ufshci_utr_cmd_capacity(sc, ccb, xs);
	if (error)
		goto error2;

	if (ISSET(xs->flags, SCSI_POLL)) {
		if (ufshci_doorbell_poll(sc, ccb->ccb_slot, xs->timeout) == 0) {
			ccb->ccb_done(sc, ccb);
			return;
		}
		goto error2;
	}
 
	return;

error2:
	bus_dmamap_unload(sc->sc_dmat, dmap);
	ccb->ccb_cookie = NULL;
	ccb->ccb_status = CCB_STATUS_FREE;
	ccb->ccb_done = NULL;
error1:
	xs->error = XS_DRIVER_STUFFUP;
	scsi_done(xs);
}

void
ufshci_scsi_sync(struct scsi_xfer *xs)
{
	struct scsi_link *link = xs->sc_link;
	struct ufshci_softc *sc = link->bus->sb_adapter_softc;
	struct ufshci_ccb *ccb = xs->io;
	uint64_t lba;
	uint32_t blocks;
	int error;

	/* lba = 0, blocks = 0: Synchronize all logical blocks. */
	lba = 0; blocks = 0;

	DPRINTF(3, "%s: SYNC, lba=%llu, blocks=%u (%s)\n",
	    __func__, lba, blocks,
	    ISSET(xs->flags, SCSI_POLL) ? "poll"  : "no poll");

	ccb->ccb_cookie = xs;
	ccb->ccb_done = ufshci_scsi_done;

	error = ufshci_utr_cmd_sync(sc, ccb, xs, (uint32_t)lba,
	    (uint16_t)blocks);
	if (error)
		goto error;

	if (ISSET(xs->flags, SCSI_POLL)) {
		if (ufshci_doorbell_poll(sc, ccb->ccb_slot, xs->timeout) == 0) {
			ccb->ccb_done(sc, ccb);
			return;
		}
		goto error;
	}

	return;

error:
        ccb->ccb_cookie = NULL;
	ccb->ccb_status = CCB_STATUS_FREE;
        ccb->ccb_done = NULL;

	xs->error = XS_DRIVER_STUFFUP;
	scsi_done(xs);
}

void
ufshci_scsi_io(struct scsi_xfer *xs, int dir)
{
	struct scsi_link *link = xs->sc_link;
	struct ufshci_softc *sc = link->bus->sb_adapter_softc;
	struct ufshci_ccb *ccb = xs->io;
	bus_dmamap_t dmap = ccb->ccb_dmamap;
	int error;

	if ((xs->flags & (SCSI_DATA_IN | SCSI_DATA_OUT)) != dir)
		goto error1;

	error = bus_dmamap_load(sc->sc_dmat, dmap, xs->data, xs->datalen, NULL,
	    ISSET(xs->flags, SCSI_NOSLEEP) ? BUS_DMA_NOWAIT : BUS_DMA_WAITOK);
	if (error) {
		printf("%s: bus_dmamap_load error=%d\n", __func__, error);
		goto error1;
	}

	bus_dmamap_sync(sc->sc_dmat, dmap, 0, dmap->dm_mapsize,
	    ISSET(xs->flags, SCSI_DATA_IN) ? BUS_DMASYNC_PREREAD :
	    BUS_DMASYNC_PREWRITE);

	ccb->ccb_cookie = xs;
	ccb->ccb_done = ufshci_scsi_io_done;

	if (dir == SCSI_DATA_IN)
		error = ufshci_utr_cmd_io(sc, ccb, xs, SCSI_DATA_IN);
	else
		error = ufshci_utr_cmd_io(sc, ccb, xs, SCSI_DATA_OUT);
	if (error)
		goto error2;

	if (ISSET(xs->flags, SCSI_POLL)) {
		if (ufshci_doorbell_poll(sc, ccb->ccb_slot, xs->timeout) == 0) {
			ccb->ccb_done(sc, ccb);
			return;
		}
		goto error2;
	}

	return;

error2:
	bus_dmamap_unload(sc->sc_dmat, dmap);
	ccb->ccb_cookie = NULL;
	ccb->ccb_status = CCB_STATUS_FREE;
	ccb->ccb_done = NULL;
error1:
	xs->error = XS_DRIVER_STUFFUP;
	scsi_done(xs);
}

void
ufshci_scsi_io_done(struct ufshci_softc *sc, struct ufshci_ccb *ccb)
{
	struct scsi_xfer *xs = ccb->ccb_cookie;
	bus_dmamap_t dmap = ccb->ccb_dmamap;
	struct ufshci_ucd *ucd;
	struct ufshci_utrd *utrd;

	bus_dmamap_sync(sc->sc_dmat, dmap, 0, dmap->dm_mapsize,
	    ISSET(xs->flags, SCSI_DATA_IN) ? BUS_DMASYNC_POSTREAD :
	    BUS_DMASYNC_POSTWRITE);

	bus_dmamap_unload(sc->sc_dmat, dmap);

	bus_dmamap_sync(sc->sc_dmat, UFSHCI_DMA_MAP(sc->sc_dmamem_ucd),
	    sizeof(*ucd) * ccb->ccb_slot, sizeof(*ucd),
	    BUS_DMASYNC_POSTWRITE);
	bus_dmamap_sync(sc->sc_dmat, UFSHCI_DMA_MAP(sc->sc_dmamem_utrd),
	    sizeof(*utrd) * ccb->ccb_slot, sizeof(*utrd),
	    BUS_DMASYNC_POSTWRITE);

	/* TODO: Do more checks on the Response UPIU in case of errors? */
	utrd = UFSHCI_DMA_KVA(sc->sc_dmamem_utrd);
	utrd += ccb->ccb_slot;
	ucd = UFSHCI_DMA_KVA(sc->sc_dmamem_ucd);
	ucd += ccb->ccb_slot;
	if (utrd->dw2 != UFSHCI_UTRD_DW2_OCS_SUCCESS) {
		printf("%s: error: slot=%d, ocs=0x%x, rsp-tc=0x%x\n",
		    __func__, ccb->ccb_slot, utrd->dw2, ucd->rsp.hdr.tc);
	}

	ccb->ccb_cookie = NULL;
	ccb->ccb_status = CCB_STATUS_FREE;
	ccb->ccb_done = NULL;

	xs->error = (utrd->dw2 == UFSHCI_UTRD_DW2_OCS_SUCCESS) ?
	    XS_NOERROR : XS_DRIVER_STUFFUP;
	xs->status = SCSI_OK;
	xs->resid = 0;
	scsi_done(xs);
}

void
ufshci_scsi_done(struct ufshci_softc *sc, struct ufshci_ccb *ccb)
{
	struct scsi_xfer *xs = ccb->ccb_cookie;
	struct ufshci_ucd *ucd;
	struct ufshci_utrd *utrd;

	bus_dmamap_sync(sc->sc_dmat, UFSHCI_DMA_MAP(sc->sc_dmamem_ucd),
	    sizeof(*ucd) * ccb->ccb_slot, sizeof(*ucd),
	    BUS_DMASYNC_POSTWRITE);
	bus_dmamap_sync(sc->sc_dmat, UFSHCI_DMA_MAP(sc->sc_dmamem_utrd),
	    sizeof(*utrd) * ccb->ccb_slot, sizeof(*utrd),
	    BUS_DMASYNC_POSTWRITE);

	/* TODO: Do more checks on the Response UPIU in case of errors? */
	utrd = UFSHCI_DMA_KVA(sc->sc_dmamem_utrd);
	utrd += ccb->ccb_slot;
	ucd = UFSHCI_DMA_KVA(sc->sc_dmamem_ucd);
	ucd += ccb->ccb_slot;
	if (utrd->dw2 != UFSHCI_UTRD_DW2_OCS_SUCCESS) {
		printf("%s: error: slot=%d, ocs=0x%x, rsp-tc=0x%x\n",
		    __func__, ccb->ccb_slot, utrd->dw2, ucd->rsp.hdr.tc);
	}

	ccb->ccb_cookie = NULL;
	ccb->ccb_status = CCB_STATUS_FREE;
	ccb->ccb_done = NULL;

	xs->error = (utrd->dw2 == UFSHCI_UTRD_DW2_OCS_SUCCESS) ?
	    XS_NOERROR : XS_DRIVER_STUFFUP;
	xs->status = SCSI_OK;
	xs->resid = 0;
	scsi_done(xs);
}

#ifdef HIBERNATE
int
ufshci_hibernate_io(dev_t dev, daddr_t blkno, vaddr_t addr, size_t size,
    int op, void *page)
{
	struct ufshci_hibernate_page {
		struct ufshci_utrd utrd;
		struct ufshci_ucd ucd;

		struct ufshci_softc *sc;	/* Copy of softc */

		daddr_t	poffset;		/* Start of SWAP partition */
		size_t psize;			/* Size of SWAP partition */
		uint32_t secsize;		/* Our sector size */
	} *my = page;
	paddr_t data_phys, page_phys;
	uint64_t data_bus_phys, page_bus_phys;
	uint64_t timeout_us;
	int off, len, slot;
	uint32_t blocks, reg;
	uint64_t lba;

	if (op == HIB_INIT) {
		struct device *disk;
		struct device *scsibus;
		extern struct cfdriver sd_cd;

		/* Find ufshci softc. */
		disk = disk_lookup(&sd_cd, DISKUNIT(dev));
		if (disk == NULL)
			return ENOTTY;
		scsibus = disk->dv_parent;
		my->sc = (struct ufshci_softc *)disk->dv_parent->dv_parent;

		/* Stop run queues and disable interrupts. */
		ufshci_disable(my->sc);

		/* Tell the controller the new hibernate UTRD address. */
		pmap_extract(pmap_kernel(), (vaddr_t)page, &page_phys);
		page_bus_phys = page_phys + ((void *)&my->utrd - page);
		UFSHCI_WRITE_4(my->sc, UFSHCI_REG_UTRLBA,
		    (uint32_t)page_bus_phys);
		UFSHCI_WRITE_4(my->sc, UFSHCI_REG_UTRLBAU,
		    (uint32_t)(page_bus_phys >> 32));

		/* Start run queues. */
		UFSHCI_WRITE_4(my->sc, UFSHCI_REG_UTMRLRSR,
		    UFSHCI_REG_UTMRLRSR_START);
		UFSHCI_WRITE_4(my->sc, UFSHCI_REG_UTRLRSR,
		    UFSHCI_REG_UTRLRSR_START);

		my->poffset = blkno;
		my->psize = size;
		my->secsize = UFSHCI_LBS;

		return 0;
	}

	if (op != HIB_W)
		return 0;

	if (blkno + (size / DEV_BSIZE) > my->psize)
		return E2BIG;
	blocks = size / my->secsize;
	lba = (blkno + my->poffset) / (my->secsize / DEV_BSIZE);

	/*
	 * The following code is a ripped down version of ufshci_utr_cmd_io()
	 * adapted for hibernate.
	 */
	slot = 0; /* We only use the first slot for hibernate */

	memset(&my->utrd, 0, sizeof(struct ufshci_utrd));

	my->utrd.dw0 = UFSHCI_UTRD_DW0_CT_UFS;
	my->utrd.dw0 |= UFSHCI_UTRD_DW0_DD_I2T;
	my->utrd.dw0 |= UFSHCI_UTRD_DW0_I_REG;
	my->utrd.dw2 = UFSHCI_UTRD_DW2_OCS_IOV;

	memset(&my->ucd, 0, sizeof(struct ufshci_ucd));

	my->ucd.cmd.hdr.tc = UPIU_TC_I2T_COMMAND;
	my->ucd.cmd.hdr.flags = (1 << 5); /* Bit-5 = Write */

	my->ucd.cmd.hdr.lun = 0;
	my->ucd.cmd.hdr.task_tag = slot;
	my->ucd.cmd.hdr.cmd_set_type = 0; /* SCSI command */
	my->ucd.cmd.hdr.query = 0;
	my->ucd.cmd.hdr.response = 0;
	my->ucd.cmd.hdr.status = 0;
	my->ucd.cmd.hdr.ehs_len = 0;
	my->ucd.cmd.hdr.device_info = 0;
	my->ucd.cmd.hdr.ds_len = 0;

	my->ucd.cmd.expected_xfer_len = htobe32(UFSHCI_LBS * blocks);
	my->ucd.cmd.cdb[0] = WRITE_10; /* 0x2a */
	my->ucd.cmd.cdb[1] = (1 << 3); /* FUA: Force Unit Access */
	my->ucd.cmd.cdb[2] = (lba >> 24) & 0xff;
	my->ucd.cmd.cdb[3] = (lba >> 16) & 0xff;
	my->ucd.cmd.cdb[4] = (lba >>  8) & 0xff;
	my->ucd.cmd.cdb[5] = (lba >>  0) & 0xff;
	my->ucd.cmd.cdb[7] = (blocks >> 8) & 0xff;
	my->ucd.cmd.cdb[8] = (blocks >> 0) & 0xff;

	pmap_extract(pmap_kernel(), (vaddr_t)page, &page_phys);
	page_bus_phys = page_phys + ((void *)&my->ucd - page);
	my->utrd.dw4 = (uint32_t)page_bus_phys;
	my->utrd.dw5 = (uint32_t)(page_bus_phys >> 32);

	off = sizeof(struct upiu_command) / 4; /* DWORD offset */
	my->utrd.dw6 = UFSHCI_UTRD_DW6_RUO(off);

	len = sizeof(struct upiu_response) / 4; /* DWORD length */
	my->utrd.dw6 |= UFSHCI_UTRD_DW6_RUL(len);

	off = (sizeof(struct upiu_command) + sizeof(struct upiu_response)) / 4;
	my->utrd.dw7 = UFSHCI_UTRD_DW7_PRDTO(off);

	my->utrd.dw7 |= UFSHCI_UTRD_DW7_PRDTL(1); /* dm_nsegs */

	pmap_extract(pmap_kernel(), (vaddr_t)addr, &data_phys);
	data_bus_phys = data_phys;
	my->ucd.prdt[0].dw0 = (uint32_t)data_bus_phys;
	my->ucd.prdt[0].dw1 = (uint32_t)(data_bus_phys >> 32);
	my->ucd.prdt[0].dw2 = 0;
	my->ucd.prdt[0].dw3 = size - 1; /* ds_len */

	if (UFSHCI_READ_4(my->sc, UFSHCI_REG_UTRLRSR) != 1)
		return EIO;

	ufshci_doorbell_write(my->sc, slot);

	/* ufshci_doorbell_poll() adaption for hibernate. */
	for (timeout_us = 1000000 * 1000; timeout_us != 0;
	    timeout_us -= 1000) {
		reg = UFSHCI_READ_4(my->sc, UFSHCI_REG_UTRLDBR);
		if ((reg & (1U << slot)) == 0)
			break;
		delay(1000);
	}
	if (timeout_us == 0)
		return EIO;
	UFSHCI_WRITE_4(my->sc, UFSHCI_REG_UTRLCNR, (1U << slot));

	/* Check if the command was successfully executed. */
	if (my->utrd.dw2 != UFSHCI_UTRD_DW2_OCS_SUCCESS)
		return EIO;

	return 0;
}
#endif /* HIBERNATE */

#if NKSTAT > 0
struct kstat_kv ufshci_counters_slot[CCB_STATUS_COUNT] = {
	KSTAT_KV_UNIT_INITIALIZER("slots free", KSTAT_KV_T_COUNTER16,
	    KSTAT_KV_U_NONE),
	KSTAT_KV_UNIT_INITIALIZER("slots inpr", KSTAT_KV_T_COUNTER16,
	    KSTAT_KV_U_NONE),
	KSTAT_KV_UNIT_INITIALIZER("slots r2fr", KSTAT_KV_T_COUNTER16,
	    KSTAT_KV_U_NONE),
};

void
ufshci_kstat_attach(struct ufshci_softc *sc)
{
	struct kstat *ks;
	struct kstat_kv *kvs;
	char name[KSTAT_KV_NAMELEN];
	int i;

	/*
	 * Allocate array to count ccb slot utilization.
	 */
	sc->sc_stats_slots = mallocarray(sc->sc_nutrs, sizeof(uint64_t),
	    M_DEVBUF, M_WAITOK | M_ZERO);
	if (sc->sc_stats_slots == NULL) {
		printf("%s: can't allocate stats_slots array\n",
		    sc->sc_dev.dv_xname);
		return;
	}

	/*
	 * Setup 'ccbs' kstat.
	 */
	kvs = mallocarray(sc->sc_nutrs, sizeof(*kvs), M_DEVBUF,
	    M_WAITOK | M_ZERO);
	if (kvs == NULL) {
		printf("%s: can't allocate kvs ccbs array\n",
		    sc->sc_dev.dv_xname);
		return;
	}
	for (i = 0; i < sc->sc_nutrs; i++) {
		snprintf(name, sizeof(name), "slot %d ccbs", i);
		kstat_kv_unit_init(&kvs[i], name, KSTAT_KV_T_COUNTER64,
		    KSTAT_KV_U_NONE);
	}

	mtx_init(&sc->sc_kstat_mtx_ccb, IPL_SOFTCLOCK);

	ks = kstat_create(sc->sc_dev.dv_xname, 0, "ccbs", 0, KSTAT_T_KV, 0);
	if (ks == NULL) {
		printf("%s: can't create ccbs kstats\n", sc->sc_dev.dv_xname);
		free(kvs, M_DEVBUF, sc->sc_nutrs * sizeof(*kvs));
		return;
	}

	kstat_set_mutex(ks, &sc->sc_kstat_mtx_ccb);
	ks->ks_softc = sc;
	ks->ks_data = kvs;
	ks->ks_datalen = sc->sc_nutrs * sizeof(*kvs);
	ks->ks_read = ufshci_kstat_read_ccb;

	sc->sc_kstat_ccb = ks;
	kstat_install(ks);

	/*
	 * Setup 'slots' kstat.
	 */
	mtx_init(&sc->sc_kstat_mtx_slot, IPL_SOFTCLOCK);

	ks = kstat_create(sc->sc_dev.dv_xname, 0, "slots", 0, KSTAT_T_KV, 0);
	if (ks == NULL) {
		printf("%s: can't create slots kstats\n", sc->sc_dev.dv_xname);
		return;
	}

	kstat_set_mutex(ks, &sc->sc_kstat_mtx_slot);
	ks->ks_softc = sc;
	ks->ks_data = ufshci_counters_slot;
	ks->ks_datalen = CCB_STATUS_COUNT * sizeof(*kvs);
	ks->ks_read = ufshci_kstat_read_slot;

	sc->sc_kstat_slot = ks;
	kstat_install(ks);
}

int
ufshci_kstat_read_ccb(struct kstat *ks)
{
	struct ufshci_softc *sc = ks->ks_softc;
	struct kstat_kv *kvs = ks->ks_data;
	int i;

	for (i = 0; i < sc->sc_nutrs; i++)
		kstat_kv_u64(&kvs[i]) = sc->sc_stats_slots[i];

	return 0;
}

int
ufshci_kstat_read_slot(struct kstat *ks)
{
	struct ufshci_softc *sc = ks->ks_softc;
	struct kstat_kv *kvs = ks->ks_data;
	struct ufshci_ccb *ccb;
	uint16_t free, inprogress, ready2free;
	int i;

	free = inprogress = ready2free = 0;

	for (i = 0; i < sc->sc_nutrs; i++) {
		ccb = &sc->sc_ccbs[i];

		switch (ccb->ccb_status) {
		case CCB_STATUS_FREE:
			free++;
			break;
		case CCB_STATUS_INPROGRESS:
			inprogress++;
			break;
		case CCB_STATUS_READY2FREE:
			ready2free++;
			break;
		}
	}

	kstat_kv_u16(&kvs[0]) = free;
	kstat_kv_u16(&kvs[1]) = inprogress;
	kstat_kv_u16(&kvs[2]) = ready2free;

	return 0;
}
#endif /* NKSTAT > 0 */
