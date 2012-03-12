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

#include <linux/uprobes.h>

static struct rb_root uprobes_tree = RB_ROOT;

static DEFINE_SPINLOCK(uprobes_treelock);	/* serialize rbtree access */

#define UPROBES_HASH_SZ	13

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

/*
 * Maintain a temporary per vma info that can be used to search if a vma
 * has already been handled. This structure is introduced since extending
 * vm_area_struct wasnt recommended.
 */
struct vma_info {
	struct list_head	probe_list;
	struct mm_struct	*mm;
	loff_t			vaddr;
};

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

	if ((vma->vm_flags & (VM_READ|VM_WRITE|VM_EXEC|VM_SHARED)) == (VM_READ|VM_EXEC))
		return true;

	return false;
}

static loff_t vma_address(struct vm_area_struct *vma, loff_t offset)
{
	loff_t vaddr;

	vaddr = vma->vm_start + offset;
	vaddr -= vma->vm_pgoff << PAGE_SHIFT;

	return vaddr;
}

/**
 * __replace_page - replace page in vma by new page.
 * based on replace_page in mm/ksm.c
 *
 * @vma:      vma that holds the pte pointing to page
 * @page:     the cowed page we are replacing by kpage
 * @kpage:    the modified page we replace page by
 *
 * Returns 0 on success, -EFAULT on failure.
 */
static int __replace_page(struct vm_area_struct *vma, struct page *page, struct page *kpage)
{
	struct mm_struct *mm = vma->vm_mm;
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *ptep;
	spinlock_t *ptl;
	unsigned long addr;
	int err = -EFAULT;

	addr = page_address_in_vma(page, vma);
	if (addr == -EFAULT)
		goto out;

	pgd = pgd_offset(mm, addr);
	if (!pgd_present(*pgd))
		goto out;

	pud = pud_offset(pgd, addr);
	if (!pud_present(*pud))
		goto out;

	pmd = pmd_offset(pud, addr);
	if (!pmd_present(*pmd))
		goto out;

	ptep = pte_offset_map_lock(mm, pmd, addr, &ptl);
	if (!ptep)
		goto out;

	get_page(kpage);
	page_add_new_anon_rmap(kpage, vma, addr);

	flush_cache_page(vma, addr, pte_pfn(*ptep));
	ptep_clear_flush(vma, addr, ptep);
	set_pte_at_notify(mm, addr, ptep, mk_pte(kpage, vma->vm_page_prot));

	page_remove_rmap(page);
	if (!page_mapped(page))
		try_to_free_swap(page);
	put_page(page);
	pte_unmap_unlock(ptep, ptl);
	err = 0;

out:
	return err;
}

/**
 * is_bkpt_insn - check if instruction is breakpoint instruction.
 * @insn: instruction to be checked.
 * Default implementation of is_bkpt_insn
 * Returns true if @insn is a breakpoint instruction.
 */
bool __weak is_bkpt_insn(uprobe_opcode_t *insn)
{
	return *insn == UPROBE_BKPT_INSN;
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
 * @mm: the probed process address space.
 * @arch_uprobe: the breakpointing information.
 * @vaddr: the virtual address to store the opcode.
 * @opcode: opcode to be written at @vaddr.
 *
 * Called with mm->mmap_sem held (for read and with a reference to
 * mm).
 *
 * For mm @mm, write the opcode at @vaddr.
 * Return 0 (success) or a negative errno.
 */
static int write_opcode(struct mm_struct *mm, struct arch_uprobe *auprobe,
			unsigned long vaddr, uprobe_opcode_t opcode)
{
	struct page *old_page, *new_page;
	struct address_space *mapping;
	void *vaddr_old, *vaddr_new;
	struct vm_area_struct *vma;
	struct uprobe *uprobe;
	loff_t addr;
	int ret;

	/* Read the page with vaddr into memory */
	ret = get_user_pages(NULL, mm, vaddr, 1, 0, 0, &old_page, &vma);
	if (ret <= 0)
		return ret;

	ret = -EINVAL;

	/*
	 * We are interested in text pages only. Our pages of interest
	 * should be mapped for read and execute only. We desist from
	 * adding probes in write mapped pages since the breakpoints
	 * might end up in the file copy.
	 */
	if (!valid_vma(vma, is_bkpt_insn(&opcode)))
		goto put_out;

	uprobe = container_of(auprobe, struct uprobe, arch);
	mapping = uprobe->inode->i_mapping;
	if (mapping != vma->vm_file->f_mapping)
		goto put_out;

	addr = vma_address(vma, uprobe->offset);
	if (vaddr != (unsigned long)addr)
		goto put_out;

	ret = -ENOMEM;
	new_page = alloc_page_vma(GFP_HIGHUSER_MOVABLE, vma, vaddr);
	if (!new_page)
		goto put_out;

	__SetPageUptodate(new_page);

	/*
	 * lock page will serialize against do_wp_page()'s
	 * PageAnon() handling
	 */
	lock_page(old_page);
	/* copy the page now that we've got it stable */
	vaddr_old = kmap_atomic(old_page);
	vaddr_new = kmap_atomic(new_page);

	memcpy(vaddr_new, vaddr_old, PAGE_SIZE);

	/* poke the new insn in, ASSUMES we don't cross page boundary */
	vaddr &= ~PAGE_MASK;
	BUG_ON(vaddr + UPROBE_BKPT_INSN_SIZE > PAGE_SIZE);
	memcpy(vaddr_new + vaddr, &opcode, UPROBE_BKPT_INSN_SIZE);

	kunmap_atomic(vaddr_new);
	kunmap_atomic(vaddr_old);

	ret = anon_vma_prepare(vma);
	if (ret)
		goto unlock_out;

	lock_page(new_page);
	ret = __replace_page(vma, old_page, new_page);
	unlock_page(new_page);

unlock_out:
	unlock_page(old_page);
	page_cache_release(new_page);

put_out:
	put_page(old_page);

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

	ret = get_user_pages(NULL, mm, vaddr, 1, 0, 0, &page, NULL);
	if (ret <= 0)
		return ret;

	lock_page(page);
	vaddr_new = kmap_atomic(page);
	vaddr &= ~PAGE_MASK;
	memcpy(opcode, vaddr_new + vaddr, UPROBE_BKPT_INSN_SIZE);
	kunmap_atomic(vaddr_new);
	unlock_page(page);

	put_page(page);

	return 0;
}

static int is_bkpt_at_addr(struct mm_struct *mm, unsigned long vaddr)
{
	uprobe_opcode_t opcode;
	int result;

	result = read_opcode(mm, vaddr, &opcode);
	if (result)
		return result;

	if (is_bkpt_insn(&opcode))
		return 1;

	return 0;
}

/**
 * set_bkpt - store breakpoint at a given address.
 * @mm: the probed process address space.
 * @uprobe: the probepoint information.
 * @vaddr: the virtual address to insert the opcode.
 *
 * For mm @mm, store the breakpoint instruction at @vaddr.
 * Return 0 (success) or a negative errno.
 */
int __weak set_bkpt(struct mm_struct *mm, struct arch_uprobe *auprobe, unsigned long vaddr)
{
	int result;

	result = is_bkpt_at_addr(mm, vaddr);
	if (result == 1)
		return -EEXIST;

	if (result)
		return result;

	return write_opcode(mm, auprobe, vaddr, UPROBE_BKPT_INSN);
}

/**
 * set_orig_insn - Restore the original instruction.
 * @mm: the probed process address space.
 * @uprobe: the probepoint information.
 * @vaddr: the virtual address to insert the opcode.
 * @verify: if true, verify existance of breakpoint instruction.
 *
 * For mm @mm, restore the original opcode (opcode) at @vaddr.
 * Return 0 (success) or a negative errno.
 */
int __weak
set_orig_insn(struct mm_struct *mm, struct arch_uprobe *auprobe, unsigned long vaddr, bool verify)
{
	if (verify) {
		int result;

		result = is_bkpt_at_addr(mm, vaddr);
		if (!result)
			return -EINVAL;

		if (result != 1)
			return result;
	}
	return write_opcode(mm, auprobe, vaddr, *(uprobe_opcode_t *)auprobe->insn);
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
	unsigned long flags;

	spin_lock_irqsave(&uprobes_treelock, flags);
	uprobe = __find_uprobe(inode, offset);
	spin_unlock_irqrestore(&uprobes_treelock, flags);

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
	unsigned long flags;
	struct uprobe *u;

	spin_lock_irqsave(&uprobes_treelock, flags);
	u = __insert_uprobe(uprobe);
	spin_unlock_irqrestore(&uprobes_treelock, flags);

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
	INIT_LIST_HEAD(&uprobe->pending_list);

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

/* Returns the previous consumer */
static struct uprobe_consumer *
consumer_add(struct uprobe *uprobe, struct uprobe_consumer *consumer)
{
	down_write(&uprobe->consumer_rwsem);
	consumer->next = uprobe->consumers;
	uprobe->consumers = consumer;
	up_write(&uprobe->consumer_rwsem);

	return consumer->next;
}

/*
 * For uprobe @uprobe, delete the consumer @consumer.
 * Return true if the @consumer is deleted successfully
 * or return false.
 */
static bool consumer_del(struct uprobe *uprobe, struct uprobe_consumer *consumer)
{
	struct uprobe_consumer **con;
	bool ret = false;

	down_write(&uprobe->consumer_rwsem);
	for (con = &uprobe->consumers; *con; con = &(*con)->next) {
		if (*con == consumer) {
			*con = consumer->next;
			ret = true;
			break;
		}
	}
	up_write(&uprobe->consumer_rwsem);

	return ret;
}

static int __copy_insn(struct address_space *mapping,
			struct vm_area_struct *vma, char *insn,
			unsigned long nbytes, unsigned long offset)
{
	struct file *filp = vma->vm_file;
	struct page *page;
	void *vaddr;
	unsigned long off1;
	unsigned long idx;

	if (!filp)
		return -EINVAL;

	idx = (unsigned long)(offset >> PAGE_CACHE_SHIFT);
	off1 = offset &= ~PAGE_MASK;

	/*
	 * Ensure that the page that has the original instruction is
	 * populated and in page-cache.
	 */
	page = read_mapping_page(mapping, idx, filp);
	if (IS_ERR(page))
		return PTR_ERR(page);

	vaddr = kmap_atomic(page);
	memcpy(insn, vaddr + off1, nbytes);
	kunmap_atomic(vaddr);
	page_cache_release(page);

	return 0;
}

static int copy_insn(struct uprobe *uprobe, struct vm_area_struct *vma, unsigned long addr)
{
	struct address_space *mapping;
	unsigned long nbytes;
	int bytes;

	addr &= ~PAGE_MASK;
	nbytes = PAGE_SIZE - addr;
	mapping = uprobe->inode->i_mapping;

	/* Instruction at end of binary; copy only available bytes */
	if (uprobe->offset + MAX_UINSN_BYTES > uprobe->inode->i_size)
		bytes = uprobe->inode->i_size - uprobe->offset;
	else
		bytes = MAX_UINSN_BYTES;

	/* Instruction at the page-boundary; copy bytes in second page */
	if (nbytes < bytes) {
		if (__copy_insn(mapping, vma, uprobe->arch.insn + nbytes,
				bytes - nbytes, uprobe->offset + nbytes))
			return -ENOMEM;

		bytes = nbytes;
	}
	return __copy_insn(mapping, vma, uprobe->arch.insn, bytes, uprobe->offset);
}

static int install_breakpoint(struct mm_struct *mm, struct uprobe *uprobe,
				struct vm_area_struct *vma, loff_t vaddr)
{
	unsigned long addr;
	int ret;

	/*
	 * If probe is being deleted, unregister thread could be done with
	 * the vma-rmap-walk through. Adding a probe now can be fatal since
	 * nobody will be able to cleanup. Also we could be from fork or
	 * mremap path, where the probe might have already been inserted.
	 * Hence behave as if probe already existed.
	 */
	if (!uprobe->consumers)
		return -EEXIST;

	addr = (unsigned long)vaddr;

	if (!(uprobe->flags & UPROBE_COPY_INSN)) {
		ret = copy_insn(uprobe, vma, addr);
		if (ret)
			return ret;

		if (is_bkpt_insn((uprobe_opcode_t *)uprobe->arch.insn))
			return -EEXIST;

		ret = arch_uprobes_analyze_insn(mm, &uprobe->arch);
		if (ret)
			return ret;

		uprobe->flags |= UPROBE_COPY_INSN;
	}
	ret = set_bkpt(mm, &uprobe->arch, addr);

	return ret;
}

static void remove_breakpoint(struct mm_struct *mm, struct uprobe *uprobe, loff_t vaddr)
{
	set_orig_insn(mm, &uprobe->arch, (unsigned long)vaddr, true);
}

static void delete_uprobe(struct uprobe *uprobe)
{
	unsigned long flags;

	spin_lock_irqsave(&uprobes_treelock, flags);
	rb_erase(&uprobe->rb_node, &uprobes_tree);
	spin_unlock_irqrestore(&uprobes_treelock, flags);
	iput(uprobe->inode);
	put_uprobe(uprobe);
	atomic_dec(&uprobe_events);
}

static struct vma_info *__find_next_vma_info(struct list_head *head,
			loff_t offset, struct address_space *mapping,
			struct vma_info *vi, bool is_register)
{
	struct prio_tree_iter iter;
	struct vm_area_struct *vma;
	struct vma_info *tmpvi;
	unsigned long pgoff;
	int existing_vma;
	loff_t vaddr;

	pgoff = offset >> PAGE_SHIFT;

	vma_prio_tree_foreach(vma, &iter, &mapping->i_mmap, pgoff, pgoff) {
		if (!valid_vma(vma, is_register))
			continue;

		existing_vma = 0;
		vaddr = vma_address(vma, offset);

		list_for_each_entry(tmpvi, head, probe_list) {
			if (tmpvi->mm == vma->vm_mm && tmpvi->vaddr == vaddr) {
				existing_vma = 1;
				break;
			}
		}

		/*
		 * Another vma needs a probe to be installed. However skip
		 * installing the probe if the vma is about to be unlinked.
		 */
		if (!existing_vma && atomic_inc_not_zero(&vma->vm_mm->mm_users)) {
			vi->mm = vma->vm_mm;
			vi->vaddr = vaddr;
			list_add(&vi->probe_list, head);

			return vi;
		}
	}

	return NULL;
}

/*
 * Iterate in the rmap prio tree  and find a vma where a probe has not
 * yet been inserted.
 */
static struct vma_info *
find_next_vma_info(struct list_head *head, loff_t offset, struct address_space *mapping,
		   bool is_register)
{
	struct vma_info *vi, *retvi;

	vi = kzalloc(sizeof(struct vma_info), GFP_KERNEL);
	if (!vi)
		return ERR_PTR(-ENOMEM);

	mutex_lock(&mapping->i_mmap_mutex);
	retvi = __find_next_vma_info(head, offset, mapping, vi, is_register);
	mutex_unlock(&mapping->i_mmap_mutex);

	if (!retvi)
		kfree(vi);

	return retvi;
}

static int register_for_each_vma(struct uprobe *uprobe, bool is_register)
{
	struct list_head try_list;
	struct vm_area_struct *vma;
	struct address_space *mapping;
	struct vma_info *vi, *tmpvi;
	struct mm_struct *mm;
	loff_t vaddr;
	int ret;

	mapping = uprobe->inode->i_mapping;
	INIT_LIST_HEAD(&try_list);

	ret = 0;

	for (;;) {
		vi = find_next_vma_info(&try_list, uprobe->offset, mapping, is_register);
		if (!vi)
			break;

		if (IS_ERR(vi)) {
			ret = PTR_ERR(vi);
			break;
		}

		mm = vi->mm;
		down_read(&mm->mmap_sem);
		vma = find_vma(mm, (unsigned long)vi->vaddr);
		if (!vma || !valid_vma(vma, is_register)) {
			list_del(&vi->probe_list);
			kfree(vi);
			up_read(&mm->mmap_sem);
			mmput(mm);
			continue;
		}
		vaddr = vma_address(vma, uprobe->offset);
		if (vma->vm_file->f_mapping->host != uprobe->inode ||
						vaddr != vi->vaddr) {
			list_del(&vi->probe_list);
			kfree(vi);
			up_read(&mm->mmap_sem);
			mmput(mm);
			continue;
		}

		if (is_register)
			ret = install_breakpoint(mm, uprobe, vma, vi->vaddr);
		else
			remove_breakpoint(mm, uprobe, vi->vaddr);

		up_read(&mm->mmap_sem);
		mmput(mm);
		if (is_register) {
			if (ret && ret == -EEXIST)
				ret = 0;
			if (ret)
				break;
		}
	}

	list_for_each_entry_safe(vi, tmpvi, &try_list, probe_list) {
		list_del(&vi->probe_list);
		kfree(vi);
	}

	return ret;
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
 * @consumer: information on howto handle the probe..
 *
 * Apart from the access refcount, uprobe_register() takes a creation
 * refcount (thro alloc_uprobe) if and only if this @uprobe is getting
 * inserted into the rbtree (i.e first consumer for a @inode:@offset
 * tuple).  Creation refcount stops uprobe_unregister from freeing the
 * @uprobe even before the register operation is complete. Creation
 * refcount is released when the last @consumer for the @uprobe
 * unregisters.
 *
 * Return errno if it cannot successully install probes
 * else return 0 (success)
 */
int uprobe_register(struct inode *inode, loff_t offset, struct uprobe_consumer *consumer)
{
	struct uprobe *uprobe;
	int ret;

	if (!inode || !consumer || consumer->next)
		return -EINVAL;

	if (offset > i_size_read(inode))
		return -EINVAL;

	ret = 0;
	mutex_lock(uprobes_hash(inode));
	uprobe = alloc_uprobe(inode, offset);

	if (uprobe && !consumer_add(uprobe, consumer)) {
		ret = __uprobe_register(uprobe);
		if (ret) {
			uprobe->consumers = NULL;
			__uprobe_unregister(uprobe);
		} else {
			uprobe->flags |= UPROBE_RUN_HANDLER;
		}
	}

	mutex_unlock(uprobes_hash(inode));
	put_uprobe(uprobe);

	return ret;
}

/*
 * uprobe_unregister - unregister a already registered probe.
 * @inode: the file in which the probe has to be removed.
 * @offset: offset from the start of the file.
 * @consumer: identify which probe if multiple probes are colocated.
 */
void uprobe_unregister(struct inode *inode, loff_t offset, struct uprobe_consumer *consumer)
{
	struct uprobe *uprobe;

	if (!inode || !consumer)
		return;

	uprobe = find_uprobe(inode, offset);
	if (!uprobe)
		return;

	mutex_lock(uprobes_hash(inode));

	if (consumer_del(uprobe, consumer)) {
		if (!uprobe->consumers) {
			__uprobe_unregister(uprobe);
			uprobe->flags &= ~UPROBE_RUN_HANDLER;
		}
	}

	mutex_unlock(uprobes_hash(inode));
	if (uprobe)
		put_uprobe(uprobe);
}

/*
 * Of all the nodes that correspond to the given inode, return the node
 * with the least offset.
 */
static struct rb_node *find_least_offset_node(struct inode *inode)
{
	struct uprobe u = { .inode = inode, .offset = 0};
	struct rb_node *n = uprobes_tree.rb_node;
	struct rb_node *close_node = NULL;
	struct uprobe *uprobe;
	int match;

	while (n) {
		uprobe = rb_entry(n, struct uprobe, rb_node);
		match = match_uprobe(&u, uprobe);

		if (uprobe->inode == inode)
			close_node = n;

		if (!match)
			return close_node;

		if (match < 0)
			n = n->rb_left;
		else
			n = n->rb_right;
	}

	return close_node;
}

/*
 * For a given inode, build a list of probes that need to be inserted.
 */
static void build_probe_list(struct inode *inode, struct list_head *head)
{
	struct uprobe *uprobe;
	unsigned long flags;
	struct rb_node *n;

	spin_lock_irqsave(&uprobes_treelock, flags);

	n = find_least_offset_node(inode);

	for (; n; n = rb_next(n)) {
		uprobe = rb_entry(n, struct uprobe, rb_node);
		if (uprobe->inode != inode)
			break;

		list_add(&uprobe->pending_list, head);
		atomic_inc(&uprobe->ref);
	}

	spin_unlock_irqrestore(&uprobes_treelock, flags);
}

/*
 * Called from mmap_region.
 * called with mm->mmap_sem acquired.
 *
 * Return -ve no if we fail to insert probes and we cannot
 * bail-out.
 * Return 0 otherwise. i.e:
 *
 *	- successful insertion of probes
 *	- (or) no possible probes to be inserted.
 *	- (or) insertion of probes failed but we can bail-out.
 */
int uprobe_mmap(struct vm_area_struct *vma)
{
	struct list_head tmp_list;
	struct uprobe *uprobe, *u;
	struct inode *inode;
	int ret;

	if (!atomic_read(&uprobe_events) || !valid_vma(vma, true))
		return 0;

	inode = vma->vm_file->f_mapping->host;
	if (!inode)
		return 0;

	INIT_LIST_HEAD(&tmp_list);
	mutex_lock(uprobes_mmap_hash(inode));
	build_probe_list(inode, &tmp_list);

	ret = 0;

	list_for_each_entry_safe(uprobe, u, &tmp_list, pending_list) {
		loff_t vaddr;

		list_del(&uprobe->pending_list);
		if (!ret) {
			vaddr = vma_address(vma, uprobe->offset);
			if (vaddr >= vma->vm_start && vaddr < vma->vm_end) {
				ret = install_breakpoint(vma->vm_mm, uprobe, vma, vaddr);
				/* Ignore double add: */
				if (ret == -EEXIST)
					ret = 0;
			}
		}
		put_uprobe(uprobe);
	}

	mutex_unlock(uprobes_mmap_hash(inode));

	return ret;
}

static int __init init_uprobes(void)
{
	int i;

	for (i = 0; i < UPROBES_HASH_SZ; i++) {
		mutex_init(&uprobes_mutex[i]);
		mutex_init(&uprobes_mmap_mutex[i]);
	}
	return 0;
}

static void __exit exit_uprobes(void)
{
}

module_init(init_uprobes);
module_exit(exit_uprobes);
