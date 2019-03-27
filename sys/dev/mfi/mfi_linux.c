/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 IronPort Systems
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
#include <sys/systm.h>
#include <sys/capsicum.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/file.h>
#include <sys/proc.h>
#include <machine/bus.h>

#if defined(__amd64__) /* Assume amd64 wants 32 bit Linux */
#include <machine/../linux32/linux.h>
#include <machine/../linux32/linux32_proto.h>
#else
#include <machine/../linux/linux.h>
#include <machine/../linux/linux_proto.h>
#endif
#include <compat/linux/linux_ioctl.h>
#include <compat/linux/linux_util.h>

#include <dev/mfi/mfireg.h>
#include <dev/mfi/mfi_ioctl.h>

/* There are multiple ioctl number ranges that need to be handled */
#define MFI_LINUX_IOCTL_MIN  0x4d00
#define MFI_LINUX_IOCTL_MAX  0x4d04

static linux_ioctl_function_t mfi_linux_ioctl;
static struct linux_ioctl_handler mfi_linux_handler = {mfi_linux_ioctl,
						       MFI_LINUX_IOCTL_MIN,
						       MFI_LINUX_IOCTL_MAX};

SYSINIT  (mfi_register,   SI_SUB_KLD, SI_ORDER_MIDDLE,
	  linux_ioctl_register_handler, &mfi_linux_handler);
SYSUNINIT(mfi_unregister, SI_SUB_KLD, SI_ORDER_MIDDLE,
	  linux_ioctl_unregister_handler, &mfi_linux_handler);

static struct linux_device_handler mfi_device_handler =
	{ "mfi", "megaraid_sas", "mfi0", "megaraid_sas_ioctl_node", -1, 0, 1};

SYSINIT  (mfi_register2,   SI_SUB_KLD, SI_ORDER_MIDDLE,
	  linux_device_register_handler, &mfi_device_handler);
SYSUNINIT(mfi_unregister2, SI_SUB_KLD, SI_ORDER_MIDDLE,
	  linux_device_unregister_handler, &mfi_device_handler);

static int
mfi_linux_modevent(module_t mod, int cmd, void *data)
{
	return (0);
}

DEV_MODULE(mfi_linux, mfi_linux_modevent, NULL);
MODULE_DEPEND(mfi, linux, 1, 1, 1);

static int
mfi_linux_ioctl(struct thread *p, struct linux_ioctl_args *args)
{
	cap_rights_t rights;
	struct file *fp;
	int error;
	u_long cmd = args->cmd;

	switch (cmd) {
	case MFI_LINUX_CMD:
		cmd = MFI_LINUX_CMD_2;
		break;
	case MFI_LINUX_SET_AEN:
		cmd = MFI_LINUX_SET_AEN_2;
		break;
	}

	error = fget(p, args->fd, cap_rights_init(&rights, CAP_IOCTL), &fp);
	if (error != 0)
		return (error);
	error = fo_ioctl(fp, cmd, (caddr_t)args->arg, p->td_ucred, p);
	fdrop(fp, p);
	return (error);
}
