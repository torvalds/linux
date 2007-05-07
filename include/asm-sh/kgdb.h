/*
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * Based on original code by Glenn Engel, Jim Kingdon,
 * David Grothe <dave@gcom.com>, Tigran Aivazian, <tigran@sco.com> and
 * Amit S. Kale <akale@veritas.com>
 * 
 * Super-H port based on sh-stub.c (Ben Lee and Steve Chamberlain) by
 * Henry Bell <henry.bell@st.com>
 * 
 * Header file for low-level support for remote debug using GDB. 
 *
 */

#ifndef __KGDB_H
#define __KGDB_H

#include <asm/ptrace.h>
#include <asm/cacheflush.h>

struct console;

/* Same as pt_regs but has vbr in place of syscall_nr */
struct kgdb_regs {
        unsigned long regs[16];
        unsigned long pc;
        unsigned long pr;
        unsigned long sr;
        unsigned long gbr;
        unsigned long mach;
        unsigned long macl;
        unsigned long vbr;
};

/* State info */
extern char kgdb_in_gdb_mode;
extern int kgdb_done_init;
extern int kgdb_enabled;
extern int kgdb_nofault;	/* Ignore bus errors (in gdb mem access) */
extern int kgdb_halt;		/* Execute initial breakpoint at startup */
extern char in_nmi;		/* Debounce flag to prevent NMI reentry*/

/* SCI */
extern int kgdb_portnum;
extern int kgdb_baud;
extern char kgdb_parity;
extern char kgdb_bits;

/* Init and interface stuff */
extern int kgdb_init(void);
extern int (*kgdb_getchar)(void);
extern void (*kgdb_putchar)(int);

/* Trap functions */
typedef void (kgdb_debug_hook_t)(struct pt_regs *regs);
typedef void (kgdb_bus_error_hook_t)(void);
extern kgdb_debug_hook_t  *kgdb_debug_hook;
extern kgdb_bus_error_hook_t *kgdb_bus_err_hook;

/* Console */
void kgdb_console_write(struct console *co, const char *s, unsigned count);
extern int kgdb_console_setup(struct console *, char *);

/* Prototypes for jmp fns */
#define _JBLEN 9
typedef        int jmp_buf[_JBLEN];
extern void    longjmp(jmp_buf __jmpb, int __retval);
extern int     setjmp(jmp_buf __jmpb);

/* Forced breakpoint */
#define breakpoint()					\
do {							\
	if (kgdb_enabled)				\
		__asm__ __volatile__("trapa   #0x3c");	\
} while (0)

/* KGDB should be able to flush all kernel text space */
#if defined(CONFIG_CPU_SH4)
#define kgdb_flush_icache_range(start, end) \
{									\
	__flush_purge_region((void*)(start), (int)(end) - (int)(start));\
	flush_icache_range((start), (end));				\
}
#else
#define kgdb_flush_icache_range(start, end)	do { } while (0)
#endif

/* Taken from sh-stub.c of GDB 4.18 */
static const char hexchars[] = "0123456789abcdef";

/* Get high hex bits */
static inline char highhex(const int x)
{
	return hexchars[(x >> 4) & 0xf];
}

/* Get low hex bits */
static inline char lowhex(const int x)
{
	return hexchars[x & 0xf];
}
#endif
