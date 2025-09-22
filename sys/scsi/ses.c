/*	$OpenBSD: ses.c,v 1.64 2021/10/24 16:57:30 mpi Exp $ */

/*
 * Copyright (c) 2005 David Gwynne <dlg@openbsd.org>
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
#include <sys/device.h>
#include <sys/scsiio.h>
#include <sys/malloc.h>
#include <sys/pool.h>
#include <sys/rwlock.h>
#include <sys/queue.h>
#include <sys/sensors.h>

#if NBIO > 0
#include <dev/biovar.h>
#endif /* NBIO > 0 */

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>

#include <scsi/ses.h>

#ifdef SES_DEBUG
#define DPRINTF(x...)		do { if (sesdebug) printf(x); } while (0)
#define DPRINTFN(n, x...)	do { if (sesdebug > (n)) printf(x); } while (0)
int	sesdebug = 2;
#else
#define DPRINTF(x...)		/* x */
#define DPRINTFN(n,x...)	/* n: x */
#endif /* SES_DEBUG */

int	ses_match(struct device *, void *, void *);
void	ses_attach(struct device *, struct device *, void *);
int	ses_detach(struct device *, int);

struct ses_sensor {
	struct ksensor		 se_sensor;
	u_int8_t		 se_type;
	struct ses_status	*se_stat;

	TAILQ_ENTRY(ses_sensor)	se_entry;
};

#if NBIO > 0
struct ses_slot {
	struct ses_status	*sl_stat;

	TAILQ_ENTRY(ses_slot)	 sl_entry;
};
#endif /* NBIO > 0 */

struct ses_softc {
	struct device		 sc_dev;
	struct scsi_link	*sc_link;
	struct rwlock		 sc_lock;

	enum {
		SES_ENC_STD,
		SES_ENC_DELL
	}			 sc_enctype;

	u_char			*sc_buf;
	ssize_t			 sc_buflen;

#if NBIO > 0
	TAILQ_HEAD(, ses_slot)	 sc_slots;
#endif /* NBIO > 0 */
	TAILQ_HEAD(, ses_sensor) sc_sensors;
	struct ksensordev	 sc_sensordev;
	struct sensor_task	*sc_sensortask;
};

const struct cfattach ses_ca = {
	sizeof(struct ses_softc), ses_match, ses_attach, ses_detach
};

struct cfdriver ses_cd = {
	NULL, "ses", DV_DULL
};

#define DEVNAME(s)	((s)->sc_dev.dv_xname)

#define SES_BUFLEN	2048 /* XXX Is this enough? */

int	ses_read_config(struct ses_softc *);
int	ses_read_status(struct ses_softc *);
int	ses_make_sensors(struct ses_softc *, struct ses_type_desc *, int);
void	ses_refresh_sensors(void *);

#if NBIO > 0
int	ses_ioctl(struct device *, u_long, caddr_t);
int	ses_write_config(struct ses_softc *);
int	ses_bio_blink(struct ses_softc *, struct bioc_blink *);
#endif /* NBIO > 0 */

void	ses_psu2sensor(struct ses_softc *, struct ses_sensor *);
void	ses_cool2sensor(struct ses_softc *, struct ses_sensor *);
void	ses_temp2sensor(struct ses_softc *, struct ses_sensor *);

#ifdef SES_DEBUG
void	 ses_dump_enc_desc(struct ses_enc_desc *);
char	*ses_dump_enc_string(u_char *, ssize_t);
#endif /* SES_DEBUG */

int
ses_match(struct device *parent, void *match, void *aux)
{
	struct scsi_attach_args		*sa = aux;
	struct scsi_inquiry_data	*inq = &sa->sa_sc_link->inqdata;

	if ((inq->device & SID_TYPE) == T_ENCLOSURE &&
	    SID_ANSII_REV(inq) >= SCSI_REV_2)
		return 2;

	/* Match on Dell enclosures. */
	if ((inq->device & SID_TYPE) == T_PROCESSOR &&
	    SID_ANSII_REV(inq) == SCSI_REV_SPC)
		return 3;

	return 0;
}

void
ses_attach(struct device *parent, struct device *self, void *aux)
{
	char				 vendor[33];
	struct ses_softc		*sc = (struct ses_softc *)self;
	struct scsi_attach_args		*sa = aux;
	struct ses_sensor		*sensor;
#if NBIO > 0
	struct ses_slot			*slot;
#endif /* NBIO > 0 */

	sc->sc_link = sa->sa_sc_link;
	sa->sa_sc_link->device_softc = sc;
	rw_init(&sc->sc_lock, DEVNAME(sc));

	scsi_strvis(vendor, sc->sc_link->inqdata.vendor,
	    sizeof(sc->sc_link->inqdata.vendor));
	if (strncasecmp(vendor, "Dell", sizeof(vendor)) == 0)
		sc->sc_enctype = SES_ENC_DELL;
	else
		sc->sc_enctype = SES_ENC_STD;

	printf("\n");

	if (ses_read_config(sc) != 0) {
		printf("%s: unable to read enclosure configuration\n",
		    DEVNAME(sc));
		return;
	}

	if (!TAILQ_EMPTY(&sc->sc_sensors)) {
		sc->sc_sensortask = sensor_task_register(sc,
		    ses_refresh_sensors, 10);
		if (sc->sc_sensortask == NULL) {
			printf("%s: unable to register update task\n",
			    DEVNAME(sc));
			while (!TAILQ_EMPTY(&sc->sc_sensors)) {
				sensor = TAILQ_FIRST(&sc->sc_sensors);
				TAILQ_REMOVE(&sc->sc_sensors, sensor,
				    se_entry);
				free(sensor, M_DEVBUF, sizeof(*sensor));
			}
		} else {
			TAILQ_FOREACH(sensor, &sc->sc_sensors, se_entry)
				sensor_attach(&sc->sc_sensordev,
				    &sensor->se_sensor);
			sensordev_install(&sc->sc_sensordev);
		}
	}

#if NBIO > 0
	if (!TAILQ_EMPTY(&sc->sc_slots) &&
	    bio_register(self, ses_ioctl) != 0) {
		printf("%s: unable to register ioctl\n", DEVNAME(sc));
		while (!TAILQ_EMPTY(&sc->sc_slots)) {
			slot = TAILQ_FIRST(&sc->sc_slots);
			TAILQ_REMOVE(&sc->sc_slots, slot, sl_entry);
			free(slot, M_DEVBUF, sizeof(*slot));
		}
	}
#endif /* NBIO > 0 */

	if (TAILQ_EMPTY(&sc->sc_sensors)
#if NBIO > 0
	    && TAILQ_EMPTY(&sc->sc_slots)
#endif /* NBIO > 0 */
	    ) {
		dma_free(sc->sc_buf, sc->sc_buflen);
		sc->sc_buf = NULL;
	}
}

int
ses_detach(struct device *self, int flags)
{
	struct ses_softc		*sc = (struct ses_softc *)self;
	struct ses_sensor		*sensor;
#if NBIO > 0
	struct ses_slot			*slot;
#endif /* NBIO > 0 */

	rw_enter_write(&sc->sc_lock);

#if NBIO > 0
	if (!TAILQ_EMPTY(&sc->sc_slots)) {
		bio_unregister(self);
		while (!TAILQ_EMPTY(&sc->sc_slots)) {
			slot = TAILQ_FIRST(&sc->sc_slots);
			TAILQ_REMOVE(&sc->sc_slots, slot, sl_entry);
			free(slot, M_DEVBUF, sizeof(*slot));
		}
	}
#endif /* NBIO > 0 */

	if (!TAILQ_EMPTY(&sc->sc_sensors)) {
		sensordev_deinstall(&sc->sc_sensordev);
		sensor_task_unregister(sc->sc_sensortask);

		while (!TAILQ_EMPTY(&sc->sc_sensors)) {
			sensor = TAILQ_FIRST(&sc->sc_sensors);
			sensor_detach(&sc->sc_sensordev, &sensor->se_sensor);
			TAILQ_REMOVE(&sc->sc_sensors, sensor, se_entry);
			free(sensor, M_DEVBUF, sizeof(*sensor));
		}
	}

	if (sc->sc_buf != NULL)
		dma_free(sc->sc_buf, sc->sc_buflen);

	rw_exit_write(&sc->sc_lock);

	return 0;
}

int
ses_read_config(struct ses_softc *sc)
{
	struct ses_scsi_diag		*cmd;
	struct ses_config_hdr		*cfg;
	struct ses_type_desc		*tdh, *tdlist;
#ifdef SES_DEBUG
	struct ses_enc_desc		*desc;
#endif /* SES_DEBUG */
	struct ses_enc_hdr		*enc;
	struct scsi_xfer		*xs;
	u_char				*buf, *p;
	int				 error = 0, i;
	int				 flags = 0, ntypes = 0, nelems = 0;

	buf = dma_alloc(SES_BUFLEN, PR_NOWAIT | PR_ZERO);
	if (buf == NULL)
		return 1;

	if (cold)
		SET(flags, SCSI_AUTOCONF);
	xs = scsi_xs_get(sc->sc_link, flags | SCSI_DATA_IN | SCSI_SILENT);
	if (xs == NULL) {
		error = 1;
		goto done;
	}
	xs->cmdlen = sizeof(*cmd);
	xs->data = buf;
	xs->datalen = SES_BUFLEN;
	xs->retries = 2;
	xs->timeout = 3000;

	cmd = (struct ses_scsi_diag *)&xs->cmd;
	cmd->opcode = RECEIVE_DIAGNOSTIC;
	SET(cmd->flags, SES_DIAG_PCV);
	cmd->pgcode = SES_PAGE_CONFIG;
	cmd->length = htobe16(SES_BUFLEN);

	error = scsi_xs_sync(xs);
	scsi_xs_put(xs);

	if (error) {
		error = 1;
		goto done;
	}

	cfg = (struct ses_config_hdr *)buf;
	if (cfg->pgcode != SES_PAGE_CONFIG || betoh16(cfg->length) >
	    SES_BUFLEN) {
		error = 1;
		goto done;
	}

	DPRINTF("%s: config: n_subenc: %d length: %d\n", DEVNAME(sc),
	    cfg->n_subenc, betoh16(cfg->length));

	p = buf + SES_CFG_HDRLEN;
	for (i = 0; i <= cfg->n_subenc; i++) {
		enc = (struct ses_enc_hdr *)p;
#ifdef SES_DEBUG
		DPRINTF("%s: enclosure %d enc_id: 0x%02x n_types: %d\n",
		    DEVNAME(sc), i, enc->enc_id, enc->n_types);
		desc = (struct ses_enc_desc *)(p + SES_ENC_HDRLEN);
		ses_dump_enc_desc(desc);
#endif /* SES_DEBUG */

		ntypes += enc->n_types;

		p += SES_ENC_HDRLEN + enc->vendor_len;
	}

	tdlist = (struct ses_type_desc *)p; /* Stash this for later. */

	for (i = 0; i < ntypes; i++) {
		tdh = (struct ses_type_desc *)p;
		DPRINTF("%s: td %d subenc_id: %d type 0x%02x n_elem: %d\n",
		    DEVNAME(sc), i, tdh->subenc_id, tdh->type, tdh->n_elem);

		nelems += tdh->n_elem;

		p += SES_TYPE_DESCLEN;
	}

#ifdef SES_DEBUG
	for (i = 0; i < ntypes; i++) {
		DPRINTF("%s: td %d '%s'\n", DEVNAME(sc), i,
		    ses_dump_enc_string(p, tdlist[i].desc_len));

		p += tdlist[i].desc_len;
	}
#endif /* SES_DEBUG */

	sc->sc_buflen = SES_STAT_LEN(ntypes, nelems);
	sc->sc_buf = dma_alloc(sc->sc_buflen, PR_NOWAIT);
	if (sc->sc_buf == NULL) {
		error = 1;
		goto done;
	}

	/* Get the status page and then use it to generate a list of sensors. */
	if (ses_make_sensors(sc, tdlist, ntypes) != 0) {
		dma_free(sc->sc_buf, sc->sc_buflen);
		error = 1;
		goto done;
	}

done:
	if (buf)
		dma_free(buf, SES_BUFLEN);
	return error;
}

int
ses_read_status(struct ses_softc *sc)
{
	struct ses_scsi_diag		*cmd;
	struct scsi_xfer		*xs;
	int				 error, flags = 0;

	if (cold)
		SET(flags, SCSI_AUTOCONF);
	xs = scsi_xs_get(sc->sc_link, flags | SCSI_DATA_IN | SCSI_SILENT);
	if (xs == NULL)
		return 1;
	xs->cmdlen = sizeof(*cmd);
	xs->data = sc->sc_buf;
	xs->datalen = sc->sc_buflen;
	xs->retries = 2;
	xs->timeout = 3000;

	cmd = (struct ses_scsi_diag *)&xs->cmd;
	cmd->opcode = RECEIVE_DIAGNOSTIC;
	SET(cmd->flags, SES_DIAG_PCV);
	cmd->pgcode = SES_PAGE_STATUS;
	cmd->length = htobe16(sc->sc_buflen);

	error = scsi_xs_sync(xs);
	scsi_xs_put(xs);

	if (error != 0)
		return 1;

	return 0;
}

int
ses_make_sensors(struct ses_softc *sc, struct ses_type_desc *types, int ntypes)
{
	struct ses_status		*status;
	struct ses_sensor		*sensor;
	char				*fmt;
#if NBIO > 0
	struct ses_slot			*slot;
#endif /* NBIO > 0 */
	enum sensor_type		 stype;
	int				 i, j;

	if (ses_read_status(sc) != 0)
		return 1;

	strlcpy(sc->sc_sensordev.xname, DEVNAME(sc),
	    sizeof(sc->sc_sensordev.xname));

	TAILQ_INIT(&sc->sc_sensors);
#if NBIO > 0
	TAILQ_INIT(&sc->sc_slots);
#endif /* NBIO > 0 */

	status = (struct ses_status *)(sc->sc_buf + SES_STAT_HDRLEN);
	for (i = 0; i < ntypes; i++) {
		/* Ignore the overall status element for this type. */
		DPRINTFN(1, "%s: %3d:-   0x%02x 0x%02x%02x%02x type: 0x%02x\n",
		     DEVNAME(sc), i, status->com, status->f1, status->f2,
		    status->f3, types[i].type);

		for (j = 0; j < types[i].n_elem; j++) {
			/* Move to the current status element. */
			status++;

			DPRINTFN(1, "%s: %3d:%-3d 0x%02x 0x%02x%02x%02x\n",
			    DEVNAME(sc), i, j, status->com, status->f1,
			    status->f2, status->f3);

			if (SES_STAT_CODE(status->com) == SES_STAT_CODE_NOTINST)
				continue;

			switch (types[i].type) {
#if NBIO > 0
			case SES_T_DEVICE:
				slot = malloc(sizeof(*slot), M_DEVBUF,
				    M_NOWAIT | M_ZERO);
				if (slot == NULL)
					goto error;

				slot->sl_stat = status;

				TAILQ_INSERT_TAIL(&sc->sc_slots, slot,
				    sl_entry);

				continue;
#endif /* NBIO > 0 */

			case SES_T_POWERSUPPLY:
				stype = SENSOR_INDICATOR;
				fmt = "PSU";
				break;

			case SES_T_COOLING:
				stype = SENSOR_PERCENT;
				fmt = "Fan";
				break;

			case SES_T_TEMP:
				stype = SENSOR_TEMP;
				fmt = "";
				break;

			default:
				continue;
			}

			sensor = malloc(sizeof(*sensor), M_DEVBUF,
			    M_NOWAIT | M_ZERO);
			if (sensor == NULL)
				goto error;

			sensor->se_type = types[i].type;
			sensor->se_stat = status;
			sensor->se_sensor.type = stype;
			strlcpy(sensor->se_sensor.desc, fmt,
			    sizeof(sensor->se_sensor.desc));

			TAILQ_INSERT_TAIL(&sc->sc_sensors, sensor, se_entry);
		}

		/* Move to the overall status element of the next type. */
		status++;
	}

	return 0;
error:
#if NBIO > 0
	while (!TAILQ_EMPTY(&sc->sc_slots)) {
		slot = TAILQ_FIRST(&sc->sc_slots);
		TAILQ_REMOVE(&sc->sc_slots, slot, sl_entry);
		free(slot, M_DEVBUF, sizeof(*slot));
	}
#endif /* NBIO > 0 */
	while (!TAILQ_EMPTY(&sc->sc_sensors)) {
		sensor = TAILQ_FIRST(&sc->sc_sensors);
		TAILQ_REMOVE(&sc->sc_sensors, sensor, se_entry);
		free(sensor, M_DEVBUF, sizeof(*sensor));
	}
	return 1;
}

void
ses_refresh_sensors(void *arg)
{
	struct ses_softc		*sc = (struct ses_softc *)arg;
	struct ses_sensor		*sensor;
	int				 ret = 0;

	rw_enter_write(&sc->sc_lock);

	if (ses_read_status(sc) != 0) {
		rw_exit_write(&sc->sc_lock);
		return;
	}

	TAILQ_FOREACH(sensor, &sc->sc_sensors, se_entry) {
		DPRINTFN(10, "%s: %s 0x%02x 0x%02x%02x%02x\n", DEVNAME(sc),
		    sensor->se_sensor.desc, sensor->se_stat->com,
		    sensor->se_stat->f1, sensor->se_stat->f2,
		    sensor->se_stat->f3);

		switch (SES_STAT_CODE(sensor->se_stat->com)) {
		case SES_STAT_CODE_OK:
			sensor->se_sensor.status = SENSOR_S_OK;
			break;

		case SES_STAT_CODE_CRIT:
		case SES_STAT_CODE_UNREC:
			sensor->se_sensor.status = SENSOR_S_CRIT;
			break;

		case SES_STAT_CODE_NONCRIT:
			sensor->se_sensor.status = SENSOR_S_WARN;
			break;

		case SES_STAT_CODE_NOTINST:
		case SES_STAT_CODE_UNKNOWN:
		case SES_STAT_CODE_NOTAVAIL:
			sensor->se_sensor.status = SENSOR_S_UNKNOWN;
			break;
		}

		switch (sensor->se_type) {
		case SES_T_POWERSUPPLY:
			ses_psu2sensor(sc, sensor);
			break;

		case SES_T_COOLING:
			ses_cool2sensor(sc, sensor);
			break;

		case SES_T_TEMP:
			ses_temp2sensor(sc, sensor);
			break;

		default:
			ret = 1;
			break;
		}
	}

	rw_exit_write(&sc->sc_lock);

	if (ret)
		printf("%s: error in sensor data\n", DEVNAME(sc));
}

#if NBIO > 0
int
ses_ioctl(struct device *dev, u_long cmd, caddr_t addr)
{
	struct ses_softc		*sc = (struct ses_softc *)dev;
	int				 error = 0;

	switch (cmd) {
	case BIOCBLINK:
		error = ses_bio_blink(sc, (struct bioc_blink *)addr);
		break;

	default:
		error = EINVAL;
		break;
	}

	return error;
}

int
ses_write_config(struct ses_softc *sc)
{
	struct ses_scsi_diag		*cmd;
	struct scsi_xfer		*xs;
	int				 error, flags = 0;

	if (cold)
		SET(flags, SCSI_AUTOCONF);

	xs = scsi_xs_get(sc->sc_link, flags | SCSI_DATA_OUT | SCSI_SILENT);
	if (xs == NULL)
		return 1;
	xs->cmdlen = sizeof(*cmd);
	xs->data = sc->sc_buf;
	xs->datalen = sc->sc_buflen;
	xs->retries = 2;
	xs->timeout = 3000;

	cmd = (struct ses_scsi_diag *)&xs->cmd;
	cmd->opcode = SEND_DIAGNOSTIC;
	SET(cmd->flags, SES_DIAG_PF);
	cmd->length = htobe16(sc->sc_buflen);

	error = scsi_xs_sync(xs);
	scsi_xs_put(xs);

	if (error != 0)
		return 1;

	return 0;
}

int
ses_bio_blink(struct ses_softc *sc, struct bioc_blink *blink)
{
	struct ses_slot			*slot;

	rw_enter_write(&sc->sc_lock);

	if (ses_read_status(sc) != 0) {
		rw_exit_write(&sc->sc_lock);
		return EIO;
	}

	TAILQ_FOREACH(slot, &sc->sc_slots, sl_entry) {
		if (slot->sl_stat->f1 == blink->bb_target)
			break;
	}

	if (slot == NULL) {
		rw_exit_write(&sc->sc_lock);
		return EINVAL;
	}

	DPRINTFN(3, "%s: 0x%02x 0x%02x 0x%02x 0x%02x\n", DEVNAME(sc),
	    slot->sl_stat->com, slot->sl_stat->f1, slot->sl_stat->f2,
	    slot->sl_stat->f3);

	slot->sl_stat->com = SES_STAT_SELECT;
	slot->sl_stat->f2 &= SES_C_DEV_F2MASK;
	slot->sl_stat->f3 &= SES_C_DEV_F3MASK;

	switch (blink->bb_status) {
	case BIOC_SBUNBLINK:
		slot->sl_stat->f2 &= ~SES_C_DEV_IDENT;
		break;

	case BIOC_SBBLINK:
		SET(slot->sl_stat->f2, SES_C_DEV_IDENT);
		break;

	default:
		rw_exit_write(&sc->sc_lock);
		return EINVAL;
	}

	DPRINTFN(3, "%s: 0x%02x 0x%02x 0x%02x 0x%02x\n", DEVNAME(sc),
	    slot->sl_stat->com, slot->sl_stat->f1, slot->sl_stat->f2,
	    slot->sl_stat->f3);

	if (ses_write_config(sc) != 0) {
		rw_exit_write(&sc->sc_lock);
		return EIO;
	}

	rw_exit_write(&sc->sc_lock);

	return 0;
}
#endif /* NBIO > 0 */

void
ses_psu2sensor(struct ses_softc *sc, struct ses_sensor *s)
{
	s->se_sensor.value = SES_S_PSU_OFF(s->se_stat) ? 0 : 1;
}

void
ses_cool2sensor(struct ses_softc *sc, struct ses_sensor *s)
{
	switch (sc->sc_enctype) {
	case SES_ENC_STD:
		switch (SES_S_COOL_CODE(s->se_stat)) {
		case SES_S_COOL_C_STOPPED:
			s->se_sensor.value = 0;
			break;
		case SES_S_COOL_C_LOW1:
		case SES_S_COOL_C_LOW2:
		case SES_S_COOL_C_LOW3:
			s->se_sensor.value = 33333;
			break;
		case SES_S_COOL_C_INTER:
		case SES_S_COOL_C_HI3:
		case SES_S_COOL_C_HI2:
			s->se_sensor.value = 66666;
			break;
		case SES_S_COOL_C_HI1:
			s->se_sensor.value = 100000;
			break;
		}
		break;

	/* Dell only use the first three codes to represent speed */
	case SES_ENC_DELL:
		switch (SES_S_COOL_CODE(s->se_stat)) {
		case SES_S_COOL_C_STOPPED:
			s->se_sensor.value = 0;
			break;
		case SES_S_COOL_C_LOW1:
			s->se_sensor.value = 33333;
			break;
		case SES_S_COOL_C_LOW2:
			s->se_sensor.value = 66666;
			break;
		case SES_S_COOL_C_LOW3:
		case SES_S_COOL_C_INTER:
		case SES_S_COOL_C_HI3:
		case SES_S_COOL_C_HI2:
		case SES_S_COOL_C_HI1:
			s->se_sensor.value = 100000;
			break;
		}
		break;
	}
}

void
ses_temp2sensor(struct ses_softc *sc, struct ses_sensor *s)
{
	s->se_sensor.value = (int64_t)SES_S_TEMP(s->se_stat);
	s->se_sensor.value += SES_S_TEMP_OFFSET;
	s->se_sensor.value *= 1000000;		/* Convert to micro degrees. */
	s->se_sensor.value += 273150000;	/* Convert to kelvin. */
}

#ifdef SES_DEBUG
void
ses_dump_enc_desc(struct ses_enc_desc *desc)
{
	char				str[32];

#if 0
	/* XXX not a string. wwn? */
	memset(str, 0, sizeof(str));
	memcpy(str, desc->logical_id, sizeof(desc->logical_id));
	DPRINTF("logical_id: %s", str);
#endif /* 0 */

	memset(str, 0, sizeof(str));
	memcpy(str, desc->vendor_id, sizeof(desc->vendor_id));
	DPRINTF(" vendor_id: %s", str);

	memset(str, 0, sizeof(str));
	memcpy(str, desc->prod_id, sizeof(desc->prod_id));
	DPRINTF(" prod_id: %s", str);

	memset(str, 0, sizeof(str));
	memcpy(str, desc->prod_rev, sizeof(desc->prod_rev));
	DPRINTF(" prod_rev: %s\n", str);
}

char *
ses_dump_enc_string(u_char *buf, ssize_t len)
{
	static char			str[256];

	memset(str, 0, sizeof(str));
	if (len > 0)
		memcpy(str, buf, len);

	return str;
}
#endif /* SES_DEBUG */
