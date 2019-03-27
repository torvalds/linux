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

#define	NFSMB_DEBUG(x)	if (nfsmb_debug) (x)

#ifdef DEBUG
static int nfsmb_debug = 1;
#else
static int nfsmb_debug = 0;
#endif

/* NVIDIA nForce2/3/4 MCP */
#define	NFSMB_VENDORID_NVIDIA		0x10de
#define	NFSMB_DEVICEID_NF2_SMB		0x0064
#define	NFSMB_DEVICEID_NF2_ULTRA_SMB	0x0084
#define	NFSMB_DEVICEID_NF3_PRO150_SMB	0x00d4
#define	NFSMB_DEVICEID_NF3_250GB_SMB	0x00e4
#define	NFSMB_DEVICEID_NF4_SMB		0x0052
#define	NFSMB_DEVICEID_NF4_04_SMB	0x0034
#define	NFSMB_DEVICEID_NF4_51_SMB	0x0264
#define	NFSMB_DEVICEID_NF4_55_SMB	0x0368
#define	NFSMB_DEVICEID_NF4_61_SMB	0x03eb
#define	NFSMB_DEVICEID_NF4_65_SMB	0x0446
#define	NFSMB_DEVICEID_NF4_67_SMB	0x0542
#define	NFSMB_DEVICEID_NF4_73_SMB	0x07d8
#define	NFSMB_DEVICEID_NF4_78S_SMB	0x0752
#define	NFSMB_DEVICEID_NF4_79_SMB	0x0aa2

/* PCI Configuration space registers */
#define	NF2PCI_SMBASE_1		PCIR_BAR(4)
#define	NF2PCI_SMBASE_2		PCIR_BAR(5)

/*
 * ACPI 3.0, Chapter 12, SMBus Host Controller Interface.
 */
#define	SMB_PRTCL		0x00	/* protocol */
#define	SMB_STS			0x01	/* status */
#define	SMB_ADDR		0x02	/* address */
#define	SMB_CMD			0x03	/* command */
#define	SMB_DATA		0x04	/* 32 data registers */
#define	SMB_BCNT		0x24	/* number of data bytes */
#define	SMB_ALRM_A		0x25	/* alarm address */
#define	SMB_ALRM_D		0x26	/* 2 bytes alarm data */

#define	SMB_STS_DONE		0x80
#define	SMB_STS_ALRM		0x40
#define	SMB_STS_RES		0x20
#define	SMB_STS_STATUS		0x1f
#define	SMB_STS_OK		0x00	/* OK */
#define	SMB_STS_UF		0x07	/* Unknown Failure */
#define	SMB_STS_DANA		0x10	/* Device Address Not Acknowledged */
#define	SMB_STS_DED		0x11	/* Device Error Detected */
#define	SMB_STS_DCAD		0x12	/* Device Command Access Denied */
#define	SMB_STS_UE		0x13	/* Unknown Error */
#define	SMB_STS_DAD		0x17	/* Device Access Denied */
#define	SMB_STS_T		0x18	/* Timeout */
#define	SMB_STS_HUP		0x19	/* Host Unsupported Protocol */
#define	SMB_STS_B		0x1A	/* Busy */
#define	SMB_STS_PEC		0x1F	/* PEC (CRC-8) Error */

#define	SMB_PRTCL_WRITE		0x00
#define	SMB_PRTCL_READ		0x01
#define	SMB_PRTCL_QUICK		0x02
#define	SMB_PRTCL_BYTE		0x04
#define	SMB_PRTCL_BYTE_DATA	0x06
#define	SMB_PRTCL_WORD_DATA	0x08
#define	SMB_PRTCL_BLOCK_DATA	0x0a
#define	SMB_PRTCL_PROC_CALL	0x0c
#define	SMB_PRTCL_BLOCK_PROC_CALL 0x0d
#define	SMB_PRTCL_PEC		0x80

struct nfsmb_softc {
	int rid;
	struct resource *res;
	device_t smbus;
	device_t subdev;
	struct mtx lock;
};

#define	NFSMB_LOCK(nfsmb)		mtx_lock(&(nfsmb)->lock)
#define	NFSMB_UNLOCK(nfsmb)		mtx_unlock(&(nfsmb)->lock)
#define	NFSMB_LOCK_ASSERT(nfsmb)	mtx_assert(&(nfsmb)->lock, MA_OWNED)

#define	NFSMB_SMBINB(nfsmb, register)					\
	(bus_read_1(nfsmb->res, register))
#define	NFSMB_SMBOUTB(nfsmb, register, value) \
	(bus_write_1(nfsmb->res, register, value))

static int	nfsmb_detach(device_t dev);
static int	nfsmbsub_detach(device_t dev);

static int
nfsmbsub_probe(device_t dev)
{

	device_set_desc(dev, "nForce2/3/4 MCP SMBus Controller");
	return (BUS_PROBE_DEFAULT);
}

static int
nfsmb_probe(device_t dev)
{
	u_int16_t vid;
	u_int16_t did;

	vid = pci_get_vendor(dev);
	did = pci_get_device(dev);

	if (vid == NFSMB_VENDORID_NVIDIA) {
		switch(did) {
		case NFSMB_DEVICEID_NF2_SMB:
		case NFSMB_DEVICEID_NF2_ULTRA_SMB:
		case NFSMB_DEVICEID_NF3_PRO150_SMB:
		case NFSMB_DEVICEID_NF3_250GB_SMB:
		case NFSMB_DEVICEID_NF4_SMB:
		case NFSMB_DEVICEID_NF4_04_SMB:
		case NFSMB_DEVICEID_NF4_51_SMB:
		case NFSMB_DEVICEID_NF4_55_SMB:
		case NFSMB_DEVICEID_NF4_61_SMB:
		case NFSMB_DEVICEID_NF4_65_SMB:
		case NFSMB_DEVICEID_NF4_67_SMB:
		case NFSMB_DEVICEID_NF4_73_SMB:
		case NFSMB_DEVICEID_NF4_78S_SMB:
		case NFSMB_DEVICEID_NF4_79_SMB:
			device_set_desc(dev, "nForce2/3/4 MCP SMBus Controller");
			return (BUS_PROBE_DEFAULT);
		}
	}

	return (ENXIO);
}

static int
nfsmbsub_attach(device_t dev)
{
	device_t parent;
	struct nfsmb_softc *nfsmbsub_sc = device_get_softc(dev);

	parent = device_get_parent(dev);

	nfsmbsub_sc->rid = NF2PCI_SMBASE_2;

	nfsmbsub_sc->res = bus_alloc_resource_any(parent, SYS_RES_IOPORT,
	    &nfsmbsub_sc->rid, RF_ACTIVE);
	if (nfsmbsub_sc->res == NULL) {
		/* Older incarnations of the device used non-standard BARs. */
		nfsmbsub_sc->rid = 0x54;
		nfsmbsub_sc->res = bus_alloc_resource_any(parent,
		    SYS_RES_IOPORT, &nfsmbsub_sc->rid, RF_ACTIVE);
		if (nfsmbsub_sc->res == NULL) {
			device_printf(dev, "could not map i/o space\n");
			return (ENXIO);
		}
	}
	mtx_init(&nfsmbsub_sc->lock, device_get_nameunit(dev), "nfsmb",
	    MTX_DEF);

	nfsmbsub_sc->smbus = device_add_child(dev, "smbus", -1);
	if (nfsmbsub_sc->smbus == NULL) {
		nfsmbsub_detach(dev);
		return (EINVAL);
	}

	bus_generic_attach(dev);

	return (0);
}

static int
nfsmb_attach(device_t dev)
{
	struct nfsmb_softc *nfsmb_sc = device_get_softc(dev);

	/* Allocate I/O space */
	nfsmb_sc->rid = NF2PCI_SMBASE_1;

	nfsmb_sc->res = bus_alloc_resource_any(dev, SYS_RES_IOPORT,
		&nfsmb_sc->rid, RF_ACTIVE);

	if (nfsmb_sc->res == NULL) {
		/* Older incarnations of the device used non-standard BARs. */
		nfsmb_sc->rid = 0x50;
		nfsmb_sc->res = bus_alloc_resource_any(dev,
		    SYS_RES_IOPORT, &nfsmb_sc->rid, RF_ACTIVE);
		if (nfsmb_sc->res == NULL) {
			device_printf(dev, "could not map i/o space\n");
			return (ENXIO);
		}
	}

	mtx_init(&nfsmb_sc->lock, device_get_nameunit(dev), "nfsmb", MTX_DEF);

	/* Allocate a new smbus device */
	nfsmb_sc->smbus = device_add_child(dev, "smbus", -1);
	if (!nfsmb_sc->smbus) {
		nfsmb_detach(dev);
		return (EINVAL);
	}

	nfsmb_sc->subdev = NULL;
	switch (pci_get_device(dev)) {
	case NFSMB_DEVICEID_NF2_SMB:
	case NFSMB_DEVICEID_NF2_ULTRA_SMB:
	case NFSMB_DEVICEID_NF3_PRO150_SMB:
	case NFSMB_DEVICEID_NF3_250GB_SMB:
	case NFSMB_DEVICEID_NF4_SMB:
	case NFSMB_DEVICEID_NF4_04_SMB:
	case NFSMB_DEVICEID_NF4_51_SMB:
	case NFSMB_DEVICEID_NF4_55_SMB:
	case NFSMB_DEVICEID_NF4_61_SMB:
	case NFSMB_DEVICEID_NF4_65_SMB:
	case NFSMB_DEVICEID_NF4_67_SMB:
	case NFSMB_DEVICEID_NF4_73_SMB:
	case NFSMB_DEVICEID_NF4_78S_SMB:
	case NFSMB_DEVICEID_NF4_79_SMB:
		/* Trying to add secondary device as slave */
		nfsmb_sc->subdev = device_add_child(dev, "nfsmb", -1);
		if (!nfsmb_sc->subdev) {
			nfsmb_detach(dev);
			return (EINVAL);
		}
		break;
	default:
		break;
	}

	bus_generic_attach(dev);

	return (0);
}

static int
nfsmbsub_detach(device_t dev)
{
	device_t parent;
	struct nfsmb_softc *nfsmbsub_sc = device_get_softc(dev);

	parent = device_get_parent(dev);

	if (nfsmbsub_sc->smbus) {
		device_delete_child(dev, nfsmbsub_sc->smbus);
		nfsmbsub_sc->smbus = NULL;
	}
	mtx_destroy(&nfsmbsub_sc->lock);
	if (nfsmbsub_sc->res) {
		bus_release_resource(parent, SYS_RES_IOPORT, nfsmbsub_sc->rid,
		    nfsmbsub_sc->res);
		nfsmbsub_sc->res = NULL;
	}
	return (0);
}

static int
nfsmb_detach(device_t dev)
{
	struct nfsmb_softc *nfsmb_sc = device_get_softc(dev);

	if (nfsmb_sc->subdev) {
		device_delete_child(dev, nfsmb_sc->subdev);
		nfsmb_sc->subdev = NULL;
	}

	if (nfsmb_sc->smbus) {
		device_delete_child(dev, nfsmb_sc->smbus);
		nfsmb_sc->smbus = NULL;
	}

	mtx_destroy(&nfsmb_sc->lock);
	if (nfsmb_sc->res) {
		bus_release_resource(dev, SYS_RES_IOPORT, nfsmb_sc->rid,
		    nfsmb_sc->res);
		nfsmb_sc->res = NULL;
	}

	return (0);
}

static int
nfsmb_callback(device_t dev, int index, void *data)
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
nfsmb_wait(struct nfsmb_softc *sc)
{
	u_char sts;
	int error, count;

	NFSMB_LOCK_ASSERT(sc);
	if (NFSMB_SMBINB(sc, SMB_PRTCL) != 0)
	{
		count = 10000;
		do {
			DELAY(500);
		} while (NFSMB_SMBINB(sc, SMB_PRTCL) != 0 && count--);
		if (count == 0)
			return (SMB_ETIMEOUT);
	}

	sts = NFSMB_SMBINB(sc, SMB_STS) & SMB_STS_STATUS;
	NFSMB_DEBUG(printf("nfsmb: STS=0x%x\n", sts));

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
nfsmb_quick(device_t dev, u_char slave, int how)
{
	struct nfsmb_softc *sc = (struct nfsmb_softc *)device_get_softc(dev);
	u_char protocol;
	int error;

	protocol = SMB_PRTCL_QUICK;

	switch (how) {
	case SMB_QWRITE:
		protocol |= SMB_PRTCL_WRITE;
		NFSMB_DEBUG(printf("nfsmb: QWRITE to 0x%x", slave));
		break;
	case SMB_QREAD:
		protocol |= SMB_PRTCL_READ;
		NFSMB_DEBUG(printf("nfsmb: QREAD to 0x%x", slave));
		break;
	default:
		panic("%s: unknown QUICK command (%x)!", __func__, how);
	}

	NFSMB_LOCK(sc);
	NFSMB_SMBOUTB(sc, SMB_ADDR, slave);
	NFSMB_SMBOUTB(sc, SMB_PRTCL, protocol);

	error = nfsmb_wait(sc);

	NFSMB_DEBUG(printf(", error=0x%x\n", error));
	NFSMB_UNLOCK(sc);

	return (error);
}

static int
nfsmb_sendb(device_t dev, u_char slave, char byte)
{
	struct nfsmb_softc *sc = (struct nfsmb_softc *)device_get_softc(dev);
	int error;

	NFSMB_LOCK(sc);
	NFSMB_SMBOUTB(sc, SMB_CMD, byte);
	NFSMB_SMBOUTB(sc, SMB_ADDR, slave);
	NFSMB_SMBOUTB(sc, SMB_PRTCL, SMB_PRTCL_WRITE | SMB_PRTCL_BYTE);

	error = nfsmb_wait(sc);

	NFSMB_DEBUG(printf("nfsmb: SENDB to 0x%x, byte=0x%x, error=0x%x\n", slave, byte, error));
	NFSMB_UNLOCK(sc);

	return (error);
}

static int
nfsmb_recvb(device_t dev, u_char slave, char *byte)
{
	struct nfsmb_softc *sc = (struct nfsmb_softc *)device_get_softc(dev);
	int error;

	NFSMB_LOCK(sc);
	NFSMB_SMBOUTB(sc, SMB_ADDR, slave);
	NFSMB_SMBOUTB(sc, SMB_PRTCL, SMB_PRTCL_READ | SMB_PRTCL_BYTE);

	if ((error = nfsmb_wait(sc)) == SMB_ENOERR)
		*byte = NFSMB_SMBINB(sc, SMB_DATA);

	NFSMB_DEBUG(printf("nfsmb: RECVB from 0x%x, byte=0x%x, error=0x%x\n", slave, *byte, error));
	NFSMB_UNLOCK(sc);

	return (error);
}

static int
nfsmb_writeb(device_t dev, u_char slave, char cmd, char byte)
{
	struct nfsmb_softc *sc = (struct nfsmb_softc *)device_get_softc(dev);
	int error;

	NFSMB_LOCK(sc);
	NFSMB_SMBOUTB(sc, SMB_CMD, cmd);
	NFSMB_SMBOUTB(sc, SMB_DATA, byte);
	NFSMB_SMBOUTB(sc, SMB_ADDR, slave);
	NFSMB_SMBOUTB(sc, SMB_PRTCL, SMB_PRTCL_WRITE | SMB_PRTCL_BYTE_DATA);

	error = nfsmb_wait(sc);

	NFSMB_DEBUG(printf("nfsmb: WRITEB to 0x%x, cmd=0x%x, byte=0x%x, error=0x%x\n", slave, cmd, byte, error));
	NFSMB_UNLOCK(sc);

	return (error);
}

static int
nfsmb_readb(device_t dev, u_char slave, char cmd, char *byte)
{
	struct nfsmb_softc *sc = (struct nfsmb_softc *)device_get_softc(dev);
	int error;

	NFSMB_LOCK(sc);
	NFSMB_SMBOUTB(sc, SMB_CMD, cmd);
	NFSMB_SMBOUTB(sc, SMB_ADDR, slave);
	NFSMB_SMBOUTB(sc, SMB_PRTCL, SMB_PRTCL_READ | SMB_PRTCL_BYTE_DATA);

	if ((error = nfsmb_wait(sc)) == SMB_ENOERR)
		*byte = NFSMB_SMBINB(sc, SMB_DATA);

	NFSMB_DEBUG(printf("nfsmb: READB from 0x%x, cmd=0x%x, byte=0x%x, error=0x%x\n", slave, cmd, (unsigned char)*byte, error));
	NFSMB_UNLOCK(sc);

	return (error);
}

static int
nfsmb_writew(device_t dev, u_char slave, char cmd, short word)
{
	struct nfsmb_softc *sc = (struct nfsmb_softc *)device_get_softc(dev);
	int error;

	NFSMB_LOCK(sc);
	NFSMB_SMBOUTB(sc, SMB_CMD, cmd);
	NFSMB_SMBOUTB(sc, SMB_DATA, word);
	NFSMB_SMBOUTB(sc, SMB_DATA + 1, word >> 8);
	NFSMB_SMBOUTB(sc, SMB_ADDR, slave);
	NFSMB_SMBOUTB(sc, SMB_PRTCL, SMB_PRTCL_WRITE | SMB_PRTCL_WORD_DATA);

	error = nfsmb_wait(sc);

	NFSMB_DEBUG(printf("nfsmb: WRITEW to 0x%x, cmd=0x%x, word=0x%x, error=0x%x\n", slave, cmd, word, error));
	NFSMB_UNLOCK(sc);

	return (error);
}

static int
nfsmb_readw(device_t dev, u_char slave, char cmd, short *word)
{
	struct nfsmb_softc *sc = (struct nfsmb_softc *)device_get_softc(dev);
	int error;

	NFSMB_LOCK(sc);
	NFSMB_SMBOUTB(sc, SMB_CMD, cmd);
	NFSMB_SMBOUTB(sc, SMB_ADDR, slave);
	NFSMB_SMBOUTB(sc, SMB_PRTCL, SMB_PRTCL_READ | SMB_PRTCL_WORD_DATA);

	if ((error = nfsmb_wait(sc)) == SMB_ENOERR)
		*word = NFSMB_SMBINB(sc, SMB_DATA) |
		    (NFSMB_SMBINB(sc, SMB_DATA + 1) << 8);

	NFSMB_DEBUG(printf("nfsmb: READW from 0x%x, cmd=0x%x, word=0x%x, error=0x%x\n", slave, cmd, (unsigned short)*word, error));
	NFSMB_UNLOCK(sc);

	return (error);
}

static int
nfsmb_bwrite(device_t dev, u_char slave, char cmd, u_char count, char *buf)
{
	struct nfsmb_softc *sc = (struct nfsmb_softc *)device_get_softc(dev);
	u_char i;
	int error;

	if (count < 1 || count > 32)
		return (SMB_EINVAL);

	NFSMB_LOCK(sc);
	NFSMB_SMBOUTB(sc, SMB_CMD, cmd);
	NFSMB_SMBOUTB(sc, SMB_BCNT, count);
	for (i = 0; i < count; i++)
		NFSMB_SMBOUTB(sc, SMB_DATA + i, buf[i]);
	NFSMB_SMBOUTB(sc, SMB_ADDR, slave);
	NFSMB_SMBOUTB(sc, SMB_PRTCL, SMB_PRTCL_WRITE | SMB_PRTCL_BLOCK_DATA);

	error = nfsmb_wait(sc);

	NFSMB_DEBUG(printf("nfsmb: WRITEBLK to 0x%x, count=0x%x, cmd=0x%x, error=0x%x", slave, count, cmd, error));
	NFSMB_UNLOCK(sc);

	return (error);
}

static int
nfsmb_bread(device_t dev, u_char slave, char cmd, u_char *count, char *buf)
{
	struct nfsmb_softc *sc = (struct nfsmb_softc *)device_get_softc(dev);
	u_char data, len, i;
	int error;

	if (*count < 1 || *count > 32)
		return (SMB_EINVAL);

	NFSMB_LOCK(sc);
	NFSMB_SMBOUTB(sc, SMB_CMD, cmd);
	NFSMB_SMBOUTB(sc, SMB_ADDR, slave);
	NFSMB_SMBOUTB(sc, SMB_PRTCL, SMB_PRTCL_READ | SMB_PRTCL_BLOCK_DATA);

	if ((error = nfsmb_wait(sc)) == SMB_ENOERR) {
		len = NFSMB_SMBINB(sc, SMB_BCNT);
		for (i = 0; i < len; i++) {
			data = NFSMB_SMBINB(sc, SMB_DATA + i);
			if (i < *count)
				buf[i] = data;
		}
		*count = len;
	}

	NFSMB_DEBUG(printf("nfsmb: READBLK to 0x%x, count=0x%x, cmd=0x%x, error=0x%x", slave, *count, cmd, error));
	NFSMB_UNLOCK(sc);

	return (error);
}

static device_method_t nfsmb_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		nfsmb_probe),
	DEVMETHOD(device_attach,	nfsmb_attach),
	DEVMETHOD(device_detach,	nfsmb_detach),

	/* SMBus interface */
	DEVMETHOD(smbus_callback,	nfsmb_callback),
	DEVMETHOD(smbus_quick,		nfsmb_quick),
	DEVMETHOD(smbus_sendb,		nfsmb_sendb),
	DEVMETHOD(smbus_recvb,		nfsmb_recvb),
	DEVMETHOD(smbus_writeb,		nfsmb_writeb),
	DEVMETHOD(smbus_readb,		nfsmb_readb),
	DEVMETHOD(smbus_writew,		nfsmb_writew),
	DEVMETHOD(smbus_readw,		nfsmb_readw),
	DEVMETHOD(smbus_bwrite,		nfsmb_bwrite),
	DEVMETHOD(smbus_bread,		nfsmb_bread),

	{ 0, 0 }
};

static device_method_t nfsmbsub_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		nfsmbsub_probe),
	DEVMETHOD(device_attach,	nfsmbsub_attach),
	DEVMETHOD(device_detach,	nfsmbsub_detach),

	/* SMBus interface */
	DEVMETHOD(smbus_callback,	nfsmb_callback),
	DEVMETHOD(smbus_quick,		nfsmb_quick),
	DEVMETHOD(smbus_sendb,		nfsmb_sendb),
	DEVMETHOD(smbus_recvb,		nfsmb_recvb),
	DEVMETHOD(smbus_writeb,		nfsmb_writeb),
	DEVMETHOD(smbus_readb,		nfsmb_readb),
	DEVMETHOD(smbus_writew,		nfsmb_writew),
	DEVMETHOD(smbus_readw,		nfsmb_readw),
	DEVMETHOD(smbus_bwrite,		nfsmb_bwrite),
	DEVMETHOD(smbus_bread,		nfsmb_bread),

	{ 0, 0 }
};

static devclass_t nfsmb_devclass;

static driver_t nfsmb_driver = {
	"nfsmb",
	nfsmb_methods,
	sizeof(struct nfsmb_softc),
};

static driver_t nfsmbsub_driver = {
	"nfsmb",
	nfsmbsub_methods,
	sizeof(struct nfsmb_softc),
};

DRIVER_MODULE(nfsmb, pci, nfsmb_driver, nfsmb_devclass, 0, 0);
DRIVER_MODULE(nfsmb, nfsmb, nfsmbsub_driver, nfsmb_devclass, 0, 0);
DRIVER_MODULE(smbus, nfsmb, smbus_driver, smbus_devclass, 0, 0);

MODULE_DEPEND(nfsmb, pci, 1, 1, 1);
MODULE_DEPEND(nfsmb, smbus, SMBUS_MINVER, SMBUS_PREFVER, SMBUS_MAXVER);
MODULE_VERSION(nfsmb, 1);
