/*-
 * Copyright (c) 2015-2016 Landon Fuller <landonf@FreeBSD.org>
 * All rights reserved.
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

#ifndef _BHND_PWRCTL_BHND_PWRCTLVAR_H_
#define _BHND_PWRCTL_BHND_PWRCTLVAR_H_

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/queue.h>

#include <dev/bhnd/bhnd.h>

uint32_t	bhnd_pwrctl_clock_rate(uint32_t pll_type, uint32_t n,
		    uint32_t m);

bus_size_t	bhnd_pwrctl_si_clkreg_m(const struct bhnd_chipid *cid,
		    uint8_t pll_type, uint32_t *fixed_hz);
uint32_t	bhnd_pwrctl_si_clock_rate(const struct bhnd_chipid *cid,
		    uint32_t pll_type, uint32_t n, uint32_t m);

bus_size_t	bhnd_pwrctl_cpu_clkreg_m(const struct bhnd_chipid *cid,
		    uint8_t pll_type, uint32_t *fixed_hz);
uint32_t	bhnd_pwrctl_cpu_clock_rate(const struct bhnd_chipid *cid,
		    uint32_t pll_type, uint32_t n, uint32_t m);

/**
 * bhnd pwrctl device quirks.
 */
enum {
	/** No quirks */
	PWRCTL_QUIRK_NONE		= 0,

	/**
	 * Early ChipCommon revisions do not support dynamic clock control
	 */
	PWRCTL_QUIRK_FIXED_CLK		= (1 << 0),

	/**
	 * On PCI (not PCIe) devices, early ChipCommon revisions
	 * (rev <= 5) vend xtal/pll and clock config registers via the PCI
	 * config space.
	 * 
	 * Dynamic clock control is not supported on these devices.
	 */
	PWRCTL_QUIRK_PCICLK_CTL		= (1 << 1) | PWRCTL_QUIRK_FIXED_CLK,
	
	
	/**
	 * On earliy BCM4311, BCM4321, and BCM4716 PCI(e) devices, no ALP
	 * clock is available, and the HT clock must be enabled.
	 */
	PWRCTL_QUIRK_FORCE_HT		= (1 << 2),

	/**
	 * ChipCommon revisions 6-9 use the slowclk register layout.
	 */
	PWRCTL_QUIRK_SLOWCLK_CTL	= (1 << 3),
	
	/**
	 * ChipCommon revisions 10-19 support the instaclk register layout.
	 */
	PWRCTL_QUIRK_INSTACLK_CTL	= (1 << 4),

};

/**
 * device clock reservation.
 */
struct bhnd_pwrctl_clkres {
	device_t	owner;	/**< bhnd(4) device holding this reservation */
	bhnd_clock	clock;	/**< requested clock */
	STAILQ_ENTRY(bhnd_pwrctl_clkres) cr_link;
};


/**
 * bhnd pwrctl driver instance state.
 */
struct bhnd_pwrctl_softc {
	device_t		 dev;
	uint32_t		 quirks;

	device_t		 chipc_dev;	/**< core device */
	struct bhnd_resource	*res;		/**< core register block. */

	struct mtx		 mtx;		/**< state mutex */

	/** active clock reservations */
	STAILQ_HEAD(, bhnd_pwrctl_clkres) clkres_list;
};

#define	PWRCTL_LOCK_INIT(sc) \
	mtx_init(&(sc)->mtx, device_get_nameunit((sc)->dev), \
	    "bhnd pwrctl driver lock", MTX_DEF)
#define	PWRCTL_LOCK(sc)				mtx_lock(&(sc)->mtx)
#define	PWRCTL_UNLOCK(sc)			mtx_unlock(&(sc)->mtx)
#define	PWRCTL_LOCK_ASSERT(sc, what)		mtx_assert(&(sc)->mtx, what)
#define	PWRCTL_LOCK_DESTROY(sc)			mtx_destroy(&(sc)->mtx)

/* quirk convenience macro */
#define	PWRCTL_QUIRK(_sc, _name)	\
    ((_sc)->quirks & PWRCTL_QUIRK_ ## _name)
    
#define	PWRCTL_ASSERT_QUIRK(_sc, name)	\
    KASSERT(PWRCTL_QUIRK((_sc), name), ("quirk " __STRING(_name) " not set"))

#endif /* _BHND_PWRCTL_BHND_PWRCTLVAR_H_ */
