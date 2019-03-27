/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010-2011 Monthadar Al Jaberi, TerraNet AB
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 *
 * $FreeBSD$
 */
#include <sys/param.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/sysctl.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/ucred.h>
#include <sys/jail.h>

#include <sys/sockio.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/errno.h>
#include <sys/callout.h>
#include <sys/endian.h>
#include <sys/kthread.h>
#include <sys/taskqueue.h>
#include <sys/priv.h>
#include <sys/sysctl.h>

#include <machine/bus.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_llc.h>
#include <net/vnet.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_regdomain.h>

#include <net/bpf.h>


#include <sys/errno.h>
#include <sys/conf.h>   /* cdevsw struct */
#include <sys/uio.h>    /* uio struct */


#include <netinet/in.h>
#include <netinet/if_ether.h>

#include "if_wtapvar.h"
#include "if_wtapioctl.h"
#include "if_medium.h"
#include "wtap_hal/hal.h"

/* WTAP PLUGINS */
#include "plugins/visibility.h"

MALLOC_DEFINE(M_WTAP, "wtap", "wtap wireless simulator");
MALLOC_DEFINE(M_WTAP_PACKET, "wtap packet", "wtap wireless simulator packet");
MALLOC_DEFINE(M_WTAP_RXBUF, "wtap rxbuf",
    "wtap wireless simulator receive buffer");
MALLOC_DEFINE(M_WTAP_PLUGIN, "wtap plugin", "wtap wireless simulator plugin");

static struct wtap_hal		*hal;

/* Function prototypes */
static d_ioctl_t	wtap_ioctl;

static struct cdev *sdev;
static struct cdevsw wtap_cdevsw = {
	.d_version =	D_VERSION,
	.d_flags =	0,
	.d_ioctl =	wtap_ioctl,
	.d_name =	"wtapctl",
};

int
wtap_ioctl(struct cdev *dev, u_long cmd, caddr_t data,
    int fflag, struct thread *td)
{
	int error = 0;

	CURVNET_SET(CRED_TO_VNET(curthread->td_ucred));

	switch(cmd) {
	case WTAPIOCTLCRT:
		if(new_wtap(hal, *(int *)data))
			error = EINVAL;
		break;
	case WTAPIOCTLDEL:
		if(free_wtap(hal, *(int *)data))
			error = EINVAL;
		break;
	default:
		DWTAP_PRINTF("Unknown WTAP IOCTL\n");
		error = EINVAL;
	}

	CURVNET_RESTORE();
	return error;
}


/* The function called at load/unload. */
static int
event_handler(module_t module, int event, void *arg) 
{
	struct visibility_plugin *plugin;
	int e = 0; /* Error, 0 for normal return status */

	switch (event) {
	case MOD_LOAD:
		sdev = make_dev(&wtap_cdevsw,0,UID_ROOT,
		    GID_WHEEL,0600,(const char *)"wtapctl");
		hal = (struct wtap_hal *)malloc(sizeof(struct wtap_hal),
		    M_WTAP, M_NOWAIT | M_ZERO);
		bzero(hal, sizeof(struct wtap_hal));

		init_hal(hal);

		/* Setting up a simple plugin */
		plugin = (struct visibility_plugin *)malloc
		    (sizeof(struct visibility_plugin), M_WTAP_PLUGIN,
		    M_NOWAIT | M_ZERO);
		plugin->base.wp_hal  = hal;
		plugin->base.init = visibility_init;
		plugin->base.deinit = visibility_deinit;
		plugin->base.work = visibility_work;
		register_plugin(hal, (struct wtap_plugin *)plugin);

                printf("Loaded wtap wireless simulator\n");
                break;
	case MOD_UNLOAD:
		destroy_dev(sdev);
		deregister_plugin(hal);
		deinit_hal(hal);
		free(hal, M_WTAP);
		printf("Unloading wtap wireless simulator\n");
		break;
	default:
		e = EOPNOTSUPP; /* Error, Operation Not Supported */
		break;
	}

	return(e);
}

/* The second argument of DECLARE_MODULE. */
static moduledata_t wtap_conf = {
	"wtap",		/* module name */
	event_handler,	/* event handler */
	NULL		/* extra data */
};

DECLARE_MODULE(wtap, wtap_conf, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_DEPEND(wtap, wlan, 1, 1, 1);	/* 802.11 media layer */
