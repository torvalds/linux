/* $OpenBSD: ihidev.c,v 1.40 2025/06/20 22:00:49 kettenis Exp $ */
/*
 * HID-over-i2c driver
 *
 * https://msdn.microsoft.com/en-us/library/windows/hardware/dn642101%28v=vs.85%29.aspx
 *
 * Copyright (c) 2015, 2016 joshua stein <jcs@openbsd.org>
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
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/stdint.h>

#include <dev/i2c/i2cvar.h>
#include <dev/i2c/ihidev.h>

#include <dev/hid/hid.h>

/* #define IHIDEV_DEBUG */

#ifdef IHIDEV_DEBUG
#define DPRINTF(x) printf x
#else
#define DPRINTF(x)
#endif

#define SLOW_POLL_MS	200
#define FAST_POLL_MS	10

/* 7.2 */
enum {
	I2C_HID_CMD_DESCR	= 0x0,
	I2C_HID_CMD_RESET	= 0x1,
	I2C_HID_CMD_GET_REPORT	= 0x2,
	I2C_HID_CMD_SET_REPORT	= 0x3,
	I2C_HID_CMD_GET_IDLE	= 0x4,
	I2C_HID_CMD_SET_IDLE	= 0x5,
	I2C_HID_CMD_GET_PROTO	= 0x6,
	I2C_HID_CMD_SET_PROTO	= 0x7,
	I2C_HID_CMD_SET_POWER	= 0x8,

	/* pseudo commands */
	I2C_HID_REPORT_DESCR	= 0x100,
	I2C_HID_RESET_RESPONSE	= 0x101,
};

static int I2C_HID_POWER_ON	= 0x0;
static int I2C_HID_POWER_OFF	= 0x1;

int	ihidev_match(struct device *, void *, void *);
void	ihidev_attach(struct device *, struct device *, void *);
int	ihidev_detach(struct device *, int);
int	ihidev_activate(struct device *, int);

int	ihidev_hid_command(struct ihidev_softc *, int, void *);
int	ihidev_intr(void *);
int	ihidev_poweron(struct ihidev_softc *);
int	ihidev_reset(struct ihidev_softc *);
int	ihidev_hid_desc_parse(struct ihidev_softc *);

int	ihidev_maxrepid(void *buf, int len);
int	ihidev_print(void *aux, const char *pnp);
int	ihidev_submatch(struct device *parent, void *cf, void *aux);

#define IHIDEV_QUIRK_RE_POWER_ON	0x1

const struct ihidev_quirks {
	uint16_t		ihq_vid;
	uint16_t		ihq_pid;
	int			ihq_quirks;
} ihidev_devs[] = {
	/* HONOR MagicBook Art 14 Touchpad (QTEC0002) */
	{ 0x35cc, 0x0104, IHIDEV_QUIRK_RE_POWER_ON },
};

const struct cfattach ihidev_ca = {
	sizeof(struct ihidev_softc),
	ihidev_match,
	ihidev_attach,
	ihidev_detach,
	ihidev_activate,
};

struct cfdriver ihidev_cd = {
	NULL, "ihidev", DV_DULL
};

int
ihidev_match(struct device *parent, void *match, void *aux)
{
	struct i2c_attach_args *ia = aux;

	if (strcmp(ia->ia_name, "ihidev") == 0)
		return (1);

	return (0);
}

int
ihidev_quirks(struct ihidev_softc *sc)
{
	const struct ihidev_quirks	*q;
	uint16_t			 vid, pid;
	int 				 i, nent;

	nent = nitems(ihidev_devs);

	vid = letoh16(sc->hid_desc.wVendorID);
	pid = letoh16(sc->hid_desc.wProductID);

	for (i = 0, q = ihidev_devs; i < nent; i++, q++)
		if (vid == q->ihq_vid && pid == q->ihq_pid)
			return (q->ihq_quirks);

	return (0);
}

void
ihidev_attach(struct device *parent, struct device *self, void *aux)
{
	struct ihidev_softc *sc = (struct ihidev_softc *)self;
	struct i2c_attach_args *ia = aux;
	struct ihidev_attach_arg iha;
	struct device *dev;
	int repid, repsz;
	int repsizes[256];

	sc->sc_tag = ia->ia_tag;
	sc->sc_addr = ia->ia_addr;
	sc->sc_hid_desc_addr = ia->ia_size;

	if (ihidev_hid_command(sc, I2C_HID_CMD_DESCR, NULL) ||
	    ihidev_hid_desc_parse(sc)) {
		printf(", failed fetching initial HID descriptor\n");
		return;
	}

	sc->sc_nrepid = ihidev_maxrepid(sc->sc_report, sc->sc_reportlen);
	if (sc->sc_nrepid < 0)
		return;

	sc->sc_nrepid++;
	sc->sc_subdevs = mallocarray(sc->sc_nrepid, sizeof(struct ihidev *),
	    M_DEVBUF, M_WAITOK | M_ZERO);

	/* find largest report size and allocate memory for input buffer */
	sc->sc_isize = letoh16(sc->hid_desc.wMaxInputLength);
	for (repid = 0; repid < sc->sc_nrepid; repid++) {
		repsz = hid_report_size(sc->sc_report, sc->sc_reportlen,
		    hid_input, repid);
		repsizes[repid] = repsz;
		if (repsz > sc->sc_isize)
			sc->sc_isize = repsz;
		if (repsz != 0)
			DPRINTF(("%s: repid %d size %d\n", sc->sc_dev.dv_xname,
			    repid, repsz));
	}
	sc->sc_ibuf = malloc(sc->sc_isize, M_DEVBUF, M_WAITOK | M_ZERO);

	if (ia->ia_intr) {
		printf(" %s", iic_intr_string(sc->sc_tag, ia->ia_intr));

		sc->sc_ih = iic_intr_establish(sc->sc_tag, ia->ia_intr,
		    IPL_TTY, ihidev_intr, sc, sc->sc_dev.dv_xname);
		if (sc->sc_ih == NULL)
			printf("%s: can't establish interrupt\n",
			    sc->sc_dev.dv_xname);
	}

	if (ia->ia_poll || !sc->sc_ih) {
		printf(" (polling)");
		sc->sc_poll = 1;
		sc->sc_fastpoll = 1;
	}

	printf(", vendor 0x%x product 0x%x, %s\n",
	    letoh16(sc->hid_desc.wVendorID), letoh16(sc->hid_desc.wProductID),
	    (char *)ia->ia_cookie);

	printf("%s: %d report id%s\n", sc->sc_dev.dv_xname, (sc->sc_nrepid - 1),
	    (sc->sc_nrepid - 1) > 1 ? "s" : "");

	iha.iaa = ia;
	iha.parent = sc;

	/* Look for a driver claiming multiple report IDs first. */
	iha.reportid = IHIDEV_CLAIM_MULTIPLEID;
	iha.nclaims = 0;
	dev = config_found_sm((struct device *)sc, &iha, NULL,
	    ihidev_submatch);
	if (dev != NULL) {
		for (repid = 0; repid < iha.nclaims; repid++) {
			sc->sc_subdevs[iha.claims[repid]] =
			    (struct ihidev *)dev;
		}
	}

	for (repid = 0; repid < sc->sc_nrepid; repid++) {
		if (sc->sc_subdevs[repid] != NULL)
			continue;

		if (hid_report_size(sc->sc_report, sc->sc_reportlen, hid_input,
		    repid) == 0 &&
		    hid_report_size(sc->sc_report, sc->sc_reportlen,
		    hid_output, repid) == 0 &&
		    hid_report_size(sc->sc_report, sc->sc_reportlen,
		    hid_feature, repid) == 0)
			continue;

		iha.reportid = repid;
		dev = config_found_sm(self, &iha, ihidev_print,
		    ihidev_submatch);
		sc->sc_subdevs[repid] = (struct ihidev *)dev;
	}

	if (sc->sc_refcnt > 0)
		return;
	
	/* power down until we're opened */
	if (ihidev_hid_command(sc, I2C_HID_CMD_SET_POWER, &I2C_HID_POWER_OFF)) {
		printf("%s: failed to power down\n", sc->sc_dev.dv_xname);
		return;
	}
}

int
ihidev_detach(struct device *self, int flags)
{
	struct ihidev_softc *sc = (struct ihidev_softc *)self;

	if (sc->sc_ih != NULL) {
		iic_intr_disestablish(sc->sc_tag, sc->sc_ih);
		sc->sc_ih = NULL;
	}

	if (sc->sc_ibuf != NULL) {
		free(sc->sc_ibuf, M_DEVBUF, sc->sc_isize);
		sc->sc_ibuf = NULL;
	}

	if (sc->sc_report != NULL)
		free(sc->sc_report, M_DEVBUF, sc->sc_reportlen);

	return (0);
}

int
ihidev_activate(struct device *self, int act)
{
	struct ihidev_softc *sc = (struct ihidev_softc *)self;
	int rv;

	DPRINTF(("%s(%d)\n", __func__, act));

	switch (act) {
	case DVACT_QUIESCE:
		rv = config_activate_children(self, act);
		sc->sc_dying = 1;
		if (sc->sc_poll && timeout_initialized(&sc->sc_timer)) {
			DPRINTF(("%s: cancelling polling\n",
			    sc->sc_dev.dv_xname));
			timeout_del_barrier(&sc->sc_timer);
		}
		if (ihidev_hid_command(sc, I2C_HID_CMD_SET_POWER,
		    &I2C_HID_POWER_OFF))
			printf("%s: failed to power down\n",
			    sc->sc_dev.dv_xname);
		break;
	case DVACT_WAKEUP:
		ihidev_poweron(sc);
		sc->sc_dying = 0;
		if (sc->sc_poll && timeout_initialized(&sc->sc_timer))
			timeout_add(&sc->sc_timer, 2000);
		rv = config_activate_children(self, act);
		break;
	default:
		rv = config_activate_children(self, act);
		break;
	}
	return rv;
}

void
ihidev_sleep(struct ihidev_softc *sc, int ms)
{
	if (cold)
		delay(ms * 1000);
	else
		tsleep_nsec(&sc, PWAIT, "ihidev", MSEC_TO_NSEC(ms));
}

int
ihidev_hid_command(struct ihidev_softc *sc, int hidcmd, void *arg)
{
	int i, res = 1;

	iic_acquire_bus(sc->sc_tag, 0);

	switch (hidcmd) {
	case I2C_HID_CMD_DESCR: {
		/*
		 * 5.2.2 - HID Descriptor Retrieval
		 * register is passed from the controller
		 */
		uint8_t cmd[] = {
			htole16(sc->sc_hid_desc_addr) & 0xff,
			htole16(sc->sc_hid_desc_addr) >> 8,
		};

		DPRINTF(("%s: HID command I2C_HID_CMD_DESCR at 0x%x\n",
		    sc->sc_dev.dv_xname, htole16(sc->sc_hid_desc_addr)));

		/* 20 00 */
		res = iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_addr,
		    &cmd, sizeof(cmd), &sc->hid_desc_buf,
		    sizeof(struct i2c_hid_desc), 0);

		DPRINTF(("%s: HID descriptor:", sc->sc_dev.dv_xname));
		for (i = 0; i < sizeof(struct i2c_hid_desc); i++)
			DPRINTF((" %.2x", sc->hid_desc_buf[i]));
		DPRINTF(("\n"));

		break;
	}
	case I2C_HID_CMD_RESET: {
		uint8_t cmd[] = {
			htole16(sc->hid_desc.wCommandRegister) & 0xff,
			htole16(sc->hid_desc.wCommandRegister) >> 8,
			0,
			I2C_HID_CMD_RESET,
		};

		DPRINTF(("%s: HID command I2C_HID_CMD_RESET\n",
		    sc->sc_dev.dv_xname));

		/* 22 00 00 01 */
		res = iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP, sc->sc_addr,
		    &cmd, sizeof(cmd), NULL, 0, 0);

		break;
	}
	case I2C_HID_CMD_GET_REPORT: {
		struct i2c_hid_report_request *rreq =
		    (struct i2c_hid_report_request *)arg;

		uint8_t cmd[] = {
			htole16(sc->hid_desc.wCommandRegister) & 0xff,
			htole16(sc->hid_desc.wCommandRegister) >> 8,
			0,
			I2C_HID_CMD_GET_REPORT,
			0, 0, 0,
		};
		int cmdlen = 7;
		int dataoff = 4;
		int report_id = rreq->id;
		int report_len = rreq->len + 2 + 1;
		int d;
		uint8_t *tmprep;

		DPRINTF(("%s: HID command I2C_HID_CMD_GET_REPORT %d "
		    "(type %d, len %d)\n", sc->sc_dev.dv_xname, report_id,
		    rreq->type, rreq->len));

		/*
		 * 7.2.2.4 - "The protocol is optimized for Report < 15.  If a
		 * report ID >= 15 is necessary, then the Report ID in the Low
		 * Byte must be set to 1111 and a Third Byte is appended to the
		 * protocol.  This Third Byte contains the entire/actual report
		 * ID."
		 */
		if (report_id >= 15) {
			cmd[dataoff++] = report_id;
			report_id = 15;
		} else
			cmdlen--;

		cmd[2] = report_id | rreq->type << 4;

		cmd[dataoff++] = sc->hid_desc.wDataRegister & 0xff;
		cmd[dataoff] = sc->hid_desc.wDataRegister >> 8;

		/*
		 * 7.2.2.2 - Response will be a 2-byte length value,
		 * the report id, and then the report.
		 * Allocate rreq->len + 2 + 1 bytes, read into that temporary
		 * buffer, and then copy only the report back out to
		 * rreq->data.
		 */
		tmprep = malloc(report_len, M_DEVBUF, M_WAITOK | M_ZERO);

		/* type 3 id 8: 22 00 38 02 23 00 */
		res = iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_addr,
		    &cmd, cmdlen, tmprep, report_len, 0);

		d = tmprep[0] | tmprep[1] << 8;
		if (d != report_len)
			DPRINTF(("%s: response size %d != expected length %d\n",
			    sc->sc_dev.dv_xname, d, report_len));

		d = tmprep[2];
		if (d != rreq->id) {
			DPRINTF(("%s: response report id %d != %d\n",
			    sc->sc_dev.dv_xname, d, rreq->id));
			iic_release_bus(sc->sc_tag, 0);
			free(tmprep, M_DEVBUF, report_len);
			return (1);
		}

		DPRINTF(("%s: response:", sc->sc_dev.dv_xname));
		for (i = 0; i < report_len; i++)
			DPRINTF((" %.2x", tmprep[i]));
		DPRINTF(("\n"));

		memcpy(rreq->data, tmprep + 2 + 1, rreq->len);
		free(tmprep, M_DEVBUF, report_len);

		break;
	}
	case I2C_HID_CMD_SET_REPORT: {
		struct i2c_hid_report_request *rreq =
		    (struct i2c_hid_report_request *)arg;

		uint8_t cmd[] = {
			htole16(sc->hid_desc.wCommandRegister) & 0xff,
			htole16(sc->hid_desc.wCommandRegister) >> 8,
			0,
			I2C_HID_CMD_SET_REPORT,
			0, 0, 0, 0, 0, 0,
		};
		int cmdlen = sizeof(cmd);
		int report_id = rreq->id;
		int report_len = 2 + (report_id ? 1 : 0) + rreq->len;
		int dataoff;
		uint8_t *finalcmd;

		DPRINTF(("%s: HID command I2C_HID_CMD_SET_REPORT %d "
		    "(type %d, len %d):", sc->sc_dev.dv_xname, report_id,
		    rreq->type, rreq->len));
		for (i = 0; i < rreq->len; i++)
			DPRINTF((" %.2x", ((uint8_t *)rreq->data)[i]));
		DPRINTF(("\n"));

		/*
		 * 7.2.3.4 - "The protocol is optimized for Report < 15.  If a
		 * report ID >= 15 is necessary, then the Report ID in the Low
		 * Byte must be set to 1111 and a Third Byte is appended to the
		 * protocol.  This Third Byte contains the entire/actual report
		 * ID."
		 */
		dataoff = 4;
		if (report_id >= 15) {
			cmd[dataoff++] = report_id;
			report_id = 15;
		} else
			cmdlen--;

		cmd[2] = report_id | rreq->type << 4;

		cmd[dataoff++] = htole16(sc->hid_desc.wDataRegister) & 0xff;
		cmd[dataoff++] = htole16(sc->hid_desc.wDataRegister) >> 8;

		cmd[dataoff++] = report_len & 0xff;
		cmd[dataoff++] = report_len >> 8;
		cmd[dataoff] = rreq->id;

		finalcmd = malloc(cmdlen + rreq->len, M_DEVBUF,
		    M_WAITOK | M_ZERO);

		memcpy(finalcmd, cmd, cmdlen);
		memcpy(finalcmd + cmdlen, rreq->data, rreq->len);

		/* type 3 id 4: 22 00 34 03 23 00 04 00 04 03 */
		res = iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP, sc->sc_addr,
		    finalcmd, cmdlen + rreq->len, NULL, 0, 0);

		free(finalcmd, M_DEVBUF, cmdlen + rreq->len);

 		break;
 	}

	case I2C_HID_CMD_SET_POWER: {
		int power = *(int *)arg;
		uint8_t cmd[] = {
			htole16(sc->hid_desc.wCommandRegister) & 0xff,
			htole16(sc->hid_desc.wCommandRegister) >> 8,
			power,
			I2C_HID_CMD_SET_POWER,
		};

		DPRINTF(("%s: HID command I2C_HID_CMD_SET_POWER(%d)\n",
		    sc->sc_dev.dv_xname, power));

		/* 22 00 00 08 */
		res = iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP, sc->sc_addr,
		    &cmd, sizeof(cmd), NULL, 0, 0);

		break;
	}
	case I2C_HID_REPORT_DESCR: {
		uint8_t cmd[] = {
			htole16(sc->hid_desc.wReportDescRegister) & 0xff,
			htole16(sc->hid_desc.wReportDescRegister) >> 8,
		};

		DPRINTF(("%s: HID command I2C_HID_REPORT_DESCR at 0x%x with "
		    "size %d\n", sc->sc_dev.dv_xname, cmd[0],
		    sc->sc_reportlen));

		/* 20 00 */
		res = iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_addr,
		    &cmd, sizeof(cmd), sc->sc_report, sc->sc_reportlen, 0);

		DPRINTF(("%s: HID report descriptor:", sc->sc_dev.dv_xname));
		for (i = 0; i < sc->sc_reportlen; i++)
			DPRINTF((" %.2x", sc->sc_report[i]));
		DPRINTF(("\n"));

		break;
	}
	case I2C_HID_RESET_RESPONSE: {
		int i;
		uint8_t buf[2] = { 0xff, 0xff };

		DPRINTF(("%s: HID command I2C_HID_RESET_RESPONSE\n",
		    sc->sc_dev.dv_xname));

		/*
		 * 7.2.1 states that a device should response for RESET
		 * in less than 5 seconds. It uses poll instead of
		 * tsleep because interrupts are blocked during autoconf.
		 */
		for (i = 0; i < 50; i++) {
			ihidev_sleep(sc, 100);
			res = iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
			    sc->sc_addr, NULL, 0, buf, sizeof(buf), 0);
			DPRINTF(("%s: read attempt %d: 0x%x, 0x%x, res: %d\n",
			    sc->sc_dev.dv_xname, i, buf[0], buf[1], res));
			if (!res)
				res = (buf[0] != 0x00 || buf[1] != 0x00);
			if (!res)
				break;
		}

		break;
	}
	default:
		printf("%s: unknown command %d\n", sc->sc_dev.dv_xname,
		    hidcmd);
	}

	iic_release_bus(sc->sc_tag, 0);

	return (res);
}

int
ihidev_poweron(struct ihidev_softc *sc)
{
	if (ihidev_hid_command(sc, I2C_HID_CMD_SET_POWER, &I2C_HID_POWER_ON)) {
		printf("%s: failed to power on\n", sc->sc_dev.dv_xname);
		return (1);
	}

	ihidev_sleep(sc, 100);

	return 0;
}


int
ihidev_reset(struct ihidev_softc *sc)
{
	DPRINTF(("%s: resetting\n", sc->sc_dev.dv_xname));

	if (ihidev_poweron(sc))
		return (1);

	if (ihidev_hid_command(sc, I2C_HID_CMD_RESET, 0)) {
		printf("%s: failed to reset hardware\n", sc->sc_dev.dv_xname);

		ihidev_hid_command(sc, I2C_HID_CMD_SET_POWER,
		    &I2C_HID_POWER_OFF);

		return (1);
	}

	if (ihidev_hid_command(sc, I2C_HID_RESET_RESPONSE, 0)) {
		printf("%s: unexpected reset response\n",
		    sc->sc_dev.dv_xname);
		return (1);
	}

	return (0);
}

/*
 * 5.2.2 - HID Descriptor Retrieval
 *
 * parse HID Descriptor that has already been read into hid_desc with
 * I2C_HID_CMD_DESCR
 */
int
ihidev_hid_desc_parse(struct ihidev_softc *sc)
{
	sc->sc_quirks = ihidev_quirks(sc);

	/* must be v01.00 */
	if (letoh16(sc->hid_desc.bcdVersion) != 0x0100) {
		printf("%s: bad HID descriptor bcdVersion (0x%x)\n",
		    sc->sc_dev.dv_xname,
		    letoh16(sc->hid_desc.bcdVersion));
		return (1);
	}

	/* must be 30 bytes for v1.00 */
	if (letoh16(sc->hid_desc.wHIDDescLength !=
	    sizeof(struct i2c_hid_desc))) {
		printf("%s: bad HID descriptor size (%d != %zu)\n",
		    sc->sc_dev.dv_xname,
		    letoh16(sc->hid_desc.wHIDDescLength),
		    sizeof(struct i2c_hid_desc));
		return (1);
	}

	if (letoh16(sc->hid_desc.wReportDescLength) <= 0) {
		printf("%s: bad HID report descriptor size (%d)\n",
		    sc->sc_dev.dv_xname,
		    letoh16(sc->hid_desc.wReportDescLength));
		return (1);
	}

	if (ihidev_reset(sc))
		return (1);

	sc->sc_reportlen = letoh16(sc->hid_desc.wReportDescLength);
	sc->sc_report = malloc(sc->sc_reportlen, M_DEVBUF, M_WAITOK | M_ZERO);

	if (ihidev_hid_command(sc, I2C_HID_REPORT_DESCR, 0)) {
		printf("%s: failed fetching HID report\n",
		    sc->sc_dev.dv_xname);
		return (1);
	}

	if (sc->sc_quirks & IHIDEV_QUIRK_RE_POWER_ON) {
		if (ihidev_poweron(sc))
			return (1);

		/*
		 * 7.2.8 states that a device shall not respond back
		 * after receiving the power on command, and must ensure
		 * that it transitions to power on state in less than 1
		 * second. The ihidev_poweron function uses a shorter
		 * sleep, sufficient for the ON-RESET sequence. Here,
		 * however, it sleeps for the full second to accommodate
		 * cold boot scenarios on affected devices.
		 */

		ihidev_sleep(sc, 1000);
	}

	return (0);
}

void
ihidev_poll(void *arg)
{
	struct ihidev_softc *sc = arg;

	sc->sc_frompoll = 1;
	ihidev_intr(sc);
	sc->sc_frompoll = 0;
}

int
ihidev_intr(void *arg)
{
	struct ihidev_softc *sc = arg;
	struct ihidev *scd;
	int psize, res, i, fast = 0;
	u_char *p;
	u_int rep = 0;

	if (sc->sc_poll && !sc->sc_frompoll) {
		DPRINTF(("%s: received interrupt while polling, disabling "
		    "polling\n", sc->sc_dev.dv_xname));
		sc->sc_poll = 0;
		timeout_del_barrier(&sc->sc_timer);
	}

	/*
	 * XXX: force I2C_F_POLL for now to avoid dwiic interrupting
	 * while we are interrupting
	 */

	iic_acquire_bus(sc->sc_tag, I2C_F_POLL);
	res = iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_addr, NULL, 0,
	    sc->sc_ibuf, letoh16(sc->hid_desc.wMaxInputLength), I2C_F_POLL);
	iic_release_bus(sc->sc_tag, I2C_F_POLL);

	/*
	 * 6.1.1 - First two bytes are the packet length, which must be less
	 * than or equal to wMaxInputLength
	 */
	psize = sc->sc_ibuf[0] | sc->sc_ibuf[1] << 8;
	if (psize <= 2 || psize > sc->sc_isize) {
		if (sc->sc_poll) {
			/*
			 * TODO: all fingers are up, should we pass to hid
			 * layer?
			 */
			sc->sc_fastpoll = 0;
			goto more_polling;
		} else
			DPRINTF(("%s: %s: invalid packet size (%d vs. %d)\n",
			    sc->sc_dev.dv_xname, __func__, psize,
			    sc->sc_isize));
		return (1);
	}

	/* 3rd byte is the report id */
	p = sc->sc_ibuf + 2;
	psize -= 2;
	if (sc->sc_nrepid != 1)
		rep = *p++, psize--;

	if (rep >= sc->sc_nrepid) {
		printf("%s: %s: bad report id %d\n", sc->sc_dev.dv_xname,
		    __func__, rep);
		if (sc->sc_poll) {
			sc->sc_fastpoll = 0;
			goto more_polling;
		}
		return (1);
	}

	DPRINTF(("%s: %s: hid input (rep %d):", sc->sc_dev.dv_xname, __func__,
	    rep));
	for (i = 0; i < psize; i++) {
		if (i > 0 && p[i] != 0 && p[i] != 0xff) {
			fast = 1;
		}
		DPRINTF((" %.2x", p[i]));
	}
	DPRINTF(("\n"));

	scd = sc->sc_subdevs[rep];
	if (scd == NULL || !(scd->sc_state & IHIDEV_OPEN)) {
		if (sc->sc_poll) {
			if (sc->sc_fastpoll) {
				DPRINTF(("%s: fast->slow polling\n",
				    sc->sc_dev.dv_xname));
				sc->sc_fastpoll = 0;
			}
			goto more_polling;
		}
		return (1);
	}

	if (!sc->sc_dying)
		scd->sc_intr(scd, p, psize);

	if (sc->sc_poll && (fast != sc->sc_fastpoll)) {
		DPRINTF(("%s: %s->%s polling\n", sc->sc_dev.dv_xname,
		    sc->sc_fastpoll ? "fast" : "slow",
		    fast ? "fast" : "slow"));
		sc->sc_fastpoll = fast;
	}

more_polling:
	if (sc->sc_poll && sc->sc_refcnt && !sc->sc_dying &&
	    !timeout_pending(&sc->sc_timer))
		timeout_add_msec(&sc->sc_timer,
		    sc->sc_fastpoll ? FAST_POLL_MS : SLOW_POLL_MS);

	return (1);
}

int
ihidev_maxrepid(void *buf, int len)
{
	struct hid_data *d;
	struct hid_item h;
	int maxid;

	maxid = -1;
	h.report_ID = 0;
	for (d = hid_start_parse(buf, len, hid_all); hid_get_item(d, &h);)
		if (h.report_ID > maxid)
			maxid = h.report_ID;
	hid_end_parse(d);

	return (maxid);
}

int
ihidev_print(void *aux, const char *pnp)
{
	struct ihidev_attach_arg *iha = aux;

	if (pnp)
		printf("hid at %s", pnp);

	if (iha->reportid != 0)
		printf(" reportid %d", iha->reportid);

	return (UNCONF);
}

int
ihidev_submatch(struct device *parent, void *match, void *aux)
{
	struct ihidev_attach_arg *iha = aux;
        struct cfdata *cf = match;

	if (cf->ihidevcf_reportid != IHIDEV_UNK_REPORTID &&
	    cf->ihidevcf_reportid != iha->reportid)
		return (0);

	return ((*cf->cf_attach->ca_match)(parent, cf, aux));
}

int
ihidev_open(struct ihidev *scd)
{
	struct ihidev_softc *sc = scd->sc_parent;

	DPRINTF(("%s: %s: state=%d refcnt=%d\n", sc->sc_dev.dv_xname,
	    __func__, scd->sc_state, sc->sc_refcnt));

	if (scd->sc_state & IHIDEV_OPEN)
		return (EBUSY);

	scd->sc_state |= IHIDEV_OPEN;

	if (sc->sc_refcnt++ || sc->sc_isize == 0)
		return (0);

	/* power on */
	ihidev_poweron(sc);

	if (sc->sc_poll) {
		if (!timeout_initialized(&sc->sc_timer))
			timeout_set(&sc->sc_timer, (void *)ihidev_poll, sc);
		if (!timeout_pending(&sc->sc_timer))
			timeout_add(&sc->sc_timer, FAST_POLL_MS);
	}

	return (0);
}

void
ihidev_close(struct ihidev *scd)
{
	struct ihidev_softc *sc = scd->sc_parent;

	DPRINTF(("%s: %s: state=%d refcnt=%d\n", sc->sc_dev.dv_xname,
	    __func__, scd->sc_state, sc->sc_refcnt));

	if (!(scd->sc_state & IHIDEV_OPEN))
		return;

	scd->sc_state &= ~IHIDEV_OPEN;

	if (--sc->sc_refcnt)
		return;

	/* no sub-devices open, conserve power */

	if (sc->sc_poll && timeout_pending(&sc->sc_timer))
		timeout_del(&sc->sc_timer);

	if (ihidev_hid_command(sc, I2C_HID_CMD_SET_POWER, &I2C_HID_POWER_OFF))
		printf("%s: failed to power down\n", sc->sc_dev.dv_xname);
}

int
ihidev_ioctl(struct ihidev *sc, u_long cmd, caddr_t addr, int flag,
    struct proc *p)
{
	return -1;
}

void
ihidev_get_report_desc(struct ihidev_softc *sc, void **desc, int *size)
{
	*desc = sc->sc_report;
	*size = sc->sc_reportlen;
}

int
ihidev_report_type_conv(int hid_type_id)
{
	switch (hid_type_id) {
	case hid_input:
		return I2C_HID_REPORT_TYPE_INPUT;
	case hid_output:
		return I2C_HID_REPORT_TYPE_OUTPUT;
	case hid_feature:
		return I2C_HID_REPORT_TYPE_FEATURE;
	default:
		return -1;
	}
}

int
ihidev_get_report(struct device *dev, int type, int id, void *data, int len)
{
	struct ihidev_softc *sc = (struct ihidev_softc *)dev;
	struct i2c_hid_report_request rreq;

	rreq.type = type;
	rreq.id = id;
	rreq.data = data;
	rreq.len = len;

	if (ihidev_hid_command(sc, I2C_HID_CMD_GET_REPORT, &rreq)) {
		printf("%s: failed fetching report\n", sc->sc_dev.dv_xname);
		return (1);
	}

	return 0;
}

int
ihidev_set_report(struct device *dev, int type, int id, void *data, int len)
{
	struct ihidev_softc *sc = (struct ihidev_softc *)dev;
	struct i2c_hid_report_request rreq;

	rreq.type = type;
	rreq.id = id;
	rreq.data = data;
	rreq.len = len;

	if (ihidev_hid_command(sc, I2C_HID_CMD_SET_REPORT, &rreq)) {
		printf("%s: failed setting report\n", sc->sc_dev.dv_xname);
		return (1);
	}

	return 0;
}

int
ihidev_send_report(struct device *dev, int repid, void *data, int data_len)
{
	struct ihidev_softc *sc = (struct ihidev_softc *)dev;
	uint8_t *finalcmd, cmd[5];
	int cmd_len, report_len, res;

	cmd_len = sizeof(cmd);
	report_len = 2 + 1 + data_len;

	cmd[0] = htole16(sc->hid_desc.wOutputRegister) & 0xff;
	cmd[1] = htole16(sc->hid_desc.wOutputRegister) >> 8;
	cmd[2] = report_len & 0xff;
	cmd[3] = report_len >> 8;
	cmd[4] = repid;
	
	finalcmd = malloc(cmd_len + data_len, M_DEVBUF, M_NOWAIT | M_ZERO);
	if (finalcmd == NULL)
		return ENOMEM;

	memcpy(finalcmd, cmd, cmd_len);
	memcpy(finalcmd + cmd_len, data, data_len);

	res = iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP, sc->sc_addr,
	    finalcmd, cmd_len + data_len, NULL, 0, 0);

	free(finalcmd, M_DEVBUF, cmd_len + data_len);

	return res;
}
