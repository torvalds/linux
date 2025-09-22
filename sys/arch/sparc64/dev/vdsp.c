/*	$OpenBSD: vdsp.c,v 1.48 2021/10/24 17:05:04 mpi Exp $	*/
/*
 * Copyright (c) 2009, 2011, 2014 Mark Kettenis
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
#include <sys/conf.h>
#include <sys/proc.h>
#include <sys/buf.h>
#include <sys/device.h>
#include <sys/disklabel.h>
#include <sys/fcntl.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/namei.h>
#include <sys/systm.h>
#include <sys/task.h>
#include <sys/vnode.h>
#include <sys/dkio.h>
#include <sys/specdev.h>

#include <machine/autoconf.h>
#include <machine/conf.h>
#include <machine/hypervisor.h>
#include <machine/mdesc.h>

#include <uvm/uvm_extern.h>

#include <scsi/scsi_all.h>
#include <scsi/scsi_disk.h>
#include <scsi/scsiconf.h>

#include <isofs/cd9660/iso.h>

#include <dev/sun/disklabel.h>

#include <sparc64/dev/cbusvar.h>
#include <sparc64/dev/ldcvar.h>
#include <sparc64/dev/viovar.h>

#ifdef VDSP_DEBUG
#define DPRINTF(x)	printf x
#else
#define DPRINTF(x)
#endif

#define VDSK_TX_ENTRIES			64
#define VDSK_RX_ENTRIES			64

#define VDSK_MAX_DESCRIPTORS		1024
#define VDSK_MAX_DESCRIPTOR_SIZE	512

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

/* Sun standard fields. */
struct sun_vtoc_preamble {
	char	sl_text[128];
	u_int	sl_version;	/* label version */
	char	sl_volume[8];	/* short volume name */
	u_short	sl_nparts;	/* partition count */

	struct sun_partinfo sl_part[8];

	u_int	sl_bootinfo[3];
	u_int	sl_sanity;
};

struct vd_vtoc_part {
	uint16_t	id_tag;
	uint16_t	perm;
	uint32_t	reserved;
	uint64_t	start;
	uint64_t	nblocks;
	
};
struct vd_vtoc {
	uint8_t		volume_name[8];
	uint16_t	sector_size;
	uint16_t	num_partitions;
	uint32_t	reserved;
	uint8_t		ascii_label[128];
	struct vd_vtoc_part partition[8];
};

struct vd_diskgeom {
	uint16_t	ncyl;
	uint16_t	acyl;
	uint16_t	bcyl;
	uint16_t	nhead;
	uint16_t	nsect;
	uint16_t	intrlv;
	uint16_t	apc;
	uint16_t	rpm;
	uint16_t	pcyl;
	uint16_t	write_reinstruct;
	uint16_t	read_reinstruct;
};

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
	struct ldc_cookie	cookie[1];
};

#define VD_SLICE_NONE		0xff

struct vdsk_desc_msg {
	struct vio_msg_tag	tag;
	uint64_t		seq_no;
	uint64_t		desc_handle;
	uint64_t		req_id;
	uint8_t			operation;
	uint8_t			slice;
	uint16_t		_reserved1;
	uint32_t		status;
	uint64_t		offset;
	uint64_t		size;
	uint32_t		ncookies;
	uint32_t		_reserved2;
	struct ldc_cookie	cookie[1];
};

/*
 * We support vDisk 1.1.
 */
#define VDSK_MAJOR	1
#define VDSK_MINOR	1

/*
 * But we only support a subset of the defined commands.
 */
#define VD_OP_MASK \
    ((1 << VD_OP_BREAD) | (1 << VD_OP_BWRITE) | (1 << VD_OP_FLUSH) | \
     (1 << VD_OP_GET_WCE) | (1 << VD_OP_SET_WCE) | \
     (1 << VD_OP_GET_VTOC) | (1 << VD_OP_SET_VTOC) | \
     (1 << VD_OP_GET_DISKGEOM))

struct vdsp_softc {
	struct device	sc_dv;
	int		sc_idx;
	bus_space_tag_t	sc_bustag;
	bus_dma_tag_t	sc_dmatag;

	uint64_t	sc_tx_ino;
	uint64_t	sc_rx_ino;
	void		*sc_tx_ih;
	void		*sc_rx_ih;

	struct ldc_conn	sc_lc;

	uint16_t	sc_vio_state;
#define VIO_SND_VER_INFO	0x0001
#define VIO_ACK_VER_INFO	0x0002
#define VIO_RCV_VER_INFO	0x0004
#define VIO_SND_ATTR_INFO	0x0008
#define VIO_ACK_ATTR_INFO	0x0010
#define VIO_RCV_ATTR_INFO	0x0020
#define VIO_SND_DRING_REG	0x0040
#define VIO_ACK_DRING_REG	0x0080
#define VIO_RCV_DRING_REG	0x0100
#define VIO_SND_RDX		0x0200
#define VIO_ACK_RDX		0x0400
#define VIO_RCV_RDX		0x0800

	uint16_t	sc_major;
	uint16_t	sc_minor;

	uint8_t		sc_xfer_mode;

	uint32_t	sc_local_sid;
	uint64_t	sc_seq_no;

	uint64_t	sc_dring_ident;
	uint32_t	sc_num_descriptors;
	uint32_t	sc_descriptor_size;
	struct ldc_cookie sc_dring_cookie;

	struct task	sc_open_task;
	struct task	sc_alloc_task;
	struct task	sc_close_task;

	struct mutex	sc_desc_mtx;
	struct vdsk_desc_msg *sc_desc_msg[VDSK_RX_ENTRIES];
	int		sc_desc_head;
	int		sc_desc_tail;

	struct task	sc_read_task;

	caddr_t		sc_vd;
	struct task	sc_vd_task;
	struct vd_desc	**sc_vd_ring;
	u_int		sc_vd_prod;
	u_int		sc_vd_cons;

	uint32_t	sc_vdisk_block_size;
	uint64_t	sc_vdisk_size;

	struct vnode	*sc_vp;

	struct sun_disklabel *sc_label;
	uint16_t	sc_ncyl;
	uint16_t	sc_acyl;
	uint16_t	sc_nhead;
	uint16_t	sc_nsect;
};

int	vdsp_match(struct device *, void *, void *);
void	vdsp_attach(struct device *, struct device *, void *);

const struct cfattach vdsp_ca = {
	sizeof(struct vdsp_softc), vdsp_match, vdsp_attach
};

struct cfdriver vdsp_cd = {
	NULL, "vdsp", DV_DULL
};

int	vdsp_tx_intr(void *);
int	vdsp_rx_intr(void *);

void	vdsp_rx_data(struct ldc_conn *, struct ldc_pkt *);
void	vdsp_rx_vio_ctrl(struct vdsp_softc *, struct vio_msg *);
void	vdsp_rx_vio_ver_info(struct vdsp_softc *, struct vio_msg_tag *);
void	vdsp_rx_vio_attr_info(struct vdsp_softc *, struct vio_msg_tag *);
void	vdsp_rx_vio_dring_reg(struct vdsp_softc *, struct vio_msg_tag *);
void	vdsp_rx_vio_rdx(struct vdsp_softc *sc, struct vio_msg_tag *);
void	vdsp_rx_vio_data(struct vdsp_softc *sc, struct vio_msg *);
void	vdsp_rx_vio_dring_data(struct vdsp_softc *sc,
	    struct vio_msg_tag *);
void	vdsp_rx_vio_desc_data(struct vdsp_softc *sc, struct vio_msg_tag *);

void	vdsp_ldc_reset(struct ldc_conn *);
void	vdsp_ldc_start(struct ldc_conn *);

void	vdsp_sendmsg(struct vdsp_softc *, void *, size_t, int dowait);

void	vdsp_open(void *);
void	vdsp_close(void *);
void	vdsp_alloc(void *);
void	vdsp_readlabel(struct vdsp_softc *);
int	vdsp_writelabel(struct vdsp_softc *);
int	vdsp_is_iso(struct vdsp_softc *);
void	vdsp_read(void *);
void	vdsp_read_desc(struct vdsp_softc *, struct vdsk_desc_msg *);
void	vdsp_vd_task(void *);
void	vdsp_read_dring(void *, void *);
void	vdsp_write_dring(void *, void *);
void	vdsp_flush_dring(void *, void *);
void	vdsp_get_vtoc(void *, void *);
void	vdsp_set_vtoc(void *, void *);
void	vdsp_get_diskgeom(void *, void *);
void	vdsp_unimp(void *, void *);

void	vdsp_ack_desc(struct vdsp_softc *, struct vd_desc *);

int
vdsp_match(struct device *parent, void *match, void *aux)
{
	struct cbus_attach_args *ca = aux;

	if (strcmp(ca->ca_name, "vds-port") == 0)
		return (1);

	return (0);
}

void
vdsp_attach(struct device *parent, struct device *self, void *aux)
{
	struct vdsp_softc *sc = (struct vdsp_softc *)self;
	struct cbus_attach_args *ca = aux;
	struct ldc_conn *lc;

	sc->sc_idx = ca->ca_idx;
	sc->sc_bustag = ca->ca_bustag;
	sc->sc_dmatag = ca->ca_dmatag;
	sc->sc_tx_ino = ca->ca_tx_ino;
	sc->sc_rx_ino = ca->ca_rx_ino;

	printf(": ivec 0x%llx, 0x%llx", sc->sc_tx_ino, sc->sc_rx_ino);

	mtx_init(&sc->sc_desc_mtx, IPL_BIO);

	/*
	 * Un-configure queues before registering interrupt handlers,
	 * such that we dont get any stale LDC packets or events.
	 */
	hv_ldc_tx_qconf(ca->ca_id, 0, 0);
	hv_ldc_rx_qconf(ca->ca_id, 0, 0);

	sc->sc_tx_ih = bus_intr_establish(ca->ca_bustag, sc->sc_tx_ino,
	    IPL_BIO, BUS_INTR_ESTABLISH_MPSAFE, vdsp_tx_intr, sc,
	    sc->sc_dv.dv_xname);
	sc->sc_rx_ih = bus_intr_establish(ca->ca_bustag, sc->sc_rx_ino,
	    IPL_BIO, BUS_INTR_ESTABLISH_MPSAFE, vdsp_rx_intr, sc,
	    sc->sc_dv.dv_xname);
	if (sc->sc_tx_ih == NULL || sc->sc_rx_ih == NULL) {
		printf(", can't establish interrupt\n");
		return;
	}

	lc = &sc->sc_lc;
	lc->lc_id = ca->ca_id;
	lc->lc_sc = sc;
	lc->lc_reset = vdsp_ldc_reset;
	lc->lc_start = vdsp_ldc_start;
	lc->lc_rx_data = vdsp_rx_data;

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

	task_set(&sc->sc_open_task, vdsp_open, sc);
	task_set(&sc->sc_alloc_task, vdsp_alloc, sc);
	task_set(&sc->sc_close_task, vdsp_close, sc);
	task_set(&sc->sc_read_task, vdsp_read, sc);

	printf("\n");

	return;

#if 0
free_rxqueue:
	ldc_queue_free(sc->sc_dmatag, lc->lc_rxq);
#endif
free_txqueue:
	ldc_queue_free(sc->sc_dmatag, lc->lc_txq);
}

int
vdsp_tx_intr(void *arg)
{
	struct vdsp_softc *sc = arg;
	struct ldc_conn *lc = &sc->sc_lc;
	uint64_t tx_head, tx_tail, tx_state;
	int err;

	err = hv_ldc_tx_get_state(lc->lc_id, &tx_head, &tx_tail, &tx_state);
	if (err != H_EOK) {
		printf("hv_ldc_rx_get_state %d\n", err);
		return (0);
	}

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

	wakeup(lc->lc_txq);
	return (1);
}

int
vdsp_rx_intr(void *arg)
{
	struct vdsp_softc *sc = arg;
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
		switch (rx_state) {
		case LDC_CHANNEL_DOWN:
			DPRINTF(("%s: Rx link down\n", __func__));
			lc->lc_tx_seqid = 0;
			lc->lc_state = 0;
			lc->lc_reset(lc);
			break;
		case LDC_CHANNEL_UP:
			DPRINTF(("%s: Rx link up\n", __func__));
			break;
		case LDC_CHANNEL_RESET:
			DPRINTF(("%s: Rx link reset\n", __func__));
			lc->lc_tx_seqid = 0;
			lc->lc_state = 0;
			lc->lc_reset(lc);
			break;
		}
		lc->lc_rx_state = rx_state;
		return (1);
	}

	if (lc->lc_rx_state == LDC_CHANNEL_DOWN)
		return (1);

	lp = (struct ldc_pkt *)(lc->lc_rxq->lq_va + rx_head);
	switch (lp->type) {
	case LDC_CTRL:
		ldc_rx_ctrl(lc, lp);
		break;

	case LDC_DATA:
		ldc_rx_data(lc, lp);
		break;

	default:
		DPRINTF(("0x%02x/0x%02x/0x%02x\n", lp->type, lp->stype,
		    lp->ctrl));
		ldc_reset(lc);
		break;
	}

	rx_head += sizeof(*lp);
	rx_head &= ((lc->lc_rxq->lq_nentries * sizeof(*lp)) - 1);
	err = hv_ldc_rx_set_qhead(lc->lc_id, rx_head);
	if (err != H_EOK)
		printf("%s: hv_ldc_rx_set_qhead %d\n", __func__, err);

	return (1);
}

void
vdsp_rx_data(struct ldc_conn *lc, struct ldc_pkt *lp)
{
	struct vio_msg *vm = (struct vio_msg *)lp;

	switch (vm->type) {
	case VIO_TYPE_CTRL:
		if ((lp->env & LDC_FRAG_START) == 0 &&
		    (lp->env & LDC_FRAG_STOP) == 0)
			return;
		vdsp_rx_vio_ctrl(lc->lc_sc, vm);
		break;

	case VIO_TYPE_DATA:
		if((lp->env & LDC_FRAG_START) == 0)
			return;
		vdsp_rx_vio_data(lc->lc_sc, vm);
		break;

	default:
		DPRINTF(("Unhandled packet type 0x%02x\n", vm->type));
		ldc_reset(lc);
		break;
	}
}

void
vdsp_rx_vio_ctrl(struct vdsp_softc *sc, struct vio_msg *vm)
{
	struct vio_msg_tag *tag = (struct vio_msg_tag *)&vm->type;

	switch (tag->stype_env) {
	case VIO_VER_INFO:
		vdsp_rx_vio_ver_info(sc, tag);
		break;
	case VIO_ATTR_INFO:
		vdsp_rx_vio_attr_info(sc, tag);
		break;
	case VIO_DRING_REG:
		vdsp_rx_vio_dring_reg(sc, tag);
		break;
	case VIO_RDX:
		vdsp_rx_vio_rdx(sc, tag);
		break;
	default:
		DPRINTF(("CTRL/0x%02x/0x%04x\n", tag->stype, tag->stype_env));
		break;
	}
}

void
vdsp_rx_vio_ver_info(struct vdsp_softc *sc, struct vio_msg_tag *tag)
{
	struct vio_ver_info *vi = (struct vio_ver_info *)tag;

	switch (vi->tag.stype) {
	case VIO_SUBTYPE_INFO:
		DPRINTF(("CTRL/INFO/VER_INFO\n"));

		/* Make sure we're talking to a virtual disk. */
		if (vi->dev_class != VDEV_DISK) {
			/* Huh, we're not talking to a disk device? */
			printf("%s: peer is not a disk device\n",
			    sc->sc_dv.dv_xname);
			vi->tag.stype = VIO_SUBTYPE_NACK;
			vi->major = 0;
			vdsp_sendmsg(sc, vi, sizeof(*vi), 0);
			return;
		}

		if (vi->major != VDSK_MAJOR) {
			vi->tag.stype = VIO_SUBTYPE_NACK;
			vi->major = VDSK_MAJOR;
			vi->minor = VDSK_MINOR;
			vdsp_sendmsg(sc, vi, sizeof(*vi), 0);
			return;
		}

		sc->sc_major = vi->major;
		sc->sc_minor = vi->minor;
		sc->sc_local_sid = vi->tag.sid;

		vi->tag.stype = VIO_SUBTYPE_ACK;
		if (vi->minor > VDSK_MINOR)
			vi->minor = VDSK_MINOR;
		vi->dev_class = VDEV_DISK_SERVER;
		vdsp_sendmsg(sc, vi, sizeof(*vi), 0);
		sc->sc_vio_state |= VIO_RCV_VER_INFO;
		break;

	case VIO_SUBTYPE_ACK:
		DPRINTF(("CTRL/ACK/VER_INFO\n"));
		break;

	default:
		DPRINTF(("CTRL/0x%02x/VER_INFO\n", vi->tag.stype));
		break;
	}
}

void
vdsp_rx_vio_attr_info(struct vdsp_softc *sc, struct vio_msg_tag *tag)
{
	struct vd_attr_info *ai = (struct vd_attr_info *)tag;

	switch (ai->tag.stype) {
	case VIO_SUBTYPE_INFO:
		DPRINTF(("CTRL/INFO/ATTR_INFO\n"));

		if (ai->xfer_mode != VIO_DESC_MODE &&
		    ai->xfer_mode != VIO_DRING_MODE) {
			printf("%s: peer uses unsupported xfer mode 0x%02x\n",
			    sc->sc_dv.dv_xname, ai->xfer_mode);
			ai->tag.stype = VIO_SUBTYPE_NACK;
			vdsp_sendmsg(sc, ai, sizeof(*ai), 0);
			return;
		}
		sc->sc_xfer_mode = ai->xfer_mode;
		sc->sc_vio_state |= VIO_RCV_ATTR_INFO;

		task_add(systq, &sc->sc_open_task);
		break;

	case VIO_SUBTYPE_ACK:
		DPRINTF(("CTRL/ACK/ATTR_INFO\n"));
		break;

	default:
		DPRINTF(("CTRL/0x%02x/ATTR_INFO\n", ai->tag.stype));
		break;
	}
}

void
vdsp_rx_vio_dring_reg(struct vdsp_softc *sc, struct vio_msg_tag *tag)
{
	struct vio_dring_reg *dr = (struct vio_dring_reg *)tag;

	switch (dr->tag.stype) {
	case VIO_SUBTYPE_INFO:
		DPRINTF(("CTRL/INFO/DRING_REG\n"));

		if (dr->num_descriptors > VDSK_MAX_DESCRIPTORS ||
		    dr->descriptor_size > VDSK_MAX_DESCRIPTOR_SIZE ||
		    dr->ncookies > 1) {
			dr->tag.stype = VIO_SUBTYPE_NACK;
			vdsp_sendmsg(sc, dr, sizeof(*dr), 0);
			return;
		}
		sc->sc_num_descriptors = dr->num_descriptors;
		sc->sc_descriptor_size = dr->descriptor_size;
		sc->sc_dring_cookie = dr->cookie[0];
		sc->sc_vio_state |= VIO_RCV_DRING_REG;

		task_add(systq, &sc->sc_alloc_task);
		break;

	case VIO_SUBTYPE_ACK:
		DPRINTF(("CTRL/ACK/DRING_REG\n"));
		break;

	default:
		DPRINTF(("CTRL/0x%02x/DRING_REG\n", dr->tag.stype));
		break;
	}
}

void
vdsp_rx_vio_rdx(struct vdsp_softc *sc, struct vio_msg_tag *tag)
{
	switch(tag->stype) {
	case VIO_SUBTYPE_INFO:
		DPRINTF(("CTRL/INFO/RDX\n"));

		tag->stype = VIO_SUBTYPE_ACK;
		tag->sid = sc->sc_local_sid;
		vdsp_sendmsg(sc, tag, sizeof(*tag), 0);
		sc->sc_vio_state |= VIO_RCV_RDX;
		break;

	case VIO_SUBTYPE_ACK:
		DPRINTF(("CTRL/ACK/RDX\n"));
		break;

	default:
		DPRINTF(("CTRL/0x%02x/RDX (VIO)\n", tag->stype));
		break;
	}
}

void
vdsp_rx_vio_data(struct vdsp_softc *sc, struct vio_msg *vm)
{
	struct vio_msg_tag *tag = (struct vio_msg_tag *)&vm->type;

	if (!ISSET(sc->sc_vio_state, VIO_RCV_RDX)) {
		DPRINTF(("Spurious DATA/0x%02x/0x%04x\n", tag->stype,
		    tag->stype_env));
		return;
	}

	switch(tag->stype_env) {
	case VIO_DESC_DATA:
		vdsp_rx_vio_desc_data(sc, tag);
		break;

	case VIO_DRING_DATA:
		vdsp_rx_vio_dring_data(sc, tag);
		break;

	default:
		DPRINTF(("DATA/0x%02x/0x%04x\n", tag->stype, tag->stype_env));
		break;
	}
}

void
vdsp_rx_vio_dring_data(struct vdsp_softc *sc, struct vio_msg_tag *tag)
{
	struct vio_dring_msg *dm = (struct vio_dring_msg *)tag;
	struct vd_desc *vd;
	vaddr_t va;
	paddr_t pa;
	uint64_t size, off;
	psize_t nbytes;
	int err;

	switch(tag->stype) {
	case VIO_SUBTYPE_INFO:
		DPRINTF(("DATA/INFO/DRING_DATA\n"));

		if (dm->dring_ident != sc->sc_dring_ident ||
		    dm->start_idx >= sc->sc_num_descriptors) {
			dm->tag.stype = VIO_SUBTYPE_NACK;
			vdsp_sendmsg(sc, dm, sizeof(*dm), 0);
			return;
		}

		off = dm->start_idx * sc->sc_descriptor_size;
		vd = (struct vd_desc *)(sc->sc_vd + off);
		va = (vaddr_t)vd;
		size = sc->sc_descriptor_size;
		while (size > 0) {
			pmap_extract(pmap_kernel(), va, &pa);
			nbytes = MIN(size, PAGE_SIZE - (off & PAGE_MASK));
			err = hv_ldc_copy(sc->sc_lc.lc_id, LDC_COPY_IN,
			    sc->sc_dring_cookie.addr + off, pa,
			    nbytes, &nbytes);
			if (err != H_EOK) {
				printf("%s: hv_ldc_copy %d\n", __func__, err);
				return;
			}
			va += nbytes;
			size -= nbytes;
			off += nbytes;
		}

		sc->sc_vd_ring[sc->sc_vd_prod % sc->sc_num_descriptors] = vd;
		membar_producer();
		sc->sc_vd_prod++;
		task_add(systq, &sc->sc_vd_task);

		break;

	case VIO_SUBTYPE_ACK:
		DPRINTF(("DATA/ACK/DRING_DATA\n"));
		break;

	case VIO_SUBTYPE_NACK:
		DPRINTF(("DATA/NACK/DRING_DATA\n"));
		break;

	default:
		DPRINTF(("DATA/0x%02x/DRING_DATA\n", tag->stype));
		break;
	}
}

void
vdsp_vd_task(void *xsc)
{
	struct vdsp_softc *sc = xsc;
	struct vd_desc *vd;

	while (sc->sc_vd_cons != sc->sc_vd_prod) {
		membar_consumer();
		vd = sc->sc_vd_ring[sc->sc_vd_cons++ % sc->sc_num_descriptors];
	
		DPRINTF(("%s: operation %x\n", sc->sc_dv.dv_xname,
		    vd->operation));
		switch (vd->operation) {
		case VD_OP_BREAD:
			vdsp_read_dring(sc, vd);
			break;
		case VD_OP_BWRITE:
			vdsp_write_dring(sc, vd);
			break;
		case VD_OP_FLUSH:
			vdsp_flush_dring(sc, vd);
			break;
		case VD_OP_GET_VTOC:
			vdsp_get_vtoc(sc, vd);
			break;
		case VD_OP_SET_VTOC:
			vdsp_set_vtoc(sc, vd);
			break;
		case VD_OP_GET_DISKGEOM:
			vdsp_get_diskgeom(sc, vd);
			break;
		case VD_OP_GET_WCE:
		case VD_OP_SET_WCE:
		case VD_OP_GET_DEVID:
			/*
			 * Solaris issues VD_OP_GET_DEVID despite the
			 * fact that we don't advertise it.  It seems
			 * to be able to handle failure just fine, so
			 * we silently ignore it.
			 */
			vdsp_unimp(sc, vd);
			break;
		default:
			printf("%s: unsupported operation 0x%02x\n",
			    sc->sc_dv.dv_xname, vd->operation);
			vdsp_unimp(sc, vd);
			break;
		}
	}
}

void
vdsp_rx_vio_desc_data(struct vdsp_softc *sc, struct vio_msg_tag *tag)
{
	struct vdsk_desc_msg *dm = (struct vdsk_desc_msg *)tag;

	switch(tag->stype) {
	case VIO_SUBTYPE_INFO:
		DPRINTF(("DATA/INFO/DESC_DATA\n"));

		switch (dm->operation) {
		case VD_OP_BREAD:
			mtx_enter(&sc->sc_desc_mtx);
			sc->sc_desc_msg[sc->sc_desc_head++] = dm;
			sc->sc_desc_head &= (VDSK_RX_ENTRIES - 1);
			KASSERT(sc->sc_desc_head != sc->sc_desc_tail);
			mtx_leave(&sc->sc_desc_mtx);
			task_add(systq, &sc->sc_read_task);
			break;
		default:
			printf("%s: unsupported operation 0x%02x\n",
			    sc->sc_dv.dv_xname, dm->operation);
			break;
		}
		break;

	case VIO_SUBTYPE_ACK:
		DPRINTF(("DATA/ACK/DESC_DATA\n"));
		break;

	case VIO_SUBTYPE_NACK:
		DPRINTF(("DATA/NACK/DESC_DATA\n"));
		break;

	default:
		DPRINTF(("DATA/0x%02x/DESC_DATA\n", tag->stype));
		break;
	}
}

void
vdsp_ldc_reset(struct ldc_conn *lc)
{
	struct vdsp_softc *sc = lc->lc_sc;

	sc->sc_vio_state = 0;
	task_add(systq, &sc->sc_close_task);
}

void
vdsp_ldc_start(struct ldc_conn *lc)
{
	/* The vDisk client is supposed to initiate the handshake. */
}

void
vdsp_sendmsg(struct vdsp_softc *sc, void *msg, size_t len, int dowait)
{
	struct ldc_conn *lc = &sc->sc_lc;
	int err;

	do {
		err = ldc_send_unreliable(lc, msg, len);
		if (dowait && err == EWOULDBLOCK) {
			/*
			 * Seems like the hypervisor doesn't actually
			 * generate interrupts for transmit queues, so
			 * we specify a timeout such that we don't
			 * block forever.
			 */
			err = tsleep_nsec(lc->lc_txq, PWAIT, "vdsp",
			    MSEC_TO_NSEC(10));
		}
	} while (dowait && err == EWOULDBLOCK);
}

void
vdsp_open(void *arg1)
{
	struct vdsp_softc *sc = arg1;
	struct proc *p = curproc;
	struct vd_attr_info ai;

	if (sc->sc_vp == NULL) {
		struct nameidata nd;
		struct vattr va;
		struct partinfo pi;
		const char *name;
		dev_t dev;
		int error;

		name = mdesc_get_prop_str(sc->sc_idx, "vds-block-device");
		if (name == NULL)
			return;

		NDINIT(&nd, 0, 0, UIO_SYSSPACE, name, p);
		error = vn_open(&nd, FREAD | FWRITE, 0);
		if (error) {
			printf("VOP_OPEN: %s, %d\n", name, error);
			return;
		}

		if (nd.ni_vp->v_type == VBLK) {
			dev = nd.ni_vp->v_rdev;
			error = (*bdevsw[major(dev)].d_ioctl)(dev,
			    DIOCGPART, (caddr_t)&pi, FREAD, curproc);
			if (error)
				printf("DIOCGPART: %s, %d\n", name, error);
			sc->sc_vdisk_block_size = pi.disklab->d_secsize;
			sc->sc_vdisk_size = DL_GETPSIZE(pi.part);
		} else {
			error = VOP_GETATTR(nd.ni_vp, &va, p->p_ucred, p);
			if (error)
				printf("VOP_GETATTR: %s, %d\n", name, error);
			sc->sc_vdisk_block_size = DEV_BSIZE;
			sc->sc_vdisk_size = va.va_size / DEV_BSIZE;
		}

		VOP_UNLOCK(nd.ni_vp);
		sc->sc_vp = nd.ni_vp;

		vdsp_readlabel(sc);
	}

	bzero(&ai, sizeof(ai));
	ai.tag.type = VIO_TYPE_CTRL;
	ai.tag.stype = VIO_SUBTYPE_ACK;
	ai.tag.stype_env = VIO_ATTR_INFO;
	ai.tag.sid = sc->sc_local_sid;
	ai.xfer_mode = sc->sc_xfer_mode;
	ai.vd_type = VD_DISK_TYPE_DISK;
	if (sc->sc_major > 1 || sc->sc_minor >= 1) {
		if (vdsp_is_iso(sc))
			ai.vd_mtype = VD_MEDIA_TYPE_CD;
		else
			ai.vd_mtype = VD_MEDIA_TYPE_FIXED;
	}
	ai.vdisk_block_size = sc->sc_vdisk_block_size;
	ai.operations = VD_OP_MASK;
	ai.vdisk_size = sc->sc_vdisk_size;
	ai.max_xfer_sz = MAXPHYS / sc->sc_vdisk_block_size;
	vdsp_sendmsg(sc, &ai, sizeof(ai), 1);
}

void
vdsp_close(void *arg1)
{
	struct vdsp_softc *sc = arg1;
	struct proc *p = curproc;

	sc->sc_seq_no = 0;

	free(sc->sc_vd, M_DEVBUF, 0);
	sc->sc_vd = NULL;
	free(sc->sc_vd_ring, M_DEVBUF,
	     sc->sc_num_descriptors * sizeof(*sc->sc_vd_ring));
	sc->sc_vd_ring = NULL;
	free(sc->sc_label, M_DEVBUF, 0);
	sc->sc_label = NULL;
	if (sc->sc_vp) {
		vn_close(sc->sc_vp, FREAD | FWRITE, p->p_ucred, p);
		sc->sc_vp = NULL;
	}
}

void
vdsp_readlabel(struct vdsp_softc *sc)
{
	struct proc *p = curproc;
	struct iovec iov;
	struct uio uio;
	int err;

	if (sc->sc_vp == NULL)
		return;

	sc->sc_label = malloc(sizeof(*sc->sc_label), M_DEVBUF, M_WAITOK);

	iov.iov_base = sc->sc_label;
	iov.iov_len = sizeof(*sc->sc_label);
	uio.uio_iov = &iov;
	uio.uio_iovcnt = 1;
	uio.uio_offset = 0;
	uio.uio_resid = sizeof(*sc->sc_label);
	uio.uio_segflg = UIO_SYSSPACE;
	uio.uio_rw = UIO_READ;
	uio.uio_procp = p;

	vn_lock(sc->sc_vp, LK_EXCLUSIVE | LK_RETRY);
	err = VOP_READ(sc->sc_vp, &uio, 0, p->p_ucred);
	VOP_UNLOCK(sc->sc_vp);
	if (err) {
		free(sc->sc_label, M_DEVBUF, 0);
		sc->sc_label = NULL;
	}
}

int
vdsp_writelabel(struct vdsp_softc *sc)
{
	struct proc *p = curproc;
	struct iovec iov;
	struct uio uio;
	int err;

	if (sc->sc_vp == NULL || sc->sc_label == NULL)
		return (EINVAL);

	iov.iov_base = sc->sc_label;
	iov.iov_len = sizeof(*sc->sc_label);
	uio.uio_iov = &iov;
	uio.uio_iovcnt = 1;
	uio.uio_offset = 0;
	uio.uio_resid = sizeof(*sc->sc_label);
	uio.uio_segflg = UIO_SYSSPACE;
	uio.uio_rw = UIO_WRITE;
	uio.uio_procp = p;

	vn_lock(sc->sc_vp, LK_EXCLUSIVE | LK_RETRY);
	err = VOP_WRITE(sc->sc_vp, &uio, 0, p->p_ucred);
	VOP_UNLOCK(sc->sc_vp);

	return (err);
}

int
vdsp_is_iso(struct vdsp_softc *sc)
{
	struct proc *p = curproc;
	struct iovec iov;
	struct uio uio;
	struct iso_volume_descriptor *vdp;
	int err;

	if (sc->sc_vp == NULL)
		return (0);

	vdp = malloc(sizeof(*vdp), M_DEVBUF, M_WAITOK);

	iov.iov_base = vdp;
	iov.iov_len = sizeof(*vdp);
	uio.uio_iov = &iov;
	uio.uio_iovcnt = 1;
	uio.uio_offset = 16 * ISO_DEFAULT_BLOCK_SIZE;
	uio.uio_resid = sizeof(*vdp);
	uio.uio_segflg = UIO_SYSSPACE;
	uio.uio_rw = UIO_READ;
	uio.uio_procp = p;

	vn_lock(sc->sc_vp, LK_EXCLUSIVE | LK_RETRY);
	err = VOP_READ(sc->sc_vp, &uio, 0, p->p_ucred);
	VOP_UNLOCK(sc->sc_vp);

	if (err == 0 && memcmp(vdp->id, ISO_STANDARD_ID, sizeof(vdp->id)))
		err = ENOENT;

	free(vdp, M_DEVBUF, 0);
	return (err == 0);
}

void
vdsp_alloc(void *arg1)
{
	struct vdsp_softc *sc = arg1;
	struct vio_dring_reg dr;

	KASSERT(sc->sc_num_descriptors <= VDSK_MAX_DESCRIPTORS);
	KASSERT(sc->sc_descriptor_size <= VDSK_MAX_DESCRIPTOR_SIZE);
	sc->sc_vd = mallocarray(sc->sc_num_descriptors,
	    sc->sc_descriptor_size, M_DEVBUF, M_WAITOK);
	sc->sc_vd_ring = mallocarray(sc->sc_num_descriptors,
	    sizeof(*sc->sc_vd_ring), M_DEVBUF, M_WAITOK);
	task_set(&sc->sc_vd_task, vdsp_vd_task, sc);

	bzero(&dr, sizeof(dr));
	dr.tag.type = VIO_TYPE_CTRL;
	dr.tag.stype = VIO_SUBTYPE_ACK;
	dr.tag.stype_env = VIO_DRING_REG;
	dr.tag.sid = sc->sc_local_sid;
	dr.dring_ident = ++sc->sc_dring_ident;
	vdsp_sendmsg(sc, &dr, sizeof(dr), 1);
}

void
vdsp_read(void *arg1)
{
	struct vdsp_softc *sc = arg1;

	mtx_enter(&sc->sc_desc_mtx);
	while (sc->sc_desc_tail != sc->sc_desc_head) {
		mtx_leave(&sc->sc_desc_mtx);
		vdsp_read_desc(sc, sc->sc_desc_msg[sc->sc_desc_tail]);
		mtx_enter(&sc->sc_desc_mtx);
		sc->sc_desc_tail++;
		sc->sc_desc_tail &= (VDSK_RX_ENTRIES - 1);
	}
	mtx_leave(&sc->sc_desc_mtx);
}

void
vdsp_read_desc(struct vdsp_softc *sc, struct vdsk_desc_msg *dm)
{
	struct ldc_conn *lc = &sc->sc_lc;
	struct proc *p = curproc;
	struct iovec iov;
	struct uio uio;
	caddr_t buf;
	vaddr_t va;
	paddr_t pa;
	uint64_t size, off;
	psize_t nbytes;
	int err, i;

	if (sc->sc_vp == NULL)
		return;

	buf = malloc(dm->size, M_DEVBUF, M_WAITOK);

	iov.iov_base = buf;
	iov.iov_len = dm->size;
	uio.uio_iov = &iov;
	uio.uio_iovcnt = 1;
	uio.uio_offset = dm->offset * DEV_BSIZE;
	uio.uio_resid = dm->size;
	uio.uio_segflg = UIO_SYSSPACE;
	uio.uio_rw = UIO_READ;
	uio.uio_procp = p;

	vn_lock(sc->sc_vp, LK_EXCLUSIVE | LK_RETRY);
	dm->status = VOP_READ(sc->sc_vp, &uio, 0, p->p_ucred);
	VOP_UNLOCK(sc->sc_vp);

	KERNEL_UNLOCK();
	if (dm->status == 0) {
		i = 0;
		va = (vaddr_t)buf;
		size = dm->size;
		off = 0;
		while (size > 0 && i < dm->ncookies) {
			pmap_extract(pmap_kernel(), va, &pa);
			nbytes = MIN(size, dm->cookie[i].size - off);
			nbytes = MIN(nbytes, PAGE_SIZE - (off & PAGE_MASK));
			err = hv_ldc_copy(lc->lc_id, LDC_COPY_OUT,
			    dm->cookie[i].addr + off, pa, nbytes, &nbytes);
			if (err != H_EOK) {
				printf("%s: hv_ldc_copy: %d\n", __func__, err);
				dm->status = EIO;
				KERNEL_LOCK();
				goto fail;
			}
			va += nbytes;
			size -= nbytes;
			off += nbytes;
			if (off >= dm->cookie[i].size) {
				off = 0;
				i++;
			}
		}
	}
	KERNEL_LOCK();

fail:
	free(buf, M_DEVBUF, 0);

	/* ACK the descriptor. */
	dm->tag.stype = VIO_SUBTYPE_ACK;
	dm->tag.sid = sc->sc_local_sid;
	vdsp_sendmsg(sc, dm, sizeof(*dm) +
	    (dm->ncookies - 1) * sizeof(struct ldc_cookie), 1);
}

void
vdsp_read_dring(void *arg1, void *arg2)
{
	struct vdsp_softc *sc = arg1;
	struct ldc_conn *lc = &sc->sc_lc;
	struct vd_desc *vd = arg2;
	struct proc *p = curproc;
	struct iovec iov;
	struct uio uio;
	caddr_t buf;
	vaddr_t va;
	paddr_t pa;
	uint64_t size, off;
	psize_t nbytes;
	int err, i;

	if (sc->sc_vp == NULL)
		return;

	buf = malloc(vd->size, M_DEVBUF, M_WAITOK);

	iov.iov_base = buf;
	iov.iov_len = vd->size;
	uio.uio_iov = &iov;
	uio.uio_iovcnt = 1;
	uio.uio_offset = vd->offset * DEV_BSIZE;
	uio.uio_resid = vd->size;
	uio.uio_segflg = UIO_SYSSPACE;
	uio.uio_rw = UIO_READ;
	uio.uio_procp = p;

	vn_lock(sc->sc_vp, LK_EXCLUSIVE | LK_RETRY);
	vd->status = VOP_READ(sc->sc_vp, &uio, 0, p->p_ucred);
	VOP_UNLOCK(sc->sc_vp);

	KERNEL_UNLOCK();
	if (vd->status == 0) {
		i = 0;
		va = (vaddr_t)buf;
		size = vd->size;
		off = 0;
		while (size > 0 && i < vd->ncookies) {
			pmap_extract(pmap_kernel(), va, &pa);
			nbytes = MIN(size, vd->cookie[i].size - off);
			nbytes = MIN(nbytes, PAGE_SIZE - (off & PAGE_MASK));
			err = hv_ldc_copy(lc->lc_id, LDC_COPY_OUT,
			    vd->cookie[i].addr + off, pa, nbytes, &nbytes);
			if (err != H_EOK) {
				printf("%s: hv_ldc_copy: %d\n", __func__, err);
				vd->status = EIO;
				KERNEL_LOCK();
				goto fail;
			}
			va += nbytes;
			size -= nbytes;
			off += nbytes;
			if (off >= vd->cookie[i].size) {
				off = 0;
				i++;
			}
		}
	}
	KERNEL_LOCK();

fail:
	free(buf, M_DEVBUF, 0);

	/* ACK the descriptor. */
	vd->hdr.dstate = VIO_DESC_DONE;
	vdsp_ack_desc(sc, vd);
}

void
vdsp_write_dring(void *arg1, void *arg2)
{
	struct vdsp_softc *sc = arg1;
	struct ldc_conn *lc = &sc->sc_lc;
	struct vd_desc *vd = arg2;
	struct proc *p = curproc;
	struct iovec iov;
	struct uio uio;
	caddr_t buf;
	vaddr_t va;
	paddr_t pa;
	uint64_t size, off;
	psize_t nbytes;
	int err, i;

	if (sc->sc_vp == NULL)
		return;

	buf = malloc(vd->size, M_DEVBUF, M_WAITOK);

	KERNEL_UNLOCK();
	i = 0;
	va = (vaddr_t)buf;
	size = vd->size;
	off = 0;
	while (size > 0 && i < vd->ncookies) {
		pmap_extract(pmap_kernel(), va, &pa);
		nbytes = MIN(size, vd->cookie[i].size - off);
		nbytes = MIN(nbytes, PAGE_SIZE - (off & PAGE_MASK));
		err = hv_ldc_copy(lc->lc_id, LDC_COPY_IN,
		    vd->cookie[i].addr + off, pa, nbytes, &nbytes);
		if (err != H_EOK) {
			printf("%s: hv_ldc_copy: %d\n", __func__, err);
			vd->status = EIO;
			KERNEL_LOCK();
			goto fail;
		}
		va += nbytes;
		size -= nbytes;
		off += nbytes;
		if (off >= vd->cookie[i].size) {
			off = 0;
			i++;
		}
	}
	KERNEL_LOCK();

	iov.iov_base = buf;
	iov.iov_len = vd->size;
	uio.uio_iov = &iov;
	uio.uio_iovcnt = 1;
	uio.uio_offset = vd->offset * DEV_BSIZE;
	uio.uio_resid = vd->size;
	uio.uio_segflg = UIO_SYSSPACE;
	uio.uio_rw = UIO_WRITE;
	uio.uio_procp = p;

	vn_lock(sc->sc_vp, LK_EXCLUSIVE | LK_RETRY);
	vd->status = VOP_WRITE(sc->sc_vp, &uio, 0, p->p_ucred);
	VOP_UNLOCK(sc->sc_vp);

fail:
	free(buf, M_DEVBUF, 0);

	/* ACK the descriptor. */
	vd->hdr.dstate = VIO_DESC_DONE;
	vdsp_ack_desc(sc, vd);
}

void
vdsp_flush_dring(void *arg1, void *arg2)
{
	struct vdsp_softc *sc = arg1;
	struct vd_desc *vd = arg2;

	if (sc->sc_vp == NULL)
		return;

	/* ACK the descriptor. */
	vd->status = 0;
	vd->hdr.dstate = VIO_DESC_DONE;
	vdsp_ack_desc(sc, vd);
}

void
vdsp_get_vtoc(void *arg1, void *arg2)
{
	struct vdsp_softc *sc = arg1;
	struct ldc_conn *lc = &sc->sc_lc;
	struct vd_desc *vd = arg2;
	struct sun_vtoc_preamble *sl;
	struct vd_vtoc *vt;
	vaddr_t va;
	paddr_t pa;
	uint64_t size, off;
	psize_t nbytes;
	int err, i;

	vt = malloc(PAGE_SIZE, M_DEVBUF, M_WAITOK | M_ZERO);

	if (sc->sc_label == NULL)
		vdsp_readlabel(sc);

	if (sc->sc_label && sc->sc_label->sl_magic == SUN_DKMAGIC) {
		sl = (struct sun_vtoc_preamble *)sc->sc_label;

		memcpy(vt->ascii_label, sl->sl_text, sizeof(sl->sl_text));
		memcpy(vt->volume_name, sl->sl_volume, sizeof(sl->sl_volume));
		vt->sector_size = DEV_BSIZE;
		vt->num_partitions = sl->sl_nparts;
		for (i = 0; i < vt->num_partitions; i++) {
			vt->partition[i].id_tag = sl->sl_part[i].spi_tag;
			vt->partition[i].perm = sl->sl_part[i].spi_flag;
			vt->partition[i].start =
			    sc->sc_label->sl_part[i].sdkp_cyloffset *
				sc->sc_label->sl_ntracks *
				sc->sc_label->sl_nsectors;
			vt->partition[i].nblocks =
			    sc->sc_label->sl_part[i].sdkp_nsectors;
		}
	} else {
		uint64_t disk_size;
		int unit;

		/* Human-readable disk size. */
		disk_size = sc->sc_vdisk_size * sc->sc_vdisk_block_size;
		disk_size >>= 10;
		unit = 'K';
		if (disk_size > (2 << 10)) {
			disk_size >>= 10;
			unit = 'M';
		}
		if (disk_size > (2 << 10)) {
			disk_size >>= 10;
			unit = 'G';
		}

		snprintf(vt->ascii_label, sizeof(vt->ascii_label),
		    "OpenBSD-DiskImage-%lld%cB cyl %d alt %d hd %d sec %d",
		    disk_size, unit, sc->sc_ncyl, sc->sc_acyl,
		    sc->sc_nhead, sc->sc_nsect);
		vt->sector_size = sc->sc_vdisk_block_size;
		vt->num_partitions = 8;
		vt->partition[2].id_tag = SPTAG_WHOLE_DISK;
		vt->partition[2].nblocks =
		    sc->sc_ncyl * sc->sc_nhead * sc->sc_nsect;
	}

	i = 0;
	va = (vaddr_t)vt;
	size = roundup(sizeof(*vt), 64);
	off = 0;
	while (size > 0 && i < vd->ncookies) {
		pmap_extract(pmap_kernel(), va, &pa);
		nbytes = MIN(size, vd->cookie[i].size - off);
		nbytes = MIN(nbytes, PAGE_SIZE - (off & PAGE_MASK));
		err = hv_ldc_copy(lc->lc_id, LDC_COPY_OUT,
		    vd->cookie[i].addr + off, pa, nbytes, &nbytes);
		if (err != H_EOK) {
			printf("%s: hv_ldc_copy: %d\n", __func__, err);
			vd->status = EIO;
			goto fail;
		}
		va += nbytes;
		size -= nbytes;
		off += nbytes;
		if (off >= vd->cookie[i].size) {
			off = 0;
			i++;
		}
	}

	vd->status = 0;

fail:
	free(vt, M_DEVBUF, 0);

	/* ACK the descriptor. */
	vd->hdr.dstate = VIO_DESC_DONE;
	vdsp_ack_desc(sc, vd);
}

void
vdsp_set_vtoc(void *arg1, void *arg2)
{
	struct vdsp_softc *sc = arg1;
	struct ldc_conn *lc = &sc->sc_lc;
	struct vd_desc *vd = arg2;
	struct sun_vtoc_preamble *sl;
	struct vd_vtoc *vt;
	u_short cksum = 0, *sp1, *sp2;
	vaddr_t va;
	paddr_t pa;
	uint64_t size, off;
	psize_t nbytes;
	int err, i;

	vt = malloc(PAGE_SIZE, M_DEVBUF, M_WAITOK | M_ZERO);

	i = 0;
	va = (vaddr_t)vt;
	size = sizeof(*vt);
	off = 0;
	while (size > 0 && i < vd->ncookies) {
		pmap_extract(pmap_kernel(), va, &pa);
		nbytes = MIN(size, vd->cookie[i].size - off);
		nbytes = MIN(nbytes, PAGE_SIZE - (off & PAGE_MASK));
		err = hv_ldc_copy(lc->lc_id, LDC_COPY_IN,
		    vd->cookie[i].addr + off, pa, nbytes, &nbytes);
		if (err != H_EOK) {
			printf("%s: hv_ldc_copy: %d\n", __func__, err);
			vd->status = EIO;
			goto fail;
		}
		va += nbytes;
		size -= nbytes;
		off += nbytes;
		if (off >= vd->cookie[i].size) {
			off = 0;
			i++;
		}
	}

	if (vt->num_partitions > nitems(sc->sc_label->sl_part)) {
		vd->status = EINVAL;
		goto fail;
	}

	if (sc->sc_label == NULL || sc->sc_label->sl_magic != SUN_DKMAGIC) {
		sc->sc_label = malloc(sizeof(*sc->sc_label),
		    M_DEVBUF, M_WAITOK | M_ZERO);

		sc->sc_label->sl_ntracks = sc->sc_nhead;
		sc->sc_label->sl_nsectors = sc->sc_nsect;
		sc->sc_label->sl_ncylinders = sc->sc_ncyl;
		sc->sc_label->sl_acylinders = sc->sc_acyl;
		sc->sc_label->sl_pcylinders = sc->sc_ncyl + sc->sc_acyl;
		sc->sc_label->sl_rpm = 3600;

		sc->sc_label->sl_magic = SUN_DKMAGIC;
	}

	sl = (struct sun_vtoc_preamble *)sc->sc_label;
	memcpy(sl->sl_text, vt->ascii_label, sizeof(sl->sl_text));
	sl->sl_version = 0x01;
	memcpy(sl->sl_volume, vt->volume_name, sizeof(sl->sl_volume));
	sl->sl_nparts = vt->num_partitions;
	for (i = 0; i < vt->num_partitions; i++) {
		sl->sl_part[i].spi_tag = vt->partition[i].id_tag;
		sl->sl_part[i].spi_flag = vt->partition[i].perm;
		sc->sc_label->sl_part[i].sdkp_cyloffset =
		    vt->partition[i].start / (sc->sc_nhead * sc->sc_nsect);
		sc->sc_label->sl_part[i].sdkp_nsectors =
		    vt->partition[i].nblocks;
	}
	sl->sl_sanity = 0x600ddeee;

	/* Compute the checksum. */
	sp1 = (u_short *)sc->sc_label;
	sp2 = (u_short *)(sc->sc_label + 1);
	while (sp1 < sp2)
		cksum ^= *sp1++;
	sc->sc_label->sl_cksum = cksum;

	vd->status = vdsp_writelabel(sc);

fail:
	free(vt, M_DEVBUF, 0);

	/* ACK the descriptor. */
	vd->hdr.dstate = VIO_DESC_DONE;
	vdsp_ack_desc(sc, vd);
}

void
vdsp_get_diskgeom(void *arg1, void *arg2)
{
	struct vdsp_softc *sc = arg1;
	struct ldc_conn *lc = &sc->sc_lc;
	struct vd_desc *vd = arg2;
	struct vd_diskgeom *vg;
	vaddr_t va;
	paddr_t pa;
	uint64_t size, off;
	psize_t nbytes;
	int err, i;

	vg = malloc(PAGE_SIZE, M_DEVBUF, M_WAITOK | M_ZERO);

	if (sc->sc_label == NULL)
		vdsp_readlabel(sc);

	if (sc->sc_label && sc->sc_label->sl_magic == SUN_DKMAGIC) {
		vg->ncyl = sc->sc_label->sl_ncylinders;
		vg->acyl = sc->sc_label->sl_acylinders;
		vg->nhead = sc->sc_label->sl_ntracks;
		vg->nsect = sc->sc_label->sl_nsectors;
		vg->intrlv = sc->sc_label->sl_interleave;
		vg->apc = sc->sc_label->sl_sparespercyl;
		vg->rpm = sc->sc_label->sl_rpm;
		vg->pcyl = sc->sc_label->sl_pcylinders;
	} else {
		uint64_t disk_size, block_size;

		disk_size = sc->sc_vdisk_size * sc->sc_vdisk_block_size;
		block_size = sc->sc_vdisk_block_size;

		if (disk_size >= 8L * 1024 * 1024 * 1024) {
			vg->nhead = 96;
			vg->nsect = 768;
		} else if (disk_size >= 2 *1024 * 1024) {
			vg->nhead = 1;
			vg->nsect = 600;
		} else {
			vg->nhead = 1;
			vg->nsect = 200;
		}

		vg->pcyl = disk_size / (block_size * vg->nhead * vg->nsect);
		if (vg->pcyl == 0)
			vg->pcyl = 1;
		if (vg->pcyl > 2)
			vg->acyl = 2;
		vg->ncyl = vg->pcyl - vg->acyl;

		vg->rpm = 3600;
	}

	sc->sc_ncyl = vg->ncyl;
	sc->sc_acyl = vg->acyl;
	sc->sc_nhead = vg->nhead;
	sc->sc_nsect = vg->nsect;

	i = 0;
	va = (vaddr_t)vg;
	size = roundup(sizeof(*vg), 64);
	off = 0;
	while (size > 0 && i < vd->ncookies) {
		pmap_extract(pmap_kernel(), va, &pa);
		nbytes = MIN(size, vd->cookie[i].size - off);
		nbytes = MIN(nbytes, PAGE_SIZE - (off & PAGE_MASK));
		err = hv_ldc_copy(lc->lc_id, LDC_COPY_OUT,
		    vd->cookie[i].addr + off, pa, nbytes, &nbytes);
		if (err != H_EOK) {
			printf("%s: hv_ldc_copy: %d\n", __func__, err);
			vd->status = EIO;
			goto fail;
		}
		va += nbytes;
		size -= nbytes;
		off += nbytes;
		if (off >= vd->cookie[i].size) {
			off = 0;
			i++;
		}
	}

	vd->status = 0;

fail:
	free(vg, M_DEVBUF, 0);

	/* ACK the descriptor. */
	vd->hdr.dstate = VIO_DESC_DONE;
	vdsp_ack_desc(sc, vd);
}

void
vdsp_unimp(void *arg1, void *arg2)
{
	struct vdsp_softc *sc = arg1;
	struct vd_desc *vd = arg2;

	/* ACK the descriptor. */
	vd->status = ENOTSUP;
	vd->hdr.dstate = VIO_DESC_DONE;
	vdsp_ack_desc(sc, vd);
}

void
vdsp_ack_desc(struct vdsp_softc *sc, struct vd_desc *vd)
{
	struct vio_dring_msg dm;
	vaddr_t va;
	paddr_t pa;
	uint64_t size, off;
	psize_t nbytes;
	int err;

	va = (vaddr_t)vd;
	off = (caddr_t)vd - sc->sc_vd;
	size = sc->sc_descriptor_size;
	while (size > 0) {
		pmap_extract(pmap_kernel(), va, &pa);
		nbytes = MIN(size, PAGE_SIZE - (off & PAGE_MASK));
		err = hv_ldc_copy(sc->sc_lc.lc_id, LDC_COPY_OUT,
		    sc->sc_dring_cookie.addr + off, pa, nbytes, &nbytes);
		if (err != H_EOK) {
			printf("%s: hv_ldc_copy %d\n", __func__, err);
			return;
		}
		va += nbytes;
		size -= nbytes;
		off += nbytes;
	}

	/* ACK the descriptor. */
	bzero(&dm, sizeof(dm));
	dm.tag.type = VIO_TYPE_DATA;
	dm.tag.stype = VIO_SUBTYPE_ACK;
	dm.tag.stype_env = VIO_DRING_DATA;
	dm.tag.sid = sc->sc_local_sid;
	dm.seq_no = ++sc->sc_seq_no;
	dm.dring_ident = sc->sc_dring_ident;
	off = (caddr_t)vd - sc->sc_vd;
	dm.start_idx = off / sc->sc_descriptor_size;
	dm.end_idx = off / sc->sc_descriptor_size;
	vdsp_sendmsg(sc, &dm, sizeof(dm), 1);
}

int
vdspopen(dev_t dev, int flag, int mode, struct proc *p)
{
	struct vdsp_softc *sc;
	struct ldc_conn *lc;
	int unit = minor(dev);
	int err;

	if (unit >= vdsp_cd.cd_ndevs)
		return (ENXIO);
	sc = vdsp_cd.cd_devs[unit];
	if (sc == NULL)
		return (ENXIO);

	lc = &sc->sc_lc;

	err = hv_ldc_tx_qconf(lc->lc_id,
	    lc->lc_txq->lq_map->dm_segs[0].ds_addr, lc->lc_txq->lq_nentries);
	if (err != H_EOK)
		printf("%s: hv_ldc_tx_qconf %d\n", __func__, err);

	err = hv_ldc_rx_qconf(lc->lc_id,
	    lc->lc_rxq->lq_map->dm_segs[0].ds_addr, lc->lc_rxq->lq_nentries);
	if (err != H_EOK)
		printf("%s: hv_ldc_rx_qconf %d\n", __func__, err);

	cbus_intr_setenabled(sc->sc_bustag, sc->sc_tx_ino, INTR_ENABLED);
	cbus_intr_setenabled(sc->sc_bustag, sc->sc_rx_ino, INTR_ENABLED);

	return (0);
}

int
vdspclose(dev_t dev, int flag, int mode, struct proc *p)
{
	struct vdsp_softc *sc;
	int unit = minor(dev);

	if (unit >= vdsp_cd.cd_ndevs)
		return (ENXIO);
	sc = vdsp_cd.cd_devs[unit];
	if (sc == NULL)
		return (ENXIO);

	cbus_intr_setenabled(sc->sc_bustag, sc->sc_tx_ino, INTR_DISABLED);
	cbus_intr_setenabled(sc->sc_bustag, sc->sc_rx_ino, INTR_DISABLED);

	hv_ldc_tx_qconf(sc->sc_lc.lc_id, 0, 0);
	hv_ldc_rx_qconf(sc->sc_lc.lc_id, 0, 0);

	task_add(systq, &sc->sc_close_task);
	return (0);
}

int
vdspioctl(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	struct vdsp_softc *sc;
	int unit = minor(dev);

	if (unit >= vdsp_cd.cd_ndevs)
		return (ENXIO);
	sc = vdsp_cd.cd_devs[unit];
	if (sc == NULL)
		return (ENXIO);

	return (ENOTTY);
}
