/*-
 * Copyright (c) 2014 Vassilis Laganakos
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
#include <sys/exec.h>
#include <sys/imgact.h>
#include <sys/imgact_elf.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/eventhandler.h>
#include <sys/sx.h>
#include <sys/sysctl.h>

#include <compat/linux/linux_emul.h>
#include <compat/linux/linux_ioctl.h>
#include <compat/linux/linux_mib.h>
#include <compat/linux/linux_util.h>

FEATURE(linuxulator_v4l, "V4L ioctl wrapper support in the linuxulator");
FEATURE(linuxulator_v4l2, "V4L2 ioctl wrapper support in the linuxulator");

MODULE_VERSION(linux_common, 1);

SET_DECLARE(linux_device_handler_set, struct linux_device_handler);

TAILQ_HEAD(, linux_ioctl_handler_element) linux_ioctl_handlers =
    TAILQ_HEAD_INITIALIZER(linux_ioctl_handlers);
struct sx linux_ioctl_sx;
SX_SYSINIT(linux_ioctl, &linux_ioctl_sx, "Linux ioctl handlers");

static eventhandler_tag linux_exec_tag;
static eventhandler_tag linux_thread_dtor_tag;
static eventhandler_tag	linux_exit_tag;


static int
linux_common_modevent(module_t mod, int type, void *data)
{
	struct linux_device_handler **ldhp;

	switch(type) {
	case MOD_LOAD:
		linux_osd_jail_register();
		linux_exit_tag = EVENTHANDLER_REGISTER(process_exit,
		    linux_proc_exit, NULL, 1000);
		linux_exec_tag = EVENTHANDLER_REGISTER(process_exec,
		    linux_proc_exec, NULL, 1000);
		linux_thread_dtor_tag = EVENTHANDLER_REGISTER(thread_dtor,
		    linux_thread_dtor, NULL, EVENTHANDLER_PRI_ANY);
		SET_FOREACH(ldhp, linux_device_handler_set)
			linux_device_register_handler(*ldhp);
		break;
	case MOD_UNLOAD:
		linux_osd_jail_deregister();
		SET_FOREACH(ldhp, linux_device_handler_set)
			linux_device_unregister_handler(*ldhp);
		EVENTHANDLER_DEREGISTER(process_exit, linux_exit_tag);
		EVENTHANDLER_DEREGISTER(process_exec, linux_exec_tag);
		EVENTHANDLER_DEREGISTER(thread_dtor, linux_thread_dtor_tag);
		break;
	default:
		return (EOPNOTSUPP);
	}
	return (0);
}

static moduledata_t linux_common_mod = {
	"linuxcommon",
	linux_common_modevent,
	0
};

DECLARE_MODULE(linuxcommon, linux_common_mod, SI_SUB_EXEC, SI_ORDER_ANY);
