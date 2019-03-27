/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1998, 2001 Nicolas Souchu
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
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/fcntl.h>

#include <dev/smbus/smbconf.h>
#include <dev/smbus/smbus.h>
#include <dev/smbus/smb.h>

#include "smbus_if.h"

#define SMB_OLD_READB	_IOW('i', 7, struct smbcmd)
#define SMB_OLD_READW	_IOW('i', 8, struct smbcmd)
#define SMB_OLD_PCALL	_IOW('i', 9, struct smbcmd)

struct smb_softc {
	device_t sc_dev;
	struct cdev *sc_devnode;
};

static void smb_identify(driver_t *driver, device_t parent);
static int smb_probe(device_t);
static int smb_attach(device_t);
static int smb_detach(device_t);

static devclass_t smb_devclass;

static device_method_t smb_methods[] = {
	/* device interface */
	DEVMETHOD(device_identify,	smb_identify),
	DEVMETHOD(device_probe,		smb_probe),
	DEVMETHOD(device_attach,	smb_attach),
	DEVMETHOD(device_detach,	smb_detach),

	/* smbus interface */
	DEVMETHOD(smbus_intr,		smbus_generic_intr),

	{ 0, 0 }
};

static driver_t smb_driver = {
	"smb",
	smb_methods,
	sizeof(struct smb_softc),
};

static	d_ioctl_t	smbioctl;

static struct cdevsw smb_cdevsw = {
	.d_version =	D_VERSION,
	.d_flags =	D_TRACKCLOSE,
	.d_ioctl =	smbioctl,
	.d_name =	"smb",
};

static void
smb_identify(driver_t *driver, device_t parent)
{

	if (device_find_child(parent, "smb", -1) == NULL)
		BUS_ADD_CHILD(parent, 0, "smb", -1);
}

static int
smb_probe(device_t dev)
{
	if (smbus_get_addr(dev) != -1)
		return (ENXIO);

	device_set_desc(dev, "SMBus generic I/O");
	return (BUS_PROBE_NOWILDCARD);
}
	
static int
smb_attach(device_t dev)
{
	struct smb_softc *sc;
	struct make_dev_args mda;
	int error;

	sc = device_get_softc(dev);
	sc->sc_dev = dev;

	make_dev_args_init(&mda);
	mda.mda_devsw = &smb_cdevsw;
	mda.mda_unit = device_get_unit(dev);
	mda.mda_uid = UID_ROOT;
	mda.mda_gid = GID_WHEEL;
	mda.mda_mode = 0600;
	mda.mda_si_drv1 = sc;
	error = make_dev_s(&mda, &sc->sc_devnode, "smb%d", mda.mda_unit);
	return (error);
}

static int
smb_detach(device_t dev)
{
	struct smb_softc *sc;

	sc = device_get_softc(dev);
	destroy_dev(sc->sc_devnode);
	return (0);
}

static int
smbioctl(struct cdev *dev, u_long cmd, caddr_t data, int flags, struct thread *td)
{
	char buf[SMB_MAXBLOCKSIZE];
	device_t parent;
	struct smbcmd *s = (struct smbcmd *)data;
	struct smb_softc *sc = dev->si_drv1;
	device_t smbdev = sc->sc_dev;
	int error;
	int unit;
	u_char bcount;

	/*
	 * If a specific slave device is being used, override any passed-in
	 * slave.
	 */
	unit = dev2unit(dev);
	if (unit & 0x0400)
		s->slave = unit & 0x03ff;

	parent = device_get_parent(smbdev);

	/* Make sure that LSB bit is cleared. */
	if (s->slave & 0x1)
		return (EINVAL);

	/* Allocate the bus. */
	if ((error = smbus_request_bus(parent, smbdev,
			(flags & O_NONBLOCK) ? SMB_DONTWAIT : (SMB_WAIT | SMB_INTR))))
		return (error);

	switch (cmd) {
	case SMB_QUICK_WRITE:
		error = smbus_error(smbus_quick(parent, s->slave, SMB_QWRITE));
		break;

	case SMB_QUICK_READ:
		error = smbus_error(smbus_quick(parent, s->slave, SMB_QREAD));
		break;

	case SMB_SENDB:
		error = smbus_error(smbus_sendb(parent, s->slave, s->cmd));
		break;

	case SMB_RECVB:
		error = smbus_error(smbus_recvb(parent, s->slave, &s->cmd));
		break;

	case SMB_WRITEB:
		error = smbus_error(smbus_writeb(parent, s->slave, s->cmd,
						s->wdata.byte));
		break;

	case SMB_WRITEW:
		error = smbus_error(smbus_writew(parent, s->slave,
						s->cmd, s->wdata.word));
		break;

	case SMB_OLD_READB:
	case SMB_READB:
		/* NB: for SMB_OLD_READB the read data goes to rbuf only. */
		error = smbus_error(smbus_readb(parent, s->slave, s->cmd,
		    &s->rdata.byte));
		if (error)
			break;
		if (s->rbuf && s->rcount >= 1) {
			error = copyout(&s->rdata.byte, s->rbuf, 1);
			s->rcount = 1;
		}
		break;

	case SMB_OLD_READW:
	case SMB_READW:
		/* NB: for SMB_OLD_READW the read data goes to rbuf only. */
		error = smbus_error(smbus_readw(parent, s->slave, s->cmd,
		    &s->rdata.word));
		if (error)
			break;
		if (s->rbuf && s->rcount >= 2) {
			buf[0] = (u_char)s->rdata.word;
			buf[1] = (u_char)(s->rdata.word >> 8);
			error = copyout(buf, s->rbuf, 2);
			s->rcount = 2;
		}
		break;

	case SMB_OLD_PCALL:
	case SMB_PCALL:
		/* NB: for SMB_OLD_PCALL the read data goes to rbuf only. */
		error = smbus_error(smbus_pcall(parent, s->slave, s->cmd,
		    s->wdata.word, &s->rdata.word));
		if (error)
			break;
		if (s->rbuf && s->rcount >= 2) {
			buf[0] = (u_char)s->rdata.word;
			buf[1] = (u_char)(s->rdata.word >> 8);
			error = copyout(buf, s->rbuf, 2);
			s->rcount = 2;
		}

		break;

	case SMB_BWRITE:
		if (s->wcount < 0) {
			error = EINVAL;
			break;
		}
		if (s->wcount > SMB_MAXBLOCKSIZE)
			s->wcount = SMB_MAXBLOCKSIZE;
		if (s->wcount)
			error = copyin(s->wbuf, buf, s->wcount);
		if (error)
			break;
		error = smbus_error(smbus_bwrite(parent, s->slave, s->cmd,
		    s->wcount, buf));
		break;

	case SMB_BREAD:
		if (s->rcount < 0) {
			error = EINVAL;
			break;
		}
		if (s->rcount > SMB_MAXBLOCKSIZE)
			s->rcount = SMB_MAXBLOCKSIZE;
		error = smbus_error(smbus_bread(parent, s->slave, s->cmd,
		    &bcount, buf));
		if (error)
			break;
		if (s->rcount > bcount)
			s->rcount = bcount;
		error = copyout(buf, s->rbuf, s->rcount);
		break;

	default:
		error = ENOTTY;
	}

	smbus_release_bus(parent, smbdev);

	return (error);
}

DRIVER_MODULE(smb, smbus, smb_driver, smb_devclass, 0, 0);
MODULE_DEPEND(smb, smbus, SMBUS_MINVER, SMBUS_PREFVER, SMBUS_MAXVER);
MODULE_VERSION(smb, 1);
