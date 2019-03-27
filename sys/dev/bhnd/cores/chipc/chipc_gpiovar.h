/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
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
 *     notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#ifndef _BHND_CORES_CHIPC_CHIPC_GPIOVAR_H_
#define _BHND_CORES_CHIPC_CHIPC_GPIOVAR_H_

#include <sys/param.h>
#include <sys/bus.h>

#include <sys/lock.h>
#include <sys/mutex.h>

#include <dev/bhnd/bhnd.h>

/**
 * ChipCommon GPIO device quirks.
 */
enum {
	/**
	 * No GPIO event support.
	 * 
	 * The CHIPC_GPIOEVENT, CHIPC_GPIOEVENT_INTM, and
	 * CHIPC_GPIOEVENT_INTPOLARITY registers are not available.
	 */
	CC_GPIO_QUIRK_NO_EVENTS		= (1<<0),

	/**
	 * No GPIO duty-cycle timer support.
	 * 
	 * The CHIPC_GPIOTIMERVAL and CHIPC_GPIOTIMEROUTMASK registers are not
	 * available.
	 */
	CC_GPIO_QUIRK_NO_DCTIMER	= (1<<1),

	/**
	 * No GPIO pull-up/pull-down configuration support.
	 *
	 * The CHIPC_GPIOPU and CHIPC_GPIOPD registers are not available.
	 */
	CC_GPIO_QUIRK_NO_PULLUPDOWN	= (1<<2),

	/**
	 * Do not attach a child gpioc(4) device.
	 * 
	 * This is primarily intended for use on bridged Wi-Fi adapters, where
	 * userspace modification of GPIO pin configuration could introduce
	 * significant undesirable behavior.
	 */
	CC_GPIO_QUIRK_NO_GPIOC		= (1<<3),
};

/** ChipCommon GPIO pin modes */
typedef enum {
	CC_GPIO_PIN_INPUT,
	CC_GPIO_PIN_OUTPUT,
	CC_GPIO_PIN_TRISTATE
} chipc_gpio_pin_mode;

/**
 * A single GPIO update register.
 */
struct chipc_gpio_reg {
	uint32_t value;	/**< register update value */
	uint32_t mask;	/**< register update mask */
};

/**
 * A GPIO register update descriptor.
 */
struct chipc_gpio_update {
	struct chipc_gpio_reg	pullup;		/**< CHIPC_GPIOPU changes */
	struct chipc_gpio_reg	pulldown;	/**< CHIPC_GPIOPD changes */
	struct chipc_gpio_reg	out;		/**< CHIPC_GPIOOUT changes */
	struct chipc_gpio_reg	outen;		/**< CHIPC_GPIOOUTEN changes */
	struct chipc_gpio_reg	timeroutmask;	/**< CHIPC_GPIOTIMEROUTMASK changes */
	struct chipc_gpio_reg	ctrl;		/**< CHIPC_GPIOCTRL changes */
};

#define	CC_GPIO_UPDATE(_upd, _pin, _reg, _val)	do {	\
	(_upd)->_reg.mask |= (1 << (_pin));		\
	if (_val)					\
		(_upd)->_reg.value |= (1 << (_pin));	\
	else						\
		(_upd)->_reg.value &= ~(1 << (_pin));	\
} while(0)	

/**
 * ChipCommon GPIO driver instance state.
 */
struct chipc_gpio_softc {
	device_t		 dev;
	device_t		 gpiobus;	/**< attached gpiobus child */
	struct bhnd_resource	*mem_res;	/**< chipcommon register block */
	int			 mem_rid;	/**< resource ID of mem_res */
	uint32_t		 quirks;	/**< device quirks (see CC_GPIO_QUIRK_*) */
	struct mtx		 mtx;		/**< lock protecting RMW register access */
};

#define	CC_GPIO_LOCK_INIT(sc)		mtx_init(&(sc)->mtx,	\
    device_get_nameunit((sc)->dev), NULL, MTX_DEF)
#define	CC_GPIO_LOCK(sc)		mtx_lock(&(sc)->mtx)
#define	CC_GPIO_UNLOCK(sc)		mtx_unlock(&(sc)->mtx)
#define	CC_GPIO_LOCK_ASSERT(sc, what)	mtx_assert(&(sc)->mtx, what)
#define	CC_GPIO_LOCK_DESTROY(sc)	mtx_destroy(&(sc)->mtx)

#define	CC_GPIO_WR4(sc, off, val)					\
	bhnd_bus_write_4((sc)->mem_res, (off), (val))
#define	CC_GPIO_WRFLAG(sc, pin_num, flag, val)				\
	CC_GPIO_WR4(sc, CHIPC_ ## flag,					\
	    (CC_GPIO_RD4(sc, CHIPC_ ## flag) & ~(1 << pin_num)) |	\
	     (val ? (1 << pin_num) : 0))

#define	CC_GPIO_RD4(sc, off)				\
	bhnd_bus_read_4((sc)->mem_res, (off))
#define	CC_GPIO_RDFLAG(sc, pin_num, flag)		\
	((CC_GPIO_RD4(sc, CHIPC_ ## flag) & (1 << pin_num)) != 0)

#define	CC_GPIO_NPINS		32
#define	CC_GPIO_VALID_PIN(_pin)	\
    ((_pin) >= 0 && (_pin) < CC_GPIO_NPINS)
#define	CC_GPIO_VALID_PINS(_first, _num)	\
	((_num) <= CC_GPIO_NPINS && CC_GPIO_NPINS - (_num) >= _first)

#define CC_GPIO_ASSERT_VALID_PIN(sc, pin_num)	\
	KASSERT(CC_GPIO_VALID_PIN(pin_num), ("invalid pin# %" PRIu32, pin_num));

#define	CC_GPIO_QUIRK(_sc, _name)	\
    ((_sc)->quirks & CC_GPIO_QUIRK_ ## _name)

#define	CC_GPIO_ASSERT_QUIRK(_sc, name)	\
    KASSERT(CC_GPIO_QUIRK((_sc), name), ("quirk " __STRING(_name) " not set"))

#endif /* _BHND_PWRCTL_BHND_PWRCTLVAR_H_ */
