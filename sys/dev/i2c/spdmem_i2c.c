/*	$OpenBSD: spdmem_i2c.c,v 1.2 2022/04/06 18:59:28 naddy Exp $	*/
/* $NetBSD: spdmem.c,v 1.3 2007/09/20 23:09:59 xtraeme Exp $ */

/*
 * Copyright (c) 2007 Jonathan Gray <jsg@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Copyright (c) 2007 Nicolas Joly
 * Copyright (c) 2007 Paul Goyette
 * Copyright (c) 2007 Tobias Nygren
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS
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

/*
 * Serial Presence Detect (SPD) memory identification
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <dev/spdmemvar.h>
#include <dev/i2c/i2cvar.h>

struct spdmem_iic_softc {
	struct spdmem_softc	sc_base;
	i2c_tag_t		sc_tag;
	i2c_addr_t		sc_addr;
};

int	spdmem_iic_match(struct device *, void *, void *);
void	spdmem_iic_attach(struct device *, struct device *, void *);
uint8_t	spdmem_iic_read(struct spdmem_softc *, uint8_t);

const struct cfattach spdmem_iic_ca = {
	sizeof(struct spdmem_iic_softc), spdmem_iic_match, spdmem_iic_attach
};

int
spdmem_iic_match(struct device *parent, void *match, void *aux)
{
	struct i2c_attach_args *ia = aux;
	struct spdmem_iic_softc sc;

	/* clever attachments like openfirmware informed macppc */	
	if (strcmp(ia->ia_name, "spd") == 0)
		return (1);

	/* dumb, need sanity checks */
	if (strcmp(ia->ia_name, "eeprom") != 0)
		return (0);

	sc.sc_tag = ia->ia_tag;
	sc.sc_addr = ia->ia_addr;
	sc.sc_base.sc_read = spdmem_iic_read;

	return spdmem_probe(&sc.sc_base);
}

void
spdmem_iic_attach(struct device *parent, struct device *self, void *aux)
{
	struct spdmem_iic_softc *sc = (struct spdmem_iic_softc *)self;
	struct i2c_attach_args *ia = aux;

	sc->sc_tag = ia->ia_tag;
	sc->sc_addr = ia->ia_addr;
	sc->sc_base.sc_read = spdmem_iic_read;

	printf(":");

	spdmem_attach_common(&sc->sc_base);
}

uint8_t
spdmem_iic_read(struct spdmem_softc *v, uint8_t reg)
{
	struct spdmem_iic_softc *sc = (struct spdmem_iic_softc *)v;
	uint8_t val = 0xff;

	iic_acquire_bus(sc->sc_tag,0);
	iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_addr,
	    &reg, sizeof reg, &val, sizeof val, 0);
	iic_release_bus(sc->sc_tag, 0);

	return val;
}
