/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Benno Rice.  All rights reserved.
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
 *
 */

#ifndef _IF_SMCVAR_H_
#define	_IF_SMCVAR_H_

struct smc_softc {
	struct ifnet		*smc_ifp;
	device_t		smc_dev;
	struct mtx		smc_mtx;
	u_int			smc_chip;
	u_int			smc_rev;
	u_int			smc_mask;

	/* Resources */
	int			smc_usemem;
	int			smc_reg_rid;
	int			smc_irq_rid;
	struct resource		*smc_reg;
	struct resource		*smc_irq;
	void			*smc_ih;

	/* Tasks */
	struct taskqueue	*smc_tq;
	struct task		smc_intr;
	struct task		smc_rx;
	struct task		smc_tx;
	struct mbuf		*smc_pending;
	struct callout		smc_watchdog;
	
	/* MII support */
	device_t		smc_miibus;
	struct callout		smc_mii_tick_ch;
	void			(*smc_mii_tick)(void *);
	void			(*smc_mii_mediachg)(struct smc_softc *);
	int			(*smc_mii_mediaioctl)(struct smc_softc *,
				    struct ifreq *, u_long);

	/* DMA support */
	void			(*smc_read_packet)(struct smc_softc *,
				    bus_addr_t, uint8_t *, bus_size_t);
	void			*smc_read_arg;
};

int	smc_probe(device_t);
int	smc_attach(device_t);
int	smc_detach(device_t);

int	smc_miibus_readreg(device_t, int, int);
int	smc_miibus_writereg(device_t, int, int, int);
void	smc_miibus_statchg(device_t);

#endif /* _IF_SMCVAR_H_ */
