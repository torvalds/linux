/*	$OpenBSD: gscsio.c,v 1.14 2022/04/06 18:59:28 naddy Exp $	*/
/*
 * Copyright (c) 2004 Alexander Yurchenko <grange@openbsd.org>
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
 * National Semiconductor Geode SC1100 Super I/O.
 * Only ACCESS.bus logical device is supported.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/kernel.h>
#include <sys/rwlock.h>

#include <machine/bus.h>

#include <dev/i2c/i2cvar.h>

#include <dev/isa/isareg.h>
#include <dev/isa/isavar.h>

#include <dev/isa/gscsioreg.h>

struct gscsio_softc {
	struct device sc_dev;

	bus_space_tag_t sc_iot;
	bus_space_handle_t sc_ioh;

	int sc_ld_en[GSCSIO_LDN_LAST + 1];
	bus_space_handle_t sc_ld_ioh0[GSCSIO_LDN_LAST + 1];
	bus_space_handle_t sc_ld_ioh1[GSCSIO_LDN_LAST + 1];

	/* ACCESS.bus */
	struct gscsio_acb {
		void *sc;
		bus_space_handle_t ioh;
		struct rwlock buslock;
	} sc_acb[2];
	struct i2c_controller sc_acb1_tag;
	struct i2c_controller sc_acb2_tag;
};

/* Supported logical devices description */
static const struct {
	const char *ld_name;
	int ld_num;
	int ld_iosize0;
	int ld_iosize1;
} gscsio_ld[] = {
	{ "ACB1", GSCSIO_LDN_ACB1, 6, 0 },
	{ "ACB2", GSCSIO_LDN_ACB2, 6, 0 },
};

int	gscsio_probe(struct device *, void *, void *);
void	gscsio_attach(struct device *, struct device *, void *);

void	gscsio_acb_init(struct gscsio_acb *, i2c_tag_t);
int	gscsio_acb_wait(struct gscsio_acb *, int, int);
void	gscsio_acb_reset(struct gscsio_acb *acb);

int	gscsio_acb_acquire_bus(void *, int);
void	gscsio_acb_release_bus(void *, int);
int	gscsio_acb_send_start(void *, int);
int	gscsio_acb_send_stop(void *, int);
int	gscsio_acb_initiate_xfer(void *, i2c_addr_t, int);
int	gscsio_acb_read_byte(void *, uint8_t *, int);
int	gscsio_acb_write_byte(void *, uint8_t, int);

const struct cfattach gscsio_ca = {
	sizeof(struct gscsio_softc),
	gscsio_probe,
	gscsio_attach
};

struct cfdriver gscsio_cd = {
	NULL, "gscsio", DV_DULL
};

#define ACB_READ(reg) \
	bus_space_read_1(sc->sc_iot, acb->ioh, (reg))
#define ACB_WRITE(reg, val) \
	bus_space_write_1(sc->sc_iot, acb->ioh, (reg), (val))

static __inline u_int8_t
idxread(bus_space_tag_t iot, bus_space_handle_t ioh, int idx)
{
	bus_space_write_1(iot, ioh, GSCSIO_IDX, idx);

	return (bus_space_read_1(iot, ioh, GSCSIO_DAT));
}

static __inline void
idxwrite(bus_space_tag_t iot, bus_space_handle_t ioh, int idx, u_int8_t data)
{
	bus_space_write_1(iot, ioh, GSCSIO_IDX, idx);
	bus_space_write_1(iot, ioh, GSCSIO_DAT, data);
}

int
gscsio_probe(struct device *parent, void *match, void *aux)
{
	struct isa_attach_args *ia = aux;
	bus_space_tag_t iot;
	bus_space_handle_t ioh;
	int iobase;
	int rv = 0;

	iot = ia->ia_iot;
	iobase = ia->ipa_io[0].base;
	if (bus_space_map(iot, iobase, GSCSIO_IOSIZE, 0, &ioh))
		return (0);
	if (idxread(iot, ioh, GSCSIO_ID) == GSCSIO_ID_SC1100)
		rv = 1;
	bus_space_unmap(iot, ioh, GSCSIO_IOSIZE);

	if (rv) {
		ia->ipa_nio = 1;
		ia->ipa_io[0].length = GSCSIO_IOSIZE;
		ia->ipa_nmem = 0;
		ia->ipa_nirq = 0;
		ia->ipa_ndrq = 0;
	}

	return (rv);
}

void
gscsio_attach(struct device *parent, struct device *self, void *aux)
{
	struct gscsio_softc *sc = (void *)self;
	struct isa_attach_args *ia = aux;
	int i;
	int iobase;

	sc->sc_iot = ia->ia_iot;
	if (bus_space_map(sc->sc_iot, ia->ipa_io[0].base, GSCSIO_IOSIZE,
	    0, &sc->sc_ioh)) {
		printf(": can't map i/o space\n");
		return;
	}
	printf(": SC1100 SIO rev %d:",
	    idxread(sc->sc_iot, sc->sc_ioh, GSCSIO_REV));

	/* Configure all supported logical devices */
	for (i = 0; i < sizeof (gscsio_ld) / sizeof(gscsio_ld[0]); i++) {
		sc->sc_ld_en[gscsio_ld[i].ld_num] = 0;

		/* Select the device and check if it's activated */
		idxwrite(sc->sc_iot, sc->sc_ioh, GSCSIO_LDN,
		    gscsio_ld[i].ld_num);
		if ((idxread(sc->sc_iot, sc->sc_ioh, GSCSIO_ACT) &
		    GSCSIO_ACT_EN) == 0)
			continue;

		/* Map I/O space 0 if necessary */
		if (gscsio_ld[i].ld_iosize0 != 0) {
			iobase = idxread(sc->sc_iot, sc->sc_ioh,
			    GSCSIO_IO0_MSB);
			iobase <<= 8;
			iobase |= idxread(sc->sc_iot, sc->sc_ioh,
			    GSCSIO_IO0_LSB);
			if (bus_space_map(sc->sc_iot, iobase,
			    gscsio_ld[i].ld_iosize0, 0,
			    &sc->sc_ld_ioh0[gscsio_ld[i].ld_num]))
				continue;
		}

		/* Map I/O space 1 if necessary */
		if (gscsio_ld[i].ld_iosize1 != 0) {
			iobase = idxread(sc->sc_iot, sc->sc_ioh,
			    GSCSIO_IO1_MSB);
			iobase <<= 8;
			iobase |= idxread(sc->sc_iot, sc->sc_ioh,
			    GSCSIO_IO1_LSB);
			if (bus_space_map(sc->sc_iot, iobase,
			    gscsio_ld[i].ld_iosize1, 0,
			    &sc->sc_ld_ioh0[gscsio_ld[i].ld_num])) {
				bus_space_unmap(sc->sc_iot,
				    sc->sc_ld_ioh0[gscsio_ld[i].ld_num],
				    gscsio_ld[i].ld_iosize0);
				continue;
			}
		}

		sc->sc_ld_en[gscsio_ld[i].ld_num] = 1;
		printf(" %s", gscsio_ld[i].ld_name);
	}
	printf("\n");

	/* Initialize ACCESS.bus 1 */
	if (sc->sc_ld_en[GSCSIO_LDN_ACB1]) {
		sc->sc_acb[0].sc = sc;
		sc->sc_acb[0].ioh = sc->sc_ld_ioh0[GSCSIO_LDN_ACB1];
		rw_init(&sc->sc_acb[0].buslock, "iiclk");
		gscsio_acb_init(&sc->sc_acb[0], &sc->sc_acb1_tag);
	}

	/* Initialize ACCESS.bus 2 */
	if (sc->sc_ld_en[GSCSIO_LDN_ACB2]) {
		sc->sc_acb[1].sc = sc;
		sc->sc_acb[1].ioh = sc->sc_ld_ioh0[GSCSIO_LDN_ACB2];
		rw_init(&sc->sc_acb[1].buslock, "iiclk");
		gscsio_acb_init(&sc->sc_acb[1], &sc->sc_acb2_tag);
	}
}

void
gscsio_acb_init(struct gscsio_acb *acb, i2c_tag_t tag)
{
	struct gscsio_softc *sc = acb->sc;
	struct i2cbus_attach_args iba;

	/* Enable ACB and configure clock frequency */
	ACB_WRITE(GSCSIO_ACB_CTL2, GSCSIO_ACB_CTL2_EN |
	    (GSCSIO_ACB_FREQ << GSCSIO_ACB_CTL2_FREQ_SHIFT));

	/* Select polling mode */
	ACB_WRITE(GSCSIO_ACB_CTL1, ACB_READ(GSCSIO_ACB_CTL1) &
	    ~GSCSIO_ACB_CTL1_INTEN);

	/* Disable slave address */
	ACB_WRITE(GSCSIO_ACB_ADDR, ACB_READ(GSCSIO_ACB_ADDR) &
	    ~GSCSIO_ACB_ADDR_SAEN);

	/* Attach I2C framework */
	tag->ic_cookie = acb;
	tag->ic_acquire_bus = gscsio_acb_acquire_bus;
	tag->ic_release_bus = gscsio_acb_release_bus;
	tag->ic_send_start = gscsio_acb_send_start;
	tag->ic_send_stop = gscsio_acb_send_stop;
	tag->ic_initiate_xfer = gscsio_acb_initiate_xfer;
	tag->ic_read_byte = gscsio_acb_read_byte;
	tag->ic_write_byte = gscsio_acb_write_byte;

	bzero(&iba, sizeof(iba));
	iba.iba_name = "iic";
	iba.iba_tag = tag;
	config_found(&sc->sc_dev, &iba, iicbus_print);
}

int
gscsio_acb_wait(struct gscsio_acb *acb, int bits, int flags)
{
	struct gscsio_softc *sc = acb->sc;
	u_int8_t st;
	int i;

	for (i = 0; i < 100; i++) {
		st = ACB_READ(GSCSIO_ACB_ST);
		if (st & GSCSIO_ACB_ST_BER) {
			printf("%s: bus error, flags=0x%x\n",
			    sc->sc_dev.dv_xname, flags);
			gscsio_acb_reset(acb);
			return (EIO);
		}
		if (st & GSCSIO_ACB_ST_NEGACK) {
#if 0
			printf("%s: negative ack, flags=0x%x\n",
			    sc->sc_dev.dv_xname, flags);
#endif
			gscsio_acb_reset(acb);
			return (EIO);
		}
		if ((st & bits) == bits)
			break;
		delay(10);
	}
	if ((st & bits) != bits) {
		printf("%s: timeout, flags=0x%x\n",
		    sc->sc_dev.dv_xname, flags);
		gscsio_acb_reset(acb);
		return (ETIMEDOUT);
	}

	return (0);
}

void
gscsio_acb_reset(struct gscsio_acb *acb)
{
	struct gscsio_softc *sc = acb->sc;
	u_int8_t st, ctl;

	/* Clear MASTER, NEGACK and BER */
	st = ACB_READ(GSCSIO_ACB_ST);
	st |= GSCSIO_ACB_ST_MASTER | GSCSIO_ACB_ST_NEGACK | GSCSIO_ACB_ST_BER;
	ACB_WRITE(GSCSIO_ACB_ST, st);

	/* Disable and re-enable ACB */
	ACB_WRITE(GSCSIO_ACB_CTL2, 0);
	ACB_WRITE(GSCSIO_ACB_CTL2, GSCSIO_ACB_CTL2_EN |
	    (GSCSIO_ACB_FREQ << GSCSIO_ACB_CTL2_FREQ_SHIFT));

	/* Send stop */
	ctl = ACB_READ(GSCSIO_ACB_CTL1);
	ctl |= GSCSIO_ACB_CTL1_STOP;
	ACB_WRITE(GSCSIO_ACB_CTL1, ctl);
}

int
gscsio_acb_acquire_bus(void *cookie, int flags)
{
	struct gscsio_acb *acb = cookie;

	if (cold || flags & I2C_F_POLL)
		return (0);

	return (rw_enter(&acb->buslock, RW_WRITE | RW_INTR));
}

void
gscsio_acb_release_bus(void *cookie, int flags)
{
	struct gscsio_acb *acb = cookie;

	if (cold || flags & I2C_F_POLL)
		return;

	rw_exit(&acb->buslock);
}

int
gscsio_acb_send_start(void *cookie, int flags)
{
	struct gscsio_acb *acb = cookie;
	struct gscsio_softc *sc = acb->sc;
	u_int8_t ctl;

	ctl = ACB_READ(GSCSIO_ACB_CTL1);
	ctl |= GSCSIO_ACB_CTL1_START;
	ACB_WRITE(GSCSIO_ACB_CTL1, ctl);

	return (0);
}

int
gscsio_acb_send_stop(void *cookie, int flags)
{
	struct gscsio_acb *acb = cookie;
	struct gscsio_softc *sc = acb->sc;
	u_int8_t ctl;

	ctl = ACB_READ(GSCSIO_ACB_CTL1);
	ctl |= GSCSIO_ACB_CTL1_STOP;
	ACB_WRITE(GSCSIO_ACB_CTL1, ctl);

	return (0);
}

int
gscsio_acb_initiate_xfer(void *cookie, i2c_addr_t addr, int flags)
{
	struct gscsio_acb *acb = cookie;
	struct gscsio_softc *sc = acb->sc;
	u_int8_t ctl;
	int dir;
	int error;

	/* Issue start condition */
	ctl = ACB_READ(GSCSIO_ACB_CTL1);
	ctl |= GSCSIO_ACB_CTL1_START;
	ACB_WRITE(GSCSIO_ACB_CTL1, ctl);

	/* Wait for bus mastership */
	if ((error = gscsio_acb_wait(acb,
	    GSCSIO_ACB_ST_MASTER | GSCSIO_ACB_ST_SDAST, flags)))
		return (error);

	/* Send address byte */
	dir = (flags & I2C_F_READ ? 1 : 0);
	ACB_WRITE(GSCSIO_ACB_SDA, (addr << 1) | dir);

	return (0);
}

int
gscsio_acb_read_byte(void *cookie, uint8_t *bytep, int flags)
{
	struct gscsio_acb *acb = cookie;
	struct gscsio_softc *sc = acb->sc;
	u_int8_t ctl;
	int error;

	/* Wait for the bus to be ready */
	if ((error = gscsio_acb_wait(acb, GSCSIO_ACB_ST_SDAST, flags)))
		return (error);

	/* Acknowledge the last byte */
	if (flags & I2C_F_LAST) {
		ctl = ACB_READ(GSCSIO_ACB_CTL1);
		ctl |= GSCSIO_ACB_CTL1_ACK;
		ACB_WRITE(GSCSIO_ACB_CTL1, ctl);
	}

	/* Read data byte */
	*bytep = ACB_READ(GSCSIO_ACB_SDA);

	return (0);
}

int
gscsio_acb_write_byte(void *cookie, uint8_t byte, int flags)
{
	struct gscsio_acb *acb = cookie;
	struct gscsio_softc *sc = acb->sc;
	u_int8_t ctl;
	int error;

	/* Wait for the bus to be ready */
	if ((error = gscsio_acb_wait(acb, GSCSIO_ACB_ST_SDAST, flags)))
		return (error);

	/* Send stop after the last byte */
	if (flags & I2C_F_STOP) {
		ctl = ACB_READ(GSCSIO_ACB_CTL1);
		ctl |= GSCSIO_ACB_CTL1_STOP;
		ACB_WRITE(GSCSIO_ACB_CTL1, ctl);
	}

	/* Write data byte */
	ACB_WRITE(GSCSIO_ACB_SDA, byte);

	return (0);
}
