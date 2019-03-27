/* 08 Nov 1998*/
/*-
 * cdevmod.c - a sample kld module implementing a character device driver.
 *
 * 08 Nov 1998  Rajesh Vaidheeswarran
 *
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1998 Rajesh Vaidheeswarran
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Rajesh Vaidheeswarran.
 * 4. The name Rajesh Vaidheeswarran may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY RAJESH VAIDHEESWARRAN ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE RAJESH VAIDHEESWARRAN BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Copyright (c) 1993 Terrence R. Lambert.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Terrence R. Lambert.
 * 4. The name Terrence R. Lambert may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY TERRENCE R. LAMBERT ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE TERRENCE R. LAMBERT BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *
 * $FreeBSD$
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/conf.h>

#include "cdev.h"

static struct cdevsw my_devsw = {
	/* version */	.d_version = D_VERSION,
	/* open */	.d_open = mydev_open,
	/* close */	.d_close = mydev_close,
	/* read */	.d_read = mydev_read,
	/* write */	.d_write = mydev_write,
	/* ioctl */	.d_ioctl = mydev_ioctl,
	/* name */	.d_name = "cdev"
};

/* 
 * Used as the variable that is the reference to our device
 * in devfs... we must keep this variable sane until we 
 * call kldunload.
 */
static struct cdev *sdev;

/*
 * This function is called each time the module is loaded or unloaded.
 * Since we are a miscellaneous module, we have to provide whatever
 * code is necessary to patch ourselves into the area we are being
 * loaded to change.
 *
 * The stat information is basically common to all modules, so there
 * is no real issue involved with stat; we will leave it lkm_nullcmd(),
 * since we don't have to do anything about it.
 */

static int
cdev_load(module_t mod, int cmd, void *arg)
{
    int  err = 0;
    struct make_dev_args mda;

    switch (cmd) {
    case MOD_LOAD:
	
	/* Do any initialization that you should do with the kernel */
	
	/* if we make it to here, print copyright on console*/
	printf("\nSample Loaded kld character device driver\n");
	printf("Copyright (c) 1998\n");
	printf("Rajesh Vaidheeswarran\n");
	printf("All rights reserved\n");

	make_dev_args_init(&mda);
	mda.mda_devsw = &my_devsw;
	mda.mda_uid = UID_ROOT;
	mda.mda_gid = GID_WHEEL;
	mda.mda_mode = 0600;
	err = make_dev_s(&mda, &sdev, "cdev");
	break;

    case MOD_UNLOAD:
	printf("Unloaded kld character device driver\n");
	destroy_dev(sdev);
	break;		/* Success*/

    default:	/* we only understand load/unload*/
	err = EOPNOTSUPP;
	break;
    }

    return(err);
}

/* Now declare the module to the system */

DEV_MODULE(cdev, cdev_load, NULL);
