/*-
 * Copyright (C) 2015-2016 by Stanislav Galabov. All rights reserved.
 * Copyright (C) 2010-2011 by Aleksandr Rybalko. All rights reserved.
 * Copyright (C) 2007 by Oleksandr Tymoshenko. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ddb.h"

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/imgact.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/bus.h>
#include <sys/cpu.h>
#include <sys/cons.h>
#include <sys/exec.h>
#include <sys/ucontext.h>
#include <sys/proc.h>
#include <sys/kdb.h>
#include <sys/ptrace.h>
#include <sys/boot.h>
#include <sys/reboot.h>
#include <sys/signalvar.h>
#include <sys/sysent.h>
#include <sys/sysproto.h>
#include <sys/user.h>

#include <vm/vm.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>

#include <machine/cache.h>
#include <machine/clock.h>
#include <machine/cpu.h>
#include <machine/cpuinfo.h>
#include <machine/cpufunc.h>
#include <machine/cpuregs.h>
#include <machine/hwfunc.h>
#include <machine/intr_machdep.h>
#include <machine/locore.h>
#include <machine/md_var.h>
#include <machine/pte.h>
#include <machine/sigframe.h>
#include <machine/trap.h>
#include <machine/vmparam.h>

#include <mips/mediatek/mtk_sysctl.h>
#include <mips/mediatek/mtk_soc.h>

#include "opt_platform.h"
#include "opt_rt305x.h"

#include <dev/fdt/fdt_common.h>
#include <dev/ofw/openfirm.h>

extern int	*edata;
extern int	*end;
static char 	boot1_env[0x1000];

void
platform_cpu_init()
{
	/* Nothing special */
}

static void
mips_init(void)
{
	struct mem_region mr[FDT_MEM_REGIONS];
	uint64_t val;
	int i, j, mr_cnt;
	char *memsize;

	printf("entry: mips_init()\n");

	bootverbose = 1;

	for (i = 0; i < 10; i++)
		phys_avail[i] = 0;

	dump_avail[0] = phys_avail[0] = MIPS_KSEG0_TO_PHYS(kernel_kseg0_end);

	/*
	 * The most low memory MT7621 can have. Currently MT7621 is the chip
	 * that supports the most memory, so that seems reasonable.
	 */
	realmem = btoc(448 * 1024 * 1024);

	if (fdt_get_mem_regions(mr, &mr_cnt, &val) == 0) {
		physmem = btoc(val);

		printf("RAM size: %ldMB (from FDT)\n",
		    ctob(physmem) / (1024 * 1024));

		KASSERT((phys_avail[0] >= mr[0].mr_start) && \
			(phys_avail[0] < (mr[0].mr_start + mr[0].mr_size)),
			("First region is not within FDT memory range"));

		/* Limit size of the first region */
		phys_avail[1] = (mr[0].mr_start +
		    MIN(mr[0].mr_size, ctob(realmem)));
		dump_avail[1] = phys_avail[1];

		/* Add the rest of the regions */
		for (i = 1, j = 2; i < mr_cnt; i++, j+=2) {
			phys_avail[j] = mr[i].mr_start;
			phys_avail[j+1] = (mr[i].mr_start + mr[i].mr_size);
			dump_avail[j] = phys_avail[j];
			dump_avail[j+1] = phys_avail[j+1];
		}
	} else {
		if ((memsize = kern_getenv("memsize")) != NULL) {
			physmem = btoc(strtol(memsize, NULL, 0) << 20);
			printf("RAM size: %ldMB (from memsize)\n",
			    ctob(physmem) / (1024 * 1024));
		} else { /* All else failed, assume 32MB */
			physmem = btoc(32 * 1024 * 1024);
			printf("RAM size: %ldMB (assumed)\n",
			    ctob(physmem) / (1024 * 1024));
		}

		if (mtk_soc_get_socid() == MTK_SOC_RT2880) {
			/* RT2880 memory start is 88000000 */
			dump_avail[1] = phys_avail[1] = ctob(physmem)
			    + 0x08000000;
		} else if (ctob(physmem) < (448 * 1024 * 1024)) {
			/*
			 * Anything up to 448MB is assumed to be directly
			 * mappable as low memory...
			 */
			dump_avail[1] = phys_avail[1] = ctob(physmem);
		} else if (mtk_soc_get_socid() == MTK_SOC_MT7621) {
			/*
			 * On MT7621 the low memory is limited to 448MB, the
			 * rest is high memory, mapped at 0x20000000
			 */
			phys_avail[1] = 448 * 1024 * 1024;
			phys_avail[2] = 0x20000000;
			phys_avail[3] = phys_avail[2] + ctob(physmem) -
			    phys_avail[1];
			dump_avail[1] = phys_avail[1] - phys_avail[0];
			dump_avail[2] = phys_avail[2];
			dump_avail[3] = phys_avail[3] - phys_avail[2];
		} else {
			/*
			 * We have > 448MB RAM and we're not MT7621? Currently
			 * there is no such chip, so we'll just limit the RAM to
			 * 32MB and let the user know...
			 */
			printf("Unknown chip, assuming 32MB RAM\n");
			physmem = btoc(32 * 1024 * 1024);
			dump_avail[1] = phys_avail[1] = ctob(physmem);
		}
	}

	if (physmem < realmem)
		realmem = physmem;

	init_param1();
	init_param2(physmem);
	mips_cpu_init();
	pmap_bootstrap();
	mips_proc0_init();
	mutex_init();
	kdb_init();
#ifdef KDB
	if (boothowto & RB_KDB)
		kdb_enter(KDB_WHY_BOOTFLAGS, "Boot flags requested debugger");
#endif
}

void
platform_reset(void)
{

	mtk_soc_reset();
}

void
platform_start(__register_t a0 __unused, __register_t a1 __unused, 
    __register_t a2 __unused, __register_t a3 __unused)
{
	vm_offset_t kernend;
	int argc = a0, i;//, res;
	uint32_t timer_clk;
	char **argv = (char **)MIPS_PHYS_TO_KSEG0(a1);
	char **envp = (char **)MIPS_PHYS_TO_KSEG0(a2);
	void *dtbp;
	phandle_t chosen;
	char buf[2048];

	/* clear the BSS and SBSS segments */
	kernend = (vm_offset_t)&end;
	memset(&edata, 0, kernend - (vm_offset_t)(&edata));

	mips_postboot_fixup();

	/* Initialize pcpu stuff */
	mips_pcpu0_init();

	dtbp = &fdt_static_dtb;
	if (OF_install(OFW_FDT, 0) == FALSE)
		while (1);
	if (OF_init((void *)dtbp) != 0)
		while (1);

	mtk_soc_try_early_detect();
	mtk_soc_set_cpu_model();

	if ((timer_clk = mtk_soc_get_timerclk()) == 0)
		timer_clk = 1000000000; /* no such speed yet */

	mips_timer_early_init(timer_clk);

	/* initialize console so that we have printf */
	boothowto |= (RB_SERIAL | RB_MULTIPLE);	/* Use multiple consoles */
	boothowto |= (RB_VERBOSE);
	cninit();

	init_static_kenv(boot1_env, sizeof(boot1_env));

	/*
	 * Get bsdbootargs from FDT if specified.
	 */
	chosen = OF_finddevice("/chosen");
	if (OF_getprop(chosen, "bsdbootargs", buf, sizeof(buf)) != -1)
		boothowto |= boot_parse_cmdline(buf);

	printf("FDT DTB  at: 0x%08x\n", (uint32_t)dtbp);

	printf("CPU   clock: %4dMHz\n", mtk_soc_get_cpuclk()/(1000*1000));
	printf("Timer clock: %4dMHz\n", timer_clk/(1000*1000));
	printf("UART  clock: %4dMHz\n\n", mtk_soc_get_uartclk()/(1000*1000));

	printf("U-Boot args (from %d args):\n", argc - 1);

	if (argc == 1)
		printf("\tNone\n");

	for (i = 1; i < argc; i++) {
		char *n = "argv  ", *arg;

		if (i > 99)
			break;

		if (argv[i])
		{
			arg = (char *)(intptr_t)MIPS_PHYS_TO_KSEG0(argv[i]);
			printf("\targv[%d] = %s\n", i, arg);
			sprintf(n, "argv%d", i);
			kern_setenv(n, arg);
		}
	}

	printf("Environment:\n");

	for (i = 0; envp[i] && MIPS_IS_VALID_PTR(envp[i]); i++) {
		char *n, *arg;

		arg = (char *)(intptr_t)MIPS_PHYS_TO_KSEG0(envp[i]);
		if (! MIPS_IS_VALID_PTR(arg))
			continue;
		printf("\t%s\n", arg);
		n = strsep(&arg, "=");
		if (arg == NULL)
			kern_setenv(n, "1");
		else
			kern_setenv(n, arg);
	}


	mips_init();
	mips_timer_init_params(timer_clk, 0);
}
