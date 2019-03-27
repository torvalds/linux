/*-
 * Copyright (c) 2015 Oleksandr Tymoshenko <gonzo@freebsd.org>
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
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/ioccom.h>
#include <sys/conf.h>
#include <sys/proc.h>

#include <arm/broadcom/bcm2835/bcm2835_mbox_prop.h>

MALLOC_DECLARE(M_VCIO);
MALLOC_DEFINE(M_VCIO, "vcio", "VCIO temporary buffers");

static struct cdev *sdev;
static d_ioctl_t vcio_ioctl;

static struct cdevsw vcio_devsw = {
	/* version */	.d_version = D_VERSION,
	/* ioctl */	.d_ioctl = vcio_ioctl,
};

#define VCIO_IOC_MAGIC 100
#define IOCTL_MBOX_PROPERTY _IOWR(VCIO_IOC_MAGIC, 0, char *)

int
vcio_ioctl(struct cdev *dev, u_long cmd, caddr_t arg, int mode,
    struct thread *td)
{
    int error;
    void *ptr;
    uint32_t size;
    uint8_t *property;

    error = 0;
    switch(cmd) {
    case IOCTL_MBOX_PROPERTY:
    	memcpy (&ptr, arg, sizeof(ptr));
	error = copyin(ptr, &size, sizeof(size));

	if (error != 0)
		break;
	property = malloc(size, M_VCIO, M_WAITOK);

	error = copyin(ptr, property, size);
	if (error) {
		free(property, M_VCIO);
		break;
	}

	error = bcm2835_mbox_property(property, size);
	if (error) {
		free(property, M_VCIO);
		break;
	}

	error = copyout(property, ptr, size);
	free(property, M_VCIO);

	break;
    default:
	error = EINVAL;
	break;
    }
    return (error);
}

static int
vcio_load(module_t mod, int cmd, void *arg)
{
    int  err = 0;

    switch (cmd) {
    case MOD_LOAD:
	sdev = make_dev(&vcio_devsw, 0, UID_ROOT, GID_WHEEL, 0600, "vcio");
	break;

    case MOD_UNLOAD:
	destroy_dev(sdev);
	break;

    default:
	err = EOPNOTSUPP;
	break;
    }

    return(err);
}

DEV_MODULE(vcio, vcio_load, NULL);
MODULE_DEPEND(vcio, mbox, 1, 1, 1);
