/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 IronPort Systems Inc. <ambrisko@ironport.com>
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

/*
 * Linux ioctl handler for the ipmi device driver
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/capsicum.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/file.h>
#include <sys/proc.h>
#ifdef __amd64__
#include <machine/../linux32/linux.h>
#include <machine/../linux32/linux32_proto.h>
#else
#include <machine/../linux/linux.h>
#include <machine/../linux/linux_proto.h>
#endif
#include <compat/linux/linux_ioctl.h>
#include <sys/ioccom.h>
#include <sys/ipmi.h>

/* There are multiple ioctl number ranges that need to be handled */
#define IPMI_LINUX_IOCTL_MIN  0x690b
#define IPMI_LINUX_IOCTL_MAX  0x6915

/* Linux versions of ioctl's */
#define L_IPMICTL_RECEIVE_MSG_TRUNC       _IOWR(IPMI_IOC_MAGIC, 11, struct ipmi_recv)
#define L_IPMICTL_RECEIVE_MSG             _IOWR(IPMI_IOC_MAGIC, 12, struct ipmi_recv)
#define L_IPMICTL_SEND_COMMAND            _IOW(IPMI_IOC_MAGIC, 13, struct ipmi_req)
#define L_IPMICTL_REGISTER_FOR_CMD        _IOW(IPMI_IOC_MAGIC, 14, struct ipmi_cmdspec)
#define L_IPMICTL_UNREGISTER_FOR_CMD      _IOW(IPMI_IOC_MAGIC, 15, struct ipmi_cmdspec)
#define L_IPMICTL_SET_GETS_EVENTS_CMD     _IOW(IPMI_IOC_MAGIC, 16, int)
#define L_IPMICTL_SET_MY_ADDRESS_CMD      _IOW(IPMI_IOC_MAGIC, 17, unsigned int)
#define L_IPMICTL_GET_MY_ADDRESS_CMD      _IOW(IPMI_IOC_MAGIC, 18, unsigned int)
#define L_IPMICTL_SET_MY_LUN_CMD          _IOW(IPMI_IOC_MAGIC, 19, unsigned int)
#define L_IPMICTL_GET_MY_LUN_CMD          _IOW(IPMI_IOC_MAGIC, 20, unsigned int)

static linux_ioctl_function_t ipmi_linux_ioctl;
static struct linux_ioctl_handler ipmi_linux_handler = {ipmi_linux_ioctl,
						       IPMI_LINUX_IOCTL_MIN,
						       IPMI_LINUX_IOCTL_MAX};

SYSINIT  (ipmi_linux_register,   SI_SUB_KLD, SI_ORDER_MIDDLE,
	  linux_ioctl_register_handler, &ipmi_linux_handler);
SYSUNINIT(ipmi_linux_unregister, SI_SUB_KLD, SI_ORDER_MIDDLE,
	  linux_ioctl_unregister_handler, &ipmi_linux_handler);

static int
ipmi_linux_modevent(module_t mod, int type, void *data)
{
	/* Do we care about any specific load/unload actions? */
	return (0);
}

DEV_MODULE(ipmi_linux, ipmi_linux_modevent, NULL);
MODULE_DEPEND(ipmi_linux, linux, 1, 1, 1);

static int
ipmi_linux_ioctl(struct thread *td, struct linux_ioctl_args *args)
{
	cap_rights_t rights;
	struct file *fp;
	u_long cmd;
	int error;

	error = fget(td, args->fd, cap_rights_init(&rights, CAP_IOCTL), &fp);
	if (error != 0)
		return (error);
	cmd = args->cmd;

	switch(cmd) {
	case L_IPMICTL_GET_MY_ADDRESS_CMD:
		cmd = IPMICTL_GET_MY_ADDRESS_CMD;
		break;
	case L_IPMICTL_GET_MY_LUN_CMD:
		cmd = IPMICTL_GET_MY_LUN_CMD;
		break;
	}
	/*
	 * Pass the ioctl off to our standard handler.
	 */
	error = (fo_ioctl(fp, cmd, (caddr_t)args->arg, td->td_ucred, td));
	fdrop(fp, td);
	return (error);
}
