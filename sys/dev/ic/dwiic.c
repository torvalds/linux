/* $OpenBSD: dwiic.c,v 1.21 2024/08/17 02:35:00 deraadt Exp $ */
/*
 * Synopsys DesignWare I2C controller
 *
 * Copyright (c) 2015-2017 joshua stein <jcs@openbsd.org>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
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
#include <sys/kernel.h>

#include <dev/i2c/i2cvar.h>

#include <dev/ic/dwiicvar.h>

struct cfdriver dwiic_cd = {
	NULL, "dwiic", DV_DULL
};

int
dwiic_activate(struct device *self, int act)
{
	struct dwiic_softc *sc = (struct dwiic_softc *)self;
	int rv;

	switch (act) {
	case DVACT_SUSPEND:
		rv = config_activate_children(self, act);
		/* disable controller */
		dwiic_enable(sc, 0);

		/* disable interrupts */
		dwiic_write(sc, DW_IC_INTR_MASK, 0);
		dwiic_read(sc, DW_IC_CLR_INTR);
		break;
	case DVACT_RESUME:
		/* if it became enabled for some reason, force it down */
		dwiic_enable(sc, 0);

		dwiic_write(sc, DW_IC_INTR_MASK, 0);
		dwiic_read(sc, DW_IC_CLR_INTR);

		/* write standard-mode SCL timing parameters */
		dwiic_write(sc, DW_IC_SS_SCL_HCNT, sc->ss_hcnt);
		dwiic_write(sc, DW_IC_SS_SCL_LCNT, sc->ss_lcnt);

		/* and fast-mode SCL timing parameters */
		dwiic_write(sc, DW_IC_FS_SCL_HCNT, sc->fs_hcnt);
		dwiic_write(sc, DW_IC_FS_SCL_LCNT, sc->fs_lcnt);

		/* SDA hold time */
		dwiic_write(sc, DW_IC_SDA_HOLD, sc->sda_hold_time);

		dwiic_write(sc, DW_IC_TX_TL, sc->tx_fifo_depth / 2);
		dwiic_write(sc, DW_IC_RX_TL, 0);

		/* configure as i2c master with fast speed */
		sc->master_cfg = DW_IC_CON_MASTER | DW_IC_CON_SLAVE_DISABLE |
		    DW_IC_CON_RESTART_EN | DW_IC_CON_SPEED_FAST;
		dwiic_write(sc, DW_IC_CON, sc->master_cfg);
		rv = config_activate_children(self, act);
		break;
	default:
		rv = config_activate_children(self, act);
		break;
	}
	return rv;
}

int
dwiic_i2c_print(void *aux, const char *pnp)
{
	struct i2c_attach_args *ia = aux;

	if (pnp != NULL)
		printf("\"%s\" at %s", ia->ia_name, pnp);

	printf(" addr 0x%x", ia->ia_addr);

	return UNCONF;
}

uint32_t
dwiic_read(struct dwiic_softc *sc, int offset)
{
	u_int32_t b = bus_space_read_4(sc->sc_iot, sc->sc_ioh, offset);

	DPRINTF(("%s: read at 0x%x = 0x%x\n", sc->sc_dev.dv_xname, offset, b));

	return b;
}

void
dwiic_write(struct dwiic_softc *sc, int offset, uint32_t val)
{
	DPRINTF(("%s: write at 0x%x: 0x%x\n", sc->sc_dev.dv_xname, offset,
	    val));

	bus_space_write_4(sc->sc_iot, sc->sc_ioh, offset, val);
}

int
dwiic_i2c_acquire_bus(void *cookie, int flags)
{
	struct dwiic_softc *sc = cookie;

	if (cold || sc->sc_poll || (flags & I2C_F_POLL))
		return (0);

	return rw_enter(&sc->sc_i2c_lock, RW_WRITE | RW_INTR);
}

void
dwiic_i2c_release_bus(void *cookie, int flags)
{
	struct dwiic_softc *sc = cookie;

	if (cold || sc->sc_poll || (flags & I2C_F_POLL))
		return;

	rw_exit(&sc->sc_i2c_lock);
}

int
dwiic_init(struct dwiic_softc *sc)
{
	uint32_t reg;
	uint8_t tx_fifo_depth;
	uint8_t rx_fifo_depth;

	/* make sure we're talking to a device we know */
	reg = dwiic_read(sc, DW_IC_COMP_TYPE);
	if (reg != DW_IC_COMP_TYPE_VALUE) {
		DPRINTF(("%s: invalid component type 0x%x\n",
		    sc->sc_dev.dv_xname, reg));
		return 1;
	}

	/* fetch default timing parameters if not already specified */
	if (!sc->ss_hcnt)
		sc->ss_hcnt = dwiic_read(sc, DW_IC_SS_SCL_HCNT);
	if (!sc->ss_lcnt)
		sc->ss_lcnt = dwiic_read(sc, DW_IC_SS_SCL_LCNT);
	if (!sc->fs_hcnt)
		sc->fs_hcnt = dwiic_read(sc, DW_IC_FS_SCL_HCNT);
	if (!sc->fs_lcnt)
		sc->fs_lcnt = dwiic_read(sc, DW_IC_FS_SCL_LCNT);
	if (!sc->sda_hold_time)
		sc->sda_hold_time = dwiic_read(sc, DW_IC_SDA_HOLD);

	/* disable the adapter */
	dwiic_enable(sc, 0);

	/* write standard-mode SCL timing parameters */
	dwiic_write(sc, DW_IC_SS_SCL_HCNT, sc->ss_hcnt);
	dwiic_write(sc, DW_IC_SS_SCL_LCNT, sc->ss_lcnt);

	/* and fast-mode SCL timing parameters */
	dwiic_write(sc, DW_IC_FS_SCL_HCNT, sc->fs_hcnt);
	dwiic_write(sc, DW_IC_FS_SCL_LCNT, sc->fs_lcnt);

	/* SDA hold time */
	reg = dwiic_read(sc, DW_IC_COMP_VERSION);
	if (reg >= DW_IC_SDA_HOLD_MIN_VERS)
		dwiic_write(sc, DW_IC_SDA_HOLD, sc->sda_hold_time);

	/* FIFO threshold levels */
	sc->tx_fifo_depth = 32;
	sc->rx_fifo_depth = 32;
	reg = dwiic_read(sc, DW_IC_COMP_PARAM_1);
	tx_fifo_depth = DW_IC_TX_FIFO_DEPTH(reg);
	rx_fifo_depth = DW_IC_RX_FIFO_DEPTH(reg);
	if (tx_fifo_depth > 1 && tx_fifo_depth < sc->tx_fifo_depth)
		sc->tx_fifo_depth = tx_fifo_depth;
	if (rx_fifo_depth > 1 && rx_fifo_depth < sc->rx_fifo_depth)
		sc->rx_fifo_depth = rx_fifo_depth;
		
	dwiic_write(sc, DW_IC_TX_TL, sc->tx_fifo_depth / 2);
	dwiic_write(sc, DW_IC_RX_TL, 0);

	/* configure as i2c master with fast speed */
	sc->master_cfg = DW_IC_CON_MASTER | DW_IC_CON_SLAVE_DISABLE |
	    DW_IC_CON_RESTART_EN | DW_IC_CON_SPEED_FAST;
	dwiic_write(sc, DW_IC_CON, sc->master_cfg);

	return 0;
}

void
dwiic_enable(struct dwiic_softc *sc, int enable)
{
	int retries;

	for (retries = 100; retries > 0; retries--) {
		dwiic_write(sc, DW_IC_ENABLE, enable);
		if ((dwiic_read(sc, DW_IC_ENABLE_STATUS) & 1) == enable)
			return;

		DELAY(25);
	}

	printf("%s: failed to %sable\n", sc->sc_dev.dv_xname,
	    (enable ? "en" : "dis"));
}

int
dwiic_i2c_exec(void *cookie, i2c_op_t op, i2c_addr_t addr, const void *cmdbuf,
    size_t cmdlen, void *buf, size_t len, int flags)
{
	struct dwiic_softc *sc = cookie;
	u_int32_t ic_con, st, cmd, resp;
	int retries, tx_limit, rx_avail, x, readpos;
	uint8_t *b;
	int s;

	if (sc->sc_busy)
		return 1;

	sc->sc_busy++;

	DPRINTF(("%s: %s: op %d, addr 0x%02x, cmdlen %zu, len %zu, "
	    "flags 0x%02x\n", sc->sc_dev.dv_xname, __func__, op, addr, cmdlen,
	    len, flags));

	/* setup transfer */
	sc->sc_i2c_xfer.op = op;
	sc->sc_i2c_xfer.buf = buf;
	sc->sc_i2c_xfer.len = len;
	sc->sc_i2c_xfer.flags = flags;
	sc->sc_i2c_xfer.error = 0;

	/* wait for bus to be idle */
	for (retries = 100; retries > 0; retries--) {
		st = dwiic_read(sc, DW_IC_STATUS);
		if (!(st & DW_IC_STATUS_ACTIVITY))
			break;
		DELAY(1000);
	}
	DPRINTF(("%s: %s: status 0x%x\n", sc->sc_dev.dv_xname, __func__, st));
	if (st & DW_IC_STATUS_ACTIVITY) {
		sc->sc_busy = 0;
		return (1);
	}

	if (cold || sc->sc_poll)
		flags |= I2C_F_POLL;

	/* disable controller */
	dwiic_enable(sc, 0);

	/* set slave address */
	ic_con = dwiic_read(sc, DW_IC_CON);
	ic_con &= ~DW_IC_CON_10BITADDR_MASTER;
	dwiic_write(sc, DW_IC_CON, ic_con);
	dwiic_write(sc, DW_IC_TAR, addr);

	/* disable interrupts */
	dwiic_write(sc, DW_IC_INTR_MASK, 0);
	dwiic_read(sc, DW_IC_CLR_INTR);

	/* enable controller */
	dwiic_enable(sc, 1);

	/* wait until the controller is ready for commands */
	if (flags & I2C_F_POLL)
		DELAY(200);
	else {
		s = splbio();
		dwiic_read(sc, DW_IC_CLR_INTR);
		dwiic_write(sc, DW_IC_INTR_MASK, DW_IC_INTR_TX_EMPTY);

		if (tsleep_nsec(&sc->sc_writewait, PRIBIO, "dwiic",
		    MSEC_TO_NSEC(500)) != 0)
			printf("%s: timed out waiting for tx_empty intr\n",
			    sc->sc_dev.dv_xname);
		splx(s);
	}

	/* send our command, one byte at a time */
	if (cmdlen > 0) {
		b = (void *)cmdbuf;

		DPRINTF(("%s: %s: sending cmd (len %zu):", sc->sc_dev.dv_xname,
		    __func__, cmdlen));
		for (x = 0; x < cmdlen; x++)
			DPRINTF((" %02x", b[x]));
		DPRINTF(("\n"));

		tx_limit = sc->tx_fifo_depth - dwiic_read(sc, DW_IC_TXFLR);
		if (cmdlen > tx_limit) {
			/* TODO */
			printf("%s: can't write %zu (> %d)\n",
			    sc->sc_dev.dv_xname, cmdlen, tx_limit);
			sc->sc_i2c_xfer.error = 1;
			sc->sc_busy = 0;
			return (1);
		}

		for (x = 0; x < cmdlen; x++) {
			cmd = b[x];
			/*
			 * Generate STOP condition if this is the last
			 * byte of the transfer.
			 */
			if (x == (cmdlen - 1) && len == 0 && I2C_OP_STOP_P(op))
				cmd |= DW_IC_DATA_CMD_STOP;
			dwiic_write(sc, DW_IC_DATA_CMD, cmd);
		}
	}

	b = (void *)buf;
	x = readpos = 0;
	tx_limit = sc->tx_fifo_depth - dwiic_read(sc, DW_IC_TXFLR);

	DPRINTF(("%s: %s: need to read %zu bytes, can send %d read reqs\n",
		sc->sc_dev.dv_xname, __func__, len, tx_limit));

	while (x < len) {
		if (I2C_OP_WRITE_P(op))
			cmd = b[x];
		else
			cmd = DW_IC_DATA_CMD_READ;

		/*
		 * Generate RESTART condition if we're reversing
		 * direction.
		 */
		if (x == 0 && cmdlen > 0 && I2C_OP_READ_P(op))
			cmd |= DW_IC_DATA_CMD_RESTART;
		/*
		 * Generate STOP condition on the last byte of the
		 * transfer.
		 */
		if (x == (len - 1) && I2C_OP_STOP_P(op))
			cmd |= DW_IC_DATA_CMD_STOP;

		dwiic_write(sc, DW_IC_DATA_CMD, cmd);

		/*
		 * For a block read, get the byte count before
		 * continuing to read the data bytes.
		 */
		if (I2C_OP_READ_P(op) && I2C_OP_BLKMODE_P(op) && readpos == 0)
			tx_limit = 1;

		tx_limit--;
		x++;

		/*
		 * As TXFLR fills up, we need to clear it out by reading all
		 * available data.
		 */
		while (I2C_OP_READ_P(op) && (tx_limit == 0 || x == len)) {
			DPRINTF(("%s: %s: tx_limit %d, sent %d read reqs\n",
			    sc->sc_dev.dv_xname, __func__, tx_limit, x));

			if (flags & I2C_F_POLL) {
				for (retries = 1000; retries > 0; retries--) {
					rx_avail = dwiic_read(sc, DW_IC_RXFLR);
					if (rx_avail > 0)
						break;
					DELAY(50);
				}
			} else {
				s = splbio();
				dwiic_read(sc, DW_IC_CLR_INTR);
				dwiic_write(sc, DW_IC_INTR_MASK,
				    DW_IC_INTR_RX_FULL);

				if (tsleep_nsec(&sc->sc_readwait, PRIBIO,
				    "dwiic", MSEC_TO_NSEC(500)) != 0)
					printf("%s: timed out waiting for "
					    "rx_full intr\n",
					    sc->sc_dev.dv_xname);
				splx(s);

				rx_avail = dwiic_read(sc, DW_IC_RXFLR);
			}

			if (rx_avail == 0) {
				printf("%s: timed out reading remaining %d\n",
				    sc->sc_dev.dv_xname, (int)(len - readpos));
				sc->sc_i2c_xfer.error = 1;
				sc->sc_busy = 0;

				return (1);
			}

			DPRINTF(("%s: %s: %d avail to read (%zu remaining)\n",
			    sc->sc_dev.dv_xname, __func__, rx_avail,
			    len - readpos));

			while (rx_avail > 0) {
				resp = dwiic_read(sc, DW_IC_DATA_CMD);
				if (readpos < len) {
					b[readpos] = resp;
					readpos++;
				}
				rx_avail--;
			}

			/*
			 * Update the transfer length when doing a
			 * block read.
			 */
			if (I2C_OP_BLKMODE_P(op) && readpos > 0 && len > b[0])
				len = b[0] + 1;

			if (readpos >= len)
				break;

			DPRINTF(("%s: still need to read %d bytes\n",
			    sc->sc_dev.dv_xname, (int)(len - readpos)));
			tx_limit = sc->tx_fifo_depth -
			    dwiic_read(sc, DW_IC_TXFLR);
		}

		if (I2C_OP_WRITE_P(op) && tx_limit == 0 && x < len) {
			if (flags & I2C_F_POLL) {
				for (retries = 1000; retries > 0; retries--) {
					tx_limit = sc->tx_fifo_depth -
					    dwiic_read(sc, DW_IC_TXFLR);
					if (tx_limit > 0)
						break;
					DELAY(50);
				}
			} else {
				s = splbio();
				dwiic_read(sc, DW_IC_CLR_INTR);
				dwiic_write(sc, DW_IC_INTR_MASK,
				    DW_IC_INTR_TX_EMPTY);

				if (tsleep_nsec(&sc->sc_writewait, PRIBIO,
				    "dwiic", MSEC_TO_NSEC(500)) != 0)
					printf("%s: timed out waiting for "
					    "tx_empty intr\n",
					    sc->sc_dev.dv_xname);
				splx(s);

				tx_limit = sc->tx_fifo_depth -
				    dwiic_read(sc, DW_IC_TXFLR);
			}

			if (tx_limit == 0) {
				printf("%s: timed out writing remaining %d\n",
				    sc->sc_dev.dv_xname, (int)(len - x));
				sc->sc_i2c_xfer.error = 1;
				sc->sc_busy = 0;

				return (1);
			}
		}
	}

	if (I2C_OP_STOP_P(op) && I2C_OP_WRITE_P(op)) {
		if (flags & I2C_F_POLL) {
			for (retries = 100; retries > 0; retries--) {
				st = dwiic_read(sc, DW_IC_RAW_INTR_STAT);
				if (st & DW_IC_INTR_STOP_DET)
					break;
				DELAY(1000);
			}
			if (!(st & DW_IC_INTR_STOP_DET))
				printf("%s: timed out waiting for bus idle\n",
				    sc->sc_dev.dv_xname);
		} else {
			s = splbio();
			while (sc->sc_busy) {
				dwiic_write(sc, DW_IC_INTR_MASK,
				    DW_IC_INTR_STOP_DET);
				if (tsleep_nsec(&sc->sc_busy, PRIBIO, "dwiic",
				    MSEC_TO_NSEC(500)) != 0)
					printf("%s: timed out waiting for "
					    "stop intr\n",
					    sc->sc_dev.dv_xname);
			}
			splx(s);
		}
	}
	sc->sc_busy = 0;

	return 0;
}

uint32_t
dwiic_read_clear_intrbits(struct dwiic_softc *sc)
{
	uint32_t stat;

	stat = dwiic_read(sc, DW_IC_INTR_STAT);

	if (stat & DW_IC_INTR_RX_UNDER)
		dwiic_read(sc, DW_IC_CLR_RX_UNDER);
	if (stat & DW_IC_INTR_RX_OVER)
		dwiic_read(sc, DW_IC_CLR_RX_OVER);
	if (stat & DW_IC_INTR_TX_OVER)
		dwiic_read(sc, DW_IC_CLR_TX_OVER);
	if (stat & DW_IC_INTR_RD_REQ)
		dwiic_read(sc, DW_IC_CLR_RD_REQ);
	if (stat & DW_IC_INTR_TX_ABRT)
		dwiic_read(sc, DW_IC_CLR_TX_ABRT);
	if (stat & DW_IC_INTR_RX_DONE)
		dwiic_read(sc, DW_IC_CLR_RX_DONE);
	if (stat & DW_IC_INTR_ACTIVITY)
		dwiic_read(sc, DW_IC_CLR_ACTIVITY);
	if (stat & DW_IC_INTR_STOP_DET)
		dwiic_read(sc, DW_IC_CLR_STOP_DET);
	if (stat & DW_IC_INTR_START_DET)
		dwiic_read(sc, DW_IC_CLR_START_DET);
	if (stat & DW_IC_INTR_GEN_CALL)
		dwiic_read(sc, DW_IC_CLR_GEN_CALL);

	return stat;
}

int
dwiic_intr(void *arg)
{
	struct dwiic_softc *sc = arg;
	uint32_t en, stat;

	en = dwiic_read(sc, DW_IC_ENABLE);
	/* probably for the other controller */
	if (!en)
		return 0;

	stat = dwiic_read_clear_intrbits(sc);
	DPRINTF(("%s: %s: enabled=0x%x stat=0x%x\n", sc->sc_dev.dv_xname,
	    __func__, en, stat));
	if (!(stat & ~DW_IC_INTR_ACTIVITY))
		return 0;

	if (stat & DW_IC_INTR_TX_ABRT)
		sc->sc_i2c_xfer.error = 1;

	if (sc->sc_i2c_xfer.flags & I2C_F_POLL)
		DPRINTF(("%s: %s: intr in poll mode?\n", sc->sc_dev.dv_xname,
		    __func__));
	else {
		if (stat & DW_IC_INTR_RX_FULL) {
			dwiic_write(sc, DW_IC_INTR_MASK, 0);
			DPRINTF(("%s: %s: waking up reader\n",
			    sc->sc_dev.dv_xname, __func__));
			wakeup(&sc->sc_readwait);
		}
		if (stat & DW_IC_INTR_TX_EMPTY) {
			dwiic_write(sc, DW_IC_INTR_MASK, 0);
			DPRINTF(("%s: %s: waking up writer\n",
			    sc->sc_dev.dv_xname, __func__));
			wakeup(&sc->sc_writewait);
		}
		if (stat & DW_IC_INTR_STOP_DET) {
			dwiic_write(sc, DW_IC_INTR_MASK, 0);
			sc->sc_busy = 0;
			wakeup(&sc->sc_busy);
		}
	}

	return 1;
}
