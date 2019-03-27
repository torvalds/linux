/*-
 * Copyright (c) 1998, 1999 Takanori Watanabe
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *        notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *        notice, this list of conditions and the following disclaimer in the
 *        documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.    IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/rman.h>
#include <machine/bus.h>
#include <dev/smbus/smbconf.h>

#include "smbus_if.h"

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/intpm/intpmreg.h>
#include <dev/amdsbwd/amd_chipset.h>

#include "opt_intpm.h"

struct intsmb_softc {
	device_t		dev;
	struct resource		*io_res;
	struct resource		*irq_res;
	void			*irq_hand;
	device_t		smbus;
	int			io_rid;
	int			isbusy;
	int			cfg_irq9;
	int			sb8xx;
	int			poll;
	struct mtx		lock;
};

#define	INTSMB_LOCK(sc)		mtx_lock(&(sc)->lock)
#define	INTSMB_UNLOCK(sc)	mtx_unlock(&(sc)->lock)
#define	INTSMB_LOCK_ASSERT(sc)	mtx_assert(&(sc)->lock, MA_OWNED)

static int intsmb_probe(device_t);
static int intsmb_attach(device_t);
static int intsmb_detach(device_t);
static int intsmb_intr(struct intsmb_softc *sc);
static int intsmb_slvintr(struct intsmb_softc *sc);
static void intsmb_alrintr(struct intsmb_softc *sc);
static int intsmb_callback(device_t dev, int index, void *data);
static int intsmb_quick(device_t dev, u_char slave, int how);
static int intsmb_sendb(device_t dev, u_char slave, char byte);
static int intsmb_recvb(device_t dev, u_char slave, char *byte);
static int intsmb_writeb(device_t dev, u_char slave, char cmd, char byte);
static int intsmb_writew(device_t dev, u_char slave, char cmd, short word);
static int intsmb_readb(device_t dev, u_char slave, char cmd, char *byte);
static int intsmb_readw(device_t dev, u_char slave, char cmd, short *word);
static int intsmb_pcall(device_t dev, u_char slave, char cmd, short sdata, short *rdata);
static int intsmb_bwrite(device_t dev, u_char slave, char cmd, u_char count, char *buf);
static int intsmb_bread(device_t dev, u_char slave, char cmd, u_char *count, char *buf);
static void intsmb_start(struct intsmb_softc *sc, u_char cmd, int nointr);
static int intsmb_stop(struct intsmb_softc *sc);
static int intsmb_stop_poll(struct intsmb_softc *sc);
static int intsmb_free(struct intsmb_softc *sc);
static void intsmb_rawintr(void *arg);

const struct intsmb_device {
	uint32_t devid;
	const char *description;
} intsmb_products[] = {
	{ 0x71138086, "Intel PIIX4 SMBUS Interface" },
	{ 0x719b8086, "Intel PIIX4 SMBUS Interface" },
#if 0
	/* Not a good idea yet, this stops isab0 functioning */
	{ 0x02001166, "ServerWorks OSB4" },
#endif
	{ 0x43721002, "ATI IXP400 SMBus Controller" },
	{ AMDSB_SMBUS_DEVID, "AMD SB600/7xx/8xx/9xx SMBus Controller" },
	{ AMDFCH_SMBUS_DEVID, "AMD FCH SMBus Controller" },
	{ AMDCZ_SMBUS_DEVID, "AMD FCH SMBus Controller" },
};

static int
intsmb_probe(device_t dev)
{
	const struct intsmb_device *isd;
	uint32_t devid;
	size_t i;

	devid = pci_get_devid(dev);
	for (i = 0; i < nitems(intsmb_products); i++) {
		isd = &intsmb_products[i];
		if (isd->devid == devid) {
			device_set_desc(dev, isd->description);
			return (BUS_PROBE_DEFAULT);
		}
	}
	return (ENXIO);
}

static uint8_t
amd_pmio_read(struct resource *res, uint8_t reg)
{
	bus_write_1(res, 0, reg);	/* Index */
	return (bus_read_1(res, 1));	/* Data */
}

static int
sb8xx_attach(device_t dev)
{
	static const int	AMDSB_SMBIO_WIDTH = 0x10;
	struct intsmb_softc	*sc;
	struct resource		*res;
	uint32_t		devid;
	uint8_t			revid;
	uint16_t		addr;
	int			rid;
	int			rc;
	bool			enabled;

	sc = device_get_softc(dev);
	rid = 0;
	rc = bus_set_resource(dev, SYS_RES_IOPORT, rid, AMDSB_PMIO_INDEX,
	    AMDSB_PMIO_WIDTH);
	if (rc != 0) {
		device_printf(dev, "bus_set_resource for PM IO failed\n");
		return (ENXIO);
	}
	res = bus_alloc_resource_any(dev, SYS_RES_IOPORT, &rid,
	    RF_ACTIVE);
	if (res == NULL) {
		device_printf(dev, "bus_alloc_resource for PM IO failed\n");
		return (ENXIO);
	}

	devid = pci_get_devid(dev);
	revid = pci_get_revid(dev);
	if (devid == AMDSB_SMBUS_DEVID ||
	    (devid == AMDFCH_SMBUS_DEVID && revid < AMDFCH41_SMBUS_REVID) ||
	    (devid == AMDCZ_SMBUS_DEVID  && revid < AMDCZ49_SMBUS_REVID)) {
		addr = amd_pmio_read(res, AMDSB8_PM_SMBUS_EN + 1);
		addr <<= 8;
		addr |= amd_pmio_read(res, AMDSB8_PM_SMBUS_EN);
		enabled = (addr & AMDSB8_SMBUS_EN) != 0;
		addr &= AMDSB8_SMBUS_ADDR_MASK;
	} else {
		addr = amd_pmio_read(res, AMDFCH41_PM_DECODE_EN0);
		enabled = (addr & AMDFCH41_SMBUS_EN) != 0;
		addr = amd_pmio_read(res, AMDFCH41_PM_DECODE_EN1);
		addr <<= 8;
	}

	bus_release_resource(dev, SYS_RES_IOPORT, rid, res);
	bus_delete_resource(dev, SYS_RES_IOPORT, rid);

	if (!enabled) {
		device_printf(dev, "SB8xx/SB9xx/FCH SMBus not enabled\n");
		return (ENXIO);
	}

	sc->io_rid = 0;
	rc = bus_set_resource(dev, SYS_RES_IOPORT, sc->io_rid, addr,
	    AMDSB_SMBIO_WIDTH);
	if (rc != 0) {
		device_printf(dev, "bus_set_resource for SMBus IO failed\n");
		return (ENXIO);
	}
	sc->io_res = bus_alloc_resource_any(dev, SYS_RES_IOPORT, &sc->io_rid,
	    RF_ACTIVE);
	if (sc->io_res == NULL) {
		device_printf(dev, "Could not allocate I/O space\n");
		return (ENXIO);
	}
	sc->poll = 1;
	return (0);
}

static void
intsmb_release_resources(device_t dev)
{
	struct intsmb_softc *sc = device_get_softc(dev);

	if (sc->smbus)
		device_delete_child(dev, sc->smbus);
	if (sc->irq_hand)
		bus_teardown_intr(dev, sc->irq_res, sc->irq_hand);
	if (sc->irq_res)
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->irq_res);
	if (sc->io_res)
		bus_release_resource(dev, SYS_RES_IOPORT, sc->io_rid,
		    sc->io_res);
	mtx_destroy(&sc->lock);
}

static int
intsmb_attach(device_t dev)
{
	struct intsmb_softc *sc = device_get_softc(dev);
	int error, rid, value;
	int intr;
	char *str;

	sc->dev = dev;

	mtx_init(&sc->lock, device_get_nameunit(dev), "intsmb", MTX_DEF);

	sc->cfg_irq9 = 0;
	switch (pci_get_devid(dev)) {
#ifndef NO_CHANGE_PCICONF
	case 0x71138086:	/* Intel 82371AB */
	case 0x719b8086:	/* Intel 82443MX */
		/* Changing configuration is allowed. */
		sc->cfg_irq9 = 1;
		break;
#endif
	case AMDSB_SMBUS_DEVID:
		if (pci_get_revid(dev) >= AMDSB8_SMBUS_REVID)
			sc->sb8xx = 1;
		break;
	case AMDFCH_SMBUS_DEVID:
	case AMDCZ_SMBUS_DEVID:
		sc->sb8xx = 1;
		break;
	}

	if (sc->sb8xx) {
		error = sb8xx_attach(dev);
		if (error != 0)
			goto fail;
		else
			goto no_intr;
	}

	sc->io_rid = PCI_BASE_ADDR_SMB;
	sc->io_res = bus_alloc_resource_any(dev, SYS_RES_IOPORT, &sc->io_rid,
	    RF_ACTIVE);
	if (sc->io_res == NULL) {
		device_printf(dev, "Could not allocate I/O space\n");
		error = ENXIO;
		goto fail;
	}

	if (sc->cfg_irq9) {
		pci_write_config(dev, PCIR_INTLINE, 0x9, 1);
		pci_write_config(dev, PCI_HST_CFG_SMB,
		    PCI_INTR_SMB_IRQ9 | PCI_INTR_SMB_ENABLE, 1);
	}
	value = pci_read_config(dev, PCI_HST_CFG_SMB, 1);
	sc->poll = (value & PCI_INTR_SMB_ENABLE) == 0;
	intr = value & PCI_INTR_SMB_MASK;
	switch (intr) {
	case PCI_INTR_SMB_SMI:
		str = "SMI";
		break;
	case PCI_INTR_SMB_IRQ9:
		str = "IRQ 9";
		break;
	case PCI_INTR_SMB_IRQ_PCI:
		str = "PCI IRQ";
		break;
	default:
		str = "BOGUS";
	}

	device_printf(dev, "intr %s %s ", str,
	    sc->poll == 0 ? "enabled" : "disabled");
	printf("revision %d\n", pci_read_config(dev, PCI_REVID_SMB, 1));

	if (!sc->poll && intr == PCI_INTR_SMB_SMI) {
		device_printf(dev,
		    "using polling mode when configured interrupt is SMI\n");
		sc->poll = 1;
	}

	if (sc->poll)
	    goto no_intr;

	if (intr != PCI_INTR_SMB_IRQ9 && intr != PCI_INTR_SMB_IRQ_PCI) {
		device_printf(dev, "Unsupported interrupt mode\n");
		error = ENXIO;
		goto fail;
	}

	/* Force IRQ 9. */
	rid = 0;
	if (sc->cfg_irq9)
		bus_set_resource(dev, SYS_RES_IRQ, rid, 9, 1);

	sc->irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
	    RF_SHAREABLE | RF_ACTIVE);
	if (sc->irq_res == NULL) {
		device_printf(dev, "Could not allocate irq\n");
		error = ENXIO;
		goto fail;
	}

	error = bus_setup_intr(dev, sc->irq_res, INTR_TYPE_MISC | INTR_MPSAFE,
	    NULL, intsmb_rawintr, sc, &sc->irq_hand);
	if (error) {
		device_printf(dev, "Failed to map intr\n");
		goto fail;
	}

no_intr:
	sc->isbusy = 0;
	sc->smbus = device_add_child(dev, "smbus", -1);
	if (sc->smbus == NULL) {
		device_printf(dev, "failed to add smbus child\n");
		error = ENXIO;
		goto fail;
	}
	error = device_probe_and_attach(sc->smbus);
	if (error) {
		device_printf(dev, "failed to probe+attach smbus child\n");
		goto fail;
	}

#ifdef ENABLE_ALART
	/* Enable Arart */
	bus_write_1(sc->io_res, PIIX4_SMBSLVCNT, PIIX4_SMBSLVCNT_ALTEN);
#endif
	return (0);

fail:
	intsmb_release_resources(dev);
	return (error);
}

static int
intsmb_detach(device_t dev)
{
	int error;

	error = bus_generic_detach(dev);
	if (error) {
		device_printf(dev, "bus detach failed\n");
		return (error);
	}

	intsmb_release_resources(dev);
	return (0);
}

static void
intsmb_rawintr(void *arg)
{
	struct intsmb_softc *sc = arg;

	INTSMB_LOCK(sc);
	intsmb_intr(sc);
	intsmb_slvintr(sc);
	INTSMB_UNLOCK(sc);
}

static int
intsmb_callback(device_t dev, int index, void *data)
{
	int error = 0;

	switch (index) {
	case SMB_REQUEST_BUS:
		break;
	case SMB_RELEASE_BUS:
		break;
	default:
		error = SMB_EINVAL;
	}

	return (error);
}

/* Counterpart of smbtx_smb_free(). */
static int
intsmb_free(struct intsmb_softc *sc)
{

	INTSMB_LOCK_ASSERT(sc);
	if ((bus_read_1(sc->io_res, PIIX4_SMBHSTSTS) & PIIX4_SMBHSTSTAT_BUSY) ||
#ifdef ENABLE_ALART
	    (bus_read_1(sc->io_res, PIIX4_SMBSLVSTS) & PIIX4_SMBSLVSTS_BUSY) ||
#endif
	    sc->isbusy)
		return (SMB_EBUSY);

	sc->isbusy = 1;
	/* Disable Interrupt in slave part. */
#ifndef ENABLE_ALART
	bus_write_1(sc->io_res, PIIX4_SMBSLVCNT, 0);
#endif
	/* Reset INTR Flag to prepare INTR. */
	bus_write_1(sc->io_res, PIIX4_SMBHSTSTS,
	    PIIX4_SMBHSTSTAT_INTR | PIIX4_SMBHSTSTAT_ERR |
	    PIIX4_SMBHSTSTAT_BUSC | PIIX4_SMBHSTSTAT_FAIL);
	return (0);
}

static int
intsmb_intr(struct intsmb_softc *sc)
{
	int status, tmp;

	status = bus_read_1(sc->io_res, PIIX4_SMBHSTSTS);
	if (status & PIIX4_SMBHSTSTAT_BUSY)
		return (1);

	if (status & (PIIX4_SMBHSTSTAT_INTR | PIIX4_SMBHSTSTAT_ERR |
	    PIIX4_SMBHSTSTAT_BUSC | PIIX4_SMBHSTSTAT_FAIL)) {

		tmp = bus_read_1(sc->io_res, PIIX4_SMBHSTCNT);
		bus_write_1(sc->io_res, PIIX4_SMBHSTCNT,
		    tmp & ~PIIX4_SMBHSTCNT_INTREN);
		if (sc->isbusy) {
			sc->isbusy = 0;
			wakeup(sc);
		}
		return (0);
	}
	return (1); /* Not Completed */
}

static int
intsmb_slvintr(struct intsmb_softc *sc)
{
	int status;

	status = bus_read_1(sc->io_res, PIIX4_SMBSLVSTS);
	if (status & PIIX4_SMBSLVSTS_BUSY)
		return (1);
	if (status & PIIX4_SMBSLVSTS_ALART)
		intsmb_alrintr(sc);
	else if (status & ~(PIIX4_SMBSLVSTS_ALART | PIIX4_SMBSLVSTS_SDW2
		| PIIX4_SMBSLVSTS_SDW1)) {
	}

	/* Reset Status Register */
	bus_write_1(sc->io_res, PIIX4_SMBSLVSTS,
	    PIIX4_SMBSLVSTS_ALART | PIIX4_SMBSLVSTS_SDW2 |
	    PIIX4_SMBSLVSTS_SDW1 | PIIX4_SMBSLVSTS_SLV);
	return (0);
}

static void
intsmb_alrintr(struct intsmb_softc *sc)
{
	int slvcnt;
#ifdef ENABLE_ALART
	int error;
	uint8_t addr;
#endif

	/* Stop generating INTR from ALART. */
	slvcnt = bus_read_1(sc->io_res, PIIX4_SMBSLVCNT);
#ifdef ENABLE_ALART
	bus_write_1(sc->io_res, PIIX4_SMBSLVCNT,
	    slvcnt & ~PIIX4_SMBSLVCNT_ALTEN);
#endif
	DELAY(5);

	/* Ask bus who asserted it and then ask it what's the matter. */
#ifdef ENABLE_ALART
	error = intsmb_free(sc);
	if (error)
		return;

	bus_write_1(sc->io_res, PIIX4_SMBHSTADD, SMBALTRESP | LSB);
	intsmb_start(sc, PIIX4_SMBHSTCNT_PROT_BYTE, 1);
	error = intsmb_stop_poll(sc);
	if (error)
		device_printf(sc->dev, "ALART: ERROR\n");
	else {
		addr = bus_read_1(sc->io_res, PIIX4_SMBHSTDAT0);
		device_printf(sc->dev, "ALART_RESPONSE: 0x%x\n", addr);
	}

	/* Re-enable INTR from ALART. */
	bus_write_1(sc->io_res, PIIX4_SMBSLVCNT,
	    slvcnt | PIIX4_SMBSLVCNT_ALTEN);
	DELAY(5);
#endif
}

static void
intsmb_start(struct intsmb_softc *sc, unsigned char cmd, int nointr)
{
	unsigned char tmp;

	INTSMB_LOCK_ASSERT(sc);
	tmp = bus_read_1(sc->io_res, PIIX4_SMBHSTCNT);
	tmp &= 0xe0;
	tmp |= cmd;
	tmp |= PIIX4_SMBHSTCNT_START;

	/* While not in autoconfiguration enable interrupts. */
	if (!sc->poll && !cold && !nointr)
		tmp |= PIIX4_SMBHSTCNT_INTREN;
	bus_write_1(sc->io_res, PIIX4_SMBHSTCNT, tmp);
}

static int
intsmb_error(device_t dev, int status)
{
	int error = 0;

	/*
	 * PIIX4_SMBHSTSTAT_ERR can mean either of
	 * - SMB_ENOACK ("Unclaimed cycle"),
	 * - SMB_ETIMEOUT ("Host device time-out"),
	 * - SMB_EINVAL ("Illegal command field").
	 * SMB_ENOACK seems to be most typical.
	 */
	if (status & PIIX4_SMBHSTSTAT_ERR)
		error |= SMB_ENOACK;
	if (status & PIIX4_SMBHSTSTAT_BUSC)
		error |= SMB_ECOLLI;
	if (status & PIIX4_SMBHSTSTAT_FAIL)
		error |= SMB_EABORT;

	if (error != 0 && bootverbose)
		device_printf(dev, "error = %d, status = %#x\n", error, status);

	return (error);
}

/*
 * Polling Code.
 *
 * Polling is not encouraged because it requires waiting for the
 * device if it is busy.
 * (29063505.pdf from Intel) But during boot, interrupt cannot be used, so use
 * polling code then.
 */
static int
intsmb_stop_poll(struct intsmb_softc *sc)
{
	int error, i, status, tmp;

	INTSMB_LOCK_ASSERT(sc);

	/* First, wait for busy to be set. */
	for (i = 0; i < 0x7fff; i++)
		if (bus_read_1(sc->io_res, PIIX4_SMBHSTSTS) &
		    PIIX4_SMBHSTSTAT_BUSY)
			break;

	/* Wait for busy to clear. */
	for (i = 0; i < 0x7fff; i++) {
		status = bus_read_1(sc->io_res, PIIX4_SMBHSTSTS);
		if (!(status & PIIX4_SMBHSTSTAT_BUSY)) {
			sc->isbusy = 0;
			error = intsmb_error(sc->dev, status);
			return (error);
		}
	}

	/* Timed out waiting for busy to clear. */
	sc->isbusy = 0;
	tmp = bus_read_1(sc->io_res, PIIX4_SMBHSTCNT);
	bus_write_1(sc->io_res, PIIX4_SMBHSTCNT, tmp & ~PIIX4_SMBHSTCNT_INTREN);
	return (SMB_ETIMEOUT);
}

/*
 * Wait for completion and return result.
 */
static int
intsmb_stop(struct intsmb_softc *sc)
{
	int error, status;

	INTSMB_LOCK_ASSERT(sc);

	if (sc->poll || cold)
		/* So that it can use device during device probe on SMBus. */
		return (intsmb_stop_poll(sc));

	error = msleep(sc, &sc->lock, PWAIT | PCATCH, "SMBWAI", hz / 8);
	if (error == 0) {
		status = bus_read_1(sc->io_res, PIIX4_SMBHSTSTS);
		if (!(status & PIIX4_SMBHSTSTAT_BUSY)) {
			error = intsmb_error(sc->dev, status);
			if (error == 0 && !(status & PIIX4_SMBHSTSTAT_INTR))
				device_printf(sc->dev, "unknown cause why?\n");
#ifdef ENABLE_ALART
			bus_write_1(sc->io_res, PIIX4_SMBSLVCNT,
			    PIIX4_SMBSLVCNT_ALTEN);
#endif
			return (error);
		}
	}

	/* Timeout Procedure. */
	sc->isbusy = 0;

	/* Re-enable suppressed interrupt from slave part. */
	bus_write_1(sc->io_res, PIIX4_SMBSLVCNT, PIIX4_SMBSLVCNT_ALTEN);
	if (error == EWOULDBLOCK)
		return (SMB_ETIMEOUT);
	else
		return (SMB_EABORT);
}

static int
intsmb_quick(device_t dev, u_char slave, int how)
{
	struct intsmb_softc *sc = device_get_softc(dev);
	int error;
	u_char data;

	data = slave;

	/* Quick command is part of Address, I think. */
	switch(how) {
	case SMB_QWRITE:
		data &= ~LSB;
		break;
	case SMB_QREAD:
		data |= LSB;
		break;
	default:
		return (SMB_EINVAL);
	}

	INTSMB_LOCK(sc);
	error = intsmb_free(sc);
	if (error) {
		INTSMB_UNLOCK(sc);
		return (error);
	}
	bus_write_1(sc->io_res, PIIX4_SMBHSTADD, data);
	intsmb_start(sc, PIIX4_SMBHSTCNT_PROT_QUICK, 0);
	error = intsmb_stop(sc);
	INTSMB_UNLOCK(sc);
	return (error);
}

static int
intsmb_sendb(device_t dev, u_char slave, char byte)
{
	struct intsmb_softc *sc = device_get_softc(dev);
	int error;

	INTSMB_LOCK(sc);
	error = intsmb_free(sc);
	if (error) {
		INTSMB_UNLOCK(sc);
		return (error);
	}
	bus_write_1(sc->io_res, PIIX4_SMBHSTADD, slave & ~LSB);
	bus_write_1(sc->io_res, PIIX4_SMBHSTCMD, byte);
	intsmb_start(sc, PIIX4_SMBHSTCNT_PROT_BYTE, 0);
	error = intsmb_stop(sc);
	INTSMB_UNLOCK(sc);
	return (error);
}

static int
intsmb_recvb(device_t dev, u_char slave, char *byte)
{
	struct intsmb_softc *sc = device_get_softc(dev);
	int error;

	INTSMB_LOCK(sc);
	error = intsmb_free(sc);
	if (error) {
		INTSMB_UNLOCK(sc);
		return (error);
	}
	bus_write_1(sc->io_res, PIIX4_SMBHSTADD, slave | LSB);
	intsmb_start(sc, PIIX4_SMBHSTCNT_PROT_BYTE, 0);
	error = intsmb_stop(sc);
	if (error == 0) {
#ifdef RECV_IS_IN_CMD
		/*
		 * Linux SMBus stuff also troubles
		 * Because Intel's datasheet does not make clear.
		 */
		*byte = bus_read_1(sc->io_res, PIIX4_SMBHSTCMD);
#else
		*byte = bus_read_1(sc->io_res, PIIX4_SMBHSTDAT0);
#endif
	}
	INTSMB_UNLOCK(sc);
	return (error);
}

static int
intsmb_writeb(device_t dev, u_char slave, char cmd, char byte)
{
	struct intsmb_softc *sc = device_get_softc(dev);
	int error;

	INTSMB_LOCK(sc);
	error = intsmb_free(sc);
	if (error) {
		INTSMB_UNLOCK(sc);
		return (error);
	}
	bus_write_1(sc->io_res, PIIX4_SMBHSTADD, slave & ~LSB);
	bus_write_1(sc->io_res, PIIX4_SMBHSTCMD, cmd);
	bus_write_1(sc->io_res, PIIX4_SMBHSTDAT0, byte);
	intsmb_start(sc, PIIX4_SMBHSTCNT_PROT_BDATA, 0);
	error = intsmb_stop(sc);
	INTSMB_UNLOCK(sc);
	return (error);
}

static int
intsmb_writew(device_t dev, u_char slave, char cmd, short word)
{
	struct intsmb_softc *sc = device_get_softc(dev);
	int error;

	INTSMB_LOCK(sc);
	error = intsmb_free(sc);
	if (error) {
		INTSMB_UNLOCK(sc);
		return (error);
	}
	bus_write_1(sc->io_res, PIIX4_SMBHSTADD, slave & ~LSB);
	bus_write_1(sc->io_res, PIIX4_SMBHSTCMD, cmd);
	bus_write_1(sc->io_res, PIIX4_SMBHSTDAT0, word & 0xff);
	bus_write_1(sc->io_res, PIIX4_SMBHSTDAT1, (word >> 8) & 0xff);
	intsmb_start(sc, PIIX4_SMBHSTCNT_PROT_WDATA, 0);
	error = intsmb_stop(sc);
	INTSMB_UNLOCK(sc);
	return (error);
}

static int
intsmb_readb(device_t dev, u_char slave, char cmd, char *byte)
{
	struct intsmb_softc *sc = device_get_softc(dev);
	int error;

	INTSMB_LOCK(sc);
	error = intsmb_free(sc);
	if (error) {
		INTSMB_UNLOCK(sc);
		return (error);
	}
	bus_write_1(sc->io_res, PIIX4_SMBHSTADD, slave | LSB);
	bus_write_1(sc->io_res, PIIX4_SMBHSTCMD, cmd);
	intsmb_start(sc, PIIX4_SMBHSTCNT_PROT_BDATA, 0);
	error = intsmb_stop(sc);
	if (error == 0)
		*byte = bus_read_1(sc->io_res, PIIX4_SMBHSTDAT0);
	INTSMB_UNLOCK(sc);
	return (error);
}

static int
intsmb_readw(device_t dev, u_char slave, char cmd, short *word)
{
	struct intsmb_softc *sc = device_get_softc(dev);
	int error;

	INTSMB_LOCK(sc);
	error = intsmb_free(sc);
	if (error) {
		INTSMB_UNLOCK(sc);
		return (error);
	}
	bus_write_1(sc->io_res, PIIX4_SMBHSTADD, slave | LSB);
	bus_write_1(sc->io_res, PIIX4_SMBHSTCMD, cmd);
	intsmb_start(sc, PIIX4_SMBHSTCNT_PROT_WDATA, 0);
	error = intsmb_stop(sc);
	if (error == 0) {
		*word = bus_read_1(sc->io_res, PIIX4_SMBHSTDAT0);
		*word |= bus_read_1(sc->io_res, PIIX4_SMBHSTDAT1) << 8;
	}
	INTSMB_UNLOCK(sc);
	return (error);
}

static int
intsmb_pcall(device_t dev, u_char slave, char cmd, short sdata, short *rdata)
{

	return (SMB_ENOTSUPP);
}

static int
intsmb_bwrite(device_t dev, u_char slave, char cmd, u_char count, char *buf)
{
	struct intsmb_softc *sc = device_get_softc(dev);
	int error, i;

	if (count > SMBBLOCKTRANS_MAX || count == 0)
		return (SMB_EINVAL);

	INTSMB_LOCK(sc);
	error = intsmb_free(sc);
	if (error) {
		INTSMB_UNLOCK(sc);
		return (error);
	}

	/* Reset internal array index. */
	bus_read_1(sc->io_res, PIIX4_SMBHSTCNT);

	bus_write_1(sc->io_res, PIIX4_SMBHSTADD, slave & ~LSB);
	bus_write_1(sc->io_res, PIIX4_SMBHSTCMD, cmd);
	for (i = 0; i < count; i++)
		bus_write_1(sc->io_res, PIIX4_SMBBLKDAT, buf[i]);
	bus_write_1(sc->io_res, PIIX4_SMBHSTDAT0, count);
	intsmb_start(sc, PIIX4_SMBHSTCNT_PROT_BLOCK, 0);
	error = intsmb_stop(sc);
	INTSMB_UNLOCK(sc);
	return (error);
}

static int
intsmb_bread(device_t dev, u_char slave, char cmd, u_char *count, char *buf)
{
	struct intsmb_softc *sc = device_get_softc(dev);
	int error, i;
	u_char data, nread;

	INTSMB_LOCK(sc);
	error = intsmb_free(sc);
	if (error) {
		INTSMB_UNLOCK(sc);
		return (error);
	}

	/* Reset internal array index. */
	bus_read_1(sc->io_res, PIIX4_SMBHSTCNT);

	bus_write_1(sc->io_res, PIIX4_SMBHSTADD, slave | LSB);
	bus_write_1(sc->io_res, PIIX4_SMBHSTCMD, cmd);
	intsmb_start(sc, PIIX4_SMBHSTCNT_PROT_BLOCK, 0);
	error = intsmb_stop(sc);
	if (error == 0) {
		nread = bus_read_1(sc->io_res, PIIX4_SMBHSTDAT0);
		if (nread != 0 && nread <= SMBBLOCKTRANS_MAX) {
			*count = nread;
			for (i = 0; i < nread; i++)
				data = bus_read_1(sc->io_res, PIIX4_SMBBLKDAT);
		} else
			error = SMB_EBUSERR;
	}
	INTSMB_UNLOCK(sc);
	return (error);
}

static devclass_t intsmb_devclass;

static device_method_t intsmb_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		intsmb_probe),
	DEVMETHOD(device_attach,	intsmb_attach),
	DEVMETHOD(device_detach,	intsmb_detach),

	/* SMBus interface */
	DEVMETHOD(smbus_callback,	intsmb_callback),
	DEVMETHOD(smbus_quick,		intsmb_quick),
	DEVMETHOD(smbus_sendb,		intsmb_sendb),
	DEVMETHOD(smbus_recvb,		intsmb_recvb),
	DEVMETHOD(smbus_writeb,		intsmb_writeb),
	DEVMETHOD(smbus_writew,		intsmb_writew),
	DEVMETHOD(smbus_readb,		intsmb_readb),
	DEVMETHOD(smbus_readw,		intsmb_readw),
	DEVMETHOD(smbus_pcall,		intsmb_pcall),
	DEVMETHOD(smbus_bwrite,		intsmb_bwrite),
	DEVMETHOD(smbus_bread,		intsmb_bread),

	DEVMETHOD_END
};

static driver_t intsmb_driver = {
	"intsmb",
	intsmb_methods,
	sizeof(struct intsmb_softc),
};

DRIVER_MODULE_ORDERED(intsmb, pci, intsmb_driver, intsmb_devclass, 0, 0,
    SI_ORDER_ANY);
DRIVER_MODULE(smbus, intsmb, smbus_driver, smbus_devclass, 0, 0);
MODULE_DEPEND(intsmb, smbus, SMBUS_MINVER, SMBUS_PREFVER, SMBUS_MAXVER);
MODULE_VERSION(intsmb, 1);
MODULE_PNP_INFO("W32:vendor/device;D:#", pci, intpm, intsmb_products,
    nitems(intsmb_products));
