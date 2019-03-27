/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 The FreeBSD Project
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
#include <sys/capsicum.h>
#include <sys/file.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/proc.h>
#include <sys/systm.h>

#include <dev/tdfx/tdfx_linux.h>

LINUX_IOCTL_SET(tdfx, LINUX_IOCTL_TDFX_MIN, LINUX_IOCTL_TDFX_MAX);

/*
 * Linux emulation IOCTL for /dev/tdfx
 */
static int
linux_ioctl_tdfx(struct thread *td, struct linux_ioctl_args* args)
{
   cap_rights_t rights;
   int error = 0;
   u_long cmd = args->cmd & 0xffff;

   /* The structure passed to ioctl has two shorts, one int
      and one void*. */
   char d_pio[2*sizeof(short) + sizeof(int) + sizeof(void*)];

   struct file *fp;

   error = fget(td, args->fd, cap_rights_init(&rights, CAP_IOCTL), &fp);
   if (error != 0)
	   return (error);
   /* We simply copy the data and send it right to ioctl */
   copyin((caddr_t)args->arg, &d_pio, sizeof(d_pio));
   error = fo_ioctl(fp, cmd, (caddr_t)&d_pio, td->td_ucred, td);
   fdrop(fp, td);
   return error;
}

static int
tdfx_linux_modevent(struct module *mod __unused, int what, void *arg __unused)
{

	switch (what) {
	case MOD_LOAD:
	case MOD_UNLOAD:
		return (0);
	}
	return (EOPNOTSUPP);
}

static moduledata_t tdfx_linux_mod = {
	"tdfx_linux",
	tdfx_linux_modevent,
	0
};

/* As in SYSCALL_MODULE */
DECLARE_MODULE(tdfx_linux, tdfx_linux_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_VERSION(tdfx_linux, 1);
MODULE_DEPEND(tdfx_linux, tdfx, 1, 1, 1);
MODULE_DEPEND(tdfx_linux, linux, 1, 1, 1);
