/* $OpenBSD: ietp.c,v 1.3 2024/08/18 03:25:04 deraadt Exp $ */
/*
 * Elan I2C Touchpad driver
 *
 * Copyright (c) 2015, 2016 joshua stein <jcs@openbsd.org>
 * Copyright (c) 2020, 2022 Vladimir Kondratyev <wulf@FreeBSD.org>
 * Copyright (c) 2023 vladimir serbinenko <phcoder@gmail.com>
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

/* Protocol documentation:
 * https://lkml.indiana.edu/hypermail/linux/kernel/1205.0/02551.html
 * Based on FreeBSD ietp driver.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/stdint.h>

#include <dev/i2c/i2cvar.h>
#include <dev/i2c/ietp.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsmousevar.h>

/* #define IETP_DEBUG */

#ifdef IETP_DEBUG
#define DPRINTF(x) printf x
#else
#define DPRINTF(x)
#endif

enum {
	I2C_HID_CMD_DESCR	= 0x0,
	I2C_HID_CMD_RESET	= 0x1,
	I2C_HID_CMD_SET_POWER	= 0x8,
};

#define I2C_HID_POWER_ON	0x0
#define I2C_HID_POWER_OFF	0x1

#define IETP_PATTERN            0x0100
#define	IETP_UNIQUEID		0x0101
#define	IETP_IC_TYPE		0x0103
#define	IETP_OSM_VERSION	0x0103
#define	IETP_NSM_VERSION	0x0104
#define	IETP_TRACENUM		0x0105
#define	IETP_MAX_X_AXIS		0x0106
#define	IETP_MAX_Y_AXIS		0x0107
#define	IETP_RESOLUTION		0x0108
#define	IETP_PRESSURE		0x010A

#define	IETP_CONTROL		0x0300
#define	IETP_CTRL_ABSOLUTE	0x0001
#define	IETP_CTRL_STANDARD	0x0000

#define	IETP_REPORT_LEN_LO	31
#define	IETP_REPORT_LEN_HI	36
#define	IETP_MAX_FINGERS	5

#define	IETP_REPORT_ID_LO	0x5D
#define	IETP_REPORT_ID_HI	0x60

#define	IETP_TOUCH_INFO		0
#define	IETP_FINGER_DATA	1
#define	IETP_FINGER_DATA_LEN	5
#define	IETP_WH_DATA		31

#define	IETP_TOUCH_LMB		(1 << 0)
#define	IETP_TOUCH_RMB		(1 << 1)
#define	IETP_TOUCH_MMB		(1 << 2)

#define	IETP_MAX_PRESSURE	255
#define	IETP_FWIDTH_REDUCE	90
#define	IETP_PRESSURE_BASE	25

int	ietp_match(struct device *, void *, void *);
void	ietp_attach(struct device *, struct device *, void *);
int	ietp_detach(struct device *, int);
int	ietp_activate(struct device *, int);

int	ietp_intr(void *);
int	ietp_reset(struct ietp_softc *);

int	ietp_fetch_descriptor(struct ietp_softc *sc);
int	ietp_set_power(struct ietp_softc *sc, int power);
int	ietp_reset_cmd(struct ietp_softc *sc);

int32_t		ietp_res2dpmm(uint8_t, bool);

int		ietp_iic_read_reg(struct ietp_softc *, uint16_t, size_t, void *);
int		ietp_iic_write_reg(struct ietp_softc *, uint16_t, uint16_t);
int		ietp_iic_set_absolute_mode(struct ietp_softc *, bool);

const struct cfattach ietp_ca = {
	sizeof(struct ietp_softc),
	ietp_match,
	ietp_attach,
	ietp_detach,
	ietp_activate,
};

const struct wsmouse_accessops ietp_mouse_access = {
	ietp_enable,
	ietp_ioctl,
	ietp_disable
};

struct cfdriver ietp_cd = {
	NULL, "ietp", DV_DULL
};

int
ietp_match(struct device *parent, void *match, void *aux)
{
	struct i2c_attach_args *ia = aux;

	if (strcmp(ia->ia_name, "ietp") == 0)
		return (1);

	return (0);
}

int32_t
ietp_res2dpmm(uint8_t res, bool hi_precision)
{
	int32_t dpi;

	dpi = hi_precision ? 300 + res * 100 : 790 + res * 10;

	return (dpi * 10 /254);
}

void
ietp_attach(struct device *parent, struct device *self, void *aux)
{
	struct ietp_softc *sc = (struct ietp_softc *)self;
 	struct i2c_attach_args *ia = aux;
	uint16_t buf, reg;
	uint8_t *buf8;
	uint8_t pattern;
	struct wsmousedev_attach_args a;
	struct wsmousehw *hw;

	sc->sc_tag = ia->ia_tag;
	sc->sc_addr = ia->ia_addr;

	ietp_fetch_descriptor(sc);

	if (ia->ia_intr) {
		printf(" %s", iic_intr_string(sc->sc_tag, ia->ia_intr));

		sc->sc_ih = iic_intr_establish(sc->sc_tag, ia->ia_intr,
		    IPL_TTY, ietp_intr, sc, sc->sc_dev.dv_xname);
		if (sc->sc_ih == NULL) {
			printf(", can't establish interrupt\n");
			return;
		}
	}

	sc->sc_buttons = 0;
	sc->sc_refcnt = 0;

	buf8 = (uint8_t *)&buf;

	if (ietp_iic_read_reg(sc, IETP_UNIQUEID, sizeof(buf), &buf) != 0) {
		printf(": failed reading product ID\n");
		return;
	}
	sc->product_id = le16toh(buf);

	if (ietp_iic_read_reg(sc, IETP_PATTERN, sizeof(buf), &buf) != 0) {
		printf(": failed reading pattern\n");
		return;
	}
	pattern = buf == 0xFFFF ? 0 : buf8[1];
	sc->hi_precision = pattern >= 0x02;

	reg = pattern >= 0x01 ? IETP_IC_TYPE : IETP_OSM_VERSION;
	if (ietp_iic_read_reg(sc, reg, sizeof(buf), &buf) != 0) {
		printf(": failed reading IC type\n");
		return;
	}
	sc->ic_type = pattern >= 0x01 ? be16toh(buf) : buf8[1];

	if (ietp_iic_read_reg(sc, IETP_NSM_VERSION, sizeof(buf), &buf) != 0) {
		printf(": failed reading SM version\n");
		return;
	}
	sc->is_clickpad = (buf8[0] & 0x10) != 0;

	if (ietp_iic_set_absolute_mode(sc, true) != 0) {
		printf(": failed to set absolute mode\n");
		return;
	}

	if (ietp_iic_read_reg(sc, IETP_MAX_X_AXIS, sizeof(buf), &buf) != 0) {
		printf(": failed reading max x\n");
		return;
	}
	sc->max_x = le16toh(buf);

	if (ietp_iic_read_reg(sc, IETP_MAX_Y_AXIS, sizeof(buf), &buf) != 0) {
		printf(": failed reading max y\n");
		return;
	}
	sc->max_y = le16toh(buf);

	if (ietp_iic_read_reg(sc, IETP_TRACENUM, sizeof(buf), &buf) != 0) {
		printf(": failed reading trace info\n");
		return;
	}
	sc->trace_x = sc->max_x / buf8[0];
	sc->trace_y = sc->max_y / buf8[1];

	if (ietp_iic_read_reg(sc, IETP_PRESSURE, sizeof(buf), &buf) != 0) {
		printf(": failed reading pressure format\n");
		return;
	}
	sc->pressure_base = (buf8[0] & 0x10) ? 0 : IETP_PRESSURE_BASE;

	if (ietp_iic_read_reg(sc, IETP_RESOLUTION, sizeof(buf), &buf)  != 0) {
		printf(": failed reading resolution\n");
		return;
	}
	/* Conversion from internal format to dot per mm */
	sc->res_x = ietp_res2dpmm(buf8[0], sc->hi_precision);
	sc->res_y = ietp_res2dpmm(buf8[1], sc->hi_precision);

	sc->report_id = sc->hi_precision ?
	    IETP_REPORT_ID_HI : IETP_REPORT_ID_LO;
	sc->report_len = sc->hi_precision ?
	    IETP_REPORT_LEN_HI : IETP_REPORT_LEN_LO;

	sc->sc_ibuf = malloc(IETP_REPORT_LEN_HI + 12, M_DEVBUF,
	    M_NOWAIT | M_ZERO);
	sc->sc_isize = sc->report_len + 3;

	a.accessops = &ietp_mouse_access;
	a.accesscookie = sc;
	sc->sc_wsmousedev = config_found(self, &a, wsmousedevprint);

	hw = wsmouse_get_hw(sc->sc_wsmousedev);
	hw->type = WSMOUSE_TYPE_TOUCHPAD;
	hw->hw_type = sc->is_clickpad ? WSMOUSEHW_CLICKPAD : WSMOUSEHW_TOUCHPAD;
	hw->x_min = 0;
	hw->x_max = sc->max_x;
	hw->y_min = 0;
	hw->y_max = sc->max_y;
	hw->h_res = sc->res_x;
	hw->v_res = sc->res_y;
	hw->mt_slots = IETP_MAX_FINGERS;

	wsmouse_configure(sc->sc_wsmousedev, NULL, 0);

	/* power down until we're opened */
	if (ietp_set_power(sc, I2C_HID_POWER_OFF)) {
		printf(": failed to power down\n");
		return;
	}

	printf("\n");

	DPRINTF(("%s: max_x=%d, max_y=%d, %s\n", sc->sc_dev.dv_xname,
		 sc->max_x, sc->max_y,
		 sc->is_clickpad ? "clickpad" : "touchpad"));

	return;
}

int
ietp_detach(struct device *self, int flags)
{
	struct ietp_softc *sc = (struct ietp_softc *)self;

	if (sc->sc_ih != NULL) {
		iic_intr_disestablish(sc->sc_tag, sc->sc_ih);
		sc->sc_ih = NULL;
	}

	if (sc->sc_ibuf != NULL) {
		free(sc->sc_ibuf, M_DEVBUF, sc->sc_isize);
		sc->sc_ibuf = NULL;
	}

	return (0);
}

int
ietp_activate(struct device *self, int act)
{
	struct ietp_softc *sc = (struct ietp_softc *)self;
	int rv;

	DPRINTF(("%s(%d)\n", __func__, act));

	switch (act) {
	case DVACT_QUIESCE:
		rv = config_activate_children(self, act);
		sc->sc_dying = 1;
		if (ietp_set_power(sc, I2C_HID_POWER_OFF))
			printf("%s: failed to power down\n",
			    sc->sc_dev.dv_xname);
		break;
	case DVACT_WAKEUP:
		ietp_reset(sc);
		sc->sc_dying = 0;
		rv = config_activate_children(self, act);
		break;
	default:
		rv = config_activate_children(self, act);
		break;
	}
	return rv;
}

void
ietp_sleep(struct ietp_softc *sc, int ms)
{
	if (cold)
		delay(ms * 1000);
	else
		tsleep_nsec(&sc, PWAIT, "ietp", MSEC_TO_NSEC(ms));
}

int
ietp_iic_set_absolute_mode(struct ietp_softc *sc, bool enable)
{
	static const struct {
		uint16_t	ic_type;
		uint16_t	product_id;
	} special_fw[] = {
	    { 0x0E, 0x05 }, { 0x0E, 0x06 }, { 0x0E, 0x07 }, { 0x0E, 0x09 },
	    { 0x0E, 0x13 }, { 0x08, 0x26 },
	};
	uint16_t val;
	int i, error;
	bool require_wakeup;

	error = 0;

	/*
	 * Some ASUS touchpads need to be powered on to enter absolute mode.
	 */
	require_wakeup = false;
	for (i = 0; i < nitems(special_fw); i++) {
		if (sc->ic_type == special_fw[i].ic_type &&
		    sc->product_id == special_fw[i].product_id) {
			require_wakeup = true;
			break;
		}
	}

	if (require_wakeup && ietp_set_power(sc, I2C_HID_POWER_ON) != 0) {
		printf("%s: failed writing poweron command\n",
		    sc->sc_dev.dv_xname);
		return (EIO);
	}

	val = enable ? IETP_CTRL_ABSOLUTE : IETP_CTRL_STANDARD;
	if (ietp_iic_write_reg(sc, IETP_CONTROL, val) != 0) {
		printf("%s: failed setting absolute mode\n",
		    sc->sc_dev.dv_xname);
		error = EIO;
	}

	if (require_wakeup && ietp_set_power(sc, I2C_HID_POWER_OFF) != 0) {
		printf("%s: failed writing poweroff command\n",
		    sc->sc_dev.dv_xname);
		error = EIO;
	}

	return (error);
}

int
ietp_iic_read_reg(struct ietp_softc *sc, uint16_t reg, size_t len, void *val)
{
	uint8_t cmd[] = {
		reg & 0xff,
		reg >> 8,
	};

	return iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_addr,
		 &cmd, 2, val, len, 0);
}

int
ietp_iic_write_reg(struct ietp_softc *sc, uint16_t reg, uint16_t val)
{
	uint8_t cmd[] = {
		reg & 0xff,
		reg >> 8,
		val & 0xff,
		val >> 8,
	};

	return iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP, sc->sc_addr,
		 &cmd, 4, NULL, 0, 0);
}

int
ietp_set_power(struct ietp_softc *sc, int power)
{
	int res = 1;
	uint8_t cmd[] = {
		htole16(sc->hid_desc.wCommandRegister) & 0xff,
		htole16(sc->hid_desc.wCommandRegister) >> 8,
		power,
		I2C_HID_CMD_SET_POWER,
	};

	iic_acquire_bus(sc->sc_tag, 0);

	DPRINTF(("%s: HID command I2C_HID_CMD_SET_POWER(%d)\n",
		 sc->sc_dev.dv_xname, power));

	/* 22 00 00 08 */
	res = iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP, sc->sc_addr,
		       &cmd, sizeof(cmd), NULL, 0, 0);

	iic_release_bus(sc->sc_tag, 0);

	return (res);
}

int
ietp_reset_cmd(struct ietp_softc *sc)
{
	int res = 1;
	uint8_t cmd[] = {
		htole16(sc->hid_desc.wCommandRegister) & 0xff,
		htole16(sc->hid_desc.wCommandRegister) >> 8,
		0,
		I2C_HID_CMD_RESET,
	};

	iic_acquire_bus(sc->sc_tag, 0);

	DPRINTF(("%s: HID command I2C_HID_CMD_RESET\n",
		 sc->sc_dev.dv_xname));

	/* 22 00 00 01 */
	res = iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP, sc->sc_addr,
		       &cmd, sizeof(cmd), NULL, 0, 0);

	iic_release_bus(sc->sc_tag, 0);

	return (res);
}

int
ietp_fetch_descriptor(struct ietp_softc *sc)
{
	int i, res = 1;
	/*
	 * 5.2.2 - HID Descriptor Retrieval
	 * register is passed from the controller
	 */
	uint8_t cmd[] = {
		1,
		0,
	};

	iic_acquire_bus(sc->sc_tag, 0);

	DPRINTF(("%s: HID command I2C_HID_CMD_DESCR at 0x1\n",
		 sc->sc_dev.dv_xname));

	/* 20 00 */
	res = iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_addr,
		       &cmd, sizeof(cmd), &sc->hid_desc_buf,
		       sizeof(struct i2c_hid_desc), 0);

	DPRINTF(("%s: HID descriptor:", sc->sc_dev.dv_xname));
	for (i = 0; i < sizeof(struct i2c_hid_desc); i++)
		DPRINTF((" %.2x", sc->hid_desc_buf[i]));
	DPRINTF(("\n"));

	iic_release_bus(sc->sc_tag, 0);

	return (res);
}

int
ietp_reset(struct ietp_softc *sc)
{
	DPRINTF(("%s: resetting\n", sc->sc_dev.dv_xname));

	if (ietp_set_power(sc, I2C_HID_POWER_ON)) {
		printf("%s: failed to power on\n", sc->sc_dev.dv_xname);
		return (1);
	}

	ietp_sleep(sc, 100);

	if (ietp_reset_cmd(sc)) {
		printf("%s: failed to reset hardware\n", sc->sc_dev.dv_xname);

		ietp_set_power(sc, I2C_HID_POWER_OFF);

		return (1);
	}

	ietp_sleep(sc, 100);

	return (0);
}

void
parse_input(struct ietp_softc *sc, u_char *report, int len)
{
	uint8_t *fdata;
	int32_t finger;
	int32_t x, y, p;
	int buttons = 0;
	int s;

	/* we seem to get 0 length reports sometimes, ignore them */
	if (len == 0)
		return;
	if (len != sc->report_len) {
		printf("%s: wrong report length (%d vs %d expected)",
		    sc->sc_dev.dv_xname, len, (int) sc->report_len);
		return;
	}

	s = spltty();

	buttons = report[IETP_TOUCH_INFO] & 7;

	if (sc->sc_buttons != buttons) {
		wsmouse_buttons(sc->sc_wsmousedev, buttons);
		sc->sc_buttons = buttons;
	}

	for (finger = 0, fdata = report + IETP_FINGER_DATA;
	     finger < IETP_MAX_FINGERS;
	     finger++, fdata += IETP_FINGER_DATA_LEN) {
		if ((report[IETP_TOUCH_INFO] & (1 << (finger + 3))) != 0) {
			if (sc->hi_precision) {
				x = fdata[0] << 8 | fdata[1];
				y = fdata[2] << 8 | fdata[3];
			} else {
				x = (fdata[0] & 0xf0) << 4 | fdata[1];
				y = (fdata[0] & 0x0f) << 8 | fdata[2];
			}

			if (x > sc->max_x || y > sc->max_y) {
				printf("%s: [%d] x=%d y=%d over max (%d, %d)\n",
				    sc->sc_dev.dv_xname, finger, x, y,
				    sc->max_x, sc->max_y);
				continue;
			}


			p = MIN((int32_t)fdata[4] + sc->pressure_base,
				    IETP_MAX_PRESSURE);

		} else {
			x = 0;
			y = 0;
			p = 0;
		}

		DPRINTF(("position: [finger=%d, x=%d, y=%d, p=%d]\n", finger,
		    x, y, p));
		wsmouse_mtstate(sc->sc_wsmousedev, finger, x, y, p);
	}

	wsmouse_input_sync(sc->sc_wsmousedev);

	splx(s);
}

int
ietp_intr(void *arg)
{
	struct ietp_softc *sc = arg;
	int psize, i;
	u_char *p;
	u_int rep = 0;

	if (sc->sc_dying)
		return 1;

	/*
	 * XXX: force I2C_F_POLL for now to avoid dwiic interrupting
	 * while we are interrupting
	 */

	iic_acquire_bus(sc->sc_tag, I2C_F_POLL);
	iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_addr, NULL, 0,
	    sc->sc_ibuf, letoh16(sc->hid_desc.wMaxInputLength), I2C_F_POLL);
	iic_release_bus(sc->sc_tag, I2C_F_POLL);

	/*
	 * 6.1.1 - First two bytes are the packet length, which must be less
	 * than or equal to wMaxInputLength
	 */
	psize = sc->sc_ibuf[0] | sc->sc_ibuf[1] << 8;
	if (psize <= 2 || psize > sc->sc_isize) {
		DPRINTF(("%s: %s: invalid packet size (%d vs. %d)\n",
			    sc->sc_dev.dv_xname, __func__, psize,
			    sc->sc_isize));
		return (1);
	}

	/* 3rd byte is the report id */
	p = sc->sc_ibuf + 2;
	psize -= 2;
	rep = *p++;
	psize--;

	DPRINTF(("%s: %s: hid input (rep 0x%x):", sc->sc_dev.dv_xname, __func__,
	    rep));
	for (i = 0; i < psize; i++) {
		DPRINTF((" %.2x", p[i]));
	}
	DPRINTF(("\n"));

	if (sc->sc_refcnt && rep == sc->report_id) {
		parse_input(sc, p, psize);
	}

	return (1);
}

int
ietp_enable(void *dev)
{
	struct ietp_softc *sc = dev;

	DPRINTF(("%s: %s: refcnt=%d\n", sc->sc_dev.dv_xname,
	    __func__, sc->sc_refcnt));

	if (sc->sc_refcnt++ || sc->sc_isize == 0)
		return (0);

	/* power on */
	ietp_reset(sc);

	return (0);
}

void
ietp_disable(void *dev)
{
	struct ietp_softc *sc = dev;
	DPRINTF(("%s: %s: refcnt=%d\n", sc->sc_dev.dv_xname,
	    __func__, sc->sc_refcnt));

	if (--sc->sc_refcnt)
		return;

	/* no sub-devices open, conserve power */

	if (ietp_set_power(sc, I2C_HID_POWER_OFF))
		printf("%s: failed to power down\n", sc->sc_dev.dv_xname);
}

int
ietp_ioctl(void *dev, u_long cmd, caddr_t data, int flag,
    struct proc *p)
{
	struct ietp_softc *sc = dev;
 	struct wsmouse_calibcoords *wsmc = (struct wsmouse_calibcoords *)data;

	switch (cmd) {
	case WSMOUSEIO_GTYPE:
		*(u_int *)data = WSMOUSE_TYPE_TOUCHPAD;
		return 0;

	case WSMOUSEIO_GCALIBCOORDS:
		wsmc->minx = 0;
		wsmc->maxx = sc->max_x;
		wsmc->miny = 0;
		wsmc->maxy = sc->max_y;
		wsmc->swapxy = 0;
		wsmc->resx = sc->res_x;
		wsmc->resy = sc->res_y;
		return 0;
	}
	return -1;
}
