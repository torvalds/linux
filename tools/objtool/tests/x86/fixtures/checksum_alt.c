// SPDX-License-Identifier: GPL-2.0
/*
 * An x86 alternative whose replacement code is part of the patched function's
 * checksum.
 *
 * checksum_update_insn() walks insn->alts after hashing the instruction
 * itself, hashing the alternative's type and, when the replacement forms a
 * group, its feature number and every instruction in it.  So editing only the
 * replacement -- code the CPU may or may not ever run -- has to move the
 * function's checksum.
 *
 * It is reached through objtool's own alternative handling, so the object has
 * to go through the check pass first: insn->alts is built there, not by the
 * compiler.
 *
 * struct alt_instr is written out by hand as in empty_alternative.c: s32
 * instr_offset, s32 repl_offset, u32 ft_flags, u8 instrlen, u8 replacementlen.
 *
 * Variants, applied to the patched build only:
 *
 *   ALT_REPL     the replacement instruction changes; the original does not
 *   ALT_FEATURE  the feature number changes; no code changes at all
 */

static const char __modinfo[]
	__attribute__((section(".modinfo"), used, aligned(1))) = "\0name=vmlinux";

/*
 * Both spellings are two bytes, because a replacement may not be longer than
 * the instruction it replaces: "xchg %ax, %ax" is 66 90 and two nops are
 * 90 90.  The original below is padded to match.
 */
#if defined(PATCHED) && defined(ALT_REPL)
#define REPL_INSN "	nop\n\t	nop\n\t"
#else
#define REPL_INSN "	xchg %ax, %ax\n\t"
#endif

#if defined(PATCHED) && defined(ALT_FEATURE)
#define FEATURE "7"
#else
#define FEATURE "3"
#endif

int target(int x)
{
	asm volatile(
		"661:	nop\n\t"
		"	nop\n\t"
		"662:\n\t"
		".pushsection .altinstr_replacement, \"ax\"\n\t"
		".globl target_repl\n\t"
		"target_repl:\n\t"
		REPL_INSN
		"target_repl_end:\n\t"
		".popsection\n\t"
		".pushsection .altinstructions, \"aM\", @progbits, 14\n\t"
		".long 661b - .\n\t"
		".long target_repl - .\n\t"
		".long " FEATURE "\n\t"
		".byte 662b - 661b\n\t"
		".byte target_repl_end - target_repl\n\t"
		".popsection\n\t");

	return x + 1;
}
