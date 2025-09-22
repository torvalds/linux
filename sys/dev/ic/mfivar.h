/* $OpenBSD: mfivar.h,v 1.55 2020/07/22 13:16:04 krw Exp $ */
/*
 * Copyright (c) 2006 Marco Peereboom <marco@peereboom.us>
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

struct mfi_softc;
#define DEVNAME(_s)	((_s)->sc_dev.dv_xname)

/* #define MFI_DEBUG */
#ifdef MFI_DEBUG
extern uint32_t			mfi_debug;
#define DPRINTF(x...)		do { if (mfi_debug) printf(x); } while(0)
#define DNPRINTF(n,x...)	do { if (mfi_debug & n) printf(x); } while(0)
#define	MFI_D_CMD		0x0001
#define	MFI_D_INTR		0x0002
#define	MFI_D_MISC		0x0004
#define	MFI_D_DMA		0x0008
#define	MFI_D_IOCTL		0x0010
#define	MFI_D_RW		0x0020
#define	MFI_D_MEM		0x0040
#define	MFI_D_CCB		0x0080
#else
#define DPRINTF(x...)
#define DNPRINTF(n,x...)
#endif

struct mfi_mem {
	bus_dmamap_t		am_map;
	bus_dma_segment_t	am_seg;
	size_t			am_size;
	caddr_t			am_kva;
};

#define MFIMEM_MAP(_am)		((_am)->am_map)
#define MFIMEM_LEN(_am)		((_am)->am_map->dm_mapsize)
#define MFIMEM_DVA(_am)		((_am)->am_map->dm_segs[0].ds_addr)
#define MFIMEM_KVA(_am)		((void *)(_am)->am_kva)

struct mfi_prod_cons {
	uint32_t		mpc_producer;
	uint32_t		mpc_consumer;
	uint32_t		mpc_reply_q[1]; /* compensate for 1 extra reply per spec */
};

struct mfi_ccb {
	union mfi_frame		*ccb_frame;
	paddr_t			ccb_pframe;
	bus_addr_t		ccb_pframe_offset;
	uint32_t		ccb_frame_size;
	uint32_t		ccb_extra_frames;

	struct mfi_sense	*ccb_sense;
	paddr_t			ccb_psense;

	bus_dmamap_t		ccb_dmamap;

	union mfi_sgl		*ccb_sgl;

	/* data for sgl */
	void			*ccb_data;
	uint32_t		ccb_len;

	uint32_t		ccb_direction;
#define MFI_DATA_NONE	0
#define MFI_DATA_IN	1
#define MFI_DATA_OUT	2

	void			*ccb_cookie;
	void			(*ccb_done)(struct mfi_softc *,
				    struct mfi_ccb *);

	volatile enum {
		MFI_CCB_FREE,
		MFI_CCB_READY,
		MFI_CCB_DONE
	}			ccb_state;
	uint32_t		ccb_flags;
#define MFI_CCB_F_ERR			(1<<0)
	SLIST_ENTRY(mfi_ccb)	ccb_link;
};

SLIST_HEAD(mfi_ccb_list, mfi_ccb);

enum mfi_iop {
	MFI_IOP_XSCALE,
	MFI_IOP_PPC,
	MFI_IOP_GEN2,
	MFI_IOP_SKINNY
};

struct mfi_iop_ops {
	u_int32_t	(*mio_fw_state)(struct mfi_softc *);
	void		(*mio_intr_ena)(struct mfi_softc *);
	int		(*mio_intr)(struct mfi_softc *);
	void		(*mio_post)(struct mfi_softc *, struct mfi_ccb *);
	u_int		(*mio_sgd_load)(struct mfi_softc *, struct mfi_ccb *);
	u_int32_t	mio_idb;
	u_int32_t	mio_flags;
#define MFI_IOP_F_SYSPD		(1 << 0)
};

struct mfi_pd_link {
	u_int16_t		pd_id;
	struct mfi_pd_details	pd_info;
};

struct mfi_pd_softc {
	struct scsibus_softc	*pd_scsibus;
	struct mfi_pd_link	*pd_links[MFI_MAX_PD];
};

struct mfi_softc {
	struct device		sc_dev;
	void			*sc_ih;
	struct scsi_iopool	sc_iopool;

	const struct mfi_iop_ops *sc_iop;

	int			sc_64bit_dma;

	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	bus_dma_tag_t		sc_dmat;

	/* save some useful information for logical drives that is missing
	 * in sc_ld_list
	 */
	struct {
		uint32_t	ld_present;
		char		ld_dev[16];	/* device name sd? */
	}			sc_ld[MFI_MAX_LD];

	/* scsi ioctl from sd device */
	int			(*sc_ioctl)(struct device *, u_long, caddr_t);

	/* firmware determined max, totals and other information*/
	uint32_t		sc_max_cmds;
	uint32_t		sc_max_sgl;
	uint32_t		sc_sgl_size;
	uint32_t		sc_ld_cnt;

	uint16_t		sc_sgl_flags;
	uint16_t		sc_reserved;

	/* bio */
	struct mfi_conf		*sc_cfg;
	struct mfi_ctrl_info	sc_info;
	struct mfi_ld_list	sc_ld_list;
	struct mfi_ld_details	*sc_ld_details; /* array to all logical disks */
	int			sc_no_pd; /* used physical disks */
	int			sc_ld_sz; /* sizeof sc_ld_details */

	/* all commands */
	struct mfi_ccb		*sc_ccb;

	/* producer/consumer pointers and reply queue */
	struct mfi_mem		*sc_pcq;

	/* frame memory */
	struct mfi_mem		*sc_frames;
	uint32_t		sc_frames_size;

	/* sense memory */
	struct mfi_mem		*sc_sense;

	struct mfi_ccb_list	sc_ccb_freeq;
	struct mutex		sc_ccb_mtx;

	struct scsibus_softc	*sc_scsibus;
	struct mfi_pd_softc	*sc_pd;

	/* mgmt lock */
	struct rwlock		sc_lock;

	/* sensors */
	struct ksensordev	sc_sensordev;
	struct ksensor		*sc_bbu;
	struct ksensor		*sc_bbu_status;
	struct ksensor		*sc_sensors;
};

int	mfi_attach(struct mfi_softc *sc, enum mfi_iop);
int	mfi_intr(void *);
