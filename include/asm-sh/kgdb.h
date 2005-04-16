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
extern int kgdb_console_setup(struct console *, char *);

/* Init and interface stuff */
extern int kgdb_init(void);
extern int (*kgdb_serial_setup)(void);
extern int (*kgdb_getchar)(void);
extern void (*kgdb_putchar)(int);

struct kgdb_sermap {
	char *name;
	int namelen;
	int (*setup_fn)(struct console *, char *);
	struct kgdb_sermap *next;
};
extern void kgdb_register_sermap(struct kgdb_sermap *map);
extern struct kgdb_sermap *kgdb_porttype;

/* Trap functions */
typedef void (kgdb_debug_hook_t)(struct pt_regs *regs); 
typedef void (kgdb_bus_error_hook_t)(void);
extern kgdb_debug_hook_t  *kgdb_debug_hook;
extern kgdb_bus_error_hook_t *kgdb_bus_err_hook;

extern void breakpoint(void);

/* Console */
struct console;
void kgdb_console_write(struct console *co, const char *s, unsigned count);
void kgdb_console_init(void);

/* Prototypes for jmp fns */
#define _JBLEN 9
typedef        int jmp_buf[_JBLEN];
extern void    longjmp(jmp_buf __jmpb, int __retval);
extern int     setjmp(jmp_buf __jmpb);

/* Variadic macro to print our own message to the console */
#define KGDB_PRINTK(...) printk("KGDB: " __VA_ARGS__)

/* Forced breakpoint */
#define BREAKPOINT() do {                                     \
  if (kgdb_enabled) {                                         \
    asm volatile("trapa   #0xff");                            \
  }                                                           \
} while (0)

/* KGDB should be able to flush all kernel text space */
#if defined(CONFIG_CPU_SH4)
#define kgdb_flush_icache_range(start, end) \
{									\
	extern void __flush_purge_region(void *, int);			\
	__flush_purge_region((void*)(start), (int)(end) - (int)(start));\
	flush_icache_range((start), (end));				\
}
#else
#define kgdb_flush_icache_range(start, end)	do { } while (0)
#endif

/* Kernel assert macros */
#ifdef CONFIG_KGDB_KERNEL_ASSERTS

/* Predefined conditions */
#define KA_VALID_ERRNO(errno) ((errno) > 0 && (errno) <= EMEDIUMTYPE)
#define KA_VALID_PTR_ERR(ptr) KA_VALID_ERRNO(-PTR_ERR(ptr))
#define KA_VALID_KPTR(ptr)  (!(ptr) || \
              ((void *)(ptr) >= (void *)PAGE_OFFSET &&  \
               (void *)(ptr) < ERR_PTR(-EMEDIUMTYPE)))
#define KA_VALID_PTRORERR(errptr) \
               (KA_VALID_KPTR(errptr) || KA_VALID_PTR_ERR(errptr))
#define KA_HELD_GKL()  (current->lock_depth >= 0)

/* The actual assert */
#define KGDB_ASSERT(condition, message) do {                   \
       if (!(condition) && (kgdb_enabled)) {                   \
               KGDB_PRINTK("Assertion failed at %s:%d: %s\n",  \
                                  __FILE__, __LINE__, message);\
               BREAKPOINT();                                   \
       }                                                       \
} while (0)
#else
#define KGDB_ASSERT(condition, message)
#endif

#endif
