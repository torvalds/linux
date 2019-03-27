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

#ifndef _IF_CSVAR_H
#define _IF_CSVAR_H

/*
 * cs_softc: per line info and status
 */
struct cs_softc {
	/* Ethernet common code */
	struct ifnet *ifp;
	device_t dev;

	/* Configuration words from EEPROM */
	int auto_neg_cnf;               /* AutoNegotitation configuration */
	int adapter_cnf;                /* Adapter configuration */
	int isa_config;                 /* ISA configuration */
	int chip_type;			/* Type of chip */

	u_char	enaddr[ETHER_ADDR_LEN];

	struct ifmedia media;		/* Media information */

	int     port_rid;		/* resource id for port range */
	struct resource* port_res;	/* resource for port range */
	int     irq_rid;		/* resource id for irq */
	struct resource* irq_res;	/* resource for irq */
	void*   irq_handle;		/* handle for irq handler */

	int	flags;
#define	CS_NO_IRQ	0x1
	int	send_cmd;
	int	line_ctl;		/* */
	int	send_underrun;
	void	*recv_ring;

	unsigned char *buffer;
	int buf_len;
	struct mtx lock;
	struct callout timer;
	int	tx_timeout;
};

#define	CS_LOCK(sc)		mtx_lock(&(sc)->lock)
#define	CS_UNLOCK(sc)		mtx_unlock(&(sc)->lock)
#define	CS_ASSERT_LOCKED(sc)	mtx_assert(&(sc)->lock, MA_OWNED)

int	cs_alloc_port(device_t dev, int rid, int size);
int	cs_alloc_irq(device_t dev, int rid);
int	cs_attach(device_t dev);
int	cs_cs89x0_probe(device_t dev);
int	cs_detach(device_t dev);
void	cs_release_resources(device_t dev);

#endif /* _IF_CSVAR_H */
