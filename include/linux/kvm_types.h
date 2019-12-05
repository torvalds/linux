/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */

#ifndef __KVM_TYPES_H__
#define __KVM_TYPES_H__

struct kvm;
struct kvm_async_pf;
struct kvm_device_ops;
struct kvm_interrupt;
struct kvm_irq_routing_table;
struct kvm_memory_slot;
struct kvm_one_reg;
struct kvm_run;
struct kvm_userspace_memory_region;
struct kvm_vcpu;
struct kvm_vcpu_init;
struct kvm_memslots;

enum kvm_mr_change;

#include <linux/types.h>

/*
 * Address types:
 *
 *  gva - guest virtual address
 *  gpa - guest physical address
 *  gfn - guest frame number
 *  hva - host virtual address
 *  hpa - host physical address
 *  hfn - host frame number
 */

typedef unsigned long  gva_t;
typedef u64            gpa_t;
typedef u64            gfn_t;

typedef unsigned long  hva_t;
typedef u64            hpa_t;
typedef u64            hfn_t;

typedef hfn_t kvm_pfn_t;

struct gfn_to_hva_cache {
	u64 generation;
	gpa_t gpa;
	unsigned long hva;
	unsigned long len;
	struct kvm_memory_slot *memslot;
};

struct gfn_to_pfn_cache {
	u64 generation;
	gfn_t gfn;
	kvm_pfn_t pfn;
	bool dirty;
};

#endif /* __KVM_TYPES_H__ */
