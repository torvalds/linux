/*	$OpenBSD: mgiic.c,v 1.4 2021/10/24 17:05:03 mpi Exp $	*/
/*
 * Copyright (c) 2008 Theo de Raadt <deraadt@openbsd.org>
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
#include <sys/device.h>
#include <sys/errno.h>
#include <sys/malloc.h>
#include <sys/systm.h>
#include <sys/rwlock.h>

#include <uvm/uvm_extern.h>

#include <machine/bus.h>
#include <machine/autoconf.h>
#include <machine/openfirm.h>

#include <dev/i2c/i2cvar.h>
#include <sparc64/dev/ofwi2cvar.h>

#define MGSLAVEADDR		0x00
#define MGSLAVEXADDR		0x08
#define MGDATA			0x10
#define MGCONTROL			0x18
#define  MGCONTROL_IEN			0x80
#define  MGCONTROL_ENAB			0x40
#define  MGCONTROL_STA			0x20
#define  MGCONTROL_STP			0x10
#define  MGCONTROL_IFLG			0x08
#define  MGCONTROL_AAK			0x04
#define MGSTATUS		0x20
#define  MGSTATUS_BUSERR		0x00
#define  MGSTATUS_STARTSENT		0x08
#define	 MGSTATUS_REPEATSTART		0x10
#define  MGSTATUS_ADDR_W_ACKR		0x18
#define  MGSTATUS_ADDR_W_NOACKR		0x20
#define  MGSTATUS_MDATA_ACKR		0x28
#define  MGSTATUS_MDATA_NOACKR		0x30
#define  MGSTATUS_ARBLOST		0x38
#define  MGSTATUS_ADDR_R_ACKR		0x40
#define  MGSTATUS_ADDR_R_NOACKR		0x48
#define  MGSTATUS_MDATA_ACKT		0x50
#define  MGSTATUS_MDATA_NOACKT		0x58
#define  MGSTATUS_SADDR_W_ACKT		0x60
#define  MGSTATUS_ARBLOST_SLW_ACKT	0x68
#define  MGSTATUS_GC_TACK		0x70
#define  MGSTATUS_ARBLOST_GC_ACKT	0x78
#define  MGSTATUS_IDLE			0xf8
#define MGCLOCKCONTROL		0x28
#define MGSOFTRESET		0x30

struct mgiic_softc {
	struct device sc_dev;

	bus_space_tag_t		sc_bt;
	bus_space_handle_t	sc_regh;


	int			sc_poll;

	struct i2c_controller	sc_i2c;
	struct rwlock		sc_lock;
};

int			mgiic_match(struct device *, void *, void *);
void			mgiic_attach(struct device *, struct device *, void *);

struct cfdriver mgiic_cd = {
        NULL, "mgiic", DV_DULL
};

const struct cfattach mgiic_ca = {
        sizeof(struct mgiic_softc), mgiic_match, mgiic_attach
};

int			mgiic_i2c_acquire_bus(void *, int);
void			mgiic_i2c_release_bus(void *, int);
int			mgiic_i2c_exec(void *, i2c_op_t, i2c_addr_t, const void *,
			    size_t, void *, size_t, int);

int			mgiic_xmit(struct mgiic_softc *, u_int8_t, const u_int8_t *,
			    size_t);
int			mgiic_recv(struct mgiic_softc *, u_int8_t, u_int8_t *, size_t);
volatile u_int8_t	mgiic_read(struct mgiic_softc *, bus_size_t);
volatile void		mgiic_write(struct mgiic_softc *, bus_size_t, u_int8_t);
volatile void		mgiic_control(struct mgiic_softc *, u_int8_t, u_int8_t);
int			mgiic_poll(struct mgiic_softc *);

int
mgiic_match(struct device *parent, void *match, void *aux)
{
	struct mainbus_attach_args *ma = aux;
	char compat[32];

	if (strcmp(ma->ma_name, "i2c") != 0)
		return (0);
	if (OF_getprop(ma->ma_node, "compatible", compat, sizeof(compat)) == -1)
		return (0);
	if (strcmp(compat, "fire-i2c") == 0)
		return (1);
	return (0);
}

void
mgiic_attach(struct device *parent, struct device *self, void *aux)
{
	struct mgiic_softc *sc = (struct mgiic_softc *)self;
	struct mainbus_attach_args *ma = aux;
	struct i2cbus_attach_args iba;

	sc->sc_bt = ma->ma_bustag;

	if (bus_space_map(sc->sc_bt, ma->ma_reg[0].ur_paddr,
	    ma->ma_reg[0].ur_len, 0, &sc->sc_regh)) {
		printf(": failed to map preg\n");
		return;
	}

	rw_init(&sc->sc_lock, "iiclk");
	sc->sc_i2c.ic_cookie = sc;
	sc->sc_i2c.ic_acquire_bus = mgiic_i2c_acquire_bus;
	sc->sc_i2c.ic_release_bus = mgiic_i2c_release_bus;
	sc->sc_i2c.ic_exec = mgiic_i2c_exec;

	printf("\n");

	bzero(&iba, sizeof(iba));
	iba.iba_name = "iic";
	iba.iba_tag = &sc->sc_i2c;
	iba.iba_bus_scan = ofwiic_scan;
	iba.iba_bus_scan_arg = &ma->ma_node;
	config_found(&sc->sc_dev, &iba, iicbus_print);
}

int
mgiic_i2c_acquire_bus(void *arg, int flags)
{
	struct mgiic_softc     *sc = arg;

	if (cold || sc->sc_poll || (flags & I2C_F_POLL))
		return (0);

	return (rw_enter(&sc->sc_lock, RW_WRITE | RW_INTR));
}

void
mgiic_i2c_release_bus(void *arg, int flags)
{
	struct mgiic_softc     *sc = arg;

	if (cold || sc->sc_poll || (flags & I2C_F_POLL))
		return;

	rw_exit(&sc->sc_lock);
}

int
mgiic_i2c_exec(void *arg, i2c_op_t op, i2c_addr_t addr,
    const void *cmdbuf, size_t cmdlen, void *buf, size_t len, int flags)
{
	struct mgiic_softc	*sc = arg;
	int			ret = 0;

	if (addr & ~0x7f)
		return (1);

	if (cold || sc->sc_poll)
		flags |= I2C_F_POLL;

	if (cmdlen > 0) {
		ret = mgiic_xmit(sc, addr & 0x7f, cmdbuf, cmdlen);
		if (ret != 0)
			goto done;
	}

	if (len > 0) {
		if (I2C_OP_WRITE_P(op))
			ret = mgiic_xmit(sc, addr & 0x7f, buf, len);
		else
			ret = mgiic_recv(sc, addr & 0x7f, buf, len);
	}
done:
	printf("e%d\n", ret);
	return (ret);
}

int
mgiic_xmit(struct mgiic_softc *sc, u_int8_t addr, const u_int8_t *buf,
    size_t len)
{
	int err = 1, i = 0;

top:
	printf("xmit s%02x STA ", mgiic_read(sc, MGSTATUS));
	mgiic_control(sc, MGCONTROL_STA, MGCONTROL_IFLG);

	if (mgiic_poll(sc))
		goto bail;
	printf("s%02x ", mgiic_read(sc, MGSTATUS));
	if (mgiic_read(sc, MGSTATUS) != MGSTATUS_STARTSENT)
		goto bail;

	mgiic_write(sc, MGDATA, addr << 1);
	printf("a%02x ", addr << 1);
	mgiic_control(sc, 0, MGCONTROL_IFLG);

	while (i < len) {
		if (mgiic_poll(sc))
			 goto bail;
		printf("s%02x ", mgiic_read(sc, MGSTATUS));
		switch (mgiic_read(sc, MGSTATUS)) {
		case MGSTATUS_ADDR_W_ACKR:
		case MGSTATUS_MDATA_ACKR:
			mgiic_write(sc, MGDATA, buf[i]);
			printf("w%02x ", buf[i]);
			i++;
			mgiic_control(sc, 0, MGCONTROL_IFLG);
			break;
		case MGSTATUS_ADDR_W_NOACKR:
		case MGSTATUS_MDATA_NOACKR:
			mgiic_write(sc, MGDATA, buf[i]);
			printf("w%02x ", buf[i]);
			mgiic_control(sc, 0, MGCONTROL_IFLG);
			break;
		case MGSTATUS_BUSERR:
			mgiic_control(sc, MGCONTROL_STP, MGCONTROL_IFLG);
			i = 0;
			if (mgiic_poll(sc))
				goto bail;
			goto top;
		case MGSTATUS_IDLE:
		default:
			err = 1;
			goto bail;
		}
	}
	printf("OK ");
	err = 0;
bail:
	if (err)
		printf("BAIL STP s%02x\n", mgiic_read(sc, MGSTATUS));
	mgiic_control(sc, MGCONTROL_STP, MGCONTROL_IFLG);
	while (mgiic_read(sc, MGSTATUS) != MGSTATUS_IDLE)
		;
	printf("s%02x\n", mgiic_read(sc, MGSTATUS));
	return (err);
}

int
mgiic_recv(struct mgiic_softc *sc, u_int8_t addr, u_int8_t *buf, size_t len)
{
	int err = 1, i = 0;

	printf("recv s%02x ", mgiic_read(sc, MGSTATUS));
	mgiic_control(sc, MGCONTROL_STA, MGCONTROL_IFLG);
	if (mgiic_poll(sc))
		goto bail;

	printf("s%02x ", mgiic_read(sc, MGSTATUS));
	if (mgiic_read(sc, MGSTATUS) != MGSTATUS_STARTSENT)
		goto bail;

re_address:
	mgiic_write(sc, MGDATA, (addr << 1) | 0x01);
	printf("a%02x ", (addr << 1) | 0x01);
	mgiic_control(sc, 0, MGCONTROL_IFLG);

	while (i < len) {
		if (mgiic_poll(sc))
			goto bail;
		printf("s%02x ", mgiic_read(sc, MGSTATUS));
		switch (mgiic_read(sc, MGSTATUS)) {
		case MGSTATUS_ADDR_R_ACKR:
			if (len - i > 1)
				mgiic_control(sc, MGCONTROL_AAK, MGCONTROL_IFLG);
			else
				mgiic_control(sc, 0, MGCONTROL_IFLG);
			break;
		case MGSTATUS_ADDR_R_NOACKR:
			mgiic_control(sc, MGCONTROL_STA, MGCONTROL_IFLG);
			break;
		case MGSTATUS_REPEATSTART:
			goto re_address;
		case MGSTATUS_MDATA_ACKT:
			buf[i] = mgiic_read(sc, MGDATA);
			printf("r%02x ", buf[i]);
			i++;
			if (len - i > 1)
				mgiic_control(sc, MGCONTROL_AAK, MGCONTROL_IFLG);
			else
				mgiic_control(sc, 0, MGCONTROL_IFLG|MGCONTROL_AAK);
			break;
		case MGSTATUS_MDATA_NOACKT:
			buf[i] = mgiic_read(sc, MGDATA);
			printf("r%02x ", buf[i]);
			i++;
			if (len == i) {
				printf("DONE ");
				err = 0;
				goto bail;
			}
			printf("SHORT ");
			goto bail;
			break;
		default:
			printf("BAD");
			goto bail;
		}
	}
	printf("OK ");
	err = 0;
bail:
	if (err)
		printf("BAIL STP s%02x\n", mgiic_read(sc, MGSTATUS));
	mgiic_control(sc, MGCONTROL_STP, MGCONTROL_IFLG | MGCONTROL_AAK);
	while (mgiic_read(sc, MGSTATUS) != MGSTATUS_IDLE)
		;
	printf("s%02x\n", mgiic_read(sc, MGSTATUS));
	return (err);
}

volatile u_int8_t
mgiic_read(struct mgiic_softc *sc, bus_size_t r)
{
	bus_space_barrier(sc->sc_bt, sc->sc_regh, r, 8,
	    BUS_SPACE_BARRIER_READ);
	return (bus_space_read_8(sc->sc_bt, sc->sc_regh, r)) & 0xff;
}

volatile void
mgiic_write(struct mgiic_softc *sc, bus_size_t r, u_int8_t v)
{
	u_int64_t val = v;

	bus_space_write_8(sc->sc_bt, sc->sc_regh, r, val);
	bus_space_barrier(sc->sc_bt, sc->sc_regh, r, 8,
	    BUS_SPACE_BARRIER_WRITE);
}

volatile void
mgiic_control(struct mgiic_softc *sc, u_int8_t on, u_int8_t off)
{
	u_int8_t val;

	val = (mgiic_read(sc, MGCONTROL) | on) & ~off;
	mgiic_write(sc, MGCONTROL, val);
}

int
mgiic_poll(struct mgiic_softc *sc)
{
	int		i;

	for (i = 0; i < 1000; i++) {
		if (mgiic_read(sc, MGCONTROL) & MGCONTROL_IFLG)
			return (0);
		delay(100);
	}
	return (1);
}
