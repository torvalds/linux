#ifndef _X86_64_ALTERNATIVE_H
#define _X86_64_ALTERNATIVE_H

#ifdef __KERNEL__

#include <linux/types.h>
#include <linux/stddef.h>

/*
 * Alternative inline assembly for SMP.
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
#define LOCK_PREFIX \
		".section .smp_locks,\"a\"\n"	\
		"  .align 8\n"			\
		"  .quad 661f\n" /* address */	\
		".previous\n"			\
	       	"661:\n\tlock; "

#else /* ! CONFIG_SMP */
#define LOCK_PREFIX ""
#endif

/* This must be included *after* the definition of LOCK_PREFIX */
#include <asm/cpufeature.h>

struct alt_instr {
	u8 *instr; 		/* original instruction */
	u8 *replacement;
	u8  cpuid;		/* cpuid bit set for replacement */
	u8  instrlen;		/* length of original instruction */
	u8  replacementlen; 	/* length of new instruction, <= instrlen */
	u8  pad[5];
};

extern void alternative_instructions(void);
extern void apply_alternatives(struct alt_instr *start, struct alt_instr *end);

struct module;

#ifdef CONFIG_SMP
extern void alternatives_smp_module_add(struct module *mod, char *name,
					void *locks, void *locks_end,
					void *text, void *text_end);
extern void alternatives_smp_module_del(struct module *mod);
extern void alternatives_smp_switch(int smp);
#else
static inline void alternatives_smp_module_add(struct module *mod, char *name,
					void *locks, void *locks_end,
					void *text, void *text_end) {}
static inline void alternatives_smp_module_del(struct module *mod) {}
static inline void alternatives_smp_switch(int smp) {}
#endif

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
#define alternative(oldinstr, newinstr, feature) 	\
	asm volatile ("661:\n\t" oldinstr "\n662:\n" 		     \
		      ".section .altinstructions,\"a\"\n"     	     \
		      "  .align 8\n"				       \
		      "  .quad 661b\n"            /* label */          \
		      "  .quad 663f\n"		  /* new instruction */ \
		      "  .byte %c0\n"             /* feature bit */    \
		      "  .byte 662b-661b\n"       /* sourcelen */      \
		      "  .byte 664f-663f\n"       /* replacementlen */ \
		      ".previous\n"					\
		      ".section .altinstr_replacement,\"ax\"\n"		\
		      "663:\n\t" newinstr "\n664:\n"   /* replacement */ \
		      ".previous" :: "i" (feature) : "memory")

/*
 * Alternative inline assembly with input.
 *
 * Pecularities:
 * No memory clobber here.
 * Argument numbers start with 1.
 * Best is to use constraints that are fixed size (like (%1) ... "r")
 * If you use variable sized constraints like "m" or "g" in the
 * replacement make sure to pad to the worst case length.
 */
#define alternative_input(oldinstr, newinstr, feature, input...)	\
	asm volatile ("661:\n\t" oldinstr "\n662:\n"			\
		      ".section .altinstructions,\"a\"\n"		\
		      "  .align 8\n"					\
		      "  .quad 661b\n"            /* label */		\
		      "  .quad 663f\n"		  /* new instruction */	\
		      "  .byte %c0\n"             /* feature bit */	\
		      "  .byte 662b-661b\n"       /* sourcelen */	\
		      "  .byte 664f-663f\n"       /* replacementlen */	\
		      ".previous\n"					\
		      ".section .altinstr_replacement,\"ax\"\n"		\
		      "663:\n\t" newinstr "\n664:\n"   /* replacement */ \
		      ".previous" :: "i" (feature), ##input)

/* Like alternative_input, but with a single output argument */
#define alternative_io(oldinstr, newinstr, feature, output, input...) \
	asm volatile ("661:\n\t" oldinstr "\n662:\n"			\
		      ".section .altinstructions,\"a\"\n"		\
		      "  .align 8\n"					\
		      "  .quad 661b\n"            /* label */		\
		      "  .quad 663f\n"		  /* new instruction */	\
		      "  .byte %c[feat]\n"        /* feature bit */	\
		      "  .byte 662b-661b\n"       /* sourcelen */	\
		      "  .byte 664f-663f\n"       /* replacementlen */	\
		      ".previous\n"					\
		      ".section .altinstr_replacement,\"ax\"\n"		\
		      "663:\n\t" newinstr "\n664:\n"   /* replacement */ \
		      ".previous" : output : [feat] "i" (feature), ##input)

/*
 * use this macro(s) if you need more than one output parameter
 * in alternative_io
 */
#define ASM_OUTPUT2(a, b) a, b

struct paravirt_patch;
#ifdef CONFIG_PARAVIRT
void apply_paravirt(struct paravirt_patch *start, struct paravirt_patch *end);
#else
static inline void
apply_paravirt(struct paravirt_patch *start, struct paravirt_patch *end)
{}
#define __parainstructions NULL
#define __parainstructions_end NULL
#endif

#endif /* _X86_64_ALTERNATIVE_H */
