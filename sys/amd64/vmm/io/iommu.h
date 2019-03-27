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

#ifndef _IO_IOMMU_H_
#define	_IO_IOMMU_H_

typedef int (*iommu_init_func_t)(void);
typedef void (*iommu_cleanup_func_t)(void);
typedef void (*iommu_enable_func_t)(void);
typedef void (*iommu_disable_func_t)(void);
typedef void *(*iommu_create_domain_t)(vm_paddr_t maxaddr);
typedef void (*iommu_destroy_domain_t)(void *domain);
typedef uint64_t (*iommu_create_mapping_t)(void *domain, vm_paddr_t gpa,
					   vm_paddr_t hpa, uint64_t len);
typedef uint64_t (*iommu_remove_mapping_t)(void *domain, vm_paddr_t gpa,
					   uint64_t len);
typedef void (*iommu_add_device_t)(void *domain, uint16_t rid);
typedef void (*iommu_remove_device_t)(void *dom, uint16_t rid);
typedef void (*iommu_invalidate_tlb_t)(void *dom);

struct iommu_ops {
	iommu_init_func_t	init;		/* module wide */
	iommu_cleanup_func_t	cleanup;
	iommu_enable_func_t	enable;
	iommu_disable_func_t	disable;

	iommu_create_domain_t	create_domain;	/* domain-specific */
	iommu_destroy_domain_t	destroy_domain;
	iommu_create_mapping_t	create_mapping;
	iommu_remove_mapping_t	remove_mapping;
	iommu_add_device_t	add_device;
	iommu_remove_device_t	remove_device;
	iommu_invalidate_tlb_t	invalidate_tlb;
};

extern struct iommu_ops iommu_ops_intel;
extern struct iommu_ops iommu_ops_amd;

void	iommu_cleanup(void);
void	*iommu_host_domain(void);
void	*iommu_create_domain(vm_paddr_t maxaddr);
void	iommu_destroy_domain(void *dom);
void	iommu_create_mapping(void *dom, vm_paddr_t gpa, vm_paddr_t hpa,
			     size_t len);
void	iommu_remove_mapping(void *dom, vm_paddr_t gpa, size_t len);
void	iommu_add_device(void *dom, uint16_t rid);
void	iommu_remove_device(void *dom, uint16_t rid);
void	iommu_invalidate_tlb(void *domain);
#endif
