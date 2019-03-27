/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000 Mark R. V. Murray & Jeroen C. van Gelderen
 * Copyright (c) 2001-2004 Mark R. V. Murray
 * Copyright (c) 2014 Eitan Adler
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/disk.h>
#include <sys/bus.h>
#include <sys/filio.h>

#include <machine/bus.h>
#include <machine/vmparam.h>

/* For use with destroy_dev(9). */
static struct cdev *full_dev;
static struct cdev *null_dev;
static struct cdev *zero_dev;

static d_write_t full_write;
static d_write_t null_write;
static d_ioctl_t null_ioctl;
static d_ioctl_t zero_ioctl;
static d_read_t zero_read;

static struct cdevsw full_cdevsw = {
	.d_version =	D_VERSION,
	.d_read =	zero_read,
	.d_write =	full_write,
	.d_ioctl =	zero_ioctl,
	.d_name =	"full",
};

static struct cdevsw null_cdevsw = {
	.d_version =	D_VERSION,
	.d_read =	(d_read_t *)nullop,
	.d_write =	null_write,
	.d_ioctl =	null_ioctl,
	.d_name =	"null",
};

static struct cdevsw zero_cdevsw = {
	.d_version =	D_VERSION,
	.d_read =	zero_read,
	.d_write =	null_write,
	.d_ioctl =	zero_ioctl,
	.d_name =	"zero",
	.d_flags =	D_MMAP_ANON,
};



/* ARGSUSED */
static int
full_write(struct cdev *dev __unused, struct uio *uio __unused, int flags __unused)
{

	return (ENOSPC);
}

/* ARGSUSED */
static int
null_write(struct cdev *dev __unused, struct uio *uio, int flags __unused)
{
	uio->uio_resid = 0;

	return (0);
}

/* ARGSUSED */
static int
null_ioctl(struct cdev *dev __unused, u_long cmd, caddr_t data __unused,
    int flags __unused, struct thread *td)
{
	int error;

	error = 0;
	switch (cmd) {
#ifdef COMPAT_FREEBSD11
	case DIOCSKERNELDUMP_FREEBSD11:
#endif
	case DIOCSKERNELDUMP:
		error = clear_dumper(td);
		break;
	case FIONBIO:
		break;
	case FIOASYNC:
		if (*(int *)data != 0)
			error = EINVAL;
		break;
	default:
		error = ENOIOCTL;
	}
	return (error);
}

/* ARGSUSED */
static int
zero_ioctl(struct cdev *dev __unused, u_long cmd, caddr_t data __unused,
	   int flags __unused, struct thread *td)
{
	int error;
	error = 0;

	switch (cmd) {
	case FIONBIO:
		break;
	case FIOASYNC:
		if (*(int *)data != 0)
			error = EINVAL;
		break;
	default:
		error = ENOIOCTL;
	}
	return (error);
}


/* ARGSUSED */
static int
zero_read(struct cdev *dev __unused, struct uio *uio, int flags __unused)
{
	void *zbuf;
	ssize_t len;
	int error = 0;

	KASSERT(uio->uio_rw == UIO_READ,
	    ("Can't be in %s for write", __func__));
	zbuf = __DECONST(void *, zero_region);
	while (uio->uio_resid > 0 && error == 0) {
		len = uio->uio_resid;
		if (len > ZERO_REGION_SIZE)
			len = ZERO_REGION_SIZE;
		error = uiomove(zbuf, len, uio);
	}

	return (error);
}

/* ARGSUSED */
static int
null_modevent(module_t mod __unused, int type, void *data __unused)
{
	switch(type) {
	case MOD_LOAD:
		if (bootverbose)
			printf("null: <full device, null device, zero device>\n");
		full_dev = make_dev_credf(MAKEDEV_ETERNAL_KLD, &full_cdevsw, 0,
		    NULL, UID_ROOT, GID_WHEEL, 0666, "full");
		null_dev = make_dev_credf(MAKEDEV_ETERNAL_KLD, &null_cdevsw, 0,
		    NULL, UID_ROOT, GID_WHEEL, 0666, "null");
		zero_dev = make_dev_credf(MAKEDEV_ETERNAL_KLD, &zero_cdevsw, 0,
		    NULL, UID_ROOT, GID_WHEEL, 0666, "zero");
		break;

	case MOD_UNLOAD:
		destroy_dev(full_dev);
		destroy_dev(null_dev);
		destroy_dev(zero_dev);
		break;

	case MOD_SHUTDOWN:
		break;

	default:
		return (EOPNOTSUPP);
	}

	return (0);
}

DEV_MODULE(null, null_modevent, NULL);
MODULE_VERSION(null, 1);
