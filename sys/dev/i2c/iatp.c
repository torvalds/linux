/* $OpenBSD: iatp.c,v 1.11 2024/08/19 14:24:24 deraadt Exp $ */
/*
 * Atmel maXTouch i2c touchscreen/touchpad driver
 * Copyright (c) 2016 joshua stein <jcs@openbsd.org>
 *
 * AT421085 datasheet:
 * http://www.atmel.com/images/Atmel-9626-AT42-QTouch-BSW-AT421085-Object-Protocol-Guide_Datasheet.pdf
 *
 * Uses code from libmaxtouch <https://github.com/atmel-maxtouch/mxt-app>
 * Copyright 2011 Atmel Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *    1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 *    2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY ATMEL ''AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL ATMEL OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/stdint.h>

#include <dev/i2c/i2cvar.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsmousevar.h>
#include <dev/hid/hid.h>
#include <dev/hid/hidmsvar.h>

/* #define IATP_DEBUG */

#ifdef IATP_DEBUG
#define DPRINTF(x) printf x
#else
#define DPRINTF(x)
#endif

struct mxt_object {
	uint8_t type;
	uint16_t start_pos;
	uint8_t size_minus_one;
#define MXT_SIZE(o)		((uint16_t)((o)->size_minus_one) + 1)
	uint8_t instances_minus_one;
#define MXT_INSTANCES(o)	((uint16_t)((o)->instances_minus_one) + 1)
	uint8_t num_report_ids;
} __packed;

struct mxt_id_info {
	uint8_t family;
	uint8_t variant;
	uint8_t version;
	uint8_t build;
	uint8_t matrix_x_size;
	uint8_t matrix_y_size;
	uint8_t num_objects;
} __packed;

struct mxt_info {
	struct mxt_id_info id;
	struct mxt_object *objects;
	uint32_t crc;
	uint8_t *raw_info;
	uint8_t max_report_id;
};

/* object types we care about (of 117 total!) */

#define MXT_GEN_MESSAGEPROCESSOR_T5	5

#define MXT_GEN_COMMANDPROCESSOR_T6	6
# define MXT_T6_STATUS_RESET		(1 << 7)
# define MXT_T6_STATUS_OFL		(1 << 6)
# define MXT_T6_STATUS_SIGERR		(1 << 5)
# define MXT_T6_STATUS_CAL		(1 << 4)
# define MXT_T6_STATUS_CFGERR		(1 << 3)
# define MXT_T6_STATUS_COMSERR		(1 << 2)
# define MXT_T6_CMD_RESET		0
# define MXT_T6_CMD_BACKUPNV		1
# define MXT_T6_CMD_CALIBRATE		2
# define MXT_T6_CMD_REPORTALL		3
# define MXT_T6_CMD_DIAGNOSTIC		5

#define MXT_GEN_POWERCONFIG_T7		7
# define MXT_T7_POWER_MODE_DEFAULT	1
# define MXT_T7_POWER_MODE_DEEP_SLEEP	2
struct mxt_t7_config {
	uint8_t idle;
	uint8_t active;
	uint8_t atoi_timeout;
} __packed;

#define MXT_SPT_GPIOPWM_T19		19
static const struct mxt_t19_button_map {
	const char *vendor;
	const char *product;
	const char *hid;
	int bit;
} mxt_t19_button_map_devs[] = {
	/* Chromebook Pixel 2015 */
	{ "GOOGLE", "Samus", "ATML0000", 3 },
	/* Other Google Chromebooks */
	{ "GOOGLE", "", "ATML0000", 5 },
	{ NULL }
};

#define MXT_SPT_MESSAGECOUNT_T44	44

#define MXT_TOUCH_MULTITOUCHSCREEN_T100	100
# define MXT_T100_CTRL			0
# define MXT_T100_CFG1			1
# define MXT_T100_TCHAUX		3
# define MXT_T100_XRANGE		13
# define MXT_T100_YRANGE		24
# define MXT_T100_CFG_SWITCHXY		(1 << 5)
# define MXT_T100_TCHAUX_VECT		(1 << 0)
# define MXT_T100_TCHAUX_AMPL		(1 << 1)
# define MXT_T100_TCHAUX_AREA		(1 << 2)
# define MXT_T100_DETECT		(1 << 7)
# define MXT_T100_TYPE_MASK		0x70

enum t100_type {
	MXT_T100_TYPE_FINGER		= 1,
	MXT_T100_TYPE_PASSIVE_STYLUS	= 2,
	MXT_T100_TYPE_HOVERING_FINGER	= 4,
	MXT_T100_TYPE_GLOVE		= 5,
	MXT_T100_TYPE_LARGE_TOUCH	= 6,
};

#define MXT_DISTANCE_ACTIVE_TOUCH	0
#define MXT_DISTANCE_HOVERING		1

#define MXT_TOUCH_MAJOR_DEFAULT		1

struct iatp_softc {
	struct device		sc_dev;
	i2c_tag_t		sc_tag;

	i2c_addr_t		sc_addr;
	void			*sc_ih;

	struct device		*sc_wsmousedev;
	char			sc_hid[16];
	int			sc_busy;
	int			sc_enabled;
	int			sc_touchpad;
	struct tsscale		sc_tsscale;

	uint8_t			*table;
	size_t			table_size;

	struct mxt_info		info;
	uint8_t			*msg_buf;
	uint8_t			multitouch;
	uint8_t			num_touchids;
	uint32_t		max_x;
	uint32_t		max_y;
	uint8_t			button;

	uint16_t		t5_address;
	uint8_t			t5_msg_size;
	uint16_t		t6_address;
	uint8_t			t6_reportid;
	uint16_t		t7_address;
	struct mxt_t7_config	t7_config;
	uint8_t			t19_reportid;
	int			t19_button_bit;
	uint16_t		t44_address;
	uint8_t			t100_reportid_min;
	uint8_t			t100_reportid_max;
	uint8_t			t100_aux_ampl;
	uint8_t			t100_aux_area;
	uint8_t			t100_aux_vect;
};

int	iatp_match(struct device *, void *, void *);
void	iatp_attach(struct device *, struct device *, void *);
int	iatp_detach(struct device *, int);
int	iatp_activate(struct device *, int);

int	iatp_ioctl(void *, u_long, caddr_t, int, struct proc *);
int	iatp_enable(void *);
void	iatp_disable(void *);

int	iatp_read_reg(struct iatp_softc *, uint16_t, size_t, void *);
int	iatp_write_reg(struct iatp_softc *, uint16_t, size_t, void *);
int	iatp_init(struct iatp_softc *);
int	iatp_intr(void *);

int	iatp_proc_msg(struct iatp_softc *, uint8_t *);
int	iatp_t5_read_msgs(struct iatp_softc *, int);
void	iatp_t6_proc_msg(struct iatp_softc *, uint8_t *);
int	iatp_t7_set_power_mode(struct iatp_softc *, int);
void	iatp_t19_proc_msg(struct iatp_softc *, uint8_t *);
int	iatp_t44_read_count(struct iatp_softc *);
void	iatp_t100_proc_msg(struct iatp_softc *, uint8_t *);

const struct wsmouse_accessops iatp_accessops = {
	iatp_enable,
	iatp_ioctl,
	iatp_disable,
};

const struct cfattach iatp_ca = {
	sizeof(struct iatp_softc),
	iatp_match,
	iatp_attach,
	iatp_detach,
	iatp_activate
};

struct cfdriver iatp_cd = {
	NULL, "iatp", DV_DULL
};

int
iatp_match(struct device *parent, void *match, void *aux)
{
	struct i2c_attach_args *ia = aux;

	if (strcmp(ia->ia_name, "iatp") == 0)
		return 1;

	return 0;
}

void
iatp_attach(struct device *parent, struct device *self, void *aux)
{
	struct iatp_softc *sc = (struct iatp_softc *)self;
	struct i2c_attach_args *ia = aux;
	struct wsmousedev_attach_args wsmaa;

	sc->sc_tag = ia->ia_tag;
	sc->sc_addr = ia->ia_addr;

	if (ia->ia_cookie != NULL)
		memcpy(&sc->sc_hid, ia->ia_cookie, sizeof(sc->sc_hid));

	if (!iatp_init(sc))
		return;

	if (ia->ia_intr) {
		printf(" %s", iic_intr_string(sc->sc_tag, ia->ia_intr));

		sc->sc_ih = iic_intr_establish(sc->sc_tag, ia->ia_intr,
		    IPL_TTY, iatp_intr, sc, sc->sc_dev.dv_xname);
		if (sc->sc_ih == NULL) {
			printf(", can't establish interrupt\n");
			return;
		}
	}

	printf(": Atmel maXTouch Touch%s (%dx%d)\n",
	    sc->sc_touchpad ? "pad" : "screen", sc->max_x, sc->max_y);

	wsmaa.accessops = &iatp_accessops;
	wsmaa.accesscookie = sc;
	sc->sc_wsmousedev = config_found(self, &wsmaa, wsmousedevprint);
}

int
iatp_detach(struct device *self, int flags)
{
	struct iatp_softc *sc = (struct iatp_softc *)self;

	if (sc->sc_ih != NULL) {
		intr_disestablish(sc->sc_ih);
		sc->sc_ih = NULL;
	}

	sc->sc_enabled = 0;

	return 0;
}

int
iatp_activate(struct device *self, int act)
{
	struct iatp_softc *sc = (struct iatp_softc *)self;
	int rv;

	switch (act) {
	case DVACT_QUIESCE:
		rv = config_activate_children(self, act);
		iatp_t7_set_power_mode(sc, MXT_T7_POWER_MODE_DEEP_SLEEP);
		break;
	case DVACT_WAKEUP:
		sc->sc_busy = 1;
		iatp_init(sc);
		sc->sc_busy = 0;
		rv = config_activate_children(self, act);
		break;
	default:
		rv = config_activate_children(self, act);
		break;
	}
	return rv;
}

int
iatp_configure(struct iatp_softc *sc)
{
	struct wsmousehw *hw;

	hw = wsmouse_get_hw(sc->sc_wsmousedev);
	if (sc->sc_touchpad) {
		hw->type = WSMOUSE_TYPE_TOUCHPAD;
		hw->hw_type = WSMOUSEHW_CLICKPAD;
	} else {
		hw->type = WSMOUSE_TYPE_TPANEL;
		hw->hw_type = WSMOUSEHW_TPANEL;
	}
	hw->x_min = sc->sc_tsscale.minx;
	hw->x_max = sc->sc_tsscale.maxx;
	hw->y_min = sc->sc_tsscale.miny;
	hw->y_max = sc->sc_tsscale.maxy;
	hw->h_res = sc->sc_tsscale.resx;
	hw->v_res = sc->sc_tsscale.resy;
	hw->mt_slots = sc->num_touchids;

	return (wsmouse_configure(sc->sc_wsmousedev, NULL, 0));
}

int
iatp_enable(void *v)
{
	struct iatp_softc *sc = v;

	if (sc->sc_busy &&
	    tsleep_nsec(&sc->sc_busy, PRIBIO, "iatp", SEC_TO_NSEC(1)) != 0) {
		printf("%s: trying to enable but we're busy\n",
		    sc->sc_dev.dv_xname);
		return 1;
	}

	sc->sc_busy = 1;

	DPRINTF(("%s: enabling\n", sc->sc_dev.dv_xname));

	if (iatp_configure(sc)) {
		printf("%s: failed wsmouse_configure\n", sc->sc_dev.dv_xname);
		return 1;
	}

	/* force a read of any pending messages so we start getting new
	 * interrupts */
	iatp_t5_read_msgs(sc, sc->info.max_report_id);

	sc->sc_enabled = 1;
	sc->sc_busy = 0;

	return 0;
}

void
iatp_disable(void *v)
{
	struct iatp_softc *sc = v;

	DPRINTF(("%s: disabling\n", sc->sc_dev.dv_xname));

	if (sc->sc_touchpad)
		wsmouse_set_mode(sc->sc_wsmousedev, WSMOUSE_COMPAT);

	sc->sc_enabled = 0;
}

int
iatp_ioctl(void *v, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	struct iatp_softc *sc = v;
	struct wsmouse_calibcoords *wsmc = (struct wsmouse_calibcoords *)data;
	int wsmode;

	DPRINTF(("%s: %s: cmd %ld\n", sc->sc_dev.dv_xname, __func__, cmd));

	switch (cmd) {
	case WSMOUSEIO_SCALIBCOORDS:
		sc->sc_tsscale.minx = wsmc->minx;
		sc->sc_tsscale.maxx = wsmc->maxx;
		sc->sc_tsscale.miny = wsmc->miny;
		sc->sc_tsscale.maxy = wsmc->maxy;
		sc->sc_tsscale.swapxy = wsmc->swapxy;
		sc->sc_tsscale.resx = wsmc->resx;
		sc->sc_tsscale.resy = wsmc->resy;
		break;

	case WSMOUSEIO_GCALIBCOORDS:
		wsmc->minx = sc->sc_tsscale.minx;
		wsmc->maxx = sc->sc_tsscale.maxx;
		wsmc->miny = sc->sc_tsscale.miny;
		wsmc->maxy = sc->sc_tsscale.maxy;
		wsmc->swapxy = sc->sc_tsscale.swapxy;
		wsmc->resx = sc->sc_tsscale.resx;
		wsmc->resy = sc->sc_tsscale.resy;
		break;

	case WSMOUSEIO_GTYPE: {
		struct wsmousehw *hw = wsmouse_get_hw(sc->sc_wsmousedev);
		*(u_int *)data = hw->type;
		break;
	}

	case WSMOUSEIO_SETMODE:
		if (!sc->sc_touchpad)
			return -1;

		wsmode = *(u_int *)data;
		if (wsmode != WSMOUSE_COMPAT && wsmode != WSMOUSE_NATIVE) {
			printf("%s: invalid mode %d\n", sc->sc_dev.dv_xname,
			    wsmode);
			return EINVAL;
		}
		wsmouse_set_mode(sc->sc_wsmousedev, wsmode);
		break;

	default:
		return -1;
	}

	return 0;
}

int
iatp_init(struct iatp_softc *sc)
{
	uint8_t reportid;
	int i;

	sc->sc_enabled = 0;

	/* some sane defaults */
	sc->num_touchids = 10;
	sc->max_x = 1023;
	sc->max_y = 1023;
	sc->sc_touchpad = 0;

	/*
	 * AT42QT1085 Information block:
	 *
	 * ID information (struct mxt_id_info)
	 *	0 Family ID
	 *	1 Variant ID
	 *	2 Version
	 *	3 Build
	 *	4 Number of Keys
	 *	5 1
	 *	6 Number of Object Table Elements
	 * Object Table Element 1 (struct mxt_object)
	 *	7 Object Type
	 *	8-9 Object Start Address
	 *	10 Size - 1
	 *	11 Instances - 1
	 *	12 Number of report IDs per instance
	 * Object Table Element 2 (struct mxt_object)
	 * 	...
	 * Information Block Checksum
	 * [ Object 1 ]
	 * ...
	 */

	/* read table header */
	if (iatp_read_reg(sc, 0, sizeof(struct mxt_id_info), &sc->info.id) ||
	    !sc->info.id.num_objects) {
		printf("%s: failed reading main memory map\n",
		    sc->sc_dev.dv_xname);
		return 0;
	}

	sc->table_size = sc->info.id.num_objects * sizeof(struct mxt_object);
	sc->table = malloc(sc->table_size, M_DEVBUF, M_NOWAIT | M_ZERO);

	/* read all table objects */
	if (iatp_read_reg(sc, sizeof(struct mxt_id_info), sc->table_size,
	    sc->table)) {
		printf("%s: failed reading info table of size %zu\n",
		    sc->sc_dev.dv_xname, sc->table_size);
		return 0;
	}

	reportid = 1;
	for (i = 0; i < sc->info.id.num_objects; i++) {
		struct mxt_object *object = (void *)(sc->table +
		    (sizeof(struct mxt_object) * i));
		int min_id = 0, max_id = 0;

		if (object->num_report_ids) {
			min_id = reportid;
			reportid += (object->num_report_ids *
			    (uint8_t)MXT_INSTANCES(object));
			max_id = reportid - 1;
		}

		DPRINTF(("%s: object[%d] T%d at 0x%x, %d report ids (%d-%d)\n",
		    sc->sc_dev.dv_xname, i, object->type,
		    le16toh(object->start_pos), object->num_report_ids, min_id,
		    max_id));

		switch (object->type) {
		case MXT_GEN_MESSAGEPROCESSOR_T5:
			/*
			 * 4.2 - message processor is what interrupts and
			 * relays new messages to us
			 */

			if (sc->info.id.family == 0x80 &&
			    sc->info.id.version < 0x20)
				/*
				 * from linux: "On mXT224 firmware versions
				 * prior to V2.0 read and discard unused CRC
				 * byte otherwise DMA reads are misaligned."
				 */
				sc->t5_msg_size = MXT_SIZE(object);
			else
				sc->t5_msg_size = MXT_SIZE(object) - 1;

			sc->t5_address = le16toh(object->start_pos);
			break;

		case MXT_GEN_COMMANDPROCESSOR_T6:
			/*
			 * 4.3 - command processor receives commands from us
			 * and reports command status messages
			 */
			sc->t6_address = le16toh(object->start_pos);
			sc->t6_reportid = min_id;
			break;

		case MXT_GEN_POWERCONFIG_T7:
			/*
			 * 4.4 - power configuration, number of milliseconds
			 * between sampling in each mode
			 */
			sc->t7_address = le16toh(object->start_pos);

			iatp_read_reg(sc, sc->t7_address,
			    sizeof(sc->t7_config), &sc->t7_config);

			break;

		case MXT_SPT_GPIOPWM_T19: {
			/*
			 * generic gpio pin, mapped to touchpad button(s)
			 */
			const struct mxt_t19_button_map *m;

			sc->t19_reportid = min_id;

			/* find this machine's button config */
			sc->t19_button_bit = -1;
			if (hw_vendor == NULL || hw_prod == NULL)
				break;

			for (m = mxt_t19_button_map_devs; m->vendor != NULL;
			    m++) {
				if (strncmp(hw_vendor, m->vendor,
				    strlen(m->vendor)) != 0 ||
				    strncmp(hw_prod, m->product,
				    strlen(m->product)) != 0 ||
				    strncmp(sc->sc_hid, m->hid,
				    strlen(m->hid)) != 0)
					continue;

				DPRINTF(("%s: found matching t19 "
				    "button map device \"%s\"/\"%s\" on %s: "
				    "bit %d\n", sc->sc_dev.dv_xname,
				    m->vendor, m->product, m->hid, m->bit));
				sc->t19_button_bit = m->bit;
				break;
			}

			if (sc->t19_button_bit > -1)
				sc->sc_touchpad = 1;

			break;
		}

		case MXT_SPT_MESSAGECOUNT_T44:
			sc->t44_address = le16toh(object->start_pos);
			break;

		case MXT_TOUCH_MULTITOUCHSCREEN_T100: {
			uint16_t range_x, range_y;
			uint8_t orient, tchaux;
			int aux;

			sc->t100_reportid_min = min_id;
			sc->t100_reportid_max = max_id;
			sc->num_touchids = object->num_report_ids - 2;
			sc->multitouch = MXT_TOUCH_MULTITOUCHSCREEN_T100;

			if (iatp_read_reg(sc, object->start_pos +
			    MXT_T100_XRANGE, sizeof(range_x), &range_x) ||
			    iatp_read_reg(sc, object->start_pos +
			    MXT_T100_YRANGE, sizeof(range_y), &range_y) ||
			    iatp_read_reg(sc, object->start_pos +
			    MXT_T100_CFG1, 1, &orient) ||
			    iatp_read_reg(sc, object->start_pos +
			    MXT_T100_TCHAUX, 1, &tchaux)) {
				printf("%s: failed reading t100 settings\n",
				    sc->sc_dev.dv_xname);
				continue;
			}

			/*
			 * orient just affects the size we read, not the x/y
			 * values we read per-packet later.
			 */
			if (orient & MXT_T100_CFG_SWITCHXY) {
				sc->max_x = le16toh(range_y);
				sc->max_y = le16toh(range_x);
			} else {
				sc->max_x = le16toh(range_x);
				sc->max_y = le16toh(range_y);
			}

			aux = 6;
			if (tchaux & MXT_T100_TCHAUX_VECT)
				sc->t100_aux_vect = aux++;
			if (tchaux & MXT_T100_TCHAUX_AMPL)
				sc->t100_aux_ampl = aux++;
			if (tchaux & MXT_T100_TCHAUX_AREA)
				sc->t100_aux_area = aux++;
			break;
		}
		}
	}

	sc->info.max_report_id = reportid;

	sc->sc_tsscale.minx = 0;
	sc->sc_tsscale.maxx = sc->max_x;
	sc->sc_tsscale.miny = 0;
	sc->sc_tsscale.maxy = sc->max_y;
	sc->sc_tsscale.swapxy = 0;
	sc->sc_tsscale.resx = 0;
	sc->sc_tsscale.resy = 0;

	/*
	 * iatp_t44_read_count expects t5 message processor to immediately
	 * follow t44 message count byte
	 */
	if (sc->t44_address && (sc->t5_address != sc->t44_address + 1)) {
		printf("%s: t5 address (0x%x) != t44 (0x%x + 1)\n",
		    sc->sc_dev.dv_xname, sc->t5_address, sc->t44_address);
		return 0;
	}

	sc->msg_buf = mallocarray(sc->info.max_report_id, sc->t5_msg_size,
	    M_DEVBUF, M_NOWAIT | M_ZERO);

	/* flush queue of any pending messages */
	iatp_t5_read_msgs(sc, sc->info.max_report_id);

	return 1;
}

int
iatp_read_reg(struct iatp_softc *sc, uint16_t reg, size_t len, void *val)
{
	uint8_t cmd[2] = { reg & 0xff, (reg >> 8) & 0xff };
	int ret;

	iic_acquire_bus(sc->sc_tag, I2C_F_POLL);

	ret = iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_addr, &cmd,
	    sizeof(cmd), val, len, I2C_F_POLL);

	iic_release_bus(sc->sc_tag, I2C_F_POLL);

	return ret;
}

int
iatp_write_reg(struct iatp_softc *sc, uint16_t reg, size_t len, void *val)
{
	int ret;
	uint8_t *cmd;

	cmd = malloc(len + 2, M_DEVBUF, M_NOWAIT | M_ZERO);
	cmd[0] = reg & 0xff;
	cmd[1] = (reg >> 8) & 0xff;
	memcpy(&cmd[2], val, len);

	iic_acquire_bus(sc->sc_tag, 0);

	ret = iic_exec(sc->sc_tag, I2C_OP_WRITE, sc->sc_addr, cmd, len + 2,
	    NULL, 0, I2C_F_POLL);

	iic_release_bus(sc->sc_tag, 0);

	free(cmd, M_DEVBUF, len + 2);

	return ret;
}

int
iatp_intr(void *arg)
{
	struct iatp_softc *sc = arg;
	int count;

	DPRINTF(("%s: %s (busy:%d enabled:%d)\n", sc->sc_dev.dv_xname,
	    __func__, sc->sc_busy, sc->sc_enabled));

	if (sc->sc_busy)
		return 1;

	sc->sc_busy = 1;

	if (sc->t44_address)
		count = iatp_t44_read_count(sc);
	else
		count = 1;

	if (count)
		iatp_t5_read_msgs(sc, count);

	sc->sc_busy = 0;
	wakeup(&sc->sc_busy);

	return 1;
}

int
iatp_proc_msg(struct iatp_softc *sc, uint8_t *msg)
{
	uint8_t report_id = msg[0];
	int i;

	/* process a single message that has already been read off the wire */

	if (report_id == 0xff)
		/*
		 * this is usually when we've intentionally over-read just to
		 * clear any pending data to keep interrupts flowing
		 */
		return 0;

	DPRINTF(("%s: %s: report id %d\n", sc->sc_dev.dv_xname, __func__,
	    report_id));

	if (report_id == sc->t19_reportid)
		iatp_t19_proc_msg(sc, msg);
	else if (report_id >= sc->t100_reportid_min &&
	    report_id <= sc->t100_reportid_max)
		iatp_t100_proc_msg(sc, msg);
	else {
		DPRINTF(("%s: unknown message (report id %d)",
		    sc->sc_dev.dv_xname, report_id));
		for (i = 0; i < sc->t5_msg_size; i++)
			DPRINTF((" %02x", msg[i]));
		DPRINTF(("\n"));
	}

	return 1;
}

int
iatp_t5_read_msgs(struct iatp_softc *sc, int count)
{
	int i;

	if (count > sc->info.max_report_id) {
		DPRINTF(("%s: clamping count %d to max_report_id %d\n",
		    sc->sc_dev.dv_xname, count, sc->info.max_report_id));
		count = sc->info.max_report_id;
	}

	DPRINTF(("%s: %s: %d message(s) to read\n", sc->sc_dev.dv_xname,
	    __func__, count));

	if (iatp_read_reg(sc, sc->t5_address, sc->t5_msg_size * count,
	    sc->msg_buf)) {
		printf("%s: failed reading %d\n", sc->sc_dev.dv_xname,
		    sc->t5_msg_size * count);
		return 0;
	}

	for (i = 0;  i < count; i++)
		iatp_proc_msg(sc, sc->msg_buf + (sc->t5_msg_size * i));

	return 1;
}

void
iatp_t6_proc_msg(struct iatp_softc *sc, uint8_t *msg)
{
	uint8_t status = msg[1];

	if (status & MXT_T6_STATUS_RESET)
		DPRINTF(("%s: completed reset\n", sc->sc_dev.dv_xname));
	else
		DPRINTF(("%s: other status report 0x%x\n", sc->sc_dev.dv_xname,
		    status));
}

int
iatp_t7_set_power_mode(struct iatp_softc *sc, int mode)
{
	struct mxt_t7_config new_config;

	if (mode == MXT_T7_POWER_MODE_DEEP_SLEEP) {
		new_config.idle = 0;
		new_config.active = 0;
		new_config.atoi_timeout = 0;
	} else
		new_config = sc->t7_config;

	DPRINTF(("%s: setting power mode to %d\n", sc->sc_dev.dv_xname, mode));

	if (iatp_write_reg(sc, sc->t7_address, sizeof(new_config),
	    &new_config)) {
		printf("%s: failed setting power mode to %d\n",
		    sc->sc_dev.dv_xname, mode);
		return 1;
	}

	return 0;
}

void
iatp_t19_proc_msg(struct iatp_softc *sc, uint8_t *msg)
{
	int s;

	if (!sc->sc_enabled)
		return;

	/* active-low switch */
	sc->button = !(msg[1] & (1 << sc->t19_button_bit));

	DPRINTF(("%s: button is %d\n", sc->sc_dev.dv_xname, sc->button));

	s = spltty();
	wsmouse_buttons(sc->sc_wsmousedev, sc->button);
	wsmouse_input_sync(sc->sc_wsmousedev);
	splx(s);
}

int
iatp_t44_read_count(struct iatp_softc *sc)
{
	int ret, count;

	/* read t44 count byte and t5 message data in one shot */
	ret = iatp_read_reg(sc, sc->t44_address, 1 + sc->t5_msg_size,
	    sc->msg_buf);
	if (ret) {
		printf("%s: failed reading t44 and t5\n", sc->sc_dev.dv_xname);
		return 0;
	}

	count = sc->msg_buf[0];
	if (count == 0) {
		DPRINTF(("%s: %s: no messages\n", sc->sc_dev.dv_xname,
		    __func__));
		/* flush so we keep getting interrupts */
		iatp_t5_read_msgs(sc, sc->info.max_report_id);
		return 0;
	}

	count--;
	iatp_proc_msg(sc, sc->msg_buf + 1);

	return count;
}

void
iatp_t100_proc_msg(struct iatp_softc *sc, uint8_t *msg)
{
	int id = msg[0] - sc->t100_reportid_min - 2;
	int s;
	uint8_t status, type = 0, pressure = 0;
	uint16_t x, y;

	if (id < 0 || !sc->sc_enabled)
		return;

	status = msg[1];
	x = (msg[3] << 8) | msg[2];
	y = (msg[5] << 8) | msg[4];

	if (status & MXT_T100_DETECT) {
		type = (status & MXT_T100_TYPE_MASK) >> 4;

		if (sc->t100_aux_ampl)
			pressure = msg[sc->t100_aux_ampl];

		if (!pressure && type != MXT_T100_TYPE_HOVERING_FINGER)
			pressure = 50; /* large enough for synaptics driver */

		DPRINTF(("%s: type=%d x=%d y=%d finger=%d pressure=%d "
		    "button=%d\n", sc->sc_dev.dv_xname, type, x, y, id,
		    pressure, sc->button));
	} else {
		DPRINTF(("%s: closing slot for finger=%d\n",
		    sc->sc_dev.dv_xname, id));

		if (sc->sc_touchpad)
			x = y = 0;

		pressure = 0;
	}

	if (sc->sc_touchpad)
		y = (sc->max_y - y);

	/* TODO: adjust to sc_tsscale? */

	s = spltty();

	wsmouse_mtstate(sc->sc_wsmousedev, id, x, y, pressure);

	/* on the touchscreen, assume any finger down is clicking */
	if (!sc->sc_touchpad)
		wsmouse_buttons(sc->sc_wsmousedev, pressure ? 1 : 0);

	wsmouse_input_sync(sc->sc_wsmousedev);

	splx(s);
}
