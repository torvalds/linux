/*-
 * Copyright (c) 2013-2014 Robert N. M. Watson
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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

#include <sys/param.h>
#include <sys/linker.h>
#include <sys/reboot.h>

#include <machine/bootinfo.h>
#include <machine/elf.h>

#include <stand.h>
#include <bootstrap.h>
#include <loader.h>
#include <mips.h>

#ifdef LOADER_USB_SUPPORT
#include <storage/umass_common.h>
#endif

static int	__elfN(exec)(struct preloaded_file *);
static void	extract_currdev(struct bootinfo *);

struct devsw *devsw[] = {
	&beri_cfi_disk,
	&beri_sdcard_disk,
#ifdef LOADER_USB_SUPPORT
	&umass_disk,
#endif
	NULL
};

struct arch_switch archsw;

struct file_format *file_formats[] = {
	&beri_elf,
	NULL
};

struct fs_ops *file_system[] = {
#ifdef LOADER_UFS_SUPPORT
	&ufs_fsops,
#endif
	NULL
};

struct console *consoles[] = {
	&altera_jtag_uart_console,
	NULL
};

extern uint8_t	__bss_start, __bss_end;
extern uint8_t	__heap_start, __heap_end;

static int
__elfN(exec)(struct preloaded_file *fp)
{

	return (EFTYPE);
}

/*
 * Capture arguments from boot2 for later reuse when launching the kernel.
 * Note that we choose not to maintain a pointer to boo2_bootinfop after
 * initial argument processing: this is because we might load the kernel over
 * the spot where boot2 was running, so we can't pass that pointer on to the
 * kernel.  To be on the safe side, never reference it outside of the body of
 * main(), instead preserving a copy.
 */
int		 boot2_argc;
char		**boot2_argv;
char		**boot2_envv;

struct bootinfo	boot2_bootinfo;

int
main(int argc, char *argv[], char *envv[], struct bootinfo *bootinfop)
{
	struct devsw **dp;

	/* NB: Must be sure to bzero() before using any globals. */
	bzero(&__bss_start, &__bss_end - &__bss_start);

	boot2_argc = argc;
	boot2_argv = argv;
	boot2_envv = envv;
	boot2_bootinfo = *bootinfop;	/* Copy rather than by reference. */

	setheap(&__heap_start, &__heap_end);

	/*
	 * Pick up console settings from boot2; probe console.
	 */
	if (bootinfop->bi_boot2opts & RB_MULTIPLE) {
		if (bootinfop->bi_boot2opts & RB_SERIAL)
			setenv("console", "comconsole vidconsole", 1);
		else
			setenv("console", "vidconsole comconsole", 1);
	} else if (bootinfop->bi_boot2opts & RB_SERIAL)
		setenv("console", "comconsole", 1);
	else if (bootinfop->bi_boot2opts & RB_MUTE)
		setenv("console", "nullconsole", 1);
	cons_probe();
	setenv("LINES", "24", 1);

	printf("%s(%d, %p, %p, %p (%p))\n", __func__, argc, argv, envv,
	    bootinfop, (void *)bootinfop->bi_memsize);

	/*
	 * Initialise devices.
	 */
	for (dp = devsw; *dp != NULL; dp++) {
		if ((*dp)->dv_init != NULL)
			(*dp)->dv_init();
	}
	extract_currdev(bootinfop);

	printf("\n%s", bootprog_info);
#if 0
	printf("bootpath=\"%s\"\n", bootpath);
#endif

	interact();
	return (0);
}

static void
extract_currdev(struct bootinfo *bootinfop)
{
	const char *bootdev;

	/*
	 * Pick up boot device information from boot2.
	 *
	 * XXXRW: Someday: device units.
	 */
	switch(bootinfop->bi_boot_dev_type) {
	case BOOTINFO_DEV_TYPE_DRAM:
		bootdev = "dram0";
		break;

	case BOOTINFO_DEV_TYPE_CFI:
		bootdev = "cfi0";
		break;

	case BOOTINFO_DEV_TYPE_SDCARD:
		bootdev = "sdcard0";
		break;

	default:
		bootdev = NULL;
	}

	if (bootdev != NULL) {
		env_setenv("currdev", EV_VOLATILE, bootdev, NULL, env_nounset);
		env_setenv("loaddev", EV_VOLATILE, bootdev, env_noset,
		    env_nounset);
	}
}

void
abort(void)
{

	printf("error: loader abort\n");
	while (1);
	__unreachable();
}

void
exit(int code)
{

	printf("error: loader exit\n");
	while (1);
	__unreachable();
}

void
longjmperror(void)
{

	printf("error: loader longjmp error\n");
	while (1);
	__unreachable();
}

time_t
time(time_t *tloc)
{

	/* We can't provide time since UTC, so just provide time since boot. */
	return (cp0_count_get() / 100000000);
}

/*
 * Delay - in usecs
 *
 * NOTE: We are assuming that the CPU is running at 100MHz.
 */
void
delay(int usecs)
{
	uint32_t delta;
	uint32_t curr;
	uint32_t last;

	last = cp0_count_get();
	while (usecs > 0) {
		curr = cp0_count_get();
		delta = curr - last;
		while (usecs > 0 && delta >= 100) {
			usecs--;
			last += 100;
			delta -= 100;
		}
	}
}
