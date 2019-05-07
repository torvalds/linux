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
#include <linux/sched/coredump.h>
#include <linux/export.h>
#include <linux/rmap.h>		/* anon_vma_prepare */
#include <linux/mmu_notifier.h>	/* set_pte_at_notify */
#include <linux/swap.h>		/* try_to_free_swap */
#include <linux/ptrace.h>	/* user_enable_single_step */
#include <linux/kdebug.h>	/* notifier mechanism */
#include "../../mm/internal.h"	/* munlock_vma_page */
#include <linux/percpu-rwsem.h>
#include <linux/task_work.h>
#include <linux/shmem_fs.h>

#include <linux/uprobes.h>

#define UINSNS_PER_PAGE			(PAGE_SIZE/UPROBE_XOL_SLOT_BYTES)
#define MAX_UPROBE_XOL_SLOTS		UINSNS_PER_PAGE

static struct rb_root uprobes_tree = RB_ROOT;
/*
 * allows us to skip the uprobe_mmap if there are no uprobe events active
 * at this time.  Probably a fine grained per inode count is better?
 */
#define no_uprobe_events()	RB_EMPTY_ROOT(&uprobes_tree)

static DEFINE_SPINLOCK(uprobes_treelock);	/* serialize rbtree access */

#define UPROBES_HASH_SZ	13
/* serialize uprobe->pending_list */
static struct mutex uprobes_mmap_mutex[UPROBES_HASH_SZ];
#define uprobes_mmap_hash(v)	(&uprobes_mmap_mutex[((unsigned long)(v)) % UPROBES_HASH_SZ])

static struct percpu_rw_semaphore dup_mmap_sem;

/* Have a copy of original instruction */
#define UPROBE_COPY_INSN	0

struct uprobe {
	struct rb_node		rb_node;	/* node in the rb tree */
	refcount_t		ref;
	struct rw_semaphore	register_rwsem;
	struct rw_semaphore	consumer_rwsem;
	struct list_head	pending_list;
	struct uprobe_consumer	*consumers;
	struct inode		*inode;		/* Also hold a ref to inode */
	loff_t			offset;
	loff_t			ref_ctr_offset;
	unsigned long		flags;

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
	atomic_t 			slot_count;	/* number of in-use slots */
	unsigned long 			*bitmap;	/* 0 = free slot */

	struct vm_special_mapping	xol_mapping;
	struct page 			*pages[2];
	/*
	 * We keep the vma's vm_start rather than a pointer to the vma
	 * itself.  The probed process or a naughty kernel module could make
	 * the vma go away, and we must handle that reasonably gracefully.
	 */
	unsigned long 			vaddr;		/* Page(s) of instruction slots */
};

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
 * @page:     the cowed page we are replacing by kpage
 * @kpage:    the modified page we replace page by
 *
 * Returns 0 on success, -EFAULT on failure.
 */
static int __replace_page(struct vm_area_struct *vma, unsigned long addr,
				struct page *old_page, struct page *new_page)
{
	struct mm_struct *mm = vma->vm_mm;
	struct page_vma_mapped_walk pvmw = {
		.page = old_page,
		.vma = vma,
		.address = addr,
	};
	int err;
	struct mmu_notifier_range range;
	struct mem_cgroup *memcg;

	mmu_notifier_range_init(&range, mm, addr, addr + PAGE_SIZE);

	VM_BUG_ON_PAGE(PageTransHuge(old_page), old_page);

	err = mem_cgroup_try_charge(new_page, vma->vm_mm, GFP_KERNEL, &memcg,
			false);
	if (err)
		return err;

	/* For try_to_free_swap() and munlock_vma_page() below */
	lock_page(old_page);

	mmu_notifier_invalidate_range_start(&range);
	err = -EAGAIN;
	if (!page_vma_mapped_walk(&pvmw)) {
		mem_cgroup_cancel_charge(new_page, memcg, false);
		goto unlock;
	}
	VM_BUG_ON_PAGE(addr != pvmw.address, old_page);

	get_page(new_page);
	page_add_new_anon_rmap(new_page, vma, addr, false);
	mem_cgroup_commit_charge(new_page, memcg, false, false);
	lru_cache_add_active_or_unevictable(new_page, vma);

	if (!PageAnon(old_page)) {
		dec_mm_counter(mm, mm_counter_file(old_page));
		inc_mm_counter(mm, MM_ANONPAGES);
	}

	flush_cache_page(vma, addr, pte_pfn(*pvmw.pte));
	ptep_clear_flush_notify(vma, addr, pvmw.pte);
	set_pte_at_notify(mm, addr, pvmw.pte,
			mk_pte(new_page, vma->vm_page_prot));

	page_remove_rmap(old_page, false);
	if (!page_mapped(old_page))
		try_to_free_swap(old_page);
	page_vma_mapped_walk_done(&pvmw);

	if (vma->vm_flags & VM_LOCKED)
		munlock_vma_page(old_page);
	put_page(old_page);

	err = 0;
 unlock:
	mmu_notifier_invalidate_range_end(&range);
	unlock_page(old_page);
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
	struct vm_area_struct *tmp;

	for (tmp = mm->mmap; tmp; tmp = tmp->vm_next)
		if (valid_ref_ctr_vma(uprobe, tmp))
			return tmp;

	return NULL;
}

static int
__update_ref_ctr(struct mm_struct *mm, unsigned long vaddr, short d)
{
	void *kaddr;
	struct page *page;
	struct vm_area_struct *vma;
	int ret;
	short *ptr;

	if (!vaddr || !d)
		return -EINVAL;

	ret = get_user_pages_remote(NULL, mm, vaddr, 1,
			FOLL_WRITE, &page, &vma, NULL);
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
		"0x%llx ref_ctr_offset: 0x%llx of mm: 0x%pK\n",
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
 * @mm: the probed process address space.
 * @vaddr: the virtual address to store the opcode.
 * @opcode: opcode to be written at @vaddr.
 *
 * Called with mm->mmap_sem held for write.
 * Return 0 (success) or a negative errno.
 */
int uprobe_write_opcode(struct arch_uprobe *auprobe, struct mm_struct *mm,
			unsigned long vaddr, uprobe_opcode_t opcode)
{
	struct uprobe *uprobe;
	struct page *old_page, *new_page;
	struct vm_area_struct *vma;
	int ret, is_register, ref_ctr_updated = 0;

	is_register = is_swbp_insn(&opcode);
	uprobe = container_of(auprobe, struct uprobe, arch);

retry:
	/* Read the page with vaddr into memory */
	ret = get_user_pages_remote(NULL, mm, vaddr, 1,
			FOLL_FORCE | FOLL_SPLIT, &old_page, &vma, NULL);
	if (ret <= 0)
		return ret;

	ret = verify_opcode(old_page, vaddr, &opcode);
	if (ret <= 0)
		goto put_old;

	/* We are going to replace instruction, update ref_ctr. */
	if (!ref_ctr_updated && uprobe->ref_ctr_offset) {
		ret = update_ref_ctr(uprobe, mm, is_register ? 1 : -1);
		if (ret)
			goto put_old;

		ref_ctr_updated = 1;
	}

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

	ret = __replace_page(vma, vaddr, old_page, new_page);
	put_page(new_page);
put_old:
	put_page(old_page);

	if (unlikely(ret == -EAGAIN))
		goto retry;

	/* Revert back reference counter if instruction update failed. */
	if (ret && is_register && ref_ctr_updated)
		update_ref_ctr(uprobe, mm, -1);

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

static struct uprobe *get_uprobe(struct uprobe *uprobe)
{
	refcount_inc(&uprobe->ref);
	return uprobe;
}

static void put_uprobe(struct uprobe *uprobe)
{
	if (refcount_dec_and_test(&uprobe->ref)) {
		/*
		 * If application munmap(exec_vma) before uprobe_unregister()
		 * gets called, we don't get a chance to remove uprobe from
		 * delayed_uprobe_list from remove_breakpoint(). Do it here.
		 */
		mutex_lock(&delayed_uprobe_lock);
		delayed_uprobe_remove(uprobe, NULL);
		mutex_unlock(&delayed_uprobe_lock);
		kfree(uprobe);
	}
}

static int match_uprobe(struct uprobe *l, struct uprobe *r)
{
	if (l->inode < r->inode)
		return -1;

	if (l->inode > r->inode)
		return 1;

	if (l->offset < r->offset)
		return -1;

	if (l->offset > r->offset)
		return 1;

	return 0;
}

static struct uprobe *__find_uprobe(struct inode *inode, loff_t offset)
{
	struct uprobe u = { .inode = inode, .offset = offset };
	struct rb_node *n = uprobes_tree.rb_node;
	struct uprobe *uprobe;
	int match;

	while (n) {
		uprobe = rb_entry(n, struct uprobe, rb_node);
		match = match_uprobe(&u, uprobe);
		if (!match)
			return get_uprobe(uprobe);

		if (match < 0)
			n = n->rb_left;
		else
			n = n->rb_right;
	}
	return NULL;
}

/*
 * Find a uprobe corresponding to a given inode:offset
 * Acquires uprobes_treelock
 */
static struct uprobe *find_uprobe(struct inode *inode, loff_t offset)
{
	struct uprobe *uprobe;

	spin_lock(&uprobes_treelock);
	uprobe = __find_uprobe(inode, offset);
	spin_unlock(&uprobes_treelock);

	return uprobe;
}

static struct uprobe *__insert_uprobe(struct uprobe *uprobe)
{
	struct rb_node **p = &uprobes_tree.rb_node;
	struct rb_node *parent = NULL;
	struct uprobe *u;
	int match;

	while (*p) {
		parent = *p;
		u = rb_entry(parent, struct uprobe, rb_node);
		match = match_uprobe(uprobe, u);
		if (!match)
			return get_uprobe(u);

		if (match < 0)
			p = &parent->rb_left;
		else
			p = &parent->rb_right;

	}

	u = NULL;
	rb_link_node(&uprobe->rb_node, parent, p);
	rb_insert_color(&uprobe->rb_node, &uprobes_tree);
	/* get access + creation ref */
	refcount_set(&uprobe->ref, 2);

	return u;
}

/*
 * Acquire uprobes_treelock.
 * Matching uprobe already exists in rbtree;
 *	increment (access refcount) and return the matching uprobe.
 *
 * No matching uprobe; insert the uprobe in rb_tree;
 *	get a double refcount (access + creation) and return NULL.
 */
static struct uprobe *insert_uprobe(struct uprobe *uprobe)
{
	struct uprobe *u;

	spin_lock(&uprobes_treelock);
	u = __insert_uprobe(uprobe);
	spin_unlock(&uprobes_treelock);

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
		return NULL;

	uprobe->inode = inode;
	uprobe->offset = offset;
	uprobe->ref_ctr_offset = ref_ctr_offset;
	init_rwsem(&uprobe->register_rwsem);
	init_rwsem(&uprobe->consumer_rwsem);

	/* add to uprobes_tree, sorted on inode:offset */
	cur_uprobe = insert_uprobe(uprobe);
	/* a uprobe exists for this inode:offset combination */
	if (cur_uprobe) {
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
	down_write(&uprobe->consumer_rwsem);
	uc->next = uprobe->consumers;
	uprobe->consumers = uc;
	up_write(&uprobe->consumer_rwsem);
}

/*
 * For uprobe @uprobe, delete the consumer @uc.
 * Return true if the @uc is deleted successfully
 * or return false.
 */
static bool consumer_del(struct uprobe *uprobe, struct uprobe_consumer *uc)
{
	struct uprobe_consumer **con;
	bool ret = false;

	down_write(&uprobe->consumer_rwsem);
	for (con = &uprobe->consumers; *con; con = &(*con)->next) {
		if (*con == uc) {
			*con = uc->next;
			ret = true;
			break;
		}
	}
	up_write(&uprobe->consumer_rwsem);

	return ret;
}

static int __copy_insn(struct address_space *mapping, struct file *filp,
			void *insn, int nbytes, loff_t offset)
{
	struct page *page;
	/*
	 * Ensure that the page that has the original instruction is populated
	 * and in page-cache. If ->readpage == NULL it must be shmem_mapping(),
	 * see uprobe_register().
	 */
	if (mapping->a_ops->readpage)
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

	/* uprobe_write_opcode() assumes we don't cross page boundary */
	BUG_ON((uprobe->offset & ~PAGE_MASK) +
			UPROBE_SWBP_INSN_SIZE > PAGE_SIZE);

	smp_wmb(); /* pairs with the smp_rmb() in handle_swbp() */
	set_bit(UPROBE_COPY_INSN, &uprobe->flags);

 out:
	up_write(&uprobe->consumer_rwsem);

	return ret;
}

static inline bool consumer_filter(struct uprobe_consumer *uc,
				   enum uprobe_filter_ctx ctx, struct mm_struct *mm)
{
	return !uc->filter || uc->filter(uc, ctx, mm);
}

static bool filter_chain(struct uprobe *uprobe,
			 enum uprobe_filter_ctx ctx, struct mm_struct *mm)
{
	struct uprobe_consumer *uc;
	bool ret = false;

	down_read(&uprobe->consumer_rwsem);
	for (uc = uprobe->consumers; uc; uc = uc->next) {
		ret = consumer_filter(uc, ctx, mm);
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

static inline bool uprobe_is_active(struct uprobe *uprobe)
{
	return !RB_EMPTY_NODE(&uprobe->rb_node);
}
/*
 * There could be threads that have already hit the breakpoint. They
 * will recheck the current insn and restart if find_uprobe() fails.
 * See find_active_uprobe().
 */
static void delete_uprobe(struct uprobe *uprobe)
{
	if (WARN_ON(!uprobe_is_active(uprobe)))
		return;

	spin_lock(&uprobes_treelock);
	rb_erase(&uprobe->rb_node, &uprobes_tree);
	spin_unlock(&uprobes_treelock);
	RB_CLEAR_NODE(&uprobe->rb_node); /* for uprobe_is_active() */
	put_uprobe(uprobe);
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

		down_write(&mm->mmap_sem);
		vma = find_vma(mm, info->vaddr);
		if (!vma || !valid_vma(vma, is_register) ||
		    file_inode(vma->vm_file) != uprobe->inode)
			goto unlock;

		if (vma->vm_start > info->vaddr ||
		    vaddr_to_offset(vma, info->vaddr) != uprobe->offset)
			goto unlock;

		if (is_register) {
			/* consult only the "caller", new consumer. */
			if (consumer_filter(new,
					UPROBE_FILTER_REGISTER, mm))
				err = install_breakpoint(uprobe, mm, vma, info->vaddr);
		} else if (test_bit(MMF_HAS_UPROBES, &mm->flags)) {
			if (!filter_chain(uprobe,
					UPROBE_FILTER_UNREGISTER, mm))
				err |= remove_breakpoint(uprobe, mm, info->vaddr);
		}

 unlock:
		up_write(&mm->mmap_sem);
 free:
		mmput(mm);
		info = free_map_info(info);
	}
 out:
	percpu_up_write(&dup_mmap_sem);
	return err;
}

static void
__uprobe_unregister(struct uprobe *uprobe, struct uprobe_consumer *uc)
{
	int err;

	if (WARN_ON(!consumer_del(uprobe, uc)))
		return;

	err = register_for_each_vma(uprobe, NULL);
	/* TODO : cant unregister? schedule a worker thread */
	if (!uprobe->consumers && !err)
		delete_uprobe(uprobe);
}

/*
 * uprobe_unregister - unregister an already registered probe.
 * @inode: the file in which the probe has to be removed.
 * @offset: offset from the start of the file.
 * @uc: identify which probe if multiple probes are colocated.
 */
void uprobe_unregister(struct inode *inode, loff_t offset, struct uprobe_consumer *uc)
{
	struct uprobe *uprobe;

	uprobe = find_uprobe(inode, offset);
	if (WARN_ON(!uprobe))
		return;

	down_write(&uprobe->register_rwsem);
	__uprobe_unregister(uprobe, uc);
	up_write(&uprobe->register_rwsem);
	put_uprobe(uprobe);
}
EXPORT_SYMBOL_GPL(uprobe_unregister);

/*
 * __uprobe_register - register a probe
 * @inode: the file in which the probe has to be placed.
 * @offset: offset from the start of the file.
 * @uc: information on howto handle the probe..
 *
 * Apart from the access refcount, __uprobe_register() takes a creation
 * refcount (thro alloc_uprobe) if and only if this @uprobe is getting
 * inserted into the rbtree (i.e first consumer for a @inode:@offset
 * tuple).  Creation refcount stops uprobe_unregister from freeing the
 * @uprobe even before the register operation is complete. Creation
 * refcount is released when the last @uc for the @uprobe
 * unregisters. Caller of __uprobe_register() is required to keep @inode
 * (and the containing mount) referenced.
 *
 * Return errno if it cannot successully install probes
 * else return 0 (success)
 */
static int __uprobe_register(struct inode *inode, loff_t offset,
			     loff_t ref_ctr_offset, struct uprobe_consumer *uc)
{
	struct uprobe *uprobe;
	int ret;

	/* Uprobe must have at least one set consumer */
	if (!uc->handler && !uc->ret_handler)
		return -EINVAL;

	/* copy_insn() uses read_mapping_page() or shmem_read_mapping_page() */
	if (!inode->i_mapping->a_ops->readpage && !shmem_mapping(inode->i_mapping))
		return -EIO;
	/* Racy, just to catch the obvious mistakes */
	if (offset > i_size_read(inode))
		return -EINVAL;

 retry:
	uprobe = alloc_uprobe(inode, offset, ref_ctr_offset);
	if (!uprobe)
		return -ENOMEM;
	if (IS_ERR(uprobe))
		return PTR_ERR(uprobe);

	/*
	 * We can race with uprobe_unregister()->delete_uprobe().
	 * Check uprobe_is_active() and retry if it is false.
	 */
	down_write(&uprobe->register_rwsem);
	ret = -EAGAIN;
	if (likely(uprobe_is_active(uprobe))) {
		consumer_add(uprobe, uc);
		ret = register_for_each_vma(uprobe, uc);
		if (ret)
			__uprobe_unregister(uprobe, uc);
	}
	up_write(&uprobe->register_rwsem);
	put_uprobe(uprobe);

	if (unlikely(ret == -EAGAIN))
		goto retry;
	return ret;
}

int uprobe_register(struct inode *inode, loff_t offset,
		    struct uprobe_consumer *uc)
{
	return __uprobe_register(inode, offset, 0, uc);
}
EXPORT_SYMBOL_GPL(uprobe_register);

int uprobe_register_refctr(struct inode *inode, loff_t offset,
			   loff_t ref_ctr_offset, struct uprobe_consumer *uc)
{
	return __uprobe_register(inode, offset, ref_ctr_offset, uc);
}
EXPORT_SYMBOL_GPL(uprobe_register_refctr);

/*
 * uprobe_apply - unregister an already registered probe.
 * @inode: the file in which the probe has to be removed.
 * @offset: offset from the start of the file.
 * @uc: consumer which wants to add more or remove some breakpoints
 * @add: add or remove the breakpoints
 */
int uprobe_apply(struct inode *inode, loff_t offset,
			struct uprobe_consumer *uc, bool add)
{
	struct uprobe *uprobe;
	struct uprobe_consumer *con;
	int ret = -ENOENT;

	uprobe = find_uprobe(inode, offset);
	if (WARN_ON(!uprobe))
		return ret;

	down_write(&uprobe->register_rwsem);
	for (con = uprobe->consumers; con && con != uc ; con = con->next)
		;
	if (con)
		ret = register_for_each_vma(uprobe, add ? uc : NULL);
	up_write(&uprobe->register_rwsem);
	put_uprobe(uprobe);

	return ret;
}

static int unapply_uprobe(struct uprobe *uprobe, struct mm_struct *mm)
{
	struct vm_area_struct *vma;
	int err = 0;

	down_read(&mm->mmap_sem);
	for (vma = mm->mmap; vma; vma = vma->vm_next) {
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
	up_read(&mm->mmap_sem);

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

	spin_lock(&uprobes_treelock);
	n = find_node_in_range(inode, min, max);
	if (n) {
		for (t = n; t; t = rb_prev(t)) {
			u = rb_entry(t, struct uprobe, rb_node);
			if (u->inode != inode || u->offset < min)
				break;
			list_add(&u->pending_list, head);
			get_uprobe(u);
		}
		for (t = n; (t = rb_next(t)); ) {
			u = rb_entry(t, struct uprobe, rb_node);
			if (u->inode != inode || u->offset > max)
				break;
			list_add(&u->pending_list, head);
			get_uprobe(u);
		}
	}
	spin_unlock(&uprobes_treelock);
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
 * Called from mmap_region/vma_adjust with mm->mmap_sem acquired.
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
		    filter_chain(uprobe, UPROBE_FILTER_MMAP, vma->vm_mm)) {
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

	spin_lock(&uprobes_treelock);
	n = find_node_in_range(inode, min, max);
	spin_unlock(&uprobes_treelock);

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

/* Slot allocation for XOL */
static int xol_add_vma(struct mm_struct *mm, struct xol_area *area)
{
	struct vm_area_struct *vma;
	int ret;

	if (down_write_killable(&mm->mmap_sem))
		return -EINTR;

	if (mm->uprobes_state.xol_area) {
		ret = -EALREADY;
		goto fail;
	}

	if (!area->vaddr) {
		/* Try to map as high as possible, this is only a hint. */
		area->vaddr = get_unmapped_area(NULL, TASK_SIZE - PAGE_SIZE,
						PAGE_SIZE, 0, 0);
		if (area->vaddr & ~PAGE_MASK) {
			ret = area->vaddr;
			goto fail;
		}
	}

	vma = _install_special_mapping(mm, area->vaddr, PAGE_SIZE,
				VM_EXEC|VM_MAYEXEC|VM_DONTCOPY|VM_IO,
				&area->xol_mapping);
	if (IS_ERR(vma)) {
		ret = PTR_ERR(vma);
		goto fail;
	}

	ret = 0;
	/* pairs with get_xol_area() */
	smp_store_release(&mm->uprobes_state.xol_area, area); /* ^^^ */
 fail:
	up_write(&mm->mmap_sem);

	return ret;
}

static struct xol_area *__create_xol_area(unsigned long vaddr)
{
	struct mm_struct *mm = current->mm;
	uprobe_opcode_t insn = UPROBE_SWBP_INSN;
	struct xol_area *area;

	area = kmalloc(sizeof(*area), GFP_KERNEL);
	if (unlikely(!area))
		goto out;

	area->bitmap = kcalloc(BITS_TO_LONGS(UINSNS_PER_PAGE), sizeof(long),
			       GFP_KERNEL);
	if (!area->bitmap)
		goto free_area;

	area->xol_mapping.name = "[uprobes]";
	area->xol_mapping.fault = NULL;
	area->xol_mapping.pages = area->pages;
	area->pages[0] = alloc_page(GFP_HIGHUSER);
	if (!area->pages[0])
		goto free_bitmap;
	area->pages[1] = NULL;

	area->vaddr = vaddr;
	init_waitqueue_head(&area->wq);
	/* Reserve the 1st slot for get_trampoline_vaddr() */
	set_bit(0, area->bitmap);
	atomic_set(&area->slot_count, 1);
	arch_uprobe_copy_ixol(area->pages[0], 0, &insn, UPROBE_SWBP_INSN_SIZE);

	if (!xol_add_vma(mm, area))
		return area;

	__free_page(area->pages[0]);
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

	put_page(area->pages[0]);
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

/*
 *  - search for a free slot.
 */
static unsigned long xol_take_insn_slot(struct xol_area *area)
{
	unsigned long slot_addr;
	int slot_nr;

	do {
		slot_nr = find_first_zero_bit(area->bitmap, UINSNS_PER_PAGE);
		if (slot_nr < UINSNS_PER_PAGE) {
			if (!test_and_set_bit(slot_nr, area->bitmap))
				break;

			slot_nr = UINSNS_PER_PAGE;
			continue;
		}
		wait_event(area->wq, (atomic_read(&area->slot_count) < UINSNS_PER_PAGE));
	} while (slot_nr >= UINSNS_PER_PAGE);

	slot_addr = area->vaddr + (slot_nr * UPROBE_XOL_SLOT_BYTES);
	atomic_inc(&area->slot_count);

	return slot_addr;
}

/*
 * xol_get_insn_slot - allocate a slot for xol.
 * Returns the allocated slot address or 0.
 */
static unsigned long xol_get_insn_slot(struct uprobe *uprobe)
{
	struct xol_area *area;
	unsigned long xol_vaddr;

	area = get_xol_area();
	if (!area)
		return 0;

	xol_vaddr = xol_take_insn_slot(area);
	if (unlikely(!xol_vaddr))
		return 0;

	arch_uprobe_copy_ixol(area->pages[0], xol_vaddr,
			      &uprobe->arch.ixol, sizeof(uprobe->arch.ixol));

	return xol_vaddr;
}

/*
 * xol_free_insn_slot - If slot was earlier allocated by
 * @xol_get_insn_slot(), make the slot available for
 * subsequent requests.
 */
static void xol_free_insn_slot(struct task_struct *tsk)
{
	struct xol_area *area;
	unsigned long vma_end;
	unsigned long slot_addr;

	if (!tsk->mm || !tsk->mm->uprobes_state.xol_area || !tsk->utask)
		return;

	slot_addr = tsk->utask->xol_vaddr;
	if (unlikely(!slot_addr))
		return;

	area = tsk->mm->uprobes_state.xol_area;
	vma_end = area->vaddr + PAGE_SIZE;
	if (area->vaddr <= slot_addr && slot_addr < vma_end) {
		unsigned long offset;
		int slot_nr;

		offset = slot_addr - area->vaddr;
		slot_nr = offset / UPROBE_XOL_SLOT_BYTES;
		if (slot_nr >= UINSNS_PER_PAGE)
			return;

		clear_bit(slot_nr, area->bitmap);
		atomic_dec(&area->slot_count);
		smp_mb__after_atomic(); /* pairs with prepare_to_wait() */
		if (waitqueue_active(&area->wq))
			wake_up(&area->wq);

		tsk->utask->xol_vaddr = 0;
	}
}

void __weak arch_uprobe_copy_ixol(struct page *page, unsigned long vaddr,
				  void *src, unsigned long len)
{
	/* Initialize the slot */
	copy_to_page(page, vaddr, src, len);

	/*
	 * We probably need flush_icache_user_range() but it needs vma.
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

static struct return_instance *free_ret_instance(struct return_instance *ri)
{
	struct return_instance *next = ri->next;
	put_uprobe(ri->uprobe);
	kfree(ri);
	return next;
}

/*
 * Called with no locks held.
 * Called in context of an exiting or an exec-ing thread.
 */
void uprobe_free_utask(struct task_struct *t)
{
	struct uprobe_task *utask = t->utask;
	struct return_instance *ri;

	if (!utask)
		return;

	if (utask->active_uprobe)
		put_uprobe(utask->active_uprobe);

	ri = utask->return_instances;
	while (ri)
		ri = free_ret_instance(ri);

	xol_free_insn_slot(t);
	kfree(utask);
	t->utask = NULL;
}

/*
 * Allocate a uprobe_task object for the task if if necessary.
 * Called when the thread hits a breakpoint.
 *
 * Returns:
 * - pointer to new uprobe_task on success
 * - NULL otherwise
 */
static struct uprobe_task *get_utask(void)
{
	if (!current->utask)
		current->utask = kzalloc(sizeof(struct uprobe_task), GFP_KERNEL);
	return current->utask;
}

static int dup_utask(struct task_struct *t, struct uprobe_task *o_utask)
{
	struct uprobe_task *n_utask;
	struct return_instance **p, *o, *n;

	n_utask = kzalloc(sizeof(struct uprobe_task), GFP_KERNEL);
	if (!n_utask)
		return -ENOMEM;
	t->utask = n_utask;

	p = &n_utask->return_instances;
	for (o = o_utask->return_instances; o; o = o->next) {
		n = kmalloc(sizeof(struct return_instance), GFP_KERNEL);
		if (!n)
			return -ENOMEM;

		*n = *o;
		get_uprobe(n->uprobe);
		n->next = NULL;

		*p = n;
		p = &n->next;
		n_utask->depth++;
	}

	return 0;
}

static void uprobe_warn(struct task_struct *t, const char *msg)
{
	pr_warn("uprobe: %s:%d failed to %s\n",
			current->comm, current->pid, msg);
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
	task_work_add(t, &t->utask->dup_xol_work, true);
}

/*
 * Current area->vaddr notion assume the trampoline address is always
 * equal area->vaddr.
 *
 * Returns -1 in case the xol_area is not allocated.
 */
static unsigned long get_trampoline_vaddr(void)
{
	struct xol_area *area;
	unsigned long trampoline_vaddr = -1;

	/* Pairs with xol_add_vma() smp_store_release() */
	area = READ_ONCE(current->mm->uprobes_state.xol_area); /* ^^^ */
	if (area)
		trampoline_vaddr = area->vaddr;

	return trampoline_vaddr;
}

static void cleanup_return_instances(struct uprobe_task *utask, bool chained,
					struct pt_regs *regs)
{
	struct return_instance *ri = utask->return_instances;
	enum rp_check ctx = chained ? RP_CHECK_CHAIN_CALL : RP_CHECK_CALL;

	while (ri && !arch_uretprobe_is_alive(ri, ctx, regs)) {
		ri = free_ret_instance(ri);
		utask->depth--;
	}
	utask->return_instances = ri;
}

static void prepare_uretprobe(struct uprobe *uprobe, struct pt_regs *regs)
{
	struct return_instance *ri;
	struct uprobe_task *utask;
	unsigned long orig_ret_vaddr, trampoline_vaddr;
	bool chained;

	if (!get_xol_area())
		return;

	utask = get_utask();
	if (!utask)
		return;

	if (utask->depth >= MAX_URETPROBE_DEPTH) {
		printk_ratelimited(KERN_INFO "uprobe: omit uretprobe due to"
				" nestedness limit pid/tgid=%d/%d\n",
				current->pid, current->tgid);
		return;
	}

	ri = kmalloc(sizeof(struct return_instance), GFP_KERNEL);
	if (!ri)
		return;

	trampoline_vaddr = get_trampoline_vaddr();
	orig_ret_vaddr = arch_uretprobe_hijack_return_addr(trampoline_vaddr, regs);
	if (orig_ret_vaddr == -1)
		goto fail;

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
			goto fail;
		}
		orig_ret_vaddr = utask->return_instances->orig_ret_vaddr;
	}

	ri->uprobe = get_uprobe(uprobe);
	ri->func = instruction_pointer(regs);
	ri->stack = user_stack_pointer(regs);
	ri->orig_ret_vaddr = orig_ret_vaddr;
	ri->chained = chained;

	utask->depth++;
	ri->next = utask->return_instances;
	utask->return_instances = ri;

	return;
 fail:
	kfree(ri);
}

/* Prepare to single-step probed instruction out of line. */
static int
pre_ssout(struct uprobe *uprobe, struct pt_regs *regs, unsigned long bp_vaddr)
{
	struct uprobe_task *utask;
	unsigned long xol_vaddr;
	int err;

	utask = get_utask();
	if (!utask)
		return -ENOMEM;

	xol_vaddr = xol_get_insn_slot(uprobe);
	if (!xol_vaddr)
		return -ENOMEM;

	utask->xol_vaddr = xol_vaddr;
	utask->vaddr = bp_vaddr;

	err = arch_uprobe_pre_xol(&uprobe->arch, regs);
	if (unlikely(err)) {
		xol_free_insn_slot(current);
		return err;
	}

	utask->active_uprobe = uprobe;
	utask->state = UTASK_SSTEP;
	return 0;
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

	if (signal_pending(t)) {
		spin_lock_irq(&t->sighand->siglock);
		clear_tsk_thread_flag(t, TIF_SIGPENDING);
		spin_unlock_irq(&t->sighand->siglock);

		if (__fatal_signal_pending(t) || arch_uprobe_xol_was_trapped(t)) {
			utask->state = UTASK_SSTEP_TRAPPED;
			set_tsk_thread_flag(t, TIF_UPROBE);
		}
	}

	return true;
}

static void mmf_recalc_uprobes(struct mm_struct *mm)
{
	struct vm_area_struct *vma;

	for (vma = mm->mmap; vma; vma = vma->vm_next) {
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

	pagefault_disable();
	result = __get_user(opcode, (uprobe_opcode_t __user *)vaddr);
	pagefault_enable();

	if (likely(result == 0))
		goto out;

	/*
	 * The NULL 'tsk' here ensures that any faults that occur here
	 * will not be accounted to the task.  'mm' *is* current->mm,
	 * but we treat this as a 'remote' access since it is
	 * essentially a kernel access to the memory.
	 */
	result = get_user_pages_remote(NULL, mm, vaddr, 1, FOLL_FORCE, &page,
			NULL, NULL);
	if (result < 0)
		return result;

	copy_from_page(page, vaddr, &opcode, UPROBE_SWBP_INSN_SIZE);
	put_page(page);
 out:
	/* This needs to return true for any variant of the trap insn */
	return is_trap_insn(&opcode);
}

static struct uprobe *find_active_uprobe(unsigned long bp_vaddr, int *is_swbp)
{
	struct mm_struct *mm = current->mm;
	struct uprobe *uprobe = NULL;
	struct vm_area_struct *vma;

	down_read(&mm->mmap_sem);
	vma = find_vma(mm, bp_vaddr);
	if (vma && vma->vm_start <= bp_vaddr) {
		if (valid_vma(vma, false)) {
			struct inode *inode = file_inode(vma->vm_file);
			loff_t offset = vaddr_to_offset(vma, bp_vaddr);

			uprobe = find_uprobe(inode, offset);
		}

		if (!uprobe)
			*is_swbp = is_trap_at_addr(mm, bp_vaddr);
	} else {
		*is_swbp = -EFAULT;
	}

	if (!uprobe && test_and_clear_bit(MMF_RECALC_UPROBES, &mm->flags))
		mmf_recalc_uprobes(mm);
	up_read(&mm->mmap_sem);

	return uprobe;
}

static void handler_chain(struct uprobe *uprobe, struct pt_regs *regs)
{
	struct uprobe_consumer *uc;
	int remove = UPROBE_HANDLER_REMOVE;
	bool need_prep = false; /* prepare return uprobe, when needed */

	down_read(&uprobe->register_rwsem);
	for (uc = uprobe->consumers; uc; uc = uc->next) {
		int rc = 0;

		if (uc->handler) {
			rc = uc->handler(uc, regs);
			WARN(rc & ~UPROBE_HANDLER_MASK,
				"bad rc=0x%x from %ps()\n", rc, uc->handler);
		}

		if (uc->ret_handler)
			need_prep = true;

		remove &= rc;
	}

	if (need_prep && !remove)
		prepare_uretprobe(uprobe, regs); /* put bp at return */

	if (remove && uprobe->consumers) {
		WARN_ON(!uprobe_is_active(uprobe));
		unapply_uprobe(uprobe, current->mm);
	}
	up_read(&uprobe->register_rwsem);
}

static void
handle_uretprobe_chain(struct return_instance *ri, struct pt_regs *regs)
{
	struct uprobe *uprobe = ri->uprobe;
	struct uprobe_consumer *uc;

	down_read(&uprobe->register_rwsem);
	for (uc = uprobe->consumers; uc; uc = uc->next) {
		if (uc->ret_handler)
			uc->ret_handler(uc, ri->func, regs);
	}
	up_read(&uprobe->register_rwsem);
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

static void handle_trampoline(struct pt_regs *regs)
{
	struct uprobe_task *utask;
	struct return_instance *ri, *next;
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
		next = find_next_ret_chain(ri);
		valid = !next || arch_uretprobe_is_alive(next, RP_CHECK_RET, regs);

		instruction_pointer_set(regs, ri->orig_ret_vaddr);
		do {
			if (valid)
				handle_uretprobe_chain(ri, regs);
			ri = free_ret_instance(ri);
			utask->depth--;
		} while (ri != next);
	} while (!valid);

	utask->return_instances = ri;
	return;

 sigill:
	uprobe_warn(current, "handle uretprobe, sending SIGILL.");
	force_sig(SIGILL, current);

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
	int uninitialized_var(is_swbp);

	bp_vaddr = uprobe_get_swbp_addr(regs);
	if (bp_vaddr == get_trampoline_vaddr())
		return handle_trampoline(regs);

	uprobe = find_active_uprobe(bp_vaddr, &is_swbp);
	if (!uprobe) {
		if (is_swbp > 0) {
			/* No matching uprobe; signal SIGTRAP. */
			send_sig(SIGTRAP, current, 0);
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
		return;
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

	if (!pre_ssout(uprobe, regs, bp_vaddr))
		return;

	/* arch_uprobe_skip_sstep() succeeded, or restart if can't singlestep */
out:
	put_uprobe(uprobe);
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
	xol_free_insn_slot(current);

	spin_lock_irq(&current->sighand->siglock);
	recalc_sigpending(); /* see uprobe_deny_signal() */
	spin_unlock_irq(&current->sighand->siglock);

	if (unlikely(err)) {
		uprobe_warn(current, "execute the probed insn, sending SIGILL.");
		force_sig(SIGILL, current);
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

	BUG_ON(percpu_init_rwsem(&dup_mmap_sem));

	BUG_ON(register_die_notifier(&uprobe_exception_nb));
}
