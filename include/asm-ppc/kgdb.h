/*
 * kgdb.h: Defines and declarations for serial line source level
 *         remote debugging of the Linux kernel using gdb.
 *
 * PPC Mods (C) 1998 Michael Tesch (tesch@cs.wisc.edu)
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */
#ifdef __KERNEL__
#ifndef _PPC_KGDB_H
#define _PPC_KGDB_H

#ifndef __ASSEMBLY__

/* Things specific to the gen550 backend. */
struct uart_port;

extern void gen550_progress(char *, unsigned short);
extern void gen550_kgdb_map_scc(void);
extern void gen550_init(int, struct uart_port *);

/* Things specific to the pmac backend. */
extern void zs_kgdb_hook(int tty_num);

/* To init the kgdb engine. (called by serial hook)*/
extern void set_debug_traps(void);

/* To enter the debugger explicitly. */
extern void breakpoint(void);

/* For taking exceptions
 * these are defined in traps.c
 */
extern int (*debugger)(struct pt_regs *regs);
extern int (*debugger_bpt)(struct pt_regs *regs);
extern int (*debugger_sstep)(struct pt_regs *regs);
extern int (*debugger_iabr_match)(struct pt_regs *regs);
extern int (*debugger_dabr_match)(struct pt_regs *regs);
extern void (*debugger_fault_handler)(struct pt_regs *regs);

/* What we bring to the party */
int kgdb_bpt(struct pt_regs *regs);
int kgdb_sstep(struct pt_regs *regs);
void kgdb(struct pt_regs *regs);
int kgdb_iabr_match(struct pt_regs *regs);
int kgdb_dabr_match(struct pt_regs *regs);

/*
 * external low-level support routines (ie macserial.c)
 */
extern void kgdb_interruptible(int); /* control interrupts from serial */
extern void putDebugChar(char);   /* write a single character      */
extern char getDebugChar(void);   /* read and return a single char */

#endif /* !(__ASSEMBLY__) */
#endif /* !(_PPC_KGDB_H) */
#endif /* __KERNEL__ */
