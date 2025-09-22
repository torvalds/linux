/*	$OpenBSD: mbg.c,v 1.37 2024/05/24 06:02:58 jsg Exp $ */

/*
 * Copyright (c) 2006, 2007 Marc Balmer <mbalmer@openbsd.org>
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
#include <sys/device.h>
#include <sys/timeout.h>
#include <sys/systm.h>
#include <sys/sensors.h>
#include <sys/time.h>

#include <machine/bus.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcidevs.h>

struct mbg_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	/*
	 * I/O region used by the AMCC S5920 found on the PCI509 card
	 * used to access the data.
	 */
	bus_space_tag_t		sc_iot_s5920;
	bus_space_handle_t	sc_ioh_s5920;

	struct ksensor		sc_timedelta;
	struct ksensor		sc_signal;
	struct ksensordev	sc_sensordev;
	struct timeout		sc_timeout;	/* invalidate sensor */
	int			sc_trust;	/* trust time in seconds */
	
	int			(*sc_read)(struct mbg_softc *, int cmd,
				    char *buf, size_t len,
				    struct timespec *tstamp);
};

struct mbg_time {
	u_int8_t		hundreds;
	u_int8_t		sec;
	u_int8_t		min;
	u_int8_t		hour;
	u_int8_t		mday;
	u_int8_t		wday;	/* 1 (monday) - 7 (sunday) */
	u_int8_t		mon;
	u_int8_t		year;	/* 0 - 99 */
	u_int8_t		status;
	u_int8_t		signal;
	int8_t			utc_off;
};

struct mbg_time_hr {
	u_int32_t		sec;		/* always UTC */
	u_int32_t		frac;		/* fractions of second */
	int32_t			utc_off;	/* informal only, in seconds */
	u_int16_t		status;
	u_int8_t		signal;
};

/* mbg_time.status bits */
#define MBG_FREERUN		0x01	/* clock running on xtal */
#define MBG_DST_ENA		0x02	/* DST enabled */
#define MBG_SYNC		0x04	/* clock synced at least once */
#define MBG_DST_CHG		0x08	/* DST change announcement */
#define MBG_UTC			0x10	/* special UTC firmware is installed */
#define MBG_LEAP		0x20	/* announcement of a leap second */
#define MBG_IFTM		0x40	/* current time was set from host */
#define MBG_INVALID		0x80	/* time invalid, batt. was disconn. */

/* AMCC S5920 registers */
#define AMCC_DATA		0x00	/* data register, on 2nd IO region */
#define AMCC_OMB		0x0c	/* outgoing mailbox */
#define AMCC_IMB		0x1c	/* incoming mailbox */

/* AMCC S5933 registers */
#define AMCC_OMB1		0x00	/* outgoing mailbox 1 */
#define AMCC_IMB4		0x1c	/* incoming mailbox 4 */
#define AMCC_FIFO		0x20	/* FIFO register */
#define AMCC_INTCSR		0x38	/* interrupt control/status register */
#define AMCC_MCSR		0x3c	/* master control/status register */

/* ASIC registers */
#define ASIC_CFG		0x00
#define ASIC_FEATURES		0x08	/* r/o */
#define ASIC_STATUS		0x10
#define ASIC_CTLSTATUS		0x14
#define ASIC_DATA		0x18
#define ASIC_RES1		0x1c
#define ASIC_ADDON		0x20

/* commands */
#define MBG_GET_TIME		0x00
#define MBG_GET_SYNC_TIME	0x02
#define MBG_GET_TIME_HR		0x03
#define MBG_SET_TIME		0x10
#define MBG_GET_TZCODE		0x32
#define MBG_SET_TZCODE		0x33
#define MBG_GET_FW_ID_1		0x40
#define MBG_GET_FW_ID_2		0x41
#define MBG_GET_SERNUM		0x42

/* timezone codes (for MBG_{GET|SET}_TZCODE) */
#define MBG_TZCODE_CET_CEST	0x00
#define MBG_TZCODE_CET		0x01
#define MBG_TZCODE_UTC		0x02
#define MBG_TZCODE_EET_EEST	0x03

/* misc. constants */
#define MBG_FIFO_LEN		16
#define MBG_ID_LEN		(2 * MBG_FIFO_LEN + 1)
#define MBG_BUSY		0x01
#define MBG_SIG_BIAS		55
#define MBG_SIG_MAX		68
#define NSECPERSEC		1000000000LL	/* nanoseconds per second */
#define HRDIVISOR		0x100000000LL	/* for hi-res timestamp */

int	mbg_probe(struct device *, void *, void *);
void	mbg_attach(struct device *, struct device *, void *);
void	mbg_task(void *);
void	mbg_task_hr(void *);
void	mbg_update_sensor(struct mbg_softc *sc, struct timespec *tstamp,
	    int64_t timedelta, u_int8_t rsignal, u_int16_t status);
int	mbg_read_amcc_s5920(struct mbg_softc *, int cmd, char *buf, size_t len,
	    struct timespec *tstamp);
int	mbg_read_amcc_s5933(struct mbg_softc *, int cmd, char *buf, size_t len,
	    struct timespec *tstamp);
int	mbg_read_asic(struct mbg_softc *, int cmd, char *buf, size_t len,
	    struct timespec *tstamp);
void	mbg_timeout(void *);

const struct cfattach mbg_ca = {
	sizeof(struct mbg_softc), mbg_probe, mbg_attach
};

struct cfdriver mbg_cd = {
	NULL, "mbg", DV_DULL
};

const struct pci_matchid mbg_devices[] = {
	{ PCI_VENDOR_MEINBERG, PCI_PRODUCT_MEINBERG_GPS170PCI },
	{ PCI_VENDOR_MEINBERG, PCI_PRODUCT_MEINBERG_PCI32 },
	{ PCI_VENDOR_MEINBERG, PCI_PRODUCT_MEINBERG_PCI509 },
	{ PCI_VENDOR_MEINBERG, PCI_PRODUCT_MEINBERG_PCI510 },
	{ PCI_VENDOR_MEINBERG, PCI_PRODUCT_MEINBERG_PCI511 },
	{ PCI_VENDOR_MEINBERG, PCI_PRODUCT_MEINBERG_PEX511 },
	{ PCI_VENDOR_MEINBERG, PCI_PRODUCT_MEINBERG_PZF180PEX }
};

int
mbg_probe(struct device *parent, void *match, void *aux)
{
	return pci_matchbyid((struct pci_attach_args *)aux, mbg_devices,
	    nitems(mbg_devices));
}

void
mbg_attach(struct device *parent, struct device *self, void *aux)
{
	struct mbg_softc *sc = (struct mbg_softc *)self;
	struct pci_attach_args *pa = (struct pci_attach_args *)aux;
	struct mbg_time tframe;
	pcireg_t memtype;
	bus_size_t iosize, iosize2;
	int bar = PCI_MAPREG_START, signal, t_trust;
	const char *desc;
#ifdef MBG_DEBUG
	char fw_id[MBG_ID_LEN];
#endif

	timeout_set(&sc->sc_timeout, mbg_timeout, sc);

	/* for the PEX511 use BAR2 instead of BAR0*/
	if (PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_MEINBERG_PEX511)
		bar += 0x08;

	memtype = pci_mapreg_type(pa->pa_pc, pa->pa_tag, bar);
	if (pci_mapreg_map(pa, bar, memtype, 0, &sc->sc_iot,
	    &sc->sc_ioh, NULL, &iosize, 0)) {
		printf(": PCI %s region not found\n",
		    memtype == PCI_MAPREG_TYPE_IO ? "I/O" : "memory");
		return;
	}

	if ((desc = pci_findproduct(pa->pa_id)) == NULL)
		desc = "Radio clock";
	strlcpy(sc->sc_timedelta.desc, desc, sizeof(sc->sc_timedelta.desc));

	strlcpy(sc->sc_sensordev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));

	sc->sc_timedelta.type = SENSOR_TIMEDELTA;
	sc->sc_timedelta.status = SENSOR_S_UNKNOWN;
	sensor_attach(&sc->sc_sensordev, &sc->sc_timedelta);

	sc->sc_signal.type = SENSOR_PERCENT;
	sc->sc_signal.status = SENSOR_S_UNKNOWN;
	strlcpy(sc->sc_signal.desc, "Signal", sizeof(sc->sc_signal.desc));
	sensor_attach(&sc->sc_sensordev, &sc->sc_signal);

	t_trust = 12 * 60 * 60;		/* twelve hours */

	switch (PCI_PRODUCT(pa->pa_id)) {
	case PCI_PRODUCT_MEINBERG_PCI32:
		sc->sc_read = mbg_read_amcc_s5933;
		sensor_task_register(sc, mbg_task, 10);
		break;
	case PCI_PRODUCT_MEINBERG_PCI509:
		/*
		 * map the second I/O region needed in addition to the first
		 * to get at the actual data.
		 */
		memtype = pci_mapreg_type(pa->pa_pc, pa->pa_tag,
		    PCI_MAPREG_START + 0x04);
		if (pci_mapreg_map(pa, PCI_MAPREG_START + 0x04, memtype, 0,
		    &sc->sc_iot_s5920, &sc->sc_ioh_s5920, NULL, &iosize2, 0)) {
			printf(": PCI2 %s region not found\n",
			    memtype == PCI_MAPREG_TYPE_IO ? "I/O" : "memory");

			/* unmap first mapped region as well if we fail */
			bus_space_unmap(sc->sc_iot, sc->sc_ioh, iosize);
			return;
		}
		sc->sc_read = mbg_read_amcc_s5920;
		sensor_task_register(sc, mbg_task, 10);
		break;
	case PCI_PRODUCT_MEINBERG_PCI510:
	case PCI_PRODUCT_MEINBERG_PCI511:
	case PCI_PRODUCT_MEINBERG_PEX511:
		sc->sc_read = mbg_read_asic;
		sensor_task_register(sc, mbg_task, 10);
		break;
	case PCI_PRODUCT_MEINBERG_GPS170PCI:
	case PCI_PRODUCT_MEINBERG_PZF180PEX:
		t_trust = 4 * 24 * 60 * 60;	/* four days */
		sc->sc_read = mbg_read_asic;
		sensor_task_register(sc, mbg_task_hr, 1);
		break;
	default:
		/* this can not normally happen, but then there is murphy */
		panic(": unsupported product 0x%04x", PCI_PRODUCT(pa->pa_id));
		break;
	}

	sc->sc_trust = t_trust;

	if (sc->sc_read(sc, MBG_GET_TIME, (char *)&tframe,
	    sizeof(struct mbg_time), NULL)) {
		printf(": unknown status");
		sc->sc_signal.status = SENSOR_S_CRIT;
	} else {
		sc->sc_signal.status = SENSOR_S_OK;
		signal = tframe.signal - MBG_SIG_BIAS;
		if (signal < 0)
			signal = 0;
		else if (signal > MBG_SIG_MAX)
			signal = MBG_SIG_MAX;
		sc->sc_signal.value = signal;

		if (tframe.status & MBG_SYNC)
			printf(": synchronized");
		else
			printf(": not synchronized");
		if (tframe.status & MBG_FREERUN) {
			sc->sc_signal.status = SENSOR_S_WARN;
			printf(", free running");
		}
		if (tframe.status & MBG_IFTM)
			printf(", time set from host");
	}
#ifdef MBG_DEBUG
	if (sc->sc_read(sc, MBG_GET_FW_ID_1, fw_id, MBG_FIFO_LEN, NULL) ||
	    sc->sc_read(sc, MBG_GET_FW_ID_2, &fw_id[MBG_FIFO_LEN], MBG_FIFO_LEN,
	    NULL))
		printf(", firmware unknown");
	else {
		fw_id[MBG_ID_LEN - 1] = '\0';
		printf(", firmware %s", fw_id);
	}
#endif
	printf("\n");
	sensordev_install(&sc->sc_sensordev);
	timeout_add_sec(&sc->sc_timeout, sc->sc_trust);
}

/*
 * mbg_task() reads a timestamp from cards that to not provide a high
 * resolution timestamp.  The precision is limited to 1/100 sec.
 */
void
mbg_task(void *arg)
{
	struct mbg_softc *sc = (struct mbg_softc *)arg;
	struct mbg_time tframe;
	struct clock_ymdhms ymdhms;
	struct timespec tstamp;
	int64_t timedelta;
	time_t trecv;

	if (sc->sc_read(sc, MBG_GET_TIME, (char *)&tframe, sizeof(tframe),
	    &tstamp)) {
		sc->sc_signal.status = SENSOR_S_CRIT;
		return;
	}
	if (tframe.status & MBG_INVALID) {
		sc->sc_signal.status = SENSOR_S_CRIT;
		return;
	}
	ymdhms.dt_year = tframe.year + 2000;
	ymdhms.dt_mon = tframe.mon;
	ymdhms.dt_day = tframe.mday;
	ymdhms.dt_hour = tframe.hour;
	ymdhms.dt_min = tframe.min;
	ymdhms.dt_sec = tframe.sec;
	trecv = clock_ymdhms_to_secs(&ymdhms) - tframe.utc_off * 3600;

	timedelta = (int64_t)((tstamp.tv_sec - trecv) * 100
	    - tframe.hundreds) * 10000000LL + tstamp.tv_nsec;

	mbg_update_sensor(sc, &tstamp, timedelta, tframe.signal,
	    (u_int16_t)tframe.status);
}

/*
 * mbg_task_hr() reads a timestamp from cards that do provide a high
 * resolution timestamp.
 */
void
mbg_task_hr(void *arg)
{
	struct mbg_softc *sc = (struct mbg_softc *)arg;
	struct mbg_time_hr tframe;
	struct timespec tstamp;
	int64_t tlocal, trecv;

	if (sc->sc_read(sc, MBG_GET_TIME_HR, (char *)&tframe, sizeof(tframe),
	    &tstamp)) {
		sc->sc_signal.status = SENSOR_S_CRIT;
		return;
	}
	if (tframe.status & MBG_INVALID) {
		sc->sc_signal.status = SENSOR_S_CRIT;
		return;
	}

	tlocal = tstamp.tv_sec * NSECPERSEC + tstamp.tv_nsec;
	trecv = letoh32(tframe.sec) * NSECPERSEC +
	    (letoh32(tframe.frac) * NSECPERSEC >> 32);

	mbg_update_sensor(sc, &tstamp, tlocal - trecv, tframe.signal,
	    letoh16(tframe.status));
}

/* update the sensor value, common to all cards */
void
mbg_update_sensor(struct mbg_softc *sc, struct timespec *tstamp,
    int64_t timedelta, u_int8_t rsignal, u_int16_t status)
{
	int signal;

	sc->sc_timedelta.value = timedelta;
	sc->sc_timedelta.tv.tv_sec = tstamp->tv_sec;
	sc->sc_timedelta.tv.tv_usec = tstamp->tv_nsec / 1000;

	signal = rsignal - MBG_SIG_BIAS;
	if (signal < 0)
		signal = 0;
	else if (signal > MBG_SIG_MAX)
		signal = MBG_SIG_MAX;

	sc->sc_signal.value = signal * 100000 / MBG_SIG_MAX;
	sc->sc_signal.status = status & MBG_FREERUN ?
	    SENSOR_S_WARN : SENSOR_S_OK;
	sc->sc_signal.tv.tv_sec = sc->sc_timedelta.tv.tv_sec;
	sc->sc_signal.tv.tv_usec = sc->sc_timedelta.tv.tv_usec;
	if (!(status & MBG_FREERUN)) {
		sc->sc_timedelta.status = SENSOR_S_OK;
		timeout_add_sec(&sc->sc_timeout, sc->sc_trust);
	}
}

/*
 * send a command and read back results to an AMCC S5920 based card
 * (e.g. the PCI509 DCF77 radio clock)
 */
int
mbg_read_amcc_s5920(struct mbg_softc *sc, int cmd, char *buf, size_t len,
    struct timespec *tstamp)
{
	long timer, tmax;
	size_t quot, rem;
	u_int32_t ul;
	int n;
	u_int8_t status;

	quot = len / 4;
	rem = len % 4;

	/* write the command, optionally taking a timestamp */
	if (tstamp)
		nanotime(tstamp);
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, AMCC_OMB, cmd);

	/* wait for the BUSY flag to go low (approx 70 us on i386) */
	timer = 0;
	tmax = cold ? 50 : 10;
	do {
		if (cold)
			delay(20);
		else
			tsleep_nsec(tstamp, 0, "mbg", MSEC_TO_NSEC(1));
		status = bus_space_read_1(sc->sc_iot, sc->sc_ioh,
		    AMCC_IMB4 + 3);
	} while ((status & MBG_BUSY) && timer++ < tmax);

	if (status & MBG_BUSY)
		return -1;

	/* read data from the device */
	if (len) {
		for (n = 0; n < quot; n++) {
			*(u_int32_t *)buf = bus_space_read_4(sc->sc_iot_s5920,
			    sc->sc_ioh_s5920, AMCC_DATA);
			buf += sizeof(u_int32_t);
		}
		if (rem) {
			ul =  bus_space_read_4(sc->sc_iot_s5920,
			    sc->sc_ioh_s5920, AMCC_DATA);
			for (n = 0; n < rem; n++)
				*buf++ = *((char *)&ul + n);
		}
	} else
		bus_space_read_4(sc->sc_iot_s5920, sc->sc_ioh_s5920, AMCC_DATA);
	return 0;
}

/*
 * send a command and read back results to an AMCC S5933 based card
 * (e.g. the PCI32 DCF77 radio clock)
 */
int
mbg_read_amcc_s5933(struct mbg_softc *sc, int cmd, char *buf, size_t len,
    struct timespec *tstamp)
{
	long timer, tmax;
	size_t n;
	u_int8_t status;

	/* reset inbound mailbox and clear FIFO status */
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, AMCC_MCSR + 3, 0x0c);

	/* set FIFO */
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, AMCC_INTCSR + 3, 0x3c);

	/* write the command, optionally taking a timestamp */
	if (tstamp)
		nanotime(tstamp);
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, AMCC_OMB1, cmd);

	/* wait for the BUSY flag to go low (approx 70 us on i386) */
	timer = 0;
	tmax = cold ? 50 : 10;
	do {
		if (cold)
			delay(20);
		else
			tsleep_nsec(tstamp, 0, "mbg", MSEC_TO_NSEC(1));
		status = bus_space_read_1(sc->sc_iot, sc->sc_ioh,
		    AMCC_IMB4 + 3);
	} while ((status & MBG_BUSY) && timer++ < tmax);

	if (status & MBG_BUSY)
		return -1;

	/* read data from the device FIFO */
	for (n = 0; n < len; n++) {
		if (bus_space_read_2(sc->sc_iot, sc->sc_ioh, AMCC_MCSR)
		    & 0x20) {
			return -1;
		}
		buf[n] = bus_space_read_1(sc->sc_iot, sc->sc_ioh,
		    AMCC_FIFO + (n % 4));
	}
	return 0;
}

/*
 * send a command and read back results to an ASIC based card
 * (e.g. the PCI511 DCF77 radio clock)
 */
int
mbg_read_asic(struct mbg_softc *sc, int cmd, char *buf, size_t len,
    struct timespec *tstamp)
{
	long timer, tmax;
	size_t n;
	u_int32_t data;
	u_int16_t port;
	char *p = buf;
	u_int8_t status;
	int s;

	/* write the command, optionally taking a timestamp */
	if (tstamp) {
		s = splhigh();
		nanotime(tstamp);
		bus_space_write_4(sc->sc_iot, sc->sc_ioh, ASIC_DATA, cmd);
		splx(s);
	} else
		bus_space_write_4(sc->sc_iot, sc->sc_ioh, ASIC_DATA, cmd);

	/* wait for the BUSY flag to go low */
	timer = 0;
	tmax = cold ? 50 : 10;
	do {
		if (cold)
			delay(20);
		else
			tsleep_nsec(tstamp, 0, "mbg", MSEC_TO_NSEC(1));
		status = bus_space_read_1(sc->sc_iot, sc->sc_ioh, ASIC_STATUS);
	} while ((status & MBG_BUSY) && timer++ < tmax);

	if (status & MBG_BUSY)
		return -1;

	/* read data from the device FIFO */
	port = ASIC_ADDON;
	for (n = 0; n < len / 4; n++) {
		data = bus_space_read_4(sc->sc_iot, sc->sc_ioh, port);
		*(u_int32_t *)p = data;
		p += sizeof(data);
		port += sizeof(data);
	}

	if (len % 4) {
		data = bus_space_read_4(sc->sc_iot, sc->sc_ioh, port);
		for (n = 0; n < len % 4; n++) {
			*p++ = data & 0xff;
			data >>= 8;
		}
	}
	return 0;
}

/*
 * degrade the sensor state if we are freerunning for more than
 * sc->sc_trust seconds.
 */
void
mbg_timeout(void *xsc)
{
	struct mbg_softc *sc = xsc;

	if (sc->sc_timedelta.status == SENSOR_S_OK) {
		sc->sc_timedelta.status = SENSOR_S_WARN;
		/*
		 * further degrade in sc->sc_trust seconds if no new valid
		 * time data can be read from the device.
		 */
		timeout_add_sec(&sc->sc_timeout, sc->sc_trust);
	} else
		sc->sc_timedelta.status = SENSOR_S_CRIT;
}
