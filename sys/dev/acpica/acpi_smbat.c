/*-
 * Copyright (c) 2005 Hans Petter Selasky
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_acpi.h"
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>

#include <contrib/dev/acpica/include/acpi.h>

#include <dev/acpica/acpivar.h>
#include <dev/acpica/acpiio.h>
#include <dev/acpica/acpi_smbus.h>

/* Transactions have failed after 500 ms. */
#define SMBUS_TIMEOUT	50

struct acpi_smbat_softc {
	uint8_t		sb_base_addr;
	device_t	ec_dev;

	struct acpi_bif	bif;
	struct acpi_bst	bst;
	struct timespec	bif_lastupdated;
	struct timespec	bst_lastupdated;
};

static int	acpi_smbat_probe(device_t dev);
static int	acpi_smbat_attach(device_t dev);
static int	acpi_smbat_shutdown(device_t dev);
static int	acpi_smbat_info_expired(struct timespec *lastupdated);
static void	acpi_smbat_info_updated(struct timespec *lastupdated);
static int	acpi_smbat_get_bif(device_t dev, struct acpi_bif *bif);
static int	acpi_smbat_get_bst(device_t dev, struct acpi_bst *bst);

ACPI_SERIAL_DECL(smbat, "ACPI Smart Battery");

static SYSCTL_NODE(_debug_acpi, OID_AUTO, batt, CTLFLAG_RD, NULL,
    "Battery debugging");

/* On some laptops with smart batteries, enabling battery monitoring
 * software causes keystrokes from atkbd to be lost.  This has also been
 * reported on Linux, and is apparently due to the keyboard and I2C line
 * for the battery being routed through the same chip.  Whether that's
 * accurate or not, adding extra sleeps to the status checking code
 * causes the problem to go away.
 *
 * If you experience that problem, try a value of 10ms and move up
 * from there.
 */
static int      batt_sleep_ms;
SYSCTL_INT(_debug_acpi_batt, OID_AUTO, batt_sleep_ms, CTLFLAG_RW, &batt_sleep_ms, 0,
    "Sleep during battery status updates to prevent keystroke loss.");

static device_method_t acpi_smbat_methods[] = {
	/* device interface */
	DEVMETHOD(device_probe, acpi_smbat_probe),
	DEVMETHOD(device_attach, acpi_smbat_attach),
	DEVMETHOD(device_shutdown, acpi_smbat_shutdown),

	/* ACPI battery interface */
	DEVMETHOD(acpi_batt_get_status, acpi_smbat_get_bst),
	DEVMETHOD(acpi_batt_get_info, acpi_smbat_get_bif),

	DEVMETHOD_END
};

static driver_t	acpi_smbat_driver = {
	"battery",
	acpi_smbat_methods,
	sizeof(struct acpi_smbat_softc),
};

static devclass_t acpi_smbat_devclass;
DRIVER_MODULE(acpi_smbat, acpi, acpi_smbat_driver, acpi_smbat_devclass, 0, 0);
MODULE_DEPEND(acpi_smbat, acpi, 1, 1, 1);

static int
acpi_smbat_probe(device_t dev)
{
	static char *smbat_ids[] = {"ACPI0001", "ACPI0005", NULL};
	ACPI_STATUS status;
	int rv;

	if (acpi_disabled("smbat"))
		return (ENXIO);
	rv = ACPI_ID_PROBE(device_get_parent(dev), dev, smbat_ids, NULL);
	if (rv > 0)
	  return (rv);
	status = AcpiEvaluateObject(acpi_get_handle(dev), "_EC", NULL, NULL);
	if (ACPI_FAILURE(status))
		return (ENXIO);
	device_set_desc(dev, "ACPI Smart Battery");
	return (rv);
}

static int
acpi_smbat_attach(device_t dev)
{
	struct acpi_smbat_softc *sc;
	uint32_t base;

	sc = device_get_softc(dev);
	if (ACPI_FAILURE(acpi_GetInteger(acpi_get_handle(dev), "_EC", &base))) {
		device_printf(dev, "cannot get EC base address\n");
		return (ENXIO);
	}
	sc->sb_base_addr = (base >> 8) & 0xff;

	/* XXX Only works with one EC, but nearly all systems only have one. */
	sc->ec_dev = devclass_get_device(devclass_find("acpi_ec"), 0);
	if (sc->ec_dev == NULL) {
		device_printf(dev, "cannot find EC device\n");
		return (ENXIO);
	}

	timespecclear(&sc->bif_lastupdated);
	timespecclear(&sc->bst_lastupdated);

	if (acpi_battery_register(dev) != 0) {
		device_printf(dev, "cannot register battery\n");
		return (ENXIO);
	}
	return (0);
}

static int
acpi_smbat_shutdown(device_t dev)
{

	acpi_battery_remove(dev);
	return (0);
}

static int
acpi_smbat_info_expired(struct timespec *lastupdated)
{
	struct timespec	curtime;

	ACPI_SERIAL_ASSERT(smbat);

	if (lastupdated == NULL)
		return (TRUE);
	if (!timespecisset(lastupdated))
		return (TRUE);

	getnanotime(&curtime);
	timespecsub(&curtime, lastupdated, &curtime);
	return (curtime.tv_sec < 0 ||
	    curtime.tv_sec > acpi_battery_get_info_expire());
}

static void
acpi_smbat_info_updated(struct timespec *lastupdated)
{

	ACPI_SERIAL_ASSERT(smbat);

	if (lastupdated != NULL)
		getnanotime(lastupdated);
}

static int
acpi_smbus_read_2(struct acpi_smbat_softc *sc, uint8_t addr, uint8_t cmd,
    uint16_t *ptr)
{
	int error, to;
	UINT64 val;

	ACPI_SERIAL_ASSERT(smbat);

	if (batt_sleep_ms)
	    AcpiOsSleep(batt_sleep_ms);

	val = addr;
	error = ACPI_EC_WRITE(sc->ec_dev, sc->sb_base_addr + SMBUS_ADDR,
	    val, 1);
	if (error)
		goto out;

	val = cmd;
	error = ACPI_EC_WRITE(sc->ec_dev, sc->sb_base_addr + SMBUS_CMD,
	    val, 1);
	if (error)
		goto out;

	val = 0x09; /* | 0x80 if PEC */
	error = ACPI_EC_WRITE(sc->ec_dev, sc->sb_base_addr + SMBUS_PRTCL,
	    val, 1);
	if (error)
		goto out;

	if (batt_sleep_ms)
	    AcpiOsSleep(batt_sleep_ms);

	for (to = SMBUS_TIMEOUT; to != 0; to--) {
		error = ACPI_EC_READ(sc->ec_dev, sc->sb_base_addr + SMBUS_PRTCL,
		    &val, 1);
		if (error)
			goto out;
		if (val == 0)
			break;
		AcpiOsSleep(10);
	}
	if (to == 0) {
		error = ETIMEDOUT;
		goto out;
	}

	error = ACPI_EC_READ(sc->ec_dev, sc->sb_base_addr + SMBUS_STS, &val, 1);
	if (error)
		goto out;
	if (val & SMBUS_STS_MASK) {
		printf("%s: AE_ERROR 0x%x\n",
		       __FUNCTION__, (int)(val & SMBUS_STS_MASK));
		error = EIO;
		goto out;
	}

	error = ACPI_EC_READ(sc->ec_dev, sc->sb_base_addr + SMBUS_DATA,
	    &val, 2);
	if (error)
		goto out;

	*ptr = val;

out:
	return (error);
}

static int
acpi_smbus_read_multi_1(struct acpi_smbat_softc *sc, uint8_t addr, uint8_t cmd,
    uint8_t *ptr, uint16_t len)
{
	UINT64 val;
	uint8_t	to;
	int error;

	ACPI_SERIAL_ASSERT(smbat);

	if (batt_sleep_ms)
	    AcpiOsSleep(batt_sleep_ms);

	val = addr;
	error = ACPI_EC_WRITE(sc->ec_dev, sc->sb_base_addr + SMBUS_ADDR,
	    val, 1);
	if (error)
		goto out;

	val = cmd;
	error = ACPI_EC_WRITE(sc->ec_dev, sc->sb_base_addr + SMBUS_CMD,
	    val, 1);
	if (error)
		goto out;

	val = 0x0B /* | 0x80 if PEC */ ;
	error = ACPI_EC_WRITE(sc->ec_dev, sc->sb_base_addr + SMBUS_PRTCL,
	    val, 1);
	if (error)
		goto out;

	if (batt_sleep_ms)
	    AcpiOsSleep(batt_sleep_ms);

	for (to = SMBUS_TIMEOUT; to != 0; to--) {
		error = ACPI_EC_READ(sc->ec_dev, sc->sb_base_addr + SMBUS_PRTCL,
		    &val, 1);
		if (error)
			goto out;
		if (val == 0)
			break;
		AcpiOsSleep(10);
	}
	if (to == 0) {
		error = ETIMEDOUT;
		goto out;
	}

	error = ACPI_EC_READ(sc->ec_dev, sc->sb_base_addr + SMBUS_STS, &val, 1);
	if (error)
		goto out;
	if (val & SMBUS_STS_MASK) {
		printf("%s: AE_ERROR 0x%x\n",
		       __FUNCTION__, (int)(val & SMBUS_STS_MASK));
		error = EIO;
		goto out;
	}

	/* get length */
	error = ACPI_EC_READ(sc->ec_dev, sc->sb_base_addr + SMBUS_BCNT,
	    &val, 1);
	if (error)
		goto out;
	val = (val & 0x1f) + 1;

	bzero(ptr, len);
	if (len > val)
		len = val;

	if (batt_sleep_ms)
	    AcpiOsSleep(batt_sleep_ms);

	while (len--) {
		error = ACPI_EC_READ(sc->ec_dev, sc->sb_base_addr + SMBUS_DATA
		    + len, &val, 1);
		if (error)
			goto out;

		ptr[len] = val;
		if (batt_sleep_ms)
		    AcpiOsSleep(batt_sleep_ms);
	}

out:
	return (error);
}

static int
acpi_smbat_get_bst(device_t dev, struct acpi_bst *bst)
{
	struct acpi_smbat_softc *sc;
	int error;
	uint32_t factor;
	int16_t val;
	uint8_t	addr;

	ACPI_SERIAL_BEGIN(smbat);

	addr = SMBATT_ADDRESS;
	error = ENXIO;
	sc = device_get_softc(dev);

	if (!acpi_smbat_info_expired(&sc->bst_lastupdated)) {
		error = 0;
		goto out;
	}

	if (acpi_smbus_read_2(sc, addr, SMBATT_CMD_BATTERY_MODE, &val))
		goto out;
	if (val & SMBATT_BM_CAPACITY_MODE)
		factor = 10;
	else
		factor = 1;

	/* get battery status */
	if (acpi_smbus_read_2(sc, addr, SMBATT_CMD_BATTERY_STATUS, &val))
		goto out;

	sc->bst.state = 0;
	if (val & SMBATT_BS_DISCHARGING)
		sc->bst.state |= ACPI_BATT_STAT_DISCHARG;

	if (val & SMBATT_BS_REMAINING_CAPACITY_ALARM)
		sc->bst.state |= ACPI_BATT_STAT_CRITICAL;

	/*
	 * If the rate is negative, it is discharging.  Otherwise,
	 * it is charging.
	 */
	if (acpi_smbus_read_2(sc, addr, SMBATT_CMD_CURRENT, &val))
		goto out;

	if (val > 0) {
		sc->bst.rate = val * factor;
		sc->bst.state &= ~SMBATT_BS_DISCHARGING;
		sc->bst.state |= ACPI_BATT_STAT_CHARGING;
	} else if (val < 0)
		sc->bst.rate = (-val) * factor;
	else
		sc->bst.rate = 0;

	if (acpi_smbus_read_2(sc, addr, SMBATT_CMD_REMAINING_CAPACITY, &val))
		goto out;
	sc->bst.cap = val * factor;

	if (acpi_smbus_read_2(sc, addr, SMBATT_CMD_VOLTAGE, &val))
		goto out;
	sc->bst.volt = val;

	acpi_smbat_info_updated(&sc->bst_lastupdated);
	error = 0;

out:
	if (error == 0)
		memcpy(bst, &sc->bst, sizeof(sc->bst));
	ACPI_SERIAL_END(smbat);
	return (error);
}

static int
acpi_smbat_get_bif(device_t dev, struct acpi_bif *bif)
{
	struct acpi_smbat_softc *sc;
	int error;
	uint32_t factor;
	uint16_t val;
	uint8_t addr;

	ACPI_SERIAL_BEGIN(smbat);

	addr = SMBATT_ADDRESS;
	error = ENXIO;
	sc = device_get_softc(dev);

	if (!acpi_smbat_info_expired(&sc->bif_lastupdated)) {
		error = 0;
		goto out;
	}

	if (acpi_smbus_read_2(sc, addr, SMBATT_CMD_BATTERY_MODE, &val))
		goto out;
	if (val & SMBATT_BM_CAPACITY_MODE) {
		factor = 10;
		sc->bif.units = ACPI_BIF_UNITS_MW;
	} else {
		factor = 1;
		sc->bif.units = ACPI_BIF_UNITS_MA;
	}

	if (acpi_smbus_read_2(sc, addr, SMBATT_CMD_DESIGN_CAPACITY, &val))
		goto out;
	sc->bif.dcap = val * factor;

	if (acpi_smbus_read_2(sc, addr, SMBATT_CMD_FULL_CHARGE_CAPACITY, &val))
		goto out;
	sc->bif.lfcap = val * factor;
	sc->bif.btech = 1;		/* secondary (rechargeable) */

	if (acpi_smbus_read_2(sc, addr, SMBATT_CMD_DESIGN_VOLTAGE, &val))
		goto out;
	sc->bif.dvol = val;

	sc->bif.wcap = sc->bif.dcap / 10;
	sc->bif.lcap = sc->bif.dcap / 10;

	sc->bif.gra1 = factor;	/* not supported */
	sc->bif.gra2 = factor;	/* not supported */

	if (acpi_smbus_read_multi_1(sc, addr, SMBATT_CMD_DEVICE_NAME,
	    sc->bif.model, sizeof(sc->bif.model)))
		goto out;

	if (acpi_smbus_read_2(sc, addr, SMBATT_CMD_SERIAL_NUMBER, &val))
		goto out;
	snprintf(sc->bif.serial, sizeof(sc->bif.serial), "0x%04x", val);

	if (acpi_smbus_read_multi_1(sc, addr, SMBATT_CMD_DEVICE_CHEMISTRY,
	    sc->bif.type, sizeof(sc->bif.type)))
		goto out;

	if (acpi_smbus_read_multi_1(sc, addr, SMBATT_CMD_MANUFACTURER_DATA,
	    sc->bif.oeminfo, sizeof(sc->bif.oeminfo)))
		goto out;

	/* XXX check if device was replugged during read? */

	acpi_smbat_info_updated(&sc->bif_lastupdated);
	error = 0;

out:
	if (error == 0)
		memcpy(bif, &sc->bif, sizeof(sc->bif));
	ACPI_SERIAL_END(smbat);
	return (error);
}
