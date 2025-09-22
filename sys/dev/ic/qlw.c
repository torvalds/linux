/*	$OpenBSD: qlw.c,v 1.48 2022/04/16 19:19:59 naddy Exp $ */

/*
 * Copyright (c) 2011 David Gwynne <dlg@openbsd.org>
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/atomic.h>
#include <sys/device.h>
#include <sys/ioctl.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/mutex.h>
#include <sys/rwlock.h>
#include <sys/sensors.h>
#include <sys/queue.h>

#include <machine/bus.h>

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>

#include <dev/ic/qlwreg.h>
#include <dev/ic/qlwvar.h>

#ifndef SMALL_KERNEL
#ifndef QLW_DEBUG
#define QLW_DEBUG
#endif
#endif

#ifdef QLW_DEBUG
#define DPRINTF(m, f...) do { if ((qlwdebug & (m)) == (m)) printf(f); } \
    while (0)
#define QLW_D_MBOX		0x01
#define QLW_D_INTR		0x02
#define QLW_D_PORT		0x04
#define QLW_D_IO		0x08
#define QLW_D_IOCB		0x10
int qlwdebug = QLW_D_PORT | QLW_D_INTR | QLW_D_MBOX;
#else
#define DPRINTF(m, f...)
#endif

struct cfdriver qlw_cd = {
	NULL,
	"qlw",
	DV_DULL
};

void		qlw_scsi_cmd(struct scsi_xfer *);

u_int16_t	qlw_read(struct qlw_softc *, bus_size_t);
void		qlw_write(struct qlw_softc *, bus_size_t, u_int16_t);
void		qlw_host_cmd(struct qlw_softc *sc, u_int16_t);

int		qlw_mbox(struct qlw_softc *, int, int);
void		qlw_mbox_putaddr(u_int16_t *, struct qlw_dmamem *);
u_int16_t	qlw_read_mbox(struct qlw_softc *, int);
void		qlw_write_mbox(struct qlw_softc *, int, u_int16_t);

int		qlw_config_bus(struct qlw_softc *, int);
int		qlw_config_target(struct qlw_softc *, int, int);
void		qlw_update_bus(struct qlw_softc *, int);
void		qlw_update_target(struct qlw_softc *, int, int);
void		qlw_update_task(void *);

void		qlw_handle_intr(struct qlw_softc *, u_int16_t, u_int16_t);
void		qlw_set_ints(struct qlw_softc *, int);
int		qlw_read_isr(struct qlw_softc *, u_int16_t *, u_int16_t *);
void		qlw_clear_isr(struct qlw_softc *, u_int16_t);

void		qlw_update(struct qlw_softc *, int);
void		qlw_put_marker(struct qlw_softc *, int, void *);
void		qlw_put_cmd(struct qlw_softc *, void *, struct scsi_xfer *,
		    struct qlw_ccb *);
void		qlw_put_cont(struct qlw_softc *, void *, struct scsi_xfer *,
		    struct qlw_ccb *, int);
struct qlw_ccb *qlw_handle_resp(struct qlw_softc *, u_int16_t);
void		qlw_get_header(struct qlw_softc *, struct qlw_iocb_hdr *,
		    int *, int *);
void		qlw_put_header(struct qlw_softc *, struct qlw_iocb_hdr *,
		    int, int);
void		qlw_put_data_seg(struct qlw_softc *, struct qlw_iocb_seg *,
		    bus_dmamap_t, int);

int		qlw_softreset(struct qlw_softc *);
void		qlw_dma_burst_enable(struct qlw_softc *);

int		qlw_async(struct qlw_softc *, u_int16_t);

int		qlw_load_firmware_words(struct qlw_softc *, const u_int16_t *,
		    u_int16_t);
int		qlw_load_firmware(struct qlw_softc *);
int		qlw_read_nvram(struct qlw_softc *);
void		qlw_parse_nvram_1040(struct qlw_softc *, int);
void		qlw_parse_nvram_1080(struct qlw_softc *, int);
void		qlw_init_defaults(struct qlw_softc *, int);

struct qlw_dmamem *qlw_dmamem_alloc(struct qlw_softc *, size_t);
void		qlw_dmamem_free(struct qlw_softc *, struct qlw_dmamem *);

int		qlw_alloc_ccbs(struct qlw_softc *);
void		qlw_free_ccbs(struct qlw_softc *);
void		*qlw_get_ccb(void *);
void		qlw_put_ccb(void *, void *);

#ifdef QLW_DEBUG
void		qlw_dump_iocb(struct qlw_softc *, void *, int);
void		qlw_dump_iocb_segs(struct qlw_softc *, void *, int);
#else
#define qlw_dump_iocb(sc, h, fl)	do { /* nothing */ } while (0)
#define qlw_dump_iocb_segs(sc, h, fl)	do { /* nothing */ } while (0)
#endif

static inline int
qlw_xs_bus(struct qlw_softc *sc, struct scsi_xfer *xs)
{
	/*
	 * sc_scsibus[0] == NULL -> bus 0 probing during config_found().
	 * sc_scsibus[0] == xs->sc_link->bus -> bus 0 normal operation.
	 * sc_scsibus[1] == NULL -> bus 1 probing during config_found().
	 * sc_scsibus[1] == xs->sc_link->bus -> bus 1 normal operation.
	 */
	if ((sc->sc_scsibus[0] == NULL) ||
	    (xs->sc_link->bus == sc->sc_scsibus[0]))
		return 0;
	else
		return 1;
}

static inline u_int16_t
qlw_swap16(struct qlw_softc *sc, u_int16_t value)
{
	if (sc->sc_isp_gen == QLW_GEN_ISP1000)
		return htobe16(value);
	else
		return htole16(value);
}

static inline u_int32_t
qlw_swap32(struct qlw_softc *sc, u_int32_t value)
{
	if (sc->sc_isp_gen == QLW_GEN_ISP1000)
		return htobe32(value);
	else
		return htole32(value);
}

static inline u_int16_t
qlw_queue_read(struct qlw_softc *sc, bus_size_t offset)
{
	return qlw_read(sc, sc->sc_mbox_base + offset);
}

static inline void
qlw_queue_write(struct qlw_softc *sc, bus_size_t offset, u_int16_t value)
{
	qlw_write(sc, sc->sc_mbox_base + offset, value);
}

const struct scsi_adapter qlw_switch = {
	qlw_scsi_cmd, NULL, NULL, NULL, NULL
};

int
qlw_attach(struct qlw_softc *sc)
{
	struct scsibus_attach_args saa;
	void (*parse_nvram)(struct qlw_softc *, int);
	int reset_delay;
	int bus;

	task_set(&sc->sc_update_task, qlw_update_task, sc);

	switch (sc->sc_isp_gen) {
	case QLW_GEN_ISP1000:
		sc->sc_nvram_size = 0;
		break;
	case QLW_GEN_ISP1040:
		sc->sc_nvram_size = 128;
		sc->sc_nvram_minversion = 2;
		parse_nvram = qlw_parse_nvram_1040;
		break;
	case QLW_GEN_ISP1080:
	case QLW_GEN_ISP12160:
		sc->sc_nvram_size = 256;
		sc->sc_nvram_minversion = 1;
		parse_nvram = qlw_parse_nvram_1080;
		break;

	default:
		printf("unknown isp type\n");
		return (ENXIO);
	}

	/* after reset, mbox registers 1-3 should contain the string "ISP   " */
	if (qlw_read_mbox(sc, 1) != 0x4953 ||
	    qlw_read_mbox(sc, 2) != 0x5020 ||
	    qlw_read_mbox(sc, 3) != 0x2020) {
		/* try releasing the risc processor */
		qlw_host_cmd(sc, QLW_HOST_CMD_RELEASE);
	}

	qlw_host_cmd(sc, QLW_HOST_CMD_PAUSE);
	if (qlw_softreset(sc) != 0) {
		printf("softreset failed\n");
		return (ENXIO);
	}

	for (bus = 0; bus < sc->sc_numbusses; bus++)
		qlw_init_defaults(sc, bus);

	if (qlw_read_nvram(sc) == 0) {
		for (bus = 0; bus < sc->sc_numbusses; bus++)
			parse_nvram(sc, bus);
	}

#ifndef ISP_NOFIRMWARE
	if (sc->sc_firmware && qlw_load_firmware(sc)) {
		printf("firmware load failed\n");
		return (ENXIO);
	}
#endif

	/* execute firmware */
	sc->sc_mbox[0] = QLW_MBOX_EXEC_FIRMWARE;
	sc->sc_mbox[1] = QLW_CODE_ORG;
	if (qlw_mbox(sc, 0x0003, 0x0001)) {
		printf("ISP couldn't exec firmware: %x\n", sc->sc_mbox[0]);
		return (ENXIO);
	}

	delay(250000);		/* from isp(4) */

	sc->sc_mbox[0] = QLW_MBOX_ABOUT_FIRMWARE;
	if (qlw_mbox(sc, QLW_MBOX_ABOUT_FIRMWARE_IN,
	    QLW_MBOX_ABOUT_FIRMWARE_OUT)) {
		printf("ISP not talking after firmware exec: %x\n",
		    sc->sc_mbox[0]);
		return (ENXIO);
	}
	/* The ISP1000 firmware we use doesn't return a version number. */
	if (sc->sc_isp_gen == QLW_GEN_ISP1000 && sc->sc_firmware) {
		sc->sc_mbox[1] = 1;
		sc->sc_mbox[2] = 37;
		sc->sc_mbox[3] = 0;
		sc->sc_mbox[6] = 0;
	}
	printf("%s: firmware rev %d.%d.%d, attrs 0x%x\n", DEVNAME(sc),
	    sc->sc_mbox[1], sc->sc_mbox[2], sc->sc_mbox[3], sc->sc_mbox[6]);

	/* work out how many ccbs to allocate */
	sc->sc_mbox[0] = QLW_MBOX_GET_FIRMWARE_STATUS;
	if (qlw_mbox(sc, 0x0001, 0x0007)) {
		printf("couldn't get firmware status: %x\n", sc->sc_mbox[0]);
		return (ENXIO);
	}
	sc->sc_maxrequests = sc->sc_mbox[2];
	if (sc->sc_maxrequests > 512)
		sc->sc_maxrequests = 512;
	for (bus = 0; bus < sc->sc_numbusses; bus++) {
		if (sc->sc_max_queue_depth[bus] > sc->sc_maxrequests)
			sc->sc_max_queue_depth[bus] = sc->sc_maxrequests;
	}

	/*
	 * On some 1020/1040 variants the response queue is limited to
	 * 256 entries.  We don't really need all that many anyway.
	 */
	sc->sc_maxresponses = sc->sc_maxrequests / 2;
	if (sc->sc_maxresponses < 64)
		sc->sc_maxresponses = 64;

	/* We may need up to 3 request entries per SCSI command. */
	sc->sc_maxccbs = sc->sc_maxrequests / 3;

	/* Allegedly the FIFO is busted on the 1040A. */
	if (sc->sc_isp_type == QLW_ISP1040A)
		sc->sc_isp_config &= ~QLW_PCI_FIFO_MASK;
	qlw_write(sc, QLW_CFG1, sc->sc_isp_config);

	if (sc->sc_isp_config & QLW_BURST_ENABLE)
		qlw_dma_burst_enable(sc);

	sc->sc_mbox[0] = QLW_MBOX_SET_FIRMWARE_FEATURES;
	sc->sc_mbox[1] = 0;
	if (sc->sc_fw_features & QLW_FW_FEATURE_LVD_NOTIFY)
		sc->sc_mbox[1] |= QLW_FW_FEATURE_LVD_NOTIFY;
	if (sc->sc_mbox[1] != 0 && qlw_mbox(sc, 0x0003, 0x0001)) {
		printf("couldn't set firmware features: %x\n", sc->sc_mbox[0]);
		return (ENXIO);
	}

	sc->sc_mbox[0] = QLW_MBOX_SET_CLOCK_RATE;
	sc->sc_mbox[1] = sc->sc_clock;
	if (qlw_mbox(sc, 0x0003, 0x0001)) {
		printf("couldn't set clock rate: %x\n", sc->sc_mbox[0]);
		return (ENXIO);
	}

	sc->sc_mbox[0] = QLW_MBOX_SET_RETRY_COUNT;
	sc->sc_mbox[1] = sc->sc_retry_count[0];
	sc->sc_mbox[2] = sc->sc_retry_delay[0];
	sc->sc_mbox[6] = sc->sc_retry_count[1];
	sc->sc_mbox[7] = sc->sc_retry_delay[1];
	if (qlw_mbox(sc, 0x00c7, 0x0001)) {
		printf("couldn't set retry count: %x\n", sc->sc_mbox[0]);
		return (ENXIO);
	}

	sc->sc_mbox[0] = QLW_MBOX_SET_ASYNC_DATA_SETUP;
	sc->sc_mbox[1] = sc->sc_async_data_setup[0];
	sc->sc_mbox[2] = sc->sc_async_data_setup[1];
	if (qlw_mbox(sc, 0x0007, 0x0001)) {
		printf("couldn't set async data setup: %x\n", sc->sc_mbox[0]);
		return (ENXIO);
	}

	sc->sc_mbox[0] = QLW_MBOX_SET_ACTIVE_NEGATION;
	sc->sc_mbox[1] = sc->sc_req_ack_active_neg[0] << 5;
	sc->sc_mbox[1] |= sc->sc_data_line_active_neg[0] << 4;
	sc->sc_mbox[2] = sc->sc_req_ack_active_neg[1] << 5;
	sc->sc_mbox[2] |= sc->sc_data_line_active_neg[1] << 4;
	if (qlw_mbox(sc, 0x0007, 0x0001)) {
		printf("couldn't set active negation: %x\n", sc->sc_mbox[0]);
		return (ENXIO);
	}

	sc->sc_mbox[0] = QLW_MBOX_SET_TAG_AGE_LIMIT;
	sc->sc_mbox[1] = sc->sc_tag_age_limit[0];
	sc->sc_mbox[2] = sc->sc_tag_age_limit[1];
	if (qlw_mbox(sc, 0x0007, 0x0001)) {
		printf("couldn't set tag age limit: %x\n", sc->sc_mbox[0]);
		return (ENXIO);
	}

	sc->sc_mbox[0] = QLW_MBOX_SET_SELECTION_TIMEOUT;
	sc->sc_mbox[1] = sc->sc_selection_timeout[0];
	sc->sc_mbox[2] = sc->sc_selection_timeout[1];
	if (qlw_mbox(sc, 0x0007, 0x0001)) {
		printf("couldn't set selection timeout: %x\n", sc->sc_mbox[0]);
		return (ENXIO);
	}

	for (bus = 0; bus < sc->sc_numbusses; bus++) {
		if (qlw_config_bus(sc, bus))
			return (ENXIO);
	}

	if (qlw_alloc_ccbs(sc)) {
		/* error already printed */
		return (ENOMEM);
	}

	sc->sc_mbox[0] = QLW_MBOX_INIT_REQ_QUEUE;
	sc->sc_mbox[1] = sc->sc_maxrequests;
	qlw_mbox_putaddr(sc->sc_mbox, sc->sc_requests);
	sc->sc_mbox[4] = 0;
	if (qlw_mbox(sc, 0x00df, 0x0001)) {
		printf("couldn't init request queue: %x\n", sc->sc_mbox[0]);
		goto free_ccbs;
	}

	sc->sc_mbox[0] = QLW_MBOX_INIT_RSP_QUEUE;
	sc->sc_mbox[1] = sc->sc_maxresponses;
	qlw_mbox_putaddr(sc->sc_mbox, sc->sc_responses);
	sc->sc_mbox[5] = 0;
	if (qlw_mbox(sc, 0x00ef, 0x0001)) {
		printf("couldn't init response queue: %x\n", sc->sc_mbox[0]);
		goto free_ccbs;
	}

	reset_delay = 0;
	for (bus = 0; bus < sc->sc_numbusses; bus++) {
		sc->sc_mbox[0] = QLW_MBOX_BUS_RESET;
		sc->sc_mbox[1] = sc->sc_reset_delay[bus];
		sc->sc_mbox[2] = bus;
		if (qlw_mbox(sc, 0x0007, 0x0001)) {
			printf("couldn't reset bus: %x\n", sc->sc_mbox[0]);
			goto free_ccbs;
		}
		sc->sc_marker_required[bus] = 1;
		sc->sc_update_required[bus] = 0xffff;

		if (sc->sc_reset_delay[bus] > reset_delay)
			reset_delay = sc->sc_reset_delay[bus];
	}

	/* wait for the busses to settle */
	delay(reset_delay * 1000000);

	saa.saa_adapter = &qlw_switch;
	saa.saa_adapter_softc = sc;
	saa.saa_adapter_buswidth = QLW_MAX_TARGETS;
	saa.saa_luns = QLW_MAX_LUNS;
	saa.saa_pool = &sc->sc_iopool;
	saa.saa_quirks = saa.saa_flags = 0;
	saa.saa_wwpn = saa.saa_wwnn = 0;
	for (bus = 0; bus < sc->sc_numbusses; bus++) {
		saa.saa_adapter_target = sc->sc_initiator[bus];
		saa.saa_openings = sc->sc_max_queue_depth[bus];

		sc->sc_scsibus[bus] = (struct scsibus_softc *)
		    config_found(&sc->sc_dev, &saa, scsiprint);

		qlw_update_bus(sc, bus);
	}

	sc->sc_running = 1;
	return(0);

free_ccbs:
	qlw_free_ccbs(sc);
	return (ENXIO);
}

int
qlw_detach(struct qlw_softc *sc, int flags)
{
	return (0);
}

int
qlw_config_bus(struct qlw_softc *sc, int bus)
{
	int target, err;

	sc->sc_mbox[0] = QLW_MBOX_SET_INITIATOR_ID;
	sc->sc_mbox[1] = (bus << 7) | sc->sc_initiator[bus];

	if (qlw_mbox(sc, 0x0003, 0x0001)) {
		printf("couldn't set initiator id: %x\n", sc->sc_mbox[0]);
		return (ENXIO);
	}

	for (target = 0; target < QLW_MAX_TARGETS; target++) {
		err = qlw_config_target(sc, bus, target);
		if (err)
			return (err);
	}

	return (0);
}

int
qlw_config_target(struct qlw_softc *sc, int bus, int target)
{
	int lun;

	sc->sc_mbox[0] = QLW_MBOX_SET_TARGET_PARAMETERS;
	sc->sc_mbox[1] = (((bus << 7) | target) << 8);
	sc->sc_mbox[2] = sc->sc_target[bus][target].qt_params;
	sc->sc_mbox[2] &= QLW_TARGET_SAFE;
	sc->sc_mbox[2] |= QLW_TARGET_NARROW | QLW_TARGET_ASYNC;
	sc->sc_mbox[3] = 0;

	if (qlw_mbox(sc, 0x000f, 0x0001)) {
		printf("couldn't set target parameters: %x\n", sc->sc_mbox[0]);
		return (ENXIO);
	}

	for (lun = 0; lun < QLW_MAX_LUNS; lun++) {
		sc->sc_mbox[0] = QLW_MBOX_SET_DEVICE_QUEUE;
		sc->sc_mbox[1] = (((bus << 7) | target) << 8) | lun;
		sc->sc_mbox[2] = sc->sc_max_queue_depth[bus];
		sc->sc_mbox[3] = sc->sc_target[bus][target].qt_exec_throttle;
		if (qlw_mbox(sc, 0x000f, 0x0001)) {
			printf("couldn't set lun parameters: %x\n",
			    sc->sc_mbox[0]);
			return (ENXIO);
		}
	}

	return (0);
}

void
qlw_update_bus(struct qlw_softc *sc, int bus)
{
	int target;

	for (target = 0; target < QLW_MAX_TARGETS; target++)
		qlw_update_target(sc, bus, target);
}

void
qlw_update_target(struct qlw_softc *sc, int bus, int target)
{
	struct scsi_link *link;
	int lun;

	if ((sc->sc_update_required[bus] & (1 << target)) == 0)
		return;
	atomic_clearbits_int(&sc->sc_update_required[bus], (1 << target));

	link = scsi_get_link(sc->sc_scsibus[bus], target, 0);
	if (link == NULL)
		return;

	sc->sc_mbox[0] = QLW_MBOX_SET_TARGET_PARAMETERS;
	sc->sc_mbox[1] = (((bus << 7) | target) << 8);
	sc->sc_mbox[2] = sc->sc_target[bus][target].qt_params;
	sc->sc_mbox[2] |= QLW_TARGET_RENEG;
	sc->sc_mbox[2] &= ~QLW_TARGET_QFRZ;
	if (link->quirks & SDEV_NOSYNC)
		sc->sc_mbox[2] &= ~QLW_TARGET_SYNC;
	if (link->quirks & SDEV_NOWIDE)
		sc->sc_mbox[2] &= ~QLW_TARGET_WIDE;
	if (link->quirks & SDEV_NOTAGS)
		sc->sc_mbox[2] &= ~QLW_TARGET_TAGS;

	sc->sc_mbox[3] = sc->sc_target[bus][target].qt_sync_period;
	sc->sc_mbox[3] |= (sc->sc_target[bus][target].qt_sync_offset << 8);

	if (qlw_mbox(sc, 0x000f, 0x0001)) {
		printf("couldn't set target parameters: %x\n", sc->sc_mbox[0]);
		return;
	}

	/* XXX do PPR detection */

	for (lun = 0; lun < QLW_MAX_LUNS; lun++) {
		sc->sc_mbox[0] = QLW_MBOX_SET_DEVICE_QUEUE;
		sc->sc_mbox[1] = (((bus << 7) | target) << 8) | lun;
		sc->sc_mbox[2] = sc->sc_max_queue_depth[bus];
		sc->sc_mbox[3] = sc->sc_target[bus][target].qt_exec_throttle;
		if (qlw_mbox(sc, 0x000f, 0x0001)) {
			printf("couldn't set lun parameters: %x\n",
			    sc->sc_mbox[0]);
			return;
		}
	}
}

void
qlw_update_task(void *xsc)
{
	struct qlw_softc *sc = xsc;
	int bus;

	for (bus = 0; bus < sc->sc_numbusses; bus++)
		qlw_update_bus(sc, bus);
}

struct qlw_ccb *
qlw_handle_resp(struct qlw_softc *sc, u_int16_t id)
{
	struct qlw_ccb *ccb;
	struct qlw_iocb_hdr *hdr;
	struct qlw_iocb_status *status;
	struct scsi_xfer *xs;
	u_int32_t handle;
	int entry_type;
	int flags;
	int bus;

	ccb = NULL;
	hdr = QLW_DMA_KVA(sc->sc_responses) + (id * QLW_QUEUE_ENTRY_SIZE);

	bus_dmamap_sync(sc->sc_dmat,
	    QLW_DMA_MAP(sc->sc_responses), id * QLW_QUEUE_ENTRY_SIZE,
	    QLW_QUEUE_ENTRY_SIZE, BUS_DMASYNC_POSTREAD);

	qlw_get_header(sc, hdr, &entry_type, &flags);
	switch (entry_type) {
	case QLW_IOCB_STATUS:
		status = (struct qlw_iocb_status *)hdr;
		handle = qlw_swap32(sc, status->handle);
		if (handle > sc->sc_maxccbs) {
			panic("bad completed command handle: %d (> %d)",
			    handle, sc->sc_maxccbs);
		}

		ccb = &sc->sc_ccbs[handle];
		xs = ccb->ccb_xs;
		if (xs == NULL) {
			DPRINTF(QLW_D_INTR, "%s: got status for inactive"
			    " ccb %d\n", DEVNAME(sc), handle);
			qlw_dump_iocb(sc, hdr, QLW_D_INTR);
			ccb = NULL;
			break;
		}
		if (xs->io != ccb) {
			panic("completed command handle doesn't match xs "
			    "(handle %d, ccb %p, xs->io %p)", handle, ccb,
			    xs->io);
		}

		if (xs->datalen > 0) {
			bus_dmamap_sync(sc->sc_dmat, ccb->ccb_dmamap, 0,
			    ccb->ccb_dmamap->dm_mapsize,
			    (xs->flags & SCSI_DATA_IN) ? BUS_DMASYNC_POSTREAD :
			    BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->sc_dmat, ccb->ccb_dmamap);
		}

		bus = qlw_xs_bus(sc, xs);
		xs->status = qlw_swap16(sc, status->scsi_status);
		switch (qlw_swap16(sc, status->completion)) {
		case QLW_IOCB_STATUS_COMPLETE:
			if (qlw_swap16(sc, status->scsi_status) &
			    QLW_SCSI_STATUS_SENSE_VALID) {
				memcpy(&xs->sense, status->sense_data,
				    sizeof(xs->sense));
				xs->error = XS_SENSE;
			} else {
				xs->error = XS_NOERROR;
			}
			xs->resid = 0;
			break;

		case QLW_IOCB_STATUS_INCOMPLETE:
			if (flags & QLW_STATE_GOT_TARGET) {
				xs->error = XS_DRIVER_STUFFUP;
			} else {
				xs->error = XS_SELTIMEOUT;
			}
			break;

		case QLW_IOCB_STATUS_DMA_ERROR:
			DPRINTF(QLW_D_INTR, "%s: dma error\n", DEVNAME(sc));
			/* set resid apparently? */
			break;

		case QLW_IOCB_STATUS_RESET:
			DPRINTF(QLW_D_INTR, "%s: reset destroyed command\n",
			    DEVNAME(sc));
			sc->sc_marker_required[bus] = 1;
			xs->error = XS_RESET;
			break;

		case QLW_IOCB_STATUS_ABORTED:
			DPRINTF(QLW_D_INTR, "%s: aborted\n", DEVNAME(sc));
			sc->sc_marker_required[bus] = 1;
			xs->error = XS_DRIVER_STUFFUP;
			break;

		case QLW_IOCB_STATUS_TIMEOUT:
			DPRINTF(QLW_D_INTR, "%s: command timed out\n",
			    DEVNAME(sc));
			xs->error = XS_TIMEOUT;
			break;

		case QLW_IOCB_STATUS_DATA_OVERRUN:
		case QLW_IOCB_STATUS_DATA_UNDERRUN:
			xs->resid = qlw_swap32(sc, status->resid);
			xs->error = XS_NOERROR;
			break;

		case QLW_IOCB_STATUS_QUEUE_FULL:
			DPRINTF(QLW_D_INTR, "%s: queue full\n", DEVNAME(sc));
			xs->error = XS_BUSY;
			break;

		case QLW_IOCB_STATUS_WIDE_FAILED:
			DPRINTF(QLW_D_INTR, "%s: wide failed\n", DEVNAME(sc));
			xs->sc_link->quirks |= SDEV_NOWIDE;
			atomic_setbits_int(&sc->sc_update_required[bus],
			    1 << xs->sc_link->target);
			task_add(systq, &sc->sc_update_task);
			xs->resid = qlw_swap32(sc, status->resid);
			xs->error = XS_NOERROR;
			break;

		case QLW_IOCB_STATUS_SYNCXFER_FAILED:
			DPRINTF(QLW_D_INTR, "%s: sync failed\n", DEVNAME(sc));
			xs->sc_link->quirks |= SDEV_NOSYNC;
			atomic_setbits_int(&sc->sc_update_required[bus],
			    1 << xs->sc_link->target);
			task_add(systq, &sc->sc_update_task);
			xs->resid = qlw_swap32(sc, status->resid);
			xs->error = XS_NOERROR;
			break;

		default:
			DPRINTF(QLW_D_INTR, "%s: unexpected completion"
			    " status %x\n", DEVNAME(sc),
			    qlw_swap16(sc, status->completion));
			qlw_dump_iocb(sc, hdr, QLW_D_INTR);
			xs->error = XS_DRIVER_STUFFUP;
			break;
		}
		break;

	default:
		DPRINTF(QLW_D_INTR, "%s: unexpected response entry type %x\n",
		    DEVNAME(sc), entry_type);
		qlw_dump_iocb(sc, hdr, QLW_D_INTR);
		break;
	}

	return (ccb);
}

void
qlw_handle_intr(struct qlw_softc *sc, u_int16_t isr, u_int16_t info)
{
	int i;
	u_int16_t rspin;
	struct qlw_ccb *ccb;

	switch (isr) {
	case QLW_INT_TYPE_ASYNC:
		qlw_async(sc, info);
		qlw_clear_isr(sc, isr);
		break;

	case QLW_INT_TYPE_IO:
		qlw_clear_isr(sc, isr);
		rspin = qlw_queue_read(sc, QLW_RESP_IN);
		if (rspin == sc->sc_last_resp_id) {
			/* seems to happen a lot on 2200s when mbox commands
			 * complete but it doesn't want to give us the register
			 * semaphore, or something.
			 *
			 * if we're waiting on a mailbox command, don't ack
			 * the interrupt yet.
			 */
			if (sc->sc_mbox_pending) {
				DPRINTF(QLW_D_MBOX, "%s: ignoring premature"
				    " mbox int\n", DEVNAME(sc));
				return;
			}

			break;
		}

		if (sc->sc_responses == NULL)
			break;

		DPRINTF(QLW_D_IO, "%s: response queue %x=>%x\n",
		    DEVNAME(sc), sc->sc_last_resp_id, rspin);

		do {
			ccb = qlw_handle_resp(sc, sc->sc_last_resp_id);
			if (ccb)
				scsi_done(ccb->ccb_xs);

			sc->sc_last_resp_id++;
			sc->sc_last_resp_id %= sc->sc_maxresponses;
		} while (sc->sc_last_resp_id != rspin);

		qlw_queue_write(sc, QLW_RESP_OUT, rspin);
		break;

	case QLW_INT_TYPE_MBOX:
		if (sc->sc_mbox_pending) {
			if (info == QLW_MBOX_COMPLETE) {
				for (i = 1; i < nitems(sc->sc_mbox); i++) {
					sc->sc_mbox[i] = qlw_read_mbox(sc, i);
				}
			} else {
				sc->sc_mbox[0] = info;
			}
			wakeup(sc->sc_mbox);
		} else {
			DPRINTF(QLW_D_MBOX, "%s: unexpected mbox interrupt:"
			    " %x\n", DEVNAME(sc), info);
		}
		qlw_clear_isr(sc, isr);
		break;

	default:
		/* maybe log something? */
		break;
	}
}

int
qlw_intr(void *xsc)
{
	struct qlw_softc *sc = xsc;
	u_int16_t isr;
	u_int16_t info;

	if (qlw_read_isr(sc, &isr, &info) == 0)
		return (0);

	qlw_handle_intr(sc, isr, info);
	return (1);
}

void
qlw_scsi_cmd(struct scsi_xfer *xs)
{
	struct scsi_link	*link = xs->sc_link;
	struct qlw_softc	*sc = link->bus->sb_adapter_softc;
	struct qlw_ccb		*ccb;
	struct qlw_iocb_req0	*iocb;
	struct qlw_ccb_list	list;
	u_int16_t		req, rspin;
	int			offset, error, done;
	bus_dmamap_t		dmap;
	int			bus;
	int			seg;

	if (xs->cmdlen > sizeof(iocb->cdb)) {
		DPRINTF(QLW_D_IO, "%s: cdb too big (%d)\n", DEVNAME(sc),
		    xs->cmdlen);
		memset(&xs->sense, 0, sizeof(xs->sense));
		xs->sense.error_code = SSD_ERRCODE_VALID | SSD_ERRCODE_CURRENT;
		xs->sense.flags = SKEY_ILLEGAL_REQUEST;
		xs->sense.add_sense_code = 0x20;
		xs->error = XS_SENSE;
		scsi_done(xs);
		return;
	}

	ccb = xs->io;
	dmap = ccb->ccb_dmamap;
	if (xs->datalen > 0) {
		error = bus_dmamap_load(sc->sc_dmat, dmap, xs->data,
		    xs->datalen, NULL, (xs->flags & SCSI_NOSLEEP) ?
		    BUS_DMA_NOWAIT : BUS_DMA_WAITOK);
		if (error) {
			xs->error = XS_DRIVER_STUFFUP;
			scsi_done(xs);
			return;
		}

		bus_dmamap_sync(sc->sc_dmat, dmap, 0,
		    dmap->dm_mapsize,
		    (xs->flags & SCSI_DATA_IN) ? BUS_DMASYNC_PREREAD :
		    BUS_DMASYNC_PREWRITE);
	}

	mtx_enter(&sc->sc_queue_mtx);

	/* put in a sync marker if required */
	bus = qlw_xs_bus(sc, xs);
	if (sc->sc_marker_required[bus]) {
		req = sc->sc_next_req_id++;
		if (sc->sc_next_req_id == sc->sc_maxrequests)
			sc->sc_next_req_id = 0;

		DPRINTF(QLW_D_IO, "%s: writing marker at request %d\n",
		    DEVNAME(sc), req);
		offset = (req * QLW_QUEUE_ENTRY_SIZE);
		iocb = QLW_DMA_KVA(sc->sc_requests) + offset;
		bus_dmamap_sync(sc->sc_dmat, QLW_DMA_MAP(sc->sc_requests),
		    offset, QLW_QUEUE_ENTRY_SIZE, BUS_DMASYNC_POSTWRITE);
		qlw_put_marker(sc, bus, iocb);
		bus_dmamap_sync(sc->sc_dmat, QLW_DMA_MAP(sc->sc_requests),
		    offset, QLW_QUEUE_ENTRY_SIZE, BUS_DMASYNC_PREWRITE);
		qlw_queue_write(sc, QLW_REQ_IN, sc->sc_next_req_id);
		sc->sc_marker_required[bus] = 0;
	}

	req = sc->sc_next_req_id++;
	if (sc->sc_next_req_id == sc->sc_maxrequests)
		sc->sc_next_req_id = 0;

	offset = (req * QLW_QUEUE_ENTRY_SIZE);
	iocb = QLW_DMA_KVA(sc->sc_requests) + offset;
	bus_dmamap_sync(sc->sc_dmat, QLW_DMA_MAP(sc->sc_requests), offset,
	    QLW_QUEUE_ENTRY_SIZE, BUS_DMASYNC_POSTWRITE);

	ccb->ccb_xs = xs;

	DPRINTF(QLW_D_IO, "%s: writing cmd at request %d\n", DEVNAME(sc), req);
	qlw_put_cmd(sc, iocb, xs, ccb);
	seg = QLW_IOCB_SEGS_PER_CMD;

	bus_dmamap_sync(sc->sc_dmat, QLW_DMA_MAP(sc->sc_requests), offset,
	    QLW_QUEUE_ENTRY_SIZE, BUS_DMASYNC_PREWRITE);

	while (seg < ccb->ccb_dmamap->dm_nsegs) {
		req = sc->sc_next_req_id++;
		if (sc->sc_next_req_id == sc->sc_maxrequests)
			sc->sc_next_req_id = 0;

		offset = (req * QLW_QUEUE_ENTRY_SIZE);
		iocb = QLW_DMA_KVA(sc->sc_requests) + offset;
		bus_dmamap_sync(sc->sc_dmat, QLW_DMA_MAP(sc->sc_requests), offset,
		    QLW_QUEUE_ENTRY_SIZE, BUS_DMASYNC_POSTWRITE);

		DPRINTF(QLW_D_IO, "%s: writing cont at request %d\n", DEVNAME(sc), req);
		qlw_put_cont(sc, iocb, xs, ccb, seg);
		seg += QLW_IOCB_SEGS_PER_CONT;

		bus_dmamap_sync(sc->sc_dmat, QLW_DMA_MAP(sc->sc_requests), offset,
		    QLW_QUEUE_ENTRY_SIZE, BUS_DMASYNC_PREWRITE);
	}

	qlw_queue_write(sc, QLW_REQ_IN, sc->sc_next_req_id);

	if (!ISSET(xs->flags, SCSI_POLL)) {
		mtx_leave(&sc->sc_queue_mtx);
		return;
	}

	done = 0;
	SIMPLEQ_INIT(&list);
	do {
		u_int16_t isr, info;

		delay(100);

		if (qlw_read_isr(sc, &isr, &info) == 0) {
			continue;
		}

		if (isr != QLW_INT_TYPE_IO) {
			qlw_handle_intr(sc, isr, info);
			continue;
		}

		qlw_clear_isr(sc, isr);

		rspin = qlw_queue_read(sc, QLW_RESP_IN);
		while (rspin != sc->sc_last_resp_id) {
			ccb = qlw_handle_resp(sc, sc->sc_last_resp_id);

			sc->sc_last_resp_id++;
			if (sc->sc_last_resp_id == sc->sc_maxresponses)
				sc->sc_last_resp_id = 0;

			if (ccb != NULL)
				SIMPLEQ_INSERT_TAIL(&list, ccb, ccb_link);
			if (ccb == xs->io)
				done = 1;
		}
		qlw_queue_write(sc, QLW_RESP_OUT, rspin);
	} while (done == 0);

	mtx_leave(&sc->sc_queue_mtx);

	while ((ccb = SIMPLEQ_FIRST(&list)) != NULL) {
		SIMPLEQ_REMOVE_HEAD(&list, ccb_link);
		scsi_done(ccb->ccb_xs);
	}
}

u_int16_t
qlw_read(struct qlw_softc *sc, bus_size_t offset)
{
	u_int16_t v;
	v = bus_space_read_2(sc->sc_iot, sc->sc_ioh, offset);
	bus_space_barrier(sc->sc_iot, sc->sc_ioh, offset, 2,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	return (v);
}

void
qlw_write(struct qlw_softc *sc, bus_size_t offset, u_int16_t value)
{
	bus_space_write_2(sc->sc_iot, sc->sc_ioh, offset, value);
	bus_space_barrier(sc->sc_iot, sc->sc_ioh, offset, 2,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
}

u_int16_t
qlw_read_mbox(struct qlw_softc *sc, int mbox)
{
	/* could range-check mboxes according to chip type? */
	return (qlw_read(sc, sc->sc_mbox_base + (mbox * 2)));
}

void
qlw_write_mbox(struct qlw_softc *sc, int mbox, u_int16_t value)
{
	qlw_write(sc, sc->sc_mbox_base + (mbox * 2), value);
}

void
qlw_host_cmd(struct qlw_softc *sc, u_int16_t cmd)
{
	qlw_write(sc, sc->sc_host_cmd_ctrl, cmd << QLW_HOST_CMD_SHIFT);
}

#define MBOX_COMMAND_TIMEOUT	4000

int
qlw_mbox(struct qlw_softc *sc, int maskin, int maskout)
{
	int i;
	int result = 0;
	int rv;

	sc->sc_mbox_pending = 1;
	for (i = 0; i < nitems(sc->sc_mbox); i++) {
		if (maskin & (1 << i)) {
			qlw_write_mbox(sc, i, sc->sc_mbox[i]);
		}
	}
	qlw_host_cmd(sc, QLW_HOST_CMD_SET_HOST_INT);

	if (sc->sc_running == 0) {
		for (i = 0; i < MBOX_COMMAND_TIMEOUT && result == 0; i++) {
			u_int16_t isr, info;

			delay(100);

			if (qlw_read_isr(sc, &isr, &info) == 0)
				continue;

			switch (isr) {
			case QLW_INT_TYPE_MBOX:
				result = info;
				break;

			default:
				qlw_handle_intr(sc, isr, info);
				break;
			}
		}
	} else {
		tsleep_nsec(sc->sc_mbox, PRIBIO, "qlw_mbox", INFSLP);
		result = sc->sc_mbox[0];
	}

	switch (result) {
	case QLW_MBOX_COMPLETE:
		for (i = 1; i < nitems(sc->sc_mbox); i++) {
			sc->sc_mbox[i] = (maskout & (1 << i)) ?
			    qlw_read_mbox(sc, i) : 0;
		}
		rv = 0;
		break;

	case 0:
		/* timed out; do something? */
		DPRINTF(QLW_D_MBOX, "%s: mbox timed out\n", DEVNAME(sc));
		rv = 1;
		break;

	default:
		sc->sc_mbox[0] = result;
		rv = result;
		break;
	}

	qlw_clear_isr(sc, QLW_INT_TYPE_MBOX);
	sc->sc_mbox_pending = 0;
	return (rv);
}

void
qlw_mbox_putaddr(u_int16_t *mbox, struct qlw_dmamem *mem)
{
	mbox[2] = (QLW_DMA_DVA(mem) >> 16) & 0xffff;
	mbox[3] = (QLW_DMA_DVA(mem) >> 0) & 0xffff;
	mbox[6] = (QLW_DMA_DVA(mem) >> 48) & 0xffff;
	mbox[7] = (QLW_DMA_DVA(mem) >> 32) & 0xffff;
}

void
qlw_set_ints(struct qlw_softc *sc, int enabled)
{
	u_int16_t v = enabled ? (QLW_INT_REQ | QLW_RISC_INT_REQ) : 0;
	qlw_write(sc, QLW_INT_CTRL, v);
}

int
qlw_read_isr(struct qlw_softc *sc, u_int16_t *isr, u_int16_t *info)
{
	u_int16_t int_status;

	if (qlw_read(sc, QLW_SEMA) & QLW_SEMA_LOCK) {
		*info = qlw_read_mbox(sc, 0);
		if (*info & QLW_MBOX_HAS_STATUS)
			*isr = QLW_INT_TYPE_MBOX;
		else
			*isr = QLW_INT_TYPE_ASYNC;
	} else {
		int_status = qlw_read(sc, QLW_INT_STATUS);
		if ((int_status & (QLW_INT_REQ | QLW_RISC_INT_REQ)) == 0)
			return (0);

		*isr = QLW_INT_TYPE_IO;
	}

	return (1);
}

void
qlw_clear_isr(struct qlw_softc *sc, u_int16_t isr)
{
	qlw_host_cmd(sc, QLW_HOST_CMD_CLR_RISC_INT);
	switch (isr) {
	case QLW_INT_TYPE_MBOX:
	case QLW_INT_TYPE_ASYNC:
		qlw_write(sc, QLW_SEMA, 0);
		break;
	default:
		break;
	}
}

int
qlw_softreset(struct qlw_softc *sc)
{
	int i;

	qlw_set_ints(sc, 0);

	/* reset */
	qlw_write(sc, QLW_INT_CTRL, QLW_RESET);
	delay(100);
	/* clear data and control dma engines? */

	/* wait for soft reset to clear */
	for (i = 0; i < 1000; i++) {
		if ((qlw_read(sc, QLW_INT_CTRL) & QLW_RESET) == 0)
			break;

		delay(100);
	}

	if (i == 1000) {
		DPRINTF(QLW_D_INTR, "%s: reset didn't clear\n", DEVNAME(sc));
		qlw_set_ints(sc, 0);
		return (ENXIO);
	}

	qlw_write(sc, QLW_CFG1, 0);

	/* reset risc processor */
	qlw_host_cmd(sc, QLW_HOST_CMD_RESET);
	delay(100);
	qlw_write(sc, QLW_SEMA, 0);
	qlw_host_cmd(sc, QLW_HOST_CMD_RELEASE);

	/* reset queue pointers */
	qlw_queue_write(sc, QLW_REQ_IN, 0);
	qlw_queue_write(sc, QLW_REQ_OUT, 0);
	qlw_queue_write(sc, QLW_RESP_IN, 0);
	qlw_queue_write(sc, QLW_RESP_OUT, 0);

	qlw_set_ints(sc, 1);
	qlw_host_cmd(sc, QLW_HOST_CMD_BIOS);

	/* do a basic mailbox operation to check we're alive */
	sc->sc_mbox[0] = QLW_MBOX_NOP;
	if (qlw_mbox(sc, 0x0001, 0x0001)) {
		DPRINTF(QLW_D_INTR, "%s: ISP not responding after reset\n",
		    DEVNAME(sc));
		return (ENXIO);
	}

	return (0);
}

void
qlw_dma_burst_enable(struct qlw_softc *sc)
{
	if (sc->sc_isp_gen == QLW_GEN_ISP1000 ||
	    sc->sc_isp_gen == QLW_GEN_ISP1040) {
		qlw_write(sc, QLW_CDMA_CFG,
		    qlw_read(sc, QLW_CDMA_CFG) | QLW_DMA_BURST_ENABLE);
		qlw_write(sc, QLW_DDMA_CFG,
		    qlw_read(sc, QLW_DDMA_CFG) | QLW_DMA_BURST_ENABLE);
	} else {
		qlw_host_cmd(sc, QLW_HOST_CMD_PAUSE);
		qlw_write(sc, QLW_CFG1,
		    qlw_read(sc, QLW_CFG1) | QLW_DMA_BANK);
		qlw_write(sc, QLW_CDMA_CFG_1080,
		    qlw_read(sc, QLW_CDMA_CFG_1080) | QLW_DMA_BURST_ENABLE);
		qlw_write(sc, QLW_DDMA_CFG_1080,
		    qlw_read(sc, QLW_DDMA_CFG_1080) | QLW_DMA_BURST_ENABLE);
		qlw_write(sc, QLW_CFG1,
		    qlw_read(sc, QLW_CFG1) & ~QLW_DMA_BANK);
		qlw_host_cmd(sc, QLW_HOST_CMD_RELEASE);
	}
}

void
qlw_update(struct qlw_softc *sc, int task)
{
	/* do things */
}

int
qlw_async(struct qlw_softc *sc, u_int16_t info)
{
	int bus;

	switch (info) {
	case QLW_ASYNC_BUS_RESET:
		DPRINTF(QLW_D_PORT, "%s: bus reset\n", DEVNAME(sc));
		bus = qlw_read_mbox(sc, 6);
		sc->sc_marker_required[bus] = 1;
		break;

#if 0
	case QLW_ASYNC_SYSTEM_ERROR:
		qla_update(sc, QLW_UPDATE_SOFTRESET);
		break;

	case QLW_ASYNC_REQ_XFER_ERROR:
		qla_update(sc, QLW_UPDATE_SOFTRESET);
		break;

	case QLW_ASYNC_RSP_XFER_ERROR:
		qla_update(sc, QLW_UPDATE_SOFTRESET);
		break;
#endif

	case QLW_ASYNC_SCSI_CMD_COMPLETE:
		/* shouldn't happen, we disable fast posting */
		break;

	case QLW_ASYNC_CTIO_COMPLETE:
		/* definitely shouldn't happen, we don't do target mode */
		break;

	default:
		DPRINTF(QLW_D_INTR, "%s: unknown async %x\n", DEVNAME(sc),
		    info);
		break;
	}
	return (1);
}

#ifdef QLW_DEBUG
void
qlw_dump_iocb(struct qlw_softc *sc, void *buf, int flags)
{
	u_int8_t *iocb = buf;
	int l;
	int b;

	if ((qlwdebug & flags) == 0)
		return;

	printf("%s: iocb:\n", DEVNAME(sc));
	for (l = 0; l < 4; l++) {
		for (b = 0; b < 16; b++) {
			printf(" %2.2x", iocb[(l*16)+b]);
		}
		printf("\n");
	}
}

void
qlw_dump_iocb_segs(struct qlw_softc *sc, void *segs, int n)
{
	u_int8_t *buf = segs;
	int s, b;
	if ((qlwdebug & QLW_D_IOCB) == 0)
		return;

	printf("%s: iocb segs:\n", DEVNAME(sc));
	for (s = 0; s < n; s++) {
		for (b = 0; b < sizeof(struct qlw_iocb_seg); b++) {
			printf(" %2.2x", buf[(s*(sizeof(struct qlw_iocb_seg)))
			    + b]);
		}
		printf("\n");
	}
}
#endif

/*
 * The PCI bus is little-endian whereas SBus is big-endian.  This
 * leads to some differences in byte twisting of DMA transfers of
 * request and response queue entries.  Most fields can be treated as
 * 16-bit or 32-bit with the endianness of the bus, but the header
 * fields end up being swapped by the ISP1000's SBus interface.
 */

void
qlw_get_header(struct qlw_softc *sc, struct qlw_iocb_hdr *hdr,
    int *type, int *flags)
{
	if (sc->sc_isp_gen == QLW_GEN_ISP1000) {
		*type = hdr->entry_count;
		*flags = hdr->seqno;
	} else {
		*type = hdr->entry_type;
		*flags = hdr->flags;
	}
}

void
qlw_put_header(struct qlw_softc *sc, struct qlw_iocb_hdr *hdr,
    int type, int count)
{
	if (sc->sc_isp_gen == QLW_GEN_ISP1000) {
		hdr->entry_type = count;
		hdr->entry_count = type;
		hdr->seqno = 0;
		hdr->flags = 0;
	} else {
		hdr->entry_type = type;
		hdr->entry_count = count;
		hdr->seqno = 0;
		hdr->flags = 0;
	}
}

void
qlw_put_data_seg(struct qlw_softc *sc, struct qlw_iocb_seg *seg,
    bus_dmamap_t dmap, int num)
{
	seg->seg_addr = qlw_swap32(sc, dmap->dm_segs[num].ds_addr);
	seg->seg_len = qlw_swap32(sc, dmap->dm_segs[num].ds_len);
}

void
qlw_put_marker(struct qlw_softc *sc, int bus, void *buf)
{
	struct qlw_iocb_marker *marker = buf;

	qlw_put_header(sc, &marker->hdr, QLW_IOCB_MARKER, 1);

	/* could be more specific here; isp(4) isn't */
	marker->device = qlw_swap16(sc, (bus << 7) << 8);
	marker->modifier = qlw_swap16(sc, QLW_IOCB_MARKER_SYNC_ALL);
	qlw_dump_iocb(sc, buf, QLW_D_IOCB);
}

void
qlw_put_cmd(struct qlw_softc *sc, void *buf, struct scsi_xfer *xs,
    struct qlw_ccb *ccb)
{
	struct qlw_iocb_req0 *req = buf;
	int entry_count = 1;
	u_int16_t dir;
	int seg, nsegs;
	int seg_count;
	int timeout = 0;
	int bus, target, lun;

	if (xs->datalen == 0) {
		dir = QLW_IOCB_CMD_NO_DATA;
		seg_count = 1;
	} else {
		dir = xs->flags & SCSI_DATA_IN ? QLW_IOCB_CMD_READ_DATA :
		    QLW_IOCB_CMD_WRITE_DATA;
		seg_count = ccb->ccb_dmamap->dm_nsegs;
		nsegs = ccb->ccb_dmamap->dm_nsegs - QLW_IOCB_SEGS_PER_CMD;
		while (nsegs > 0) {
			entry_count++;
			nsegs -= QLW_IOCB_SEGS_PER_CONT;
		}
		for (seg = 0; seg < ccb->ccb_dmamap->dm_nsegs; seg++) {
			if (seg >= QLW_IOCB_SEGS_PER_CMD)
				break;
			qlw_put_data_seg(sc, &req->segs[seg],
			    ccb->ccb_dmamap, seg);
		}
	}

	if (sc->sc_running && (xs->sc_link->quirks & SDEV_NOTAGS) == 0)
		dir |= QLW_IOCB_CMD_SIMPLE_QUEUE;

	qlw_put_header(sc, &req->hdr, QLW_IOCB_CMD_TYPE_0, entry_count);

	/*
	 * timeout is in seconds.  make sure it's at least 1 if a timeout
	 * was specified in xs
	 */
	if (xs->timeout != 0)
		timeout = MAX(1, xs->timeout/1000);

	req->flags = qlw_swap16(sc, dir);
	req->seg_count = qlw_swap16(sc, seg_count);
	req->timeout = qlw_swap16(sc, timeout);

	bus = qlw_xs_bus(sc, xs);
	target = xs->sc_link->target;
	lun = xs->sc_link->lun;
	req->device = qlw_swap16(sc, (((bus << 7) | target) << 8) | lun);

	memcpy(req->cdb, &xs->cmd, xs->cmdlen);
	req->ccblen = qlw_swap16(sc, xs->cmdlen);

	req->handle = qlw_swap32(sc, ccb->ccb_id);

	qlw_dump_iocb(sc, buf, QLW_D_IOCB);
}

void
qlw_put_cont(struct qlw_softc *sc, void *buf, struct scsi_xfer *xs,
    struct qlw_ccb *ccb, int seg0)
{
	struct qlw_iocb_cont0 *cont = buf;
	int seg;

	qlw_put_header(sc, &cont->hdr, QLW_IOCB_CONT_TYPE_0, 1);

	for (seg = seg0; seg < ccb->ccb_dmamap->dm_nsegs; seg++) {
		if ((seg - seg0) >= QLW_IOCB_SEGS_PER_CONT)
			break;
		qlw_put_data_seg(sc, &cont->segs[seg - seg0],
		    ccb->ccb_dmamap, seg);
	}
}

#ifndef ISP_NOFIRMWARE
int
qlw_load_firmware_words(struct qlw_softc *sc, const u_int16_t *src,
    u_int16_t dest)
{
	u_int16_t i;

	for (i = 0; i < src[3]; i++) {
		sc->sc_mbox[0] = QLW_MBOX_WRITE_RAM_WORD;
		sc->sc_mbox[1] = i + dest;
		sc->sc_mbox[2] = src[i];
		if (qlw_mbox(sc, 0x07, 0x01)) {
			printf("firmware load failed\n");
			return (1);
		}
	}

	sc->sc_mbox[0] = QLW_MBOX_VERIFY_CSUM;
	sc->sc_mbox[1] = dest;
	if (qlw_mbox(sc, 0x0003, 0x0003)) {
		printf("verification of chunk at %x failed: %x\n",
		    dest, sc->sc_mbox[1]);
		return (1);
	}

	return (0);
}

int
qlw_load_firmware(struct qlw_softc *sc)
{
	return qlw_load_firmware_words(sc, sc->sc_firmware, QLW_CODE_ORG);
}

#endif	/* !ISP_NOFIRMWARE */

int
qlw_read_nvram(struct qlw_softc *sc)
{
	u_int16_t data[sizeof(sc->sc_nvram) >> 1];
	u_int16_t req, cmd, val;
	u_int8_t csum;
	int i, bit;
	int reqcmd;
	int nbits;

	if (sc->sc_nvram_size == 0)
		return (1);

	if (sc->sc_nvram_size == 128) {
		reqcmd = (QLW_NVRAM_CMD_READ << 6);
		nbits = 8;
	} else {
		reqcmd = (QLW_NVRAM_CMD_READ << 8);
		nbits = 10;
	}

	qlw_write(sc, QLW_NVRAM, QLW_NVRAM_CHIP_SEL);
	delay(10);
	qlw_write(sc, QLW_NVRAM, QLW_NVRAM_CHIP_SEL | QLW_NVRAM_CLOCK);
	delay(10);

	for (i = 0; i < (sc->sc_nvram_size >> 1); i++) {
		req = i | reqcmd;

		/* write each bit out through the nvram register */
		for (bit = nbits; bit >= 0; bit--) {
			cmd = QLW_NVRAM_CHIP_SEL;
			if ((req >> bit) & 1) {
				cmd |= QLW_NVRAM_DATA_OUT;
			}
			qlw_write(sc, QLW_NVRAM, cmd);
			delay(10);
			qlw_read(sc, QLW_NVRAM);

			qlw_write(sc, QLW_NVRAM, cmd | QLW_NVRAM_CLOCK);
			delay(10);
			qlw_read(sc, QLW_NVRAM);

			qlw_write(sc, QLW_NVRAM, cmd);
			delay(10);
			qlw_read(sc, QLW_NVRAM);
		}

		/* read the result back */
		val = 0;
		for (bit = 0; bit < 16; bit++) {
			val <<= 1;
			qlw_write(sc, QLW_NVRAM, QLW_NVRAM_CHIP_SEL |
			    QLW_NVRAM_CLOCK);
			delay(10);
			if (qlw_read(sc, QLW_NVRAM) & QLW_NVRAM_DATA_IN)
				val |= 1;
			delay(10);

			qlw_write(sc, QLW_NVRAM, QLW_NVRAM_CHIP_SEL);
			delay(10);
			qlw_read(sc, QLW_NVRAM);
		}

		qlw_write(sc, QLW_NVRAM, 0);
		delay(10);
		qlw_read(sc, QLW_NVRAM);

		data[i] = letoh16(val);
	}

	csum = 0;
	for (i = 0; i < (sc->sc_nvram_size >> 1); i++) {
		csum += data[i] & 0xff;
		csum += data[i] >> 8;
	}

	memcpy(&sc->sc_nvram, data, sizeof(sc->sc_nvram));
	/* id field should be 'ISP ', version should high enough */
	if (sc->sc_nvram.id[0] != 'I' || sc->sc_nvram.id[1] != 'S' ||
	    sc->sc_nvram.id[2] != 'P' || sc->sc_nvram.id[3] != ' ' ||
	    sc->sc_nvram.nvram_version < sc->sc_nvram_minversion ||
	    (csum != 0)) {
		printf("%s: nvram corrupt\n", DEVNAME(sc));
		return (1);
	}
	return (0);
}

void
qlw_parse_nvram_1040(struct qlw_softc *sc, int bus)
{
	struct qlw_nvram_1040 *nv = (struct qlw_nvram_1040 *)&sc->sc_nvram;
	int target;

	KASSERT(bus == 0);

	if (!ISSET(sc->sc_flags, QLW_FLAG_INITIATOR))
		sc->sc_initiator[0] = (nv->config1 >> 4);

	sc->sc_retry_count[0] = nv->retry_count;
	sc->sc_retry_delay[0] = nv->retry_delay;
	sc->sc_reset_delay[0] = nv->reset_delay;
	sc->sc_tag_age_limit[0] = nv->tag_age_limit;
	sc->sc_selection_timeout[0] = letoh16(nv->selection_timeout);
	sc->sc_max_queue_depth[0] = letoh16(nv->max_queue_depth);
	sc->sc_async_data_setup[0] = (nv->config2 & 0x0f);
	sc->sc_req_ack_active_neg[0] = ((nv->config2 & 0x10) >> 4);
	sc->sc_data_line_active_neg[0] = ((nv->config2 & 0x20) >> 5);

	for (target = 0; target < QLW_MAX_TARGETS; target++) {
		struct qlw_target *qt = &sc->sc_target[0][target];

		qt->qt_params = (nv->target[target].parameter << 8);
		qt->qt_exec_throttle = nv->target[target].execution_throttle;
		qt->qt_sync_period = nv->target[target].sync_period;
		qt->qt_sync_offset = nv->target[target].flags & 0x0f;
	}
}

void
qlw_parse_nvram_1080(struct qlw_softc *sc, int bus)
{
	struct qlw_nvram_1080 *nvram = (struct qlw_nvram_1080 *)&sc->sc_nvram;
	struct qlw_nvram_bus *nv = &nvram->bus[bus];
	int target;

	sc->sc_isp_config = nvram->isp_config;
	sc->sc_fw_features = nvram->fw_features;

	if (!ISSET(sc->sc_flags, QLW_FLAG_INITIATOR))
		sc->sc_initiator[bus] = (nv->config1 & 0x0f);

	sc->sc_retry_count[bus] = nv->retry_count;
	sc->sc_retry_delay[bus] = nv->retry_delay;
	sc->sc_reset_delay[bus] = nv->reset_delay;
	sc->sc_selection_timeout[bus] = letoh16(nv->selection_timeout);
	sc->sc_max_queue_depth[bus] = letoh16(nv->max_queue_depth);
	sc->sc_async_data_setup[bus] = (nv->config2 & 0x0f);
	sc->sc_req_ack_active_neg[bus] = ((nv->config2 & 0x10) >> 4);
	sc->sc_data_line_active_neg[bus] = ((nv->config2 & 0x20) >> 5);

	for (target = 0; target < QLW_MAX_TARGETS; target++) {
		struct qlw_target *qt = &sc->sc_target[bus][target];

		qt->qt_params = (nv->target[target].parameter << 8);
		qt->qt_exec_throttle = nv->target[target].execution_throttle;
		qt->qt_sync_period = nv->target[target].sync_period;
		if (sc->sc_isp_gen == QLW_GEN_ISP12160)
			qt->qt_sync_offset = nv->target[target].flags & 0x1f;
		else
			qt->qt_sync_offset = nv->target[target].flags & 0x0f;
	}
}

void
qlw_init_defaults(struct qlw_softc *sc, int bus)
{
	int target;

	switch (sc->sc_isp_gen) {
	case QLW_GEN_ISP1000:
		break;
	case QLW_GEN_ISP1040:
		sc->sc_isp_config = QLW_BURST_ENABLE | QLW_PCI_FIFO_64;
		break;
	case QLW_GEN_ISP1080:
	case QLW_GEN_ISP12160:
		sc->sc_isp_config = QLW_BURST_ENABLE | QLW_PCI_FIFO_128;
		sc->sc_fw_features = QLW_FW_FEATURE_LVD_NOTIFY;
		break;
	}

	sc->sc_retry_count[bus] = 0;
	sc->sc_retry_delay[bus] = 0;
	sc->sc_reset_delay[bus] = 3;
	sc->sc_tag_age_limit[bus] = 8;
	sc->sc_selection_timeout[bus] = 250;
	sc->sc_max_queue_depth[bus] = 32;
	if (sc->sc_clock > 40)
		sc->sc_async_data_setup[bus] = 9;
	else
		sc->sc_async_data_setup[bus] = 6;
	sc->sc_req_ack_active_neg[bus] = 1;
	sc->sc_data_line_active_neg[bus] = 1;

	for (target = 0; target < QLW_MAX_TARGETS; target++) {
		struct qlw_target *qt = &sc->sc_target[bus][target];

		qt->qt_params = QLW_TARGET_DEFAULT;
		qt->qt_exec_throttle = 16;
		qt->qt_sync_period = 10;
		qt->qt_sync_offset = 12;
	}
}

struct qlw_dmamem *
qlw_dmamem_alloc(struct qlw_softc *sc, size_t size)
{
	struct qlw_dmamem *m;
	int nsegs;

	m = malloc(sizeof(*m), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (m == NULL)
		return (NULL);

	m->qdm_size = size;

	if (bus_dmamap_create(sc->sc_dmat, size, 1, size, 0,
	    BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW, &m->qdm_map) != 0)
		goto qdmfree;

	if (bus_dmamem_alloc(sc->sc_dmat, size, PAGE_SIZE, 0, &m->qdm_seg, 1,
	    &nsegs, BUS_DMA_NOWAIT | BUS_DMA_ZERO) != 0)
		goto destroy;

	if (bus_dmamem_map(sc->sc_dmat, &m->qdm_seg, nsegs, size, &m->qdm_kva,
	    BUS_DMA_NOWAIT) != 0)
		goto free;

	if (bus_dmamap_load(sc->sc_dmat, m->qdm_map, m->qdm_kva, size, NULL,
	    BUS_DMA_NOWAIT) != 0)
		goto unmap;

	return (m);

unmap:
	bus_dmamem_unmap(sc->sc_dmat, m->qdm_kva, m->qdm_size);
free:
	bus_dmamem_free(sc->sc_dmat, &m->qdm_seg, 1);
destroy:
	bus_dmamap_destroy(sc->sc_dmat, m->qdm_map);
qdmfree:
	free(m, M_DEVBUF, sizeof(*m));

	return (NULL);
}

void
qlw_dmamem_free(struct qlw_softc *sc, struct qlw_dmamem *m)
{
	bus_dmamap_unload(sc->sc_dmat, m->qdm_map);
	bus_dmamem_unmap(sc->sc_dmat, m->qdm_kva, m->qdm_size);
	bus_dmamem_free(sc->sc_dmat, &m->qdm_seg, 1);
	bus_dmamap_destroy(sc->sc_dmat, m->qdm_map);
	free(m, M_DEVBUF, sizeof(*m));
}

int
qlw_alloc_ccbs(struct qlw_softc *sc)
{
	struct qlw_ccb		*ccb;
	u_int8_t		*cmd;
	int			i;

	SIMPLEQ_INIT(&sc->sc_ccb_free);
	mtx_init(&sc->sc_ccb_mtx, IPL_BIO);
	mtx_init(&sc->sc_queue_mtx, IPL_BIO);

	sc->sc_ccbs = mallocarray(sc->sc_maxccbs, sizeof(struct qlw_ccb),
	    M_DEVBUF, M_WAITOK | M_CANFAIL | M_ZERO);
	if (sc->sc_ccbs == NULL) {
		printf("%s: unable to allocate ccbs\n", DEVNAME(sc));
		return (1);
	}

	sc->sc_requests = qlw_dmamem_alloc(sc, sc->sc_maxrequests *
	    QLW_QUEUE_ENTRY_SIZE);
	if (sc->sc_requests == NULL) {
		printf("%s: unable to allocate ccb dmamem\n", DEVNAME(sc));
		goto free_ccbs;
	}
	sc->sc_responses = qlw_dmamem_alloc(sc, sc->sc_maxresponses *
	    QLW_QUEUE_ENTRY_SIZE);
	if (sc->sc_responses == NULL) {
		printf("%s: unable to allocate rcb dmamem\n", DEVNAME(sc));
		goto free_req;
	}

	cmd = QLW_DMA_KVA(sc->sc_requests);
	memset(cmd, 0, QLW_QUEUE_ENTRY_SIZE * sc->sc_maxccbs);
	for (i = 0; i < sc->sc_maxccbs; i++) {
		ccb = &sc->sc_ccbs[i];

		if (bus_dmamap_create(sc->sc_dmat, MAXPHYS,
		    QLW_MAX_SEGS, MAXPHYS, 0,
		    BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW,
		    &ccb->ccb_dmamap) != 0) {
			printf("%s: unable to create dma map\n", DEVNAME(sc));
			goto free_maps;
		}

		ccb->ccb_sc = sc;
		ccb->ccb_id = i;

		qlw_put_ccb(sc, ccb);
	}

	scsi_iopool_init(&sc->sc_iopool, sc, qlw_get_ccb, qlw_put_ccb);
	return (0);

free_maps:
	while ((ccb = qlw_get_ccb(sc)) != NULL)
		bus_dmamap_destroy(sc->sc_dmat, ccb->ccb_dmamap);

	qlw_dmamem_free(sc, sc->sc_responses);
free_req:
	qlw_dmamem_free(sc, sc->sc_requests);
free_ccbs:
	free(sc->sc_ccbs, M_DEVBUF, 0);

	return (1);
}

void
qlw_free_ccbs(struct qlw_softc *sc)
{
	struct qlw_ccb		*ccb;

	scsi_iopool_destroy(&sc->sc_iopool);
	while ((ccb = qlw_get_ccb(sc)) != NULL)
		bus_dmamap_destroy(sc->sc_dmat, ccb->ccb_dmamap);
	qlw_dmamem_free(sc, sc->sc_responses);
	qlw_dmamem_free(sc, sc->sc_requests);
	free(sc->sc_ccbs, M_DEVBUF, 0);
}

void *
qlw_get_ccb(void *xsc)
{
	struct qlw_softc	*sc = xsc;
	struct qlw_ccb		*ccb;

	mtx_enter(&sc->sc_ccb_mtx);
	ccb = SIMPLEQ_FIRST(&sc->sc_ccb_free);
	if (ccb != NULL) {
		SIMPLEQ_REMOVE_HEAD(&sc->sc_ccb_free, ccb_link);
	}
	mtx_leave(&sc->sc_ccb_mtx);
	return (ccb);
}

void
qlw_put_ccb(void *xsc, void *io)
{
	struct qlw_softc	*sc = xsc;
	struct qlw_ccb		*ccb = io;

	ccb->ccb_xs = NULL;
	mtx_enter(&sc->sc_ccb_mtx);
	SIMPLEQ_INSERT_HEAD(&sc->sc_ccb_free, ccb, ccb_link);
	mtx_leave(&sc->sc_ccb_mtx);
}
