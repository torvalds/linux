/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Ed Schouten under sponsorship from the
 * FreeBSD Foundation.
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
#include <sys/consio.h>
#include <sys/kernel.h>
#include <sys/systm.h>

#include <dev/vt/vt.h>

static d_ioctl_t	consolectl_ioctl;

static struct cdevsw consolectl_cdevsw = {
	.d_version	= D_VERSION,
	.d_ioctl	= consolectl_ioctl,
	.d_name		= "consolectl",
};

static int
consolectl_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int flag,
    struct thread *td)
{

	switch (cmd) {
	case CONS_GETVERS:
		*(int*)data = 0x200;
		return 0;
	case CONS_MOUSECTL: {
		mouse_info_t *mi = (mouse_info_t*)data;

		sysmouse_process_event(mi);
		return (0);
	}
	default:
#ifdef VT_CONSOLECTL_DEBUG
		printf("consolectl: unknown ioctl: %c:%lx\n",
		    (char)IOCGROUP(cmd), IOCBASECMD(cmd));
#endif
		return (ENOIOCTL);
	}
}

static void
consolectl_drvinit(void *unused)
{

	if (!vty_enabled(VTY_VT))
		return;
	make_dev(&consolectl_cdevsw, 0, UID_ROOT, GID_WHEEL, 0600,
	    "consolectl");
}

SYSINIT(consolectl, SI_SUB_DRIVERS, SI_ORDER_MIDDLE, consolectl_drvinit, NULL);
