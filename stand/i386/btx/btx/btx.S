/*
 * Copyright (c) 1998 Robert Nordier
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are freely
 * permitted provided that the above copyright notice and this
 * paragraph and the following disclaimer are duplicated in all
 * such forms.
 *
 * This software is provided "AS IS" and without any express or
 * implied warranties, including, without limitation, the implied
 * warranties of merchantability and fitness for a particular
 * purpose.
 *
 * $FreeBSD$
 */

#include <bootargs.h>

/*
 * Memory layout.
 */
		.set MEM_BTX,0x1000		# Start of BTX memory
		.set MEM_ESP0,0x1800		# Supervisor stack
		.set MEM_BUF,0x1800		# Scratch buffer
		.set MEM_ESPR,0x5e00		# Real mode stack
		.set MEM_IDT,0x5e00		# IDT
		.set MEM_TSS,0x5f98		# TSS
		.set MEM_MAP,0x6000		# I/O bit map
		.set MEM_TSS_END,0x7fff		# End of TSS
		.set MEM_ORG,0x9000		# BTX code
		.set MEM_USR,0xa000		# Start of user memory
/*
 * Paging control.
 */
		.set PAG_SIZ,0x1000		# Page size
		.set PAG_CNT,0x1000		# Pages to map
/*
 * Fields in %eflags.
 */
		.set PSL_RESERVED_DEFAULT,0x00000002
		.set PSL_T,0x00000100		# Trap flag
		.set PSL_I,0x00000200		# Interrupt enable flag
		.set PSL_D,0x00000400		# String instruction direction
		.set PSL_NT,0x00004000		# Nested task flag
		.set PSL_VM,0x00020000		# Virtual 8086 mode flag
		.set PSL_AC,0x00040000		# Alignment check flag
/*
 * Segment selectors.
 */
		.set SEL_SCODE,0x8		# Supervisor code
		.set SEL_SDATA,0x10		# Supervisor data
		.set SEL_RCODE,0x18		# Real mode code
		.set SEL_RDATA,0x20		# Real mode data
		.set SEL_UCODE,0x28|3		# User code
		.set SEL_UDATA,0x30|3		# User data
		.set SEL_TSS,0x38		# TSS
/*
 * Task state segment fields.
 */
		.set TSS_ESP0,0x4		# PL 0 ESP
		.set TSS_SS0,0x8		# PL 0 SS
		.set TSS_MAP,0x66		# I/O bit map base
/*
 * System calls.
 */
		.set SYS_EXIT,0x0		# Exit
		.set SYS_EXEC,0x1		# Exec
/*
 * Fields in V86 interface structure.
 */
		.set V86_CTL,0x0		# Control flags
		.set V86_ADDR,0x4		# Int number/address
		.set V86_ES,0x8			# V86 ES
		.set V86_DS,0xc			# V86 DS
		.set V86_FS,0x10		# V86 FS
		.set V86_GS,0x14		# V86 GS
/*
 * V86 control flags.
 */
		.set V86F_ADDR,0x10000		# Segment:offset address
		.set V86F_CALLF,0x20000		# Emulate far call
		.set V86F_FLAGS,0x40000		# Return flags
/*
 * Dump format control bytes.
 */
		.set DMP_X16,0x1		# Word
		.set DMP_X32,0x2		# Long
		.set DMP_MEM,0x4		# Memory
		.set DMP_EOL,0x8		# End of line
/*
 * Screen defaults and assumptions.
 */
		.set SCR_MAT,0x7		# Mode/attribute
		.set SCR_COL,0x50		# Columns per row
		.set SCR_ROW,0x19		# Rows per screen
/*
 * BIOS Data Area locations.
 */
		.set BDA_MEM,0x413		# Free memory
		.set BDA_SCR,0x449		# Video mode
		.set BDA_POS,0x450		# Cursor position
		.set BDA_BOOT,0x472		# Boot howto flag
/*
 * Derivations, for brevity.
 */
		.set _ESP0H,MEM_ESP0>>0x8	# Byte 1 of ESP0
		.set _TSSIO,MEM_MAP-MEM_TSS	# TSS I/O base
		.set _TSSLM,MEM_TSS_END-MEM_TSS	# TSS limit
		.set _IDTLM,MEM_TSS-MEM_IDT-1	# IDT limit
/*
 * Code segment.
 */
		.globl start
		.code16
start:						# Start of code
/*
 * BTX header.
 */
btx_hdr:	.byte 0xeb			# Machine ID
		.byte 0xe			# Header size
		.ascii "BTX"			# Magic
		.byte 0x1			# Major version
		.byte 0x2			# Minor version
		.byte BTX_FLAGS			# Flags
		.word PAG_CNT-MEM_ORG>>0xc	# Paging control
		.word break-start		# Text size
		.long 0x0			# Entry address
/*
 * Initialization routine.
 */
init:		cli				# Disable interrupts
		xor %ax,%ax			# Zero/segment
		mov %ax,%ss			# Set up
		mov $MEM_ESP0,%sp		#  stack
		mov %ax,%es			# Address
		mov %ax,%ds			#  data
		pushl $0x2			# Clear
		popfl				#  flags
/*
 * Initialize memory.
 */
		mov $MEM_IDT,%di		# Memory to initialize
		mov $(MEM_ORG-MEM_IDT)/2,%cx	# Words to zero
		rep				# Zero-fill
		stosw				#  memory
/*
 * Update real mode IDT for reflecting hardware interrupts.
 */
		mov $intr20,%bx			# Address first handler
		mov $0x10,%cx			# Number of handlers
		mov $0x20*4,%di			# First real mode IDT entry
init.0:		mov %bx,(%di)			# Store IP
		inc %di				# Address next
		inc %di				#  entry
		stosw				# Store CS
		add $4,%bx			# Next handler
		loop init.0			# Next IRQ
/*
 * Create IDT.
 */
		mov $MEM_IDT,%di
		mov $idtctl,%si			# Control string
init.1: 	lodsb				# Get entry
		cbw				#  count
		xchg %ax,%cx			#  as word
		jcxz init.4			# If done
		lodsb				# Get segment
		xchg %ax,%dx	 		#  P:DPL:type
		lodsw				# Get control
		xchg %ax,%bx			#  set
		lodsw				# Get handler offset
		mov $SEL_SCODE,%dh		# Segment selector
init.2: 	shr %bx				# Handle this int?
		jnc init.3			# No
		mov %ax,(%di)			# Set handler offset
		mov %dh,0x2(%di)		#  and selector
		mov %dl,0x5(%di)		# Set P:DPL:type
		add $0x4,%ax			# Next handler
init.3: 	lea 0x8(%di),%di		# Next entry
		loop init.2			# Till set done
		jmp init.1			# Continue
/*
 * Initialize TSS.
 */
init.4: 	movb $_ESP0H,TSS_ESP0+1(%di)	# Set ESP0
		movb $SEL_SDATA,TSS_SS0(%di)	# Set SS0
		movb $_TSSIO,TSS_MAP(%di)	# Set I/O bit map base
/*
 * Bring up the system.
 */
		mov $0x2820,%bx			# Set protected mode
		callw setpic			#  IRQ offsets
		lidt idtdesc	 		# Set IDT
		lgdt gdtdesc	 		# Set GDT
		mov %cr0,%eax			# Switch to protected
		inc %ax				#  mode
		mov %eax,%cr0			#
		ljmp $SEL_SCODE,$init.8		# To 32-bit code
		.code32
init.8: 	xorl %ecx,%ecx			# Zero
		movb $SEL_SDATA,%cl		# To 32-bit
		movw %cx,%ss			#  stack
/*
 * Launch user task.
 */
		movb $SEL_TSS,%cl		# Set task
		ltr %cx				#  register
		movl $MEM_USR,%edx		# User base address
		movzwl %ss:BDA_MEM,%eax 	# Get free memory
		shll $0xa,%eax			# To bytes
		subl $ARGSPACE,%eax		# Less arg space
		subl %edx,%eax			# Less base
		movb $SEL_UDATA,%cl		# User data selector
		pushl %ecx			# Set SS
		pushl %eax			# Set ESP
		push $0x202			# Set flags (IF set)
		push $SEL_UCODE			# Set CS
		pushl btx_hdr+0xc		# Set EIP
		pushl %ecx			# Set GS
		pushl %ecx			# Set FS
		pushl %ecx			# Set DS
		pushl %ecx			# Set ES
		pushl %edx			# Set EAX
		movb $0x7,%cl			# Set remaining
init.9:		push $0x0			#  general
		loop init.9			#  registers
#ifdef BTX_SERIAL
		call sio_init			# setup the serial console
#endif
		popa				#  and initialize
		popl %es			# Initialize
		popl %ds			#  user
		popl %fs			#  segment
		popl %gs			#  registers
		iret				# To user mode
/*
 * Exit routine.
 */
exit:		cli				# Disable interrupts
		movl $MEM_ESP0,%esp		# Clear stack
/*
 * Turn off paging.
 */
		movl %cr0,%eax			# Get CR0
		andl $~0x80000000,%eax		# Disable
		movl %eax,%cr0			#  paging
		xorl %ecx,%ecx			# Zero
		movl %ecx,%cr3			# Flush TLB
/*
 * Restore the GDT in case we caught a kernel trap.
 */
		lgdt %cs:gdtdesc		# Set GDT
/*
 * To 16 bits.
 */
		ljmpw $SEL_RCODE,$exit.1	# Reload CS
		.code16
exit.1: 	mov $SEL_RDATA,%cl		# 16-bit selector
		mov %cx,%ss			# Reload SS
		mov %cx,%ds			# Load
		mov %cx,%es			#  remaining
		mov %cx,%fs			#  segment
		mov %cx,%gs			#  registers
/*
 * To real-address mode.
 */
		dec %ax				# Switch to
		mov %eax,%cr0			#  real mode
		ljmp $0x0,$exit.2		# Reload CS
exit.2: 	xor %ax,%ax			# Real mode segment
		mov %ax,%ss			# Reload SS
		mov %ax,%ds			# Address data
		mov $0x7008,%bx			# Set real mode
		callw setpic			#  IRQ offsets
		lidt ivtdesc	 		# Set IVT
/*
 * Reboot or await reset.
 */
		sti				# Enable interrupts
		testb $0x1,btx_hdr+0x7		# Reboot?
exit.3:		jz exit.3			# No
		movw $0x1234, BDA_BOOT		# Do a warm boot
		ljmp $0xf000,$0xfff0		# reboot the machine
/*
 * Set IRQ offsets by reprogramming 8259A PICs.
 */
setpic: 	in $0x21,%al			# Save master
		push %ax			#  IMR
		in $0xa1,%al			# Save slave
		push %ax			#  IMR
		movb $0x11,%al			# ICW1 to
		outb %al,$0x20			#  master,
		outb %al,$0xa0			#  slave
		movb %bl,%al			# ICW2 to
		outb %al,$0x21			#  master
		movb %bh,%al			# ICW2 to
		outb %al,$0xa1			#  slave
		movb $0x4,%al			# ICW3 to
		outb %al,$0x21			#  master
		movb $0x2,%al			# ICW3 to
		outb %al,$0xa1			#  slave
		movb $0x1,%al			# ICW4 to
		outb %al,$0x21			#  master,
		outb %al,$0xa1			#  slave
		pop %ax				# Restore slave
		outb %al,$0xa1			#  IMR
		pop %ax				# Restore master
		outb %al,$0x21			#  IMR
		retw				# To caller
		.code32
/*
 * Exception jump table.
 */
intx00: 	push $0x0			# Int 0x0: #DE
		jmp ex_noc			# Divide error
		push $0x1			# Int 0x1: #DB
		jmp ex_noc			# Debug
		push $0x3			# Int 0x3: #BP
		jmp ex_noc			# Breakpoint
		push $0x4			# Int 0x4: #OF
		jmp ex_noc			# Overflow
		push $0x5			# Int 0x5: #BR
		jmp ex_noc			# BOUND range exceeded
		push $0x6			# Int 0x6: #UD
		jmp ex_noc			# Invalid opcode
		push $0x7			# Int 0x7: #NM
		jmp ex_noc			# Device not available
		push $0x8			# Int 0x8: #DF
		jmp except			# Double fault
		push $0xa			# Int 0xa: #TS
		jmp except			# Invalid TSS
		push $0xb			# Int 0xb: #NP
		jmp except			# Segment not present
		push $0xc			# Int 0xc: #SS
		jmp except			# Stack segment fault
		push $0xd			# Int 0xd: #GP
		jmp except			# General protection
		push $0xe			# Int 0xe: #PF
		jmp except			# Page fault
intx10: 	push $0x10			# Int 0x10: #MF
		jmp ex_noc			# Floating-point error
/*
 * Save a zero error code.
 */
ex_noc: 	pushl (%esp,1)			# Duplicate int no
		movb $0x0,0x4(%esp,1)		# Fake error code
/*
 * Handle exception.
 */
except: 	cld				# String ops inc
		pushl %ds			# Save
		pushl %es			#  most
		pusha				#  registers
		pushl %gs			# Set GS
		pushl %fs			# Set FS
		pushl %ds			# Set DS
		pushl %es			# Set ES
		cmpw $SEL_SCODE,0x44(%esp,1)	# Supervisor mode?
		jne except.1			# No
		pushl %ss			# Set SS
		jmp except.2			# Join common code
except.1:	pushl 0x50(%esp,1)		# Set SS
except.2:	pushl 0x50(%esp,1)		# Set ESP
		push $SEL_SDATA			# Set up
		popl %ds			#  to
		pushl %ds			#  address
		popl %es			#  data
		movl %esp,%ebx			# Stack frame
		movl $dmpfmt,%esi		# Dump format string
		movl $MEM_BUF,%edi		# Buffer
		pushl %edi			# Dump to
		call dump			#  buffer
		popl %esi			#  and
		call putstr			#  display
		leal 0x18(%esp,1),%esp		# Discard frame
		popa				# Restore
		popl %es			#  registers
		popl %ds			#  saved
		cmpb $0x3,(%esp,1)		# Breakpoint?
		je except.3			# Yes
		cmpb $0x1,(%esp,1)		# Debug?
		jne except.2a			# No
		testl $PSL_T,0x10(%esp,1)	# Trap flag set?
		jnz except.3			# Yes
except.2a:	jmp exit			# Exit
except.3:	leal 0x8(%esp,1),%esp		# Discard err, int no
		iret				# From interrupt

/*
 * Reboot the machine by setting the reboot flag and exiting
 */
reboot:		orb $0x1,btx_hdr+0x7		# Set the reboot flag
		jmp exit			# Terminate BTX and reboot

/*
 * Protected Mode Hardware interrupt jump table.
 */
intx20: 	push $0x8			# Int 0x20: IRQ0
		jmp int_hw			# V86 int 0x8
		push $0x9			# Int 0x21: IRQ1
		jmp int_hw			# V86 int 0x9
		push $0xa			# Int 0x22: IRQ2
		jmp int_hw			# V86 int 0xa
		push $0xb			# Int 0x23: IRQ3
		jmp int_hw			# V86 int 0xb
		push $0xc			# Int 0x24: IRQ4
		jmp int_hw			# V86 int 0xc
		push $0xd			# Int 0x25: IRQ5
		jmp int_hw			# V86 int 0xd
		push $0xe			# Int 0x26: IRQ6
		jmp int_hw			# V86 int 0xe
		push $0xf			# Int 0x27: IRQ7
		jmp int_hw			# V86 int 0xf
		push $0x70			# Int 0x28: IRQ8
		jmp int_hw			# V86 int 0x70
		push $0x71			# Int 0x29: IRQ9
		jmp int_hw			# V86 int 0x71
		push $0x72			# Int 0x2a: IRQ10
		jmp int_hw			# V86 int 0x72
		push $0x73			# Int 0x2b: IRQ11
		jmp int_hw			# V86 int 0x73
		push $0x74			# Int 0x2c: IRQ12
		jmp int_hw			# V86 int 0x74
		push $0x75			# Int 0x2d: IRQ13
		jmp int_hw			# V86 int 0x75
		push $0x76			# Int 0x2e: IRQ14
		jmp int_hw			# V86 int 0x76
		push $0x77			# Int 0x2f: IRQ15
		jmp int_hw			# V86 int 0x77

/*
 * Invoke real mode interrupt/function call from user mode with arguments.
 */
intx31: 	pushl $-1			# Dummy int no for btx_v86
/*
 * Invoke real mode interrupt/function call from protected mode.
 *
 * We place a trampoline on the user stack that will return to rret_tramp
 * which will reenter protected mode and then finally return to the user
 * client.
 *
 * Kernel frame %esi points to:		Real mode stack frame at MEM_ESPR:
 *
 * -0x00 user %ss			-0x04 kernel %esp (with full frame)
 * -0x04 user %esp			-0x08 btx_v86 pointer
 * -0x08 user %eflags			-0x0c flags (only used if interrupt)
 * -0x0c user %cs			-0x10 real mode CS:IP return trampoline
 * -0x10 user %eip			-0x12 real mode flags
 * -0x14 int no				-0x16 real mode CS:IP (target)
 * -0x18 %eax
 * -0x1c %ecx
 * -0x20 %edx
 * -0x24 %ebx
 * -0x28 %esp
 * -0x2c %ebp
 * -0x30 %esi
 * -0x34 %edi
 * -0x38 %gs
 * -0x3c %fs
 * -0x40 %ds
 * -0x44 %es
 * -0x48 zero %eax (hardware int only)	
 * -0x4c zero %ecx (hardware int only)
 * -0x50 zero %edx (hardware int only)
 * -0x54 zero %ebx (hardware int only)
 * -0x58 zero %esp (hardware int only)
 * -0x5c zero %ebp (hardware int only)
 * -0x60 zero %esi (hardware int only)
 * -0x64 zero %edi (hardware int only)
 * -0x68 zero %gs (hardware int only)
 * -0x6c zero %fs (hardware int only)
 * -0x70 zero %ds (hardware int only)
 * -0x74 zero %es (hardware int only)
 */
int_hw: 	cld				# String ops inc
		pusha				# Save gp regs
		pushl %gs			# Save
		pushl %fs			#  seg
		pushl %ds			#  regs
		pushl %es
		push $SEL_SDATA			# Set up
		popl %ds			#  to
		pushl %ds			#  address
		popl %es			#  data
		leal 0x44(%esp,1),%esi		# Base of frame
		movl %esp,MEM_ESPR-0x04		# Save kernel stack pointer
		movl -0x14(%esi),%eax		# Get Int no
		cmpl $-1,%eax			# Hardware interrupt?
		jne intusr.1			# Yes
/*
 * v86 calls save the btx_v86 pointer on the real mode stack and read
 * the address and flags from the btx_v86 structure.  For interrupt
 * handler invocations (VM86 INTx requests), disable interrupts,
 * tracing, and alignment checking while the handler runs.
 */
		movl $MEM_USR,%ebx		# User base
		movl %ebx,%edx			#  address
		addl -0x4(%esi),%ebx		# User ESP
		movl (%ebx),%ebp		# btx_v86 pointer
		addl %ebp,%edx			# Flatten btx_v86 ptr
		movl %edx,MEM_ESPR-0x08		# Save btx_v86 ptr
		movl V86_ADDR(%edx),%eax	# Get int no/address
		movl V86_CTL(%edx),%edx		# Get control flags
		movl -0x08(%esi),%ebx		# Save user flags in %ebx
		testl $V86F_ADDR,%edx		# Segment:offset?
		jnz intusr.4			# Yes
		andl $~(PSL_I|PSL_T|PSL_AC),%ebx # Disable interrupts, tracing,
						#  and alignment checking for
						#  interrupt handler
		jmp intusr.3			# Skip hardware interrupt
/*
 * Hardware interrupts store a NULL btx_v86 pointer and use the
 * address (interrupt number) from the stack with empty flags.  Also,
 * push a dummy frame of zeros onto the stack for all the general
 * purpose and segment registers and clear %eflags.  This gives the
 * hardware interrupt handler a clean slate.
 */
intusr.1:	xorl %edx,%edx			# Control flags
		movl %edx,MEM_ESPR-0x08		# NULL btx_v86 ptr
		movl $12,%ecx			# Frame is 12 dwords
intusr.2:	pushl $0x0			# Fill frame
		loop intusr.2			#  with zeros
		movl $PSL_RESERVED_DEFAULT,%ebx # Set clean %eflags
/*
 * Look up real mode IDT entry for hardware interrupts and VM86 INTx
 * requests.
 */
intusr.3:	shll $0x2,%eax			# Scale
		movl (%eax),%eax		# Load int vector
		jmp intusr.5			# Skip CALLF test
/*
 * Panic if V86F_CALLF isn't set with V86F_ADDR.
 */
intusr.4:	testl $V86F_CALLF,%edx		# Far call?
		jnz intusr.5			# Ok
		movl %edx,0x30(%esp,1)		# Place VM86 flags in int no
		movl $badvm86,%esi		# Display bad
		call putstr			#  VM86 call
		popl %es			# Restore
		popl %ds			#  seg
		popl %fs			#  regs
		popl %gs
		popal				# Restore gp regs
		jmp ex_noc			# Panic
/*
 * %eax now holds the segment:offset of the function.
 * %ebx now holds the %eflags to pass to real mode.
 * %edx now holds the V86F_* flags.
 */
intusr.5:	movw %bx,MEM_ESPR-0x12		# Pass user flags to real mode
						#  target
/*
 * If this is a v86 call, copy the seg regs out of the btx_v86 structure.
 */
		movl MEM_ESPR-0x08,%ecx		# Get btx_v86 ptr
		jecxz intusr.6			# Skip for hardware ints
		leal -0x44(%esi),%edi		# %edi => kernel stack seg regs
		pushl %esi			# Save
		leal V86_ES(%ecx),%esi		# %esi => btx_v86 seg regs
		movl $4,%ecx			# Copy seg regs
		rep				#  from btx_v86
		movsl				#  to kernel stack
		popl %esi			# Restore
intusr.6:	movl -0x08(%esi),%ebx		# Copy user flags to real
		movl %ebx,MEM_ESPR-0x0c		#  mode return trampoline
		movl $rret_tramp,%ebx		# Set return trampoline
		movl %ebx,MEM_ESPR-0x10		#  CS:IP
		movl %eax,MEM_ESPR-0x16		# Real mode target CS:IP
		ljmpw $SEL_RCODE,$intusr.7	# Change to 16-bit segment
		.code16
intusr.7:	movl %cr0,%eax			# Leave
		dec %al				#  protected
		movl %eax,%cr0			#  mode
		ljmpw $0x0,$intusr.8
intusr.8:	xorw %ax,%ax			# Reset %ds
		movw %ax,%ds			#  and
		movw %ax,%ss			#  %ss
		lidt ivtdesc	 		# Set IVT
		popl %es			# Restore
		popl %ds			#  seg
		popl %fs			#  regs
		popl %gs
		popal				# Restore gp regs
		movw $MEM_ESPR-0x16,%sp		# Switch to real mode stack
		iret				# Call target routine
/*
 * For the return to real mode we setup a stack frame like this on the real
 * mode stack.  Note that callf calls won't pop off the flags, but we just
 * ignore that by repositioning %sp to be just above the btx_v86 pointer
 * so it is aligned.  The stack is relative to MEM_ESPR.
 *
 * -0x04	kernel %esp
 * -0x08	btx_v86
 * -0x0c	%eax
 * -0x10	%ecx
 * -0x14	%edx
 * -0x18	%ebx
 * -0x1c	%esp
 * -0x20	%ebp
 * -0x24	%esi
 * -0x28	%edi
 * -0x2c	%gs
 * -0x30	%fs
 * -0x34	%ds
 * -0x38	%es
 * -0x3c	%eflags
 */
rret_tramp:	movw $MEM_ESPR-0x08,%sp		# Reset stack pointer
		pushal				# Save gp regs
		pushl %gs			# Save
		pushl %fs			#  seg
		pushl %ds			#  regs
		pushl %es
		pushfl				# Save %eflags
		pushl $PSL_RESERVED_DEFAULT|PSL_D # Use clean %eflags with
		popfl				#  string ops dec
		xorw %ax,%ax			# Reset seg 
		movw %ax,%ds			#  regs
		movw %ax,%es			#  (%ss is already 0)
		lidt idtdesc	 		# Set IDT
		lgdt gdtdesc	 		# Set GDT
		mov %cr0,%eax			# Switch to protected
		inc %ax				#  mode
		mov %eax,%cr0			#
		ljmp $SEL_SCODE,$rret_tramp.1	# To 32-bit code
		.code32
rret_tramp.1:	xorl %ecx,%ecx			# Zero
		movb $SEL_SDATA,%cl		# Setup
		movw %cx,%ss			#  32-bit
		movw %cx,%ds			#  seg
		movw %cx,%es			#  regs
		movl MEM_ESPR-0x04,%esp		# Switch to kernel stack
		leal 0x44(%esp,1),%esi		# Base of frame
		andb $~0x2,tss_desc+0x5		# Clear TSS busy
		movb $SEL_TSS,%cl		# Set task
		ltr %cx				#  register
/*
 * Now we are back in protected mode.  The kernel stack frame set up
 * before entering real mode is still intact. For hardware interrupts,
 * leave the frame unchanged.
 */
		cmpl $0,MEM_ESPR-0x08		# Leave saved regs unchanged
		jz rret_tramp.3			#  for hardware ints
/*
 * For V86 calls, copy the registers off of the real mode stack onto
 * the kernel stack as we want their updated values.  Also, initialize
 * the segment registers on the kernel stack.
 *
 * Note that the %esp in the kernel stack after this is garbage, but popa
 * ignores it, so we don't have to fix it up.
 */
		leal -0x18(%esi),%edi		# Kernel stack GP regs
		pushl %esi			# Save
		movl $MEM_ESPR-0x0c,%esi	# Real mode stack GP regs
		movl $8,%ecx			# Copy GP regs from
		rep				#  real mode stack
		movsl				#  to kernel stack
		movl $SEL_UDATA,%eax		# Selector for data seg regs
		movl $4,%ecx			# Initialize %ds,
		rep				#  %es, %fs, and
		stosl				#  %gs
/*
 * For V86 calls, copy the saved seg regs on the real mode stack back
 * over to the btx_v86 structure.  Also, conditionally update the
 * saved eflags on the kernel stack based on the flags from the user.
 */
		movl MEM_ESPR-0x08,%ecx		# Get btx_v86 ptr
		leal V86_GS(%ecx),%edi		# %edi => btx_v86 seg regs
		leal MEM_ESPR-0x2c,%esi		# %esi => real mode seg regs
		xchgl %ecx,%edx			# Save btx_v86 ptr
		movl $4,%ecx			# Copy seg regs
		rep				#  from real mode stack
		movsl				#  to btx_v86
		popl %esi			# Restore
		movl V86_CTL(%edx),%edx		# Read V86 control flags
		testl $V86F_FLAGS,%edx		# User wants flags?
		jz rret_tramp.3			# No
		movl MEM_ESPR-0x3c,%eax		# Read real mode flags
		andl $~(PSL_T|PSL_NT),%eax	# Clear unsafe flags
		movw %ax,-0x08(%esi)		# Update user flags (low 16)
/*
 * Return to the user task
 */
rret_tramp.3:	popl %es			# Restore
		popl %ds			#  seg
		popl %fs			#  regs
		popl %gs
		popal				# Restore gp regs
		addl $4,%esp			# Discard int no
		iret				# Return to user mode

/*
 * System Call.
 */
intx30: 	cmpl $SYS_EXEC,%eax		# Exec system call?
		jne intx30.1			# No
		pushl %ss			# Set up
		popl %es			#  all
		pushl %es			#  segment
		popl %ds			#  registers
		pushl %ds			#  for the
		popl %fs			#  program
		pushl %fs			#  we're
		popl %gs			#  invoking
		movl $MEM_USR,%eax		# User base address
		addl 0xc(%esp,1),%eax		# Change to user
		leal 0x4(%eax),%esp		#  stack
		popl %eax			# Call
		call *%eax			#  program
intx30.1:	orb $0x1,%ss:btx_hdr+0x7	# Flag reboot
		jmp exit			# Exit
/*
 * Dump structure [EBX] to [EDI], using format string [ESI].
 */
dump.0: 	stosb				# Save char
dump:		lodsb				# Load char
		testb %al,%al			# End of string?
		jz dump.10			# Yes
		testb $0x80,%al 		# Control?
		jz dump.0			# No
		movb %al,%ch			# Save control
		movb $'=',%al			# Append
		stosb				#  '='
		lodsb				# Get offset
		pushl %esi			# Save
		movsbl %al,%esi 		# To
		addl %ebx,%esi			#  pointer
		testb $DMP_X16,%ch		# Dump word?
		jz dump.1			# No
		lodsw				# Get and
		call hex16			#  dump it
dump.1: 	testb $DMP_X32,%ch		# Dump long?
		jz dump.2			# No
		lodsl				# Get and
		call hex32			#  dump it
dump.2: 	testb $DMP_MEM,%ch		# Dump memory?
		jz dump.8			# No
		pushl %ds			# Save
		testl $PSL_VM,0x50(%ebx)	# V86 mode?
		jnz dump.3			# Yes
		verr 0x4(%esi)	 		# Readable selector?
		jnz dump.3			# No
		ldsl (%esi),%esi		# Load pointer
		jmp dump.4			# Join common code
dump.3: 	lodsl				# Set offset
		xchgl %eax,%edx 		# Save
		lodsl				# Get segment
		shll $0x4,%eax			#  * 0x10
		addl %edx,%eax			#  + offset
		xchgl %eax,%esi 		# Set pointer
dump.4: 	movb $2,%dl			# Num lines
dump.4a:	movb $0x10,%cl			# Bytes to dump
dump.5: 	lodsb				# Get byte and
		call hex8			#  dump it
		decb %cl			# Keep count
		jz dump.6a			# If done
		movb $'-',%al			# Separator
		cmpb $0x8,%cl			# Half way?
		je dump.6			# Yes
		movb $' ',%al			# Use space
dump.6: 	stosb				# Save separator
		jmp dump.5			# Continue
dump.6a:	decb %dl			# Keep count
		jz dump.7			# If done
		movb $0xa,%al			# Line feed
		stosb				# Save one
		movb $7,%cl			# Leading
		movb $' ',%al			#  spaces
dump.6b:	stosb				# Dump
		decb %cl			#  spaces
		jnz dump.6b
		jmp dump.4a			# Next line
dump.7: 	popl %ds			# Restore
dump.8: 	popl %esi			# Restore
		movb $0xa,%al			# Line feed
		testb $DMP_EOL,%ch		# End of line?
		jnz dump.9			# Yes
		movb $' ',%al			# Use spaces
		stosb				# Save one
dump.9: 	jmp dump.0			# Continue
dump.10:	stosb				# Terminate string
		ret				# To caller
/*
 * Convert EAX, AX, or AL to hex, saving the result to [EDI].
 */
hex32:		pushl %eax			# Save
		shrl $0x10,%eax 		# Do upper
		call hex16			#  16
		popl %eax			# Restore
hex16:		call hex16.1			# Do upper 8
hex16.1:	xchgb %ah,%al			# Save/restore
hex8:		pushl %eax			# Save
		shrb $0x4,%al			# Do upper
		call hex8.1			#  4
		popl %eax			# Restore
hex8.1: 	andb $0xf,%al			# Get lower 4
		cmpb $0xa,%al			# Convert
		sbbb $0x69,%al			#  to hex
		das				#  digit
		orb $0x20,%al			# To lower case
		stosb				# Save char
		ret				# (Recursive)
/*
 * Output zero-terminated string [ESI] to the console.
 */
putstr.0:	call putchr			# Output char
putstr: 	lodsb				# Load char
		testb %al,%al			# End of string?
		jnz putstr.0			# No
		ret				# To caller
#ifdef BTX_SERIAL
		.set SIO_PRT,SIOPRT		# Base port
		.set SIO_FMT,SIOFMT		# 8N1
		.set SIO_DIV,(115200/SIOSPD)	# 115200 / SPD

/*
 * int sio_init(void)
 */
sio_init:	movw $SIO_PRT+0x3,%dx		# Data format reg
		movb $SIO_FMT|0x80,%al		# Set format
		outb %al,(%dx)			#  and DLAB
		pushl %edx			# Save
		subb $0x3,%dl			# Divisor latch reg
		movw $SIO_DIV,%ax		# Set
		outw %ax,(%dx)			#  BPS
		popl %edx			# Restore
		movb $SIO_FMT,%al		# Clear
		outb %al,(%dx)			#  DLAB
		incl %edx			# Modem control reg
		movb $0x3,%al			# Set RTS,
		outb %al,(%dx)			#  DTR
		incl %edx			# Line status reg
		call sio_getc.1 		# Get character

/*
 * int sio_flush(void)
 */
sio_flush:	xorl %eax,%eax			# Return value
		xorl %ecx,%ecx			# Timeout
		movb $0x80,%ch			#  counter
sio_flush.1:	call sio_ischar 		# Check for character
		jz sio_flush.2			# Till none
		loop sio_flush.1		#  or counter is zero
		movb $1, %al			# Exhausted all tries
sio_flush.2:	ret				# To caller

/*
 * void sio_putc(int c)
 */
sio_putc:	movw $SIO_PRT+0x5,%dx		# Line status reg
		xor %ecx,%ecx			# Timeout
		movb $0x40,%ch			#  counter
sio_putc.1:	inb (%dx),%al			# Transmitter
		testb $0x20,%al 		#  buffer empty?
		loopz sio_putc.1		# No
		jz sio_putc.2			# If timeout
		movb 0x4(%esp,1),%al		# Get character
		subb $0x5,%dl			# Transmitter hold reg
		outb %al,(%dx)			# Write character
sio_putc.2:	ret $0x4			# To caller

/*
 * int sio_getc(void)
 */
sio_getc:	call sio_ischar 		# Character available?
		jz sio_getc			# No
sio_getc.1:	subb $0x5,%dl			# Receiver buffer reg
		inb (%dx),%al			# Read character
		ret				# To caller

/*
 * int sio_ischar(void)
 */
sio_ischar:	movw $SIO_PRT+0x5,%dx		# Line status register
		xorl %eax,%eax			# Zero
		inb (%dx),%al			# Received data
		andb $0x1,%al			#  ready?
		ret				# To caller

/*
 * Output character AL to the serial console.
 */
putchr: 	pusha				# Save
		cmpb $10, %al			# is it a newline?
		jne putchr.1			#  no?, then leave
		push $13			# output a carriage
		call sio_putc			#  return first
		movb $10, %al			# restore %al
putchr.1:	pushl %eax			# Push the character
						#  onto the stack
		call sio_putc			# Output the character
		popa				# Restore
		ret				# To caller
#else
/*
 * Output character AL to the console.
 */
putchr: 	pusha				# Save
		xorl %ecx,%ecx			# Zero for loops
		movb $SCR_MAT,%ah		# Mode/attribute
		movl $BDA_POS,%ebx		# BDA pointer
		movw (%ebx),%dx 		# Cursor position
		movl $0xb8000,%edi		# Regen buffer (color)
		cmpb %ah,BDA_SCR-BDA_POS(%ebx)	# Mono mode?
		jne putchr.1			# No
		xorw %di,%di			# Regen buffer (mono)
putchr.1:	cmpb $0xa,%al			# New line?
		je putchr.2			# Yes
		xchgl %eax,%ecx 		# Save char
		movb $SCR_COL,%al		# Columns per row
		mulb %dh			#  * row position
		addb %dl,%al			#  + column
		adcb $0x0,%ah			#  position
		shll %eax			#  * 2
		xchgl %eax,%ecx 		# Swap char, offset
		movw %ax,(%edi,%ecx,1)		# Write attr:char
		incl %edx			# Bump cursor
		cmpb $SCR_COL,%dl		# Beyond row?
		jb putchr.3			# No
putchr.2:	xorb %dl,%dl			# Zero column
		incb %dh			# Bump row
putchr.3:	cmpb $SCR_ROW,%dh		# Beyond screen?
		jb putchr.4			# No
		leal 2*SCR_COL(%edi),%esi	# New top line
		movw $(SCR_ROW-1)*SCR_COL/2,%cx # Words to move
		rep				# Scroll
		movsl				#  screen
		movb $0x20,%al			# Space
		movb $SCR_COL,%cl		# Columns to clear
		rep				# Clear
		stosw				#  line
		movb $SCR_ROW-1,%dh		# Bottom line
putchr.4:	movw %dx,(%ebx) 		# Update position
		popa				# Restore
		ret				# To caller
#endif

		.code16
/*
 * Real Mode Hardware interrupt jump table.
 */
intr20: 	push $0x8			# Int 0x20: IRQ0
		jmp int_hwr			# V86 int 0x8
		push $0x9			# Int 0x21: IRQ1
		jmp int_hwr			# V86 int 0x9
		push $0xa			# Int 0x22: IRQ2
		jmp int_hwr			# V86 int 0xa
		push $0xb			# Int 0x23: IRQ3
		jmp int_hwr			# V86 int 0xb
		push $0xc			# Int 0x24: IRQ4
		jmp int_hwr			# V86 int 0xc
		push $0xd			# Int 0x25: IRQ5
		jmp int_hwr			# V86 int 0xd
		push $0xe			# Int 0x26: IRQ6
		jmp int_hwr			# V86 int 0xe
		push $0xf			# Int 0x27: IRQ7
		jmp int_hwr			# V86 int 0xf
		push $0x70			# Int 0x28: IRQ8
		jmp int_hwr			# V86 int 0x70
		push $0x71			# Int 0x29: IRQ9
		jmp int_hwr			# V86 int 0x71
		push $0x72			# Int 0x2a: IRQ10
		jmp int_hwr			# V86 int 0x72
		push $0x73			# Int 0x2b: IRQ11
		jmp int_hwr			# V86 int 0x73
		push $0x74			# Int 0x2c: IRQ12
		jmp int_hwr			# V86 int 0x74
		push $0x75			# Int 0x2d: IRQ13
		jmp int_hwr			# V86 int 0x75
		push $0x76			# Int 0x2e: IRQ14
		jmp int_hwr			# V86 int 0x76
		push $0x77			# Int 0x2f: IRQ15
		jmp int_hwr			# V86 int 0x77
/*
 * Reflect hardware interrupts in real mode.
 */
int_hwr: 	push %ax			# Save
		push %ds			# Save
		push %bp			# Save
		mov %sp,%bp			# Address stack frame 
		xchg %bx,6(%bp)			# Swap BX, int no
		xor %ax,%ax			# Set %ds:%bx to
		shl $2,%bx			#  point to
		mov %ax,%ds			#  IDT entry
		mov (%bx),%ax			# Load IP
		mov 2(%bx),%bx			# Load CS
		xchg %ax,4(%bp)			# Swap saved %ax,%bx with
		xchg %bx,6(%bp)			#  CS:IP of handler
		pop %bp				# Restore
		pop %ds				# Restore
		lret				# Jump to handler

		.p2align 4
/*
 * Global descriptor table.
 */
gdt:		.word 0x0,0x0,0x0,0x0		# Null entry
		.word 0xffff,0x0,0x9a00,0xcf	# SEL_SCODE
		.word 0xffff,0x0,0x9200,0xcf	# SEL_SDATA
		.word 0xffff,0x0,0x9a00,0x0	# SEL_RCODE
		.word 0xffff,0x0,0x9200,0x0	# SEL_RDATA
		.word 0xffff,MEM_USR,0xfa00,0xcf# SEL_UCODE
		.word 0xffff,MEM_USR,0xf200,0xcf# SEL_UDATA
tss_desc:	.word _TSSLM,MEM_TSS,0x8900,0x0 # SEL_TSS
gdt.1:
/*
 * Pseudo-descriptors.
 */
gdtdesc:	.word gdt.1-gdt-1,gdt,0x0	# GDT
idtdesc:	.word _IDTLM,MEM_IDT,0x0	# IDT
ivtdesc:	.word 0x400-0x0-1,0x0,0x0	# IVT
/*
 * IDT construction control string.
 */
idtctl: 	.byte 0x10,  0x8e		# Int 0x0-0xf
		.word 0x7dfb,intx00		#  (exceptions)
		.byte 0x10,  0x8e		# Int 0x10
		.word 0x1,   intx10		#  (exception)
		.byte 0x10,  0x8e		# Int 0x20-0x2f
		.word 0xffff,intx20		#  (hardware)
		.byte 0x1,   0xee		# int 0x30
		.word 0x1,   intx30		#  (system call)
		.byte 0x2,   0xee		# Int 0x31-0x32
		.word 0x1,   intx31		#  (V86, null)
		.byte 0x0			# End of string
/*
 * Dump format string.
 */
dmpfmt: 	.byte '\n'			# "\n"
		.ascii "int"			# "int="
		.byte 0x80|DMP_X32,	   0x40 # "00000000  "
		.ascii "err"			# "err="
		.byte 0x80|DMP_X32,	   0x44 # "00000000  "
		.ascii "efl"			# "efl="
		.byte 0x80|DMP_X32,	   0x50 # "00000000  "
		.ascii "eip"			# "eip="
		.byte 0x80|DMP_X32|DMP_EOL,0x48 # "00000000\n"
		.ascii "eax"			# "eax="
		.byte 0x80|DMP_X32,	   0x34 # "00000000  "
		.ascii "ebx"			# "ebx="
		.byte 0x80|DMP_X32,	   0x28 # "00000000  "
		.ascii "ecx"			# "ecx="
		.byte 0x80|DMP_X32,	   0x30 # "00000000  "
		.ascii "edx"			# "edx="
		.byte 0x80|DMP_X32|DMP_EOL,0x2c # "00000000\n"
		.ascii "esi"			# "esi="
		.byte 0x80|DMP_X32,	   0x1c # "00000000  "
		.ascii "edi"			# "edi="
		.byte 0x80|DMP_X32,	   0x18 # "00000000  "
		.ascii "ebp"			# "ebp="
		.byte 0x80|DMP_X32,	   0x20 # "00000000  "
		.ascii "esp"			# "esp="
		.byte 0x80|DMP_X32|DMP_EOL,0x0	# "00000000\n"
		.ascii "cs"			# "cs="
		.byte 0x80|DMP_X16,	   0x4c # "0000  "
		.ascii "ds"			# "ds="
		.byte 0x80|DMP_X16,	   0xc	# "0000  "
		.ascii "es"			# "es="
		.byte 0x80|DMP_X16,	   0x8	# "0000  "
		.ascii "  "			# "  "
		.ascii "fs"			# "fs="
		.byte 0x80|DMP_X16,	   0x10 # "0000  "
		.ascii "gs"			# "gs="
		.byte 0x80|DMP_X16,	   0x14 # "0000  "
		.ascii "ss"			# "ss="
		.byte 0x80|DMP_X16|DMP_EOL,0x4	# "0000\n"
		.ascii "cs:eip" 		# "cs:eip="
		.byte 0x80|DMP_MEM|DMP_EOL,0x48 # "00 00 ... 00 00\n"
		.ascii "ss:esp" 		# "ss:esp="
		.byte 0x80|DMP_MEM|DMP_EOL,0x0	# "00 00 ... 00 00\n"
		.asciz "BTX halted\n"		# End
/*
 * Bad VM86 call panic
 */
badvm86:	.asciz "Invalid VM86 Request\n"

/*
 * End of BTX memory.
 */
		.p2align 4
break:
