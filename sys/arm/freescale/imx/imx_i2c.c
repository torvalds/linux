/*-
 * Copyright (C) 2008-2009 Semihalf, Michal Hajduk
 * Copyright (c) 2012, 2013 The FreeBSD Foundation
 * Copyright (c) 2015 Ian Lepore <ian@FreeBSD.org>
 * All rights reserved.
 *
 * Portions of this software were developed by Oleksandr Rybalko
 * under sponsorship from the FreeBSD Foundation.
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
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * I2C driver for Freescale i.MX hardware.
 *
 * Note that the hardware is capable of running as both a master and a slave.
 * This driver currently implements only master-mode operations.
 *
 * This driver supports multi-master i2c buses, by detecting bus arbitration
 * loss and returning IIC_EBUSBSY status.  Notably, it does not do any kind of
 * retries if some other master jumps onto the bus and interrupts one of our
 * transfer cycles resulting in arbitration loss in mid-transfer.  The caller
 * must handle retries in a way that makes sense for the slave being addressed.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/gpio.h>
#include <sys/kernel.h>
#include <sys/limits.h>
#include <sys/module.h>
#include <sys/resource.h>
#include <sys/sysctl.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <arm/freescale/imx/imx_ccmvar.h>

#include <dev/iicbus/iiconf.h>
#include <dev/iicbus/iicbus.h>
#include <dev/iicbus/iic_recover_bus.h>
#include "iicbus_if.h"

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/fdt/fdt_pinctrl.h>
#include <dev/gpio/gpiobusvar.h>

#define I2C_ADDR_REG		0x00 /* I2C slave address register */
#define I2C_FDR_REG		0x04 /* I2C frequency divider register */
#define I2C_CONTROL_REG		0x08 /* I2C control register */
#define I2C_STATUS_REG		0x0C /* I2C status register */
#define I2C_DATA_REG		0x10 /* I2C data register */
#define I2C_DFSRR_REG		0x14 /* I2C Digital Filter Sampling rate */

#define I2CCR_MEN		(1 << 7) /* Module enable */
#define I2CCR_MSTA		(1 << 5) /* Master/slave mode */
#define I2CCR_MTX		(1 << 4) /* Transmit/receive mode */
#define I2CCR_TXAK		(1 << 3) /* Transfer acknowledge */
#define I2CCR_RSTA		(1 << 2) /* Repeated START */

#define I2CSR_MCF		(1 << 7) /* Data transfer */
#define I2CSR_MASS		(1 << 6) /* Addressed as a slave */
#define I2CSR_MBB		(1 << 5) /* Bus busy */
#define I2CSR_MAL		(1 << 4) /* Arbitration lost */
#define I2CSR_SRW		(1 << 2) /* Slave read/write */
#define I2CSR_MIF		(1 << 1) /* Module interrupt */
#define I2CSR_RXAK		(1 << 0) /* Received acknowledge */

#define I2C_BAUD_RATE_FAST	0x31
#define I2C_BAUD_RATE_DEF	0x3F
#define I2C_DFSSR_DIV		0x10

/*
 * A table of available divisors and the associated coded values to put in the
 * FDR register to achieve that divisor.. There is no algorithmic relationship I
 * can see between divisors and the codes that go into the register.  The table
 * begins and ends with entries that handle insane configuration values.
 */
struct clkdiv {
	u_int divisor;
	u_int regcode;
};
static struct clkdiv clkdiv_table[] = {
        {    0, 0x20 }, {   22, 0x20 }, {   24, 0x21 }, {   26, 0x22 }, 
        {   28, 0x23 }, {   30, 0x00 }, {   32, 0x24 }, {   36, 0x25 }, 
        {   40, 0x26 }, {   42, 0x03 }, {   44, 0x27 }, {   48, 0x28 }, 
        {   52, 0x05 }, {   56, 0x29 }, {   60, 0x06 }, {   64, 0x2a }, 
        {   72, 0x2b }, {   80, 0x2c }, {   88, 0x09 }, {   96, 0x2d }, 
        {  104, 0x0a }, {  112, 0x2e }, {  128, 0x2f }, {  144, 0x0c }, 
        {  160, 0x30 }, {  192, 0x31 }, {  224, 0x32 }, {  240, 0x0f }, 
        {  256, 0x33 }, {  288, 0x10 }, {  320, 0x34 }, {  384, 0x35 }, 
        {  448, 0x36 }, {  480, 0x13 }, {  512, 0x37 }, {  576, 0x14 }, 
        {  640, 0x38 }, {  768, 0x39 }, {  896, 0x3a }, {  960, 0x17 }, 
        { 1024, 0x3b }, { 1152, 0x18 }, { 1280, 0x3c }, { 1536, 0x3d }, 
        { 1792, 0x3e }, { 1920, 0x1b }, { 2048, 0x3f }, { 2304, 0x1c }, 
        { 2560, 0x1d }, { 3072, 0x1e }, { 3840, 0x1f }, {UINT_MAX, 0x1f} 
};

static struct ofw_compat_data compat_data[] = {
	{"fsl,imx6q-i2c",  1},
	{"fsl,imx-i2c",	   1},
	{NULL,             0}
};

struct i2c_softc {
	device_t		dev;
	device_t		iicbus;
	struct resource		*res;
	int			rid;
	sbintime_t		byte_time_sbt;
	int			rb_pinctl_idx;
	gpio_pin_t		rb_sclpin;
	gpio_pin_t 		rb_sdapin;
	u_int			debug;
	u_int			slave;
};

#define DEVICE_DEBUGF(sc, lvl, fmt, args...) \
    if ((lvl) <= (sc)->debug) \
        device_printf((sc)->dev, fmt, ##args)

#define DEBUGF(sc, lvl, fmt, args...) \
    if ((lvl) <= (sc)->debug) \
        printf(fmt, ##args)

static phandle_t i2c_get_node(device_t, device_t);
static int i2c_probe(device_t);
static int i2c_attach(device_t);
static int i2c_detach(device_t);

static int i2c_repeated_start(device_t, u_char, int);
static int i2c_start(device_t, u_char, int);
static int i2c_stop(device_t);
static int i2c_reset(device_t, u_char, u_char, u_char *);
static int i2c_read(device_t, char *, int, int *, int, int);
static int i2c_write(device_t, const char *, int, int *, int);

static device_method_t i2c_methods[] = {
	DEVMETHOD(device_probe,			i2c_probe),
	DEVMETHOD(device_attach,		i2c_attach),
	DEVMETHOD(device_detach,		i2c_detach),

	/* OFW methods */
	DEVMETHOD(ofw_bus_get_node,		i2c_get_node),

	DEVMETHOD(iicbus_callback,		iicbus_null_callback),
	DEVMETHOD(iicbus_repeated_start,	i2c_repeated_start),
	DEVMETHOD(iicbus_start,			i2c_start),
	DEVMETHOD(iicbus_stop,			i2c_stop),
	DEVMETHOD(iicbus_reset,			i2c_reset),
	DEVMETHOD(iicbus_read,			i2c_read),
	DEVMETHOD(iicbus_write,			i2c_write),
	DEVMETHOD(iicbus_transfer,		iicbus_transfer_gen),

	DEVMETHOD_END
};

static driver_t i2c_driver = {
	"imx_i2c",
	i2c_methods,
	sizeof(struct i2c_softc),
};
static devclass_t  i2c_devclass;

DRIVER_MODULE(imx_i2c, simplebus, i2c_driver, i2c_devclass, 0, 0);
DRIVER_MODULE(ofw_iicbus, imx_i2c, ofw_iicbus_driver, ofw_iicbus_devclass, 0, 0);
MODULE_DEPEND(imx_i2c, iicbus, 1, 1, 1);
MODULE_DEPEND(imx_i2c, ofw_iicbus, 1, 1, 1);

static phandle_t
i2c_get_node(device_t bus, device_t dev)
{
	/*
	 * Share controller node with iicbus device
	 */
	return ofw_bus_get_node(bus);
}

static __inline void
i2c_write_reg(struct i2c_softc *sc, bus_size_t off, uint8_t val)
{

	bus_write_1(sc->res, off, val);
}

static __inline uint8_t
i2c_read_reg(struct i2c_softc *sc, bus_size_t off)
{

	return (bus_read_1(sc->res, off));
}

static __inline void
i2c_flag_set(struct i2c_softc *sc, bus_size_t off, uint8_t mask)
{
	uint8_t status;

	status = i2c_read_reg(sc, off);
	status |= mask;
	i2c_write_reg(sc, off, status);
}

/* Wait for bus to become busy or not-busy. */
static int
wait_for_busbusy(struct i2c_softc *sc, int wantbusy)
{
	int retry, srb;

	retry = 1000;
	while (retry --) {
		srb = i2c_read_reg(sc, I2C_STATUS_REG) & I2CSR_MBB;
		if ((srb && wantbusy) || (!srb && !wantbusy))
			return (IIC_NOERR);
		DELAY(1);
	}
	return (IIC_ETIMEOUT);
}

/* Wait for transfer to complete, optionally check RXAK. */
static int
wait_for_xfer(struct i2c_softc *sc, int checkack)
{
	int retry, sr;

	/*
	 * Sleep for about the time it takes to transfer a byte (with precision
	 * set to tolerate 5% oversleep).  We calculate the approximate byte
	 * transfer time when we set the bus speed divisor.  Slaves are allowed
	 * to do clock-stretching so the actual transfer time can be larger, but
	 * this gets the bulk of the waiting out of the way without tying up the
	 * processor the whole time.
	 */
	pause_sbt("imxi2c", sc->byte_time_sbt, sc->byte_time_sbt / 20, 0);

	retry = 10000;
	while (retry --) {
		sr = i2c_read_reg(sc, I2C_STATUS_REG);
		if (sr & I2CSR_MIF) {
                        if (sr & I2CSR_MAL) 
				return (IIC_EBUSERR);
			else if (checkack && (sr & I2CSR_RXAK))
				return (IIC_ENOACK);
			else
				return (IIC_NOERR);
		}
		DELAY(1);
	}
	return (IIC_ETIMEOUT);
}

/*
 * Implement the error handling shown in the state diagram of the imx6 reference
 * manual.  If there was an error, then:
 *  - Clear master mode (MSTA and MTX).
 *  - Wait for the bus to become free or for a timeout to happen.
 *  - Disable the controller.
 */
static int
i2c_error_handler(struct i2c_softc *sc, int error)
{

	if (error != 0) {
		i2c_write_reg(sc, I2C_STATUS_REG, 0);
		i2c_write_reg(sc, I2C_CONTROL_REG, I2CCR_MEN);
		wait_for_busbusy(sc, false);
		i2c_write_reg(sc, I2C_CONTROL_REG, 0);
	}
	return (error);
}

static int
i2c_recover_getsda(void *ctx)
{
	bool active;

	gpio_pin_is_active(((struct i2c_softc *)ctx)->rb_sdapin, &active);
	return (active);
}

static void
i2c_recover_setsda(void *ctx, int value)
{

	gpio_pin_set_active(((struct i2c_softc *)ctx)->rb_sdapin, value);
}

static int
i2c_recover_getscl(void *ctx)
{
	bool active;

	gpio_pin_is_active(((struct i2c_softc *)ctx)->rb_sclpin, &active);
	return (active);

}

static void
i2c_recover_setscl(void *ctx, int value)
{

	gpio_pin_set_active(((struct i2c_softc *)ctx)->rb_sclpin, value);
}

static int
i2c_recover_bus(struct i2c_softc *sc)
{
	struct iicrb_pin_access pins;
	int err;

	/*
	 * If we have gpio pinmux config, reconfigure the pins to gpio mode,
	 * invoke iic_recover_bus which checks for a hung bus and bitbangs a
	 * recovery sequence if necessary, then configure the pins back to i2c
	 * mode (idx 0).
	 */
	if (sc->rb_pinctl_idx == 0)
		return (0);

	fdt_pinctrl_configure(sc->dev, sc->rb_pinctl_idx);

	pins.ctx = sc;
	pins.getsda = i2c_recover_getsda;
	pins.setsda = i2c_recover_setsda;
	pins.getscl = i2c_recover_getscl;
	pins.setscl = i2c_recover_setscl;
	err = iic_recover_bus(&pins);

	fdt_pinctrl_configure(sc->dev, 0);

	return (err);
}

static int
i2c_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "Freescale i.MX I2C");

	return (BUS_PROBE_DEFAULT);
}

static int
i2c_attach(device_t dev)
{
	char wrkstr[16];
	struct i2c_softc *sc;
	phandle_t node;
	int err, cfgidx;

	sc = device_get_softc(dev);
	sc->dev = dev;
	sc->rid = 0;

	sc->res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &sc->rid,
	    RF_ACTIVE);
	if (sc->res == NULL) {
		device_printf(dev, "could not allocate resources");
		return (ENXIO);
	}

	sc->iicbus = device_add_child(dev, "iicbus", -1);
	if (sc->iicbus == NULL) {
		device_printf(dev, "could not add iicbus child");
		return (ENXIO);
	}

	/* Set up debug-enable sysctl. */
	SYSCTL_ADD_INT(device_get_sysctl_ctx(sc->dev), 
	    SYSCTL_CHILDREN(device_get_sysctl_tree(sc->dev)),
	    OID_AUTO, "debug", CTLFLAG_RWTUN, &sc->debug, 0,
	    "Enable debug; 1=reads/writes, 2=add starts/stops");

	/*
	 * Set up for bus recovery using gpio pins, if the pinctrl and gpio
	 * properties are present.  This is optional.  If all the config data is
	 * not in place, we just don't do gpio bitbang bus recovery.
	 */
	node = ofw_bus_get_node(sc->dev);

	err = gpio_pin_get_by_ofw_property(dev, node, "scl-gpios",
	    &sc->rb_sclpin);
	if (err != 0)
		goto no_recovery;
	err = gpio_pin_get_by_ofw_property(dev, node, "sda-gpios",
	    &sc->rb_sdapin);
	if (err != 0)
		goto no_recovery;

	/*
	 * Preset the gpio pins to output high (idle bus state).  The signal
	 * won't actually appear on the pins until the bus recovery code changes
	 * the pinmux config from i2c to gpio.
	 */
	gpio_pin_setflags(sc->rb_sclpin, GPIO_PIN_OUTPUT);
	gpio_pin_setflags(sc->rb_sdapin, GPIO_PIN_OUTPUT);
	gpio_pin_set_active(sc->rb_sclpin, true);
	gpio_pin_set_active(sc->rb_sdapin, true);

	/*
	 * Obtain the index of pinctrl node for bus recovery using gpio pins,
	 * then confirm that pinctrl properties exist for that index and for the
	 * default pinctrl-0.  If sc->rb_pinctl_idx is non-zero, the reset code
	 * will also do a bus recovery, so setting this value must be last.
	 */
	err = ofw_bus_find_string_index(node, "pinctrl-names", "gpio", &cfgidx);
	if (err == 0) {
		snprintf(wrkstr, sizeof(wrkstr), "pinctrl-%d", cfgidx);
		if (OF_hasprop(node, "pinctrl-0") && OF_hasprop(node, wrkstr))
			sc->rb_pinctl_idx = cfgidx;
	}

no_recovery:

	/* We don't do a hardware reset here because iicbus_attach() does it. */

	/* Probe and attach the iicbus when interrupts are available. */
	config_intrhook_oneshot((ich_func_t)bus_generic_attach, dev);
	return (0);
}

static int
i2c_detach(device_t dev)
{
	struct i2c_softc *sc;
	int error;

	sc = device_get_softc(dev);

	if ((error = bus_generic_detach(sc->dev)) != 0) {
		device_printf(sc->dev, "cannot detach child devices\n");
		return (error);
	}

	if (sc->iicbus != NULL)
		device_delete_child(dev, sc->iicbus);

	if (sc->res != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->res);

	return (0);
}

static int
i2c_repeated_start(device_t dev, u_char slave, int timeout)
{
	struct i2c_softc *sc;
	int error;

	sc = device_get_softc(dev);

	if ((i2c_read_reg(sc, I2C_STATUS_REG) & I2CSR_MBB) == 0) {
		return (IIC_EBUSERR);
	}

	/*
	 * Set repeated start condition, delay (per reference manual, min 156nS)
	 * before writing slave address, wait for ack after write.
	 */
	i2c_flag_set(sc, I2C_CONTROL_REG, I2CCR_RSTA);
	DELAY(1);
	i2c_write_reg(sc, I2C_STATUS_REG, 0x0);
	i2c_write_reg(sc, I2C_DATA_REG, slave);
	sc->slave = slave;
	DEVICE_DEBUGF(sc, 2, "rstart 0x%02x\n", sc->slave);
	error = wait_for_xfer(sc, true);
	return (i2c_error_handler(sc, error));
}

static int
i2c_start_ll(device_t dev, u_char slave, int timeout)
{
	struct i2c_softc *sc;
	int error;

	sc = device_get_softc(dev);

	i2c_write_reg(sc, I2C_CONTROL_REG, I2CCR_MEN);
	DELAY(10); /* Delay for controller to sample bus state. */
	if (i2c_read_reg(sc, I2C_STATUS_REG) & I2CSR_MBB) {
		return (i2c_error_handler(sc, IIC_EBUSERR));
	}
	i2c_write_reg(sc, I2C_CONTROL_REG, I2CCR_MEN | I2CCR_MSTA | I2CCR_MTX);
	if ((error = wait_for_busbusy(sc, true)) != IIC_NOERR)
		return (i2c_error_handler(sc, error));
	i2c_write_reg(sc, I2C_STATUS_REG, 0);
	i2c_write_reg(sc, I2C_DATA_REG, slave);
	sc->slave = slave;
	DEVICE_DEBUGF(sc, 2, "start  0x%02x\n", sc->slave);
	error = wait_for_xfer(sc, true);
	return (i2c_error_handler(sc, error));
}

static int
i2c_start(device_t dev, u_char slave, int timeout)
{
	struct i2c_softc *sc;
	int error;

	sc = device_get_softc(dev);

	/*
	 * Invoke the low-level code to put the bus into master mode and address
	 * the given slave.  If that fails, idle the controller and attempt a
	 * bus recovery, and then try again one time.  Signaling a start and
	 * addressing the slave is the only operation that a low-level driver
	 * can safely retry without any help from the upper layers that know
	 * more about the slave device.
	 */
	if ((error = i2c_start_ll(dev, slave, timeout)) != 0) {
		i2c_write_reg(sc, I2C_CONTROL_REG, 0x0);
		if ((error = i2c_recover_bus(sc)) != 0)
			return (error);
		error = i2c_start_ll(dev, slave, timeout);
	}
	return (error);
}

static int
i2c_stop(device_t dev)
{
	struct i2c_softc *sc;

	sc = device_get_softc(dev);

	i2c_write_reg(sc, I2C_CONTROL_REG, I2CCR_MEN);
	wait_for_busbusy(sc, false);
	i2c_write_reg(sc, I2C_CONTROL_REG, 0);
	DEVICE_DEBUGF(sc, 2, "stop   0x%02x\n", sc->slave);
	return (IIC_NOERR);
}

static int
i2c_reset(device_t dev, u_char speed, u_char addr, u_char *oldadr)
{
	struct i2c_softc *sc;
	u_int busfreq, div, i, ipgfreq;

	sc = device_get_softc(dev);

	DEVICE_DEBUGF(sc, 1, "reset\n");

	/*
	 * Look up the divisor that gives the nearest speed that doesn't exceed
	 * the configured value for the bus.
	 */
	ipgfreq = imx_ccm_ipg_hz();
	busfreq = IICBUS_GET_FREQUENCY(sc->iicbus, speed);
	div = howmany(ipgfreq, busfreq);
	for (i = 0; i < nitems(clkdiv_table); i++) {
		if (clkdiv_table[i].divisor >= div)
			break;
	}

	/*
	 * Calculate roughly how long it will take to transfer a byte (which
	 * requires 9 clock cycles) at the new bus speed.  This value is used to
	 * pause() while waiting for transfer-complete.  With a 66MHz IPG clock
	 * and the actual i2c bus speeds that leads to, for nominal 100KHz and
	 * 400KHz bus speeds the transfer times are roughly 104uS and 22uS.
	 */
	busfreq = ipgfreq / clkdiv_table[i].divisor;
	sc->byte_time_sbt = SBT_1US * (9000000 / busfreq);

	/*
	 * Disable the controller (do the reset), and set the new clock divisor.
	 */
	i2c_write_reg(sc, I2C_STATUS_REG, 0x0);
	i2c_write_reg(sc, I2C_CONTROL_REG, 0x0);
	i2c_write_reg(sc, I2C_FDR_REG, (uint8_t)clkdiv_table[i].regcode);

	/*
	 * Now that the controller is idle, perform bus recovery.  If the bus
	 * isn't hung, this a fairly fast no-op.
	 */
	return (i2c_recover_bus(sc));
}

static int
i2c_read(device_t dev, char *buf, int len, int *read, int last, int delay)
{
	struct i2c_softc *sc;
	int error, reg;

	sc = device_get_softc(dev);
	*read = 0;

	DEVICE_DEBUGF(sc, 1, "read   0x%02x len %d: ", sc->slave, len);
	if (len) {
		if (len == 1)
			i2c_write_reg(sc, I2C_CONTROL_REG, I2CCR_MEN |
			    I2CCR_MSTA | I2CCR_TXAK);
		else
			i2c_write_reg(sc, I2C_CONTROL_REG, I2CCR_MEN |
			    I2CCR_MSTA);
                /* Dummy read to prime the receiver. */
		i2c_write_reg(sc, I2C_STATUS_REG, 0x0);
		i2c_read_reg(sc, I2C_DATA_REG);
	}

	error = 0;
	*read = 0;
	while (*read < len) {
		if ((error = wait_for_xfer(sc, false)) != IIC_NOERR)
			break;
		i2c_write_reg(sc, I2C_STATUS_REG, 0x0);
		if (last) {
			if (*read == len - 2) {
				/* NO ACK on last byte */
				i2c_write_reg(sc, I2C_CONTROL_REG, I2CCR_MEN |
				    I2CCR_MSTA | I2CCR_TXAK);
			} else if (*read == len - 1) {
				/* Transfer done, signal stop. */
				i2c_write_reg(sc, I2C_CONTROL_REG, I2CCR_MEN |
				    I2CCR_TXAK);
				wait_for_busbusy(sc, false);
			}
		}
		reg = i2c_read_reg(sc, I2C_DATA_REG);
		DEBUGF(sc, 1, "0x%02x ", reg);
		*buf++ = reg;
		(*read)++;
	}
	DEBUGF(sc, 1, "\n");

	return (i2c_error_handler(sc, error));
}

static int
i2c_write(device_t dev, const char *buf, int len, int *sent, int timeout)
{
	struct i2c_softc *sc;
	int error;

	sc = device_get_softc(dev);

	error = 0;
	*sent = 0;
	DEVICE_DEBUGF(sc, 1, "write  0x%02x len %d: ", sc->slave, len);
	while (*sent < len) {
		DEBUGF(sc, 1, "0x%02x ", *buf);
		i2c_write_reg(sc, I2C_STATUS_REG, 0x0);
		i2c_write_reg(sc, I2C_DATA_REG, *buf++);
		if ((error = wait_for_xfer(sc, true)) != IIC_NOERR)
			break;
		(*sent)++;
	}
	DEBUGF(sc, 1, "\n");
	return (i2c_error_handler(sc, error));
}
