#ifndef LINUX_MM_DEBUG_H
#define LINUX_MM_DEBUG_H 1

struct page;

extern void dump_page(struct page *page, char *reason);
extern void dump_page_badflags(struct page *page, char *reason,
			       unsigned long badflags);

#ifdef CONFIG_DEBUG_VM
#define VM_BUG_ON(cond) BUG_ON(cond)
#define VM_BUG_ON_PAGE(cond, page) \
	do { if (unlikely(cond)) { dump_page(page, NULL); BUG(); } } while (0)
#else
#define VM_BUG_ON(cond) BUILD_BUG_ON_INVALID(cond)
#define VM_BUG_ON_PAGE(cond, page) VM_BUG_ON(cond)
#endif

#ifdef CONFIG_DEBUG_VIRTUAL
#define VIRTUAL_BUG_ON(cond) BUG_ON(cond)
#else
#define VIRTUAL_BUG_ON(cond) do { } while (0)
#endif

#endif
