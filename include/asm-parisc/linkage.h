#ifndef __ASM_PARISC_LINKAGE_H
#define __ASM_PARISC_LINKAGE_H

#ifndef __ALIGN
#define __ALIGN         .align 4
#define __ALIGN_STR     ".align 4"
#endif

/*
 * In parisc assembly a semicolon marks a comment.
 * Because of that we use an exclamation mark to seperate independend lines.
 */
#define ENTRY(name) \
	.globl name !\
	ALIGN !\
name:

#endif  /* __ASM_PARISC_LINKAGE_H */
