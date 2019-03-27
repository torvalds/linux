/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1995, David Greenman
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ed.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/socket.h>
#include <sys/kernel.h>

#include <sys/module.h>
#include <sys/bus.h>
#include <machine/bus.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <net/if_media.h>
#include <net/if_mib.h>

#include <isa/isavar.h>

#include <dev/ed/if_edvar.h>
#include <dev/ed/if_edreg.h>

static int ed_isa_probe(device_t);
static int ed_isa_attach(device_t);

static struct isa_pnp_id ed_ids[] = {
	{ 0x0131d805,	NULL },		/* ANX3101 */
	{ 0x4cf48906,	NULL },		/* ATIf44c */
	{ 0x01200507,	NULL },		/* AXE2001 */
	{ 0x0115180e,	NULL },		/* CPX1501 */
	{ 0x0090252a,	NULL },		/* JQE9000 */
	{ 0x0020832e,	NULL },		/* KTC2000 */
	{ 0xd680d041,	NULL },		/* PNP80d6 */
	{ 0x6081d041,	NULL },		/* PNP8160 */
	{ 0x19808c4a,	NULL },		/* RTL8019 */
	{ 0x1684a34d,	NULL },		/* SMC8416 */
	{ 0x1980635e,	NULL },		/* WSC8019 */
	{ 0,		NULL }
};

static int
ed_isa_probe_Novell(device_t dev)
{
	struct ed_softc *sc = device_get_softc(dev);
	int flags = device_get_flags(dev);
	int err;

	err = ed_probe_Novell(dev, 0, flags);
	if (err)
		return err;
	ed_Novell_read_mac(sc);
	/*
	 * Final sanity check for Gateway Ethernet cards before
	 * believing that they really are Gateway AT.
	 * XXX I think this is stale.
	 */
	if ((ED_FLAGS_GETTYPE(flags) == ED_FLAGS_GWETHER) &&
	    (sc->enaddr[2] == 0x86)) {
		sc->type_str = "Gateway AT";
	}
	
	return (0);
}

static int
ed_isa_probe(device_t dev)
{
	struct ed_softc *sc = device_get_softc(dev);
	int flags = device_get_flags(dev);
	int error = 0;

	/* Check isapnp ids */
	error = ISA_PNP_PROBE(device_get_parent(dev), dev, ed_ids);

	/* If the card had a PnP ID that didn't match any we know about */
	if (error == ENXIO)
		goto end;

	/* If we had some other problem. */
	if (!(error == 0 || error == ENOENT))
		goto end;

	/* Heuristic probes */

	error = ed_probe_WD80x3(dev, 0, flags);
	if (error == 0)
		goto end;
	ed_release_resources(dev);

	error = ed_probe_RTL80x9(dev, 0, flags);
	if (error == 0) {
		ed_Novell_read_mac(sc);
		goto end;
	}
	ed_release_resources(dev);
	
#ifdef ED_3C503
	error = ed_probe_3Com(dev, 0, flags);
	if (error == 0)
		goto end;
	ed_release_resources(dev);
#endif

#ifdef ED_SIC
	error = ed_probe_SIC(dev, 0, flags);
	if (error == 0)
		goto end;
	ed_release_resources(dev);
#endif
	error = ed_isa_probe_Novell(dev);
	if (error == 0)
		goto end;
	ed_release_resources(dev);

#ifdef ED_HPP
	error = ed_probe_HP_pclanp(dev, 0, flags);
	if (error == 0)
		goto end;
	ed_release_resources(dev);
#endif
end:
	if (error == 0)
		error = ed_alloc_irq(dev, 0, 0);

	ed_release_resources(dev);
	return (error);
}

static int
ed_isa_attach(device_t dev)
{
	struct ed_softc *sc = device_get_softc(dev);
	int error;
	
	if (sc->port_used > 0)
		ed_alloc_port(dev, 0, sc->port_used);
	if (sc->mem_used)
		ed_alloc_memory(dev, 0, sc->mem_used);
	ed_alloc_irq(dev, 0, 0);

	if (sc->sc_media_ioctl == NULL)
		ed_gen_ifmedia_init(sc);
	error = ed_attach(dev);
	if (error) {
		ed_release_resources(dev);
		return (error);
	}
	error = bus_setup_intr(dev, sc->irq_res, INTR_TYPE_NET | INTR_MPSAFE,
	    NULL, edintr, sc, &sc->irq_handle);
	if (error)
		ed_release_resources(dev);
	return (error);
}

static device_method_t ed_isa_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ed_isa_probe),
	DEVMETHOD(device_attach,	ed_isa_attach),
	DEVMETHOD(device_detach,	ed_detach),

	{ 0, 0 }
};

static driver_t ed_isa_driver = {
	"ed",
	ed_isa_methods,
	sizeof(struct ed_softc)
};

DRIVER_MODULE(ed, isa, ed_isa_driver, ed_devclass, 0, 0);
MODULE_DEPEND(ed, isa, 1, 1, 1);
MODULE_DEPEND(ed, ether, 1, 1, 1);
ISA_PNP_INFO(ed_ids);
