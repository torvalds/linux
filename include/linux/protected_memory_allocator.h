/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2019-2021 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

#ifndef _PROTECTED_MEMORY_ALLOCATOR_H_
#define _PROTECTED_MEMORY_ALLOCATOR_H_

#include <linux/mm.h>

/**
 * struct protected_memory_allocation - Protected memory allocation
 *
 * @pa:    Physical address of the protected memory allocation.
 * @order: Size of memory allocation in pages, as a base-2 logarithm.
 */
struct protected_memory_allocation {
	phys_addr_t pa;
	unsigned int order;
};

struct protected_memory_allocator_device;

/**
 * struct protected_memory_allocator_ops - Callbacks for protected memory
 *                                         allocator operations
 *
 * @pma_alloc_page:    Callback to allocate protected memory
 * @pma_get_phys_addr: Callback to get the physical address of an allocation
 * @pma_free_page:     Callback to free protected memory
 */
struct protected_memory_allocator_ops {
	/*
	 * pma_alloc_page - Allocate protected memory pages
	 *
	 * @pma_dev: The protected memory allocator the request is being made
	 *           through.
	 * @order:   How many pages to allocate, as a base-2 logarithm.
	 *
	 * Return: Pointer to allocated memory, or NULL if allocation failed.
	 */
	struct protected_memory_allocation *(*pma_alloc_page)(
		struct protected_memory_allocator_device *pma_dev,
		unsigned int order);

	/*
	 * pma_get_phys_addr - Get the physical address of the protected memory
	 *                     allocation
	 *
	 * @pma_dev: The protected memory allocator the request is being made
	 *           through.
	 * @pma:     The protected memory allocation whose physical address
	 *           shall be retrieved
	 *
	 * Return: The physical address of the given allocation.
	 */
	phys_addr_t (*pma_get_phys_addr)(
		struct protected_memory_allocator_device *pma_dev,
		struct protected_memory_allocation *pma);

	/*
	 * pma_free_page - Free a page of memory
	 *
	 * @pma_dev: The protected memory allocator the request is being made
	 *           through.
	 * @pma:     The protected memory allocation to free.
	 */
	void (*pma_free_page)(
		struct protected_memory_allocator_device *pma_dev,
		struct protected_memory_allocation *pma);
};

/**
 * struct protected_memory_allocator_device - Device structure for protected
 *                                            memory allocator
 *
 * @ops:   Callbacks associated with this device
 * @owner: Pointer to the module owner
 *
 * In order for a system integrator to provide custom behaviors for protected
 * memory operations performed by the kbase module (controller driver),
 * they shall provide a platform-specific driver module which implements
 * this interface.
 *
 * This structure should be registered with the platform device using
 * platform_set_drvdata().
 */
struct protected_memory_allocator_device {
	struct protected_memory_allocator_ops ops;
	struct module *owner;
};

#endif /* _PROTECTED_MEMORY_ALLOCATOR_H_ */
