/*	$OpenBSD: autoconf.c,v 1.109 2023/01/30 10:49:04 jsg Exp $	*/
/*	$NetBSD: autoconf.c,v 1.20 1996/05/03 19:41:56 christos Exp $	*/

/*-
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)autoconf.c	7.1 (Berkeley) 5/9/91
 */

/*
 * Setup the system to run on the current machine.
 *
 * cpu_configure() is called at boot time and initializes the vba
 * device tables and the memory controller monitoring.  Available
 * devices are determined (from possibilities mentioned in ioconf.c),
 * and the drivers are initialized.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/reboot.h>
#include <sys/device.h>
#include <sys/hibernate.h>

#include <net/if.h>
#include <net/if_types.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <uvm/uvm_extern.h>

#include <machine/gdt.h>
#include <machine/biosvar.h>

#include "ioapic.h"

#include "acpi.h"

#if NIOAPIC > 0
#include <machine/i82093var.h>
#endif

#ifdef MULTIPROCESSOR
#include <machine/mpbiosvar.h>
#endif

/*
 * The following several variables are related to
 * the configuration process, and are used in initializing
 * the machine.
 */
extern dev_t bootdev;

/* Support for VIA C3 RNG */
extern struct timeout viac3_rnd_tmo;
extern int	viac3_rnd_present;
void		viac3_rnd(void *);

extern struct timeout rdrand_tmo;
extern int	has_rdrand;
extern int	has_rdseed;
void		rdrand(void *);

#ifdef CRYPTO
void		viac3_crypto_setup(void);
extern int	i386_has_xcrypt;
#endif

void
unmap_startup(void)
{
	extern int kernel_text[], endboot[];
	vaddr_t p = (vaddr_t)kernel_text;

	do {
		pmap_kremove(p, PAGE_SIZE);
		p += PAGE_SIZE;
	} while (p < (vaddr_t)endboot);
}

/*
 * Determine i/o configuration for a machine.
 */
void
cpu_configure(void)
{
	/*
	 * Note, on i386, configure is not running under splhigh unlike other
	 * architectures.  This fact is used by the pcmcia irq line probing.
	 */

	gdt_init();		/* XXX - pcibios uses gdt stuff */

	/* Set up proc0's TSS */
	i386_proc0_tss_init();

	pmap_bootstrap_pae();

#if defined(MULTIPROCESSOR) || \
    (NACPI > 0 && !defined(SMALL_KERNEL))
	/* install the lowmem ptp after boot args for 1:1 mappings */
	pmap_prealloc_lowmem_ptp();
#endif

#ifdef MULTIPROCESSOR
	pmap_kenter_pa((vaddr_t)MP_TRAMPOLINE,		/* virtual */
	    (paddr_t)MP_TRAMPOLINE,			/* physical */
	    PROT_READ | PROT_WRITE | PROT_EXEC);	/* protection */
	pmap_kenter_pa((vaddr_t)MP_TRAMP_DATA,		/* virtual */
	    (paddr_t)MP_TRAMP_DATA,			/* physical */
	    PROT_READ | PROT_WRITE);			/* protection */
#endif

	if (config_rootfound("mainbus", NULL) == NULL)
		panic("cpu_configure: mainbus not configured");

#if NIOAPIC > 0
	ioapic_enable();
#endif

	proc0.p_addr->u_pcb.pcb_cr0 = rcr0();

	unmap_startup();

#ifdef MULTIPROCESSOR
	/* propagate TSS configuration to the idle pcb's. */
	cpu_init_idle_pcbs();
#endif
	spl0();

	/*
	 * We can not know which is our root disk, defer
	 * until we can checksum blocks to figure it out.
	 */
	cold = 0;


	/*
	 * At this point the RNG is running, and if FSXR is set we can
	 * use it.  Here we setup a periodic timeout to collect the data.
	 */
	if (viac3_rnd_present) {
		timeout_set(&viac3_rnd_tmo, viac3_rnd, &viac3_rnd_tmo);
		viac3_rnd(&viac3_rnd_tmo);
	}
	if (has_rdrand || has_rdseed) {
		timeout_set(&rdrand_tmo, rdrand, &rdrand_tmo);
		rdrand(&rdrand_tmo);
	}

#ifdef CRYPTO
	/*
	 * Also, if the chip has crypto available, enable it.
	 */
	if (i386_has_xcrypt)
		viac3_crypto_setup();
#endif
}

void
device_register(struct device *dev, void *aux)
{
}

/*
 * Now that we are fully operational, we can checksum the
 * disks, and using some heuristics, hopefully are able to
 * always determine the correct root disk.
 */
void
diskconf(void)
{
	int majdev, unit, part = 0;
	struct device *bootdv = NULL;
	dev_t tmpdev;
	char buf[128];
	extern bios_bootmac_t *bios_bootmac;

	dkcsumattach();

	if ((bootdev & B_MAGICMASK) == (u_int)B_DEVMAGIC) {
		majdev = B_TYPE(bootdev);
		unit = B_UNIT(bootdev);
		part = B_PARTITION(bootdev);
		snprintf(buf, sizeof buf, "%s%d%c", findblkname(majdev),
		    unit, part + 'a');
		bootdv = parsedisk(buf, strlen(buf), part, &tmpdev);
	}

	if (bios_bootmac) {
		struct ifnet *ifp;

		TAILQ_FOREACH(ifp, &ifnetlist, if_list) {
			if (ifp->if_type == IFT_ETHER &&
			    memcmp(bios_bootmac->mac,
			    ((struct arpcom *)ifp)->ac_enaddr,
			    ETHER_ADDR_LEN) == 0)
				break;
		}
		if (ifp) {
			printf("PXE boot MAC address %s, interface %s\n",
			    ether_sprintf(bios_bootmac->mac), ifp->if_xname);
#if defined(NFSCLIENT)
			bootdv = parsedisk(ifp->if_xname, strlen(ifp->if_xname),
			    0, &tmpdev);
			part = 0;
#endif
		} else
			printf("PXE boot MAC address %s, interface %s\n",
			    ether_sprintf(bios_bootmac->mac), "unknown");
	}

	setroot(bootdv, part, RB_USERREQ);
	dumpconf();

#ifdef HIBERNATE
	hibernate_resume();
#endif /* HIBERNATE */
}

const struct nam2blk nam2blk[] = {
	{ "wd",		0 },
	{ "fd",		2 },
	{ "sd",		4 },
	{ "cd",		6 },
	{ "vnd",	14 },
	{ "rd",		17 },
	{ NULL,		-1 }
};
