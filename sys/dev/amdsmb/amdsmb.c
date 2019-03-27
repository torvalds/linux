/*-
 * Copyright (c) 2005 Ruslan Ermilov
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/systm.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

#include <dev/smbus/smbconf.h>
#include "smbus_if.h"

#define	AMDSMB_DEBUG(x)	if (amdsmb_debug) (x)

#ifdef DEBUG
static int amdsmb_debug = 1;
#else
static int amdsmb_debug = 0;
#endif

#define	AMDSMB_VENDORID_AMD		0x1022
#define	AMDSMB_DEVICEID_AMD8111_SMB2	0x746a

/*
 * ACPI 3.0, Chapter 12, Embedded Controller Interface.
 */
#define	EC_DATA		0x00	/* data register */
#define	EC_SC		0x04	/* status of controller */
#define	EC_CMD		0x04	/* command register */

#define	EC_SC_IBF	0x02	/* data ready for embedded controller */
#define	EC_SC_OBF	0x01	/* data ready for host */
#define	EC_CMD_WR	0x81	/* write EC */
#define	EC_CMD_RD	0x80	/* read EC */

/*
 * ACPI 3.0, Chapter 12, SMBus Host Controller Interface.
 */
#define	SMB_PRTCL	0x00	/* protocol */
#define	SMB_STS		0x01	/* status */
#define	SMB_ADDR	0x02	/* address */
#define	SMB_CMD		0x03	/* command */
#define	SMB_DATA	0x04	/* 32 data registers */
#define	SMB_BCNT	0x24	/* number of data bytes */
#define	SMB_ALRM_A	0x25	/* alarm address */
#define	SMB_ALRM_D	0x26	/* 2 bytes alarm data */

#define	SMB_STS_DONE	0x80
#define	SMB_STS_ALRM	0x40
#define	SMB_STS_RES	0x20
#define	SMB_STS_STATUS	0x1f
#define	SMB_STS_OK	0x00	/* OK */
#define	SMB_STS_UF	0x07	/* Unknown Failure */
#define	SMB_STS_DANA	0x10	/* Device Address Not Acknowledged */
#define	SMB_STS_DED	0x11	/* Device Error Detected */
#define	SMB_STS_DCAD	0x12	/* Device Command Access Denied */
#define	SMB_STS_UE	0x13	/* Unknown Error */
#define	SMB_STS_DAD	0x17	/* Device Access Denied */
#define	SMB_STS_T	0x18	/* Timeout */
#define	SMB_STS_HUP	0x19	/* Host Unsupported Protocol */
#define	SMB_STS_B	0x1a	/* Busy */
#define	SMB_STS_PEC	0x1f	/* PEC (CRC-8) Error */

#define	SMB_PRTCL_WRITE			0x00
#define	SMB_PRTCL_READ			0x01
#define	SMB_PRTCL_QUICK			0x02
#define	SMB_PRTCL_BYTE			0x04
#define	SMB_PRTCL_BYTE_DATA		0x06
#define	SMB_PRTCL_WORD_DATA		0x08
#define	SMB_PRTCL_BLOCK_DATA		0x0a
#define	SMB_PRTCL_PROC_CALL		0x0c
#define	SMB_PRTCL_BLOCK_PROC_CALL	0x0d
#define	SMB_PRTCL_PEC			0x80

struct amdsmb_softc {
	int rid;
	struct resource *res;
	device_t smbus;
	struct mtx lock;
};

#define	AMDSMB_LOCK(amdsmb)		mtx_lock(&(amdsmb)->lock)
#define	AMDSMB_UNLOCK(amdsmb)		mtx_unlock(&(amdsmb)->lock)
#define	AMDSMB_LOCK_ASSERT(amdsmb)	mtx_assert(&(amdsmb)->lock, MA_OWNED)

#define	AMDSMB_ECINB(amdsmb, register)					\
	(bus_read_1(amdsmb->res, register))
#define	AMDSMB_ECOUTB(amdsmb, register, value) \
	(bus_write_1(amdsmb->res, register, value))

static int	amdsmb_detach(device_t dev);

struct pci_device_table amdsmb_devs[] = {
	{ PCI_DEV(AMDSMB_VENDORID_AMD, AMDSMB_DEVICEID_AMD8111_SMB2),
	  PCI_DESCR("AMD-8111 SMBus 2.0 Controller") }
};

static int
amdsmb_probe(device_t dev)
{
	const struct pci_device_table *tbl;

	tbl = PCI_MATCH(dev, amdsmb_devs);
	if (tbl == NULL)
		return (ENXIO);
	device_set_desc(dev, tbl->descr);

	return (BUS_PROBE_DEFAULT);
}

static int
amdsmb_attach(device_t dev)
{
	struct amdsmb_softc *amdsmb_sc = device_get_softc(dev);

	/* Allocate I/O space */
	amdsmb_sc->rid = PCIR_BAR(0);
	
	amdsmb_sc->res = bus_alloc_resource_any(dev, SYS_RES_IOPORT,
		&amdsmb_sc->rid, RF_ACTIVE);

	if (amdsmb_sc->res == NULL) {
		device_printf(dev, "could not map i/o space\n");
		return (ENXIO);
	}

	mtx_init(&amdsmb_sc->lock, device_get_nameunit(dev), "amdsmb", MTX_DEF);

	/* Allocate a new smbus device */
	amdsmb_sc->smbus = device_add_child(dev, "smbus", -1);
	if (!amdsmb_sc->smbus) {
		amdsmb_detach(dev);
		return (EINVAL);
	}

	bus_generic_attach(dev);

	return (0);
}

static int
amdsmb_detach(device_t dev)
{
	struct amdsmb_softc *amdsmb_sc = device_get_softc(dev);

	if (amdsmb_sc->smbus) {
		device_delete_child(dev, amdsmb_sc->smbus);
		amdsmb_sc->smbus = NULL;
	}

	mtx_destroy(&amdsmb_sc->lock);
	if (amdsmb_sc->res)
		bus_release_resource(dev, SYS_RES_IOPORT, amdsmb_sc->rid,
		    amdsmb_sc->res);

	return (0);
}

static int
amdsmb_callback(device_t dev, int index, void *data)
{
	int error = 0;

	switch (index) {
	case SMB_REQUEST_BUS:
	case SMB_RELEASE_BUS:
		break;
	default:
		error = EINVAL;
	}

	return (error);
}

static int
amdsmb_ec_wait_write(struct amdsmb_softc *sc)
{
	int timeout = 500;

	while (timeout-- && AMDSMB_ECINB(sc, EC_SC) & EC_SC_IBF)
		DELAY(1);
	if (timeout == 0) {
		device_printf(sc->smbus, "timeout waiting for IBF to clear\n");
		return (1);
	}
	return (0);
}

static int
amdsmb_ec_wait_read(struct amdsmb_softc *sc)
{
	int timeout = 500;

	while (timeout-- && ~AMDSMB_ECINB(sc, EC_SC) & EC_SC_OBF)
		DELAY(1);
	if (timeout == 0) {
		device_printf(sc->smbus, "timeout waiting for OBF to set\n");
		return (1);
	}
	return (0);
}

static int
amdsmb_ec_read(struct amdsmb_softc *sc, u_char addr, u_char *data)
{

	AMDSMB_LOCK_ASSERT(sc);
	if (amdsmb_ec_wait_write(sc))
		return (1);
	AMDSMB_ECOUTB(sc, EC_CMD, EC_CMD_RD);

	if (amdsmb_ec_wait_write(sc))
		return (1);
	AMDSMB_ECOUTB(sc, EC_DATA, addr);

	if (amdsmb_ec_wait_read(sc))
		return (1);
	*data = AMDSMB_ECINB(sc, EC_DATA);

	return (0);
}

static int
amdsmb_ec_write(struct amdsmb_softc *sc, u_char addr, u_char data)
{

	AMDSMB_LOCK_ASSERT(sc);
	if (amdsmb_ec_wait_write(sc))
		return (1);
	AMDSMB_ECOUTB(sc, EC_CMD, EC_CMD_WR);

	if (amdsmb_ec_wait_write(sc))
		return (1);
	AMDSMB_ECOUTB(sc, EC_DATA, addr);

	if (amdsmb_ec_wait_write(sc))
		return (1);
	AMDSMB_ECOUTB(sc, EC_DATA, data);

	return (0);
}

static int
amdsmb_wait(struct amdsmb_softc *sc)
{
	u_char sts, temp;
	int error, count;

	AMDSMB_LOCK_ASSERT(sc);
	amdsmb_ec_read(sc, SMB_PRTCL, &temp);
	if (temp != 0)
	{
		count = 10000;
		do {
			DELAY(500);
			amdsmb_ec_read(sc, SMB_PRTCL, &temp);
		} while (temp != 0 && count--);
		if (count == 0)
			return (SMB_ETIMEOUT);
	}

	amdsmb_ec_read(sc, SMB_STS, &sts);
	sts &= SMB_STS_STATUS;
	AMDSMB_DEBUG(printf("amdsmb: STS=0x%x\n", sts));

	switch (sts) {
	case SMB_STS_OK:
		error = SMB_ENOERR;
		break;
	case SMB_STS_DANA:
		error = SMB_ENOACK;
		break;
	case SMB_STS_B:
		error = SMB_EBUSY;
		break;
	case SMB_STS_T:
		error = SMB_ETIMEOUT;
		break;
	case SMB_STS_DCAD:
	case SMB_STS_DAD:
	case SMB_STS_HUP:
		error = SMB_ENOTSUPP;
		break;
	default:
		error = SMB_EBUSERR;
		break;
	}

	return (error);
}

static int
amdsmb_quick(device_t dev, u_char slave, int how)
{
	struct amdsmb_softc *sc = (struct amdsmb_softc *)device_get_softc(dev);
	u_char protocol;
	int error;

	protocol = SMB_PRTCL_QUICK;

	switch (how) {
	case SMB_QWRITE:
		protocol |= SMB_PRTCL_WRITE;
		AMDSMB_DEBUG(printf("amdsmb: QWRITE to 0x%x", slave));
		break;
	case SMB_QREAD:
		protocol |= SMB_PRTCL_READ;
		AMDSMB_DEBUG(printf("amdsmb: QREAD to 0x%x", slave));
		break;
	default:
		panic("%s: unknown QUICK command (%x)!", __func__, how);
	}

	AMDSMB_LOCK(sc);
	amdsmb_ec_write(sc, SMB_ADDR, slave);
	amdsmb_ec_write(sc, SMB_PRTCL, protocol);

	error = amdsmb_wait(sc);

	AMDSMB_DEBUG(printf(", error=0x%x\n", error));
	AMDSMB_UNLOCK(sc);

	return (error);
}

static int
amdsmb_sendb(device_t dev, u_char slave, char byte)
{
	struct amdsmb_softc *sc = (struct amdsmb_softc *)device_get_softc(dev);
	int error;

	AMDSMB_LOCK(sc);
	amdsmb_ec_write(sc, SMB_CMD, byte);
	amdsmb_ec_write(sc, SMB_ADDR, slave);
	amdsmb_ec_write(sc, SMB_PRTCL, SMB_PRTCL_WRITE | SMB_PRTCL_BYTE);

	error = amdsmb_wait(sc);

	AMDSMB_DEBUG(printf("amdsmb: SENDB to 0x%x, byte=0x%x, error=0x%x\n",
	   slave, byte, error));
	AMDSMB_UNLOCK(sc);

	return (error);
}

static int
amdsmb_recvb(device_t dev, u_char slave, char *byte)
{
	struct amdsmb_softc *sc = (struct amdsmb_softc *)device_get_softc(dev);
	int error;

	AMDSMB_LOCK(sc);
	amdsmb_ec_write(sc, SMB_ADDR, slave);
	amdsmb_ec_write(sc, SMB_PRTCL, SMB_PRTCL_READ | SMB_PRTCL_BYTE);

	if ((error = amdsmb_wait(sc)) == SMB_ENOERR)
		amdsmb_ec_read(sc, SMB_DATA, byte);

	AMDSMB_DEBUG(printf("amdsmb: RECVB from 0x%x, byte=0x%x, error=0x%x\n",
	    slave, *byte, error));
	AMDSMB_UNLOCK(sc);

	return (error);
}

static int
amdsmb_writeb(device_t dev, u_char slave, char cmd, char byte)
{
	struct amdsmb_softc *sc = (struct amdsmb_softc *)device_get_softc(dev);
	int error;

	AMDSMB_LOCK(sc);
	amdsmb_ec_write(sc, SMB_CMD, cmd);
	amdsmb_ec_write(sc, SMB_DATA, byte);
	amdsmb_ec_write(sc, SMB_ADDR, slave);
	amdsmb_ec_write(sc, SMB_PRTCL, SMB_PRTCL_WRITE | SMB_PRTCL_BYTE_DATA);

	error = amdsmb_wait(sc);

	AMDSMB_DEBUG(printf("amdsmb: WRITEB to 0x%x, cmd=0x%x, byte=0x%x, "
	    "error=0x%x\n", slave, cmd, byte, error));
	AMDSMB_UNLOCK(sc);

	return (error);
}

static int
amdsmb_readb(device_t dev, u_char slave, char cmd, char *byte)
{
	struct amdsmb_softc *sc = (struct amdsmb_softc *)device_get_softc(dev);
	int error;

	AMDSMB_LOCK(sc);
	amdsmb_ec_write(sc, SMB_CMD, cmd);
	amdsmb_ec_write(sc, SMB_ADDR, slave);
	amdsmb_ec_write(sc, SMB_PRTCL, SMB_PRTCL_READ | SMB_PRTCL_BYTE_DATA);

	if ((error = amdsmb_wait(sc)) == SMB_ENOERR)
		amdsmb_ec_read(sc, SMB_DATA, byte);

	AMDSMB_DEBUG(printf("amdsmb: READB from 0x%x, cmd=0x%x, byte=0x%x, "
	    "error=0x%x\n", slave, cmd, (unsigned char)*byte, error));
	AMDSMB_UNLOCK(sc);

	return (error);
}

static int
amdsmb_writew(device_t dev, u_char slave, char cmd, short word)
{
	struct amdsmb_softc *sc = (struct amdsmb_softc *)device_get_softc(dev);
	int error;

	AMDSMB_LOCK(sc);
	amdsmb_ec_write(sc, SMB_CMD, cmd);
	amdsmb_ec_write(sc, SMB_DATA, word);
	amdsmb_ec_write(sc, SMB_DATA + 1, word >> 8);
	amdsmb_ec_write(sc, SMB_ADDR, slave);
	amdsmb_ec_write(sc, SMB_PRTCL, SMB_PRTCL_WRITE | SMB_PRTCL_WORD_DATA);

	error = amdsmb_wait(sc);

	AMDSMB_DEBUG(printf("amdsmb: WRITEW to 0x%x, cmd=0x%x, word=0x%x, "
	    "error=0x%x\n", slave, cmd, word, error));
	AMDSMB_UNLOCK(sc);

	return (error);
}

static int
amdsmb_readw(device_t dev, u_char slave, char cmd, short *word)
{
	struct amdsmb_softc *sc = (struct amdsmb_softc *)device_get_softc(dev);
	u_char temp[2];
	int error;

	AMDSMB_LOCK(sc);
	amdsmb_ec_write(sc, SMB_CMD, cmd);
	amdsmb_ec_write(sc, SMB_ADDR, slave);
	amdsmb_ec_write(sc, SMB_PRTCL, SMB_PRTCL_READ | SMB_PRTCL_WORD_DATA);

	if ((error = amdsmb_wait(sc)) == SMB_ENOERR) {
		amdsmb_ec_read(sc, SMB_DATA + 0, &temp[0]);
		amdsmb_ec_read(sc, SMB_DATA + 1, &temp[1]);
		*word = temp[0] | (temp[1] << 8);
	}

	AMDSMB_DEBUG(printf("amdsmb: READW from 0x%x, cmd=0x%x, word=0x%x, "
	    "error=0x%x\n", slave, cmd, (unsigned short)*word, error));
	AMDSMB_UNLOCK(sc);

	return (error);
}

static int
amdsmb_bwrite(device_t dev, u_char slave, char cmd, u_char count, char *buf)
{
	struct amdsmb_softc *sc = (struct amdsmb_softc *)device_get_softc(dev);
	u_char i;
	int error;

	if (count < 1 || count > 32)
		return (SMB_EINVAL);

	AMDSMB_LOCK(sc);
	amdsmb_ec_write(sc, SMB_CMD, cmd);
	amdsmb_ec_write(sc, SMB_BCNT, count);
	for (i = 0; i < count; i++)
		amdsmb_ec_write(sc, SMB_DATA + i, buf[i]);
	amdsmb_ec_write(sc, SMB_ADDR, slave);
	amdsmb_ec_write(sc, SMB_PRTCL, SMB_PRTCL_WRITE | SMB_PRTCL_BLOCK_DATA);

	error = amdsmb_wait(sc);

	AMDSMB_DEBUG(printf("amdsmb: WRITEBLK to 0x%x, count=0x%x, cmd=0x%x, "
	    "error=0x%x", slave, count, cmd, error));
	AMDSMB_UNLOCK(sc);

	return (error);
}

static int
amdsmb_bread(device_t dev, u_char slave, char cmd, u_char *count, char *buf)
{
	struct amdsmb_softc *sc = (struct amdsmb_softc *)device_get_softc(dev);
	u_char data, len, i;
	int error;

	if (*count < 1 || *count > 32)
		return (SMB_EINVAL);

	AMDSMB_LOCK(sc);
	amdsmb_ec_write(sc, SMB_CMD, cmd);
	amdsmb_ec_write(sc, SMB_ADDR, slave);
	amdsmb_ec_write(sc, SMB_PRTCL, SMB_PRTCL_READ | SMB_PRTCL_BLOCK_DATA);

	if ((error = amdsmb_wait(sc)) == SMB_ENOERR) {
		amdsmb_ec_read(sc, SMB_BCNT, &len);
		for (i = 0; i < len; i++) {
			amdsmb_ec_read(sc, SMB_DATA + i, &data);
			if (i < *count)
				buf[i] = data;
		}
		*count = len;
	}

	AMDSMB_DEBUG(printf("amdsmb: READBLK to 0x%x, count=0x%x, cmd=0x%x, "
	    "error=0x%x", slave, *count, cmd, error));
	AMDSMB_UNLOCK(sc);

	return (error);
}

static device_method_t amdsmb_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		amdsmb_probe),
	DEVMETHOD(device_attach,	amdsmb_attach),
	DEVMETHOD(device_detach,	amdsmb_detach),

	/* SMBus interface */
	DEVMETHOD(smbus_callback,	amdsmb_callback),
	DEVMETHOD(smbus_quick,		amdsmb_quick),
	DEVMETHOD(smbus_sendb,		amdsmb_sendb),
	DEVMETHOD(smbus_recvb,		amdsmb_recvb),
	DEVMETHOD(smbus_writeb,		amdsmb_writeb),
	DEVMETHOD(smbus_readb,		amdsmb_readb),
	DEVMETHOD(smbus_writew,		amdsmb_writew),
	DEVMETHOD(smbus_readw,		amdsmb_readw),
	DEVMETHOD(smbus_bwrite,		amdsmb_bwrite),
	DEVMETHOD(smbus_bread,		amdsmb_bread),

	{ 0, 0 }
};

static devclass_t amdsmb_devclass;

static driver_t amdsmb_driver = {
	"amdsmb",
	amdsmb_methods,
	sizeof(struct amdsmb_softc),
};

DRIVER_MODULE(amdsmb, pci, amdsmb_driver, amdsmb_devclass, 0, 0);
DRIVER_MODULE(smbus, amdsmb, smbus_driver, smbus_devclass, 0, 0);

MODULE_DEPEND(amdsmb, pci, 1, 1, 1);
MODULE_DEPEND(amdsmb, smbus, SMBUS_MINVER, SMBUS_PREFVER, SMBUS_MAXVER);
MODULE_VERSION(amdsmb, 1);
