/*
 * User-space Probes (UProbes)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright (C) IBM Corporation, 2008-2012
 * Authors:
 *	Srikar Dronamraju
 *	Jim Keniston
 * Copyright (C) 2011-2012 Red Hat, Inc., Peter Zijlstra <pzijlstr@redhat.com>
 */

#include <linux/kernel.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>	/* read_mapping_page */
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/rmap.h>		/* anon_vma_prepare */
#include <linux/mmu_notifier.h>	/* set_pte_at_notify */
#include <linux/swap.h>		/* try_to_free_swap */
#include <linux/ptrace.h>	/* user_enable_single_step */
#include <linux/kdebug.h>	/* notifier mechanism */
#include "../../mm/internal.h"	/* munlock_vma_page */

#include <linux/uprobes.h>

#define UINSNS_PER_PAGE			(PAGE_SIZE/UPROBE_XOL_SLOT_BYTES)
#define MAX_UPROBE_XOL_SLOTS		UINSNS_PER_PAGE

static struct rb_root uprobes_tree = RB_ROOT;

static DEFINE_SPINLOCK(uprobes_treelock);	/* serialize rbtree access */

#define UPROBES_HASH_SZ	13

/*
 * We need separate register/unregister and mmap/munmap lock hashes because
 * of mmap_sem nesting.
 *
 * uprobe_register() needs to install probes on (potentially) all processes
 * and thus needs to acquire multiple mmap_sems (consequtively, not
 * concurrently), whereas uprobe_mmap() is called while holding mmap_sem
 * for the particular process doing the mmap.
 *
 * uprobe_register()->register_for_each_vma() needs to drop/acquire mmap_sem
 * because of lock order against i_mmap_mutex. This means there's a hole in
 * the register vma iteration where a mmap() can happen.
 *
 * Thus uprobe_register() can race with uprobe_mmap() and we can try and
 * install a probe where one is already installed.
 */

/* serialize (un)register */
static struct mutex uprobes_mutex[UPROBES_HASH_SZ];

#define uprobes_hash(v)		(&uprobes_mutex[((unsigned long)(v)) % UPROBES_HASH_SZ])

/* serialize uprobe->pending_list */
static struct mutex uprobes_mmap_mutex[UPROBES_HASH_SZ];
#define uprobes_mmap_hash(v)	(&uprobes_mmap_mutex[((unsigned long)(v)) % UPROBES_HASH_SZ])

/*
 * uprobe_events allows us to skip the uprobe_mmap if there are no uprobe
 * events active at this time.  Probably a fine grained per inode count is
 * better?
 */
static atomic_t uprobe_events = ATOMIC_INIT(0);

struct uprobe {
	struct rb_node		rb_node;	/* node in the rb tree */
	atomic_t		ref;
	struct rw_semaphore	consumer_rwsem;
	struct list_head	pending_list;
	struct uprobe_consumer	*consumers;
	struct inode		*inode;		/* Also hold a ref to inode */
	loff_t			offset;
	int			flags;
	struct arch_uprobe	arch;
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
	if (!vma->vm_file)
		return false;

	if (!is_register)
		return true;

	if ((vma->vm_flags & (VM_HUGETLB|VM_READ|VM_WRITE|VM_EXEC|VM_SHARED))
				== (VM_READ|VM_EXEC))
		return true;

	return false;
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
				struct page *page, struct page *kpage)
{
	struct mm_struct *mm = vma->vm_mm;
	spinlock_t *ptl;
	pte_t *ptep;
	int err;

	/* For try_to_free_swap() and munlock_vma_page() below */
	lock_page(page);

	err = -EAGAIN;
	ptep = page_check_address(page, mm, addr, &ptl, 0);
	if (!ptep)
		goto unlock;

	get_page(kpage);
	page_add_new_anon_rmap(kpage, vma, addr);

	if (!PageAnon(page)) {
		dec_mm_counter(mm, MM_FILEPAGES);
		inc_mm_counter(mm, MM_ANONPAGES);
	}

	flush_cache_page(vma, addr, pte_pfn(*ptep));
	ptep_clear_flush(vma, addr, ptep);
	set_pte_at_notify(mm, addr, ptep, mk_pte(kpage, vma->vm_page_prot));

	page_remove_rmap(page);
	if (!page_mapped(page))
		try_to_free_swap(page);
	pte_unmap_unlock(ptep, ptl);

	if (vma->vm_flags & VM_LOCKED)
		munlock_vma_page(page);
	put_page(page);

	err = 0;
 unlock:
	unlock_page(page);
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

/*
 * NOTE:
 * Expect the breakpoint instruction to be the smallest size instruction for
 * the architecture. If an arch has variable length instruction and the
 * breakpoint instruction is not of the smallest length instruction
 * supported by that architecture then we need to modify read_opcode /
 * write_opcode accordingly. This would never be a problem for archs that
 * have fixed length instructions.
 */

/*
 * write_opcode - write the opcode at a given virtual address.
 * @auprobe: arch breakpointing information.
 * @mm: the probed process address space.
 * @vaddr: the virtual address to store the opcode.
 * @opcode: opcode to be written at @vaddr.
 *
 * Called with mm->mmap_sem held (for read and with a reference to
 * mm).
 *
 * For mm @mm, write the opcode at @vaddr.
 * Return 0 (success) or a negative errno.
 */
static int write_opcode(struct arch_uprobe *auprobe, struct mm_struct *mm,
			unsigned long vaddr, uprobe_opcode_t opcode)
{
	struct page *old_page, *new_page;
	void *vaddr_old, *vaddr_new;
	struct vm_area_struct *vma;
	int ret;

retry:
	/* Read the page with vaddr into memory */
	ret = get_user_pages(NULL, mm, vaddr, 1, 0, 0, &old_page, &vma);
	if (ret <= 0)
		return ret;

	ret = -ENOMEM;
	new_page = alloc_page_vma(GFP_HIGHUSER_MOVABLE, vma, vaddr);
	if (!new_page)
		goto put_old;

	__SetPageUptodate(new_page);

	/* copy the page now that we've got it stable */
	vaddr_old = kmap_atomic(old_page);
	vaddr_new = kmap_atomic(new_page);

	memcpy(vaddr_new, vaddr_old, PAGE_SIZE);
	memcpy(vaddr_new + (vaddr & ~PAGE_MASK), &opcode, UPROBE_SWBP_INSN_SIZE);

	kunmap_atomic(vaddr_new);
	kunmap_atomic(vaddr_old);

	ret = anon_vma_prepare(vma);
	if (ret)
		goto put_new;

	ret = __replace_page(vma, vaddr, old_page, new_page);

put_new:
	page_cache_release(new_page);
put_old:
	put_page(old_page);

	if (unlikely(ret == -EAGAIN))
		goto retry;
	return ret;
}

/**
 * read_opcode - read the opcode at a given virtual address.
 * @mm: the probed process address space.
 * @vaddr: the virtual address to read the opcode.
 * @opcode: location to store the read opcode.
 *
 * Called with mm->mmap_sem held (for read and with a reference to
 * mm.
 *
 * For mm @mm, read the opcode at @vaddr and store it in @opcode.
 * Return 0 (success) or a negative errno.
 */
static int read_opcode(struct mm_struct *mm, unsigned long vaddr, uprobe_opcode_t *opcode)
{
	struct page *page;
	void *vaddr_new;
	int ret;

	ret = get_user_pages(NULL, mm, vaddr, 1, 0, 1, &page, NULL);
	if (ret <= 0)
		return ret;

	vaddr_new = kmap_atomic(page);
	vaddr &= ~PAGE_MASK;
	memcpy(opcode, vaddr_new + vaddr, UPROBE_SWBP_INSN_SIZE);
	kunmap_atomic(vaddr_new);

	put_page(page);

	return 0;
}

static int is_swbp_at_addr(struct mm_struct *mm, unsigned long vaddr)
{
	uprobe_opcode_t opcode;
	int result;

	if (current->mm == mm) {
		pagefault_disable();
		result = __copy_from_user_inatomic(&opcode, (void __user*)vaddr,
								sizeof(opcode));
		pagefault_enable();

		if (likely(result == 0))
			goto out;
	}

	result = read_opcode(mm, vaddr, &opcode);
	if (result)
		return result;
out:
	if (is_swbp_insn(&opcode))
		return 1;

	return 0;
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
	int result;
	/*
	 * See the comment near uprobes_hash().
	 */
	result = is_swbp_at_addr(mm, vaddr);
	if (result == 1)
		return 0;

	if (result)
		return result;

	return write_opcode(auprobe, mm, vaddr, UPROBE_SWBP_INSN);
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
	int result;

	result = is_swbp_at_addr(mm, vaddr);
	if (!result)
		return -EINVAL;

	if (result != 1)
		return result;

	return write_opcode(auprobe, mm, vaddr, *(uprobe_opcode_t *)auprobe->insn);
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
		if (!match) {
			atomic_inc(&uprobe->ref);
			return uprobe;
		}

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
		if (!match) {
			atomic_inc(&u->ref);
			return u;
		}

		if (match < 0)
			p = &parent->rb_left;
		else
			p = &parent->rb_right;

	}

	u = NULL;
	rb_link_node(&uprobe->rb_node, parent, p);
	rb_insert_color(&uprobe->rb_node, &uprobes_tree);
	/* get access + creation ref */
	atomic_set(&uprobe->ref, 2);

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

	/* For now assume that the instruction need not be single-stepped */
	uprobe->flags |= UPROBE_SKIP_SSTEP;

	return u;
}

static void put_uprobe(struct uprobe *uprobe)
{
	if (atomic_dec_and_test(&uprobe->ref))
		kfree(uprobe);
}

static struct uprobe *alloc_uprobe(struct inode *inode, loff_t offset)
{
	struct uprobe *uprobe, *cur_uprobe;

	uprobe = kzalloc(sizeof(struct uprobe), GFP_KERNEL);
	if (!uprobe)
		return NULL;

	uprobe->inode = igrab(inode);
	uprobe->offset = offset;
	init_rwsem(&uprobe->consumer_rwsem);

	/* add to uprobes_tree, sorted on inode:offset */
	cur_uprobe = insert_uprobe(uprobe);

	/* a uprobe exists for this inode:offset combination */
	if (cur_uprobe) {
		kfree(uprobe);
		uprobe = cur_uprobe;
		iput(inode);
	} else {
		atomic_inc(&uprobe_events);
	}

	return uprobe;
}

static void handler_chain(struct uprobe *uprobe, struct pt_regs *regs)
{
	struct uprobe_consumer *uc;

	if (!(uprobe->flags & UPROBE_RUN_HANDLER))
		return;

	down_read(&uprobe->consumer_rwsem);
	for (uc = uprobe->consumers; uc; uc = uc->next) {
		if (!uc->filter || uc->filter(uc, current))
			uc->handler(uc, regs);
	}
	up_read(&uprobe->consumer_rwsem);
}

/* Returns the previous consumer */
static struct uprobe_consumer *
consumer_add(struct uprobe *uprobe, struct uprobe_consumer *uc)
{
	down_write(&uprobe->consumer_rwsem);
	uc->next = uprobe->consumers;
	uprobe->consumers = uc;
	up_write(&uprobe->consumer_rwsem);

	return uc->next;
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

static int
__copy_insn(struct address_space *mapping, struct file *filp, char *insn,
			unsigned long nbytes, loff_t offset)
{
	struct page *page;
	void *vaddr;
	unsigned long off;
	pgoff_t idx;

	if (!filp)
		return -EINVAL;

	if (!mapping->a_ops->readpage)
		return -EIO;

	idx = offset >> PAGE_CACHE_SHIFT;
	off = offset & ~PAGE_MASK;

	/*
	 * Ensure that the page that has the original instruction is
	 * populated and in page-cache.
	 */
	page = read_mapping_page(mapping, idx, filp);
	if (IS_ERR(page))
		return PTR_ERR(page);

	vaddr = kmap_atomic(page);
	memcpy(insn, vaddr + off, nbytes);
	kunmap_atomic(vaddr);
	page_cache_release(page);

	return 0;
}

static int copy_insn(struct uprobe *uprobe, struct file *filp)
{
	struct address_space *mapping;
	unsigned long nbytes;
	int bytes;

	nbytes = PAGE_SIZE - (uprobe->offset & ~PAGE_MASK);
	mapping = uprobe->inode->i_mapping;

	/* Instruction at end of binary; copy only available bytes */
	if (uprobe->offset + MAX_UINSN_BYTES > uprobe->inode->i_size)
		bytes = uprobe->inode->i_size - uprobe->offset;
	else
		bytes = MAX_UINSN_BYTES;

	/* Instruction at the page-boundary; copy bytes in second page */
	if (nbytes < bytes) {
		int err = __copy_insn(mapping, filp, uprobe->arch.insn + nbytes,
				bytes - nbytes, uprobe->offset + nbytes);
		if (err)
			return err;
		bytes = nbytes;
	}
	return __copy_insn(mapping, filp, uprobe->arch.insn, bytes, uprobe->offset);
}

/*
 * How mm->uprobes_state.count gets updated
 * uprobe_mmap() increments the count if
 * 	- it successfully adds a breakpoint.
 * 	- it cannot add a breakpoint, but sees that there is a underlying
 * 	  breakpoint (via a is_swbp_at_addr()).
 *
 * uprobe_munmap() decrements the count if
 * 	- it sees a underlying breakpoint, (via is_swbp_at_addr)
 * 	  (Subsequent uprobe_unregister wouldnt find the breakpoint
 * 	  unless a uprobe_mmap kicks in, since the old vma would be
 * 	  dropped just after uprobe_munmap.)
 *
 * uprobe_register increments the count if:
 * 	- it successfully adds a breakpoint.
 *
 * uprobe_unregister decrements the count if:
 * 	- it sees a underlying breakpoint and removes successfully.
 * 	  (via is_swbp_at_addr)
 * 	  (Subsequent uprobe_munmap wouldnt find the breakpoint
 * 	  since there is no underlying breakpoint after the
 * 	  breakpoint removal.)
 */
static int
install_breakpoint(struct uprobe *uprobe, struct mm_struct *mm,
			struct vm_area_struct *vma, unsigned long vaddr)
{
	bool first_uprobe;
	int ret;

	/*
	 * If probe is being deleted, unregister thread could be done with
	 * the vma-rmap-walk through. Adding a probe now can be fatal since
	 * nobody will be able to cleanup. Also we could be from fork or
	 * mremap path, where the probe might have already been inserted.
	 * Hence behave as if probe already existed.
	 */
	if (!uprobe->consumers)
		return 0;

	if (!(uprobe->flags & UPROBE_COPY_INSN)) {
		ret = copy_insn(uprobe, vma->vm_file);
		if (ret)
			return ret;

		if (is_swbp_insn((uprobe_opcode_t *)uprobe->arch.insn))
			return -ENOTSUPP;

		ret = arch_uprobe_analyze_insn(&uprobe->arch, mm, vaddr);
		if (ret)
			return ret;

		/* write_opcode() assumes we don't cross page boundary */
		BUG_ON((uprobe->offset & ~PAGE_MASK) +
				UPROBE_SWBP_INSN_SIZE > PAGE_SIZE);

		uprobe->flags |= UPROBE_COPY_INSN;
	}

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

static void
remove_breakpoint(struct uprobe *uprobe, struct mm_struct *mm, unsigned long vaddr)
{
	/* can happen if uprobe_register() fails */
	if (!test_bit(MMF_HAS_UPROBES, &mm->flags))
		return;

	set_bit(MMF_RECALC_UPROBES, &mm->flags);
	set_orig_insn(&uprobe->arch, mm, vaddr);
}

/*
 * There could be threads that have already hit the breakpoint. They
 * will recheck the current insn and restart if find_uprobe() fails.
 * See find_active_uprobe().
 */
static void delete_uprobe(struct uprobe *uprobe)
{
	spin_lock(&uprobes_treelock);
	rb_erase(&uprobe->rb_node, &uprobes_tree);
	spin_unlock(&uprobes_treelock);
	iput(uprobe->inode);
	put_uprobe(uprobe);
	atomic_dec(&uprobe_events);
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
	struct prio_tree_iter iter;
	struct vm_area_struct *vma;
	struct map_info *curr = NULL;
	struct map_info *prev = NULL;
	struct map_info *info;
	int more = 0;

 again:
	mutex_lock(&mapping->i_mmap_mutex);
	vma_prio_tree_foreach(vma, &iter, &mapping->i_mmap, pgoff, pgoff) {
		if (!valid_vma(vma, is_register))
			continue;

		if (!prev && !more) {
			/*
			 * Needs GFP_NOWAIT to avoid i_mmap_mutex recursion through
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

		if (!atomic_inc_not_zero(&vma->vm_mm->mm_users))
			continue;

		info = prev;
		prev = prev->next;
		info->next = curr;
		curr = info;

		info->mm = vma->vm_mm;
		info->vaddr = offset_to_vaddr(vma, offset);
	}
	mutex_unlock(&mapping->i_mmap_mutex);

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

static int register_for_each_vma(struct uprobe *uprobe, bool is_register)
{
	struct map_info *info;
	int err = 0;

	info = build_map_info(uprobe->inode->i_mapping,
					uprobe->offset, is_register);
	if (IS_ERR(info))
		return PTR_ERR(info);

	while (info) {
		struct mm_struct *mm = info->mm;
		struct vm_area_struct *vma;

		if (err)
			goto free;

		down_write(&mm->mmap_sem);
		vma = find_vma(mm, info->vaddr);
		if (!vma || !valid_vma(vma, is_register) ||
		    vma->vm_file->f_mapping->host != uprobe->inode)
			goto unlock;

		if (vma->vm_start > info->vaddr ||
		    vaddr_to_offset(vma, info->vaddr) != uprobe->offset)
			goto unlock;

		if (is_register)
			err = install_breakpoint(uprobe, mm, vma, info->vaddr);
		else
			remove_breakpoint(uprobe, mm, info->vaddr);

 unlock:
		up_write(&mm->mmap_sem);
 free:
		mmput(mm);
		info = free_map_info(info);
	}

	return err;
}

static int __uprobe_register(struct uprobe *uprobe)
{
	return register_for_each_vma(uprobe, true);
}

static void __uprobe_unregister(struct uprobe *uprobe)
{
	if (!register_for_each_vma(uprobe, false))
		delete_uprobe(uprobe);

	/* TODO : cant unregister? schedule a worker thread */
}

/*
 * uprobe_register - register a probe
 * @inode: the file in which the probe has to be placed.
 * @offset: offset from the start of the file.
 * @uc: information on howto handle the probe..
 *
 * Apart from the access refcount, uprobe_register() takes a creation
 * refcount (thro alloc_uprobe) if and only if this @uprobe is getting
 * inserted into the rbtree (i.e first consumer for a @inode:@offset
 * tuple).  Creation refcount stops uprobe_unregister from freeing the
 * @uprobe even before the register operation is complete. Creation
 * refcount is released when the last @uc for the @uprobe
 * unregisters.
 *
 * Return errno if it cannot successully install probes
 * else return 0 (success)
 */
int uprobe_register(struct inode *inode, loff_t offset, struct uprobe_consumer *uc)
{
	struct uprobe *uprobe;
	int ret;

	if (!inode || !uc || uc->next)
		return -EINVAL;

	if (offset > i_size_read(inode))
		return -EINVAL;

	ret = 0;
	mutex_lock(uprobes_hash(inode));
	uprobe = alloc_uprobe(inode, offset);

	if (uprobe && !consumer_add(uprobe, uc)) {
		ret = __uprobe_register(uprobe);
		if (ret) {
			uprobe->consumers = NULL;
			__uprobe_unregister(uprobe);
		} else {
			uprobe->flags |= UPROBE_RUN_HANDLER;
		}
	}

	mutex_unlock(uprobes_hash(inode));
	if (uprobe)
		put_uprobe(uprobe);

	return ret;
}

/*
 * uprobe_unregister - unregister a already registered probe.
 * @inode: the file in which the probe has to be removed.
 * @offset: offset from the start of the file.
 * @uc: identify which probe if multiple probes are colocated.
 */
void uprobe_unregister(struct inode *inode, loff_t offset, struct uprobe_consumer *uc)
{
	struct uprobe *uprobe;

	if (!inode || !uc)
		return;

	uprobe = find_uprobe(inode, offset);
	if (!uprobe)
		return;

	mutex_lock(uprobes_hash(inode));

	if (consumer_del(uprobe, uc)) {
		if (!uprobe->consumers) {
			__uprobe_unregister(uprobe);
			uprobe->flags &= ~UPROBE_RUN_HANDLER;
		}
	}

	mutex_unlock(uprobes_hash(inode));
	if (uprobe)
		put_uprobe(uprobe);
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
			atomic_inc(&u->ref);
		}
		for (t = n; (t = rb_next(t)); ) {
			u = rb_entry(t, struct uprobe, rb_node);
			if (u->inode != inode || u->offset > max)
				break;
			list_add(&u->pending_list, head);
			atomic_inc(&u->ref);
		}
	}
	spin_unlock(&uprobes_treelock);
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

	if (!atomic_read(&uprobe_events) || !valid_vma(vma, true))
		return 0;

	inode = vma->vm_file->f_mapping->host;
	if (!inode)
		return 0;

	mutex_lock(uprobes_mmap_hash(inode));
	build_probe_list(inode, vma, vma->vm_start, vma->vm_end, &tmp_list);

	list_for_each_entry_safe(uprobe, u, &tmp_list, pending_list) {
		if (!fatal_signal_pending(current)) {
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

	inode = vma->vm_file->f_mapping->host;

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
	if (!atomic_read(&uprobe_events) || !valid_vma(vma, false))
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
static int xol_add_vma(struct xol_area *area)
{
	struct mm_struct *mm;
	int ret;

	area->page = alloc_page(GFP_HIGHUSER);
	if (!area->page)
		return -ENOMEM;

	ret = -EALREADY;
	mm = current->mm;

	down_write(&mm->mmap_sem);
	if (mm->uprobes_state.xol_area)
		goto fail;

	ret = -ENOMEM;

	/* Try to map as high as possible, this is only a hint. */
	area->vaddr = get_unmapped_area(NULL, TASK_SIZE - PAGE_SIZE, PAGE_SIZE, 0, 0);
	if (area->vaddr & ~PAGE_MASK) {
		ret = area->vaddr;
		goto fail;
	}

	ret = install_special_mapping(mm, area->vaddr, PAGE_SIZE,
				VM_EXEC|VM_MAYEXEC|VM_DONTCOPY|VM_IO, &area->page);
	if (ret)
		goto fail;

	smp_wmb();	/* pairs with get_xol_area() */
	mm->uprobes_state.xol_area = area;
	ret = 0;

fail:
	up_write(&mm->mmap_sem);
	if (ret)
		__free_page(area->page);

	return ret;
}

static struct xol_area *get_xol_area(struct mm_struct *mm)
{
	struct xol_area *area;

	area = mm->uprobes_state.xol_area;
	smp_read_barrier_depends();	/* pairs with wmb in xol_add_vma() */

	return area;
}

/*
 * xol_alloc_area - Allocate process's xol_area.
 * This area will be used for storing instructions for execution out of
 * line.
 *
 * Returns the allocated area or NULL.
 */
static struct xol_area *xol_alloc_area(void)
{
	struct xol_area *area;

	area = kzalloc(sizeof(*area), GFP_KERNEL);
	if (unlikely(!area))
		return NULL;

	area->bitmap = kzalloc(BITS_TO_LONGS(UINSNS_PER_PAGE) * sizeof(long), GFP_KERNEL);

	if (!area->bitmap)
		goto fail;

	init_waitqueue_head(&area->wq);
	if (!xol_add_vma(area))
		return area;

fail:
	kfree(area->bitmap);
	kfree(area);

	return get_xol_area(current->mm);
}

/*
 * uprobe_clear_state - Free the area allocated for slots.
 */
void uprobe_clear_state(struct mm_struct *mm)
{
	struct xol_area *area = mm->uprobes_state.xol_area;

	if (!area)
		return;

	put_page(area->page);
	kfree(area->bitmap);
	kfree(area);
}

void uprobe_dup_mmap(struct mm_struct *oldmm, struct mm_struct *newmm)
{
	newmm->uprobes_state.xol_area = NULL;

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
 * xol_get_insn_slot - If was not allocated a slot, then
 * allocate a slot.
 * Returns the allocated slot address or 0.
 */
static unsigned long xol_get_insn_slot(struct uprobe *uprobe, unsigned long slot_addr)
{
	struct xol_area *area;
	unsigned long offset;
	void *vaddr;

	area = get_xol_area(current->mm);
	if (!area) {
		area = xol_alloc_area();
		if (!area)
			return 0;
	}
	current->utask->xol_vaddr = xol_take_insn_slot(area);

	/*
	 * Initialize the slot if xol_vaddr points to valid
	 * instruction slot.
	 */
	if (unlikely(!current->utask->xol_vaddr))
		return 0;

	current->utask->vaddr = slot_addr;
	offset = current->utask->xol_vaddr & ~PAGE_MASK;
	vaddr = kmap_atomic(area->page);
	memcpy(vaddr + offset, uprobe->arch.insn, MAX_UINSN_BYTES);
	kunmap_atomic(vaddr);

	return current->utask->xol_vaddr;
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

	if (unlikely(!slot_addr || IS_ERR_VALUE(slot_addr)))
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
		if (waitqueue_active(&area->wq))
			wake_up(&area->wq);

		tsk->utask->xol_vaddr = 0;
	}
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

/*
 * Called with no locks held.
 * Called in context of a exiting or a exec-ing thread.
 */
void uprobe_free_utask(struct task_struct *t)
{
	struct uprobe_task *utask = t->utask;

	if (!utask)
		return;

	if (utask->active_uprobe)
		put_uprobe(utask->active_uprobe);

	xol_free_insn_slot(t);
	kfree(utask);
	t->utask = NULL;
}

/*
 * Called in context of a new clone/fork from copy_process.
 */
void uprobe_copy_process(struct task_struct *t)
{
	t->utask = NULL;
}

/*
 * Allocate a uprobe_task object for the task.
 * Called when the thread hits a breakpoint for the first time.
 *
 * Returns:
 * - pointer to new uprobe_task on success
 * - NULL otherwise
 */
static struct uprobe_task *add_utask(void)
{
	struct uprobe_task *utask;

	utask = kzalloc(sizeof *utask, GFP_KERNEL);
	if (unlikely(!utask))
		return NULL;

	current->utask = utask;
	return utask;
}

/* Prepare to single-step probed instruction out of line. */
static int
pre_ssout(struct uprobe *uprobe, struct pt_regs *regs, unsigned long vaddr)
{
	if (xol_get_insn_slot(uprobe, vaddr) && !arch_uprobe_pre_xol(&uprobe->arch, regs))
		return 0;

	return -EFAULT;
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
			set_tsk_thread_flag(t, TIF_NOTIFY_RESUME);
		}
	}

	return true;
}

/*
 * Avoid singlestepping the original instruction if the original instruction
 * is a NOP or can be emulated.
 */
static bool can_skip_sstep(struct uprobe *uprobe, struct pt_regs *regs)
{
	if (arch_uprobe_skip_sstep(&uprobe->arch, regs))
		return true;

	uprobe->flags &= ~UPROBE_SKIP_SSTEP;
	return false;
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
		 */
		if (vma_has_uprobes(vma, vma->vm_start, vma->vm_end))
			return;
	}

	clear_bit(MMF_HAS_UPROBES, &mm->flags);
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
			struct inode *inode = vma->vm_file->f_mapping->host;
			loff_t offset = vaddr_to_offset(vma, bp_vaddr);

			uprobe = find_uprobe(inode, offset);
		}

		if (!uprobe)
			*is_swbp = is_swbp_at_addr(mm, bp_vaddr);
	} else {
		*is_swbp = -EFAULT;
	}

	if (!uprobe && test_and_clear_bit(MMF_RECALC_UPROBES, &mm->flags))
		mmf_recalc_uprobes(mm);
	up_read(&mm->mmap_sem);

	return uprobe;
}

void __weak arch_uprobe_enable_step(struct arch_uprobe *arch)
{
	user_enable_single_step(current);
}

void __weak arch_uprobe_disable_step(struct arch_uprobe *arch)
{
	user_disable_single_step(current);
}

/*
 * Run handler and ask thread to singlestep.
 * Ensure all non-fatal signals cannot interrupt thread while it singlesteps.
 */
static void handle_swbp(struct pt_regs *regs)
{
	struct uprobe_task *utask;
	struct uprobe *uprobe;
	unsigned long bp_vaddr;
	int uninitialized_var(is_swbp);

	bp_vaddr = uprobe_get_swbp_addr(regs);
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

	utask = current->utask;
	if (!utask) {
		utask = add_utask();
		/* Cannot allocate; re-execute the instruction. */
		if (!utask)
			goto cleanup_ret;
	}
	utask->active_uprobe = uprobe;
	handler_chain(uprobe, regs);
	if (uprobe->flags & UPROBE_SKIP_SSTEP && can_skip_sstep(uprobe, regs))
		goto cleanup_ret;

	utask->state = UTASK_SSTEP;
	if (!pre_ssout(uprobe, regs, bp_vaddr)) {
		arch_uprobe_enable_step(&uprobe->arch);
		return;
	}

cleanup_ret:
	if (utask) {
		utask->active_uprobe = NULL;
		utask->state = UTASK_RUNNING;
	}
	if (!(uprobe->flags & UPROBE_SKIP_SSTEP))

		/*
		 * cannot singlestep; cannot skip instruction;
		 * re-execute the instruction.
		 */
		instruction_pointer_set(regs, bp_vaddr);

	put_uprobe(uprobe);
}

/*
 * Perform required fix-ups and disable singlestep.
 * Allow pending signals to take effect.
 */
static void handle_singlestep(struct uprobe_task *utask, struct pt_regs *regs)
{
	struct uprobe *uprobe;

	uprobe = utask->active_uprobe;
	if (utask->state == UTASK_SSTEP_ACK)
		arch_uprobe_post_xol(&uprobe->arch, regs);
	else if (utask->state == UTASK_SSTEP_TRAPPED)
		arch_uprobe_abort_xol(&uprobe->arch, regs);
	else
		WARN_ON_ONCE(1);

	arch_uprobe_disable_step(&uprobe->arch);
	put_uprobe(uprobe);
	utask->active_uprobe = NULL;
	utask->state = UTASK_RUNNING;
	xol_free_insn_slot(current);

	spin_lock_irq(&current->sighand->siglock);
	recalc_sigpending(); /* see uprobe_deny_signal() */
	spin_unlock_irq(&current->sighand->siglock);
}

/*
 * On breakpoint hit, breakpoint notifier sets the TIF_UPROBE flag.  (and on
 * subsequent probe hits on the thread sets the state to UTASK_BP_HIT) and
 * allows the thread to return from interrupt.
 *
 * On singlestep exception, singlestep notifier sets the TIF_UPROBE flag and
 * also sets the state to UTASK_SSTEP_ACK and allows the thread to return from
 * interrupt.
 *
 * While returning to userspace, thread notices the TIF_UPROBE flag and calls
 * uprobe_notify_resume().
 */
void uprobe_notify_resume(struct pt_regs *regs)
{
	struct uprobe_task *utask;

	utask = current->utask;
	if (!utask || utask->state == UTASK_BP_HIT)
		handle_swbp(regs);
	else
		handle_singlestep(utask, regs);
}

/*
 * uprobe_pre_sstep_notifier gets called from interrupt context as part of
 * notifier mechanism. Set TIF_UPROBE flag and indicate breakpoint hit.
 */
int uprobe_pre_sstep_notifier(struct pt_regs *regs)
{
	struct uprobe_task *utask;

	if (!current->mm || !test_bit(MMF_HAS_UPROBES, &current->mm->flags))
		return 0;

	utask = current->utask;
	if (utask)
		utask->state = UTASK_BP_HIT;

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

static int __init init_uprobes(void)
{
	int i;

	for (i = 0; i < UPROBES_HASH_SZ; i++) {
		mutex_init(&uprobes_mutex[i]);
		mutex_init(&uprobes_mmap_mutex[i]);
	}

	return register_die_notifier(&uprobe_exception_nb);
}
module_init(init_uprobes);

static void __exit exit_uprobes(void)
{
}
module_exit(exit_uprobes);
