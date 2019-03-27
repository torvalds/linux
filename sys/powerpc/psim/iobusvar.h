/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright 2002 by Peter Grehan. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _PSIM_IOBUSVAR_H_
#define _PSIM_IOBUSVAR_H_

/* 
 * Accessors for iobus devices
 */

enum iobus_ivars {
        IOBUS_IVAR_NODE,
        IOBUS_IVAR_NAME,
	IOBUS_IVAR_NREGS,
	IOBUS_IVAR_REGS,
};

#define IOBUS_ACCESSOR(var, ivar, type)                                 \
        __BUS_ACCESSOR(iobus, var, IOBUS, ivar, type)

IOBUS_ACCESSOR(node,            NODE,                   phandle_t)
IOBUS_ACCESSOR(name,            NAME,                   char *)
IOBUS_ACCESSOR(nregs,           NREGS,                  u_int)
IOBUS_ACCESSOR(regs,            REGS,                   u_int *)

#undef IOBUS_ACCESSOR

/*
 * Per-device structure.
 */
struct iobus_devinfo {
        phandle_t  id_node;
        char      *id_name;
        int        id_interrupt;
	u_int      id_nregs;
	u_int      id_reg[24];
        struct resource_list id_resources;
};

#endif /* _PSIM_IOBUSVAR_H_ */
