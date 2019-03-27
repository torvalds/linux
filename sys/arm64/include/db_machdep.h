/*-
 * Copyright (c) 2014 Andrew Turner
 * Copyright (c) 2014-2015 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Semihalf under
 * sponsorship from the FreeBSD Foundation.
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

#ifndef	_MACHINE_DB_MACHDEP_H_
#define	_MACHINE_DB_MACHDEP_H_

#include <machine/armreg.h>
#include <machine/frame.h>
#include <machine/trap.h>

#define	T_BREAKPOINT	(EXCP_BRK)
#define	T_WATCHPOINT	(EXCP_WATCHPT_EL1)

typedef vm_offset_t	db_addr_t;
typedef long		db_expr_t;

#define	PC_REGS()	((db_addr_t)kdb_thrctx->pcb_pc)

#define	BKPT_INST	(0xd4200000)
#define	BKPT_SIZE	(4)
#define	BKPT_SET(inst)	(BKPT_INST)

#define	BKPT_SKIP do {							\
	kdb_frame->tf_elr += BKPT_SIZE; \
} while (0)

#define	db_clear_single_step	kdb_cpu_clear_singlestep
#define	db_set_single_step	kdb_cpu_set_singlestep

#define	IS_BREAKPOINT_TRAP(type, code)	(type == T_BREAKPOINT)
#define	IS_WATCHPOINT_TRAP(type, code)	(type == T_WATCHPOINT)

#define	inst_trap_return(ins)	(0)
/* ret */
#define	inst_return(ins)	(((ins) & 0xfffffc1fu) == 0xd65f0000)
#define	inst_call(ins)		(((ins) & 0xfc000000u) == 0x94000000u || /* BL */ \
				 ((ins) & 0xfffffc1fu) == 0xd63f0000u) /* BLR */

#define	inst_load(ins) ({							\
	uint32_t tmp_instr = db_get_value(PC_REGS(), sizeof(uint32_t), FALSE);	\
	is_load_instr(tmp_instr);						\
})

#define	inst_store(ins) ({							\
	uint32_t tmp_instr = db_get_value(PC_REGS(), sizeof(uint32_t), FALSE);	\
	is_store_instr(tmp_instr);						\
})

#define	is_load_instr(ins)	((((ins) & 0x3b000000u) == 0x18000000u) || /* literal */ \
				 (((ins) & 0x3f400000u) == 0x08400000u) ||  /* exclusive */ \
				 (((ins) & 0x3bc00000u) == 0x28400000u) || /* no-allocate pair */ \
				 ((((ins) & 0x3b200c00u) == 0x38000400u) && \
				  (((ins) & 0x3be00c00u) != 0x38000400u) && \
				  (((ins) & 0xffe00c00u) != 0x3c800400u)) || /* immediate post-indexed */ \
				 ((((ins) & 0x3b200c00u) == 0x38000c00u) && \
				  (((ins) & 0x3be00c00u) != 0x38000c00u) && \
				  (((ins) & 0xffe00c00u) != 0x3c800c00u)) || /* immediate pre-indexed */ \
				 ((((ins) & 0x3b200c00u) == 0x38200800u) && \
				  (((ins) & 0x3be00c00u) != 0x38200800u) && \
				  (((ins) & 0xffe00c00u) != 0x3ca00c80u)) || /* register offset */ \
				 ((((ins) & 0x3b200c00u) == 0x38000800u) && \
				  (((ins) & 0x3be00c00u) != 0x38000800u)) || /* unprivileged */ \
				 ((((ins) & 0x3b200c00u) == 0x38000000u) && \
				  (((ins) & 0x3be00c00u) != 0x38000000u) && \
				  (((ins) & 0xffe00c00u) != 0x3c800000u)) ||  /* unscaled immediate */ \
				 ((((ins) & 0x3b000000u) == 0x39000000u) && \
				  (((ins) & 0x3bc00000u) != 0x39000000u) && \
				  (((ins) & 0xffc00000u) != 0x3d800000u)) &&  /* unsigned immediate */ \
				 (((ins) & 0x3bc00000u) == 0x28400000u) || /* pair (offset) */ \
				 (((ins) & 0x3bc00000u) == 0x28c00000u) || /* pair (post-indexed) */ \
				 (((ins) & 0x3bc00000u) == 0x29800000u)) /* pair (pre-indexed) */

#define	is_store_instr(ins)	((((ins) & 0x3f400000u) == 0x08000000u) || /* exclusive */ \
				 (((ins) & 0x3bc00000u) == 0x28000000u) || /* no-allocate pair */ \
				 ((((ins) & 0x3be00c00u) == 0x38000400u) || \
				  (((ins) & 0xffe00c00u) == 0x3c800400u)) || /* immediate post-indexed */ \
				 ((((ins) & 0x3be00c00u) == 0x38000c00u) || \
				  (((ins) & 0xffe00c00u) == 0x3c800c00u)) || /* immediate pre-indexed */ \
				 ((((ins) & 0x3be00c00u) == 0x38200800u) || \
				  (((ins) & 0xffe00c00u) == 0x3ca00800u)) || /* register offset */ \
				 (((ins) & 0x3be00c00u) == 0x38000800u) ||  /* unprivileged */ \
				 ((((ins) & 0x3be00c00u) == 0x38000000u) || \
				  (((ins) & 0xffe00c00u) == 0x3c800000u)) ||  /* unscaled immediate */ \
				 ((((ins) & 0x3bc00000u) == 0x39000000u) || \
				  (((ins) & 0xffc00000u) == 0x3d800000u)) ||  /* unsigned immediate */ \
				 (((ins) & 0x3bc00000u) == 0x28000000u) || /* pair (offset) */ \
				 (((ins) & 0x3bc00000u) == 0x28800000u) || /* pair (post-indexed) */ \
				 (((ins) & 0x3bc00000u) == 0x29800000u)) /* pair (pre-indexed) */

#define	next_instr_address(pc, bd)	((bd) ? (pc) : ((pc) + 4))

#define	DB_ELFSIZE		64

#endif /* !_MACHINE_DB_MACHDEP_H_ */
