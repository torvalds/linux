/*	$NetBSD: x86emu.h,v 1.1 2007/12/01 20:14:10 joerg Exp $	*/
/*	$OpenBSD: x86emu.h,v 1.3 2009/06/06 03:45:05 matthieu Exp $ */

/****************************************************************************
*
*  Realmode X86 Emulator Library
*
*  Copyright (C) 1996-1999 SciTech Software, Inc.
*  Copyright (C) David Mosberger-Tang
*  Copyright (C) 1999 Egbert Eich
*  Copyright (C) 2007 Joerg Sonnenberger
*
*  ========================================================================
*
*  Permission to use, copy, modify, distribute, and sell this software and
*  its documentation for any purpose is hereby granted without fee,
*  provided that the above copyright notice appear in all copies and that
*  both that copyright notice and this permission notice appear in
*  supporting documentation, and that the name of the authors not be used
*  in advertising or publicity pertaining to distribution of the software
*  without specific, written prior permission.  The authors makes no
*  representations about the suitability of this software for any purpose.
*  It is provided "as is" without express or implied warranty.
*
*  THE AUTHORS DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
*  INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
*  EVENT SHALL THE AUTHORS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
*  CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF
*  USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
*  OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
*  PERFORMANCE OF THIS SOFTWARE.
*
****************************************************************************/

#ifndef __X86EMU_X86EMU_H
#define __X86EMU_X86EMU_H

#include <sys/types.h>
#include <sys/endian.h>

#ifdef _KERNEL
#include <sys/systm.h>
#else
#include <setjmp.h>
#endif

/*
 * General EAX, EBX, ECX, EDX type registers.  Note that for
 * portability, and speed, the issue of byte swapping is not addressed
 * in the registers.  All registers are stored in the default format
 * available on the host machine.  The only critical issue is that the
 * registers should line up EXACTLY in the same manner as they do in
 * the 386.  That is:
 *
 * EAX & 0xff  === AL
 * EAX & 0xffff == AX
 *
 * etc.  The result is that alot of the calculations can then be
 * done using the native instruction set fully.
 */

#ifdef	__BIG_ENDIAN__

struct x86emu_register32 {
	uint32_t e_reg;
};

struct x86emu_register16 {
	uint16_t filler0;
	uint16_t x_reg;
};

struct x86emu_register8 {
	uint8_t filler0, filler1;
	uint8_t h_reg, l_reg;
};

#else /* !__BIG_ENDIAN__ */

struct x86emu_register32 {
	uint32_t e_reg;
};

struct x86emu_register16 {
	uint16_t x_reg;
};

struct x86emu_register8 {
	uint8_t l_reg, h_reg;
};

#endif /* BIG_ENDIAN */

union x86emu_register {
	struct x86emu_register32	I32_reg;
	struct x86emu_register16	I16_reg;
	struct x86emu_register8		I8_reg;
};

struct x86emu_regs {
	uint16_t		register_cs;
	uint16_t		register_ds;
	uint16_t		register_es;
	uint16_t		register_fs;
	uint16_t		register_gs;
	uint16_t		register_ss;
	uint32_t		register_flags;
	union x86emu_register	register_a;
	union x86emu_register	register_b;
	union x86emu_register	register_c;
	union x86emu_register	register_d;

	union x86emu_register	register_sp;
	union x86emu_register	register_bp;
	union x86emu_register	register_si;
	union x86emu_register	register_di;
	union x86emu_register	register_ip;

	/*
	 * MODE contains information on:
	 *  REPE prefix             2 bits  repe,repne
	 *  SEGMENT overrides       5 bits  normal,DS,SS,CS,ES
	 *  Delayed flag set        3 bits  (zero, signed, parity)
	 *  reserved                6 bits
	 *  interrupt #             8 bits  instruction raised interrupt
	 *  BIOS video segregs      4 bits  
	 *  Interrupt Pending       1 bits  
	 *  Extern interrupt        1 bits
	 *  Halted                  1 bits
	 */
	uint32_t		mode;
	volatile int		intr;   /* mask of pending interrupts */
	uint8_t			intno;
	uint8_t			__pad[3];
};

struct x86emu {
	char			*mem_base;
	size_t			mem_size;
	void        		*sys_private;
	struct x86emu_regs	x86;

#ifdef _KERNEL
	label_t		exec_state;
#else
	jmp_buf		exec_state;
#endif

	uint64_t	cur_cycles;

	unsigned int	cur_mod:2;
	unsigned int	cur_rl:3;
	unsigned int	cur_rh:3;
	uint32_t	cur_offset;

	uint8_t  	(*emu_rdb)(struct x86emu *, uint32_t addr);
	uint16_t 	(*emu_rdw)(struct x86emu *, uint32_t addr);
	uint32_t 	(*emu_rdl)(struct x86emu *, uint32_t addr);
	void		(*emu_wrb)(struct x86emu *, uint32_t addr,uint8_t val);
	void		(*emu_wrw)(struct x86emu *, uint32_t addr, uint16_t val);
	void		(*emu_wrl)(struct x86emu *, uint32_t addr, uint32_t val);

	uint8_t  	(*emu_inb)(struct x86emu *, uint16_t addr);
	uint16_t 	(*emu_inw)(struct x86emu *, uint16_t addr);
	uint32_t 	(*emu_inl)(struct x86emu *, uint16_t addr);
	void		(*emu_outb)(struct x86emu *, uint16_t addr, uint8_t val);
	void		(*emu_outw)(struct x86emu *, uint16_t addr, uint16_t val);
	void		(*emu_outl)(struct x86emu *, uint16_t addr, uint32_t val);

	void 		(*_x86emu_intrTab[256])(struct x86emu *, int);
};

__BEGIN_DECLS

void	x86emu_init_default(struct x86emu *);

/* decode.c */

void 	x86emu_exec(struct x86emu *);
void	x86emu_exec_call(struct x86emu *, uint16_t, uint16_t);
void	x86emu_exec_intr(struct x86emu *, uint8_t);
void 	x86emu_halt_sys(struct x86emu *) __dead;

__END_DECLS

#endif /* __X86EMU_X86EMU_H */
