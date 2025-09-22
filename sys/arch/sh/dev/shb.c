/*	$OpenBSD: shb.c,v 1.4 2024/11/05 18:58:59 miod Exp $	*/
/*	$NetBSD: shb.c,v 1.10 2005/12/11 12:18:58 christos Exp $	*/

/*-
 * Copyright (c) 2002 The NetBSD Foundation, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <machine/autoconf.h>

int shb_match(struct device *, void *, void *);
void shb_attach(struct device *, struct device *, void *);
int shb_print(void *, const char *);
int shb_search(struct device *, void *, void *);

const struct cfattach shb_ca = {
	sizeof(struct device), shb_match, shb_attach
};

struct cfdriver shb_cd = {
	NULL, "shb", DV_DULL
};

int
shb_match(struct device *parent, void *vcf, void *aux)
{
	extern struct cfdriver shb_cd;
	struct mainbus_attach_args *ma = aux;

	if (strcmp(ma->ma_name, shb_cd.cd_name) != 0)
		return (0);

	return (1);
}

void
shb_attach(struct device *parent, struct device *self, void *aux)
{
	printf("\n");

	config_search(shb_search, self, aux);
}

int
shb_search(struct device *parent, void *vcf, void *aux)
{
	struct cfdata *cf = vcf;

	if ((*cf->cf_attach->ca_match)(parent, cf, NULL) == 0)
		return (0);
	config_attach(parent, cf, NULL, shb_print);
	return (1);
}

int
shb_print(void *aux, const char *pnp)
{
	return (UNCONF);
}
