#-
# Copyright (c) 2007 Yahoo!, Inc.
# All rights reserved.
# Written by: John Baldwin <jhb@FreeBSD.org>
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
# 3. Neither the name of the author nor the names of any co-contributors
#    may be used to endorse or promote products derived from this software
#    without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# $FreeBSD$
#
# Partly from: src/sys/boot/i386/mbr/mbr.s 1.7

# A 512 byte PMBR boot manager that looks for a FreeBSD boot GPT partition
# and boots it.

		.set LOAD,0x7c00		# Load address
		.set EXEC,0x600 		# Execution address
		.set MAGIC,0xaa55		# Magic: bootable
		.set SECSIZE,0x200		# Size of a single disk sector
		.set DISKSIG,440		# Disk signature offset
		.set STACK,EXEC+SECSIZE*4	# Stack address
		.set GPT_ADDR,STACK		# GPT header address
		.set GPT_SIG,0
		.set GPT_SIG_0,0x20494645	# "EFI "
		.set GPT_SIG_1,0x54524150	# "PART"
		.set GPT_MYLBA,24
		.set GPT_PART_LBA,72
		.set GPT_NPART,80
		.set GPT_PART_SIZE,84
		.set PART_ADDR,GPT_ADDR+SECSIZE	# GPT partition array address
		.set PART_TYPE,0
		.set PART_START_LBA,32
		.set PART_END_LBA,40
		.set DPBUF,PART_ADDR+SECSIZE
		.set DPBUF_SEC,0x10		# Number of sectors

		.set NHRDRV,0x475		# Number of hard drives

		.globl start			# Entry point
		.code16

#
# Setup the segment registers for flat addressing and setup the stack.
#
start:		cld				# String ops inc
		xorw %ax,%ax			# Zero
		movw %ax,%es			# Address
		movw %ax,%ds			#  data
		movw %ax,%ss			# Set up
		movw $STACK,%sp			#  stack
#
# Relocate ourself to a lower address so that we have more room to load
# other sectors.
# 
		movw $main-EXEC+LOAD,%si	# Source
		movw $main,%di			# Destination
		movw $SECSIZE-(main-start),%cx	# Byte count
		rep				# Relocate
		movsb				#  code
#
# Jump to the relocated code.
#
		jmp main-LOAD+EXEC		# To relocated code
#
# Validate drive number in %dl.
#
main:	 	cmpb $0x80,%dl			# Drive valid?
		jb main.1			# No
		movb NHRDRV,%dh			# Calculate the highest
		addb $0x80,%dh			#  drive number available
		cmpb %dh,%dl			# Within range?
		jb main.2			# Yes
main.1: 	movb $0x80,%dl			# Assume drive 0x80
#
# Load the GPT header and verify signature.  Try LBA 1 for the primary one and
# the last LBA for the backup if it is broken.
#
main.2:		call getdrvparams		# Read drive parameters
		movb $1,%dh			# %dh := 1 (reading primary)
main.2a:	movw $GPT_ADDR,%bx
		movw $lba,%si
		call read			# Read header and check GPT sig
		cmpl $GPT_SIG_0,GPT_ADDR+GPT_SIG
		jnz main.2b
		cmpl $GPT_SIG_1,GPT_ADDR+GPT_SIG+4
		jnz main.2b
		jmp load_part
main.2b:	cmpb $1,%dh			# Reading primary?
		jne err_pt			# If no - invalid table found
#
# Try alternative LBAs from the last sector for the GPT header.
#
main.3:		movb $0,%dh			# %dh := 0 (reading backup)
		movw $DPBUF+DPBUF_SEC,%si	# %si = last sector + 1
		movw $lba,%di			# %di = $lba
main.3a:	decl (%si)			# 0x0(%si) = last sec (0-31)
		movw $2,%cx
		rep
		movsw				# $lastsec--, copy it to $lba
		jmp main.2a			# Read the next sector
#
# Load a partition table sector from disk and look for a FreeBSD boot
# partition.
#
load_part:	movw $GPT_ADDR+GPT_PART_LBA,%si
		movw $PART_ADDR,%bx
		call read
scan:		movw %bx,%si			# Compare partition UUID
		movw $boot_uuid,%di		#  with FreeBSD boot UUID 
		movb $0x10,%cl
		repe cmpsb
		jnz next_part			# Didn't match, next partition
#
# We found a boot partition.  Load it into RAM starting at 0x7c00.
#
		movw %bx,%di			# Save partition pointer in %di
		leaw PART_START_LBA(%di),%si
		movw $LOAD/16,%bx
		movw %bx,%es
		xorw %bx,%bx
load_boot:	push %si			# Save %si
		call read
		pop %si				# Restore
		movl PART_END_LBA(%di),%eax	# See if this was the last LBA
		cmpl (%si),%eax
		jnz next_boot
		movl PART_END_LBA+4(%di),%eax
		cmpl 4(%si),%eax
		jnz next_boot
		mov %bx,%es			# Reset %es to zero 
		jmp LOAD			# Jump to boot code
next_boot:	incl (%si)			# Next LBA
		adcl $0,4(%si)
		mov %es,%ax			# Adjust segment for next
		addw $SECSIZE/16,%ax		#  sector
		cmp $0x9000,%ax			# Don't load past 0x90000,
		jae err_big			#  545k should be enough for
		mov %ax,%es			#  any boot code. :)
		jmp load_boot
#
# Move to the next partition.  If we walk off the end of the sector, load
# the next sector.  We assume that partition entries are smaller than 64k
# and that they won't span a sector boundary.
#
# XXX: Should we int 0x18 instead of err_noboot if we hit the end of the table?
#
next_part:	decl GPT_ADDR+GPT_NPART		# Was this the last partition?
		jz err_noboot
		movw GPT_ADDR+GPT_PART_SIZE,%ax
		addw %ax,%bx			# Next partition
		cmpw $PART_ADDR+0x200,%bx	# Still in sector?
		jb scan
		incl GPT_ADDR+GPT_PART_LBA	# Next sector
		adcl $0,GPT_ADDR+GPT_PART_LBA+4
		jmp load_part
#
# Load a sector (64-bit LBA at %si) from disk %dl into %es:%bx by creating
# a EDD packet on the stack and passing it to the BIOS.  Trashes %ax and %si.
#
read:		pushl 0x4(%si)			# Set the LBA
		pushl 0x0(%si)			#  address
		pushw %es			# Set the address of
		pushw %bx			#  the transfer buffer
		pushw $0x1			# Read 1 sector
		pushw $0x10			# Packet length
		movw %sp,%si			# Packer pointer
		movw $0x4200,%ax		# BIOS:	LBA Read from disk
		int $0x13			# Call the BIOS
		add $0x10,%sp			# Restore stack
		jc err_rd			# If error
		ret
#
# Check the number of LBAs on the drive index %dx.  Trashes %ax and %si.
#
getdrvparams:
		movw $DPBUF,%si			# Set the address of result buf
		movw $0x001e,(%si)		# len
		movw $0x4800,%ax		# BIOS: Read Drive Parameters
		int $0x13			# Call the BIOS
		jc err_rd			# "I/O error" if error
		ret
#
# Various error message entry points.
#
err_big: 	movw $msg_big,%si		# "Boot loader too
		jmp putstr			#  large"

err_pt: 	movw $msg_pt,%si		# "Invalid partition
		jmp putstr			#  table"

err_rd: 	movw $msg_rd,%si		# "I/O error loading
		jmp putstr			#  boot loader"

err_noboot: 	movw $msg_noboot,%si		# "Missing boot
		jmp putstr			#  loader"
#
# Output an ASCIZ string to the console via the BIOS.
# 
putstr.0:	movw $0x7,%bx	 		# Page:attribute
		movb $0xe,%ah			# BIOS: Display
		int $0x10			#  character
putstr: 	lodsb				# Get character
		testb %al,%al			# End of string?
		jnz putstr.0			# No
putstr.1:	jmp putstr.1			# Await reset

msg_big: 	.asciz "Boot loader too large"
msg_pt: 	.asciz "Invalid partition table"
msg_rd: 	.asciz "I/O error loading boot loader"
msg_noboot: 	.asciz "Missing boot loader"

lba:		.quad 1				# LBA of GPT header 

boot_uuid:	.long 0x83bd6b9d
		.word 0x7f41
		.word 0x11dc
		.byte 0xbe
		.byte 0x0b
		.byte 0x00
		.byte 0x15
		.byte 0x60
		.byte 0xb8
		.byte 0x4f
		.byte 0x0f

		.org DISKSIG,0x90
sig:		.long 0				# OS Disk Signature
		.word 0				# "Unknown" in PMBR

partbl: 	.fill 0x10,0x4,0x0		# Partition table
		.word MAGIC			# Magic number
