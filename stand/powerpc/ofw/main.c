/*-
 * Copyright (c) 2000 Benno Rice <benno@jeamland.net>
 * Copyright (c) 2000 Stephane Potvin <sepotvin@videotron.ca>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
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

#include <stand.h>
#include "openfirm.h"
#include "libofw.h"
#include "bootstrap.h"

#include <machine/psl.h>

struct arch_switch	archsw;		/* MI/MD interface boundary */

extern char end[];

uint32_t	acells, scells;

static char bootargs[128];

#define	HEAP_SIZE	0x800000
static char heap[HEAP_SIZE]; // In BSS, so uses no space

#define OF_puts(fd, text) OF_write(fd, text, strlen(text))

static __inline register_t
mfmsr(void)
{
	register_t value;

	__asm __volatile ("mfmsr %0" : "=r"(value));

	return (value);
}

void
init_heap(void)
{
	bzero(heap, HEAP_SIZE);

	setheap(heap, (void *)((int)heap + HEAP_SIZE));
}

uint64_t
memsize(void)
{
	phandle_t	memoryp;
	cell_t		reg[24];
	int		i, sz;
	uint64_t	memsz;

	memsz = 0;
	memoryp = OF_instance_to_package(memory);

	sz = OF_getprop(memoryp, "reg", &reg, sizeof(reg));
	sz /= sizeof(reg[0]);

	for (i = 0; i < sz; i += (acells + scells)) {
		if (scells > 1)
			memsz += (uint64_t)reg[i + acells] << 32;
		memsz += reg[i + acells + scells - 1];
	}

	return (memsz);
}

int
main(int (*openfirm)(void *))
{
	phandle_t	root;
	int		i;
	char		bootpath[64];
	char		*ch;
	int		bargc;
	char		**bargv;

	/*
	 * Initialise the Open Firmware routines by giving them the entry point.
	 */
	OF_init(openfirm);

	root = OF_finddevice("/");

	scells = acells = 1;
	OF_getprop(root, "#address-cells", &acells, sizeof(acells));
	OF_getprop(root, "#size-cells", &scells, sizeof(scells));

	/*
	 * Initialise the heap as early as possible.  Once this is done,
	 * alloc() is usable. The stack is buried inside us, so this is
	 * safe.
	 */
	init_heap();

	/*
         * Set up console.
         */
	cons_probe();

	/*
	 * March through the device switch probing for things.
	 */
	for (i = 0; devsw[i] != NULL; i++)
		if (devsw[i]->dv_init != NULL)
			(devsw[i]->dv_init)();

	printf("\n%s", bootprog_info);
	printf("Memory: %lldKB\n", memsize() / 1024);

	OF_getprop(chosen, "bootpath", bootpath, 64);
	ch = strchr(bootpath, ':');
	*ch = '\0';
	printf("Booted from: %s\n", bootpath);

	printf("\n");

	/*
	 * Only parse the first bootarg if present. It should
	 * be simple to handle extra arguments
	 */
	OF_getprop(chosen, "bootargs", bootargs, sizeof(bootargs));
	bargc = 0;
	parse(&bargc, &bargv, bootargs);
	if (bargc == 1)
		env_setenv("currdev", EV_VOLATILE, bargv[0], ofw_setcurrdev,
		    env_nounset);
	else
		env_setenv("currdev", EV_VOLATILE, bootpath,
			   ofw_setcurrdev, env_nounset);
	env_setenv("loaddev", EV_VOLATILE, bootpath, env_noset,
	    env_nounset);
	setenv("LINES", "24", 1);		/* optional */

	/*
	 * On non-Apple hardware, where it works reliably, pass flattened
	 * device trees to the kernel by default instead of OF CI pointers.
	 * Apple hardware is the only virtual-mode OF implementation in
	 * existence, so far as I am aware, so use that as a flag.
	 */
	if (!(mfmsr() & PSL_DR))
		setenv("usefdt", "1", 1);

	archsw.arch_getdev = ofw_getdev;
	archsw.arch_copyin = ofw_copyin;
	archsw.arch_copyout = ofw_copyout;
	archsw.arch_readin = ofw_readin;
	archsw.arch_autoload = ofw_autoload;

	interact();				/* doesn't return */

	OF_exit();

	return 0;
}

COMMAND_SET(halt, "halt", "halt the system", command_halt);

static int
command_halt(int argc, char *argv[])
{

	OF_exit();
	return (CMD_OK);
}

COMMAND_SET(memmap, "memmap", "print memory map", command_memmap);

int
command_memmap(int argc, char **argv)
{

	ofw_memmap(acells);
	return (CMD_OK);
}
