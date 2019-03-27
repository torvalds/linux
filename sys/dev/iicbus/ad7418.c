/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 Sam Leffler.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
/*
 * Analog Devices AD7418 chip sitting on the I2C bus.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/resource.h>
#include <sys/rman.h>
#include <sys/sysctl.h>
#include <sys/sx.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/cpufunc.h>
#include <machine/frame.h>
#include <machine/resource.h>
#include <machine/intr.h>

#include <dev/iicbus/iiconf.h>

#include "iicbus_if.h"

#define	IIC_M_WR	0	/* write operation */

#define	AD7418_ADDR	0x50	/* slave address */

#define	AD7418_TEMP	0	/* Temperature Value (r/o) */
#define	AD7418_CONF	1	/* Config Register (r/w) */
#define	AD7418_CONF_SHUTDOWN	0x01
#define	AD7418_CONF_CHAN	0xe0	/* channel select mask */
#define	AD7418_CHAN_TEMP	0x00	/* temperature channel */
#define	AD7418_CHAN_VOLT	0x80	/* voltage channel */
#define	AD7418_THYST	2	/* Thyst Setpoint (r/o) */
#define	AD7418_TOTI	3	/* Toti Setpoint */
#define	AD7418_VOLT	4	/* ADC aka Voltage (r/o) */
#define	AD7418_CONF2	5	/* Config2 Register (r/w) */

struct ad7418_softc {
	device_t	sc_dev;
	struct sx	sc_lock;
	int		sc_curchan;	/* current channel */
	int		sc_curtemp;
	int		sc_curvolt;
	int		sc_lastupdate;	/* in ticks */
};

static void ad7418_update(struct ad7418_softc *);
static int ad7418_read_1(device_t dev, int reg);
static int ad7418_write_1(device_t dev, int reg, int v);

static int
ad7418_probe(device_t dev)
{
	/* XXX really probe? */
	device_set_desc(dev, "Analog Devices AD7418 ADC");
	return (BUS_PROBE_NOWILDCARD);
}

static int
ad7418_sysctl_temp(SYSCTL_HANDLER_ARGS)
{
	struct ad7418_softc *sc = arg1;
	int temp;

	sx_xlock(&sc->sc_lock);
	ad7418_update(sc);
	temp = (sc->sc_curtemp / 64) * 25;
	sx_xunlock(&sc->sc_lock);
	return sysctl_handle_int(oidp, &temp, 0, req);
}

static int
ad7418_sysctl_voltage(SYSCTL_HANDLER_ARGS)
{
	struct ad7418_softc *sc = arg1;
	int volt;

	sx_xlock(&sc->sc_lock);
	ad7418_update(sc);
	volt = (sc->sc_curvolt >> 6) * 564 / 10;
	sx_xunlock(&sc->sc_lock);
	return sysctl_handle_int(oidp, &volt, 0, req);
}

static int
ad7418_attach(device_t dev)
{
	struct ad7418_softc *sc = device_get_softc(dev);
	struct sysctl_ctx_list *ctx = device_get_sysctl_ctx(dev);
	struct sysctl_oid *tree = device_get_sysctl_tree(dev);
	int conf;

	sc->sc_dev = dev;
	sc->sc_lastupdate = ticks - hz;

	sx_init(&sc->sc_lock, "ad7418");

	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		"temp", CTLTYPE_INT | CTLFLAG_RD, sc, 0,
		ad7418_sysctl_temp, "I", "operating temperature");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		"volt", CTLTYPE_INT | CTLFLAG_RD, sc, 0,
		ad7418_sysctl_voltage, "I", "input voltage");

	/* enable chip if configured in shutdown mode */
	conf = ad7418_read_1(dev, AD7418_CONF);
	if (conf >= 0 && (conf & AD7418_CONF_SHUTDOWN))
		ad7418_write_1(dev, AD7418_CONF, conf &~ AD7418_CONF_SHUTDOWN);

	return (0);
}

static int
ad7418_read_1(device_t dev, int reg) 
{
	uint8_t addr = reg;
	uint8_t data[1];
	struct iic_msg msgs[2] = {
	     { AD7418_ADDR, IIC_M_WR, 1, &addr },
	     { AD7418_ADDR, IIC_M_RD, 1, data },
	};
	return iicbus_transfer(dev, msgs, 2) != 0 ? -1 : data[0];
}

static int
ad7418_write_1(device_t dev, int reg, int v) 
{
	/* NB: register pointer precedes actual data */
	uint8_t data[2];
	struct iic_msg msgs[1] = {
	     { AD7418_ADDR, IIC_M_WR, 2, data },
	};
	data[0] = reg;
	data[1] = v & 0xff;
	return iicbus_transfer(dev, msgs, 1);
}

static void
ad7418_set_channel(struct ad7418_softc *sc, int chan)
{
	if (sc->sc_curchan == chan)
		return;
	ad7418_write_1(sc->sc_dev, AD7418_CONF, 
	    (ad7418_read_1(sc->sc_dev, AD7418_CONF) &~ AD7418_CONF_CHAN)|chan);
	sc->sc_curchan = chan;
#if 0
	/*
	 * NB: Linux driver delays here but chip data sheet
	 *     says nothing and things appear to work fine w/o
	 *     a delay on channel change.
	 */
	/* let channel change settle, 1 tick should be 'nuf (need ~1ms) */
	tsleep(sc, 0, "ad7418", hz/1000);
#endif
}

static int
ad7418_read_2(device_t dev, int reg) 
{
	uint8_t addr = reg;
	uint8_t data[2];
	struct iic_msg msgs[2] = {
	     { AD7418_ADDR, IIC_M_WR, 1, &addr },
	     { AD7418_ADDR, IIC_M_RD, 2, data },
	};
	/* NB: D15..D8 precede D7..D0 per data sheet (Fig 12) */
	return iicbus_transfer(dev, msgs, 2) != 0 ?
		-1 : ((data[0] << 8) | data[1]);
}

static void
ad7418_update(struct ad7418_softc *sc)
{
	int v;

	sx_assert(&sc->sc_lock, SA_XLOCKED);
	/* NB: no point in updating any faster than the chip */
	if (ticks - sc->sc_lastupdate > hz) {
		ad7418_set_channel(sc, AD7418_CHAN_TEMP);
		v = ad7418_read_2(sc->sc_dev, AD7418_TEMP);
		if (v >= 0)
			sc->sc_curtemp = v;
		ad7418_set_channel(sc, AD7418_CHAN_VOLT);
		v = ad7418_read_2(sc->sc_dev, AD7418_VOLT);
		if (v >= 0)
			sc->sc_curvolt = v;
		sc->sc_lastupdate = ticks;
	}
}

static device_method_t ad7418_methods[] = {
	DEVMETHOD(device_probe,		ad7418_probe),
	DEVMETHOD(device_attach,	ad7418_attach),

	DEVMETHOD_END
};

static driver_t ad7418_driver = {
	"ad7418",
	ad7418_methods,
	sizeof(struct ad7418_softc),
};
static devclass_t ad7418_devclass;

DRIVER_MODULE(ad7418, iicbus, ad7418_driver, ad7418_devclass, 0, 0);
MODULE_VERSION(ad7418, 1);
MODULE_DEPEND(ad7418, iicbus, 1, 1, 1);
