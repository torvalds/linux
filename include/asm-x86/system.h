#ifndef _ASM_X86_SYSTEM_H_
#define _ASM_X86_SYSTEM_H_

#include <asm/asm.h>

#ifdef CONFIG_X86_32
# include "system_32.h"
#else
# include "system_64.h"
#endif

#ifdef __KERNEL__
#define _set_base(addr, base) do { unsigned long __pr; \
__asm__ __volatile__ ("movw %%dx,%1\n\t" \
	"rorl $16,%%edx\n\t" \
	"movb %%dl,%2\n\t" \
	"movb %%dh,%3" \
	:"=&d" (__pr) \
	:"m" (*((addr)+2)), \
	 "m" (*((addr)+4)), \
	 "m" (*((addr)+7)), \
	 "0" (base) \
	); } while (0)

#define _set_limit(addr, limit) do { unsigned long __lr; \
__asm__ __volatile__ ("movw %%dx,%1\n\t" \
	"rorl $16,%%edx\n\t" \
	"movb %2,%%dh\n\t" \
	"andb $0xf0,%%dh\n\t" \
	"orb %%dh,%%dl\n\t" \
	"movb %%dl,%2" \
	:"=&d" (__lr) \
	:"m" (*(addr)), \
	 "m" (*((addr)+6)), \
	 "0" (limit) \
	); } while (0)

#define set_base(ldt, base) _set_base(((char *)&(ldt)) , (base))
#define set_limit(ldt, limit) _set_limit(((char *)&(ldt)) , ((limit)-1))

/*
 * Load a segment. Fall back on loading the zero
 * segment if something goes wrong..
 */
#define loadsegment(seg, value)			\
	asm volatile("\n"			\
		"1:\t"				\
		"movl %k0,%%" #seg "\n"		\
		"2:\n"				\
		".section .fixup,\"ax\"\n"	\
		"3:\t"				\
		"movl %k1, %%" #seg "\n\t"	\
		"jmp 2b\n"			\
		".previous\n"			\
		".section __ex_table,\"a\"\n\t"	\
		_ASM_ALIGN "\n\t"		\
		_ASM_PTR " 1b,3b\n"		\
		".previous"			\
		: :"r" (value), "r" (0))


/*
 * Save a segment register away
 */
#define savesegment(seg, value) \
	asm volatile("mov %%" #seg ",%0":"=rm" (value))

static inline unsigned long get_limit(unsigned long segment)
{
	unsigned long __limit;
	__asm__("lsll %1,%0"
		:"=r" (__limit):"r" (segment));
	return __limit+1;
}
#endif /* __KERNEL__ */

static inline void clflush(void *__p)
{
	asm volatile("clflush %0" : "+m" (*(char __force *)__p));
}

#define nop() __asm__ __volatile__ ("nop")

void disable_hlt(void);
void enable_hlt(void);

extern int es7000_plat;
void cpu_idle_wait(void);

extern unsigned long arch_align_stack(unsigned long sp);
extern void free_init_pages(char *what, unsigned long begin, unsigned long end);

void default_idle(void);

#endif
