/*	$OpenBSD: ghcb.h,v 1.6 2025/09/17 18:37:44 sf Exp $	*/

/*
 * Copyright (c) 2024, 2025 Hans-Joerg Hoexer <hshoexer@genua.de>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _MACHINE_GHCB_H_
#define _MACHINE_GHCB_H_

#ifndef _LOCORE

#include <sys/systm.h>

#include <machine/frame.h>

#define GHCB_OFFSET(m)			((m) / 8)
#define GHCB_IDX(m)			(GHCB_OFFSET((m)) / 8)
#define GHCB_BIT(m)			(GHCB_OFFSET((m)) % 8)

#define GHCB_XSS			0x140
#define GHCB_RAX			0x1F8
#define GHCB_RBX			0x318
#define GHCB_RCX			0x308
#define GHCB_RDX			0x310
#define GHCB_SW_EXITCODE		0x390
#define GHCB_SW_EXITINFO1		0x398
#define GHCB_SW_EXITINFO2		0x3A0
#define GHCB_SW_SCRATCH			0x3A8
#define GHCB_XCR0			0x3E8

#define GHCB_MAX			0xFFF

struct ghcb_sa {
	uint8_t			v_pad0[0xcb];		/* 000h-0CAh */
	uint8_t			v_cpl;			/* 0CBh */
	uint8_t			v_pad1[0x74];		/* 0CCh-13Fh */
	uint64_t		v_xss;			/* 140h */
	uint8_t			v_pad2[0x18];		/* 148h-15Fh */
	uint64_t		v_dr7;			/* 160h */
	uint8_t			v_pad3[0x10];		/* 168h-177h */
	uint64_t		v_rip;			/* 178h */
	uint8_t			v_pad4[0x58];		/* 180h-1D7h */
	uint64_t		v_rsp;			/* 1D8h */
	uint8_t			v_pad5[0x18];		/* 1E0h-1F7h */
	uint64_t		v_rax;			/* 1F8h */
	uint8_t			v_pad6[0x108];		/* 200h-307h */
	uint64_t		v_rcx;			/* 308h */
	uint64_t		v_rdx;			/* 310h */
	uint64_t		v_rbx;			/* 318h */
	uint8_t			v_pad7[0x8];		/* 320h-327h */
	uint64_t		v_rbp;			/* 328h */
	uint64_t		v_rsi;			/* 330h */
	uint64_t		v_rdi;			/* 338h */
	uint64_t		v_r8;			/* 340h */
	uint64_t		v_r9;			/* 348h */
	uint64_t		v_r10;			/* 350h */
	uint64_t		v_r11;			/* 358h */
	uint64_t		v_r12;			/* 360h */
	uint64_t		v_r13;			/* 368h */
	uint64_t		v_r14;			/* 370h */
	uint64_t		v_r15;			/* 378h */
	uint8_t			v_pad8[0x10];		/* 380h-38Fh */
	uint64_t		v_sw_exitcode;		/* 390h */
	uint64_t		v_sw_exitinfo1;		/* 398h */
	uint64_t		v_sw_exitinfo2;		/* 3a0h */
	uint64_t		v_sw_scratch;		/* 3a8h */
	uint8_t			v_pad9[0x38];		/* 3B0h-3E7h */
	uint64_t		v_xcr0;			/* 3E8h */
#define GHCB_VB_SZ	0x10
	uint8_t			valid_bitmap[GHCB_VB_SZ];
							/* 3F0h-3FFh */
	uint64_t		v_x87_state_gpa;	/* 400h */
	uint8_t			v_pad10[0x3f8];		/* 408h-7FFh */
	uint8_t			v_sharedbuf[0x7f0];	/* 800h-FEFh */
	uint8_t			v_pad11[0xa];		/* FF0h-FF9h */
	uint16_t		v_ghcb_proto_version;	/* FFAh-FFBh */
	uint32_t		v_ghcb_usage;		/* FFCh-FFFh */
};

#define GHCB_SZ8	0
#define GHCB_SZ16	1
#define GHCB_SZ32	2
#define GHCB_SZ64	3

struct ghcb_sync {
	uint8_t			valid_bitmap[GHCB_VB_SZ];

	int			sz_a;
	int			sz_b;
	int			sz_c;
	int			sz_d;
};
#endif /* !_LOCORE */

/* Definitions used with the MSR protocol */
#define MSR_PROTO_CPUID_REQ	0x4
#define MSR_PROTO_CPUID_RESP	0x5
#define MSR_PROTO_TERMINATE	0x100

#ifndef _LOCORE

extern vaddr_t ghcb_vaddr;
extern paddr_t ghcb_paddr;

struct ghcb_extra_regs {
	uint64_t	 exitcode;
	uint64_t	 exitinfo1;
	uint64_t	 exitinfo2;
	uint64_t	 scratch;
	void		*data;
	size_t		 data_sz;
};

void	ghcb_clear(struct ghcb_sa *);
int	ghcb_valbm_set(uint8_t *, int);
int	ghcb_valbm_isset(uint8_t *, int);
int	ghcb_verify_bm(uint8_t *, uint8_t *);
int	ghcb_valid(struct ghcb_sa *);

void	ghcb_sync_val(int, int, struct ghcb_sync *);
void	ghcb_sync_out(struct trapframe *, const struct ghcb_extra_regs *,
	    struct ghcb_sa *, struct ghcb_sync *);
void	ghcb_sync_in(struct trapframe *, struct ghcb_extra_regs *,
	    struct ghcb_sa *, struct ghcb_sync *);
void	_ghcb_mem_rw(vaddr_t, int, void *, bool);
void	_ghcb_io_rw(uint16_t, int, uint32_t *, bool);

static inline uint8_t
ghcb_mem_read_1(vaddr_t addr)
{
	uint8_t val;

	_ghcb_mem_rw(addr, GHCB_SZ8, &val, true);
	return val;
}

static inline uint16_t
ghcb_mem_read_2(vaddr_t addr)
{
	uint16_t val;

	_ghcb_mem_rw(addr, GHCB_SZ16, &val, true);
	return val;
}

static inline uint32_t
ghcb_mem_read_4(vaddr_t addr)
{
	uint32_t val;

	_ghcb_mem_rw(addr, GHCB_SZ32, &val, true);
	return val;
}

static inline uint64_t
ghcb_mem_read_8(vaddr_t addr)
{
	uint64_t val;

	_ghcb_mem_rw(addr, GHCB_SZ64, &val, true);
	return val;
}

static inline void
ghcb_mem_write_1(vaddr_t addr, uint8_t v)
{
	_ghcb_mem_rw(addr, GHCB_SZ8, &v, false);
}

static inline void
ghcb_mem_write_2(vaddr_t addr, uint16_t v)
{
	_ghcb_mem_rw(addr, GHCB_SZ16, &v, false);
}

static inline void
ghcb_mem_write_4(vaddr_t addr, uint32_t v)
{
	_ghcb_mem_rw(addr, GHCB_SZ32, &v, false);
}

static inline void
ghcb_mem_write_8(vaddr_t addr, uint64_t v)
{
	_ghcb_mem_rw(addr, GHCB_SZ64, &v, false);
}

static inline uint8_t
ghcb_io_read_1(uint16_t port)
{
	uint32_t val;

	_ghcb_io_rw(port, GHCB_SZ8, &val, true);
	return val;
}

static inline uint16_t
ghcb_io_read_2(uint16_t port)
{
	uint32_t val;

	_ghcb_io_rw(port, GHCB_SZ16, &val, true);
	return val;
}

static inline uint32_t
ghcb_io_read_4(uint16_t port)
{
	uint32_t val;

	_ghcb_io_rw(port, GHCB_SZ32, &val, true);
	return val;
}

static inline void
ghcb_io_write_1(uint16_t port, uint8_t v)
{
	uint32_t val = v;

	_ghcb_io_rw(port, GHCB_SZ8, &val, false);
}

static inline void
ghcb_io_write_2(uint16_t port, uint16_t v)
{
	uint32_t val = v;

	_ghcb_io_rw(port, GHCB_SZ16, &val, false);
}

static inline void
ghcb_io_write_4(uint16_t port, uint32_t v)
{
	_ghcb_io_rw(port, GHCB_SZ32, &v, false);
}

#endif /* !_LOCORE */

#endif /* !_MACHINE_GHCB_H_ */
