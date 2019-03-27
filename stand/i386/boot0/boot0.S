/*
 * Copyright (c) 2008 Luigi Rizzo (mostly documentation)
 * Copyright (c) 2002 Bruce M. Simpson
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

/* build options: */
#ifdef SIO		/* use serial console on COM1.	*/
#endif

#ifdef PXE		/* enable PXE/INT18 booting with F6 */
#define SAVE_MORE_MEMORY
#endif

#ifdef CHECK_DRIVE	/* make sure we boot from a HD. */
#endif

#ifdef ONLY_F_KEYS	/* Only F1..F6, no digits on console */
#endif

#ifdef VOLUME_SERIAL	/* support Volume serial number */
#define B0_BASE	0x1ae	/* move the internal data area */
#define SAVE_MEMORY
#else
#define B0_BASE	0x1b2
#endif

#ifdef TEST		/* enable some test code */
#define SAVE_MEMORY
#define SAVE_MORE_MEMORY
#endif

/*
 * Note - this code uses many tricks to save space and fit in one sector.
 * This includes using side effects of certain instructions, reusing
 * register values from previous operations, etc.
 * Be extremely careful when changing the code, even for simple things.
 */

/*
 *		BOOT BLOCK STRUCTURE
 *
 * This code implements a Master Boot Record (MBR) for an Intel/PC disk.
 * It is 512 bytes long and it is normally loaded by the BIOS (or another
 * bootloader) at 0:0x7c00. This code depends on %cs:%ip being 0:0x7c00
 *
 * The initial chunk of instructions is used as a signature by external
 * tools (e.g. boot0cfg) which can manipulate the block itself.
 *
 * The area at offset 0x1b2 contains a magic string ('Drive '), also
 * used as a signature to detect the block, and some variables that can
 * be updated by boot0cfg (and optionally written back to the disk).
 * These variables control the operation of the bootloader itself,
 * e.g. which partitions to enable, the timeout, the use of LBA
 * (called 'packet') or CHS mode, whether to force a drive number,
 * and whether to write back the user's selection back to disk.
 *
 * As in every Master Boot Record, the partition table is at 0x1be,
 * made of four 16-byte entries each containing:
 *
 *   OFF SIZE	DESCRIPTION
 *    0	  1	status (0x80: bootable, 0: non bootable)
 *    1	  3	start sector CHS
 *		   8:head, 6:sector, 2:cyl bit 9..8, 8:cyl bit 7..0
 *    4   1	partition type
 *    5   3	end sector CHS
 *    8   4	LBA of first sector
 *   12   4	partition size in sectors
 *
 * and followed by the two bytes 0x55, 0xAA (MBR signature).
 */


/*
 *		BOOT BLOCK OPERATION
 *
 * On entry, the registers contain the following values:
 *
 *	%cs:%ip	0:0x7c00
 *	%dl	drive number (0x80, 0x81, ... )
 *	%si	pointer to the partition table from which we were loaded.
 *		Some boot code (e.g. syslinux) use this info to relocate
 *		themselves, so we want to pass a valid one to the next stage.
 *		NOTE: the use of %si is not a standard.
 *
 * This boot block first relocates itself at a different address (0:0x600),
 * to free the space at 0:0x7c00 for the next stage boot block.
 *
 * It then initializes some memory at 0:0x800 and above (pointed by %bp)
 * to store the original drive number (%dl) passed to us, and to construct a
 * fake partition entry. The latter is used by the disk I/O routine and,
 * in some cases, passed in %si to the next stage boot code.
 *
 * The variables at 0x1b2 are accessed as negative offsets from %bp.
 *
 * After the relocation, the code scans the partition table printing
 * out enabled partition or disks, and waits for user input.
 *
 * When a partition is selected, or a timeout expires, the currently
 * selected partition is used to load the next stage boot code,
 * %dl and %si are set appropriately as when we were called, and
 * control is transferred to the newly loaded code at 0:0x7c00.
 */

/*
 *	CONSTANTS
 *
 * NHRDRV is the address in segment 0 where the BIOS writes the
 *	total number of hard disks in the system.
 * LOAD is the original load address and cannot be changed.
 * ORIGIN is the relocation address. If you change it, you also need
 * 	to change the value passed to the linker in the Makefile
 * PRT_OFF is the location of the partition table (from the MBR standard).
 * B0_OFF is the location of the data area, known to boot0cfg so
 *	it cannot be changed. Computed as a negative offset from 0x200
 * MAGIC is the signature of a boot block.
 */

		.set NHRDRV,0x475		# Number of hard drives
		.set ORIGIN,0x600		# Execution address
		.set LOAD,0x7c00		# Load address

		.set PRT_OFF,0x1be		# Partition table
		.set B0_OFF,(B0_BASE-0x200)	# Offset of boot0 data

		.set MAGIC,0xaa55		# Magic: bootable

		.set KEY_ENTER,0x1c		# Enter key scan code
		.set KEY_F1,0x3b		# F1 key scan code
		.set KEY_1,0x02			# #1 key scan code

		.set ASCII_BEL,'#'		# ASCII code for <BEL>
		.set ASCII_CR,0x0D		# ASCII code for <CR>

/*
 * Offsets of variables in the block at B0_OFF, and in the volatile
 * data area, computed as displacement from %bp.
 * We need to define them as constant as the assembler cannot
 * compute them in its single pass.
 */
		.set _NXTDRV,	B0_OFF+6	# Next drive
		.set _OPT,	B0_OFF+7	# Default option
		.set _SETDRV,	B0_OFF+8	# Drive to force
		.set _FLAGS,	B0_OFF+9	# Flags
		.set SETDRV,	0x20		# the 'setdrv' flag
		.set NOUPDATE,	0x40		# the 'noupdate' flag
		.set USEPACKET,	0x80		# the 'packet' flag

	/* ticks is at a fixed position */
		.set _TICKS,	(PRT_OFF - 0x200 - 2)	# Timeout ticks
		.set _MNUOPT, 0x10		# Saved menu entries

		.set TLEN, (desc_ofs - bootable_ids)	# size of bootable ids
		.globl start			# Entry point
		.code16				# This runs in real mode

/*
 * 	MAIN ENTRY POINT
 * Initialise segments and registers to known values.
 * segments start at 0.
 * The stack is immediately below the address we were loaded to.
 * NOTE: the initial section of the code (up to movw $LOAD,%sp)
 * is used by boot0cfg, together with the 'Drive ' string and
 * the 0x55, 0xaa at the end, as an identifier for version 1.0
 * of the boot code. Do not change it.
 * In version 1.0 the parameter table (_NEXTDRV etc) is at 0x1b9
 */
start:		cld				# String ops inc
		xorw %ax,%ax			# Zero
		movw %ax,%es			# Address
		movw %ax,%ds			#  data
		movw %ax,%ss			# Set up
		movw $LOAD,%sp			#  stack

	/*
	 * Copy this code to the address it was linked for, 0x600 by default.
	 */
		movw %sp,%si			# Source
		movw $start,%di			# Destination
		movw $0x100,%cx			# Word count
		rep				# Relocate
		movsw				#  code
	/*
	 * After the code, (i.e. at %di+0, 0x800) create a partition entry,
	 * initialized to LBA 0 / CHS 0:0:1.
	 * Set %bp to point to the partition and also, with negative offsets,
	 * to the variables embedded in the bootblock (nextdrv and so on).
	 */
		movw %di,%bp			# Address variables
		movb $0x8,%cl			# Words to clear
		rep				# Zero
		stosw				#  them
		incb -0xe(%di)			# Set the S field to 1

		jmp main-LOAD+ORIGIN		# Jump to relocated code

main:
#if defined(SIO) && COMSPEED != 0
	/*
	 * Init the serial port. bioscom preserves the driver number in DX.
	 */
		movw $COMSPEED,%ax		# defined by Makefile
		callw bioscom
#endif

	/*
	 * If the 'setdrv' flag is set in the boot sector, use the drive
	 * number from the boot sector at 'setdrv_num'.
	 * Optionally, do the same if the BIOS gives us an invalid number
	 * (note though that the override prevents booting from a floppy
	 * or a ZIP/flash drive in floppy emulation).
	 * The test costs 4 bytes of code so it is disabled by default.
	 */
		testb $SETDRV,_FLAGS(%bp)	# Set drive number?
#ifndef CHECK_DRIVE	/* disable drive checks */
		jz save_curdrive		# no, use the default
#else
		jnz disable_update		# Yes
		testb %dl,%dl			# Drive number valid?
		js save_curdrive		# Possibly (0x80 set)
#endif
	/*
	 * Disable updates if the drive number is forced.
	 */
disable_update:	orb $NOUPDATE,_FLAGS(%bp)	# Disable updates
		movb _SETDRV(%bp),%dl		# Use stored drive number

	/*
	 * Whatever drive we decided to use, store it at (%bp). The byte
	 * is normally used for the state of the partition (0x80 or 0x00),
	 * but we abuse it as it is very convenient to access at offset 0.
	 * The value is read back after 'check_selection'
	 */
save_curdrive:	movb %dl, (%bp)			# Save drive number
		pushw %dx			# Also in the stack
#ifdef	TEST	/* test code, print internal bios drive */
		rolb $1, %dl
		movw $drive, %si
		call putkey
#endif
		callw putn			# Print a newline
	/*
	 * Start out with a pointer to the 4th byte of the first table entry
	 * so that after 4 iterations it's beyond the end of the sector
	 * and beyond a 256 byte boundary. We use the latter trick to check for
	 * end of the loop without using an extra register (see start.5).
	 */
		movw $(partbl+0x4),%bx		# Partition table (+4)
		xorw %dx,%dx			# Item number

	/*
	 * Loop around on the partition table, printing values until we
	 * pass a 256 byte boundary.
	 */
read_entry:	movb %ch,-0x4(%bx)		# Zero active flag (ch == 0)
		btw %dx,_FLAGS(%bp)		# Entry enabled?
		jnc next_entry			# No
		movb (%bx),%al			# Load type
		test %al, %al			# skip empty partition
		jz next_entry
	/*
	 * Scan the table of bootable ids, which starts at %di and has
	 * length TLEN. On a match, %di points to the element following the
	 * match; the corresponding offset to the description is $(TLEN-1)
	 * bytes ahead. We use a count of TLEN+1 so if we don't find a match
	 * within the first TLEN entries, we hit the 'unknown' entry.
	 */
		movw $bootable_ids,%di		# Lookup tables
		movb $(TLEN+1),%cl		# Number of entries
		repne				# Locate
		scasb				#  type
	/*
	 * Get the matching element in the next array.
	 * The byte at $(TLEN-1)(%di) contains the offset of the description
	 * string from %di, so we add the number and print the string.
	 */
		addw $(TLEN-1), %di		# Adjust
		movb (%di),%cl			# Partition
		addw %cx,%di			#  description
		callw putx			# Display it

next_entry:	incw %dx			# Next item
		addb $0x10,%bl			# Next entry
		jnc read_entry			# Till done
	/*
	 * We are past a 256 byte boundary: the partition table is finished.
	 * Add one to the drive number and check it is valid.
	 * Note that if we started from a floppy, %dl was 0 so we still
	 * get an entry for the next drive, which is the first Hard Disk.
	 */
		popw %ax			# Drive number
		subb $0x80-0x1,%al		# Does next
		cmpb NHRDRV,%al			#  drive exist? (from BIOS?)
		jb print_drive			# Yes
	/*
	 * If this is the only drive, don't display it as an option.
	 */
		decw %ax			# Already drive 0?
		jz print_prompt			# Yes
	/*
	 * If it was illegal or we cycled through them, go back to drive 0.
	 */
		xorb %al,%al			# Drive 0
	/*
	 * Whatever drive we selected, make it an ascii digit and save it
	 * back to the "nxtdrv" location in case we want to save it to disk.
	 * This digit is also part of the printed drive string, so add 0x80
	 * to indicate end of string.
	 */
print_drive:	addb $'0'|0x80,%al		# Save next
		movb %al,_NXTDRV(%bp)		#  drive number
		movw $drive,%di			# Display
		callw putx			#  item
	/*
	 * Menu is complete, display a prompt followed by current selection.
	 * 'decw %si' makes the register point to the space after 'Boot: '
	 * so we do not see an extra CRLF on the screen.
	 */
print_prompt:	movw $prompt,%si		# Display
		callw putstr			#  prompt
		movb _OPT(%bp),%dl		# Display
		decw %si			#  default
		callw putkey			#  key
		jmp start_input			# Skip beep

/*
 * Here we have the code waiting for user input or a timeout.
 */
beep:		movb $ASCII_BEL,%al		# Input error, print or beep
		callw putchr

start_input:
	/*
	 * Actual Start of input loop.  Take note of time
	 */
		xorb %ah,%ah			# BIOS: Get
		int $0x1a			#  system time
		movw %dx,%di			# Ticks when
		addw _TICKS(%bp),%di		#  timeout
read_key:
	/*
	 * Busy loop, looking for keystrokes but keeping one eye on the time.
	 */
#ifndef SIO
		movb $0x1,%ah			# BIOS: Check
		int $0x16			#  for keypress
#else /* SIO */
		movb $0x03,%ah			# BIOS: Read COM
		call bioscom
		testb $0x01,%ah			# Check line status
						# (bit 1 indicates input)
#endif /* SIO */
		jnz got_key 			# Have input
		xorb %ah,%ah			# BIOS: int 0x1a, 00
		int $0x1a			#  get system time
		cmpw %di,%dx			# Timeout?
		jb read_key			# No

	/*
	 * Timed out or default selection
	 */
use_default:	movb _OPT(%bp),%al		# Load default
		orb $NOUPDATE,_FLAGS(%bp) 	# Disable updates
		jmp check_selection		# Join common code

	/*
	 * Get the keystroke.
	 * ENTER or CR confirm the current selection (same as a timeout).
	 * Otherwise convert F1..F6 (or '1'..'6') to 0..5 and check if the
	 * selection is valid.
	 * The SIO code uses ascii chars, the console code uses scancodes.
	 */
got_key:
#ifndef SIO
		xorb %ah,%ah			# BIOS: int 0x16, 00
		int $0x16			#  get keypress
		movb %ah,%al			# move scan code to %al
		cmpb $KEY_ENTER,%al
#else
		movb $0x02,%ah			# BIOS: Receive
		call bioscom
		cmpb $ASCII_CR,%al
#endif
		je use_default			# enter -> default
	/*
	 * Check if the key is acceptable, and loop back if not.
	 * The console (non-SIO) code looks at scancodes and accepts
	 * both F1..F6 and 1..6 (the latter costs 6 bytes of code),
	 * relying on the fact that F1..F6 have higher scancodes than 1..6
	 * The SIO code only takes 1..6
	 */
#ifdef SIO /* SIO mode, use ascii values */
		subb $'1',%al			# Subtract '1' ascii code
#else /*  console mode -- use scancodes */
		subb $KEY_F1,%al		/* Subtract F1 scan code */
#if !defined(ONLY_F_KEYS)
		cmpb $0x5,%al			# F1..F6
		jna 3f				# Yes
		subb $(KEY_1 - KEY_F1),%al	# Less #1 scan code
	3:
#endif /* ONLY_F_KEYS */
#endif /* SIO */
check_selection:
		cmpb $0x5,%al			# F1..F6 or 1..6 ?
#ifdef PXE /* enable PXE/INT18 using F6 */
		jne 1f;
		int $0x18			# found F6, try INT18
	1:
#endif /* PXE */
		jae beep			# Not in F1..F5, beep

	/*
	 * We have a selection.  If it's a bad selection go back to complain.
	 * The bits in MNUOPT were set when the options were printed.
	 * Anything not printed is not an option.
	 */
		cbtw				# Extend (%ah=0 used later)
		btw %ax,_MNUOPT(%bp)	 	# Option enabled?
		jnc beep			# No
	/*
	 * Save the info in the original tables
	 * for rewriting to the disk.
	 */
		movb %al,_OPT(%bp)		# Save option

	/*
	 * Make %si and %bx point to the fake partition at LBA 0 (CHS 0:0:1).
	 * Because the correct address is already in %bp, just use it.
	 * Set %dl with the drive number saved in byte 0.
	 * If we have pressed F5 or 5, then this is a good, fake value
	 * to present to the next stage boot code.
	 */
		movw %bp,%si			# Partition for write
		movb (%si),%dl			# Drive number, saved above
		movw %si,%bx			# Partition for read
		cmpb $0x4,%al			# F5/#5 pressed?
		pushf				# Save results for later
		je 1f				# Yes, F5

	/*
	 * F1..F4 was pressed, so make %bx point to the currently
	 * selected partition, and leave the drive number unchanged.
	 */
		shlb $0x4,%al			# Point to
		addw $partbl,%ax		#  selected
		xchgw %bx,%ax	 		#  partition
		movb $0x80,(%bx)		# Flag active
	/*
	 * If not asked to do a write-back (flags 0x40) don't do one.
	 * Around the call, save the partition pointer to %bx and
	 * restore to %si which is where the next stage expects it.
	 */
	1:	pushw %bx			# Save
		testb $NOUPDATE,_FLAGS(%bp)	# No updates?
		jnz 2f				# skip update
		movw $start,%bx			# Data to write
		movb $0x3,%ah			# Write sector
		callw intx13			#  to disk
	2:	popw %si			# Restore

	/*
	 * If going to next drive, replace drive with selected one.
	 * Remember to un-ascii it. Hey 0x80 is already set, cool!
	 */
		popf				# Restore %al test results
		jne 3f				# If not F5/#5
		movb _NXTDRV(%bp),%dl		# Next drive
		subb $'0',%dl			#  number
	/*
	 * Load selected bootsector to the LOAD location in RAM. If read
	 * fails or there is no 0x55aa marker, treat it as a bad selection.
	 */
	3:	movw $LOAD,%bx			# Address for read
		movb $0x2,%ah			# Read sector
		callw intx13			#  from disk
		jc beep				# If error
		cmpw $MAGIC,0x1fe(%bx)		# Bootable?
		jne beep			# No
		pushw %si			# Save ptr to selected part.
		callw putn			# Leave some space
		popw %si			# Restore, next stage uses it
		jmp *%bx			# Invoke bootstrap

/*
 * Display routines
 * putkey	prints the option selected in %dl (F1..F5 or 1..5) followed by
 *		the string at %si
 * putx:	print the option in %dl followed by the string at %di
 *		also record the drive as valid.
 * putn:	print a crlf
 * putstr:	print the string at %si
 * putchr:	print the char in al
 */

/*
 * Display the option and record the drive as valid in the options.
 * That last point is done using the btsw instruction which does
 * a test and set. We don't care for the test part.
 */
putx:		btsw %dx,_MNUOPT(%bp)		# Enable menu option
		movw $item,%si			# Display
		callw putkey			#  key
		movw %di,%si			# Display the rest
		callw putstr			# Display string

putn:		movw $crlf,%si			# To next line
		jmp putstr

putkey:
#ifndef SIO
		movb $'F',%al			# Display
		callw putchr			#  'F'
#endif
		movb $'1',%al			# Prepare
		addb %dl,%al			#  digit

putstr.1:	callw putchr			# Display char
putstr:		lodsb				# Get byte
		testb $0x80,%al 		# End of string?
		jz putstr.1			# No
		andb $~0x80,%al 		# Clear MSB then print last 

putchr:
#ifndef SIO
		pushw %bx			# Save
		movw $0x7,%bx	 		# Page:attribute
		movb $0xe,%ah			# BIOS: Display
		int $0x10			#  character
		popw %bx			# Restore
#else /* SIO */
		movb $0x01,%ah			# BIOS: Send character
bioscom:
		pushw %dx			# Save
		xorw %dx,%dx 			# Use COM1
		int $0x14			# BIOS: Serial I/O
		popw %dx			# Restore
#endif /* SIO */
		retw				# To caller

/* One-sector disk I/O routine */

/*
 * %dl: drive, %si partition entry, %es:%bx transfer buffer.
 * Load the CHS values and possibly the LBA address from the block
 * at %si, and use the appropriate method to load the sector.
 * Don't use packet mode for a floppy.
 */
intx13:						# Prepare CHS parameters
		movb 0x1(%si),%dh		# Load head
		movw 0x2(%si),%cx		# Load cylinder:sector
		movb $0x1,%al			# Sector count
		pushw %si			# Save
		movw %sp,%di			# Save
#ifndef CHECK_DRIVE				/* floppy support */
		testb %dl, %dl			# is this a floppy ?
		jz 1f				# Yes, use CHS mode
#endif
		testb $USEPACKET,_FLAGS(%bp)	# Use packet interface?
		jz 1f				# No
		pushl $0x0			# Set the
		pushl 0x8(%si)			# LBA address
		pushw %es			# Set the transfer
		pushw %bx			#  buffer address
		push  $0x1			# Block count
		push  $0x10			# Packet size
		movw %sp,%si			# Packet pointer
		decw %ax			# Verify off
		orb $0x40,%ah			# Use disk packet
	1:	int $0x13			# BIOS: Disk I/O
		movw %di,%sp			# Restore
		popw %si			# Restore
		retw				# To caller

/*
 * Various menu strings. 'item' goes after 'prompt' to save space.
 * Also use shorter versions to make room for the PXE/INT18 code.
 */
prompt:
#ifdef PXE
		.ascii "\nF6 PXE\r"
#endif
		.ascii "\nBoot:"
item:		.ascii " ";	     .byte ' '|0x80
crlf:		.ascii "\r";	     .byte '\n'|0x80

/* Partition type tables */

bootable_ids:
	/*
	 * These values indicate bootable types we know about.
	 * Corresponding descriptions are at desc_ofs:
	 * Entries don't need to be sorted.
	 */
		.byte 0x83, 0xa5, 0xa6, 0xa9, 0x06, 0x07, 0x0b
#ifndef SAVE_MORE_MEMORY
		.byte 0x05	# extended partition
#endif
#ifndef SAVE_MEMORY	/* other DOS partitions */
		.byte 0x01	# FAT12
		.byte 0x04	# FAT16 < 32M
#endif

desc_ofs:
	/*
	 * Offsets that match the known types above, used to point to the
	 * actual partition name. The last entry must point to os_misc,
	 * which is used for non-matching names.
	 */
		.byte os_linux-.		# 131, Linux
		.byte os_freebsd-.		# 165, FreeBSD
		.byte os_bsd-.			# 166, OpenBSD
		.byte os_bsd-.			# 169, NetBSD
		.byte os_dos-.			#   6, FAT16 >= 32M
		.byte os_win-.			#   7, NTFS
		.byte os_win-.			#  11, FAT32

#ifndef SAVE_MORE_MEMORY
		.byte os_ext-.			#   5, DOS Ext
#endif
#ifndef SAVE_MEMORY
		.byte os_dos-.			#   1, FAT12 DOS
		.byte os_dos-.			#   4, FAT16 <32M
#endif
		.byte os_misc-. 		# Unknown

	/*
	 * And here are the strings themselves. The last byte of
	 * the string has bit 7 set.
	 */
os_misc:	.byte '?'|0x80
os_dos:
#ifndef SAVE_MORE_MEMORY	/* 'DOS' remapped to 'WIN' if no room */
		.ascii "DO";   .byte 'S'|0x80
#endif
os_win:		.ascii "Wi";   .byte 'n'|0x80
os_linux:	.ascii "Linu"; .byte 'x'|0x80
os_freebsd:	.ascii "Free"
os_bsd:		.ascii "BS";   .byte 'D'|0x80
#ifndef SAVE_MORE_MEMORY
os_ext:		.ascii "EX";   .byte 'T'|0x80
#endif

		.org (0x200 + B0_OFF),0x90
/*
 * The boot0 version 1.0 parameter table.
 * Do not move it nor change the "Drive " string, boot0cfg
 * uses its offset and content to identify the boot sector.
 * The other fields are sometimes changed before writing back to the drive
 * Be especially careful that nxtdrv: must come after drive:, as it
 * is part of the same string.
 */
drive:		.ascii "Drive "
nxtdrv:		.byte 0x0			# Next drive number
opt:		.byte 0x0			# Option
setdrv_num:	.byte 0x80			# Drive to force
flags:		.byte FLAGS			# Flags
#ifdef VOLUME_SERIAL
		.byte 0xa8,0xa8,0xa8,0xa8       # Volume Serial Number
#endif
ticks:		.word TICKS			# Delay

		.org PRT_OFF
/*
 * Here is the 64 byte partition table that fdisk would fiddle with.
 */
partbl:		.fill 0x40,0x1,0x0		# Partition table
		.word MAGIC			# Magic number
		.org 0x200			# again, safety check
endblock:
