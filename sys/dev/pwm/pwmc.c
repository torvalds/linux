/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2018 Emmanuel Vadot <manu@FreeBSD.org>
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/time.h>

#include <sys/pwm.h>

#include "pwmbus_if.h"
#include "pwm_if.h"

struct pwmc_softc {
	device_t	dev;
	device_t	pdev;
	struct cdev	*pwm_dev;
	char		name[32];
};

static int
pwm_ioctl(struct cdev *dev, u_long cmd, caddr_t data,
    int fflag, struct thread *td)
{
	struct pwmc_softc *sc;
	struct pwm_state state;
	device_t bus;
	int nchannel;
	int rv = 0;

	sc = dev->si_drv1;
	bus = PWM_GET_BUS(sc->pdev);
	if (bus == NULL)
		return (EINVAL);

	switch (cmd) {
	case PWMMAXCHANNEL:
		nchannel = -1;
		rv = PWM_CHANNEL_MAX(sc->pdev, &nchannel);
		bcopy(&nchannel, data, sizeof(nchannel));
		break;
	case PWMSETSTATE:
		bcopy(data, &state, sizeof(state));
		rv = PWMBUS_CHANNEL_CONFIG(bus, state.channel,
		    state.period, state.duty);
		if (rv == 0)
			rv = PWMBUS_CHANNEL_ENABLE(bus, state.channel,
			    state.enable);
		break;
	case PWMGETSTATE:
		bcopy(data, &state, sizeof(state));
		rv = PWMBUS_CHANNEL_GET_CONFIG(bus, state.channel,
		    &state.period, &state.duty);
		if (rv != 0)
			return (rv);
		rv = PWMBUS_CHANNEL_IS_ENABLED(bus, state.channel,
		    &state.enable);
		if (rv != 0)
			return (rv);
		bcopy(&state, data, sizeof(state));
		break;
	}

	return (rv);
}

static struct cdevsw pwm_cdevsw = {
	.d_version	= D_VERSION,
	.d_name		= "pwm",
	.d_ioctl	= pwm_ioctl
};

static int
pwmc_probe(device_t dev)
{

	device_set_desc(dev, "PWM Controller");
	return (0);
}

static int
pwmc_attach(device_t dev)
{
	struct pwmc_softc *sc;
	struct make_dev_args args;

	sc = device_get_softc(dev);
	sc->dev = dev;
	sc->pdev = device_get_parent(dev);

	snprintf(sc->name, sizeof(sc->name), "pwmc%d", device_get_unit(dev));
	make_dev_args_init(&args);
	args.mda_flags = MAKEDEV_CHECKNAME | MAKEDEV_WAITOK;
	args.mda_devsw = &pwm_cdevsw;
	args.mda_uid = UID_ROOT;
	args.mda_gid = GID_OPERATOR;
	args.mda_mode = 0600;
	args.mda_si_drv1 = sc;
	if (make_dev_s(&args, &sc->pwm_dev, "%s", sc->name) != 0) {
		device_printf(dev, "Failed to make PWM device\n");
		return (ENXIO);
	}
	return (0);
}

static int
pwmc_detach(device_t dev)
{

	return (0);
}

static device_method_t pwmc_methods[] = {
	/* device_if */
	DEVMETHOD(device_probe, pwmc_probe),
	DEVMETHOD(device_attach, pwmc_attach),
	DEVMETHOD(device_detach, pwmc_detach),

	DEVMETHOD_END
};

driver_t pwmc_driver = {
	"pwmc",
	pwmc_methods,
	sizeof(struct pwmc_softc),
};
devclass_t pwmc_devclass;

DRIVER_MODULE(pwmc, pwm, pwmc_driver, pwmc_devclass, 0, 0);
MODULE_VERSION(pwmc, 1);
