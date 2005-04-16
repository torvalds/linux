/* gdb-stub.h: FRV GDB stub
 *
 * Copyright (C) 2003 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 * - Derived from asm-mips/gdb-stub.h (c) 1995 Andreas Busse
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#ifndef __ASM_GDB_STUB_H
#define __ASM_GDB_STUB_H

#undef GDBSTUB_DEBUG_PROTOCOL

#include <asm/ptrace.h>

/*
 * important register numbers in GDB protocol
 * - GR0,  GR1,  GR2,  GR3,  GR4,  GR5,  GR6,  GR7,
 * - GR8,  GR9,  GR10, GR11, GR12, GR13, GR14, GR15,
 * - GR16, GR17, GR18, GR19, GR20, GR21, GR22, GR23,
 * - GR24, GR25, GR26, GR27, GR28, GR29, GR30, GR31,
 * - GR32, GR33, GR34, GR35, GR36, GR37, GR38, GR39,
 * - GR40, GR41, GR42, GR43, GR44, GR45, GR46, GR47,
 * - GR48, GR49, GR50, GR51, GR52, GR53, GR54, GR55,
 * - GR56, GR57, GR58, GR59, GR60, GR61, GR62, GR63,
 * - FR0,  FR1,  FR2,  FR3,  FR4,  FR5,  FR6,  FR7,
 * - FR8,  FR9,  FR10, FR11, FR12, FR13, FR14, FR15,
 * - FR16, FR17, FR18, FR19, FR20, FR21, FR22, FR23,
 * - FR24, FR25, FR26, FR27, FR28, FR29, FR30, FR31,
 * - FR32, FR33, FR34, FR35, FR36, FR37, FR38, FR39,
 * - FR40, FR41, FR42, FR43, FR44, FR45, FR46, FR47,
 * - FR48, FR49, FR50, FR51, FR52, FR53, FR54, FR55,
 * - FR56, FR57, FR58, FR59, FR60, FR61, FR62, FR63,
 * - PC, PSR, CCR, CCCR,
 * - _X132, _X133, _X134
 * - TBR, BRR, DBAR0, DBAR1, DBAR2, DBAR3,
 * - SCR0, SCR1, SCR2, SCR3,
 * - LR, LCR,
 * - IACC0H, IACC0L,
 * - FSR0,
 * - ACC0, ACC1, ACC2, ACC3, ACC4, ACC5, ACC6, ACC7,
 * - ACCG0123, ACCG4567,
 * - MSR0, MSR1,
 * - GNER0, GNER1,
 * - FNER0, FNER1,
 */
#define GDB_REG_GR(N)	(N)
#define GDB_REG_FR(N)	(64+(N))
#define GDB_REG_PC	128
#define GDB_REG_PSR	129
#define GDB_REG_CCR	130
#define GDB_REG_CCCR	131
#define GDB_REG_TBR	135
#define GDB_REG_BRR	136
#define GDB_REG_DBAR(N)	(137+(N))
#define GDB_REG_SCR(N)	(141+(N))
#define GDB_REG_LR	145
#define GDB_REG_LCR	146
#define GDB_REG_FSR0	149
#define GDB_REG_ACC(N)	(150+(N))
#define GDB_REG_ACCG(N)	(158+(N)/4)
#define GDB_REG_MSR(N)	(160+(N))
#define GDB_REG_GNER(N)	(162+(N))
#define GDB_REG_FNER(N)	(164+(N))

#define GDB_REG_SP	GDB_REG_GR(1)
#define GDB_REG_FP	GDB_REG_GR(2)

#ifndef _LANGUAGE_ASSEMBLY

/*
 * Prototypes
 */
extern void show_registers_only(struct pt_regs *regs);

extern void gdbstub_init(void);
extern void gdbstub(int type);
extern void gdbstub_exit(int status);

extern void gdbstub_io_init(void);
extern void gdbstub_set_baud(unsigned baud);
extern int gdbstub_rx_char(unsigned char *_ch, int nonblock);
extern void gdbstub_tx_char(unsigned char ch);
extern void gdbstub_tx_flush(void);
extern void gdbstub_do_rx(void);

extern asmlinkage void __debug_stub_init_break(void);
extern asmlinkage void __break_hijack_kernel_event(void);
extern asmlinkage void start_kernel(void);

extern asmlinkage void gdbstub_rx_handler(void);
extern asmlinkage void gdbstub_rx_irq(void);
extern asmlinkage void gdbstub_intercept(void);

extern uint32_t __entry_usertrap_table[];
extern uint32_t __entry_kerneltrap_table[];

extern volatile u8	gdbstub_rx_buffer[PAGE_SIZE];
extern volatile u32	gdbstub_rx_inp;
extern volatile u32	gdbstub_rx_outp;
extern volatile u8	gdbstub_rx_overflow;
extern u8		gdbstub_rx_unget;

extern void gdbstub_printk(const char *fmt, ...);
extern void debug_to_serial(const char *p, int n);
extern void console_set_baud(unsigned baud);

#ifdef GDBSTUB_DEBUG_PROTOCOL
#define gdbstub_proto(FMT,...) gdbstub_printk(FMT,##__VA_ARGS__)
#else
#define gdbstub_proto(FMT,...) ({ 0; })
#endif

#endif /* _LANGUAGE_ASSEMBLY */
#endif /* __ASM_GDB_STUB_H */
