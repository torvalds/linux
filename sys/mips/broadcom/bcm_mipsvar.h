/*-
 * Copyright (c) 2017 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Landon Fuller under sponsorship from
 * the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 * 
 * $FreeBSD$
 */

#ifndef _MIPS_BROADCOM_BCM_MIPSVAR_H_
#define _MIPS_BROADCOM_BCM_MIPSVAR_H_

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/intr.h>
#include <sys/lock.h>

#include <machine/intr.h>

DECLARE_CLASS(bcm_mips_driver);

struct bcm_mips_irqsrc;
struct bcm_mips_softc;

#define	BCM_MIPS_NINTR		32			/**< maximum number of addressable backplane interrupt vectors */
#define	BCM_MIPS_IRQ_SHARED	0			/**< MIPS CPU IRQ reserved for shared interrupt handling */
#define	INTR_MAP_DATA_BCM_MIPS	INTR_MAP_DATA_PLAT_2	/**< Broadcom MIPS PIC interrupt map data type */


int	bcm_mips_attach(device_t dev, u_int num_cpuirqs, u_int timer_irq,
	    driver_filter_t filter);
int	bcm_mips_detach(device_t dev);

/**
 * Broadcom MIPS PIC interrupt map data.
 */
struct bcm_mips_intr_map_data {
	struct intr_map_data	mdata;
	u_int			ivec;	/**< bus interrupt vector */
};

/**
 * Nested MIPS CPU interrupt handler state.
 */
struct bcm_mips_cpuirq {
	struct bcm_mips_softc	*sc;		/**< driver instance state, or NULL if uninitialized. */
	u_int			 mips_irq;	/**< mips hardware interrupt number (relative to NSOFT_IRQ) */
	int			 irq_rid;	/**< mips IRQ resource id, or -1 if this entry is unavailable */
	struct resource		*irq_res;	/**< mips interrupt resource */
	void			*irq_cookie;	/**< mips interrupt handler cookie, or NULL */
	struct bcm_mips_irqsrc	*isrc_solo;	/**< solo isrc assigned to this interrupt, or NULL */
	u_int			 refs;		/**< isrc consumer refcount */
};

/**
 * Broadcom MIPS PIC interrupt source definition.
 */
struct bcm_mips_irqsrc {
	struct intr_irqsrc	 isrc;
	u_int			 ivec;		/**< bus interrupt vector */
	u_int			 refs;		/**< active reference count */
	struct bcm_mips_cpuirq	*cpuirq;	/**< assigned MIPS HW IRQ, or NULL if no assignment */
};

/**
 * bcm_mips driver instance state. Must be first member of all subclass
 * softc structures.
 */
struct bcm_mips_softc {
	device_t		 dev;
	struct bcm_mips_cpuirq	 cpuirqs[NREAL_IRQS];	/**< nested CPU IRQ handlers */
	u_int			 num_cpuirqs;		/**< number of nested CPU IRQ handlers */
	u_int			 timer_irq;		/**< CPU timer IRQ */
	struct bcm_mips_irqsrc	 isrcs[BCM_MIPS_NINTR];
	struct mtx		 mtx;
};


#define	BCM_MIPS_IVEC_MASK(_isrc)	(1 << ((_isrc)->ivec))

#define	BCM_MIPS_LOCK_INIT(sc) \
	mtx_init(&(sc)->mtx, device_get_nameunit((sc)->dev), \
	    "bhnd mips driver lock", MTX_DEF)
#define	BCM_MIPS_LOCK(sc)		mtx_lock(&(sc)->mtx)
#define	BCM_MIPS_UNLOCK(sc)		mtx_unlock(&(sc)->mtx)
#define	BCM_MIPS_LOCK_ASSERT(sc, what)	mtx_assert(&(sc)->mtx, what)
#define	BCM_MIPS_LOCK_DESTROY(sc)	mtx_destroy(&(sc)->mtx)

#endif /* _MIPS_BROADCOM_BCM_MIPSVAR_H_ */
