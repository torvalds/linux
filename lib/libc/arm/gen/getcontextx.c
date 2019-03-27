/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2017 Michal Meloun <mmel@FreeBSD.org>
 * All rights reserved.
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

#include <sys/types.h>
#include <sys/ucontext.h>
#include <errno.h>
#include <stdlib.h>
#include <machine/sysarch.h>

struct ucontextx {
	ucontext_t	ucontext;
	mcontext_vfp_t	mcontext_vfp;
};

int
__getcontextx_size(void)
{

	return (sizeof(struct ucontextx));
}

int
__fillcontextx2(char *ctx)
{
	struct ucontextx *ucxp;
	ucontext_t	 *ucp;
	mcontext_vfp_t	 *mvp;
	struct arm_get_vfpstate_args vfp_arg;

	ucxp = (struct ucontextx *)ctx;
	ucp = &ucxp->ucontext;
	mvp = &ucxp->mcontext_vfp;

	vfp_arg.mc_vfp_size = sizeof(mcontext_vfp_t);
	vfp_arg.mc_vfp = mvp;
	if (sysarch(ARM_GET_VFPSTATE, &vfp_arg) == -1)
			return (-1);
	ucp->uc_mcontext.mc_vfp_size = sizeof(mcontext_vfp_t);
	ucp->uc_mcontext.mc_vfp_ptr = mvp;
	return (0);
}

int
__fillcontextx(char *ctx)
{
	struct ucontextx *ucxp;

	ucxp = (struct ucontextx *)ctx;
	if (getcontext(&ucxp->ucontext) == -1)
		return (-1);
	__fillcontextx2(ctx);
	return (0);
}

__weak_reference(__getcontextx, getcontextx);

ucontext_t *
__getcontextx(void)
{
	char *ctx;
	int error;

	ctx = malloc(__getcontextx_size());
	if (ctx == NULL)
		return (NULL);
	if (__fillcontextx(ctx) == -1) {
		error = errno;
		free(ctx);
		errno = error;
		return (NULL);
	}
	return ((ucontext_t *)ctx);
}
