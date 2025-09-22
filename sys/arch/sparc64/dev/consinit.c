/*	$OpenBSD: consinit.c,v 1.17 2017/12/30 20:46:59 guenther Exp $	*/
/*	$NetBSD: consinit.c,v 1.9 2000/10/20 05:32:35 mrg Exp $	*/

/*-
 * Copyright (c) 1999 Eduardo E. Horvath
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "pcons.h"
#include "ukbd.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/ioctl.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/tty.h>
#include <sys/time.h>
#include <sys/syslog.h>

#include <machine/autoconf.h>
#include <machine/openfirm.h>
#include <machine/conf.h>
#include <machine/cpu.h>
#include <machine/psl.h>
#include <machine/z8530var.h>
#include <machine/sparc64.h>

#include <dev/cons.h>

#include <sparc64/dev/cons.h>

#include <dev/usb/ukbdvar.h>

cons_decl(prom_);

int stdin = 0, stdout = 0;

/*
 * The console is set to this one initially,
 * which lets us use the PROM until consinit()
 * is called to select a real console.
 */
struct consdev consdev_prom = {
	prom_cnprobe,
	prom_cninit,
	prom_cngetc,
	prom_cnputc,
	prom_cnpollc,
	NULL
};

/*
 * The console table pointer is statically initialized
 * to point to the PROM (output only) table, so that
 * early calls to printf will work.
 */
struct consdev *cn_tab = &consdev_prom;

void
prom_cnprobe(struct consdev *cd)
{
#if NPCONS > 0
	int maj;

	for (maj = 0; maj < nchrdev; maj++)
		if (cdevsw[maj].d_open == pconsopen)
			break;
	cd->cn_dev = makedev(maj, 0);
	cd->cn_pri = CN_MIDPRI;
#endif
}

int
prom_cngetc(dev_t dev)
{
	unsigned char ch = '\0';
	int l;
#ifdef DDB
	static int nplus = 0;
#endif
	
	while ((l = OF_read(stdin, &ch, 1)) != 1)
		/* void */;
#ifdef DDB
	if (ch == '+') {
		if (nplus++ > 3)
			db_enter();
	} else
		nplus = 0;
#endif
	if (ch == '\r')
		ch = '\n';
	if (ch == '\b')
		ch = '\177';
	return ch;
}

void
prom_cninit(struct consdev *cn)
{
	if (!stdin) stdin = OF_stdin();
	if (!stdout) stdout = OF_stdout();
}

/*
 * PROM console output putchar.
 */
void
prom_cnputc(dev_t dev, int c)
{
	int s;
	char c0 = (c & 0x7f);

#if 0
	if (!stdout) stdout = OF_stdout();
#endif
	s = splhigh();
	OF_write(stdout, &c0, 1);
	splx(s);
}

void
prom_cnpollc(dev_t dev, int on)
{
	if (on) {
                /* Entering debugger. */
                fb_unblank();
	} else {
                /* Resuming kernel. */
	}
#if NPCONS > 0
	pcons_cnpollc(dev, on);
#endif  
}

/*****************************************************************/

#ifdef	DEBUG
#define	DBPRINT(x)	prom_printf x
#else
#define	DBPRINT(x)
#endif

/*
 * This function replaces sys/dev/cninit.c
 * Determine which device is the console using
 * the PROM "input source" and "output sink".
 */
void
consinit(void)
{
	register int chosen;
	char buffer[128];
	extern int stdinnode, fbnode;
	char *consname = "unknown";
	
	DBPRINT(("consinit()\r\n"));
	if (cn_tab != &consdev_prom) return;
	
	DBPRINT(("setting up stdin\r\n"));
	chosen = OF_finddevice("/chosen");
	OF_getprop(chosen, "stdin",  &stdin, sizeof(stdin));
	DBPRINT(("stdin instance = %x\r\n", stdin));
	
	if ((stdinnode = OF_instance_to_package(stdin)) == 0) {
		printf("WARNING: no PROM stdin\n");
	}
#if NUKBD > 0
	else {
		if (OF_getprop(stdinnode, "compatible", buffer,
		    sizeof(buffer)) != -1 && strncmp("usb", buffer, 3) == 0)
			ukbd_cnattach();
	}
#endif

	DBPRINT(("setting up stdout\r\n"));
	OF_getprop(chosen, "stdout", &stdout, sizeof(stdout));
	
	DBPRINT(("stdout instance = %x\r\n", stdout));
	
	if ((fbnode = OF_instance_to_package(stdout)) == 0)
		printf("WARNING: no PROM stdout\n");
	
	DBPRINT(("stdout package = %x\r\n", fbnode));
	
	if (stdinnode && (OF_getproplen(stdinnode,"keyboard") >= 0)) {
		consname = "keyboard/display";
	} else if (fbnode && 
		   (OF_instance_to_path(stdin, buffer, sizeof(buffer)) >= 0)) {
		consname = buffer;
	}
	printf("console is %s\n", consname);
 
	/* Initialize PROM console */
	(*cn_tab->cn_probe)(cn_tab);
	(*cn_tab->cn_init)(cn_tab);
}
