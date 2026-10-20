// SPDX-License-Identifier: GPL-2.0
/*
 * Instruction operands whose change must move a function's checksum even
 * though the instruction bytes themselves do not.
 *
 * checksum_update_insn() hashes the raw bytes and then, when the instruction
 * carries a relocation, what that relocation refers to: a string section
 * contributes the string's contents, anything else the target symbol's name
 * and the adjusted addend.  A reference to a static arrives as a section
 * symbol and has to be resolved back to the object first.
 *
 * The bytes are identical in every case below -- a rel32 operand is zero in
 * the object and supplied by the relocation -- so a checksum that stopped at
 * the bytes would call all of these unchanged.
 *
 * Each variant applies to the patched build only:
 *
 *   WHICH_CALL    calls a different function
 *   STR_CONTENT   passes a literal whose text was edited
 *   WHICH_SLOT    reads a different index of a global array: addend only
 *   WHICH_PRIV    the same, for a static, reached through its section symbol
 */

static const char __modinfo[]
	__attribute__((section(".modinfo"), used, aligned(1))) = "\0name=vmlinux";

int callee_a(int x);
int callee_b(int x);
int sink(const char *s);

int slots[4];

/*
 * A file-local array, plus a writer the compiler cannot see through.  Without
 * one it can prove the array is never written, folds every read to zero, and
 * emits no relocation at all -- so the reference this is here to exercise does
 * not exist.
 */
static int priv_slots[4];

void set_priv(int i, int v);
void set_priv(int i, int v)
{
	priv_slots[i] = v;
}

#if defined(PATCHED) && defined(STR_CONTENT)
#define MESSAGE "edited"
#else
#define MESSAGE "original"
#endif

int target(int x)
{
	int r;

#if defined(PATCHED) && defined(WHICH_CALL)
	r = callee_b(x);
#else
	r = callee_a(x);
#endif

	r += sink(MESSAGE);

#if defined(PATCHED) && defined(WHICH_SLOT)
	r += slots[2];
#else
	r += slots[1];
#endif

#if defined(PATCHED) && defined(WHICH_PRIV)
	r += priv_slots[3];
#else
	r += priv_slots[1];
#endif

	return r;
}
