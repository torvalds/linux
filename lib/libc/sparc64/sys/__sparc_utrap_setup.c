/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Jake Burkholder.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>

#include <machine/utrap.h>
#include <machine/sysarch.h>

#include "__sparc_utrap_private.h"

static const struct sparc_utrap_args ua[] = {
	{ UT_FP_DISABLED, __sparc_utrap_fp_disabled, NULL, NULL, NULL },
	{ UT_FP_EXCEPTION_IEEE_754, __sparc_utrap_gen, NULL, NULL, NULL },
	{ UT_FP_EXCEPTION_OTHER, __sparc_utrap_gen, NULL, NULL, NULL },
	{ UT_ILLEGAL_INSTRUCTION, __sparc_utrap_gen, NULL, NULL, NULL },
	{ UT_MEM_ADDRESS_NOT_ALIGNED, __sparc_utrap_gen, NULL, NULL, NULL },
};

static const struct sparc_utrap_install_args uia[] = {
	{ nitems(ua), ua }
};

void __sparc_utrap_setup(void);

void
__sparc_utrap_setup(void)
{

	sysarch(SPARC_UTRAP_INSTALL, (void *)&uia);
}
