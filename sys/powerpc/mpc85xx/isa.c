/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 Marcel Moolenaar
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
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/malloc.h>
#include <machine/bus.h>
#include <sys/rman.h>

#include <machine/intr_machdep.h>
#include <machine/resource.h>

#include <isa/isareg.h>
#include <isa/isavar.h>
#include <isa/isa_common.h>

void
isa_init(device_t dev)
{
}

struct resource *
isa_alloc_resource(device_t bus, device_t child, int type, int *rid,
    u_long start, u_long end, u_long count, u_int flags)
{
	struct isa_device* idev = DEVTOISA(child);
	struct resource_list *rl = &idev->id_resources;
	int isdefault, passthrough, rids;

	isdefault = RMAN_IS_DEFAULT_RANGE(start, end) ? 1 : 0;
	passthrough = (device_get_parent(child) != bus) ? 1 : 0;

	if (!passthrough && !isdefault &&
	    resource_list_find(rl, type, *rid) == NULL) {
		switch (type) {
		case SYS_RES_IOPORT:	rids = ISA_PNP_NPORT; break;
		case SYS_RES_IRQ:	rids = ISA_PNP_NIRQ; break;
		case SYS_RES_MEMORY:	rids = ISA_PNP_NMEM; break;
		default:		rids = 0; break;
		}
		if (*rid < 0 || *rid >= rids)
			return (NULL);

		resource_list_add(rl, type, *rid, start, end, count);
	}

	return (resource_list_alloc(rl, bus, child, type, rid, start, end,
	    count, flags));
}

int
isa_release_resource(device_t bus, device_t child, int type, int rid,
    struct resource *r)
{
	struct isa_device* idev = DEVTOISA(child);
	struct resource_list *rl = &idev->id_resources;

	return (resource_list_release(rl, bus, child, type, rid, r));
}
