/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1998 Doug Rabson
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

#ifndef _ISA_ISAVAR_H_
#define _ISA_ISAVAR_H_

struct isa_config;
struct isa_pnp_id;
typedef void isa_config_cb(void *arg, struct isa_config *config, int enable);

#include "isa_if.h"
#include <isa/pnpvar.h>

#ifdef _KERNEL

/*
 * ISA devices are partially ordered.  This is to ensure that hardwired
 * devices the BIOS tells us are there appear first, then speculative
 * devices that are sensitive to the probe order, then devices that
 * are hinted to be there, then the most flexible devices which support
 * the ISA bus PNP standard.
 */
#define ISA_ORDER_PNPBIOS	10 /* plug-and-play BIOS inflexible hardware */
#define ISA_ORDER_SENSITIVE	20 /* legacy sensitive hardware */
#define ISA_ORDER_SPECULATIVE	30 /* legacy non-sensitive hardware */
#define ISA_ORDER_PNP		40 /* plug-and-play flexible hardware */

/*
 * Limits on resources that we can manage
 */
#define	ISA_NPORT	50
#define	ISA_NMEM	50
#define	ISA_NIRQ	50
#define	ISA_NDRQ	50

/*
 * Limits on resources the hardware can actually handle
 */
#define ISA_PNP_NPORT	8
#define ISA_PNP_NMEM	4
#define ISA_PNP_NIRQ	2
#define ISA_PNP_NDRQ	2

#define ISADMA_READ	0x00100000
#define ISADMA_WRITE	0
#define ISADMA_RAW	0x00080000
/*
 * Plug and play cards can support a range of resource
 * configurations. This structure is used by the isapnp parser to
 * inform the isa bus about the resource possibilities of the
 * device. Each different alternative should be supplied by calling
 * ISA_ADD_CONFIG().
 */
struct isa_range {
	u_int32_t		ir_start;
	u_int32_t		ir_end;
	u_int32_t		ir_size;
	u_int32_t		ir_align;
};

struct isa_config {
	struct isa_range	ic_mem[ISA_NMEM];
	struct isa_range	ic_port[ISA_NPORT];
	u_int32_t		ic_irqmask[ISA_NIRQ];
	u_int32_t		ic_drqmask[ISA_NDRQ];
	int			ic_nmem;
	int			ic_nport;
	int			ic_nirq;
	int			ic_ndrq;
};

/*
 * Used to build lists of IDs and description strings for PnP drivers.
 */
struct isa_pnp_id {
	u_int32_t		ip_id;
	const char		*ip_desc;
};

enum isa_device_ivars {
	ISA_IVAR_PORT,
	ISA_IVAR_PORT_0 = ISA_IVAR_PORT,
	ISA_IVAR_PORT_1,
	ISA_IVAR_PORTSIZE,
	ISA_IVAR_PORTSIZE_0 = ISA_IVAR_PORTSIZE,
	ISA_IVAR_PORTSIZE_1,
	ISA_IVAR_MADDR,
	ISA_IVAR_MADDR_0 = ISA_IVAR_MADDR,
	ISA_IVAR_MADDR_1,
	ISA_IVAR_MEMSIZE,
	ISA_IVAR_MEMSIZE_0 = ISA_IVAR_MEMSIZE,
	ISA_IVAR_MEMSIZE_1,
	ISA_IVAR_IRQ,
	ISA_IVAR_IRQ_0 = ISA_IVAR_IRQ,
	ISA_IVAR_IRQ_1,
	ISA_IVAR_DRQ,
	ISA_IVAR_DRQ_0 = ISA_IVAR_DRQ,
	ISA_IVAR_DRQ_1,
	ISA_IVAR_VENDORID,
	ISA_IVAR_SERIAL,
	ISA_IVAR_LOGICALID,
	ISA_IVAR_COMPATID,
	ISA_IVAR_CONFIGATTR,
	ISA_IVAR_PNP_CSN,
	ISA_IVAR_PNP_LDN,
	ISA_IVAR_PNPBIOS_HANDLE
};

/*
 * ISA_IVAR_CONFIGATTR bits
 */
#define ISACFGATTR_CANDISABLE	(1 << 0)	/* can be disabled */
#define ISACFGATTR_DYNAMIC	(1 << 1)	/* dynamic configuration */
#define ISACFGATTR_HINTS	(1 << 3)	/* source of config is hints */

#define	ISA_PNP_DESCR "E:pnpid;D:#"
#define ISA_PNP_INFO(t) \
	MODULE_PNP_INFO(ISA_PNP_DESCR, isa, t, t, nitems(t) - 1); \

/*
 * Simplified accessors for isa devices
 */
#define ISA_ACCESSOR(var, ivar, type)					\
	__BUS_ACCESSOR(isa, var, ISA, ivar, type)

ISA_ACCESSOR(port, PORT, int)
ISA_ACCESSOR(portsize, PORTSIZE, int)
ISA_ACCESSOR(irq, IRQ, int)
ISA_ACCESSOR(drq, DRQ, int)
ISA_ACCESSOR(maddr, MADDR, int)
ISA_ACCESSOR(msize, MEMSIZE, int)
ISA_ACCESSOR(vendorid, VENDORID, int)
ISA_ACCESSOR(serial, SERIAL, int)
ISA_ACCESSOR(logicalid, LOGICALID, int)
ISA_ACCESSOR(compatid, COMPATID, int)
ISA_ACCESSOR(configattr, CONFIGATTR, int)
ISA_ACCESSOR(pnp_csn, PNP_CSN, int)
ISA_ACCESSOR(pnp_ldn, PNP_LDN, int)
ISA_ACCESSOR(pnpbios_handle, PNPBIOS_HANDLE, int)

/* Device class for ISA bridges. */
extern devclass_t isab_devclass;

extern intrmask_t isa_irq_pending(void);
extern void	isa_probe_children(device_t dev);

void	isa_dmacascade(int chan);
void	isa_dmadone(int flags, caddr_t addr, int nbytes, int chan);
int	isa_dma_init(int chan, u_int bouncebufsize, int flag);
void	isa_dmastart(int flags, caddr_t addr, u_int nbytes, int chan);
int	isa_dma_acquire(int chan);
void	isa_dma_release(int chan);
int	isa_dmastatus(int chan);
int	isa_dmastop(int chan);
int	isa_dmatc(int chan);

#define isa_dmainit(chan, size) do { \
	if (isa_dma_init(chan, size, M_NOWAIT)) \
		printf("WARNING: isa_dma_init(%d, %ju) failed\n", \
		    (int)(chan), (uintmax_t)(size)); \
	} while (0) 

void	isa_hinted_child(device_t parent, const char *name, int unit);
void	isa_hint_device_unit(device_t bus, device_t child, const char *name,
	    int *unitp);
int	isab_attach(device_t dev);

#endif /* _KERNEL */

#endif /* !_ISA_ISAVAR_H_ */
