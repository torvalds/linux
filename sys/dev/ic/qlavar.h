/*	$OpenBSD: qlavar.h,v 1.12 2020/07/22 13:16:04 krw Exp $ */

/*
 * Copyright (c) 2013, 2014 Jonathan Matthew <jmatthew@openbsd.org>
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

#define QLA_DEFAULT_PORT_NAME		0x400000007F000003ULL /* from isp(4) */

#define QLA_WAIT_FOR_LOOP		10

/* maximum number of segments allowed for in a single io */
#define QLA_MAX_SEGS			16

struct qla_softc;

enum qla_isp_gen {
	QLA_GEN_ISP2100 = 1,
	QLA_GEN_ISP2200,
	QLA_GEN_ISP23XX,
};

enum qla_isp_type {
	QLA_ISP2100 = 1,
	QLA_ISP2200,
	QLA_ISP2300,
	QLA_ISP2312,
	QLA_ISP2322
};

struct qla_regs {
	u_int16_t	(*read)(struct qla_softc *, bus_size_t);
	int		(*read_isr)(struct qla_softc *,
			    u_int16_t *, u_int16_t *);

	bus_size_t	req_in;
	bus_size_t	req_out;
	bus_size_t	res_in;
	bus_size_t	res_out;
};

/* port database things */
#define QLA_SCRATCH_SIZE		0x1000

enum qla_port_disp {
	QLA_PORT_DISP_NEW,
	QLA_PORT_DISP_GONE,
	QLA_PORT_DISP_SAME,
	QLA_PORT_DISP_CHANGED,
	QLA_PORT_DISP_MOVED,
	QLA_PORT_DISP_DUP
};

#define QLA_LOCATION_LOOP		(1 << 24)
#define QLA_LOCATION_FABRIC		(2 << 24)
#define QLA_LOCATION_LOOP_ID(l)		(l | QLA_LOCATION_LOOP)
#define QLA_LOCATION_PORT_ID(p)		(p | QLA_LOCATION_FABRIC)

struct qla_fc_port {
	TAILQ_ENTRY(qla_fc_port) ports;
	TAILQ_ENTRY(qla_fc_port) update;

	u_int64_t	node_name;
	u_int64_t	port_name;
	u_int32_t	location;	/* port id or loop id */

	int		flags;
#define QLA_PORT_FLAG_IS_TARGET		1
#define QLA_PORT_FLAG_NEEDS_LOGIN	2

	u_int32_t	portid;
	u_int16_t	loopid;
};


/* request/response queue stuff */
#define QLA_QUEUE_ENTRY_SIZE		64

struct qla_ccb {
	struct qla_softc 	*ccb_sc;
	int			ccb_id;
	struct scsi_xfer	*ccb_xs;

	bus_dmamap_t		ccb_dmamap;

	struct qla_iocb_seg	*ccb_t4segs;
	u_int64_t		ccb_seg_offset;
	u_int64_t		ccb_seg_dva;

	SIMPLEQ_ENTRY(qla_ccb)	ccb_link;
};

SIMPLEQ_HEAD(qla_ccb_list, qla_ccb);

struct qla_dmamem {
	bus_dmamap_t		qdm_map;
	bus_dma_segment_t	qdm_seg;
	size_t			qdm_size;
	caddr_t			qdm_kva;
};
#define QLA_DMA_MAP(_qdm)	((_qdm)->qdm_map)
#define QLA_DMA_LEN(_qdm)	((_qdm)->qdm_size)
#define QLA_DMA_DVA(_qdm)	((u_int64_t)(_qdm)->qdm_map->dm_segs[0].ds_addr)
#define QLA_DMA_KVA(_qdm)	((void *)(_qdm)->qdm_kva)

struct qla_softc {
	struct device		sc_dev;

	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	bus_size_t		sc_ios;
	bus_dma_tag_t		sc_dmat;

	struct scsibus_softc	*sc_scsibus;

	enum qla_isp_type	sc_isp_type;
	enum qla_isp_gen	sc_isp_gen;
	int			sc_port;
	int			sc_expanded_lun;
	int			sc_fabric;
	int			sc_2k_logins;

	const struct qla_regs	*sc_regs;

	int			sc_mbox_base;
	u_int16_t		sc_mbox[12];
	int			sc_mbox_pending;
	struct mutex		sc_mbox_mtx;

	int			sc_loop_up;
	int			sc_topology;
	int			sc_loop_id;
	int			sc_port_id;
	int			sc_loop_max_id;
	u_int64_t		sc_sns_port_name;

	struct mutex		sc_port_mtx;
	TAILQ_HEAD(, qla_fc_port) sc_ports;
	TAILQ_HEAD(, qla_fc_port) sc_ports_new;
	TAILQ_HEAD(, qla_fc_port) sc_ports_gone;
	TAILQ_HEAD(, qla_fc_port) sc_ports_found;
	struct qla_fc_port	*sc_targets[QLA_2KL_BUSWIDTH];

	struct taskq		*sc_update_taskq;
	struct task		sc_update_task;
	int			sc_update;
	int			sc_update_tasks;
#define QLA_UPDATE_TASK_CLEAR_ALL	0x00000001
#define QLA_UPDATE_TASK_SOFTRESET	0x00000002
#define QLA_UPDATE_TASK_UPDATE_TOPO	0x00000004
#define QLA_UPDATE_TASK_GET_PORT_LIST	0x00000008
#define QLA_UPDATE_TASK_PORT_LIST	0x00000010
#define QLA_UPDATE_TASK_SCAN_FABRIC	0x00000020
#define QLA_UPDATE_TASK_SCANNING_FABRIC	0x00000040
#define QLA_UPDATE_TASK_FABRIC_LOGIN	0x00000080
#define QLA_UPDATE_TASK_FABRIC_RELOGIN	0x00000100
#define QLA_UPDATE_TASK_DETACH_TARGET	0x00000200
#define QLA_UPDATE_TASK_ATTACH_TARGET	0x00000400

	int			sc_maxcmds;
	struct qla_dmamem	*sc_requests;
	struct qla_dmamem	*sc_responses;
	struct qla_dmamem	*sc_segments;
	struct qla_dmamem	*sc_scratch;
	struct qla_ccb		*sc_ccbs;
	struct qla_ccb_list	sc_ccb_free;
	struct mutex		sc_ccb_mtx;
	struct mutex		sc_queue_mtx;
	struct scsi_iopool	sc_iopool;
	u_int16_t		sc_next_req_id;
	u_int16_t		sc_last_resp_id;
	int			sc_marker_required;

	struct qla_nvram	sc_nvram;
	int			sc_nvram_valid;
	u_int64_t		sc_node_name;
	u_int64_t		sc_port_name;
};
#define DEVNAME(_sc) ((_sc)->sc_dev.dv_xname)

int	qla_attach(struct qla_softc *);
int	qla_detach(struct qla_softc *, int);

int	qla_intr(void *);
