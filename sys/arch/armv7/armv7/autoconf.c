/*	$OpenBSD: autoconf.c,v 1.13 2022/12/14 13:55:11 kettenis Exp $	*/
/*	$NetBSD: autoconf.c,v 1.2 2001/09/05 16:17:36 matt Exp $	*/

/*
 * Copyright (c) 1994-1998 Mark Brinicombe.
 * Copyright (c) 1994 Brini.
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
 *	This product includes software developed by Mark Brinicombe for
 *      the NetBSD project.
 * 4. The name of the company nor the name of the author may be used to
 *    endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * RiscBSD kernel project
 *
 * autoconf.c
 *
 * Autoconfiguration functions
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/reboot.h>
#include <sys/device.h>

#include <net/if.h>
#include <net/if_types.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <machine/bootconfig.h>
#include <machine/intr.h>

struct device *bootdv = NULL;

void dumpconf(void);

void
device_register(struct device *dev, void *aux)
{
}

/*
 * void cpu_configure()
 *
 * Configure all the root devices
 * The root devices are expected to configure their own children
 */
void
cpu_configure(void)
{
	splhigh();

	softintr_init();

	/*
	 * Since various PCI interrupts could be routed via the ICU
	 * (for PCI devices in the bridge) we need to set up the ICU
	 * now so that these interrupts can be established correctly
	 * i.e. This is a hack.
	 */

	config_rootfound("mainbus", NULL);

	/*
	 * We can not know which is our root disk, defer
	 * until we can checksum blocks to figure it out.
	 */
	cold = 0;

	/* Time to start taking interrupts so lets open the flood gates .... */
	(void)spl0();

}

/*
 * Now that we are fully operational, we can checksum the
 * disks, and using some heuristics, hopefully are able to
 * always determine the correct root disk.
 */
void
diskconf(void)
{
	size_t	len;
	char	*p;
	dev_t	tmpdev;
	extern uint8_t *bootmac;

	if (*boot_file != '\0')
		printf("bootfile: %s\n", boot_file);

	/* Lookup boot device from boot if not set by configuration */
	if (bootdv == NULL) {

		/* boot_file is of the form wd0a:/bsd, we want 'wd0a' */
		if ((p = strchr(boot_file, ':')) != NULL)
			len = p - boot_file;
		else
			len = strlen(boot_file);
		bootdv = parsedisk(boot_file, len, 0, &tmpdev);
	}

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

	if (bootdv != NULL)
		printf("boot device: %s\n", bootdv->dv_xname);
	else
		printf("boot device: lookup %s failed.\n", boot_file);
	setroot(bootdv, 0, RB_USERREQ);
	dumpconf();
}

const struct nam2blk nam2blk[] = {
	{ "wd",		16 },
	{ "rd",		18 },
	{ "vnd",	19 },
	{ "sd",		24 },
	{ "cd",		26 },
	{ NULL,		-1 }
};

