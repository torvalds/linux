// SPDX-License-Identifier: GPL-2.0
/*
 * Data objects whose checksums must move for reasons the raw bytes do not
 * show.
 *
 * checksum_update_object() hashes a data symbol's length and its bytes, and
 * then walks its relocations: a reference into a string section contributes
 * the string's *contents*, and any other reference contributes the target
 * symbol's name and the adjusted addend.  So three changes that leave the
 * object's own bytes identical still have to change its checksum:
 *
 * Each variant is selected by a -D on the patched build only, so the original
 * is always the baseline:
 *
 *   WHICH_FUNC   the function pointer points somewhere else
 *   WHICH_STR    the string pointer points at a different literal
 *   STR_CONTENT  the string it points at is edited in place
 *   WHICH_SLOT   the same array, at a different index: addend only
 *   WHICH_PRIV   likewise, but a static, reached through its section symbol
 *
 * The last is the interesting one.  Nothing in the pointer changes -- same
 * section, same offset -- so a checksum that hashed only the relocation and
 * not what it referred to would call the object unchanged, and the patched
 * kernel would keep the old string.
 */

static const char __modinfo[]
	__attribute__((section(".modinfo"), used, aligned(1))) = "\0name=vmlinux";

int callee_a(int x);
int callee_b(int x);
int callee_a(int x) { return x + 1; }
int callee_b(int x) { return x + 2; }

/*
 * String *literals*, not named arrays.  The contents-hashing path keys on
 * SHF_STRINGS, which the compiler sets on the mergeable .rodata.str1.1 a
 * literal lands in and not on a named char[] given a section of its own.  A
 * fixture using the latter exercises the ordinary name-and-addend path and
 * reports nothing when the text changes.
 */
#if defined(PATCHED) && defined(STR_CONTENT)
#define MESSAGE "edited"
#else
#define MESSAGE "original"
#endif

/* A plain data object: only its own bytes decide the checksum. */
#if defined(PATCHED) && defined(PLAIN_VALUE)
int plain = 43;
#else
int plain = 42;
#endif

/*
 * A .bss object, where length is the only thing there is to hash: the section
 * has no data, so the bytes are skipped and only sym->len distinguishes this
 * from an object of another size.  An initialised array would not isolate it
 * -- growing one changes the hashed bytes as well.
 */
#if defined(PATCHED) && defined(LONGER)
char sized[4];
#else
char sized[2];
#endif

/*
 * A reference into the middle of an array: same target symbol, different
 * addend.  Nothing else in the object changes, so this is the only way to see
 * whether the addend is hashed at all.
 */
int slots[4];

/*
 * A file-local array.  A reference to a static lands on its section symbol
 * plus an offset, so the hash has to resolve that back to the underlying
 * object before it has a name to hash at all -- a different code path from the
 * global above, and one that silently contributes nothing when it fails.
 */
static int priv_slots[4];

struct desc {
	int (*fn)(int arg);
	const char *str;
	int *slot;
	int *priv;
};

const struct desc descriptor = {
#if defined(PATCHED) && defined(WHICH_FUNC)
	.fn = callee_b,
#else
	.fn = callee_a,
#endif
#if defined(PATCHED) && defined(WHICH_STR)
	.str = "a different literal",
#else
	.str = MESSAGE,
#endif
#if defined(PATCHED) && defined(WHICH_SLOT)
	.slot = &slots[2],
#else
	.slot = &slots[1],
#endif
#if defined(PATCHED) && defined(WHICH_PRIV)
	.priv = &priv_slots[3],
#else
	.priv = &priv_slots[1],
#endif
};

int target(int x)
{
	return descriptor.fn(x) + plain + sized[0] + (int)descriptor.str[0] +
	       *descriptor.slot + *descriptor.priv;
}
