/*-
 * Copyright (c) 2018 The FreeBSD Foundation
 *
 * This software was developed by Konstantin Belousov <kib@FreeBSD.org>
 * under sponsorship from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#include <machine/specialreg.h>
#include <machine/cpufunc.h>

static void
crt1_handle_rel(const Elf_Rel *r)
{
	Elf_Addr *where, target;
	u_int cpuid_supported, p[4];
	uint32_t cpu_feature, cpu_feature2;
	uint32_t cpu_stdext_feature, cpu_stdext_feature2;

	__asm __volatile(
	    "	pushfl\n"
	    "	popl	%%eax\n"
	    "	movl    %%eax,%%ecx\n"
	    "	xorl    $0x200000,%%eax\n"
	    "	pushl	%%eax\n"
	    "	popfl\n"
	    "	pushfl\n"
	    "	popl    %%eax\n"
	    "	xorl    %%eax,%%ecx\n"
	    "	je	1f\n"
	    "	movl	$1,%0\n"
	    "	jmp	2f\n"
	    "1:	movl	$0,%0\n"
	    "2:\n"
	    : "=r" (cpuid_supported) : : "eax", "ecx", "cc");
	if (cpuid_supported) {
		do_cpuid(1, p);
		cpu_feature = p[3];
		cpu_feature2 = p[2];
		do_cpuid(0, p);
		if (p[0] >= 7) {
			cpuid_count(7, 0, p);
			cpu_stdext_feature = p[1];
			cpu_stdext_feature2 = p[2];
		} else {
			cpu_stdext_feature = 0;
			cpu_stdext_feature2 = 0;
		}
	} else {
		cpu_feature = 0;
		cpu_feature2 = 0;
		cpu_stdext_feature = 0;
		cpu_stdext_feature2 = 0;
	}

	switch (ELF_R_TYPE(r->r_info)) {
	case R_386_IRELATIVE:
		where = (Elf_Addr *)r->r_offset;
		target = ((Elf_Addr (*)(uint32_t, uint32_t, uint32_t,
		    uint32_t))*where)(cpu_feature, cpu_feature2,
		    cpu_stdext_feature, cpu_stdext_feature2);
		*where = target;
		break;
	}
}
