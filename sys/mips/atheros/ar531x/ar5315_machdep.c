/*-
 * Copyright (c) 2016, Hiroki Mori
 * Copyright (c) 2009 Oleksandr Tymoshenko
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
#include "opt_ar531x.h"

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/cons.h>
#include <sys/kdb.h>
#include <sys/reboot.h>
#include <sys/boot.h>

#include <vm/vm.h>
#include <vm/vm_page.h>

#include <net/ethernet.h>

#include <machine/clock.h>
#include <machine/cpu.h>
#include <machine/cpuregs.h>
#include <machine/hwfunc.h>
#include <machine/md_var.h>
#include <machine/trap.h>
#include <machine/vmparam.h>

#include <mips/atheros/ar531x/ar5315reg.h>

#include <mips/atheros/ar531x/ar5315_setup.h>
#include <mips/atheros/ar531x/ar5315_cpudef.h>

extern char edata[], end[];

uint32_t ar711_base_mac[ETHER_ADDR_LEN];
/* 4KB static data aread to keep a copy of the bootload env until
   the dynamic kenv is setup */
char boot1_env[4096];

void
platform_cpu_init()
{
	/* Nothing special */
}

void
platform_reset(void)
{
	ar531x_device_reset();
	/* Wait for reset */
	while(1)
		;
}

/*
 * Obtain the MAC address via the Redboot environment.
 */
static void
ar5315_redboot_get_macaddr(void)
{
	char *var;
	int count = 0;

	/*
	 * "ethaddr" is passed via envp on RedBoot platforms
	 * "kmac" is passed via argv on RouterBOOT platforms
	 */
	if ((var = kern_getenv("ethaddr")) != NULL ||
	    (var = kern_getenv("kmac")) != NULL) {
		count = sscanf(var, "%x%*c%x%*c%x%*c%x%*c%x%*c%x",
		    &ar711_base_mac[0], &ar711_base_mac[1],
		    &ar711_base_mac[2], &ar711_base_mac[3],
		    &ar711_base_mac[4], &ar711_base_mac[5]);
		if (count < 6)
			memset(ar711_base_mac, 0,
			    sizeof(ar711_base_mac));
		freeenv(var);
	}
}

#if defined(SOC_VENDOR) || defined(SOC_MODEL) || defined(SOC_REV)
static SYSCTL_NODE(_hw, OID_AUTO, soc, CTLFLAG_RD, 0,
    "System on Chip information");
#endif
#if defined(SOC_VENDOR)
static char hw_soc_vendor[] = SOC_VENDOR;
SYSCTL_STRING(_hw_soc, OID_AUTO, vendor, CTLFLAG_RD, hw_soc_vendor, 0,
	   "SoC vendor");
#endif
#if defined(SOC_MODEL)
static char hw_soc_model[] = SOC_MODEL;
SYSCTL_STRING(_hw_soc, OID_AUTO, model, CTLFLAG_RD, hw_soc_model, 0,
	   "SoC model");
#endif
#if defined(SOC_REV)
static char hw_soc_revision[] = SOC_REV;
SYSCTL_STRING(_hw_soc, OID_AUTO, revision, CTLFLAG_RD, hw_soc_revision, 0,
	   "SoC revision");
#endif

#if defined(DEVICE_VENDOR) || defined(DEVICE_MODEL) || defined(DEVICE_REV)
static SYSCTL_NODE(_hw, OID_AUTO, device, CTLFLAG_RD, 0, "Board information");
#endif
#if defined(DEVICE_VENDOR)
static char hw_device_vendor[] = DEVICE_VENDOR;
SYSCTL_STRING(_hw_device, OID_AUTO, vendor, CTLFLAG_RD, hw_device_vendor, 0,
	   "Board vendor");
#endif
#if defined(DEVICE_MODEL)
static char hw_device_model[] = DEVICE_MODEL;
SYSCTL_STRING(_hw_device, OID_AUTO, model, CTLFLAG_RD, hw_device_model, 0,
	   "Board model");
#endif
#if defined(DEVICE_REV)
static char hw_device_revision[] = DEVICE_REV;
SYSCTL_STRING(_hw_device, OID_AUTO, revision, CTLFLAG_RD, hw_device_revision, 0,
	   "Board revision");
#endif

void
platform_start(__register_t a0 __unused, __register_t a1 __unused, 
    __register_t a2 __unused, __register_t a3 __unused)
{
	uint64_t platform_counter_freq;
	int argc = 0, i;
	char **argv = NULL;
#ifndef	AR531X_ENV_UBOOT
	char **envp = NULL;
#endif
	vm_offset_t kernend;

	/* 
	 * clear the BSS and SBSS segments, this should be first call in
	 * the function
	 */
	kernend = (vm_offset_t)&end;
	memset(&edata, 0, kernend - (vm_offset_t)(&edata));

	mips_postboot_fixup();

	/* Initialize pcpu stuff */
	mips_pcpu0_init();

	/*
	 * Until some more sensible abstractions for uboot/redboot
	 * environment handling, we have to make this a compile-time
	 * hack.  The existing code handles the uboot environment
	 * very incorrectly so we should just ignore initialising
	 * the relevant pointers.
	 */
#ifndef	AR531X_ENV_UBOOT
	argc = a0;
	argv = (char**)a1;
	envp = (char**)a2;
#endif
	/* 
	 * Protect ourselves from garbage in registers 
	 */
	if (MIPS_IS_VALID_PTR(envp)) {
		for (i = 0; envp[i]; i += 2) {
			if (strcmp(envp[i], "memsize") == 0)
				realmem = btoc(strtoul(envp[i+1], NULL, 16));
		}
	}

	ar5315_detect_sys_type();

// RedBoot SDRAM Detect is missing
//	ar531x_detect_mem_size();

	/*
	 * Just wild guess. RedBoot let us down and didn't reported 
	 * memory size
	 */
	if (realmem == 0)
		realmem = btoc(16*1024*1024);

	/*
	 * Allow build-time override in case Redboot lies
	 * or in other situations (eg where there's u-boot)
	 * where there isn't (yet) a convienent method of
	 * being told how much RAM is available.
	 *
	 * This happens on at least the Ubiquiti LS-SR71A
	 * board, where redboot says there's 16mb of RAM
	 * but in fact there's 32mb.
	 */
#if	defined(AR531X_REALMEM)
		realmem = btoc(AR531X_REALMEM);
#endif

	/* phys_avail regions are in bytes */
	phys_avail[0] = MIPS_KSEG0_TO_PHYS(kernel_kseg0_end);
	phys_avail[1] = ctob(realmem);

	dump_avail[0] = phys_avail[0];
	dump_avail[1] = phys_avail[1] - phys_avail[0];

	physmem = realmem;

	/*
	 * ns8250 uart code uses DELAY so ticker should be inititalized 
	 * before cninit. And tick_init_params refers to hz, so * init_param1 
	 * should be called first.
	 */
	init_param1();
	boothowto |= (RB_SERIAL | RB_MULTIPLE); /* Use multiple consoles */
//	boothowto |= RB_VERBOSE;
//	boothowto |= (RB_SINGLE);

	/* Detect the system type - this is needed for subsequent chipset-specific calls */


	ar531x_device_soc_init();
	ar531x_detect_sys_frequency();

	platform_counter_freq = ar531x_cpu_freq();
	mips_timer_init_params(platform_counter_freq, 1);
	cninit();
	init_static_kenv(boot1_env, sizeof(boot1_env));

	printf("CPU platform: %s\n", ar5315_get_system_type());
	printf("CPU Frequency=%d MHz\n", ar531x_cpu_freq() / 1000000);
	printf("CPU DDR Frequency=%d MHz\n", ar531x_ddr_freq() / 1000000);
	printf("CPU AHB Frequency=%d MHz\n", ar531x_ahb_freq() / 1000000); 

	printf("platform frequency: %lld\n", platform_counter_freq);
	printf("arguments: \n");
	printf("  a0 = %08x\n", a0);
	printf("  a1 = %08x\n", a1);
	printf("  a2 = %08x\n", a2);
	printf("  a3 = %08x\n", a3);

	/*
	 * XXX this code is very redboot specific.
	 */
	printf("Cmd line:");
	if (MIPS_IS_VALID_PTR(argv)) {
		for (i = 0; i < argc; i++) {
			printf(" %s", argv[i]);
			boothowto |= boot_parse_arg(argv[i]);
		}
	}
	else
		printf ("argv is invalid");
	printf("\n");

	printf("Environment:\n");
#if 0
	if (MIPS_IS_VALID_PTR(envp)) {
		if (envp[0] && strchr(envp[0], '=') ) {
			char *env_val; //
			for (i = 0; envp[i]; i++) {
				env_val = strchr(envp[i], '=');
				/* Not sure if we correct to change data, but env in RAM */
				*(env_val++) = '\0';
				printf("=  %s = %s\n", envp[i], env_val);
				kern_setenv(envp[i], env_val);
			}
		} else {
			for (i = 0; envp[i]; i+=2) {
				printf("  %s = %s\n", envp[i], envp[i+1]);
				kern_setenv(envp[i], envp[i+1]);
			}
		}
	}
	else 
		printf ("envp is invalid\n");
#else
	printf ("envp skiped\n");
#endif

	/* Redboot if_are MAC address is in the environment */
	ar5315_redboot_get_macaddr();

	init_param2(physmem);
	mips_cpu_init();
	pmap_bootstrap();
	mips_proc0_init();
	mutex_init();

	ar531x_device_start();

	kdb_init();
#ifdef KDB
	if (boothowto & RB_KDB)
		kdb_enter(KDB_WHY_BOOTFLAGS, "Boot flags requested debugger");
#endif

}
