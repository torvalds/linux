/*-
 * Copyright (c) 2017 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * This software was developed by BAE Systems, the University of Cambridge
 * Computer Laboratory, and Memorial University under DARPA/AFRL contract
 * FA8650-15-C-7558 ("CADETS"), as part of the DARPA Transparent Computing
 * (TC) research program.
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

#ifndef _AMD64_SGX_SGXVAR_H_
#define _AMD64_SGX_SGXVAR_H_

#define	SGX_CPUID			0x12
#define	SGX_PAGE_SIZE			4096
#define	SGX_VA_PAGE_SLOTS		512
#define	SGX_VA_PAGES_OFFS		512
#define	SGX_SECS_VM_OBJECT_INDEX	-1
#define	SGX_SIGSTRUCT_SIZE		1808
#define	SGX_EINITTOKEN_SIZE		304
#define	SGX_IOCTL_MAX_DATA_LEN		26
#define	SGX_ENCL_SIZE_MAX_DEF		0x1000000000ULL
#define	SGX_EFAULT			99

#ifndef LOCORE
static MALLOC_DEFINE(M_SGX, "sgx", "SGX driver");

struct sgx_vm_handle {
	struct sgx_softc	*sc;
	vm_object_t		mem;
	uint64_t		base;
	vm_size_t		size;
	struct sgx_enclave	*enclave;
};

/* EPC (Enclave Page Cache) page. */
struct epc_page {
	uint64_t		base;
	uint64_t		phys;
	int			index;
};

struct sgx_enclave {
	uint64_t			base;
	uint64_t			size;
	struct sgx_vm_handle		*vmh;
	TAILQ_ENTRY(sgx_enclave)	next;
	vm_object_t			object;
	struct epc_page			*secs_epc_page;
};

struct sgx_softc {
	struct cdev			*sgx_cdev;
	struct mtx			mtx_encls;
	struct mtx			mtx;
	uint64_t			epc_base;
	uint64_t			epc_size;
	struct epc_page			*epc_pages;
	struct vmem			*vmem_epc;
	uint32_t			npages;
	TAILQ_HEAD(, sgx_enclave)	enclaves;
	uint64_t			enclave_size_max;
	uint8_t				state;
#define	SGX_STATE_RUNNING		(1 << 0)
};
#endif /* !LOCORE */

#endif /* !_AMD64_SGX_SGXVAR_H_ */
