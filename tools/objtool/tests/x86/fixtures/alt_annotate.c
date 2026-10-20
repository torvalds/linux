// SPDX-License-Identifier: GPL-2.0
/*
 * An x86 alternative whose replacement instruction carries a text annotation.
 *
 * The kernel does this wherever an ALTERNATIVE contains something objtool has
 * to be told about -- a retpoline-safe indirect branch, an intentionally
 * missing ENDBR -- so the .discard.annotate_insn entry references an address
 * inside .altinstr_replacement rather than inside a function.
 *
 * Two things make that awkward for klp diff, and both are why this fixture
 * exists.  Replacement code has no real symbol: objtool invents a NOTYPE fake
 * symbol for it, so an annotation pointing there does not reference a FUNC.
 * And .discard.annotate_insn has to be cloned after .altinstructions, or the
 * replacement it names has no clone to point at yet.
 *
 * struct alt_instr is written out by hand as in empty_alternative.c: s32
 * instr_offset, s32 repl_offset, u32 ft_flags, u8 instrlen, u8 replacementlen,
 * with an entsize so klp diff can find the entry boundaries.
 * .discard.annotate_insn entries are s32 offset, s32 type; type 2 is
 * ANNOTYPE_RETPOLINE_SAFE.
 *
 * The replacement label is global so the relocations name it rather than
 * .altinstr_replacement plus an addend, which klp diff cannot convert.  It is
 * still NOTYPE, which is the shape that matters here.
 */

static const char __modinfo[]
	__attribute__((section(".modinfo"), used, aligned(1))) = "\0name=vmlinux";

int target(int x)
{
	asm volatile(
		"661:	nop\n\t"
		".pushsection .altinstr_replacement, \"ax\"\n\t"
		".globl target_repl\n\t"
		"target_repl:\n\t"
		"	nop\n\t"
		/* The annotation lands inside the replacement. */
		".pushsection .discard.annotate_insn, \"M\", @progbits, 8\n\t"
		".long target_repl - .\n\t"
		".long 2\n\t"
		".popsection\n\t"
		"target_repl_end:\n\t"
		".popsection\n\t"
		".pushsection .altinstructions, \"aM\", @progbits, 14\n\t"
		".long 661b - .\n\t"
		".long target_repl - .\n\t"
		".long 0\n\t"
		".byte 1\n\t"
		".byte target_repl_end - target_repl\n\t"
		".popsection\n\t");
#ifdef PATCHED
	return x + 2;
#else
	return x + 1;
#endif
}
