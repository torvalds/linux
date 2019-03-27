/*-
 * Copyright (c) 2016 Landon Fuller <landonf@FreeBSD.org>
 * Copyright (c) 2017 The FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed by Landon Fuller
 * under sponsorship from the FreeBSD Foundation.
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

#ifndef _BHND_PWRCTL_BHND_PWRCTL_PRIVATE_H_
#define _BHND_PWRCTL_BHND_PWRCTL_PRIVATE_H_

#include "bhnd_pwrctl_hostb_if.h"

#include "bhnd_pwrctlvar.h"

int		bhnd_pwrctl_init(struct bhnd_pwrctl_softc *sc);
int		bhnd_pwrctl_setclk(struct bhnd_pwrctl_softc *sc,
		    bhnd_clock clock);
uint32_t	bhnd_pwrctl_getclk_speed(struct bhnd_pwrctl_softc *sc);
u_int		bhnd_pwrctl_fast_pwrup_delay(struct bhnd_pwrctl_softc *sc);

/**
 * If supported by the chipset, return the clock source for the given clock.
 *
 * This function is only supported on early PWRCTL-equipped chipsets
 * that expose clock management via their host bridge interface. Currently,
 * this includes PCI (not PCIe) devices, with ChipCommon core revisions 0-9.
 *
 * @param dev A bhnd bus child device.
 * @param clock The clock for which a clock source will be returned.
 *
 * @retval	bhnd_clksrc		The clock source for @p clock.
 * @retval	BHND_CLKSRC_UNKNOWN	If @p clock is unsupported, or its
 *					clock source is not known to the bus.
 */
static inline bhnd_clksrc
bhnd_pwrctl_hostb_get_clksrc(device_t dev, bhnd_clock clock)
{
	return (BHND_PWRCTL_HOSTB_GET_CLKSRC(device_get_parent(dev), dev,
	    clock));
}

/**
 * If supported by the chipset, gate @p clock
 *
 * This function is only supported on early PWRCTL-equipped chipsets
 * that expose clock management via their host bridge interface. Currently,
 * this includes PCI (not PCIe) devices, with ChipCommon core revisions 0-9.
 *
 * @param dev A bhnd bus child device.
 * @param clock The clock to be disabled.
 *
 * @retval 0 success
 * @retval ENODEV If bus-level clock source management is not supported.
 * @retval ENXIO If bus-level management of @p clock is not supported.
 */
static inline int
bhnd_pwrctl_hostb_gate_clock(device_t dev, bhnd_clock clock)
{
	return (BHND_PWRCTL_HOSTB_GATE_CLOCK(device_get_parent(dev), dev,
	    clock));
}

/**
 * If supported by the chipset, ungate @p clock
 *
 * This function is only supported on early PWRCTL-equipped chipsets
 * that expose clock management via their host bridge interface. Currently,
 * this includes PCI (not PCIe) devices, with ChipCommon core revisions 0-9.
 *
 * @param dev A bhnd bus child device.
 * @param clock The clock to be enabled.
 *
 * @retval 0 success
 * @retval ENODEV If bus-level clock source management is not supported.
 * @retval ENXIO If bus-level management of @p clock is not supported.
 */
static inline int
bhnd_pwrctl_hostb_ungate_clock(device_t dev, bhnd_clock clock)
{
	return (BHND_PWRCTL_HOSTB_UNGATE_CLOCK(device_get_parent(dev), dev,
	    clock));
}

#endif /* _BHND_PWRCTL_BHND_PWRCTL_PRIVATE_H_ */
