/* $Id: head.h,v 1.39 2000/05/26 22:18:45 ecd Exp $ */
#ifndef __SPARC_HEAD_H
#define __SPARC_HEAD_H

#define KERNBASE        0xf0000000  /* First address the kernel will eventually be */
#define LOAD_ADDR       0x4000      /* prom jumps to us here unless this is elf /boot */
#define SUN4C_SEGSZ     (1 << 18)
#define SRMMU_L1_KBASE_OFFSET ((KERNBASE>>24)<<2)  /* Used in boot remapping. */
#define INTS_ENAB        0x01           /* entry.S uses this. */

#define SUN4_PROM_VECTOR 0xFFE81000     /* SUN4 PROM needs to be hardwired */

#define WRITE_PAUSE      nop; nop; nop; /* Have to do this after %wim/%psr chg */
#define NOP_INSN         0x01000000     /* Used to patch sparc_save_state */

/* Here are some trap goodies */

/* Generic trap entry. */
#define TRAP_ENTRY(type, label) \
	rd %psr, %l0; b label; rd %wim, %l3; nop;

/* Data/text faults. Defaults to sun4c version at boot time. */
#define SPARC_TFAULT rd %psr, %l0; rd %wim, %l3; b sun4c_fault; mov 1, %l7;
#define SPARC_DFAULT rd %psr, %l0; rd %wim, %l3; b sun4c_fault; mov 0, %l7;
#define SRMMU_TFAULT rd %psr, %l0; rd %wim, %l3; b srmmu_fault; mov 1, %l7;
#define SRMMU_DFAULT rd %psr, %l0; rd %wim, %l3; b srmmu_fault; mov 0, %l7;

/* This is for traps we should NEVER get. */
#define BAD_TRAP(num) \
        rd %psr, %l0; mov num, %l7; b bad_trap_handler; rd %wim, %l3;

/* This is for traps when we want just skip the instruction which caused it */
#define SKIP_TRAP(type, name) \
	jmpl %l2, %g0; rett %l2 + 4; nop; nop;

/* Notice that for the system calls we pull a trick.  We load up a
 * different pointer to the system call vector table in %l7, but call
 * the same generic system call low-level entry point.  The trap table
 * entry sequences are also HyperSparc pipeline friendly ;-)
 */

/* Software trap for Linux system calls. */
#define LINUX_SYSCALL_TRAP \
        sethi %hi(sys_call_table), %l7; \
        or %l7, %lo(sys_call_table), %l7; \
        b linux_sparc_syscall; \
        rd %psr, %l0;

/* Software trap for SunOS4.1.x system calls. */
#define SUNOS_SYSCALL_TRAP \
        rd %psr, %l0; \
        sethi %hi(sunos_sys_table), %l7; \
        b linux_sparc_syscall; \
        or %l7, %lo(sunos_sys_table), %l7;

#define SUNOS_NO_SYSCALL_TRAP \
        b sunos_syscall; \
        rd %psr, %l0; \
        nop; \
        nop;

/* Software trap for Slowaris system calls. */
#define SOLARIS_SYSCALL_TRAP \
        b solaris_syscall; \
        rd %psr, %l0; \
        nop; \
        nop;

#define INDIRECT_SOLARIS_SYSCALL(x) \
	mov x, %g1; \
	b solaris_syscall; \
	rd %psr, %l0; \
	nop;

#define BREAKPOINT_TRAP \
	b breakpoint_trap; \
	rd %psr,%l0; \
	nop; \
	nop;

/* Software trap for Sparc-netbsd system calls. */
#define NETBSD_SYSCALL_TRAP \
        sethi %hi(sys_call_table), %l7; \
        or %l7, %lo(sys_call_table), %l7; \
        b bsd_syscall; \
        rd %psr, %l0;

/* The Get Condition Codes software trap for userland. */
#define GETCC_TRAP \
        b getcc_trap_handler; mov %psr, %l0; nop; nop;

/* The Set Condition Codes software trap for userland. */
#define SETCC_TRAP \
        b setcc_trap_handler; mov %psr, %l0; nop; nop;

/* The Get PSR software trap for userland. */
#define GETPSR_TRAP \
	mov %psr, %i0; jmp %l2; rett %l2 + 4; nop;

/* This is for hard interrupts from level 1-14, 15 is non-maskable (nmi) and
 * gets handled with another macro.
 */
#define TRAP_ENTRY_INTERRUPT(int_level) \
        mov int_level, %l7; rd %psr, %l0; b real_irq_entry; rd %wim, %l3;

/* NMI's (Non Maskable Interrupts) are special, you can't keep them
 * from coming in, and basically if you get one, the shows over. ;(
 * On the sun4c they are usually asynchronous memory errors, on the
 * the sun4m they could be either due to mem errors or a software
 * initiated interrupt from the prom/kern on an SMP box saying "I
 * command you to do CPU tricks, read your mailbox for more info."
 */
#define NMI_TRAP \
        rd %wim, %l3; b linux_trap_nmi_sun4c; mov %psr, %l0; nop;

/* Window overflows/underflows are special and we need to try to be as
 * efficient as possible here....
 */
#define WINDOW_SPILL \
        rd %psr, %l0; rd %wim, %l3; b spill_window_entry; andcc %l0, PSR_PS, %g0;

#define WINDOW_FILL \
        rd %psr, %l0; rd %wim, %l3; b fill_window_entry; andcc %l0, PSR_PS, %g0;

#endif /* __SPARC_HEAD_H */
