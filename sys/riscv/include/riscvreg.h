/*-
 * Copyright (c) 2015-2017 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * Portions of this software were developed by SRI International and the
 * University of Cambridge Computer Laboratory under DARPA/AFRL contract
 * FA8750-10-C-0237 ("CTSRD"), as part of the DARPA CRASH research programme.
 *
 * Portions of this software were developed by the University of Cambridge
 * Computer Laboratory as part of the CTSRD Project, with support from the
 * UK Higher Education Innovation Fund (HEIF).
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

#ifndef _MACHINE_RISCVREG_H_
#define	_MACHINE_RISCVREG_H_

#define	EXCP_SHIFT			0
#define	EXCP_MASK			(0xf << EXCP_SHIFT)
#define	EXCP_MISALIGNED_FETCH		0
#define	EXCP_FAULT_FETCH		1
#define	EXCP_ILLEGAL_INSTRUCTION	2
#define	EXCP_BREAKPOINT			3
#define	EXCP_MISALIGNED_LOAD		4
#define	EXCP_FAULT_LOAD			5
#define	EXCP_MISALIGNED_STORE		6
#define	EXCP_FAULT_STORE		7
#define	EXCP_USER_ECALL			8
#define	EXCP_SUPERVISOR_ECALL		9
#define	EXCP_HYPERVISOR_ECALL		10
#define	EXCP_MACHINE_ECALL		11
#define	EXCP_INST_PAGE_FAULT		12
#define	EXCP_LOAD_PAGE_FAULT		13
#define	EXCP_STORE_PAGE_FAULT		15
#define	EXCP_INTR			(1ul << 63)

#define	SSTATUS_UIE			(1 << 0)
#define	SSTATUS_SIE			(1 << 1)
#define	SSTATUS_UPIE			(1 << 4)
#define	SSTATUS_SPIE			(1 << 5)
#define	SSTATUS_SPIE_SHIFT		5
#define	SSTATUS_SPP			(1 << 8)
#define	SSTATUS_SPP_SHIFT		8
#define	SSTATUS_FS_SHIFT		13
#define	SSTATUS_FS_OFF			(0x0 << SSTATUS_FS_SHIFT)
#define	SSTATUS_FS_INITIAL		(0x1 << SSTATUS_FS_SHIFT)
#define	SSTATUS_FS_CLEAN		(0x2 << SSTATUS_FS_SHIFT)
#define	SSTATUS_FS_DIRTY		(0x3 << SSTATUS_FS_SHIFT)
#define	SSTATUS_FS_MASK			(0x3 << SSTATUS_FS_SHIFT)
#define	SSTATUS_XS_SHIFT		15
#define	SSTATUS_XS_MASK			(0x3 << SSTATUS_XS_SHIFT)
#define	SSTATUS_SUM			(1 << 18)
#define	SSTATUS32_SD			(1 << 63)
#define	SSTATUS64_SD			(1 << 31)

#define	MSTATUS_UIE			(1 << 0)
#define	MSTATUS_SIE			(1 << 1)
#define	MSTATUS_HIE			(1 << 2)
#define	MSTATUS_MIE			(1 << 3)
#define	MSTATUS_UPIE			(1 << 4)
#define	MSTATUS_SPIE			(1 << 5)
#define	MSTATUS_SPIE_SHIFT		5
#define	MSTATUS_HPIE			(1 << 6)
#define	MSTATUS_MPIE			(1 << 7)
#define	MSTATUS_MPIE_SHIFT		7
#define	MSTATUS_SPP			(1 << 8)
#define	MSTATUS_SPP_SHIFT		8
#define	MSTATUS_HPP_MASK		0x3
#define	MSTATUS_HPP_SHIFT		9
#define	MSTATUS_MPP_MASK		0x3
#define	MSTATUS_MPP_SHIFT		11
#define	MSTATUS_FS_MASK			0x3
#define	MSTATUS_FS_SHIFT		13
#define	MSTATUS_XS_MASK			0x3
#define	MSTATUS_XS_SHIFT		15
#define	MSTATUS_MPRV			(1 << 17)
#define	MSTATUS_PUM			(1 << 18)
#define	MSTATUS_VM_MASK			0x1f
#define	MSTATUS_VM_SHIFT		24
#define	 MSTATUS_VM_MBARE		0
#define	 MSTATUS_VM_MBB			1
#define	 MSTATUS_VM_MBBID		2
#define	 MSTATUS_VM_SV32		8
#define	 MSTATUS_VM_SV39		9
#define	 MSTATUS_VM_SV48		10
#define	 MSTATUS_VM_SV57		11
#define	 MSTATUS_VM_SV64		12
#define	MSTATUS32_SD			(1 << 63)
#define	MSTATUS64_SD			(1 << 31)

#define	MSTATUS_PRV_U			0	/* user */
#define	MSTATUS_PRV_S			1	/* supervisor */
#define	MSTATUS_PRV_H			2	/* hypervisor */
#define	MSTATUS_PRV_M			3	/* machine */

#define	MIE_USIE	(1 << 0)
#define	MIE_SSIE	(1 << 1)
#define	MIE_HSIE	(1 << 2)
#define	MIE_MSIE	(1 << 3)
#define	MIE_UTIE	(1 << 4)
#define	MIE_STIE	(1 << 5)
#define	MIE_HTIE	(1 << 6)
#define	MIE_MTIE	(1 << 7)

#define	MIP_USIP	(1 << 0)
#define	MIP_SSIP	(1 << 1)
#define	MIP_HSIP	(1 << 2)
#define	MIP_MSIP	(1 << 3)
#define	MIP_UTIP	(1 << 4)
#define	MIP_STIP	(1 << 5)
#define	MIP_HTIP	(1 << 6)
#define	MIP_MTIP	(1 << 7)

#define	SIE_USIE	(1 << 0)
#define	SIE_SSIE	(1 << 1)
#define	SIE_UTIE	(1 << 4)
#define	SIE_STIE	(1 << 5)
#define	SIE_UEIE	(1 << 8)
#define	SIE_SEIE	(1 << 9)

#define	MIP_SEIP	(1 << 9)

/* Note: sip register has no SIP_STIP bit in Spike simulator */
#define	SIP_SSIP	(1 << 1)
#define	SIP_STIP	(1 << 5)

#define	SATP_PPN_S	0
#define	SATP_PPN_M	(0xfffffffffff << SATP_PPN_S)
#define	SATP_ASID_S	44
#define	SATP_ASID_M	(0xffff << SATP_ASID_S)
#define	SATP_MODE_S	60
#define	SATP_MODE_M	(0xf << SATP_MODE_S)
#define	SATP_MODE_SV39	(8ULL << SATP_MODE_S)
#define	SATP_MODE_SV48	(9ULL << SATP_MODE_S)

#define	XLEN		__riscv_xlen
#define	XLEN_BYTES	(XLEN / 8)
#define	INSN_SIZE	4
#define	INSN_C_SIZE	2

#define	X_RA	1
#define	X_SP	2
#define	X_GP	3
#define	X_TP	4
#define	X_T0	5
#define	X_T1	6
#define	X_T2	7
#define	X_T3	28

#define	RD_SHIFT	7
#define	RD_MASK		(0x1f << RD_SHIFT)
#define	RS1_SHIFT	15
#define	RS1_MASK	(0x1f << RS1_SHIFT)
#define	RS1_SP		(X_SP << RS1_SHIFT)
#define	RS2_SHIFT	20
#define	RS2_MASK	(0x1f << RS2_SHIFT)
#define	RS2_RA		(X_RA << RS2_SHIFT)
#define	IMM_SHIFT	20
#define	IMM_MASK	(0xfff << IMM_SHIFT)

#define	RS2_C_SHIFT	2
#define	RS2_C_MASK	(0x1f << RS2_C_SHIFT)
#define	RS2_C_RA	(X_RA << RS2_C_SHIFT)

#define	CSR_ZIMM(val)							\
	(__builtin_constant_p(val) && ((u_long)(val) < 32))

#define	csr_swap(csr, val)						\
({	if (CSR_ZIMM(val))  						\
		__asm __volatile("csrrwi %0, " #csr ", %1"		\
				: "=r" (val) : "i" (val));		\
	else 								\
		__asm __volatile("csrrw %0, " #csr ", %1"		\
				: "=r" (val) : "r" (val));		\
	val;								\
})

#define	csr_write(csr, val)						\
({	if (CSR_ZIMM(val)) 						\
		__asm __volatile("csrwi " #csr ", %0" :: "i" (val));	\
	else 								\
		__asm __volatile("csrw " #csr ", %0" ::  "r" (val));	\
})

#define	csr_set(csr, val)						\
({	if (CSR_ZIMM(val)) 						\
		__asm __volatile("csrsi " #csr ", %0" :: "i" (val));	\
	else								\
		__asm __volatile("csrs " #csr ", %0" :: "r" (val));	\
})

#define	csr_clear(csr, val)						\
({	if (CSR_ZIMM(val))						\
		__asm __volatile("csrci " #csr ", %0" :: "i" (val));	\
	else								\
		__asm __volatile("csrc " #csr ", %0" :: "r" (val));	\
})

#define	csr_read(csr)							\
({	u_long val;							\
	__asm __volatile("csrr %0, " #csr : "=r" (val));		\
	val;								\
})

#if __riscv_xlen == 32
#define	csr_read64(csr)							\
({	uint64_t val;							\
	uint32_t high, low;						\
	__asm __volatile("1: "						\
			 "csrr t0, " #csr "h\n"				\
			 "csrr %0, " #csr "\n"				\
			 "csrr %1, " #csr "h\n"				\
			 "bne t0, %1, 1b"				\
			 : "=r" (low), "=r" (high)			\
			 :						\
			 : "t0");					\
	val = (low | ((uint64_t)high << 32));				\
	val;								\
})
#else
#define	csr_read64(csr)		((uint64_t)csr_read(csr))
#endif

#endif /* !_MACHINE_RISCVREG_H_ */
