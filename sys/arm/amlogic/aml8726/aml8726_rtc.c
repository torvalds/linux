/*-
 * Copyright 2013-2015 John Wehle <john@feith.com>
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

/*
 * Amlogic aml8726 RTC driver.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/clock.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/resource.h>
#include <sys/rman.h>

#include <sys/time.h>

#include <machine/bus.h>
#include <machine/cpu.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <arm/amlogic/aml8726/aml8726_soc.h>

#include "clock_if.h"

/*
 * The RTC initialization various slightly between the different chips.
 *
 *                 aml8726-m1     aml8726-m3     aml8726-m6 (and later)
 *  init-always    true           true           false
 *  xo-init        0x0004         0x3c0a         0x180a
 *  gpo-init       0x100000       0x100000       0x500000
 */

struct aml8726_rtc_init {
	boolean_t	always;
	uint16_t	xo;
	uint32_t	gpo;
};

struct aml8726_rtc_softc {
	device_t		dev;
	struct aml8726_rtc_init	init;
	struct resource	*	res[2];
	struct mtx		mtx;
};

static struct resource_spec aml8726_rtc_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ SYS_RES_IRQ,		0,	RF_ACTIVE },
	{ -1, 0 }
};

#define	AML_RTC_LOCK(sc)		mtx_lock_spin(&(sc)->mtx)
#define	AML_RTC_UNLOCK(sc)		mtx_unlock_spin(&(sc)->mtx)
#define	AML_RTC_LOCK_INIT(sc)		\
    mtx_init(&(sc)->mtx, device_get_nameunit((sc)->dev),	\
    "rtc", MTX_SPIN)
#define	AML_RTC_LOCK_DESTROY(sc)	mtx_destroy(&(sc)->mtx);

#define	AML_RTC_0_REG			0
#define	AML_RTC_SCLK			(1 << 0)
#define	AML_RTC_SDI			(1 << 2)
#define	AML_RTC_SEN			(1 << 1)
#define	AML_RTC_AS			(1 << 17)
#define	AML_RTC_ABSY			(1 << 22)
#define	AML_RTC_IRQ_DIS			(1 << 12)
#define	AML_RTC_1_REG			4
#define	AML_RTC_SDO			(1 << 0)
#define	AML_RTC_SRDY			(1 << 1)
#define	AML_RTC_2_REG			8
#define	AML_RTC_3_REG			12
#define	AML_RTC_MSR_BUSY		(1 << 20)
#define	AML_RTC_MSR_CA			(1 << 17)
#define	AML_RTC_MSR_DURATION_EN		(1 << 16)
#define	AML_RTC_MSR_DURATION_MASK	0xffff
#define	AML_RTC_MSR_DURATION_SHIFT	0
#define	AML_RTC_4_REG			16

#define	AML_RTC_TIME_SREG		0
#define	AML_RTC_GPO_SREG		1
#define	AML_RTC_GPO_LEVEL		(1 << 24)
#define	AML_RTC_GPO_BUSY		(1 << 23)
#define	AML_RTC_GPO_ACTIVE_HIGH		(1 << 22)
#define	AML_RTC_GPO_CMD_MASK		(3 << 20)
#define	AML_RTC_GPO_CMD_SHIFT		20
#define	AML_RTC_GPO_CMD_NOW		(1 << 20)
#define	AML_RTC_GPO_CMD_COUNT		(2 << 20)
#define	AML_RTC_GPO_CMD_PULSE		(3 << 20)
#define	AML_RTC_GPO_CNT_MASK		0xfffff
#define	AML_RTC_GPO_CNT_SHIFT		0

#define	CSR_WRITE_4(sc, reg, val)	bus_write_4((sc)->res[0], reg, (val))
#define	CSR_READ_4(sc, reg)		bus_read_4((sc)->res[0], reg)
#define	CSR_BARRIER(sc, reg)		bus_barrier((sc)->res[0], reg, 4, \
    (BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE))

static int
aml8726_rtc_start_transfer(struct aml8726_rtc_softc *sc)
{
	unsigned i;

	/* idle the serial interface */
	CSR_WRITE_4(sc, AML_RTC_0_REG, (CSR_READ_4(sc, AML_RTC_0_REG) &
	    ~(AML_RTC_SCLK | AML_RTC_SEN | AML_RTC_SDI)));

	CSR_BARRIER(sc, AML_RTC_0_REG);

	/* see if it is ready for a new cycle */
	for (i = 40; i; i--) {
		DELAY(5);
		if ( (CSR_READ_4(sc, AML_RTC_1_REG) & AML_RTC_SRDY) )
			break;
	}

	if (i == 0)
		return (EIO);

	/* start the cycle */
	CSR_WRITE_4(sc, AML_RTC_0_REG, (CSR_READ_4(sc, AML_RTC_0_REG) |
	    AML_RTC_SEN));

	return (0);
}

static inline void
aml8726_rtc_sclk_pulse(struct aml8726_rtc_softc *sc)
{

	DELAY(5);

	CSR_WRITE_4(sc, AML_RTC_0_REG, (CSR_READ_4(sc, AML_RTC_0_REG) |
	    AML_RTC_SCLK));

	CSR_BARRIER(sc, AML_RTC_0_REG);

	DELAY(5);

	CSR_WRITE_4(sc, AML_RTC_0_REG, (CSR_READ_4(sc, AML_RTC_0_REG) &
	    ~AML_RTC_SCLK));

	CSR_BARRIER(sc, AML_RTC_0_REG);
}

static inline void
aml8726_rtc_send_bit(struct aml8726_rtc_softc *sc, unsigned bit)
{

	if (bit) {
		CSR_WRITE_4(sc, AML_RTC_0_REG, (CSR_READ_4(sc, AML_RTC_0_REG) |
		    AML_RTC_SDI));
	} else {
		CSR_WRITE_4(sc, AML_RTC_0_REG, (CSR_READ_4(sc, AML_RTC_0_REG) &
		    ~AML_RTC_SDI));
	}

	aml8726_rtc_sclk_pulse(sc);
}

static inline void
aml8726_rtc_send_addr(struct aml8726_rtc_softc *sc, u_char addr)
{
	unsigned mask;

	for (mask = 1 << 3; mask; mask >>= 1) {
		if (mask == 1) {
			/* final bit indicates read / write mode */
			CSR_WRITE_4(sc, AML_RTC_0_REG,
			    (CSR_READ_4(sc, AML_RTC_0_REG) & ~AML_RTC_SEN));
		}
		aml8726_rtc_send_bit(sc, (addr & mask));
	}
}

static inline void
aml8726_rtc_send_data(struct aml8726_rtc_softc *sc, uint32_t data)
{
	unsigned mask;

	for (mask = 1U << 31; mask; mask >>= 1)
		aml8726_rtc_send_bit(sc, (data & mask));
}

static inline void
aml8726_rtc_recv_data(struct aml8726_rtc_softc *sc, uint32_t *dp)
{
	uint32_t data;
	unsigned i;

	data = 0;

	for (i = 0; i < 32; i++) {
		aml8726_rtc_sclk_pulse(sc);
		data <<= 1;
		data |= (CSR_READ_4(sc, AML_RTC_1_REG) & AML_RTC_SDO) ? 1 : 0;
	}

	*dp = data;
}

static int
aml8726_rtc_sreg_read(struct aml8726_rtc_softc *sc, u_char sreg, uint32_t *val)
{
	u_char addr;
	int error;

	/* read is indicated by lsb = 0 */
	addr = (sreg << 1) | 0;

	error = aml8726_rtc_start_transfer(sc);

	if (error)
		return (error);

	aml8726_rtc_send_addr(sc, addr);
	aml8726_rtc_recv_data(sc, val);

	return (0);
}

static int
aml8726_rtc_sreg_write(struct aml8726_rtc_softc *sc, u_char sreg, uint32_t val)
{
	u_char addr;
	int error;

	/* write is indicated by lsb = 1 */
	addr = (sreg << 1) | 1;

	error = aml8726_rtc_start_transfer(sc);

	if (error)
		return (error);

	aml8726_rtc_send_data(sc, val);
	aml8726_rtc_send_addr(sc, addr);

	return (0);
}

static int
aml8726_rtc_initialize(struct aml8726_rtc_softc *sc)
{
	int error;
	unsigned i;

	/* idle the serial interface */
	CSR_WRITE_4(sc, AML_RTC_0_REG, (CSR_READ_4(sc, AML_RTC_0_REG) &
	    ~(AML_RTC_SCLK | AML_RTC_SEN | AML_RTC_SDI)));

	CSR_BARRIER(sc, AML_RTC_0_REG);

	/* see if it is ready for a new cycle */
	for (i = 40; i; i--) {
		DELAY(5);
		if ( (CSR_READ_4(sc, AML_RTC_1_REG) & AML_RTC_SRDY) )
			break;
	}

	if (sc->init.always == TRUE || (CSR_READ_4(sc, AML_RTC_1_REG) &
	    AML_RTC_SRDY) == 0) {

		/*
		 * The RTC has a 16 bit initialization register.  The upper
		 * bits can be written directly.  The lower bits are written
		 * through a shift register.
		 */

		CSR_WRITE_4(sc, AML_RTC_4_REG, ((sc->init.xo >> 8) & 0xff));

		CSR_WRITE_4(sc, AML_RTC_0_REG,
		    ((CSR_READ_4(sc, AML_RTC_0_REG) & 0xffffff) |
		    ((uint32_t)(sc->init.xo & 0xff) << 24) | AML_RTC_AS |
		    AML_RTC_IRQ_DIS));

		while ((CSR_READ_4(sc, AML_RTC_0_REG) & AML_RTC_ABSY) != 0)
			cpu_spinwait();

		DELAY(2);

		error = aml8726_rtc_sreg_write(sc, AML_RTC_GPO_SREG,
		    sc->init.gpo);

		if (error)
			return (error);
	}

	return (0);
}

static int
aml8726_rtc_check_xo(struct aml8726_rtc_softc *sc)
{
	uint32_t now, previous;
	int i;

	/*
	 * The RTC is driven by a 32.768khz clock meaning it's period
	 * is roughly 30.5 us.  Check that it's working (implying the
	 * RTC could contain a valid value) by enabling count always
	 * and seeing if the value changes after 200 us (per RTC User
	 * Guide ... presumably the extra time is to cover XO startup).
	 */

	CSR_WRITE_4(sc, AML_RTC_3_REG, (CSR_READ_4(sc, AML_RTC_3_REG) |
	    AML_RTC_MSR_CA));

	previous = CSR_READ_4(sc, AML_RTC_2_REG);

	for (i = 0; i < 4; i++) {
		DELAY(50);
		now = CSR_READ_4(sc, AML_RTC_2_REG);
		if (now != previous)
			break;
	}

	CSR_WRITE_4(sc, AML_RTC_3_REG, (CSR_READ_4(sc, AML_RTC_3_REG) &
	    ~AML_RTC_MSR_CA));

	if (now == previous)
		return (EINVAL);

	return (0);
}

static int
aml8726_rtc_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "amlogic,aml8726-rtc"))
		return (ENXIO);

	device_set_desc(dev, "Amlogic aml8726 RTC");

	return (BUS_PROBE_DEFAULT);
}

static int
aml8726_rtc_attach(device_t dev)
{
	struct aml8726_rtc_softc *sc = device_get_softc(dev);

	sc->dev = dev;

	switch (aml8726_soc_hw_rev) {
	case AML_SOC_HW_REV_M3:
		sc->init.always = true;
		sc->init.xo = 0x3c0a;
		sc->init.gpo = 0x100000;
		break;
	case AML_SOC_HW_REV_M6:
	case AML_SOC_HW_REV_M8:
	case AML_SOC_HW_REV_M8B:
		sc->init.always = false;
		sc->init.xo = 0x180a;
		sc->init.gpo = 0x500000;
		break;
	default:
		device_printf(dev, "unsupported SoC\n");
		return (ENXIO);
		/* NOTREACHED */
	}

	if (bus_alloc_resources(dev, aml8726_rtc_spec, sc->res)) {
		device_printf(dev, "can not allocate resources for device\n");
		return (ENXIO);
	}

	aml8726_rtc_initialize(sc);

	if (aml8726_rtc_check_xo(sc) != 0) {
		device_printf(dev, "crystal oscillator check failed\n");

		bus_release_resources(dev, aml8726_rtc_spec, sc->res);

		return (ENXIO);
	}

	AML_RTC_LOCK_INIT(sc);

	clock_register(dev, 1000000);

	return (0);
}

static int
aml8726_rtc_detach(device_t dev)
{

	return (EBUSY);
}

static int
aml8726_rtc_gettime(device_t dev, struct timespec *ts)
{
	struct aml8726_rtc_softc *sc = device_get_softc(dev);
	uint32_t sec;
	int error;

	AML_RTC_LOCK(sc);

	error = aml8726_rtc_sreg_read(sc, AML_RTC_TIME_SREG, &sec);

	AML_RTC_UNLOCK(sc);

	ts->tv_sec = sec;
	ts->tv_nsec = 0;

	return (error);
}

static int
aml8726_rtc_settime(device_t dev, struct timespec *ts)
{
	struct aml8726_rtc_softc *sc = device_get_softc(dev);
	uint32_t sec;
	int error;

	sec = ts->tv_sec;

	/* Accuracy is only one second. */
	if (ts->tv_nsec >= 500000000)
		sec++;

	AML_RTC_LOCK(sc);

	error = aml8726_rtc_sreg_write(sc, AML_RTC_TIME_SREG, sec);

	AML_RTC_UNLOCK(sc);

	return (error);	
}

static device_method_t aml8726_rtc_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		aml8726_rtc_probe),
	DEVMETHOD(device_attach,	aml8726_rtc_attach),
	DEVMETHOD(device_detach,	aml8726_rtc_detach),

	/* Clock interface */
	DEVMETHOD(clock_gettime,	aml8726_rtc_gettime),
	DEVMETHOD(clock_settime,	aml8726_rtc_settime),

	DEVMETHOD_END
};

static driver_t aml8726_rtc_driver = {
	"rtc",
	aml8726_rtc_methods,
	sizeof(struct aml8726_rtc_softc),
};

static devclass_t aml8726_rtc_devclass;

DRIVER_MODULE(rtc, simplebus, aml8726_rtc_driver, aml8726_rtc_devclass, 0, 0);
