/*	$OpenBSD: os-note-elf.h,v 1.5 2009/03/06 23:13:40 kurt Exp $	*/
/*
 * Contents:
 *
 *  long Name length
 *  long Description length
 *  long ELF_NOTE_TYPE_OSVERSION (1) XXX - need a define.
 *  "OpenBSD\0"
 *  version? 0 XXX
 */

__asm("	.section \".note.openbsd.ident\", \"a\"\n"
"	.p2align 2\n"
"	.long	8\n"
"	.long	4\n"
"	.long	1\n"
"	.ascii \"OpenBSD\\0\"\n"
"	.long	0\n"
"	.previous\n");
