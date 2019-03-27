/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1998 Nicolas Souchu
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
 *
 */
#ifndef __IICBUS_H
#define __IICBUS_H

#include <sys/_lock.h>
#include <sys/_mutex.h>

#define IICBUS_IVAR(d) (struct iicbus_ivar *) device_get_ivars(d)
#define IICBUS_SOFTC(d) (struct iicbus_softc *) device_get_softc(d)

struct iicbus_softc
{
	device_t dev;		/* Myself */
	device_t owner;		/* iicbus owner device structure */
	u_int owncount;		/* iicbus ownership nesting count */
	u_char started;		/* address of the 'started' slave
				 * 0 if no start condition succeeded */
	u_char strict;		/* deny operations that violate the
				 * I2C protocol */
	struct mtx lock;
	u_int bus_freq;		/* Configured bus Hz. */
};

struct iicbus_ivar
{
	uint32_t	addr;
	struct resource_list	rl;
	bool		nostop;
};

enum {
	IICBUS_IVAR_ADDR,		/* Address or base address */
	IICBUS_IVAR_NOSTOP,		/* nostop defaults */
};

#define IICBUS_ACCESSOR(A, B, T)					\
	__BUS_ACCESSOR(iicbus, A, IICBUS, B, T)
	
IICBUS_ACCESSOR(addr,		ADDR,		uint32_t)
IICBUS_ACCESSOR(nostop,		NOSTOP,		bool)

#define	IICBUS_LOCK(sc)			mtx_lock(&(sc)->lock)
#define	IICBUS_UNLOCK(sc)      		mtx_unlock(&(sc)->lock)
#define	IICBUS_ASSERT_LOCKED(sc)       	mtx_assert(&(sc)->lock, MA_OWNED)

int  iicbus_generic_intr(device_t dev, int event, char *buf);
void iicbus_init_frequency(device_t dev, u_int bus_freq);

extern driver_t iicbus_driver;
extern devclass_t iicbus_devclass;
extern driver_t ofw_iicbus_driver;
extern devclass_t ofw_iicbus_devclass;

#endif
