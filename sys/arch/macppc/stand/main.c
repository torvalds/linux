/*	$OpenBSD: main.c,v 1.14 2024/11/01 17:16:04 gkoehler Exp $	*/
/*	$NetBSD: boot.c,v 1.1 1997/04/16 20:29:17 thorpej Exp $	*/

/*
 * Copyright (c) 1997 Jason R. Thorpe.  All rights reserved.
 * Copyright (C) 1995, 1996 Wolfgang Solfrank.
 * Copyright (C) 1995, 1996 TooLs GmbH.
 * All rights reserved.
 *
 * ELF support derived from NetBSD/alpha's boot loader, written
 * by Christopher G. Demetriou.
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
 *	This product includes software developed by TooLs GmbH.
 * 4. The name of TooLs GmbH may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY TOOLS GMBH ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * First try for the boot code
 *
 * Input syntax is:
 *	[promdev[{:|,}partition]]/[filename] [flags]
 */

#define	ELFSIZE		32		/* We use 32-bit ELF. */

#include <sys/param.h>
#include <sys/exec.h>
#include <sys/exec_elf.h>
#include <sys/reboot.h>
#include <sys/disklabel.h>

#include <lib/libkern/libkern.h>
#include <lib/libsa/loadfile.h>
#include <stand/boot/cmd.h>

#include <machine/cpu.h>

#include <macppc/stand/ofdev.h>
#include <macppc/stand/openfirm.h>

#include "libsa.h"

char bootdev[128];
int boothowto;
int debug;

void
get_alt_bootdev(char *, size_t, char *, size_t);

static void
prom2boot(char *dev)
{
	char *cp, *lp = 0;

	for (cp = dev; *cp; cp++)
		if (*cp == ':')
			lp = cp;
	if (!lp)
		lp = cp;
	*lp = 0;
}

static void
chain(void (*entry)(), char *args, void *ssym, void *esym)
{
	extern char end[];
	int l;
#ifdef __notyet__
	int machine_tag;
#endif

	freeall();

	/*
	 * Stash pointer to end of symbol table after the argument
	 * strings.
	 */
	l = strlen(args) + 1;
	bcopy(&ssym, args + l, sizeof(ssym));
	l += sizeof(ssym);
	bcopy(&esym, args + l, sizeof(esym));
	l += sizeof(esym);

#ifdef __notyet__
	/*
	 * Tell the kernel we're an OpenFirmware system.
	 */
	machine_tag = POWERPC_MACHINE_OPENFIRMWARE;
	bcopy(&machine_tag, args + l, sizeof(machine_tag));
	l += sizeof(machine_tag);
#endif

	OF_chain((void *)RELOC, end - (char *)RELOC, entry, args, l);
	panic("chain");
}

/*
 * XXX This limits the maximum size of the (uncompressed) bsd.rd to a
 * little under 14MB.
 */
#define CLAIM_LIMIT	0x00f00000

char bootline[512];

extern char *kernelfile;

int
main(void)
{
	int chosen;

	/*
	 * Get the boot arguments from Openfirmware
	 */
	if ((chosen = OF_finddevice("/chosen")) == -1 ||
	    OF_getprop(chosen, "bootpath", bootdev, sizeof bootdev) < 0 ||
	    OF_getprop(chosen, "bootargs", bootline, sizeof bootline) < 0) {
		printf("Invalid Openfirmware environment\n");
		exit();
	}
	prom2boot(bootdev);
	get_alt_bootdev(bootdev, sizeof(bootdev), bootline, sizeof(bootline));
	if (bootline[0] != '\0')
		kernelfile = bootline;

	OF_claim((void *)0x00100000, CLAIM_LIMIT, 0); /* XXX */
	boot(0);
	return 0;
}

void
get_alt_bootdev(char *dev, size_t devsz, char *line, size_t linesz)
{
	char *p;
	int len;
	/*
	 * if the kernel image specified contains a ':' it is
	 * [device]:[kernel], so separate the two fields.
	 */
	p = strrchr(line, ':');
	if (p == NULL)
		return;
	/* user specified boot device for kernel */
	len = p - line + 1; /* str len plus nil */
	strlcpy(dev, line, len > devsz ? devsz : len);

	strlcpy(line, p+1, linesz); /* rest of string after ':' */
}


void
devboot(dev_t dev, char *p)
{
	strlcpy(p, bootdev, BOOTDEVLEN);
}

void
run_loadfile(uint64_t *marks, int howto)
{
	char bootline[512];		/* Should check size? */
	u_int32_t entry;
	char *cp;
	void *ssym, *esym;

	strlcpy(bootline, opened_name, sizeof bootline);
	cp = bootline + strlen(bootline);
	*cp++ = ' ';
        *cp = '-';
        if (howto & RB_ASKNAME)
                *++cp = 'a';
        if (howto & RB_CONFIG)
                *++cp = 'c';
        if (howto & RB_SINGLE)
                *++cp = 's';
        if (howto & RB_KDB)
                *++cp = 'd';
        if (howto & RB_GOODRANDOM)
                *++cp = 'R';
        if (*cp == '-')
		*--cp = 0;
	else
		*++cp = 0;

	entry = marks[MARK_ENTRY];
	ssym = (void *)marks[MARK_SYM];
	esym = (void *)marks[MARK_END];
	{
		u_int32_t lastpage;
		lastpage = roundup(marks[MARK_END], PAGE_SIZE);
		OF_release((void*)lastpage, CLAIM_LIMIT - lastpage);
	}

	syncicache((void *)entry, (size_t)ssym - entry);
	chain((void *)entry, bootline, ssym, esym);

	_rtt();
}
