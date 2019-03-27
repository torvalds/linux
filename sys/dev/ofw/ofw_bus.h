/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001, 2003 by Thomas Moestl <tmm@FreeBSD.org>
 * Copyright (c) 2004 by Marius Strobl <marius@FreeBSD.org>
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
 * IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef	_DEV_OFW_OFW_BUS_H_
#define	_DEV_OFW_OFW_BUS_H_

#include <sys/bus.h>

#include <dev/ofw/openfirm.h>

#include "ofw_bus_if.h"

static __inline const char *
ofw_bus_get_compat(device_t dev)
{

	return (OFW_BUS_GET_COMPAT(device_get_parent(dev), dev));
}

static __inline const char *
ofw_bus_get_model(device_t dev)
{

	return (OFW_BUS_GET_MODEL(device_get_parent(dev), dev));
}

static __inline const char *
ofw_bus_get_name(device_t dev)
{

	return (OFW_BUS_GET_NAME(device_get_parent(dev), dev));
}

static __inline phandle_t
ofw_bus_get_node(device_t dev)
{

	return (OFW_BUS_GET_NODE(device_get_parent(dev), dev));
}

static __inline const char *
ofw_bus_get_type(device_t dev)
{

	return (OFW_BUS_GET_TYPE(device_get_parent(dev), dev));
}

static __inline int
ofw_bus_map_intr(device_t dev, phandle_t iparent, int icells, pcell_t *intr)
{
	return (OFW_BUS_MAP_INTR(dev, dev, iparent, icells, intr));
}

#endif /* !_DEV_OFW_OFW_BUS_H_ */
