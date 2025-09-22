/* $OpenBSD: mfii.c,v 1.91 2024/05/24 06:02:58 jsg Exp $ */

/*
 * Copyright (c) 2012 David Gwynne <dlg@openbsd.org>
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
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/dkio.h>
#include <sys/pool.h>
#include <sys/task.h>
#include <sys/atomic.h>
#include <sys/sensors.h>
#include <sys/rwlock.h>
#include <sys/smr.h>

#include <dev/biovar.h>
#include <dev/pci/pcidevs.h>
#include <dev/pci/pcivar.h>

#include <machine/bus.h>

#include <scsi/scsi_all.h>
#include <scsi/scsi_disk.h>
#include <scsi/scsiconf.h>

#include <dev/ic/mfireg.h>
#include <dev/pci/mpiireg.h>

#define	MFII_BAR		0x14
#define MFII_BAR_35		0x10
#define	MFII_PCI_MEMSIZE	0x2000 /* 8k */

#define MFII_OSTS_INTR_VALID	0x00000009
#define MFII_RPI		0x6c /* reply post host index */
#define MFII_OSP2		0xb4 /* outbound scratch pad 2 */
#define MFII_OSP3		0xb8 /* outbound scratch pad 3 */

#define MFII_REQ_TYPE_SCSI	MPII_REQ_DESCR_SCSI_IO
#define MFII_REQ_TYPE_LDIO	(0x7 << 1)
#define MFII_REQ_TYPE_MFA	(0x1 << 1)
#define MFII_REQ_TYPE_NO_LOCK	(0x2 << 1)
#define MFII_REQ_TYPE_HI_PRI	(0x6 << 1)

#define MFII_REQ_MFA(_a)	htole64((_a) | MFII_REQ_TYPE_MFA)

#define MFII_FUNCTION_PASSTHRU_IO			(0xf0)
#define MFII_FUNCTION_LDIO_REQUEST			(0xf1)

#define MFII_MAX_CHAIN_UNIT	0x00400000
#define MFII_MAX_CHAIN_MASK	0x000003E0
#define MFII_MAX_CHAIN_SHIFT	5

#define MFII_256K_IO		128
#define MFII_1MB_IO		(MFII_256K_IO * 4)

#define MFII_CHAIN_FRAME_MIN	1024

struct mfii_request_descr {
	u_int8_t	flags;
	u_int8_t	msix_index;
	u_int16_t	smid;

	u_int16_t	lmid;
	u_int16_t	dev_handle;
} __packed;

#define MFII_RAID_CTX_IO_TYPE_SYSPD	(0x1 << 4)
#define MFII_RAID_CTX_TYPE_CUDA		(0x2 << 4)

struct mfii_raid_context {
	u_int8_t	type_nseg;
	u_int8_t	_reserved1;
	u_int16_t	timeout_value;

	u_int16_t	reg_lock_flags;
#define MFII_RAID_CTX_RL_FLAGS_SEQNO_EN	(0x08)
#define MFII_RAID_CTX_RL_FLAGS_CPU0	(0x00)
#define MFII_RAID_CTX_RL_FLAGS_CPU1	(0x10)
#define MFII_RAID_CTX_RL_FLAGS_CUDA	(0x80)

#define MFII_RAID_CTX_ROUTING_FLAGS_SQN	(1 << 4)
#define MFII_RAID_CTX_ROUTING_FLAGS_CPU0 0
	u_int16_t	virtual_disk_target_id;

	u_int64_t	reg_lock_row_lba;

	u_int32_t	reg_lock_length;

	u_int16_t	next_lm_id;
	u_int8_t	ex_status;
	u_int8_t	status;

	u_int8_t	raid_flags;
	u_int8_t	num_sge;
	u_int16_t	config_seq_num;

	u_int8_t	span_arm;
	u_int8_t	_reserved3[3];
} __packed;

struct mfii_sge {
	u_int64_t	sg_addr;
	u_int32_t	sg_len;
	u_int16_t	_reserved;
	u_int8_t	sg_next_chain_offset;
	u_int8_t	sg_flags;
} __packed;

#define MFII_SGE_ADDR_MASK		(0x03)
#define MFII_SGE_ADDR_SYSTEM		(0x00)
#define MFII_SGE_ADDR_IOCDDR		(0x01)
#define MFII_SGE_ADDR_IOCPLB		(0x02)
#define MFII_SGE_ADDR_IOCPLBNTA		(0x03)
#define MFII_SGE_END_OF_LIST		(0x40)
#define MFII_SGE_CHAIN_ELEMENT		(0x80)

#define MFII_REQUEST_SIZE	256

#define MR_DCMD_LD_MAP_GET_INFO			0x0300e101

#define MFII_MAX_ROW		32
#define MFII_MAX_ARRAY		128

struct mfii_array_map {
	uint16_t		mam_pd[MFII_MAX_ROW];
} __packed;

struct mfii_dev_handle {
	uint16_t		mdh_cur_handle;
	uint8_t			mdh_valid;
	uint8_t			mdh_reserved;
	uint16_t		mdh_handle[2];
} __packed;

struct mfii_ld_map {
	uint32_t		mlm_total_size;
	uint32_t		mlm_reserved1[5];
	uint32_t		mlm_num_lds;
	uint32_t		mlm_reserved2;
	uint8_t			mlm_tgtid_to_ld[2 * MFI_MAX_LD];
	uint8_t			mlm_pd_timeout;
	uint8_t			mlm_reserved3[7];
	struct mfii_array_map	mlm_am[MFII_MAX_ARRAY];
	struct mfii_dev_handle	mlm_dev_handle[MFI_MAX_PD];
} __packed;

struct mfii_task_mgmt {
	union {
		uint8_t			request[128];
		struct mpii_msg_scsi_task_request
					mpii_request;
	} __packed __aligned(8);

	union {
		uint8_t			reply[128];
		uint32_t		flags;
#define MFII_TASK_MGMT_FLAGS_LD				(1 << 0)
#define MFII_TASK_MGMT_FLAGS_PD				(1 << 1)
		struct mpii_msg_scsi_task_reply
					mpii_reply;
	} __packed __aligned(8);
} __packed __aligned(8);

struct mfii_dmamem {
	bus_dmamap_t		mdm_map;
	bus_dma_segment_t	mdm_seg;
	size_t			mdm_size;
	caddr_t			mdm_kva;
};
#define MFII_DMA_MAP(_mdm)	((_mdm)->mdm_map)
#define MFII_DMA_LEN(_mdm)	((_mdm)->mdm_size)
#define MFII_DMA_DVA(_mdm)	((u_int64_t)(_mdm)->mdm_map->dm_segs[0].ds_addr)
#define MFII_DMA_KVA(_mdm)	((void *)(_mdm)->mdm_kva)

struct mfii_softc;

struct mfii_ccb {
	void			*ccb_request;
	u_int64_t		ccb_request_dva;
	bus_addr_t		ccb_request_offset;

	void			*ccb_mfi;
	u_int64_t		ccb_mfi_dva;
	bus_addr_t		ccb_mfi_offset;

	struct mfi_sense	*ccb_sense;
	u_int64_t		ccb_sense_dva;
	bus_addr_t		ccb_sense_offset;

	struct mfii_sge		*ccb_sgl;
	u_int64_t		ccb_sgl_dva;
	bus_addr_t		ccb_sgl_offset;
	u_int			ccb_sgl_len;

	struct mfii_request_descr ccb_req;

	bus_dmamap_t		ccb_dmamap;

	/* data for sgl */
	void			*ccb_data;
	size_t			ccb_len;

	int			ccb_direction;
#define MFII_DATA_NONE			0
#define MFII_DATA_IN			1
#define MFII_DATA_OUT			2

	void			*ccb_cookie;
	void			(*ccb_done)(struct mfii_softc *,
				    struct mfii_ccb *);

	u_int32_t		ccb_flags;
#define MFI_CCB_F_ERR			(1<<0)
	u_int			ccb_smid;
	u_int			ccb_refcnt;
	SIMPLEQ_ENTRY(mfii_ccb)	ccb_link;
};
SIMPLEQ_HEAD(mfii_ccb_list, mfii_ccb);

struct mfii_pd_dev_handles {
	struct smr_entry	pd_smr;
	uint16_t		pd_handles[MFI_MAX_PD];
};

struct mfii_pd_softc {
	struct scsibus_softc	*pd_scsibus;
	struct mfii_pd_dev_handles *pd_dev_handles;
	uint8_t			pd_timeout;
};

struct mfii_iop {
	int bar;
	int num_sge_loc;
#define MFII_IOP_NUM_SGE_LOC_ORIG	0
#define MFII_IOP_NUM_SGE_LOC_35		1
	u_int16_t ldio_ctx_reg_lock_flags;
	u_int8_t ldio_req_type;
	u_int8_t ldio_ctx_type_nseg;
	u_int8_t sge_flag_chain;
	u_int8_t sge_flag_eol;
};

struct mfii_softc {
	struct device		sc_dev;
	const struct mfii_iop	*sc_iop;

	pci_chipset_tag_t	sc_pc;
	pcitag_t		sc_tag;

	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	bus_size_t		sc_ios;
	bus_dma_tag_t		sc_dmat;

	void			*sc_ih;

	struct mutex		sc_ccb_mtx;
	struct mutex		sc_post_mtx;

	u_int			sc_max_fw_cmds;
	u_int			sc_max_cmds;
	u_int			sc_max_sgl;

	u_int			sc_reply_postq_depth;
	u_int			sc_reply_postq_index;
	struct mutex		sc_reply_postq_mtx;
	struct mfii_dmamem	*sc_reply_postq;

	struct mfii_dmamem	*sc_requests;
	struct mfii_dmamem	*sc_mfi;
	struct mfii_dmamem	*sc_sense;
	struct mfii_dmamem	*sc_sgl;

	struct mfii_ccb		*sc_ccb;
	struct mfii_ccb_list	sc_ccb_freeq;

	struct mfii_ccb		*sc_aen_ccb;
	struct task		sc_aen_task;

	struct mutex		sc_abort_mtx;
	struct mfii_ccb_list	sc_abort_list;
	struct task		sc_abort_task;

	struct scsibus_softc	*sc_scsibus;
	struct mfii_pd_softc	*sc_pd;
	struct scsi_iopool	sc_iopool;

	/* save some useful information for logical drives that is missing
	 * in sc_ld_list
	 */
	struct {
		char		ld_dev[16];	/* device name sd? */
	}			sc_ld[MFI_MAX_LD];
	int			sc_target_lds[MFI_MAX_LD];

	/* scsi ioctl from sd device */
	int			(*sc_ioctl)(struct device *, u_long, caddr_t);

	/* bio */
	struct mfi_conf		*sc_cfg;
	struct mfi_ctrl_info	sc_info;
	struct mfi_ld_list	sc_ld_list;
	struct mfi_ld_details	*sc_ld_details; /* array to all logical disks */
	int			sc_no_pd; /* used physical disks */
	int			sc_ld_sz; /* sizeof sc_ld_details */

	/* mgmt lock */
	struct rwlock		sc_lock;

	/* sensors */
	struct ksensordev	sc_sensordev;
	struct ksensor		*sc_bbu;
	struct ksensor		*sc_bbu_status;
	struct ksensor		*sc_sensors;
};

#ifdef MFII_DEBUG
#define DPRINTF(x...)		do { if (mfii_debug) printf(x); } while(0)
#define DNPRINTF(n,x...)	do { if (mfii_debug & n) printf(x); } while(0)
#define	MFII_D_CMD		0x0001
#define	MFII_D_INTR		0x0002
#define	MFII_D_MISC		0x0004
#define	MFII_D_DMA		0x0008
#define	MFII_D_IOCTL		0x0010
#define	MFII_D_RW		0x0020
#define	MFII_D_MEM		0x0040
#define	MFII_D_CCB		0x0080
uint32_t	mfii_debug = 0
/*		    | MFII_D_CMD */
/*		    | MFII_D_INTR */
		    | MFII_D_MISC
/*		    | MFII_D_DMA */
/*		    | MFII_D_IOCTL */
/*		    | MFII_D_RW */
/*		    | MFII_D_MEM */
/*		    | MFII_D_CCB */
		;
#else
#define DPRINTF(x...)
#define DNPRINTF(n,x...)
#endif

int		mfii_match(struct device *, void *, void *);
void		mfii_attach(struct device *, struct device *, void *);
int		mfii_detach(struct device *, int);
int		mfii_activate(struct device *, int);

const struct cfattach mfii_ca = {
	sizeof(struct mfii_softc),
	mfii_match,
	mfii_attach,
	mfii_detach,
	mfii_activate,
};

struct cfdriver mfii_cd = {
	NULL,
	"mfii",
	DV_DULL
};

void		mfii_scsi_cmd(struct scsi_xfer *);
void		mfii_scsi_cmd_done(struct mfii_softc *, struct mfii_ccb *);
int		mfii_scsi_ioctl(struct scsi_link *, u_long, caddr_t, int);
int		mfii_ioctl_cache(struct scsi_link *, u_long, struct dk_cache *);

const struct scsi_adapter mfii_switch = {
	mfii_scsi_cmd, NULL, NULL, NULL, mfii_scsi_ioctl
};

void		mfii_pd_scsi_cmd(struct scsi_xfer *);
int		mfii_pd_scsi_probe(struct scsi_link *);

const struct scsi_adapter mfii_pd_switch = {
	mfii_pd_scsi_cmd, NULL, mfii_pd_scsi_probe, NULL, NULL,
};

#define DEVNAME(_sc)		((_sc)->sc_dev.dv_xname)

u_int32_t		mfii_read(struct mfii_softc *, bus_size_t);
void			mfii_write(struct mfii_softc *, bus_size_t, u_int32_t);

struct mfii_dmamem *	mfii_dmamem_alloc(struct mfii_softc *, size_t);
void			mfii_dmamem_free(struct mfii_softc *,
			    struct mfii_dmamem *);

void *			mfii_get_ccb(void *);
void			mfii_put_ccb(void *, void *);
int			mfii_init_ccb(struct mfii_softc *);
void			mfii_scrub_ccb(struct mfii_ccb *);

int			mfii_reset_hard(struct mfii_softc *);
int			mfii_transition_firmware(struct mfii_softc *);
int			mfii_initialise_firmware(struct mfii_softc *);
int			mfii_get_info(struct mfii_softc *);
int			mfii_syspd(struct mfii_softc *);

void			mfii_start(struct mfii_softc *, struct mfii_ccb *);
void			mfii_done(struct mfii_softc *, struct mfii_ccb *);
int			mfii_poll(struct mfii_softc *, struct mfii_ccb *);
void			mfii_poll_done(struct mfii_softc *, struct mfii_ccb *);
int			mfii_exec(struct mfii_softc *, struct mfii_ccb *);
void			mfii_exec_done(struct mfii_softc *, struct mfii_ccb *);
int			mfii_my_intr(struct mfii_softc *);
int			mfii_intr(void *);
void			mfii_postq(struct mfii_softc *);

int			mfii_load_ccb(struct mfii_softc *, struct mfii_ccb *,
			    void *, int);
int			mfii_load_mfa(struct mfii_softc *, struct mfii_ccb *,
			    void *, int);

int			mfii_mfa_poll(struct mfii_softc *, struct mfii_ccb *);

int			mfii_mgmt(struct mfii_softc *, uint32_t,
			    const union mfi_mbox *, void *, size_t, int);
int			mfii_do_mgmt(struct mfii_softc *, struct mfii_ccb *,
			    uint32_t, const union mfi_mbox *, void *, size_t,
			    int);
void			mfii_empty_done(struct mfii_softc *, struct mfii_ccb *);

int			mfii_scsi_cmd_io(struct mfii_softc *,
			    struct scsi_xfer *);
int			mfii_scsi_cmd_cdb(struct mfii_softc *,
			    struct scsi_xfer *);
int			mfii_pd_scsi_cmd_cdb(struct mfii_softc *,
			    struct scsi_xfer *);
void			mfii_scsi_cmd_tmo(void *);

int			mfii_dev_handles_update(struct mfii_softc *sc);
void			mfii_dev_handles_smr(void *pd_arg);

void			mfii_abort_task(void *);
void			mfii_abort(struct mfii_softc *, struct mfii_ccb *,
			    uint16_t, uint16_t, uint8_t, uint32_t);
void			mfii_scsi_cmd_abort_done(struct mfii_softc *,
			    struct mfii_ccb *);

int			mfii_aen_register(struct mfii_softc *);
void			mfii_aen_start(struct mfii_softc *, struct mfii_ccb *,
			    struct mfii_dmamem *, uint32_t);
void			mfii_aen_done(struct mfii_softc *, struct mfii_ccb *);
void			mfii_aen(void *);
void			mfii_aen_unregister(struct mfii_softc *);

void			mfii_aen_pd_insert(struct mfii_softc *,
			    const struct mfi_evtarg_pd_address *);
void			mfii_aen_pd_remove(struct mfii_softc *,
			    const struct mfi_evtarg_pd_address *);
void			mfii_aen_pd_state_change(struct mfii_softc *,
			    const struct mfi_evtarg_pd_state *);
void			mfii_aen_ld_update(struct mfii_softc *);

#if NBIO > 0
int		mfii_ioctl(struct device *, u_long, caddr_t);
int		mfii_bio_getitall(struct mfii_softc *);
int		mfii_ioctl_inq(struct mfii_softc *, struct bioc_inq *);
int		mfii_ioctl_vol(struct mfii_softc *, struct bioc_vol *);
int		mfii_ioctl_disk(struct mfii_softc *, struct bioc_disk *);
int		mfii_ioctl_alarm(struct mfii_softc *, struct bioc_alarm *);
int		mfii_ioctl_blink(struct mfii_softc *sc, struct bioc_blink *);
int		mfii_ioctl_setstate(struct mfii_softc *,
		    struct bioc_setstate *);
int		mfii_ioctl_patrol(struct mfii_softc *sc, struct bioc_patrol *);
int		mfii_bio_hs(struct mfii_softc *, int, int, void *);

#ifndef SMALL_KERNEL
static const char *mfi_bbu_indicators[] = {
	"pack missing",
	"voltage low",
	"temp high",
	"charge active",
	"discharge active",
	"learn cycle req'd",
	"learn cycle active",
	"learn cycle failed",
	"learn cycle timeout",
	"I2C errors",
	"replace pack",
	"low capacity",
	"periodic learn req'd"
};

void		mfii_init_ld_sensor(struct mfii_softc *, int);
void		mfii_refresh_ld_sensor(struct mfii_softc *, int);
int		mfii_create_sensors(struct mfii_softc *);
void		mfii_refresh_sensors(void *);
void		mfii_bbu(struct mfii_softc *);
#endif /* SMALL_KERNEL */
#endif /* NBIO > 0 */

/*
 * mfii boards support asynchronous (and non-polled) completion of
 * dcmds by proxying them through a passthru mpii command that points
 * at a dcmd frame. since the passthru command is submitted like
 * the scsi commands using an SMID in the request descriptor,
 * ccb_request memory * must contain the passthru command because
 * that is what the SMID refers to. this means ccb_request cannot
 * contain the dcmd. rather than allocating separate dma memory to
 * hold the dcmd, we reuse the sense memory buffer for it.
 */

void			mfii_dcmd_start(struct mfii_softc *,
			    struct mfii_ccb *);

static inline void
mfii_dcmd_scrub(struct mfii_ccb *ccb)
{
	memset(ccb->ccb_sense, 0, sizeof(*ccb->ccb_sense));
}

static inline struct mfi_dcmd_frame *
mfii_dcmd_frame(struct mfii_ccb *ccb)
{
	CTASSERT(sizeof(struct mfi_dcmd_frame) <= sizeof(*ccb->ccb_sense));
	return ((struct mfi_dcmd_frame *)ccb->ccb_sense);
}

static inline void
mfii_dcmd_sync(struct mfii_softc *sc, struct mfii_ccb *ccb, int flags)
{
	bus_dmamap_sync(sc->sc_dmat, MFII_DMA_MAP(sc->sc_sense),
	    ccb->ccb_sense_offset, sizeof(*ccb->ccb_sense), flags);
}

#define mfii_fw_state(_sc) mfii_read((_sc), MFI_OSP)

const struct mfii_iop mfii_iop_thunderbolt = {
	MFII_BAR,
	MFII_IOP_NUM_SGE_LOC_ORIG,
	0,
	MFII_REQ_TYPE_LDIO,
	0,
	MFII_SGE_CHAIN_ELEMENT | MFII_SGE_ADDR_IOCPLBNTA,
	0
};

/*
 * a lot of these values depend on us not implementing fastpath yet.
 */
const struct mfii_iop mfii_iop_25 = {
	MFII_BAR,
	MFII_IOP_NUM_SGE_LOC_ORIG,
	MFII_RAID_CTX_RL_FLAGS_CPU0, /* | MFII_RAID_CTX_RL_FLAGS_SEQNO_EN */
	MFII_REQ_TYPE_NO_LOCK,
	MFII_RAID_CTX_TYPE_CUDA | 0x1,
	MFII_SGE_CHAIN_ELEMENT,
	MFII_SGE_END_OF_LIST
};

const struct mfii_iop mfii_iop_35 = {
	MFII_BAR_35,
	MFII_IOP_NUM_SGE_LOC_35,
	MFII_RAID_CTX_ROUTING_FLAGS_CPU0, /* | MFII_RAID_CTX_ROUTING_FLAGS_SQN */
	MFII_REQ_TYPE_NO_LOCK,
	MFII_RAID_CTX_TYPE_CUDA | 0x1,
	MFII_SGE_CHAIN_ELEMENT,
	MFII_SGE_END_OF_LIST
};

struct mfii_device {
	pcireg_t		mpd_vendor;
	pcireg_t		mpd_product;
	const struct mfii_iop	*mpd_iop;
};

const struct mfii_device mfii_devices[] = {
	{ PCI_VENDOR_SYMBIOS,	PCI_PRODUCT_SYMBIOS_MEGARAID_2208,
	    &mfii_iop_thunderbolt },
	{ PCI_VENDOR_SYMBIOS,	PCI_PRODUCT_SYMBIOS_MEGARAID_3008,
	    &mfii_iop_25 },
	{ PCI_VENDOR_SYMBIOS,	PCI_PRODUCT_SYMBIOS_MEGARAID_3108,
	    &mfii_iop_25 },
	{ PCI_VENDOR_SYMBIOS,	PCI_PRODUCT_SYMBIOS_MEGARAID_3404,
	    &mfii_iop_35 },
	{ PCI_VENDOR_SYMBIOS,	PCI_PRODUCT_SYMBIOS_MEGARAID_3504,
	    &mfii_iop_35 },
	{ PCI_VENDOR_SYMBIOS,	PCI_PRODUCT_SYMBIOS_MEGARAID_3408,
	    &mfii_iop_35 },
	{ PCI_VENDOR_SYMBIOS,	PCI_PRODUCT_SYMBIOS_MEGARAID_3508,
	    &mfii_iop_35 },
	{ PCI_VENDOR_SYMBIOS,	PCI_PRODUCT_SYMBIOS_MEGARAID_3416,
	    &mfii_iop_35 },
	{ PCI_VENDOR_SYMBIOS,	PCI_PRODUCT_SYMBIOS_MEGARAID_3516,
	    &mfii_iop_35 },
	{ PCI_VENDOR_SYMBIOS,   PCI_PRODUCT_SYMBIOS_MEGARAID_38XX,
	    &mfii_iop_35 },
	{ PCI_VENDOR_SYMBIOS,   PCI_PRODUCT_SYMBIOS_MEGARAID_38XX_2,
	    &mfii_iop_35 },
	{ PCI_VENDOR_SYMBIOS,   PCI_PRODUCT_SYMBIOS_MEGARAID_39XX,
	    &mfii_iop_35 },
	{ PCI_VENDOR_SYMBIOS,   PCI_PRODUCT_SYMBIOS_MEGARAID_39XX_2,
	    &mfii_iop_35 }
};

const struct mfii_iop *mfii_find_iop(struct pci_attach_args *);

const struct mfii_iop *
mfii_find_iop(struct pci_attach_args *pa)
{
	const struct mfii_device *mpd;
	int i;

	for (i = 0; i < nitems(mfii_devices); i++) {
		mpd = &mfii_devices[i];

		if (mpd->mpd_vendor == PCI_VENDOR(pa->pa_id) &&
		    mpd->mpd_product == PCI_PRODUCT(pa->pa_id))
			return (mpd->mpd_iop);
	}

	return (NULL);
}

int
mfii_match(struct device *parent, void *match, void *aux)
{
	return ((mfii_find_iop(aux) != NULL) ? 1 : 0);
}

void
mfii_attach(struct device *parent, struct device *self, void *aux)
{
	struct mfii_softc *sc = (struct mfii_softc *)self;
	struct pci_attach_args *pa = aux;
	pcireg_t memtype;
	pci_intr_handle_t ih;
	struct scsibus_attach_args saa;
	u_int32_t status, scpad2, scpad3;
	int chain_frame_sz, nsge_in_io, nsge_in_chain, i;

	/* init sc */
	sc->sc_iop = mfii_find_iop(aux);
	sc->sc_dmat = pa->pa_dmat;
	SIMPLEQ_INIT(&sc->sc_ccb_freeq);
	mtx_init(&sc->sc_ccb_mtx, IPL_BIO);
	mtx_init(&sc->sc_post_mtx, IPL_BIO);
	mtx_init(&sc->sc_reply_postq_mtx, IPL_BIO);
	scsi_iopool_init(&sc->sc_iopool, sc, mfii_get_ccb, mfii_put_ccb);

	rw_init(&sc->sc_lock, "mfii_lock");

	sc->sc_aen_ccb = NULL;
	task_set(&sc->sc_aen_task, mfii_aen, sc);

	mtx_init(&sc->sc_abort_mtx, IPL_BIO);
	SIMPLEQ_INIT(&sc->sc_abort_list);
	task_set(&sc->sc_abort_task, mfii_abort_task, sc);

	/* wire up the bus shizz */
	memtype = pci_mapreg_type(pa->pa_pc, pa->pa_tag, sc->sc_iop->bar);
	if (pci_mapreg_map(pa, sc->sc_iop->bar, memtype, 0,
	    &sc->sc_iot, &sc->sc_ioh, NULL, &sc->sc_ios, MFII_PCI_MEMSIZE)) {
		printf(": unable to map registers\n");
		return;
	}

	/* disable interrupts */
	mfii_write(sc, MFI_OMSK, 0xffffffff);

	if (pci_intr_map_msi(pa, &ih) != 0 && pci_intr_map(pa, &ih) != 0) {
		printf(": unable to map interrupt\n");
		goto pci_unmap;
	}
	printf(": %s\n", pci_intr_string(pa->pa_pc, ih));

	/* lets get started */
	if (mfii_transition_firmware(sc))
		goto pci_unmap;

	/* determine max_cmds (refer to the Linux megaraid_sas driver) */
	scpad3 = mfii_read(sc, MFII_OSP3);
	status = mfii_fw_state(sc);
	sc->sc_max_fw_cmds = scpad3 & MFI_STATE_MAXCMD_MASK;
	if (sc->sc_max_fw_cmds == 0)
		sc->sc_max_fw_cmds = status & MFI_STATE_MAXCMD_MASK;
	/*
	 * reduce max_cmds by 1 to ensure that the reply queue depth does not
	 * exceed FW supplied max_fw_cmds.
	 */
	sc->sc_max_cmds = min(sc->sc_max_fw_cmds, 1024) - 1;

	/* determine max_sgl (refer to the Linux megaraid_sas driver) */
	scpad2 = mfii_read(sc, MFII_OSP2);
	chain_frame_sz =
		((scpad2 & MFII_MAX_CHAIN_MASK) >> MFII_MAX_CHAIN_SHIFT) *
		((scpad2 & MFII_MAX_CHAIN_UNIT) ? MFII_1MB_IO : MFII_256K_IO);
	if (chain_frame_sz < MFII_CHAIN_FRAME_MIN)
		chain_frame_sz = MFII_CHAIN_FRAME_MIN;

	nsge_in_io = (MFII_REQUEST_SIZE -
		sizeof(struct mpii_msg_scsi_io) -
		sizeof(struct mfii_raid_context)) / sizeof(struct mfii_sge);
	nsge_in_chain = chain_frame_sz / sizeof(struct mfii_sge);

	/* round down to nearest power of two */
	sc->sc_max_sgl = 1;
	while ((sc->sc_max_sgl << 1) <= (nsge_in_io + nsge_in_chain))
		sc->sc_max_sgl <<= 1;

	DNPRINTF(MFII_D_MISC, "%s: OSP 0x%08x, OSP2 0x%08x, OSP3 0x%08x\n",
	    DEVNAME(sc), status, scpad2, scpad3);
	DNPRINTF(MFII_D_MISC, "%s: max_fw_cmds %d, max_cmds %d\n",
	    DEVNAME(sc), sc->sc_max_fw_cmds, sc->sc_max_cmds);
	DNPRINTF(MFII_D_MISC, "%s: nsge_in_io %d, nsge_in_chain %d, "
	    "max_sgl %d\n", DEVNAME(sc), nsge_in_io, nsge_in_chain,
	    sc->sc_max_sgl);

	/* sense memory */
	CTASSERT(sizeof(struct mfi_sense) == MFI_SENSE_SIZE);
	sc->sc_sense = mfii_dmamem_alloc(sc, sc->sc_max_cmds * MFI_SENSE_SIZE);
	if (sc->sc_sense == NULL) {
		printf("%s: unable to allocate sense memory\n", DEVNAME(sc));
		goto pci_unmap;
	}

	/* reply post queue */
	sc->sc_reply_postq_depth = roundup(sc->sc_max_fw_cmds, 16);

	sc->sc_reply_postq = mfii_dmamem_alloc(sc,
	    sc->sc_reply_postq_depth * sizeof(struct mpii_reply_descr));
	if (sc->sc_reply_postq == NULL)
		goto free_sense;

	memset(MFII_DMA_KVA(sc->sc_reply_postq), 0xff,
	    MFII_DMA_LEN(sc->sc_reply_postq));

	/* MPII request frame array */
	sc->sc_requests = mfii_dmamem_alloc(sc,
	    MFII_REQUEST_SIZE * (sc->sc_max_cmds + 1));
	if (sc->sc_requests == NULL)
		goto free_reply_postq;

	/* MFI command frame array */
	sc->sc_mfi = mfii_dmamem_alloc(sc, sc->sc_max_cmds * MFI_FRAME_SIZE);
	if (sc->sc_mfi == NULL)
		goto free_requests;

	/* MPII SGL array */
	sc->sc_sgl = mfii_dmamem_alloc(sc, sc->sc_max_cmds *
	    sizeof(struct mfii_sge) * sc->sc_max_sgl);
	if (sc->sc_sgl == NULL)
		goto free_mfi;

	if (mfii_init_ccb(sc) != 0) {
		printf("%s: could not init ccb list\n", DEVNAME(sc));
		goto free_sgl;
	}

	/* kickstart firmware with all addresses and pointers */
	if (mfii_initialise_firmware(sc) != 0) {
		printf("%s: could not initialize firmware\n", DEVNAME(sc));
		goto free_sgl;
	}

	if (mfii_get_info(sc) != 0) {
		printf("%s: could not retrieve controller information\n",
		    DEVNAME(sc));
		goto free_sgl;
	}

	printf("%s: \"%s\", firmware %s", DEVNAME(sc),
	    sc->sc_info.mci_product_name, sc->sc_info.mci_package_version);
	if (letoh16(sc->sc_info.mci_memory_size) > 0)
		printf(", %uMB cache", letoh16(sc->sc_info.mci_memory_size));
	printf("\n");

	sc->sc_ih = pci_intr_establish(sc->sc_pc, ih, IPL_BIO,
	    mfii_intr, sc, DEVNAME(sc));
	if (sc->sc_ih == NULL)
		goto free_sgl;

	saa.saa_adapter_softc = sc;
	saa.saa_adapter = &mfii_switch;
	saa.saa_adapter_target = SDEV_NO_ADAPTER_TARGET;
	saa.saa_adapter_buswidth = sc->sc_info.mci_max_lds;
	saa.saa_luns = 8;
	saa.saa_openings = sc->sc_max_cmds;
	saa.saa_pool = &sc->sc_iopool;
	saa.saa_quirks = saa.saa_flags = 0;
	saa.saa_wwpn = saa.saa_wwnn = 0;

	sc->sc_scsibus = (struct scsibus_softc *)config_found(&sc->sc_dev, &saa,
	    scsiprint);

	mfii_syspd(sc);

	if (mfii_aen_register(sc) != 0) {
		/* error printed by mfii_aen_register */
		goto intr_disestablish;
	}

	if (mfii_mgmt(sc, MR_DCMD_LD_GET_LIST, NULL, &sc->sc_ld_list,
	    sizeof(sc->sc_ld_list), SCSI_DATA_IN) != 0) {
		printf("%s: getting list of logical disks failed\n", DEVNAME(sc));
		goto intr_disestablish;
	}
	memset(sc->sc_target_lds, -1, sizeof(sc->sc_target_lds));
	for (i = 0; i < sc->sc_ld_list.mll_no_ld; i++) {
		int target = sc->sc_ld_list.mll_list[i].mll_ld.mld_target;
		sc->sc_target_lds[target] = i;
	}

	/* enable interrupts */
	mfii_write(sc, MFI_OSTS, 0xffffffff);
	mfii_write(sc, MFI_OMSK, ~MFII_OSTS_INTR_VALID);

#if NBIO > 0
	if (bio_register(&sc->sc_dev, mfii_ioctl) != 0)
		panic("%s: controller registration failed", DEVNAME(sc));
	else
		sc->sc_ioctl = mfii_ioctl;

#ifndef SMALL_KERNEL
	if (mfii_create_sensors(sc) != 0)
		printf("%s: unable to create sensors\n", DEVNAME(sc));
#endif
#endif /* NBIO > 0 */

	return;
intr_disestablish:
	pci_intr_disestablish(sc->sc_pc, sc->sc_ih);
free_sgl:
	mfii_dmamem_free(sc, sc->sc_sgl);
free_mfi:
	mfii_dmamem_free(sc, sc->sc_mfi);
free_requests:
	mfii_dmamem_free(sc, sc->sc_requests);
free_reply_postq:
	mfii_dmamem_free(sc, sc->sc_reply_postq);
free_sense:
	mfii_dmamem_free(sc, sc->sc_sense);
pci_unmap:
	bus_space_unmap(sc->sc_iot, sc->sc_ioh, sc->sc_ios);
}

static inline uint16_t
mfii_dev_handle(struct mfii_softc *sc, uint16_t target)
{
	struct mfii_pd_dev_handles *handles;
	uint16_t handle;

	smr_read_enter();
	handles = SMR_PTR_GET(&sc->sc_pd->pd_dev_handles);
	handle = handles->pd_handles[target];
	smr_read_leave();

	return (handle);
}

void
mfii_dev_handles_smr(void *pd_arg)
{
	struct mfii_pd_dev_handles *handles = pd_arg;

	free(handles, M_DEVBUF, sizeof(*handles));
}

int
mfii_dev_handles_update(struct mfii_softc *sc)
{
	struct mfii_ld_map *lm;
	struct mfii_pd_dev_handles *handles, *old_handles;
	int i;
	int rv = 0;

	lm = malloc(sizeof(*lm), M_TEMP, M_WAITOK|M_ZERO);

	rv = mfii_mgmt(sc, MR_DCMD_LD_MAP_GET_INFO, NULL, lm, sizeof(*lm),
	    SCSI_DATA_IN|SCSI_NOSLEEP);

	if (rv != 0) {
		rv = EIO;
		goto free_lm;
	}

	handles = malloc(sizeof(*handles), M_DEVBUF, M_WAITOK);
	smr_init(&handles->pd_smr);
	for (i = 0; i < MFI_MAX_PD; i++)
		handles->pd_handles[i] = lm->mlm_dev_handle[i].mdh_cur_handle;

	/* commit the updated info */
	sc->sc_pd->pd_timeout = lm->mlm_pd_timeout;
	old_handles = SMR_PTR_GET_LOCKED(&sc->sc_pd->pd_dev_handles);
	SMR_PTR_SET_LOCKED(&sc->sc_pd->pd_dev_handles, handles);

	if (old_handles != NULL)
		smr_call(&old_handles->pd_smr, mfii_dev_handles_smr, old_handles);

free_lm:
	free(lm, M_TEMP, sizeof(*lm));

	return (rv);
}

int
mfii_syspd(struct mfii_softc *sc)
{
	struct scsibus_attach_args saa;

	sc->sc_pd = malloc(sizeof(*sc->sc_pd), M_DEVBUF, M_WAITOK|M_ZERO);
	if (sc->sc_pd == NULL)
		return (1);

	if (mfii_dev_handles_update(sc) != 0)
		goto free_pdsc;

	saa.saa_adapter =  &mfii_pd_switch;
	saa.saa_adapter_softc = sc;
	saa.saa_adapter_buswidth = MFI_MAX_PD;
	saa.saa_adapter_target = SDEV_NO_ADAPTER_TARGET;
	saa.saa_luns = 8;
	saa.saa_openings = sc->sc_max_cmds - 1;
	saa.saa_pool = &sc->sc_iopool;
	saa.saa_quirks = saa.saa_flags = 0;
	saa.saa_wwpn = saa.saa_wwnn = 0;

	sc->sc_pd->pd_scsibus = (struct scsibus_softc *)
	    config_found(&sc->sc_dev, &saa, scsiprint);

	return (0);

free_pdsc:
	free(sc->sc_pd, M_DEVBUF, sizeof(*sc->sc_pd));
	return (1);
}

int
mfii_detach(struct device *self, int flags)
{
	struct mfii_softc *sc = (struct mfii_softc *)self;

	if (sc->sc_ih == NULL)
		return (0);

#ifndef SMALL_KERNEL
	if (sc->sc_sensors) {
		sensordev_deinstall(&sc->sc_sensordev);
		free(sc->sc_sensors, M_DEVBUF,
		    MFI_MAX_LD * sizeof(struct ksensor));
	}

	if (sc->sc_bbu) {
		free(sc->sc_bbu, M_DEVBUF, 4 * sizeof(*sc->sc_bbu));
	}

	if (sc->sc_bbu_status) {
		free(sc->sc_bbu_status, M_DEVBUF,
		    sizeof(*sc->sc_bbu_status) * sizeof(mfi_bbu_indicators));
	}
#endif /* SMALL_KERNEL */

	mfii_aen_unregister(sc);
	pci_intr_disestablish(sc->sc_pc, sc->sc_ih);
	mfii_dmamem_free(sc, sc->sc_sgl);
	mfii_dmamem_free(sc, sc->sc_mfi);
	mfii_dmamem_free(sc, sc->sc_requests);
	mfii_dmamem_free(sc, sc->sc_reply_postq);
	mfii_dmamem_free(sc, sc->sc_sense);
	bus_space_unmap(sc->sc_iot, sc->sc_ioh, sc->sc_ios);

	return (0);
}

static void
mfii_flush_cache(struct mfii_softc *sc, struct mfii_ccb *ccb)
{
#if 0
	union mfi_mbox mbox = {
		.b[0] = MR_FLUSH_CTRL_CACHE | MR_FLUSH_DISK_CACHE,
	};
	int rv;

	mfii_scrub_ccb(ccb);
	rv = mfii_do_mgmt(sc, ccb, MR_DCMD_CTRL_CACHE_FLUSH, &mbox,
	    NULL, 0, SCSI_NOSLEEP);
	if (rv != 0) {
		printf("%s: unable to flush cache\n", DEVNAME(sc));
		return;
	}
#endif
}

static void
mfii_shutdown(struct mfii_softc *sc, struct mfii_ccb *ccb)
{
#if 0
	int rv;

	mfii_scrub_ccb(ccb);
	rv = mfii_do_mgmt(sc, ccb, MR_DCMD_CTRL_SHUTDOWN, NULL,
	    NULL, 0, SCSI_POLL);
	if (rv != 0) {
		printf("%s: unable to shutdown controller\n", DEVNAME(sc));
		return;
	}
#endif
}

static void
mfii_powerdown(struct mfii_softc *sc)
{
	struct mfii_ccb *ccb;

	ccb = scsi_io_get(&sc->sc_iopool, SCSI_NOSLEEP);
	if (ccb == NULL) {
		printf("%s: unable to allocate ccb for shutdown\n",
		    DEVNAME(sc));
		return;
	}

	mfii_flush_cache(sc, ccb);
	mfii_shutdown(sc, ccb);
	scsi_io_put(&sc->sc_iopool, ccb);
}

int
mfii_activate(struct device *self, int act)
{
	struct mfii_softc *sc = (struct mfii_softc *)self;
	int rv;

	switch (act) {
	case DVACT_POWERDOWN:
		rv = config_activate_children(&sc->sc_dev, act);
		mfii_powerdown(sc);
		break;
	default:
		rv = config_activate_children(&sc->sc_dev, act);
		break;
	}

	return (rv);
}

u_int32_t
mfii_read(struct mfii_softc *sc, bus_size_t r)
{
	bus_space_barrier(sc->sc_iot, sc->sc_ioh, r, 4,
	    BUS_SPACE_BARRIER_READ);
	return (bus_space_read_4(sc->sc_iot, sc->sc_ioh, r));
}

void
mfii_write(struct mfii_softc *sc, bus_size_t r, u_int32_t v)
{
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, r, v);
	bus_space_barrier(sc->sc_iot, sc->sc_ioh, r, 4,
	    BUS_SPACE_BARRIER_WRITE);
}

struct mfii_dmamem *
mfii_dmamem_alloc(struct mfii_softc *sc, size_t size)
{
	struct mfii_dmamem *m;
	int nsegs;

	m = malloc(sizeof(*m), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (m == NULL)
		return (NULL);

	m->mdm_size = size;

	if (bus_dmamap_create(sc->sc_dmat, size, 1, size, 0,
	    BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW, &m->mdm_map) != 0)
		goto mdmfree;

	if (bus_dmamem_alloc(sc->sc_dmat, size, PAGE_SIZE, 0, &m->mdm_seg, 1,
	    &nsegs, BUS_DMA_NOWAIT | BUS_DMA_ZERO) != 0)
		goto destroy;

	if (bus_dmamem_map(sc->sc_dmat, &m->mdm_seg, nsegs, size, &m->mdm_kva,
	    BUS_DMA_NOWAIT) != 0)
		goto free;

	if (bus_dmamap_load(sc->sc_dmat, m->mdm_map, m->mdm_kva, size, NULL,
	    BUS_DMA_NOWAIT) != 0)
		goto unmap;

	return (m);

unmap:
	bus_dmamem_unmap(sc->sc_dmat, m->mdm_kva, m->mdm_size);
free:
	bus_dmamem_free(sc->sc_dmat, &m->mdm_seg, 1);
destroy:
	bus_dmamap_destroy(sc->sc_dmat, m->mdm_map);
mdmfree:
	free(m, M_DEVBUF, sizeof *m);

	return (NULL);
}

void
mfii_dmamem_free(struct mfii_softc *sc, struct mfii_dmamem *m)
{
	bus_dmamap_unload(sc->sc_dmat, m->mdm_map);
	bus_dmamem_unmap(sc->sc_dmat, m->mdm_kva, m->mdm_size);
	bus_dmamem_free(sc->sc_dmat, &m->mdm_seg, 1);
	bus_dmamap_destroy(sc->sc_dmat, m->mdm_map);
	free(m, M_DEVBUF, sizeof *m);
}

void
mfii_dcmd_start(struct mfii_softc *sc, struct mfii_ccb *ccb)
{
	struct mpii_msg_scsi_io *io = ccb->ccb_request;
	struct mfii_raid_context *ctx = (struct mfii_raid_context *)(io + 1);
	struct mfii_sge *sge = (struct mfii_sge *)(ctx + 1);

	io->function = MFII_FUNCTION_PASSTHRU_IO;
	io->sgl_offset0 = (uint32_t *)sge - (uint32_t *)io;
	io->chain_offset = io->sgl_offset0 / 4;

	htolem64(&sge->sg_addr, ccb->ccb_sense_dva);
	htolem32(&sge->sg_len, sizeof(*ccb->ccb_sense));
	sge->sg_flags = MFII_SGE_CHAIN_ELEMENT | MFII_SGE_ADDR_IOCPLBNTA;

	ccb->ccb_req.flags = MFII_REQ_TYPE_SCSI;
	ccb->ccb_req.smid = letoh16(ccb->ccb_smid);

	mfii_start(sc, ccb);
}

int
mfii_aen_register(struct mfii_softc *sc)
{
	struct mfi_evt_log_info mel;
	struct mfii_ccb *ccb;
	struct mfii_dmamem *mdm;
	int rv;

	ccb = scsi_io_get(&sc->sc_iopool, SCSI_NOSLEEP);
	if (ccb == NULL) {
		printf("%s: unable to allocate ccb for aen\n", DEVNAME(sc));
		return (ENOMEM);
	}

	memset(&mel, 0, sizeof(mel));
	mfii_scrub_ccb(ccb);

	rv = mfii_do_mgmt(sc, ccb, MR_DCMD_CTRL_EVENT_GET_INFO, NULL,
	    &mel, sizeof(mel), SCSI_DATA_IN|SCSI_NOSLEEP);
	if (rv != 0) {
		scsi_io_put(&sc->sc_iopool, ccb);
		printf("%s: unable to get event info\n", DEVNAME(sc));
		return (EIO);
	}

	mdm = mfii_dmamem_alloc(sc, sizeof(struct mfi_evt_detail));
	if (mdm == NULL) {
		scsi_io_put(&sc->sc_iopool, ccb);
		printf("%s: unable to allocate event data\n", DEVNAME(sc));
		return (ENOMEM);
	}

	/* replay all the events from boot */
	mfii_aen_start(sc, ccb, mdm, lemtoh32(&mel.mel_boot_seq_num));

	return (0);
}

void
mfii_aen_start(struct mfii_softc *sc, struct mfii_ccb *ccb,
    struct mfii_dmamem *mdm, uint32_t seq)
{
	struct mfi_dcmd_frame *dcmd = mfii_dcmd_frame(ccb);
	struct mfi_frame_header *hdr = &dcmd->mdf_header;
	union mfi_sgl *sgl = &dcmd->mdf_sgl;
	union mfi_evt_class_locale mec;

	mfii_scrub_ccb(ccb);
	mfii_dcmd_scrub(ccb);
	memset(MFII_DMA_KVA(mdm), 0, MFII_DMA_LEN(mdm));

	ccb->ccb_cookie = mdm;
	ccb->ccb_done = mfii_aen_done;
	sc->sc_aen_ccb = ccb;

	mec.mec_members.class = MFI_EVT_CLASS_DEBUG;
	mec.mec_members.reserved = 0;
	mec.mec_members.locale = htole16(MFI_EVT_LOCALE_ALL);

	hdr->mfh_cmd = MFI_CMD_DCMD;
	hdr->mfh_sg_count = 1;
	hdr->mfh_flags = htole16(MFI_FRAME_DIR_READ | MFI_FRAME_SGL64);
	htolem32(&hdr->mfh_data_len, MFII_DMA_LEN(mdm));
	dcmd->mdf_opcode = htole32(MR_DCMD_CTRL_EVENT_WAIT);
	htolem32(&dcmd->mdf_mbox.w[0], seq);
	htolem32(&dcmd->mdf_mbox.w[1], mec.mec_word);
	htolem64(&sgl->sg64[0].addr, MFII_DMA_DVA(mdm));
	htolem32(&sgl->sg64[0].len, MFII_DMA_LEN(mdm));

	bus_dmamap_sync(sc->sc_dmat, MFII_DMA_MAP(mdm),
	    0, MFII_DMA_LEN(mdm), BUS_DMASYNC_PREREAD);

	mfii_dcmd_sync(sc, ccb, BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);
	mfii_dcmd_start(sc, ccb);
}

void
mfii_aen_done(struct mfii_softc *sc, struct mfii_ccb *ccb)
{
	KASSERT(sc->sc_aen_ccb == ccb);

	/* defer to a thread with KERNEL_LOCK so we can run autoconf */
	task_add(systq, &sc->sc_aen_task);
}

void
mfii_aen(void *arg)
{
	struct mfii_softc *sc = arg;
	struct mfii_ccb *ccb = sc->sc_aen_ccb;
	struct mfii_dmamem *mdm = ccb->ccb_cookie;
	const struct mfi_evt_detail *med = MFII_DMA_KVA(mdm);
	uint32_t code;

	mfii_dcmd_sync(sc, ccb,
	    BUS_DMASYNC_POSTREAD|BUS_DMASYNC_POSTWRITE);
	bus_dmamap_sync(sc->sc_dmat, MFII_DMA_MAP(mdm),
	    0, MFII_DMA_LEN(mdm), BUS_DMASYNC_POSTREAD);

	code = lemtoh32(&med->med_code);

#if 0
	log(LOG_DEBUG, "%s (seq %u, code %08x) %s\n", DEVNAME(sc),
	    lemtoh32(&med->med_seq_num), code, med->med_description);
#endif

	switch (code) {
	case MFI_EVT_PD_INSERTED_EXT:
		if (med->med_arg_type != MFI_EVT_ARGS_PD_ADDRESS)
			break;

		mfii_aen_pd_insert(sc, &med->args.pd_address);
		break;
 	case MFI_EVT_PD_REMOVED_EXT:
		if (med->med_arg_type != MFI_EVT_ARGS_PD_ADDRESS)
			break;

		mfii_aen_pd_remove(sc, &med->args.pd_address);
		break;

	case MFI_EVT_PD_STATE_CHANGE:
		if (med->med_arg_type != MFI_EVT_ARGS_PD_STATE)
			break;

		mfii_aen_pd_state_change(sc, &med->args.pd_state);
		break;

	case MFI_EVT_LD_CREATED:
	case MFI_EVT_LD_DELETED:
		mfii_aen_ld_update(sc);
		break;

	default:
		break;
	}

	mfii_aen_start(sc, ccb, mdm, lemtoh32(&med->med_seq_num) + 1);
}

void
mfii_aen_pd_insert(struct mfii_softc *sc,
    const struct mfi_evtarg_pd_address *pd)
{
#if 0
	printf("%s: pd inserted ext\n", DEVNAME(sc));
	printf("%s:  device_id %04x encl_id: %04x type %x\n", DEVNAME(sc),
	    lemtoh16(&pd->device_id), lemtoh16(&pd->encl_id),
	    pd->scsi_dev_type);
	printf("%s:  connected %02x addrs %016llx %016llx\n", DEVNAME(sc),
	    pd->connected.port_bitmap, lemtoh64(&pd->sas_addr[0]),
	    lemtoh64(&pd->sas_addr[1]));
#endif

	if (mfii_dev_handles_update(sc) != 0) /* refresh map */
		return;

	scsi_probe_target(sc->sc_pd->pd_scsibus, lemtoh16(&pd->device_id));
}

void
mfii_aen_pd_remove(struct mfii_softc *sc,
    const struct mfi_evtarg_pd_address *pd)
{
#if 0
	printf("%s: pd removed ext\n", DEVNAME(sc));
	printf("%s:  device_id %04x encl_id: %04x type %u\n", DEVNAME(sc),
	    lemtoh16(&pd->device_id), lemtoh16(&pd->encl_id),
	    pd->scsi_dev_type);
	printf("%s:  connected %02x addrs %016llx %016llx\n", DEVNAME(sc),
	    pd->connected.port_bitmap, lemtoh64(&pd->sas_addr[0]),
	    lemtoh64(&pd->sas_addr[1]));
#endif
	uint16_t target = lemtoh16(&pd->device_id);

	scsi_activate(sc->sc_pd->pd_scsibus, target, -1, DVACT_DEACTIVATE);

	/* the firmware will abort outstanding commands for us */

	scsi_detach_target(sc->sc_pd->pd_scsibus, target, DETACH_FORCE);
}

void
mfii_aen_pd_state_change(struct mfii_softc *sc,
    const struct mfi_evtarg_pd_state *state)
{
	uint16_t target = lemtoh16(&state->pd.mep_device_id);

	if (state->prev_state == htole32(MFI_PD_SYSTEM) &&
	    state->new_state != htole32(MFI_PD_SYSTEM)) {
		/* it's been pulled or configured for raid */

		scsi_activate(sc->sc_pd->pd_scsibus, target, -1,
		    DVACT_DEACTIVATE);
		/* outstanding commands will simply complete or get aborted */
		scsi_detach_target(sc->sc_pd->pd_scsibus, target,
		    DETACH_FORCE);

	} else if (state->prev_state == htole32(MFI_PD_UNCONFIG_GOOD) &&
	    state->new_state == htole32(MFI_PD_SYSTEM)) {
		/* the firmware is handing the disk over */

		scsi_probe_target(sc->sc_pd->pd_scsibus, target);
	}
}

void
mfii_aen_ld_update(struct mfii_softc *sc)
{
	int i, state, target, old, nld;
	int newlds[MFI_MAX_LD];

	if (mfii_mgmt(sc, MR_DCMD_LD_GET_LIST, NULL, &sc->sc_ld_list,
	    sizeof(sc->sc_ld_list), SCSI_DATA_IN) != 0) {
		DNPRINTF(MFII_D_MISC, "%s: getting list of logical disks failed\n",
		    DEVNAME(sc));
		return;
	}

	memset(newlds, -1, sizeof(newlds));

	for (i = 0; i < sc->sc_ld_list.mll_no_ld; i++) {
		state = sc->sc_ld_list.mll_list[i].mll_state;
		target = sc->sc_ld_list.mll_list[i].mll_ld.mld_target;
		DNPRINTF(MFII_D_MISC, "%s: target %d: state %d\n",
		    DEVNAME(sc), target, state);
		newlds[target] = i;
	}

	for (i = 0; i < MFI_MAX_LD; i++) {
		old = sc->sc_target_lds[i];
		nld = newlds[i];

		if (old == -1 && nld != -1) {
			DNPRINTF(MFII_D_MISC, "%s: attaching target %d\n",
			    DEVNAME(sc), i);

			scsi_probe_target(sc->sc_scsibus, i);

#ifndef SMALL_KERNEL
			mfii_init_ld_sensor(sc, nld);
			sensor_attach(&sc->sc_sensordev, &sc->sc_sensors[i]);
#endif
		} else if (nld == -1 && old != -1) {
			DNPRINTF(MFII_D_MISC, "%s: detaching target %d\n",
			    DEVNAME(sc), i);

			scsi_activate(sc->sc_scsibus, i, -1,
			    DVACT_DEACTIVATE);
			scsi_detach_target(sc->sc_scsibus, i,
			    DETACH_FORCE);
#ifndef SMALL_KERNEL
			sensor_detach(&sc->sc_sensordev, &sc->sc_sensors[i]);
#endif
		}
	}

	memcpy(sc->sc_target_lds, newlds, sizeof(sc->sc_target_lds));
}

void
mfii_aen_unregister(struct mfii_softc *sc)
{
	/* XXX */
}

int
mfii_reset_hard(struct mfii_softc *sc)
{
	u_int16_t		i;

	mfii_write(sc, MFI_OSTS, 0);

	/* enable diagnostic register */
	mfii_write(sc, MPII_WRITESEQ, MPII_WRITESEQ_FLUSH);
	mfii_write(sc, MPII_WRITESEQ, MPII_WRITESEQ_1);
	mfii_write(sc, MPII_WRITESEQ, MPII_WRITESEQ_2);
	mfii_write(sc, MPII_WRITESEQ, MPII_WRITESEQ_3);
	mfii_write(sc, MPII_WRITESEQ, MPII_WRITESEQ_4);
	mfii_write(sc, MPII_WRITESEQ, MPII_WRITESEQ_5);
	mfii_write(sc, MPII_WRITESEQ, MPII_WRITESEQ_6);

	delay(100);

	if ((mfii_read(sc, MPII_HOSTDIAG) & MPII_HOSTDIAG_DWRE) == 0) {
		printf("%s: failed to enable diagnostic read/write\n",
		    DEVNAME(sc));
		return(1);
	}

	/* reset ioc */
	mfii_write(sc, MPII_HOSTDIAG, MPII_HOSTDIAG_RESET_ADAPTER);

	/* 240 milliseconds */
	delay(240000);

	for (i = 0; i < 30000; i++) {
		if ((mfii_read(sc, MPII_HOSTDIAG) &
		    MPII_HOSTDIAG_RESET_ADAPTER) == 0)
			break;
		delay(10000);
	}
	if (i >= 30000) {
		printf("%s: failed to reset device\n", DEVNAME(sc));
		return (1);
	}

	/* disable diagnostic register */
	mfii_write(sc, MPII_WRITESEQ, 0xff);

	return(0);
}

int
mfii_transition_firmware(struct mfii_softc *sc)
{
	int32_t			fw_state, cur_state;
	int			max_wait, i, reset_on_fault = 1;

	fw_state = mfii_fw_state(sc) & MFI_STATE_MASK;

	while (fw_state != MFI_STATE_READY) {
		cur_state = fw_state;
		switch (fw_state) {
		case MFI_STATE_FAULT:
			if (!reset_on_fault) {
				printf("%s: firmware fault\n", DEVNAME(sc));
				return (1);
			}
			printf("%s: firmware fault; attempting full device "
			    "reset, this can take some time\n", DEVNAME(sc));
			if (mfii_reset_hard(sc))
				return (1);
			max_wait = 20;
			reset_on_fault = 0;
			break;
		case MFI_STATE_WAIT_HANDSHAKE:
			mfii_write(sc, MFI_SKINNY_IDB,
			    MFI_INIT_CLEAR_HANDSHAKE);
			max_wait = 2;
			break;
		case MFI_STATE_OPERATIONAL:
			mfii_write(sc, MFI_SKINNY_IDB, MFI_INIT_READY);
			max_wait = 10;
			break;
		case MFI_STATE_BB_INIT:
			max_wait = 20;
			break;
		case MFI_STATE_UNDEFINED:
		case MFI_STATE_FW_INIT:
		case MFI_STATE_FW_INIT_2:
		case MFI_STATE_DEVICE_SCAN:
		case MFI_STATE_FLUSH_CACHE:
			max_wait = 40;
			break;
		case MFI_STATE_BOOT_MESSAGE_PENDING:
			mfii_write(sc, MFI_SKINNY_IDB, MFI_INIT_HOTPLUG);
			max_wait = 10;
			break;
		default:
			printf("%s: unknown firmware state %#x\n",
			    DEVNAME(sc), fw_state);
			return (1);
		}
		for (i = 0; i < (max_wait * 10); i++) {
			fw_state = mfii_fw_state(sc) & MFI_STATE_MASK;
			if (fw_state == cur_state)
				DELAY(100000);
			else
				break;
		}
		if (fw_state == cur_state) {
			printf("%s: firmware stuck in state %#x\n",
			    DEVNAME(sc), fw_state);
			return (1);
		} else {
			DPRINTF("%s: firmware state change %#x -> %#x after "
			    "%d iterations\n",
			    DEVNAME(sc), cur_state, fw_state, i);
		}
	}

	return (0);
}

int
mfii_get_info(struct mfii_softc *sc)
{
	int i, rv;

	rv = mfii_mgmt(sc, MR_DCMD_CTRL_GET_INFO, NULL, &sc->sc_info,
	    sizeof(sc->sc_info), SCSI_DATA_IN|SCSI_NOSLEEP);

	if (rv != 0)
		return (rv);

	for (i = 0; i < sc->sc_info.mci_image_component_count; i++) {
		DPRINTF("%s: active FW %s Version %s date %s time %s\n",
		    DEVNAME(sc),
		    sc->sc_info.mci_image_component[i].mic_name,
		    sc->sc_info.mci_image_component[i].mic_version,
		    sc->sc_info.mci_image_component[i].mic_build_date,
		    sc->sc_info.mci_image_component[i].mic_build_time);
	}

	for (i = 0; i < sc->sc_info.mci_pending_image_component_count; i++) {
		DPRINTF("%s: pending FW %s Version %s date %s time %s\n",
		    DEVNAME(sc),
		    sc->sc_info.mci_pending_image_component[i].mic_name,
		    sc->sc_info.mci_pending_image_component[i].mic_version,
		    sc->sc_info.mci_pending_image_component[i].mic_build_date,
		    sc->sc_info.mci_pending_image_component[i].mic_build_time);
	}

	DPRINTF("%s: max_arms %d max_spans %d max_arrs %d max_lds %d name %s\n",
	    DEVNAME(sc),
	    sc->sc_info.mci_max_arms,
	    sc->sc_info.mci_max_spans,
	    sc->sc_info.mci_max_arrays,
	    sc->sc_info.mci_max_lds,
	    sc->sc_info.mci_product_name);

	DPRINTF("%s: serial %s present %#x fw time %d max_cmds %d max_sg %d\n",
	    DEVNAME(sc),
	    sc->sc_info.mci_serial_number,
	    sc->sc_info.mci_hw_present,
	    sc->sc_info.mci_current_fw_time,
	    sc->sc_info.mci_max_cmds,
	    sc->sc_info.mci_max_sg_elements);

	DPRINTF("%s: max_rq %d lds_pres %d lds_deg %d lds_off %d pd_pres %d\n",
	    DEVNAME(sc),
	    sc->sc_info.mci_max_request_size,
	    sc->sc_info.mci_lds_present,
	    sc->sc_info.mci_lds_degraded,
	    sc->sc_info.mci_lds_offline,
	    sc->sc_info.mci_pd_present);

	DPRINTF("%s: pd_dsk_prs %d pd_dsk_pred_fail %d pd_dsk_fail %d\n",
	    DEVNAME(sc),
	    sc->sc_info.mci_pd_disks_present,
	    sc->sc_info.mci_pd_disks_pred_failure,
	    sc->sc_info.mci_pd_disks_failed);

	DPRINTF("%s: nvram %d mem %d flash %d\n",
	    DEVNAME(sc),
	    sc->sc_info.mci_nvram_size,
	    sc->sc_info.mci_memory_size,
	    sc->sc_info.mci_flash_size);

	DPRINTF("%s: ram_cor %d ram_uncor %d clus_all %d clus_act %d\n",
	    DEVNAME(sc),
	    sc->sc_info.mci_ram_correctable_errors,
	    sc->sc_info.mci_ram_uncorrectable_errors,
	    sc->sc_info.mci_cluster_allowed,
	    sc->sc_info.mci_cluster_active);

	DPRINTF("%s: max_strps_io %d raid_lvl %#x adapt_ops %#x ld_ops %#x\n",
	    DEVNAME(sc),
	    sc->sc_info.mci_max_strips_per_io,
	    sc->sc_info.mci_raid_levels,
	    sc->sc_info.mci_adapter_ops,
	    sc->sc_info.mci_ld_ops);

	DPRINTF("%s: strp_sz_min %d strp_sz_max %d pd_ops %#x pd_mix %#x\n",
	    DEVNAME(sc),
	    sc->sc_info.mci_stripe_sz_ops.min,
	    sc->sc_info.mci_stripe_sz_ops.max,
	    sc->sc_info.mci_pd_ops,
	    sc->sc_info.mci_pd_mix_support);

	DPRINTF("%s: ecc_bucket %d pckg_prop %s\n",
	    DEVNAME(sc),
	    sc->sc_info.mci_ecc_bucket_count,
	    sc->sc_info.mci_package_version);

	DPRINTF("%s: sq_nm %d prd_fail_poll %d intr_thrtl %d intr_thrtl_to %d\n",
	    DEVNAME(sc),
	    sc->sc_info.mci_properties.mcp_seq_num,
	    sc->sc_info.mci_properties.mcp_pred_fail_poll_interval,
	    sc->sc_info.mci_properties.mcp_intr_throttle_cnt,
	    sc->sc_info.mci_properties.mcp_intr_throttle_timeout);

	DPRINTF("%s: rbld_rate %d patr_rd_rate %d bgi_rate %d cc_rate %d\n",
	    DEVNAME(sc),
	    sc->sc_info.mci_properties.mcp_rebuild_rate,
	    sc->sc_info.mci_properties.mcp_patrol_read_rate,
	    sc->sc_info.mci_properties.mcp_bgi_rate,
	    sc->sc_info.mci_properties.mcp_cc_rate);

	DPRINTF("%s: rc_rate %d ch_flsh %d spin_cnt %d spin_dly %d clus_en %d\n",
	    DEVNAME(sc),
	    sc->sc_info.mci_properties.mcp_recon_rate,
	    sc->sc_info.mci_properties.mcp_cache_flush_interval,
	    sc->sc_info.mci_properties.mcp_spinup_drv_cnt,
	    sc->sc_info.mci_properties.mcp_spinup_delay,
	    sc->sc_info.mci_properties.mcp_cluster_enable);

	DPRINTF("%s: coerc %d alarm %d dis_auto_rbld %d dis_bat_wrn %d ecc %d\n",
	    DEVNAME(sc),
	    sc->sc_info.mci_properties.mcp_coercion_mode,
	    sc->sc_info.mci_properties.mcp_alarm_enable,
	    sc->sc_info.mci_properties.mcp_disable_auto_rebuild,
	    sc->sc_info.mci_properties.mcp_disable_battery_warn,
	    sc->sc_info.mci_properties.mcp_ecc_bucket_size);

	DPRINTF("%s: ecc_leak %d rest_hs %d exp_encl_dev %d\n",
	    DEVNAME(sc),
	    sc->sc_info.mci_properties.mcp_ecc_bucket_leak_rate,
	    sc->sc_info.mci_properties.mcp_restore_hotspare_on_insertion,
	    sc->sc_info.mci_properties.mcp_expose_encl_devices);

	DPRINTF("%s: vendor %#x device %#x subvendor %#x subdevice %#x\n",
	    DEVNAME(sc),
	    sc->sc_info.mci_pci.mip_vendor,
	    sc->sc_info.mci_pci.mip_device,
	    sc->sc_info.mci_pci.mip_subvendor,
	    sc->sc_info.mci_pci.mip_subdevice);

	DPRINTF("%s: type %#x port_count %d port_addr ",
	    DEVNAME(sc),
	    sc->sc_info.mci_host.mih_type,
	    sc->sc_info.mci_host.mih_port_count);

	for (i = 0; i < 8; i++)
		DPRINTF("%.0llx ", sc->sc_info.mci_host.mih_port_addr[i]);
	DPRINTF("\n");

	DPRINTF("%s: type %.x port_count %d port_addr ",
	    DEVNAME(sc),
	    sc->sc_info.mci_device.mid_type,
	    sc->sc_info.mci_device.mid_port_count);

	for (i = 0; i < 8; i++)
		DPRINTF("%.0llx ", sc->sc_info.mci_device.mid_port_addr[i]);
	DPRINTF("\n");

	return (0);
}

int
mfii_mfa_poll(struct mfii_softc *sc, struct mfii_ccb *ccb)
{
	struct mfi_frame_header	*hdr = ccb->ccb_request;
	u_int64_t r;
	int to = 0, rv = 0;

#ifdef DIAGNOSTIC
	if (ccb->ccb_cookie != NULL || ccb->ccb_done != NULL)
		panic("mfii_mfa_poll called with cookie or done set");
#endif

	hdr->mfh_context = ccb->ccb_smid;
	hdr->mfh_cmd_status = MFI_STAT_INVALID_STATUS;
	hdr->mfh_flags |= htole16(MFI_FRAME_DONT_POST_IN_REPLY_QUEUE);

	r = MFII_REQ_MFA(ccb->ccb_request_dva);
	memcpy(&ccb->ccb_req, &r, sizeof(ccb->ccb_req));

	mfii_start(sc, ccb);

	for (;;) {
		bus_dmamap_sync(sc->sc_dmat, MFII_DMA_MAP(sc->sc_requests),
		    ccb->ccb_request_offset, MFII_REQUEST_SIZE,
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

		if (hdr->mfh_cmd_status != MFI_STAT_INVALID_STATUS)
			break;

		if (to++ > 5000) { /* XXX 5 seconds busywait sucks */
			printf("%s: timeout on ccb %d\n", DEVNAME(sc),
			    ccb->ccb_smid);
			ccb->ccb_flags |= MFI_CCB_F_ERR;
			rv = 1;
			break;
		}

		bus_dmamap_sync(sc->sc_dmat, MFII_DMA_MAP(sc->sc_requests),
		    ccb->ccb_request_offset, MFII_REQUEST_SIZE,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

		delay(1000);
	}

	if (ccb->ccb_len > 0) {
		bus_dmamap_sync(sc->sc_dmat, ccb->ccb_dmamap,
		    0, ccb->ccb_dmamap->dm_mapsize,
		    (ccb->ccb_direction == MFII_DATA_IN) ?
		    BUS_DMASYNC_POSTREAD : BUS_DMASYNC_POSTWRITE);

		bus_dmamap_unload(sc->sc_dmat, ccb->ccb_dmamap);
	}

	return (rv);
}

int
mfii_poll(struct mfii_softc *sc, struct mfii_ccb *ccb)
{
	void (*done)(struct mfii_softc *, struct mfii_ccb *);
	void *cookie;
	int rv = 1;

	done = ccb->ccb_done;
	cookie = ccb->ccb_cookie;

	ccb->ccb_done = mfii_poll_done;
	ccb->ccb_cookie = &rv;

	mfii_start(sc, ccb);

	do {
		delay(10);
		mfii_postq(sc);
	} while (rv == 1);

	ccb->ccb_cookie = cookie;
	done(sc, ccb);

	return (0);
}

void
mfii_poll_done(struct mfii_softc *sc, struct mfii_ccb *ccb)
{
	int *rv = ccb->ccb_cookie;

	*rv = 0;
}

int
mfii_exec(struct mfii_softc *sc, struct mfii_ccb *ccb)
{
	struct mutex m;

	mtx_init(&m, IPL_BIO);

#ifdef DIAGNOSTIC
	if (ccb->ccb_cookie != NULL || ccb->ccb_done != NULL)
		panic("mfii_exec called with cookie or done set");
#endif

	ccb->ccb_cookie = &m;
	ccb->ccb_done = mfii_exec_done;

	mfii_start(sc, ccb);

	mtx_enter(&m);
	while (ccb->ccb_cookie != NULL)
		msleep_nsec(ccb, &m, PRIBIO, "mfiiexec", INFSLP);
	mtx_leave(&m);

	return (0);
}

void
mfii_exec_done(struct mfii_softc *sc, struct mfii_ccb *ccb)
{
	struct mutex *m = ccb->ccb_cookie;

	mtx_enter(m);
	ccb->ccb_cookie = NULL;
	wakeup_one(ccb);
	mtx_leave(m);
}

int
mfii_mgmt(struct mfii_softc *sc, uint32_t opc, const union mfi_mbox *mbox,
    void *buf, size_t len, int flags)
{
	struct mfii_ccb *ccb;
	int rv;

	ccb = scsi_io_get(&sc->sc_iopool, flags);
	if (ccb == NULL)
		return (ENOMEM);

	mfii_scrub_ccb(ccb);
	rv = mfii_do_mgmt(sc, ccb, opc, mbox, buf, len, flags);
	scsi_io_put(&sc->sc_iopool, ccb);

	return (rv);
}

int
mfii_do_mgmt(struct mfii_softc *sc, struct mfii_ccb *ccb, uint32_t opc,
    const union mfi_mbox *mbox, void *buf, size_t len, int flags)
{
	struct mpii_msg_scsi_io *io = ccb->ccb_request;
	struct mfii_raid_context *ctx = (struct mfii_raid_context *)(io + 1);
	struct mfii_sge *sge = (struct mfii_sge *)(ctx + 1);
	struct mfi_dcmd_frame *dcmd = ccb->ccb_mfi;
	struct mfi_frame_header *hdr = &dcmd->mdf_header;
	u_int8_t *dma_buf = NULL;
	int rv = EIO;

	if (cold)
		flags |= SCSI_NOSLEEP;

	if (buf != NULL) {
		dma_buf = dma_alloc(len, PR_WAITOK);
		if (dma_buf == NULL)
			return (ENOMEM);
	}

	ccb->ccb_data = dma_buf;
	ccb->ccb_len = len;
	switch (flags & (SCSI_DATA_IN | SCSI_DATA_OUT)) {
	case SCSI_DATA_IN:
		ccb->ccb_direction = MFII_DATA_IN;
		hdr->mfh_flags = htole16(MFI_FRAME_DIR_READ);
		break;
	case SCSI_DATA_OUT:
		ccb->ccb_direction = MFII_DATA_OUT;
		hdr->mfh_flags = htole16(MFI_FRAME_DIR_WRITE);
		memcpy(dma_buf, buf, len);
		break;
	case 0:
		ccb->ccb_direction = MFII_DATA_NONE;
		hdr->mfh_flags = htole16(MFI_FRAME_DIR_NONE);
		break;
	}

	if (mfii_load_mfa(sc, ccb, &dcmd->mdf_sgl,
	    ISSET(flags, SCSI_NOSLEEP)) != 0) {
		rv = ENOMEM;
		goto done;
	}

	hdr->mfh_cmd = MFI_CMD_DCMD;
	hdr->mfh_context = ccb->ccb_smid;
	hdr->mfh_data_len = htole32(len);
	hdr->mfh_sg_count = len ? ccb->ccb_dmamap->dm_nsegs : 0;

	dcmd->mdf_opcode = opc;
	/* handle special opcodes */
	if (mbox != NULL)
		memcpy(&dcmd->mdf_mbox, mbox, sizeof(dcmd->mdf_mbox));

	io->function = MFII_FUNCTION_PASSTHRU_IO;

	if (len) {
		io->sgl_offset0 = ((u_int8_t *)sge - (u_int8_t *)io) / 4;
		io->chain_offset = ((u_int8_t *)sge - (u_int8_t *)io) / 16;
		htolem64(&sge->sg_addr, ccb->ccb_mfi_dva);
		htolem32(&sge->sg_len, MFI_FRAME_SIZE);
		sge->sg_flags =
		    MFII_SGE_CHAIN_ELEMENT | MFII_SGE_ADDR_IOCPLBNTA;
	}

	ccb->ccb_req.flags = MFII_REQ_TYPE_SCSI;
	ccb->ccb_req.smid = letoh16(ccb->ccb_smid);

	if (ISSET(flags, SCSI_NOSLEEP)) {
		ccb->ccb_done = mfii_empty_done;
		mfii_poll(sc, ccb);
	} else
		mfii_exec(sc, ccb);

	if (hdr->mfh_cmd_status == MFI_STAT_OK) {
		rv = 0;

		if (ccb->ccb_direction == MFII_DATA_IN)
			memcpy(buf, dma_buf, len);
	}

done:
	if (buf != NULL)
		dma_free(dma_buf, len);

	return (rv);
}

void
mfii_empty_done(struct mfii_softc *sc, struct mfii_ccb *ccb)
{
	return;
}

int
mfii_load_mfa(struct mfii_softc *sc, struct mfii_ccb *ccb,
    void *sglp, int nosleep)
{
	union mfi_sgl *sgl = sglp;
	bus_dmamap_t dmap = ccb->ccb_dmamap;
	int error;
	int i;

	if (ccb->ccb_len == 0)
		return (0);

	error = bus_dmamap_load(sc->sc_dmat, dmap,
	    ccb->ccb_data, ccb->ccb_len, NULL,
	    nosleep ? BUS_DMA_NOWAIT : BUS_DMA_WAITOK);
	if (error) {
		printf("%s: error %d loading dmamap\n", DEVNAME(sc), error);
		return (1);
	}

	for (i = 0; i < dmap->dm_nsegs; i++) {
		sgl->sg32[i].addr = htole32(dmap->dm_segs[i].ds_addr);
		sgl->sg32[i].len = htole32(dmap->dm_segs[i].ds_len);
	}

	bus_dmamap_sync(sc->sc_dmat, dmap, 0, dmap->dm_mapsize,
	    ccb->ccb_direction == MFII_DATA_OUT ?
	    BUS_DMASYNC_PREWRITE : BUS_DMASYNC_PREREAD);

	return (0);
}

void
mfii_start(struct mfii_softc *sc, struct mfii_ccb *ccb)
{
	u_long *r = (u_long *)&ccb->ccb_req;

	bus_dmamap_sync(sc->sc_dmat, MFII_DMA_MAP(sc->sc_requests),
	    ccb->ccb_request_offset, MFII_REQUEST_SIZE,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

#if defined(__LP64__)
	bus_space_write_raw_8(sc->sc_iot, sc->sc_ioh, MFI_IQPL, *r);
#else
	mtx_enter(&sc->sc_post_mtx);
	bus_space_write_raw_4(sc->sc_iot, sc->sc_ioh, MFI_IQPL, r[0]);
	bus_space_barrier(sc->sc_iot, sc->sc_ioh,
	    MFI_IQPL, 8, BUS_SPACE_BARRIER_WRITE);

	bus_space_write_raw_4(sc->sc_iot, sc->sc_ioh, MFI_IQPH, r[1]);
	bus_space_barrier(sc->sc_iot, sc->sc_ioh,
	    MFI_IQPH, 8, BUS_SPACE_BARRIER_WRITE);
	mtx_leave(&sc->sc_post_mtx);
#endif
}

void
mfii_done(struct mfii_softc *sc, struct mfii_ccb *ccb)
{
	bus_dmamap_sync(sc->sc_dmat, MFII_DMA_MAP(sc->sc_requests),
	    ccb->ccb_request_offset, MFII_REQUEST_SIZE,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	if (ccb->ccb_sgl_len > 0) {
		bus_dmamap_sync(sc->sc_dmat, MFII_DMA_MAP(sc->sc_sgl),
		    ccb->ccb_sgl_offset, ccb->ccb_sgl_len,
		    BUS_DMASYNC_POSTWRITE);
	}

	if (ccb->ccb_len > 0) {
		bus_dmamap_sync(sc->sc_dmat, ccb->ccb_dmamap,
		    0, ccb->ccb_dmamap->dm_mapsize,
		    (ccb->ccb_direction == MFII_DATA_IN) ?
		    BUS_DMASYNC_POSTREAD : BUS_DMASYNC_POSTWRITE);

		bus_dmamap_unload(sc->sc_dmat, ccb->ccb_dmamap);
	}

	ccb->ccb_done(sc, ccb);
}

int
mfii_initialise_firmware(struct mfii_softc *sc)
{
	struct mpii_msg_iocinit_request *iiq;
	struct mfii_dmamem *m;
	struct mfii_ccb *ccb;
	struct mfi_init_frame *init;
	int rv;

	m = mfii_dmamem_alloc(sc, sizeof(*iiq));
	if (m == NULL)
		return (1);

	iiq = MFII_DMA_KVA(m);
	memset(iiq, 0, sizeof(*iiq));

	iiq->function = MPII_FUNCTION_IOC_INIT;
	iiq->whoinit = MPII_WHOINIT_HOST_DRIVER;

	iiq->msg_version_maj = 0x02;
	iiq->msg_version_min = 0x00;
	iiq->hdr_version_unit = 0x10;
	iiq->hdr_version_dev = 0x0;

	iiq->system_request_frame_size = htole16(MFII_REQUEST_SIZE / 4);

	iiq->reply_descriptor_post_queue_depth =
	    htole16(sc->sc_reply_postq_depth);
	iiq->reply_free_queue_depth = htole16(0);

	htolem32(&iiq->sense_buffer_address_high,
	    MFII_DMA_DVA(sc->sc_sense) >> 32);

	htolem32(&iiq->reply_descriptor_post_queue_address_lo,
	    MFII_DMA_DVA(sc->sc_reply_postq));
	htolem32(&iiq->reply_descriptor_post_queue_address_hi,
	    MFII_DMA_DVA(sc->sc_reply_postq) >> 32);

	htolem32(&iiq->system_request_frame_base_address_lo,
	    MFII_DMA_DVA(sc->sc_requests));
	htolem32(&iiq->system_request_frame_base_address_hi,
	    MFII_DMA_DVA(sc->sc_requests) >> 32);

	iiq->timestamp = htole64(getuptime());

	ccb = scsi_io_get(&sc->sc_iopool, SCSI_NOSLEEP);
	if (ccb == NULL) {
		/* shouldn't ever run out of ccbs during attach */
		return (1);
	}
	mfii_scrub_ccb(ccb);
	init = ccb->ccb_request;

	init->mif_header.mfh_cmd = MFI_CMD_INIT;
	init->mif_header.mfh_data_len = htole32(sizeof(*iiq));
	init->mif_qinfo_new_addr = htole64(MFII_DMA_DVA(m));

	bus_dmamap_sync(sc->sc_dmat, MFII_DMA_MAP(sc->sc_reply_postq),
	    0, MFII_DMA_LEN(sc->sc_reply_postq),
	    BUS_DMASYNC_PREREAD);

	bus_dmamap_sync(sc->sc_dmat, MFII_DMA_MAP(m),
	    0, sizeof(*iiq), BUS_DMASYNC_PREREAD);

	rv = mfii_mfa_poll(sc, ccb);

	bus_dmamap_sync(sc->sc_dmat, MFII_DMA_MAP(m),
	    0, sizeof(*iiq), BUS_DMASYNC_POSTREAD);

	scsi_io_put(&sc->sc_iopool, ccb);
	mfii_dmamem_free(sc, m);

	return (rv);
}

int
mfii_my_intr(struct mfii_softc *sc)
{
	u_int32_t status;

	status = mfii_read(sc, MFI_OSTS);
	if (ISSET(status, 0x1)) {
		mfii_write(sc, MFI_OSTS, status);
		return (1);
	}

	return (ISSET(status, MFII_OSTS_INTR_VALID) ? 1 : 0);
}

int
mfii_intr(void *arg)
{
	struct mfii_softc *sc = arg;

	if (!mfii_my_intr(sc))
		return (0);

	mfii_postq(sc);

	return (1);
}

void
mfii_postq(struct mfii_softc *sc)
{
	struct mfii_ccb_list ccbs = SIMPLEQ_HEAD_INITIALIZER(ccbs);
	struct mpii_reply_descr *postq = MFII_DMA_KVA(sc->sc_reply_postq);
	struct mpii_reply_descr *rdp;
	struct mfii_ccb *ccb;
	int rpi = 0;

	mtx_enter(&sc->sc_reply_postq_mtx);

	bus_dmamap_sync(sc->sc_dmat, MFII_DMA_MAP(sc->sc_reply_postq),
	    0, MFII_DMA_LEN(sc->sc_reply_postq),
	    BUS_DMASYNC_POSTREAD);

	for (;;) {
		rdp = &postq[sc->sc_reply_postq_index];
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

		ccb = &sc->sc_ccb[letoh16(rdp->smid) - 1];
		SIMPLEQ_INSERT_TAIL(&ccbs, ccb, ccb_link);
		memset(rdp, 0xff, sizeof(*rdp));

		sc->sc_reply_postq_index++;
		sc->sc_reply_postq_index %= sc->sc_reply_postq_depth;
		rpi = 1;
	}

	bus_dmamap_sync(sc->sc_dmat, MFII_DMA_MAP(sc->sc_reply_postq),
	    0, MFII_DMA_LEN(sc->sc_reply_postq),
	    BUS_DMASYNC_PREREAD);

	if (rpi)
		mfii_write(sc, MFII_RPI, sc->sc_reply_postq_index);

	mtx_leave(&sc->sc_reply_postq_mtx);

	while ((ccb = SIMPLEQ_FIRST(&ccbs)) != NULL) {
		SIMPLEQ_REMOVE_HEAD(&ccbs, ccb_link);
		mfii_done(sc, ccb);
	}
}

void
mfii_scsi_cmd(struct scsi_xfer *xs)
{
	struct scsi_link *link = xs->sc_link;
	struct mfii_softc *sc = link->bus->sb_adapter_softc;
	struct mfii_ccb *ccb = xs->io;

	mfii_scrub_ccb(ccb);
	ccb->ccb_cookie = xs;
	ccb->ccb_done = mfii_scsi_cmd_done;
	ccb->ccb_data = xs->data;
	ccb->ccb_len = xs->datalen;

	timeout_set(&xs->stimeout, mfii_scsi_cmd_tmo, xs);

	switch (xs->cmd.opcode) {
	case READ_COMMAND:
	case READ_10:
	case READ_12:
	case READ_16:
	case WRITE_COMMAND:
	case WRITE_10:
	case WRITE_12:
	case WRITE_16:
		if (mfii_scsi_cmd_io(sc, xs) != 0)
			goto stuffup;

		break;

	default:
		if (mfii_scsi_cmd_cdb(sc, xs) != 0)
			goto stuffup;
		break;
	}

	xs->error = XS_NOERROR;
	xs->resid = 0;

	if (ISSET(xs->flags, SCSI_POLL)) {
		if (mfii_poll(sc, ccb) != 0)
			goto stuffup;
		return;
	}

	ccb->ccb_refcnt = 2; /* one for the chip, one for the timeout */
	timeout_add_msec(&xs->stimeout, xs->timeout);
	mfii_start(sc, ccb);

	return;

stuffup:
	xs->error = XS_DRIVER_STUFFUP;
	scsi_done(xs);
}

void
mfii_scsi_cmd_done(struct mfii_softc *sc, struct mfii_ccb *ccb)
{
	struct scsi_xfer *xs = ccb->ccb_cookie;
	struct mpii_msg_scsi_io *io = ccb->ccb_request;
	struct mfii_raid_context *ctx = (struct mfii_raid_context *)(io + 1);
	u_int refs = 1;

	if (timeout_del(&xs->stimeout))
		refs = 2;

	switch (ctx->status) {
	case MFI_STAT_OK:
		break;

	case MFI_STAT_SCSI_DONE_WITH_ERROR:
		xs->error = XS_SENSE;
		memset(&xs->sense, 0, sizeof(xs->sense));
		memcpy(&xs->sense, ccb->ccb_sense, sizeof(xs->sense));
		break;

	case MFI_STAT_LD_OFFLINE:
	case MFI_STAT_DEVICE_NOT_FOUND:
		xs->error = XS_SELTIMEOUT;
		break;

	default:
		xs->error = XS_DRIVER_STUFFUP;
		break;
	}

	if (atomic_sub_int_nv(&ccb->ccb_refcnt, refs) == 0)
		scsi_done(xs);
}

int
mfii_scsi_ioctl(struct scsi_link *link, u_long cmd, caddr_t addr, int flag)
{
	struct mfii_softc	*sc = link->bus->sb_adapter_softc;

	DNPRINTF(MFII_D_IOCTL, "%s: mfii_scsi_ioctl\n", DEVNAME(sc));

	switch (cmd) {
	case DIOCGCACHE:
	case DIOCSCACHE:
		return (mfii_ioctl_cache(link, cmd, (struct dk_cache *)addr));
		break;

	default:
		if (sc->sc_ioctl)
			return (sc->sc_ioctl(&sc->sc_dev, cmd, addr));
		break;
	}

	return (ENOTTY);
}

int
mfii_ioctl_cache(struct scsi_link *link, u_long cmd,  struct dk_cache *dc)
{
	struct mfii_softc	*sc = link->bus->sb_adapter_softc;
	int			 rv, wrenable, rdenable;
	struct mfi_ld_prop	 ldp;
	union mfi_mbox		 mbox;

	if (mfii_get_info(sc)) {
		rv = EIO;
		goto done;
	}

	if (sc->sc_target_lds[link->target] == -1) {
		rv = EIO;
		goto done;
	}

	memset(&mbox, 0, sizeof(mbox));
	mbox.b[0] = link->target;
	rv = mfii_mgmt(sc, MR_DCMD_LD_GET_PROPERTIES, &mbox, &ldp, sizeof(ldp),
	    SCSI_DATA_IN);
	if (rv != 0)
		goto done;

	if (sc->sc_info.mci_memory_size > 0) {
		wrenable = ISSET(ldp.mlp_cur_cache_policy,
		    MR_LD_CACHE_ALLOW_WRITE_CACHE)? 1 : 0;
		rdenable = ISSET(ldp.mlp_cur_cache_policy,
		    MR_LD_CACHE_ALLOW_READ_CACHE)? 1 : 0;
	} else {
		wrenable = ISSET(ldp.mlp_diskcache_policy,
		    MR_LD_DISK_CACHE_ENABLE)? 1 : 0;
		rdenable = 0;
	}

	if (cmd == DIOCGCACHE) {
		dc->wrcache = wrenable;
		dc->rdcache = rdenable;
		goto done;
	} /* else DIOCSCACHE */

	if (((dc->wrcache) ? 1 : 0) == wrenable &&
	    ((dc->rdcache) ? 1 : 0) == rdenable)
		goto done;

	memset(&mbox, 0, sizeof(mbox));
	mbox.b[0] = ldp.mlp_ld.mld_target;
	mbox.b[1] = ldp.mlp_ld.mld_res;
	mbox.s[1] = ldp.mlp_ld.mld_seq;

	if (sc->sc_info.mci_memory_size > 0) {
		if (dc->rdcache)
			SET(ldp.mlp_cur_cache_policy,
			    MR_LD_CACHE_ALLOW_READ_CACHE);
		else
			CLR(ldp.mlp_cur_cache_policy,
			    MR_LD_CACHE_ALLOW_READ_CACHE);
		if (dc->wrcache)
			SET(ldp.mlp_cur_cache_policy,
			    MR_LD_CACHE_ALLOW_WRITE_CACHE);
		else
			CLR(ldp.mlp_cur_cache_policy,
			    MR_LD_CACHE_ALLOW_WRITE_CACHE);
	} else {
		if (dc->rdcache) {
			rv = EOPNOTSUPP;
			goto done;
		}
		if (dc->wrcache)
			ldp.mlp_diskcache_policy = MR_LD_DISK_CACHE_ENABLE;
		else
			ldp.mlp_diskcache_policy = MR_LD_DISK_CACHE_DISABLE;
	}

	rv = mfii_mgmt(sc, MR_DCMD_LD_SET_PROPERTIES, &mbox, &ldp, sizeof(ldp),
	    SCSI_DATA_OUT);
done:
	return (rv);
}

int
mfii_scsi_cmd_io(struct mfii_softc *sc, struct scsi_xfer *xs)
{
	struct scsi_link *link = xs->sc_link;
	struct mfii_ccb *ccb = xs->io;
	struct mpii_msg_scsi_io *io = ccb->ccb_request;
	struct mfii_raid_context *ctx = (struct mfii_raid_context *)(io + 1);
	int segs;

	io->dev_handle = htole16(link->target);
	io->function = MFII_FUNCTION_LDIO_REQUEST;
	io->sense_buffer_low_address = htole32(ccb->ccb_sense_dva);
	io->sgl_flags = htole16(0x02); /* XXX */
	io->sense_buffer_length = sizeof(xs->sense);
	io->sgl_offset0 = (sizeof(*io) + sizeof(*ctx)) / 4;
	io->data_length = htole32(xs->datalen);
	io->io_flags = htole16(xs->cmdlen);
	switch (xs->flags & (SCSI_DATA_IN | SCSI_DATA_OUT)) {
	case SCSI_DATA_IN:
		ccb->ccb_direction = MFII_DATA_IN;
		io->direction = MPII_SCSIIO_DIR_READ;
		break;
	case SCSI_DATA_OUT:
		ccb->ccb_direction = MFII_DATA_OUT;
		io->direction = MPII_SCSIIO_DIR_WRITE;
		break;
	default:
		ccb->ccb_direction = MFII_DATA_NONE;
		io->direction = MPII_SCSIIO_DIR_NONE;
		break;
	}
	memcpy(io->cdb, &xs->cmd, xs->cmdlen);

	ctx->type_nseg = sc->sc_iop->ldio_ctx_type_nseg;
	ctx->timeout_value = htole16(0x14); /* XXX */
	ctx->reg_lock_flags = htole16(sc->sc_iop->ldio_ctx_reg_lock_flags);
	ctx->virtual_disk_target_id = htole16(link->target);

	if (mfii_load_ccb(sc, ccb, ctx + 1,
	    ISSET(xs->flags, SCSI_NOSLEEP)) != 0)
		return (1);

	segs = (ccb->ccb_len == 0) ? 0 : ccb->ccb_dmamap->dm_nsegs;
	switch (sc->sc_iop->num_sge_loc) {
	case MFII_IOP_NUM_SGE_LOC_ORIG:
		ctx->num_sge = segs;
		break;
	case MFII_IOP_NUM_SGE_LOC_35:
		/* 12 bit field, but we're only using the lower 8 */
		ctx->span_arm = segs;
		break;
	}

	ccb->ccb_req.flags = sc->sc_iop->ldio_req_type;
	ccb->ccb_req.smid = letoh16(ccb->ccb_smid);

	return (0);
}

int
mfii_scsi_cmd_cdb(struct mfii_softc *sc, struct scsi_xfer *xs)
{
	struct scsi_link *link = xs->sc_link;
	struct mfii_ccb *ccb = xs->io;
	struct mpii_msg_scsi_io *io = ccb->ccb_request;
	struct mfii_raid_context *ctx = (struct mfii_raid_context *)(io + 1);

	io->dev_handle = htole16(link->target);
	io->function = MFII_FUNCTION_LDIO_REQUEST;
	io->sense_buffer_low_address = htole32(ccb->ccb_sense_dva);
	io->sgl_flags = htole16(0x02); /* XXX */
	io->sense_buffer_length = sizeof(xs->sense);
	io->sgl_offset0 = (sizeof(*io) + sizeof(*ctx)) / 4;
	io->data_length = htole32(xs->datalen);
	io->io_flags = htole16(xs->cmdlen);
	io->lun[0] = htobe16(link->lun);
	switch (xs->flags & (SCSI_DATA_IN | SCSI_DATA_OUT)) {
	case SCSI_DATA_IN:
		ccb->ccb_direction = MFII_DATA_IN;
		io->direction = MPII_SCSIIO_DIR_READ;
		break;
	case SCSI_DATA_OUT:
		ccb->ccb_direction = MFII_DATA_OUT;
		io->direction = MPII_SCSIIO_DIR_WRITE;
		break;
	default:
		ccb->ccb_direction = MFII_DATA_NONE;
		io->direction = MPII_SCSIIO_DIR_NONE;
		break;
	}
	memcpy(io->cdb, &xs->cmd, xs->cmdlen);

	ctx->virtual_disk_target_id = htole16(link->target);

	if (mfii_load_ccb(sc, ccb, ctx + 1,
	    ISSET(xs->flags, SCSI_NOSLEEP)) != 0)
		return (1);

	ctx->num_sge = (ccb->ccb_len == 0) ? 0 : ccb->ccb_dmamap->dm_nsegs;

	ccb->ccb_req.flags = MFII_REQ_TYPE_SCSI;
	ccb->ccb_req.smid = letoh16(ccb->ccb_smid);

	return (0);
}

void
mfii_pd_scsi_cmd(struct scsi_xfer *xs)
{
	struct scsi_link *link = xs->sc_link;
	struct mfii_softc *sc = link->bus->sb_adapter_softc;
	struct mfii_ccb *ccb = xs->io;

	mfii_scrub_ccb(ccb);
	ccb->ccb_cookie = xs;
	ccb->ccb_done = mfii_scsi_cmd_done;
	ccb->ccb_data = xs->data;
	ccb->ccb_len = xs->datalen;

	timeout_set(&xs->stimeout, mfii_scsi_cmd_tmo, xs);

	xs->error = mfii_pd_scsi_cmd_cdb(sc, xs);
	if (xs->error != XS_NOERROR)
		goto done;

	xs->resid = 0;

	if (ISSET(xs->flags, SCSI_POLL)) {
		if (mfii_poll(sc, ccb) != 0)
			goto stuffup;
		return;
	}

	ccb->ccb_refcnt = 2; /* one for the chip, one for the timeout */
	timeout_add_msec(&xs->stimeout, xs->timeout);
	mfii_start(sc, ccb);

	return;

stuffup:
	xs->error = XS_DRIVER_STUFFUP;
done:
	scsi_done(xs);
}

int
mfii_pd_scsi_probe(struct scsi_link *link)
{
	struct mfii_softc *sc = link->bus->sb_adapter_softc;
	struct mfi_pd_details mpd;
	union mfi_mbox mbox;
	int rv;

	if (link->lun > 0)
		return (0);

	memset(&mbox, 0, sizeof(mbox));
	mbox.s[0] = htole16(link->target);

	rv = mfii_mgmt(sc, MR_DCMD_PD_GET_INFO, &mbox, &mpd, sizeof(mpd),
	    SCSI_DATA_IN|SCSI_NOSLEEP);
	if (rv != 0)
		return (EIO);

	if (mpd.mpd_fw_state != htole16(MFI_PD_SYSTEM))
		return (ENXIO);

	return (0);
}

int
mfii_pd_scsi_cmd_cdb(struct mfii_softc *sc, struct scsi_xfer *xs)
{
	struct scsi_link *link = xs->sc_link;
	struct mfii_ccb *ccb = xs->io;
	struct mpii_msg_scsi_io *io = ccb->ccb_request;
	struct mfii_raid_context *ctx = (struct mfii_raid_context *)(io + 1);
	uint16_t dev_handle;

	dev_handle = mfii_dev_handle(sc, link->target);
	if (dev_handle == htole16(0xffff))
		return (XS_SELTIMEOUT);

	io->dev_handle = dev_handle;
	io->function = 0;
	io->sense_buffer_low_address = htole32(ccb->ccb_sense_dva);
	io->sgl_flags = htole16(0x02); /* XXX */
	io->sense_buffer_length = sizeof(xs->sense);
	io->sgl_offset0 = (sizeof(*io) + sizeof(*ctx)) / 4;
	io->data_length = htole32(xs->datalen);
	io->io_flags = htole16(xs->cmdlen);
	io->lun[0] = htobe16(link->lun);
	switch (xs->flags & (SCSI_DATA_IN | SCSI_DATA_OUT)) {
	case SCSI_DATA_IN:
		ccb->ccb_direction = MFII_DATA_IN;
		io->direction = MPII_SCSIIO_DIR_READ;
		break;
	case SCSI_DATA_OUT:
		ccb->ccb_direction = MFII_DATA_OUT;
		io->direction = MPII_SCSIIO_DIR_WRITE;
		break;
	default:
		ccb->ccb_direction = MFII_DATA_NONE;
		io->direction = MPII_SCSIIO_DIR_NONE;
		break;
	}
	memcpy(io->cdb, &xs->cmd, xs->cmdlen);

	ctx->virtual_disk_target_id = htole16(link->target);
	ctx->raid_flags = MFII_RAID_CTX_IO_TYPE_SYSPD;
	ctx->timeout_value = sc->sc_pd->pd_timeout;

	if (mfii_load_ccb(sc, ccb, ctx + 1,
	    ISSET(xs->flags, SCSI_NOSLEEP)) != 0)
		return (XS_DRIVER_STUFFUP);

	ctx->num_sge = (ccb->ccb_len == 0) ? 0 : ccb->ccb_dmamap->dm_nsegs;

	ccb->ccb_req.flags = MFII_REQ_TYPE_HI_PRI;
	ccb->ccb_req.smid = letoh16(ccb->ccb_smid);
	ccb->ccb_req.dev_handle = dev_handle;

	return (XS_NOERROR);
}

int
mfii_load_ccb(struct mfii_softc *sc, struct mfii_ccb *ccb, void *sglp,
    int nosleep)
{
	struct mpii_msg_request *req = ccb->ccb_request;
	struct mfii_sge *sge = NULL, *nsge = sglp;
	struct mfii_sge *ce = NULL;
	bus_dmamap_t dmap = ccb->ccb_dmamap;
	u_int space;
	int i;

	int error;

	if (ccb->ccb_len == 0)
		return (0);

	error = bus_dmamap_load(sc->sc_dmat, dmap,
	    ccb->ccb_data, ccb->ccb_len, NULL,
	    nosleep ? BUS_DMA_NOWAIT : BUS_DMA_WAITOK);
	if (error) {
		printf("%s: error %d loading dmamap\n", DEVNAME(sc), error);
		return (1);
	}

	space = (MFII_REQUEST_SIZE - ((u_int8_t *)nsge - (u_int8_t *)req)) /
	    sizeof(*nsge);
	if (dmap->dm_nsegs > space) {
		space--;

		ccb->ccb_sgl_len = (dmap->dm_nsegs - space) * sizeof(*nsge);
		memset(ccb->ccb_sgl, 0, ccb->ccb_sgl_len);

		ce = nsge + space;
		ce->sg_addr = htole64(ccb->ccb_sgl_dva);
		ce->sg_len = htole32(ccb->ccb_sgl_len);
		ce->sg_flags = sc->sc_iop->sge_flag_chain;

		req->chain_offset = ((u_int8_t *)ce - (u_int8_t *)req) / 16;
	}

	for (i = 0; i < dmap->dm_nsegs; i++) {
		if (nsge == ce)
			nsge = ccb->ccb_sgl;

		sge = nsge;

		sge->sg_addr = htole64(dmap->dm_segs[i].ds_addr);
		sge->sg_len = htole32(dmap->dm_segs[i].ds_len);
		sge->sg_flags = MFII_SGE_ADDR_SYSTEM;

		nsge = sge + 1;
	}
	sge->sg_flags |= sc->sc_iop->sge_flag_eol;

	bus_dmamap_sync(sc->sc_dmat, dmap, 0, dmap->dm_mapsize,
	    ccb->ccb_direction == MFII_DATA_OUT ?
	    BUS_DMASYNC_PREWRITE : BUS_DMASYNC_PREREAD);

	if (ccb->ccb_sgl_len > 0) {
		bus_dmamap_sync(sc->sc_dmat, MFII_DMA_MAP(sc->sc_sgl),
		    ccb->ccb_sgl_offset, ccb->ccb_sgl_len,
		    BUS_DMASYNC_PREWRITE);
	}

	return (0);
}

void
mfii_scsi_cmd_tmo(void *xsp)
{
	struct scsi_xfer *xs = xsp;
	struct scsi_link *link = xs->sc_link;
	struct mfii_softc *sc = link->bus->sb_adapter_softc;
	struct mfii_ccb *ccb = xs->io;

	mtx_enter(&sc->sc_abort_mtx);
	SIMPLEQ_INSERT_TAIL(&sc->sc_abort_list, ccb, ccb_link);
	mtx_leave(&sc->sc_abort_mtx);

	task_add(systqmp, &sc->sc_abort_task);
}

void
mfii_abort_task(void *scp)
{
	struct mfii_softc *sc = scp;
	struct mfii_ccb *list;

	mtx_enter(&sc->sc_abort_mtx);
	list = SIMPLEQ_FIRST(&sc->sc_abort_list);
	SIMPLEQ_INIT(&sc->sc_abort_list);
	mtx_leave(&sc->sc_abort_mtx);

	while (list != NULL) {
		struct mfii_ccb *ccb = list;
		struct scsi_xfer *xs = ccb->ccb_cookie;
		struct scsi_link *link = xs->sc_link;

		uint16_t dev_handle;
		struct mfii_ccb *accb;

		list = SIMPLEQ_NEXT(ccb, ccb_link);

		dev_handle = mfii_dev_handle(sc, link->target);
		if (dev_handle == htole16(0xffff)) {
			/* device is gone */
			if (atomic_dec_int_nv(&ccb->ccb_refcnt) == 0)
				scsi_done(xs);
			continue;
		}

		accb = scsi_io_get(&sc->sc_iopool, 0);
		mfii_scrub_ccb(accb);
		mfii_abort(sc, accb, dev_handle, ccb->ccb_smid,
		    MPII_SCSI_TASK_ABORT_TASK,
		    htole32(MFII_TASK_MGMT_FLAGS_PD));

		accb->ccb_cookie = ccb;
		accb->ccb_done = mfii_scsi_cmd_abort_done;

		mfii_start(sc, accb);
	}
}

void
mfii_abort(struct mfii_softc *sc, struct mfii_ccb *accb, uint16_t dev_handle,
    uint16_t smid, uint8_t type, uint32_t flags)
{
	struct mfii_task_mgmt *msg;
	struct mpii_msg_scsi_task_request *req;

	msg = accb->ccb_request;
	req = &msg->mpii_request;
	req->dev_handle = dev_handle;
	req->function = MPII_FUNCTION_SCSI_TASK_MGMT;
	req->task_type = type;
	htolem16(&req->task_mid, smid);
	msg->flags = flags;

	accb->ccb_req.flags = MFII_REQ_TYPE_HI_PRI;
	accb->ccb_req.smid = letoh16(accb->ccb_smid);
}

void
mfii_scsi_cmd_abort_done(struct mfii_softc *sc, struct mfii_ccb *accb)
{
	struct mfii_ccb *ccb = accb->ccb_cookie;
	struct scsi_xfer *xs = ccb->ccb_cookie;

	/* XXX check accb completion? */

	scsi_io_put(&sc->sc_iopool, accb);

	if (atomic_dec_int_nv(&ccb->ccb_refcnt) == 0)
		scsi_done(xs);
}

void *
mfii_get_ccb(void *cookie)
{
	struct mfii_softc *sc = cookie;
	struct mfii_ccb *ccb;

	mtx_enter(&sc->sc_ccb_mtx);
	ccb = SIMPLEQ_FIRST(&sc->sc_ccb_freeq);
	if (ccb != NULL)
		SIMPLEQ_REMOVE_HEAD(&sc->sc_ccb_freeq, ccb_link);
	mtx_leave(&sc->sc_ccb_mtx);

	return (ccb);
}

void
mfii_scrub_ccb(struct mfii_ccb *ccb)
{
	ccb->ccb_cookie = NULL;
	ccb->ccb_done = NULL;
	ccb->ccb_flags = 0;
	ccb->ccb_data = NULL;
	ccb->ccb_direction = 0;
	ccb->ccb_len = 0;
	ccb->ccb_sgl_len = 0;
	ccb->ccb_refcnt = 1;

	memset(&ccb->ccb_req, 0, sizeof(ccb->ccb_req));
	memset(ccb->ccb_request, 0, MFII_REQUEST_SIZE);
	memset(ccb->ccb_mfi, 0, MFI_FRAME_SIZE);
}

void
mfii_put_ccb(void *cookie, void *io)
{
	struct mfii_softc *sc = cookie;
	struct mfii_ccb *ccb = io;

	mtx_enter(&sc->sc_ccb_mtx);
	SIMPLEQ_INSERT_HEAD(&sc->sc_ccb_freeq, ccb, ccb_link);
	mtx_leave(&sc->sc_ccb_mtx);
}

int
mfii_init_ccb(struct mfii_softc *sc)
{
	struct mfii_ccb *ccb;
	u_int8_t *request = MFII_DMA_KVA(sc->sc_requests);
	u_int8_t *mfi = MFII_DMA_KVA(sc->sc_mfi);
	u_int8_t *sense = MFII_DMA_KVA(sc->sc_sense);
	u_int8_t *sgl = MFII_DMA_KVA(sc->sc_sgl);
	u_int i;
	int error;

	sc->sc_ccb = mallocarray(sc->sc_max_cmds, sizeof(struct mfii_ccb),
	    M_DEVBUF, M_WAITOK|M_ZERO);

	for (i = 0; i < sc->sc_max_cmds; i++) {
		ccb = &sc->sc_ccb[i];

		/* create a dma map for transfer */
		error = bus_dmamap_create(sc->sc_dmat,
		    MAXPHYS, sc->sc_max_sgl, MAXPHYS, 0,
		    BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW, &ccb->ccb_dmamap);
		if (error) {
			printf("%s: cannot create ccb dmamap (%d)\n",
			    DEVNAME(sc), error);
			goto destroy;
		}

		/* select i + 1'th request. 0 is reserved for events */
		ccb->ccb_smid = i + 1;
		ccb->ccb_request_offset = MFII_REQUEST_SIZE * (i + 1);
		ccb->ccb_request = request + ccb->ccb_request_offset;
		ccb->ccb_request_dva = MFII_DMA_DVA(sc->sc_requests) +
		    ccb->ccb_request_offset;

		/* select i'th MFI command frame */
		ccb->ccb_mfi_offset = MFI_FRAME_SIZE * i;
		ccb->ccb_mfi = mfi + ccb->ccb_mfi_offset;
		ccb->ccb_mfi_dva = MFII_DMA_DVA(sc->sc_mfi) +
		    ccb->ccb_mfi_offset;

		/* select i'th sense */
		ccb->ccb_sense_offset = MFI_SENSE_SIZE * i;
		ccb->ccb_sense = (struct mfi_sense *)(sense +
		    ccb->ccb_sense_offset);
		ccb->ccb_sense_dva = MFII_DMA_DVA(sc->sc_sense) +
		    ccb->ccb_sense_offset;

		/* select i'th sgl */
		ccb->ccb_sgl_offset = sizeof(struct mfii_sge) *
		    sc->sc_max_sgl * i;
		ccb->ccb_sgl = (struct mfii_sge *)(sgl + ccb->ccb_sgl_offset);
		ccb->ccb_sgl_dva = MFII_DMA_DVA(sc->sc_sgl) +
		    ccb->ccb_sgl_offset;

		/* add ccb to queue */
		mfii_put_ccb(sc, ccb);
	}

	return (0);

destroy:
	/* free dma maps and ccb memory */
	while ((ccb = mfii_get_ccb(sc)) != NULL)
		bus_dmamap_destroy(sc->sc_dmat, ccb->ccb_dmamap);

	free(sc->sc_ccb, M_DEVBUF, 0);

	return (1);
}

#if NBIO > 0
int
mfii_ioctl(struct device *dev, u_long cmd, caddr_t addr)
{
	struct mfii_softc	*sc = (struct mfii_softc *)dev;
	int error = 0;

	DNPRINTF(MFII_D_IOCTL, "%s: mfii_ioctl ", DEVNAME(sc));

	rw_enter_write(&sc->sc_lock);

	switch (cmd) {
	case BIOCINQ:
		DNPRINTF(MFII_D_IOCTL, "inq\n");
		error = mfii_ioctl_inq(sc, (struct bioc_inq *)addr);
		break;

	case BIOCVOL:
		DNPRINTF(MFII_D_IOCTL, "vol\n");
		error = mfii_ioctl_vol(sc, (struct bioc_vol *)addr);
		break;

	case BIOCDISK:
		DNPRINTF(MFII_D_IOCTL, "disk\n");
		error = mfii_ioctl_disk(sc, (struct bioc_disk *)addr);
		break;

	case BIOCALARM:
		DNPRINTF(MFII_D_IOCTL, "alarm\n");
		error = mfii_ioctl_alarm(sc, (struct bioc_alarm *)addr);
		break;

	case BIOCBLINK:
		DNPRINTF(MFII_D_IOCTL, "blink\n");
		error = mfii_ioctl_blink(sc, (struct bioc_blink *)addr);
		break;

	case BIOCSETSTATE:
		DNPRINTF(MFII_D_IOCTL, "setstate\n");
		error = mfii_ioctl_setstate(sc, (struct bioc_setstate *)addr);
		break;

	case BIOCPATROL:
		DNPRINTF(MFII_D_IOCTL, "patrol\n");
		error = mfii_ioctl_patrol(sc, (struct bioc_patrol *)addr);
		break;

	default:
		DNPRINTF(MFII_D_IOCTL, " invalid ioctl\n");
		error = ENOTTY;
	}

	rw_exit_write(&sc->sc_lock);

	return (error);
}

int
mfii_bio_getitall(struct mfii_softc *sc)
{
	int			i, d, rv = EINVAL;
	size_t			size;
	union mfi_mbox		mbox;
	struct mfi_conf		*cfg = NULL;
	struct mfi_ld_details	*ld_det = NULL;

	/* get info */
	if (mfii_get_info(sc)) {
		DNPRINTF(MFII_D_IOCTL, "%s: mfii_get_info failed\n",
		    DEVNAME(sc));
		goto done;
	}

	/* send single element command to retrieve size for full structure */
	cfg = malloc(sizeof *cfg, M_DEVBUF, M_NOWAIT | M_ZERO);
	if (cfg == NULL)
		goto done;
	if (mfii_mgmt(sc, MR_DCMD_CONF_GET, NULL, cfg, sizeof(*cfg),
	    SCSI_DATA_IN)) {
		free(cfg, M_DEVBUF, sizeof *cfg);
		goto done;
	}

	size = cfg->mfc_size;
	free(cfg, M_DEVBUF, sizeof *cfg);

	/* memory for read config */
	cfg = malloc(size, M_DEVBUF, M_NOWAIT | M_ZERO);
	if (cfg == NULL)
		goto done;
	if (mfii_mgmt(sc, MR_DCMD_CONF_GET, NULL, cfg, size, SCSI_DATA_IN)) {
		free(cfg, M_DEVBUF, size);
		goto done;
	}

	/* replace current pointer with new one */
	if (sc->sc_cfg)
		free(sc->sc_cfg, M_DEVBUF, 0);
	sc->sc_cfg = cfg;

	/* get all ld info */
	if (mfii_mgmt(sc, MR_DCMD_LD_GET_LIST, NULL, &sc->sc_ld_list,
	    sizeof(sc->sc_ld_list), SCSI_DATA_IN))
		goto done;

	/* get memory for all ld structures */
	size = cfg->mfc_no_ld * sizeof(struct mfi_ld_details);
	if (sc->sc_ld_sz != size) {
		if (sc->sc_ld_details)
			free(sc->sc_ld_details, M_DEVBUF, 0);

		ld_det = malloc(size, M_DEVBUF, M_NOWAIT | M_ZERO);
		if (ld_det == NULL)
			goto done;
		sc->sc_ld_sz = size;
		sc->sc_ld_details = ld_det;
	}

	/* find used physical disks */
	size = sizeof(struct mfi_ld_details);
	for (i = 0, d = 0; i < cfg->mfc_no_ld; i++) {
		memset(&mbox, 0, sizeof(mbox));
		mbox.b[0] = sc->sc_ld_list.mll_list[i].mll_ld.mld_target;
		if (mfii_mgmt(sc, MR_DCMD_LD_GET_INFO, &mbox, &sc->sc_ld_details[i], size,
		    SCSI_DATA_IN))
			goto done;

		d += sc->sc_ld_details[i].mld_cfg.mlc_parm.mpa_no_drv_per_span *
		    sc->sc_ld_details[i].mld_cfg.mlc_parm.mpa_span_depth;
	}
	sc->sc_no_pd = d;

	rv = 0;
done:
	return (rv);
}

int
mfii_ioctl_inq(struct mfii_softc *sc, struct bioc_inq *bi)
{
	int			rv = EINVAL;
	struct mfi_conf		*cfg = NULL;

	DNPRINTF(MFII_D_IOCTL, "%s: mfii_ioctl_inq\n", DEVNAME(sc));

	if (mfii_bio_getitall(sc)) {
		DNPRINTF(MFII_D_IOCTL, "%s: mfii_bio_getitall failed\n",
		    DEVNAME(sc));
		goto done;
	}

	/* count unused disks as volumes */
	if (sc->sc_cfg == NULL)
		goto done;
	cfg = sc->sc_cfg;

	bi->bi_nodisk = sc->sc_info.mci_pd_disks_present;
	bi->bi_novol = cfg->mfc_no_ld + cfg->mfc_no_hs;
#if notyet
	bi->bi_novol = cfg->mfc_no_ld + cfg->mfc_no_hs +
	    (bi->bi_nodisk - sc->sc_no_pd);
#endif
	/* tell bio who we are */
	strlcpy(bi->bi_dev, DEVNAME(sc), sizeof(bi->bi_dev));

	rv = 0;
done:
	return (rv);
}

int
mfii_ioctl_vol(struct mfii_softc *sc, struct bioc_vol *bv)
{
	int			i, per, target, rv = EINVAL;
	struct scsi_link	*link;
	struct device		*dev;

	DNPRINTF(MFII_D_IOCTL, "%s: mfii_ioctl_vol %#x\n",
	    DEVNAME(sc), bv->bv_volid);

	/* we really could skip and expect that inq took care of it */
	if (mfii_bio_getitall(sc)) {
		DNPRINTF(MFII_D_IOCTL, "%s: mfii_bio_getitall failed\n",
		    DEVNAME(sc));
		goto done;
	}

	if (bv->bv_volid >= sc->sc_ld_list.mll_no_ld) {
		/* go do hotspares & unused disks */
		rv = mfii_bio_hs(sc, bv->bv_volid, MFI_MGMT_VD, bv);
		goto done;
	}

	i = bv->bv_volid;
	target = sc->sc_ld_list.mll_list[i].mll_ld.mld_target;
	link = scsi_get_link(sc->sc_scsibus, target, 0);
	if (link == NULL) {
		strlcpy(bv->bv_dev, "cache", sizeof(bv->bv_dev));
	} else {
		dev = link->device_softc;
		if (dev == NULL)
			goto done;

		strlcpy(bv->bv_dev, dev->dv_xname, sizeof(bv->bv_dev));
	}

	switch(sc->sc_ld_list.mll_list[i].mll_state) {
	case MFI_LD_OFFLINE:
		bv->bv_status = BIOC_SVOFFLINE;
		break;

	case MFI_LD_PART_DEGRADED:
	case MFI_LD_DEGRADED:
		bv->bv_status = BIOC_SVDEGRADED;
		break;

	case MFI_LD_ONLINE:
		bv->bv_status = BIOC_SVONLINE;
		break;

	default:
		bv->bv_status = BIOC_SVINVALID;
		DNPRINTF(MFII_D_IOCTL, "%s: invalid logical disk state %#x\n",
		    DEVNAME(sc),
		    sc->sc_ld_list.mll_list[i].mll_state);
	}

	/* additional status can modify MFI status */
	switch (sc->sc_ld_details[i].mld_progress.mlp_in_prog) {
	case MFI_LD_PROG_CC:
		bv->bv_status = BIOC_SVSCRUB;
		per = (int)sc->sc_ld_details[i].mld_progress.mlp_cc.mp_progress;
		bv->bv_percent = (per * 100) / 0xffff;
		bv->bv_seconds =
		    sc->sc_ld_details[i].mld_progress.mlp_cc.mp_elapsed_seconds;
		break;

	case MFI_LD_PROG_BGI:
		bv->bv_status = BIOC_SVSCRUB;
		per = (int)sc->sc_ld_details[i].mld_progress.mlp_bgi.mp_progress;
		bv->bv_percent = (per * 100) / 0xffff;
		bv->bv_seconds =
		    sc->sc_ld_details[i].mld_progress.mlp_bgi.mp_elapsed_seconds;
		break;

	case MFI_LD_PROG_FGI:
	case MFI_LD_PROG_RECONSTRUCT:
		/* nothing yet */
		break;
	}

	if (sc->sc_ld_details[i].mld_cfg.mlc_prop.mlp_cur_cache_policy & 0x01)
		bv->bv_cache = BIOC_CVWRITEBACK;
	else
		bv->bv_cache = BIOC_CVWRITETHROUGH;

	/*
	 * The RAID levels are determined per the SNIA DDF spec, this is only
	 * a subset that is valid for the MFI controller.
	 */
	bv->bv_level = sc->sc_ld_details[i].mld_cfg.mlc_parm.mpa_pri_raid;
	if (sc->sc_ld_details[i].mld_cfg.mlc_parm.mpa_span_depth > 1)
		bv->bv_level *= 10;

	bv->bv_nodisk = sc->sc_ld_details[i].mld_cfg.mlc_parm.mpa_no_drv_per_span *
	    sc->sc_ld_details[i].mld_cfg.mlc_parm.mpa_span_depth;

	bv->bv_size = sc->sc_ld_details[i].mld_size * 512; /* bytes per block */

	rv = 0;
done:
	return (rv);
}

int
mfii_ioctl_disk(struct mfii_softc *sc, struct bioc_disk *bd)
{
	struct mfi_conf		*cfg;
	struct mfi_array	*ar;
	struct mfi_ld_cfg	*ld;
	struct mfi_pd_details	*pd;
	struct mfi_pd_list	*pl;
	struct mfi_pd_progress	*mfp;
	struct mfi_progress	*mp;
	struct scsi_inquiry_data *inqbuf;
	char			vend[8+16+4+1], *vendp;
	int			i, rv = EINVAL;
	int			arr, vol, disk, span;
	union mfi_mbox		mbox;

	DNPRINTF(MFII_D_IOCTL, "%s: mfii_ioctl_disk %#x\n",
	    DEVNAME(sc), bd->bd_diskid);

	/* we really could skip and expect that inq took care of it */
	if (mfii_bio_getitall(sc)) {
		DNPRINTF(MFII_D_IOCTL, "%s: mfii_bio_getitall failed\n",
		    DEVNAME(sc));
		return (rv);
	}
	cfg = sc->sc_cfg;

	pd = malloc(sizeof *pd, M_DEVBUF, M_WAITOK);
	pl = malloc(sizeof *pl, M_DEVBUF, M_WAITOK);

	ar = cfg->mfc_array;
	vol = bd->bd_volid;
	if (vol >= cfg->mfc_no_ld) {
		/* do hotspares */
		rv = mfii_bio_hs(sc, bd->bd_volid, MFI_MGMT_SD, bd);
		goto freeme;
	}

	/* calculate offset to ld structure */
	ld = (struct mfi_ld_cfg *)(
	    ((uint8_t *)cfg) + offsetof(struct mfi_conf, mfc_array) +
	    cfg->mfc_array_size * cfg->mfc_no_array);

	/* use span 0 only when raid group is not spanned */
	if (ld[vol].mlc_parm.mpa_span_depth > 1)
		span = bd->bd_diskid / ld[vol].mlc_parm.mpa_no_drv_per_span;
	else
		span = 0;
	arr = ld[vol].mlc_span[span].mls_index;

	/* offset disk into pd list */
	disk = bd->bd_diskid % ld[vol].mlc_parm.mpa_no_drv_per_span;

	if (ar[arr].pd[disk].mar_pd.mfp_id == 0xffffU) {
		/* disk is missing but succeed command */
		bd->bd_status = BIOC_SDFAILED;
		rv = 0;

		/* try to find an unused disk for the target to rebuild */
		if (mfii_mgmt(sc, MR_DCMD_PD_GET_LIST, NULL, pl, sizeof(*pl),
		    SCSI_DATA_IN))
			goto freeme;

		for (i = 0; i < pl->mpl_no_pd; i++) {
			if (pl->mpl_address[i].mpa_scsi_type != 0)
				continue;

			memset(&mbox, 0, sizeof(mbox));
			mbox.s[0] = pl->mpl_address[i].mpa_pd_id;
			if (mfii_mgmt(sc, MR_DCMD_PD_GET_INFO, &mbox, pd, sizeof(*pd),
			    SCSI_DATA_IN))
				continue;

			if (pd->mpd_fw_state == MFI_PD_UNCONFIG_GOOD ||
			    pd->mpd_fw_state == MFI_PD_UNCONFIG_BAD)
				break;
		}

		if (i == pl->mpl_no_pd)
			goto freeme;
	} else {
		memset(&mbox, 0, sizeof(mbox));
		mbox.s[0] = ar[arr].pd[disk].mar_pd.mfp_id;
		if (mfii_mgmt(sc, MR_DCMD_PD_GET_INFO, &mbox, pd, sizeof(*pd),
		    SCSI_DATA_IN)) {
			bd->bd_status = BIOC_SDINVALID;
			goto freeme;
		}
	}

	/* get the remaining fields */
	bd->bd_channel = pd->mpd_enc_idx;
	bd->bd_target = pd->mpd_enc_slot;

	/* get status */
	switch (pd->mpd_fw_state){
	case MFI_PD_UNCONFIG_GOOD:
	case MFI_PD_UNCONFIG_BAD:
		bd->bd_status = BIOC_SDUNUSED;
		break;

	case MFI_PD_HOTSPARE: /* XXX dedicated hotspare part of array? */
		bd->bd_status = BIOC_SDHOTSPARE;
		break;

	case MFI_PD_OFFLINE:
		bd->bd_status = BIOC_SDOFFLINE;
		break;

	case MFI_PD_FAILED:
		bd->bd_status = BIOC_SDFAILED;
		break;

	case MFI_PD_REBUILD:
		bd->bd_status = BIOC_SDREBUILD;
		break;

	case MFI_PD_ONLINE:
		bd->bd_status = BIOC_SDONLINE;
		break;

	case MFI_PD_COPYBACK:
	case MFI_PD_SYSTEM:
	default:
		bd->bd_status = BIOC_SDINVALID;
		break;
	}

	bd->bd_size = pd->mpd_size * 512; /* bytes per block */

	inqbuf = (struct scsi_inquiry_data *)&pd->mpd_inq_data;
	vendp = inqbuf->vendor;
	memcpy(vend, vendp, sizeof vend - 1);
	vend[sizeof vend - 1] = '\0';
	strlcpy(bd->bd_vendor, vend, sizeof(bd->bd_vendor));

	/* XXX find a way to retrieve serial nr from drive */
	/* XXX find a way to get bd_procdev */

	mfp = &pd->mpd_progress;
	if (mfp->mfp_in_prog & MFI_PD_PROG_PR) {
		mp = &mfp->mfp_patrol_read;
		bd->bd_patrol.bdp_percent = (mp->mp_progress * 100) / 0xffff;
		bd->bd_patrol.bdp_seconds = mp->mp_elapsed_seconds;
	}

	rv = 0;
freeme:
	free(pd, M_DEVBUF, sizeof *pd);
	free(pl, M_DEVBUF, sizeof *pl);

	return (rv);
}

int
mfii_ioctl_alarm(struct mfii_softc *sc, struct bioc_alarm *ba)
{
	uint32_t		opc, flags = 0;
	int			rv = 0;
	int8_t			ret;

	switch(ba->ba_opcode) {
	case BIOC_SADISABLE:
		opc = MR_DCMD_SPEAKER_DISABLE;
		break;

	case BIOC_SAENABLE:
		opc = MR_DCMD_SPEAKER_ENABLE;
		break;

	case BIOC_SASILENCE:
		opc = MR_DCMD_SPEAKER_SILENCE;
		break;

	case BIOC_GASTATUS:
		opc = MR_DCMD_SPEAKER_GET;
		flags = SCSI_DATA_IN;
		break;

	case BIOC_SATEST:
		opc = MR_DCMD_SPEAKER_TEST;
		break;

	default:
		DNPRINTF(MFII_D_IOCTL, "%s: mfii_ioctl_alarm biocalarm invalid "
		    "opcode %x\n", DEVNAME(sc), ba->ba_opcode);
		return (EINVAL);
	}

	if (mfii_mgmt(sc, opc, NULL, &ret, sizeof(ret), flags))
		rv = EINVAL;
	else
		if (ba->ba_opcode == BIOC_GASTATUS)
			ba->ba_status = ret;
		else
			ba->ba_status = 0;

	return (rv);
}

int
mfii_ioctl_blink(struct mfii_softc *sc, struct bioc_blink *bb)
{
	int			i, found, rv = EINVAL;
	union mfi_mbox		mbox;
	uint32_t		cmd;
	struct mfi_pd_list	*pd;

	DNPRINTF(MFII_D_IOCTL, "%s: mfii_ioctl_blink %x\n", DEVNAME(sc),
	    bb->bb_status);

	/* channel 0 means not in an enclosure so can't be blinked */
	if (bb->bb_channel == 0)
		return (EINVAL);

	pd = malloc(sizeof(*pd), M_DEVBUF, M_WAITOK);

	if (mfii_mgmt(sc, MR_DCMD_PD_GET_LIST, NULL, pd, sizeof(*pd), SCSI_DATA_IN))
		goto done;

	for (i = 0, found = 0; i < pd->mpl_no_pd; i++)
		if (bb->bb_channel == pd->mpl_address[i].mpa_enc_index &&
		    bb->bb_target == pd->mpl_address[i].mpa_enc_slot) {
			found = 1;
			break;
		}

	if (!found)
		goto done;

	memset(&mbox, 0, sizeof(mbox));
	mbox.s[0] = pd->mpl_address[i].mpa_pd_id;

	switch (bb->bb_status) {
	case BIOC_SBUNBLINK:
		cmd = MR_DCMD_PD_UNBLINK;
		break;

	case BIOC_SBBLINK:
		cmd = MR_DCMD_PD_BLINK;
		break;

	case BIOC_SBALARM:
	default:
		DNPRINTF(MFII_D_IOCTL, "%s: mfii_ioctl_blink biocblink invalid "
		    "opcode %x\n", DEVNAME(sc), bb->bb_status);
		goto done;
	}


	if (mfii_mgmt(sc, cmd, &mbox, NULL, 0, 0) == 0)
		rv = 0;

done:
	free(pd, M_DEVBUF, sizeof *pd);
	return (rv);
}

static int
mfii_makegood(struct mfii_softc *sc, uint16_t pd_id)
{
	struct mfii_foreign_scan_info *fsi;
	struct mfi_pd_details	*pd;
	union mfi_mbox		mbox;
	int			rv;

	fsi = malloc(sizeof *fsi, M_DEVBUF, M_WAITOK);
	pd = malloc(sizeof *pd, M_DEVBUF, M_WAITOK);

	memset(&mbox, 0, sizeof mbox);
	mbox.s[0] = pd_id;
	rv = mfii_mgmt(sc, MR_DCMD_PD_GET_INFO, &mbox, pd, sizeof(*pd), SCSI_DATA_IN);
	if (rv != 0)
		goto done;

	if (pd->mpd_fw_state == MFI_PD_UNCONFIG_BAD) {
		mbox.s[0] = pd_id;
		mbox.s[1] = pd->mpd_pd.mfp_seq;
		mbox.b[4] = MFI_PD_UNCONFIG_GOOD;
		rv = mfii_mgmt(sc, MR_DCMD_PD_SET_STATE, &mbox, NULL, 0, 0);
		if (rv != 0)
			goto done;
	}

	memset(&mbox, 0, sizeof mbox);
	mbox.s[0] = pd_id;
	rv = mfii_mgmt(sc, MR_DCMD_PD_GET_INFO, &mbox, pd, sizeof(*pd), SCSI_DATA_IN);
	if (rv != 0)
		goto done;

	if (pd->mpd_ddf_state & MFI_DDF_FOREIGN) {
		rv = mfii_mgmt(sc, MR_DCMD_CFG_FOREIGN_SCAN, NULL, fsi, sizeof(*fsi),
		    SCSI_DATA_IN);
		if (rv != 0)
			goto done;

		if (fsi->count > 0) {
			rv = mfii_mgmt(sc, MR_DCMD_CFG_FOREIGN_CLEAR, NULL, NULL, 0, 0);
			if (rv != 0)
				goto done;
		}
	}

	memset(&mbox, 0, sizeof mbox);
	mbox.s[0] = pd_id;
	rv = mfii_mgmt(sc, MR_DCMD_PD_GET_INFO, &mbox, pd, sizeof(*pd), SCSI_DATA_IN);
	if (rv != 0)
		goto done;

	if (pd->mpd_fw_state != MFI_PD_UNCONFIG_GOOD ||
	    pd->mpd_ddf_state & MFI_DDF_FOREIGN)
		rv = ENXIO;

done:
	free(fsi, M_DEVBUF, sizeof *fsi);
	free(pd, M_DEVBUF, sizeof *pd);

	return (rv);
}

static int
mfii_makespare(struct mfii_softc *sc, uint16_t pd_id)
{
	struct mfi_hotspare	*hs;
	struct mfi_pd_details	*pd;
	union mfi_mbox		mbox;
	size_t			size;
	int			rv = EINVAL;

	/* we really could skip and expect that inq took care of it */
	if (mfii_bio_getitall(sc)) {
		DNPRINTF(MFII_D_IOCTL, "%s: mfii_bio_getitall failed\n",
		    DEVNAME(sc));
		return (rv);
	}
	size = sizeof *hs + sizeof(uint16_t) * sc->sc_cfg->mfc_no_array;

	hs = malloc(size, M_DEVBUF, M_WAITOK);
	pd = malloc(sizeof *pd, M_DEVBUF, M_WAITOK);

	memset(&mbox, 0, sizeof mbox);
	mbox.s[0] = pd_id;
	rv = mfii_mgmt(sc, MR_DCMD_PD_GET_INFO, &mbox, pd, sizeof(*pd),
	    SCSI_DATA_IN);
	if (rv != 0)
		goto done;

	memset(hs, 0, size);
	hs->mhs_pd.mfp_id = pd->mpd_pd.mfp_id;
	hs->mhs_pd.mfp_seq = pd->mpd_pd.mfp_seq;
	rv = mfii_mgmt(sc, MR_DCMD_CFG_MAKE_SPARE, NULL, hs, size, SCSI_DATA_OUT);

done:
	free(hs, M_DEVBUF, size);
	free(pd, M_DEVBUF, sizeof *pd);

	return (rv);
}

int
mfii_ioctl_setstate(struct mfii_softc *sc, struct bioc_setstate *bs)
{
	struct mfi_pd_details	*pd;
	struct mfi_pd_list	*pl;
	int			i, found, rv = EINVAL;
	union mfi_mbox		mbox;

	DNPRINTF(MFII_D_IOCTL, "%s: mfii_ioctl_setstate %x\n", DEVNAME(sc),
	    bs->bs_status);

	pd = malloc(sizeof *pd, M_DEVBUF, M_WAITOK);
	pl = malloc(sizeof *pl, M_DEVBUF, M_WAITOK);

	if (mfii_mgmt(sc, MR_DCMD_PD_GET_LIST, NULL, pl, sizeof(*pl), SCSI_DATA_IN))
		goto done;

	for (i = 0, found = 0; i < pl->mpl_no_pd; i++)
		if (bs->bs_channel == pl->mpl_address[i].mpa_enc_index &&
		    bs->bs_target == pl->mpl_address[i].mpa_enc_slot) {
			found = 1;
			break;
		}

	if (!found)
		goto done;

	memset(&mbox, 0, sizeof(mbox));
	mbox.s[0] = pl->mpl_address[i].mpa_pd_id;

	if (mfii_mgmt(sc, MR_DCMD_PD_GET_INFO, &mbox, pd, sizeof(*pd), SCSI_DATA_IN))
		goto done;

	mbox.s[0] = pl->mpl_address[i].mpa_pd_id;
	mbox.s[1] = pd->mpd_pd.mfp_seq;

	switch (bs->bs_status) {
	case BIOC_SSONLINE:
		mbox.b[4] = MFI_PD_ONLINE;
		break;

	case BIOC_SSOFFLINE:
		mbox.b[4] = MFI_PD_OFFLINE;
		break;

	case BIOC_SSHOTSPARE:
		mbox.b[4] = MFI_PD_HOTSPARE;
		break;

	case BIOC_SSREBUILD:
		if (pd->mpd_fw_state != MFI_PD_OFFLINE) {
			if ((rv = mfii_makegood(sc,
			    pl->mpl_address[i].mpa_pd_id)))
				goto done;

			if ((rv = mfii_makespare(sc,
			    pl->mpl_address[i].mpa_pd_id)))
				goto done;

			memset(&mbox, 0, sizeof(mbox));
			mbox.s[0] = pl->mpl_address[i].mpa_pd_id;
			rv = mfii_mgmt(sc, MR_DCMD_PD_GET_INFO, &mbox, pd, sizeof(*pd),
			    SCSI_DATA_IN);
			if (rv != 0)
				goto done;

			/* rebuilding might be started by mfii_makespare() */
			if (pd->mpd_fw_state == MFI_PD_REBUILD) {
				rv = 0;
				goto done;
			}

			mbox.s[0] = pl->mpl_address[i].mpa_pd_id;
			mbox.s[1] = pd->mpd_pd.mfp_seq;
		}
		mbox.b[4] = MFI_PD_REBUILD;
		break;

	default:
		DNPRINTF(MFII_D_IOCTL, "%s: mfii_ioctl_setstate invalid "
		    "opcode %x\n", DEVNAME(sc), bs->bs_status);
		goto done;
	}


	rv = mfii_mgmt(sc, MR_DCMD_PD_SET_STATE, &mbox, NULL, 0, 0);
done:
	free(pd, M_DEVBUF, sizeof *pd);
	free(pl, M_DEVBUF, sizeof *pl);
	return (rv);
}

int
mfii_ioctl_patrol(struct mfii_softc *sc, struct bioc_patrol *bp)
{
	uint32_t		opc;
	int			rv = 0;
	struct mfi_pr_properties prop;
	struct mfi_pr_status	status;
	uint32_t		time, exec_freq;

	switch (bp->bp_opcode) {
	case BIOC_SPSTOP:
	case BIOC_SPSTART:
		if (bp->bp_opcode == BIOC_SPSTART)
			opc = MR_DCMD_PR_START;
		else
			opc = MR_DCMD_PR_STOP;
		if (mfii_mgmt(sc, opc, NULL, NULL, 0, SCSI_DATA_IN))
			return (EINVAL);
		break;

	case BIOC_SPMANUAL:
	case BIOC_SPDISABLE:
	case BIOC_SPAUTO:
		/* Get device's time. */
		opc = MR_DCMD_TIME_SECS_GET;
		if (mfii_mgmt(sc, opc, NULL, &time, sizeof(time), SCSI_DATA_IN))
			return (EINVAL);

		opc = MR_DCMD_PR_GET_PROPERTIES;
		if (mfii_mgmt(sc, opc, NULL, &prop, sizeof(prop), SCSI_DATA_IN))
			return (EINVAL);

		switch (bp->bp_opcode) {
		case BIOC_SPMANUAL:
			prop.op_mode = MFI_PR_OPMODE_MANUAL;
			break;
		case BIOC_SPDISABLE:
			prop.op_mode = MFI_PR_OPMODE_DISABLED;
			break;
		case BIOC_SPAUTO:
			if (bp->bp_autoival != 0) {
				if (bp->bp_autoival == -1)
					/* continuously */
					exec_freq = 0xffffffffU;
				else if (bp->bp_autoival > 0)
					exec_freq = bp->bp_autoival;
				else
					return (EINVAL);
				prop.exec_freq = exec_freq;
			}
			if (bp->bp_autonext != 0) {
				if (bp->bp_autonext < 0)
					return (EINVAL);
				else
					prop.next_exec = time + bp->bp_autonext;
			}
			prop.op_mode = MFI_PR_OPMODE_AUTO;
			break;
		}

		opc = MR_DCMD_PR_SET_PROPERTIES;
		if (mfii_mgmt(sc, opc, NULL, &prop, sizeof(prop), SCSI_DATA_OUT))
			return (EINVAL);

		break;

	case BIOC_GPSTATUS:
		opc = MR_DCMD_PR_GET_PROPERTIES;
		if (mfii_mgmt(sc, opc, NULL, &prop, sizeof(prop), SCSI_DATA_IN))
			return (EINVAL);

		opc = MR_DCMD_PR_GET_STATUS;
		if (mfii_mgmt(sc, opc, NULL, &status, sizeof(status), SCSI_DATA_IN))
			return (EINVAL);

		/* Get device's time. */
		opc = MR_DCMD_TIME_SECS_GET;
		if (mfii_mgmt(sc, opc, NULL, &time, sizeof(time), SCSI_DATA_IN))
			return (EINVAL);

		switch (prop.op_mode) {
		case MFI_PR_OPMODE_AUTO:
			bp->bp_mode = BIOC_SPMAUTO;
			bp->bp_autoival = prop.exec_freq;
			bp->bp_autonext = prop.next_exec;
			bp->bp_autonow = time;
			break;
		case MFI_PR_OPMODE_MANUAL:
			bp->bp_mode = BIOC_SPMMANUAL;
			break;
		case MFI_PR_OPMODE_DISABLED:
			bp->bp_mode = BIOC_SPMDISABLED;
			break;
		default:
			printf("%s: unknown patrol mode %d\n",
			    DEVNAME(sc), prop.op_mode);
			break;
		}

		switch (status.state) {
		case MFI_PR_STATE_STOPPED:
			bp->bp_status = BIOC_SPSSTOPPED;
			break;
		case MFI_PR_STATE_READY:
			bp->bp_status = BIOC_SPSREADY;
			break;
		case MFI_PR_STATE_ACTIVE:
			bp->bp_status = BIOC_SPSACTIVE;
			break;
		case MFI_PR_STATE_ABORTED:
			bp->bp_status = BIOC_SPSABORTED;
			break;
		default:
			printf("%s: unknown patrol state %d\n",
			    DEVNAME(sc), status.state);
			break;
		}

		break;

	default:
		DNPRINTF(MFII_D_IOCTL, "%s: mfii_ioctl_patrol biocpatrol invalid "
		    "opcode %x\n", DEVNAME(sc), bp->bp_opcode);
		return (EINVAL);
	}

	return (rv);
}

int
mfii_bio_hs(struct mfii_softc *sc, int volid, int type, void *bio_hs)
{
	struct mfi_conf		*cfg;
	struct mfi_hotspare	*hs;
	struct mfi_pd_details	*pd;
	struct bioc_disk	*sdhs;
	struct bioc_vol		*vdhs;
	struct scsi_inquiry_data *inqbuf;
	char			vend[8+16+4+1], *vendp;
	int			i, rv = EINVAL;
	uint32_t		size;
	union mfi_mbox		mbox;

	DNPRINTF(MFII_D_IOCTL, "%s: mfii_vol_hs %d\n", DEVNAME(sc), volid);

	if (!bio_hs)
		return (EINVAL);

	pd = malloc(sizeof *pd, M_DEVBUF, M_WAITOK);

	/* send single element command to retrieve size for full structure */
	cfg = malloc(sizeof *cfg, M_DEVBUF, M_WAITOK);
	if (mfii_mgmt(sc, MR_DCMD_CONF_GET, NULL, cfg, sizeof(*cfg), SCSI_DATA_IN))
		goto freeme;

	size = cfg->mfc_size;
	free(cfg, M_DEVBUF, sizeof *cfg);

	/* memory for read config */
	cfg = malloc(size, M_DEVBUF, M_WAITOK|M_ZERO);
	if (mfii_mgmt(sc, MR_DCMD_CONF_GET, NULL, cfg, size, SCSI_DATA_IN))
		goto freeme;

	/* calculate offset to hs structure */
	hs = (struct mfi_hotspare *)(
	    ((uint8_t *)cfg) + offsetof(struct mfi_conf, mfc_array) +
	    cfg->mfc_array_size * cfg->mfc_no_array +
	    cfg->mfc_ld_size * cfg->mfc_no_ld);

	if (volid < cfg->mfc_no_ld)
		goto freeme; /* not a hotspare */

	if (volid > (cfg->mfc_no_ld + cfg->mfc_no_hs))
		goto freeme; /* not a hotspare */

	/* offset into hotspare structure */
	i = volid - cfg->mfc_no_ld;

	DNPRINTF(MFII_D_IOCTL, "%s: mfii_vol_hs i %d volid %d no_ld %d no_hs %d "
	    "hs %p cfg %p id %02x\n", DEVNAME(sc), i, volid, cfg->mfc_no_ld,
	    cfg->mfc_no_hs, hs, cfg, hs[i].mhs_pd.mfp_id);

	/* get pd fields */
	memset(&mbox, 0, sizeof(mbox));
	mbox.s[0] = hs[i].mhs_pd.mfp_id;
	if (mfii_mgmt(sc, MR_DCMD_PD_GET_INFO, &mbox, pd, sizeof(*pd),
	    SCSI_DATA_IN)) {
		DNPRINTF(MFII_D_IOCTL, "%s: mfii_vol_hs illegal PD\n",
		    DEVNAME(sc));
		goto freeme;
	}

	switch (type) {
	case MFI_MGMT_VD:
		vdhs = bio_hs;
		vdhs->bv_status = BIOC_SVONLINE;
		vdhs->bv_size = pd->mpd_size / 2 * 1024; /* XXX why? */
		vdhs->bv_level = -1; /* hotspare */
		vdhs->bv_nodisk = 1;
		break;

	case MFI_MGMT_SD:
		sdhs = bio_hs;
		sdhs->bd_status = BIOC_SDHOTSPARE;
		sdhs->bd_size = pd->mpd_size / 2 * 1024; /* XXX why? */
		sdhs->bd_channel = pd->mpd_enc_idx;
		sdhs->bd_target = pd->mpd_enc_slot;
		inqbuf = (struct scsi_inquiry_data *)&pd->mpd_inq_data;
		vendp = inqbuf->vendor;
		memcpy(vend, vendp, sizeof vend - 1);
		vend[sizeof vend - 1] = '\0';
		strlcpy(sdhs->bd_vendor, vend, sizeof(sdhs->bd_vendor));
		break;

	default:
		goto freeme;
	}

	DNPRINTF(MFII_D_IOCTL, "%s: mfii_vol_hs 6\n", DEVNAME(sc));
	rv = 0;
freeme:
	free(pd, M_DEVBUF, sizeof *pd);
	free(cfg, M_DEVBUF, 0);

	return (rv);
}

#ifndef SMALL_KERNEL

#define MFI_BBU_SENSORS 4

void
mfii_bbu(struct mfii_softc *sc)
{
	struct mfi_bbu_status bbu;
	u_int32_t status;
	u_int32_t mask;
	u_int32_t soh_bad;
	int i;

	if (mfii_mgmt(sc, MR_DCMD_BBU_GET_STATUS, NULL, &bbu,
	    sizeof(bbu), SCSI_DATA_IN) != 0) {
		for (i = 0; i < MFI_BBU_SENSORS; i++) {
			sc->sc_bbu[i].value = 0;
			sc->sc_bbu[i].status = SENSOR_S_UNKNOWN;
		}
		for (i = 0; i < nitems(mfi_bbu_indicators); i++) {
			sc->sc_bbu_status[i].value = 0;
			sc->sc_bbu_status[i].status = SENSOR_S_UNKNOWN;
		}
		return;
	}

	switch (bbu.battery_type) {
	case MFI_BBU_TYPE_IBBU:
		mask = MFI_BBU_STATE_BAD_IBBU;
		soh_bad = 0;
		break;
	case MFI_BBU_TYPE_BBU:
		mask = MFI_BBU_STATE_BAD_BBU;
		soh_bad = (bbu.detail.bbu.is_SOH_good == 0);
		break;

	case MFI_BBU_TYPE_NONE:
	default:
		sc->sc_bbu[0].value = 0;
		sc->sc_bbu[0].status = SENSOR_S_CRIT;
		for (i = 1; i < MFI_BBU_SENSORS; i++) {
			sc->sc_bbu[i].value = 0;
			sc->sc_bbu[i].status = SENSOR_S_UNKNOWN;
		}
		for (i = 0; i < nitems(mfi_bbu_indicators); i++) {
			sc->sc_bbu_status[i].value = 0;
			sc->sc_bbu_status[i].status = SENSOR_S_UNKNOWN;
		}
		return;
	}

	status = letoh32(bbu.fw_status);

	sc->sc_bbu[0].value = ((status & mask) || soh_bad) ? 0 : 1;
	sc->sc_bbu[0].status = ((status & mask) || soh_bad) ? SENSOR_S_CRIT :
	    SENSOR_S_OK;

	sc->sc_bbu[1].value = letoh16(bbu.voltage) * 1000;
	sc->sc_bbu[2].value = (int16_t)letoh16(bbu.current) * 1000;
	sc->sc_bbu[3].value = letoh16(bbu.temperature) * 1000000 + 273150000;
	for (i = 1; i < MFI_BBU_SENSORS; i++)
		sc->sc_bbu[i].status = SENSOR_S_UNSPEC;

	for (i = 0; i < nitems(mfi_bbu_indicators); i++) {
		sc->sc_bbu_status[i].value = (status & (1 << i)) ? 1 : 0;
		sc->sc_bbu_status[i].status = SENSOR_S_UNSPEC;
	}
}

void
mfii_refresh_ld_sensor(struct mfii_softc *sc, int ld)
{
	struct ksensor *sensor;
	int target;

	target = sc->sc_ld_list.mll_list[ld].mll_ld.mld_target;
	sensor = &sc->sc_sensors[target];

	switch(sc->sc_ld_list.mll_list[ld].mll_state) {
	case MFI_LD_OFFLINE:
		sensor->value = SENSOR_DRIVE_FAIL;
		sensor->status = SENSOR_S_CRIT;
		break;

	case MFI_LD_PART_DEGRADED:
	case MFI_LD_DEGRADED:
		sensor->value = SENSOR_DRIVE_PFAIL;
		sensor->status = SENSOR_S_WARN;
		break;

	case MFI_LD_ONLINE:
		sensor->value = SENSOR_DRIVE_ONLINE;
		sensor->status = SENSOR_S_OK;
		break;

	default:
		sensor->value = 0; /* unknown */
		sensor->status = SENSOR_S_UNKNOWN;
		break;
	}
}

void
mfii_init_ld_sensor(struct mfii_softc *sc, int ld)
{
	struct device		*dev;
	struct scsi_link	*link;
	struct ksensor		*sensor;
	int			target;

	target = sc->sc_ld_list.mll_list[ld].mll_ld.mld_target;
	sensor = &sc->sc_sensors[target];

	link = scsi_get_link(sc->sc_scsibus, target, 0);
	if (link == NULL) {
		strlcpy(sensor->desc, "cache", sizeof(sensor->desc));
	} else {
		dev = link->device_softc;
		if (dev != NULL)
			strlcpy(sensor->desc, dev->dv_xname,
			    sizeof(sensor->desc));
	}
	sensor->type = SENSOR_DRIVE;
	mfii_refresh_ld_sensor(sc, ld);
}

int
mfii_create_sensors(struct mfii_softc *sc)
{
	int			i, target;

	strlcpy(sc->sc_sensordev.xname, DEVNAME(sc),
	    sizeof(sc->sc_sensordev.xname));

	if (ISSET(letoh32(sc->sc_info.mci_hw_present), MFI_INFO_HW_BBU)) {
		sc->sc_bbu = mallocarray(4, sizeof(*sc->sc_bbu),
		    M_DEVBUF, M_WAITOK | M_ZERO);

		sc->sc_bbu[0].type = SENSOR_INDICATOR;
		sc->sc_bbu[0].status = SENSOR_S_UNKNOWN;
		strlcpy(sc->sc_bbu[0].desc, "bbu ok",
		    sizeof(sc->sc_bbu[0].desc));
		sensor_attach(&sc->sc_sensordev, &sc->sc_bbu[0]);

		sc->sc_bbu[1].type = SENSOR_VOLTS_DC;
		sc->sc_bbu[1].status = SENSOR_S_UNSPEC;
		sc->sc_bbu[2].type = SENSOR_AMPS;
		sc->sc_bbu[2].status = SENSOR_S_UNSPEC;
		sc->sc_bbu[3].type = SENSOR_TEMP;
		sc->sc_bbu[3].status = SENSOR_S_UNSPEC;
		for (i = 1; i < MFI_BBU_SENSORS; i++) {
			strlcpy(sc->sc_bbu[i].desc, "bbu",
			    sizeof(sc->sc_bbu[i].desc));
			sensor_attach(&sc->sc_sensordev, &sc->sc_bbu[i]);
		}

		sc->sc_bbu_status = malloc(sizeof(*sc->sc_bbu_status) *
		    sizeof(mfi_bbu_indicators), M_DEVBUF, M_WAITOK | M_ZERO);

		for (i = 0; i < nitems(mfi_bbu_indicators); i++) {
			sc->sc_bbu_status[i].type = SENSOR_INDICATOR;
			sc->sc_bbu_status[i].status = SENSOR_S_UNSPEC;
			strlcpy(sc->sc_bbu_status[i].desc,
			    mfi_bbu_indicators[i],
			    sizeof(sc->sc_bbu_status[i].desc));

			sensor_attach(&sc->sc_sensordev, &sc->sc_bbu_status[i]);
		}
	}

	sc->sc_sensors = mallocarray(MFI_MAX_LD, sizeof(struct ksensor),
	    M_DEVBUF, M_NOWAIT | M_ZERO);
	if (sc->sc_sensors == NULL)
		return (1);

	for (i = 0; i < sc->sc_ld_list.mll_no_ld; i++) {
		mfii_init_ld_sensor(sc, i);
		target = sc->sc_ld_list.mll_list[i].mll_ld.mld_target;
		sensor_attach(&sc->sc_sensordev, &sc->sc_sensors[target]);
	}

	if (sensor_task_register(sc, mfii_refresh_sensors, 10) == NULL)
		goto bad;

	sensordev_install(&sc->sc_sensordev);

	return (0);

bad:
	free(sc->sc_sensors, M_DEVBUF,
	    MFI_MAX_LD * sizeof(struct ksensor));

	return (1);
}

void
mfii_refresh_sensors(void *arg)
{
	struct mfii_softc	*sc = arg;
	int			i;

	rw_enter_write(&sc->sc_lock);
	if (sc->sc_bbu != NULL)
		mfii_bbu(sc);

	mfii_bio_getitall(sc);
	rw_exit_write(&sc->sc_lock);

	for (i = 0; i < sc->sc_ld_list.mll_no_ld; i++)
		mfii_refresh_ld_sensor(sc, i);
}
#endif /* SMALL_KERNEL */
#endif /* NBIO > 0 */
