/* $OpenBSD: mcmem.c,v 1.5 2025/06/29 15:55:21 miod Exp $ */
/* $NetBSD: mcmem.c,v 1.4 2002/10/02 04:06:38 thorpej Exp $ */

/*
 * Copyright (c) 2007 Robert Nagy <robert@openbsd.org>
 * Copyright (c) 1998 by Matthew Jacob
 * NASA AMES Research Center.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice immediately at the beginning of the file, without modification,
 *    this list of conditions, and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * 'dummy' (for now) node for the memory modules attached to
 * the MCBUS main system bus found on AlphaServer 4100 systems.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/malloc.h>

#include <machine/autoconf.h>
#include <machine/rpb.h>
#include <machine/pte.h>

#include <alpha/mcbus/mcbusreg.h>
#include <alpha/mcbus/mcbusvar.h>

int	mcmemmatch (struct device *, void *, void *);
void	mcmemattach (struct device *, struct device *, void *);

const struct cfattach mcmem_ca = {
	sizeof(struct device), mcmemmatch, mcmemattach
};

struct cfdriver mcmem_cd = {
	NULL, "mcmem", DV_DULL,
};

int
mcmemmatch(struct device *parent, void *cf, void *aux)
{
	struct mcbus_dev_attach_args *ta = aux;

	if (ta->ma_type == MCBUS_TYPE_MEM)
		return (1);

	return (0);
}

void
mcmemattach(struct device *parent, struct device *self, void *aux)
{
	printf("\n");
}
