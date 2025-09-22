/*	$OpenBSD: cdbr.S,v 1.3 2012/10/31 14:29:58 jsing Exp $	*/

/*
 * Copyright (c) 2004 Tom Cosgrove <tom.cosgrove@arches-consulting.com>
 * Copyright (c) 2001 John Baldwin <jhb@FreeBSD.org>
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
 */

	.file	"cdbr.S"

/* #include <machine/asm.h> */
/* #include <assym.h> */

/*
 * This program is a CD boot sector, similar to the partition boot record
 * (pbr, also called biosboot) used by hard disks.  It is implemented as a
 * "no-emulation" boot sector, as described in the "El Torito" Bootable
 * CD-ROM Format Specification.
 *
 * The function of this boot sector is to load and start the next-stage
 * cdboot program, which will load the kernel.
 *
 * The El Torito standard allows us to specify where we want to be loaded,
 * but for maximum compatibility we choose the default load address of
 * 0x07C00.
 *
 * Memory layout:
 *
 * 0x00000 -> 0x003FF	real mode interrupt vector table
 * 0x00400 -> 0x00500	BIOS data segment
 *
 * 0x00000 -> 0x073FF	our stack (grows down)		(from 29k)
 * 0x07400 -> 0x07BFF	we relocate to here		(at 29k)
 * 0x07C00 -> 0x08400	BIOS loads us here		(at 31k, for 2k)
 * 0x07C00 -> ...	/cdboot
 *
 * The BIOS loads us at physical address 0x07C00.  We then relocate to
 * 0x07400, seg:offset 0740:0000.  We then load /cdboot at seg:offset
 * 07C0:0000.
 */
#define BOOTSEG		0x7c0			/* segment we're loaded to */
#define BOOTSECTSIZE	0x800			/* our size in bytes */
#define BOOTRELOCSEG	0x740			/* segment we relocate to */
#define BOOTSTACKOFF  ((BOOTRELOCSEG << 4) - 4)	/* starts here, grows down */

/* Constants for reading from the CD */
#define ERROR_TIMEOUT		0x80		/* BIOS timeout on read */
#define NUM_RETRIES		3		/* Num times to retry */
#define SECTOR_SIZE		0x800		/* size of a sector */
#define SECTOR_SHIFT		11		/* number of place to shift */
#define BUFFER_LEN		0x100		/* number of sectors in buffr */
#define MAX_READ		0x10000		/* max we can read at a time */
#define MAX_READ_PARAS		MAX_READ >> 4
#define MAX_READ_SEC		MAX_READ >> SECTOR_SHIFT
#define MEM_READ_BUFFER		0x9000		/* buffer to read from CD */
#define MEM_VOLDESC		MEM_READ_BUFFER /* volume descriptor */
#define MEM_DIR			MEM_VOLDESC+SECTOR_SIZE /* Lookup buffer */
#define VOLDESC_LBA		0x10		/* LBA of vol descriptor */
#define VD_PRIMARY		1		/* Primary VD */
#define VD_END			255		/* VD Terminator */
#define VD_ROOTDIR		156		/* Offset of Root Dir Record */
#define DIR_LEN			0		/* Offset of Dir Rec length */
#define DIR_EA_LEN		1		/* Offset of EA length */
#define DIR_EXTENT		2		/* Offset of 64-bit LBA */
#define DIR_SIZE		10		/* Offset of 64-bit length */
#define DIR_NAMELEN		32		/* Offset of 8-bit name len */
#define DIR_NAME		33		/* Offset of dir name */

	.text
	.code16

	.globl	start
start:
	/* Set up stack */
	xorw	%ax, %ax
	movw	%ax, %ss
	movw	$BOOTSTACKOFF, %sp

	/* Relocate so we can load cdboot where we were */
	movw	$BOOTSEG, %ax
	movw	%ax, %ds
	movw	$BOOTRELOCSEG, %ax
	movw	%ax, %es
	xorw	%si, %si
	xorw	%di, %di
	movw	$BOOTSECTSIZE, %cx	/* Bytes in cdbr, relocate it all */
	cld
	rep
	movsb

	/* Jump to relocated self */
	ljmp $BOOTRELOCSEG, $reloc
reloc:

	/*
	 * Set up %ds and %es: %ds is our data segment (= %cs), %es is
	 * used to specify the segment address of the destination buffer
	 * for cd reads.  We initially have %es = %ds.
	 */
	movw	%cs, %ax
	movw	%ax, %ds
	movw	%ax, %es

	movb	%dl, drive		/* Store the boot drive number */

	movw	$signon, %si		/* Say "hi", and give boot drive */
	call	display_string
	movb	drive, %al
	call	hex_byte
	movw	$crlf, %si
	call	display_string

/*
 * Load Volume Descriptor
 */
	movl	$VOLDESC_LBA, %eax	/* Get the sector # for vol desc */
load_vd:
	pushl	%eax
	movb	$1, %dh			/* One sector */
	movw	$MEM_VOLDESC, %bx	/* Destination */
	call	read			/* Read it in */
	popl	%eax
	cmpb	$VD_PRIMARY, (%bx)	/* Primary vol descriptor? */
	je	have_vd			/* Yes */
	inc	%eax			/* Try the next one */
	cmpb	$VD_END, (%bx)		/* Is it the last one? */
	jne	load_vd			/* No, so go try the next one */
	movw	$msg_novd, %si		/* No pri vol descriptor */
	jmp	err_stop		/* Panic */
have_vd:				/* Have Primary VD */

/*
 * Look for the next-stage loader binary at pre-defined paths (loader_paths)
 */
	movw	$loader_paths, %si	/* Point to start of array */
lookup_path:
	movw	%si, loader		/* remember the one we're looking for */
	pushw	%si			/* Save file name pointer */
	call	lookup			/* Try to find file */
	popw	%di			/* Restore file name pointer */
	jnc	lookup_found		/* Found this file */
	xorb	%al, %al		/* Look for next */
	movw	$0xffff, %cx		/*  path name by */
	repnz				/*  scanning for */
	scasb				/*  nul char */
	movw	%di, %si		/* Point %si at next path */
	movb	(%si), %al		/* Get first char of next path */
	orb	%al, %al		/* Is it double nul? */
	jnz	lookup_path		/* No, try it */
	movw	$msg_failed, %si	/* Failed message */
	jmp	err_stop		/* Print it and halt */

lookup_found:				/* Found a loader file */

/*
 * Load the binary into the buffer.  Due to real mode addressing limitations
 * we have to read it in in 64k chunks.
 */
	movl	DIR_SIZE(%bx), %eax	/* Read file length */
	add	$SECTOR_SIZE-1, %eax	/* Convert length to sectors */
	shr	$SECTOR_SHIFT, %eax
	cmp	$BUFFER_LEN, %eax
	jbe	load_sizeok
	movw	$msg_load2big, %si	/* Error message */
	jmp	err_stop
load_sizeok:
	movzbw	%al, %cx		/* Num sectors to read */
	movl	DIR_EXTENT(%bx), %eax	/* Load extent */
	xorl	%edx, %edx
	movb	DIR_EA_LEN(%bx), %dl
	addl	%edx, %eax		/* Skip extended */

	/* Use %bx to hold the segment (para) number */
	movw	$BOOTSEG, %bx		/* We put cdboot here too */
load_loop:
	movb	%cl, %dh
	cmpb	$MAX_READ_SEC, %cl	/* Truncate to max read size */
	jbe	load_notrunc
	movb	$MAX_READ_SEC, %dh
load_notrunc:
	subb	%dh, %cl		/* Update count */
	pushl	%eax			/* Save */
	pushl	%ebx			/* Save */
	movw	%bx, %es		/* %bx has the segment (para) number */
	xorw	%bx, %bx		/* %es:0000 for destination */
	call	read			/* Read it in */
	popl	%ebx			/* Restore */
	popl	%eax			/* Restore */
	addl	$MAX_READ_SEC, %eax	/* Update LBA */
	addw	$MAX_READ_PARAS, %bx	/* Update dest addr */
	jcxz	load_done		/* Done? */
	jmp	load_loop		/* Keep going */
load_done:

	/* Now we can start the loaded program */

	movw	loader, %cx		/* Tell cdboot where it is */
					/* (Older versions of cdbr have */
					/*  %cx == 0 from the jcxz load_done) */
	movb	drive, %dl		/* Get the boot drive number */
	ljmp	$BOOTSEG, $0		/* Go run cdboot */

/*
 * Lookup the file in the path at [SI] from the root directory.
 *
 * Trashes: All but BX
 * Returns: CF = 0 (success), BX = pointer to record
 *          CF = 1 (not found)
 */
lookup:
	movw	$VD_ROOTDIR + MEM_VOLDESC, %bx	/* Root directory record */

lookup_dir:
	lodsb				/* Get first char of path */
	cmpb	$0, %al			/* Are we done? */
	je	lookup_done		/* Yes */
	cmpb	$'/', %al		/* Skip path separator */
	je	lookup_dir
	decw	%si			/* Undo lodsb side effect */
	call	find_file		/* Lookup first path item */
	jnc	lookup_dir		/* Try next component */
	ret
lookup_done:
	movw	$msg_loading, %si	/* Success message - say which file */
	call	display_string
	mov	loader, %si
	call	display_string
	mov	$crlf, %si
	call	display_string
	clc				/* Clear carry */
	ret

/*
 * Lookup file at [SI] in directory whose record is at [BX].
 *
 * Trashes: All but returns
 * Returns: CF = 0 (success), BX = pointer to record, SI = next path item
 *          CF = 1 (not found), SI = preserved
 */
find_file:
	mov	DIR_EXTENT(%bx), %eax	/* Load extent */
	xor	%edx, %edx
	mov	DIR_EA_LEN(%bx), %dl
	add	%edx, %eax		/* Skip extended attributes */
	mov	%eax, rec_lba		/* Save LBA */
	mov	DIR_SIZE(%bx), %eax	/* Save size */
	mov	%eax, rec_size
	xor	%cl, %cl		/* Zero length */
	push	%si			/* Save */
ff_namelen:
	inc	%cl			/* Update length */
	lodsb				/* Read char */
	cmp	$0, %al			/* Nul? */
	je	ff_namedone		/* Yes */
	cmp	$'/', %al		/* Path separator? */
	jnz	ff_namelen		/* No, keep going */
ff_namedone:
	dec	%cl			/* Adjust length and save */
	mov	%cl, name_len
	pop	%si			/* Restore */
ff_load:
	mov	rec_lba, %eax		/* Load LBA */
	mov	$MEM_DIR, %ebx		/* Address buffer */
	mov	$1, %dh			/* One sector */
	call	read			/* Read directory block */
	incl	rec_lba			/* Update LBA to next block */
ff_scan:
	mov	%ebx, %edx		/* Check for EOF */
	sub	$MEM_DIR, %edx
	cmp	%edx, rec_size
	ja	ff_scan_1
	stc				/* EOF reached */
	ret
ff_scan_1:
	cmpb	$0, DIR_LEN(%bx)	/* Last record in block? */
	je	ff_nextblock
	push	%si			/* Save */
	movzbw	DIR_NAMELEN(%bx), %si	/* Find end of string */
ff_checkver:
	cmpb	$'0', DIR_NAME-1(%bx,%si)	/* Less than '0'? */
	jb	ff_checkver_1
	cmpb	$'9', DIR_NAME-1(%bx,%si)	/* Greater than '9'? */
	ja	ff_checkver_1
	dec	%si			/* Next char */
	jnz	ff_checkver
	jmp	ff_checklen		/* All numbers in name, so */
					/*  no version */
ff_checkver_1:
	movzbw	DIR_NAMELEN(%bx), %cx
	cmp	%cx, %si		/* Did we find any digits? */
	je	ff_checkdot		/* No */
	cmpb	$';', DIR_NAME-1(%bx,%si)	/* Check for semicolon */
	jne	ff_checkver_2
	dec	%si			/* Skip semicolon */
	mov	%si, %cx
	mov	%cl, DIR_NAMELEN(%bx)	/* Adjust length */
	jmp	ff_checkdot
ff_checkver_2:
	mov	%cx, %si		/* Restore %si to end of string */
ff_checkdot:
	cmpb	$'.', DIR_NAME-1(%bx,%si)	/* Trailing dot? */
	jne	ff_checklen			/* No */
	decb	DIR_NAMELEN(%bx)	/* Adjust length */
ff_checklen:
	pop	%si			/* Restore */
	movzbw	name_len, %cx		/* Load length of name */
	cmp	%cl, DIR_NAMELEN(%bx)	/* Does length match? */
	je	ff_checkname		/* Yes, check name */
ff_nextrec:
	add	DIR_LEN(%bx), %bl	/* Next record */
	adc	$0, %bh
	jmp	ff_scan
ff_nextblock:
	subl	$SECTOR_SIZE, rec_size	/* Adjust size */
	jnc	ff_load			/* If subtract ok, keep going */
	ret				/* End of file, so not found */
ff_checkname:
	lea	DIR_NAME(%bx), %di	/* Address name in record */
	push	%si			/* Save */
	repe	cmpsb			/* Compare name */
	jcxz	ff_match		/* We have a winner! */
	pop	%si			/* Restore */
	jmp	ff_nextrec		/* Keep looking */
ff_match:
	add	$2, %sp			/* Discard saved %si */
	clc				/* Clear carry */
	ret

/*
 * Load DH sectors starting at LBA %eax into address %es:%bx.
 *
 * Preserves %bx, %cx, %dx, %si, %es
 * Trashes %eax
 */
read:
	pushw	%si			/* Save */
	pushw	%cx			/* Save since some BIOSs trash */
	movl	%eax, edd_lba		/* LBA to read from */
	movw	%es, %ax		/* Get the segment */
	movw	%ax, edd_addr + 2	/*  and store */
	movw	%bx, edd_addr		/* Store offset too */
read_retry:
	call	twiddle			/* Entertain the user */
	pushw	%dx			/* Save */
	movw	$edd_packet, %si	/* Address Packet */
	movb	%dh, edd_len		/* Set length */
	movb	drive, %dl		/* BIOS Device */
	movb	$0x42, %ah		/* BIOS: Extended Read */
	int	$0x13			/* Call BIOS */
	popw	%dx			/* Restore */
	jc	read_fail		/* Worked? */
	popw	%cx			/* Restore */
	popw	%si
	ret				/* Return */
read_fail:
	cmpb	$ERROR_TIMEOUT, %ah	/* Timeout? */
	je	read_retry		/* Yes, Retry */
read_error:
	pushw	%ax			/* Save error */
	movw	$msg_badread, %si	/* "Read error: 0x" */
	call	display_string
	popw	%ax			/* Retrieve error code */
	movb	%ah, %al		/* Into %al */
	call	hex_byte		/* Display error code */
	jmp	stay_stopped		/* ... then hang */

/*
 * Display the ASCIZ error message in %esi then halt
 */
err_stop:
	call	display_string

stay_stopped:
	sti				/* Ensure Ctl-Alt-Del will work */
	hlt				/* (don't require power cycle) */
	jmp	stay_stopped		/* (Just to make sure) */

/*
 * Output the "twiddle"
 */
twiddle:
	push	%ax			/* Save */
	push	%bx			/* Save */
	mov	twiddle_index, %al	/* Load index */
	mov	twiddle_chars, %bx	/* Address table */
	inc	%al			/* Next */
	and	$3, %al			/*  char */
	mov	%al, twiddle_index	/* Save index for next call */
	xlat				/* Get char */
	call	display_char		/* Output it */
	mov	$8, %al			/* Backspace */
	call	display_char		/* Output it */
	pop	%bx			/* Restore */
	pop	%ax			/* Restore */
	ret

/*
 * Display the ASCIZ string pointed to by %si.
 *
 * Destroys %si, possibly others.
 */
display_string:
	pushw	%ax
	cld
1:
	lodsb			/* %al = *%si++ */
	testb	%al, %al
	jz	1f
	call    display_char
	jmp	1b

/*
 * Write out value in %eax in hex
 */
hex_long:
	pushl	%eax
	shrl	$16, %eax
	call	hex_word
	popl	%eax
	/* fall thru */

/*
 * Write out value in %ax in hex
 */
hex_word:
	pushw	%ax
	mov	%ah, %al
	call	hex_byte
	popw	%ax
	/* fall thru */
/*
 * Write out value in %al in hex
 */
hex_byte:
	pushw	%ax
	shrb	$4, %al
	call	hex_nibble
	popw	%ax
	/* fall thru */

/* Write out nibble in %al */
hex_nibble:
	and	$0x0F, %al
	add	$'0', %al
	cmpb	$'9', %al
	jbe	display_char
	addb	$'A'-'9'-1, %al
	/* fall thru to display_char */

/*
 * Display the character in %al
 */
display_char:
	pushw	%ax

	pushw	%bx
	movb	$0x0e, %ah
	movw	$1, %bx
	int	$0x10
	popw	%bx
1:	popw	%ax
	ret

/*
 * Data
 */
drive:		.byte	0			/* Given to us by the BIOS */
signon:		.asciz	"CD-ROM: "
crlf:		.asciz	"\r\n"
msg_load2big:	.asciz  "File too big"
msg_badread:	.asciz  "Read error: 0x"
msg_novd:	.asciz  "No Primary Volume Descriptor"
msg_loading:	.asciz  "Loading "

/* State for searching dir */
rec_lba:	.long	0x0			/* LBA (adjusted for EA) */
rec_size:	.long	0x0			/* File size */
name_len:	.byte	0x0			/* Length of current name */

twiddle_index:	.byte	0x0
twiddle_chars:	.ascii	"|/-\\"

/* Packet for LBA (CD) read */
edd_packet:	.byte	0x10			/* Length */
		.byte	0			/* Reserved */
edd_len:	.byte	0x0			/* Num to read */
		.byte	0			/* Reserved */
edd_addr:	.word	0x0, 0x0		/* Seg:Off */
edd_lba:	.quad	0x0			/* LBA */

/* The data from here must be last in the file, only followed by 0x00 bytes */

loader:		.word	0			/* The path we end up using */

msg_failed:	.ascii	"Can't find "		/* This string runs into... */

/* loader_paths is a list of ASCIZ strings followed by a term NUL byte */
loader_paths:	.asciz  "/cdboot"
		.asciz	"/CDBOOT"
		.ascii	"/", OSREV, "/", MACH, "/cdboot"
		.byte	0			/* NUL-term line above */
		.ascii	"/", OSREV, "/", MACH_U, "/CDBOOT"
		.byte	0			/* NUL-term line above */
		.byte	0			/* Terminate the list */

	. = BOOTSECTSIZE

	.end
