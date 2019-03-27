#
# Copyright (c) 2001 John Baldwin <jhb@FreeBSD.org>
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
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
# This program is a freestanding boot program to load an a.out binary
# from a CD-ROM booted with no emulation mode as described by the El
# Torito standard.  Due to broken BIOSen that do not load the desired
# number of sectors, we try to fit this in as small a space as possible.
#
# Basically, we first create a set of boot arguments to pass to the loaded
# binary.  Then we attempt to load /boot/loader from the CD we were booted
# off of. 
#

#include <bootargs.h>

#
# Memory locations.
#
		.set MEM_PAGE_SIZE,0x1000	# memory page size, 4k
		.set MEM_ARG,0x900		# Arguments at start
		.set MEM_ARG_BTX,0xa100		# Where we move them to so the
						#  BTX client can see them
		.set MEM_ARG_SIZE,0x18		# Size of the arguments
		.set MEM_BTX_ADDRESS,0x9000	# where BTX lives
		.set MEM_BTX_ENTRY,0x9010	# where BTX starts to execute
		.set MEM_BTX_OFFSET,MEM_PAGE_SIZE # offset of BTX in the loader
		.set MEM_BTX_CLIENT,0xa000	# where BTX clients live
#
# a.out header fields
#
		.set AOUT_TEXT,0x04		# text segment size
		.set AOUT_DATA,0x08		# data segment size
		.set AOUT_BSS,0x0c		# zero'd BSS size
		.set AOUT_SYMBOLS,0x10		# symbol table
		.set AOUT_ENTRY,0x14		# entry point
		.set AOUT_HEADER,MEM_PAGE_SIZE	# size of the a.out header
#
# Segment selectors.
#
		.set SEL_SDATA,0x8		# Supervisor data
		.set SEL_RDATA,0x10		# Real mode data
		.set SEL_SCODE,0x18		# PM-32 code
		.set SEL_SCODE16,0x20		# PM-16 code
#
# BTX constants
#
		.set INT_SYS,0x30		# BTX syscall interrupt
#
# Constants for reading from the CD.
#
		.set ERROR_TIMEOUT,0x80		# BIOS timeout on read
		.set NUM_RETRIES,3		# Num times to retry
		.set SECTOR_SIZE,0x800		# size of a sector
		.set SECTOR_SHIFT,11		# number of place to shift
		.set BUFFER_LEN,0x100		# number of sectors in buffer
		.set MAX_READ,0x10000		# max we can read at a time
		.set MAX_READ_SEC,MAX_READ >> SECTOR_SHIFT
		.set MEM_READ_BUFFER,0x9000	# buffer to read from CD
		.set MEM_VOLDESC,MEM_READ_BUFFER # volume descriptor
		.set MEM_DIR,MEM_VOLDESC+SECTOR_SIZE # Lookup buffer
		.set VOLDESC_LBA,0x10		# LBA of vol descriptor
		.set VD_PRIMARY,1		# Primary VD
		.set VD_END,255			# VD Terminator
		.set VD_ROOTDIR,156		# Offset of Root Dir Record
		.set DIR_LEN,0			# Offset of Dir Record length
		.set DIR_EA_LEN,1		# Offset of EA length
		.set DIR_EXTENT,2		# Offset of 64-bit LBA
		.set DIR_SIZE,10		# Offset of 64-bit length
		.set DIR_NAMELEN,32		# Offset of 8-bit name len
		.set DIR_NAME,33		# Offset of dir name
#
# We expect to be loaded by the BIOS at 0x7c00 (standard boot loader entry
# point)
#
		.code16
		.globl start
		.org 0x0, 0x0
#
# Program start.
#
start:		cld				# string ops inc
		xor %ax,%ax			# zero %ax
		mov %ax,%ss			# setup the
		mov $start,%sp			#  stack
		mov %ax,%ds			# setup the
		mov %ax,%es			#  data segments
		mov %dl,drive			# Save BIOS boot device
		mov $msg_welcome,%si		# %ds:(%si) -> welcome message
		call putstr			# display the welcome message
#
# Setup the arguments that the loader is expecting from boot[12]
#
		mov $msg_bootinfo,%si		# %ds:(%si) -> boot args message
		call putstr			# display the message
		mov $MEM_ARG,%bx		# %ds:(%bx) -> boot args
		mov %bx,%di			# %es:(%di) -> boot args
		xor %eax,%eax			# zero %eax
		mov $(MEM_ARG_SIZE/4),%cx	# Size of arguments in 32-bit
						#  dwords
		rep				# Clear the arguments
		stosl				#  to zero
		mov drive,%dl			# Store BIOS boot device
		mov %dl,0x4(%bx)		#  in kargs->bootdev
		orb $KARGS_FLAGS_CD,0x8(%bx)	# kargs->bootflags |=
						#  KARGS_FLAGS_CD
#
# Load Volume Descriptor
#
		mov $VOLDESC_LBA,%eax		# Set LBA of first VD
load_vd:	push %eax			# Save %eax
		mov $1,%dh			# One sector
		mov $MEM_VOLDESC,%ebx		# Destination
		call read			# Read it in
		cmpb $VD_PRIMARY,(%bx)		# Primary VD?
		je have_vd			# Yes
		pop %eax			# Prepare to
		inc %eax			#  try next
		cmpb $VD_END,(%bx)		# Last VD?
		jne load_vd			# No, read next
		mov $msg_novd,%si		# No VD
		jmp error			# Halt
have_vd:					# Have Primary VD
#
# Try to look up the loader binary using the paths in the loader_paths
# array.
#
		mov $loader_paths,%si		# Point to start of array
lookup_path:	push %si			# Save file name pointer
		call lookup			# Try to find file
		pop %di				# Restore file name pointer
		jnc lookup_found		# Found this file
		xor %al,%al			# Look for next
		mov $0xffff,%cx			#  path name by
		repnz				#  scanning for
		scasb				#  nul char
		mov %di,%si			# Point %si at next path
		mov (%si),%al			# Get first char of next path
		or %al,%al			# Is it double nul?
		jnz lookup_path			# No, try it.
		mov $msg_failed,%si		# Failed message
		jmp error			# Halt
lookup_found:					# Found a loader file
#
# Load the binary into the buffer.  Due to real mode addressing limitations
# we have to read it in 64k chunks.
#
		mov DIR_SIZE(%bx),%eax		# Read file length
		add $SECTOR_SIZE-1,%eax		# Convert length to sectors
		shr $SECTOR_SHIFT,%eax
		cmp $BUFFER_LEN,%eax
		jbe load_sizeok
		mov $msg_load2big,%si		# Error message
		call error
load_sizeok:	movzbw %al,%cx			# Num sectors to read
		mov DIR_EXTENT(%bx),%eax	# Load extent
		xor %edx,%edx
		mov DIR_EA_LEN(%bx),%dl
		add %edx,%eax			# Skip extended
		mov $MEM_READ_BUFFER,%ebx	# Read into the buffer
load_loop:	mov %cl,%dh
		cmp $MAX_READ_SEC,%cl		# Truncate to max read size
		jbe load_notrunc
		mov $MAX_READ_SEC,%dh
load_notrunc:	sub %dh,%cl			# Update count
		push %eax			# Save
		call read			# Read it in
		pop %eax			# Restore
		add $MAX_READ_SEC,%eax		# Update LBA
		add $MAX_READ,%ebx		# Update dest addr
		jcxz load_done			# Done?
		jmp load_loop			# Keep going
load_done:
#
# Turn on the A20 address line
#
		call seta20			# Turn A20 on
#
# Relocate the loader and BTX using a very lazy protected mode
#
		mov $msg_relocate,%si		# Display the
		call putstr			#  relocation message
		mov MEM_READ_BUFFER+AOUT_ENTRY,%edi # %edi is the destination
		mov $(MEM_READ_BUFFER+AOUT_HEADER),%esi	# %esi is
						#  the start of the text
						#  segment
		mov MEM_READ_BUFFER+AOUT_TEXT,%ecx # %ecx = length of the text
						#  segment
		push %edi			# Save entry point for later
		lgdt gdtdesc			# setup our own gdt
		cli				# turn off interrupts
		mov %cr0,%eax			# Turn on
		or $0x1,%al			#  protected
		mov %eax,%cr0			#  mode
		ljmp $SEL_SCODE,$pm_start	# long jump to clear the
						#  instruction pre-fetch queue
		.code32
pm_start:	mov $SEL_SDATA,%ax		# Initialize
		mov %ax,%ds			#  %ds and
		mov %ax,%es			#  %es to a flat selector
		rep				# Relocate the
		movsb				#  text segment
		add $(MEM_PAGE_SIZE - 1),%edi	# pad %edi out to a new page
		and $~(MEM_PAGE_SIZE - 1),%edi #  for the data segment
		mov MEM_READ_BUFFER+AOUT_DATA,%ecx # size of the data segment
		rep				# Relocate the
		movsb				#  data segment
		mov MEM_READ_BUFFER+AOUT_BSS,%ecx # size of the bss
		xor %eax,%eax			# zero %eax
		add $3,%cl			# round %ecx up to
		shr $2,%ecx			#  a multiple of 4
		rep				# zero the
		stosl				#  bss
		mov MEM_READ_BUFFER+AOUT_ENTRY,%esi # %esi -> relocated loader
		add $MEM_BTX_OFFSET,%esi	# %esi -> BTX in the loader
		mov $MEM_BTX_ADDRESS,%edi	# %edi -> where BTX needs to go
		movzwl 0xa(%esi),%ecx		# %ecx -> length of BTX
		rep				# Relocate
		movsb				#  BTX
		ljmp $SEL_SCODE16,$pm_16	# Jump to 16-bit PM
		.code16
pm_16:		mov $SEL_RDATA,%ax		# Initialize
		mov %ax,%ds			#  %ds and
		mov %ax,%es			#  %es to a real mode selector
		mov %cr0,%eax			# Turn off
		and $~0x1,%al			#  protected
		mov %eax,%cr0			#  mode
		ljmp $0,$pm_end			# Long jump to clear the
						#  instruction pre-fetch queue
pm_end:		sti				# Turn interrupts back on now
#
# Copy the BTX client to MEM_BTX_CLIENT
#
		xor %ax,%ax			# zero %ax and set
		mov %ax,%ds			#  %ds and %es
		mov %ax,%es			#  to segment 0
		mov $MEM_BTX_CLIENT,%di		# Prepare to relocate
		mov $btx_client,%si		#  the simple btx client
		mov $(btx_client_end-btx_client),%cx # length of btx client
		rep				# Relocate the
		movsb				#  simple BTX client
#
# Copy the boot[12] args to where the BTX client can see them
#
		mov $MEM_ARG,%si		# where the args are at now
		mov $MEM_ARG_BTX,%di		# where the args are moving to
		mov $(MEM_ARG_SIZE/4),%cx	# size of the arguments in longs
		rep				# Relocate
		movsl				#  the words
#
# Save the entry point so the client can get to it later on
#
		pop %eax			# Restore saved entry point
		stosl				#  and add it to the end of
						#  the arguments
#
# Now we just start up BTX and let it do the rest
#
		mov $msg_jump,%si		# Display the
		call putstr			#  jump message
		ljmp $0,$MEM_BTX_ENTRY		# Jump to the BTX entry point

#
# Lookup the file in the path at [SI] from the root directory.
#
# Trashes: All but BX
# Returns: CF = 0 (success), BX = pointer to record
#          CF = 1 (not found)
#
lookup:		mov $VD_ROOTDIR+MEM_VOLDESC,%bx	# Root directory record
		push %si
		mov $msg_lookup,%si		# Display lookup message
		call putstr
		pop %si
		push %si
		call putstr
		mov $msg_lookup2,%si
		call putstr
		pop %si
lookup_dir:	lodsb				# Get first char of path
		cmp $0,%al			# Are we done?
		je lookup_done			# Yes
		cmp $'/',%al			# Skip path separator.
		je lookup_dir
		dec %si				# Undo lodsb side effect
		call find_file			# Lookup first path item
		jnc lookup_dir			# Try next component
		mov $msg_lookupfail,%si		# Not found message
		call putstr
		stc				# Set carry
		ret
		jmp error
lookup_done:	mov $msg_lookupok,%si		# Success message
		call putstr
		clc				# Clear carry
		ret

#
# Lookup file at [SI] in directory whose record is at [BX].
#
# Trashes: All but returns
# Returns: CF = 0 (success), BX = pointer to record, SI = next path item
#          CF = 1 (not found), SI = preserved
#
find_file:	mov DIR_EXTENT(%bx),%eax	# Load extent
		xor %edx,%edx
		mov DIR_EA_LEN(%bx),%dl
		add %edx,%eax			# Skip extended attributes
		mov %eax,rec_lba		# Save LBA
		mov DIR_SIZE(%bx),%eax		# Save size
		mov %eax,rec_size
		xor %cl,%cl			# Zero length
		push %si			# Save
ff.namelen:	inc %cl				# Update length
		lodsb				# Read char
		cmp $0,%al			# Nul?
		je ff.namedone			# Yes
		cmp $'/',%al			# Path separator?
		jnz ff.namelen			# No, keep going
ff.namedone:	dec %cl				# Adjust length and save
		mov %cl,name_len
		pop %si				# Restore
ff.load:	mov rec_lba,%eax		# Load LBA
		mov $MEM_DIR,%ebx		# Address buffer
		mov $1,%dh			# One sector
		call read			# Read directory block
		incl rec_lba			# Update LBA to next block
ff.scan:	mov %ebx,%edx			# Check for EOF
		sub $MEM_DIR,%edx
		cmp %edx,rec_size
		ja ff.scan.1
		stc				# EOF reached
		ret
ff.scan.1:	cmpb $0,DIR_LEN(%bx)		# Last record in block?
		je ff.nextblock
		push %si			# Save
		movzbw DIR_NAMELEN(%bx),%si	# Find end of string
ff.checkver:	cmpb $'0',DIR_NAME-1(%bx,%si)	# Less than '0'?
		jb ff.checkver.1
		cmpb $'9',DIR_NAME-1(%bx,%si)	# Greater than '9'?
		ja ff.checkver.1
		dec %si				# Next char
		jnz ff.checkver
		jmp ff.checklen			# All numbers in name, so
						#  no version
ff.checkver.1:	movzbw DIR_NAMELEN(%bx),%cx
		cmp %cx,%si			# Did we find any digits?
		je ff.checkdot			# No
		cmpb $';',DIR_NAME-1(%bx,%si)	# Check for semicolon
		jne ff.checkver.2
		dec %si				# Skip semicolon
		mov %si,%cx
		mov %cl,DIR_NAMELEN(%bx)	# Adjust length
		jmp ff.checkdot
ff.checkver.2:	mov %cx,%si			# Restore %si to end of string
ff.checkdot:	cmpb $'.',DIR_NAME-1(%bx,%si)	# Trailing dot?
		jne ff.checklen			# No
		decb DIR_NAMELEN(%bx)		# Adjust length
ff.checklen:	pop %si				# Restore
		movzbw name_len,%cx		# Load length of name
		cmp %cl,DIR_NAMELEN(%bx)	# Does length match?
		je ff.checkname			# Yes, check name
ff.nextrec:	add DIR_LEN(%bx),%bl		# Next record
		adc $0,%bh
		jmp ff.scan
ff.nextblock:	subl $SECTOR_SIZE,rec_size	# Adjust size
		jnc ff.load			# If subtract ok, keep going
		ret				# End of file, so not found
ff.checkname:	lea DIR_NAME(%bx),%di		# Address name in record
		push %si			# Save
		repe cmpsb			# Compare name
		je ff.match			# We have a winner!
		pop %si				# Restore
		jmp ff.nextrec			# Keep looking.
ff.match:	add $2,%sp			# Discard saved %si
		clc				# Clear carry
		ret

#
# Load DH sectors starting at LBA EAX into [EBX].
#
# Trashes: EAX
#
read:		push %si			# Save
		push %cx			# Save since some BIOSs trash
		mov %eax,edd_lba		# LBA to read from
		mov %ebx,%eax			# Convert address
		shr $4,%eax			#  to segment
		mov %ax,edd_addr+0x2		#  and store
read.retry:	call twiddle			# Entertain the user
		push %dx			# Save
		mov $edd_packet,%si		# Address Packet
		mov %dh,edd_len			# Set length
		mov drive,%dl			# BIOS Device
		mov $0x42,%ah			# BIOS: Extended Read
		int $0x13			# Call BIOS
		pop %dx				# Restore
		jc read.fail			# Worked?
		pop %cx				# Restore
		pop %si
		ret				# Return
read.fail:	cmp $ERROR_TIMEOUT,%ah		# Timeout?
		je read.retry			# Yes, Retry.
read.error:	mov %ah,%al			# Save error
		mov $hex_error,%di		# Format it
		call hex8			#  as hex
		mov $msg_badread,%si		# Display Read error message

#
# Display error message at [SI] and halt.
#
error:		call putstr			# Display message
halt:		hlt
		jmp halt			# Spin

#
# Display a null-terminated string.
#
# Trashes: AX, SI
#
putstr:		push %bx			# Save
putstr.load:	lodsb				# load %al from %ds:(%si)
		test %al,%al			# stop at null
		jnz putstr.putc			# if the char != null, output it
		pop %bx				# Restore
		ret				# return when null is hit
putstr.putc:	call putc			# output char
		jmp putstr.load			# next char

#
# Display a single char.
#
putc:		mov $0x7,%bx			# attribute for output
		mov $0xe,%ah			# BIOS: put_char
		int $0x10			# call BIOS, print char in %al
		ret				# Return to caller

#
# Output the "twiddle"
#
twiddle:	push %ax			# Save
		push %bx			# Save
		mov twiddle_index,%al		# Load index
		mov $twiddle_chars,%bx		# Address table
		inc %al				# Next
		and $3,%al			#  char
		mov %al,twiddle_index		# Save index for next call
		xlat				# Get char
		call putc			# Output it
		mov $8,%al			# Backspace
		call putc			# Output it
		pop %bx				# Restore
		pop %ax				# Restore
		ret

#
# Enable A20. Put an upper limit on the amount of time we wait for the
# keyboard controller to get ready (65K x ISA access time). If
# we wait more than that amount, the hardware is probably
# legacy-free and simply doesn't have a keyboard controller.
# Thus, the A20 line is already enabled.
#
seta20: 	cli				# Disable interrupts
		xor %cx,%cx			# Clear
seta20.1:	inc %cx				# Increment, overflow?
		jz seta20.3			# Yes
		in $0x64,%al			# Get status
		test $0x2,%al			# Busy?
		jnz seta20.1			# Yes
		mov $0xd1,%al			# Command: Write
		out %al,$0x64			#  output port
seta20.2:	in $0x64,%al			# Get status
		test $0x2,%al			# Busy?
		jnz seta20.2			# Yes
		mov $0xdf,%al			# Enable
		out %al,$0x60			#  A20
seta20.3:	sti				# Enable interrupts
		ret				# To caller

#
# Convert AL to hex, saving the result to [EDI].
#
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

#
# BTX client to start btxldr
#
		.code32
btx_client:	mov $(MEM_ARG_BTX-MEM_BTX_CLIENT+MEM_ARG_SIZE-4), %esi
						# %ds:(%esi) -> end
						#  of boot[12] args
		mov $(MEM_ARG_SIZE/4),%ecx	# Number of words to push
		std				# Go backwards
push_arg:	lodsl				# Read argument
		push %eax			# Push it onto the stack
		loop push_arg			# Push all of the arguments
		cld				# In case anyone depends on this
		pushl MEM_ARG_BTX-MEM_BTX_CLIENT+MEM_ARG_SIZE # Entry point of
						#  the loader
		push %eax			# Emulate a near call
		mov $0x1,%eax			# 'exec' system call
		int $INT_SYS			# BTX system call
btx_client_end:
		.code16

		.p2align 4
#
# Global descriptor table.
#
gdt:		.word 0x0,0x0,0x0,0x0		# Null entry
		.word 0xffff,0x0,0x9200,0xcf	# SEL_SDATA
		.word 0xffff,0x0,0x9200,0x0	# SEL_RDATA
		.word 0xffff,0x0,0x9a00,0xcf	# SEL_SCODE (32-bit)
		.word 0xffff,0x0,0x9a00,0x8f	# SEL_SCODE16 (16-bit)
gdt.1:
#
# Pseudo-descriptors.
#
gdtdesc:	.word gdt.1-gdt-1		# Limit
		.long gdt			# Base
#
# EDD Packet
#
edd_packet:	.byte 0x10			# Length
		.byte 0				# Reserved
edd_len:	.byte 0x0			# Num to read
		.byte 0				# Reserved
edd_addr:	.word 0x0,0x0			# Seg:Off
edd_lba:	.quad 0x0			# LBA

drive:		.byte 0

#
# State for searching dir
#
rec_lba:	.long 0x0			# LBA (adjusted for EA)
rec_size:	.long 0x0			# File size
name_len:	.byte 0x0			# Length of current name

twiddle_index:	.byte 0x0

msg_welcome:	.asciz	"CD Loader 1.2\r\n\n"
msg_bootinfo:	.asciz	"Building the boot loader arguments\r\n"
msg_relocate:	.asciz	"Relocating the loader and the BTX\r\n"
msg_jump:	.asciz	"Starting the BTX loader\r\n"
msg_badread:	.ascii  "Read Error: 0x"
hex_error:	.asciz	"00\r\n"
msg_novd:	.asciz  "Could not find Primary Volume Descriptor\r\n"
msg_lookup:	.asciz  "Looking up "
msg_lookup2:	.asciz  "... "
msg_lookupok:	.asciz  "Found\r\n"
msg_lookupfail:	.asciz  "File not found\r\n"
msg_load2big:	.asciz  "File too big\r\n"
msg_failed:	.asciz	"Boot failed\r\n"
twiddle_chars:	.ascii	"|/-\\"
loader_paths:	.asciz  "/BOOT/LOADER"
		.asciz	"/boot/loader"
		.byte 0

