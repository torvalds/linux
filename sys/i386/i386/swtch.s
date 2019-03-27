/*-
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz.
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

#include "opt_sched.h"

#include <machine/asmacros.h>

#include "assym.inc"

#if defined(SMP) && defined(SCHED_ULE)
#define	SETOP		xchgl
#define	BLOCK_SPIN(reg)							\
		movl		$blocked_lock,%eax ;			\
	100: ;								\
		lock ;							\
		cmpxchgl	%eax,TD_LOCK(reg) ;			\
		jne		101f ;					\
		pause ;							\
		jmp		100b ;					\
	101:
#else
#define	SETOP		movl
#define	BLOCK_SPIN(reg)
#endif

/*****************************************************************************/
/* Scheduling                                                                */
/*****************************************************************************/

	.text

/*
 * cpu_throw()
 *
 * This is the second half of cpu_switch(). It is used when the current
 * thread is either a dummy or slated to die, and we no longer care
 * about its state.  This is only a slight optimization and is probably
 * not worth it anymore.  Note that we need to clear the pm_active bits so
 * we do need the old proc if it still exists.
 * 0(%esp) = ret
 * 4(%esp) = oldtd
 * 8(%esp) = newtd
 */
ENTRY(cpu_throw)
	movl	PCPU(CPUID), %esi
	/* release bit from old pm_active */
	movl	PCPU(CURPMAP), %ebx
#ifdef SMP
	lock
#endif
	btrl	%esi, PM_ACTIVE(%ebx)		/* clear old */
	movl	8(%esp),%ecx			/* New thread */
	movl	TD_PCB(%ecx),%edx
	/* set bit in new pm_active */
	movl	TD_PROC(%ecx),%eax
	movl	P_VMSPACE(%eax), %ebx
	addl	$VM_PMAP, %ebx
	movl	%ebx, PCPU(CURPMAP)
#ifdef SMP
	lock
#endif
	btsl	%esi, PM_ACTIVE(%ebx)		/* set new */
	jmp	sw1
END(cpu_throw)

/*
 * cpu_switch(old, new)
 *
 * Save the current thread state, then select the next thread to run
 * and load its state.
 * 0(%esp) = ret
 * 4(%esp) = oldtd
 * 8(%esp) = newtd
 * 12(%esp) = newlock
 */
ENTRY(cpu_switch)

	/* Switch to new thread.  First, save context. */
	movl	4(%esp),%ecx

#ifdef INVARIANTS
	testl	%ecx,%ecx			/* no thread? */
	jz	badsw2				/* no, panic */
#endif

	movl	TD_PCB(%ecx),%edx

	movl	(%esp),%eax			/* Hardware registers */
	movl	%eax,PCB_EIP(%edx)
	movl	%ebx,PCB_EBX(%edx)
	movl	%esp,PCB_ESP(%edx)
	movl	%ebp,PCB_EBP(%edx)
	movl	%esi,PCB_ESI(%edx)
	movl	%edi,PCB_EDI(%edx)
	mov	%gs,PCB_GS(%edx)
	/* Test if debug registers should be saved. */
	testl	$PCB_DBREGS,PCB_FLAGS(%edx)
	jz      1f                              /* no, skip over */
	movl    %dr7,%eax                       /* yes, do the save */
	movl    %eax,PCB_DR7(%edx)
	andl    $0x0000fc00, %eax               /* disable all watchpoints */
	movl    %eax,%dr7
	movl    %dr6,%eax
	movl    %eax,PCB_DR6(%edx)
	movl    %dr3,%eax
	movl    %eax,PCB_DR3(%edx)
	movl    %dr2,%eax
	movl    %eax,PCB_DR2(%edx)
	movl    %dr1,%eax
	movl    %eax,PCB_DR1(%edx)
	movl    %dr0,%eax
	movl    %eax,PCB_DR0(%edx)
1:

	/* have we used fp, and need a save? */
	cmpl	%ecx,PCPU(FPCURTHREAD)
	jne	1f
	pushl	PCB_SAVEFPU(%edx)		/* h/w bugs make saving complicated */
	call	npxsave				/* do it in a big C function */
	popl	%eax
1:

	/* Save is done.  Now fire up new thread. */
	movl	4(%esp),%edi
	movl	8(%esp),%ecx			/* New thread */
	movl	12(%esp),%esi			/* New lock */
#ifdef INVARIANTS
	testl	%ecx,%ecx			/* no thread? */
	jz	badsw3				/* no, panic */
#endif
	movl	TD_PCB(%ecx),%edx

	/* Switchout td_lock */
	movl	%esi,%eax
	movl	PCPU(CPUID),%esi
	SETOP	%eax,TD_LOCK(%edi)

	/* Release bit from old pmap->pm_active */
	movl	PCPU(CURPMAP), %ebx
#ifdef SMP
	lock
#endif
	btrl	%esi, PM_ACTIVE(%ebx)		/* clear old */

	/* Set bit in new pmap->pm_active */
	movl	TD_PROC(%ecx),%eax		/* newproc */
	movl	P_VMSPACE(%eax), %ebx
	addl	$VM_PMAP, %ebx
	movl	%ebx, PCPU(CURPMAP)
#ifdef SMP
	lock
#endif
	btsl	%esi, PM_ACTIVE(%ebx)		/* set new */
sw1:
	BLOCK_SPIN(%ecx)
	/*
	 * At this point, we have managed thread locks and are ready
	 * to load up the rest of the next context.
	 */

	/* Load a pointer to the thread kernel stack into PCPU. */
	leal	-VM86_STACK_SPACE(%edx), %eax	/* leave space for vm86 */
	movl	%eax, PCPU(KESP0)

	cmpl	$0, PCB_EXT(%edx)		/* has pcb extension? */
	je	1f				/* If not, use the default */
	movl	$1, PCPU(PRIVATE_TSS) 		/* mark use of private tss */
	movl	PCB_EXT(%edx), %edi		/* new tss descriptor */
	movl	PCPU(TRAMPSTK), %ebx
	movl	%ebx, PCB_EXT_TSS+TSS_ESP0(%edi)
	jmp	2f				/* Load it up */

1:	/*
	 * Use the common default TSS instead of our own.
	 * Stack pointer in the common TSS points to the trampoline stack
	 * already and should be not changed.
	 *
	 * Test this CPU's flag to see if this CPU was using a private TSS.
	 */
	cmpl	$0, PCPU(PRIVATE_TSS)		/* Already using the common? */
	je	3f				/* if so, skip reloading */
	movl	$0, PCPU(PRIVATE_TSS)
	PCPU_ADDR(COMMON_TSSD, %edi)
2:
	/* Move correct tss descriptor into GDT slot, then reload tr. */
	movl	PCPU(TSS_GDT), %ebx		/* entry in GDT */
	movl	0(%edi), %eax
	movl	4(%edi), %esi
	movl	%eax, 0(%ebx)
	movl	%esi, 4(%ebx)
	movl	$GPROC0_SEL*8, %esi		/* GSEL(GPROC0_SEL, SEL_KPL) */
	ltr	%si
3:

	/* Copy the %fs and %gs selectors into this pcpu gdt */
	leal	PCB_FSD(%edx), %esi
	movl	PCPU(FSGS_GDT), %edi
	movl	0(%esi), %eax		/* %fs selector */
	movl	4(%esi), %ebx
	movl	%eax, 0(%edi)
	movl	%ebx, 4(%edi)
	movl	8(%esi), %eax		/* %gs selector, comes straight after */
	movl	12(%esi), %ebx
	movl	%eax, 8(%edi)
	movl	%ebx, 12(%edi)

	/* Restore context. */
	movl	PCB_EBX(%edx),%ebx
	movl	PCB_ESP(%edx),%esp
	movl	PCB_EBP(%edx),%ebp
	movl	PCB_ESI(%edx),%esi
	movl	PCB_EDI(%edx),%edi
	movl	PCB_EIP(%edx),%eax
	movl	%eax,(%esp)

	movl	%edx, PCPU(CURPCB)
	movl	%ecx, PCPU(CURTHREAD)		/* into next thread */

	/*
	 * Determine the LDT to use and load it if is the default one and
	 * that is not the current one.
	 */
	movl	TD_PROC(%ecx),%eax
	cmpl    $0,P_MD+MD_LDT(%eax)
	jnz	1f
	movl	_default_ldt,%eax
	cmpl	PCPU(CURRENTLDT),%eax
	je	2f
	lldt	_default_ldt
	movl	%eax,PCPU(CURRENTLDT)
	jmp	2f
1:
	/* Load the LDT when it is not the default one. */
	pushl	%edx				/* Preserve pointer to pcb. */
	addl	$P_MD,%eax			/* Pointer to mdproc is arg. */
	pushl	%eax
	/*
	 * Holding dt_lock prevents context switches, so dt_lock cannot
	 * be held now and set_user_ldt() will not deadlock acquiring it.
	 */
	call	set_user_ldt
	addl	$4,%esp
	popl	%edx
2:

	/* This must be done after loading the user LDT. */
	.globl	cpu_switch_load_gs
cpu_switch_load_gs:
	mov	PCB_GS(%edx),%gs

	pushl	%edx
	pushl	PCPU(CURTHREAD)
	call	npxswitch
	popl	%edx
	popl	%edx

	/* Test if debug registers should be restored. */
	testl	$PCB_DBREGS,PCB_FLAGS(%edx)
	jz      1f

	/*
	 * Restore debug registers.  The special code for dr7 is to
	 * preserve the current values of its reserved bits.
	 */
	movl    PCB_DR6(%edx),%eax
	movl    %eax,%dr6
	movl    PCB_DR3(%edx),%eax
	movl    %eax,%dr3
	movl    PCB_DR2(%edx),%eax
	movl    %eax,%dr2
	movl    PCB_DR1(%edx),%eax
	movl    %eax,%dr1
	movl    PCB_DR0(%edx),%eax
	movl    %eax,%dr0
	movl	%dr7,%eax
	andl    $0x0000fc00,%eax
	movl    PCB_DR7(%edx),%ecx
	andl	$~0x0000fc00,%ecx
	orl     %ecx,%eax
	movl    %eax,%dr7
1:
	ret

#ifdef INVARIANTS
badsw1:
	pushal
	pushl	$sw0_1
	call	panic
sw0_1:	.asciz	"cpu_throw: no newthread supplied"

badsw2:
	pushal
	pushl	$sw0_2
	call	panic
sw0_2:	.asciz	"cpu_switch: no curthread supplied"

badsw3:
	pushal
	pushl	$sw0_3
	call	panic
sw0_3:	.asciz	"cpu_switch: no newthread supplied"
#endif
END(cpu_switch)

/*
 * savectx(pcb)
 * Update pcb, saving current processor state.
 */
ENTRY(savectx)
	/* Fetch PCB. */
	movl	4(%esp),%ecx

	/* Save caller's return address.  Child won't execute this routine. */
	movl	(%esp),%eax
	movl	%eax,PCB_EIP(%ecx)

	movl	%cr3,%eax
	movl	%eax,PCB_CR3(%ecx)

	movl	%ebx,PCB_EBX(%ecx)
	movl	%esp,PCB_ESP(%ecx)
	movl	%ebp,PCB_EBP(%ecx)
	movl	%esi,PCB_ESI(%ecx)
	movl	%edi,PCB_EDI(%ecx)
	mov	%gs,PCB_GS(%ecx)

	movl	%cr0,%eax
	movl	%eax,PCB_CR0(%ecx)
	movl	%cr2,%eax
	movl	%eax,PCB_CR2(%ecx)
	movl	%cr4,%eax
	movl	%eax,PCB_CR4(%ecx)

	movl	%dr0,%eax
	movl	%eax,PCB_DR0(%ecx)
	movl	%dr1,%eax
	movl	%eax,PCB_DR1(%ecx)
	movl	%dr2,%eax
	movl	%eax,PCB_DR2(%ecx)
	movl	%dr3,%eax
	movl	%eax,PCB_DR3(%ecx)
	movl	%dr6,%eax
	movl	%eax,PCB_DR6(%ecx)
	movl	%dr7,%eax
	movl	%eax,PCB_DR7(%ecx)

	mov	%ds,PCB_DS(%ecx)
	mov	%es,PCB_ES(%ecx)
	mov	%fs,PCB_FS(%ecx)
	mov	%ss,PCB_SS(%ecx)
	
	sgdt	PCB_GDT(%ecx)
	sidt	PCB_IDT(%ecx)
	sldt	PCB_LDT(%ecx)
	str	PCB_TR(%ecx)

	movl	$1,%eax
	ret
END(savectx)

/*
 * resumectx(pcb) __fastcall
 * Resuming processor state from pcb.
 */
ENTRY(resumectx)
	/* Restore GDT. */
	lgdt	PCB_GDT(%ecx)

	/* Restore segment registers */
	movzwl	PCB_DS(%ecx),%eax
	mov	%ax,%ds
	movzwl	PCB_ES(%ecx),%eax
	mov	%ax,%es
	movzwl	PCB_FS(%ecx),%eax
	mov	%ax,%fs
	movzwl	PCB_GS(%ecx),%eax
	movw	%ax,%gs
	movzwl	PCB_SS(%ecx),%eax
	mov	%ax,%ss

	/* Restore CR2, CR4, CR3 and CR0 */
	movl	PCB_CR2(%ecx),%eax
	movl	%eax,%cr2
	movl	PCB_CR4(%ecx),%eax
	movl	%eax,%cr4
	movl	PCB_CR3(%ecx),%eax
	movl	%eax,%cr3
	movl	PCB_CR0(%ecx),%eax
	movl	%eax,%cr0
	jmp	1f
1:

	/* Restore descriptor tables */
	lidt	PCB_IDT(%ecx)
	lldt	PCB_LDT(%ecx)

#define SDT_SYS386TSS	9
#define SDT_SYS386BSY	11
	/* Clear "task busy" bit and reload TR */
	movl	PCPU(TSS_GDT),%eax
	andb	$(~SDT_SYS386BSY | SDT_SYS386TSS),5(%eax)
	movzwl	PCB_TR(%ecx),%eax
	ltr	%ax
#undef SDT_SYS386TSS
#undef SDT_SYS386BSY

	/* Restore debug registers */
	movl	PCB_DR0(%ecx),%eax
	movl	%eax,%dr0
	movl	PCB_DR1(%ecx),%eax
	movl	%eax,%dr1
	movl	PCB_DR2(%ecx),%eax
	movl	%eax,%dr2
	movl	PCB_DR3(%ecx),%eax
	movl	%eax,%dr3
	movl	PCB_DR6(%ecx),%eax
	movl	%eax,%dr6
	movl	PCB_DR7(%ecx),%eax
	movl	%eax,%dr7

	/* Restore other registers */
	movl	PCB_EDI(%ecx),%edi
	movl	PCB_ESI(%ecx),%esi
	movl	PCB_EBP(%ecx),%ebp
	movl	PCB_ESP(%ecx),%esp
	movl	PCB_EBX(%ecx),%ebx

	/* reload code selector by turning return into intersegmental return */
	pushl	PCB_EIP(%ecx)
	movl	$KCSEL,4(%esp)
	xorl	%eax,%eax
	lret
END(resumectx)
