/*
 * Copyright (c) 2015, AVAGO Tech. All rights reserved. Author: Kashyap Desai,
 * Copyright (c) 2014, LSI Corp. All rights reserved. Author: Kashyap Desai,
 * Sibananda Sahu Support: freebsdraid@avagotech.com
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer. 2. Redistributions
 * in binary form must reproduce the above copyright notice, this list of
 * conditions and the following disclaimer in the documentation and/or other
 * materials provided with the distribution. 3. Neither the name of the
 * <ORGANIZATION> nor the names of its contributors may be used to endorse or
 * promote products derived from this software without specific prior written
 * permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation are
 * those of the authors and should not be interpreted as representing
 * official policies,either expressed or implied, of the FreeBSD Project.
 *
 * Send feedback to: <megaraidfbsd@avagotech.com> Mail to: AVAGO TECHNOLOGIES, 1621
 * Barber Lane, Milpitas, CA 95035 ATTN: MegaRaid FreeBSD
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>

#if (__FreeBSD_version >= 1001511)
#include <sys/capsicum.h>
#elif (__FreeBSD_version > 900000)
#include <sys/capability.h>
#endif

#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/file.h>
#include <sys/proc.h>
#include <machine/bus.h>

#if defined(__amd64__)			/* Assume amd64 wants 32 bit Linux */
#include <machine/../linux32/linux.h>
#include <machine/../linux32/linux32_proto.h>
#else
#include <machine/../linux/linux.h>
#include <machine/../linux/linux_proto.h>
#endif
#include <compat/linux/linux_ioctl.h>
#include <compat/linux/linux_util.h>

#include <dev/mrsas/mrsas.h>
#undef COMPAT_FREEBSD32
#include <dev/mrsas/mrsas_ioctl.h>

/* There are multiple ioctl number ranges that need to be handled */
#define	MRSAS_LINUX_IOCTL_MIN  0x4d00
#define	MRSAS_LINUX_IOCTL_MAX  0x4d01

static linux_ioctl_function_t mrsas_linux_ioctl;
static struct linux_ioctl_handler mrsas_linux_handler = {mrsas_linux_ioctl,
	MRSAS_LINUX_IOCTL_MIN,
MRSAS_LINUX_IOCTL_MAX};

SYSINIT(mrsas_register, SI_SUB_KLD, SI_ORDER_MIDDLE,
    linux_ioctl_register_handler, &mrsas_linux_handler);
SYSUNINIT(mrsas_unregister, SI_SUB_KLD, SI_ORDER_MIDDLE,
    linux_ioctl_unregister_handler, &mrsas_linux_handler);

static struct linux_device_handler mrsas_device_handler =
{"mrsas", "megaraid_sas", "mrsas0", "megaraid_sas_ioctl_node", -1, 0, 1};

SYSINIT(mrsas_register2, SI_SUB_KLD, SI_ORDER_MIDDLE,
    linux_device_register_handler, &mrsas_device_handler);
SYSUNINIT(mrsas_unregister2, SI_SUB_KLD, SI_ORDER_MIDDLE,
    linux_device_unregister_handler, &mrsas_device_handler);

static int
mrsas_linux_modevent(module_t mod __unused, int cmd __unused, void *data __unused)
{
	return (0);
}

/*
 * mrsas_linux_ioctl:	linux emulator IOCtl commands entry point.
 *
 * This function is the entry point for IOCtls from linux binaries.
 * It calls the mrsas_ioctl function for processing
 * depending on the IOCTL command received.
 */
static int
mrsas_linux_ioctl(struct thread *p, struct linux_ioctl_args *args)
{
#if (__FreeBSD_version >= 1000000)
	cap_rights_t rights;

#endif
	struct file *fp;
	int error;
	u_long cmd = args->cmd;

	if (cmd != MRSAS_LINUX_CMD32) {
		error = ENOTSUP;
		goto END;
	}
#if (__FreeBSD_version >= 1000000)
	error = fget(p, args->fd, cap_rights_init(&rights, CAP_IOCTL), &fp);
#elif (__FreeBSD_version <= 900000)
	error = fget(p, args->fd, &fp);
#else					/* For FreeBSD version greater than
					 * 9.0.0 but less than 10.0.0 */
	error = fget(p, args->fd, CAP_IOCTL, &fp);
#endif
	if (error != 0)
		goto END;

	error = fo_ioctl(fp, cmd, (caddr_t)args->arg, p->td_ucred, p);
	fdrop(fp, p);
END:
	return (error);
}

DEV_MODULE(mrsas_linux, mrsas_linux_modevent, NULL);
MODULE_DEPEND(mrsas, linux, 1, 1, 1);
