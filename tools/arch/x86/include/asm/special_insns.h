/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _TOOLS_ASM_X86_SPECIAL_INSNS_H
#define _TOOLS_ASM_X86_SPECIAL_INSNS_H

/* The dst parameter must be 64-bytes aligned */
static inline void movdir64b(void *dst, const void *src)
{
	const struct { char _[64]; } *__src = src;
	struct { char _[64]; } *__dst = dst;

	/*
	 * MOVDIR64B %(rdx), rax.
	 *
	 * Both __src and __dst must be memory constraints in order to tell the
	 * compiler that no other memory accesses should be reordered around
	 * this one.
	 *
	 * Also, both must be supplied as lvalues because this tells
	 * the compiler what the object is (its size) the instruction accesses.
	 * I.e., not the pointers but what they point to, thus the deref'ing '*'.
	 */
	asm volatile(".byte 0x66, 0x0f, 0x38, 0xf8, 0x02"
		     : "+m" (*__dst)
		     :  "m" (*__src), "a" (__dst), "d" (__src));
}

#endif /* _TOOLS_ASM_X86_SPECIAL_INSNS_H */
