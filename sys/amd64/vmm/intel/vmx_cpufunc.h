/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 NetApp, Inc.
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

#ifndef	_VMX_CPUFUNC_H_
#define	_VMX_CPUFUNC_H_

struct vmcs;

/*
 * Section 5.2 "Conventions" from Intel Architecture Manual 2B.
 *
 *			error
 * VMsucceed		  0
 * VMFailInvalid	  1
 * VMFailValid		  2	see also VMCS VM-Instruction Error Field
 */
#define	VM_SUCCESS		0
#define	VM_FAIL_INVALID		1
#define	VM_FAIL_VALID		2
#define	VMX_SET_ERROR_CODE \
	"	jnc 1f;"						\
	"	mov $1, %[error];"	/* CF: error = 1 */		\
	"	jmp 3f;"						\
	"1:	jnz 2f;"						\
	"	mov $2, %[error];"	/* ZF: error = 2 */		\
	"	jmp 3f;"						\
	"2:	mov $0, %[error];"					\
	"3:"

/* returns 0 on success and non-zero on failure */
static __inline int
vmxon(char *region)
{
	int error;
	uint64_t addr;

	addr = vtophys(region);
	__asm __volatile("vmxon %[addr];"
			 VMX_SET_ERROR_CODE
			 : [error] "=r" (error)
			 : [addr] "m" (*(uint64_t *)&addr)
			 : "memory");

	return (error);
}

/* returns 0 on success and non-zero on failure */
static __inline int
vmclear(struct vmcs *vmcs)
{
	int error;
	uint64_t addr;

	addr = vtophys(vmcs);
	__asm __volatile("vmclear %[addr];"
			 VMX_SET_ERROR_CODE
			 : [error] "=r" (error)
			 : [addr] "m" (*(uint64_t *)&addr)
			 : "memory");
	return (error);
}

static __inline void
vmxoff(void)
{

	__asm __volatile("vmxoff");
}

static __inline void
vmptrst(uint64_t *addr)
{

	__asm __volatile("vmptrst %[addr]" :: [addr]"m" (*addr) : "memory");
}

static __inline int
vmptrld(struct vmcs *vmcs)
{
	int error;
	uint64_t addr;

	addr = vtophys(vmcs);
	__asm __volatile("vmptrld %[addr];"
			 VMX_SET_ERROR_CODE
			 : [error] "=r" (error)
			 : [addr] "m" (*(uint64_t *)&addr)
			 : "memory");
	return (error);
}

static __inline int
vmwrite(uint64_t reg, uint64_t val)
{
	int error;

	__asm __volatile("vmwrite %[val], %[reg];"
			 VMX_SET_ERROR_CODE
			 : [error] "=r" (error)
			 : [val] "r" (val), [reg] "r" (reg)
			 : "memory");

	return (error);
}

static __inline int
vmread(uint64_t r, uint64_t *addr)
{
	int error;

	__asm __volatile("vmread %[r], %[addr];"
			 VMX_SET_ERROR_CODE
			 : [error] "=r" (error)
			 : [r] "r" (r), [addr] "m" (*addr)
			 : "memory");

	return (error);
}

static void __inline
VMCLEAR(struct vmcs *vmcs)
{
	int err;

	err = vmclear(vmcs);
	if (err != 0)
		panic("%s: vmclear(%p) error %d", __func__, vmcs, err);

	critical_exit();
}

static void __inline
VMPTRLD(struct vmcs *vmcs)
{
	int err;

	critical_enter();

	err = vmptrld(vmcs);
	if (err != 0)
		panic("%s: vmptrld(%p) error %d", __func__, vmcs, err);
}

#define	INVVPID_TYPE_ADDRESS		0UL
#define	INVVPID_TYPE_SINGLE_CONTEXT	1UL
#define	INVVPID_TYPE_ALL_CONTEXTS	2UL

struct invvpid_desc {
	uint16_t	vpid;
	uint16_t	_res1;
	uint32_t	_res2;
	uint64_t	linear_addr;
};
CTASSERT(sizeof(struct invvpid_desc) == 16);

static void __inline
invvpid(uint64_t type, struct invvpid_desc desc)
{
	int error;

	__asm __volatile("invvpid %[desc], %[type];"
			 VMX_SET_ERROR_CODE
			 : [error] "=r" (error)
			 : [desc] "m" (desc), [type] "r" (type)
			 : "memory");

	if (error)
		panic("invvpid error %d", error);
}

#define	INVEPT_TYPE_SINGLE_CONTEXT	1UL
#define	INVEPT_TYPE_ALL_CONTEXTS	2UL
struct invept_desc {
	uint64_t	eptp;
	uint64_t	_res;
};
CTASSERT(sizeof(struct invept_desc) == 16);

static void __inline
invept(uint64_t type, struct invept_desc desc)
{
	int error;

	__asm __volatile("invept %[desc], %[type];"
			 VMX_SET_ERROR_CODE
			 : [error] "=r" (error)
			 : [desc] "m" (desc), [type] "r" (type)
			 : "memory");

	if (error)
		panic("invept error %d", error);
}
#endif
