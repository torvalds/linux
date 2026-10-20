// SPDX-License-Identifier: GPL-2.0
/*
 * A function whose position in the section changes between the two builds,
 * without the function itself changing.
 *
 * target() calls callee() twice, and a call within the same section needs no
 * relocation: the displacement is in the instruction.  It is that displacement
 * which moves, and hashing those bytes makes the checksum move with it.  Both
 * must therefore share a section, which is why the test passes
 * -fno-function-sections.
 *
 * What has to change is the distance between the two, and PATCHED changes it
 * by aligning them rather than by inserting a function between them.  Where a
 * compiler puts an added function is its own business: gcc emits these in
 * source order, so a function written between callee() and target() separates
 * them, but clang emits target() immediately before callee() whatever the
 * source says, and an added function lands ahead of both.  That moves target()
 * without moving it relative to callee(), the displacement comes out identical
 * in both builds, and the test passes without having asked anything.
 *
 * Alignment moves the functions apart on both, and moves neither function's
 * own instructions -- which is exactly the distinction under test.
 */

#ifdef PATCHED
#define MOVED	__attribute__((aligned(64)))
#else
#define MOVED
#endif

static const char __modinfo[]
	__attribute__((section(".modinfo"), used, aligned(1))) = "\0name=vmlinux";

__attribute__((noinline)) MOVED static int callee(int x)
{
	return x * 5 + 1;
}

__attribute__((noinline)) MOVED int target(int x)
{
	return callee(x) + callee(x + 1);
}
