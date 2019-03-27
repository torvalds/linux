/*-
 * Copyright (c) 2011 Justin Hibbits
 * Copyright (c) 2010 Andreas Tobler
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/systm.h>
#include <sys/module.h>
#include <sys/callout.h>
#include <sys/conf.h>
#include <sys/cpu.h>
#include <sys/ctype.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/limits.h>
#include <sys/reboot.h>
#include <sys/rman.h>
#include <sys/sysctl.h>
#include <sys/unistd.h>

#include <machine/bus.h>
#include <machine/md_var.h>

#include <dev/iicbus/iicbus.h>
#include <dev/iicbus/iiconf.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <powerpc/powermac/powermac_thermal.h>

struct adm1030_softc {
	struct pmac_fan fan;
	device_t	sc_dev;
	struct intr_config_hook enum_hook;
	uint32_t	sc_addr;
	int		sc_pwm;
};

/* Regular bus attachment functions */
static int	adm1030_probe(device_t);
static int	adm1030_attach(device_t);

/* Utility functions */
static void	adm1030_start(void *xdev);
static int	adm1030_write_byte(device_t dev, uint32_t addr, uint8_t reg, uint8_t buf);
static int	adm1030_set(struct adm1030_softc *fan, int pwm);
static int	adm1030_sysctl(SYSCTL_HANDLER_ARGS);

static device_method_t adm1030_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, adm1030_probe),
	DEVMETHOD(device_attach, adm1030_attach),
	{0, 0},
};

static driver_t	adm1030_driver = {
	"adm1030",
	adm1030_methods,
	sizeof(struct adm1030_softc)
};

static devclass_t adm1030_devclass;

DRIVER_MODULE(adm1030, iicbus, adm1030_driver, adm1030_devclass, 0, 0);

static int
adm1030_write_byte(device_t dev, uint32_t addr, uint8_t reg, uint8_t byte)
{
	unsigned char	buf[4];
	int try = 0;

	struct iic_msg	msg[] = {
		{addr, IIC_M_WR, 0, buf}
	};

	msg[0].len = 2;
	buf[0] = reg;
	buf[1] = byte;

	for (;;)
	{
		if (iicbus_transfer(dev, msg, 1) == 0)
			return (0);

		if (++try > 5) {
			device_printf(dev, "iicbus write failed\n");
			return (-1);
		}
		pause("adm1030_write_byte", hz);
	}
}

static int
adm1030_probe(device_t dev)
{
	const char     *name, *compatible;
	struct adm1030_softc *sc;
	phandle_t	handle;
	phandle_t	thermostat;

	name = ofw_bus_get_name(dev);
	compatible = ofw_bus_get_compat(dev);
	handle = ofw_bus_get_node(dev);

	if (!name)
		return (ENXIO);

	if (strcmp(name, "fan") != 0 || strcmp(compatible, "adm1030") != 0)
		return (ENXIO);

	/* This driver can only be used if there's an associated temp sensor. */
	if (OF_getprop(handle, "platform-getTemp", &thermostat, sizeof(thermostat)) < 0)
		return (ENXIO);

	sc = device_get_softc(dev);
	sc->sc_dev = dev;
	sc->sc_addr = iicbus_get_addr(dev);

	device_set_desc(dev, "G4 MDD Fan driver");

	return (0);
}

static int
adm1030_attach(device_t dev)
{
	struct adm1030_softc *sc;
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *tree;

	sc = device_get_softc(dev);

	sc->enum_hook.ich_func = adm1030_start;
	sc->enum_hook.ich_arg = dev;

	/*
	 * Wait until interrupts are available, which won't be until the openpic is
	 * intialized.
	 */

	if (config_intrhook_establish(&sc->enum_hook) != 0)
		return (ENOMEM);

	ctx = device_get_sysctl_ctx(dev);
	tree = device_get_sysctl_tree(dev);
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO, "pwm",
			CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_MPSAFE, dev,
			0, adm1030_sysctl, "I", "Fan PWM Rate");

	return (0);
}

static void
adm1030_start(void *xdev)
{
	struct adm1030_softc *sc;

	device_t	dev = (device_t) xdev;

	sc = device_get_softc(dev);

	/* Start the adm1030 device. */
	adm1030_write_byte(sc->sc_dev, sc->sc_addr, 0x1, 0x1);
	adm1030_write_byte(sc->sc_dev, sc->sc_addr, 0x0, 0x95);
	adm1030_write_byte(sc->sc_dev, sc->sc_addr, 0x23, 0x91);

	/* Use the RPM fields as PWM duty cycles. */
	sc->fan.min_rpm = 0;
	sc->fan.max_rpm = 0x0F;
	sc->fan.default_rpm = 2;

	strcpy(sc->fan.name, "MDD Case fan");
	sc->fan.zone = 0;
	sc->fan.read = NULL;
	sc->fan.set = (int (*)(struct pmac_fan *, int))adm1030_set;
	config_intrhook_disestablish(&sc->enum_hook);

	pmac_thermal_fan_register(&sc->fan);
}

static int adm1030_set(struct adm1030_softc *fan, int pwm)
{
	/* Clamp the PWM to 0-0xF, one nibble. */
	if (pwm > 0xF)
		pwm = 0xF;
	if (pwm < 0)
		pwm = 0;

	if (adm1030_write_byte(fan->sc_dev, fan->sc_addr, 0x22, pwm) < 0)
		return (-1);

	fan->sc_pwm = pwm;
	return (0);
}

static int
adm1030_sysctl(SYSCTL_HANDLER_ARGS)
{
	device_t adm1030;
	struct adm1030_softc *sc;
	int pwm, error;

	adm1030 = arg1;
	sc = device_get_softc(adm1030);

	pwm = sc->sc_pwm;

	error = sysctl_handle_int(oidp, &pwm, 0, req);

	if (error || !req->newptr)
		return (error);

	return (adm1030_set(sc, pwm));
}
