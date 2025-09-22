/*	$OpenBSD: mpivar.h,v 1.41 2020/07/22 13:16:04 krw Exp $ */

/*
 * Copyright (c) 2005 David Gwynne <dlg@openbsd.org>
 * Copyright (c) 2005 Marco Peereboom <marco@openbsd.org>
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

#include <sys/task.h>

/* #define MPI_DEBUG */
#ifdef MPI_DEBUG
extern uint32_t			mpi_debug;
#define DPRINTF(x...)		do { if (mpi_debug) printf(x); } while(0)
#define DNPRINTF(n,x...)	do { if (mpi_debug & (n)) printf(x); } while(0)
#define	MPI_D_CMD		0x0001
#define	MPI_D_INTR		0x0002
#define	MPI_D_MISC		0x0004
#define	MPI_D_DMA		0x0008
#define	MPI_D_IOCTL		0x0010
#define	MPI_D_RW		0x0020
#define	MPI_D_MEM		0x0040
#define	MPI_D_CCB		0x0080
#define	MPI_D_PPR		0x0100
#define	MPI_D_RAID		0x0200
#define	MPI_D_EVT		0x0400
#else
#define DPRINTF(x...)
#define DNPRINTF(n,x...)
#endif

#define MPI_REQUEST_SIZE	512
#define MPI_REPLY_SIZE		80
#define MPI_REPLYQ_DEPTH	128
#define MPI_REPLY_COUNT		(PAGE_SIZE / MPI_REPLY_SIZE)

/*
 * this is the max number of sge's we can stuff in a request frame:
 * sizeof(scsi_io) + sizeof(sense) + sizeof(sge) * 32 = MPI_REQUEST_SIZE
 */
#define MPI_MAX_SGL		36

struct mpi_dmamem {
	bus_dmamap_t		mdm_map;
	bus_dma_segment_t	mdm_seg;
	size_t			mdm_size;
	caddr_t			mdm_kva;
};
#define MPI_DMA_MAP(_mdm)	((_mdm)->mdm_map)
#define MPI_DMA_DVA(_mdm)	((u_int64_t)(_mdm)->mdm_map->dm_segs[0].ds_addr)
#define MPI_DMA_KVA(_mdm)	((void *)(_mdm)->mdm_kva)

struct mpi_ccb_bundle {
	struct mpi_msg_scsi_io	mcb_io; /* sgl must follow */
	struct mpi_sge		mcb_sgl[MPI_MAX_SGL];
	struct scsi_sense_data	mcb_sense;
} __packed;

struct mpi_softc;

struct mpi_rcb {
	SIMPLEQ_ENTRY(mpi_rcb)	rcb_link;
	void			*rcb_reply;
	bus_addr_t		rcb_offset;
	u_int32_t		rcb_reply_dva;
};
SIMPLEQ_HEAD(mpi_rcb_list, mpi_rcb);

struct mpi_ccb {
	struct mpi_softc	*ccb_sc;
	int			ccb_id;

	void 			*ccb_cookie;
	bus_dmamap_t		ccb_dmamap;

	bus_addr_t		ccb_offset;
	void			*ccb_cmd;
	u_int64_t		ccb_cmd_dva;

	volatile enum {
		MPI_CCB_FREE,
		MPI_CCB_READY,
		MPI_CCB_QUEUED
	}			ccb_state;
	void			(*ccb_done)(struct mpi_ccb *);
	struct mpi_rcb		*ccb_rcb;

	SLIST_ENTRY(mpi_ccb)	ccb_link;
};

SLIST_HEAD(mpi_ccb_list, mpi_ccb);

struct mpi_softc {
	struct device		sc_dev;

	u_int64_t		sc_port_wwn;
	u_int64_t		sc_node_wwn;

	int			sc_flags;
#define MPI_F_SPI			(1<<0)
#define MPI_F_RAID			(1<<1)

	struct scsibus_softc	*sc_scsibus;

	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	bus_size_t		sc_ios;
	bus_dma_tag_t		sc_dmat;

	u_int8_t		sc_fw_maj;
	u_int8_t		sc_fw_min;
	u_int8_t		sc_fw_unit;
	u_int8_t		sc_fw_dev;

	u_int8_t		sc_porttype;
	int			sc_maxcmds;
	int			sc_maxchdepth;
	int			sc_first_sgl_len;
	int			sc_chain_len;
	int			sc_max_sgl_len;

	int			sc_buswidth;
	int			sc_target;
	int			sc_ioc_number;

	struct mpi_dmamem	*sc_requests;
	struct mpi_ccb		*sc_ccbs;
	struct mpi_ccb_list	sc_ccb_free;
	struct mutex		sc_ccb_mtx;
	struct scsi_iopool	sc_iopool;

	struct mpi_dmamem	*sc_replies;
	struct mpi_rcb		*sc_rcbs;
	int			sc_repq;

	struct mpi_ccb		*sc_evt_ccb;
	struct mpi_rcb_list	sc_evt_ack_queue;
	struct mutex		sc_evt_ack_mtx;
	struct scsi_iohandler	sc_evt_ack_handler;

	struct mpi_rcb_list	sc_evt_scan_queue;
	struct mutex		sc_evt_scan_mtx;
	struct scsi_iohandler	sc_evt_scan_handler;

	struct task		sc_evt_rescan;

	size_t			sc_fw_len;
	struct mpi_dmamem	*sc_fw;

	/* scsi ioctl from sd device */
	int			(*sc_ioctl)(struct device *, u_long, caddr_t);

	struct rwlock		sc_lock;
	struct mpi_cfg_hdr	sc_cfg_hdr;
	struct mpi_cfg_ioc_pg2	*sc_vol_page;
	struct mpi_cfg_raid_vol	*sc_vol_list;
	struct mpi_cfg_raid_vol_pg0 *sc_rpg0;

	struct ksensor		*sc_sensors;
	struct ksensordev	sc_sensordev;
};

int	mpi_attach(struct mpi_softc *);
void	mpi_detach(struct mpi_softc *);

int	mpi_intr(void *);
