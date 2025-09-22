/*	$OpenBSD: riscvreg.h,v 1.5 2022/08/29 02:01:18 jsg Exp $	*/

/*-
 * Copyright (c) 2019 Brian Bamsch <bbamsch@google.com>
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
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
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
 */

#ifndef _MACHINE_RISCVREG_H_
#define _MACHINE_RISCVREG_H_

#define EXCP_SHIFT			0
#define EXCP_MASK			(0xf << EXCP_SHIFT)
#define EXCP_MISALIGNED_FETCH		0
#define EXCP_FAULT_FETCH		1
#define EXCP_ILLEGAL_INSTRUCTION	2
#define EXCP_BREAKPOINT			3
#define EXCP_MISALIGNED_LOAD		4
#define EXCP_FAULT_LOAD			5
#define EXCP_MISALIGNED_STORE		6
#define EXCP_FAULT_STORE		7
#define EXCP_USER_ECALL			8
#define EXCP_SUPERVISOR_ECALL		9
#define EXCP_HYPERVISOR_ECALL		10
#define EXCP_MACHINE_ECALL		11
#define EXCP_INST_PAGE_FAULT		12
#define EXCP_LOAD_PAGE_FAULT		13
#define EXCP_STORE_PAGE_FAULT		15
#define EXCP_INTR			(1ULL << 63)

#define MSTATUS_UIE		(1 << 0)
#define MSTATUS_SIE		(1 << 1)
#define MSTATUS_MIE		(1 << 3)
#define MSTATUS_UPIE		(1 << 4)
#define MSTATUS_SPIE		(1 << 5)
#define MSTATUS_MPIE		(1 << 7)
#define MSTATUS_SPP		(1 << 8)
#define MSTATUS_MPP_SHIFT	11
#define MSTATUS_MPP_MASK	(0x3 << MSTATUS_MPP_SHIFT)
#define MSTATUS_FS_SHIFT	13
#define MSTATUS_FS_MASK		(0x3 << MSTATUS_FS_SHIFT)
#define MSTATUS_XS_SHIFT	15
#define MSTATUS_XS_MASK		(0x3 << MSTATUS_XS_SHIFT)
#define MSTATUS_MPRV		(1 << 17)
#define MSTATUS_SUM		(1 << 18)
#define MSTATUS_MXR		(1 << 19)
#define MSTATUS_TVM		(1 << 20)
#define MSTATUS_TW		(1 << 21)
#define MSTATUS_TSR		(1 << 22)
#define MSTATUS_UXL_SHIFT	32
#define MSTATUS_UXL_MASK	(0x3ULL << MSTATUS_UXL_SHIFT)
#define MSTATUS_SXL_SHIFT	34
#define MSTATUS_SXL_MASK	(0x3ULL << MSTATUS_SXL_SHIFT)
#define MSTATUS_SD		(1ULL << (MXLEN - 1))

#define SSTATUS_UIE		(1 << 0)
#define SSTATUS_SIE		(1 << 1)
#define SSTATUS_UPIE		(1 << 4)
#define SSTATUS_SPIE		(1 << 5)
#define SSTATUS_SPP		(1 << 8)
#define SSTATUS_FS_SHIFT	13
#define SSTATUS_FS_MASK		(0x3 << SSTATUS_FS_SHIFT)
#define SSTATUS_FS_OFF		(0x0 << SSTATUS_FS_SHIFT)
#define SSTATUS_FS_INITIAL	(0x1 << SSTATUS_FS_SHIFT)
#define SSTATUS_FS_CLEAN	(0x2 << SSTATUS_FS_SHIFT)
#define SSTATUS_FS_DIRTY	(0x3 << SSTATUS_FS_SHIFT)
#define SSTATUS_XS_SHIFT	15
#define SSTATUS_XS_MASK		(0x3 << SSTATUS_XS_SHIFT)
#define SSTATUS_SUM		(1 << 18)
#define SSTATUS_MXR		(1 << 19)
#define SSTATUS_UXL_SHIFT	32
#define SSTATUS_UXL_MASK	(0x3ULL << SSTATUS_UXL_SHIFT)
#define SSTATUS_SD		(1ULL << (SXLEN - 1))

#define USTATUS_UIE		(1 << 0)
#define USTATUS_UPIE		(1 << 4)

#define MSTATUS_PRV_U		0	/* user */
#define MSTATUS_PRV_S		1	/* supervisor */
#define MSTATUS_PRV_H		2	/* hypervisor */
#define MSTATUS_PRV_M		3	/* machine */

#define MIE_USIE	(1 << 0)
#define MIE_SSIE	(1 << 1)
#define MIE_MSIE	(1 << 3)
#define MIE_UTIE	(1 << 4)
#define MIE_STIE	(1 << 5)
#define MIE_MTIE	(1 << 7)
#define MIE_UEIE	(1 << 8)
#define MIE_SEIE	(1 << 9)
#define MIE_MEIE	(1 << 11)

#define MIP_USIP	(1 << 0)
#define MIP_SSIP	(1 << 1)
#define MIP_MSIP	(1 << 3)
#define MIP_UTIP	(1 << 4)
#define MIP_STIP	(1 << 5)
#define MIP_MTIP	(1 << 7)
#define MIP_UEIP	(1 << 8)
#define MIP_SEIP	(1 << 9)
#define MIP_MEIP	(1 << 11)

#define SIE_USIE	(1 << 0)
#define SIE_SSIE	(1 << 1)
#define SIE_UTIE	(1 << 4)
#define SIE_STIE	(1 << 5)
#define SIE_UEIE	(1 << 8)
#define SIE_SEIE	(1 << 9)

#define SIP_USIP	(1 << 0)
#define SIP_SSIP	(1 << 1)
#define SIP_UTIP	(1 << 4)
#define SIP_STIP	(1 << 5)
#define SIP_UEIP	(1 << 8)
#define SIP_SEIP	(1 << 9)

#define UIE_USIE	(1 << 0)
#define UIE_UTIE	(1 << 4)
#define UIE_UEIE	(1 << 8)

#define UIP_USIP	(1 << 0)
#define UIP_UTIP	(1 << 4)
#define UIP_UEIP	(1 << 8)

#define PPN(pa)			((pa) >> PAGE_SHIFT)
#define SATP_PPN_SHIFT		0
#define SATP_PPN_MASK		(0xfffffffffffULL << SATP_PPN_SHIFT)
#define SATP_PPN(satp)		(((satp) & SATP_PPN_MASK) >> SATP_PPN_SHIFT)
#define SATP_FORMAT_PPN(ppn)	(((uint64_t)(ppn) << SATP_PPN_SHIFT) & SATP_PPN_MASK)
#define SATP_ASID_SHIFT		44
#define SATP_ASID_MASK		(0xffffULL << SATP_ASID_SHIFT)
#define SATP_ASID(satp)		(((satp) & SATP_ASID_MASK) >> SATP_ASID_SHIFT)
#define SATP_FORMAT_ASID(asid)	(((uint64_t)(asid) << SATP_ASID_SHIFT) & SATP_ASID_MASK)
#define SATP_MODE_SHIFT		60
#define SATP_MODE_MASK		(0xfULL << SATP_MODE_SHIFT)
#define SATP_MODE(mode)		(((satp) & SATP_MODE_MASK) >> SATP_MODE_SHIFT)

#define SATP_MODE_SV39		(8ULL  << SATP_MODE_SHIFT)
#define SATP_MODE_SV48		(9ULL  << SATP_MODE_SHIFT)
#define SATP_MODE_SV57		(10ULL << SATP_MODE_SHIFT)
#define SATP_MODE_SV64		(11ULL << SATP_MODE_SHIFT)

/**
 * As of RISC-V Machine ISA v1.11, the XLEN can vary between
 * Machine, Supervisor, and User modes. The Machine XLEN (MXLEN)
 * is resolved from the MXL field of the 'misa' CSR. The
 * Supervisor XLEN (SXLEN) and User XLEN (UXLEN) are resolved
 * from the SXL and UXL fields of the 'mstatus' CSR, respectively.
 *
 * The Machine XLEN is reset to the widest supported ISA variant
 * at machine reset. For now, assume that all modes will always
 * use the same, static XLEN of 64 bits.
 */
#define XLEN			64
#define XLEN_BYTES		(XLEN / 8)
#define MXLEN			XLEN
#define SXLEN			XLEN
#define UXLEN			XLEN
#define INSN_SIZE		4
#define INSN_C_SIZE		2

// Check if val can fit in the CSR immediate form
#define CSR_ZIMM(val)							\
	(__builtin_constant_p(val) && ((u_long)(val) < 32))

#define csr_swap(csr, val)						\
({	if (CSR_ZIMM(val))						\
		__asm volatile("csrrwi %0, " #csr ", %1"		\
				: "=r" (val) : "i" (val));		\
	else								\
		__asm volatile("csrrw %0, " #csr ", %1"		\
				: "=r" (val) : "r" (val));		\
	val;								\
})

#define csr_write(csr, val)						\
({	if (CSR_ZIMM(val))						\
		__asm volatile("csrwi " #csr ", %0" :: "i" (val));	\
	else								\
		__asm volatile("csrw " #csr ", %0" ::  "r" (val));	\
})

#define csr_set(csr, val)						\
({	if (CSR_ZIMM(val))						\
		__asm volatile("csrsi " #csr ", %0" :: "i" (val));	\
	else								\
		__asm volatile("csrs " #csr ", %0" :: "r" (val));	\
})

#define csr_clear(csr, val)						\
({	if (CSR_ZIMM(val))						\
		__asm volatile("csrci " #csr ", %0" :: "i" (val));	\
	else								\
		__asm volatile("csrc " #csr ", %0" :: "r" (val));	\
})

#define csr_read(csr)							\
({	u_long val;							\
	__asm volatile("csrr %0, " #csr : "=r" (val));		\
	val;								\
})

#endif /* !_MACHINE_RISCVREG_H_ */
