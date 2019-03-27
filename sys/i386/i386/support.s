/*-
 * Copyright (c) 1993 The Regents of the University of California.
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
 * $FreeBSD$
 */

#include <machine/asmacros.h>
#include <machine/cputypes.h>
#include <machine/pmap.h>
#include <machine/specialreg.h>

#include "assym.inc"

#define IDXSHIFT	10

	.text

/*
 * bcopy family
 * void bzero(void *buf, u_int len)
 */
ENTRY(bzero)
	pushl	%edi
	movl	8(%esp),%edi
	movl	12(%esp),%ecx
	xorl	%eax,%eax
	shrl	$2,%ecx
	rep
	stosl
	movl	12(%esp),%ecx
	andl	$3,%ecx
	rep
	stosb
	popl	%edi
	ret
END(bzero)

ENTRY(sse2_pagezero)
	pushl	%ebx
	movl	8(%esp),%ecx
	movl	%ecx,%eax
	addl	$4096,%eax
	xor	%ebx,%ebx
	jmp	1f
	/*
	 * The loop takes 14 bytes.  Ensure that it doesn't cross a 16-byte
	 * cache line.
	 */
	.p2align 4,0x90
1:
	movnti	%ebx,(%ecx)
	movnti	%ebx,4(%ecx)
	addl	$8,%ecx
	cmpl	%ecx,%eax
	jne	1b
	sfence
	popl	%ebx
	ret
END(sse2_pagezero)

ENTRY(i686_pagezero)
	pushl	%edi
	pushl	%ebx

	movl	12(%esp),%edi
	movl	$1024,%ecx

	ALIGN_TEXT
1:
	xorl	%eax,%eax
	repe
	scasl
	jnz	2f

	popl	%ebx
	popl	%edi
	ret

	ALIGN_TEXT

2:
	incl	%ecx
	subl	$4,%edi

	movl	%ecx,%edx
	cmpl	$16,%ecx

	jge	3f

	movl	%edi,%ebx
	andl	$0x3f,%ebx
	shrl	%ebx
	shrl	%ebx
	movl	$16,%ecx
	subl	%ebx,%ecx

3:
	subl	%ecx,%edx
	rep
	stosl

	movl	%edx,%ecx
	testl	%edx,%edx
	jnz	1b

	popl	%ebx
	popl	%edi
	ret
END(i686_pagezero)

/* fillw(pat, base, cnt) */
ENTRY(fillw)
	pushl	%edi
	movl	8(%esp),%eax
	movl	12(%esp),%edi
	movl	16(%esp),%ecx
	rep
	stosw
	popl	%edi
	ret
END(fillw)

/*
 * memmove(dst, src, cnt) (return dst)
 * bcopy(src, dst, cnt)
 *  ws@tools.de     (Wolfgang Solfrank, TooLs GmbH) +49-228-985800
 */
ENTRY(bcopy)
	movl	4(%esp),%eax
	movl	8(%esp),%edx
	movl	%eax,8(%esp)
	movl	%edx,4(%esp)
	MEXITCOUNT
	jmp	memmove
END(bcopy)

ENTRY(memmove)
	pushl	%ebp
	movl	%esp,%ebp
	pushl	%esi
	pushl	%edi
	movl	8(%ebp),%edi
	movl	12(%ebp),%esi
1:
	movl	16(%ebp),%ecx

	movl	%edi,%eax
	subl	%esi,%eax
	cmpl	%ecx,%eax			/* overlapping && src < dst? */
	jb	1f

	shrl	$2,%ecx				/* copy by 32-bit words */
	rep
	movsl
	movl	16(%ebp),%ecx
	andl	$3,%ecx				/* any bytes left? */
	rep
	movsb
	popl	%edi
	popl	%esi
	movl	8(%ebp),%eax			/* return dst for memmove */
	popl	%ebp
	ret

	ALIGN_TEXT
1:
	addl	%ecx,%edi			/* copy backwards */
	addl	%ecx,%esi
	decl	%edi
	decl	%esi
	andl	$3,%ecx				/* any fractional bytes? */
	std
	rep
	movsb
	movl	16(%ebp),%ecx			/* copy remainder by 32-bit words */
	shrl	$2,%ecx
	subl	$3,%esi
	subl	$3,%edi
	rep
	movsl
	popl	%edi
	popl	%esi
	cld
	movl	8(%ebp),%eax			/* return dst for memmove */
	popl	%ebp
	ret
END(memmove)

/*
 * Note: memcpy does not support overlapping copies
 */
ENTRY(memcpy)
	pushl	%edi
	pushl	%esi
	movl	12(%esp),%edi
	movl	16(%esp),%esi
	movl	20(%esp),%ecx
	movl	%edi,%eax
	shrl	$2,%ecx				/* copy by 32-bit words */
	rep
	movsl
	movl	20(%esp),%ecx
	andl	$3,%ecx				/* any bytes left? */
	rep
	movsb
	popl	%esi
	popl	%edi
	ret
END(memcpy)

/*
 * copystr(from, to, maxlen, int *lencopied) - MP SAFE
 */
ENTRY(copystr)
	pushl	%esi
	pushl	%edi

	movl	12(%esp),%esi			/* %esi = from */
	movl	16(%esp),%edi			/* %edi = to */
	movl	20(%esp),%edx			/* %edx = maxlen */
	incl	%edx
1:
	decl	%edx
	jz	4f
	lodsb
	stosb
	orb	%al,%al
	jnz	1b

	/* Success -- 0 byte reached */
	decl	%edx
	xorl	%eax,%eax
	jmp	6f
4:
	/* edx is zero -- return ENAMETOOLONG */
	movl	$ENAMETOOLONG,%eax

6:
	/* set *lencopied and return %eax */
	movl	20(%esp),%ecx
	subl	%edx,%ecx
	movl	24(%esp),%edx
	testl	%edx,%edx
	jz	7f
	movl	%ecx,(%edx)
7:
	popl	%edi
	popl	%esi
	ret
END(copystr)

ENTRY(bcmp)
	pushl	%edi
	pushl	%esi
	movl	12(%esp),%edi
	movl	16(%esp),%esi
	movl	20(%esp),%edx

	movl	%edx,%ecx
	shrl	$2,%ecx
	repe
	cmpsl
	jne	1f

	movl	%edx,%ecx
	andl	$3,%ecx
	repe
	cmpsb
1:
	setne	%al
	movsbl	%al,%eax
	popl	%esi
	popl	%edi
	ret
END(bcmp)

/*
 * Handling of special 386 registers and descriptor tables etc
 */
/* void lgdt(struct region_descriptor *rdp); */
ENTRY(lgdt)
	/* reload the descriptor table */
	movl	4(%esp),%eax
	lgdt	(%eax)

	/* flush the prefetch q */
	jmp	1f
	nop
1:
	/* reload "stale" selectors */
	movl	$KDSEL,%eax
	movl	%eax,%ds
	movl	%eax,%es
	movl	%eax,%gs
	movl	%eax,%ss
	movl	$KPSEL,%eax
	movl	%eax,%fs

	/* reload code selector by turning return into intersegmental return */
	movl	(%esp),%eax
	pushl	%eax
	movl	$KCSEL,4(%esp)
	MEXITCOUNT
	lret
END(lgdt)

/* ssdtosd(*ssdp,*sdp) */
ENTRY(ssdtosd)
	pushl	%ebx
	movl	8(%esp),%ecx
	movl	8(%ecx),%ebx
	shll	$16,%ebx
	movl	(%ecx),%edx
	roll	$16,%edx
	movb	%dh,%bl
	movb	%dl,%bh
	rorl	$8,%ebx
	movl	4(%ecx),%eax
	movw	%ax,%dx
	andl	$0xf0000,%eax
	orl	%eax,%ebx
	movl	12(%esp),%ecx
	movl	%edx,(%ecx)
	movl	%ebx,4(%ecx)
	popl	%ebx
	ret
END(ssdtosd)

/* void reset_dbregs() */
ENTRY(reset_dbregs)
	movl	$0,%eax
	movl	%eax,%dr7	/* disable all breakpoints first */
	movl	%eax,%dr0
	movl	%eax,%dr1
	movl	%eax,%dr2
	movl	%eax,%dr3
	movl	%eax,%dr6
	ret
END(reset_dbregs)

/*****************************************************************************/
/* setjump, longjump                                                         */
/*****************************************************************************/

ENTRY(setjmp)
	movl	4(%esp),%eax
	movl	%ebx,(%eax)			/* save ebx */
	movl	%esp,4(%eax)			/* save esp */
	movl	%ebp,8(%eax)			/* save ebp */
	movl	%esi,12(%eax)			/* save esi */
	movl	%edi,16(%eax)			/* save edi */
	movl	(%esp),%edx			/* get rta */
	movl	%edx,20(%eax)			/* save eip */
	xorl	%eax,%eax			/* return(0); */
	ret
END(setjmp)

ENTRY(longjmp)
	movl	4(%esp),%eax
	movl	(%eax),%ebx			/* restore ebx */
	movl	4(%eax),%esp			/* restore esp */
	movl	8(%eax),%ebp			/* restore ebp */
	movl	12(%eax),%esi			/* restore esi */
	movl	16(%eax),%edi			/* restore edi */
	movl	20(%eax),%edx			/* get rta */
	movl	%edx,(%esp)			/* put in return frame */
	xorl	%eax,%eax			/* return(1); */
	incl	%eax
	ret
END(longjmp)

/*
 * Support for reading MSRs in the safe manner.  (Instead of panic on #gp,
 * return an error.)
 */
ENTRY(rdmsr_safe)
/* int rdmsr_safe(u_int msr, uint64_t *data) */
	movl	PCPU(CURPCB),%ecx
	movl	$msr_onfault,PCB_ONFAULT(%ecx)

	movl	4(%esp),%ecx
	rdmsr
	movl	8(%esp),%ecx
	movl	%eax,(%ecx)
	movl	%edx,4(%ecx)
	xorl	%eax,%eax

	movl	PCPU(CURPCB),%ecx
	movl	%eax,PCB_ONFAULT(%ecx)

	ret

/*
 * Support for writing MSRs in the safe manner.  (Instead of panic on #gp,
 * return an error.)
 */
ENTRY(wrmsr_safe)
/* int wrmsr_safe(u_int msr, uint64_t data) */
	movl	PCPU(CURPCB),%ecx
	movl	$msr_onfault,PCB_ONFAULT(%ecx)

	movl	4(%esp),%ecx
	movl	8(%esp),%eax
	movl	12(%esp),%edx
	wrmsr
	xorl	%eax,%eax

	movl	PCPU(CURPCB),%ecx
	movl	%eax,PCB_ONFAULT(%ecx)

	ret

/*
 * MSR operations fault handler
 */
	ALIGN_TEXT
msr_onfault:
	movl	PCPU(CURPCB),%ecx
	movl	$0,PCB_ONFAULT(%ecx)
	movl	$EFAULT,%eax
	ret

ENTRY(handle_ibrs_entry)
	cmpb	$0,hw_ibrs_active
	je	1f
	movl	$MSR_IA32_SPEC_CTRL,%ecx
	rdmsr
	orl	$(IA32_SPEC_CTRL_IBRS|IA32_SPEC_CTRL_STIBP),%eax
	orl	$(IA32_SPEC_CTRL_IBRS|IA32_SPEC_CTRL_STIBP)>>32,%edx
	wrmsr
	movb	$1,PCPU(IBPB_SET)
	/*
	 * i386 does not implement SMEP, but the 4/4 split makes this not
	 * that important.
	 */
1:	ret
END(handle_ibrs_entry)

ENTRY(handle_ibrs_exit)
	cmpb	$0,PCPU(IBPB_SET)
	je	1f
	movl	$MSR_IA32_SPEC_CTRL,%ecx
	rdmsr
	andl	$~(IA32_SPEC_CTRL_IBRS|IA32_SPEC_CTRL_STIBP),%eax
	andl	$~((IA32_SPEC_CTRL_IBRS|IA32_SPEC_CTRL_STIBP)>>32),%edx
	wrmsr
	movb	$0,PCPU(IBPB_SET)
1:	ret
END(handle_ibrs_exit)
