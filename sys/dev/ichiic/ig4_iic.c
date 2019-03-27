/*
 * Copyright (c) 2014 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com> and was subsequently ported
 * to FreeBSD by Michael Gmelin <freebsd@grem.de>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Intel fourth generation mobile cpus integrated I2C device.
 *
 * See ig4_reg.h for datasheet reference and notes.
 * See ig4_var.h for locking semantics.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/errno.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sx.h>
#include <sys/syslog.h>
#include <sys/bus.h>
#include <sys/sysctl.h>

#include <machine/bus.h>
#include <sys/rman.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include <dev/iicbus/iicbus.h>
#include <dev/iicbus/iiconf.h>

#include <dev/ichiic/ig4_reg.h>
#include <dev/ichiic/ig4_var.h>

#define TRANS_NORMAL	1
#define TRANS_PCALL	2
#define TRANS_BLOCK	3

static void ig4iic_start(void *xdev);
static void ig4iic_intr(void *cookie);
static void ig4iic_dump(ig4iic_softc_t *sc);

static int ig4_dump;
SYSCTL_INT(_debug, OID_AUTO, ig4_dump, CTLFLAG_RW,
	   &ig4_dump, 0, "Dump controller registers");

/*
 * Low-level inline support functions
 */
static __inline void
reg_write(ig4iic_softc_t *sc, uint32_t reg, uint32_t value)
{
	bus_write_4(sc->regs_res, reg, value);
	bus_barrier(sc->regs_res, reg, 4, BUS_SPACE_BARRIER_WRITE);
}

static __inline uint32_t
reg_read(ig4iic_softc_t *sc, uint32_t reg)
{
	uint32_t value;

	bus_barrier(sc->regs_res, reg, 4, BUS_SPACE_BARRIER_READ);
	value = bus_read_4(sc->regs_res, reg);
	return (value);
}

/*
 * Enable or disable the controller and wait for the controller to acknowledge
 * the state change.
 */
static int
set_controller(ig4iic_softc_t *sc, uint32_t ctl)
{
	int retry;
	int error;
	uint32_t v;

	/*
	 * When the controller is enabled, interrupt on STOP detect
	 * or receive character ready and clear pending interrupts.
	 */
	if (ctl & IG4_I2C_ENABLE) {
		reg_write(sc, IG4_REG_INTR_MASK, IG4_INTR_STOP_DET |
						 IG4_INTR_RX_FULL);
		reg_read(sc, IG4_REG_CLR_INTR);
	} else
		reg_write(sc, IG4_REG_INTR_MASK, 0);

	reg_write(sc, IG4_REG_I2C_EN, ctl);
	error = IIC_ETIMEOUT;

	for (retry = 100; retry > 0; --retry) {
		v = reg_read(sc, IG4_REG_ENABLE_STATUS);
		if (((v ^ ctl) & IG4_I2C_ENABLE) == 0) {
			error = 0;
			break;
		}
		if (cold)
			DELAY(1000);
		else
			mtx_sleep(sc, &sc->io_lock, 0, "i2cslv", 1);
	}
	return (error);
}

/*
 * Wait up to 25ms for the requested status using a 25uS polling loop.
 */
static int
wait_status(ig4iic_softc_t *sc, uint32_t status)
{
	uint32_t v;
	int error;
	int txlvl = -1;
	u_int count_us = 0;
	u_int limit_us = 25000; /* 25ms */

	error = IIC_ETIMEOUT;

	for (;;) {
		/*
		 * Check requested status
		 */
		v = reg_read(sc, IG4_REG_I2C_STA);
		if (v & status) {
			error = 0;
			break;
		}

		/*
		 * When waiting for receive data break-out if the interrupt
		 * loaded data into the FIFO.
		 */
		if (status & IG4_STATUS_RX_NOTEMPTY) {
			if (sc->rpos != sc->rnext) {
				error = 0;
				break;
			}
		}

		/*
		 * When waiting for the transmit FIFO to become empty,
		 * reset the timeout if we see a change in the transmit
		 * FIFO level as progress is being made.
		 */
		if (status & IG4_STATUS_TX_EMPTY) {
			v = reg_read(sc, IG4_REG_TXFLR) & IG4_FIFOLVL_MASK;
			if (txlvl != v) {
				txlvl = v;
				count_us = 0;
			}
		}

		/*
		 * Stop if we've run out of time.
		 */
		if (count_us >= limit_us)
			break;

		/*
		 * When waiting for receive data let the interrupt do its
		 * work, otherwise poll with the lock held.
		 */
		if (status & IG4_STATUS_RX_NOTEMPTY) {
			mtx_sleep(sc, &sc->io_lock, 0, "i2cwait",
				  (hz + 99) / 100); /* sleep up to 10ms */
			count_us += 10000;
		} else {
			DELAY(25);
			count_us += 25;
		}
	}

	return (error);
}

/*
 * Read I2C data.  The data might have already been read by
 * the interrupt code, otherwise it is sitting in the data
 * register.
 */
static uint8_t
data_read(ig4iic_softc_t *sc)
{
	uint8_t c;

	if (sc->rpos == sc->rnext) {
		c = (uint8_t)reg_read(sc, IG4_REG_DATA_CMD);
	} else {
		c = sc->rbuf[sc->rpos & IG4_RBUFMASK];
		++sc->rpos;
	}
	return (c);
}

/*
 * Set the slave address.  The controller must be disabled when
 * changing the address.
 *
 * This operation does not issue anything to the I2C bus but sets
 * the target address for when the controller later issues a START.
 */
static void
set_slave_addr(ig4iic_softc_t *sc, uint8_t slave)
{
	uint32_t tar;
	uint32_t ctl;
	int use_10bit;

	use_10bit = 0;
	if (sc->slave_valid && sc->last_slave == slave &&
	    sc->use_10bit == use_10bit) {
		return;
	}
	sc->use_10bit = use_10bit;

	/*
	 * Wait for TXFIFO to drain before disabling the controller.
	 *
	 * If a write message has not been completed it's really a
	 * programming error, but for now in that case issue an extra
	 * byte + STOP.
	 *
	 * If a read message has not been completed it's also a programming
	 * error, for now just ignore it.
	 */
	wait_status(sc, IG4_STATUS_TX_NOTFULL);
	if (sc->write_started) {
		reg_write(sc, IG4_REG_DATA_CMD, IG4_DATA_STOP);
		sc->write_started = 0;
	}
	if (sc->read_started)
		sc->read_started = 0;
	wait_status(sc, IG4_STATUS_TX_EMPTY);

	set_controller(sc, 0);
	ctl = reg_read(sc, IG4_REG_CTL);
	ctl &= ~IG4_CTL_10BIT;
	ctl |= IG4_CTL_RESTARTEN;

	tar = slave;
	if (sc->use_10bit) {
		tar |= IG4_TAR_10BIT;
		ctl |= IG4_CTL_10BIT;
	}
	reg_write(sc, IG4_REG_CTL, ctl);
	reg_write(sc, IG4_REG_TAR_ADD, tar);
	set_controller(sc, IG4_I2C_ENABLE);
	sc->slave_valid = 1;
	sc->last_slave = slave;
}

/*
 *				IICBUS API FUNCTIONS
 */
static int
ig4iic_xfer_start(ig4iic_softc_t *sc, uint16_t slave)
{
	set_slave_addr(sc, slave >> 1);
	return (0);
}

static int
ig4iic_read(ig4iic_softc_t *sc, uint8_t *buf, uint16_t len,
    bool repeated_start, bool stop)
{
	uint32_t cmd;
	uint16_t i;
	int error;

	if (len == 0)
		return (0);

	cmd = IG4_DATA_COMMAND_RD;
	cmd |= repeated_start ? IG4_DATA_RESTART : 0;
	cmd |= stop && len == 1 ? IG4_DATA_STOP : 0;

	/* Issue request for the first byte (could be last as well). */
	reg_write(sc, IG4_REG_DATA_CMD, cmd);

	for (i = 0; i < len; i++) {
		/*
		 * Maintain a pipeline by queueing the allowance for the next
		 * read before waiting for the current read.
		 */
		cmd = IG4_DATA_COMMAND_RD;
		if (i < len - 1) {
			cmd = IG4_DATA_COMMAND_RD;
			cmd |= stop && i == len - 2 ? IG4_DATA_STOP : 0;
			reg_write(sc, IG4_REG_DATA_CMD, cmd);
		}
		error = wait_status(sc, IG4_STATUS_RX_NOTEMPTY);
		if (error)
			break;
		buf[i] = data_read(sc);
	}

	(void)reg_read(sc, IG4_REG_TX_ABRT_SOURCE);
	return (error);
}

static int
ig4iic_write(ig4iic_softc_t *sc, uint8_t *buf, uint16_t len,
    bool repeated_start, bool stop)
{
	uint32_t cmd;
	uint16_t i;
	int error;

	if (len == 0)
		return (0);

	cmd = repeated_start ? IG4_DATA_RESTART : 0;
	for (i = 0; i < len; i++) {
		error = wait_status(sc, IG4_STATUS_TX_NOTFULL);
		if (error)
			break;
		cmd |= buf[i];
		cmd |= stop && i == len - 1 ? IG4_DATA_STOP : 0;
		reg_write(sc, IG4_REG_DATA_CMD, cmd);
		cmd = 0;
	}

	(void)reg_read(sc, IG4_REG_TX_ABRT_SOURCE);
	return (error);
}

int
ig4iic_transfer(device_t dev, struct iic_msg *msgs, uint32_t nmsgs)
{
	ig4iic_softc_t *sc = device_get_softc(dev);
	const char *reason = NULL;
	uint32_t i;
	int error;
	int unit;
	bool rpstart;
	bool stop;

	/*
	 * The hardware interface imposes limits on allowed I2C messages.
	 * It is not possible to explicitly send a start or stop.
	 * They are automatically sent (or not sent, depending on the
	 * configuration) when a data byte is transferred.
	 * For this reason it's impossible to send a message with no data
	 * at all (like an SMBus quick message).
	 * The start condition is automatically generated after the stop
	 * condition, so it's impossible to not have a start after a stop.
	 * The repeated start condition is automatically sent if a change
	 * of the transfer direction happens, so it's impossible to have
	 * a change of direction without a (repeated) start.
	 * The repeated start can be forced even without the change of
	 * direction.
	 * Changing the target slave address requires resetting the hardware
	 * state, so it's impossible to do that without the stop followed
	 * by the start.
	 */
	for (i = 0; i < nmsgs; i++) {
#if 0
		if (i == 0 && (msgs[i].flags & IIC_M_NOSTART) != 0) {
			reason = "first message without start";
			break;
		}
		if (i == nmsgs - 1 && (msgs[i].flags & IIC_M_NOSTOP) != 0) {
			reason = "last message without stop";
			break;
		}
#endif
		if (msgs[i].len == 0) {
			reason = "message with no data";
			break;
		}
		if (i > 0) {
			if ((msgs[i].flags & IIC_M_NOSTART) != 0 &&
			    (msgs[i - 1].flags & IIC_M_NOSTOP) == 0) {
				reason = "stop not followed by start";
				break;
			}
			if ((msgs[i - 1].flags & IIC_M_NOSTOP) != 0 &&
			    msgs[i].slave != msgs[i - 1].slave) {
				reason = "change of slave without stop";
				break;
			}
			if ((msgs[i].flags & IIC_M_NOSTART) != 0 &&
			    (msgs[i].flags & IIC_M_RD) !=
			    (msgs[i - 1].flags & IIC_M_RD)) {
				reason = "change of direction without repeated"
				    " start";
				break;
			}
		}
	}
	if (reason != NULL) {
		if (bootverbose)
			device_printf(dev, "%s\n", reason);
		return (IIC_ENOTSUPP);
	}

	sx_xlock(&sc->call_lock);
	mtx_lock(&sc->io_lock);

	/* Debugging - dump registers. */
	if (ig4_dump) {
		unit = device_get_unit(dev);
		if (ig4_dump & (1 << unit)) {
			ig4_dump &= ~(1 << unit);
			ig4iic_dump(sc);
		}
	}

	/*
	 * Clear any previous abort condition that may have been holding
	 * the txfifo in reset.
	 */
	reg_read(sc, IG4_REG_CLR_TX_ABORT);

	/*
	 * Clean out any previously received data.
	 */
	if (sc->rpos != sc->rnext && bootverbose) {
		device_printf(sc->dev, "discarding %d bytes of spurious data\n",
		    sc->rnext - sc->rpos);
	}
	sc->rpos = 0;
	sc->rnext = 0;

	rpstart = false;
	error = 0;
	for (i = 0; i < nmsgs; i++) {
		if ((msgs[i].flags & IIC_M_NOSTART) == 0) {
			error = ig4iic_xfer_start(sc, msgs[i].slave);
		} else {
			if (!sc->slave_valid ||
			    (msgs[i].slave >> 1) != sc->last_slave) {
				device_printf(dev, "start condition suppressed"
				    "but slave address is not set up");
				error = EINVAL;
				break;
			}
			rpstart = false;
		}
		if (error != 0)
			break;

		stop = (msgs[i].flags & IIC_M_NOSTOP) == 0;
		if (msgs[i].flags & IIC_M_RD)
			error = ig4iic_read(sc, msgs[i].buf, msgs[i].len,
			    rpstart, stop);
		else
			error = ig4iic_write(sc, msgs[i].buf, msgs[i].len,
			    rpstart, stop);
		if (error != 0)
			break;

		rpstart = !stop;
	}

	mtx_unlock(&sc->io_lock);
	sx_unlock(&sc->call_lock);
	return (error);
}

int
ig4iic_reset(device_t dev, u_char speed, u_char addr, u_char *oldaddr)
{
	ig4iic_softc_t *sc = device_get_softc(dev);

	sx_xlock(&sc->call_lock);
	mtx_lock(&sc->io_lock);

	/* TODO handle speed configuration? */
	if (oldaddr != NULL)
		*oldaddr = sc->last_slave << 1;
	set_slave_addr(sc, addr >> 1);
	if (addr == IIC_UNKNOWN)
		sc->slave_valid = false;

	mtx_unlock(&sc->io_lock);
	sx_unlock(&sc->call_lock);
	return (0);
}

/*
 * Called from ig4iic_pci_attach/detach()
 */
int
ig4iic_attach(ig4iic_softc_t *sc)
{
	int error;
	uint32_t v;

	mtx_init(&sc->io_lock, "IG4 I/O lock", NULL, MTX_DEF);
	sx_init(&sc->call_lock, "IG4 call lock");

	v = reg_read(sc, IG4_REG_DEVIDLE_CTRL);
	if (sc->version == IG4_SKYLAKE && (v & IG4_RESTORE_REQUIRED) ) {
		reg_write(sc, IG4_REG_DEVIDLE_CTRL, IG4_DEVICE_IDLE | IG4_RESTORE_REQUIRED);
		reg_write(sc, IG4_REG_DEVIDLE_CTRL, 0);

		reg_write(sc, IG4_REG_RESETS_SKL, IG4_RESETS_ASSERT_SKL);
		reg_write(sc, IG4_REG_RESETS_SKL, IG4_RESETS_DEASSERT_SKL);
		DELAY(1000);
	}

	if (sc->version == IG4_ATOM)
		v = reg_read(sc, IG4_REG_COMP_TYPE);
	
	if (sc->version == IG4_HASWELL || sc->version == IG4_ATOM) {
		v = reg_read(sc, IG4_REG_COMP_PARAM1);
		v = reg_read(sc, IG4_REG_GENERAL);
		/*
		 * The content of IG4_REG_GENERAL is different for each
		 * controller version.
		 */
		if (sc->version == IG4_HASWELL &&
		    (v & IG4_GENERAL_SWMODE) == 0) {
			v |= IG4_GENERAL_SWMODE;
			reg_write(sc, IG4_REG_GENERAL, v);
			v = reg_read(sc, IG4_REG_GENERAL);
		}
	}

	if (sc->version == IG4_HASWELL) {
		v = reg_read(sc, IG4_REG_SW_LTR_VALUE);
		v = reg_read(sc, IG4_REG_AUTO_LTR_VALUE);
	} else if (sc->version == IG4_SKYLAKE) {
		v = reg_read(sc, IG4_REG_ACTIVE_LTR_VALUE);
		v = reg_read(sc, IG4_REG_IDLE_LTR_VALUE);
	}

	if (sc->version == IG4_HASWELL || sc->version == IG4_ATOM) {
		v = reg_read(sc, IG4_REG_COMP_VER);
		if (v < IG4_COMP_MIN_VER) {
			error = ENXIO;
			goto done;
		}
	}
	v = reg_read(sc, IG4_REG_SS_SCL_HCNT);
	v = reg_read(sc, IG4_REG_SS_SCL_LCNT);
	v = reg_read(sc, IG4_REG_FS_SCL_HCNT);
	v = reg_read(sc, IG4_REG_FS_SCL_LCNT);
	v = reg_read(sc, IG4_REG_SDA_HOLD);

	v = reg_read(sc, IG4_REG_SS_SCL_HCNT);
	reg_write(sc, IG4_REG_FS_SCL_HCNT, v);
	v = reg_read(sc, IG4_REG_SS_SCL_LCNT);
	reg_write(sc, IG4_REG_FS_SCL_LCNT, v);

	/*
	 * Program based on a 25000 Hz clock.  This is a bit of a
	 * hack (obviously).  The defaults are 400 and 470 for standard
	 * and 60 and 130 for fast.  The defaults for standard fail
	 * utterly (presumably cause an abort) because the clock time
	 * is ~18.8ms by default.  This brings it down to ~4ms (for now).
	 */
	reg_write(sc, IG4_REG_SS_SCL_HCNT, 100);
	reg_write(sc, IG4_REG_SS_SCL_LCNT, 125);
	reg_write(sc, IG4_REG_FS_SCL_HCNT, 100);
	reg_write(sc, IG4_REG_FS_SCL_LCNT, 125);

	/*
	 * Use a threshold of 1 so we get interrupted on each character,
	 * allowing us to use mtx_sleep() in our poll code.  Not perfect
	 * but this is better than using DELAY() for receiving data.
	 *
	 * See ig4_var.h for details on interrupt handler synchronization.
	 */
	reg_write(sc, IG4_REG_RX_TL, 1);

	reg_write(sc, IG4_REG_CTL,
		  IG4_CTL_MASTER |
		  IG4_CTL_SLAVE_DISABLE |
		  IG4_CTL_RESTARTEN |
		  IG4_CTL_SPEED_STD);

	sc->iicbus = device_add_child(sc->dev, "iicbus", -1);
	if (sc->iicbus == NULL) {
		device_printf(sc->dev, "iicbus driver not found\n");
		error = ENXIO;
		goto done;
	}

#if 0
	/*
	 * Don't do this, it blows up the PCI config
	 */
	if (sc->version == IG4_HASWELL || sc->version == IG4_ATOM) {
		reg_write(sc, IG4_REG_RESETS_HSW, IG4_RESETS_ASSERT_HSW);
		reg_write(sc, IG4_REG_RESETS_HSW, IG4_RESETS_DEASSERT_HSW);
	} else if (sc->version = IG4_SKYLAKE) {
		reg_write(sc, IG4_REG_RESETS_SKL, IG4_RESETS_ASSERT_SKL);
		reg_write(sc, IG4_REG_RESETS_SKL, IG4_RESETS_DEASSERT_SKL);
	}
#endif

	mtx_lock(&sc->io_lock);
	if (set_controller(sc, 0))
		device_printf(sc->dev, "controller error during attach-1\n");
	if (set_controller(sc, IG4_I2C_ENABLE))
		device_printf(sc->dev, "controller error during attach-2\n");
	mtx_unlock(&sc->io_lock);
	error = bus_setup_intr(sc->dev, sc->intr_res, INTR_TYPE_MISC | INTR_MPSAFE,
			       NULL, ig4iic_intr, sc, &sc->intr_handle);
	if (error) {
		device_printf(sc->dev,
			      "Unable to setup irq: error %d\n", error);
	}

	sc->enum_hook.ich_func = ig4iic_start;
	sc->enum_hook.ich_arg = sc->dev;

	/*
	 * We have to wait until interrupts are enabled. I2C read and write
	 * only works if the interrupts are available.
	 */
	if (config_intrhook_establish(&sc->enum_hook) != 0)
		error = ENOMEM;
	else
		error = 0;

done:
	return (error);
}

void
ig4iic_start(void *xdev)
{
	int error;
	ig4iic_softc_t *sc;
	device_t dev = (device_t)xdev;

	sc = device_get_softc(dev);

	config_intrhook_disestablish(&sc->enum_hook);

	error = bus_generic_attach(sc->dev);
	if (error) {
		device_printf(sc->dev,
			      "failed to attach child: error %d\n", error);
	}
}

int
ig4iic_detach(ig4iic_softc_t *sc)
{
	int error;

	if (device_is_attached(sc->dev)) {
		error = bus_generic_detach(sc->dev);
		if (error)
			return (error);
	}
	if (sc->iicbus)
		device_delete_child(sc->dev, sc->iicbus);
	if (sc->intr_handle)
		bus_teardown_intr(sc->dev, sc->intr_res, sc->intr_handle);

	sx_xlock(&sc->call_lock);
	mtx_lock(&sc->io_lock);

	sc->iicbus = NULL;
	sc->intr_handle = NULL;
	reg_write(sc, IG4_REG_INTR_MASK, 0);
	set_controller(sc, 0);

	mtx_unlock(&sc->io_lock);
	sx_xunlock(&sc->call_lock);

	mtx_destroy(&sc->io_lock);
	sx_destroy(&sc->call_lock);

	return (0);
}

/*
 * Interrupt Operation, see ig4_var.h for locking semantics.
 */
static void
ig4iic_intr(void *cookie)
{
	ig4iic_softc_t *sc = cookie;
	uint32_t status;

	mtx_lock(&sc->io_lock);
/*	reg_write(sc, IG4_REG_INTR_MASK, IG4_INTR_STOP_DET);*/
	reg_read(sc, IG4_REG_CLR_INTR);
	status = reg_read(sc, IG4_REG_I2C_STA);
	while (status & IG4_STATUS_RX_NOTEMPTY) {
		sc->rbuf[sc->rnext & IG4_RBUFMASK] =
		    (uint8_t)reg_read(sc, IG4_REG_DATA_CMD);
		++sc->rnext;
		status = reg_read(sc, IG4_REG_I2C_STA);
	}

	/* 
	 * Workaround to trigger pending interrupt if IG4_REG_INTR_STAT
	 * is changed after clearing it
	 */
	if (sc->access_intr_mask != 0) {
		status = reg_read(sc, IG4_REG_INTR_MASK);
		if (status != 0) {
			reg_write(sc, IG4_REG_INTR_MASK, 0);
			reg_write(sc, IG4_REG_INTR_MASK, status);
		}
	}

	wakeup(sc);
	mtx_unlock(&sc->io_lock);
}

#define REGDUMP(sc, reg)	\
	device_printf(sc->dev, "  %-23s %08x\n", #reg, reg_read(sc, reg))

static void
ig4iic_dump(ig4iic_softc_t *sc)
{
	device_printf(sc->dev, "ig4iic register dump:\n");
	REGDUMP(sc, IG4_REG_CTL);
	REGDUMP(sc, IG4_REG_TAR_ADD);
	REGDUMP(sc, IG4_REG_SS_SCL_HCNT);
	REGDUMP(sc, IG4_REG_SS_SCL_LCNT);
	REGDUMP(sc, IG4_REG_FS_SCL_HCNT);
	REGDUMP(sc, IG4_REG_FS_SCL_LCNT);
	REGDUMP(sc, IG4_REG_INTR_STAT);
	REGDUMP(sc, IG4_REG_INTR_MASK);
	REGDUMP(sc, IG4_REG_RAW_INTR_STAT);
	REGDUMP(sc, IG4_REG_RX_TL);
	REGDUMP(sc, IG4_REG_TX_TL);
	REGDUMP(sc, IG4_REG_I2C_EN);
	REGDUMP(sc, IG4_REG_I2C_STA);
	REGDUMP(sc, IG4_REG_TXFLR);
	REGDUMP(sc, IG4_REG_RXFLR);
	REGDUMP(sc, IG4_REG_SDA_HOLD);
	REGDUMP(sc, IG4_REG_TX_ABRT_SOURCE);
	REGDUMP(sc, IG4_REG_SLV_DATA_NACK);
	REGDUMP(sc, IG4_REG_DMA_CTRL);
	REGDUMP(sc, IG4_REG_DMA_TDLR);
	REGDUMP(sc, IG4_REG_DMA_RDLR);
	REGDUMP(sc, IG4_REG_SDA_SETUP);
	REGDUMP(sc, IG4_REG_ENABLE_STATUS);
	if (sc->version == IG4_HASWELL || sc->version == IG4_ATOM) {
		REGDUMP(sc, IG4_REG_COMP_PARAM1);
		REGDUMP(sc, IG4_REG_COMP_VER);
	}
	if (sc->version == IG4_ATOM) {
		REGDUMP(sc, IG4_REG_COMP_TYPE);
		REGDUMP(sc, IG4_REG_CLK_PARMS);
	}
	if (sc->version == IG4_HASWELL || sc->version == IG4_ATOM) {
		REGDUMP(sc, IG4_REG_RESETS_HSW);
		REGDUMP(sc, IG4_REG_GENERAL);
	} else if (sc->version == IG4_SKYLAKE) {
		REGDUMP(sc, IG4_REG_RESETS_SKL);
	}
	if (sc->version == IG4_HASWELL) {
		REGDUMP(sc, IG4_REG_SW_LTR_VALUE);
		REGDUMP(sc, IG4_REG_AUTO_LTR_VALUE);
	} else if (sc->version == IG4_SKYLAKE) {
		REGDUMP(sc, IG4_REG_ACTIVE_LTR_VALUE);
		REGDUMP(sc, IG4_REG_IDLE_LTR_VALUE);
	}
}
#undef REGDUMP

DRIVER_MODULE(iicbus, ig4iic_acpi, iicbus_driver, iicbus_devclass, NULL, NULL);
DRIVER_MODULE(iicbus, ig4iic_pci, iicbus_driver, iicbus_devclass, NULL, NULL);
