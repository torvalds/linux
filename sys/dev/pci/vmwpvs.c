/*	$OpenBSD: vmwpvs.c,v 1.31 2025/08/12 04:09:43 jmatthew Exp $ */

/*
 * Copyright (c) 2013 David Gwynne <dlg@openbsd.org>
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
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/task.h>

#include <machine/bus.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <scsi/scsi_all.h>
#include <scsi/scsi_message.h>
#include <scsi/scsiconf.h>

/* pushbuttons */
#define VMWPVS_OPENINGS		64 /* according to the linux driver */
#define VMWPVS_RING_PAGES	2
#define VMWPVS_MAXSGL		(MAXPHYS / PAGE_SIZE)
#define VMWPVS_SENSELEN		roundup(sizeof(struct scsi_sense_data), 16)

/* "chip" definitions */

#define VMWPVS_R_COMMAND	0x0000
#define VMWPVS_R_COMMAND_DATA	0x0004
#define VMWPVS_R_COMMAND_STATUS	0x0008
#define VMWPVS_R_LAST_STS_0	0x0100
#define VMWPVS_R_LAST_STS_1	0x0104
#define VMWPVS_R_LAST_STS_2	0x0108
#define VMWPVS_R_LAST_STS_3	0x010c
#define VMWPVS_R_INTR_STATUS	0x100c
#define VMWPVS_R_INTR_MASK	0x2010
#define VMWPVS_R_KICK_NON_RW_IO	0x3014
#define VMWPVS_R_DEBUG		0x3018
#define VMWPVS_R_KICK_RW_IO	0x4018

#define VMWPVS_INTR_CMPL_0	(1 << 0)
#define VMWPVS_INTR_CMPL_1	(1 << 1)
#define VMWPVS_INTR_CMPL_MASK	(VMWPVS_INTR_CMPL_0 | VMWPVS_INTR_CMPL_1)
#define VMWPVS_INTR_MSG_0	(1 << 2)
#define VMWPVS_INTR_MSG_1	(1 << 3)
#define VMWPVS_INTR_MSG_MASK	(VMWPVS_INTR_MSG_0 | VMWPVS_INTR_MSG_1)
#define VMWPVS_INTR_ALL_MASK	(VMWPVS_INTR_CMPL_MASK | VMWPVS_INTR_MSG_MASK)

#define VMWPVS_PAGE_SHIFT	12
#define VMWPVS_PAGE_SIZE	(1 << VMWPVS_PAGE_SHIFT)

#define VMWPVS_NPG_COMMAND	1
#define VMWPVS_NPG_INTR_STATUS	1
#define VMWPVS_NPG_MISC		2
#define VMWPVS_NPG_KICK_IO	2
#define VMWPVS_NPG_MSI_X	2

#define VMWPVS_PG_COMMAND	0
#define VMWPVS_PG_INTR_STATUS	(VMWPVS_PG_COMMAND + \
				    VMWPVS_NPG_COMMAND * VMWPVS_PAGE_SIZE)
#define VMWPVS_PG_MISC		(VMWPVS_PG_INTR_STATUS + \
				    VMWPVS_NPG_INTR_STATUS * VMWPVS_PAGE_SIZE)
#define VMWPVS_PG_KICK_IO	(VMWPVS_PG_MISC + \
				    VMWPVS_NPG_MISC * VMWPVS_PAGE_SIZE)
#define VMWPVS_PG_MSI_X		(VMWPVS_PG_KICK_IO + \
				    VMWPVS_NPG_KICK_IO * VMWPVS_PAGE_SIZE)
#define VMMPVS_PG_LEN		(VMWPVS_PG_MSI_X + \
				    VMWPVS_NPG_MSI_X * VMWPVS_PAGE_SIZE)

struct vmwpvw_ring_state {
	u_int32_t		req_prod;
	u_int32_t		req_cons;
	u_int32_t		req_entries; /* log 2 */

	u_int32_t		cmp_prod;
	u_int32_t		cmp_cons;
	u_int32_t		cmp_entries; /* log 2 */

	u_int32_t		__reserved[26];

	u_int32_t		msg_prod;
	u_int32_t		msg_cons;
	u_int32_t		msg_entries; /* log 2 */
} __packed;

struct vmwpvs_ring_req {
	u_int64_t		context;

	u_int64_t		data_addr;
	u_int64_t		data_len;

	u_int64_t		sense_addr;
	u_int32_t		sense_len;

	u_int32_t		flags;
#define VMWPVS_REQ_SGL			(1 << 0)
#define VMWPVS_REQ_OOBCDB		(1 << 1)
#define VMWPVS_REQ_DIR_NONE		(1 << 2)
#define VMWPVS_REQ_DIR_IN		(1 << 3)
#define VMWPVS_REQ_DIR_OUT		(1 << 4)

	u_int8_t		cdb[16];
	u_int8_t		cdblen;
	u_int8_t		lun[8];
	u_int8_t		tag;
	u_int8_t		bus;
	u_int8_t		target;
	u_int8_t		vcpu_hint;

	u_int8_t		__reserved[59];
} __packed;
#define VMWPVS_REQ_COUNT	((VMWPVS_RING_PAGES * VMWPVS_PAGE_SIZE) / \
				    sizeof(struct vmwpvs_ring_req))

struct vmwpvs_ring_cmp {
	u_int64_t		context;
	u_int64_t		data_len;
	u_int32_t		sense_len;
	u_int16_t		host_status;
	u_int16_t		scsi_status;
	u_int32_t		__reserved[2];
} __packed;
#define VMWPVS_CMP_COUNT	((VMWPVS_RING_PAGES * VMWPVS_PAGE_SIZE) / \
				    sizeof(struct vmwpvs_ring_cmp))

struct vmwpvs_sge {
	u_int64_t		addr;
	u_int32_t		len;
	u_int32_t		flags;
} __packed;

struct vmwpvs_ring_msg {
	u_int32_t		type;
	u_int32_t		__args[31];
} __packed;
#define VMWPVS_MSG_COUNT	((VMWPVS_RING_PAGES * VMWPVS_PAGE_SIZE) / \
				    sizeof(struct vmwpvs_ring_msg))

#define VMWPVS_MSG_T_ADDED	0
#define VMWPVS_MSG_T_REMOVED	1

struct vmwpvs_ring_msg_dev {
	u_int32_t		type;
	u_int32_t		bus;
	u_int32_t		target;
	u_int8_t		lun[8];

	u_int32_t		__pad[27];
} __packed;

struct vmwpvs_cfg_cmd {
	u_int64_t		cmp_addr;
	u_int32_t		pg_addr;
	u_int32_t		pg_addr_type;
	u_int32_t		pg_num;
	u_int32_t		__reserved;
} __packed;

#define VMWPVS_MAX_RING_PAGES		32
struct vmwpvs_setup_rings_cmd {
	u_int32_t		req_pages;
	u_int32_t		cmp_pages;
	u_int64_t		state_ppn;
	u_int64_t		req_page_ppn[VMWPVS_MAX_RING_PAGES];
	u_int64_t		cmp_page_ppn[VMWPVS_MAX_RING_PAGES];
} __packed;

#define VMWPVS_MAX_MSG_RING_PAGES	16
struct vmwpvs_setup_rings_msg {
	u_int32_t		msg_pages;
	u_int32_t		__reserved;
	u_int64_t		msg_page_ppn[VMWPVS_MAX_MSG_RING_PAGES];
} __packed;

#define VMWPVS_CMD_FIRST		0
#define VMWPVS_CMD_ADAPTER_RESET	1
#define VMWPVS_CMD_ISSUE_SCSI		2
#define VMWPVS_CMD_SETUP_RINGS		3
#define VMWPVS_CMD_RESET_BUS		4
#define VMWPVS_CMD_RESET_DEVICE		5
#define VMWPVS_CMD_ABORT_CMD		6
#define VMWPVS_CMD_CONFIG		7
#define VMWPVS_CMD_SETUP_MSG_RING	8
#define VMWPVS_CMD_DEVICE_UNPLUG	9
#define VMWPVS_CMD_LAST			10

#define VMWPVS_CFGPG_CONTROLLER		0x1958
#define VMWPVS_CFGPG_PHY		0x1959
#define VMWPVS_CFGPG_DEVICE		0x195a

#define VMWPVS_CFGPGADDR_CONTROLLER	0x2120
#define VMWPVS_CFGPGADDR_TARGET		0x2121
#define VMWPVS_CFGPGADDR_PHY		0x2122

struct vmwpvs_cfg_pg_header {
	u_int32_t		pg_num;
	u_int16_t		num_dwords;
	u_int16_t		host_status;
	u_int16_t		scsi_status;
	u_int16_t		__reserved[3];
} __packed;

#define VMWPVS_HOST_STATUS_SUCCESS	0x00
#define VMWPVS_HOST_STATUS_LINKED_CMD_COMPLETED 0x0a
#define VMWPVS_HOST_STATUS_LINKED_CMD_COMPLETED_WITH_FLAG 0x0b
#define VMWPVS_HOST_STATUS_UNDERRUN	0x0c
#define VMWPVS_HOST_STATUS_SELTIMEOUT	0x11
#define VMWPVS_HOST_STATUS_DATARUN	0x12
#define VMWPVS_HOST_STATUS_BUSFREE	0x13
#define VMWPVS_HOST_STATUS_INVPHASE	0x14
#define VMWPVS_HOST_STATUS_LUNMISMATCH	0x17
#define VMWPVS_HOST_STATUS_INVPARAM	0x1a
#define VMWPVS_HOST_STATUS_SENSEFAILED	0x1b
#define VMWPVS_HOST_STATUS_TAGREJECT	0x1c
#define VMWPVS_HOST_STATUS_BADMSG	0x1d
#define VMWPVS_HOST_STATUS_HAHARDWARE	0x20
#define VMWPVS_HOST_STATUS_NORESPONSE	0x21
#define VMWPVS_HOST_STATUS_SENT_RST	0x22
#define VMWPVS_HOST_STATUS_RECV_RST	0x23
#define VMWPVS_HOST_STATUS_DISCONNECT	0x24
#define VMWPVS_HOST_STATUS_BUS_RESET	0x25
#define VMWPVS_HOST_STATUS_ABORT_QUEUE	0x26
#define VMWPVS_HOST_STATUS_HA_SOFTWARE	0x27
#define VMWPVS_HOST_STATUS_HA_TIMEOUT	0x30
#define VMWPVS_HOST_STATUS_SCSI_PARITY	0x34

#define VMWPVS_SCSI_STATUS_OK		0x00
#define VMWPVS_SCSI_STATUS_CHECK	0x02

struct vmwpvs_cfg_pg_controller {
	struct vmwpvs_cfg_pg_header header;

	u_int64_t		wwnn;
	u_int16_t		manufacturer[64];
	u_int16_t		serial_number[64];
	u_int16_t		oprom_version[32];
	u_int16_t		hardware_version[32];
	u_int16_t		firmware_version[32];
	u_int32_t		num_phys;
	u_int8_t		use_consec_phy_wwns;
	u_int8_t		__reserved[3];
} __packed;

/* driver stuff */

struct vmwpvs_dmamem {
	bus_dmamap_t		dm_map;
	bus_dma_segment_t	dm_seg;
	size_t			dm_size;
	caddr_t			dm_kva;
};
#define VMWPVS_DMA_MAP(_dm)	(_dm)->dm_map
#define VMWPVS_DMA_DVA(_dm)	(_dm)->dm_map->dm_segs[0].ds_addr
#define VMWPVS_DMA_KVA(_dm)	(void *)(_dm)->dm_kva

struct vmwpvs_sgl {
	struct vmwpvs_sge	list[VMWPVS_MAXSGL];
} __packed;

struct vmwpvs_ccb {
	SIMPLEQ_ENTRY(vmwpvs_ccb)
				ccb_entry;

	bus_dmamap_t		ccb_dmamap;
	struct scsi_xfer	*ccb_xs;
	u_int64_t		ccb_ctx;

	struct vmwpvs_sgl	*ccb_sgl;
	bus_addr_t		ccb_sgl_offset;

	void			*ccb_sense;
	bus_addr_t		ccb_sense_offset;
};
SIMPLEQ_HEAD(vmwpvs_ccb_list, vmwpvs_ccb);

struct vmwpvs_softc {
	struct device		sc_dev;

	pci_chipset_tag_t	sc_pc;
	pcitag_t		sc_tag;

	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	bus_size_t		sc_ios;
	bus_dma_tag_t		sc_dmat;

	struct vmwpvs_dmamem	*sc_req_ring;
	struct vmwpvs_dmamem	*sc_cmp_ring;
	struct vmwpvs_dmamem	*sc_msg_ring;
	struct vmwpvs_dmamem	*sc_ring_state;
	struct mutex		sc_ring_mtx;

	struct vmwpvs_dmamem	*sc_sgls;
	struct vmwpvs_dmamem	*sc_sense;
	struct vmwpvs_ccb	*sc_ccbs;
	struct vmwpvs_ccb_list	sc_ccb_list;
	struct mutex		sc_ccb_mtx;

	void			*sc_ih;

	struct task		sc_msg_task;

	u_int			sc_bus_width;

	struct scsi_iopool	sc_iopool;
	struct scsibus_softc	*sc_scsibus;
};
#define DEVNAME(_s)		((_s)->sc_dev.dv_xname)

int	vmwpvs_match(struct device *, void *, void *);
void	vmwpvs_attach(struct device *, struct device *, void *);

int	vmwpvs_intx(void *);
int	vmwpvs_intr(void *);

#define vmwpvs_read(_s, _r) \
	bus_space_read_4((_s)->sc_iot, (_s)->sc_ioh, (_r))
#define vmwpvs_write(_s, _r, _v) \
	bus_space_write_4((_s)->sc_iot, (_s)->sc_ioh, (_r), (_v))
#define vmwpvs_barrier(_s, _r, _l, _d) \
	bus_space_barrier((_s)->sc_iot, (_s)->sc_ioh, (_r), (_l), (_d))

const struct cfattach vmwpvs_ca = {
	sizeof(struct vmwpvs_softc),
	vmwpvs_match,
	vmwpvs_attach,
	NULL
};

struct cfdriver vmwpvs_cd = {
	NULL,
	"vmwpvs",
	DV_DULL
};

void		vmwpvs_scsi_cmd(struct scsi_xfer *);

const struct scsi_adapter vmwpvs_switch = {
	vmwpvs_scsi_cmd, NULL, NULL, NULL, NULL
};

#define dwordsof(s)		(sizeof(s) / sizeof(u_int32_t))

void		vmwpvs_ccb_put(void *, void *);
void *		vmwpvs_ccb_get(void *);

struct vmwpvs_dmamem *
		vmwpvs_dmamem_alloc(struct vmwpvs_softc *, size_t);
struct vmwpvs_dmamem *
		vmwpvs_dmamem_zalloc(struct vmwpvs_softc *, size_t);
void		vmwpvs_dmamem_free(struct vmwpvs_softc *,
		    struct vmwpvs_dmamem *);

void		vmwpvs_cmd(struct vmwpvs_softc *, u_int32_t, void *, size_t);
int		vmwpvs_get_config(struct vmwpvs_softc *);
void		vmwpvs_setup_rings(struct vmwpvs_softc *);
void		vmwpvs_setup_msg_ring(struct vmwpvs_softc *);
void		vmwpvs_msg_task(void *);

struct vmwpvs_ccb *
		vmwpvs_scsi_cmd_poll(struct vmwpvs_softc *);
struct vmwpvs_ccb *
		vmwpvs_scsi_cmd_done(struct vmwpvs_softc *,
		    struct vmwpvs_ring_cmp *);

int
vmwpvs_match(struct device *parent, void *match, void *aux)
{
	struct pci_attach_args *pa = aux;

	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_VMWARE &&
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_VMWARE_PVSCSI)
		return (1);

	return (0);
}

void
vmwpvs_attach(struct device *parent, struct device *self, void *aux)
{
	struct vmwpvs_softc *sc = (struct vmwpvs_softc *)self;
	struct pci_attach_args *pa = aux;
	struct scsibus_attach_args saa;
	pcireg_t memtype;
	u_int i, r, use_msg;
	int (*isr)(void *) = vmwpvs_intx;
	u_int32_t intmask;
	pci_intr_handle_t ih;

	struct vmwpvs_ccb *ccb;
	struct vmwpvs_sgl *sgls;
	u_int8_t *sense;

	sc->sc_pc = pa->pa_pc;
	sc->sc_tag = pa->pa_tag;
	sc->sc_dmat = pa->pa_dmat;

	sc->sc_bus_width = 16;
	mtx_init(&sc->sc_ring_mtx, IPL_BIO);
	mtx_init(&sc->sc_ccb_mtx, IPL_BIO);
	task_set(&sc->sc_msg_task, vmwpvs_msg_task, sc);
	SIMPLEQ_INIT(&sc->sc_ccb_list);

	for (r = PCI_MAPREG_START; r < PCI_MAPREG_END; r += sizeof(memtype)) {
		memtype = pci_mapreg_type(sc->sc_pc, sc->sc_tag, r);
		if ((memtype & PCI_MAPREG_TYPE_MASK) == PCI_MAPREG_TYPE_MEM)
			break;
	}
	if (r >= PCI_MAPREG_END) {
		printf(": unable to locate registers\n");
		return;
	}

	if (pci_mapreg_map(pa, r, memtype, 0, &sc->sc_iot, &sc->sc_ioh,
	    NULL, &sc->sc_ios, VMMPVS_PG_LEN) != 0) {
		printf(": unable to map registers\n");
		return;
	}

	/* hook up the interrupt */
	vmwpvs_write(sc, VMWPVS_R_INTR_MASK, 0);

	if (pci_intr_map_msi(pa, &ih) == 0)
		isr = vmwpvs_intr;
	else if (pci_intr_map(pa, &ih) != 0) {
		printf(": unable to map interrupt\n");
		goto unmap;
	}
	printf(": %s\n", pci_intr_string(sc->sc_pc, ih));

	/* do we have msg support? */
	vmwpvs_write(sc, VMWPVS_R_COMMAND, VMWPVS_CMD_SETUP_MSG_RING);
	use_msg = (vmwpvs_read(sc, VMWPVS_R_COMMAND_STATUS) != 0xffffffff);

	if (vmwpvs_get_config(sc) != 0) {
		printf("%s: get configuration failed\n", DEVNAME(sc));
		goto unmap;
	}

	sc->sc_ring_state = vmwpvs_dmamem_zalloc(sc, VMWPVS_PAGE_SIZE);
	if (sc->sc_ring_state == NULL) {
		printf("%s: unable to allocate ring state\n", DEVNAME(sc));
		goto unmap;
	}

	sc->sc_req_ring = vmwpvs_dmamem_zalloc(sc,
	    VMWPVS_RING_PAGES * VMWPVS_PAGE_SIZE);
	if (sc->sc_req_ring == NULL) {
		printf("%s: unable to allocate req ring\n", DEVNAME(sc));
		goto free_ring_state;
	}

	sc->sc_cmp_ring = vmwpvs_dmamem_zalloc(sc,
	    VMWPVS_RING_PAGES * VMWPVS_PAGE_SIZE);
	if (sc->sc_cmp_ring == NULL) {
		printf("%s: unable to allocate cmp ring\n", DEVNAME(sc));
		goto free_req_ring;
	}

	if (use_msg) {
		sc->sc_msg_ring = vmwpvs_dmamem_zalloc(sc,
		    VMWPVS_RING_PAGES * VMWPVS_PAGE_SIZE);
		if (sc->sc_msg_ring == NULL) {
			printf("%s: unable to allocate msg ring\n",
			    DEVNAME(sc));
			goto free_cmp_ring;
		}
	}

	r = (VMWPVS_RING_PAGES * VMWPVS_PAGE_SIZE) /
	    sizeof(struct vmwpvs_ring_req);

	sc->sc_sgls = vmwpvs_dmamem_alloc(sc, r * sizeof(struct vmwpvs_sgl));
	if (sc->sc_sgls == NULL) {
		printf("%s: unable to allocate sgls\n", DEVNAME(sc));
		goto free_msg_ring;
	}

	sc->sc_sense = vmwpvs_dmamem_alloc(sc, r * VMWPVS_SENSELEN);
	if (sc->sc_sense == NULL) {
		printf("%s: unable to allocate sense data\n", DEVNAME(sc));
		goto free_sgl;
	}

	sc->sc_ccbs = mallocarray(r, sizeof(struct vmwpvs_ccb),
	    M_DEVBUF, M_WAITOK);
	/* can't fail */

	sgls = VMWPVS_DMA_KVA(sc->sc_sgls);
	sense = VMWPVS_DMA_KVA(sc->sc_sense);
	for (i = 0; i < r; i++) {
		ccb = &sc->sc_ccbs[i];

		if (bus_dmamap_create(sc->sc_dmat, MAXPHYS,
		    VMWPVS_MAXSGL, MAXPHYS, 0,
		    BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW,
		    &ccb->ccb_dmamap) != 0) {
			printf("%s: unable to create ccb map\n", DEVNAME(sc));
			goto free_ccbs;
		}

		ccb->ccb_ctx = 0xdeadbeef00000000ULL | (u_int64_t)i;

		ccb->ccb_sgl_offset = i * sizeof(*sgls);
		ccb->ccb_sgl = &sgls[i];

		ccb->ccb_sense_offset = i * VMWPVS_SENSELEN;
		ccb->ccb_sense = sense + ccb->ccb_sense_offset;

		vmwpvs_ccb_put(sc, ccb);
	}

	sc->sc_ih = pci_intr_establish(sc->sc_pc, ih, IPL_BIO | IPL_MPSAFE,
	    isr, sc, DEVNAME(sc));
	if (sc->sc_ih == NULL)
		goto free_msg_ring;

	bus_dmamap_sync(sc->sc_dmat, VMWPVS_DMA_MAP(sc->sc_cmp_ring), 0,
	    VMWPVS_RING_PAGES * VMWPVS_PAGE_SIZE, BUS_DMASYNC_PREREAD);
	bus_dmamap_sync(sc->sc_dmat, VMWPVS_DMA_MAP(sc->sc_req_ring), 0,
	    VMWPVS_RING_PAGES * VMWPVS_PAGE_SIZE, BUS_DMASYNC_PREWRITE);
	if (use_msg) {
		bus_dmamap_sync(sc->sc_dmat, VMWPVS_DMA_MAP(sc->sc_msg_ring), 0,
		    VMWPVS_RING_PAGES * VMWPVS_PAGE_SIZE, BUS_DMASYNC_PREREAD);
	}
	bus_dmamap_sync(sc->sc_dmat, VMWPVS_DMA_MAP(sc->sc_ring_state), 0,
	    VMWPVS_PAGE_SIZE, BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	intmask = VMWPVS_INTR_CMPL_MASK;

	vmwpvs_setup_rings(sc);
	if (use_msg) {
		vmwpvs_setup_msg_ring(sc);
		intmask |= VMWPVS_INTR_MSG_MASK;
	}

	vmwpvs_write(sc, VMWPVS_R_INTR_MASK, intmask);

	scsi_iopool_init(&sc->sc_iopool, sc, vmwpvs_ccb_get, vmwpvs_ccb_put);

	saa.saa_adapter = &vmwpvs_switch;
	saa.saa_adapter_softc = sc;
	saa.saa_adapter_target = SDEV_NO_ADAPTER_TARGET;
	saa.saa_adapter_buswidth = sc->sc_bus_width;
	saa.saa_luns = 8;
	saa.saa_openings = VMWPVS_OPENINGS;
	saa.saa_pool = &sc->sc_iopool;
	saa.saa_quirks = saa.saa_flags = 0;
	saa.saa_wwpn = saa.saa_wwnn = 0;

	sc->sc_scsibus = (struct scsibus_softc *)config_found(&sc->sc_dev, &saa,
	    scsiprint);

	return;
free_ccbs:
	while ((ccb = vmwpvs_ccb_get(sc)) != NULL)
		bus_dmamap_destroy(sc->sc_dmat, ccb->ccb_dmamap);
	free(sc->sc_ccbs, M_DEVBUF, r * sizeof(struct vmwpvs_ccb));
/* free_sense: */
	vmwpvs_dmamem_free(sc, sc->sc_sense);
free_sgl:
	vmwpvs_dmamem_free(sc, sc->sc_sgls);
free_msg_ring:
	if (use_msg)
		vmwpvs_dmamem_free(sc, sc->sc_msg_ring);
free_cmp_ring:
	vmwpvs_dmamem_free(sc, sc->sc_cmp_ring);
free_req_ring:
	vmwpvs_dmamem_free(sc, sc->sc_req_ring);
free_ring_state:
	vmwpvs_dmamem_free(sc, sc->sc_ring_state);
unmap:
	bus_space_unmap(sc->sc_iot, sc->sc_ioh, sc->sc_ios);
	sc->sc_ios = 0;
}

void
vmwpvs_setup_rings(struct vmwpvs_softc *sc)
{
	struct vmwpvs_setup_rings_cmd cmd;
	u_int64_t ppn;
	u_int i;

	memset(&cmd, 0, sizeof(cmd));
	cmd.req_pages = VMWPVS_RING_PAGES;
	cmd.cmp_pages = VMWPVS_RING_PAGES;
	cmd.state_ppn = VMWPVS_DMA_DVA(sc->sc_ring_state) >> VMWPVS_PAGE_SHIFT;

	ppn = VMWPVS_DMA_DVA(sc->sc_req_ring) >> VMWPVS_PAGE_SHIFT;
	for (i = 0; i < VMWPVS_RING_PAGES; i++)
		cmd.req_page_ppn[i] = ppn + i;

	ppn = VMWPVS_DMA_DVA(sc->sc_cmp_ring) >> VMWPVS_PAGE_SHIFT;
	for (i = 0; i < VMWPVS_RING_PAGES; i++)
		cmd.cmp_page_ppn[i] = ppn + i;

	vmwpvs_cmd(sc, VMWPVS_CMD_SETUP_RINGS, &cmd, sizeof(cmd));
}

void
vmwpvs_setup_msg_ring(struct vmwpvs_softc *sc)
{
	struct vmwpvs_setup_rings_msg cmd;
	u_int64_t ppn;
	u_int i;

	memset(&cmd, 0, sizeof(cmd));
	cmd.msg_pages = VMWPVS_RING_PAGES;

	ppn = VMWPVS_DMA_DVA(sc->sc_msg_ring) >> VMWPVS_PAGE_SHIFT;
	for (i = 0; i < VMWPVS_RING_PAGES; i++)
		cmd.msg_page_ppn[i] = ppn + i;

	vmwpvs_cmd(sc, VMWPVS_CMD_SETUP_MSG_RING, &cmd, sizeof(cmd));
}

int
vmwpvs_get_config(struct vmwpvs_softc *sc)
{
	struct vmwpvs_cfg_cmd cmd;
	struct vmwpvs_dmamem *dm;
	struct vmwpvs_cfg_pg_controller *pg;
	struct vmwpvs_cfg_pg_header *hdr;
	int rv = 0;

	dm = vmwpvs_dmamem_alloc(sc, VMWPVS_PAGE_SIZE);
	if (dm == NULL)
		return (ENOMEM);

	memset(&cmd, 0, sizeof(cmd));
	cmd.cmp_addr = VMWPVS_DMA_DVA(dm);
	cmd.pg_addr_type = VMWPVS_CFGPGADDR_CONTROLLER;
	cmd.pg_num = VMWPVS_CFGPG_CONTROLLER;

	pg = VMWPVS_DMA_KVA(dm);
	memset(pg, 0, VMWPVS_PAGE_SIZE);
	hdr = &pg->header;
	hdr->host_status = VMWPVS_HOST_STATUS_INVPARAM;
	hdr->scsi_status = VMWPVS_SCSI_STATUS_CHECK;

	bus_dmamap_sync(sc->sc_dmat, VMWPVS_DMA_MAP(dm), 0, VMWPVS_PAGE_SIZE,
	    BUS_DMASYNC_PREREAD);
	vmwpvs_cmd(sc, VMWPVS_CMD_CONFIG, &cmd, sizeof(cmd));
	bus_dmamap_sync(sc->sc_dmat, VMWPVS_DMA_MAP(dm), 0, VMWPVS_PAGE_SIZE,
	    BUS_DMASYNC_POSTREAD);

	if (hdr->host_status != VMWPVS_HOST_STATUS_SUCCESS ||
	    hdr->scsi_status != VMWPVS_SCSI_STATUS_OK) {
		rv = EIO;
		goto done;
	}

	sc->sc_bus_width = pg->num_phys;

done:
	vmwpvs_dmamem_free(sc, dm);

	return (rv);

}

void
vmwpvs_cmd(struct vmwpvs_softc *sc, u_int32_t cmd, void *buf, size_t len)
{
	u_int32_t *p = buf;
	u_int i;

	len /= sizeof(*p);

	vmwpvs_write(sc, VMWPVS_R_COMMAND, cmd);
	for (i = 0; i < len; i++)
		vmwpvs_write(sc, VMWPVS_R_COMMAND_DATA, p[i]);
}

int
vmwpvs_intx(void *xsc)
{
	struct vmwpvs_softc *sc = xsc;
	u_int32_t status;

	status = vmwpvs_read(sc, VMWPVS_R_INTR_STATUS);
	if ((status & VMWPVS_INTR_ALL_MASK) == 0)
		return (0);

	vmwpvs_write(sc, VMWPVS_R_INTR_STATUS, status);

	return (vmwpvs_intr(sc));
}

int
vmwpvs_intr(void *xsc)
{
	struct vmwpvs_softc *sc = xsc;
	volatile struct vmwpvw_ring_state *s =
	    VMWPVS_DMA_KVA(sc->sc_ring_state);
	struct vmwpvs_ring_cmp *ring = VMWPVS_DMA_KVA(sc->sc_cmp_ring);
	struct vmwpvs_ccb_list list = SIMPLEQ_HEAD_INITIALIZER(list);
	struct vmwpvs_ccb *ccb;
	u_int32_t cons, prod;
	int msg;

	mtx_enter(&sc->sc_ring_mtx);

	bus_dmamap_sync(sc->sc_dmat, VMWPVS_DMA_MAP(sc->sc_ring_state), 0,
	    VMWPVS_PAGE_SIZE, BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	cons = s->cmp_cons;
	prod = s->cmp_prod;
	s->cmp_cons = prod;

	msg = (sc->sc_msg_ring != NULL && s->msg_cons != s->msg_prod);

	bus_dmamap_sync(sc->sc_dmat, VMWPVS_DMA_MAP(sc->sc_ring_state), 0,
	    VMWPVS_PAGE_SIZE, BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	if (cons != prod) {
		bus_dmamap_sync(sc->sc_dmat, VMWPVS_DMA_MAP(sc->sc_cmp_ring),
		    0, VMWPVS_PAGE_SIZE, BUS_DMASYNC_POSTREAD);

		do {
			ccb = vmwpvs_scsi_cmd_done(sc,
			    &ring[cons++ % VMWPVS_CMP_COUNT]);
			SIMPLEQ_INSERT_TAIL(&list, ccb, ccb_entry);
		} while (cons != prod);

		bus_dmamap_sync(sc->sc_dmat, VMWPVS_DMA_MAP(sc->sc_cmp_ring),
		    0, VMWPVS_PAGE_SIZE, BUS_DMASYNC_PREREAD);
	}

	mtx_leave(&sc->sc_ring_mtx);

	while ((ccb = SIMPLEQ_FIRST(&list)) != NULL) {
		SIMPLEQ_REMOVE_HEAD(&list, ccb_entry);
		scsi_done(ccb->ccb_xs);
	}

	if (msg)
		task_add(systqmp, &sc->sc_msg_task);

	return (1);
}

void
vmwpvs_msg_task(void *xsc)
{
	struct vmwpvs_softc *sc = xsc;
	volatile struct vmwpvw_ring_state *s =
	    VMWPVS_DMA_KVA(sc->sc_ring_state);
	struct vmwpvs_ring_msg *ring = VMWPVS_DMA_KVA(sc->sc_msg_ring);
	struct vmwpvs_ring_msg *msg;
	struct vmwpvs_ring_msg_dev *dvmsg;
	u_int32_t cons, prod;

	mtx_enter(&sc->sc_ring_mtx);
	bus_dmamap_sync(sc->sc_dmat, VMWPVS_DMA_MAP(sc->sc_ring_state), 0,
	    VMWPVS_PAGE_SIZE, BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	cons = s->msg_cons;
	prod = s->msg_prod;
	bus_dmamap_sync(sc->sc_dmat, VMWPVS_DMA_MAP(sc->sc_ring_state), 0,
	    VMWPVS_PAGE_SIZE, BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	mtx_leave(&sc->sc_ring_mtx);

	/*
	 * we dont have to lock around the msg ring cos the system taskq has
	 * only one thread.
	 */

	bus_dmamap_sync(sc->sc_dmat, VMWPVS_DMA_MAP(sc->sc_msg_ring), 0,
	    VMWPVS_RING_PAGES * VMWPVS_PAGE_SIZE, BUS_DMASYNC_POSTREAD);
	while (cons != prod) {
		msg = &ring[cons++ % VMWPVS_MSG_COUNT];

		switch (letoh32(msg->type)) {
		case VMWPVS_MSG_T_ADDED:
			dvmsg = (struct vmwpvs_ring_msg_dev *)msg;
			if (letoh32(dvmsg->bus) != 0) {
				printf("%s: ignoring request to add device"
				    " on bus %d\n", DEVNAME(sc),
				    letoh32(msg->type));
				break;
			}

			KERNEL_LOCK();
			if (scsi_probe_lun(sc->sc_scsibus,
			    letoh32(dvmsg->target), dvmsg->lun[1]) != 0) {
				printf("%s: error probing target %d lun %d\n",
				    DEVNAME(sc), letoh32(dvmsg->target),
				    dvmsg->lun[1]);
			}
			KERNEL_UNLOCK();
			break;

		case VMWPVS_MSG_T_REMOVED:
			dvmsg = (struct vmwpvs_ring_msg_dev *)msg;
			if (letoh32(dvmsg->bus) != 0) {
				printf("%s: ignoring request to remove device"
				    " on bus %d\n", DEVNAME(sc),
				    letoh32(msg->type));
				break;
			}

			KERNEL_LOCK();
			if (scsi_detach_lun(sc->sc_scsibus,
			    letoh32(dvmsg->target), dvmsg->lun[1],
			    DETACH_FORCE) != 0) {
				printf("%s: error detaching target %d lun %d\n",
				    DEVNAME(sc), letoh32(dvmsg->target),
				    dvmsg->lun[1]);
			}
			KERNEL_UNLOCK();
			break;

		default:
			printf("%s: unknown msg type %u\n", DEVNAME(sc),
			    letoh32(msg->type));
			break;
		}
	}
	bus_dmamap_sync(sc->sc_dmat, VMWPVS_DMA_MAP(sc->sc_msg_ring), 0,
	    VMWPVS_RING_PAGES * VMWPVS_PAGE_SIZE, BUS_DMASYNC_PREREAD);

	mtx_enter(&sc->sc_ring_mtx);
	bus_dmamap_sync(sc->sc_dmat, VMWPVS_DMA_MAP(sc->sc_ring_state), 0,
	    VMWPVS_PAGE_SIZE, BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	s->msg_cons = prod;
	bus_dmamap_sync(sc->sc_dmat, VMWPVS_DMA_MAP(sc->sc_ring_state), 0,
	    VMWPVS_PAGE_SIZE, BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	mtx_leave(&sc->sc_ring_mtx);
}

void
vmwpvs_scsi_cmd(struct scsi_xfer *xs)
{
	struct scsi_link *link = xs->sc_link;
	struct vmwpvs_softc *sc = link->bus->sb_adapter_softc;
	struct vmwpvs_ccb *ccb = xs->io;
	bus_dmamap_t dmap = ccb->ccb_dmamap;
	volatile struct vmwpvw_ring_state *s =
	    VMWPVS_DMA_KVA(sc->sc_ring_state);
	struct vmwpvs_ring_req *ring = VMWPVS_DMA_KVA(sc->sc_req_ring), *r;
	u_int32_t prod;
	struct vmwpvs_ccb_list list;
	int error;
	u_int i;

	ccb->ccb_xs = xs;

	if (xs->datalen > 0) {
		error = bus_dmamap_load(sc->sc_dmat, dmap,
		    xs->data, xs->datalen, NULL, (xs->flags & SCSI_NOSLEEP) ?
		    BUS_DMA_NOWAIT : BUS_DMA_WAITOK);
		if (error) {
			xs->error = XS_DRIVER_STUFFUP;
			scsi_done(xs);
			return;
		}

		bus_dmamap_sync(sc->sc_dmat, dmap, 0, dmap->dm_mapsize,
		    (xs->flags & SCSI_DATA_IN) ? BUS_DMASYNC_PREREAD :
		    BUS_DMASYNC_PREWRITE);
	}

	mtx_enter(&sc->sc_ring_mtx);

	bus_dmamap_sync(sc->sc_dmat, VMWPVS_DMA_MAP(sc->sc_ring_state), 0,
	    VMWPVS_PAGE_SIZE, BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	prod = s->req_prod;
	r = &ring[prod % VMWPVS_REQ_COUNT];

	bus_dmamap_sync(sc->sc_dmat, VMWPVS_DMA_MAP(sc->sc_req_ring),
	    prod * sizeof(*r), sizeof(*r), BUS_DMASYNC_POSTWRITE);

	memset(r, 0, sizeof(*r));
	r->context = ccb->ccb_ctx;

	if (xs->datalen > 0) {
		r->data_len = xs->datalen;
		if (dmap->dm_nsegs == 1) {
			r->data_addr = dmap->dm_segs[0].ds_addr;
		} else {
			struct vmwpvs_sge *sgl = ccb->ccb_sgl->list, *sge;

			r->data_addr = VMWPVS_DMA_DVA(sc->sc_sgls) +
			    ccb->ccb_sgl_offset;
			r->flags = VMWPVS_REQ_SGL;

			for (i = 0; i < dmap->dm_nsegs; i++) {
				sge = &sgl[i];
				sge->addr = dmap->dm_segs[i].ds_addr;
				sge->len = dmap->dm_segs[i].ds_len;
				sge->flags = 0;
			}

			bus_dmamap_sync(sc->sc_dmat,
			    VMWPVS_DMA_MAP(sc->sc_sgls), ccb->ccb_sgl_offset,
			    sizeof(*sge) * dmap->dm_nsegs,
			    BUS_DMASYNC_PREWRITE);
		}
	}
	r->sense_addr = VMWPVS_DMA_DVA(sc->sc_sense) + ccb->ccb_sense_offset;
	r->sense_len = sizeof(xs->sense);

	bus_dmamap_sync(sc->sc_dmat, VMWPVS_DMA_MAP(sc->sc_req_ring), 0,
	    VMWPVS_RING_PAGES * VMWPVS_PAGE_SIZE, BUS_DMASYNC_POSTWRITE);

	switch (xs->flags & (SCSI_DATA_IN | SCSI_DATA_OUT)) {
	case SCSI_DATA_IN:
		r->flags |= VMWPVS_REQ_DIR_IN;
		break;
	case SCSI_DATA_OUT:
		r->flags |= VMWPVS_REQ_DIR_OUT;
		break;
	default:
		r->flags |= VMWPVS_REQ_DIR_NONE;
		break;
	}

	memcpy(r->cdb, &xs->cmd, xs->cmdlen);
	r->cdblen = xs->cmdlen;
	r->lun[1] = link->lun; /* ugly :( */
	r->tag = MSG_SIMPLE_Q_TAG;
	r->bus = 0;
	r->target = link->target;
	r->vcpu_hint = 0;

	bus_dmamap_sync(sc->sc_dmat, VMWPVS_DMA_MAP(sc->sc_req_ring), 0,
	    VMWPVS_RING_PAGES * VMWPVS_PAGE_SIZE, BUS_DMASYNC_PREWRITE);

	s->req_prod = prod + 1;

	bus_dmamap_sync(sc->sc_dmat, VMWPVS_DMA_MAP(sc->sc_ring_state), 0,
	    VMWPVS_PAGE_SIZE, BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	vmwpvs_write(sc, xs->bp == NULL ?
	    VMWPVS_R_KICK_NON_RW_IO : VMWPVS_R_KICK_RW_IO, 0);

	if (!ISSET(xs->flags, SCSI_POLL)) {
		mtx_leave(&sc->sc_ring_mtx);
		return;
	}

	SIMPLEQ_INIT(&list);
	do {
		ccb = vmwpvs_scsi_cmd_poll(sc);
		SIMPLEQ_INSERT_TAIL(&list, ccb, ccb_entry);
	} while (xs->io != ccb);

	mtx_leave(&sc->sc_ring_mtx);

	while ((ccb = SIMPLEQ_FIRST(&list)) != NULL) {
		SIMPLEQ_REMOVE_HEAD(&list, ccb_entry);
		scsi_done(ccb->ccb_xs);
	}
}

struct vmwpvs_ccb *
vmwpvs_scsi_cmd_poll(struct vmwpvs_softc *sc)
{
	volatile struct vmwpvw_ring_state *s =
	    VMWPVS_DMA_KVA(sc->sc_ring_state);
	struct vmwpvs_ring_cmp *ring = VMWPVS_DMA_KVA(sc->sc_cmp_ring);
	struct vmwpvs_ccb *ccb;
	u_int32_t prod, cons;

	for (;;) {
		bus_dmamap_sync(sc->sc_dmat,
		    VMWPVS_DMA_MAP(sc->sc_ring_state), 0, VMWPVS_PAGE_SIZE,
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

		cons = s->cmp_cons;
		prod = s->cmp_prod;

		if (cons != prod)
			s->cmp_cons = cons + 1;

		bus_dmamap_sync(sc->sc_dmat,
		    VMWPVS_DMA_MAP(sc->sc_ring_state), 0, VMWPVS_PAGE_SIZE,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

		if (cons != prod)
			break;
		else
			delay(1000);
	}

	bus_dmamap_sync(sc->sc_dmat, VMWPVS_DMA_MAP(sc->sc_cmp_ring),
	    0, VMWPVS_PAGE_SIZE * VMWPVS_RING_PAGES,
	    BUS_DMASYNC_POSTREAD);
	ccb = vmwpvs_scsi_cmd_done(sc, &ring[cons % VMWPVS_CMP_COUNT]);
	bus_dmamap_sync(sc->sc_dmat, VMWPVS_DMA_MAP(sc->sc_cmp_ring),
	    0, VMWPVS_PAGE_SIZE * VMWPVS_RING_PAGES,
	    BUS_DMASYNC_PREREAD);

	return (ccb);
}

struct vmwpvs_ccb *
vmwpvs_scsi_cmd_done(struct vmwpvs_softc *sc, struct vmwpvs_ring_cmp *c)
{
	u_int64_t ctx = c->context;
	struct vmwpvs_ccb *ccb = &sc->sc_ccbs[ctx & 0xffffffff];
	bus_dmamap_t dmap = ccb->ccb_dmamap;
	struct scsi_xfer *xs = ccb->ccb_xs;

	bus_dmamap_sync(sc->sc_dmat, VMWPVS_DMA_MAP(sc->sc_sense),
	    ccb->ccb_sense_offset, sizeof(xs->sense), BUS_DMASYNC_POSTREAD);

	if (xs->datalen > 0) {
		if (dmap->dm_nsegs > 1) {
			bus_dmamap_sync(sc->sc_dmat,
			    VMWPVS_DMA_MAP(sc->sc_sgls), ccb->ccb_sgl_offset,
			    sizeof(struct vmwpvs_sge) * dmap->dm_nsegs,
			    BUS_DMASYNC_POSTWRITE);
		}

		bus_dmamap_sync(sc->sc_dmat, dmap, 0, dmap->dm_mapsize,
		    (xs->flags & SCSI_DATA_IN) ? BUS_DMASYNC_POSTREAD :
		    BUS_DMASYNC_POSTWRITE);

		bus_dmamap_unload(sc->sc_dmat, dmap);
	}

	xs->status = c->scsi_status;
	switch (c->host_status) {
	case VMWPVS_HOST_STATUS_SUCCESS:
	case VMWPVS_HOST_STATUS_LINKED_CMD_COMPLETED:
	case VMWPVS_HOST_STATUS_LINKED_CMD_COMPLETED_WITH_FLAG:
		if (c->scsi_status == VMWPVS_SCSI_STATUS_CHECK) {
			memcpy(&xs->sense, ccb->ccb_sense, sizeof(xs->sense));
			xs->error = XS_SENSE;
		} else
			xs->error = XS_NOERROR;
		xs->resid = 0;
		break;

	case VMWPVS_HOST_STATUS_UNDERRUN:
	case VMWPVS_HOST_STATUS_DATARUN:
		xs->resid = xs->datalen - c->data_len;
		xs->error = XS_NOERROR;
		break;

	case VMWPVS_HOST_STATUS_SELTIMEOUT:
		xs->error = XS_SELTIMEOUT;
		break;

	default:
		printf("%s: %s:%d h:0x%x s:0x%x\n", DEVNAME(sc),
		    __FUNCTION__, __LINE__, c->host_status, c->scsi_status);
		xs->error = XS_DRIVER_STUFFUP;
		break;
	}

	return (ccb);
}

void *
vmwpvs_ccb_get(void *xsc)
{
	struct vmwpvs_softc *sc = xsc;
	struct vmwpvs_ccb *ccb;

	mtx_enter(&sc->sc_ccb_mtx);
	ccb = SIMPLEQ_FIRST(&sc->sc_ccb_list);
	if (ccb != NULL)
		SIMPLEQ_REMOVE_HEAD(&sc->sc_ccb_list, ccb_entry);
	mtx_leave(&sc->sc_ccb_mtx);

	return (ccb);
}

void
vmwpvs_ccb_put(void *xsc, void *io)
{
	struct vmwpvs_softc *sc = xsc;
	struct vmwpvs_ccb *ccb = io;

	mtx_enter(&sc->sc_ccb_mtx);
	SIMPLEQ_INSERT_HEAD(&sc->sc_ccb_list, ccb, ccb_entry);
	mtx_leave(&sc->sc_ccb_mtx);
}

struct vmwpvs_dmamem *
vmwpvs_dmamem_alloc(struct vmwpvs_softc *sc, size_t size)
{
	struct vmwpvs_dmamem *dm;
	int nsegs;

	dm = malloc(sizeof(*dm), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (dm == NULL)
		return (NULL);

	dm->dm_size = size;

	if (bus_dmamap_create(sc->sc_dmat, size, 1, size, 0,
	    BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW, &dm->dm_map) != 0)
		goto dmfree;

	if (bus_dmamem_alloc(sc->sc_dmat, size, PAGE_SIZE, 0, &dm->dm_seg,
	    1, &nsegs, BUS_DMA_NOWAIT | BUS_DMA_ZERO) != 0)
		goto destroy;

	if (bus_dmamem_map(sc->sc_dmat, &dm->dm_seg, nsegs, size,
	    &dm->dm_kva, BUS_DMA_NOWAIT) != 0)
		goto free;

	if (bus_dmamap_load(sc->sc_dmat, dm->dm_map, dm->dm_kva, size,
	    NULL, BUS_DMA_NOWAIT) != 0)
		goto unmap;

	return (dm);

unmap:
	bus_dmamem_unmap(sc->sc_dmat, dm->dm_kva, size);
free:
	bus_dmamem_free(sc->sc_dmat, &dm->dm_seg, 1);
destroy:
	bus_dmamap_destroy(sc->sc_dmat, dm->dm_map);
dmfree:
	free(dm, M_DEVBUF, sizeof *dm);

	return (NULL);
}

struct vmwpvs_dmamem *
vmwpvs_dmamem_zalloc(struct vmwpvs_softc *sc, size_t size)
{
	struct vmwpvs_dmamem *dm;

	dm = vmwpvs_dmamem_alloc(sc, size);
	if (dm == NULL)
		return (NULL);

	memset(VMWPVS_DMA_KVA(dm), 0, size);

	return (dm);
}

void
vmwpvs_dmamem_free(struct vmwpvs_softc *sc, struct vmwpvs_dmamem *dm)
{
	bus_dmamap_unload(sc->sc_dmat, dm->dm_map);
	bus_dmamem_unmap(sc->sc_dmat, dm->dm_kva, dm->dm_size);
	bus_dmamem_free(sc->sc_dmat, &dm->dm_seg, 1);
	bus_dmamap_destroy(sc->sc_dmat, dm->dm_map);
	free(dm, M_DEVBUF, sizeof *dm);
}
