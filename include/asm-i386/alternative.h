#ifndef _I386_ALTERNATIVE_H
#define _I386_ALTERNATIVE_H

#ifdef __KERNEL__

#include <asm/types.h>

struct alt_instr {
	u8 *instr; 		/* original instruction */
	u8 *replacement;
	u8  cpuid;		/* cpuid bit set for replacement */
	u8  instrlen;		/* length of original instruction */
	u8  replacementlen; 	/* length of new instruction, <= instrlen */
	u8  pad;
};

extern void apply_alternatives(struct alt_instr *start, struct alt_instr *end);

struct module;
extern void alternatives_smp_module_add(struct module *mod, char *name,
					void *locks, void *locks_end,
					void *text, void *text_end);
extern void alternatives_smp_module_del(struct module *mod);
extern void alternatives_smp_switch(int smp);

#endif

/*
 * Alternative instructions for different CPU types or capabilities.
 *
 * This allows to use optimized instructions even on generic binary
 * kernels.
 *
 * length of oldinstr must be longer or equal the length of newinstr
 * It can be padded with nops as needed.
 *
 * For non barrier like inlines please define new variants
 * without volatile and memory clobber.
 */
#define alternative(oldinstr, newinstr, feature)			\
	asm volatile ("661:\n\t" oldinstr "\n662:\n" 			\
		      ".section .altinstructions,\"a\"\n"		\
		      "  .align 4\n"					\
		      "  .long 661b\n"            /* label */		\
		      "  .long 663f\n"		  /* new instruction */	\
		      "  .byte %c0\n"             /* feature bit */	\
		      "  .byte 662b-661b\n"       /* sourcelen */	\
		      "  .byte 664f-663f\n"       /* replacementlen */	\
		      ".previous\n"					\
		      ".section .altinstr_replacement,\"ax\"\n"		\
		      "663:\n\t" newinstr "\n664:\n"   /* replacement */\
		      ".previous" :: "i" (feature) : "memory")

/*
 * Alternative inline assembly with input.
 *
 * Pecularities:
 * No memory clobber here.
 * Argument numbers start with 1.
 * Best is to use constraints that are fixed size (like (%1) ... "r")
 * If you use variable sized constraints like "m" or "g" in the
 * replacement maake sure to pad to the worst case length.
 */
#define alternative_input(oldinstr, newinstr, feature, input...)	\
	asm volatile ("661:\n\t" oldinstr "\n662:\n"			\
		      ".section .altinstructions,\"a\"\n"		\
		      "  .align 4\n"					\
		      "  .long 661b\n"            /* label */		\
		      "  .long 663f\n"		  /* new instruction */ \
		      "  .byte %c0\n"             /* feature bit */	\
		      "  .byte 662b-661b\n"       /* sourcelen */	\
		      "  .byte 664f-663f\n"       /* replacementlen */ 	\
		      ".previous\n"					\
		      ".section .altinstr_replacement,\"ax\"\n"		\
		      "663:\n\t" newinstr "\n664:\n"   /* replacement */\
		      ".previous" :: "i" (feature), ##input)

/*
 * Alternative inline assembly for SMP.
 *
 * alternative_smp() takes two versions (SMP first, UP second) and is
 * for more complex stuff such as spinlocks.
 *
 * The LOCK_PREFIX macro defined here replaces the LOCK and
 * LOCK_PREFIX macros used everywhere in the source tree.
 *
 * SMP alternatives use the same data structures as the other
 * alternatives and the X86_FEATURE_UP flag to indicate the case of a
 * UP system running a SMP kernel.  The existing apply_alternatives()
 * works fine for patching a SMP kernel for UP.
 *
 * The SMP alternative tables can be kept after boot and contain both
 * UP and SMP versions of the instructions to allow switching back to
 * SMP at runtime, when hotplugging in a new CPU, which is especially
 * useful in virtualized environments.
 *
 * The very common lock prefix is handled as special case in a
 * separate table which is a pure address list without replacement ptr
 * and size information.  That keeps the table sizes small.
 */

#ifdef CONFIG_SMP
#define alternative_smp(smpinstr, upinstr, args...)			\
	asm volatile ("661:\n\t" smpinstr "\n662:\n" 			\
		      ".section .smp_altinstructions,\"a\"\n"		\
		      "  .align 4\n"					\
		      "  .long 661b\n"            /* label */		\
		      "  .long 663f\n"		  /* new instruction */	\
		      "  .byte 0x68\n"            /* X86_FEATURE_UP */	\
		      "  .byte 662b-661b\n"       /* sourcelen */	\
		      "  .byte 664f-663f\n"       /* replacementlen */	\
		      ".previous\n"					\
		      ".section .smp_altinstr_replacement,\"awx\"\n"   	\
		      "663:\n\t" upinstr "\n"     /* replacement */	\
		      "664:\n\t.fill 662b-661b,1,0x42\n" /* space for original */ \
		      ".previous" : args)

#define LOCK_PREFIX \
		".section .smp_locks,\"a\"\n"	\
		"  .align 4\n"			\
		"  .long 661f\n" /* address */	\
		".previous\n"			\
	       	"661:\n\tlock; "

#else /* ! CONFIG_SMP */
#define alternative_smp(smpinstr, upinstr, args...) \
	asm volatile (upinstr : args)
#define LOCK_PREFIX ""
#endif

#endif /* _I386_ALTERNATIVE_H */
