/*	$OpenBSD: asmc.c,v 1.5 2022/10/20 16:08:13 kn Exp $	*/
/*
 * Copyright (c) 2015 Joerg Jung <jung@openbsd.org>
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

/*
 * Driver for Apple's System Management Controller (SMC) an H8S/2117 chip
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/kernel.h>
#include <sys/rwlock.h>
#include <sys/task.h>
#include <sys/sensors.h>

#include <machine/bus.h>

#include <dev/acpi/acpivar.h>
#include <dev/acpi/acpidev.h>
#include <dev/acpi/amltypes.h>
#include <dev/acpi/dsdt.h>

#include <dev/wscons/wsconsio.h>

#define ASMC_DATA	0x00	/* SMC data port offset */
#define ASMC_COMMAND	0x04	/* SMC command port offset */
#define ASMC_STATUS	0x1e	/* SMC status port offset */
#define ASMC_INTERRUPT	0x1f	/* SMC interrupt port offset */

#define ASMC_READ	0x10	/* SMC read command */
#define ASMC_WRITE	0x11	/* SMC write command */
#define ASMC_INFO	0x13	/* SMC info/type command */

#define ASMC_OBF	0x01	/* Output buffer full */
#define ASMC_IBF	0x02	/* Input buffer full */
#define ASMC_ACCEPT	0x04

#define ASMC_RETRY	3
#define ASMC_MAXLEN	32	/* SMC maximum data size len */
#define ASMC_NOTFOUND	0x84	/* SMC status key not found */

#define ASMC_MAXTEMP	101	/* known asmc_prods temperature sensor keys */
#define ASMC_MAXFAN	10	/* fan keys with digits 0-9 */
#define ASMC_MAXLIGHT	2	/* left and right light sensor */
#define ASMC_MAXMOTION	3	/* x y z axis motion sensors */

#define ASMC_DELAY_LOOP	200	/* ASMC_DELAY_LOOP * 10us = 2ms */

struct asmc_prod {
	const char	*pr_name;
	uint8_t		 pr_light;
	const char	*pr_temp[ASMC_MAXTEMP];
};

struct asmc_softc {
	struct device		 sc_dev;

	struct acpi_softc       *sc_acpi;
	struct aml_node         *sc_devnode;

	bus_space_tag_t		 sc_iot;
	bus_space_handle_t	 sc_ioh;

	const struct asmc_prod	*sc_prod;
	uint8_t			 sc_nfans;	/* number of fans */
	uint8_t			 sc_lightlen;	/* light data len */
	uint8_t			 sc_backlight;	/* keyboard backlight value */

	struct rwlock		 sc_lock;
	struct task		 sc_task_backlight;

	struct ksensor		 sc_sensor_temp[ASMC_MAXTEMP];
	struct ksensor		 sc_sensor_fan[ASMC_MAXFAN];
	struct ksensor		 sc_sensor_light[ASMC_MAXLIGHT];
	struct ksensor		 sc_sensor_motion[ASMC_MAXMOTION];
	struct ksensordev	 sc_sensor_dev;
	struct sensor_task	*sc_sensor_task;
};

int	asmc_try(struct asmc_softc *, int, const char *, uint8_t *, uint8_t);
void	asmc_init(struct asmc_softc *);
void	asmc_update(void *);

int	asmc_match(struct device *, void *, void *);
void	asmc_attach(struct device *, struct device *, void *);
int 	asmc_detach(struct device *, int);
int	asmc_activate(struct device *, int);

/* wskbd hook functions */
void	asmc_backlight(void *);
int	asmc_get_backlight(struct wskbd_backlight *);
int	asmc_set_backlight(struct wskbd_backlight *);
extern int (*wskbd_get_backlight)(struct wskbd_backlight *);
extern int (*wskbd_set_backlight)(struct wskbd_backlight *);

const struct cfattach asmc_ca = {
	sizeof(struct asmc_softc), asmc_match, asmc_attach, NULL, asmc_activate
};

struct cfdriver asmc_cd = {
	NULL, "asmc", DV_DULL
};

const char *asmc_hids[] = {
	"APP0001", NULL
};

static const struct asmc_prod asmc_prods[] = {
	{ "MacBookAir", 1, {
		"TB0T", "TB1S", "TB1T", "TB2S", "TB2T", "TBXT", "TC0C", "TC0D",
		"TC0E", "TC0F", "TC0P", "TC1C", "TC1E", "TC2C", "TCFP", "TCGC",
		"TCHP", "TCMX", "TCSA", "TCXC", "TCZ3", "TCZ4", "TCZ5", "TG0E",
		"TG1E", "TG2E", "TGZ3", "TGZ4", "TGZ5", "TH0A", "TH0B", "TH0V",
		"TH0a", "TH0b", "THSP", "TM0P", "TN0D", "TPCD", "TS2P", "TTF0",
		"TV0P", "TVFP", "TW0P", "Ta0P", "Th0H", "Th0P", "Th1H", "Tm0P",
		"Tm1P", "Tp0P", "Tp1P", "TpFP", "Ts0P", "Ts0S", NULL }
	},
	{ "MacBookPro", 1, {
		"TA0P", "TA1P", "TALP", "TB0T", "TB1T", "TB2T", "TB3T", "TBXT",
		"TC0C", "TC0D", "TC0E", "TC0F", "TC0P", "TC1C", "TC2C", "TC3C",
		"TC4C", "TCGC", "TCSA", "TCXC", "TG0D", "TG0F", "TG0H", "TG0P",
		"TG0T", "TG1D", "TG1F", "TG1H", "TG1d", "TH0A", "TH0B", "TH0F",
		"TH0R", "TH0V", "TH0a", "TH0b", "TH0c", "TH0x", "THSP", "TM0P",
		"TM0S", "TMCD", "TN0D", "TN0P", "TN0S", "TN1D", "TN1F", "TN1G",
		"TN1S", "TP0P", "TPCD", "TTF0", "TW0P", "Ta0P", "TaSP", "Th0H",
		"Th1H", "Th2H", "Tm0P", "Ts0P", "Ts0S", "Ts1P", "Ts1S", NULL }
	},
	{ "MacBook", 0, {
		"TB0T", "TB1T", "TB2T", "TB3T", "TC0D", "TC0P", "TM0P", "TN0D",
		"TN0P", "TN1P", "TTF0", "TW0P", "Th0H", "Th0S", "Th1H", "ThFH",
		"Ts0P", "Ts0S", NULL }
	},
	{ "MacPro", 0, {
		"TA0P", "TC0C", "TC0D", "TC0P", "TC1C", "TC1D", "TC2C", "TC2D",
		"TC3C", "TC3D", "TCAC", "TCAD", "TCAG", "TCAH", "TCAS", "TCBC",
		"TCBD", "TCBG", "TCBH", "TCBS", "TH0P", "TH1F", "TH1P", "TH1V",
		"TH2F", "TH2P", "TH2V", "TH3F", "TH3P", "TH3V", "TH4F", "TH4P",
		"TH4V", "THPS", "THTG", "TM0P", "TM0S", "TM1P", "TM1S", "TM2P",
		"TM2S", "TM2V", "TM3P", "TM3S", "TM3V", "TM4P", "TM5P", "TM6P",
		"TM6V", "TM7P", "TM7V", "TM8P", "TM8S", "TM8V", "TM9P", "TM9S",
		"TM9V", "TMA1", "TMA2", "TMA3", "TMA4", "TMAP", "TMAS", "TMB1",
		"TMB2", "TMB3", "TMB4", "TMBS", "TMHS", "TMLS", "TMPS", "TMPV",
		"TMTG", "TN0C", "TN0D", "TN0H", "TNTG", "TS0C", "Te1F", "Te1P",
		"Te1S", "Te2F", "Te2S", "Te3F", "Te3S", "Te4F", "Te4S", "Te5F",
		"Te5S", "TeGG", "TeGP", "TeRG", "TeRP", "TeRV", "Tp0C", "Tp1C",
		"TpPS", "TpTG", "Tv0S", "Tv1S", NULL }
	},
	{ "MacMini", 0, {
		"TC0D", "TC0H", "TC0P", "TH0P", "TN0D", "TN0P", "TN1P", "TW0P",
		NULL }
	},
	{ "iMac", 0, {
		"TA0P", "TC0D", "TC0H", "TC0P", "TG0D", "TG0H", "TG0P", "TH0P",
		"TL0P", "TN0D", "TN0H", "TN0P", "TO0P", "TW0P", "Tm0P", "Tp0C",
		"Tp0P", NULL }
	},
	{ NULL, 0, { NULL } }
};

static const char *asmc_temp_desc[][2] = {
	{ "TA0P", "ambient" }, { "TA0P", "hdd bay 1" },
	{ "TA0S", "pci slot 1 pos 1" }, { "TA1P", "ambient 2" },
	{ "TA1S", "pci slot 1 pos 2" }, { "TA2S", "pci slot 2 pos 1" },
	{ "TA3S", "pci slot 2 pos 2" },
	{ "TB0T", "enclosure bottom" }, { "TB1T", "enclosure bottom 2" },
	{ "TB2T", "enclosure bottom 3" }, { "TB3T", "enclosure bottom 4" },
	{ "TC0D", "cpu0 die core" }, { "TC0H", "cpu0 heatsink" },
	{ "TC0P", "cpu0 proximity" },
	{ "TC1D", "cpu1" }, { "TC2D", "cpu2" }, { "TC3D", "cpu3" },
	{ "TCAH", "cpu0" }, { "TCBH", "cpu1" }, { "TCCH", "cpu2" },
	{ "TCDH", "cpu3" },
	{ "TG0D", "gpu0 diode" }, { "TG0H", "gpu0 heatsink" },
	{ "TG0P", "gpu0 proximity" },
	{ "TG1H", "gpu heatsink 2" },
	{ "TH0P", "hdd bay 1" }, { "TH1P", "hdd bay 2" },
	{ "TH2P", "hdd bay 3" }, { "TH3P", "hdd bay 4" },
	{ "TL0P", "lcd proximity"},
	{ "TM0P", "mem bank a1" }, { "TM0S", "mem module a1" },
	{ "TM1P", "mem bank a2" }, { "TM1S", "mem module a2" },
	{ "TM2P", "mem bank a3" }, { "TM2S", "mem module a3" },
	{ "TM3P", "mem bank a4" }, { "TM3S", "mem module a4" },
	{ "TM4P", "mem bank a5" }, { "TM4S", "mem module a5" },
	{ "TM5P", "mem bank a6" }, { "TM5S", "mem module a6" },
	{ "TM6P", "mem bank a7" }, { "TM6S", "mem module a7" },
	{ "TM7P", "mem bank a8" }, { "TM7S", "mem module a8" },
	{ "TM8P", "mem bank b1" }, { "TM8S", "mem module b1" },
	{ "TM9P", "mem bank b2" }, { "TM9S", "mem module b2" },
	{ "TMA1", "ram a1" }, { "TMA2", "ram a2" },
	{ "TMA3", "ram a3" }, { "TMA4", "ram a4" },
	{ "TMB1", "ram b1" }, { "TMB2", "ram b2" },
	{ "TMB3", "ram b3" }, { "TMB4", "ram b4" },
	{ "TMAP", "mem bank b3" }, { "TMAS", "mem module b3" },
	{ "TMBP", "mem bank b4" }, { "TMBS", "mem module b4" },
	{ "TMCP", "mem bank b5" }, { "TMCS", "mem module b5" },
	{ "TMDP", "mem bank b6" }, { "TMDS", "mem module b6" },
	{ "TMEP", "mem bank b7" }, { "TMES", "mem module b7" },
	{ "TMFP", "mem bank b8" }, { "TMFS", "mem module b8" },
	{ "TN0D", "northbridge die core" }, { "TN0H", "northbridge" },
	{ "TN0P", "northbridge proximity" }, { "TN1P", "northbridge 2" },
	{ "TO0P", "optical drive" }, { "TS0C", "expansion slots" },
	{ "TW0P", "wireless airport card" },
	{ "Th0H", "main heatsink a" }, { "Th1H", "main heatsink b" },
	{ "Th2H", "main heatsink c" },
	{ "Tm0P", "memory controller" },
	{ "Tp0C", "power supply 1" }, { "Tp0P", "power supply 1" },
	{ "Tp1C", "power supply 2" }, { "Tp1P", "power supply 2" },
	{ "Tp2P", "power supply 3" }, { "Tp3P", "power supply 4" },
	{ "Tp4P", "power supply 5" }, { "Tp5P", "power supply 6" },
	{ NULL, NULL }
};

static const char *asmc_fan_loc[] = {
	"left lower front", "center lower front", "right lower front",
	"left mid front",   "center mid front",   "right mid front",
	"left upper front", "center upper front", "right upper front",
	"left lower rear",  "center lower rear",  "right lower rear",
	"left mid rear",    "center mid rear",    "right mid rear",
	"left upper rear",  "center upper rear",  "right upper rear"
};

static const char *asmc_light_desc[ASMC_MAXLIGHT] = {
	"left", "right"
};

int
asmc_match(struct device *parent, void *match, void *aux)
{
	struct acpi_attach_args *aa = aux;
	struct cfdata *cf = match;

	if (aa->aaa_naddr < 1)
		return 0;
	return acpi_matchhids(aa, asmc_hids, cf->cf_driver->cd_name);
}

void
asmc_attach(struct device *parent, struct device *self, void *aux)
{
	struct asmc_softc *sc = (struct asmc_softc *)self;
	struct acpi_attach_args *aaa = aux;
	struct aml_value res;
	int64_t sta;
	uint8_t buf[6];
	int i, r;

	if (!hw_vendor || !hw_prod || strncmp(hw_vendor, "Apple", 5))
		return;

	for (i = 0; asmc_prods[i].pr_name && !sc->sc_prod; i++)
		if (!strncasecmp(asmc_prods[i].pr_name, hw_prod,
		    strlen(asmc_prods[i].pr_name)))
			sc->sc_prod = &asmc_prods[i];
	if (!sc->sc_prod)
		return;

	sc->sc_acpi = (struct acpi_softc *)parent;
	sc->sc_devnode = aaa->aaa_node;

	printf(": %s", sc->sc_devnode->name);

	sta = acpi_getsta(sc->sc_acpi, sc->sc_devnode);
	if ((sta & (STA_PRESENT | STA_ENABLED | STA_DEV_OK)) !=
	    (STA_PRESENT | STA_ENABLED | STA_DEV_OK)) {
		printf(": not enabled\n");
		return;
	}

	if (!(aml_evalname(sc->sc_acpi, sc->sc_devnode, "_CID", 0, NULL, &res)))
		printf(" (%s)", res.v_string);

	printf (" addr 0x%llx/0x%llx", aaa->aaa_addr[0], aaa->aaa_size[0]);

	sc->sc_iot = aaa->aaa_bst[0];
	if (bus_space_map(sc->sc_iot, aaa->aaa_addr[0], aaa->aaa_size[0], 0,
	    &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	rw_init(&sc->sc_lock, sc->sc_dev.dv_xname);

	if ((r = asmc_try(sc, ASMC_READ, "REV ", buf, 6))) {
		printf(": revision failed (0x%x)\n", r);
		bus_space_unmap(sc->sc_iot, aaa->aaa_addr[0], aaa->aaa_size[0]);
		return;
	}
	printf(": rev %x.%x%x%x", buf[0], buf[1], buf[2],
	    ntohs(*(uint16_t *)buf + 4));

	if ((r = asmc_try(sc, ASMC_READ, "#KEY", buf, 4))) {
		printf(", no of keys failed (0x%x)\n", r);
		bus_space_unmap(sc->sc_iot, aaa->aaa_addr[0], aaa->aaa_size[0]);
		return;
	}
	printf(", %u key%s\n", ntohl(*(uint32_t *)buf),
	    (ntohl(*(uint32_t *)buf) == 1) ? "" : "s");

	/* keyboard backlight led is optional */
	sc->sc_backlight = buf[0] = 127, buf[1] = 0;
	if ((r = asmc_try(sc, ASMC_WRITE, "LKSB", buf, 2))) {
		if (r != ASMC_NOTFOUND)
			printf("%s: keyboard backlight failed (0x%x)\n",
			    sc->sc_dev.dv_xname, r);
	} else {
		wskbd_get_backlight = asmc_get_backlight;
		wskbd_set_backlight = asmc_set_backlight;
	}
	task_set(&sc->sc_task_backlight, asmc_backlight, sc);

	strlcpy(sc->sc_sensor_dev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sc_sensor_dev.xname));
	for (i = 0; i < ASMC_MAXTEMP; i++) {
		sc->sc_sensor_temp[i].flags |= SENSOR_FINVALID;
		sc->sc_sensor_temp[i].flags |= SENSOR_FUNKNOWN;
	}
	for (i = 0; i < ASMC_MAXFAN; i++) {
		sc->sc_sensor_fan[i].flags |= SENSOR_FINVALID;
		sc->sc_sensor_fan[i].flags |= SENSOR_FUNKNOWN;
	}
	for (i = 0; i < ASMC_MAXLIGHT; i++) {
		sc->sc_sensor_light[i].flags |= SENSOR_FINVALID;
		sc->sc_sensor_light[i].flags |= SENSOR_FUNKNOWN;
	}
	for (i = 0; i < ASMC_MAXMOTION; i++) {
		sc->sc_sensor_motion[i].flags |= SENSOR_FINVALID;
		sc->sc_sensor_motion[i].flags |= SENSOR_FUNKNOWN;
	}
	asmc_init(sc);

	if (!(sc->sc_sensor_task = sensor_task_register(sc, asmc_update, 5))) {
		printf("%s: unable to register task\n", sc->sc_dev.dv_xname);
		bus_space_unmap(sc->sc_iot, aaa->aaa_addr[0], aaa->aaa_size[0]);
		return;
	}
	sensordev_install(&sc->sc_sensor_dev);
}

int
asmc_detach(struct device *self, int flags)
{
	struct asmc_softc *sc = (struct asmc_softc *)self;
	uint8_t buf[2] = { (sc->sc_backlight = 0), 0 };
	int i;

	if (sc->sc_sensor_task) {
		sensor_task_unregister(sc->sc_sensor_task);
		sc->sc_sensor_task = NULL;
	}
	sensordev_deinstall(&sc->sc_sensor_dev);
	for (i = 0; i < ASMC_MAXMOTION; i++)
		sensor_detach(&sc->sc_sensor_dev, &sc->sc_sensor_motion[i]);
	for (i = 0; i < ASMC_MAXLIGHT; i++)
		sensor_detach(&sc->sc_sensor_dev, &sc->sc_sensor_light[i]);
	for (i = 0; i < ASMC_MAXFAN; i++)
		sensor_detach(&sc->sc_sensor_dev, &sc->sc_sensor_fan[i]);
	for (i = 0; i < ASMC_MAXTEMP; i++)
		sensor_detach(&sc->sc_sensor_dev, &sc->sc_sensor_temp[i]);

	task_del(systq, &sc->sc_task_backlight);
	asmc_try(sc, ASMC_WRITE, "LKSB", buf, 2);
	return 0;
}

int
asmc_activate(struct device *self, int act)
{
	struct asmc_softc *sc = (struct asmc_softc *)self;

	switch (act) {
	case DVACT_WAKEUP:
		asmc_backlight(sc);
		break;
	}

	return 0;
}

void
asmc_backlight(void *arg)
{
	struct asmc_softc *sc = arg;
	uint8_t buf[2] = { sc->sc_backlight, 0 };
	int r;

	if ((r = asmc_try(sc, ASMC_WRITE, "LKSB", buf, 2)))
		printf("%s: keyboard backlight failed (0x%x)\n",
		    sc->sc_dev.dv_xname, r);
}

int
asmc_get_backlight(struct wskbd_backlight *kbl)
{
	struct asmc_softc *sc = asmc_cd.cd_devs[0];

	KASSERT(sc != NULL);
	kbl->min = 0;
	kbl->max = 0xff;
	kbl->curval = sc->sc_backlight;
	return 0;
}

int
asmc_set_backlight(struct wskbd_backlight *kbl)
{
	struct asmc_softc *sc = asmc_cd.cd_devs[0];

	KASSERT(sc != NULL);
	if (kbl->curval > 0xff)
		return EINVAL;
	sc->sc_backlight = kbl->curval;
	task_add(systq, &sc->sc_task_backlight);
	return 0;
}

static uint8_t
asmc_status(struct asmc_softc *sc)
{
	return bus_space_read_1(sc->sc_iot, sc->sc_ioh, ASMC_STATUS);
}

static int
asmc_write(struct asmc_softc *sc, uint8_t off, uint8_t val)
{
	int i;
	uint8_t status;

	bus_space_write_1(sc->sc_iot, sc->sc_ioh, off, val);
	for (i = 0; i < ASMC_DELAY_LOOP; i++) {
		status = bus_space_read_1(sc->sc_iot, sc->sc_ioh, ASMC_COMMAND);
		if (status & ASMC_IBF)
			continue;
		if (status & ASMC_ACCEPT)
			return 0;
		delay(10);
		bus_space_write_1(sc->sc_iot, sc->sc_ioh, off, val);
	}

	return ETIMEDOUT;
}

static int
asmc_read(struct asmc_softc *sc, uint8_t off, uint8_t *buf)
{
	int i;
	uint8_t status;

	for (i = 0; i < ASMC_DELAY_LOOP; i++) {
		status = bus_space_read_1(sc->sc_iot, sc->sc_ioh, ASMC_COMMAND);
		if (status & ASMC_OBF)
			break;
		delay(10);
	}
	if (i == ASMC_DELAY_LOOP)
		return ETIMEDOUT;
	*buf = bus_space_read_1(sc->sc_iot, sc->sc_ioh, off);

	return 0;
}

static int
asmc_command(struct asmc_softc *sc, int cmd, const char *key, uint8_t *buf,
    uint8_t len)
{
	int i;

	if (len > ASMC_MAXLEN)
		return 1;
	if (asmc_write(sc, ASMC_COMMAND, cmd))
		return 1;
	for (i = 0; i < 4; i++)
		if (asmc_write(sc, ASMC_DATA, key[i]))
			return 1;
	if (asmc_write(sc, ASMC_DATA, len))
		return 1;
	if (cmd == ASMC_READ || cmd == ASMC_INFO) {
		for (i = 0; i < len; i++)
			if (asmc_read(sc, ASMC_DATA, &buf[i]))
				return 1;
	} else if (cmd == ASMC_WRITE) {
		for (i = 0; i < len; i++)
			if (asmc_write(sc, ASMC_DATA, buf[i]))
				return 1;
	} else
		return 1;
	return 0;
}

int
asmc_try(struct asmc_softc *sc, int cmd, const char *key, uint8_t *buf,
    uint8_t len)
{
	uint8_t s;
	int i, r;

	rw_enter_write(&sc->sc_lock);
	for (i = 0; i < ASMC_RETRY; i++)
		if (!(r = asmc_command(sc, cmd, key, buf, len)))
			break;
	if (r && (s = asmc_status(sc)))
		r = s;
	rw_exit_write(&sc->sc_lock);

	return r;
}

static uint32_t
asmc_uk(uint8_t *buf)
{
	/* spe78: floating point, signed, 7 bits exponent, 8 bits fraction */
	return (((int16_t)ntohs(*(uint16_t *)buf)) >> 8) * 1000000 + 273150000;
}

static uint16_t
asmc_rpm(uint8_t *buf)
{
	/* fpe2: floating point, unsigned, 14 bits exponent, 2 bits fraction */
	return ntohs(*(uint16_t *)buf) >> 2;
}

static uint32_t
asmc_lux(uint8_t *buf, uint8_t lightlen)
{
	/* newer macbooks report a 10 bit big endian value */
	return (lightlen == 10) ?
	    /* fp18.14: floating point, 18 bits exponent, 14 bits fraction */
	    (ntohl(*(uint32_t *)(buf + 6)) >> 14) * 1000000 :
	    /*
	     * todo: calculate lux from ADC raw data
	     * buf[1] true/false for high/low gain chan reads
	     * chan 0: ntohs(*(uint16_t *)(buf + 2));
	     * chan 1: ntohs(*(uint16_t *)(buf + 4));
	     */
	    ntohs(*(uint16_t *)(buf + 2)) * 1000000;
}

static int
asmc_temp(struct asmc_softc *sc, uint8_t idx, int init)
{
	uint8_t buf[2];
	uint32_t uk;
	int i, r;

	if ((r = asmc_try(sc, ASMC_READ, sc->sc_prod->pr_temp[idx], buf, 2)))
		return r;
	if ((uk = asmc_uk(buf)) < 253150000) /* ignore unlikely values */
		return 0;
	sc->sc_sensor_temp[idx].value = uk;
	sc->sc_sensor_temp[idx].flags &= ~SENSOR_FUNKNOWN;

	if (!init)
		return 0;

	strlcpy(sc->sc_sensor_temp[idx].desc, sc->sc_prod->pr_temp[idx],
	    sizeof(sc->sc_sensor_temp[idx].desc));
	for (i = 0; asmc_temp_desc[i][0]; i++)
		if (!strcmp(asmc_temp_desc[i][0], sc->sc_prod->pr_temp[idx]))
			break;
	if (asmc_temp_desc[i][0]) {
		strlcat(sc->sc_sensor_temp[idx].desc, " ",
		    sizeof(sc->sc_sensor_temp[idx].desc));
		strlcat(sc->sc_sensor_temp[idx].desc, asmc_temp_desc[i][1],
		    sizeof(sc->sc_sensor_temp[idx].desc));
	}
	sc->sc_sensor_temp[idx].type = SENSOR_TEMP;
	sc->sc_sensor_temp[idx].flags &= ~SENSOR_FINVALID;
	sensor_attach(&sc->sc_sensor_dev, &sc->sc_sensor_temp[idx]);
	return 0;
}

static int
asmc_fan(struct asmc_softc *sc, uint8_t idx, int init)
{
	char key[5];
	uint8_t buf[17], *end;
	int r;

	snprintf(key, sizeof(key), "F%dAc", idx);
	if ((r = asmc_try(sc, ASMC_READ, key, buf, 2)))
		return r;
	sc->sc_sensor_fan[idx].value = asmc_rpm(buf);
	sc->sc_sensor_fan[idx].flags &= ~SENSOR_FUNKNOWN;

	if (!init)
		return 0;

	snprintf(key, sizeof(key), "F%dID", idx);
	if ((r = asmc_try(sc, ASMC_READ, key, buf, 16)))
		return r;
	buf[16] = '\0';
	end = buf + 4 + strlen((char *)buf + 4) - 1;
	while (buf + 4 < end && *end == ' ') /* trim trailing spaces */
		*end-- = '\0';
	strlcpy(sc->sc_sensor_fan[idx].desc, buf + 4,
	    sizeof(sc->sc_sensor_fan[idx].desc));
	if (buf[2] < nitems(asmc_fan_loc)) {
		strlcat(sc->sc_sensor_fan[idx].desc, ", ",
		    sizeof(sc->sc_sensor_fan[idx].desc));
		strlcat(sc->sc_sensor_fan[idx].desc, asmc_fan_loc[buf[2]],
		    sizeof(sc->sc_sensor_fan[idx].desc));
	}
	sc->sc_sensor_fan[idx].type = SENSOR_FANRPM;
	sc->sc_sensor_fan[idx].flags &= ~SENSOR_FINVALID;
	sensor_attach(&sc->sc_sensor_dev, &sc->sc_sensor_fan[idx]);
	return 0;
}

static int
asmc_light(struct asmc_softc *sc, uint8_t idx, int init)
{
	char key[5];
	uint8_t buf[10];
	int r;

	snprintf(key, sizeof(key), "ALV%d", idx);
	if (!sc->sc_lightlen) {
		if ((r = asmc_try(sc, ASMC_INFO, key, buf, 6)))
			return r;
		if ((sc->sc_lightlen = buf[0]) > 10)
			return 1;
	}
	if ((r = asmc_try(sc, ASMC_READ, key, buf, sc->sc_lightlen)))
		return r;
	if (!buf[0]) /* valid data? */
		return 0;
	sc->sc_sensor_light[idx].value = asmc_lux(buf, sc->sc_lightlen);
	sc->sc_sensor_light[idx].flags &= ~SENSOR_FUNKNOWN;

	if (!init)
		return 0;

	strlcpy(sc->sc_sensor_light[idx].desc, asmc_light_desc[idx],
	    sizeof(sc->sc_sensor_light[idx].desc));
	sc->sc_sensor_light[idx].type = SENSOR_LUX;
	sc->sc_sensor_light[idx].flags &= ~SENSOR_FINVALID;
	sensor_attach(&sc->sc_sensor_dev, &sc->sc_sensor_light[idx]);
	return 0;
}

#if 0 /* todo: implement motion sensors update and initialization */
static int
asmc_motion(struct asmc_softc *sc, uint8_t idx, int init)
{
	char key[5];
	uint8_t buf[2];
	int r;

	snprintf(key, sizeof(key), "MO_%c", 88 + idx); /* X, Y, Z */
	if ((r = asmc_try(sc, ASMC_READ, key, buf, 2)))
		return r;
	sc->sc_sensor_motion[idx].value = 0;
	sc->sc_sensor_motion[idx].flags &= ~SENSOR_FUNKNOWN;

	if (!init)
		return 0;

	/* todo: setup and attach sensors and description */
	strlcpy(sc->sc_sensor_motion[idx].desc, 120 + idx, /* x, y, z */
	    sizeof(sc->sc_sensor_motion[idx].desc));
	strlcat(sc->sc_sensor_motion[idx].desc, "-axis",
	    sizeof(sc->sc_sensor_motion[idx].desc));
	sc->sc_sensor_motion[idx].type = SENSOR_ACCEL;
	sc->sc_sensor_motion[idx].flags &= ~SENSOR_FINVALID;
	sensor_attach(&sc->sc_sensor_dev, &sc->sc_sensor_motion[idx]);
	return 0;
}
#endif

void
asmc_init(struct asmc_softc *sc)
{
	uint8_t buf[2];
	int i, r;

	/* number of temperature sensors depends on product */
	for (i = 0; i < ASMC_MAXTEMP && sc->sc_prod->pr_temp[i]; i++)
		if ((r = asmc_temp(sc, i, 1)) && r != ASMC_NOTFOUND)
			printf("%s: read temp %d failed (0x%x)\n",
			    sc->sc_dev.dv_xname, i, r);
	/* number of fan sensors depends on product */
	if ((r = asmc_try(sc, ASMC_READ, "FNum", buf, 1)))
		printf("%s: read FNum failed (0x%x)\n",
		    sc->sc_dev.dv_xname, r);
	else
		sc->sc_nfans = buf[0];
	for (i = 0; i < sc->sc_nfans && i < ASMC_MAXFAN; i++)
		if ((r = asmc_fan(sc, i, 1)) && r != ASMC_NOTFOUND)
			printf("%s: read fan %d failed (0x%x)\n",
			    sc->sc_dev.dv_xname, i, r);
	/* left and right light sensors are optional */
	for (i = 0; sc->sc_prod->pr_light && i < ASMC_MAXLIGHT; i++)
		if ((r = asmc_light(sc, i, 1)) && r != ASMC_NOTFOUND)
			printf("%s: read light %d failed (0x%x)\n",
			    sc->sc_dev.dv_xname, i, r);
	/* motion sensors are optional */
	if ((r = asmc_try(sc, ASMC_READ, "MOCN", buf, 2)) &&
	    r != ASMC_NOTFOUND)
		printf("%s: read MOCN failed (0x%x)\n",
		    sc->sc_dev.dv_xname, r);
#if 0 /* todo: initialize sudden motion sensors and setup interrupt handling */
	buf[0] = 0xe0, buf[1] = 0xf8;
	if ((r = asmc_try(sc, ASMC_WRITE, "MOCN", buf, 2)))
		printf("%s write MOCN failed (0x%x)\n",
		    sc->sc_dev.dv_xname, r);
	for (i = 0; i < ASMC_MAXMOTION; i++)
		if ((r = asmc_motion(sc, i, 1)) && r != ASMC_NOTFOUND)
			printf("%s: read motion %d failed (0x%x)\n",
			    sc->sc_dev.dv_xname, i, r);
#endif
}

void
asmc_update(void *arg)
{
	struct asmc_softc *sc = arg;
	int i;

	for (i = 0; i < ASMC_MAXTEMP && sc->sc_prod->pr_temp[i]; i++)
		if (!(sc->sc_sensor_temp[i].flags & SENSOR_FINVALID))
			asmc_temp(sc, i, 0);
	for (i = 0; i < sc->sc_nfans && i < ASMC_MAXFAN; i++)
		if (!(sc->sc_sensor_fan[i].flags & SENSOR_FINVALID))
			asmc_fan(sc, i, 0);
	for (i = 0; i < ASMC_MAXLIGHT; i++)
		if (!(sc->sc_sensor_light[i].flags & SENSOR_FINVALID))
			asmc_light(sc, i, 0);
#if 0
	for (i = 0; i < ASMC_MAXMOTION; i++)
		if (!(sc->sc_sensor_motion[i].flags & SENSOR_FINVALID))
			asmc_motion(sc, i, 0);
#endif
}
