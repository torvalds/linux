/*	$OpenBSD: autoconf.c,v 1.8 2024/11/11 23:48:46 jsg Exp $	*/

/*
 * Copyright (c) 2009 Miodrag Vallat.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/device.h>
#include <sys/reboot.h>
#include <sys/hibernate.h>
#include <sys/systm.h>
#include <uvm/uvm_extern.h>

#if defined(NFSCLIENT)
#include <net/if.h>
#include <net/if_types.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#endif

extern void dumpconf(void);

void
unmap_startup(void)
{
	extern void *_start, *endboot;
	vaddr_t p = (vaddr_t)&_start;

	do {
		pmap_kremove(p, PAGE_SIZE);
		p += PAGE_SIZE;
	} while (p < (vaddr_t)&endboot);
}

void
cpu_configure(void)
{
	splhigh();

	softintr_init();
	config_rootfound("mainbus", NULL);

	unmap_startup();

	cold = 0;
	spl0();
}

void
diskconf(void)
{
#if defined(NFSCLIENT)
	extern uint8_t *bootmac;
	dev_t tmpdev = NODEV;
#endif
	struct device *bootdv = NULL;
	int part = 0;

#if defined(NFSCLIENT)
	if (bootmac) {
		struct ifnet *ifp;

		TAILQ_FOREACH(ifp, &ifnetlist, if_list) {
			if (ifp->if_type == IFT_ETHER &&
			    memcmp(bootmac, ((struct arpcom *)ifp)->ac_enaddr,
			    ETHER_ADDR_LEN) == 0)
				break;
		}
		if (ifp)
			bootdv = parsedisk(ifp->if_xname, strlen(ifp->if_xname),
			    0, &tmpdev);
	}
#endif

	setroot(bootdv, part, RB_USERREQ);
	dumpconf();

#ifdef HIBERNATE
	hibernate_resume();
#endif /* HIBERNATE */
}

void
device_register(struct device *dev, void *aux)
{
}

const struct nam2blk nam2blk[] = {
	{ "wd",		 0 },
	{ "sd",		 4 },
	{ "cd",		 6 },
	{ "rd",		 8 },
	{ "vnd",	14 },
	{ NULL,		-1 }
};
