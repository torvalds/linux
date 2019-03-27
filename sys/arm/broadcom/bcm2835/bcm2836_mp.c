/*-
 * Copyright (C) 2015 Daisuke Aoyama <aoyama@peach.ne.jp>
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
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/smp.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/cpu.h>
#include <machine/smp.h>
#include <machine/bus.h>
#include <machine/fdt.h>
#include <machine/intr.h>
#include <machine/platformvar.h>

#include <arm/broadcom/bcm2835/bcm2836_mp.h>

#ifdef DEBUG
#define	DPRINTF(fmt, ...) do {			\
	printf("%s:%u: ", __func__, __LINE__);	\
	printf(fmt, ##__VA_ARGS__);		\
} while (0)
#else
#define	DPRINTF(fmt, ...)
#endif

#define	ARM_LOCAL_BASE		0x40000000
#define	ARM_LOCAL_SIZE		0x00001000

/* mailbox registers */
#define	MBOXINTRCTRL_CORE(n)	(0x00000050 + (0x04 * (n)))
#define	MBOX0SET_CORE(n)	(0x00000080 + (0x10 * (n)))
#define	MBOX1SET_CORE(n)	(0x00000084 + (0x10 * (n)))
#define	MBOX2SET_CORE(n)	(0x00000088 + (0x10 * (n)))
#define	MBOX3SET_CORE(n)	(0x0000008C + (0x10 * (n)))
#define	MBOX0CLR_CORE(n)	(0x000000C0 + (0x10 * (n)))
#define	MBOX1CLR_CORE(n)	(0x000000C4 + (0x10 * (n)))
#define	MBOX2CLR_CORE(n)	(0x000000C8 + (0x10 * (n)))
#define	MBOX3CLR_CORE(n)	(0x000000CC + (0x10 * (n)))

static bus_space_handle_t bs_periph;

#define	BSRD4(addr) \
	bus_space_read_4(fdtbus_bs_tag, bs_periph, (addr))
#define	BSWR4(addr, val) \
	bus_space_write_4(fdtbus_bs_tag, bs_periph, (addr), (val))

void
bcm2836_mp_setmaxid(platform_t plat)
{

	DPRINTF("platform_mp_setmaxid\n");
	if (mp_ncpus != 0)
		return;

	mp_ncpus = 4;
	mp_maxid = mp_ncpus - 1;
	DPRINTF("mp_maxid=%d\n", mp_maxid);
}

void
bcm2836_mp_start_ap(platform_t plat)
{
	uint32_t val;
	int i, retry;

	DPRINTF("platform_mp_start_ap\n");

	/* initialize */
	if (bus_space_map(fdtbus_bs_tag, ARM_LOCAL_BASE, ARM_LOCAL_SIZE,
	    0, &bs_periph) != 0)
		panic("can't map local peripheral\n");
	for (i = 0; i < mp_ncpus; i++) {
		/* clear mailbox 0/3 */
		BSWR4(MBOX0CLR_CORE(i), 0xffffffff);
		BSWR4(MBOX3CLR_CORE(i), 0xffffffff);
	}
	wmb();
	dcache_wbinv_poc_all();

	/* boot secondary CPUs */
	for (i = 1; i < mp_ncpus; i++) {
		/* set entry point to mailbox 3 */
		BSWR4(MBOX3SET_CORE(i),
		    (uint32_t)pmap_kextract((vm_offset_t)mpentry));
		/* Firmware put cores in WFE state, need SEV to wake up. */
		dsb();
		sev();

		/* wait for bootup */
		retry = 1000;
		do {
			/* check entry point */
			val = BSRD4(MBOX3CLR_CORE(i));
			if (val == 0)
				break;
			DELAY(100);
			retry--;
			if (retry <= 0) {
				printf("can't start for CPU%d\n", i);
				break;
			}
		} while (1);

		/* dsb and sev */
		dsb();
		sev();

		/* recode AP in CPU map */
		CPU_SET(i, &all_cpus);
	}
}
