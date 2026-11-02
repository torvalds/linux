// SPDX-License-Identifier: GPL-2.0
/*
 * Two constructs which appear all over the kernel and must not upset klp
 * checksum or klp diff.
 *
 * An absolute symbol (SHN_ABS) has no section, so anything walking sym->sec
 * without checking dereferences NULL.  The kernel makes them with linker
 * scripts and with .set in asm; VDSO and the fixed-address per-cpu bases are
 * the usual sources.
 *
 * __ADDRESSABLE() emits a pointer into .discard.addressable purely to keep a
 * symbol referenced.  It is discarded at link time and means nothing to a
 * livepatch, but the pointer is a relocation like any other and has to survive
 * being looked at.
 *
 * Neither is the subject of the patch; the point is that their presence does
 * not disturb the function that is.
 */

static const char __modinfo[]
	__attribute__((section(".modinfo"), used, aligned(1))) = "\0name=vmlinux";

/* SHN_ABS, referenced from code. */
extern char abs_sym[];
__asm__(".globl abs_sym\n"
	".set abs_sym, 0x1234\n");

int helper(int x);
int helper(int x) { return x + 1; }

/* The shape of __ADDRESSABLE(helper). */
__asm__(".pushsection .discard.addressable, \"aw\"\n"
	".balign 8\n"
	".quad helper\n"
	".popsection\n");

int target(int x)
{
#ifdef PATCHED
	return helper(x) + (int)(long)abs_sym + 1;
#else
	return helper(x) + (int)(long)abs_sym;
#endif
}
