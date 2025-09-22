/*	$OpenBSD: pcf8584.c,v 1.11 2010/08/01 18:48:41 kettenis Exp $ */

/*
 * Copyright (c) 2006 David Gwynne <dlg@openbsd.org>
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/rwlock.h>
#include <sys/proc.h>

#include <machine/bus.h>

#include <dev/i2c/i2cvar.h>

#include <dev/ic/pcf8584var.h>

#define PCF_S0			0x00
#define PCF_S1			0x01
#define PCF_S2			0x02
#define PCF_S3			0x03

#define PCF_CTRL_ACK		(1<<0)
#define PCF_CTRL_STO		(1<<1)
#define PCF_CTRL_STA		(1<<2)
#define PCF_CTRL_ENI		(1<<3)
#define PCF_CTRL_ES2		(1<<4)
#define PCF_CTRL_ES1		(1<<5)
#define PCF_CTRL_ESO		(1<<6)
#define PCF_CTRL_PIN		(1<<7)

#define PCF_CTRL_START		(PCF_CTRL_PIN | PCF_CTRL_ESO | \
    PCF_CTRL_STA | PCF_CTRL_ACK)
#define PCF_CTRL_STOP		(PCF_CTRL_PIN | PCF_CTRL_ESO | \
    PCF_CTRL_STO | PCF_CTRL_ACK)
#define PCF_CTRL_REPSTART	(PCF_CTRL_ESO | PCF_CTRL_STA | PCF_CTRL_ACK)
#define PCF_CTRL_IDLE		(PCF_CTRL_PIN | PCF_CTRL_ESO | PCF_CTRL_ACK)

#define PCF_STAT_nBB		(1<<0)
#define PCF_STAT_LAB		(1<<1)
#define PCF_STAT_AAS		(1<<2)
#define PCF_STAT_AD0		(1<<3)
#define PCF_STAT_LRB		(1<<3)
#define PCF_STAT_BER		(1<<4)
#define PCF_STAT_STS		(1<<5)
#define PCF_STAT_PIN		(1<<7)

struct cfdriver pcfiic_cd = {
	NULL, "pcfiic", DV_DULL
};

void		pcfiic_init(struct pcfiic_softc *);
int		pcfiic_i2c_acquire_bus(void *, int);
void		pcfiic_i2c_release_bus(void *, int);
int		pcfiic_i2c_exec(void *, i2c_op_t, i2c_addr_t, const void *,
		    size_t, void *, size_t, int);

int		pcfiic_xmit(struct pcfiic_softc *, u_int8_t, const u_int8_t *,
		    size_t);
int		pcfiic_recv(struct pcfiic_softc *, u_int8_t, u_int8_t *,
		    size_t);

u_int8_t	pcfiic_read(struct pcfiic_softc *, bus_size_t);
void		pcfiic_write(struct pcfiic_softc *, bus_size_t, u_int8_t);
void		pcfiic_choose_bus(struct pcfiic_softc *, u_int8_t);
int		pcfiic_wait_nBB(struct pcfiic_softc *);
int		pcfiic_wait_pin(struct pcfiic_softc *, volatile u_int8_t *);

void
pcfiic_init(struct pcfiic_softc *sc)
{
	/* init S1 */
	pcfiic_write(sc, PCF_S1, PCF_CTRL_PIN);
	/* own address */
	pcfiic_write(sc, PCF_S0, sc->sc_addr);

	/* select clock reg */
	pcfiic_write(sc, PCF_S1, PCF_CTRL_PIN|PCF_CTRL_ES1);
	pcfiic_write(sc, PCF_S0, sc->sc_clock);

	pcfiic_write(sc, PCF_S1, PCF_CTRL_IDLE);

	delay(200000);	/* Multi-Master mode, wait for longest i2c message */
}

void
pcfiic_attach(struct pcfiic_softc *sc, i2c_addr_t addr, u_int8_t clock,
    int swapregs,
    void (*scan_func)(struct device *, struct i2cbus_attach_args *, void *),
    void *scan_arg)
{
	struct i2cbus_attach_args		iba;

	if (swapregs) {
		sc->sc_regmap[PCF_S1] = PCF_S0;
		sc->sc_regmap[PCF_S0] = PCF_S1;
	} else {
		sc->sc_regmap[PCF_S0] = PCF_S0;
		sc->sc_regmap[PCF_S1] = PCF_S1;
	}
	sc->sc_clock = clock;
	sc->sc_addr = addr;

	pcfiic_init(sc);

	printf("\n");

	if (sc->sc_master)
		pcfiic_choose_bus(sc, 0);

	rw_init(&sc->sc_lock, "iiclk");
	sc->sc_i2c.ic_cookie = sc;
	sc->sc_i2c.ic_acquire_bus = pcfiic_i2c_acquire_bus;
	sc->sc_i2c.ic_release_bus = pcfiic_i2c_release_bus;
	sc->sc_i2c.ic_exec = pcfiic_i2c_exec;

	bzero(&iba, sizeof(iba));
	iba.iba_name = "iic";
	iba.iba_tag = &sc->sc_i2c;
	iba.iba_bus_scan = scan_func;
	iba.iba_bus_scan_arg = scan_arg;
	config_found(&sc->sc_dev, &iba, iicbus_print);
}

int
pcfiic_intr(void *arg)
{
	return (0);
}

int
pcfiic_i2c_acquire_bus(void *arg, int flags)
{
	struct pcfiic_softc	*sc = arg;

	if (cold || sc->sc_poll || (flags & I2C_F_POLL))
		return (0);

	return (rw_enter(&sc->sc_lock, RW_WRITE | RW_INTR));
}

void
pcfiic_i2c_release_bus(void *arg, int flags)
{
	struct pcfiic_softc	*sc = arg;

	if (cold || sc->sc_poll || (flags & I2C_F_POLL))
		return;

	rw_exit(&sc->sc_lock);
}

int
pcfiic_i2c_exec(void *arg, i2c_op_t op, i2c_addr_t addr,
    const void *cmdbuf, size_t cmdlen, void *buf, size_t len, int flags)
{
	struct pcfiic_softc	*sc = arg;
	int			ret = 0;

#if 0
        printf("%s: exec op: %d addr: 0x%x cmdlen: %d len: %d flags 0x%x\n",
            sc->sc_dev.dv_xname, op, addr, cmdlen, len, flags);
#endif

	if (cold || sc->sc_poll)
		flags |= I2C_F_POLL;

	if (sc->sc_master)
		pcfiic_choose_bus(sc, addr >> 7);

	if (cmdlen > 0)
		if (pcfiic_xmit(sc, addr & 0x7f, cmdbuf, cmdlen) != 0)
			return (1);

	if (len > 0) {
		if (I2C_OP_WRITE_P(op))
			ret = pcfiic_xmit(sc, addr & 0x7f, buf, len);
		else
			ret = pcfiic_recv(sc, addr & 0x7f, buf, len);
	}
	return (ret);
}

int
pcfiic_xmit(struct pcfiic_softc *sc, u_int8_t addr, const u_int8_t *buf,
    size_t len)
{
	int			i, err = 0;
	volatile u_int8_t	r;

	if (pcfiic_wait_nBB(sc) != 0)
		return (1);

	pcfiic_write(sc, PCF_S0, addr << 1);
	pcfiic_write(sc, PCF_S1, PCF_CTRL_START);

	for (i = 0; i <= len; i++) {
		if (pcfiic_wait_pin(sc, &r) != 0) {
			pcfiic_write(sc, PCF_S1, PCF_CTRL_STOP);
			return (1);
		}

		if (r & PCF_STAT_LRB) {
			err = 1;
			break;
		}

		if (i < len)
			pcfiic_write(sc, PCF_S0, buf[i]);
	}
	pcfiic_write(sc, PCF_S1, PCF_CTRL_STOP);
	return (err);
}

int
pcfiic_recv(struct pcfiic_softc *sc, u_int8_t addr, u_int8_t *buf, size_t len)
{
	int			i = 0, err = 0;
	volatile u_int8_t	r;

	if (pcfiic_wait_nBB(sc) != 0)
		return (1);

	pcfiic_write(sc, PCF_S0, (addr << 1) | 0x01);
	pcfiic_write(sc, PCF_S1, PCF_CTRL_START);

	for (i = 0; i <= len; i++) {
		if (pcfiic_wait_pin(sc, &r) != 0) {
			pcfiic_write(sc, PCF_S1, PCF_CTRL_STOP);
			return (1);
		}

		if ((i != len) && (r & PCF_STAT_LRB)) {
			pcfiic_write(sc, PCF_S1, PCF_CTRL_STOP);
			return (1);
		}

		if (i == len - 1) {
			pcfiic_write(sc, PCF_S1, PCF_CTRL_ESO);
		} else if (i == len) {
			pcfiic_write(sc, PCF_S1, PCF_CTRL_STOP);
		}

		r = pcfiic_read(sc, PCF_S0);
		if (i > 0)
			buf[i - 1] = r;
	}
	return (err);
}

u_int8_t
pcfiic_read(struct pcfiic_softc *sc, bus_size_t r)
{
	bus_space_barrier(sc->sc_iot, sc->sc_ioh, sc->sc_regmap[r], 1,
	    BUS_SPACE_BARRIER_READ);
	return (bus_space_read_1(sc->sc_iot, sc->sc_ioh, sc->sc_regmap[r]));
}

void
pcfiic_write(struct pcfiic_softc *sc, bus_size_t r, u_int8_t v)
{
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, sc->sc_regmap[r], v);
	bus_space_barrier(sc->sc_iot, sc->sc_ioh, 0, 4,
	    BUS_SPACE_BARRIER_WRITE | BUS_SPACE_BARRIER_READ);
	bus_space_read_1(sc->sc_iot, sc->sc_ioh, sc->sc_regmap[PCF_S1]);
}

void
pcfiic_choose_bus(struct pcfiic_softc *sc, u_int8_t bus)
{
	bus_space_write_1(sc->sc_iot, sc->sc_ioh2, 0, bus);
	bus_space_barrier(sc->sc_iot, sc->sc_ioh2, 0, 1,
	    BUS_SPACE_BARRIER_WRITE);
}

int
pcfiic_wait_nBB(struct pcfiic_softc *sc)
{
	int		i;

	for (i = 0; i < 1000; i++) {
		if (pcfiic_read(sc, PCF_S1) & PCF_STAT_nBB)
			return (0);
		delay(1000);
	}
	return (1);
}

int
pcfiic_wait_pin(struct pcfiic_softc *sc, volatile u_int8_t *r)
{
	int		i;

	for (i = 0; i < 1000; i++) {
		*r = pcfiic_read(sc, PCF_S1);
		if ((*r & PCF_STAT_PIN) == 0)
			return (0);
		delay(1000);
	}
	return (1);
}
