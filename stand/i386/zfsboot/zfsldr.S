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

/* Memory Locations */
		.set MEM_ARG,0x900		# Arguments
		.set MEM_ORG,0x7c00		# Origin
		.set MEM_BUF,0x8000		# Load area
		.set MEM_BTX,0x9000		# BTX start
		.set MEM_JMP,0x9010		# BTX entry point
		.set MEM_USR,0xa000		# Client start
		.set BDA_BOOT,0x472		# Boot howto flag
	
/* Partition Constants */
		.set PRT_OFF,0x1be		# Partition offset
		.set PRT_NUM,0x4		# Partitions
		.set PRT_BSD,0xa5		# Partition type

/* Misc. Constants */
		.set SIZ_PAG,0x1000		# Page size
		.set SIZ_SEC,0x200		# Sector size
		.set COPY_BLKS,0x8		# Number of blocks
						# to copy for boot2 (<= 15)
		.set COPY_BLK_SZ,0x8000		# Copy in 32k blocks; must be
						# a multiple of 16 bytes
		.set NSECT,(COPY_BLK_SZ / SIZ_SEC * COPY_BLKS)
		.globl start
		.code16

/*
 * Load the rest of zfsboot2 and BTX up, copy the parts to the right locations,
 * and start it all up.
 */

/*
 * Setup the segment registers to flat addressing (segment 0) and setup the
 * stack to end just below the start of our code.
 */
start:		cld				# String ops inc
		xor %cx,%cx			# Zero
		mov %cx,%es			# Address
		mov %cx,%ds			#  data
		mov %cx,%ss			# Set up
		mov $start,%sp			#  stack
/*
 * Load the MBR and look for the first FreeBSD slice.  We use the fake
 * partition entry below that points to the MBR when we call read.
 * The first pass looks for the first active FreeBSD slice.  The
 * second pass looks for the first non-active FreeBSD slice if the
 * first one fails.
 */
		call check_edd		 	# Make sure EDD works
		mov $part4,%si			# Dummy partition
		xor %eax,%eax			# Read MBR
		movl $MEM_BUF,%ebx		#  from first
		call read			#  sector
		mov $0x1,%cx	 		# Two passes
main.1: 	mov $MEM_BUF+PRT_OFF,%si	# Partition table
		movb $0x1,%dh			# Partition
main.2: 	cmpb $PRT_BSD,0x4(%si)		# Our partition type?
		jne main.3			# No
		jcxz main.5			# If second pass
		testb $0x80,(%si)		# Active?
		jnz main.5			# Yes
main.3: 	add $0x10,%si	 		# Next entry
		incb %dh			# Partition
		cmpb $0x1+PRT_NUM,%dh		# In table?
		jb main.2			# Yes
		dec %cx				# Do two
		jcxz main.1			#  passes
/*
 * If we get here, we didn't find any FreeBSD slices at all, so print an
 * error message and die.
 */
		mov $msg_part,%si		# Message
		jmp error			# Error

/*
 * Ok, we have a slice and drive in %dx now, so use that to locate and
 * load boot2.  %si references the start of the slice we are looking
 * for, so go ahead and load up the COPY_BLKS*COPY_BLK_SZ/SIZ_SEC sectors
 * starting at sector 1024 (i.e. after the two vdev labels).  We don't
 * have do anything fancy here to allow for an extra copy of boot1 and
 * a partition table (compare to this section of the UFS bootstrap) so we
 * just load it all at 0x9000. The first part of boot2 is BTX, which wants
 * to run at 0x9000. The boot2.bin binary starts right after the end of BTX,
 * so we have to figure out where the start of it is and then move the
 * binary to 0xc000.  Normally, BTX clients start at MEM_USR, or 0xa000,
 * but when we use btxld to create zfsboot2, we use an entry point of
 * 0x2000.  That entry point is relative to MEM_USR; thus boot2.bin
 * starts at 0xc000.
 *
 * The load area and the target area for the client overlap so we have
 * to use a decrementing string move. We also play segment register
 * games with the destination address for the move so that the client
 * can be larger than 16k (which would overflow the zero segment since
 * the client starts at 0xc000).
 */
main.5: 	mov %dx,MEM_ARG			# Save args
		mov $NSECT,%cx			# Sector count
		movl $1024,%eax			# Offset to boot2
		mov $MEM_BTX,%ebx		# Destination buffer
main.6:		pushal				# Save params
		call read			# Read disk
		popal				# Restore
		incl %eax			# Advance to
		add $SIZ_SEC,%ebx		#  next sector
		loop main.6			# If not last, read another

		mov $MEM_BTX,%bx		# BTX
		mov 0xa(%bx),%si		# Get BTX length and set
		add %bx,%si			#  %si to start of boot2
		dec %si				# Set %ds:%si to point at the
		mov %si,%ax			# last byte we want to copy
		shr $4,%ax			# from boot2, with %si made as
		add $(COPY_BLKS*COPY_BLK_SZ/16),%ax	# small as possible.
		and $0xf,%si			#
		mov %ax,%ds			#
		mov $(MEM_USR+2*SIZ_PAG)/16,%ax # Set %es:(-1) to point at
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
		cld				# Back to increment

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

		jmp start+MEM_JMP-MEM_ORG	# Start BTX


/*
 * Read a sector from the disk.  Sets up an EDD packet on the stack
 * and passes it to read.  We assume that the destination address is
 * always segment-aligned.
 *
 * %eax		- int     - LBA to read in relative to partition start
 * %ebx		- ptr	  - destination address
 * %dl		- byte    - drive to read from
 * %si		- ptr     - MBR partition entry
 */
read:		xor %ecx,%ecx			# Get
		addl 0x8(%si),%eax		#  LBA
		adc $0,%ecx
		pushl %ecx			# Starting absolute block
		pushl %eax			#  block number
		shr $4,%ebx			# Convert to segment
		push %bx			# Address of
		push $0				#  transfer buffer
		push $0x1			# Read 1 sector
		push $0x10			# Size of packet
		mov %sp,%si			# Packet pointer
		mov $0x42,%ah			# BIOS: Extended
		int $0x13			#  read
		jc read.1			# If error, fail
		lea 0x10(%si),%sp		# Clear stack
		ret				# If success, return
read.1:		mov %ah,%al			# Format
		mov $read_err,%di		#  error
		call hex8			#  code
		mov $msg_read,%si		# Set the error message and
						#  fall through to the error
						#  routine
/*
 * Print out the error message pointed to by %ds:(%si) followed
 * by a prompt, wait for a keypress, and then reboot the machine.
 */
error:		callw putstr			# Display message
		mov $prompt,%si			# Display
		callw putstr			#  prompt
		xorb %ah,%ah			# BIOS: Get
		int $0x16			#  keypress
		movw $0x1234, BDA_BOOT		# Do a warm boot
		ljmp $0xffff,$0x0		# reboot the machine
/*
 * Display a null-terminated string using the BIOS output.
 */
putstr.0:	mov $0x7,%bx	 		# Page:attribute
		movb $0xe,%ah			# BIOS: Display
		int $0x10			#  character
putstr: 	lodsb				# Get char
		testb %al,%al			# End of string?
		jne putstr.0			# No
		ret				# To caller
/*
 * Check to see if the disk supports EDD.  zfsboot requires EDD and does not
 * support older C/H/S disk I/O.
 */
check_edd:	cmpb $0x80,%dl			# Hard drive?
		jb check_edd.1 			# No, fail to boot
		mov $0x55aa,%bx			# Magic
		push %dx			# Save
		movb $0x41,%ah			# BIOS: Check
		int $0x13			#  extensions present
		pop %dx				# Restore
		jc check_edd.1			# If error, fail
		cmp $0xaa55,%bx			# Magic?
		jne check_edd.1			# No, so fail
		testb $0x1,%cl			# Packet interface?
		jz check_edd.1			# No, so fail
		ret				# EDD ok, keep booting
check_edd.1:	mov $msg_chs,%si		# Warn that CHS is
		jmp error			#  unsupported and fail
/*
 * AL to hex, saving the result to [EDI].
 */
hex8:		push %ax			# Save
		shrb $0x4,%al			# Do upper
		call hex8.1			#  4
		pop %ax				# Restore
hex8.1: 	andb $0xf,%al			# Get lower 4
		cmpb $0xa,%al			# Convert
		sbbb $0x69,%al			#  to hex
		das				#  digit
		orb $0x20,%al			# To lower case
		stosb				# Save char
		ret				# (Recursive)

/* Messages */

msg_chs:	.asciz "CHS not supported"
msg_read:	.ascii "Read error: "
read_err:	.asciz "XX"
msg_part:	.asciz "Boot error"

prompt: 	.asciz "\r\n"

		.org PRT_OFF,0x90

/* Partition table */

		.fill 0x30,0x1,0x0
part4:		.byte 0x80, 0x00, 0x01, 0x00
		.byte 0xa5, 0xfe, 0xff, 0xff
		.byte 0x00, 0x00, 0x00, 0x00
		.byte 0x50, 0xc3, 0x00, 0x00	# 50000 sectors long, bleh

		.word 0xaa55			# Magic number
