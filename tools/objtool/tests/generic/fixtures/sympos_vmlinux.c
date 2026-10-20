// SPDX-License-Identifier: GPL-2.0
/*
 * Two translation units with a same-named static, placed so that the linker
 * puts them in the opposite order to the one they appear in the symbol table.
 *
 * VARSEC selects the section the static lands in.  Linking with
 * --sort-section=name then orders them alphabetically rather than by object
 * order, so the first symbol in the symbol table ends up at the *higher*
 * address.  That is the whole point: counting symbol table order and reading
 * the linked image's addresses now give different answers, which is what makes
 * it possible to tell which one klp diff used.
 *
 * Only use_a is patched, so exactly one sympos is emitted and there is nothing
 * to attribute.
 */

#ifndef FUNC_NAME
#define FUNC_NAME use_a
#endif
#ifndef VARSEC
#define VARSEC ".data.mmm"
#endif

#ifndef NO_MODINFO
static const char __modinfo[]
	__attribute__((section(".modinfo"), used, aligned(1))) = "\0name=vmlinux";
#endif

/* volatile so it survives as an STT_OBJECT rather than being folded away */
static volatile int dup_counter __attribute__((section(VARSEC))) = 1;

int FUNC_NAME(int x)
{
	dup_counter += x;
#ifdef PATCHED
	return dup_counter + 1;
#else
	return dup_counter;
#endif
}
