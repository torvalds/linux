/*	$OpenBSD: amdiic.c,v 1.14 2024/05/24 06:02:53 jsg Exp $	*/

/*
 * Copyright (c) 2005 Alexander Yurchenko <grange@openbsd.org>
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
 * AMD-8111 SMBus controller driver.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/rwlock.h>

#include <machine/bus.h>

#include <dev/pci/pcidevs.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <dev/i2c/i2cvar.h>

#ifdef AMDIIC_DEBUG
#define DPRINTF(x) printf x
#else
#define DPRINTF(x)
#endif

#define AMDIIC_DELAY	100
#define AMDIIC_TIMEOUT	1

/* PCI configuration registers */
#define AMD8111_SMB_BASE	0x10	/* SMBus base address */
#define AMD8111_SMB_MISC	0x48	/* miscellaneous control */
#define AMD8111_SMB_MISC_SU	(1 << 0)	/* 16x clock speed-up */
#define AMD8111_SMB_MISC_INTEN	(1 << 1)	/* PCI IRQ enabled */
#define AMD8111_SMB_MISC_SCIEN	(1 << 2)	/* SCI enabled */

/* SMBus I/O registers */
#define AMD8111_SMB_SC_DATA	0x00	/* data port */
#define AMD8111_SMB_SC_ST	0x04	/* status */
#define AMD8111_SMB_SC_ST_OBF	(1 << 0)	/* output buffer full */
#define AMD8111_SMB_SC_ST_IBF	(1 << 1)	/* input buffer full */
#define AMD8111_SMB_SC_ST_CMD	(1 << 3)	/* command byte */
#define AMD8111_SMB_SC_ST_BITS	"\020\001OBF\002IBF\004CMD"
#define AMD8111_SMB_SC_CMD	0x04	/* command port */
#define AMD8111_SMB_SC_CMD_RD	0x80		/* read */
#define AMD8111_SMB_SC_CMD_WR	0x81		/* write */
#define AMD8111_SMB_SC_IC	0x08	/* interrupt control */

/* Host controller interface registers */
#define AMD8111_SMB_PROTO	0x00	/* protocol */
#define AMD8111_SMB_PROTO_READ	0x01		/* read direction */
#define AMD8111_SMB_PROTO_QUICK	0x02		/* QUICK command */
#define AMD8111_SMB_PROTO_BYTE	0x04		/* BYTE command */
#define AMD8111_SMB_PROTO_BDATA	0x06		/* BYTE DATA command */
#define AMD8111_SMB_PROTO_WDATA	0x08		/* WORD DATA command */
#define AMD8111_SMB_STAT	0x01	/* status */
#define AMD8111_SMB_STAT_MASK	0x1f
#define AMD8111_SMB_STAT_DONE	(1 << 7)	/* command completion */
#define AMD8111_SMB_ADDR	0x02	/* address */
#define AMD8111_SMB_ADDR_SHIFT	1
#define AMD8111_SMB_CMD		0x03	/* SMBus command */
#define AMD8111_SMB_DATA(x)	(0x04 + (x)) /* SMBus data */

struct amdiic_softc {
	struct device		sc_dev;

	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	void *			sc_ih;
	int			sc_poll;

	struct i2c_controller	sc_i2c_tag;
	struct rwlock		sc_i2c_lock;
	struct {
		i2c_op_t     op;
		void *       buf;
		size_t       len;
		int          flags;
		volatile int error;
	}			sc_i2c_xfer;
};

int	amdiic_match(struct device *, void *, void *);
void	amdiic_attach(struct device *, struct device *, void *);

int	amdiic_read(struct amdiic_softc *, u_int8_t);
int	amdiic_write(struct amdiic_softc *, u_int8_t, u_int8_t);
int	amdiic_wait(struct amdiic_softc *, int);

int	amdiic_i2c_acquire_bus(void *, int);
void	amdiic_i2c_release_bus(void *, int);
int	amdiic_i2c_exec(void *, i2c_op_t, i2c_addr_t, const void *, size_t,
	    void *, size_t, int);

int	amdiic_intr(void *);

const struct cfattach amdiic_ca = {
	sizeof(struct amdiic_softc),
	amdiic_match,
	amdiic_attach
};

struct cfdriver amdiic_cd = {
	NULL, "amdiic", DV_DULL
};

const struct pci_matchid amdiic_ids[] = {
	{ PCI_VENDOR_AMD, PCI_PRODUCT_AMD_8111_SMB }
};

int
amdiic_match(struct device *parent, void *match, void *aux)
{
	return (pci_matchbyid(aux, amdiic_ids,
	    sizeof(amdiic_ids) / sizeof(amdiic_ids[0])));
}

void
amdiic_attach(struct device *parent, struct device *self, void *aux)
{
	struct amdiic_softc *sc = (struct amdiic_softc *)self;
	struct pci_attach_args *pa = aux;
	struct i2cbus_attach_args iba;
	pcireg_t conf;
	bus_size_t iosize;
	pci_intr_handle_t ih;
	const char *intrstr = NULL;

	/* Map I/O space */
	if (pci_mapreg_map(pa, AMD8111_SMB_BASE, PCI_MAPREG_TYPE_IO, 0,
	    &sc->sc_iot, &sc->sc_ioh, NULL, &iosize, 0)) {
		printf(": can't map i/o space\n");
		return;
	}

	/* Read configuration */
	conf = pci_conf_read(pa->pa_pc, pa->pa_tag, AMD8111_SMB_MISC);
	DPRINTF((": conf 0x%08x", conf));

	sc->sc_poll = 1;
	if (conf & AMD8111_SMB_MISC_SCIEN) {
		/* No PCI IRQ */
		printf(": SCI");
	} else if (conf & AMD8111_SMB_MISC_INTEN) {
		/* Install interrupt handler */
		if (pci_intr_map(pa, &ih) == 0) {
			intrstr = pci_intr_string(pa->pa_pc, ih);
			sc->sc_ih = pci_intr_establish(pa->pa_pc, ih, IPL_BIO,
			    amdiic_intr, sc, sc->sc_dev.dv_xname);
			if (sc->sc_ih != NULL) {
				printf(": %s", intrstr);
				sc->sc_poll = 0;
			}
		}
		if (sc->sc_poll)
			printf(": polling");
	}

	printf("\n");

	/* Attach I2C bus */
	rw_init(&sc->sc_i2c_lock, "iiclk");
	sc->sc_i2c_tag.ic_cookie = sc;
	sc->sc_i2c_tag.ic_acquire_bus = amdiic_i2c_acquire_bus;
	sc->sc_i2c_tag.ic_release_bus = amdiic_i2c_release_bus;
	sc->sc_i2c_tag.ic_exec = amdiic_i2c_exec;

	bzero(&iba, sizeof(iba));
	iba.iba_name = "iic";
	iba.iba_tag = &sc->sc_i2c_tag;
	config_found(self, &iba, iicbus_print);

	return;
}

int
amdiic_read(struct amdiic_softc *sc, u_int8_t reg)
{
	if (amdiic_wait(sc, 0))
		return (-1);
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, AMD8111_SMB_SC_CMD,
	    AMD8111_SMB_SC_CMD_RD);
	if (amdiic_wait(sc, 0))
		return (-1);
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, AMD8111_SMB_SC_DATA, reg);
	if (amdiic_wait(sc, 1))
		return (-1);

	return (bus_space_read_1(sc->sc_iot, sc->sc_ioh, AMD8111_SMB_SC_DATA));
}

int
amdiic_write(struct amdiic_softc *sc, u_int8_t reg, u_int8_t val)
{
	if (amdiic_wait(sc, 0))
		return (-1);
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, AMD8111_SMB_SC_CMD,
	    AMD8111_SMB_SC_CMD_WR);
	if (amdiic_wait(sc, 0))
		return (-1);
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, AMD8111_SMB_SC_DATA, reg);
	if (amdiic_wait(sc, 0))
		return (-1);
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, AMD8111_SMB_SC_DATA, val);

	return (0);
}

int
amdiic_wait(struct amdiic_softc *sc, int output)
{
	int retries;
	u_int8_t st;

	for (retries = 100; retries > 0; retries--) {
		st = bus_space_read_1(sc->sc_iot, sc->sc_ioh,
		    AMD8111_SMB_SC_ST);
		if (output && (st & AMD8111_SMB_SC_ST_OBF))
			return (0);
		if (!output && (st & AMD8111_SMB_SC_ST_IBF) == 0)
			return (0);
		DELAY(1);
	}
	DPRINTF(("%s: %s wait timeout: st 0x%b\n", sc->sc_dev.dv_xname,
	    (output ? "output" : "input"), st));

	return (1);
}

int
amdiic_i2c_acquire_bus(void *cookie, int flags)
{
	struct amdiic_softc *sc = cookie;

	if (cold || sc->sc_poll || (flags & I2C_F_POLL))
		return (0);

	return (rw_enter(&sc->sc_i2c_lock, RW_WRITE | RW_INTR));
}

void
amdiic_i2c_release_bus(void *cookie, int flags)
{
	struct amdiic_softc *sc = cookie;

	if (cold || sc->sc_poll || (flags & I2C_F_POLL))
		return;

	rw_exit(&sc->sc_i2c_lock);
}

int
amdiic_i2c_exec(void *cookie, i2c_op_t op, i2c_addr_t addr,
    const void *cmdbuf, size_t cmdlen, void *buf, size_t len, int flags)
{
	struct amdiic_softc *sc = cookie;
	u_int8_t *b;
	u_int8_t proto, st;
	int retries;

	DPRINTF(("%s: exec: op %d, addr 0x%02x, cmdlen %d, len %d, "
	    "flags 0x%02x\n", sc->sc_dev.dv_xname, op, addr,
	    cmdlen, len, flags));

	if (cold || sc->sc_poll)
		flags |= I2C_F_POLL;

	if (!I2C_OP_STOP_P(op) || cmdlen > 1 || len > 2)
		return (1);

	/* Setup transfer */
	sc->sc_i2c_xfer.op = op;
	sc->sc_i2c_xfer.buf = buf;
	sc->sc_i2c_xfer.len = len;
	sc->sc_i2c_xfer.flags = flags;
	sc->sc_i2c_xfer.error = 0;

	/* Set slave address */
	if (amdiic_write(sc, AMD8111_SMB_ADDR,
	    addr << AMD8111_SMB_ADDR_SHIFT) == -1)
		return (1);

	b = (void *)cmdbuf;
	if (cmdlen > 0)
		/* Set command byte */
		if (amdiic_write(sc, AMD8111_SMB_CMD, b[0]) == -1)
			return (1);

	if (I2C_OP_WRITE_P(op)) {
		/* Write data */
		b = buf;
		if (len > 0)
			if (amdiic_write(sc, AMD8111_SMB_DATA(0), b[0]) == -1)
				return (1);
		if (len > 1)
			if (amdiic_write(sc, AMD8111_SMB_DATA(1), b[1]) == -1)
				return (1);
	}

	/* Set SMBus command */
	if (len == 0)
		proto = AMD8111_SMB_PROTO_BYTE;
	else if (len == 1)
		proto = AMD8111_SMB_PROTO_BDATA;
	else if (len == 2)
		proto = AMD8111_SMB_PROTO_WDATA;
	else
		panic("%s: unexpected len %zd", __func__, len);

	/* Set direction */
	if (I2C_OP_READ_P(op))
		proto |= AMD8111_SMB_PROTO_READ;

	/* Start transaction */
	amdiic_write(sc, AMD8111_SMB_PROTO, proto);

	if (flags & I2C_F_POLL) {
		/* Poll for completion */
		DELAY(AMDIIC_DELAY);
		for (retries = 1000; retries > 0; retries--) {
			st = amdiic_read(sc, AMD8111_SMB_STAT);
			if (st != 0)
				break;
			DELAY(AMDIIC_DELAY);
		}
		if (st == 0) {
			printf("%s: exec: op %d, addr 0x%02x, cmdlen %zu, "
			    "len %zu, flags 0x%02x: timeout\n",
			    sc->sc_dev.dv_xname, op, addr, cmdlen, len, flags);
			return (1);
		}
		amdiic_intr(sc);
	} else {
		/* Wait for interrupt */
		if (tsleep_nsec(sc, PRIBIO, "amdiic",
		    SEC_TO_NSEC(AMDIIC_TIMEOUT)))
			return (1);
	}	

	if (sc->sc_i2c_xfer.error)
		return (1);

	return (0);
}

int
amdiic_intr(void *arg)
{
	struct amdiic_softc *sc = arg;
	int st;
	u_int8_t *b;
	size_t len;

	/* Read status */
	if ((st = amdiic_read(sc, AMD8111_SMB_STAT)) == -1)
		return (-1);
	if (st == 0)
		/* Interrupt was not for us */
		return (0);

	DPRINTF(("%s: intr: st 0x%02x\n", sc->sc_dev.dv_xname, st));

	/* Check for errors */
	if ((st & AMD8111_SMB_STAT_MASK) != 0) {
		sc->sc_i2c_xfer.error = 1;
		goto done;
	}

	if (st & AMD8111_SMB_STAT_DONE) {
		if (I2C_OP_WRITE_P(sc->sc_i2c_xfer.op))
			goto done;

		/* Read data */
		b = sc->sc_i2c_xfer.buf;
		len = sc->sc_i2c_xfer.len;
		if (len > 0)
			b[0] = amdiic_read(sc, AMD8111_SMB_DATA(0));
		if (len > 1)
			b[1] = amdiic_read(sc, AMD8111_SMB_DATA(1));
	}

done:
	if ((sc->sc_i2c_xfer.flags & I2C_F_POLL) == 0)
		wakeup(sc);
	return (1);
}
