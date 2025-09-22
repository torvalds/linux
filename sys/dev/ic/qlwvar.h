/*	$OpenBSD: qlwvar.h,v 1.11 2020/07/22 13:16:04 krw Exp $ */

/*
 * Copyright (c) 2013, 2014 Jonathan Matthew <jmatthew@openbsd.org>
 * Copyright (c) 2014 Mark Kettenis <kettenis@openbsd.org>
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

#define QLW_MAX_TARGETS			16
#define QLW_MAX_LUNS			8

/* maximum number of segments allowed for in a single io */
#define QLW_MAX_SEGS			16

struct qlw_softc;

enum qlw_isp_gen {
	QLW_GEN_ISP1000 = 1,
	QLW_GEN_ISP1040,
	QLW_GEN_ISP1080,
	QLW_GEN_ISP12160,
};

enum qlw_isp_type {
	QLW_ISP1000 = 1,
	QLW_ISP1020,
	QLW_ISP1020A,
	QLW_ISP1040,
	QLW_ISP1040A,
	QLW_ISP1040B,
	QLW_ISP1040C,
	QLW_ISP1240,
	QLW_ISP1080,
	QLW_ISP1280,
	QLW_ISP10160,
	QLW_ISP12160,
};

/* request/response queue stuff */
#define QLW_QUEUE_ENTRY_SIZE		64

struct qlw_ccb {
	struct qlw_softc 	*ccb_sc;
	int			ccb_id;
	struct scsi_xfer	*ccb_xs;

	bus_dmamap_t		ccb_dmamap;

	SIMPLEQ_ENTRY(qlw_ccb)	ccb_link;
};

SIMPLEQ_HEAD(qlw_ccb_list, qlw_ccb);

struct qlw_dmamem {
	bus_dmamap_t		qdm_map;
	bus_dma_segment_t	qdm_seg;
	size_t			qdm_size;
	caddr_t			qdm_kva;
};
#define QLW_DMA_MAP(_qdm)	((_qdm)->qdm_map)
#define QLW_DMA_LEN(_qdm)	((_qdm)->qdm_size)
#define QLW_DMA_DVA(_qdm)	((u_int64_t)(_qdm)->qdm_map->dm_segs[0].ds_addr)
#define QLW_DMA_KVA(_qdm)	((void *)(_qdm)->qdm_kva)

struct qlw_target {
	u_int16_t		qt_params;
	u_int8_t		qt_exec_throttle;
	u_int8_t		qt_sync_period;
	u_int8_t		qt_sync_offset;
};

struct qlw_softc {
	struct device		sc_dev;

	int			sc_flags;
#define QLW_FLAG_INITIATOR	0x0001

	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	bus_size_t		sc_ios;
	bus_dma_tag_t		sc_dmat;

	struct scsibus_softc	*sc_scsibus[2];
	int			sc_running;

	enum qlw_isp_type	sc_isp_type;
	enum qlw_isp_gen	sc_isp_gen;
	const u_int16_t		*sc_firmware;
	int			sc_numbusses;
	int			sc_clock;

	int			sc_host_cmd_ctrl;
	int			sc_mbox_base;
	u_int16_t		sc_mbox[8];
	int			sc_mbox_pending;

	int			sc_maxrequests;
	struct qlw_dmamem	*sc_requests;
	int			sc_maxresponses;
	struct qlw_dmamem	*sc_responses;
	int			sc_maxccbs;
	struct qlw_ccb		*sc_ccbs;
	struct qlw_ccb_list	sc_ccb_free;
	struct mutex		sc_ccb_mtx;
	struct mutex		sc_queue_mtx;
	struct scsi_iopool	sc_iopool;
	u_int16_t		sc_next_req_id;
	u_int16_t		sc_last_resp_id;
	int			sc_marker_required[2];
	u_int			sc_update_required[2];
	struct task		sc_update_task;

	struct qlw_nvram	sc_nvram;
	int			sc_nvram_size;
	int			sc_nvram_minversion;

	u_int16_t		sc_isp_config;
	u_int16_t		sc_fw_features;

	u_int8_t		sc_initiator[2];
	u_int8_t		sc_retry_count[2];
	u_int8_t		sc_retry_delay[2];
	u_int8_t		sc_reset_delay[2];
	u_int8_t		sc_tag_age_limit[2];
	u_int16_t		sc_selection_timeout[2];
	u_int16_t		sc_max_queue_depth[2];
	u_int8_t		sc_async_data_setup[2];
	u_int8_t		sc_req_ack_active_neg[2];
	u_int8_t		sc_data_line_active_neg[2];
	struct qlw_target	sc_target[2][QLW_MAX_TARGETS];
};
#define DEVNAME(_sc) ((_sc)->sc_dev.dv_xname)

int	qlw_attach(struct qlw_softc *);
int	qlw_detach(struct qlw_softc *, int);

int	qlw_intr(void *);
