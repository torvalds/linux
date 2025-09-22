/*	$OpenBSD: autoconf.c,v 1.40 2025/06/28 16:04:09 miod Exp $	*/
/*	$NetBSD: autoconf.c,v 1.16 1996/11/13 21:13:04 cgd Exp $	*/

/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * All advertising materials mentioning features or use of this software
 * must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratory.
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
 *	@(#)autoconf.c	8.4 (Berkeley) 10/1/93
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/disklabel.h>
#include <sys/conf.h>
#include <sys/reboot.h>
#include <sys/device.h>

#include <uvm/uvm_extern.h>

#include <machine/autoconf.h>
#include <machine/rpb.h>
#include <machine/prom.h>
#include <machine/cpuconf.h>
#include <machine/intr.h>

#include <dev/cons.h>

struct device		*booted_device;
int			booted_partition;
struct bootdev_data	*bootdev_data;
char			boot_dev[128];

void	parse_prom_bootdev(void);
int	atoi(char *);

void
unmap_startup(void)
{
	extern uint32_t kernel_text[], endboot[];
	uint32_t *word = kernel_text;

	/* Cannot unmap KSEG0; smash with 0x00000000 (call_pall PAL_halt) */
	while (word < endboot)
		*word++ = 0x00000000;
}

/*
 * cpu_configure:
 * called at boot time, configure all devices on system
 */
void
cpu_configure(void)
{
	parse_prom_bootdev();
	softintr_init();

	unmap_startup();

	/*
	 * Disable interrupts during autoconfiguration.  splhigh() won't
	 * work, because it simply _raises_ the IPL, so if machine checks
	 * are disabled, they'll stay disabled.  Machine checks are needed
	 * during autoconfig.
	 */
	(void)alpha_pal_swpipl(ALPHA_PSL_IPL_HIGH);
	if (config_rootfound("mainbus", "mainbus") == NULL)
		panic("no mainbus found");
	(void)spl0();

	hwrpb_restart_setup();
	cold = 0;
}

void
diskconf(void)
{
	struct device *bootdv;
	int bootpartition;

	if (booted_device == NULL)
		printf("WARNING: can't figure what device matches \"%s\"\n",
		    boot_dev);
	bootdv = booted_device;
	bootpartition = booted_partition;

	setroot(bootdv, bootpartition, RB_USERREQ);
	dumpconf();
}

void
parse_prom_bootdev(void)
{
	static struct bootdev_data bd;
	char *cp, *scp, *boot_fields[8];
	int i, done;

	booted_device = NULL;
	booted_partition = 0;
	bootdev_data = NULL;

	bcopy(bootinfo.booted_dev, boot_dev, sizeof bootinfo.booted_dev);
#if 0
	printf("parse_prom_bootdev: boot dev = \"%s\"\n", boot_dev);
#endif

	i = 0;
	scp = cp = boot_dev;
	for (done = 0; !done; cp++) {
		if (*cp != ' ' && *cp != '\0')
			continue;
		if (*cp == '\0')
			done = 1;

		*cp = '\0';
		boot_fields[i++] = scp;
		scp = cp + 1;
		if (i == 8)
			done = 1;
	}
	if (i != 8)
		return;		/* doesn't look like anything we know! */

#if 0
	printf("i = %d, done = %d\n", i, done);
	for (i--; i >= 0; i--)
		printf("%d = %s\n", i, boot_fields[i]);
#endif

	bd.protocol = boot_fields[0];
	bd.bus = atoi(boot_fields[1]);
	bd.slot = atoi(boot_fields[2]);
	bd.channel = atoi(boot_fields[3]);
	bd.remote_address = boot_fields[4];
	bd.unit = atoi(boot_fields[5]);
	bd.boot_dev_type = atoi(boot_fields[6]);
	bd.ctrl_dev_type = boot_fields[7];

#if 0
	printf("parsed: proto = %s, bus = %d, slot = %d, channel = %d,\n",
	    bd.protocol, bd.bus, bd.slot, bd.channel);
	printf("\tremote = %s, unit = %d, dev_type = %d, ctrl_type = %s\n",
	    bd.remote_address, bd.unit, bd.boot_dev_type, bd.ctrl_dev_type);
#endif

	bootdev_data = &bd;
}

int
atoi(char *s)
{
	int n, neg;

	n = 0;
	neg = 0;

	while (*s == '-') {
		s++;
		neg = !neg;
	}

	while (*s != '\0') {
		if (*s < '0' || *s > '9')
			break;

		n = (10 * n) + (*s - '0');
		s++;
	}

	return (neg ? -n : n);
}

void
device_register(struct device *dev, void *aux)
{
	if (bootdev_data == NULL) {
		/*
		 * There is no hope.
		 */
		return;
	}

	if (platform.device_register)
		(*platform.device_register)(dev, aux);
}

const struct nam2blk nam2blk[] = {
	{ "wd",		0 },
	{ "cd",		3 },
	{ "fd",		4 },
	{ "rd",		6 },
	{ "sd",		8 },
	{ "vnd",	9 },
	{ NULL,		-1 }
};
