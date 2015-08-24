/*
 * entry_from_vm86.c - tests kernel entries from vm86 mode
 * Copyright (c) 2014-2015 Andrew Lutomirski
 *
 * This exercises a few paths that need to special-case vm86 mode.
 *
 * GPL v2.
 */

#define _GNU_SOURCE

#include <assert.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <sys/signal.h>
#include <sys/ucontext.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <sys/mman.h>
#include <err.h>
#include <stddef.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/vm86.h>

static unsigned long load_addr = 0x10000;
static int nerrs = 0;

asm (
	".pushsection .rodata\n\t"
	".type vmcode_bound, @object\n\t"
	"vmcode:\n\t"
	"vmcode_bound:\n\t"
	".code16\n\t"
	"bound %ax, (2048)\n\t"
	"int3\n\t"
	"vmcode_sysenter:\n\t"
	"sysenter\n\t"
	".size vmcode, . - vmcode\n\t"
	"end_vmcode:\n\t"
	".code32\n\t"
	".popsection"
	);

extern unsigned char vmcode[], end_vmcode[];
extern unsigned char vmcode_bound[], vmcode_sysenter[];

static void do_test(struct vm86plus_struct *v86, unsigned long eip,
		    const char *text)
{
	long ret;

	printf("[RUN]\t%s from vm86 mode\n", text);
	v86->regs.eip = eip;
	ret = vm86(VM86_ENTER, v86);

	if (ret == -1 && errno == ENOSYS) {
		printf("[SKIP]\tvm86 not supported\n");
		return;
	}

	if (VM86_TYPE(ret) == VM86_INTx) {
		char trapname[32];
		int trapno = VM86_ARG(ret);
		if (trapno == 13)
			strcpy(trapname, "GP");
		else if (trapno == 5)
			strcpy(trapname, "BR");
		else if (trapno == 14)
			strcpy(trapname, "PF");
		else
			sprintf(trapname, "%d", trapno);

		printf("[OK]\tExited vm86 mode due to #%s\n", trapname);
	} else if (VM86_TYPE(ret) == VM86_UNKNOWN) {
		printf("[OK]\tExited vm86 mode due to unhandled GP fault\n");
	} else {
		printf("[OK]\tExited vm86 mode due to type %ld, arg %ld\n",
		       VM86_TYPE(ret), VM86_ARG(ret));
	}
}

int main(void)
{
	struct vm86plus_struct v86;
	unsigned char *addr = mmap((void *)load_addr, 4096,
				   PROT_READ | PROT_WRITE | PROT_EXEC,
				   MAP_ANONYMOUS | MAP_PRIVATE, -1,0);
	if (addr != (unsigned char *)load_addr)
		err(1, "mmap");

	memcpy(addr, vmcode, end_vmcode - vmcode);
	addr[2048] = 2;
	addr[2050] = 3;

	memset(&v86, 0, sizeof(v86));

	v86.regs.cs = load_addr / 16;
	v86.regs.ss = load_addr / 16;
	v86.regs.ds = load_addr / 16;
	v86.regs.es = load_addr / 16;

	assert((v86.regs.cs & 3) == 0);	/* Looks like RPL = 0 */

	/* #BR -- should deliver SIG??? */
	do_test(&v86, vmcode_bound - vmcode, "#BR");

	/* SYSENTER -- should cause #GP or #UD depending on CPU */
	do_test(&v86, vmcode_sysenter - vmcode, "SYSENTER");

	return (nerrs == 0 ? 0 : 1);
}
