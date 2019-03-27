/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Luiz Otavio O Souza.
 * Copyright (c) 2011-2012 Stefan Bethke.
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
 *
 * $FreeBSD$
 */
#ifndef	__ARSWITCH_VLANS_H__
#define	__ARSWITCH_VLANS_H__

void ar8xxx_reset_vlans(struct arswitch_softc *);
int ar8xxx_getvgroup(struct arswitch_softc *, etherswitch_vlangroup_t *);
int ar8xxx_setvgroup(struct arswitch_softc *, etherswitch_vlangroup_t *);
int ar8xxx_get_pvid(struct arswitch_softc *, int, int *);
int ar8xxx_set_pvid(struct arswitch_softc *, int, int);

int ar8xxx_flush_dot1q_vlan(struct arswitch_softc *sc);
int ar8xxx_purge_dot1q_vlan(struct arswitch_softc *sc, int vid);
int ar8xxx_get_dot1q_vlan(struct arswitch_softc *sc, uint32_t *ports,
    uint32_t *untagged_ports, int vid);
int ar8xxx_set_dot1q_vlan(struct arswitch_softc *sc, uint32_t ports,
    uint32_t untagged_ports, int vid);
int ar8xxx_get_port_vlan(struct arswitch_softc *sc, uint32_t *ports, int vid);
int ar8xxx_set_port_vlan(struct arswitch_softc *sc, uint32_t ports, int vid);

#endif	/* __ARSWITCH_VLANS_H__ */
