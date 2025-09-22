/*	$OpenBSD: ugold.c,v 1.29 2024/07/27 17:31:49 miod Exp $   */

/*
 * Copyright (c) 2013 Takayoshi SASANO <uaa@openbsd.org>
 * Copyright (c) 2013 Martin Pieuchot <mpi@openbsd.org>
 * Copyright (c) 2015 Joerg Jung <jung@openbsd.org>
 * Copyright (c) 2023 Miodrag Vallat.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Driver for Microdia's HID based TEMPer and TEMPerHUM temperature and
 * humidity sensors
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/sensors.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbhid.h>

#include <dev/usb/usbdi.h>
#include <dev/usb/usbdevs.h>
#include <dev/usb/uhidev.h>

#define UGOLD_INNER		0
#define UGOLD_OUTER		1
#define UGOLD_HUM		1
#define UGOLD_MAX_SENSORS	2

#define UGOLD_CMD_DATA		0x80
#define UGOLD_CMD_INIT		0x82

#define UGOLD_TYPE_INVALID	-1
#define UGOLD_TYPE_SI7005	1
#define UGOLD_TYPE_SI7006	2
#define UGOLD_TYPE_SHT1X	3
#define UGOLD_TYPE_GOLD		4
#define UGOLD_TYPE_TEMPERX	5
#define UGOLD_TYPE_DS75		6

/*
 * This driver uses three known commands for the TEMPer and TEMPerHUM
 * devices.
 *
 * The first byte of the answer corresponds to the command and the
 * second one seems to be the size (in bytes) of the answer.
 *
 * The device always sends 8 bytes and if the length of the answer
 * is less than that, it just leaves the last bytes untouched.  That
 * is why most of the time the last n bytes of the answers are the
 * same.
 *
 * The type command below seems to generate two answers with a
 * string corresponding to the device, for example:
 *	'TEMPer1F' and '1.1Per1F' (here Per1F is repeated).
 */
static uint8_t cmd_data[8] = { 0x01, 0x80, 0x33, 0x01, 0x00, 0x00, 0x00, 0x00 };
static uint8_t cmd_init[8] = { 0x01, 0x82, 0x77, 0x01, 0x00, 0x00, 0x00, 0x00 };
static uint8_t cmd_type[8] = { 0x01, 0x86, 0xff, 0x01, 0x00, 0x00, 0x00, 0x00 };
/*
 * The following command is also recognized and reports some kind of status
 * byte (i.e. 87 xx 00 00 00 00 00 00).
			     { 0x01, 0x87, 0xee, 0x01, 0x00, 0x00, 0x00, 0x00 };
 */

struct ugold_softc;

struct ugold_softc {
	struct uhidev		 sc_hdev;
	struct usbd_device	*sc_udev;

	int			 sc_num_sensors;
	int			 sc_type;

	char			 sc_model[16 + 1];
	unsigned int		 sc_model_len;

	struct ksensor		 sc_sensor[UGOLD_MAX_SENSORS];
	struct ksensordev	 sc_sensordev;
	struct sensor_task	*sc_sensortask;

	void		(*sc_intr)(struct ugold_softc *, uint8_t *, u_int);
};

const struct usb_devno ugold_devs[] = {
	{ USB_VENDOR_MICRODIA, USB_PRODUCT_MICRODIA_TEMPER },
	{ USB_VENDOR_MICRODIA, USB_PRODUCT_MICRODIA_TEMPERHUM },
	{ USB_VENDOR_PCSENSORS, USB_PRODUCT_PCSENSORS_TEMPER },
	{ USB_VENDOR_RDING, USB_PRODUCT_RDING_TEMPER },
	{ USB_VENDOR_WCH2, USB_PRODUCT_WCH2_TEMPER },
};

int 	ugold_match(struct device *, void *, void *);
void	ugold_attach(struct device *, struct device *, void *);
int 	ugold_detach(struct device *, int);

void	ugold_setup_sensors(struct ugold_softc *);
void	ugold_intr(struct uhidev *, void *, u_int);
void	ugold_ds75_intr(struct ugold_softc *, uint8_t *, u_int);
void	ugold_si700x_intr(struct ugold_softc *, uint8_t *, u_int);
void	ugold_refresh(void *);

int	ugold_issue_cmd(struct ugold_softc *, uint8_t *, int);

struct cfdriver ugold_cd = {
	NULL, "ugold", DV_DULL
};

const struct cfattach ugold_ca = {
	sizeof(struct ugold_softc), ugold_match, ugold_attach, ugold_detach,
};

int
ugold_match(struct device *parent, void *match, void *aux)
{
	struct uhidev_attach_arg *uha = aux;
	int size;
	void *desc;

	if (UHIDEV_CLAIM_MULTIPLE_REPORTID(uha))
		return (UMATCH_NONE);

	if (usb_lookup(ugold_devs, uha->uaa->vendor, uha->uaa->product) == NULL)
		return (UMATCH_NONE);

	/*
	 * XXX Only match the sensor interface.
	 *
	 * Does it make sense to attach various uhidev(4) to these
	 * non-standard HID devices?
	 */
	uhidev_get_report_desc(uha->parent, &desc, &size);
	if (hid_is_collection(desc, size, uha->reportid,
	    HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_KEYBOARD)))
		return (UMATCH_NONE);

	return (UMATCH_VENDOR_PRODUCT);

}

void
ugold_attach(struct device *parent, struct device *self, void *aux)
{
	struct ugold_softc *sc = (struct ugold_softc *)self;
	struct uhidev_attach_arg *uha = aux;
	int size, repid;
	void *desc;

	sc->sc_udev = uha->parent->sc_udev;
	sc->sc_hdev.sc_parent = uha->parent;
	sc->sc_hdev.sc_report_id = uha->reportid;
	sc->sc_hdev.sc_intr = ugold_intr;
	switch (uha->uaa->product) {
	case USB_PRODUCT_MICRODIA_TEMPER:
		sc->sc_intr = ugold_ds75_intr;
		break;
	case USB_PRODUCT_MICRODIA_TEMPERHUM:
	case USB_PRODUCT_PCSENSORS_TEMPER:
	case USB_PRODUCT_RDING_TEMPER:
	case USB_PRODUCT_WCH2_TEMPER:
		sc->sc_intr = ugold_si700x_intr;
		break;
	default:
		printf(", unknown product\n");
		return;
	}

	uhidev_get_report_desc(uha->parent, &desc, &size);
	repid = uha->reportid;
	sc->sc_hdev.sc_isize = hid_report_size(desc, size, hid_input, repid);
	sc->sc_hdev.sc_osize = hid_report_size(desc, size, hid_output, repid);
	sc->sc_hdev.sc_fsize = hid_report_size(desc, size, hid_feature, repid);

	if (uhidev_open(&sc->sc_hdev)) {
		printf(", unable to open interrupt pipe\n");
		return;
	}

	strlcpy(sc->sc_sensordev.xname, sc->sc_hdev.sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));

	/* 0.166Hz */
	sc->sc_sensortask = sensor_task_register(sc, ugold_refresh, 6);
	if (sc->sc_sensortask == NULL) {
		printf(", unable to register update task\n");
		return;
	}
	printf("\n");

	/* speed up sensor identification */
	ugold_refresh(sc);

	sensordev_install(&sc->sc_sensordev);
}

int
ugold_detach(struct device *self, int flags)
{
	struct ugold_softc *sc = (struct ugold_softc *)self;
	int i;

	if (sc->sc_sensortask != NULL) {
		sensor_task_unregister(sc->sc_sensortask);
		sensordev_deinstall(&sc->sc_sensordev);
	}

	if (sc->sc_type != UGOLD_TYPE_INVALID) {
		for (i = 0; i < sc->sc_num_sensors; i++)
			sensor_detach(&sc->sc_sensordev, &sc->sc_sensor[i]);
	}

	if (sc->sc_hdev.sc_state & UHIDEV_OPEN)
		uhidev_close(&sc->sc_hdev);

	return (0);
}

void
ugold_setup_sensors(struct ugold_softc *sc)
{
	int i;

	switch (sc->sc_type) {
	default:
		return;
	case UGOLD_TYPE_SI7005:
	case UGOLD_TYPE_SI7006:
	case UGOLD_TYPE_SHT1X:
	case UGOLD_TYPE_TEMPERX:
		/* 1 temperature and 1 humidity sensor */
		sc->sc_sensor[UGOLD_INNER].type = SENSOR_TEMP;
		strlcpy(sc->sc_sensor[UGOLD_INNER].desc, "inner",
		    sizeof(sc->sc_sensor[UGOLD_INNER].desc));
		sc->sc_sensor[UGOLD_HUM].type = SENSOR_HUMIDITY;
		strlcpy(sc->sc_sensor[UGOLD_HUM].desc, "RH",
		    sizeof(sc->sc_sensor[UGOLD_HUM].desc));
		break;
	case UGOLD_TYPE_GOLD:
	case UGOLD_TYPE_DS75:
		/* up to 2 temperature sensors */
		sc->sc_sensor[UGOLD_INNER].type = SENSOR_TEMP;
		strlcpy(sc->sc_sensor[UGOLD_INNER].desc, "inner",
		    sizeof(sc->sc_sensor[UGOLD_INNER].desc));
		sc->sc_sensor[UGOLD_OUTER].type = SENSOR_TEMP;
		strlcpy(sc->sc_sensor[UGOLD_OUTER].desc, "outer",
		    sizeof(sc->sc_sensor[UGOLD_OUTER].desc));
		break;
	}
	for (i = 0; i < sc->sc_num_sensors; i++) {
		sc->sc_sensor[i].flags |= SENSOR_FINVALID;
		sensor_attach(&sc->sc_sensordev, &sc->sc_sensor[i]);
	}
}

static void
strnvis(char *dst, const char *src, size_t siz)
{
	char *start, *end;
	int c;

	for (start = dst, end = start + siz - 1; (c = *src) && dst < end; ) {
		if (c >= 0x20 && c <= 0x7f) {
			if (c == '\\') {
				/* need space for the extra '\\' */
				if (dst + 2 > end)
					break;
				*dst++ = '\\';
			}
			*dst++ = c;
		} else {
			if (dst + 4 > end)
				break;
			*dst++ = '\\';
			*dst++ = ((u_char)c >> 6 & 07) + '0';
			*dst++ = ((u_char)c >> 3 & 07) + '0';
			*dst++ = ((u_char)c & 07) + '0';
		}
		src++;
	}
	if (siz > 0)
		*dst = '\0';
}

static int
ugold_ds75_temp(uint8_t msb, uint8_t lsb)
{
	/* DS75 12bit precision mode: 0.0625 degrees Celsius ticks */
	return (((msb * 100) + ((lsb >> 4) * 25 / 4)) * 10000) + 273150000;
}

static void
ugold_ds75_type(struct ugold_softc *sc)
{
	char model[4 * sizeof(sc->sc_model) + 1];

	strnvis(model, sc->sc_model, sizeof model);

	if (memcmp(sc->sc_model, "TEMPer1F", 8) == 0 ||
	    memcmp(sc->sc_model, "TEMPer2F", 8) == 0 ||
	    memcmp(sc->sc_model, "TEMPerF1", 8) == 0) {
		sc->sc_type = UGOLD_TYPE_DS75;
		ugold_setup_sensors(sc);
		printf("%s: \"%s\", %d sensor%s"
		       " type ds75/12bit (temperature)\n",
		    sc->sc_hdev.sc_dev.dv_xname, model, sc->sc_num_sensors,
		    (sc->sc_num_sensors == 1) ? "" : "s");
		ugold_refresh(sc);
		return;
	}

	printf("%s: unknown model \"%s\"\n",
	    sc->sc_hdev.sc_dev.dv_xname, model);
	sc->sc_num_sensors = 0;
	sc->sc_type = UGOLD_TYPE_INVALID;
}

void
ugold_ds75_intr(struct ugold_softc *sc, uint8_t *buf, u_int len)
{
	int temp;

	switch (buf[0]) {
	case UGOLD_CMD_INIT:
		if (sc->sc_num_sensors != 0)
			break;
		sc->sc_num_sensors = imin(buf[1], UGOLD_MAX_SENSORS) /* XXX */;
		ugold_refresh(sc);
		break;
	case UGOLD_CMD_DATA:
		switch (buf[1]) {
		case 4:
			temp = ugold_ds75_temp(buf[4], buf[5]);
			sc->sc_sensor[UGOLD_OUTER].value = temp;
			sc->sc_sensor[UGOLD_OUTER].flags &= ~SENSOR_FINVALID;
			/* FALLTHROUGH */
		case 2:
			temp = ugold_ds75_temp(buf[2], buf[3]);
			sc->sc_sensor[UGOLD_INNER].value = temp;
			sc->sc_sensor[UGOLD_INNER].flags &= ~SENSOR_FINVALID;
			break;
		default:
#ifdef UGOLD_DEBUG
			printf("%s: invalid data length (%d bytes)\n",
				sc->sc_hdev.sc_dev.dv_xname, buf[1]);
#endif
			break;
		}
		break;
	default:
		ugold_ds75_type(sc);
		break;
	}
}

static int
ugold_si700x_temp(int type, uint8_t msb, uint8_t lsb)
{
	int temp = msb * 256 + lsb;

	switch (type) { /* convert to mdegC */
	case UGOLD_TYPE_SI7005: /* 14bit 32 codes per degC 0x0000 = -50 degC */
		temp = (((temp & 0x3fff) * 1000) / 32) - 50000;
		break;
	case UGOLD_TYPE_SI7006: /* 14bit and status bit */
		temp = (((temp & ~3) * 21965) / 8192) - 46850;
		break;
	case UGOLD_TYPE_SHT1X:
		temp = (temp * 1000) / 256;
		break;
	case UGOLD_TYPE_GOLD:
	case UGOLD_TYPE_TEMPERX:
		/* temp = temp / 100 to get degC, then * 1000 to get mdegC */
		temp = temp * 10;
		break;
	default:
		temp = 0;
	}

	return temp;
}

static int
ugold_si700x_rhum(int type, uint8_t msb, uint8_t lsb, int temp)
{
	int rhum = msb * 256 + lsb;

	switch (type) { /* convert to m%RH */
	case UGOLD_TYPE_SI7005: /* 12bit 16 codes per %RH 0x0000 = -24 %RH */
		rhum = (((rhum & 0x0fff) * 1000) / 16) - 24000;
#if 0		/* todo: linearization and temperature compensation */
		rhum -= -0.00393 * rhum * rhum + 0.4008 * rhum - 4.7844;
		rhum += (temp - 30) * (0.00237 * rhum + 0.1973);
#endif
		break;
	case UGOLD_TYPE_SI7006: /* 14bit and status bit */
		rhum = (((rhum & ~3) * 15625) / 8192) - 6000;
		break;
	case UGOLD_TYPE_SHT1X: /* 16 bit */
		rhum = rhum * 32;
		break;
	case UGOLD_TYPE_TEMPERX:
		rhum = rhum * 10;
		break;
	default:
		rhum = 0;
	}

	/* limit the humidity to valid values */
	if (rhum < 0)
		rhum = 0;
	else if (rhum > 100000)
		rhum = 100000;
	return rhum;
}

static void
ugold_si700x_type(struct ugold_softc *sc)
{
	char model[4 * sizeof(sc->sc_model) + 1];
	const char *descr;
	int nsensors = 0;

	strnvis(model, sc->sc_model, sizeof model);

	/* TEMPerHUM prefix */
	if (sc->sc_model_len >= 9 &&
	    memcmp(sc->sc_model, "TEMPerHum", 9) == 0) {
		if (memcmp(sc->sc_model + 9, "M12V1.0", 16 - 9) == 0) {
			sc->sc_type = UGOLD_TYPE_SI7005;
			descr = "si7005 (temperature and humidity)";
			goto identified;
		}
		if (memcmp(sc->sc_model + 9, "M12V1.2", 16 - 9) == 0) {
			sc->sc_type = UGOLD_TYPE_SI7006;
			descr = "si7006 (temperature and humidity)";
			goto identified;
		}
	}
	if (sc->sc_model_len >= 9 &&
	    memcmp(sc->sc_model, "TEMPerHUM", 9) == 0) {
		if (memcmp(sc->sc_model + 9, "_V3.9  ", 16 - 9) == 0 ||
		    memcmp(sc->sc_model + 9, "_V4.0  ", 16 - 9) == 0 ||
		    memcmp(sc->sc_model + 9, "_V4.1\0\0", 16 - 9) == 0) {
			sc->sc_type = UGOLD_TYPE_TEMPERX;
			descr = "temperx (temperature and humidity)";
			goto identified;
		}
	}

	/* TEMPerX prefix */
	if (sc->sc_model_len >= 8 &&
	    memcmp(sc->sc_model, "TEMPerX_", 8) == 0) {
		if (memcmp(sc->sc_model + 8, "V3.1    ", 16 - 8) == 0 ||
		    memcmp(sc->sc_model + 8, "V3.3    ", 16 - 8) == 0) {
			sc->sc_type = UGOLD_TYPE_TEMPERX;
			descr = "temperx (temperature and humidity)";
			goto identified;
		}
	}

	/* TEMPer1F or TEMPer2_ prefixes */
	if (sc->sc_model_len >= 16 &&
	    memcmp(sc->sc_model, "TEMPer1F_H1V1.5F", 16) == 0) {
		sc->sc_type = UGOLD_TYPE_SHT1X;
		descr = "sht1x (temperature and humidity)";
		goto identified;
	}
	if (sc->sc_model_len >= 16 &&
	    (memcmp(sc->sc_model, "TEMPer1F_V4.1\0\0\0", 16) == 0 ||
	     memcmp(sc->sc_model, "TEMPer2_V4.1\0\0\0\0", 16) == 0)) {
		sc->sc_type = UGOLD_TYPE_GOLD;
		/*
		 * TEMPer1F devices lack the internal sensor, but will never
		 * report data for it, so it will never get marked as valid.
		 * We thus keep the value of sc_num_sensors unchanged at 2,
		 * and make sure we will only report one single sensor below.
		 */
		if (sc->sc_model[6] == '1')
			nsensors = 1;
		descr = "gold (temperature only)";
		goto identified;
	}

	/* TEMPerGold prefix */
	if (sc->sc_model_len >= 11 &&
	    memcmp(sc->sc_model, "TEMPerGold_", 11) == 0) {
		/*
		 * All V3.something models ought to work, but better be
		 * safe than sorry, and TEMPerHum models have been known
		 * to use slightly different sensors between models.
		 */
		if (memcmp(sc->sc_model + 11, "V3.1 ", 16 - 11) == 0 ||
		    memcmp(sc->sc_model + 11, "V3.4 ", 16 - 11) == 0 ||
		    memcmp(sc->sc_model + 11, "V3.5 ", 16 - 11) == 0) {
			sc->sc_type = UGOLD_TYPE_GOLD;
			sc->sc_num_sensors = 1;
			descr = "gold (temperature only)";
			goto identified;
		}
	}
	
	printf("%s: unknown model \"%s\"\n",
	    sc->sc_hdev.sc_dev.dv_xname, model);
	sc->sc_num_sensors = 0;
	sc->sc_type = UGOLD_TYPE_INVALID;
	return;

 identified:
	ugold_setup_sensors(sc);
	if (nsensors == 0)
		nsensors = sc->sc_num_sensors;
	printf("%s: \"%s\", %d sensor%s type %s\n", sc->sc_hdev.sc_dev.dv_xname,
	    model, nsensors, (nsensors == 1) ? "" : "s", descr);
	ugold_refresh(sc);
}

void
ugold_si700x_intr(struct ugold_softc *sc, uint8_t *buf, u_int len)
{
	int temp, sensor, rhum;

	switch (buf[0]) {
	case UGOLD_CMD_INIT:
		if (sc->sc_num_sensors != 0)
			break;
		/* XXX some devices report 0x04 here */
		sc->sc_num_sensors = imin(buf[1], UGOLD_MAX_SENSORS);
		ugold_refresh(sc);
		break;
	case UGOLD_CMD_DATA:
		if (sc->sc_type == UGOLD_TYPE_GOLD) {
			if (buf[1] == 0x80)
				sensor = UGOLD_INNER;
			else if (buf[1] == 0x01)
				sensor = UGOLD_OUTER;
			else
				sensor = -1;
		} else {
			if (buf[1] == 0x04 || buf[1] == 0x20 ||
			    buf[1] == 0x40 || buf[1] == 0x80)
				sensor = UGOLD_INNER;
			else
				sensor = -1;
		}
		if (sensor < 0) {
			/* unexpected data, ignore */
#ifdef UGOLD_DEBUG
			printf("%s: unexpected sensor id %02x\n",
			    sc->sc_hdev.sc_dev.dv_xname, buf[1]);
#endif
			break;
		}

		temp = ugold_si700x_temp(sc->sc_type, buf[2], buf[3]);
		sc->sc_sensor[sensor].value = (temp * 1000) + 273150000;
		/*
		 * TEMPer1F and TEMPer2 report 200C when the sensor probe is
		 * missing or not plugged correctly.
		 */
		if (sc->sc_type == UGOLD_TYPE_GOLD && temp == 200000)
			sc->sc_sensor[sensor].flags |= SENSOR_FINVALID;
		else
			sc->sc_sensor[sensor].flags &= ~SENSOR_FINVALID;

		if (sc->sc_type != UGOLD_TYPE_GOLD) {
			rhum = ugold_si700x_rhum(sc->sc_type, buf[4], buf[5], temp);
			sc->sc_sensor[UGOLD_HUM].value = rhum;
			sc->sc_sensor[UGOLD_HUM].flags &= ~SENSOR_FINVALID;
		}
		break;
	default:
		ugold_si700x_type(sc);
		break;
	}
}

void
ugold_intr(struct uhidev *addr, void *ibuf, u_int len)
{
	struct ugold_softc *sc = (struct ugold_softc *)addr;
	uint8_t *buf = ibuf;
	unsigned long chunk;

#ifdef UGOLD_DEBUG
	{
		printf("%s: %u bytes\n", sc->sc_hdev.sc_dev.dv_xname, len);
		u_int i;
		for (i = 0; i < len; i++) {
			if (i != 0 && (i % 8) == 0)
				printf("\n");
			printf("%02x ", buf[i]);
		}
		printf("\n");
	}
#endif

	switch (buf[0]) {
	case UGOLD_CMD_INIT:
	case UGOLD_CMD_DATA:
		(*sc->sc_intr)(sc, buf, len);
		break;
	default:
		if (!sc->sc_type) {
			/*
			 * During initialization, some devices need a bit
			 * more time to submit their identification string.
			 */
			if (len == sc->sc_model_len &&
			    !memcmp(sc->sc_model, buf, len)) {
#ifdef UGOLD_DEBUG
				printf("%s: duplicate string component\n",
				    sc->sc_hdev.sc_dev.dv_xname);
#endif
				break;
			}
			/*
			 * Exact sensor type is not known yet, type command
			 * returns arbitrary string.
			 */
			chunk = ulmin(len,
			    sizeof(sc->sc_model) - 1 - sc->sc_model_len);
			if (chunk != 0) {
				memcpy(sc->sc_model + sc->sc_model_len, buf,
				    chunk);
				sc->sc_model_len += chunk;
			}
			if (sc->sc_model_len > 8) {
				/* should have enough data now */
				(*sc->sc_intr)(sc, buf, len);
			}
			break;
		}
		printf("%s: unknown command 0x%02x\n",
		    sc->sc_hdev.sc_dev.dv_xname, buf[0]);
		break;
	}
}

void
ugold_refresh(void *arg)
{
	struct ugold_softc *sc = arg;
	int i;

	/*
	 * Don't waste time talking to the device if we don't understand
	 * its language.
	 */
	if (sc->sc_type == UGOLD_TYPE_INVALID)
		return;

	if (!sc->sc_num_sensors) {
		ugold_issue_cmd(sc, cmd_init, sizeof(cmd_init));
		return;
	}
	if (!sc->sc_type) {
		ugold_issue_cmd(sc, cmd_type, sizeof(cmd_type));
		return;
	}

	if (ugold_issue_cmd(sc, cmd_data, sizeof(cmd_data))) {
		for (i = 0; i < sc->sc_num_sensors; i++)
			sc->sc_sensor[i].flags |= SENSOR_FINVALID;
	}
}

int
ugold_issue_cmd(struct ugold_softc *sc, uint8_t *cmd, int len)
{
	int actlen;

	actlen = uhidev_set_report_async(sc->sc_hdev.sc_parent,
	    UHID_OUTPUT_REPORT, sc->sc_hdev.sc_report_id, cmd, len);
	return (actlen != len);
}
