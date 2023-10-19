/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM fs_dax

#if !defined(_TRACE_FS_DAX_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_FS_DAX_H

#include <linux/tracepoint.h>

DECLARE_EVENT_CLASS(dax_pmd_fault_class,
	TP_PROTO(struct inode *inode, struct vm_fault *vmf,
		pgoff_t max_pgoff, int result),
	TP_ARGS(inode, vmf, max_pgoff, result),
	TP_STRUCT__entry(
		__field(unsigned long, ino)
		__field(unsigned long, vm_start)
		__field(unsigned long, vm_end)
		__field(unsigned long, vm_flags)
		__field(unsigned long, address)
		__field(pgoff_t, pgoff)
		__field(pgoff_t, max_pgoff)
		__field(dev_t, dev)
		__field(unsigned int, flags)
		__field(int, result)
	),
	TP_fast_assign(
		__entry->dev = inode->i_sb->s_dev;
		__entry->ino = inode->i_ino;
		__entry->vm_start = vmf->vma->vm_start;
		__entry->vm_end = vmf->vma->vm_end;
		__entry->vm_flags = vmf->vma->vm_flags;
		__entry->address = vmf->address;
		__entry->flags = vmf->flags;
		__entry->pgoff = vmf->pgoff;
		__entry->max_pgoff = max_pgoff;
		__entry->result = result;
	),
	TP_printk("dev %d:%d ino %#lx %s %s address %#lx vm_start "
			"%#lx vm_end %#lx pgoff %#lx max_pgoff %#lx %s",
		MAJOR(__entry->dev),
		MINOR(__entry->dev),
		__entry->ino,
		__entry->vm_flags & VM_SHARED ? "shared" : "private",
		__print_flags(__entry->flags, "|", FAULT_FLAG_TRACE),
		__entry->address,
		__entry->vm_start,
		__entry->vm_end,
		__entry->pgoff,
		__entry->max_pgoff,
		__print_flags(__entry->result, "|", VM_FAULT_RESULT_TRACE)
	)
)

#define DEFINE_PMD_FAULT_EVENT(name) \
DEFINE_EVENT(dax_pmd_fault_class, name, \
	TP_PROTO(struct inode *inode, struct vm_fault *vmf, \
		pgoff_t max_pgoff, int result), \
	TP_ARGS(inode, vmf, max_pgoff, result))

DEFINE_PMD_FAULT_EVENT(dax_pmd_fault);
DEFINE_PMD_FAULT_EVENT(dax_pmd_fault_done);

DECLARE_EVENT_CLASS(dax_pmd_load_hole_class,
	TP_PROTO(struct inode *inode, struct vm_fault *vmf,
		struct page *zero_page,
		void *radix_entry),
	TP_ARGS(inode, vmf, zero_page, radix_entry),
	TP_STRUCT__entry(
		__field(unsigned long, ino)
		__field(unsigned long, vm_flags)
		__field(unsigned long, address)
		__field(struct page *, zero_page)
		__field(void *, radix_entry)
		__field(dev_t, dev)
	),
	TP_fast_assign(
		__entry->dev = inode->i_sb->s_dev;
		__entry->ino = inode->i_ino;
		__entry->vm_flags = vmf->vma->vm_flags;
		__entry->address = vmf->address;
		__entry->zero_page = zero_page;
		__entry->radix_entry = radix_entry;
	),
	TP_printk("dev %d:%d ino %#lx %s address %#lx zero_page %p "
			"radix_entry %#lx",
		MAJOR(__entry->dev),
		MINOR(__entry->dev),
		__entry->ino,
		__entry->vm_flags & VM_SHARED ? "shared" : "private",
		__entry->address,
		__entry->zero_page,
		(unsigned long)__entry->radix_entry
	)
)

#define DEFINE_PMD_LOAD_HOLE_EVENT(name) \
DEFINE_EVENT(dax_pmd_load_hole_class, name, \
	TP_PROTO(struct inode *inode, struct vm_fault *vmf, \
		struct page *zero_page, void *radix_entry), \
	TP_ARGS(inode, vmf, zero_page, radix_entry))

DEFINE_PMD_LOAD_HOLE_EVENT(dax_pmd_load_hole);
DEFINE_PMD_LOAD_HOLE_EVENT(dax_pmd_load_hole_fallback);

DECLARE_EVENT_CLASS(dax_pmd_insert_mapping_class,
	TP_PROTO(struct inode *inode, struct vm_fault *vmf,
		long length, pfn_t pfn, void *radix_entry),
	TP_ARGS(inode, vmf, length, pfn, radix_entry),
	TP_STRUCT__entry(
		__field(unsigned long, ino)
		__field(unsigned long, vm_flags)
		__field(unsigned long, address)
		__field(long, length)
		__field(u64, pfn_val)
		__field(void *, radix_entry)
		__field(dev_t, dev)
		__field(int, write)
	),
	TP_fast_assign(
		__entry->dev = inode->i_sb->s_dev;
		__entry->ino = inode->i_ino;
		__entry->vm_flags = vmf->vma->vm_flags;
		__entry->address = vmf->address;
		__entry->write = vmf->flags & FAULT_FLAG_WRITE;
		__entry->length = length;
		__entry->pfn_val = pfn.val;
		__entry->radix_entry = radix_entry;
	),
	TP_printk("dev %d:%d ino %#lx %s %s address %#lx length %#lx "
			"pfn %#llx %s radix_entry %#lx",
		MAJOR(__entry->dev),
		MINOR(__entry->dev),
		__entry->ino,
		__entry->vm_flags & VM_SHARED ? "shared" : "private",
		__entry->write ? "write" : "read",
		__entry->address,
		__entry->length,
		__entry->pfn_val & ~PFN_FLAGS_MASK,
		__print_flags_u64(__entry->pfn_val & PFN_FLAGS_MASK, "|",
			PFN_FLAGS_TRACE),
		(unsigned long)__entry->radix_entry
	)
)

#define DEFINE_PMD_INSERT_MAPPING_EVENT(name) \
DEFINE_EVENT(dax_pmd_insert_mapping_class, name, \
	TP_PROTO(struct inode *inode, struct vm_fault *vmf, \
		long length, pfn_t pfn, void *radix_entry), \
	TP_ARGS(inode, vmf, length, pfn, radix_entry))

DEFINE_PMD_INSERT_MAPPING_EVENT(dax_pmd_insert_mapping);

DECLARE_EVENT_CLASS(dax_pte_fault_class,
	TP_PROTO(struct inode *inode, struct vm_fault *vmf, int result),
	TP_ARGS(inode, vmf, result),
	TP_STRUCT__entry(
		__field(unsigned long, ino)
		__field(unsigned long, vm_flags)
		__field(unsigned long, address)
		__field(pgoff_t, pgoff)
		__field(dev_t, dev)
		__field(unsigned int, flags)
		__field(int, result)
	),
	TP_fast_assign(
		__entry->dev = inode->i_sb->s_dev;
		__entry->ino = inode->i_ino;
		__entry->vm_flags = vmf->vma->vm_flags;
		__entry->address = vmf->address;
		__entry->flags = vmf->flags;
		__entry->pgoff = vmf->pgoff;
		__entry->result = result;
	),
	TP_printk("dev %d:%d ino %#lx %s %s address %#lx pgoff %#lx %s",
		MAJOR(__entry->dev),
		MINOR(__entry->dev),
		__entry->ino,
		__entry->vm_flags & VM_SHARED ? "shared" : "private",
		__print_flags(__entry->flags, "|", FAULT_FLAG_TRACE),
		__entry->address,
		__entry->pgoff,
		__print_flags(__entry->result, "|", VM_FAULT_RESULT_TRACE)
	)
)

#define DEFINE_PTE_FAULT_EVENT(name) \
DEFINE_EVENT(dax_pte_fault_class, name, \
	TP_PROTO(struct inode *inode, struct vm_fault *vmf, int result), \
	TP_ARGS(inode, vmf, result))

DEFINE_PTE_FAULT_EVENT(dax_pte_fault);
DEFINE_PTE_FAULT_EVENT(dax_pte_fault_done);
DEFINE_PTE_FAULT_EVENT(dax_load_hole);
DEFINE_PTE_FAULT_EVENT(dax_insert_pfn_mkwrite_no_entry);
DEFINE_PTE_FAULT_EVENT(dax_insert_pfn_mkwrite);

TRACE_EVENT(dax_insert_mapping,
	TP_PROTO(struct inode *inode, struct vm_fault *vmf, void *radix_entry),
	TP_ARGS(inode, vmf, radix_entry),
	TP_STRUCT__entry(
		__field(unsigned long, ino)
		__field(unsigned long, vm_flags)
		__field(unsigned long, address)
		__field(void *, radix_entry)
		__field(dev_t, dev)
		__field(int, write)
	),
	TP_fast_assign(
		__entry->dev = inode->i_sb->s_dev;
		__entry->ino = inode->i_ino;
		__entry->vm_flags = vmf->vma->vm_flags;
		__entry->address = vmf->address;
		__entry->write = vmf->flags & FAULT_FLAG_WRITE;
		__entry->radix_entry = radix_entry;
	),
	TP_printk("dev %d:%d ino %#lx %s %s address %#lx radix_entry %#lx",
		MAJOR(__entry->dev),
		MINOR(__entry->dev),
		__entry->ino,
		__entry->vm_flags & VM_SHARED ? "shared" : "private",
		__entry->write ? "write" : "read",
		__entry->address,
		(unsigned long)__entry->radix_entry
	)
)

DECLARE_EVENT_CLASS(dax_writeback_range_class,
	TP_PROTO(struct inode *inode, pgoff_t start_index, pgoff_t end_index),
	TP_ARGS(inode, start_index, end_index),
	TP_STRUCT__entry(
		__field(unsigned long, ino)
		__field(pgoff_t, start_index)
		__field(pgoff_t, end_index)
		__field(dev_t, dev)
	),
	TP_fast_assign(
		__entry->dev = inode->i_sb->s_dev;
		__entry->ino = inode->i_ino;
		__entry->start_index = start_index;
		__entry->end_index = end_index;
	),
	TP_printk("dev %d:%d ino %#lx pgoff %#lx-%#lx",
		MAJOR(__entry->dev),
		MINOR(__entry->dev),
		__entry->ino,
		__entry->start_index,
		__entry->end_index
	)
)

#define DEFINE_WRITEBACK_RANGE_EVENT(name) \
DEFINE_EVENT(dax_writeback_range_class, name, \
	TP_PROTO(struct inode *inode, pgoff_t start_index, pgoff_t end_index),\
	TP_ARGS(inode, start_index, end_index))

DEFINE_WRITEBACK_RANGE_EVENT(dax_writeback_range);
DEFINE_WRITEBACK_RANGE_EVENT(dax_writeback_range_done);

TRACE_EVENT(dax_writeback_one,
	TP_PROTO(struct inode *inode, pgoff_t pgoff, pgoff_t pglen),
	TP_ARGS(inode, pgoff, pglen),
	TP_STRUCT__entry(
		__field(unsigned long, ino)
		__field(pgoff_t, pgoff)
		__field(pgoff_t, pglen)
		__field(dev_t, dev)
	),
	TP_fast_assign(
		__entry->dev = inode->i_sb->s_dev;
		__entry->ino = inode->i_ino;
		__entry->pgoff = pgoff;
		__entry->pglen = pglen;
	),
	TP_printk("dev %d:%d ino %#lx pgoff %#lx pglen %#lx",
		MAJOR(__entry->dev),
		MINOR(__entry->dev),
		__entry->ino,
		__entry->pgoff,
		__entry->pglen
	)
)

#endif /* _TRACE_FS_DAX_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
