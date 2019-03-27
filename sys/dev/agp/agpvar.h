/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000 Doug Rabson
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
 *	$FreeBSD$
 */

#ifndef _PCI_AGPVAR_H_
#define _PCI_AGPVAR_H_

/*
 * The AGP chipset can be acquired by user or kernel code. If the
 * chipset has already been acquired, it cannot be acquired by another 
 * user until the previous user has released it.
 */
enum agp_acquire_state {
	AGP_ACQUIRE_FREE,
	AGP_ACQUIRE_USER,
	AGP_ACQUIRE_KERNEL
};

/*
 * This structure is used to query the state of the AGP system.
 */
struct agp_info {
	u_int32_t	ai_mode;
	vm_offset_t	ai_aperture_base;
	vm_size_t	ai_aperture_size;
	vm_size_t	ai_memory_allowed;
	vm_size_t	ai_memory_used;
	u_int32_t	ai_devid;
};

struct agp_memory_info {
	vm_size_t	ami_size;	/* size in bytes */
	vm_offset_t	ami_physical;	/* bogus hack for i810 */
	vm_offset_t	ami_offset;	/* page offset if bound */
	int		ami_is_bound;	/* non-zero if bound */
};

/*
 * Find the AGP device and return it.
 */
device_t agp_find_device(void);

/*
 * Return the current owner of the AGP chipset.
 */
enum agp_acquire_state agp_state(device_t dev);

/*
 * Query the state of the AGP system.
 */
void agp_get_info(device_t dev, struct agp_info *info);

/*
 * Acquire the AGP chipset for use by the kernel. Returns EBUSY if the
 * AGP chipset is already acquired by another user. 
 */
int agp_acquire(device_t dev);

/*
 * Release the AGP chipset.
 */
int agp_release(device_t dev);

/*
 * Enable the agp hardware with the relavent mode. The mode bits are
 * defined in <dev/agp/agpreg.h>
 */
int agp_enable(device_t dev, u_int32_t mode);

/*
 * Allocate physical memory suitable for mapping into the AGP
 * aperture.  The value returned is an opaque handle which can be
 * passed to agp_bind(), agp_unbind() or agp_deallocate().
 */
void *agp_alloc_memory(device_t dev, int type, vm_size_t bytes);

/*
 * Free memory which was allocated with agp_allocate().
 */
void agp_free_memory(device_t dev, void *handle);

/*
 * Bind memory allocated with agp_allocate() at a given offset within
 * the AGP aperture. Returns EINVAL if the memory is already bound or
 * the offset is not at an AGP page boundary.
 */
int agp_bind_memory(device_t dev, void *handle, vm_offset_t offset);

/*
 * Unbind memory from the AGP aperture. Returns EINVAL if the memory
 * is not bound.
 */
int agp_unbind_memory(device_t dev, void *handle);

/*
 * Retrieve information about a memory block allocated with
 * agp_alloc_memory().
 */
void agp_memory_info(device_t dev, void *handle, struct agp_memory_info *mi);

/*
 * Bind a set of pages at a given offset within the AGP aperture.
 * Returns EINVAL if the given size or offset is not at an AGP page boundary.
 */
int agp_bind_pages(device_t dev, vm_page_t *pages, vm_size_t size,
		   vm_offset_t offset);

/*
 * Unbind a set of pages from the AGP aperture.
 * Returns EINVAL if the given size or offset is not at an AGP page boundary.
 */
int agp_unbind_pages(device_t dev, vm_size_t size, vm_offset_t offset);

#define AGP_NORMAL_MEMORY 0

#define AGP_USER_TYPES (1 << 16)
#define AGP_USER_MEMORY (AGP_USER_TYPES)
#define AGP_USER_CACHED_MEMORY (AGP_USER_TYPES + 1)

#endif /* !_PCI_AGPVAR_H_ */
