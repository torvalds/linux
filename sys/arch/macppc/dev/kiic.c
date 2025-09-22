/*	$OpenBSD: kiic.c,v 1.5 2022/03/13 12:33:01 mpi Exp $	*/
/*	$NetBSD: kiic.c,v 1.1 2003/12/27 02:19:34 grant Exp $	*/

/*-
 * Copyright (c) 2001 Tsubai Masanari.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/device.h>
#include <sys/systm.h>

#include <dev/ofw/openfirm.h>
#include <uvm/uvm_extern.h>
#include <machine/autoconf.h>

#include <macppc/dev/kiicvar.h>
#include <macppc/dev/maci2cvar.h>

int kiic_match(struct device *, void *, void *);
void kiic_attach(struct device *, struct device *, void *);
inline u_int kiic_readreg(struct kiic_softc *, int);
inline void kiic_writereg(struct kiic_softc *, int, u_int);
void kiic_setmode(struct kiic_softc *, u_int, u_int);
void kiic_setspeed(struct kiic_softc *, u_int);
int kiic_intr(struct kiic_softc *);
int kiic_poll(struct kiic_softc *, int);
int kiic_start(struct kiic_softc *, int, int, void *, int);
int kiic_read(struct kiic_softc *, int, int, void *, int);
int kiic_write(struct kiic_softc *, int, int, const void *, int);

/* I2C glue */
int kiic_i2c_acquire_bus(void *, int);
void kiic_i2c_release_bus(void *, int);
int kiic_i2c_exec(void *, i2c_op_t, i2c_addr_t, const void *, size_t,
    void *, size_t, int);

const struct cfattach kiic_ca = {
	sizeof(struct kiic_softc), kiic_match, kiic_attach
};
const struct cfattach kiic_memc_ca = {
	sizeof(struct kiic_softc), kiic_match, kiic_attach
};

struct cfdriver kiic_cd = {
	NULL, "kiic", DV_DULL
};

int
kiic_match(struct device *parent, void *match, void *aux)
{
	struct confargs *ca = aux;

	if (strcmp(ca->ca_name, "i2c") == 0 &&
	   ca->ca_nreg >= 4)
		return (1);

	return (0);
}

void
kiic_attach(struct device *parent, struct device *self, void *aux)
{
	struct kiic_softc *sc = (struct kiic_softc *)self;
	struct confargs *ca = aux;
	struct i2cbus_attach_args iba;
	int rate, node = ca->ca_node;
	char name[32];
	uint32_t reg;

	ca->ca_reg[0] += ca->ca_baseaddr;

	if (OF_getprop(node, "AAPL,i2c-rate", &rate, 4) != 4) {
		printf(": cannot get i2c-rate\n");
		return;
	}
	if (OF_getprop(node, "AAPL,address", &sc->sc_paddr, 4) != 4) {
		printf(": unable to find i2c address\n");
		return;
	}
	if (OF_getprop(node, "AAPL,address-step", &sc->sc_regstep, 4) != 4) {
		printf(": unable to find i2c address step\n");
		return;
	}
	sc->sc_reg = mapiodev(sc->sc_paddr, (DATA+1)*sc->sc_regstep);

	printf("\n");

	kiic_writereg(sc, STATUS, 0);
	kiic_writereg(sc, ISR, 0);
	kiic_writereg(sc, IER, 0);

	kiic_setmode(sc, I2C_STDSUBMODE, 0);
	kiic_setspeed(sc, I2C_100kHz);		/* XXX rate */

	rw_init(&sc->sc_buslock, sc->sc_dev.dv_xname);
	kiic_writereg(sc, IER,I2C_INT_DATA|I2C_INT_ADDR|I2C_INT_STOP);

	sc->sc_busnode = node;

	node = OF_child(ca->ca_node);
	if (OF_getprop(node, "name", name, sizeof(name)) > 0) {
		if (strcmp(name, "i2c-bus") == 0) {
			if (OF_getprop(node, "reg", &reg, sizeof(reg)) > 0)
				sc->sc_busport = reg;

			sc->sc_busnode = node;
		}
	}

	sc->sc_i2c_tag.ic_cookie = sc;
	sc->sc_i2c_tag.ic_acquire_bus = kiic_i2c_acquire_bus;
	sc->sc_i2c_tag.ic_release_bus = kiic_i2c_release_bus;
	sc->sc_i2c_tag.ic_exec = kiic_i2c_exec;

	bzero(&iba, sizeof(iba));
	iba.iba_name = "iic";
	iba.iba_tag = &sc->sc_i2c_tag;
	iba.iba_bus_scan = maciic_scan;
	iba.iba_bus_scan_arg = &sc->sc_busnode;
	config_found(&sc->sc_dev, &iba, NULL);
}

u_int
kiic_readreg(struct kiic_softc *sc, int reg)
{
	u_char *addr = sc->sc_reg + sc->sc_regstep * reg;

	return (*addr);
}

void
kiic_writereg(struct kiic_softc *sc, int reg, u_int val)
{
	u_char *addr = sc->sc_reg + sc->sc_regstep * reg;

	*addr = val;
	asm volatile ("eieio");
	delay(10);
}

void
kiic_setmode(struct kiic_softc *sc, u_int mode, u_int bus)
{
	u_int x;

	KASSERT((mode & ~I2C_MODE) == 0);
	x = kiic_readreg(sc, MODE);
	x &= ~(I2C_MODE);
	if (bus)
		x |= I2C_BUS1;
	else
		x &= ~I2C_BUS1;
	x |= mode;
	kiic_writereg(sc, MODE, x);
}

void
kiic_setspeed(struct kiic_softc *sc, u_int speed)
{
	u_int x;

	KASSERT((speed & ~I2C_SPEED) == 0);
	x = kiic_readreg(sc, MODE);
	x &= ~I2C_SPEED;
	x |= speed;
	kiic_writereg(sc, MODE, x);
}

int
kiic_intr(struct kiic_softc *sc)
{
	u_int isr, x;

	isr = kiic_readreg(sc, ISR);
	if (isr & I2C_INT_ADDR) {
#if 0
		if ((kiic_readreg(sc, STATUS) & I2C_ST_LASTAAK) == 0) {
			/* No slave responded. */
			sc->sc_flags |= I2C_ERROR;
			goto out;
		}
#endif

		if (sc->sc_flags & I2C_READING) {
			if (sc->sc_resid > 1) {
				x = kiic_readreg(sc, CONTROL);
				x |= I2C_CT_AAK;
				kiic_writereg(sc, CONTROL, x);
			}
		} else {
			kiic_writereg(sc, DATA, *sc->sc_data++);
			sc->sc_resid--;
		}
	}

	if (isr & I2C_INT_DATA) {
		if (sc->sc_flags & I2C_READING) {
			if (sc->sc_resid > 0) {
				*sc->sc_data++ = kiic_readreg(sc, DATA);
				sc->sc_resid--;
			}
			if (sc->sc_resid == 0) {	/* Completed */
				kiic_writereg(sc, CONTROL, 0);
				goto out;
			}
		} else {
#if 0
			if ((kiic_readreg(sc, STATUS) & I2C_ST_LASTAAK) == 0) {
				/* No slave responded. */
				sc->sc_flags |= I2C_ERROR;
				goto out;
			}
#endif

			if (sc->sc_resid == 0) {
				x = kiic_readreg(sc, CONTROL) | I2C_CT_STOP;
				kiic_writereg(sc, CONTROL, x);
			} else {
				kiic_writereg(sc, DATA, *sc->sc_data++);
				sc->sc_resid--;
			}
		}
	}

out:
	if (isr & I2C_INT_STOP) {
		kiic_writereg(sc, CONTROL, 0);
		sc->sc_flags &= ~I2C_BUSY;
	}

	kiic_writereg(sc, ISR, isr);

	return (1);
}

int
kiic_poll(struct kiic_softc *sc, int timo)
{
	while (sc->sc_flags & I2C_BUSY) {
		if (kiic_readreg(sc, ISR))
			kiic_intr(sc);
		timo -= 100;
		if (timo < 0) {
			printf("i2c_poll: timeout\n");
			return (-1);
		}
		delay(100);
	}
	return (0);
}

int
kiic_start(struct kiic_softc *sc, int addr, int subaddr, void *data, int len)
{
	int rw = (sc->sc_flags & I2C_READING) ? 1 : 0;
	int timo, x;

	KASSERT((addr & 1) == 0);

	sc->sc_data = data;
	sc->sc_resid = len;
	sc->sc_flags |= I2C_BUSY;

	timo = 1000 + len * 200;

	/* XXX TAS3001 sometimes takes 50ms to finish writing registers. */
	/* if (addr == 0x68) */
		timo += 100000;

	kiic_writereg(sc, ADDR, addr | rw);
	kiic_writereg(sc, SUBADDR, subaddr);

	x = kiic_readreg(sc, CONTROL) | I2C_CT_ADDR;
	kiic_writereg(sc, CONTROL, x);

	if (kiic_poll(sc, timo))
		return (-1);
	if (sc->sc_flags & I2C_ERROR) {
		printf("I2C_ERROR\n");
		return (-1);
	}
	if (sc->sc_resid != 0)
		return (-1);
	return (0);
}

int
kiic_read(struct kiic_softc *sc, int addr, int subaddr, void *data, int len)
{
	sc->sc_flags = I2C_READING;
	return kiic_start(sc, addr, subaddr, data, len);
}

int
kiic_write(struct kiic_softc *sc, int addr, int subaddr, const void *data, int len)
{
	sc->sc_flags = 0;
	return kiic_start(sc, addr, subaddr, (void *)data, len);
}

int
kiic_i2c_acquire_bus(void *cookie, int flags)
{
	struct kiic_softc *sc = cookie;

	return (rw_enter(&sc->sc_buslock, RW_WRITE));
}

void
kiic_i2c_release_bus(void *cookie, int flags)
{
	struct kiic_softc *sc = cookie;

	(void) rw_exit(&sc->sc_buslock);
}

int
kiic_i2c_exec(void *cookie, i2c_op_t op, i2c_addr_t addr,
    const void *cmdbuf, size_t cmdlen, void *buf, size_t len, int flags)
{
	struct kiic_softc *sc = cookie;
	u_int mode = I2C_STDSUBMODE;
	u_int8_t cmd = 0;

	if (!I2C_OP_STOP_P(op) || cmdlen > 1)
		return (EINVAL);

	if (cmdlen == 0)
		mode = I2C_STDMODE;
	else if (I2C_OP_READ_P(op))
		mode = I2C_COMBMODE;

	if (cmdlen > 0)
		cmd = *(u_int8_t *)cmdbuf;

	/*
	 * Use the value read from the "i2c-bus" child node or
	 * the 9th bit of the address to select the right bus.
	 */
	kiic_setmode(sc, mode, sc->sc_busport || addr & 0x80);
	addr &= 0x7f;

	if (I2C_OP_READ_P(op)) {
		if (kiic_read(sc, (addr << 1), cmd, buf, len) != 0)
			return (EIO);
	} else {
		if (kiic_write(sc, (addr << 1), cmd, buf, len) != 0)
			return (EIO);
	}
	return (0);
}
