/*	$OpenBSD: vdsk.c,v 1.73 2022/04/16 19:19:58 naddy Exp $	*/
/*
 * Copyright (c) 2009, 2011 Mark Kettenis
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
#include <sys/buf.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/systm.h>

#include <machine/autoconf.h>
#include <machine/hypervisor.h>

#include <uvm/uvm_extern.h>

#include <scsi/scsi_all.h>
#include <scsi/cd.h>
#include <scsi/scsi_disk.h>
#include <scsi/scsiconf.h>

#include <sparc64/dev/cbusvar.h>
#include <sparc64/dev/ldcvar.h>
#include <sparc64/dev/viovar.h>

#ifdef VDSK_DEBUG
#define DPRINTF(x)	printf x
#else
#define DPRINTF(x)
#endif

#define VDSK_TX_ENTRIES		32
#define VDSK_RX_ENTRIES		32

struct vd_attr_info {
	struct vio_msg_tag	tag;
	uint8_t			xfer_mode;
	uint8_t			vd_type;
	uint8_t			vd_mtype;
	uint8_t			_reserved1;
	uint32_t		vdisk_block_size;
	uint64_t		operations;
	uint64_t		vdisk_size;
	uint64_t		max_xfer_sz;
	uint64_t		_reserved2[2];
};

#define VD_DISK_TYPE_SLICE	0x01
#define VD_DISK_TYPE_DISK	0x02

#define VD_MEDIA_TYPE_FIXED	0x01
#define VD_MEDIA_TYPE_CD	0x02
#define VD_MEDIA_TYPE_DVD	0x03

/* vDisk version 1.0. */
#define VD_OP_BREAD		0x01
#define VD_OP_BWRITE		0x02
#define VD_OP_FLUSH		0x03
#define VD_OP_GET_WCE		0x04
#define VD_OP_SET_WCE		0x05
#define VD_OP_GET_VTOC		0x06
#define VD_OP_SET_VTOC		0x07
#define VD_OP_GET_DISKGEOM	0x08
#define VD_OP_SET_DISKGEOM	0x09
#define VD_OP_GET_DEVID		0x0b
#define VD_OP_GET_EFI		0x0c
#define VD_OP_SET_EFI		0x0d

/* vDisk version 1.1 */
#define VD_OP_SCSICMD		0x0a
#define VD_OP_RESET		0x0e
#define VD_OP_GET_ACCESS	0x0f
#define VD_OP_SET_ACCESS	0x10
#define VD_OP_GET_CAPACITY	0x11

struct vd_desc {
	struct vio_dring_hdr	hdr;
	uint64_t		req_id;
	uint8_t			operation;
	uint8_t			slice;
	uint16_t		_reserved1;
	uint32_t		status;
	uint64_t		offset;
	uint64_t		size;
	uint32_t		ncookies;
	uint32_t		_reserved2;
	struct ldc_cookie	cookie[MAXPHYS / PAGE_SIZE];
};

#define VD_SLICE_NONE		0xff

struct vdsk_dring {
	bus_dmamap_t		vd_map;
	bus_dma_segment_t	vd_seg;
	struct vd_desc		*vd_desc;
	int			vd_nentries;
};

struct vdsk_dring *vdsk_dring_alloc(bus_dma_tag_t, int);
void	vdsk_dring_free(bus_dma_tag_t, struct vdsk_dring *);

/*
 * We support vDisk 1.0 and 1.1.
 */
#define VDSK_MAJOR	1
#define VDSK_MINOR	1

struct vdsk_soft_desc {
	int		vsd_map_idx[MAXPHYS / PAGE_SIZE];
	struct scsi_xfer *vsd_xs;
	int		vsd_ncookies;
};

struct vdsk_softc {
	struct device	sc_dv;
	bus_space_tag_t	sc_bustag;
	bus_dma_tag_t	sc_dmatag;

	void		*sc_tx_ih;
	void		*sc_rx_ih;

	struct ldc_conn	sc_lc;

	uint16_t	sc_vio_state;
#define VIO_SND_VER_INFO	0x0001
#define VIO_ACK_VER_INFO	0x0002
#define VIO_SND_ATTR_INFO	0x0004
#define VIO_ACK_ATTR_INFO	0x0008
#define VIO_SND_DRING_REG	0x0010
#define VIO_ACK_DRING_REG	0x0020
#define VIO_SND_RDX		0x0040
#define VIO_ACK_RDX		0x0080
#define VIO_ESTABLISHED		0x00ff

	uint16_t	sc_major;
	uint16_t	sc_minor;

	uint32_t	sc_local_sid;
	uint64_t	sc_dring_ident;
	uint64_t	sc_seq_no;

	int		sc_tx_cnt;
	int		sc_tx_prod;
	int		sc_tx_cons;

	struct ldc_map	*sc_lm;
	struct vdsk_dring *sc_vd;
	struct vdsk_soft_desc *sc_vsd;

	struct scsi_iopool sc_iopool;

	uint32_t	sc_vdisk_block_size;
	uint64_t	sc_vdisk_size;
	uint8_t		sc_vd_mtype;
};

int	vdsk_match(struct device *, void *, void *);
void	vdsk_attach(struct device *, struct device *, void *);

const struct cfattach vdsk_ca = {
	sizeof(struct vdsk_softc), vdsk_match, vdsk_attach
};

struct cfdriver vdsk_cd = {
	NULL, "vdsk", DV_DULL
};

void	vdsk_scsi_cmd(struct scsi_xfer *);

const struct scsi_adapter vdsk_switch = {
	vdsk_scsi_cmd, NULL, NULL, NULL, NULL
};

int	vdsk_tx_intr(void *);
int	vdsk_rx_intr(void *);

void	vdsk_rx_data(struct ldc_conn *, struct ldc_pkt *);
void	vdsk_rx_vio_ctrl(struct vdsk_softc *, struct vio_msg *);
void	vdsk_rx_vio_ver_info(struct vdsk_softc *, struct vio_msg_tag *);
void	vdsk_rx_vio_attr_info(struct vdsk_softc *, struct vio_msg_tag *);
void	vdsk_rx_vio_dring_reg(struct vdsk_softc *, struct vio_msg_tag *);
void	vdsk_rx_vio_rdx(struct vdsk_softc *sc, struct vio_msg_tag *);
void	vdsk_rx_vio_data(struct vdsk_softc *sc, struct vio_msg *);
void	vdsk_rx_vio_dring_data(struct vdsk_softc *sc, struct vio_msg_tag *);

void	vdsk_ldc_reset(struct ldc_conn *);
void	vdsk_ldc_start(struct ldc_conn *);

void	vdsk_sendmsg(struct vdsk_softc *, void *, size_t);
void	vdsk_send_ver_info(struct vdsk_softc *, uint16_t, uint16_t);
void	vdsk_send_attr_info(struct vdsk_softc *);
void	vdsk_send_dring_reg(struct vdsk_softc *);
void	vdsk_send_rdx(struct vdsk_softc *);

void	*vdsk_io_get(void *);
void	vdsk_io_put(void *, void *);

int	vdsk_submit_cmd(struct scsi_xfer *);
void	vdsk_complete_cmd(struct scsi_xfer *, int);

void	vdsk_scsi_inq(struct scsi_xfer *);
void	vdsk_scsi_inquiry(struct scsi_xfer *);
void	vdsk_scsi_capacity(struct scsi_xfer *);
void	vdsk_scsi_capacity16(struct scsi_xfer *);
void	vdsk_scsi_done(struct scsi_xfer *, int);

int
vdsk_match(struct device *parent, void *match, void *aux)
{
	struct cbus_attach_args *ca = aux;

	if (strcmp(ca->ca_name, "disk") == 0)
		return (1);

	return (0);
}

void
vdsk_attach(struct device *parent, struct device *self, void *aux)
{
	struct vdsk_softc *sc = (struct vdsk_softc *)self;
	struct cbus_attach_args *ca = aux;
	struct scsibus_attach_args saa;
	struct ldc_conn *lc;
	int err, s;
	int timeout;

	sc->sc_bustag = ca->ca_bustag;
	sc->sc_dmatag = ca->ca_dmatag;

	printf(": ivec 0x%llx, 0x%llx", ca->ca_tx_ino, ca->ca_rx_ino);

	/*
	 * Un-configure queues before registering interrupt handlers,
	 * such that we dont get any stale LDC packets or events.
	 */
	hv_ldc_tx_qconf(ca->ca_id, 0, 0);
	hv_ldc_rx_qconf(ca->ca_id, 0, 0);

	sc->sc_tx_ih = bus_intr_establish(ca->ca_bustag, ca->ca_tx_ino,
	    IPL_BIO, 0, vdsk_tx_intr, sc, sc->sc_dv.dv_xname);
	sc->sc_rx_ih = bus_intr_establish(ca->ca_bustag, ca->ca_rx_ino,
	    IPL_BIO, 0, vdsk_rx_intr, sc, sc->sc_dv.dv_xname);
	if (sc->sc_tx_ih == NULL || sc->sc_rx_ih == NULL) {
		printf(", can't establish interrupt\n");
		return;
	}

	lc = &sc->sc_lc;
	lc->lc_id = ca->ca_id;
	lc->lc_sc = sc;
	lc->lc_reset = vdsk_ldc_reset;
	lc->lc_start = vdsk_ldc_start;
	lc->lc_rx_data = vdsk_rx_data;

	lc->lc_txq = ldc_queue_alloc(sc->sc_dmatag, VDSK_TX_ENTRIES);
	if (lc->lc_txq == NULL) {
		printf(", can't allocate tx queue\n");
		return;
	}

	lc->lc_rxq = ldc_queue_alloc(sc->sc_dmatag, VDSK_RX_ENTRIES);
	if (lc->lc_rxq == NULL) {
		printf(", can't allocate rx queue\n");
		goto free_txqueue;
	}

	sc->sc_lm = ldc_map_alloc(sc->sc_dmatag, 2048);
	if (sc->sc_lm == NULL) {
		printf(", can't allocate LDC mapping table\n");
		goto free_rxqueue;
	}

	err = hv_ldc_set_map_table(lc->lc_id,
	    sc->sc_lm->lm_map->dm_segs[0].ds_addr, sc->sc_lm->lm_nentries);
	if (err != H_EOK) {
		printf("hv_ldc_set_map_table %d\n", err);
		goto free_map;
	}

	sc->sc_vd = vdsk_dring_alloc(sc->sc_dmatag, 32);
	if (sc->sc_vd == NULL) {
		printf(", can't allocate dring\n");
		goto free_map;
	}
	sc->sc_vsd = malloc(32 * sizeof(*sc->sc_vsd), M_DEVBUF, M_NOWAIT);
	if (sc->sc_vsd == NULL) {
		printf(", can't allocate software ring\n");
		goto free_dring;
	}

	sc->sc_lm->lm_slot[0].entry = sc->sc_vd->vd_map->dm_segs[0].ds_addr;
	sc->sc_lm->lm_slot[0].entry &= LDC_MTE_RA_MASK;
	sc->sc_lm->lm_slot[0].entry |= LDC_MTE_CPR | LDC_MTE_CPW;
	sc->sc_lm->lm_slot[0].entry |= LDC_MTE_R | LDC_MTE_W;
	sc->sc_lm->lm_next = 1;
	sc->sc_lm->lm_count = 1;

	err = hv_ldc_tx_qconf(lc->lc_id,
	    lc->lc_txq->lq_map->dm_segs[0].ds_addr, lc->lc_txq->lq_nentries);
	if (err != H_EOK)
		printf("hv_ldc_tx_qconf %d\n", err);

	err = hv_ldc_rx_qconf(lc->lc_id,
	    lc->lc_rxq->lq_map->dm_segs[0].ds_addr, lc->lc_rxq->lq_nentries);
	if (err != H_EOK)
		printf("hv_ldc_rx_qconf %d\n", err);

	cbus_intr_setenabled(sc->sc_bustag, ca->ca_tx_ino, INTR_ENABLED);
	cbus_intr_setenabled(sc->sc_bustag, ca->ca_rx_ino, INTR_ENABLED);

	ldc_send_vers(lc);

	printf("\n");

	/*
	 * Interrupts aren't enabled during autoconf, so poll for VIO
	 * peer-to-peer handshake completion.
	 */
	s = splbio();
	timeout = 1000;
	do {
		if (vdsk_rx_intr(sc) && sc->sc_vio_state == VIO_ESTABLISHED)
			break;

		delay(1000);
	} while(--timeout > 0);
	splx(s);

	if (sc->sc_vio_state != VIO_ESTABLISHED)
		return;

	scsi_iopool_init(&sc->sc_iopool, sc, vdsk_io_get, vdsk_io_put);


	saa.saa_adapter = &vdsk_switch;
	saa.saa_adapter_softc = self;
	saa.saa_adapter_buswidth = 1;
	saa.saa_luns = 1;
	saa.saa_adapter_target = SDEV_NO_ADAPTER_TARGET;
	saa.saa_openings = sc->sc_vd->vd_nentries - 1;
	saa.saa_pool = &sc->sc_iopool;
	saa.saa_quirks = saa.saa_flags = 0;
	saa.saa_wwpn = saa.saa_wwnn = 0;

	config_found(self, &saa, scsiprint);

	return;

free_dring:
	vdsk_dring_free(sc->sc_dmatag, sc->sc_vd);
free_map:
	hv_ldc_set_map_table(lc->lc_id, 0, 0);
	ldc_map_free(sc->sc_dmatag, sc->sc_lm);
free_rxqueue:
	ldc_queue_free(sc->sc_dmatag, lc->lc_rxq);
free_txqueue:
	ldc_queue_free(sc->sc_dmatag, lc->lc_txq);
}

int
vdsk_tx_intr(void *arg)
{
	struct vdsk_softc *sc = arg;
	struct ldc_conn *lc = &sc->sc_lc;
	uint64_t tx_head, tx_tail, tx_state;

	hv_ldc_tx_get_state(lc->lc_id, &tx_head, &tx_tail, &tx_state);
	if (tx_state != lc->lc_tx_state) {
		switch (tx_state) {
		case LDC_CHANNEL_DOWN:
			DPRINTF(("%s: Tx link down\n", __func__));
			break;
		case LDC_CHANNEL_UP:
			DPRINTF(("%s: Tx link up\n", __func__));
			break;
		case LDC_CHANNEL_RESET:
			DPRINTF(("%s: Tx link reset\n", __func__));
			break;
		}
		lc->lc_tx_state = tx_state;
	}

	return (1);
}

int
vdsk_rx_intr(void *arg)
{
	struct vdsk_softc *sc = arg;
	struct ldc_conn *lc = &sc->sc_lc;
	uint64_t rx_head, rx_tail, rx_state;
	struct ldc_pkt *lp;
	int err;

	err = hv_ldc_rx_get_state(lc->lc_id, &rx_head, &rx_tail, &rx_state);
	if (err == H_EINVAL)
		return (0);
	if (err != H_EOK) {
		printf("hv_ldc_rx_get_state %d\n", err);
		return (0);
	}

	if (rx_state != lc->lc_rx_state) {
		sc->sc_vio_state = 0;
		lc->lc_tx_seqid = 0;
		lc->lc_state = 0;
		switch (rx_state) {
		case LDC_CHANNEL_DOWN:
			DPRINTF(("%s: Rx link down\n", __func__));
			break;
		case LDC_CHANNEL_UP:
			DPRINTF(("%s: Rx link up\n", __func__));
			ldc_send_vers(lc);
			break;
		case LDC_CHANNEL_RESET:
			DPRINTF(("%s: Rx link reset\n", __func__));
			break;
		}
		lc->lc_rx_state = rx_state;
		hv_ldc_rx_set_qhead(lc->lc_id, rx_tail);
		return (1);
	}

	if (rx_head == rx_tail)
		return (0);

	lp = (struct ldc_pkt *)(lc->lc_rxq->lq_va + rx_head);
	switch (lp->type) {
	case LDC_CTRL:
		ldc_rx_ctrl(lc, lp);
		break;

	case LDC_DATA:
		ldc_rx_data(lc, lp);
		break;

	default:
		DPRINTF(("%0x02/%0x02/%0x02\n", lp->type, lp->stype,
		    lp->ctrl));
		ldc_reset(lc);
		break;
	}

	if (lc->lc_state == 0)
		return (1);

	rx_head += sizeof(*lp);
	rx_head &= ((lc->lc_rxq->lq_nentries * sizeof(*lp)) - 1);
	err = hv_ldc_rx_set_qhead(lc->lc_id, rx_head);
	if (err != H_EOK)
		printf("%s: hv_ldc_rx_set_qhead %d\n", __func__, err);

	return (1);
}

void
vdsk_rx_data(struct ldc_conn *lc, struct ldc_pkt *lp)
{
	struct vio_msg *vm = (struct vio_msg *)lp;

	switch (vm->type) {
	case VIO_TYPE_CTRL:
		if ((lp->env & LDC_FRAG_START) == 0 &&
		    (lp->env & LDC_FRAG_STOP) == 0)
			return;
		vdsk_rx_vio_ctrl(lc->lc_sc, vm);
		break;

	case VIO_TYPE_DATA:
		if((lp->env & LDC_FRAG_START) == 0)
			return;
		vdsk_rx_vio_data(lc->lc_sc, vm);
		break;

	default:
		DPRINTF(("Unhandled packet type 0x%02x\n", vm->type));
		ldc_reset(lc);
		break;
	}
}

void
vdsk_rx_vio_ctrl(struct vdsk_softc *sc, struct vio_msg *vm)
{
	struct vio_msg_tag *tag = (struct vio_msg_tag *)&vm->type;

	switch (tag->stype_env) {
	case VIO_VER_INFO:
		vdsk_rx_vio_ver_info(sc, tag);
		break;
	case VIO_ATTR_INFO:
		vdsk_rx_vio_attr_info(sc, tag);
		break;
	case VIO_DRING_REG:
		vdsk_rx_vio_dring_reg(sc, tag);
		break;
	case VIO_RDX:
		vdsk_rx_vio_rdx(sc, tag);
		break;
	default:
		DPRINTF(("CTRL/0x%02x/0x%04x\n", tag->stype, tag->stype_env));
		break;
	}
}

void
vdsk_rx_vio_ver_info(struct vdsk_softc *sc, struct vio_msg_tag *tag)
{
	struct vio_ver_info *vi = (struct vio_ver_info *)tag;

	switch (vi->tag.stype) {
	case VIO_SUBTYPE_INFO:
		DPRINTF(("CTRL/INFO/VER_INFO\n"));
		break;

	case VIO_SUBTYPE_ACK:
		DPRINTF(("CTRL/ACK/VER_INFO\n"));
		if (!ISSET(sc->sc_vio_state, VIO_SND_VER_INFO)) {
			ldc_reset(&sc->sc_lc);
			break;
		}
		sc->sc_major = vi->major;
		sc->sc_minor = vi->minor;
		sc->sc_vio_state |= VIO_ACK_VER_INFO;
		break;

	default:
		DPRINTF(("CTRL/0x%02x/VER_INFO\n", vi->tag.stype));
		break;
	}

	if (ISSET(sc->sc_vio_state, VIO_ACK_VER_INFO))
		vdsk_send_attr_info(sc);
}

void
vdsk_rx_vio_attr_info(struct vdsk_softc *sc, struct vio_msg_tag *tag)
{
	struct vd_attr_info *ai = (struct vd_attr_info *)tag;

	switch (ai->tag.stype) {
	case VIO_SUBTYPE_INFO:
		DPRINTF(("CTRL/INFO/ATTR_INFO\n"));
		break;

	case VIO_SUBTYPE_ACK:
		DPRINTF(("CTRL/ACK/ATTR_INFO\n"));
		if (!ISSET(sc->sc_vio_state, VIO_SND_ATTR_INFO)) {
			ldc_reset(&sc->sc_lc);
			break;
		}

		sc->sc_vdisk_block_size = ai->vdisk_block_size;
		sc->sc_vdisk_size = ai->vdisk_size;
		if (sc->sc_major > 1 || sc->sc_minor >= 1)
			sc->sc_vd_mtype = ai->vd_mtype;
		else
			sc->sc_vd_mtype = VD_MEDIA_TYPE_FIXED;

		sc->sc_vio_state |= VIO_ACK_ATTR_INFO;
		break;

	default:
		DPRINTF(("CTRL/0x%02x/ATTR_INFO\n", ai->tag.stype));
		break;
	}

	if (ISSET(sc->sc_vio_state, VIO_ACK_ATTR_INFO))
		vdsk_send_dring_reg(sc);

}

void
vdsk_rx_vio_dring_reg(struct vdsk_softc *sc, struct vio_msg_tag *tag)
{
	struct vio_dring_reg *dr = (struct vio_dring_reg *)tag;

	switch (dr->tag.stype) {
	case VIO_SUBTYPE_INFO:
		DPRINTF(("CTRL/INFO/DRING_REG\n"));
		break;

	case VIO_SUBTYPE_ACK:
		DPRINTF(("CTRL/ACK/DRING_REG\n"));
		if (!ISSET(sc->sc_vio_state, VIO_SND_DRING_REG)) {
			ldc_reset(&sc->sc_lc);
			break;
		}

		sc->sc_dring_ident = dr->dring_ident;
		sc->sc_seq_no = 1;

		sc->sc_vio_state |= VIO_ACK_DRING_REG;
		break;

	default:
		DPRINTF(("CTRL/0x%02x/DRING_REG\n", dr->tag.stype));
		break;
	}

	if (ISSET(sc->sc_vio_state, VIO_ACK_DRING_REG))
		vdsk_send_rdx(sc);
}

void
vdsk_rx_vio_rdx(struct vdsk_softc *sc, struct vio_msg_tag *tag)
{
	switch(tag->stype) {
	case VIO_SUBTYPE_INFO:
		DPRINTF(("CTRL/INFO/RDX\n"));
		break;

	case VIO_SUBTYPE_ACK:
	{
		int prod;

		DPRINTF(("CTRL/ACK/RDX\n"));
		if (!ISSET(sc->sc_vio_state, VIO_SND_RDX)) {
			ldc_reset(&sc->sc_lc);
			break;
		}
		sc->sc_vio_state |= VIO_ACK_RDX;

		/*
		 * If this ACK is the result of a reconnect, we may
		 * have pending I/O that we need to resubmit.  We need
		 * to rebuild the ring descriptors though since the
		 * vDisk server on the other side may have touched
		 * them already.  So we just clean up the ring and the
		 * LDC map and resubmit the SCSI commands based on our
		 * soft descriptors.
		 */
		prod = sc->sc_tx_prod;
		sc->sc_tx_prod = sc->sc_tx_cons;
		sc->sc_tx_cnt = 0;
		sc->sc_lm->lm_next = 1;
		sc->sc_lm->lm_count = 1;
		while (sc->sc_tx_prod != prod)
			vdsk_submit_cmd(sc->sc_vsd[sc->sc_tx_prod].vsd_xs);

		scsi_iopool_run(&sc->sc_iopool);
		break;
	}

	default:
		DPRINTF(("CTRL/0x%02x/RDX (VIO)\n", tag->stype));
		break;
	}
}

void
vdsk_rx_vio_data(struct vdsk_softc *sc, struct vio_msg *vm)
{
	struct vio_msg_tag *tag = (struct vio_msg_tag *)&vm->type;

	if (sc->sc_vio_state != VIO_ESTABLISHED) {
		DPRINTF(("Spurious DATA/0x%02x/0x%04x\n", tag->stype,
		    tag->stype_env));
		return;
	}

	switch(tag->stype_env) {
	case VIO_DRING_DATA:
		vdsk_rx_vio_dring_data(sc, tag);
		break;

	default:
		DPRINTF(("DATA/0x%02x/0x%04x\n", tag->stype, tag->stype_env));
		break;
	}
}

void
vdsk_rx_vio_dring_data(struct vdsk_softc *sc, struct vio_msg_tag *tag)
{
	switch(tag->stype) {
	case VIO_SUBTYPE_INFO:
		DPRINTF(("DATA/INFO/DRING_DATA\n"));
		break;

	case VIO_SUBTYPE_ACK:
	{
		struct scsi_xfer *xs;
		int cons;

		cons = sc->sc_tx_cons;
		while (sc->sc_vd->vd_desc[cons].hdr.dstate == VIO_DESC_DONE) {
			xs = sc->sc_vsd[cons].vsd_xs;
			if (ISSET(xs->flags, SCSI_POLL) == 0)
				vdsk_complete_cmd(xs, cons);
			cons++;
			cons &= (sc->sc_vd->vd_nentries - 1);
		}
		sc->sc_tx_cons = cons;
		break;
	}

	case VIO_SUBTYPE_NACK:
		DPRINTF(("DATA/NACK/DRING_DATA\n"));
		break;

	default:
		DPRINTF(("DATA/0x%02x/DRING_DATA\n", tag->stype));
		break;
	}
}

void
vdsk_ldc_reset(struct ldc_conn *lc)
{
	struct vdsk_softc *sc = lc->lc_sc;

	sc->sc_vio_state = 0;
}

void
vdsk_ldc_start(struct ldc_conn *lc)
{
	struct vdsk_softc *sc = lc->lc_sc;

	vdsk_send_ver_info(sc, VDSK_MAJOR, VDSK_MINOR);
}

void
vdsk_sendmsg(struct vdsk_softc *sc, void *msg, size_t len)
{
	struct ldc_conn *lc = &sc->sc_lc;
	int err;

	err = ldc_send_unreliable(lc, msg, len);
	if (err)
		printf("%s: ldc_send_unreliable: %d\n", __func__, err);
}

void
vdsk_send_ver_info(struct vdsk_softc *sc, uint16_t major, uint16_t minor)
{
	struct vio_ver_info vi;

	/* Allocate new session ID. */
	sc->sc_local_sid = tick();

	bzero(&vi, sizeof(vi));
	vi.tag.type = VIO_TYPE_CTRL;
	vi.tag.stype = VIO_SUBTYPE_INFO;
	vi.tag.stype_env = VIO_VER_INFO;
	vi.tag.sid = sc->sc_local_sid;
	vi.major = major;
	vi.minor = minor;
	vi.dev_class = VDEV_DISK;
	vdsk_sendmsg(sc, &vi, sizeof(vi));

	sc->sc_vio_state |= VIO_SND_VER_INFO;
}

void
vdsk_send_attr_info(struct vdsk_softc *sc)
{
	struct vd_attr_info ai;

	bzero(&ai, sizeof(ai));
	ai.tag.type = VIO_TYPE_CTRL;
	ai.tag.stype = VIO_SUBTYPE_INFO;
	ai.tag.stype_env = VIO_ATTR_INFO;
	ai.tag.sid = sc->sc_local_sid;
	ai.xfer_mode = VIO_DRING_MODE;
	ai.vdisk_block_size = DEV_BSIZE;
	ai.max_xfer_sz = MAXPHYS / DEV_BSIZE;
	vdsk_sendmsg(sc, &ai, sizeof(ai));

	sc->sc_vio_state |= VIO_SND_ATTR_INFO;
}

void
vdsk_send_dring_reg(struct vdsk_softc *sc)
{
	struct vio_dring_reg dr;

	bzero(&dr, sizeof(dr));
	dr.tag.type = VIO_TYPE_CTRL;
	dr.tag.stype = VIO_SUBTYPE_INFO;
	dr.tag.stype_env = VIO_DRING_REG;
	dr.tag.sid = sc->sc_local_sid;
	dr.dring_ident = 0;
	dr.num_descriptors = sc->sc_vd->vd_nentries;
	dr.descriptor_size = sizeof(struct vd_desc);
	dr.options = VIO_TX_RING | VIO_RX_RING;
	dr.ncookies = 1;
	dr.cookie[0].addr = 0;
	dr.cookie[0].size = PAGE_SIZE;
	vdsk_sendmsg(sc, &dr, sizeof(dr));

	sc->sc_vio_state |= VIO_SND_DRING_REG;
};

void
vdsk_send_rdx(struct vdsk_softc *sc)
{
	struct vio_rdx rdx;

	bzero(&rdx, sizeof(rdx));
	rdx.tag.type = VIO_TYPE_CTRL;
	rdx.tag.stype = VIO_SUBTYPE_INFO;
	rdx.tag.stype_env = VIO_RDX;
	rdx.tag.sid = sc->sc_local_sid;
	vdsk_sendmsg(sc, &rdx, sizeof(rdx));

	sc->sc_vio_state |= VIO_SND_RDX;
}

struct vdsk_dring *
vdsk_dring_alloc(bus_dma_tag_t t, int nentries)
{
	struct vdsk_dring *vd;
	bus_size_t size;
	caddr_t va;
	int nsegs;
	int i;

	vd = malloc(sizeof(struct vdsk_dring), M_DEVBUF, M_NOWAIT);
	if (vd == NULL)
		return NULL;

	size = roundup(nentries * sizeof(struct vd_desc), PAGE_SIZE);

	if (bus_dmamap_create(t, size, 1, size, 0,
	    BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW, &vd->vd_map) != 0)
		goto error;

	if (bus_dmamem_alloc(t, size, PAGE_SIZE, 0, &vd->vd_seg, 1,
	    &nsegs, BUS_DMA_NOWAIT) != 0)
		goto destroy;

	if (bus_dmamem_map(t, &vd->vd_seg, 1, size, &va,
	    BUS_DMA_NOWAIT) != 0)
		goto free;

	if (bus_dmamap_load(t, vd->vd_map, va, size, NULL,
	    BUS_DMA_NOWAIT) != 0)
		goto unmap;

	vd->vd_desc = (struct vd_desc *)va;
	vd->vd_nentries = nentries;
	bzero(vd->vd_desc, nentries * sizeof(struct vd_desc));
	for (i = 0; i < vd->vd_nentries; i++)
		vd->vd_desc[i].hdr.dstate = VIO_DESC_FREE;
	return (vd);

unmap:
	bus_dmamem_unmap(t, va, size);
free:
	bus_dmamem_free(t, &vd->vd_seg, 1);
destroy:
	bus_dmamap_destroy(t, vd->vd_map);
error:
	free(vd, M_DEVBUF, sizeof(struct vdsk_dring));

	return (NULL);
}

void
vdsk_dring_free(bus_dma_tag_t t, struct vdsk_dring *vd)
{
	bus_size_t size;

	size = vd->vd_nentries * sizeof(struct vd_desc);
	size = roundup(size, PAGE_SIZE);

	bus_dmamap_unload(t, vd->vd_map);
	bus_dmamem_unmap(t, (caddr_t)vd->vd_desc, size);
	bus_dmamem_free(t, &vd->vd_seg, 1);
	bus_dmamap_destroy(t, vd->vd_map);
	free(vd, M_DEVBUF, 0);
}

void *
vdsk_io_get(void *xsc)
{
	struct vdsk_softc *sc = xsc;
	void *rv = sc; /* just has to be !NULL */
	int s;

	s = splbio();
	if (sc->sc_vio_state != VIO_ESTABLISHED ||
	    sc->sc_tx_cnt >= sc->sc_vd->vd_nentries)
		rv = NULL;
	else
		sc->sc_tx_cnt++;
	splx(s);

	return (rv);
}

void
vdsk_io_put(void *xsc, void *io)
{
	struct vdsk_softc *sc = xsc;
	int s;

#ifdef DIAGNOSTIC
	if (sc != io)
		panic("vdsk_io_put: unexpected io");
#endif

	s = splbio();
	sc->sc_tx_cnt--;
	splx(s);
}

void
vdsk_scsi_cmd(struct scsi_xfer *xs)
{
	struct vdsk_softc *sc = xs->sc_link->bus->sb_adapter_softc;
	int timeout, s;
	int desc;

	switch (xs->cmd.opcode) {
	case READ_COMMAND:
	case READ_10:
	case READ_12:
	case READ_16:
	case WRITE_COMMAND:
	case WRITE_10:
	case WRITE_12:
	case WRITE_16:
	case SYNCHRONIZE_CACHE:
		break;

	case INQUIRY:
		vdsk_scsi_inq(xs);
		return;
	case READ_CAPACITY:
		vdsk_scsi_capacity(xs);
		return;
	case READ_CAPACITY_16:
		vdsk_scsi_capacity16(xs);
		return;

	case TEST_UNIT_READY:
	case START_STOP:
	case PREVENT_ALLOW:
		vdsk_scsi_done(xs, XS_NOERROR);
		return;

	default:
		printf("%s cmd 0x%02x\n", __func__, xs->cmd.opcode);
	case MODE_SENSE:
	case MODE_SENSE_BIG:
	case REPORT_LUNS:
	case READ_TOC:
		vdsk_scsi_done(xs, XS_DRIVER_STUFFUP);
		return;
	}

	s = splbio();
	desc = vdsk_submit_cmd(xs);

	if (!ISSET(xs->flags, SCSI_POLL)) {
		splx(s);
		return;
	}

	timeout = 1000;
	do {
		if (sc->sc_vd->vd_desc[desc].hdr.dstate == VIO_DESC_DONE)
			break;

		delay(1000);
	} while(--timeout > 0);
	if (sc->sc_vd->vd_desc[desc].hdr.dstate == VIO_DESC_DONE) {
		vdsk_complete_cmd(xs, desc);
	} else {
		ldc_reset(&sc->sc_lc);
		vdsk_scsi_done(xs, XS_TIMEOUT);
	}
	splx(s);
}

int
vdsk_submit_cmd(struct scsi_xfer *xs)
{
	struct vdsk_softc *sc = xs->sc_link->bus->sb_adapter_softc;
	struct ldc_map *map = sc->sc_lm;
	struct vio_dring_msg dm;
	struct scsi_rw *rw;
	struct scsi_rw_10 *rw10;
	struct scsi_rw_12 *rw12;
	struct scsi_rw_16 *rw16;
	u_int64_t lba;
	u_int32_t sector_count;
	uint8_t operation;
	vaddr_t va;
	paddr_t pa;
	psize_t nbytes;
	int len, ncookies;
	int desc;

	switch (xs->cmd.opcode) {
	case READ_COMMAND:
	case READ_10:
	case READ_12:
	case READ_16:
		operation = VD_OP_BREAD;
		break;

	case WRITE_COMMAND:
	case WRITE_10:
	case WRITE_12:
	case WRITE_16:
		operation = VD_OP_BWRITE;
		break;

	case SYNCHRONIZE_CACHE:
		operation = VD_OP_FLUSH;
		break;
	}

	/*
	 * READ/WRITE/SYNCHRONIZE commands. SYNCHRONIZE CACHE has same
	 * layout as 10-byte READ/WRITE commands.
	 */
	if (xs->cmdlen == 6) {
		rw = (struct scsi_rw *)&xs->cmd;
		lba = _3btol(rw->addr) & (SRW_TOPADDR << 16 | 0xffff);
		sector_count = rw->length ? rw->length : 0x100;
	} else if (xs->cmdlen == 10) {
		rw10 = (struct scsi_rw_10 *)&xs->cmd;
		lba = _4btol(rw10->addr);
		sector_count = _2btol(rw10->length);
	} else if (xs->cmdlen == 12) {
		rw12 = (struct scsi_rw_12 *)&xs->cmd;
		lba = _4btol(rw12->addr);
		sector_count = _4btol(rw12->length);
	} else if (xs->cmdlen == 16) {
		rw16 = (struct scsi_rw_16 *)&xs->cmd;
		lba = _8btol(rw16->addr);
		sector_count = _4btol(rw16->length);
	}

	desc = sc->sc_tx_prod;

	ncookies = 0;
	len = xs->datalen;
	va = (vaddr_t)xs->data;
	while (len > 0) {
		KASSERT(ncookies < MAXPHYS / PAGE_SIZE);
		pmap_extract(pmap_kernel(), va, &pa);
		while (map->lm_slot[map->lm_next].entry != 0) {
			map->lm_next++;
			map->lm_next &= (map->lm_nentries - 1);
		}
		map->lm_slot[map->lm_next].entry = (pa & LDC_MTE_RA_MASK);
		map->lm_slot[map->lm_next].entry |= LDC_MTE_CPR | LDC_MTE_CPW;
		map->lm_slot[map->lm_next].entry |= LDC_MTE_IOR | LDC_MTE_IOW;
		map->lm_slot[map->lm_next].entry |= LDC_MTE_R | LDC_MTE_W;
		map->lm_count++;

		nbytes = MIN(len, PAGE_SIZE - (pa & PAGE_MASK));

		sc->sc_vd->vd_desc[desc].cookie[ncookies].addr =
		    map->lm_next << PAGE_SHIFT | (pa & PAGE_MASK);
		sc->sc_vd->vd_desc[desc].cookie[ncookies].size = nbytes;

		sc->sc_vsd[desc].vsd_map_idx[ncookies] = map->lm_next;
		va += nbytes;
		len -= nbytes;
		ncookies++;
	}

	if (ISSET(xs->flags, SCSI_POLL) == 0)
		sc->sc_vd->vd_desc[desc].hdr.ack = 1;
	else
		sc->sc_vd->vd_desc[desc].hdr.ack = 0;
	sc->sc_vd->vd_desc[desc].operation = operation;
	sc->sc_vd->vd_desc[desc].slice = VD_SLICE_NONE;
	sc->sc_vd->vd_desc[desc].status = 0xffffffff;
	sc->sc_vd->vd_desc[desc].offset = lba;
	sc->sc_vd->vd_desc[desc].size = xs->datalen;
	sc->sc_vd->vd_desc[desc].ncookies = ncookies;
	membar_sync();
	sc->sc_vd->vd_desc[desc].hdr.dstate = VIO_DESC_READY;

	sc->sc_vsd[desc].vsd_xs = xs;
	sc->sc_vsd[desc].vsd_ncookies = ncookies;

	sc->sc_tx_prod++;
	sc->sc_tx_prod &= (sc->sc_vd->vd_nentries - 1);

	bzero(&dm, sizeof(dm));
	dm.tag.type = VIO_TYPE_DATA;
	dm.tag.stype = VIO_SUBTYPE_INFO;
	dm.tag.stype_env = VIO_DRING_DATA;
	dm.tag.sid = sc->sc_local_sid;
	dm.seq_no = sc->sc_seq_no++;
	dm.dring_ident = sc->sc_dring_ident;
	dm.start_idx = dm.end_idx = desc;
	vdsk_sendmsg(sc, &dm, sizeof(dm));

	return desc;
}

void
vdsk_complete_cmd(struct scsi_xfer *xs, int desc)
{
	struct vdsk_softc *sc = xs->sc_link->bus->sb_adapter_softc;
	struct ldc_map *map = sc->sc_lm;
	int cookie, idx;
	int error;

	cookie = 0;
	while (cookie < sc->sc_vsd[desc].vsd_ncookies) {
		idx = sc->sc_vsd[desc].vsd_map_idx[cookie++];
		map->lm_slot[idx].entry = 0;
		map->lm_count--;
	}

	error = XS_NOERROR;
	if (sc->sc_vd->vd_desc[desc].status != 0)
		error = XS_DRIVER_STUFFUP;
	xs->resid = xs->datalen -
		sc->sc_vd->vd_desc[desc].size;
	vdsk_scsi_done(xs, error);

	sc->sc_vd->vd_desc[desc].hdr.dstate = VIO_DESC_FREE;
}

void
vdsk_scsi_inq(struct scsi_xfer *xs)
{
	struct scsi_inquiry *inq = (struct scsi_inquiry *)&xs->cmd;

	if (ISSET(inq->flags, SI_EVPD))
		vdsk_scsi_done(xs, XS_DRIVER_STUFFUP);
	else
		vdsk_scsi_inquiry(xs);
}

void
vdsk_scsi_inquiry(struct scsi_xfer *xs)
{
	struct vdsk_softc *sc = xs->sc_link->bus->sb_adapter_softc;
	struct scsi_inquiry_data inq;
	char buf[5];

	bzero(&inq, sizeof(inq));

	switch (sc->sc_vd_mtype) {
	case VD_MEDIA_TYPE_CD:
	case VD_MEDIA_TYPE_DVD:
		inq.device = T_CDROM;
		break;

	case VD_MEDIA_TYPE_FIXED:
	default:
		inq.device = T_DIRECT;
		break;
	}

	inq.version = SCSI_REV_SPC3;
	inq.response_format = SID_SCSI2_RESPONSE;
	inq.additional_length = SID_SCSI2_ALEN;
	inq.flags |= SID_CmdQue;
	bcopy("SUN     ", inq.vendor, sizeof(inq.vendor));
	bcopy("Virtual Disk    ", inq.product, sizeof(inq.product));
	snprintf(buf, sizeof(buf), "%u.%u ", sc->sc_major, sc->sc_minor);
	bcopy(buf, inq.revision, sizeof(inq.revision));

	scsi_copy_internal_data(xs, &inq, sizeof(inq));

	vdsk_scsi_done(xs, XS_NOERROR);
}

void
vdsk_scsi_capacity(struct scsi_xfer *xs)
{
	struct vdsk_softc *sc = xs->sc_link->bus->sb_adapter_softc;
	struct scsi_read_cap_data rcd;
	uint64_t capacity;

	bzero(&rcd, sizeof(rcd));

	capacity = sc->sc_vdisk_size - 1;
	if (capacity > 0xffffffff)
		capacity = 0xffffffff;

	_lto4b(capacity, rcd.addr);
	_lto4b(sc->sc_vdisk_block_size, rcd.length);

	bcopy(&rcd, xs->data, MIN(sizeof(rcd), xs->datalen));

	vdsk_scsi_done(xs, XS_NOERROR);
}

void
vdsk_scsi_capacity16(struct scsi_xfer *xs)
{
	struct vdsk_softc *sc = xs->sc_link->bus->sb_adapter_softc;
	struct scsi_read_cap_data_16 rcd;

	bzero(&rcd, sizeof(rcd));

	_lto8b(sc->sc_vdisk_size - 1, rcd.addr);
	_lto4b(sc->sc_vdisk_block_size, rcd.length);

	bcopy(&rcd, xs->data, MIN(sizeof(rcd), xs->datalen));

	vdsk_scsi_done(xs, XS_NOERROR);
}

void
vdsk_scsi_done(struct scsi_xfer *xs, int error)
{
	xs->error = error;

	scsi_done(xs);
}

