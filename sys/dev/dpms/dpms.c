/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2008 Yahoo!, Inc.
 * All rights reserved.
 * Written by: John Baldwin <jhb@FreeBSD.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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
 * Copyright (c) 2004 Benjamin Close <Benjamin.Close@clearchain.com>
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
 * Support for managing the display via DPMS for suspend/resume.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/libkern.h>
#include <sys/module.h>

#include <compat/x86bios/x86bios.h>

/*
 * VESA DPMS States 
 */
#define DPMS_ON		0x00
#define DPMS_STANDBY	0x01
#define DPMS_SUSPEND	0x02
#define DPMS_OFF	0x04
#define DPMS_REDUCEDON	0x08

#define	VBE_DPMS_FUNCTION	0x4F10
#define	VBE_DPMS_GET_SUPPORTED_STATES 0x00
#define	VBE_DPMS_GET_STATE	0x02
#define	VBE_DPMS_SET_STATE	0x01
#define VBE_MAJORVERSION_MASK	0x0F
#define VBE_MINORVERSION_MASK	0xF0

struct dpms_softc {
	int	dpms_supported_states;
	int	dpms_initial_state;
};

static int	dpms_attach(device_t);
static int	dpms_detach(device_t);
static int	dpms_get_supported_states(int *);
static int	dpms_get_current_state(int *);
static void	dpms_identify(driver_t *, device_t);
static int	dpms_probe(device_t);
static int	dpms_resume(device_t);
static int	dpms_set_state(int);
static int	dpms_suspend(device_t);

static device_method_t dpms_methods[] = {
	DEVMETHOD(device_identify,	dpms_identify),
	DEVMETHOD(device_probe,		dpms_probe),
	DEVMETHOD(device_attach,	dpms_attach),
	DEVMETHOD(device_detach,	dpms_detach),
	DEVMETHOD(device_suspend,	dpms_suspend),
	DEVMETHOD(device_resume,	dpms_resume),
	{ 0, 0 }
};

static driver_t dpms_driver = {
	"dpms",
	dpms_methods,
	sizeof(struct dpms_softc),
};

static devclass_t dpms_devclass;

DRIVER_MODULE(dpms, vgapm, dpms_driver, dpms_devclass, NULL, NULL);
MODULE_DEPEND(dpms, x86bios, 1, 1, 1);

static void
dpms_identify(driver_t *driver, device_t parent)
{

	if (x86bios_match_device(0xc0000, device_get_parent(parent)))
		device_add_child(parent, "dpms", 0);
}

static int
dpms_probe(device_t dev)
{
	int error, states;

	error = dpms_get_supported_states(&states);
	if (error)
		return (error);
	device_set_desc(dev, "DPMS suspend/resume");
	device_quiet(dev);
	return (BUS_PROBE_DEFAULT);
}

static int
dpms_attach(device_t dev)
{
	struct dpms_softc *sc;
	int error;

	sc = device_get_softc(dev);
	error = dpms_get_supported_states(&sc->dpms_supported_states);
	if (error)
		return (error);
	error = dpms_get_current_state(&sc->dpms_initial_state);
	return (error);
}

static int
dpms_detach(device_t dev)
{

	return (0);
}

static int
dpms_suspend(device_t dev)
{
	struct dpms_softc *sc;

	sc = device_get_softc(dev);
	if ((sc->dpms_supported_states & DPMS_OFF) != 0)
		dpms_set_state(DPMS_OFF);
	return (0);
}

static int
dpms_resume(device_t dev)
{
	struct dpms_softc *sc;

	sc = device_get_softc(dev);
	dpms_set_state(sc->dpms_initial_state);
	return (0);
}

static int
dpms_call_bios(int subfunction, int *bh)
{
	x86regs_t regs;

	if (x86bios_get_intr(0x10) == 0)
		return (ENXIO);

	x86bios_init_regs(&regs);
	regs.R_AX = VBE_DPMS_FUNCTION;
	regs.R_BL = subfunction;
	regs.R_BH = *bh;
	x86bios_intr(&regs, 0x10);

	if (regs.R_AX != 0x004f)
		return (ENXIO);

	*bh = regs.R_BH;

	return (0);
}

static int
dpms_get_supported_states(int *states)
{

	*states = 0;
	return (dpms_call_bios(VBE_DPMS_GET_SUPPORTED_STATES, states));
}

static int
dpms_get_current_state(int *state)
{

	*state = 0;
	return (dpms_call_bios(VBE_DPMS_GET_STATE, state));
}

static int
dpms_set_state(int state)
{

	return (dpms_call_bios(VBE_DPMS_SET_STATE, &state));
}
