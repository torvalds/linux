/*	$OpenBSD: nvmevar.h,v 1.31 2024/09/13 09:57:34 jmatthew Exp $ */

/*
 * Copyright (c) 2014 David Gwynne <dlg@openbsd.org>
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

#include <sys/sensors.h>

#define NVME_IO_Q	1
#define NVME_HIB_Q	2
#define NVME_MAXPHYS	(128 * 1024)

struct nvme_dmamem {
	bus_dmamap_t		ndm_map;
	bus_dma_segment_t	ndm_seg;
	size_t			ndm_size;
	caddr_t			ndm_kva;
};
#define NVME_DMA_MAP(_ndm)	((_ndm)->ndm_map)
#define NVME_DMA_LEN(_ndm)	((_ndm)->ndm_map->dm_segs[0].ds_len)
#define NVME_DMA_DVA(_ndm)	((u_int64_t)(_ndm)->ndm_map->dm_segs[0].ds_addr)
#define NVME_DMA_KVA(_ndm)	((void *)(_ndm)->ndm_kva)

struct nvme_softc;

struct nvme_ccb {
	SIMPLEQ_ENTRY(nvme_ccb)	ccb_entry;

	bus_dmamap_t		ccb_dmamap;

	void			*ccb_cookie;
	void			(*ccb_done)(struct nvme_softc *sc,
				    struct nvme_ccb *, struct nvme_cqe *);

	bus_addr_t		ccb_prpl_off;
	u_int64_t		ccb_prpl_dva;
	u_int64_t		*ccb_prpl;

	u_int16_t		ccb_id;
};
SIMPLEQ_HEAD(nvme_ccb_list, nvme_ccb);

struct nvme_queue {
	struct mutex		q_sq_mtx;
	struct mutex		q_cq_mtx;
	struct nvme_dmamem	*q_sq_dmamem;
	struct nvme_dmamem	*q_cq_dmamem;
	struct nvme_dmamem	*q_nvmmu_dmamem; /* for aplns(4) */
	bus_size_t 		q_sqtdbl; /* submission queue tail doorbell */
	bus_size_t 		q_cqhdbl; /* completion queue head doorbell */
	u_int16_t		q_id;
	u_int32_t		q_entries;
	u_int32_t		q_sq_tail;
	u_int32_t		q_cq_head;
	u_int16_t		q_cq_phase;
};

struct nvme_namespace {
	struct nvm_identify_namespace *ident;
};

struct nvme_ops {
	void		(*op_enable)(struct nvme_softc *);

	int		(*op_q_alloc)(struct nvme_softc *,
			      struct nvme_queue *);
	void		(*op_q_free)(struct nvme_softc *,
			      struct nvme_queue *);

	uint32_t	(*op_sq_enter)(struct nvme_softc *,
			      struct nvme_queue *, struct nvme_ccb *);
	void		(*op_sq_leave)(struct nvme_softc *,
			      struct nvme_queue *, struct nvme_ccb *);
	uint32_t	(*op_sq_enter_locked)(struct nvme_softc *,
			      struct nvme_queue *, struct nvme_ccb *);
	void		(*op_sq_leave_locked)(struct nvme_softc *,
			      struct nvme_queue *, struct nvme_ccb *);

	void		(*op_cq_done)(struct nvme_softc *,
			      struct nvme_queue *, struct nvme_ccb *);
};

struct nvme_softc {
	struct device		sc_dev;

	const struct nvme_ops	*sc_ops;
	u_int			sc_openings;

	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	bus_size_t		sc_ios;
	bus_dma_tag_t		sc_dmat;

	void			*sc_ih;

	u_int			sc_rdy_to;
	size_t			sc_mps;
	size_t			sc_mdts;
	u_int			sc_max_prpl;
	u_int			sc_dstrd;

	struct nvm_identify_controller
				sc_identify;

	u_int			sc_nn;
	struct nvme_namespace	*sc_namespaces;

	struct nvme_queue	*sc_admin_q;
	struct nvme_queue	*sc_q;
	struct nvme_queue	*sc_hib_q;

	struct mutex		sc_ccb_mtx;
	struct nvme_ccb		*sc_ccbs;
	struct nvme_ccb_list	sc_ccb_list;
	struct nvme_dmamem	*sc_ccb_prpls;
	struct scsi_iopool	sc_iopool;
	struct rwlock		sc_lock;
	struct scsibus_softc	*sc_scsibus;

	struct ksensordev	sc_sensordev;
	struct ksensor		sc_temp_sensor;
	struct ksensor		sc_spare_sensor;
	struct ksensor		sc_usage_sensor;
};

#define DEVNAME(_sc) ((_sc)->sc_dev.dv_xname)

int	nvme_attach(struct nvme_softc *);
int	nvme_activate(struct nvme_softc *, int);
int	nvme_intr(void *);
int	nvme_intr_intx(void *);

#define nvme_read4(_s, _r) \
	bus_space_read_4((_s)->sc_iot, (_s)->sc_ioh, (_r))
#define nvme_write4(_s, _r, _v) \
	bus_space_write_4((_s)->sc_iot, (_s)->sc_ioh, (_r), (_v))

u_int64_t
	nvme_read8(struct nvme_softc *, bus_size_t);
void	nvme_write8(struct nvme_softc *, bus_size_t, u_int64_t);

#define nvme_barrier(_s, _r, _l, _f) \
	bus_space_barrier((_s)->sc_iot, (_s)->sc_ioh, (_r), (_l), (_f))

struct nvme_dmamem *
	nvme_dmamem_alloc(struct nvme_softc *, size_t);
void	nvme_dmamem_free(struct nvme_softc *, struct nvme_dmamem *);
void	nvme_dmamem_sync(struct nvme_softc *, struct nvme_dmamem *, int);
