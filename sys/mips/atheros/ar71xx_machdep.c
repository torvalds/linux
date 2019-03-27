/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
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
#include "opt_ar71xx.h"

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/cons.h>
#include <sys/kdb.h>
#include <sys/boot.h>
#include <sys/reboot.h>

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

#include <mips/atheros/ar71xxreg.h>

#include <mips/atheros/ar71xx_setup.h>
#include <mips/atheros/ar71xx_cpudef.h>
#include <mips/atheros/ar71xx_macaddr.h>

extern char edata[], end[];

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
	ar71xx_device_stop(RST_RESET_FULL_CHIP);
	/* Wait for reset */
	while(1)
		;
}

/*
 * Obtain the MAC address via the Redboot environment.
 */
static int
ar71xx_redboot_get_macaddr(void)
{
	char *var;
	int count = 0, i;
	uint32_t macaddr[ETHER_ADDR_LEN];
	uint8_t tmpmac[ETHER_ADDR_LEN];

	/*
	 * "ethaddr" is passed via envp on RedBoot platforms
	 * "kmac" is passed via argv on RouterBOOT platforms
	 */
	if ((var = kern_getenv("ethaddr")) != NULL ||
	    (var = kern_getenv("kmac")) != NULL) {
		count = sscanf(var, "%x%*c%x%*c%x%*c%x%*c%x%*c%x",
		    &macaddr[0], &macaddr[1],
		    &macaddr[2], &macaddr[3],
		    &macaddr[4], &macaddr[5]);

		if (count < 6) {
			memset(macaddr, 0,
			    sizeof(macaddr));
		} else {
			for (i = 0; i < ETHER_ADDR_LEN; i++)
				tmpmac[i] = macaddr[i] & 0xff;
			(void) ar71xx_mac_addr_init(ar71xx_board_mac_addr,
			    tmpmac,
			    0, /* offset */
			    0); /* is_local */
		}
		freeenv(var);
		return (0);
	}
	return (-1);
}

#ifdef	AR71XX_ENV_ROUTERBOOT
/*
 * RouterBoot gives us the board memory in a command line argument.
 */
static int
ar71xx_routerboot_get_mem(int argc, char **argv)
{
	int i, board_mem;

	/*
	 * Protect ourselves from garbage in registers.
	 */
	if (!MIPS_IS_VALID_PTR(argv))
		return (0);

	for (i = 0; i < argc; i++) {
		if (argv[i] == NULL)
			continue;
		if (strncmp(argv[i], "mem=", 4) == 0) {
			if (sscanf(argv[i] + 4, "%dM", &board_mem) == 1)
				return (btoc(board_mem * 1024 * 1024));
		}
	}

	return (0);
}
#endif

/*
 * Handle initialising the MAC address from a specific EEPROM
 * offset.
 *
 * This is done during (very) early boot.
 *
 * hint.ar71xx.0.eeprom_mac_addr=<address to read from>
 * hint.ar71xx.0.eeprom_mac_isascii=<0|1>
 */
static int
ar71xx_platform_read_eeprom_mac(void)
{
	long eeprom_mac_addr = 0;
	const char *mac;
	int i, readascii = 0;
	uint8_t macaddr[ETHER_ADDR_LEN];

	if (resource_long_value("ar71xx", 0, "eeprom_mac_addr",
	    &eeprom_mac_addr) != 0)
		return (-1);

	/* get a pointer to the EEPROM MAC address */

	mac = (const char *) MIPS_PHYS_TO_KSEG1(eeprom_mac_addr);

	/* Check if it's ASCII or not */
	if (resource_int_value("ar71xx", 0, "eeprom_mac_isascii",
	    &readascii) == 0 && readascii == 1) {
		printf("ar71xx: Overriding MAC from EEPROM (ascii)\n");
		for (i = 0; i < 6; i++) {
			macaddr[i] = strtol(&(mac[i*3]), NULL, 16);
		}
	} else {
		printf("ar71xx: Overriding MAC from EEPROM\n");
		for (i = 0; i < 6; i++) {
			macaddr[i] = mac[i];
		}
	}

	/* Set the default board MAC */
	(void) ar71xx_mac_addr_init(ar71xx_board_mac_addr,
	    macaddr,
	    0, /* offset */
	    0); /* is_local */
	printf("ar71xx: Board MAC: %6D\n", ar71xx_board_mac_addr, ":");
	return (0);
}

/*
 * Populate a kenv hint for the given device based on the given
 * MAC address and offset.
 *
 * Returns 0 if ok, < 0 on error.
 */
static int
ar71xx_platform_set_mac_hint(const char *dev, int unit,
    const uint8_t *macaddr, int offset, int islocal)
{
	char macstr[32];
	uint8_t lclmac[ETHER_ADDR_LEN];
	char devstr[32];

	/* Initialise the MAC address, plus/minus the offset */
	if (ar71xx_mac_addr_init(lclmac, macaddr, offset, islocal) != 0) {
		return (-1);
	}

	/* Turn it into a string */
	snprintf(macstr, 32, "%6D", lclmac, ":");
	snprintf(devstr, 32, "hint.%s.%d.macaddr", dev, unit);

	printf("  %s => %s\n", devstr, macstr);

	/* Call setenv */
	if (kern_setenv(devstr, macstr) != 0) {
		printf("%s: failed to set hint (%s => %s)\n",
		    __func__,
		    devstr,
		    macstr);
		return (-1);
	}

	return (0);
}

/*
 * Iterate through the list of boot time hints that populate
 * a device MAC address hint based on the "board" MAC address.
 *
 * ar71xx_mac_map.X.devid=<device id, eg ath>
 * ar71xx_mac_map.X.unitid=<unit id, eg 0>
 * ar71xx_mac_map.X.offset=<mac address value offset>
 * ar71xx_mac_map.X.is_local=<1 or 0>
 */
static int
ar71xx_platform_check_mac_hints(void)
{
	int i;
	const char *devid;
	int offset, is_local, unitid;

	for (i = 0; i < 8; i++) {
		if (resource_string_value("ar71xx_mac_map", i, "devid",
		    &devid) != 0)
			break;
		if (resource_int_value("ar71xx_mac_map", i, "unitid",
		    &unitid) != 0)
			break;
		if (resource_int_value("ar71xx_mac_map", i, "offset",
		    &offset) != 0)
			break;
		if (resource_int_value("ar71xx_mac_map", i, "is_local",
		    &is_local) != 0)
			break;
		printf("ar71xx: devid '%s.%d', MAC offset '%d'\n",
		    devid, unitid, offset);
		(void) ar71xx_platform_set_mac_hint(devid, unitid,
		    ar71xx_board_mac_addr, offset, is_local);
	}

	return (0);
}

extern char cpu_model[];

void
platform_start(__register_t a0 __unused, __register_t a1 __unused, 
    __register_t a2 __unused, __register_t a3 __unused)
{
	uint64_t platform_counter_freq;
	int argc = 0, i;
	char **argv = NULL, **envp = NULL;
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
#ifndef	AR71XX_ENV_UBOOT
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
			else if (strcmp(envp[i], "bootverbose") == 0)
				bootverbose = btoc(strtoul(envp[i+1], NULL, 10));
		}
	}
	bootverbose = 1;

#ifdef	AR71XX_ENV_ROUTERBOOT
	/*
	 * RouterBoot informs the board memory as a command line argument.
	 */
	if (realmem == 0)
		realmem = ar71xx_routerboot_get_mem(argc, argv);
#endif

	/*
	 * Just wild guess. RedBoot let us down and didn't reported 
	 * memory size
	 */
	if (realmem == 0)
		realmem = btoc(32*1024*1024);

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
#if	defined(AR71XX_REALMEM)
		realmem = btoc(AR71XX_REALMEM);
#endif

	/* phys_avail regions are in bytes */
	phys_avail[0] = MIPS_KSEG0_TO_PHYS(kernel_kseg0_end);
	phys_avail[1] = ctob(realmem);

	dump_avail[0] = 0;
	dump_avail[1] = phys_avail[1];

	physmem = realmem;

	/*
	 * ns8250 uart code uses DELAY so ticker should be inititalized 
	 * before cninit. And tick_init_params refers to hz, so * init_param1 
	 * should be called first.
	 */
	init_param1();

	/* Detect the system type - this is needed for subsequent chipset-specific calls */
	ar71xx_detect_sys_type();
	ar71xx_detect_sys_frequency();

	platform_counter_freq = ar71xx_cpu_freq();
	mips_timer_init_params(platform_counter_freq, 1);
	cninit();
	init_static_kenv(boot1_env, sizeof(boot1_env));

	printf("CPU platform: %s\n", ar71xx_get_system_type());
	printf("CPU Frequency=%d MHz\n", u_ar71xx_cpu_freq / 1000000);
	printf("CPU DDR Frequency=%d MHz\n", u_ar71xx_ddr_freq / 1000000);
	printf("CPU AHB Frequency=%d MHz\n", u_ar71xx_ahb_freq / 1000000);
	printf("platform frequency: %lld MHz\n", platform_counter_freq / 1000000);
	printf("CPU reference clock: %d MHz\n", u_ar71xx_refclk / 1000000);
	printf("CPU MDIO clock: %d MHz\n", u_ar71xx_mdio_freq / 1000000);
	printf("arguments: \n");
	printf("  a0 = %08x\n", a0);
	printf("  a1 = %08x\n", a1);
	printf("  a2 = %08x\n", a2);
	printf("  a3 = %08x\n", a3);

	strcpy(cpu_model, ar71xx_get_system_type());

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
	if (MIPS_IS_VALID_PTR(envp)) {
		for (i = 0; envp[i]; i+=2) {
			printf("  %s = %s\n", envp[i], envp[i+1]);
			kern_setenv(envp[i], envp[i+1]);
		}
	}
	else 
		printf ("envp is invalid\n");

	/* Platform setup */
	init_param2(physmem);
	mips_cpu_init();
	pmap_bootstrap();
	mips_proc0_init();
	mutex_init();

	/*
	 * Reset USB devices 
	 */
	ar71xx_init_usb_peripheral();

	/*
	 * Reset internal ethernet switch, if one exists
	 */
	ar71xx_reset_ethernet_switch();

	/*
	 * Initialise the gmac driver.
	 */
	ar71xx_init_gmac();

	/* Redboot if_arge MAC address is in the environment */
	(void) ar71xx_redboot_get_macaddr();

	/* Various other boards need things to come out of EEPROM */
	(void) ar71xx_platform_read_eeprom_mac();

	/* Initialise the MAC address hint map */
	ar71xx_platform_check_mac_hints();

	kdb_init();
#ifdef KDB
	if (boothowto & RB_KDB)
		kdb_enter(KDB_WHY_BOOTFLAGS, "Boot flags requested debugger");
#endif
}
