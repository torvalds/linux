/*-
 * Copyright (c) 2014 Bjoern A. Zeeb
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract FA8750-11-C-0249
 * ("MRC2"), as part of the DARPA MRC research programme.
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

#ifndef	_DEV_IF_NF10BMACREG_H
#define	_DEV_IF_NF10BMACREG_H

struct nf10bmac_softc {
	struct ifnet		*nf10bmac_ifp;
	struct resource		*nf10bmac_ctrl_res;
	struct resource		*nf10bmac_tx_mem_res;
	struct resource		*nf10bmac_rx_mem_res;
	struct resource		*nf10bmac_intr_res;
	struct resource		*nf10bmac_rx_irq_res;
	void			*nf10bmac_rx_intrhand;
	uint8_t			*nf10bmac_tx_buf;
	device_t		nf10bmac_dev;
	int			nf10bmac_unit;
	int			nf10bmac_ctrl_rid;
	int			nf10bmac_tx_mem_rid;
	int			nf10bmac_rx_mem_rid;
	int			nf10bmac_intr_rid;
	int			nf10bmac_rx_irq_rid;
	int			nf10bmac_if_flags;
	uint32_t		nf10bmac_flags;
#define	NF10BMAC_FLAGS_LINK		0x00000001
	uint8_t			nf10bmac_eth_addr[ETHER_ADDR_LEN];
#ifdef ENABLE_WATCHDOG
	uint16_t		nf10bmac_watchdog_timer;
	struct callout		nf10bmac_tick;
#endif
	struct ifmedia		nf10bmac_media; /* to fake it. */
	struct mtx		nf10bmac_mtx;
};

int	nf10bmac_attach(device_t);
int	nf10bmac_detach_dev(device_t);
void	nf10bmac_detach_resources(device_t);

extern devclass_t nf10bmac_devclass;

#endif /* _DEV_IF_NF10BMACREG_H */

/* end */
