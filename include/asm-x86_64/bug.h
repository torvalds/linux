#ifndef __ASM_X8664_BUG_H
#define __ASM_X8664_BUG_H 1

#include <linux/stringify.h>

/*
 * Tell the user there is some problem.  The exception handler decodes 
 * this frame.
 */
struct bug_frame {
	unsigned char ud2[2];
	unsigned char push;
	signed int filename;
	unsigned char ret;
	unsigned short line;
} __attribute__((packed));

#ifdef CONFIG_BUG
#define HAVE_ARCH_BUG
/* We turn the bug frame into valid instructions to not confuse
   the disassembler. Thanks to Jan Beulich & Suresh Siddha
   for nice instruction selection.
   The magic numbers generate mov $64bitimm,%eax ; ret $offset. */
#define BUG() 								\
	asm volatile(							\
	"ud2 ; pushq $%c1 ; ret $%c0" :: 				\
		     "i"(__LINE__), "i" (__FILE__))
void out_of_line_bug(void);
#else
static inline void out_of_line_bug(void) { }
#endif

#include <asm-generic/bug.h>
#endif
