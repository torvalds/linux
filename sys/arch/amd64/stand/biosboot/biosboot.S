/*	$OpenBSD: biosboot.S,v 1.13 2023/05/30 08:30:00 jsg Exp $	*/

/*
 * Copyright (c) 2003 Tobias Weingartner
 * Copyright (c) 2003 Tom Cosgrove <tom.cosgrove@arches-consulting.com>
 * Copyright (c) 1997 Michael Shalayeff, Tobias Weingartner
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR 
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */
	.file	"biosboot.S"

#include <machine/asm.h>
#include <assym.h>

/* Error indicators */
#define PBR_READ_ERROR			'R'
#define PBR_CANT_BOOT			'X'
#define PBR_BAD_MAGIC			'M'
#define PBR_TOO_MANY_INDIRECTS		'I'

#define CHAR_BLOCK_READ		'.'
#define CHAR_CHS_READ		';'

/*
 * Memory layout:
 *
 * 0x00000 -> 0x07BFF	our stack		(to  31k)
 * 0x07A00 -> 0x07BFF	typical MBR loc		(at  30k5)
 * 0x07C00 -> 0x07DFF	our code		(at  31k)
 * 0x07E00 -> ...    	/boot inode block	(at  31k5)
 * 0x07E00 -> ...    	(indirect block if nec)
 * 0x40000 -> ...	/boot			(at 256k)
 *
 * The BIOS loads the MBR at physical address 0x07C00.  It then relocates
 * itself to (typically) 0x07A00.
 *
 * The MBR then loads us at physical address 0x07C00.
 *
 * We use a long jmp to normalise our address to seg:offset 07C0:0000.
 * (In real mode on x86, segment registers contain a base address in
 * paragraphs (16 bytes).  0000:00010 is the same as 0001:0000.)
 *
 * We set the stack to start at 0000:7BFC (grows down on i386)
 *
 * We then read the inode for /boot into memory just above us at
 * 07E0:0000, and run through the direct block table (and the first
 * indirect block table, if necessary).
 *
 * We load /boot at seg:offset 4000:0000.
 *
 * Previous versions limited the size of /boot to 64k (loaded in a single
 * segment).  This version does not have this limitation.
 */
#define INODESEG	0x07e0	/* where we put /boot's inode's block */
#define INDIRECTSEG	0x07e0	/* where we put indirect table, if nec */
#define BOOTSEG		0x07c0	/* biosboot loaded here */
#define BOOTSTACKOFF  ((BOOTSEG << 4) - 4)  /* stack starts here, grows down */
#define LFMAGIC		0x464c  /* LFMAGIC (last two bytes of \7fELF) */
#define ELFMAGIC    0x464c457f  /* ELFMAGIC ("\7fELF") */

#define INODEOFF  ((INODESEG-BOOTSEG) << 4)

/*
 * The data passed by installboot is:
 *
 * inodeblk	uint32	the filesystem block that holds /boot's inode
 * inodedbl	uint32	the memory offset to the beginning of the
 *			direct block list (di_db[]).  (This is the
 *			offset within the block + $INODEOFF, which is
 *			where we load the block to.)
 * fs_bsize_p	uint16	the filesystem block size _in paragraphs_
 *			(i.e. fs_bsize / 16)
 * fs_bsize_s	uint16	the number of disk sectors in a filesystem
 *			block (i.e. fs_bsize / d_secsize). Directly written
 *			into the LBA command block, at lba_count.
 *			XXX LIMITED TO 127 BY PHOENIX EDD SPEC.
 * fsbtodb	uint8	shift count to convert filesystem blocks to
 *			disk blocks (sectors).  Note that this is NOT
 *			log2 fs_bsize, since fragmentation allows
 *			the trailing part of a file to use part of a
 *			filesystem block.  In other words, filesystem
 *			block numbers can point into the middle of
 *			filesystem blocks.
 * p_offset	uint32	the starting disk block (sector) of the
 *			filesystem
 * nblocks	uint16	the number of filesystem blocks to read.
 *			While this can be calculated as
 *			howmany(di_size, fs_bsize) it takes us too
 *			many code bytes to do it.
 * blkincr	uint8	the increment used to parse di_db[]. set to four by
 *			installboot for ffs2 (due to 64-bit blocks) and should
 *			be zero for ffs1.
 *
 * All of these are patched directly into the code where they are used
 * (once only, each), to save space.
 */

	.globl	inodeblk, inodedbl, fs_bsize_p, fsbtodb, p_offset, nblocks
	.globl	fs_bsize_s, blkincr
	.type	inodeblk, @function
	.type	inodedbl, @function
	.type	fs_bsize_p, @function
	.type	fs_bsize_s, @function
	.type	fsbtodb, @function
	.type	p_offset, @function
	.type	nblocks, @function
	.type	blkincr, @function


/* Clobbers %ax, maybe more */
#define	putc(c)		movb	$c, %al;	call	Lchr

/* Clobbers %ax, %si, maybe more */
#define	puts(s)		movw	$s, %si;	call	Lmessage


	.text
	.code16
	.globl	_start
_start:
	jmp	begin
	nop

	/*
	 * BIOS Parameter Block.  Read by many disk utilities.
	 *
	 * We would have liked biosboot to go from the superblock to
	 * the root directory to the inode for /boot, thence to read
	 * its blocks into memory.
	 *
	 * As code and data space is quite tight in the 512-byte
	 * partition boot sector, we instead get installboot to pass
	 * us some pre-processed fields.
	 *
	 * We would have liked to put these in the BIOS parameter block,
	 * as that seems to be the right place to put them (it's really
	 * the equivalent of the superblock for FAT filesystems), but
	 * caution prevents us.
	 *
	 * For now, these fields are either directly in the code (when they
	 * are used once only) or at the end of this sector.
	 */

	. = _start + 3

	.asciz	"OpenBSD"

	/* BPB */
	. = _start + 0x0b
bpb:	.word	DEV_BSIZE			/* sector size */
	.byte	2				/* sectors/cluster */
	.word	0				/* reserved sectors */
	.byte	0				/* # of FAT */
	.word	0				/* root entries */
	.word	0				/* small sectors */
	.byte	0xf8				/* media type (hd) */
	.word	0				/* sectors/fat */
	.word	0				/* sectors per track */
	.word	0				/* # of heads */

	/* EBPB */
	. = _start + 0x1c
ebpb:	.long	16			/* hidden sectors */
	.long	0			/* large sectors */
	.word	0			/* physical disk */
	.byte	0x29			/* signature, needed by NT */
	.space	4, 0			/* volume serial number */
	.asciz	"UNIX LABEL"
	.asciz	"UFS 4.4"

	/* boot code */
	. = _start + 0x3e

begin:
	/* Fix up %cs just in case */
	ljmp	$BOOTSEG, $main

	/*
	 * Come here if we have to do a CHS boot, but we get an error from
	 * BIOS get drive parameters, or it returns nsectors == 0 (in which
	 * case we can't do the division we need to convert LBA sector
	 * number to CHS).
	 */
cant_boot:
	movb	$PBR_CANT_BOOT, %al
	jmp	err_print_crlf

main:
	/* Set up stack */
	xorw	%ax, %ax
	movw	%ax, %ss
	movw	$BOOTSTACKOFF, %sp

	/* Set up needed data segment reg */
	pushw	%cs
	popw	%ds			/* Now %cs == %ds, != %ss (%ss == 0) */

#ifdef SERIAL
	/* Initialize the serial port to 9600 baud, 8N1 */
	push	%dx
	movw	$0x00e3, %ax
	movw	SERIAL, %dx
	int	$0x14
	pop	%dx
#endif

#ifdef BDEBUG
	putc('R')
#endif

	/*
	 * We're going to print our sign-on message.
	 *
	 * We're now LBA-aware, and will use LBA to load /boot if
	 * it's available.
	 */
	movw	$load_msg, %si	/* "Loading" */
	call	Lmessage /* Print pretty message */

	/*
	 * We will use LBA reads if we have LBA support, but don't even try
	 * on floppies.
	 */
	testb	$0x80, %dl
	jz	no_lba

	/*
	 * BIOS call "INT 0x13 Extensions Installation Check"
	 *	Call with	%ah = 0x41
	 *			%bx = 0x55AA
	 *			%dl = drive (0x80 for 1st hd, 0x81 for 2nd, etc)
	 *	Return:
	 *			carry set: failure
	 *				%ah = error code (0x01, invalid func)
	 *			carry clear: success
	 *				%bx = 0xAA55 (must verify)
	 *				%ah = major version of extensions
	 *				%al   (internal use)
	 *				%cx = capabilities bitmap
	 *					0x0001 - extnd disk access funcs
	 *					0x0002 - rem. drive ctrl funcs
	 *					0x0004 - EDD functions with EBP
	 *				%dx   (extension version?)
	 */

	pushw	%dx			/* Save the drive number (%dl) */
	movw	$0x55AA, %bx
	movb	$0x41, %ah
	int	$0x13
	popw	%dx			/* Retrieve drive number */

	jc	no_lba			/* Did the command work? Jump if not */
	cmpw	$0xAA55, %bx		/* Check that bl, bh exchanged */
	jne	no_lba			/* If not, don't have EDD extensions */
	testb	$0x01, %cl		/* And do we have "read" available? */
	jz	no_lba			/* Again, use CHS if not */

	/* We have LBA support, so that's the vector to use */

	movw	$load_lba, load_fsblock
	jmp	get_going

no_lba:
	pushw	%dx

	/*
	 * BIOS call "INT 0x13 Function 0x08" to get drive parameters
	 *	Call with        %ah = 0x08
	 *                       %dl = drive (0x80 for 1st hd, 0x81 for 2nd...)
	 *       Return:
	 *                       carry set: failure
	 *                           %ah = err code
	 *                       carry clear: success
	 *                           %ah = 0x00
	 *                           %al = 0x00 (some BIOSes)
	 *                           %ch = 0x00 (some BIOSes)
	 *                           %ch = max-cylinder & 0xFF
	 *                           %cl = max sector | rest of max-cyl bits
	 *                           %dh = max head number
	 *                           %dl = number of drives
	 *                                 (according to Ralph Brown Int List)
	 */
	movb	$0x08, %ah
	int	$0x13			/* We need to know heads & sectors */

	jc	cant_boot		/* If error, can't boot */

	movb	%dh, maxheads		/* Remember this */

	andb	$0x3F, %cl
	jz	cant_boot
	movb	%cl, nsectors

	putc(CHAR_CHS_READ)		/* Indicate (subtly) CHS reads */

	popw	%dx			/* Retrieve the drive number */

get_going:
	/*
	 * Older versions of biosboot used to set up the destination
	 * segment, and increase the target offset every time a number
	 * of blocks was read.  That limits /boot to 64k.
	 *
	 * In order to support /boots > 64k, we always read to offset
	 * 0000 in the target segment, and just increase the target segment
	 * each time.
	 */

	/*
	 * We would do movl inodeblk, %eax  here, but that instruction
	 * is 4 bytes long; add 4 bytes for data takes 8 bytes.  Using
	 * a load immediate takes 6 bytes, and we just get installboot
	 * to patch here, rather than data anywhere else.
	 */
inodeblk = .+2
	movl	$0x90909090, %eax	/* mov $inodeblk, %eax */

	movw	$INODESEG, %bx		/* Where to put /boot's inode */

	/*
	 * %eax - filesystem block to read
	 * %bx  - target segment (target offset is 0000)
	 * %dl  - BIOS drive number
	 */
	call	*load_fsblock		/* This will crash'n'burn on errs */

	/*
	 * We now have /boot's inode in memory.
	 *
	 * /usr/include/ufs/ufs/dinode.h for the details:
	 *
	 * Offset  8 (decimal): 64-bit file size (only use low 32 bits)
	 * Offset 40 (decimal): list of NDADDR (12) direct disk blocks
	 * Offset 88 (decimal): list of NIADDR (3) indirect disk blocks
	 *
	 * NOTE: list of indirect blocks immediately follows list of
	 * direct blocks.  We use this fact in the code.
	 *
	 * We only support loading from direct blocks plus the first
	 * indirect block.  This is the same as the previous biosboot/
	 * installboot limit.  Note that, with default 16,384-bytes
	 * filesystem blocks, the direct block list supports files up
	 * to 192 KB.  /boot is currently around 60 KB.
	 *
	 * The on-disk format can't change (filesystems with this format
	 * already exist) so okay to hardcode offsets here.
	 *
	 * The nice thing about doing things with filesystem blocks
	 * rather than sectors is that filesystem blocks numbers have
	 * 32 bits, so fit into a single register (even if "e"d).
	 *
	 * Note that this code does need updating if booting from a new
	 * filesystem is required.
	 */
#define NDADDR	12
#define di_db	40			/* Not used; addr put in by instboot */
#define di_ib	88			/* Not used; run on from direct blks */

	/*
	 * Register usage:
	 *
	 * %eax - block number for load_fsblock
	 * %bx  - target segment (target offset is 0000) for load_fsblock
	 * %dl  - BIOS drive number for load_fsblock
	 * %esi - points to block table in inode/indirect block
	 * %cx  - number of blocks to load within loop (i.e. from current
	 *	  block list, which is either the direct block list di_db[]
	 *	  or the indirect block list)
	 * %di  - total number of blocks to load
	 */

	/*
	 * We would do movl inodedbl, %esi  here, but that instruction
	 * is 4 bytes long; add 4 bytes for data takes 8 bytes.  Using
	 * a load immediate takes 6 bytes, and we just get installboot
	 * to patch here, rather than in data anywhere else.
	 */
inodedbl = .+2
	movl	$0x90909090, %esi	/* mov $inodedbl, %esi */
					/* Now esi -> di_db[] */

nblocks = .+1
	movw	$0x9090, %di		/* mov nblocks, %di */
	movw	%di, %cx
	cmpw	$NDADDR, %cx
	jc	1f
	movw	$NDADDR, %cx
1:					/* %cx = min(nblocks, $NADDR) */

	movw	$(LOADADDR >> 4), %bx	/* Target segment for /boot */

load_blocks:
	putc(CHAR_BLOCK_READ)		/* Show progress indicator */

	cld

	/* Get the next filesystem block number into %eax */
	lodsl			/* %eax = *(%si++), make sure 0x66 0xad */

	/*
	 * The addw could be a 3 byte instruction, but stick to a 4 byte
	 * one since the former introduces mysterious hangs on *some*
	 * BIOS implementations, possibly alignment related.
	 * Grand prize for somebody finding the root cause!
	 */
blkincr = .+2
	addw	$0x90, %si	/* adjust %si if needed (for ffs2) */

	pushal				/* Save all 32-bit registers */

	/*
	 * Read a single filesystem block (will almost certainly be multiple
	 * disk sectors)
	 *
	 * %eax - filesystem block to read
	 * %bx  - target segment (target offset is 0000)
	 * %dl  - BIOS drive number
	 */
	call	*load_fsblock		/* This will crash'n'burn on errs */

	popal				/* Restore 32-bit registers */

	/*
	 * We want to put addw fs_bsize_p, %bx, which takes 4 bytes
	 * of code and two bytes of data.
	 *
	 * Instead, use an immediate load, and have installboot patch
	 * here directly.
	 */
	/* Move on one filesystem block */
fs_bsize_p = .+2
	addw	$0x9090, %bx		/* addw $fs_bsize_p, %bx */

	decw	%di
	loop	load_blocks

	/* %cx == 0 ... important it stays this way (used later) */

	/*
	 * Finished reading a set of blocks.
	 *
	 * This was either the direct blocks, and there may or may not
	 * be indirect blocks to read, or it was the indirect blocks,
	 * and we may or may not have read in all of /boot.  (Ideally
	 * will have read in all of /boot.)
	 */
	orw	%di, %di
	jz	done_load		/* No more sectors to read */

	/* We have more blocks to load */

	/* We only support a single indirect block (the same as previous
	 * versions of installboot.  This is required for the boot floppies.
	 *
	 * We use a bit of the code to store a flag that indicates
	 * whether we have read the first indirect block or not.
	 *
	 * If we've already read the indirect list, we can't load this /boot.
	 *
	 * indirect	uint8	0 => running through load_blocks loop reading
	 *			direct blocks.  If != 0, we're reading the
	 *			indirect blocks.  Must use a field that is
	 *			initialised to 0.
	 */
indirect = .+2
	movw	$PBR_TOO_MANY_INDIRECTS, %ax	/* movb $PRB_TOO..., %al */
						/* movb indirect, %ah */
	orb	%ah, %ah
	jnz	err_print_crlf

	incb	indirect		/* No need to worry about wrap */
					/* around, as this will only be done */
					/* once before we fail */

	/* Okay, let's read in the indirect block */

	lodsl				/* Get blk num of 1st indirect blk */

	pushw	%bx			/* Remember where we got to */
	movw	$INODESEG, %bx
	call	*load_fsblock		/* This will crash'n'burn on errs */
	popw	%bx			/* Indirect blocks get added on to */
					/* just after where we got to */
	movl	$INODEOFF, %esi
	movw	%di, %cx		/* How many blocks left to read */

	jmp	load_blocks

done_load:
	puts(crlf)

	/* %cx == 0 from loop above... keep it that way */

	/*
	 * Check the magic signature at the beginning of /boot.
	 * Since /boot is now ELF, this should be 0x7F E L F.
	 */
	movw	$(LOADADDR >> 4), %ax	/* Target segment */
	movw	%ax, %es

	/*
	 * We cheat a little here, and only check the L and F.
	 *
	 * (Saves 3 bytes of code... the two signature bytes we
	 * don't check, and the operand size prefix that's not
	 * needed.)
	 */
	cmpw	$LFMAGIC, %es:2(,1)
	je	exec_boot

	movb	$PBR_BAD_MAGIC, %al

err_print:
	movw	$err_txt, %si
err_print2:
	movb	%al, err_id
err_stop:
	call	Lmessage
stay_stopped:
	sti				/* Ensure Ctl-Alt-Del will work */
	hlt				/* (don't require power cycle) */
	jmp	stay_stopped		/* Just to make sure :-) */

exec_boot:
	/* At this point we could try to use the entry point in
	 * the image we just loaded.  But if we do that, we also
	 * have to potentially support loading that image where it
	 * is supposed to go.  Screw it, just assume that the image
	 * is sane.
	 */
#ifdef BDEBUG
	putc('P')
#endif

	/* %cx == 0 from loop above... keep it that way */

	/*
	 * We want to do movzbl %dl, %eax ; pushl %eax to zero-extend the
	 * drive number to 32 bits and pass it to /boot.  However, this
	 * takes 6 bytes.
	 *
	 * Doing it this way saves 2 bytes.
	 */
	pushw	%cx
	movb	%dl, %cl
	pushw	%cx

	pushl	$BOOTMAGIC	/* use some magic */

	/* jmp	/boot */
	ljmp $(LINKADDR >> 4), $0
	/* not reached */


/*
 * Load a single filesystem block into memory using CHS calls.
 *
 * Input:	%eax - 32-bit filesystem block number
 * 		%bx  - target segment (target offset is 0000)
 * 		%dl  - BIOS drive number
 *
 * Output:	block successfully read in (panics if not)
 *		all general purpose registers may have been trashed
 */
load_chs:
	/*
	 * BIOS call "INT 0x13 Function 0x2" to read sectors from disk into
	 * memory.
	 *	Call with        %ah = 0x42
	 *                       %ah = 0x2
	 *                       %al = number of sectors
	 *                       %ch = cylinder & 0xFF
	 *                       %cl = sector (0-63) | rest of cylinder bits
	 *                       %dh = head
	 *                       %dl = drive (0x80 for 1st hd, 0x81 for 2nd...)
	 *                       %es:%bx = segment:offset of buffer
	 *       Return:
	 *                       carry set: failure
	 *                           %ah = err code
	 *                           %al = number of sectors transferred
	 *                       carry clear: success
	 *                           %al = 0x0 OR number of sectors transferred
	 *                                 (depends on BIOS!)
	 *                                 (according to Ralph Brown Int List)
	 */

	/* Convert the filesystem block into a sector value */
	call	fsbtosector
	movl	lba_sector, %eax	/* we can only use 24 bits, really */

	movw	fs_bsize_s, %cx	/* sectors per filesystem block */

	/*
	 * Some BIOSes require that reads don't cross track boundaries.
	 * Therefore we do all CHS reads single-sector.
	 */
calc_chs:
	pushal
	movw	%bx, %es	/* Set up target segment */

	pushw	%dx		/* Save drive number (in %dl) */
	xorl	%edx, %edx
	movl	%edx, %ecx

nsectors = .+1
	movb	$0x90, %cl	/* movb $nsectors, %cl */
				/* Doing it this way saves 4-2 = 2 bytes code */
				/* bytes (no data, since we would overload) */

	divl	%ecx, %eax
				/* Now have sector number in %dl */
	pushw	%dx		/* Remember for later */

	xorl	%edx, %edx

maxheads = .+1
	movb	$0x90, %cl	/* movb $maxheads, %cl; 0 <= maxheads <= 255 */
				/* Doing it this way saves 4-2 = 2 code */
				/* bytes (no data, since we would overload */

	incw	%cx		/* Number of heads is 1..256, no "/0" worries */

	divl	%ecx, %eax
				/* Have head number in %dl */
				/* Cylinder number in %ax */
	movb	%al, %ch	/* Bottom 8 bits of cyl number */
	shlb	$6, %ah		/* Move up top 2 bits of cyl number */
	movb	%ah, %cl	/* Top 2 bits of cyl number in here */

	popw	%bx		/* (pushed %dx, but need %dl for now */
	incb	%bl		/* Sector numbers run from 1, not 0 */
	orb	%bl, %cl	/* Or the sector number into top bits cyl */

				/* Remember, %dl has head number */
	popw	%ax
				/* %al has BIOS drive number -> %dl */

	movb	%dl, %dh	/* Now %dh has head number (from 0) */
	movb	%al, %dl	/* Now %dl has BIOS drive number */

	xorw	%bx, %bx	/* Set up target offset */

	movw	$0x0201, %ax	/* %al = 1 - read one sector at a time */
				/* %ah = 2 - int 0x13 function for CHS read */

	call	do_int_13	/* saves us 1 byte :-) */

	/* Get the next sector */

	popal
	incl	%eax
	addw	$32, %bx	/* Number of segments/paras in a sector */
	loop	calc_chs

	ret

	/* read error */
read_error:
	movb	$PBR_READ_ERROR, %al
err_print_crlf:
	movw	$err_txt_crlf, %si
	jmp	err_print2


/*
 * Load a single filesystem block into memory using LBA calls.
 *
 * Input:	%eax - 32-bit filesystem block number
 * 		%bx  - target segment (target offset is 0000)
 * 		%dl  - BIOS drive number
 *
 * Output:	block successfully read in (panics if not)
 *		all general purpose registers may have been trashed
 */
load_lba:
	/*
	 * BIOS call "INT 0x13 Extensions Extended Read"
	 *	Call with	%ah = 0x42
	 *			%dl = drive (0x80 for 1st hd, 0x81 for 2nd, etc)
	 *			%ds:%si = segment:offset of command packet
	 *	Return:
	 *			carry set: failure
	 *				%ah = error code (0x01, invalid func)
	 *				command packet's sector count field set
	 *				to the number of sectors successfully
	 *				transferred
	 *			carry clear: success
	 *				%ah = 0 (success)
	 *	Command Packet:
	 *			0x0000	BYTE	packet size (0x10 or 0x18)
	 *			0x0001	BYTE	reserved (should be 0)
	 *			0x0002	WORD	sectors to transfer (max 127)
	 *			0x0004	DWORD	seg:offset of transfer buffer
	 *			0x0008	QWORD	starting sector number
	 */
	call	fsbtosector		/* Set up lba_sector & lba_sector+4 */

	/* movb	%dh, lba_count		<- XXX done by installboot */
	movw	%bx, lba_seg
	movw	$lba_command, %si
	movb	$0x42, %ah
do_int_13:
	int	$0x13
	jc	read_error

	ret


/*
 * Converts a given filesystem block number into a disk sector
 * at lba_sector and lba_sector+4.
 *
 * Input:	%eax - 32-bit filesystem block number
 *
 * Output:	lba_sector and lba_sector+4 set up
 *		XXX
 */
fsbtosector:
	/*
	 * We want to do
	 *
	 * movb	fsbtodb, %ch		/# Shift counts we'll need #/
	 * movb	$32, %cl
	 *
	 * which is 6 bytes of code + 1 byte of data.
	 *
	 * We'll actually code it with an immediate 16-bit load into %cx,
	 * which is just 3 bytes of data (saves 4 bytes).
	 */
fsbtodb = .+2
	movw	$0x9020, %cx		/* %ch = fsbtodb, %cl = 0x20 */

	pushl	%eax
	subb	%ch, %cl
	shrl	%cl, %eax
	movl	%eax, lba_sector+4
	popl	%eax

	movb	%ch, %cl
	shll	%cl, %eax

	/*
	 * And add p_offset, which is the block offset to the start
	 * of the filesystem.
	 *
	 * We would do addl p_offset, %eax, which is 5 bytes of code
	 * and 4 bytes of data, but it's more efficient to have
	 * installboot patch directly in the code (this variable is
	 * only used here) for 6 bytes of code (but no data).
	 */
p_offset = .+2
	addl	$0x90909090, %eax	/* addl $p_offset, %eax */

	movl	%eax, lba_sector
	jnc	1f

	incl	lba_sector+4
1:
	ret


/*
 * Display string
 */
Lmessage:
	cld
1:
	lodsb			/* load a byte into %al */
	orb	%al, %al
	jz	1f
	call	Lchr
	jmp	1b

/*
 *	Lchr: write the character in %al to console
 */
Lchr:
#ifdef SERIAL
	pushw	%dx
	movb	$0x01, %ah
	xorw	%dx, %dx
	movb	SERIAL, %dl
	int	$0x14
	popw	%dx
#else
	pushw	%bx
	movb	$0x0e, %ah
	xorw	%bx, %bx
	incw	%bx		/* movw $0x01, %bx */
	int	$0x10
	popw	%bx
#endif
1:
	ret

	/* .data */

/* vector to the routine to read a particular filesystem block for us */
load_fsblock:
	.word	load_chs


/* This next block is used for the EDD command packet used to read /boot
 * sectors.
 *
 * lba_count is set up for us by installboot.  It is the number of sectors
 * in a filesystem block.  (Max value 127.)
 *
 * XXX The EDD limit of 127 sectors in one read means that we currently
 *     restrict filesystem blocks to 127 sectors, or < 64 KB.  That is
 *     effectively a 32 KB block limit, as filesystem block sizes are
 *     powers of two.  The default filesystem block size is 16 KB.
 *
 *     I say we run with this limitation and see where it bites us...
 */

lba_command:
	.byte	0x10			/* size of command packet */
	.byte	0x00			/* reserved */
fs_bsize_s:
lba_count:
	.word	0			/* sectors to transfer, max 127 */
	.word	0			/* target buffer, offset */
lba_seg:
	.word	0			/* target buffer, segment */
lba_sector:
	.long	0, 0			/* sector number */

load_msg:
	.asciz	"Loading"
err_txt_crlf:
	.ascii	"\r\n"
err_txt:
	.ascii	"ERR "
err_id:
	.ascii	"?"
crlf:	.asciz	"\r\n"

	. = 0x200 - 2
	/* a little signature */
	.word	DOSMBR_SIGNATURE
