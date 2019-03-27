/*-
 * Copyright (c) 2009 Alex Keda <admin@lissyara.su>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */
/*
 * x86 registers were borrowed from x86emu.h x86emu_regs.h
 * for compatibility.
 */

#ifndef _X86BIOS_H_
#define	_X86BIOS_H_

#include <sys/endian.h>
#include <sys/systm.h>
#include <sys/types.h>

#ifdef	__BIG_ENDIAN__

struct x86_register32 {
	uint32_t		e_reg;
};

struct x86_register16 {
	uint16_t		filler0;
	uint16_t		x_reg;
};

struct x86_register8 {
	uint8_t			filler0;
	uint8_t			filler1;
	uint8_t			h_reg;
	uint8_t			l_reg;
};

#else /* !__BIG_ENDIAN__ */

struct x86_register32 {
	uint32_t		e_reg;
};

struct x86_register16 {
	uint16_t		x_reg;
};

struct x86_register8 {
	uint8_t			l_reg;
	uint8_t			h_reg;
};

#endif /* __BIG_ENDIAN__ */

union x86_register {
	struct x86_register32	I32_reg;
	struct x86_register16	I16_reg;
	struct x86_register8	I8_reg;
};

struct x86regs {
	uint16_t		_pad0;		/* CS */
	uint16_t		_pad1;		/* DS */
	uint16_t		register_es;
	uint16_t		register_fs;
	uint16_t		register_gs;
	uint16_t		_pad2;		/* SS */
	uint32_t		register_flags;
	union x86_register	register_a;
	union x86_register	register_b;
	union x86_register	register_c;
	union x86_register	register_d;

	union x86_register	_pad3;		/* SP */
	union x86_register	register_bp;
	union x86_register	register_si;
	union x86_register	register_di;
};

typedef struct x86regs	x86regs_t;

/* 8 bit registers */
#define	R_AH		register_a.I8_reg.h_reg
#define	R_AL		register_a.I8_reg.l_reg
#define	R_BH		register_b.I8_reg.h_reg
#define	R_BL		register_b.I8_reg.l_reg
#define	R_CH		register_c.I8_reg.h_reg
#define	R_CL		register_c.I8_reg.l_reg
#define	R_DH		register_d.I8_reg.h_reg
#define	R_DL		register_d.I8_reg.l_reg

/* 16 bit registers */
#define	R_AX		register_a.I16_reg.x_reg
#define	R_BX		register_b.I16_reg.x_reg
#define	R_CX		register_c.I16_reg.x_reg
#define	R_DX		register_d.I16_reg.x_reg

/* 32 bit extended registers */
#define	R_EAX		register_a.I32_reg.e_reg
#define	R_EBX		register_b.I32_reg.e_reg
#define	R_ECX		register_c.I32_reg.e_reg
#define	R_EDX		register_d.I32_reg.e_reg

/* special registers */
#define	R_BP		register_bp.I16_reg.x_reg
#define	R_SI		register_si.I16_reg.x_reg
#define	R_DI		register_di.I16_reg.x_reg
#define	R_FLG		register_flags

/* special registers */
#define	R_EBP		register_bp.I32_reg.e_reg
#define	R_ESI		register_si.I32_reg.e_reg
#define	R_EDI		register_di.I32_reg.e_reg
#define	R_EFLG		register_flags

/* segment registers */
#define	R_ES		register_es
#define	R_FS		register_fs
#define	R_GS		register_gs

#define	X86BIOS_PHYSTOSEG(x)	(((x) >> 4) & 0xff00)
#define	X86BIOS_PHYSTOOFF(x)	((x) & 0x0fff)

__BEGIN_DECLS
void	*x86bios_alloc(uint32_t *offset, size_t size, int flags);
void	 x86bios_call(struct x86regs *regs, uint16_t seg, uint16_t off);
void	 x86bios_free(void *addr, size_t size);
uint32_t x86bios_get_intr(int intno);
void	*x86bios_get_orm(uint32_t offset);
void	 x86bios_init_regs(struct x86regs *regs);
void	 x86bios_intr(struct x86regs *regs, int intno);
int	 x86bios_match_device(uint32_t offset, device_t dev);
void	*x86bios_offset(uint32_t offset);
void	 x86bios_set_intr(int intno, uint32_t saddr);
__END_DECLS

#endif /* !_X86BIOS_H_ */
