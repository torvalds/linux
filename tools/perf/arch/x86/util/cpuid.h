/* SPDX-License-Identifier: GPL-2.0 */
#ifndef PERF_CPUID_H
#define PERF_CPUID_H 1


static inline void
cpuid(unsigned int op, unsigned int op2, unsigned int *a, unsigned int *b,
	unsigned int *c, unsigned int *d)
{
	/*
	 * Preserve %ebx/%rbx register by either placing it in %rdi or saving it
	 * on the stack - x86-64 needs to avoid the stack red zone. In PIC
	 * compilations %ebx contains the address of the global offset
	 * table. %rbx is occasionally used to address stack variables in
	 * presence of dynamic allocas.
	 */
	asm(
#if defined(__x86_64__)
		"mov %%rbx, %%rdi\n"
		"cpuid\n"
		"xchg %%rdi, %%rbx\n"
#else
		"pushl %%ebx\n"
		"cpuid\n"
		"movl %%ebx, %%edi\n"
		"popl %%ebx\n"
#endif
		: "=a"(*a), "=D"(*b), "=c"(*c), "=d"(*d)
		: "a"(op), "2"(op2));
}

void get_cpuid_0(char *vendor, unsigned int *lvl);

#endif
