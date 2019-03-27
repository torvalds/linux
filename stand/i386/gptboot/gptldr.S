/*-
 * Copyright (c) 2007 Yahoo!, Inc.
 * All rights reserved.
 * Written by: John Baldwin <jhb@FreeBSD.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 * Partly from: src/sys/boot/i386/boot2/boot1.S 1.31
 */

/* Memory Locations */
		.set MEM_REL,0x700		# Relocation address
		.set MEM_ARG,0x900		# Arguments
		.set MEM_ORG,0x7c00		# Origin
		.set MEM_BUF,0x8cec		# Load area
		.set MEM_BTX,0x9000		# BTX start
		.set MEM_JMP,0x9010		# BTX entry point
		.set MEM_USR,0xa000		# Client start
		.set BDA_BOOT,0x472		# Boot howto flag
	
/* Misc. Constants */
		.set SIZ_PAG,0x1000		# Page size
		.set SIZ_SEC,0x200		# Sector size
		.set COPY_BLKS,0x8		# Number of blocks
						# to copy for boot2 (<= 15)
		.set COPY_BLK_SZ,0x8000		# Copy in 32k blocks; must be
						# a multiple of 16 bytes

		.globl start
		.code16

/*
 * Copy BTX and boot2 to the right locations and start it all up.
 */

/*
 * Setup the segment registers to flat addressing (segment 0) and setup the
 * stack to end just below the start of our code.
 */
start:		xor %cx,%cx			# Zero
		mov %cx,%es			# Address
		mov %cx,%ds			#  data
		mov %cx,%ss			# Set up
		mov $start,%sp			#  stack

/*
 * BTX is right after us at 'end'.  We read the length of BTX out of
 * its header to find boot2.  We need to copy boot2 to MEM_USR and BTX
 * to MEM_BTX.  Since those might overlap, we have to copy boot2
 * backwards first and then copy BTX.  We aren't sure exactly how long
 * boot2 is, but it's currently under 256kB so we'll copy 8 blocks of 32kB
 * each; this can be adjusted via COPY_BLK and COPY_BLK_SZ above.
 */
		mov $end,%bx			# BTX
		mov 0xa(%bx),%si		# Get BTX length and set
		add %bx,%si			#  %si to start of boot2
		dec %si				# Set %ds:%si to point at the
		mov %si,%ax			# last byte we want to copy
		shr $4,%ax			# from boot2, with %si made as
		add $(COPY_BLKS*COPY_BLK_SZ/16),%ax	# small as possible.
		and $0xf,%si			# 
		mov %ax,%ds			#
		mov $MEM_USR/16,%ax		# Set %es:(-1) to point at
		add $(COPY_BLKS*COPY_BLK_SZ/16),%ax	# the last byte we
		mov %ax,%es			# want to copy boot2 into.
		mov $COPY_BLKS,%bx		# Copy COPY_BLKS 32k blocks
copyloop:
		add $COPY_BLK_SZ,%si		# Adjust %ds:%si to point at
		mov %ds,%ax			# the end of the next 32k to
		sub $COPY_BLK_SZ/16,%ax		# copy from boot2
		mov %ax,%ds
		mov $COPY_BLK_SZ-1,%di		# Adjust %es:%di to point at
		mov %es,%ax			# the end of the next 32k into
		sub $COPY_BLK_SZ/16,%ax		# which we want boot2 copied
		mov %ax,%es
		mov $COPY_BLK_SZ,%cx		# Copy 32k
		std
		rep movsb
		dec %bx
		jnz copyloop
		mov %cx,%ds			# Reset %ds and %es
		mov %cx,%es
		mov $end,%bx			# BTX
		mov 0xa(%bx),%cx		# Get BTX length and set
		mov %bx,%si			#  %si to end of BTX
		mov $MEM_BTX,%di		# %di -> end of BTX at
		add %cx,%si			#  MEM_BTX
		add %cx,%di
		dec %si
		dec %di
		rep movsb			# Move BTX
		cld				# String ops inc
/*
 * Enable A20 so we can access memory above 1 meg.
 * Use the zero-valued %cx as a timeout for embedded hardware which do not
 * have a keyboard controller.
 */
seta20: 	cli				# Disable interrupts
seta20.1:	dec %cx				# Timeout?
		jz seta20.3			# Yes
		inb $0x64,%al			# Get status
		testb $0x2,%al			# Busy?
		jnz seta20.1			# Yes
		movb $0xd1,%al			# Command: Write
		outb %al,$0x64			#  output port
seta20.2:	inb $0x64,%al			# Get status
		testb $0x2,%al			# Busy?
		jnz seta20.2			# Yes
		movb $0xdf,%al			# Enable
		outb %al,$0x60			#  A20
seta20.3:	sti				# Enable interrupts

/*
 * Save drive number from BIOS so boot2 can see it and start BTX.
 */
		movb %dl,MEM_ARG
		jmp MEM_JMP			# Start BTX
end:
