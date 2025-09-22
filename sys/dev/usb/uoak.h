/*	$OpenBSD: uoak.h,v 1.5 2022/04/09 20:09:03 naddy Exp $   */

/*
 * Copyright (c) 2012 Yojiro UO <yuo@nui.org>
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

/* TORADEX OAK series sensors */
/* http://developer.toradex.com/files/toradex-dev/uploads/media/Oak/Oak_ProgrammingGuide.pdf */

/* feature request direction */
#define OAK_SET			0x0
#define OAK_GET			0x1

/* specification */
#define OAK_V_MAXSENSORS	8

/* OAK sensor command */
/* 1 byte commands */
#define OAK_CMD_REPORTMODE	0x0000
#define  OAK_REPORTMODE_AFTERSAMPLING	0x0	/* default */
#define  OAK_REPORTMODE_AFTERCHANGE	0x1
#define  OAK_REPORTMODE_FIXEDRATE	0x2
#define OAK_CMD_LEDMODE		0x0001
#define OAK_CMD_SENSORSETTING	0x0002
/* RH */
#define  OAK_RH_SENSOR_HEATER_MASK	(0x1 << 3)
#define  OAK_RH_SENSOR_RES_MASK		(0x1 << 0)
#define  OAK_RH_SENSOR_HEATER_OFF	0x0	/* default */
#define  OAK_RH_SENSOR_HEATER_ON	0x1
#define  OAK_RH_SENSOR_HIGHRES		0x0	/* default */
#define  OAK_RH_SENSOR_LOWRES		0x1
/* LUX */
#define  OAK_LUX_SENSOR_GAIN_MASK	(0x1 << 4)
#define  OAK_LUX_SENSOR_LOWGAIN		0x0	/* default */
#define  OAK_LUX_SENSOR_HIGHGAIN	0x1
#define  OAK_LUX_SENSOR_INTTIME_MASK	0x3
#define  OAK_LUX_SENSOR_INTTIME_13_7ms	0x0	/* 13.7ms */
#define  OAK_LUX_SENSOR_INTTIME_101ms	0x1	/* 101 ms */
#define  OAK_LUX_SENSOR_INTTIME_402ms	0x2	/* 402 ms (default) */
/* 10V */
#define  OAK_V_SENSOR_INPUTMODEMASK	(0x1 << 0)
#define  OAK_V_SENSOR_SINGLEENDED	0x0	/* default */
#define  OAK_V_SENSOR_DIFFERENTIAL	0x1

/* 2 bytes commands */
#define OAK_CMD_REPORTRATE	0x0000
#define OAK_CMD_SAMPLERATE	0x0001

/* 21 bytes (0x15) commands */
#define OAK_CMD_DEVNAME		0x0000
#define OAK_CMD_CHANNAME0	0x0001
#define OAK_CMD_CHANNAME1	0x0002
#define OAK_CMD_CHANNAME2	0x0003
#define OAK_CMD_CHANNAME3	0x0004
#define OAK_CMD_CHANNAME4	0x0005
#define OAK_CMD_CHANNAME5	0x0006
#define OAK_CMD_CHANNAME6	0x0007
#define OAK_CMD_CHANNAME7	0x0008
#define OAK_CMD_CHANNAME8	0x0009

/* OAK LED command */
#define OAK_LED_OFF		0x0
#define OAK_LED_ON		0x1
#define OAK_LED_BLINK_SLOW	0x2
#define OAK_LED_BLINK_FAST	0x3
#define OAK_LED_BLINK_PULSES	0x4

/* OAK config storage targets */
enum uoak_target {
	OAK_TARGET_RAM,
	OAK_TARGET_FLASH,
	OAK_TARGET_CPU,
	OAK_TARGET_SENSOR,
	OAK_TARGET_OTHER,
	OAK_TARGET_MAXTYPES
};

#define OAK_RH_TARGET_MAX	2
#define OAK_V_TARGET_MAX	2
#define OAK_LUX_TARGET_MAX	2

struct uoak_rcmd {
	uint8_t dir;
	uint8_t target;
	uint8_t datasize;
	uint16_t cmd;
	uint8_t val[26];
} __packed;

struct uoak_config {
	char devname[24];
	int  report_mode;
	int  report_rate;
	int  sample_rate;
};

struct uoak_methods {
	void (*dev_print)(void *parent, enum uoak_target target);
	void (*dev_setting)(void *parent, enum uoak_target target);
};

struct uoak_softc {
	struct uhidev		*sc_hdev;
	void			*sc_parent;
	struct ksensordev	*sc_sensordev;
	struct usbd_device	*sc_udev;
	uint16_t		 sc_flag;
	struct usb_device_info	 sc_udi;

	/* uhidev parameters */
	size_t			 sc_flen;	/* feature report length */
	size_t			 sc_ilen;	/* input report length */
	size_t			 sc_olen;	/* output report length */
	uint8_t			*sc_ibuf;	

	/* buffers */
	struct uoak_rcmd	 sc_rcmd;
	uint8_t			 sc_buf[32];

	/* configurations */
	struct uoak_config	 sc_config[OAK_TARGET_MAXTYPES];

	/* device specific methods */
	const struct uoak_methods *sc_methods;
};


struct uoak_sensor {
	struct ksensor		 avg;
	struct ksensor		 max;
	struct ksensor		 min;
	int64_t vavg, vmax, vmin;
	unsigned int count;
};


int uoak_check_device_ready(struct uoak_softc *);
int uoak_set_cmd(struct uoak_softc *);
int uoak_get_cmd(struct uoak_softc *);

int uoak_get_device_name(struct uoak_softc *, enum uoak_target);
int uoak_get_report_mode(struct uoak_softc *, enum uoak_target);
int uoak_get_report_rate(struct uoak_softc *, enum uoak_target);
int uoak_get_sample_rate(struct uoak_softc *, enum uoak_target);
int uoak_set_sample_rate(struct uoak_softc *, enum uoak_target, int);

int uoak_led_ctrl(struct uoak_softc *, enum uoak_target, uint8_t);
int uoak_led_status(struct uoak_softc *, enum uoak_target, uint8_t *);

void uoak_get_devinfo(struct uoak_softc *);
void uoak_get_setting(struct uoak_softc *, enum uoak_target);
void uoak_print_devinfo(struct uoak_softc *);
void uoak_print_setting(struct uoak_softc *, enum uoak_target);

void uoak_sensor_attach(struct uoak_softc *, struct uoak_sensor *,
  enum sensor_type);
void uoak_sensor_detach(struct uoak_softc *, struct uoak_sensor *);
void uoak_sensor_update(struct uoak_sensor *, int);
void uoak_sensor_refresh(struct uoak_sensor *, int, int);
