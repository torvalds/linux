/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Kazutaka YOKOTA <yokota@zodiac.mech.utsunomiya-u.ac.jp>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer as
 *    the first lines of this file unmodified.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *	from: FreeBSD: src/sys/isa/atkbdc_isa.c,v 1.31 2005/05/29 04:42:28 nyan Exp
 * $FreeBSD$
 */

#ifndef _DEV_ATKBDC_ATKBDC_SUBR_H_
#define	_DEV_ATKBDC_ATKBDC_SUBR_H_

MALLOC_DECLARE(M_ATKBDDEV);

extern devclass_t atkbdc_devclass;

/* children */
typedef struct atkbdc_device {
	struct resource_list resources;
	int rid;
	u_int32_t vendorid;
	u_int32_t serial;
	u_int32_t logicalid;
	u_int32_t compatid;
} atkbdc_device_t;

/* kbdc */
int atkbdc_print_child(device_t bus, device_t dev);
int atkbdc_read_ivar(device_t bus, device_t dev, int index, uintptr_t *val);
int atkbdc_write_ivar(device_t bus, device_t dev, int index, uintptr_t val);
struct resource_list *atkbdc_get_resource_list(device_t bus, device_t dev);

#endif /* !_DEV_ATKBDC_ATKBDC_SUBR_H_ */
