/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_MM_DE_H
#define LINUX_MM_DE_H 1

#include <linux/.h>
#include <linux/stringify.h>

struct page;
struct vm_area_struct;
struct mm_struct;

extern void dump_page(struct page *page, const char *reason);
extern void __dump_page(struct page *page, const char *reason);
void dump_vma(const struct vm_area_struct *vma);
void dump_mm(const struct mm_struct *mm);

#ifdef CONFIG_DE_VM
#define VM__ON(cond) _ON(cond)
#define VM__ON_PAGE(cond, page)					\
	do {								\
		if (unlikely(cond)) {					\
			dump_page(page, "VM__ON_PAGE(" __stringify(cond)")");\
			();						\
		}							\
	} while (0)
#define VM__ON_VMA(cond, vma)					\
	do {								\
		if (unlikely(cond)) {					\
			dump_vma(vma);					\
			();						\
		}							\
	} while (0)
#define VM__ON_MM(cond, mm)						\
	do {								\
		if (unlikely(cond)) {					\
			dump_mm(mm);					\
			();						\
		}							\
	} while (0)
#define VM_WARN_ON(cond) (void)WARN_ON(cond)
#define VM_WARN_ON_ONCE(cond) (void)WARN_ON_ONCE(cond)
#define VM_WARN_ONCE(cond, format...) (void)WARN_ONCE(cond, format)
#define VM_WARN(cond, format...) (void)WARN(cond, format)
#else
#define VM__ON(cond) BUILD__ON_INVALID(cond)
#define VM__ON_PAGE(cond, page) VM__ON(cond)
#define VM__ON_VMA(cond, vma) VM__ON(cond)
#define VM__ON_MM(cond, mm) VM__ON(cond)
#define VM_WARN_ON(cond) BUILD__ON_INVALID(cond)
#define VM_WARN_ON_ONCE(cond) BUILD__ON_INVALID(cond)
#define VM_WARN_ONCE(cond, format...) BUILD__ON_INVALID(cond)
#define VM_WARN(cond, format...) BUILD__ON_INVALID(cond)
#endif

#ifdef CONFIG_DE_VIRTUAL
#define VIRTUAL__ON(cond) _ON(cond)
#else
#define VIRTUAL__ON(cond) do { } while (0)
#endif

#ifdef CONFIG_DE_VM_PGFLAGS
#define VM__ON_PGFLAGS(cond, page) VM__ON_PAGE(cond, page)
#else
#define VM__ON_PGFLAGS(cond, page) BUILD__ON_INVALID(cond)
#endif

#endif
