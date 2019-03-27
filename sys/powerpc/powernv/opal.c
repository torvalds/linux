/*-
 * Copyright (C) 2015 Nathan Whitehorn
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
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <dev/ofw/openfirm.h>

#include "opal.h"

extern uint64_t opal_entrypoint;
extern uint64_t opal_data;
extern uint64_t opal_msr;

static int opal_initialized = 0;

int
opal_check(void)
{
	phandle_t opal;
	cell_t val[2];

	if (opal_initialized)
		return (0);

	opal = OF_finddevice("/ibm,opal");
	if (opal == -1)
		return (ENOENT);

	if (!OF_hasprop(opal, "opal-base-address") ||
	    !OF_hasprop(opal, "opal-entry-address"))
		return (ENOENT);
	
	OF_getencprop(opal, "opal-base-address", val, sizeof(val));
	opal_data = ((uint64_t)val[0] << 32) | val[1];
	OF_getencprop(opal, "opal-entry-address", val, sizeof(val));
	opal_entrypoint = ((uint64_t)val[0] << 32) | val[1];

	opal_msr = mfmsr() & ~(PSL_EE | PSL_IR | PSL_DR | PSL_SE);

	opal_initialized = 1;

	return (0);
}

