/*	$OpenBSD: lom.c,v 1.29 2022/07/04 19:06:10 miod Exp $	*/
/*
 * Copyright (c) 2009 Mark Kettenis
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
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/sensors.h>
#include <sys/systm.h>
#include <sys/timeout.h>

#include <machine/autoconf.h>
#include <machine/openfirm.h>

#include <sparc64/dev/ebusreg.h>
#include <sparc64/dev/ebusvar.h>

/*
 * LOMlite is a so far unidentified microcontroller.
 */
#define LOM1_STATUS		0x00	/* R */
#define  LOM1_STATUS_BUSY	0x80
#define LOM1_CMD		0x00	/* W */
#define LOM1_DATA		0x01	/* R/W */

/*
 * LOMlite2 is implemented as a H8/3437 microcontroller which has its
 * on-chip host interface hooked up to EBus.
 */
#define LOM2_DATA		0x00	/* R/W */
#define LOM2_CMD		0x01	/* W */
#define LOM2_STATUS		0x01	/* R */
#define  LOM2_STATUS_OBF	0x01	/* Output Buffer Full */
#define  LOM2_STATUS_IBF	0x02	/* Input Buffer Full  */

#define LOM_IDX_CMD		0x00
#define  LOM_IDX_CMD_GENERIC	0x00
#define  LOM_IDX_CMD_TEMP	0x04
#define  LOM_IDX_CMD_FAN	0x05

#define LOM_IDX_FW_REV		0x01	/* Firmware revision  */

#define LOM_IDX_FAN1		0x04	/* Fan speed */
#define LOM_IDX_FAN2		0x05
#define LOM_IDX_FAN3		0x06
#define LOM_IDX_FAN4		0x07
#define LOM_IDX_PSU1		0x08	/* PSU status */
#define LOM_IDX_PSU2		0x09
#define LOM_IDX_PSU3		0x0a
#define  LOM_PSU_INPUTA		0x01
#define  LOM_PSU_INPUTB		0x02
#define  LOM_PSU_OUTPUT		0x04
#define  LOM_PSU_PRESENT	0x08
#define  LOM_PSU_STANDBY	0x10

#define LOM_IDX_TEMP1		0x18	/* Temperature */
#define LOM_IDX_TEMP2		0x19
#define LOM_IDX_TEMP3		0x1a
#define LOM_IDX_TEMP4		0x1b
#define LOM_IDX_TEMP5		0x1c
#define LOM_IDX_TEMP6		0x1d
#define LOM_IDX_TEMP7		0x1e
#define LOM_IDX_TEMP8		0x1f

#define LOM_IDX_LED1		0x25

#define LOM_IDX_ALARM		0x30
#define LOM_IDX_WDOG_CTL	0x31
#define  LOM_WDOG_ENABLE	0x01
#define  LOM_WDOG_RESET		0x02
#define  LOM_WDOG_AL3_WDOG	0x04
#define  LOM_WDOG_AL3_FANPSU	0x08
#define LOM_IDX_WDOG_TIME	0x32
#define  LOM_WDOG_TIME_MAX	126

#define LOM1_IDX_HOSTNAME1	0x33
#define LOM1_IDX_HOSTNAME2	0x34
#define LOM1_IDX_HOSTNAME3	0x35
#define LOM1_IDX_HOSTNAME4	0x36
#define LOM1_IDX_HOSTNAME5	0x37
#define LOM1_IDX_HOSTNAME6	0x38
#define LOM1_IDX_HOSTNAME7	0x39
#define LOM1_IDX_HOSTNAME8	0x3a
#define LOM1_IDX_HOSTNAME9	0x3b
#define LOM1_IDX_HOSTNAME10	0x3c
#define LOM1_IDX_HOSTNAME11	0x3d
#define LOM1_IDX_HOSTNAME12	0x3e

#define LOM2_IDX_HOSTNAMELEN	0x38
#define LOM2_IDX_HOSTNAME	0x39

#define LOM_IDX_CONFIG		0x5d
#define LOM_IDX_FAN1_CAL	0x5e
#define LOM_IDX_FAN2_CAL	0x5f
#define LOM_IDX_FAN3_CAL	0x60
#define LOM_IDX_FAN4_CAL	0x61
#define LOM_IDX_FAN1_LOW	0x62
#define LOM_IDX_FAN2_LOW	0x63
#define LOM_IDX_FAN3_LOW	0x64
#define LOM_IDX_FAN4_LOW	0x65

#define LOM_IDX_CONFIG2		0x66
#define LOM_IDX_CONFIG3		0x67

#define LOM_IDX_PROBE55		0x7e	/* Always returns 0x55 */
#define LOM_IDX_PROBEAA		0x7f	/* Always returns 0xaa */

#define LOM_IDX_WRITE		0x80

#define LOM_IDX4_TEMP_NAME_START	0x40
#define LOM_IDX4_TEMP_NAME_END		0xff

#define LOM_IDX5_FAN_NAME_START		0x40
#define LOM_IDX5_FAN_NAME_END		0xff

#define LOM_MAX_FAN	4
#define LOM_MAX_PSU	3
#define LOM_MAX_TEMP	8

struct lom_cmd {
	uint8_t			lc_cmd;
	uint8_t			lc_data;

	TAILQ_ENTRY(lom_cmd)	lc_next;
};

struct lom_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	int			sc_type;
#define LOM_LOMLITE		0
#define LOM_LOMLITE2		2
	int			sc_space;

	struct ksensor		sc_fan[LOM_MAX_FAN];
	struct ksensor		sc_psu[LOM_MAX_PSU];
	struct ksensor		sc_temp[LOM_MAX_TEMP];
	struct ksensordev	sc_sensordev;

	int			sc_num_fan;
	int			sc_num_psu;
	int			sc_num_temp;

	uint8_t			sc_fan_cal[LOM_MAX_FAN];
	uint8_t			sc_fan_low[LOM_MAX_FAN];

	char			sc_hostname[MAXHOSTNAMELEN];

	struct timeout		sc_wdog_to;
	int			sc_wdog_period;
	uint8_t			sc_wdog_ctl;
	struct lom_cmd		sc_wdog_pat;

	TAILQ_HEAD(, lom_cmd)	sc_queue;
	struct mutex		sc_queue_mtx;
	struct timeout		sc_state_to;
	int			sc_state;
#define LOM_STATE_IDLE		0
#define LOM_STATE_CMD		1
#define LOM_STATE_DATA		2
	int			sc_retry;
};

int	lom_match(struct device *, void *, void *);
void	lom_attach(struct device *, struct device *, void *);
int	lom_activate(struct device *, int);

const struct cfattach lom_ca = {
	sizeof(struct lom_softc), lom_match, lom_attach,
	NULL, lom_activate
};

struct cfdriver lom_cd = {
	NULL, "lom", DV_DULL
};

int	lom_read(struct lom_softc *, uint8_t, uint8_t *);
int	lom_write(struct lom_softc *, uint8_t, uint8_t);
void	lom_queue_cmd(struct lom_softc *, struct lom_cmd *);
void	lom_dequeue_cmd(struct lom_softc *, struct lom_cmd *);
int	lom1_read(struct lom_softc *, uint8_t, uint8_t *);
int	lom1_write(struct lom_softc *, uint8_t, uint8_t);
int	lom1_read_polled(struct lom_softc *, uint8_t, uint8_t *);
int	lom1_write_polled(struct lom_softc *, uint8_t, uint8_t);
void	lom1_queue_cmd(struct lom_softc *, struct lom_cmd *);
void	lom1_process_queue(void *);
void	lom1_process_queue_locked(struct lom_softc *);
int	lom2_read(struct lom_softc *, uint8_t, uint8_t *);
int	lom2_write(struct lom_softc *, uint8_t, uint8_t);
int	lom2_read_polled(struct lom_softc *, uint8_t, uint8_t *);
int	lom2_write_polled(struct lom_softc *, uint8_t, uint8_t);
void	lom2_queue_cmd(struct lom_softc *, struct lom_cmd *);
int	lom2_intr(void *);

int	lom_init_desc(struct lom_softc *sc);
void	lom_refresh(void *);
void	lom1_write_hostname(struct lom_softc *);
void	lom2_write_hostname(struct lom_softc *);

void	lom_wdog_pat(void *);
int	lom_wdog_cb(void *, int);

void	lom_shutdown(void *);

int
lom_match(struct device *parent, void *match, void *aux)
{
	struct ebus_attach_args *ea = aux;

	if (strcmp(ea->ea_name, "SUNW,lom") == 0 ||
	    strcmp(ea->ea_name, "SUNW,lomh") == 0)
		return (1);

	return (0);
}

void
lom_attach(struct device *parent, struct device *self, void *aux)
{
	struct lom_softc *sc = (void *)self;
	struct ebus_attach_args *ea = aux;
	uint8_t reg, fw_rev, config, config2, config3;
	uint8_t cal, low;
	int i;

	if (strcmp(ea->ea_name, "SUNW,lomh") == 0) {
		if (ea->ea_nintrs < 1) {
			printf(": no interrupt\n");
			return;
		}
		sc->sc_type = LOM_LOMLITE2;
	}

	if (ebus_bus_map(ea->ea_iotag, 0,
	    EBUS_PADDR_FROM_REG(&ea->ea_regs[0]),
	    ea->ea_regs[0].size, 0, 0, &sc->sc_ioh) == 0) {
		sc->sc_iot = ea->ea_iotag;
	} else if (ebus_bus_map(ea->ea_memtag, 0,
	    EBUS_PADDR_FROM_REG(&ea->ea_regs[0]),
	    ea->ea_regs[0].size, 0, 0, &sc->sc_ioh) == 0) {
		sc->sc_iot = ea->ea_memtag;
	} else {
		printf(": can't map register space\n");
                return;
	}

	if (sc->sc_type < LOM_LOMLITE2) {
		/* XXX Magic */
		bus_space_read_1(sc->sc_iot, sc->sc_ioh, 0);
		bus_space_write_1(sc->sc_iot, sc->sc_ioh, 3, 0xca);
	}

	if (lom_read(sc, LOM_IDX_PROBE55, &reg) || reg != 0x55 ||
	    lom_read(sc, LOM_IDX_PROBEAA, &reg) || reg != 0xaa ||
	    lom_read(sc, LOM_IDX_FW_REV, &fw_rev) ||
	    lom_read(sc, LOM_IDX_CONFIG, &config))
	{
		printf(": not responding\n");
		return;
	}

	TAILQ_INIT(&sc->sc_queue);
	mtx_init(&sc->sc_queue_mtx, IPL_BIO);

	config2 = config3 = 0;
	if (sc->sc_type < LOM_LOMLITE2) {
		/*
		 * LOMlite doesn't do interrupts so we limp along on
		 * timeouts.
		 */
		timeout_set(&sc->sc_state_to, lom1_process_queue, sc);
	} else {
		lom_read(sc, LOM_IDX_CONFIG2, &config2);
		lom_read(sc, LOM_IDX_CONFIG3, &config3);

		bus_intr_establish(sc->sc_iot, ea->ea_intrs[0],
		    IPL_BIO, 0, lom2_intr, sc, self->dv_xname);
	}

	sc->sc_num_fan = min((config >> 5) & 0x7, LOM_MAX_FAN);
	sc->sc_num_psu = min((config >> 3) & 0x3, LOM_MAX_PSU);
	sc->sc_num_temp = min((config2 >> 4) & 0xf, LOM_MAX_TEMP);

	for (i = 0; i < sc->sc_num_fan; i++) {
		if (lom_read(sc, LOM_IDX_FAN1_CAL + i, &cal) ||
		    lom_read(sc, LOM_IDX_FAN1_LOW + i, &low)) {
			printf(": can't read fan information\n");
			return;
		}
		sc->sc_fan_cal[i] = cal;
		sc->sc_fan_low[i] = low;
	}

	/* Initialize sensor data. */
	strlcpy(sc->sc_sensordev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));
	for (i = 0; i < sc->sc_num_fan; i++) {
		sc->sc_fan[i].type = SENSOR_FANRPM;
		sensor_attach(&sc->sc_sensordev, &sc->sc_fan[i]);
		snprintf(sc->sc_fan[i].desc, sizeof(sc->sc_fan[i].desc),
		    "fan%d", i + 1);
	}
	for (i = 0; i < sc->sc_num_psu; i++) {
		sc->sc_psu[i].type = SENSOR_INDICATOR;
		sensor_attach(&sc->sc_sensordev, &sc->sc_psu[i]);
		snprintf(sc->sc_psu[i].desc, sizeof(sc->sc_psu[i].desc),
		    "PSU%d", i + 1);
	}
	for (i = 0; i < sc->sc_num_temp; i++) {
		sc->sc_temp[i].type = SENSOR_TEMP;
		sensor_attach(&sc->sc_sensordev, &sc->sc_temp[i]);
	}
	if (lom_init_desc(sc)) {
		printf(": can't read sensor names\n");
		return;
	}

	if (sensor_task_register(sc, lom_refresh, 5) == NULL) {
		printf(": unable to register update task\n");
		return;
	}

	sensordev_install(&sc->sc_sensordev);

	/*
	 * We configure the watchdog to turn on the fault LED when the
	 * watchdog timer expires.  We run our own timeout to pat it
	 * such that this won't happen unless the kernel hangs.  When
	 * the watchdog is explicitly configured using sysctl(8), we
	 * reconfigure it to reset the machine and let the standard
	 * watchdog(4) machinery take over.
	 */
	lom_write(sc, LOM_IDX_WDOG_TIME, LOM_WDOG_TIME_MAX);
	lom_read(sc, LOM_IDX_WDOG_CTL, &sc->sc_wdog_ctl);
	sc->sc_wdog_ctl &= ~LOM_WDOG_RESET;
	sc->sc_wdog_ctl |= LOM_WDOG_ENABLE;
	lom_write(sc, LOM_IDX_WDOG_CTL, sc->sc_wdog_ctl);
	timeout_set(&sc->sc_wdog_to, lom_wdog_pat, sc);
	timeout_add_sec(&sc->sc_wdog_to, LOM_WDOG_TIME_MAX / 2);

	wdog_register(lom_wdog_cb, sc);

	printf(": %s rev %d.%d\n",
	    sc->sc_type < LOM_LOMLITE2 ? "LOMlite" : "LOMlite2",
	    fw_rev >> 4, fw_rev & 0x0f);
}

int
lom_activate(struct device *self, int act)
{
	int ret = 0;

	switch (act) {
	case DVACT_POWERDOWN:
		wdog_shutdown(self);
		lom_shutdown(self);
		break;
	}

	return (ret);
}

int
lom_read(struct lom_softc *sc, uint8_t reg, uint8_t *val)
{
	if (sc->sc_type < LOM_LOMLITE2)
		return lom1_read(sc, reg, val);
	else
		return lom2_read(sc, reg, val);
}

int
lom_write(struct lom_softc *sc, uint8_t reg, uint8_t val)
{
	if (sc->sc_type < LOM_LOMLITE2)
		return lom1_write(sc, reg, val);
	else
		return lom2_write(sc, reg, val);
}

void
lom_queue_cmd(struct lom_softc *sc, struct lom_cmd *lc)
{
	if (sc->sc_type < LOM_LOMLITE2)
		return lom1_queue_cmd(sc, lc);
	else
		return lom2_queue_cmd(sc, lc);
}

void
lom_dequeue_cmd(struct lom_softc *sc, struct lom_cmd *lc)
{
	struct lom_cmd *lcp;

	mtx_enter(&sc->sc_queue_mtx);
	TAILQ_FOREACH(lcp, &sc->sc_queue, lc_next) {
		if (lcp == lc) {
			TAILQ_REMOVE(&sc->sc_queue, lc, lc_next);
			break;
		}
	}
	mtx_leave(&sc->sc_queue_mtx);
}

int
lom1_read(struct lom_softc *sc, uint8_t reg, uint8_t *val)
{
	struct lom_cmd lc;
	int error;

	if (cold)
		return lom1_read_polled(sc, reg, val);

	lc.lc_cmd = reg;
	lc.lc_data = 0xff;
	lom1_queue_cmd(sc, &lc);

	error = tsleep_nsec(&lc, PZERO, "lomrd", SEC_TO_NSEC(1));
	if (error)
		lom_dequeue_cmd(sc, &lc);

	*val = lc.lc_data;

	return (error);
}

int
lom1_write(struct lom_softc *sc, uint8_t reg, uint8_t val)
{
	struct lom_cmd lc;
	int error;

	if (cold)
		return lom1_write_polled(sc, reg, val);

	lc.lc_cmd = reg | LOM_IDX_WRITE;
	lc.lc_data = val;
	lom1_queue_cmd(sc, &lc);

	error = tsleep_nsec(&lc, PZERO, "lomwr", SEC_TO_NSEC(2));
	if (error)
		lom_dequeue_cmd(sc, &lc);

	return (error);
}

int
lom1_read_polled(struct lom_softc *sc, uint8_t reg, uint8_t *val)
{
	uint8_t str;
	int i;

	/* Wait for input buffer to become available. */
	for (i = 30; i > 0; i--) {
		str = bus_space_read_1(sc->sc_iot, sc->sc_ioh, LOM1_STATUS);
		delay(1000);
		if ((str & LOM1_STATUS_BUSY) == 0)
			break;
	}
	if (i == 0)
		return (ETIMEDOUT);

	bus_space_write_1(sc->sc_iot, sc->sc_ioh, LOM1_CMD, reg);

	/* Wait until the microcontroller fills output buffer. */
	for (i = 30; i > 0; i--) {
		str = bus_space_read_1(sc->sc_iot, sc->sc_ioh, LOM1_STATUS);
		delay(1000);
		if ((str & LOM1_STATUS_BUSY) == 0)
			break;
	}
	if (i == 0)
		return (ETIMEDOUT);

	*val = bus_space_read_1(sc->sc_iot, sc->sc_ioh, LOM1_DATA);
	return (0);
}

int
lom1_write_polled(struct lom_softc *sc, uint8_t reg, uint8_t val)
{
	uint8_t str;
	int i;

	/* Wait for input buffer to become available. */
	for (i = 30; i > 0; i--) {
		str = bus_space_read_1(sc->sc_iot, sc->sc_ioh, LOM1_STATUS);
		delay(1000);
		if ((str & LOM1_STATUS_BUSY) == 0)
			break;
	}
	if (i == 0)
		return (ETIMEDOUT);

	reg |= LOM_IDX_WRITE;
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, LOM1_CMD, reg);

	/* Wait until the microcontroller fills output buffer. */
	for (i = 30; i > 0; i--) {
		str = bus_space_read_1(sc->sc_iot, sc->sc_ioh, LOM1_STATUS);
		delay(1000);
		if ((str & LOM1_STATUS_BUSY) == 0)
			break;
	}
	if (i == 0)
		return (ETIMEDOUT);

	bus_space_write_1(sc->sc_iot, sc->sc_ioh, LOM1_DATA, val);

	return (0);
}

void
lom1_queue_cmd(struct lom_softc *sc, struct lom_cmd *lc)
{
	mtx_enter(&sc->sc_queue_mtx);
	TAILQ_INSERT_TAIL(&sc->sc_queue, lc, lc_next);
	if (sc->sc_state == LOM_STATE_IDLE) {
		sc->sc_state = LOM_STATE_CMD;
		lom1_process_queue_locked(sc);
	}
	mtx_leave(&sc->sc_queue_mtx);
}

void
lom1_process_queue(void *arg)
{
	struct lom_softc *sc = arg;

	mtx_enter(&sc->sc_queue_mtx);
	lom1_process_queue_locked(sc);
	mtx_leave(&sc->sc_queue_mtx);
}

void
lom1_process_queue_locked(struct lom_softc *sc)
{
	struct lom_cmd *lc;
	uint8_t str;

	lc = TAILQ_FIRST(&sc->sc_queue);
	if (lc == NULL) {
		sc->sc_state = LOM_STATE_IDLE;
		return;
	}

	str = bus_space_read_1(sc->sc_iot, sc->sc_ioh, LOM1_STATUS);
	if (str & LOM1_STATUS_BUSY) {
		if (sc->sc_retry++ < 30) {
			timeout_add_msec(&sc->sc_state_to, 1);
			return;
		}

		/*
		 * Looks like the microcontroller got wedged.  Unwedge
		 * it by writing this magic value.  Give it some time
		 * to recover.
		 */
		bus_space_write_1(sc->sc_iot, sc->sc_ioh, LOM1_DATA, 0xac);
		timeout_add_msec(&sc->sc_state_to, 1000);
		sc->sc_state = LOM_STATE_CMD;
		return;
	}

	sc->sc_retry = 0;

	if (sc->sc_state == LOM_STATE_CMD) {
		bus_space_write_1(sc->sc_iot, sc->sc_ioh, LOM1_CMD, lc->lc_cmd);
		sc->sc_state = LOM_STATE_DATA;
		timeout_add_msec(&sc->sc_state_to, 250);
		return;
	}

	KASSERT(sc->sc_state == LOM_STATE_DATA);
	if ((lc->lc_cmd & LOM_IDX_WRITE) == 0)
		lc->lc_data = bus_space_read_1(sc->sc_iot, sc->sc_ioh, LOM1_DATA);
	else
		bus_space_write_1(sc->sc_iot, sc->sc_ioh, LOM1_DATA, lc->lc_data);

	TAILQ_REMOVE(&sc->sc_queue, lc, lc_next);

	wakeup(lc);

	if (!TAILQ_EMPTY(&sc->sc_queue)) {
		sc->sc_state = LOM_STATE_CMD;
		timeout_add_msec(&sc->sc_state_to, 1);
		return;
	}

	sc->sc_state = LOM_STATE_IDLE;
}

int
lom2_read(struct lom_softc *sc, uint8_t reg, uint8_t *val)
{
	struct lom_cmd lc;
	int error;

	if (cold)
		return lom2_read_polled(sc, reg, val);

	lc.lc_cmd = reg;
	lc.lc_data = 0xff;
	lom2_queue_cmd(sc, &lc);

	error = tsleep_nsec(&lc, PZERO, "lom2rd", SEC_TO_NSEC(1));
	if (error)
		lom_dequeue_cmd(sc, &lc);

	*val = lc.lc_data;

	return (error);
}

int
lom2_read_polled(struct lom_softc *sc, uint8_t reg, uint8_t *val)
{
	uint8_t str;
	int i;

	/* Wait for input buffer to become available. */
	for (i = 1000; i > 0; i--) {
		str = bus_space_read_1(sc->sc_iot, sc->sc_ioh, LOM2_STATUS);
		delay(10);
		if ((str & LOM2_STATUS_IBF) == 0)
			break;
	}
	if (i == 0)
		return (ETIMEDOUT);

	bus_space_write_1(sc->sc_iot, sc->sc_ioh, LOM2_CMD, reg);

	/* Wait until the microcontroller fills output buffer. */
	for (i = 1000; i > 0; i--) {
		str = bus_space_read_1(sc->sc_iot, sc->sc_ioh, LOM2_STATUS);
		delay(10);
		if (str & LOM2_STATUS_OBF)
			break;
	}
	if (i == 0)
		return (ETIMEDOUT);

	*val = bus_space_read_1(sc->sc_iot, sc->sc_ioh, LOM2_DATA);
	return (0);
}

int
lom2_write(struct lom_softc *sc, uint8_t reg, uint8_t val)
{
	struct lom_cmd lc;
	int error;

	if (cold)
		return lom2_write_polled(sc, reg, val);

	lc.lc_cmd = reg | LOM_IDX_WRITE;
	lc.lc_data = val;
	lom2_queue_cmd(sc, &lc);

	error = tsleep_nsec(&lc, PZERO, "lom2wr", SEC_TO_NSEC(1));
	if (error)
		lom_dequeue_cmd(sc, &lc);

	return (error);
}

int
lom2_write_polled(struct lom_softc *sc, uint8_t reg, uint8_t val)
{
	uint8_t str;
	int i;

	/* Wait for input buffer to become available. */
	for (i = 1000; i > 0; i--) {
		str = bus_space_read_1(sc->sc_iot, sc->sc_ioh, LOM2_STATUS);
		delay(10);
		if ((str & LOM2_STATUS_IBF) == 0)
			break;
	}
	if (i == 0)
		return (ETIMEDOUT);

	if (sc->sc_space == LOM_IDX_CMD_GENERIC && reg != LOM_IDX_CMD)
		reg |= LOM_IDX_WRITE;

	bus_space_write_1(sc->sc_iot, sc->sc_ioh, LOM2_CMD, reg);

	/* Wait until the microcontroller fills output buffer. */
	for (i = 1000; i > 0; i--) {
		str = bus_space_read_1(sc->sc_iot, sc->sc_ioh, LOM2_STATUS);
		delay(10);
		if (str & LOM2_STATUS_OBF)
			break;
	}
	if (i == 0)
		return (ETIMEDOUT);

	bus_space_read_1(sc->sc_iot, sc->sc_ioh, LOM2_DATA);

	/* Wait for input buffer to become available. */
	for (i = 1000; i > 0; i--) {
		str = bus_space_read_1(sc->sc_iot, sc->sc_ioh, LOM2_STATUS);
		delay(10);
		if ((str & LOM2_STATUS_IBF) == 0)
			break;
	}
	if (i == 0)
		return (ETIMEDOUT);

	bus_space_write_1(sc->sc_iot, sc->sc_ioh, LOM2_DATA, val);

	/* Wait until the microcontroller fills output buffer. */
	for (i = 1000; i > 0; i--) {
		str = bus_space_read_1(sc->sc_iot, sc->sc_ioh, LOM2_STATUS);
		delay(10);
		if (str & LOM2_STATUS_OBF)
			break;
	}
	if (i == 0)
		return (ETIMEDOUT);

	bus_space_read_1(sc->sc_iot, sc->sc_ioh, LOM2_DATA);

	/* If we switched spaces, remember the one we're in now. */
	if (reg == LOM_IDX_CMD)
		sc->sc_space = val;

	return (0);
}

void
lom2_queue_cmd(struct lom_softc *sc, struct lom_cmd *lc)
{
	uint8_t str;

	mtx_enter(&sc->sc_queue_mtx);
	TAILQ_INSERT_TAIL(&sc->sc_queue, lc, lc_next);
	if (sc->sc_state == LOM_STATE_IDLE) {
		str = bus_space_read_1(sc->sc_iot, sc->sc_ioh, LOM2_STATUS);
		if ((str & LOM2_STATUS_IBF) == 0) {
			bus_space_write_1(sc->sc_iot, sc->sc_ioh,
			    LOM2_CMD, lc->lc_cmd);
			sc->sc_state = LOM_STATE_DATA;
		}
	}
	mtx_leave(&sc->sc_queue_mtx);
}

int
lom2_intr(void *arg)
{
	struct lom_softc *sc = arg;
	struct lom_cmd *lc;
	uint8_t str, obr;

	mtx_enter(&sc->sc_queue_mtx);

	str = bus_space_read_1(sc->sc_iot, sc->sc_ioh, LOM2_STATUS);
	obr = bus_space_read_1(sc->sc_iot, sc->sc_ioh, LOM2_DATA);

	lc = TAILQ_FIRST(&sc->sc_queue);
	if (lc == NULL) {
		mtx_leave(&sc->sc_queue_mtx);
		return (0);
	}

	if (lc->lc_cmd & LOM_IDX_WRITE) {
		bus_space_write_1(sc->sc_iot, sc->sc_ioh,
		    LOM2_DATA, lc->lc_data);
		lc->lc_cmd &= ~LOM_IDX_WRITE;
		mtx_leave(&sc->sc_queue_mtx);
		return (1);
	}

	KASSERT(sc->sc_state == LOM_STATE_DATA);
	lc->lc_data = obr;

	TAILQ_REMOVE(&sc->sc_queue, lc, lc_next);

	wakeup(lc);

	sc->sc_state = LOM_STATE_IDLE;

	if (!TAILQ_EMPTY(&sc->sc_queue)) {
		str = bus_space_read_1(sc->sc_iot, sc->sc_ioh, LOM2_STATUS);
		if ((str & LOM2_STATUS_IBF) == 0) {
			bus_space_write_1(sc->sc_iot, sc->sc_ioh,
			    LOM2_CMD, lc->lc_cmd);
			sc->sc_state = LOM_STATE_DATA;
		}
	}

	mtx_leave(&sc->sc_queue_mtx);

	return (1);
}

int
lom_init_desc(struct lom_softc *sc)
{
	uint8_t val;
	int i, j, k;
	int error;

	/* LOMlite doesn't provide sensor descriptions. */
	if (sc->sc_type < LOM_LOMLITE2)
		return (0);

	/*
	 * Read temperature sensor names.
	 */
	error = lom_write(sc, LOM_IDX_CMD, LOM_IDX_CMD_TEMP);
	if (error)
		return (error);

	i = 0;
	j = 0;
	k = LOM_IDX4_TEMP_NAME_START;
	while (k <= LOM_IDX4_TEMP_NAME_END) {
		error = lom_read(sc, k++, &val);
		if (error)
			goto fail;

		if (val == 0xff)
			break;

		if (j < sizeof (sc->sc_temp[i].desc) - 1)
			sc->sc_temp[i].desc[j++] = val;

		if (val == '\0') {
			i++;
			j = 0;
			if (i < sc->sc_num_temp)
				continue;

			break;
		}
	}

	/*
	 * Read fan names.
	 */
	error = lom_write(sc, LOM_IDX_CMD, LOM_IDX_CMD_FAN);
	if (error)
		return (error);

	i = 0;
	j = 0;
	k = LOM_IDX5_FAN_NAME_START;
	while (k <= LOM_IDX5_FAN_NAME_END) {
		error = lom_read(sc, k++, &val);
		if (error)
			goto fail;

		if (val == 0xff)
			break;

		if (j < sizeof (sc->sc_fan[i].desc) - 1)
			sc->sc_fan[i].desc[j++] = val;

		if (val == '\0') {
			i++;
			j = 0;
			if (i < sc->sc_num_fan)
				continue;

			break;
		}
	}

fail:
	lom_write(sc, LOM_IDX_CMD, LOM_IDX_CMD_GENERIC);
	return (error);
}

void
lom_refresh(void *arg)
{
	struct lom_softc *sc = arg;
	uint8_t val;
	int i;

	for (i = 0; i < sc->sc_num_fan; i++) {
		if (lom_read(sc, LOM_IDX_FAN1 + i, &val)) {
			sc->sc_fan[i].flags |= SENSOR_FINVALID;
			continue;
		}

		sc->sc_fan[i].value = (60 * sc->sc_fan_cal[i] * val) / 100;
		if (val < sc->sc_fan_low[i])
			sc->sc_fan[i].status = SENSOR_S_CRIT;
		else
			sc->sc_fan[i].status = SENSOR_S_OK;
		sc->sc_fan[i].flags &= ~SENSOR_FINVALID;
	}

	for (i = 0; i < sc->sc_num_psu; i++) {
		if (lom_read(sc, LOM_IDX_PSU1 + i, &val) ||
		    !ISSET(val, LOM_PSU_PRESENT)) {
			sc->sc_psu[i].flags |= SENSOR_FINVALID;
			continue;
		}

		if (val & LOM_PSU_STANDBY) {
			sc->sc_psu[i].value = 0;
			sc->sc_psu[i].status = SENSOR_S_UNSPEC;
		} else {
			sc->sc_psu[i].value = 1;
			if (ISSET(val, LOM_PSU_INPUTA) &&
			    ISSET(val, LOM_PSU_INPUTB) &&
			    ISSET(val, LOM_PSU_OUTPUT))
				sc->sc_psu[i].status = SENSOR_S_OK;
			else
				sc->sc_psu[i].status = SENSOR_S_CRIT;
		}
		sc->sc_psu[i].flags &= ~SENSOR_FINVALID;
	}

	for (i = 0; i < sc->sc_num_temp; i++) {
		if (lom_read(sc, LOM_IDX_TEMP1 + i, &val)) {
			sc->sc_temp[i].flags |= SENSOR_FINVALID;
			continue;
		}

		sc->sc_temp[i].value = val * 1000000 + 273150000;
		sc->sc_temp[i].flags &= ~SENSOR_FINVALID;
	}

	/*
	 * If our hostname is set and differs from what's stored in
	 * the LOM, write the new hostname back to the LOM.  Note that
	 * we include the terminating NUL when writing the hostname
	 * back to the LOM, otherwise the LOM will print any trailing
	 * garbage.
	 */
	if (hostnamelen > 0 &&
	    strncmp(sc->sc_hostname, hostname, sizeof(hostname)) != 0) {
		if (sc->sc_type < LOM_LOMLITE2)
			lom1_write_hostname(sc);
		else
			lom2_write_hostname(sc);
		strlcpy(sc->sc_hostname, hostname, sizeof(sc->sc_hostname));
	}
}

void
lom1_write_hostname(struct lom_softc *sc)
{
	char name[(LOM1_IDX_HOSTNAME12 - LOM1_IDX_HOSTNAME1 + 1) + 1];
	char *p;
	int i;

	/*
	 * LOMlite generally doesn't have enough space to store the
	 * fully qualified hostname.  If the hostname is too long,
	 * strip off the domain name.
	 */
	strlcpy(name, hostname, sizeof(name));
	if (hostnamelen >= sizeof(name)) {
		p = strchr(name, '.');
		if (p)
			*p = '\0';
	}

	for (i = 0; i < strlen(name) + 1; i++)
		if (lom_write(sc, LOM1_IDX_HOSTNAME1 + i, name[i]))
			break;
}

void
lom2_write_hostname(struct lom_softc *sc)
{
	int i;

	lom_write(sc, LOM2_IDX_HOSTNAMELEN, hostnamelen + 1);
	for (i = 0; i < hostnamelen + 1; i++)
		lom_write(sc, LOM2_IDX_HOSTNAME, hostname[i]);
}

void
lom_wdog_pat(void *arg)
{
	struct lom_softc *sc = arg;

	/* Pat the dog. */
	sc->sc_wdog_pat.lc_cmd = LOM_IDX_WDOG_CTL | LOM_IDX_WRITE;
	sc->sc_wdog_pat.lc_data = sc->sc_wdog_ctl;
	lom_queue_cmd(sc, &sc->sc_wdog_pat);

	timeout_add_sec(&sc->sc_wdog_to, LOM_WDOG_TIME_MAX / 2);
}

int
lom_wdog_cb(void *arg, int period)
{
	struct lom_softc *sc = arg;

	if (period > LOM_WDOG_TIME_MAX)
		period = LOM_WDOG_TIME_MAX;
	else if (period < 0)
		period = 0;

	if (period == 0) {
		if (sc->sc_wdog_period != 0) {
			/* Stop watchdog from resetting the machine. */
			sc->sc_wdog_ctl &= ~LOM_WDOG_RESET;
			lom_write(sc, LOM_IDX_WDOG_CTL, sc->sc_wdog_ctl);

			lom_write(sc, LOM_IDX_WDOG_TIME, LOM_WDOG_TIME_MAX);
			timeout_add_sec(&sc->sc_wdog_to, LOM_WDOG_TIME_MAX / 2);
		}
	} else {
		if (sc->sc_wdog_period != period) {
			/* Set new timeout. */
			lom_write(sc, LOM_IDX_WDOG_TIME, period);
		}
		if (sc->sc_wdog_period == 0) {
			/* Make watchdog reset the machine. */
			sc->sc_wdog_ctl |= LOM_WDOG_RESET;
			lom_write(sc, LOM_IDX_WDOG_CTL, sc->sc_wdog_ctl);

			timeout_del(&sc->sc_wdog_to);
		} else {
			/* Pat the dog. */
			lom_dequeue_cmd(sc, &sc->sc_wdog_pat);
			sc->sc_wdog_pat.lc_cmd = LOM_IDX_WDOG_CTL | LOM_IDX_WRITE;
			sc->sc_wdog_pat.lc_data = sc->sc_wdog_ctl;
			lom_queue_cmd(sc, &sc->sc_wdog_pat);
		}
	}
	sc->sc_wdog_period = period;

	return (period);
}

void
lom_shutdown(void *arg)
{
	struct lom_softc *sc = arg;

	sc->sc_wdog_ctl &= ~LOM_WDOG_ENABLE;
	lom_write(sc, LOM_IDX_WDOG_CTL, sc->sc_wdog_ctl);
}
