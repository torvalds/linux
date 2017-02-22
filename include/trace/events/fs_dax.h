#undef TRACE_SYSTEM
#define TRACE_SYSTEM fs_dax

#if !defined(_TRACE_FS_DAX_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_FS_DAX_H

#include <linux/tracepoint.h>

DECLARE_EVENT_CLASS(dax_pmd_fault_class,
	TP_PROTO(struct inode *inode, struct vm_area_struct *vma,
		unsigned long address, unsigned int flags, pgoff_t pgoff,
		pgoff_t max_pgoff, int result),
	TP_ARGS(inode, vma, address, flags, pgoff, max_pgoff, result),
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
		__entry->vm_start = vma->vm_start;
		__entry->vm_end = vma->vm_end;
		__entry->vm_flags = vma->vm_flags;
		__entry->address = address;
		__entry->flags = flags;
		__entry->pgoff = pgoff;
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
	TP_PROTO(struct inode *inode, struct vm_area_struct *vma, \
		unsigned long address, unsigned int flags, pgoff_t pgoff, \
		pgoff_t max_pgoff, int result), \
	TP_ARGS(inode, vma, address, flags, pgoff, max_pgoff, result))

DEFINE_PMD_FAULT_EVENT(dax_pmd_fault);
DEFINE_PMD_FAULT_EVENT(dax_pmd_fault_done);


#endif /* _TRACE_FS_DAX_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
