/*-
 * Copyright (c) 2009 Oleksandr Tymoshenko
 * Copyright (c) 2015 Alexander Kabaev
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

#include "opt_ddb.h"
#include "opt_platform.h"

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/boot.h>
#include <sys/cons.h>
#include <sys/kdb.h>
#include <sys/reboot.h>

#ifdef FDT
#include <dev/fdt/fdt_common.h>
#include <dev/ofw/openfirm.h>
#endif

#include <vm/vm.h>
#include <vm/vm_page.h>

#include <net/ethernet.h>

#include <machine/clock.h>
#include <machine/cpu.h>
#include <machine/cpufunc.h>
#include <machine/hwfunc.h>
#include <machine/md_var.h>
#include <machine/trap.h>
#include <machine/vmparam.h>

#include <mips/ingenic/jz4780_regs.h>
#include <mips/ingenic/jz4780_cpuregs.h>

uint32_t * const led = (uint32_t *)0xb0010548;

extern char edata[], end[];
static char boot1_env[4096];

void
platform_cpu_init(void)
{
	uint32_t reg;

	/*
	 * Do not expect mbox interrups while writing
	 * mbox
	 */
	reg = mips_rd_xburst_reim();
	reg &= ~JZ_REIM_MIRQ0M;
	mips_wr_xburst_reim(reg);

	/* Clean mailboxes */
	mips_wr_xburst_mbox0(0);
	mips_wr_xburst_mbox1(0);
	mips_wr_xburst_core_sts(~JZ_CORESTS_MIRQ0P);

	/* Unmask mbox interrupts */
	reg |= JZ_REIM_MIRQ0M;
	mips_wr_xburst_reim(reg);
}

void
platform_reset(void)
{
	/*
	 * For now, provoke a watchdog reset in about a second, so UART buffers
	 * have a fighting chance to flush before we pull the plug
	 */
	writereg(JZ_TCU_BASE + JZ_WDOG_TCER, 0);	/* disable watchdog */
	writereg(JZ_TCU_BASE + JZ_WDOG_TCNT, 0);	/* reset counter */
	writereg(JZ_TCU_BASE + JZ_WDOG_TDR, 128);	/* wait for ~1s */
	writereg(JZ_TCU_BASE + JZ_WDOG_TCSR, TCSR_RTC_EN | TCSR_DIV_256);
	writereg(JZ_TCU_BASE + JZ_WDOG_TCER, TCER_ENABLE);	/* fire! */

	/* Wait for reset */
	while (1)
		;
}

static void
mips_init(void)
{
	int i;
#ifdef FDT
	struct mem_region mr[FDT_MEM_REGIONS];
	uint64_t val;
	int mr_cnt;
	int j;
#endif

	for (i = 0; i < 10; i++) {
		phys_avail[i] = 0;
	}

	/* The minimal amount of memory Ingenic SoC can have. */
	dump_avail[0] = phys_avail[0] = MIPS_KSEG0_TO_PHYS(kernel_kseg0_end);
	physmem = realmem = btoc(32 * 1024 * 1024);

	/*
	 * X1000 mips cpu special.
	 * TODO: do anyone know what is this ?
	 */
	__asm(
		"li	$2, 0xa9000000	\n\t"
		"mtc0	$2, $5, 4	\n\t"
		"nop			\n\t"
		::"r"(2));

#ifdef FDT
	if (fdt_get_mem_regions(mr, &mr_cnt, &val) == 0) {

		physmem = realmem = btoc(val);

		KASSERT((phys_avail[0] >= mr[0].mr_start) && \
			(phys_avail[0] < (mr[0].mr_start + mr[0].mr_size)),
			("First region is not within FDT memory range"));

		/* Limit size of the first region */
		phys_avail[1] = (mr[0].mr_start + MIN(mr[0].mr_size, ctob(realmem)));
		dump_avail[1] = phys_avail[1];

		/* Add the rest of regions */
		for (i = 1, j = 2; i < mr_cnt; i++, j+=2) {
			phys_avail[j] = mr[i].mr_start;
			phys_avail[j+1] = (mr[i].mr_start + mr[i].mr_size);
			dump_avail[j] = phys_avail[j];
			dump_avail[j+1] = phys_avail[j+1];
		}
	}
#endif

	init_param1();
	init_param2(physmem);
	mips_cpu_init();
	pmap_bootstrap();
	mips_proc0_init();
	mutex_init();
	kdb_init();
	led[0] = 0x8000;
#ifdef KDB
	if (boothowto & RB_KDB)
		kdb_enter(KDB_WHY_BOOTFLAGS, "Boot flags requested debugger");
#endif
}

void
platform_start(__register_t a0,  __register_t a1,
    __register_t a2 __unused, __register_t a3 __unused)
{
	char **argv;
	int  argc;
	vm_offset_t kernend;
#ifdef FDT
	vm_offset_t dtbp;
	phandle_t chosen;
	char buf[2048];		/* early stack supposedly big enough */
#endif
	/*
	 * clear the BSS and SBSS segments, this should be first call in
	 * the function
	 */
	kernend = (vm_offset_t)&end;
	memset(&edata, 0, kernend - (vm_offset_t)(&edata));

	mips_postboot_fixup();

	/* Initialize pcpu stuff */
	mips_pcpu0_init();

	/* Something to hold kernel env until kmem is available */
	init_static_kenv(boot1_env, sizeof(boot1_env));
#ifdef FDT
	/*
	 * Find the dtb passed in by the boot loader (currently fictional).
	 */
	dtbp = (vm_offset_t)NULL;

#if defined(FDT_DTB_STATIC)
	/*
	 * In case the device tree blob was not retrieved (from metadata) try
	 * to use the statically embedded one.
	 */
	if (dtbp == (vm_offset_t)NULL)
		dtbp = (vm_offset_t)&fdt_static_dtb;
#else
#error	"Non-static FDT not supported on JZ4780"
#endif
	if (OF_install(OFW_FDT, 0) == FALSE)
		while (1);
	if (OF_init((void *)dtbp) != 0)
		while (1);
#endif

	cninit();
#ifdef FDT
	/*
	 * Get bootargs from FDT if specified.
	 */
	chosen = OF_finddevice("/chosen");
	if (OF_getprop(chosen, "bootargs", buf, sizeof(buf)) != -1)
		boothowto |= boot_parse_cmdline(buf);
#endif
	/* Parse cmdline from U-Boot */
	argc = a0;
	argv = (char **)a1;
	boothowto |= boot_parse_args(argc, argv);

	mips_init();
}
