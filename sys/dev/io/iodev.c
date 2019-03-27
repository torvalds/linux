/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004 Mark R V Murray
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
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/ioccom.h>
#include <sys/module.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/systm.h>

#include <machine/iodev.h>

#include <dev/io/iodev.h>

static int	 ioopen(struct cdev *dev, int flags, int fmt,
		    struct thread *td);
static int	 ioclose(struct cdev *dev, int flags, int fmt,
		    struct thread *td);
static int	 ioioctl(struct cdev *dev, u_long cmd, caddr_t data,
		    int fflag, struct thread *td);

static int	 iopio_read(struct iodev_pio_req *req);
static int	 iopio_write(struct iodev_pio_req *req);

static struct cdev *iodev;

static struct cdevsw io_cdevsw = {
	.d_version =	D_VERSION,
	.d_open =	ioopen,
	.d_close =	ioclose,
	.d_ioctl =	ioioctl,
	.d_name =	"io",
};

/* ARGSUSED */
static int
ioopen(struct cdev *dev __unused, int flags __unused, int fmt __unused,
    struct thread *td)
{
	int error;

	error = priv_check(td, PRIV_IO);
	if (error != 0)
		return (error);
	error = securelevel_gt(td->td_ucred, 0);
	if (error != 0)
		return (error);
	error = iodev_open(td);

        return (error);
}

/* ARGSUSED */
static int
ioclose(struct cdev *dev __unused, int flags __unused, int fmt __unused,
    struct thread *td)
{

	return (iodev_close(td));
}

/* ARGSUSED */
static int
ioioctl(struct cdev *dev __unused, u_long cmd, caddr_t data,
    int fflag __unused, struct thread *td __unused)
{
	struct iodev_pio_req *pio_req;
	int error;

	switch (cmd) {
	case IODEV_PIO:
		pio_req = (struct iodev_pio_req *)data;
		switch (pio_req->access) {
		case IODEV_PIO_READ:
			error = iopio_read(pio_req);
			break;
		case IODEV_PIO_WRITE:
			error = iopio_write(pio_req);
			break;
		default:
			error = EINVAL;
			break;
		}
		break;
	default:
		error = iodev_ioctl(cmd, data);
	}

	return (error);
}

static int
iopio_read(struct iodev_pio_req *req)
{

	switch (req->width) {
	case 1:
		req->val = iodev_read_1(req->port);
		break;
	case 2:
		if (req->port & 1) {
			req->val = iodev_read_1(req->port);
			req->val |= iodev_read_1(req->port + 1) << 8;
		} else
			req->val = iodev_read_2(req->port);
		break;
	case 4:
		if (req->port & 1) {
			req->val = iodev_read_1(req->port);
			req->val |= iodev_read_2(req->port + 1) << 8;
			req->val |= iodev_read_1(req->port + 3) << 24;
		} else if (req->port & 2) {
			req->val = iodev_read_2(req->port);
			req->val |= iodev_read_2(req->port + 2) << 16;
		} else
			req->val = iodev_read_4(req->port);
		break;
	default:
		return (EINVAL);
	}

	return (0);
}

static int
iopio_write(struct iodev_pio_req *req)
{

	switch (req->width) {
	case 1:
		iodev_write_1(req->port, req->val);
		break;
	case 2:
		if (req->port & 1) {
			iodev_write_1(req->port, req->val);
			iodev_write_1(req->port + 1, req->val >> 8);
		} else
			iodev_write_2(req->port, req->val);
		break;
	case 4:
		if (req->port & 1) {
			iodev_write_1(req->port, req->val);
			iodev_write_2(req->port + 1, req->val >> 8);
			iodev_write_1(req->port + 3, req->val >> 24);
		} else if (req->port & 2) {
			iodev_write_2(req->port, req->val);
			iodev_write_2(req->port + 2, req->val >> 16);
		} else
			iodev_write_4(req->port, req->val);
		break;
	default:
		return (EINVAL);
	}

	return (0);
}

/* ARGSUSED */
static int
io_modevent(module_t mod __unused, int type, void *data __unused)
{
	switch(type) {
	case MOD_LOAD:
		if (bootverbose)
			printf("io: <I/O>\n");
		iodev = make_dev(&io_cdevsw, 0,
			UID_ROOT, GID_WHEEL, 0600, "io");
		break;

	case MOD_UNLOAD:
		destroy_dev(iodev);
		break;

	case MOD_SHUTDOWN:
		break;

	default:
		return(EOPNOTSUPP);
		break;

	}

	return (0);
}

DEV_MODULE(io, io_modevent, NULL);
MODULE_VERSION(io, 1);
