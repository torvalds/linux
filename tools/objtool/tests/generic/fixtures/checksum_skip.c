// SPDX-License-Identifier: GPL-2.0
/*
 * Symbols calculate_checksums() must not give an entry of their own.
 *
 * Three kinds are skipped, for two different reasons:
 *
 *   zero-length   there is nothing to hash, and an entry keyed on the
 *                 symbol's address would collide with whatever really lives
 *                 there.
 *   alias         a second name for an address already checksummed.
 *   cold part     hashed as part of its parent, which func_for_each_insn()
 *                 walks into, so a separate entry would double-count it.
 *
 * An entry per address is the invariant: .discard.sym_checksum is looked up by
 * the address a relocation points at, so two entries for one address make the
 * lookup ambiguous and one of the two checksums unreachable.
 */

static const char __modinfo[]
	__attribute__((section(".modinfo"), used, aligned(1))) = "\0name=vmlinux";

/* Zero-length: an object symbol of size 0, in a section of its own. */
extern char empty_marker[];
__asm__(".pushsection .data.empty_marker,\"aw\",@progbits\n"
	".globl empty_marker\n"
	".type empty_marker, @object\n"
	"empty_marker:\n"
	".size empty_marker, 0\n"
	".popsection\n");

int real_function(int x);
int real_function(int x)
{
#ifdef PATCHED
	return x + 2;
#else
	return x + 1;
#endif
}

/* Alias: a second name for real_function's address. */
int alias_function(int x) __attribute__((alias("real_function")));

int target(int x)
{
	return real_function(x) + alias_function(x) + (int)(long)empty_marker;
}
