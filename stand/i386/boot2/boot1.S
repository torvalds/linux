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
		.set MEM_REL,0x700		# Relocation address
		.set MEM_ARG,0x900		# Arguments
		.set MEM_ORG,0x7c00		# Origin
		.set MEM_BUF,0x8c00		# Load area
		.set MEM_BTX,0x9000		# BTX start
		.set MEM_JMP,0x9010		# BTX entry point
		.set MEM_USR,0xa000		# Client start
		.set BDA_BOOT,0x472		# Boot howto flag
	
/* Partition Constants */
		.set PRT_OFF,0x1be		# Partition offset
		.set PRT_NUM,0x4		# Partitions
		.set PRT_BSD,0xa5		# Partition type

/* Flag Bits */
		.set FL_PACKET,0x80		# Packet mode

/* Misc. Constants */
		.set SIZ_PAG,0x1000		# Page size
		.set SIZ_SEC,0x200		# Sector size

		.set NSECT,0x10
		.globl start
		.globl xread
		.code16

start:		jmp main			# Start recognizably

/*
 * This is the start of a standard BIOS Parameter Block (BPB). Most bootable
 * FAT disks have this at the start of their MBR. While normal BIOS's will
 * work fine without this section, IBM's El Torito emulation "fixes" up the
 * BPB by writing into the memory copy of the MBR. Rather than have data
 * written into our xread routine, we'll define a BPB to work around it.
 * The data marked with (T) indicates a field required for a ThinkPad to
 * recognize the disk and (W) indicates fields written from IBM BIOS code.
 * The use of the BPB is based on what OpenBSD and NetBSD implemented in
 * their boot code but the required fields were determined by trial and error.
 *
 * Note: If additional space is needed in boot1, one solution would be to
 * move the "prompt" message data (below) to replace the OEM ID.
 */
		.org 0x03, 0x00
oemid:		.space 0x08, 0x00	# OEM ID

		.org 0x0b, 0x00
bpb:		.word   512		# sector size (T)
		.byte	0		# sectors/clustor
		.word	0		# reserved sectors
		.byte	0		# number of FATs
		.word	0		# root entries
		.word	0		# small sectors
		.byte	0		# media type (W)
		.word	0		# sectors/fat
		.word	18		# sectors per track (T)
		.word	2		# number of heads (T)
		.long	0		# hidden sectors (W)
		.long	0		# large sectors

		.org 0x24, 0x00
ebpb:		.byte	0		# BIOS physical drive number (W)

		.org 0x25,0x90
/*
 * Trampoline used by boot2 to call read to read data from the disk via
 * the BIOS.  Call with:
 *
 * %cx:%ax	- long    - LBA to read in
 * %es:(%bx)	- caddr_t - buffer to read data into
 * %dl		- byte    - drive to read from
 * %dh		- byte    - num sectors to read
 */

xread:		push %ss			# Address
		pop %ds				#  data
/*
 * Setup an EDD disk packet and pass it to read
 */
xread.1:					# Starting
		pushl $0x0			#  absolute
		push %cx			#  block
		push %ax			#  number
		push %es			# Address of
		push %bx			#  transfer buffer
		xor %ax,%ax			# Number of
		movb %dh,%al			#  blocks to
		push %ax			#  transfer
		push $0x10			# Size of packet
		mov %sp,%bp			# Packet pointer
		callw read			# Read from disk
		lea 0x10(%bp),%sp		# Clear stack
		lret				# To far caller
/*
 * Load the rest of boot2 and BTX up, copy the parts to the right locations,
 * and start it all up.
 */

/*
 * Setup the segment registers to flat addressing (segment 0) and setup the
 * stack to end just below the start of our code.
 */
main:		cld				# String ops inc
		xor %cx,%cx			# Zero
		mov %cx,%es			# Address
		mov %cx,%ds			#  data
		mov %cx,%ss			# Set up
		mov $start,%sp			#  stack
/*
 * Relocate ourself to MEM_REL.  Since %cx == 0, the inc %ch sets
 * %cx == 0x100.  Note that boot1 does not use this relocated copy
 * of itself while loading boot2; however, BTX reclaims the memory
 * used by boot1 during its initialization.  As a result, boot2 uses
 * xread from the relocated copy.
 */
		mov %sp,%si			# Source
		mov $MEM_REL,%di		# Destination
		incb %ch			# Word count
		rep				# Copy
		movsw				#  code
/*
 * If we are on a hard drive, then load the MBR and look for the first
 * FreeBSD slice.  We use the fake partition entry below that points to
 * the MBR when we call nread.  The first pass looks for the first active
 * FreeBSD slice.  The second pass looks for the first non-active FreeBSD
 * slice if the first one fails.
 */
		mov $part4,%si			# Partition
		cmpb $0x80,%dl			# Hard drive?
		jb main.4			# No
		movb $0x1,%dh			# Block count
		callw nread			# Read MBR
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
 * Floppies use partition 0 of drive 0.
 */
main.4: 	xor %dx,%dx			# Partition:drive
/*
 * Ok, we have a slice and drive in %dx now, so use that to locate and load
 * boot2.  %si references the start of the slice we are looking for, so go
 * ahead and load up the first 16 sectors (boot1 + boot2) from that.  When
 * we read it in, we conveniently use 0x8c00 as our transfer buffer.  Thus,
 * boot1 ends up at 0x8c00, and boot2 starts at 0x8c00 + 0x200 = 0x8e00.
 * The first part of boot2 is the disklabel, which is 0x200 bytes long.
 * The second part is BTX, which is thus loaded into 0x9000, which is where
 * it also runs from.  The boot2.bin binary starts right after the end of
 * BTX, so we have to figure out where the start of it is and then move the
 * binary to 0xc000.  Normally, BTX clients start at MEM_USR, or 0xa000, but
 * when we use btxld to create boot2, we use an entry point of 0x2000.  That
 * entry point is relative to MEM_USR; thus boot2.bin starts at 0xc000.
 */
main.5: 	mov %dx,MEM_ARG			# Save args
		movb $NSECT,%dh			# Sector count
		callw nread			# Read disk
		mov $MEM_BTX,%bx		# BTX
		mov 0xa(%bx),%si		# Get BTX length and set
		add %bx,%si			#  %si to start of boot2.bin
		mov $MEM_USR+SIZ_PAG*2,%di	# Client page 2
		mov $MEM_BTX+(NSECT-1)*SIZ_SEC,%cx # Byte
		sub %si,%cx			#  count
		rep				# Relocate
		movsb				#  client

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
 * Trampoline used to call read from within boot1.
 */
nread:		mov $MEM_BUF,%bx		# Transfer buffer
		mov 0x8(%si),%ax		# Get
		mov 0xa(%si),%cx		#  LBA
		push %cs			# Read from
		callw xread.1	 		#  disk
		jnc return			# If success, return
		mov $msg_read,%si		# Otherwise, set the error
						#  message and fall through to
						#  the error routine
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
		ljmp $0xf000,$0xfff0		# reboot the machine
/*
 * Display a null-terminated string using the BIOS output.
 */
putstr.0:	mov $0x7,%bx	 		# Page:attribute
		movb $0xe,%ah			# BIOS: Display
		int $0x10			#  character
putstr: 	lodsb				# Get char
		testb %al,%al			# End of string?
		jne putstr.0			# No

/*
 * Overused return code.  ereturn is used to return an error from the
 * read function.  Since we assume putstr succeeds, we (ab)use the
 * same code when we return from putstr.
 */
ereturn:	movb $0x1,%ah			# Invalid
		stc				#  argument
return: 	retw				# To caller
/*
 * Reads sectors from the disk.  If EDD is enabled, then check if it is
 * installed and use it if it is.  If it is not installed or not enabled, then
 * fall back to using CHS.  Since we use a LBA, if we are using CHS, we have to
 * fetch the drive parameters from the BIOS and divide it out ourselves.
 * Call with:
 *
 * %dl	- byte     - drive number
 * stack - 10 bytes - EDD Packet
 */
read:		testb $FL_PACKET,%cs:MEM_REL+flags-start # LBA support enabled?
		jz read.1			# No, use CHS
		cmpb $0x80,%dl			# Hard drive?
		jb read.1			# No, use CHS
		mov $0x55aa,%bx			# Magic
		push %dx			# Save
		movb $0x41,%ah			# BIOS: Check
		int $0x13			#  extensions present
		pop %dx				# Restore
		jc read.1			# If error, use CHS
		cmp $0xaa55,%bx			# Magic?
		jne read.1			# No, so use CHS
		testb $0x1,%cl			# Packet interface?
		jz read.1			# No, so use CHS
		mov %bp,%si			# Disk packet
		movb $0x42,%ah			# BIOS: Extended
		int $0x13			#  read
		retw				# To caller
read.1:	 	push %dx			# Save
		movb $0x8,%ah			# BIOS: Get drive
		int $0x13			#  parameters
		movb %dh,%ch			# Max head number
		pop %dx				# Restore
		jc return			# If error
		andb $0x3f,%cl			# Sectors per track
		jz ereturn			# If zero
		cli				# Disable interrupts
		mov 0x8(%bp),%eax		# Get LBA
		push %dx			# Save
		movzbl %cl,%ebx			# Divide by
		xor %edx,%edx			#  sectors
		div %ebx			#  per track
		movb %ch,%bl			# Max head number
		movb %dl,%ch			# Sector number
		inc %bx				# Divide by
		xorb %dl,%dl			#  number
		div %ebx			#  of heads
		movb %dl,%bh			# Head number
		pop %dx				# Restore
		cmpl $0x3ff,%eax		# Cylinder number supportable?
		sti				# Enable interrupts
		ja ereturn			# No, return an error
		xchgb %al,%ah			# Set up cylinder
		rorb $0x2,%al			#  number
		orb %ch,%al			# Merge
		inc %ax				#  sector
		xchg %ax,%cx	 		#  number
		movb %bh,%dh			# Head number
		subb %ah,%al			# Sectors this track
		mov 0x2(%bp),%ah		# Blocks to read
		cmpb %ah,%al			# To read
		jb read.2			#  this
#ifdef	TRACK_AT_A_TIME
		movb %ah,%al			#  track
#else
		movb $1,%al			#  one sector
#endif
read.2: 	mov $0x5,%di	 		# Try count
read.3: 	les 0x4(%bp),%bx		# Transfer buffer
		push %ax			# Save
		movb $0x2,%ah			# BIOS: Read
		int $0x13			#  from disk
		pop %bx				# Restore
		jnc read.4			# If success
		dec %di				# Retry?
		jz read.6			# No
		xorb %ah,%ah			# BIOS: Reset
		int $0x13			#  disk system
		xchg %bx,%ax	 		# Block count
		jmp read.3			# Continue
read.4: 	movzbw %bl,%ax	 		# Sectors read
		add %ax,0x8(%bp)		# Adjust
		jnc read.5			#  LBA,
		incw 0xa(%bp)	 		#  transfer
read.5: 	shlb %bl			#  buffer
		add %bl,0x5(%bp)		#  pointer,
		sub %al,0x2(%bp)		#  block count
		ja read.1			# If not done
read.6: 	retw				# To caller

/* Messages */

msg_read:	.asciz "Read"
msg_part:	.asciz "Boot"

prompt: 	.asciz " error\r\n"

flags:		.byte FLAGS			# Flags

		.org PRT_OFF,0x90

/* Partition table */

		.fill 0x30,0x1,0x0
part4:		.byte 0x80, 0x00, 0x01, 0x00
		.byte 0xa5, 0xfe, 0xff, 0xff
		.byte 0x00, 0x00, 0x00, 0x00
		.byte 0x50, 0xc3, 0x00, 0x00	# 50000 sectors long, bleh

		.word 0xaa55			# Magic number
