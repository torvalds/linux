/*	$OpenBSD: qla.c,v 1.70 2025/03/06 09:56:45 kevlo Exp $ */

/*
 * Copyright (c) 2011 David Gwynne <dlg@openbsd.org>
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

#include <dev/ic/qlareg.h>
#include <dev/ic/qlavar.h>

#ifdef QLA_DEBUG
#define DPRINTF(m, f...) do { if ((qladebug & (m)) == (m)) printf(f); } \
    while (0)
#define QLA_D_MBOX		0x01
#define QLA_D_INTR		0x02
#define QLA_D_PORT		0x04
#define QLA_D_IO		0x08
#define QLA_D_IOCB		0x10
int qladebug = QLA_D_PORT;
#else
#define DPRINTF(m, f...)
#endif


#ifndef ISP_NOFIRMWARE
#include <dev/microcode/isp/asm_2100.h>
#include <dev/microcode/isp/asm_2200.h>
#include <dev/microcode/isp/asm_2300.h>
#endif

struct cfdriver qla_cd = {
	NULL,
	"qla",
	DV_DULL
};

void		qla_scsi_cmd(struct scsi_xfer *);
int		qla_scsi_probe(struct scsi_link *);

u_int16_t	qla_read(struct qla_softc *, bus_size_t);
void		qla_write(struct qla_softc *, bus_size_t, u_int16_t);
void		qla_host_cmd(struct qla_softc *sc, u_int16_t);

u_int16_t	qla_read_queue_2100(struct qla_softc *, bus_size_t);

int		qla_mbox(struct qla_softc *, int);
int		qla_sns_req(struct qla_softc *, struct qla_dmamem *, int);
void		qla_mbox_putaddr(u_int16_t *, struct qla_dmamem *);
u_int16_t	qla_read_mbox(struct qla_softc *, int);
void		qla_write_mbox(struct qla_softc *, int, u_int16_t);

void		qla_handle_intr(struct qla_softc *, u_int16_t, u_int16_t);
void		qla_set_ints(struct qla_softc *, int);
int		qla_read_isr_1G(struct qla_softc *, u_int16_t *, u_int16_t *);
int		qla_read_isr_2G(struct qla_softc *, u_int16_t *, u_int16_t *);
void		qla_clear_isr(struct qla_softc *, u_int16_t);

void		qla_update_start(struct qla_softc *, int);
void		qla_update_done(struct qla_softc *, int);
void		qla_do_update(void *);

void		qla_put_marker(struct qla_softc *, void *);
void		qla_put_cmd(struct qla_softc *, void *, struct scsi_xfer *,
		    struct qla_ccb *);
struct qla_ccb *qla_handle_resp(struct qla_softc *, u_int16_t);

int		qla_get_port_name_list(struct qla_softc *, u_int32_t);
struct qla_fc_port *qla_next_fabric_port(struct qla_softc *, u_int32_t *,
		    u_int32_t *);
int		qla_get_port_db(struct qla_softc *c, u_int16_t,
		    struct qla_dmamem *);
int		qla_add_loop_port(struct qla_softc *, struct qla_fc_port *);
int		qla_add_fabric_port(struct qla_softc *, struct qla_fc_port *);
int		qla_add_logged_in_port(struct qla_softc *, int, u_int32_t);
int		qla_classify_port(struct qla_softc *, u_int32_t, u_int64_t,
		    u_int64_t, struct qla_fc_port **);
int		qla_get_loop_id(struct qla_softc *sc, int);
void		qla_clear_port_lists(struct qla_softc *);
int		qla_softreset(struct qla_softc *);
void		qla_update_topology(struct qla_softc *);
int		qla_update_fabric(struct qla_softc *);
int		qla_fabric_plogi(struct qla_softc *, struct qla_fc_port *);
void		qla_fabric_plogo(struct qla_softc *, struct qla_fc_port *);

void		qla_update_start(struct qla_softc *, int);
int		qla_async(struct qla_softc *, u_int16_t);

int		qla_verify_firmware(struct qla_softc *sc, u_int16_t);
int		qla_load_firmware_words(struct qla_softc *, const u_int16_t *,
		    u_int16_t);
int		qla_load_firmware_2100(struct qla_softc *);
int		qla_load_firmware_2200(struct qla_softc *);
int		qla_load_fwchunk_2300(struct qla_softc *,
		    struct qla_dmamem *, const u_int16_t *, u_int32_t);
int		qla_load_firmware_2300(struct qla_softc *);
int		qla_load_firmware_2322(struct qla_softc *);
int		qla_read_nvram(struct qla_softc *);

struct qla_dmamem *qla_dmamem_alloc(struct qla_softc *, size_t);
void		qla_dmamem_free(struct qla_softc *, struct qla_dmamem *);

int		qla_alloc_ccbs(struct qla_softc *);
void		qla_free_ccbs(struct qla_softc *);
void		*qla_get_ccb(void *);
void		qla_put_ccb(void *, void *);

void		qla_dump_iocb(struct qla_softc *, void *);
void		qla_dump_iocb_segs(struct qla_softc *, void *, int);

static const struct qla_regs qla_regs_2100 = {
	qla_read_queue_2100,
	qla_read_isr_1G,
	QLA_MBOX_BASE_2100 + 0x8,
	QLA_MBOX_BASE_2100 + 0x8,
	QLA_MBOX_BASE_2100 + 0xa,
	QLA_MBOX_BASE_2100 + 0xa
};

static const struct qla_regs qla_regs_2200 = {
	qla_read,
	qla_read_isr_1G,
	QLA_MBOX_BASE_2200 + 0x8,
	QLA_MBOX_BASE_2200 + 0x8,
	QLA_MBOX_BASE_2200 + 0xa,
	QLA_MBOX_BASE_2200 + 0xa
};

static const struct qla_regs qla_regs_23XX = {
	qla_read,
	qla_read_isr_2G,
	QLA_REQ_IN,
	QLA_REQ_OUT,
	QLA_RESP_IN,
	QLA_RESP_OUT
};

#define qla_queue_read(_sc, _r) ((*(_sc)->sc_regs->read)((_sc), (_r)))
#define qla_queue_write(_sc, _r, _v) qla_write((_sc), (_r), (_v))

#define qla_read_isr(_sc, _isr, _info) \
    ((*(_sc)->sc_regs->read_isr)((_sc), (_isr), (_info)))

const struct scsi_adapter qla_switch = {
	qla_scsi_cmd, NULL, qla_scsi_probe, NULL, NULL
};

int
qla_classify_port(struct qla_softc *sc, u_int32_t location,
    u_int64_t port_name, u_int64_t node_name, struct qla_fc_port **prev)
{
	struct qla_fc_port *port, *locmatch, *wwnmatch;
	locmatch = NULL;
	wwnmatch = NULL;

	/* make sure we don't try to add a port or location twice */
	TAILQ_FOREACH(port, &sc->sc_ports_new, update) {
		if ((port->port_name == port_name &&
		    port->node_name == node_name) ||
		    port->location == location) {
			*prev = port;
			return (QLA_PORT_DISP_DUP);
		}
	}

	/* if we're attaching, everything is new */
	if (sc->sc_scsibus == NULL) {
		*prev = NULL;
		return (QLA_PORT_DISP_NEW);
	}

	TAILQ_FOREACH(port, &sc->sc_ports, ports) {
		if (port->location == location)
			locmatch = port;

		if (port->port_name == port_name &&
		    port->node_name == node_name)
			wwnmatch = port;
	}

	if (locmatch == NULL && wwnmatch == NULL) {
		*prev = NULL;
		return (QLA_PORT_DISP_NEW);
	} else if (locmatch == wwnmatch) {
		*prev = locmatch;
		return (QLA_PORT_DISP_SAME);
	} else if (wwnmatch != NULL) {
		*prev = wwnmatch;
		return (QLA_PORT_DISP_MOVED);
	} else {
		*prev = locmatch;
		return (QLA_PORT_DISP_CHANGED);
	}
}

int
qla_get_loop_id(struct qla_softc *sc, int start)
{
	int i, last;

	if (sc->sc_2k_logins) {
		i = QLA_2KL_MIN_HANDLE;
		last = QLA_2KL_MAX_HANDLE;
	} else {
		/* if we're an F port, we can have two ranges, but meh */
		i = QLA_MIN_HANDLE;
		last = QLA_MAX_HANDLE;
	}
	if (i < start)
		i = start;

	for (; i <= last; i++) {
		if (sc->sc_targets[i] == NULL)
			return (i);
	}

	return (-1);
}

int
qla_get_port_db(struct qla_softc *sc, u_int16_t loopid, struct qla_dmamem *mem)
{
	sc->sc_mbox[0] = QLA_MBOX_GET_PORT_DB;
	if (sc->sc_2k_logins) {
		sc->sc_mbox[1] = loopid;
	} else {
		sc->sc_mbox[1] = loopid << 8;
	}

	memset(QLA_DMA_KVA(mem), 0, sizeof(struct qla_get_port_db));
	qla_mbox_putaddr(sc->sc_mbox, mem);
	bus_dmamap_sync(sc->sc_dmat, QLA_DMA_MAP(mem), 0,
	    sizeof(struct qla_get_port_db), BUS_DMASYNC_PREREAD);
	if (qla_mbox(sc, 0x00cf)) {
		DPRINTF(QLA_D_PORT, "%s: get port db %d failed: %x\n",
		    DEVNAME(sc), loopid, sc->sc_mbox[0]);
		return (1);
	}

	bus_dmamap_sync(sc->sc_dmat, QLA_DMA_MAP(mem), 0,
	    sizeof(struct qla_get_port_db), BUS_DMASYNC_POSTREAD);
	return (0);
}

int
qla_add_loop_port(struct qla_softc *sc, struct qla_fc_port *port)
{
	struct qla_get_port_db *pdb;
	struct qla_fc_port *pport = NULL;
	int disp;

	if (qla_get_port_db(sc, port->loopid, sc->sc_scratch)) {
		return (1);
	}
	pdb = QLA_DMA_KVA(sc->sc_scratch);

	if (letoh16(pdb->prli_svc_word3) & QLA_SVC3_TARGET_ROLE)
		port->flags |= QLA_PORT_FLAG_IS_TARGET;

	port->port_name = betoh64(pdb->port_name);
	port->node_name = betoh64(pdb->node_name);
	port->portid = (letoh16(pdb->port_id[0]) << 16) |
	    letoh16(pdb->port_id[1]);

	mtx_enter(&sc->sc_port_mtx);
	disp = qla_classify_port(sc, port->location, port->port_name,
	    port->node_name, &pport);
	switch (disp) {
	case QLA_PORT_DISP_CHANGED:
	case QLA_PORT_DISP_MOVED:
	case QLA_PORT_DISP_NEW:
		TAILQ_INSERT_TAIL(&sc->sc_ports_new, port, update);
		sc->sc_targets[port->loopid] = port;
		break;
	case QLA_PORT_DISP_DUP:
		free(port, M_DEVBUF, sizeof *port);
		break;
	case QLA_PORT_DISP_SAME:
		TAILQ_REMOVE(&sc->sc_ports_gone, pport, update);
		free(port, M_DEVBUF, sizeof *port);
		break;
	}
	mtx_leave(&sc->sc_port_mtx);

	switch (disp) {
	case QLA_PORT_DISP_CHANGED:
	case QLA_PORT_DISP_MOVED:
	case QLA_PORT_DISP_NEW:
		DPRINTF(QLA_D_PORT, "%s: %s %d; name %llx, port %06x\n",
		    DEVNAME(sc), ISSET(port->flags, QLA_PORT_FLAG_IS_TARGET) ?
		    "target" : "non-target", port->loopid, port->port_name,
		    port->portid);
		break;
	}
	return (0);
}

int
qla_add_fabric_port(struct qla_softc *sc, struct qla_fc_port *port)
{
	struct qla_get_port_db *pdb;

	if (qla_get_port_db(sc, port->loopid, sc->sc_scratch)) {
		return (1);
	}
	pdb = QLA_DMA_KVA(sc->sc_scratch);

	if (letoh16(pdb->prli_svc_word3) & QLA_SVC3_TARGET_ROLE)
		port->flags |= QLA_PORT_FLAG_IS_TARGET;

	/*
	 * if we only know about this port because qla_get_port_name_list
	 * returned it, we don't have its port id or node name, so fill
	 * those in and update its location.
	 */
	if (port->location == QLA_LOCATION_FABRIC) {
		port->node_name = betoh64(pdb->node_name);
		port->port_name = betoh64(pdb->port_name);
		port->portid = (letoh16(pdb->port_id[0]) << 16) |
		    letoh16(pdb->port_id[1]);
		port->location = QLA_LOCATION_PORT_ID(port->portid);
	}

	mtx_enter(&sc->sc_port_mtx);
	TAILQ_INSERT_TAIL(&sc->sc_ports_new, port, update);
	sc->sc_targets[port->loopid] = port;
	mtx_leave(&sc->sc_port_mtx);

	DPRINTF(QLA_D_PORT, "%s: %s %d; name %llx\n",
	    DEVNAME(sc), ISSET(port->flags, QLA_PORT_FLAG_IS_TARGET) ?
	    "target" : "non-target", port->loopid, port->port_name);
	return (0);
}

int
qla_add_logged_in_port(struct qla_softc *sc, int loopid, u_int32_t portid)
{
	struct qla_fc_port *port;
	struct qla_get_port_db *pdb;
	u_int64_t node_name, port_name;
	int flags, ret;

	ret = qla_get_port_db(sc, loopid, sc->sc_scratch);
	mtx_enter(&sc->sc_port_mtx);
	if (ret != 0) {
		/* put in a fake port to prevent use of this loop id */
		printf("%s: loop id %d used, but can't see what's using it\n",
		    DEVNAME(sc), loopid);
		node_name = 0;
		port_name = 0;
		flags = 0;
	} else {
		pdb = QLA_DMA_KVA(sc->sc_scratch);
		node_name = betoh64(pdb->node_name);
		port_name = betoh64(pdb->port_name);
		flags = 0;
		if (letoh16(pdb->prli_svc_word3) & QLA_SVC3_TARGET_ROLE)
			flags |= QLA_PORT_FLAG_IS_TARGET;

		/* see if we've already found this port */
		TAILQ_FOREACH(port, &sc->sc_ports_found, update) {
			if ((port->node_name == node_name) &&
			    (port->port_name == port_name) &&
			    (port->portid == portid)) {
				mtx_leave(&sc->sc_port_mtx);
				DPRINTF(QLA_D_PORT, "%s: already found port "
				    "%06x\n", DEVNAME(sc), portid);
				return (0);
			}
		}
	}

	port = malloc(sizeof(*port), M_DEVBUF, M_ZERO | M_NOWAIT);
	if (port == NULL) {
		mtx_leave(&sc->sc_port_mtx);
		printf("%s: failed to allocate a port structure\n",
		    DEVNAME(sc));
		return (1);
	}
	port->location = QLA_LOCATION_PORT_ID(portid);
	port->port_name = port_name;
	port->node_name = node_name;
	port->loopid = loopid;
	port->portid = portid;
	port->flags = flags;

	TAILQ_INSERT_TAIL(&sc->sc_ports, port, ports);
	sc->sc_targets[port->loopid] = port;
	mtx_leave(&sc->sc_port_mtx);

	DPRINTF(QLA_D_PORT, "%s: added logged in port %06x at %d\n",
	    DEVNAME(sc), portid, loopid);
	return (0);
}

int
qla_attach(struct qla_softc *sc)
{
	struct scsibus_attach_args saa;
	struct qla_init_cb *icb;
#ifndef ISP_NOFIRMWARE
	int (*loadfirmware)(struct qla_softc *) = NULL;
#endif
	u_int16_t firmware_addr = 0;
	u_int64_t dva;
	int i, rv;

	TAILQ_INIT(&sc->sc_ports);
	TAILQ_INIT(&sc->sc_ports_new);
	TAILQ_INIT(&sc->sc_ports_gone);
	TAILQ_INIT(&sc->sc_ports_found);

	switch (sc->sc_isp_gen) {
	case QLA_GEN_ISP2100:
		sc->sc_mbox_base = QLA_MBOX_BASE_2100;
		sc->sc_regs = &qla_regs_2100;
#ifndef ISP_NOFIRMWARE
		loadfirmware = qla_load_firmware_2100;
#endif
		firmware_addr = QLA_2100_CODE_ORG;
		break;

	case QLA_GEN_ISP2200:
		sc->sc_mbox_base = QLA_MBOX_BASE_2200;
		sc->sc_regs = &qla_regs_2200;
#ifndef ISP_NOFIRMWARE
		loadfirmware = qla_load_firmware_2200;
#endif
		firmware_addr = QLA_2200_CODE_ORG;
		break;

	case QLA_GEN_ISP23XX:
		sc->sc_mbox_base = QLA_MBOX_BASE_23XX;
		sc->sc_regs = &qla_regs_23XX;
#ifndef ISP_NOFIRMWARE
		if (sc->sc_isp_type != QLA_ISP2322)
			loadfirmware = qla_load_firmware_2300;
#endif
		firmware_addr = QLA_2300_CODE_ORG;
		break;

	default:
		printf("unknown isp type\n");
		return (ENXIO);
	}

	/* after reset, mbox registers 1-3 should contain the string "ISP   " */
	if (qla_read_mbox(sc, 1) != 0x4953 ||
	    qla_read_mbox(sc, 2) != 0x5020 ||
	    qla_read_mbox(sc, 3) != 0x2020) {
		/* try releasing the risc processor */
		qla_host_cmd(sc, QLA_HOST_CMD_RELEASE);
	}

	qla_host_cmd(sc, QLA_HOST_CMD_PAUSE);
	if (qla_softreset(sc) != 0) {
		printf("softreset failed\n");
		return (ENXIO);
	}

	if (qla_read_nvram(sc) == 0) {
		sc->sc_nvram_valid = 1;
		if (sc->sc_port_name == 0)
			sc->sc_port_name = betoh64(sc->sc_nvram.port_name);
		if (sc->sc_node_name == 0)
			sc->sc_node_name = betoh64(sc->sc_nvram.node_name);
	}

	if (sc->sc_port_name == 0)
		sc->sc_port_name = QLA_DEFAULT_PORT_NAME;

#ifdef ISP_NOFIRMWARE
	if (qla_verify_firmware(sc, firmware_addr)) {
		printf("%s: no firmware loaded\n", DEVNAME(sc));
		return (ENXIO);
	}
#else
	if (loadfirmware && (loadfirmware)(sc)) {
		printf("%s: firmware load failed\n", DEVNAME(sc));
		return (ENXIO);
	}
#endif

	/* execute firmware */
	sc->sc_mbox[0] = QLA_MBOX_EXEC_FIRMWARE;
	sc->sc_mbox[1] = firmware_addr;
#ifdef ISP_NOFIRMWARE
	sc->sc_mbox[2] = 1;
#else
	if (loadfirmware)
		sc->sc_mbox[2] = 0;
	else
		sc->sc_mbox[2] = 1;
#endif
	if (qla_mbox(sc, 0x0007)) {
		printf("ISP couldn't exec firmware: %x\n", sc->sc_mbox[0]);
		return (ENXIO);
	}

	delay(250000);		/* from isp(4) */

	sc->sc_mbox[0] = QLA_MBOX_ABOUT_FIRMWARE;
	if (qla_mbox(sc, 0x0001)) {
		printf("ISP not talking after firmware exec: %x\n",
		    sc->sc_mbox[0]);
		return (ENXIO);
	}
	printf("%s: firmware rev %d.%d.%d, attrs 0x%x\n", DEVNAME(sc),
	    sc->sc_mbox[1], sc->sc_mbox[2], sc->sc_mbox[3], sc->sc_mbox[6]);

	if (sc->sc_mbox[6] & QLA_FW_ATTR_EXPANDED_LUN)
		sc->sc_expanded_lun = 1;
	if (sc->sc_mbox[6] & QLA_FW_ATTR_FABRIC)
		sc->sc_fabric = 1;
	if (sc->sc_mbox[6] & QLA_FW_ATTR_2K_LOGINS)
		sc->sc_2k_logins = 1;

	/* work out how many ccbs to allocate */
	sc->sc_mbox[0] = QLA_MBOX_GET_FIRMWARE_STATUS;
	if (qla_mbox(sc, 0x0001)) {
		printf("couldn't get firmware status: %x\n", sc->sc_mbox[0]);
		return (ENXIO);
	}
	sc->sc_maxcmds = sc->sc_mbox[2];

	if (qla_alloc_ccbs(sc)) {
		/* error already printed */
		return (ENOMEM);
	}
	sc->sc_scratch = qla_dmamem_alloc(sc, QLA_SCRATCH_SIZE);
	if (sc->sc_scratch == NULL) {
		printf("%s: unable to allocate scratch\n", DEVNAME(sc));
		goto free_ccbs;
	}

	/* build init buffer thing */
	icb = (struct qla_init_cb *)QLA_DMA_KVA(sc->sc_scratch);
	memset(icb, 0, sizeof(*icb));
	icb->icb_version = QLA_ICB_VERSION;
	/* port and node names are big-endian in the icb */
	htobem32(&icb->icb_portname_hi, sc->sc_port_name >> 32);
	htobem32(&icb->icb_portname_lo, sc->sc_port_name);
	htobem32(&icb->icb_nodename_hi, sc->sc_node_name >> 32);
	htobem32(&icb->icb_nodename_lo, sc->sc_node_name);
	if (sc->sc_nvram_valid) {
		icb->icb_fw_options = sc->sc_nvram.fw_options;
		icb->icb_max_frame_len = sc->sc_nvram.frame_payload_size;
		icb->icb_max_alloc = sc->sc_nvram.max_iocb_allocation;
		icb->icb_exec_throttle = sc->sc_nvram.execution_throttle;
		icb->icb_retry_count = sc->sc_nvram.retry_count;
		icb->icb_retry_delay = sc->sc_nvram.retry_delay;
		icb->icb_hardaddr = sc->sc_nvram.hard_address;
		icb->icb_inquiry_data = sc->sc_nvram.inquiry_data;
		icb->icb_login_timeout = sc->sc_nvram.login_timeout;
		icb->icb_xfwoptions = sc->sc_nvram.add_fw_options;
		icb->icb_zfwoptions = sc->sc_nvram.special_options;
	} else {
		/* defaults copied from isp(4) */
		icb->icb_retry_count = 3;
		icb->icb_retry_delay = 5;
		icb->icb_exec_throttle = htole16(16);
		icb->icb_max_alloc = htole16(256);
		icb->icb_max_frame_len = htole16(1024);
		icb->icb_fw_options = htole16(QLA_ICB_FW_FAIRNESS |
		    QLA_ICB_FW_ENABLE_PDB_CHANGED | QLA_ICB_FW_HARD_ADDR |
		    QLA_ICB_FW_FULL_DUPLEX);
	}
	/* target mode stuff that we don't care about */
	icb->icb_lun_enables = 0;
	icb->icb_cmd_count = 0;
	icb->icb_notify_count = 0;
	icb->icb_lun_timeout = 0;

	/* "zero interrupt operation" */
	icb->icb_int_delaytimer = 0;

	icb->icb_req_out = 0;
	icb->icb_resp_in = 0;
	htolem16(&icb->icb_req_queue_len, sc->sc_maxcmds);
	htolem16(&icb->icb_resp_queue_len, sc->sc_maxcmds);
	dva = QLA_DMA_DVA(sc->sc_requests);
	htolem32(&icb->icb_req_queue_addr_lo, dva);
	htolem32(&icb->icb_req_queue_addr_hi, dva >> 32);
	dva = QLA_DMA_DVA(sc->sc_responses);
	htolem32(&icb->icb_resp_queue_addr_lo, dva);
	htolem32(&icb->icb_resp_queue_addr_hi, dva >> 32);

	/* adjust firmware options a bit */
	icb->icb_fw_options |= htole16(QLA_ICB_FW_EXTENDED_INIT_CB);
	icb->icb_fw_options &= ~htole16(QLA_ICB_FW_FAST_POST);

	sc->sc_mbox[0] = QLA_MBOX_INIT_FIRMWARE;
	sc->sc_mbox[4] = 0;
	sc->sc_mbox[5] = 0;
	qla_mbox_putaddr(sc->sc_mbox, sc->sc_scratch);
	bus_dmamap_sync(sc->sc_dmat, QLA_DMA_MAP(sc->sc_scratch), 0,
	    sizeof(*icb), BUS_DMASYNC_PREWRITE);
	rv = qla_mbox(sc, 0x00fd);
	bus_dmamap_sync(sc->sc_dmat, QLA_DMA_MAP(sc->sc_scratch), 0,
	    sizeof(*icb), BUS_DMASYNC_POSTWRITE);

	if (rv != 0) {
		printf("%s: ISP firmware init failed: %x\n", DEVNAME(sc),
		    sc->sc_mbox[0]);
		goto free_scratch;
	}

	/* enable some more notifications */
	sc->sc_mbox[0] = QLA_MBOX_SET_FIRMWARE_OPTIONS;
	sc->sc_mbox[1] = QLA_FW_OPTION1_ASYNC_LIP_F8 |
	    QLA_FW_OPTION1_ASYNC_LIP_RESET |
	    QLA_FW_OPTION1_ASYNC_LIP_ERROR |
	    QLA_FW_OPTION1_ASYNC_LOGIN_RJT;
	sc->sc_mbox[2] = 0;
	sc->sc_mbox[3] = 0;
	if (qla_mbox(sc, 0x000f)) {
		printf("%s: setting firmware options failed: %x\n",
		    DEVNAME(sc), sc->sc_mbox[0]);
		goto free_scratch;
	}

	sc->sc_update_taskq = taskq_create(DEVNAME(sc), 1, IPL_BIO, 0);
	task_set(&sc->sc_update_task, qla_do_update, sc);

	/* wait a bit for link to come up so we can scan and attach devices */
	for (i = 0; i < QLA_WAIT_FOR_LOOP * 10000; i++) {
		u_int16_t isr, info;

		delay(100);

		if (qla_read_isr(sc, &isr, &info) == 0)
			continue;

		qla_handle_intr(sc, isr, info);

		if (sc->sc_loop_up)
			break;
	}

	if (sc->sc_loop_up) {
		qla_do_update(sc);
	} else {
		DPRINTF(QLA_D_PORT, "%s: loop still down, giving up\n",
		    DEVNAME(sc));
	}

	saa.saa_adapter = &qla_switch;
	saa.saa_adapter_softc = sc;
	if (sc->sc_2k_logins) {
		saa.saa_adapter_buswidth = QLA_2KL_BUSWIDTH;
	} else {
		saa.saa_adapter_buswidth = QLA_BUSWIDTH;
	}
	saa.saa_adapter_target = SDEV_NO_ADAPTER_TARGET;
	saa.saa_luns = 8;
	saa.saa_openings = sc->sc_maxcmds;
	saa.saa_pool = &sc->sc_iopool;
	saa.saa_wwpn = sc->sc_port_name;
	saa.saa_wwnn = sc->sc_node_name;
	if (saa.saa_wwnn == 0) {
		/*
		 * mask out the port number from the port name to get
		 * the node name.
		 */
		saa.saa_wwnn = saa.saa_wwpn;
		saa.saa_wwnn &= ~(0xfULL << 56);
	}
	saa.saa_quirks = saa.saa_flags = 0;

	sc->sc_scsibus = (struct scsibus_softc *)config_found(&sc->sc_dev,
	    &saa, scsiprint);

	return(0);

free_scratch:
	qla_dmamem_free(sc, sc->sc_scratch);
free_ccbs:
	qla_free_ccbs(sc);
	return (ENXIO);
}

int
qla_detach(struct qla_softc *sc, int flags)
{
	return (0);
}

struct qla_ccb *
qla_handle_resp(struct qla_softc *sc, u_int16_t id)
{
	struct qla_ccb *ccb;
	struct qla_iocb_status *status;
	struct scsi_xfer *xs;
	u_int32_t handle;
	u_int8_t *entry;

	ccb = NULL;
	entry = QLA_DMA_KVA(sc->sc_responses) + (id * QLA_QUEUE_ENTRY_SIZE);

	bus_dmamap_sync(sc->sc_dmat,
	    QLA_DMA_MAP(sc->sc_responses), id * QLA_QUEUE_ENTRY_SIZE,
	    QLA_QUEUE_ENTRY_SIZE, BUS_DMASYNC_POSTREAD);

	qla_dump_iocb(sc, entry);
	switch (entry[0]) {
	case QLA_IOCB_STATUS:
		status = (struct qla_iocb_status *)entry;
		handle = status->handle;
		if (handle > sc->sc_maxcmds) {
			panic("bad completed command handle: %d (> %d)",
			    handle, sc->sc_maxcmds);
		}

		ccb = &sc->sc_ccbs[handle];
		xs = ccb->ccb_xs;
		if (xs == NULL) {
			DPRINTF(QLA_D_INTR, "%s: got status for inactive"
			    " ccb %d\n", DEVNAME(sc), handle);
			ccb = NULL;
			break;
		}
		if (xs->io != ccb) {
			panic("completed command handle doesn't match xs "
			    "(handle %d, ccb %p, xs->io %p)", handle, ccb,
			    xs->io);
		}

		if (xs->datalen > 0) {
			if (ccb->ccb_dmamap->dm_nsegs >
			    QLA_IOCB_SEGS_PER_CMD) {
				bus_dmamap_sync(sc->sc_dmat,
				    QLA_DMA_MAP(sc->sc_segments),
				    ccb->ccb_seg_offset,
				    sizeof(*ccb->ccb_t4segs) *
				    ccb->ccb_dmamap->dm_nsegs,
				    BUS_DMASYNC_POSTWRITE);
			}

			bus_dmamap_sync(sc->sc_dmat, ccb->ccb_dmamap, 0,
			    ccb->ccb_dmamap->dm_mapsize,
			    (xs->flags & SCSI_DATA_IN) ? BUS_DMASYNC_POSTREAD :
			    BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->sc_dmat, ccb->ccb_dmamap);
		}

		xs->status = letoh16(status->scsi_status);
		switch (letoh16(status->completion)) {
		case QLA_IOCB_STATUS_COMPLETE:
			if (letoh16(status->scsi_status) &
			    QLA_SCSI_STATUS_SENSE_VALID) {
				memcpy(&xs->sense, status->sense_data,
				    sizeof(xs->sense));
				xs->error = XS_SENSE;
			} else {
				xs->error = XS_NOERROR;
			}
			xs->resid = 0;
			break;

		case QLA_IOCB_STATUS_DMA_ERROR:
			DPRINTF(QLA_D_INTR, "%s: dma error\n", DEVNAME(sc));
			/* set resid apparently? */
			break;

		case QLA_IOCB_STATUS_RESET:
			DPRINTF(QLA_D_IO, "%s: reset destroyed command\n",
			    DEVNAME(sc));
			sc->sc_marker_required = 1;
			xs->error = XS_RESET;
			break;

		case QLA_IOCB_STATUS_ABORTED:
			DPRINTF(QLA_D_IO, "%s: aborted\n", DEVNAME(sc));
			sc->sc_marker_required = 1;
			xs->error = XS_DRIVER_STUFFUP;
			break;

		case QLA_IOCB_STATUS_TIMEOUT:
			DPRINTF(QLA_D_IO, "%s: command timed out\n",
			    DEVNAME(sc));
			xs->error = XS_TIMEOUT;
			break;

		case QLA_IOCB_STATUS_DATA_OVERRUN:
		case QLA_IOCB_STATUS_DATA_UNDERRUN:
			xs->resid = letoh32(status->resid);
			xs->error = XS_NOERROR;
			break;

		case QLA_IOCB_STATUS_QUEUE_FULL:
			DPRINTF(QLA_D_IO, "%s: queue full\n", DEVNAME(sc));
			xs->error = XS_BUSY;
			break;

		case QLA_IOCB_STATUS_PORT_UNAVAIL:
		case QLA_IOCB_STATUS_PORT_LOGGED_OUT:
		case QLA_IOCB_STATUS_PORT_CHANGED:
			DPRINTF(QLA_D_IO, "%s: dev gone\n", DEVNAME(sc));
			xs->error = XS_SELTIMEOUT;
			break;

		default:
			DPRINTF(QLA_D_INTR, "%s: unexpected completion"
			    " status %x\n", DEVNAME(sc), status->completion);
			xs->error = XS_DRIVER_STUFFUP;
			break;
		}
		break;

	case QLA_IOCB_STATUS_CONT:
		DPRINTF(QLA_D_INTR, "%s: ignoring status continuation iocb\n",
		    DEVNAME(sc));
		break;

		/* check for requests that bounce back? */
	default:
		DPRINTF(QLA_D_INTR, "%s: unexpected response entry type %x\n",
		    DEVNAME(sc), entry[0]);
		break;
	}

	return (ccb);
}

void
qla_handle_intr(struct qla_softc *sc, u_int16_t isr, u_int16_t info)
{
	int i;
	u_int16_t rspin;
	struct qla_ccb *ccb;

	switch (isr) {
	case QLA_INT_TYPE_ASYNC:
		qla_async(sc, info);
		break;

	case QLA_INT_TYPE_IO:
		rspin = qla_queue_read(sc, sc->sc_regs->res_in);
		if (rspin == sc->sc_last_resp_id) {
			/* seems to happen a lot on 2200s when mbox commands
			 * complete but it doesn't want to give us the register
			 * semaphore, or something.
			 *
			 * if we're waiting on a mailbox command, don't ack
			 * the interrupt yet.
			 */
			if (sc->sc_mbox_pending) {
				DPRINTF(QLA_D_MBOX, "%s: ignoring premature"
				    " mbox int\n", DEVNAME(sc));
				return;
			}

			break;
		}

		if (sc->sc_responses == NULL)
			break;

		DPRINTF(QLA_D_IO, "%s: response queue %x=>%x\n",
		    DEVNAME(sc), sc->sc_last_resp_id, rspin);

		do {
			ccb = qla_handle_resp(sc, sc->sc_last_resp_id);
			if (ccb)
				scsi_done(ccb->ccb_xs);

			sc->sc_last_resp_id++;
			sc->sc_last_resp_id %= sc->sc_maxcmds;
		} while (sc->sc_last_resp_id != rspin);

		qla_queue_write(sc, sc->sc_regs->res_out, rspin);
		break;

	case QLA_INT_TYPE_MBOX:
		mtx_enter(&sc->sc_mbox_mtx);
		if (sc->sc_mbox_pending) {
			DPRINTF(QLA_D_MBOX, "%s: mbox response %x\n",
			    DEVNAME(sc), info);
			for (i = 0; i < nitems(sc->sc_mbox); i++) {
				sc->sc_mbox[i] = qla_read_mbox(sc, i);
			}
			sc->sc_mbox_pending = 2;
			wakeup(sc->sc_mbox);
			mtx_leave(&sc->sc_mbox_mtx);
		} else {
			mtx_leave(&sc->sc_mbox_mtx);
			DPRINTF(QLA_D_MBOX, "%s: unexpected mbox interrupt:"
			    " %x\n", DEVNAME(sc), info);
		}
		break;

	default:
		/* maybe log something? */
		break;
	}

	qla_clear_isr(sc, isr);
}

int
qla_intr(void *xsc)
{
	struct qla_softc *sc = xsc;
	u_int16_t isr;
	u_int16_t info;

	if (qla_read_isr(sc, &isr, &info) == 0)
		return (0);

	qla_handle_intr(sc, isr, info);
	return (1);
}

int
qla_scsi_probe(struct scsi_link *link)
{
	struct qla_softc *sc = link->bus->sb_adapter_softc;
	int rv = 0;

	mtx_enter(&sc->sc_port_mtx);
	if (sc->sc_targets[link->target] == NULL)
		rv = ENXIO;
	else if (!ISSET(sc->sc_targets[link->target]->flags,
	    QLA_PORT_FLAG_IS_TARGET))
		rv = ENXIO;
	else {
		link->port_wwn = sc->sc_targets[link->target]->port_name;
		link->node_wwn = sc->sc_targets[link->target]->node_name;
	}
	mtx_leave(&sc->sc_port_mtx);

	return (rv);
}

void
qla_scsi_cmd(struct scsi_xfer *xs)
{
	struct scsi_link	*link = xs->sc_link;
	struct qla_softc	*sc = link->bus->sb_adapter_softc;
	struct qla_ccb		*ccb;
	struct qla_iocb_req34	*iocb;
	struct qla_ccb_list	list;
	u_int16_t		req, rspin;
	int			offset, error, done;
	bus_dmamap_t		dmap;

	if (xs->cmdlen > sizeof(iocb->req_cdb)) {
		DPRINTF(QLA_D_IO, "%s: cdb too big (%d)\n", DEVNAME(sc),
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
	if (sc->sc_marker_required) {
		req = sc->sc_next_req_id++;
		if (sc->sc_next_req_id == sc->sc_maxcmds)
			sc->sc_next_req_id = 0;

		DPRINTF(QLA_D_IO, "%s: writing marker at request %d\n",
		    DEVNAME(sc), req);
		offset = (req * QLA_QUEUE_ENTRY_SIZE);
		iocb = QLA_DMA_KVA(sc->sc_requests) + offset;
		bus_dmamap_sync(sc->sc_dmat, QLA_DMA_MAP(sc->sc_requests),
		    offset, QLA_QUEUE_ENTRY_SIZE, BUS_DMASYNC_POSTWRITE);
		qla_put_marker(sc, iocb);
		qla_queue_write(sc, sc->sc_regs->req_in, sc->sc_next_req_id);
		sc->sc_marker_required = 0;
	}

	req = sc->sc_next_req_id++;
	if (sc->sc_next_req_id == sc->sc_maxcmds)
		sc->sc_next_req_id = 0;

	offset = (req * QLA_QUEUE_ENTRY_SIZE);
	iocb = QLA_DMA_KVA(sc->sc_requests) + offset;
	bus_dmamap_sync(sc->sc_dmat, QLA_DMA_MAP(sc->sc_requests), offset,
	    QLA_QUEUE_ENTRY_SIZE, BUS_DMASYNC_POSTWRITE);

	ccb->ccb_xs = xs;

	DPRINTF(QLA_D_IO, "%s: writing cmd at request %d\n", DEVNAME(sc), req);
	qla_put_cmd(sc, iocb, xs, ccb);

	qla_queue_write(sc, sc->sc_regs->req_in, sc->sc_next_req_id);

	if (!ISSET(xs->flags, SCSI_POLL)) {
		mtx_leave(&sc->sc_queue_mtx);
		return;
	}

	done = 0;
	SIMPLEQ_INIT(&list);
	do {
		u_int16_t isr, info;

		delay(100);

		if (qla_read_isr(sc, &isr, &info) == 0) {
			continue;
		}

		if (isr != QLA_INT_TYPE_IO) {
			qla_handle_intr(sc, isr, info);
			continue;
		}

		rspin = qla_queue_read(sc, sc->sc_regs->res_in);
		while (rspin != sc->sc_last_resp_id) {
			ccb = qla_handle_resp(sc, sc->sc_last_resp_id);

			sc->sc_last_resp_id++;
			if (sc->sc_last_resp_id == sc->sc_maxcmds)
				sc->sc_last_resp_id = 0;

			if (ccb != NULL)
				SIMPLEQ_INSERT_TAIL(&list, ccb, ccb_link);
			if (ccb == xs->io)
				done = 1;
		}
		qla_queue_write(sc, sc->sc_regs->res_out, rspin);
		qla_clear_isr(sc, isr);
	} while (done == 0);

	mtx_leave(&sc->sc_queue_mtx);

	while ((ccb = SIMPLEQ_FIRST(&list)) != NULL) {
		SIMPLEQ_REMOVE_HEAD(&list, ccb_link);
		scsi_done(ccb->ccb_xs);
	}
}

u_int16_t
qla_read(struct qla_softc *sc, bus_size_t offset)
{
	u_int16_t v;
	v = bus_space_read_2(sc->sc_iot, sc->sc_ioh, offset);
	bus_space_barrier(sc->sc_iot, sc->sc_ioh, offset, 2,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	return (v);
}

void
qla_write(struct qla_softc *sc, bus_size_t offset, u_int16_t value)
{
	bus_space_write_2(sc->sc_iot, sc->sc_ioh, offset, value);
	bus_space_barrier(sc->sc_iot, sc->sc_ioh, offset, 2,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
}

u_int16_t
qla_read_mbox(struct qla_softc *sc, int mbox)
{
	/* could range-check mboxes according to chip type? */
	return (qla_read(sc, sc->sc_mbox_base + (mbox * 2)));
}

void
qla_write_mbox(struct qla_softc *sc, int mbox, u_int16_t value)
{
	qla_write(sc, sc->sc_mbox_base + (mbox * 2), value);
}

void
qla_host_cmd(struct qla_softc *sc, u_int16_t cmd)
{
	qla_write(sc, QLA_HOST_CMD_CTRL, cmd << QLA_HOST_CMD_SHIFT);
}

#define MBOX_COMMAND_TIMEOUT	4000

int
qla_mbox(struct qla_softc *sc, int maskin)
{
	int i;
	int result = 0;
	int rv;

	sc->sc_mbox_pending = 1;
	for (i = 0; i < nitems(sc->sc_mbox); i++) {
		if (maskin & (1 << i)) {
			qla_write_mbox(sc, i, sc->sc_mbox[i]);
		}
	}
	qla_host_cmd(sc, QLA_HOST_CMD_SET_HOST_INT);

	if (sc->sc_scsibus != NULL) {
		mtx_enter(&sc->sc_mbox_mtx);
		sc->sc_mbox_pending = 1;
		while (sc->sc_mbox_pending == 1) {
			msleep_nsec(sc->sc_mbox, &sc->sc_mbox_mtx, PRIBIO,
			    "qlambox", INFSLP);
		}
		result = sc->sc_mbox[0];
		sc->sc_mbox_pending = 0;
		mtx_leave(&sc->sc_mbox_mtx);
		return (result == QLA_MBOX_COMPLETE ? 0 : result);
	}

	for (i = 0; i < MBOX_COMMAND_TIMEOUT && result == 0; i++) {
		u_int16_t isr, info;

		delay(100);

		if (qla_read_isr(sc, &isr, &info) == 0)
			continue;

		switch (isr) {
		case QLA_INT_TYPE_MBOX:
			result = info;
			break;

		default:
			qla_handle_intr(sc, isr, info);
			break;
		}
	}

	if (result == 0) {
		/* timed out; do something? */
		DPRINTF(QLA_D_MBOX, "%s: mbox timed out\n", DEVNAME(sc));
		rv = 1;
	} else {
		for (i = 0; i < nitems(sc->sc_mbox); i++) {
			sc->sc_mbox[i] = qla_read_mbox(sc, i);
		}
		rv = (result == QLA_MBOX_COMPLETE ? 0 : result);
	}

	qla_clear_isr(sc, QLA_INT_TYPE_MBOX);
	sc->sc_mbox_pending = 0;
	return (rv);
}

void
qla_mbox_putaddr(u_int16_t *mbox, struct qla_dmamem *mem)
{
	mbox[2] = (QLA_DMA_DVA(mem) >> 16) & 0xffff;
	mbox[3] = (QLA_DMA_DVA(mem) >> 0) & 0xffff;
	mbox[6] = (QLA_DMA_DVA(mem) >> 48) & 0xffff;
	mbox[7] = (QLA_DMA_DVA(mem) >> 32) & 0xffff;
}

int
qla_sns_req(struct qla_softc *sc, struct qla_dmamem *mem, int reqsize)
{
	struct qla_sns_req_hdr *header;
	uint64_t dva;
	int rv;

	memset(&sc->sc_mbox, 0, sizeof(sc->sc_mbox));
	sc->sc_mbox[0] = QLA_MBOX_SEND_SNS;
	sc->sc_mbox[1] = reqsize / 2;
	qla_mbox_putaddr(sc->sc_mbox, mem);

	header = QLA_DMA_KVA(mem);
	htolem16(&header->resp_len, (QLA_DMA_LEN(mem) - reqsize) / 2);
	dva = QLA_DMA_DVA(mem) + reqsize;
	htolem32(&header->resp_addr_lo, dva);
	htolem32(&header->resp_addr_hi, dva >> 32);
	header->subcmd_len = htole16((reqsize - sizeof(*header)) / 2);

	bus_dmamap_sync(sc->sc_dmat, QLA_DMA_MAP(mem), 0, QLA_DMA_LEN(mem),
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	rv = qla_mbox(sc, 0x00cf);
	bus_dmamap_sync(sc->sc_dmat, QLA_DMA_MAP(mem), 0, QLA_DMA_LEN(mem),
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	return (rv);
}

void
qla_set_ints(struct qla_softc *sc, int enabled)
{
	u_int16_t v = enabled ? (QLA_INT_REQ | QLA_RISC_INT_REQ) : 0;
	qla_write(sc, QLA_INT_CTRL, v);
}

int
qla_read_isr_1G(struct qla_softc *sc, u_int16_t *isr, u_int16_t *info)
{
	u_int16_t int_status;

	if (qla_read(sc, QLA_SEMA) & QLA_SEMA_LOCK) {
		*info = qla_read_mbox(sc, 0);
		if (*info & QLA_MBOX_HAS_STATUS)
			*isr = QLA_INT_TYPE_MBOX;
		else
			*isr = QLA_INT_TYPE_ASYNC;
	} else {
		int_status = qla_read(sc, QLA_INT_STATUS);
		if ((int_status & QLA_INT_REQ) == 0)
			return (0);

		*isr = QLA_INT_TYPE_IO;
	}

	return (1);
}

int
qla_read_isr_2G(struct qla_softc *sc, u_int16_t *isr, u_int16_t *info)
{
	u_int32_t v;

	if ((qla_read(sc, QLA_INT_STATUS) & QLA_INT_REQ) == 0)
		return (0);

	v = bus_space_read_4(sc->sc_iot, sc->sc_ioh, QLA_RISC_STATUS_LOW);
	bus_space_barrier(sc->sc_iot, sc->sc_ioh, QLA_RISC_STATUS_LOW,
	    4, BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);

	switch (v & QLA_INT_STATUS_MASK) {
	case QLA_23XX_INT_ROM_MBOX:
	case QLA_23XX_INT_ROM_MBOX_FAIL:
	case QLA_23XX_INT_MBOX:
	case QLA_23XX_INT_MBOX_FAIL:
		*isr = QLA_INT_TYPE_MBOX;
		break;

	case QLA_23XX_INT_ASYNC:
		*isr = QLA_INT_TYPE_ASYNC;
		break;

	case QLA_23XX_INT_RSPQ:
		*isr = QLA_INT_TYPE_IO;
		break;

	default:
		*isr = QLA_INT_TYPE_OTHER;
		break;
	}

	*info = (v >> QLA_INT_INFO_SHIFT);

	return (1);
}

void
qla_clear_isr(struct qla_softc *sc, u_int16_t isr)
{
	qla_host_cmd(sc, QLA_HOST_CMD_CLR_RISC_INT);
	switch (isr) {
	case QLA_INT_TYPE_MBOX:
	case QLA_INT_TYPE_ASYNC:
		qla_write(sc, QLA_SEMA, 0);
		break;
	default:
		break;
	}
}

u_int16_t
qla_read_queue_2100(struct qla_softc *sc, bus_size_t queue)
{
	u_int16_t a, b, i;

	for (i = 0; i < 1000; i++) {
		a = qla_read(sc, queue);
		b = qla_read(sc, queue);

		if (a == b)
			return (a);
	}

	DPRINTF(QLA_D_INTR, "%s: queue ptr unstable\n", DEVNAME(sc));

	return (a);
}

int
qla_softreset(struct qla_softc *sc)
{
	int i;
	qla_set_ints(sc, 0);

	/* reset */
	qla_write(sc, QLA_CTRL_STATUS, QLA_CTRL_RESET);
	delay(100);
	/* clear data and control dma engines? */

	/* wait for soft reset to clear */
	for (i = 0; i < 1000; i++) {
		if ((qla_read(sc, QLA_CTRL_STATUS) & QLA_CTRL_RESET) == 0)
			break;

		delay(100);
	}

	if (i == 1000) {
		DPRINTF(QLA_D_INTR, "%s: reset didn't clear\n", DEVNAME(sc));
		qla_set_ints(sc, 0);
		return (ENXIO);
	}

	/* reset FPM */
	qla_write(sc, QLA_CTRL_STATUS, QLA_CTRL_FPM0_REGS);
	qla_write(sc, QLA_FPM_DIAG, QLA_FPM_RESET);
	qla_write(sc, QLA_FPM_DIAG, 0);	/* isp(4) doesn't do this? */
	qla_write(sc, QLA_CTRL_STATUS, QLA_CTRL_RISC_REGS);

	/* reset risc processor */
	qla_host_cmd(sc, QLA_HOST_CMD_RESET);
	delay(100);
	qla_write(sc, QLA_SEMA, 0);
	qla_host_cmd(sc, QLA_HOST_CMD_MASK_PARITY);	/* from isp(4) */
	qla_host_cmd(sc, QLA_HOST_CMD_RELEASE);

	/* reset queue pointers */
	qla_queue_write(sc, sc->sc_regs->req_in, 0);
	qla_queue_write(sc, sc->sc_regs->req_out, 0);
	qla_queue_write(sc, sc->sc_regs->res_in, 0);
	qla_queue_write(sc, sc->sc_regs->res_out, 0);

	qla_set_ints(sc, 1);
	/* isp(4) sends QLA_HOST_CMD_BIOS here.. not documented? */

	/* do a basic mailbox operation to check we're alive */
	sc->sc_mbox[0] = QLA_MBOX_NOP;
	if (qla_mbox(sc, 0x0001)) {
		DPRINTF(QLA_D_INTR, "%s: ISP not responding after reset\n",
		    DEVNAME(sc));
		return (ENXIO);
	}

	return (0);
}

void
qla_update_topology(struct qla_softc *sc)
{
	sc->sc_mbox[0] = QLA_MBOX_GET_LOOP_ID;
	if (qla_mbox(sc, 0x0001)) {
		DPRINTF(QLA_D_PORT, "%s: unable to get loop id\n", DEVNAME(sc));
		sc->sc_topology = QLA_TOPO_N_PORT_NO_TARGET;
	} else {
		sc->sc_topology = sc->sc_mbox[6];
		sc->sc_loop_id = sc->sc_mbox[1];

		switch (sc->sc_topology) {
		case QLA_TOPO_NL_PORT:
		case QLA_TOPO_N_PORT:
			DPRINTF(QLA_D_PORT, "%s: loop id %d\n", DEVNAME(sc),
			    sc->sc_loop_id);
			break;

		case QLA_TOPO_FL_PORT:
		case QLA_TOPO_F_PORT:
			sc->sc_port_id = sc->sc_mbox[2] |
			    (sc->sc_mbox[3] << 16);
			DPRINTF(QLA_D_PORT, "%s: fabric port id %06x\n",
			    DEVNAME(sc), sc->sc_port_id);
			break;

		case QLA_TOPO_N_PORT_NO_TARGET:
		default:
			DPRINTF(QLA_D_PORT, "%s: not connected\n", DEVNAME(sc));
			break;
		}

		switch (sc->sc_topology) {
		case QLA_TOPO_NL_PORT:
		case QLA_TOPO_FL_PORT:
			sc->sc_loop_max_id = 126;
			break;

		case QLA_TOPO_N_PORT:
			sc->sc_loop_max_id = 2;
			break;

		default:
			sc->sc_loop_max_id = 0;
			break;
		}
	}
}

int
qla_update_fabric(struct qla_softc *sc)
{
	struct qla_sns_rft_id *rft;

	if (sc->sc_fabric == 0)
		return (0);

	switch (sc->sc_topology) {
	case QLA_TOPO_F_PORT:
	case QLA_TOPO_FL_PORT:
		break;

	default:
		return (0);
	}

	/* get the name server's port db entry */
	sc->sc_mbox[0] = QLA_MBOX_GET_PORT_DB;
	if (sc->sc_2k_logins) {
		sc->sc_mbox[1] = QLA_F_PORT_HANDLE;
	} else {
		sc->sc_mbox[1] = QLA_F_PORT_HANDLE << 8;
	}
	qla_mbox_putaddr(sc->sc_mbox, sc->sc_scratch);
	bus_dmamap_sync(sc->sc_dmat, QLA_DMA_MAP(sc->sc_scratch), 0,
	    sizeof(struct qla_get_port_db), BUS_DMASYNC_PREREAD);
	if (qla_mbox(sc, 0x00cf)) {
		DPRINTF(QLA_D_PORT, "%s: get port db for SNS failed: %x\n",
		    DEVNAME(sc), sc->sc_mbox[0]);
		sc->sc_sns_port_name = 0;
	} else {
		struct qla_get_port_db *pdb;
		bus_dmamap_sync(sc->sc_dmat, QLA_DMA_MAP(sc->sc_scratch), 0,
		    sizeof(struct qla_get_port_db), BUS_DMASYNC_POSTREAD);
		pdb = QLA_DMA_KVA(sc->sc_scratch);
		DPRINTF(QLA_D_PORT, "%s: SNS port name %llx\n", DEVNAME(sc),
		    betoh64(pdb->port_name));
		sc->sc_sns_port_name = betoh64(pdb->port_name);
	}

	/*
	 * register fc4 types with the fabric
	 * some switches do this automatically, but apparently
	 * some don't.
	 */
	rft = QLA_DMA_KVA(sc->sc_scratch);
	memset(rft, 0, sizeof(*rft) + sizeof(struct qla_sns_req_hdr));
	rft->subcmd = htole16(QLA_SNS_RFT_ID);
	rft->max_word = htole16(sizeof(struct qla_sns_req_hdr) / 4);
	rft->port_id = htole32(sc->sc_port_id);
	rft->fc4_types[0] = htole32(1 << QLA_FC4_SCSI);
	if (qla_sns_req(sc, sc->sc_scratch, sizeof(*rft))) {
		DPRINTF(QLA_D_PORT, "%s: RFT_ID failed\n", DEVNAME(sc));
		/* we might be able to continue after this fails */
	}

	return (1);
}

int
qla_get_port_name_list(struct qla_softc *sc, u_int32_t match)
{
	int i;
	struct qla_port_name_list *l;
	struct qla_fc_port *port;

	sc->sc_mbox[0] = QLA_MBOX_GET_PORT_NAME_LIST;
	sc->sc_mbox[1] = 0x08;	/* include initiators */
	if (match & QLA_LOCATION_FABRIC)
		sc->sc_mbox[1] |= 0x02;	 /* return all loop ids */
	qla_mbox_putaddr(sc->sc_mbox, sc->sc_scratch);
	bus_dmamap_sync(sc->sc_dmat, QLA_DMA_MAP(sc->sc_scratch), 0,
	    QLA_DMA_LEN(sc->sc_scratch), BUS_DMASYNC_PREREAD);
	if (qla_mbox(sc, 0x04f)) {
		DPRINTF(QLA_D_PORT, "%s: get port name list failed: %x\n",
		    DEVNAME(sc), sc->sc_mbox[0]);
		return (1);
	}
	bus_dmamap_sync(sc->sc_dmat, QLA_DMA_MAP(sc->sc_scratch), 0,
	    QLA_DMA_LEN(sc->sc_scratch), BUS_DMASYNC_PREREAD);

	i = 0;
	l = QLA_DMA_KVA(sc->sc_scratch);
	mtx_enter(&sc->sc_port_mtx);
	while (i * sizeof(*l) < sc->sc_mbox[1]) {
		u_int16_t loopid;
		u_int32_t loc;

		loopid = letoh16(l[i].loop_id);
		/* skip special ports */
		switch (loopid) {
		case QLA_F_PORT_HANDLE:
		case QLA_SNS_HANDLE:
		case QLA_FABRIC_CTRL_HANDLE:
			loc = 0;
			break;
		default:
			if (loopid <= sc->sc_loop_max_id) {
				loc = QLA_LOCATION_LOOP_ID(loopid);
			} else {
				/*
				 * we don't have the port id here, so just
				 * indicate it's a fabric port.
				 */
				loc = QLA_LOCATION_FABRIC;
			}
		}

		if (match & loc) {
			port = malloc(sizeof(*port), M_DEVBUF, M_ZERO |
			    M_NOWAIT);
			if (port == NULL) {
				printf("%s: failed to allocate port struct\n",
				    DEVNAME(sc));
				break;
			}
			port->location = loc;
			port->loopid = loopid;
			port->port_name = letoh64(l[i].port_name);
			DPRINTF(QLA_D_PORT, "%s: loop id %d, port name %llx\n",
			    DEVNAME(sc), port->loopid, port->port_name);
			TAILQ_INSERT_TAIL(&sc->sc_ports_found, port, update);
		}
		i++;
	}
	mtx_leave(&sc->sc_port_mtx);

	return (0);
}

struct qla_fc_port *
qla_next_fabric_port(struct qla_softc *sc, u_int32_t *firstport,
    u_int32_t *lastport)
{
	struct qla_sns_ga_nxt *ga;
	struct qla_sns_ga_nxt_resp *gar;
	struct qla_fc_port *fport;
	int result;

	/* get the next port from the fabric nameserver */
	ga = QLA_DMA_KVA(sc->sc_scratch);
	memset(ga, 0, sizeof(*ga) + sizeof(*gar));
	ga->subcmd = htole16(QLA_SNS_GA_NXT);
	ga->max_word = htole16(sizeof(*gar) / 4);
	ga->port_id = htole32(*lastport);
	result = qla_sns_req(sc, sc->sc_scratch, sizeof(*ga));
	if (result) {
		DPRINTF(QLA_D_PORT, "%s: GA_NXT %06x failed: %x\n", DEVNAME(sc),
		    *lastport, result);
		*lastport = 0xffffffff;
		return (NULL);
	}

	gar = (struct qla_sns_ga_nxt_resp *)(ga + 1);
	/* if the response is all zeroes, try again */
	if (gar->port_type_id == 0 && gar->port_name == 0 &&
	    gar->node_name == 0) {
		DPRINTF(QLA_D_PORT, "%s: GA_NXT returned junk\n", DEVNAME(sc));
		return (NULL);
	}

	/* are we back at the start? */
	*lastport = betoh32(gar->port_type_id) & 0xffffff;
	if (*lastport == *firstport) {
		*lastport = 0xffffffff;
		return (NULL);
	}
	if (*firstport == 0xffffffff)
		*firstport = *lastport;

	DPRINTF(QLA_D_PORT, "%s: GA_NXT: port id: %06x, wwpn %llx, wwnn %llx\n",
	    DEVNAME(sc), *lastport, betoh64(gar->port_name),
	    betoh64(gar->node_name));

	/* don't try to log in to ourselves */
	if (*lastport == sc->sc_port_id) {
		return (NULL);
	}

	fport = malloc(sizeof(*fport), M_DEVBUF, M_ZERO | M_NOWAIT);
	if (fport == NULL) {
		printf("%s: failed to allocate a port struct\n",
		    DEVNAME(sc));
		*lastport = 0xffffffff;
		return (NULL);
	}
	fport->port_name = betoh64(gar->port_name);
	fport->node_name = betoh64(gar->node_name);
	fport->location = QLA_LOCATION_PORT_ID(*lastport);
	fport->portid = *lastport;
	return (fport);
}

int
qla_fabric_plogi(struct qla_softc *sc, struct qla_fc_port *port)
{
	int loopid, mboxin, err;
	u_int32_t id;

	loopid = 0;
retry:
	if (port->loopid == 0) {
		mtx_enter(&sc->sc_port_mtx);
		loopid = qla_get_loop_id(sc, loopid);
		mtx_leave(&sc->sc_port_mtx);
		if (loopid == -1) {
			DPRINTF(QLA_D_PORT, "%s: ran out of loop ids\n",
			    DEVNAME(sc));
			return (1);
		}
	}

	mboxin = 0x000f;
	sc->sc_mbox[0] = QLA_MBOX_FABRIC_PLOGI;
	sc->sc_mbox[2] = (port->portid >> 16) & 0xff;
	sc->sc_mbox[3] = port->portid & 0xffff;
	if (sc->sc_2k_logins) {
		sc->sc_mbox[1] = loopid;
		sc->sc_mbox[10] = 0;
		mboxin |= (1 << 10);
	} else {
		sc->sc_mbox[1] = loopid << 8;
	}

	err = qla_mbox(sc, mboxin);
	switch (err) {
	case 0:
		DPRINTF(QLA_D_PORT, "%s: logged in to %06x as %d\n",
		    DEVNAME(sc), port->portid, loopid);
		port->flags &= ~QLA_PORT_FLAG_NEEDS_LOGIN;
		port->loopid = loopid;
		return (0);

	case QLA_MBOX_PORT_USED:
		DPRINTF(QLA_D_PORT, "%s: already logged in to %06x as %d\n",
		    DEVNAME(sc), port->portid, sc->sc_mbox[1]);
		port->flags &= ~QLA_PORT_FLAG_NEEDS_LOGIN;
		port->loopid = sc->sc_mbox[1];
		return (0);

	case QLA_MBOX_LOOP_USED:
		id = (sc->sc_mbox[1] << 16) | sc->sc_mbox[2];
		if (qla_add_logged_in_port(sc, loopid, id)) {
			return (1);
		}
		port->loopid = 0;
		loopid++;
		goto retry;

	default:
		DPRINTF(QLA_D_PORT, "%s: error %x logging in to port %06x\n",
		    DEVNAME(sc), err, port->portid);
		port->loopid = 0;
		return (1);
	}
}

void
qla_fabric_plogo(struct qla_softc *sc, struct qla_fc_port *port)
{
	int mboxin = 0x0003;
	sc->sc_mbox[0] = QLA_MBOX_FABRIC_PLOGO;
	if (sc->sc_2k_logins) {
		sc->sc_mbox[1] = port->loopid;
		sc->sc_mbox[10] = 0;
		mboxin |= (1 << 10);
	} else {
		sc->sc_mbox[1] = port->loopid << 8;
	}

	if (qla_mbox(sc, mboxin))
		DPRINTF(QLA_D_PORT, "%s: loop id %d logout failed\n",
		    DEVNAME(sc), port->loopid);
}

void
qla_update_done(struct qla_softc *sc, int task)
{
	atomic_clearbits_int(&sc->sc_update_tasks, task);
}

void
qla_update_start(struct qla_softc *sc, int task)
{
	atomic_setbits_int(&sc->sc_update_tasks, task);
	task_add(sc->sc_update_taskq, &sc->sc_update_task);
}

void
qla_clear_port_lists(struct qla_softc *sc)
{
	struct qla_fc_port *p;

	while (!TAILQ_EMPTY(&sc->sc_ports_found)) {
		p = TAILQ_FIRST(&sc->sc_ports_found);
		TAILQ_REMOVE(&sc->sc_ports_found, p, update);
		free(p, M_DEVBUF, sizeof *p);
	}

	while (!TAILQ_EMPTY(&sc->sc_ports_new)) {
		p = TAILQ_FIRST(&sc->sc_ports_new);
		TAILQ_REMOVE(&sc->sc_ports_new, p, update);
		free(p, M_DEVBUF, sizeof *p);
	}

	while (!TAILQ_EMPTY(&sc->sc_ports_gone)) {
		p = TAILQ_FIRST(&sc->sc_ports_gone);
		TAILQ_REMOVE(&sc->sc_ports_gone, p, update);
	}
}

void
qla_do_update(void *xsc)
{
	struct qla_softc *sc = xsc;
	int firstport, lastport;
	struct qla_fc_port *port, *fport;

	DPRINTF(QLA_D_PORT, "%s: updating\n", DEVNAME(sc));
	while (sc->sc_update_tasks != 0) {
		if (sc->sc_update_tasks & QLA_UPDATE_TASK_CLEAR_ALL) {
			TAILQ_HEAD(, qla_fc_port) detach;
			DPRINTF(QLA_D_PORT, "%s: detaching everything\n",
			    DEVNAME(sc));

			mtx_enter(&sc->sc_port_mtx);
			qla_clear_port_lists(sc);
			TAILQ_INIT(&detach);
			TAILQ_CONCAT(&detach, &sc->sc_ports, ports);
			mtx_leave(&sc->sc_port_mtx);

			while (!TAILQ_EMPTY(&detach)) {
				port = TAILQ_FIRST(&detach);
				TAILQ_REMOVE(&detach, port, ports);
				if (port->flags & QLA_PORT_FLAG_IS_TARGET) {
					scsi_detach_target(sc->sc_scsibus,
					    port->loopid, DETACH_FORCE |
					    DETACH_QUIET);
				}
				sc->sc_targets[port->loopid] = NULL;
				if (port->location & QLA_LOCATION_FABRIC)
					qla_fabric_plogo(sc, port);

				free(port, M_DEVBUF, sizeof *port);
			}

			qla_update_done(sc, QLA_UPDATE_TASK_CLEAR_ALL);
			continue;
		}

		if (sc->sc_update_tasks & QLA_UPDATE_TASK_SOFTRESET) {
			/* what no */
			qla_update_done(sc, QLA_UPDATE_TASK_SOFTRESET);
			continue;
		}

		if (sc->sc_update_tasks & QLA_UPDATE_TASK_UPDATE_TOPO) {
			DPRINTF(QLA_D_PORT, "%s: updating topology\n",
			    DEVNAME(sc));
			qla_update_topology(sc);
			qla_update_done(sc, QLA_UPDATE_TASK_UPDATE_TOPO);
			continue;
		}

		if (sc->sc_update_tasks & QLA_UPDATE_TASK_GET_PORT_LIST) {
			DPRINTF(QLA_D_PORT, "%s: getting port name list\n",
			    DEVNAME(sc));
			mtx_enter(&sc->sc_port_mtx);
			qla_clear_port_lists(sc);
			mtx_leave(&sc->sc_port_mtx);

			qla_get_port_name_list(sc, QLA_LOCATION_LOOP |
			    QLA_LOCATION_FABRIC);
			mtx_enter(&sc->sc_port_mtx);
			TAILQ_FOREACH(port, &sc->sc_ports, ports) {
				TAILQ_INSERT_TAIL(&sc->sc_ports_gone, port,
				    update);
				if (port->location & QLA_LOCATION_FABRIC) {
					port->flags |=
					    QLA_PORT_FLAG_NEEDS_LOGIN;
				}
			}

			/* take care of ports that haven't changed first */
			TAILQ_FOREACH(fport, &sc->sc_ports_found, update) {
				port = sc->sc_targets[fport->loopid];
				if (port == NULL || fport->port_name !=
				    port->port_name) {
					/* new or changed port, handled later */
					continue;
				}

				/*
				 * the port hasn't been logged out, which
				 * means we don't need to log in again, and,
				 * for loop ports, that the port still exists.
				 */
				port->flags &= ~QLA_PORT_FLAG_NEEDS_LOGIN;
				if (port->location & QLA_LOCATION_LOOP)
					TAILQ_REMOVE(&sc->sc_ports_gone,
					    port, update);

				fport->location = 0;
			}
			mtx_leave(&sc->sc_port_mtx);
			qla_update_start(sc, QLA_UPDATE_TASK_PORT_LIST);
			qla_update_done(sc, QLA_UPDATE_TASK_GET_PORT_LIST);
			continue;
		}

		if (sc->sc_update_tasks & QLA_UPDATE_TASK_PORT_LIST) {
			mtx_enter(&sc->sc_port_mtx);
			fport = TAILQ_FIRST(&sc->sc_ports_found);
			if (fport != NULL) {
				TAILQ_REMOVE(&sc->sc_ports_found, fport,
				    update);
			}
			mtx_leave(&sc->sc_port_mtx);

			if (fport == NULL) {
				DPRINTF(QLA_D_PORT, "%s: done with ports\n",
				    DEVNAME(sc));
				qla_update_done(sc,
				    QLA_UPDATE_TASK_PORT_LIST);
				qla_update_start(sc,
				    QLA_UPDATE_TASK_SCAN_FABRIC);
			} else if (fport->location & QLA_LOCATION_LOOP) {
				DPRINTF(QLA_D_PORT, "%s: loop port %d\n",
				    DEVNAME(sc), fport->loopid);
				if (qla_add_loop_port(sc, fport) != 0)
					free(fport, M_DEVBUF, sizeof *fport);
			} else if (fport->location & QLA_LOCATION_FABRIC) {
				qla_add_fabric_port(sc, fport);
			} else {
				/* already processed */
				free(fport, M_DEVBUF, sizeof *fport);
			}
			continue;
		}

		if (sc->sc_update_tasks & QLA_UPDATE_TASK_SCAN_FABRIC) {
			DPRINTF(QLA_D_PORT, "%s: starting fabric scan\n",
			    DEVNAME(sc));
			lastport = sc->sc_port_id;
			firstport = 0xffffffff;
			if (qla_update_fabric(sc))
				qla_update_start(sc,
				    QLA_UPDATE_TASK_SCANNING_FABRIC);
			qla_update_done(sc, QLA_UPDATE_TASK_SCAN_FABRIC);
			continue;
		}

		if (sc->sc_update_tasks & QLA_UPDATE_TASK_SCANNING_FABRIC) {
			fport = qla_next_fabric_port(sc, &firstport, &lastport);
			if (fport != NULL) {
				int disp;

				mtx_enter(&sc->sc_port_mtx);
				disp = qla_classify_port(sc, fport->location,
				    fport->port_name, fport->node_name, &port);
				switch (disp) {
				case QLA_PORT_DISP_CHANGED:
				case QLA_PORT_DISP_MOVED:
					/* we'll log out the old port later */
				case QLA_PORT_DISP_NEW:
					DPRINTF(QLA_D_PORT, "%s: new port "
					    "%06x\n", DEVNAME(sc),
					    fport->portid);
					TAILQ_INSERT_TAIL(&sc->sc_ports_found,
					    fport, update);
					break;
				case QLA_PORT_DISP_DUP:
					free(fport, M_DEVBUF, sizeof *fport);
					break;
				case QLA_PORT_DISP_SAME:
					DPRINTF(QLA_D_PORT, "%s: existing port"
					    " %06x\n", DEVNAME(sc),
					    fport->portid);
					TAILQ_REMOVE(&sc->sc_ports_gone, port,
					    update);
					free(fport, M_DEVBUF, sizeof *fport);
					break;
				}
				mtx_leave(&sc->sc_port_mtx);
			}
			if (lastport == 0xffffffff) {
				DPRINTF(QLA_D_PORT, "%s: finished\n",
				    DEVNAME(sc));
				qla_update_done(sc,
				    QLA_UPDATE_TASK_SCANNING_FABRIC);
				qla_update_start(sc,
				    QLA_UPDATE_TASK_FABRIC_LOGIN);
			}
			continue;
		}

		if (sc->sc_update_tasks & QLA_UPDATE_TASK_FABRIC_LOGIN) {
			mtx_enter(&sc->sc_port_mtx);
			port = TAILQ_FIRST(&sc->sc_ports_found);
			if (port != NULL) {
				TAILQ_REMOVE(&sc->sc_ports_found, port, update);
			}
			mtx_leave(&sc->sc_port_mtx);

			if (port != NULL) {
				DPRINTF(QLA_D_PORT, "%s: found port %06x\n",
				    DEVNAME(sc), port->portid);
				if (qla_fabric_plogi(sc, port) == 0) {
					qla_add_fabric_port(sc, port);
				} else {
					free(port, M_DEVBUF, sizeof *port);
				}
			} else {
				DPRINTF(QLA_D_PORT, "%s: done with logins\n",
				    DEVNAME(sc));
				qla_update_done(sc,
				    QLA_UPDATE_TASK_FABRIC_LOGIN);
				qla_update_start(sc,
				    QLA_UPDATE_TASK_ATTACH_TARGET |
				    QLA_UPDATE_TASK_DETACH_TARGET);
			}
			continue;
		}

		if (sc->sc_update_tasks & QLA_UPDATE_TASK_FABRIC_RELOGIN) {
			TAILQ_FOREACH(port, &sc->sc_ports, ports) {
				if (port->flags & QLA_PORT_FLAG_NEEDS_LOGIN) {
					qla_fabric_plogi(sc, port);
					break;
				}
			}

			if (port == NULL)
				qla_update_done(sc,
				    QLA_UPDATE_TASK_FABRIC_RELOGIN);
			continue;
		}

		if (sc->sc_update_tasks & QLA_UPDATE_TASK_DETACH_TARGET) {
			mtx_enter(&sc->sc_port_mtx);
			port = TAILQ_FIRST(&sc->sc_ports_gone);
			if (port != NULL) {
				sc->sc_targets[port->loopid] = NULL;
				TAILQ_REMOVE(&sc->sc_ports_gone, port, update);
				TAILQ_REMOVE(&sc->sc_ports, port, ports);
			}
			mtx_leave(&sc->sc_port_mtx);

			if (port != NULL) {
				DPRINTF(QLA_D_PORT, "%s: detaching target %d\n",
				    DEVNAME(sc), port->loopid);
				if (sc->sc_scsibus != NULL)
					scsi_detach_target(sc->sc_scsibus,
					    port->loopid, DETACH_FORCE |
					    DETACH_QUIET);

				if (port->location & QLA_LOCATION_FABRIC)
					qla_fabric_plogo(sc, port);

				free(port, M_DEVBUF, sizeof *port);
			} else {
				qla_update_done(sc,
				    QLA_UPDATE_TASK_DETACH_TARGET);
			}
			continue;
		}

		if (sc->sc_update_tasks & QLA_UPDATE_TASK_ATTACH_TARGET) {
			mtx_enter(&sc->sc_port_mtx);
			port = TAILQ_FIRST(&sc->sc_ports_new);
			if (port != NULL) {
				TAILQ_REMOVE(&sc->sc_ports_new, port, update);
				TAILQ_INSERT_TAIL(&sc->sc_ports, port, ports);
			}
			mtx_leave(&sc->sc_port_mtx);

			if (port != NULL) {
				if (sc->sc_scsibus != NULL)
					scsi_probe_target(sc->sc_scsibus,
					    port->loopid);
			} else {
				qla_update_done(sc,
				    QLA_UPDATE_TASK_ATTACH_TARGET);
			}
			continue;
		}

	}

	DPRINTF(QLA_D_PORT, "%s: done updating\n", DEVNAME(sc));
}

int
qla_async(struct qla_softc *sc, u_int16_t info)
{
	u_int16_t id, exp;

	switch (info) {
	case QLA_ASYNC_SYSTEM_ERROR:
		qla_update_start(sc, QLA_UPDATE_TASK_SOFTRESET);
		break;

	case QLA_ASYNC_REQ_XFER_ERROR:
		qla_update_start(sc, QLA_UPDATE_TASK_SOFTRESET);
		break;

	case QLA_ASYNC_RSP_XFER_ERROR:
		qla_update_start(sc, QLA_UPDATE_TASK_SOFTRESET);
		break;

	case QLA_ASYNC_LIP_OCCURRED:
		DPRINTF(QLA_D_PORT, "%s: lip occurred\n", DEVNAME(sc));
		break;

	case QLA_ASYNC_LOOP_UP:
		DPRINTF(QLA_D_PORT, "%s: loop up\n", DEVNAME(sc));
		sc->sc_loop_up = 1;
		sc->sc_marker_required = 1;
		qla_update_start(sc, QLA_UPDATE_TASK_UPDATE_TOPO |
		    QLA_UPDATE_TASK_GET_PORT_LIST);
		break;

	case QLA_ASYNC_LOOP_DOWN:
		DPRINTF(QLA_D_PORT, "%s: loop down\n", DEVNAME(sc));
		sc->sc_loop_up = 0;
		qla_update_start(sc, QLA_UPDATE_TASK_CLEAR_ALL);
		break;

	case QLA_ASYNC_LIP_RESET:
		DPRINTF(QLA_D_PORT, "%s: lip reset\n", DEVNAME(sc));
		sc->sc_marker_required = 1;
		qla_update_start(sc, QLA_UPDATE_TASK_FABRIC_RELOGIN);
		break;

	case QLA_ASYNC_PORT_DB_CHANGE:
		DPRINTF(QLA_D_PORT, "%s: port db changed %x\n", DEVNAME(sc),
		    qla_read_mbox(sc, 1));
		qla_update_start(sc, QLA_UPDATE_TASK_GET_PORT_LIST);
		break;

	case QLA_ASYNC_CHANGE_NOTIFY:
		DPRINTF(QLA_D_PORT, "%s: name server change (%02x:%02x)\n",
		    DEVNAME(sc), qla_read_mbox(sc, 1), qla_read_mbox(sc, 2));
		qla_update_start(sc, QLA_UPDATE_TASK_GET_PORT_LIST);
		break;

	case QLA_ASYNC_LIP_F8:
		DPRINTF(QLA_D_PORT, "%s: lip f8\n", DEVNAME(sc));
		break;

	case QLA_ASYNC_LOOP_INIT_ERROR:
		DPRINTF(QLA_D_PORT, "%s: loop initialization error: %x\n",
		    DEVNAME(sc), qla_read_mbox(sc, 1));
		break;

	case QLA_ASYNC_LOGIN_REJECT:
		id = qla_read_mbox(sc, 1);
		exp = qla_read_mbox(sc, 2);
		DPRINTF(QLA_D_PORT, "%s: login reject from %x (reason %d,"
		    " explanation %x)\n", DEVNAME(sc), id >> 8, id & 0xff, exp);
		break;

	case QLA_ASYNC_SCSI_CMD_COMPLETE:
		/* shouldn't happen, we disable fast posting */
		break;

	case QLA_ASYNC_CTIO_COMPLETE:
		/* definitely shouldn't happen, we don't do target mode */
		break;

	case QLA_ASYNC_POINT_TO_POINT:
		DPRINTF(QLA_D_PORT, "%s: connected in point-to-point mode\n",
		    DEVNAME(sc));
		/* we get stuck handling these if we have the wrong loop
		 * topology; should somehow reinit with different things
		 * somehow.
		 */
		break;

	case QLA_ASYNC_ZIO_RESP_UPDATE:
		/* shouldn't happen, we don't do zio */
		break;

	case QLA_ASYNC_RND_ERROR:
		/* do nothing? */
		break;

	case QLA_ASYNC_QUEUE_FULL:
		break;

	default:
		DPRINTF(QLA_D_INTR, "%s: unknown async %x\n", DEVNAME(sc),
		    info);
		break;
	}
	return (1);
}

void
qla_dump_iocb(struct qla_softc *sc, void *buf)
{
#ifdef QLA_DEBUG
	u_int8_t *iocb = buf;
	int l;
	int b;

	if ((qladebug & QLA_D_IOCB) == 0)
		return;

	printf("%s: iocb:\n", DEVNAME(sc));
	for (l = 0; l < 4; l++) {
		for (b = 0; b < 16; b++) {
			printf(" %2.2x", iocb[(l*16)+b]);
		}
		printf("\n");
	}
#endif
}

void
qla_dump_iocb_segs(struct qla_softc *sc, void *segs, int n)
{
#ifdef QLA_DEBUG
	u_int8_t *buf = segs;
	int s, b;
	if ((qladebug & QLA_D_IOCB) == 0)
		return;

	printf("%s: iocb segs:\n", DEVNAME(sc));
	for (s = 0; s < n; s++) {
		for (b = 0; b < sizeof(struct qla_iocb_seg); b++) {
			printf(" %2.2x", buf[(s*(sizeof(struct qla_iocb_seg)))
			    + b]);
		}
		printf("\n");
	}
#endif
}

void
qla_put_marker(struct qla_softc *sc, void *buf)
{
	struct qla_iocb_marker *marker = buf;

	marker->entry_type = QLA_IOCB_MARKER;
	marker->entry_count = 1;
	marker->seqno = 0;
	marker->flags = 0;

	/* could be more specific here; isp(4) isn't */
	marker->target = 0;
	marker->modifier = QLA_IOCB_MARKER_SYNC_ALL;
	qla_dump_iocb(sc, buf);
}

static inline void
qla_put_data_seg(struct qla_iocb_seg *seg, bus_dmamap_t dmap, int num)
{
	uint64_t addr = dmap->dm_segs[num].ds_addr;

	htolem32(&seg->seg_addr_lo, addr);
	htolem32(&seg->seg_addr_hi, addr >> 32);
	htolem32(&seg->seg_len, dmap->dm_segs[num].ds_len);
}

void
qla_put_cmd(struct qla_softc *sc, void *buf, struct scsi_xfer *xs,
    struct qla_ccb *ccb)
{
	struct qla_iocb_req34 *req = buf;
	u_int16_t dir;
	int seg;
	int target = xs->sc_link->target;

	req->seqno = 0;
	req->flags = 0;
	req->entry_count = 1;

	if (xs->datalen == 0) {
		dir = QLA_IOCB_CMD_NO_DATA;
		req->req_seg_count = 0;
		req->entry_type = QLA_IOCB_CMD_TYPE_3;
	} else {
		dir = xs->flags & SCSI_DATA_IN ? QLA_IOCB_CMD_READ_DATA :
		    QLA_IOCB_CMD_WRITE_DATA;
		htolem16(&req->req_seg_count, ccb->ccb_dmamap->dm_nsegs);
		if (ccb->ccb_dmamap->dm_nsegs > QLA_IOCB_SEGS_PER_CMD) {
			req->entry_type = QLA_IOCB_CMD_TYPE_4;
			for (seg = 0; seg < ccb->ccb_dmamap->dm_nsegs; seg++) {
				qla_put_data_seg(&ccb->ccb_t4segs[seg],
				    ccb->ccb_dmamap, seg);
			}
			req->req_type.req4.req4_seg_type = htole16(1);
			req->req_type.req4.req4_seg_base = 0;
			req->req_type.req4.req4_seg_addr = ccb->ccb_seg_dva;
			memset(req->req_type.req4.req4_reserved, 0,
			    sizeof(req->req_type.req4.req4_reserved));
			bus_dmamap_sync(sc->sc_dmat,
			    QLA_DMA_MAP(sc->sc_segments), ccb->ccb_seg_offset,
			    sizeof(*ccb->ccb_t4segs) * ccb->ccb_dmamap->dm_nsegs,
			    BUS_DMASYNC_PREWRITE);
		} else {
			req->entry_type = QLA_IOCB_CMD_TYPE_3;
			for (seg = 0; seg < ccb->ccb_dmamap->dm_nsegs; seg++) {
				qla_put_data_seg(&req->req_type.req3_segs[seg],
				    ccb->ccb_dmamap, seg);
			}
		}
	}

	/* isp(4) uses head of queue for 'request sense' commands */
	htolem16(&req->req_flags, QLA_IOCB_CMD_SIMPLE_QUEUE | dir);

	/*
	 * timeout is in seconds.  make sure it's at least 1 if a timeout
	 * was specified in xs
	 */
	if (xs->timeout != 0)
		htolem16(&req->req_time, MAX(1, xs->timeout/1000));

	/* lun and target layout vary with firmware attributes */
	if (sc->sc_expanded_lun) {
		if (sc->sc_2k_logins) {
			req->req_target = htole16(target);
		} else {
			req->req_target = htole16(target << 8);
		}
		req->req_scclun = htole16(xs->sc_link->lun);
	} else {
		req->req_target = htole16(target << 8 | xs->sc_link->lun);
	}
	memcpy(req->req_cdb, &xs->cmd, xs->cmdlen);
	req->req_totalcnt = htole32(xs->datalen);

	req->req_handle = ccb->ccb_id;

	qla_dump_iocb(sc, buf);
}

int
qla_verify_firmware(struct qla_softc *sc, u_int16_t addr)
{
	sc->sc_mbox[0] = QLA_MBOX_VERIFY_CSUM;
	sc->sc_mbox[1] = addr;
	return (qla_mbox(sc, 0x0003));
}

#ifndef ISP_NOFIRMWARE
int
qla_load_firmware_words(struct qla_softc *sc, const u_int16_t *src,
    u_int16_t dest)
{
	u_int16_t i;

	for (i = 0; i < src[3]; i++) {
		sc->sc_mbox[0] = QLA_MBOX_WRITE_RAM_WORD;
		sc->sc_mbox[1] = i + dest;
		sc->sc_mbox[2] = src[i];
		if (qla_mbox(sc, 0x07)) {
			printf("firmware load failed\n");
			return (1);
		}
	}

	return (qla_verify_firmware(sc, dest));
}

int
qla_load_firmware_2100(struct qla_softc *sc)
{
	return qla_load_firmware_words(sc, isp_2100_risc_code,
	    QLA_2100_CODE_ORG);
}

int
qla_load_firmware_2200(struct qla_softc *sc)
{
	return qla_load_firmware_words(sc, isp_2200_risc_code,
	    QLA_2200_CODE_ORG);
}

int
qla_load_fwchunk_2300(struct qla_softc *sc, struct qla_dmamem *mem,
    const u_int16_t *src, u_int32_t dest)
{
	u_int16_t origin, done, total;
	int i;

	origin = dest;
	done = 0;
	total = src[3];

	while (done < total) {
		u_int16_t *copy;
		u_int32_t words;

		/* limit transfer size otherwise it just doesn't work */
		words = MIN(total - done, 1 << 10);
		copy = QLA_DMA_KVA(mem);
		for (i = 0; i < words; i++) {
			copy[i] = htole16(src[done++]);
		}
		bus_dmamap_sync(sc->sc_dmat, QLA_DMA_MAP(mem), 0, words * 2,
		    BUS_DMASYNC_PREWRITE);

		sc->sc_mbox[0] = QLA_MBOX_LOAD_RAM_EXT;
		sc->sc_mbox[1] = dest;
		sc->sc_mbox[4] = words;
		sc->sc_mbox[8] = dest >> 16;
		qla_mbox_putaddr(sc->sc_mbox, mem);
		if (qla_mbox(sc, 0x01ff)) {
			printf("firmware load failed\n");
			return (1);
		}
		bus_dmamap_sync(sc->sc_dmat, QLA_DMA_MAP(mem), 0, words * 2,
		    BUS_DMASYNC_POSTWRITE);

		dest += words;
	}

	return (qla_verify_firmware(sc, origin));
}

int
qla_load_firmware_2300(struct qla_softc *sc)
{
	struct qla_dmamem *mem;
	const u_int16_t *fw = isp_2300_risc_code;
	int rv;

	mem = qla_dmamem_alloc(sc, 65536);
	if (mem == NULL)
		return ENOMEM;
	rv = qla_load_fwchunk_2300(sc, mem, fw, QLA_2300_CODE_ORG);
	qla_dmamem_free(sc, mem);

	return (rv);
}

int
qla_load_firmware_2322(struct qla_softc *sc)
{
	/* we don't have the 2322 firmware image yet */
#if 0
	struct qla_dmamem *mem;
	const u_int16_t *fw = isp_2322_risc_code;
	u_int32_t addr;
	int i;

	mem = qla_dmamem_alloc(sc, 65536);
	if (qla_load_fwchunk_2300(sc, mem, fw, QLA_2300_CODE_ORG)) {
		qla_dmamem_free(sc, mem);
		return (1);
	}

	for (i = 0; i < 2; i++) {
		fw += fw[3];
		addr = fw[5] | ((fw[4] & 0x3f) << 16);
		if (qla_load_fwchunk_2300(sc, mem, fw, addr)) {
			qla_dmamem_free(sc, mem);
			return (1);
		}
	}

	qla_dmamem_free(sc, mem);
#endif
	return (0);
}

#endif	/* !ISP_NOFIRMWARE */

int
qla_read_nvram(struct qla_softc *sc)
{
	u_int16_t data[sizeof(sc->sc_nvram) >> 1];
	u_int16_t req, cmd, val;
	u_int8_t csum;
	int i, base, bit;

	base = sc->sc_port * 0x80;

	qla_write(sc, QLA_NVRAM, QLA_NVRAM_CHIP_SEL);
	delay(10);
	qla_write(sc, QLA_NVRAM, QLA_NVRAM_CHIP_SEL | QLA_NVRAM_CLOCK);
	delay(10);

	for (i = 0; i < nitems(data); i++) {
		req = (i + base) | (QLA_NVRAM_CMD_READ << 8);

		/* write each bit out through the nvram register */
		for (bit = 10; bit >= 0; bit--) {
			cmd = QLA_NVRAM_CHIP_SEL;
			if ((req >> bit) & 1) {
				cmd |= QLA_NVRAM_DATA_OUT;
			}
			qla_write(sc, QLA_NVRAM, cmd);
			delay(10);
			qla_read(sc, QLA_NVRAM);

			qla_write(sc, QLA_NVRAM, cmd | QLA_NVRAM_CLOCK);
			delay(10);
			qla_read(sc, QLA_NVRAM);

			qla_write(sc, QLA_NVRAM, cmd);
			delay(10);
			qla_read(sc, QLA_NVRAM);
		}

		/* read the result back */
		val = 0;
		for (bit = 0; bit < 16; bit++) {
			val <<= 1;
			qla_write(sc, QLA_NVRAM, QLA_NVRAM_CHIP_SEL |
			    QLA_NVRAM_CLOCK);
			delay(10);
			if (qla_read(sc, QLA_NVRAM) & QLA_NVRAM_DATA_IN)
				val |= 1;
			delay(10);

			qla_write(sc, QLA_NVRAM, QLA_NVRAM_CHIP_SEL);
			delay(10);
			qla_read(sc, QLA_NVRAM);
		}

		qla_write(sc, QLA_NVRAM, 0);
		delay(10);
		qla_read(sc, QLA_NVRAM);

		data[i] = letoh16(val);
	}

	csum = 0;
	for (i = 0; i < nitems(data); i++) {
		csum += data[i] & 0xff;
		csum += data[i] >> 8;
	}

	memcpy(&sc->sc_nvram, data, sizeof(sc->sc_nvram));
	/* id field should be 'ISP ', version should be at least 1 */
	if (sc->sc_nvram.id[0] != 'I' || sc->sc_nvram.id[1] != 'S' ||
	    sc->sc_nvram.id[2] != 'P' || sc->sc_nvram.id[3] != ' ' ||
	    sc->sc_nvram.nvram_version < 1 || (csum != 0)) {
		/*
		 * onboard 2200s on Sun hardware don't have an nvram
		 * fitted, but will provide us with node and port name
		 * through Open Firmware; don't complain in that case.
		 */
		if (sc->sc_node_name == 0 || sc->sc_port_name == 0)
			printf("%s: nvram corrupt\n", DEVNAME(sc));
		return (1);
	}
	return (0);
}

struct qla_dmamem *
qla_dmamem_alloc(struct qla_softc *sc, size_t size)
{
	struct qla_dmamem *m;
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
qla_dmamem_free(struct qla_softc *sc, struct qla_dmamem *m)
{
	bus_dmamap_unload(sc->sc_dmat, m->qdm_map);
	bus_dmamem_unmap(sc->sc_dmat, m->qdm_kva, m->qdm_size);
	bus_dmamem_free(sc->sc_dmat, &m->qdm_seg, 1);
	bus_dmamap_destroy(sc->sc_dmat, m->qdm_map);
	free(m, M_DEVBUF, sizeof(*m));
}

int
qla_alloc_ccbs(struct qla_softc *sc)
{
	struct qla_ccb		*ccb;
	u_int8_t		*cmd;
	int			i;

	SIMPLEQ_INIT(&sc->sc_ccb_free);
	mtx_init(&sc->sc_ccb_mtx, IPL_BIO);
	mtx_init(&sc->sc_queue_mtx, IPL_BIO);
	mtx_init(&sc->sc_port_mtx, IPL_BIO);
	mtx_init(&sc->sc_mbox_mtx, IPL_BIO);

	sc->sc_ccbs = mallocarray(sc->sc_maxcmds, sizeof(struct qla_ccb),
	    M_DEVBUF, M_WAITOK | M_CANFAIL | M_ZERO);
	if (sc->sc_ccbs == NULL) {
		printf("%s: unable to allocate ccbs\n", DEVNAME(sc));
		return (1);
	}

	sc->sc_requests = qla_dmamem_alloc(sc, sc->sc_maxcmds *
	    QLA_QUEUE_ENTRY_SIZE);
	if (sc->sc_requests == NULL) {
		printf("%s: unable to allocate ccb dmamem\n", DEVNAME(sc));
		goto free_ccbs;
	}
	sc->sc_responses = qla_dmamem_alloc(sc, sc->sc_maxcmds *
	    QLA_QUEUE_ENTRY_SIZE);
	if (sc->sc_responses == NULL) {
		printf("%s: unable to allocate rcb dmamem\n", DEVNAME(sc));
		goto free_req;
	}
	sc->sc_segments = qla_dmamem_alloc(sc, sc->sc_maxcmds * QLA_MAX_SEGS *
	    sizeof(struct qla_iocb_seg));
	if (sc->sc_segments == NULL) {
		printf("%s: unable to allocate iocb segments\n", DEVNAME(sc));
		goto free_res;
	}

	cmd = QLA_DMA_KVA(sc->sc_requests);
	memset(cmd, 0, QLA_QUEUE_ENTRY_SIZE * sc->sc_maxcmds);
	for (i = 0; i < sc->sc_maxcmds; i++) {
		ccb = &sc->sc_ccbs[i];

		if (bus_dmamap_create(sc->sc_dmat, MAXPHYS,
		    QLA_MAX_SEGS, MAXPHYS, 0,
		    BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW,
		    &ccb->ccb_dmamap) != 0) {
			printf("%s: unable to create dma map\n", DEVNAME(sc));
			goto free_maps;
		}

		ccb->ccb_sc = sc;
		ccb->ccb_id = i;

		ccb->ccb_seg_offset = i * QLA_MAX_SEGS *
		    sizeof(struct qla_iocb_seg);
		htolem64(&ccb->ccb_seg_dva,
		    QLA_DMA_DVA(sc->sc_segments) + ccb->ccb_seg_offset);
		ccb->ccb_t4segs = QLA_DMA_KVA(sc->sc_segments) +
		    ccb->ccb_seg_offset;

		qla_put_ccb(sc, ccb);
	}

	scsi_iopool_init(&sc->sc_iopool, sc, qla_get_ccb, qla_put_ccb);
	return (0);

free_maps:
	while ((ccb = qla_get_ccb(sc)) != NULL)
		bus_dmamap_destroy(sc->sc_dmat, ccb->ccb_dmamap);

	qla_dmamem_free(sc, sc->sc_segments);
free_res:
	qla_dmamem_free(sc, sc->sc_responses);
free_req:
	qla_dmamem_free(sc, sc->sc_requests);
free_ccbs:
	free(sc->sc_ccbs, M_DEVBUF, 0);

	return (1);
}

void
qla_free_ccbs(struct qla_softc *sc)
{
	struct qla_ccb		*ccb;

	scsi_iopool_destroy(&sc->sc_iopool);
	while ((ccb = qla_get_ccb(sc)) != NULL)
		bus_dmamap_destroy(sc->sc_dmat, ccb->ccb_dmamap);
	qla_dmamem_free(sc, sc->sc_segments);
	qla_dmamem_free(sc, sc->sc_responses);
	qla_dmamem_free(sc, sc->sc_requests);
	free(sc->sc_ccbs, M_DEVBUF, 0);
}

void *
qla_get_ccb(void *xsc)
{
	struct qla_softc	*sc = xsc;
	struct qla_ccb		*ccb;

	mtx_enter(&sc->sc_ccb_mtx);
	ccb = SIMPLEQ_FIRST(&sc->sc_ccb_free);
	if (ccb != NULL) {
		SIMPLEQ_REMOVE_HEAD(&sc->sc_ccb_free, ccb_link);
	}
	mtx_leave(&sc->sc_ccb_mtx);
	return (ccb);
}

void
qla_put_ccb(void *xsc, void *io)
{
	struct qla_softc	*sc = xsc;
	struct qla_ccb		*ccb = io;

	ccb->ccb_xs = NULL;
	mtx_enter(&sc->sc_ccb_mtx);
	SIMPLEQ_INSERT_HEAD(&sc->sc_ccb_free, ccb, ccb_link);
	mtx_leave(&sc->sc_ccb_mtx);
}
