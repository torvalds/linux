/*	$OpenBSD: ti_iic.c,v 1.15 2021/10/24 17:52:28 mpi Exp $	*/
/* $NetBSD: ti_iic.c,v 1.4 2013/04/25 13:04:27 rkujawa Exp $ */

/*
 * Copyright (c) 2013 Manuel Bouyer.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*-
 * Copyright (c) 2012 Jared D. McNeill <jmcneill@invisible.ca>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/rwlock.h>

#include <machine/bus.h>
#include <machine/intr.h>
#include <machine/fdt.h>

#include <dev/i2c/i2cvar.h>

#include <armv7/omap/prcmvar.h>
#include <armv7/omap/ti_iicreg.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_pinctrl.h>
#include <dev/ofw/fdt.h>

#ifndef AM335X_I2C_SLAVE_ADDR
#define AM335X_I2C_SLAVE_ADDR	0x01
#endif

#ifdef I2CDEBUG
#define DPRINTF(args)	printf args
#else
#define DPRINTF(args)
#endif

/* operation in progress */
typedef enum {
	TI_I2CREAD,
	TI_I2CWRITE,
	TI_I2CDONE,
	TI_I2CERROR
} ti_i2cop_t;

struct ti_iic_softc {
	struct device		sc_dev;
	struct i2c_controller	sc_ic;
	struct rwlock		sc_buslock;
	struct device		*sc_i2cdev;

	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	void			*sc_ih;
	int			sc_node;
	ti_i2cop_t		sc_op;
	int			sc_buflen;
	int			sc_bufidx;
	char			*sc_buf;

	int			sc_rxthres;
	int			sc_txthres;
};


#define I2C_READ_REG(sc, reg)		\
	bus_space_read_2((sc)->sc_iot, (sc)->sc_ioh, (reg))
#define I2C_READ_DATA(sc)		\
	bus_space_read_1((sc)->sc_iot, (sc)->sc_ioh, AM335X_I2C_DATA);
#define I2C_WRITE_REG(sc, reg, val)	\
	bus_space_write_2((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define I2C_WRITE_DATA(sc, val)		\
	bus_space_write_1((sc)->sc_iot, (sc)->sc_ioh, AM335X_I2C_DATA, (val))

#define DEVNAME(sc)	((sc)->sc_dev.dv_xname)

int	ti_iic_match(struct device *, void *, void *);
void	ti_iic_attach(struct device *, struct device *, void *);
int	ti_iic_intr(void *);

int	ti_iic_acquire_bus(void *, int);
void	ti_iic_release_bus(void *, int);
int	ti_iic_exec(void *, i2c_op_t, i2c_addr_t, const void *, size_t, void *,
	    size_t, int);
void	ti_iic_scan(struct device *, struct i2cbus_attach_args *, void *);

int	ti_iic_reset(struct ti_iic_softc *);
int	ti_iic_op(struct ti_iic_softc *, i2c_addr_t, ti_i2cop_t, uint8_t *,
	    size_t, int);
void	ti_iic_handle_intr(struct ti_iic_softc *, uint32_t);
void	ti_iic_do_read(struct ti_iic_softc *, uint32_t);
void	ti_iic_do_write(struct ti_iic_softc *, uint32_t);

int	ti_iic_wait(struct ti_iic_softc *, uint16_t, uint16_t, int);
uint32_t	ti_iic_stat(struct ti_iic_softc *, uint32_t);
int	ti_iic_flush(struct ti_iic_softc *);

const struct cfattach tiiic_ca = {
	sizeof (struct ti_iic_softc), ti_iic_match, ti_iic_attach
};

struct cfdriver tiiic_cd = {
	NULL, "tiiic", DV_DULL
};

int
ti_iic_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "ti,omap4-i2c");
}

void
ti_iic_attach(struct device *parent, struct device *self, void *aux)
{
	struct ti_iic_softc *sc = (struct ti_iic_softc *)self;
	struct fdt_attach_args *faa = aux;
	struct i2cbus_attach_args iba;
	uint16_t rev;
	int unit, len;
	char hwmods[128];

	if (faa->fa_nreg < 1)
		return;

	sc->sc_iot = faa->fa_iot;
	sc->sc_node = faa->fa_node;

	unit = -1;
	if ((len = OF_getprop(faa->fa_node, "ti,hwmods", hwmods,
	    sizeof(hwmods))) == 5) {
		if (!strncmp(hwmods, "i2c", 3) &&
		    (hwmods[3] > '0') && (hwmods[3] <= '9'))
			unit = hwmods[3] - '1';
	}

	rw_init(&sc->sc_buslock, "tiiilk");

	sc->sc_rxthres = sc->sc_txthres = 4;

	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh))
		panic("%s: bus_space_map failed!", DEVNAME(sc));

	pinctrl_byname(faa->fa_node, "default");

	sc->sc_ih = arm_intr_establish_fdt(faa->fa_node, IPL_NET,
	    ti_iic_intr, sc, DEVNAME(sc));

	if (unit != -1)
		prcm_enablemodule(PRCM_I2C0 + unit);

	rev = I2C_READ_REG(sc, AM335X_I2C_REVNB_LO);
	printf(" rev %d.%d\n",
	    (int)I2C_REVNB_LO_MAJOR(rev),
	    (int)I2C_REVNB_LO_MINOR(rev));

	ti_iic_reset(sc);
	ti_iic_flush(sc);

	sc->sc_ic.ic_cookie = sc;
	sc->sc_ic.ic_acquire_bus = ti_iic_acquire_bus;
	sc->sc_ic.ic_release_bus = ti_iic_release_bus;
	sc->sc_ic.ic_exec = ti_iic_exec;

	bzero(&iba, sizeof iba);
	iba.iba_name = "iic";
	iba.iba_tag = &sc->sc_ic;
	iba.iba_bus_scan = ti_iic_scan;
	iba.iba_bus_scan_arg = &sc->sc_node;
	(void) config_found(&sc->sc_dev, &iba, iicbus_print);
}

int
ti_iic_intr(void *arg)
{
	struct ti_iic_softc *sc = arg;
	uint32_t stat;

	DPRINTF(("ti_iic_intr\n"));
	stat = I2C_READ_REG(sc, AM335X_I2C_IRQSTATUS);
	I2C_WRITE_REG(sc, AM335X_I2C_IRQSTATUS, stat);
	DPRINTF(("ti_iic_intr pre handle sc->sc_op eq %#x\n", sc->sc_op));

	ti_iic_handle_intr(sc, stat);

	if (sc->sc_op == TI_I2CERROR || sc->sc_op == TI_I2CDONE) {
		DPRINTF(("ti_iic_intr post handle sc->sc_op %#x\n", sc->sc_op));
		wakeup(&sc->sc_dev);
	}

	DPRINTF(("ti_iic_intr status 0x%x\n", stat));

	return 1;
}

int
ti_iic_acquire_bus(void *opaque, int flags)
{
	struct ti_iic_softc *sc = opaque;

	if (flags & I2C_F_POLL)
		return 0;

	return (rw_enter(&sc->sc_buslock, RW_WRITE));
}

void
ti_iic_release_bus(void *opaque, int flags)
{
	struct ti_iic_softc *sc = opaque;

	if (flags & I2C_F_POLL)
		return;

	rw_exit(&sc->sc_buslock);
}

int
ti_iic_exec(void *opaque, i2c_op_t op, i2c_addr_t addr,
    const void *cmdbuf, size_t cmdlen, void *buf, size_t len, int flags)
{
	struct ti_iic_softc *sc = opaque;
	int err = 0;

	DPRINTF(("ti_iic_exec: op 0x%x cmdlen %zd len %zd flags 0x%x\n",
	    op, cmdlen, len, flags));

#define __UNCONST(a)  ((void *)(unsigned long)(const void *)(a))
	if (cmdlen > 0) {
		err = ti_iic_op(sc, addr, TI_I2CWRITE, __UNCONST(cmdbuf),
		    cmdlen, (I2C_OP_READ_P(op) ? 0 : I2C_F_STOP) | flags);
		if (err)
			goto done;
	}
	if (I2C_OP_STOP_P(op))
		flags |= I2C_F_STOP;

	/*
	 * I2C controller doesn't allow for zero-byte transfers.
	 */
	if (len == 0)
		goto done;

	if (I2C_OP_READ_P(op))
		err = ti_iic_op(sc, addr, TI_I2CREAD, buf, len, flags);
	else
		err = ti_iic_op(sc, addr, TI_I2CWRITE, buf, len, flags);

done:
	if (err)
		ti_iic_reset(sc);

	ti_iic_flush(sc);

	DPRINTF(("ti_iic_exec: done %d\n", err));
	return err;
}

int
ti_iic_reset(struct ti_iic_softc *sc)
{
	uint32_t psc, scll, sclh;
	int i;

	DPRINTF(("ti_iic_reset\n"));

	/* Disable */
	I2C_WRITE_REG(sc, AM335X_I2C_CON, 0);
	/* Soft reset */
	I2C_WRITE_REG(sc, AM335X_I2C_SYSC, I2C_SYSC_SRST);
	delay(1000);
	/* enable so that we can check for reset complete */
	I2C_WRITE_REG(sc, AM335X_I2C_CON, I2C_CON_EN);
	delay(1000);
	for (i = 0; i < 1000; i++) { /* 1s delay for reset */
		if (I2C_READ_REG(sc, AM335X_I2C_SYSS) & I2C_SYSS_RDONE)
			break;
	}
	/* Disable again */
	I2C_WRITE_REG(sc, AM335X_I2C_CON, 0);
	delay(50000);

	if (i >= 1000) {
		printf("%s: couldn't reset module\n", DEVNAME(sc));
		return 1;
	}

	/* XXX standard speed only */
	psc = 3;
	scll = 53;
	sclh = 55;

	/* Clocks */
	I2C_WRITE_REG(sc, AM335X_I2C_PSC, psc);
	I2C_WRITE_REG(sc, AM335X_I2C_SCLL, scll);
	I2C_WRITE_REG(sc, AM335X_I2C_SCLH, sclh);

	/* Own I2C address */
	I2C_WRITE_REG(sc, AM335X_I2C_OA, AM335X_I2C_SLAVE_ADDR);

	/* 5 bytes fifo */
	I2C_WRITE_REG(sc, AM335X_I2C_BUF,
	    I2C_BUF_RXTRSH(sc->sc_rxthres) | I2C_BUF_TXTRSH(sc->sc_txthres));

	/* Enable */
	I2C_WRITE_REG(sc, AM335X_I2C_CON, I2C_CON_EN);

	return 0;
}

int
ti_iic_op(struct ti_iic_softc *sc, i2c_addr_t addr, ti_i2cop_t op,
    uint8_t *buf, size_t buflen, int flags)
{
	uint16_t con, stat, mask;
	int err, retry;

	KASSERT(op == TI_I2CREAD || op == TI_I2CWRITE);
	DPRINTF(("ti_iic_op: addr %#x op %#x buf %p buflen %#x flags %#x\n",
	    addr, op, buf, (unsigned int) buflen, flags));

	mask = I2C_IRQSTATUS_ARDY | I2C_IRQSTATUS_NACK | I2C_IRQSTATUS_AL;
	if (op == TI_I2CREAD)
		mask |= I2C_IRQSTATUS_RDR | I2C_IRQSTATUS_RRDY;
	else
		mask |= I2C_IRQSTATUS_XDR | I2C_IRQSTATUS_XRDY;

	err = ti_iic_wait(sc, I2C_IRQSTATUS_BB, 0, flags);
	if (err) {
		DPRINTF(("ti_iic_op: wait error %d\n", err));
		return err;
	}

	con = I2C_CON_EN;
	con |= I2C_CON_MST;
	con |= I2C_CON_STT;
	if (flags & I2C_F_STOP)
		con |= I2C_CON_STP;
	if (addr & ~0x7f)
		con |= I2C_CON_XSA;
	if (op == TI_I2CWRITE)
		con |= I2C_CON_TRX;

	sc->sc_op = op;
	sc->sc_buf = buf;
	sc->sc_buflen = buflen;
	sc->sc_bufidx = 0;

	I2C_WRITE_REG(sc,
	    AM335X_I2C_CON, I2C_CON_EN | I2C_CON_MST | I2C_CON_STP);
	DPRINTF(("ti_iic_op: op %d con 0x%x ", op, con));
	I2C_WRITE_REG(sc, AM335X_I2C_CNT, buflen);
	I2C_WRITE_REG(sc, AM335X_I2C_SA, (addr & I2C_SA_MASK));
	DPRINTF(("SA 0x%x len %d\n",
	    I2C_READ_REG(sc, AM335X_I2C_SA), I2C_READ_REG(sc, AM335X_I2C_CNT)));

	if ((flags & I2C_F_POLL) == 0) {
		/* clear any pending interrupt */
		I2C_WRITE_REG(sc, AM335X_I2C_IRQSTATUS,
		    I2C_READ_REG(sc, AM335X_I2C_IRQSTATUS));
		/* and enable */
		I2C_WRITE_REG(sc, AM335X_I2C_IRQENABLE_SET, mask);
	}
	/* start transfer */
	I2C_WRITE_REG(sc, AM335X_I2C_CON, con);

	if ((flags & I2C_F_POLL) == 0) {
		/* and wait for completion */
		DPRINTF(("ti_iic_op waiting, op %#x\n", sc->sc_op));
		while (sc->sc_op == op) {
			if (tsleep_nsec(&sc->sc_dev, PWAIT, "tiiic",
			    SEC_TO_NSEC(5)) == EWOULDBLOCK) {
				/* timeout */
				op = TI_I2CERROR;
			}
		}
		DPRINTF(("ti_iic_op waiting done, op %#x\n", sc->sc_op));

		/* disable interrupts */
		I2C_WRITE_REG(sc, AM335X_I2C_IRQENABLE_CLR, 0xffff);
	} else {
		/* poll for completion */
		DPRINTF(("ti_iic_op polling, op %x\n", sc->sc_op));
		while (sc->sc_op == op) {
			stat = ti_iic_stat(sc, mask);
			DPRINTF(("ti_iic_op stat 0x%x\n", stat));
			if (stat == 0) /* timeout */
				sc->sc_op = TI_I2CERROR;
			else
				ti_iic_handle_intr(sc, stat);
			I2C_WRITE_REG(sc, AM335X_I2C_IRQSTATUS, stat);
		}
		DPRINTF(("ti_iic_op polling done, op now %x\n", sc->sc_op));
	}
	retry = 10000;
	I2C_WRITE_REG(sc, AM335X_I2C_CON, 0);
	while (I2C_READ_REG(sc, AM335X_I2C_CON) & I2C_CON_MST) {
		delay(100);
		if (--retry == 0)
			break;
	}

	return (sc->sc_op == TI_I2CDONE) ? 0 : EIO;
}

void
ti_iic_handle_intr(struct ti_iic_softc *sc, uint32_t stat)
{
	KASSERT(stat != 0);
	DPRINTF(("ti_iic_handle_intr stat %#x\n", stat));

	if (stat & (I2C_IRQSTATUS_NACK|I2C_IRQSTATUS_AL)) {
		sc->sc_op = TI_I2CERROR;
		return;
	}
	if (stat & I2C_IRQSTATUS_ARDY) {
		sc->sc_op = TI_I2CDONE;
		return;
	}
	if (sc->sc_op == TI_I2CREAD)
		ti_iic_do_read(sc, stat);
	else if (sc->sc_op == TI_I2CWRITE)
		ti_iic_do_write(sc, stat);
	else
		return;
}
void
ti_iic_do_read(struct ti_iic_softc *sc, uint32_t stat)
{
	int len = 0;

	DPRINTF(("ti_iic_do_read stat %#x\n", stat));
	if (stat & I2C_IRQSTATUS_RDR) {
		len = I2C_READ_REG(sc, AM335X_I2C_BUFSTAT);
		len = I2C_BUFSTAT_RXSTAT(len);
		DPRINTF(("ti_iic_do_read receive drain len %d left %d\n",
		    len, I2C_READ_REG(sc, AM335X_I2C_CNT)));
	} else if (stat & I2C_IRQSTATUS_RRDY) {
		len = sc->sc_rxthres + 1;
		DPRINTF(("ti_iic_do_read receive len %d left %d\n",
		    len, I2C_READ_REG(sc, AM335X_I2C_CNT)));
	}
	for (;
	    sc->sc_bufidx < sc->sc_buflen && len > 0;
	    sc->sc_bufidx++, len--) {
		sc->sc_buf[sc->sc_bufidx] = I2C_READ_DATA(sc);
		DPRINTF(("ti_iic_do_read got b[%d]=0x%x\n", sc->sc_bufidx,
		    sc->sc_buf[sc->sc_bufidx]));
	}
	DPRINTF(("ti_iic_do_read done\n"));
}

void
ti_iic_do_write(struct ti_iic_softc *sc, uint32_t stat)
{
	int len = 0;

	DPRINTF(("ti_iic_do_write stat %#x\n", stat));

	if (stat & I2C_IRQSTATUS_XDR) {
		len = I2C_READ_REG(sc, AM335X_I2C_BUFSTAT);
		len = I2C_BUFSTAT_TXSTAT(len);
		DPRINTF(("ti_iic_do_write xmit drain len %d left %d\n",
		    len, I2C_READ_REG(sc, AM335X_I2C_CNT)));
	} else if (stat & I2C_IRQSTATUS_XRDY) {
		len = sc->sc_txthres + 1;
		DPRINTF(("ti_iic_do_write xmit len %d left %d\n",
		    len, I2C_READ_REG(sc, AM335X_I2C_CNT)));
	}
	for (;
	    sc->sc_bufidx < sc->sc_buflen && len > 0;
	    sc->sc_bufidx++, len--) {
		DPRINTF(("ti_iic_do_write send b[%d]=0x%x\n",
		    sc->sc_bufidx, sc->sc_buf[sc->sc_bufidx]));
		I2C_WRITE_DATA(sc, sc->sc_buf[sc->sc_bufidx]);
	}
	DPRINTF(("ti_iic_do_write done\n"));
}

int
ti_iic_wait(struct ti_iic_softc *sc, uint16_t mask, uint16_t val, int flags)
{
	int retry = 10;
	uint16_t v;
	DPRINTF(("ti_iic_wait mask %#x val %#x flags %#x\n", mask, val, flags));

	while (((v = I2C_READ_REG(sc, AM335X_I2C_IRQSTATUS_RAW)) & mask) != val) {
		--retry;
		if (retry == 0) {
			printf("%s: wait timeout, mask=%#x val=%#x stat=%#x\n",
			    DEVNAME(sc), mask, val, v);
			return EBUSY;
		}
		if (flags & I2C_F_POLL)
			delay(50000);
		else
			tsleep_nsec(&sc->sc_dev, PWAIT, "tiiic",
			    MSEC_TO_NSEC(50));
	}
	DPRINTF(("ti_iic_wait done retry %#x\n", retry));

	return 0;
}

uint32_t
ti_iic_stat(struct ti_iic_softc *sc, uint32_t mask)
{
	uint32_t v;
	int retry = 500;
	DPRINTF(("ti_iic_wait mask %#x\n", mask));
	while (--retry > 0) {
		v = I2C_READ_REG(sc, AM335X_I2C_IRQSTATUS_RAW) & mask;
		if (v != 0)
			break;
		delay(100);
	}
	DPRINTF(("ti_iic_wait done retry %#x\n", retry));
	return v;
}

int
ti_iic_flush(struct ti_iic_softc *sc)
{
	DPRINTF(("ti_iic_flush\n"));
#if 0
	int retry = 1000;
	uint16_t v;

	while ((v =
	    I2C_READ_REG(sc, AM335X_I2C_IRQSTATUS_RAW)) & I2C_IRQSTATUS_RRDY) {
		if (--retry == 0) {
			printf("%s: flush timeout, stat = %#x\n", DEVNAME(sc), v);
			return EBUSY;
		}
		(void)I2C_READ_DATA(sc);
		delay(1000);
	}
#endif

	I2C_WRITE_REG(sc, AM335X_I2C_CNT, 0);
	return 0;
}

void
ti_iic_scan(struct device *self, struct i2cbus_attach_args *iba, void *aux)
{
	int iba_node = *(int *)aux;
	extern int iic_print(void *, const char *);
	struct i2c_attach_args ia;
	char name[32];
	uint32_t reg[1];
	int node;

	for (node = OF_child(iba_node); node; node = OF_peer(node)) {
		memset(name, 0, sizeof(name));
		memset(reg, 0, sizeof(reg));

		if (OF_getprop(node, "compatible", name, sizeof(name)) == -1)
			continue;
		if (name[0] == '\0')
			continue;

		if (OF_getprop(node, "reg", &reg, sizeof(reg)) != sizeof(reg))
			continue;

		memset(&ia, 0, sizeof(ia));
		ia.ia_tag = iba->iba_tag;
		ia.ia_addr = bemtoh32(&reg[0]);
		ia.ia_name = name;
		ia.ia_cookie = &node;
	
		config_found(self, &ia, iic_print);
	}
}
