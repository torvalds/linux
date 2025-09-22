/*-
 * Copyright (c) 2009-2012,2016 Microsoft Corp.
 * Copyright (c) 2012 NetApp Inc.
 * Copyright (c) 2012 Citrix Inc.
 * Copyright (c) 2017 Mike Belopuhov <mike@esdenera.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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
 */

/*
 * The OpenBSD port was done under funding by Esdenera Networks GmbH.
 */

/* #define HVS_DEBUG_IO */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/atomic.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/task.h>

#include <machine/bus.h>

#include <uvm/uvm_extern.h>

#include <dev/pv/hypervreg.h>
#include <dev/pv/hypervvar.h>

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>

#define HVS_PROTO_VERSION_WIN6		 0x200
#define HVS_PROTO_VERSION_WIN7		 0x402
#define HVS_PROTO_VERSION_WIN8		 0x501
#define HVS_PROTO_VERSION_WIN8_1	 0x600
#define HVS_PROTO_VERSION_WIN10		 0x602

#define HVS_MSG_IODONE			 0x01
#define HVS_MSG_DEVGONE			 0x02
#define HVS_MSG_ENUMERATE		 0x0b

#define HVS_REQ_SCSIIO			 0x03
#define HVS_REQ_STARTINIT		 0x07
#define HVS_REQ_FINISHINIT		 0x08
#define HVS_REQ_QUERYPROTO		 0x09
#define HVS_REQ_QUERYPROPS		 0x0a

struct hvs_cmd_hdr {
	uint32_t		 hdr_op;
	uint32_t		 hdr_flags;
	uint32_t		 hdr_status;
#define cmd_op			 cmd_hdr.hdr_op
#define cmd_flags		 cmd_hdr.hdr_flags
#define cmd_status		 cmd_hdr.hdr_status
} __packed;

/* Negotiate version */
struct hvs_cmd_ver {
	struct hvs_cmd_hdr	 cmd_hdr;
	uint16_t		 cmd_ver;
	uint16_t		 cmd_rev;
} __packed;

/* Query channel properties */
struct hvs_chp {
	uint16_t		 chp_proto;
	uint8_t			 chp_path;
	uint8_t			 chp_target;
	uint16_t		 chp_maxchan;
	uint16_t		 chp_port;
	uint32_t		 chp_chflags;
	uint32_t		 chp_maxfer;
	uint64_t		 chp_chanid;
} __packed;

struct hvs_cmd_chp {
	struct hvs_cmd_hdr	 cmd_hdr;
	struct hvs_chp		 cmd_chp;
} __packed;

#define SENSE_DATA_LEN_WIN7		 18
#define SENSE_DATA_LEN			 20
#define MAX_SRB_DATA			 20

/* SCSI Request Block */
struct hvs_srb {
	uint16_t		 srb_reqlen;
	uint8_t			 srb_iostatus;
	uint8_t			 srb_scsistatus;

	uint8_t			 srb_initiator;
	uint8_t			 srb_bus;
	uint8_t			 srb_target;
	uint8_t			 srb_lun;

	uint8_t			 srb_cdblen;
	uint8_t			 srb_senselen;
	uint8_t			 srb_direction;
	uint8_t			 _reserved;

	uint32_t		 srb_datalen;
	uint8_t			 srb_data[MAX_SRB_DATA];
} __packed;

#define SRB_DATA_WRITE			 0
#define SRB_DATA_READ			 1
#define SRB_DATA_NONE			 2

#define SRB_STATUS_PENDING		 0x00
#define SRB_STATUS_SUCCESS		 0x01
#define SRB_STATUS_ABORTED		 0x02
#define SRB_STATUS_ERROR		 0x04
#define SRB_STATUS_INVALID_LUN		 0x20
#define SRB_STATUS_QUEUE_FROZEN		 0x40
#define SRB_STATUS_AUTOSENSE_VALID	 0x80

#define SRB_FLAGS_QUEUE_ACTION_ENABLE	 0x00000002
#define SRB_FLAGS_DISABLE_DISCONNECT	 0x00000004
#define SRB_FLAGS_DISABLE_SYNCH_TRANSFER 0x00000008
#define SRB_FLAGS_BYPASS_FROZEN_QUEUE	 0x00000010
#define SRB_FLAGS_DISABLE_AUTOSENSE	 0x00000020
#define SRB_FLAGS_DATA_IN		 0x00000040
#define SRB_FLAGS_DATA_OUT		 0x00000080
#define SRB_FLAGS_NO_DATA_TRANSFER	 0x00000000
#define SRB_FLAGS_NO_QUEUE_FREEZE	 0x00000100
#define SRB_FLAGS_ADAPTER_CACHE_ENABLE	 0x00000200
#define SRB_FLAGS_FREE_SENSE_BUFFER	 0x00000400

struct hvs_cmd_io {
	struct hvs_cmd_hdr	 cmd_hdr;
	struct hvs_srb		 cmd_srb;
	/* Win8 extensions */
	uint16_t		 _reserved;
	uint8_t			 cmd_qtag;
	uint8_t			 cmd_qaction;
	uint32_t		 cmd_srbflags;
	uint32_t		 cmd_timeout;
	uint32_t		 cmd_qsortkey;
} __packed;

#define HVS_CMD_SIZE			 64

union hvs_cmd {
	struct hvs_cmd_hdr	 cmd_hdr;
	struct hvs_cmd_ver	 ver;
	struct hvs_cmd_chp	 chp;
	struct hvs_cmd_io	 io;
	uint8_t			 pad[HVS_CMD_SIZE];
} __packed;

#define HVS_RING_SIZE			 (20 * PAGE_SIZE)
#define HVS_MAX_CCB			 128
#define HVS_MAX_SGE			 (MAXPHYS / PAGE_SIZE + 1)

struct hvs_softc;

struct hvs_ccb {
	struct scsi_xfer	*ccb_xfer;  /* associated transfer */
	union hvs_cmd		*ccb_cmd;   /* associated command */
	union hvs_cmd		 ccb_rsp;   /* response */
	bus_dmamap_t		 ccb_dmap;  /* transfer map */
	uint64_t		 ccb_rid;   /* request id */
	struct vmbus_gpa_range	*ccb_sgl;
	int			 ccb_nsge;
	void			(*ccb_done)(struct hvs_ccb *);
	void			*ccb_cookie;
	SIMPLEQ_ENTRY(hvs_ccb)	 ccb_link;
};
SIMPLEQ_HEAD(hvs_ccb_queue, hvs_ccb);

struct hvs_softc {
	struct device		 sc_dev;
	struct hv_softc		*sc_hvsc;
	struct hv_channel	*sc_chan;
	bus_dma_tag_t		 sc_dmat;

	int			 sc_proto;
	int			 sc_flags;
#define  HVSF_SCSI		  0x0001
#define  HVSF_W8PLUS		  0x0002
	struct hvs_chp		 sc_props;

	/* CCBs */
	int			 sc_nccb;
	struct hvs_ccb		*sc_ccbs;
	struct hvs_ccb_queue	 sc_ccb_fq; /* free queue */
	struct mutex		 sc_ccb_fqlck;

	int			 sc_bus;
	int			 sc_initiator;

	struct scsi_iopool	 sc_iopool;
	struct device		*sc_scsibus;
	struct task		 sc_probetask;
};

int	hvs_match(struct device *, void *, void *);
void	hvs_attach(struct device *, struct device *, void *);

void	hvs_scsi_cmd(struct scsi_xfer *);
void	hvs_scsi_cmd_done(struct hvs_ccb *);
int	hvs_start(struct hvs_softc *, struct hvs_ccb *);
int	hvs_poll(struct hvs_softc *, struct hvs_ccb *);
void	hvs_poll_done(struct hvs_ccb *);
void	hvs_intr(void *);
void	hvs_scsi_probe(void *arg);
void	hvs_scsi_done(struct scsi_xfer *, int);

int	hvs_connect(struct hvs_softc *);
void	hvs_empty_done(struct hvs_ccb *);

int	hvs_alloc_ccbs(struct hvs_softc *);
void	hvs_free_ccbs(struct hvs_softc *);
void	*hvs_get_ccb(void *);
void	hvs_put_ccb(void *, void *);

struct cfdriver hvs_cd = {
	NULL, "hvs", DV_DULL
};

const struct cfattach hvs_ca = {
	sizeof(struct hvs_softc), hvs_match, hvs_attach
};

const struct scsi_adapter hvs_switch = {
	hvs_scsi_cmd, NULL, NULL, NULL, NULL
};

int
hvs_match(struct device *parent, void *match, void *aux)
{
	struct hv_attach_args *aa = aux;

	if (strcmp("ide", aa->aa_ident) &&
	    strcmp("scsi", aa->aa_ident))
		return (0);

	return (1);
}

void
hvs_attach(struct device *parent, struct device *self, void *aux)
{
	struct hv_attach_args *aa = aux;
	struct hvs_softc *sc = (struct hvs_softc *)self;
	struct scsibus_attach_args saa;
	extern int pciide_skip_ata;

	sc->sc_hvsc = (struct hv_softc *)parent;
	sc->sc_chan = aa->aa_chan;
	sc->sc_dmat = aa->aa_dmat;

	printf(" channel %u: %s", sc->sc_chan->ch_id, aa->aa_ident);

	if (strcmp("scsi", aa->aa_ident) == 0)
		sc->sc_flags |= HVSF_SCSI;

	if (hv_channel_setdeferred(sc->sc_chan, sc->sc_dev.dv_xname)) {
		printf(": failed to create the interrupt thread\n");
		return;
	}

	if (hv_channel_open(sc->sc_chan, HVS_RING_SIZE, &sc->sc_props,
	    sizeof(sc->sc_props), hvs_intr, sc)) {
		printf(": failed to open channel\n");
		return;
	}

	hv_evcount_attach(sc->sc_chan, sc->sc_dev.dv_xname);

	if (hvs_alloc_ccbs(sc))
		return;

	if (hvs_connect(sc))
		return;

	printf(", protocol %u.%u\n", (sc->sc_proto >> 8) & 0xff,
	    sc->sc_proto & 0xff);

	if (sc->sc_proto >= HVS_PROTO_VERSION_WIN8)
		sc->sc_flags |= HVSF_W8PLUS;

	task_set(&sc->sc_probetask, hvs_scsi_probe, sc);

	saa.saa_adapter = &hvs_switch;
	saa.saa_adapter_softc = self;
	saa.saa_luns = sc->sc_flags & HVSF_SCSI ? 64 : 1;
	saa.saa_adapter_buswidth = 2;
	saa.saa_adapter_target = SDEV_NO_ADAPTER_TARGET;
	saa.saa_openings = sc->sc_nccb;
	saa.saa_pool = &sc->sc_iopool;
	saa.saa_quirks = saa.saa_flags = 0;
	saa.saa_wwpn = saa.saa_wwnn = 0;

	sc->sc_scsibus = config_found(self, &saa, scsiprint);

	/*
	 * If the driver has successfully attached to an IDE
	 * device, we need to make sure that the same disk is
	 * not available to the system via pciide(4) causing
	 * DUID conflicts and preventing system from booting.
	 */
	if (!(sc->sc_flags & HVSF_SCSI) && sc->sc_scsibus)
		pciide_skip_ata = 1;
}

void
hvs_scsi_cmd(struct scsi_xfer *xs)
{
	struct scsi_link *link = xs->sc_link;
	struct hvs_softc *sc = link->bus->sb_adapter_softc;
	struct hvs_ccb *ccb = xs->io;
	union hvs_cmd cmd;
	struct hvs_cmd_io *io = &cmd.io;
	struct hvs_srb *srb = &io->cmd_srb;
	int i, rv, flags = BUS_DMA_NOWAIT;

	if (xs->cmdlen > MAX_SRB_DATA) {
		printf("%s: CDB is too big: %d\n", sc->sc_dev.dv_xname,
		    xs->cmdlen);
		memset(&xs->sense, 0, sizeof(xs->sense));
		xs->sense.error_code = SSD_ERRCODE_VALID | 0x70;
		xs->sense.flags = SKEY_ILLEGAL_REQUEST;
		xs->sense.add_sense_code = 0x20;
		hvs_scsi_done(xs, XS_SENSE);
		return;
	}

	KERNEL_UNLOCK();

	memset(&cmd, 0, sizeof(cmd));

	srb->srb_initiator = sc->sc_initiator;
	srb->srb_bus = sc->sc_bus;
	srb->srb_target = link->target;
	srb->srb_lun = link->lun;

	srb->srb_cdblen = xs->cmdlen;
	memcpy(srb->srb_data, &xs->cmd, xs->cmdlen);

	switch (xs->flags & (SCSI_DATA_IN | SCSI_DATA_OUT)) {
	case SCSI_DATA_IN:
		srb->srb_direction = SRB_DATA_READ;
		if (sc->sc_flags & HVSF_W8PLUS)
			io->cmd_srbflags |= SRB_FLAGS_DATA_IN;
		flags |= BUS_DMA_WRITE;
		break;
	case SCSI_DATA_OUT:
		srb->srb_direction = SRB_DATA_WRITE;
		if (sc->sc_flags & HVSF_W8PLUS)
			io->cmd_srbflags |= SRB_FLAGS_DATA_OUT;
		flags |= BUS_DMA_READ;
		break;
	default:
		srb->srb_direction = SRB_DATA_NONE;
		if (sc->sc_flags & HVSF_W8PLUS)
			io->cmd_srbflags |= SRB_FLAGS_NO_DATA_TRANSFER;
		break;
	}

	srb->srb_datalen = xs->datalen;

	if (sc->sc_flags & HVSF_W8PLUS) {
		srb->srb_reqlen = sizeof(*io);
		srb->srb_senselen = SENSE_DATA_LEN;
	} else {
		srb->srb_reqlen = sizeof(struct hvs_cmd_hdr) +
		    sizeof(struct hvs_srb);
		srb->srb_senselen = SENSE_DATA_LEN_WIN7;
	}

	cmd.cmd_op = HVS_REQ_SCSIIO;
	cmd.cmd_flags = VMBUS_CHANPKT_FLAG_RC;

	if (xs->datalen > 0) {
		rv = bus_dmamap_load(sc->sc_dmat, ccb->ccb_dmap, xs->data,
		    xs->datalen, NULL, flags);
		if (rv) {
			printf("%s: failed to load %d bytes (%d)\n",
			    sc->sc_dev.dv_xname, xs->datalen, rv);
			KERNEL_LOCK();
			hvs_scsi_done(xs, XS_DRIVER_STUFFUP);
			return;
		}

		ccb->ccb_sgl->gpa_len = xs->datalen;
		ccb->ccb_sgl->gpa_ofs = (vaddr_t)xs->data & PAGE_MASK;
		for (i = 0; i < ccb->ccb_dmap->dm_nsegs; i++)
			ccb->ccb_sgl->gpa_page[i] =
			    atop(ccb->ccb_dmap->dm_segs[i].ds_addr);
		ccb->ccb_nsge = ccb->ccb_dmap->dm_nsegs;
	} else
		ccb->ccb_nsge = 0;

	ccb->ccb_xfer = xs;
	ccb->ccb_cmd = &cmd;
	ccb->ccb_done = hvs_scsi_cmd_done;

#ifdef HVS_DEBUG_IO
	DPRINTF("%s: %u.%u: rid %llu opcode %#x flags %#x datalen %d\n",
	    sc->sc_dev.dv_xname, link->target, link->lun, ccb->ccb_rid,
	    xs->cmd.opcode, xs->flags, xs->datalen);
#endif

	if (xs->flags & SCSI_POLL)
		rv = hvs_poll(sc, ccb);
	else
		rv = hvs_start(sc, ccb);
	if (rv) {
		KERNEL_LOCK();
		hvs_scsi_done(xs, XS_DRIVER_STUFFUP);
		return;
	}

	KERNEL_LOCK();
}

int
hvs_start(struct hvs_softc *sc, struct hvs_ccb *ccb)
{
	union hvs_cmd *cmd = ccb->ccb_cmd;
	int rv;

	ccb->ccb_cmd = NULL;

	if (ccb->ccb_nsge > 0) {
		rv = hv_channel_send_prpl(sc->sc_chan, ccb->ccb_sgl,
		    ccb->ccb_nsge, cmd, HVS_CMD_SIZE, ccb->ccb_rid);
		if (rv) {
			printf("%s: failed to submit operation %x via prpl\n",
			    sc->sc_dev.dv_xname, cmd->cmd_op);
			bus_dmamap_unload(sc->sc_dmat, ccb->ccb_dmap);
		}
	} else {
		rv = hv_channel_send(sc->sc_chan, cmd, HVS_CMD_SIZE,
		    ccb->ccb_rid, VMBUS_CHANPKT_TYPE_INBAND,
		    VMBUS_CHANPKT_FLAG_RC);
		if (rv)
			printf("%s: failed to submit operation %x\n",
			    sc->sc_dev.dv_xname, cmd->cmd_op);
	}

	return (rv);
}

void
hvs_poll_done(struct hvs_ccb *ccb)
{
	int *rv = ccb->ccb_cookie;

	if (ccb->ccb_cmd) {
		memcpy(&ccb->ccb_rsp, ccb->ccb_cmd, HVS_CMD_SIZE);
		ccb->ccb_cmd = &ccb->ccb_rsp;
	} else
		memset(&ccb->ccb_rsp, 0, HVS_CMD_SIZE);

	*rv = 0;
}

int
hvs_poll(struct hvs_softc *sc, struct hvs_ccb *ccb)
{
	void (*done)(struct hvs_ccb *);
	void *cookie;
	int s, rv = 1;

	done = ccb->ccb_done;
	cookie = ccb->ccb_cookie;

	ccb->ccb_done = hvs_poll_done;
	ccb->ccb_cookie = &rv;

	if (hvs_start(sc, ccb)) {
		ccb->ccb_cookie = cookie;
		ccb->ccb_done = done;
		return (-1);
	}

	while (rv == 1) {
		delay(10);
		s = splbio();
		hvs_intr(sc);
		splx(s);
	}

	ccb->ccb_cookie = cookie;
	ccb->ccb_done = done;
	ccb->ccb_done(ccb);

	return (0);
}

void
hvs_intr(void *xsc)
{
	struct hvs_softc *sc = xsc;
	struct hvs_ccb *ccb;
	union hvs_cmd cmd;
	uint64_t rid;
	uint32_t rlen;
	int rv;

	for (;;) {
		rv = hv_channel_recv(sc->sc_chan, &cmd, sizeof(cmd), &rlen,
		    &rid, 0);
		switch (rv) {
		case 0:
			break;
		case EAGAIN:
			/* No more messages to process */
			return;
		default:
			printf("%s: error %d while receiving a reply\n",
			    sc->sc_dev.dv_xname, rv);
			return;
		}
		if (rlen != sizeof(cmd)) {
			printf("%s: short read: %u\n", sc->sc_dev.dv_xname,
			    rlen);
			return;
		}

#ifdef HVS_DEBUG_IO
		DPRINTF("%s: rid %llu operation %u flags %#x status %#x\n",
		    sc->sc_dev.dv_xname, rid, cmd.cmd_op, cmd.cmd_flags,
		    cmd.cmd_status);
#endif

		switch (cmd.cmd_op) {
		case HVS_MSG_IODONE:
			if (rid >= sc->sc_nccb) {
				printf("%s: invalid response %#llx\n",
				    sc->sc_dev.dv_xname, rid);
				continue;
			}
			ccb = &sc->sc_ccbs[rid];
			ccb->ccb_cmd = &cmd;
			ccb->ccb_done(ccb);
			break;
		case HVS_MSG_ENUMERATE:
			task_add(systq, &sc->sc_probetask);
			break;
		default:
			printf("%s: operation %u is not implemented\n",
			    sc->sc_dev.dv_xname, cmd.cmd_op);
		}
	}
}

static inline int
is_inquiry_valid(struct scsi_inquiry_data *inq)
{
	if ((inq->device & SID_TYPE) == T_NODEVICE)
		return (0);
	if ((inq->device & SID_QUAL) == SID_QUAL_BAD_LU)
		return (0);
	return (1);
}

static inline void
fixup_inquiry(struct scsi_xfer *xs, struct hvs_srb *srb)
{
	struct hvs_softc *sc = xs->sc_link->bus->sb_adapter_softc;
	struct scsi_inquiry_data *inq = (struct scsi_inquiry_data *)xs->data;
	int datalen, resplen;
	char vendor[8];

	resplen = srb->srb_datalen >= SID_SCSI2_HDRLEN ?
	    SID_SCSI2_HDRLEN + inq->additional_length : 0;
	datalen = MIN(resplen, srb->srb_datalen);
	xs->resid = xs->datalen - datalen;

	/* Fixup wrong response from WS2012 */
	if ((sc->sc_proto == HVS_PROTO_VERSION_WIN8_1 ||
	    sc->sc_proto == HVS_PROTO_VERSION_WIN8 ||
	    sc->sc_proto == HVS_PROTO_VERSION_WIN7) &&
	    !is_inquiry_valid(inq) && datalen >= 4 &&
	    (inq->version == 0 || inq->response_format == 0)) {
		inq->version = SCSI_REV_SPC3;
		inq->response_format = SID_SCSI2_RESPONSE;
	} else if (datalen >= SID_SCSI2_HDRLEN + SID_SCSI2_ALEN) {
		/*
		 * Upgrade SPC2 to SPC3 if host is Win8 or WS2012 R2
		 * to support UNMAP feature.
		 */
		scsi_strvis(vendor, inq->vendor, sizeof(vendor));
		if ((sc->sc_proto == HVS_PROTO_VERSION_WIN8_1 ||
		    sc->sc_proto == HVS_PROTO_VERSION_WIN8) &&
		    (SID_ANSII_REV(inq) == SCSI_REV_SPC2) &&
		    !strncmp(vendor, "Msft", 4))
			inq->version = SCSI_REV_SPC3;
	}
}

void
hvs_scsi_cmd_done(struct hvs_ccb *ccb)
{
	struct scsi_xfer *xs = ccb->ccb_xfer;
	struct hvs_softc *sc = xs->sc_link->bus->sb_adapter_softc;
	union hvs_cmd *cmd = ccb->ccb_cmd;
	struct hvs_srb *srb;
	bus_dmamap_t map;
	int error;

	map = ccb->ccb_dmap;
	bus_dmamap_sync(sc->sc_dmat, map, 0, map->dm_mapsize,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	bus_dmamap_unload(sc->sc_dmat, map);

	xs = ccb->ccb_xfer;
	srb = &cmd->io.cmd_srb;

	xs->status = srb->srb_scsistatus & 0xff;

	switch (xs->status) {
	case SCSI_OK:
		if ((srb->srb_iostatus & ~(SRB_STATUS_AUTOSENSE_VALID |
		    SRB_STATUS_QUEUE_FROZEN)) != SRB_STATUS_SUCCESS)
			error = XS_SELTIMEOUT;
		else
			error = XS_NOERROR;
		break;
	case SCSI_BUSY:
	case SCSI_QUEUE_FULL:
		printf("%s: status %#x iostatus %#x (busy)\n",
		    sc->sc_dev.dv_xname, srb->srb_scsistatus,
		    srb->srb_iostatus);
		error = XS_BUSY;
		break;
	case SCSI_CHECK:
		if (srb->srb_iostatus & SRB_STATUS_AUTOSENSE_VALID) {
			memcpy(&xs->sense, srb->srb_data,
			    MIN(sizeof(xs->sense), srb->srb_senselen));
			error = XS_SENSE;
			break;
		}
		/* FALLTHROUGH */
	default:
		error = XS_DRIVER_STUFFUP;
	}

	if (error == XS_NOERROR) {
		if (xs->cmd.opcode == INQUIRY)
			fixup_inquiry(xs, srb);
		else if (srb->srb_direction != SRB_DATA_NONE)
			xs->resid = xs->datalen - srb->srb_datalen;
	}

	KERNEL_LOCK();
	hvs_scsi_done(xs, error);
	KERNEL_UNLOCK();
}

void
hvs_scsi_probe(void *arg)
{
	struct hvs_softc *sc = arg;

	if (sc->sc_scsibus)
		scsi_probe_bus((void *)sc->sc_scsibus);
}

void
hvs_scsi_done(struct scsi_xfer *xs, int error)
{
	int s;

	KERNEL_ASSERT_LOCKED();

	xs->error = error;

	s = splbio();
	scsi_done(xs);
	splx(s);
}

int
hvs_connect(struct hvs_softc *sc)
{
	const uint32_t protos[] = {
		HVS_PROTO_VERSION_WIN10,
		HVS_PROTO_VERSION_WIN8_1,
		HVS_PROTO_VERSION_WIN8,
		HVS_PROTO_VERSION_WIN7,
		HVS_PROTO_VERSION_WIN6
	};
	union hvs_cmd ucmd;
	struct hvs_cmd_ver *cmd;
	struct hvs_chp *chp;
	struct hvs_ccb *ccb;
	int i;

	ccb = scsi_io_get(&sc->sc_iopool, SCSI_POLL);
	if (ccb == NULL) {
		printf(": failed to allocate ccb\n");
		return (-1);
	}

	ccb->ccb_done = hvs_empty_done;

	cmd = (struct hvs_cmd_ver *)&ucmd;

	/*
	 * Begin initialization
	 */

	memset(&ucmd, 0, sizeof(ucmd));

	cmd->cmd_op = HVS_REQ_STARTINIT;
	cmd->cmd_flags = VMBUS_CHANPKT_FLAG_RC;

	ccb->ccb_cmd = &ucmd;
	if (hvs_poll(sc, ccb)) {
		printf(": failed to send initialization command\n");
		scsi_io_put(&sc->sc_iopool, ccb);
		return (-1);
	}
	if (ccb->ccb_rsp.cmd_status != 0) {
		printf(": failed to initialize, status %#x\n",
		    ccb->ccb_rsp.cmd_status);
		scsi_io_put(&sc->sc_iopool, ccb);
		return (-1);
	}

	/*
	 * Negotiate protocol version
	 */

	memset(&ucmd, 0, sizeof(ucmd));

	cmd->cmd_op = HVS_REQ_QUERYPROTO;
	cmd->cmd_flags = VMBUS_CHANPKT_FLAG_RC;

	for (i = 0; i < nitems(protos); i++) {
		cmd->cmd_ver = protos[i];

		ccb->ccb_cmd = &ucmd;
		if (hvs_poll(sc, ccb)) {
			printf(": failed to send protocol query\n");
			scsi_io_put(&sc->sc_iopool, ccb);
			return (-1);
		}
		if (ccb->ccb_rsp.cmd_status == 0) {
			sc->sc_proto = protos[i];
			break;
		}
	}
	if (!sc->sc_proto) {
		printf(": failed to negotiate protocol version\n");
		scsi_io_put(&sc->sc_iopool, ccb);
		return (-1);
	}

	/*
	 * Query channel properties
	 */

	memset(&ucmd, 0, sizeof(ucmd));

	cmd->cmd_op = HVS_REQ_QUERYPROPS;
	cmd->cmd_flags = VMBUS_CHANPKT_FLAG_RC;

	ccb->ccb_cmd = &ucmd;
	if (hvs_poll(sc, ccb)) {
		printf(": failed to send channel properties query\n");
		scsi_io_put(&sc->sc_iopool, ccb);
		return (-1);
	}
	if (ccb->ccb_rsp.cmd_op != HVS_MSG_IODONE ||
	    ccb->ccb_rsp.cmd_status != 0) {
		printf(": failed to obtain channel properties, status %#x\n",
		    ccb->ccb_rsp.cmd_status);
		scsi_io_put(&sc->sc_iopool, ccb);
		return (-1);
	}
	chp = &ccb->ccb_rsp.chp.cmd_chp;

	DPRINTF(": proto %#x path %u target %u maxchan %u",
	    chp->chp_proto, chp->chp_path, chp->chp_target,
	    chp->chp_maxchan);
	DPRINTF(" port %u chflags %#x maxfer %u chanid %#llx",
	    chp->chp_port, chp->chp_chflags, chp->chp_maxfer,
	    chp->chp_chanid);

	/* XXX */
	sc->sc_bus = chp->chp_path;
	sc->sc_initiator = chp->chp_target;

	/*
	 * Finish initialization
	 */

	memset(&ucmd, 0, sizeof(ucmd));

	cmd->cmd_op = HVS_REQ_FINISHINIT;
	cmd->cmd_flags = VMBUS_CHANPKT_FLAG_RC;

	ccb->ccb_cmd = &ucmd;
	if (hvs_poll(sc, ccb)) {
		printf(": failed to send initialization finish\n");
		scsi_io_put(&sc->sc_iopool, ccb);
		return (-1);
	}
	if (ccb->ccb_rsp.cmd_op != HVS_MSG_IODONE ||
	    ccb->ccb_rsp.cmd_status != 0) {
		printf(": failed to finish initialization, status %#x\n",
		    ccb->ccb_rsp.cmd_status);
		scsi_io_put(&sc->sc_iopool, ccb);
		return (-1);
	}

	scsi_io_put(&sc->sc_iopool, ccb);

	return (0);
}

void
hvs_empty_done(struct hvs_ccb *ccb)
{
	/* nothing */
}

int
hvs_alloc_ccbs(struct hvs_softc *sc)
{
	int i, error;

	SIMPLEQ_INIT(&sc->sc_ccb_fq);
	mtx_init(&sc->sc_ccb_fqlck, IPL_BIO);

	sc->sc_nccb = HVS_MAX_CCB;

	sc->sc_ccbs = mallocarray(sc->sc_nccb, sizeof(struct hvs_ccb),
	    M_DEVBUF, M_ZERO | M_NOWAIT);
	if (sc->sc_ccbs == NULL) {
		printf(": failed to allocate CCBs\n");
		return (-1);
	}

	for (i = 0; i < sc->sc_nccb; i++) {
		error = bus_dmamap_create(sc->sc_dmat, MAXPHYS, HVS_MAX_SGE,
		    PAGE_SIZE, PAGE_SIZE, BUS_DMA_NOWAIT,
		    &sc->sc_ccbs[i].ccb_dmap);
		if (error) {
			printf(": failed to create a CCB memory map (%d)\n",
			    error);
			goto errout;
		}

		sc->sc_ccbs[i].ccb_sgl = malloc(sizeof(struct vmbus_gpa_range) *
		    (HVS_MAX_SGE + 1), M_DEVBUF, M_ZERO | M_NOWAIT);
		if (sc->sc_ccbs[i].ccb_sgl == NULL) {
			printf(": failed to allocate SGL array\n");
			goto errout;
		}

		sc->sc_ccbs[i].ccb_rid = i;
		hvs_put_ccb(sc, &sc->sc_ccbs[i]);
	}

	scsi_iopool_init(&sc->sc_iopool, sc, hvs_get_ccb, hvs_put_ccb);

	return (0);

 errout:
	hvs_free_ccbs(sc);
	return (-1);
}

void
hvs_free_ccbs(struct hvs_softc *sc)
{
	struct hvs_ccb *ccb;
	int i;

	for (i = 0; i < sc->sc_nccb; i++) {
		ccb = &sc->sc_ccbs[i];
		if (ccb->ccb_dmap == NULL)
			continue;
		bus_dmamap_sync(sc->sc_dmat, ccb->ccb_dmap, 0, 0,
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmat, ccb->ccb_dmap);
		bus_dmamap_destroy(sc->sc_dmat, ccb->ccb_dmap);

		free(ccb->ccb_sgl, M_DEVBUF, sizeof(struct vmbus_gpa_range) *
		    (HVS_MAX_SGE + 1));
	}

	free(sc->sc_ccbs, M_DEVBUF, sc->sc_nccb * sizeof(struct hvs_ccb));
	sc->sc_ccbs = NULL;
	sc->sc_nccb = 0;
}

void *
hvs_get_ccb(void *xsc)
{
	struct hvs_softc *sc = xsc;
	struct hvs_ccb *ccb;

	mtx_enter(&sc->sc_ccb_fqlck);
	ccb = SIMPLEQ_FIRST(&sc->sc_ccb_fq);
	if (ccb != NULL)
		SIMPLEQ_REMOVE_HEAD(&sc->sc_ccb_fq, ccb_link);
	mtx_leave(&sc->sc_ccb_fqlck);

	return (ccb);
}

void
hvs_put_ccb(void *xsc, void *io)
{
	struct hvs_softc *sc = xsc;
	struct hvs_ccb *ccb = io;

	ccb->ccb_cmd = NULL;
	ccb->ccb_xfer = NULL;
	ccb->ccb_done = NULL;
	ccb->ccb_cookie = NULL;
	ccb->ccb_nsge = 0;

	mtx_enter(&sc->sc_ccb_fqlck);
	SIMPLEQ_INSERT_HEAD(&sc->sc_ccb_fq, ccb, ccb_link);
	mtx_leave(&sc->sc_ccb_fqlck);
}
