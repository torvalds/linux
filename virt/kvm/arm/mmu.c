/*
 * Copyright (C) 2012 - Virtual Open Systems and Columbia University
 * Author: Christoffer Dall <c.dall@virtualopensystems.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/mman.h>
#include <linux/kvm_host.h>
#include <linux/io.h>
#include <linux/hugetlb.h>
#include <linux/sched/signal.h>
#include <trace/events/kvm.h>
#include <asm/pgalloc.h>
#include <asm/cacheflush.h>
#include <asm/kvm_arm.h>
#include <asm/kvm_mmu.h>
#include <asm/kvm_mmio.h>
#include <asm/kvm_asm.h>
#include <asm/kvm_emulate.h>
#include <asm/virt.h>
#include <asm/system_misc.h>

#include "trace.h"

static pgd_t *boot_hyp_pgd;
static pgd_t *hyp_pgd;
static pgd_t *merged_hyp_pgd;
static DEFINE_MUTEX(kvm_hyp_pgd_mutex);

static unsigned long hyp_idmap_start;
static unsigned long hyp_idmap_end;
static phys_addr_t hyp_idmap_vector;

#define S2_PGD_SIZE	(PTRS_PER_S2_PGD * sizeof(pgd_t))
#define hyp_pgd_order get_order(PTRS_PER_PGD * sizeof(pgd_t))

#define KVM_S2PTE_FLAG_IS_IOMAP		(1UL << 0)
#define KVM_S2_FLAG_LOGGING_ACTIVE	(1UL << 1)

static bool memslot_is_logging(struct kvm_memory_slot *memslot)
{
	return memslot->dirty_bitmap && !(memslot->flags & KVM_MEM_READONLY);
}

/**
 * kvm_flush_remote_tlbs() - flush all VM TLB entries for v7/8
 * @kvm:	pointer to kvm structure.
 *
 * Interface to HYP function to flush all VM TLB entries
 */
void kvm_flush_remote_tlbs(struct kvm *kvm)
{
	kvm_call_hyp(__kvm_tlb_flush_vmid, kvm);
}

static void kvm_tlb_flush_vmid_ipa(struct kvm *kvm, phys_addr_t ipa)
{
	kvm_call_hyp(__kvm_tlb_flush_vmid_ipa, kvm, ipa);
}

/*
 * D-Cache management functions. They take the page table entries by
 * value, as they are flushing the cache using the kernel mapping (or
 * kmap on 32bit).
 */
static void kvm_flush_dcache_pte(pte_t pte)
{
	__kvm_flush_dcache_pte(pte);
}

static void kvm_flush_dcache_pmd(pmd_t pmd)
{
	__kvm_flush_dcache_pmd(pmd);
}

static void kvm_flush_dcache_pud(pud_t pud)
{
	__kvm_flush_dcache_pud(pud);
}

static bool kvm_is_device_pfn(unsigned long pfn)
{
	return !pfn_valid(pfn);
}

/**
 * stage2_dissolve_pmd() - clear and flush huge PMD entry
 * @kvm:	pointer to kvm structure.
 * @addr:	IPA
 * @pmd:	pmd pointer for IPA
 *
 * Function clears a PMD entry, flushes addr 1st and 2nd stage TLBs. Marks all
 * pages in the range dirty.
 */
static void stage2_dissolve_pmd(struct kvm *kvm, phys_addr_t addr, pmd_t *pmd)
{
	if (!pmd_thp_or_huge(*pmd))
		return;

	pmd_clear(pmd);
	kvm_tlb_flush_vmid_ipa(kvm, addr);
	put_page(virt_to_page(pmd));
}

static int mmu_topup_memory_cache(struct kvm_mmu_memory_cache *cache,
				  int min, int max)
{
	void *page;

	BUG_ON(max > KVM_NR_MEM_OBJS);
	if (cache->nobjs >= min)
		return 0;
	while (cache->nobjs < max) {
		page = (void *)__get_free_page(PGALLOC_GFP);
		if (!page)
			return -ENOMEM;
		cache->objects[cache->nobjs++] = page;
	}
	return 0;
}

static void mmu_free_memory_cache(struct kvm_mmu_memory_cache *mc)
{
	while (mc->nobjs)
		free_page((unsigned long)mc->objects[--mc->nobjs]);
}

static void *mmu_memory_cache_alloc(struct kvm_mmu_memory_cache *mc)
{
	void *p;

	BUG_ON(!mc || !mc->nobjs);
	p = mc->objects[--mc->nobjs];
	return p;
}

static void clear_stage2_pgd_entry(struct kvm *kvm, pgd_t *pgd, phys_addr_t addr)
{
	pud_t *pud_table __maybe_unused = stage2_pud_offset(pgd, 0UL);
	stage2_pgd_clear(pgd);
	kvm_tlb_flush_vmid_ipa(kvm, addr);
	stage2_pud_free(pud_table);
	put_page(virt_to_page(pgd));
}

static void clear_stage2_pud_entry(struct kvm *kvm, pud_t *pud, phys_addr_t addr)
{
	pmd_t *pmd_table __maybe_unused = stage2_pmd_offset(pud, 0);
	VM_BUG_ON(stage2_pud_huge(*pud));
	stage2_pud_clear(pud);
	kvm_tlb_flush_vmid_ipa(kvm, addr);
	stage2_pmd_free(pmd_table);
	put_page(virt_to_page(pud));
}

static void clear_stage2_pmd_entry(struct kvm *kvm, pmd_t *pmd, phys_addr_t addr)
{
	pte_t *pte_table = pte_offset_kernel(pmd, 0);
	VM_BUG_ON(pmd_thp_or_huge(*pmd));
	pmd_clear(pmd);
	kvm_tlb_flush_vmid_ipa(kvm, addr);
	pte_free_kernel(NULL, pte_table);
	put_page(virt_to_page(pmd));
}

/*
 * Unmapping vs dcache management:
 *
 * If a guest maps certain memory pages as uncached, all writes will
 * bypass the data cache and go directly to RAM.  However, the CPUs
 * can still speculate reads (not writes) and fill cache lines with
 * data.
 *
 * Those cache lines will be *clean* cache lines though, so a
 * clean+invalidate operation is equivalent to an invalidate
 * operation, because no cache lines are marked dirty.
 *
 * Those clean cache lines could be filled prior to an uncached write
 * by the guest, and the cache coherent IO subsystem would therefore
 * end up writing old data to disk.
 *
 * This is why right after unmapping a page/section and invalidating
 * the corresponding TLBs, we call kvm_flush_dcache_p*() to make sure
 * the IO subsystem will never hit in the cache.
 */
static void unmap_stage2_ptes(struct kvm *kvm, pmd_t *pmd,
		       phys_addr_t addr, phys_addr_t end)
{
	phys_addr_t start_addr = addr;
	pte_t *pte, *start_pte;

	start_pte = pte = pte_offset_kernel(pmd, addr);
	do {
		if (!pte_none(*pte)) {
			pte_t old_pte = *pte;

			kvm_set_pte(pte, __pte(0));
			kvm_tlb_flush_vmid_ipa(kvm, addr);

			/* No need to invalidate the cache for device mappings */
			if (!kvm_is_device_pfn(pte_pfn(old_pte)))
				kvm_flush_dcache_pte(old_pte);

			put_page(virt_to_page(pte));
		}
	} while (pte++, addr += PAGE_SIZE, addr != end);

	if (stage2_pte_table_empty(start_pte))
		clear_stage2_pmd_entry(kvm, pmd, start_addr);
}

static void unmap_stage2_pmds(struct kvm *kvm, pud_t *pud,
		       phys_addr_t addr, phys_addr_t end)
{
	phys_addr_t next, start_addr = addr;
	pmd_t *pmd, *start_pmd;

	start_pmd = pmd = stage2_pmd_offset(pud, addr);
	do {
		next = stage2_pmd_addr_end(addr, end);
		if (!pmd_none(*pmd)) {
			if (pmd_thp_or_huge(*pmd)) {
				pmd_t old_pmd = *pmd;

				pmd_clear(pmd);
				kvm_tlb_flush_vmid_ipa(kvm, addr);

				kvm_flush_dcache_pmd(old_pmd);

				put_page(virt_to_page(pmd));
			} else {
				unmap_stage2_ptes(kvm, pmd, addr, next);
			}
		}
	} while (pmd++, addr = next, addr != end);

	if (stage2_pmd_table_empty(start_pmd))
		clear_stage2_pud_entry(kvm, pud, start_addr);
}

static void unmap_stage2_puds(struct kvm *kvm, pgd_t *pgd,
		       phys_addr_t addr, phys_addr_t end)
{
	phys_addr_t next, start_addr = addr;
	pud_t *pud, *start_pud;

	start_pud = pud = stage2_pud_offset(pgd, addr);
	do {
		next = stage2_pud_addr_end(addr, end);
		if (!stage2_pud_none(*pud)) {
			if (stage2_pud_huge(*pud)) {
				pud_t old_pud = *pud;

				stage2_pud_clear(pud);
				kvm_tlb_flush_vmid_ipa(kvm, addr);
				kvm_flush_dcache_pud(old_pud);
				put_page(virt_to_page(pud));
			} else {
				unmap_stage2_pmds(kvm, pud, addr, next);
			}
		}
	} while (pud++, addr = next, addr != end);

	if (stage2_pud_table_empty(start_pud))
		clear_stage2_pgd_entry(kvm, pgd, start_addr);
}

/**
 * unmap_stage2_range -- Clear stage2 page table entries to unmap a range
 * @kvm:   The VM pointer
 * @start: The intermediate physical base address of the range to unmap
 * @size:  The size of the area to unmap
 *
 * Clear a range of stage-2 mappings, lowering the various ref-counts.  Must
 * be called while holding mmu_lock (unless for freeing the stage2 pgd before
 * destroying the VM), otherwise another faulting VCPU may come in and mess
 * with things behind our backs.
 */
static void unmap_stage2_range(struct kvm *kvm, phys_addr_t start, u64 size)
{
	pgd_t *pgd;
	phys_addr_t addr = start, end = start + size;
	phys_addr_t next;

	assert_spin_locked(&kvm->mmu_lock);
	pgd = kvm->arch.pgd + stage2_pgd_index(addr);
	do {
		/*
		 * Make sure the page table is still active, as another thread
		 * could have possibly freed the page table, while we released
		 * the lock.
		 */
		if (!READ_ONCE(kvm->arch.pgd))
			break;
		next = stage2_pgd_addr_end(addr, end);
		if (!stage2_pgd_none(*pgd))
			unmap_stage2_puds(kvm, pgd, addr, next);
		/*
		 * If the range is too large, release the kvm->mmu_lock
		 * to prevent starvation and lockup detector warnings.
		 */
		if (next != end)
			cond_resched_lock(&kvm->mmu_lock);
	} while (pgd++, addr = next, addr != end);
}

static void stage2_flush_ptes(struct kvm *kvm, pmd_t *pmd,
			      phys_addr_t addr, phys_addr_t end)
{
	pte_t *pte;

	pte = pte_offset_kernel(pmd, addr);
	do {
		if (!pte_none(*pte) && !kvm_is_device_pfn(pte_pfn(*pte)))
			kvm_flush_dcache_pte(*pte);
	} while (pte++, addr += PAGE_SIZE, addr != end);
}

static void stage2_flush_pmds(struct kvm *kvm, pud_t *pud,
			      phys_addr_t addr, phys_addr_t end)
{
	pmd_t *pmd;
	phys_addr_t next;

	pmd = stage2_pmd_offset(pud, addr);
	do {
		next = stage2_pmd_addr_end(addr, end);
		if (!pmd_none(*pmd)) {
			if (pmd_thp_or_huge(*pmd))
				kvm_flush_dcache_pmd(*pmd);
			else
				stage2_flush_ptes(kvm, pmd, addr, next);
		}
	} while (pmd++, addr = next, addr != end);
}

static void stage2_flush_puds(struct kvm *kvm, pgd_t *pgd,
			      phys_addr_t addr, phys_addr_t end)
{
	pud_t *pud;
	phys_addr_t next;

	pud = stage2_pud_offset(pgd, addr);
	do {
		next = stage2_pud_addr_end(addr, end);
		if (!stage2_pud_none(*pud)) {
			if (stage2_pud_huge(*pud))
				kvm_flush_dcache_pud(*pud);
			else
				stage2_flush_pmds(kvm, pud, addr, next);
		}
	} while (pud++, addr = next, addr != end);
}

static void stage2_flush_memslot(struct kvm *kvm,
				 struct kvm_memory_slot *memslot)
{
	phys_addr_t addr = memslot->base_gfn << PAGE_SHIFT;
	phys_addr_t end = addr + PAGE_SIZE * memslot->npages;
	phys_addr_t next;
	pgd_t *pgd;

	pgd = kvm->arch.pgd + stage2_pgd_index(addr);
	do {
		next = stage2_pgd_addr_end(addr, end);
		stage2_flush_puds(kvm, pgd, addr, next);
	} while (pgd++, addr = next, addr != end);
}

/**
 * stage2_flush_vm - Invalidate cache for pages mapped in stage 2
 * @kvm: The struct kvm pointer
 *
 * Go through the stage 2 page tables and invalidate any cache lines
 * backing memory already mapped to the VM.
 */
static void stage2_flush_vm(struct kvm *kvm)
{
	struct kvm_memslots *slots;
	struct kvm_memory_slot *memslot;
	int idx;

	idx = srcu_read_lock(&kvm->srcu);
	spin_lock(&kvm->mmu_lock);

	slots = kvm_memslots(kvm);
	kvm_for_each_memslot(memslot, slots)
		stage2_flush_memslot(kvm, memslot);

	spin_unlock(&kvm->mmu_lock);
	srcu_read_unlock(&kvm->srcu, idx);
}

static void clear_hyp_pgd_entry(pgd_t *pgd)
{
	pud_t *pud_table __maybe_unused = pud_offset(pgd, 0UL);
	pgd_clear(pgd);
	pud_free(NULL, pud_table);
	put_page(virt_to_page(pgd));
}

static void clear_hyp_pud_entry(pud_t *pud)
{
	pmd_t *pmd_table __maybe_unused = pmd_offset(pud, 0);
	VM_BUG_ON(pud_huge(*pud));
	pud_clear(pud);
	pmd_free(NULL, pmd_table);
	put_page(virt_to_page(pud));
}

static void clear_hyp_pmd_entry(pmd_t *pmd)
{
	pte_t *pte_table = pte_offset_kernel(pmd, 0);
	VM_BUG_ON(pmd_thp_or_huge(*pmd));
	pmd_clear(pmd);
	pte_free_kernel(NULL, pte_table);
	put_page(virt_to_page(pmd));
}

static void unmap_hyp_ptes(pmd_t *pmd, phys_addr_t addr, phys_addr_t end)
{
	pte_t *pte, *start_pte;

	start_pte = pte = pte_offset_kernel(pmd, addr);
	do {
		if (!pte_none(*pte)) {
			kvm_set_pte(pte, __pte(0));
			put_page(virt_to_page(pte));
		}
	} while (pte++, addr += PAGE_SIZE, addr != end);

	if (hyp_pte_table_empty(start_pte))
		clear_hyp_pmd_entry(pmd);
}

static void unmap_hyp_pmds(pud_t *pud, phys_addr_t addr, phys_addr_t end)
{
	phys_addr_t next;
	pmd_t *pmd, *start_pmd;

	start_pmd = pmd = pmd_offset(pud, addr);
	do {
		next = pmd_addr_end(addr, end);
		/* Hyp doesn't use huge pmds */
		if (!pmd_none(*pmd))
			unmap_hyp_ptes(pmd, addr, next);
	} while (pmd++, addr = next, addr != end);

	if (hyp_pmd_table_empty(start_pmd))
		clear_hyp_pud_entry(pud);
}

static void unmap_hyp_puds(pgd_t *pgd, phys_addr_t addr, phys_addr_t end)
{
	phys_addr_t next;
	pud_t *pud, *start_pud;

	start_pud = pud = pud_offset(pgd, addr);
	do {
		next = pud_addr_end(addr, end);
		/* Hyp doesn't use huge puds */
		if (!pud_none(*pud))
			unmap_hyp_pmds(pud, addr, next);
	} while (pud++, addr = next, addr != end);

	if (hyp_pud_table_empty(start_pud))
		clear_hyp_pgd_entry(pgd);
}

static void unmap_hyp_range(pgd_t *pgdp, phys_addr_t start, u64 size)
{
	pgd_t *pgd;
	phys_addr_t addr = start, end = start + size;
	phys_addr_t next;

	/*
	 * We don't unmap anything from HYP, except at the hyp tear down.
	 * Hence, we don't have to invalidate the TLBs here.
	 */
	pgd = pgdp + pgd_index(addr);
	do {
		next = pgd_addr_end(addr, end);
		if (!pgd_none(*pgd))
			unmap_hyp_puds(pgd, addr, next);
	} while (pgd++, addr = next, addr != end);
}

/**
 * free_hyp_pgds - free Hyp-mode page tables
 *
 * Assumes hyp_pgd is a page table used strictly in Hyp-mode and
 * therefore contains either mappings in the kernel memory area (above
 * PAGE_OFFSET), or device mappings in the vmalloc range (from
 * VMALLOC_START to VMALLOC_END).
 *
 * boot_hyp_pgd should only map two pages for the init code.
 */
void free_hyp_pgds(void)
{
	mutex_lock(&kvm_hyp_pgd_mutex);

	if (boot_hyp_pgd) {
		unmap_hyp_range(boot_hyp_pgd, hyp_idmap_start, PAGE_SIZE);
		free_pages((unsigned long)boot_hyp_pgd, hyp_pgd_order);
		boot_hyp_pgd = NULL;
	}

	if (hyp_pgd) {
		unmap_hyp_range(hyp_pgd, hyp_idmap_start, PAGE_SIZE);
		unmap_hyp_range(hyp_pgd, kern_hyp_va(PAGE_OFFSET),
				(uintptr_t)high_memory - PAGE_OFFSET);
		unmap_hyp_range(hyp_pgd, kern_hyp_va(VMALLOC_START),
				VMALLOC_END - VMALLOC_START);

		free_pages((unsigned long)hyp_pgd, hyp_pgd_order);
		hyp_pgd = NULL;
	}
	if (merged_hyp_pgd) {
		clear_page(merged_hyp_pgd);
		free_page((unsigned long)merged_hyp_pgd);
		merged_hyp_pgd = NULL;
	}

	mutex_unlock(&kvm_hyp_pgd_mutex);
}

static void create_hyp_pte_mappings(pmd_t *pmd, unsigned long start,
				    unsigned long end, unsigned long pfn,
				    pgprot_t prot)
{
	pte_t *pte;
	unsigned long addr;

	addr = start;
	do {
		pte = pte_offset_kernel(pmd, addr);
		kvm_set_pte(pte, pfn_pte(pfn, prot));
		get_page(virt_to_page(pte));
		kvm_flush_dcache_to_poc(pte, sizeof(*pte));
		pfn++;
	} while (addr += PAGE_SIZE, addr != end);
}

static int create_hyp_pmd_mappings(pud_t *pud, unsigned long start,
				   unsigned long end, unsigned long pfn,
				   pgprot_t prot)
{
	pmd_t *pmd;
	pte_t *pte;
	unsigned long addr, next;

	addr = start;
	do {
		pmd = pmd_offset(pud, addr);

		BUG_ON(pmd_sect(*pmd));

		if (pmd_none(*pmd)) {
			pte = pte_alloc_one_kernel(NULL, addr);
			if (!pte) {
				kvm_err("Cannot allocate Hyp pte\n");
				return -ENOMEM;
			}
			pmd_populate_kernel(NULL, pmd, pte);
			get_page(virt_to_page(pmd));
			kvm_flush_dcache_to_poc(pmd, sizeof(*pmd));
		}

		next = pmd_addr_end(addr, end);

		create_hyp_pte_mappings(pmd, addr, next, pfn, prot);
		pfn += (next - addr) >> PAGE_SHIFT;
	} while (addr = next, addr != end);

	return 0;
}

static int create_hyp_pud_mappings(pgd_t *pgd, unsigned long start,
				   unsigned long end, unsigned long pfn,
				   pgprot_t prot)
{
	pud_t *pud;
	pmd_t *pmd;
	unsigned long addr, next;
	int ret;

	addr = start;
	do {
		pud = pud_offset(pgd, addr);

		if (pud_none_or_clear_bad(pud)) {
			pmd = pmd_alloc_one(NULL, addr);
			if (!pmd) {
				kvm_err("Cannot allocate Hyp pmd\n");
				return -ENOMEM;
			}
			pud_populate(NULL, pud, pmd);
			get_page(virt_to_page(pud));
			kvm_flush_dcache_to_poc(pud, sizeof(*pud));
		}

		next = pud_addr_end(addr, end);
		ret = create_hyp_pmd_mappings(pud, addr, next, pfn, prot);
		if (ret)
			return ret;
		pfn += (next - addr) >> PAGE_SHIFT;
	} while (addr = next, addr != end);

	return 0;
}

static int __create_hyp_mappings(pgd_t *pgdp,
				 unsigned long start, unsigned long end,
				 unsigned long pfn, pgprot_t prot)
{
	pgd_t *pgd;
	pud_t *pud;
	unsigned long addr, next;
	int err = 0;

	mutex_lock(&kvm_hyp_pgd_mutex);
	addr = start & PAGE_MASK;
	end = PAGE_ALIGN(end);
	do {
		pgd = pgdp + pgd_index(addr);

		if (pgd_none(*pgd)) {
			pud = pud_alloc_one(NULL, addr);
			if (!pud) {
				kvm_err("Cannot allocate Hyp pud\n");
				err = -ENOMEM;
				goto out;
			}
			pgd_populate(NULL, pgd, pud);
			get_page(virt_to_page(pgd));
			kvm_flush_dcache_to_poc(pgd, sizeof(*pgd));
		}

		next = pgd_addr_end(addr, end);
		err = create_hyp_pud_mappings(pgd, addr, next, pfn, prot);
		if (err)
			goto out;
		pfn += (next - addr) >> PAGE_SHIFT;
	} while (addr = next, addr != end);
out:
	mutex_unlock(&kvm_hyp_pgd_mutex);
	return err;
}

static phys_addr_t kvm_kaddr_to_phys(void *kaddr)
{
	if (!is_vmalloc_addr(kaddr)) {
		BUG_ON(!virt_addr_valid(kaddr));
		return __pa(kaddr);
	} else {
		return page_to_phys(vmalloc_to_page(kaddr)) +
		       offset_in_page(kaddr);
	}
}

/**
 * create_hyp_mappings - duplicate a kernel virtual address range in Hyp mode
 * @from:	The virtual kernel start address of the range
 * @to:		The virtual kernel end address of the range (exclusive)
 * @prot:	The protection to be applied to this range
 *
 * The same virtual address as the kernel virtual address is also used
 * in Hyp-mode mapping (modulo HYP_PAGE_OFFSET) to the same underlying
 * physical pages.
 */
int create_hyp_mappings(void *from, void *to, pgprot_t prot)
{
	phys_addr_t phys_addr;
	unsigned long virt_addr;
	unsigned long start = kern_hyp_va((unsigned long)from);
	unsigned long end = kern_hyp_va((unsigned long)to);

	if (is_kernel_in_hyp_mode())
		return 0;

	start = start & PAGE_MASK;
	end = PAGE_ALIGN(end);

	for (virt_addr = start; virt_addr < end; virt_addr += PAGE_SIZE) {
		int err;

		phys_addr = kvm_kaddr_to_phys(from + virt_addr - start);
		err = __create_hyp_mappings(hyp_pgd, virt_addr,
					    virt_addr + PAGE_SIZE,
					    __phys_to_pfn(phys_addr),
					    prot);
		if (err)
			return err;
	}

	return 0;
}

/**
 * create_hyp_io_mappings - duplicate a kernel IO mapping into Hyp mode
 * @from:	The kernel start VA of the range
 * @to:		The kernel end VA of the range (exclusive)
 * @phys_addr:	The physical start address which gets mapped
 *
 * The resulting HYP VA is the same as the kernel VA, modulo
 * HYP_PAGE_OFFSET.
 */
int create_hyp_io_mappings(void *from, void *to, phys_addr_t phys_addr)
{
	unsigned long start = kern_hyp_va((unsigned long)from);
	unsigned long end = kern_hyp_va((unsigned long)to);

	if (is_kernel_in_hyp_mode())
		return 0;

	/* Check for a valid kernel IO mapping */
	if (!is_vmalloc_addr(from) || !is_vmalloc_addr(to - 1))
		return -EINVAL;

	return __create_hyp_mappings(hyp_pgd, start, end,
				     __phys_to_pfn(phys_addr), PAGE_HYP_DEVICE);
}

/**
 * kvm_alloc_stage2_pgd - allocate level-1 table for stage-2 translation.
 * @kvm:	The KVM struct pointer for the VM.
 *
 * Allocates only the stage-2 HW PGD level table(s) (can support either full
 * 40-bit input addresses or limited to 32-bit input addresses). Clears the
 * allocated pages.
 *
 * Note we don't need locking here as this is only called when the VM is
 * created, which can only be done once.
 */
int kvm_alloc_stage2_pgd(struct kvm *kvm)
{
	pgd_t *pgd;

	if (kvm->arch.pgd != NULL) {
		kvm_err("kvm_arch already initialized?\n");
		return -EINVAL;
	}

	/* Allocate the HW PGD, making sure that each page gets its own refcount */
	pgd = alloc_pages_exact(S2_PGD_SIZE, GFP_KERNEL | __GFP_ZERO);
	if (!pgd)
		return -ENOMEM;

	kvm->arch.pgd = pgd;
	return 0;
}

static void stage2_unmap_memslot(struct kvm *kvm,
				 struct kvm_memory_slot *memslot)
{
	hva_t hva = memslot->userspace_addr;
	phys_addr_t addr = memslot->base_gfn << PAGE_SHIFT;
	phys_addr_t size = PAGE_SIZE * memslot->npages;
	hva_t reg_end = hva + size;

	/*
	 * A memory region could potentially cover multiple VMAs, and any holes
	 * between them, so iterate over all of them to find out if we should
	 * unmap any of them.
	 *
	 *     +--------------------------------------------+
	 * +---------------+----------------+   +----------------+
	 * |   : VMA 1     |      VMA 2     |   |    VMA 3  :    |
	 * +---------------+----------------+   +----------------+
	 *     |               memory region                |
	 *     +--------------------------------------------+
	 */
	do {
		struct vm_area_struct *vma = find_vma(current->mm, hva);
		hva_t vm_start, vm_end;

		if (!vma || vma->vm_start >= reg_end)
			break;

		/*
		 * Take the intersection of this VMA with the memory region
		 */
		vm_start = max(hva, vma->vm_start);
		vm_end = min(reg_end, vma->vm_end);

		if (!(vma->vm_flags & VM_PFNMAP)) {
			gpa_t gpa = addr + (vm_start - memslot->userspace_addr);
			unmap_stage2_range(kvm, gpa, vm_end - vm_start);
		}
		hva = vm_end;
	} while (hva < reg_end);
}

/**
 * stage2_unmap_vm - Unmap Stage-2 RAM mappings
 * @kvm: The struct kvm pointer
 *
 * Go through the memregions and unmap any reguler RAM
 * backing memory already mapped to the VM.
 */
void stage2_unmap_vm(struct kvm *kvm)
{
	struct kvm_memslots *slots;
	struct kvm_memory_slot *memslot;
	int idx;

	idx = srcu_read_lock(&kvm->srcu);
	down_read(&current->mm->mmap_sem);
	spin_lock(&kvm->mmu_lock);

	slots = kvm_memslots(kvm);
	kvm_for_each_memslot(memslot, slots)
		stage2_unmap_memslot(kvm, memslot);

	spin_unlock(&kvm->mmu_lock);
	up_read(&current->mm->mmap_sem);
	srcu_read_unlock(&kvm->srcu, idx);
}

/**
 * kvm_free_stage2_pgd - free all stage-2 tables
 * @kvm:	The KVM struct pointer for the VM.
 *
 * Walks the level-1 page table pointed to by kvm->arch.pgd and frees all
 * underlying level-2 and level-3 tables before freeing the actual level-1 table
 * and setting the struct pointer to NULL.
 */
void kvm_free_stage2_pgd(struct kvm *kvm)
{
	void *pgd = NULL;

	spin_lock(&kvm->mmu_lock);
	if (kvm->arch.pgd) {
		unmap_stage2_range(kvm, 0, KVM_PHYS_SIZE);
		pgd = READ_ONCE(kvm->arch.pgd);
		kvm->arch.pgd = NULL;
	}
	spin_unlock(&kvm->mmu_lock);

	/* Free the HW pgd, one page at a time */
	if (pgd)
		free_pages_exact(pgd, S2_PGD_SIZE);
}

static pud_t *stage2_get_pud(struct kvm *kvm, struct kvm_mmu_memory_cache *cache,
			     phys_addr_t addr)
{
	pgd_t *pgd;
	pud_t *pud;

	pgd = kvm->arch.pgd + stage2_pgd_index(addr);
	if (WARN_ON(stage2_pgd_none(*pgd))) {
		if (!cache)
			return NULL;
		pud = mmu_memory_cache_alloc(cache);
		stage2_pgd_populate(pgd, pud);
		get_page(virt_to_page(pgd));
	}

	return stage2_pud_offset(pgd, addr);
}

static pmd_t *stage2_get_pmd(struct kvm *kvm, struct kvm_mmu_memory_cache *cache,
			     phys_addr_t addr)
{
	pud_t *pud;
	pmd_t *pmd;

	pud = stage2_get_pud(kvm, cache, addr);
	if (!pud)
		return NULL;

	if (stage2_pud_none(*pud)) {
		if (!cache)
			return NULL;
		pmd = mmu_memory_cache_alloc(cache);
		stage2_pud_populate(pud, pmd);
		get_page(virt_to_page(pud));
	}

	return stage2_pmd_offset(pud, addr);
}

static int stage2_set_pmd_huge(struct kvm *kvm, struct kvm_mmu_memory_cache
			       *cache, phys_addr_t addr, const pmd_t *new_pmd)
{
	pmd_t *pmd, old_pmd;

	pmd = stage2_get_pmd(kvm, cache, addr);
	VM_BUG_ON(!pmd);

	old_pmd = *pmd;
	if (pmd_present(old_pmd)) {
		/*
		 * Multiple vcpus faulting on the same PMD entry, can
		 * lead to them sequentially updating the PMD with the
		 * same value. Following the break-before-make
		 * (pmd_clear() followed by tlb_flush()) process can
		 * hinder forward progress due to refaults generated
		 * on missing translations.
		 *
		 * Skip updating the page table if the entry is
		 * unchanged.
		 */
		if (pmd_val(old_pmd) == pmd_val(*new_pmd))
			return 0;

		/*
		 * Mapping in huge pages should only happen through a
		 * fault.  If a page is merged into a transparent huge
		 * page, the individual subpages of that huge page
		 * should be unmapped through MMU notifiers before we
		 * get here.
		 *
		 * Merging of CompoundPages is not supported; they
		 * should become splitting first, unmapped, merged,
		 * and mapped back in on-demand.
		 */
		VM_BUG_ON(pmd_pfn(old_pmd) != pmd_pfn(*new_pmd));

		pmd_clear(pmd);
		kvm_tlb_flush_vmid_ipa(kvm, addr);
	} else {
		get_page(virt_to_page(pmd));
	}

	kvm_set_pmd(pmd, *new_pmd);
	return 0;
}

static int stage2_set_pte(struct kvm *kvm, struct kvm_mmu_memory_cache *cache,
			  phys_addr_t addr, const pte_t *new_pte,
			  unsigned long flags)
{
	pmd_t *pmd;
	pte_t *pte, old_pte;
	bool iomap = flags & KVM_S2PTE_FLAG_IS_IOMAP;
	bool logging_active = flags & KVM_S2_FLAG_LOGGING_ACTIVE;

	VM_BUG_ON(logging_active && !cache);

	/* Create stage-2 page table mapping - Levels 0 and 1 */
	pmd = stage2_get_pmd(kvm, cache, addr);
	if (!pmd) {
		/*
		 * Ignore calls from kvm_set_spte_hva for unallocated
		 * address ranges.
		 */
		return 0;
	}

	/*
	 * While dirty page logging - dissolve huge PMD, then continue on to
	 * allocate page.
	 */
	if (logging_active)
		stage2_dissolve_pmd(kvm, addr, pmd);

	/* Create stage-2 page mappings - Level 2 */
	if (pmd_none(*pmd)) {
		if (!cache)
			return 0; /* ignore calls from kvm_set_spte_hva */
		pte = mmu_memory_cache_alloc(cache);
		pmd_populate_kernel(NULL, pmd, pte);
		get_page(virt_to_page(pmd));
	}

	pte = pte_offset_kernel(pmd, addr);

	if (iomap && pte_present(*pte))
		return -EFAULT;

	/* Create 2nd stage page table mapping - Level 3 */
	old_pte = *pte;
	if (pte_present(old_pte)) {
		/* Skip page table update if there is no change */
		if (pte_val(old_pte) == pte_val(*new_pte))
			return 0;

		kvm_set_pte(pte, __pte(0));
		kvm_tlb_flush_vmid_ipa(kvm, addr);
	} else {
		get_page(virt_to_page(pte));
	}

	kvm_set_pte(pte, *new_pte);
	return 0;
}

#ifndef __HAVE_ARCH_PTEP_TEST_AND_CLEAR_YOUNG
static int stage2_ptep_test_and_clear_young(pte_t *pte)
{
	if (pte_young(*pte)) {
		*pte = pte_mkold(*pte);
		return 1;
	}
	return 0;
}
#else
static int stage2_ptep_test_and_clear_young(pte_t *pte)
{
	return __ptep_test_and_clear_young(pte);
}
#endif

static int stage2_pmdp_test_and_clear_young(pmd_t *pmd)
{
	return stage2_ptep_test_and_clear_young((pte_t *)pmd);
}

/**
 * kvm_phys_addr_ioremap - map a device range to guest IPA
 *
 * @kvm:	The KVM pointer
 * @guest_ipa:	The IPA at which to insert the mapping
 * @pa:		The physical address of the device
 * @size:	The size of the mapping
 */
int kvm_phys_addr_ioremap(struct kvm *kvm, phys_addr_t guest_ipa,
			  phys_addr_t pa, unsigned long size, bool writable)
{
	phys_addr_t addr, end;
	int ret = 0;
	unsigned long pfn;
	struct kvm_mmu_memory_cache cache = { 0, };

	end = (guest_ipa + size + PAGE_SIZE - 1) & PAGE_MASK;
	pfn = __phys_to_pfn(pa);

	for (addr = guest_ipa; addr < end; addr += PAGE_SIZE) {
		pte_t pte = pfn_pte(pfn, PAGE_S2_DEVICE);

		if (writable)
			pte = kvm_s2pte_mkwrite(pte);

		ret = mmu_topup_memory_cache(&cache, KVM_MMU_CACHE_MIN_PAGES,
						KVM_NR_MEM_OBJS);
		if (ret)
			goto out;
		spin_lock(&kvm->mmu_lock);
		ret = stage2_set_pte(kvm, &cache, addr, &pte,
						KVM_S2PTE_FLAG_IS_IOMAP);
		spin_unlock(&kvm->mmu_lock);
		if (ret)
			goto out;

		pfn++;
	}

out:
	mmu_free_memory_cache(&cache);
	return ret;
}

static bool transparent_hugepage_adjust(kvm_pfn_t *pfnp, phys_addr_t *ipap)
{
	kvm_pfn_t pfn = *pfnp;
	gfn_t gfn = *ipap >> PAGE_SHIFT;

	if (PageTransCompoundMap(pfn_to_page(pfn))) {
		unsigned long mask;
		/*
		 * The address we faulted on is backed by a transparent huge
		 * page.  However, because we map the compound huge page and
		 * not the individual tail page, we need to transfer the
		 * refcount to the head page.  We have to be careful that the
		 * THP doesn't start to split while we are adjusting the
		 * refcounts.
		 *
		 * We are sure this doesn't happen, because mmu_notifier_retry
		 * was successful and we are holding the mmu_lock, so if this
		 * THP is trying to split, it will be blocked in the mmu
		 * notifier before touching any of the pages, specifically
		 * before being able to call __split_huge_page_refcount().
		 *
		 * We can therefore safely transfer the refcount from PG_tail
		 * to PG_head and switch the pfn from a tail page to the head
		 * page accordingly.
		 */
		mask = PTRS_PER_PMD - 1;
		VM_BUG_ON((gfn & mask) != (pfn & mask));
		if (pfn & mask) {
			*ipap &= PMD_MASK;
			kvm_release_pfn_clean(pfn);
			pfn &= ~mask;
			kvm_get_pfn(pfn);
			*pfnp = pfn;
		}

		return true;
	}

	return false;
}

static bool kvm_is_write_fault(struct kvm_vcpu *vcpu)
{
	if (kvm_vcpu_trap_is_iabt(vcpu))
		return false;

	return kvm_vcpu_dabt_iswrite(vcpu);
}

/**
 * stage2_wp_ptes - write protect PMD range
 * @pmd:	pointer to pmd entry
 * @addr:	range start address
 * @end:	range end address
 */
static void stage2_wp_ptes(pmd_t *pmd, phys_addr_t addr, phys_addr_t end)
{
	pte_t *pte;

	pte = pte_offset_kernel(pmd, addr);
	do {
		if (!pte_none(*pte)) {
			if (!kvm_s2pte_readonly(pte))
				kvm_set_s2pte_readonly(pte);
		}
	} while (pte++, addr += PAGE_SIZE, addr != end);
}

/**
 * stage2_wp_pmds - write protect PUD range
 * @pud:	pointer to pud entry
 * @addr:	range start address
 * @end:	range end address
 */
static void stage2_wp_pmds(pud_t *pud, phys_addr_t addr, phys_addr_t end)
{
	pmd_t *pmd;
	phys_addr_t next;

	pmd = stage2_pmd_offset(pud, addr);

	do {
		next = stage2_pmd_addr_end(addr, end);
		if (!pmd_none(*pmd)) {
			if (pmd_thp_or_huge(*pmd)) {
				if (!kvm_s2pmd_readonly(pmd))
					kvm_set_s2pmd_readonly(pmd);
			} else {
				stage2_wp_ptes(pmd, addr, next);
			}
		}
	} while (pmd++, addr = next, addr != end);
}

/**
  * stage2_wp_puds - write protect PGD range
  * @pgd:	pointer to pgd entry
  * @addr:	range start address
  * @end:	range end address
  *
  * Process PUD entries, for a huge PUD we cause a panic.
  */
static void  stage2_wp_puds(pgd_t *pgd, phys_addr_t addr, phys_addr_t end)
{
	pud_t *pud;
	phys_addr_t next;

	pud = stage2_pud_offset(pgd, addr);
	do {
		next = stage2_pud_addr_end(addr, end);
		if (!stage2_pud_none(*pud)) {
			/* TODO:PUD not supported, revisit later if supported */
			BUG_ON(stage2_pud_huge(*pud));
			stage2_wp_pmds(pud, addr, next);
		}
	} while (pud++, addr = next, addr != end);
}

/**
 * stage2_wp_range() - write protect stage2 memory region range
 * @kvm:	The KVM pointer
 * @addr:	Start address of range
 * @end:	End address of range
 */
static void stage2_wp_range(struct kvm *kvm, phys_addr_t addr, phys_addr_t end)
{
	pgd_t *pgd;
	phys_addr_t next;

	pgd = kvm->arch.pgd + stage2_pgd_index(addr);
	do {
		/*
		 * Release kvm_mmu_lock periodically if the memory region is
		 * large. Otherwise, we may see kernel panics with
		 * CONFIG_DETECT_HUNG_TASK, CONFIG_LOCKUP_DETECTOR,
		 * CONFIG_LOCKDEP. Additionally, holding the lock too long
		 * will also starve other vCPUs. We have to also make sure
		 * that the page tables are not freed while we released
		 * the lock.
		 */
		cond_resched_lock(&kvm->mmu_lock);
		if (!READ_ONCE(kvm->arch.pgd))
			break;
		next = stage2_pgd_addr_end(addr, end);
		if (stage2_pgd_present(*pgd))
			stage2_wp_puds(pgd, addr, next);
	} while (pgd++, addr = next, addr != end);
}

/**
 * kvm_mmu_wp_memory_region() - write protect stage 2 entries for memory slot
 * @kvm:	The KVM pointer
 * @slot:	The memory slot to write protect
 *
 * Called to start logging dirty pages after memory region
 * KVM_MEM_LOG_DIRTY_PAGES operation is called. After this function returns
 * all present PMD and PTEs are write protected in the memory region.
 * Afterwards read of dirty page log can be called.
 *
 * Acquires kvm_mmu_lock. Called with kvm->slots_lock mutex acquired,
 * serializing operations for VM memory regions.
 */
void kvm_mmu_wp_memory_region(struct kvm *kvm, int slot)
{
	struct kvm_memslots *slots = kvm_memslots(kvm);
	struct kvm_memory_slot *memslot = id_to_memslot(slots, slot);
	phys_addr_t start = memslot->base_gfn << PAGE_SHIFT;
	phys_addr_t end = (memslot->base_gfn + memslot->npages) << PAGE_SHIFT;

	spin_lock(&kvm->mmu_lock);
	stage2_wp_range(kvm, start, end);
	spin_unlock(&kvm->mmu_lock);
	kvm_flush_remote_tlbs(kvm);
}

/**
 * kvm_mmu_write_protect_pt_masked() - write protect dirty pages
 * @kvm:	The KVM pointer
 * @slot:	The memory slot associated with mask
 * @gfn_offset:	The gfn offset in memory slot
 * @mask:	The mask of dirty pages at offset 'gfn_offset' in this memory
 *		slot to be write protected
 *
 * Walks bits set in mask write protects the associated pte's. Caller must
 * acquire kvm_mmu_lock.
 */
static void kvm_mmu_write_protect_pt_masked(struct kvm *kvm,
		struct kvm_memory_slot *slot,
		gfn_t gfn_offset, unsigned long mask)
{
	phys_addr_t base_gfn = slot->base_gfn + gfn_offset;
	phys_addr_t start = (base_gfn +  __ffs(mask)) << PAGE_SHIFT;
	phys_addr_t end = (base_gfn + __fls(mask) + 1) << PAGE_SHIFT;

	stage2_wp_range(kvm, start, end);
}

/*
 * kvm_arch_mmu_enable_log_dirty_pt_masked - enable dirty logging for selected
 * dirty pages.
 *
 * It calls kvm_mmu_write_protect_pt_masked to write protect selected pages to
 * enable dirty logging for them.
 */
void kvm_arch_mmu_enable_log_dirty_pt_masked(struct kvm *kvm,
		struct kvm_memory_slot *slot,
		gfn_t gfn_offset, unsigned long mask)
{
	kvm_mmu_write_protect_pt_masked(kvm, slot, gfn_offset, mask);
}

static void coherent_cache_guest_page(struct kvm_vcpu *vcpu, kvm_pfn_t pfn,
				      unsigned long size)
{
	__coherent_cache_guest_page(vcpu, pfn, size);
}

static void kvm_send_hwpoison_signal(unsigned long address,
				     struct vm_area_struct *vma)
{
	siginfo_t info;

	info.si_signo   = SIGBUS;
	info.si_errno   = 0;
	info.si_code    = BUS_MCEERR_AR;
	info.si_addr    = (void __user *)address;

	if (is_vm_hugetlb_page(vma))
		info.si_addr_lsb = huge_page_shift(hstate_vma(vma));
	else
		info.si_addr_lsb = PAGE_SHIFT;

	send_sig_info(SIGBUS, &info, current);
}

static int user_mem_abort(struct kvm_vcpu *vcpu, phys_addr_t fault_ipa,
			  struct kvm_memory_slot *memslot, unsigned long hva,
			  unsigned long fault_status)
{
	int ret;
	bool write_fault, writable, hugetlb = false, force_pte = false;
	unsigned long mmu_seq;
	gfn_t gfn = fault_ipa >> PAGE_SHIFT;
	struct kvm *kvm = vcpu->kvm;
	struct kvm_mmu_memory_cache *memcache = &vcpu->arch.mmu_page_cache;
	struct vm_area_struct *vma;
	kvm_pfn_t pfn;
	pgprot_t mem_type = PAGE_S2;
	bool logging_active = memslot_is_logging(memslot);
	unsigned long flags = 0;

	write_fault = kvm_is_write_fault(vcpu);
	if (fault_status == FSC_PERM && !write_fault) {
		kvm_err("Unexpected L2 read permission error\n");
		return -EFAULT;
	}

	/* Let's check if we will get back a huge page backed by hugetlbfs */
	down_read(&current->mm->mmap_sem);
	vma = find_vma_intersection(current->mm, hva, hva + 1);
	if (unlikely(!vma)) {
		kvm_err("Failed to find VMA for hva 0x%lx\n", hva);
		up_read(&current->mm->mmap_sem);
		return -EFAULT;
	}

	if (vma_kernel_pagesize(vma) == PMD_SIZE && !logging_active) {
		hugetlb = true;
		gfn = (fault_ipa & PMD_MASK) >> PAGE_SHIFT;
	} else {
		/*
		 * Pages belonging to memslots that don't have the same
		 * alignment for userspace and IPA cannot be mapped using
		 * block descriptors even if the pages belong to a THP for
		 * the process, because the stage-2 block descriptor will
		 * cover more than a single THP and we loose atomicity for
		 * unmapping, updates, and splits of the THP or other pages
		 * in the stage-2 block range.
		 */
		if ((memslot->userspace_addr & ~PMD_MASK) !=
		    ((memslot->base_gfn << PAGE_SHIFT) & ~PMD_MASK))
			force_pte = true;
	}
	up_read(&current->mm->mmap_sem);

	/* We need minimum second+third level pages */
	ret = mmu_topup_memory_cache(memcache, KVM_MMU_CACHE_MIN_PAGES,
				     KVM_NR_MEM_OBJS);
	if (ret)
		return ret;

	mmu_seq = vcpu->kvm->mmu_notifier_seq;
	/*
	 * Ensure the read of mmu_notifier_seq happens before we call
	 * gfn_to_pfn_prot (which calls get_user_pages), so that we don't risk
	 * the page we just got a reference to gets unmapped before we have a
	 * chance to grab the mmu_lock, which ensure that if the page gets
	 * unmapped afterwards, the call to kvm_unmap_hva will take it away
	 * from us again properly. This smp_rmb() interacts with the smp_wmb()
	 * in kvm_mmu_notifier_invalidate_<page|range_end>.
	 */
	smp_rmb();

	pfn = gfn_to_pfn_prot(kvm, gfn, write_fault, &writable);
	if (pfn == KVM_PFN_ERR_HWPOISON) {
		kvm_send_hwpoison_signal(hva, vma);
		return 0;
	}
	if (is_error_noslot_pfn(pfn))
		return -EFAULT;

	if (kvm_is_device_pfn(pfn)) {
		mem_type = PAGE_S2_DEVICE;
		flags |= KVM_S2PTE_FLAG_IS_IOMAP;
	} else if (logging_active) {
		/*
		 * Faults on pages in a memslot with logging enabled
		 * should not be mapped with huge pages (it introduces churn
		 * and performance degradation), so force a pte mapping.
		 */
		force_pte = true;
		flags |= KVM_S2_FLAG_LOGGING_ACTIVE;

		/*
		 * Only actually map the page as writable if this was a write
		 * fault.
		 */
		if (!write_fault)
			writable = false;
	}

	spin_lock(&kvm->mmu_lock);
	if (mmu_notifier_retry(kvm, mmu_seq))
		goto out_unlock;

	if (!hugetlb && !force_pte)
		hugetlb = transparent_hugepage_adjust(&pfn, &fault_ipa);

	if (hugetlb) {
		pmd_t new_pmd = pfn_pmd(pfn, mem_type);
		new_pmd = pmd_mkhuge(new_pmd);
		if (writable) {
			new_pmd = kvm_s2pmd_mkwrite(new_pmd);
			kvm_set_pfn_dirty(pfn);
		}
		coherent_cache_guest_page(vcpu, pfn, PMD_SIZE);
		ret = stage2_set_pmd_huge(kvm, memcache, fault_ipa, &new_pmd);
	} else {
		pte_t new_pte = pfn_pte(pfn, mem_type);

		if (writable) {
			new_pte = kvm_s2pte_mkwrite(new_pte);
			kvm_set_pfn_dirty(pfn);
			mark_page_dirty(kvm, gfn);
		}
		coherent_cache_guest_page(vcpu, pfn, PAGE_SIZE);
		ret = stage2_set_pte(kvm, memcache, fault_ipa, &new_pte, flags);
	}

out_unlock:
	spin_unlock(&kvm->mmu_lock);
	kvm_set_pfn_accessed(pfn);
	kvm_release_pfn_clean(pfn);
	return ret;
}

/*
 * Resolve the access fault by making the page young again.
 * Note that because the faulting entry is guaranteed not to be
 * cached in the TLB, we don't need to invalidate anything.
 * Only the HW Access Flag updates are supported for Stage 2 (no DBM),
 * so there is no need for atomic (pte|pmd)_mkyoung operations.
 */
static void handle_access_fault(struct kvm_vcpu *vcpu, phys_addr_t fault_ipa)
{
	pmd_t *pmd;
	pte_t *pte;
	kvm_pfn_t pfn;
	bool pfn_valid = false;

	trace_kvm_access_fault(fault_ipa);

	spin_lock(&vcpu->kvm->mmu_lock);

	pmd = stage2_get_pmd(vcpu->kvm, NULL, fault_ipa);
	if (!pmd || pmd_none(*pmd))	/* Nothing there */
		goto out;

	if (pmd_thp_or_huge(*pmd)) {	/* THP, HugeTLB */
		*pmd = pmd_mkyoung(*pmd);
		pfn = pmd_pfn(*pmd);
		pfn_valid = true;
		goto out;
	}

	pte = pte_offset_kernel(pmd, fault_ipa);
	if (pte_none(*pte))		/* Nothing there either */
		goto out;

	*pte = pte_mkyoung(*pte);	/* Just a page... */
	pfn = pte_pfn(*pte);
	pfn_valid = true;
out:
	spin_unlock(&vcpu->kvm->mmu_lock);
	if (pfn_valid)
		kvm_set_pfn_accessed(pfn);
}

/**
 * kvm_handle_guest_abort - handles all 2nd stage aborts
 * @vcpu:	the VCPU pointer
 * @run:	the kvm_run structure
 *
 * Any abort that gets to the host is almost guaranteed to be caused by a
 * missing second stage translation table entry, which can mean that either the
 * guest simply needs more memory and we must allocate an appropriate page or it
 * can mean that the guest tried to access I/O memory, which is emulated by user
 * space. The distinction is based on the IPA causing the fault and whether this
 * memory region has been registered as standard RAM by user space.
 */
int kvm_handle_guest_abort(struct kvm_vcpu *vcpu, struct kvm_run *run)
{
	unsigned long fault_status;
	phys_addr_t fault_ipa;
	struct kvm_memory_slot *memslot;
	unsigned long hva;
	bool is_iabt, write_fault, writable;
	gfn_t gfn;
	int ret, idx;

	fault_status = kvm_vcpu_trap_get_fault_type(vcpu);

	fault_ipa = kvm_vcpu_get_fault_ipa(vcpu);
	is_iabt = kvm_vcpu_trap_is_iabt(vcpu);

	/* Synchronous External Abort? */
	if (kvm_vcpu_dabt_isextabt(vcpu)) {
		/*
		 * For RAS the host kernel may handle this abort.
		 * There is no need to pass the error into the guest.
		 */
		if (!handle_guest_sea(fault_ipa, kvm_vcpu_get_hsr(vcpu)))
			return 1;

		if (unlikely(!is_iabt)) {
			kvm_inject_vabt(vcpu);
			return 1;
		}
	}

	trace_kvm_guest_fault(*vcpu_pc(vcpu), kvm_vcpu_get_hsr(vcpu),
			      kvm_vcpu_get_hfar(vcpu), fault_ipa);

	/* Check the stage-2 fault is trans. fault or write fault */
	if (fault_status != FSC_FAULT && fault_status != FSC_PERM &&
	    fault_status != FSC_ACCESS) {
		kvm_err("Unsupported FSC: EC=%#x xFSC=%#lx ESR_EL2=%#lx\n",
			kvm_vcpu_trap_get_class(vcpu),
			(unsigned long)kvm_vcpu_trap_get_fault(vcpu),
			(unsigned long)kvm_vcpu_get_hsr(vcpu));
		return -EFAULT;
	}

	idx = srcu_read_lock(&vcpu->kvm->srcu);

	gfn = fault_ipa >> PAGE_SHIFT;
	memslot = gfn_to_memslot(vcpu->kvm, gfn);
	hva = gfn_to_hva_memslot_prot(memslot, gfn, &writable);
	write_fault = kvm_is_write_fault(vcpu);
	if (kvm_is_error_hva(hva) || (write_fault && !writable)) {
		if (is_iabt) {
			/* Prefetch Abort on I/O address */
			kvm_inject_pabt(vcpu, kvm_vcpu_get_hfar(vcpu));
			ret = 1;
			goto out_unlock;
		}

		/*
		 * Check for a cache maintenance operation. Since we
		 * ended-up here, we know it is outside of any memory
		 * slot. But we can't find out if that is for a device,
		 * or if the guest is just being stupid. The only thing
		 * we know for sure is that this range cannot be cached.
		 *
		 * So let's assume that the guest is just being
		 * cautious, and skip the instruction.
		 */
		if (kvm_vcpu_dabt_is_cm(vcpu)) {
			kvm_skip_instr(vcpu, kvm_vcpu_trap_il_is32bit(vcpu));
			ret = 1;
			goto out_unlock;
		}

		/*
		 * The IPA is reported as [MAX:12], so we need to
		 * complement it with the bottom 12 bits from the
		 * faulting VA. This is always 12 bits, irrespective
		 * of the page size.
		 */
		fault_ipa |= kvm_vcpu_get_hfar(vcpu) & ((1 << 12) - 1);
		ret = io_mem_abort(vcpu, run, fault_ipa);
		goto out_unlock;
	}

	/* Userspace should not be able to register out-of-bounds IPAs */
	VM_BUG_ON(fault_ipa >= KVM_PHYS_SIZE);

	if (fault_status == FSC_ACCESS) {
		handle_access_fault(vcpu, fault_ipa);
		ret = 1;
		goto out_unlock;
	}

	ret = user_mem_abort(vcpu, fault_ipa, memslot, hva, fault_status);
	if (ret == 0)
		ret = 1;
out_unlock:
	srcu_read_unlock(&vcpu->kvm->srcu, idx);
	return ret;
}

static int handle_hva_to_gpa(struct kvm *kvm,
			     unsigned long start,
			     unsigned long end,
			     int (*handler)(struct kvm *kvm,
					    gpa_t gpa, u64 size,
					    void *data),
			     void *data)
{
	struct kvm_memslots *slots;
	struct kvm_memory_slot *memslot;
	int ret = 0;

	slots = kvm_memslots(kvm);

	/* we only care about the pages that the guest sees */
	kvm_for_each_memslot(memslot, slots) {
		unsigned long hva_start, hva_end;
		gfn_t gpa;

		hva_start = max(start, memslot->userspace_addr);
		hva_end = min(end, memslot->userspace_addr +
					(memslot->npages << PAGE_SHIFT));
		if (hva_start >= hva_end)
			continue;

		gpa = hva_to_gfn_memslot(hva_start, memslot) << PAGE_SHIFT;
		ret |= handler(kvm, gpa, (u64)(hva_end - hva_start), data);
	}

	return ret;
}

static int kvm_unmap_hva_handler(struct kvm *kvm, gpa_t gpa, u64 size, void *data)
{
	unmap_stage2_range(kvm, gpa, size);
	return 0;
}

int kvm_unmap_hva(struct kvm *kvm, unsigned long hva)
{
	unsigned long end = hva + PAGE_SIZE;

	if (!kvm->arch.pgd)
		return 0;

	trace_kvm_unmap_hva(hva);
	handle_hva_to_gpa(kvm, hva, end, &kvm_unmap_hva_handler, NULL);
	return 0;
}

int kvm_unmap_hva_range(struct kvm *kvm,
			unsigned long start, unsigned long end)
{
	if (!kvm->arch.pgd)
		return 0;

	trace_kvm_unmap_hva_range(start, end);
	handle_hva_to_gpa(kvm, start, end, &kvm_unmap_hva_handler, NULL);
	return 0;
}

static int kvm_set_spte_handler(struct kvm *kvm, gpa_t gpa, u64 size, void *data)
{
	pte_t *pte = (pte_t *)data;

	WARN_ON(size != PAGE_SIZE);
	/*
	 * We can always call stage2_set_pte with KVM_S2PTE_FLAG_LOGGING_ACTIVE
	 * flag clear because MMU notifiers will have unmapped a huge PMD before
	 * calling ->change_pte() (which in turn calls kvm_set_spte_hva()) and
	 * therefore stage2_set_pte() never needs to clear out a huge PMD
	 * through this calling path.
	 */
	stage2_set_pte(kvm, NULL, gpa, pte, 0);
	return 0;
}


void kvm_set_spte_hva(struct kvm *kvm, unsigned long hva, pte_t pte)
{
	unsigned long end = hva + PAGE_SIZE;
	pte_t stage2_pte;

	if (!kvm->arch.pgd)
		return;

	trace_kvm_set_spte_hva(hva);
	stage2_pte = pfn_pte(pte_pfn(pte), PAGE_S2);
	handle_hva_to_gpa(kvm, hva, end, &kvm_set_spte_handler, &stage2_pte);
}

static int kvm_age_hva_handler(struct kvm *kvm, gpa_t gpa, u64 size, void *data)
{
	pmd_t *pmd;
	pte_t *pte;

	WARN_ON(size != PAGE_SIZE && size != PMD_SIZE);
	pmd = stage2_get_pmd(kvm, NULL, gpa);
	if (!pmd || pmd_none(*pmd))	/* Nothing there */
		return 0;

	if (pmd_thp_or_huge(*pmd))	/* THP, HugeTLB */
		return stage2_pmdp_test_and_clear_young(pmd);

	pte = pte_offset_kernel(pmd, gpa);
	if (pte_none(*pte))
		return 0;

	return stage2_ptep_test_and_clear_young(pte);
}

static int kvm_test_age_hva_handler(struct kvm *kvm, gpa_t gpa, u64 size, void *data)
{
	pmd_t *pmd;
	pte_t *pte;

	WARN_ON(size != PAGE_SIZE && size != PMD_SIZE);
	pmd = stage2_get_pmd(kvm, NULL, gpa);
	if (!pmd || pmd_none(*pmd))	/* Nothing there */
		return 0;

	if (pmd_thp_or_huge(*pmd))		/* THP, HugeTLB */
		return pmd_young(*pmd);

	pte = pte_offset_kernel(pmd, gpa);
	if (!pte_none(*pte))		/* Just a page... */
		return pte_young(*pte);

	return 0;
}

int kvm_age_hva(struct kvm *kvm, unsigned long start, unsigned long end)
{
	if (!kvm->arch.pgd)
		return 0;
	trace_kvm_age_hva(start, end);
	return handle_hva_to_gpa(kvm, start, end, kvm_age_hva_handler, NULL);
}

int kvm_test_age_hva(struct kvm *kvm, unsigned long hva)
{
	if (!kvm->arch.pgd)
		return 0;
	trace_kvm_test_age_hva(hva);
	return handle_hva_to_gpa(kvm, hva, hva, kvm_test_age_hva_handler, NULL);
}

void kvm_mmu_free_memory_caches(struct kvm_vcpu *vcpu)
{
	mmu_free_memory_cache(&vcpu->arch.mmu_page_cache);
}

phys_addr_t kvm_mmu_get_httbr(void)
{
	if (__kvm_cpu_uses_extended_idmap())
		return virt_to_phys(merged_hyp_pgd);
	else
		return virt_to_phys(hyp_pgd);
}

phys_addr_t kvm_get_idmap_vector(void)
{
	return hyp_idmap_vector;
}

static int kvm_map_idmap_text(pgd_t *pgd)
{
	int err;

	/* Create the idmap in the boot page tables */
	err = 	__create_hyp_mappings(pgd,
				      hyp_idmap_start, hyp_idmap_end,
				      __phys_to_pfn(hyp_idmap_start),
				      PAGE_HYP_EXEC);
	if (err)
		kvm_err("Failed to idmap %lx-%lx\n",
			hyp_idmap_start, hyp_idmap_end);

	return err;
}

int kvm_mmu_init(void)
{
	int err;

	hyp_idmap_start = kvm_virt_to_phys(__hyp_idmap_text_start);
	hyp_idmap_end = kvm_virt_to_phys(__hyp_idmap_text_end);
	hyp_idmap_vector = kvm_virt_to_phys(__kvm_hyp_init);

	/*
	 * We rely on the linker script to ensure at build time that the HYP
	 * init code does not cross a page boundary.
	 */
	BUG_ON((hyp_idmap_start ^ (hyp_idmap_end - 1)) & PAGE_MASK);

	kvm_debug("IDMAP page: %lx\n", hyp_idmap_start);
	kvm_debug("HYP VA range: %lx:%lx\n",
		  kern_hyp_va(PAGE_OFFSET), kern_hyp_va(~0UL));

	if (hyp_idmap_start >= kern_hyp_va(PAGE_OFFSET) &&
	    hyp_idmap_start <  kern_hyp_va(~0UL) &&
	    hyp_idmap_start != (unsigned long)__hyp_idmap_text_start) {
		/*
		 * The idmap page is intersecting with the VA space,
		 * it is not safe to continue further.
		 */
		kvm_err("IDMAP intersecting with HYP VA, unable to continue\n");
		err = -EINVAL;
		goto out;
	}

	hyp_pgd = (pgd_t *)__get_free_pages(GFP_KERNEL | __GFP_ZERO, hyp_pgd_order);
	if (!hyp_pgd) {
		kvm_err("Hyp mode PGD not allocated\n");
		err = -ENOMEM;
		goto out;
	}

	if (__kvm_cpu_uses_extended_idmap()) {
		boot_hyp_pgd = (pgd_t *)__get_free_pages(GFP_KERNEL | __GFP_ZERO,
							 hyp_pgd_order);
		if (!boot_hyp_pgd) {
			kvm_err("Hyp boot PGD not allocated\n");
			err = -ENOMEM;
			goto out;
		}

		err = kvm_map_idmap_text(boot_hyp_pgd);
		if (err)
			goto out;

		merged_hyp_pgd = (pgd_t *)__get_free_page(GFP_KERNEL | __GFP_ZERO);
		if (!merged_hyp_pgd) {
			kvm_err("Failed to allocate extra HYP pgd\n");
			goto out;
		}
		__kvm_extend_hypmap(boot_hyp_pgd, hyp_pgd, merged_hyp_pgd,
				    hyp_idmap_start);
	} else {
		err = kvm_map_idmap_text(hyp_pgd);
		if (err)
			goto out;
	}

	return 0;
out:
	free_hyp_pgds();
	return err;
}

void kvm_arch_commit_memory_region(struct kvm *kvm,
				   const struct kvm_userspace_memory_region *mem,
				   const struct kvm_memory_slot *old,
				   const struct kvm_memory_slot *new,
				   enum kvm_mr_change change)
{
	/*
	 * At this point memslot has been committed and there is an
	 * allocated dirty_bitmap[], dirty pages will be be tracked while the
	 * memory slot is write protected.
	 */
	if (change != KVM_MR_DELETE && mem->flags & KVM_MEM_LOG_DIRTY_PAGES)
		kvm_mmu_wp_memory_region(kvm, mem->slot);
}

int kvm_arch_prepare_memory_region(struct kvm *kvm,
				   struct kvm_memory_slot *memslot,
				   const struct kvm_userspace_memory_region *mem,
				   enum kvm_mr_change change)
{
	hva_t hva = mem->userspace_addr;
	hva_t reg_end = hva + mem->memory_size;
	bool writable = !(mem->flags & KVM_MEM_READONLY);
	int ret = 0;

	if (change != KVM_MR_CREATE && change != KVM_MR_MOVE &&
			change != KVM_MR_FLAGS_ONLY)
		return 0;

	/*
	 * Prevent userspace from creating a memory region outside of the IPA
	 * space addressable by the KVM guest IPA space.
	 */
	if (memslot->base_gfn + memslot->npages >=
	    (KVM_PHYS_SIZE >> PAGE_SHIFT))
		return -EFAULT;

	down_read(&current->mm->mmap_sem);
	/*
	 * A memory region could potentially cover multiple VMAs, and any holes
	 * between them, so iterate over all of them to find out if we can map
	 * any of them right now.
	 *
	 *     +--------------------------------------------+
	 * +---------------+----------------+   +----------------+
	 * |   : VMA 1     |      VMA 2     |   |    VMA 3  :    |
	 * +---------------+----------------+   +----------------+
	 *     |               memory region                |
	 *     +--------------------------------------------+
	 */
	do {
		struct vm_area_struct *vma = find_vma(current->mm, hva);
		hva_t vm_start, vm_end;

		if (!vma || vma->vm_start >= reg_end)
			break;

		/*
		 * Mapping a read-only VMA is only allowed if the
		 * memory region is configured as read-only.
		 */
		if (writable && !(vma->vm_flags & VM_WRITE)) {
			ret = -EPERM;
			break;
		}

		/*
		 * Take the intersection of this VMA with the memory region
		 */
		vm_start = max(hva, vma->vm_start);
		vm_end = min(reg_end, vma->vm_end);

		if (vma->vm_flags & VM_PFNMAP) {
			gpa_t gpa = mem->guest_phys_addr +
				    (vm_start - mem->userspace_addr);
			phys_addr_t pa;

			pa = (phys_addr_t)vma->vm_pgoff << PAGE_SHIFT;
			pa += vm_start - vma->vm_start;

			/* IO region dirty page logging not allowed */
			if (memslot->flags & KVM_MEM_LOG_DIRTY_PAGES) {
				ret = -EINVAL;
				goto out;
			}

			ret = kvm_phys_addr_ioremap(kvm, gpa, pa,
						    vm_end - vm_start,
						    writable);
			if (ret)
				break;
		}
		hva = vm_end;
	} while (hva < reg_end);

	if (change == KVM_MR_FLAGS_ONLY)
		goto out;

	spin_lock(&kvm->mmu_lock);
	if (ret)
		unmap_stage2_range(kvm, mem->guest_phys_addr, mem->memory_size);
	else
		stage2_flush_memslot(kvm, memslot);
	spin_unlock(&kvm->mmu_lock);
out:
	up_read(&current->mm->mmap_sem);
	return ret;
}

void kvm_arch_free_memslot(struct kvm *kvm, struct kvm_memory_slot *free,
			   struct kvm_memory_slot *dont)
{
}

int kvm_arch_create_memslot(struct kvm *kvm, struct kvm_memory_slot *slot,
			    unsigned long npages)
{
	return 0;
}

void kvm_arch_memslots_updated(struct kvm *kvm, struct kvm_memslots *slots)
{
}

void kvm_arch_flush_shadow_all(struct kvm *kvm)
{
	kvm_free_stage2_pgd(kvm);
}

void kvm_arch_flush_shadow_memslot(struct kvm *kvm,
				   struct kvm_memory_slot *slot)
{
	gpa_t gpa = slot->base_gfn << PAGE_SHIFT;
	phys_addr_t size = slot->npages << PAGE_SHIFT;

	spin_lock(&kvm->mmu_lock);
	unmap_stage2_range(kvm, gpa, size);
	spin_unlock(&kvm->mmu_lock);
}

/*
 * See note at ARMv7 ARM B1.14.4 (TL;DR: S/W ops are not easily virtualized).
 *
 * Main problems:
 * - S/W ops are local to a CPU (not broadcast)
 * - We have line migration behind our back (speculation)
 * - System caches don't support S/W at all (damn!)
 *
 * In the face of the above, the best we can do is to try and convert
 * S/W ops to VA ops. Because the guest is not allowed to infer the
 * S/W to PA mapping, it can only use S/W to nuke the whole cache,
 * which is a rather good thing for us.
 *
 * Also, it is only used when turning caches on/off ("The expected
 * usage of the cache maintenance instructions that operate by set/way
 * is associated with the cache maintenance instructions associated
 * with the powerdown and powerup of caches, if this is required by
 * the implementation.").
 *
 * We use the following policy:
 *
 * - If we trap a S/W operation, we enable VM trapping to detect
 *   caches being turned on/off, and do a full clean.
 *
 * - We flush the caches on both caches being turned on and off.
 *
 * - Once the caches are enabled, we stop trapping VM ops.
 */
void kvm_set_way_flush(struct kvm_vcpu *vcpu)
{
	unsigned long hcr = vcpu_get_hcr(vcpu);

	/*
	 * If this is the first time we do a S/W operation
	 * (i.e. HCR_TVM not set) flush the whole memory, and set the
	 * VM trapping.
	 *
	 * Otherwise, rely on the VM trapping to wait for the MMU +
	 * Caches to be turned off. At that point, we'll be able to
	 * clean the caches again.
	 */
	if (!(hcr & HCR_TVM)) {
		trace_kvm_set_way_flush(*vcpu_pc(vcpu),
					vcpu_has_cache_enabled(vcpu));
		stage2_flush_vm(vcpu->kvm);
		vcpu_set_hcr(vcpu, hcr | HCR_TVM);
	}
}

void kvm_toggle_cache(struct kvm_vcpu *vcpu, bool was_enabled)
{
	bool now_enabled = vcpu_has_cache_enabled(vcpu);

	/*
	 * If switching the MMU+caches on, need to invalidate the caches.
	 * If switching it off, need to clean the caches.
	 * Clean + invalidate does the trick always.
	 */
	if (now_enabled != was_enabled)
		stage2_flush_vm(vcpu->kvm);

	/* Caches are now on, stop trapping VM ops (until a S/W op) */
	if (now_enabled)
		vcpu_set_hcr(vcpu, vcpu_get_hcr(vcpu) & ~HCR_TVM);

	trace_kvm_toggle_cache(*vcpu_pc(vcpu), was_enabled, now_enabled);
}
