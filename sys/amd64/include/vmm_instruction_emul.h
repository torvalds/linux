/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 NetApp, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY NETAPP, INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL NETAPP, INC OR CONTRIBUTORS BE LIABLE
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

#ifndef	_VMM_INSTRUCTION_EMUL_H_
#define	_VMM_INSTRUCTION_EMUL_H_

#include <sys/mman.h>

/*
 * Callback functions to read and write memory regions.
 */
typedef int (*mem_region_read_t)(void *vm, int cpuid, uint64_t gpa,
				 uint64_t *rval, int rsize, void *arg);

typedef int (*mem_region_write_t)(void *vm, int cpuid, uint64_t gpa,
				  uint64_t wval, int wsize, void *arg);

/*
 * Emulate the decoded 'vie' instruction.
 *
 * The callbacks 'mrr' and 'mrw' emulate reads and writes to the memory region
 * containing 'gpa'. 'mrarg' is an opaque argument that is passed into the
 * callback functions.
 *
 * 'void *vm' should be 'struct vm *' when called from kernel context and
 * 'struct vmctx *' when called from user context.
 * s
 */
int vmm_emulate_instruction(void *vm, int cpuid, uint64_t gpa, struct vie *vie,
    struct vm_guest_paging *paging, mem_region_read_t mrr,
    mem_region_write_t mrw, void *mrarg);

int vie_update_register(void *vm, int vcpuid, enum vm_reg_name reg,
    uint64_t val, int size);

/*
 * Returns 1 if an alignment check exception should be injected and 0 otherwise.
 */
int vie_alignment_check(int cpl, int operand_size, uint64_t cr0,
    uint64_t rflags, uint64_t gla);

/* Returns 1 if the 'gla' is not canonical and 0 otherwise. */
int vie_canonical_check(enum vm_cpu_mode cpu_mode, uint64_t gla);

uint64_t vie_size2mask(int size);

int vie_calculate_gla(enum vm_cpu_mode cpu_mode, enum vm_reg_name seg,
    struct seg_desc *desc, uint64_t off, int length, int addrsize, int prot,
    uint64_t *gla);

#ifdef _KERNEL
/*
 * APIs to fetch and decode the instruction from nested page fault handler.
 *
 * 'vie' must be initialized before calling 'vmm_fetch_instruction()'
 */
int vmm_fetch_instruction(struct vm *vm, int cpuid,
			  struct vm_guest_paging *guest_paging,
			  uint64_t rip, int inst_length, struct vie *vie,
			  int *is_fault);

/*
 * Translate the guest linear address 'gla' to a guest physical address.
 *
 * retval	is_fault	Interpretation
 *   0		   0		'gpa' contains result of the translation
 *   0		   1		An exception was injected into the guest
 * EFAULT	  N/A		An unrecoverable hypervisor error occurred
 */
int vm_gla2gpa(struct vm *vm, int vcpuid, struct vm_guest_paging *paging,
    uint64_t gla, int prot, uint64_t *gpa, int *is_fault);

/*
 * Like vm_gla2gpa, but no exceptions are injected into the guest and
 * PTEs are not changed.
 */
int vm_gla2gpa_nofault(struct vm *vm, int vcpuid, struct vm_guest_paging *paging,
    uint64_t gla, int prot, uint64_t *gpa, int *is_fault);

void vie_init(struct vie *vie, const char *inst_bytes, int inst_length);

/*
 * Decode the instruction fetched into 'vie' so it can be emulated.
 *
 * 'gla' is the guest linear address provided by the hardware assist
 * that caused the nested page table fault. It is used to verify that
 * the software instruction decoding is in agreement with the hardware.
 * 
 * Some hardware assists do not provide the 'gla' to the hypervisor.
 * To skip the 'gla' verification for this or any other reason pass
 * in VIE_INVALID_GLA instead.
 */
#define	VIE_INVALID_GLA		(1UL << 63)	/* a non-canonical address */
int vmm_decode_instruction(struct vm *vm, int cpuid, uint64_t gla,
			   enum vm_cpu_mode cpu_mode, int csd, struct vie *vie);
#endif	/* _KERNEL */

#endif	/* _VMM_INSTRUCTION_EMUL_H_ */
