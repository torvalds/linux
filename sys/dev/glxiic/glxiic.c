/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 Henrik Brix Andersen <brix@FreeBSD.org>
 * All rights reserved.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
/*
 * AMD Geode LX CS5536 System Management Bus controller.
 *
 * Although AMD refers to this device as an SMBus controller, it
 * really is an I2C controller (It lacks SMBus ALERT# and Alert
 * Response support).
 *
 * The driver is implemented as an interrupt-driven state machine,
 * supporting both master and slave mode.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>
#ifdef GLXIIC_DEBUG
#include <sys/syslog.h>
#endif

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <machine/bus.h>
#include <sys/rman.h>
#include <machine/resource.h>

#include <dev/iicbus/iiconf.h>
#include <dev/iicbus/iicbus.h>

#include "iicbus_if.h"

/* CS5536 PCI-ISA ID. */
#define	GLXIIC_CS5536_DEV_ID		0x20901022

/* MSRs. */
#define	GLXIIC_MSR_PIC_YSEL_HIGH	0x51400021

/* Bus speeds. */
#define	GLXIIC_SLOW	0x0258	/*  10 kHz. */
#define	GLXIIC_FAST	0x0078	/*  50 kHz. */
#define	GLXIIC_FASTEST	0x003c	/* 100 kHz. */

/* Default bus activity timeout in milliseconds. */
#define GLXIIC_DEFAULT_TIMEOUT	35

/* GPIO register offsets. */
#define	GLXIIC_GPIOL_OUT_AUX1_SEL	0x10
#define	GLXIIC_GPIOL_IN_AUX1_SEL	0x34

/* GPIO 14 (SMB_CLK) and 15 (SMB_DATA) bitmasks. */
#define	GLXIIC_GPIO_14_15_ENABLE	0x0000c000
#define	GLXIIC_GPIO_14_15_DISABLE	0xc0000000

/* SMB register offsets. */
#define	GLXIIC_SMB_SDA				0x00
#define	GLXIIC_SMB_STS				0x01
#define		GLXIIC_SMB_STS_SLVSTP_BIT	(1 << 7)
#define		GLXIIC_SMB_STS_SDAST_BIT	(1 << 6)
#define		GLXIIC_SMB_STS_BER_BIT		(1 << 5)
#define		GLXIIC_SMB_STS_NEGACK_BIT	(1 << 4)
#define		GLXIIC_SMB_STS_STASTR_BIT	(1 << 3)
#define		GLXIIC_SMB_STS_NMATCH_BIT	(1 << 2)
#define		GLXIIC_SMB_STS_MASTER_BIT	(1 << 1)
#define		GLXIIC_SMB_STS_XMIT_BIT		(1 << 0)
#define	GLXIIC_SMB_CTRL_STS			0x02
#define		GLXIIC_SMB_CTRL_STS_TGSCL_BIT	(1 << 5)
#define		GLXIIC_SMB_CTRL_STS_TSDA_BIT	(1 << 4)
#define		GLXIIC_SMB_CTRL_STS_GCMTCH_BIT	(1 << 3)
#define		GLXIIC_SMB_CTRL_STS_MATCH_BIT	(1 << 2)
#define		GLXIIC_SMB_CTRL_STS_BB_BIT	(1 << 1)
#define		GLXIIC_SMB_CTRL_STS_BUSY_BIT	(1 << 0)
#define	GLXIIC_SMB_CTRL1			0x03
#define		GLXIIC_SMB_CTRL1_STASTRE_BIT	(1 << 7)
#define		GLXIIC_SMB_CTRL1_NMINTE_BIT	(1 << 6)
#define		GLXIIC_SMB_CTRL1_GCMEN_BIT	(1 << 5)
#define		GLXIIC_SMB_CTRL1_ACK_BIT	(1 << 4)
#define		GLXIIC_SMB_CTRL1_INTEN_BIT	(1 << 2)
#define		GLXIIC_SMB_CTRL1_STOP_BIT	(1 << 1)
#define		GLXIIC_SMB_CTRL1_START_BIT	(1 << 0)
#define	GLXIIC_SMB_ADDR				0x04
#define		GLXIIC_SMB_ADDR_SAEN_BIT	(1 << 7)
#define	GLXIIC_SMB_CTRL2			0x05
#define		GLXIIC_SMB_CTRL2_EN_BIT		(1 << 0)
#define	GLXIIC_SMB_CTRL3			0x06

typedef enum {
	GLXIIC_STATE_IDLE,
	GLXIIC_STATE_SLAVE_TX,
	GLXIIC_STATE_SLAVE_RX,
	GLXIIC_STATE_MASTER_ADDR,
	GLXIIC_STATE_MASTER_TX,
	GLXIIC_STATE_MASTER_RX,
	GLXIIC_STATE_MASTER_STOP,
	GLXIIC_STATE_MAX,
} glxiic_state_t;

struct glxiic_softc {
	device_t	 dev;		/* Myself. */
	device_t	 iicbus;	/* IIC bus. */
	struct mtx	 mtx;		/* Lock. */
	glxiic_state_t	 state;		/* Driver state. */
	struct callout	 callout;	/* Driver state timeout callout. */
	int		 timeout;	/* Driver state timeout (ms). */

	int		 smb_rid;	/* SMB controller resource ID. */
	struct resource *smb_res;	/* SMB controller resource. */
	int		 gpio_rid;	/* GPIO resource ID. */
	struct resource *gpio_res;	/* GPIO resource. */

	int		 irq_rid;	/* IRQ resource ID. */
	struct resource *irq_res;	/* IRQ resource. */
	void		*irq_handler;	/* IRQ handler cookie. */
	int		 old_irq;	/* IRQ mapped by board firmware. */

	struct iic_msg	*msg;		/* Current master mode message. */
	uint32_t	 nmsgs;		/* Number of messages remaining. */
	uint8_t		*data;		/* Current master mode data byte. */
	uint16_t	 ndata;		/* Number of data bytes remaining. */
	int		 error;		/* Last master mode error. */

	uint8_t		 addr;		/* Own address. */
	uint16_t	 sclfrq;	/* Bus frequency. */
};

#ifdef GLXIIC_DEBUG
#define GLXIIC_DEBUG_LOG(fmt, args...)	\
	log(LOG_DEBUG, "%s: " fmt "\n" , __func__ , ## args)
#else
#define GLXIIC_DEBUG_LOG(fmt, args...)
#endif

#define	GLXIIC_SCLFRQ(n)		((n << 1))
#define	GLXIIC_SMBADDR(n)		((n >> 1))
#define	GLXIIC_SMB_IRQ_TO_MAP(n)	((n << 16))
#define	GLXIIC_MAP_TO_SMB_IRQ(n)	((n >> 16) & 0xf)

#define	GLXIIC_LOCK(_sc)		mtx_lock(&_sc->mtx)
#define	GLXIIC_UNLOCK(_sc)		mtx_unlock(&_sc->mtx)
#define	GLXIIC_LOCK_INIT(_sc)		\
	mtx_init(&_sc->mtx, device_get_nameunit(_sc->dev), "glxiic", MTX_DEF)
#define	GLXIIC_SLEEP(_sc)		\
	mtx_sleep(_sc, &_sc->mtx, IICPRI, "glxiic", 0)
#define	GLXIIC_WAKEUP(_sc)		wakeup(_sc);
#define	GLXIIC_LOCK_DESTROY(_sc)	mtx_destroy(&_sc->mtx);
#define	GLXIIC_ASSERT_LOCKED(_sc)	mtx_assert(&_sc->mtx, MA_OWNED);

typedef	int (glxiic_state_callback_t)(struct glxiic_softc *sc,
    uint8_t status);

static glxiic_state_callback_t	glxiic_state_idle_callback;
static glxiic_state_callback_t	glxiic_state_slave_tx_callback;
static glxiic_state_callback_t	glxiic_state_slave_rx_callback;
static glxiic_state_callback_t	glxiic_state_master_addr_callback;
static glxiic_state_callback_t	glxiic_state_master_tx_callback;
static glxiic_state_callback_t	glxiic_state_master_rx_callback;
static glxiic_state_callback_t	glxiic_state_master_stop_callback;

struct glxiic_state_table_entry {
	glxiic_state_callback_t *callback;
	boolean_t master;
};
typedef struct glxiic_state_table_entry glxiic_state_table_entry_t;

static glxiic_state_table_entry_t glxiic_state_table[GLXIIC_STATE_MAX] = {
	[GLXIIC_STATE_IDLE] = {
		.callback = &glxiic_state_idle_callback,
		.master = FALSE,
	},

	[GLXIIC_STATE_SLAVE_TX] = {
		.callback = &glxiic_state_slave_tx_callback,
		.master = FALSE,
	},

	[GLXIIC_STATE_SLAVE_RX] = {
		.callback = &glxiic_state_slave_rx_callback,
		.master = FALSE,
	},

	[GLXIIC_STATE_MASTER_ADDR] = {
		.callback = &glxiic_state_master_addr_callback,
		.master = TRUE,
	},

	[GLXIIC_STATE_MASTER_TX] = {
		.callback = &glxiic_state_master_tx_callback,
		.master = TRUE,
	},

	[GLXIIC_STATE_MASTER_RX] = {
		.callback = &glxiic_state_master_rx_callback,
		.master = TRUE,
	},

	[GLXIIC_STATE_MASTER_STOP] = {
		.callback = &glxiic_state_master_stop_callback,
		.master = TRUE,
	},
};

static void	glxiic_identify(driver_t *driver, device_t parent);
static int	glxiic_probe(device_t dev);
static int	glxiic_attach(device_t dev);
static int	glxiic_detach(device_t dev);

static uint8_t	glxiic_read_status_locked(struct glxiic_softc *sc);
static void	glxiic_stop_locked(struct glxiic_softc *sc);
static void	glxiic_timeout(void *arg);
static void	glxiic_start_timeout_locked(struct glxiic_softc *sc);
static void	glxiic_set_state_locked(struct glxiic_softc *sc,
    glxiic_state_t state);
static int	glxiic_handle_slave_match_locked(struct glxiic_softc *sc,
    uint8_t status);
static void	glxiic_intr(void *arg);

static int	glxiic_reset(device_t dev, u_char speed, u_char addr,
    u_char *oldaddr);
static int	glxiic_transfer(device_t dev, struct iic_msg *msgs,
    uint32_t nmsgs);

static void	glxiic_smb_map_interrupt(int irq);
static void 	glxiic_gpio_enable(struct glxiic_softc *sc);
static void 	glxiic_gpio_disable(struct glxiic_softc *sc);
static void	glxiic_smb_enable(struct glxiic_softc *sc, uint8_t speed,
    uint8_t addr);
static void	glxiic_smb_disable(struct glxiic_softc *sc);

static device_method_t glxiic_methods[] = {
	DEVMETHOD(device_identify,	glxiic_identify),
	DEVMETHOD(device_probe,		glxiic_probe),
	DEVMETHOD(device_attach,	glxiic_attach),
	DEVMETHOD(device_detach,	glxiic_detach),

	DEVMETHOD(iicbus_reset,		glxiic_reset),
	DEVMETHOD(iicbus_transfer,	glxiic_transfer),
	DEVMETHOD(iicbus_callback,	iicbus_null_callback),

	{ 0, 0 }
};

static driver_t glxiic_driver = {
	"glxiic",
	glxiic_methods,
	sizeof(struct glxiic_softc),
};

static devclass_t glxiic_devclass;

DRIVER_MODULE(glxiic, isab, glxiic_driver, glxiic_devclass, 0, 0);
DRIVER_MODULE(iicbus, glxiic, iicbus_driver, iicbus_devclass, 0, 0);
MODULE_DEPEND(glxiic, iicbus, 1, 1, 1);

static void
glxiic_identify(driver_t *driver, device_t parent)
{

	/* Prevent child from being added more than once. */
	if (device_find_child(parent, driver->name, -1) != NULL)
		return;

	if (pci_get_devid(parent) == GLXIIC_CS5536_DEV_ID) {
		if (device_add_child(parent, driver->name, -1) == NULL)
			device_printf(parent, "Could not add glxiic child\n");
	}
}

static int
glxiic_probe(device_t dev)
{

	if (resource_disabled("glxiic", device_get_unit(dev)))
		return (ENXIO);

	device_set_desc(dev, "AMD Geode CS5536 SMBus controller");

	return (BUS_PROBE_DEFAULT);
}

static int
glxiic_attach(device_t dev)
{
	struct glxiic_softc *sc;
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *tree;
	int error, irq, unit;
	uint32_t irq_map;

	sc = device_get_softc(dev);
	sc->dev = dev;
	sc->state = GLXIIC_STATE_IDLE;
	error = 0;

	GLXIIC_LOCK_INIT(sc);
	callout_init_mtx(&sc->callout, &sc->mtx, 0);

	sc->smb_rid = PCIR_BAR(0);
	sc->smb_res = bus_alloc_resource_any(dev, SYS_RES_IOPORT, &sc->smb_rid,
	    RF_ACTIVE);
	if (sc->smb_res == NULL) {
		device_printf(dev, "Could not allocate SMBus I/O port\n");
		error = ENXIO;
		goto out;
	}

	sc->gpio_rid = PCIR_BAR(1);
	sc->gpio_res = bus_alloc_resource_any(dev, SYS_RES_IOPORT,
	    &sc->gpio_rid, RF_SHAREABLE | RF_ACTIVE);
	if (sc->gpio_res == NULL) {
		device_printf(dev, "Could not allocate GPIO I/O port\n");
		error = ENXIO;
		goto out;
	}

	/* Ensure the controller is not enabled by firmware. */
	glxiic_smb_disable(sc);

	/* Read the existing IRQ map. */
	irq_map = rdmsr(GLXIIC_MSR_PIC_YSEL_HIGH);
	sc->old_irq = GLXIIC_MAP_TO_SMB_IRQ(irq_map);

	unit = device_get_unit(dev);
	if (resource_int_value("glxiic", unit, "irq", &irq) == 0) {
		if (irq < 1 || irq > 15) {
			device_printf(dev, "Bad value %d for glxiic.%d.irq\n",
			    irq, unit);
			error = ENXIO;
			goto out;
		}

		if (bootverbose)
			device_printf(dev, "Using irq %d set by hint\n", irq);
	} else if (sc->old_irq != 0) {
		if (bootverbose)
			device_printf(dev, "Using irq %d set by firmware\n",
			    irq);
		irq = sc->old_irq;
	} else {
		device_printf(dev, "No irq mapped by firmware");
		printf(" and no glxiic.%d.irq hint provided\n", unit);
		error = ENXIO;
		goto out;
	}

	/* Map the SMBus interrupt to the requested legacy IRQ. */
	glxiic_smb_map_interrupt(irq);

	sc->irq_rid = 0;
	sc->irq_res = bus_alloc_resource(dev, SYS_RES_IRQ, &sc->irq_rid,
	    irq, irq, 1, RF_SHAREABLE | RF_ACTIVE);
	if (sc->irq_res == NULL) {
		device_printf(dev, "Could not allocate IRQ %d\n", irq);
		error = ENXIO;
		goto out;
	}

	error = bus_setup_intr(dev, sc->irq_res, INTR_TYPE_MISC | INTR_MPSAFE,
	    NULL, glxiic_intr, sc, &(sc->irq_handler));
	if (error != 0) {
		device_printf(dev, "Could not setup IRQ handler\n");
		error = ENXIO;
		goto out;
	}

	if ((sc->iicbus = device_add_child(dev, "iicbus", -1)) == NULL) {
		device_printf(dev, "Could not allocate iicbus instance\n");
		error = ENXIO;
		goto out;
	}

	ctx = device_get_sysctl_ctx(dev);
	tree = device_get_sysctl_tree(dev);

	sc->timeout = GLXIIC_DEFAULT_TIMEOUT;
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
	    "timeout", CTLFLAG_RWTUN, &sc->timeout, 0,
	    "activity timeout in ms");

	glxiic_gpio_enable(sc);
	glxiic_smb_enable(sc, IIC_FASTEST, 0);

	/* Probe and attach the iicbus when interrupts are available. */
	config_intrhook_oneshot((ich_func_t)bus_generic_attach, dev);
	error = 0;

out:
	if (error != 0) {
		callout_drain(&sc->callout);

		if (sc->iicbus != NULL)
			device_delete_child(dev, sc->iicbus);
		if (sc->smb_res != NULL) {
			glxiic_smb_disable(sc);
			bus_release_resource(dev, SYS_RES_IOPORT, sc->smb_rid,
			    sc->smb_res);
		}
		if (sc->gpio_res != NULL) {
			glxiic_gpio_disable(sc);
			bus_release_resource(dev, SYS_RES_IOPORT, sc->gpio_rid,
			    sc->gpio_res);
		}
		if (sc->irq_handler != NULL)
			bus_teardown_intr(dev, sc->irq_res, sc->irq_handler);
		if (sc->irq_res != NULL)
			bus_release_resource(dev, SYS_RES_IRQ, sc->irq_rid,
			    sc->irq_res);

		/* Restore the old SMBus interrupt mapping. */
		glxiic_smb_map_interrupt(sc->old_irq);

		GLXIIC_LOCK_DESTROY(sc);
	}

	return (error);
}

static int
glxiic_detach(device_t dev)
{
	struct glxiic_softc *sc;
	int error;

	sc = device_get_softc(dev);

	error = bus_generic_detach(dev);
	if (error != 0)
		goto out;
	if (sc->iicbus != NULL)
		error = device_delete_child(dev, sc->iicbus);

out:
	callout_drain(&sc->callout);

	if (sc->smb_res != NULL) {
		glxiic_smb_disable(sc);
		bus_release_resource(dev, SYS_RES_IOPORT, sc->smb_rid,
		    sc->smb_res);
	}
	if (sc->gpio_res != NULL) {
		glxiic_gpio_disable(sc);
		bus_release_resource(dev, SYS_RES_IOPORT, sc->gpio_rid,
		    sc->gpio_res);
	}
	if (sc->irq_handler != NULL)
		bus_teardown_intr(dev, sc->irq_res, sc->irq_handler);
	if (sc->irq_res != NULL)
		bus_release_resource(dev, SYS_RES_IRQ, sc->irq_rid,
		    sc->irq_res);

	/* Restore the old SMBus interrupt mapping. */
	glxiic_smb_map_interrupt(sc->old_irq);

	GLXIIC_LOCK_DESTROY(sc);

	return (error);
}

static uint8_t
glxiic_read_status_locked(struct glxiic_softc *sc)
{
	uint8_t status;

	GLXIIC_ASSERT_LOCKED(sc);

	status = bus_read_1(sc->smb_res, GLXIIC_SMB_STS);

	/* Clear all status flags except SDAST and STASTR after reading. */
	bus_write_1(sc->smb_res, GLXIIC_SMB_STS, (GLXIIC_SMB_STS_SLVSTP_BIT |
		GLXIIC_SMB_STS_BER_BIT | GLXIIC_SMB_STS_NEGACK_BIT |
		GLXIIC_SMB_STS_NMATCH_BIT));

	return (status);
}

static void
glxiic_stop_locked(struct glxiic_softc *sc)
{
	uint8_t status, ctrl1;

	GLXIIC_ASSERT_LOCKED(sc);

	status = glxiic_read_status_locked(sc);

	ctrl1 = bus_read_1(sc->smb_res, GLXIIC_SMB_CTRL1);
	bus_write_1(sc->smb_res, GLXIIC_SMB_CTRL1,
	    ctrl1 | GLXIIC_SMB_CTRL1_STOP_BIT);

	/*
	 * Perform a dummy read of SDA in master receive mode to clear
	 * SDAST if set.
	 */
	if ((status & GLXIIC_SMB_STS_XMIT_BIT) == 0 &&
	    (status & GLXIIC_SMB_STS_SDAST_BIT) != 0)
	 	bus_read_1(sc->smb_res, GLXIIC_SMB_SDA);

	/* Check stall after start bit and clear if needed */
	if ((status & GLXIIC_SMB_STS_STASTR_BIT) != 0) {
		bus_write_1(sc->smb_res, GLXIIC_SMB_STS,
		    GLXIIC_SMB_STS_STASTR_BIT);
	}
}

static void
glxiic_timeout(void *arg)
{
	struct glxiic_softc *sc;
	uint8_t error;

	sc = (struct glxiic_softc *)arg;

	GLXIIC_DEBUG_LOG("timeout in state %d", sc->state);

	if (glxiic_state_table[sc->state].master) {
		sc->error = IIC_ETIMEOUT;
		GLXIIC_WAKEUP(sc);
	} else {
		error = IIC_ETIMEOUT;
		iicbus_intr(sc->iicbus, INTR_ERROR, &error);
	}

	glxiic_smb_disable(sc);
	glxiic_smb_enable(sc, IIC_UNKNOWN, sc->addr);
	glxiic_set_state_locked(sc, GLXIIC_STATE_IDLE);
}

static void
glxiic_start_timeout_locked(struct glxiic_softc *sc)
{

	GLXIIC_ASSERT_LOCKED(sc);

	callout_reset_sbt(&sc->callout, SBT_1MS * sc->timeout, 0,
	    glxiic_timeout, sc, 0);
}

static void
glxiic_set_state_locked(struct glxiic_softc *sc, glxiic_state_t state)
{

	GLXIIC_ASSERT_LOCKED(sc);

	if (state == GLXIIC_STATE_IDLE)
		callout_stop(&sc->callout);
	else if (sc->timeout > 0)
		glxiic_start_timeout_locked(sc);

	sc->state = state;
}

static int
glxiic_handle_slave_match_locked(struct glxiic_softc *sc, uint8_t status)
{
	uint8_t ctrl_sts, addr;

	GLXIIC_ASSERT_LOCKED(sc);

	ctrl_sts = bus_read_1(sc->smb_res, GLXIIC_SMB_CTRL_STS);

	if ((ctrl_sts & GLXIIC_SMB_CTRL_STS_MATCH_BIT) != 0) {
		if ((status & GLXIIC_SMB_STS_XMIT_BIT) != 0) {
			addr = sc->addr | LSB;
			glxiic_set_state_locked(sc,
			    GLXIIC_STATE_SLAVE_TX);
		} else {
			addr = sc->addr & ~LSB;
			glxiic_set_state_locked(sc,
			    GLXIIC_STATE_SLAVE_RX);
		}
		iicbus_intr(sc->iicbus, INTR_START, &addr);
	} else if ((ctrl_sts & GLXIIC_SMB_CTRL_STS_GCMTCH_BIT) != 0) {
		addr = 0;
		glxiic_set_state_locked(sc, GLXIIC_STATE_SLAVE_RX);
		iicbus_intr(sc->iicbus, INTR_GENERAL, &addr);
	} else {
		GLXIIC_DEBUG_LOG("unknown slave match");
		return (IIC_ESTATUS);
	}

	return (IIC_NOERR);
}

static int
glxiic_state_idle_callback(struct glxiic_softc *sc, uint8_t status)
{

	GLXIIC_ASSERT_LOCKED(sc);

	if ((status & GLXIIC_SMB_STS_BER_BIT) != 0) {
		GLXIIC_DEBUG_LOG("bus error in idle");
		return (IIC_EBUSERR);
	}

	if ((status & GLXIIC_SMB_STS_NMATCH_BIT) != 0) {
		return (glxiic_handle_slave_match_locked(sc, status));
	}

	return (IIC_NOERR);
}

static int
glxiic_state_slave_tx_callback(struct glxiic_softc *sc, uint8_t status)
{
	uint8_t data;

	GLXIIC_ASSERT_LOCKED(sc);

	if ((status & GLXIIC_SMB_STS_BER_BIT) != 0) {
		GLXIIC_DEBUG_LOG("bus error in slave tx");
		return (IIC_EBUSERR);
	}

	if ((status & GLXIIC_SMB_STS_SLVSTP_BIT) != 0) {
		iicbus_intr(sc->iicbus, INTR_STOP, NULL);
		glxiic_set_state_locked(sc, GLXIIC_STATE_IDLE);
		return (IIC_NOERR);
	}

	if ((status & GLXIIC_SMB_STS_NEGACK_BIT) != 0) {
		iicbus_intr(sc->iicbus, INTR_NOACK, NULL);
		return (IIC_NOERR);
	}

	if ((status & GLXIIC_SMB_STS_NMATCH_BIT) != 0) {
		/* Handle repeated start in slave mode. */
		return (glxiic_handle_slave_match_locked(sc, status));
	}

	if ((status & GLXIIC_SMB_STS_SDAST_BIT) == 0) {
		GLXIIC_DEBUG_LOG("not awaiting data in slave tx");
		return (IIC_ESTATUS);
	}

	iicbus_intr(sc->iicbus, INTR_TRANSMIT, &data);
	bus_write_1(sc->smb_res, GLXIIC_SMB_SDA, data);

	glxiic_start_timeout_locked(sc);

	return (IIC_NOERR);
}

static int
glxiic_state_slave_rx_callback(struct glxiic_softc *sc, uint8_t status)
{
	uint8_t data;

	GLXIIC_ASSERT_LOCKED(sc);

	if ((status & GLXIIC_SMB_STS_BER_BIT) != 0) {
		GLXIIC_DEBUG_LOG("bus error in slave rx");
		return (IIC_EBUSERR);
	}

	if ((status & GLXIIC_SMB_STS_SLVSTP_BIT) != 0) {
		iicbus_intr(sc->iicbus, INTR_STOP, NULL);
		glxiic_set_state_locked(sc, GLXIIC_STATE_IDLE);
		return (IIC_NOERR);
	}

	if ((status & GLXIIC_SMB_STS_NMATCH_BIT) != 0) {
		/* Handle repeated start in slave mode. */
		return (glxiic_handle_slave_match_locked(sc, status));
	}

	if ((status & GLXIIC_SMB_STS_SDAST_BIT) == 0) {
		GLXIIC_DEBUG_LOG("no pending data in slave rx");
		return (IIC_ESTATUS);
	}

	data = bus_read_1(sc->smb_res, GLXIIC_SMB_SDA);
	iicbus_intr(sc->iicbus, INTR_RECEIVE, &data);

	glxiic_start_timeout_locked(sc);

	return (IIC_NOERR);
}

static int
glxiic_state_master_addr_callback(struct glxiic_softc *sc, uint8_t status)
{
	uint8_t slave;
	uint8_t ctrl1;

	GLXIIC_ASSERT_LOCKED(sc);

	if ((status & GLXIIC_SMB_STS_BER_BIT) != 0) {
		GLXIIC_DEBUG_LOG("bus error after master start");
		return (IIC_EBUSERR);
	}

	if ((status & GLXIIC_SMB_STS_MASTER_BIT) == 0) {
		GLXIIC_DEBUG_LOG("not bus master after master start");
		return (IIC_ESTATUS);
	}

	if ((status & GLXIIC_SMB_STS_SDAST_BIT) == 0) {
		GLXIIC_DEBUG_LOG("not awaiting address in master addr");
		return (IIC_ESTATUS);
	}

	if ((sc->msg->flags & IIC_M_RD) != 0) {
		slave = sc->msg->slave | LSB;
		glxiic_set_state_locked(sc, GLXIIC_STATE_MASTER_RX);
	} else {
		slave = sc->msg->slave & ~LSB;
		glxiic_set_state_locked(sc, GLXIIC_STATE_MASTER_TX);
	}

	sc->data = sc->msg->buf;
	sc->ndata = sc->msg->len;

	/* Handle address-only transfer. */
	if (sc->ndata == 0)
		glxiic_set_state_locked(sc, GLXIIC_STATE_MASTER_STOP);

	bus_write_1(sc->smb_res, GLXIIC_SMB_SDA, slave);

	if ((sc->msg->flags & IIC_M_RD) != 0 && sc->ndata == 1) {
		/* Last byte from slave, set NACK. */
		ctrl1 = bus_read_1(sc->smb_res, GLXIIC_SMB_CTRL1);
		bus_write_1(sc->smb_res, GLXIIC_SMB_CTRL1,
		    ctrl1 | GLXIIC_SMB_CTRL1_ACK_BIT);
	}

	return (IIC_NOERR);
}

static int
glxiic_state_master_tx_callback(struct glxiic_softc *sc, uint8_t status)
{

	GLXIIC_ASSERT_LOCKED(sc);

	if ((status & GLXIIC_SMB_STS_BER_BIT) != 0) {
		GLXIIC_DEBUG_LOG("bus error in master tx");
		return (IIC_EBUSERR);
	}

	if ((status & GLXIIC_SMB_STS_MASTER_BIT) == 0) {
		GLXIIC_DEBUG_LOG("not bus master in master tx");
		return (IIC_ESTATUS);
	}

	if ((status & GLXIIC_SMB_STS_NEGACK_BIT) != 0) {
		GLXIIC_DEBUG_LOG("slave nack in master tx");
		return (IIC_ENOACK);
	}

	if ((status & GLXIIC_SMB_STS_STASTR_BIT) != 0) {
		bus_write_1(sc->smb_res, GLXIIC_SMB_STS,
		    GLXIIC_SMB_STS_STASTR_BIT);
	}

	if ((status & GLXIIC_SMB_STS_SDAST_BIT) == 0) {
		GLXIIC_DEBUG_LOG("not awaiting data in master tx");
		return (IIC_ESTATUS);
	}

	bus_write_1(sc->smb_res, GLXIIC_SMB_SDA, *sc->data++);
	if (--sc->ndata == 0)
		glxiic_set_state_locked(sc, GLXIIC_STATE_MASTER_STOP);
	else
		glxiic_start_timeout_locked(sc);

	return (IIC_NOERR);
}

static int
glxiic_state_master_rx_callback(struct glxiic_softc *sc, uint8_t status)
{
	uint8_t ctrl1;

	GLXIIC_ASSERT_LOCKED(sc);

	if ((status & GLXIIC_SMB_STS_BER_BIT) != 0) {
		GLXIIC_DEBUG_LOG("bus error in master rx");
		return (IIC_EBUSERR);
	}

	if ((status & GLXIIC_SMB_STS_MASTER_BIT) == 0) {
		GLXIIC_DEBUG_LOG("not bus master in master rx");
		return (IIC_ESTATUS);
	}

	if ((status & GLXIIC_SMB_STS_NEGACK_BIT) != 0) {
		GLXIIC_DEBUG_LOG("slave nack in rx");
		return (IIC_ENOACK);
	}

	if ((status & GLXIIC_SMB_STS_STASTR_BIT) != 0) {
		/* Bus is stalled, clear and wait for data. */
		bus_write_1(sc->smb_res, GLXIIC_SMB_STS,
		    GLXIIC_SMB_STS_STASTR_BIT);
		return (IIC_NOERR);
	}

	if ((status & GLXIIC_SMB_STS_SDAST_BIT) == 0) {
		GLXIIC_DEBUG_LOG("no pending data in master rx");
		return (IIC_ESTATUS);
	}

	*sc->data++ = bus_read_1(sc->smb_res, GLXIIC_SMB_SDA);
	if (--sc->ndata == 0) {
		/* Proceed with stop on reading last byte. */
		glxiic_set_state_locked(sc, GLXIIC_STATE_MASTER_STOP);
		return (glxiic_state_table[sc->state].callback(sc, status));
	}

	if (sc->ndata == 1) {
		/* Last byte from slave, set NACK. */
		ctrl1 = bus_read_1(sc->smb_res, GLXIIC_SMB_CTRL1);
		bus_write_1(sc->smb_res, GLXIIC_SMB_CTRL1,
		    ctrl1 | GLXIIC_SMB_CTRL1_ACK_BIT);
	}

	glxiic_start_timeout_locked(sc);

	return (IIC_NOERR);
}

static int
glxiic_state_master_stop_callback(struct glxiic_softc *sc, uint8_t status)
{
	uint8_t ctrl1;

	GLXIIC_ASSERT_LOCKED(sc);

	if ((status & GLXIIC_SMB_STS_BER_BIT) != 0) {
		GLXIIC_DEBUG_LOG("bus error in master stop");
		return (IIC_EBUSERR);
	}

	if ((status & GLXIIC_SMB_STS_MASTER_BIT) == 0) {
		GLXIIC_DEBUG_LOG("not bus master in master stop");
		return (IIC_ESTATUS);
	}

	if ((status & GLXIIC_SMB_STS_NEGACK_BIT) != 0) {
		GLXIIC_DEBUG_LOG("slave nack in master stop");
		return (IIC_ENOACK);
	}

	if (--sc->nmsgs > 0) {
		/* Start transfer of next message. */
		if ((sc->msg->flags & IIC_M_NOSTOP) == 0) {
			glxiic_stop_locked(sc);
		}

		ctrl1 = bus_read_1(sc->smb_res, GLXIIC_SMB_CTRL1);
		bus_write_1(sc->smb_res, GLXIIC_SMB_CTRL1,
		    ctrl1 | GLXIIC_SMB_CTRL1_START_BIT);

		glxiic_set_state_locked(sc, GLXIIC_STATE_MASTER_ADDR);
		sc->msg++;
	} else {
		/* Last message. */
		glxiic_stop_locked(sc);
		glxiic_set_state_locked(sc, GLXIIC_STATE_IDLE);
		sc->error = IIC_NOERR;
		GLXIIC_WAKEUP(sc);
	}

	return (IIC_NOERR);
}

static void
glxiic_intr(void *arg)
{
	struct glxiic_softc *sc;
	int error;
	uint8_t status, data;

	sc = (struct glxiic_softc *)arg;

	GLXIIC_LOCK(sc);

	status = glxiic_read_status_locked(sc);

	/* Check if this interrupt originated from the SMBus. */
	if ((status &
		~(GLXIIC_SMB_STS_MASTER_BIT | GLXIIC_SMB_STS_XMIT_BIT)) != 0) {

		error = glxiic_state_table[sc->state].callback(sc, status);

		if (error != IIC_NOERR) {
			if (glxiic_state_table[sc->state].master) {
				glxiic_stop_locked(sc);
				glxiic_set_state_locked(sc, GLXIIC_STATE_IDLE);
				sc->error = error;
				GLXIIC_WAKEUP(sc);
			} else {
				data = error & 0xff;
				iicbus_intr(sc->iicbus, INTR_ERROR, &data);
				glxiic_set_state_locked(sc, GLXIIC_STATE_IDLE);
			}
		}
	}

	GLXIIC_UNLOCK(sc);
}

static int
glxiic_reset(device_t dev, u_char speed, u_char addr, u_char *oldaddr)
{
	struct glxiic_softc *sc;

	sc = device_get_softc(dev);

	GLXIIC_LOCK(sc);

	if (oldaddr != NULL)
		*oldaddr = sc->addr;
	sc->addr = addr;

	/* A disable/enable cycle resets the controller. */
	glxiic_smb_disable(sc);
	glxiic_smb_enable(sc, speed, addr);

	if (glxiic_state_table[sc->state].master) {
		sc->error = IIC_ESTATUS;
		GLXIIC_WAKEUP(sc);
	}
	glxiic_set_state_locked(sc, GLXIIC_STATE_IDLE);

	GLXIIC_UNLOCK(sc);

	return (IIC_NOERR);
}

static int
glxiic_transfer(device_t dev, struct iic_msg *msgs, uint32_t nmsgs)
{
	struct glxiic_softc *sc;
	int error;
	uint8_t ctrl1;

	sc = device_get_softc(dev);

	GLXIIC_LOCK(sc);

	if (sc->state != GLXIIC_STATE_IDLE) {
		error = IIC_EBUSBSY;
		goto out;
	}

	sc->msg = msgs;
	sc->nmsgs = nmsgs;
	glxiic_set_state_locked(sc, GLXIIC_STATE_MASTER_ADDR);

	/* Set start bit and let glxiic_intr() handle the transfer. */
	ctrl1 = bus_read_1(sc->smb_res, GLXIIC_SMB_CTRL1);
	bus_write_1(sc->smb_res, GLXIIC_SMB_CTRL1,
	    ctrl1 | GLXIIC_SMB_CTRL1_START_BIT);

	GLXIIC_SLEEP(sc);
	error = sc->error;
out:
	GLXIIC_UNLOCK(sc);

	return (error);
}

static void
glxiic_smb_map_interrupt(int irq)
{
	uint32_t irq_map;
	int old_irq;

	/* Protect the read-modify-write operation. */
	critical_enter();

	irq_map = rdmsr(GLXIIC_MSR_PIC_YSEL_HIGH);
	old_irq = GLXIIC_MAP_TO_SMB_IRQ(irq_map);

	if (irq != old_irq) {
		irq_map &= ~GLXIIC_SMB_IRQ_TO_MAP(old_irq);
		irq_map |= GLXIIC_SMB_IRQ_TO_MAP(irq);
		wrmsr(GLXIIC_MSR_PIC_YSEL_HIGH, irq_map);
	}

	critical_exit();
}

static void
glxiic_gpio_enable(struct glxiic_softc *sc)
{

	bus_write_4(sc->gpio_res, GLXIIC_GPIOL_IN_AUX1_SEL,
	    GLXIIC_GPIO_14_15_ENABLE);
	bus_write_4(sc->gpio_res, GLXIIC_GPIOL_OUT_AUX1_SEL,
	    GLXIIC_GPIO_14_15_ENABLE);
}

static void
glxiic_gpio_disable(struct glxiic_softc *sc)
{

	bus_write_4(sc->gpio_res, GLXIIC_GPIOL_OUT_AUX1_SEL,
	    GLXIIC_GPIO_14_15_DISABLE);
	bus_write_4(sc->gpio_res, GLXIIC_GPIOL_IN_AUX1_SEL,
	    GLXIIC_GPIO_14_15_DISABLE);
}

static void
glxiic_smb_enable(struct glxiic_softc *sc, uint8_t speed, uint8_t addr)
{
	uint8_t ctrl1;

	ctrl1 = 0;

	switch (speed) {
	case IIC_SLOW:
		sc->sclfrq = GLXIIC_SLOW;
		break;
	case IIC_FAST:
		sc->sclfrq = GLXIIC_FAST;
		break;
	case IIC_FASTEST:
		sc->sclfrq = GLXIIC_FASTEST;
		break;
	case IIC_UNKNOWN:
	default:
		/* Reuse last frequency. */
		break;
	}

	/* Set bus speed and enable controller. */
	bus_write_2(sc->smb_res, GLXIIC_SMB_CTRL2,
	    GLXIIC_SCLFRQ(sc->sclfrq) | GLXIIC_SMB_CTRL2_EN_BIT);

	if (addr != 0) {
		/* Enable new match and global call match interrupts. */
		ctrl1 |= GLXIIC_SMB_CTRL1_NMINTE_BIT |
			GLXIIC_SMB_CTRL1_GCMEN_BIT;
		bus_write_1(sc->smb_res, GLXIIC_SMB_ADDR,
		    GLXIIC_SMB_ADDR_SAEN_BIT | GLXIIC_SMBADDR(addr));
	} else {
		bus_write_1(sc->smb_res, GLXIIC_SMB_ADDR, 0);
	}

	/* Enable stall after start and interrupt. */
	bus_write_1(sc->smb_res, GLXIIC_SMB_CTRL1,
	    ctrl1 | GLXIIC_SMB_CTRL1_STASTRE_BIT | GLXIIC_SMB_CTRL1_INTEN_BIT);
}

static void
glxiic_smb_disable(struct glxiic_softc *sc)
{
	uint16_t sclfrq;

	sclfrq = bus_read_2(sc->smb_res, GLXIIC_SMB_CTRL2);
	bus_write_2(sc->smb_res, GLXIIC_SMB_CTRL2,
	    sclfrq & ~GLXIIC_SMB_CTRL2_EN_BIT);
}
