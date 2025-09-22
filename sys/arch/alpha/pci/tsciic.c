/*	$OpenBSD: tsciic.c,v 1.1 2014/12/24 18:46:14 miod Exp $	*/
/*	$NetBSD: tsciic.c,v 1.1 2014/02/21 12:23:30 jdc Exp $	*/

/*
 * Copyright (c) 2013 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Julian Coleman.
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
#include <sys/rwlock.h>

#include <alpha/pci/tsreg.h>
#include <alpha/pci/tsvar.h>

#include <dev/i2c/i2cvar.h>
#include <dev/i2c/i2c_bitbang.h>

struct tsciic_softc {
	struct device		sc_dev;

	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	struct i2c_controller	sc_i2c_tag;
	struct rwlock		sc_i2c_lock;
};

int	tsciic_match(struct device *, void *, void *);
void	tsciic_attach(struct device *, struct device *, void *);

const struct cfattach tsciic_ca = {
	.ca_devsize = sizeof(struct tsciic_softc),
	.ca_match = tsciic_match,
	.ca_attach = tsciic_attach
};

struct cfdriver tsciic_cd = {
	.cd_name = "tsciic",
	.cd_class = DV_DULL
};

/* I2C glue */
int	tsciic_acquire_bus(void *, int);
void	tsciic_release_bus(void *, int);
int	tsciic_send_start(void *, int);
int	tsciic_send_stop(void *, int);
int	tsciic_initiate_xfer(void *, i2c_addr_t, int);
int	tsciic_read_byte(void *, uint8_t *, int);
int	tsciic_write_byte(void *, uint8_t, int);

/* I2C bitbang glue */
void	tsciicbb_set_bits(void *, uint32_t);
void	tsciicbb_set_dir(void *, uint32_t);
uint32_t tsciicbb_read(void *);

#define MPD_BIT_SDA 0x01
#define MPD_BIT_SCL 0x02
static const struct i2c_bitbang_ops tsciicbb_ops = {
	.ibo_set_bits = tsciicbb_set_bits,
	.ibo_set_dir = tsciicbb_set_dir,
	.ibo_read_bits = tsciicbb_read,
	.ibo_bits = {
		[I2C_BIT_SDA] MPD_BIT_SDA,
		[I2C_BIT_SCL] MPD_BIT_SCL
	}
};

int
tsciic_match(struct device *parent, void *match, void *aux)
{
	struct tsp_attach_args *tsp = (struct tsp_attach_args *)aux;

	return strcmp(tsp->tsp_name, tsciic_cd.cd_name) == 0;
}

void
tsciic_attach(struct device *parent, struct device *self, void *aux)
{
	struct tsciic_softc *sc = (struct tsciic_softc *)self;
	struct i2cbus_attach_args iba;

	printf("\n");

	rw_init(&sc->sc_i2c_lock, "tsciiclk");
	sc->sc_i2c_tag.ic_cookie = sc;
	sc->sc_i2c_tag.ic_acquire_bus = tsciic_acquire_bus;
	sc->sc_i2c_tag.ic_release_bus = tsciic_release_bus;
	sc->sc_i2c_tag.ic_send_start = tsciic_send_start;
	sc->sc_i2c_tag.ic_send_stop = tsciic_send_stop;
	sc->sc_i2c_tag.ic_initiate_xfer = tsciic_initiate_xfer;
	sc->sc_i2c_tag.ic_read_byte = tsciic_read_byte;
	sc->sc_i2c_tag.ic_write_byte = tsciic_write_byte;

	memset(&iba, 0, sizeof iba);
	iba.iba_name = "iic";
	iba.iba_tag = &sc->sc_i2c_tag;
	config_found(self, &iba, iicbus_print);
}

int
tsciic_acquire_bus(void *cookie, int flags)
{
	struct tsciic_softc *sc = cookie;

	if (cold || (flags & I2C_F_POLL))
		return 0;

	return rw_enter(&sc->sc_i2c_lock, RW_WRITE | RW_INTR);
}

void
tsciic_release_bus(void *cookie, int flags)
{
	struct tsciic_softc *sc = cookie;

	if (cold || (flags & I2C_F_POLL))
		return;

	rw_exit(&sc->sc_i2c_lock);
}

int
tsciic_send_start(void *cookie, int flags)
{
	return i2c_bitbang_send_start(cookie, flags, &tsciicbb_ops);
}

int
tsciic_send_stop(void *cookie, int flags)
{
	return i2c_bitbang_send_stop(cookie, flags, &tsciicbb_ops);
}

int
tsciic_initiate_xfer(void *cookie, i2c_addr_t addr, int flags)
{
	return i2c_bitbang_initiate_xfer(cookie, addr, flags, &tsciicbb_ops);
}

int
tsciic_read_byte(void *cookie, uint8_t *valp, int flags)
{
	return i2c_bitbang_read_byte(cookie, valp, flags, &tsciicbb_ops);
}

int
tsciic_write_byte(void *cookie, uint8_t val, int flags)
{
	return i2c_bitbang_write_byte(cookie, val, flags, &tsciicbb_ops);
}

/* I2C bitbanging */
void
tsciicbb_set_bits(void *cookie, uint32_t bits)
{
	uint64_t val;

	val = (bits & MPD_BIT_SDA ? MPD_DS : 0) |
	    (bits & MPD_BIT_SCL ? MPD_CKS : 0);
	alpha_mb();
	STQP(TS_C_MPD) = val;
	alpha_mb();
}

void
tsciicbb_set_dir(void *cookie, uint32_t dir)
{
	/* Nothing to do */
}

uint32_t
tsciicbb_read(void *cookie)
{
	uint64_t val;
	uint32_t bits;

	val = LDQP(TS_C_MPD);
	bits = (val & MPD_DR ? MPD_BIT_SDA : 0) |
	    (val & MPD_CKR ? MPD_BIT_SCL : 0);
	return bits;
}
