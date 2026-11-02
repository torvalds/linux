// SPDX-License-Identifier: GPL-2.0
/*
 * An indirect call, which under kCFI is preceded by a type check and a trap.
 *
 * Clang emits a __cfi_<func> prefix symbol carrying the type hash ahead of
 * every address-taken function, and records the trap site in .kcfi_traps.
 * Both belong to the function and both have to come with it into a patch.
 *
 * Needs -fsanitize=kcfi, which only Clang has.
 */

static const char __modinfo[]
	__attribute__((section(".modinfo"), used, aligned(1))) = "\0name=vmlinux";

static int impl_a(int x)
{
	return x + 1;
}

static int impl_b(int x)
{
	return x * 2;
}

__attribute__((noinline)) int (*pick(int x))(int)
{
	return (x & 1) ? impl_a : impl_b;
}

int target(int x)
{
	int (*fn)(int arg) = pick(x);

#ifdef PATCHED
	return fn(x) + 2;
#else
	return fn(x) + 1;
#endif
}
