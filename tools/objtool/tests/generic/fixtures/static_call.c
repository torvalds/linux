// SPDX-License-Identifier: GPL-2.0
/*
 * Static call site in a patched function, laid out by hand as for
 * jump_label.c.  MODNAME selects whether the key belongs to vmlinux or a
 * module.
 *
 * objtool's check pass would emit the site, and klp-write-tests.txt says to
 * let it.  Not here: it does not emit the ANNOTATE_DATA_SPECIAL describing
 * the entry boundaries -- in the kernel that comes from the static_call
 * macros -- and NO_ANNOTATE below has to be able to take it away.  A fixture
 * which varies the annotation has to write the entry that goes with it.
 *
 * NO_ANNOTATE drops the ANNOTATE_DATA_SPECIAL block from the patched build,
 * leaving .static_call_sites with no annotation to describe its entry
 * boundaries.  The section carries no entsize either, so klp diff has to fall
 * back on the annotations it can still see -- and when the patched object is
 * the only one that lost them, the two sides disagree about how the section is
 * divided up.
 *
 * NEW_CALL puts the call site behind PATCHED, so the patch introduces one
 * where the original had none.  The .static_call_sites entry is then new, with
 * nothing in the original to correlate it against.
 */

#ifndef MODNAME
#define MODNAME "vmlinux"
#endif

static const char __modinfo[]
	__attribute__((section(".modinfo"), used, aligned(1))) = "\0name=" MODNAME;

long __SCK__klp_test_call;

int target(int x)
{
#if defined(NEW_CALL) && !defined(PATCHED)
	/* The original has no static call at all. */
	return x + 1;
#else
	__asm__ volatile(
		"1:	nop\n\t"
		".pushsection	.static_call_sites, \"aw\"\n\t"
		".balign	8\n\t"
		"912:\n\t"
#if !(defined(PATCHED) && defined(NO_ANNOTATE))
		".pushsection	.discard.annotate_data, \"M\", @progbits, 8\n\t"
		".long		912b - ., 1\n\t"
		".popsection\n\t"
#endif
		".long		1b - ., %c0 - .\n\t"
		".popsection\n\t"
		:: "i" (&__SCK__klp_test_call));
#endif
#ifdef PATCHED
	return x + 2;
#else
	return x + 1;
#endif
}
