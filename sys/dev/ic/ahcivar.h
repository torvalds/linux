/*	$OpenBSD: ahcivar.h,v 1.11 2021/05/30 15:05:33 visa Exp $ */

/*
 * Copyright (c) 2006 David Gwynne <dlg@openbsd.org>
 * Copyright (c) 2010 Conformal Systems LLC <info@conformal.com>
 * Copyright (c) 2010 Jonathan Matthew <jonathan@d14n.org>
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

#include <sys/mutex.h>
#include <sys/timeout.h>
#include <dev/ata/atascsi.h>
#include <dev/ata/pmreg.h>

/* change to AHCI_DEBUG for dmesg spam */
#define NO_AHCI_DEBUG

struct ahci_dmamem {
	bus_dmamap_t		adm_map;
	bus_dma_segment_t	adm_seg;
	size_t			adm_size;
	caddr_t			adm_kva;
};
#define AHCI_DMA_MAP(_adm)	((_adm)->adm_map)
#define AHCI_DMA_DVA(_adm)	((u_int64_t)(_adm)->adm_map->dm_segs[0].ds_addr)
#define AHCI_DMA_KVA(_adm)	((void *)(_adm)->adm_kva)

struct ahci_softc;
struct ahci_port;

struct ahci_ccb {
	/* ATA xfer associated with this CCB.  Must be 1st struct member. */
	struct ata_xfer		ccb_xa;

	int			ccb_slot;
	struct ahci_port	*ccb_port;

	bus_dmamap_t		ccb_dmamap;
	struct ahci_cmd_hdr	*ccb_cmd_hdr;
	struct ahci_cmd_table	*ccb_cmd_table;

	void			(*ccb_done)(struct ahci_ccb *);

	TAILQ_ENTRY(ahci_ccb)	ccb_entry;
};

struct ahci_port {
	struct ahci_softc	*ap_sc;
	bus_space_handle_t	ap_ioh;

#ifdef AHCI_COALESCE
	int			ap_num;
#endif

	struct ahci_rfis	*ap_rfis;
	struct ahci_dmamem	*ap_dmamem_rfis;

	struct ahci_dmamem	*ap_dmamem_cmd_list;
	struct ahci_dmamem	*ap_dmamem_cmd_table;

	volatile u_int32_t	ap_active;
	volatile u_int32_t	ap_active_cnt;
	volatile u_int32_t	ap_sactive;
	volatile u_int32_t	ap_pmp_ncq_port;
	struct ahci_ccb		*ap_ccbs;

	TAILQ_HEAD(, ahci_ccb)	ap_ccb_free;
	TAILQ_HEAD(, ahci_ccb)	ap_ccb_pending;
	struct mutex		ap_ccb_mtx;
	struct ahci_ccb		*ap_ccb_err;

	u_int32_t		ap_state;
#define AP_S_NORMAL			0
#define AP_S_PMP_PROBE			1
#define AP_S_PMP_PORT_PROBE		2
#define AP_S_ERROR_RECOVERY		3
#define AP_S_FATAL_ERROR		4

	int			ap_pmp_ports;
	int			ap_port;
	int			ap_pmp_ignore_ifs;

	/* For error recovery. */
#ifdef DIAGNOSTIC
	int			ap_err_busy;
#endif
	u_int32_t		ap_err_saved_sactive;
	u_int32_t		ap_err_saved_active;
	u_int32_t		ap_err_saved_active_cnt;
	u_int32_t		ap_saved_cmd;

	u_int8_t		*ap_err_scratch;

#ifdef AHCI_DEBUG
	char			ap_name[16];
#define PORTNAME(_ap)	((_ap)->ap_name)
#else
#define PORTNAME(_ap)	DEVNAME((_ap)->ap_sc)
#endif
};

struct ahci_softc {
	struct device		sc_dev;

	void			*sc_ih;

	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	bus_size_t		sc_ios;
	bus_dma_tag_t		sc_dmat;

	int			sc_flags;
#define AHCI_F_NO_NCQ			(1<<0)
#define AHCI_F_IPMS_PROBE		(1<<1)	/* IPMS on failed PMP probe */
#define AHCI_F_NO_PMP			(1<<2)	/* ignore PMP capability */
#define AHCI_F_NO_MSI			(1<<3)	/* disable MSI */

	u_int			sc_ncmds;

	struct ahci_port	*sc_ports[AHCI_MAX_PORTS];

	struct atascsi		*sc_atascsi;

	u_int32_t		sc_cap;

#ifdef AHCI_COALESCE
	u_int32_t		sc_ccc_mask;
	u_int32_t		sc_ccc_ports;
	u_int32_t		sc_ccc_ports_cur;
#endif

	int			(*sc_port_start)(struct ahci_port *, int);
};

#define DEVNAME(_s)		((_s)->sc_dev.dv_xname)
#define ahci_port_start(_p, _f)	((_p)->ap_sc->sc_port_start((_p), (_f)))

int			ahci_attach(struct ahci_softc *);
int			ahci_detach(struct ahci_softc *, int);
int			ahci_activate(struct device *, int);

int			ahci_intr(void *);
