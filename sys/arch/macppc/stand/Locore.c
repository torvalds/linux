/*	$OpenBSD: Locore.c,v 1.18 2022/10/21 21:26:49 gkoehler Exp $	*/
/*	$NetBSD: Locore.c,v 1.1 1997/04/16 20:29:11 thorpej Exp $	*/

/*
 * Copyright (C) 1995, 1996 Wolfgang Solfrank.
 * Copyright (C) 1995, 1996 TooLs GmbH.
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

#include <macppc/stand/openfirm.h>
#include <dev/cons.h>

#include "libsa.h"

int main(void);

#define ENABLE_DECREMENTER_WORKAROUND
void bat_init(void);
void patch_dec_intr(void);

__dead void exit(void);

static int (*openfirmware)(void *);

static void setup(void);

asm (".text; .globl _entry; _entry: .long _start,0,0");
asm("   .text			\n"
"	.globl	bat_init	\n"
"bat_init:			\n"
"				\n"
"	mfmsr   8		\n"
"	li      0,0		\n"
"	mtmsr   0		\n"
"	isync			\n"
"				\n"
"	mtibatu 0,0		\n"	
"	mtibatu 1,0		\n"
"	mtibatu 2,0		\n"
"	mtibatu 3,0		\n"
"	mtdbatu 0,0		\n"
"	mtdbatu 1,0		\n"
"	mtdbatu 2,0		\n"
"	mtdbatu 3,0		\n"
"				\n"
"	li      9,0x12         	\n"	 /* BATL(0, BAT_M, BAT_PP_RW) */	
"	mtibatl 0,9		\n"
"	mtdbatl 0,9		\n"
"	li      9,0x1ffe        \n"	/* BATU(0, BAT_BL_256M, BAT_Vs) */
"	mtibatu 0,9		\n"
"	mtdbatu 0,9		\n"
"	isync			\n"
"				\n"
"	mtmsr 8  		\n"
"	isync			\n"
"	blr			\n");

#ifdef XCOFF_GLUE
static int stack[8192/4 + 4] __attribute__((__used__));
#endif

__dead void
_start(void *vpd, int res, int (*openfirm)(void *), char *arg, int argl)
{
	extern char etext[];

#ifdef XCOFF_GLUE
	asm(
	"sync			\n"
	"isync			\n"
	"lis	%r1,stack@ha	\n"
	"addi	%r1,%r1,stack@l	\n"
	"addi	%r1,%r1,8192	\n");
#endif
	syncicache((void *)RELOC, etext - (char *)RELOC);

	bat_init();
	openfirmware = openfirm;	/* Save entry to Open Firmware */
#ifdef ENABLE_DECREMENTER_WORKAROUND
	patch_dec_intr();
#endif
	setup();
	main();
	exit();
}

#ifdef ENABLE_DECREMENTER_WORKAROUND
void handle_decr_intr();
__asm (	"	.globl handle_decr_intr\n"
	"handle_decr_intr:\n"
	"	rfi\n");

void
patch_dec_intr()
{
	int time;
	unsigned int *decr_intr = (unsigned int *)0x900;
	unsigned int br_instr;

	/* this hack is to prevent unexpected Decrementer Exceptions
	 * when Apple openfirmware enables interrupts
	 */
	time = 0x40000000;
	asm("mtdec %0" :: "r"(time));

	/* we assume that handle_decr_intr is in the first 128 Meg */
	br_instr = (18 << 23) | (unsigned int)handle_decr_intr;
	*decr_intr = br_instr;
}
#endif

__dead void
_rtt()
{
	static struct {
		char *name;
		int nargs;
		int nreturns;
	} args = {
		"exit",
		0,
		0
	};

	openfirmware(&args);
	while (1);			/* just in case */
}

int
OF_finddevice(char *name)
{
	static struct {
		char *name;
		int nargs;
		int nreturns;
		char *device;
		int phandle;
	} args = {
		"finddevice",
		1,
		1,
	};

	args.device = name;
	if (openfirmware(&args) == -1)
		return -1;
	return args.phandle;
}

int
OF_instance_to_package(int ihandle)
{
	static struct {
		char *name;
		int nargs;
		int nreturns;
		int ihandle;
		int phandle;
	} args = {
		"instance-to-package",
		1,
		1,
	};

	args.ihandle = ihandle;
	if (openfirmware(&args) == -1)
		return -1;
	return args.phandle;
}

int
OF_getprop(int handle, char *prop, void *buf, int buflen)
{
	static struct {
		char *name;
		int nargs;
		int nreturns;
		int phandle;
		char *prop;
		void *buf;
		int buflen;
		int size;
	} args = {
		"getprop",
		4,
		1,
	};

	args.phandle = handle;
	args.prop = prop;
	args.buf = buf;
	args.buflen = buflen;
	if (openfirmware(&args) == -1)
		return -1;
	return args.size;
}

int
OF_open(char *dname)
{
	static struct {
		char *name;
		int nargs;
		int nreturns;
		char *dname;
		int handle;
	} args = {
		"open",
		1,
		1,
	};

	args.dname = dname;
	if (openfirmware(&args) == -1)
		return -1;
	return args.handle;
}

void
OF_close(int handle)
{
	static struct {
		char *name;
		int nargs;
		int nreturns;
		int handle;
	} args = {
		"close",
		1,
		0,
	};

	args.handle = handle;
	openfirmware(&args);
}

int
OF_write(int handle, void *addr, int len)
{
	static struct {
		char *name;
		int nargs;
		int nreturns;
		int ihandle;
		void *addr;
		int len;
		int actual;
	} args = {
		"write",
		3,
		1,
	};

	args.ihandle = handle;
	args.addr = addr;
	args.len = len;
	if (openfirmware(&args) == -1)
		return -1;
	return args.actual;
}

int
OF_read(int handle, void *addr, int len)
{
	static struct {
		char *name;
		int nargs;
		int nreturns;
		int ihandle;
		void *addr;
		int len;
		int actual;
	} args = {
		"read",
		3,
		1,
	};

	args.ihandle = handle;
	args.addr = addr;
	args.len = len;
	if (openfirmware(&args) == -1)
		return -1;
	return args.actual;
}

int
OF_seek(int handle, u_quad_t pos)
{
	static struct {
		char *name;
		int nargs;
		int nreturns;
		int handle;
		int poshi;
		int poslo;
		int status;
	} args = {
		"seek",
		3,
		1,
	};

	args.handle = handle;
	args.poshi = (int)(pos >> 32);
	args.poslo = (int)pos;
	if (openfirmware(&args) == -1)
		return -1;
	return args.status;
}

void *
OF_claim(void *virt, u_int size, u_int align)
{
	static struct {
		char *name;
		int nargs;
		int nreturns;
		void *virt;
		u_int size;
		u_int align;
		void *baseaddr;
	} args = {
		"claim",
		3,
		1,
	};

	args.virt = virt;
	args.size = size;
	args.align = align;
	if (openfirmware(&args) == -1)
		return (void *)-1;
	if (virt != 0)
		return virt;
	return args.baseaddr;
}

void
OF_release(void *virt, u_int size)
{
	static struct {
		char *name;
		int nargs;
		int nreturns;
		void *virt;
		u_int size;
	} args = {
		"release",
		2,
		0,
	};

	args.virt = virt;
	args.size = size;
	openfirmware(&args);
}

int
OF_milliseconds()
{
	static struct {
		char *name;
		int nargs;
		int nreturns;
		int ms;
	} args = {
		"milliseconds",
		0,
		1,
	};

	openfirmware(&args);
	return args.ms;
}

#ifdef __notyet__
void
OF_chain(void *virt, u_int size, void (*entry)(), void *arg, u_int len)
{
	static struct {
		char *name;
		int nargs;
		int nreturns;
		void *virt;
		u_int size;
		void (*entry)();
		void *arg;
		u_int len;
	} args = {
		"chain",
		5,
		0,
	};

	args.virt = virt;
	args.size = size;
	args.entry = entry;
	args.arg = arg;
	args.len = len;
	openfirmware(&args);
}
#else
void
OF_chain(void *virt, u_int size, void (*entry)(), void *arg, u_int len)
{
	/*
	 * This is a REALLY dirty hack till the firmware gets this going
	OF_release(virt, size);
	 */
	entry(0, 0, openfirmware, arg, len);
}
#endif

int
OF_call_method(char *method, int ihandle, int nargs, int nreturns, ...)
{
	va_list ap;
	static struct {
		char *name;
		int nargs;
		int nreturns;
		char *method;
		int ihandle;
		int args_n_results[12];
	} args = {
		"call-method",
		2,
		1,
	};
	int *ip, n;

	if (nargs > 6)
		return -1;
	args.nargs = nargs + 2;
	args.nreturns = nreturns + 1;
	args.method = method;
	args.ihandle = ihandle;
	va_start(ap, nreturns);
	for (ip = args.args_n_results + (n = nargs); --n >= 0;)
		*--ip = va_arg(ap, int);

	if (openfirmware(&args) == -1) {
		va_end(ap);
		return -1;
	}
	if (args.args_n_results[nargs]) {
		va_end(ap);
		return args.args_n_results[nargs];
	}
	for (ip = args.args_n_results + nargs + (n = args.nreturns); --n > 0;)
		*va_arg(ap, int *) = *--ip;
	va_end(ap);
	return 0;
}

static int stdin;
static int stdout;

static void
setup()
{
	int chosen;

	if ((chosen = OF_finddevice("/chosen")) == -1)
		_rtt();
	if (OF_getprop(chosen, "stdin", &stdin, sizeof(stdin)) != sizeof(stdin)
	    || OF_getprop(chosen, "stdout", &stdout, sizeof(stdout)) !=
	    sizeof(stdout))
		_rtt();
	if (stdout == 0) {
		/* screen should be console, but it is not open */
		stdout = OF_open("screen");
	}
}

void
putchar(int c)
{
	char ch = c;
	if (c == '\177') {
		ch = '\b';
		OF_write(stdout, &ch, 1);
		ch = ' ';
		OF_write(stdout, &ch, 1);
		ch = '\b';
	}
	if (c == '\n')
		putchar('\r');
	OF_write(stdout, &ch, 1);
}

void
ofc_probe(struct consdev *cn)
{
	cn->cn_pri = CN_LOWPRI;
	cn->cn_dev = makedev(0,0); /* WTF */
}


void
ofc_init(struct consdev *cn)
{
}

char buffered_char;
int
ofc_getc(dev_t dev)
{
	u_int8_t ch;
	int l;

	if (dev & 0x80)  {
		if (buffered_char != 0)
			return 1;

		l = OF_read(stdin, &ch, 1);
		if (l == 1) {
			buffered_char = ch;
			return 1;
		}
		return 0;
	}

	if (buffered_char != 0) {
		ch = buffered_char;
		buffered_char = 0;
		return ch;
	}

	while ((l = OF_read(stdin, &ch, 1)) != 1)
		if (l != -2 && l != 0)
			return 0;
	return ch;
		
}

void
ofc_putc(dev_t dev, int c)
{
	char ch;

	ch = 'a';
	OF_write(stdout, &ch, 1);
	ch = c;
	if (c == '\177' && c == '\b') {
		ch = 'A';
	}
	OF_write(stdout, &ch, 1);
}


void 
machdep()
{
	cninit();
}
