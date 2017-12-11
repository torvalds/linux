/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_MM_DEBUG_H
#define LINUX_MM_DEBUG_H 1

#include <linux/bug.h>
#include <linux/stringify.h>

struct page;
struct vm_area_struct;
struct mm_struct;

extern void dump_page(struct page *page, const char *reason);
extern void __dump_page(struct page *page, const char *reason);
void dump_vma(const struct vm_area_struct *vma);
void dump_mm(const struct mm_struct *mm);

#ifdef CONFIG_DEBUG_VM
#define VM_BUG_ON(cond) BUG_ON(cond)
#define VM_BUG_ON_PAGE(cond, page)					\
	do {								\
		if (unlikely(cond)) {					\
			dump_page(page, "VM_BUG_ON_PAGE(" __stringify(cond)")");\
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
#define VM_WARN_ON(cond) WARN_ON(cond)
#define VM_WARN_ON_ONCE(cond) WARN_ON_ONCE(cond)
#define VM_WARN_ONCE(cond, format...) WARN_ONCE(cond, format)
#define VM_WARN(cond, format...) WARN(cond, format)
#else
#define VM_BUG_ON(cond) BUILD_BUG_ON_INVALID(cond)
#define VM_BUG_ON_PAGE(cond, page) VM_BUG_ON(cond)
#define VM_BUG_ON_VMA(cond, vma) VM_BUG_ON(cond)
#define VM_BUG_ON_MM(cond, mm) VM_BUG_ON(cond)
#define VM_WARN_ON(cond) BUILD_BUG_ON_INVALID(cond)
#define VM_WARN_ON_ONCE(cond) BUILD_BUG_ON_INVALID(cond)
#define VM_WARN_ONCE(cond, format...) BUILD_BUG_ON_INVALID(cond)
#define VM_WARN(cond, format...) BUILD_BUG_ON_INVALID(cond)
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
