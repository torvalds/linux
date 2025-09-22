/*	$OpenBSD: mpii.c,v 1.148 2024/05/24 06:02:58 jsg Exp $	*/
/*
 * Copyright (c) 2010, 2012 Mike Belopuhov
 * Copyright (c) 2009 James Giannoules
 * Copyright (c) 2005 - 2010 David Gwynne <dlg@openbsd.org>
 * Copyright (c) 2005 - 2010 Marco Peereboom <marco@openbsd.org>
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

#include "bio.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/sensors.h>
#include <sys/dkio.h>
#include <sys/task.h>

#include <machine/bus.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>

#include <dev/biovar.h>

#include <dev/pci/mpiireg.h>

/* #define MPII_DEBUG */
#ifdef MPII_DEBUG
#define DPRINTF(x...)		do { if (mpii_debug) printf(x); } while(0)
#define DNPRINTF(n,x...)	do { if (mpii_debug & (n)) printf(x); } while(0)
#define	MPII_D_CMD		(0x0001)
#define	MPII_D_INTR		(0x0002)
#define	MPII_D_MISC		(0x0004)
#define	MPII_D_DMA		(0x0008)
#define	MPII_D_IOCTL		(0x0010)
#define	MPII_D_RW		(0x0020)
#define	MPII_D_MEM		(0x0040)
#define	MPII_D_CCB		(0x0080)
#define	MPII_D_PPR		(0x0100)
#define	MPII_D_RAID		(0x0200)
#define	MPII_D_EVT		(0x0400)
#define MPII_D_CFG		(0x0800)
#define MPII_D_MAP		(0x1000)

u_int32_t  mpii_debug = 0
		| MPII_D_CMD
		| MPII_D_INTR
		| MPII_D_MISC
		| MPII_D_DMA
		| MPII_D_IOCTL
		| MPII_D_RW
		| MPII_D_MEM
		| MPII_D_CCB
		| MPII_D_PPR
		| MPII_D_RAID
		| MPII_D_EVT
		| MPII_D_CFG
		| MPII_D_MAP
	;
#else
#define DPRINTF(x...)
#define DNPRINTF(n,x...)
#endif

#define MPII_REQUEST_SIZE		(512)
#define MPII_REQUEST_CREDIT		(128)

struct mpii_dmamem {
	bus_dmamap_t		mdm_map;
	bus_dma_segment_t	mdm_seg;
	size_t			mdm_size;
	caddr_t			mdm_kva;
};
#define MPII_DMA_MAP(_mdm) ((_mdm)->mdm_map)
#define MPII_DMA_DVA(_mdm) ((u_int64_t)(_mdm)->mdm_map->dm_segs[0].ds_addr)
#define MPII_DMA_KVA(_mdm) ((void *)(_mdm)->mdm_kva)

struct mpii_softc;

struct mpii_rcb {
	SIMPLEQ_ENTRY(mpii_rcb)	rcb_link;
	void			*rcb_reply;
	u_int32_t		rcb_reply_dva;
};

SIMPLEQ_HEAD(mpii_rcb_list, mpii_rcb);

struct mpii_device {
	int			flags;
#define MPII_DF_ATTACH		(0x0001)
#define MPII_DF_DETACH		(0x0002)
#define MPII_DF_HIDDEN		(0x0004)
#define MPII_DF_UNUSED		(0x0008)
#define MPII_DF_VOLUME		(0x0010)
#define MPII_DF_VOLUME_DISK	(0x0020)
#define MPII_DF_HOT_SPARE	(0x0040)
	short			slot;
	short			percent;
	u_int16_t		dev_handle;
	u_int16_t		enclosure;
	u_int16_t		expander;
	u_int8_t		phy_num;
	u_int8_t		physical_port;
};

struct mpii_ccb {
	struct mpii_softc	*ccb_sc;

	void *			ccb_cookie;
	bus_dmamap_t		ccb_dmamap;

	bus_addr_t		ccb_offset;
	void			*ccb_cmd;
	bus_addr_t		ccb_cmd_dva;
	u_int16_t		ccb_dev_handle;
	u_int16_t		ccb_smid;

	volatile enum {
		MPII_CCB_FREE,
		MPII_CCB_READY,
		MPII_CCB_QUEUED,
		MPII_CCB_TIMEOUT
	}			ccb_state;

	void			(*ccb_done)(struct mpii_ccb *);
	struct mpii_rcb		*ccb_rcb;

	SIMPLEQ_ENTRY(mpii_ccb)	ccb_link;
};

SIMPLEQ_HEAD(mpii_ccb_list, mpii_ccb);

struct mpii_softc {
	struct device		sc_dev;

	pci_chipset_tag_t	sc_pc;
	pcitag_t		sc_tag;

	void			*sc_ih;

	int			sc_flags;
#define MPII_F_RAID		(1<<1)
#define MPII_F_SAS3		(1<<2)
#define MPII_F_AERO		(1<<3)

	struct scsibus_softc	*sc_scsibus;
	unsigned int		sc_pending;

	struct mpii_device	**sc_devs;

	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	bus_size_t		sc_ios;
	bus_dma_tag_t		sc_dmat;

	struct mutex		sc_req_mtx;
	struct mutex		sc_rep_mtx;

	ushort			sc_reply_size;
	ushort			sc_request_size;

	ushort			sc_max_cmds;
	ushort			sc_num_reply_frames;
	u_int			sc_reply_free_qdepth;
	u_int			sc_reply_post_qdepth;

	ushort			sc_chain_sge;
	ushort			sc_max_sgl;
	int			sc_max_chain;

	u_int8_t		sc_ioc_event_replay;

	u_int8_t		sc_porttype;
	u_int8_t		sc_max_volumes;
	u_int16_t		sc_max_devices;
	u_int16_t		sc_vd_count;
	u_int16_t		sc_vd_id_low;
	u_int16_t		sc_pd_id_start;
	int			sc_ioc_number;
	u_int8_t		sc_vf_id;

	struct mpii_ccb		*sc_ccbs;
	struct mpii_ccb_list	sc_ccb_free;
	struct mutex		sc_ccb_free_mtx;

	struct mutex		sc_ccb_mtx;
				/*
				 * this protects the ccb state and list entry
				 * between mpii_scsi_cmd and scsidone.
				 */

	struct mpii_ccb_list	sc_ccb_tmos;
	struct scsi_iohandler	sc_ccb_tmo_handler;

	struct scsi_iopool	sc_iopool;

	struct mpii_dmamem	*sc_requests;

	struct mpii_dmamem	*sc_replies;
	struct mpii_rcb		*sc_rcbs;

	struct mpii_dmamem	*sc_reply_postq;
	struct mpii_reply_descr	*sc_reply_postq_kva;
	u_int			sc_reply_post_host_index;

	struct mpii_dmamem	*sc_reply_freeq;
	u_int			sc_reply_free_host_index;

	struct mpii_rcb_list	sc_evt_sas_queue;
	struct mutex		sc_evt_sas_mtx;
	struct task		sc_evt_sas_task;

	struct mpii_rcb_list	sc_evt_ack_queue;
	struct mutex		sc_evt_ack_mtx;
	struct scsi_iohandler	sc_evt_ack_handler;

	/* scsi ioctl from sd device */
	int			(*sc_ioctl)(struct device *, u_long, caddr_t);

	int			sc_nsensors;
	struct ksensor		*sc_sensors;
	struct ksensordev	sc_sensordev;
};

int	mpii_match(struct device *, void *, void *);
void	mpii_attach(struct device *, struct device *, void *);
int	mpii_detach(struct device *, int);

int	mpii_intr(void *);

const struct cfattach mpii_ca = {
	sizeof(struct mpii_softc),
	mpii_match,
	mpii_attach,
	mpii_detach
};

struct cfdriver mpii_cd = {
	NULL,
	"mpii",
	DV_DULL
};

void		mpii_scsi_cmd(struct scsi_xfer *);
void		mpii_scsi_cmd_done(struct mpii_ccb *);
int		mpii_scsi_probe(struct scsi_link *);
int		mpii_scsi_ioctl(struct scsi_link *, u_long, caddr_t, int);

const struct scsi_adapter mpii_switch = {
	mpii_scsi_cmd, NULL, mpii_scsi_probe, NULL, mpii_scsi_ioctl
};

struct mpii_dmamem *
		mpii_dmamem_alloc(struct mpii_softc *, size_t);
void		mpii_dmamem_free(struct mpii_softc *,
		    struct mpii_dmamem *);
int		mpii_alloc_ccbs(struct mpii_softc *);
void *		mpii_get_ccb(void *);
void		mpii_put_ccb(void *, void *);
int		mpii_alloc_replies(struct mpii_softc *);
int		mpii_alloc_queues(struct mpii_softc *);
void		mpii_push_reply(struct mpii_softc *, struct mpii_rcb *);
void		mpii_push_replies(struct mpii_softc *);

void		mpii_scsi_cmd_tmo(void *);
void		mpii_scsi_cmd_tmo_handler(void *, void *);
void		mpii_scsi_cmd_tmo_done(struct mpii_ccb *);

int		mpii_insert_dev(struct mpii_softc *, struct mpii_device *);
int		mpii_remove_dev(struct mpii_softc *, struct mpii_device *);
struct mpii_device *
		mpii_find_dev(struct mpii_softc *, u_int16_t);

void		mpii_start(struct mpii_softc *, struct mpii_ccb *);
int		mpii_poll(struct mpii_softc *, struct mpii_ccb *);
void		mpii_poll_done(struct mpii_ccb *);
struct mpii_rcb *
		mpii_reply(struct mpii_softc *, struct mpii_reply_descr *);

void		mpii_wait(struct mpii_softc *, struct mpii_ccb *);
void		mpii_wait_done(struct mpii_ccb *);

void		mpii_init_queues(struct mpii_softc *);

int		mpii_load_xs(struct mpii_ccb *);
int		mpii_load_xs_sas3(struct mpii_ccb *);

u_int32_t	mpii_read(struct mpii_softc *, bus_size_t);
void		mpii_write(struct mpii_softc *, bus_size_t, u_int32_t);
int		mpii_wait_eq(struct mpii_softc *, bus_size_t, u_int32_t,
		    u_int32_t);
int		mpii_wait_ne(struct mpii_softc *, bus_size_t, u_int32_t,
		    u_int32_t);

int		mpii_init(struct mpii_softc *);
int		mpii_reset_soft(struct mpii_softc *);
int		mpii_reset_hard(struct mpii_softc *);

int		mpii_handshake_send(struct mpii_softc *, void *, size_t);
int		mpii_handshake_recv_dword(struct mpii_softc *,
		    u_int32_t *);
int		mpii_handshake_recv(struct mpii_softc *, void *, size_t);

void		mpii_empty_done(struct mpii_ccb *);

int		mpii_iocinit(struct mpii_softc *);
int		mpii_iocfacts(struct mpii_softc *);
int		mpii_portfacts(struct mpii_softc *);
int		mpii_portenable(struct mpii_softc *);
int		mpii_cfg_coalescing(struct mpii_softc *);
int		mpii_board_info(struct mpii_softc *);
int		mpii_target_map(struct mpii_softc *);

int		mpii_eventnotify(struct mpii_softc *);
void		mpii_eventnotify_done(struct mpii_ccb *);
void		mpii_eventack(void *, void *);
void		mpii_eventack_done(struct mpii_ccb *);
void		mpii_event_process(struct mpii_softc *, struct mpii_rcb *);
void		mpii_event_done(struct mpii_softc *, struct mpii_rcb *);
void		mpii_event_sas(void *);
void		mpii_event_raid(struct mpii_softc *,
		    struct mpii_msg_event_reply *);
void		mpii_event_discovery(struct mpii_softc *,
		    struct mpii_msg_event_reply *);

void		mpii_sas_remove_device(struct mpii_softc *, u_int16_t);

int		mpii_req_cfg_header(struct mpii_softc *, u_int8_t,
		    u_int8_t, u_int32_t, int, void *);
int		mpii_req_cfg_page(struct mpii_softc *, u_int32_t, int,
		    void *, int, void *, size_t);

int		mpii_ioctl_cache(struct scsi_link *, u_long, struct dk_cache *);

#if NBIO > 0
int		mpii_ioctl(struct device *, u_long, caddr_t);
int		mpii_ioctl_inq(struct mpii_softc *, struct bioc_inq *);
int		mpii_ioctl_vol(struct mpii_softc *, struct bioc_vol *);
int		mpii_ioctl_disk(struct mpii_softc *, struct bioc_disk *);
int		mpii_bio_hs(struct mpii_softc *, struct bioc_disk *, int,
		    int, int *);
int		mpii_bio_disk(struct mpii_softc *, struct bioc_disk *,
		    u_int8_t);
struct mpii_device *
		mpii_find_vol(struct mpii_softc *, int);
#ifndef SMALL_KERNEL
 int		mpii_bio_volstate(struct mpii_softc *, struct bioc_vol *);
int		mpii_create_sensors(struct mpii_softc *);
void		mpii_refresh_sensors(void *);
#endif /* SMALL_KERNEL */
#endif /* NBIO > 0 */

#define DEVNAME(s)		((s)->sc_dev.dv_xname)

#define dwordsof(s)		(sizeof(s) / sizeof(u_int32_t))

#define mpii_read_db(s)		mpii_read((s), MPII_DOORBELL)
#define mpii_write_db(s, v)	mpii_write((s), MPII_DOORBELL, (v))
#define mpii_read_intr(s)	mpii_read((s), MPII_INTR_STATUS)
#define mpii_write_intr(s, v)	mpii_write((s), MPII_INTR_STATUS, (v))
#define mpii_reply_waiting(s)	((mpii_read_intr((s)) & MPII_INTR_STATUS_REPLY)\
				    == MPII_INTR_STATUS_REPLY)

#define mpii_write_reply_free(s, v) \
    bus_space_write_4((s)->sc_iot, (s)->sc_ioh, \
    MPII_REPLY_FREE_HOST_INDEX, (v))
#define mpii_write_reply_post(s, v) \
    bus_space_write_4((s)->sc_iot, (s)->sc_ioh, \
    MPII_REPLY_POST_HOST_INDEX, (v))

#define mpii_wait_db_int(s)	mpii_wait_ne((s), MPII_INTR_STATUS, \
				    MPII_INTR_STATUS_IOC2SYSDB, 0)
#define mpii_wait_db_ack(s)	mpii_wait_eq((s), MPII_INTR_STATUS, \
				    MPII_INTR_STATUS_SYS2IOCDB, 0)

static inline void
mpii_dvatosge(struct mpii_sge *sge, u_int64_t dva)
{
	htolem32(&sge->sg_addr_lo, dva);
	htolem32(&sge->sg_addr_hi, dva >> 32);
}

#define MPII_PG_EXTENDED	(1<<0)
#define MPII_PG_POLL		(1<<1)
#define MPII_PG_FMT		"\020" "\002POLL" "\001EXTENDED"

static const struct pci_matchid mpii_devices[] = {
	{ PCI_VENDOR_SYMBIOS,	PCI_PRODUCT_SYMBIOS_SAS2004 },
	{ PCI_VENDOR_SYMBIOS,	PCI_PRODUCT_SYMBIOS_SAS2008 },
	{ PCI_VENDOR_SYMBIOS,	PCI_PRODUCT_SYMBIOS_SSS6200 },
	{ PCI_VENDOR_SYMBIOS,	PCI_PRODUCT_SYMBIOS_SAS2108_3 },
	{ PCI_VENDOR_SYMBIOS,	PCI_PRODUCT_SYMBIOS_SAS2108_4 },
	{ PCI_VENDOR_SYMBIOS,	PCI_PRODUCT_SYMBIOS_SAS2108_5 },
	{ PCI_VENDOR_SYMBIOS,	PCI_PRODUCT_SYMBIOS_SAS2116_1 },
	{ PCI_VENDOR_SYMBIOS,	PCI_PRODUCT_SYMBIOS_SAS2116_2 },
	{ PCI_VENDOR_SYMBIOS,	PCI_PRODUCT_SYMBIOS_SAS2208_1 },
	{ PCI_VENDOR_SYMBIOS,	PCI_PRODUCT_SYMBIOS_SAS2208_2 },
	{ PCI_VENDOR_SYMBIOS,	PCI_PRODUCT_SYMBIOS_SAS2208_3 },
	{ PCI_VENDOR_SYMBIOS,	PCI_PRODUCT_SYMBIOS_SAS2208_4 },
	{ PCI_VENDOR_SYMBIOS,	PCI_PRODUCT_SYMBIOS_SAS2208_5 },
	{ PCI_VENDOR_SYMBIOS,	PCI_PRODUCT_SYMBIOS_SAS2208_6 },
	{ PCI_VENDOR_SYMBIOS,	PCI_PRODUCT_SYMBIOS_SAS2308_1 },
	{ PCI_VENDOR_SYMBIOS,	PCI_PRODUCT_SYMBIOS_SAS2308_2 },
	{ PCI_VENDOR_SYMBIOS,	PCI_PRODUCT_SYMBIOS_SAS2308_3 },
	{ PCI_VENDOR_SYMBIOS,	PCI_PRODUCT_SYMBIOS_SAS3004 },
	{ PCI_VENDOR_SYMBIOS,	PCI_PRODUCT_SYMBIOS_SAS3008 },
	{ PCI_VENDOR_SYMBIOS,	PCI_PRODUCT_SYMBIOS_SAS3108_1 },
	{ PCI_VENDOR_SYMBIOS,	PCI_PRODUCT_SYMBIOS_SAS3108_2 },
	{ PCI_VENDOR_SYMBIOS,	PCI_PRODUCT_SYMBIOS_SAS3108_3 },
	{ PCI_VENDOR_SYMBIOS,	PCI_PRODUCT_SYMBIOS_SAS3108_4 },
	{ PCI_VENDOR_SYMBIOS,	PCI_PRODUCT_SYMBIOS_SAS3408 },
	{ PCI_VENDOR_SYMBIOS,	PCI_PRODUCT_SYMBIOS_SAS3416 },
	{ PCI_VENDOR_SYMBIOS,	PCI_PRODUCT_SYMBIOS_SAS3508 },
	{ PCI_VENDOR_SYMBIOS,	PCI_PRODUCT_SYMBIOS_SAS3508_1 },
	{ PCI_VENDOR_SYMBIOS,	PCI_PRODUCT_SYMBIOS_SAS3516 },
	{ PCI_VENDOR_SYMBIOS,	PCI_PRODUCT_SYMBIOS_SAS3516_1 },
	{ PCI_VENDOR_SYMBIOS,	PCI_PRODUCT_SYMBIOS_SAS38XX },
	{ PCI_VENDOR_SYMBIOS,	PCI_PRODUCT_SYMBIOS_SAS38XX_1 },
	{ PCI_VENDOR_SYMBIOS,	PCI_PRODUCT_SYMBIOS_SAS39XX },
	{ PCI_VENDOR_SYMBIOS,	PCI_PRODUCT_SYMBIOS_SAS39XX_1 },
};

int
mpii_match(struct device *parent, void *match, void *aux)
{
	return (pci_matchbyid(aux, mpii_devices, nitems(mpii_devices)));
}

void
mpii_attach(struct device *parent, struct device *self, void *aux)
{
	struct mpii_softc		*sc = (struct mpii_softc *)self;
	struct pci_attach_args		*pa = aux;
	pcireg_t			memtype;
	int				r;
	pci_intr_handle_t		ih;
	struct scsibus_attach_args	saa;
	struct mpii_ccb			*ccb;

	sc->sc_pc = pa->pa_pc;
	sc->sc_tag = pa->pa_tag;
	sc->sc_dmat = pa->pa_dmat;

	mtx_init(&sc->sc_req_mtx, IPL_BIO);
	mtx_init(&sc->sc_rep_mtx, IPL_BIO);

	/* find the appropriate memory base */
	for (r = PCI_MAPREG_START; r < PCI_MAPREG_END; r += sizeof(memtype)) {
		memtype = pci_mapreg_type(sc->sc_pc, sc->sc_tag, r);
		if ((memtype & PCI_MAPREG_TYPE_MASK) == PCI_MAPREG_TYPE_MEM)
			break;
	}
	if (r >= PCI_MAPREG_END) {
		printf(": unable to locate system interface registers\n");
		return;
	}

	if (pci_mapreg_map(pa, r, memtype, 0, &sc->sc_iot, &sc->sc_ioh,
	    NULL, &sc->sc_ios, 0xFF) != 0) {
		printf(": unable to map system interface registers\n");
		return;
	}

	/* disable the expansion rom */
	pci_conf_write(sc->sc_pc, sc->sc_tag, PCI_ROM_REG,
	    pci_conf_read(sc->sc_pc, sc->sc_tag, PCI_ROM_REG) &
	    ~PCI_ROM_ENABLE);

	/* disable interrupts */
	mpii_write(sc, MPII_INTR_MASK,
	    MPII_INTR_MASK_RESET | MPII_INTR_MASK_REPLY |
	    MPII_INTR_MASK_DOORBELL);

	/* hook up the interrupt */
	if (pci_intr_map_msi(pa, &ih) != 0 && pci_intr_map(pa, &ih) != 0) {
		printf(": unable to map interrupt\n");
		goto unmap;
	}
	printf(": %s\n", pci_intr_string(sc->sc_pc, ih));

	switch (PCI_PRODUCT(pa->pa_id)) {
	case PCI_PRODUCT_SYMBIOS_SAS38XX:
	case PCI_PRODUCT_SYMBIOS_SAS38XX_1:
	case PCI_PRODUCT_SYMBIOS_SAS39XX:
	case PCI_PRODUCT_SYMBIOS_SAS39XX_1:
		SET(sc->sc_flags, MPII_F_AERO);
		break;
	}

	if (mpii_iocfacts(sc) != 0) {
		printf("%s: unable to get iocfacts\n", DEVNAME(sc));
		goto unmap;
	}

	if (mpii_init(sc) != 0) {
		printf("%s: unable to initialize ioc\n", DEVNAME(sc));
		goto unmap;
	}

	if (mpii_alloc_ccbs(sc) != 0) {
		/* error already printed */
		goto unmap;
	}

	if (mpii_alloc_replies(sc) != 0) {
		printf("%s: unable to allocated reply space\n", DEVNAME(sc));
		goto free_ccbs;
	}

	if (mpii_alloc_queues(sc) != 0) {
		printf("%s: unable to allocate reply queues\n", DEVNAME(sc));
		goto free_replies;
	}

	if (mpii_iocinit(sc) != 0) {
		printf("%s: unable to send iocinit\n", DEVNAME(sc));
		goto free_queues;
	}

	if (mpii_wait_eq(sc, MPII_DOORBELL, MPII_DOORBELL_STATE,
	    MPII_DOORBELL_STATE_OPER) != 0) {
		printf("%s: state: 0x%08x\n", DEVNAME(sc),
			mpii_read_db(sc) & MPII_DOORBELL_STATE);
		printf("%s: operational state timeout\n", DEVNAME(sc));
		goto free_queues;
	}

	mpii_push_replies(sc);
	mpii_init_queues(sc);

	if (mpii_board_info(sc) != 0) {
		printf("%s: unable to get manufacturing page 0\n",
		    DEVNAME(sc));
		goto free_queues;
	}

	if (mpii_portfacts(sc) != 0) {
		printf("%s: unable to get portfacts\n", DEVNAME(sc));
		goto free_queues;
	}

	if (mpii_target_map(sc) != 0) {
		printf("%s: unable to setup target mappings\n", DEVNAME(sc));
		goto free_queues;
	}

	if (mpii_cfg_coalescing(sc) != 0) {
		printf("%s: unable to configure coalescing\n", DEVNAME(sc));
		goto free_queues;
	}

	/* XXX bail on unsupported porttype? */
	if ((sc->sc_porttype == MPII_PORTFACTS_PORTTYPE_SAS_PHYSICAL) ||
	    (sc->sc_porttype == MPII_PORTFACTS_PORTTYPE_SAS_VIRTUAL) ||
	    (sc->sc_porttype == MPII_PORTFACTS_PORTTYPE_TRI_MODE)) {
		if (mpii_eventnotify(sc) != 0) {
			printf("%s: unable to enable events\n", DEVNAME(sc));
			goto free_queues;
		}
	}

	sc->sc_devs = mallocarray(sc->sc_max_devices,
	    sizeof(struct mpii_device *), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (sc->sc_devs == NULL) {
		printf("%s: unable to allocate memory for mpii_device\n",
		    DEVNAME(sc));
		goto free_queues;
	}

	if (mpii_portenable(sc) != 0) {
		printf("%s: unable to enable port\n", DEVNAME(sc));
		goto free_devs;
	}

	sc->sc_ih = pci_intr_establish(sc->sc_pc, ih, IPL_BIO,
	    mpii_intr, sc, sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL)
		goto free_devs;

	/* force autoconf to wait for the first sas discovery to complete */
	sc->sc_pending = 1;
	config_pending_incr();

	saa.saa_adapter = &mpii_switch;
	saa.saa_adapter_softc = sc;
	saa.saa_adapter_target = SDEV_NO_ADAPTER_TARGET;
	saa.saa_adapter_buswidth = sc->sc_max_devices;
	saa.saa_luns = 1;
	saa.saa_openings = sc->sc_max_cmds - 1;
	saa.saa_pool = &sc->sc_iopool;
	saa.saa_quirks = saa.saa_flags = 0;
	saa.saa_wwpn = saa.saa_wwnn = 0;

	sc->sc_scsibus = (struct scsibus_softc *) config_found(&sc->sc_dev,
	    &saa, scsiprint);

	/* enable interrupts */
	mpii_write(sc, MPII_INTR_MASK, MPII_INTR_MASK_DOORBELL
	    | MPII_INTR_MASK_RESET);

#if NBIO > 0
	if (ISSET(sc->sc_flags, MPII_F_RAID)) {
		if (bio_register(&sc->sc_dev, mpii_ioctl) != 0)
			panic("%s: controller registration failed",
			    DEVNAME(sc));
		else
			sc->sc_ioctl = mpii_ioctl;

#ifndef SMALL_KERNEL
		if (mpii_create_sensors(sc) != 0)
			printf("%s: unable to create sensors\n", DEVNAME(sc));
#endif
	}
#endif

	return;

free_devs:
	free(sc->sc_devs, M_DEVBUF, 0);
	sc->sc_devs = NULL;

free_queues:
	bus_dmamap_sync(sc->sc_dmat, MPII_DMA_MAP(sc->sc_reply_freeq),
	    0, sc->sc_reply_free_qdepth * 4, BUS_DMASYNC_POSTREAD);
	mpii_dmamem_free(sc, sc->sc_reply_freeq);

	bus_dmamap_sync(sc->sc_dmat, MPII_DMA_MAP(sc->sc_reply_postq),
	    0, sc->sc_reply_post_qdepth * 8, BUS_DMASYNC_POSTREAD);
	mpii_dmamem_free(sc, sc->sc_reply_postq);

free_replies:
	bus_dmamap_sync(sc->sc_dmat, MPII_DMA_MAP(sc->sc_replies),
		0, PAGE_SIZE, BUS_DMASYNC_POSTREAD);
	mpii_dmamem_free(sc, sc->sc_replies);

free_ccbs:
	while ((ccb = mpii_get_ccb(sc)) != NULL)
		bus_dmamap_destroy(sc->sc_dmat, ccb->ccb_dmamap);
	mpii_dmamem_free(sc, sc->sc_requests);
	free(sc->sc_ccbs, M_DEVBUF, 0);

unmap:
	bus_space_unmap(sc->sc_iot, sc->sc_ioh, sc->sc_ios);
	sc->sc_ios = 0;
}

int
mpii_detach(struct device *self, int flags)
{
	struct mpii_softc		*sc = (struct mpii_softc *)self;

	if (sc->sc_ih != NULL) {
		pci_intr_disestablish(sc->sc_pc, sc->sc_ih);
		sc->sc_ih = NULL;
	}
	if (sc->sc_ios != 0) {
		bus_space_unmap(sc->sc_iot, sc->sc_ioh, sc->sc_ios);
		sc->sc_ios = 0;
	}

	return (0);
}

int
mpii_intr(void *arg)
{
	struct mpii_rcb_list		evts = SIMPLEQ_HEAD_INITIALIZER(evts);
	struct mpii_ccb_list		ccbs = SIMPLEQ_HEAD_INITIALIZER(ccbs);
	struct mpii_softc		*sc = arg;
	struct mpii_reply_descr		*postq = sc->sc_reply_postq_kva, *rdp;
	struct mpii_ccb			*ccb;
	struct mpii_rcb			*rcb;
	int				smid;
	u_int				idx;
	int				rv = 0;

	mtx_enter(&sc->sc_rep_mtx);
	bus_dmamap_sync(sc->sc_dmat,
	    MPII_DMA_MAP(sc->sc_reply_postq),
	    0, sc->sc_reply_post_qdepth * sizeof(*rdp),
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	idx = sc->sc_reply_post_host_index;
	for (;;) {
		rdp = &postq[idx];
		if ((rdp->reply_flags & MPII_REPLY_DESCR_TYPE_MASK) ==
		    MPII_REPLY_DESCR_UNUSED)
			break;
		if (rdp->data == 0xffffffff) {
			/*
			 * ioc is still writing to the reply post queue
			 * race condition - bail!
			 */
			break;
		}

		smid = lemtoh16(&rdp->smid);
		rcb = mpii_reply(sc, rdp);

		if (smid) {
			ccb = &sc->sc_ccbs[smid - 1];
			ccb->ccb_state = MPII_CCB_READY;
			ccb->ccb_rcb = rcb;
			SIMPLEQ_INSERT_TAIL(&ccbs, ccb, ccb_link);
		} else
			SIMPLEQ_INSERT_TAIL(&evts, rcb, rcb_link);

		if (++idx >= sc->sc_reply_post_qdepth)
			idx = 0;

		rv = 1;
	}

	bus_dmamap_sync(sc->sc_dmat,
	    MPII_DMA_MAP(sc->sc_reply_postq),
	    0, sc->sc_reply_post_qdepth * sizeof(*rdp),
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	if (rv)
		mpii_write_reply_post(sc, sc->sc_reply_post_host_index = idx);

	mtx_leave(&sc->sc_rep_mtx);

	if (rv == 0)
		return (0);

	while ((ccb = SIMPLEQ_FIRST(&ccbs)) != NULL) {
		SIMPLEQ_REMOVE_HEAD(&ccbs, ccb_link);
		ccb->ccb_done(ccb);
	}
	while ((rcb = SIMPLEQ_FIRST(&evts)) != NULL) {
		SIMPLEQ_REMOVE_HEAD(&evts, rcb_link);
		mpii_event_process(sc, rcb);
	}

	return (1);
}

int
mpii_load_xs_sas3(struct mpii_ccb *ccb)
{
	struct mpii_softc	*sc = ccb->ccb_sc;
	struct scsi_xfer	*xs = ccb->ccb_cookie;
	struct mpii_msg_scsi_io	*io = ccb->ccb_cmd;
	struct mpii_ieee_sge	*csge, *nsge, *sge;
	bus_dmamap_t		dmap = ccb->ccb_dmamap;
	int			i, error;

	/* Request frame structure is described in the mpii_iocfacts */
	nsge = (struct mpii_ieee_sge *)(io + 1);

	/* zero length transfer still requires an SGE */
	if (xs->datalen == 0) {
		nsge->sg_flags = MPII_IEEE_SGE_END_OF_LIST;
		return (0);
	}

	error = bus_dmamap_load(sc->sc_dmat, dmap, xs->data, xs->datalen, NULL,
	    (xs->flags & SCSI_NOSLEEP) ? BUS_DMA_NOWAIT : BUS_DMA_WAITOK);
	if (error) {
		printf("%s: error %d loading dmamap\n", DEVNAME(sc), error);
		return (1);
	}

	csge = NULL;
	if (dmap->dm_nsegs > sc->sc_chain_sge) {
		csge = nsge + sc->sc_chain_sge;

		/* offset to the chain sge from the beginning */
		io->chain_offset = ((caddr_t)csge - (caddr_t)io) / sizeof(*sge);
	}

	for (i = 0; i < dmap->dm_nsegs; i++, nsge++) {
		if (nsge == csge) {
			nsge++;

			/* address of the next sge */
			htolem64(&csge->sg_addr, ccb->ccb_cmd_dva +
			    ((caddr_t)nsge - (caddr_t)io));
			htolem32(&csge->sg_len, (dmap->dm_nsegs - i) *
			    sizeof(*sge));
			csge->sg_next_chain_offset = 0;
			csge->sg_flags = MPII_IEEE_SGE_CHAIN_ELEMENT |
			    MPII_IEEE_SGE_ADDR_SYSTEM;

			if ((dmap->dm_nsegs - i) > sc->sc_max_chain) {
				csge->sg_next_chain_offset = sc->sc_max_chain;
				csge += sc->sc_max_chain;
			}
		}

		sge = nsge;
		sge->sg_flags = MPII_IEEE_SGE_ADDR_SYSTEM;
		sge->sg_next_chain_offset = 0;
		htolem32(&sge->sg_len, dmap->dm_segs[i].ds_len);
		htolem64(&sge->sg_addr, dmap->dm_segs[i].ds_addr);
	}

	/* terminate list */
	sge->sg_flags |= MPII_IEEE_SGE_END_OF_LIST;

	bus_dmamap_sync(sc->sc_dmat, dmap, 0, dmap->dm_mapsize,
	    (xs->flags & SCSI_DATA_IN) ? BUS_DMASYNC_PREREAD :
	    BUS_DMASYNC_PREWRITE);

	return (0);
}

int
mpii_load_xs(struct mpii_ccb *ccb)
{
	struct mpii_softc	*sc = ccb->ccb_sc;
	struct scsi_xfer	*xs = ccb->ccb_cookie;
	struct mpii_msg_scsi_io	*io = ccb->ccb_cmd;
	struct mpii_sge		*csge, *nsge, *sge;
	bus_dmamap_t		dmap = ccb->ccb_dmamap;
	u_int32_t		flags;
	u_int16_t		len;
	int			i, error;

	/* Request frame structure is described in the mpii_iocfacts */
	nsge = (struct mpii_sge *)(io + 1);
	csge = nsge + sc->sc_chain_sge;

	/* zero length transfer still requires an SGE */
	if (xs->datalen == 0) {
		nsge->sg_hdr = htole32(MPII_SGE_FL_TYPE_SIMPLE |
		    MPII_SGE_FL_LAST | MPII_SGE_FL_EOB | MPII_SGE_FL_EOL);
		return (0);
	}

	error = bus_dmamap_load(sc->sc_dmat, dmap, xs->data, xs->datalen, NULL,
	    (xs->flags & SCSI_NOSLEEP) ? BUS_DMA_NOWAIT : BUS_DMA_WAITOK);
	if (error) {
		printf("%s: error %d loading dmamap\n", DEVNAME(sc), error);
		return (1);
	}

	/* safe default starting flags */
	flags = MPII_SGE_FL_TYPE_SIMPLE | MPII_SGE_FL_SIZE_64;
	if (xs->flags & SCSI_DATA_OUT)
		flags |= MPII_SGE_FL_DIR_OUT;

	for (i = 0; i < dmap->dm_nsegs; i++, nsge++) {
		if (nsge == csge) {
			nsge++;
			/* offset to the chain sge from the beginning */
			io->chain_offset = ((caddr_t)csge - (caddr_t)io) / 4;
			/* length of the sgl segment we're pointing to */
			len = (dmap->dm_nsegs - i) * sizeof(*sge);
			htolem32(&csge->sg_hdr, MPII_SGE_FL_TYPE_CHAIN |
			    MPII_SGE_FL_SIZE_64 | len);
			/* address of the next sge */
			mpii_dvatosge(csge, ccb->ccb_cmd_dva +
			    ((caddr_t)nsge - (caddr_t)io));
		}

		sge = nsge;
		htolem32(&sge->sg_hdr, flags | dmap->dm_segs[i].ds_len);
		mpii_dvatosge(sge, dmap->dm_segs[i].ds_addr);
	}

	/* terminate list */
	sge->sg_hdr |= htole32(MPII_SGE_FL_LAST | MPII_SGE_FL_EOB |
	    MPII_SGE_FL_EOL);

	bus_dmamap_sync(sc->sc_dmat, dmap, 0, dmap->dm_mapsize,
	    (xs->flags & SCSI_DATA_IN) ? BUS_DMASYNC_PREREAD :
	    BUS_DMASYNC_PREWRITE);

	return (0);
}

int
mpii_scsi_probe(struct scsi_link *link)
{
	struct mpii_softc *sc = link->bus->sb_adapter_softc;
	struct mpii_cfg_sas_dev_pg0 pg0;
	struct mpii_ecfg_hdr ehdr;
	struct mpii_device *dev;
	uint32_t address;
	int flags;

	if ((sc->sc_porttype != MPII_PORTFACTS_PORTTYPE_SAS_PHYSICAL) &&
	    (sc->sc_porttype != MPII_PORTFACTS_PORTTYPE_SAS_VIRTUAL) &&
	    (sc->sc_porttype != MPII_PORTFACTS_PORTTYPE_TRI_MODE))
		return (ENXIO);

	dev = sc->sc_devs[link->target];
	if (dev == NULL)
		return (1);

	flags = dev->flags;
	if (ISSET(flags, MPII_DF_HIDDEN) || ISSET(flags, MPII_DF_UNUSED))
		return (1);

	if (ISSET(flags, MPII_DF_VOLUME)) {
		struct mpii_cfg_hdr hdr;
		struct mpii_cfg_raid_vol_pg1 vpg;
		size_t pagelen;

		address = MPII_CFG_RAID_VOL_ADDR_HANDLE | dev->dev_handle;

		if (mpii_req_cfg_header(sc, MPII_CONFIG_REQ_PAGE_TYPE_RAID_VOL,
		    1, address, MPII_PG_POLL, &hdr) != 0)
			return (EINVAL);

		memset(&vpg, 0, sizeof(vpg));
		/* avoid stack trash on future page growth */
		pagelen = min(sizeof(vpg), hdr.page_length * 4);

		if (mpii_req_cfg_page(sc, address, MPII_PG_POLL, &hdr, 1,
		    &vpg, pagelen) != 0)
			return (EINVAL);

		link->port_wwn = letoh64(vpg.wwid);
		/*
		 * WWIDs generated by LSI firmware are not IEEE NAA compliant
		 * and historical practise in OBP on sparc64 is to set the top
		 * nibble to 3 to indicate that this is a RAID volume.
		 */
		link->port_wwn &= 0x0fffffffffffffff;
		link->port_wwn |= 0x3000000000000000;

		return (0);
	}

	memset(&ehdr, 0, sizeof(ehdr));
	ehdr.page_type = MPII_CONFIG_REQ_PAGE_TYPE_EXTENDED;
	ehdr.page_number = 0;
	ehdr.page_version = 0;
	ehdr.ext_page_type = MPII_CONFIG_REQ_EXTPAGE_TYPE_SAS_DEVICE;
	ehdr.ext_page_length = htole16(sizeof(pg0) / 4); /* dwords */

	address = MPII_PGAD_SAS_DEVICE_FORM_HANDLE | (uint32_t)dev->dev_handle;
	if (mpii_req_cfg_page(sc, address, MPII_PG_EXTENDED,
	    &ehdr, 1, &pg0, sizeof(pg0)) != 0) {
		printf("%s: unable to fetch SAS device page 0 for target %u\n",
		    DEVNAME(sc), link->target);

		return (0); /* the handle should still work */
	}

	link->port_wwn = letoh64(pg0.sas_addr);
	link->node_wwn = letoh64(pg0.device_name);

	if (ISSET(lemtoh32(&pg0.device_info),
	    MPII_CFG_SAS_DEV_0_DEVINFO_ATAPI_DEVICE)) {
		link->flags |= SDEV_ATAPI;
	}

	return (0);
}

u_int32_t
mpii_read(struct mpii_softc *sc, bus_size_t r)
{
	u_int32_t			rv;
	int				i;

	if (ISSET(sc->sc_flags, MPII_F_AERO)) {
		i = 0;
		do {
			if (i > 0)
				DNPRINTF(MPII_D_RW, "%s: mpii_read retry %d\n",
				    DEVNAME(sc), i);
			bus_space_barrier(sc->sc_iot, sc->sc_ioh, r, 4,
			    BUS_SPACE_BARRIER_READ);
			rv = bus_space_read_4(sc->sc_iot, sc->sc_ioh, r);
			i++;
		} while (rv == 0 && i < 3);
	} else {
		bus_space_barrier(sc->sc_iot, sc->sc_ioh, r, 4,
		    BUS_SPACE_BARRIER_READ);
		rv = bus_space_read_4(sc->sc_iot, sc->sc_ioh, r);
	}

	DNPRINTF(MPII_D_RW, "%s: mpii_read %#lx %#x\n", DEVNAME(sc), r, rv);

	return (rv);
}

void
mpii_write(struct mpii_softc *sc, bus_size_t r, u_int32_t v)
{
	DNPRINTF(MPII_D_RW, "%s: mpii_write %#lx %#x\n", DEVNAME(sc), r, v);

	bus_space_write_4(sc->sc_iot, sc->sc_ioh, r, v);
	bus_space_barrier(sc->sc_iot, sc->sc_ioh, r, 4,
	    BUS_SPACE_BARRIER_WRITE);
}


int
mpii_wait_eq(struct mpii_softc *sc, bus_size_t r, u_int32_t mask,
    u_int32_t target)
{
	int			i;

	DNPRINTF(MPII_D_RW, "%s: mpii_wait_eq %#lx %#x %#x\n", DEVNAME(sc), r,
	    mask, target);

	for (i = 0; i < 15000; i++) {
		if ((mpii_read(sc, r) & mask) == target)
			return (0);
		delay(1000);
	}

	return (1);
}

int
mpii_wait_ne(struct mpii_softc *sc, bus_size_t r, u_int32_t mask,
    u_int32_t target)
{
	int			i;

	DNPRINTF(MPII_D_RW, "%s: mpii_wait_ne %#lx %#x %#x\n", DEVNAME(sc), r,
	    mask, target);

	for (i = 0; i < 15000; i++) {
		if ((mpii_read(sc, r) & mask) != target)
			return (0);
		delay(1000);
	}

	return (1);
}

int
mpii_init(struct mpii_softc *sc)
{
	u_int32_t		db;
	int			i;

	/* spin until the ioc leaves the reset state */
	if (mpii_wait_ne(sc, MPII_DOORBELL, MPII_DOORBELL_STATE,
	    MPII_DOORBELL_STATE_RESET) != 0) {
		DNPRINTF(MPII_D_MISC, "%s: mpii_init timeout waiting to leave "
		    "reset state\n", DEVNAME(sc));
		return (1);
	}

	/* check current ownership */
	db = mpii_read_db(sc);
	if ((db & MPII_DOORBELL_WHOINIT) == MPII_DOORBELL_WHOINIT_PCIPEER) {
		DNPRINTF(MPII_D_MISC, "%s: mpii_init initialised by pci peer\n",
		    DEVNAME(sc));
		return (0);
	}

	for (i = 0; i < 5; i++) {
		switch (db & MPII_DOORBELL_STATE) {
		case MPII_DOORBELL_STATE_READY:
			DNPRINTF(MPII_D_MISC, "%s: mpii_init ioc is ready\n",
			    DEVNAME(sc));
			return (0);

		case MPII_DOORBELL_STATE_OPER:
			DNPRINTF(MPII_D_MISC, "%s: mpii_init ioc is oper\n",
			    DEVNAME(sc));
			if (sc->sc_ioc_event_replay)
				mpii_reset_soft(sc);
			else
				mpii_reset_hard(sc);
			break;

		case MPII_DOORBELL_STATE_FAULT:
			DNPRINTF(MPII_D_MISC, "%s: mpii_init ioc is being "
			    "reset hard\n" , DEVNAME(sc));
			mpii_reset_hard(sc);
			break;

		case MPII_DOORBELL_STATE_RESET:
			DNPRINTF(MPII_D_MISC, "%s: mpii_init waiting to come "
			    "out of reset\n", DEVNAME(sc));
			if (mpii_wait_ne(sc, MPII_DOORBELL, MPII_DOORBELL_STATE,
			    MPII_DOORBELL_STATE_RESET) != 0)
				return (1);
			break;
		}
		db = mpii_read_db(sc);
	}

	return (1);
}

int
mpii_reset_soft(struct mpii_softc *sc)
{
	DNPRINTF(MPII_D_MISC, "%s: mpii_reset_soft\n", DEVNAME(sc));

	if (mpii_read_db(sc) & MPII_DOORBELL_INUSE) {
		return (1);
	}

	mpii_write_db(sc,
	    MPII_DOORBELL_FUNCTION(MPII_FUNCTION_IOC_MESSAGE_UNIT_RESET));

	/* XXX LSI waits 15 sec */
	if (mpii_wait_db_ack(sc) != 0)
		return (1);

	/* XXX LSI waits 15 sec */
	if (mpii_wait_eq(sc, MPII_DOORBELL, MPII_DOORBELL_STATE,
	    MPII_DOORBELL_STATE_READY) != 0)
		return (1);

	/* XXX wait for Sys2IOCDB bit to clear in HIS?? */

	return (0);
}

int
mpii_reset_hard(struct mpii_softc *sc)
{
	u_int16_t		i;

	DNPRINTF(MPII_D_MISC, "%s: mpii_reset_hard\n", DEVNAME(sc));

	mpii_write_intr(sc, 0);

	/* enable diagnostic register */
	mpii_write(sc, MPII_WRITESEQ, MPII_WRITESEQ_FLUSH);
	mpii_write(sc, MPII_WRITESEQ, MPII_WRITESEQ_1);
	mpii_write(sc, MPII_WRITESEQ, MPII_WRITESEQ_2);
	mpii_write(sc, MPII_WRITESEQ, MPII_WRITESEQ_3);
	mpii_write(sc, MPII_WRITESEQ, MPII_WRITESEQ_4);
	mpii_write(sc, MPII_WRITESEQ, MPII_WRITESEQ_5);
	mpii_write(sc, MPII_WRITESEQ, MPII_WRITESEQ_6);

	delay(100);

	if ((mpii_read(sc, MPII_HOSTDIAG) & MPII_HOSTDIAG_DWRE) == 0) {
		DNPRINTF(MPII_D_MISC, "%s: mpii_reset_hard failure to enable "
		    "diagnostic read/write\n", DEVNAME(sc));
		return(1);
	}

	/* reset ioc */
	mpii_write(sc, MPII_HOSTDIAG, MPII_HOSTDIAG_RESET_ADAPTER);

	/* 240 milliseconds */
	delay(240000);


	/* XXX this whole function should be more robust */

	/* XXX  read the host diagnostic reg until reset adapter bit clears ? */
	for (i = 0; i < 30000; i++) {
		if ((mpii_read(sc, MPII_HOSTDIAG) &
		    MPII_HOSTDIAG_RESET_ADAPTER) == 0)
			break;
		delay(10000);
	}

	/* disable diagnostic register */
	mpii_write(sc, MPII_WRITESEQ, 0xff);

	/* XXX what else? */

	DNPRINTF(MPII_D_MISC, "%s: done with mpii_reset_hard\n", DEVNAME(sc));

	return(0);
}

int
mpii_handshake_send(struct mpii_softc *sc, void *buf, size_t dwords)
{
	u_int32_t		*query = buf;
	int			i;

	/* make sure the doorbell is not in use. */
	if (mpii_read_db(sc) & MPII_DOORBELL_INUSE)
		return (1);

	/* clear pending doorbell interrupts */
	if (mpii_read_intr(sc) & MPII_INTR_STATUS_IOC2SYSDB)
		mpii_write_intr(sc, 0);

	/*
	 * first write the doorbell with the handshake function and the
	 * dword count.
	 */
	mpii_write_db(sc, MPII_DOORBELL_FUNCTION(MPII_FUNCTION_HANDSHAKE) |
	    MPII_DOORBELL_DWORDS(dwords));

	/*
	 * the doorbell used bit will be set because a doorbell function has
	 * started. wait for the interrupt and then ack it.
	 */
	if (mpii_wait_db_int(sc) != 0)
		return (1);
	mpii_write_intr(sc, 0);

	/* poll for the acknowledgement. */
	if (mpii_wait_db_ack(sc) != 0)
		return (1);

	/* write the query through the doorbell. */
	for (i = 0; i < dwords; i++) {
		mpii_write_db(sc, htole32(query[i]));
		if (mpii_wait_db_ack(sc) != 0)
			return (1);
	}

	return (0);
}

int
mpii_handshake_recv_dword(struct mpii_softc *sc, u_int32_t *dword)
{
	u_int16_t		*words = (u_int16_t *)dword;
	int			i;

	for (i = 0; i < 2; i++) {
		if (mpii_wait_db_int(sc) != 0)
			return (1);
		words[i] = letoh16(mpii_read_db(sc) & MPII_DOORBELL_DATA_MASK);
		mpii_write_intr(sc, 0);
	}

	return (0);
}

int
mpii_handshake_recv(struct mpii_softc *sc, void *buf, size_t dwords)
{
	struct mpii_msg_reply	*reply = buf;
	u_int32_t		*dbuf = buf, dummy;
	int			i;

	/* get the first dword so we can read the length out of the header. */
	if (mpii_handshake_recv_dword(sc, &dbuf[0]) != 0)
		return (1);

	DNPRINTF(MPII_D_CMD, "%s: mpii_handshake_recv dwords: %lu reply: %d\n",
	    DEVNAME(sc), dwords, reply->msg_length);

	/*
	 * the total length, in dwords, is in the message length field of the
	 * reply header.
	 */
	for (i = 1; i < MIN(dwords, reply->msg_length); i++) {
		if (mpii_handshake_recv_dword(sc, &dbuf[i]) != 0)
			return (1);
	}

	/* if there's extra stuff to come off the ioc, discard it */
	while (i++ < reply->msg_length) {
		if (mpii_handshake_recv_dword(sc, &dummy) != 0)
			return (1);
		DNPRINTF(MPII_D_CMD, "%s: mpii_handshake_recv dummy read: "
		    "0x%08x\n", DEVNAME(sc), dummy);
	}

	/* wait for the doorbell used bit to be reset and clear the intr */
	if (mpii_wait_db_int(sc) != 0)
		return (1);

	if (mpii_wait_eq(sc, MPII_DOORBELL, MPII_DOORBELL_INUSE, 0) != 0)
		return (1);

	mpii_write_intr(sc, 0);

	return (0);
}

void
mpii_empty_done(struct mpii_ccb *ccb)
{
	/* nothing to do */
}

int
mpii_iocfacts(struct mpii_softc *sc)
{
	struct mpii_msg_iocfacts_request	ifq;
	struct mpii_msg_iocfacts_reply		ifp;
	int					irs;
	int					sge_size;
	u_int					qdepth;

	DNPRINTF(MPII_D_MISC, "%s: mpii_iocfacts\n", DEVNAME(sc));

	memset(&ifq, 0, sizeof(ifq));
	memset(&ifp, 0, sizeof(ifp));

	ifq.function = MPII_FUNCTION_IOC_FACTS;

	if (mpii_handshake_send(sc, &ifq, dwordsof(ifq)) != 0) {
		DNPRINTF(MPII_D_MISC, "%s: mpii_iocfacts send failed\n",
		    DEVNAME(sc));
		return (1);
	}

	if (mpii_handshake_recv(sc, &ifp, dwordsof(ifp)) != 0) {
		DNPRINTF(MPII_D_MISC, "%s: mpii_iocfacts recv failed\n",
		    DEVNAME(sc));
		return (1);
	}

	sc->sc_ioc_number = ifp.ioc_number;
	sc->sc_vf_id = ifp.vf_id;

	sc->sc_max_volumes = ifp.max_volumes;
	sc->sc_max_devices = ifp.max_volumes + lemtoh16(&ifp.max_targets);

	if (ISSET(lemtoh32(&ifp.ioc_capabilities),
	    MPII_IOCFACTS_CAPABILITY_INTEGRATED_RAID))
		SET(sc->sc_flags, MPII_F_RAID);
	if (ISSET(lemtoh32(&ifp.ioc_capabilities),
	    MPII_IOCFACTS_CAPABILITY_EVENT_REPLAY))
		sc->sc_ioc_event_replay = 1;

	sc->sc_max_cmds = MIN(lemtoh16(&ifp.request_credit),
	    MPII_REQUEST_CREDIT);

	/* SAS3 and 3.5 controllers have different sgl layouts */
	if (ifp.msg_version_maj == 2 && ((ifp.msg_version_min == 5)
	    || (ifp.msg_version_min == 6)))
		SET(sc->sc_flags, MPII_F_SAS3);

	/*
	 * The host driver must ensure that there is at least one
	 * unused entry in the Reply Free Queue. One way to ensure
	 * that this requirement is met is to never allocate a number
	 * of reply frames that is a multiple of 16.
	 */
	sc->sc_num_reply_frames = sc->sc_max_cmds + 32;
	if (!(sc->sc_num_reply_frames % 16))
		sc->sc_num_reply_frames--;

	/* must be multiple of 16 */
	sc->sc_reply_post_qdepth = sc->sc_max_cmds +
	    sc->sc_num_reply_frames;
	sc->sc_reply_post_qdepth += 16 - (sc->sc_reply_post_qdepth % 16);

	qdepth = lemtoh16(&ifp.max_reply_descriptor_post_queue_depth);
	if (sc->sc_reply_post_qdepth > qdepth) {
		sc->sc_reply_post_qdepth = qdepth;
		if (sc->sc_reply_post_qdepth < 16) {
			printf("%s: RDPQ is too shallow\n", DEVNAME(sc));
			return (1);
		}
		sc->sc_max_cmds = sc->sc_reply_post_qdepth / 2 - 4;
		sc->sc_num_reply_frames = sc->sc_max_cmds + 4;
	}

	sc->sc_reply_free_qdepth = sc->sc_num_reply_frames +
	    16 - (sc->sc_num_reply_frames % 16);

	/*
	 * Our request frame for an I/O operation looks like this:
	 *
	 * +-------------------+ -.
	 * | mpii_msg_scsi_io  |  |
	 * +-------------------|  |
	 * | mpii_sge          |  |
	 * + - - - - - - - - - +  |
	 * | ...               |  > ioc_request_frame_size
	 * + - - - - - - - - - +  |
	 * | mpii_sge (tail)   |  |
	 * + - - - - - - - - - +  |
	 * | mpii_sge (csge)   |  | --.
	 * + - - - - - - - - - + -'   | chain sge points to the next sge
	 * | mpii_sge          |<-----'
	 * + - - - - - - - - - +
	 * | ...               |
	 * + - - - - - - - - - +
	 * | mpii_sge (tail)   |
	 * +-------------------+
	 * |                   |
	 * ~~~~~~~~~~~~~~~~~~~~~
	 * |                   |
	 * +-------------------+ <- sc_request_size - sizeof(scsi_sense_data)
	 * | scsi_sense_data   |
	 * +-------------------+
	 *
	 * If the controller gives us a maximum chain size, there can be
	 * multiple chain sges, each of which points to the sge following it.
	 * Otherwise, there will only be one chain sge.
	 */

	/* both sizes are in 32-bit words */
	sc->sc_reply_size = ifp.reply_frame_size * 4;
	irs = lemtoh16(&ifp.ioc_request_frame_size) * 4;
	sc->sc_request_size = MPII_REQUEST_SIZE;
	/* make sure we have enough space for scsi sense data */
	if (irs > sc->sc_request_size) {
		sc->sc_request_size = irs + sizeof(struct scsi_sense_data);
		sc->sc_request_size += 16 - (sc->sc_request_size % 16);
	}

	if (ISSET(sc->sc_flags, MPII_F_SAS3)) {
		sge_size = sizeof(struct mpii_ieee_sge);
	} else {
		sge_size = sizeof(struct mpii_sge);
	}

	/* offset to the chain sge */
	sc->sc_chain_sge = (irs - sizeof(struct mpii_msg_scsi_io)) /
	    sge_size - 1;

	sc->sc_max_chain = lemtoh16(&ifp.ioc_max_chain_seg_size);

	/*
	 * A number of simple scatter-gather elements we can fit into the
	 * request buffer after the I/O command minus the chain element(s).
	 */
	sc->sc_max_sgl = (sc->sc_request_size -
 	    sizeof(struct mpii_msg_scsi_io) - sizeof(struct scsi_sense_data)) /
	    sge_size - 1;
	if (sc->sc_max_chain > 0) {
		sc->sc_max_sgl -= (sc->sc_max_sgl - sc->sc_chain_sge) /
		    sc->sc_max_chain;
	}

	return (0);
}

int
mpii_iocinit(struct mpii_softc *sc)
{
	struct mpii_msg_iocinit_request		iiq;
	struct mpii_msg_iocinit_reply		iip;

	DNPRINTF(MPII_D_MISC, "%s: mpii_iocinit\n", DEVNAME(sc));

	memset(&iiq, 0, sizeof(iiq));
	memset(&iip, 0, sizeof(iip));

	iiq.function = MPII_FUNCTION_IOC_INIT;
	iiq.whoinit = MPII_WHOINIT_HOST_DRIVER;

	/* XXX JPG do something about vf_id */
	iiq.vf_id = 0;

	iiq.msg_version_maj = 0x02;
	iiq.msg_version_min = 0x00;

	/* XXX JPG ensure compliance with some level and hard-code? */
	iiq.hdr_version_unit = 0x00;
	iiq.hdr_version_dev = 0x00;

	htolem16(&iiq.system_request_frame_size, sc->sc_request_size / 4);

	htolem16(&iiq.reply_descriptor_post_queue_depth,
	    sc->sc_reply_post_qdepth);

	htolem16(&iiq.reply_free_queue_depth, sc->sc_reply_free_qdepth);

	htolem32(&iiq.sense_buffer_address_high,
	    MPII_DMA_DVA(sc->sc_requests) >> 32);

	htolem32(&iiq.system_reply_address_high,
	    MPII_DMA_DVA(sc->sc_replies) >> 32);

	htolem32(&iiq.system_request_frame_base_address_lo,
	    MPII_DMA_DVA(sc->sc_requests));
	htolem32(&iiq.system_request_frame_base_address_hi,
	    MPII_DMA_DVA(sc->sc_requests) >> 32);

	htolem32(&iiq.reply_descriptor_post_queue_address_lo,
	    MPII_DMA_DVA(sc->sc_reply_postq));
	htolem32(&iiq.reply_descriptor_post_queue_address_hi,
	    MPII_DMA_DVA(sc->sc_reply_postq) >> 32);

	htolem32(&iiq.reply_free_queue_address_lo,
	    MPII_DMA_DVA(sc->sc_reply_freeq));
	htolem32(&iiq.reply_free_queue_address_hi,
	    MPII_DMA_DVA(sc->sc_reply_freeq) >> 32);

	if (mpii_handshake_send(sc, &iiq, dwordsof(iiq)) != 0) {
		DNPRINTF(MPII_D_MISC, "%s: mpii_iocinit send failed\n",
		    DEVNAME(sc));
		return (1);
	}

	if (mpii_handshake_recv(sc, &iip, dwordsof(iip)) != 0) {
		DNPRINTF(MPII_D_MISC, "%s: mpii_iocinit recv failed\n",
		    DEVNAME(sc));
		return (1);
	}

	DNPRINTF(MPII_D_MISC, "%s:  function: 0x%02x msg_length: %d "
	    "whoinit: 0x%02x\n", DEVNAME(sc), iip.function,
	    iip.msg_length, iip.whoinit);
	DNPRINTF(MPII_D_MISC, "%s:  msg_flags: 0x%02x\n", DEVNAME(sc),
	    iip.msg_flags);
	DNPRINTF(MPII_D_MISC, "%s:  vf_id: 0x%02x vp_id: 0x%02x\n", DEVNAME(sc),
	    iip.vf_id, iip.vp_id);
	DNPRINTF(MPII_D_MISC, "%s:  ioc_status: 0x%04x\n", DEVNAME(sc),
	    lemtoh16(&iip.ioc_status));
	DNPRINTF(MPII_D_MISC, "%s:  ioc_loginfo: 0x%08x\n", DEVNAME(sc),
	    lemtoh32(&iip.ioc_loginfo));

	if (lemtoh16(&iip.ioc_status) != MPII_IOCSTATUS_SUCCESS ||
	    lemtoh32(&iip.ioc_loginfo))
		return (1);

	return (0);
}

void
mpii_push_reply(struct mpii_softc *sc, struct mpii_rcb *rcb)
{
	u_int32_t		*rfp;
	u_int			idx;

	if (rcb == NULL)
		return;

	idx = sc->sc_reply_free_host_index;

	rfp = MPII_DMA_KVA(sc->sc_reply_freeq);
	htolem32(&rfp[idx], rcb->rcb_reply_dva);

	if (++idx >= sc->sc_reply_free_qdepth)
		idx = 0;

	mpii_write_reply_free(sc, sc->sc_reply_free_host_index = idx);
}

int
mpii_portfacts(struct mpii_softc *sc)
{
	struct mpii_msg_portfacts_request	*pfq;
	struct mpii_msg_portfacts_reply		*pfp;
	struct mpii_ccb				*ccb;
	int					rv = 1;

	DNPRINTF(MPII_D_MISC, "%s: mpii_portfacts\n", DEVNAME(sc));

	ccb = scsi_io_get(&sc->sc_iopool, 0);
	if (ccb == NULL) {
		DNPRINTF(MPII_D_MISC, "%s: mpii_portfacts mpii_get_ccb fail\n",
		    DEVNAME(sc));
		return (rv);
	}

	ccb->ccb_done = mpii_empty_done;
	pfq = ccb->ccb_cmd;

	memset(pfq, 0, sizeof(*pfq));

	pfq->function = MPII_FUNCTION_PORT_FACTS;
	pfq->chain_offset = 0;
	pfq->msg_flags = 0;
	pfq->port_number = 0;
	pfq->vp_id = 0;
	pfq->vf_id = 0;

	if (mpii_poll(sc, ccb) != 0) {
		DNPRINTF(MPII_D_MISC, "%s: mpii_portfacts poll\n",
		    DEVNAME(sc));
		goto err;
	}

	if (ccb->ccb_rcb == NULL) {
		DNPRINTF(MPII_D_MISC, "%s: empty portfacts reply\n",
		    DEVNAME(sc));
		goto err;
	}

	pfp = ccb->ccb_rcb->rcb_reply;
	sc->sc_porttype = pfp->port_type;

	mpii_push_reply(sc, ccb->ccb_rcb);
	rv = 0;
err:
	scsi_io_put(&sc->sc_iopool, ccb);

	return (rv);
}

void
mpii_eventack(void *cookie, void *io)
{
	struct mpii_softc			*sc = cookie;
	struct mpii_ccb				*ccb = io;
	struct mpii_rcb				*rcb, *next;
	struct mpii_msg_event_reply		*enp;
	struct mpii_msg_eventack_request	*eaq;

	mtx_enter(&sc->sc_evt_ack_mtx);
	rcb = SIMPLEQ_FIRST(&sc->sc_evt_ack_queue);
	if (rcb != NULL) {
		next = SIMPLEQ_NEXT(rcb, rcb_link);
		SIMPLEQ_REMOVE_HEAD(&sc->sc_evt_ack_queue, rcb_link);
	}
	mtx_leave(&sc->sc_evt_ack_mtx);

	if (rcb == NULL) {
		scsi_io_put(&sc->sc_iopool, ccb);
		return;
	}

	enp = (struct mpii_msg_event_reply *)rcb->rcb_reply;

	ccb->ccb_done = mpii_eventack_done;
	eaq = ccb->ccb_cmd;

	eaq->function = MPII_FUNCTION_EVENT_ACK;

	eaq->event = enp->event;
	eaq->event_context = enp->event_context;

	mpii_push_reply(sc, rcb);

	mpii_start(sc, ccb);

	if (next != NULL)
		scsi_ioh_add(&sc->sc_evt_ack_handler);
}

void
mpii_eventack_done(struct mpii_ccb *ccb)
{
	struct mpii_softc			*sc = ccb->ccb_sc;

	DNPRINTF(MPII_D_EVT, "%s: event ack done\n", DEVNAME(sc));

	mpii_push_reply(sc, ccb->ccb_rcb);
	scsi_io_put(&sc->sc_iopool, ccb);
}

int
mpii_portenable(struct mpii_softc *sc)
{
	struct mpii_msg_portenable_request	*peq;
	struct mpii_ccb				*ccb;

	DNPRINTF(MPII_D_MISC, "%s: mpii_portenable\n", DEVNAME(sc));

	ccb = scsi_io_get(&sc->sc_iopool, 0);
	if (ccb == NULL) {
		DNPRINTF(MPII_D_MISC, "%s: mpii_portenable ccb_get\n",
		    DEVNAME(sc));
		return (1);
	}

	ccb->ccb_done = mpii_empty_done;
	peq = ccb->ccb_cmd;

	peq->function = MPII_FUNCTION_PORT_ENABLE;
	peq->vf_id = sc->sc_vf_id;

	if (mpii_poll(sc, ccb) != 0) {
		DNPRINTF(MPII_D_MISC, "%s: mpii_portenable poll\n",
		    DEVNAME(sc));
		return (1);
	}

	if (ccb->ccb_rcb == NULL) {
		DNPRINTF(MPII_D_MISC, "%s: empty portenable reply\n",
		    DEVNAME(sc));
		return (1);
	}

	mpii_push_reply(sc, ccb->ccb_rcb);
	scsi_io_put(&sc->sc_iopool, ccb);

	return (0);
}

int
mpii_cfg_coalescing(struct mpii_softc *sc)
{
	struct mpii_cfg_hdr			hdr;
	struct mpii_cfg_ioc_pg1			ipg;

	hdr.page_version = 0;
	hdr.page_length = sizeof(ipg) / 4;
	hdr.page_number = 1;
	hdr.page_type = MPII_CONFIG_REQ_PAGE_TYPE_IOC;
	memset(&ipg, 0, sizeof(ipg));
	if (mpii_req_cfg_page(sc, 0, MPII_PG_POLL, &hdr, 1, &ipg,
	    sizeof(ipg)) != 0) {
		DNPRINTF(MPII_D_MISC, "%s: unable to fetch IOC page 1\n"
		    "page 1\n", DEVNAME(sc));
		return (1);
	}

	if (!ISSET(lemtoh32(&ipg.flags), MPII_CFG_IOC_1_REPLY_COALESCING))
		return (0);

	/* Disable coalescing */
	CLR(ipg.flags, htole32(MPII_CFG_IOC_1_REPLY_COALESCING));
	if (mpii_req_cfg_page(sc, 0, MPII_PG_POLL, &hdr, 0, &ipg,
	    sizeof(ipg)) != 0) {
		DNPRINTF(MPII_D_MISC, "%s: unable to clear coalescing\n",
		    DEVNAME(sc));
		return (1);
	}

	return (0);
}

#define MPII_EVENT_MASKALL(enq)		do {			\
		enq->event_masks[0] = 0xffffffff;		\
		enq->event_masks[1] = 0xffffffff;		\
		enq->event_masks[2] = 0xffffffff;		\
		enq->event_masks[3] = 0xffffffff;		\
	} while (0)

#define MPII_EVENT_UNMASK(enq, evt)	do {			\
		enq->event_masks[evt / 32] &=			\
		    htole32(~(1 << (evt % 32)));		\
	} while (0)

int
mpii_eventnotify(struct mpii_softc *sc)
{
	struct mpii_msg_event_request		*enq;
	struct mpii_ccb				*ccb;

	ccb = scsi_io_get(&sc->sc_iopool, 0);
	if (ccb == NULL) {
		DNPRINTF(MPII_D_MISC, "%s: mpii_eventnotify ccb_get\n",
		    DEVNAME(sc));
		return (1);
	}

	SIMPLEQ_INIT(&sc->sc_evt_sas_queue);
	mtx_init(&sc->sc_evt_sas_mtx, IPL_BIO);
	task_set(&sc->sc_evt_sas_task, mpii_event_sas, sc);

	SIMPLEQ_INIT(&sc->sc_evt_ack_queue);
	mtx_init(&sc->sc_evt_ack_mtx, IPL_BIO);
	scsi_ioh_set(&sc->sc_evt_ack_handler, &sc->sc_iopool,
	    mpii_eventack, sc);

	ccb->ccb_done = mpii_eventnotify_done;
	enq = ccb->ccb_cmd;

	enq->function = MPII_FUNCTION_EVENT_NOTIFICATION;

	/*
	 * Enable reporting of the following events:
	 *
	 * MPII_EVENT_SAS_DISCOVERY
	 * MPII_EVENT_SAS_TOPOLOGY_CHANGE_LIST
	 * MPII_EVENT_SAS_DEVICE_STATUS_CHANGE
	 * MPII_EVENT_SAS_ENCL_DEVICE_STATUS_CHANGE
	 * MPII_EVENT_IR_CONFIGURATION_CHANGE_LIST
	 * MPII_EVENT_IR_VOLUME
	 * MPII_EVENT_IR_PHYSICAL_DISK
	 * MPII_EVENT_IR_OPERATION_STATUS
	 */

	MPII_EVENT_MASKALL(enq);
	MPII_EVENT_UNMASK(enq, MPII_EVENT_SAS_DISCOVERY);
	MPII_EVENT_UNMASK(enq, MPII_EVENT_SAS_TOPOLOGY_CHANGE_LIST);
	MPII_EVENT_UNMASK(enq, MPII_EVENT_SAS_DEVICE_STATUS_CHANGE);
	MPII_EVENT_UNMASK(enq, MPII_EVENT_SAS_ENCL_DEVICE_STATUS_CHANGE);
	MPII_EVENT_UNMASK(enq, MPII_EVENT_IR_CONFIGURATION_CHANGE_LIST);
	MPII_EVENT_UNMASK(enq, MPII_EVENT_IR_VOLUME);
	MPII_EVENT_UNMASK(enq, MPII_EVENT_IR_PHYSICAL_DISK);
	MPII_EVENT_UNMASK(enq, MPII_EVENT_IR_OPERATION_STATUS);

	mpii_start(sc, ccb);

	return (0);
}

void
mpii_eventnotify_done(struct mpii_ccb *ccb)
{
	struct mpii_softc			*sc = ccb->ccb_sc;
	struct mpii_rcb				*rcb = ccb->ccb_rcb;

	DNPRINTF(MPII_D_EVT, "%s: mpii_eventnotify_done\n", DEVNAME(sc));

	scsi_io_put(&sc->sc_iopool, ccb);
	mpii_event_process(sc, rcb);
}

void
mpii_event_raid(struct mpii_softc *sc, struct mpii_msg_event_reply *enp)
{
	struct mpii_evt_ir_cfg_change_list	*ccl;
	struct mpii_evt_ir_cfg_element		*ce;
	struct mpii_device			*dev;
	u_int16_t				type;
	int					i;

	ccl = (struct mpii_evt_ir_cfg_change_list *)(enp + 1);
	if (ccl->num_elements == 0)
		return;

	if (ISSET(lemtoh32(&ccl->flags), MPII_EVT_IR_CFG_CHANGE_LIST_FOREIGN)) {
		/* bail on foreign configurations */
		return;
	}

	ce = (struct mpii_evt_ir_cfg_element *)(ccl + 1);

	for (i = 0; i < ccl->num_elements; i++, ce++) {
		type = (lemtoh16(&ce->element_flags) &
		    MPII_EVT_IR_CFG_ELEMENT_TYPE_MASK);

		switch (type) {
		case MPII_EVT_IR_CFG_ELEMENT_TYPE_VOLUME:
			switch (ce->reason_code) {
			case MPII_EVT_IR_CFG_ELEMENT_RC_ADDED:
			case MPII_EVT_IR_CFG_ELEMENT_RC_VOLUME_CREATED:
				if (mpii_find_dev(sc,
				    lemtoh16(&ce->vol_dev_handle))) {
					printf("%s: device %#x is already "
					    "configured\n", DEVNAME(sc),
					    lemtoh16(&ce->vol_dev_handle));
					break;
				}
				dev = malloc(sizeof(*dev), M_DEVBUF,
				    M_NOWAIT | M_ZERO);
				if (!dev) {
					printf("%s: failed to allocate a "
					    "device structure\n", DEVNAME(sc));
					break;
				}
				SET(dev->flags, MPII_DF_VOLUME);
				dev->slot = sc->sc_vd_id_low;
				dev->dev_handle = lemtoh16(&ce->vol_dev_handle);
				if (mpii_insert_dev(sc, dev)) {
					free(dev, M_DEVBUF, sizeof *dev);
					break;
				}
				sc->sc_vd_count++;
				break;
			case MPII_EVT_IR_CFG_ELEMENT_RC_REMOVED:
			case MPII_EVT_IR_CFG_ELEMENT_RC_VOLUME_DELETED:
				if (!(dev = mpii_find_dev(sc,
				    lemtoh16(&ce->vol_dev_handle))))
					break;
				mpii_remove_dev(sc, dev);
				sc->sc_vd_count--;
				break;
			}
			break;
		case MPII_EVT_IR_CFG_ELEMENT_TYPE_VOLUME_DISK:
			if (ce->reason_code ==
			    MPII_EVT_IR_CFG_ELEMENT_RC_PD_CREATED ||
			    ce->reason_code ==
			    MPII_EVT_IR_CFG_ELEMENT_RC_HIDE) {
				/* there should be an underlying sas drive */
				if (!(dev = mpii_find_dev(sc,
				    lemtoh16(&ce->phys_disk_dev_handle))))
					break;
				/* promoted from a hot spare? */
				CLR(dev->flags, MPII_DF_HOT_SPARE);
				SET(dev->flags, MPII_DF_VOLUME_DISK |
				    MPII_DF_HIDDEN);
			}
			break;
		case MPII_EVT_IR_CFG_ELEMENT_TYPE_HOT_SPARE:
			if (ce->reason_code ==
			    MPII_EVT_IR_CFG_ELEMENT_RC_HIDE) {
				/* there should be an underlying sas drive */
				if (!(dev = mpii_find_dev(sc,
				    lemtoh16(&ce->phys_disk_dev_handle))))
					break;
				SET(dev->flags, MPII_DF_HOT_SPARE |
				    MPII_DF_HIDDEN);
			}
			break;
		}
	}
}

void
mpii_event_sas(void *xsc)
{
	struct mpii_softc *sc = xsc;
	struct mpii_rcb *rcb, *next;
	struct mpii_msg_event_reply *enp;
	struct mpii_evt_sas_tcl		*tcl;
	struct mpii_evt_phy_entry	*pe;
	struct mpii_device		*dev;
	int				i;
	u_int16_t			handle;

	mtx_enter(&sc->sc_evt_sas_mtx);
	rcb = SIMPLEQ_FIRST(&sc->sc_evt_sas_queue);
	if (rcb != NULL) {
		next = SIMPLEQ_NEXT(rcb, rcb_link);
		SIMPLEQ_REMOVE_HEAD(&sc->sc_evt_sas_queue, rcb_link);
	}
	mtx_leave(&sc->sc_evt_sas_mtx);

	if (rcb == NULL)
		return;
	if (next != NULL)
		task_add(systq, &sc->sc_evt_sas_task);

	enp = (struct mpii_msg_event_reply *)rcb->rcb_reply;
	switch (lemtoh16(&enp->event)) {
	case MPII_EVENT_SAS_DISCOVERY:
		mpii_event_discovery(sc, enp);
		goto done;
	case MPII_EVENT_SAS_TOPOLOGY_CHANGE_LIST:
		/* handle below */
		break;
	default:
		panic("%s: unexpected event %#x in sas event queue",
		    DEVNAME(sc), lemtoh16(&enp->event));
		/* NOTREACHED */
	}

	tcl = (struct mpii_evt_sas_tcl *)(enp + 1);
	pe = (struct mpii_evt_phy_entry *)(tcl + 1);

	for (i = 0; i < tcl->num_entries; i++, pe++) {
		switch (pe->phy_status & MPII_EVENT_SAS_TOPO_PS_RC_MASK) {
		case MPII_EVENT_SAS_TOPO_PS_RC_ADDED:
			handle = lemtoh16(&pe->dev_handle);
			if (mpii_find_dev(sc, handle)) {
				printf("%s: device %#x is already "
				    "configured\n", DEVNAME(sc), handle);
				break;
			}

			dev = malloc(sizeof(*dev), M_DEVBUF, M_WAITOK | M_ZERO);
			dev->slot = sc->sc_pd_id_start + tcl->start_phy_num + i;
			dev->dev_handle = handle;
			dev->phy_num = tcl->start_phy_num + i;
			if (tcl->enclosure_handle)
				dev->physical_port = tcl->physical_port;
			dev->enclosure = lemtoh16(&tcl->enclosure_handle);
			dev->expander = lemtoh16(&tcl->expander_handle);

			if (mpii_insert_dev(sc, dev)) {
				free(dev, M_DEVBUF, sizeof *dev);
				break;
			}

			if (sc->sc_scsibus != NULL)
				scsi_probe_target(sc->sc_scsibus, dev->slot);
			break;

		case MPII_EVENT_SAS_TOPO_PS_RC_MISSING:
			dev = mpii_find_dev(sc, lemtoh16(&pe->dev_handle));
			if (dev == NULL)
				break;

			mpii_remove_dev(sc, dev);
			mpii_sas_remove_device(sc, dev->dev_handle);
			if (sc->sc_scsibus != NULL &&
			    !ISSET(dev->flags, MPII_DF_HIDDEN)) {
				scsi_activate(sc->sc_scsibus, dev->slot, -1,
				    DVACT_DEACTIVATE);
				scsi_detach_target(sc->sc_scsibus, dev->slot,
				    DETACH_FORCE);
			}

			free(dev, M_DEVBUF, sizeof *dev);
			break;
		}
	}

done:
	mpii_event_done(sc, rcb);
}

void
mpii_event_discovery(struct mpii_softc *sc, struct mpii_msg_event_reply *enp)
{
	struct mpii_evt_sas_discovery *esd =
	    (struct mpii_evt_sas_discovery *)(enp + 1);

	if (sc->sc_pending == 0)
		return;

	switch (esd->reason_code) {
	case MPII_EVENT_SAS_DISC_REASON_CODE_STARTED:
		++sc->sc_pending;
		break;
	case MPII_EVENT_SAS_DISC_REASON_CODE_COMPLETED:
		if (--sc->sc_pending == 1) {
			sc->sc_pending = 0;
			config_pending_decr();
		}
		break;
	}
}

void
mpii_event_process(struct mpii_softc *sc, struct mpii_rcb *rcb)
{
	struct mpii_msg_event_reply		*enp;

	enp = (struct mpii_msg_event_reply *)rcb->rcb_reply;

	DNPRINTF(MPII_D_EVT, "%s: mpii_event_process: %#x\n", DEVNAME(sc),
	    lemtoh16(&enp->event));

	switch (lemtoh16(&enp->event)) {
	case MPII_EVENT_EVENT_CHANGE:
		/* should be properly ignored */
		break;
	case MPII_EVENT_SAS_DISCOVERY:
	case MPII_EVENT_SAS_TOPOLOGY_CHANGE_LIST:
		mtx_enter(&sc->sc_evt_sas_mtx);
		SIMPLEQ_INSERT_TAIL(&sc->sc_evt_sas_queue, rcb, rcb_link);
		mtx_leave(&sc->sc_evt_sas_mtx);
		task_add(systq, &sc->sc_evt_sas_task);
		return;
	case MPII_EVENT_SAS_DEVICE_STATUS_CHANGE:
		break;
	case MPII_EVENT_SAS_ENCL_DEVICE_STATUS_CHANGE:
		break;
	case MPII_EVENT_IR_VOLUME: {
		struct mpii_evt_ir_volume	*evd =
		    (struct mpii_evt_ir_volume *)(enp + 1);
		struct mpii_device		*dev;
#if NBIO > 0
		const char *vol_states[] = {
			BIOC_SVINVALID_S,
			BIOC_SVOFFLINE_S,
			BIOC_SVBUILDING_S,
			BIOC_SVONLINE_S,
			BIOC_SVDEGRADED_S,
			BIOC_SVONLINE_S,
		};
#endif

		if (cold)
			break;
		KERNEL_LOCK();
		dev = mpii_find_dev(sc, lemtoh16(&evd->vol_dev_handle));
		KERNEL_UNLOCK();
		if (dev == NULL)
			break;
#if NBIO > 0
		if (evd->reason_code == MPII_EVENT_IR_VOL_RC_STATE_CHANGED)
			printf("%s: volume %d state changed from %s to %s\n",
			    DEVNAME(sc), dev->slot - sc->sc_vd_id_low,
			    vol_states[evd->prev_value],
			    vol_states[evd->new_value]);
#endif
		if (evd->reason_code == MPII_EVENT_IR_VOL_RC_STATUS_CHANGED &&
		    ISSET(evd->new_value, MPII_CFG_RAID_VOL_0_STATUS_RESYNC) &&
		    !ISSET(evd->prev_value, MPII_CFG_RAID_VOL_0_STATUS_RESYNC))
			printf("%s: started resync on a volume %d\n",
			    DEVNAME(sc), dev->slot - sc->sc_vd_id_low);
		}
		break;
	case MPII_EVENT_IR_PHYSICAL_DISK:
		break;
	case MPII_EVENT_IR_CONFIGURATION_CHANGE_LIST:
		mpii_event_raid(sc, enp);
		break;
	case MPII_EVENT_IR_OPERATION_STATUS: {
		struct mpii_evt_ir_status	*evs =
		    (struct mpii_evt_ir_status *)(enp + 1);
		struct mpii_device		*dev;

		KERNEL_LOCK();
		dev = mpii_find_dev(sc, lemtoh16(&evs->vol_dev_handle));
		KERNEL_UNLOCK();
		if (dev != NULL &&
		    evs->operation == MPII_EVENT_IR_RAIDOP_RESYNC)
			dev->percent = evs->percent;
		break;
		}
	default:
		DNPRINTF(MPII_D_EVT, "%s:  unhandled event 0x%02x\n",
		    DEVNAME(sc), lemtoh16(&enp->event));
	}

	mpii_event_done(sc, rcb);
}

void
mpii_event_done(struct mpii_softc *sc, struct mpii_rcb *rcb)
{
	struct mpii_msg_event_reply *enp = rcb->rcb_reply;

	if (enp->ack_required) {
		mtx_enter(&sc->sc_evt_ack_mtx);
		SIMPLEQ_INSERT_TAIL(&sc->sc_evt_ack_queue, rcb, rcb_link);
		mtx_leave(&sc->sc_evt_ack_mtx);
		scsi_ioh_add(&sc->sc_evt_ack_handler);
	} else
		mpii_push_reply(sc, rcb);
}

void
mpii_sas_remove_device(struct mpii_softc *sc, u_int16_t handle)
{
	struct mpii_msg_scsi_task_request	*stq;
	struct mpii_msg_sas_oper_request	*soq;
	struct mpii_ccb				*ccb;

	ccb = scsi_io_get(&sc->sc_iopool, 0);
	if (ccb == NULL)
		return;

	stq = ccb->ccb_cmd;
	stq->function = MPII_FUNCTION_SCSI_TASK_MGMT;
	stq->task_type = MPII_SCSI_TASK_TARGET_RESET;
	htolem16(&stq->dev_handle, handle);

	ccb->ccb_done = mpii_empty_done;
	mpii_wait(sc, ccb);

	if (ccb->ccb_rcb != NULL)
		mpii_push_reply(sc, ccb->ccb_rcb);

	/* reuse a ccb */
	ccb->ccb_state = MPII_CCB_READY;
	ccb->ccb_rcb = NULL;

	soq = ccb->ccb_cmd;
	memset(soq, 0, sizeof(*soq));
	soq->function = MPII_FUNCTION_SAS_IO_UNIT_CONTROL;
	soq->operation = MPII_SAS_OP_REMOVE_DEVICE;
	htolem16(&soq->dev_handle, handle);

	ccb->ccb_done = mpii_empty_done;
	mpii_wait(sc, ccb);
	if (ccb->ccb_rcb != NULL)
		mpii_push_reply(sc, ccb->ccb_rcb);

	scsi_io_put(&sc->sc_iopool, ccb);
}

int
mpii_board_info(struct mpii_softc *sc)
{
	struct mpii_msg_iocfacts_request	ifq;
	struct mpii_msg_iocfacts_reply		ifp;
	struct mpii_cfg_manufacturing_pg0	mpg;
	struct mpii_cfg_hdr			hdr;

	memset(&ifq, 0, sizeof(ifq));
	memset(&ifp, 0, sizeof(ifp));

	ifq.function = MPII_FUNCTION_IOC_FACTS;

	if (mpii_handshake_send(sc, &ifq, dwordsof(ifq)) != 0) {
		DNPRINTF(MPII_D_MISC, "%s: failed to request ioc facts\n",
		    DEVNAME(sc));
		return (1);
	}

	if (mpii_handshake_recv(sc, &ifp, dwordsof(ifp)) != 0) {
		DNPRINTF(MPII_D_MISC, "%s: failed to receive ioc facts\n",
		    DEVNAME(sc));
		return (1);
	}

	hdr.page_version = 0;
	hdr.page_length = sizeof(mpg) / 4;
	hdr.page_number = 0;
	hdr.page_type = MPII_CONFIG_REQ_PAGE_TYPE_MANUFACTURING;
	memset(&mpg, 0, sizeof(mpg));
	if (mpii_req_cfg_page(sc, 0, MPII_PG_POLL, &hdr, 1, &mpg,
	    sizeof(mpg)) != 0) {
		printf("%s: unable to fetch manufacturing page 0\n",
		    DEVNAME(sc));
		return (EINVAL);
	}

	printf("%s: %s, firmware %u.%u.%u.%u%s, MPI %u.%u\n", DEVNAME(sc),
	    mpg.board_name, ifp.fw_version_maj, ifp.fw_version_min,
	    ifp.fw_version_unit, ifp.fw_version_dev,
	    ISSET(sc->sc_flags, MPII_F_RAID) ? " IR" : "",
	    ifp.msg_version_maj, ifp.msg_version_min);

	return (0);
}

int
mpii_target_map(struct mpii_softc *sc)
{
	struct mpii_cfg_hdr			hdr;
	struct mpii_cfg_ioc_pg8			ipg;
	int					flags, pad = 0;

	hdr.page_version = 0;
	hdr.page_length = sizeof(ipg) / 4;
	hdr.page_number = 8;
	hdr.page_type = MPII_CONFIG_REQ_PAGE_TYPE_IOC;
	memset(&ipg, 0, sizeof(ipg));
	if (mpii_req_cfg_page(sc, 0, MPII_PG_POLL, &hdr, 1, &ipg,
	    sizeof(ipg)) != 0) {
		printf("%s: unable to fetch ioc page 8\n",
		    DEVNAME(sc));
		return (EINVAL);
	}

	if (lemtoh16(&ipg.flags) & MPII_IOC_PG8_FLAGS_RESERVED_TARGETID_0)
		pad = 1;

	flags = lemtoh16(&ipg.ir_volume_mapping_flags) &
	    MPII_IOC_PG8_IRFLAGS_VOLUME_MAPPING_MODE_MASK;
	if (ISSET(sc->sc_flags, MPII_F_RAID)) {
		if (flags == MPII_IOC_PG8_IRFLAGS_LOW_VOLUME_MAPPING) {
			sc->sc_vd_id_low += pad;
			pad = sc->sc_max_volumes; /* for sc_pd_id_start */
		} else
			sc->sc_vd_id_low = sc->sc_max_devices -
			    sc->sc_max_volumes;
	}

	sc->sc_pd_id_start += pad;

	return (0);
}

int
mpii_req_cfg_header(struct mpii_softc *sc, u_int8_t type, u_int8_t number,
    u_int32_t address, int flags, void *p)
{
	struct mpii_msg_config_request		*cq;
	struct mpii_msg_config_reply		*cp;
	struct mpii_ccb				*ccb;
	struct mpii_cfg_hdr			*hdr = p;
	struct mpii_ecfg_hdr			*ehdr = p;
	int					etype = 0;
	int					rv = 0;

	DNPRINTF(MPII_D_MISC, "%s: mpii_req_cfg_header type: %#x number: %x "
	    "address: 0x%08x flags: 0x%b\n", DEVNAME(sc), type, number,
	    address, flags, MPII_PG_FMT);

	ccb = scsi_io_get(&sc->sc_iopool,
	    ISSET(flags, MPII_PG_POLL) ? SCSI_NOSLEEP : 0);
	if (ccb == NULL) {
		DNPRINTF(MPII_D_MISC, "%s: mpii_cfg_header ccb_get\n",
		    DEVNAME(sc));
		return (1);
	}

	if (ISSET(flags, MPII_PG_EXTENDED)) {
		etype = type;
		type = MPII_CONFIG_REQ_PAGE_TYPE_EXTENDED;
	}

	cq = ccb->ccb_cmd;

	cq->function = MPII_FUNCTION_CONFIG;

	cq->action = MPII_CONFIG_REQ_ACTION_PAGE_HEADER;

	cq->config_header.page_number = number;
	cq->config_header.page_type = type;
	cq->ext_page_type = etype;
	htolem32(&cq->page_address, address);
	htolem32(&cq->page_buffer.sg_hdr, MPII_SGE_FL_TYPE_SIMPLE |
	    MPII_SGE_FL_LAST | MPII_SGE_FL_EOB | MPII_SGE_FL_EOL);

	ccb->ccb_done = mpii_empty_done;
	if (ISSET(flags, MPII_PG_POLL)) {
		if (mpii_poll(sc, ccb) != 0) {
			DNPRINTF(MPII_D_MISC, "%s: mpii_cfg_header poll\n",
			    DEVNAME(sc));
			return (1);
		}
	} else
		mpii_wait(sc, ccb);

	if (ccb->ccb_rcb == NULL) {
		scsi_io_put(&sc->sc_iopool, ccb);
		return (1);
	}
	cp = ccb->ccb_rcb->rcb_reply;

	DNPRINTF(MPII_D_MISC, "%s:  action: 0x%02x sgl_flags: 0x%02x "
	    "msg_length: %d function: 0x%02x\n", DEVNAME(sc), cp->action,
	    cp->sgl_flags, cp->msg_length, cp->function);
	DNPRINTF(MPII_D_MISC, "%s:  ext_page_length: %d ext_page_type: 0x%02x "
	    "msg_flags: 0x%02x\n", DEVNAME(sc),
	    lemtoh16(&cp->ext_page_length), cp->ext_page_type,
	    cp->msg_flags);
	DNPRINTF(MPII_D_MISC, "%s:  vp_id: 0x%02x vf_id: 0x%02x\n", DEVNAME(sc),
	    cp->vp_id, cp->vf_id);
	DNPRINTF(MPII_D_MISC, "%s:  ioc_status: 0x%04x\n", DEVNAME(sc),
	    lemtoh16(&cp->ioc_status));
	DNPRINTF(MPII_D_MISC, "%s:  ioc_loginfo: 0x%08x\n", DEVNAME(sc),
	    lemtoh32(&cp->ioc_loginfo));
	DNPRINTF(MPII_D_MISC, "%s:  page_version: 0x%02x page_length: %d "
	    "page_number: 0x%02x page_type: 0x%02x\n", DEVNAME(sc),
	    cp->config_header.page_version,
	    cp->config_header.page_length,
	    cp->config_header.page_number,
	    cp->config_header.page_type);

	if (lemtoh16(&cp->ioc_status) != MPII_IOCSTATUS_SUCCESS)
		rv = 1;
	else if (ISSET(flags, MPII_PG_EXTENDED)) {
		memset(ehdr, 0, sizeof(*ehdr));
		ehdr->page_version = cp->config_header.page_version;
		ehdr->page_number = cp->config_header.page_number;
		ehdr->page_type = cp->config_header.page_type;
		ehdr->ext_page_length = cp->ext_page_length;
		ehdr->ext_page_type = cp->ext_page_type;
	} else
		*hdr = cp->config_header;

	mpii_push_reply(sc, ccb->ccb_rcb);
	scsi_io_put(&sc->sc_iopool, ccb);

	return (rv);
}

int
mpii_req_cfg_page(struct mpii_softc *sc, u_int32_t address, int flags,
    void *p, int read, void *page, size_t len)
{
	struct mpii_msg_config_request		*cq;
	struct mpii_msg_config_reply		*cp;
	struct mpii_ccb				*ccb;
	struct mpii_cfg_hdr			*hdr = p;
	struct mpii_ecfg_hdr			*ehdr = p;
	caddr_t					kva;
	int					page_length;
	int					rv = 0;

	DNPRINTF(MPII_D_MISC, "%s: mpii_cfg_page address: %d read: %d "
	    "type: %x\n", DEVNAME(sc), address, read, hdr->page_type);

	page_length = ISSET(flags, MPII_PG_EXTENDED) ?
	    lemtoh16(&ehdr->ext_page_length) : hdr->page_length;

	if (len > sc->sc_request_size - sizeof(*cq) || len < page_length * 4)
		return (1);

	ccb = scsi_io_get(&sc->sc_iopool,
	    ISSET(flags, MPII_PG_POLL) ? SCSI_NOSLEEP : 0);
	if (ccb == NULL) {
		DNPRINTF(MPII_D_MISC, "%s: mpii_cfg_page ccb_get\n",
		    DEVNAME(sc));
		return (1);
	}

	cq = ccb->ccb_cmd;

	cq->function = MPII_FUNCTION_CONFIG;

	cq->action = (read ? MPII_CONFIG_REQ_ACTION_PAGE_READ_CURRENT :
	    MPII_CONFIG_REQ_ACTION_PAGE_WRITE_CURRENT);

	if (ISSET(flags, MPII_PG_EXTENDED)) {
		cq->config_header.page_version = ehdr->page_version;
		cq->config_header.page_number = ehdr->page_number;
		cq->config_header.page_type = ehdr->page_type;
		cq->ext_page_len = ehdr->ext_page_length;
		cq->ext_page_type = ehdr->ext_page_type;
	} else
		cq->config_header = *hdr;
	cq->config_header.page_type &= MPII_CONFIG_REQ_PAGE_TYPE_MASK;
	htolem32(&cq->page_address, address);
	htolem32(&cq->page_buffer.sg_hdr, MPII_SGE_FL_TYPE_SIMPLE |
	    MPII_SGE_FL_LAST | MPII_SGE_FL_EOB | MPII_SGE_FL_EOL |
	    MPII_SGE_FL_SIZE_64 | (page_length * 4) |
	    (read ? MPII_SGE_FL_DIR_IN : MPII_SGE_FL_DIR_OUT));

	/* bounce the page via the request space to avoid more bus_dma games */
	mpii_dvatosge(&cq->page_buffer, ccb->ccb_cmd_dva +
	    sizeof(struct mpii_msg_config_request));

	kva = ccb->ccb_cmd;
	kva += sizeof(struct mpii_msg_config_request);

	if (!read)
		memcpy(kva, page, len);

	ccb->ccb_done = mpii_empty_done;
	if (ISSET(flags, MPII_PG_POLL)) {
		if (mpii_poll(sc, ccb) != 0) {
			DNPRINTF(MPII_D_MISC, "%s: mpii_cfg_header poll\n",
			    DEVNAME(sc));
			return (1);
		}
	} else
		mpii_wait(sc, ccb);

	if (ccb->ccb_rcb == NULL) {
		scsi_io_put(&sc->sc_iopool, ccb);
		return (1);
	}
	cp = ccb->ccb_rcb->rcb_reply;

	DNPRINTF(MPII_D_MISC, "%s:  action: 0x%02x msg_length: %d "
	    "function: 0x%02x\n", DEVNAME(sc), cp->action, cp->msg_length,
	    cp->function);
	DNPRINTF(MPII_D_MISC, "%s:  ext_page_length: %d ext_page_type: 0x%02x "
	    "msg_flags: 0x%02x\n", DEVNAME(sc),
	    lemtoh16(&cp->ext_page_length), cp->ext_page_type,
	    cp->msg_flags);
	DNPRINTF(MPII_D_MISC, "%s:  vp_id: 0x%02x vf_id: 0x%02x\n", DEVNAME(sc),
	    cp->vp_id, cp->vf_id);
	DNPRINTF(MPII_D_MISC, "%s:  ioc_status: 0x%04x\n", DEVNAME(sc),
	    lemtoh16(&cp->ioc_status));
	DNPRINTF(MPII_D_MISC, "%s:  ioc_loginfo: 0x%08x\n", DEVNAME(sc),
	    lemtoh32(&cp->ioc_loginfo));
	DNPRINTF(MPII_D_MISC, "%s:  page_version: 0x%02x page_length: %d "
	    "page_number: 0x%02x page_type: 0x%02x\n", DEVNAME(sc),
	    cp->config_header.page_version,
	    cp->config_header.page_length,
	    cp->config_header.page_number,
	    cp->config_header.page_type);

	if (lemtoh16(&cp->ioc_status) != MPII_IOCSTATUS_SUCCESS)
		rv = 1;
	else if (read)
		memcpy(page, kva, len);

	mpii_push_reply(sc, ccb->ccb_rcb);
	scsi_io_put(&sc->sc_iopool, ccb);

	return (rv);
}

struct mpii_rcb *
mpii_reply(struct mpii_softc *sc, struct mpii_reply_descr *rdp)
{
	struct mpii_rcb		*rcb = NULL;
	u_int32_t		rfid;

	DNPRINTF(MPII_D_INTR, "%s: mpii_reply\n", DEVNAME(sc));

	if ((rdp->reply_flags & MPII_REPLY_DESCR_TYPE_MASK) ==
	    MPII_REPLY_DESCR_ADDRESS_REPLY) {
		rfid = (lemtoh32(&rdp->frame_addr) -
		    (u_int32_t)MPII_DMA_DVA(sc->sc_replies)) /
		    sc->sc_reply_size;

		bus_dmamap_sync(sc->sc_dmat,
		    MPII_DMA_MAP(sc->sc_replies), sc->sc_reply_size * rfid,
		    sc->sc_reply_size, BUS_DMASYNC_POSTREAD);

		rcb = &sc->sc_rcbs[rfid];
	}

	memset(rdp, 0xff, sizeof(*rdp));

	bus_dmamap_sync(sc->sc_dmat, MPII_DMA_MAP(sc->sc_reply_postq),
	    8 * sc->sc_reply_post_host_index, 8,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	return (rcb);
}

struct mpii_dmamem *
mpii_dmamem_alloc(struct mpii_softc *sc, size_t size)
{
	struct mpii_dmamem	*mdm;
	int			nsegs;

	mdm = malloc(sizeof(*mdm), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (mdm == NULL)
		return (NULL);

	mdm->mdm_size = size;

	if (bus_dmamap_create(sc->sc_dmat, size, 1, size, 0,
	    BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW, &mdm->mdm_map) != 0)
		goto mdmfree;

	if (bus_dmamem_alloc(sc->sc_dmat, size, PAGE_SIZE, 0, &mdm->mdm_seg,
	    1, &nsegs, BUS_DMA_NOWAIT | BUS_DMA_ZERO) != 0)
		goto destroy;

	if (bus_dmamem_map(sc->sc_dmat, &mdm->mdm_seg, nsegs, size,
	    &mdm->mdm_kva, BUS_DMA_NOWAIT) != 0)
		goto free;

	if (bus_dmamap_load(sc->sc_dmat, mdm->mdm_map, mdm->mdm_kva, size,
	    NULL, BUS_DMA_NOWAIT) != 0)
		goto unmap;

	return (mdm);

unmap:
	bus_dmamem_unmap(sc->sc_dmat, mdm->mdm_kva, size);
free:
	bus_dmamem_free(sc->sc_dmat, &mdm->mdm_seg, 1);
destroy:
	bus_dmamap_destroy(sc->sc_dmat, mdm->mdm_map);
mdmfree:
	free(mdm, M_DEVBUF, sizeof *mdm);

	return (NULL);
}

void
mpii_dmamem_free(struct mpii_softc *sc, struct mpii_dmamem *mdm)
{
	DNPRINTF(MPII_D_MEM, "%s: mpii_dmamem_free %p\n", DEVNAME(sc), mdm);

	bus_dmamap_unload(sc->sc_dmat, mdm->mdm_map);
	bus_dmamem_unmap(sc->sc_dmat, mdm->mdm_kva, mdm->mdm_size);
	bus_dmamem_free(sc->sc_dmat, &mdm->mdm_seg, 1);
	bus_dmamap_destroy(sc->sc_dmat, mdm->mdm_map);
	free(mdm, M_DEVBUF, sizeof *mdm);
}

int
mpii_insert_dev(struct mpii_softc *sc, struct mpii_device *dev)
{
	int		slot;	/* initial hint */

	if (dev == NULL || dev->slot < 0)
		return (1);
	slot = dev->slot;

	while (slot < sc->sc_max_devices && sc->sc_devs[slot] != NULL)
		slot++;

	if (slot >= sc->sc_max_devices)
		return (1);

	dev->slot = slot;
	sc->sc_devs[slot] = dev;

	return (0);
}

int
mpii_remove_dev(struct mpii_softc *sc, struct mpii_device *dev)
{
	int			i;

	if (dev == NULL)
		return (1);

	for (i = 0; i < sc->sc_max_devices; i++) {
		if (sc->sc_devs[i] == NULL)
			continue;

		if (sc->sc_devs[i]->dev_handle == dev->dev_handle) {
			sc->sc_devs[i] = NULL;
			return (0);
		}
	}

	return (1);
}

struct mpii_device *
mpii_find_dev(struct mpii_softc *sc, u_int16_t handle)
{
	int			i;

	for (i = 0; i < sc->sc_max_devices; i++) {
		if (sc->sc_devs[i] == NULL)
			continue;

		if (sc->sc_devs[i]->dev_handle == handle)
			return (sc->sc_devs[i]);
	}

	return (NULL);
}

int
mpii_alloc_ccbs(struct mpii_softc *sc)
{
	struct mpii_ccb		*ccb;
	u_int8_t		*cmd;
	int			i;

	SIMPLEQ_INIT(&sc->sc_ccb_free);
	SIMPLEQ_INIT(&sc->sc_ccb_tmos);
	mtx_init(&sc->sc_ccb_free_mtx, IPL_BIO);
	mtx_init(&sc->sc_ccb_mtx, IPL_BIO);
	scsi_ioh_set(&sc->sc_ccb_tmo_handler, &sc->sc_iopool,
	    mpii_scsi_cmd_tmo_handler, sc);

	sc->sc_ccbs = mallocarray((sc->sc_max_cmds-1), sizeof(*ccb),
	    M_DEVBUF, M_NOWAIT | M_ZERO);
	if (sc->sc_ccbs == NULL) {
		printf("%s: unable to allocate ccbs\n", DEVNAME(sc));
		return (1);
	}

	sc->sc_requests = mpii_dmamem_alloc(sc,
	    sc->sc_request_size * sc->sc_max_cmds);
	if (sc->sc_requests == NULL) {
		printf("%s: unable to allocate ccb dmamem\n", DEVNAME(sc));
		goto free_ccbs;
	}
	cmd = MPII_DMA_KVA(sc->sc_requests);

	/*
	 * we have sc->sc_max_cmds system request message
	 * frames, but smid zero cannot be used. so we then
	 * have (sc->sc_max_cmds - 1) number of ccbs
	 */
	for (i = 1; i < sc->sc_max_cmds; i++) {
		ccb = &sc->sc_ccbs[i - 1];

		if (bus_dmamap_create(sc->sc_dmat, MAXPHYS, sc->sc_max_sgl,
		    MAXPHYS, 0,
		    BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW | BUS_DMA_64BIT,
		    &ccb->ccb_dmamap) != 0) {
			printf("%s: unable to create dma map\n", DEVNAME(sc));
			goto free_maps;
		}

		ccb->ccb_sc = sc;
		htolem16(&ccb->ccb_smid, i);
		ccb->ccb_offset = sc->sc_request_size * i;

		ccb->ccb_cmd = &cmd[ccb->ccb_offset];
		ccb->ccb_cmd_dva = (u_int32_t)MPII_DMA_DVA(sc->sc_requests) +
		    ccb->ccb_offset;

		DNPRINTF(MPII_D_CCB, "%s: mpii_alloc_ccbs(%d) ccb: %p map: %p "
		    "sc: %p smid: %#x offs: %#lx cmd: %p dva: %#lx\n",
		    DEVNAME(sc), i, ccb, ccb->ccb_dmamap, ccb->ccb_sc,
		    ccb->ccb_smid, ccb->ccb_offset, ccb->ccb_cmd,
		    ccb->ccb_cmd_dva);

		mpii_put_ccb(sc, ccb);
	}

	scsi_iopool_init(&sc->sc_iopool, sc, mpii_get_ccb, mpii_put_ccb);

	return (0);

free_maps:
	while ((ccb = mpii_get_ccb(sc)) != NULL)
		bus_dmamap_destroy(sc->sc_dmat, ccb->ccb_dmamap);

	mpii_dmamem_free(sc, sc->sc_requests);
free_ccbs:
	free(sc->sc_ccbs, M_DEVBUF, (sc->sc_max_cmds-1) * sizeof(*ccb));

	return (1);
}

void
mpii_put_ccb(void *cookie, void *io)
{
	struct mpii_softc	*sc = cookie;
	struct mpii_ccb		*ccb = io;

	DNPRINTF(MPII_D_CCB, "%s: mpii_put_ccb %p\n", DEVNAME(sc), ccb);

	ccb->ccb_state = MPII_CCB_FREE;
	ccb->ccb_cookie = NULL;
	ccb->ccb_done = NULL;
	ccb->ccb_rcb = NULL;
	memset(ccb->ccb_cmd, 0, sc->sc_request_size);

	KERNEL_UNLOCK();
	mtx_enter(&sc->sc_ccb_free_mtx);
	SIMPLEQ_INSERT_HEAD(&sc->sc_ccb_free, ccb, ccb_link);
	mtx_leave(&sc->sc_ccb_free_mtx);
	KERNEL_LOCK();
}

void *
mpii_get_ccb(void *cookie)
{
	struct mpii_softc	*sc = cookie;
	struct mpii_ccb		*ccb;

	KERNEL_UNLOCK();

	mtx_enter(&sc->sc_ccb_free_mtx);
	ccb = SIMPLEQ_FIRST(&sc->sc_ccb_free);
	if (ccb != NULL) {
		SIMPLEQ_REMOVE_HEAD(&sc->sc_ccb_free, ccb_link);
		ccb->ccb_state = MPII_CCB_READY;
	}
	mtx_leave(&sc->sc_ccb_free_mtx);

	KERNEL_LOCK();

	DNPRINTF(MPII_D_CCB, "%s: mpii_get_ccb %p\n", DEVNAME(sc), ccb);

	return (ccb);
}

int
mpii_alloc_replies(struct mpii_softc *sc)
{
	DNPRINTF(MPII_D_MISC, "%s: mpii_alloc_replies\n", DEVNAME(sc));

	sc->sc_rcbs = mallocarray(sc->sc_num_reply_frames,
	    sizeof(struct mpii_rcb), M_DEVBUF, M_NOWAIT);
	if (sc->sc_rcbs == NULL)
		return (1);

	sc->sc_replies = mpii_dmamem_alloc(sc, sc->sc_reply_size *
	    sc->sc_num_reply_frames);
	if (sc->sc_replies == NULL) {
		free(sc->sc_rcbs, M_DEVBUF,
		    sc->sc_num_reply_frames * sizeof(struct mpii_rcb));
		return (1);
	}

	return (0);
}

void
mpii_push_replies(struct mpii_softc *sc)
{
	struct mpii_rcb		*rcb;
	caddr_t			kva = MPII_DMA_KVA(sc->sc_replies);
	int			i;

	bus_dmamap_sync(sc->sc_dmat, MPII_DMA_MAP(sc->sc_replies),
	    0, sc->sc_reply_size * sc->sc_num_reply_frames,
	    BUS_DMASYNC_PREREAD);

	for (i = 0; i < sc->sc_num_reply_frames; i++) {
		rcb = &sc->sc_rcbs[i];

		rcb->rcb_reply = kva + sc->sc_reply_size * i;
		rcb->rcb_reply_dva = (u_int32_t)MPII_DMA_DVA(sc->sc_replies) +
		    sc->sc_reply_size * i;
		mpii_push_reply(sc, rcb);
	}
}

void
mpii_start(struct mpii_softc *sc, struct mpii_ccb *ccb)
{
	struct mpii_request_header	*rhp;
	struct mpii_request_descr	descr;
	u_long				 *rdp = (u_long *)&descr;

	DNPRINTF(MPII_D_RW, "%s: mpii_start %#lx\n", DEVNAME(sc),
	    ccb->ccb_cmd_dva);

	bus_dmamap_sync(sc->sc_dmat, MPII_DMA_MAP(sc->sc_requests),
	    ccb->ccb_offset, sc->sc_request_size,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	ccb->ccb_state = MPII_CCB_QUEUED;

	rhp = ccb->ccb_cmd;

	memset(&descr, 0, sizeof(descr));

	switch (rhp->function) {
	case MPII_FUNCTION_SCSI_IO_REQUEST:
		descr.request_flags = MPII_REQ_DESCR_SCSI_IO;
		descr.dev_handle = htole16(ccb->ccb_dev_handle);
		break;
	case MPII_FUNCTION_SCSI_TASK_MGMT:
		descr.request_flags = MPII_REQ_DESCR_HIGH_PRIORITY;
		break;
	default:
		descr.request_flags = MPII_REQ_DESCR_DEFAULT;
	}

	descr.vf_id = sc->sc_vf_id;
	descr.smid = ccb->ccb_smid;

	DNPRINTF(MPII_D_RW, "%s:   MPII_REQ_DESCR_POST_LOW (0x%08x) write "
	    "0x%08lx\n", DEVNAME(sc), MPII_REQ_DESCR_POST_LOW, *rdp);

	DNPRINTF(MPII_D_RW, "%s:   MPII_REQ_DESCR_POST_HIGH (0x%08x) write "
	    "0x%08lx\n", DEVNAME(sc), MPII_REQ_DESCR_POST_HIGH, *(rdp+1));

#if defined(__LP64__)
	bus_space_write_raw_8(sc->sc_iot, sc->sc_ioh,
	    MPII_REQ_DESCR_POST_LOW, *rdp);
#else
	mtx_enter(&sc->sc_req_mtx);
	bus_space_write_raw_4(sc->sc_iot, sc->sc_ioh,
	    MPII_REQ_DESCR_POST_LOW, rdp[0]);
	bus_space_barrier(sc->sc_iot, sc->sc_ioh,
	    MPII_REQ_DESCR_POST_LOW, 8, BUS_SPACE_BARRIER_WRITE);

	bus_space_write_raw_4(sc->sc_iot, sc->sc_ioh,
	    MPII_REQ_DESCR_POST_HIGH, rdp[1]);
	bus_space_barrier(sc->sc_iot, sc->sc_ioh,
	    MPII_REQ_DESCR_POST_LOW, 8, BUS_SPACE_BARRIER_WRITE);
	mtx_leave(&sc->sc_req_mtx);
#endif
}

int
mpii_poll(struct mpii_softc *sc, struct mpii_ccb *ccb)
{
	void				(*done)(struct mpii_ccb *);
	void				*cookie;
	int				rv = 1;

	DNPRINTF(MPII_D_INTR, "%s: mpii_poll\n", DEVNAME(sc));

	done = ccb->ccb_done;
	cookie = ccb->ccb_cookie;

	ccb->ccb_done = mpii_poll_done;
	ccb->ccb_cookie = &rv;

	mpii_start(sc, ccb);

	while (rv == 1) {
		/* avoid excessive polling */
		if (mpii_reply_waiting(sc))
			mpii_intr(sc);
		else
			delay(10);
	}

	ccb->ccb_cookie = cookie;
	done(ccb);

	return (0);
}

void
mpii_poll_done(struct mpii_ccb *ccb)
{
	int				*rv = ccb->ccb_cookie;

	*rv = 0;
}

int
mpii_alloc_queues(struct mpii_softc *sc)
{
	u_int32_t		*rfp;
	int			i;

	DNPRINTF(MPII_D_MISC, "%s: mpii_alloc_queues\n", DEVNAME(sc));

	sc->sc_reply_freeq = mpii_dmamem_alloc(sc,
	    sc->sc_reply_free_qdepth * sizeof(*rfp));
	if (sc->sc_reply_freeq == NULL)
		return (1);
	rfp = MPII_DMA_KVA(sc->sc_reply_freeq);
	for (i = 0; i < sc->sc_num_reply_frames; i++) {
		rfp[i] = (u_int32_t)MPII_DMA_DVA(sc->sc_replies) +
		    sc->sc_reply_size * i;
	}

	sc->sc_reply_postq = mpii_dmamem_alloc(sc,
	    sc->sc_reply_post_qdepth * sizeof(struct mpii_reply_descr));
	if (sc->sc_reply_postq == NULL)
		goto free_reply_freeq;
	sc->sc_reply_postq_kva = MPII_DMA_KVA(sc->sc_reply_postq);
	memset(sc->sc_reply_postq_kva, 0xff, sc->sc_reply_post_qdepth *
	    sizeof(struct mpii_reply_descr));

	return (0);

free_reply_freeq:
	mpii_dmamem_free(sc, sc->sc_reply_freeq);
	return (1);
}

void
mpii_init_queues(struct mpii_softc *sc)
{
	DNPRINTF(MPII_D_MISC, "%s:  mpii_init_queues\n", DEVNAME(sc));

	sc->sc_reply_free_host_index = sc->sc_reply_free_qdepth - 1;
	sc->sc_reply_post_host_index = 0;
	mpii_write_reply_free(sc, sc->sc_reply_free_host_index);
	mpii_write_reply_post(sc, sc->sc_reply_post_host_index);
}

void
mpii_wait(struct mpii_softc *sc, struct mpii_ccb *ccb)
{
	struct mutex		mtx;
	void			(*done)(struct mpii_ccb *);
	void			*cookie;

	mtx_init(&mtx, IPL_BIO);

	done = ccb->ccb_done;
	cookie = ccb->ccb_cookie;

	ccb->ccb_done = mpii_wait_done;
	ccb->ccb_cookie = &mtx;

	/* XXX this will wait forever for the ccb to complete */

	mpii_start(sc, ccb);

	mtx_enter(&mtx);
	while (ccb->ccb_cookie != NULL)
		msleep_nsec(ccb, &mtx, PRIBIO, "mpiiwait", INFSLP);
	mtx_leave(&mtx);

	ccb->ccb_cookie = cookie;
	done(ccb);
}

void
mpii_wait_done(struct mpii_ccb *ccb)
{
	struct mutex		*mtx = ccb->ccb_cookie;

	mtx_enter(mtx);
	ccb->ccb_cookie = NULL;
	mtx_leave(mtx);

	wakeup_one(ccb);
}

void
mpii_scsi_cmd(struct scsi_xfer *xs)
{
	struct scsi_link	*link = xs->sc_link;
	struct mpii_softc	*sc = link->bus->sb_adapter_softc;
	struct mpii_ccb		*ccb = xs->io;
	struct mpii_msg_scsi_io	*io;
	struct mpii_device	*dev;
	int			 ret;

	DNPRINTF(MPII_D_CMD, "%s: mpii_scsi_cmd\n", DEVNAME(sc));

	if (xs->cmdlen > MPII_CDB_LEN) {
		DNPRINTF(MPII_D_CMD, "%s: CDB too big %d\n",
		    DEVNAME(sc), xs->cmdlen);
		memset(&xs->sense, 0, sizeof(xs->sense));
		xs->sense.error_code = SSD_ERRCODE_VALID | 0x70;
		xs->sense.flags = SKEY_ILLEGAL_REQUEST;
		xs->sense.add_sense_code = 0x20;
		xs->error = XS_SENSE;
		scsi_done(xs);
		return;
	}

	if ((dev = sc->sc_devs[link->target]) == NULL) {
		/* device no longer exists */
		xs->error = XS_SELTIMEOUT;
		scsi_done(xs);
		return;
	}

	KERNEL_UNLOCK();

	DNPRINTF(MPII_D_CMD, "%s: ccb_smid: %d xs->flags: 0x%x\n",
	    DEVNAME(sc), ccb->ccb_smid, xs->flags);

	ccb->ccb_cookie = xs;
	ccb->ccb_done = mpii_scsi_cmd_done;
	ccb->ccb_dev_handle = dev->dev_handle;

	io = ccb->ccb_cmd;
	memset(io, 0, sizeof(*io));
	io->function = MPII_FUNCTION_SCSI_IO_REQUEST;
	io->sense_buffer_length = sizeof(xs->sense);
	io->sgl_offset0 = sizeof(struct mpii_msg_scsi_io) / 4;
	htolem16(&io->io_flags, xs->cmdlen);
	htolem16(&io->dev_handle, ccb->ccb_dev_handle);
	htobem16(&io->lun[0], link->lun);

	switch (xs->flags & (SCSI_DATA_IN | SCSI_DATA_OUT)) {
	case SCSI_DATA_IN:
		io->direction = MPII_SCSIIO_DIR_READ;
		break;
	case SCSI_DATA_OUT:
		io->direction = MPII_SCSIIO_DIR_WRITE;
		break;
	default:
		io->direction = MPII_SCSIIO_DIR_NONE;
		break;
	}

	io->tagging = MPII_SCSIIO_ATTR_SIMPLE_Q;

	memcpy(io->cdb, &xs->cmd, xs->cmdlen);

	htolem32(&io->data_length, xs->datalen);

	/* sense data is at the end of a request */
	htolem32(&io->sense_buffer_low_address, ccb->ccb_cmd_dva +
	    sc->sc_request_size - sizeof(struct scsi_sense_data));

	if (ISSET(sc->sc_flags, MPII_F_SAS3))
		ret = mpii_load_xs_sas3(ccb);
	else
		ret = mpii_load_xs(ccb);

	if (ret != 0) {
		xs->error = XS_DRIVER_STUFFUP;
		goto done;
	}

	timeout_set(&xs->stimeout, mpii_scsi_cmd_tmo, ccb);
	if (xs->flags & SCSI_POLL) {
		if (mpii_poll(sc, ccb) != 0) {
			xs->error = XS_DRIVER_STUFFUP;
			goto done;
		}
	} else {
		timeout_add_msec(&xs->stimeout, xs->timeout);
		mpii_start(sc, ccb);
	}

	KERNEL_LOCK();
	return;

done:
	KERNEL_LOCK();
	scsi_done(xs);
}

void
mpii_scsi_cmd_tmo(void *xccb)
{
	struct mpii_ccb		*ccb = xccb;
	struct mpii_softc	*sc = ccb->ccb_sc;

	printf("%s: mpii_scsi_cmd_tmo (0x%08x)\n", DEVNAME(sc),
	    mpii_read_db(sc));

	mtx_enter(&sc->sc_ccb_mtx);
	if (ccb->ccb_state == MPII_CCB_QUEUED) {
		ccb->ccb_state = MPII_CCB_TIMEOUT;
		SIMPLEQ_INSERT_HEAD(&sc->sc_ccb_tmos, ccb, ccb_link);
	}
	mtx_leave(&sc->sc_ccb_mtx);

	scsi_ioh_add(&sc->sc_ccb_tmo_handler);
}

void
mpii_scsi_cmd_tmo_handler(void *cookie, void *io)
{
	struct mpii_softc			*sc = cookie;
	struct mpii_ccb				*tccb = io;
	struct mpii_ccb				*ccb;
	struct mpii_msg_scsi_task_request	*stq;

	mtx_enter(&sc->sc_ccb_mtx);
	ccb = SIMPLEQ_FIRST(&sc->sc_ccb_tmos);
	if (ccb != NULL) {
		SIMPLEQ_REMOVE_HEAD(&sc->sc_ccb_tmos, ccb_link);
		ccb->ccb_state = MPII_CCB_QUEUED;
	}
	/* should remove any other ccbs for the same dev handle */
	mtx_leave(&sc->sc_ccb_mtx);

	if (ccb == NULL) {
		scsi_io_put(&sc->sc_iopool, tccb);
		return;
	}

	stq = tccb->ccb_cmd;
	stq->function = MPII_FUNCTION_SCSI_TASK_MGMT;
	stq->task_type = MPII_SCSI_TASK_TARGET_RESET;
	htolem16(&stq->dev_handle, ccb->ccb_dev_handle);

	tccb->ccb_done = mpii_scsi_cmd_tmo_done;
	mpii_start(sc, tccb);
}

void
mpii_scsi_cmd_tmo_done(struct mpii_ccb *tccb)
{
	mpii_scsi_cmd_tmo_handler(tccb->ccb_sc, tccb);
}

void
mpii_scsi_cmd_done(struct mpii_ccb *ccb)
{
	struct mpii_ccb		*tccb;
	struct mpii_msg_scsi_io_error	*sie;
	struct mpii_softc	*sc = ccb->ccb_sc;
	struct scsi_xfer	*xs = ccb->ccb_cookie;
	struct scsi_sense_data	*sense;
	bus_dmamap_t		dmap = ccb->ccb_dmamap;

	timeout_del(&xs->stimeout);
	mtx_enter(&sc->sc_ccb_mtx);
	if (ccb->ccb_state == MPII_CCB_TIMEOUT) {
		/* ENOSIMPLEQ_REMOVE :( */
		if (ccb == SIMPLEQ_FIRST(&sc->sc_ccb_tmos))
			SIMPLEQ_REMOVE_HEAD(&sc->sc_ccb_tmos, ccb_link);
		else {
			SIMPLEQ_FOREACH(tccb, &sc->sc_ccb_tmos, ccb_link) {
				if (SIMPLEQ_NEXT(tccb, ccb_link) == ccb) {
					SIMPLEQ_REMOVE_AFTER(&sc->sc_ccb_tmos,
					    tccb, ccb_link);
					break;
				}
			}
		}
	}

	ccb->ccb_state = MPII_CCB_READY;
	mtx_leave(&sc->sc_ccb_mtx);

	if (xs->datalen != 0) {
		bus_dmamap_sync(sc->sc_dmat, dmap, 0, dmap->dm_mapsize,
		    (xs->flags & SCSI_DATA_IN) ? BUS_DMASYNC_POSTREAD :
		    BUS_DMASYNC_POSTWRITE);

		bus_dmamap_unload(sc->sc_dmat, dmap);
	}

	xs->error = XS_NOERROR;
	xs->resid = 0;

	if (ccb->ccb_rcb == NULL) {
		/* no scsi error, we're ok so drop out early */
		xs->status = SCSI_OK;
		goto done;
	}

	sie = ccb->ccb_rcb->rcb_reply;

	DNPRINTF(MPII_D_CMD, "%s: mpii_scsi_cmd_done xs cmd: 0x%02x len: %d "
	    "flags 0x%x\n", DEVNAME(sc), xs->cmd.opcode, xs->datalen,
	    xs->flags);
	DNPRINTF(MPII_D_CMD, "%s:  dev_handle: %d msg_length: %d "
	    "function: 0x%02x\n", DEVNAME(sc), lemtoh16(&sie->dev_handle),
	    sie->msg_length, sie->function);
	DNPRINTF(MPII_D_CMD, "%s:  vp_id: 0x%02x vf_id: 0x%02x\n", DEVNAME(sc),
	    sie->vp_id, sie->vf_id);
	DNPRINTF(MPII_D_CMD, "%s:  scsi_status: 0x%02x scsi_state: 0x%02x "
	    "ioc_status: 0x%04x\n", DEVNAME(sc), sie->scsi_status,
	    sie->scsi_state, lemtoh16(&sie->ioc_status));
	DNPRINTF(MPII_D_CMD, "%s:  ioc_loginfo: 0x%08x\n", DEVNAME(sc),
	    lemtoh32(&sie->ioc_loginfo));
	DNPRINTF(MPII_D_CMD, "%s:  transfer_count: %d\n", DEVNAME(sc),
	    lemtoh32(&sie->transfer_count));
	DNPRINTF(MPII_D_CMD, "%s:  sense_count: %d\n", DEVNAME(sc),
	    lemtoh32(&sie->sense_count));
	DNPRINTF(MPII_D_CMD, "%s:  response_info: 0x%08x\n", DEVNAME(sc),
	    lemtoh32(&sie->response_info));
	DNPRINTF(MPII_D_CMD, "%s:  task_tag: 0x%04x\n", DEVNAME(sc),
	    lemtoh16(&sie->task_tag));
	DNPRINTF(MPII_D_CMD, "%s:  bidirectional_transfer_count: 0x%08x\n",
	    DEVNAME(sc), lemtoh32(&sie->bidirectional_transfer_count));

	if (sie->scsi_state & MPII_SCSIIO_STATE_NO_SCSI_STATUS)
		xs->status = SCSI_TERMINATED;
	else
		xs->status = sie->scsi_status;
	xs->resid = 0;

	switch (lemtoh16(&sie->ioc_status) & MPII_IOCSTATUS_MASK) {
	case MPII_IOCSTATUS_SCSI_DATA_UNDERRUN:
		xs->resid = xs->datalen - lemtoh32(&sie->transfer_count);
		/* FALLTHROUGH */

	case MPII_IOCSTATUS_SUCCESS:
	case MPII_IOCSTATUS_SCSI_RECOVERED_ERROR:
		switch (xs->status) {
		case SCSI_OK:
			xs->error = XS_NOERROR;
			break;

		case SCSI_CHECK:
			xs->error = XS_SENSE;
			break;

		case SCSI_BUSY:
		case SCSI_QUEUE_FULL:
			xs->error = XS_BUSY;
			break;

		default:
			xs->error = XS_DRIVER_STUFFUP;
		}
		break;

	case MPII_IOCSTATUS_BUSY:
	case MPII_IOCSTATUS_INSUFFICIENT_RESOURCES:
		xs->error = XS_BUSY;
		break;

	case MPII_IOCSTATUS_SCSI_IOC_TERMINATED:
	case MPII_IOCSTATUS_SCSI_TASK_TERMINATED:
		xs->error = XS_RESET;
		break;

	case MPII_IOCSTATUS_SCSI_INVALID_DEVHANDLE:
	case MPII_IOCSTATUS_SCSI_DEVICE_NOT_THERE:
		xs->error = XS_SELTIMEOUT;
		break;

	default:
		xs->error = XS_DRIVER_STUFFUP;
		break;
	}

	sense = (struct scsi_sense_data *)((caddr_t)ccb->ccb_cmd +
	    sc->sc_request_size - sizeof(*sense));
	if (sie->scsi_state & MPII_SCSIIO_STATE_AUTOSENSE_VALID)
		memcpy(&xs->sense, sense, sizeof(xs->sense));

	DNPRINTF(MPII_D_CMD, "%s:  xs err: %d status: %#x\n", DEVNAME(sc),
	    xs->error, xs->status);

	mpii_push_reply(sc, ccb->ccb_rcb);
done:
	KERNEL_LOCK();
	scsi_done(xs);
	KERNEL_UNLOCK();
}

int
mpii_scsi_ioctl(struct scsi_link *link, u_long cmd, caddr_t addr, int flag)
{
	struct mpii_softc	*sc = link->bus->sb_adapter_softc;
	struct mpii_device	*dev = sc->sc_devs[link->target];

	DNPRINTF(MPII_D_IOCTL, "%s: mpii_scsi_ioctl\n", DEVNAME(sc));

	switch (cmd) {
	case DIOCGCACHE:
	case DIOCSCACHE:
		if (dev != NULL && ISSET(dev->flags, MPII_DF_VOLUME)) {
			return (mpii_ioctl_cache(link, cmd,
			    (struct dk_cache *)addr));
		}
		break;

	default:
		if (sc->sc_ioctl)
			return (sc->sc_ioctl(&sc->sc_dev, cmd, addr));

		break;
	}

	return (ENOTTY);
}

int
mpii_ioctl_cache(struct scsi_link *link, u_long cmd, struct dk_cache *dc)
{
	struct mpii_softc *sc = link->bus->sb_adapter_softc;
	struct mpii_device *dev = sc->sc_devs[link->target];
	struct mpii_cfg_raid_vol_pg0 *vpg;
	struct mpii_msg_raid_action_request *req;
	struct mpii_msg_raid_action_reply *rep;
	struct mpii_cfg_hdr hdr;
	struct mpii_ccb	*ccb;
	u_int32_t addr = MPII_CFG_RAID_VOL_ADDR_HANDLE | dev->dev_handle;
	size_t pagelen;
	int rv = 0;
	int enabled;

	if (mpii_req_cfg_header(sc, MPII_CONFIG_REQ_PAGE_TYPE_RAID_VOL, 0,
	    addr, MPII_PG_POLL, &hdr) != 0)
		return (EINVAL);

	pagelen = hdr.page_length * 4;
	vpg = malloc(pagelen, M_TEMP, M_WAITOK | M_CANFAIL | M_ZERO);
	if (vpg == NULL)
		return (ENOMEM);

	if (mpii_req_cfg_page(sc, addr, MPII_PG_POLL, &hdr, 1,
	    vpg, pagelen) != 0) {
		rv = EINVAL;
		goto done;
	}

	enabled = ((lemtoh16(&vpg->volume_settings) &
	    MPII_CFG_RAID_VOL_0_SETTINGS_CACHE_MASK) ==
	    MPII_CFG_RAID_VOL_0_SETTINGS_CACHE_ENABLED) ? 1 : 0;

	if (cmd == DIOCGCACHE) {
		dc->wrcache = enabled;
		dc->rdcache = 0;
		goto done;
	} /* else DIOCSCACHE */

	if (dc->rdcache) {
		rv = EOPNOTSUPP;
		goto done;
	}

	if (((dc->wrcache) ? 1 : 0) == enabled)
		goto done;

	ccb = scsi_io_get(&sc->sc_iopool, SCSI_POLL);
	if (ccb == NULL) {
		rv = ENOMEM;
		goto done;
	}

	ccb->ccb_done = mpii_empty_done;

	req = ccb->ccb_cmd;
	memset(req, 0, sizeof(*req));
	req->function = MPII_FUNCTION_RAID_ACTION;
	req->action = MPII_RAID_ACTION_CHANGE_VOL_WRITE_CACHE;
	htolem16(&req->vol_dev_handle, dev->dev_handle);
	htolem32(&req->action_data, dc->wrcache ?
	    MPII_RAID_VOL_WRITE_CACHE_ENABLE :
	    MPII_RAID_VOL_WRITE_CACHE_DISABLE);

	if (mpii_poll(sc, ccb) != 0) {
		rv = EIO;
		goto done;
	}

	if (ccb->ccb_rcb != NULL) {
		rep = ccb->ccb_rcb->rcb_reply;
		if ((rep->ioc_status != MPII_IOCSTATUS_SUCCESS) ||
		    ((rep->action_data[0] &
		     MPII_RAID_VOL_WRITE_CACHE_MASK) !=
		    (dc->wrcache ? MPII_RAID_VOL_WRITE_CACHE_ENABLE :
		     MPII_RAID_VOL_WRITE_CACHE_DISABLE)))
			rv = EINVAL;
		mpii_push_reply(sc, ccb->ccb_rcb);
	}

	scsi_io_put(&sc->sc_iopool, ccb);

done:
	free(vpg, M_TEMP, pagelen);
	return (rv);
}

#if NBIO > 0
int
mpii_ioctl(struct device *dev, u_long cmd, caddr_t addr)
{
	struct mpii_softc	*sc = (struct mpii_softc *)dev;
	int			error = 0;

	DNPRINTF(MPII_D_IOCTL, "%s: mpii_ioctl ", DEVNAME(sc));

	switch (cmd) {
	case BIOCINQ:
		DNPRINTF(MPII_D_IOCTL, "inq\n");
		error = mpii_ioctl_inq(sc, (struct bioc_inq *)addr);
		break;
	case BIOCVOL:
		DNPRINTF(MPII_D_IOCTL, "vol\n");
		error = mpii_ioctl_vol(sc, (struct bioc_vol *)addr);
		break;
	case BIOCDISK:
		DNPRINTF(MPII_D_IOCTL, "disk\n");
		error = mpii_ioctl_disk(sc, (struct bioc_disk *)addr);
		break;
	default:
		DNPRINTF(MPII_D_IOCTL, " invalid ioctl\n");
		error = ENOTTY;
	}

	return (error);
}

int
mpii_ioctl_inq(struct mpii_softc *sc, struct bioc_inq *bi)
{
	int			i;

	DNPRINTF(MPII_D_IOCTL, "%s: mpii_ioctl_inq\n", DEVNAME(sc));

	strlcpy(bi->bi_dev, DEVNAME(sc), sizeof(bi->bi_dev));
	for (i = 0; i < sc->sc_max_devices; i++)
		if (sc->sc_devs[i] &&
		    ISSET(sc->sc_devs[i]->flags, MPII_DF_VOLUME))
			bi->bi_novol++;
	return (0);
}

int
mpii_ioctl_vol(struct mpii_softc *sc, struct bioc_vol *bv)
{
	struct mpii_cfg_raid_vol_pg0	*vpg;
	struct mpii_cfg_hdr		hdr;
	struct mpii_device		*dev;
	struct scsi_link		*lnk;
	struct device			*scdev;
	size_t				pagelen;
	u_int16_t			volh;
	int				rv, hcnt = 0;

	DNPRINTF(MPII_D_IOCTL, "%s: mpii_ioctl_vol %d\n",
	    DEVNAME(sc), bv->bv_volid);

	if ((dev = mpii_find_vol(sc, bv->bv_volid)) == NULL)
		return (ENODEV);
	volh = dev->dev_handle;

	if (mpii_req_cfg_header(sc, MPII_CONFIG_REQ_PAGE_TYPE_RAID_VOL, 0,
	    MPII_CFG_RAID_VOL_ADDR_HANDLE | volh, 0, &hdr) != 0) {
		printf("%s: unable to fetch header for raid volume page 0\n",
		    DEVNAME(sc));
		return (EINVAL);
	}

	pagelen = hdr.page_length * 4;
	vpg = malloc(pagelen, M_TEMP, M_WAITOK | M_CANFAIL | M_ZERO);
	if (vpg == NULL) {
		printf("%s: unable to allocate space for raid "
		    "volume page 0\n", DEVNAME(sc));
		return (ENOMEM);
	}

	if (mpii_req_cfg_page(sc, MPII_CFG_RAID_VOL_ADDR_HANDLE | volh, 0,
	    &hdr, 1, vpg, pagelen) != 0) {
		printf("%s: unable to fetch raid volume page 0\n",
		    DEVNAME(sc));
		free(vpg, M_TEMP, pagelen);
		return (EINVAL);
	}

	switch (vpg->volume_state) {
	case MPII_CFG_RAID_VOL_0_STATE_ONLINE:
	case MPII_CFG_RAID_VOL_0_STATE_OPTIMAL:
		bv->bv_status = BIOC_SVONLINE;
		break;
	case MPII_CFG_RAID_VOL_0_STATE_DEGRADED:
		if (ISSET(lemtoh32(&vpg->volume_status),
		    MPII_CFG_RAID_VOL_0_STATUS_RESYNC)) {
			bv->bv_status = BIOC_SVREBUILD;
			bv->bv_percent = dev->percent;
		} else
			bv->bv_status = BIOC_SVDEGRADED;
		break;
	case MPII_CFG_RAID_VOL_0_STATE_FAILED:
		bv->bv_status = BIOC_SVOFFLINE;
		break;
	case MPII_CFG_RAID_VOL_0_STATE_INITIALIZING:
		bv->bv_status = BIOC_SVBUILDING;
		break;
	case MPII_CFG_RAID_VOL_0_STATE_MISSING:
	default:
		bv->bv_status = BIOC_SVINVALID;
		break;
	}

	switch (vpg->volume_type) {
	case MPII_CFG_RAID_VOL_0_TYPE_RAID0:
		bv->bv_level = 0;
		break;
	case MPII_CFG_RAID_VOL_0_TYPE_RAID1:
		bv->bv_level = 1;
		break;
	case MPII_CFG_RAID_VOL_0_TYPE_RAID1E:
		bv->bv_level = 0x1E;
		break;
	case MPII_CFG_RAID_VOL_0_TYPE_RAID10:
		bv->bv_level = 10;
		break;
	default:
		bv->bv_level = -1;
	}

	if ((rv = mpii_bio_hs(sc, NULL, 0, vpg->hot_spare_pool, &hcnt)) != 0) {
		free(vpg, M_TEMP, pagelen);
		return (rv);
	}

	bv->bv_nodisk = vpg->num_phys_disks + hcnt;

	bv->bv_size = letoh64(vpg->max_lba) * lemtoh16(&vpg->block_size);

	lnk = scsi_get_link(sc->sc_scsibus, dev->slot, 0);
	if (lnk != NULL) {
		scdev = lnk->device_softc;
		strlcpy(bv->bv_dev, scdev->dv_xname, sizeof(bv->bv_dev));
	}

	free(vpg, M_TEMP, pagelen);
	return (0);
}

int
mpii_ioctl_disk(struct mpii_softc *sc, struct bioc_disk *bd)
{
	struct mpii_cfg_raid_vol_pg0		*vpg;
	struct mpii_cfg_raid_vol_pg0_physdisk	*pd;
	struct mpii_cfg_hdr			hdr;
	struct mpii_device			*dev;
	size_t					pagelen;
	u_int16_t				volh;
	u_int8_t				dn;

	DNPRINTF(MPII_D_IOCTL, "%s: mpii_ioctl_disk %d/%d\n",
	    DEVNAME(sc), bd->bd_volid, bd->bd_diskid);

	if ((dev = mpii_find_vol(sc, bd->bd_volid)) == NULL)
		return (ENODEV);
	volh = dev->dev_handle;

	if (mpii_req_cfg_header(sc, MPII_CONFIG_REQ_PAGE_TYPE_RAID_VOL, 0,
	    MPII_CFG_RAID_VOL_ADDR_HANDLE | volh, 0, &hdr) != 0) {
		printf("%s: unable to fetch header for raid volume page 0\n",
		    DEVNAME(sc));
		return (EINVAL);
	}

	pagelen = hdr.page_length * 4;
	vpg = malloc(pagelen, M_TEMP, M_WAITOK | M_CANFAIL | M_ZERO);
	if (vpg == NULL) {
		printf("%s: unable to allocate space for raid "
		    "volume page 0\n", DEVNAME(sc));
		return (ENOMEM);
	}

	if (mpii_req_cfg_page(sc, MPII_CFG_RAID_VOL_ADDR_HANDLE | volh, 0,
	    &hdr, 1, vpg, pagelen) != 0) {
		printf("%s: unable to fetch raid volume page 0\n",
		    DEVNAME(sc));
		free(vpg, M_TEMP, pagelen);
		return (EINVAL);
	}

	if (bd->bd_diskid >= vpg->num_phys_disks) {
		int		nvdsk = vpg->num_phys_disks;
		int		hsmap = vpg->hot_spare_pool;

		free(vpg, M_TEMP, pagelen);
		return (mpii_bio_hs(sc, bd, nvdsk, hsmap, NULL));
	}

	pd = (struct mpii_cfg_raid_vol_pg0_physdisk *)(vpg + 1) +
	    bd->bd_diskid;
	dn = pd->phys_disk_num;

	free(vpg, M_TEMP, pagelen);
	return (mpii_bio_disk(sc, bd, dn));
}

int
mpii_bio_hs(struct mpii_softc *sc, struct bioc_disk *bd, int nvdsk,
     int hsmap, int *hscnt)
{
	struct mpii_cfg_raid_config_pg0	*cpg;
	struct mpii_raid_config_element	*el;
	struct mpii_ecfg_hdr		ehdr;
	size_t				pagelen;
	int				i, nhs = 0;

	if (bd)
		DNPRINTF(MPII_D_IOCTL, "%s: mpii_bio_hs %d\n", DEVNAME(sc),
		    bd->bd_diskid - nvdsk);
	else
		DNPRINTF(MPII_D_IOCTL, "%s: mpii_bio_hs\n", DEVNAME(sc));

	if (mpii_req_cfg_header(sc, MPII_CONFIG_REQ_PAGE_TYPE_RAID_CONFIG,
	    0, MPII_CFG_RAID_CONFIG_ACTIVE_CONFIG, MPII_PG_EXTENDED,
	    &ehdr) != 0) {
		printf("%s: unable to fetch header for raid config page 0\n",
		    DEVNAME(sc));
		return (EINVAL);
	}

	pagelen = lemtoh16(&ehdr.ext_page_length) * 4;
	cpg = malloc(pagelen, M_TEMP, M_WAITOK | M_CANFAIL | M_ZERO);
	if (cpg == NULL) {
		printf("%s: unable to allocate space for raid config page 0\n",
		    DEVNAME(sc));
		return (ENOMEM);
	}

	if (mpii_req_cfg_page(sc, MPII_CFG_RAID_CONFIG_ACTIVE_CONFIG,
	    MPII_PG_EXTENDED, &ehdr, 1, cpg, pagelen) != 0) {
		printf("%s: unable to fetch raid config page 0\n",
		    DEVNAME(sc));
		free(cpg, M_TEMP, pagelen);
		return (EINVAL);
	}

	el = (struct mpii_raid_config_element *)(cpg + 1);
	for (i = 0; i < cpg->num_elements; i++, el++) {
		if (ISSET(lemtoh16(&el->element_flags),
		    MPII_RAID_CONFIG_ELEMENT_FLAG_HSP_PHYS_DISK) &&
		    el->hot_spare_pool == hsmap) {
			/*
			 * diskid comparison is based on the idea that all
			 * disks are counted by the bio(4) in sequence, thus
			 * subtracting the number of disks in the volume
			 * from the diskid yields us a "relative" hotspare
			 * number, which is good enough for us.
			 */
			if (bd != NULL && bd->bd_diskid == nhs + nvdsk) {
				u_int8_t dn = el->phys_disk_num;

				free(cpg, M_TEMP, pagelen);
				return (mpii_bio_disk(sc, bd, dn));
			}
			nhs++;
		}
	}

	if (hscnt)
		*hscnt = nhs;

	free(cpg, M_TEMP, pagelen);
	return (0);
}

int
mpii_bio_disk(struct mpii_softc *sc, struct bioc_disk *bd, u_int8_t dn)
{
	struct mpii_cfg_raid_physdisk_pg0	*ppg;
	struct mpii_cfg_hdr			hdr;
	struct mpii_device			*dev;
	int					len;

	DNPRINTF(MPII_D_IOCTL, "%s: mpii_bio_disk %d\n", DEVNAME(sc),
	    bd->bd_diskid);

	ppg = malloc(sizeof(*ppg), M_TEMP, M_WAITOK | M_CANFAIL | M_ZERO);
	if (ppg == NULL) {
		printf("%s: unable to allocate space for raid physical disk "
		    "page 0\n", DEVNAME(sc));
		return (ENOMEM);
	}

	hdr.page_version = 0;
	hdr.page_length = sizeof(*ppg) / 4;
	hdr.page_number = 0;
	hdr.page_type = MPII_CONFIG_REQ_PAGE_TYPE_RAID_PD;

	if (mpii_req_cfg_page(sc, MPII_CFG_RAID_PHYS_DISK_ADDR_NUMBER | dn, 0,
	    &hdr, 1, ppg, sizeof(*ppg)) != 0) {
		printf("%s: unable to fetch raid drive page 0\n",
		    DEVNAME(sc));
		free(ppg, M_TEMP, sizeof(*ppg));
		return (EINVAL);
	}

	bd->bd_target = ppg->phys_disk_num;

	if ((dev = mpii_find_dev(sc, lemtoh16(&ppg->dev_handle))) == NULL) {
		bd->bd_status = BIOC_SDINVALID;
		free(ppg, M_TEMP, sizeof(*ppg));
		return (0);
	}

	switch (ppg->phys_disk_state) {
	case MPII_CFG_RAID_PHYDISK_0_STATE_ONLINE:
	case MPII_CFG_RAID_PHYDISK_0_STATE_OPTIMAL:
		bd->bd_status = BIOC_SDONLINE;
		break;
	case MPII_CFG_RAID_PHYDISK_0_STATE_OFFLINE:
		if (ppg->offline_reason ==
		    MPII_CFG_RAID_PHYDISK_0_OFFLINE_FAILED ||
		    ppg->offline_reason ==
		    MPII_CFG_RAID_PHYDISK_0_OFFLINE_FAILEDREQ)
			bd->bd_status = BIOC_SDFAILED;
		else
			bd->bd_status = BIOC_SDOFFLINE;
		break;
	case MPII_CFG_RAID_PHYDISK_0_STATE_DEGRADED:
		bd->bd_status = BIOC_SDFAILED;
		break;
	case MPII_CFG_RAID_PHYDISK_0_STATE_REBUILDING:
		bd->bd_status = BIOC_SDREBUILD;
		break;
	case MPII_CFG_RAID_PHYDISK_0_STATE_HOTSPARE:
		bd->bd_status = BIOC_SDHOTSPARE;
		break;
	case MPII_CFG_RAID_PHYDISK_0_STATE_NOTCONFIGURED:
		bd->bd_status = BIOC_SDUNUSED;
		break;
	case MPII_CFG_RAID_PHYDISK_0_STATE_NOTCOMPATIBLE:
	default:
		bd->bd_status = BIOC_SDINVALID;
		break;
	}

	bd->bd_size = letoh64(ppg->dev_max_lba) * lemtoh16(&ppg->block_size);

	scsi_strvis(bd->bd_vendor, ppg->vendor_id, sizeof(ppg->vendor_id));
	len = strlen(bd->bd_vendor);
	bd->bd_vendor[len] = ' ';
	scsi_strvis(&bd->bd_vendor[len + 1], ppg->product_id,
	    sizeof(ppg->product_id));
	scsi_strvis(bd->bd_serial, ppg->serial, sizeof(ppg->serial));

	free(ppg, M_TEMP, sizeof(*ppg));
	return (0);
}

struct mpii_device *
mpii_find_vol(struct mpii_softc *sc, int volid)
{
	struct mpii_device	*dev = NULL;

	if (sc->sc_vd_id_low + volid >= sc->sc_max_devices)
		return (NULL);
	dev = sc->sc_devs[sc->sc_vd_id_low + volid];
	if (dev && ISSET(dev->flags, MPII_DF_VOLUME))
		return (dev);
	return (NULL);
}

#ifndef SMALL_KERNEL
/*
 * Non-sleeping lightweight version of the mpii_ioctl_vol
 */
int
mpii_bio_volstate(struct mpii_softc *sc, struct bioc_vol *bv)
{
	struct mpii_cfg_raid_vol_pg0	*vpg;
	struct mpii_cfg_hdr		hdr;
	struct mpii_device		*dev = NULL;
	size_t				pagelen;
	u_int16_t			volh;

	if ((dev = mpii_find_vol(sc, bv->bv_volid)) == NULL)
		return (ENODEV);
	volh = dev->dev_handle;

	if (mpii_req_cfg_header(sc, MPII_CONFIG_REQ_PAGE_TYPE_RAID_VOL, 0,
	    MPII_CFG_RAID_VOL_ADDR_HANDLE | volh, MPII_PG_POLL, &hdr) != 0) {
		DNPRINTF(MPII_D_MISC, "%s: unable to fetch header for raid "
		    "volume page 0\n", DEVNAME(sc));
		return (EINVAL);
	}

	pagelen = hdr.page_length * 4;
	vpg = malloc(pagelen, M_TEMP, M_NOWAIT | M_ZERO);
	if (vpg == NULL) {
		DNPRINTF(MPII_D_MISC, "%s: unable to allocate space for raid "
		    "volume page 0\n", DEVNAME(sc));
		return (ENOMEM);
	}

	if (mpii_req_cfg_page(sc, MPII_CFG_RAID_VOL_ADDR_HANDLE | volh,
	    MPII_PG_POLL, &hdr, 1, vpg, pagelen) != 0) {
		DNPRINTF(MPII_D_MISC, "%s: unable to fetch raid volume "
		    "page 0\n", DEVNAME(sc));
		free(vpg, M_TEMP, pagelen);
		return (EINVAL);
	}

	switch (vpg->volume_state) {
	case MPII_CFG_RAID_VOL_0_STATE_ONLINE:
	case MPII_CFG_RAID_VOL_0_STATE_OPTIMAL:
		bv->bv_status = BIOC_SVONLINE;
		break;
	case MPII_CFG_RAID_VOL_0_STATE_DEGRADED:
		if (ISSET(lemtoh32(&vpg->volume_status),
		    MPII_CFG_RAID_VOL_0_STATUS_RESYNC))
			bv->bv_status = BIOC_SVREBUILD;
		else
			bv->bv_status = BIOC_SVDEGRADED;
		break;
	case MPII_CFG_RAID_VOL_0_STATE_FAILED:
		bv->bv_status = BIOC_SVOFFLINE;
		break;
	case MPII_CFG_RAID_VOL_0_STATE_INITIALIZING:
		bv->bv_status = BIOC_SVBUILDING;
		break;
	case MPII_CFG_RAID_VOL_0_STATE_MISSING:
	default:
		bv->bv_status = BIOC_SVINVALID;
		break;
	}

	free(vpg, M_TEMP, pagelen);
	return (0);
}

int
mpii_create_sensors(struct mpii_softc *sc)
{
	struct scsibus_softc	*ssc = sc->sc_scsibus;
	struct device		*dev;
	struct scsi_link	*link;
	int			i;

	sc->sc_sensors = mallocarray(sc->sc_vd_count, sizeof(struct ksensor),
	    M_DEVBUF, M_NOWAIT | M_ZERO);
	if (sc->sc_sensors == NULL)
		return (1);
	sc->sc_nsensors = sc->sc_vd_count;

	strlcpy(sc->sc_sensordev.xname, DEVNAME(sc),
	    sizeof(sc->sc_sensordev.xname));

	for (i = 0; i < sc->sc_vd_count; i++) {
		link = scsi_get_link(ssc, i + sc->sc_vd_id_low, 0);
		if (link == NULL)
			goto bad;

		dev = link->device_softc;

		sc->sc_sensors[i].type = SENSOR_DRIVE;
		sc->sc_sensors[i].status = SENSOR_S_UNKNOWN;

		strlcpy(sc->sc_sensors[i].desc, dev->dv_xname,
		    sizeof(sc->sc_sensors[i].desc));

		sensor_attach(&sc->sc_sensordev, &sc->sc_sensors[i]);
	}

	if (sensor_task_register(sc, mpii_refresh_sensors, 10) == NULL)
		goto bad;

	sensordev_install(&sc->sc_sensordev);

	return (0);

bad:
	free(sc->sc_sensors, M_DEVBUF, 0);

	return (1);
}

void
mpii_refresh_sensors(void *arg)
{
	struct mpii_softc	*sc = arg;
	struct bioc_vol		bv;
	int			i;

	for (i = 0; i < sc->sc_nsensors; i++) {
		memset(&bv, 0, sizeof(bv));
		bv.bv_volid = i;
		if (mpii_bio_volstate(sc, &bv))
			return;
		switch(bv.bv_status) {
		case BIOC_SVOFFLINE:
			sc->sc_sensors[i].value = SENSOR_DRIVE_FAIL;
			sc->sc_sensors[i].status = SENSOR_S_CRIT;
			break;
		case BIOC_SVDEGRADED:
			sc->sc_sensors[i].value = SENSOR_DRIVE_PFAIL;
			sc->sc_sensors[i].status = SENSOR_S_WARN;
			break;
		case BIOC_SVREBUILD:
			sc->sc_sensors[i].value = SENSOR_DRIVE_REBUILD;
			sc->sc_sensors[i].status = SENSOR_S_WARN;
			break;
		case BIOC_SVONLINE:
			sc->sc_sensors[i].value = SENSOR_DRIVE_ONLINE;
			sc->sc_sensors[i].status = SENSOR_S_OK;
			break;
		case BIOC_SVINVALID:
			/* FALLTHROUGH */
		default:
			sc->sc_sensors[i].value = 0; /* unknown */
			sc->sc_sensors[i].status = SENSOR_S_UNKNOWN;
		}
	}
}
#endif /* SMALL_KERNEL */
#endif /* NBIO > 0 */
