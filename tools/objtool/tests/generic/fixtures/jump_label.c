// SPDX-License-Identifier: GPL-2.0
/*
 * Static branch in a patched function.  The jump table entry is written out by
 * hand, mirroring JUMP_TABLE_ENTRY(), so the fixture builds without kernel
 * headers.  The key is an STT_OBJECT; anything else is ignored by
 * validate_special_section_klp_reloc().
 *
 * MODNAME selects whether the key is taken to belong to vmlinux or a module.
 *
 * NEW_KEY puts the whole static branch behind PATCHED, so the patch introduces
 * one where the original had none -- a different question from patching code
 * that already has a key, because the __jump_table entry itself is new.
 *
 * KEY_NAME renames the key.  Two names are special to
 * validate_special_section_klp_reloc(): a __tracepoint_* key and the
 * __UNIQUE_ID_ddebug_* one pr_debug() generates are both unsupported in a
 * module, but are disabled with a warning rather than rejected, because the
 * kernel is full of them and refusing outright would make ordinary functions
 * unpatchable.
 *
 * STATIC_KEY makes the key file-local.  That changes the shape of the
 * relocation rather than the meaning of the code: a reference to a static lands
 * on the section symbol plus an addend, so the key has to be resolved from the
 * section before it can be recognised as a key at all.
 */

#ifndef MODNAME
#define MODNAME "vmlinux"
#endif

#ifndef KEY_NAME
#define KEY_NAME klp_test_key
#endif

static const char __modinfo[]
	__attribute__((section(".modinfo"), used, aligned(1))) = "\0name=" MODNAME;

#ifdef STATIC_KEY
static long KEY_NAME;
#else
long KEY_NAME;
#endif

int target(int x)
{
	int r = x;

#if defined(NEW_KEY) && !defined(PATCHED)
	/* The original has no static branch at all. */
	return r + 1;
#else
	asm goto(
		"1:	nop\n\t"
		".pushsection	__jump_table, \"aw\"\n\t"
		".balign	8\n\t"
		"912:\n\t"
		".pushsection	.discard.annotate_data, \"M\", @progbits, 8\n\t"
		".long		912b - ., 1\n\t"
		".popsection\n\t"
		".long		1b - ., %l[l_yes] - .\n\t"
		".quad		%c0 - .\n\t"
		".popsection\n\t"
		: : "i" (&KEY_NAME) : : l_yes);

	r += 1;
	goto out;
l_yes:
	r += 2;
out:
#endif
#ifdef PATCHED
	return r + 100;
#else
	return r;
#endif
}
