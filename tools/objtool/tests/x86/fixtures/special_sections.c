// SPDX-License-Identifier: GPL-2.0
/*
 * A special section entry belonging to a patched function.  SPECIAL_SEC picks
 * which section, since klp diff treats eight of them alike and each needs
 * extracting for the patched function and no other.
 *
 * The entry is written out by hand so the fixture builds without kernel
 * headers.  Only the leading relocation matters to klp diff; the rest is
 * padded to the section's real entry size, because the entries have to be the
 * right length for the boundaries between them to fall in the right places.
 *
 * SPECIAL_RELOCS covers __ex_table, whose entries relocate both the faulting
 * instruction and its fixup; objtool rejects one with only the first.
 */

#ifndef SPECIAL_SEC
#define SPECIAL_SEC "__bug_table"
#endif
#ifndef SPECIAL_ENTSIZE
#define SPECIAL_ENTSIZE 12
#endif
#ifndef SPECIAL_RELOCS
#define SPECIAL_RELOCS 1
#endif

#define STR_(x) #x
#define STR(x) STR_(x)

static const char __modinfo[]
	__attribute__((section(".modinfo"), used, aligned(1))) = "\0name=vmlinux";

/*
 * other() gets an entry of its own, so that "the untouched function's entry
 * was not dragged in" is a question the section can actually answer.  With
 * only target() contributing, there is nothing for klp diff to leave behind
 * and the negative assertion holds however the extraction behaves.
 */
int other(int x)
{
	asm volatile(
		"3:	nop\n\t"
		"4:\n\t"
		".pushsection " SPECIAL_SEC ", \"aM\", @progbits, "
			STR(SPECIAL_ENTSIZE) "\n\t"
		".long 3b - .\n\t"
#if SPECIAL_RELOCS > 1
		".long 4b - .\n\t"
		".fill " STR(SPECIAL_ENTSIZE) " - 8, 1, 0\n\t"
#else
		".fill " STR(SPECIAL_ENTSIZE) " - 4, 1, 0\n\t"
#endif
		".popsection\n\t");

	return x + 9;
}

int target(int x)
{
	asm volatile(
		"1:	nop\n\t"
		"2:\n\t"
		".pushsection " SPECIAL_SEC ", \"aM\", @progbits, "
			STR(SPECIAL_ENTSIZE) "\n\t"
		".long 1b - .\n\t"
#if SPECIAL_RELOCS > 1
		".long 2b - .\n\t"
		".fill " STR(SPECIAL_ENTSIZE) " - 8, 1, 0\n\t"
#else
		".fill " STR(SPECIAL_ENTSIZE) " - 4, 1, 0\n\t"
#endif
		".popsection\n\t");
#ifdef PATCHED
	return x + 2;
#else
	return x + 1;
#endif
}
