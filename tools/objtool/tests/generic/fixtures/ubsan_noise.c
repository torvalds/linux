// SPDX-License-Identifier: GPL-2.0
/*
 * A translation unit built with UBSAN, where only one of two functions is
 * patched.
 *
 * Every instrumented operation gets a per-callsite metadata object in an
 * anonymous data section -- .data..Lubsan_data and .data..Lubsan_type from
 * GCC, .data..L__unnamed_ from Clang -- and a call to a __ubsan_handle_*
 * routine.  The names are compiler-generated and carry no meaning across a
 * rebuild, so klp diff has to treat those sections as uncorrelated rather than
 * pairing them up by name.
 *
 * untouched() is byte-identical in both builds and exists to catch the false
 * positive: if the metadata were correlated by name, its shifts would look
 * changed and it would be dragged into the patch.
 *
 * The shifts are what draw the instrumentation.  A bounds check would do as
 * well but neither compiler emits one for an index it can prove in range.
 */

static const char __modinfo[]
	__attribute__((section(".modinfo"), used, aligned(1))) = "\0name=vmlinux";

int shift_by(int v, int n);

int untouched(int v, int n)
{
	int s = 0;

	s += v << (n & 31);
	s += v << ((n + 1) & 31);
	s += shift_by(v, n);

	return s;
}

int touched(int v, int n)
{
	int s = 0;

	s += v << (n & 31);
#ifdef PATCHED
	s += v << ((n + 3) & 31);
#else
	s += v << ((n + 2) & 31);
#endif

	return s;
}
