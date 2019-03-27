/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 Neelkanth Natu
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

#include <sys/param.h>
#include <sys/kenv.h>

#include <dev/cfe/cfe_api.h>

__FBSDID("$FreeBSD$");

#ifndef	CFE_ENV_SIZE
#define	CFE_ENV_SIZE	PAGE_SIZE	/* default is one page */
#endif

extern void cfe_env_init(void);

static char cfe_env_buf[CFE_ENV_SIZE];

void
cfe_env_init(void)
{
	int idx;
	char name[KENV_MNAMELEN], val[KENV_MVALLEN];

	init_static_kenv(cfe_env_buf, CFE_ENV_SIZE);

	idx = 0;
	while (1) {
		if (cfe_enumenv(idx, name, sizeof(name), val, sizeof(val)) != 0)
			break;

		if (kern_setenv(name, val) != 0) {
			printf("No space to store CFE env: \"%s=%s\"\n",
				name, val);
		}
		++idx;
	}
}
