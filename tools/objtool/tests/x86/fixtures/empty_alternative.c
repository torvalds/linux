// SPDX-License-Identifier: GPL-2.0
/*
 * An x86 alternative with an empty replacement, as the second entry of
 * ALTERNATIVE_2("orig", "repl", ft1, "", ft2) produces.  Its replacement
 * offset still gets a relocation, but the label it points at is the end of the
 * previous replacement, which is also where the *next* one begins -- here,
 * neighbor()'s.  The value is meaningless; it is only ever used with a length
 * of zero.
 *
 * struct alt_instr is written out by hand so the fixture builds without kernel
 * headers: s32 instr_offset, s32 repl_offset, u32 ft_flags, u8 instrlen,
 * u8 replacementlen.  The section carries an entsize because klp diff needs
 * either that or an ANNOTATE_DATA_SPECIAL annotation to find entry boundaries.
 *
 * The replacement labels are global so the relocations name them rather than
 * .altinstr_replacement plus an addend.
 */

static const char __modinfo[]
	__attribute__((section(".modinfo"), used, aligned(1))) = "\0name=vmlinux";

extern int neighbor_only(int x);

int target(int x)
{
	asm volatile(
		"661:	nop\n\t"
		".pushsection .altinstr_replacement, \"ax\"\n\t"
		".globl target_repl\n\t"
		"target_repl:\n\t"
		"	nop\n\t"
		"target_repl_end:\n\t"
		".popsection\n\t"
		".pushsection .altinstructions, \"aM\", @progbits, 14\n\t"
		/* a real replacement */
		".long 661b - .\n\t"
		".long target_repl - .\n\t"
		".long 0\n\t"
		".byte 1\n\t"
		".byte target_repl_end - target_repl\n\t"
		/* an empty one, pointing at neighbor()'s replacement */
		".long 661b - .\n\t"
		".long neighbor_repl - .\n\t"
		".long 0\n\t"
		".byte 1\n\t"
		".byte 0\n\t"
		".popsection\n\t");
#ifdef PATCHED
	return x + 2;
#else
	return x + 1;
#endif
}

/*
 * Unrelated, unpatched, and referencing a symbol nothing else does, so that
 * dragging its replacement in is visible.
 */
int neighbor(int x)
{
	asm volatile(
		"771:	nop\n\t"
		".pushsection .altinstr_replacement, \"ax\"\n\t"
		".globl neighbor_repl\n\t"
		"neighbor_repl:\n\t"
		"	call neighbor_only\n\t"
		"neighbor_repl_end:\n\t"
		".popsection\n\t"
		".pushsection .altinstructions, \"aM\", @progbits, 14\n\t"
		".long 771b - .\n\t"
		".long neighbor_repl - .\n\t"
		".long 0\n\t"
		".byte 1\n\t"
		".byte neighbor_repl_end - neighbor_repl\n\t"
		".popsection\n\t");
	return x;
}
