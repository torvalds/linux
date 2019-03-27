/*-
 * Copyright (c) 1998, 1999, 2001 Nicolas Souchu
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

/*
 * Power Management support for the Acer M15x3 chipsets
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

#define ALPM_DEBUG(x)	if (alpm_debug) (x)

#ifdef DEBUG
static int alpm_debug = 1;
#else
static int alpm_debug = 0;
#endif

#define ACER_M1543_PMU_ID	0x710110b9

/*
 * I/O registers offsets - the base address is programmed via the
 * SMBBA PCI configuration register
 */
#define SMBSTS		0x0	/* SMBus host/slave status register */
#define SMBCMD		0x1	/* SMBus host/slave command register */
#define SMBSTART	0x2	/* start to generate programmed cycle */
#define SMBHADDR	0x3	/* host address register */
#define SMBHDATA	0x4	/* data A register for host controller */
#define SMBHDATB	0x5	/* data B register for host controller */
#define SMBHBLOCK	0x6	/* block register for host controller */
#define SMBHCMD		0x7	/* command register for host controller */

/* SMBHADDR mask. */
#define	LSB		0x1	/* XXX: Better name: Read/Write? */

/* SMBSTS masks */
#define TERMINATE	0x80
#define BUS_COLLI	0x40
#define DEVICE_ERR	0x20
#define SMI_I_STS	0x10
#define HST_BSY		0x08
#define IDL_STS		0x04
#define HSTSLV_STS	0x02
#define HSTSLV_BSY	0x01

/* SMBCMD masks */
#define SMB_BLK_CLR	0x80
#define T_OUT_CMD	0x08
#define ABORT_HOST	0x04

/* SMBus commands */
#define SMBQUICK	0x00
#define SMBSRBYTE	0x10		/* send/receive byte */
#define SMBWRBYTE	0x20		/* write/read byte */
#define SMBWRWORD	0x30		/* write/read word */
#define SMBWRBLOCK	0x40		/* write/read block */

/* PCI configuration registers and masks
 */
#define COM		0x4
#define COM_ENABLE_IO	0x1

#define SMBBA		PCIR_BAR(1)

#define ATPC		0x5b
#define ATPC_SMBCTRL	0x04 		/* XX linux has this as 0x6 */

#define SMBHSI		0xe0
#define SMBHSI_SLAVE	0x2
#define SMBHSI_HOST	0x1

#define SMBHCBC		0xe2
#define SMBHCBC_CLOCK	0x70

#define SMBCLOCK_149K	0x0
#define SMBCLOCK_74K	0x20
#define SMBCLOCK_37K	0x40
#define SMBCLOCK_223K	0x80
#define SMBCLOCK_111K	0xa0
#define SMBCLOCK_55K	0xc0

struct alpm_softc {
	int base;
	struct resource *res;
        bus_space_tag_t smbst;
        bus_space_handle_t smbsh;
	device_t smbus;
	struct mtx lock;
};

#define	ALPM_LOCK(alpm)		mtx_lock(&(alpm)->lock)
#define	ALPM_UNLOCK(alpm)	mtx_unlock(&(alpm)->lock)
#define	ALPM_LOCK_ASSERT(alpm)	mtx_assert(&(alpm)->lock, MA_OWNED)

#define ALPM_SMBINB(alpm,register) \
	(bus_space_read_1(alpm->smbst, alpm->smbsh, register))
#define ALPM_SMBOUTB(alpm,register,value) \
	(bus_space_write_1(alpm->smbst, alpm->smbsh, register, value))

static int	alpm_detach(device_t dev);

static int
alpm_probe(device_t dev)
{

	if (pci_get_devid(dev) == ACER_M1543_PMU_ID) {
		device_set_desc(dev, "AcerLabs M15x3 Power Management Unit");

		return (BUS_PROBE_DEFAULT);
	}

	return (ENXIO);
}

static int
alpm_attach(device_t dev)
{
	int rid;
	u_int32_t l;
	struct alpm_softc *alpm;

	alpm = device_get_softc(dev);

	/* Unlock SMBIO base register access */
	l = pci_read_config(dev, ATPC, 1);
	pci_write_config(dev, ATPC, l & ~ATPC_SMBCTRL, 1);

	/*
	 * XX linux sets clock to 74k, should we?
	l = pci_read_config(dev, SMBHCBC, 1);
	l &= 0x1f;
	l |= SMBCLOCK_74K;
	pci_write_config(dev, SMBHCBC, l, 1);
	 */

	if (bootverbose || alpm_debug) {
		l = pci_read_config(dev, SMBHSI, 1);
		device_printf(dev, "%s/%s",
			(l & SMBHSI_HOST) ? "host":"nohost",
			(l & SMBHSI_SLAVE) ? "slave":"noslave");

		l = pci_read_config(dev, SMBHCBC, 1);
		switch (l & SMBHCBC_CLOCK) {
		case SMBCLOCK_149K:
			printf(" 149K");
			break;
		case SMBCLOCK_74K:
			printf(" 74K");
			break;
		case SMBCLOCK_37K:
			printf(" 37K");
			break;
		case SMBCLOCK_223K:
			printf(" 223K");
			break;
		case SMBCLOCK_111K:
			printf(" 111K");
			break;
		case SMBCLOCK_55K:
			printf(" 55K");
			break;
		default:
			printf("unknown");
			break;
		}
		printf("\n");
	}

	rid = SMBBA;
	alpm->res = bus_alloc_resource_any(dev, SYS_RES_IOPORT, &rid,
	    RF_ACTIVE);

	if (alpm->res == NULL) {
		device_printf(dev,"Could not allocate Bus space\n");
		return (ENXIO);
	}
	alpm->smbst = rman_get_bustag(alpm->res);
	alpm->smbsh = rman_get_bushandle(alpm->res);
	mtx_init(&alpm->lock, device_get_nameunit(dev), "alpm", MTX_DEF);

	/* attach the smbus */
	alpm->smbus = device_add_child(dev, "smbus", -1);
	if (alpm->smbus == NULL) {
		alpm_detach(dev);
		return (EINVAL);
	}
	bus_generic_attach(dev);

	return (0);
}

static int
alpm_detach(device_t dev)
{
	struct alpm_softc *alpm = device_get_softc(dev);

	if (alpm->smbus) {
		device_delete_child(dev, alpm->smbus);
		alpm->smbus = NULL;
	}
	mtx_destroy(&alpm->lock);

	if (alpm->res)
		bus_release_resource(dev, SYS_RES_IOPORT, SMBBA, alpm->res);

	return (0);
}

static int
alpm_callback(device_t dev, int index, void *data)
{
	int error = 0;

	switch (index) {
	case SMB_REQUEST_BUS:
	case SMB_RELEASE_BUS:
		/* ok, bus allocation accepted */
		break;
	default:
		error = EINVAL;
	}

	return (error);
}

static int
alpm_clear(struct alpm_softc *sc)
{
	ALPM_SMBOUTB(sc, SMBSTS, 0xff);
	DELAY(10);

	return (0);
}

#if 0
static int
alpm_abort(struct alpm_softc *sc)
{
	ALPM_SMBOUTB(sc, SMBCMD, T_OUT_CMD | ABORT_HOST);

	return (0);
}
#endif

static int
alpm_idle(struct alpm_softc *sc)
{
	u_char sts;

	sts = ALPM_SMBINB(sc, SMBSTS);

	ALPM_DEBUG(printf("alpm: idle? STS=0x%x\n", sts));

	return (sts & IDL_STS);
}

/*
 * Poll the SMBus controller
 */
static int
alpm_wait(struct alpm_softc *sc)
{
	int count = 10000;
	u_char sts = 0;
	int error;

	/* wait for command to complete and SMBus controller is idle */
	while (count--) {
		DELAY(10);
		sts = ALPM_SMBINB(sc, SMBSTS);
		if (sts & SMI_I_STS)
			break;
	}

	ALPM_DEBUG(printf("alpm: STS=0x%x\n", sts));

	error = SMB_ENOERR;

	if (!count)
		error |= SMB_ETIMEOUT;

	if (sts & TERMINATE)
		error |= SMB_EABORT;

	if (sts & BUS_COLLI)
		error |= SMB_ENOACK;

	if (sts & DEVICE_ERR)
		error |= SMB_EBUSERR;

	if (error != SMB_ENOERR)
		alpm_clear(sc);

	return (error);
}

static int
alpm_quick(device_t dev, u_char slave, int how)
{
	struct alpm_softc *sc = (struct alpm_softc *)device_get_softc(dev);
	int error;

	ALPM_LOCK(sc);
	alpm_clear(sc);
	if (!alpm_idle(sc)) {
		ALPM_UNLOCK(sc);
		return (EBUSY);
	}

	switch (how) {
	case SMB_QWRITE:
		ALPM_DEBUG(printf("alpm: QWRITE to 0x%x", slave));
		ALPM_SMBOUTB(sc, SMBHADDR, slave & ~LSB);
		break;
	case SMB_QREAD:
		ALPM_DEBUG(printf("alpm: QREAD to 0x%x", slave));
		ALPM_SMBOUTB(sc, SMBHADDR, slave | LSB);
		break;
	default:
		panic("%s: unknown QUICK command (%x)!", __func__,
			how);
	}
	ALPM_SMBOUTB(sc, SMBCMD, SMBQUICK);
	ALPM_SMBOUTB(sc, SMBSTART, 0xff);

	error = alpm_wait(sc);

	ALPM_DEBUG(printf(", error=0x%x\n", error));
	ALPM_UNLOCK(sc);

	return (error);
}

static int
alpm_sendb(device_t dev, u_char slave, char byte)
{
	struct alpm_softc *sc = (struct alpm_softc *)device_get_softc(dev);
	int error;

	ALPM_LOCK(sc);
	alpm_clear(sc);
	if (!alpm_idle(sc)) {
		ALPM_UNLOCK(sc);
		return (SMB_EBUSY);
	}

	ALPM_SMBOUTB(sc, SMBHADDR, slave & ~LSB);
	ALPM_SMBOUTB(sc, SMBCMD, SMBSRBYTE);
	ALPM_SMBOUTB(sc, SMBHDATA, byte);
	ALPM_SMBOUTB(sc, SMBSTART, 0xff);

	error = alpm_wait(sc);

	ALPM_DEBUG(printf("alpm: SENDB to 0x%x, byte=0x%x, error=0x%x\n", slave, byte, error));
	ALPM_UNLOCK(sc);

	return (error);
}

static int
alpm_recvb(device_t dev, u_char slave, char *byte)
{
	struct alpm_softc *sc = (struct alpm_softc *)device_get_softc(dev);
	int error;

	ALPM_LOCK(sc);
	alpm_clear(sc);
	if (!alpm_idle(sc)) {
		ALPM_UNLOCK(sc);
		return (SMB_EBUSY);
	}

	ALPM_SMBOUTB(sc, SMBHADDR, slave | LSB);
	ALPM_SMBOUTB(sc, SMBCMD, SMBSRBYTE);
	ALPM_SMBOUTB(sc, SMBSTART, 0xff);

	if ((error = alpm_wait(sc)) == SMB_ENOERR)
		*byte = ALPM_SMBINB(sc, SMBHDATA);

	ALPM_DEBUG(printf("alpm: RECVB from 0x%x, byte=0x%x, error=0x%x\n", slave, *byte, error));
	ALPM_UNLOCK(sc);

	return (error);
}

static int
alpm_writeb(device_t dev, u_char slave, char cmd, char byte)
{
	struct alpm_softc *sc = (struct alpm_softc *)device_get_softc(dev);
	int error;

	ALPM_LOCK(sc);
	alpm_clear(sc);
	if (!alpm_idle(sc)) {
		ALPM_UNLOCK(sc);
		return (SMB_EBUSY);
	}

	ALPM_SMBOUTB(sc, SMBHADDR, slave & ~LSB);
	ALPM_SMBOUTB(sc, SMBCMD, SMBWRBYTE);
	ALPM_SMBOUTB(sc, SMBHDATA, byte);
	ALPM_SMBOUTB(sc, SMBHCMD, cmd);
	ALPM_SMBOUTB(sc, SMBSTART, 0xff);

	error = alpm_wait(sc);

	ALPM_DEBUG(printf("alpm: WRITEB to 0x%x, cmd=0x%x, byte=0x%x, error=0x%x\n", slave, cmd, byte, error));
	ALPM_UNLOCK(sc);

	return (error);
}

static int
alpm_readb(device_t dev, u_char slave, char cmd, char *byte)
{
	struct alpm_softc *sc = (struct alpm_softc *)device_get_softc(dev);
	int error;

	ALPM_LOCK(sc);
	alpm_clear(sc);
	if (!alpm_idle(sc)) {
		ALPM_UNLOCK(sc);
		return (SMB_EBUSY);
	}

	ALPM_SMBOUTB(sc, SMBHADDR, slave | LSB);
	ALPM_SMBOUTB(sc, SMBCMD, SMBWRBYTE);
	ALPM_SMBOUTB(sc, SMBHCMD, cmd);
	ALPM_SMBOUTB(sc, SMBSTART, 0xff);

	if ((error = alpm_wait(sc)) == SMB_ENOERR)
		*byte = ALPM_SMBINB(sc, SMBHDATA);

	ALPM_DEBUG(printf("alpm: READB from 0x%x, cmd=0x%x, byte=0x%x, error=0x%x\n", slave, cmd, *byte, error));
	ALPM_UNLOCK(sc);

	return (error);
}

static int
alpm_writew(device_t dev, u_char slave, char cmd, short word)
{
	struct alpm_softc *sc = (struct alpm_softc *)device_get_softc(dev);
	int error;

	ALPM_LOCK(sc);
	alpm_clear(sc);
	if (!alpm_idle(sc)) {
		ALPM_UNLOCK(sc);
		return (SMB_EBUSY);
	}

	ALPM_SMBOUTB(sc, SMBHADDR, slave & ~LSB);
	ALPM_SMBOUTB(sc, SMBCMD, SMBWRWORD);
	ALPM_SMBOUTB(sc, SMBHDATA, word & 0x00ff);
	ALPM_SMBOUTB(sc, SMBHDATB, (word & 0xff00) >> 8);
	ALPM_SMBOUTB(sc, SMBHCMD, cmd);
	ALPM_SMBOUTB(sc, SMBSTART, 0xff);

	error = alpm_wait(sc);

	ALPM_DEBUG(printf("alpm: WRITEW to 0x%x, cmd=0x%x, word=0x%x, error=0x%x\n", slave, cmd, word, error));
	ALPM_UNLOCK(sc);

	return (error);
}

static int
alpm_readw(device_t dev, u_char slave, char cmd, short *word)
{
	struct alpm_softc *sc = (struct alpm_softc *)device_get_softc(dev);
	int error;
	u_char high, low;

	ALPM_LOCK(sc);
	alpm_clear(sc);
	if (!alpm_idle(sc)) {
		ALPM_UNLOCK(sc);
		return (SMB_EBUSY);
	}

	ALPM_SMBOUTB(sc, SMBHADDR, slave | LSB);
	ALPM_SMBOUTB(sc, SMBCMD, SMBWRWORD);
	ALPM_SMBOUTB(sc, SMBHCMD, cmd);
	ALPM_SMBOUTB(sc, SMBSTART, 0xff);

	if ((error = alpm_wait(sc)) == SMB_ENOERR) {
		low = ALPM_SMBINB(sc, SMBHDATA);
		high = ALPM_SMBINB(sc, SMBHDATB);

		*word = ((high & 0xff) << 8) | (low & 0xff);
	}

	ALPM_DEBUG(printf("alpm: READW from 0x%x, cmd=0x%x, word=0x%x, error=0x%x\n", slave, cmd, *word, error));
	ALPM_UNLOCK(sc);

	return (error);
}

static int
alpm_bwrite(device_t dev, u_char slave, char cmd, u_char count, char *buf)
{
	struct alpm_softc *sc = (struct alpm_softc *)device_get_softc(dev);
	u_char i;
	int error;

	if (count < 1 || count > 32)
		return (SMB_EINVAL);

	ALPM_LOCK(sc);
	alpm_clear(sc);
	if(!alpm_idle(sc)) {
		ALPM_UNLOCK(sc);
		return (SMB_EBUSY);
	}

	ALPM_SMBOUTB(sc, SMBHADDR, slave & ~LSB);
	
	/* set the cmd and reset the
	 * 32-byte long internal buffer */
	ALPM_SMBOUTB(sc, SMBCMD, SMBWRBLOCK | SMB_BLK_CLR);

	ALPM_SMBOUTB(sc, SMBHDATA, count);

	/* fill the 32-byte internal buffer */
	for (i = 0; i < count; i++) {
		ALPM_SMBOUTB(sc, SMBHBLOCK, buf[i]);
		DELAY(2);
	}
	ALPM_SMBOUTB(sc, SMBHCMD, cmd);
	ALPM_SMBOUTB(sc, SMBSTART, 0xff);

	error = alpm_wait(sc);

	ALPM_DEBUG(printf("alpm: WRITEBLK to 0x%x, count=0x%x, cmd=0x%x, error=0x%x", slave, count, cmd, error));
	ALPM_UNLOCK(sc);

	return (error);
}

static int
alpm_bread(device_t dev, u_char slave, char cmd, u_char *count, char *buf)
{
	struct alpm_softc *sc = (struct alpm_softc *)device_get_softc(dev);
	u_char data, len, i;
	int error;

	if (*count < 1 || *count > 32)
		return (SMB_EINVAL);

	ALPM_LOCK(sc);
	alpm_clear(sc);
	if (!alpm_idle(sc)) {
		ALPM_UNLOCK(sc);
		return (SMB_EBUSY);
	}

	ALPM_SMBOUTB(sc, SMBHADDR, slave | LSB);
	
	/* set the cmd and reset the
	 * 32-byte long internal buffer */
	ALPM_SMBOUTB(sc, SMBCMD, SMBWRBLOCK | SMB_BLK_CLR);

	ALPM_SMBOUTB(sc, SMBHCMD, cmd);
	ALPM_SMBOUTB(sc, SMBSTART, 0xff);

	if ((error = alpm_wait(sc)) != SMB_ENOERR)
			goto error;

	len = ALPM_SMBINB(sc, SMBHDATA);

	/* read the 32-byte internal buffer */
	for (i = 0; i < len; i++) {
		data = ALPM_SMBINB(sc, SMBHBLOCK);
		if (i < *count)
			buf[i] = data;
		DELAY(2);
	}
	*count = len;

error:
	ALPM_DEBUG(printf("alpm: READBLK to 0x%x, count=0x%x, cmd=0x%x, error=0x%x", slave, *count, cmd, error));
	ALPM_UNLOCK(sc);

	return (error);
}

static devclass_t alpm_devclass;

static device_method_t alpm_methods[] = {
	/* device interface */
	DEVMETHOD(device_probe,		alpm_probe),
	DEVMETHOD(device_attach,	alpm_attach),
	DEVMETHOD(device_detach,	alpm_detach),
	
	/* smbus interface */
	DEVMETHOD(smbus_callback,	alpm_callback),
	DEVMETHOD(smbus_quick,		alpm_quick),
	DEVMETHOD(smbus_sendb,		alpm_sendb),
	DEVMETHOD(smbus_recvb,		alpm_recvb),
	DEVMETHOD(smbus_writeb,		alpm_writeb),
	DEVMETHOD(smbus_readb,		alpm_readb),
	DEVMETHOD(smbus_writew,		alpm_writew),
	DEVMETHOD(smbus_readw,		alpm_readw),
	DEVMETHOD(smbus_bwrite,		alpm_bwrite),
	DEVMETHOD(smbus_bread,		alpm_bread),
	
	{ 0, 0 }
};

static driver_t alpm_driver = {
	"alpm",
	alpm_methods,
	sizeof(struct alpm_softc)
};

DRIVER_MODULE(alpm, pci, alpm_driver, alpm_devclass, 0, 0);
DRIVER_MODULE(smbus, alpm, smbus_driver, smbus_devclass, 0, 0);
MODULE_DEPEND(alpm, pci, 1, 1, 1);
MODULE_DEPEND(alpm, smbus, SMBUS_MINVER, SMBUS_PREFVER, SMBUS_MAXVER);
MODULE_VERSION(alpm, 1);
