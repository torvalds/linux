/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_MM_DEBUG_H
#define LINUX_MM_DEBUG_H 1

#include <linux/bug.h>
#include <linux/stringify.h>

struct page;
struct vm_area_struct;
struct mm_struct;
struct vma_iterator;
struct vma_merge_struct;

void dump_page(const struct page *page, const char *reason);
void dump_vma(const struct vm_area_struct *vma);
void dump_mm(const struct mm_struct *mm);
void dump_vmg(const struct vma_merge_struct *vmg, const char *reason);
void vma_iter_dump_tree(const struct vma_iterator *vmi);

#ifdef CONFIG_DEBUG_VM
#define VM_BUG_ON(cond) BUG_ON(cond)
#define VM_BUG_ON_PAGE(cond, page)					\
	do {								\
		if (unlikely(cond)) {					\
			dump_page(page, "VM_BUG_ON_PAGE(" __stringify(cond)")");\
			BUG();						\
		}							\
	} while (0)
#define VM_BUG_ON_FOLIO(cond, folio)					\
	do {								\
		if (unlikely(cond)) {					\
			dump_page(&folio->page, "VM_BUG_ON_FOLIO(" __stringify(cond)")");\
			BUG();						\
		}							\
	} while (0)
#define VM_BUG_ON_VMA(cond, vma)					\
	do {								\
		if (unlikely(cond)) {					\
			dump_vma(vma);					\
			BUG();						\
		}							\
	} while (0)
#define VM_BUG_ON_MM(cond, mm)						\
	do {								\
		if (unlikely(cond)) {					\
			dump_mm(mm);					\
			BUG();						\
		}							\
	} while (0)
#define VM_WARN_ON_ONCE_PAGE(cond, page)	({			\
	static bool __section(".data..once") __warned;			\
	int __ret_warn_once = !!(cond);					\
									\
	if (unlikely(__ret_warn_once && !__warned)) {			\
		dump_page(page, "VM_WARN_ON_ONCE_PAGE(" __stringify(cond)")");\
		__warned = true;					\
		WARN_ON(1);						\
	}								\
	unlikely(__ret_warn_once);					\
})
#define VM_WARN_ON_FOLIO(cond, folio)		({			\
	int __ret_warn = !!(cond);					\
									\
	if (unlikely(__ret_warn)) {					\
		dump_page(&folio->page, "VM_WARN_ON_FOLIO(" __stringify(cond)")");\
		WARN_ON(1);						\
	}								\
	unlikely(__ret_warn);						\
})
#define VM_WARN_ON_ONCE_FOLIO(cond, folio)	({			\
	static bool __section(".data..once") __warned;			\
	int __ret_warn_once = !!(cond);					\
									\
	if (unlikely(__ret_warn_once && !__warned)) {			\
		dump_page(&folio->page, "VM_WARN_ON_ONCE_FOLIO(" __stringify(cond)")");\
		__warned = true;					\
		WARN_ON(1);						\
	}								\
	unlikely(__ret_warn_once);					\
})
#define VM_WARN_ON_ONCE_MM(cond, mm)		({			\
	static bool __section(".data..once") __warned;			\
	int __ret_warn_once = !!(cond);					\
									\
	if (unlikely(__ret_warn_once && !__warned)) {			\
		dump_mm(mm);						\
		__warned = true;					\
		WARN_ON(1);						\
	}								\
	unlikely(__ret_warn_once);					\
})
#define VM_WARN_ON_VMG(cond, vmg)		({			\
	int __ret_warn = !!(cond);					\
									\
	if (unlikely(__ret_warn)) {					\
		dump_vmg(vmg, "VM_WARN_ON_VMG(" __stringify(cond)")");	\
		WARN_ON(1);						\
	}								\
	unlikely(__ret_warn);						\
})

#define VM_WARN_ON(cond) (void)WARN_ON(cond)
#define VM_WARN_ON_ONCE(cond) (void)WARN_ON_ONCE(cond)
#define VM_WARN_ONCE(cond, format...) (void)WARN_ONCE(cond, format)
#define VM_WARN(cond, format...) (void)WARN(cond, format)
#else
#define VM_BUG_ON(cond) BUILD_BUG_ON_INVALID(cond)
#define VM_BUG_ON_PAGE(cond, page) VM_BUG_ON(cond)
#define VM_BUG_ON_FOLIO(cond, folio) VM_BUG_ON(cond)
#define VM_BUG_ON_VMA(cond, vma) VM_BUG_ON(cond)
#define VM_BUG_ON_MM(cond, mm) VM_BUG_ON(cond)
#define VM_WARN_ON(cond) BUILD_BUG_ON_INVALID(cond)
#define VM_WARN_ON_ONCE(cond) BUILD_BUG_ON_INVALID(cond)
#define VM_WARN_ON_ONCE_PAGE(cond, page)  BUILD_BUG_ON_INVALID(cond)
#define VM_WARN_ON_FOLIO(cond, folio)  BUILD_BUG_ON_INVALID(cond)
#define VM_WARN_ON_ONCE_FOLIO(cond, folio)  BUILD_BUG_ON_INVALID(cond)
#define VM_WARN_ON_ONCE_MM(cond, mm)  BUILD_BUG_ON_INVALID(cond)
#define VM_WARN_ON_VMG(cond, vmg)  BUILD_BUG_ON_INVALID(cond)
#define VM_WARN_ONCE(cond, format...) BUILD_BUG_ON_INVALID(cond)
#define VM_WARN(cond, format...) BUILD_BUG_ON_INVALID(cond)
#endif /* CONFIG_DEBUG_VM */

#ifdef CONFIG_DEBUG_VM_IRQSOFF
#define VM_WARN_ON_IRQS_ENABLED() WARN_ON_ONCE(!irqs_disabled())
#else
#define VM_WARN_ON_IRQS_ENABLED() do { } while (0)
#endif

#ifdef CONFIG_DEBUG_VIRTUAL
#define VIRTUAL_BUG_ON(cond) BUG_ON(cond)
#else
#define VIRTUAL_BUG_ON(cond) do { } while (0)
#endif

#ifdef CONFIG_DEBUG_VM_PGFLAGS
#define VM_BUG_ON_PGFLAGS(cond, page) VM_BUG_ON_PAGE(cond, page)
#else
#define VM_BUG_ON_PGFLAGS(cond, page) BUILD_BUG_ON_INVALID(cond)
#endif

#endif
