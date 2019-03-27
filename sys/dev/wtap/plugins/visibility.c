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

#include "visibility.h"

/* Function prototypes */
static d_ioctl_t	vis_ioctl;

static struct cdevsw vis_cdevsw = {
	.d_version =	D_VERSION,
	.d_flags =	0,
	.d_ioctl =	vis_ioctl,
	.d_name =	"visctl",
};

void
visibility_init(struct wtap_plugin *plugin)
{
	struct visibility_plugin *vis_plugin;

	vis_plugin = (struct visibility_plugin *) plugin;
	plugin->wp_sdev = make_dev(&vis_cdevsw,0,UID_ROOT,GID_WHEEL,0600,
	    (const char *)"visctl");
	plugin->wp_sdev->si_drv1 = vis_plugin;
	mtx_init(&vis_plugin->pl_mtx, "visibility_plugin mtx",
	    NULL, MTX_DEF | MTX_RECURSE);
	printf("Using visibility wtap plugin...\n");
}

void
visibility_deinit(struct wtap_plugin *plugin)
{
	struct visibility_plugin *vis_plugin;

	vis_plugin = (struct visibility_plugin *) plugin;
	destroy_dev(plugin->wp_sdev);
	mtx_destroy(&vis_plugin->pl_mtx);
	free(vis_plugin, M_WTAP_PLUGIN);
	printf("Removing visibility wtap plugin...\n");
}

/* We need to use a mutex lock when we read out a visibility map
 * and when we change visibility map from user space through IOCTL
 */
void
visibility_work(struct wtap_plugin *plugin, struct packet *p)
{
	struct visibility_plugin *vis_plugin =
	    (struct visibility_plugin *) plugin;
	struct wtap_hal *hal = (struct wtap_hal *)vis_plugin->base.wp_hal;
	struct vis_map *map;

	KASSERT(mtod(p->m, const char *) != (const char *) 0xdeadc0de ||
	    mtod(p->m, const char *) != NULL,
	    ("[%s] got a corrupt packet from master queue, p->m=%p, p->id=%d\n",
	    __func__, p->m, p->id));
	DWTAP_PRINTF("[%d] BROADCASTING m=%p\n", p->id, p->m);
	mtx_lock(&vis_plugin->pl_mtx);
	map = &vis_plugin->pl_node[p->id];
	mtx_unlock(&vis_plugin->pl_mtx);

	/* This is O(n*n) which is not optimal for large
	 * number of nodes. Another way of doing it is
	 * creating groups of nodes that hear each other.
	 * Atleast for this simple static node plugin.
	 */
	for(int i=0; i<ARRAY_SIZE; ++i){
		uint32_t index = map->map[i];
		for(int j=0; j<32; ++j){
			int vis = index & 0x01;
			if(vis){
				int k = i*ARRAY_SIZE + j;
				if(hal->hal_devs[k] != NULL
				    && hal->hal_devs[k]->up == 1){
					struct wtap_softc *sc =
					    hal->hal_devs[k];
					struct mbuf *m =
					    m_dup(p->m, M_NOWAIT);
					DWTAP_PRINTF("[%d] duplicated old_m=%p"
					    "to new_m=%p\n", p->id, p->m, m);
#if 0
					printf("[%d] sending to %d\n",
					    p->id, k);
#endif
					wtap_inject(sc, m);
				}
			}
			index = index >> 1;
		}
	}
}

static void
add_link(struct visibility_plugin *vis_plugin, struct link *l)
{

	mtx_lock(&vis_plugin->pl_mtx);
	struct vis_map *map = &vis_plugin->pl_node[l->id1];
	int index = l->id2/ARRAY_SIZE;
	int bit = l->id2 % ARRAY_SIZE;
	uint32_t value = 1 << bit;
	map->map[index] = map->map[index] | value;
	mtx_unlock(&vis_plugin->pl_mtx);
#if 0
	printf("l->id1=%d, l->id2=%d, map->map[%d] = %u, bit=%d\n",
	    l->id1, l->id2, index, map->map[index], bit);
#endif
}

static void
del_link(struct visibility_plugin *vis_plugin, struct link *l)
{

	mtx_lock(&vis_plugin->pl_mtx);
	struct vis_map *map = &vis_plugin->pl_node[l->id1];
	int index = l->id2/ARRAY_SIZE;
	int bit = l->id2 % ARRAY_SIZE;
	uint32_t value = 1 << bit;
	map->map[index] = map->map[index] & ~value;
	mtx_unlock(&vis_plugin->pl_mtx);
#if 0
	printf("map->map[index] = %u\n", map->map[index]);
#endif
}


int
vis_ioctl(struct cdev *sdev, u_long cmd, caddr_t data,
    int fflag, struct thread *td)
{
	struct visibility_plugin *vis_plugin =
	    (struct visibility_plugin *) sdev->si_drv1;
	struct wtap_hal *hal = vis_plugin->base.wp_hal;
	struct link l;
	int op;
	int error = 0;

	CURVNET_SET(CRED_TO_VNET(curthread->td_ucred));
	switch(cmd) {
	case VISIOCTLOPEN:
		op =  *(int *)data; 
		if(op == 0)
			medium_close(hal->hal_md);
		else
			medium_open(hal->hal_md);
		break;
	case VISIOCTLLINK:
		l = *(struct link *)data;
		if(l.op == 0)
			del_link(vis_plugin, &l);
		else
			add_link(vis_plugin, &l);
#if 0
		printf("op=%d, id1=%d, id2=%d\n", l.op, l.id1, l.id2);
#endif
		break;
	default:
		DWTAP_PRINTF("Unknown WTAP IOCTL\n");
		error = EINVAL;
	}

	CURVNET_RESTORE();
	return error;
}

