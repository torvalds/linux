/* SPDX-License-Identifier: GPL-2.0 */

#if __alpha__
register unsigned long sp asm("$30");
#elif __arm__ || __aarch64__ || __csky__ || __m68k__ || __mips__ || __riscv
register unsigned long sp asm("sp");
#elif __i386__
register unsigned long sp asm("esp");
#elif __loongarch64
register unsigned long sp asm("$sp");
#elif __powerpc__
register unsigned long sp asm("r1");
#elif __s390x__
register unsigned long sp asm("%15");
#elif __sh__
register unsigned long sp asm("r15");
#elif __x86_64__
register unsigned long sp asm("rsp");
#elif __XTENSA__
register unsigned long sp asm("a1");
#else
#error "implement current_stack_pointer equivalent"
#endif
