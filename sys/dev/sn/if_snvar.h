/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 M. Warner Losh <imp@village.org> 
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
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _IF_SNVAR_H
#define _IF_SNVAR_H

#include <net/if_arp.h>

struct sn_softc {
	struct ifnet    *ifp;
	struct mtx sc_mtx;
	struct callout watchdog;
	int		timer;
	int             pages_wanted;	/* Size of outstanding MMU ALLOC */
	int             intr_mask;	/* Most recently set interrupt mask */
	device_t	dev;
	void		*intrhand;
	struct resource *irq_res;
	int		irq_rid;
	struct resource	*port_res;
	int		port_rid;
	struct resource	*modem_res;	/* Extra resource for modem */
	int		modem_rid;
};

int	sn_probe(device_t);
int	sn_attach(device_t);
int	sn_detach(device_t);
void	sn_intr(void *);

int	sn_activate(device_t);
void	sn_deactivate(device_t);

#define CSR_READ_1(sc, off) (bus_read_1((sc)->port_res, off))
#define CSR_READ_2(sc, off) (bus_read_2((sc)->port_res, off))
#define CSR_WRITE_1(sc, off, val) \
	bus_write_1((sc)->port_res, off, val)
#define CSR_WRITE_2(sc, off, val) \
	bus_write_2((sc)->port_res, off, val)
#define CSR_WRITE_MULTI_1(sc, off, addr, count) \
	bus_write_multi_1((sc)->port_res, off, addr, count)
#define CSR_WRITE_MULTI_2(sc, off, addr, count) \
	bus_write_multi_2((sc)->port_res, off, addr, count)
#define CSR_READ_MULTI_1(sc, off, addr, count) \
	bus_read_multi_1((sc)->port_res, off, addr, count)
#define CSR_READ_MULTI_2(sc, off, addr, count) \
	bus_read_multi_2((sc)->port_res, off, addr, count)

#define SN_LOCK(_sc)		mtx_lock(&(_sc)->sc_mtx)
#define	SN_UNLOCK(_sc)		mtx_unlock(&(_sc)->sc_mtx)
#define SN_LOCK_INIT(_sc) \
	mtx_init(&_sc->sc_mtx, device_get_nameunit(_sc->dev), \
	    MTX_NETWORK_LOCK, MTX_DEF)
#define SN_LOCK_DESTROY(_sc)	mtx_destroy(&_sc->sc_mtx);
#define SN_ASSERT_LOCKED(_sc)	mtx_assert(&_sc->sc_mtx, MA_OWNED);
#define SN_ASSERT_UNLOCKED(_sc)	mtx_assert(&_sc->sc_mtx, MA_NOTOWNED);

#endif /* _IF_SNVAR_H */
