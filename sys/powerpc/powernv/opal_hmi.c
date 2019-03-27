/*-
 * Copyright (c) 2019 Justin Hibbits
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
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
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/types.h>
#include <sys/kernel.h>
#include <sys/systm.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/spr.h>
#include <machine/trap.h>
#include "opal.h"

static int
opal_hmi_handler2(struct trapframe *frame)
{
	int64_t flags;
	int err;

	err = opal_call(OPAL_HANDLE_HMI2, vtophys(&flags));

	/* XXX: At some point, handle the flags outvar. */
	if (err == OPAL_SUCCESS) {
		mtspr(SPR_HMER, 0);
		return (0);
	}

	printf("HMI handler failed!  OPAL error code: %d\n", err);

	return (-1);
}

static int
opal_hmi_handler(struct trapframe *frame)
{
	int err;

	err = opal_call(OPAL_HANDLE_HMI);

	if (err == OPAL_SUCCESS) {
		mtspr(SPR_HMER, 0);
		return (0);
	}

	printf("HMI handler failed!  OPAL error code: %d\n", err);

	return (-1);
}

static void
opal_setup_hmi(void *data)
{
	/* This only works for OPAL, so first make sure we have it. */
	if (opal_check() != 0)
		return;

	if (opal_call(OPAL_CHECK_TOKEN, OPAL_HANDLE_HMI2) == OPAL_TOKEN_PRESENT)
		hmi_handler = opal_hmi_handler2;
	else if (opal_call(OPAL_CHECK_TOKEN, OPAL_HANDLE_HMI) == OPAL_TOKEN_PRESENT)
		hmi_handler = opal_hmi_handler;
	else {
		printf("Warning: No OPAL HMI handler found.\n");
		return;
	}

	if (bootverbose)
		printf("Installed OPAL HMI handler.\n");
}

SYSINIT(opal_setup_hmi, SI_SUB_HYPERVISOR, SI_ORDER_ANY, opal_setup_hmi, NULL);
