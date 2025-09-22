/*	$OpenBSD: i2c.c,v 1.19 2022/04/06 18:59:28 naddy Exp $	*/
/*	$NetBSD: i2c.c,v 1.1 2003/09/30 00:35:31 thorpej Exp $	*/

/*
 * Copyright (c) 2003 Wasabi Systems, Inc.
 * All rights reserved.
 *
 * Written by Jason R. Thorpe for Wasabi Systems, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed for the NetBSD Project by
 *      Wasabi Systems, Inc.
 * 4. The name of Wasabi Systems, Inc. may not be used to endorse
 *    or promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY WASABI SYSTEMS, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL WASABI SYSTEMS, INC
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
#include <sys/event.h>

#define _I2C_PRIVATE
#include <dev/i2c/i2cvar.h>

#define IICCF_ADDR	0
#define IICCF_SIZE	1

struct iic_softc {
	struct device sc_dev;
	i2c_tag_t sc_tag;
};

int	iic_match(struct device *, void *, void *);
void	iic_attach(struct device *, struct device *, void *);
int	iic_search(struct device *, void *, void *);

const struct cfattach iic_ca = {
	sizeof (struct iic_softc),
	iic_match,
	iic_attach
};

struct cfdriver iic_cd = {
	NULL, "iic", DV_DULL, CD_SKIPHIBERNATE
};

int
iicbus_print(void *aux, const char *pnp)
{
	struct i2cbus_attach_args *iba = aux;

	if (pnp != NULL)
		printf("%s at %s", iba->iba_name, pnp);

	return (UNCONF);
}

int
iic_print(void *aux, const char *pnp)
{
	struct i2c_attach_args *ia = aux;

	if (pnp != NULL)
		printf("\"%s\" at %s", ia->ia_name, pnp);
	printf(" addr 0x%x", ia->ia_addr);

	return (UNCONF);
}

int
iic_search(struct device *parent, void *arg, void *aux)
{
	struct iic_softc *sc = (void *) parent;
	struct cfdata *cf = arg;
	struct i2c_attach_args ia;

	if (cf->cf_loc[IICCF_ADDR] != -1) {
		memset(&ia, 0, sizeof(ia));
		ia.ia_tag = sc->sc_tag;
		ia.ia_addr = cf->cf_loc[IICCF_ADDR];
		ia.ia_size = cf->cf_loc[IICCF_SIZE];
		ia.ia_name = "unknown";

		if (cf->cf_attach->ca_match(parent, cf, &ia) > 0)
			config_attach(parent, cf, &ia, iic_print);
	}
	return (0);
}

int
iic_match(struct device *parent, void *arg, void *aux)
{
	struct cfdata *cf = arg;
	struct i2cbus_attach_args *iba = aux;

	/* Just make sure we're looking for i2c. */
	return (strcmp(iba->iba_name, cf->cf_driver->cd_name) == 0);
}

void
iic_attach(struct device *parent, struct device *self, void *aux)
{
	struct iic_softc *sc = (void *) self;
	struct i2cbus_attach_args *iba = aux;

	sc->sc_tag = iba->iba_tag;

	printf("\n");

	/*
	 * Attach all i2c devices described in the kernel
	 * configuration file.
	 */
	config_search(iic_search, self, NULL);

	/*
	 * Scan for known device signatures.
	 */
	if (iba->iba_bus_scan)
		(iba->iba_bus_scan)(self, aux, iba->iba_bus_scan_arg);
	else
		iic_scan(self, aux);
}

int
iic_is_compatible(const struct i2c_attach_args *ia, const char *name)
{
	const char *end, *entry;

	if (ia->ia_namelen > 0) {
		/* ia_name points to a concatenation of strings. */
		entry = ia->ia_name;
		end = entry + ia->ia_namelen;
		while (entry < end) {
			if (strcmp(entry, name) == 0)
				return (1);
			entry += strlen(entry) + 1;
		}
	} else {
		/* ia_name points to a string. */
		if (strcmp(ia->ia_name, name) == 0)
			return (1);
	}

	return (0);
}
