// SPDX-License-Identifier: GPL-2.0+
/*
 * User-space Probes (UProbes)
 *
 * Copyright (C) IBM Corporation, 2008-2012
 * Authors:
 *	Srikar Dronamraju
 *	Jim Keniston
 * Copyright (C) 2011-2012 Red Hat, Inc., Peter Zijlstra
 */

#include <linux/kernel.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>	/* read_mapping_page */
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/export.h>
#include <linux/rmap.h>		/* anon_vma_prepare */
#include <linux/mmu_notifier.h>
#include <linux/swap.h>		/* folio_free_swap */
#include <linux/ptrace.h>	/* user_enable_single_step */
#include <linux/kdebug.h>	/* notifier mechanism */
#include <linux/percpu-rwsem.h>
#include <linux/task_work.h>
#include <linux/shmem_fs.h>
#include <linux/khugepaged.h>
#include <linux/rcupdate_trace.h>
#include <linux/workqueue.h>
#include <linux/srcu.h>
#include <linux/oom.h>          /* check_stable_address_space */

#include <linux/uprobes.h>

#define UINSNS_PER_PAGE			(PAGE_SIZE/UPROBE_XOL_SLOT_BYTES)
#define MAX_UPROBE_XOL_SLOTS		UINSNS_PER_PAGE

static struct rb_root uprobes_tree = RB_ROOT;
/*
 * allows us to skip the uprobe_mmap if there are no uprobe events active
 * at this time.  Probably a fine grained per inode count is better?
 */
#define no_uprobe_events()	RB_EMPTY_ROOT(&uprobes_tree)

static DEFINE_RWLOCK(uprobes_treelock);	/* serialize rbtree access */
static seqcount_rwlock_t uprobes_seqcount = SEQCNT_RWLOCK_ZERO(uprobes_seqcount, &uprobes_treelock);

#define UPROBES_HASH_SZ	13
/* serialize uprobe->pending_list */
static struct mutex uprobes_mmap_mutex[UPROBES_HASH_SZ];
#define uprobes_mmap_hash(v)	(&uprobes_mmap_mutex[((unsigned long)(v)) % UPROBES_HASH_SZ])

DEFINE_STATIC_PERCPU_RWSEM(dup_mmap_sem);

/* Covers return_instance's uprobe lifetime. */
DEFINE_STATIC_SRCU(uretprobes_srcu);

/* Have a copy of original instruction */
#define UPROBE_COPY_INSN	0

struct uprobe {
	struct rb_node		rb_node;	/* node in the rb tree */
	refcount_t		ref;
	struct rw_semaphore	register_rwsem;
	struct rw_semaphore	consumer_rwsem;
	struct list_head	pending_list;
	struct list_head	consumers;
	struct inode		*inode;		/* Also hold a ref to inode */
	union {
		struct rcu_head		rcu;
		struct work_struct	work;
	};
	loff_t			offset;
	loff_t			ref_ctr_offset;
	unsigned long		flags;		/* "unsigned long" so bitops work */

	/*
	 * The generic code assumes that it has two members of unknown type
	 * owned by the arch-specific code:
	 *
	 * 	insn -	copy_insn() saves the original instruction here for
	 *		arch_uprobe_analyze_insn().
	 *
	 *	ixol -	potentially modified instruction to execute out of
	 *		line, copied to xol_area by xol_get_insn_slot().
	 */
	struct arch_uprobe	arch;
};

struct delayed_uprobe {
	struct list_head list;
	struct uprobe *uprobe;
	struct mm_struct *mm;
};

static DEFINE_MUTEX(delayed_uprobe_lock);
static LIST_HEAD(delayed_uprobe_list);

/*
 * Execute out of line area: anonymous executable mapping installed
 * by the probed task to execute the copy of the original instruction
 * mangled by set_swbp().
 *
 * On a breakpoint hit, thread contests for a slot.  It frees the
 * slot after singlestep. Currently a fixed number of slots are
 * allocated.
 */
struct xol_area {
	wait_queue_head_t 		wq;		/* if all slots are busy */
	unsigned long 			*bitmap;	/* 0 = free slot */

	struct page			*page;
	/*
	 * We keep the vma's vm_start rather than a pointer to the vma
	 * itself.  The probed process or a naughty kernel module could make
	 * the vma go away, and we must handle that reasonably gracefully.
	 */
	unsigned long 			vaddr;		/* Page(s) of instruction slots */
};

static void uprobe_warn(struct task_struct *t, const char *msg)
{
	pr_warn("uprobe: %s:%d failed to %s\n", current->comm, current->pid, msg);
}

/*
 * valid_vma: Verify if the specified vma is an executable vma
 * Relax restrictions while unregistering: vm_flags might have
 * changed after breakpoint was inserted.
 *	- is_register: indicates if we are in register context.
 *	- Return 1 if the specified virtual address is in an
 *	  executable vma.
 */
static bool valid_vma(struct vm_area_struct *vma, bool is_register)
{
	vm_flags_t flags = VM_HUGETLB | VM_MAYEXEC | VM_MAYSHARE;

	if (is_register)
		flags |= VM_WRITE;

	return vma->vm_file && (vma->vm_flags & flags) == VM_MAYEXEC;
}

static unsigned long offset_to_vaddr(struct vm_area_struct *vma, loff_t offset)
{
	return vma->vm_start + offset - ((loff_t)vma->vm_pgoff << PAGE_SHIFT);
}

static loff_t vaddr_to_offset(struct vm_area_struct *vma, unsigned long vaddr)
{
	return ((loff_t)vma->vm_pgoff << PAGE_SHIFT) + (vaddr - vma->vm_start);
}

/**
 * __replace_page - replace page in vma by new page.
 * based on replace_page in mm/ksm.c
 *
 * @vma:      vma that holds the pte pointing to page
 * @addr:     address the old @page is mapped at
 * @old_page: the page we are replacing by new_page
 * @new_page: the modified page we replace page by
 *
 * If @new_page is NULL, only unmap @old_page.
 *
 * Returns 0 on success, negative error code otherwise.
 */
static int __replace_page(struct vm_area_struct *vma, unsigned long addr,
				struct page *old_page, struct page *new_page)
{
	struct folio *old_folio = page_folio(old_page);
	struct folio *new_folio;
	struct mm_struct *mm = vma->vm_mm;
	DEFINE_FOLIO_VMA_WALK(pvmw, old_folio, vma, addr, 0);
	int err;
	struct mmu_notifier_range range;
	pte_t pte;

	mmu_notifier_range_init(&range, MMU_NOTIFY_CLEAR, 0, mm, addr,
				addr + PAGE_SIZE);

	if (new_page) {
		new_folio = page_folio(new_page);
		err = mem_cgroup_charge(new_folio, vma->vm_mm, GFP_KERNEL);
		if (err)
			return err;
	}

	/* For folio_free_swap() below */
	folio_lock(old_folio);

	mmu_notifier_invalidate_range_start(&range);
	err = -EAGAIN;
	if (!page_vma_mapped_walk(&pvmw))
		goto unlock;
	VM_BUG_ON_PAGE(addr != pvmw.address, old_page);
	pte = ptep_get(pvmw.pte);

	/*
	 * Handle PFN swap PTES, such as device-exclusive ones, that actually
	 * map pages: simply trigger GUP again to fix it up.
	 */
	if (unlikely(!pte_present(pte))) {
		page_vma_mapped_walk_done(&pvmw);
		goto unlock;
	}

	if (new_page) {
		folio_get(new_folio);
		folio_add_new_anon_rmap(new_folio, vma, addr, RMAP_EXCLUSIVE);
		folio_add_lru_vma(new_folio, vma);
	} else
		/* no new page, just dec_mm_counter for old_page */
		dec_mm_counter(mm, MM_ANONPAGES);

	if (!folio_test_anon(old_folio)) {
		dec_mm_counter(mm, mm_counter_file(old_folio));
		inc_mm_counter(mm, MM_ANONPAGES);
	}

	flush_cache_page(vma, addr, pte_pfn(pte));
	ptep_clear_flush(vma, addr, pvmw.pte);
	if (new_page)
		set_pte_at(mm, addr, pvmw.pte,
			   mk_pte(new_page, vma->vm_page_prot));

	folio_remove_rmap_pte(old_folio, old_page, vma);
	if (!folio_mapped(old_folio))
		folio_free_swap(old_folio);
	page_vma_mapped_walk_done(&pvmw);
	folio_put(old_folio);

	err = 0;
 unlock:
	mmu_notifier_invalidate_range_end(&range);
	folio_unlock(old_folio);
	return err;
}

/**
 * is_swbp_insn - check if instruction is breakpoint instruction.
 * @insn: instruction to be checked.
 * Default implementation of is_swbp_insn
 * Returns true if @insn is a breakpoint instruction.
 */
bool __weak is_swbp_insn(uprobe_opcode_t *insn)
{
	return *insn == UPROBE_SWBP_INSN;
}

/**
 * is_trap_insn - check if instruction is breakpoint instruction.
 * @insn: instruction to be checked.
 * Default implementation of is_trap_insn
 * Returns true if @insn is a breakpoint instruction.
 *
 * This function is needed for the case where an architecture has multiple
 * trap instructions (like powerpc).
 */
bool __weak is_trap_insn(uprobe_opcode_t *insn)
{
	return is_swbp_insn(insn);
}

static void copy_from_page(struct page *page, unsigned long vaddr, void *dst, int len)
{
	void *kaddr = kmap_atomic(page);
	memcpy(dst, kaddr + (vaddr & ~PAGE_MASK), len);
	kunmap_atomic(kaddr);
}

static void copy_to_page(struct page *page, unsigned long vaddr, const void *src, int len)
{
	void *kaddr = kmap_atomic(page);
	memcpy(kaddr + (vaddr & ~PAGE_MASK), src, len);
	kunmap_atomic(kaddr);
}

static int verify_opcode(struct page *page, unsigned long vaddr, uprobe_opcode_t *new_opcode)
{
	uprobe_opcode_t old_opcode;
	bool is_swbp;

	/*
	 * Note: We only check if the old_opcode is UPROBE_SWBP_INSN here.
	 * We do not check if it is any other 'trap variant' which could
	 * be conditional trap instruction such as the one powerpc supports.
	 *
	 * The logic is that we do not care if the underlying instruction
	 * is a trap variant; uprobes always wins over any other (gdb)
	 * breakpoint.
	 */
	copy_from_page(page, vaddr, &old_opcode, UPROBE_SWBP_INSN_SIZE);
	is_swbp = is_swbp_insn(&old_opcode);

	if (is_swbp_insn(new_opcode)) {
		if (is_swbp)		/* register: already installed? */
			return 0;
	} else {
		if (!is_swbp)		/* unregister: was it changed by us? */
			return 0;
	}

	return 1;
}

static struct delayed_uprobe *
delayed_uprobe_check(struct uprobe *uprobe, struct mm_struct *mm)
{
	struct delayed_uprobe *du;

	list_for_each_entry(du, &delayed_uprobe_list, list)
		if (du->uprobe == uprobe && du->mm == mm)
			return du;
	return NULL;
}

static int delayed_uprobe_add(struct uprobe *uprobe, struct mm_struct *mm)
{
	struct delayed_uprobe *du;

	if (delayed_uprobe_check(uprobe, mm))
		return 0;

	du  = kzalloc(sizeof(*du), GFP_KERNEL);
	if (!du)
		return -ENOMEM;

	du->uprobe = uprobe;
	du->mm = mm;
	list_add(&du->list, &delayed_uprobe_list);
	return 0;
}

static void delayed_uprobe_delete(struct delayed_uprobe *du)
{
	if (WARN_ON(!du))
		return;
	list_del(&du->list);
	kfree(du);
}

static void delayed_uprobe_remove(struct uprobe *uprobe, struct mm_struct *mm)
{
	struct list_head *pos, *q;
	struct delayed_uprobe *du;

	if (!uprobe && !mm)
		return;

	list_for_each_safe(pos, q, &delayed_uprobe_list) {
		du = list_entry(pos, struct delayed_uprobe, list);

		if (uprobe && du->uprobe != uprobe)
			continue;
		if (mm && du->mm != mm)
			continue;

		delayed_uprobe_delete(du);
	}
}

static bool valid_ref_ctr_vma(struct uprobe *uprobe,
			      struct vm_area_struct *vma)
{
	unsigned long vaddr = offset_to_vaddr(vma, uprobe->ref_ctr_offset);

	return uprobe->ref_ctr_offset &&
		vma->vm_file &&
		file_inode(vma->vm_file) == uprobe->inode &&
		(vma->vm_flags & (VM_WRITE|VM_SHARED)) == VM_WRITE &&
		vma->vm_start <= vaddr &&
		vma->vm_end > vaddr;
}

static struct vm_area_struct *
find_ref_ctr_vma(struct uprobe *uprobe, struct mm_struct *mm)
{
	VMA_ITERATOR(vmi, mm, 0);
	struct vm_area_struct *tmp;

	for_each_vma(vmi, tmp)
		if (valid_ref_ctr_vma(uprobe, tmp))
			return tmp;

	return NULL;
}

static int
__update_ref_ctr(struct mm_struct *mm, unsigned long vaddr, short d)
{
	void *kaddr;
	struct page *page;
	int ret;
	short *ptr;

	if (!vaddr || !d)
		return -EINVAL;

	ret = get_user_pages_remote(mm, vaddr, 1,
				    FOLL_WRITE, &page, NULL);
	if (unlikely(ret <= 0)) {
		/*
		 * We are asking for 1 page. If get_user_pages_remote() fails,
		 * it may return 0, in that case we have to return error.
		 */
		return ret == 0 ? -EBUSY : ret;
	}

	kaddr = kmap_atomic(page);
	ptr = kaddr + (vaddr & ~PAGE_MASK);

	if (unlikely(*ptr + d < 0)) {
		pr_warn("ref_ctr going negative. vaddr: 0x%lx, "
			"curr val: %d, delta: %d\n", vaddr, *ptr, d);
		ret = -EINVAL;
		goto out;
	}

	*ptr += d;
	ret = 0;
out:
	kunmap_atomic(kaddr);
	put_page(page);
	return ret;
}

static void update_ref_ctr_warn(struct uprobe *uprobe,
				struct mm_struct *mm, short d)
{
	pr_warn("ref_ctr %s failed for inode: 0x%lx offset: "
		"0x%llx ref_ctr_offset: 0x%llx of mm: 0x%p\n",
		d > 0 ? "increment" : "decrement", uprobe->inode->i_ino,
		(unsigned long long) uprobe->offset,
		(unsigned long long) uprobe->ref_ctr_offset, mm);
}

static int update_ref_ctr(struct uprobe *uprobe, struct mm_struct *mm,
			  short d)
{
	struct vm_area_struct *rc_vma;
	unsigned long rc_vaddr;
	int ret = 0;

	rc_vma = find_ref_ctr_vma(uprobe, mm);

	if (rc_vma) {
		rc_vaddr = offset_to_vaddr(rc_vma, uprobe->ref_ctr_offset);
		ret = __update_ref_ctr(mm, rc_vaddr, d);
		if (ret)
			update_ref_ctr_warn(uprobe, mm, d);

		if (d > 0)
			return ret;
	}

	mutex_lock(&delayed_uprobe_lock);
	if (d > 0)
		ret = delayed_uprobe_add(uprobe, mm);
	else
		delayed_uprobe_remove(uprobe, mm);
	mutex_unlock(&delayed_uprobe_lock);

	return ret;
}

/*
 * NOTE:
 * Expect the breakpoint instruction to be the smallest size instruction for
 * the architecture. If an arch has variable length instruction and the
 * breakpoint instruction is not of the smallest length instruction
 * supported by that architecture then we need to modify is_trap_at_addr and
 * uprobe_write_opcode accordingly. This would never be a problem for archs
 * that have fixed length instructions.
 *
 * uprobe_write_opcode - write the opcode at a given virtual address.
 * @auprobe: arch specific probepoint information.
 * @mm: the probed process address space.
 * @vaddr: the virtual address to store the opcode.
 * @opcode: opcode to be written at @vaddr.
 *
 * Called with mm->mmap_lock held for read or write.
 * Return 0 (success) or a negative errno.
 */
int uprobe_write_opcode(struct arch_uprobe *auprobe, struct mm_struct *mm,
			unsigned long vaddr, uprobe_opcode_t opcode)
{
	struct uprobe *uprobe;
	struct page *old_page, *new_page;
	struct vm_area_struct *vma;
	int ret, is_register, ref_ctr_updated = 0;
	bool orig_page_huge = false;
	unsigned int gup_flags = FOLL_FORCE;

	is_register = is_swbp_insn(&opcode);
	uprobe = container_of(auprobe, struct uprobe, arch);

retry:
	if (is_register)
		gup_flags |= FOLL_SPLIT_PMD;
	/* Read the page with vaddr into memory */
	old_page = get_user_page_vma_remote(mm, vaddr, gup_flags, &vma);
	if (IS_ERR(old_page))
		return PTR_ERR(old_page);

	ret = verify_opcode(old_page, vaddr, &opcode);
	if (ret <= 0)
		goto put_old;

	if (is_zero_page(old_page)) {
		ret = -EINVAL;
		goto put_old;
	}

	if (WARN(!is_register && PageCompound(old_page),
		 "uprobe unregister should never work on compound page\n")) {
		ret = -EINVAL;
		goto put_old;
	}

	/* We are going to replace instruction, update ref_ctr. */
	if (!ref_ctr_updated && uprobe->ref_ctr_offset) {
		ret = update_ref_ctr(uprobe, mm, is_register ? 1 : -1);
		if (ret)
			goto put_old;

		ref_ctr_updated = 1;
	}

	ret = 0;
	if (!is_register && !PageAnon(old_page))
		goto put_old;

	ret = anon_vma_prepare(vma);
	if (ret)
		goto put_old;

	ret = -ENOMEM;
	new_page = alloc_page_vma(GFP_HIGHUSER_MOVABLE, vma, vaddr);
	if (!new_page)
		goto put_old;

	__SetPageUptodate(new_page);
	copy_highpage(new_page, old_page);
	copy_to_page(new_page, vaddr, &opcode, UPROBE_SWBP_INSN_SIZE);

	if (!is_register) {
		struct page *orig_page;
		pgoff_t index;

		VM_BUG_ON_PAGE(!PageAnon(old_page), old_page);

		index = vaddr_to_offset(vma, vaddr & PAGE_MASK) >> PAGE_SHIFT;
		orig_page = find_get_page(vma->vm_file->f_inode->i_mapping,
					  index);

		if (orig_page) {
			if (PageUptodate(orig_page) &&
			    pages_identical(new_page, orig_page)) {
				/* let go new_page */
				put_page(new_page);
				new_page = NULL;

				if (PageCompound(orig_page))
					orig_page_huge = true;
			}
			put_page(orig_page);
		}
	}

	ret = __replace_page(vma, vaddr & PAGE_MASK, old_page, new_page);
	if (new_page)
		put_page(new_page);
put_old:
	put_page(old_page);

	if (unlikely(ret == -EAGAIN))
		goto retry;

	/* Revert back reference counter if instruction update failed. */
	if (ret && is_register && ref_ctr_updated)
		update_ref_ctr(uprobe, mm, -1);

	/* try collapse pmd for compound page */
	if (!ret && orig_page_huge)
		collapse_pte_mapped_thp(mm, vaddr, false);

	return ret;
}

/**
 * set_swbp - store breakpoint at a given address.
 * @auprobe: arch specific probepoint information.
 * @mm: the probed process address space.
 * @vaddr: the virtual address to insert the opcode.
 *
 * For mm @mm, store the breakpoint instruction at @vaddr.
 * Return 0 (success) or a negative errno.
 */
int __weak set_swbp(struct arch_uprobe *auprobe, struct mm_struct *mm, unsigned long vaddr)
{
	return uprobe_write_opcode(auprobe, mm, vaddr, UPROBE_SWBP_INSN);
}

/**
 * set_orig_insn - Restore the original instruction.
 * @mm: the probed process address space.
 * @auprobe: arch specific probepoint information.
 * @vaddr: the virtual address to insert the opcode.
 *
 * For mm @mm, restore the original opcode (opcode) at @vaddr.
 * Return 0 (success) or a negative errno.
 */
int __weak
set_orig_insn(struct arch_uprobe *auprobe, struct mm_struct *mm, unsigned long vaddr)
{
	return uprobe_write_opcode(auprobe, mm, vaddr,
			*(uprobe_opcode_t *)&auprobe->insn);
}

/* uprobe should have guaranteed positive refcount */
static struct uprobe *get_uprobe(struct uprobe *uprobe)
{
	refcount_inc(&uprobe->ref);
	return uprobe;
}

/*
 * uprobe should have guaranteed lifetime, which can be either of:
 *   - caller already has refcount taken (and wants an extra one);
 *   - uprobe is RCU protected and won't be freed until after grace period;
 *   - we are holding uprobes_treelock (for read or write, doesn't matter).
 */
static struct uprobe *try_get_uprobe(struct uprobe *uprobe)
{
	if (refcount_inc_not_zero(&uprobe->ref))
		return uprobe;
	return NULL;
}

static inline bool uprobe_is_active(struct uprobe *uprobe)
{
	return !RB_EMPTY_NODE(&uprobe->rb_node);
}

static void uprobe_free_rcu_tasks_trace(struct rcu_head *rcu)
{
	struct uprobe *uprobe = container_of(rcu, struct uprobe, rcu);

	kfree(uprobe);
}

static void uprobe_free_srcu(struct rcu_head *rcu)
{
	struct uprobe *uprobe = container_of(rcu, struct uprobe, rcu);

	call_rcu_tasks_trace(&uprobe->rcu, uprobe_free_rcu_tasks_trace);
}

static void uprobe_free_deferred(struct work_struct *work)
{
	struct uprobe *uprobe = container_of(work, struct uprobe, work);

	write_lock(&uprobes_treelock);

	if (uprobe_is_active(uprobe)) {
		write_seqcount_begin(&uprobes_seqcount);
		rb_erase(&uprobe->rb_node, &uprobes_tree);
		write_seqcount_end(&uprobes_seqcount);
	}

	write_unlock(&uprobes_treelock);

	/*
	 * If application munmap(exec_vma) before uprobe_unregister()
	 * gets called, we don't get a chance to remove uprobe from
	 * delayed_uprobe_list from remove_breakpoint(). Do it here.
	 */
	mutex_lock(&delayed_uprobe_lock);
	delayed_uprobe_remove(uprobe, NULL);
	mutex_unlock(&delayed_uprobe_lock);

	/* start srcu -> rcu_tasks_trace -> kfree chain */
	call_srcu(&uretprobes_srcu, &uprobe->rcu, uprobe_free_srcu);
}

static void put_uprobe(struct uprobe *uprobe)
{
	if (!refcount_dec_and_test(&uprobe->ref))
		return;

	INIT_WORK(&uprobe->work, uprobe_free_deferred);
	schedule_work(&uprobe->work);
}

/* Initialize hprobe as SRCU-protected "leased" uprobe */
static void hprobe_init_leased(struct hprobe *hprobe, struct uprobe *uprobe, int srcu_idx)
{
	WARN_ON(!uprobe);
	hprobe->state = HPROBE_LEASED;
	hprobe->uprobe = uprobe;
	hprobe->srcu_idx = srcu_idx;
}

/* Initialize hprobe as refcounted ("stable") uprobe (uprobe can be NULL). */
static void hprobe_init_stable(struct hprobe *hprobe, struct uprobe *uprobe)
{
	hprobe->state = uprobe ? HPROBE_STABLE : HPROBE_GONE;
	hprobe->uprobe = uprobe;
	hprobe->srcu_idx = -1;
}

/*
 * hprobe_consume() fetches hprobe's underlying uprobe and detects whether
 * uprobe is SRCU protected or is refcounted. hprobe_consume() can be
 * used only once for a given hprobe.
 *
 * Caller has to call hprobe_finalize() and pass previous hprobe_state, so
 * that hprobe_finalize() can perform SRCU unlock or put uprobe, whichever
 * is appropriate.
 */
static inline struct uprobe *hprobe_consume(struct hprobe *hprobe, enum hprobe_state *hstate)
{
	*hstate = xchg(&hprobe->state, HPROBE_CONSUMED);
	switch (*hstate) {
	case HPROBE_LEASED:
	case HPROBE_STABLE:
		return hprobe->uprobe;
	case HPROBE_GONE:	/* uprobe is NULL, no SRCU */
	case HPROBE_CONSUMED:	/* uprobe was finalized already, do nothing */
		return NULL;
	default:
		WARN(1, "hprobe invalid state %d", *hstate);
		return NULL;
	}
}

/*
 * Reset hprobe state and, if hprobe was LEASED, release SRCU lock.
 * hprobe_finalize() can only be used from current context after
 * hprobe_consume() call (which determines uprobe and hstate value).
 */
static void hprobe_finalize(struct hprobe *hprobe, enum hprobe_state hstate)
{
	switch (hstate) {
	case HPROBE_LEASED:
		__srcu_read_unlock(&uretprobes_srcu, hprobe->srcu_idx);
		break;
	case HPROBE_STABLE:
		put_uprobe(hprobe->uprobe);
		break;
	case HPROBE_GONE:
	case HPROBE_CONSUMED:
		break;
	default:
		WARN(1, "hprobe invalid state %d", hstate);
		break;
	}
}

/*
 * Attempt to switch (atomically) uprobe from being SRCU protected (LEASED)
 * to refcounted (STABLE) state. Competes with hprobe_consume(); only one of
 * them can win the race to perform SRCU unlocking. Whoever wins must perform
 * SRCU unlock.
 *
 * Returns underlying valid uprobe or NULL, if there was no underlying uprobe
 * to begin with or we failed to bump its refcount and it's going away.
 *
 * Returned non-NULL uprobe can be still safely used within an ongoing SRCU
 * locked region. If `get` is true, it's guaranteed that non-NULL uprobe has
 * an extra refcount for caller to assume and use. Otherwise, it's not
 * guaranteed that returned uprobe has a positive refcount, so caller has to
 * attempt try_get_uprobe(), if it needs to preserve uprobe beyond current
 * SRCU lock region. See dup_utask().
 */
static struct uprobe *hprobe_expire(struct hprobe *hprobe, bool get)
{
	enum hprobe_state hstate;

	/*
	 * Caller should guarantee that return_instance is not going to be
	 * freed from under us. This can be achieved either through holding
	 * rcu_read_lock() or by owning return_instance in the first place.
	 *
	 * Underlying uprobe is itself protected from reuse by SRCU, so ensure
	 * SRCU lock is held properly.
	 */
	lockdep_assert(srcu_read_lock_held(&uretprobes_srcu));

	hstate = READ_ONCE(hprobe->state);
	switch (hstate) {
	case HPROBE_STABLE:
		/* uprobe has positive refcount, bump refcount, if necessary */
		return get ? get_uprobe(hprobe->uprobe) : hprobe->uprobe;
	case HPROBE_GONE:
		/*
		 * SRCU was unlocked earlier and we didn't manage to take
		 * uprobe refcnt, so it's effectively NULL
		 */
		return NULL;
	case HPROBE_CONSUMED:
		/*
		 * uprobe was consumed, so it's effectively NULL as far as
		 * uretprobe processing logic is concerned
		 */
		return NULL;
	case HPROBE_LEASED: {
		struct uprobe *uprobe = try_get_uprobe(hprobe->uprobe);
		/*
		 * Try to switch hprobe state, guarding against
		 * hprobe_consume() or another hprobe_expire() racing with us.
		 * Note, if we failed to get uprobe refcount, we use special
		 * HPROBE_GONE state to signal that hprobe->uprobe shouldn't
		 * be used as it will be freed after SRCU is unlocked.
		 */
		if (try_cmpxchg(&hprobe->state, &hstate, uprobe ? HPROBE_STABLE : HPROBE_GONE)) {
			/* We won the race, we are the ones to unlock SRCU */
			__srcu_read_unlock(&uretprobes_srcu, hprobe->srcu_idx);
			return get ? get_uprobe(uprobe) : uprobe;
		}

		/*
		 * We lost the race, undo refcount bump (if it ever happened),
		 * unless caller would like an extra refcount anyways.
		 */
		if (uprobe && !get)
			put_uprobe(uprobe);
		/*
		 * Even if hprobe_consume() or another hprobe_expire() wins
		 * the state update race and unlocks SRCU from under us, we
		 * still have a guarantee that underyling uprobe won't be
		 * freed due to ongoing caller's SRCU lock region, so we can
		 * return it regardless. Also, if `get` was true, we also have
		 * an extra ref for the caller to own. This is used in dup_utask().
		 */
		return uprobe;
	}
	default:
		WARN(1, "unknown hprobe state %d", hstate);
		return NULL;
	}
}

static __always_inline
int uprobe_cmp(const struct inode *l_inode, const loff_t l_offset,
	       const struct uprobe *r)
{
	if (l_inode < r->inode)
		return -1;

	if (l_inode > r->inode)
		return 1;

	if (l_offset < r->offset)
		return -1;

	if (l_offset > r->offset)
		return 1;

	return 0;
}

#define __node_2_uprobe(node) \
	rb_entry((node), struct uprobe, rb_node)

struct __uprobe_key {
	struct inode *inode;
	loff_t offset;
};

static inline int __uprobe_cmp_key(const void *key, const struct rb_node *b)
{
	const struct __uprobe_key *a = key;
	return uprobe_cmp(a->inode, a->offset, __node_2_uprobe(b));
}

static inline int __uprobe_cmp(struct rb_node *a, const struct rb_node *b)
{
	struct uprobe *u = __node_2_uprobe(a);
	return uprobe_cmp(u->inode, u->offset, __node_2_uprobe(b));
}

/*
 * Assumes being inside RCU protected region.
 * No refcount is taken on returned uprobe.
 */
static struct uprobe *find_uprobe_rcu(struct inode *inode, loff_t offset)
{
	struct __uprobe_key key = {
		.inode = inode,
		.offset = offset,
	};
	struct rb_node *node;
	unsigned int seq;

	lockdep_assert(rcu_read_lock_trace_held());

	do {
		seq = read_seqcount_begin(&uprobes_seqcount);
		node = rb_find_rcu(&key, &uprobes_tree, __uprobe_cmp_key);
		/*
		 * Lockless RB-tree lookups can result only in false negatives.
		 * If the element is found, it is correct and can be returned
		 * under RCU protection. If we find nothing, we need to
		 * validate that seqcount didn't change. If it did, we have to
		 * try again as we might have missed the element (false
		 * negative). If seqcount is unchanged, search truly failed.
		 */
		if (node)
			return __node_2_uprobe(node);
	} while (read_seqcount_retry(&uprobes_seqcount, seq));

	return NULL;
}

/*
 * Attempt to insert a new uprobe into uprobes_tree.
 *
 * If uprobe already exists (for given inode+offset), we just increment
 * refcount of previously existing uprobe.
 *
 * If not, a provided new instance of uprobe is inserted into the tree (with
 * assumed initial refcount == 1).
 *
 * In any case, we return a uprobe instance that ends up being in uprobes_tree.
 * Caller has to clean up new uprobe instance, if it ended up not being
 * inserted into the tree.
 *
 * We assume that uprobes_treelock is held for writing.
 */
static struct uprobe *__insert_uprobe(struct uprobe *uprobe)
{
	struct rb_node *node;
again:
	node = rb_find_add_rcu(&uprobe->rb_node, &uprobes_tree, __uprobe_cmp);
	if (node) {
		struct uprobe *u = __node_2_uprobe(node);

		if (!try_get_uprobe(u)) {
			rb_erase(node, &uprobes_tree);
			RB_CLEAR_NODE(&u->rb_node);
			goto again;
		}

		return u;
	}

	return uprobe;
}

/*
 * Acquire uprobes_treelock and insert uprobe into uprobes_tree
 * (or reuse existing one, see __insert_uprobe() comments above).
 */
static struct uprobe *insert_uprobe(struct uprobe *uprobe)
{
	struct uprobe *u;

	write_lock(&uprobes_treelock);
	write_seqcount_begin(&uprobes_seqcount);
	u = __insert_uprobe(uprobe);
	write_seqcount_end(&uprobes_seqcount);
	write_unlock(&uprobes_treelock);

	return u;
}

static void
ref_ctr_mismatch_warn(struct uprobe *cur_uprobe, struct uprobe *uprobe)
{
	pr_warn("ref_ctr_offset mismatch. inode: 0x%lx offset: 0x%llx "
		"ref_ctr_offset(old): 0x%llx ref_ctr_offset(new): 0x%llx\n",
		uprobe->inode->i_ino, (unsigned long long) uprobe->offset,
		(unsigned long long) cur_uprobe->ref_ctr_offset,
		(unsigned long long) uprobe->ref_ctr_offset);
}

static struct uprobe *alloc_uprobe(struct inode *inode, loff_t offset,
				   loff_t ref_ctr_offset)
{
	struct uprobe *uprobe, *cur_uprobe;

	uprobe = kzalloc(sizeof(struct uprobe), GFP_KERNEL);
	if (!uprobe)
		return ERR_PTR(-ENOMEM);

	uprobe->inode = inode;
	uprobe->offset = offset;
	uprobe->ref_ctr_offset = ref_ctr_offset;
	INIT_LIST_HEAD(&uprobe->consumers);
	init_rwsem(&uprobe->register_rwsem);
	init_rwsem(&uprobe->consumer_rwsem);
	RB_CLEAR_NODE(&uprobe->rb_node);
	refcount_set(&uprobe->ref, 1);

	/* add to uprobes_tree, sorted on inode:offset */
	cur_uprobe = insert_uprobe(uprobe);
	/* a uprobe exists for this inode:offset combination */
	if (cur_uprobe != uprobe) {
		if (cur_uprobe->ref_ctr_offset != uprobe->ref_ctr_offset) {
			ref_ctr_mismatch_warn(cur_uprobe, uprobe);
			put_uprobe(cur_uprobe);
			kfree(uprobe);
			return ERR_PTR(-EINVAL);
		}
		kfree(uprobe);
		uprobe = cur_uprobe;
	}

	return uprobe;
}

static void consumer_add(struct uprobe *uprobe, struct uprobe_consumer *uc)
{
	static atomic64_t id;

	down_write(&uprobe->consumer_rwsem);
	list_add_rcu(&uc->cons_node, &uprobe->consumers);
	uc->id = (__u64) atomic64_inc_return(&id);
	up_write(&uprobe->consumer_rwsem);
}

/*
 * For uprobe @uprobe, delete the consumer @uc.
 * Should never be called with consumer that's not part of @uprobe->consumers.
 */
static void consumer_del(struct uprobe *uprobe, struct uprobe_consumer *uc)
{
	down_write(&uprobe->consumer_rwsem);
	list_del_rcu(&uc->cons_node);
	up_write(&uprobe->consumer_rwsem);
}

static int __copy_insn(struct address_space *mapping, struct file *filp,
			void *insn, int nbytes, loff_t offset)
{
	struct page *page;
	/*
	 * Ensure that the page that has the original instruction is populated
	 * and in page-cache. If ->read_folio == NULL it must be shmem_mapping(),
	 * see uprobe_register().
	 */
	if (mapping->a_ops->read_folio)
		page = read_mapping_page(mapping, offset >> PAGE_SHIFT, filp);
	else
		page = shmem_read_mapping_page(mapping, offset >> PAGE_SHIFT);
	if (IS_ERR(page))
		return PTR_ERR(page);

	copy_from_page(page, offset, insn, nbytes);
	put_page(page);

	return 0;
}

static int copy_insn(struct uprobe *uprobe, struct file *filp)
{
	struct address_space *mapping = uprobe->inode->i_mapping;
	loff_t offs = uprobe->offset;
	void *insn = &uprobe->arch.insn;
	int size = sizeof(uprobe->arch.insn);
	int len, err = -EIO;

	/* Copy only available bytes, -EIO if nothing was read */
	do {
		if (offs >= i_size_read(uprobe->inode))
			break;

		len = min_t(int, size, PAGE_SIZE - (offs & ~PAGE_MASK));
		err = __copy_insn(mapping, filp, insn, len, offs);
		if (err)
			break;

		insn += len;
		offs += len;
		size -= len;
	} while (size);

	return err;
}

static int prepare_uprobe(struct uprobe *uprobe, struct file *file,
				struct mm_struct *mm, unsigned long vaddr)
{
	int ret = 0;

	if (test_bit(UPROBE_COPY_INSN, &uprobe->flags))
		return ret;

	/* TODO: move this into _register, until then we abuse this sem. */
	down_write(&uprobe->consumer_rwsem);
	if (test_bit(UPROBE_COPY_INSN, &uprobe->flags))
		goto out;

	ret = copy_insn(uprobe, file);
	if (ret)
		goto out;

	ret = -ENOTSUPP;
	if (is_trap_insn((uprobe_opcode_t *)&uprobe->arch.insn))
		goto out;

	ret = arch_uprobe_analyze_insn(&uprobe->arch, mm, vaddr);
	if (ret)
		goto out;

	smp_wmb(); /* pairs with the smp_rmb() in handle_swbp() */
	set_bit(UPROBE_COPY_INSN, &uprobe->flags);

 out:
	up_write(&uprobe->consumer_rwsem);

	return ret;
}

static inline bool consumer_filter(struct uprobe_consumer *uc, struct mm_struct *mm)
{
	return !uc->filter || uc->filter(uc, mm);
}

static bool filter_chain(struct uprobe *uprobe, struct mm_struct *mm)
{
	struct uprobe_consumer *uc;
	bool ret = false;

	down_read(&uprobe->consumer_rwsem);
	list_for_each_entry_rcu(uc, &uprobe->consumers, cons_node, rcu_read_lock_trace_held()) {
		ret = consumer_filter(uc, mm);
		if (ret)
			break;
	}
	up_read(&uprobe->consumer_rwsem);

	return ret;
}

static int
install_breakpoint(struct uprobe *uprobe, struct mm_struct *mm,
			struct vm_area_struct *vma, unsigned long vaddr)
{
	bool first_uprobe;
	int ret;

	ret = prepare_uprobe(uprobe, vma->vm_file, mm, vaddr);
	if (ret)
		return ret;

	/*
	 * set MMF_HAS_UPROBES in advance for uprobe_pre_sstep_notifier(),
	 * the task can hit this breakpoint right after __replace_page().
	 */
	first_uprobe = !test_bit(MMF_HAS_UPROBES, &mm->flags);
	if (first_uprobe)
		set_bit(MMF_HAS_UPROBES, &mm->flags);

	ret = set_swbp(&uprobe->arch, mm, vaddr);
	if (!ret)
		clear_bit(MMF_RECALC_UPROBES, &mm->flags);
	else if (first_uprobe)
		clear_bit(MMF_HAS_UPROBES, &mm->flags);

	return ret;
}

static int
remove_breakpoint(struct uprobe *uprobe, struct mm_struct *mm, unsigned long vaddr)
{
	set_bit(MMF_RECALC_UPROBES, &mm->flags);
	return set_orig_insn(&uprobe->arch, mm, vaddr);
}

struct map_info {
	struct map_info *next;
	struct mm_struct *mm;
	unsigned long vaddr;
};

static inline struct map_info *free_map_info(struct map_info *info)
{
	struct map_info *next = info->next;
	kfree(info);
	return next;
}

static struct map_info *
build_map_info(struct address_space *mapping, loff_t offset, bool is_register)
{
	unsigned long pgoff = offset >> PAGE_SHIFT;
	struct vm_area_struct *vma;
	struct map_info *curr = NULL;
	struct map_info *prev = NULL;
	struct map_info *info;
	int more = 0;

 again:
	i_mmap_lock_read(mapping);
	vma_interval_tree_foreach(vma, &mapping->i_mmap, pgoff, pgoff) {
		if (!valid_vma(vma, is_register))
			continue;

		if (!prev && !more) {
			/*
			 * Needs GFP_NOWAIT to avoid i_mmap_rwsem recursion through
			 * reclaim. This is optimistic, no harm done if it fails.
			 */
			prev = kmalloc(sizeof(struct map_info),
					GFP_NOWAIT | __GFP_NOMEMALLOC | __GFP_NOWARN);
			if (prev)
				prev->next = NULL;
		}
		if (!prev) {
			more++;
			continue;
		}

		if (!mmget_not_zero(vma->vm_mm))
			continue;

		info = prev;
		prev = prev->next;
		info->next = curr;
		curr = info;

		info->mm = vma->vm_mm;
		info->vaddr = offset_to_vaddr(vma, offset);
	}
	i_mmap_unlock_read(mapping);

	if (!more)
		goto out;

	prev = curr;
	while (curr) {
		mmput(curr->mm);
		curr = curr->next;
	}

	do {
		info = kmalloc(sizeof(struct map_info), GFP_KERNEL);
		if (!info) {
			curr = ERR_PTR(-ENOMEM);
			goto out;
		}
		info->next = prev;
		prev = info;
	} while (--more);

	goto again;
 out:
	while (prev)
		prev = free_map_info(prev);
	return curr;
}

static int
register_for_each_vma(struct uprobe *uprobe, struct uprobe_consumer *new)
{
	bool is_register = !!new;
	struct map_info *info;
	int err = 0;

	percpu_down_write(&dup_mmap_sem);
	info = build_map_info(uprobe->inode->i_mapping,
					uprobe->offset, is_register);
	if (IS_ERR(info)) {
		err = PTR_ERR(info);
		goto out;
	}

	while (info) {
		struct mm_struct *mm = info->mm;
		struct vm_area_struct *vma;

		if (err && is_register)
			goto free;
		/*
		 * We take mmap_lock for writing to avoid the race with
		 * find_active_uprobe_rcu() which takes mmap_lock for reading.
		 * Thus this install_breakpoint() can not make
		 * is_trap_at_addr() true right after find_uprobe_rcu()
		 * returns NULL in find_active_uprobe_rcu().
		 */
		mmap_write_lock(mm);
		if (check_stable_address_space(mm))
			goto unlock;

		vma = find_vma(mm, info->vaddr);
		if (!vma || !valid_vma(vma, is_register) ||
		    file_inode(vma->vm_file) != uprobe->inode)
			goto unlock;

		if (vma->vm_start > info->vaddr ||
		    vaddr_to_offset(vma, info->vaddr) != uprobe->offset)
			goto unlock;

		if (is_register) {
			/* consult only the "caller", new consumer. */
			if (consumer_filter(new, mm))
				err = install_breakpoint(uprobe, mm, vma, info->vaddr);
		} else if (test_bit(MMF_HAS_UPROBES, &mm->flags)) {
			if (!filter_chain(uprobe, mm))
				err |= remove_breakpoint(uprobe, mm, info->vaddr);
		}

 unlock:
		mmap_write_unlock(mm);
 free:
		mmput(mm);
		info = free_map_info(info);
	}
 out:
	percpu_up_write(&dup_mmap_sem);
	return err;
}

/**
 * uprobe_unregister_nosync - unregister an already registered probe.
 * @uprobe: uprobe to remove
 * @uc: identify which probe if multiple probes are colocated.
 */
void uprobe_unregister_nosync(struct uprobe *uprobe, struct uprobe_consumer *uc)
{
	int err;

	down_write(&uprobe->register_rwsem);
	consumer_del(uprobe, uc);
	err = register_for_each_vma(uprobe, NULL);
	up_write(&uprobe->register_rwsem);

	/* TODO : cant unregister? schedule a worker thread */
	if (unlikely(err)) {
		uprobe_warn(current, "unregister, leaking uprobe");
		return;
	}

	put_uprobe(uprobe);
}
EXPORT_SYMBOL_GPL(uprobe_unregister_nosync);

void uprobe_unregister_sync(void)
{
	/*
	 * Now that handler_chain() and handle_uretprobe_chain() iterate over
	 * uprobe->consumers list under RCU protection without holding
	 * uprobe->register_rwsem, we need to wait for RCU grace period to
	 * make sure that we can't call into just unregistered
	 * uprobe_consumer's callbacks anymore. If we don't do that, fast and
	 * unlucky enough caller can free consumer's memory and cause
	 * handler_chain() or handle_uretprobe_chain() to do an use-after-free.
	 */
	synchronize_rcu_tasks_trace();
	synchronize_srcu(&uretprobes_srcu);
}
EXPORT_SYMBOL_GPL(uprobe_unregister_sync);

/**
 * uprobe_register - register a probe
 * @inode: the file in which the probe has to be placed.
 * @offset: offset from the start of the file.
 * @ref_ctr_offset: offset of SDT marker / reference counter
 * @uc: information on howto handle the probe..
 *
 * Apart from the access refcount, uprobe_register() takes a creation
 * refcount (thro alloc_uprobe) if and only if this @uprobe is getting
 * inserted into the rbtree (i.e first consumer for a @inode:@offset
 * tuple).  Creation refcount stops uprobe_unregister from freeing the
 * @uprobe even before the register operation is complete. Creation
 * refcount is released when the last @uc for the @uprobe
 * unregisters. Caller of uprobe_register() is required to keep @inode
 * (and the containing mount) referenced.
 *
 * Return: pointer to the new uprobe on success or an ERR_PTR on failure.
 */
struct uprobe *uprobe_register(struct inode *inode,
				loff_t offset, loff_t ref_ctr_offset,
				struct uprobe_consumer *uc)
{
	struct uprobe *uprobe;
	int ret;

	/* Uprobe must have at least one set consumer */
	if (!uc->handler && !uc->ret_handler)
		return ERR_PTR(-EINVAL);

	/* copy_insn() uses read_mapping_page() or shmem_read_mapping_page() */
	if (!inode->i_mapping->a_ops->read_folio &&
	    !shmem_mapping(inode->i_mapping))
		return ERR_PTR(-EIO);
	/* Racy, just to catch the obvious mistakes */
	if (offset > i_size_read(inode))
		return ERR_PTR(-EINVAL);

	/*
	 * This ensures that copy_from_page(), copy_to_page() and
	 * __update_ref_ctr() can't cross page boundary.
	 */
	if (!IS_ALIGNED(offset, UPROBE_SWBP_INSN_SIZE))
		return ERR_PTR(-EINVAL);
	if (!IS_ALIGNED(ref_ctr_offset, sizeof(short)))
		return ERR_PTR(-EINVAL);

	uprobe = alloc_uprobe(inode, offset, ref_ctr_offset);
	if (IS_ERR(uprobe))
		return uprobe;

	down_write(&uprobe->register_rwsem);
	consumer_add(uprobe, uc);
	ret = register_for_each_vma(uprobe, uc);
	up_write(&uprobe->register_rwsem);

	if (ret) {
		uprobe_unregister_nosync(uprobe, uc);
		/*
		 * Registration might have partially succeeded, so we can have
		 * this consumer being called right at this time. We need to
		 * sync here. It's ok, it's unlikely slow path.
		 */
		uprobe_unregister_sync();
		return ERR_PTR(ret);
	}

	return uprobe;
}
EXPORT_SYMBOL_GPL(uprobe_register);

/**
 * uprobe_apply - add or remove the breakpoints according to @uc->filter
 * @uprobe: uprobe which "owns" the breakpoint
 * @uc: consumer which wants to add more or remove some breakpoints
 * @add: add or remove the breakpoints
 * Return: 0 on success or negative error code.
 */
int uprobe_apply(struct uprobe *uprobe, struct uprobe_consumer *uc, bool add)
{
	struct uprobe_consumer *con;
	int ret = -ENOENT;

	down_write(&uprobe->register_rwsem);

	rcu_read_lock_trace();
	list_for_each_entry_rcu(con, &uprobe->consumers, cons_node, rcu_read_lock_trace_held()) {
		if (con == uc) {
			ret = register_for_each_vma(uprobe, add ? uc : NULL);
			break;
		}
	}
	rcu_read_unlock_trace();

	up_write(&uprobe->register_rwsem);

	return ret;
}

static int unapply_uprobe(struct uprobe *uprobe, struct mm_struct *mm)
{
	VMA_ITERATOR(vmi, mm, 0);
	struct vm_area_struct *vma;
	int err = 0;

	mmap_read_lock(mm);
	for_each_vma(vmi, vma) {
		unsigned long vaddr;
		loff_t offset;

		if (!valid_vma(vma, false) ||
		    file_inode(vma->vm_file) != uprobe->inode)
			continue;

		offset = (loff_t)vma->vm_pgoff << PAGE_SHIFT;
		if (uprobe->offset <  offset ||
		    uprobe->offset >= offset + vma->vm_end - vma->vm_start)
			continue;

		vaddr = offset_to_vaddr(vma, uprobe->offset);
		err |= remove_breakpoint(uprobe, mm, vaddr);
	}
	mmap_read_unlock(mm);

	return err;
}

static struct rb_node *
find_node_in_range(struct inode *inode, loff_t min, loff_t max)
{
	struct rb_node *n = uprobes_tree.rb_node;

	while (n) {
		struct uprobe *u = rb_entry(n, struct uprobe, rb_node);

		if (inode < u->inode) {
			n = n->rb_left;
		} else if (inode > u->inode) {
			n = n->rb_right;
		} else {
			if (max < u->offset)
				n = n->rb_left;
			else if (min > u->offset)
				n = n->rb_right;
			else
				break;
		}
	}

	return n;
}

/*
 * For a given range in vma, build a list of probes that need to be inserted.
 */
static void build_probe_list(struct inode *inode,
				struct vm_area_struct *vma,
				unsigned long start, unsigned long end,
				struct list_head *head)
{
	loff_t min, max;
	struct rb_node *n, *t;
	struct uprobe *u;

	INIT_LIST_HEAD(head);
	min = vaddr_to_offset(vma, start);
	max = min + (end - start) - 1;

	read_lock(&uprobes_treelock);
	n = find_node_in_range(inode, min, max);
	if (n) {
		for (t = n; t; t = rb_prev(t)) {
			u = rb_entry(t, struct uprobe, rb_node);
			if (u->inode != inode || u->offset < min)
				break;
			/* if uprobe went away, it's safe to ignore it */
			if (try_get_uprobe(u))
				list_add(&u->pending_list, head);
		}
		for (t = n; (t = rb_next(t)); ) {
			u = rb_entry(t, struct uprobe, rb_node);
			if (u->inode != inode || u->offset > max)
				break;
			/* if uprobe went away, it's safe to ignore it */
			if (try_get_uprobe(u))
				list_add(&u->pending_list, head);
		}
	}
	read_unlock(&uprobes_treelock);
}

/* @vma contains reference counter, not the probed instruction. */
static int delayed_ref_ctr_inc(struct vm_area_struct *vma)
{
	struct list_head *pos, *q;
	struct delayed_uprobe *du;
	unsigned long vaddr;
	int ret = 0, err = 0;

	mutex_lock(&delayed_uprobe_lock);
	list_for_each_safe(pos, q, &delayed_uprobe_list) {
		du = list_entry(pos, struct delayed_uprobe, list);

		if (du->mm != vma->vm_mm ||
		    !valid_ref_ctr_vma(du->uprobe, vma))
			continue;

		vaddr = offset_to_vaddr(vma, du->uprobe->ref_ctr_offset);
		ret = __update_ref_ctr(vma->vm_mm, vaddr, 1);
		if (ret) {
			update_ref_ctr_warn(du->uprobe, vma->vm_mm, 1);
			if (!err)
				err = ret;
		}
		delayed_uprobe_delete(du);
	}
	mutex_unlock(&delayed_uprobe_lock);
	return err;
}

/*
 * Called from mmap_region/vma_merge with mm->mmap_lock acquired.
 *
 * Currently we ignore all errors and always return 0, the callers
 * can't handle the failure anyway.
 */
int uprobe_mmap(struct vm_area_struct *vma)
{
	struct list_head tmp_list;
	struct uprobe *uprobe, *u;
	struct inode *inode;

	if (no_uprobe_events())
		return 0;

	if (vma->vm_file &&
	    (vma->vm_flags & (VM_WRITE|VM_SHARED)) == VM_WRITE &&
	    test_bit(MMF_HAS_UPROBES, &vma->vm_mm->flags))
		delayed_ref_ctr_inc(vma);

	if (!valid_vma(vma, true))
		return 0;

	inode = file_inode(vma->vm_file);
	if (!inode)
		return 0;

	mutex_lock(uprobes_mmap_hash(inode));
	build_probe_list(inode, vma, vma->vm_start, vma->vm_end, &tmp_list);
	/*
	 * We can race with uprobe_unregister(), this uprobe can be already
	 * removed. But in this case filter_chain() must return false, all
	 * consumers have gone away.
	 */
	list_for_each_entry_safe(uprobe, u, &tmp_list, pending_list) {
		if (!fatal_signal_pending(current) &&
		    filter_chain(uprobe, vma->vm_mm)) {
			unsigned long vaddr = offset_to_vaddr(vma, uprobe->offset);
			install_breakpoint(uprobe, vma->vm_mm, vma, vaddr);
		}
		put_uprobe(uprobe);
	}
	mutex_unlock(uprobes_mmap_hash(inode));

	return 0;
}

static bool
vma_has_uprobes(struct vm_area_struct *vma, unsigned long start, unsigned long end)
{
	loff_t min, max;
	struct inode *inode;
	struct rb_node *n;

	inode = file_inode(vma->vm_file);

	min = vaddr_to_offset(vma, start);
	max = min + (end - start) - 1;

	read_lock(&uprobes_treelock);
	n = find_node_in_range(inode, min, max);
	read_unlock(&uprobes_treelock);

	return !!n;
}

/*
 * Called in context of a munmap of a vma.
 */
void uprobe_munmap(struct vm_area_struct *vma, unsigned long start, unsigned long end)
{
	if (no_uprobe_events() || !valid_vma(vma, false))
		return;

	if (!atomic_read(&vma->vm_mm->mm_users)) /* called by mmput() ? */
		return;

	if (!test_bit(MMF_HAS_UPROBES, &vma->vm_mm->flags) ||
	     test_bit(MMF_RECALC_UPROBES, &vma->vm_mm->flags))
		return;

	if (vma_has_uprobes(vma, start, end))
		set_bit(MMF_RECALC_UPROBES, &vma->vm_mm->flags);
}

static vm_fault_t xol_fault(const struct vm_special_mapping *sm,
			    struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct xol_area *area = vma->vm_mm->uprobes_state.xol_area;

	vmf->page = area->page;
	get_page(vmf->page);
	return 0;
}

static int xol_mremap(const struct vm_special_mapping *sm, struct vm_area_struct *new_vma)
{
	return -EPERM;
}

static const struct vm_special_mapping xol_mapping = {
	.name = "[uprobes]",
	.fault = xol_fault,
	.mremap = xol_mremap,
};

/* Slot allocation for XOL */
static int xol_add_vma(struct mm_struct *mm, struct xol_area *area)
{
	struct vm_area_struct *vma;
	int ret;

	if (mmap_write_lock_killable(mm))
		return -EINTR;

	if (mm->uprobes_state.xol_area) {
		ret = -EALREADY;
		goto fail;
	}

	if (!area->vaddr) {
		/* Try to map as high as possible, this is only a hint. */
		area->vaddr = get_unmapped_area(NULL, TASK_SIZE - PAGE_SIZE,
						PAGE_SIZE, 0, 0);
		if (IS_ERR_VALUE(area->vaddr)) {
			ret = area->vaddr;
			goto fail;
		}
	}

	vma = _install_special_mapping(mm, area->vaddr, PAGE_SIZE,
				VM_EXEC|VM_MAYEXEC|VM_DONTCOPY|VM_IO|
				VM_SEALED_SYSMAP,
				&xol_mapping);
	if (IS_ERR(vma)) {
		ret = PTR_ERR(vma);
		goto fail;
	}

	ret = 0;
	/* pairs with get_xol_area() */
	smp_store_release(&mm->uprobes_state.xol_area, area); /* ^^^ */
 fail:
	mmap_write_unlock(mm);

	return ret;
}

void * __weak arch_uprobe_trampoline(unsigned long *psize)
{
	static uprobe_opcode_t insn = UPROBE_SWBP_INSN;

	*psize = UPROBE_SWBP_INSN_SIZE;
	return &insn;
}

static struct xol_area *__create_xol_area(unsigned long vaddr)
{
	struct mm_struct *mm = current->mm;
	unsigned long insns_size;
	struct xol_area *area;
	void *insns;

	area = kzalloc(sizeof(*area), GFP_KERNEL);
	if (unlikely(!area))
		goto out;

	area->bitmap = kcalloc(BITS_TO_LONGS(UINSNS_PER_PAGE), sizeof(long),
			       GFP_KERNEL);
	if (!area->bitmap)
		goto free_area;

	area->page = alloc_page(GFP_HIGHUSER | __GFP_ZERO);
	if (!area->page)
		goto free_bitmap;

	area->vaddr = vaddr;
	init_waitqueue_head(&area->wq);
	/* Reserve the 1st slot for get_trampoline_vaddr() */
	set_bit(0, area->bitmap);
	insns = arch_uprobe_trampoline(&insns_size);
	arch_uprobe_copy_ixol(area->page, 0, insns, insns_size);

	if (!xol_add_vma(mm, area))
		return area;

	__free_page(area->page);
 free_bitmap:
	kfree(area->bitmap);
 free_area:
	kfree(area);
 out:
	return NULL;
}

/*
 * get_xol_area - Allocate process's xol_area if necessary.
 * This area will be used for storing instructions for execution out of line.
 *
 * Returns the allocated area or NULL.
 */
static struct xol_area *get_xol_area(void)
{
	struct mm_struct *mm = current->mm;
	struct xol_area *area;

	if (!mm->uprobes_state.xol_area)
		__create_xol_area(0);

	/* Pairs with xol_add_vma() smp_store_release() */
	area = READ_ONCE(mm->uprobes_state.xol_area); /* ^^^ */
	return area;
}

/*
 * uprobe_clear_state - Free the area allocated for slots.
 */
void uprobe_clear_state(struct mm_struct *mm)
{
	struct xol_area *area = mm->uprobes_state.xol_area;

	mutex_lock(&delayed_uprobe_lock);
	delayed_uprobe_remove(NULL, mm);
	mutex_unlock(&delayed_uprobe_lock);

	if (!area)
		return;

	put_page(area->page);
	kfree(area->bitmap);
	kfree(area);
}

void uprobe_start_dup_mmap(void)
{
	percpu_down_read(&dup_mmap_sem);
}

void uprobe_end_dup_mmap(void)
{
	percpu_up_read(&dup_mmap_sem);
}

void uprobe_dup_mmap(struct mm_struct *oldmm, struct mm_struct *newmm)
{
	if (test_bit(MMF_HAS_UPROBES, &oldmm->flags)) {
		set_bit(MMF_HAS_UPROBES, &newmm->flags);
		/* unconditionally, dup_mmap() skips VM_DONTCOPY vmas */
		set_bit(MMF_RECALC_UPROBES, &newmm->flags);
	}
}

static unsigned long xol_get_slot_nr(struct xol_area *area)
{
	unsigned long slot_nr;

	slot_nr = find_first_zero_bit(area->bitmap, UINSNS_PER_PAGE);
	if (slot_nr < UINSNS_PER_PAGE) {
		if (!test_and_set_bit(slot_nr, area->bitmap))
			return slot_nr;
	}

	return UINSNS_PER_PAGE;
}

/*
 * xol_get_insn_slot - allocate a slot for xol.
 */
static bool xol_get_insn_slot(struct uprobe *uprobe, struct uprobe_task *utask)
{
	struct xol_area *area = get_xol_area();
	unsigned long slot_nr;

	if (!area)
		return false;

	wait_event(area->wq, (slot_nr = xol_get_slot_nr(area)) < UINSNS_PER_PAGE);

	utask->xol_vaddr = area->vaddr + slot_nr * UPROBE_XOL_SLOT_BYTES;
	arch_uprobe_copy_ixol(area->page, utask->xol_vaddr,
			      &uprobe->arch.ixol, sizeof(uprobe->arch.ixol));
	return true;
}

/*
 * xol_free_insn_slot - free the slot allocated by xol_get_insn_slot()
 */
static void xol_free_insn_slot(struct uprobe_task *utask)
{
	struct xol_area *area = current->mm->uprobes_state.xol_area;
	unsigned long offset = utask->xol_vaddr - area->vaddr;
	unsigned int slot_nr;

	utask->xol_vaddr = 0;
	/* xol_vaddr must fit into [area->vaddr, area->vaddr + PAGE_SIZE) */
	if (WARN_ON_ONCE(offset >= PAGE_SIZE))
		return;

	slot_nr = offset / UPROBE_XOL_SLOT_BYTES;
	clear_bit(slot_nr, area->bitmap);
	smp_mb__after_atomic(); /* pairs with prepare_to_wait() */
	if (waitqueue_active(&area->wq))
		wake_up(&area->wq);
}

void __weak arch_uprobe_copy_ixol(struct page *page, unsigned long vaddr,
				  void *src, unsigned long len)
{
	/* Initialize the slot */
	copy_to_page(page, vaddr, src, len);

	/*
	 * We probably need flush_icache_user_page() but it needs vma.
	 * This should work on most of architectures by default. If
	 * architecture needs to do something different it can define
	 * its own version of the function.
	 */
	flush_dcache_page(page);
}

/**
 * uprobe_get_swbp_addr - compute address of swbp given post-swbp regs
 * @regs: Reflects the saved state of the task after it has hit a breakpoint
 * instruction.
 * Return the address of the breakpoint instruction.
 */
unsigned long __weak uprobe_get_swbp_addr(struct pt_regs *regs)
{
	return instruction_pointer(regs) - UPROBE_SWBP_INSN_SIZE;
}

unsigned long uprobe_get_trap_addr(struct pt_regs *regs)
{
	struct uprobe_task *utask = current->utask;

	if (unlikely(utask && utask->active_uprobe))
		return utask->vaddr;

	return instruction_pointer(regs);
}

static void ri_pool_push(struct uprobe_task *utask, struct return_instance *ri)
{
	ri->cons_cnt = 0;
	ri->next = utask->ri_pool;
	utask->ri_pool = ri;
}

static struct return_instance *ri_pool_pop(struct uprobe_task *utask)
{
	struct return_instance *ri = utask->ri_pool;

	if (likely(ri))
		utask->ri_pool = ri->next;

	return ri;
}

static void ri_free(struct return_instance *ri)
{
	kfree(ri->extra_consumers);
	kfree_rcu(ri, rcu);
}

static void free_ret_instance(struct uprobe_task *utask,
			      struct return_instance *ri, bool cleanup_hprobe)
{
	unsigned seq;

	if (cleanup_hprobe) {
		enum hprobe_state hstate;

		(void)hprobe_consume(&ri->hprobe, &hstate);
		hprobe_finalize(&ri->hprobe, hstate);
	}

	/*
	 * At this point return_instance is unlinked from utask's
	 * return_instances list and this has become visible to ri_timer().
	 * If seqcount now indicates that ri_timer's return instance
	 * processing loop isn't active, we can return ri into the pool of
	 * to-be-reused return instances for future uretprobes. If ri_timer()
	 * happens to be running right now, though, we fallback to safety and
	 * just perform RCU-delated freeing of ri.
	 */
	if (raw_seqcount_try_begin(&utask->ri_seqcount, seq)) {
		/* immediate reuse of ri without RCU GP is OK */
		ri_pool_push(utask, ri);
	} else {
		/* we might be racing with ri_timer(), so play it safe */
		ri_free(ri);
	}
}

/*
 * Called with no locks held.
 * Called in context of an exiting or an exec-ing thread.
 */
void uprobe_free_utask(struct task_struct *t)
{
	struct uprobe_task *utask = t->utask;
	struct return_instance *ri, *ri_next;

	if (!utask)
		return;

	t->utask = NULL;
	WARN_ON_ONCE(utask->active_uprobe || utask->xol_vaddr);

	timer_delete_sync(&utask->ri_timer);

	ri = utask->return_instances;
	while (ri) {
		ri_next = ri->next;
		free_ret_instance(utask, ri, true /* cleanup_hprobe */);
		ri = ri_next;
	}

	/* free_ret_instance() above might add to ri_pool, so this loop should come last */
	ri = utask->ri_pool;
	while (ri) {
		ri_next = ri->next;
		ri_free(ri);
		ri = ri_next;
	}

	kfree(utask);
}

#define RI_TIMER_PERIOD (HZ / 10) /* 100 ms */

#define for_each_ret_instance_rcu(pos, head) \
	for (pos = rcu_dereference_raw(head); pos; pos = rcu_dereference_raw(pos->next))

static void ri_timer(struct timer_list *timer)
{
	struct uprobe_task *utask = container_of(timer, struct uprobe_task, ri_timer);
	struct return_instance *ri;

	/* SRCU protects uprobe from reuse for the cmpxchg() inside hprobe_expire(). */
	guard(srcu)(&uretprobes_srcu);
	/* RCU protects return_instance from freeing. */
	guard(rcu)();

	write_seqcount_begin(&utask->ri_seqcount);

	for_each_ret_instance_rcu(ri, utask->return_instances)
		hprobe_expire(&ri->hprobe, false);

	write_seqcount_end(&utask->ri_seqcount);
}

static struct uprobe_task *alloc_utask(void)
{
	struct uprobe_task *utask;

	utask = kzalloc(sizeof(*utask), GFP_KERNEL);
	if (!utask)
		return NULL;

	timer_setup(&utask->ri_timer, ri_timer, 0);
	seqcount_init(&utask->ri_seqcount);

	return utask;
}

/*
 * Allocate a uprobe_task object for the task if necessary.
 * Called when the thread hits a breakpoint.
 *
 * Returns:
 * - pointer to new uprobe_task on success
 * - NULL otherwise
 */
static struct uprobe_task *get_utask(void)
{
	if (!current->utask)
		current->utask = alloc_utask();
	return current->utask;
}

static struct return_instance *alloc_return_instance(struct uprobe_task *utask)
{
	struct return_instance *ri;

	ri = ri_pool_pop(utask);
	if (ri)
		return ri;

	ri = kzalloc(sizeof(*ri), GFP_KERNEL);
	if (!ri)
		return ZERO_SIZE_PTR;

	return ri;
}

static struct return_instance *dup_return_instance(struct return_instance *old)
{
	struct return_instance *ri;

	ri = kmemdup(old, sizeof(*ri), GFP_KERNEL);
	if (!ri)
		return NULL;

	if (unlikely(old->cons_cnt > 1)) {
		ri->extra_consumers = kmemdup(old->extra_consumers,
					      sizeof(ri->extra_consumers[0]) * (old->cons_cnt - 1),
					      GFP_KERNEL);
		if (!ri->extra_consumers) {
			kfree(ri);
			return NULL;
		}
	}

	return ri;
}

static int dup_utask(struct task_struct *t, struct uprobe_task *o_utask)
{
	struct uprobe_task *n_utask;
	struct return_instance **p, *o, *n;
	struct uprobe *uprobe;

	n_utask = alloc_utask();
	if (!n_utask)
		return -ENOMEM;
	t->utask = n_utask;

	/* protect uprobes from freeing, we'll need try_get_uprobe() them */
	guard(srcu)(&uretprobes_srcu);

	p = &n_utask->return_instances;
	for (o = o_utask->return_instances; o; o = o->next) {
		n = dup_return_instance(o);
		if (!n)
			return -ENOMEM;

		/* if uprobe is non-NULL, we'll have an extra refcount for uprobe */
		uprobe = hprobe_expire(&o->hprobe, true);

		/*
		 * New utask will have stable properly refcounted uprobe or
		 * NULL. Even if we failed to get refcounted uprobe, we still
		 * need to preserve full set of return_instances for proper
		 * uretprobe handling and nesting in forked task.
		 */
		hprobe_init_stable(&n->hprobe, uprobe);

		n->next = NULL;
		rcu_assign_pointer(*p, n);
		p = &n->next;

		n_utask->depth++;
	}

	return 0;
}

static void dup_xol_work(struct callback_head *work)
{
	if (current->flags & PF_EXITING)
		return;

	if (!__create_xol_area(current->utask->dup_xol_addr) &&
			!fatal_signal_pending(current))
		uprobe_warn(current, "dup xol area");
}

/*
 * Called in context of a new clone/fork from copy_process.
 */
void uprobe_copy_process(struct task_struct *t, unsigned long flags)
{
	struct uprobe_task *utask = current->utask;
	struct mm_struct *mm = current->mm;
	struct xol_area *area;

	t->utask = NULL;

	if (!utask || !utask->return_instances)
		return;

	if (mm == t->mm && !(flags & CLONE_VFORK))
		return;

	if (dup_utask(t, utask))
		return uprobe_warn(t, "dup ret instances");

	/* The task can fork() after dup_xol_work() fails */
	area = mm->uprobes_state.xol_area;
	if (!area)
		return uprobe_warn(t, "dup xol area");

	if (mm == t->mm)
		return;

	t->utask->dup_xol_addr = area->vaddr;
	init_task_work(&t->utask->dup_xol_work, dup_xol_work);
	task_work_add(t, &t->utask->dup_xol_work, TWA_RESUME);
}

/*
 * Current area->vaddr notion assume the trampoline address is always
 * equal area->vaddr.
 *
 * Returns -1 in case the xol_area is not allocated.
 */
unsigned long uprobe_get_trampoline_vaddr(void)
{
	unsigned long trampoline_vaddr = UPROBE_NO_TRAMPOLINE_VADDR;
	struct xol_area *area;

	/* Pairs with xol_add_vma() smp_store_release() */
	area = READ_ONCE(current->mm->uprobes_state.xol_area); /* ^^^ */
	if (area)
		trampoline_vaddr = area->vaddr;

	return trampoline_vaddr;
}

static void cleanup_return_instances(struct uprobe_task *utask, bool chained,
					struct pt_regs *regs)
{
	struct return_instance *ri = utask->return_instances, *ri_next;
	enum rp_check ctx = chained ? RP_CHECK_CHAIN_CALL : RP_CHECK_CALL;

	while (ri && !arch_uretprobe_is_alive(ri, ctx, regs)) {
		ri_next = ri->next;
		rcu_assign_pointer(utask->return_instances, ri_next);
		utask->depth--;

		free_ret_instance(utask, ri, true /* cleanup_hprobe */);
		ri = ri_next;
	}
}

static void prepare_uretprobe(struct uprobe *uprobe, struct pt_regs *regs,
			      struct return_instance *ri)
{
	struct uprobe_task *utask = current->utask;
	unsigned long orig_ret_vaddr, trampoline_vaddr;
	bool chained;
	int srcu_idx;

	if (!get_xol_area())
		goto free;

	if (utask->depth >= MAX_URETPROBE_DEPTH) {
		printk_ratelimited(KERN_INFO "uprobe: omit uretprobe due to"
				" nestedness limit pid/tgid=%d/%d\n",
				current->pid, current->tgid);
		goto free;
	}

	trampoline_vaddr = uprobe_get_trampoline_vaddr();
	orig_ret_vaddr = arch_uretprobe_hijack_return_addr(trampoline_vaddr, regs);
	if (orig_ret_vaddr == -1)
		goto free;

	/* drop the entries invalidated by longjmp() */
	chained = (orig_ret_vaddr == trampoline_vaddr);
	cleanup_return_instances(utask, chained, regs);

	/*
	 * We don't want to keep trampoline address in stack, rather keep the
	 * original return address of first caller thru all the consequent
	 * instances. This also makes breakpoint unwrapping easier.
	 */
	if (chained) {
		if (!utask->return_instances) {
			/*
			 * This situation is not possible. Likely we have an
			 * attack from user-space.
			 */
			uprobe_warn(current, "handle tail call");
			goto free;
		}
		orig_ret_vaddr = utask->return_instances->orig_ret_vaddr;
	}

	/* __srcu_read_lock() because SRCU lock survives switch to user space */
	srcu_idx = __srcu_read_lock(&uretprobes_srcu);

	ri->func = instruction_pointer(regs);
	ri->stack = user_stack_pointer(regs);
	ri->orig_ret_vaddr = orig_ret_vaddr;
	ri->chained = chained;

	utask->depth++;

	hprobe_init_leased(&ri->hprobe, uprobe, srcu_idx);
	ri->next = utask->return_instances;
	rcu_assign_pointer(utask->return_instances, ri);

	mod_timer(&utask->ri_timer, jiffies + RI_TIMER_PERIOD);

	return;
free:
	ri_free(ri);
}

/* Prepare to single-step probed instruction out of line. */
static int
pre_ssout(struct uprobe *uprobe, struct pt_regs *regs, unsigned long bp_vaddr)
{
	struct uprobe_task *utask = current->utask;
	int err;

	if (!try_get_uprobe(uprobe))
		return -EINVAL;

	if (!xol_get_insn_slot(uprobe, utask)) {
		err = -ENOMEM;
		goto err_out;
	}

	utask->vaddr = bp_vaddr;
	err = arch_uprobe_pre_xol(&uprobe->arch, regs);
	if (unlikely(err)) {
		xol_free_insn_slot(utask);
		goto err_out;
	}

	utask->active_uprobe = uprobe;
	utask->state = UTASK_SSTEP;
	return 0;
err_out:
	put_uprobe(uprobe);
	return err;
}

/*
 * If we are singlestepping, then ensure this thread is not connected to
 * non-fatal signals until completion of singlestep.  When xol insn itself
 * triggers the signal,  restart the original insn even if the task is
 * already SIGKILL'ed (since coredump should report the correct ip).  This
 * is even more important if the task has a handler for SIGSEGV/etc, The
 * _same_ instruction should be repeated again after return from the signal
 * handler, and SSTEP can never finish in this case.
 */
bool uprobe_deny_signal(void)
{
	struct task_struct *t = current;
	struct uprobe_task *utask = t->utask;

	if (likely(!utask || !utask->active_uprobe))
		return false;

	WARN_ON_ONCE(utask->state != UTASK_SSTEP);

	if (task_sigpending(t)) {
		utask->signal_denied = true;
		clear_tsk_thread_flag(t, TIF_SIGPENDING);

		if (__fatal_signal_pending(t) || arch_uprobe_xol_was_trapped(t)) {
			utask->state = UTASK_SSTEP_TRAPPED;
			set_tsk_thread_flag(t, TIF_UPROBE);
		}
	}

	return true;
}

static void mmf_recalc_uprobes(struct mm_struct *mm)
{
	VMA_ITERATOR(vmi, mm, 0);
	struct vm_area_struct *vma;

	for_each_vma(vmi, vma) {
		if (!valid_vma(vma, false))
			continue;
		/*
		 * This is not strictly accurate, we can race with
		 * uprobe_unregister() and see the already removed
		 * uprobe if delete_uprobe() was not yet called.
		 * Or this uprobe can be filtered out.
		 */
		if (vma_has_uprobes(vma, vma->vm_start, vma->vm_end))
			return;
	}

	clear_bit(MMF_HAS_UPROBES, &mm->flags);
}

static int is_trap_at_addr(struct mm_struct *mm, unsigned long vaddr)
{
	struct page *page;
	uprobe_opcode_t opcode;
	int result;

	if (WARN_ON_ONCE(!IS_ALIGNED(vaddr, UPROBE_SWBP_INSN_SIZE)))
		return -EINVAL;

	pagefault_disable();
	result = __get_user(opcode, (uprobe_opcode_t __user *)vaddr);
	pagefault_enable();

	if (likely(result == 0))
		goto out;

	result = get_user_pages(vaddr, 1, FOLL_FORCE, &page);
	if (result < 0)
		return result;

	copy_from_page(page, vaddr, &opcode, UPROBE_SWBP_INSN_SIZE);
	put_page(page);
 out:
	/* This needs to return true for any variant of the trap insn */
	return is_trap_insn(&opcode);
}

static struct uprobe *find_active_uprobe_speculative(unsigned long bp_vaddr)
{
	struct mm_struct *mm = current->mm;
	struct uprobe *uprobe = NULL;
	struct vm_area_struct *vma;
	struct file *vm_file;
	loff_t offset;
	unsigned int seq;

	guard(rcu)();

	if (!mmap_lock_speculate_try_begin(mm, &seq))
		return NULL;

	vma = vma_lookup(mm, bp_vaddr);
	if (!vma)
		return NULL;

	/*
	 * vm_file memory can be reused for another instance of struct file,
	 * but can't be freed from under us, so it's safe to read fields from
	 * it, even if the values are some garbage values; ultimately
	 * find_uprobe_rcu() + mmap_lock_speculation_end() check will ensure
	 * that whatever we speculatively found is correct
	 */
	vm_file = READ_ONCE(vma->vm_file);
	if (!vm_file)
		return NULL;

	offset = (loff_t)(vma->vm_pgoff << PAGE_SHIFT) + (bp_vaddr - vma->vm_start);
	uprobe = find_uprobe_rcu(vm_file->f_inode, offset);
	if (!uprobe)
		return NULL;

	/* now double check that nothing about MM changed */
	if (mmap_lock_speculate_retry(mm, seq))
		return NULL;

	return uprobe;
}

/* assumes being inside RCU protected region */
static struct uprobe *find_active_uprobe_rcu(unsigned long bp_vaddr, int *is_swbp)
{
	struct mm_struct *mm = current->mm;
	struct uprobe *uprobe = NULL;
	struct vm_area_struct *vma;

	uprobe = find_active_uprobe_speculative(bp_vaddr);
	if (uprobe)
		return uprobe;

	mmap_read_lock(mm);
	vma = vma_lookup(mm, bp_vaddr);
	if (vma) {
		if (vma->vm_file) {
			struct inode *inode = file_inode(vma->vm_file);
			loff_t offset = vaddr_to_offset(vma, bp_vaddr);

			uprobe = find_uprobe_rcu(inode, offset);
		}

		if (!uprobe)
			*is_swbp = is_trap_at_addr(mm, bp_vaddr);
	} else {
		*is_swbp = -EFAULT;
	}

	if (!uprobe && test_and_clear_bit(MMF_RECALC_UPROBES, &mm->flags))
		mmf_recalc_uprobes(mm);
	mmap_read_unlock(mm);

	return uprobe;
}

static struct return_instance *push_consumer(struct return_instance *ri, __u64 id, __u64 cookie)
{
	struct return_consumer *ric;

	if (unlikely(ri == ZERO_SIZE_PTR))
		return ri;

	if (unlikely(ri->cons_cnt > 0)) {
		ric = krealloc(ri->extra_consumers, sizeof(*ric) * ri->cons_cnt, GFP_KERNEL);
		if (!ric) {
			ri_free(ri);
			return ZERO_SIZE_PTR;
		}
		ri->extra_consumers = ric;
	}

	ric = likely(ri->cons_cnt == 0) ? &ri->consumer : &ri->extra_consumers[ri->cons_cnt - 1];
	ric->id = id;
	ric->cookie = cookie;

	ri->cons_cnt++;
	return ri;
}

static struct return_consumer *
return_consumer_find(struct return_instance *ri, int *iter, int id)
{
	struct return_consumer *ric;
	int idx;

	for (idx = *iter; idx < ri->cons_cnt; idx++)
	{
		ric = likely(idx == 0) ? &ri->consumer : &ri->extra_consumers[idx - 1];
		if (ric->id == id) {
			*iter = idx + 1;
			return ric;
		}
	}

	return NULL;
}

static bool ignore_ret_handler(int rc)
{
	return rc == UPROBE_HANDLER_REMOVE || rc == UPROBE_HANDLER_IGNORE;
}

static void handler_chain(struct uprobe *uprobe, struct pt_regs *regs)
{
	struct uprobe_consumer *uc;
	bool has_consumers = false, remove = true;
	struct return_instance *ri = NULL;
	struct uprobe_task *utask = current->utask;

	utask->auprobe = &uprobe->arch;

	list_for_each_entry_rcu(uc, &uprobe->consumers, cons_node, rcu_read_lock_trace_held()) {
		bool session = uc->handler && uc->ret_handler;
		__u64 cookie = 0;
		int rc = 0;

		if (uc->handler) {
			rc = uc->handler(uc, regs, &cookie);
			WARN(rc < 0 || rc > 2,
				"bad rc=0x%x from %ps()\n", rc, uc->handler);
		}

		remove &= rc == UPROBE_HANDLER_REMOVE;
		has_consumers = true;

		if (!uc->ret_handler || ignore_ret_handler(rc))
			continue;

		if (!ri)
			ri = alloc_return_instance(utask);

		if (session)
			ri = push_consumer(ri, uc->id, cookie);
	}
	utask->auprobe = NULL;

	if (!ZERO_OR_NULL_PTR(ri))
		prepare_uretprobe(uprobe, regs, ri);

	if (remove && has_consumers) {
		down_read(&uprobe->register_rwsem);

		/* re-check that removal is still required, this time under lock */
		if (!filter_chain(uprobe, current->mm)) {
			WARN_ON(!uprobe_is_active(uprobe));
			unapply_uprobe(uprobe, current->mm);
		}

		up_read(&uprobe->register_rwsem);
	}
}

static void
handle_uretprobe_chain(struct return_instance *ri, struct uprobe *uprobe, struct pt_regs *regs)
{
	struct return_consumer *ric;
	struct uprobe_consumer *uc;
	int ric_idx = 0;

	/* all consumers unsubscribed meanwhile */
	if (unlikely(!uprobe))
		return;

	rcu_read_lock_trace();
	list_for_each_entry_rcu(uc, &uprobe->consumers, cons_node, rcu_read_lock_trace_held()) {
		bool session = uc->handler && uc->ret_handler;

		if (uc->ret_handler) {
			ric = return_consumer_find(ri, &ric_idx, uc->id);
			if (!session || ric)
				uc->ret_handler(uc, ri->func, regs, ric ? &ric->cookie : NULL);
		}
	}
	rcu_read_unlock_trace();
}

static struct return_instance *find_next_ret_chain(struct return_instance *ri)
{
	bool chained;

	do {
		chained = ri->chained;
		ri = ri->next;	/* can't be NULL if chained */
	} while (chained);

	return ri;
}

void uprobe_handle_trampoline(struct pt_regs *regs)
{
	struct uprobe_task *utask;
	struct return_instance *ri, *ri_next, *next_chain;
	struct uprobe *uprobe;
	enum hprobe_state hstate;
	bool valid;

	utask = current->utask;
	if (!utask)
		goto sigill;

	ri = utask->return_instances;
	if (!ri)
		goto sigill;

	do {
		/*
		 * We should throw out the frames invalidated by longjmp().
		 * If this chain is valid, then the next one should be alive
		 * or NULL; the latter case means that nobody but ri->func
		 * could hit this trampoline on return. TODO: sigaltstack().
		 */
		next_chain = find_next_ret_chain(ri);
		valid = !next_chain || arch_uretprobe_is_alive(next_chain, RP_CHECK_RET, regs);

		instruction_pointer_set(regs, ri->orig_ret_vaddr);
		do {
			/* pop current instance from the stack of pending return instances,
			 * as it's not pending anymore: we just fixed up original
			 * instruction pointer in regs and are about to call handlers;
			 * this allows fixup_uretprobe_trampoline_entries() to properly fix up
			 * captured stack traces from uretprobe handlers, in which pending
			 * trampoline addresses on the stack are replaced with correct
			 * original return addresses
			 */
			ri_next = ri->next;
			rcu_assign_pointer(utask->return_instances, ri_next);
			utask->depth--;

			uprobe = hprobe_consume(&ri->hprobe, &hstate);
			if (valid)
				handle_uretprobe_chain(ri, uprobe, regs);
			hprobe_finalize(&ri->hprobe, hstate);

			/* We already took care of hprobe, no need to waste more time on that. */
			free_ret_instance(utask, ri, false /* !cleanup_hprobe */);
			ri = ri_next;
		} while (ri != next_chain);
	} while (!valid);

	return;

sigill:
	uprobe_warn(current, "handle uretprobe, sending SIGILL.");
	force_sig(SIGILL);
}

bool __weak arch_uprobe_ignore(struct arch_uprobe *aup, struct pt_regs *regs)
{
	return false;
}

bool __weak arch_uretprobe_is_alive(struct return_instance *ret, enum rp_check ctx,
					struct pt_regs *regs)
{
	return true;
}

/*
 * Run handler and ask thread to singlestep.
 * Ensure all non-fatal signals cannot interrupt thread while it singlesteps.
 */
static void handle_swbp(struct pt_regs *regs)
{
	struct uprobe *uprobe;
	unsigned long bp_vaddr;
	int is_swbp;

	bp_vaddr = uprobe_get_swbp_addr(regs);
	if (bp_vaddr == uprobe_get_trampoline_vaddr())
		return uprobe_handle_trampoline(regs);

	rcu_read_lock_trace();

	uprobe = find_active_uprobe_rcu(bp_vaddr, &is_swbp);
	if (!uprobe) {
		if (is_swbp > 0) {
			/* No matching uprobe; signal SIGTRAP. */
			force_sig(SIGTRAP);
		} else {
			/*
			 * Either we raced with uprobe_unregister() or we can't
			 * access this memory. The latter is only possible if
			 * another thread plays with our ->mm. In both cases
			 * we can simply restart. If this vma was unmapped we
			 * can pretend this insn was not executed yet and get
			 * the (correct) SIGSEGV after restart.
			 */
			instruction_pointer_set(regs, bp_vaddr);
		}
		goto out;
	}

	/* change it in advance for ->handler() and restart */
	instruction_pointer_set(regs, bp_vaddr);

	/*
	 * TODO: move copy_insn/etc into _register and remove this hack.
	 * After we hit the bp, _unregister + _register can install the
	 * new and not-yet-analyzed uprobe at the same address, restart.
	 */
	if (unlikely(!test_bit(UPROBE_COPY_INSN, &uprobe->flags)))
		goto out;

	/*
	 * Pairs with the smp_wmb() in prepare_uprobe().
	 *
	 * Guarantees that if we see the UPROBE_COPY_INSN bit set, then
	 * we must also see the stores to &uprobe->arch performed by the
	 * prepare_uprobe() call.
	 */
	smp_rmb();

	/* Tracing handlers use ->utask to communicate with fetch methods */
	if (!get_utask())
		goto out;

	if (arch_uprobe_ignore(&uprobe->arch, regs))
		goto out;

	handler_chain(uprobe, regs);

	if (arch_uprobe_skip_sstep(&uprobe->arch, regs))
		goto out;

	if (pre_ssout(uprobe, regs, bp_vaddr))
		goto out;

out:
	/* arch_uprobe_skip_sstep() succeeded, or restart if can't singlestep */
	rcu_read_unlock_trace();
}

/*
 * Perform required fix-ups and disable singlestep.
 * Allow pending signals to take effect.
 */
static void handle_singlestep(struct uprobe_task *utask, struct pt_regs *regs)
{
	struct uprobe *uprobe;
	int err = 0;

	uprobe = utask->active_uprobe;
	if (utask->state == UTASK_SSTEP_ACK)
		err = arch_uprobe_post_xol(&uprobe->arch, regs);
	else if (utask->state == UTASK_SSTEP_TRAPPED)
		arch_uprobe_abort_xol(&uprobe->arch, regs);
	else
		WARN_ON_ONCE(1);

	put_uprobe(uprobe);
	utask->active_uprobe = NULL;
	utask->state = UTASK_RUNNING;
	xol_free_insn_slot(utask);

	if (utask->signal_denied) {
		set_thread_flag(TIF_SIGPENDING);
		utask->signal_denied = false;
	}

	if (unlikely(err)) {
		uprobe_warn(current, "execute the probed insn, sending SIGILL.");
		force_sig(SIGILL);
	}
}

/*
 * On breakpoint hit, breakpoint notifier sets the TIF_UPROBE flag and
 * allows the thread to return from interrupt. After that handle_swbp()
 * sets utask->active_uprobe.
 *
 * On singlestep exception, singlestep notifier sets the TIF_UPROBE flag
 * and allows the thread to return from interrupt.
 *
 * While returning to userspace, thread notices the TIF_UPROBE flag and calls
 * uprobe_notify_resume().
 */
void uprobe_notify_resume(struct pt_regs *regs)
{
	struct uprobe_task *utask;

	clear_thread_flag(TIF_UPROBE);

	utask = current->utask;
	if (utask && utask->active_uprobe)
		handle_singlestep(utask, regs);
	else
		handle_swbp(regs);
}

/*
 * uprobe_pre_sstep_notifier gets called from interrupt context as part of
 * notifier mechanism. Set TIF_UPROBE flag and indicate breakpoint hit.
 */
int uprobe_pre_sstep_notifier(struct pt_regs *regs)
{
	if (!current->mm)
		return 0;

	if (!test_bit(MMF_HAS_UPROBES, &current->mm->flags) &&
	    (!current->utask || !current->utask->return_instances))
		return 0;

	set_thread_flag(TIF_UPROBE);
	return 1;
}

/*
 * uprobe_post_sstep_notifier gets called in interrupt context as part of notifier
 * mechanism. Set TIF_UPROBE flag and indicate completion of singlestep.
 */
int uprobe_post_sstep_notifier(struct pt_regs *regs)
{
	struct uprobe_task *utask = current->utask;

	if (!current->mm || !utask || !utask->active_uprobe)
		/* task is currently not uprobed */
		return 0;

	utask->state = UTASK_SSTEP_ACK;
	set_thread_flag(TIF_UPROBE);
	return 1;
}

static struct notifier_block uprobe_exception_nb = {
	.notifier_call		= arch_uprobe_exception_notify,
	.priority		= INT_MAX-1,	/* notified after kprobes, kgdb */
};

void __init uprobes_init(void)
{
	int i;

	for (i = 0; i < UPROBES_HASH_SZ; i++)
		mutex_init(&uprobes_mmap_mutex[i]);

	BUG_ON(register_die_notifier(&uprobe_exception_nb));
}
