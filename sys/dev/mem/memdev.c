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
#include <sys/fcntl.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/memrange.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/signalvar.h>
#include <sys/systm.h>
#include <sys/uio.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/memdev.h>

static struct cdev *memdev, *kmemdev;

static struct cdevsw mem_cdevsw = {
	.d_version =	D_VERSION,
	.d_flags =	D_MEM,
	.d_open =	memopen,
	.d_read =	memrw,
	.d_write =	memrw,
	.d_ioctl =	memioctl,
	.d_mmap =	memmmap,
	.d_name =	"mem",
};

/* ARGSUSED */
int
memopen(struct cdev *dev __unused, int flags, int fmt __unused,
    struct thread *td)
{
	int error = 0;

	if (flags & FREAD)
		error = priv_check(td, PRIV_KMEM_READ);
	if (flags & FWRITE) {
		if (error == 0)
			error = priv_check(td, PRIV_KMEM_WRITE);
		if (error == 0)
			error = securelevel_gt(td->td_ucred, 0);
	}

	return (error);
}

/* ARGSUSED */
static int
mem_modevent(module_t mod __unused, int type, void *data __unused)
{
	switch(type) {
	case MOD_LOAD:
		if (bootverbose)
			printf("mem: <memory>\n");
		mem_range_init();
		memdev = make_dev(&mem_cdevsw, CDEV_MINOR_MEM,
			UID_ROOT, GID_KMEM, 0640, "mem");
		kmemdev = make_dev(&mem_cdevsw, CDEV_MINOR_KMEM,
			UID_ROOT, GID_KMEM, 0640, "kmem");
		break;

	case MOD_UNLOAD:
		mem_range_destroy();
		destroy_dev(memdev);
		destroy_dev(kmemdev);
		break;

	case MOD_SHUTDOWN:
		break;

	default:
		return(EOPNOTSUPP);

	}

	return (0);
}

DEV_MODULE(mem, mem_modevent, NULL);
MODULE_VERSION(mem, 1);
